#include "banker.h"
#include <string.h>

const char *banker_status_str(BankerStatus s) {
    switch (s) {
    case BANKER_OK:               return "state is valid";
    case BANKER_ERR_DIMENSIONS:   return "process or resource count out of range";
    case BANKER_ERR_NEGATIVE:     return "negative value";
    case BANKER_ERR_RANGE:        return "value too large";
    case BANKER_ERR_ALLOC_GT_MAX: return "allocation exceeds maximum";
    case BANKER_ERR_MAX_GT_TOTAL: return "maximum exceeds total resources";
    case BANKER_ERR_CONSERVATION: return "available + allocated != total";
    }
    return "unknown";
}

const char *banker_why_status_str(WhyNotStatus status) {
    switch (status) {
    case WHY_OK:                      return "analysis complete";
    case WHY_ERR_BAD_STATE:            return "invalid system state";
    case WHY_ERR_BAD_INPUT:            return "invalid process index or negative request";
    case WHY_ERR_EXCEEDS_NEED:         return "request exceeds remaining need";
    case WHY_ERR_UNAVAILABLE:          return "request exceeds currently available resources";
    case WHY_ERR_NOT_UNSAFE:           return "the supplied request is not an unsafe request";
    case WHY_ERR_SEARCH_SPACE_TOO_LARGE:return "request space exceeds the exhaustive-search limit";
    }
    return "unknown";
}

const char *request_result_str(RequestResult r) {
    switch (r) {
    case REQ_GRANTED:          return "REQUEST GRANTED";
    case REQ_ERR_BAD_STATE:    return "INVALID STATE";
    case REQ_ERR_BAD_INPUT:    return "INVALID REQUEST (bad process index or negative value)";
    case REQ_ERR_EXCEEDS_NEED: return "INVALID REQUEST: exceeds remaining need";
    case REQ_WAIT_UNAVAILABLE: return "WAIT: resources not currently available";
    case REQ_DENIED_UNSAFE:    return "REQUEST DENIED: would leave the system in an unsafe state";
    }
    return "unknown";
}

void banker_init(BankerState *s, int n, int m) {
    memset(s, 0, sizeof *s);
    s->n = n;
    s->m = m;
}

BankerStatus banker_validate(const BankerState *s, int *bad_proc, int *bad_res) {
    int bp = -1, br = -1;
    BankerStatus st = BANKER_OK;
#define FAIL(code, p, r) do { st = (code); bp = (p); br = (r); goto done; } while (0)

    if (s->n < 1 || s->n > BANKER_MAX_PROCS || s->m < 1 || s->m > BANKER_MAX_RES)
        FAIL(BANKER_ERR_DIMENSIONS, -1, -1);

    for (int j = 0; j < s->m; j++) {
        if (s->total[j] < 0 || s->available[j] < 0) FAIL(BANKER_ERR_NEGATIVE, -1, j);
        if (s->total[j] > BANKER_MAX_VALUE || s->available[j] > BANKER_MAX_VALUE)
            FAIL(BANKER_ERR_RANGE, -1, j);
    }
    for (int i = 0; i < s->n; i++)
        for (int j = 0; j < s->m; j++) {
            if (s->max[i][j] < 0 || s->alloc[i][j] < 0) FAIL(BANKER_ERR_NEGATIVE, i, j);
            if (s->max[i][j] > BANKER_MAX_VALUE || s->alloc[i][j] > BANKER_MAX_VALUE)
                FAIL(BANKER_ERR_RANGE, i, j);
        }
    for (int i = 0; i < s->n; i++)
        for (int j = 0; j < s->m; j++)
            if (s->alloc[i][j] > s->max[i][j]) FAIL(BANKER_ERR_ALLOC_GT_MAX, i, j);
    for (int i = 0; i < s->n; i++)
        for (int j = 0; j < s->m; j++)
            if (s->max[i][j] > s->total[j]) FAIL(BANKER_ERR_MAX_GT_TOTAL, i, j);
    for (int j = 0; j < s->m; j++) {
        long long sum = s->available[j];
        for (int i = 0; i < s->n; i++) sum += s->alloc[i][j];
        if (sum != s->total[j]) FAIL(BANKER_ERR_CONSERVATION, -1, j);
    }
done:
#undef FAIL
    if (bad_proc) *bad_proc = bp;
    if (bad_res)  *bad_res = br;
    return st;
}

void banker_need(const BankerState *s, int need[][BANKER_MAX_RES]) {
    for (int i = 0; i < s->n; i++)
        for (int j = 0; j < s->m; j++)
            need[i][j] = s->max[i][j] - s->alloc[i][j];
}

bool banker_is_safe(const BankerState *s, int seq[], int *seq_len, Trace *trace) {
    int  work[BANKER_MAX_RES];
    bool fin[BANKER_MAX_PROCS] = { false };
    int  count = 0;

    memcpy(work, s->available, sizeof work);
    if (trace) memset(trace, 0, sizeof *trace);

    while (count < s->n) {
        int pick = -1;
        for (int i = 0; i < s->n && pick < 0; i++) {
            if (fin[i]) continue;
            bool fits = true;
            for (int j = 0; j < s->m; j++)
                if (s->max[i][j] - s->alloc[i][j] > work[j]) { fits = false; break; }
            if (fits) pick = i;
        }
        if (trace) {
            TraceStep *st = &trace->steps[trace->count++];
            memcpy(st->work, work, sizeof work);
            memcpy(st->finish, fin, sizeof fin);
            st->picked = pick;
        }
        if (pick < 0) break;
        for (int j = 0; j < s->m; j++) work[j] += s->alloc[pick][j];
        fin[pick] = true;
        if (seq) seq[count] = pick;
        count++;
    }

    if (seq_len) *seq_len = count;
    if (trace) {
        memcpy(trace->final_work, work, sizeof work);
        trace->safe = (count == s->n);
    }
    return count == s->n;
}

RequestResult banker_request(BankerState *s, int pid, const int req[], Trace *trace) {
    if (trace) memset(trace, 0, sizeof *trace);
    if (banker_validate(s, NULL, NULL) != BANKER_OK) return REQ_ERR_BAD_STATE;

    if (pid < 0 || pid >= s->n) return REQ_ERR_BAD_INPUT;
    bool all_zero = true;
    for (int j = 0; j < s->m; j++) {
        if (req[j] < 0) return REQ_ERR_BAD_INPUT;
        if (req[j] != 0) all_zero = false;
    }
    if (all_zero) return REQ_GRANTED;

    for (int j = 0; j < s->m; j++)
        if (req[j] > s->max[pid][j] - s->alloc[pid][j]) return REQ_ERR_EXCEEDS_NEED;
    for (int j = 0; j < s->m; j++)
        if (req[j] > s->available[j]) return REQ_WAIT_UNAVAILABLE;

    BankerState trial = *s;                       /* work on a copy: rollback is free */
    for (int j = 0; j < s->m; j++) {
        trial.available[j]  -= req[j];
        trial.alloc[pid][j] += req[j];
    }
    if (!banker_is_safe(&trial, NULL, NULL, trace)) return REQ_DENIED_UNSAFE;
    *s = trial;                                   /* commit */
    return REQ_GRANTED;
}


typedef struct {
    const BankerState *state;
    int pid;
    int bounds[BANKER_MAX_RES];
    int rejected[BANKER_MAX_RES];
    int candidate[BANKER_MAX_RES];
    WhyNotReport *report;
    BankerWhyVisitor visitor;
    void *user_data;
} WhySearch;

static bool request_vector_is_safe(const BankerState *s, int pid, const int req[]) {
    BankerState trial = *s;
    for (int j = 0; j < s->m; j++) {
        trial.available[j] -= req[j];
        trial.alloc[pid][j] += req[j];
    }
    return banker_is_safe(&trial, NULL, NULL, NULL);
}

static unsigned long long vector_distance(const int *a, const int *b, int m) {
    unsigned long long distance = 0;
    for (int j = 0; j < m; j++)
        distance += (unsigned long long)(a[j] >= b[j] ? a[j] - b[j] : b[j] - a[j]);
    return distance;
}

static long long vector_sum(const int *v, int m) {
    long long sum = 0;
    for (int j = 0; j < m; j++) sum += v[j];
    return sum;
}

static bool lexicographically_greater(const int *a, const int *b, int m) {
    for (int j = 0; j < m; j++) {
        if (a[j] != b[j]) return a[j] > b[j];
    }
    return false;
}

static void why_visit_candidate(WhySearch *search) {
    const BankerState *s = search->state;
    WhyNotReport *report = search->report;
    const int *req = search->candidate;
    bool safe = request_vector_is_safe(s, search->pid, req);

    report->candidate_count++;
    if (safe) {
        report->safe_count++;

        if (!report->has_safe_request) {
            memcpy(report->componentwise_max, req, (size_t)s->m * sizeof req[0]);
            report->has_safe_request = true;
        } else {
            for (int j = 0; j < s->m; j++)
                if (req[j] > report->componentwise_max[j])
                    report->componentwise_max[j] = req[j];
        }

        unsigned long long distance = vector_distance(req, search->rejected, s->m);
        long long sum = vector_sum(req, s->m);
        long long best_sum = report->has_nearest_safe_request
                           ? vector_sum(report->nearest_safe_request, s->m) : -1;

        if (!report->has_nearest_safe_request ||
            distance < report->nearest_distance ||
            (distance == report->nearest_distance &&
             (sum > best_sum ||
              (sum == best_sum &&
               lexicographically_greater(req, report->nearest_safe_request, s->m))))) {
            memcpy(report->nearest_safe_request, req, (size_t)s->m * sizeof req[0]);
            report->nearest_distance = distance;
            report->has_nearest_safe_request = true;
        }
    }

    if (search->visitor)
        search->visitor(req, safe, search->user_data);
}

static void why_enumerate(WhySearch *search, int resource_index) {
    const int m = search->state->m;
    if (resource_index == m) {
        why_visit_candidate(search);
        return;
    }

    for (int value = 0; value <= search->bounds[resource_index]; value++) {
        search->candidate[resource_index] = value;
        why_enumerate(search, resource_index + 1);
    }
}

WhyNotStatus banker_why_not(const BankerState *s, int pid, const int rejected_req[],
                            WhyNotReport *report, BankerWhyVisitor visitor,
                            void *user_data) {
    if (!report || !rejected_req) return WHY_ERR_BAD_INPUT;
    memset(report, 0, sizeof *report);

    if (banker_validate(s, NULL, NULL) != BANKER_OK) return WHY_ERR_BAD_STATE;
    if (pid < 0 || pid >= s->n) return WHY_ERR_BAD_INPUT;

    /* Reuse the request engine to enforce its input/check ordering and capture
     * the exact trace explaining why this particular request is unsafe. */
    BankerState probe = *s;
    RequestResult request_status =
        banker_request(&probe, pid, rejected_req, &report->denial_trace);

    switch (request_status) {
    case REQ_ERR_BAD_STATE:    return WHY_ERR_BAD_STATE;
    case REQ_ERR_BAD_INPUT:    return WHY_ERR_BAD_INPUT;
    case REQ_ERR_EXCEEDS_NEED: return WHY_ERR_EXCEEDS_NEED;
    case REQ_WAIT_UNAVAILABLE: return WHY_ERR_UNAVAILABLE;
    case REQ_GRANTED:          return WHY_ERR_NOT_UNSAFE;
    case REQ_DENIED_UNSAFE:    break;
    }

    WhySearch search;
    memset(&search, 0, sizeof search);
    search.state = s;
    search.pid = pid;
    search.report = report;
    search.visitor = visitor;
    search.user_data = user_data;
    memcpy(search.rejected, rejected_req, (size_t)s->m * sizeof rejected_req[0]);

    /* Count the Cartesian product before searching; never do partial analysis. */
    unsigned long long combinations = 1;
    for (int j = 0; j < s->m; j++) {
        int need = s->max[pid][j] - s->alloc[pid][j];
        int bound = need < s->available[j] ? need : s->available[j];
        search.bounds[j] = bound;
        report->search_bound[j] = bound;
        unsigned long long width = (unsigned long long)bound + 1ULL;
        if (combinations > BANKER_WHY_MAX_CANDIDATES / width)
            return WHY_ERR_SEARCH_SPACE_TOO_LARGE;
        combinations *= width;
    }

    why_enumerate(&search, 0);
    report->unsafe_count = report->candidate_count - report->safe_count;

    /* Coordinate-wise maxima need not form a safe vector. Verify the combined
     * vector before calling it the component-wise largest safe request. */
    if (report->has_safe_request)
        report->componentwise_max_is_safe =
            request_vector_is_safe(s, pid, report->componentwise_max);

    return WHY_OK;
}

bool banker_release(BankerState *s, int pid, const int rel[]) {
    if (banker_validate(s, NULL, NULL) != BANKER_OK) return false;
    if (pid < 0 || pid >= s->n) return false;
    for (int j = 0; j < s->m; j++)
        if (rel[j] < 0 || rel[j] > s->alloc[pid][j]) return false;
    for (int j = 0; j < s->m; j++) {
        s->alloc[pid][j] -= rel[j];
        s->available[j]  += rel[j];
    }
    return true;
}

bool banker_sequence_is_valid(const BankerState *s, const int seq[], int len) {
    if (len != s->n) return false;
    int  work[BANKER_MAX_RES];
    bool seen[BANKER_MAX_PROCS] = { false };
    memcpy(work, s->available, sizeof work);
    for (int k = 0; k < len; k++) {
        int i = seq[k];
        if (i < 0 || i >= s->n || seen[i]) return false;
        seen[i] = true;
        for (int j = 0; j < s->m; j++)
            if (s->max[i][j] - s->alloc[i][j] > work[j]) return false;
        for (int j = 0; j < s->m; j++) work[j] += s->alloc[i][j];
    }
    return true;
}
