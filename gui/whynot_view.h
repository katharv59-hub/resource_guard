/* ResourceGuard GTK4 Why-not Intelligence View.
 * Provides exhaustive request-space diagnostic exploration, Manhattan distance metrics,
 * exact algorithmic denial safety traces with resource deficits, and actionable safe alternatives. */

#ifndef GUI_WHYNOT_VIEW_H
#define GUI_WHYNOT_VIEW_H

#include <gtk/gtk.h>
#include "model.h"

typedef struct {
    GuiModel  *model;
    GtkWidget *window;

    /* Top-level containers */
    GtkWidget *empty_box;
    GtkWidget *content_box;

    /* Header badges */
    GtkWidget *badge_proc;
    GtkWidget *badge_req;
    GtkWidget *badge_version;
    GtkWidget *badge_status;

    /* Warning & Status Banners */
    GtkWidget *banner_stale;
    GtkWidget *label_stale;
    GtkWidget *banner_draft;
    GtkWidget *label_draft;
    GtkWidget *banner_oversized;
    GtkWidget *label_oversized;

    /* Card 1: Diagnostic Exploration Summary */
    GtkWidget *card_summary;
    GtkWidget *label_total_candidates;
    GtkWidget *label_safe_candidates;
    GtkWidget *label_unsafe_candidates;
    GtkWidget *label_search_bounds;
    GtkWidget *label_nearest_safe;
    GtkWidget *label_nearest_distance;
    GtkWidget *label_compwise_max;
    GtkWidget *badge_compwise_safe;

    /* Card 2: Denial Safety Trace & Resource Deficits */
    GtkWidget *card_trace;
    GtkWidget *box_trace_steps;
    GtkWidget *label_stall_explanation;
    GtkWidget *box_blocked_procs;

    /* Card 3: Actionable Safe Alternatives */
    GtkWidget *card_alternatives;
    GtkWidget *label_alt_nearest;
    GtkWidget *label_alt_distance;
    GtkWidget *label_alt_compwise;
    GtkWidget *btn_load_simulator;
    GtkWidget *label_load_status;

    /* Card 4: Live Re-evaluation */
    GtkWidget *card_reeval;
    GtkWidget *btn_reevaluate;
    GtkWidget *label_reeval_status;

} WhynotWidgets;

GtkWidget *gui_page_whynot_create(GuiModel *model, GtkWidget *window);
WhynotWidgets *gui_whynot_view_get_widgets(GtkWidget *page_widget);

#endif /* GUI_WHYNOT_VIEW_H */
