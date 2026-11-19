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

    /* Listener callback */
    GuiModelChangeListener listener;
    void *listener_user_data;
};

/* Lifecycle */
void gui_model_init(GuiModel *m);
void gui_model_init_demo(GuiModel *m);
void gui_model_set_listener(GuiModel *m, GuiModelChangeListener cb, void *user_data);

/* Scenario management */
bool gui_model_load_scenario(GuiModel *m, const char *path, char *err_buf, size_t err_len);
bool gui_model_reset_to_original(GuiModel *m);

/* Draft editing & validation */
void gui_model_init_draft_from_current(GuiModel *m);
bool gui_model_set_draft_dimensions(GuiModel *m, int n, int res_m);
void gui_model_set_draft_total(GuiModel *m, int res, int val);
void gui_model_set_draft_available(GuiModel *m, int res, int val);
void gui_model_set_draft_alloc(GuiModel *m, int proc, int res, int val);
void gui_model_set_draft_max(GuiModel *m, int proc, int res, int val);
void gui_model_auto_calculate_draft_available(GuiModel *m);

/* Validation & commit */
BankerStatus gui_model_validate_draft(GuiModel *m);
bool gui_model_commit_draft(GuiModel *m);

/* Queries */
void gui_model_get_current_need(const GuiModel *m, int need[BANKER_MAX_PROCS][BANKER_MAX_RES]);
void gui_model_get_draft_need(const GuiModel *m, int need[BANKER_MAX_PROCS][BANKER_MAX_RES]);
bool gui_model_is_safe(const GuiModel *m, int seq[], int *seq_len, Trace *trace);
void gui_model_get_dashboard_snapshot(const GuiModel *m, GuiDashboardSnapshot *out);

#endif /* GUI_MODEL_H */
