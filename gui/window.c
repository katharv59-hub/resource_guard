/* ResourceGuard GTK4 Main Application Window — implementation.
 * Integrates HeaderBar, GtkStackSidebar, GuiModel, and the 7 page containers
 * into a responsive, high-contrast dark Linux desktop shell. */

#include "window.h"
#include "pages.h"
#include "model.h"

GtkWidget *gui_window_new(GtkApplication *app) {
    GtkWidget *win = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(win), "ResourceGuard — Safety Simulator & Concurrency Lab");
    gtk_window_set_default_size(GTK_WINDOW(win), 1220, 800);

    /* Allocate and initialize authoritative application model */
    GuiModel *model = g_new0(GuiModel, 1);
    gui_model_init_demo(model);
    g_object_set_data_full(G_OBJECT(win), "gui-model", model, g_free);

    /* ---- HeaderBar ---- */
    GtkWidget *header = gtk_header_bar_new();
    gtk_widget_add_css_class(header, "main-headerbar");
    gtk_header_bar_set_show_title_buttons(GTK_HEADER_BAR(header), TRUE);

    GtkWidget *title_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    GtkWidget *t = gtk_label_new("ResourceGuard");
    gtk_widget_add_css_class(t, "title");
    gtk_widget_add_css_class(t, "header-title");
    GtkWidget *sub = gtk_label_new("Processor Architecture & Operating Systems");
    gtk_widget_add_css_class(sub, "subtitle");
    gtk_widget_add_css_class(sub, "header-subtitle");
    gtk_box_append(GTK_BOX(title_box), t);
    gtk_box_append(GTK_BOX(title_box), sub);
    gtk_header_bar_set_title_widget(GTK_HEADER_BAR(header), title_box);

    GtkWidget *badge = gtk_label_new("C11 / GTK4 / Linux");
    gtk_widget_add_css_class(badge, "badge");
    gtk_widget_add_css_class(badge, "badge-teal");
    gtk_header_bar_pack_end(GTK_HEADER_BAR(header), badge);

    gtk_window_set_titlebar(GTK_WINDOW(win), header);

    /* ---- Content Stack ---- */
    GtkWidget *stack = gtk_stack_new();
    gtk_stack_set_transition_type(GTK_STACK(stack), GTK_STACK_TRANSITION_TYPE_CROSSFADE);
    gtk_stack_set_transition_duration(GTK_STACK(stack), 150);
    gtk_widget_set_hexpand(stack, TRUE);
    gtk_widget_set_vexpand(stack, TRUE);

    /* Store reference to stack for navigation access */
    g_object_set_data(G_OBJECT(win), "gui-stack", stack);

    /* Add all 7 navigation pages */
    gtk_stack_add_titled(GTK_STACK(stack), gui_page_dashboard_create(model, win),   "dashboard",   "Dashboard");
    gtk_stack_add_titled(GTK_STACK(stack), gui_page_resources_create(model, win),   "resources",   "System Resources");
    gtk_stack_add_titled(GTK_STACK(stack), gui_page_safety_create(),      "safety",      "Safety Analysis");
    gtk_stack_add_titled(GTK_STACK(stack), gui_page_simulator_create(),   "simulator",   "Request Simulator");
    gtk_stack_add_titled(GTK_STACK(stack), gui_page_whynot_create(),      "whynot",      "Why-not Intelligence");
    gtk_stack_add_titled(GTK_STACK(stack), gui_page_concurrency_create(), "concurrency", "Concurrency Lab");
    gtk_stack_add_titled(GTK_STACK(stack), gui_page_history_create(),      "history",      "Event History");

    /* ---- Sidebar ---- */
    GtkWidget *sidebar = gtk_stack_sidebar_new();
    gtk_stack_sidebar_set_stack(GTK_STACK_SIDEBAR(sidebar), GTK_STACK(stack));
    gtk_widget_set_size_request(sidebar, 230, -1);

    /* ---- Main Window Layout ---- */
    GtkWidget *main_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_box_append(GTK_BOX(main_box), sidebar);

    GtkWidget *sep = gtk_separator_new(GTK_ORIENTATION_VERTICAL);
    gtk_box_append(GTK_BOX(main_box), sep);

    gtk_box_append(GTK_BOX(main_box), stack);

    gtk_window_set_child(GTK_WINDOW(win), main_box);

    return win;
}

GtkWidget *gui_window_get_stack(GtkWidget *win) {
    if (!win) return NULL;
    return GTK_WIDGET(g_object_get_data(G_OBJECT(win), "gui-stack"));
}

GuiModel *gui_window_get_model(GtkWidget *win) {
    if (!win) return NULL;
    return (GuiModel *)g_object_get_data(G_OBJECT(win), "gui-model");
}

void gui_window_switch_to_page(GtkWidget *win, const char *page_name) {
    if (!win || !page_name) return;
    GtkWidget *stack = gui_window_get_stack(win);
    if (stack) {
        gtk_stack_set_visible_child_name(GTK_STACK(stack), page_name);
    }
}
