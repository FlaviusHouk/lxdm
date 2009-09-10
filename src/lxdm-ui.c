/*
 *      lxdm-ui.c
 *      
 *      Copyright 2009 PCMan <pcman.tw@gmail.com>
 *      
 *      This program is free software; you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation; either version 2 of the License, or
 *      (at your option) any later version.
 *      
 *      This program is distributed in the hope that it will be useful,
 *      but WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *      GNU General Public License for more details.
 *      
 *      You should have received a copy of the GNU General Public License
 *      along with this program; if not, write to the Free Software
 *      Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 *      MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "lxdm.h"

#include <gtk/gtk.h>
#include <glib/gi18n.h>

#define XSESSION_DIR    "/usr/share/xsessions"

static GtkWidget* win;
static GtkWidget* prompt;
static GtkWidget* login_entry;
static GtkWidget* prompt;

static GtkWidget* sessions;
static GtkWidget* lang;

static GtkWidget* exit;

static GtkWidget* exit_menu;

static gboolean get_passwd = FALSE;

static char* user = NULL;
static char* pass = NULL;

static void handle_input()
{
    int ret;
    struct passwd *pw;

    if(!strcmp(user,"reboot"))
    {
        do_reboot();
    }
    else if(!strcmp(user,"shutdown"))
    {
        do_shutdown();
    }
    ret=auth_user(user,pass,&pw);
    if(AUTH_SUCCESS==ret && pw!=NULL)
    {
        char *exec;
        GtkTreeIter it;
        if(gtk_combo_box_get_active_iter(sessions, &it))
        {
            GtkTreeModel* model = gtk_combo_box_get_model(sessions);
            gtk_tree_model_get(model, &it, 1, &exec, -1);
            if(!exec) /* FIXME: is this appropriate? */
                exec = g_strdup("/usr/bin/startlxde");
            do_login(pw,exec);
            g_free(exec);
            gtk_widget_destroy(win);
            gtk_main_quit();
            /* FIXME: is this correct? */
        }
    }
}

static void on_screen_size_changed(GdkScreen* scr, GtkWindow* win)
{
    gtk_window_resize(win, gdk_screen_get_width(scr), gdk_screen_get_height(scr));
}

static void on_entry_activate(GtkEntry* entry, gpointer user_data)
{
    if(!get_passwd)
    {
        user = g_strdup( gtk_entry_get_text(entry) );
        gtk_label_set_text(prompt, _("Password:"));
        gtk_entry_set_text(entry, "");

        gtk_entry_set_visibility(entry, FALSE);
        get_passwd = TRUE;
    }
    else
    {
        pass = g_strdup( gtk_entry_get_text(entry));
        /* FIXME: check password */
        /* login currently selectied session if the passwd is valid. */
        handle_input();

        /* password check failed */
        g_free(pass);
        pass = NULL;

        gtk_label_set_text(prompt, _("User:"));
        gtk_entry_set_text(entry, "");
        gtk_entry_set_visibility(entry, TRUE);
        get_passwd = FALSE;
        g_free(user);
        user = NULL;
    }
}

static void load_sessions()
{
    GtkListStore* list;
    GtkTreeIter it;
    char *path, *file_name, *name, *exec;
    GKeyFile* kf;
    GtkCellRendererText* render;
    GDir* dir = g_dir_open(XSESSION_DIR, 0, NULL);
    if(!dir)
        return;

    list = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_STRING);
    kf = g_key_file_new();
    while(file_name = g_dir_read_name(dir))
    {
        path = g_build_filename(XSESSION_DIR, file_name, NULL);
        if(g_key_file_load_from_file(kf, path, 0, NULL))
        {
            name = g_key_file_get_locale_string(kf, "Desktop Entry", "Name", NULL, NULL);
            exec = g_key_file_get_string(kf, "Desktop Entry", "Exec", NULL);
            gtk_list_store_append(list, &it);
            gtk_list_store_set(list, &it, 0, name, 1, exec, -1);
            g_free(name);
            g_free(exec);
        }
        g_free(path);
    }
    g_dir_close(dir);
    g_key_file_free(kf);

    gtk_list_store_append(list, &it);
    gtk_list_store_set(list, &it, 0, _("Default"), 1, "xterm", -1);

    render = gtk_cell_renderer_text_new();
    gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(sessions), render, TRUE);
    gtk_cell_layout_set_attributes(GTK_CELL_LAYOUT(sessions), render, "text", 0, NULL);

    gtk_combo_box_set_model(sessions, list);
    gtk_combo_box_set_active_iter(sessions, &it);

    g_object_unref(list);
}

static void load_langs()
{
    
}

static void on_exit_clicked(GtkButton* exit_btn, gpointer user_data)
{
    gtk_menu_popup(exit_menu, NULL, NULL, NULL, NULL,
                   0, gtk_get_current_event_time());
}

static void load_exit()
{
    GtkWidget* item;
    exit_menu = gtk_menu_new();
    item = gtk_image_menu_item_new_with_mnemonic(_("_Reboot"));
    gtk_menu_shell_append(exit_menu, item);
    item = gtk_image_menu_item_new_with_mnemonic(_("_Shutdown"));
    gtk_menu_shell_append(exit_menu, item);
    gtk_widget_show_all(exit_menu);
    g_signal_connect(exit, "clicked", G_CALLBACK(on_exit_clicked), NULL);
}

static void create_win()
{
    GtkBuilder* builder;
    GdkScreen* scr;
    builder = gtk_builder_new();
    gtk_builder_add_from_file(builder, CONFIG_DIR "/lxdm.glade", NULL);
    win = (GtkWidget*)gtk_builder_get_object(builder, "lxdm");
    scr = gtk_widget_get_screen(win);
    g_signal_connect(scr, "size-changed", G_CALLBACK(on_screen_size_changed), win);

    prompt = (GtkWidget*)gtk_builder_get_object(builder, "prompt");
    login_entry = (GtkWidget*)gtk_builder_get_object(builder, "login_entry");

    g_signal_connect(login_entry, "activate", G_CALLBACK(on_entry_activate), NULL);


    sessions = (GtkWidget*)gtk_builder_get_object(builder, "sessions");
    load_sessions();

    lang = (GtkWidget*)gtk_builder_get_object(builder, "lang");
    load_langs();

    exit = (GtkWidget*)gtk_builder_get_object(builder, "exit");
    load_exit();

    g_object_unref(builder);

    gtk_window_set_default_size(win, gdk_screen_get_width(scr), gdk_screen_get_height(scr));
    gtk_window_present(win);
}

int gtk_ui_main()
{
    create_win();
    gtk_main();
	return 0;
}

