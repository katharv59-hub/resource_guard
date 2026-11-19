/* ResourceGuard GTK4 Page Containers — implementation.
 * Provides interactive resource matrix editing, live Need derivation,
 * validation feedback, scenario loading, and dashboard telemetry. */

#include "pages.h"
#include "window.h"
#include "model.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/*  Reusable Layout Helpers                                            */
/* ------------------------------------------------------------------ */

static GtkWidget *create_card(const char *title, const char *subtitle) {
    GtkWidget *card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_add_css_class(card, "card");

    if (title) {
        GtkWidget *t = gtk_label_new(title);
        gtk_widget_add_css_class(t, "card-title");
        gtk_label_set_xalign(GTK_LABEL(t), 0.0f);
        gtk_box_append(GTK_BOX(card), t);
    }
    if (subtitle) {
        GtkWidget *s = gtk_label_new(subtitle);
        gtk_widget_add_css_class(s, "card-subtitle");
        gtk_label_set_xalign(GTK_LABEL(s), 0.0f);
        gtk_box_append(GTK_BOX(card), s);
    }
    return card;
}

static GtkWidget *create_badge(const char *text, const char *css_class) {
    GtkWidget *badge = gtk_label_new(text);
    gtk_widget_add_css_class(badge, "badge");
    if (css_class) {
        gtk_widget_add_css_class(badge, css_class);
    }
    return badge;
}

static GtkWidget *create_placeholder_banner(const char *title, const char *target_milestone) {
    GtkWidget *banner = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_add_css_class(banner, "placeholder-banner");

    char header_buf[128];
    snprintf(header_buf, sizeof(header_buf), "[Pending Integration] %s", title);
    GtkWidget *h = gtk_label_new(header_buf);
    gtk_widget_add_css_class(h, "card-title");
    gtk_label_set_xalign(GTK_LABEL(h), 0.0f);
    gtk_box_append(GTK_BOX(banner), h);

    char desc_buf[256];
    snprintf(desc_buf, sizeof(desc_buf),
             "This interactive subsystem is scheduled for %s. Backend engine hooks are prepared.",
             target_milestone);
    GtkWidget *desc = gtk_label_new(desc_buf);
    gtk_label_set_xalign(GTK_LABEL(desc), 0.0f);
    gtk_widget_add_css_class(desc, "placeholder-text");
    gtk_box_append(GTK_BOX(banner), desc);

    return banner;
}

static GtkWidget *wrap_in_scrolled_window(GtkWidget *content_box) {
    gtk_widget_add_css_class(content_box, "page-container");
    GtkWidget *scrolled = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
                                   GTK_POLICY_NEVER,
                                   GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled), content_box);
    return scrolled;
}

/* ------------------------------------------------------------------ */
/*  1. Dashboard Page (Live Telemetry & Safety State)                  */
/* ------------------------------------------------------------------ */

DashboardWidgets *gui_dashboard_view_get_widgets(GtkWidget *page_widget) {
    if (!page_widget) return NULL;
    return (DashboardWidgets *)g_object_get_data(G_OBJECT(page_widget), "dashboard_widgets");
}

static void dashboard_refresh(DashboardWidgets *w) {
    if (!w || !w->model) return;

    GuiDashboardSnapshot snap;
    gui_model_get_dashboard_snapshot(w->model, &snap);

    if (!snap.is_loaded) {
        gtk_widget_set_visible(w->empty_box, TRUE);
        gtk_widget_set_visible(w->content_box, FALSE);
        return;
    }

    gtk_widget_set_visible(w->empty_box, FALSE);
    gtk_widget_set_visible(w->content_box, TRUE);

    /* Update Header Badges */
    if (snap.is_safe) {
        gtk_label_set_text(GTK_LABEL(w->badge_safety), "STATUS: SAFE STATE");
        gtk_widget_remove_css_class(w->badge_safety, "badge-rose");
        gtk_widget_add_css_class(w->badge_safety, "badge-emerald");
    } else {
        gtk_label_set_text(GTK_LABEL(w->badge_safety), "STATUS: UNSAFE STATE");
        gtk_widget_remove_css_class(w->badge_safety, "badge-emerald");
        gtk_widget_add_css_class(w->badge_safety, "badge-rose");
    }

    char ver_buf[64];
    snprintf(ver_buf, sizeof(ver_buf), "State: v%lu", (unsigned long)snap.state_version);
    gtk_label_set_text(GTK_LABEL(w->badge_version), ver_buf);

    char dim_buf[64];
    snprintf(dim_buf, sizeof(dim_buf), "%d Processes x %d Resources", snap.n, snap.m);
    gtk_label_set_text(GTK_LABEL(w->badge_dim), dim_buf);

    /* Update Source Info */
    char src_buf[768];
    snprintf(src_buf, sizeof(src_buf), "Scenario: %s  |  Source: %s",
             snap.scenario_name,
             snap.scenario_path[0] ? snap.scenario_path : "[Direct Edit]");
    gtk_label_set_text(GTK_LABEL(w->label_source), src_buf);

    /* Update Safety Section */
    if (snap.is_safe) {
        gtk_label_set_text(GTK_LABEL(w->label_safety_title), "Safe Execution Sequence Verified:");
        char seq_buf[256];
        size_t pos = (size_t)snprintf(seq_buf, sizeof(seq_buf), "< ");
        for (int i = 0; i < snap.safe_seq_len && pos < sizeof(seq_buf) - 8; i++) {
            int written = snprintf(seq_buf + pos, sizeof(seq_buf) - pos, "%sP%d", (i > 0 ? " -> " : ""), snap.safe_seq[i]);
            if (written > 0 && pos + (size_t)written < sizeof(seq_buf)) {
                pos += (size_t)written;
            } else {
                break;
            }
        }
        if (pos + 3 < sizeof(seq_buf)) {
            snprintf(seq_buf + pos, sizeof(seq_buf) - pos, " >");
        }
        gtk_label_set_text(GTK_LABEL(w->label_safety_seq), seq_buf);
        gtk_label_set_text(GTK_LABEL(w->label_safety_desc),
                           "All processes can acquire their maximum declared demand and terminate safely without deadlock.");
    } else {
        gtk_label_set_text(GTK_LABEL(w->label_safety_title), "Unsafe Allocation State Detected:");
        gtk_label_set_text(GTK_LABEL(w->label_safety_seq), "< No Safe Execution Sequence Exists >");
        gtk_label_set_text(GTK_LABEL(w->label_safety_desc),
                           "No safe execution sequence exists for current state. Potential deadlock risk under maximum demand; safe process completion cannot be guaranteed.");
    }

    /* Update Total Vector */
    char total_buf[256];
    size_t tpos = 0;
    total_buf[0] = '\0';
    for (int j = 0; j < snap.m && tpos < sizeof(total_buf) - 1; j++) {
        int written = snprintf(total_buf + tpos, sizeof(total_buf) - tpos, "R%d: %-3d   ", j, snap.total[j]);
        if (written > 0 && tpos + (size_t)written < sizeof(total_buf)) {
            tpos += (size_t)written;
        } else {
            break;
        }
    }
    gtk_label_set_text(GTK_LABEL(w->label_total_vec), total_buf);

    /* Update Available Vector */
    char avail_buf[256];
    size_t apos = 0;
    avail_buf[0] = '\0';
    for (int j = 0; j < snap.m && apos < sizeof(avail_buf) - 1; j++) {
        int written = snprintf(avail_buf + apos, sizeof(avail_buf) - apos, "R%d: %-3d   ", j, snap.available[j]);
        if (written > 0 && apos + (size_t)written < sizeof(avail_buf)) {
            apos += (size_t)written;
        } else {
            break;
        }
    }
    gtk_label_set_text(GTK_LABEL(w->label_avail_vec), avail_buf);
}

static void on_dashboard_model_changed(GuiModel *model, void *user_data) {
    (void)model;
    dashboard_refresh((DashboardWidgets *)user_data);
}

static void on_dashboard_free(gpointer data) {
    DashboardWidgets *w = (DashboardWidgets *)data;
    if (w) {
        if (w->model) {
            gui_model_remove_observer(w->model, on_dashboard_model_changed, w);
            w->model = NULL;
        }
        g_free(w);
    }
}

static void on_dashboard_destroy(GtkWidget *widget, gpointer user_data) {
    (void)widget;
    DashboardWidgets *w = (DashboardWidgets *)user_data;
    if (w && w->model) {
        gui_model_remove_observer(w->model, on_dashboard_model_changed, w);
        w->model = NULL;
    }
}

static void on_dashboard_map(GtkWidget *widget, gpointer user_data) {
    (void)widget;
    dashboard_refresh((DashboardWidgets *)user_data);
}

static void on_dash_goto_resources(GtkWidget *btn, gpointer user_data) {
    (void)btn;
    DashboardWidgets *w = (DashboardWidgets *)user_data;
    gui_window_switch_to_page(w->window, "resources");
}

static void on_dash_load_demo(GtkWidget *btn, gpointer user_data) {
    (void)btn;
    DashboardWidgets *w = (DashboardWidgets *)user_data;
    gui_model_load_demo(w->model);
}

static void on_dash_reset_baseline(GtkWidget *btn, gpointer user_data) {
    (void)btn;
    DashboardWidgets *w = (DashboardWidgets *)user_data;
    gui_model_reset_to_original(w->model);
}

GtkWidget *gui_page_dashboard_create(GuiModel *model, GtkWidget *window) {
    DashboardWidgets *w = g_new0(DashboardWidgets, 1);
    w->model = model;
    w->window = window;

    GtkWidget *page_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    g_object_set_data(G_OBJECT(page_box), "widgets", w);
    g_signal_connect(page_box, "map", G_CALLBACK(on_dashboard_map), w);
    g_signal_connect(page_box, "destroy", G_CALLBACK(on_dashboard_destroy), w);

    /* Header Card */
    GtkWidget *header_card = create_card("System Dashboard",
                                         "Real-time BankerState telemetry, dimensional bounds, and algorithmic safety status.");
    GtkWidget *badge_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    w->badge_safety = create_badge("STATUS: SAFE STATE", "badge-emerald");
    w->badge_version = create_badge("State: v1", "badge-teal");
    w->badge_dim = create_badge("5 Processes x 3 Resources", "badge-muted");
    gtk_box_append(GTK_BOX(badge_box), w->badge_safety);
    gtk_box_append(GTK_BOX(badge_box), w->badge_version);
    gtk_box_append(GTK_BOX(badge_box), w->badge_dim);
    gtk_box_append(GTK_BOX(header_card), badge_box);
    gtk_box_append(GTK_BOX(page_box), header_card);

    /* Empty State Container */
    w->empty_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_add_css_class(w->empty_box, "empty-state-box");
    GtkWidget *empty_title = gtk_label_new("No Active Resource Configuration");
    gtk_widget_add_css_class(empty_title, "empty-state-title");
    GtkWidget *empty_sub = gtk_label_new("No scenario is currently loaded into the simulator.\n"
                                         "Load a benchmark scenario file or configure matrices manually in System Resources.");
    gtk_widget_add_css_class(empty_sub, "empty-state-subtitle");
    gtk_box_append(GTK_BOX(w->empty_box), empty_title);
    gtk_box_append(GTK_BOX(w->empty_box), empty_sub);

    GtkWidget *empty_btn_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    GtkWidget *btn_demo = gtk_button_new_with_label("Load Demo Baseline (5p, 3r)");
    gtk_widget_add_css_class(btn_demo, "primary");
    g_signal_connect(btn_demo, "clicked", G_CALLBACK(on_dash_load_demo), w);
    GtkWidget *btn_res = gtk_button_new_with_label("Open System Resources Matrix");
    g_signal_connect(btn_res, "clicked", G_CALLBACK(on_dash_goto_resources), w);
    gtk_box_append(GTK_BOX(empty_btn_box), btn_demo);
    gtk_box_append(GTK_BOX(empty_btn_box), btn_res);
    gtk_box_append(GTK_BOX(w->empty_box), empty_btn_box);
    gtk_box_append(GTK_BOX(page_box), w->empty_box);

    /* Content Container */
    w->content_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);

    /* Safety Status Card */
    GtkWidget *safety_card = create_card("Safety Analysis & Execution Feasibility",
                                         "Evaluates if an execution order exists guaranteeing all processes satisfy max demand.");
    w->label_safety_title = gtk_label_new("Safe Execution Sequence Verified:");
    gtk_widget_add_css_class(w->label_safety_title, "card-title");
    gtk_label_set_xalign(GTK_LABEL(w->label_safety_title), 0.0f);
    gtk_box_append(GTK_BOX(safety_card), w->label_safety_title);

    w->label_safety_seq = gtk_label_new("< P1 -> P3 -> P4 -> P0 -> P2 >");
    gtk_widget_add_css_class(w->label_safety_seq, "mono-text");
    gtk_label_set_xalign(GTK_LABEL(w->label_safety_seq), 0.0f);
    gtk_box_append(GTK_BOX(safety_card), w->label_safety_seq);

    w->label_safety_desc = gtk_label_new("");
    gtk_widget_add_css_class(w->label_safety_desc, "card-body");
    gtk_label_set_xalign(GTK_LABEL(w->label_safety_desc), 0.0f);
    gtk_label_set_wrap(GTK_LABEL(w->label_safety_desc), TRUE);
    gtk_box_append(GTK_BOX(safety_card), w->label_safety_desc);
    gtk_box_append(GTK_BOX(w->content_box), safety_card);

    /* Vectors Summary Card */
    GtkWidget *vec_card = create_card("Resource Vectors Telemetry",
                                      "Global resource bounds and available unallocated instances.");
    GtkWidget *lbl_t_title = gtk_label_new("Total Resource Capacity Vector (Total[j]):");
    gtk_label_set_xalign(GTK_LABEL(lbl_t_title), 0.0f);
    gtk_widget_add_css_class(lbl_t_title, "card-subtitle");
    gtk_box_append(GTK_BOX(vec_card), lbl_t_title);

    w->label_total_vec = gtk_label_new("");
    gtk_widget_add_css_class(w->label_total_vec, "mono-text");
    gtk_label_set_xalign(GTK_LABEL(w->label_total_vec), 0.0f);
    gtk_box_append(GTK_BOX(vec_card), w->label_total_vec);

    GtkWidget *lbl_a_title = gtk_label_new("Available Unallocated Vector (Available[j]):");
    gtk_label_set_xalign(GTK_LABEL(lbl_a_title), 0.0f);
    gtk_widget_add_css_class(lbl_a_title, "card-subtitle");
    gtk_box_append(GTK_BOX(vec_card), lbl_a_title);

    w->label_avail_vec = gtk_label_new("");
    gtk_widget_add_css_class(w->label_avail_vec, "mono-text");
    gtk_label_set_xalign(GTK_LABEL(w->label_avail_vec), 0.0f);
    gtk_box_append(GTK_BOX(vec_card), w->label_avail_vec);
    gtk_box_append(GTK_BOX(w->content_box), vec_card);

    /* Source & Quick Actions Card */
    GtkWidget *action_card = create_card("Configuration Source & Simulator Controls",
                                         "Quick actions to reconfigure, load scenarios, or reset state.");
    w->label_source = gtk_label_new("");
    gtk_widget_add_css_class(w->label_source, "card-body");
    gtk_label_set_xalign(GTK_LABEL(w->label_source), 0.0f);
    gtk_box_append(GTK_BOX(action_card), w->label_source);

    GtkWidget *act_btn_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget *b_edit = gtk_button_new_with_label("Edit Resources Matrix");
    gtk_widget_add_css_class(b_edit, "primary");
    g_signal_connect(b_edit, "clicked", G_CALLBACK(on_dash_goto_resources), w);

    GtkWidget *b_reset = gtk_button_new_with_label("Reset to Baseline");
    g_signal_connect(b_reset, "clicked", G_CALLBACK(on_dash_reset_baseline), w);

    gtk_box_append(GTK_BOX(act_btn_box), b_edit);
    gtk_box_append(GTK_BOX(act_btn_box), b_reset);
    gtk_box_append(GTK_BOX(action_card), act_btn_box);
    gtk_box_append(GTK_BOX(w->content_box), action_card);

    gtk_box_append(GTK_BOX(page_box), w->content_box);

    /* Register observer and initial refresh */
    gui_model_add_observer(model, on_dashboard_model_changed, w);
    dashboard_refresh(w);

    GtkWidget *scrolled = wrap_in_scrolled_window(page_box);
    g_object_set_data_full(G_OBJECT(scrolled), "dashboard_widgets", w, on_dashboard_free);
    return scrolled;
}

/* ------------------------------------------------------------------ */
/*  2. System Resources Page (Matrix Editor & Validation)              */
/* ------------------------------------------------------------------ */

ResourcesWidgets *gui_resources_view_get_widgets(GtkWidget *page_widget) {
    if (!page_widget) return NULL;
    return (ResourcesWidgets *)g_object_get_data(G_OBJECT(page_widget), "resources_widgets");
}

static void update_validation_display(ResourcesWidgets *w) {
    if (!w || !w->model) return;

    BankerStatus status = gui_model_validate_draft(w->model);

    gtk_label_set_text(GTK_LABEL(w->banner_label), w->model->draft_validation_msg);

    if (status == BANKER_OK) {
        gtk_widget_remove_css_class(w->banner_box, "error-banner");
        gtk_widget_add_css_class(w->banner_box, "valid-banner");
        gtk_widget_set_sensitive(w->btn_commit, TRUE);
        gtk_label_set_text(GTK_LABEL(w->badge_status), "DRAFT VALID");
        gtk_widget_remove_css_class(w->badge_status, "badge-rose");
        gtk_widget_remove_css_class(w->badge_status, "badge-amber");
        gtk_widget_add_css_class(w->badge_status, "badge-emerald");
    } else {
        gtk_widget_remove_css_class(w->banner_box, "valid-banner");
        gtk_widget_add_css_class(w->banner_box, "error-banner");
        gtk_widget_set_sensitive(w->btn_commit, FALSE);
        gtk_label_set_text(GTK_LABEL(w->badge_status), "INVALID DRAFT");
        gtk_widget_remove_css_class(w->badge_status, "badge-emerald");
        gtk_widget_remove_css_class(w->badge_status, "badge-amber");
        gtk_widget_add_css_class(w->badge_status, "badge-rose");
    }
}

/* Update derived Need matrix using the engine's authoritative banker_need */
static void update_need_display(ResourcesWidgets *w) {
    if (!w || !w->model) return;
    int need[BANKER_MAX_PROCS][BANKER_MAX_RES];
    gui_model_get_draft_need(w->model, need);

    int n = w->model->draft_state.n;
    int m = w->model->draft_state.m;
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < m; j++) {
            if (w->label_need[i][j]) {
                char n_buf[32];
                snprintf(n_buf, sizeof(n_buf), "%d", need[i][j]);
                gtk_label_set_text(GTK_LABEL(w->label_need[i][j]), n_buf);
            }
        }
    }
}

static void on_alloc_cell_changed(GtkSpinButton *spin, gpointer user_data) {
    ResourcesWidgets *w = (ResourcesWidgets *)user_data;
    if (w->updating_ui) return;

    int p = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(spin), "proc"));
    int r = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(spin), "res"));
    int val = gtk_spin_button_get_value_as_int(spin);

    gui_model_set_draft_alloc(w->model, p, r, val);

    /* Update derived Need matrix via authoritative banker_need */
    update_need_display(w);
    update_validation_display(w);
}

static void on_max_cell_changed(GtkSpinButton *spin, gpointer user_data) {
    ResourcesWidgets *w = (ResourcesWidgets *)user_data;
    if (w->updating_ui) return;

    int p = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(spin), "proc"));
    int r = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(spin), "res"));
    int val = gtk_spin_button_get_value_as_int(spin);

    gui_model_set_draft_max(w->model, p, r, val);

    /* Update derived Need matrix via authoritative banker_need */
    update_need_display(w);
    update_validation_display(w);
}

static void on_total_cell_changed(GtkSpinButton *spin, gpointer user_data) {
    ResourcesWidgets *w = (ResourcesWidgets *)user_data;
    if (w->updating_ui) return;

    int r = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(spin), "res"));
    int val = gtk_spin_button_get_value_as_int(spin);

    gui_model_set_draft_total(w->model, r, val);
    update_validation_display(w);
}

static void on_available_cell_changed(GtkSpinButton *spin, gpointer user_data) {
    ResourcesWidgets *w = (ResourcesWidgets *)user_data;
    if (w->updating_ui) return;

    int r = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(spin), "res"));
    int val = gtk_spin_button_get_value_as_int(spin);

    gui_model_set_draft_available(w->model, r, val);
    update_validation_display(w);
}

static void rebuild_matrix_grids(ResourcesWidgets *w) {
    w->updating_ui = true;
    int n = w->model->draft_state.n;
    int m = w->model->draft_state.m;

    /* ---- 1. Rebuild Vectors Grid (Total & Available) ---- */
    while (gtk_widget_get_first_child(w->grid_vectors)) {
        gtk_grid_remove_row(GTK_GRID(w->grid_vectors), 0);
    }

    GtkWidget *lbl_hdr_tot = gtk_label_new("Total Vector");
    gtk_widget_add_css_class(lbl_hdr_tot, "matrix-header-proc");
    gtk_grid_attach(GTK_GRID(w->grid_vectors), lbl_hdr_tot, 0, 0, 1, 1);

    GtkWidget *lbl_hdr_av = gtk_label_new("Available Vector");
    gtk_widget_add_css_class(lbl_hdr_av, "matrix-header-proc");
    gtk_grid_attach(GTK_GRID(w->grid_vectors), lbl_hdr_av, 0, 1, 1, 1);

    for (int j = 0; j < m; j++) {
        char r_name[16];
        snprintf(r_name, sizeof(r_name), "R%d", j);

        /* Total spin */
        w->spin_total[j] = gtk_spin_button_new_with_range(0, BANKER_MAX_VALUE, 1);
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(w->spin_total[j]), w->model->draft_state.total[j]);
        g_object_set_data(G_OBJECT(w->spin_total[j]), "res", GINT_TO_POINTER(j));
        g_signal_connect(w->spin_total[j], "value-changed", G_CALLBACK(on_total_cell_changed), w);
        gtk_grid_attach(GTK_GRID(w->grid_vectors), w->spin_total[j], j + 1, 0, 1, 1);

        /* Available spin */
        w->spin_available[j] = gtk_spin_button_new_with_range(0, BANKER_MAX_VALUE, 1);
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(w->spin_available[j]), w->model->draft_state.available[j]);
        g_object_set_data(G_OBJECT(w->spin_available[j]), "res", GINT_TO_POINTER(j));
        g_signal_connect(w->spin_available[j], "value-changed", G_CALLBACK(on_available_cell_changed), w);
        gtk_grid_attach(GTK_GRID(w->grid_vectors), w->spin_available[j], j + 1, 1, 1, 1);
    }

    /* ---- 2. Helper Lambda / Rebuilder for Allocation Grid ---- */
    while (gtk_widget_get_first_child(w->grid_alloc)) {
        gtk_grid_remove_row(GTK_GRID(w->grid_alloc), 0);
    }
    for (int j = 0; j < m; j++) {
        char r_name[16];
        snprintf(r_name, sizeof(r_name), "R%d", j);
        GtkWidget *hdr = gtk_label_new(r_name);
        gtk_widget_add_css_class(hdr, "matrix-header-res");
        gtk_grid_attach(GTK_GRID(w->grid_alloc), hdr, j + 1, 0, 1, 1);
    }
    for (int i = 0; i < n; i++) {
        char p_name[16];
        snprintf(p_name, sizeof(p_name), "P%d", i);
        GtkWidget *hdr = gtk_label_new(p_name);
        gtk_widget_add_css_class(hdr, "matrix-header-proc");
        gtk_grid_attach(GTK_GRID(w->grid_alloc), hdr, 0, i + 1, 1, 1);

        for (int j = 0; j < m; j++) {
            w->spin_alloc[i][j] = gtk_spin_button_new_with_range(0, BANKER_MAX_VALUE, 1);
            gtk_spin_button_set_value(GTK_SPIN_BUTTON(w->spin_alloc[i][j]), w->model->draft_state.alloc[i][j]);
            g_object_set_data(G_OBJECT(w->spin_alloc[i][j]), "proc", GINT_TO_POINTER(i));
            g_object_set_data(G_OBJECT(w->spin_alloc[i][j]), "res", GINT_TO_POINTER(j));
            g_signal_connect(w->spin_alloc[i][j], "value-changed", G_CALLBACK(on_alloc_cell_changed), w);
            gtk_grid_attach(GTK_GRID(w->grid_alloc), w->spin_alloc[i][j], j + 1, i + 1, 1, 1);
        }
    }

    /* ---- 3. Helper Lambda / Rebuilder for Maximum Grid ---- */
    while (gtk_widget_get_first_child(w->grid_max)) {
        gtk_grid_remove_row(GTK_GRID(w->grid_max), 0);
    }
    for (int j = 0; j < m; j++) {
        char r_name[16];
        snprintf(r_name, sizeof(r_name), "R%d", j);
        GtkWidget *hdr = gtk_label_new(r_name);
        gtk_widget_add_css_class(hdr, "matrix-header-res");
        gtk_grid_attach(GTK_GRID(w->grid_max), hdr, j + 1, 0, 1, 1);
    }
    for (int i = 0; i < n; i++) {
        char p_name[16];
        snprintf(p_name, sizeof(p_name), "P%d", i);
        GtkWidget *hdr = gtk_label_new(p_name);
        gtk_widget_add_css_class(hdr, "matrix-header-proc");
        gtk_grid_attach(GTK_GRID(w->grid_max), hdr, 0, i + 1, 1, 1);

        for (int j = 0; j < m; j++) {
            w->spin_max[i][j] = gtk_spin_button_new_with_range(0, BANKER_MAX_VALUE, 1);
            gtk_spin_button_set_value(GTK_SPIN_BUTTON(w->spin_max[i][j]), w->model->draft_state.max[i][j]);
            g_object_set_data(G_OBJECT(w->spin_max[i][j]), "proc", GINT_TO_POINTER(i));
            g_object_set_data(G_OBJECT(w->spin_max[i][j]), "res", GINT_TO_POINTER(j));
            g_signal_connect(w->spin_max[i][j], "value-changed", G_CALLBACK(on_max_cell_changed), w);
            gtk_grid_attach(GTK_GRID(w->grid_max), w->spin_max[i][j], j + 1, i + 1, 1, 1);
        }
    }

    /* ---- 4. Helper Lambda / Rebuilder for Need Grid (READ-ONLY) ---- */
    while (gtk_widget_get_first_child(w->grid_need)) {
        gtk_grid_remove_row(GTK_GRID(w->grid_need), 0);
    }
    for (int j = 0; j < m; j++) {
        char r_name[16];
        snprintf(r_name, sizeof(r_name), "R%d", j);
        GtkWidget *hdr = gtk_label_new(r_name);
        gtk_widget_add_css_class(hdr, "matrix-header-res");
        gtk_grid_attach(GTK_GRID(w->grid_need), hdr, j + 1, 0, 1, 1);
    }
    for (int i = 0; i < n; i++) {
        char p_name[16];
        snprintf(p_name, sizeof(p_name), "P%d", i);
        GtkWidget *hdr = gtk_label_new(p_name);
        gtk_widget_add_css_class(hdr, "matrix-header-proc");
        gtk_grid_attach(GTK_GRID(w->grid_need), hdr, 0, i + 1, 1, 1);

        for (int j = 0; j < m; j++) {
            w->label_need[i][j] = gtk_label_new("-");
            gtk_widget_add_css_class(w->label_need[i][j], "need-cell");
            gtk_grid_attach(GTK_GRID(w->grid_need), w->label_need[i][j], j + 1, i + 1, 1, 1);
        }
    }

    w->updating_ui = false;
    update_need_display(w);
    update_validation_display(w);
}

static void on_dimension_spin_changed(GtkSpinButton *spin, gpointer user_data) {
    (void)spin;
    ResourcesWidgets *w = (ResourcesWidgets *)user_data;
    if (w->updating_ui) return;

    int n = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(w->spin_n));
    int m = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(w->spin_m));

    gui_model_set_draft_dimensions(w->model, n, m);
    rebuild_matrix_grids(w);
}

static void on_auto_calc_available(GtkWidget *btn, gpointer user_data) {
    (void)btn;
    ResourcesWidgets *w = (ResourcesWidgets *)user_data;
    gui_model_auto_calculate_draft_available(w->model);

    w->updating_ui = true;
    for (int j = 0; j < w->model->draft_state.m; j++) {
        if (w->spin_available[j]) {
            gtk_spin_button_set_value(GTK_SPIN_BUTTON(w->spin_available[j]),
                                      w->model->draft_state.available[j]);
        }
    }
    w->updating_ui = false;
    update_validation_display(w);
}

static void on_commit_clicked(GtkWidget *btn, gpointer user_data) {
    (void)btn;
    ResourcesWidgets *w = (ResourcesWidgets *)user_data;
    if (gui_model_commit_draft(w->model)) {
        char buf[64];
        snprintf(buf, sizeof(buf), "COMMITTED (v%lu)", (unsigned long)w->model->state_version);
        gtk_label_set_text(GTK_LABEL(w->badge_status), buf);
        gtk_widget_remove_css_class(w->badge_status, "badge-rose");
        gtk_widget_add_css_class(w->badge_status, "badge-emerald");
    }
}

static void on_discard_clicked(GtkWidget *btn, gpointer user_data) {
    (void)btn;
    ResourcesWidgets *w = (ResourcesWidgets *)user_data;
    gui_model_init_draft_from_current(w->model);
    w->updating_ui = true;
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(w->spin_n), w->model->draft_state.n);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(w->spin_m), w->model->draft_state.m);
    w->updating_ui = false;
    rebuild_matrix_grids(w);
}

static void on_reset_baseline_clicked(GtkWidget *btn, gpointer user_data) {
    (void)btn;
    ResourcesWidgets *w = (ResourcesWidgets *)user_data;
    if (gui_model_reset_to_original(w->model)) {
        w->updating_ui = true;
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(w->spin_n), w->model->draft_state.n);
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(w->spin_m), w->model->draft_state.m);
        w->updating_ui = false;
        rebuild_matrix_grids(w);
    }
}

static void on_scenario_file_response(GtkNativeDialog *dialog, int response, gpointer user_data) {
    ResourcesWidgets *w = (ResourcesWidgets *)user_data;
    if (response == GTK_RESPONSE_ACCEPT) {
        GtkFileChooser *chooser = GTK_FILE_CHOOSER(dialog);
        GFile *file = gtk_file_chooser_get_file(chooser);
        if (file) {
            char *path = g_file_get_path(file);
            if (path) {
                char err[256] = {0};
                if (!gui_model_load_scenario(w->model, path, err, sizeof(err))) {
                    gtk_label_set_text(GTK_LABEL(w->banner_label), err);
                    gtk_widget_remove_css_class(w->banner_box, "valid-banner");
                    gtk_widget_add_css_class(w->banner_box, "error-banner");
                } else {
                    w->updating_ui = true;
                    gtk_spin_button_set_value(GTK_SPIN_BUTTON(w->spin_n), w->model->draft_state.n);
                    gtk_spin_button_set_value(GTK_SPIN_BUTTON(w->spin_m), w->model->draft_state.m);
                    w->updating_ui = false;
                    rebuild_matrix_grids(w);
                }
                g_free(path);
            }
            g_object_unref(file);
        }
    }
    g_object_unref(dialog);
}

static void on_open_scenario_dialog(GtkWidget *btn, gpointer user_data) {
    (void)btn;
    ResourcesWidgets *w = (ResourcesWidgets *)user_data;

    GtkFileChooserNative *native = gtk_file_chooser_native_new(
        "Open ResourceGuard Scenario File",
        GTK_WINDOW(w->window),
        GTK_FILE_CHOOSER_ACTION_OPEN,
        "_Open",
        "_Cancel"
    );

    GtkFileFilter *filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "Scenario text files (*.txt)");
    gtk_file_filter_add_pattern(filter, "*.txt");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(native), filter);

    g_signal_connect(native, "response", G_CALLBACK(on_scenario_file_response), w);
    gtk_native_dialog_show(GTK_NATIVE_DIALOG(native));
}

static void on_load_preset(GtkWidget *btn, gpointer user_data) {
    ResourcesWidgets *w = (ResourcesWidgets *)user_data;
    const char *preset_path = (const char *)g_object_get_data(G_OBJECT(btn), "path");
    if (!preset_path) return;

    char err[256] = {0};
    if (gui_model_load_scenario(w->model, preset_path, err, sizeof(err))) {
        w->updating_ui = true;
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(w->spin_n), w->model->draft_state.n);
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(w->spin_m), w->model->draft_state.m);
        w->updating_ui = false;
        rebuild_matrix_grids(w);
    } else {
        /* Preserve current committed state, display error without silent fallback */
        gtk_label_set_text(GTK_LABEL(w->banner_label), err);
        gtk_widget_remove_css_class(w->banner_box, "valid-banner");
        gtk_widget_add_css_class(w->banner_box, "error-banner");
    }
}

static void on_resources_map(GtkWidget *widget, gpointer user_data) {
    (void)widget;
    ResourcesWidgets *w = (ResourcesWidgets *)user_data;
    /* Preserve uncommitted draft across page navigation; do not reset from current */
    w->updating_ui = true;
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(w->spin_n), w->model->draft_state.n);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(w->spin_m), w->model->draft_state.m);
    w->updating_ui = false;
    rebuild_matrix_grids(w);
}

static void on_resources_model_changed(GuiModel *model, void *user_data) {
    (void)model;
    ResourcesWidgets *w = (ResourcesWidgets *)user_data;
    if (!w || !w->model) return;
    w->updating_ui = true;
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(w->spin_n), w->model->draft_state.n);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(w->spin_m), w->model->draft_state.m);
    w->updating_ui = false;
    rebuild_matrix_grids(w);
}

static void on_resources_free(gpointer data) {
    ResourcesWidgets *w = (ResourcesWidgets *)data;
    if (w) {
        if (w->model) {
            gui_model_remove_observer(w->model, on_resources_model_changed, w);
            w->model = NULL;
        }
        g_free(w);
    }
}

static void on_resources_destroy(GtkWidget *widget, gpointer user_data) {
    (void)widget;
    ResourcesWidgets *w = (ResourcesWidgets *)user_data;
    if (w && w->model) {
        gui_model_remove_observer(w->model, on_resources_model_changed, w);
        w->model = NULL;
    }
}

GtkWidget *gui_page_resources_create(GuiModel *model, GtkWidget *window) {
    ResourcesWidgets *w = g_new0(ResourcesWidgets, 1);
    w->model = model;
    w->window = window;

    GtkWidget *page_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    g_object_set_data(G_OBJECT(page_box), "widgets", w);
    g_signal_connect(page_box, "map", G_CALLBACK(on_resources_map), w);
    g_signal_connect(page_box, "destroy", G_CALLBACK(on_resources_destroy), w);
    gui_model_add_observer(model, on_resources_model_changed, w);

    /* Header Card */
    GtkWidget *header_card = create_card("System Resources & Allocation Matrices",
                                         "Edit system dimensions, Total capacity, Allocation matrix, and Maximum demand. Need is automatically derived.");
    GtkWidget *hdr_btn_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);

    GtkWidget *btn_open = gtk_button_new_with_label("Open Scenario File...");
    gtk_widget_add_css_class(btn_open, "primary");
    g_signal_connect(btn_open, "clicked", G_CALLBACK(on_open_scenario_dialog), w);
    gtk_box_append(GTK_BOX(hdr_btn_box), btn_open);

    GtkWidget *btn_p1 = gtk_button_new_with_label("Preset: demo.txt (Safe)");
    g_object_set_data(G_OBJECT(btn_p1), "path", (gpointer)"tests/scenarios/demo.txt");
    g_signal_connect(btn_p1, "clicked", G_CALLBACK(on_load_preset), w);
    gtk_box_append(GTK_BOX(hdr_btn_box), btn_p1);

    GtkWidget *btn_p2 = gtk_button_new_with_label("Preset: unsafe_initial.txt (Unsafe)");
    g_object_set_data(G_OBJECT(btn_p2), "path", (gpointer)"tests/scenarios/unsafe_initial.txt");
    g_signal_connect(btn_p2, "clicked", G_CALLBACK(on_load_preset), w);
    gtk_box_append(GTK_BOX(hdr_btn_box), btn_p2);

    GtkWidget *btn_rst = gtk_button_new_with_label("Reset to Baseline");
    g_signal_connect(btn_rst, "clicked", G_CALLBACK(on_reset_baseline_clicked), w);
    gtk_box_append(GTK_BOX(hdr_btn_box), btn_rst);

    GtkWidget *btn_disc = gtk_button_new_with_label("Discard Draft");
    g_signal_connect(btn_disc, "clicked", G_CALLBACK(on_discard_clicked), w);
    gtk_box_append(GTK_BOX(hdr_btn_box), btn_disc);

    w->badge_status = create_badge("COMMITTED (v1)", "badge-emerald");
    gtk_box_append(GTK_BOX(hdr_btn_box), w->badge_status);

    gtk_box_append(GTK_BOX(header_card), hdr_btn_box);
    gtk_box_append(GTK_BOX(page_box), header_card);

    /* Dimensions & Global Vectors Card */
    GtkWidget *dim_card = create_card("System Dimensions & Global Capacity Vectors",
                                      "Configure number of processes (1-16), resource types (1-8), Total capacity, and Available unallocated instances.");

    GtkWidget *dim_controls_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 20);

    GtkWidget *box_n = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *lbl_n = gtk_label_new("Processes (n):");
    w->spin_n = gtk_spin_button_new_with_range(1, BANKER_MAX_PROCS, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(w->spin_n), model->draft_state.n);
    g_signal_connect(w->spin_n, "value-changed", G_CALLBACK(on_dimension_spin_changed), w);
    gtk_box_append(GTK_BOX(box_n), lbl_n);
    gtk_box_append(GTK_BOX(box_n), w->spin_n);
    gtk_box_append(GTK_BOX(dim_controls_box), box_n);

    GtkWidget *box_m = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *lbl_m = gtk_label_new("Resource Types (m):");
    w->spin_m = gtk_spin_button_new_with_range(1, BANKER_MAX_RES, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(w->spin_m), model->draft_state.m);
    g_signal_connect(w->spin_m, "value-changed", G_CALLBACK(on_dimension_spin_changed), w);
    gtk_box_append(GTK_BOX(box_m), lbl_m);
    gtk_box_append(GTK_BOX(box_m), w->spin_m);
    gtk_box_append(GTK_BOX(dim_controls_box), box_m);

    GtkWidget *btn_auto_avail = gtk_button_new_with_label("Auto-Calculate Available from Conservation");
    g_signal_connect(btn_auto_avail, "clicked", G_CALLBACK(on_auto_calc_available), w);
    gtk_box_append(GTK_BOX(dim_controls_box), btn_auto_avail);

    gtk_box_append(GTK_BOX(dim_card), dim_controls_box);

    w->grid_vectors = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(w->grid_vectors), 8);
    gtk_grid_set_column_spacing(GTK_GRID(w->grid_vectors), 8);
    gtk_box_append(GTK_BOX(dim_card), w->grid_vectors);

    gtk_box_append(GTK_BOX(page_box), dim_card);

    /* Allocation Matrix Card */
    GtkWidget *alloc_card = create_card("Allocation Matrix (Alloc[i][j])",
                                        "Instances of each resource currently allocated to each process.");
    w->grid_alloc = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(w->grid_alloc), 6);
    gtk_grid_set_column_spacing(GTK_GRID(w->grid_alloc), 6);
    gtk_box_append(GTK_BOX(alloc_card), w->grid_alloc);
    gtk_box_append(GTK_BOX(page_box), alloc_card);

    /* Maximum Demand Matrix Card */
    GtkWidget *max_card = create_card("Maximum Demand Matrix (Max[i][j])",
                                      "Upper bound on instances each process may declare and request throughout its execution.");
    w->grid_max = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(w->grid_max), 6);
    gtk_grid_set_column_spacing(GTK_GRID(w->grid_max), 6);
    gtk_box_append(GTK_BOX(max_card), w->grid_max);
    gtk_box_append(GTK_BOX(page_box), max_card);

    /* Need Matrix Card (READ-ONLY) */
    GtkWidget *need_card = create_card("Derived Need Matrix (Need[i][j] = Max[i][j] - Alloc[i][j]) [READ-ONLY]",
                                       "Remaining instances each process may request. Automatically recalculated from Max and Alloc; direct editing prohibited.");
    w->grid_need = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(w->grid_need), 6);
    gtk_grid_set_column_spacing(GTK_GRID(w->grid_need), 6);
    gtk_box_append(GTK_BOX(need_card), w->grid_need);
    gtk_box_append(GTK_BOX(page_box), need_card);

    /* Validation & Commit Banner */
    GtkWidget *val_card = create_card("Draft State Validation & Commit",
                                      "Live checks ensure dimension bounds, non-negativity, Alloc <= Max, Max <= Total, and Available + Sum(Alloc) == Total.");

    w->banner_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_add_css_class(w->banner_box, "valid-banner");
    w->banner_label = gtk_label_new("Configuration is valid.");
    gtk_label_set_xalign(GTK_LABEL(w->banner_label), 0.0f);
    gtk_label_set_wrap(GTK_LABEL(w->banner_label), TRUE);
    gtk_box_append(GTK_BOX(w->banner_box), w->banner_label);
    gtk_box_append(GTK_BOX(val_card), w->banner_box);

    GtkWidget *commit_btn_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    w->btn_commit = gtk_button_new_with_label("Commit Configuration to Active State");
    gtk_widget_add_css_class(w->btn_commit, "success");
    g_signal_connect(w->btn_commit, "clicked", G_CALLBACK(on_commit_clicked), w);

    w->btn_discard = gtk_button_new_with_label("Discard Draft Changes");
    g_signal_connect(w->btn_discard, "clicked", G_CALLBACK(on_discard_clicked), w);

    gtk_box_append(GTK_BOX(commit_btn_box), w->btn_commit);
    gtk_box_append(GTK_BOX(commit_btn_box), w->btn_discard);
    gtk_box_append(GTK_BOX(val_card), commit_btn_box);

    gtk_box_append(GTK_BOX(page_box), val_card);

    /* Initial build */
    rebuild_matrix_grids(w);

    GtkWidget *scrolled = wrap_in_scrolled_window(page_box);
    g_object_set_data_full(G_OBJECT(scrolled), "resources_widgets", w, on_resources_free);
    return scrolled;
}

/* ------------------------------------------------------------------ */
/*  5. Why-not Intelligence Page (Placeholder - Milestone 6)          */
/* ------------------------------------------------------------------ */

GtkWidget *gui_page_whynot_create(void) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    GtkWidget *card = create_card("Why-not Diagnostic Intelligence",
                                  "Root-cause analysis of rejected requests and safe-region geometry.");
    gtk_box_append(GTK_BOX(card), create_placeholder_banner("Safe-Region Geometric Map & Manhattan Distance", "Milestone 6"));
    gtk_box_append(GTK_BOX(box), card);
    return wrap_in_scrolled_window(box);
}

/* ------------------------------------------------------------------ */
/*  6. Concurrency Lab Page (Placeholder - Milestone 7)                */
/* ------------------------------------------------------------------ */

GtkWidget *gui_page_concurrency_create(void) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    GtkWidget *card = create_card("Concurrency Lab: Multithreaded Race Simulation",
                                  "Demonstrates race-condition vulnerability and atomic mutex protection.");
    gtk_box_append(GTK_BOX(card), create_placeholder_banner("Live Pthread TOCTOU vs Safe Mode Runner", "Milestone 7"));
    gtk_box_append(GTK_BOX(box), card);
    return wrap_in_scrolled_window(box);
}

/* ------------------------------------------------------------------ */
/*  7. Event History Page (Placeholder - Milestone 7)                  */
/* ------------------------------------------------------------------ */

GtkWidget *gui_page_history_create(void) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    GtkWidget *card = create_card("Simulation Event Log & Auditing",
                                  "Ordered event stream with microsecond timestamps and thread IDs.");
    gtk_box_append(GTK_BOX(card), create_placeholder_banner("Event Log Stream Table", "Milestone 7"));
    gtk_box_append(GTK_BOX(box), card);
    return wrap_in_scrolled_window(box);
}
