/* ResourceGuard GTK4 Safety Analysis View.
 * Provides interactive step-through visualization of Banker's Algorithm safety trace,
 * Work vector derivation, Finish array tracking, and stuck iteration diagnostics. */

#ifndef GUI_SAFETY_VIEW_H
#define GUI_SAFETY_VIEW_H

#include <gtk/gtk.h>
#include "model.h"

typedef struct {
    GuiModel  *model;
    GtkWidget *window;

    /* Cached safety run */
    bool       has_analysis;
    bool       is_safe;
    int        safe_seq[BANKER_MAX_PROCS];
    int        safe_seq_len;
    Trace      trace;
    int        current_step;
    uint64_t   analyzed_version;

    /* Top-level containers */
    GtkWidget *empty_box;
    GtkWidget *content_box;

    /* Header badges */
    GtkWidget *badge_status;
    GtkWidget *badge_version;
    GtkWidget *badge_dim;

    /* Sequence banner */
    GtkWidget *label_seq;

    /* Stepper navigation widgets */
    GtkWidget *btn_prev;
    GtkWidget *btn_next;
    GtkWidget *btn_restart;
    GtkWidget *label_step_indicator;

    /* Step details widgets */
    GtkWidget *card_step_detail;
    GtkWidget *label_iter_title;
    GtkWidget *label_work_before;
    GtkWidget *box_finish_indicators;
    GtkWidget *label_process_picked;
    GtkWidget *label_need_eval;
    GtkWidget *label_alloc_returned;
    GtkWidget *label_work_after;
    GtkWidget *box_stuck_diagnostic;
    GtkWidget *label_stuck_reason;

    /* Complete Trace Table Container */
    GtkWidget *box_trace_table;
} SafetyWidgets;

GtkWidget *gui_page_safety_create(GuiModel *model, GtkWidget *window);
SafetyWidgets *gui_safety_view_get_widgets(GtkWidget *page_widget);

#endif /* GUI_SAFETY_VIEW_H */
