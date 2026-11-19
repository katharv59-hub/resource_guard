/* ResourceGuard GTK4 Request Simulator View — implementation.
 * Interactive request construction and transactional evaluation via banker_request().
 * Handles all 6 RequestResult states with rich diagnostic explanations and rollback auditing. */

#include "request_view.h"
#include "window.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

static void format_vec(const int vec[], int m, char *out, size_t out_len) {
    if (!out || out_len == 0) return;
    out[0] = '\0';
    snprintf(out, out_len, "[ ");
    for (int j = 0; j < m; j++) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%d%s", vec[j], (j + 1 < m ? ", " : " "));
        strncat(out, buf, out_len - strlen(out) - 1);
    }
    strncat(out, "]", out_len - strlen(out) - 1);
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

static int get_selected_pid(SimulatorWidgets *w) {
    if (!w || !w->dropdown_proc) return 0;
    guint sel = gtk_drop_down_get_selected(GTK_DROP_DOWN(w->dropdown_proc));
    if (sel == GTK_INVALID_LIST_POSITION) return 0;
    return (int)sel;
}

static void update_process_context(SimulatorWidgets *w) {
    if (!w || !w->model || !w->model->is_loaded) return;

    int pid = get_selected_pid(w);
    int n = w->model->current_state.n;
    int m = w->model->current_state.m;
    if (pid < 0 || pid >= n) pid = 0;

    int need[BANKER_MAX_RES];
    for (int j = 0; j < m; j++) {
        need[j] = w->model->current_state.max[pid][j] - w->model->current_state.alloc[pid][j];
    }

    char alloc_str[128], max_str[128], need_str[128], avail_str[128];
    format_vec(w->model->current_state.alloc[pid], m, alloc_str, sizeof(alloc_str));
    format_vec(w->model->current_state.max[pid], m, max_str, sizeof(max_str));
    format_vec(need, m, need_str, sizeof(need_str));
    format_vec(w->model->current_state.available, m, avail_str, sizeof(avail_str));

    char buf[256];
    snprintf(buf, sizeof(buf), "Current Allocation of P%d:  %s", pid, alloc_str);
    gtk_label_set_text(GTK_LABEL(w->label_proc_alloc), buf);

    snprintf(buf, sizeof(buf), "Declared Maximum of P%d:    %s", pid, max_str);
    gtk_label_set_text(GTK_LABEL(w->label_proc_max), buf);

    snprintf(buf, sizeof(buf), "Remaining Need of P%d:       %s", pid, need_str);
    gtk_label_set_text(GTK_LABEL(w->label_proc_need), buf);

    snprintf(buf, sizeof(buf), "System Available Resources:  %s", avail_str);
    gtk_label_set_text(GTK_LABEL(w->label_sys_avail), buf);

    /* Update dynamic spin labels */
    for (int j = 0; j < m; j++) {
        if (w->lbl_res_context[j]) {
            char ctx_buf[128];
            snprintf(ctx_buf, sizeof(ctx_buf), "Need: %d | Avail: %d", need[j], w->model->current_state.available[j]);
            gtk_label_set_text(GTK_LABEL(w->lbl_res_context[j]), ctx_buf);
        }
    }
}

static void on_process_selected(GObject *dropdown, GParamSpec *pspec, gpointer user_data) {
    (void)dropdown;
    (void)pspec;
    SimulatorWidgets *w = (SimulatorWidgets *)user_data;
    update_process_context(w);
}

static void on_btn_clear_clicked(GtkWidget *btn, gpointer user_data) {
    (void)btn;
    SimulatorWidgets *w = (SimulatorWidgets *)user_data;
    if (!w || !w->model) return;
    for (int j = 0; j < w->model->current_state.m; j++) {
        if (w->spin_req[j]) {
            gtk_spin_button_set_value(GTK_SPIN_BUTTON(w->spin_req[j]), 0.0);
        }
    }
}

static void display_result(SimulatorWidgets *w, RequestResult res, int pid, const int req[], const Trace *trace) {
    if (!w || !w->model) return;

    gtk_widget_set_visible(w->card_result, TRUE);

    /* Clear prior details */
    GtkWidget *child = gtk_widget_get_first_child(w->box_result_details);
    while (child) {
        GtkWidget *next = gtk_widget_get_next_sibling(child);
        gtk_box_remove(GTK_BOX(w->box_result_details), child);
        child = next;
    }

    char req_str[128], avail_str[128], need_str[128];
    int m = w->model->current_state.m;
    format_vec(req, m, req_str, sizeof(req_str));
    format_vec(w->model->current_state.available, m, avail_str, sizeof(avail_str));

    int current_need[BANKER_MAX_RES];
    for (int j = 0; j < m; j++) {
        current_need[j] = w->model->current_state.max[pid][j] - w->model->current_state.alloc[pid][j];
    }
    format_vec(current_need, m, need_str, sizeof(need_str));

    switch (res) {
        case REQ_GRANTED: {
            gtk_label_set_text(GTK_LABEL(w->badge_result_code), "REQ_GRANTED");
            gtk_widget_remove_css_class(w->badge_result_code, "badge-rose");
            gtk_widget_remove_css_class(w->badge_result_code, "badge-amber");
            gtk_widget_add_css_class(w->badge_result_code, "badge-emerald");

            char title[128];
            snprintf(title, sizeof(title), "REQUEST GRANTED: Process P%d allocated %s", pid, req_str);
            gtk_label_set_text(GTK_LABEL(w->label_result_title), title);
            gtk_label_set_text(GTK_LABEL(w->label_result_desc),
                               "Transaction committed. Trial state confirmed 100% safe. System resources and model state updated.");

            char buf[256];
            snprintf(buf, sizeof(buf), "Committed Available Vector:    %s", avail_str);
            GtkWidget *l1 = gtk_label_new(buf);
            gtk_label_set_xalign(GTK_LABEL(l1), 0.0f);
            gtk_box_append(GTK_BOX(w->box_result_details), l1);

            char alloc_str[128];
            format_vec(w->model->current_state.alloc[pid], m, alloc_str, sizeof(alloc_str));
            snprintf(buf, sizeof(buf), "Updated Allocation of P%d:       %s", pid, alloc_str);
            GtkWidget *l2 = gtk_label_new(buf);
            gtk_label_set_xalign(GTK_LABEL(l2), 0.0f);
            gtk_box_append(GTK_BOX(w->box_result_details), l2);

            snprintf(buf, sizeof(buf), "Updated Remaining Need of P%d:   %s", pid, need_str);
            GtkWidget *l3 = gtk_label_new(buf);
            gtk_label_set_xalign(GTK_LABEL(l3), 0.0f);
            gtk_box_append(GTK_BOX(w->box_result_details), l3);

            int seq[BANKER_MAX_PROCS], seq_len = 0;
            if (gui_model_is_safe(w->model, seq, &seq_len, NULL)) {
                char s_buf[256] = "Resulting Safe Sequence:  < ";
                for (int i = 0; i < seq_len; i++) {
                    char p_str[16];
                    snprintf(p_str, sizeof(p_str), "%sP%d", (i > 0 ? " -> " : ""), seq[i]);
                    strncat(s_buf, p_str, sizeof(s_buf) - strlen(s_buf) - 1);
                }
                strncat(s_buf, " >", sizeof(s_buf) - strlen(s_buf) - 1);
                GtkWidget *ls = gtk_label_new(s_buf);
                gtk_widget_add_css_class(ls, "status-pill-safe");
                gtk_label_set_xalign(GTK_LABEL(ls), 0.0f);
                gtk_box_append(GTK_BOX(w->box_result_details), ls);
            }
            break;
        }

        case REQ_ERR_BAD_STATE: {
            gtk_label_set_text(GTK_LABEL(w->badge_result_code), "REQ_ERR_BAD_STATE");
            gtk_widget_remove_css_class(w->badge_result_code, "badge-emerald");
            gtk_widget_remove_css_class(w->badge_result_code, "badge-amber");
            gtk_widget_add_css_class(w->badge_result_code, "badge-rose");

            gtk_label_set_text(GTK_LABEL(w->label_result_title), "REQUEST REJECTED: INVALID SYSTEM STATE");
            gtk_label_set_text(GTK_LABEL(w->label_result_desc),
                               "The active system configuration failed banker_validate() or contains an uncommitted draft. Requests cannot be evaluated against an inconsistent baseline.");
            break;
        }

        case REQ_ERR_BAD_INPUT: {
            gtk_label_set_text(GTK_LABEL(w->badge_result_code), "REQ_ERR_BAD_INPUT");
            gtk_widget_remove_css_class(w->badge_result_code, "badge-emerald");
            gtk_widget_remove_css_class(w->badge_result_code, "badge-amber");
            gtk_widget_add_css_class(w->badge_result_code, "badge-rose");

            gtk_label_set_text(GTK_LABEL(w->label_result_title), "REQUEST REJECTED: INVALID INPUT ARGUMENTS");
            char buf[256];
            snprintf(buf, sizeof(buf), "Process P%d or requested vector %s contains negative or out-of-bounds parameters.", pid, req_str);
            gtk_label_set_text(GTK_LABEL(w->label_result_desc), buf);
            break;
        }

        case REQ_ERR_EXCEEDS_NEED: {
            gtk_label_set_text(GTK_LABEL(w->badge_result_code), "REQ_ERR_EXCEEDS_NEED");
            gtk_widget_remove_css_class(w->badge_result_code, "badge-emerald");
            gtk_widget_remove_css_class(w->badge_result_code, "badge-amber");
            gtk_widget_add_css_class(w->badge_result_code, "badge-rose");

            gtk_label_set_text(GTK_LABEL(w->label_result_title), "REQUEST REJECTED: EXCEEDS DECLARED REMAINING NEED");
            gtk_label_set_text(GTK_LABEL(w->label_result_desc),
                               "A process may not request more resources than its declared Maximum minus currently held Allocation (Need).");

            char buf[256];
            snprintf(buf, sizeof(buf), "Requested Vector:    %s", req_str);
            GtkWidget *l1 = gtk_label_new(buf);
            gtk_label_set_xalign(GTK_LABEL(l1), 0.0f);
            gtk_box_append(GTK_BOX(w->box_result_details), l1);

            snprintf(buf, sizeof(buf), "Declared Need(P%d):   %s", pid, need_str);
            GtkWidget *l2 = gtk_label_new(buf);
            gtk_label_set_xalign(GTK_LABEL(l2), 0.0f);
            gtk_box_append(GTK_BOX(w->box_result_details), l2);

            for (int j = 0; j < m; j++) {
                if (req[j] > current_need[j]) {
                    char ex[128];
                    snprintf(ex, sizeof(ex), "  • Resource R%d: Requested %d exceeds Remaining Need %d (exceeded by %d)",
                             j, req[j], current_need[j], req[j] - current_need[j]);
                    GtkWidget *lex = gtk_label_new(ex);
                    gtk_label_set_xalign(GTK_LABEL(lex), 0.0f);
                    gtk_box_append(GTK_BOX(w->box_result_details), lex);
                }
            }
            break;
        }

        case REQ_WAIT_UNAVAILABLE: {
            gtk_label_set_text(GTK_LABEL(w->badge_result_code), "REQ_WAIT_UNAVAILABLE");
            gtk_widget_remove_css_class(w->badge_result_code, "badge-emerald");
            gtk_widget_remove_css_class(w->badge_result_code, "badge-rose");
            gtk_widget_add_css_class(w->badge_result_code, "badge-amber");

            gtk_label_set_text(GTK_LABEL(w->label_result_title), "REQUEST WAITING: INSUFFICIENT SYSTEM AVAILABLE");
            gtk_label_set_text(GTK_LABEL(w->label_result_desc),
                               "Resources are currently allocated to other processes. Process P"
                               " must wait until holding processes release resources. Engine safety check was not evaluated.");

            char buf[256];
            snprintf(buf, sizeof(buf), "Requested Vector:           %s", req_str);
            GtkWidget *l1 = gtk_label_new(buf);
            gtk_label_set_xalign(GTK_LABEL(l1), 0.0f);
            gtk_box_append(GTK_BOX(w->box_result_details), l1);

            snprintf(buf, sizeof(buf), "Currently Available Pool:   %s", avail_str);
            GtkWidget *l2 = gtk_label_new(buf);
            gtk_label_set_xalign(GTK_LABEL(l2), 0.0f);
            gtk_box_append(GTK_BOX(w->box_result_details), l2);

            for (int j = 0; j < m; j++) {
                if (req[j] > w->model->current_state.available[j]) {
                    char def[128];
                    snprintf(def, sizeof(def), "  • Resource R%d: Requested %d > Available %d (deficit: %d units)",
                             j, req[j], w->model->current_state.available[j],
                             req[j] - w->model->current_state.available[j]);
                    GtkWidget *ldef = gtk_label_new(def);
                    gtk_label_set_xalign(GTK_LABEL(ldef), 0.0f);
                    gtk_box_append(GTK_BOX(w->box_result_details), ldef);
                }
            }
            break;
        }

        case REQ_DENIED_UNSAFE: {
            gtk_label_set_text(GTK_LABEL(w->badge_result_code), "REQ_DENIED_UNSAFE");
            gtk_widget_remove_css_class(w->badge_result_code, "badge-emerald");
            gtk_widget_remove_css_class(w->badge_result_code, "badge-amber");
            gtk_widget_add_css_class(w->badge_result_code, "badge-rose");

            gtk_label_set_text(GTK_LABEL(w->label_result_title), "REQUEST DENIED: TRIAL STATE IS UNSAFE");
            gtk_label_set_text(GTK_LABEL(w->label_result_desc),
                               "Resources are currently available, but granting this request would leave the system with no safe execution sequence. An unsafe state indicates risk of deadlock, not an inevitable deadlock.");

            char buf[256];
            int trial_avail[BANKER_MAX_RES];
            for (int j = 0; j < m; j++) {
                trial_avail[j] = w->model->current_state.available[j] - req[j];
            }
            char trial_str[128];
            format_vec(trial_avail, m, trial_str, sizeof(trial_str));

            snprintf(buf, sizeof(buf), "Hypothetical Available If Granted:   %s", trial_str);
            GtkWidget *l1 = gtk_label_new(buf);
            gtk_label_set_xalign(GTK_LABEL(l1), 0.0f);
            gtk_box_append(GTK_BOX(w->box_result_details), l1);

            GtkWidget *lrb = gtk_label_new("EXACT ROLLBACK EXECUTED: Original state preserved with zero modifications.");
            gtk_widget_add_css_class(lrb, "status-pill-safe");
            gtk_label_set_xalign(GTK_LABEL(lrb), 0.0f);
            gtk_box_append(GTK_BOX(w->box_result_details), lrb);

            if (trace && trace->count > 0) {
                char tr_info[256];
                snprintf(tr_info, sizeof(tr_info), "Safety Run: Halted at Step %d (picked = %d). Stuck iteration work = ",
                         trace->count, trace->steps[trace->count - 1].picked);
                char stuck_work[128];
                format_vec(trace->final_work, m, stuck_work, sizeof(stuck_work));
                strncat(tr_info, stuck_work, sizeof(tr_info) - strlen(tr_info) - 1);

                GtkWidget *ltr = gtk_label_new(tr_info);
                gtk_label_set_xalign(GTK_LABEL(ltr), 0.0f);
                gtk_box_append(GTK_BOX(w->box_result_details), ltr);
            }
            break;
        }

        default:
            break;
    }
}

static void on_btn_submit_clicked(GtkWidget *btn, gpointer user_data) {
    (void)btn;
    SimulatorWidgets *w = (SimulatorWidgets *)user_data;
    if (!w || !w->model || !w->model->is_loaded) return;

    int pid = get_selected_pid(w);
    int m = w->model->current_state.m;
    int req[BANKER_MAX_RES] = {0};

    for (int j = 0; j < m; j++) {
        if (w->spin_req[j]) {
            req[j] = (int)gtk_spin_button_get_value(GTK_SPIN_BUTTON(w->spin_req[j]));
        }
    }

    Trace tr;
    memset(&tr, 0, sizeof(tr));
    RequestResult res = gui_model_submit_request(w->model, pid, req, &tr);

    display_result(w, res, pid, req, &tr);
    update_process_context(w);
}

static void simulator_refresh(SimulatorWidgets *w) {
    if (!w || !w->model) return;

    if (!w->model->is_loaded) {
        gtk_widget_set_visible(w->empty_box, TRUE);
        gtk_widget_set_visible(w->content_box, FALSE);
        return;
    }

    gtk_widget_set_visible(w->empty_box, FALSE);
    gtk_widget_set_visible(w->content_box, TRUE);

    /* Update Header Badges */
    char ver_buf[64];
    snprintf(ver_buf, sizeof(ver_buf), "State: v%lu", (unsigned long)w->model->state_version);
    gtk_label_set_text(GTK_LABEL(w->badge_version), ver_buf);

    char dim_buf[64];
    snprintf(dim_buf, sizeof(dim_buf), "%d Processes x %d Resources",
             w->model->current_state.n, w->model->current_state.m);
    gtk_label_set_text(GTK_LABEL(w->badge_dim), dim_buf);

    /* Check uncommitted draft warning */
    bool has_draft = gui_model_has_uncommitted_draft(w->model);
    if (has_draft) {
        gtk_widget_set_visible(w->banner_draft_warning, TRUE);
        gtk_widget_set_sensitive(w->btn_submit, FALSE);
    } else {
        gtk_widget_set_visible(w->banner_draft_warning, FALSE);
        gtk_widget_set_sensitive(w->btn_submit, TRUE);
    }

    /* Update process dropdown model */
    int n = w->model->current_state.n;
    char proc_names[BANKER_MAX_PROCS][32];
    const char *proc_ptrs[BANKER_MAX_PROCS + 1];
    for (int i = 0; i < n; i++) {
        snprintf(proc_names[i], sizeof(proc_names[i]), "Process P%d", i);
        proc_ptrs[i] = proc_names[i];
    }
    proc_ptrs[n] = NULL;

    GtkStringList *slist = gtk_string_list_new(proc_ptrs);
    guint cur_sel = gtk_drop_down_get_selected(GTK_DROP_DOWN(w->dropdown_proc));
    gtk_drop_down_set_model(GTK_DROP_DOWN(w->dropdown_proc), G_LIST_MODEL(slist));
    if (cur_sel < (guint)n) {
        gtk_drop_down_set_selected(GTK_DROP_DOWN(w->dropdown_proc), cur_sel);
    } else {
        gtk_drop_down_set_selected(GTK_DROP_DOWN(w->dropdown_proc), 0);
    }

    /* Update dynamic resource inputs */
    /* Remove existing input rows */
    GtkWidget *child = gtk_widget_get_first_child(w->box_resource_inputs);
    while (child) {
        GtkWidget *next = gtk_widget_get_next_sibling(child);
        gtk_box_remove(GTK_BOX(w->box_resource_inputs), child);
        child = next;
    }

    int m = w->model->current_state.m;
    for (int j = 0; j < m; j++) {
        GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
        gtk_widget_set_margin_bottom(row, 6);

        char r_name[32];
        snprintf(r_name, sizeof(r_name), "Resource R%d Request:", j);
        GtkWidget *lbl_name = gtk_label_new(r_name);
        gtk_widget_set_size_request(lbl_name, 170, -1);
        gtk_label_set_xalign(GTK_LABEL(lbl_name), 0.0f);
        gtk_box_append(GTK_BOX(row), lbl_name);

        w->spin_req[j] = gtk_spin_button_new_with_range(0.0, (double)BANKER_MAX_VALUE, 1.0);
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(w->spin_req[j]), 0.0);
        gtk_widget_set_size_request(w->spin_req[j], 120, -1);
        gtk_box_append(GTK_BOX(row), w->spin_req[j]);

        w->lbl_res_context[j] = gtk_label_new("");
        gtk_widget_add_css_class(w->lbl_res_context[j], "card-subtitle");
        gtk_label_set_xalign(GTK_LABEL(w->lbl_res_context[j]), 0.0f);
        gtk_box_append(GTK_BOX(row), w->lbl_res_context[j]);

        gtk_box_append(GTK_BOX(w->box_resource_inputs), row);
    }

    update_process_context(w);
}

static void on_sim_model_changed(GuiModel *m, void *user_data) {
    (void)m;
    SimulatorWidgets *w = (SimulatorWidgets *)user_data;
    simulator_refresh(w);
}

static void on_sim_map(GtkWidget *widget, gpointer user_data) {
    (void)widget;
    SimulatorWidgets *w = (SimulatorWidgets *)user_data;
    simulator_refresh(w);
}

static void on_sim_destroy(GtkWidget *widget, gpointer user_data) {
    (void)widget;
    SimulatorWidgets *w = (SimulatorWidgets *)user_data;
    if (w) {
        if (w->model) {
            gui_model_remove_observer(w->model, on_sim_model_changed, w);
        }
        g_free(w);
    }
}

GtkWidget *gui_page_simulator_create(GuiModel *model, GtkWidget *window) {
    SimulatorWidgets *w = g_new0(SimulatorWidgets, 1);
    w->model = model;
    w->window = window;

    GtkWidget *page_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_widget_add_css_class(page_box, "page-container");

    g_signal_connect(page_box, "map", G_CALLBACK(on_sim_map), w);
    g_signal_connect(page_box, "destroy", G_CALLBACK(on_sim_destroy), w);

    gui_model_add_observer(model, on_sim_model_changed, w);

    /* 1. Header Card */
    GtkWidget *header_card = make_card("Interactive Resource Request Simulator",
                                       "Simulate dynamic process resource requests, execute atomic trial allocations, and inspect Banker safety decisions.");
    GtkWidget *badge_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    w->badge_status = make_badge("SIMULATOR READY", "badge-teal");
    w->badge_version = make_badge("State: v1", "badge-teal");
    w->badge_dim = make_badge("5 Processes x 3 Resources", "badge-muted");
    gtk_box_append(GTK_BOX(badge_box), w->badge_status);
    gtk_box_append(GTK_BOX(badge_box), w->badge_version);
    gtk_box_append(GTK_BOX(badge_box), w->badge_dim);
    gtk_box_append(GTK_BOX(header_card), badge_box);
    gtk_box_append(GTK_BOX(page_box), header_card);

    /* 2. Empty State */
    w->empty_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_add_css_class(w->empty_box, "empty-state-box");
    GtkWidget *empty_title = gtk_label_new("No Active Resource Configuration");
    gtk_widget_add_css_class(empty_title, "empty-state-title");
    GtkWidget *empty_sub = gtk_label_new("Load a baseline scenario or configure matrices in System Resources to submit interactive resource requests.");
    gtk_widget_add_css_class(empty_sub, "empty-state-subtitle");
    gtk_box_append(GTK_BOX(w->empty_box), empty_title);
    gtk_box_append(GTK_BOX(w->empty_box), empty_sub);
    gtk_box_append(GTK_BOX(page_box), w->empty_box);

    /* 3. Content Box */
    w->content_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);

    /* Draft Warning Banner */
    w->banner_draft_warning = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_add_css_class(w->banner_draft_warning, "placeholder-banner");
    GtkWidget *bw_title = gtk_label_new("UNCOMMITTED RESOURCE DRAFT DETECTED");
    gtk_widget_add_css_class(bw_title, "status-pill-wait");
    gtk_label_set_xalign(GTK_LABEL(bw_title), 0.0f);
    gtk_box_append(GTK_BOX(w->banner_draft_warning), bw_title);

    w->label_draft_warning = gtk_label_new("Uncommitted matrix edits exist in System Resources. Requests cannot be evaluated against an ambiguous state.\nPlease commit or discard changes in System Resources first.");
    gtk_label_set_xalign(GTK_LABEL(w->label_draft_warning), 0.0f);
    gtk_box_append(GTK_BOX(w->banner_draft_warning), w->label_draft_warning);
    gtk_box_append(GTK_BOX(w->content_box), w->banner_draft_warning);

    /* Request Input Card */
    GtkWidget *input_card = make_card("Configure Process Resource Request",
                                      "Select a requesting process, specify requested units per resource type, and evaluate safety.");

    /* Process selector */
    GtkWidget *proc_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    GtkWidget *lbl_proc = gtk_label_new("Requesting Process:");
    gtk_label_set_xalign(GTK_LABEL(lbl_proc), 0.0f);
    gtk_widget_set_size_request(lbl_proc, 170, -1);

    const char *initial_items[] = { "Process P0", NULL };
    w->dropdown_proc = gtk_drop_down_new_from_strings(initial_items);
    gtk_widget_set_size_request(w->dropdown_proc, 180, -1);
    g_signal_connect(w->dropdown_proc, "notify::selected", G_CALLBACK(on_process_selected), w);

    gtk_box_append(GTK_BOX(proc_box), lbl_proc);
    gtk_box_append(GTK_BOX(proc_box), w->dropdown_proc);
    gtk_box_append(GTK_BOX(input_card), proc_box);

    /* Process live context */
    GtkWidget *ctx_card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_add_css_class(ctx_card, "empty-state-box");
    gtk_widget_set_margin_top(ctx_card, 8);
    gtk_widget_set_margin_bottom(ctx_card, 8);

    w->label_proc_alloc = gtk_label_new("Current Allocation: [ ]");
    gtk_label_set_xalign(GTK_LABEL(w->label_proc_alloc), 0.0f);
    w->label_proc_max = gtk_label_new("Declared Maximum:   [ ]");
    gtk_label_set_xalign(GTK_LABEL(w->label_proc_max), 0.0f);
    w->label_proc_need = gtk_label_new("Remaining Need:      [ ]");
    gtk_label_set_xalign(GTK_LABEL(w->label_proc_need), 0.0f);
    w->label_sys_avail = gtk_label_new("System Available:    [ ]");
    gtk_label_set_xalign(GTK_LABEL(w->label_sys_avail), 0.0f);

    gtk_box_append(GTK_BOX(ctx_card), w->label_proc_alloc);
    gtk_box_append(GTK_BOX(ctx_card), w->label_proc_max);
    gtk_box_append(GTK_BOX(ctx_card), w->label_proc_need);
    gtk_box_append(GTK_BOX(ctx_card), w->label_sys_avail);
    gtk_box_append(GTK_BOX(input_card), ctx_card);

    /* Dynamic resource request inputs box */
    w->box_resource_inputs = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_box_append(GTK_BOX(input_card), w->box_resource_inputs);

    /* Buttons box */
    GtkWidget *btn_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_set_margin_top(btn_box, 12);

    w->btn_submit = gtk_button_new_with_label("Evaluate & Submit Request");
    gtk_widget_add_css_class(w->btn_submit, "primary");
    g_signal_connect(w->btn_submit, "clicked", G_CALLBACK(on_btn_submit_clicked), w);

    w->btn_clear = gtk_button_new_with_label("Clear Request");
    g_signal_connect(w->btn_clear, "clicked", G_CALLBACK(on_btn_clear_clicked), w);

    gtk_box_append(GTK_BOX(btn_box), w->btn_submit);
    gtk_box_append(GTK_BOX(btn_box), w->btn_clear);
    gtk_box_append(GTK_BOX(input_card), btn_box);

    gtk_box_append(GTK_BOX(w->content_box), input_card);

    /* 4. Result Presentation Card */
    w->card_result = make_card("Evaluation & Transaction Result",
                               "Feedback produced by the Banker algorithm engine on the proposed resource request.");
    GtkWidget *res_hdr = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    w->badge_result_code = make_badge("READY", "badge-teal");
    w->label_result_title = gtk_label_new("Awaiting Request Submission");
    gtk_widget_add_css_class(w->label_result_title, "card-title");
    gtk_label_set_xalign(GTK_LABEL(w->label_result_title), 0.0f);

    gtk_box_append(GTK_BOX(res_hdr), w->badge_result_code);
    gtk_box_append(GTK_BOX(res_hdr), w->label_result_title);
    gtk_box_append(GTK_BOX(w->card_result), res_hdr);

    w->label_result_desc = gtk_label_new("Submit a request above to evaluate safety feasibility.");
    gtk_label_set_xalign(GTK_LABEL(w->label_result_desc), 0.0f);
    gtk_box_append(GTK_BOX(w->card_result), w->label_result_desc);

    w->box_result_details = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_box_append(GTK_BOX(w->card_result), w->box_result_details);

    gtk_widget_set_visible(w->card_result, FALSE);
    gtk_box_append(GTK_BOX(w->content_box), w->card_result);

    gtk_box_append(GTK_BOX(page_box), w->content_box);

    /* Wrap in scrolled window */
    GtkWidget *scrolled = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
                                   GTK_POLICY_NEVER,
                                   GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled), page_box);

    simulator_refresh(w);

    return scrolled;
}
