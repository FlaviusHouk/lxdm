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

#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#include <glib/gi18n.h>

#include "lang.h"
#include <time.h>
#include <stdlib.h>
#include <sys/stat.h>

#include "lxcom.h"

enum {
    COL_SESSION_NAME,
    COL_SESSION_EXEC,
    COL_SESSION_DESKTOP_FILE,
    N_SESSION_COLS
};

enum {
    COL_LANG_DISPNAME,
    COL_LANG,
    N_LANG_COLS
};

#define VCONFIG_FILE "/var/lib/lxdm/lxdm.conf"

static GKeyFile *config;
static GKeyFile * var_config;
static GtkWidget* win;
static GtkWidget* prompt;
static GtkWidget* login_entry;
static GtkWidget* prompt;
static GtkWidget* user_list;

static GtkWidget* sessions;
static GtkWidget* lang;

static GtkWidget* exit_btn;

static GtkWidget* exit_menu;
static GtkWidget *lang_menu;

static char* user = NULL;
static char* pass = NULL;
static char* session_exec = NULL;
static char* session_desktop_file = NULL;

static char* ui_file = NULL;

static GdkPixbuf *bg_img = NULL;
static GdkColor bg_color = {0};

static GIOChannel *greeter_io;

static void do_reboot(void)
{
    printf("reboot\n");
}

static void do_shutdown(void)
{
    printf("shutdown\n");
}

static void on_screen_size_changed(GdkScreen* scr, GtkWindow* win)
{
    gtk_window_resize( win, gdk_screen_get_width(scr), gdk_screen_get_height(scr) );
}

static void on_entry_activate(GtkEntry* entry)
{
    char* tmp;
    if( !user )
    {
        user = g_strdup( gtk_entry_get_text( GTK_ENTRY(entry) ) );
        gtk_entry_set_text(GTK_ENTRY(entry), "");
        gtk_label_set_text( GTK_LABEL(prompt), _("Password:") );
        if(strchr(user, ' '))
        {
            g_free(user);
            user = NULL;
            return;
        }
        gtk_entry_set_visibility(entry, FALSE);
    }
    else
    {
        GtkTreeIter it;
        char *session_lang = "";

        if( gtk_combo_box_get_active_iter(GTK_COMBO_BOX(sessions), &it) )
        {
            GtkTreeModel* model = gtk_combo_box_get_model( GTK_COMBO_BOX(sessions) );
            gtk_tree_model_get(model, &it, 1, &session_exec, 2, &session_desktop_file, -1);
        }
        else
        {
            /* FIXME: fatal error */
        }

        pass = g_strdup( gtk_entry_get_text(entry) );
        if( strchr(pass, ' ') )
        {
            g_free(user); user = NULL;
            g_free(pass); pass = NULL;
            gtk_label_set_text( GTK_LABEL(prompt), _("User:") );
            gtk_entry_set_text(GTK_ENTRY(entry), "");
            gtk_entry_set_visibility(GTK_ENTRY(entry), TRUE);
            return;
        }

        if( lang && gtk_combo_box_get_active_iter(GTK_COMBO_BOX(lang), &it) )
        {
            GtkTreeModel* model = gtk_combo_box_get_model( GTK_COMBO_BOX(lang) );
            gtk_tree_model_get(model, &it, 1, &session_lang, -1);
            //FIXME: is session leaked?
        }

        tmp = g_key_file_get_string(var_config, "base", "last_session", NULL);
        if( g_strcmp0(tmp, session_desktop_file) )
        {
            g_key_file_set_string(var_config, "base", "last_session", session_desktop_file);
        }
        g_free(tmp);

        tmp = g_key_file_get_string(var_config, "base", "last_lang", NULL);
        if( g_strcmp0(tmp, session_lang) )
        {
            g_key_file_set_string(var_config, "base", "last_lang", session_lang);
        }
        g_free(tmp);

        printf("login user=%s pass=%s session=%s lang=%s\n",
               user, pass, session_exec, session_lang);

        /* password check failed */
        g_free(user);
        user = NULL;
        g_free(pass);
        pass = NULL;

        gtk_widget_hide(prompt);
        gtk_widget_hide( GTK_WIDGET(entry) );

        gtk_label_set_text( GTK_LABEL(prompt), _("User:") );
        gtk_entry_set_text(GTK_ENTRY(entry), "");
        gtk_entry_set_visibility(GTK_ENTRY(entry), TRUE);
    }
}

static void load_sessions()
{
	GtkListStore* list;
	GtkTreeIter it, active_it = {0};
	char* last;
	char *path, *file_name, *name, *exec;
	GKeyFile* kf;
	GDir* dir = g_dir_open(XSESSIONS_DIR, 0, NULL);
	if( !dir )
		return;

	last = g_key_file_get_string(var_config, "base", "last_session", NULL);

	list = gtk_list_store_new(N_SESSION_COLS, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
	kf = g_key_file_new();
	while( ( file_name = (char*)g_dir_read_name(dir) ) != NULL )
	{
		path = g_build_filename(XSESSIONS_DIR, file_name, NULL);
		if( g_key_file_load_from_file(kf, path, 0, NULL) )
		{
			name = g_key_file_get_locale_string(kf, "Desktop Entry", "Name", NULL, NULL);
			if(!name || !name[0])
			{
				g_free(name);
				g_free(path);
				continue;
			}
            exec = g_key_file_get_string(kf, "Desktop Entry", "Exec", NULL);
			if(!exec || !exec[0])
			{
				/* bad session config file */
				g_free(exec);
				g_free(name);
				g_free(path);
				continue;
			}
			g_free(exec); /* we just test it, and not use it */
            exec=g_strdup(path);

            if( !strcmp(name, "LXDE") )
                gtk_list_store_prepend(list, &it);
            else
                gtk_list_store_append(list, &it);
            gtk_list_store_set(list, &it,
                               COL_SESSION_NAME, name,
                               COL_SESSION_EXEC, exec,
                               COL_SESSION_DESKTOP_FILE, file_name, -1);
            if( last && g_strcmp0(path, last) == 0 )
            {
                active_it = it;
			}

            g_free(name);
            g_free(exec);
        }
        g_free(path);
    }
	g_dir_close(dir);
	g_key_file_free(kf);

	gtk_list_store_prepend(list, &it);
	gtk_list_store_set(list, &it,
					   COL_SESSION_NAME, _("Default"),
					   COL_SESSION_EXEC, "",
					   COL_SESSION_DESKTOP_FILE, "__default__", -1);
	if( last && g_strcmp0("__default__", last) == 0 )
		active_it = it;

	g_free(last);
	gtk_combo_box_set_model( GTK_COMBO_BOX(sessions), GTK_TREE_MODEL(list) );
	gtk_combo_box_entry_set_text_column(GTK_COMBO_BOX_ENTRY(sessions), 0);
	if( active_it.stamp )
		gtk_combo_box_set_active_iter(GTK_COMBO_BOX(sessions), &active_it);
	else
		gtk_combo_box_set_active(GTK_COMBO_BOX(sessions), 0);

	g_object_unref(list);
}

static void load_lang_cb(void *arg, char *lang, char *desc)
{
    GtkListStore* list = (GtkListStore*)arg;
    GtkTreeIter it;
    gchar *temp,*p,*lang2;

    lang2=g_strdup(lang);
    p=strchr(lang2,'.');
    if(p) *p=0;

    if(lang2[0] && lang2[0]!='~')
        temp=g_strdup_printf("%s\t%s",lang2,desc?desc:"");
    else
        temp=g_strdup(desc);
    g_free(lang2);
    gtk_list_store_append(list, &it);
    gtk_list_store_set(list, &it,
                       COL_LANG_DISPNAME, temp,
                       COL_LANG, lang, -1);
    g_free(temp);
}

static gint lang_cmpr(GtkTreeModel *list,GtkTreeIter *a,GtkTreeIter *b,gpointer user_data)
{
	gint ret;
	gchar *as,*bs;
	gtk_tree_model_get(list,a,1,&as,-1);
	gtk_tree_model_get(list,b,1,&bs,-1);
	ret=strcmp(as,bs);
	g_free(as);g_free(bs);
	return ret;
}

static void on_menu_lang_select(GtkMenuItem *item,gpointer user_data)
{
	GtkTreeIter iter;
	char *sel=(char*)user_data;
	int i;
	gboolean res;
	GtkTreeModel *list;
	int active=-1;
	char *temp;
	if(!sel || !sel[0]) return;
	
	list=gtk_combo_box_get_model(GTK_COMBO_BOX(lang));
	res=gtk_tree_model_get_iter_first(GTK_TREE_MODEL(list),&iter);
	for(i=0;res==TRUE;i++)
	{
            gtk_tree_model_get(GTK_TREE_MODEL(list),&iter,1,&temp,-1);
            if(!strcmp(temp,sel))
            {
                 g_free(temp);
                 active=i;
                 break;
            }
            g_free(temp);
            res=gtk_tree_model_iter_next(GTK_TREE_MODEL(list),&iter);
	}
	if(active>=0)
	{
		gtk_combo_box_set_active(GTK_COMBO_BOX(lang),active);
		return;
	}
	gtk_list_store_append((GtkListStore*)list, &iter);
	temp=(char*)gtk_menu_item_get_label(item);
	gtk_list_store_set((GtkListStore*)list, &iter,
                       COL_LANG_DISPNAME, temp,
                       COL_LANG, sel, -1);
	gtk_combo_box_set_active_iter(GTK_COMBO_BOX(lang),&iter);
}

static void load_menu_lang_cb(void *arg, char *lang, char *desc)
{
	GtkWidget *menu=GTK_WIDGET(arg);
	GtkWidget* item;

	gchar *temp,*p,*lang2;

	lang2=g_strdup(lang);
	p=strchr(lang2,'.');
	if(p) *p=0;

	if(lang2[0] && lang2[0]!='~')
        	temp=g_strdup_printf("%s\t%s",lang2,desc?desc:"");
	else
		temp=g_strdup(desc);
	g_free(lang2);

	item = gtk_menu_item_new_with_label(temp);
	g_signal_connect(item, "activate", G_CALLBACK(on_menu_lang_select), g_strdup(lang));
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
	g_free(temp);
}

static void show_all_languages(void)
{
	if(!lang_menu)
	{
		lang_menu=gtk_menu_new();
		lxdm_load_langs(var_config,TRUE,lang_menu,load_menu_lang_cb);
		gtk_widget_show_all(lang_menu);
	}
	gtk_menu_popup(GTK_MENU(lang_menu),NULL,NULL,NULL,NULL,0,gtk_get_current_event_time());
}

static void on_lang_changed(GtkComboBox *widget)
{
	GtkTreeIter it;
	if( gtk_combo_box_get_active_iter(widget, &it) )
	{
		GtkListStore *list=(GtkListStore*)gtk_combo_box_get_model(GTK_COMBO_BOX(lang));
		char *lang=NULL;
		gtk_tree_model_get(GTK_TREE_MODEL(list), &it, 1, &lang, -1);
		if(lang[0]=='~')
		{
			gtk_combo_box_set_active(widget,0);
			show_all_languages();
		}
		g_free(lang);
	}
}

static void load_langs()
{
    GtkListStore* list;
    char* lang_str;
    int active = 0;

    list = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_STRING);
    gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(list),0,GTK_SORT_ASCENDING);
    gtk_tree_sortable_set_sort_func(GTK_TREE_SORTABLE(list),0,lang_cmpr,NULL,NULL);
    lxdm_load_langs(var_config,FALSE,list, load_lang_cb);
    lang_str = g_key_file_get_string(var_config, "base", "last_lang", NULL);
    if(lang_str && lang_str[0])
    {
        gboolean res;
        GtkTreeIter iter;
        int i;
        res=gtk_tree_model_get_iter_first(GTK_TREE_MODEL(list),&iter);
        if(res) for(i=0;;i++)
        {
            gchar *lang;
            gtk_tree_model_get(GTK_TREE_MODEL(list),&iter,1,&lang,-1);
            if(!strcmp(lang,lang_str))
            {
                 g_free(lang);
                 active=i;
                 break;
            }
            g_free(lang);
            res=gtk_tree_model_iter_next(GTK_TREE_MODEL(list),&iter);
            if(!res) break;
        }
    }
    g_free(lang_str);
    gtk_combo_box_set_model( GTK_COMBO_BOX(lang), GTK_TREE_MODEL(list) );
    gtk_combo_box_entry_set_text_column(GTK_COMBO_BOX_ENTRY(lang), 0);
    gtk_combo_box_set_active(GTK_COMBO_BOX(lang), active < 0 ? 0 : active);
    g_object_unref(list);

    g_signal_connect(G_OBJECT(lang),"changed",G_CALLBACK(on_lang_changed),NULL);
}

static void on_exit_clicked(GtkButton* exit_btn, gpointer user_data)
{
    gtk_menu_popup( GTK_MENU(exit_menu), NULL, NULL, NULL, NULL,
                   0, gtk_get_current_event_time() );
}

static void load_exit()
{
    GtkWidget* item;
    exit_menu = gtk_menu_new();
    item = gtk_image_menu_item_new_with_mnemonic( _("_Reboot") );
    g_signal_connect(item, "activate", G_CALLBACK(do_reboot), NULL);
    gtk_menu_shell_append(GTK_MENU_SHELL(exit_menu), item);

    item = gtk_image_menu_item_new_with_mnemonic( _("_Shutdown") );
    g_signal_connect(item, "activate", G_CALLBACK(do_shutdown), NULL);
    gtk_menu_shell_append(GTK_MENU_SHELL(exit_menu), item);

    gtk_widget_show_all(exit_menu);
    g_signal_connect(exit_btn, "clicked", G_CALLBACK(on_exit_clicked), NULL);
}

static gboolean on_expose(GtkWidget* widget, GdkEventExpose* evt, gpointer user_data)
{
    cairo_t *cr;

    if( !GTK_WIDGET_REALIZED(widget) )
        return FALSE;
    cr = gdk_cairo_create(widget->window);
    if( bg_img )
    {
        cairo_matrix_t matrix;
        double x = -0.5, y = -0.5, sx, sy;
        cairo_get_matrix(cr, &matrix);
        sx = (double)gdk_screen_width() / (double)gdk_pixbuf_get_width(bg_img);
        sy = (double)gdk_screen_height() / (double)gdk_pixbuf_get_height(bg_img);
        cairo_scale(cr, sx, sy);
        gdk_cairo_set_source_pixbuf(cr, bg_img, x, y);
        cairo_paint(cr);
        cairo_set_matrix(cr, &matrix);
    }
    else
    {
        gdk_cairo_set_source_color(cr, &bg_color);
        cairo_rectangle( cr, 0, 0, gdk_screen_width(), gdk_screen_height() );
        cairo_fill(cr);
    }
    cairo_destroy(cr);
    return FALSE;
}

static gboolean on_combobox_entry_button_release(GtkWidget* w, GdkEventButton* evt, GtkComboBox* combo)
{
    gboolean shown;
    g_object_get(combo, "popup-shown", &shown, NULL);
    if( shown )
        gtk_combo_box_popdown(combo);
    else
        gtk_combo_box_popup(combo);
    return FALSE;
}

static void fix_combobox_entry(GtkWidget* combo)
{
    GtkWidget* edit = gtk_bin_get_child(GTK_BIN(combo));
    gtk_editable_set_editable( (GtkEditable*)edit, FALSE );
    GTK_WIDGET_UNSET_FLAGS(edit, GTK_CAN_FOCUS);
    g_signal_connect(edit, "button-release-event", G_CALLBACK(on_combobox_entry_button_release), combo);
}

static void on_evt_box_expose(GtkWidget* widget, GdkEventExpose* evt, gpointer user_data)
{
    if (GTK_WIDGET_DRAWABLE (widget))
    {
        GtkWidgetClass* klass = (GtkWidgetClass*)G_OBJECT_GET_CLASS(widget);
        gtk_paint_flat_box (widget->style, widget->window,
                widget->state, GTK_SHADOW_NONE,
                &evt->area, widget, "eventbox",
                0, 0, -1, -1);
        klass->expose_event (widget, evt);
    }
}

static gboolean on_timeout(GtkLabel* label)
{
    char buf[128];
    time_t t;
    struct tm* tmbuf;
    time(&t);
    tmbuf = localtime(&t);
    strftime(buf, 128, "%c", tmbuf);
    gtk_label_set_text(label, buf);
    return TRUE;
}

static void on_user_select(GtkIconView *iconview)
{
	GList *list=gtk_icon_view_get_selected_items(iconview);
	GtkTreeIter iter;
	GtkTreeModel *model=gtk_icon_view_get_model(iconview);
	char *name;
	if(!list) return;
	gtk_tree_model_get_iter(model,&iter,list->data);
	g_list_foreach (list, (GFunc)gtk_tree_path_free, NULL);
	g_list_free (list);
	gtk_tree_model_get(model,&iter,2,&name,-1);
	gtk_widget_hide(user_list);
	gtk_entry_set_text(GTK_ENTRY(login_entry),name);
	g_free(name);
	on_entry_activate(GTK_ENTRY(login_entry));
	gtk_widget_show(login_entry);
	gtk_widget_grab_focus(login_entry);
}

static gboolean load_user_list(GtkWidget *widget)
{
	GtkListStore *model;
	GKeyFile *kf;
	char *res=NULL;
	char **users;
	gsize count;
	int i;
	lxcom_send("/var/run/lxdm/lxdm.sock","USER_LIST",&res);
	if(!res)
	{
		printf("log USER_LIST fail\n");
		return FALSE;
	}
	kf=g_key_file_new();
	if(!g_key_file_load_from_data(kf,res,-1,0,NULL))
	{
		g_key_file_free(kf);
		g_free(res);
		printf("log USER_LIST data bad\n");
		return FALSE;
	}
	g_free(res);
	
	gtk_icon_view_set_selection_mode(GTK_ICON_VIEW(widget),GTK_SELECTION_SINGLE);
	gtk_icon_view_set_pixbuf_column(GTK_ICON_VIEW(widget),0);
	gtk_icon_view_set_markup_column(GTK_ICON_VIEW(widget),1);
	gtk_icon_view_set_orientation(GTK_ICON_VIEW(widget),GTK_ORIENTATION_HORIZONTAL);
	model=gtk_list_store_new(5,GDK_TYPE_PIXBUF,G_TYPE_STRING,
			G_TYPE_STRING,G_TYPE_STRING,G_TYPE_BOOLEAN);
	gtk_icon_view_set_model(GTK_ICON_VIEW(widget),GTK_TREE_MODEL(model));
	g_signal_connect(G_OBJECT(widget),"item-activated",G_CALLBACK(on_user_select),NULL);
	g_signal_connect(G_OBJECT(widget),"selection-changed",G_CALLBACK(on_user_select),NULL);
	
	users=g_key_file_get_groups(kf,&count);
	if(!users || count<=0)
	{
		g_key_file_free(kf);
		printf("USER_LIST 0 user\n");
		return FALSE;
	}
	for(i=0;i<count;i++)
	{
		GtkTreeIter iter;
		char *gecos,*face_path,*display;
		gboolean login;
		GdkPixbuf *face=NULL;
		gtk_list_store_append(model,&iter);
		gecos=g_key_file_get_string(kf,users[i],"gecos",0);
		face_path=g_key_file_get_string(kf,users[i],"face",0);
		login=g_key_file_get_boolean(kf,users[i],"login",0);
		if(face_path)
			face=gdk_pixbuf_new_from_file_at_scale(face_path,48,48,TRUE,NULL);
		if(!face)
		{
			/* TODO: load some default face */
			face=gdk_pixbuf_new_from_file_at_scale(LXDM_DATA_DIR"/nobody.png",48,48,TRUE,NULL);
		}
		display=g_strdup_printf("<span font_size=\"x-large\">%s</span>%s%s%s%s",
			gecos?gecos:users[i],
			(gecos&&strcmp(gecos,users[i]))?"(":"",
			(gecos&&strcmp(gecos,users[i]))?users[i]:"",
			(gecos&&strcmp(gecos,users[i]))?")":"",
			login?_("\n<i>logged in</i>"):"");
		gtk_list_store_set(model,&iter,0,face,1,display,2,users[i],3,gecos,4,login,-1);
		if(face) g_object_unref(G_OBJECT(face));
		g_free(display);
		g_free(gecos);
		g_free(face_path);
	}
	g_strfreev(users);
	g_key_file_free(kf);
	return TRUE;
}

static void create_win()
{
    GtkBuilder* builder;
    GdkScreen* scr;
    GSList* objs, *l;
    GtkWidget* w;

    builder = gtk_builder_new();
    gtk_builder_add_from_file(builder, ui_file ? ui_file : LXDM_DATA_DIR "/lxdm.glade", NULL);
    win = (GtkWidget*)gtk_builder_get_object(builder, "lxdm");

    /* set widget names according to their object id in GtkBuilder xml */
    objs = gtk_builder_get_objects(builder);
    for( l = objs; l; l = l->next )
    {
        GtkWidget* widget = (GtkWidget*)l->data;
        gtk_widget_set_name( widget, gtk_buildable_get_name( (GtkBuildable*)widget ) );
        char* path;
        gtk_widget_path(widget, NULL, &path, NULL);
    }
    g_slist_free(objs);

    if( bg_img ) /* only paint our own background if custom background image is set. */
    {
        GTK_WIDGET_SET_FLAGS(win, GTK_APP_PAINTABLE);
        g_signal_connect(win, "expose-event", G_CALLBACK(on_expose), NULL);
    } /* otherwise, let gtk theme paint it. */

    scr = gtk_widget_get_screen(win);
    g_signal_connect(scr, "size-changed", G_CALLBACK(on_screen_size_changed), win);
    
    user_list=(GtkWidget*)gtk_builder_get_object(builder,"user_list");
    if(user_list)
    {
		//TODO: load user list, connect select_changed,activate signal
		// Use GtkIconView may enough
	}

    prompt = (GtkWidget*)gtk_builder_get_object(builder, "prompt");
    login_entry = (GtkWidget*)gtk_builder_get_object(builder, "login_entry");

    g_signal_connect(login_entry, "activate", G_CALLBACK(on_entry_activate), NULL);

    sessions = (GtkWidget*)gtk_builder_get_object(builder, "sessions");
    gtk_widget_set_name(sessions, "sessions");
    fix_combobox_entry(sessions);
    load_sessions();

    w = (GtkWidget*)gtk_builder_get_object(builder, "bottom_pane");
    if( g_key_file_get_integer(config, "display", "bottom_pane", 0) )
    {
        /* hacks to let GtkEventBox paintable with gtk pixmap engine. */
        if(GTK_WIDGET_APP_PAINTABLE(w))
            g_signal_connect(w, "expose-event", G_CALLBACK(on_evt_box_expose), NULL);
    }
    else
        gtk_event_box_set_visible_window(GTK_EVENT_BOX(w), FALSE);

    if( g_key_file_get_integer(config, "display", "lang", 0) == 0 )
    {
        GtkWidget *w;
        w = (GtkWidget*)gtk_builder_get_object(builder, "lang_box");
        if( w )
            gtk_widget_hide(w);
    }
    else
    {
        lang = (GtkWidget*)gtk_builder_get_object(builder, "lang");
        gtk_widget_set_name(lang, "lang");
        fix_combobox_entry(lang);
        load_langs();
    }

	if( (w = (GtkWidget*)gtk_builder_get_object(builder, "time"))!=NULL )
	{
		guint timeout = g_timeout_add(1000, (GSourceFunc)on_timeout, w);
		g_signal_connect_swapped(w, "destroy",
			G_CALLBACK(g_source_remove), GUINT_TO_POINTER(timeout));
		on_timeout((GtkLabel*)w);
	}

	exit_btn = (GtkWidget*)gtk_builder_get_object(builder, "exit");
	load_exit();

	g_object_unref(builder);

	gtk_window_set_default_size( GTK_WINDOW(win), gdk_screen_get_width(scr), gdk_screen_get_height(scr) );
	gtk_window_present( GTK_WINDOW(win) );
	gtk_widget_realize(login_entry);
    
	if(user_list && load_user_list(user_list))
	{
		gtk_widget_hide(login_entry);
	}
	else
	{
		if(user_list)
		{
			gtk_widget_hide(user_list);
			user_list=NULL;
		}
		gtk_widget_grab_focus(login_entry);
	}
}

int set_background(void)
{
    char *bg;
    char *style;
    GdkWindow* root = gdk_get_default_root_window();
    GdkCursor* cursor = gdk_cursor_new(GDK_LEFT_PTR);

    gdk_window_set_cursor(root, cursor);

    bg = g_key_file_get_string(config, "display", "bg", 0);
    if( !bg )
        bg = g_strdup("#222E45");
    style = g_key_file_get_string(config, "display", "bg_style", 0);

    if( bg )
    {
        if( bg[0] != '#' )
        {
            /* default the bg stretch */
            if(!style || strcmp(style, "stretch") == 0 )
            {
                GdkPixbuf *tmp=gdk_pixbuf_new_from_file(bg,0);
                if(tmp)
                {
                    bg_img=gdk_pixbuf_scale_simple(tmp,
                                                    gdk_screen_width(),
                                                    gdk_screen_height(),
                                                    GDK_INTERP_HYPER);
                    g_object_unref(tmp);
                }
            }
            else
            {
                bg_img = gdk_pixbuf_new_from_file(bg, 0);
            }
            if( !bg_img )
            {
                g_free(bg);
                bg = g_strdup("#222E45");
            }
        }
        if( bg[0] == '#' )
            gdk_color_parse(bg, &bg_color);
    }
    g_free(bg);
    g_free(style);
    return 0;
}

static gboolean on_lxdm_command(GIOChannel *source, GIOCondition condition, gpointer data)
{
    GIOStatus ret;
    char *str;

    if( !(G_IO_IN & condition) )
        return FALSE;
    ret = g_io_channel_read_line(source, &str, NULL, NULL, NULL);
    if( ret != G_IO_STATUS_NORMAL )
        return FALSE;

    if( !strncmp(str, "quit", 4) || !strncmp(str, "exit",4))
        gtk_main_quit();
    else if( !strncmp(str, "reset", 5) )
    {
        gtk_widget_show(prompt);
        if(user_list)
        {
			gtk_widget_hide(login_entry);
			gtk_icon_view_unselect_all(GTK_ICON_VIEW(user_list));
			gtk_widget_show(user_list);
		}
        else
        {
			gtk_widget_show(login_entry);
        	gtk_widget_grab_focus(login_entry);
		}
    }
    g_free(str);
    return TRUE;
}

void listen_stdin(void)
{
    greeter_io = g_io_channel_unix_new(0);
    g_io_add_watch(greeter_io, G_IO_IN, on_lxdm_command, NULL);
}

void set_root_background(void)
{
    GdkWindow *root = gdk_get_default_root_window();

    /* set background */
    if( !bg_img )
    {
        GdkColormap *map = (GdkColormap*)gdk_window_get_colormap(root);
        gdk_color_alloc(map, &bg_color);
        gdk_window_set_background(root, &bg_color);
    }
    else
    {
        GdkPixmap *pix = NULL;
        gdk_pixbuf_render_pixmap_and_mask(bg_img, &pix, NULL, 0);
        /* call x directly, because gdk will ref the pixmap */
        //gdk_window_set_back_pixmap(root,pix,FALSE);
        XSetWindowBackgroundPixmap( GDK_WINDOW_XDISPLAY(root),
                                   GDK_WINDOW_XID(root), GDK_PIXMAP_XID(pix) );
        g_object_unref(pix);
    }
    gdk_window_clear(root);
}

static void apply_theme(const char* theme_name)
{
    char* theme_dir = g_build_filename(LXDM_DATA_DIR "/themes", theme_name, NULL);
    char* rc = g_build_filename(theme_dir, "gtkrc", NULL);

    ui_file = g_build_filename(theme_dir, "greeter.ui", NULL);

    if( g_file_test(rc, G_FILE_TEST_EXISTS) )
    {
        //g_debug("%s", rc);
        gtk_rc_parse(rc);
    }
    g_free(rc);

    if( !g_file_test(ui_file, G_FILE_TEST_EXISTS) )
    {
        g_free(ui_file);
        ui_file = NULL;
    }
    g_free(theme_dir);
}

int main(int arc, char *arg[])
{
    char* theme_name;
    GtkSettings*p;

    gtk_set_locale();
    bindtextdomain("lxdm", "/usr/share/locale");
    textdomain("lxdm");

    config = g_key_file_new();
    g_key_file_load_from_file(config, CONFIG_FILE, G_KEY_FILE_KEEP_COMMENTS, NULL);

    var_config = g_key_file_new();
    g_key_file_set_list_separator(var_config, ' ');
    g_key_file_load_from_file(var_config,VCONFIG_FILE,G_KEY_FILE_KEEP_COMMENTS, NULL);

    gtk_init(&arc, &arg);

    p=gtk_settings_get_default();
    if(p)
    {
        setenv("GTK_IM_MODULE","gtk-im-context-simple",1);
        gtk_settings_set_string_property(p,"gtk-im-module","gtk-im-context-simple",0);
        gtk_settings_set_long_property(p,"gtk-show-input-method-menu",0,0);
    }

    set_background();
    set_root_background();

    /* set gtk+ theme */
    theme_name = g_key_file_get_string(config, "display", "gtk_theme", NULL);
    if( theme_name )
    {
        GtkSettings* settings = gtk_settings_get_default();
        g_object_set(settings, "gtk-theme-name", theme_name, NULL);
        g_free(theme_name);
    }

    /* load gtkrc-based themes */
    theme_name = g_key_file_get_string(config, "display", "theme", NULL);
    if( theme_name ) /* theme is specified */
    {
        apply_theme(theme_name);
        g_free(theme_name);
    }

    /* create the login window */
    create_win();
    listen_stdin();
    /* use line buffered stdout for inter-process-communcation of
     * single-line-commands */
    setvbuf(stdout, NULL, _IOLBF, 0 );

    gtk_main();

    g_key_file_free(config);
    g_key_file_free(var_config);

    return 0;
}
