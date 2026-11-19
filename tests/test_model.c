/* ResourceGuard Application Model & Controller Test Suite.
 * Validates state lifecycle, draft validation, dimensional resizing,
 * Need derivation, conservation checks, scenario loading, and dashboard snapshots. */

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
    printf("[TEST] Initial empty model state...\n");
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
}

static void test_demo_initialization(void) {
    printf("[TEST] Demo baseline initialization...\n");
    GuiModel m;
    gui_model_init_demo(&m);

    assert(m.is_loaded);
    assert(m.state_version == 1);
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
}

static void test_dimension_changes(void) {
    printf("[TEST] Dimensional resizing and bounds clamping...\n");
    GuiModel m;
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
}

static void test_need_recalculation(void) {
    printf("[TEST] Automatic Need matrix recalculation (Read-Only)...\n");
    GuiModel m;
    gui_model_init_demo(&m);

    int need[BANKER_MAX_PROCS][BANKER_MAX_RES];
    gui_model_get_draft_need(&m, need);

    /* In demo: P0 Max=[7,5,3], Alloc=[0,1,0] -> Need=[7,4,3] */
    assert(need[0][0] == 7);
    assert(need[0][1] == 4);
    assert(need[0][2] == 3);

    /* In demo: P1 Max=[3,2,2], Alloc=[2,0,0] -> Need=[1,2,2] */
    assert(need[1][0] == 1);
    assert(need[1][1] == 2);
    assert(need[1][2] == 2);

    /* Modify draft Max and Alloc */
    gui_model_set_draft_alloc(&m, 0, 1, 3);
    gui_model_set_draft_max(&m, 0, 1, 5);
    gui_model_get_draft_need(&m, need);

    /* Need should automatically be 5 - 3 = 2 */
    assert(need[0][1] == 2);

    printf("  -> Passed.\n");
}

static void test_draft_validation_rejections(void) {
    printf("[TEST] Validation errors and draft rejection...\n");
    GuiModel m;
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
}

static void test_auto_calculate_available_and_commit(void) {
    printf("[TEST] Auto-calculate Available and commit draft...\n");
    GuiModel m;
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
}

static void test_scenario_loading(void) {
    printf("[TEST] Scenario loading and invalid file rejection...\n");
    GuiModel m;
    gui_model_init(&m);
    g_listener_called = 0;
    gui_model_set_listener(&m, test_listener, NULL);

    char err_buf[256] = {0};

    /* Nonexistent file */
    assert(!gui_model_load_scenario(&m, "nonexistent_scenario_12345.txt", err_buf, sizeof(err_buf)));
    assert(!m.is_loaded);
    assert(strlen(err_buf) > 0);

    /* Valid scenario */
    assert(gui_model_load_scenario(&m, "tests/scenarios/demo.txt", err_buf, sizeof(err_buf)));
    assert(m.is_loaded);
    assert(m.current_state.n == 5);
    assert(m.current_state.m == 3);
    assert(g_listener_called > 0);

    /* Reset to original */
    gui_model_set_draft_alloc(&m, 0, 0, 2);
    gui_model_auto_calculate_draft_available(&m);
    gui_model_commit_draft(&m);
    assert(m.current_state.alloc[0][0] == 2);

    assert(gui_model_reset_to_original(&m));
    assert(m.current_state.alloc[0][0] == 0); /* Restored to demo baseline */

    printf("  -> Passed.\n");
}

int main(void) {
    printf("\n============================================================\n");
    printf(" RESOURCEGUARD GUI MODEL & APPLICATION CONTROLLER TESTS\n");
    printf("============================================================\n");

    test_initialization();
    test_demo_initialization();
    test_dimension_changes();
    test_need_recalculation();
    test_draft_validation_rejections();
    test_auto_calculate_available_and_commit();
    test_scenario_loading();

    printf("\n============================================================\n");
    printf(" ALL APPLICATION MODEL TESTS PASSED SUCCESSFULLY!\n");
    printf("============================================================\n\n");
    return 0;
}
