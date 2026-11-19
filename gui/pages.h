/* ResourceGuard GTK4 Page Containers.
 * Presentation layer — provides interactive layouts for all 7 application views. */

#ifndef GUI_PAGES_H
#define GUI_PAGES_H

#include <gtk/gtk.h>
#include <stdbool.h>
#include "model.h"

typedef struct {
    GuiModel  *model;
    GtkWidget *window;

    GtkWidget *empty_box;
    GtkWidget *content_box;

    /* Header Badges */
    GtkWidget *badge_safety;
    GtkWidget *badge_version;
    GtkWidget *badge_dim;

    /* Telemetry Labels */
    GtkWidget *label_source;
    GtkWidget *label_safety_title;
    GtkWidget *label_safety_seq;
    GtkWidget *label_safety_desc;
    GtkWidget *label_total_vec;
    GtkWidget *label_avail_vec;
} DashboardWidgets;

typedef struct {
    GuiModel  *model;
    GtkWidget *window;

    /* Dimension Controls */
    GtkWidget *spin_n;
    GtkWidget *spin_m;

    /* Vectors Grid */
    GtkWidget *box_vectors;
    GtkWidget *grid_vectors;
    GtkWidget *spin_total[BANKER_MAX_RES];
    GtkWidget *spin_available[BANKER_MAX_RES];

    /* Allocation Matrix */
    GtkWidget *grid_alloc;
    GtkWidget *spin_alloc[BANKER_MAX_PROCS][BANKER_MAX_RES];

    /* Maximum Matrix */
    GtkWidget *grid_max;
    GtkWidget *spin_max[BANKER_MAX_PROCS][BANKER_MAX_RES];

    /* Need Matrix (Read-Only) */
    GtkWidget *grid_need;
    GtkWidget *label_need[BANKER_MAX_PROCS][BANKER_MAX_RES];

    /* Validation & Commit */
    GtkWidget *banner_box;
    GtkWidget *banner_label;
    GtkWidget *badge_status;
    GtkWidget *btn_commit;
    GtkWidget *btn_discard;

    bool       updating_ui;
} ResourcesWidgets;

GtkWidget *gui_page_dashboard_create(GuiModel *model, GtkWidget *window);
DashboardWidgets *gui_dashboard_view_get_widgets(GtkWidget *page_widget);

GtkWidget *gui_page_resources_create(GuiModel *model, GtkWidget *window);
ResourcesWidgets *gui_resources_view_get_widgets(GtkWidget *page_widget);

GtkWidget *gui_page_safety_create(GuiModel *model, GtkWidget *window);
GtkWidget *gui_page_simulator_create(GuiModel *model, GtkWidget *window);
GtkWidget *gui_page_whynot_create(void);
GtkWidget *gui_page_concurrency_create(void);
GtkWidget *gui_page_history_create(void);

#endif /* GUI_PAGES_H */
