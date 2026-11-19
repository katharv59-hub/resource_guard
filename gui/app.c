/* ResourceGuard GTK4 Application Controller — implementation.
 * Manages GtkApplication lifecycle, CSS initialization, and main window presentation. */

#include "app.h"
#include "window.h"
#include "style.h"

static void on_startup(GApplication *app, gpointer user_data) {
    (void)app;
    (void)user_data;
    gui_style_init();
}

static void on_activate(GApplication *app, gpointer user_data) {
    (void)user_data;
    GtkApplication *gtk_app = GTK_APPLICATION(app);
    GtkWidget *win = gui_window_new(gtk_app);
    gtk_window_present(GTK_WINDOW(win));
}

GtkApplication *gui_app_new(void) {
    GtkApplication *app = gtk_application_new(
        "io.github.katharv59.resourceguard",
        G_APPLICATION_DEFAULT_FLAGS
    );

    g_signal_connect(app, "startup",  G_CALLBACK(on_startup),  NULL);
    g_signal_connect(app, "activate", G_CALLBACK(on_activate), NULL);

    return app;
}
