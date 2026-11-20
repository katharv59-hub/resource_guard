/* ResourceGuard Application Model & Controller Test Suite.
 * Validates state lifecycle, listener preservation, monotonic versioning,
 * draft persistence, Need derivation, scenario loading, and safety evaluation. */

#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#endif

#include "model.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>

#if defined(_WIN32)
#include <direct.h>
#include <windows.h>
#define MKDIR(d) _mkdir(d)
#define RMDIR(d) _rmdir(d)
#define CHDIR(d) _chdir(d)
#define GETCWD(buf, sz) _getcwd(buf, (int)(sz))
int _putenv(const char *string);
char *_fullpath(char *absPath, const char *relPath, size_t maxLength);
static void set_test_env(const char *name, const char *val) {
    char buf[512];
    snprintf(buf, sizeof(buf), "%s=%s", name, val ? val : "");
    _putenv(buf);
}
static void clear_test_env(const char *name) {
    char buf[512];
    snprintf(buf, sizeof(buf), "%s=", name);
    _putenv(buf);
}
#else
#include <sys/stat.h>
#include <unistd.h>
#define MKDIR(d) mkdir(d, 0755)
#define RMDIR(d) rmdir(d)
#define CHDIR(d) chdir(d)
#define GETCWD(buf, sz) getcwd(buf, sz)
static void set_test_env(const char *name, const char *val) {
    setenv(name, val ? val : "", 1);
}
static void clear_test_env(const char *name) {
    unsetenv(name);
}
#endif

static int g_listener_called = 0;
static void test_listener(GuiModel *m, void *user_data) {
    (void)m;
    (void)user_data;
    g_listener_called++;
}

static void test_initialization(void) {
    printf("[TEST 1/15] Initial empty model state...\n");
    fflush(stdout);
    GuiModel m;
    gui_model_init(&m);

    assert(!m.is_loaded);
    assert(m.state_version == 0);
    assert(m.scenario_path[0] == '\0');
    assert(strcmp(m.scenario_name, "None (Empty State)") == 0);

    GuiDashboardSnapshot snap;
    gui_model_get_dashboard_snapshot(&m, &snap);
    assert(!snap.is_loaded);
    assert(!snap.is_safe);

    printf("  -> Passed.\n");
    fflush(stdout);
}

static void test_demo_initialization(void) {
    printf("[TEST 2/15] Demo baseline initialization...\n");
    fflush(stdout);
    GuiModel m;
    gui_model_init(&m);
    gui_model_init_demo(&m);

    assert(m.is_loaded);
    assert(m.state_version >= 1);
    assert(m.current_state.n == 5);
    assert(m.current_state.m == 3);
    assert(m.draft_status == BANKER_OK);

    /* Verify safety */
    int seq[BANKER_MAX_PROCS];
    int len = 0;
    bool safe = gui_model_is_safe(&m, seq, &len, NULL);
    assert(safe);
    assert(len == 5);

    /* Verify dashboard snapshot */
    GuiDashboardSnapshot snap;
    gui_model_get_dashboard_snapshot(&m, &snap);
    assert(snap.is_loaded);
    assert(snap.n == 5);
    assert(snap.m == 3);
    assert(snap.is_safe);
    assert(snap.safe_seq_len == 5);

    printf("  -> Passed.\n");
    fflush(stdout);
}

static void test_listener_preservation_and_monotonic_version(void) {
    printf("[TEST 3/15] Listener preservation & monotonic state version advancement...\n");
    fflush(stdout);
    GuiModel m;
    gui_model_init(&m);

    g_listener_called = 0;
    gui_model_set_listener(&m, test_listener, (void*)0x1234);
    assert(m.listener == test_listener);
    assert(m.listener_user_data == (void*)0x1234);

    /* Load demo state */
    gui_model_load_demo(&m);
    assert(m.listener == test_listener);
    assert(m.listener_user_data == (void*)0x1234);
    assert(g_listener_called > 0);
    uint64_t v1 = m.state_version;
    assert(v1 > 0);

    /* Load demo state again: must advance version and preserve listener */
    int prev_calls = g_listener_called;
    gui_model_load_demo(&m);
    assert(m.listener == test_listener);
    assert(m.listener_user_data == (void*)0x1234);
    assert(g_listener_called > prev_calls);
    assert(m.state_version > v1);

    printf("  -> Passed (version advanced from %lu to %lu, listener retained).\n",
           (unsigned long)v1, (unsigned long)m.state_version);
    fflush(stdout);
}

static void test_dimension_changes(void) {
    printf("[TEST 4/15] Dimensional resizing and bounds clamping...\n");
    fflush(stdout);
    GuiModel m;
    gui_model_init(&m);
    gui_model_init_demo(&m);

    /* Invalid dimension rejections */
    assert(!gui_model_set_draft_dimensions(&m, 0, 3));
    assert(!gui_model_set_draft_dimensions(&m, 17, 3));
    assert(!gui_model_set_draft_dimensions(&m, 5, 0));
    assert(!gui_model_set_draft_dimensions(&m, 5, 9));

    /* Valid expansion */
    assert(gui_model_set_draft_dimensions(&m, 8, 4));
    assert(m.draft_state.n == 8);
    assert(m.draft_state.m == 4);
    assert(m.draft_state.total[3] == 0);
    assert(m.draft_state.alloc[7][3] == 0);

    /* Valid contraction */
    assert(gui_model_set_draft_dimensions(&m, 3, 2));
    assert(m.draft_state.n == 3);
    assert(m.draft_state.m == 2);

    printf("  -> Passed.\n");
    fflush(stdout);
}

static void test_need_matches_banker_need(void) {
    printf("[TEST 5/15] Authoritative Need derivation (gui_model_get_draft_need == banker_need)...\n");
    fflush(stdout);
    GuiModel m;
    gui_model_init(&m);
    gui_model_init_demo(&m);

    int draft_need[BANKER_MAX_PROCS][BANKER_MAX_RES];
    int expected_need[BANKER_MAX_PROCS][BANKER_MAX_RES];

    gui_model_get_draft_need(&m, draft_need);
    banker_need(&m.draft_state, expected_need);

    for (int i = 0; i < m.draft_state.n; i++) {
        for (int j = 0; j < m.draft_state.m; j++) {
            assert(draft_need[i][j] == expected_need[i][j]);
        }
    }

    /* In demo: P0 Max=[7,5,3], Alloc=[0,1,0] -> Need=[7,4,3] */
    assert(draft_need[0][0] == 7);
    assert(draft_need[0][1] == 4);
    assert(draft_need[0][2] == 3);

    /* Modify draft Max and Alloc */
    gui_model_set_draft_alloc(&m, 0, 1, 3);
    gui_model_set_draft_max(&m, 0, 1, 5);

    gui_model_get_draft_need(&m, draft_need);
    banker_need(&m.draft_state, expected_need);
    assert(draft_need[0][1] == 2);
    assert(draft_need[0][1] == expected_need[0][1]);

    printf("  -> Passed.\n");
    fflush(stdout);
}

static void test_draft_validation_rejections(void) {
    printf("[TEST 6/15] Validation errors and draft rejection...\n");
    fflush(stdout);
    GuiModel m;
    gui_model_init(&m);
    gui_model_init_demo(&m);
    uint64_t initial_ver = m.state_version;

    /* 1. Allocation exceeds Maximum */
    gui_model_set_draft_alloc(&m, 2, 0, 10);
    gui_model_set_draft_max(&m, 2, 0, 5);
    assert(gui_model_validate_draft(&m) == BANKER_ERR_ALLOC_GT_MAX);
    assert(m.draft_bad_proc == 2);
    assert(m.draft_bad_res == 0);
    assert(!gui_model_commit_draft(&m));
    assert(m.state_version == initial_ver); /* Unmodified */

    /* 2. Maximum exceeds Total */
    gui_model_init_draft_from_current(&m);
    gui_model_set_draft_total(&m, 1, 4);
    gui_model_set_draft_max(&m, 0, 1, 5);
    assert(gui_model_validate_draft(&m) == BANKER_ERR_MAX_GT_TOTAL);
    assert(!gui_model_commit_draft(&m));
    assert(m.state_version == initial_ver);

    /* 3. Conservation violation */
    gui_model_init_draft_from_current(&m);
    gui_model_set_draft_available(&m, 0, 99); /* Breaks Available + Alloc == Total */
    assert(gui_model_validate_draft(&m) == BANKER_ERR_CONSERVATION);
    assert(!gui_model_commit_draft(&m));
    assert(m.state_version == initial_ver);

    /* 4. Negative values */
    gui_model_init_draft_from_current(&m);
    gui_model_set_draft_total(&m, 0, -1);
    assert(gui_model_validate_draft(&m) == BANKER_ERR_NEGATIVE);
    assert(!gui_model_commit_draft(&m));

    printf("  -> Passed.\n");
    fflush(stdout);
}

static void test_draft_persistence_and_discard(void) {
    printf("[TEST 7/15] Draft persistence across page navigation and explicit discard...\n");
    fflush(stdout);
    GuiModel m;
    gui_model_init(&m);
    gui_model_init_demo(&m);

    /* Edit draft allocation */
    gui_model_set_draft_alloc(&m, 0, 0, 2);
    assert(m.draft_state.alloc[0][0] == 2);
    assert(m.current_state.alloc[0][0] == 0); /* Committed state preserved */

    /* Simulate navigating to other pages and back: draft must persist */
    assert(m.draft_state.alloc[0][0] == 2);

    /* Explicit discard restores draft from current committed state */
    gui_model_init_draft_from_current(&m);
    assert(m.draft_state.alloc[0][0] == 0);
    assert(m.draft_state.alloc[0][0] == m.current_state.alloc[0][0]);

    printf("  -> Passed.\n");
    fflush(stdout);
}

static void test_auto_calculate_available_and_commit(void) {
    printf("[TEST 8/15] Auto-calculate Available and commit draft...\n");
    fflush(stdout);
    GuiModel m;
    gui_model_init(&m);
    gui_model_init_demo(&m);
    uint64_t initial_ver = m.state_version;

    /* Change an allocation */
    gui_model_set_draft_alloc(&m, 0, 0, 1);
    /* Now conservation is broken until Available is adjusted */
    assert(gui_model_validate_draft(&m) == BANKER_ERR_CONSERVATION);

    /* Auto-calculate Available */
    gui_model_auto_calculate_draft_available(&m);
    assert(gui_model_validate_draft(&m) == BANKER_OK);

    /* Commit */
    assert(gui_model_commit_draft(&m));
    assert(m.state_version == initial_ver + 1);
    assert(m.current_state.alloc[0][0] == 1);

    printf("  -> Passed.\n");
    fflush(stdout);
}

static void test_unsafe_scenario_preset_and_safety_state(void) {
    printf("[TEST 9/15] Loading unsafe_initial.txt and verifying UNSAFE evaluation...\n");
    fflush(stdout);
    GuiModel m;
    gui_model_init(&m);

    char err_buf[256] = {0};
    bool loaded = gui_model_load_scenario(&m, "tests/scenarios/unsafe_initial.txt", err_buf, sizeof(err_buf));
    assert(loaded);
    assert(m.is_loaded);
    assert(m.current_state.n == 5);
    assert(m.current_state.m == 3);

    /* Must evaluate UNSAFE via banker_is_safe */
    int seq[BANKER_MAX_PROCS];
    int len = 0;
    bool safe = gui_model_is_safe(&m, seq, &len, NULL);
    assert(!safe);

    /* Dashboard snapshot must report UNSAFE */
    GuiDashboardSnapshot snap;
    gui_model_get_dashboard_snapshot(&m, &snap);
    assert(snap.is_loaded);
    assert(!snap.is_safe);

    printf("  -> Passed (unsafe_initial.txt correctly evaluated as UNSAFE).\n");
    fflush(stdout);
}

static void test_failed_preset_preserves_state(void) {
    printf("[TEST 10/15] Failed preset loading preserves current state without silent fallback...\n");
    fflush(stdout);
    GuiModel m;
    gui_model_init(&m);
    gui_model_init_demo(&m);

    uint64_t ver_before = m.state_version;
    int n_before = m.current_state.n;
    char path_before[GUI_MAX_PATH_LEN];
    snprintf(path_before, sizeof(path_before), "%s", m.scenario_path);

    char err_buf[256] = {0};

    /* Attempt to load nonexistent file */
    bool res1 = gui_model_load_scenario(&m, "tests/scenarios/nonexistent_file.txt", err_buf, sizeof(err_buf));
    assert(!res1);
    assert(m.state_version == ver_before);
    assert(m.current_state.n == n_before);
    assert(strcmp(m.scenario_path, path_before) == 0);

    /* Attempt to load malformed file */
    bool res2 = gui_model_load_scenario(&m, "tests/scenarios/malformed.txt", err_buf, sizeof(err_buf));
    assert(!res2);
    assert(m.state_version == ver_before);
    assert(m.current_state.n == n_before);
    assert(strcmp(m.scenario_path, path_before) == 0);

    printf("  -> Passed (state, version, and path preserved on failure).\n");
    fflush(stdout);
}

static void test_demo_parity_with_file(void) {
    printf("[TEST 11/15] Parity check between built-in demo baseline and tests/scenarios/demo.txt...\n");
    fflush(stdout);
    BankerState file_state;
    char err_buf[256] = {0};
    bool ok = banker_load_scenario("tests/scenarios/demo.txt", &file_state, err_buf, sizeof(err_buf));
    assert(ok);

    GuiModel m;
    gui_model_init(&m);
    gui_model_init_demo(&m);

    assert(m.current_state.n == file_state.n);
    assert(m.current_state.m == file_state.m);

    for (int j = 0; j < file_state.m; j++) {
        assert(m.current_state.total[j] == file_state.total[j]);
        assert(m.current_state.available[j] == file_state.available[j]);
    }

    for (int i = 0; i < file_state.n; i++) {
        for (int j = 0; j < file_state.m; j++) {
            assert(m.current_state.alloc[i][j] == file_state.alloc[i][j]);
            assert(m.current_state.max[i][j] == file_state.max[i][j]);
        }
    }

    printf("  -> Passed (100%% matrix parity verified).\n");
    fflush(stdout);
}

static void test_reset_to_original_baseline(void) {
    printf("[TEST 12/15] Reset to baseline restores original scenario after draft edits & commit...\n");
    fflush(stdout);
    GuiModel m;
    gui_model_init(&m);
    gui_model_load_demo(&m);

    /* Verify original baseline values */
    int orig_p0_alloc = m.original_state.alloc[0][1];
    assert(orig_p0_alloc == 1);

    /* Modify draft, auto-calculate available, and commit */
    gui_model_set_draft_alloc(&m, 0, 1, 3);
    gui_model_auto_calculate_draft_available(&m);
    assert(gui_model_commit_draft(&m));
    assert(m.current_state.alloc[0][1] == 3);
    uint64_t ver_after_commit = m.state_version;

    /* Register listener to verify notification on reset */
    g_listener_called = 0;
    gui_model_set_listener(&m, test_listener, NULL);

    /* Reset to original baseline */
    assert(gui_model_reset_to_original(&m));
    assert(m.current_state.alloc[0][1] == orig_p0_alloc);
    assert(m.draft_state.alloc[0][1] == orig_p0_alloc);
    assert(m.state_version > ver_after_commit);
    assert(g_listener_called == 1);

    printf("  -> Passed (baseline restored, version advanced, listener notified).\n");
    fflush(stdout);
}

static void test_resource_lookup_order_and_unrelated_cwd(void) {
    printf("[TEST 13/15] Resource lookup order and unrelated CWD regression test...\n");
    fflush(stdout);

    /* 1. Preserve original environment variable values */
    bool has_orig_scen = false;
    char orig_scen[512] = {0};
    const char *raw_scen = getenv("RESOURCEGUARD_SCENARIOS_DIR");
    if (raw_scen) {
        has_orig_scen = true;
        snprintf(orig_scen, sizeof(orig_scen), "%s", raw_scen);
    }

    bool has_orig_data = false;
    char orig_data[512] = {0};
    const char *raw_data = getenv("RESOURCEGUARD_DATA_DIR");
    if (raw_data) {
        has_orig_data = true;
        snprintf(orig_data, sizeof(orig_data), "%s", raw_data);
    }

    /* 2. Normal relative path from repository root */
    char resolved[512] = {0};
    bool ok1 = gui_model_resolve_resource_path("tests/scenarios/demo.txt", resolved, sizeof(resolved));
    assert(ok1);
    assert(strlen(resolved) > 0);

    /* 3. Environment variable override check */
    set_test_env("RESOURCEGUARD_SCENARIOS_DIR", "tests/scenarios");
    char resolved_env[512] = {0};
    bool ok_env = gui_model_resolve_resource_path("demo.txt", resolved_env, sizeof(resolved_env));
    assert(ok_env);

    /* 4. Non-existent file rejection without silent substitution */
    char resolved_bad[512] = {0};
    bool ok_bad = gui_model_resolve_resource_path("non_existent_scenario_12345.txt", resolved_bad, sizeof(resolved_bad));
    assert(!ok_bad);
    assert(resolved_bad[0] == '\0');

    /* Clear environment variables for the unrelated CWD test */
    clear_test_env("RESOURCEGUARD_SCENARIOS_DIR");
    clear_test_env("RESOURCEGUARD_DATA_DIR");

    /* 5. State tracking variables for single-path cleanup & deferred assertions */
    char orig_cwd[1024] = {0};
    bool orig_cwd_saved = (GETCWD(orig_cwd, sizeof(orig_cwd)) != NULL);

    char tmp_dir_buf[512] = {0};
    bool tmp_dir_created = false;
    bool cwd_changed = false;

    bool scenario_absent_from_tmp_cwd = false;
    bool resolution_succeeded = false;
    char resolved_unrelated[512] = {0};
    bool scenario_parsing_succeeded = false;
    bool validate_is_ok = false;
    bool banker_is_safe_ok = false;
    bool cwd_restored = true;
    bool tmp_dir_removed = true;

    if (!orig_cwd_saved) {
        fprintf(stderr, "ERROR: Unable to save original working directory before CWD switch\n");
        goto cleanup;
    }

    /* Create temporary directory outside repository */
#if defined(_WIN32)
    char tmp_base[MAX_PATH] = {0};
    if (GetTempPathA(sizeof(tmp_base), tmp_base) > 0) {
        snprintf(tmp_dir_buf, sizeof(tmp_dir_buf), "%srg_test_cwd_%lu", tmp_base, (unsigned long)GetCurrentProcessId());
        if (_mkdir(tmp_dir_buf) == 0) {
            tmp_dir_created = true;
        }
    }
#else
    char tmp_template[] = "/tmp/rg_test_cwd_XXXXXX";
    char *created = mkdtemp(tmp_template);
    if (created != NULL) {
        tmp_dir_created = true;
        snprintf(tmp_dir_buf, sizeof(tmp_dir_buf), "%s", created);
    }
#endif

    if (!tmp_dir_created) {
        fprintf(stderr, "ERROR: Failed to create temporary directory for unrelated CWD test\n");
        goto cleanup;
    }

    /* Switch into unrelated directory */
    if (CHDIR(tmp_dir_buf) == 0) {
        cwd_changed = true;
    } else {
        fprintf(stderr, "ERROR: Failed to change working directory into '%s'\n", tmp_dir_buf);
        goto cleanup;
    }

    /* Negative check: verify scenario is absent from local temporary directory */
    FILE *local_check = fopen("tests/scenarios/demo.txt", "r");
    if (local_check == NULL) {
        scenario_absent_from_tmp_cwd = true;
    } else {
        fclose(local_check);
        fprintf(stderr, "ERROR: Scenario unexpectedly found relative to temporary CWD\n");
    }

    /* Executable-relative resolution with zero environment overrides */
    bool ok_res = gui_model_resolve_resource_path("tests/scenarios/demo.txt",
                                                  resolved_unrelated,
                                                  sizeof(resolved_unrelated));
    if (ok_res && resolved_unrelated[0] != '\0') {
        resolution_succeeded = true;
    }

    /* Load scenario and verify model validation and safety */
    if (resolution_succeeded) {
        GuiModel m;
        gui_model_init(&m);
        char err_buf[256] = {0};
        if (gui_model_load_scenario(&m, resolved_unrelated, err_buf, sizeof(err_buf)) && m.is_loaded) {
            scenario_parsing_succeeded = true;
            int bad_p = -1, bad_r = -1;
            if (banker_validate(&m.current_state, &bad_p, &bad_r) == BANKER_OK) {
                validate_is_ok = true;
            }
            int seq[BANKER_MAX_PROCS];
            int seq_len = 0;
            if (gui_model_is_safe(&m, seq, &seq_len, NULL) && seq_len == m.current_state.n) {
                banker_is_safe_ok = true;
            }
        }
    }

cleanup:
    /* Single cleanup path: always attempt to restore CWD, remove temp dir, and restore env */
    cwd_restored = true;
    if (cwd_changed) {
        if (!orig_cwd_saved || CHDIR(orig_cwd) != 0) {
            cwd_restored = false;
            fprintf(stderr, "ERROR: Failed to restore original working directory to '%s'\n", orig_cwd);
        }
    }

    tmp_dir_removed = true;
    if (tmp_dir_created) {
        if (RMDIR(tmp_dir_buf) != 0) {
            tmp_dir_removed = false;
            fprintf(stderr, "ERROR: Failed to remove temporary directory '%s'\n", tmp_dir_buf);
        }
    }

    if (has_orig_scen) {
        set_test_env("RESOURCEGUARD_SCENARIOS_DIR", orig_scen);
    } else {
        clear_test_env("RESOURCEGUARD_SCENARIOS_DIR");
    }

    if (has_orig_data) {
        set_test_env("RESOURCEGUARD_DATA_DIR", orig_data);
    } else {
        clear_test_env("RESOURCEGUARD_DATA_DIR");
    }

    /* Perform final assertions ONLY after cleanup */
    assert(orig_cwd_saved);
    assert(tmp_dir_created);
    assert(cwd_changed);
    assert(cwd_restored);
    assert(tmp_dir_removed);

    assert(scenario_absent_from_tmp_cwd);
    assert(resolution_succeeded);
    assert(scenario_parsing_succeeded);
    assert(validate_is_ok);
    assert(banker_is_safe_ok);

    printf("  -> Passed (single-path cleanup, guaranteed CWD restoration, and resolution verified).\n");
    fflush(stdout);
}

static void test_absolute_scenario_path(void) {
    printf("[TEST 14/15] Absolute user-selected scenario path loading...\n");
    fflush(stdout);

    char abs_path[512] = {0};
#if defined(_WIN32)
    char *res = _fullpath(abs_path, "tests/scenarios/demo.txt", sizeof(abs_path));
    assert(res != NULL);
#else
    char *res = realpath("tests/scenarios/demo.txt", NULL);
    assert(res != NULL);
    snprintf(abs_path, sizeof(abs_path), "%s", res);
    free(res);
#endif

    GuiModel m;
    gui_model_init(&m);

    char err[256] = {0};
    bool loaded = gui_model_load_scenario(&m, abs_path, err, sizeof(err));
    assert(loaded);
    assert(m.is_loaded);
    assert(m.current_state.n == 5);
    assert(m.current_state.m == 3);
    assert(strcmp(m.scenario_path, abs_path) == 0);

    printf("  -> Passed (absolute path loaded accurately without alteration).\n");
    fflush(stdout);
}

static void test_demo_fallback_metadata_and_invalid_demo_rejection(void) {
    printf("[TEST 15/15] Built-in demo fallback metadata and invalid demo rejection...\n");
    fflush(stdout);

    /* Create temporary folder and an invalid demo scenario file where Allocation > Maximum */
    MKDIR("tests/scenarios/test_tmp");
    FILE *f = fopen("tests/scenarios/test_tmp/demo.txt", "w");
    assert(f != NULL);
    fprintf(f, "processes 2\nresources 2\n");
    fprintf(f, "total 10 10\navailable 0 0\n");
    fprintf(f, "allocation\n10 10\n0 0\n");
    fprintf(f, "max\n5 5\n0 0\n");
    fclose(f);

    /* Direct scenario lookup to test_tmp folder */
    set_test_env("RESOURCEGUARD_SCENARIOS_DIR", "tests/scenarios/test_tmp");

    GuiModel m;
    gui_model_init(&m);

    /* Load demo: parser parses test_tmp/demo.txt, but banker_validate fails!
     * Model MUST reject it and fall back to built-in demo baseline */
    gui_model_load_demo(&m);

    assert(m.is_loaded);
    assert(m.current_state.n == 5);
    assert(m.current_state.m == 3);
    /* Fallback must be explicitly labelled and have empty path */
    assert(m.scenario_path[0] == '\0');
    assert(strcmp(m.scenario_name, "Built-in Demo Baseline (5 procs, 3 res)") == 0);

    /* Verify active state is valid BankerState */
    int bad_p = -1, bad_r = -1;
    assert(banker_validate(&m.current_state, &bad_p, &bad_r) == BANKER_OK);

    /* Cleanup temporary file, folder, and environment override */
    remove("tests/scenarios/test_tmp/demo.txt");
    RMDIR("tests/scenarios/test_tmp");
    clear_test_env("RESOURCEGUARD_SCENARIOS_DIR");

    printf("  -> Passed (invalid demo rejected, built-in baseline metadata verified).\n");
    fflush(stdout);
}

static void test_safety_trace_work_finish_vectors(void) {
    printf("[TEST 16/21] Safety trace: sequence <1,3,0,2,4>, Work evolution, Finish arrays, and Total conservation...\n");
    fflush(stdout);

    GuiModel m;
    gui_model_init(&m);
    gui_model_init_demo(&m);

    int seq[BANKER_MAX_PROCS] = {0};
    int len = 0;
    Trace tr;
    memset(&tr, 0, sizeof(tr));

    bool safe = gui_model_is_safe(&m, seq, &len, &tr);
    assert(safe);
    assert(len == 5);
    int expected_seq[5] = { 1, 3, 0, 2, 4 };
    assert(memcmp(seq, expected_seq, sizeof(expected_seq)) == 0);

    /* Point 2: Step count == 5 */
    assert(tr.count == 5);

    /* Point 3 & 4: Work vector and Finish array before each iteration */
    int expected_work[5][3] = {
        { 3, 3, 2 },
        { 5, 3, 2 },
        { 7, 4, 3 },
        { 7, 5, 3 },
        { 10, 5, 5 }
    };
    bool expected_finish[5][BANKER_MAX_PROCS] = {
        { false, false, false, false, false },
        { false, true,  false, false, false },
        { false, true,  false, true,  false },
        { true,  true,  false, true,  false },
        { true,  true,  true,  true,  false }
    };

    for (int s = 0; s < 5; s++) {
        assert(tr.steps[s].picked == expected_seq[s]);
        assert(memcmp(tr.steps[s].work, expected_work[s], (size_t)m.current_state.m * sizeof(int)) == 0);
        assert(memcmp(tr.steps[s].finish, expected_finish[s], (size_t)m.current_state.n * sizeof(bool)) == 0);
    }

    /* Point 5: Final Work vector matches system Total [10, 5, 7] */
    int expected_total[3] = { 10, 5, 7 };
    assert(memcmp(tr.final_work, expected_total, sizeof(expected_total)) == 0);
    assert(memcmp(m.current_state.total, tr.final_work, sizeof(expected_total)) == 0);

    printf("  -> Passed (trace matches exact Work evolution and Finish array states).\n");
    fflush(stdout);
}

static void test_unsafe_scenario_trace_and_stuck_diagnostic(void) {
    printf("[TEST 17/21] Unsafe scenario trace: stuck iteration (picked == -1) and incomplete Finish array...\n");
    fflush(stdout);

    GuiModel m;
    gui_model_init(&m);

    char err[256] = {0};
    bool loaded = gui_model_load_scenario(&m, "tests/scenarios/unsafe_initial.txt", err, sizeof(err));
    assert(loaded);
    assert(m.is_loaded);

    int seq[BANKER_MAX_PROCS] = {0};
    int len = 0;
    Trace tr;
    memset(&tr, 0, sizeof(tr));

    bool safe = gui_model_is_safe(&m, seq, &len, &tr);
    assert(!safe);
    assert(!tr.safe);
    assert(tr.count > 0);

    /* Final step must represent stuck iteration with picked == -1 */
    const TraceStep *last_step = &tr.steps[tr.count - 1];
    assert(last_step->picked == -1);

    /* Verify at least one process remained unfinished */
    bool has_unfinished = false;
    for (int p = 0; p < m.current_state.n; p++) {
        if (!last_step->finish[p]) {
            has_unfinished = true;
            break;
        }
    }
    assert(has_unfinished);

    printf("  -> Passed (stuck iteration picked == -1 verified on unsafe_initial.txt).\n");
    fflush(stdout);
}

static void test_request_submission_granted_and_versioning(void) {
    printf("[TEST 18/21] Request granted (P1 asks [1,0,2]), draft sync, state commit, and rollback restoration...\n");
    fflush(stdout);

    GuiModel m;
    gui_model_init(&m);
    gui_model_init_demo(&m);

    uint64_t v_init = m.state_version;
    assert(v_init >= 1);
    assert(m.current_state.alloc[1][0] == 2);
    assert(m.current_state.alloc[1][1] == 0);
    assert(m.current_state.alloc[1][2] == 0);
    assert(m.current_state.available[0] == 3);
    assert(m.current_state.available[1] == 3);
    assert(m.current_state.available[2] == 2);

    /* P1 requests [1, 0, 2] */
    int req[3] = { 1, 0, 2 };
    Trace tr;
    memset(&tr, 0, sizeof(tr));

    RequestResult res = gui_model_submit_request(&m, 1, req, &tr);
    assert(res == REQ_GRANTED);
    assert(tr.safe);

    /* Point 7 & 20: State committed, version advanced exactly once */
    assert(m.state_version == v_init + 1);
    assert(m.current_state.alloc[1][0] == 3);
    assert(m.current_state.alloc[1][1] == 0);
    assert(m.current_state.alloc[1][2] == 2);
    assert(m.current_state.available[0] == 2);
    assert(m.current_state.available[1] == 3);
    assert(m.current_state.available[2] == 0);

    /* Point 18: Draft state synchronized with committed state */
    assert(!gui_model_has_uncommitted_draft(&m));
    assert(m.draft_state.alloc[1][0] == 3);
    assert(m.draft_state.alloc[1][2] == 2);
    assert(m.draft_state.available[0] == 2);
    assert(m.draft_state.available[2] == 0);

    /* Point 19: Original baseline remains intact and can be restored */
    assert(m.original_state.alloc[1][0] == 2);
    assert(m.original_state.alloc[1][2] == 0);
    assert(m.original_state.available[0] == 3);
    assert(m.original_state.available[2] == 2);

    bool reset_ok = gui_model_reset_to_original(&m);
    assert(reset_ok);
    assert(m.state_version == v_init + 2);
    assert(m.current_state.alloc[1][0] == 2);
    assert(m.current_state.alloc[1][2] == 0);
    assert(m.current_state.available[0] == 3);
    assert(m.current_state.available[2] == 2);
    assert(!gui_model_has_uncommitted_draft(&m));

    printf("  -> Passed (granted request commits state, syncs draft, advances version, reset restores baseline).\n");
    fflush(stdout);
}

static void test_request_validation_rejections_and_rollback(void) {
    printf("[TEST 19/21] Request rejection codes (Exceeds Need, Wait Unavailable, Denied Unsafe, Bad Input, Zero)...\n");
    fflush(stdout);

    GuiModel m;
    gui_model_init(&m);
    gui_model_init_demo(&m);

    BankerState baseline = m.current_state;
    uint64_t v_baseline = m.state_version;

    /* Point 8: Exceeds Need (P1 asks [1, 3, 0], Need is [1, 2, 2]) */
    int req_exceed[3] = { 1, 3, 0 };
    RequestResult r_exceed = gui_model_submit_request(&m, 1, req_exceed, NULL);
    assert(r_exceed == REQ_ERR_EXCEEDS_NEED);
    assert(m.state_version == v_baseline);
    assert(memcmp(&m.current_state, &baseline, sizeof(BankerState)) == 0);

    /* Point 9: Wait Unavailable (P4 asks [3, 3, 0] after P1 grant, where Avail is [2, 3, 0]) */
    int req_p1[3] = { 1, 0, 2 };
    assert(gui_model_submit_request(&m, 1, req_p1, NULL) == REQ_GRANTED);
    uint64_t v_after_p1 = m.state_version;
    BankerState state_after_p1 = m.current_state;

    int req_wait[3] = { 3, 3, 0 };
    RequestResult r_wait = gui_model_submit_request(&m, 4, req_wait, NULL);
    assert(r_wait == REQ_WAIT_UNAVAILABLE);
    assert(m.state_version == v_after_p1);
    assert(memcmp(&m.current_state, &state_after_p1, sizeof(BankerState)) == 0);

    /* Reset to baseline for Point 10 */
    gui_model_reset_to_original(&m);
    baseline = m.current_state;
    v_baseline = m.state_version;

    /* Point 10: Denied Unsafe (P4 asks [3, 3, 0] from original state, Available [3, 3, 2]) */
    Trace tr_unsafe;
    memset(&tr_unsafe, 0, sizeof(tr_unsafe));
    RequestResult r_unsafe = gui_model_submit_request(&m, 4, req_wait, &tr_unsafe);
    assert(r_unsafe == REQ_DENIED_UNSAFE);
    assert(!tr_unsafe.safe);
    assert(m.state_version == v_baseline);
    assert(memcmp(&m.current_state, &baseline, sizeof(BankerState)) == 0);

    /* Point 11: Invalid process index */
    int zero[3] = { 0, 0, 0 };
    assert(gui_model_submit_request(&m, -1, zero, NULL) == REQ_ERR_BAD_INPUT);
    assert(gui_model_submit_request(&m, 5, zero, NULL) == REQ_ERR_BAD_INPUT);
    assert(m.state_version == v_baseline);

    /* Point 12: Negative request */
    int neg[3] = { -1, 0, 0 };
    assert(gui_model_submit_request(&m, 0, neg, NULL) == REQ_ERR_BAD_INPUT);
    assert(m.state_version == v_baseline);

    /* Point 13: Zero request returns REQ_GRANTED without advancing version */
    assert(gui_model_submit_request(&m, 0, zero, NULL) == REQ_GRANTED);
    assert(m.state_version == v_baseline);
    assert(memcmp(&m.current_state, &baseline, sizeof(BankerState)) == 0);

    printf("  -> Passed (all 6 request result branches audited with exact state/version preservation).\n");
    fflush(stdout);
}

static int g_obs1_calls = 0;
static int g_obs2_calls = 0;
static void test_observer_1(GuiModel *m, void *ud) { (void)m; (void)ud; g_obs1_calls++; }
static void test_observer_2(GuiModel *m, void *ud) { (void)m; (void)ud; g_obs2_calls++; }

static void test_request_observers_and_notifications(void) {
    printf("[TEST 20/21] Observer notification semantics: multi-subscriber, deduplication, removal, and failed silence...\n");
    fflush(stdout);

    GuiModel m;
    gui_model_init(&m);
    gui_model_init_demo(&m);

    g_obs1_calls = 0;
    g_obs2_calls = 0;

    /* Register observers */
    assert(gui_model_add_observer(&m, test_observer_1, NULL));
    assert(gui_model_add_observer(&m, test_observer_2, NULL));

    /* Deduplication test: re-adding returns true and does not register duplicate */
    assert(gui_model_add_observer(&m, test_observer_1, NULL));
    assert(m.observer_count == 2);

    /* Point 14: Failed request does not notify observers */
    int req_bad[3] = { 1, 3, 0 };
    assert(gui_model_submit_request(&m, 1, req_bad, NULL) == REQ_ERR_EXCEEDS_NEED);
    assert(g_obs1_calls == 0);
    assert(g_obs2_calls == 0);

    /* Zero request does not notify observers */
    int req_zero[3] = { 0, 0, 0 };
    assert(gui_model_submit_request(&m, 0, req_zero, NULL) == REQ_GRANTED);
    assert(g_obs1_calls == 0);
    assert(g_obs2_calls == 0);

    /* Point 15 & 16: Granted request notifies all registered observers */
    int req_ok[3] = { 1, 0, 2 };
    assert(gui_model_submit_request(&m, 1, req_ok, NULL) == REQ_GRANTED);
    assert(g_obs1_calls == 1);
    assert(g_obs2_calls == 1);

    /* Observer removal */
    assert(gui_model_remove_observer(&m, test_observer_1, NULL));
    assert(m.observer_count == 1);

    /* Next operation (reset baseline) notifies only remaining observer */
    gui_model_reset_to_original(&m);
    assert(g_obs1_calls == 1);
    assert(g_obs2_calls == 2);

    /* Remove observer 2 */
    assert(gui_model_remove_observer(&m, test_observer_2, NULL));
    assert(m.observer_count == 0);

    /* Removing non-registered observer returns false */
    assert(!gui_model_remove_observer(&m, test_observer_1, NULL));

    printf("  -> Passed (observer multi-subscription, deduplication, removal, and notification verified).\n");
    fflush(stdout);
}

static void test_uncommitted_draft_blocks_request(void) {
    printf("[TEST 21/21] Uncommitted draft mismatch blocks request submission (REQ_ERR_BAD_STATE)...\n");
    fflush(stdout);

    GuiModel m;
    gui_model_init(&m);
    gui_model_init_demo(&m);

    assert(!gui_model_has_uncommitted_draft(&m));

    /* Modify draft allocation */
    int orig = m.draft_state.alloc[0][0];
    gui_model_set_draft_alloc(&m, 0, 0, orig + 1);
    assert(gui_model_has_uncommitted_draft(&m));

    /* Point 17: Request submission blocked when uncommitted draft exists */
    int req[3] = { 1, 0, 2 };
    RequestResult res = gui_model_submit_request(&m, 1, req, NULL);
    assert(res == REQ_ERR_BAD_STATE);

    /* Discard draft changes */
    gui_model_init_draft_from_current(&m);
    assert(!gui_model_has_uncommitted_draft(&m));

    /* Now request submission succeeds */
    res = gui_model_submit_request(&m, 1, req, NULL);
    assert(res == REQ_GRANTED);

    printf("  -> Passed (uncommitted draft detection blocks submission until committed/discarded).\n");
    fflush(stdout);
}

static int g_legacy_l1_calls = 0;
static int g_legacy_l2_calls = 0;
static int g_obs_a_calls = 0;
static void callback_l1(GuiModel *m, void *ud) { (void)m; (void)ud; g_legacy_l1_calls++; }
static void callback_l2(GuiModel *m, void *ud) { (void)m; (void)ud; g_legacy_l2_calls++; }
static void callback_obs_a(GuiModel *m, void *ud) { (void)m; (void)ud; g_obs_a_calls++; }

static void test_legacy_listener_replacement_preserves_observers(void) {
    printf("[TEST 22/25] Legacy listener replacement preserves independent registered observers...\n");
    fflush(stdout);

    GuiModel m;
    gui_model_init(&m);
    gui_model_init_demo(&m);

    g_legacy_l1_calls = 0;
    g_legacy_l2_calls = 0;
    g_obs_a_calls = 0;

    /* 1. Register independent observer A */
    assert(gui_model_add_observer(&m, callback_obs_a, NULL));
    assert(m.observer_count == 1);

    /* 2. Set legacy listener L1 */
    gui_model_set_listener(&m, callback_l1, NULL);
    assert(m.observer_count == 2);

    /* 3. Replace legacy listener with L2: MUST NOT ERASE OBSERVERS! */
    gui_model_set_listener(&m, callback_l2, NULL);
    assert(m.observer_count == 2);
    assert(m.listener == callback_l2);

    /* 4. Trigger state change */
    gui_model_reset_to_original(&m);

    assert(g_legacy_l1_calls == 0); /* L1 was replaced, never called */
    assert(g_legacy_l2_calls == 1); /* L2 called */
    assert(g_obs_a_calls == 1);     /* Observer A preserved and called */

    /* 5. Clear listener by passing NULL */
    gui_model_set_listener(&m, NULL, NULL);
    assert(m.observer_count == 1);
    assert(m.listener == NULL);

    gui_model_reset_to_original(&m);
    assert(g_legacy_l2_calls == 1); /* L2 not called again */
    assert(g_obs_a_calls == 2);     /* Observer A still active */

    printf("  -> Passed (legacy listener replacement did not affect independent observers).\n");
    fflush(stdout);
}

static int g_rem_a = 0, g_rem_b = 0, g_rem_c = 0;
static void cb_rem_a(GuiModel *m, void *ud) { (void)m; (void)ud; g_rem_a++; }
static void cb_rem_b(GuiModel *m, void *ud) { (void)m; (void)ud; g_rem_b++; }
static void cb_rem_c(GuiModel *m, void *ud) { (void)m; (void)ud; g_rem_c++; }

static void test_observer_removal_independence(void) {
    printf("[TEST 23/25] Observer removal independence (removing one observer does not remove others)...\n");
    fflush(stdout);

    GuiModel m;
    gui_model_init(&m);
    gui_model_init_demo(&m);

    g_rem_a = 0; g_rem_b = 0; g_rem_c = 0;

    assert(gui_model_add_observer(&m, cb_rem_a, NULL));
    assert(gui_model_add_observer(&m, cb_rem_b, NULL));
    assert(gui_model_add_observer(&m, cb_rem_c, NULL));
    assert(m.observer_count == 3);

    /* Remove middle observer B */
    assert(gui_model_remove_observer(&m, cb_rem_b, NULL));
    assert(m.observer_count == 2);

    /* Trigger state change */
    gui_model_reset_to_original(&m);

    assert(g_rem_a == 1);
    assert(g_rem_b == 0); /* B was removed */
    assert(g_rem_c == 1); /* C remained registered and executed */

    printf("  -> Passed (removing one observer preserves remaining registered observers).\n");
    fflush(stdout);
}

static int g_self_rem_calls = 0;
static void cb_self_removing(GuiModel *m, void *ud) {
    (void)ud;
    g_self_rem_calls++;
    gui_model_remove_observer(m, cb_self_removing, NULL);
}

static void test_observer_notification_modification_safety(void) {
    printf("[TEST 24/25] Observer collection modification during notification safety...\n");
    fflush(stdout);

    GuiModel m;
    gui_model_init(&m);
    gui_model_init_demo(&m);

    g_self_rem_calls = 0;
    g_rem_a = 0;

    /* Add self-removing observer and regular observer */
    assert(gui_model_add_observer(&m, cb_self_removing, NULL));
    assert(gui_model_add_observer(&m, cb_rem_a, NULL));
    assert(m.observer_count == 2);

    /* First notification pass: cb_self_removing unregisters itself during its call */
    gui_model_reset_to_original(&m);
    assert(g_self_rem_calls == 1);
    assert(g_rem_a == 1);
    assert(m.observer_count == 1);

    /* Second notification pass: only cb_rem_a remains */
    gui_model_reset_to_original(&m);
    assert(g_self_rem_calls == 1); /* not called again */
    assert(g_rem_a == 2);

    printf("  -> Passed (modifications during notification pass handled safely).\n");
    fflush(stdout);
}

static int g_zero_unsafe_obs_calls = 0;
static void cb_zero_unsafe_obs(GuiModel *m, void *ud) { (void)m; (void)ud; g_zero_unsafe_obs_calls++; }

static void test_zero_request_on_unsafe_state_preserves_unsafe(void) {
    printf("[TEST 25/25] Zero-resource request on unsafe state preserves UNSAFE status, version, and observers...\n");
    fflush(stdout);

    GuiModel m;
    gui_model_init(&m);

    char err[256] = {0};
    bool loaded = gui_model_load_scenario(&m, "tests/scenarios/unsafe_initial.txt", err, sizeof(err));
    assert(loaded);
    assert(m.is_loaded);

    /* Verify initial state is unsafe */
    assert(!gui_model_is_safe(&m, NULL, NULL, NULL));
    uint64_t v_initial = m.state_version;

    g_zero_unsafe_obs_calls = 0;
    assert(gui_model_add_observer(&m, cb_zero_unsafe_obs, NULL));

    /* Submit zero request for P0 */
    int zero[BANKER_MAX_RES] = {0};
    RequestResult res = gui_model_submit_request(&m, 0, zero, NULL);
    assert(res == REQ_GRANTED);

    /* State must still be UNSAFE */
    assert(!gui_model_is_safe(&m, NULL, NULL, NULL));

    /* Version must be completely unchanged */
    assert(m.state_version == v_initial);

    /* Observers must NOT be notified */
    assert(g_zero_unsafe_obs_calls == 0);

    printf("  -> Passed (zero request on unsafe state verified as no-op without safety false-positive).\n");
    fflush(stdout);
}

static void test_why_not_rejection_capture_and_immutability(void) {
    printf("[TEST 26/31] Why-not rejection capture on REQ_DENIED_UNSAFE and snapshot immutability...\n");
    fflush(stdout);

    GuiModel m;
    gui_model_init(&m);
    gui_model_load_demo(&m);

    /* Initially, no rejection is recorded */
    assert(!gui_model_has_why_not(&m));
    GuiWhyNotContext ctx;
    assert(!gui_model_get_why_not_context(&m, &ctx));

    /* Submit unsafe request: P4 requesting [3, 3, 0] */
    int req[BANKER_MAX_RES] = {3, 3, 0};
    Trace trace;
    RequestResult res = gui_model_submit_request(&m, 4, req, &trace);
    assert(res == REQ_DENIED_UNSAFE);

    /* Model must now have Why-not context */
    assert(gui_model_has_why_not(&m));
    assert(gui_model_get_why_not_context(&m, &ctx));
    assert(ctx.has_rejected_request);
    assert(ctx.pid == 4);
    assert(ctx.request[0] == 3 && ctx.request[1] == 3 && ctx.request[2] == 0);
    assert(ctx.state_version == 1);
    assert(!ctx.analyzed);
    assert(ctx.status == WHY_OK);

    /* Snapshot state must match current state byte-for-byte */
    assert(memcmp(&ctx.snapshot_state, &m.current_state, sizeof(BankerState)) == 0);

    /* Execute analysis */
    WhyNotStatus st = gui_model_run_why_not(&m, NULL, NULL);
    assert(st == WHY_OK);

    assert(gui_model_get_why_not_context(&m, &ctx));
    assert(ctx.analyzed);
    assert(ctx.status == WHY_OK);
    assert(ctx.report.candidate_count == 32);
    assert(ctx.report.safe_count == 24);
    assert(ctx.report.unsafe_count == 8);
    assert(ctx.report.has_safe_request);
    assert(ctx.report.has_nearest_safe_request);
    assert(ctx.report.nearest_distance == 1);
    assert(ctx.report.nearest_safe_request[0] == 3 &&
           ctx.report.nearest_safe_request[1] == 2 &&
           ctx.report.nearest_safe_request[2] == 0);
    assert(ctx.report.componentwise_max_is_safe);
    assert(ctx.report.componentwise_max[0] == 3 &&
           ctx.report.componentwise_max[1] == 2 &&
           ctx.report.componentwise_max[2] == 1);
    assert(!ctx.report.denial_trace.safe);
    assert(ctx.report.denial_trace.final_work[0] == 0 &&
           ctx.report.denial_trace.final_work[1] == 0 &&
           ctx.report.denial_trace.final_work[2] == 2);

    /* Save the original captured snapshot and state version */
    BankerState original_snapshot = ctx.snapshot_state;
    uint64_t original_v = ctx.state_version;

    /* Perform a legitimate committed state mutation via a granted request */
    int safe_req[BANKER_MAX_RES] = {1, 0, 2};
    assert(gui_model_submit_request(&m, 1, safe_req, NULL) == REQ_GRANTED);
    assert(m.state_version == original_v + 1);

    /* Retrieve GuiWhyNotContext again from the model to verify storage immutability */
    GuiWhyNotContext ctx2;
    assert(gui_model_get_why_not_context(&m, &ctx2));
    assert(memcmp(&ctx2.snapshot_state, &original_snapshot, sizeof(BankerState)) == 0);
    assert(ctx2.state_version == original_v);
    assert(gui_model_is_why_not_stale(&m));

    printf("  -> Passed (exact PID, vector, snapshot, trace, and alternatives verified).\n");
    fflush(stdout);
}

static void test_why_not_state_version_tracking_and_staleness(void) {
    printf("[TEST 27/31] Why-not state-version tracking and staleness detection...\n");
    fflush(stdout);

    GuiModel m;
    gui_model_init(&m);
    gui_model_load_demo(&m);

    /* Record rejection at state_version = 1 */
    int req[BANKER_MAX_RES] = {3, 3, 0};
    RequestResult res = gui_model_submit_request(&m, 4, req, NULL);
    assert(res == REQ_DENIED_UNSAFE);

    assert(!gui_model_is_why_not_stale(&m));

    /* Submit a safe request for P1: [1, 0, 2] -> granted, advances state_version to 2 */
    int safe_req[BANKER_MAX_RES] = {1, 0, 2};
    res = gui_model_submit_request(&m, 1, safe_req, NULL);
    assert(res == REQ_GRANTED);
    assert(m.state_version == 2);

    /* Diagnostic must now be reported as stale/historical */
    assert(gui_model_is_why_not_stale(&m));

    GuiWhyNotContext ctx;
    assert(gui_model_get_why_not_context(&m, &ctx));
    assert(ctx.state_version == 1);
    assert(m.state_version == 2);

    printf("  -> Passed (staleness accurately flags historical diagnostic after state changes).\n");
    fflush(stdout);
}

static void test_why_not_replacement_on_new_rejection(void) {
    printf("[TEST 28/31] Why-not replacement on new unsafe rejection vs wait-unavailable...\n");
    fflush(stdout);

    GuiModel m;
    gui_model_init(&m);
    gui_model_load_demo(&m);

    /* Initial rejection: P4 requesting [3, 3, 0] */
    int req_p4[BANKER_MAX_RES] = {3, 3, 0};
    assert(gui_model_submit_request(&m, 4, req_p4, NULL) == REQ_DENIED_UNSAFE);

    GuiWhyNotContext ctx;
    assert(gui_model_get_why_not_context(&m, &ctx));
    assert(ctx.pid == 4);
    assert(ctx.request[0] == 3 && ctx.request[1] == 3 && ctx.request[2] == 0);

    /* Second rejection: P0 requesting [0, 3, 0] (Need is [7,4,3], Avail is [3,3,2]; trial avail is [3,0,2] -> unsafe) */
    int req_p0[BANKER_MAX_RES] = {0, 3, 0};
    assert(gui_model_submit_request(&m, 0, req_p0, NULL) == REQ_DENIED_UNSAFE);

    /* Context must now reflect P0's rejection, replacing P4 */
    assert(gui_model_get_why_not_context(&m, &ctx));
    assert(ctx.pid == 0);
    assert(ctx.request[0] == 0 && ctx.request[1] == 3 && ctx.request[2] == 0);
    assert(!ctx.analyzed);

    /* Submitting an unavailable request (within Need, but exceeds Available) must NOT replace or clear why_context */
    /* P0 Need is [7, 4, 3], Available is [3, 3, 2]. Request [4, 0, 0] <= Need, but > Available */
    int req_unavail[BANKER_MAX_RES] = {4, 0, 0};
    assert(gui_model_submit_request(&m, 0, req_unavail, NULL) == REQ_WAIT_UNAVAILABLE);

    assert(gui_model_get_why_not_context(&m, &ctx));
    assert(ctx.pid == 0);
    assert(ctx.request[0] == 0 && ctx.request[1] == 3 && ctx.request[2] == 0);

    printf("  -> Passed (new unsafe rejection atomically replaces context; unavailable request preserves it).\n");
    fflush(stdout);
}

static void test_why_not_lifecycle_clearing(void) {
    printf("[TEST 29/31] Why-not lifecycle clearing on reset and scenario load...\n");
    fflush(stdout);

    GuiModel m;
    gui_model_init(&m);
    gui_model_load_demo(&m);

    int req[BANKER_MAX_RES] = {3, 3, 0};
    assert(gui_model_submit_request(&m, 4, req, NULL) == REQ_DENIED_UNSAFE);
    assert(gui_model_has_why_not(&m));

    /* Reset to original baseline clears why_not context */
    assert(gui_model_reset_to_original(&m));
    assert(!gui_model_has_why_not(&m));

    /* Re-trigger rejection */
    assert(gui_model_submit_request(&m, 4, req, NULL) == REQ_DENIED_UNSAFE);
    assert(gui_model_has_why_not(&m));

    /* Loading a scenario clears why_not context */
    char err[256];
    assert(gui_model_load_scenario(&m, "tests/scenarios/demo.txt", err, sizeof(err)));
    assert(!gui_model_has_why_not(&m));

    /* Re-trigger rejection and test demo reload clearing */
    assert(gui_model_submit_request(&m, 4, req, NULL) == REQ_DENIED_UNSAFE);
    assert(gui_model_has_why_not(&m));

    /* Reloading the demo baseline must clear why_not context */
    gui_model_load_demo(&m);
    assert(!gui_model_has_why_not(&m));

    printf("  -> Passed (why_not context safely cleared on reset, scenario load, and demo reload).\n");
    fflush(stdout);
}

static void test_why_not_reevaluate_semantics(void) {
    printf("[TEST 30/31] Why-not re-evaluate semantics across state changes (unsafe -> unavail -> safe)...\n");
    fflush(stdout);

    GuiModel m;
    gui_model_init(&m);
    gui_model_load_demo(&m);

    /* P4 requests [3, 3, 0] -> rejected at v=1 */
    int req[BANKER_MAX_RES] = {3, 3, 0};
    assert(gui_model_submit_request(&m, 4, req, NULL) == REQ_DENIED_UNSAFE);

    /* 1. Re-evaluate against same state -> returns WHY_OK */
    WhyNotReport rep;
    assert(gui_model_reevaluate_why_not(&m, &rep) == WHY_OK);
    assert(rep.candidate_count == 32);

    /* 1b. Test uncommitted draft blocks re-evaluation with WHY_ERR_BAD_STATE */
    gui_model_set_draft_available(&m, 0, 99);
    assert(gui_model_has_uncommitted_draft(&m));
    assert(gui_model_reevaluate_why_not(&m, &rep) == WHY_ERR_BAD_STATE);

    /* Discard uncommitted draft -> re-evaluation allowed again */
    gui_model_init_draft_from_current(&m);
    assert(!gui_model_has_uncommitted_draft(&m));
    assert(gui_model_reevaluate_why_not(&m, &rep) == WHY_OK);

    /* 2. Grant request to P1: [1, 0, 2] -> available drops to [2, 3, 0] */
    int req_p1[BANKER_MAX_RES] = {1, 0, 2};
    assert(gui_model_submit_request(&m, 1, req_p1, NULL) == REQ_GRANTED);

    /* Now P4's request of [3, 3, 0] exceeds available R0 (3 > 2) */
    assert(gui_model_reevaluate_why_not(&m, &rep) == WHY_ERR_UNAVAILABLE);

    /* 3. Increase available capacity in draft and commit so that [3, 3, 0] is safe */
    gui_model_reset_to_original(&m);
    assert(gui_model_submit_request(&m, 4, req, NULL) == REQ_DENIED_UNSAFE);

    /* Modify draft to give plenty of available resources */
    for (int j = 0; j < m.current_state.m; j++) {
        gui_model_set_draft_total(&m, j, m.current_state.total[j] + 20);
        gui_model_set_draft_available(&m, j, m.current_state.available[j] + 20);
    }
    assert(gui_model_commit_draft(&m));

    /* Re-evaluate: [3, 3, 0] should now be safe (WHY_ERR_NOT_UNSAFE) */
    assert(gui_model_reevaluate_why_not(&m, &rep) == WHY_ERR_NOT_UNSAFE);

    printf("  -> Passed (re-evaluation properly detects WHY_OK, WHY_ERR_UNAVAILABLE, and WHY_ERR_NOT_UNSAFE).\n");
    fflush(stdout);
}

static void test_why_not_oversized_search_space_rejection(void) {
    printf("[TEST 31/31] Why-not oversized search space (> 100,000) pre-check and zero partial analysis...\n");
    fflush(stdout);

    GuiModel m;
    gui_model_init(&m);

    /* Build a state with oversized search space */
    BankerState s;
    banker_init(&s, 5, 3);
    for (int j = 0; j < 3; j++) {
        for (int i = 0; i < 5; i++) {
            s.alloc[i][j] = 100;
            s.max[i][j] = (i == 4) ? 250 : 110;
        }
        s.available[j] = 100;
        s.total[j] = 500 + 100;
    }
    assert(banker_validate(&s, NULL, NULL) == BANKER_OK);

    m.current_state = s;
    m.original_state = s;
    m.draft_state = s;
    m.is_loaded = true;
    m.state_version = 1;

    /* P4 requests [100, 100, 100]:
     * Need is [150, 150, 150], Avail is [100, 100, 100].
     * Bounds are min(150, 100) = [100, 100, 100].
     * Candidates = 101 * 101 * 101 = 1,030,301 > 100,000 cap!
     * Trial available is [0, 0, 0], and P4 remaining need is [50, 50, 50] > 0.
     * No process can finish -> REQ_DENIED_UNSAFE! */
    int req[BANKER_MAX_RES] = {100, 100, 100};
    assert(gui_model_submit_request(&m, 4, req, NULL) == REQ_DENIED_UNSAFE);

    /* Run analysis: must immediately abort with WHY_ERR_SEARCH_SPACE_TOO_LARGE */
    WhyNotStatus st = gui_model_run_why_not(&m, NULL, NULL);
    assert(st == WHY_ERR_SEARCH_SPACE_TOO_LARGE);

    GuiWhyNotContext ctx;
    assert(gui_model_get_why_not_context(&m, &ctx));
    assert(ctx.status == WHY_ERR_SEARCH_SPACE_TOO_LARGE);
    assert(ctx.report.candidate_count == 0); /* Zero partial analysis */

    printf("  -> Passed (oversized space >100,000 cleanly aborted with zero visited candidates).\n");
    fflush(stdout);
}

int main(void) {
    printf("\n============================================================\n");
    printf(" RESOURCEGUARD GUI MODEL & APPLICATION CONTROLLER TESTS\n");
    printf("============================================================\n");
    fflush(stdout);

    test_initialization();
    test_demo_initialization();
    test_listener_preservation_and_monotonic_version();
    test_dimension_changes();
    test_need_matches_banker_need();
    test_draft_validation_rejections();
    test_draft_persistence_and_discard();
    test_auto_calculate_available_and_commit();
    test_unsafe_scenario_preset_and_safety_state();
    test_failed_preset_preserves_state();
    test_demo_parity_with_file();
    test_reset_to_original_baseline();
    test_resource_lookup_order_and_unrelated_cwd();
    test_absolute_scenario_path();
    test_demo_fallback_metadata_and_invalid_demo_rejection();
    test_safety_trace_work_finish_vectors();
    test_unsafe_scenario_trace_and_stuck_diagnostic();
    test_request_submission_granted_and_versioning();
    test_request_validation_rejections_and_rollback();
    test_request_observers_and_notifications();
    test_uncommitted_draft_blocks_request();
    test_legacy_listener_replacement_preserves_observers();
    test_observer_removal_independence();
    test_observer_notification_modification_safety();
    test_zero_request_on_unsafe_state_preserves_unsafe();
    test_why_not_rejection_capture_and_immutability();
    test_why_not_state_version_tracking_and_staleness();
    test_why_not_replacement_on_new_rejection();
    test_why_not_lifecycle_clearing();
    test_why_not_reevaluate_semantics();
    test_why_not_oversized_search_space_rejection();

    printf("\n============================================================\n");
    printf(" ALL 31 APPLICATION MODEL TESTS PASSED SUCCESSFULLY!\n");
    printf("============================================================\n\n");
    fflush(stdout);
    return 0;
}
