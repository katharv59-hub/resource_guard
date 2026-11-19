/* ResourceGuard thread simulation — implementation.
 *
 * Every access to the shared BankerState is mutex-protected, so there
 * is NO C-level data race and NO undefined behaviour in either mode.
 *
 * TSIM_MODE_SAFE:  the mutex is held across the entire
 *     banker_request() call (check + commit are atomic).
 *
 * TSIM_MODE_TOCTOU:  the mutex is released after taking a snapshot,
 *     the safety check runs on the stale snapshot, and the commit
 *     re-acquires the mutex and applies the request blindly.  Another
 *     thread can change the real state in the gap, so the commit may
 *     push the system into an invalid or unsafe configuration.
 *
 * Callback safety:  external callbacks fire WITHOUT any ThreadSim
 * mutex held.  The event data is copied to a stack-local struct
 * before the log mutex is released; the pointer passed to the
 * callback is valid only during the call.  Callbacks may safely call
 * tsim_event_count() (which acquires log_mtx internally), but must
 * NOT call tsim_event_at() during simulation — the returned pointer
 * could be invalidated by a concurrent realloc in another thread's
 * log_event().
 *
 * Process completion:  when a worker thread finishes its iterations,
 * it releases all remaining allocations back to Available via
 * banker_release(), mirroring the Banker's Algorithm assumption that
 * completed processes release all resources.
 *
 * Build:  gcc -std=c11 -pthread ... engine/threading.c              */

#define _POSIX_C_SOURCE 200809L   /* clock_gettime, nanosleep */

#include "threading.h"
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>

/* ================================================================== */
/*  Internal structures                                                */
/* ================================================================== */

struct ThreadSim {
    /* Shared mutable state, protected by state_mtx. */
    BankerState       state;
    BankerState       initial;
    pthread_mutex_t   state_mtx;

    /* Dynamically growing event log, protected by log_mtx. */
    ThreadSimEvent   *events;
    int               event_count;
    int               event_cap;
    pthread_mutex_t   log_mtx;

    /* Aggregate counters, protected by res_mtx. */
    ThreadSimResults  results;
    pthread_mutex_t   res_mtx;

    ThreadSimConfig   config;
    ThreadSimMode     mode;

    ThreadSimCallback callback;
    void             *cb_data;

    struct timespec   t0;      /* monotonic start time */
    bool              ran;     /* tsim_run() completed */
};

/* Per-worker context passed to pthread_create. */
typedef struct {
    ThreadSim *sim;
    int        pid;
    uint64_t   rng;
} WorkerCtx;

/* Startup gate for deterministic TOCTOU pair workers.
 * Workers wait on the condvar until 'ready' or 'abort' is set.
 * This replaces pthread_cancel: if the second thread fails to
 * create, the first is unblocked cleanly via the abort flag. */
typedef struct {
    pthread_mutex_t mtx;
    pthread_cond_t  cv;
    bool            ready;
    bool            abort;
} StartupGate;

/* Context for deterministic TOCTOU pair workers. */
typedef struct {
    ThreadSim        *sim;
    pthread_barrier_t *bar;
    StartupGate      *gate;
    int               pid;
    int               req[BANKER_MAX_RES];
    bool              check_safe;
    bool              committed;
} ToctouPairCtx;

/* ================================================================== */
/*  String helpers                                                     */
/* ================================================================== */

const char *tsim_event_type_str(ThreadSimEventType type) {
    switch (type) {
    case TSIM_THREAD_START:        return "THREAD_START";
    case TSIM_THREAD_END:          return "THREAD_END";
    case TSIM_REQUEST:             return "REQUEST";
    case TSIM_GRANTED:             return "GRANTED";
    case TSIM_DENIED_UNSAFE:       return "DENIED_UNSAFE";
    case TSIM_DENIED_WAIT:         return "DENIED_WAIT";
    case TSIM_RELEASE:             return "RELEASE";
    case TSIM_TOCTOU_SPLIT:        return "TOCTOU_SPLIT";
    case TSIM_TOCTOU_BLIND_COMMIT: return "TOCTOU_BLIND_COMMIT";
    case TSIM_VIOLATION_INVALID:   return "VIOLATION_INVALID";
    case TSIM_VIOLATION_UNSAFE:    return "VIOLATION_UNSAFE";
    }
    return "UNKNOWN";
}

const char *tsim_mode_str(ThreadSimMode mode) {
    switch (mode) {
    case TSIM_MODE_SAFE:  return "SAFE (atomic check-and-commit)";
    case TSIM_MODE_TOCTOU:return "TOCTOU (split check / commit)";
    }
    return "UNKNOWN";
}

/* ================================================================== */
/*  Internal helpers                                                   */
/* ================================================================== */

/* Microseconds elapsed since t0. */
static uint64_t elapsed_us(const struct timespec *t0) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    long sec  = now.tv_sec  - t0->tv_sec;
    long nsec = now.tv_nsec - t0->tv_nsec;
    if (nsec < 0) { sec--; nsec += 1000000000L; }
    return (uint64_t)sec * 1000000ULL + (uint64_t)nsec / 1000ULL;
}

/* Sleep for the given number of microseconds.  No-op if us <= 0. */
static void delay_us(int us) {
    if (us <= 0) return;
    struct timespec ts = {
        .tv_sec  = us / 1000000,
        .tv_nsec = (long)(us % 1000000) * 1000L
    };
    nanosleep(&ts, NULL);
}

/* Deterministic xorshift64 PRNG (same family as test_banker.c). */
static int rng_next(uint64_t *s, int bound) {
    *s ^= *s << 13;
    *s ^= *s >> 7;
    *s ^= *s << 17;
    return (int)(*s % (uint64_t)bound);
}

/* Append an event to the log.  The callback, if set, fires AFTER the
 * log mutex is released — it receives a stack copy of the event and
 * is never invoked while any ThreadSim mutex is held.
 *
 * vec and avail may be NULL (stored as zeros). */
static void log_event(ThreadSim *sim, int pid, ThreadSimEventType type,
                      const int *vec, const int *avail) {
    ThreadSimEvent ev_copy;
    ThreadSimCallback cb;
    void *cb_data;

    pthread_mutex_lock(&sim->log_mtx);

    /* Grow if full. */
    if (sim->event_count >= sim->event_cap) {
        int new_cap = sim->event_cap * 2;
        if (new_cap < 256) new_cap = 256;
        ThreadSimEvent *buf = realloc(sim->events,
                                      (size_t)new_cap * sizeof *buf);
        if (!buf) { pthread_mutex_unlock(&sim->log_mtx); return; }
        sim->events   = buf;
        sim->event_cap = new_cap;
    }

    int idx = sim->event_count++;
    ThreadSimEvent *ev = &sim->events[idx];
    ev->event_id     = (uint64_t)idx;
    ev->timestamp_us = elapsed_us(&sim->t0);
    ev->process_id   = pid;
    ev->type         = type;
    ev->m            = sim->initial.m;

    int m = ev->m;
    if (vec)   memcpy(ev->vector,    vec,   (size_t)m * sizeof vec[0]);
    else       memset(ev->vector,    0,     sizeof ev->vector);
    if (avail) memcpy(ev->available, avail, (size_t)m * sizeof avail[0]);
    else       memset(ev->available, 0,     sizeof ev->available);

    /* Copy the event and callback pointer while still under the lock. */
    ev_copy = *ev;
    cb      = sim->callback;
    cb_data = sim->cb_data;

    pthread_mutex_unlock(&sim->log_mtx);

    /* Fire callback WITHOUT any ThreadSim mutex held. */
    if (cb)
        cb(&ev_copy, cb_data);
}

/* ================================================================== */
/*  Request / release generation                                       */
/* ================================================================== */

/* Generate a random non-zero request within the process's remaining
 * Need.  Returns false if Need is entirely zero (fully allocated) or
 * if the state appears corrupted (alloc > max). */
static bool gen_request(const BankerState *s, int pid, uint64_t *rng,
                        int req[]) {
    int m = s->m;
    int need[BANKER_MAX_RES];
    bool has_need = false;

    for (int j = 0; j < m; j++) {
        need[j] = s->max[pid][j] - s->alloc[pid][j];
        if (need[j] < 0) return false;          /* corrupted state guard */
        if (need[j] > 0) has_need = true;
    }
    if (!has_need) return false;

    memset(req, 0, (size_t)BANKER_MAX_RES * sizeof req[0]);
    for (int j = 0; j < m; j++)
        if (need[j] > 0)
            req[j] = rng_next(rng, need[j] + 1);

    /* Guarantee at least one non-zero component. */
    bool zero = true;
    for (int j = 0; j < m; j++)
        if (req[j] > 0) { zero = false; break; }

    if (zero) {
        int start = rng_next(rng, m);
        for (int k = 0; k < m; k++) {
            int j = (start + k) % m;
            if (need[j] > 0) {
                req[j] = 1 + rng_next(rng, need[j]);
                break;
            }
        }
    }
    return true;
}

/* Generate a random partial release from what the process holds.
 * Returns false if the process holds nothing. */
static bool gen_release(const BankerState *s, int pid, uint64_t *rng,
                        int rel[]) {
    int m = s->m;
    bool holds = false;
    for (int j = 0; j < m; j++)
        if (s->alloc[pid][j] > 0) holds = true;
    if (!holds) return false;

    memset(rel, 0, (size_t)BANKER_MAX_RES * sizeof rel[0]);
    for (int j = 0; j < m; j++) {
        int held = s->alloc[pid][j];
        if (held > 0)
            rel[j] = rng_next(rng, held + 1);
    }

    /* Guarantee at least one non-zero component. */
    bool zero = true;
    for (int j = 0; j < m; j++)
        if (rel[j] > 0) { zero = false; break; }

    if (zero) {
        int start = rng_next(rng, m);
        for (int k = 0; k < m; k++) {
            int j = (start + k) % m;
            if (s->alloc[pid][j] > 0) {
                rel[j] = 1 + rng_next(rng, s->alloc[pid][j]);
                break;
            }
        }
    }
    return true;
}

/* ================================================================== */
/*  Worker: SAFE mode                                                  */
/* ================================================================== */

/* Each iteration:
 *   1. Lock the state mutex.
 *   2. Generate a random request within Need.
 *   3. Call banker_request() on the shared state (check + commit are
 *      atomic because the mutex is held the whole time).
 *   4. Copy results for logging.
 *   5. Unlock.
 *   6. Log events OUTSIDE the state mutex.
 *   7. With configured probability, re-lock and release resources.
 *
 * After all iterations, the worker releases all remaining allocations
 * (process completion), then logs THREAD_END.
 *
 * Because banker_request() runs entirely under the mutex, the system
 * invariants (conservation, safety) can NEVER be violated. */

static void *worker_safe(void *arg) {
    WorkerCtx *ctx = arg;
    ThreadSim *sim = ctx->sim;
    int pid = ctx->pid;
    int m = sim->initial.m;
    int requests = 0, granted = 0, denied = 0, waited = 0, releases = 0;

    log_event(sim, pid, TSIM_THREAD_START, NULL, NULL);

    for (int i = 0; i < sim->config.requests_per_process; i++) {
        delay_us(sim->config.think_time_us);

        int req[BANKER_MAX_RES];
        int avail_before[BANKER_MAX_RES];
        int avail_after[BANKER_MAX_RES];
        RequestResult r;

        pthread_mutex_lock(&sim->state_mtx);

        if (!gen_request(&sim->state, pid, &ctx->rng, req)) {
            /* Need is zero — release everything and retry. */
            int rel[BANKER_MAX_RES];
            bool did_rel = false;
            if (gen_release(&sim->state, pid, &ctx->rng, rel)) {
                banker_release(&sim->state, pid, rel);
                releases++;
                memcpy(avail_after, sim->state.available,
                       (size_t)m * sizeof(int));
                did_rel = true;
            }
            pthread_mutex_unlock(&sim->state_mtx);
            if (did_rel)
                log_event(sim, pid, TSIM_RELEASE, rel, avail_after);
            continue;
        }

        requests++;
        memcpy(avail_before, sim->state.available,
               (size_t)m * sizeof(int));

        r = banker_request(&sim->state, pid, req, NULL);

        memcpy(avail_after, sim->state.available,
               (size_t)m * sizeof(int));

        if (r == REQ_GRANTED)        granted++;
        else if (r == REQ_DENIED_UNSAFE) denied++;
        else if (r == REQ_WAIT_UNAVAILABLE) waited++;

        pthread_mutex_unlock(&sim->state_mtx);

        /* Log events OUTSIDE the state lock. */
        log_event(sim, pid, TSIM_REQUEST, req, avail_before);

        if (r == REQ_GRANTED)
            log_event(sim, pid, TSIM_GRANTED, req, avail_after);
        else if (r == REQ_DENIED_UNSAFE)
            log_event(sim, pid, TSIM_DENIED_UNSAFE, req, avail_after);
        else if (r == REQ_WAIT_UNAVAILABLE)
            log_event(sim, pid, TSIM_DENIED_WAIT, req, avail_after);

        /* Probabilistic release after a grant. */
        if (r == REQ_GRANTED &&
            rng_next(&ctx->rng, 100) < sim->config.release_pct) {
            int rel[BANKER_MAX_RES];
            int avail_rel[BANKER_MAX_RES];
            bool did_rel = false;

            pthread_mutex_lock(&sim->state_mtx);
            if (gen_release(&sim->state, pid, &ctx->rng, rel)) {
                banker_release(&sim->state, pid, rel);
                releases++;
                memcpy(avail_rel, sim->state.available,
                       (size_t)m * sizeof(int));
                did_rel = true;
            }
            pthread_mutex_unlock(&sim->state_mtx);

            if (did_rel)
                log_event(sim, pid, TSIM_RELEASE, rel, avail_rel);
        }
    }

    /* ---- Process completion: release all remaining allocations ---- */
    {
        int rel_final[BANKER_MAX_RES];
        int avail_final[BANKER_MAX_RES];
        bool holds = false;

        pthread_mutex_lock(&sim->state_mtx);
        for (int j = 0; j < m; j++) {
            rel_final[j] = sim->state.alloc[pid][j];
            if (rel_final[j] > 0) holds = true;
        }
        if (holds) {
            banker_release(&sim->state, pid, rel_final);
            releases++;
            memcpy(avail_final, sim->state.available,
                   (size_t)m * sizeof(int));
        }
        pthread_mutex_unlock(&sim->state_mtx);

        if (holds)
            log_event(sim, pid, TSIM_RELEASE, rel_final, avail_final);
    }

    log_event(sim, pid, TSIM_THREAD_END, NULL, NULL);

    pthread_mutex_lock(&sim->res_mtx);
    sim->results.total_requests += requests;
    sim->results.granted        += granted;
    sim->results.denied_unsafe  += denied;
    sim->results.denied_wait    += waited;
    sim->results.releases       += releases;
    pthread_mutex_unlock(&sim->res_mtx);

    return NULL;
}

/* ================================================================== */
/*  Worker: TOCTOU mode                                                */
/* ================================================================== */

/* Each iteration:
 *   Phase 1 — SNAPSHOT (under lock):
 *     Lock.  Copy the shared state.  Generate a request from the
 *     snapshot.  Copy Available for logging.  Unlock.
 *
 *   Phase 2 — CHECK (no lock held):
 *     Log the request and TOCTOU_SPLIT events.  Run banker_request()
 *     on the SNAPSHOT.  Sleep for gap_time_us to widen the race.
 *
 *   Phase 3 — COMMIT (under lock, stale decision):
 *     If the snapshot check said GRANTED, re-lock and apply the
 *     request to the REAL state WITHOUT re-running the safety check.
 *     Copy Available snapshots for logging.  Unlock.  Log events
 *     outside the lock.  Then verify invariants.
 *
 * Individual memory accesses are always mutex-protected, so there is
 * NO C data race.  The race is purely logical: the decision was made
 * on a snapshot that may no longer reflect the shared state.
 *
 * After all iterations, releases remaining allocations if the state
 * is still valid. */

static void *worker_toctou(void *arg) {
    WorkerCtx *ctx = arg;
    ThreadSim *sim = ctx->sim;
    int pid = ctx->pid;
    int m   = sim->initial.m;
    int requests = 0, granted = 0, denied = 0, waited = 0, releases = 0;
    int violations = 0, unsafe_seen = 0;

    log_event(sim, pid, TSIM_THREAD_START, NULL, NULL);

    for (int i = 0; i < sim->config.requests_per_process; i++) {
        delay_us(sim->config.think_time_us);

        /* ---- Phase 1: snapshot under lock ---- */
        BankerState snapshot;
        int req[BANKER_MAX_RES];
        int avail_snap[BANKER_MAX_RES];

        pthread_mutex_lock(&sim->state_mtx);
        snapshot = sim->state;

        /* If earlier violations corrupted the state, bail. */
        if (banker_validate(&snapshot, NULL, NULL) != BANKER_OK) {
            pthread_mutex_unlock(&sim->state_mtx);
            continue;
        }

        if (!gen_request(&snapshot, pid, &ctx->rng, req)) {
            /* Fully allocated — release under real state. */
            int rel[BANKER_MAX_RES];
            int avail_rel[BANKER_MAX_RES];
            bool did_rel = false;
            if (gen_release(&sim->state, pid, &ctx->rng, rel) &&
                banker_validate(&sim->state, NULL, NULL) == BANKER_OK) {
                banker_release(&sim->state, pid, rel);
                releases++;
                memcpy(avail_rel, sim->state.available,
                       (size_t)m * sizeof(int));
                did_rel = true;
            }
            pthread_mutex_unlock(&sim->state_mtx);
            if (did_rel)
                log_event(sim, pid, TSIM_RELEASE, rel, avail_rel);
            continue;
        }

        requests++;
        memcpy(avail_snap, sim->state.available,
               (size_t)m * sizeof(int));
        pthread_mutex_unlock(&sim->state_mtx);

        /* ==== LOCK RELEASED — TOCTOU WINDOW OPENS ==== */

        /* Log outside state lock. */
        log_event(sim, pid, TSIM_REQUEST, req, avail_snap);
        log_event(sim, pid, TSIM_TOCTOU_SPLIT, req, avail_snap);

        /* ---- Phase 2: check on stale snapshot (no lock) ---- */
        BankerState trial = snapshot;
        RequestResult snap_r = banker_request(&trial, pid, req, NULL);

        /* Widen the race window so other threads can intervene. */
        delay_us(sim->config.gap_time_us);

        /* ---- Phase 3: act on the stale decision ---- */
        if (snap_r == REQ_GRANTED) {
            int avail_before[BANKER_MAX_RES];
            int avail_after[BANKER_MAX_RES];
            bool violated = false, unsafe_det = false;

            pthread_mutex_lock(&sim->state_mtx);

            memcpy(avail_before, sim->state.available,
                   (size_t)m * sizeof(int));

            /* Blind commit — apply WITHOUT rechecking safety. */
            for (int j = 0; j < m; j++) {
                sim->state.available[j]  -= req[j];
                sim->state.alloc[pid][j] += req[j];
            }

            granted++;

            memcpy(avail_after, sim->state.available,
                   (size_t)m * sizeof(int));

            /* Detect invariant violations. */
            if (banker_validate(&sim->state, NULL, NULL) != BANKER_OK) {
                violations++;
                violated = true;
            } else if (!banker_is_safe(&sim->state, NULL, NULL, NULL)) {
                unsafe_seen++;
                unsafe_det = true;
            }

            pthread_mutex_unlock(&sim->state_mtx);

            /* Log events OUTSIDE the state lock. */
            log_event(sim, pid, TSIM_TOCTOU_BLIND_COMMIT, req,
                      avail_before);
            log_event(sim, pid, TSIM_GRANTED, req, avail_after);

            if (violated)
                log_event(sim, pid, TSIM_VIOLATION_INVALID, req,
                          avail_after);
            else if (unsafe_det)
                log_event(sim, pid, TSIM_VIOLATION_UNSAFE, req,
                          avail_after);

        } else if (snap_r == REQ_DENIED_UNSAFE) {
            denied++;
            log_event(sim, pid, TSIM_DENIED_UNSAFE, req, avail_snap);
        } else if (snap_r == REQ_WAIT_UNAVAILABLE) {
            waited++;
            log_event(sim, pid, TSIM_DENIED_WAIT, req, avail_snap);
        }

        /* Probabilistic release (only if state is still valid). */
        if (snap_r == REQ_GRANTED &&
            rng_next(&ctx->rng, 100) < sim->config.release_pct) {
            int rel[BANKER_MAX_RES];
            int avail_rel[BANKER_MAX_RES];
            bool did_rel = false;

            pthread_mutex_lock(&sim->state_mtx);
            if (banker_validate(&sim->state, NULL, NULL) == BANKER_OK) {
                if (gen_release(&sim->state, pid, &ctx->rng, rel)) {
                    banker_release(&sim->state, pid, rel);
                    releases++;
                    memcpy(avail_rel, sim->state.available,
                           (size_t)m * sizeof(int));
                    did_rel = true;
                }
            }
            pthread_mutex_unlock(&sim->state_mtx);

            if (did_rel)
                log_event(sim, pid, TSIM_RELEASE, rel, avail_rel);
        }
    }

    /* ---- Process completion: release remaining (if state valid) ---- */
    {
        int rel_final[BANKER_MAX_RES];
        int avail_final[BANKER_MAX_RES];
        bool holds = false;

        pthread_mutex_lock(&sim->state_mtx);
        if (banker_validate(&sim->state, NULL, NULL) == BANKER_OK) {
            for (int j = 0; j < m; j++) {
                rel_final[j] = sim->state.alloc[pid][j];
                if (rel_final[j] > 0) holds = true;
            }
            if (holds) {
                banker_release(&sim->state, pid, rel_final);
                releases++;
                memcpy(avail_final, sim->state.available,
                       (size_t)m * sizeof(int));
            }
        }
        pthread_mutex_unlock(&sim->state_mtx);

        if (holds)
            log_event(sim, pid, TSIM_RELEASE, rel_final, avail_final);
    }

    log_event(sim, pid, TSIM_THREAD_END, NULL, NULL);

    pthread_mutex_lock(&sim->res_mtx);
    sim->results.total_requests       += requests;
    sim->results.granted              += granted;
    sim->results.denied_unsafe        += denied;
    sim->results.denied_wait          += waited;
    sim->results.releases             += releases;
    sim->results.invariant_violations += violations;
    sim->results.unsafe_states_observed += unsafe_seen;
    pthread_mutex_unlock(&sim->res_mtx);

    return NULL;
}

/* ================================================================== */
/*  Deterministic TOCTOU pair worker                                   */
/* ================================================================== */

/* Each of two threads:
 *   Phase 1 — snapshot the shared state under state_mtx.
 *   BARRIER 1 — both hold snapshots of the SAME original state.
 *   Phase 2 — check safety on the stale snapshot (no lock).
 *   BARRIER 2 — both have made their stale decisions.
 *   Phase 3 — blind commit to the real state under state_mtx.
 *
 * All shared-memory accesses are mutex-protected (no C data race).
 * The race is purely logical — the barrier guarantees it without
 * timing dependence. */

static void *toctou_pair_worker(void *arg) {
    ToctouPairCtx *ctx = arg;
    StartupGate *gate = ctx->gate;

    /* Wait at the startup gate until both threads are created or aborted. */
    pthread_mutex_lock(&gate->mtx);
    while (!gate->ready && !gate->abort) {
        pthread_cond_wait(&gate->cv, &gate->mtx);
    }
    bool aborted = gate->abort;
    pthread_mutex_unlock(&gate->mtx);

    if (aborted) {
        return NULL;
    }

    ThreadSim *sim = ctx->sim;
    int pid = ctx->pid;
    int m = sim->initial.m;

    log_event(sim, pid, TSIM_THREAD_START, NULL, NULL);

    /* Phase 1: Snapshot under lock. */
    BankerState snapshot;
    int avail_snap[BANKER_MAX_RES];

    pthread_mutex_lock(&sim->state_mtx);
    snapshot = sim->state;
    memcpy(avail_snap, sim->state.available, (size_t)m * sizeof(int));
    pthread_mutex_unlock(&sim->state_mtx);

    log_event(sim, pid, TSIM_REQUEST, ctx->req, avail_snap);

    /* ═══ BARRIER 1: both threads hold snapshots of SAME state ═══ */
    pthread_barrier_wait(ctx->bar);

    /* Phase 2: Safety check on snapshot (no lock held). */
    BankerState trial = snapshot;
    RequestResult r = banker_request(&trial, pid, ctx->req, NULL);
    ctx->check_safe = (r == REQ_GRANTED);

    log_event(sim, pid, TSIM_TOCTOU_SPLIT, ctx->req, avail_snap);

    /* ═══ BARRIER 2: both threads have made stale decisions ═══ */
    pthread_barrier_wait(ctx->bar);

    /* Phase 3: Blind commit to real state. */
    if (ctx->check_safe) {
        int avail_before[BANKER_MAX_RES];
        int avail_after[BANKER_MAX_RES];
        bool violated = false, unsafe_det = false;

        pthread_mutex_lock(&sim->state_mtx);

        memcpy(avail_before, sim->state.available,
               (size_t)m * sizeof(int));

        for (int j = 0; j < m; j++) {
            sim->state.available[j]  -= ctx->req[j];
            sim->state.alloc[pid][j] += ctx->req[j];
        }
        ctx->committed = true;

        memcpy(avail_after, sim->state.available,
               (size_t)m * sizeof(int));

        if (banker_validate(&sim->state, NULL, NULL) != BANKER_OK)
            violated = true;
        else if (!banker_is_safe(&sim->state, NULL, NULL, NULL))
            unsafe_det = true;

        pthread_mutex_unlock(&sim->state_mtx);

        /* Log events OUTSIDE all locks. */
        log_event(sim, pid, TSIM_TOCTOU_BLIND_COMMIT, ctx->req,
                  avail_before);
        log_event(sim, pid, TSIM_GRANTED, ctx->req, avail_after);

        if (violated)
            log_event(sim, pid, TSIM_VIOLATION_INVALID, ctx->req,
                      avail_after);
        else if (unsafe_det)
            log_event(sim, pid, TSIM_VIOLATION_UNSAFE, ctx->req,
                      avail_after);

        pthread_mutex_lock(&sim->res_mtx);
        sim->results.total_requests++;
        sim->results.granted++;
        if (violated)   sim->results.invariant_violations++;
        if (unsafe_det) sim->results.unsafe_states_observed++;
        pthread_mutex_unlock(&sim->res_mtx);

    } else {
        pthread_mutex_lock(&sim->res_mtx);
        sim->results.total_requests++;
        if (r == REQ_DENIED_UNSAFE)      sim->results.denied_unsafe++;
        else if (r == REQ_WAIT_UNAVAILABLE) sim->results.denied_wait++;
        pthread_mutex_unlock(&sim->res_mtx);

        if (r == REQ_DENIED_UNSAFE)
            log_event(sim, pid, TSIM_DENIED_UNSAFE, ctx->req, avail_snap);
        else
            log_event(sim, pid, TSIM_DENIED_WAIT, ctx->req, avail_snap);
    }

    log_event(sim, pid, TSIM_THREAD_END, NULL, NULL);
    return NULL;
}

/* ================================================================== */
/*  Public API                                                         */
/* ================================================================== */

ThreadSim *tsim_create(const BankerState *initial,
                       const ThreadSimConfig *config,
                       ThreadSimMode mode) {
    if (!initial || !config) return NULL;
    if (banker_validate(initial, NULL, NULL) != BANKER_OK) return NULL;
    if (config->active_processes < 1 ||
        config->active_processes > initial->n)       return NULL;
    if (config->requests_per_process < 1)            return NULL;
    if (config->release_pct < 0 || config->release_pct > 100) return NULL;

    ThreadSim *sim = calloc(1, sizeof *sim);
    if (!sim) return NULL;

    sim->state   = *initial;
    sim->initial = *initial;
    sim->config  = *config;
    sim->mode    = mode;

    pthread_mutex_init(&sim->state_mtx, NULL);
    pthread_mutex_init(&sim->log_mtx,   NULL);
    pthread_mutex_init(&sim->res_mtx,   NULL);

    sim->event_cap = 1024;
    sim->events = calloc((size_t)sim->event_cap, sizeof *sim->events);
    if (!sim->events) {
        pthread_mutex_destroy(&sim->state_mtx);
        pthread_mutex_destroy(&sim->log_mtx);
        pthread_mutex_destroy(&sim->res_mtx);
        free(sim);
        return NULL;
    }

    return sim;
}

void tsim_set_callback(ThreadSim *sim, ThreadSimCallback cb,
                       void *user_data) {
    if (!sim) return;
    sim->callback = cb;
    sim->cb_data  = user_data;
}

bool tsim_run(ThreadSim *sim) {
    if (!sim || sim->ran) return false;

    int np = sim->config.active_processes;
    pthread_t *thr  = calloc((size_t)np, sizeof *thr);
    WorkerCtx *ctxs = calloc((size_t)np, sizeof *ctxs);
    if (!thr || !ctxs) { free(thr); free(ctxs); return false; }

    clock_gettime(CLOCK_MONOTONIC, &sim->t0);

    void *(*fn)(void *) =
        (sim->mode == TSIM_MODE_SAFE) ? worker_safe : worker_toctou;

    for (int i = 0; i < np; i++) {
        ctxs[i].sim = sim;
        ctxs[i].pid = i;
        /* Per-thread seed: mix config seed with thread index. */
        ctxs[i].rng = sim->config.seed ^
                      ((uint64_t)(i + 1) * 0x9E3779B97F4A7C15ULL);

        if (pthread_create(&thr[i], NULL, fn, &ctxs[i]) != 0) {
            /* Join threads that already started, then fail. */
            for (int k = 0; k < i; k++)
                pthread_join(thr[k], NULL);
            free(thr);
            free(ctxs);
            return false;
        }
    }

    for (int i = 0; i < np; i++)
        pthread_join(thr[i], NULL);

    /* Record final state. */
    sim->results.final_state = sim->state;
    sim->results.final_state_valid =
        (banker_validate(&sim->state, NULL, NULL) == BANKER_OK);
    sim->results.final_state_safe =
        sim->results.final_state_valid &&
        banker_is_safe(&sim->state, NULL, NULL, NULL);

    sim->ran = true;
    free(thr);
    free(ctxs);
    return true;
}

/* ------------------------------------------------------------------ */
/*  Deterministic TOCTOU pair                                          */
/* ------------------------------------------------------------------ */

bool tsim_run_toctou_pair(ThreadSim *sim,
                          const ThreadSimToctouPair *pair) {
    if (!sim || !pair || sim->ran) return false;
    if (sim->mode != TSIM_MODE_TOCTOU) return false;

    int n = sim->initial.n;
    int m = sim->initial.m;

    /* Validate process IDs. */
    if (pair->pid_a < 0 || pair->pid_a >= n) return false;
    if (pair->pid_b < 0 || pair->pid_b >= n) return false;
    if (pair->pid_a == pair->pid_b)          return false;

    /* Verify both requests are individually safe on the current state. */
    int req_a[BANKER_MAX_RES], req_b[BANKER_MAX_RES];
    memcpy(req_a, pair->request_a, (size_t)m * sizeof(int));
    memcpy(req_b, pair->request_b, (size_t)m * sizeof(int));

    BankerState probe = sim->state;
    if (banker_request(&probe, pair->pid_a, req_a, NULL) != REQ_GRANTED)
        return false;
    probe = sim->state;
    if (banker_request(&probe, pair->pid_b, req_b, NULL) != REQ_GRANTED)
        return false;

    /* Set up synchronization: startup gate and execution barriers. */
    StartupGate gate;
    pthread_mutex_init(&gate.mtx, NULL);
    pthread_cond_init(&gate.cv, NULL);
    gate.ready = false;
    gate.abort = false;

    pthread_barrier_t bar;
    pthread_barrier_init(&bar, NULL, 2);
    clock_gettime(CLOCK_MONOTONIC, &sim->t0);

    ToctouPairCtx ctx_a = { .sim = sim, .bar = &bar, .gate = &gate,
                            .pid = pair->pid_a };
    ToctouPairCtx ctx_b = { .sim = sim, .bar = &bar, .gate = &gate,
                            .pid = pair->pid_b };
    memcpy(ctx_a.req, pair->request_a, (size_t)m * sizeof(int));
    memcpy(ctx_b.req, pair->request_b, (size_t)m * sizeof(int));

    pthread_t ta, tb;
    if (pthread_create(&ta, NULL, toctou_pair_worker, &ctx_a) != 0) {
        pthread_barrier_destroy(&bar);
        pthread_cond_destroy(&gate.cv);
        pthread_mutex_destroy(&gate.mtx);
        return false;
    }
    if (pthread_create(&tb, NULL, toctou_pair_worker, &ctx_b) != 0) {
        /* Thread B failed to launch. Abort thread A cleanly through the startup gate. */
        pthread_mutex_lock(&gate.mtx);
        gate.abort = true;
        pthread_cond_broadcast(&gate.cv);
        pthread_mutex_unlock(&gate.mtx);

        pthread_join(ta, NULL);
        pthread_barrier_destroy(&bar);
        pthread_cond_destroy(&gate.cv);
        pthread_mutex_destroy(&gate.mtx);
        return false;
    }

    /* Both threads created successfully: open startup gate. */
    pthread_mutex_lock(&gate.mtx);
    gate.ready = true;
    pthread_cond_broadcast(&gate.cv);
    pthread_mutex_unlock(&gate.mtx);

    pthread_join(ta, NULL);
    pthread_join(tb, NULL);

    /* Record final state. */
    sim->results.final_state = sim->state;
    sim->results.final_state_valid =
        (banker_validate(&sim->state, NULL, NULL) == BANKER_OK);
    sim->results.final_state_safe =
        sim->results.final_state_valid &&
        banker_is_safe(&sim->state, NULL, NULL, NULL);

    sim->ran = true;
    pthread_barrier_destroy(&bar);
    pthread_cond_destroy(&gate.cv);
    pthread_mutex_destroy(&gate.mtx);
    return true;
}

/* ------------------------------------------------------------------ */
/*  Results and event log access                                       */
/* ------------------------------------------------------------------ */

ThreadSimResults tsim_results(const ThreadSim *sim) {
    ThreadSimResults empty;
    memset(&empty, 0, sizeof empty);
    return sim ? sim->results : empty;
}

/* Thread-safe: acquires log_mtx.  Safe to call from a live callback
 * (which fires outside all ThreadSim mutexes). */
int tsim_event_count(const ThreadSim *sim) {
    if (!sim) return 0;
    ThreadSim *s = (ThreadSim *)sim;   /* cast for mutex access */
    pthread_mutex_lock(&s->log_mtx);
    int count = s->event_count;
    pthread_mutex_unlock(&s->log_mtx);
    return count;
}

/* Post-run only.  After tsim_run() / tsim_run_toctou_pair() returns,
 * no threads are writing to the event buffer, so the returned pointer
 * is stable.  Do NOT call during simulation from a callback — the
 * pointer could be invalidated by a concurrent realloc. */
const ThreadSimEvent *tsim_event_at(const ThreadSim *sim, int index) {
    if (!sim || index < 0 || index >= sim->event_count) return NULL;
    return &sim->events[index];
}

/* Free all resources.  Safe to call with NULL.  Must be called only
 * after tsim_run() or tsim_run_toctou_pair() has returned (all worker
 * threads have been joined), or if neither was ever called. */
void tsim_destroy(ThreadSim *sim) {
    if (!sim) return;
    pthread_mutex_destroy(&sim->state_mtx);
    pthread_mutex_destroy(&sim->log_mtx);
    pthread_mutex_destroy(&sim->res_mtx);
    free(sim->events);
    free(sim);
}
