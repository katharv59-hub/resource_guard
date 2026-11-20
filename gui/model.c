/* ResourceGuard Application Model & State Controller — implementation.
 * Pure C11 model owning BankerState lifecycle, draft validation,
 * scenario loading, and safety queries. */

#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#endif

#include "model.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>
#else
#include <unistd.h>
#endif

static void extract_basename(const char *path, char *out, size_t out_len) {
    if (!path || !out || out_len == 0) return;
    const char *last_sep = NULL;
    for (const char *p = path; *p; p++) {
        if (*p == '/' || *p == '\\') {
            last_sep = p;
        }
    }
    const char *start = last_sep ? last_sep + 1 : path;
    snprintf(out, out_len, "%s", start);
}

static bool file_exists_readable(const char *path) {
    if (!path || path[0] == '\0') return false;
    FILE *f = fopen(path, "r");
    if (f) {
        fclose(f);
        return true;
    }
    return false;
}

static bool is_absolute_path(const char *path) {
    if (!path || path[0] == '\0') return false;
    if (path[0] == '/' || path[0] == '\\') return true;
#if defined(_WIN32)
    if (path[0] != '\0' && path[1] == ':') return true;
#endif
    return false;
}

static bool get_executable_directory(char *out, size_t out_len) {
    if (!out || out_len == 0) return false;
    char exe_buf[512] = {0};

#if defined(_WIN32)
    DWORD len = GetModuleFileNameA(NULL, exe_buf, (DWORD)(sizeof(exe_buf) - 1));
    if (len == 0 || len >= sizeof(exe_buf) - 1) return false;
#else
    ssize_t len = readlink("/proc/self/exe", exe_buf, sizeof(exe_buf) - 1);
    if (len <= 0 || (size_t)len >= sizeof(exe_buf) - 1) return false;
    exe_buf[len] = '\0';
#endif

    char *last_sep = NULL;
    for (char *p = exe_buf; *p; p++) {
        if (*p == '/' || *p == '\\') {
            last_sep = p;
        }
    }
    if (!last_sep) return false;
    *last_sep = '\0';
    snprintf(out, out_len, "%s", exe_buf);
    return true;
}

/* Resource Lookup Order:
 * 1. Absolute Path: if path is absolute, check readability and return directly.
 * 2. Environment Overrides: $RESOURCEGUARD_SCENARIOS_DIR, $RESOURCEGUARD_DATA_DIR.
 * 3. Executable-Relative Path:
 *    - <exe_dir>/<rel_path>
 *    - <exe_dir>/../<rel_path> (bin/ or build/ subdirs)
 *    - <exe_dir>/../../<rel_path>
 *    - <exe_dir>/../share/resourceguard/<rel_path> (Linux FHS prefix)
 *    - <exe_dir>/../share/resourceguard/scenarios/<basename>
 * 4. Current Working Directory Relative: ./<rel_path>, ../<rel_path>, ../../<rel_path>
 * 5. System Data Directories: /usr/local/share/resourceguard/, /usr/share/resourceguard/
 * 6. Non-existent: return false without silent substitution. */
bool gui_model_resolve_resource_path(const char *rel_path, char *out, size_t out_len) {
    if (!rel_path || !out || out_len == 0) return false;

    /* 1. Absolute path */
    if (is_absolute_path(rel_path)) {
        if (file_exists_readable(rel_path)) {
            snprintf(out, out_len, "%s", rel_path);
            return true;
        }
        return false;
    }

    char cand[512];
    char bname[128] = {0};
    extract_basename(rel_path, bname, sizeof(bname));

    /* 2. Environment variable overrides */
    const char *env_scen = getenv("RESOURCEGUARD_SCENARIOS_DIR");
    if (env_scen && env_scen[0] != '\0') {
        snprintf(cand, sizeof(cand), "%s/%s", env_scen, bname);
        if (file_exists_readable(cand)) {
            snprintf(out, out_len, "%s", cand);
            return true;
        }
    }
    const char *env_data = getenv("RESOURCEGUARD_DATA_DIR");
    if (env_data && env_data[0] != '\0') {
        snprintf(cand, sizeof(cand), "%s/%s", env_data, rel_path);
        if (file_exists_readable(cand)) {
            snprintf(out, out_len, "%s", cand);
            return true;
        }
        snprintf(cand, sizeof(cand), "%s/scenarios/%s", env_data, bname);
        if (file_exists_readable(cand)) {
            snprintf(out, out_len, "%s", cand);
            return true;
        }
    }

    /* 3. Executable-relative paths */
    char exe_dir[512] = {0};
    if (get_executable_directory(exe_dir, sizeof(exe_dir))) {
        snprintf(cand, sizeof(cand), "%s/%s", exe_dir, rel_path);
        if (file_exists_readable(cand)) {
            snprintf(out, out_len, "%s", cand);
            return true;
        }
        snprintf(cand, sizeof(cand), "%s/../%s", exe_dir, rel_path);
        if (file_exists_readable(cand)) {
            snprintf(out, out_len, "%s", cand);
            return true;
        }
        snprintf(cand, sizeof(cand), "%s/../../%s", exe_dir, rel_path);
        if (file_exists_readable(cand)) {
            snprintf(out, out_len, "%s", cand);
            return true;
        }
        snprintf(cand, sizeof(cand), "%s/../share/resourceguard/%s", exe_dir, rel_path);
        if (file_exists_readable(cand)) {
            snprintf(out, out_len, "%s", cand);
            return true;
        }
        snprintf(cand, sizeof(cand), "%s/../share/resourceguard/scenarios/%s", exe_dir, bname);
        if (file_exists_readable(cand)) {
            snprintf(out, out_len, "%s", cand);
            return true;
        }
    }

    /* 4. Current working directory relative */
    if (file_exists_readable(rel_path)) {
        snprintf(out, out_len, "%s", rel_path);
        return true;
    }
    snprintf(cand, sizeof(cand), "../%s", rel_path);
    if (file_exists_readable(cand)) {
        snprintf(out, out_len, "%s", cand);
        return true;
    }
    snprintf(cand, sizeof(cand), "../../%s", rel_path);
    if (file_exists_readable(cand)) {
        snprintf(out, out_len, "%s", cand);
        return true;
    }

    /* 5. System standard data directories */
    snprintf(cand, sizeof(cand), "/usr/local/share/resourceguard/scenarios/%s", bname);
    if (file_exists_readable(cand)) {
        snprintf(out, out_len, "%s", cand);
        return true;
    }
    snprintf(cand, sizeof(cand), "/usr/share/resourceguard/scenarios/%s", bname);
    if (file_exists_readable(cand)) {
        snprintf(out, out_len, "%s", cand);
        return true;
    }

    /* 6. Not found - do not substitute */
    return false;
}

static void notify_change(GuiModel *m) {
    if (!m) return;
    GuiModelObserver snapshot[GUI_MODEL_MAX_OBSERVERS];
    int count = m->observer_count;
    if (count > GUI_MODEL_MAX_OBSERVERS) count = GUI_MODEL_MAX_OBSERVERS;
    memcpy(snapshot, m->observers, (size_t)count * sizeof(GuiModelObserver));

    for (int i = 0; i < count; i++) {
        if (snapshot[i].fn) {
            bool still_registered = false;
            for (int j = 0; j < m->observer_count; j++) {
                if (m->observers[j].fn == snapshot[i].fn &&
                    m->observers[j].user_data == snapshot[i].user_data) {
                    still_registered = true;
                    break;
                }
            }
            if (still_registered) {
                snapshot[i].fn(m, snapshot[i].user_data);
            }
        }
    }
}

void gui_model_init(GuiModel *m) {
    if (!m) return;
    memset(m, 0, sizeof(*m));
    m->is_loaded = false;
    m->state_version = 0;
    m->scenario_path[0] = '\0';
    snprintf(m->scenario_name, sizeof(m->scenario_name), "None (Empty State)");
    m->draft_status = BANKER_OK;
    m->draft_validation_msg[0] = '\0';
    m->listener = NULL;
    m->listener_user_data = NULL;
    m->observer_count = 0;
}

bool gui_model_add_observer(GuiModel *m, GuiModelChangeListener cb, void *user_data) {
    if (!m || !cb) return false;
    for (int i = 0; i < m->observer_count; i++) {
        if (m->observers[i].fn == cb && m->observers[i].user_data == user_data) {
            return true; /* already registered */
        }
    }
    if (m->observer_count >= GUI_MODEL_MAX_OBSERVERS) {
        return false;
    }
    m->observers[m->observer_count].fn = cb;
    m->observers[m->observer_count].user_data = user_data;
    m->observer_count++;
    return true;
}

bool gui_model_remove_observer(GuiModel *m, GuiModelChangeListener cb, void *user_data) {
    if (!m || !cb) return false;
    for (int i = 0; i < m->observer_count; i++) {
        if (m->observers[i].fn == cb && m->observers[i].user_data == user_data) {
            for (int j = i; j < m->observer_count - 1; j++) {
                m->observers[j] = m->observers[j + 1];
            }
            m->observer_count--;
            m->observers[m->observer_count].fn = NULL;
            m->observers[m->observer_count].user_data = NULL;
            return true;
        }
    }
    return false;
}

void gui_model_set_listener(GuiModel *m, GuiModelChangeListener cb, void *user_data) {
    if (!m) return;
    if (m->listener) {
        gui_model_remove_observer(m, m->listener, m->listener_user_data);
    }
    m->listener = cb;
    m->listener_user_data = user_data;
    if (cb) {
        gui_model_add_observer(m, cb, user_data);
    }
}

void gui_model_load_demo(GuiModel *m) {
    if (!m) return;

    /* Advance version monotonically */
    m->state_version++;

    char resolved[512] = {0};
    bool has_file = gui_model_resolve_resource_path("tests/scenarios/demo.txt", resolved, sizeof(resolved));

    BankerState s;
    char parse_err[256] = {0};
    bool loaded_from_file = false;

    if (has_file) {
        if (banker_load_scenario(resolved, &s, parse_err, sizeof(parse_err))) {
            int bad_p = -1, bad_r = -1;
            if (banker_validate(&s, &bad_p, &bad_r) == BANKER_OK) {
                loaded_from_file = true;
            }
        }
    }

    if (!loaded_from_file) {
        /* Fall back to built-in demonstration configuration */
        banker_init(&s, 5, 3);
        s.total[0] = 10; s.total[1] = 5; s.total[2] = 7;
        s.available[0] = 3; s.available[1] = 3; s.available[2] = 2;

        int alloc[5][3] = {
            {0, 1, 0},
            {2, 0, 0},
            {3, 0, 2},
            {2, 1, 1},
            {0, 0, 2}
        };
        int max_dem[5][3] = {
            {7, 5, 3},
            {3, 2, 2},
            {9, 0, 2},
            {2, 2, 2},
            {4, 3, 3}
        };

        for (int i = 0; i < 5; i++) {
            for (int j = 0; j < 3; j++) {
                s.alloc[i][j] = alloc[i][j];
                s.max[i][j] = max_dem[i][j];
            }
        }
        m->scenario_path[0] = '\0';
        snprintf(m->scenario_name, sizeof(m->scenario_name), "Built-in Demo Baseline (5 procs, 3 res)");
    } else {
        snprintf(m->scenario_path, sizeof(m->scenario_path), "%s", resolved);
        snprintf(m->scenario_name, sizeof(m->scenario_name), "demo.txt (5 procs, 3 res)");
    }

    m->current_state = s;
    m->original_state = s;
    m->draft_state = s;
    m->is_loaded = true;

    gui_model_clear_why_not(m);
    gui_model_validate_draft(m);
    notify_change(m);
}

void gui_model_init_demo(GuiModel *m) {
    if (!m) return;
    gui_model_init(m);
    gui_model_load_demo(m);
}

bool gui_model_load_scenario(GuiModel *m, const char *path, char *err_buf, size_t err_len) {
    if (!m || !path) {
        if (err_buf && err_len > 0) snprintf(err_buf, err_len, "Invalid argument to load_scenario");
        return false;
    }

    char actual_path[512];
    if (!gui_model_resolve_resource_path(path, actual_path, sizeof(actual_path))) {
        snprintf(actual_path, sizeof(actual_path), "%s", path);
    }

    BankerState loaded;
    char parse_err[256] = {0};
    if (!banker_load_scenario(actual_path, &loaded, parse_err, sizeof(parse_err))) {
        if (err_buf && err_len > 0) {
            snprintf(err_buf, err_len, "Scenario parsing failed: %s", parse_err);
        }
        return false;
    }

    int bad_p = -1, bad_r = -1;
    BankerStatus status = banker_validate(&loaded, &bad_p, &bad_r);
    if (status != BANKER_OK) {
        if (err_buf && err_len > 0) {
            snprintf(err_buf, err_len, "Validation error: %s (proc=%d, res=%d)",
                     banker_status_str(status), bad_p, bad_r);
        }
        return false;
    }

    /* Valid scenario loaded: commit to model and increment state version */
    m->current_state = loaded;
    m->original_state = loaded;
    m->draft_state = loaded;
    m->is_loaded = true;
    m->state_version++;

    snprintf(m->scenario_path, sizeof(m->scenario_path), "%s", path);
    extract_basename(path, m->scenario_name, sizeof(m->scenario_name));

    gui_model_clear_why_not(m);
    gui_model_validate_draft(m);
    notify_change(m);
    return true;
}

bool gui_model_reset_to_original(GuiModel *m) {
    if (!m || !m->is_loaded) return false;
    m->current_state = m->original_state;
    m->draft_state = m->original_state;
    m->state_version++;
    gui_model_clear_why_not(m);
    gui_model_validate_draft(m);
    notify_change(m);
    return true;
}

void gui_model_init_draft_from_current(GuiModel *m) {
    if (!m) return;
    m->draft_state = m->current_state;
    gui_model_validate_draft(m);
}

bool gui_model_has_uncommitted_draft(const GuiModel *m) {
    if (!m || !m->is_loaded) return false;
    if (m->draft_state.n != m->current_state.n || m->draft_state.m != m->current_state.m) {
        return true;
    }
    for (int j = 0; j < m->current_state.m; j++) {
        if (m->draft_state.total[j] != m->current_state.total[j]) return true;
        if (m->draft_state.available[j] != m->current_state.available[j]) return true;
    }
    for (int i = 0; i < m->current_state.n; i++) {
        for (int j = 0; j < m->current_state.m; j++) {
            if (m->draft_state.max[i][j] != m->current_state.max[i][j]) return true;
            if (m->draft_state.alloc[i][j] != m->current_state.alloc[i][j]) return true;
        }
    }
    return false;
}

bool gui_model_set_draft_dimensions(GuiModel *m, int n, int res_m) {
    if (!m) return false;
    if (n < 1 || n > BANKER_MAX_PROCS || res_m < 1 || res_m > BANKER_MAX_RES) {
        return false;
    }

    int old_n = m->draft_state.n;
    int old_m = m->draft_state.m;

    m->draft_state.n = n;
    m->draft_state.m = res_m;

    /* Zero newly exposed resource fields if dimension expanded */
    if (res_m > old_m) {
        for (int j = old_m; j < res_m; j++) {
            m->draft_state.total[j] = 0;
            m->draft_state.available[j] = 0;
            for (int i = 0; i < BANKER_MAX_PROCS; i++) {
                m->draft_state.alloc[i][j] = 0;
                m->draft_state.max[i][j] = 0;
            }
        }
    }

    /* Zero newly exposed process fields if dimension expanded */
    if (n > old_n) {
        for (int i = old_n; i < n; i++) {
            for (int j = 0; j < BANKER_MAX_RES; j++) {
                m->draft_state.alloc[i][j] = 0;
                m->draft_state.max[i][j] = 0;
            }
        }
    }

    gui_model_validate_draft(m);
    return true;
}

void gui_model_set_draft_total(GuiModel *m, int res, int val) {
    if (!m || res < 0 || res >= BANKER_MAX_RES) return;
    m->draft_state.total[res] = val;
    gui_model_validate_draft(m);
}

void gui_model_set_draft_available(GuiModel *m, int res, int val) {
    if (!m || res < 0 || res >= BANKER_MAX_RES) return;
    m->draft_state.available[res] = val;
    gui_model_validate_draft(m);
}

void gui_model_set_draft_alloc(GuiModel *m, int proc, int res, int val) {
    if (!m || proc < 0 || proc >= BANKER_MAX_PROCS || res < 0 || res >= BANKER_MAX_RES) return;
    m->draft_state.alloc[proc][res] = val;
    gui_model_validate_draft(m);
}

void gui_model_set_draft_max(GuiModel *m, int proc, int res, int val) {
    if (!m || proc < 0 || proc >= BANKER_MAX_PROCS || res < 0 || res >= BANKER_MAX_RES) return;
    m->draft_state.max[proc][res] = val;
    gui_model_validate_draft(m);
}

void gui_model_auto_calculate_draft_available(GuiModel *m) {
    if (!m) return;
    for (int j = 0; j < m->draft_state.m; j++) {
        int sum_alloc = 0;
        for (int i = 0; i < m->draft_state.n; i++) {
            sum_alloc += m->draft_state.alloc[i][j];
        }
        int avail = m->draft_state.total[j] - sum_alloc;
        if (avail < 0) avail = 0;
        m->draft_state.available[j] = avail;
    }
    gui_model_validate_draft(m);
}

BankerStatus gui_model_validate_draft(GuiModel *m) {
    if (!m) return BANKER_ERR_DIMENSIONS;

    m->draft_bad_proc = -1;
    m->draft_bad_res = -1;
    m->draft_status = banker_validate(&m->draft_state, &m->draft_bad_proc, &m->draft_bad_res);

    switch (m->draft_status) {
        case BANKER_OK:
            snprintf(m->draft_validation_msg, sizeof(m->draft_validation_msg),
                     "Configuration is valid. Conservation laws and boundary conditions satisfied.");
            break;
        case BANKER_ERR_DIMENSIONS:
            snprintf(m->draft_validation_msg, sizeof(m->draft_validation_msg),
                     "Dimensions out of range: n=%d (must be 1-%d), m=%d (must be 1-%d)",
                     m->draft_state.n, BANKER_MAX_PROCS, m->draft_state.m, BANKER_MAX_RES);
            break;
        case BANKER_ERR_NEGATIVE:
            if (m->draft_bad_proc >= 0 && m->draft_bad_res >= 0) {
                snprintf(m->draft_validation_msg, sizeof(m->draft_validation_msg),
                         "Negative value found at Process P%d, Resource R%d",
                         m->draft_bad_proc, m->draft_bad_res);
            } else if (m->draft_bad_res >= 0) {
                snprintf(m->draft_validation_msg, sizeof(m->draft_validation_msg),
                         "Negative value in Total or Available vector for Resource R%d",
                         m->draft_bad_res);
            } else {
                snprintf(m->draft_validation_msg, sizeof(m->draft_validation_msg),
                         "Negative value detected in configuration");
            }
            break;
        case BANKER_ERR_RANGE:
            snprintf(m->draft_validation_msg, sizeof(m->draft_validation_msg),
                     "Value exceeds BANKER_MAX_VALUE (%d)", BANKER_MAX_VALUE);
            break;
        case BANKER_ERR_ALLOC_GT_MAX:
            snprintf(m->draft_validation_msg, sizeof(m->draft_validation_msg),
                     "Allocation (%d) exceeds Maximum demand (%d) for Process P%d on Resource R%d",
                     m->draft_state.alloc[m->draft_bad_proc][m->draft_bad_res],
                     m->draft_state.max[m->draft_bad_proc][m->draft_bad_res],
                     m->draft_bad_proc, m->draft_bad_res);
            break;
        case BANKER_ERR_MAX_GT_TOTAL:
            snprintf(m->draft_validation_msg, sizeof(m->draft_validation_msg),
                     "Maximum demand (%d) exceeds Total system capacity (%d) for Process P%d on Resource R%d",
                     m->draft_state.max[m->draft_bad_proc][m->draft_bad_res],
                     m->draft_state.total[m->draft_bad_res],
                     m->draft_bad_proc, m->draft_bad_res);
            break;
        case BANKER_ERR_CONSERVATION: {
            int j = m->draft_bad_res;
            int sum_alloc = 0;
            if (j >= 0 && j < m->draft_state.m) {
                for (int i = 0; i < m->draft_state.n; i++) {
                    sum_alloc += m->draft_state.alloc[i][j];
                }
                snprintf(m->draft_validation_msg, sizeof(m->draft_validation_msg),
                         "Conservation violation on Resource R%d: Available (%d) + Alloc sum (%d) = %d != Total (%d)",
                         j, m->draft_state.available[j], sum_alloc,
                         m->draft_state.available[j] + sum_alloc, m->draft_state.total[j]);
            } else {
                snprintf(m->draft_validation_msg, sizeof(m->draft_validation_msg),
                         "Conservation violation: Available + sum(Alloc) != Total");
            }
            break;
        }
        default:
            snprintf(m->draft_validation_msg, sizeof(m->draft_validation_msg),
                     "Unknown validation status: %d", m->draft_status);
            break;
    }

    return m->draft_status;
}

bool gui_model_commit_draft(GuiModel *m) {
    if (!m) return false;
    if (gui_model_validate_draft(m) != BANKER_OK) {
        return false;
    }

    m->current_state = m->draft_state;
    if (!m->is_loaded) {
        m->original_state = m->draft_state;
        m->is_loaded = true;
        snprintf(m->scenario_name, sizeof(m->scenario_name), "Manual Configuration (%dp, %dr)",
                 m->current_state.n, m->current_state.m);
    }
    m->state_version++;

    notify_change(m);
    return true;
}

RequestResult gui_model_submit_request(GuiModel *m, int pid, const int req[], Trace *trace) {
    if (trace) memset(trace, 0, sizeof(*trace));
    if (!m || !m->is_loaded) return REQ_ERR_BAD_STATE;

    /* 1. Committed state must be valid */
    int bp = -1, br = -1;
    if (banker_validate(&m->current_state, &bp, &br) != BANKER_OK) {
        return REQ_ERR_BAD_STATE;
    }

    /* 2. Disallow request if there is an uncommitted draft conflicting with current state */
    if (gui_model_has_uncommitted_draft(m)) {
        return REQ_ERR_BAD_STATE;
    }

    /* 3. Run request on a temporary copy via authoritative Banker engine */
    BankerState copy = m->current_state;
    RequestResult res = banker_request(&copy, pid, req, trace);

    /* 4. On REQ_GRANTED, commit to model only if state actually changed */
    if (res == REQ_GRANTED) {
        bool changed = false;
        for (int j = 0; j < m->current_state.m; j++) {
            if (copy.available[j] != m->current_state.available[j]) { changed = true; break; }
            if (copy.alloc[pid][j] != m->current_state.alloc[pid][j]) { changed = true; break; }
        }

        if (changed) {
            m->current_state = copy;
            m->draft_state = copy;  /* keep draft in sync with committed state */
            m->state_version++;     /* advance version monotonically */
            gui_model_validate_draft(m);
            notify_change(m);       /* notify observers of committed state change */
        }
    } else if (res == REQ_DENIED_UNSAFE) {
        /* Atomically capture rejection context */
        m->why_context.has_rejected_request = true;
        m->why_context.pid = pid;
        for (int j = 0; j < m->current_state.m; j++) {
            m->why_context.request[j] = req[j];
        }
        m->why_context.snapshot_state = m->current_state; /* immutable snapshot at rejection */
        m->why_context.state_version = m->state_version;
        m->why_context.analyzed = false;
        m->why_context.status = WHY_OK;
        memset(&m->why_context.report, 0, sizeof(m->why_context.report));
    }

    return res;
}

void gui_model_clear_why_not(GuiModel *m) {
    if (!m) return;
    memset(&m->why_context, 0, sizeof(m->why_context));
}

bool gui_model_has_why_not(const GuiModel *m) {
    if (!m) return false;
    return m->why_context.has_rejected_request;
}

bool gui_model_get_why_not_context(const GuiModel *m, GuiWhyNotContext *out) {
    if (!m || !out || !m->why_context.has_rejected_request) return false;
    *out = m->why_context;
    return true;
}

bool gui_model_is_why_not_stale(const GuiModel *m) {
    if (!m || !m->why_context.has_rejected_request) return false;
    return (m->why_context.state_version < m->state_version);
}

WhyNotStatus gui_model_run_why_not(GuiModel *m, BankerWhyVisitor visitor, void *user_data) {
    if (!m || !m->is_loaded || !m->why_context.has_rejected_request) {
        return WHY_ERR_BAD_STATE;
    }

    WhyNotStatus st = banker_why_not(&m->why_context.snapshot_state,
                                     m->why_context.pid,
                                     m->why_context.request,
                                     &m->why_context.report,
                                     visitor,
                                     user_data);
    m->why_context.status = st;
    m->why_context.analyzed = true;
    return st;
}

WhyNotStatus gui_model_reevaluate_why_not(GuiModel *m, WhyNotReport *out_report) {
    if (!m || !m->is_loaded || !m->why_context.has_rejected_request) {
        return WHY_ERR_BAD_STATE;
    }

    /* Disallow re-evaluation if there is an uncommitted draft conflicting with current state */
    if (gui_model_has_uncommitted_draft(m)) {
        return WHY_ERR_BAD_STATE;
    }

    WhyNotReport rep;
    memset(&rep, 0, sizeof(rep));
    WhyNotStatus st = banker_why_not(&m->current_state,
                                     m->why_context.pid,
                                     m->why_context.request,
                                     &rep,
                                     NULL,
                                     NULL);
    if (out_report) {
        *out_report = rep;
    }
    return st;
}

void gui_model_get_current_need(const GuiModel *m, int need[BANKER_MAX_PROCS][BANKER_MAX_RES]) {
    if (!m || !need) return;
    banker_need(&m->current_state, need);
}

void gui_model_get_draft_need(const GuiModel *m, int need[BANKER_MAX_PROCS][BANKER_MAX_RES]) {
    if (!m || !need) return;
    banker_need(&m->draft_state, need);
}

bool gui_model_is_safe(const GuiModel *m, int seq[], int *seq_len, Trace *trace) {
    if (!m || !m->is_loaded) return false;
    return banker_is_safe(&m->current_state, seq, seq_len, trace);
}

void gui_model_get_dashboard_snapshot(const GuiModel *m, GuiDashboardSnapshot *out) {
    if (!out) return;
    memset(out, 0, sizeof(*out));
    if (!m || !m->is_loaded) {
        snprintf(out->scenario_name, sizeof(out->scenario_name), "None (Empty State)");
        return;
    }

    out->n = m->current_state.n;
    out->m = m->current_state.m;
    for (int j = 0; j < out->m; j++) {
        out->total[j] = m->current_state.total[j];
        out->available[j] = m->current_state.available[j];
    }

    out->is_loaded = m->is_loaded;
    out->state_version = m->state_version;
    snprintf(out->scenario_name, sizeof(out->scenario_name), "%s", m->scenario_name);
    snprintf(out->scenario_path, sizeof(out->scenario_path), "%s", m->scenario_path);

    out->is_safe = banker_is_safe(&m->current_state, out->safe_seq, &out->safe_seq_len, NULL);
}
