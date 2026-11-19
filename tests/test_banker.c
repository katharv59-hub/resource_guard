/* Run from the project root:  make test
 * No external test framework. A failed CHECK prints file:line and the test run exits non-zero. */
#include "banker.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int checks = 0, failures = 0;

#define CHECK(cond) do { checks++; if (!(cond)) { failures++; \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static void load_valid(const char *path, BankerState *s) {
    char err[128];
    if (!banker_load_scenario(path, s, err, sizeof err)) {
        fprintf(stderr, "cannot load %s: %s\n", path, err);
        exit(2);
    }
}

static bool vec_eq(const int *a, const int *b, int m) { return memcmp(a, b, (size_t)m * sizeof *a) == 0; }

/* ---------- named test vectors T1..T14 from the README ---------- */

static void test_vectors(void) {
    BankerState demo, s;
    int seq[BANKER_MAX_PROCS], len;
    load_valid("tests/scenarios/demo.txt", &demo);

    /* T1 */
    CHECK(banker_validate(&demo, NULL, NULL) == BANKER_OK);
    CHECK(banker_is_safe(&demo, seq, &len, NULL));
    CHECK(banker_sequence_is_valid(&demo, seq, len));
    int expect_policy[5] = { 1, 3, 0, 2, 4 };
    CHECK(len == 5 && memcmp(seq, expect_policy, sizeof expect_policy) == 0);
    int textbook[5] = { 1, 3, 4, 0, 2 };                 /* also valid: sequences are not unique */
    CHECK(banker_sequence_is_valid(&demo, textbook, 5));
    int bad_order[5] = { 0, 1, 2, 3, 4 };
    CHECK(!banker_sequence_is_valid(&demo, bad_order, 5));

    /* T2: safe grant */
    s = demo;
    int r2[3] = { 1, 0, 2 };
    CHECK(banker_request(&s, 1, r2, NULL) == REQ_GRANTED);
    int av2[3] = { 2, 3, 0 };
    CHECK(vec_eq(s.available, av2, 3));
    CHECK(banker_validate(&s, NULL, NULL) == BANKER_OK);
    CHECK(banker_is_safe(&s, NULL, NULL, NULL));

    /* T4: after T2, P4 asks 3 3 0 -> wait, and safety is not run */
    int r3[3] = { 3, 3, 0 };
    BankerState before = s;
    Trace tr;
    CHECK(banker_request(&s, 4, r3, &tr) == REQ_WAIT_UNAVAILABLE);
    CHECK(tr.count == 0);
    CHECK(memcmp(&before, &s, sizeof s) == 0);

    /* T3: from the original state, available but unsafe */
    s = demo;
    CHECK(banker_request(&s, 4, r3, &tr) == REQ_DENIED_UNSAFE);
    CHECK(!tr.safe);
    CHECK(tr.steps[tr.count - 1].picked == -1);
    int stuck_work[3] = { 0, 0, 2 };
    CHECK(vec_eq(tr.final_work, stuck_work, 3));
    CHECK(memcmp(&demo, &s, sizeof s) == 0);             /* exact rollback */

    /* T14: rollback integrity, then T2 still works */
    CHECK(banker_request(&s, 1, r2, NULL) == REQ_GRANTED);

    /* T5: exceeds need */
    s = demo;
    int r5[3] = { 1, 3, 0 };
    CHECK(banker_request(&s, 1, r5, NULL) == REQ_ERR_EXCEEDS_NEED);
    CHECK(memcmp(&demo, &s, sizeof s) == 0);

    /* T6: check ordering: exceeds Need AND Available -> Need wins */
    int r6[3] = { 7, 0, 0 };
    CHECK(banker_request(&s, 2, r6, NULL) == REQ_ERR_EXCEEDS_NEED);

    /* T7: zero request */
    int zero[3] = { 0, 0, 0 };
    CHECK(banker_request(&s, 0, zero, NULL) == REQ_GRANTED);
    CHECK(memcmp(&demo, &s, sizeof s) == 0);

    /* T8, T9: bad index, negative request */
    CHECK(banker_request(&s, 9, zero, NULL) == REQ_ERR_BAD_INPUT);
    CHECK(banker_request(&s, -1, zero, NULL) == REQ_ERR_BAD_INPUT);
    int neg[3] = { -1, 0, 0 };
    CHECK(banker_request(&s, 0, neg, NULL) == REQ_ERR_BAD_INPUT);
    CHECK(memcmp(&demo, &s, sizeof s) == 0);

    /* T10..T12: invalid states, each with its own code and location */
    int bp, br;
    load_valid("tests/scenarios/invalid_alloc_gt_max.txt", &s);
    CHECK(banker_validate(&s, &bp, &br) == BANKER_ERR_ALLOC_GT_MAX && bp == 2 && br == 0);
    CHECK(banker_request(&s, 0, zero, NULL) == REQ_ERR_BAD_STATE);
    load_valid("tests/scenarios/invalid_max_gt_total.txt", &s);
    CHECK(banker_validate(&s, &bp, &br) == BANKER_ERR_MAX_GT_TOTAL && bp == 2 && br == 0);
    load_valid("tests/scenarios/invalid_conservation.txt", &s);
    CHECK(banker_validate(&s, &bp, &br) == BANKER_ERR_CONSERVATION && br == 0);

    /* T13: valid but unsafe initial state */
    load_valid("tests/scenarios/unsafe_initial.txt", &s);
    CHECK(banker_validate(&s, NULL, NULL) == BANKER_OK);
    CHECK(!banker_is_safe(&s, seq, &len, &tr));
    CHECK(len == 0 && tr.count == 1 && tr.steps[0].picked == -1);

    /* extra validation cases */
    banker_init(&s, 0, 3);
    CHECK(banker_validate(&s, NULL, NULL) == BANKER_ERR_DIMENSIONS);
    load_valid("tests/scenarios/demo.txt", &s);
    s.available[1] = -1;
    CHECK(banker_validate(&s, NULL, NULL) == BANKER_ERR_NEGATIVE);
    s.available[1] = BANKER_MAX_VALUE + 1;
    CHECK(banker_validate(&s, NULL, NULL) == BANKER_ERR_RANGE);

    /* release */
    s = demo;
    int rel[3] = { 1, 0, 0 };
    CHECK(banker_release(&s, 3, rel));                    /* P3 holds 2 A, gives back 1 */
    CHECK(s.available[0] == 4 && s.alloc[3][0] == 1);
    CHECK(banker_validate(&s, NULL, NULL) == BANKER_OK);
    int too_much[3] = { 5, 0, 0 };
    BankerState b2 = s;
    CHECK(!banker_release(&s, 3, too_much));
    CHECK(memcmp(&b2, &s, sizeof s) == 0);
}


/* ---------- explainability: exhaustive safe-request region ---------- */

typedef struct {
    int visited;
    int safe;
    int unsafe;
    bool saw_rejected;
    bool saw_largest;
} WhyVisitorCounts;

static void count_why_candidate(const int req[BANKER_MAX_RES], bool safe, void *user_data) {
    WhyVisitorCounts *counts = user_data;
    counts->visited++;
    if (safe) counts->safe++; else counts->unsafe++;

    int rejected[3] = { 3, 3, 0 };
    int largest[3] = { 3, 2, 1 };
    if (memcmp(req, rejected, sizeof rejected) == 0 && !safe)
        counts->saw_rejected = true;
    if (memcmp(req, largest, sizeof largest) == 0 && safe)
        counts->saw_largest = true;
}

static void test_why_not(void) {
    BankerState demo, unchanged;
    WhyNotReport report;
    WhyVisitorCounts counts = { 0 };
    load_valid("tests/scenarios/demo.txt", &demo);
    unchanged = demo;

    int denied[3] = { 3, 3, 0 };
    CHECK(banker_why_not(&demo, 4, denied, &report,
                         count_why_candidate, &counts) == WHY_OK);

    CHECK(report.candidate_count == 32);
    CHECK(report.safe_count == 24);
    CHECK(report.unsafe_count == 8);
    CHECK(counts.visited == 32);
    CHECK(counts.safe == 24 && counts.unsafe == 8);
    CHECK(counts.saw_rejected && counts.saw_largest);

    int search_bound[3] = { 3, 3, 1 };
    int componentwise_max[3] = { 3, 2, 1 };
    int nearest[3] = { 3, 2, 0 };
    CHECK(report.has_safe_request);
    CHECK(vec_eq(report.search_bound, search_bound, 3));
    CHECK(vec_eq(report.componentwise_max, componentwise_max, 3));
    CHECK(report.componentwise_max_is_safe);
    CHECK(report.has_nearest_safe_request);
    CHECK(vec_eq(report.nearest_safe_request, nearest, 3));
    CHECK(report.nearest_distance == 1);

    int stuck_work[3] = { 0, 0, 2 };
    CHECK(!report.denial_trace.safe);
    CHECK(report.denial_trace.count > 0);
    CHECK(report.denial_trace.steps[report.denial_trace.count - 1].picked == -1);
    CHECK(vec_eq(report.denial_trace.final_work, stuck_work, 3));
    CHECK(memcmp(&demo, &unchanged, sizeof demo) == 0); /* analysis is read-only */

    /* Coordinate-wise maxima do not always combine into a safe vector. */
    BankerState frontier;
    banker_init(&frontier, 3, 3);
    int f_total[3] = { 4, 2, 6 };
    int f_available[3] = { 1, 2, 2 };
    memcpy(frontier.total, f_total, sizeof f_total);
    memcpy(frontier.available, f_available, sizeof f_available);
    int f_alloc[3][3] = {
        { 1, 0, 2 },
        { 0, 0, 2 },
        { 2, 0, 0 }
    };
    int f_max[3][3] = {
        { 1, 1, 3 },
        { 1, 2, 2 },
        { 2, 2, 3 }
    };
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) {
            frontier.alloc[i][j] = f_alloc[i][j];
            frontier.max[i][j] = f_max[i][j];
        }
    CHECK(banker_validate(&frontier, NULL, NULL) == BANKER_OK);
    int frontier_denied[3] = { 0, 1, 2 };
    CHECK(banker_why_not(&frontier, 2, frontier_denied,
                         &report, NULL, NULL) == WHY_OK);
    int frontier_max[3] = { 0, 1, 2 };
    CHECK(report.safe_count == 5 && report.candidate_count == 9);
    CHECK(vec_eq(report.componentwise_max, frontier_max, 3));
    CHECK(!report.componentwise_max_is_safe);
    CHECK(report.has_nearest_safe_request && report.nearest_distance == 1);

    int safe_request[3] = { 1, 0, 2 };
    CHECK(banker_why_not(&demo, 1, safe_request, &report, NULL, NULL) == WHY_ERR_NOT_UNSAFE);

    int exceeds_need[3] = { 5, 0, 0 };
    CHECK(banker_why_not(&demo, 4, exceeds_need, &report, NULL, NULL) == WHY_ERR_EXCEEDS_NEED);

    int unavailable[3] = { 4, 0, 0 };
    CHECK(banker_why_not(&demo, 4, unavailable, &report, NULL, NULL) == WHY_ERR_UNAVAILABLE);

    CHECK(banker_why_not(&demo, 99, denied, &report, NULL, NULL) == WHY_ERR_BAD_INPUT);
    int negative[3] = { -1, 0, 0 };
    CHECK(banker_why_not(&demo, 4, negative, &report, NULL, NULL) == WHY_ERR_BAD_INPUT);
    CHECK(banker_why_not(&demo, 4, denied, NULL, NULL, NULL) == WHY_ERR_BAD_INPUT);

    /* Oversized search must fail before visiting any candidates. */
    BankerState large;
    banker_init(&large, 2, 3);
    for (int j = 0; j < 3; j++) {
        large.total[j] = 200;
        large.available[j] = 100;
        large.alloc[0][j] = 100;
        large.max[0][j] = 200;
        large.max[1][j] = 200;
    }
    CHECK(banker_validate(&large, NULL, NULL) == BANKER_OK);
    int one_unit[3] = { 1, 0, 0 };
    WhyVisitorCounts no_visits = { 0 };
    CHECK(banker_why_not(&large, 1, one_unit, &report,
                         count_why_candidate, &no_visits) == WHY_ERR_SEARCH_SPACE_TOO_LARGE);
    CHECK(no_visits.visited == 0);
    CHECK(large.available[0] == 100 && large.alloc[0][0] == 100);
}

static void test_scenario_loader(void) {
    BankerState s;
    char err[128];
    CHECK(!banker_load_scenario("tests/scenarios/malformed.txt", &s, err, sizeof err));
    CHECK(strstr(err, "unknown keyword") != NULL);
    CHECK(!banker_load_scenario("tests/scenarios/does_not_exist.txt", &s, err, sizeof err));
}

/* ---------- property tests against a brute-force oracle ---------- */

static unsigned long long rng_state = 0x9E3779B97F4A7C15ULL;
static int rnd(int bound) {                               /* deterministic xorshift, range [0, bound) */
    rng_state ^= rng_state << 13; rng_state ^= rng_state >> 7; rng_state ^= rng_state << 17;
    return (int)(rng_state % (unsigned long long)bound);
}

static void random_valid_state(BankerState *s) {
    int n = 1 + rnd(6), m = 1 + rnd(3);
    banker_init(s, n, m);
    for (int j = 0; j < m; j++) {
        s->total[j] = 1 + rnd(8);
        int left = s->total[j];
        for (int i = 0; i < n; i++) s->max[i][j] = rnd(s->total[j] + 1);
        for (int i = 0; i < n; i++) {
            int cap = s->max[i][j] < left ? s->max[i][j] : left;
            s->alloc[i][j] = rnd(cap + 1);
            left -= s->alloc[i][j];
        }
        s->available[j] = left;
    }
}

/* Oracle: try every permutation. */
static bool perm_search(const BankerState *s, int *perm, bool *used, int depth) {
    if (depth == s->n) return banker_sequence_is_valid(s, perm, s->n);
    for (int i = 0; i < s->n; i++) {
        if (used[i]) continue;
        used[i] = true; perm[depth] = i;
        if (perm_search(s, perm, used, depth + 1)) { used[i] = false; return true; }
        used[i] = false;
    }
    return false;
}
static bool oracle_safe(const BankerState *s) {
    int perm[BANKER_MAX_PROCS]; bool used[BANKER_MAX_PROCS] = { false };
    return perm_search(s, perm, used, 0);
}

static void test_random(void) {
    int safe_count = 0, unsafe_count = 0, granted = 0, denied = 0, waited = 0, invalid = 0;
    for (int iter = 0; iter < 5000; iter++) {
        BankerState s;
        random_valid_state(&s);
        CHECK(banker_validate(&s, NULL, NULL) == BANKER_OK);

        int seq[BANKER_MAX_PROCS], len;
        bool eng = banker_is_safe(&s, seq, &len, NULL);
        CHECK(eng == oracle_safe(&s));                    /* engine agrees with brute force */
        if (eng) { CHECK(banker_sequence_is_valid(&s, seq, len)); safe_count++; } else unsafe_count++;

        /* random request, sometimes out of bounds */
        int pid = rnd(s.n);
        int req[BANKER_MAX_RES] = { 0 };
        for (int j = 0; j < s.m; j++) req[j] = rnd(s.max[pid][j] - s.alloc[pid][j] + 2);

        BankerState before = s;
        RequestResult res = banker_request(&s, pid, req, NULL);

        /* independent expectation */
        bool exceeds_need = false, exceeds_avail = false, zero = true;
        for (int j = 0; j < s.m; j++) {
            if (req[j] > before.max[pid][j] - before.alloc[pid][j]) exceeds_need = true;
            if (req[j] > before.available[j]) exceeds_avail = true;
            if (req[j]) zero = false;
        }
        if (zero)               CHECK(res == REQ_GRANTED);
        else if (exceeds_need)  CHECK(res == REQ_ERR_EXCEEDS_NEED);
        else if (exceeds_avail) CHECK(res == REQ_WAIT_UNAVAILABLE);
        else {
            BankerState trial = before;
            for (int j = 0; j < s.m; j++) { trial.available[j] -= req[j]; trial.alloc[pid][j] += req[j]; }
            CHECK(res == (oracle_safe(&trial) ? REQ_GRANTED : REQ_DENIED_UNSAFE));
        }

        if (res == REQ_GRANTED) {
            granted++;
            CHECK(banker_validate(&s, NULL, NULL) == BANKER_OK);        /* invariants survive a commit */
            if (!zero) CHECK(banker_is_safe(&s, NULL, NULL, NULL));          /* a commit is always safe */
        } else {
            if (res == REQ_DENIED_UNSAFE) denied++;
            else if (res == REQ_WAIT_UNAVAILABLE) waited++; else invalid++;
            CHECK(memcmp(&before, &s, sizeof s) == 0);                  /* rollback is exact */
        }
    }
    printf("random: %d safe / %d unsafe states; %d granted, %d denied-unsafe, %d wait, %d invalid\n",
           safe_count, unsafe_count, granted, denied, waited, invalid);
    /* make sure the generator actually exercised every branch */
    CHECK(safe_count > 100 && unsafe_count > 100 && granted > 100 && denied > 50 && waited > 50 && invalid > 50);
}

int main(void) {
    test_vectors();
    test_why_not();
    test_scenario_loader();
    test_random();
    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
