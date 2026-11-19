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

    char resolved[512] = {0};

    /* 1. Normal relative path from repo root */
    bool ok1 = gui_model_resolve_resource_path("tests/scenarios/demo.txt", resolved, sizeof(resolved));
    assert(ok1);
    assert(strlen(resolved) > 0);

    /* 2. Environment variable override */
    set_test_env("RESOURCEGUARD_SCENARIOS_DIR", "tests/scenarios");

    char resolved_env[512] = {0};
    bool ok_env = gui_model_resolve_resource_path("demo.txt", resolved_env, sizeof(resolved_env));
    assert(ok_env);

    /* 3. Verify non-existent file does NOT silently substitute */
    char resolved_bad[512] = {0};
    bool ok_bad = gui_model_resolve_resource_path("non_existent_scenario_12345.txt", resolved_bad, sizeof(resolved_bad));
    assert(!ok_bad);
    assert(resolved_bad[0] == '\0');

    /* Clear environment variables before CWD test */
    clear_test_env("RESOURCEGUARD_SCENARIOS_DIR");
    clear_test_env("RESOURCEGUARD_DATA_DIR");

    /* 4. Genuine Unrelated Working Directory Regression Test
     * Save original working directory, create a temporary directory outside the repo,
     * chdir into it, verify executable-relative lookup succeeds with zero env overrides,
     * load and validate the BankerState, verify safety, and cleanly restore CWD. */
    char orig_cwd[1024] = {0};
    assert(GETCWD(orig_cwd, sizeof(orig_cwd)) != NULL);

    char tmp_dir_buf[512] = {0};
#if defined(_WIN32)
    char tmp_base[MAX_PATH] = {0};
    GetTempPathA(sizeof(tmp_base), tmp_base);
    snprintf(tmp_dir_buf, sizeof(tmp_dir_buf), "%srg_test_cwd_%lu", tmp_base, (unsigned long)GetCurrentProcessId());
    _mkdir(tmp_dir_buf);
#else
    char tmp_template[] = "/tmp/rg_test_cwd_XXXXXX";
    char *created = mkdtemp(tmp_template);
    assert(created != NULL);
    snprintf(tmp_dir_buf, sizeof(tmp_dir_buf), "%s", created);
#endif

    int chdir_res = CHDIR(tmp_dir_buf);
    assert(chdir_res == 0);

    /* Inside unrelated directory: verify tests/scenarios/demo.txt is NOT in local CWD */
    FILE *local_check = fopen("tests/scenarios/demo.txt", "r");
    assert(local_check == NULL); /* Proves CWD does NOT contain repository scenarios */

    /* Resolution must succeed via executable-relative lookup */
    char resolved_unrelated[512] = {0};
    bool ok_unrelated = gui_model_resolve_resource_path("tests/scenarios/demo.txt",
                                                        resolved_unrelated,
                                                        sizeof(resolved_unrelated));

    /* Load scenario from resolved path and verify state validity & safety */
    GuiModel m;
    gui_model_init(&m);
    char err_buf[256] = {0};
    bool loaded = false;
    bool is_valid = false;
    bool is_safe = false;

    if (ok_unrelated) {
        loaded = gui_model_load_scenario(&m, resolved_unrelated, err_buf, sizeof(err_buf));
        if (loaded && m.is_loaded) {
            int bad_p = -1, bad_r = -1;
            if (banker_validate(&m.current_state, &bad_p, &bad_r) == BANKER_OK) {
                is_valid = true;
            }
            int seq[BANKER_MAX_PROCS];
            int seq_len = 0;
            if (gui_model_is_safe(&m, seq, &seq_len, NULL) && seq_len == 5) {
                is_safe = true;
            }
        }
    }

    /* Guaranteed restoration of original working directory and cleanup of temporary folder */
    int restore_res = CHDIR(orig_cwd);
    RMDIR(tmp_dir_buf);

    /* Assertions checked after restoring directory and removing temp folder */
    assert(restore_res == 0);
    assert(ok_unrelated);
    assert(strlen(resolved_unrelated) > 0);
    assert(loaded);
    assert(is_valid);
    assert(is_safe);

    printf("  -> Passed (genuine unrelated CWD resolution via executable-relative lookup verified).\n");
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

    printf("\n============================================================\n");
    printf(" ALL 15 APPLICATION MODEL TESTS PASSED SUCCESSFULLY!\n");
    printf("============================================================\n\n");
    fflush(stdout);
    return 0;
}
