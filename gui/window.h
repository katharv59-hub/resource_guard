/* ResourceGuard GTK4 Main Application Window. */
#ifndef GUI_WINDOW_H
#define GUI_WINDOW_H

#include <gtk/gtk.h>

GtkWidget *gui_window_new(GtkApplication *app);
GtkWidget *gui_window_get_stack(GtkWidget *win);

#endif /* GUI_WINDOW_H */
