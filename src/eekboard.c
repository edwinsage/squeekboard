/* 
 * Copyright (C) 2010 Daiki Ueno <ueno@unixuser.org>
 * Copyright (C) 2010 Red Hat, Inc.
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif  /* HAVE_CONFIG_H */

#if HAVE_CLUTTER_GTK
#include <clutter-gtk/clutter-gtk.h>
#if NEED_SWAP_EVENT_WORKAROUND
#include <clutter/x11/clutter-x11.h>
#endif
#endif

#include <glib/gi18n.h>
#include <gdk/gdkx.h>
#include <gtk/gtk.h>
#include <gconf/gconf-client.h>
#include <libxklavier/xklavier.h>
#include <fakekey/fakekey.h>
#include <cspi/spi.h>
#include <libnotify/notify.h>

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#if HAVE_CLUTTER_GTK
#include "eek/eek-clutter.h"
#endif

#include "eek/eek-gtk.h"
#include "eek/eek-xkl.h"

#define CSW 640
#define CSH 480

#if HAVE_CLUTTER_GTK
#define USE_CLUTTER TRUE
#else
#define USE_CLUTTER FALSE
#endif

#ifdef EEKBOARD_ENABLE_DEBUG
#define EEKBOARD_NOTE(x,a...) g_message (G_STRLOC ": " x, ##a);
#else
#define EEKBOARD_NOTE(x,a...)
#endif

#define LICENSE \
    "This program is free software: you can redistribute it and/or modify " \
    "it under the terms of the GNU General Public License as published by " \
    "the Free Software Foundation, either version 3 of the License, or " \
    "(at your option) any later version." \
    "\n\n" \
    "This program is distributed in the hope that it will be useful, " \
    "but WITHOUT ANY WARRANTY; without even the implied warranty of " \
    "MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the " \
    "GNU General Public License for more details." \
    "\n\n" \
    "You should have received a copy of the GNU General Public License " \
    "along with this program.  If not, see <http://www.gnu.org/licenses/>. " \

struct _Eekboard {
    gboolean use_clutter;
    gboolean need_swap_event_workaround;
    gboolean accessibility_enabled;
    Display *display;
    FakeKey *fakekey;
    GConfClient *gconfc;
    GtkWidget *widget, *window;
    gint width, height;
    XklEngine *engine;
    XklConfigRegistry *registry;
    GtkUIManager *ui_manager;

    guint countries_merge_id;
    GtkActionGroup *countries_action_group;

    guint languages_merge_id;
    GtkActionGroup *languages_action_group;

    guint models_merge_id;
    GtkActionGroup *models_action_group;

    guint layouts_merge_id;
    GtkActionGroup *layouts_action_group;

    guint options_merge_id;
    GtkActionGroup *options_action_group;

    EekKeyboard *keyboard;
    EekLayout *layout;          /* FIXME: eek_keyboard_get_layout() */
    guint modifiers;
};
typedef struct _Eekboard Eekboard;

struct _SetConfigCallbackData {
    Eekboard *eekboard;
    XklConfigRec *config;
};
typedef struct _SetConfigCallbackData SetConfigCallbackData;

struct _CreateMenuCallbackData {
    Eekboard *eekboard;
    GtkActionGroup *action_group;
    guint merge_id;
};
typedef struct _CreateMenuCallbackData CreateMenuCallbackData;

struct _LayoutVariant {
    XklConfigItem *layout;
    XklConfigItem *variant;
};
typedef struct _LayoutVariant LayoutVariant;

static void       on_countries_menu (GtkAction       *action,
                                     GtkWidget       *widget);
static void       on_languages_menu (GtkAction       *action,
                                     GtkWidget       *widget);
static void       on_models_menu    (GtkAction       *action,
                                     GtkWidget       *window);
static void       on_layouts_menu   (GtkAction       *action,
                                     GtkWidget       *window);
static void       on_options_menu   (GtkAction       *action,
                                     GtkWidget       *window);
static void       on_about          (GtkAction       *action,
                                     GtkWidget       *window);
static void       on_quit           (GtkAction *      action,
                                     GtkWidget       *window);
static void       on_monitor_key_event_toggled
                                    (GtkToggleAction *action,
                                     GtkWidget       *window);
static void       eekboard_free     (Eekboard        *eekboard);
static GtkWidget *create_widget     (Eekboard        *eekboard,
                                     gint             initial_width,
                                     gint             initial_height);
static GtkWidget *create_widget_gtk (Eekboard        *eekboard,
                                     gint             initial_width,
                                     gint             initial_height);

#if !HAVE_CLUTTER_GTK
#define create_widget create_widget_gtk
#endif

static const char ui_description[] =
    "<ui>"
    "  <menubar name='MainMenu'>"
    "    <menu action='FileMenu'>"
    "      <menuitem action='Quit'/>"
    "    </menu>"
    "    <menu action='KeyboardMenu'>"
    "      <menuitem action='MonitorKeyEvent'/>"
    "      <menu action='Country'>"
    "        <placeholder name='CountriesPH'/>"
    "      </menu>"
    "      <menu action='Language'>"
    "        <placeholder name='LanguagesPH'/>"
    "      </menu>"
    "      <separator/>"
    "      <menu action='Model'>"
    "        <placeholder name='ModelsPH'/>"
    "      </menu>"
    "      <menu action='Layout'>"
    "        <placeholder name='LayoutsPH'/>"
    "      </menu>"
    "      <menu action='Option'>"
    "        <placeholder name='OptionsPH'/>"
    "      </menu>"
    "    </menu>"
    "    <menu action='HelpMenu'>"
    "      <menuitem action='About'/>"
    "    </menu>"
    "  </menubar>"
    "</ui>";

#define COUNTRIES_UI_PATH "/MainMenu/KeyboardMenu/Country/CountriesPH"
#define LANGUAGES_UI_PATH "/MainMenu/KeyboardMenu/Language/LanguagesPH"
#define MODELS_UI_PATH "/MainMenu/KeyboardMenu/Model/ModelsPH"
#define LAYOUTS_UI_PATH "/MainMenu/KeyboardMenu/Layout/LayoutsPH"
#define OPTIONS_UI_PATH "/MainMenu/KeyboardMenu/Option/OptionsPH"

static const GtkActionEntry action_entry[] = {
    {"FileMenu", NULL, N_("_File")},
    {"KeyboardMenu", NULL, N_("_Keyboard")},
    {"HelpMenu", NULL, N_("_Help")},
    {"Quit", GTK_STOCK_QUIT, NULL, NULL, NULL, G_CALLBACK (on_quit)},
    {"Country", NULL, N_("Country"), NULL, NULL,
     G_CALLBACK(on_countries_menu)},
    {"Language", NULL, N_("Language"), NULL, NULL,
     G_CALLBACK(on_languages_menu)},
    {"Model", NULL, N_("Model"), NULL, NULL,
     G_CALLBACK(on_models_menu)},
    {"Layout", NULL, N_("Layout"), NULL, NULL,
     G_CALLBACK(on_layouts_menu)},
    {"Option", NULL, N_("Option"), NULL, NULL,
     G_CALLBACK(on_options_menu)},
    {"About", GTK_STOCK_ABOUT, NULL, NULL, NULL, G_CALLBACK (on_about)}
};

static const GtkToggleActionEntry toggle_action_entry[] = {
    {"MonitorKeyEvent", NULL, N_("Monitor Key Typing"), NULL, NULL,
     G_CALLBACK(on_monitor_key_event_toggled), FALSE}
};

static gchar *opt_model = NULL;
static gchar *opt_layouts = NULL;
static gchar *opt_options = NULL;
static gboolean opt_list_models = FALSE;
static gboolean opt_list_layouts = FALSE;
static gboolean opt_list_options = FALSE;
static gboolean opt_version = FALSE;

static const GOptionEntry options[] = {
    {"model", 'M', 0, G_OPTION_ARG_STRING, &opt_model,
     N_("Keyboard model to display")},
    {"layouts", 'L', 0, G_OPTION_ARG_STRING, &opt_layouts,
     N_("Keyboard layouts to display, separated with commas")},
    {"options", 'O', 0, G_OPTION_ARG_STRING, &opt_options,
     N_("Keyboard layout options to display, separated with commas")},
    {"list-models", '\0', 0, G_OPTION_ARG_NONE, &opt_list_models,
     N_("List keyboard models")},
    {"list-layouts", '\0', 0, G_OPTION_ARG_NONE, &opt_list_layouts,
     N_("List all available keyboard layouts and variants")},
    {"list-options", 'O', 0, G_OPTION_ARG_NONE, &opt_list_options,
     N_("List all available keyboard layout options")},
    {"version", 'v', 0, G_OPTION_ARG_NONE, &opt_version,
     N_("Display version")},
    {NULL}
};

static void
on_about (GtkAction * action, GtkWidget *window)
{
  const gchar *authors[] = { "Daiki Ueno", NULL };

  gtk_show_about_dialog (GTK_WINDOW (window),
                         "program-name", PACKAGE,
                         "version", VERSION,
                         "copyright",
                         "Copyright \xc2\xa9 2010 Daiki Ueno\n"
                         "Copyright \xc2\xa9 2010 Red Hat, Inc.",
                         "license", LICENSE,
                         "comments",
                         _("A virtual keyboard for GNOME"),
                         "authors", authors,
                         "website",
                         "http://github.com/ueno/eek/",
                         "website-label", _("Eekboard web site"),
                         "wrap-license", TRUE, NULL);
}

static void
on_quit (GtkAction * action, GtkWidget *window)
{
    Eekboard *eekboard = g_object_get_data (G_OBJECT(window), "eekboard");

    fakekey_release (eekboard->fakekey);
    eekboard_free (eekboard);
    gtk_main_quit ();
}

static SPIBoolean
a11y_focus_listener (const AccessibleEvent *event,
                     void                  *user_data)
{
    Eekboard *eekboard = user_data;
    Accessible *acc = event->source;
    AccessibleStateSet *state_set = Accessible_getStateSet (acc);

    if (AccessibleStateSet_contains (state_set, SPI_STATE_EDITABLE) ||
        Accessible_getRole (acc) == SPI_ROLE_TERMINAL) {
        gtk_widget_show (eekboard->window);
    } else if (!gtk_widget_has_focus (eekboard->window)) {
        gtk_widget_hide (eekboard->window);
    }

    return FALSE;
}

static SPIBoolean
a11y_keystroke_listener (const AccessibleKeystroke *stroke,
                         void                      *user_data)
{
    Eekboard *eekboard = user_data;
    EekKey *key;

    //g_return_val_if_fail (stroke->modifiers == SPI_KEYMASK_UNMODIFIED, FALSE);
    key = eek_keyboard_find_key_by_keycode (eekboard->keyboard,
                                            stroke->keycode);
    g_return_val_if_fail (key, FALSE);
    if (stroke->type == SPI_KEY_PRESSED)
        g_signal_emit_by_name (key, "pressed");
    else
        g_signal_emit_by_name (key, "released");
    return TRUE;
}

static AccessibleEventListener* focusListener;
static AccessibleEventListener* keystrokeListener;

static void
on_monitor_key_event_toggled (GtkToggleAction *action,
                              GtkWidget       *window)
{

    Eekboard *eekboard = g_object_get_data (G_OBJECT(window), "eekboard");

    if (!keystrokeListener) {
        keystrokeListener =
            SPI_createAccessibleKeystrokeListener (a11y_keystroke_listener,
                                                   eekboard);
    }
    if (gtk_toggle_action_get_active (action)) {
        if (!SPI_registerAccessibleKeystrokeListener (keystrokeListener,
                                                      SPI_KEYSET_ALL_KEYS,
                                                      0,
                                                      SPI_KEY_PRESSED |
                                                      SPI_KEY_RELEASED,
                                                      SPI_KEYLISTENER_NOSYNC))
            g_warning ("failed to register keystroke listener");
    } else
        if (!SPI_deregisterAccessibleKeystrokeListener (keystrokeListener, 0))
            g_warning ("failed to deregister keystroke listener");
}

static void
on_key_pressed (EekKeyboard *keyboard,
                EekKey      *key,
                gpointer user_data)
{
    Eekboard *eekboard = user_data;
    guint keysym;

    keysym = eek_key_get_keysym (key);
    EEKBOARD_NOTE("%s %X", eek_keysym_to_string (keysym), eekboard->modifiers);
    
    if (keysym == XK_Shift_L || keysym == XK_Shift_R) {
        gint group, level;

        eekboard->modifiers ^= ShiftMask;
        eek_keyboard_get_keysym_index (keyboard, &group, &level);
        eek_keyboard_set_keysym_index (keyboard, group,
                                       eekboard->modifiers & ShiftMask ? 1 : 0);
    } else if (keysym == XK_Control_L || keysym == XK_Control_R) {
        eekboard->modifiers ^= ControlMask;
    } else if (keysym == XK_Alt_L || keysym == XK_Alt_R) {
        eekboard->modifiers ^= Mod1Mask;
    } else
        fakekey_press_keysym (eekboard->fakekey, keysym, eekboard->modifiers);
}

static void
on_key_released (EekKeyboard *keyboard,
                 EekKey      *key,
                 gpointer user_data)
{
    Eekboard *eekboard = user_data;
    fakekey_release (eekboard->fakekey);
}

static void
on_config_activate (GtkAction *action,
             gpointer   user_data)
{
    SetConfigCallbackData *data = user_data;
    eek_xkl_layout_set_config (EEK_XKL_LAYOUT(data->eekboard->layout),
                               data->config);
}

static void
on_option_toggled (GtkToggleAction *action,
                   gpointer         user_data)
{
    SetConfigCallbackData *data = user_data;
    if (gtk_toggle_action_get_active (action))
        eek_xkl_layout_enable_option (EEK_XKL_LAYOUT(data->eekboard->layout),
                                      data->config->options[0]);
    else
        eek_xkl_layout_disable_option (EEK_XKL_LAYOUT(data->eekboard->layout),
                                       data->config->options[0]);
}

static void
on_changed (EekLayout *layout, gpointer user_data)
{
    Eekboard *eekboard = user_data;
    GtkWidget *vbox, *widget;
    GtkAllocation allocation;

    gtk_widget_get_allocation (GTK_WIDGET (eekboard->widget), &allocation);
    vbox = gtk_widget_get_parent (eekboard->widget);
    /* gtk_widget_destroy() seems not usable for GtkClutterEmbed */
    gtk_container_remove (GTK_CONTAINER(vbox), eekboard->widget);

    g_object_unref (eekboard->keyboard);
    widget = create_widget (eekboard, allocation.width, allocation.height);
    gtk_container_add (GTK_CONTAINER(vbox), widget);
    gtk_widget_show_all (vbox);
}

static void
collect_layout_variant (XklConfigRegistry *registry,
                        const XklConfigItem *layout,
                        const XklConfigItem *variant,
                        gpointer user_data)
{
    GSList **r_lv_list = user_data;
    LayoutVariant *lv;

    lv = g_slice_new0 (LayoutVariant);
    lv->layout = g_slice_dup (XklConfigItem, layout);
    if (variant)
        lv->variant = g_slice_dup (XklConfigItem, variant);
    *r_lv_list = g_slist_prepend (*r_lv_list, lv);
}

static void
add_layout_variant_actions (CreateMenuCallbackData *data,
                            const gchar            *name,
                            const gchar            *path,
                            GSList                 *lv_list)
{
    gchar variant_action_name[128];
    SetConfigCallbackData *config;
    GSList *head;

    for (head = lv_list; head; head = head->next) {
        LayoutVariant *lv = head->data;
        GtkAction *action;
        gchar description[XKL_MAX_CI_DESC_LENGTH * 2 + 4];

        if (lv->variant) {
            g_snprintf (variant_action_name, sizeof (variant_action_name),
                        "SetLayoutVariant %s %s %s",
                        name,
                        lv->layout->name,
                        lv->variant->name);
            g_snprintf (description, sizeof (description),
                        "%s (%s)",
                        lv->layout->description,
                        lv->variant->description);
        } else {
            g_snprintf (variant_action_name, sizeof (variant_action_name),
                        "SetLayout %s %s",
                        name,
                        lv->layout->name);
            g_strlcpy (description,
                       lv->layout->description,
                       sizeof (description));
        }

        action = gtk_action_new (variant_action_name,
                                 description,
                                 NULL,
                                 NULL);

        config = g_slice_new (SetConfigCallbackData);
        config->eekboard = data->eekboard;
        config->config = xkl_config_rec_new ();
        config->config->layouts = g_new0 (char *, 2);
        config->config->layouts[0] = g_strdup (lv->layout->name);
        config->config->layouts[1] = NULL;
        if (lv->variant) {
            config->config->variants = g_new0 (char *, 2);
            config->config->variants[0] = g_strdup (lv->variant->name);
            config->config->variants[1] = NULL;
        }
        g_signal_connect (action, "activate", G_CALLBACK (on_config_activate), config);

        gtk_action_group_add_action (data->action_group, action);
        g_object_unref (action);

        gtk_ui_manager_add_ui (data->eekboard->ui_manager, data->merge_id,
                               path,
                               variant_action_name, variant_action_name,
                               GTK_UI_MANAGER_MENUITEM, FALSE);

        g_slice_free (XklConfigItem, lv->layout);
        if (lv->variant)
            g_slice_free (XklConfigItem, lv->variant);
        g_slice_free (LayoutVariant, lv);
    }
}

static void
country_callback (XklConfigRegistry   *registry,
                  const XklConfigItem *item,
                  gpointer             user_data)
{
    CreateMenuCallbackData *data = user_data;
    GtkAction *action;
    GSList *lv_list = NULL;
    char country_action_name[128];
    char country_action_path[128];

    g_snprintf (country_action_name, sizeof (country_action_name),
                "Country %s", item->name);
    action = gtk_action_new (country_action_name, item->description,
                             NULL, NULL);
    gtk_action_group_add_action (data->action_group, action);
    g_object_unref (action);

    gtk_ui_manager_add_ui (data->eekboard->ui_manager, data->merge_id,
                           COUNTRIES_UI_PATH,
                           country_action_name, country_action_name,
                           GTK_UI_MANAGER_MENU, FALSE);
    g_snprintf (country_action_path, sizeof (country_action_path),
                COUNTRIES_UI_PATH "/%s", country_action_name);

    xkl_config_registry_foreach_country_variant (data->eekboard->registry,
                                                 item->name,
                                                 collect_layout_variant,
                                                 &lv_list);
    add_layout_variant_actions (data, item->name, country_action_path,
                                lv_list);
    g_slist_free (lv_list);
}

static guint
create_countries_menu (Eekboard *eekboard)
{
    CreateMenuCallbackData data;

    data.eekboard = eekboard;
    data.merge_id = gtk_ui_manager_new_merge_id (eekboard->ui_manager);
    data.action_group = eekboard->countries_action_group;

    xkl_config_registry_foreach_country (eekboard->registry,
                                         country_callback,
                                         &data);
    return data.merge_id;
}

static void
on_countries_menu (GtkAction *action, GtkWidget *window)
{
    Eekboard *eekboard = g_object_get_data (G_OBJECT(window), "eekboard");
    if (eekboard->countries_merge_id == 0)
        eekboard->countries_merge_id = create_countries_menu (eekboard);
}

static void
language_callback (XklConfigRegistry *registry,
                   const XklConfigItem *item,
                   gpointer user_data)
{
    CreateMenuCallbackData *data = user_data;
    GtkAction *action;
    GSList *lv_list = NULL;
    char language_action_name[128];
    char language_action_path[128];

    g_snprintf (language_action_name, sizeof (language_action_name),
                "Language %s", item->name);
    action = gtk_action_new (language_action_name, item->description,
                             NULL, NULL);
    gtk_action_group_add_action (data->action_group, action);
    g_object_unref (action);

    gtk_ui_manager_add_ui (data->eekboard->ui_manager, data->merge_id,
                           LANGUAGES_UI_PATH,
                           language_action_name, language_action_name,
                           GTK_UI_MANAGER_MENU, FALSE);
    g_snprintf (language_action_path, sizeof (language_action_path),
                LANGUAGES_UI_PATH "/%s", language_action_name);

    xkl_config_registry_foreach_language_variant (data->eekboard->registry,
                                                  item->name,
                                                  collect_layout_variant,
                                                  &lv_list);
    add_layout_variant_actions (data, item->name, language_action_path,
                                lv_list);
    g_slist_free (lv_list);
}

static guint
create_languages_menu (Eekboard *eekboard)
{
    CreateMenuCallbackData data;

    data.eekboard = eekboard;
    data.merge_id = gtk_ui_manager_new_merge_id (eekboard->ui_manager);
    data.action_group = eekboard->languages_action_group;

    xkl_config_registry_foreach_language (eekboard->registry,
                                          language_callback,
                                          &data);
    return data.merge_id;
}

static void
on_languages_menu (GtkAction *action, GtkWidget *window)
{
    Eekboard *eekboard = g_object_get_data (G_OBJECT(window), "eekboard");
    if (eekboard->languages_merge_id == 0)
        eekboard->languages_merge_id = create_languages_menu (eekboard);
}

static void
model_callback (XklConfigRegistry *registry,
                const XklConfigItem *item,
                gpointer user_data)
{
    CreateMenuCallbackData *data = user_data;
    GtkAction *action;
    char model_action_name[128];
    SetConfigCallbackData *config;

    g_snprintf (model_action_name, sizeof (model_action_name),
                "Model %s", item->name);
    action = gtk_action_new (model_action_name, item->description, NULL, NULL);
    gtk_action_group_add_action (data->action_group, action);

    config = g_slice_new (SetConfigCallbackData);
    config->eekboard = data->eekboard;
    config->config = xkl_config_rec_new ();
    config->config->model = g_strdup (item->name);

    g_signal_connect (action, "activate", G_CALLBACK (on_config_activate),
                      config);
    g_object_unref (action);
    gtk_ui_manager_add_ui (data->eekboard->ui_manager, data->merge_id,
                           MODELS_UI_PATH,
                           model_action_name, model_action_name,
                           GTK_UI_MANAGER_MENUITEM, FALSE);
}

static guint
create_models_menu (Eekboard *eekboard)
{
    CreateMenuCallbackData data;

    data.eekboard = eekboard;
    data.merge_id = gtk_ui_manager_new_merge_id (eekboard->ui_manager);
    data.action_group = eekboard->models_action_group;

    xkl_config_registry_foreach_model (eekboard->registry,
                                       model_callback,
                                       &data);
    return data.merge_id;
}

static void
on_models_menu (GtkAction *action, GtkWidget *window)
{
    Eekboard *eekboard = g_object_get_data (G_OBJECT(window), "eekboard");
    if (eekboard->models_merge_id == 0)
        eekboard->models_merge_id = create_models_menu (eekboard);
}

static void
collect_variant (XklConfigRegistry *registry,
                 const XklConfigItem *variant,
                 gpointer user_data)
{
    GSList **r_variants = user_data;
    XklConfigItem *item;

    item = g_slice_dup (XklConfigItem, variant);
    *r_variants = g_slist_prepend (*r_variants, item);
}

static void
layout_callback (XklConfigRegistry *registry,
                 const XklConfigItem *item,
                 gpointer user_data)
{
    CreateMenuCallbackData *data = user_data;
    GtkAction *action;
    GSList *variants = NULL;
    char layout_action_name[128], variant_action_name[128];
    SetConfigCallbackData *config;

    g_snprintf (layout_action_name, sizeof (layout_action_name),
                "SetLayout %s", item->name);
    action = gtk_action_new (layout_action_name, item->description, NULL, NULL);
    gtk_action_group_add_action (data->action_group, action);

    xkl_config_registry_foreach_layout_variant (registry,
                                                item->name,
                                                collect_variant,
                                                &variants);

    if (!variants) {
        config = g_slice_new (SetConfigCallbackData);
        config->eekboard = data->eekboard;
        config->config = xkl_config_rec_new ();
        config->config->layouts = g_new0 (char *, 2);
        config->config->layouts[0] = g_strdup (item->name);
        config->config->layouts[1] = NULL;
        /* Reset the existing variant setting. */
        config->config->variants = g_new0 (char *, 1);
        config->config->variants[0] = NULL;
        g_signal_connect (action, "activate", G_CALLBACK (on_config_activate),
                          config);
        g_object_unref (action);
        gtk_ui_manager_add_ui (data->eekboard->ui_manager, data->merge_id,
                               LAYOUTS_UI_PATH,
                               layout_action_name, layout_action_name,
                               GTK_UI_MANAGER_MENUITEM, FALSE);
    } else {
        char layout_path[128];
        GSList *head;

        g_object_unref (action);
        gtk_ui_manager_add_ui (data->eekboard->ui_manager, data->merge_id,
                               LAYOUTS_UI_PATH,
                               layout_action_name, layout_action_name,
                               GTK_UI_MANAGER_MENU, FALSE);
        g_snprintf (layout_path, sizeof (layout_path),
                    LAYOUTS_UI_PATH "/%s", layout_action_name);

        for (head = variants; head; head = head->next) {
            XklConfigItem *_item = head->data;

            g_snprintf (variant_action_name, sizeof (variant_action_name),
                        "SetLayoutVariant %s %s", item->name, _item->name);
            action = gtk_action_new (variant_action_name,
                                     _item->description,
                                     NULL,
                                     NULL);

            config = g_slice_new (SetConfigCallbackData);
            config->eekboard = data->eekboard;
            config->config = xkl_config_rec_new ();
            config->config->layouts = g_new0 (char *, 2);
            config->config->layouts[0] = g_strdup (item->name);
            config->config->layouts[1] = NULL;
            config->config->variants = g_new0 (char *, 2);
            config->config->variants[0] = g_strdup (_item->name);
            config->config->variants[1] = NULL;
            g_signal_connect (action, "activate", G_CALLBACK (on_config_activate),
                              config);

            gtk_action_group_add_action (data->action_group, action);
            g_object_unref (action);

            gtk_ui_manager_add_ui (data->eekboard->ui_manager, data->merge_id,
                                   layout_path,
                                   variant_action_name, variant_action_name,
                                   GTK_UI_MANAGER_MENUITEM, FALSE);
            g_slice_free (XklConfigItem, _item);
        }
        g_slist_free (variants);
    }
}

static guint
create_layouts_menu (Eekboard *eekboard)
{
    CreateMenuCallbackData data;

    data.eekboard = eekboard;
    data.merge_id = gtk_ui_manager_new_merge_id (eekboard->ui_manager);
    data.action_group = eekboard->layouts_action_group;

    xkl_config_registry_foreach_layout (eekboard->registry,
                                        layout_callback,
                                        &data);
    return data.merge_id;
}

static void
on_layouts_menu (GtkAction *action, GtkWidget *window)
{
    Eekboard *eekboard = g_object_get_data (G_OBJECT(window), "eekboard");
    if (eekboard->layouts_merge_id == 0)
        eekboard->layouts_merge_id = create_layouts_menu (eekboard);
}

static void
collect_option (XklConfigRegistry *registry,
                 const XklConfigItem *option,
                 gpointer user_data)
{
    GSList **r_options = user_data;
    XklConfigItem *item;

    item = g_slice_dup (XklConfigItem, option);
    *r_options = g_slist_prepend (*r_options, item);
}

static void
option_group_callback (XklConfigRegistry   *registry,
                       const XklConfigItem *item,
                       gpointer             user_data)
{
    CreateMenuCallbackData *data = user_data;
    GtkAction *action;
    GSList *options = NULL, *head;
    gchar option_group_action_name[128], option_action_name[128];
    gchar option_group_path[128];
    SetConfigCallbackData *config;

    g_snprintf (option_group_action_name, sizeof (option_group_action_name),
                "OptionGroup %s", item->name);
    action = gtk_action_new (option_group_action_name, item->description,
                             NULL, NULL);
    gtk_action_group_add_action (data->action_group, action);
    g_object_unref (action);

    xkl_config_registry_foreach_option (registry,
                                        item->name,
                                        collect_option,
                                        &options);
    g_return_if_fail (options);

    gtk_ui_manager_add_ui (data->eekboard->ui_manager, data->merge_id,
                           OPTIONS_UI_PATH,
                           option_group_action_name, option_group_action_name,
                           GTK_UI_MANAGER_MENU, FALSE);
    g_snprintf (option_group_path, sizeof (option_group_path),
                OPTIONS_UI_PATH "/%s", option_group_action_name);

    for (head = options; head; head = head->next) {
        XklConfigItem *_item = head->data;
        GtkToggleAction *toggle;

        g_snprintf (option_action_name, sizeof (option_action_name),
                    "SetOption %s", _item->name);
        toggle = gtk_toggle_action_new (option_action_name,
                                        _item->description,
                                        NULL,
                                        NULL);

        config = g_slice_new (SetConfigCallbackData);
        config->eekboard = data->eekboard;
        config->config = xkl_config_rec_new ();
        config->config->options = g_new0 (char *, 2);
        config->config->options[0] = g_strdup (_item->name);
        config->config->options[1] = NULL;
        g_signal_connect (toggle, "toggled", G_CALLBACK (on_option_toggled),
                          config);

        gtk_action_group_add_action (data->action_group, GTK_ACTION(toggle));
        g_object_unref (toggle);

        gtk_ui_manager_add_ui (data->eekboard->ui_manager, data->merge_id,
                               option_group_path,
                               option_action_name, option_action_name,
                               GTK_UI_MANAGER_MENUITEM, FALSE);
        g_slice_free (XklConfigItem, _item);
    }
    g_slist_free (options);
}

static guint
create_options_menu (Eekboard *eekboard)
{
    CreateMenuCallbackData data;

    data.eekboard = eekboard;
    data.merge_id = gtk_ui_manager_new_merge_id (eekboard->ui_manager);
    data.action_group = eekboard->options_action_group;

    xkl_config_registry_foreach_option_group (eekboard->registry,
                                              option_group_callback,
                                              &data);
    return data.merge_id;
}

static void
on_options_menu (GtkAction *action, GtkWidget *window)
{
    Eekboard *eekboard = g_object_get_data (G_OBJECT(window), "eekboard");
    if (eekboard->options_merge_id == 0)
        eekboard->options_merge_id = create_options_menu (eekboard);
}

static void
create_menus (Eekboard      *eekboard,
              GtkWidget     *window)
{
    GtkActionGroup *action_group;

    action_group = gtk_action_group_new ("MenuActions");
    gtk_action_group_set_translation_domain (action_group, GETTEXT_PACKAGE);

    gtk_action_group_add_actions (action_group, action_entry,
                                  G_N_ELEMENTS (action_entry), window);
    gtk_action_group_add_toggle_actions (action_group, toggle_action_entry,
                                         G_N_ELEMENTS (toggle_action_entry),
                                         window);

    gtk_ui_manager_insert_action_group (eekboard->ui_manager, action_group, 0);
    gtk_ui_manager_add_ui_from_string (eekboard->ui_manager, ui_description, -1, NULL);

    eekboard->countries_action_group = gtk_action_group_new ("Countries");
    gtk_ui_manager_insert_action_group (eekboard->ui_manager,
                                        eekboard->countries_action_group,
                                        -1);

    eekboard->languages_action_group = gtk_action_group_new ("Languages");
    gtk_ui_manager_insert_action_group (eekboard->ui_manager,
                                        eekboard->languages_action_group,
                                        -1);
    
    eekboard->models_action_group = gtk_action_group_new ("Models");
    gtk_ui_manager_insert_action_group (eekboard->ui_manager,
                                        eekboard->models_action_group,
                                        -1);

    eekboard->layouts_action_group = gtk_action_group_new ("Layouts");
    gtk_ui_manager_insert_action_group (eekboard->ui_manager,
                                        eekboard->layouts_action_group,
                                        -1);

    eekboard->options_action_group = gtk_action_group_new ("Options");
    gtk_ui_manager_insert_action_group (eekboard->ui_manager,
                                        eekboard->options_action_group,
                                        -1);
}

static GtkWidget *
create_widget_gtk (Eekboard *eekboard,
                   gint      initial_width,
                   gint      initial_height)
{
    EekBounds bounds;

    bounds.x = bounds.y = 0;
    bounds.width = initial_width;
    bounds.height = initial_height;

    eekboard->keyboard = eek_gtk_keyboard_new ();
    eek_keyboard_set_layout (eekboard->keyboard, eekboard->layout);
    eek_element_set_bounds (EEK_ELEMENT(eekboard->keyboard), &bounds);
    g_signal_connect (eekboard->keyboard, "key-pressed",
                      G_CALLBACK(on_key_pressed), eekboard);
    g_signal_connect (eekboard->keyboard, "key-released",
                      G_CALLBACK(on_key_released), eekboard);

    eekboard->widget =
        eek_gtk_keyboard_get_widget (EEK_GTK_KEYBOARD (eekboard->keyboard));
    eek_element_get_bounds (EEK_ELEMENT(eekboard->keyboard), &bounds);
    eekboard->width = bounds.width;
    eekboard->height = bounds.height;
    return eekboard->widget;
}

#if HAVE_CLUTTER_GTK
#if NEED_SWAP_EVENT_WORKAROUND
static GdkFilterReturn
gtk_clutter_filter_func (GdkXEvent *native_event,
                         GdkEvent  *event,
                         gpointer   user_data)
{
  XEvent *xevent = native_event;

  clutter_x11_handle_event (xevent);

  return GDK_FILTER_CONTINUE;
}

static void
on_gtk_clutter_embed_realize (GtkWidget *widget, gpointer user_data)
{
    gdk_window_add_filter (NULL, gtk_clutter_filter_func, widget);
}
#endif

static GtkWidget *
create_widget_clutter (Eekboard *eekboard,
                       gint      initial_width,
                       gint      initial_height)
{
    ClutterActor *stage, *actor;
    ClutterColor stage_color = { 0xff, 0xff, 0xff, 0xff };
    EekBounds bounds;

    bounds.x = bounds.y = 0;
    bounds.width = initial_width;
    bounds.height = initial_height;

    eekboard->keyboard = eek_clutter_keyboard_new ();
    eek_keyboard_set_layout (eekboard->keyboard, eekboard->layout);
    eek_element_set_bounds (EEK_ELEMENT(eekboard->keyboard), &bounds);
    g_signal_connect (eekboard->keyboard, "key-pressed",
                      G_CALLBACK(on_key_pressed), eekboard);
    g_signal_connect (eekboard->keyboard, "key-released",
                      G_CALLBACK(on_key_released), eekboard);

    eekboard->widget = gtk_clutter_embed_new ();
#if NEED_SWAP_EVENT_WORKAROUND
    if (eekboard->need_swap_event_workaround)
        g_signal_connect (eekboard->widget, "realize",
                          G_CALLBACK(on_gtk_clutter_embed_realize), NULL);
#endif
    stage = gtk_clutter_embed_get_stage (GTK_CLUTTER_EMBED(eekboard->widget));
    clutter_stage_set_color (CLUTTER_STAGE(stage), &stage_color);
    clutter_stage_set_user_resizable (CLUTTER_STAGE (stage), TRUE);

    actor = eek_clutter_keyboard_get_actor
        (EEK_CLUTTER_KEYBOARD(eekboard->keyboard));
    clutter_container_add_actor (CLUTTER_CONTAINER(stage), actor);
    eek_element_get_bounds (EEK_ELEMENT(eekboard->keyboard), &bounds);
    clutter_actor_set_size (stage, bounds.width, bounds.height);
    eekboard->width = bounds.width;
    eekboard->height = bounds.height;
    return eekboard->widget;
}

static GtkWidget *
create_widget (Eekboard *eekboard,
               gint    initial_width,
               gint    initial_height)
{
    if (eekboard->use_clutter)
        return create_widget_clutter (eekboard, initial_width, initial_height);
    else
        return create_widget_gtk (eekboard, initial_width, initial_height);
}
#endif

Eekboard *
eekboard_new (gboolean use_clutter,
              gboolean need_swap_event_workaround,
              gboolean accessibility_enabled)
{
    Eekboard *eekboard;

    eekboard = g_slice_new0 (Eekboard);
    eekboard->use_clutter = use_clutter;
    eekboard->need_swap_event_workaround = need_swap_event_workaround;
    eekboard->accessibility_enabled = accessibility_enabled;
    eekboard->display = GDK_DISPLAY_XDISPLAY (gdk_display_get_default ());
    if (!eekboard->display) {
        g_slice_free (Eekboard, eekboard);
        g_warning ("Can't open display");
        return NULL;
    }

    eekboard->fakekey = fakekey_init (eekboard->display);
    if (!eekboard->fakekey) {
        g_slice_free (Eekboard, eekboard);
        g_warning ("Can't init fakekey");
        return NULL;
    }

    eekboard->layout = eek_xkl_layout_new ();
    if (!eekboard->layout) {
        g_slice_free (Eekboard, eekboard);
        g_warning ("Can't create layout");
        return NULL;
    }
    if (opt_model)
        eek_xkl_layout_set_model (EEK_XKL_LAYOUT(eekboard->layout), opt_model);
    if (opt_layouts) {
        gchar **layouts, **variants;
        gint i;
        layouts = g_strsplit (opt_layouts, ",", -1);
        variants = g_strdupv (layouts);
        for (i = 0; layouts[i]; i++) {
            gchar *layout = layouts[i], *variant = variants[i],
                *variant_start, *variant_end;

            variant_start = strchr (layout, '(');
            variant_end = strrchr (layout, ')');
            if (variant_start && variant_end) {
                *variant_start++ = '\0';
                g_strlcpy (variant, variant_start,
                           variant_end - variant_start + 1);
            } else
                *variant = '\0';
        }
        eek_xkl_layout_set_layouts (EEK_XKL_LAYOUT(eekboard->layout), layouts);
        g_strfreev (layouts);
        g_strfreev (variants);
    }
    if (opt_options) {
        gchar **options;
        options = g_strsplit (opt_options, ",", -1);
        eek_xkl_layout_set_options (EEK_XKL_LAYOUT(eekboard->layout), options);
        g_strfreev (options);
    }
    g_signal_connect (eekboard->layout, "changed",
                      G_CALLBACK(on_changed), eekboard);

    eekboard->ui_manager = gtk_ui_manager_new ();
    eekboard->engine = xkl_engine_get_instance (eekboard->display);
    eekboard->registry = xkl_config_registry_get_instance (eekboard->engine);
    xkl_config_registry_load (eekboard->registry, FALSE);

    return eekboard;
}

static void
eekboard_free (Eekboard *eekboard)
{
    if (eekboard->layout)
        g_object_unref (eekboard->layout);
#if 0
    if (eekboard->keyboard)
        g_object_unref (eekboard->keyboard);
#endif
    if (eekboard->registry)
        g_object_unref (eekboard->registry);
    if (eekboard->engine)
        g_object_unref (eekboard->engine);
    g_slice_free (Eekboard, eekboard);
}

static void
print_layout (XklConfigRegistry   *registry,
              const XklConfigItem *item,
              gpointer             user_data)
{
    GSList *variants = NULL;
    xkl_config_registry_foreach_layout_variant (registry,
                                                item->name,
                                                collect_variant,
                                                &variants);
    if (!variants)
        printf ("%s: %s\n", item->name, item->description);
    else {
        GSList *head;
        for (head = variants; head; head = head->next) {
            XklConfigItem *_item = head->data;
            
            printf ("%s(%s): %s %s\n",
                    item->name,
                    _item->name,
                    item->description,
                    _item->description);
            g_slice_free (XklConfigItem, _item);
        }
        g_slist_free (variants);
    }
}

static void
print_item (XklConfigRegistry *registry,
            const XklConfigItem *item,
            gpointer user_data)
{
    printf ("%s: %s\n", item->name, item->description);
}

static void
print_option_group (XklConfigRegistry *registry,
                    const XklConfigItem *item,
                    gpointer user_data)
{
    xkl_config_registry_foreach_option (registry,
                                        item->name,
                                        print_item,
                                        NULL);
}

static void
on_notify_never_show (NotifyNotification *notification,
                      char *action,
                      gpointer user_data)
{
    Eekboard *eekboard = user_data;
    GError *error;

    gconf_client_set_bool (eekboard->gconfc,
                           "/apps/eekboard/inhibit-startup-notify",
                           TRUE,
                           &error);
}

int
main (int argc, char *argv[])
{
    const gchar *env;
    gboolean use_clutter = USE_CLUTTER;
    gboolean need_swap_event_workaround = FALSE;
    gboolean accessibility_enabled = FALSE;
    Eekboard *eekboard;
    GtkWidget *widget, *vbox, *menubar, *window;
    GOptionContext *context;
    GConfClient *gconfc;
    GError *error;

    context = g_option_context_new ("eekboard");
    g_option_context_add_main_entries (context, options, NULL);
    g_option_context_parse (context, &argc, &argv, NULL);
    g_option_context_free (context);

    if (opt_version) {
        g_print ("eekboard %s\n", VERSION);
        exit (0);
    }

#ifdef ENABLE_NLS
    bindtextdomain (GETTEXT_PACKAGE, EEKBOARD_LOCALEDIR);
    bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
#endif

    error = NULL;
    gconfc = gconf_client_get_default ();
    if (gconf_client_get_bool (gconfc,
                               "/desktop/gnome/interface/accessibility",
                               &error) ||
        gconf_client_get_bool (gconfc,
                               "/desktop/gnome/interface/accessibility2",
                               &error)) {
        if (SPI_init () == 0)
            accessibility_enabled = TRUE;
        else
            g_warning("AT-SPI initialization failed");
    }

    env = g_getenv ("EEKBOARD_DISABLE_CLUTTER");
    if (env && g_strcmp0 (env, "1") == 0)
        use_clutter = FALSE;

#if HAVE_CLUTTER_GTK
    if (use_clutter &&
        gtk_clutter_init (&argc, &argv) != CLUTTER_INIT_SUCCESS) {
        g_warning ("Can't init Clutter-Gtk...fallback to GTK");
        use_clutter = FALSE;
    }
#ifdef NEED_SWAP_EVENT_WORKAROUND
    if (use_clutter &&
        clutter_feature_available (CLUTTER_FEATURE_SWAP_EVENTS)) {
        g_warning ("Enabling GLX_INTEL_swap_event workaround for Clutter-Gtk");
        need_swap_event_workaround = TRUE;
    }
#endif
#endif

    if (!use_clutter && !gtk_init_check (&argc, &argv)) {
        g_warning ("Can't init GTK");
        exit (1);
    }

    eekboard = eekboard_new (use_clutter,
                             need_swap_event_workaround,
                             accessibility_enabled);
    if (opt_list_models) {
        xkl_config_registry_foreach_model (eekboard->registry,
                                           print_item,
                                           NULL);
        eekboard_free (eekboard);
        exit (0);
    }
    if (opt_list_layouts) {
        xkl_config_registry_foreach_layout (eekboard->registry,
                                            print_layout,
                                            NULL);
        eekboard_free (eekboard);
        exit (0);
    }
    if (opt_list_options) {
        xkl_config_registry_foreach_option_group (eekboard->registry,
                                                  print_option_group,
                                                  NULL);
        eekboard_free (eekboard);
        exit (0);
    }

    window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
    gtk_widget_set_can_focus (window, FALSE);
    g_object_set (G_OBJECT(window), "accept_focus", FALSE, NULL);
    gtk_window_set_title (GTK_WINDOW(window), "Keyboard");
    g_signal_connect (G_OBJECT (window), "destroy",
                      G_CALLBACK (gtk_main_quit), NULL);

    vbox = gtk_vbox_new (FALSE, 0);

    g_object_set_data (G_OBJECT(window), "eekboard", eekboard);
    widget = create_widget (eekboard, CSW, CSH);
    
    create_menus (eekboard, window);
    menubar = gtk_ui_manager_get_widget (eekboard->ui_manager, "/MainMenu");
    gtk_box_pack_start (GTK_BOX (vbox), menubar, FALSE, FALSE, 0);

    gtk_container_add (GTK_CONTAINER(vbox), widget);
    gtk_container_add (GTK_CONTAINER(window), vbox);
  
    gtk_widget_set_size_request (widget, eekboard->width, eekboard->height);
    gtk_widget_show_all (window);
    gtk_widget_set_size_request (widget, -1, -1);

    notify_init ("eekboard");
    eekboard->window = window;
    eekboard->gconfc = gconfc;
    if (eekboard->accessibility_enabled) {
        NotifyNotification *notification;

        error = NULL;
        if (!gconf_client_get_bool (eekboard->gconfc,
                                    "/apps/eekboard/inhibit-startup-notify",
                                    &error)) {
            notification = notify_notification_new
                ("eekboard started in background",
                 "As GNOME accessibility support enabled, "
                 "eekboard is starting without a window.\n"
                 "To make eekboard show up, click on some window with "
                 "an editable widget.",
                 "keyboard",
                 NULL);
            notify_notification_add_action
                (notification,
                 "dont-ask",
                 "Don't show up",
                 NOTIFY_ACTION_CALLBACK(on_notify_never_show),
                 eekboard,
                 NULL);
            error = NULL;
            notify_notification_show (notification, &error);
        }
 
        gtk_widget_hide (window);

        focusListener = SPI_createAccessibleEventListener (a11y_focus_listener,
                                                           eekboard);
        SPI_registerGlobalEventListener (focusListener,
                                         "object:state-changed:focused");
    }

    gtk_main ();

    return 0;
}
