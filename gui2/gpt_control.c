/*
 *  Copyright (c) by Allin Cottrell
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

/* gpt_control.c for gretl -- GUI gnuplot controller */

#include "gretl.h"
#include "gpt_control.h"

#ifdef G_OS_WIN32
# include <windows.h>
# include <io.h>
#endif

#ifdef GNUPLOT_PNG
# include <gdk-pixbuf/gdk-pixbuf.h>
# include <gdk/gdkkeysyms.h>
#endif

struct gpt_titles_t {
    char *description; /* How the field will show up in the options dialog */
    short tab;         /* which tab (if any) does the item fall under? */
    GtkWidget *widget;
};

typedef struct {
    gint ID;
    GtkWidget *isauto;
    GtkWidget *min;
    GtkWidget *max;
} GPT_RANGE;

GtkWidget *linetitle[6];
GtkWidget *stylecombo[6];
GtkWidget *yaxiscombo[6];
GtkWidget *linescale[6];
static GtkWidget *gpt_control;
static GtkWidget *keycombo;
static GtkWidget *termcombo;
GtkWidget *filesavebutton;

GPT_RANGE axis_range[3];

#define NTITLES 4

struct gpt_titles_t gpt_titles[] = {
    { N_("Title of plot"),  0, NULL },
    { N_("Title for axis"), 1, NULL },
    { N_("Title for axis"), 2, NULL },
    { N_("Title for axis"), 3, NULL },
};

typedef enum {
    PLOT_SAVED          = 1 << 0,
    PLOT_RANGE_MEAN     = 1 << 1,
    PLOT_HAS_CONTROLLER = 1 << 3,
    PLOT_ZOOMED         = 1 << 4,
    PLOT_ZOOMING        = 1 << 5
} plot_flags;

#define plot_is_saved(p)        (p->flags & PLOT_SAVED)
#define plot_is_range_mean(p)   (p->flags & PLOT_RANGE_MEAN)
#define plot_has_controller(p)  (p->flags & PLOT_HAS_CONTROLLER)
#define plot_is_zoomed(p)       (p->flags & PLOT_ZOOMED)
#define plot_is_zooming(p)      (p->flags & PLOT_ZOOMING)

#ifdef GNUPLOT_PNG
typedef enum {
    PNG_START,
    PNG_ZOOM,
    PNG_UNZOOM,
    PNG_REDISPLAY
} png_zoom;

typedef struct zoom_t {
    double xmin, xmax;
    double ymin, ymax;
    int screen_xmin, screen_ymin;
} zoom_t;

typedef struct png_plot_t {
    GtkWidget *shell;
    GtkWidget *canvas;
    GtkWidget *popup;
    GtkWidget *statusarea;    
    GtkWidget *statusbar;
    GtkWidget *cursor_label;
    GdkPixmap *pixmap;
    GdkGC *invert_gc;
    GPT_SPEC *spec;
    double xmin, xmax;
    double ymin, ymax;
    int xint, yint;
    int pd;
    int title;
    guint cid;
    zoom_t *zoom;
    unsigned char flags;    
} png_plot_t;

static void render_pngfile (png_plot_t *plot, int view);
static int zoom_unzoom_png (png_plot_t *plot, int view);
static int redisplay_edited_png (png_plot_t *plot);
#endif /* GNUPLOT_PNG */

/* ........................................................... */

static void close_plot_controller (GtkWidget *widget, gpointer data) 
{
    GPT_SPEC *spec = (GPT_SPEC *) data;
#ifdef GNUPLOT_PNG
    png_plot_t *plot = (png_plot_t *) spec->ptr;
#endif

    gpt_control = NULL;

#ifdef GNUPLOT_PIPE
    pclose(spec->fp);
    free_plotspec(spec); 
#else
    if (plot != NULL) { /* PNG plot window open */
	plot->flags ^= PLOT_HAS_CONTROLLER;
    } else {
	free_plotspec(spec); 
    }
#endif
}     

/* ........................................................... */

static void flip_manual_range (GtkWidget *widget, gpointer data)
{
    gint axis = GPOINTER_TO_INT(data);

    if (GTK_TOGGLE_BUTTON (axis_range[axis].isauto)->active) {
	gtk_widget_set_sensitive(GTK_WIDGET(axis_range[axis].min), FALSE);
	gtk_widget_set_sensitive(GTK_WIDGET(axis_range[axis].max), FALSE);
    } else {
	gtk_widget_set_sensitive(GTK_WIDGET(axis_range[axis].min), TRUE);
	gtk_widget_set_sensitive(GTK_WIDGET(axis_range[axis].max), TRUE);
    }
}

/* ........................................................... */

static void widget_to_str (GtkWidget *w, char *str, size_t n)
{
    const gchar *tmp;
    
    *str = 0;
    tmp = gtk_entry_get_text(GTK_ENTRY(w));

    if (tmp != NULL && strlen(tmp)) {
#ifdef ENABLE_NLS
	gchar *trstr;
	gsize bytes;

	trstr = g_locale_from_utf8(tmp, -1, NULL, &bytes, NULL);
	strncat(str, trstr, n-1);
	g_free(trstr);
#else
	strncat(str, tmp, n-1);
#endif
    }
}

/* ........................................................... */

#ifdef GNUPLOT_PNG

static int add_or_remove_png_term (const char *fname, int add)
{
    FILE *fs, *fd;
    char tmp[MAXLEN], fline[MAXLEN];

    sprintf(tmp, "%sgpttmp.XXXXXX", paths.userdir);
    if (mktemp(tmp) == NULL) return 1;

    fs = fopen(fname, "r");
    if (!fs) {
	sprintf(errtext, _("Couldn't open %s"), fname);
	errbox(errtext);
	return 1;
    }

    fd = fopen(tmp, "w");
    if (!fd) {
	sprintf(errtext, _("Couldn't write to %s"), tmp);
	errbox(errtext);
	fclose(fs);
	return 1;
    }

    if (add) {
	fprintf(fd, "set term png\n");
	fprintf(fd, "set output '%sgretltmp.png'\n", paths.userdir);
    }

    while (fgets(fline, MAXLEN-1, fs)) {
	if (add || (strncmp(fline, "set term", 8) && 
	    strncmp(fline, "set output", 10))) {
	    fputs(fline, fd);
	}
    }

    fclose(fs);
    fclose(fd);

    return rename(tmp, fname);
}

static int add_png_term_to_plotfile (const char *fname)
{
    return add_or_remove_png_term(fname, 1);
}

int remove_png_term_from_plotfile (const char *fname)
{
    return add_or_remove_png_term(fname, 0);
}

void mark_plot_as_saved (GPT_SPEC *spec)
{
    png_plot_t *plot = (png_plot_t *) spec->ptr;

    plot->flags |= PLOT_SAVED;
}

static int gnuplot_png_init (const char *fname, FILE **fpp)
{
    *fpp = fopen(fname, "w");
    if (*fpp == NULL) {
	sprintf(errtext, _("Couldn't write to %s"), fname);
	errbox(errtext);
	return 1;
    }
    fprintf(*fpp, "set term png\n");
    fprintf(*fpp, "set output '%sgretltmp.png'\n", paths.userdir);
    return 0;
}

void display_session_graph_png (const char *fname) 
{
    gchar *plotcmd;
    int err = 0;

    /* take saved plot source file and make PNG from it, then display
       the PNG */
    if (add_png_term_to_plotfile(fname)) return;

    plotcmd = g_strdup_printf("\"%s\" \"%s\"", paths.gnuplot, fname);
#ifdef G_OS_WIN32
    err = winfork(plotcmd, NULL, SW_SHOWMINIMIZED, 0);
#else
    err = system(plotcmd);
#endif
    g_free(plotcmd);

    if (err) {
	errbox(_("Gnuplot error creating graph"));
    } else {
	gnuplot_show_png(fname, NULL, 1);
    }
}

#endif /* GNUPLOT_PNG */

/* ........................................................... */

static void apply_gpt_changes (GtkWidget *widget, GPT_SPEC *spec) 
{
    const gchar *yaxis;
    int i, save = 0, k, numlines;

    numlines = spec->list[0] - 1;
    if (widget == filesavebutton) {
	widget_to_str(GTK_COMBO(termcombo)->entry, spec->termtype, 
		      sizeof spec->termtype);
	if (strcmp(spec->termtype, "screen")) save = 1;
    }
   
    for (i=0; i<NTITLES; i++) {
	if (gpt_titles[i].widget != NULL) {
	    widget_to_str(gpt_titles[i].widget, spec->titles[i], 
			  sizeof spec->titles[0]);
	}
    }

    widget_to_str(GTK_COMBO(keycombo)->entry, spec->keyspec, 
		  sizeof spec->keyspec);

    spec->y2axis = 0;
    for (i=0; i<numlines; i++) {
	spec->lines[i].yaxis = 1;
	yaxis = 
	    gtk_entry_get_text(GTK_ENTRY(GTK_COMBO(yaxiscombo[i])->entry));
	if (yaxis != NULL && strlen(yaxis) && !strcmp(yaxis, "right"))
	    spec->lines[i].yaxis = 2;	
	if (spec->lines[i].yaxis == 2) spec->y2axis = 1;
    }

    k = (spec->y2axis)? 3 : 2;
    for (i=0; i<k; i++) {
	if (axis_range[i].isauto != NULL) {
	    if (GTK_TOGGLE_BUTTON (axis_range[i].isauto)->active) {
		strcpy(spec->range[i][0], "*");
		strcpy(spec->range[i][1], "*");
	    } else {
		widget_to_str(axis_range[i].min, spec->range[i][0], 
			      sizeof spec->range[0][0]);
		widget_to_str(axis_range[i].max, spec->range[i][1], 
			      sizeof spec->range[0][1]);
	    }
	}
    }

    for (i=0; i<numlines; i++) {
	widget_to_str(GTK_COMBO(stylecombo[i])->entry, 
		      spec->lines[i].style, sizeof spec->lines[0].style);
	widget_to_str(linetitle[i], 
		      spec->lines[i].title, sizeof spec->lines[0].title);
	widget_to_str(linescale[i], spec->lines[i].scale, 
		      sizeof spec->lines[0].scale);
    }

#ifdef GNUPLOT_PIPE
    if (spec->edit == 2 || spec->edit == 3) {  /* silent update */
	spec->edit -= 2;
	return;
    }
#endif

    if (save) { /* do something other than a screen graph? */
	file_selector(_("Save gnuplot graph"), SAVE_GNUPLOT, spec);
    } else { 
#if defined(GNUPLOT_PNG) && !defined(GNUPLOT_PIPE)
	png_plot_t *plot = (png_plot_t *) spec->ptr;

	redisplay_edited_png(plot);
#else
	go_gnuplot(spec, NULL, &paths);
#endif /* GNUPLOT_PNG */
    }

    session_changed(1);
}

/* ........................................................... */

#ifdef GNUPLOT_PIPE
static void save_session_graph_plotspec (GtkWidget *w, GPT_SPEC *spec)
{
    int err = 0;

    spec->edit += 2;
    apply_gpt_changes(NULL, spec);

    strcpy(spec->termtype, "plot commands");

    plot->edit += 2;
    err = go_gnuplot(spec, spec->fname, &paths);

    if (err == 1) {
	errbox(_("Error saving graph"));
    } else {
	infobox(_("graph saved"));
    }
}
#endif

/* ........................................................... */

static void gpt_tab_main (GtkWidget *notebook, GPT_SPEC *spec) 
{
    GtkWidget *tempwid, *box, *tbl;
    int i, tbl_len;
    GList *keypos = NULL;

    gchar *key_positions[] = {
	"left top",
	"right top",
	"left bottom",
	"right bottom",
	"outside",
	"none"
    };

    for (i=0; i<6; i++) {
	keypos = g_list_append(keypos, key_positions[i]);
    }
   
    box = gtk_vbox_new (FALSE, 0);
    gtk_container_set_border_width (GTK_CONTAINER (box), 10);
    gtk_widget_show(box);
    
    tempwid = gtk_label_new (_("Main"));
    gtk_widget_show (tempwid);
    gtk_notebook_append_page (GTK_NOTEBOOK (notebook), box, tempwid);   

    tbl_len = 1;
    tbl = gtk_table_new (tbl_len, 2, FALSE);
    gtk_table_set_row_spacings (GTK_TABLE (tbl), 5);
    gtk_table_set_col_spacings (GTK_TABLE (tbl), 5);
    gtk_box_pack_start (GTK_BOX (box), tbl, FALSE, FALSE, 0);
    gtk_widget_show (tbl);
   
    for (i=0; i<NTITLES; i++) {
	if (gpt_titles[i].tab == 0) {
	    gsize bytes;
	    gchar *titlestr;

	    tbl_len++;
	    gtk_table_resize(GTK_TABLE(tbl), tbl_len, 2);
	    tempwid = gtk_label_new(_(gpt_titles[i].description));
	    gtk_misc_set_alignment(GTK_MISC (tempwid), 1, 0.5);
	    gtk_table_attach_defaults(GTK_TABLE (tbl), 
				      tempwid, 0, 1, tbl_len-1, tbl_len);
	    gtk_widget_show(tempwid);
	    tempwid = gtk_entry_new();
	    gtk_table_attach_defaults(GTK_TABLE(tbl), 
				      tempwid, 1, 2, tbl_len-1, tbl_len);
	    titlestr = g_locale_to_utf8(spec->titles[i], -1, NULL,
					&bytes, NULL);
	    gtk_entry_set_text(GTK_ENTRY(tempwid), titlestr);
	    g_free(titlestr);
	    g_signal_connect(G_OBJECT(tempwid), "activate", 
			     G_CALLBACK(apply_gpt_changes), 
			     spec);
	    gtk_widget_show (tempwid);
	    gpt_titles[i].widget = tempwid;
	}
    }
    tbl_len++;

    tempwid = gtk_label_new(_("key position"));
    gtk_table_attach_defaults(GTK_TABLE(tbl), 
			      tempwid, 0, 1, tbl_len-1, tbl_len);
    gtk_widget_show(tempwid);

    keycombo = gtk_combo_new();
    gtk_table_attach_defaults(GTK_TABLE(tbl), 
			      keycombo, 1, 2, tbl_len-1, tbl_len);
    gtk_combo_set_popdown_strings(GTK_COMBO(keycombo), keypos); 
    gtk_entry_set_text(GTK_ENTRY(GTK_COMBO(keycombo)->entry), spec->keyspec);
    gtk_widget_show (keycombo);	
}

/* ........................................................... */

static void gpt_tab_output (GtkWidget *notebook, GPT_SPEC *spec) 
{
    GtkWidget *tempwid, *box, *tbl;
    int i, tbl_len;
    GList *termtype = NULL;
    gchar *terminal_types[] = {
	"postscript",
	"fig",
	"latex",
	"png",
	"plot commands"
    };  

    for (i=0; i<5; i++)
	termtype = g_list_append(termtype, terminal_types[i]);
   
    box = gtk_vbox_new (FALSE, 0);
    gtk_container_set_border_width (GTK_CONTAINER (box), 10);
    gtk_widget_show(box);
    
    tempwid = gtk_label_new (_("Output to file"));
    gtk_widget_show (tempwid);
    gtk_notebook_append_page (GTK_NOTEBOOK (notebook), box, tempwid);   

    tbl_len = 1;
    tbl = gtk_table_new (tbl_len, 2, FALSE);
    gtk_table_set_row_spacings (GTK_TABLE (tbl), 5);
    gtk_table_set_col_spacings (GTK_TABLE (tbl), 5);
    gtk_box_pack_start (GTK_BOX (box), tbl, FALSE, FALSE, 0);
    gtk_widget_show (tbl);
   
    tbl_len++;
    tempwid = gtk_label_new(_("output format"));
    gtk_table_attach_defaults(GTK_TABLE(tbl), 
			      tempwid, 0, 1, tbl_len-1, tbl_len);
    gtk_widget_show(tempwid);

    termcombo = gtk_combo_new();
    gtk_table_attach_defaults(GTK_TABLE(tbl), 
			      termcombo, 1, 2, tbl_len-1, tbl_len);
    gtk_combo_set_popdown_strings(GTK_COMBO(termcombo), termtype);   
    gtk_widget_show(termcombo);

    /* button to generate output to file */
    filesavebutton = gtk_button_new_with_label (_("Save to file..."));
    GTK_WIDGET_SET_FLAGS(filesavebutton, GTK_CAN_DEFAULT);
    tbl_len++;
    gtk_table_attach_defaults(GTK_TABLE(tbl), 
			      filesavebutton, 1, 2, tbl_len-1, tbl_len);
    g_signal_connect (G_OBJECT(filesavebutton), "clicked", 
		      G_CALLBACK(apply_gpt_changes), 
		      spec);
    gtk_widget_grab_default(filesavebutton);
    gtk_widget_show(filesavebutton);    
}

/* ........................................................... */

static void gpt_tab_lines (GtkWidget *notebook, GPT_SPEC *spec) 
{
    GtkWidget *tempwid, *box, *tbl;
    int i, tbl_len, tbl_num, tbl_col, numlines;
    char label_text[32];
    GList *plot_types = NULL;
    GList *yaxis_loc = NULL;

    numlines = spec->list[0] - 1;

    if (spec->ts) {
	plot_types = g_list_append(plot_types, "lines");
	plot_types = g_list_append(plot_types, "points");
    } else {
	plot_types = g_list_append(plot_types, "points");
	plot_types = g_list_append(plot_types, "lines");
    }

    plot_types = g_list_append(plot_types, "linespoints"); 
    plot_types = g_list_append(plot_types, "impulses");
    plot_types = g_list_append(plot_types, "dots");

    yaxis_loc = g_list_append(yaxis_loc, "left");
    yaxis_loc = g_list_append(yaxis_loc, "right");

    box = gtk_vbox_new(FALSE, 0);
    gtk_container_set_border_width(GTK_CONTAINER (box), 10);
    gtk_widget_show(box);

    tempwid = gtk_label_new(_("Lines"));

    gtk_widget_show(tempwid);
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), box, tempwid);   

    tbl_len = 1;
    tbl = gtk_table_new(tbl_len, 3, FALSE);
    gtk_table_set_row_spacings(GTK_TABLE(tbl), 5);
    gtk_table_set_col_spacings(GTK_TABLE(tbl), 5);
    gtk_box_pack_start(GTK_BOX(box), tbl, FALSE, FALSE, 0);
    gtk_widget_show(tbl);
   
    tbl_num = tbl_col = 0;

    for (i=0; i<numlines; i++) {
	/* identifier and key or legend text */
	gsize bytes;
	gchar *titlestr;

	tbl_len++;
	gtk_table_resize(GTK_TABLE(tbl), tbl_len, 3);
	sprintf(label_text, _("line %d: "), i + 1);
	tempwid = gtk_label_new(label_text);
	gtk_misc_set_alignment(GTK_MISC(tempwid), 1, 0.5);
	gtk_table_attach_defaults(GTK_TABLE(tbl), 
				  tempwid, 0, 1, tbl_len-1, tbl_len);
	gtk_widget_show(tempwid);

	tempwid = gtk_label_new(_("legend"));
	gtk_table_attach_defaults(GTK_TABLE(tbl), 
				  tempwid, 1, 2, tbl_len-1, tbl_len);
	gtk_widget_show(tempwid);

	linetitle[i] = gtk_entry_new();
	gtk_table_attach_defaults(GTK_TABLE(tbl), 
				  linetitle[i], 2, 3, tbl_len-1, tbl_len);

	titlestr = g_locale_to_utf8(spec->lines[i].title, -1, NULL,
				    &bytes, NULL);
	gtk_entry_set_text (GTK_ENTRY(linetitle[i]), titlestr);
	g_free(titlestr);
	g_signal_connect (G_OBJECT(linetitle[i]), "activate", 
			  G_CALLBACK(apply_gpt_changes), 
			  spec);
	gtk_widget_show(linetitle[i]);

	/* line type or style */
	tbl_len++;
	gtk_table_resize(GTK_TABLE(tbl), tbl_len, 3);
	tempwid = gtk_label_new(_("type"));
	gtk_table_attach_defaults(GTK_TABLE(tbl), 
				  tempwid, 1, 2, tbl_len-1, tbl_len);
	gtk_widget_show(tempwid);

	stylecombo[i] = gtk_combo_new();
	gtk_table_attach_defaults(GTK_TABLE(tbl), 
				  stylecombo[i], 2, 3, tbl_len-1, tbl_len);
	gtk_combo_set_popdown_strings(GTK_COMBO(stylecombo[i]), plot_types); 
	gtk_entry_set_text(GTK_ENTRY(GTK_COMBO(stylecombo[i])->entry), 
			   spec->lines[i].style);  
	gtk_widget_show(stylecombo[i]);	
	/* scale factor for data */
	tbl_len++;
	gtk_table_resize(GTK_TABLE(tbl), tbl_len, 3);
	tempwid = gtk_label_new(_("scale"));
	gtk_table_attach_defaults(GTK_TABLE(tbl), 
				  tempwid, 1, 2, tbl_len-1, tbl_len);
	gtk_widget_show (tempwid);

	linescale[i] = gtk_entry_new();
	gtk_entry_set_max_length(GTK_ENTRY(linescale[i]), 6);
	gtk_entry_set_width_chars(GTK_ENTRY(linescale[i]), 6);
	gtk_table_attach_defaults(GTK_TABLE(tbl), 
				  linescale[i], 2, 3, tbl_len-1, tbl_len);
	gtk_entry_set_text(GTK_ENTRY(linescale[i]), spec->lines[i].scale);
	g_signal_connect(G_OBJECT(linescale[i]), "activate", 
			 G_CALLBACK(apply_gpt_changes), 
			 spec);
	gtk_widget_show(linescale[i]);
	/* use left or right y axis? */
	tbl_len++;
	gtk_table_resize(GTK_TABLE(tbl), tbl_len, 3);
	tempwid = gtk_label_new(_("y axis"));
	gtk_table_attach_defaults(GTK_TABLE(tbl), 
				  tempwid, 1, 2, tbl_len-1, tbl_len);
	gtk_widget_show(tempwid);

	yaxiscombo[i] = gtk_combo_new();
	gtk_table_attach_defaults(GTK_TABLE(tbl), 
				  yaxiscombo[i], 2, 3, tbl_len-1, tbl_len);
	gtk_combo_set_popdown_strings(GTK_COMBO(yaxiscombo[i]), yaxis_loc); 
	gtk_entry_set_text (GTK_ENTRY(GTK_COMBO(yaxiscombo[i])->entry), 
			    (spec->lines[i].yaxis == 1)? "left" : "right");  
	gtk_widget_show (yaxiscombo[i]);	
    }
}

/* ........................................................... */

static void gpt_tab_XY (GtkWidget *notebook, GPT_SPEC *spec, gint axis) 
{
    GtkWidget *box, *manual, *tbl, *tempwid = NULL;
    int i, tbl_len;
   
    box = gtk_vbox_new(FALSE, 0);
    gtk_container_set_border_width(GTK_CONTAINER(box), 10);
    gtk_widget_show (box);

    if (axis == 0) {
	tempwid = gtk_label_new(_("X-axis"));
    } else if (axis == 1) {
	tempwid = gtk_label_new(_("Y-axis"));
    } else if (axis == 2) {
	tempwid = gtk_label_new(_("Y2-axis"));
    }

    gtk_widget_show(tempwid);
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), box, tempwid);   

    tbl_len = 1;
    tbl = gtk_table_new(tbl_len, 2, FALSE);
    gtk_table_set_row_spacings(GTK_TABLE(tbl), 5);
    gtk_table_set_col_spacings(GTK_TABLE(tbl), 5);
    gtk_box_pack_start(GTK_BOX(box), tbl, FALSE, FALSE, 0);
    gtk_widget_show(tbl);
   
    for (i=0; i<NTITLES; i++) {
	if (gpt_titles[i].tab == 1 + axis) {
	    gsize bytes;
	    gchar *titlestr;

	    tbl_len++;
	    gtk_table_resize(GTK_TABLE(tbl), tbl_len, 2);
            
	    tempwid = gtk_label_new(_(gpt_titles[i].description));
	    gtk_misc_set_alignment(GTK_MISC(tempwid), 1, 0.5);
	    gtk_table_attach_defaults(GTK_TABLE(tbl), 
				      tempwid, 0, 1, tbl_len-1, tbl_len);
	    gtk_widget_show(tempwid);

	    tempwid = gtk_entry_new();
	    gtk_table_attach_defaults(GTK_TABLE(tbl), 
				      tempwid, 1, 2, tbl_len-1, tbl_len);

	    titlestr = g_locale_to_utf8(spec->titles[i], -1, NULL,
					&bytes, NULL);
	    gtk_entry_set_text(GTK_ENTRY(tempwid), titlestr);
	    g_free(titlestr);

	    g_signal_connect(G_OBJECT (tempwid), "activate", 
			     G_CALLBACK(apply_gpt_changes), 
			     spec);
	    gtk_widget_show(tempwid);
	    gpt_titles[i].widget = tempwid;
	}
    }    

    /* axis range: auto versus manual buttons */
    axis_range[axis].ID = axis;
    tbl_len +=3;
    gtk_table_resize(GTK_TABLE(tbl), tbl_len, 2);
    tempwid = gtk_label_new("");
    gtk_table_attach_defaults(GTK_TABLE(tbl), 
			      tempwid, 0, 1, tbl_len-3, tbl_len-2);
    gtk_widget_show(tempwid);
    axis_range[axis].isauto = 
	gtk_radio_button_new_with_label(NULL, _("auto axis range"));
    g_signal_connect(G_OBJECT(axis_range[axis].isauto), "clicked",
		     G_CALLBACK(flip_manual_range), 
		     GINT_TO_POINTER(axis_range[axis].ID));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON
				 (axis_range[axis].isauto), TRUE);
    gtk_table_attach_defaults(GTK_TABLE(tbl), 
			      axis_range[axis].isauto, 
			      0, 1, tbl_len-2, tbl_len-1);
    gtk_widget_show(axis_range[axis].isauto);
    manual = 
	gtk_radio_button_new_with_label(gtk_radio_button_get_group 
					(GTK_RADIO_BUTTON 
					 (axis_range[axis].isauto)),
					_("manual range:")); 
    g_signal_connect(G_OBJECT(manual), "clicked",
		     G_CALLBACK(flip_manual_range), 
		     GINT_TO_POINTER(axis_range[axis].ID));
    gtk_table_attach_defaults(GTK_TABLE(tbl), 
			      manual, 0, 1, tbl_len-1, tbl_len);
    gtk_widget_show(manual);

    /* axis range min. entry */
    tbl_len++;
    tempwid = gtk_label_new(_("minimum"));
    gtk_table_attach_defaults(GTK_TABLE(tbl), 
			      tempwid, 0, 1, tbl_len-1, tbl_len);
    gtk_widget_show(tempwid);
    gtk_table_resize(GTK_TABLE(tbl), tbl_len, 2);
    axis_range[axis].min = gtk_entry_new();
    gtk_table_attach_defaults(GTK_TABLE(tbl), 
			      axis_range[axis].min, 1, 2, tbl_len-1, tbl_len);
    gtk_entry_set_text(GTK_ENTRY(axis_range[axis].min), "");
    g_signal_connect(G_OBJECT(axis_range[axis].min), "activate", 
		     G_CALLBACK(apply_gpt_changes), 
		     spec);
    gtk_widget_show(axis_range[axis].min);

    /* axis range max. entry */
    tbl_len++;
    tempwid = gtk_label_new(_("maximum"));
    gtk_table_attach_defaults(GTK_TABLE(tbl), 
			      tempwid, 0, 1, tbl_len-1, tbl_len);
    gtk_widget_show(tempwid);
    gtk_table_resize(GTK_TABLE(tbl), tbl_len, 2);
    axis_range[axis].max = gtk_entry_new();
    gtk_table_attach_defaults(GTK_TABLE(tbl), 
			      axis_range[axis].max, 1, 2, tbl_len-1, tbl_len);
    gtk_entry_set_text(GTK_ENTRY(axis_range[axis].max), "");
    g_signal_connect(G_OBJECT(axis_range[axis].max), "activate", 
		     G_CALLBACK(apply_gpt_changes), 
		     spec);
    gtk_widget_show(axis_range[axis].max);
   
    if (strcmp(spec->range[axis][0], "*") == 0)
	flip_manual_range(NULL, GINT_TO_POINTER(axis_range[axis].ID));
    else {
	gtk_entry_set_text(GTK_ENTRY(axis_range[axis].min),
			   spec->range[axis][0]);
	gtk_entry_set_text(GTK_ENTRY(axis_range[axis].max),
			   spec->range[axis][1]);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON
				     (axis_range[axis].isauto), FALSE);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON
				     (manual), TRUE);
    }
}

/* ........................................................... */

static int show_gnuplot_dialog (GPT_SPEC *spec) 
{
    GtkWidget *tempwid, *notebook;
    int i;

    if (gpt_control != NULL) {
	errbox(_("You can only have one plot controller open\n"
		 "at any given time"));
	return 1;
    }

    for (i=0; i<3; i++) {
	axis_range[i].isauto = NULL;
    }

    gpt_control = gtk_dialog_new();
    gtk_window_set_title(GTK_WINDOW(gpt_control), _("gretl plot controls"));
    gtk_container_set_border_width 
        (GTK_CONTAINER(GTK_DIALOG(gpt_control)->vbox), 10);
    gtk_container_set_border_width 
        (GTK_CONTAINER(GTK_DIALOG(gpt_control)->action_area), 5);
    gtk_box_set_spacing(GTK_BOX(GTK_DIALOG(gpt_control)->vbox), 2);
    gtk_box_set_spacing(GTK_BOX(GTK_DIALOG(gpt_control)->action_area), 15);
    gtk_box_set_homogeneous(GTK_BOX(GTK_DIALOG(gpt_control)->action_area), TRUE);
    gtk_window_set_position(GTK_WINDOW(gpt_control), GTK_WIN_POS_MOUSE);

    g_signal_connect (G_OBJECT (gpt_control), "destroy",
		      G_CALLBACK (close_plot_controller), 
		      (gpointer *) spec);
   
    notebook = gtk_notebook_new();
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(gpt_control)->vbox), 
		       notebook, TRUE, TRUE, 0);
    gtk_widget_show(notebook);

    gpt_tab_main(notebook, spec);
    gpt_tab_XY(notebook, spec, 0);
    gpt_tab_XY(notebook, spec, 1);
    if (spec->y2axis) gpt_tab_XY(notebook, spec, 2);
    gpt_tab_lines(notebook, spec); 
    gpt_tab_output(notebook, spec);

    tempwid = standard_button(GTK_STOCK_APPLY);
    GTK_WIDGET_SET_FLAGS (tempwid, GTK_CAN_DEFAULT);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(gpt_control)->action_area), 
		       tempwid, TRUE, TRUE, 0);
    g_signal_connect (G_OBJECT(tempwid), "clicked", 
		      G_CALLBACK(apply_gpt_changes), spec);
    gtk_widget_grab_default (tempwid);
    gtk_widget_show (tempwid);

#ifdef GNUPLOT_PIPE
    tempwid = standard_button(GTK_STOCK_SAVE);
    GTK_WIDGET_SET_FLAGS (tempwid, GTK_CAN_DEFAULT);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(gpt_control)->action_area), 
		       tempwid, TRUE, TRUE, 0);
    g_signal_connect (G_OBJECT(tempwid), "clicked", 
		      G_CALLBACK(save_session_graph_plotspec), spec);
    gtk_widget_show (tempwid);
#endif

    tempwid = standard_button(GTK_STOCK_CLOSE);
    GTK_WIDGET_SET_FLAGS(tempwid, GTK_CAN_DEFAULT);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(gpt_control)->action_area), 
		       tempwid, TRUE, TRUE, 0);
    g_signal_connect(G_OBJECT(tempwid), "clicked",
		     G_CALLBACK(delete_widget), gpt_control);
    gtk_widget_show(tempwid);

    tempwid = standard_button(GTK_STOCK_HELP);
    GTK_WIDGET_SET_FLAGS(tempwid, GTK_CAN_DEFAULT);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(gpt_control)->action_area), 
		       tempwid, TRUE, TRUE, 0);
    g_signal_connect (G_OBJECT (tempwid), "clicked", 
		      G_CALLBACK(context_help), 
		      GINT_TO_POINTER(GR_PLOT));
    gtk_widget_show (tempwid);

    gtk_widget_show (gpt_control);

    return 0;
}

#ifdef GNUPLOT_PNG

#ifdef G_OS_WIN32
static void gnuplot_graph_to_clipboard (GPT_SPEC *spec);
#endif

void save_this_graph (GPT_SPEC *plot, const char *fname)
{
    FILE *fq;
    PRN *prn;
    char plottmp[MAXLEN], plotline[MAXLEN], termstr[MAXLEN];
    gchar *plotcmd = NULL;
    int cmds, err;

    if (!user_fopen("gptout.tmp", plottmp, &prn)) return;

    fq = fopen(plot->fname, "r");
    if (fq == NULL) {
	errbox(_("Couldn't access graph info"));
	gretl_print_destroy(prn);
	return;
    }
 
    cmds = termtype_to_termstr(plot->termtype, termstr);
  
    if (cmds) {
	if (copyfile(plot->fname, fname)) { 
	    errbox(_("Failed to copy graph file"));
	}
	return;
    } else {
	pprintf(prn, "set term %s\n", termstr);
#ifdef ENABLE_NLS
	if (strstr(termstr, "postscript")) {
	    pprintf(prn, "set encoding iso_8859_1\n");
	}
#endif /* ENABLE_NLS */	
	pprintf(prn, "set output '%s'\n", fname);
	while (fgets(plotline, MAXLEN-1, fq)) {
	    if (strncmp(plotline, "set term", 8) && 
		strncmp(plotline, "set output", 10))
		pprintf(prn, "%s", plotline);
	}
    }

    gretl_print_destroy(prn);
    fclose(fq);

    plotcmd = g_strdup_printf("\"%s\" \"%s\"", paths.gnuplot, 
			      plottmp);

#ifdef G_OS_WIN32
    err = winfork(plotcmd, NULL, SW_SHOWMINIMIZED, 0);
#else
    err = system(plotcmd);
#endif /* G_OS_WIN32 */

    remove(plottmp);
    g_free(plotcmd);

    if (err) {
	errbox(_("Gnuplot error creating graph"));
    } else {
	infobox(_("Graph saved"));
    }
}

#else /* not GNUPLOT_PNG */

/* Below: functions for saving last auto-generated graph */

void do_save_graph (const char *fname, char *savestr)
{
    FILE *fq;
    PRN *prn;
    char plottmp[MAXLEN], plotline[MAXLEN], termstr[MAXLEN];
    gchar *plotcmd = NULL;
    int cmds, err = 0;

    if (!user_fopen("gptout.tmp", plottmp, &prn)) return;

    fq = fopen(paths.plotfile, "r");
    if (fq == NULL) {
	errbox(_("Couldn't access graph info"));
	gretl_print_destroy(prn);
	return;
    } 

    cmds = termtype_to_termstr(savestr, termstr);  
    if (cmds) {
	if (copyfile(paths.plotfile, fname)) { 
	    errbox(_("Failed to copy graph file"));
	}
	return;
    } else {
	pprintf(prn, "set term %s\n", termstr);
#ifdef ENABLE_NLS
	if (strstr(termstr, "postscript")) {
	    pprintf(prn, "set encoding iso_8859_1\n");
	}
#endif /* ENABLE_NLS */
	pprintf(prn, "set output '%s'\n", fname);
	while (fgets(plotline, MAXLEN-1, fq)) {
	    if (strncmp(plotline, "pause", 5)) 
		pprintf(prn, "%s", plotline);
	}
    }

    gretl_print_destroy(prn);
    fclose(fq);

    plotcmd = g_strdup_printf("\"%s\" \"%s\"", paths.gnuplot, 
			      plottmp);

#ifdef G_OS_WIN32
    err = winfork(plotcmd, NULL, SW_SHOWMINIMIZED, 0);
#else
    err = system(plotcmd);
#endif /* G_OS_WIN32 */

    g_free(plotcmd);
    remove(plottmp);

    if (err) {
	errbox(_("Gnuplot error creating graph"));
    } else {
	infobox(_("Graph saved"));
    }
}

/* ........................................................... */

static void plot_save_filesel (GtkWidget *w, gpointer data)
{
    static char savestr[MAXLEN];
    GtkWidget *combo = (GtkWidget *) data;

    strcpy(savestr, 
	   gtk_entry_get_text(GTK_ENTRY(GTK_COMBO(combo)->entry)));
    gtk_widget_destroy(GTK_WIDGET(combo->parent->parent->parent));
    file_selector(_("save graph"), SAVE_LAST_GRAPH, savestr);
}

/* ........................................................... */

static void kill_gpt_save_dialog (GtkWidget *w, gpointer data)
{
    GtkWidget **dialog = (GtkWidget **) data;

    gtk_widget_destroy(*dialog);
    *dialog = NULL;
}

/* ........................................................... */

void gpt_save_dialog (void)
{
    GtkWidget *tempwid, *tbl, *combo;
    static GtkWidget *dialog;
    gint tbl_len;
    GList *termtype = NULL;
    int i;
    gchar *ttypes[] = {
	"postscript", 
	"fig", 
	"latex", 
	"png",
	"plot commands"
    };

    if (dialog != NULL) {
	gdk_window_raise(dialog->window);
	return;
    }

    for (i=0; i<5; i++) {
	termtype = g_list_append(termtype, ttypes[i]);
    }

    dialog = gtk_dialog_new();
    gtk_window_set_title(GTK_WINDOW(dialog), _("gretl: save graph"));
    gtk_container_set_border_width 
        (GTK_CONTAINER(GTK_DIALOG(dialog)->vbox), 10);
    gtk_container_set_border_width 
        (GTK_CONTAINER(GTK_DIALOG(dialog)->action_area), 5);
    gtk_box_set_spacing(GTK_BOX(GTK_DIALOG(dialog)->vbox), 2);
    gtk_box_set_spacing(GTK_BOX(GTK_DIALOG(dialog)->action_area), 15);
    gtk_box_set_homogeneous(GTK_BOX(GTK_DIALOG(dialog)->action_area), TRUE);
    gtk_window_set_position(GTK_WINDOW(dialog), GTK_WIN_POS_MOUSE);
    g_signal_connect(G_OBJECT(dialog), "destroy",
		     G_CALLBACK(kill_gpt_save_dialog), &dialog);

    tbl_len = 1;
    tbl = gtk_table_new(tbl_len, 2, FALSE);
    gtk_table_set_row_spacings(GTK_TABLE(tbl), 5);
    gtk_table_set_col_spacings(GTK_TABLE(tbl), 5);
    gtk_box_pack_start(GTK_BOX (GTK_DIALOG(dialog)->vbox), 
		       tbl, FALSE, FALSE, 0);
    gtk_widget_show(tbl);

    tempwid = gtk_label_new(_("output type"));
    gtk_table_attach_defaults(GTK_TABLE (tbl), 
			      tempwid, 0, 1, tbl_len-1, tbl_len);
    gtk_widget_show(tempwid);

    combo = gtk_combo_new();
    gtk_table_attach_defaults(GTK_TABLE(tbl), 
			      combo, 1, 2, tbl_len-1, tbl_len);
    gtk_combo_set_popdown_strings(GTK_COMBO(combo), termtype);   
    gtk_widget_show(combo);

    tempwid = gtk_button_new_with_label(_("Save"));
    GTK_WIDGET_SET_FLAGS(tempwid, GTK_CAN_DEFAULT);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->action_area), 
		       tempwid, TRUE, TRUE, 0);
    g_signal_connect(G_OBJECT(tempwid), "clicked", 
		     G_CALLBACK(plot_save_filesel), combo);
    gtk_widget_grab_default(tempwid);
    gtk_widget_show(tempwid);
   
    tempwid = gtk_button_new_with_label(_("Cancel"));
    GTK_WIDGET_SET_FLAGS(tempwid, GTK_CAN_DEFAULT);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->action_area), 
		       tempwid, TRUE, TRUE, 0);
    g_signal_connect(G_OBJECT(tempwid), "clicked", 
		     G_CALLBACK(delete_widget), dialog);
    gtk_widget_show(tempwid);

    gtk_widget_show(dialog);
}

#endif /* GNUPLOT_PNG */

/* ........................................................... */

static int chop_comma (char *str)
{
    size_t i, n = strlen(str);

    for (i=n-1; i>0; i--) {
	if (isspace((unsigned char) str[i])) continue;
	if (str[i] == ',') {
	    str[i] = 0;
	    return 1;
	}
    }		
    return 0;
}

/* ........................................................... */

static void get_gpt_data (char *line, double *x, double *y)
{
#ifdef ENABLE_NLS
    setlocale(LC_NUMERIC, "C");
#endif
    if (x != NULL) {
	if (sscanf(line, "%lf %lf", x, y) == 2) ; /* fine */
	else {
	    if (sscanf(line, "%lf", x) == 1) *y = NADBL;
	    else if (sscanf(line, "%*s %lf", y) == 1) *x = NADBL;
	    else *x = NADBL; *y = NADBL;
	}
    } else {
	if (sscanf(line, "%*s %lf", y) != 1) *y = NADBL;
    }
#ifdef ENABLE_NLS
    setlocale(LC_NUMERIC, "");
#endif
}

/* ........................................................... */

static int cant_edit (const char *line)
{
    if (strncmp(line, "# mult", 6) == 0) {
	errbox(_("Sorry, can't edit multiple scatterplots"));
	return 1;
    }
    if (strncmp(line, "# CUSUM", 7) == 0) {
	errbox(_("Sorry, can't edit CUSUM plots"));
	return 1;
    }
    if (strncmp(line, "# sampl", 7) == 0) {
	errbox(_("Sorry, can't edit sampling distribution plots"));
	return 1;
    }
    if (strstr(line, "no auto-parse")) {
	errbox(_("Sorry, can't edit this plot"));
	return 1;
    }
    return 0;
}

/* ........................................................... */

static GPT_SPEC *plotspec_new (void)
{
    GPT_SPEC *spec;
    int i;

    spec = mymalloc(sizeof *spec);
    if (spec == NULL) return NULL;

    if ((spec->lines = mymalloc(6 * sizeof(GPT_LINE))) == NULL) {
	free(spec);
	return NULL;
    }

    for (i=0; i<4; i++) {
	spec->titles[i][0] = 0;
	spec->literal[i] = NULL;
    }

    spec->xtics[0] = 0;
    spec->mxtics[0] = 0;
    spec->fname[0] = 0;
    strcpy(spec->keyspec, "left top");

    for (i=0; i<3; i++) {
	strcpy(spec->range[i][0], "*");
	strcpy(spec->range[i][1], "*");
    }

    spec->y2axis = 0;
    spec->code = GNUPLOT;
    spec->ts = 0;
    spec->fp = NULL;
    spec->data = NULL;
    spec->ptr = NULL;
    spec->edit = 0;

    spec->termtype[0] = 0;

    return spec;
}

/* ........................................................... */

static int parse_set_line (GPT_SPEC *plot, const char *line,
			   int *i)
{
    char set_thing[12], setting[MAXLEN], range[32];
    size_t n, j;

    if (strstr(line, "encoding")) return 0;

    if (sscanf(line + 4, "%11s", set_thing) != 1) {
	errbox(_("Failed to parse gnuplot file"));
	fprintf(stderr, "plotfile line: '%s'\n", line);
	return 1;
    }
    if (strcmp(set_thing, "y2tics") == 0) {
	plot->y2axis = 1;
	return 0;
    }

    n = strlen(set_thing);
    strcpy(setting, line + 4 + n);
    top_n_tail(setting);

    if (strstr(set_thing, "range")) {
	strcpy(range, setting + 1);
	n = strlen(range);
	for (j=0; j<n; j++) {
	    if (range[j] == ':') {
		range[j] = 0;
		strcpy(plot->range[*i][0], range);
		break;
	    }
	}
	strcpy(range, strchr(setting, ':') + 1);
	delchar(']', range);
	strcpy(plot->range[*i][1], range);
	*i += 1;
    }	  
    else if (strcmp(set_thing, "title") == 0) 
	strcpy(plot->titles[0], setting);
    else if (strcmp(set_thing, "xlabel") == 0)
	strcpy(plot->titles[1], setting);
    else if (strcmp(set_thing, "ylabel") == 0)
	strcpy(plot->titles[2], setting);
    else if (strcmp(set_thing, "y2label") == 0)
	strcpy(plot->titles[3], setting);
    else if (strcmp(set_thing, "key") == 0)
	strcpy(plot->keyspec, setting);
    else if (strcmp(set_thing, "nokey") == 0)
	strcpy(plot->keyspec, "none");
    else if (strcmp(set_thing, "xtics") == 0) 
	safecpy(plot->xtics, setting, 15);
    else if (strcmp(set_thing, "mxtics") == 0) 
	safecpy(plot->mxtics, setting, 3);

    return 0;
}

/* ........................................................... */

#ifdef GNUPLOT_PIPE
static int open_gnuplot_pipe (const PATHS *ppaths, GPT_SPEC *plot)
     /* add file or pipe to plot struct */
{
#ifdef OS_WIN32
    plot->fp = NULL; /* will be opened later as needed */
#else
    plot->fp = popen(ppaths->gnuplot, "w");
    if (plot->fp == NULL) return 1;
#endif
    plot->edit = 1;
    return 0;
}
#endif

/* ........................................................... */

static int read_plotspec_from_file (GPT_SPEC *spec)
     /* read in plotspec struct from gnuplot command file.
	This is _not_ a general parser for gnuplot files; it is
	designed specifically for files auto-generated by gretl. */
{
    int i, j, t, n, done;
    char line[MAXLEN], *got = NULL, *tmp = NULL;
    double *tmpy = NULL;
    size_t diff;
    FILE *fp;

    /* open the plot file */
    fp = fopen(spec->fname, "r");
    if (fp == NULL) {
	errbox(_("Couldn't open graph file"));
	return 1;
    }

    /* first get the preamble and "set" lines */
    i = 0;
    while ((got = fgets(line, MAXLEN - 1, fp))) {
	if (cant_edit(line)) goto plot_bailout;
	if (strncmp(line, "# timeseries", 12) == 0) {
	    spec->ts = 1;
	    continue;
	}
	if (strncmp(line, "# freq", 6) == 0 ||
	    strncmp(line, "# peri", 6) == 0) {
	    /* special cases */
	    if (line[2] == 'f') spec->code = FREQ;
	    else spec->code = PERGM;
	    for (j=0; j<4; j++) {
		spec->literal[j] = mymalloc(MAXLEN);
		if (spec->literal[j] == NULL) return 1;
		if (!fgets(spec->literal[j], MAXLEN - 1, fp)) {
		    errbox(_("Plot file is corrupted"));
		    free(spec->literal[j]);
		    spec->literal[j] = NULL;
		    goto plot_bailout;
		}
		top_n_tail(spec->literal[j]);
	    }
	    continue;
	}
	if (strncmp(line, "# forecast", 10) == 0) {
	    spec->code = FCASTERR;
	    continue;
	}
	/* try ignoring an unknown comment line? */
	if (strncmp(line, "# ", 2) == 0) continue;

	if (strncmp(line, "set ", 4)) break;
	if (parse_set_line(spec, line, &i)) goto plot_bailout;
    }

    if (got == NULL) goto plot_bailout;

    for (i=0; i<4; i++) {
	if (spec->titles[i][0] != 0) {
	    delchar('\'', spec->titles[i]);
	}
    }

    if (spec->keyspec[0] == 0) {
	strcpy(spec->keyspec, "none");
    }

    /* then get the "plot" lines */
    if (strncmp(line, "plot ", 4) ||
	(strlen(line) < 10 && fgets(line, MAXLEN - 1, fp) == NULL)) {	
	errbox(_("Failed to parse gnuplot file"));
	fprintf(stderr, "plotfile line: '%s'\n", line);
	goto plot_bailout;
    }

    i = 0;
    done = 0;
    while (1) {
	top_n_tail(line);
	if (!chop_comma(line)) done++;

	/* scale, [yaxis,] style */
	tmp = strstr(line, "using");
	if (tmp && tmp[11] == '*') {
            safecpy(spec->lines[i].scale, tmp + 12, 7);
	    charsub(spec->lines[i].scale, ')', '\0');
	} else {
	    if (tmp) 
		strcpy(spec->lines[i].scale, "1.0");
	    else {
		strcpy(spec->lines[i].scale, "NA");
		tmp = strstr(line, "axes");
		if (tmp == NULL)
		    tmp = strstr(line, "title");
		if (tmp != NULL) {
		    diff = tmp - line;
		    strncpy(spec->lines[i].formula, line, diff);
		    spec->lines[i].formula[diff - 1] = 0;
		}
	    }
	}

	spec->lines[i].yaxis = 1;
	tmp = strstr(line, "axes");
	if (tmp != NULL && strlen(tmp) > 8 && tmp[8] == '2')
	    spec->lines[i].yaxis = 2;

	tmp = strstr(line, "title"); 
	if (tmp != NULL) {
	    tmp += 7;
	    spec->lines[i].title[0] = '\'';
	    j = 0;
	    while (tmp[j] != '\'') { 
		spec->lines[i].title[j] = tmp[j];
		j++;
	    }
	    spec->lines[i].title[j] = 0; 
	}

	tmp = strstr(line, " w ");
	if (tmp != NULL) {
	    strcpy(spec->lines[i].style, tmp + 3);
	    delchar(',', spec->lines[i].style);
	} else {
	    strcpy(spec->lines[i].style, "points");
	}

	if (done) break;
	i++;
	if ((got = fgets(line, MAXLEN - 1, fp)) == NULL) break;
    }

    if (got == NULL) goto plot_bailout;

    /* free any unused lines */
    if (i < 5) {
	spec->lines = myrealloc(spec->lines, (i + 1) * sizeof(GPT_LINE));
    }

    /* finally, get the plot data */
    spec->data = mymalloc(datainfo->n * (i + 2) * sizeof(double));
    tmpy = mymalloc(datainfo->n * sizeof *tmpy);
    if (spec->data == NULL || tmpy == NULL) goto plot_bailout;

    j = 1;
    t = 0;
    n = 0;
    while (fgets(line, MAXLEN - 1, fp)) {
	if (line[0] == 'e') {
	    n = t;
	    t = 0;
	    j++;
	    continue;
	}
	if (strncmp(line, "pause", 5) == 0) break;
	if (j == 1) { /* first set: read both x and y */ 
	    get_gpt_data(line, &(spec->data[t]), &(tmpy[t]));
	} else {      /* any subsequent sets: read y only */ 
	    get_gpt_data(line, NULL, &(spec->data[j*n + t]));
	}
	t++;
    }

    /* put "tmpy" in as last data column */
    for (t=0; t<n; t++) {
	spec->data[n + t] = tmpy[t];
    }
    free(tmpy);

    spec->t1 = 0;
    spec->t2 = n - 1;
    spec->list[0] = i+2;

    fclose(fp);

    return 0;

 plot_bailout:
    fclose(fp);
    return 1;
}

#ifdef GNUPLOT_PIPE
void start_editing_session_graph (const char *fname)
{
    GPT_SPEC *spec;

    spec = plotspec_new();
    if (spec == NULL) return;

    strcpy(spec->fname, fname);
    if (read_plotspec_from_file(spec)) {
	free_plotspec(spec);
	return;
    }

    if (open_gnuplot_pipe(&paths, spec)) {
	errbox(_("gnuplot command failed"));
	free_plotspec(spec);
	return;
    } 

    show_gnuplot_dialog(spec);
}
#endif

#ifdef GNUPLOT_PNG

/* Size of drawing area */
#define WIDTH  640   /* try 576? */
#define HEIGHT 480   /* try 432? */

#ifdef USE_GNOME
extern void gnome_print_graph (const char *fname);
#endif

/* screen coordinates of actual plot area of gnuplot PNG graph */
#define PLOTXMIN 52.0
#define PLOTXMAX 620.0
#define PLOTYMIN 32.0
#define PLOTYMAX 446.0
#define NOTITLE_YMIN 13.0

static void get_data_xy (png_plot_t *plot, int x, int y, 
			 double *data_x, double *data_y)
{
    double xmin, xmax;
    double ymin, ymax;

    if (plot_is_zoomed(plot)) {
	xmin = plot->zoom->xmin;
	xmax = plot->zoom->xmax;
	ymin = plot->zoom->ymin;
	ymax = plot->zoom->ymax;
    } else {
	xmin = plot->xmin;
	xmax = plot->xmax;
	ymin = plot->ymin;
	ymax = plot->ymax;
    }

    *data_x = xmin + ((double) x - PLOTXMIN) / (PLOTXMAX - PLOTXMIN) *
	(xmax - xmin);
    if (ymin == 0.0 && ymax == 0.0) { /* unknown y range */
	*data_y = NADBL;
    } else {
	int plotymin = (plot->title)? PLOTYMIN : NOTITLE_YMIN;

	*data_y = ymax - ((double) y - plotymin) / (PLOTYMAX - plotymin) *
	    (ymax - ymin);
    }
}

static void x_to_date (double x, int pd, char *str)
{
    int yr = (int) x;
    double t, frac = 1.0 / pd;
    int subper = (int) ((x - yr + frac) * pd);
    static int decpoint;

    if (decpoint == 0) decpoint = get_local_decpoint();

    t = yr + subper / ((pd < 10)? 10.0 : 100.0);
    sprintf(str, "%.*f", (pd < 10)? 1 : 2, t);
    charsub(str, decpoint, ':');
}

static void create_selection_gc (png_plot_t *plot)
{
    if (plot->invert_gc == NULL) {
	plot->invert_gc = gdk_gc_new(plot->canvas->window);
	gdk_gc_set_function(plot->invert_gc, GDK_INVERT);
    }
}

static void draw_selection_rectangle (png_plot_t *plot,
				      int x, int y)
{
    int rx, ry, rw, rh;

    rx = (plot->zoom->screen_xmin < x)? plot->zoom->screen_xmin : x;
    ry = (plot->zoom->screen_ymin < y)? plot->zoom->screen_ymin : y;
    rw = x - plot->zoom->screen_xmin;
    rh = y - plot->zoom->screen_ymin;
    if (rw < 0) rw = -rw;
    if (rh < 0) rh = -rh;    

    /* draw one time to make the rectangle appear */
    gdk_draw_rectangle(plot->pixmap,
		       plot->invert_gc,
		       FALSE,
		       rx, ry, rw, rh);
    /* show the modified pixmap */
    gdk_window_copy_area(plot->canvas->window,
			 plot->canvas->style->fg_gc[GTK_STATE_NORMAL],
			 0, 0,
			 plot->pixmap,
			 0, 0,
			 WIDTH, HEIGHT);
    /* draw (invert) again to erase the rectangle */
    gdk_draw_rectangle(plot->pixmap,
		       plot->invert_gc,
		       FALSE,
		       rx, ry, rw, rh);
}

static gint
motion_notify_event (GtkWidget *widget, GdkEventMotion *event,
		     png_plot_t *plot)
{
    int x, y;
    GdkModifierType state;
    gchar label[32], label_y[16];
    int ymin = (plot->title)? PLOTYMIN : NOTITLE_YMIN;

    if (event->is_hint)
        gdk_window_get_pointer (event->window, &x, &y, &state);
    else {
        x = event->x;
        y = event->y;
        state = event->state;
    }

    if (x > PLOTXMIN && x < PLOTXMAX && y > ymin && y < PLOTYMAX) {
	double data_x, data_y;

	get_data_xy(plot, x, y, &data_x, &data_y);
	if (plot->pd == 4 || plot->pd == 12) {
	    x_to_date(data_x, plot->pd, label);
	} else
	    sprintf(label, (plot->xint)? "%7.0f" : "%7.4g", data_x);
	if (!na(data_y)) {
	    sprintf(label_y, (plot->yint)? " %-7.0f" : " %-7.4g", data_y);
	    strcat(label, label_y);
	}
	if (plot_is_zooming(plot) && (state & GDK_BUTTON1_MASK)) {
	    draw_selection_rectangle(plot, x, y);
	}
    } else
	*label = 0;
    gtk_label_set_text(GTK_LABEL(plot->cursor_label), label);
  
    return TRUE;
}

static void start_editing_png_plot (png_plot_t *plot)
     /* called from png plot popup menu */
{
    /* the spec struct is not yet filled out by reference
       to the gnuplot source file 
    */
    if (read_plotspec_from_file(plot->spec)) return;

    if (show_gnuplot_dialog(plot->spec) == 0) { /* OK */
	plot->flags |= PLOT_HAS_CONTROLLER;
    }
}

static gint plot_popup_activated (GtkWidget *w, gpointer data)
{
    gchar *item = (gchar *) data;
    gpointer ptr = g_object_get_data(G_OBJECT(w), "plot");
    png_plot_t *plot = (png_plot_t *) ptr;

    if (!strcmp(item, _("Save as postscript (EPS)..."))) {
	strcpy(plot->spec->termtype, "postscript");
	file_selector("Save graph as postscript file", SAVE_THIS_GRAPH, 
		      plot->spec);
    }
    else if (!strcmp(item, _("Save as PNG..."))) {
	strcpy(plot->spec->termtype, "png");
        file_selector("Save graph as PNG", SAVE_THIS_GRAPH, plot->spec);
    }
#ifdef G_OS_WIN32
    else if (!strcmp(item, _("Copy to clipboard"))) {
	gnuplot_graph_to_clipboard(plot->spec);
    }
#endif
    else if (!strcmp(item, _("Save to session as icon"))) { 
	add_graph_to_session(plot->spec, 0, NULL);
    }
    else if (plot_is_range_mean(plot) && !strcmp(item, _("Help"))) { 
	context_help (NULL, GINT_TO_POINTER(RANGE_MEAN));
    }
    else if (!strcmp(item, _("Zoom..."))) { 
	GdkCursor* cursor;

	cursor = gdk_cursor_new(GDK_CROSSHAIR);
	gdk_window_set_cursor(plot->canvas->window, cursor);
	gdk_cursor_destroy(cursor);
	plot->flags |= PLOT_ZOOMING;
	gtk_statusbar_push(GTK_STATUSBAR(plot->statusbar), plot->cid, 
			   _(" Drag to define zoom rectangle"));
	create_selection_gc(plot);
    }
    else if (!strcmp(item, _("Restore full view"))) { 
	zoom_unzoom_png(plot, PNG_UNZOOM);
    }
#ifdef USE_GNOME 
    else if (!strcmp(item, _("Print..."))) { 
	gnome_print_graph(plot->spec->fname);
    }
#endif 
    else if (!strcmp(item, _("Edit"))) { 
	start_editing_png_plot(plot);
    }
    else if (!strcmp(item, _("Close"))) { 
        gtk_widget_destroy(plot->shell);
    } 

    gtk_widget_destroy(plot->popup);
    plot->popup = NULL;
    return TRUE;
}

static GtkWidget *build_plot_menu (png_plot_t *plot)
{
    GtkWidget *menu, *item;    
    const char *regular_items[] = {
        N_("Save as postscript (EPS)..."),
	N_("Save as PNG..."),
#ifdef G_OS_WIN32
	N_("Copy to clipboard"),
#endif
	N_("Save to session as icon"),
	N_("Zoom..."), 
#ifdef USE_GNOME
	N_("Print..."),
#endif
	N_("Edit"),
	N_("Help"),
        N_("Close"),
        NULL
    };
    const char *zoomed_items[] = {
	N_("Restore full view"),
	N_("Close"),
	NULL
    };
    const char **plot_items;
    int i = 0;

    menu = gtk_menu_new();

    if (plot_is_zoomed(plot)) {
	plot_items = zoomed_items;
    } else {
	plot_items = regular_items;
    }

    while (plot_items[i]) {
	if (plot->statusbar == NULL &&
	    !strcmp(plot_items[i], "Zoom...")) {
	    i++;
	    continue;
	}
	if (!plot_is_range_mean(plot) &&
	    !strcmp(plot_items[i], "Help")) {
	    i++;
	    continue;
	}
	if (plot_is_saved(plot) &&
	    !strcmp(plot_items[i], "Save to session as icon")) {
	    i++;
	    continue;
	}
	if (plot_has_controller(plot) &&
	    !strcmp(plot_items[i], "Edit")) {
	    i++;
	    continue;
	}
        item = gtk_menu_item_new_with_label(_(plot_items[i]));
        g_signal_connect(G_OBJECT(item), "activate",
			 G_CALLBACK(plot_popup_activated),
			 _(plot_items[i]));
        g_object_set_data(G_OBJECT(item), "plot", plot);
        GTK_WIDGET_SET_FLAGS (item, GTK_SENSITIVE | GTK_CAN_FOCUS);
        gtk_widget_show(item);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
        i++;
    }

    return menu;
}

static int redisplay_edited_png (png_plot_t *plot)
{
    gchar *plotcmd;
    FILE *fp;
    int err = 0;

    gnuplot_png_init(plot->spec->fname, &fp);
    if (fp == NULL) return 1;

    print_plotspec_details(plot->spec, fp);
    fclose(fp);

    plotcmd = g_strdup_printf("\"%s\" \"%s\"", paths.gnuplot, 
			      plot->spec->fname);
#ifdef G_OS_WIN32
    err = winfork(plotcmd, NULL, SW_SHOWMINIMIZED, 0);
#else    
    err = system(plotcmd);
#endif
    g_free(plotcmd);

    if (err) {
	errbox(_("Failed to generate PNG file"));
	return 1;
    }

    render_pngfile(plot, PNG_REDISPLAY);

    return 0;
}

static int zoom_unzoom_png (png_plot_t *plot, int view)
{
    int err = 0;
    char fullname[MAXLEN];
    gchar *plotcmd = NULL;

    if (view == PNG_ZOOM) {
	FILE *fpin, *fpout;
	char line[MAXLEN];

	fpin = fopen(plot->spec->fname, "r");
	if (fpin == NULL) return 1;

	build_path(paths.userdir, "zoomplot.gp", fullname, NULL);
	fpout = fopen(fullname, "w");
	if (fpout == NULL) {
	    fclose(fpin);
	    return 1;
	}

	/* switch to zoomed data range */
#ifdef ENABLE_NLS
	setlocale(LC_NUMERIC, "C");
#endif
	fprintf(fpout, "set xrange [%g:%g]\n", plot->zoom->xmin,
		plot->zoom->xmax);
	fprintf(fpout, "set yrange [%g:%g]\n", plot->zoom->ymin,
		plot->zoom->ymax);
#ifdef ENABLE_NLS
	setlocale(LC_NUMERIC, "");
#endif

	while (fgets(line, MAXLEN-1, fpin)) {
	    if (strncmp(line, "set xrange", 10) &&
		strncmp(line, "set yrange", 10))
		fputs(line, fpout);
	}

	fclose(fpout);
	fclose(fpin);

	plotcmd = g_strdup_printf("\"%s\" \"%s\"", paths.gnuplot, 
				  fullname);
    } else { /* PNG_UNZOOM */
	plotcmd = g_strdup_printf("\"%s\" \"%s\"", paths.gnuplot, 
				  plot->spec->fname);
    }

#ifdef G_OS_WIN32
    err = winfork(plotcmd, NULL, SW_SHOWMINIMIZED, 0);
#else
    err = system(plotcmd);
#endif
    g_free(plotcmd);  

    if (view == PNG_ZOOM) {
	remove(fullname);
    }

    if (err) {
	errbox(_("Failed to generate PNG file"));
	return 1;
    }

    render_pngfile(plot, view);

    return 0;
}

static gint plot_button_release (GtkWidget *widget, GdkEventButton *event, 
				 png_plot_t *plot)
{
    if (plot_is_zooming(plot)) {
	double z;

	get_data_xy(plot, event->x, event->y, 
		    &plot->zoom->xmax, &plot->zoom->ymax);

	/* flip the selected rectangle if required */
	if (plot->zoom->xmin > plot->zoom->xmax) {
	    z = plot->zoom->xmax;
	    plot->zoom->xmax = plot->zoom->xmin;
	    plot->zoom->xmin = z;
	}
	if (plot->zoom->ymin > plot->zoom->ymax) {
	    z = plot->zoom->ymax;
	    plot->zoom->ymax = plot->zoom->ymin;
	    plot->zoom->ymin = z;
	}
	
	zoom_unzoom_png(plot, PNG_ZOOM);
	plot->flags ^= PLOT_ZOOMING;
	gdk_window_set_cursor(plot->canvas->window, NULL);
	gtk_statusbar_pop(GTK_STATUSBAR(plot->statusbar), plot->cid);
    }
    return TRUE;
}

static gint plot_button_press (GtkWidget *widget, GdkEventButton *event, 
			       png_plot_t *plot)
{
    if (plot_is_zooming(plot)) {
	get_data_xy(plot, event->x, event->y, 
		    &plot->zoom->xmin, &plot->zoom->ymin);
	plot->zoom->screen_xmin = event->x;
	plot->zoom->screen_ymin = event->y;
	return TRUE;
    }

    if (plot->popup) g_free(plot->popup);
    plot->popup = build_plot_menu(plot);
    gtk_menu_popup(GTK_MENU(plot->popup), NULL, NULL, NULL, NULL,
                   event->button, event->time);
    return TRUE;
}

static gboolean 
plot_key_handler (GtkWidget *w, GdkEventKey *key, gpointer data)
{
    if (key->keyval == GDK_q) {
        gtk_widget_destroy(w);
    }
    return TRUE;
}

static 
void plot_expose (GtkWidget *widget, GdkEventExpose *event,
		  GdkPixmap *dbuf_pixmap)
{
    /* Don't repaint entire window on each exposure */
    gdk_window_set_back_pixmap(widget->window, NULL, FALSE);

    /* Refresh double buffer, then copy the "dirtied" area to
       the on-screen GdkWindow */
    gdk_window_copy_area(widget->window,
			 widget->style->fg_gc[GTK_STATE_NORMAL],
			 event->area.x, event->area.y,
			 dbuf_pixmap,
			 event->area.x, event->area.y,
			 event->area.width, event->area.height);
}

static void render_pngfile (png_plot_t *plot, int view)
{
    gint width;
    gint height;
    GdkPixbuf *pbuf;
    GError *error = NULL;
    char pngname[MAXLEN];

    build_path(paths.userdir, "gretltmp.png", pngname, NULL);

    pbuf = gdk_pixbuf_new_from_file(pngname, &error);
    if (pbuf == NULL) {
        errbox(error->message);
        g_error_free(error);
	remove(pngname);
	return;
    }

    width = gdk_pixbuf_get_width(pbuf);
    height = gdk_pixbuf_get_height(pbuf);

    if (width == 0 || height == 0) {
	errbox(_("Malformed PNG file for graph"));
	g_object_unref(pbuf);
	remove(pngname);
	return;
    }

    gdk_pixbuf_render_to_drawable(pbuf, plot->pixmap, 
				  plot->canvas->style->fg_gc[GTK_STATE_NORMAL],
				  0, 0, 0, 0, width, height,
				  GDK_RGB_DITHER_NONE, 0, 0);

    g_object_unref(pbuf);
    remove(pngname);
    
    if (view != PNG_START) { 
	/* we're changing the view, so refresh the whole canvas */
	gdk_window_copy_area(plot->canvas->window,
			     plot->canvas->style->fg_gc[GTK_STATE_NORMAL],
			     0, 0,
			     plot->pixmap,
			     0, 0,
			     WIDTH, HEIGHT);
	if (view == PNG_ZOOM) {
	    plot->flags |= PLOT_ZOOMED;
	} else if (view == PNG_UNZOOM) {
	    plot->flags ^= PLOT_ZOOMED;
	}
    }
}

static void destroy_png_plot (GtkWidget *w, png_plot_t *plot)
{
    if (!plot_is_saved(plot)) {
	remove(plot->spec->fname);
    }

    /* if the png plot has a controller, we'll
       destroy it too */
    if (plot_has_controller(plot)) {
	plot->spec->ptr = NULL;
	gtk_widget_destroy(gpt_control);
    }

    if (plot->zoom) free(plot->zoom);
    if (plot->invert_gc) {
	gdk_gc_destroy(plot->invert_gc);
    }

    free(plot);
}

static int get_plot_yrange (png_plot_t *plot)
{
    FILE *fpin, *fpout;
    char line[MAXLEN], dumbgp[MAXLEN], dumbtxt[MAXLEN];
    gchar *plotcmd = NULL;
    int err = 0, x2axis = 0;

    fpin = fopen(plot->spec->fname, "r");
    if (fpin == NULL) return 1;

    build_path(paths.userdir, "dumbplot.gp", dumbgp, NULL);
    build_path(paths.userdir, "gptdumb.txt", dumbtxt, NULL);
    fpout = fopen(dumbgp, "w");
    if (fpout == NULL) {
	fclose(fpin);
	return 1;
    }

    /* switch to the "dumb" (ascii) terminal in gnuplot */
    while (fgets(line, MAXLEN-1, fpin)) {
	if (strstr(line, "set term")) 
	    fputs("set term dumb\n", fpout);
	else if (strstr(line, "set output")) 
	    fprintf(fpout, "set output '%s'\n", dumbtxt);
	else fputs(line, fpout);
	if (strstr(line, "x2range")) x2axis = 1;
    }

    fclose(fpin);
    fclose(fpout);

    plotcmd = g_strdup_printf("\"%s\" \"%s\"", paths.gnuplot,
			      dumbgp);

#ifdef G_OS_WIN32
    err = winfork(plotcmd, NULL, SW_SHOWMINIMIZED, 0);
#else
    err = system(plotcmd);
#endif
    
    g_free(plotcmd);
    remove(dumbgp);

    if (err) 
	return 1;
    else {
	double y[16];
	int i = 0;

	fpin = fopen(dumbtxt, "r");
	if (fpin == NULL) return 1;

	/* read the y-axis min and max from the ascii graph */
#ifdef ENABLE_NLS
	setlocale(LC_NUMERIC, "C");
#endif
	while (i<16 && fgets(line, MAXLEN-1, fpin)) {
	    if (sscanf(line, "%lf", &(y[i])) == 1) i++;
	}
#ifdef ENABLE_NLS
	setlocale(LC_NUMERIC, "");
#endif

	fclose(fpin);
	remove(dumbtxt);

	if (x2axis) {
	    if (i > 3 && y[1] > y[i-2]) {
		plot->ymin = y[i-2];
		plot->ymax = y[1];
	    }
	} else {	
	    if (i > 2 && y[0] > y[i-2]) {
		plot->ymin = y[i-2];
		plot->ymax = y[0];
	    }
	}
    }
    
    return 0;
}

static int get_plot_ranges (png_plot_t *plot)
{
    FILE *fp;
    char line[MAXLEN];
    int got_x = 0, got_pd = 0;

    plot->xmin = plot->xmax = 0.0;
    plot->ymin = plot->ymax = 0.0;   
    plot->xint = plot->yint = 0;
    plot->pd = 0;
    plot->title = 0;

    fp = fopen(plot->spec->fname, "r");
    if (fp == NULL) return 0;

#ifdef ENABLE_NLS
    setlocale(LC_NUMERIC, "C");
#endif
    while (fgets(line, MAXLEN-1, fp)) {
	if (strstr(line, "# range-mean")) {
	    plot->flags |= PLOT_RANGE_MEAN;
	}
	if (sscanf(line, "set xrange [%lf:%lf]", 
		   &plot->xmin, &plot->xmax) == 2) { 
	    got_x = 1;
	} else if (sscanf(line, "# timeseries %d", &plot->pd) == 1) {
	    got_pd = 1;
	} else if (!strncmp(line, "set title", 9)) {
	    plot->title = 1;
	}	
	if (!strncmp(line, "plot ", 5)) break;
    }
#ifdef ENABLE_NLS
    setlocale(LC_NUMERIC, "");
#endif

    fclose(fp);

    if (got_x) {
	int ymin = (plot->title)? PLOTYMIN : NOTITLE_YMIN;

	get_plot_yrange(plot);
	if ((plot->xmax - plot->xmin) / (PLOTXMAX - PLOTXMIN) >= 1.0)
	    plot->xint = 1;
	if ((plot->ymax - plot->ymin) / (PLOTYMAX - ymin) >= 1.0)
	    plot->yint = 1;
    }

    return got_x;
}

int gnuplot_show_png (const char *plotfile, GPT_SPEC *spec, int saved)
{
    png_plot_t *plot;
    int plot_has_xrange;

    GtkWidget *vbox;
    GtkWidget *canvas_hbox;
    GtkWidget *label_frame = NULL;
    GtkWidget *status_hbox = NULL;

    plot = mymalloc(sizeof *plot);
    if (plot == NULL) return 1;

    if (spec != NULL) {
	plot->spec = spec;
    } else {
	plot->spec = plotspec_new();
	if (plot->spec == NULL) return 1;
	strcpy(plot->spec->fname, plotfile);
    }

    /* make png plot struct accessible via spec */
    plot->spec->ptr = plot;

    plot->popup = NULL;
    plot->invert_gc = NULL;        

    plot->zoom = malloc(sizeof *plot->zoom);
    if (plot->zoom == NULL) return 1;

    plot->flags = 0;
    if (saved) {
	plot->flags |= PLOT_SAVED;
    }

    /* parse this file for x range */
    plot_has_xrange = get_plot_ranges(plot);

    plot->shell = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_widget_ref(plot->shell);
    gtk_window_set_title(GTK_WINDOW(plot->shell), _("gretl: gnuplot graph")); 

    vbox = gtk_vbox_new(FALSE, 2);
    gtk_container_add(GTK_CONTAINER(plot->shell), vbox);

    g_signal_connect(G_OBJECT(plot->shell), "destroy",
		     G_CALLBACK(destroy_png_plot), plot);
    g_signal_connect(G_OBJECT(plot->shell), "key_press_event", 
		     G_CALLBACK(plot_key_handler), plot);

    /* box to hold canvas */
    canvas_hbox = gtk_hbox_new(FALSE, 1);
    gtk_box_pack_start(GTK_BOX(vbox), canvas_hbox, TRUE, TRUE, 0);
    gtk_widget_show(canvas_hbox);

    /*  eventbox and hbox for status area  */
    if (plot_has_xrange) {
	plot->statusarea = gtk_event_box_new();
	gtk_box_pack_start(GTK_BOX(vbox), plot->statusarea, FALSE, FALSE, 0);

	status_hbox = gtk_hbox_new (FALSE, 2);
	gtk_container_add(GTK_CONTAINER(plot->statusarea), status_hbox);
	gtk_widget_show (status_hbox);
	gtk_container_set_resize_mode (GTK_CONTAINER (status_hbox),
				       GTK_RESIZE_QUEUE);
    }

    /* Create drawing-area widget */
    plot->canvas = gtk_drawing_area_new();
    gtk_widget_set_size_request(GTK_WIDGET(plot->canvas), WIDTH, HEIGHT);
    gtk_widget_set_events (plot->canvas, GDK_EXPOSURE_MASK
                           | GDK_LEAVE_NOTIFY_MASK
                           | GDK_BUTTON_PRESS_MASK
                           | GDK_BUTTON_RELEASE_MASK
                           | GDK_POINTER_MOTION_MASK
                           | GDK_POINTER_MOTION_HINT_MASK);

    GTK_WIDGET_SET_FLAGS (plot->canvas, GTK_CAN_FOCUS);
    /*
      g_object_set_user_data (G_OBJECT (plot->canvas), (gpointer) plot);
    */
    g_signal_connect(G_OBJECT(plot->canvas), "button_press_event", 
		     G_CALLBACK(plot_button_press), plot);

    g_signal_connect(G_OBJECT(plot->canvas), "button_release_event", 
		     G_CALLBACK(plot_button_release), plot);

    /* create the contents of the status area */
    plot->statusbar = NULL;
    if (plot_has_xrange) {

	/*  the cursor label (position indicator) */
	label_frame = gtk_frame_new(NULL);
	gtk_frame_set_shadow_type(GTK_FRAME(label_frame), GTK_SHADOW_IN);

	plot->cursor_label = gtk_label_new(" ");
	gtk_container_add(GTK_CONTAINER(label_frame), plot->cursor_label);
	gtk_widget_show(plot->cursor_label);

	/*  the statusbar  */
	plot->statusbar = gtk_statusbar_new();
	gtk_widget_set_size_request(plot->statusbar, 1, -1);
	gtk_container_set_resize_mode(GTK_CONTAINER (plot->statusbar),
				      GTK_RESIZE_QUEUE);
	plot->cid = gtk_statusbar_get_context_id (GTK_STATUSBAR (plot->statusbar),
						  "plot_message");
	gtk_statusbar_push (GTK_STATUSBAR (plot->statusbar),
			    plot->cid, _(" Click on graph for pop-up menu"));
	g_signal_connect (G_OBJECT (plot->canvas), "motion_notify_event",
			  G_CALLBACK(motion_notify_event), plot);
    }

    /* pack the widgets */

    gtk_box_pack_start(GTK_BOX(canvas_hbox), plot->canvas, FALSE, FALSE, 0);

    /*  fill the status area  */
    if (plot_has_xrange) {
	gtk_box_pack_start(GTK_BOX(status_hbox), label_frame, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(status_hbox), plot->statusbar, TRUE, TRUE, 0);
    }

    /* show stuff */
    gtk_widget_show(plot->canvas);

    if (plot_has_xrange) {
	gtk_widget_show(label_frame);
	gtk_widget_show(plot->statusbar);
	gtk_widget_show(plot->statusarea);
    }

    gtk_widget_realize (plot->canvas);
    gdk_window_set_back_pixmap (plot->canvas->window, NULL, FALSE);

    if (plot_has_xrange) {
	gtk_widget_realize (plot->cursor_label);
	gtk_widget_set_size_request (plot->cursor_label, 140, -1);
    }

    gtk_widget_show(vbox);
    gtk_widget_show(plot->shell);       

    /*  set the focus to the canvas area  */
    gtk_widget_grab_focus (plot->canvas);  

    plot->pixmap = gdk_pixmap_new(plot->shell->window, WIDTH, HEIGHT, -1);
    g_signal_connect(G_OBJECT(plot->canvas), "expose_event",
		     G_CALLBACK(plot_expose), plot->pixmap);

    render_pngfile(plot, PNG_START);

    return 0;
}

#ifdef G_OS_WIN32

/* win32: copy plot to clipboard by generating an EMF file
   (enhanced metafile), reading it into a buffer, and putting
   it on the clipboard.
*/

/* Weirdness: when an emf is put on the clipboard as below (it doesn't
   matter whether the short method is used, or the long one that is
   invoked when CLIPTEST is defined), Word 2000 behaves thus: a
   straight "Paste" puts in a version of the graph with squashed up
   numbers on the axes and no legend text; but a "Paste special"
   (where one accepts the default of pasting it as an enhanced
   metafile) puts in an accurate version with correct text.  Go
   figure.
*/

/* #define CLIPTEST 1 */

#ifdef CLIPTEST
static int emf_to_clip (char *emfname)
{
    LPBYTE emfbuf;
    UINT emflen;
    HENHMETAFILE hemf, hemfclip, hemftest;

    hemf = GetEnhMetaFile(emfname);
    if (hemf == NULL) {
        errbox(_("Gnuplot error creating graph"));
	return 1;
    }

    emflen = GetEnhMetaFileBits(hemf, 0, NULL);
    if (emflen == 0) {
        errbox(_("Gnuplot error creating graph"));
	return 1;
    }

    emfbuf = mymalloc(emflen);
    if (emfbuf == NULL) {
	DeleteEnhMetaFile(hemf);
	return 1;
    }

    emflen = GetEnhMetaFileBits(hemf, emflen, emfbuf);
    DeleteEnhMetaFile(hemf); /* close the handle */

    sprintf(errtext, "Read %u bytes from emf file", emflen);
    infobox(errtext);

    if (!OpenClipboard(NULL)) {
	errbox(_("Cannot open the clipboard"));
	return 1;
    }

    EmptyClipboard();

    hemfclip = SetEnhMetaFileBits(emflen, emfbuf);
    SetClipboardData(CF_ENHMETAFILE, hemfclip);
    
    CloseClipboard();

    /* test */
    hemftest = CopyEnhMetaFile(hemfclip, "c:\\userdata\\gretl\\user\\clipbd.emf");
    DeleteEnhMetaFile(hemftest);

    free(emfbuf);

    return 0;
}
#else
static int emf_to_clip (char *emfname)
{
    HENHMETAFILE hemf, hemfclip;

    if (!OpenClipboard(NULL)) {
	errbox(_("Cannot open the clipboard"));
	return 1;
    }

    EmptyClipboard();

    hemf = GetEnhMetaFile(emfname);
    hemfclip = CopyEnhMetaFile(hemf, NULL);
    SetClipboardData(CF_ENHMETAFILE, hemfclip);

    CloseClipboard();

    DeleteEnhMetaFile(hemf);

    return 0;
}
#endif /* CLIPTEST */

static void gnuplot_graph_to_clipboard (GPT_SPEC *spec)
{
    FILE *fq;
    PRN *prn;
    char plottmp[MAXLEN], plotline[MAXLEN];
    gchar *plotcmd = NULL;
    gchar *emfname = NULL;
    int err;

    /* create temporary file to hold the special gnuplot commands */
    if (!user_fopen("gptout.tmp", plottmp, &prn)) return;

    /* open the gnuplot source file for the graph */
    fq = fopen(spec->fname, "r");
    if (fq == NULL) {
	errbox(_("Couldn't access graph info"));
	gretl_print_destroy(prn);
	return;
    }

    /* generate gnuplot source file to make emf */
    emfname = g_strdup_printf("%sgpttmp.emf", paths.userdir);
    pprintf(prn, "set term emf\n");
    pprintf(prn, "set output '%s'\n", emfname);
    while (fgets(plotline, MAXLEN-1, fq)) {
	if (strncmp(plotline, "set term", 8) && 
	    strncmp(plotline, "set output", 10))
	    pprintf(prn, "%s", plotline);
    }

    gretl_print_destroy(prn);
    fclose(fq);

    /* get gnuplot to create the emf file */
    plotcmd = g_strdup_printf("\"%s\" \"%s\"", paths.gnuplot, 
			      plottmp);
    err = winfork(plotcmd, NULL, SW_SHOWMINIMIZED, 0);
    g_free(plotcmd);
    remove(plottmp);
    
    if (err) {
        errbox(_("Gnuplot error creating graph"));
    } else {
	/* copy from emf on disk onto clipboard */
	emf_to_clip(emfname);
    }

    remove(emfname);
    g_free(emfname);
}

#endif /* G_OS_WIN32 */

#endif /* GNUPLOT_PNG */
