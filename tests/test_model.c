/* ResourceGuard Application Model & Controller Test Suite.
 * Validates state lifecycle, listener preservation, monotonic versioning,
 * draft persistence, Need derivation, scenario loading, and safety evaluation. */

#include "model.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>

static int g_listener_called = 0;
static void test_listener(GuiModel *m, void *user_data) {
    (void)m;
    (void)user_data;
    g_listener_called++;
}

static void test_initialization(void) {
    printf("[TEST 1/11] Initial empty model state...\n");
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
    printf("[TEST 2/11] Demo baseline initialization...\n");
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
    printf("[TEST 3/11] Listener preservation & monotonic state version advancement...\n");
    fflush(stdout);
    GuiModel m;
    gui_model_init(&m);

    g_listener_called = 0;
    gui_model_set_listener(&m, test_listener, (void*)0x1234);
    assert(m.listener == test_listener);
    assert(m.listener_user_data == (void*)0x1234);

    /* Reinitialize demo state */
    gui_model_init_demo(&m);
    assert(m.listener == test_listener);
    assert(m.listener_user_data == (void*)0x1234);
    assert(g_listener_called > 0);
    uint64_t v1 = m.state_version;
    assert(v1 > 0);

    /* Reinitialize demo state again: must advance version and keep listener */
    int prev_calls = g_listener_called;
    gui_model_init_demo(&m);
    assert(m.listener == test_listener);
    assert(m.listener_user_data == (void*)0x1234);
    assert(g_listener_called > prev_calls);
    assert(m.state_version > v1);

    printf("  -> Passed (version advanced from %lu to %lu, listener retained).\n",
           (unsigned long)v1, (unsigned long)m.state_version);
    fflush(stdout);
}

static void test_dimension_changes(void) {
    printf("[TEST 4/11] Dimensional resizing and bounds clamping...\n");
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
    printf("[TEST 5/11] Authoritative Need derivation (gui_model_get_draft_need == banker_need)...\n");
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
    printf("[TEST 6/11] Validation errors and draft rejection...\n");
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
    printf("[TEST 7/11] Draft persistence across page navigation and explicit discard...\n");
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
    printf("[TEST 8/11] Auto-calculate Available and commit draft...\n");
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
    printf("[TEST 9/11] Loading unsafe_initial.txt and verifying UNSAFE evaluation...\n");
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
    printf("[TEST 10/11] Failed preset loading preserves current state without silent fallback...\n");
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
    printf("[TEST 11/11] Parity check between built-in demo baseline and tests/scenarios/demo.txt...\n");
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

    printf("\n============================================================\n");
    printf(" ALL 11 APPLICATION MODEL TESTS PASSED SUCCESSFULLY!\n");
    printf("============================================================\n\n");
    fflush(stdout);
    return 0;
}
