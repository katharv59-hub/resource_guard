/* ResourceGuard GTK4 Safety Analysis View.
 * Provides interactive step-through visualization of Banker's Algorithm safety trace,
 * Work vector derivation, Finish array tracking, and stuck iteration diagnostics. */

#ifndef GUI_SAFETY_VIEW_H
#define GUI_SAFETY_VIEW_H

#include <gtk/gtk.h>
#include "model.h"

GtkWidget *gui_page_safety_create(GuiModel *model, GtkWidget *window);

#endif /* GUI_SAFETY_VIEW_H */
