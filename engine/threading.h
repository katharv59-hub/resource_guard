/* ResourceGuard thread simulation module.
 *
 * Demonstrates concurrent resource allocation using POSIX threads.
 * Two modes:
 *
 *   TSIM_MODE_SAFE   — mutex held across the entire check-and-commit
 *                      transaction.  The Banker invariants always hold.
 *
 *   TSIM_MODE_TOCTOU — lock released between the safety check and the
 *                      commit.  Another thread can modify the shared state
 *                      in the gap, so the commit may act on a stale
 *                      decision.  Every individual memory access is still
 *                      mutex-protected (no C data race / no undefined
 *                      behaviour); only the LOGICAL atomicity is broken.
 *
 * Usage (random multi-thread simulation):
 *   ThreadSim *sim = tsim_create(&state, &config, TSIM_MODE_SAFE);
 *   tsim_run(sim);
 *   ThreadSimResults r = tsim_results(sim);
 *   for (int i = 0; i < tsim_event_count(sim); i++) { ... }
 *   tsim_destroy(sim);
 *
 * Usage (deterministic TOCTOU demonstration):
 *   ThreadSim *sim = tsim_create(&state, &config, TSIM_MODE_TOCTOU);
 *   ThreadSimToctouPair pair = { .pid_a = 1, .request_a = {1,2,0},
 *                                .pid_b = 4, .request_b = {3,2,0} };
 *   tsim_run_toctou_pair(sim, &pair);
 *   ThreadSimResults r = tsim_results(sim);
 *   // r.invariant_violations > 0 guaranteed for conflicting requests
 *   tsim_destroy(sim);
 *
 * No GTK dependency.  No global mutable state.  Linux only (pthreads).
 * The engine header (banker.h) is the only project dependency.
 *
 * Process completion:  each worker releases all remaining allocations
 * when it finishes its iterations, matching the Banker's Algorithm
 * assumption that completed processes free all resources. */

#ifndef THREADING_H
#define THREADING_H

#include "banker.h"
#include <stdbool.h>
#include <stdint.h>

/* ------------------------------------------------------------------ */
/*  Event types recorded during simulation                             */
/* ------------------------------------------------------------------ */

typedef enum {
    TSIM_THREAD_START,          /* worker thread began execution             */
    TSIM_THREAD_END,            /* worker thread finished all iterations     */
    TSIM_REQUEST,               /* thread submitted a resource request       */
    TSIM_GRANTED,               /* request was granted (state committed)     */
    TSIM_DENIED_UNSAFE,         /* request denied: would leave unsafe state  */
    TSIM_DENIED_WAIT,           /* request denied: resources unavailable     */
    TSIM_RELEASE,               /* thread released resources                 */
    TSIM_TOCTOU_SPLIT,          /* TOCTOU: lock released between check/commit*/
    TSIM_TOCTOU_BLIND_COMMIT,   /* TOCTOU: committing from stale decision   */
    TSIM_VIOLATION_INVALID,     /* state failed banker_validate()            */
    TSIM_VIOLATION_UNSAFE       /* state is valid but not safe               */
} ThreadSimEventType;

/* A single logged event.  Stored in the simulation's event buffer and
 * optionally forwarded to a live callback.  The available[] snapshot is
 * a copy taken while the state mutex was held; it is zeroed when
 * unavailable (e.g. for denied events in TOCTOU mode that could not
 * re-acquire the state lock cheaply). */
typedef struct {
    uint64_t           event_id;                    /* monotonic sequence: 0, 1, 2...    */
    uint64_t           timestamp_us;                /* us since sim start                */
    int                process_id;                  /* 0..n-1                            */
    ThreadSimEventType type;
    int                vector[BANKER_MAX_RES];      /* request / release                 */
    int                available[BANKER_MAX_RES];   /* Available snapshot                */
    int                m;                           /* resource count                    */
} ThreadSimEvent;

/* ------------------------------------------------------------------ */
/*  Simulation mode                                                    */
/* ------------------------------------------------------------------ */

typedef enum {
    TSIM_MODE_SAFE,   /* mutex held across check-and-commit              */
    TSIM_MODE_TOCTOU  /* lock released between check and commit          */
} ThreadSimMode;

/* ------------------------------------------------------------------ */
/*  Configuration                                                      */
/* ------------------------------------------------------------------ */

typedef struct {
    int      active_processes;       /* threads to launch, 1..n              */
    int      requests_per_process;   /* request attempts each thread makes   */
    uint64_t seed;                   /* deterministic PRNG seed              */
    int      think_time_us;          /* delay between requests (us)          */
    int      gap_time_us;            /* TOCTOU: delay between check & commit */
    int      release_pct;            /* 0..100  probability of releasing     */
} ThreadSimConfig;

/* ------------------------------------------------------------------ */
/*  Aggregate results                                                  */
/* ------------------------------------------------------------------ */

typedef struct {
    int        total_requests;
    int        granted;
    int        denied_unsafe;
    int        denied_wait;
    int        releases;
    int        invariant_violations;     /* banker_validate() != OK           */
    int        unsafe_states_observed;   /* valid but unsafe                  */
    bool       final_state_valid;
    bool       final_state_safe;
    BankerState final_state;
} ThreadSimResults;

/* ------------------------------------------------------------------ */
/*  Opaque simulation handle                                           */
/* ------------------------------------------------------------------ */

typedef struct ThreadSim ThreadSim;

/* Optional live callback for streaming events.
 *
 * Concurrency:
 *   Callbacks may execute concurrently on different worker threads.
 *   The callback function implementation MUST be thread-safe and re-entrant.
 *
 * Mutex safety:
 *   Invoked WITHOUT any ThreadSim internal mutex held.  The callback may
 *   safely call tsim_event_count() (which acquires log_mtx internally)
 *   without risking deadlocks.  It must NOT call tsim_event_at() during
 *   simulation — heap event pointers may be invalidated by concurrent
 *   realloc in log_event().
 *
 * Event-data lifetime:
 *   The 'event' pointer references a stack-allocated copy that is valid
 *   ONLY for the duration of the callback invocation.  The callback MUST
 *   copy the struct if the event data needs to be retained or enqueued.
 *
 * user_data lifetime:
 *   The 'user_data' pointer is caller-managed and MUST remain valid for the
 *   entire simulation run, until after tsim_run() or tsim_run_toctou_pair()
 *   returns and all worker threads have joined.
 *
 * Delivery order & event_id:
 *   Because callbacks fire outside internal locks across multiple worker
 *   threads, callback invocations may complete slightly out of event-log
 *   order due to OS thread scheduling.  The 'event->event_id' field contains
 *   a strictly increasing, zero-based sequence number assigned atomically
 *   under log_mtx, exactly matching the event's index in the internal log.
 *   Receivers can use event_id to sequence, reorder, or buffer events.
 *
 * GTK4 desktop integration:
 *   GTK4 is strictly single-threaded and not thread-safe.  Callbacks must
 *   NOT invoke GTK widget APIs directly from worker threads.  Instead, the
 *   callback must heap-allocate or copy the event (e.g., via g_memdup2() or
 *   an allocated queue item) and dispatch processing to the GTK main context
 *   using g_idle_add() or g_main_context_invoke().  The GTK main-loop handler
 *   then updates widgets safely on the UI thread and frees the copied event. */
typedef void (*ThreadSimCallback)(const ThreadSimEvent *event,
                                  void *user_data);

/* ------------------------------------------------------------------ */
/*  Deterministic TOCTOU demonstration                                 */
/* ------------------------------------------------------------------ */

/* A pair of requests for deterministic TOCTOU demonstration.
 * Both requests must be individually safe against the initial state.
 * The two process IDs must be distinct. */
typedef struct {
    int pid_a;                        /* first process (0..n-1)        */
    int request_a[BANKER_MAX_RES];
    int pid_b;                        /* second process (0..n-1)       */
    int request_b[BANKER_MAX_RES];
} ThreadSimToctouPair;

/* ------------------------------------------------------------------ */
/*  Public API                                                         */
/* ------------------------------------------------------------------ */

/* String helpers (same pattern as banker_status_str). */
const char *tsim_event_type_str(ThreadSimEventType type);
const char *tsim_mode_str(ThreadSimMode mode);

/* Create a simulation from a validated initial state.  The state is
 * copied; the caller's state is never modified.  Returns NULL on
 * invalid arguments (bad state, out-of-range config). */
ThreadSim *tsim_create(const BankerState *initial,
                       const ThreadSimConfig *config,
                       ThreadSimMode mode);

/* Set an optional live-event callback.  Must be called before run(). */
void tsim_set_callback(ThreadSim *sim, ThreadSimCallback cb,
                       void *user_data);

/* Run the simulation.  Blocks until every worker thread has joined.
 * Returns false if the simulation could not start (pthread failure).
 * May be called at most once per handle. */
bool tsim_run(ThreadSim *sim);

/* Run a deterministic 2-thread TOCTOU demonstration.  Both threads
 * snapshot the same initial state, check safety independently, then
 * blind-commit via pthread barriers — the result is timing-
 * independent and fully repeatable.
 *
 * The handle must have been created with TSIM_MODE_TOCTOU.  Both
 * requests must be individually safe against the initial state.
 * Returns false on invalid arguments or if the demonstration could
 * not start.  May be called at most once per handle (like tsim_run).
 *
 * After return, tsim_results() and tsim_event_*() report the
 * demonstration outcome, including any invariant violations. */
bool tsim_run_toctou_pair(ThreadSim *sim,
                          const ThreadSimToctouPair *pair);

/* Retrieve aggregate results (valid after tsim_run returns true). */
ThreadSimResults tsim_results(const ThreadSim *sim);

/* Event log access.
 * tsim_event_count() is thread-safe (acquires log_mtx internally);
 * safe to call from a live callback.
 *
 * tsim_event_at() is for post-run access only.  After tsim_run() or
 * tsim_run_toctou_pair() returns, all threads have joined, so the
 * returned pointer is stable.  Do NOT call during simulation from a
 * callback — the pointer could be invalidated by concurrent realloc. */
int                    tsim_event_count(const ThreadSim *sim);
const ThreadSimEvent  *tsim_event_at(const ThreadSim *sim, int index);

/* Free all resources.  Safe to call with NULL. */
void tsim_destroy(ThreadSim *sim);

#endif
