/* ResourceGuard Application Model & State Controller.
 * Pure C11 model owning the BankerState lifecycle, draft validation,
 * scenario loading, and safety queries.
 * Free of GTK or UI dependencies — safe for unit testing. */

#ifndef GUI_MODEL_H
#define GUI_MODEL_H

#include "banker.h"
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define GUI_MAX_PATH_LEN 512
#define GUI_MAX_MSG_LEN  256
#define GUI_MODEL_MAX_OBSERVERS 16

/* Snapshot structure capturing dashboard-ready telemetry */
typedef struct {
    int n;
    int m;
    int total[BANKER_MAX_RES];
    int available[BANKER_MAX_RES];
    bool is_safe;
    int safe_seq[BANKER_MAX_PROCS];
    int safe_seq_len;
    bool is_loaded;
    uint64_t state_version;
    char scenario_name[128];
    char scenario_path[GUI_MAX_PATH_LEN];
} GuiDashboardSnapshot;

/* Application state model */
typedef struct GuiModel GuiModel;

typedef void (*GuiModelChangeListener)(GuiModel *model, void *user_data);

typedef struct {
    GuiModelChangeListener fn;
    void *user_data;
} GuiModelObserver;

/* Rejection diagnostic context capturing the exact conditions of an unsafe denial */
typedef struct {
    bool has_rejected_request;
    int pid;
    int request[BANKER_MAX_RES];
    uint64_t state_version;         /* Version of model at time of rejection */
    BankerState snapshot_state;     /* Immutable snapshot of BankerState when rejected */
    WhyNotReport report;           /* Authoritative analysis report */
    WhyNotStatus status;           /* Status returned by banker_why_not */
    bool analyzed;                 /* Whether analysis has been executed */
} GuiWhyNotContext;

struct GuiModel {
    /* Authoritative committed state */
    BankerState current_state;
    /* Baseline state from scenario file or initial setup */
    BankerState original_state;

    /* Draft state currently being edited in Resources UI */
    BankerState draft_state;
    BankerStatus draft_status;
    int draft_bad_proc;
    int draft_bad_res;
    char draft_validation_msg[GUI_MAX_MSG_LEN];

    /* Metadata */
    bool is_loaded;
    char scenario_path[GUI_MAX_PATH_LEN];
    char scenario_name[128];
    uint64_t state_version;

    /* Primary listener callback (for backwards compatibility) */
    GuiModelChangeListener listener;
    void *listener_user_data;

    /* Multi-subscriber observer callbacks */
    GuiModelObserver observers[GUI_MODEL_MAX_OBSERVERS];
    int observer_count;

    /* Rejection intelligence context */
    GuiWhyNotContext why_context;
};

/* Lifecycle */
void gui_model_init(GuiModel *m);
void gui_model_load_demo(GuiModel *m);
void gui_model_init_demo(GuiModel *m);

/* Observer registration & notification */
bool gui_model_add_observer(GuiModel *m, GuiModelChangeListener cb, void *user_data);
bool gui_model_remove_observer(GuiModel *m, GuiModelChangeListener cb, void *user_data);
void gui_model_set_listener(GuiModel *m, GuiModelChangeListener cb, void *user_data);

/* Scenario management & path resolution */
bool gui_model_resolve_resource_path(const char *rel_path, char *out, size_t out_len);
bool gui_model_load_scenario(GuiModel *m, const char *path, char *err_buf, size_t err_len);
bool gui_model_reset_to_original(GuiModel *m);

/* Draft editing & validation */
void gui_model_init_draft_from_current(GuiModel *m);
bool gui_model_has_uncommitted_draft(const GuiModel *m);
bool gui_model_set_draft_dimensions(GuiModel *m, int n, int res_m);
void gui_model_set_draft_total(GuiModel *m, int res, int val);
void gui_model_set_draft_available(GuiModel *m, int res, int val);
void gui_model_set_draft_alloc(GuiModel *m, int proc, int res, int val);
void gui_model_set_draft_max(GuiModel *m, int proc, int res, int val);
void gui_model_auto_calculate_draft_available(GuiModel *m);

/* Validation & commit */
BankerStatus gui_model_validate_draft(GuiModel *m);
bool gui_model_commit_draft(GuiModel *m);

/* Transactional resource request */
RequestResult gui_model_submit_request(GuiModel *m, int pid, const int req[], Trace *trace);

/* Why-not Diagnostic Intelligence */
void gui_model_clear_why_not(GuiModel *m);
bool gui_model_has_why_not(const GuiModel *m);
bool gui_model_get_why_not_context(const GuiModel *m, GuiWhyNotContext *out);
bool gui_model_is_why_not_stale(const GuiModel *m);
WhyNotStatus gui_model_run_why_not(GuiModel *m, BankerWhyVisitor visitor, void *user_data);
WhyNotStatus gui_model_reevaluate_why_not(GuiModel *m, WhyNotReport *out_report);

/* Queries */
void gui_model_get_current_need(const GuiModel *m, int need[BANKER_MAX_PROCS][BANKER_MAX_RES]);
void gui_model_get_draft_need(const GuiModel *m, int need[BANKER_MAX_PROCS][BANKER_MAX_RES]);
bool gui_model_is_safe(const GuiModel *m, int seq[], int *seq_len, Trace *trace);
void gui_model_get_dashboard_snapshot(const GuiModel *m, GuiDashboardSnapshot *out);

#endif /* GUI_MODEL_H */
