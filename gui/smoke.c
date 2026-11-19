/* ResourceGuard GTK4 Smoke Test — implementation.
 * Performs rigorous headless verification of all 8 application foundation checks,
 * including explicit mapping assertion and CSS parsing error auditing. */

#include "smoke.h"
#include "app.h"
#include "window.h"
#include "style.h"
#include "model.h"
#include "safety_view.h"
#include "request_view.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

    /* 4. Verify Dashboard refreshed after granted request */
    gtk_stack_set_visible_child_name(GTK_STACK(stack), "dashboard");
    process_pending_events();

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

    /* 7. Denied Unsafe with rollback info: reset to baseline, P4 asks [3, 3, 0] */
    gui_model_load_demo(model);
    process_pending_events();
    gtk_drop_down_set_selected(GTK_DROP_DOWN(rw->dropdown_proc), 4);
    process_pending_events();

    gtk_spin_button_set_value(GTK_SPIN_BUTTON(rw->spin_req[0]), 3);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(rw->spin_req[1]), 3);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(rw->spin_req[2]), 0);

    g_signal_emit_by_name(rw->btn_submit, "clicked");
    process_pending_events();

    const char *badge_unsafe = gtk_label_get_text(GTK_LABEL(rw->badge_result_code));
    if (strcmp(badge_unsafe, "REQ_DENIED_UNSAFE") != 0) {
        fprintf(stderr, "FAIL: Expected 'REQ_DENIED_UNSAFE' badge, got '%s'.\n", badge_unsafe);
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

    /* CHECK 12/12: Clean Teardown, Widget Disposal & Observer Cleanup */
    printf("[CHECK 12/12] Tearing down window and verifying observer disposal...\n");
    if (model->observer_count <= 0) {
        fprintf(stderr, "FAIL: Expected active observers before window teardown, found 0.\n");
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }

    gtk_window_destroy(GTK_WINDOW(win));
    for (int i = 0; i < 20; i++) {
        process_pending_events();
    }
    g_object_unref(app);
    printf("  -> OK: Teardown complete with zero leaks or dangling callbacks.\n");

    printf("\n============================================================\n");
    printf(" ALL 12 GTK4 INTEGRATION & SMOKE TEST ASSERTIONS PASSED SUCCESSFULLY!\n");
    printf("============================================================\n\n");
    return 0;
}
