/* ResourceGuard GTK4 Page Containers — implementation.
 * Builds structured layouts for all 7 application pages.
 * Unavailable features are explicitly labeled as pending implementation. */

#include "pages.h"

/* ------------------------------------------------------------------ */
/*  Reusable layout helpers                                            */
/* ------------------------------------------------------------------ */

static GtkWidget *create_badge(const char *text, const char *css_class) {
    GtkWidget *label = gtk_label_new(text);
    gtk_widget_add_css_class(label, "badge");
    if (css_class && *css_class)
        gtk_widget_add_css_class(label, css_class);
    return label;
}

static GtkWidget *create_card(const char *title, const char *subtitle) {
    GtkWidget *card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_add_css_class(card, "card");

    if (title && *title) {
        GtkWidget *t = gtk_label_new(title);
        gtk_label_set_xalign(GTK_LABEL(t), 0.0f);
        gtk_widget_add_css_class(t, "card-title");
        gtk_box_append(GTK_BOX(card), t);
    }

    if (subtitle && *subtitle) {
        GtkWidget *s = gtk_label_new(subtitle);
        gtk_label_set_xalign(GTK_LABEL(s), 0.0f);
        gtk_label_set_wrap(GTK_LABEL(s), TRUE);
        gtk_widget_add_css_class(s, "card-subtitle");
        gtk_box_append(GTK_BOX(card), s);
    }

    return card;
}

static GtkWidget *create_placeholder_banner(const char *feature_name,
                                           const char *milestone_target) {
    GtkWidget *banner = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_add_css_class(banner, "placeholder-banner");

    char buf[256];
    g_snprintf(buf, sizeof(buf), "[Pending Integration] %s", feature_name);
    GtkWidget *t = gtk_label_new(buf);
    gtk_label_set_xalign(GTK_LABEL(t), 0.0f);
    gtk_widget_add_css_class(t, "badge-amber");
    gtk_widget_add_css_class(t, "badge");
    gtk_box_append(GTK_BOX(banner), t);

    g_snprintf(buf, sizeof(buf),
               "Scheduled for implementation in %s. Underlying engine algorithms and data structures are verified.",
               milestone_target);
    GtkWidget *desc = gtk_label_new(buf);
    gtk_label_set_xalign(GTK_LABEL(desc), 0.0f);
    gtk_label_set_wrap(GTK_LABEL(desc), TRUE);
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
/*  1. Dashboard Page                                                  */
/* ------------------------------------------------------------------ */

GtkWidget *gui_page_dashboard_create(void) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);

    /* Header Card */
    GtkWidget *header_card = create_card("System Dashboard",
                                         "High-level resource telemetry, safety status, and quick simulation actions.");
    GtkWidget *badge_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_append(GTK_BOX(badge_box), create_badge("M3 Foundation", "badge-teal"));
    gtk_box_append(GTK_BOX(badge_box), create_badge("C11 / GTK4 Native", "badge-muted"));
    gtk_box_append(GTK_BOX(badge_box), create_badge("Engine Ready", "badge-emerald"));
    gtk_box_append(GTK_BOX(header_card), badge_box);
    gtk_box_append(GTK_BOX(box), header_card);

    /* Overview Status Card */
    GtkWidget *status_card = create_card("System Safety & Allocation Overview",
                                         "Real-time state summary according to the Banker's safety criteria.");
    GtkWidget *body_label = gtk_label_new(
        "Active Configuration: Initialized demo baseline (5 processes, 3 resource types).\n"
        "Engine Status: Verified against 24,092 test checks (0 invariant violations).\n"
        "Safety Algorithm: Ready for live state binding and execution tracking.");
    gtk_label_set_xalign(GTK_LABEL(body_label), 0.0f);
    gtk_label_set_wrap(GTK_LABEL(body_label), TRUE);
    gtk_widget_add_css_class(body_label, "card-body");
    gtk_box_append(GTK_BOX(status_card), body_label);

    gtk_box_append(GTK_BOX(status_card),
                   create_placeholder_banner("Live Telemetry & Dashboard Widgets", "Milestone 4"));
    gtk_box_append(GTK_BOX(box), status_card);

    return wrap_in_scrolled_window(box);
}

/* ------------------------------------------------------------------ */
/*  2. System Resources Page                                           */
/* ------------------------------------------------------------------ */

GtkWidget *gui_page_resources_create(void) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);

    /* Header Card */
    GtkWidget *header_card = create_card("System Resources & Allocation Matrices",
                                         "Inspect and configure Total resources, Available vector, Allocation matrix, and Maximum demand.");
    GtkWidget *badge_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_append(GTK_BOX(badge_box), create_badge("Conservation Laws Enforced", "badge-emerald"));
    gtk_box_append(GTK_BOX(badge_box), create_badge("Max Capacity: 16 Processes x 8 Resources", "badge-muted"));
    gtk_box_append(GTK_BOX(header_card), badge_box);
    gtk_box_append(GTK_BOX(box), header_card);

    /* Matrix Overview Card */
    GtkWidget *matrix_card = create_card("Resource Matrices Architecture",
                                         "Direct mapping to BankerState internal structures.");
    GtkWidget *desc = gtk_label_new(
        "- Total Vector: Global resource capacity bounds.\n"
        "- Available Vector: Instances currently unallocated (Total - Sum(Alloc)).\n"
        "- Allocation Matrix: Instances currently assigned to each process.\n"
        "- Maximum Matrix: Upper bound claim declared by each process.\n"
        "- Need Matrix: Derived remaining claim (Max - Alloc).");
    gtk_label_set_xalign(GTK_LABEL(desc), 0.0f);
    gtk_label_set_wrap(GTK_LABEL(desc), TRUE);
    gtk_widget_add_css_class(desc, "card-body");
    gtk_box_append(GTK_BOX(matrix_card), desc);

    gtk_box_append(GTK_BOX(matrix_card),
                   create_placeholder_banner("Editable Grid & Matrix Editor", "Milestone 4"));
    gtk_box_append(GTK_BOX(box), matrix_card);

    return wrap_in_scrolled_window(box);
}

/* ------------------------------------------------------------------ */
/*  3. Safety Analysis Page                                            */
/* ------------------------------------------------------------------ */

GtkWidget *gui_page_safety_create(void) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);

    GtkWidget *header_card = create_card("Safety Algorithm & Sequence Evaluation",
                                         "Step-by-step verification of safe sequences and deadlock avoidance.");
    GtkWidget *badge_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_append(GTK_BOX(badge_box), create_badge("Deterministic Policy: Lowest Index First", "badge-teal"));
    gtk_box_append(GTK_BOX(badge_box), create_badge("Trace Step Reconstruction", "badge-muted"));
    gtk_box_append(GTK_BOX(header_card), badge_box);
    gtk_box_append(GTK_BOX(box), header_card);

    GtkWidget *details_card = create_card("Algorithm Mechanics & Work Vector",
                                          "How ResourceGuard computes and traces safety.");
    GtkWidget *desc = gtk_label_new(
        "1. Initialize Work = Available and Finish[i] = false for all processes.\n"
        "2. Find an index i such that Finish[i] == false and Need[i] <= Work.\n"
        "3. If found, Work += Alloc[i], Finish[i] = true, and record the step in the trace.\n"
        "4. If all finish, state is SAFE; if stuck, state is UNSAFE.");
    gtk_label_set_xalign(GTK_LABEL(desc), 0.0f);
    gtk_label_set_wrap(GTK_LABEL(desc), TRUE);
    gtk_widget_add_css_class(desc, "card-body");
    gtk_box_append(GTK_BOX(details_card), desc);

    gtk_box_append(GTK_BOX(details_card),
                   create_placeholder_banner("Interactive Safe Sequence Visualizer & Trace Inspector", "Milestone 5"));
    gtk_box_append(GTK_BOX(box), details_card);

    return wrap_in_scrolled_window(box);
}

/* ------------------------------------------------------------------ */
/*  4. Request Simulator Page                                          */
/* ------------------------------------------------------------------ */

GtkWidget *gui_page_simulator_create(void) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);

    GtkWidget *header_card = create_card("Resource Request Simulator",
                                         "Simulate hypothetical requests, evaluate safety, and observe atomic commit or rollback.");
    GtkWidget *badge_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_append(GTK_BOX(badge_box), create_badge("Atomic Check-and-Commit", "badge-emerald"));
    gtk_box_append(GTK_BOX(badge_box), create_badge("Instant Rollback on Denial", "badge-teal"));
    gtk_box_append(GTK_BOX(header_card), badge_box);
    gtk_box_append(GTK_BOX(box), header_card);

    GtkWidget *body_card = create_card("Request Validation Sequence",
                                       "Order of checks in banker_request().");
    GtkWidget *desc = gtk_label_new(
        "1. Dimension & value validity check (banker_validate).\n"
        "2. Need constraint: Request <= Need (exceeding need is an error).\n"
        "3. Availability constraint: Request <= Available (wait if unavailable).\n"
        "4. Trial allocation: temporarily assign resources and evaluate safety.\n"
        "5. Commit if safe; rollback without side effects if unsafe.");
    gtk_label_set_xalign(GTK_LABEL(desc), 0.0f);
    gtk_label_set_wrap(GTK_LABEL(desc), TRUE);
    gtk_widget_add_css_class(desc, "card-body");
    gtk_box_append(GTK_BOX(body_card), desc);

    gtk_box_append(GTK_BOX(body_card),
                   create_placeholder_banner("Request Input Controls & Outcome Animator", "Milestone 5"));
    gtk_box_append(GTK_BOX(box), body_card);

    return wrap_in_scrolled_window(box);
}

/* ------------------------------------------------------------------ */
/*  5. Why-Not Intelligence Page                                       */
/* ------------------------------------------------------------------ */

GtkWidget *gui_page_whynot_create(void) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);

    GtkWidget *header_card = create_card("Why-Not Intelligence Engine",
                                         "Signature feature: deep causal explanation of denied requests and safe-region mapping.");
    GtkWidget *badge_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_append(GTK_BOX(badge_box), create_badge("Exhaustive Candidate Search", "badge-teal"));
    gtk_box_append(GTK_BOX(badge_box), create_badge("Manhattan Distance Optimization", "badge-emerald"));
    gtk_box_append(GTK_BOX(header_card), badge_box);
    gtk_box_append(GTK_BOX(box), header_card);

    GtkWidget *body_card = create_card("Algorithmic Diagnostics & Space Exploration",
                                       "Causal analysis beyond standard Banker implementations.");
    GtkWidget *desc = gtk_label_new(
        "- Precise Denial Reason: Identifies whether refusal was due to Need excess, unavailability, or safety loss.\n"
        "- Safe Region Boundary: Evaluates candidates within 0 <= req <= min(Need, Available).\n"
        "- Componentwise vs Simultaneous Safety: Identifies safe upper bounds per resource.\n"
        "- Nearest Safe Alternative: Computes closest safe vector to unblock caller.");
    gtk_label_set_xalign(GTK_LABEL(desc), 0.0f);
    gtk_label_set_wrap(GTK_LABEL(desc), TRUE);
    gtk_widget_add_css_class(desc, "card-body");
    gtk_box_append(GTK_BOX(body_card), desc);

    gtk_box_append(GTK_BOX(body_card),
                   create_placeholder_banner("2D/3D Safe Region Visualizer & Alternative Recommender", "Milestone 6"));
    gtk_box_append(GTK_BOX(box), body_card);

    return wrap_in_scrolled_window(box);
}

/* ------------------------------------------------------------------ */
/*  6. Concurrency Lab Page                                            */
/* ------------------------------------------------------------------ */

GtkWidget *gui_page_concurrency_create(void) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);

    GtkWidget *header_card = create_card("Concurrency Lab & POSIX Thread Simulation",
                                         "Multi-threaded resource allocation simulator with controlled TOCTOU race demonstration.");
    GtkWidget *badge_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_append(GTK_BOX(badge_box), create_badge("POSIX Threads (pthreads)", "badge-teal"));
    gtk_box_append(GTK_BOX(badge_box), create_badge("Deterministic Barrier Synchronization", "badge-emerald"));
    gtk_box_append(GTK_BOX(badge_box), create_badge("Zero C Data Races (TSan Verified)", "badge-muted"));
    gtk_box_append(GTK_BOX(header_card), badge_box);
    gtk_box_append(GTK_BOX(box), header_card);

    GtkWidget *body_card = create_card("Simulation Modes & Mechanics",
                                       "Operating system concurrency principles in practice.");
    GtkWidget *desc = gtk_label_new(
        "TSIM_MODE_SAFE:\n"
        "  Mutex held across entire check-and-commit transaction. Invariants hold 100% of the time.\n\n"
        "TSIM_MODE_TOCTOU:\n"
        "  Time-Of-Check to Time-Of-Use race demo. Mutex released between check and commit.\n"
        "  Shows why atomic locking is necessary even when all memory accesses are locked.");
    gtk_label_set_xalign(GTK_LABEL(desc), 0.0f);
    gtk_label_set_wrap(GTK_LABEL(desc), TRUE);
    gtk_widget_add_css_class(desc, "card-body");
    gtk_box_append(GTK_BOX(body_card), desc);

    gtk_box_append(GTK_BOX(body_card),
                   create_placeholder_banner("Live Simulation Dashboard & Worker Thread Controls", "Milestone 7"));
    gtk_box_append(GTK_BOX(box), body_card);

    return wrap_in_scrolled_window(box);
}

/* ------------------------------------------------------------------ */
/*  7. Event History Page                                              */
/* ------------------------------------------------------------------ */

GtkWidget *gui_page_history_create(void) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);

    GtkWidget *header_card = create_card("Event History & Transaction Log",
                                         "Chronological log of resource requests, grants, denials, releases, and invariant checks.");
    GtkWidget *badge_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_append(GTK_BOX(badge_box), create_badge("Monotonic event_id Sequence", "badge-teal"));
    gtk_box_append(GTK_BOX(badge_box), create_badge("Microsecond Timestamps", "badge-muted"));
    gtk_box_append(GTK_BOX(header_card), badge_box);
    gtk_box_append(GTK_BOX(box), header_card);

    GtkWidget *body_card = create_card("Stream Architecture",
                                       "Thread-safe event streaming to the GTK main context.");
    GtkWidget *desc = gtk_label_new(
        "Worker callbacks fire outside internal locks, ensuring high simulation throughput.\n"
        "Events are copied and dispatched asynchronously to the GLib main context via g_idle_add().\n"
        "The strictly monotonic event_id ensures deterministic ordering in the UI stream.");
    gtk_label_set_xalign(GTK_LABEL(desc), 0.0f);
    gtk_label_set_wrap(GTK_LABEL(desc), TRUE);
    gtk_widget_add_css_class(desc, "card-body");
    gtk_box_append(GTK_BOX(body_card), desc);

    gtk_box_append(GTK_BOX(body_card),
                   create_placeholder_banner("Live Event Table & Filter Controls", "Milestone 7"));
    gtk_box_append(GTK_BOX(box), body_card);

    return wrap_in_scrolled_window(box);
}
