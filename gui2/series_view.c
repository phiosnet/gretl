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

/* series_view.c for gretl */

#include "gretl.h"
#include "textutil.h"
#include "dlgutils.h"
#include "menustate.h"
#include "textbuf.h"
#include "ssheet.h"
#include "series_view.h"

#include "libset.h"

#ifdef G_OS_WIN32
# include "gretlwin32.h"
#else
# include "clipboard.h"
#endif

enum {
    VIEW_STANDARD,
    VIEW_CUSTOM
};

typedef struct data_point_t data_point;

struct data_point_t {
    int obsnum;
    double val;
};

struct series_view_t {
    int varnum;
    int *list;
    int sortvar;
    int npoints;
    char view;
    int digits;
    char format; 
    char sorted;
    data_point *points;
};

static void series_view_unsort (series_view *sview);

void free_series_view (gpointer p)
{
    series_view *sview = (series_view *) p;

    if (sview == NULL) return;

    if (sview->points != NULL) {
	free(sview->points);
    }

    if (sview->list != NULL) {
	free(sview->list);
    }

    free(sview);
}

static void series_view_fill_points (series_view *sview)
{
    int v, t, s = 0;

    v = (sview->varnum > 0)? sview->varnum : sview->sortvar;

    for (t=datainfo->t1; t<=datainfo->t2; t++) {
	sview->points[s].obsnum = t;
	sview->points[s].val = Z[v][t];
	s++;
    }
}

static int series_view_allocate (series_view *sview)
{
    int err = 0;

    if (sview->points != NULL) {
	/* already allocated */
	return 0;
    } else {
	int T = sample_size(datainfo);

	sview->points = mymalloc(T * sizeof *sview->points);
	if (sview->points == NULL) {
	    err = E_ALLOC;
	} else {
	    sview->npoints = T;
	}
    }

    if (sview->varnum > 0) {
	/* single series view */
	series_view_fill_points(sview);
    }

    return err;
}

static PRN *single_series_view_print_formatted (windata_t *vwin, 
						PrnFormat fmt)
{
    series_view *sview = (series_view *) vwin->data;
    char obslabel[OBSLEN];
    char dchar = datainfo->delim;
    double x;
    PRN *prn;
    int i, t;

    if (bufopen(&prn)) {
	return NULL;
    }

    pprintf(prn, "obs%c%s\n", dchar, datainfo->varname[sview->varnum]);

    for (i=0; i<sview->npoints; i++) {
	t = sview->points[i].obsnum;
	x = sview->points[i].val;
	if (dataset_has_markers(datainfo)) {
	    strcpy(obslabel, datainfo->S[t]);
	} else {
	    ntodate(obslabel, t, datainfo);
	}
	if (na(x)) {
	    pprintf(prn, "%s%cNA\n", obslabel, dchar);
	} else {
	    pprintf(prn, "\"%s\"%c%.8g\n", obslabel, dchar, x);
	}
    }

    return prn;
}

/* for printing sorted or reformatted data, for a window displaying
   a single series */

static void single_series_view_print (windata_t *vwin)
{
    series_view *sview = (series_view *) vwin->data;
    char num_format[32];
    char obslabel[OBSLEN];
    int thislen, obslen = 0;
    double x;
    PRN *prn;
    int i, t, err = 0;

    if (bufopen(&prn)) {
	return;
    }

    if (sview->view == VIEW_STANDARD) {
	int list[2] = { 1, sview->varnum };

	/* regular printout: unsort if need be */
	if (sview->sorted) {
	    series_view_unsort(sview);
	}
	
	err = printdata(list, NULL, (const double **) Z, 
			datainfo, OPT_O, prn);
	if (err) {
	    gui_errmsg(err);
	} 
	goto finalize;
    }

    obslen = max_obs_label_length(datainfo);
    if (obslen < 2) {
	obslen = -2;
    }

    if (sview->format == 'g') {
	sprintf(num_format, "%%*s %%#13.%dg\n", sview->digits);
    } else {
	sprintf(num_format, "%%*s %%13.%df\n", sview->digits);
    }
	
    pprintf(prn, "\n%*s ", obslen, "");
    pprintf(prn, "%13s\n\n", datainfo->varname[sview->varnum]);

    for (i=0; i<sview->npoints; i++) {
	t = sview->points[i].obsnum;
	x = sview->points[i].val;
	if (dataset_has_markers(datainfo)) {
	    strcpy(obslabel, datainfo->S[t]);
	    thislen = get_utf_width(obslabel, obslen);
	} else {
	    ntodate(obslabel, t, datainfo);
	    thislen = obslen;
	}
	if (na(x)) {
	    pprintf(prn, "%*s\n", thislen, obslabel);
	} else {
	    pprintf(prn, num_format, thislen, obslabel, x);
	} 
    }

 finalize:

    if (!err) {
	textview_set_text(vwin->text, gretl_print_get_buffer(prn));
    }

    gretl_print_destroy(prn);
}

static int *make_obsvec (series_view *sview)
{
    int *ov;
    int t;

    ov = mymalloc((sview->npoints + 1) * sizeof *ov);

    if (ov != NULL) {
	ov[0] = sview->npoints;
	for (t=0; t<sview->npoints; t++) {
	    ov[t+1] = sview->points[t].obsnum;
	}
    }

    return ov;
}

static void multi_series_view_print_sorted (windata_t *vwin)
{
    series_view *sview = (series_view *) vwin->data;
    int *obsvec = make_obsvec(sview);
    PRN *prn;
    int err = 0;

    if (obsvec == NULL) {
	return;
    }

    if (bufopen(&prn)) {
	free(obsvec);
	return;
    }

    err = print_data_sorted(sview->list, obsvec, (const double **) Z, 
			    datainfo, prn);
    if (err) {
	gui_errmsg(err);
    } else {
	textview_set_text(vwin->text, gretl_print_get_trimmed_buffer(prn));
    }

    free(obsvec);
    gretl_print_destroy(prn);
}

static void multi_series_view_print (windata_t *vwin)
{
    series_view *sview = (series_view *) vwin->data;
    PRN *prn;
    int err = 0;

    if (bufopen(&prn)) {
	return;
    }

    if (sview->view == VIEW_STANDARD) {
	err = printdata(sview->list, NULL, (const double **) Z, 
			datainfo, OPT_O, prn);
    } else {
	err = print_series_with_format(sview->list, (const double **) Z, 
				       datainfo, sview->format, 
				       sview->digits, prn);
    } 

    if (err) {
	gui_errmsg(err);
    } else {
	textview_set_text(vwin->text, gretl_print_get_trimmed_buffer(prn));
    }

    gretl_print_destroy(prn);
}

int series_view_is_sorted (windata_t *vwin)
{
    series_view *sview = (series_view *) vwin->data;

    if (sview != NULL) {
	return sview->sorted;
    } else {
	return 0;
    }
}

PRN *vwin_print_sorted_with_format (windata_t *vwin, PrnFormat fmt)
{
    series_view *sview;
    int *obsvec;
    PRN *prn;
    int err = 0;

    if (vwin->role == VIEW_SERIES) {
	/* a single series */
	return single_series_view_print_formatted(vwin, fmt);
    }

    sview = (series_view *) vwin->data;

    obsvec = make_obsvec(sview);
    if (obsvec == NULL) {
	return NULL;
    }

    if (bufopen(&prn)) {
	free(obsvec);
	return NULL;
    }

    gretl_print_set_format(prn, fmt);
    err = print_data_sorted(sview->list, obsvec, (const double **) Z, 
			    datainfo, prn);
    if (err) {
	gui_errmsg(err);
	gretl_print_destroy(prn);
	prn = NULL;
    } 

    free(obsvec);

    return prn;
}

static int sort_points (const void *a, const void *b)
{
    const data_point *pa = (const data_point *) a;
    const data_point *pb = (const data_point *) b;
     
    return (pa->val > pb->val) - (pa->val < pb->val);
}

static int unsort_points (const void *a, const void *b)
{
    const data_point *pa = (const data_point *) a;
    const data_point *pb = (const data_point *) b;
     
    return (pa->obsnum > pb->obsnum) - (pa->obsnum < pb->obsnum);
}

static void series_view_unsort (series_view *sview)
{
    qsort(sview->points, sview->npoints, 
	  sizeof sview->points[0], unsort_points);
    sview->sorted = 0;
}

void series_view_toggle_sort (GtkWidget *w, windata_t *vwin)
{
    series_view *sview = (series_view *) vwin->data;
    
    if (series_view_allocate(sview)) {
	return;
    }

    if (!sview->sorted) {
	qsort(sview->points, sview->npoints, 
	      sizeof sview->points[0], sort_points);
	sview->sorted = 1;
	sview->view = VIEW_CUSTOM;
    } else {
	qsort(sview->points, sview->npoints, 
	      sizeof sview->points[0], unsort_points);
	sview->sorted = 0;
	sview->view = VIEW_STANDARD;
    }	

    single_series_view_print(vwin);
}

void multi_series_view_sort_by (GtkWidget *w, windata_t *vwin)
{
    series_view *sview = (series_view *) vwin->data;
    int v;

    if (sview == NULL || sview->list == NULL) {
	return;
    }

    if (sview->sorted && sview->points != NULL) {
	/* toggle back to unsorted */
	qsort(sview->points, sview->npoints, 
	      sizeof sview->points[0], unsort_points);
	sview->sorted = 0;
	sview->view = VIEW_STANDARD;
	multi_series_view_print(vwin);
	return;
    }

    if (series_view_allocate(sview)) {
	return;
    }

    v = select_var_from_list(sview->list, _("Variable to sort by"));
    if (v < 0) {
	/* canceled */
	return;
    }

    sview->sortvar = v;
    series_view_fill_points(sview);

    qsort(sview->points, sview->npoints, 
	  sizeof sview->points[0], sort_points);
    sview->sorted = 1;
    sview->view = VIEW_CUSTOM;
    multi_series_view_print_sorted(vwin);
}

void series_view_graph (GtkWidget *w, windata_t *vwin)
{
    series_view *sview = (series_view *) vwin->data;

    if (sview == NULL || sview->varnum == 0) {
	return;
    }

    if (dataset_is_time_series(datainfo)) {
	do_graph_var(sview->varnum);
    } else {
	do_boxplot_var(sview->varnum, OPT_NONE);
    }
}

void series_view_edit (GtkWidget *w, windata_t *vwin)
{
    series_view *sview = (series_view *) vwin->data;

    if (sview != NULL && sview->varnum > 0 && sview->varnum < datainfo->v) {
	show_spreadsheet_for_series(sview->varnum);
    }
}

void series_view_refresh (GtkWidget *w, windata_t *vwin)
{
    series_view *sview = (series_view *) vwin->data;

    if (sview != NULL && sview->varnum > 0 && sview->varnum < datainfo->v) {
	int list[2] = {1, sview->varnum};
	PRN *prn = NULL;
	int err;

	if (bufopen(&prn)) {
	    return;
	}

	err = printdata(list, NULL, (const double **) Z, datainfo, OPT_O, prn);

	if (err) {
	    gui_errmsg(err);
	    gretl_print_destroy(prn);
	} else {
	    /* replace text buffer in sview using prn */
	    const char *buf = gretl_print_get_trimmed_buffer(prn);

	    textview_set_text(vwin->text, buf);
	    gretl_print_destroy(prn);
	}
    }
}

int *series_view_get_list (windata_t *vwin)
{
    series_view *sview = vwin->data;
    int *list = NULL;

    if (sview == NULL) {
	return NULL;
    }

    if (vwin->role == VIEW_SERIES) {
	list = gretl_list_new(1);
	if (list != NULL) {
	    list[1] = sview->varnum;
	}
    } else {
	list = gretl_list_copy(sview->list);
    }

    return list;
}

int has_sortable_data (windata_t *vwin)
{
    series_view *sview;

    if (vwin == NULL || vwin->role != PRINT || vwin->data == NULL) {
	return 0;
    }

    sview = vwin->data;

    return (sview->list != NULL && sview->list[0] < 5);
}

int can_format_data (windata_t *vwin)
{
    if (vwin->role == VIEW_SERIES || vwin->role == VIEW_MODELTABLE) {
	return 1;
    } else if (vwin->role == PRINT && vwin->data != NULL) {
	series_view *sview = vwin->data;

	return (sview->list != NULL);
    } else {
	return 0;
    }
}

static series_view *series_view_new (int varnum, const int *list)
{
    series_view *sview = NULL;

    if (varnum == 0 && list == NULL) {
	return NULL;
    } 

    sview = malloc(sizeof *sview);
    if (sview == NULL) {
	return NULL;
    }

    if (list == NULL) {
	sview->list = NULL;
    } else {
	sview->list = gretl_list_copy(list);
	if (sview->list == NULL) {
	    free(sview);
	    sview = NULL;
	}
    } 

    if (sview != NULL) {
	sview->varnum = varnum;
	sview->sortvar = 0;
	sview->npoints = 0;
	sview->view = VIEW_STANDARD;
	sview->digits = 6;
	sview->format = 'g';
	sview->sorted = 0;
	sview->points = NULL;
    }

    return sview;
}

void series_view_connect (windata_t *vwin, int varnum)
{
    series_view *sview = series_view_new(varnum, NULL);

    vwin->data = sview;
}

series_view *multi_series_view_new (const int *list)
{
    return series_view_new(0, list);
}

struct view_toggler {
    char *view;
    GtkWidget *spin;
    GtkWidget *combo;
};

/* toggle between standard and custom views */

static void series_view_toggle_view (GtkWidget *w, struct view_toggler *vt)
{
    gint i = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(w), "view"));

    if (button_is_active(w)) {
        *vt->view = i;
	gtk_widget_set_sensitive(vt->spin, i == VIEW_CUSTOM);
	if (vt->combo != NULL) {
	    gtk_widget_set_sensitive(vt->combo, i == VIEW_CUSTOM);
	}
    }
}

static void series_view_set_digits (GtkSpinButton *b, int *digits)
{
    *digits = gtk_spin_button_get_value_as_int(b);
}

static void series_view_set_fmt (GtkComboBox *cb, char *format)
{
    gint i = gtk_combo_box_get_active(cb);

    *format = (i == 0)? 'g' : 'f';
}

static void real_view_format_dialog (GtkWidget *src, windata_t *vwin,
				     series_view *sview)
{
    struct view_toggler vt;
    GtkWidget *dlg, *vbox;
    GtkWidget *tmp, *hbox;
    GtkWidget *b1, *b2;
    GSList *group;
    int std;

    dlg = gretl_dialog_new(_("gretl: data format"), NULL,
			   GRETL_DLG_BLOCK | GRETL_DLG_MODAL);

    vbox = gtk_dialog_get_content_area(GTK_DIALOG(dlg));

    std = (sview->view == VIEW_STANDARD);
    vt.view = &sview->view;

    hbox = gtk_hbox_new(FALSE, 5);
    tmp = gtk_label_new(_("Select data format"));
    gtk_box_pack_start(GTK_BOX(hbox), tmp, FALSE, FALSE, 5);
    gtk_box_pack_start(GTK_BOX(vbox), hbox, TRUE, TRUE, 5);

    /* standard format radio option */
    hbox = gtk_hbox_new(FALSE, 5);
    b1 = gtk_radio_button_new_with_label(NULL, _("Standard format"));
    g_object_set_data(G_OBJECT(b1), "view", GINT_TO_POINTER(VIEW_STANDARD));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(b1), std);
    gtk_box_pack_start(GTK_BOX(hbox), b1, TRUE, TRUE, 5);
    gtk_box_pack_start(GTK_BOX(vbox), hbox, TRUE, TRUE, 5);

    /* custom format radio option */
    hbox = gtk_hbox_new(FALSE, 5);
    group = gtk_radio_button_get_group(GTK_RADIO_BUTTON(b1));
    b2 = gtk_radio_button_new_with_label(group, _("Show"));
    g_object_set_data(G_OBJECT(b2), "view", GINT_TO_POINTER(VIEW_CUSTOM));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(b2), !std);
    gtk_box_pack_start(GTK_BOX(hbox), b2, FALSE, FALSE, 5);

    /* with spinner for number of digits */
    vt.spin = gtk_spin_button_new_with_range(1, 15, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(vt.spin), sview->digits);
    g_signal_connect(G_OBJECT(vt.spin), "value-changed",
		     G_CALLBACK(series_view_set_digits), &sview->digits);
    gtk_widget_set_sensitive(vt.spin, !std);
    gtk_box_pack_start(GTK_BOX(hbox), vt.spin, FALSE, FALSE, 0);

    /* and selector for digits / decimal places */
    vt.combo = gtk_combo_box_text_new();
    combo_box_append_text(vt.combo, _("significant figures"));
    combo_box_append_text(vt.combo, _("decimal places"));
    if (sview->format == 'g') {
	gtk_combo_box_set_active(GTK_COMBO_BOX(vt.combo), 0);
    } else {
	gtk_combo_box_set_active(GTK_COMBO_BOX(vt.combo), 1);
    }
    g_signal_connect(G_OBJECT(GTK_COMBO_BOX(vt.combo)), "changed",
		     G_CALLBACK(series_view_set_fmt), &sview->format);
    gtk_widget_set_sensitive(vt.combo, !std);
    gtk_box_pack_start(GTK_BOX(hbox), vt.combo, FALSE, FALSE, 5);

    /* connect toggle signals */
    g_signal_connect(G_OBJECT(b1), "toggled",
                     G_CALLBACK(series_view_toggle_view), &vt);
    g_signal_connect(G_OBJECT(b2), "toggled",
                     G_CALLBACK(series_view_toggle_view), &vt);

    /* pack the custom line */
    gtk_box_pack_start(GTK_BOX(vbox), hbox, TRUE, TRUE, 5);

    hbox = gtk_dialog_get_action_area(GTK_DIALOG(dlg));

    /* Cancel button */
    cancel_options_button(hbox, dlg, &sview->digits);
   
    /* OK button */
    tmp = ok_button(hbox);
    g_signal_connect(G_OBJECT(tmp), "clicked",
		     G_CALLBACK(delete_widget), dlg);
    gtk_widget_grab_default(tmp);

    gtk_widget_show_all(dlg);

    if (sview->digits > 0) {
	if (sview->list != NULL) {
	    multi_series_view_print(vwin);
	} else {
	    single_series_view_print(vwin);
	}
    } else { 
	/* canceled */
	sview->digits = 6;
    }
}

void series_view_format_dialog (GtkWidget *src, windata_t *vwin)
{
    series_view *sview = (series_view *) vwin->data;

    if (vwin->role == VIEW_SERIES && series_view_allocate(sview)) {
	return;
    }

    real_view_format_dialog(src, vwin, sview);
}

