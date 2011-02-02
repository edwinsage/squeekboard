/* 
 * Copyright (C) 2011 Daiki Ueno <ueno@unixuser.org>
 * Copyright (C) 2011 Red Hat, Inc.
 * 
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 * 
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif  /* HAVE_CONFIG_H */

#include <string.h>
#include <stdlib.h>
#include <gdk/gdkx.h>

#include "eek/eek-xml.h"
#include "eek/eek-xkl.h"
#include "eek/eek-gtk.h"

#include "xklutil.h"

#define BUFSIZE 8192

static gchar *opt_load = NULL;
static gboolean opt_dump = FALSE;
static gchar *opt_model = NULL;
static gchar *opt_layouts = NULL;
static gchar *opt_options = NULL;
static gchar *opt_list = NULL;

static const GOptionEntry options[] = {
    {"load", 'l', 0, G_OPTION_ARG_STRING, &opt_load,
     "Show the keyboard loaded from an XML file"},
    {"dump", 'd', 0, G_OPTION_ARG_NONE, &opt_dump,
     "Dump the current layout as XML"},
    {"list", 'L', 0, G_OPTION_ARG_STRING, &opt_list,
     "List configuration items for given spec"},
    {"model", '\0', 0, G_OPTION_ARG_STRING, &opt_model,
     "Specify model"},
    {"layouts", '\0', 0, G_OPTION_ARG_STRING, &opt_layouts,
     "Specify layouts"},
    {"options", '\0', 0, G_OPTION_ARG_STRING, &opt_options,
     "Specify options"},
    {NULL}
};

static void
on_destroy (gpointer user_data)
{
    gtk_main_quit ();
}

static void
print_item (gpointer data,
            gpointer user_data)
{
    XklConfigItem *item = data;
    g_assert (item);
    printf ("%s: %s\n", item->name, item->description);
}

int
main (int argc, char **argv)
{
    GOptionContext *context;

    if (!gtk_init_check (&argc, &argv)) {
        g_printerr ("Can't init GTK\n");
        exit (1);
    }

    context = g_option_context_new ("eek-example-xml");
    g_option_context_add_main_entries (context, options, NULL);
    g_option_context_parse (context, &argc, &argv, NULL);
    g_option_context_free (context);

    if (opt_load) {
        GFile *file;
        GFileInputStream *input;
        EekLayout *layout;
        EekKeyboard *keyboard;
        EekBounds bounds;
        GtkWidget *widget, *window;
        GError *error;

        file = g_file_new_for_path (opt_load);

        error = NULL;
        input = g_file_read (file, NULL, &error);
        if (error) {
            g_printerr ("Can't read file %s: %s\n", opt_load, error->message);
            exit (1);
        }

        layout = eek_xml_layout_new (G_INPUT_STREAM(input));
        g_object_unref (input);
        keyboard = eek_keyboard_new (layout, 640, 480);
        g_object_unref (layout);

        widget = eek_gtk_keyboard_new (keyboard);
        g_object_unref (keyboard);

        eek_element_get_bounds (EEK_ELEMENT(keyboard), &bounds);
        gtk_widget_set_size_request (widget, bounds.width, bounds.height);

        window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
        gtk_container_add (GTK_CONTAINER(window), widget);
        gtk_widget_show_all (window);

        g_signal_connect (G_OBJECT (window), "destroy",
                          G_CALLBACK (on_destroy), NULL);

        gtk_main ();
        exit (0);
    } else if (opt_dump) {
        GString *output;
        EekLayout *layout;
        EekKeyboard *keyboard;

        output = g_string_sized_new (BUFSIZE);
        layout = eek_xkl_layout_new ();

        if (opt_model)
            eek_xkl_layout_set_model (EEK_XKL_LAYOUT(layout), opt_model);

        if (opt_layouts) {
            XklConfigRec *rec;

            rec = eekboard_xkl_config_rec_new_from_string (opt_layouts);
            eek_xkl_layout_set_layouts (EEK_XKL_LAYOUT(layout), rec->layouts);
            eek_xkl_layout_set_variants (EEK_XKL_LAYOUT(layout), rec->variants);
            g_object_unref (rec);
        }

        if (opt_options) {
            gchar **options;
            options = g_strsplit (opt_options, ",", -1);
            eek_xkl_layout_set_options (EEK_XKL_LAYOUT(layout), options);
            g_strfreev (options);
        }

        keyboard = eek_keyboard_new (layout, 640, 480);
        g_object_unref (layout);
        eek_keyboard_output (keyboard, output, 0);
        g_object_unref (keyboard);
        fwrite (output->str, sizeof(gchar), output->len, stdout);
        g_string_free (output, TRUE);
        exit (0);
    } else if (opt_list) {
        GdkDisplay *display;
        XklEngine *engine;
        XklConfigRegistry *registry;
        GSList *items = NULL, *head;

        display = gdk_display_get_default ();
        engine = xkl_engine_get_instance (GDK_DISPLAY_XDISPLAY(display));
        registry = xkl_config_registry_get_instance (engine);
        xkl_config_registry_load (registry, FALSE);

        if (g_strcmp0 (opt_list, "model") == 0) {
            items = eekboard_xkl_list_models (registry);
        } else if (g_strcmp0 (opt_list, "layout") == 0) {
            items = eekboard_xkl_list_layouts (registry);
        } else if (g_strcmp0 (opt_list, "option-group") == 0) {
            items = eekboard_xkl_list_option_groups (registry);
        } else if (g_str_has_prefix (opt_list, "layout-variant-")) {
            items =  eekboard_xkl_list_layout_variants
                (registry,
                 opt_list + strlen ("layout-variant-"));
        } else if (g_str_has_prefix (opt_list, "option-")) {
            items = eekboard_xkl_list_options
                (registry,
                 opt_list + strlen ("option-"));
        } else {
            g_printerr ("Unknown list spec \"%s\"\n", opt_list);
        }
        g_slist_foreach (items, print_item, NULL);
        for (head = items; head; head = g_slist_next (head))
            g_object_unref (head->data);
        g_slist_free (items);
        g_object_unref (engine);
        g_object_unref (registry);
        exit (0);
    } else {
        g_printerr ("Specify -l, -d, or -L option\n");
        exit (1);
    }

    return 0;
}