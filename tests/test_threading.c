/* ResourceGuard threading tests.
 *
 * Verifies safe-mode invariants, TOCTOU race demonstrations, event
 * lifecycle, configuration validation, and API robustness.
 *
 * Run from the project root:  make test_threads
 *
 * No external test framework.  Same CHECK macro as test_banker.c.
 *
 * IMPORTANT — Logical TOCTOU race vs C memory data race:
 *
 *   A C memory data race (undefined behaviour, C11 §5.1.2.4) occurs
 *   when two threads access the same non-atomic memory location
 *   concurrently and at least one is a write, with NO synchronization
 *   ordering the accesses.
 *
 *   A logical TOCTOU race occurs when a thread makes a decision based
 *   on state that may have changed by the time it acts.  Every
 *   individual memory access IS mutex-protected, so there is no
 *   undefined behaviour.  The flaw is at the algorithm level: the
 *   safety CHECK and the resource COMMIT are not performed as one
 *   atomic transaction.
 *
 *   The deterministic test below (test_toctou_deterministic) uses
 *   pthread barriers to guarantee the race occurs without relying
 *   on timing or sleep.  The probabilistic test (test_toctou_via_api)
 *   exercises the tsim module's built-in TOCTOU mode.                */

#define _POSIX_C_SOURCE 200809L

#include "threading.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

/* ================================================================== */
/*  Test infrastructure                                                */
/* ================================================================== */

static int checks = 0, failures = 0;

#define CHECK(cond) do { checks++; if (!(cond)) { failures++; \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

/* Build the classic 5-process, 3-resource demo state in memory.
 * Avoids a file-system dependency for self-contained tests. */
static void init_demo(BankerState *s) {
    banker_init(s, 5, 3);
    s->total[0] = 10; s->total[1] = 5; s->total[2] = 7;
    s->available[0] = 3; s->available[1] = 3; s->available[2] = 2;

    int al[][3] = {{0,1,0},{2,0,0},{3,0,2},{2,1,1},{0,0,2}};
    int mx[][3] = {{7,5,3},{3,2,2},{9,0,2},{2,2,2},{4,3,3}};
    for (int i = 0; i < 5; i++)
        for (int j = 0; j < 3; j++) {
            s->alloc[i][j] = al[i][j];
            s->max[i][j]   = mx[i][j];
        }
}

/* Conservation check: Available[j] + Σ Alloc[i][j] == Total[j]. */
static bool conservation_holds(const BankerState *s) {
    for (int j = 0; j < s->m; j++) {
        long long sum = s->available[j];
        for (int i = 0; i < s->n; i++) sum += s->alloc[i][j];
        if (sum != s->total[j]) return false;
    }
    return true;
}

/* ================================================================== */
/*  Test 1 — Invalid configuration handling                            */
/* ================================================================== */

static void test_invalid_config(void) {
    BankerState demo;
    init_demo(&demo);
    ThreadSimConfig cfg = {
        .active_processes = 3, .requests_per_process = 10,
        .seed = 42, .think_time_us = 0, .gap_time_us = 0,
        .release_pct = 50
    };

    /* NULL pointers */
    CHECK(tsim_create(NULL, &cfg, TSIM_MODE_SAFE) == NULL);
    CHECK(tsim_create(&demo, NULL, TSIM_MODE_SAFE) == NULL);

    /* Invalid BankerState */
    BankerState bad = demo;
    bad.available[0] = -1;
    CHECK(tsim_create(&bad, &cfg, TSIM_MODE_SAFE) == NULL);

    /* active_processes out of range */
    ThreadSimConfig c2 = cfg;
    c2.active_processes = 0;
    CHECK(tsim_create(&demo, &c2, TSIM_MODE_SAFE) == NULL);
    c2.active_processes = demo.n + 1;
    CHECK(tsim_create(&demo, &c2, TSIM_MODE_SAFE) == NULL);

    /* requests_per_process < 1 */
    c2 = cfg;
    c2.requests_per_process = 0;
    CHECK(tsim_create(&demo, &c2, TSIM_MODE_SAFE) == NULL);

    /* release_pct out of range */
    c2 = cfg;
    c2.release_pct = -1;
    CHECK(tsim_create(&demo, &c2, TSIM_MODE_SAFE) == NULL);
    c2.release_pct = 101;
    CHECK(tsim_create(&demo, &c2, TSIM_MODE_SAFE) == NULL);

    /* Valid config succeeds */
    ThreadSim *sim = tsim_create(&demo, &cfg, TSIM_MODE_SAFE);
    CHECK(sim != NULL);
    tsim_destroy(sim);
}

/* ================================================================== */
/*  Test 2 — API robustness: NULL and out-of-range access              */
/* ================================================================== */

static void test_api_robustness(void) {
    /* NULL-safe functions */
    tsim_destroy(NULL);                                /* must not crash */
    tsim_set_callback(NULL, NULL, NULL);               /* must not crash */
    CHECK(tsim_event_count(NULL) == 0);
    CHECK(tsim_event_at(NULL, 0) == NULL);
    ThreadSimResults empty = tsim_results(NULL);
    CHECK(empty.total_requests == 0);

    /* Create a valid sim, check bounds */
    BankerState demo;
    init_demo(&demo);
    ThreadSimConfig cfg = {
        .active_processes = 2, .requests_per_process = 5,
        .seed = 1, .think_time_us = 0, .gap_time_us = 0,
        .release_pct = 0
    };
    ThreadSim *sim = tsim_create(&demo, &cfg, TSIM_MODE_SAFE);
    CHECK(sim != NULL);

    /* Before run: no events */
    CHECK(tsim_event_count(sim) == 0);
    CHECK(tsim_event_at(sim, 0) == NULL);

    /* Run */
    CHECK(tsim_run(sim) == true);

    /* Second run must fail */
    CHECK(tsim_run(sim) == false);

    /* Out-of-range event access */
    int n = tsim_event_count(sim);
    CHECK(n > 0);
    CHECK(tsim_event_at(sim, -1) == NULL);
    CHECK(tsim_event_at(sim, n) == NULL);
    CHECK(tsim_event_at(sim, 0) != NULL);
    CHECK(tsim_event_at(sim, n - 1) != NULL);

    tsim_destroy(sim);
}

/* ================================================================== */
/*  Test 3 — Safe mode: basic operation with multiple threads          */
/* ================================================================== */

static void test_safe_mode_basic(void) {
    BankerState demo;
    init_demo(&demo);
    ThreadSimConfig cfg = {
        .active_processes = 5, .requests_per_process = 50,
        .seed = 0xDEADBEEF, .think_time_us = 0, .gap_time_us = 0,
        .release_pct = 40
    };

    ThreadSim *sim = tsim_create(&demo, &cfg, TSIM_MODE_SAFE);
    CHECK(sim != NULL);
    CHECK(tsim_run(sim) == true);

    ThreadSimResults r = tsim_results(sim);

    /* Must have made requests and gotten some granted. */
    CHECK(r.total_requests > 0);
    CHECK(r.granted > 0);

    /* Safe mode: NO invariant violations ever. */
    CHECK(r.invariant_violations == 0);
    CHECK(r.unsafe_states_observed == 0);

    /* Final state must be valid and safe. */
    CHECK(r.final_state_valid);
    CHECK(banker_validate(&r.final_state, NULL, NULL) == BANKER_OK);

    printf("  safe basic: %d requests, %d granted, %d denied, %d wait, %d releases\n",
           r.total_requests, r.granted, r.denied_unsafe,
           r.denied_wait, r.releases);

    tsim_destroy(sim);
}

/* ================================================================== */
/*  Test 4 — Safe mode: conservation and safety invariants             */
/* ================================================================== */

typedef struct {
    pthread_mutex_t mtx;
    int negative_available;   /* GRANTED events with negative Available   */
    int total_granted;        /* total GRANTED events seen by callback    */
} InvariantCbCtx;

static void invariant_callback(const ThreadSimEvent *ev, void *data) {
    InvariantCbCtx *ctx = data;
    if (ev->type == TSIM_GRANTED) {
        pthread_mutex_lock(&ctx->mtx);
        ctx->total_granted++;
        for (int j = 0; j < ev->m; j++) {
            if (ev->available[j] < 0) {
                ctx->negative_available++;
                pthread_mutex_unlock(&ctx->mtx);
                return;
            }
        }
        pthread_mutex_unlock(&ctx->mtx);
    }
}

static void test_safe_mode_invariants(void) {
    BankerState demo;
    init_demo(&demo);
    ThreadSimConfig cfg = {
        .active_processes = 5, .requests_per_process = 100,
        .seed = 0x12345678, .think_time_us = 0, .gap_time_us = 0,
        .release_pct = 50
    };

    ThreadSim *sim = tsim_create(&demo, &cfg, TSIM_MODE_SAFE);
    CHECK(sim != NULL);

    InvariantCbCtx cb = { .negative_available = 0, .total_granted = 0 };
    pthread_mutex_init(&cb.mtx, NULL);
    tsim_set_callback(sim, invariant_callback, &cb);
    CHECK(tsim_run(sim) == true);

    /* Callback: Available was never negative after any grant. */
    pthread_mutex_lock(&cb.mtx);
    CHECK(cb.total_granted > 0);
    CHECK(cb.negative_available == 0);
    int total_grants_seen = cb.total_granted;
    pthread_mutex_unlock(&cb.mtx);
    pthread_mutex_destroy(&cb.mtx);

    /* Results match callback count. */
    ThreadSimResults r = tsim_results(sim);
    CHECK(r.granted == total_grants_seen);
    CHECK(r.invariant_violations == 0);
    CHECK(r.unsafe_states_observed == 0);

    /* Final state: conservation holds. */
    CHECK(r.final_state_valid);
    CHECK(conservation_holds(&r.final_state));

    /* Final state: every Alloc[i][j] <= Max[i][j]. */
    for (int i = 0; i < r.final_state.n; i++)
        for (int j = 0; j < r.final_state.m; j++)
            CHECK(r.final_state.alloc[i][j] <= r.final_state.max[i][j]);

    /* Scan event log: every GRANTED has non-negative Available. */
    int log_grants = 0;
    for (int k = 0; k < tsim_event_count(sim); k++) {
        const ThreadSimEvent *ev = tsim_event_at(sim, k);
        if (ev->type == TSIM_GRANTED) {
            log_grants++;
            for (int j = 0; j < ev->m; j++)
                CHECK(ev->available[j] >= 0);
        }
        /* No violation events whatsoever. */
        CHECK(ev->type != TSIM_VIOLATION_INVALID);
        CHECK(ev->type != TSIM_VIOLATION_UNSAFE);
    }
    CHECK(log_grants == r.granted);

    printf("  safe invariants: %d grants checked, conservation OK\n",
           log_grants);

    tsim_destroy(sim);
}

/* ================================================================== */
/*  Test 5 — Event lifecycle: start/end counts                         */
/* ================================================================== */

static void test_event_lifecycle(void) {
    BankerState demo;
    init_demo(&demo);
    int np = 4;
    ThreadSimConfig cfg = {
        .active_processes = np, .requests_per_process = 20,
        .seed = 7, .think_time_us = 0, .gap_time_us = 0,
        .release_pct = 30
    };

    ThreadSim *sim = tsim_create(&demo, &cfg, TSIM_MODE_SAFE);
    CHECK(sim != NULL);
    CHECK(tsim_run(sim) == true);

    int starts[BANKER_MAX_PROCS] = {0};
    int ends[BANKER_MAX_PROCS]   = {0};
    int first_start[BANKER_MAX_PROCS];
    int last_end[BANKER_MAX_PROCS];
    memset(first_start, -1, sizeof first_start);
    memset(last_end,    -1, sizeof last_end);

    int total = tsim_event_count(sim);
    for (int k = 0; k < total; k++) {
        const ThreadSimEvent *ev = tsim_event_at(sim, k);
        int p = ev->process_id;
        CHECK(p >= 0 && p < np);
        if (ev->type == TSIM_THREAD_START) {
            starts[p]++;
            if (first_start[p] < 0) first_start[p] = k;
        }
        if (ev->type == TSIM_THREAD_END) {
            ends[p]++;
            last_end[p] = k;
        }
    }

    /* Exactly one START and one END per process. */
    for (int p = 0; p < np; p++) {
        CHECK(starts[p] == 1);
        CHECK(ends[p] == 1);
    }

    /* START comes before END for each process. */
    for (int p = 0; p < np; p++) {
        CHECK(first_start[p] >= 0);
        CHECK(last_end[p] >= 0);
        CHECK(first_start[p] < last_end[p]);
    }

    /* Event IDs strictly match index k (monotonic 0, 1, 2...). */
    for (int k = 0; k < total; k++) {
        const ThreadSimEvent *ev = tsim_event_at(sim, k);
        CHECK(ev->event_id == (uint64_t)k);
    }

    /* Timestamps are monotonically non-decreasing in log order. */
    for (int k = 1; k < total; k++) {
        const ThreadSimEvent *a = tsim_event_at(sim, k - 1);
        const ThreadSimEvent *b = tsim_event_at(sim, k);
        CHECK(b->timestamp_us >= a->timestamp_us);
    }

    printf("  lifecycle: %d events, %d processes, all matched\n", total, np);
    tsim_destroy(sim);
}

/* ================================================================== */
/*  Test 6 — Release when fully allocated                              */
/* ================================================================== */

static void test_release_on_full_alloc(void) {
    /* Create a state where P0 is very close to Max. */
    BankerState s;
    banker_init(&s, 2, 2);
    s.total[0] = 4; s.total[1] = 4;
    s.available[0] = 1; s.available[1] = 1;
    s.alloc[0][0] = 2; s.alloc[0][1] = 2;
    s.max[0][0]   = 3; s.max[0][1]   = 3;
    s.alloc[1][0] = 1; s.alloc[1][1] = 1;
    s.max[1][0]   = 2; s.max[1][1]   = 2;
    CHECK(banker_validate(&s, NULL, NULL) == BANKER_OK);

    ThreadSimConfig cfg = {
        .active_processes = 2, .requests_per_process = 40,
        .seed = 99, .think_time_us = 0, .gap_time_us = 0,
        .release_pct = 80    /* high release probability */
    };

    ThreadSim *sim = tsim_create(&s, &cfg, TSIM_MODE_SAFE);
    CHECK(sim != NULL);
    CHECK(tsim_run(sim) == true);

    ThreadSimResults r = tsim_results(sim);
    CHECK(r.releases > 0);
    CHECK(r.final_state_valid);
    CHECK(conservation_holds(&r.final_state));

    /* At least some RELEASE events in the log. */
    int rel_count = 0;
    for (int k = 0; k < tsim_event_count(sim); k++)
        if (tsim_event_at(sim, k)->type == TSIM_RELEASE)
            rel_count++;
    CHECK(rel_count == r.releases);

    printf("  release: %d grants, %d releases\n", r.granted, r.releases);
    tsim_destroy(sim);
}

/* ================================================================== */
/*  Test 7 — Final state validation                                    */
/* ================================================================== */

static void test_final_state(void) {
    BankerState demo;
    init_demo(&demo);

    for (int trial = 0; trial < 3; trial++) {
        ThreadSimConfig cfg = {
            .active_processes = 5, .requests_per_process = 30,
            .seed = (uint64_t)(trial * 1111 + 42),
            .think_time_us = 0, .gap_time_us = 0,
            .release_pct = 50
        };

        ThreadSim *sim = tsim_create(&demo, &cfg, TSIM_MODE_SAFE);
        CHECK(sim != NULL);
        CHECK(tsim_run(sim) == true);

        ThreadSimResults r = tsim_results(sim);

        /* Final state must be valid. */
        CHECK(r.final_state_valid);
        CHECK(banker_validate(&r.final_state, NULL, NULL) == BANKER_OK);
        CHECK(conservation_holds(&r.final_state));

        /* Total resources unchanged. */
        for (int j = 0; j < r.final_state.m; j++)
            CHECK(r.final_state.total[j] == demo.total[j]);

        /* No process over-allocated. */
        for (int i = 0; i < r.final_state.n; i++)
            for (int j = 0; j < r.final_state.m; j++) {
                CHECK(r.final_state.alloc[i][j] >= 0);
                CHECK(r.final_state.alloc[i][j] <= r.final_state.max[i][j]);
            }

        tsim_destroy(sim);
    }
    printf("  final state: 3 trials, all valid, conservation holds\n");
}

/* ================================================================== */
/*  Test 8 — Repeated simulation and cleanup                           */
/* ================================================================== */

static void test_repeated_simulation(void) {
    BankerState demo;
    init_demo(&demo);
    ThreadSimConfig cfg = {
        .active_processes = 3, .requests_per_process = 20,
        .seed = 1, .think_time_us = 0, .gap_time_us = 0,
        .release_pct = 30
    };

    for (int round = 0; round < 5; round++) {
        cfg.seed = (uint64_t)(round + 1) * 0xABCDEF01ULL;
        ThreadSim *sim = tsim_create(&demo, &cfg, TSIM_MODE_SAFE);
        CHECK(sim != NULL);
        CHECK(tsim_run(sim) == true);

        ThreadSimResults r = tsim_results(sim);
        CHECK(r.total_requests > 0);
        CHECK(r.invariant_violations == 0);
        CHECK(r.final_state_valid);
        CHECK(conservation_holds(&r.final_state));

        tsim_destroy(sim);
    }
    printf("  repeated: 5 rounds, all clean\n");
}

/* ================================================================== */
/*  Test 9 — String helpers                                            */
/* ================================================================== */

static void test_string_helpers(void) {
    CHECK(strcmp(tsim_event_type_str(TSIM_THREAD_START),  "THREAD_START") == 0);
    CHECK(strcmp(tsim_event_type_str(TSIM_GRANTED),       "GRANTED") == 0);
    CHECK(strcmp(tsim_event_type_str(TSIM_TOCTOU_SPLIT),  "TOCTOU_SPLIT") == 0);
    CHECK(strcmp(tsim_event_type_str(TSIM_VIOLATION_INVALID), "VIOLATION_INVALID") == 0);
    CHECK(strcmp(tsim_mode_str(TSIM_MODE_SAFE),  "SAFE (atomic check-and-commit)") == 0);
    CHECK(strcmp(tsim_mode_str(TSIM_MODE_TOCTOU),"TOCTOU (split check / commit)") == 0);
}

/* ================================================================== */
/*  Test 10 — Deterministic TOCTOU demonstration (barrier-based)       */
/* ================================================================== */

/*
 * This test proves the TOCTOU race without relying on timing.
 *
 * Setup (the classic demo state, Available = [3, 3, 2]):
 *   Thread A acts as P1, requests [1, 2, 0].
 *   Thread B acts as P4, requests [3, 2, 0].
 *
 * Each request is individually safe (verified by banker_request on
 * the original state).  But if both are blindly committed, Available
 * becomes [3-1-3, 3-2-2, 2-0-0] = [-1, -1, 2], which is INVALID
 * (negative resources — a conservation violation).
 *
 * The barriers guarantee:
 *   Barrier 1: both threads have their snapshot before either proceeds.
 *   Barrier 2: both threads have evaluated safety before either commits.
 *
 * Every memory access to the shared state is mutex-protected.
 * There is NO C data race.  The violation is purely LOGICAL: the
 * safety check and the commit are not performed atomically.
 */

typedef struct {
    BankerState       *shared;
    pthread_mutex_t   *mtx;
    pthread_barrier_t *bar;
    int                pid;
    int                req[BANKER_MAX_RES];
    bool               check_granted;
    bool               committed;
} ToctouCtx;

static void *toctou_worker(void *arg) {
    ToctouCtx *ctx = arg;

    /* Phase 1: Snapshot under lock. */
    pthread_mutex_lock(ctx->mtx);
    BankerState snapshot = *ctx->shared;
    pthread_mutex_unlock(ctx->mtx);

    /* BARRIER 1 — both threads now hold snapshots of the SAME state. */
    pthread_barrier_wait(ctx->bar);

    /* Phase 2: Safety check on the snapshot (no lock held). */
    BankerState trial = snapshot;
    RequestResult r = banker_request(&trial, ctx->pid, ctx->req, NULL);
    ctx->check_granted = (r == REQ_GRANTED);

    /* BARRIER 2 — both threads have made their stale decisions. */
    pthread_barrier_wait(ctx->bar);

    /* Phase 3: Blind commit to real state. */
    if (ctx->check_granted) {
        pthread_mutex_lock(ctx->mtx);
        for (int j = 0; j < ctx->shared->m; j++) {
            ctx->shared->available[j]        -= ctx->req[j];
            ctx->shared->alloc[ctx->pid][j]  += ctx->req[j];
        }
        ctx->committed = true;
        pthread_mutex_unlock(ctx->mtx);
    }

    return NULL;
}

static void test_toctou_deterministic(void) {
    printf("\n  === DETERMINISTIC TOCTOU DEMONSTRATION ===\n");

    /* Original demo state. */
    BankerState shared;
    init_demo(&shared);
    CHECK(banker_validate(&shared, NULL, NULL) == BANKER_OK);
    CHECK(banker_is_safe(&shared, NULL, NULL, NULL));

    /* Verify each request is individually safe. */
    int req_a[3] = {1, 2, 0};   /* P1 */
    int req_b[3] = {3, 2, 0};   /* P4 */

    BankerState probe = shared;
    CHECK(banker_request(&probe, 1, req_a, NULL) == REQ_GRANTED);
    probe = shared;
    CHECK(banker_request(&probe, 4, req_b, NULL) == REQ_GRANTED);

    printf("  P1 request [1,2,0]: individually SAFE\n");
    printf("  P4 request [3,2,0]: individually SAFE\n");
    printf("  Available before: [%d, %d, %d]\n",
           shared.available[0], shared.available[1], shared.available[2]);

    /* Set up synchronization. */
    pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;
    pthread_barrier_t bar;
    pthread_barrier_init(&bar, NULL, 2);

    ToctouCtx ctx_a = { .shared = &shared, .mtx = &mtx, .bar = &bar,
                        .pid = 1, .req = {1, 2, 0} };
    ToctouCtx ctx_b = { .shared = &shared, .mtx = &mtx, .bar = &bar,
                        .pid = 4, .req = {3, 2, 0} };

    pthread_t ta, tb;
    pthread_create(&ta, NULL, toctou_worker, &ctx_a);
    pthread_create(&tb, NULL, toctou_worker, &ctx_b);
    pthread_join(ta, NULL);
    pthread_join(tb, NULL);

    /* Both threads should have checked GRANTED. */
    CHECK(ctx_a.check_granted);
    CHECK(ctx_b.check_granted);
    CHECK(ctx_a.committed);
    CHECK(ctx_b.committed);

    /* The shared state should now be INVALID. */
    printf("  Available after:  [%d, %d, %d]\n",
           shared.available[0], shared.available[1], shared.available[2]);
    printf("  Expected:         [-1, -1, 2]\n");

    CHECK(shared.available[0] == -1);
    CHECK(shared.available[1] == -1);
    CHECK(shared.available[2] == 2);

    BankerStatus val = banker_validate(&shared, NULL, NULL);
    CHECK(val != BANKER_OK);
    printf("  Validation: %s\n", banker_status_str(val));
    CHECK(val == BANKER_ERR_NEGATIVE);

    printf("\n  CONCLUSION:\n");
    printf("  Both threads independently determined their request was SAFE.\n");
    printf("  Both blindly committed based on a stale snapshot.\n");
    printf("  The shared state is now INVALID (negative Available).\n");
    printf("  Every memory access was mutex-protected (no C data race).\n");
    printf("  The bug is that CHECK and COMMIT were not atomic.\n");
    printf("  This is why the Banker's Algorithm requires a mutex\n");
    printf("  across the entire check-and-commit transaction.\n");

    pthread_barrier_destroy(&bar);
    pthread_mutex_destroy(&mtx);
}

/* ================================================================== */
/*  Test 11 — TOCTOU via the tsim API (probabilistic)                  */
/* ================================================================== */

static void test_toctou_via_api(void) {
    /* Tight 2-process, 1-resource state: very high contention.
     * Total=2, Available=2, Max=[2,2], Alloc=[0,0].
     * Any two concurrent requests that sum > 2 cause a violation. */
    BankerState tight;
    banker_init(&tight, 2, 1);
    tight.total[0]     = 2;
    tight.available[0] = 2;
    tight.max[0][0]    = 2;
    tight.max[1][0]    = 2;
    CHECK(banker_validate(&tight, NULL, NULL) == BANKER_OK);
    CHECK(banker_is_safe(&tight, NULL, NULL, NULL));

    ThreadSimConfig cfg = {
        .active_processes      = 2,
        .requests_per_process  = 30,
        .seed                  = 0xFACEFEED,
        .think_time_us         = 0,
        .gap_time_us           = 50000,     /* 50 ms — wide race window */
        .release_pct           = 0          /* no releases: maximise violations */
    };

    int violations_seen = 0, unsafe_seen = 0;
    int attempts = 3;

    for (int trial = 0; trial < attempts; trial++) {
        cfg.seed = 0xFACEFEED + (uint64_t)trial * 777;
        ThreadSim *sim = tsim_create(&tight, &cfg, TSIM_MODE_TOCTOU);
        CHECK(sim != NULL);
        CHECK(tsim_run(sim) == true);

        ThreadSimResults r = tsim_results(sim);
        violations_seen += r.invariant_violations;
        unsafe_seen     += r.unsafe_states_observed;

        /* Event log should contain TOCTOU-specific events. */
        bool has_split = false, has_blind = false;
        for (int k = 0; k < tsim_event_count(sim); k++) {
            ThreadSimEventType t = tsim_event_at(sim, k)->type;
            if (t == TSIM_TOCTOU_SPLIT)        has_split = true;
            if (t == TSIM_TOCTOU_BLIND_COMMIT) has_blind = true;
        }
        CHECK(has_split);
        CHECK(has_blind);

        tsim_destroy(sim);
    }

    printf("  toctou api: %d trials, %d invalid violations, %d unsafe states\n",
           attempts, violations_seen, unsafe_seen);

    /* With a 50 ms gap and a tight state, violations are very likely
     * but not guaranteed.  Report rather than assert. */
    if (violations_seen + unsafe_seen > 0) {
        printf("  TOCTOU race successfully demonstrated via API.\n");
    } else {
        printf("  NOTE: no violations observed in %d trials.\n"
               "  This is possible due to scheduler timing.\n"
               "  The deterministic test above guarantees the race.\n",
               attempts);
    }
}

/* ================================================================== */
/*  Test 12 — Safe mode single process (no contention)                 */
/* ================================================================== */

static void test_single_process(void) {
    BankerState demo;
    init_demo(&demo);
    ThreadSimConfig cfg = {
        .active_processes = 1, .requests_per_process = 50,
        .seed = 42, .think_time_us = 0, .gap_time_us = 0,
        .release_pct = 60
    };

    ThreadSim *sim = tsim_create(&demo, &cfg, TSIM_MODE_SAFE);
    CHECK(sim != NULL);
    CHECK(tsim_run(sim) == true);

    ThreadSimResults r = tsim_results(sim);
    CHECK(r.total_requests > 0);
    CHECK(r.granted > 0);
    CHECK(r.invariant_violations == 0);
    CHECK(r.final_state_valid);
    CHECK(conservation_holds(&r.final_state));

    /* Only process 0 events in the log. */
    for (int k = 0; k < tsim_event_count(sim); k++)
        CHECK(tsim_event_at(sim, k)->process_id == 0);

    tsim_destroy(sim);
}

/* ================================================================== */
/*  Test 13 — Process completion releases all resources                */
/* ================================================================== */

static void test_completion_release(void) {
    BankerState demo;
    init_demo(&demo);
    ThreadSimConfig cfg = {
        .active_processes = demo.n,  /* ALL processes active */
        .requests_per_process = 20,
        .seed = 0xC0FFEE, .think_time_us = 0, .gap_time_us = 0,
        .release_pct = 30
    };

    ThreadSim *sim = tsim_create(&demo, &cfg, TSIM_MODE_SAFE);
    CHECK(sim != NULL);
    CHECK(tsim_run(sim) == true);

    ThreadSimResults r = tsim_results(sim);
    CHECK(r.final_state_valid);

    /* Every active process should have released everything on completion. */
    for (int i = 0; i < cfg.active_processes; i++)
        for (int j = 0; j < r.final_state.m; j++)
            CHECK(r.final_state.alloc[i][j] == 0);

    /* Available should equal Total (all processes released). */
    for (int j = 0; j < r.final_state.m; j++)
        CHECK(r.final_state.available[j] == r.final_state.total[j]);

    CHECK(conservation_holds(&r.final_state));

    /* At least one completion-release RELEASE event should exist.
     * A RELEASE immediately before a THREAD_END for the same process
     * is a completion release. */
    int completion_rels = 0;
    int total = tsim_event_count(sim);
    for (int k = 0; k < total - 1; k++) {
        const ThreadSimEvent *ev   = tsim_event_at(sim, k);
        const ThreadSimEvent *next = tsim_event_at(sim, k + 1);
        if (ev->type == TSIM_RELEASE && next->type == TSIM_THREAD_END &&
            ev->process_id == next->process_id)
            completion_rels++;
    }

    printf("  completion release: alloc=0, available=total, "
           "%d completion releases\n", completion_rels);
    tsim_destroy(sim);
}

/* ================================================================== */
/*  Test 14 — Callback reentrancy: query event log from callback       */
/* ================================================================== */

typedef struct {
    const ThreadSim *sim;      /* sim handle for API calls from callback */
    pthread_mutex_t  mtx;
    int              events_seen;
    bool             count_was_positive;
    bool             saw_event_zero;
} ReentrancyCbCtx;

static void reentrancy_callback(const ThreadSimEvent *ev, void *data) {
    ReentrancyCbCtx *ctx = data;
    pthread_mutex_lock(&ctx->mtx);
    ctx->events_seen++;
    if (ev->event_id == 0)
        ctx->saw_event_zero = true;
    pthread_mutex_unlock(&ctx->mtx);

    /* Call the public API from the callback — must NOT deadlock.
     * tsim_event_count() acquires log_mtx internally.  Since the
     * callback fires OUTSIDE all ThreadSim mutexes, this is safe. */
    int count = tsim_event_count(ctx->sim);
    if (count > 0) {
        pthread_mutex_lock(&ctx->mtx);
        ctx->count_was_positive = true;
        pthread_mutex_unlock(&ctx->mtx);
    }
}

static void test_callback_reentrancy(void) {
    BankerState demo;
    init_demo(&demo);
    ThreadSimConfig cfg = {
        .active_processes = 2, .requests_per_process = 10,
        .seed = 42, .think_time_us = 0, .gap_time_us = 0,
        .release_pct = 20
    };

    ThreadSim *sim = tsim_create(&demo, &cfg, TSIM_MODE_SAFE);
    CHECK(sim != NULL);

    ReentrancyCbCtx ctx = { .sim = sim, .events_seen = 0,
                            .count_was_positive = false,
                            .saw_event_zero = false };
    pthread_mutex_init(&ctx.mtx, NULL);
    tsim_set_callback(sim, reentrancy_callback, &ctx);
    CHECK(tsim_run(sim) == true);

    /* If we got here without deadlock, the callback is reentrant-safe. */
    pthread_mutex_lock(&ctx.mtx);
    CHECK(ctx.events_seen > 0);
    CHECK(ctx.count_was_positive);
    CHECK(ctx.saw_event_zero);
    int seen = ctx.events_seen;
    pthread_mutex_unlock(&ctx.mtx);
    pthread_mutex_destroy(&ctx.mtx);

    printf("  callback reentrancy: %d events, tsim_event_count() safe\n",
           seen);
    tsim_destroy(sim);
}

/* ================================================================== */
/*  Test 15 — Deterministic TOCTOU via production API                  */
/* ================================================================== */

static void test_toctou_pair_api(void) {
    printf("\n  === DETERMINISTIC TOCTOU API DEMONSTRATION ===\n");

    BankerState demo;
    init_demo(&demo);
    ThreadSimConfig cfg = {
        .active_processes = 5, .requests_per_process = 1,
        .seed = 1, .think_time_us = 0, .gap_time_us = 0,
        .release_pct = 0
    };

    /* Valid pair: P1=[1,2,0], P4=[3,2,0] — both individually safe,
     * combined Available becomes [-1,-1,2] — INVALID. */
    ThreadSim *sim = tsim_create(&demo, &cfg, TSIM_MODE_TOCTOU);
    CHECK(sim != NULL);

    ThreadSimToctouPair pair = {
        .pid_a = 1, .request_a = {1, 2, 0},
        .pid_b = 4, .request_b = {3, 2, 0}
    };
    CHECK(tsim_run_toctou_pair(sim, &pair) == true);

    ThreadSimResults r = tsim_results(sim);

    /* Both requests were granted (each individually safe). */
    CHECK(r.granted == 2);
    CHECK(r.total_requests == 2);

    /* Final state must be INVALID (negative Available). */
    CHECK(!r.final_state_valid);
    CHECK(r.invariant_violations > 0);

    /* Event log must contain the TOCTOU sequence. */
    bool has_split = false, has_blind = false, has_violation = false;
    for (int k = 0; k < tsim_event_count(sim); k++) {
        const ThreadSimEvent *ev = tsim_event_at(sim, k);
        if (ev->type == TSIM_TOCTOU_SPLIT)        has_split = true;
        if (ev->type == TSIM_TOCTOU_BLIND_COMMIT) has_blind = true;
        if (ev->type == TSIM_VIOLATION_INVALID)    has_violation = true;
    }
    CHECK(has_split);
    CHECK(has_blind);
    CHECK(has_violation);

    printf("  pair API: 2 grants, %d violations, final state INVALID\n",
           r.invariant_violations);
    tsim_destroy(sim);

    /* ---- Validation: same PID for both ---- */
    ThreadSim *s2 = tsim_create(&demo, &cfg, TSIM_MODE_TOCTOU);
    ThreadSimToctouPair bad1 = {
        .pid_a = 1, .pid_b = 1,
        .request_a = {1,0,0}, .request_b = {1,0,0}
    };
    CHECK(tsim_run_toctou_pair(s2, &bad1) == false);
    tsim_destroy(s2);

    /* ---- Validation: request not individually safe ---- */
    ThreadSim *s3 = tsim_create(&demo, &cfg, TSIM_MODE_TOCTOU);
    ThreadSimToctouPair bad2 = {
        .pid_a = 0, .pid_b = 1,
        .request_a = {99,99,99}, .request_b = {1,0,0}
    };
    CHECK(tsim_run_toctou_pair(s3, &bad2) == false);
    tsim_destroy(s3);

    /* ---- Validation: NULL pair ---- */
    ThreadSim *s4 = tsim_create(&demo, &cfg, TSIM_MODE_TOCTOU);
    CHECK(tsim_run_toctou_pair(s4, NULL) == false);
    tsim_destroy(s4);

    /* ---- Validation: wrong mode ---- */
    ThreadSim *s5 = tsim_create(&demo, &cfg, TSIM_MODE_SAFE);
    CHECK(tsim_run_toctou_pair(s5, &pair) == false);
    tsim_destroy(s5);

    printf("  pair API: validation checks passed\n");
}

/* ================================================================== */
/*  main                                                               */
/* ================================================================== */

int main(void) {
    test_invalid_config();
    test_api_robustness();
    test_string_helpers();
    test_safe_mode_basic();
    test_safe_mode_invariants();
    test_event_lifecycle();
    test_release_on_full_alloc();
    test_final_state();
    test_repeated_simulation();
    test_single_process();
    test_completion_release();
    test_callback_reentrancy();
    test_toctou_deterministic();
    test_toctou_via_api();
    test_toctou_pair_api();

    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
