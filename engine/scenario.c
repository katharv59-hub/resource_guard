/* Scenario file format (whitespace separated, '#' starts a comment):
 *
 *   processes 5
 *   resources 3
 *   total     10 5 7
 *   available 3 3 2
 *   allocation        # n rows of m numbers
 *   0 1 0
 *   ...
 *   max               # n rows of m numbers
 *   7 5 3
 *   ...
 *
 * 'processes' and 'resources' must come first. This file only parses; the caller validates. */
#include "banker.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { FILE *f; } Lexer;

/* Reads the next token into buf. Returns false at end of file. */
static bool next_token(Lexer *lx, char *buf, size_t cap) {
    int c;
    for (;;) {
        c = fgetc(lx->f);
        if (c == EOF) return false;
        if (c == '#') { while ((c = fgetc(lx->f)) != EOF && c != '\n') { } continue; }
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        break;
    }
    size_t len = 0;
    while (c != EOF && c != ' ' && c != '\t' && c != '\r' && c != '\n' && c != '#') {
        if (len + 1 < cap) buf[len++] = (char)c;
        c = fgetc(lx->f);
    }
    if (c == '#') ungetc(c, lx->f);
    buf[len] = '\0';
    return true;
}

static bool read_int(Lexer *lx, int *out) {
    char tok[64];
    if (!next_token(lx, tok, sizeof tok)) return false;
    char *end;
    long v = strtol(tok, &end, 10);
    if (*end != '\0' || v < -1000000000L || v > 1000000000L) return false;
    *out = (int)v;
    return true;
}

static void set_err(char *err, int len, const char *msg) {
    if (err && len > 0) snprintf(err, (size_t)len, "%s", msg);
}

bool banker_load_scenario(const char *path, BankerState *out, char *err, int err_len) {
    FILE *f = fopen(path, "r");
    if (!f) { set_err(err, err_len, "cannot open file"); return false; }
    Lexer lx = { f };
    banker_init(out, 0, 0);
    bool have_n = false, have_m = false;
    char tok[64];

    while (next_token(&lx, tok, sizeof tok)) {
        if (strcmp(tok, "processes") == 0) {
            if (!read_int(&lx, &out->n)) { set_err(err, err_len, "bad value after 'processes'"); goto fail; }
            have_n = true;
        } else if (strcmp(tok, "resources") == 0) {
            if (!read_int(&lx, &out->m)) { set_err(err, err_len, "bad value after 'resources'"); goto fail; }
            have_m = true;
        } else if (strcmp(tok, "total") == 0 || strcmp(tok, "available") == 0 ||
                   strcmp(tok, "allocation") == 0 || strcmp(tok, "max") == 0) {
            if (!have_n || !have_m) { set_err(err, err_len, "'processes' and 'resources' must come first"); goto fail; }
            if (out->n < 1 || out->n > BANKER_MAX_PROCS || out->m < 1 || out->m > BANKER_MAX_RES) {
                set_err(err, err_len, "process or resource count out of range"); goto fail;
            }
            bool is_vec = (tok[0] == 't' || tok[0] == 'a') && strcmp(tok, "allocation") != 0;
            if (is_vec) {
                int *dst = (strcmp(tok, "total") == 0) ? out->total : out->available;
                for (int j = 0; j < out->m; j++)
                    if (!read_int(&lx, &dst[j])) { set_err(err, err_len, "too few or invalid numbers in a vector"); goto fail; }
            } else {
                for (int i = 0; i < out->n; i++)
                    for (int j = 0; j < out->m; j++) {
                        int *dst = (strcmp(tok, "max") == 0) ? &out->max[i][j] : &out->alloc[i][j];
                        if (!read_int(&lx, dst)) { set_err(err, err_len, "too few or invalid numbers in a matrix"); goto fail; }
                    }
            }
        } else {
            char msg[128];
            snprintf(msg, sizeof msg, "unknown keyword '%.40s'", tok);
            set_err(err, err_len, msg);
            goto fail;
        }
    }
    if (!have_n || !have_m) { set_err(err, err_len, "missing 'processes' or 'resources'"); goto fail; }
    fclose(f);
    return true;
fail:
    fclose(f);
    return false;
}
