/* ResourceGuard GTK4 Smoke Test — implementation.
 * Performs rigorous headless verification of all 8 application foundation checks,
 * including explicit mapping assertion and CSS parsing error auditing. */

#include "smoke.h"
#include "app.h"
#include "window.h"
#include "pages.h"
#include "style.h"
#include "model.h"
#include "safety_view.h"
#include "request_view.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void count_test_obs(GuiModel *m, void *user_data) {
    (void)m;
    int *c = (int *)user_data;
    if (c) (*c)++;
}

static void process_pending_events(void) {
    while (g_main_context_pending(NULL)) {
        g_main_context_iteration(NULL, FALSE);
    }
}

int gui_smoke_test_main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    printf("\n============================================================\n");
    printf(" RESOURCEGUARD GTK4 SMOKE TEST (HEADLESS VERIFICATION)\n");
    printf("============================================================\n");

    /* CHECK 1: GTK Subsystem & CSS Provider Initialization */
    printf("[CHECK 1/12] GTK Subsystem & CSS Provider Initialization (Zero Parse Errors)...\n");
    if (!gtk_init_check()) {
        fprintf(stderr, "FAIL: gtk_init_check() failed. Cannot connect to display.\n");
        return 1;
    }

    if (!gui_style_init() || !gui_style_is_initialized() || gui_style_has_parsing_error()) {
        fprintf(stderr, "FAIL: GTK CSS provider initialization failed or reported parsing errors.\n");
        return 1;
    }
    printf("  -> OK: GTK initialized and CSS provider loaded with 0 parsing errors.\n");

    /* CHECK 2: GtkApplication Creation and Activation */
    printf("[CHECK 2/12] Creating and registering GtkApplication...\n");
    GtkApplication *app = gtk_application_new(
        "io.github.katharv59.resourceguard.smoke",
        G_APPLICATION_DEFAULT_FLAGS
    );
    if (!app) {
        fprintf(stderr, "FAIL: gtk_application_new() returned NULL.\n");
        return 1;
    }

    GError *err = NULL;
    if (!g_application_register(G_APPLICATION(app), NULL, &err)) {
        fprintf(stderr, "FAIL: g_application_register() failed: %s\n",
                err ? err->message : "unknown");
        if (err) g_error_free(err);
        g_object_unref(app);
        return 1;
    }
    printf("  -> OK: GtkApplication created and registered successfully.\n");

    /* CHECK 3: Main Application Window Creation & Model Binding */
    printf("[CHECK 3/12] Building main GtkApplicationWindow and verifying model binding...\n");
    GtkWidget *win = gui_window_new(app);
    if (!win || !GTK_IS_APPLICATION_WINDOW(win)) {
        fprintf(stderr, "FAIL: gui_window_new() did not produce a valid GtkApplicationWindow.\n");
        g_object_unref(app);
        return 1;
    }

    GuiModel *model = gui_window_get_model(win);
    if (!model || !model->is_loaded) {
        fprintf(stderr, "FAIL: Window does not have an active, loaded GuiModel bound.\n");
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }
    printf("  -> OK: Main window created and live GuiModel successfully bound (dim: %dp, %dr).\n",
           model->current_state.n, model->current_state.m);

    /* CHECK 4: Window Presentation, Visibility & Mapping (gtk_widget_get_mapped) */
    printf("[CHECK 4/12] Presenting window and asserting visibility & mapped state (gtk_widget_get_mapped)...\n");
    gtk_window_present(GTK_WINDOW(win));
    gtk_widget_set_visible(win, TRUE);

    for (int i = 0; i < 15; i++) {
        process_pending_events();
    }

    if (!gtk_widget_get_visible(win)) {
        fprintf(stderr, "FAIL: Window visibility check failed (gtk_widget_get_visible returned FALSE).\n");
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }

    if (!gtk_widget_get_mapped(win)) {
        fprintf(stderr, "FAIL: Window mapping check failed (gtk_widget_get_mapped returned FALSE).\n");
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }
    printf("  -> OK: Window is confirmed visible and mapped to display.\n");

    /* CHECK 5: GtkStack Verification (Exactly 7 Pages) */
    printf("[CHECK 5/12] Inspecting GtkStack page count...\n");
    GtkWidget *stack = gui_window_get_stack(win);
    if (!stack || !GTK_IS_STACK(stack)) {
        fprintf(stderr, "FAIL: gui_window_get_stack() returned NULL or non-GtkStack widget.\n");
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }

    GtkSelectionModel *pages = gtk_stack_get_pages(GTK_STACK(stack));
    if (!pages) {
        fprintf(stderr, "FAIL: gtk_stack_get_pages() returned NULL.\n");
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }

    guint n_pages = g_list_model_get_n_items(G_LIST_MODEL(pages));
    if (n_pages != 7) {
        fprintf(stderr, "FAIL: Expected exactly 7 pages in GtkStack, found %u.\n", n_pages);
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }
    printf("  -> OK: GtkStack contains exactly %u pages.\n", n_pages);

    /* CHECK 6: All 7 Page Names Registered */
    printf("[CHECK 6/12] Verifying registration of all 7 page containers...\n");
    const char *expected_pages[7] = {
        "dashboard",
        "resources",
        "safety",
        "simulator",
        "whynot",
        "concurrency",
        "history"
    };

    for (int i = 0; i < 7; i++) {
        GtkWidget *child = gtk_stack_get_child_by_name(GTK_STACK(stack), expected_pages[i]);
        if (!child) {
            fprintf(stderr, "FAIL: Page container '%s' is not registered in GtkStack.\n",
                    expected_pages[i]);
            gtk_window_destroy(GTK_WINDOW(win));
            g_object_unref(app);
            return 1;
        }
        printf("     [%d/7] Page '%s' found (widget: %s)\n",
               i + 1, expected_pages[i], G_OBJECT_TYPE_NAME(child));
    }
    printf("  -> OK: All 7 page containers are registered.\n");

    /* CHECK 7: Programmatic Page Switching Across All Views */
    printf("[CHECK 7/12] Testing programmatic navigation across all 7 pages...\n");
    for (int i = 0; i < 7; i++) {
        gtk_stack_set_visible_child_name(GTK_STACK(stack), expected_pages[i]);
        process_pending_events();

        const char *current = gtk_stack_get_visible_child_name(GTK_STACK(stack));
        if (!current || strcmp(current, expected_pages[i]) != 0) {
            fprintf(stderr, "FAIL: Page switch to '%s' failed (currently visible: '%s').\n",
                    expected_pages[i], current ? current : "NULL");
            gtk_window_destroy(GTK_WINDOW(win));
            g_object_unref(app);
            return 1;
        }
    }

    /* Switch back to Dashboard for initial view */
    gtk_stack_set_visible_child_name(GTK_STACK(stack), "dashboard");
    process_pending_events();
    printf("  -> OK: Programmatic page switching verified for all 7 views.\n");

    /* CHECK 8: GUI Integration — Draft Persistence Across Page Navigation */
    printf("[CHECK 8/12] Verifying uncommitted draft persistence across page navigation...\n");
    gtk_stack_set_visible_child_name(GTK_STACK(stack), "resources");
    process_pending_events();

    int orig_val = model->current_state.alloc[0][0];
    gui_model_set_draft_alloc(model, 0, 0, orig_val + 2);
    if (model->draft_state.alloc[0][0] != orig_val + 2) {
        fprintf(stderr, "FAIL: Draft allocation was not updated.\n");
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }

    /* Navigate to dashboard */
    gtk_stack_set_visible_child_name(GTK_STACK(stack), "dashboard");
    process_pending_events();

    /* Navigate back to resources (triggers map signal) */
    gtk_stack_set_visible_child_name(GTK_STACK(stack), "resources");
    process_pending_events();

    /* Verify draft is retained (map signal did NOT overwrite draft) */
    if (model->draft_state.alloc[0][0] != orig_val + 2) {
        fprintf(stderr, "FAIL: Draft was discarded upon page navigation back to resources!\n");
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }

    /* Discard draft */
    gui_model_init_draft_from_current(model);
    if (model->draft_state.alloc[0][0] != orig_val) {
        fprintf(stderr, "FAIL: Discard did not restore committed state.\n");
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }
    printf("  -> OK: Draft persistence across navigation and explicit discard verified.\n");

    /* CHECK 9/12: Safety Analysis Interactive Stepper & Stuck Diagnostics */
    printf("[CHECK 9/12] Verifying Safety Analysis stepper navigation and trace synchronization...\n");
    gtk_stack_set_visible_child_name(GTK_STACK(stack), "safety");
    process_pending_events();

    GtkWidget *safety_page = gtk_stack_get_child_by_name(GTK_STACK(stack), "safety");
    if (!safety_page) {
        fprintf(stderr, "FAIL: Safety page child not found in stack.\n");
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }
    SafetyWidgets *sw = gui_safety_view_get_widgets(safety_page);
    if (!sw) {
        fprintf(stderr, "FAIL: SafetyWidgets handle not retrieved from safety page.\n");
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }

    if (!sw->is_safe) {
        fprintf(stderr, "FAIL: Safety view did not evaluate demo baseline as safe.\n");
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }

    /* Verify initial step */
    if (sw->current_step != 0) {
        fprintf(stderr, "FAIL: Safety stepper did not start at step 0.\n");
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }
    const char *step_text = gtk_label_get_text(GTK_LABEL(sw->label_step_indicator));
    if (!strstr(step_text, "Step 1 of 5")) {
        fprintf(stderr, "FAIL: Step indicator does not show 'Step 1 of 5' (got '%s').\n", step_text);
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }

    /* Step forward: Next */
    g_signal_emit_by_name(sw->btn_next, "clicked");
    process_pending_events();
    if (sw->current_step != 1) {
        fprintf(stderr, "FAIL: Next step did not advance to step 1.\n");
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }

    /* Step backward: Previous */
    g_signal_emit_by_name(sw->btn_prev, "clicked");
    process_pending_events();
    if (sw->current_step != 0) {
        fprintf(stderr, "FAIL: Previous step did not return to step 0.\n");
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }

    /* Step forward twice and restart */
    g_signal_emit_by_name(sw->btn_next, "clicked");
    g_signal_emit_by_name(sw->btn_next, "clicked");
    process_pending_events();
    if (sw->current_step != 2) {
        fprintf(stderr, "FAIL: Stepper did not reach step 2.\n");
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }

    g_signal_emit_by_name(sw->btn_restart, "clicked");
    process_pending_events();
    if (sw->current_step != 0) {
        fprintf(stderr, "FAIL: Restart did not reset stepper to step 0.\n");
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }

    /* Load unsafe preset and verify stuck diagnostic UI */
    char err_buf[256] = {0};
    if (!gui_model_load_scenario(model, "tests/scenarios/unsafe_initial.txt", err_buf, sizeof(err_buf))) {
        fprintf(stderr, "FAIL: Failed to load unsafe_initial.txt during safety view test: %s\n", err_buf);
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }
    process_pending_events();

    if (sw->is_safe) {
        fprintf(stderr, "FAIL: Safety view failed to detect unsafe scenario.\n");
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }

    /* Advance to the last step of the unsafe trace to view stuck diagnostic */
    while (sw->current_step < sw->trace.count - 1) {
        g_signal_emit_by_name(sw->btn_next, "clicked");
        process_pending_events();
    }

    if (!gtk_widget_get_visible(sw->box_stuck_diagnostic)) {
        fprintf(stderr, "FAIL: Stuck diagnostic box is not visible on final unsafe trace step.\n");
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }

    /* Restore demo baseline for subsequent checks */
    gui_model_load_demo(model);
    process_pending_events();
    printf("  -> OK: Safety stepper navigation, step bounds, and stuck diagnostics verified.\n");

    /* CHECK 10/12: Request Simulator Interactive Transactions & Diagnostics */
    printf("[CHECK 10/12] Verifying Request Simulator transactions, context, and diagnostics...\n");
    gtk_stack_set_visible_child_name(GTK_STACK(stack), "simulator");
    process_pending_events();

    GtkWidget *sim_page = gtk_stack_get_child_by_name(GTK_STACK(stack), "simulator");
    if (!sim_page) {
        fprintf(stderr, "FAIL: Simulator page child not found in stack.\n");
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }
    SimulatorWidgets *rw = gui_request_view_get_widgets(sim_page);
    if (!rw) {
        fprintf(stderr, "FAIL: SimulatorWidgets handle not retrieved from simulator page.\n");
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }

    /* 1. Process selection */
    gtk_drop_down_set_selected(GTK_DROP_DOWN(rw->dropdown_proc), 1);
    process_pending_events();

    const char *ctx_alloc = gtk_label_get_text(GTK_LABEL(rw->label_proc_alloc));
    if (!strstr(ctx_alloc, "P1")) {
        fprintf(stderr, "FAIL: Context allocation label does not reflect Process P1 (got '%s').\n", ctx_alloc);
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }

    /* 2. Zero-resource request: accepted as no-op without state change */
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(rw->spin_req[0]), 0);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(rw->spin_req[1]), 0);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(rw->spin_req[2]), 0);

    uint64_t v_before_zero = model->state_version;
    g_signal_emit_by_name(rw->btn_submit, "clicked");
    process_pending_events();

    if (!gtk_widget_get_visible(rw->card_result)) {
        fprintf(stderr, "FAIL: Result card not visible after zero-resource request submission.\n");
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }
    const char *badge_zero = gtk_label_get_text(GTK_LABEL(rw->badge_result_code));
    if (strcmp(badge_zero, "REQ_GRANTED (NO-OP)") != 0) {
        fprintf(stderr, "FAIL: Expected 'REQ_GRANTED (NO-OP)' badge for zero request, got '%s'.\n", badge_zero);
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }
    const char *desc_zero = gtk_label_get_text(GTK_LABEL(rw->label_result_desc));
    if (!strstr(desc_zero, "accepted as a no-op") || !strstr(desc_zero, "Zero resources were allocated")) {
        fprintf(stderr, "FAIL: Zero-resource request description did not state no-op (got '%s').\n", desc_zero);
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }
    if (model->state_version != v_before_zero) {
        fprintf(stderr, "FAIL: Zero-resource request advanced state version unexpectedly!\n");
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }

    /* 3. Normal granted request: P1 asks [1, 0, 2] */
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(rw->spin_req[0]), 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(rw->spin_req[1]), 0);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(rw->spin_req[2]), 2);

    g_signal_emit_by_name(rw->btn_submit, "clicked");
    process_pending_events();

    const char *badge_grant = gtk_label_get_text(GTK_LABEL(rw->badge_result_code));
    if (strcmp(badge_grant, "REQ_GRANTED") != 0) {
        fprintf(stderr, "FAIL: Expected 'REQ_GRANTED' badge for valid request, got '%s'.\n", badge_grant);
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }
    if (model->state_version != v_before_zero + 1) {
        fprintf(stderr, "FAIL: Granted request did not advance state version exactly once.\n");
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }
    if (model->current_state.alloc[1][0] != 3 || model->current_state.available[0] != 2) {
        fprintf(stderr, "FAIL: Model allocation or available vector not updated on grant.\n");
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }

    /* 4. Verify Dashboard refreshed after granted request with actual label assertions */
    gtk_stack_set_visible_child_name(GTK_STACK(stack), "dashboard");
    process_pending_events();

    GtkWidget *dash_page = gtk_stack_get_child_by_name(GTK_STACK(stack), "dashboard");
    if (!dash_page) {
        fprintf(stderr, "FAIL: Dashboard page child missing from stack.\n");
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }
    DashboardWidgets *dw = gui_dashboard_view_get_widgets(dash_page);
    if (!dw) {
        fprintf(stderr, "FAIL: DashboardWidgets handle not retrieved from dashboard page.\n");
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }

    const char *ver_text = gtk_label_get_text(GTK_LABEL(dw->badge_version));
    if (!strstr(ver_text, "v2")) {
        fprintf(stderr, "FAIL: Dashboard version badge did not display updated version v2 (got '%s').\n", ver_text);
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }

    const char *avail_text = gtk_label_get_text(GTK_LABEL(dw->label_avail_vec));
    if (!strstr(avail_text, "R0: 2") || !strstr(avail_text, "R1: 3") || !strstr(avail_text, "R2: 0")) {
        fprintf(stderr, "FAIL: Dashboard available label did not display updated vector [2, 3, 0] (got '%s').\n", avail_text);
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }

    /* Return to simulator */
    gtk_stack_set_visible_child_name(GTK_STACK(stack), "simulator");
    process_pending_events();

    /* 5. Exceeds Need: P1 asks [1, 3, 0] */
    gtk_drop_down_set_selected(GTK_DROP_DOWN(rw->dropdown_proc), 1);
    process_pending_events();
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(rw->spin_req[0]), 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(rw->spin_req[1]), 3);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(rw->spin_req[2]), 0);

    uint64_t v_before_exceed = model->state_version;
    g_signal_emit_by_name(rw->btn_submit, "clicked");
    process_pending_events();

    const char *badge_exceed = gtk_label_get_text(GTK_LABEL(rw->badge_result_code));
    if (strcmp(badge_exceed, "REQ_ERR_EXCEEDS_NEED") != 0) {
        fprintf(stderr, "FAIL: Expected 'REQ_ERR_EXCEEDS_NEED' badge, got '%s'.\n", badge_exceed);
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }
    if (model->state_version != v_before_exceed) {
        fprintf(stderr, "FAIL: Rejected request modified state version.\n");
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }

    /* 6. Wait Unavailable: P4 asks [3, 3, 0] (available is [2, 3, 0]) */
    gtk_drop_down_set_selected(GTK_DROP_DOWN(rw->dropdown_proc), 4);
    process_pending_events();
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(rw->spin_req[0]), 3);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(rw->spin_req[1]), 3);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(rw->spin_req[2]), 0);

    g_signal_emit_by_name(rw->btn_submit, "clicked");
    process_pending_events();

    const char *badge_wait = gtk_label_get_text(GTK_LABEL(rw->badge_result_code));
    if (strcmp(badge_wait, "REQ_WAIT_UNAVAILABLE") != 0) {
        fprintf(stderr, "FAIL: Expected 'REQ_WAIT_UNAVAILABLE' badge, got '%s'.\n", badge_wait);
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }
    const char *desc_wait = gtk_label_get_text(GTK_LABEL(rw->label_result_desc));
    if (!strstr(desc_wait, "Process P4 must wait until sufficient resources are released")) {
        fprintf(stderr, "FAIL: Wait unavailable diagnostic does not identify Process P4 (got '%s').\n", desc_wait);
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }
    if (!strstr(desc_wait, "Banker's safety algorithm was not executed")) {
        fprintf(stderr, "FAIL: Wait unavailable diagnostic missing safety check omission note (got '%s').\n", desc_wait);
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }

    /* 7. Denied Unsafe with atomic GUI rollback verification: reset to baseline, P4 asks [3, 3, 0] */
    gui_model_load_demo(model);
    process_pending_events();
    gtk_drop_down_set_selected(GTK_DROP_DOWN(rw->dropdown_proc), 4);
    process_pending_events();

    gtk_spin_button_set_value(GTK_SPIN_BUTTON(rw->spin_req[0]), 3);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(rw->spin_req[1]), 3);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(rw->spin_req[2]), 0);

    BankerState state_before_unsafe = model->current_state;
    BankerState orig_before_unsafe = model->original_state;
    uint64_t v_before_unsafe = model->state_version;

    g_signal_emit_by_name(rw->btn_submit, "clicked");
    process_pending_events();

    const char *badge_unsafe = gtk_label_get_text(GTK_LABEL(rw->badge_result_code));
    if (strcmp(badge_unsafe, "REQ_DENIED_UNSAFE") != 0) {
        fprintf(stderr, "FAIL: Expected 'REQ_DENIED_UNSAFE' badge, got '%s'.\n", badge_unsafe);
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }
    if (memcmp(&model->current_state, &state_before_unsafe, sizeof(BankerState)) != 0) {
        fprintf(stderr, "FAIL: Committed BankerState was modified despite REQ_DENIED_UNSAFE rollback requirement!\n");
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }
    if (model->state_version != v_before_unsafe) {
        fprintf(stderr, "FAIL: state_version advanced unexpectedly on REQ_DENIED_UNSAFE (%lu != %lu)!\n",
                (unsigned long)model->state_version, (unsigned long)v_before_unsafe);
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }
    if (memcmp(&model->original_state, &orig_before_unsafe, sizeof(BankerState)) != 0) {
        fprintf(stderr, "FAIL: Original scenario baseline was modified on REQ_DENIED_UNSAFE!\n");
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }

    /* 8. Uncommitted draft blocks request */
    gtk_stack_set_visible_child_name(GTK_STACK(stack), "resources");
    process_pending_events();

    int orig_a00 = model->current_state.alloc[0][0];
    gui_model_set_draft_alloc(model, 0, 0, orig_a00 + 1);

    gtk_stack_set_visible_child_name(GTK_STACK(stack), "simulator");
    process_pending_events();

    if (!gtk_widget_get_visible(rw->banner_draft_warning)) {
        fprintf(stderr, "FAIL: Draft warning banner not visible when uncommitted draft exists.\n");
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }

    g_signal_emit_by_name(rw->btn_submit, "clicked");
    process_pending_events();

    const char *badge_bad_state = gtk_label_get_text(GTK_LABEL(rw->badge_result_code));
    if (strcmp(badge_bad_state, "REQ_ERR_BAD_STATE") != 0) {
        fprintf(stderr, "FAIL: Expected 'REQ_ERR_BAD_STATE' when submitting with draft mismatch, got '%s'.\n", badge_bad_state);
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }

    /* Discard draft */
    gui_model_init_draft_from_current(model);
    process_pending_events();

    /* 9. Dynamic dimensions: change resource dimension through System Resources */
    gtk_stack_set_visible_child_name(GTK_STACK(stack), "resources");
    process_pending_events();

    GtkWidget *res_page = gtk_stack_get_child_by_name(GTK_STACK(stack), "resources");
    if (!res_page) {
        fprintf(stderr, "FAIL: Resources page child missing from stack.\n");
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }
    ResourcesWidgets *res_w = gui_resources_view_get_widgets(res_page);
    if (!res_w) {
        fprintf(stderr, "FAIL: ResourcesWidgets handle not retrieved from resources page.\n");
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }

    /* Change m from 3 to 2 */
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(res_w->spin_m), 2);
    process_pending_events();
    if (model->draft_state.m != 2) {
        fprintf(stderr, "FAIL: Draft resource dimension was not updated to 2.\n");
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }

    /* Auto-calculate Available and commit draft */
    gui_model_auto_calculate_available(model);
    g_signal_emit_by_name(res_w->btn_commit, "clicked");
    process_pending_events();

    if (model->current_state.m != 2) {
        fprintf(stderr, "FAIL: Committed resource dimension was not updated to 2.\n");
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }

    /* Navigate to simulator and verify controls reflect m = 2 */
    gtk_stack_set_visible_child_name(GTK_STACK(stack), "simulator");
    process_pending_events();

    /* Verify simulator resource input count matches committed dimension (2) */
    int sim_input_rows = 0;
    for (GtkWidget *ch = gtk_widget_get_first_child(rw->box_resource_inputs); ch != NULL; ch = gtk_widget_get_next_sibling(ch)) {
        sim_input_rows++;
    }
    if (sim_input_rows != 2) {
        fprintf(stderr, "FAIL: Expected 2 simulator input rows for m=2, got %d.\n", sim_input_rows);
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }

    /* Verify old resource controls are removed */
    if (rw->spin_req[2] != NULL || rw->lbl_res_context[2] != NULL) {
        fprintf(stderr, "FAIL: Stale resource controls (R2) not cleared on dimension reduction.\n");
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }

    /* Verify new controls are initialized correctly */
    if (!rw->spin_req[0] || !rw->spin_req[1]) {
        fprintf(stderr, "FAIL: Dynamic spin buttons R0/R1 not initialized correctly.\n");
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }
    if (gtk_spin_button_get_value(GTK_SPIN_BUTTON(rw->spin_req[0])) != 0.0 ||
        gtk_spin_button_get_value(GTK_SPIN_BUTTON(rw->spin_req[1])) != 0.0) {
        fprintf(stderr, "FAIL: Spin buttons not initialized to 0.0.\n");
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }

    /* Verify Need and Available context is refreshed */
    const char *ctx_r0 = gtk_label_get_text(GTK_LABEL(rw->lbl_res_context[0]));
    const char *ctx_r1 = gtk_label_get_text(GTK_LABEL(rw->lbl_res_context[1]));
    if (!strstr(ctx_r0, "Need:") || !strstr(ctx_r0, "Avail:") ||
        !strstr(ctx_r1, "Need:") || !strstr(ctx_r1, "Avail:")) {
        fprintf(stderr, "FAIL: Context labels for R0/R1 not refreshed with Need/Avail (R0: '%s', R1: '%s').\n",
                ctx_r0, ctx_r1);
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }

    /* Restore the original demo configuration after the test */
    gui_model_load_demo(model);
    process_pending_events();
    if (model->current_state.m != 3) {
        fprintf(stderr, "FAIL: Failed to restore demo baseline dimension (m=%d != 3).\n", model->current_state.m);
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }

    /* 10. Unsafe zero-request GUI regression */
    char err_load[256];
    if (!gui_model_load_scenario(model, "tests/scenarios/unsafe_initial.txt", err_load, sizeof(err_load))) {
        fprintf(stderr, "FAIL: Failed to load tests/scenarios/unsafe_initial.txt: %s\n", err_load);
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }
    process_pending_events();

    if (gui_model_is_safe(model, NULL, NULL, NULL)) {
        fprintf(stderr, "FAIL: Loaded unsafe_initial.txt was expected to evaluate as unsafe.\n");
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }

    int test_obs_calls = 0;
    gui_model_add_observer(model, count_test_obs, &test_obs_calls);

    uint64_t v_unsafe_before = model->state_version;
    int obs_before = test_obs_calls;

    gtk_drop_down_set_selected(GTK_DROP_DOWN(rw->dropdown_proc), 0);
    process_pending_events();
    for (int j = 0; j < model->current_state.m; j++) {
        if (rw->spin_req[j]) {
            gtk_spin_button_set_value(GTK_SPIN_BUTTON(rw->spin_req[j]), 0.0);
        }
    }

    g_signal_emit_by_name(rw->btn_submit, "clicked");
    process_pending_events();

    const char *badge_zero_unsafe = gtk_label_get_text(GTK_LABEL(rw->badge_result_code));
    if (strcmp(badge_zero_unsafe, "REQ_GRANTED (NO-OP)") != 0) {
        fprintf(stderr, "FAIL: Expected 'REQ_GRANTED (NO-OP)' badge for zero-request on unsafe state, got '%s'.\n", badge_zero_unsafe);
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }

    const char *desc_zero_unsafe = gtk_label_get_text(GTK_LABEL(rw->label_result_desc));
    if (!strstr(desc_zero_unsafe, "accepted as a no-op") ||
        (!strstr(desc_zero_unsafe, "Banker's safety evaluation was not required") &&
         !strstr(desc_zero_unsafe, "no safety evaluation"))) {
        fprintf(stderr, "FAIL: Description did not state no safety evaluation was performed (got '%s').\n", desc_zero_unsafe);
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }

    const char *title_zero_unsafe = gtk_label_get_text(GTK_LABEL(rw->label_result_title));
    if (strstr(title_zero_unsafe, "SAFE STATE") || strstr(desc_zero_unsafe, "system is safe")) {
        fprintf(stderr, "FAIL: GUI erroneously claimed system is safe after zero request on unsafe state.\n");
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }

    if (gui_model_is_safe(model, NULL, NULL, NULL)) {
        fprintf(stderr, "FAIL: Model falsely evaluated as safe after zero-request on unsafe baseline!\n");
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }

    if (model->state_version != v_unsafe_before) {
        fprintf(stderr, "FAIL: state_version advanced unexpectedly on zero request (%lu != %lu)!\n",
                (unsigned long)model->state_version, (unsigned long)v_unsafe_before);
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }

    if (test_obs_calls != obs_before) {
        fprintf(stderr, "FAIL: Observer notification fired on zero request (%d != %d)!\n",
                test_obs_calls, obs_before);
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }

    gui_model_remove_observer(model, count_test_obs, &test_obs_calls);

    /* Restore demo baseline */
    gui_model_load_demo(model);
    process_pending_events();

    printf("  -> OK: Request Simulator process selection, spin controls, no-op, grant, wait, and rollback verified.\n");

    /* CHECK 11/12: Cross-Page Synchronization & Navigation State Integrity */
    printf("[CHECK 11/12] Testing cross-page synchronization after state change...\n");
    /* Commit a request in simulator */
    gtk_drop_down_set_selected(GTK_DROP_DOWN(rw->dropdown_proc), 1);
    process_pending_events();
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(rw->spin_req[0]), 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(rw->spin_req[1]), 0);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(rw->spin_req[2]), 2);
    g_signal_emit_by_name(rw->btn_submit, "clicked");
    process_pending_events();

    /* Switch to Safety Analysis and verify trace reflects the new state */
    gtk_stack_set_visible_child_name(GTK_STACK(stack), "safety");
    process_pending_events();

    if (sw->analyzed_version != model->state_version) {
        fprintf(stderr, "FAIL: Safety view trace version (%lu) does not match updated model version (%lu).\n",
                (unsigned long)sw->analyzed_version, (unsigned long)model->state_version);
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }

    /* Navigate across all views */
    for (int i = 0; i < 7; i++) {
        gtk_stack_set_visible_child_name(GTK_STACK(stack), expected_pages[i]);
        process_pending_events();
    }
    printf("  -> OK: Cross-page synchronization and trace updates verified.\n");

    /* CHECK 12/12: Page Removal, Dynamic Recreation & Observer Disposal */
    printf("[CHECK 12/12] Testing dynamic page removal, recreation, and observer lifecycle...\n");

    /* Ensure demo baseline is active */
    gui_model_load_demo(model);
    process_pending_events();

    if (model->observer_count != 4) {
        fprintf(stderr, "FAIL: Expected 4 active observers, found %d.\n", model->observer_count);
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }

    /* 1. Remove Safety Analysis and Request Simulator pages from stack */
    GtkWidget *cur_safety = gtk_stack_get_child_by_name(GTK_STACK(stack), "safety");
    GtkWidget *cur_sim = gtk_stack_get_child_by_name(GTK_STACK(stack), "simulator");
    if (!cur_safety || !cur_sim) {
        fprintf(stderr, "FAIL: Safety or Simulator page missing from stack.\n");
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }

    gtk_stack_remove(GTK_STACK(stack), cur_safety);
    gtk_stack_remove(GTK_STACK(stack), cur_sim);
    process_pending_events();

    /* Assert that their observers are unregistered (count drops from 4 to 2) */
    if (model->observer_count != 2) {
        fprintf(stderr, "FAIL: Expected 2 active observers after removing safety & simulator, found %d.\n",
                model->observer_count);
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }

    /* 2. Recreate the pages, verify observer registrations restored exactly once */
    GtkWidget *new_safety = gui_page_safety_create(model, win);
    GtkWidget *new_sim = gui_page_simulator_create(model, win);
    gtk_stack_add_titled(GTK_STACK(stack), new_safety, "safety", "Safety Analysis");
    gtk_stack_add_titled(GTK_STACK(stack), new_sim, "simulator", "Request Simulator");
    process_pending_events();

    if (model->observer_count != 4) {
        fprintf(stderr, "FAIL: Expected 4 active observers after page recreation, found %d.\n",
                model->observer_count);
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }

    /* 3. Keep GuiModel alive independently while window/pages are destroyed */
    g_object_steal_data(G_OBJECT(win), "gui-model");

    gtk_window_destroy(GTK_WINDOW(win));
    for (int i = 0; i < 20; i++) {
        process_pending_events();
    }

    /* Verify all observers have been disposed and model has no stale callbacks */
    if (model->observer_count != 0) {
        fprintf(stderr, "FAIL: Expected 0 active observers after window teardown, found %d.\n",
                model->observer_count);
        g_free(model);
        g_object_unref(app);
        return 1;
    }

    /* Trigger state change on the independently alive model to verify zero stale callbacks */
    gui_model_reset_to_original(model);

    /* Free independently managed model */
    g_free(model);
    g_object_unref(app);

    printf("  -> OK: Dynamic page removal, recreation, and complete observer cleanup verified.\n");

    printf("\n============================================================\n");
    printf(" ALL 12 GTK4 INTEGRATION & SMOKE TEST ASSERTIONS PASSED SUCCESSFULLY!\n");
    printf("============================================================\n\n");
    return 0;
}
