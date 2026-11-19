/* ResourceGuard engine: Banker's Algorithm.
 * Pure C11. No GTK, no globals, no I/O (file loading lives in scenario.c).
 * Safe to call from tests, CLI, GUI or threads, as long as callers serialise
 * access to any one BankerState. */
#ifndef BANKER_H
#define BANKER_H

#include <stdbool.h>

#define BANKER_MAX_PROCS 16
#define BANKER_MAX_RES   8
#define BANKER_MAX_VALUE 1000000   /* upper bound for any single count; prevents overflow */
#define BANKER_WHY_MAX_CANDIDATES 100000ULL /* cap exhaustive request-space search */

typedef struct {
    int n, m;                                        /* processes, resource types */
    int total[BANKER_MAX_RES];                       /* instances that exist */
    int available[BANKER_MAX_RES];                   /* instances currently free */
    int max[BANKER_MAX_PROCS][BANKER_MAX_RES];       /* declared maximum demand */
    int alloc[BANKER_MAX_PROCS][BANKER_MAX_RES];     /* currently held */
} BankerState;                                       /* Need is derived: max - alloc */

/* Validation, checked in this order. */
typedef enum {
    BANKER_OK = 0,
    BANKER_ERR_DIMENSIONS,     /* n or m out of range */
    BANKER_ERR_NEGATIVE,       /* negative value anywhere */
    BANKER_ERR_RANGE,          /* value above BANKER_MAX_VALUE */
    BANKER_ERR_ALLOC_GT_MAX,
    BANKER_ERR_MAX_GT_TOTAL,
    BANKER_ERR_CONSERVATION    /* available + sum(alloc) != total */
} BankerStatus;

/* Request outcomes, decided in this order. */
typedef enum {
    REQ_GRANTED = 0,
    REQ_ERR_BAD_STATE,         /* state failed validation */
    REQ_ERR_BAD_INPUT,         /* bad process index or negative request */
    REQ_ERR_EXCEEDS_NEED,      /* asked for more than the declared maximum allows */
    REQ_WAIT_UNAVAILABLE,      /* not enough free right now; safety check not run */
    REQ_DENIED_UNSAFE          /* would leave an unsafe state; state untouched */
} RequestResult;

/* Trace of the safety algorithm, for a GUI/CLI to replay.
 * One TraceStep per pick. If the state is unsafe, the last step has picked == -1 (stuck). */
typedef struct {
    int  work[BANKER_MAX_RES];      /* Work vector before this step */
    bool finish[BANKER_MAX_PROCS];  /* Finish[] before this step */
    int  picked;                    /* process chosen, or -1 if none could proceed */
} TraceStep;

typedef struct {
    TraceStep steps[BANKER_MAX_PROCS + 1];
    int  count;
    int  final_work[BANKER_MAX_RES];
    bool safe;
} Trace;

/* Explain an unsafe request by exhaustively exploring all request vectors
 * that pass the Need and Available checks. The search is capped to keep the
 * analysis bounded; the GUI can use the visitor to build a safe-region map. */
typedef enum {
    WHY_OK = 0,
    WHY_ERR_BAD_STATE,
    WHY_ERR_BAD_INPUT,
    WHY_ERR_EXCEEDS_NEED,
    WHY_ERR_UNAVAILABLE,
    WHY_ERR_NOT_UNSAFE,
    WHY_ERR_SEARCH_SPACE_TOO_LARGE
} WhyNotStatus;

typedef struct {
    unsigned long long candidate_count; /* all vectors within Need and Available */
    unsigned long long safe_count;
    unsigned long long unsafe_count;
    int search_bound[BANKER_MAX_RES]; /* per-resource upper bound from Need and Available */
    int componentwise_max[BANKER_MAX_RES]; /* per-resource maximum over safe vectors */
    bool has_safe_request;
    bool componentwise_max_is_safe; /* true only if the combined max vector is safe */
    int nearest_safe_request[BANKER_MAX_RES]; /* closest safe alternative to rejected vector */
    unsigned long long nearest_distance; /* Manhattan distance */
    bool has_nearest_safe_request;
    Trace denial_trace; /* exact safety trace for the rejected request */
} WhyNotReport;

/* Called once for each candidate vector; request has s->m valid elements and
 * points to temporary storage valid only during the callback. Copy it if needed.
 * Return-free by design: every candidate is visited when the search succeeds. */
typedef void (*BankerWhyVisitor)(const int request[BANKER_MAX_RES],
                                 bool safe, void *user_data);

const char *banker_status_str(BankerStatus s);
const char *request_result_str(RequestResult r);
const char *banker_why_status_str(WhyNotStatus status);

void         banker_init(BankerState *s, int n, int m);                          /* zeroes everything */
BankerStatus banker_validate(const BankerState *s, int *bad_proc, int *bad_res); /* out params optional */
void         banker_need(const BankerState *s, int need[][BANKER_MAX_RES]);

/* Safety algorithm. Policy: lowest index first, rescan from P0 after every pick.
 * seq (capacity >= n), seq_len and trace are optional. Requires a valid state. */
bool banker_is_safe(const BankerState *s, int seq[], int *seq_len, Trace *trace);

/* Request algorithm. On anything but REQ_GRANTED, *s is left exactly as it was.
 * trace (optional) holds the safety run when one happened.
 * A zero request is GRANTED with no change. */
RequestResult banker_request(BankerState *s, int pid, const int req[], Trace *trace);

/* Requires a well-formed request that passes Need and Available but is denied
 * as unsafe. Enumerates vectors 0..min(Need, Available) per resource.
 * Returns WHY_ERR_SEARCH_SPACE_TOO_LARGE when the Cartesian product exceeds
 * BANKER_WHY_MAX_CANDIDATES. State is never modified. report is required;
 * visitor is optional and can be used to draw a safe-region map. */
WhyNotStatus banker_why_not(const BankerState *s, int pid, const int rejected_req[],
                            WhyNotReport *report, BankerWhyVisitor visitor,
                            void *user_data);

/* A process hands back part of what it holds. Returns false (and changes nothing) if invalid. */
bool banker_release(BankerState *s, int pid, const int rel[]);

/* Independent checker: does this order let every process finish? Used by tests. */
bool banker_sequence_is_valid(const BankerState *s, const int seq[], int len);

/* scenario.c: load a state from a text file. Parsing only; call banker_validate afterwards.
 * Returns false and fills err on failure. */
bool banker_load_scenario(const char *path, BankerState *out, char *err, int err_len);

#endif
