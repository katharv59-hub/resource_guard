/* ResourceGuard GTK4 Why-not Intelligence View — implementation.
 * Exhaustive request-space diagnostic exploration, Manhattan distance metrics,
 * exact algorithmic denial safety traces with resource deficits, and actionable safe alternatives. */

#include "whynot_view.h"
#include "window.h"
#include "request_view.h"
#include "style.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

WhynotWidgets *gui_whynot_view_get_widgets(GtkWidget *page_widget) {
    if (!page_widget) return NULL;
    WhynotWidgets *w = (WhynotWidgets *)g_object_get_data(G_OBJECT(page_widget), "whynot_widgets");
    if (!w && GTK_IS_SCROLLED_WINDOW(page_widget)) {
        GtkWidget *child = gtk_scrolled_window_get_child(GTK_SCROLLED_WINDOW(page_widget));
        if (child) {
            w = (WhynotWidgets *)g_object_get_data(G_OBJECT(child), "whynot_widgets");
        }
    }
    return w;
}

static void format_vec(const int vec[], int m, char *out, size_t out_len) {
    if (!out || out_len == 0) return;
    out[0] = '\0';
    size_t pos = 0;
    int written = snprintf(out, out_len, "[ ");
    if (written > 0 && (size_t)written < out_len) {
        pos += (size_t)written;
    }
    for (int j = 0; j < m && pos + 1 < out_len; j++) {
        written = snprintf(out + pos, out_len - pos, "%d%s", vec[j], (j + 1 < m ? ", " : " "));
        if (written > 0 && pos + (size_t)written < out_len) {
            pos += (size_t)written;
        } else {
            pos = out_len - 1;
            break;
        }
    }
    if (pos + 2 < out_len) {
        snprintf(out + pos, out_len - pos, "]");
    }
}

static GtkWidget *make_card(const char *title, const char *subtitle) {
    GtkWidget *card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
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

static GtkWidget *make_badge(const char *text, const char *css_class) {
    GtkWidget *badge = gtk_label_new(text);
    gtk_widget_add_css_class(badge, "badge");
    if (css_class) gtk_widget_add_css_class(badge, css_class);
    return badge;
}

static GtkWidget *make_metric_tile(const char *title, GtkWidget **out_value_lbl) {
    GtkWidget *tile = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_set_size_request(tile, 180, -1);
    gtk_widget_add_css_class(tile, "card");

    GtkWidget *lbl_t = gtk_label_new(title);
    gtk_widget_add_css_class(lbl_t, "card-subtitle");
    gtk_label_set_xalign(GTK_LABEL(lbl_t), 0.0f);
    gtk_box_append(GTK_BOX(tile), lbl_t);

    GtkWidget *lbl_v = gtk_label_new("—");
    gtk_widget_add_css_class(lbl_v, "matrix-header-proc");
    gtk_label_set_xalign(GTK_LABEL(lbl_v), 0.0f);
    gtk_box_append(GTK_BOX(tile), lbl_v);

    if (out_value_lbl) *out_value_lbl = lbl_v;
    return tile;
}

static void clear_box_children(GtkWidget *box) {
    if (!box) return;
    GtkWidget *child = gtk_widget_get_first_child(box);
    while (child) {
        GtkWidget *next = gtk_widget_get_next_sibling(child);
        gtk_box_remove(GTK_BOX(box), child);
        child = next;
    }
}

static void whynot_refresh(WhynotWidgets *w) {
    if (!w || !w->model) return;

    if (!w->model->is_loaded || !gui_model_has_why_not(w->model)) {
        gtk_widget_set_visible(w->empty_box, TRUE);
        gtk_widget_set_visible(w->content_box, FALSE);
        return;
    }

    gtk_widget_set_visible(w->empty_box, FALSE);
    gtk_widget_set_visible(w->content_box, TRUE);

    /* Retrieve context and ensure analysis has been run */
    GuiWhyNotContext ctx;
    if (!gui_model_get_why_not_context(w->model, &ctx)) {
        gtk_widget_set_visible(w->empty_box, TRUE);
        gtk_widget_set_visible(w->content_box, FALSE);
        return;
    }

    if (!ctx.analyzed) {
        gui_model_run_why_not(w->model, NULL, NULL);
        gui_model_get_why_not_context(w->model, &ctx);
    }

    /* 1. Header Badges */
    char buf[512];
    snprintf(buf, sizeof(buf), "P%d", ctx.pid);
    gtk_label_set_text(GTK_LABEL(w->badge_proc), buf);

    char req_str[128];
    format_vec(ctx.request, ctx.snapshot_state.m, req_str, sizeof(req_str));
    snprintf(buf, sizeof(buf), "Request %s", req_str);
    gtk_label_set_text(GTK_LABEL(w->badge_req), buf);

    snprintf(buf, sizeof(buf), "Snapshot: v%lu", (unsigned long)ctx.state_version);
    gtk_label_set_text(GTK_LABEL(w->badge_version), buf);

    bool is_stale = gui_model_is_why_not_stale(w->model);
    bool has_draft = gui_model_has_uncommitted_draft(w->model);

    if (ctx.status == WHY_OK) {
        if (is_stale) {
            gtk_label_set_text(GTK_LABEL(w->badge_status), "COMPLETE (STALE)");
            gtk_widget_remove_css_class(w->badge_status, "badge-emerald");
            gtk_widget_remove_css_class(w->badge_status, "badge-rose");
            gtk_widget_add_css_class(w->badge_status, "badge-amber");
        } else {
            gtk_label_set_text(GTK_LABEL(w->badge_status), "COMPLETE");
            gtk_widget_remove_css_class(w->badge_status, "badge-amber");
            gtk_widget_remove_css_class(w->badge_status, "badge-rose");
            gtk_widget_add_css_class(w->badge_status, "badge-emerald");
        }
    } else if (ctx.status == WHY_ERR_SEARCH_SPACE_TOO_LARGE) {
        gtk_label_set_text(GTK_LABEL(w->badge_status), "OVERSIZED SPACE");
        gtk_widget_remove_css_class(w->badge_status, "badge-emerald");
        gtk_widget_remove_css_class(w->badge_status, "badge-amber");
        gtk_widget_add_css_class(w->badge_status, "badge-rose");
    } else {
        gtk_label_set_text(GTK_LABEL(w->badge_status), "ANALYSIS ERROR");
        gtk_widget_remove_css_class(w->badge_status, "badge-emerald");
        gtk_widget_remove_css_class(w->badge_status, "badge-amber");
        gtk_widget_add_css_class(w->badge_status, "badge-rose");
    }

    /* 2. Warning Banners */
    if (is_stale) {
        gtk_widget_set_visible(w->banner_stale, TRUE);
        snprintf(buf, sizeof(buf),
                 "Historical Diagnostic: Captured at State v%lu, but Current State is v%lu. "
                 "The system state has evolved since this denial was recorded. "
                 "Click 'Re-evaluate Against Current State' below to test this request against the live system configuration.",
                 (unsigned long)ctx.state_version, (unsigned long)w->model->state_version);
        gtk_label_set_text(GTK_LABEL(w->label_stale), buf);
    } else {
        gtk_widget_set_visible(w->banner_stale, FALSE);
    }

    if (has_draft) {
        gtk_widget_set_visible(w->banner_draft, TRUE);
        gtk_label_set_text(GTK_LABEL(w->label_draft),
                           "Uncommitted Draft in System Resources. Re-evaluation against current state is disabled "
                           "until draft modifications in System Resources are committed or discarded.");
        gtk_widget_set_sensitive(w->btn_reevaluate, FALSE);
    } else {
        gtk_widget_set_visible(w->banner_draft, FALSE);
        gtk_widget_set_sensitive(w->btn_reevaluate, TRUE);
    }

    /* Handle Oversized Space Error: omit partial or misleading results */
    if (ctx.status == WHY_ERR_SEARCH_SPACE_TOO_LARGE) {
        gtk_widget_set_visible(w->banner_oversized, TRUE);
        gtk_label_set_text(GTK_LABEL(w->label_oversized),
                           "Candidate search space exceeds the exploration limit of 100,000 vectors. "
                           "Partial or heuristic results are omitted to maintain formal analytical guarantees.");
        gtk_widget_set_visible(w->card_summary, FALSE);
        gtk_widget_set_visible(w->card_trace, FALSE);
        gtk_widget_set_visible(w->card_alternatives, FALSE);
        return;
    }

    gtk_widget_set_visible(w->banner_oversized, FALSE);
    gtk_widget_set_visible(w->card_summary, TRUE);
    gtk_widget_set_visible(w->card_trace, TRUE);
    gtk_widget_set_visible(w->card_alternatives, TRUE);

    /* 3. Card 1: Diagnostic Exploration Summary */
    snprintf(buf, sizeof(buf), "%llu", ctx.report.candidate_count);
    gtk_label_set_text(GTK_LABEL(w->label_total_candidates), buf);

    double safe_pct = ctx.report.candidate_count > 0 ?
        (100.0 * (double)ctx.report.safe_count / (double)ctx.report.candidate_count) : 0.0;
    snprintf(buf, sizeof(buf), "%llu (%.1f%%)", ctx.report.safe_count, safe_pct);
    gtk_label_set_text(GTK_LABEL(w->label_safe_candidates), buf);

    double unsafe_pct = ctx.report.candidate_count > 0 ?
        (100.0 * (double)ctx.report.unsafe_count / (double)ctx.report.candidate_count) : 0.0;
    snprintf(buf, sizeof(buf), "%llu (%.1f%%)", ctx.report.unsafe_count, unsafe_pct);
    gtk_label_set_text(GTK_LABEL(w->label_unsafe_candidates), buf);

    char bounds_str[128];
    format_vec(ctx.report.search_bound, ctx.snapshot_state.m, bounds_str, sizeof(bounds_str));
    gtk_label_set_text(GTK_LABEL(w->label_search_bounds), bounds_str);

    if (ctx.report.has_nearest_safe_request) {
        char near_str[128];
        format_vec(ctx.report.nearest_safe_request, ctx.snapshot_state.m, near_str, sizeof(near_str));
        gtk_label_set_text(GTK_LABEL(w->label_nearest_safe), near_str);

        snprintf(buf, sizeof(buf), "%llu units", ctx.report.nearest_distance);
        gtk_label_set_text(GTK_LABEL(w->label_nearest_distance), buf);
    } else {
        gtk_label_set_text(GTK_LABEL(w->label_nearest_safe), "None");
        gtk_label_set_text(GTK_LABEL(w->label_nearest_distance), "N/A");
    }

    char comp_str[128];
    format_vec(ctx.report.componentwise_max, ctx.snapshot_state.m, comp_str, sizeof(comp_str));
    gtk_label_set_text(GTK_LABEL(w->label_compwise_max), comp_str);

    if (ctx.report.componentwise_max_is_safe) {
        gtk_label_set_text(GTK_LABEL(w->badge_compwise_safe), "JOINTLY SAFE");
        gtk_widget_remove_css_class(w->badge_compwise_safe, "badge-rose");
        gtk_widget_add_css_class(w->badge_compwise_safe, "badge-emerald");
    } else {
        gtk_label_set_text(GTK_LABEL(w->badge_compwise_safe), "NOT JOINTLY SAFE");
        gtk_widget_remove_css_class(w->badge_compwise_safe, "badge-emerald");
        gtk_widget_add_css_class(w->badge_compwise_safe, "badge-rose");
    }

    /* 4. Card 2: Denial Safety Trace & Resource Deficits */
    clear_box_children(w->box_trace_steps);
    const Trace *tr = &ctx.report.denial_trace;

    for (int k = 0; k < tr->count; k++) {
        const TraceStep *step = &tr->steps[k];
        char work_str[128];
        format_vec(step->work, ctx.snapshot_state.m, work_str, sizeof(work_str));

        GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
        gtk_widget_add_css_class(row, "card");

        char step_title[64];
        snprintf(step_title, sizeof(step_title), "Step %d", k);
        GtkWidget *lbl_st = gtk_label_new(step_title);
        gtk_widget_add_css_class(lbl_st, "badge");
        gtk_widget_add_css_class(lbl_st, "badge-teal");
        gtk_box_append(GTK_BOX(row), lbl_st);

        char desc_buf[512];
        if (step->picked >= 0) {
            snprintf(desc_buf, sizeof(desc_buf),
                     "Process P%d satisfied Need <= Work %s  ->  Executes and releases its allocation into Work.",
                     step->picked, work_str);
        } else {
            snprintf(desc_buf, sizeof(desc_buf),
                     "ALGORITHMIC STALL DETECTED at Work %s  ->  No remaining unfinished process can proceed.",
                     work_str);
        }
        GtkWidget *lbl_desc = gtk_label_new(desc_buf);
        gtk_label_set_xalign(GTK_LABEL(lbl_desc), 0.0f);
        gtk_box_append(GTK_BOX(row), lbl_desc);

        gtk_box_append(GTK_BOX(w->box_trace_steps), row);
    }

    /* Clear and build blocked processes & deficits list */
    clear_box_children(w->box_blocked_procs);

    /* Construct trial state to compute exact Need and Deficits */
    BankerState trial = ctx.snapshot_state;
    for (int j = 0; j < trial.m; j++) {
        trial.alloc[ctx.pid][j] += ctx.request[j];
        trial.available[j] -= ctx.request[j];
    }

    int last_idx = tr->count > 0 ? tr->count - 1 : 0;
    const TraceStep *last_step = tr->count > 0 ? &tr->steps[last_idx] : NULL;

    for (int i = 0; i < trial.n; i++) {
        bool finished = last_step ? last_step->finish[i] : false;
        if (!finished) {
            GtkWidget *proc_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
            gtk_widget_add_css_class(proc_box, "card");

            char proc_header[128];
            snprintf(proc_header, sizeof(proc_header), "Process P%d (Blocked / Unfinished)", i);
            GtkWidget *lbl_ph = gtk_label_new(proc_header);
            gtk_widget_add_css_class(lbl_ph, "card-title");
            gtk_label_set_xalign(GTK_LABEL(lbl_ph), 0.0f);
            gtk_box_append(GTK_BOX(proc_box), lbl_ph);

            int need_i[BANKER_MAX_RES];
            int deficit_i[BANKER_MAX_RES];
            bool has_deficit = false;
            for (int j = 0; j < trial.m; j++) {
                need_i[j] = trial.max[i][j] - trial.alloc[i][j];
                deficit_i[j] = (need_i[j] > tr->final_work[j]) ? (need_i[j] - tr->final_work[j]) : 0;
                if (deficit_i[j] > 0) has_deficit = true;
            }

            char alloc_i_str[128], need_i_str[128], final_work_str[128], deficit_i_str[128];
            format_vec(trial.alloc[i], trial.m, alloc_i_str, sizeof(alloc_i_str));
            format_vec(need_i, trial.m, need_i_str, sizeof(need_i_str));
            format_vec(tr->final_work, trial.m, final_work_str, sizeof(final_work_str));
            format_vec(deficit_i, trial.m, deficit_i_str, sizeof(deficit_i_str));

            char detail_buf[1024];
            snprintf(detail_buf, sizeof(detail_buf),
                     "Alloc: %s   Need: %s   Final Work: %s   Deficit: %s%s",
                     alloc_i_str, need_i_str, final_work_str, deficit_i_str,
                     has_deficit ? "  [Deficit Prevents Completion]" : "");
            GtkWidget *lbl_detail = gtk_label_new(detail_buf);
            gtk_widget_add_css_class(lbl_detail, "mono-text");
            gtk_label_set_xalign(GTK_LABEL(lbl_detail), 0.0f);
            gtk_box_append(GTK_BOX(proc_box), lbl_detail);

            gtk_box_append(GTK_BOX(w->box_blocked_procs), proc_box);
        }
    }

    /* 5. Card 3: Actionable Safe Alternatives */
    if (ctx.report.has_nearest_safe_request) {
        char near_str[128];
        format_vec(ctx.report.nearest_safe_request, ctx.snapshot_state.m, near_str, sizeof(near_str));
        snprintf(buf, sizeof(buf), "Nearest Safe Alternative Vector: %s", near_str);
        gtk_label_set_text(GTK_LABEL(w->label_alt_nearest), buf);

        snprintf(buf, sizeof(buf), "Manhattan Distance (L1): %llu units", ctx.report.nearest_distance);
        gtk_label_set_text(GTK_LABEL(w->label_alt_distance), buf);

        gtk_widget_set_sensitive(w->btn_load_simulator, TRUE);
    } else {
        gtk_label_set_text(GTK_LABEL(w->label_alt_nearest), "Nearest Safe Alternative: None in bounded region");
        gtk_label_set_text(GTK_LABEL(w->label_alt_distance), "Manhattan Distance (L1): N/A");
        gtk_widget_set_sensitive(w->btn_load_simulator, FALSE);
    }

    char comp_summary[256];
    format_vec(ctx.report.componentwise_max, ctx.snapshot_state.m, comp_str, sizeof(comp_str));
    snprintf(comp_summary, sizeof(comp_summary),
             "Componentwise Maxima: %s (%s). Maximum safe request if each resource dimension is maximized independently.",
             comp_str, ctx.report.componentwise_max_is_safe ? "Jointly Safe" : "Not Jointly Safe");
    gtk_label_set_text(GTK_LABEL(w->label_alt_compwise), comp_summary);
}

/* Event Handlers */
static void on_btn_load_simulator_clicked(GtkWidget *btn, gpointer user_data) {
    (void)btn;
    WhynotWidgets *w = (WhynotWidgets *)user_data;
    if (!w || !w->model || !w->window) return;

    GuiWhyNotContext ctx;
    if (!gui_model_get_why_not_context(w->model, &ctx) || !ctx.report.has_nearest_safe_request) {
        return;
    }

    /* Switch to Request Simulator */
    gui_window_switch_to_page(w->window, "simulator");

    /* Obtain Simulator page and widgets */
    GtkWidget *stack = gui_window_get_stack(w->window);
    if (!stack) return;
    GtkWidget *sim_page = gtk_stack_get_child_by_name(GTK_STACK(stack), "simulator");
    if (!sim_page) return;
    SimulatorWidgets *sim_w = gui_request_view_get_widgets(sim_page);
    if (!sim_w) return;

    /* Populate process and spin controls WITHOUT submitting */
    if (sim_w->dropdown_proc) {
        gtk_drop_down_set_selected(GTK_DROP_DOWN(sim_w->dropdown_proc), (guint)ctx.pid);
    }
    for (int j = 0; j < ctx.snapshot_state.m; j++) {
        if (sim_w->spin_req[j]) {
            gtk_spin_button_set_value(GTK_SPIN_BUTTON(sim_w->spin_req[j]),
                                      (double)ctx.report.nearest_safe_request[j]);
        }
    }

    char near_str[128];
    format_vec(ctx.report.nearest_safe_request, ctx.snapshot_state.m, near_str, sizeof(near_str));
    char status_buf[256];
    snprintf(status_buf, sizeof(status_buf),
             "Nearest safe request %s loaded into Request Simulator. Ready for inspection and submission.",
             near_str);
    gtk_label_set_text(GTK_LABEL(w->label_load_status), status_buf);
}

static void on_btn_reevaluate_clicked(GtkWidget *btn, gpointer user_data) {
    (void)btn;
    WhynotWidgets *w = (WhynotWidgets *)user_data;
    if (!w || !w->model) return;

    if (gui_model_has_uncommitted_draft(w->model)) {
        gtk_label_set_text(GTK_LABEL(w->label_reeval_status),
                           "Cannot re-evaluate while uncommitted draft modifications exist in System Resources.");
        return;
    }

    WhyNotReport rep;
    WhyNotStatus st = gui_model_reevaluate_why_not(w->model, &rep);
    char buf[512];

    switch (st) {
        case WHY_OK:
            w->model->why_context.report = rep;
            w->model->why_context.snapshot_state = w->model->current_state;
            w->model->why_context.state_version = w->model->state_version;
            w->model->why_context.analyzed = true;
            w->model->why_context.status = WHY_OK;
            whynot_refresh(w);
            snprintf(buf, sizeof(buf),
                     "Re-evaluation complete against current state (v%lu): request remains unsafe. Diagnostics refreshed.",
                     (unsigned long)w->model->state_version);
            gtk_label_set_text(GTK_LABEL(w->label_reeval_status), buf);
            break;

        case WHY_ERR_NOT_UNSAFE:
            snprintf(buf, sizeof(buf),
                     "Re-evaluation result: In current state (v%lu), this request is now SAFE! It would be granted immediately.",
                     (unsigned long)w->model->state_version);
            gtk_label_set_text(GTK_LABEL(w->label_reeval_status), buf);
            break;

        case WHY_ERR_UNAVAILABLE:
            snprintf(buf, sizeof(buf),
                     "Re-evaluation result: In current state (v%lu), this request now exceeds Available resources (REQ_WAIT_UNAVAILABLE).",
                     (unsigned long)w->model->state_version);
            gtk_label_set_text(GTK_LABEL(w->label_reeval_status), buf);
            break;

        case WHY_ERR_EXCEEDS_NEED:
            snprintf(buf, sizeof(buf),
                     "Re-evaluation result: In current state (v%lu), this request exceeds the process's remaining Need (REQ_ERR_EXCEEDS_NEED).",
                     (unsigned long)w->model->state_version);
            gtk_label_set_text(GTK_LABEL(w->label_reeval_status), buf);
            break;

        case WHY_ERR_BAD_STATE:
            gtk_label_set_text(GTK_LABEL(w->label_reeval_status),
                               "Re-evaluation blocked: Model state invalid or uncommitted draft present.");
            break;

        case WHY_ERR_SEARCH_SPACE_TOO_LARGE:
            gtk_label_set_text(GTK_LABEL(w->label_reeval_status),
                               "Re-evaluation result: Candidate search space exceeds the 100,000 limit.");
            break;

        default:
            snprintf(buf, sizeof(buf), "Re-evaluation error: %s (%d)", banker_why_status_str(st), st);
            gtk_label_set_text(GTK_LABEL(w->label_reeval_status), buf);
            break;
    }
}

static void on_btn_empty_goto_sim(GtkWidget *btn, gpointer user_data) {
    (void)btn;
    WhynotWidgets *w = (WhynotWidgets *)user_data;
    if (w && w->window) {
        gui_window_switch_to_page(w->window, "simulator");
    }
}

static void on_whynot_model_changed(GuiModel *m, void *user_data) {
    (void)m;
    WhynotWidgets *w = (WhynotWidgets *)user_data;
    whynot_refresh(w);
}

static void on_whynot_map(GtkWidget *widget, gpointer user_data) {
    (void)widget;
    WhynotWidgets *w = (WhynotWidgets *)user_data;
    whynot_refresh(w);
}

static void on_whynot_destroy(GtkWidget *widget, gpointer user_data) {
    (void)widget;
    WhynotWidgets *w = (WhynotWidgets *)user_data;
    if (w && w->model) {
        gui_model_remove_observer(w->model, on_whynot_model_changed, w);
        w->model = NULL;
    }
}

static void on_whynot_free(gpointer data) {
    WhynotWidgets *w = (WhynotWidgets *)data;
    if (w) {
        if (w->model) {
            gui_model_remove_observer(w->model, on_whynot_model_changed, w);
            w->model = NULL;
        }
        g_free(w);
    }
}

GtkWidget *gui_page_whynot_create(GuiModel *model, GtkWidget *window) {
    WhynotWidgets *w = g_new0(WhynotWidgets, 1);
    w->model = model;
    w->window = window;

    GtkWidget *page_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_widget_add_css_class(page_box, "page-container");

    g_signal_connect(page_box, "map", G_CALLBACK(on_whynot_map), w);
    g_signal_connect(page_box, "destroy", G_CALLBACK(on_whynot_destroy), w);
    g_object_set_data(G_OBJECT(page_box), "whynot_widgets", w);

    gui_model_add_observer(model, on_whynot_model_changed, w);

    /* 1. Header Card */
    GtkWidget *header_card = make_card("Why-not Intelligence & Safe Alternative Diagnostics",
                                       "Exhaustive request-space analysis explaining unsafe rejections, algorithmic stalls, and nearest safe vectors.");
    GtkWidget *badge_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    w->badge_proc = make_badge("Process —", "badge-teal");
    w->badge_req = make_badge("Request —", "badge-teal");
    w->badge_version = make_badge("Snapshot: —", "badge-teal");
    w->badge_status = make_badge("NO CONTEXT", "badge-muted");
    gtk_box_append(GTK_BOX(badge_box), w->badge_proc);
    gtk_box_append(GTK_BOX(badge_box), w->badge_req);
    gtk_box_append(GTK_BOX(badge_box), w->badge_version);
    gtk_box_append(GTK_BOX(badge_box), w->badge_status);
    gtk_box_append(GTK_BOX(header_card), badge_box);
    gtk_box_append(GTK_BOX(page_box), header_card);

    /* 2. Empty State Box */
    w->empty_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_add_css_class(w->empty_box, "empty-state-box");
    GtkWidget *empty_title = gtk_label_new("No Rejection Diagnostic Captured");
    gtk_widget_add_css_class(empty_title, "empty-state-title");
    GtkWidget *empty_sub = gtk_label_new(
        "Why-not Intelligence is activated automatically when a process resource request is denied as unsafe in the Request Simulator.\n"
        "Submit an unsafe request (such as P4 requesting [3, 3, 0]) to inspect candidate search bounds, Manhattan distances, and exact algorithmic stall traces.");
    gtk_widget_add_css_class(empty_sub, "empty-state-subtitle");
    GtkWidget *btn_go_sim = gtk_button_new_with_label("Open Request Simulator");
    gtk_widget_add_css_class(btn_go_sim, "primary");
    g_signal_connect(btn_go_sim, "clicked", G_CALLBACK(on_btn_empty_goto_sim), w);

    gtk_box_append(GTK_BOX(w->empty_box), empty_title);
    gtk_box_append(GTK_BOX(w->empty_box), empty_sub);
    gtk_box_append(GTK_BOX(w->empty_box), btn_go_sim);
    gtk_box_append(GTK_BOX(page_box), w->empty_box);

    /* 3. Main Content Box */
    w->content_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_box_append(GTK_BOX(page_box), w->content_box);

    /* Banners */
    w->banner_stale = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_add_css_class(w->banner_stale, "placeholder-banner");
    w->label_stale = gtk_label_new("");
    gtk_label_set_wrap(GTK_LABEL(w->label_stale), TRUE);
    gtk_label_set_xalign(GTK_LABEL(w->label_stale), 0.0f);
    gtk_box_append(GTK_BOX(w->banner_stale), w->label_stale);
    gtk_box_append(GTK_BOX(w->content_box), w->banner_stale);

    w->banner_draft = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_add_css_class(w->banner_draft, "error-banner");
    w->label_draft = gtk_label_new("");
    gtk_label_set_wrap(GTK_LABEL(w->label_draft), TRUE);
    gtk_label_set_xalign(GTK_LABEL(w->label_draft), 0.0f);
    gtk_box_append(GTK_BOX(w->banner_draft), w->label_draft);
    gtk_box_append(GTK_BOX(w->content_box), w->banner_draft);

    w->banner_oversized = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_add_css_class(w->banner_oversized, "error-banner");
    w->label_oversized = gtk_label_new("");
    gtk_label_set_wrap(GTK_LABEL(w->label_oversized), TRUE);
    gtk_label_set_xalign(GTK_LABEL(w->label_oversized), 0.0f);
    gtk_box_append(GTK_BOX(w->banner_oversized), w->label_oversized);
    gtk_box_append(GTK_BOX(w->content_box), w->banner_oversized);

    /* Card 1: Diagnostic Exploration Summary */
    w->card_summary = make_card("Diagnostic Exploration Summary",
                                "Exhaustive candidate enumeration within bounds 0 <= r[j] <= min(Need[j], Available[j]).");
    GtkWidget *grid_metrics = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid_metrics), 12);
    gtk_grid_set_column_spacing(GTK_GRID(grid_metrics), 12);

    GtkWidget *tile_total = make_metric_tile("Total Candidates Explored", &w->label_total_candidates);
    GtkWidget *tile_safe = make_metric_tile("Safe Candidate Vectors", &w->label_safe_candidates);
    GtkWidget *tile_unsafe = make_metric_tile("Unsafe Candidate Vectors", &w->label_unsafe_candidates);
    GtkWidget *tile_bounds = make_metric_tile("Search Upper Bounds", &w->label_search_bounds);
    GtkWidget *tile_near = make_metric_tile("Nearest Safe Request", &w->label_nearest_safe);
    GtkWidget *tile_dist = make_metric_tile("Manhattan Distance (L1)", &w->label_nearest_distance);

    gtk_grid_attach(GTK_GRID(grid_metrics), tile_total, 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid_metrics), tile_safe, 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid_metrics), tile_unsafe, 2, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid_metrics), tile_bounds, 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid_metrics), tile_near, 1, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid_metrics), tile_dist, 2, 1, 1, 1);

    gtk_box_append(GTK_BOX(w->card_summary), grid_metrics);

    /* Componentwise Maximum section */
    GtkWidget *box_comp = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    GtkWidget *lbl_comp_t = gtk_label_new("Componentwise Maximum:");
    gtk_widget_add_css_class(lbl_comp_t, "card-subtitle");
    w->label_compwise_max = gtk_label_new("—");
    gtk_widget_add_css_class(w->label_compwise_max, "matrix-header-res");
    w->badge_compwise_safe = make_badge("—", "badge-muted");

    gtk_box_append(GTK_BOX(box_comp), lbl_comp_t);
    gtk_box_append(GTK_BOX(box_comp), w->label_compwise_max);
    gtk_box_append(GTK_BOX(box_comp), w->badge_compwise_safe);
    gtk_box_append(GTK_BOX(w->card_summary), box_comp);
    gtk_box_append(GTK_BOX(w->content_box), w->card_summary);

    /* Card 2: Denial Safety Trace & Resource Deficits */
    w->card_trace = make_card("Denial Safety Trace & Resource Deficit Analysis",
                              "Step-by-step Work evolution demonstrating why the trial state cannot guarantee completion.");
    
    /* Explanatory banner */
    GtkWidget *box_warn = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_add_css_class(box_warn, "placeholder-banner");
    w->label_stall_explanation = gtk_label_new(
        "ALGORITHMIC STALL DETECTED (UNSAFE STATE):\n"
        "Granting this request would leave available resources insufficient to guarantee process completion under worst-case demands. "
        "IMPORTANT: An unsafe state is NOT an active deadlock; processes may continue executing, but the operating system cannot "
        "guarantee avoidance of future deadlock if processes exercise their full declared maximum demands.");
    gtk_label_set_wrap(GTK_LABEL(w->label_stall_explanation), TRUE);
    gtk_label_set_xalign(GTK_LABEL(w->label_stall_explanation), 0.0f);
    gtk_box_append(GTK_BOX(box_warn), w->label_stall_explanation);
    gtk_box_append(GTK_BOX(w->card_trace), box_warn);

    /* Trace progression steps container */
    GtkWidget *lbl_prog_title = gtk_label_new("Work / Available Progression Steps:");
    gtk_widget_add_css_class(lbl_prog_title, "card-title");
    gtk_label_set_xalign(GTK_LABEL(lbl_prog_title), 0.0f);
    gtk_box_append(GTK_BOX(w->card_trace), lbl_prog_title);

    w->box_trace_steps = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_box_append(GTK_BOX(w->card_trace), w->box_trace_steps);

    /* Blocked processes container */
    GtkWidget *lbl_blocked_title = gtk_label_new("Blocked Processes & Resource Deficits at Algorithmic Stall:");
    gtk_widget_add_css_class(lbl_blocked_title, "card-title");
    gtk_label_set_xalign(GTK_LABEL(lbl_blocked_title), 0.0f);
    gtk_box_append(GTK_BOX(w->card_trace), lbl_blocked_title);

    w->box_blocked_procs = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_box_append(GTK_BOX(w->card_trace), w->box_blocked_procs);
    gtk_box_append(GTK_BOX(w->content_box), w->card_trace);

    /* Card 3: Actionable Safe Alternatives */
    w->card_alternatives = make_card("Actionable Safe Alternatives",
                                     "Discovered safe requests that avoid leaving the system in an unsafe state.");
    w->label_alt_nearest = gtk_label_new("Nearest Safe Alternative: —");
    gtk_label_set_xalign(GTK_LABEL(w->label_alt_nearest), 0.0f);
    gtk_box_append(GTK_BOX(w->card_alternatives), w->label_alt_nearest);

    w->label_alt_distance = gtk_label_new("Manhattan Distance (L1): —");
    gtk_label_set_xalign(GTK_LABEL(w->label_alt_distance), 0.0f);
    gtk_box_append(GTK_BOX(w->card_alternatives), w->label_alt_distance);

    w->label_alt_compwise = gtk_label_new("Componentwise Maxima: —");
    gtk_label_set_wrap(GTK_LABEL(w->label_alt_compwise), TRUE);
    gtk_label_set_xalign(GTK_LABEL(w->label_alt_compwise), 0.0f);
    gtk_box_append(GTK_BOX(w->card_alternatives), w->label_alt_compwise);

    GtkWidget *box_alt_actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    w->btn_load_simulator = gtk_button_new_with_label("Load Nearest Safe Request into Simulator");
    gtk_widget_add_css_class(w->btn_load_simulator, "primary");
    g_signal_connect(w->btn_load_simulator, "clicked", G_CALLBACK(on_btn_load_simulator_clicked), w);
    gtk_box_append(GTK_BOX(box_alt_actions), w->btn_load_simulator);

    w->label_load_status = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(w->label_load_status), 0.0f);
    gtk_box_append(GTK_BOX(box_alt_actions), w->label_load_status);
    gtk_box_append(GTK_BOX(w->card_alternatives), box_alt_actions);
    gtk_box_append(GTK_BOX(w->content_box), w->card_alternatives);

    /* Card 4: Live Re-evaluation */
    w->card_reeval = make_card("Diagnostic Re-evaluation",
                               "Re-evaluate this request vector against the live system state if configuration has evolved.");
    GtkWidget *box_reeval_actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    w->btn_reevaluate = gtk_button_new_with_label("Re-evaluate Against Current State");
    g_signal_connect(w->btn_reevaluate, "clicked", G_CALLBACK(on_btn_reevaluate_clicked), w);
    gtk_box_append(GTK_BOX(box_reeval_actions), w->btn_reevaluate);

    w->label_reeval_status = gtk_label_new("");
    gtk_label_set_wrap(GTK_LABEL(w->label_reeval_status), TRUE);
    gtk_label_set_xalign(GTK_LABEL(w->label_reeval_status), 0.0f);
    gtk_box_append(GTK_BOX(box_reeval_actions), w->label_reeval_status);
    gtk_box_append(GTK_BOX(w->card_reeval), box_reeval_actions);
    gtk_box_append(GTK_BOX(w->content_box), w->card_reeval);

    /* Wrap in scrolled window */
    GtkWidget *scrolled = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
                                   GTK_POLICY_NEVER,
                                   GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled), page_box);

    whynot_refresh(w);
    g_object_set_data_full(G_OBJECT(scrolled), "whynot_widgets", w, on_whynot_free);

    return scrolled;
}
