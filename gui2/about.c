/* 
 *  gretl -- Gnu Regression, Econometrics and Time-series Library
 *  Copyright (C) 2001 Allin Cottrell and Riccardo "Jack" Lucchetti
 * 
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 * 
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 * 
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 * 
 */

#include "gretl.h"
#include "version.h"

#ifdef G_OS_WIN32 
# include "build.h"
#endif

const gchar *copyright = "Copyright (C) 2000-2008 Allin Cottrell and "
                         "Riccardo \"Jack\" Lucchetti";
const gchar *bonmot = N_("\"By econometricians, for econometricians.\"");
const gchar *website = "http://gretl.sourceforge.net/";

gchar *no_gpl_text (void)
{
    return g_strdup_printf(_("Cannot find the license agreement file COPYING. "
			     "Please make sure it's in %s"), 
			   paths.gretldir);
}

static GtkWidget *open_logo (const char *pngname)
{
    char fullname[MAXLEN];
    GdkPixbuf *pbuf;
    GError *error = NULL;
    GtkWidget *image;

    build_path(fullname, paths.gretldir, pngname, NULL);
    pbuf = gdk_pixbuf_new_from_file(fullname, &error);

    if (pbuf == NULL) {
	errbox(error->message);
	g_error_free(error);
	return NULL;
    } else {
	image = gtk_image_new_from_pixbuf(pbuf);
	return image;
    }
}

static void about_table_setup (GtkWidget *vbox, GtkWidget *view)
{
    GtkWidget *sw = gtk_scrolled_window_new(NULL, NULL);

    gtk_box_pack_start(GTK_BOX(vbox), sw, TRUE, TRUE, 0);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw),
				   GTK_POLICY_AUTOMATIC,
				   GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(sw),
					GTK_SHADOW_IN);
    gtk_container_add(GTK_CONTAINER(sw), view); 
    gtk_widget_show(view);
    gtk_widget_show(sw);
}

void about_dialog (gpointer data) 
{
    GtkWidget *notebook, *box, *label, *w;
    GtkWidget *view, *dialog;
    GtkTextBuffer *tbuf;
    GtkTextIter iter;
    gchar *buf;
    const gchar *tr_credit = "";
    FILE *fp;

    dialog = gtk_dialog_new();
    gtk_window_set_title(GTK_WINDOW(dialog),_("About gretl")); 
    gtk_container_set_border_width(GTK_CONTAINER 
				   (GTK_DIALOG(dialog)->vbox), 10);
    gtk_container_set_border_width(GTK_CONTAINER 
				   (GTK_DIALOG(dialog)->action_area), 5);
    gtk_box_set_spacing(GTK_BOX(GTK_DIALOG(dialog)->vbox), 5);
    gtk_window_set_position(GTK_WINDOW(dialog), GTK_WIN_POS_MOUSE);
      
    notebook = gtk_notebook_new();
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->vbox), 
		       notebook, TRUE, TRUE, 0);

    /* construct the first page */
    box = gtk_vbox_new(FALSE, 5);
    gtk_container_set_border_width(GTK_CONTAINER(box), 10);
    gtk_widget_show(box);

    if ((w = open_logo("gretl-logo.xpm"))) {
	gtk_box_pack_start(GTK_BOX(box), w, FALSE, FALSE, 30);
	gtk_widget_show(w);
    }

#ifdef ENABLE_NLS
    if (strcmp(_("translator_credits"), "translator_credits")) {
	tr_credit = _("translator_credits");
    }
#endif  

    buf = 
	g_markup_printf_escaped("<span weight=\"bold\" size=\"xx-large\">"
				"gretl %s</span>\n"
#ifdef G_OS_WIN32
				BUILD_DATE
#endif
				"%s\n%s\n",
				GRETL_VERSION, _(bonmot), website);
    w = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(w), buf);
    g_free(buf);

    gtk_label_set_justify(GTK_LABEL(w), GTK_JUSTIFY_CENTER);
    gtk_box_pack_start(GTK_BOX(box), w, FALSE, FALSE, 0);
    gtk_widget_show(w);


    buf = 
	g_markup_printf_escaped("<span size=\"small\">%s</span>\n"
				"<span size=\"small\">%s</span>",
				copyright, tr_credit);
    w = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(w), buf);
    g_free(buf);

    gtk_label_set_justify(GTK_LABEL(w), GTK_JUSTIFY_CENTER);
    gtk_box_pack_start(GTK_BOX(box), w, FALSE, FALSE, 0);
    gtk_widget_show(w);

    gtk_widget_show(box);

    label = gtk_label_new(_("About"));
    gtk_widget_show(label);
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), box, label);

    /* now the second page */
    box = gtk_vbox_new(FALSE, 5);
    gtk_container_set_border_width(GTK_CONTAINER(box), 10);

    view = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(view), FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(view), GTK_WRAP_NONE);
    gtk_widget_modify_font(GTK_WIDGET(view), fixed_font);

    about_table_setup(box, view);

    gtk_widget_show(box);

    label = gtk_label_new(_("License Agreement"));
    gtk_widget_show(label);
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), box, label);

    w = gtk_button_new_from_stock(GTK_STOCK_OK);
    GTK_WIDGET_SET_FLAGS(w, GTK_CAN_DEFAULT);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->action_area), 
		       w, FALSE, FALSE, 0);
    g_signal_connect(G_OBJECT(w), "clicked", 
		     G_CALLBACK(delete_widget), 
		     dialog);
    gtk_widget_grab_default(w);
    gtk_widget_show(w);

    tbuf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));
    gtk_text_buffer_get_iter_at_offset(tbuf, &iter, 0);

    buf = g_strdup_printf("%s/COPYING", paths.gretldir);

    if ((fp = gretl_fopen(buf, "r")) == NULL) {
	gchar *msg = no_gpl_text();

	gtk_text_buffer_insert(tbuf, &iter, msg, -1);
	g_free(msg);
    } else {
	char chunk[MAXSTR];

	memset(chunk, 0, sizeof chunk);
	while(fread(chunk, 1, sizeof chunk - 1, fp)) {
	    gtk_text_buffer_insert(tbuf, &iter, chunk, -1);
	    memset(chunk, 0, sizeof chunk);
	}
	fclose(fp);
    }

    g_free(buf);
   
    gtk_widget_show(notebook);
    gtk_widget_set_size_request(dialog, 520, -1);
    gtk_widget_show(dialog);
}
