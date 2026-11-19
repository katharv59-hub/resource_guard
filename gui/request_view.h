/* ResourceGuard GTK4 Request Simulator View.
 * Provides interactive resource request construction, transactional evaluation
 * via banker_request(), live Need/Available context, and comprehensive RequestResult diagnostics. */

#ifndef GUI_REQUEST_VIEW_H
#define GUI_REQUEST_VIEW_H

#include <gtk/gtk.h>
#include "model.h"

typedef struct {
    GuiModel  *model;
    GtkWidget *window;

    /* Top-level containers */
    GtkWidget *empty_box;
    GtkWidget *content_box;

    /* Header badges */
    GtkWidget *badge_status;
    GtkWidget *badge_version;
    GtkWidget *badge_dim;

    /* Uncommitted draft warning banner */
    GtkWidget *banner_draft_warning;
    GtkWidget *label_draft_warning;

    /* Input section */
    GtkWidget *dropdown_proc;
    GtkWidget *box_resource_inputs;
    GtkWidget *spin_req[BANKER_MAX_RES];
    GtkWidget *lbl_res_context[BANKER_MAX_RES];

    /* Process telemetry context labels */
    GtkWidget *label_proc_alloc;
    GtkWidget *label_proc_max;
    GtkWidget *label_proc_need;
    GtkWidget *label_sys_avail;

    /* Action buttons */
    GtkWidget *btn_submit;
    GtkWidget *btn_clear;

    /* Result presentation card and widgets */
    GtkWidget *card_result;
    GtkWidget *badge_result_code;
    GtkWidget *label_result_title;
    GtkWidget *label_result_desc;
    GtkWidget *box_result_details;
} SimulatorWidgets;

GtkWidget *gui_page_simulator_create(GuiModel *model, GtkWidget *window);
SimulatorWidgets *gui_request_view_get_widgets(GtkWidget *page_widget);

#endif /* GUI_REQUEST_VIEW_H */
