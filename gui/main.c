/* ResourceGuard GTK4 Desktop Application — main entry point.
 * Launches GtkApplication lifecycle and enters GLib main event loop. */

#include "app.h"

int main(int argc, char *argv[]) {
    GtkApplication *app = gui_app_new();
    int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    return status;
}
