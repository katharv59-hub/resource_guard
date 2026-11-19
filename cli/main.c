/* banker_cli: interactive / scriptable front end for the engine.
 *   ./banker_cli tests/scenarios/demo.txt
 * Commands (also readable from stdin, so it works in scripts):
 *   show | safe | trace | request <pid> <r0> <r1> ... | release <pid> <r0> ... | reset | help | quit */
#include "banker.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_vec(const int *v, int m) {
    for (int j = 0; j < m; j++) printf("%3d", v[j]);
}

static void show(const BankerState *s) {
    int need[BANKER_MAX_PROCS][BANKER_MAX_RES];
    banker_need(s, need);
    printf("Total     :"); print_vec(s->total, s->m);     printf("\n");
    printf("Available :"); print_vec(s->available, s->m); printf("\n\n");
    printf("      Allocation      Max         Need\n");
    for (int i = 0; i < s->n; i++) {
        printf("P%-2d  ", i);
        print_vec(s->alloc[i], s->m); printf("   |");
        print_vec(s->max[i], s->m);   printf("   |");
        print_vec(need[i], s->m);     printf("\n");
    }
}

static void print_trace(const BankerState *s, const Trace *t) {
    for (int k = 0; k < t->count; k++) {
        const TraceStep *st = &t->steps[k];
        printf("  step %d: Work =", k + 1); print_vec(st->work, s->m);
        if (st->picked >= 0) printf("  -> P%d can finish, releases its allocation\n", st->picked);
        else printf("  -> no unfinished process fits: STUCK\n");
    }
    printf("  final Work ="); print_vec(t->final_work, s->m); printf("\n");
}

static void report_safety(const BankerState *s, bool with_trace) {
    int seq[BANKER_MAX_PROCS], len;
    Trace t;
    bool safe = banker_is_safe(s, seq, &len, &t);
    if (with_trace) print_trace(s, &t);
    if (safe) {
        printf("System is in SAFE state.\nSafe sequence: ");
        for (int i = 0; i < len; i++) printf("P%d%s", seq[i], i + 1 < len ? " -> " : "\n");
    } else {
        printf("System is in UNSAFE state. These processes cannot finish:");
        for (int i = 0; i < s->n; i++) {
            bool done = false;
            for (int k = 0; k < len; k++) if (seq[k] == i) done = true;
            if (!done) printf(" P%d", i);
        }
        printf("\n");
    }
}


static void report_why_not(const BankerState *s, int pid, const int request[]) {
    WhyNotReport report;
    WhyNotStatus status =
        banker_why_not(s, pid, request, &report, NULL, NULL);

    if (status != WHY_OK) {
        printf("Why-not analysis unavailable: %s\n", banker_why_status_str(status));
        return;
    }

    printf("REQUEST DENIED: no safe sequence exists after granting this vector.\n");
    print_trace(s, &report.denial_trace);

    const TraceStep *stuck =
        &report.denial_trace.steps[report.denial_trace.count - 1];
    int need[BANKER_MAX_PROCS][BANKER_MAX_RES];
    banker_need(s, need);

    printf("Processes still blocked at the final Work vector:\n");
    for (int i = 0; i < s->n; i++) {
        if (stuck->finish[i]) continue;
        printf("  P%d needs", i);
        for (int j = 0; j < s->m; j++)
            printf(" %d", need[i][j] - request[j] * (i == pid));
        printf(" but Work is");
        for (int j = 0; j < s->m; j++)
            printf(" %d", report.denial_trace.final_work[j]);
        printf("\n");
    }

    printf("Safe request region: %llu / %llu vectors; %llu unsafe.\n",
           report.safe_count, report.candidate_count, report.unsafe_count);

    if (report.has_safe_request) {
        printf("Per-resource maximum among safe vectors:");
        for (int j = 0; j < s->m; j++)
            printf(" %d", report.componentwise_max[j]);
        printf("\n");

        for (int j = 0; j < s->m; j++) {
            if (report.componentwise_max[j] < report.search_bound[j]) {
                printf("Resource %c boundary: feasible requests can ask up to %d, "
                       "but no safe request can ask for more than %d.\n",
                       'A' + j, report.search_bound[j], report.componentwise_max[j]);
            }
        }

        if (report.componentwise_max_is_safe) {
            printf("Largest component-wise safe request:");
            for (int j = 0; j < s->m; j++)
                printf(" %d", report.componentwise_max[j]);
            printf("\n");
        } else {
            printf("The per-resource maxima do not form one safe vector; there is no single component-wise largest request.\n");
        }

        if (report.has_nearest_safe_request) {
            printf("Nearest safe alternative (L1 distance %llu):",
                   report.nearest_distance);
            for (int j = 0; j < s->m; j++)
                printf(" %d", report.nearest_safe_request[j]);
            printf("\n");
        }
    } else {
        printf("No feasible request vector in this search space is safe.\n");
    }
}

static int parse_ints(char *rest, int *out, int want) {
    int got = 0;
    for (char *tok = strtok(rest, " \t\r\n"); tok; tok = strtok(NULL, " \t\r\n")) {
        char *end;
        long v = strtol(tok, &end, 10);
        if (*end != '\0') return -1;
        if (got < want) out[got] = (int)v;
        got++;
    }
    return got;
}

int main(int argc, char **argv) {
    if (argc != 2) { fprintf(stderr, "usage: %s <scenario-file>\n", argv[0]); return 2; }
    BankerState initial, s;
    char err[128];
    if (!banker_load_scenario(argv[1], &initial, err, sizeof err)) {
        fprintf(stderr, "error loading %s: %s\n", argv[1], err);
        return 1;
    }
    int bp, br;
    BankerStatus st = banker_validate(&initial, &bp, &br);
    if (st != BANKER_OK) {
        fprintf(stderr, "invalid state: %s", banker_status_str(st));
        if (bp >= 0) fprintf(stderr, " (process P%d", bp);
        if (bp >= 0 && br >= 0) fprintf(stderr, ", resource %d)", br);
        else if (bp >= 0) fprintf(stderr, ")");
        else if (br >= 0) fprintf(stderr, " (resource %d)", br);
        fprintf(stderr, "\n");
        return 1;
    }
    s = initial;
    puts("ResourceGuard CLI. Type 'help' for commands.");
    show(&s);

    char line[256];
    while (printf("> "), fflush(stdout), fgets(line, sizeof line, stdin)) {
        char *cmd = strtok(line, " \t\r\n");
        if (!cmd) continue;
        char *rest = strtok(NULL, "\n");
        char empty[1] = "";
        if (!rest) rest = empty;

        if (!strcmp(cmd, "quit") || !strcmp(cmd, "exit")) break;
        else if (!strcmp(cmd, "help"))
            puts("show | safe | trace | request <pid> <vec> | why <pid> <vec> | release <pid> <vec> | reset | quit");
        else if (!strcmp(cmd, "show")) show(&s);
        else if (!strcmp(cmd, "safe")) report_safety(&s, false);
        else if (!strcmp(cmd, "trace")) report_safety(&s, true);
        else if (!strcmp(cmd, "reset")) { s = initial; puts("State reset."); }
        else if (!strcmp(cmd, "request") || !strcmp(cmd, "release") || !strcmp(cmd, "why")) {
            int vals[BANKER_MAX_RES + 1];
            int got = parse_ints(rest, vals, BANKER_MAX_RES + 1);
            if (got != s.m + 1) { printf("expected: %s <pid> and %d numbers\n", cmd, s.m); continue; }
            int pid = vals[0];
            if (!strcmp(cmd, "request")) {
                Trace t;
                RequestResult r = banker_request(&s, pid, vals + 1, &t);
                if (t.count > 0) print_trace(&s, &t);
                printf("%s\n", request_result_str(r));
            } else if (!strcmp(cmd, "release")) {
                printf(banker_release(&s, pid, vals + 1) ? "Released.\n" : "Release rejected (bad process or amount).\n");
            } else {
                report_why_not(&s, pid, vals + 1);
            }
        } else printf("unknown command '%s' (try 'help')\n", cmd);
    }
    return 0;
}
