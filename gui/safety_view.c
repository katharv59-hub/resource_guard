/* ResourceGuard GTK4 Safety Analysis View — implementation.
 * Interactive step-by-step visualizer for Banker's Algorithm safety verification.
 * Renders live Work vectors, Finish arrays, process selection, and stuck iterations. */

#include "safety_view.h"
#include "window.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

SafetyWidgets *gui_safety_view_get_widgets(GtkWidget *page_widget) {
    if (!page_widget) return NULL;
    return (SafetyWidgets *)g_object_get_data(G_OBJECT(page_widget), "safety_widgets");
}

/* Helper: format vector as [ v0, v1, ... ] */
static void format_vector(const int vec[], int m, char *out, size_t out_len) {
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

static GtkWidget *make_badge(const char *text, const char *css_class) {
    GtkWidget *badge = gtk_label_new(text);
    gtk_widget_add_css_class(badge, "badge");
    if (css_class) gtk_widget_add_css_class(badge, css_class);
    return badge;
}

static void safety_recalculate(SafetyWidgets *w) {
    if (!w || !w->model) return;

    if (!w->model->is_loaded) {
        w->has_analysis = false;
        return;
    }

    w->analyzed_version = w->model->state_version;
    memset(&w->trace, 0, sizeof(w->trace));
    w->is_safe = gui_model_is_safe(w->model, w->safe_seq, &w->safe_seq_len, &w->trace);
    w->has_analysis = true;

    /* Clamp or initialize step */
    if (w->trace.count > 0) {
        if (w->current_step >= w->trace.count) {
            w->current_step = w->trace.count - 1;
        } else if (w->current_step < 0) {
            w->current_step = 0;
        }
    } else {
        w->current_step = 0;
    }
}

static void safety_render_step(SafetyWidgets *w) {
    if (!w || !w->has_analysis || w->trace.count == 0) return;

    int idx = w->current_step;
    if (idx < 0) idx = 0;
    if (idx >= w->trace.count) idx = w->trace.count - 1;
    const TraceStep *st = &w->trace.steps[idx];

    /* Update stepper button states */
    gtk_widget_set_sensitive(w->btn_prev, idx > 0);
    gtk_widget_set_sensitive(w->btn_next, idx < w->trace.count - 1);

    char ind_buf[64];
    snprintf(ind_buf, sizeof(ind_buf), "Step %d of %d", idx + 1, w->trace.count);
    gtk_label_set_text(GTK_LABEL(w->label_step_indicator), ind_buf);

    /* Iteration title */
    char title_buf[128];
    if (st->picked >= 0) {
        snprintf(title_buf, sizeof(title_buf), "Iteration %d: Process P%d Selected & Dispatched",
                 idx + 1, st->picked);
    } else {
        snprintf(title_buf, sizeof(title_buf), "Iteration %d: Algorithm Blocked (Unsafe State)",
                 idx + 1);
    }
    gtk_label_set_text(GTK_LABEL(w->label_iter_title), title_buf);

    /* Work vector before step */
    char work_buf[128];
    format_vector(st->work, w->model->current_state.m, work_buf, sizeof(work_buf));
    char work_lbl[256];
    snprintf(work_lbl, sizeof(work_lbl), "Work Vector (Available) Before Pick:  %s", work_buf);
    gtk_label_set_text(GTK_LABEL(w->label_work_before), work_lbl);

    /* Finish indicators */
    /* Clear old children from box_finish_indicators */
    GtkWidget *child = gtk_widget_get_first_child(w->box_finish_indicators);
    while (child) {
        GtkWidget *next = gtk_widget_get_next_sibling(child);
        gtk_box_remove(GTK_BOX(w->box_finish_indicators), child);
        child = next;
    }

    for (int p = 0; p < w->model->current_state.n; p++) {
        char f_buf[32];
        const char *cls = "badge-muted";
        if (st->finish[p]) {
            snprintf(f_buf, sizeof(f_buf), "P%d: Finished", p);
            cls = "badge-emerald";
        } else if (p == st->picked) {
            snprintf(f_buf, sizeof(f_buf), "P%d: Running", p);
            cls = "badge-teal";
        } else {
            snprintf(f_buf, sizeof(f_buf), "P%d: Pending", p);
            cls = "badge-muted";
        }
        GtkWidget *b = make_badge(f_buf, cls);
        gtk_box_append(GTK_BOX(w->box_finish_indicators), b);
    }

    /* Process evaluation details */
    if (st->picked >= 0) {
        gtk_widget_set_visible(w->box_stuck_diagnostic, FALSE);

        int p = st->picked;
        int all_need[BANKER_MAX_PROCS][BANKER_MAX_RES];
        gui_model_get_current_need(w->model, all_need);

        char need_buf[128], alloc_buf[128], after_buf[128];
        format_vector(all_need[p], w->model->current_state.m, need_buf, sizeof(need_buf));
        format_vector(w->model->current_state.alloc[p], w->model->current_state.m, alloc_buf, sizeof(alloc_buf));

        int work_after[BANKER_MAX_RES];
        for (int j = 0; j < w->model->current_state.m; j++) {
            work_after[j] = st->work[j] + w->model->current_state.alloc[p][j];
        }
        format_vector(work_after, w->model->current_state.m, after_buf, sizeof(after_buf));

        char pick_msg[256];
        snprintf(pick_msg, sizeof(pick_msg), "Selected Process: Process P%d satisfies condition (Need <= Work)", p);
        gtk_label_set_text(GTK_LABEL(w->label_process_picked), pick_msg);

        char need_msg[512];
        snprintf(need_msg, sizeof(need_msg), "Remaining Need(P%d): %s <= Work: %s -> TRUE", p, need_buf, work_buf);
        gtk_label_set_text(GTK_LABEL(w->label_need_eval), need_msg);

        char alloc_msg[512];
        snprintf(alloc_msg, sizeof(alloc_msg), "Allocation Reclaimed: %s returned to system pool upon P%d completion", alloc_buf, p);
        gtk_label_set_text(GTK_LABEL(w->label_alloc_returned), alloc_msg);

        char after_msg[512];
        snprintf(after_msg, sizeof(after_msg), "Updated Work (Next Available): %s", after_buf);
        gtk_label_set_text(GTK_LABEL(w->label_work_after), after_msg);

        gtk_widget_set_visible(w->label_process_picked, TRUE);
        gtk_widget_set_visible(w->label_need_eval, TRUE);
        gtk_widget_set_visible(w->label_alloc_returned, TRUE);
        gtk_widget_set_visible(w->label_work_after, TRUE);
    } else {
        /* Stuck iteration: picked == -1 */
        gtk_widget_set_visible(w->label_process_picked, FALSE);
        gtk_widget_set_visible(w->label_need_eval, FALSE);
        gtk_widget_set_visible(w->label_alloc_returned, FALSE);
        gtk_widget_set_visible(w->label_work_after, FALSE);

        gtk_widget_set_visible(w->box_stuck_diagnostic, TRUE);

        /* Construct clear explanation of why remaining processes cannot finish */
        char diag[1024];
        size_t dpos = (size_t)snprintf(diag, sizeof(diag),
            "No unfinished process can proceed because every remaining process requires more resources than currently available:\n");
        int all_need[BANKER_MAX_PROCS][BANKER_MAX_RES];
        gui_model_get_current_need(w->model, all_need);

        for (int p = 0; p < w->model->current_state.n && dpos < sizeof(diag) - 1; p++) {
            if (!st->finish[p]) {
                char need_str[128];
                format_vector(all_need[p], w->model->current_state.m, need_str, sizeof(need_str));

                int written = snprintf(diag + dpos, sizeof(diag) - dpos, "  • Process P%d: Need %s > Available %s\n",
                                       p, need_str, work_buf);
                if (written > 0 && dpos + (size_t)written < sizeof(diag)) {
                    dpos += (size_t)written;
                } else {
                    break;
                }
            }
        }
        gtk_label_set_text(GTK_LABEL(w->label_stuck_reason), diag);
    }
}

static void safety_render_full_table(SafetyWidgets *w) {
    if (!w || !w->has_analysis || !w->box_trace_table) return;

    /* Clear existing table items */
    GtkWidget *child = gtk_widget_get_first_child(w->box_trace_table);
    while (child) {
        GtkWidget *next = gtk_widget_get_next_sibling(child);
        gtk_box_remove(GTK_BOX(w->box_trace_table), child);
        child = next;
    }

    if (w->trace.count == 0) return;

    /* Build Grid */
    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(grid), 16);
    gtk_grid_set_row_spacing(GTK_GRID(grid), 8);

    /* Headers */
    const char *headers[] = { "Step", "Work (Available)", "Candidate", "Need", "Need <= Work", "Alloc Reclaimed", "New Work" };
    for (int col = 0; col < 7; col++) {
        GtkWidget *h = gtk_label_new(headers[col]);
        gtk_widget_add_css_class(h, "matrix-header");
        gtk_label_set_xalign(GTK_LABEL(h), 0.0f);
        gtk_grid_attach(GTK_GRID(grid), h, col, 0, 1, 1);
    }

    for (int i = 0; i < w->trace.count; i++) {
        const TraceStep *s = &w->trace.steps[i];
        int row = i + 1;

        char step_str[32];
        snprintf(step_str, sizeof(step_str), "Step %d", i + 1);
        GtkWidget *lbl_step = gtk_label_new(step_str);
        gtk_label_set_xalign(GTK_LABEL(lbl_step), 0.0f);
        gtk_grid_attach(GTK_GRID(grid), lbl_step, 0, row, 1, 1);

        char work_str[128];
        format_vector(s->work, w->model->current_state.m, work_str, sizeof(work_str));
        GtkWidget *lbl_work = gtk_label_new(work_str);
        gtk_label_set_xalign(GTK_LABEL(lbl_work), 0.0f);
        gtk_grid_attach(GTK_GRID(grid), lbl_work, 1, row, 1, 1);

        if (s->picked >= 0) {
            char p_str[32];
            snprintf(p_str, sizeof(p_str), "Process P%d", s->picked);
            GtkWidget *lbl_p = gtk_label_new(p_str);
            gtk_label_set_xalign(GTK_LABEL(lbl_p), 0.0f);
            gtk_grid_attach(GTK_GRID(grid), lbl_p, 2, row, 1, 1);

            int all_need[BANKER_MAX_PROCS][BANKER_MAX_RES];
            gui_model_get_current_need(w->model, all_need);
            char need_str[128];
            format_vector(all_need[s->picked], w->model->current_state.m, need_str, sizeof(need_str));
            GtkWidget *lbl_need = gtk_label_new(need_str);
            gtk_label_set_xalign(GTK_LABEL(lbl_need), 0.0f);
            gtk_grid_attach(GTK_GRID(grid), lbl_need, 3, row, 1, 1);

            GtkWidget *lbl_ok = gtk_label_new("TRUE");
            gtk_widget_add_css_class(lbl_ok, "status-pill-safe");
            gtk_grid_attach(GTK_GRID(grid), lbl_ok, 4, row, 1, 1);

            char alloc_str[128];
            format_vector(w->model->current_state.alloc[s->picked], w->model->current_state.m, alloc_str, sizeof(alloc_str));
            GtkWidget *lbl_alloc = gtk_label_new(alloc_str);
            gtk_label_set_xalign(GTK_LABEL(lbl_alloc), 0.0f);
            gtk_grid_attach(GTK_GRID(grid), lbl_alloc, 5, row, 1, 1);

            int new_w[BANKER_MAX_RES];
            for (int j = 0; j < w->model->current_state.m; j++) {
                new_w[j] = s->work[j] + w->model->current_state.alloc[s->picked][j];
            }
            char new_w_str[128];
            format_vector(new_w, w->model->current_state.m, new_w_str, sizeof(new_w_str));
            GtkWidget *lbl_new_w = gtk_label_new(new_w_str);
            gtk_label_set_xalign(GTK_LABEL(lbl_new_w), 0.0f);
            gtk_grid_attach(GTK_GRID(grid), lbl_new_w, 6, row, 1, 1);
        } else {
            /* Stuck iteration */
            GtkWidget *lbl_p = gtk_label_new("None (Blocked)");
            gtk_widget_add_css_class(lbl_p, "status-pill-unsafe");
            gtk_grid_attach(GTK_GRID(grid), lbl_p, 2, row, 1, 1);

            GtkWidget *lbl_need = gtk_label_new("—");
            gtk_grid_attach(GTK_GRID(grid), lbl_need, 3, row, 1, 1);

            GtkWidget *lbl_ok = gtk_label_new("FALSE (Stuck)");
            gtk_widget_add_css_class(lbl_ok, "status-pill-unsafe");
            gtk_grid_attach(GTK_GRID(grid), lbl_ok, 4, row, 1, 1);

            GtkWidget *lbl_alloc = gtk_label_new("[ 0, 0, ... ]");
            gtk_grid_attach(GTK_GRID(grid), lbl_alloc, 5, row, 1, 1);

            GtkWidget *lbl_new_w = gtk_label_new(work_str);
            gtk_grid_attach(GTK_GRID(grid), lbl_new_w, 6, row, 1, 1);
        }
    }

    gtk_box_append(GTK_BOX(w->box_trace_table), grid);
}

static void safety_refresh(SafetyWidgets *w) {
    if (!w || !w->model) return;

    if (!w->model->is_loaded) {
        gtk_widget_set_visible(w->empty_box, TRUE);
        gtk_widget_set_visible(w->content_box, FALSE);
        return;
    }

    gtk_widget_set_visible(w->empty_box, FALSE);
    gtk_widget_set_visible(w->content_box, TRUE);

    /* Recalculate using authoritative engine */
    safety_recalculate(w);

    /* Header badges */
    if (w->is_safe) {
        gtk_label_set_text(GTK_LABEL(w->badge_status), "STATUS: SAFE STATE");
        gtk_widget_remove_css_class(w->badge_status, "badge-rose");
        gtk_widget_add_css_class(w->badge_status, "badge-emerald");
    } else {
        gtk_label_set_text(GTK_LABEL(w->badge_status), "STATUS: UNSAFE STATE");
        gtk_widget_remove_css_class(w->badge_status, "badge-emerald");
        gtk_widget_add_css_class(w->badge_status, "badge-rose");
    }

    char ver_buf[64];
    snprintf(ver_buf, sizeof(ver_buf), "State: v%lu", (unsigned long)w->model->state_version);
    gtk_label_set_text(GTK_LABEL(w->badge_version), ver_buf);

    char dim_buf[64];
    snprintf(dim_buf, sizeof(dim_buf), "%d Processes x %d Resources",
             w->model->current_state.n, w->model->current_state.m);
    gtk_label_set_text(GTK_LABEL(w->badge_dim), dim_buf);

    /* Safe sequence banner */
    if (w->is_safe) {
        char seq_buf[512];
        size_t pos = (size_t)snprintf(seq_buf, sizeof(seq_buf), "Complete Safe Execution Sequence:  < ");
        for (int i = 0; i < w->safe_seq_len && pos < sizeof(seq_buf) - 8; i++) {
            int written = snprintf(seq_buf + pos, sizeof(seq_buf) - pos, "%sP%d", (i > 0 ? " -> " : ""), w->safe_seq[i]);
            if (written > 0 && pos + (size_t)written < sizeof(seq_buf)) {
                pos += (size_t)written;
            } else {
                break;
            }
        }
        if (pos + 3 < sizeof(seq_buf)) {
            snprintf(seq_buf + pos, sizeof(seq_buf) - pos, " >");
        }
        gtk_label_set_text(GTK_LABEL(w->label_seq), seq_buf);
        gtk_widget_remove_css_class(w->label_seq, "status-pill-unsafe");
        gtk_widget_add_css_class(w->label_seq, "status-pill-safe");
    } else {
        gtk_label_set_text(GTK_LABEL(w->label_seq),
                           "No safe execution sequence exists. System state is UNSAFE (Resource demands exceed safe availability).");
        gtk_widget_remove_css_class(w->label_seq, "status-pill-safe");
        gtk_widget_add_css_class(w->label_seq, "status-pill-unsafe");
    }

    /* Render active step details & full trace table */
    safety_render_step(w);
    safety_render_full_table(w);
}

/* Callbacks for stepper navigation */
static void on_step_prev(GtkWidget *btn, gpointer user_data) {
    (void)btn;
    SafetyWidgets *w = (SafetyWidgets *)user_data;
    if (w && w->current_step > 0) {
        w->current_step--;
        safety_render_step(w);
    }
}

static void on_step_next(GtkWidget *btn, gpointer user_data) {
    (void)btn;
    SafetyWidgets *w = (SafetyWidgets *)user_data;
    if (w && w->current_step < w->trace.count - 1) {
        w->current_step++;
        safety_render_step(w);
    }
}

static void on_step_restart(GtkWidget *btn, gpointer user_data) {
    (void)btn;
    SafetyWidgets *w = (SafetyWidgets *)user_data;
    if (w) {
        w->current_step = 0;
        safety_render_step(w);
    }
}

static void on_safety_model_changed(GuiModel *m, void *user_data) {
    (void)m;
    SafetyWidgets *w = (SafetyWidgets *)user_data;
    safety_refresh(w);
}

static void on_safety_map(GtkWidget *widget, gpointer user_data) {
    (void)widget;
    SafetyWidgets *w = (SafetyWidgets *)user_data;
    safety_refresh(w);
}

static void on_safety_free(gpointer data) {
    SafetyWidgets *w = (SafetyWidgets *)data;
    if (w) {
        if (w->model) {
            gui_model_remove_observer(w->model, on_safety_model_changed, w);
            w->model = NULL;
        }
        g_free(w);
    }
}

static void on_safety_destroy(GtkWidget *widget, gpointer user_data) {
    (void)widget;
    SafetyWidgets *w = (SafetyWidgets *)user_data;
    if (w && w->model) {
        gui_model_remove_observer(w->model, on_safety_model_changed, w);
        w->model = NULL;
    }
}

GtkWidget *gui_page_safety_create(GuiModel *model, GtkWidget *window) {
    SafetyWidgets *w = g_new0(SafetyWidgets, 1);
    w->model = model;
    w->window = window;
    w->current_step = 0;

    GtkWidget *page_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_widget_add_css_class(page_box, "page-container");

    /* Bind lifecycle signals and memory cleanup */
    g_signal_connect(page_box, "map", G_CALLBACK(on_safety_map), w);
    g_signal_connect(page_box, "destroy", G_CALLBACK(on_safety_destroy), w);
    g_object_set_data_full(G_OBJECT(page_box), "safety_widgets", w, on_safety_free);

    /* Register as observer on GuiModel */
    gui_model_add_observer(model, on_safety_model_changed, w);

    /* 1. Header Card */
    GtkWidget *header_card = make_card("Banker's Safety Analysis & Stepper Visualizer",
                                       "Step-by-step verification of whether available resources satisfy maximum process demands.");
    GtkWidget *badge_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    w->badge_status = make_badge("STATUS: SAFE STATE", "badge-emerald");
    w->badge_version = make_badge("State: v1", "badge-teal");
    w->badge_dim = make_badge("5 Processes x 3 Resources", "badge-muted");
    gtk_box_append(GTK_BOX(badge_box), w->badge_status);
    gtk_box_append(GTK_BOX(badge_box), w->badge_version);
    gtk_box_append(GTK_BOX(badge_box), w->badge_dim);
    gtk_box_append(GTK_BOX(header_card), badge_box);
    gtk_box_append(GTK_BOX(page_box), header_card);

    /* 2. Empty State Box */
    w->empty_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_add_css_class(w->empty_box, "empty-state-box");
    GtkWidget *empty_title = gtk_label_new("No Active Resource Configuration");
    gtk_widget_add_css_class(empty_title, "empty-state-title");
    GtkWidget *empty_sub = gtk_label_new("Load a benchmark scenario file or configure matrices in System Resources to perform safety analysis.");
    gtk_widget_add_css_class(empty_sub, "empty-state-subtitle");
    gtk_box_append(GTK_BOX(w->empty_box), empty_title);
    gtk_box_append(GTK_BOX(w->empty_box), empty_sub);
    gtk_box_append(GTK_BOX(page_box), w->empty_box);

    /* 3. Content Container */
    w->content_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);

    /* Global Sequence Result Card */
    GtkWidget *seq_card = make_card("Algorithmic Safety Result", NULL);
    w->label_seq = gtk_label_new("Evaluating...");
    gtk_label_set_xalign(GTK_LABEL(w->label_seq), 0.0f);
    gtk_box_append(GTK_BOX(seq_card), w->label_seq);
    gtk_box_append(GTK_BOX(w->content_box), seq_card);

    /* Step-by-Step Stepper Card */
    w->card_step_detail = make_card("Interactive Execution Stepper",
                                    "Step through each iteration of the safety algorithm to inspect Work, Finish, and Need <= Work evaluations.");

    /* Stepper Controls */
    GtkWidget *ctrl_bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    w->btn_prev = gtk_button_new_with_label("◀ Previous Step");
    w->btn_next = gtk_button_new_with_label("Next Step ▶");
    w->btn_restart = gtk_button_new_with_label("↺ Restart Trace");
    w->label_step_indicator = gtk_label_new("Step 1 of 5");
    gtk_widget_add_css_class(w->label_step_indicator, "card-title");

    gtk_box_append(GTK_BOX(ctrl_bar), w->btn_prev);
    gtk_box_append(GTK_BOX(ctrl_bar), w->btn_next);
    gtk_box_append(GTK_BOX(ctrl_bar), w->btn_restart);
    gtk_box_append(GTK_BOX(ctrl_bar), w->label_step_indicator);

    g_signal_connect(w->btn_prev, "clicked", G_CALLBACK(on_step_prev), w);
    g_signal_connect(w->btn_next, "clicked", G_CALLBACK(on_step_next), w);
    g_signal_connect(w->btn_restart, "clicked", G_CALLBACK(on_step_restart), w);

    gtk_box_append(GTK_BOX(w->card_step_detail), ctrl_bar);

    GtkWidget *sep1 = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_append(GTK_BOX(w->card_step_detail), sep1);

    /* Iteration Details */
    w->label_iter_title = gtk_label_new("Iteration 1");
    gtk_widget_add_css_class(w->label_iter_title, "card-title");
    gtk_label_set_xalign(GTK_LABEL(w->label_iter_title), 0.0f);
    gtk_box_append(GTK_BOX(w->card_step_detail), w->label_iter_title);

    w->label_work_before = gtk_label_new("Work (Available): [ ]");
    gtk_label_set_xalign(GTK_LABEL(w->label_work_before), 0.0f);
    gtk_box_append(GTK_BOX(w->card_step_detail), w->label_work_before);

    /* Finish status bar */
    GtkWidget *lbl_fin_hdr = gtk_label_new("Process Completion Status (Finish Array):");
    gtk_label_set_xalign(GTK_LABEL(lbl_fin_hdr), 0.0f);
    gtk_box_append(GTK_BOX(w->card_step_detail), lbl_fin_hdr);

    w->box_finish_indicators = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_append(GTK_BOX(w->card_step_detail), w->box_finish_indicators);

    /* Process selection & updates */
    w->label_process_picked = gtk_label_new("Selected Process:");
    gtk_label_set_xalign(GTK_LABEL(w->label_process_picked), 0.0f);
    gtk_box_append(GTK_BOX(w->card_step_detail), w->label_process_picked);

    w->label_need_eval = gtk_label_new("Need <= Work Check:");
    gtk_label_set_xalign(GTK_LABEL(w->label_need_eval), 0.0f);
    gtk_box_append(GTK_BOX(w->card_step_detail), w->label_need_eval);

    w->label_alloc_returned = gtk_label_new("Allocation Returned:");
    gtk_label_set_xalign(GTK_LABEL(w->label_alloc_returned), 0.0f);
    gtk_box_append(GTK_BOX(w->card_step_detail), w->label_alloc_returned);

    w->label_work_after = gtk_label_new("Work Vector Updated:");
    gtk_label_set_xalign(GTK_LABEL(w->label_work_after), 0.0f);
    gtk_box_append(GTK_BOX(w->card_step_detail), w->label_work_after);

    /* Stuck diagnostic container */
    w->box_stuck_diagnostic = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    GtkWidget *lbl_stuck_title = gtk_label_new("ALGORITHMIC STALL DETECTED (UNSAFE STATE)");
    gtk_widget_add_css_class(lbl_stuck_title, "status-pill-unsafe");
    gtk_label_set_xalign(GTK_LABEL(lbl_stuck_title), 0.0f);
    gtk_box_append(GTK_BOX(w->box_stuck_diagnostic), lbl_stuck_title);

    w->label_stuck_reason = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(w->label_stuck_reason), 0.0f);
    gtk_box_append(GTK_BOX(w->box_stuck_diagnostic), w->label_stuck_reason);
    gtk_box_append(GTK_BOX(w->card_step_detail), w->box_stuck_diagnostic);

    gtk_box_append(GTK_BOX(w->content_box), w->card_step_detail);

    /* Complete Trace Overview Card */
    GtkWidget *trace_card = make_card("Complete Algorithmic Trace Overview",
                                      "Full chronological log of Work vector evolution, process selection, and resource recovery.");
    w->box_trace_table = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_box_append(GTK_BOX(trace_card), w->box_trace_table);
    gtk_box_append(GTK_BOX(w->content_box), trace_card);

    gtk_box_append(GTK_BOX(page_box), w->content_box);

    /* Wrap in scrolled window */
    GtkWidget *scrolled = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
                                   GTK_POLICY_NEVER,
                                   GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled), page_box);

    /* Initial population */
    safety_refresh(w);
    g_object_set_data(G_OBJECT(scrolled), "safety_widgets", w);

    return scrolled;
}
