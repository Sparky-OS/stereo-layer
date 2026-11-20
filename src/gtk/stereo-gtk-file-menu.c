/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "stereo-gtk.h"
#include <errno.h>
#include <libintl.h>
#include <string.h>

typedef struct {
    GtkWidget *button;
    char *path;
    struct stereo_policy_file_choice choice;
    GSimpleAction *input, *swap, *enable, *display;
    StereoGtkFileChanged changed;
    StereoGtkEnableChanged enable_changed;
    gpointer data;
} FileMenu;

static const char *tr(const char *text)
{
    return g_dpgettext2("stereo-gtk", "Stereo::FileChoiceMenu|", text);
}

static void refresh(FileMenu *menu)
{
    const char *id = "automatic";
    for (size_t i = 0; i < stereo_policy_input_format_count(); ++i) {
        const struct stereo_policy_input_format *entry = stereo_policy_input_format_at(i);
        if (entry->layout == menu->choice.layout && entry->order == menu->choice.order &&
            entry->anaglyph == menu->choice.anaglyph && entry->view == menu->choice.view) {
            id = entry->id;
            break;
        }
    }
    g_simple_action_set_state(menu->input, g_variant_new_string(id));
    g_simple_action_set_state(menu->swap, g_variant_new_boolean(menu->choice.swap_eyes));
    g_simple_action_set_enabled(menu->swap, menu->choice.view != STEREO_POLICY_VIEW_MONO);
}

static void keep(FileMenu *menu, const struct stereo_policy_file_choice *choice)
{
    int error = 0;
    if (stereo_policy_set_file_choice(menu->path, choice) < 0)
        error = errno;
    else
        menu->choice = *choice;
    refresh(menu);
    if (menu->changed)
        menu->changed(menu->button, error, menu->data);
}

static void choose(GSimpleAction *action, GVariant *parameter, gpointer data)
{
    FileMenu *menu = data;
    const char *id = g_variant_get_string(parameter, NULL);
    (void)action;
    for (size_t i = 0; i < stereo_policy_input_format_count(); ++i) {
        const struct stereo_policy_input_format *entry = stereo_policy_input_format_at(i);
        if (!strcmp(id, entry->id)) {
            struct stereo_policy_file_choice choice = menu->choice;
            choice.layout = entry->layout;
            choice.order = entry->order;
            choice.anaglyph = entry->anaglyph;
            choice.view = entry->view;
            keep(menu, &choice);
            return;
        }
    }
}

static void swap(GSimpleAction *action, GVariant *parameter, gpointer data)
{
    FileMenu *menu = data;
    struct stereo_policy_file_choice choice = menu->choice;
    (void)action; (void)parameter;
    choice.swap_eyes = !choice.swap_eyes;
    keep(menu, &choice);
}

static void enable(GSimpleAction *action, GVariant *parameter, gpointer data)
{
    FileMenu *menu = data;
    (void)parameter;
    GVariant *state = g_action_get_state(G_ACTION(action));
    gboolean on = !g_variant_get_boolean(state);
    g_variant_unref(state);
    g_simple_action_set_state(action, g_variant_new_boolean(on));
    if (menu->enable_changed)
        menu->enable_changed(menu->button, on, menu->data);
}

static void display(GSimpleAction *action, GVariant *parameter, gpointer data)
{
    FileMenu *menu = data;
    GError *error = NULL;
    const char *argv[] = {"kcmshell6", "kcm_kscreen", NULL};
    (void)action; (void)parameter;
    if (!g_spawn_async(NULL, (char **)argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, &error)) {
        g_warning("Display Settings: %s", error->message);
        if (menu->changed)
            menu->changed(menu->button, ENOENT, menu->data);
        g_error_free(error);
    }
}

gboolean stereo_gtk_file_menu_get_enabled(GtkWidget *button)
{
    FileMenu *menu = g_object_get_data(G_OBJECT(button), "stereo-file-menu");
    g_return_val_if_fail(menu != NULL, FALSE);
    GVariant *state = g_action_get_state(G_ACTION(menu->enable));
    gboolean on = g_variant_get_boolean(state);
    g_variant_unref(state);
    return on;
}

gboolean stereo_gtk_file_menu_get_choice(GtkWidget *button, struct stereo_policy_file_choice *choice)
{
    FileMenu *menu = g_object_get_data(G_OBJECT(button), "stereo-file-menu");
    g_return_val_if_fail(menu != NULL && choice != NULL, FALSE);
    *choice = menu->choice;
    return menu->path != NULL;
}

static void destroy(gpointer data)
{
    FileMenu *menu = data;
    g_free(menu->path);
    g_object_unref(menu->input);
    g_object_unref(menu->swap);
    g_object_unref(menu->enable);
    g_object_unref(menu->display);
    g_free(menu);
}

GtkWidget *stereo_gtk_file_menu_new(StereoGtkFileChanged changed, StereoGtkEnableChanged enable_changed, gpointer data)
{
    FileMenu *menu = g_new0(FileMenu, 1);
    menu->button = gtk_menu_button_new();
    menu->changed = changed; menu->enable_changed = enable_changed; menu->data = data;
    gtk_menu_button_set_label(GTK_MENU_BUTTON(menu->button), tr("3D Input Format"));
    GSimpleActionGroup *actions = g_simple_action_group_new();
    menu->input = g_simple_action_new_stateful("input", G_VARIANT_TYPE_STRING, g_variant_new_string("automatic"));
    menu->swap = g_simple_action_new_stateful("swap", NULL, g_variant_new_boolean(FALSE));
    g_signal_connect(menu->input, "activate", G_CALLBACK(choose), menu);
    g_signal_connect(menu->swap, "activate", G_CALLBACK(swap), menu);
    menu->enable = g_simple_action_new_stateful("enable", NULL, g_variant_new_boolean(TRUE));
    menu->display = g_simple_action_new("display", NULL);
    g_signal_connect(menu->enable, "activate", G_CALLBACK(enable), menu);
    g_signal_connect(menu->display, "activate", G_CALLBACK(display), menu);
    g_action_map_add_action(G_ACTION_MAP(actions), G_ACTION(menu->input));
    g_action_map_add_action(G_ACTION_MAP(actions), G_ACTION(menu->swap));
    g_action_map_add_action(G_ACTION_MAP(actions), G_ACTION(menu->enable));
    g_action_map_add_action(G_ACTION_MAP(actions), G_ACTION(menu->display));
    gtk_widget_insert_action_group(menu->button, "stereo-file", G_ACTION_GROUP(actions));
    g_object_unref(actions);
    GMenu *model = g_menu_new();
    GMenu *program = g_menu_new();
    g_menu_append(program, tr("Enable 3D"), "stereo-file.enable");
    g_menu_append(program, tr("Display Settings…"), "stereo-file.display");
    g_menu_append_section(model, NULL, G_MENU_MODEL(program));
    g_object_unref(program);
    const char *groups[] = {NULL, NULL, "Interleaved", "One view after the other", "Anaglyph", "Picture and depth"};
    for (int group = 0; group < 6; ++group) {
        GMenu *section = g_menu_new();
        for (size_t i = 0; i < stereo_policy_input_format_count(); ++i) {
            const struct stereo_policy_input_format *entry = stereo_policy_input_format_at(i);
            if (entry->group != group)
                continue;
            GMenuItem *item = g_menu_item_new(tr(entry->text), NULL);
            g_menu_item_set_action_and_target(item, "stereo-file.input", "s", entry->id);
            g_menu_append_item(section, item);
            g_object_unref(item);
        }
        if (groups[group])
            g_menu_append_submenu(model, tr(groups[group]), G_MENU_MODEL(section));
        else
            g_menu_append_section(model, NULL, G_MENU_MODEL(section));
        g_object_unref(section);
    }
    GMenu *section = g_menu_new();
    g_menu_append(section, tr("Swap left and right eyes"), "stereo-file.swap");
    g_menu_append_section(model, NULL, G_MENU_MODEL(section));
    g_object_unref(section);
    gtk_menu_button_set_menu_model(GTK_MENU_BUTTON(menu->button), G_MENU_MODEL(model));
    g_object_unref(model);
    g_object_set_data_full(G_OBJECT(menu->button), "stereo-file-menu", menu, destroy);
    gtk_widget_set_visible(menu->button, FALSE);
    return menu->button;
}

int stereo_gtk_file_menu_set_file(GtkWidget *button, const char *path, gboolean stereo_source)
{
    FileMenu *menu = g_object_get_data(G_OBJECT(button), "stereo-file-menu");
    g_return_val_if_fail(menu != NULL, -1);
    g_free(menu->path); menu->path = g_strdup(path);
    memset(&menu->choice, 0, sizeof(menu->choice));
    int result = path ? stereo_policy_file_choice(path, &menu->choice) : 0;
    gtk_widget_set_visible(button, path && stereo_source);
    gtk_widget_set_sensitive(button, result >= 0 && path != NULL);
    refresh(menu);
    return result;
}
