/* ResourceGuard GTK4 Page Containers.
 * Presentation layer — provides interactive layouts for all 7 application views. */

#ifndef GUI_PAGES_H
#define GUI_PAGES_H

#include <gtk/gtk.h>
#include "model.h"

GtkWidget *gui_page_dashboard_create(GuiModel *model, GtkWidget *window);
GtkWidget *gui_page_resources_create(GuiModel *model, GtkWidget *window);
GtkWidget *gui_page_safety_create(GuiModel *model, GtkWidget *window);
GtkWidget *gui_page_simulator_create(GuiModel *model, GtkWidget *window);
GtkWidget *gui_page_whynot_create(void);
GtkWidget *gui_page_concurrency_create(void);
GtkWidget *gui_page_history_create(void);

#endif /* GUI_PAGES_H */
