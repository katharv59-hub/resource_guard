/* ResourceGuard GTK4 Smoke Test — implementation.
 * Performs rigorous headless verification of all 8 application foundation checks,
 * including explicit mapping assertion and CSS parsing error auditing. */

#include "smoke.h"
#include "app.h"
#include "window.h"
#include "style.h"
#include "model.h"
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
    printf("[CHECK 1/9] GTK Subsystem & CSS Provider Initialization (Zero Parse Errors)...\n");
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
    printf("[CHECK 2/9] Creating and registering GtkApplication...\n");
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
    printf("[CHECK 3/9] Building main GtkApplicationWindow and verifying model binding...\n");
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
    printf("[CHECK 4/9] Presenting window and asserting visibility & mapped state (gtk_widget_get_mapped)...\n");
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
    printf("[CHECK 5/9] Inspecting GtkStack page count...\n");
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
    printf("[CHECK 6/9] Verifying registration of all 7 page containers...\n");
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
    printf("[CHECK 7/9] Testing programmatic navigation across all 7 pages...\n");
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
    printf("[CHECK 8/9] Verifying uncommitted draft persistence across page navigation...\n");
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

    /* Switch back to Dashboard before exit */
    gtk_stack_set_visible_child_name(GTK_STACK(stack), "dashboard");
    process_pending_events();

    /* CHECK 9: Clean Teardown and Exit */
    printf("[CHECK 9/9] Tearing down window and releasing resources...\n");
    gtk_window_destroy(GTK_WINDOW(win));
    process_pending_events();
    g_object_unref(app);
    printf("  -> OK: Teardown complete.\n");

    printf("\n============================================================\n");
    printf(" ALL 9 GTK4 SMOKE TEST ASSERTIONS PASSED SUCCESSFULLY!\n");
    printf("============================================================\n\n");
    return 0;
}
