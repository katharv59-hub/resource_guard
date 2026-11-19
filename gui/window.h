/* ResourceGuard GTK4 Main Application Window. */
#ifndef GUI_WINDOW_H
#define GUI_WINDOW_H

#include <gtk/gtk.h>
#include "model.h"

GtkWidget *gui_window_new(GtkApplication *app);
GtkWidget *gui_window_get_stack(GtkWidget *win);
GuiModel  *gui_window_get_model(GtkWidget *win);
void       gui_window_switch_to_page(GtkWidget *win, const char *page_name);

#endif /* GUI_WINDOW_H */
