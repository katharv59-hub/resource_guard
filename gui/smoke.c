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
#include "whynot_view.h"
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

    /* 1. Process selection & verify Explain Rejection button initially hidden */
    if (gtk_widget_get_visible(rw->btn_explain_rejection)) {
        fprintf(stderr, "FAIL: Explain Rejection button unexpectedly visible on initial startup.\n");
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }

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

    char expected_ver[64];
    snprintf(expected_ver, sizeof(expected_ver), "State: v%lu", (unsigned long)model->state_version);
    const char *ver_text = gtk_label_get_text(GTK_LABEL(dw->badge_version));
    if (strcmp(ver_text, expected_ver) != 0) {
        fprintf(stderr, "FAIL: Dashboard version badge did not display updated version '%s' (got '%s').\n", expected_ver, ver_text);
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

    /* 7b. Why-not Intelligence Explain Rejection button interaction & lifecycle verification */
    if (!gtk_widget_get_visible(rw->btn_explain_rejection)) {
        fprintf(stderr, "FAIL: Explain Rejection button not visible after REQ_DENIED_UNSAFE.\n");
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }

    GuiWhyNotContext why_ctx;
    if (!gui_model_get_why_not_context(model, &why_ctx) || why_ctx.pid != 4 ||
        why_ctx.request[0] != 3 || why_ctx.request[1] != 3 || why_ctx.request[2] != 0) {
        fprintf(stderr, "FAIL: Model Why-not context not captured properly for P4 [3, 3, 0].\n");
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }

    /* Capture rejection context, committed BankerState, and state version */
    GuiWhyNotContext ctx_before_zero = why_ctx;
    BankerState state_before_zero = model->current_state;
    uint64_t v_before_zero_sub = model->state_version;

    /* Submit all-zero request through the actual Simulator Submit button */
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(rw->spin_req[0]), 0);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(rw->spin_req[1]), 0);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(rw->spin_req[2]), 0);

    g_signal_emit_by_name(rw->btn_submit, "clicked");
    process_pending_events();

    /* Verify displayed result is REQ_GRANTED (NO-OP) */
    const char *badge_zero_noop = gtk_label_get_text(GTK_LABEL(rw->badge_result_code));
    if (strcmp(badge_zero_noop, "REQ_GRANTED (NO-OP)") != 0) {
        fprintf(stderr, "FAIL: Expected 'REQ_GRANTED (NO-OP)' for zero request, got '%s'.\n", badge_zero_noop);
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }

    /* Verify previous rejection context remains unchanged */
    GuiWhyNotContext ctx_after_zero;
    if (!gui_model_get_why_not_context(model, &ctx_after_zero)) {
        fprintf(stderr, "FAIL: Why-not context lost after zero request.\n");
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }
    if (ctx_after_zero.pid != ctx_before_zero.pid ||
        memcmp(ctx_after_zero.request, ctx_before_zero.request, sizeof(int) * model->current_state.m) != 0 ||
        memcmp(&ctx_after_zero.snapshot_state, &ctx_before_zero.snapshot_state, sizeof(BankerState)) != 0 ||
        ctx_after_zero.state_version != ctx_before_zero.state_version) {
        fprintf(stderr, "FAIL: Previous rejection context modified by zero request.\n");
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }

    /* Verify committed BankerState and current state version did not change */
    if (memcmp(&model->current_state, &state_before_zero, sizeof(BankerState)) != 0 ||
        model->state_version != v_before_zero_sub) {
        fprintf(stderr, "FAIL: Committed state or version modified by zero-request no-op.\n");
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }

    /* Verify Explain Rejection button remains visible */
    if (!gtk_widget_get_visible(rw->btn_explain_rejection)) {
        fprintf(stderr, "FAIL: Explain Rejection button hidden after zero request.\n");
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }

    /* Navigation state immutability check: capture state before clicking */
    BankerState state_before_nav = model->current_state;
    uint64_t v_before_nav = model->state_version;

    /* Click Explain Rejection button and verify navigation to 'whynot' stack page */
    g_signal_emit_by_name(rw->btn_explain_rejection, "clicked");
    process_pending_events();

    const char *cur_page = gtk_stack_get_visible_child_name(GTK_STACK(stack));
    if (!cur_page || strcmp(cur_page, "whynot") != 0) {
        fprintf(stderr, "FAIL: Explain Rejection button did not navigate to 'whynot' page (got '%s').\n",
                cur_page ? cur_page : "<null>");
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }

    /* Assert committed BankerState and state_version byte-for-byte unchanged after navigation */
    if (memcmp(&model->current_state, &state_before_nav, sizeof(BankerState)) != 0) {
        fprintf(stderr, "FAIL: Navigation to 'whynot' modified committed BankerState!\n");
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }
    if (model->state_version != v_before_nav) {
        fprintf(stderr, "FAIL: Navigation to 'whynot' modified state_version!\n");
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }

    /* Assert rejection context remains intact after navigation */
    GuiWhyNotContext ctx_after_nav;
    if (!gui_model_get_why_not_context(model, &ctx_after_nav) ||
        ctx_after_nav.pid != ctx_before_zero.pid ||
        memcmp(ctx_after_nav.request, ctx_before_zero.request, sizeof(int) * model->current_state.m) != 0 ||
        memcmp(&ctx_after_nav.snapshot_state, &ctx_before_zero.snapshot_state, sizeof(BankerState)) != 0 ||
        ctx_after_nav.state_version != ctx_before_zero.state_version) {
        fprintf(stderr, "FAIL: Rejection context altered by navigation!\n");
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }

    /* Verify Why-not Diagnostic View interactive components */
    GtkWidget *whynot_page = gtk_stack_get_child_by_name(GTK_STACK(stack), "whynot");
    if (!whynot_page) {
        fprintf(stderr, "FAIL: Why-not page missing from stack.\n");
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }
    WhynotWidgets *ww = gui_whynot_view_get_widgets(whynot_page);
    if (!ww) {
        fprintf(stderr, "FAIL: WhynotWidgets handle not retrieved from whynot page.\n");
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }

    /* Verify container visibility */
    if (gtk_widget_get_visible(ww->empty_box)) {
        fprintf(stderr, "FAIL: Why-not empty_box visible when rejection context exists.\n");
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }
    if (!gtk_widget_get_visible(ww->content_box)) {
        fprintf(stderr, "FAIL: Why-not content_box hidden when rejection context exists.\n");
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }

    /* Verify Header Badges */
    const char *badge_proc_txt = gtk_label_get_text(GTK_LABEL(ww->badge_proc));
    if (strcmp(badge_proc_txt, "P4") != 0) {
        fprintf(stderr, "FAIL: Expected badge_proc 'P4', got '%s'.\n", badge_proc_txt);
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }
    const char *badge_req_txt = gtk_label_get_text(GTK_LABEL(ww->badge_req));
    if (!strstr(badge_req_txt, "3, 3, 0")) {
        fprintf(stderr, "FAIL: Expected badge_req to contain '3, 3, 0', got '%s'.\n", badge_req_txt);
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }
    char expected_why_ver[64];
    snprintf(expected_why_ver, sizeof(expected_why_ver), "v%lu", (unsigned long)ctx_after_nav.state_version);
    const char *badge_ver_txt = gtk_label_get_text(GTK_LABEL(ww->badge_version));
    if (!strstr(badge_ver_txt, expected_why_ver)) {
        fprintf(stderr, "FAIL: Expected badge_version to contain '%s', got '%s'.\n", expected_why_ver, badge_ver_txt);
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }
    const char *badge_st_txt = gtk_label_get_text(GTK_LABEL(ww->badge_status));
    if (strcmp(badge_st_txt, "COMPLETE") != 0) {
        fprintf(stderr, "FAIL: Expected badge_status 'COMPLETE', got '%s'.\n", badge_st_txt);
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }

    /* Verify Diagnostic Summary Metrics */
    const char *tot_cand = gtk_label_get_text(GTK_LABEL(ww->label_total_candidates));
    if (strcmp(tot_cand, "32") != 0) {
        fprintf(stderr, "FAIL: Expected 32 total candidates, got '%s'.\n", tot_cand);
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }
    const char *safe_cand = gtk_label_get_text(GTK_LABEL(ww->label_safe_candidates));
    if (!strstr(safe_cand, "24")) {
        fprintf(stderr, "FAIL: Expected 24 safe candidates, got '%s'.\n", safe_cand);
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }
    const char *unsafe_cand = gtk_label_get_text(GTK_LABEL(ww->label_unsafe_candidates));
    if (!strstr(unsafe_cand, "8")) {
        fprintf(stderr, "FAIL: Expected 8 unsafe candidates, got '%s'.\n", unsafe_cand);
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }
    const char *near_safe = gtk_label_get_text(GTK_LABEL(ww->label_nearest_safe));
    if (!strstr(near_safe, "3, 2, 0")) {
        fprintf(stderr, "FAIL: Expected nearest safe request '[ 3, 2, 0 ]', got '%s'.\n", near_safe);
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }
    const char *near_dist = gtk_label_get_text(GTK_LABEL(ww->label_nearest_distance));
    if (!strstr(near_dist, "1")) {
        fprintf(stderr, "FAIL: Expected Manhattan distance 1, got '%s'.\n", near_dist);
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }
    const char *comp_max = gtk_label_get_text(GTK_LABEL(ww->label_compwise_max));
    if (!strstr(comp_max, "3, 2, 1")) {
        fprintf(stderr, "FAIL: Expected componentwise max '[ 3, 2, 1 ]', got '%s'.\n", comp_max);
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }
    const char *comp_safe = gtk_label_get_text(GTK_LABEL(ww->badge_compwise_safe));
    if (strcmp(comp_safe, "JOINTLY SAFE") != 0) {
        fprintf(stderr, "FAIL: Expected componentwise max to be 'JOINTLY SAFE', got '%s'.\n", comp_safe);
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }

    /* Verify Denial Safety Trace and Non-deadlock Explanation */
    const char *stall_exp = gtk_label_get_text(GTK_LABEL(ww->label_stall_explanation));
    if ((!strstr(stall_exp, "NOT AN ACTIVE DEADLOCK") && !strstr(stall_exp, "not an active deadlock")) ||
        (!strstr(stall_exp, "UNSAFE STATE") && !strstr(stall_exp, "unsafe state"))) {
        fprintf(stderr, "FAIL: Stall explanation missing non-deadlock guarantee disclaimer.\n");
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }
    /* Strengthened denial trace assertions (Phase 3.1) */
    /* 1. Step count and step content */
    GtkWidget *step0 = gtk_widget_get_first_child(ww->box_trace_steps);
    if (!step0) {
        fprintf(stderr, "FAIL: Expected trace step 0 widget missing.\n");
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }
    GtkWidget *step0_badge = gtk_widget_get_first_child(step0);
    GtkWidget *step0_desc = step0_badge ? gtk_widget_get_next_sibling(step0_badge) : NULL;
    if (!step0_badge || !step0_desc) {
        fprintf(stderr, "FAIL: Trace step 0 layout malformed.\n");
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }
    const char *step0_title = gtk_label_get_text(GTK_LABEL(step0_badge));
    if (strcmp(step0_title, "Step 0") != 0) {
        fprintf(stderr, "FAIL: Expected 'Step 0' badge, got '%s'.\n", step0_title);
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }
    const char *step0_text = gtk_label_get_text(GTK_LABEL(step0_desc));
    if (!strstr(step0_text, "ALGORITHMIC STALL DETECTED at Work [ 0, 0, 2 ]")) {
        fprintf(stderr, "FAIL: Step 0 text did not display stall at Work [ 0, 0, 2 ] (got '%s').\n", step0_text);
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }
    if (gtk_widget_get_next_sibling(step0) != NULL) {
        fprintf(stderr, "FAIL: Expected exactly 1 step in demo denial trace.\n");
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }

    /* 2. Blocked processes count, identities, exact Need and Deficit values */
    const char *expected_proc_headers[5] = {
        "Process P0 (Blocked / Unfinished)",
        "Process P1 (Blocked / Unfinished)",
        "Process P2 (Blocked / Unfinished)",
        "Process P3 (Blocked / Unfinished)",
        "Process P4 (Blocked / Unfinished)"
    };
    const char *expected_proc_needs[5] = {
        "Need: [ 7, 4, 3 ]",
        "Need: [ 1, 2, 2 ]",
        "Need: [ 6, 0, 0 ]",
        "Need: [ 0, 1, 1 ]",
        "Need: [ 1, 0, 1 ]"
    };
    const char *expected_proc_deficits[5] = {
        "Deficit: [ 7, 4, 1 ]",
        "Deficit: [ 1, 2, 0 ]",
        "Deficit: [ 6, 0, 0 ]",
        "Deficit: [ 0, 1, 0 ]",
        "Deficit: [ 1, 0, 0 ]"
    };

    GtkWidget *cur_proc_card = gtk_widget_get_first_child(ww->box_blocked_procs);
    for (int p = 0; p < 5; p++) {
        if (!cur_proc_card) {
            fprintf(stderr, "FAIL: Expected blocked process card %d missing.\n", p);
            gtk_window_destroy(GTK_WINDOW(win));
            g_object_unref(app);
            return 1;
        }
        GtkWidget *lbl_ph = gtk_widget_get_first_child(cur_proc_card);
        GtkWidget *lbl_pd = lbl_ph ? gtk_widget_get_next_sibling(lbl_ph) : NULL;
        if (!lbl_ph || !lbl_pd) {
            fprintf(stderr, "FAIL: Blocked process card %d missing header or detail label.\n", p);
            gtk_window_destroy(GTK_WINDOW(win));
            g_object_unref(app);
            return 1;
        }
        const char *ph_text = gtk_label_get_text(GTK_LABEL(lbl_ph));
        const char *pd_text = gtk_label_get_text(GTK_LABEL(lbl_pd));

        if (strcmp(ph_text, expected_proc_headers[p]) != 0) {
            fprintf(stderr, "FAIL: Blocked process %d header mismatch: expected '%s', got '%s'.\n",
                    p, expected_proc_headers[p], ph_text);
            gtk_window_destroy(GTK_WINDOW(win));
            g_object_unref(app);
            return 1;
        }
        if (!strstr(pd_text, expected_proc_needs[p])) {
            fprintf(stderr, "FAIL: Blocked process %d missing expected %s in '%s'.\n",
                    p, expected_proc_needs[p], pd_text);
            gtk_window_destroy(GTK_WINDOW(win));
            g_object_unref(app);
            return 1;
        }
        if (!strstr(pd_text, expected_proc_deficits[p])) {
            fprintf(stderr, "FAIL: Blocked process %d missing expected %s in '%s'.\n",
                    p, expected_proc_deficits[p], pd_text);
            gtk_window_destroy(GTK_WINDOW(win));
            g_object_unref(app);
            return 1;
        }
        if (!strstr(pd_text, "Final Work: [ 0, 0, 2 ]")) {
            fprintf(stderr, "FAIL: Blocked process %d missing Final Work: [ 0, 0, 2 ] in '%s'.\n",
                    p, pd_text);
            gtk_window_destroy(GTK_WINDOW(win));
            g_object_unref(app);
            return 1;
        }
        cur_proc_card = gtk_widget_get_next_sibling(cur_proc_card);
    }
    if (cur_proc_card != NULL) {
        fprintf(stderr, "FAIL: Found more than 5 blocked processes in demo denial trace.\n");
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }

    /* Verify Actionable Alternative: Load into Simulator */
    g_signal_emit_by_name(ww->btn_load_simulator, "clicked");
    process_pending_events();

    const char *page_after_load = gtk_stack_get_visible_child_name(GTK_STACK(stack));
    if (!page_after_load || strcmp(page_after_load, "simulator") != 0) {
        fprintf(stderr, "FAIL: 'Load into Simulator' did not switch page to 'simulator' (got '%s').\n",
                page_after_load ? page_after_load : "<null>");
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }
    guint sel_proc = gtk_drop_down_get_selected(GTK_DROP_DOWN(rw->dropdown_proc));
    if (sel_proc != 4) {
        fprintf(stderr, "FAIL: 'Load into Simulator' selected process %u instead of 4.\n", sel_proc);
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }
    double s0 = gtk_spin_button_get_value(GTK_SPIN_BUTTON(rw->spin_req[0]));
    double s1 = gtk_spin_button_get_value(GTK_SPIN_BUTTON(rw->spin_req[1]));
    double s2 = gtk_spin_button_get_value(GTK_SPIN_BUTTON(rw->spin_req[2]));
    if ((int)s0 != 3 || (int)s1 != 2 || (int)s2 != 0) {
        fprintf(stderr, "FAIL: Simulator inputs not populated with [3, 2, 0] (got [%.0f, %.0f, %.0f]).\n", s0, s1, s2);
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }
    /* Verify request was NOT automatically submitted */
    if (model->state_version != v_before_nav ||
        memcmp(&model->current_state, &state_before_nav, sizeof(BankerState)) != 0) {
        fprintf(stderr, "FAIL: 'Load into Simulator' erroneously submitted request!\n");
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }

    /* Test oversized-space UI handling (Phase 3.1) */
    BankerState saved_state_before_over = model->current_state;
    uint64_t saved_ver_before_over = model->state_version;

    BankerState s_over;
    banker_init(&s_over, 5, 3);
    for (int j = 0; j < 3; j++) {
        for (int i = 0; i < 5; i++) {
            s_over.alloc[i][j] = 100;
            s_over.max[i][j] = (i == 4) ? 250 : 110;
        }
        s_over.available[j] = 100;
        s_over.total[j] = 500 + 100;
    }
    model->current_state = s_over;
    model->draft_state = s_over;
    model->state_version++;

    int req_over[BANKER_MAX_RES] = {100, 100, 100};
    RequestResult res_over = gui_model_submit_request(model, 4, req_over, NULL);
    if (res_over != REQ_DENIED_UNSAFE) {
        fprintf(stderr, "FAIL: Oversized request was not denied as REQ_DENIED_UNSAFE.\n");
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }

    gtk_stack_set_visible_child_name(GTK_STACK(stack), "whynot");
    process_pending_events();

    /* Assert oversized status badge and banner */
    const char *badge_over_txt = gtk_label_get_text(GTK_LABEL(ww->badge_status));
    if (strcmp(badge_over_txt, "OVERSIZED SPACE") != 0) {
        fprintf(stderr, "FAIL: Expected badge_status 'OVERSIZED SPACE', got '%s'.\n", badge_over_txt);
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }
    if (!gtk_widget_get_visible(ww->banner_oversized)) {
        fprintf(stderr, "FAIL: Oversized banner not visible.\n");
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }
    const char *over_lbl_txt = gtk_label_get_text(GTK_LABEL(ww->label_oversized));
    if (!strstr(over_lbl_txt, "exceeds the exploration limit of 100,000 vectors")) {
        fprintf(stderr, "FAIL: Oversized banner missing 100,000 disclaimer (got '%s').\n", over_lbl_txt);
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }

    /* Assert cards are completely hidden */
    if (gtk_widget_get_visible(ww->card_summary)) {
        fprintf(stderr, "FAIL: Summary card visible during oversized space error.\n");
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }
    if (gtk_widget_get_visible(ww->card_trace)) {
        fprintf(stderr, "FAIL: Trace card visible during oversized space error.\n");
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }
    if (gtk_widget_get_visible(ww->card_alternatives)) {
        fprintf(stderr, "FAIL: Alternatives card visible during oversized space error.\n");
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }
    if (gtk_widget_get_visible(ww->btn_load_simulator)) {
        fprintf(stderr, "FAIL: Load simulator button visible during oversized space error.\n");
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }

    /* Restore demo baseline and re-submit P4 request [3, 3, 0] to continue test suite */
    model->current_state = saved_state_before_over;
    model->draft_state = saved_state_before_over;
    model->state_version = saved_ver_before_over;
    int p4_restore_req[BANKER_MAX_RES] = {3, 3, 0};
    gui_model_submit_request(model, 4, p4_restore_req, NULL);
    gtk_stack_set_visible_child_name(GTK_STACK(stack), "whynot");
    process_pending_events();

    /* Uncommitted draft test: edit draft in Resources and navigate to Why-not */
    gtk_stack_set_visible_child_name(GTK_STACK(stack), "resources");
    process_pending_events();
    gui_model_set_draft_available(model, 0, 99);
    gtk_stack_set_visible_child_name(GTK_STACK(stack), "whynot");
    process_pending_events();

    if (!gtk_widget_get_visible(ww->banner_draft)) {
        fprintf(stderr, "FAIL: Draft warning banner hidden when uncommitted draft exists.\n");
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }
    if (gtk_widget_get_sensitive(ww->btn_reevaluate)) {
        fprintf(stderr, "FAIL: Re-evaluate button sensitive when uncommitted draft exists.\n");
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }
    gui_model_init_draft_from_current(model);
    gtk_stack_set_visible_child_name(GTK_STACK(stack), "resources");
    process_pending_events();
    gtk_stack_set_visible_child_name(GTK_STACK(stack), "whynot");
    process_pending_events();

    if (gtk_widget_get_visible(ww->banner_draft)) {
        fprintf(stderr, "FAIL: Draft warning banner visible after discarding draft.\n");
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }
    if (!gtk_widget_get_sensitive(ww->btn_reevaluate)) {
        fprintf(stderr, "FAIL: Re-evaluate button insensitive after discarding draft.\n");
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }

    /* Return to simulator */
    gtk_stack_set_visible_child_name(GTK_STACK(stack), "simulator");
    process_pending_events();

    /* Submit second unsafe request: P0 asks [0, 3, 0] (Need is [7, 4, 3], Avail is [3, 3, 2]; trial avail [3, 0, 2] -> unsafe) */
    gtk_drop_down_set_selected(GTK_DROP_DOWN(rw->dropdown_proc), 0);
    process_pending_events();
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(rw->spin_req[0]), 0);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(rw->spin_req[1]), 3);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(rw->spin_req[2]), 0);

    g_signal_emit_by_name(rw->btn_submit, "clicked");
    process_pending_events();

    if (strcmp(gtk_label_get_text(GTK_LABEL(rw->badge_result_code)), "REQ_DENIED_UNSAFE") != 0) {
        fprintf(stderr, "FAIL: Expected REQ_DENIED_UNSAFE for P0 request [0, 3, 0].\n");
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }
    if (!gui_model_get_why_not_context(model, &why_ctx) || why_ctx.pid != 0 ||
        why_ctx.request[0] != 0 || why_ctx.request[1] != 3 || why_ctx.request[2] != 0) {
        fprintf(stderr, "FAIL: Why-not context was not replaced with second unsafe request.\n");
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }
    if (!gtk_widget_get_visible(rw->btn_explain_rejection)) {
        fprintf(stderr, "FAIL: Explain Rejection button lost visibility after second unsafe request.\n");
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }

    /* Submit wait-unavailable request: P0 asks [4, 0, 0] (<= Need [7, 4, 3], > Available [3, 3, 2]) */
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(rw->spin_req[0]), 4);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(rw->spin_req[1]), 0);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(rw->spin_req[2]), 0);

    g_signal_emit_by_name(rw->btn_submit, "clicked");
    process_pending_events();

    if (strcmp(gtk_label_get_text(GTK_LABEL(rw->badge_result_code)), "REQ_WAIT_UNAVAILABLE") != 0) {
        fprintf(stderr, "FAIL: Expected REQ_WAIT_UNAVAILABLE for P0 request [4, 0, 0].\n");
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }
    /* Existing rejection context must remain intact */
    if (!gui_model_get_why_not_context(model, &why_ctx) || why_ctx.pid != 0 ||
        why_ctx.request[0] != 0 || why_ctx.request[1] != 3 || why_ctx.request[2] != 0) {
        fprintf(stderr, "FAIL: Why-not context was corrupted by wait-unavailable request.\n");
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }
    if (!gtk_widget_get_visible(rw->btn_explain_rejection)) {
        fprintf(stderr, "FAIL: Explain Rejection button lost visibility after wait-unavailable request.\n");
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }

    /* Submit a granted request: P1 asks [1, 0, 2] */
    gtk_drop_down_set_selected(GTK_DROP_DOWN(rw->dropdown_proc), 1);
    process_pending_events();
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(rw->spin_req[0]), 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(rw->spin_req[1]), 0);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(rw->spin_req[2]), 2);

    g_signal_emit_by_name(rw->btn_submit, "clicked");
    process_pending_events();

    if (strcmp(gtk_label_get_text(GTK_LABEL(rw->badge_result_code)), "REQ_GRANTED") != 0) {
        fprintf(stderr, "FAIL: Expected REQ_GRANTED for P1 request [1, 0, 2].\n");
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }
    /* Context remains available but is marked historical */
    if (!gui_model_has_why_not(model) || !gui_model_is_why_not_stale(model)) {
        fprintf(stderr, "FAIL: Why-not context missing or not marked historical after state advancement.\n");
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }
    if (!gtk_widget_get_visible(rw->btn_explain_rejection)) {
        fprintf(stderr, "FAIL: Explain Rejection button lost visibility after state advancement.\n");
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }
    const char *btn_text = gtk_button_get_label(GTK_BUTTON(rw->btn_explain_rejection));
    if (!strstr(btn_text, "Historical")) {
        fprintf(stderr, "FAIL: Explain Rejection button label does not contain 'Historical' (got '%s').\n", btn_text);
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }

    /* Verify Historical Diagnostic banner is displayed on Why-not page */
    gtk_stack_set_visible_child_name(GTK_STACK(stack), "whynot");
    process_pending_events();

    if (!gtk_widget_get_visible(ww->banner_stale)) {
        fprintf(stderr, "FAIL: Historical banner not visible on whynot page after state change.\n");
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }
    char expected_cap_ver[64], expected_cur_ver[64];
    snprintf(expected_cap_ver, sizeof(expected_cap_ver), "State v%lu", (unsigned long)why_ctx.state_version);
    snprintf(expected_cur_ver, sizeof(expected_cur_ver), "Current State is v%lu", (unsigned long)model->state_version);
    const char *stale_txt = gtk_label_get_text(GTK_LABEL(ww->label_stale));
    if (!strstr(stale_txt, "Historical Diagnostic") || !strstr(stale_txt, expected_cap_ver) || !strstr(stale_txt, expected_cur_ver)) {
        fprintf(stderr, "FAIL: Historical banner text did not report version divergence (got '%s').\n", stale_txt);
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }

    /* Phase 3.1: Stale nearest-safe alternative handling */
    if (gtk_widget_get_sensitive(ww->btn_load_simulator)) {
        fprintf(stderr, "FAIL: btn_load_simulator is sensitive while why_not context is stale.\n");
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }
    const char *nearest_lbl_stale = gtk_label_get_text(GTK_LABEL(ww->label_alt_nearest));
    if (!strstr(nearest_lbl_stale, "Historical — Not guaranteed safe in current state")) {
        fprintf(stderr, "FAIL: label_alt_nearest missing historical warning (got '%s').\n", nearest_lbl_stale);
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }
    const char *load_status_stale = gtk_label_get_text(GTK_LABEL(ww->label_load_status));
    if (!strstr(load_status_stale, "Loading disabled: Vector belongs to historical snapshot")) {
        fprintf(stderr, "FAIL: label_load_status missing historical explanation (got '%s').\n", load_status_stale);
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }
    /* Clicking btn_load_simulator while stale must do nothing */
    g_signal_emit_by_name(ww->btn_load_simulator, "clicked");
    process_pending_events();
    if (strcmp(gtk_stack_get_visible_child_name(GTK_STACK(stack)), "whynot") != 0) {
        fprintf(stderr, "FAIL: btn_load_simulator navigated away while stale.\n");
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }

    /* Phase 3.1: Test Re-evaluation Assertion 1: WHY_ERR_BAD_STATE (dirty uncommitted draft) */
    gui_model_set_draft_available(model, 0, 99);
    GuiWhyNotContext ctx_before_bad;
    gui_model_get_why_not_context(model, &ctx_before_bad);
    g_signal_emit_by_name(ww->btn_reevaluate, "clicked");
    process_pending_events();
    const char *bad_txt = gtk_label_get_text(GTK_LABEL(ww->label_reeval_status));
    if (!strstr(bad_txt, "uncommitted draft")) {
        fprintf(stderr, "FAIL: Re-evaluation did not report uncommitted draft error (got '%s').\n", bad_txt);
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }
    GuiWhyNotContext ctx_after_bad;
    gui_model_get_why_not_context(model, &ctx_after_bad);
    if (memcmp(&ctx_before_bad, &ctx_after_bad, sizeof(GuiWhyNotContext)) != 0) {
        fprintf(stderr, "FAIL: WhyNotContext corrupted by WHY_ERR_BAD_STATE re-evaluation.\n");
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }
    if (gtk_widget_get_sensitive(ww->btn_load_simulator)) {
        fprintf(stderr, "FAIL: btn_load_simulator became sensitive after WHY_ERR_BAD_STATE.\n");
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }
    gui_model_init_draft_from_current(model);

    /* Phase 3.1: Test Re-evaluation Assertion 2: WHY_OK (request remains unsafe) */
    g_signal_emit_by_name(ww->btn_reevaluate, "clicked");
    process_pending_events();
    const char *reeval_txt = gtk_label_get_text(GTK_LABEL(ww->label_reeval_status));
    if (!strstr(reeval_txt, "request remains unsafe")) {
        fprintf(stderr, "FAIL: Expected 'request remains unsafe', got '%s'.\n", reeval_txt);
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }
    if (gui_model_is_why_not_stale(model)) {
        fprintf(stderr, "FAIL: Model still reports stale after successful WHY_OK re-evaluation.\n");
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }
    if (gtk_widget_get_visible(ww->banner_stale)) {
        fprintf(stderr, "FAIL: Stale banner remains visible after WHY_OK re-evaluation.\n");
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }
    if (!gtk_widget_get_sensitive(ww->btn_load_simulator)) {
        fprintf(stderr, "FAIL: btn_load_simulator not sensitive after WHY_OK re-evaluation.\n");
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }

    /* Test Load Nearest Safe Alternative into Simulator (never auto-submits) */
    uint64_t ver_before_load = model->state_version;
    const char *badge_res_before = gtk_label_get_text(GTK_LABEL(rw->badge_result_code));
    g_signal_emit_by_name(ww->btn_load_simulator, "clicked");
    process_pending_events();

    if (strcmp(gtk_stack_get_visible_child_name(GTK_STACK(stack)), "simulator") != 0) {
        fprintf(stderr, "FAIL: btn_load_simulator did not switch page to simulator.\n");
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }
    if (gtk_drop_down_get_selected(GTK_DROP_DOWN(rw->dropdown_proc)) != (guint)model->why_context.pid) {
        fprintf(stderr, "FAIL: Simulator selected PID mismatch after load.\n");
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }
    for (int j = 0; j < model->current_state.m; j++) {
        int loaded_val = (int)gtk_spin_button_get_value(GTK_SPIN_BUTTON(rw->spin_req[j]));
        if (loaded_val != model->why_context.report.nearest_safe_request[j]) {
            fprintf(stderr, "FAIL: Spin button %d loaded %d != nearest safe %d.\n",
                    j, loaded_val, model->why_context.report.nearest_safe_request[j]);
            gtk_window_destroy(GTK_WINDOW(win));
            g_object_unref(app);
            return 1;
        }
    }
    /* Verify request was not automatically submitted */
    if (model->state_version != ver_before_load) {
        fprintf(stderr, "FAIL: State version incremented on load (request was auto-submitted!).\n");
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }
    if (strcmp(gtk_label_get_text(GTK_LABEL(rw->badge_result_code)), badge_res_before) != 0) {
        fprintf(stderr, "FAIL: Result badge changed on load (request was auto-submitted!).\n");
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }

    /* Phase 3.1: Test Re-evaluation Assertion 3: WHY_ERR_UNAVAILABLE */
    /* Reduce Available R1 so that P0's rejected request of 3 exceeds current Available */
    gui_model_set_draft_total(model, 1, model->draft_state.total[1] - 2);
    gui_model_set_draft_available(model, 1, model->draft_state.available[1] - 2);
    if (!gui_model_commit_draft(model)) {
        fprintf(stderr, "FAIL: Failed to commit draft reducing Available R1.\n");
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }
    gtk_stack_set_visible_child_name(GTK_STACK(stack), "whynot");
    process_pending_events();

    GuiWhyNotContext ctx_before_unavail;
    gui_model_get_why_not_context(model, &ctx_before_unavail);

    g_signal_emit_by_name(ww->btn_reevaluate, "clicked");
    process_pending_events();

    const char *unavail_txt = gtk_label_get_text(GTK_LABEL(ww->label_reeval_status));
    if (!strstr(unavail_txt, "exceeds Available resources (REQ_WAIT_UNAVAILABLE)")) {
        fprintf(stderr, "FAIL: Expected unavailable status, got '%s'.\n", unavail_txt);
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }
    GuiWhyNotContext ctx_after_unavail;
    gui_model_get_why_not_context(model, &ctx_after_unavail);
    if (memcmp(&ctx_before_unavail, &ctx_after_unavail, sizeof(GuiWhyNotContext)) != 0) {
        fprintf(stderr, "FAIL: WhyNotContext corrupted by WHY_ERR_UNAVAILABLE re-evaluation.\n");
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }
    if (gtk_widget_get_sensitive(ww->btn_load_simulator)) {
        fprintf(stderr, "FAIL: btn_load_simulator sensitive after WHY_ERR_UNAVAILABLE re-evaluation.\n");
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }

    /* Phase 3.1: Test Re-evaluation Assertion 4: WHY_ERR_EXCEEDS_NEED */
    /* Set P0 Max R1 = 2 (Alloc=1 => Need=1 < request=3) with high available */
    gui_model_set_draft_max(model, 0, 1, 2);
    gui_model_set_draft_total(model, 1, 15);
    gui_model_set_draft_available(model, 1, 8);
    if (!gui_model_commit_draft(model)) {
        fprintf(stderr, "FAIL: Failed to commit draft for WHY_ERR_EXCEEDS_NEED.\n");
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }
    gtk_stack_set_visible_child_name(GTK_STACK(stack), "whynot");
    process_pending_events();

    GuiWhyNotContext ctx_before_need;
    gui_model_get_why_not_context(model, &ctx_before_need);

    g_signal_emit_by_name(ww->btn_reevaluate, "clicked");
    process_pending_events();

    const char *exceed_need_txt = gtk_label_get_text(GTK_LABEL(ww->label_reeval_status));
    if (!strstr(exceed_need_txt, "exceeds the process's remaining Need (REQ_ERR_EXCEEDS_NEED)")) {
        fprintf(stderr, "FAIL: Expected exceeds need status, got '%s'.\n", exceed_need_txt);
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }
    GuiWhyNotContext ctx_after_need;
    gui_model_get_why_not_context(model, &ctx_after_need);
    if (memcmp(&ctx_before_need, &ctx_after_need, sizeof(GuiWhyNotContext)) != 0) {
        fprintf(stderr, "FAIL: WhyNotContext corrupted by WHY_ERR_EXCEEDS_NEED re-evaluation.\n");
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }
    if (gtk_widget_get_sensitive(ww->btn_load_simulator)) {
        fprintf(stderr, "FAIL: btn_load_simulator sensitive after WHY_ERR_EXCEEDS_NEED re-evaluation.\n");
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }

    /* Phase 3.1: Test Re-evaluation Assertion 5: WHY_ERR_NOT_UNSAFE */
    /* Restore P0 Max R1 to 4 and provide plenty of resources so request is safe */
    gui_model_set_draft_max(model, 0, 1, 4);
    for (int j = 0; j < model->draft_state.m; j++) {
        gui_model_set_draft_total(model, j, model->draft_state.total[j] + 50);
        gui_model_set_draft_available(model, j, model->draft_state.available[j] + 50);
    }
    if (!gui_model_commit_draft(model)) {
        fprintf(stderr, "FAIL: Failed to commit generous resources for WHY_ERR_NOT_UNSAFE.\n");
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }
    gtk_stack_set_visible_child_name(GTK_STACK(stack), "whynot");
    process_pending_events();

    GuiWhyNotContext ctx_before_safe;
    gui_model_get_why_not_context(model, &ctx_before_safe);

    g_signal_emit_by_name(ww->btn_reevaluate, "clicked");
    process_pending_events();

    const char *safe_txt = gtk_label_get_text(GTK_LABEL(ww->label_reeval_status));
    if (!strstr(safe_txt, "this request is now SAFE! It would be granted immediately")) {
        fprintf(stderr, "FAIL: Expected now safe status, got '%s'.\n", safe_txt);
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }
    GuiWhyNotContext ctx_after_safe;
    gui_model_get_why_not_context(model, &ctx_after_safe);
    if (memcmp(&ctx_before_safe, &ctx_after_safe, sizeof(GuiWhyNotContext)) != 0) {
        fprintf(stderr, "FAIL: WhyNotContext corrupted by WHY_ERR_NOT_UNSAFE re-evaluation.\n");
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }
    if (gtk_widget_get_sensitive(ww->btn_load_simulator)) {
        fprintf(stderr, "FAIL: btn_load_simulator sensitive after WHY_ERR_NOT_UNSAFE re-evaluation.\n");
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }

    /* Phase 3.1: Dimensional Mismatch Handling */
    gui_model_set_draft_dimensions(model, model->draft_state.n, 2);
    gui_model_auto_calculate_draft_available(model);
    if (!gui_model_commit_draft(model)) {
        fprintf(stderr, "FAIL: Failed to commit dimension reduction to 2.\n");
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }
    gtk_stack_set_visible_child_name(GTK_STACK(stack), "whynot");
    process_pending_events();

    if (gtk_widget_get_sensitive(ww->btn_load_simulator)) {
        fprintf(stderr, "FAIL: btn_load_simulator sensitive during dimensional mismatch.\n");
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }
    const char *dim_status = gtk_label_get_text(GTK_LABEL(ww->label_load_status));
    if (!strstr(dim_status, "Dimensional mismatch")) {
        fprintf(stderr, "FAIL: label_load_status missing 'Dimensional mismatch' (got '%s').\n", dim_status);
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }
    /* Clicking btn_load_simulator during dimensional mismatch must safely do nothing */
    g_signal_emit_by_name(ww->btn_load_simulator, "clicked");
    process_pending_events();
    if (strcmp(gtk_stack_get_visible_child_name(GTK_STACK(stack)), "whynot") != 0) {
        fprintf(stderr, "FAIL: btn_load_simulator switched page during dimensional mismatch.\n");
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }

    /* Reset baseline and verify button becomes hidden and Why-not page reverts to empty state */
    gui_model_reset_to_original(model);
    process_pending_events();

    if (gui_model_has_why_not(model)) {
        fprintf(stderr, "FAIL: Why-not context not cleared after gui_model_reset_to_original().\n");
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }
    if (gtk_widget_get_visible(rw->btn_explain_rejection)) {
        fprintf(stderr, "FAIL: Explain Rejection button unexpectedly visible after baseline reset.\n");
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }
    if (!gtk_widget_get_visible(ww->empty_box) || gtk_widget_get_visible(ww->content_box)) {
        fprintf(stderr, "FAIL: Why-not page did not revert to empty state after baseline reset.\n");
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
    gui_model_auto_calculate_draft_available(model);
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

    /* Verify Need and Available context is refreshed with exact authoritative values */
    int cur_pid = (int)gtk_drop_down_get_selected(GTK_DROP_DOWN(rw->dropdown_proc));
    if (cur_pid < 0 || cur_pid >= model->current_state.n) {
        cur_pid = 0;
    }

    int all_need[BANKER_MAX_PROCS][BANKER_MAX_RES];
    gui_model_get_current_need(model, all_need);

    char expected_ctx_r0[128];
    char expected_ctx_r1[128];
    snprintf(expected_ctx_r0, sizeof(expected_ctx_r0), "Need: %d | Avail: %d",
             all_need[cur_pid][0], model->current_state.available[0]);
    snprintf(expected_ctx_r1, sizeof(expected_ctx_r1), "Need: %d | Avail: %d",
             all_need[cur_pid][1], model->current_state.available[1]);

    const char *ctx_r0 = gtk_label_get_text(GTK_LABEL(rw->lbl_res_context[0]));
    const char *ctx_r1 = gtk_label_get_text(GTK_LABEL(rw->lbl_res_context[1]));

    if (!ctx_r0 || strcmp(ctx_r0, expected_ctx_r0) != 0) {
        fprintf(stderr, "FAIL: Context label for R0 mismatch for Process P%d.\n  Expected: '%s'\n  Actual:   '%s'\n",
                cur_pid, expected_ctx_r0, ctx_r0 ? ctx_r0 : "<null>");
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }

    if (!ctx_r1 || strcmp(ctx_r1, expected_ctx_r1) != 0) {
        fprintf(stderr, "FAIL: Context label for R1 mismatch for Process P%d.\n  Expected: '%s'\n  Actual:   '%s'\n",
                cur_pid, expected_ctx_r1, ctx_r1 ? ctx_r1 : "<null>");
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
        !strstr(desc_zero_unsafe, "no new safety evaluation was performed")) {
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

    if (model->observer_count != 5) {
        fprintf(stderr, "FAIL: Expected 5 active observers, found %d.\n", model->observer_count);
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }

    /* 1. Remove Safety Analysis, Request Simulator, and Why-not pages from stack */
    GtkWidget *cur_safety = gtk_stack_get_child_by_name(GTK_STACK(stack), "safety");
    GtkWidget *cur_sim = gtk_stack_get_child_by_name(GTK_STACK(stack), "simulator");
    GtkWidget *cur_whynot = gtk_stack_get_child_by_name(GTK_STACK(stack), "whynot");
    if (!cur_safety || !cur_sim || !cur_whynot) {
        fprintf(stderr, "FAIL: Safety, Simulator, or Why-not page missing from stack.\n");
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }

    gtk_stack_remove(GTK_STACK(stack), cur_safety);
    gtk_stack_remove(GTK_STACK(stack), cur_sim);
    gtk_stack_remove(GTK_STACK(stack), cur_whynot);
    process_pending_events();

    /* Assert that their observers are unregistered (count drops from 5 to 2: dashboard and resources remain) */
    if (model->observer_count != 2) {
        fprintf(stderr, "FAIL: Expected 2 active observers after removing dynamic pages, found %d.\n",
                model->observer_count);
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }

    /* 2. Recreate the pages, verify observer registrations restored exactly once */
    GtkWidget *new_safety = gui_page_safety_create(model, win);
    GtkWidget *new_sim = gui_page_simulator_create(model, win);
    GtkWidget *new_whynot = gui_page_whynot_create(model, win);
    gtk_stack_add_titled(GTK_STACK(stack), new_safety, "safety", "Safety Analysis");
    gtk_stack_add_titled(GTK_STACK(stack), new_sim, "simulator", "Request Simulator");
    gtk_stack_add_titled(GTK_STACK(stack), new_whynot, "whynot", "Why-not Intelligence");
    process_pending_events();

    if (model->observer_count != 5) {
        fprintf(stderr, "FAIL: Expected 5 active observers after page recreation, found %d.\n",
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
