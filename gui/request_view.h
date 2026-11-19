/* ResourceGuard GTK4 Request Simulator View.
 * Provides interactive resource request construction, transactional evaluation
 * via banker_request(), live Need/Available context, and comprehensive RequestResult diagnostics. */

#ifndef GUI_REQUEST_VIEW_H
#define GUI_REQUEST_VIEW_H

#include <gtk/gtk.h>
#include "model.h"

GtkWidget *gui_page_simulator_create(GuiModel *model, GtkWidget *window);

#endif /* GUI_REQUEST_VIEW_H */
