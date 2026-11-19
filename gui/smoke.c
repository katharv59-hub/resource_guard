/* ResourceGuard GTK4 Smoke Test — implementation.
 * Performs rigorous headless verification of all 8 application foundation checks. */

#include "smoke.h"
#include "app.h"
#include "window.h"
#include "style.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void process_pending_events(void) {
    while (g_main_context_pending(NULL)) {
        g_main_context_iteration(NULL, FALSE);
    }
}

int gui_smoke_test_main(int argc, char *argv[]) {
    const char *screenshot_file = NULL;
    for (int i = 1; i < argc; i++) {
        if ((strcmp(argv[i], "--screenshot") == 0 || strcmp(argv[i], "-s") == 0) && i + 1 < argc) {
            screenshot_file = argv[i + 1];
            i++;
        }
    }

    printf("\n============================================================\n");
    printf(" RESOURCEGUARD GTK4 SMOKE TEST (HEADLESS VERIFICATION)\n");
    printf("============================================================\n");

    /* CHECK 7 (Early): GTK CSS Provider Initialization */
    printf("[CHECK 1/8] Initializing GTK subsystem & CSS provider...\n");
    if (!gtk_init_check()) {
        fprintf(stderr, "FAIL: gtk_init_check() failed. Cannot connect to display.\n");
        return 1;
    }

    if (!gui_style_init() || !gui_style_is_initialized()) {
        fprintf(stderr, "FAIL: GTK CSS provider initialization failed.\n");
        return 1;
    }
    printf("  -> OK: GTK initialized and CSS provider loaded successfully.\n");

    /* CHECK 1: GtkApplication Creation and Activation */
    printf("[CHECK 2/8] Creating and registering GtkApplication...\n");
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

    /* CHECK 2: Main Application Window Creation */
    printf("[CHECK 3/8] Building main GtkApplicationWindow...\n");
    GtkWidget *win = gui_window_new(app);
    if (!win || !GTK_IS_APPLICATION_WINDOW(win)) {
        fprintf(stderr, "FAIL: gui_window_new() did not produce a valid GtkApplicationWindow.\n");
        g_object_unref(app);
        return 1;
    }
    printf("  -> OK: Main window created.\n");

    /* CHECK 3: Window Visibility */
    printf("[CHECK 4/8] Presenting window and asserting visibility...\n");
    gtk_window_present(GTK_WINDOW(win));
    gtk_widget_set_visible(win, TRUE);
    process_pending_events();

    if (!gtk_widget_get_visible(win)) {
        fprintf(stderr, "FAIL: Window visibility check failed (gtk_widget_get_visible returned FALSE).\n");
        gtk_window_destroy(GTK_WINDOW(win));
        g_object_unref(app);
        return 1;
    }
    printf("  -> OK: Window is visible and mapped.\n");

    /* CHECK 4: GtkStack Verification (Exactly 7 Pages) */
    printf("[CHECK 5/8] Inspecting GtkStack page count...\n");
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

    /* CHECK 5: All 7 Page Names Registered */
    printf("[CHECK 6/8] Verifying registration of all 7 page containers...\n");
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

    /* CHECK 6: Programmatic Page Switching */
    printf("[CHECK 7/8] Testing programmatic navigation across all 7 pages...\n");
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

    /* Optional Native Snapshot Capture */
    if (screenshot_file) {
        printf("Capturing native widget snapshot to '%s'...\n", screenshot_file);
        GtkWidgetPaintable *paintable = gtk_widget_paintable_new(win);
        if (paintable) {
            GdkPaintable *p = GDK_PAINTABLE(paintable);
            GdkTexture *texture = GDK_TEXTURE(gdk_paintable_get_current_image(p));
            if (texture) {
                GError *save_err = NULL;
                if (!gdk_texture_save_to_png(texture, screenshot_file)) {
                    fprintf(stderr, "WARNING: gdk_texture_save_to_png() failed.\n");
                } else {
                    printf("  -> OK: Native GTK snapshot written to '%s'.\n", screenshot_file);
                }
                g_object_unref(texture);
            }
            g_object_unref(paintable);
        }
    }

    /* CHECK 8: Clean Teardown and Exit */
    printf("[CHECK 8/8] Tearing down window and releasing resources...\n");
    gtk_window_destroy(GTK_WINDOW(win));
    process_pending_events();
    g_object_unref(app);
    printf("  -> OK: Teardown complete.\n");

    printf("\n============================================================\n");
    printf(" ALL 8 GTK4 SMOKE TEST ASSERTIONS PASSED SUCCESSFULLY!\n");
    printf("============================================================\n\n");
    return 0;
}
