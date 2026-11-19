/* ResourceGuard GTK4 Page Containers.
 * Pure presentation layer — provides structured layouts for the 7 application views. */

#ifndef GUI_PAGES_H
#define GUI_PAGES_H

#include <gtk/gtk.h>

GtkWidget *gui_page_dashboard_create(void);
GtkWidget *gui_page_resources_create(void);
GtkWidget *gui_page_safety_create(void);
GtkWidget *gui_page_simulator_create(void);
GtkWidget *gui_page_whynot_create(void);
GtkWidget *gui_page_concurrency_create(void);
GtkWidget *gui_page_history_create(void);

#endif /* GUI_PAGES_H */
