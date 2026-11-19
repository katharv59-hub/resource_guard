/* ResourceGuard GTK4 Desktop Application — main entry point.
 * Dispatches to standard GtkApplication lifecycle or headless smoke test. */

#include "app.h"
#include "smoke.h"
#include <string.h>

int main(int argc, char *argv[]) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--smoke-test") == 0) {
            return gui_smoke_test_main(argc, argv);
        }
    }

    GtkApplication *app = gui_app_new();
    int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    return status;
}
