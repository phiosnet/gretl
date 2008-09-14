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

/* dialogs.c for gretl */

#include "gretl.h"
#include "cmdstack.h"
#include "session.h"
#include "obsbutton.h"
#include "textutil.h"
#include "menustate.h"
#include "dlgutils.h"
#include "ssheet.h"
#include "database.h"
#include "selector.h"
#include "gretl_panel.h"
#include "texprint.h"

#include <errno.h>

static int all_done;

static GtkWidget *option_spinbox (int *spinvar, const char *spintxt,
				  int spinmin, int spinmax,
				  int hcode, gpointer p);
static GtkWidget *option_checkbox (int *checkvar, const char *checktxt);
static void set_radio_opt (GtkWidget *w, int *opt);

void menu_exit_check (void)
{
    if (!exit_check()) {
	gtk_main_quit();
    }
}

static void save_data_callback (void)
{
    data_save_selection_wrapper(SAVE_DATA, NULL);
    if (data_status & MODIFIED_DATA)
	data_status ^= MODIFIED_DATA;
    /* FIXME: need to do more here? */
}

gint yes_no_dialog (const char *title, const char *msg, int cancel)
{
    GtkWidget *dialog, *label, *hbox;
    int ret = GTK_RESPONSE_HELP;

    dialog = gtk_dialog_new_with_buttons (title,
					  NULL,
					  GTK_DIALOG_MODAL | 
					  GTK_DIALOG_DESTROY_WITH_PARENT,
					  GTK_STOCK_YES,
					  GTK_RESPONSE_ACCEPT,
					  GTK_STOCK_NO,
					  GTK_RESPONSE_NO,
					  NULL);
    
    if (cancel) {
	gtk_dialog_add_button (GTK_DIALOG(dialog),
			       GTK_STOCK_CANCEL,
			       GTK_RESPONSE_REJECT);
    }

    label = gtk_label_new(msg);
    gtk_widget_show(label);
    hbox = gtk_hbox_new(FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, FALSE, 12);
    gtk_widget_show(hbox);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->vbox),
		       hbox, FALSE, FALSE, 12);

    gtk_dialog_set_has_separator(GTK_DIALOG(dialog), FALSE);

    ret = gtk_dialog_run(GTK_DIALOG(dialog));
					  
    gtk_widget_destroy(dialog);

    switch (ret) {
    case GTK_RESPONSE_ACCEPT: 
	return GRETL_YES; 
    case GTK_RESPONSE_NO: 
	return GRETL_NO; 
    default: 
	return GRETL_CANCEL;
    }
}

gboolean exit_check (void) 
{
    const char save_as_msg[] = {
	N_("Do you want to save this gretl session?")
    };
    const char save_msg[] = {
	N_("Do you want to save the changes you made\n"
	   "to this session?")
    };
    int resp;

    if (maybe_raise_dialog()) {
	return TRUE;
    }

    if (session_is_modified()) {
	const char *msg;
	int as_is = 0;

	if (session_file_is_open()) {
	    msg = save_msg;
	    as_is = 1;
	} else {
	    msg = save_as_msg;
	}

	resp = yes_no_dialog("gretl", _(msg), 1);

	if (resp == GRETL_YES) {
	    if (as_is) {
		save_session(sessionfile);
	    } else {
		save_session_callback(NULL);
	    }
	    return TRUE; /* bodge */
	} else if (resp == GRETL_CANCEL) {
	    /* resp -1 = wm close */
	    return TRUE;
	}
	/* else resp = GRETL_NO: so fall through */
    }

    if (data_status & MODIFIED_DATA) {
	resp = yes_no_dialog ("gretl", 
			      _("Do you want to save changes you have\n"
				"made to the current data set?"), 1);
	if (resp == GRETL_YES) {
	    save_data_callback();
	    return TRUE; 
	} else if (resp == GRETL_CANCEL) {
	    return TRUE;
	}
    } 

    write_rc();
    all_done = 1;

    return FALSE;
}

double gui_double_from_string (const char *str, int *err)
{
    double x = 0;
    char s[32];
    int sub = 0;

    *s = '\0';
    strncat(s, str, 31);

    if (get_local_decpoint() != '.') {
	gretl_push_c_numeric_locale();
	charsub(s, ',', '.');
	sub = 1;
    }

    *err = check_atof(s);

    if (*err) {
	gui_errmsg(*err);
    } else {
	x = atof(s);
    }

    if (sub) {
	gretl_pop_c_numeric_locale();
    }

    return x;
}

/* CSV files: setting the delimiter */

typedef struct {
    GtkWidget *space_button;
    GtkWidget *point_button;
    gint delim;
    gint decpoint;
} csv_stuff;

#ifdef ENABLE_NLS
static void set_dec (GtkWidget *w, csv_stuff *csv)
{
    gint i;

    if (GTK_TOGGLE_BUTTON(w)->active) {
	i = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(w), "action"));
	csv->decpoint = i;
	if (csv->decpoint == ',' && csv->delim == ',') {
	    csv->delim = ' ';
	    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (csv->space_button), 
					  TRUE);
	}
    }
}
#endif

static void set_delim (GtkWidget *w, csv_stuff *csv)
{
    gint i;

    if (GTK_TOGGLE_BUTTON(w)->active) {
	i = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(w), "action"));
	csv->delim = i;
	if (csv->point_button != NULL && 
	    csv->delim == ',' && csv->decpoint == ',') {
	    csv->decpoint = '.';
	    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(csv->point_button), 
					 TRUE);
	}
    }
}

static void set_obscol (GtkWidget *w, gretlopt *optp)
{
    if (GTK_TOGGLE_BUTTON(w)->active) {
	*optp = OPT_NONE;
    } else {
	/* omit-obs option to "store" command */
	*optp = OPT_X;
    }
}

static void really_set_csv_stuff (GtkWidget *w, csv_stuff *csv)
{
    datainfo->delim = csv->delim;
    datainfo->decpoint = csv->decpoint;
}

static void destroy_delim_dialog (GtkWidget *w, gint *p)
{
    free(p);
}

int delimiter_dialog (gretlopt *optp)
{
    GtkWidget *dialog, *tmp, *button, *hbox;
    GtkWidget *myvbox;
    GSList *group;
    csv_stuff *csvp = NULL;
    int ret = 0;

    if (maybe_raise_dialog()) {
	return -1;
    }

    csvp = mymalloc(sizeof *csvp);
    if (csvp == NULL) {
	return - 1;
    }

    csvp->delim = datainfo->delim;
    csvp->decpoint = '.';
    csvp->point_button = NULL;

    dialog = gretl_dialog_new(_("gretl: data delimiter"), NULL, GRETL_DLG_BLOCK);

    g_signal_connect(G_OBJECT(dialog), "destroy", 
		     G_CALLBACK(destroy_delim_dialog), csvp);

    myvbox = gtk_vbox_new(FALSE, 5);

    hbox = gtk_hbox_new(FALSE, 5);
    tmp = gtk_label_new(_("separator for data columns:"));
    gtk_box_pack_start(GTK_BOX(hbox), tmp, TRUE, TRUE, 5);
    gtk_widget_show(tmp);
    gtk_box_pack_start(GTK_BOX(myvbox), hbox, TRUE, TRUE, 5);
    gtk_widget_show(hbox);    

    /* comma separator */
    button = gtk_radio_button_new_with_label(NULL, _("comma (,)"));
    gtk_box_pack_start(GTK_BOX(myvbox), button, TRUE, TRUE, 0);
    if (csvp->delim == ',')
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON (button), TRUE);
    g_signal_connect(G_OBJECT(button), "clicked",
		     G_CALLBACK(set_delim), csvp);
    g_object_set_data(G_OBJECT(button), "action", 
		      GINT_TO_POINTER(','));
    gtk_widget_show(button);

    /* space separator */
    group = gtk_radio_button_get_group(GTK_RADIO_BUTTON(button));
    button = gtk_radio_button_new_with_label(group, _("space"));
    csvp->space_button = button;
    gtk_box_pack_start(GTK_BOX(myvbox), button, TRUE, TRUE, 0);
    if (csvp->delim == ' ')
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(button), TRUE);
    g_signal_connect(G_OBJECT(button), "clicked",
		     G_CALLBACK(set_delim), csvp);
    g_object_set_data(G_OBJECT(button), "action", 
		      GINT_TO_POINTER(' '));  
    gtk_widget_show(button);

    /* tab separator */
    group = gtk_radio_button_get_group(GTK_RADIO_BUTTON(button));
    button = gtk_radio_button_new_with_label(group, _("tab"));
    gtk_box_pack_start(GTK_BOX(myvbox), button, TRUE, TRUE, 0);
    if (csvp->delim == '\t') {
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(button), TRUE);
    }
    g_signal_connect(G_OBJECT(button), "clicked",
		     G_CALLBACK(set_delim), csvp);
    g_object_set_data(G_OBJECT(button), "action", 
		      GINT_TO_POINTER('\t'));    
    gtk_widget_show (button);

    /* semicolon separator */
    group = gtk_radio_button_get_group(GTK_RADIO_BUTTON(button));
    button = gtk_radio_button_new_with_label(group, _("semicolon"));
    gtk_box_pack_start(GTK_BOX(myvbox), button, TRUE, TRUE, 0);
    if (csvp->delim == ';') {
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(button), TRUE);
    }
    g_signal_connect(G_OBJECT(button), "clicked",
		     G_CALLBACK(set_delim), csvp);
    g_object_set_data(G_OBJECT(button), "action", 
		      GINT_TO_POINTER(';'));    
    gtk_widget_show (button);

#ifdef ENABLE_NLS
    if (',' == get_local_decpoint()) {
	GSList *decgroup;

	vbox_add_hsep(myvbox);

	hbox = gtk_hbox_new(FALSE, 5);
	tmp = gtk_label_new(_("decimal point character:"));
	gtk_box_pack_start(GTK_BOX(hbox), tmp, TRUE, TRUE, 5);
	gtk_widget_show(tmp);
	gtk_box_pack_start(GTK_BOX(myvbox), hbox, TRUE, TRUE, 5);
	gtk_widget_show(hbox);    

	/* period decpoint */
	button = gtk_radio_button_new_with_label(NULL, _("period (.)"));
	csvp->point_button = button;
	gtk_box_pack_start(GTK_BOX(myvbox), button, TRUE, TRUE, 0);
	if (csvp->decpoint == '.') {
	    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(button), TRUE);
	}
	g_signal_connect(G_OBJECT(button), "clicked",
			 G_CALLBACK(set_dec), csvp);
	g_object_set_data(G_OBJECT(button), "action", 
			  GINT_TO_POINTER('.'));
	gtk_widget_show(button);

	/* comma decpoint */
	decgroup = gtk_radio_button_get_group(GTK_RADIO_BUTTON(button));
	button = gtk_radio_button_new_with_label(decgroup, _("comma (,)"));
	gtk_box_pack_start(GTK_BOX(myvbox), button, TRUE, TRUE, 0);
	if (csvp->decpoint == ',') {
	    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(button), TRUE);
	}
	g_signal_connect(G_OBJECT(button), "clicked",
			 G_CALLBACK(set_dec), csvp);
	g_object_set_data(G_OBJECT(button), "action", 
			  GINT_TO_POINTER(','));   
	gtk_widget_show(button);
    }
#endif /* ENABLE_NLS */

    if (optp != NULL) {
	vbox_add_hsep(myvbox);
	tmp = gtk_check_button_new_with_label(_("include observations column"));
	g_signal_connect(G_OBJECT(tmp), "toggled", G_CALLBACK(set_obscol), optp);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(tmp), TRUE);
	gtk_box_pack_start(GTK_BOX(myvbox), tmp, TRUE, TRUE, 0);
	gtk_widget_show(tmp);
    }

    hbox = gtk_hbox_new(FALSE, 5);
    gtk_box_pack_start(GTK_BOX(hbox), myvbox, TRUE, TRUE, 5);
    gtk_widget_show(hbox);

    gtk_widget_show(myvbox);

    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->vbox), hbox, TRUE, TRUE, 5);
    gtk_widget_show(hbox);

    /* "Cancel" button */
    cancel_delete_button(GTK_DIALOG(dialog)->action_area, dialog, &ret);

    /* Create the "OK" button */
    tmp = ok_button(GTK_DIALOG(dialog)->action_area);
    g_signal_connect(G_OBJECT(tmp), "clicked",
		     G_CALLBACK(really_set_csv_stuff), csvp);
    g_signal_connect(G_OBJECT(tmp), "clicked", 
		     G_CALLBACK(delete_widget), dialog);
    gtk_widget_grab_default(tmp);
    gtk_widget_show(tmp);

    gtk_widget_show(dialog);

    return ret;
}

/* selection of format in which to copy material to clipboard,
   or save to file */

struct format_info {
    GtkWidget *dialog;
    windata_t *vwin;
    int format;
    int multi;
    int action;
};

static void destroy_format_dialog (GtkWidget *w, struct format_info *finfo)
{
    free(finfo);
}

static void copy_with_format_callback (GtkWidget *w, struct format_info *finfo)
{
    gtk_widget_hide(finfo->dialog);

    if (finfo->action == W_COPY) {
	window_copy(finfo->vwin, finfo->format);
    } else {
	window_save(finfo->vwin, finfo->format);
    }

    gtk_widget_destroy(finfo->dialog);
}

static int preferred_format (int f, int multi)
{
# ifdef G_OS_WIN32
    static int multi_pref = GRETL_FORMAT_RTF;
    static int simple_pref = GRETL_FORMAT_TXT;
# else 
    static int multi_pref = GRETL_FORMAT_TEX;
    static int simple_pref = GRETL_FORMAT_TXT;
#endif
    int ret;

    if (multi) {
	if (f) multi_pref = f;
	ret = multi_pref;
    } else {
	if (f) simple_pref = f;
	ret = simple_pref;
    }

    return ret;
}

static void set_copy_format (GtkWidget *w, struct format_info *finfo)
{
    gpointer p = g_object_get_data(G_OBJECT(w), "format");

    if (p != NULL) {
	int f = GPOINTER_TO_INT(p);

	finfo->format = f;
	if (f != GRETL_FORMAT_CSV) {
	    preferred_format(finfo->format, finfo->multi);
	}
    }
}

static GtkWidget *
TeX_copy_button (GSList *group, GtkWidget *vbox, struct format_info *finfo,
		 int pref)
{
    GtkWidget *button;

    button = gtk_radio_button_new_with_label(group, "LaTeX");
    gtk_box_pack_start(GTK_BOX(vbox), button, TRUE, TRUE, 0);
    g_signal_connect(G_OBJECT(button), "clicked",
		     G_CALLBACK(set_copy_format), finfo);
    g_object_set_data(G_OBJECT(button), "format", 
		      GINT_TO_POINTER(GRETL_FORMAT_TEX));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(button), 
				 (pref == GRETL_FORMAT_TEX));
    gtk_widget_show(button); 

    return button;
}  

static GtkWidget *
RTF_copy_button (GSList *group, GtkWidget *vbox, struct format_info *finfo,
		 int pref)
{
    GtkWidget *button;

#ifdef G_OS_WIN32
    button = gtk_radio_button_new_with_label(group, "RTF (MS Word)");
#else
    button = gtk_radio_button_new_with_label(group, "RTF");
#endif
    gtk_box_pack_start(GTK_BOX(vbox), button, TRUE, TRUE, 0);
    g_signal_connect(G_OBJECT(button), "clicked",
		     G_CALLBACK(set_copy_format), finfo);
    if (finfo->multi) {
	g_object_set_data(G_OBJECT(button), "format", 
			  GINT_TO_POINTER(GRETL_FORMAT_RTF));  
    } else {
	g_object_set_data(G_OBJECT(button), "format", 
			  GINT_TO_POINTER(GRETL_FORMAT_RTF_TXT));
    }

    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(button), 
				 (pref == GRETL_FORMAT_RTF || 
				  pref == GRETL_FORMAT_RTF_TXT));
    gtk_widget_show(button);

    return button;
}

static GtkWidget *
tab_copy_button (GSList *group, GtkWidget *vbox, struct format_info *finfo,
		 int pref)
{
    GtkWidget *button;

    button = gtk_radio_button_new_with_label(group, _("Tab separated"));
    gtk_box_pack_start(GTK_BOX(vbox), button, TRUE, TRUE, 0);
    g_signal_connect(G_OBJECT(button), "clicked",
		     G_CALLBACK(set_copy_format), finfo);
    g_object_set_data(G_OBJECT(button), "format", 
		      GINT_TO_POINTER(GRETL_FORMAT_TAB)); 

    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(button), 
				 (pref == GRETL_FORMAT_TAB));
    gtk_widget_show(button);

    return button;
}

static GtkWidget *
CSV_copy_button (GSList *group, GtkWidget *vbox, struct format_info *finfo,
		 int pref)
{
    GtkWidget *button;

    button = gtk_radio_button_new_with_label(group, _("Comma separated"));
    gtk_box_pack_start(GTK_BOX(vbox), button, TRUE, TRUE, 0);
    g_signal_connect(G_OBJECT(button), "clicked",
		     G_CALLBACK(set_copy_format), finfo);
    g_object_set_data(G_OBJECT(button), "format", 
		      GINT_TO_POINTER(GRETL_FORMAT_CSV));  

    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(button), 
				 (pref == GRETL_FORMAT_CSV));
    gtk_widget_show(button);

    return button;
}

static GtkWidget *
plain_text_button (GSList *group, GtkWidget *vbox, struct format_info *finfo,
		   int pref)
{
    GtkWidget *button;

    button = gtk_radio_button_new_with_label (group, _("plain text"));
    gtk_box_pack_start(GTK_BOX(vbox), button, TRUE, TRUE, 0);
    g_signal_connect(G_OBJECT(button), "clicked",
		     G_CALLBACK(set_copy_format), finfo);
    g_object_set_data(G_OBJECT(button), "format", 
		      GINT_TO_POINTER(GRETL_FORMAT_TXT));

    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(button),
				 (pref == GRETL_FORMAT_TXT));
    gtk_widget_show(button);

    return button;
}

#define can_do_tabbed(v) ((v->role == PRINT && v->data != NULL) || \
		           v->role == VIEW_SERIES)

#define can_do_csv(v) ((v->role == PRINT && v->data != NULL) || \
		        v->role == VIEW_SERIES || \
                        v->role == VIEW_MODEL)

void copy_format_dialog (windata_t *vwin, int action)
{
    GtkWidget *dialog, *tempwid, *hbox;
    GtkWidget *button;
    GtkWidget *myvbox;
    GSList *group = NULL;
    struct format_info *finfo;
    int pref;

    if (maybe_raise_dialog()) {
	return;
    }

    finfo = mymalloc(sizeof *finfo);
    if (finfo == NULL) return;

    dialog = gretl_dialog_new(_("gretl: select format"), vwin->main, 
			      GRETL_DLG_BLOCK);

    finfo->vwin = vwin;
    finfo->dialog = dialog;

    finfo->multi = MULTI_FORMAT_ENABLED(vwin->role);
    finfo->format = pref = preferred_format(0, finfo->multi);
    finfo->action = action;

    g_signal_connect(G_OBJECT(dialog), "destroy", 
		     G_CALLBACK(destroy_format_dialog), finfo);

    myvbox = gtk_vbox_new(FALSE, 5);

    hbox = gtk_hbox_new(FALSE, 5);
    tempwid = gtk_label_new((action == W_COPY)? _("Copy as:") : _("Save as"));
    gtk_box_pack_start(GTK_BOX(hbox), tempwid, TRUE, TRUE, 5);
    gtk_widget_show(tempwid);
    gtk_box_pack_start(GTK_BOX(myvbox), hbox, TRUE, TRUE, 5);
    gtk_widget_show(hbox); 

    /* Tab-separated option */
    if (can_do_tabbed(vwin)) {
	button = tab_copy_button(group, myvbox, finfo, pref);
	group = gtk_radio_button_get_group(GTK_RADIO_BUTTON(button));
    }

    /* Comma-separated option */
    if (can_do_csv(vwin)) {
	button = CSV_copy_button(group, myvbox, finfo, pref);
	group = gtk_radio_button_get_group(GTK_RADIO_BUTTON(button));
    }

    /* plain text option */
    button = plain_text_button(group, myvbox, finfo, pref);
    group = gtk_radio_button_get_group(GTK_RADIO_BUTTON(button));

# ifdef G_OS_WIN32

    if (finfo->multi || !can_do_tabbed(vwin)) {
	button = RTF_copy_button(group, myvbox, finfo, pref);
	group = gtk_radio_button_get_group(GTK_RADIO_BUTTON(button));
    }

    if (finfo->multi) {
	button = TeX_copy_button(group, myvbox, finfo, pref);
	group = gtk_radio_button_get_group(GTK_RADIO_BUTTON(button));
    }

# else /* not MS Windows: reverse RTF and LaTeX options */

    if (finfo->multi) {
	button = TeX_copy_button(group, myvbox, finfo, pref);
	group = gtk_radio_button_get_group(GTK_RADIO_BUTTON(button));
    }

    if (finfo->multi || !can_do_tabbed(vwin)) {
	button = RTF_copy_button(group, myvbox, finfo, pref);
	group = gtk_radio_button_get_group(GTK_RADIO_BUTTON(button));
    }

# endif /* G_OS_WIN32 */

    hbox = gtk_hbox_new(FALSE, 5);
    gtk_box_pack_start(GTK_BOX(hbox), myvbox, TRUE, TRUE, 5);
    gtk_widget_show(hbox);
    gtk_widget_show(myvbox);

    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->vbox), hbox, TRUE, TRUE, 5);
    gtk_widget_show(hbox);

    /* "Cancel" button */
    cancel_delete_button(GTK_DIALOG(dialog)->action_area, dialog, NULL);

    /* "OK" button */
    tempwid = ok_button(GTK_DIALOG(dialog)->action_area);
    g_signal_connect(G_OBJECT(tempwid), "clicked",
		     G_CALLBACK(copy_with_format_callback), finfo);
    gtk_widget_grab_default(tempwid);
    gtk_widget_show(tempwid);

    /* Help button if needed */
    if (can_do_tabbed(vwin)) {
	context_help_button(GTK_DIALOG(dialog)->action_area, COPY_FORMATS);
    }	

    gtk_widget_show(dialog);
}

enum {
    SET_CI = 1,
    SET_PVAL,
    UNSET_NORMAL,
    SET_NORMAL,
    SET_STUDENT
};

static void set_bs_opt (GtkWidget *w, gretlopt *opt)
{
    int i = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(w), "action"));

    switch (i) {
    case SET_CI:
	*opt &= ~OPT_T;
	*opt &= ~OPT_P;
	break;
    case SET_PVAL:
	*opt &= ~OPT_T;
	*opt |= OPT_P;
	break;
    case UNSET_NORMAL:
	*opt &= ~OPT_N;
	break;
    case SET_NORMAL:
	*opt |= OPT_N;
	break;
    case SET_STUDENT:
	*opt &= ~OPT_P;
	*opt |= OPT_T;
	break;
    }
}

static void set_bs_graph (GtkWidget *w, gretlopt *opt)
{
    if (GTK_TOGGLE_BUTTON(w)->active) {
	*opt |= OPT_G;
    } else {
	*opt &= ~OPT_G;
    }
}

static void set_bs_save (GtkWidget *w, gretlopt *opt)
{
    if (GTK_TOGGLE_BUTTON(w)->active) {
	*opt |= OPT_S;
    } else {
	*opt &= ~OPT_S;
    }
}

static void bs_select_coeff (GtkComboBox *b, int *p)
{
    *p = gtk_combo_box_get_active(b);
}

struct replic_set {
    GtkWidget *dlg;
    GtkWidget *w;
    int *B;
};

static void set_bs_replics (GtkWidget *w, struct replic_set *rs)
{
    char *s = gtk_combo_box_get_active_text(GTK_COMBO_BOX(rs->w));
    char *test = NULL;
    unsigned long u;
    
    errno = 0;
    u = strtoul(s, &test, 10);
    if (*test != '\0' || errno || (int) u <= 0) {
	warnbox(_("Invalid entry"));
    } else {
	*rs->B = (int) u;
	gtk_widget_destroy(rs->dlg);
    }

    g_free(s);
}

static void make_replics_list (GtkWidget *w)
{
    gtk_combo_box_append_text(GTK_COMBO_BOX(w), "100");
    gtk_combo_box_append_text(GTK_COMBO_BOX(w), "1000");
    gtk_combo_box_append_text(GTK_COMBO_BOX(w), "10000");
    gtk_combo_box_append_text(GTK_COMBO_BOX(w), "100000");

    gtk_combo_box_set_active(GTK_COMBO_BOX(w), 1);
}

static GtkWidget *bs_coeff_popdown (MODEL *pmod, int *pp)
{
    GtkWidget *w;
    int *xlist = NULL;
    int i, vi;

    xlist = gretl_model_get_x_list(pmod);
    if (xlist == NULL) {
	return NULL;
    }

    w = gtk_combo_box_new_text();

    for (i=1; i<=xlist[0]; i++) {
	vi = xlist[i];
	gtk_combo_box_append_text(GTK_COMBO_BOX(w), datainfo->varname[vi]);
    }

    if (pmod->ifc && pmod->ncoeff > 1) {
	*pp = 1;
	gtk_combo_box_set_active(GTK_COMBO_BOX(w), 1);
    } else {
	*pp = 0;
	gtk_combo_box_set_active(GTK_COMBO_BOX(w), 0);
    }    

    free(xlist);  

    return w;
}

void bootstrap_dialog (windata_t *vwin, int *pp, int *pB,
		       gretlopt *popt, int *canceled)
{
    MODEL *pmod = vwin->data;
    GtkWidget *dialog, *hbox, *vbox;
    GtkWidget *popdown = NULL;
    GtkWidget *button;
    GtkWidget *tmp;
    GSList *group = NULL;
    gchar *tmpstr;
    struct replic_set rs;
    int htest = (pp == NULL);

    if (maybe_raise_dialog()) {
	*canceled = 1;
	return;
    }

    if (pp != NULL) {
	popdown = bs_coeff_popdown(pmod, pp);
	if (popdown == NULL) {
	    gui_errmsg(E_DATA);
	    return;
	}
    }	

    dialog = gretl_dialog_new(_("gretl: bootstrap analysis"), vwin->main, 
			      GRETL_DLG_BLOCK);
    rs.dlg = dialog;

    vbox = gtk_vbox_new(FALSE, 5);
    hbox = gtk_hbox_new(FALSE, 5);
    tmpstr = g_strdup_printf("%s:", _("Coefficient"));
    tmp = gtk_label_new(tmpstr);
    g_free(tmpstr);
    gtk_box_pack_start(GTK_BOX(hbox), tmp, TRUE, TRUE, 5);
    gtk_widget_show(tmp);

    if (htest) {
	/* not selecting coeff, or conf int vs p-value */
	goto htest_only;
    }

    /* coefficient / variable selection */

    g_signal_connect(G_OBJECT(popdown), "changed",
		     G_CALLBACK(bs_select_coeff), pp);
    gtk_box_pack_start(GTK_BOX(hbox), popdown, TRUE, TRUE, 5);
    gtk_widget_show(popdown);
    
    gtk_box_pack_start(GTK_BOX(vbox), hbox, TRUE, TRUE, 5);
    gtk_widget_show(hbox); 

    vbox_add_hsep(vbox);

    /* confidence interval vs p-value */

    button = gtk_radio_button_new_with_label(NULL, _("Confidence interval"));
    gtk_box_pack_start(GTK_BOX(vbox), button, TRUE, TRUE, 0);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON (button), TRUE);
    g_object_set_data(G_OBJECT(button), "action", GINT_TO_POINTER(SET_CI));
    g_signal_connect(G_OBJECT(button), "clicked",
		     G_CALLBACK(set_bs_opt), popt);
    gtk_widget_show(button);

    group = gtk_radio_button_get_group(GTK_RADIO_BUTTON(button));
    button = gtk_radio_button_new_with_label(group, _("Studentized confidence interval"));
    gtk_box_pack_start(GTK_BOX(vbox), button, TRUE, TRUE, 0);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON (button), FALSE);
    g_object_set_data(G_OBJECT(button), "action", GINT_TO_POINTER(SET_STUDENT));
    g_signal_connect(G_OBJECT(button), "clicked",
		     G_CALLBACK(set_bs_opt), popt);
    gtk_widget_show(button);

    group = gtk_radio_button_get_group(GTK_RADIO_BUTTON(button));
    button = gtk_radio_button_new_with_label(group, _("P-value"));
    gtk_box_pack_start(GTK_BOX(vbox), button, TRUE, TRUE, 0);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON (button), FALSE);
    g_object_set_data(G_OBJECT(button), "action", GINT_TO_POINTER(SET_PVAL));
    g_signal_connect(G_OBJECT(button), "clicked",
		     G_CALLBACK(set_bs_opt), popt);
    gtk_widget_show(button);

    vbox_add_hsep(vbox);

 htest_only:

    /* resample vs simulated normal */

    button = gtk_radio_button_new_with_label(NULL, _("Resample residuals"));
    gtk_box_pack_start(GTK_BOX(vbox), button, TRUE, TRUE, 0);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON (button), TRUE);
    g_object_set_data(G_OBJECT(button), "action", GINT_TO_POINTER(UNSET_NORMAL));
    g_signal_connect(G_OBJECT(button), "clicked",
		     G_CALLBACK(set_bs_opt), popt);
    gtk_widget_show(button);

    group = gtk_radio_button_get_group(GTK_RADIO_BUTTON(button));
    button = gtk_radio_button_new_with_label(group, _("Simulate normal errors"));
    gtk_box_pack_start(GTK_BOX(vbox), button, TRUE, TRUE, 0);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON (button), FALSE);
    g_object_set_data(G_OBJECT(button), "action", GINT_TO_POINTER(SET_NORMAL));
    g_signal_connect(G_OBJECT(button), "clicked",
		     G_CALLBACK(set_bs_opt), popt);
    gtk_widget_show(button);

    vbox_add_hsep(vbox);

    /* Number of replications */

    hbox = gtk_hbox_new(FALSE, 5);
    tmp = gtk_label_new(_("Number of replications:"));
    gtk_box_pack_start(GTK_BOX(hbox), tmp, FALSE, FALSE, 5);
    gtk_widget_show(tmp);

    rs.B = pB;
    rs.w = gtk_combo_box_entry_new_text();
    make_replics_list(rs.w);
    tmp = gtk_bin_get_child(GTK_BIN(rs.w));
    gtk_entry_set_width_chars(GTK_ENTRY(tmp), 7);
    gtk_box_pack_start(GTK_BOX(hbox), rs.w, FALSE, FALSE, 5);
    gtk_widget_show(rs.w);

    gtk_box_pack_start(GTK_BOX(vbox), hbox, TRUE, TRUE, 5);
    gtk_widget_show(hbox); 

    if (!htest) {
	/* graph check box */
	button = gtk_check_button_new_with_label(_("Show graph of sampling "
						   "distribution"));
	g_signal_connect(G_OBJECT(button), "toggled",
			 G_CALLBACK(set_bs_graph), popt);
	gtk_box_pack_start(GTK_BOX(vbox), button, TRUE, TRUE, 5);
	gtk_widget_show(button);
    }

    /* save output switch */
    button = gtk_check_button_new_with_label(_("Save bootstrap data to file"));
    g_signal_connect(G_OBJECT(button), "toggled",
		     G_CALLBACK(set_bs_save), popt);
    gtk_box_pack_start(GTK_BOX(vbox), button, TRUE, TRUE, 5);
    gtk_widget_show(button);    

    /* pack all of the above */

    hbox = gtk_hbox_new(FALSE, 5);
    gtk_box_pack_start(GTK_BOX(hbox), vbox, TRUE, TRUE, 5);
    gtk_widget_show(hbox);
    gtk_widget_show(vbox);

    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->vbox), hbox, TRUE, TRUE, 5);
    gtk_widget_show(hbox);

    /* "Cancel" button */
    cancel_delete_button(GTK_DIALOG(dialog)->action_area, dialog, 
			 canceled);

    /* "OK" button */
    button = ok_button(GTK_DIALOG(dialog)->action_area);
    g_signal_connect(G_OBJECT(button), "clicked",
		     G_CALLBACK(set_bs_replics), &rs);
    gtk_widget_grab_default(button);
    gtk_widget_show(button);

    if (!htest) {
	/* Help button */
	context_help_button(GTK_DIALOG(dialog)->action_area, BOOTSTRAP);
    }

    gtk_widget_show(dialog);
}

/* dialog for setting various properties of individual variables */

struct varinfo_settings {
    GtkWidget *dlg;
    GtkWidget *name_entry;
    GtkWidget *label_entry;
    GtkWidget *display_name_entry;
    GtkWidget *value_entry;
    GtkWidget *compaction_menu;
    GtkWidget *spin;
    GtkWidget *check;
    int varnum;
    int formula;
    int full;
};

static int got_var_row (int v, gchar *s)
{
    int ret = (v == atoi(s));

    g_free(s);
    return ret;
}

static void show_varinfo_changes (int v) 
{
    GtkTreeModel *model;
    GtkTreeIter top, kid, *iptr = NULL;
    gchar *idstr = NULL;

    model = gtk_tree_view_get_model(GTK_TREE_VIEW(mdata->listbox));

    gtk_tree_model_get_iter_first(model, &top);

    while (iptr == NULL && gtk_tree_model_iter_next(model, &top)) {
	gtk_tree_model_get(model, &top, 0, &idstr, -1);
	if (got_var_row(v, idstr)) {
	    iptr = &top;
	} else if (gtk_tree_model_iter_children(model, &kid, &top)) {
	    do {
		gtk_tree_model_get(model, &kid, 0, &idstr, -1);
		if (got_var_row(v, idstr)) {
		    iptr = &kid;
		    break;
		}
	    } while (gtk_tree_model_iter_next(model, &kid));
	}
    }

    if (iptr != NULL) {
        gtk_tree_store_set(GTK_TREE_STORE(model), iptr, 
                           1, datainfo->varname[v],
                           2, VARLABEL(datainfo, v),
                           -1);
    }
}

static char *trim_text (const char *s)
{
    char *ret = NULL;
    int i;

    while (isspace(*s)) s++;
    if (*s == '\0') return NULL;

    ret = g_strdup(s);
    for (i=strlen(ret)-1; i>0; i--) {
	if (!isspace(ret[i])) break;
	ret[i] = '\0';
    }

    return ret;
}

/* recreate a generated var in response to a change in
   the formula given under "Edit attributes" */

static int try_regenerate_var (int v, const char *s)
{
    char line[MAXLEN];
    PRN *prn;
    int err;

    /* FIXME use gretl_command_sprintf, etc? */

    if (bufopen(&prn)) {
	return 0;
    }

    if (*s == '=') s++;
    sprintf(line, "%s=%s", datainfo->varname[v], s);
    err = generate(line, &Z, datainfo, OPT_NONE, prn);

    if (err) {
	errbox(gretl_print_get_buffer(prn));
    }

    gretl_print_destroy(prn);

    return err;
}

static void 
really_set_variable_info (GtkWidget *w, struct varinfo_settings *vset)
{
    const char *edttext;
    char *newstr = NULL;
    int v = vset->varnum;
    int changed = 0, gui_changed = 0;
    int comp_changed = 0, disc_changed = 0;

    edttext = gtk_entry_get_text(GTK_ENTRY(vset->name_entry));
    newstr = trim_text(edttext);

    if (newstr != NULL && strcmp(datainfo->varname[v], newstr)) {
	if (do_rename_variable(v, newstr, vset->full)) {
	    free(newstr);
	    return;
	} else {
	    gui_changed = 1;
	}
    }

    free(newstr);

    edttext = gtk_entry_get_text(GTK_ENTRY(vset->label_entry));
    newstr = trim_text(edttext);

    if (newstr != NULL) {
	if (vset->formula) {
	    int err = try_regenerate_var(v, newstr);

	    if (err) {
		free(newstr);
		return;
	    } else {
		changed = 1;
	    }
	}
	if (strcmp(VARLABEL(datainfo, v), newstr)) {
	    *VARLABEL(datainfo, v) = 0;
	    strncat(VARLABEL(datainfo, v), newstr, MAXLABEL - 1);
	    changed = 1;
	    gui_changed = 1;
	}
    }

    free(newstr);

    if (vset->display_name_entry != NULL) {
	edttext = gtk_entry_get_text(GTK_ENTRY(vset->display_name_entry));
	newstr = trim_text(edttext);
	if (newstr != NULL && strcmp(DISPLAYNAME(datainfo, v), newstr)) {
	    if (strchr(newstr, '"')) {
		warnbox(_("The display name for a variable cannot "
			  "contain double quotes"));
		free(newstr);
		return;
	    } else {
		var_set_display_name(datainfo, v, newstr);
		changed = 1;
	    }
	}
	free(newstr);
    }

    if (vset->value_entry != NULL) {
	double val;
	int err = 0;

	edttext = gtk_entry_get_text(GTK_ENTRY(vset->value_entry));
	val = gui_double_from_string(edttext, &err);
	if (err) {
	    return;
	} else if (val != Z[v][0]) {
	    Z[v][0] = val;
	    changed = 1;
	}
    }

    if (vset->compaction_menu != NULL) {
	int comp_method = 
	    gtk_combo_box_get_active(GTK_COMBO_BOX(vset->compaction_menu));

	if (comp_method != COMPACT_METHOD(datainfo, v)) {
	    COMPACT_METHOD(datainfo, v) = comp_method;
	    comp_changed = 1;
	}
    }

    if (vset->spin != NULL) {
	int oldlw = var_get_linewidth(datainfo, v);
	int lw = (int) gtk_spin_button_get_value(GTK_SPIN_BUTTON(vset->spin));

	if (lw != oldlw) {
	    var_set_linewidth(datainfo, v, lw);
	}
    }

    if (vset->check != NULL) {
	int discrete = 
	    gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(vset->check));
	int old = (var_is_discrete(datainfo, v))? 1 : 0;

	if (discrete != old) {
	    set_var_discrete(datainfo, v, discrete);
	    disc_changed = 1;
	}
    }   

    if (vset->full) {
	if (changed) {
	    record_varlabel_change(v);
	}
	if (gui_changed) {
	    show_varinfo_changes(v);
	}
	if (changed || comp_changed || gui_changed || disc_changed) {
	    mark_dataset_as_modified();
	}
    }

    gtk_widget_destroy(vset->dlg);
}

static void varinfo_cancel (GtkWidget *w, struct varinfo_settings *vset)
{
    if (!vset->full) {
	*datainfo->varname[vset->varnum] = '\0';
    }

    gtk_widget_destroy(vset->dlg);
}

static void free_vsettings (GtkWidget *w, struct varinfo_settings *vset)
{
    free(vset);
}

static const char *comp_int_to_string (int i)
{
    if (i == COMPACT_NONE) return N_("not set");
    if (i == COMPACT_AVG)  return N_("average of observations");
    if (i == COMPACT_SUM)  return N_("sum of observations");
    if (i == COMPACT_SOP)  return N_("first observation");
    if (i == COMPACT_EOP)  return N_("last observation");
    return N_("not set");
}

static int formula_ok (int v)
{
    if (var_is_generated(datainfo, v)) {
	const char *s = VARLABEL(datainfo, v);
	int n = strlen(s);

	/* note: the test for a trailing dot here is to check
	   that we don't have a long genr formula that has been
	   truncated into the variable's description */

	if (n > 0 && s[n-1] != '.') {
	    return 1;
	}
    }

    return 0;
}

static void 
vset_toggle_formula (GtkComboBox *box, struct varinfo_settings *vset)
{
    gchar *s = gtk_combo_box_get_active_text(box);

    vset->formula = !strcmp(s, _("Formula:"));
    g_free(s);
}	    

void varinfo_dialog (int varnum, int full)
{
    GtkWidget *tmp, *hbox;
    struct varinfo_settings *vset;
    unsigned char flags;

    if (maybe_raise_dialog()) {
	return;
    }

    vset = mymalloc(sizeof *vset);
    if (vset == NULL) return;

    if (full) {
	flags = GRETL_DLG_BLOCK | GRETL_DLG_RESIZE;
    } else {
	flags = GRETL_DLG_MODAL | GRETL_DLG_BLOCK | GRETL_DLG_RESIZE;
    }

    vset->varnum = varnum;
    vset->dlg = gretl_dialog_new(_("gretl: variable attributes"), NULL, flags);

    vset->display_name_entry = NULL;
    vset->value_entry = NULL;
    vset->compaction_menu = NULL;
    vset->spin = NULL;
    vset->check = NULL;
    vset->formula = formula_ok(varnum);
    vset->full = full;

    g_signal_connect(G_OBJECT(vset->dlg), "destroy", 
		     G_CALLBACK(free_vsettings), vset);

    /* read/set name of variable */
    hbox = gtk_hbox_new(FALSE, 5);
    tmp = gtk_label_new (_("Name of variable:"));
    gtk_box_pack_start(GTK_BOX(hbox), tmp, FALSE, FALSE, 5);
    gtk_widget_show(tmp);

    vset->name_entry = gtk_entry_new();
    gtk_entry_set_max_length(GTK_ENTRY(vset->name_entry), VNAMELEN-1);
    gtk_entry_set_width_chars(GTK_ENTRY(vset->name_entry), VNAMELEN+3);
    gtk_entry_set_text(GTK_ENTRY(vset->name_entry), 
		       datainfo->varname[varnum]);
    gtk_box_pack_start(GTK_BOX(hbox), 
		       vset->name_entry, FALSE, FALSE, 0);
    gtk_widget_show(vset->name_entry); 
    gtk_entry_set_activates_default(GTK_ENTRY(vset->name_entry), TRUE);

    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(vset->dlg)->vbox), 
		       hbox, FALSE, FALSE, 5);
    gtk_widget_show(hbox); 
    
    /* read/set descriptive string or genr formula */
    hbox = gtk_hbox_new(FALSE, 5);
    if (vset->formula) {
	tmp = gtk_combo_box_new_text();
	gtk_combo_box_append_text(GTK_COMBO_BOX(tmp), _("Description:"));
	gtk_combo_box_append_text(GTK_COMBO_BOX(tmp), _("Formula:"));
	gtk_combo_box_set_active(GTK_COMBO_BOX(tmp), 0);
	vset->formula = 0;
	g_signal_connect(G_OBJECT(tmp), "changed", 
			 G_CALLBACK(vset_toggle_formula), vset);
    } else {
	tmp = gtk_label_new(_("Description:"));
    }
    gtk_box_pack_start(GTK_BOX(hbox), tmp, FALSE, FALSE, 5);
    gtk_widget_show(tmp);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(vset->dlg)->vbox), 
		       hbox, FALSE, FALSE, 0);
    gtk_widget_show(hbox);

    hbox = gtk_hbox_new(FALSE, 5);
    vset->label_entry = gtk_entry_new();
    gtk_entry_set_max_length(GTK_ENTRY(vset->label_entry), MAXLABEL-1);
    gtk_entry_set_text(GTK_ENTRY(vset->label_entry), 
		       VARLABEL(datainfo, varnum));
    gtk_box_pack_start(GTK_BOX(hbox), vset->label_entry, TRUE, TRUE, 5);
    gtk_widget_show(vset->label_entry);
    gtk_entry_set_activates_default(GTK_ENTRY(vset->label_entry), TRUE);

    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(vset->dlg)->vbox), 
		       hbox, FALSE, FALSE, 5);
    gtk_widget_show(hbox);  

    /* Of editing actions, editing the descriptive string is the most
       likely?  On this assumption we'll focus that widget */
    gtk_widget_grab_focus(vset->label_entry);

    /* read/set display name? */
    if (full) {
	hbox = gtk_hbox_new(FALSE, 5);
	tmp = gtk_label_new (_("Display name (shown in graphs):"));
	gtk_box_pack_start(GTK_BOX(hbox), tmp, FALSE, FALSE, 5);
	gtk_widget_show(tmp);

	vset->display_name_entry = gtk_entry_new();
	gtk_entry_set_max_length(GTK_ENTRY(vset->display_name_entry), 
				 MAXDISP-1);
	gtk_entry_set_width_chars(GTK_ENTRY(vset->display_name_entry), 
				  MAXDISP+4);
	gtk_entry_set_text(GTK_ENTRY(vset->display_name_entry), 
			   DISPLAYNAME(datainfo, varnum));
	gtk_box_pack_start(GTK_BOX(hbox), 
			   vset->display_name_entry, FALSE, FALSE, 5);
	gtk_widget_show(vset->display_name_entry); 
	gtk_entry_set_activates_default(GTK_ENTRY(vset->display_name_entry), TRUE);

	gtk_box_pack_start(GTK_BOX(GTK_DIALOG(vset->dlg)->vbox), 
			   hbox, FALSE, FALSE, 5);
	gtk_widget_show(hbox); 
    }

    /* read/set compaction method? */
    if (full && dataset_is_time_series(datainfo)) {  
	int i;

	hbox = gtk_hbox_new(FALSE, 5);
	tmp = gtk_label_new (_("Compaction method (for reducing frequency):"));
	gtk_box_pack_start(GTK_BOX(hbox), tmp, FALSE, FALSE, 5);
	gtk_widget_show(tmp);

	vset->compaction_menu = gtk_combo_box_new_text();

	for (i=COMPACT_NONE; i<COMPACT_MAX; i++) {
	    gtk_combo_box_append_text(GTK_COMBO_BOX(vset->compaction_menu), 
				      _(comp_int_to_string(i)));
	}
	
	gtk_combo_box_set_active(GTK_COMBO_BOX(vset->compaction_menu), 
				 COMPACT_METHOD(datainfo, varnum));
	gtk_box_pack_start(GTK_BOX(hbox), vset->compaction_menu, FALSE, FALSE, 5);
	gtk_widget_show(vset->compaction_menu); 

	gtk_box_pack_start(GTK_BOX(GTK_DIALOG(vset->dlg)->vbox), 
			   hbox, FALSE, FALSE, 5);
	gtk_widget_show(hbox); 
    }

    /* graph line width */
    if (full && dataset_is_time_series(datainfo)) {  
	int w;

	hbox = gtk_hbox_new(FALSE, 5);
	tmp = gtk_label_new (_("Graph line width:"));
	gtk_box_pack_start(GTK_BOX(hbox), tmp, FALSE, FALSE, 5);
	gtk_widget_show(tmp);

	tmp = gtk_spin_button_new_with_range(1, 8, 1);
	w = var_get_linewidth(datainfo, varnum);
	if (w > 1) {
	    gtk_spin_button_set_value(GTK_SPIN_BUTTON(tmp), w);
	}
	gtk_box_pack_start(GTK_BOX(hbox), tmp, FALSE, FALSE, 5);
	gtk_widget_show(tmp);
	vset->spin = tmp;

	gtk_box_pack_start(GTK_BOX(GTK_DIALOG(vset->dlg)->vbox), 
			   hbox, FALSE, FALSE, 5);
	gtk_widget_show(hbox); 
    }    

    /* mark variable as discrete or not? */
    if (full && (var_is_discrete(datainfo, varnum) ||
		 gretl_isint(0, datainfo->n - 1, Z[varnum]))) {
	hbox = gtk_hbox_new(FALSE, 5);
	vset->check = gtk_check_button_new_with_label(_("Treat this variable "
							"as discrete"));
	if (var_is_discrete(datainfo, varnum)) {
	    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(vset->check), TRUE);
	}
	gtk_widget_show(vset->check);
	gtk_box_pack_start(GTK_BOX(hbox), vset->check, FALSE, FALSE, 5);
	gtk_box_pack_start(GTK_BOX(GTK_DIALOG(vset->dlg)->vbox), 
			   hbox, FALSE, FALSE, 5);
	gtk_widget_show(hbox); 
    }

    hbox = GTK_DIALOG(vset->dlg)->action_area;

    /* Cancel button */
    tmp = cancel_button(hbox);
    g_signal_connect(G_OBJECT (tmp), "clicked", 
		     G_CALLBACK(varinfo_cancel), vset);
    gtk_widget_show(tmp);

    /* "OK" button */
    tmp = ok_button(hbox);
    g_signal_connect(G_OBJECT(tmp), "clicked",
		     G_CALLBACK(really_set_variable_info), vset);
    gtk_widget_grab_default(tmp);
    gtk_widget_show(tmp);

    /* And a Help button? */
    if (full) {
	context_help_button(hbox, SETINFO);
    }

    gtk_widget_show(vset->dlg);
}

static void db_descrip_callback (GtkWidget *w, GtkWidget *dlg)
{
    GtkWidget *entry;
    gchar *fname;

    entry = g_object_get_data(G_OBJECT(dlg), "entry");
    fname = g_object_get_data(G_OBJECT(dlg), "fname");

    if (entry != NULL && fname != NULL) {
	const gchar *newdesc = gtk_entry_get_text(GTK_ENTRY(entry));

	write_db_description(fname, newdesc);
    }
    
    gtk_widget_destroy(dlg);
}

static void free_db_fname (GtkWidget *w, char *fname)
{
    g_free(fname);
}

void database_description_dialog (const char *binname)
{
    GtkWidget *dlg, *entry;
    GtkWidget *tempwid, *hbox;
    gchar *fname, *descrip;

    if (maybe_raise_dialog()) {
	return;
    }

    descrip = get_db_description(binname);
    if (descrip == NULL) {
	return;
    }

    fname = g_strdup(binname);

    dlg = gretl_dialog_new(_("gretl: database description"), NULL,
			   GRETL_DLG_BLOCK | GRETL_DLG_RESIZE);

    hbox = gtk_hbox_new(FALSE, 5);
    tempwid = gtk_label_new (_("description:"));
    gtk_box_pack_start(GTK_BOX(hbox), tempwid, FALSE, FALSE, 0);
    gtk_widget_show(tempwid);

    g_signal_connect(G_OBJECT(dlg), "destroy", 
		     G_CALLBACK(free_db_fname), fname);

    entry = gtk_entry_new();
    gtk_entry_set_max_length(GTK_ENTRY(entry), 64);
    gtk_entry_set_width_chars(GTK_ENTRY(entry), 32);

    gtk_entry_set_text(GTK_ENTRY(entry), descrip);
    gtk_box_pack_start(GTK_BOX(hbox), entry, FALSE, FALSE, 0);
    gtk_widget_show(entry); 
    gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);

    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dlg)->vbox), 
		       hbox, FALSE, FALSE, 0);
    gtk_widget_show(hbox); 

    /* set data on dialog */
    g_object_set_data(G_OBJECT(dlg), "entry", entry);
    g_object_set_data(G_OBJECT(dlg), "fname", fname);
    
    /* Create the "OK" button */
    tempwid = ok_button(GTK_DIALOG(dlg)->action_area);
    g_signal_connect(G_OBJECT(tempwid), "clicked",
		     G_CALLBACK(db_descrip_callback), dlg);
    gtk_widget_grab_default(tempwid);
    gtk_widget_show(tempwid);

    gretl_set_window_modal(dlg);
    gtk_widget_show(dlg);
}

static void record_seed (GtkWidget *w, guint32 *s)
{
    *s = (guint32) GTK_ADJUSTMENT(w)->value;
}

static void set_rand_seed (GtkWidget *w, guint32 *s)
{
    guint32 newseed = *s;
	
    gretl_command_sprintf("set seed %u", newseed); 
    if (check_and_record_command()) {
	return;
    }

    gretl_rand_set_seed(newseed);
}

void rand_seed_dialog (void)
{
    guint32 dseed;
    GtkWidget *dlg;
    GtkWidget *tmp, *hbox;
    GtkObject *adj;

    if (maybe_raise_dialog()) {
	return;
    }

    dlg = gretl_dialog_new(_("gretl: seed for random numbers"), NULL,
			   GRETL_DLG_BLOCK | GRETL_DLG_RESIZE);

    hbox = gtk_hbox_new(FALSE, 5);

    tmp = gtk_label_new(_("Seed for generator:"));
    gtk_box_pack_start(GTK_BOX(hbox), tmp, FALSE, FALSE, 5);

    dseed = gretl_rand_get_seed();
    adj = gtk_adjustment_new((gdouble) dseed, 1, (gdouble) UINT_MAX, 
			     1, 1000, 0);
    g_signal_connect(G_OBJECT(adj), "value-changed",
		     G_CALLBACK(record_seed), &dseed);
    
    tmp = gtk_spin_button_new(GTK_ADJUSTMENT(adj), 1, 0);
    gtk_box_pack_start(GTK_BOX(hbox), tmp, TRUE, TRUE, 5);
    gtk_widget_show_all(hbox);

    /* FIXME activates default */

    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dlg)->vbox), hbox, TRUE, TRUE, 5);

    /* Cancel button */
    cancel_delete_button(GTK_DIALOG(dlg)->action_area, dlg, NULL);
    
    /* OK button */
    tmp = ok_button(GTK_DIALOG(dlg)->action_area);
    g_signal_connect(G_OBJECT(tmp), "clicked",
		     G_CALLBACK(set_rand_seed), &dseed);
    g_signal_connect(G_OBJECT(tmp), "clicked",
		     G_CALLBACK(delete_widget), dlg);
    gtk_widget_grab_default(tmp);
    gtk_widget_show(tmp);

    /* Help button */
    context_help_button(GTK_DIALOG(dlg)->action_area, SEED_RANDOM);

    gtk_widget_show(dlg);
}

/* apparatus for setting sample range */

struct range_setting {
    gretlopt opt;
    GtkWidget *dlg;
    GtkWidget *obslabel;
    GtkObject *adj1;
    GtkWidget *startspin;
    GtkObject *adj2;
    GtkWidget *endspin;
    GtkWidget *combo;
    gpointer p;
    MODEL *pmod;
    int *t1;
    int *t2;
};

static void free_rsetting (GtkWidget *w, struct range_setting *rset)
{
    free(rset);
}

static int unit_get_first_obs (int u)
{
    int t;

    for (t=0; t<datainfo->n; t++) {
	if (datainfo->paninfo->unit[t] == u) {
	    return t;
	}
    }

    return 0;
}

static int unit_get_last_obs (int u)
{
    int t;

    for (t=1; t<datainfo->n; t++) {
	if (datainfo->paninfo->unit[t] == u + 1) {
	    return t - 1;
	}
    }

    return 0;
}

static gboolean
set_sample_from_dialog (GtkWidget *w, struct range_setting *rset)
{
    int err;

    if (rset->opt & OPT_O) {
	/* sampling using a dummy var */
	gchar *dumv;

	dumv = gtk_combo_box_get_active_text(GTK_COMBO_BOX(rset->combo));
	gretl_command_sprintf("smpl %s --dummy", dumv);
	g_free(dumv);

	if (check_and_record_command()) {
	    return TRUE;
	}	

	err = bool_subsample(rset->opt);
	if (!err) {
	    gtk_widget_destroy(rset->dlg);
	} 
    } else if (rset->opt & OPT_N) {
	/* random subsample */
	int subn;

	subn = gtk_spin_button_get_value(GTK_SPIN_BUTTON(rset->startspin));
	gretl_command_sprintf("smpl %d --random", subn);
	if (check_and_record_command()) {
	    return TRUE;
	}

	err = bool_subsample(rset->opt);
	if (!err) {
	    gtk_widget_destroy(rset->dlg);
	} 
    } else {
	ObsButton *button;
	char s1[OBSLEN], s2[OBSLEN];
	int t1, t2;	

	button = OBS_BUTTON(rset->startspin);
	strcpy(s1, gtk_entry_get_text(GTK_ENTRY(button)));
	t1 = (int) obs_button_get_value(button);

	button = OBS_BUTTON(rset->endspin);
	strcpy(s2, gtk_entry_get_text(GTK_ENTRY(button)));
	t2 = (int) obs_button_get_value(button); 

	if (rset->opt & OPT_C) {
	    /* creating a new dataset */
	    gchar **obsstr = (gchar **) rset->p;

	    if (obsstr != NULL) {
		*obsstr = g_strdup_printf("%s %s", s1, s2);
	    }
	    gtk_widget_destroy(rset->dlg);
	    return TRUE;
	} else if (rset->opt & OPT_P) {
	    /* selecting panel group range */
	    t1 = unit_get_first_obs(t1);
	    t2 = unit_get_last_obs(t2);
	    ntodate(s1, t1, datainfo);
	    ntodate(s2, t2, datainfo);
	}

	if (t1 != datainfo->t1 || t2 != datainfo->t2) {
	    gretl_command_sprintf("smpl %s %s", s1, s2);
	    if (check_and_record_command()) {
		return TRUE;
	    }
	    err = do_set_sample();
	    if (err) {
		gui_errmsg(err);
	    } else {
		gtk_widget_destroy(rset->dlg);
		set_sample_label(datainfo);
	    }
	} else {
	    /* no change */
	    gtk_widget_destroy(rset->dlg);
	}
    }

    return TRUE;
}

static gboolean
set_obs_from_dialog (GtkWidget *w, struct range_setting *rset)
{
    ObsButton *button;
    const gchar *s;

    if (rset->startspin != NULL && rset->t1 != NULL) {
	button = OBS_BUTTON(rset->startspin);
	s = gtk_entry_get_text(GTK_ENTRY(button));
	*rset->t1 = (int) obs_button_get_value(button);
    }

    if (rset->endspin != NULL && rset->t2 != NULL) {
	button = OBS_BUTTON(rset->endspin);
	s = gtk_entry_get_text(GTK_ENTRY(button));
	*rset->t2 = (int) obs_button_get_value(button); 
    }

    gtk_widget_destroy(rset->dlg);

    return TRUE;
}

static GList *get_dummy_list (int *thisdum)
{
    GList *dumlist = NULL;
    int v = mdata_active_var();
    int i, j = 0;

    for (i=1; i<datainfo->v; i++) {
	if (gretl_isdummy(datainfo->t1, datainfo->t2, Z[i])) {
	    dumlist = g_list_append(dumlist, datainfo->varname[i]);
	    if (i == v) {
		*thisdum = j;
	    }
	    j++;
	}
    }

    return dumlist;
}

gboolean update_obs_label (GtkComboBox *box, gpointer data)
{
    struct range_setting *rset = (struct range_setting *) data;
    int n = 0;

    if (box != NULL) {
	gchar *vname = gtk_combo_box_get_active_text(box);

	if (vname != NULL) {
	    int v = series_index(datainfo, vname);

	    if (v < datainfo->v) {
		n = gretl_isdummy(0, datainfo->n - 1, Z[v]);
	    }
	    g_free(vname);
	}
    } else {
	int t1 = (int) obs_button_get_value(OBS_BUTTON(rset->startspin));
	int t2 = (int) obs_button_get_value(OBS_BUTTON(rset->endspin));

	n = t2 - t1 + 1;  
    }
    
    if (n > 0) {
	gchar *obstr = NULL;

	if (rset->opt == OPT_P) {
	    obstr = g_strdup_printf(_("Included groups: %d"), n);
	} else {
	    obstr = g_strdup_printf(_("Observations: %d"), n);  
	}
	gtk_label_set_text(GTK_LABEL(rset->obslabel), obstr); 
	g_free(obstr);
    }

    return FALSE;
}

static int default_randsize (void)
{
    int n = datainfo->t2 - datainfo->t1 + 1;

    if (n > 1000) {
	return n / 10;
    } else {
	return n / 2;
    }
}

static struct range_setting *rset_new (guint code, gpointer p,
				       MODEL *pmod, 
				       int *t1, int *t2,
				       const gchar *title)
{
    struct range_setting *rset;

    rset = mymalloc(sizeof *rset);
    if (rset == NULL) return NULL;

    if (code == SMPLDUM) {
	rset->opt = OPT_O;
    } else if (code == SMPLRAND) {
	rset->opt = OPT_N;
    } else if (code == CREATE_DATASET) {
	rset->opt = OPT_C;
    } else {
	rset->opt = OPT_NONE;
    }
    
    rset->dlg = gretl_dialog_new(title, NULL, GRETL_DLG_BLOCK);
    rset->combo = NULL;
    rset->adj1 = rset->adj2 = NULL;
    rset->startspin = rset->endspin = NULL;
    rset->obslabel = NULL;

    rset->p = p;
    rset->pmod = pmod;

    rset->t1 = t1;
    rset->t2 = t2;

    return rset;
}

/* Special sample range selector for panel datasets: express the
   choice as a matter of which units/groups to include 
*/

static GtkWidget *panel_sample_spinbox (struct range_setting *rset)
{
    DATAINFO dinfo = {0};
    GtkWidget *lbl;
    GtkWidget *vbox;
    GtkWidget *hbox;

    dinfo.n = datainfo->n / datainfo->pd;
    dinfo.t1 = datainfo->paninfo->unit[datainfo->t1];
    dinfo.t2 = datainfo->paninfo->unit[datainfo->t2];
    dataset_obs_info_default(&dinfo);

    lbl = gtk_label_new(_("Panel groups"));
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(rset->dlg)->vbox), 
		       lbl, FALSE, FALSE, 5);
    hbox = gtk_hbox_new(TRUE, 5);

    /* spinner for u1 */
    vbox = gtk_vbox_new(FALSE, 5);
    lbl = gtk_label_new(_("Start:"));
    gtk_box_pack_start(GTK_BOX(vbox), lbl, FALSE, FALSE, 0);
    rset->adj1 = gtk_adjustment_new(dinfo.t1, 0, dinfo.n - 1, 1, 1, 0);
    rset->startspin = obs_button_new(GTK_ADJUSTMENT(rset->adj1), &dinfo);
    gtk_box_pack_start(GTK_BOX(vbox), rset->startspin, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hbox), vbox, FALSE, FALSE, 5);

    /* spinner for u2 */
    vbox = gtk_vbox_new(FALSE, 5);
    lbl = gtk_label_new(_("End:"));
    gtk_box_pack_start(GTK_BOX(vbox), lbl, FALSE, FALSE, 0);
    rset->adj2 = gtk_adjustment_new(dinfo.t2, 0, dinfo.n - 1, 1, 1, 0);
    rset->endspin = obs_button_new(GTK_ADJUSTMENT(rset->adj2), &dinfo);
    gtk_box_pack_start(GTK_BOX(vbox), rset->endspin, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hbox), vbox, FALSE, FALSE, 5);

    /* inter-connect the two spinners */
    g_object_set_data(G_OBJECT(rset->startspin), "endspin", rset->endspin);
    g_object_set_data(G_OBJECT(rset->endspin), "startspin", rset->startspin);

    return hbox;
}

typedef enum {
    SPIN_LABEL_NONE,
    SPIN_LABEL_ABOVE,
    SPIN_LABEL_INLINE
} SpinLabelAlign;

static GtkWidget *
obs_spinbox (struct range_setting *rset, const char *label, 
	     const char *t1str, const char *t2str,
	     int t1min, int t1max, int *t1,
	     int t2min, int t2max, int *t2,
	     SpinLabelAlign align)
{
    GtkWidget *lbl;
    GtkWidget *vbox;
    GtkWidget *hbox;
    int smin; /* "step increment" */
    int smaj; /* "page increment" */

    if (dataset_is_panel(datainfo)) {
	int tmp;

	smin = smaj = datainfo->pd;
	/* below: minimal selection should be the complete time series
	   for a single panel unit */
	if (t1max == t2max) {
	    tmp = t1max - datainfo->pd;
	    t1max = (tmp < 0)? 0 : tmp;
	}
	if (t2min == t1min) {
	    tmp = t2min + datainfo->pd;
	    t2min = (tmp > datainfo->n - 1)? datainfo->n - 1 : tmp;
	}
    } else {
	smin = 1;
	smaj = datainfo->pd;
    }

    if (label != NULL && align == SPIN_LABEL_ABOVE) {
	lbl = gtk_label_new(label);
	gtk_box_pack_start(GTK_BOX(GTK_DIALOG(rset->dlg)->vbox), 
			   lbl, FALSE, FALSE, 5);
    }

    if (label != NULL && align == SPIN_LABEL_INLINE) {
	hbox = gtk_hbox_new(FALSE, 5);
	lbl = gtk_label_new(label);
	gtk_box_pack_start(GTK_BOX(hbox), lbl, FALSE, FALSE, 5);
    } else {
	hbox = gtk_hbox_new(TRUE, 5);
    }

    /* spinner for t1 */
    vbox = gtk_vbox_new(FALSE, 5);
    if (t1str != NULL) {
	lbl = gtk_label_new(t1str);
	gtk_box_pack_start(GTK_BOX(vbox), lbl, FALSE, FALSE, 0);
    }
    rset->adj1 = gtk_adjustment_new(*t1, t1min, t1max, smin, smaj, 0);
    rset->startspin = obs_button_new(GTK_ADJUSTMENT(rset->adj1), datainfo);
    gtk_box_pack_start(GTK_BOX(vbox), rset->startspin, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hbox), vbox, FALSE, FALSE, 5);

    /* spinner for t2, if wanted */
    if (t2 != NULL) {
	vbox = gtk_vbox_new(FALSE, 5);
	if (t2str != NULL) {
	    lbl = gtk_label_new(t2str);
	    gtk_box_pack_start(GTK_BOX(vbox), lbl, FALSE, FALSE, 0);
	}
	rset->adj2 = gtk_adjustment_new(*t2, t2min, t2max, smin, smaj, 0);
	rset->endspin = obs_button_new(GTK_ADJUSTMENT(rset->adj2), datainfo);
	gtk_box_pack_start(GTK_BOX(vbox), rset->endspin, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(hbox), vbox, FALSE, FALSE, 5);

	/* inter-connect the two spinners */
	g_object_set_data(G_OBJECT(rset->startspin), "endspin", rset->endspin);
	g_object_set_data(G_OBJECT(rset->endspin), "startspin", rset->startspin);

	if (dataset_is_panel(datainfo)) {
	    /* ensure that minimum separation of the spinners represents
	       full time-series length */
	    g_object_set_data(G_OBJECT(rset->startspin), "minsep", 
			      GINT_TO_POINTER(datainfo->pd - 1));
	    g_object_set_data(G_OBJECT(rset->endspin), "minsep", 
			      GINT_TO_POINTER(datainfo->pd - 1));
	}
    }

    return hbox;
}

static int sample_range_code (GtkAction *action)
{
    const gchar *s = gtk_action_get_name(action);

    if (!strcmp(s, "SMPLDUM"))
	return SMPLDUM;
    else if (!strcmp(s, "SMPLRAND"))
	return SMPLRAND;
    else 
	return SMPL;
}

void sample_range_dialog (GtkAction *action, gpointer p)
{
    struct range_setting *rset = NULL;
    GList *dumlist = NULL;
    int u, thisdum = 0;
    GtkWidget *tempwid, *hbox;

    u = sample_range_code(action);

    if (u == SMPLDUM) {
	dumlist = get_dummy_list(&thisdum);
	if (dumlist == NULL) {
	    if (dataset_is_restricted()) {
		warnbox(_("There are no dummy variables in the current sample"));
	    } else {
		warnbox(_("There are no dummy variables in the dataset"));
	    }
	    return;
	}
    }

    rset = rset_new(u, p, NULL, NULL, NULL, _("gretl: set sample"));
    if (rset == NULL) return;

    if (u == SMPLDUM) {
	GList *dlist;

	tempwid = gtk_label_new(_("Name of dummy variable to use:"));
	hbox = gtk_hbox_new(TRUE, 5);
	gtk_box_pack_start(GTK_BOX(hbox), tempwid, FALSE, FALSE, 5);
	gtk_box_pack_start(GTK_BOX(GTK_DIALOG(rset->dlg)->vbox), 
			   hbox, FALSE, FALSE, 5);

	rset->combo = gtk_combo_box_new_text();
	dlist = dumlist;
	while (dlist != NULL) {
	    gtk_combo_box_append_text(GTK_COMBO_BOX(rset->combo), dlist->data);
	    dlist = dlist->next;
	}
	gtk_combo_box_set_active(GTK_COMBO_BOX(rset->combo), thisdum);
	g_list_free(dumlist);
	g_signal_connect(G_OBJECT(rset->combo), "changed",
			 G_CALLBACK(update_obs_label), rset);

	hbox = gtk_hbox_new(TRUE, 5);
	gtk_box_pack_start(GTK_BOX(hbox), rset->combo, FALSE, FALSE, 5);
	gtk_box_pack_start(GTK_BOX(GTK_DIALOG(rset->dlg)->vbox), 
			   hbox, FALSE, FALSE, 5);
    } else if (u == SMPLRAND) {
	gchar *labtxt;
	GtkObject *adj;

	hbox = gtk_hbox_new(FALSE, 5);

	labtxt = g_strdup_printf(_("Number of observations to select (max %d)"),
				 datainfo->n - 1);

	/* spinner for number of obs */
	tempwid = gtk_label_new(labtxt);
	gtk_box_pack_start(GTK_BOX(hbox), tempwid, FALSE, FALSE, 5);
	adj = gtk_adjustment_new(default_randsize(), 
				 1, datainfo->n - 1,
				 1, 1, 0);
	rset->startspin = gtk_spin_button_new(GTK_ADJUSTMENT(adj), 1, 0);
	gtk_box_pack_start(GTK_BOX(hbox), rset->startspin, FALSE, FALSE, 5);

	gtk_box_pack_start(GTK_BOX(GTK_DIALOG(rset->dlg)->vbox), 
			   hbox, FALSE, FALSE, 5);
    } else if (u == SMPL && dataset_is_panel(datainfo)) {
	hbox = panel_sample_spinbox(rset);
	gtk_box_pack_start(GTK_BOX(GTK_DIALOG(rset->dlg)->vbox), 
			   hbox, FALSE, FALSE, 5);
	rset->opt = OPT_P;
    } else { 
	/* either plain SMPL or CREATE_DATASET */
	hbox = obs_spinbox(rset, _("Set sample range"), 
			   _("Start:"), _("End:"),
			   0, datainfo->n - 1, &datainfo->t1,
			   0, datainfo->n - 1, &datainfo->t2,
			   SPIN_LABEL_ABOVE);
	gtk_box_pack_start(GTK_BOX(GTK_DIALOG(rset->dlg)->vbox), 
			   hbox, FALSE, FALSE, 5);
    }

    if (u != SMPLRAND) {
	/* label that will show the number of observations */
	rset->obslabel = gtk_label_new("");
	gtk_box_pack_start(GTK_BOX(GTK_DIALOG(rset->dlg)->vbox), 
			   rset->obslabel, FALSE, FALSE, 5);
    }

    if (u == SMPL || u == CREATE_DATASET) {
	g_object_set_data(G_OBJECT(rset->startspin), "rset", rset);
	g_object_set_data(G_OBJECT(rset->endspin), "rset", rset);
    }

    if (u == SMPLDUM) {
	update_obs_label(GTK_COMBO_BOX(rset->combo), rset);
    }

    /* Cancel button */
    cancel_delete_button(GTK_DIALOG(rset->dlg)->action_area, rset->dlg, NULL);

    /* "OK" button */
    tempwid = ok_button(GTK_DIALOG(rset->dlg)->action_area);
    g_signal_connect(G_OBJECT(tempwid), "clicked",
		     G_CALLBACK(set_sample_from_dialog), rset);
    gtk_widget_grab_default(tempwid);

    g_signal_connect(G_OBJECT(rset->dlg), "destroy", 
		     G_CALLBACK(free_rsetting), rset);

    gtk_widget_show_all(rset->dlg);
}

/* general purpose dialog box for getting from the user either one or
   two observations (e.g. for setting the break point in a Chow test,
   or setting the start and end of a sample range)
*/

int get_obs_dialog (const char *title, const char *text,
		    const char *t1str, const char *t2str,
		    int t1min, int t1max, int *t1, 
		    int t2min, int t2max, int *t2)
{
    GtkWidget *tempwid;
    struct range_setting *rset;
    int ret = 0;

    rset = rset_new(0, NULL, NULL, t1, t2, title);
    if (rset == NULL) {
	return -1;
    }

    tempwid = obs_spinbox(rset, text, t1str, t2str, 
			  t1min, t1max, t1, 
			  t2min, t2max, t2,
			  SPIN_LABEL_ABOVE);

    gtk_box_pack_start(GTK_BOX(GTK_DIALOG (rset->dlg)->vbox), 
		       tempwid, TRUE, TRUE, 0);

    /* Cancel button */
    cancel_options_button(GTK_DIALOG(rset->dlg)->action_area, rset->dlg, &ret);

    /* "OK" button */
    tempwid = ok_button(GTK_DIALOG (rset->dlg)->action_area);
    g_signal_connect(G_OBJECT(tempwid), "clicked",
		     G_CALLBACK(set_obs_from_dialog), rset);
    gtk_widget_grab_default(tempwid);

    g_signal_connect(G_OBJECT(rset->dlg), "destroy", 
		     G_CALLBACK(free_rsetting), rset);

    gretl_set_window_modal(rset->dlg);
    gtk_widget_show_all(rset->dlg);

    return ret;
}

static void sync_pre_forecast (GtkWidget *w, struct range_setting *rset)
{
    if (rset->p != NULL) {
	int t1 = (int) obs_button_get_value(OBS_BUTTON(rset->startspin));
	GtkAdjustment *preadj = GTK_ADJUSTMENT(rset->p);

	if (preadj->upper != t1) {
	    preadj->upper = t1;
	    if (preadj->value > t1) {
		preadj->value = t1;
		gtk_adjustment_value_changed(preadj);
	    }
	    gtk_adjustment_changed(preadj);
	}
    }
}

static void adjust_fcast_t1 (GtkWidget *w, struct range_setting *rset)
{
    int t1 = (int) obs_button_get_value(OBS_BUTTON(rset->startspin));
    int i = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(w), "action"));

    if (rset->pmod == NULL) {
	return;
    }

    if (i == 3) {
	int t1min = rset->pmod->t1 + rset->pmod->ncoeff;

	if (t1 < t1min) {
	    obs_button_set_value(OBS_BUTTON(rset->startspin), 
				 (gdouble) t1min);
	    g_object_set(rset->adj1, "lower", (gdouble) t1min, NULL);
	}
    } else if (i == 2) {
	double txmin;

	g_object_get(rset->adj1, "lower", &txmin, NULL);
	if (txmin > *rset->t1) {
	    g_object_set(rset->adj1, "lower", (gdouble) *rset->t1, NULL);
	}
    }
}

int forecast_dialog (int t1min, int t1max, int *t1, 
		     int t2min, int t2max, int *t2,
		     int pmin, int pmax, int *p,
		     int dyn, gretlopt *optp,
		     MODEL *pmod)
{
    const char *pre_txt = N_("Number of pre-forecast observations "
			     "to graph");
    const char *opts[] = {
	N_("automatic forecast (dynamic out of sample)"),
	N_("dynamic forecast"),
	N_("static forecast"),
	N_("rolling one-step ahead forecasts"),
    };
    int nopts = 3;
    int deflt = 0;
    GtkWidget *tmp;
    GtkWidget *hbox;
    GtkWidget *button = NULL;
    struct range_setting *rset;
    int i, ret = 0;

    rset = rset_new(0, NULL, pmod, t1, t2, _("gretl: forecast"));
    if (rset == NULL) {
	return -1;
    }

    if (pmod != NULL && pmod->ci == OLS) {
	nopts++;
    }

    tmp = obs_spinbox(rset, _("Forecast range:"), 
		      _("Start"), _("End"), 
		      t1min, t1max, t1, 
		      t2min, t2max, t2,
		      SPIN_LABEL_INLINE);

    g_signal_connect(G_OBJECT(rset->adj1), "value-changed",
		     G_CALLBACK(sync_pre_forecast), rset);

    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(rset->dlg)->vbox), 
		       tmp, TRUE, TRUE, 5);

    tmp = gtk_hseparator_new();
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(rset->dlg)->vbox), 
		       tmp, TRUE, TRUE, 0);
    
    if (dyn == DYNAMIC_NA) {
	deflt = 2;
    } else if (dyn == DYNAMIC_FORCED) {
	deflt = 1;
    }

    /* forecast-type options */
    for (i=0; i<nopts; i++) {
	GSList *group;

	if (button != NULL) {
	    group = gtk_radio_button_get_group(GTK_RADIO_BUTTON(button));
	} else {
	    group = NULL;
	}

	hbox = gtk_hbox_new(FALSE, 5);
	button = gtk_radio_button_new_with_label(group, _(opts[i]));
	gtk_box_pack_start(GTK_BOX(hbox), button, FALSE, FALSE, 5);
	gtk_box_pack_start(GTK_BOX(GTK_DIALOG(rset->dlg)->vbox), 
			   hbox, TRUE, TRUE, 0);

	if (deflt == i) {
	    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(button), TRUE);
	    ret = i;
	}

	if (dyn != DYNAMIC_OK && i < 2) {
	    gtk_widget_set_sensitive(button, FALSE);
	} else {
	    if (i >= 2) {
		g_signal_connect(G_OBJECT(button), "clicked",
				 G_CALLBACK(adjust_fcast_t1), 
				 rset);
	    }
	    g_signal_connect(G_OBJECT(button), "clicked",
			     G_CALLBACK(set_radio_opt), &ret);
	    g_object_set_data(G_OBJECT(button), "action", 
			      GINT_TO_POINTER(i));
	}
    }

    /* pre-forecast obs spinner */
    tmp = gtk_hseparator_new();
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(rset->dlg)->vbox), 
		       tmp, TRUE, TRUE, 0);
    hbox = gtk_hbox_new(FALSE, 5);
    tmp = option_spinbox(p, _(pre_txt), pmin, pmax, 0, &rset->p);
    gtk_box_pack_start(GTK_BOX(hbox), tmp, FALSE, FALSE, 5);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(rset->dlg)->vbox), 
		       hbox, TRUE, TRUE, 5);
    /* get the max pre-forecast obs right */
    gtk_adjustment_value_changed(GTK_ADJUSTMENT(rset->adj1));

    /* graph style selection */
    if (1) {
	static const char *strs[] = {
	    N_("error bars"),
	    N_("low and high lines"),
	    N_("shaded area"),
	    NULL
	};
	static gretlopt opts[] = {
	    OPT_NONE,
	    OPT_L,
	    OPT_F
	};
	static combo_opts ci_opts;
	int deflt;

	if (gnuplot_has_style_fill()) {
	    deflt = (*optp & OPT_L)? 1 : (*optp & OPT_F)? 2 : 0;
	} else {
	    strs[2] = NULL;
	    deflt = (*optp & OPT_L)? 1 : 0;
	}

	ci_opts.strs = strs;
	ci_opts.vals = opts;
	ci_opts.optp = optp;

	hbox = gtk_hbox_new(FALSE, 0);
	tmp = gtk_label_new(_("Plot confidence interval using"));
	gtk_box_pack_start(GTK_BOX(hbox), tmp, FALSE, FALSE, 5);
	tmp = gretl_opts_combo(&ci_opts, deflt);
	gtk_box_pack_start(GTK_BOX(hbox), tmp, FALSE, FALSE, 5);
	gtk_box_pack_start(GTK_BOX(GTK_DIALOG(rset->dlg)->vbox), 
			   hbox, TRUE, TRUE, 5);
    }

    /* Cancel button */
    cancel_options_button(GTK_DIALOG(rset->dlg)->action_area, rset->dlg, &ret);

    /* "OK" button */
    tmp = ok_button(GTK_DIALOG(rset->dlg)->action_area);
    g_signal_connect(G_OBJECT(tmp), "clicked",
		     G_CALLBACK(set_obs_from_dialog), rset);
    gtk_widget_grab_default(tmp);

    /* Create a "Help" button */
    context_help_button(GTK_DIALOG(rset->dlg)->action_area, FCAST);

    g_signal_connect(G_OBJECT(rset->dlg), "destroy", 
		     G_CALLBACK(free_rsetting), rset);

    /* gretl_set_window_modal(rset->dlg); */
    gtk_widget_show_all(rset->dlg);

    return ret;
}

struct add_obs_info {
    GtkWidget *dlg;
    GtkWidget *spin;
    int val;
};

static gboolean set_add_obs (GtkWidget *w, struct add_obs_info *ainfo)
{
    ainfo->val = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(ainfo->spin));
    gtk_widget_destroy(ainfo->dlg);

    return TRUE;
}

int add_obs_dialog (const char *blurb, int addmin)
{
    int panel = dataset_is_panel(datainfo);
    struct add_obs_info ainfo;
    int step, addmax;
    GtkWidget *hbox;
    GtkWidget *tmp;

    if (panel) {
	ainfo.val = datainfo->pd;
	addmin = datainfo->pd;
	step = datainfo->pd;
	addmax = 10 * datainfo->pd;
    } else {
	ainfo.val = 1;
	step = 1;
	addmax = 100;
    }

    ainfo.dlg = gretl_dialog_new(_("Add observations"), NULL,
				 GRETL_DLG_MODAL | GRETL_DLG_BLOCK);

    if (blurb != NULL) {
	hbox = gtk_hbox_new(FALSE, 5);
	tmp = gtk_label_new(blurb);
	gtk_box_pack_start(GTK_BOX(hbox), tmp, TRUE, TRUE, 5);
	gtk_box_pack_start(GTK_BOX(GTK_DIALOG(ainfo.dlg)->vbox), 
			   hbox, TRUE, TRUE, 0);
    }

    hbox = gtk_hbox_new(FALSE, 5);
    tmp = gtk_label_new(_("Number of observations to add:"));
    gtk_box_pack_start(GTK_BOX(hbox), tmp, TRUE, TRUE, 5);

    ainfo.spin = gtk_spin_button_new_with_range(addmin, addmax, step);
    gtk_box_pack_start(GTK_BOX(hbox), ainfo.spin, TRUE, TRUE, 5);
    
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(ainfo.dlg)->vbox), 
		       hbox, TRUE, TRUE, 0);

    /* Cancel button */
    cancel_options_button(GTK_DIALOG(ainfo.dlg)->action_area, ainfo.dlg, 
			  &ainfo.val);

    /* "OK" button */
    tmp = ok_button(GTK_DIALOG(ainfo.dlg)->action_area);
    g_signal_connect(G_OBJECT(tmp), "clicked",
		     G_CALLBACK(set_add_obs), &ainfo);
    gtk_widget_grab_default(tmp);

    gtk_widget_show_all(ainfo.dlg);

    return ainfo.val;
}

static void set_var_from_combo (GtkWidget *w, GtkWidget *dlg)
{
    GtkWidget *combo = g_object_get_data(G_OBJECT(dlg), "combo");
    int *selvar = g_object_get_data(G_OBJECT(dlg), "selvar");
    gchar *vname;

    vname = gtk_combo_box_get_active_text(GTK_COMBO_BOX(combo));
    *selvar = series_index(datainfo, vname);
    g_free(vname);
}

static void 
dialog_option_callback (GtkWidget *w, dialog_opts *opts)
{
    int i = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(w), "i"));

    if (GTK_TOGGLE_BUTTON(w)->active) {
	*opts->optp |= opts->vals[i];
    } else {
	*opts->optp &= ~(opts->vals[i]);
    }
}

static void dialog_add_opts (dialog_opts *opts, 
			     GtkWidget *vbox)
{
    if (opts->type == OPT_TYPE_RADIO) {
	GSList *group = NULL;
	GtkWidget *b, *v2, *hbox;
	int i;

	v2 = gtk_vbox_new(FALSE, 0);

	for (i=0; i<opts->n; i++) {
	    b = gtk_radio_button_new_with_label(group, _(opts->strs[i]));
	    gtk_box_pack_start(GTK_BOX(v2), b, TRUE, TRUE, 0);
	    /* gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(b), TRUE); */
	    g_object_set_data(G_OBJECT(b), "i", GINT_TO_POINTER(i));
	    g_signal_connect(G_OBJECT(b), "clicked",
			     G_CALLBACK(dialog_option_callback), opts);
	    gtk_widget_show(b);
	    group = gtk_radio_button_get_group(GTK_RADIO_BUTTON(b));
	}

	hbox = gtk_hbox_new(FALSE, 5);
	gtk_box_pack_start(GTK_BOX(hbox), v2, TRUE, TRUE, 10);
	gtk_box_pack_start(GTK_BOX(vbox), hbox, TRUE, TRUE, 0);
	gtk_widget_show(hbox);
	gtk_widget_show(v2);

    } else {
	/* handle combo eventually? */
	dummy_call();
    }
}

int select_var_from_list_with_opt (const int *list, 
				   const char *query,
				   dialog_opts *opts,
				   int hcode)
{
    unsigned char flags;
    GtkWidget *tempwid, *hbox;
    GtkWidget *dlg, *combo;
    gchar *title;
    int i, selvar = -1;

    title = g_strdup_printf("gretl: %s", _("select variable"));

    flags = (hcode)? GRETL_DLG_BLOCK : (GRETL_DLG_MODAL | GRETL_DLG_BLOCK);
    dlg = gretl_dialog_new(title, NULL, flags);
    g_free(title);

    tempwid = gtk_label_new(query);
    hbox = gtk_hbox_new(TRUE, 5);
    gtk_box_pack_start(GTK_BOX(hbox), tempwid, FALSE, FALSE, 5);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dlg)->vbox), 
		       hbox, FALSE, FALSE, 5);

    combo = gtk_combo_box_new_text();
    for (i=1; i<=list[0]; i++) {
	gtk_combo_box_append_text(GTK_COMBO_BOX(combo), 
				  datainfo->varname[list[i]]);
    }

    /* select last entry in list */
    gtk_combo_box_set_active(GTK_COMBO_BOX(combo), list[0] - 1);

    hbox = gtk_hbox_new(TRUE, 5);
    gtk_box_pack_start(GTK_BOX(hbox), combo, FALSE, FALSE, 5);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dlg)->vbox), 
		       hbox, FALSE, FALSE, 5);

    g_object_set_data(G_OBJECT(dlg), "combo", combo);
    g_object_set_data(G_OBJECT(dlg), "selvar", &selvar);

    if (opts != NULL) {
	dialog_add_opts(opts, GTK_DIALOG(dlg)->vbox);
    }

    /* Cancel button */
    cancel_delete_button(GTK_DIALOG(dlg)->action_area, dlg, NULL);

    /* "OK" button */
    tempwid = ok_button(GTK_DIALOG(dlg)->action_area);
    g_signal_connect(G_OBJECT(tempwid), "clicked",
		     G_CALLBACK(set_var_from_combo), dlg);
    g_signal_connect(G_OBJECT(tempwid), "clicked",
		     G_CALLBACK(delete_widget), dlg);
    gtk_widget_grab_default(tempwid);

    /* Create a "Help" button? */
    if (hcode) {
	context_help_button(GTK_DIALOG(dlg)->action_area, hcode);
    }

    gtk_widget_show_all(dlg);

    return selvar;
}

int select_var_from_list (const int *list, const char *query)
{
    return select_var_from_list_with_opt(list, query, NULL, 0);
}

/* material relating to the data compaction dialog */

struct compaction_info {
    int *target_pd;
    int *repday;
    GtkWidget *monday_button;
    GtkWidget *sunday_button;
    GtkWidget *wkday_opt;
};

static void abort_compact (GtkWidget *w, gpointer data)
{
    gint *method = (gint *) data;

    *method = COMPACT_NONE;
}

static void set_compact_type (GtkWidget *w, gpointer data)
{
    gint *method = (gint *) data;

    if (GTK_TOGGLE_BUTTON (w)->active) {
        *method = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(w), "action"));
    }
}

static void sensitize_daylist (GtkWidget *w, GtkWidget *c)
{
    if (c != NULL) {
	gtk_widget_set_sensitive(c, GTK_TOGGLE_BUTTON(w)->active);
    }
}

static void set_target_pd (GtkWidget *w, gpointer data)
{
    struct compaction_info *cinfo = data;
    gboolean wtarg;

    if (GTK_TOGGLE_BUTTON(w)->active) {
	*cinfo->target_pd = 
	    GPOINTER_TO_INT(g_object_get_data(G_OBJECT(w), "action"));
    }

    wtarg = *cinfo->target_pd == 52;

    if (cinfo->monday_button != NULL) {
	gtk_widget_set_sensitive(cinfo->monday_button, wtarg);
    }
    if (cinfo->sunday_button != NULL) {
	gtk_widget_set_sensitive(cinfo->sunday_button, wtarg);
    }
    if (cinfo->wkday_opt != NULL) {
	GtkWidget *combo = g_object_get_data(G_OBJECT(cinfo->wkday_opt),
					     "combo");
						      
	gtk_widget_set_sensitive(cinfo->wkday_opt, wtarg);
	if (combo != NULL) {
	    gtk_widget_set_sensitive(cinfo->wkday_opt, wtarg);
	}
    }    
}

static void set_mon_start (GtkWidget *w, gpointer data)
{
    gint *ms = (gint *) data;

    if (GTK_TOGGLE_BUTTON (w)->active) {
        *ms = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(w), "action"));
    }
}

static void pd_buttons (GtkWidget *dlg, int spd, struct compaction_info *cinfo)
{    
    GtkWidget *button;
    GtkWidget *vbox;
    GSList *group;
    gint f1, f2;
    const char *f1str, *f2str;

    if (spd == 12) {
	f1 = 4;
	f2 = 1;
	f1str = N_("Quarterly");
	f2str = N_("Annual");
    } else if (spd == 5 || spd == 7) {
	f1 = 52;
	f2 = 12;
	f1str = N_("Weekly");
	f2str = N_("Monthly");
    } else {
	return;
    }

    vbox = GTK_DIALOG(dlg)->vbox;

    button = gtk_radio_button_new_with_label(NULL, _(f1str));
    gtk_box_pack_start (GTK_BOX(vbox), button, TRUE, TRUE, 0);

    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (button), TRUE);

    g_signal_connect(G_OBJECT(button), "clicked",
		     G_CALLBACK(set_target_pd), cinfo);
    g_object_set_data(G_OBJECT(button), "action", 
		      GINT_TO_POINTER(f1));

    gtk_widget_show (button);

    group = gtk_radio_button_get_group(GTK_RADIO_BUTTON (button));
    button = gtk_radio_button_new_with_label (group, _(f2str));
    gtk_box_pack_start (GTK_BOX(vbox), button, TRUE, TRUE, 0);

    g_signal_connect(G_OBJECT(button), "clicked",
		     G_CALLBACK(set_target_pd), cinfo);
    g_object_set_data(G_OBJECT(button), "action", 
		      GINT_TO_POINTER(f2));

    gtk_widget_show (button);
}

static void monday_buttons (GtkWidget *dlg, int *mon_start,
			    struct compaction_info *cinfo)
{
    GtkWidget *button;
    GtkWidget *vbox;
    GSList *group;

    vbox = GTK_DIALOG(dlg)->vbox;

    button = gtk_radio_button_new_with_label(NULL, _("Week starts on Monday"));
    cinfo->monday_button = button;
    gtk_box_pack_start(GTK_BOX(vbox), button, TRUE, TRUE, 0);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON (button), TRUE);

    g_signal_connect(G_OBJECT(button), "clicked",
		     G_CALLBACK(set_mon_start), mon_start);
    g_object_set_data(G_OBJECT(button), "action", 
		      GINT_TO_POINTER(1));

    gtk_widget_show (button);

    group = gtk_radio_button_get_group(GTK_RADIO_BUTTON (button));
    button = gtk_radio_button_new_with_label (group, _("Week starts on Sunday"));
    cinfo->sunday_button = button;
    gtk_box_pack_start(GTK_BOX(vbox), button, TRUE, TRUE, 0);

    g_signal_connect(G_OBJECT(button), "clicked",
		     G_CALLBACK(set_mon_start), mon_start);
    g_object_set_data(G_OBJECT(button), "action", 
		      GINT_TO_POINTER(0));

    gtk_widget_show (button);
}

static const char *weekdays[] = {
    N_("Sunday"),
    N_("Monday"),
    N_("Tuesday"),
    N_("Wednesday"),
    N_("Thursday"),
    N_("Friday"),
    N_("Saturday")
};

gboolean select_repday (GtkComboBox *menu, int *repday)
{
    int i = gtk_combo_box_get_active(menu);

    *repday = (datainfo->pd == 7)? i : i + 1;

    return FALSE;
}

enum {
    NO_METHODS_SET,
    SOME_METHODS_SET,
    ALL_METHODS_SET
};

static void compact_method_buttons (GtkWidget *dlg, CompactMethod *method,
				    int current_pd, int methods_set,
				    struct compaction_info *cinfo)
{
    GtkWidget *button;
    GtkWidget *vbox;
    GSList *group;

    vbox = GTK_DIALOG(dlg)->vbox;

    if (methods_set == SOME_METHODS_SET) {
	GtkWidget *label;

	label = gtk_label_new(_("Default method:"));
	gtk_box_pack_start(GTK_BOX(vbox), label, TRUE, TRUE, 0);
	gtk_widget_show(label);
    }

    button = gtk_radio_button_new_with_label (NULL, _("Compact by averaging"));
    gtk_box_pack_start(GTK_BOX(vbox), button, TRUE, TRUE, 0);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON (button), TRUE);
    g_signal_connect(G_OBJECT(button), "clicked",
		     G_CALLBACK(set_compact_type), method);
    g_object_set_data(G_OBJECT(button), "action", 
		      GINT_TO_POINTER(COMPACT_AVG));
    gtk_widget_show(button);

    group = gtk_radio_button_get_group(GTK_RADIO_BUTTON (button));
    button = gtk_radio_button_new_with_label (group, _("Compact by summing"));
    gtk_box_pack_start (GTK_BOX(vbox), button, TRUE, TRUE, 0);
    g_signal_connect(G_OBJECT(button), "clicked",
		     G_CALLBACK(set_compact_type), method);
    g_object_set_data(G_OBJECT(button), "action", 
		      GINT_TO_POINTER(COMPACT_SUM));
    gtk_widget_show(button);
    if (current_pd == 52) gtk_widget_set_sensitive(button, FALSE);

    group = gtk_radio_button_get_group (GTK_RADIO_BUTTON (button));
    button = gtk_radio_button_new_with_label(group, _("Use end-of-period values"));
    gtk_box_pack_start(GTK_BOX(vbox), button, TRUE, TRUE, 0);
    g_signal_connect(G_OBJECT(button), "clicked",
		     G_CALLBACK(set_compact_type), method);
    g_object_set_data(G_OBJECT(button), "action", 
		      GINT_TO_POINTER(COMPACT_EOP));
    gtk_widget_show(button);
    if (current_pd == 52) gtk_widget_set_sensitive(button, FALSE);

    group = gtk_radio_button_get_group (GTK_RADIO_BUTTON (button));
    button = gtk_radio_button_new_with_label(group, _("Use start-of-period values"));
    gtk_box_pack_start(GTK_BOX(vbox), button, TRUE, TRUE, 0);
    g_signal_connect(G_OBJECT(button), "clicked",
		     G_CALLBACK(set_compact_type), method);
    g_object_set_data(G_OBJECT(button), "action", 
		      GINT_TO_POINTER(COMPACT_SOP));
    gtk_widget_show(button);
    if (current_pd == 52) gtk_widget_set_sensitive(button, FALSE);

    if (dated_daily_data(datainfo) && cinfo->repday != NULL) {
	GtkWidget *hbox, *daymenu;
	int i;

	hbox = gtk_hbox_new(FALSE, 5);
	group = gtk_radio_button_get_group (GTK_RADIO_BUTTON (button));
	button = gtk_radio_button_new_with_label(group, _("Use representative day"));
	gtk_box_pack_start(GTK_BOX(hbox), button, FALSE, FALSE, 0);
	g_signal_connect(G_OBJECT(button), "clicked",
			 G_CALLBACK(set_compact_type), method);
	g_object_set_data(G_OBJECT(button), "action", 
			  GINT_TO_POINTER(COMPACT_WDAY));
	cinfo->wkday_opt = button;

	daymenu = gtk_combo_box_new_text();
	for (i=0; i<7; i++) {
	    if ((i == 0 && datainfo->pd != 7) ||
		(i == 6 && datainfo->pd == 5)) {
		continue;
	    }
	    gtk_combo_box_append_text(GTK_COMBO_BOX(daymenu), _(weekdays[i])); 
	}
	gtk_combo_box_set_active(GTK_COMBO_BOX(daymenu), 0);
	gtk_box_pack_start(GTK_BOX(hbox), daymenu, FALSE, FALSE, 5);
	g_signal_connect(G_OBJECT(daymenu), "changed",
			 G_CALLBACK(select_repday), cinfo->repday);
	if (*method != COMPACT_WDAY) {
	    gtk_widget_set_sensitive(daymenu, FALSE);
	}
	
	g_signal_connect(G_OBJECT(button), "clicked",
			 G_CALLBACK(sensitize_daylist), daymenu);
	g_object_set_data(G_OBJECT(button), "daymenu", daymenu);

	gtk_box_pack_start(GTK_BOX(vbox), hbox, TRUE, TRUE, 0);
	gtk_widget_show_all(hbox);
    }
}

static int compact_methods_set (void)
{
    int i, nmeth = 0;
    int ret = NO_METHODS_SET;

    for (i=1; i<datainfo->v; i++) {
	if (COMPACT_METHOD(datainfo, i) != COMPACT_NONE) {
	    nmeth++;
	}
    }

    if (nmeth == datainfo->v - 1) {
	ret = ALL_METHODS_SET;
    } else if (nmeth > 0) {
	ret = SOME_METHODS_SET;
    }

    return ret;
}

void data_compact_dialog (GtkWidget *w, int spd, int *target_pd, 
			  int *mon_start, CompactMethod *method,
			  int *repday)
{
    GtkWidget *d, *tempwid, *hbox;
    int show_pd_buttons = 0;
    int show_monday_buttons = 0;
    int show_method_buttons = 0;
    int methods_set = NO_METHODS_SET;
    struct compaction_info cinfo;
    gchar *labelstr = NULL;

    d = gretl_dialog_new(_("gretl: compact data"), w, GRETL_DLG_BLOCK);

    cinfo.target_pd = target_pd;
    cinfo.repday = repday;
    cinfo.monday_button = NULL;
    cinfo.sunday_button = NULL;
    cinfo.wkday_opt = NULL;

    if (mon_start != NULL) {
	*mon_start = 1;
    }
    
    if (*target_pd != 0) {
	/* importing series from database */
	labelstr = g_strdup_printf(_("You are adding a %s series to %s dataset"),
				   (spd == 4)? _("quarterly") : _("monthly"),
				   (*target_pd == 4)? _("a quarterly"): _("an annual"));
    } else {
	/* compacting whole data set */
	if (spd == 4) {
	    *target_pd = 1;
	    labelstr = g_strdup(_("Compact quarterly data to annual"));
	} else if (spd == 12) {
	    /* source data are monthly */
	    labelstr = g_strdup(_("Compact monthly data to:"));
	    *target_pd = 4;
	    show_pd_buttons = 1;
	} else if (spd >= 5 && spd <= 7) {
	    /* source data are daily */
	    if (dated_daily_data(datainfo)) {
		labelstr = g_strdup(_("Compact daily data to:"));
		show_pd_buttons = 1;
	    } else {
		labelstr = g_strdup(_("Compact daily data to weekly"));
	    }
	    *target_pd = 52;
	    if (mon_start != NULL) {
		show_monday_buttons = 1;
	    }
	} else if (dated_weekly_data(datainfo)) {
	    labelstr = g_strdup(_("Compact weekly data to monthly"));
	    *target_pd = 12;
	}
	methods_set = compact_methods_set();
    }

#if 0
    gtk_box_set_spacing(GTK_BOX(GTK_DIALOG(d)->action_area), 15);
#endif

    tempwid = gtk_label_new(labelstr);
    g_free(labelstr);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(d)->vbox), 
		       tempwid, TRUE, TRUE, 0);
    gtk_widget_show(tempwid);

    show_method_buttons = (methods_set != ALL_METHODS_SET);

    /* monthly data: give choice of going to quarterly or annual
       dated daily: choice of going to weekly or monthly */
    if (show_pd_buttons) {
	pd_buttons(d, spd, &cinfo);
	if (show_monday_buttons || show_method_buttons) {
	    vbox_add_hsep(GTK_DIALOG(d)->vbox);
	}	
    }

    /* 7-day daily data: give choice of when the week starts */
    if (show_monday_buttons) {
	monday_buttons(d, mon_start, &cinfo);
	if (show_method_buttons) {
	    vbox_add_hsep(GTK_DIALOG(d)->vbox);
	}	
    }

    /* per-variable compaction methods not all set already: 
       give choice of default compaction method */
    if (show_method_buttons) {
	compact_method_buttons(d, method, spd, methods_set, &cinfo);
    } 

    hbox = GTK_DIALOG(d)->action_area;

    /* "Cancel" button */
    tempwid = cancel_button(hbox);
    g_signal_connect(G_OBJECT(tempwid), "clicked", 
		     G_CALLBACK(abort_compact), method);
    g_signal_connect (G_OBJECT(tempwid), "clicked", 
		      G_CALLBACK(delete_widget), 
		      G_OBJECT(d));
    gtk_widget_show(tempwid);

    /* "OK" button */
    tempwid = ok_button(hbox);
    g_signal_connect(G_OBJECT(tempwid), "clicked", 
		     G_CALLBACK(delete_widget), 
		     G_OBJECT(d));
    gtk_widget_grab_default(tempwid);
    gtk_widget_show(tempwid);

    /* Create a "Help" button */
    context_help_button(hbox, COMPACT);

    gtk_widget_show(d);
}

static void set_expand_target_pd (GtkWidget *w, gpointer data)
{
    int *targ_pd = (int *) data;

    if (GTK_TOGGLE_BUTTON(w)->active) {
	*targ_pd = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(w), "action"));
    }
}

static void expand_pd_buttons (GtkWidget *dlg, int spd, int *target_pd)
{    
    GtkWidget *button;
    GtkWidget *vbox;
    GSList *group;
    gint f1, f2;
    const char *f1str, *f2str;

    if (spd == 1) {
	f1 = 4;
	f2 = 12;
	f1str = N_("Quarterly");
	f2str = N_("Monthly");
    } else {
	return;
    }

    vbox = GTK_DIALOG(dlg)->vbox;

    button = gtk_radio_button_new_with_label(NULL, _(f1str));
    gtk_box_pack_start(GTK_BOX(vbox), button, TRUE, TRUE, 0);

    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON (button), TRUE);

    g_signal_connect(G_OBJECT(button), "clicked",
		     G_CALLBACK(set_expand_target_pd), target_pd);
    g_object_set_data(G_OBJECT(button), "action", 
		      GINT_TO_POINTER(f1));

    gtk_widget_show (button);

    group = gtk_radio_button_get_group(GTK_RADIO_BUTTON(button));
    button = gtk_radio_button_new_with_label(group, _(f2str));
    gtk_box_pack_start (GTK_BOX(vbox), button, TRUE, TRUE, 0);

    g_signal_connect(G_OBJECT(button), "clicked",
		     G_CALLBACK(set_expand_target_pd), target_pd);
    g_object_set_data(G_OBJECT(button), "action", 
		      GINT_TO_POINTER(f2));

    gtk_widget_show (button);
}

static void abort_expand (GtkWidget *w, gpointer data)
{
    int *pd = (int *) data;

    *pd = -1;
}

void data_expand_dialog (GtkWidget *w, int spd, int *target_pd)
{
    GtkWidget *d, *tempwid, *hbox;
    int show_pd_buttons = 0;
    gchar *labelstr = NULL;

    d = gretl_dialog_new(_("gretl: expand data"), w, GRETL_DLG_BLOCK);

    if (*target_pd != 0) {
	/* importing series from database */
	labelstr = g_strdup_printf(_("Adding %s series to %s dataset"),
				   (spd == 1)? _("annual") : _("quarterly"),
				   (*target_pd == 4)? _("quarterly"): _("monthly"));
    } else {
	/* expanding whole data set */
	if (spd == 1) {
	    labelstr = g_strdup(_("Expand annual data to:"));
	    *target_pd = 4;
	    show_pd_buttons = 1;
	} else if (spd == 4) {
	    /* source data are monthly */
	    labelstr = g_strdup(_("Expand quarterly data to monthly"));
	    *target_pd = 12;
	}
    }

#if 0
    gtk_box_set_spacing(GTK_BOX(GTK_DIALOG(d)->action_area), 15);
#endif

    tempwid = gtk_label_new(labelstr);
    g_free(labelstr);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(d)->vbox), 
		       tempwid, TRUE, TRUE, 0);
    gtk_widget_show(tempwid);

    /* annual data: give choice of going to quarterly or monthly */
    if (show_pd_buttons) {
	expand_pd_buttons(d, spd, target_pd);
    }

    hbox = GTK_DIALOG(d)->action_area;

    /* Cancel button */
    tempwid = cancel_button(hbox);
    g_signal_connect(G_OBJECT(tempwid), "clicked", 
		     G_CALLBACK(abort_expand), target_pd);
    g_signal_connect (G_OBJECT(tempwid), "clicked", 
		      G_CALLBACK(delete_widget), 
		      G_OBJECT(d));
    gtk_widget_show(tempwid);

    /* "OK" button */
    tempwid = ok_button(hbox);
    g_signal_connect(G_OBJECT(tempwid), "clicked", 
		     G_CALLBACK(delete_widget), 
		     G_OBJECT(d));
    gtk_widget_grab_default(tempwid);
    gtk_widget_show(tempwid);

    /* Create a "Help" button */
    context_help_button(hbox, EXPAND);

    gtk_widget_show(d);
}

static void set_radio_opt (GtkWidget *w, int *opt)
{
    *opt = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(w), "action"));
}

int real_radio_dialog (const char *title, const char *label,
		       const char **opts, int nopts, int deflt, int hcode,
		       int *extravar, const char *extratxt,
		       int spinmin, int spinmax)
{
    GtkWidget *dialog;
    GtkWidget *tmp;
    GtkWidget *button = NULL;
    GSList *group = NULL;
    int i, ret = -1;

    if (maybe_raise_dialog()) {
	return ret;
    }

    dialog = gretl_dialog_new(title, NULL, GRETL_DLG_BLOCK);

    if (label != NULL) {
	GtkWidget *hbox = gtk_hbox_new(FALSE, 5);
	
	gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->vbox), 
			   hbox, TRUE, TRUE, 5);
	gtk_widget_show(hbox);
	tmp = gtk_label_new(label);
	gtk_box_pack_start(GTK_BOX(hbox), tmp, TRUE, TRUE, 5);
	gtk_widget_show(tmp);
    }

    for (i=0; i<nopts; i++) {
	button = gtk_radio_button_new_with_label(group, _(opts[i]));
	gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->vbox), 
			   button, TRUE, TRUE, 0);
	if (i == deflt) {
	    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON (button), TRUE);
	    ret = i;
	}
	g_signal_connect(G_OBJECT(button), "clicked",
			 G_CALLBACK(set_radio_opt), &ret);
	g_object_set_data(G_OBJECT(button), "action", 
			  GINT_TO_POINTER(i));
	gtk_widget_show(button);
	group = gtk_radio_button_get_group(GTK_RADIO_BUTTON(button));
    }

    if (extravar != NULL) {
	if (spinmin == 0 && spinmax == 0) {
	    /* must be checkbox */
	    vbox_add_hsep(GTK_DIALOG(dialog)->vbox);
	    tmp = option_checkbox(extravar, extratxt);
	} else {
	    /* create spinner */
	    tmp = option_spinbox(extravar, extratxt, spinmin, spinmax, 0, NULL);
	}
	gtk_widget_show(tmp);
	gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->vbox), 
			   tmp, TRUE, TRUE, 0);
    }

    /* "Cancel" button */
    cancel_options_button(GTK_DIALOG(dialog)->action_area, dialog, &ret);

    /* "OK" button */
    tmp = ok_button(GTK_DIALOG(dialog)->action_area);
    g_signal_connect(G_OBJECT(tmp), "clicked", 
		     G_CALLBACK(delete_widget), dialog);
    gtk_widget_grab_default(tmp);
    gtk_widget_show(tmp);

    /* Create a "Help" button? */
    if (hcode) {
	context_help_button(GTK_DIALOG(dialog)->action_area, hcode);
    }

    gtk_widget_show(dialog);

    return ret;
}

int radio_dialog (const char *title, const char *label, const char **opts, 
		  int nopts, int deflt, int hcode)
{
    return real_radio_dialog(title, label, opts, nopts, deflt, hcode,
			     NULL, NULL, 0, 0);
}

int radio_dialog_with_spinner (const char *title, const char **opts, 
			       int nopts, int deflt, int hcode,
			       int *spinvar, const char *spintxt,
			       int spinmin, int spinmax)
{
    return real_radio_dialog(title, NULL, opts, nopts, deflt, hcode,
			     spinvar, spintxt, spinmin, spinmax);
}

int radio_dialog_with_check (const char *title, const char *label, 
			     const char **opts, int nopts, int deflt, 
			     int hcode, int *checkvar, const char *checktxt)
{
    return real_radio_dialog(title, label, opts, nopts, deflt, hcode,
			     checkvar, checktxt, 0, 0);
}


/* selections in relation to kernel density estimation */

static void bw_set (GtkWidget *w, gpointer p)
{
    double *bw = (double *) p;

    *bw = gtk_spin_button_get_value(GTK_SPIN_BUTTON(w));
}

int density_dialog (int vnum, double *bw)
{
    GtkWidget *dialog;
    GtkWidget *button;
    GtkWidget *hbox;
    GtkWidget *tempwid;
    GSList *group;
    int ret = 0;

    dialog = gretl_dialog_new(_("density estimation options"), NULL,
			      GRETL_DLG_BLOCK);

    /* kernel option buttons */

    button = gtk_radio_button_new_with_label(NULL, _("Gaussian kernel"));
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->vbox), 
		       button, TRUE, TRUE, 0);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(button), TRUE);

    g_signal_connect(G_OBJECT(button), "clicked",
		     G_CALLBACK(set_radio_opt), &ret);
    g_object_set_data(G_OBJECT(button), "action", 
		      GINT_TO_POINTER(0));
    gtk_widget_show(button);

    group = gtk_radio_button_get_group(GTK_RADIO_BUTTON(button));
    button = gtk_radio_button_new_with_label(group, _("Epanechnikov kernel"));
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->vbox), 
		       button, TRUE, TRUE, 0);
    g_signal_connect(G_OBJECT(button), "clicked",
		     G_CALLBACK(set_radio_opt), &ret);
    g_object_set_data(G_OBJECT(button), "action", 
		      GINT_TO_POINTER(1));
    gtk_widget_show(button);

    /* separator */
    vbox_add_hsep(GTK_DIALOG(dialog)->vbox);

    /* bandwidth adjustment */

    hbox = gtk_hbox_new(FALSE, 5);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->vbox), 
		       hbox, TRUE, TRUE, 5);
    gtk_widget_show(hbox);

    tempwid = gtk_label_new(_("bandwidth adjustment factor:"));
    gtk_box_pack_start(GTK_BOX(hbox), tempwid, TRUE, TRUE, 5);
    gtk_widget_show(tempwid);

    tempwid = gtk_spin_button_new_with_range(0.25, 4.0, .05);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(tempwid), 1.0);
    g_signal_connect(G_OBJECT(tempwid), "value-changed", 
		     G_CALLBACK(bw_set), 
		     bw); 
    gtk_box_pack_start(GTK_BOX(hbox), tempwid, FALSE, FALSE, 5);
    gtk_widget_show(tempwid);

    /* "Cancel" button */
    cancel_options_button(GTK_DIALOG(dialog)->action_area, dialog, &ret);

    /* "OK" button */
    tempwid = ok_button(GTK_DIALOG(dialog)->action_area);
    g_signal_connect(G_OBJECT(tempwid), "clicked", 
		     G_CALLBACK(delete_widget), 
		     dialog);
    gtk_widget_grab_default (tempwid);
    gtk_widget_show(tempwid);

    /* "Help" button */
    context_help_button(GTK_DIALOG(dialog)->action_area, KERNEL_DENSITY);

    gtk_widget_show(dialog);

    return ret;
}

static void option_spin_set (GtkWidget *w, int *ivar)
{
    *ivar = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(w));
}

static GtkWidget *dialog_blurb_box (const char *text)
{
    GtkWidget *hbox;
    GtkWidget *label;

    hbox = gtk_hbox_new(FALSE, 5);
    label = gtk_label_new(text);
    gtk_widget_show(label);
    gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, FALSE, 5);

    return hbox;
}

static GtkWidget *option_spinbox (int *spinvar, const char *spintxt,
				  int spinmin, int spinmax,
				  int ci, gpointer p)
{
    GtkWidget *hbox;
    GtkWidget *label;
    GtkWidget *button;
    GtkObject *adj;
    int step = (ci == FREQ)? 2 : 1;

    hbox = gtk_hbox_new(FALSE, 5);

    label = gtk_label_new(spintxt);
    gtk_widget_show(label);
    gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, FALSE, 5);
    adj = gtk_adjustment_new(*spinvar, spinmin, spinmax, step, step, 0);
    button = gtk_spin_button_new(GTK_ADJUSTMENT(adj), 1, 0);
    gtk_widget_show(button);
    gtk_box_pack_start(GTK_BOX(hbox), button, FALSE, FALSE, 0);

    g_signal_connect(G_OBJECT(button), "value-changed",
		     G_CALLBACK(option_spin_set), spinvar);

    if (p != NULL) {
	GtkObject **pobj = (GtkObject **) p;

	*pobj = adj;
    }

    g_object_set_data(G_OBJECT(hbox), "spin-button", button);

    return hbox;
}

static void option_check_set (GtkWidget *w, int *checkvar)
{
    *checkvar = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w));
}

static GtkWidget *option_checkbox (int *checkvar, const char *checktxt)
{
    GtkWidget *hbox;
    GtkWidget *button;

    hbox = gtk_hbox_new(FALSE, 5);
    button = gtk_check_button_new_with_label(checktxt);
    gtk_box_pack_start(GTK_BOX(hbox), button, FALSE, FALSE, 5);
    gtk_widget_show(button);

    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(button), *checkvar);
    g_signal_connect(G_OBJECT(button), "toggled",
		     G_CALLBACK(option_check_set), checkvar);

    return hbox;
}

static void set_checks_opt (GtkWidget *w, int *active)
{
    int i = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(w), "optnum"));

    active[i] = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w));
}

static void trigger_ok (GtkWidget *w, GtkWidget *b)
{
    gtk_widget_activate(b);
}

/* general purpose dialog offering check-button options and/or
   a spinner with numerical values */

int checks_dialog (const char *title, const char *blurb,
		   const char **opts, int nopts,
		   int *active, int nradios, int *rvar, int *spinvar, 
		   const char *spintxt, int spinmin, int spinmax, 
		   int hcode)
{
    GtkWidget *dialog;
    GtkWidget *tmp, *okb;
    GtkWidget *button = NULL;
    GtkWidget *spin = NULL;
    int i, ret = 0;

    if (maybe_raise_dialog()) {
	return -1;
    }

    dialog = gretl_dialog_new(title, NULL, GRETL_DLG_BLOCK);

    /* create upper label if wanted */
    if (blurb != NULL) {
	tmp = dialog_blurb_box(blurb);
	gtk_widget_show(tmp);
	gtk_box_pack_start(GTK_BOX(GTK_DIALOG (dialog)->vbox), 
			   tmp, TRUE, TRUE, 5);
    }

    /* create spinner if wanted */
    if (spinvar != NULL) {
	tmp = option_spinbox(spinvar, spintxt, spinmin, spinmax, hcode, NULL);
	gtk_widget_show(tmp);
	gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->vbox), 
			   tmp, TRUE, TRUE, 5);
	spin = g_object_get_data(G_OBJECT(tmp), "spin-button");
    }

    /* create check buttons, if any */
    for (i=0; i<nopts; i++) {
	button = gtk_check_button_new_with_label(_(opts[i]));
	gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->vbox), 
			   button, TRUE, TRUE, 0);
	if (active[i] < 0) {
	    gtk_widget_set_sensitive(button, FALSE);
	} else {
	    if (active[i] > 0) {
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(button), TRUE);
	    }
	    g_signal_connect(G_OBJECT(button), "clicked",
			     G_CALLBACK(set_checks_opt), active);
	    g_object_set_data(G_OBJECT(button), "optnum", 
			      GINT_TO_POINTER(i));
	}
	gtk_widget_show(button);
    }

    /* create radio buttons, if any */
    for (i=0; i<nradios; i++) {
	int j = nopts + i;
	GSList *group;

	if (i == 0) {
	    group = NULL;
	    tmp = gtk_hseparator_new();
	    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->vbox), 
			       tmp, TRUE, TRUE, 5);
	    gtk_widget_show(tmp);
	} else {
	    group = gtk_radio_button_get_group(GTK_RADIO_BUTTON(button));
	}
	button = gtk_radio_button_new_with_label(group, _(opts[j]));
	gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->vbox), 
			   button, TRUE, TRUE, 0);
	g_signal_connect(G_OBJECT(button), "clicked",
			 G_CALLBACK(set_radio_opt), rvar);
	g_object_set_data(G_OBJECT(button), "action", 
			  GINT_TO_POINTER(i));
	gtk_widget_show(button);
    }

    /* Cancel button */
    cancel_options_button(GTK_DIALOG(dialog)->action_area, dialog, &ret);

    /* "OK" button */
    okb = ok_button(GTK_DIALOG(dialog)->action_area);
    g_signal_connect(G_OBJECT(okb), "clicked", 
		     G_CALLBACK(delete_widget), 
		     dialog);
    gtk_widget_grab_default(okb);
    gtk_widget_show(okb);
    if (spin != NULL) {
	g_signal_connect(G_OBJECT(spin), "activate",
			 G_CALLBACK(trigger_ok), okb);
    }

    /* Create a "Help" button if wanted */
    if (hcode && hcode != FREQ) {
	context_help_button(GTK_DIALOG(dialog)->action_area, hcode);
    }

    gtk_widget_show(dialog);

    return ret;
}

int spin_dialog (const char *title, const char *blurb,
		 int *spinvar, const char *spintxt, 
		 int spinmin, int spinmax, int hcode)
{
    return checks_dialog(title, blurb, NULL, 0, NULL, 0, NULL,
			 spinvar, spintxt,
			 spinmin, spinmax, hcode);
}

/* mechanism for adjusting properties of frequency plot */

struct freqdist_info {
    int *nbins;
    double *fmin;
    double *fwid;
    double xmin;
    double xmax;
    GtkWidget *spin[3];
};

static gboolean freq_info_set (GtkWidget *w, struct freqdist_info *f)
{
    int snum = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(w), "snum"));
    double val = gtk_spin_button_get_value(GTK_SPIN_BUTTON(w));

    if (snum == 0) {
	/* numbins */
	*f->nbins = (int) val;
    } else if (snum == 1) {
	/* minval */
	*f->fmin = val;
    } else {
	/* bin width */
	*f->fwid = val;
    }

    /* update complementary fields */

    if (snum == 0 && GTK_WIDGET_SENSITIVE(f->spin[0])) {
	*f->fwid = (f->xmax - f->xmin) / (*f->nbins - 1);
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(f->spin[2]), *f->fwid);
	*f->fmin = f->xmin - 0.5 * (*f->fwid);
	if (f->xmin >= 0.0 && *f->fmin < 0) {
	    *f->fmin = 0.0;
	}
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(f->spin[1]), *f->fmin);
    } else if (snum == 2 && GTK_WIDGET_SENSITIVE(f->spin[2])) {
	*f->nbins = ceil((f->xmax - *f->fmin) / *f->fwid);
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(f->spin[0]), *f->nbins);
    }

    return FALSE;
}

static void freq_set_dist (GtkWidget *w, int *dist)
{
    int fopt = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(w), "fopt"));

    if (GTK_TOGGLE_BUTTON(w)->active) {
	if (fopt == 0) *dist = D_NONE;
	else if (fopt == 1) *dist = D_NORMAL;
	else if (fopt == 2) *dist = D_GAMMA;
    }
}

static void freq_info_control (GtkWidget *w, struct freqdist_info *f)
{
    int snum = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(w), "snum"));

    if (GTK_TOGGLE_BUTTON(w)->active) {
	gtk_widget_set_sensitive(f->spin[0], snum == 0);
	gtk_widget_set_sensitive(f->spin[1], snum == 1);
	gtk_widget_set_sensitive(f->spin[2], snum == 1);
    }
}

static void revise_finfo (GtkWidget *w, struct freqdist_info *f)
{
    if (!GTK_WIDGET_SENSITIVE(f->spin[0])) {
	*f->nbins = 0;
    } else {
	*f->fmin = NADBL;
	*f->fwid = NADBL;
    }
}

int freq_dialog (const char *title, const char *blurb,
		 int *nbins, int nbmax, double *f0, double *fwid,
		 double xmin, double xmax, int *dist, int plot)
{
    const char *strs[] = {
	N_("Number of bins:"),
	N_("Minimum value, left bin:"),
	N_("Bin width:")
    };
    const char *plot_opts[] = {
	N_("Show data only"),
	N_("Show normal distribution"),
	N_("Show gamma distribution")
    };
    const char *dist_opts[] = {
	N_("Show data only"),
	N_("Test against normal distribution"),
	N_("Test against gamma distribution")
    };
    const char **opts;
    struct freqdist_info finfo;
    GtkWidget *dialog, *rad;
    GtkWidget *tmp, *okb, *tbl;
    GtkObject *adj;
    GSList *group = NULL;
    double f0min, f0max, f0step;
    double wmin, wmax, wstep;
    int i, imax, ret = 0;

    if (maybe_raise_dialog()) {
	return -1;
    }

    dialog = gretl_dialog_new(title, NULL, GRETL_DLG_BLOCK);

    finfo.nbins = nbins;
    finfo.fmin = f0;
    finfo.fwid = fwid;
    finfo.xmax = xmax;
    finfo.xmin = xmin;

    opts = (plot)? plot_opts : dist_opts;

    /* upper label */
    tmp = dialog_blurb_box(blurb);
    gtk_widget_show(tmp);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG (dialog)->vbox), 
		       tmp, TRUE, TRUE, 5);

    if (nbins == NULL) {
	goto dist_only;
    }

    tbl = gtk_table_new(3, 2, FALSE);
    gtk_table_set_col_spacings(GTK_TABLE(tbl), 5);
    gtk_table_set_row_spacings(GTK_TABLE(tbl), 5);

    *f0 = xmin - 0.5 * (*fwid);
    if (xmin >= 0.0 && *f0 < 0) {
	*f0 = 0.0;
    }

    f0min = xmin - 0.2 * (xmax - xmin);
    f0max = xmin - 0.01 * (*fwid);
    f0step = .001; 

    wmin = (xmax - xmin) / nbmax; 
    wmax = (xmax - xmin) / 3.0; 
    wstep = 0.001; 

    for (i=0; i<3; i++) {
	int dig = 3;

	if (i < 2) {
	    rad = gtk_radio_button_new_with_label(group, _(strs[i]));
	    group = gtk_radio_button_get_group(GTK_RADIO_BUTTON(rad));
	    gtk_table_attach_defaults(GTK_TABLE(tbl), rad, 0, 1, i, i+1);
	    g_object_set_data(G_OBJECT(rad), "snum", GINT_TO_POINTER(i));
	    g_signal_connect(G_OBJECT(rad), "clicked",
			     G_CALLBACK(freq_info_control), &finfo);
	} else {
	    tmp = gtk_label_new(_(strs[i]));
	    gtk_table_attach_defaults(GTK_TABLE(tbl), tmp, 0, 1, i, i+1);
	}

	if (i == 0) {
	    adj = gtk_adjustment_new(*nbins, 3, nbmax, 2, 2, 0);
	    dig = 0;
	} else if (i == 1) {
	    adj = gtk_adjustment_new(*f0, f0min, f0max, f0step, 10.0 * f0step, 0);
	} else {
	    adj = gtk_adjustment_new(*fwid, wmin, wmax, wstep, 10.0 * wstep, 0);
	}
	
	finfo.spin[i] = gtk_spin_button_new(GTK_ADJUSTMENT(adj), 1, dig);
	gtk_table_attach_defaults(GTK_TABLE(tbl), finfo.spin[i], 1, 2, i, i+1);
	g_object_set_data(G_OBJECT(finfo.spin[i]), "snum",
			  GINT_TO_POINTER(i));
	g_signal_connect(G_OBJECT(finfo.spin[i]), "value-changed",
			 G_CALLBACK(freq_info_set), &finfo);
	if (i > 0) {
	    gtk_widget_set_sensitive(finfo.spin[i], FALSE);
	}
    }

    gtk_container_add(GTK_CONTAINER(GTK_DIALOG(dialog)->vbox), tbl);
    gtk_widget_show_all(tbl);

    vbox_add_hsep(GTK_DIALOG(dialog)->vbox);
    group = NULL;

 dist_only:

    /* if var has negative values, don't show Gamma dist option */
    imax = (xmin < 0)? 2 : 3;

    for (i=0; i<imax; i++) {
	GtkWidget *hbox;

	hbox = gtk_hbox_new(FALSE, 5);
	rad = gtk_radio_button_new_with_label(group, _(opts[i]));
	group = gtk_radio_button_get_group(GTK_RADIO_BUTTON(rad));
	g_object_set_data(G_OBJECT(rad), "fopt", GINT_TO_POINTER(i));
	g_signal_connect(G_OBJECT(rad), "clicked",
			 G_CALLBACK(freq_set_dist), dist);
	gtk_container_add(GTK_CONTAINER(hbox), rad);
	gtk_container_add(GTK_CONTAINER(GTK_DIALOG(dialog)->vbox), hbox);
	gtk_widget_show_all(hbox);
    }

    /* Cancel button */
    cancel_options_button(GTK_DIALOG(dialog)->action_area, dialog, &ret);

    /* "OK" button */
    okb = ok_button(GTK_DIALOG(dialog)->action_area);
    if (nbins != NULL) {
	g_signal_connect(G_OBJECT(okb), "clicked", G_CALLBACK(revise_finfo), 
			 &finfo);
    }
    g_signal_connect(G_OBJECT(okb), "clicked", G_CALLBACK(delete_widget), 
		     dialog);
    gtk_widget_grab_default(okb);
    gtk_widget_show(okb);

    /* Help button */
    if (nbins != NULL) {
	context_help_button(GTK_DIALOG(dialog)->action_area, FREQ);
    }

    gtk_widget_show(dialog);

    return ret;
}

#ifdef G_OS_WIN32

/* MS Windows native "MessageBox" */

static void msgbox (const char *msg, int msgtype)
{
    gchar *trmsg = NULL;
    int nls_on = doing_nls();
    int utype;

    if (nls_on && !gretl_is_ascii(msg) && g_utf8_validate(msg, -1, NULL)) {
	/* recode messages in UTF-8, but don't try to recode messages
	   that are already in the locale encoding (strerror) 
	*/
	gsize bytes;

	trmsg = g_locale_from_utf8(msg, -1, NULL, &bytes, NULL);
    } 

    utype = (msgtype == GTK_MESSAGE_WARNING)? MB_ICONWARNING :
	(msgtype == GTK_MESSAGE_ERROR)? MB_ICONERROR :
	MB_ICONINFORMATION;

    MessageBox(NULL, (trmsg != NULL)? trmsg : msg, "gretl", 
	       MB_OK | utype);

    if (trmsg != NULL) {
	g_free(trmsg);
    }
}

#else /* not win32 */

static void msgbox (const char *msg, int msgtype)
{
    gchar *trmsg = NULL;
    GtkWidget *dialog;

    if (!g_utf8_validate(msg, -1, NULL)) {
	/* it's possible we have an OS string from strerror() that is
	   not UTF-8 encoded */
	gsize bytes;

	trmsg = g_locale_to_utf8(msg, -1, NULL, &bytes, NULL);
    }     

    dialog = gtk_message_dialog_new(NULL, /* GTK_WINDOW(mdata->w), */
				    GTK_DIALOG_DESTROY_WITH_PARENT,
				    msgtype,
				    GTK_BUTTONS_CLOSE,
				    (trmsg != NULL)? trmsg : msg);

    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);

    if (trmsg != NULL) {
	g_free(trmsg);
    }    
}

#endif

void errbox (const char *template, ...)
{
    char msg[MAXLEN];
    va_list args;

    if (template == NULL) {
	msgbox("Error", 1);
	return;
    }

    va_start(args, template);
    vsnprintf(msg, MAXLEN, template, args);
    va_end(args);

    msgbox(msg, GTK_MESSAGE_ERROR);
}

void infobox (const char *template, ...)
{
    char msg[MAXLEN];
    va_list args;

    va_start(args, template);
    vsnprintf(msg, MAXLEN, template, args);
    va_end(args);

    msgbox(msg, GTK_MESSAGE_INFO);
}

void warnbox (const char *template, ...)
{
    char msg[MAXLEN];
    va_list args;

    va_start(args, template);
    vsnprintf(msg, MAXLEN, template, args);
    va_end(args);

    msgbox(msg, GTK_MESSAGE_WARNING);
}

void file_read_errbox (const char *fname)
{
    const char *msg = gretl_errmsg_get();

    if (*msg != '\0') {
	errbox(msg);
    } else {
	gchar *uname = my_filename_to_utf8(fname);

	errbox(_("Couldn't open %s"), uname);
	g_free(uname);
    }
}

void file_write_errbox (const char *fname)
{
    const char *msg = gretl_errmsg_get();

    if (*msg != '\0') {
	errbox(msg);
    } else {
	gchar *uname = my_filename_to_utf8(fname);

	errbox(_("Couldn't write to %s"), uname);
	g_free(uname);
    }
}

/* --------------  Dataset structure "wizard" ---------------- */

#if (GTK_MAJOR_VERSION == 2 && GTK_MINOR_VERSION < 10)
# define USE_ASSISTANT 0
#else
# define USE_ASSISTANT 0 /* not ready yet */
#endif

#define DWDEBUG 1

#define PD_SPECIAL -1

#define known_panel(p) (p->structure == STACKED_CROSS_SECTION || \
                        p->structure == STACKED_TIME_SERIES)

enum {
    DW_SET_TYPE = 0,
    DW_TS_FREQUENCY,
    DW_WEEKLY_SELECT,
    DW_STARTING_OBS,
    DW_PANEL_MODE,
    DW_PANEL_SIZE,
    DW_PANEL_VARS,
    DW_CONFIRM,
    DW_DONE
};

static const char *wizcode_string (int code)
{
    const char *titles[] = {
	N_("Structure of dataset"),
	N_("Time series frequency"),
	N_("Daily date to represent week:"),
	N_("Starting observation:"),
	N_("Panel data organization"),
	N_("Number of cross-sectional units:"),
	N_("Panel index variables"),
	N_("Confirm dataset structure:")
    };

    if (code <= DW_CONFIRM) {
	return titles[code];
    } else {
	return "";
    }
}

enum {
    DW_FORWARD = 0,
    DW_BACK    = 1,
    DW_CANCEL = -1
};

static void dwinfo_init (DATAINFO *dwinfo)
{
    dwinfo->pd = datainfo->pd;
    dwinfo->structure = datainfo->structure;

    dwinfo->n = datainfo->n;
    strcpy(dwinfo->stobs, datainfo->stobs);
    strcpy(dwinfo->endobs, datainfo->endobs);
    dwinfo->sd0 = datainfo->sd0;

#if DWDEBUG
    fprintf(stderr, "dwinfo_init:\n"
	    " pd=%d, structure=%d, sd0=%g, stobs='%s', endobs='%s'\n",
	    dwinfo->pd, dwinfo->structure, dwinfo->sd0,
	    dwinfo->stobs, dwinfo->endobs);
#endif
}

/* for the case where the structure wizard is being used
   to create a new dataset */

static void prep_spreadsheet (GtkWidget *widget, dialog_t *dlg) 
{
    const gchar *buf;
    int t;

    buf = edit_dialog_get_text(dlg);

    if (buf == NULL || gui_validate_varname(buf, GRETL_TYPE_SERIES)) {
	return;
    }

    datainfo->varname[1][0] = 0;
    strncat(datainfo->varname[1], buf, VNAMELEN - 1);

    close_dialog(dlg);

    /* blank out the auto "index" variable */
    for (t=0; t<datainfo->n; t++) {
	Z[1][t] = NADBL;
    }
    *(VARLABEL(datainfo, 1)) = 0;

    show_spreadsheet(SHEET_NEW_DATASET);
}

static void maybe_start_editing (void)
{
    int cancel = 0;
    int resp;

    resp = yes_no_dialog(_("gretl: new dataset"), 
			 _("Do you want to start entering data values\n"
			 "using gretl's spreadsheet?"), 0);

    if (resp == GRETL_YES) {
	edit_dialog(_("gretl: name variable"), 
		    _("Enter name for first variable\n"
		      "(max. 15 characters)"),
		    NULL, prep_spreadsheet, NULL, 
		    CREATE_DATASET, VARCLICK_NONE, 
		    &cancel);
    } 

    if (resp == GRETL_NO || cancel) {
	/* accept the default blank dataset */
	register_data(NULLDATA_STARTED);
    }	
}

/* for balanced panel checking */

static int least_factor (int n)
{
    int flim = n;
    int prime = 1;
    int factor;
    
    if (n % 2 == 0) {
	return 2;
    }

    for (factor = 3; factor < flim; factor += 2) {
	if (n % factor == 0) {
	    prime = 0;
	    break;
	} else {
	    flim = n / factor;
	}
    }

    return (prime)? 1 : factor;
}

static int n_is_prime (void)
{
    int lf = least_factor(datainfo->n);

    return lf == 1;
}

static int test_for_unbalanced (const DATAINFO *dwinfo)
{
    int err = 0;

    if (datainfo->n % dwinfo->t1 != 0) {
	errbox(_("Panel datasets must be balanced.\n"
		 "The number of observations (%d) is not a multiple\n"
		 "of the number of %s (%d)."), datainfo->n,
	       _("units"), dwinfo->t1);
	err = 1;
    }

    return err;
}

static void maybe_unrestrict_dataset (void)
{
    if (complex_subsampled()) {
	maybe_free_full_dataset(datainfo);
	if (datainfo->t1 == 0 && 
	    datainfo->t2 == datainfo->n - 1) {
	    restore_sample_state(FALSE);
	}
    }
}

enum {
    DWIZ_CREATE   = 1 << 0,
    DWIZ_DROPMISS = 1 << 1
};

static int
datawiz_make_changes (DATAINFO *dwinfo, int flags)
{
    char setline[32];
    gretlopt opt = OPT_NONE;
    int create = (flags & DWIZ_CREATE);
    int delmiss = (flags & DWIZ_DROPMISS);
    int delete_markers = 0;
    int err = 0;

    /* preliminaries */
    if (dwinfo->structure == TIME_SERIES || 
	dwinfo->structure == SPECIAL_TIME_SERIES) {
	ntodate_full(dwinfo->stobs, dwinfo->t1, dwinfo);
    } else if (known_panel(dwinfo)) {
	if (test_for_unbalanced(dwinfo)) {
	    return 1;
	}
	if (!dataset_is_panel(datainfo)) {
	    /* Turning a subset of a non-panel dataset into a panel:
	       this change will be irreversible */
	    maybe_unrestrict_dataset();
	}
    }

    /* special: reorganizing dataset based on panel index vars */
    if (dwinfo->structure == PANEL_UNKNOWN) {
	err = set_panel_structure_from_vars(dwinfo->t1, dwinfo->t2,
					    Z, datainfo);
	goto finalize;
    }

    /* check for nothing to be done */
    if (dwinfo->structure == datainfo->structure &&
	dwinfo->pd == datainfo->pd &&
	strcmp(dwinfo->stobs, datainfo->stobs) == 0) {
	if (create || delmiss) {
	    goto finalize;
	} else {
	    infobox(_("No changes were made"));
	    return 0;
	}
    }

    /* if converting to time series, we probably don't want to
       retain any original observation-marker strings */
    if (dwinfo->structure == TIME_SERIES && 
	datainfo->markers && !delmiss) {
	delete_markers = 1;
    }

    /* handle panel structure */
    if (known_panel(dwinfo)) {
	int nunits = dwinfo->t1;
	int nperiods = datainfo->n / nunits;

	/* we don't offer a choice of "starting obs" */
	dwinfo->pd = (dwinfo->structure == STACKED_TIME_SERIES)? 
	    nperiods : nunits;
	strcpy(dwinfo->stobs, "1.1");
    } 

    /* handle conversion to cross-section */
    if (dwinfo->structure == CROSS_SECTION) {
	strcpy(dwinfo->stobs, "1");
    }

    sprintf(setline, "setobs %d %s", dwinfo->pd, dwinfo->stobs);

    if (dwinfo->structure == TIME_SERIES) {
	opt = OPT_T;
    } else if (dwinfo->structure == STACKED_TIME_SERIES) {
	opt = OPT_S;
    } else if (dwinfo->structure == STACKED_CROSS_SECTION) {
	opt = OPT_C;
    } else if (dwinfo->structure == CROSS_SECTION) {
	opt = OPT_X;
    } else if (dwinfo->structure == SPECIAL_TIME_SERIES) {
	opt = OPT_N;
    }

#if DWDEBUG
    fprintf(stderr, "setline = '%s', opt = %ld\n", setline, opt);
#endif

    err = set_obs(setline, Z, datainfo, opt);

#if DWDEBUG
    fprintf(stderr, "set_obs returned %d\n", err);
#endif

 finalize:

    if (!err && delmiss) {
	err = dataset_purge_missing_rows(Z, datainfo);
    }

    if (err) {
	errbox(gretl_errmsg_get());
    } else if (create) {
	if (datainfo->n < 1001) {
	    maybe_start_editing();
	} else {
	    register_data(NULLDATA_STARTED);
	}
    } else {
	if (delete_markers) {
	    dataset_destroy_obs_markers(datainfo);
	}
	mark_dataset_as_modified();
    }

#if DWDEBUG
    fprintf(stderr, "datawiz_make_changes: returning %d\n", err);
#endif

    return err;
}

#define TS_INFO_MAX 10
#define PANEL_INFO_MAX 3

struct freq_info {
    int pd;
    const char *label;
};

struct freq_info ts_info[] = {
    {  1, N_("Annual") },
    {  4, N_("Quarterly") },
    { 12, N_("Monthly") },
    { 52, N_("Weekly") },
    {  5, N_("Daily (5 days)") },
    {  6, N_("Daily (6 days)") },
    {  7, N_("Daily (7 days)") },
    { 24, N_("Hourly") },
    { 10, N_("Decennial") },
    { PD_SPECIAL, N_("Other") },
};

struct panel_info {
    int code;
    const char *label;
};

struct panel_info pan_info[] = {
    { STACKED_TIME_SERIES,   N_("Stacked time series") },
    { STACKED_CROSS_SECTION, N_("Stacked cross sections") },
    { PANEL_UNKNOWN,         N_("Use index variables") }
};

static int radio_default (DATAINFO *dwinfo, int code)
{
    int deflt = 1;

#if DWDEBUG
    fprintf(stderr, "radio_default: code=%d, dwinfo->pd=%d, dwinfo->structure=%d\n", 
	    code, dwinfo->pd, dwinfo->structure);
#endif

    if (code == DW_SET_TYPE) {
	deflt = dwinfo->structure;
    } else if (code == DW_TS_FREQUENCY) {
	if (dwinfo->structure == SPECIAL_TIME_SERIES) {
	    deflt = PD_SPECIAL;
	} else if (dwinfo->pd == 4 || dwinfo->pd == 5 || 
		   dwinfo->pd == 6 || dwinfo->pd == 7 ||
		   dwinfo->pd == 10 || dwinfo->pd == 12 ||
		   dwinfo->pd == 52) {
	    deflt = dwinfo->pd;
	} 
    } else if (code == DW_WEEKLY_SELECT) {
	deflt = dwinfo->v;
    } else if (code == DW_PANEL_MODE) { 
	deflt = dwinfo->structure;
    }

#if DWDEBUG
    fprintf(stderr, " returning deflt = %d\n", deflt);
#endif

    return deflt;
}

static int datawiz_i_to_setval (DATAINFO *dwinfo, int step, int i)
{
    int setval;

    if (step == DW_SET_TYPE && 
	dwinfo->structure == SPECIAL_TIME_SERIES &&
	i == TIME_SERIES) {
	setval = SPECIAL_TIME_SERIES;
    } else if (step == DW_TS_FREQUENCY) {
	setval = (i < TS_INFO_MAX)? ts_info[i].pd : 0;
    } else if (step == DW_PANEL_MODE) {
	setval = (i < PANEL_INFO_MAX)? pan_info[i].code : 0;
    } else {
	setval = i;
    }

    return setval;
}

static const char *datawiz_radio_strings (int wizcode, int i)
{
    if (wizcode == DW_SET_TYPE) {
	if (i == 0) return N_("Cross-sectional");
	if (i == 1) return N_("Time series");
	if (i == 2) return N_("Panel");
    } else if (wizcode == DW_WEEKLY_SELECT) {
	if (i == 0) return N_("Monday");
	if (i == 1) return N_("Tuesday");
	if (i == 2) return N_("Wednesday");
	if (i == 3) return N_("Thursday");
	if (i == 4) return N_("Friday");
	if (i == 5) return N_("Saturday");
	if (i == 6) return N_("Sunday");
	if (i == 7) return N_("None (don't use dates)");
    } else if (wizcode == DW_TS_FREQUENCY) {
	return ts_info[i].label;
    } else if (wizcode == DW_PANEL_MODE) {
	return pan_info[i].label;
    }

    return "";
}  

typedef struct dw_opts_ dw_opts;

struct dw_opts_ {
    int flags;
    int nopts;
    int deflt;
    int prime;
    int *setvar;
    int *extra;
    GtkWidget *spinner;
    GList *vlist;
};

static void datawiz_set_radio_opt (GtkWidget *w, dw_opts *opts)
{
    int val = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(w), "action"));

    if (opts->spinner != NULL) {
	if (val == PD_SPECIAL) {
	    GtkAdjustment *adj = 
		gtk_spin_button_get_adjustment(GTK_SPIN_BUTTON(opts->spinner));

	    gtk_widget_set_sensitive(opts->spinner, TRUE);
	    val = (int) adj->value;
	    if (opts->extra != NULL) {
		*opts->extra = SPECIAL_TIME_SERIES;
	    }
	} else {
	    gtk_widget_set_sensitive(opts->spinner, FALSE);
	    if (opts->extra != NULL) {
		*opts->extra = TIME_SERIES;
	    }
	}
    }

#if DWDEBUG
    fprintf(stderr, "datawiz_set_radio_opt: setting setvar to %d\n", val);
    if (opts->extra != NULL) {
	fprintf(stderr, "datawiz_set_radio_opt: extra now = %d\n", *opts->extra);
    }
#endif

    *opts->setvar = val;
}

struct ts_pd {
    int pd;
    const char *label;
};

static void make_confirmation_text (char *ctxt, DATAINFO *dwinfo, int *flags)
{
    struct ts_pd ok_pd[] = {
	{  1, N_("Annual") },
	{  4, N_("Quarterly") },
	{ 12, N_("Monthly") },
	{ 52, N_("Weekly") },
	{  5, N_("Daily") },
	{  6, N_("Daily") },
	{  7, N_("Daily") },
	{ 24, N_("Hourly") },
	{ 10, N_("Decennial") },
	{  0, NULL }
    };

    if (dwinfo->structure == CROSS_SECTION) {
	sprintf(ctxt, _("%s, observations 1 to %d"), _("cross-sectional data"), 
		datainfo->n);
    } else if (dwinfo->structure == TIME_SERIES || 
	       dwinfo->structure == SPECIAL_TIME_SERIES) {
	int lastobs = dwinfo->t1 + datainfo->n - 1;
	char stobs[OBSLEN];
	char endobs[OBSLEN];
	const char *tslabel = N_("time-series data");
	int i;

	if (dwinfo->structure == TIME_SERIES) {
	    for (i=0; ok_pd[i].pd != 0; i++) { 
		if (dwinfo->pd == ok_pd[i].pd) {
		    tslabel = _(ok_pd[i].label);
		    break;
		}
	    } 
	}

	if (lastobs > dwinfo->n - 1) {
	    dwinfo->n = lastobs + 1;
	}

	ntodate_full(stobs, dwinfo->t1, dwinfo);
	ntodate_full(endobs, lastobs, dwinfo);
	sprintf(ctxt, _("%s, %s to %s"), tslabel, stobs, endobs);
    } else if (dwinfo->structure == PANEL_UNKNOWN) {
	sprintf(ctxt, _("panel data (%s)\n"
			"%d cross-sectional units observed over %d periods"),
		_("stacked time series"), dwinfo->n, dwinfo->pd);
    } else if (known_panel(dwinfo)) {
	int nunits = dwinfo->t1;
	int nperiods = datainfo->n / nunits;

	sprintf(ctxt, _("panel data (%s)\n"
			"%d cross-sectional units observed over %d periods"),
		(dwinfo->structure == STACKED_TIME_SERIES)? 
		_("stacked time series") : _("stacked cross sections"),
		nunits, nperiods);
    } 

    if (*flags & DWIZ_DROPMISS) {
	strcat(ctxt, "\n");
	strcat(ctxt, _("(dropping missing observations)"));
    }
}

static void make_weekly_stobs (DATAINFO *dwinfo)
{
    int start_days[] = { 6, 7, 1, 2, 3, 4, 5 };

    sprintf(dwinfo->stobs, "1800/01/0%d", start_days[dwinfo->v]);    
}

static int default_start_decade (void)
{
    int d = 1700;

    if (datainfo->S != NULL) {
	d = positive_int_from_string(datainfo->S[0]);
    }
    
    if (d < 0) {
	d = 1700;
    }

    return d;
}

void compute_default_ts_info (DATAINFO *dwinfo, int newdata)
{
#if DWDEBUG
    char obsstr[OBSLEN];

    fprintf(stderr, "compute_ts_info() called: pd=%d, structure=%d\n",
	    dwinfo->pd, dwinfo->structure);
    if (dwinfo->pd == PD_SPECIAL) {
	fprintf(stderr, "breakage: pd = PD_SPECIAL\n");
    }
#endif

    if (dwinfo->pd < 0) {
	dwinfo->pd = 1;
    }
    
    if (dwinfo->structure == CROSS_SECTION) {
	dwinfo->n = 500;
	dwinfo->t1 = 0;
	strcpy(dwinfo->stobs, "1");
    } else if (dwinfo->structure == SPECIAL_TIME_SERIES) {
	dwinfo->n = 500;
	dwinfo->t1 = 0;
	if (dwinfo->pd > 1) {
	    int p = dwinfo->pd;

	    strcpy(dwinfo->stobs, "1:");
	    while ((p = p / 10) > 0) {
		strcat(dwinfo->stobs, "0");
	    }
	    strcat(dwinfo->stobs, "1");
	} else {
	    strcpy(dwinfo->stobs, "1");
	}
    } else if (dwinfo->pd == 1) {
	strcpy(dwinfo->stobs, "1700");
	dwinfo->n = 400;
	dwinfo->t1 = 250;
    } else if (dwinfo->pd == 10) {
	int dd = default_start_decade();

	sprintf(dwinfo->stobs, "%d", dd);
	if (dd > 1700) {
	    dwinfo->n = 30;
	    dwinfo->t1 = 0;
	} else {
	    dwinfo->n = 40;
	    dwinfo->t1 = 25;
	}
    } else if (dwinfo->pd == 4) {
	strcpy(dwinfo->stobs, "1700:1");
	dwinfo->n = 1300;
	dwinfo->t1 = 1000;
    } else if (dwinfo->pd == 12) {
	strcpy(dwinfo->stobs, "1700:01");
	dwinfo->n = 3900;
	dwinfo->t1 = 3360;
    } else if (dwinfo->pd == 24) {
	strcpy(dwinfo->stobs, "1:01");
	dwinfo->n = 1500;
	dwinfo->t1 = 0;
    } else if (dwinfo->pd == 52) {
	if (dwinfo->v >= 7) {
	    dwinfo->n = 500;
	    dwinfo->t1 = 0;
	    strcpy(dwinfo->stobs, "1");
	} else {
	    make_weekly_stobs(dwinfo);
	    dwinfo->n = 13000;
	    dwinfo->t1 = 7826;
	}
    } else if (dwinfo->pd == 5 ||
	       dwinfo->pd == 6 ||
	       dwinfo->pd == 7) {
	strcpy(dwinfo->stobs, "1900/01/01");
	dwinfo->n = 40000;
	if (dwinfo->pd == 5) {
	    dwinfo->t1 = 13046;
	} else if (dwinfo->pd == 6) {
	    dwinfo->t1 = 15654;
	} else {
	    dwinfo->t1 = 18263;
	}
    }

    dwinfo->sd0 = get_date_x(dwinfo->pd, dwinfo->stobs);

    if (newdata) {
	dwinfo->t2 = dwinfo->t1 + 49;
    } else if (datainfo->structure == TIME_SERIES && 
	       datainfo->pd == dwinfo->pd) {
	/* make the current start the default */
	dwinfo->t1 = dateton(datainfo->stobs, dwinfo);
    }

    ntodate_full(dwinfo->endobs, dwinfo->n - 1, dwinfo);

#if DWDEBUG
    ntodate_full(obsstr, dwinfo->t1, dwinfo);
    fprintf(stderr, "dwinfo: v=%d, pd=%d, stobs='%s', endobs='%s', sd0=%g, t1=%d (%s)\n",
	    dwinfo->v, dwinfo->pd, dwinfo->stobs, dwinfo->endobs, dwinfo->sd0, 
	    dwinfo->t1, obsstr);

    ntodate_full(obsstr, datainfo->t1, datainfo);
    fprintf(stderr, "datainfo: pd=%d, stobs='%s', sd0=%g, t1=%d (%s)\n",
	    datainfo->pd, datainfo->stobs, datainfo->sd0, datainfo->t1, obsstr);
#endif
}

int default_panel_size (DATAINFO *dwinfo, int smin)
{
    int sz = 1;

    if (dwinfo->pd > 1) {
	if (dwinfo->structure == STACKED_TIME_SERIES) {
	    sz = dwinfo->n / dwinfo->pd;
	} else {
	    sz = dwinfo->pd;
	}
    } else {
	sz = smin;
    }

    return sz;
}

static void dw_set_custom_frequency (GtkWidget *w, DATAINFO *dwinfo)
{
    dwinfo->pd = (int) GTK_ADJUSTMENT(w)->value;
#if DWDEBUG
    fprintf(stderr, "dw_set_custom_frequency: set dwinfo->pd = %d\n", dwinfo->pd);
#endif
}

static void dw_set_t1 (GtkWidget *w, DATAINFO *dwinfo)
{
    /* in case of panel data, "t1" is borrowed to represent the
       number of cross-sectional units
    */
    dwinfo->t1 = (int) GTK_ADJUSTMENT(w)->value;
#if DWDEBUG
    fprintf(stderr, "dw_set_t1: set 'dwinfo->t1' = %d\n", dwinfo->t1);
#endif
}

static int process_panel_vars (DATAINFO *dwinfo)
{
    int n = datainfo->n;
    double *uid = NULL;
    double *tid = NULL;
    int uv, tv;
    int nunits = 0;
    int nperiods = 0;
    int err = 0;

    /* FIXME sub-sampled dataset? */

    uv = dwinfo->t1;
    tv = dwinfo->t2;

    if (uv == tv) {
	errbox(_("The unit and time index variables must be distinct"));
	return E_DATA;
    }

    uid = copyvec(Z[uv], n);
    tid = copyvec(Z[tv], n);

    if (uid == NULL || tid == NULL) {
	nomem();
	err = E_ALLOC;
    }

    if (!err) {
	qsort(uid, n, sizeof *uid, gretl_compare_doubles);
	nunits = count_distinct_values(uid, n);

	qsort(tid, n, sizeof *tid, gretl_compare_doubles);
	nperiods = count_distinct_values(tid, n);

	if (nunits == 1 || nperiods == 1 || 
	    nunits == n || nperiods == n ||
	    n > nunits * nperiods) {
	    errbox(_("The selected index variables do not represent "
		     "a panel structure"));
	    err = E_DATA;
	} else {
	    dwinfo->n = nunits; 
	    dwinfo->pd = nperiods;
	}
    }

    free(uid);
    free(tid);

    return err;
}

/* try to assemble a list of at least two potential panel index
   variables: return NULL if this can't be done */

static GList *panelvars_list (void)
{
    GList *vlist = NULL;
    int i, t, ok;

    for (i=1; i<datainfo->v; i++) {
	ok = 1;
	for (t=datainfo->t1; t<=datainfo->t2; t++) {
	    if (na(Z[i][t]) || Z[i][t] < 0) {
		ok = 0;
		break;
	    }
	}
	if (ok) {
	   vlist = g_list_append(vlist, datainfo->varname[i]); 
	}
    }

    if (vlist != NULL && g_list_length(vlist) < 2) {
	g_list_free(vlist);
	vlist = NULL;
    }

#if DWDEBUG
    fprintf(stderr, "panelvars_list: returning %p\n", (void *) vlist);
#endif

    return vlist;
}

static gboolean update_panel_var (GtkWidget *box, DATAINFO *dwinfo)
{
    gchar *vname = gtk_combo_box_get_active_text(GTK_COMBO_BOX(box));
    int i = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(box), "index"));
    int v = series_index(datainfo, vname);

    /* note: borrowing t1, t2 here to record index var IDs ! */

    if (i == 0) {
	dwinfo->t1 = v;
    } else {
	dwinfo->t2 = v;
    }

#if DWDEBUG
    fprintf(stderr, "update_panel_var: 't1' = %d, 't2' = %d\n",
	    dwinfo->t1, dwinfo->t2);
#endif

    g_free(vname);

    return FALSE;
}

/* Try to find the most likely candidates for the unit (uidx) and time
   (tidx) index variables, given a list of variables that might
   perhaps be acceptable.
*/

static void panelvar_candidates (GList *vlist, int *uidx, int *tidx)
{
    GList *list = vlist;
    const char *vname;
    char vtest[VNAMELEN];
    int i;

    *uidx = *tidx = -1;

    for (i=0; list != NULL; i++) {
	vname = (const char *) list->data;
	strcpy(vtest, vname);
	lower(vtest);
	if (*tidx < 0) {
	    if (!strcmp(vtest, "time") || 
		!strcmp(vtest, "year") ||
		!strcmp(vtest, "period")) {
		*tidx = i;
	    }
	}
	if (*uidx < 0) {
	    if (!strcmp(vtest, "unit") ||
		!strcmp(vtest, "group") ||
		!strcmp(vtest, "country") ||
		!strcmp(vtest, "id")) {
		*uidx = i;
	    }
	}
	if (*uidx >= 0 && *tidx >= 0) {
	    break;
	}
	list = list->next;
    }

    /* if we didn't succeed above, just ensure a non-conflicting
       assignment to uidx and tidx */

    if (*uidx < 0) {
	if (*tidx < 0) {
	    *uidx = 0;
	    *tidx = 1;
	} else {
	    *uidx = (*tidx == 0)? 1 : 0;
	}
    } else if (*tidx < 0) {
	*tidx = (*uidx == 0)? 1 : 0;
    }
}

/* combo box selector for variables possibly representing the
   panel unit and time-period */

static GtkWidget *dwiz_combo (GList *vlist, DATAINFO *dwinfo)
{
    const char *strs[] = {
	N_("Unit or group index variable:"),
	N_("Time index variable:")
    };
    GtkWidget *w;
    GtkWidget *table;
    GtkWidget *combo;
    int uidx, tidx;
    int i;

    panelvar_candidates(vlist, &uidx, &tidx);

    if (uidx >= 0 && tidx >= 0) {
	/* borrowing! */
	dwinfo->t1 = uidx + 1;
	dwinfo->t2 = tidx + 1;
    }

#if DWDEBUG
    fprintf(stderr, "dwiz_combo: uidx = %d, tidx = %d\n",
	    uidx, tidx);
#endif

    table = gtk_table_new(2, 2, FALSE);
    gtk_table_set_col_spacings(GTK_TABLE(table), 5);
    gtk_table_set_row_spacings(GTK_TABLE(table), 5);

    for (i=0; i<2; i++) {
	GList *list = vlist;

	w = gtk_label_new(_(strs[i]));
	gtk_table_attach_defaults(GTK_TABLE(table), w, 0, 1, i, i+1);

	combo = gtk_combo_box_new_text();
	gtk_table_attach_defaults(GTK_TABLE(table), combo, 1, 2, i, i+1);

	while (list != NULL) {
	    gtk_combo_box_append_text(GTK_COMBO_BOX(combo), list->data);
	    list = list->next;
	}

	gtk_combo_box_set_active(GTK_COMBO_BOX(combo), (i == 0)? uidx : tidx);
	g_object_set_data(G_OBJECT(combo), "index", GINT_TO_POINTER(i));
	g_signal_connect(G_OBJECT(combo), "changed",
			 G_CALLBACK(update_panel_var), dwinfo);
    }

    return table;
}

static GtkWidget *dwiz_spinner (GtkWidget *hbox, DATAINFO *dwinfo, int step)
{
    GtkObject *adj;
    GtkWidget *label;
    GtkWidget *dwspin;
    int spinmin, spinmax, spinstart;

    if (step != DW_TS_FREQUENCY) {
	label = gtk_label_new(_(wizcode_string(step)));
	gtk_widget_show(label);
	gtk_box_pack_start(GTK_BOX(hbox), label, TRUE, FALSE, 0);
    }

    if (step == DW_STARTING_OBS && 
	(dwinfo->structure == TIME_SERIES || 
	 dwinfo->structure == SPECIAL_TIME_SERIES)) {
	compute_default_ts_info(dwinfo, 0);
	spinmin = 0;
	spinmax = dwinfo->n - 1;
	spinstart = dwinfo->t1;
    } else if (step == DW_TS_FREQUENCY) {
	spinmin = 1;
	spinmax = 100; /* arbitrary */
	spinstart = dwinfo->pd;
    } else if (step == DW_PANEL_SIZE) {
	spinmin = least_factor(datainfo->n);
	spinmax = datainfo->n / 2;
	spinstart = default_panel_size(dwinfo, spinmin);
    } else {
	/* should be impossible */
	return NULL;
    }

    /* appropriate step size? */
    adj = gtk_adjustment_new(spinstart, spinmin, spinmax,
			     1, 10, 0);
    if (step == DW_TS_FREQUENCY) {
	g_signal_connect(G_OBJECT(adj), "value-changed", 
			 G_CALLBACK(dw_set_custom_frequency), dwinfo);
    } else {
	g_signal_connect(G_OBJECT(adj), "value-changed", 
			 G_CALLBACK(dw_set_t1), dwinfo);
    }

    if (step == DW_STARTING_OBS) {
	dwspin = obs_button_new(GTK_ADJUSTMENT(adj), dwinfo);
    } else {
	dwspin = gtk_spin_button_new(GTK_ADJUSTMENT(adj), 1, 0);
    }

    gtk_entry_set_activates_default(GTK_ENTRY(dwspin), TRUE);

    gtk_box_pack_start(GTK_BOX(hbox), dwspin, (step != DW_TS_FREQUENCY), FALSE, 0);
    gtk_widget_show(dwspin);

    return dwspin;
}

static void set_purge_missobs (GtkWidget *w, int *flags)
{
    if (GTK_TOGGLE_BUTTON(w)->active) {
	*flags |= DWIZ_DROPMISS;
    } else {
	*flags &= ~DWIZ_DROPMISS;
    }    
}

static void maybe_add_missobs_purger (GtkWidget *vbox, int *flags)
{
    double missfrac = 0.0;
    int active = 0;

    if (*flags & DWIZ_DROPMISS) {
	active = 1;
    } else {
	missfrac = missing_obs_fraction((const double **) Z, 
					datainfo);
    }

    if (active || (missfrac > 0 && missfrac < 0.12)) {
	GtkWidget *hbox = gtk_hbox_new(FALSE, 5);
	GtkWidget *chk = gtk_check_button_new_with_label
	    (N_("purge missing observations"));

	gtk_box_pack_start(GTK_BOX(hbox), chk, FALSE, FALSE, 5);
	gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 5);
	if (active) {
	    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(chk), TRUE);
	}
	g_signal_connect(G_OBJECT(chk), "toggled", 
			 G_CALLBACK(set_purge_missobs), flags);
	gtk_widget_show_all(hbox);
    }
}

static void reactivate_main_menus (GtkWidget *w, gpointer p)
{
    main_menus_enable(TRUE);
}

static void set_up_dw_opts (dw_opts *opts, int step,
			    DATAINFO *dwinfo)
{
    opts->setvar = NULL;
    opts->extra = NULL;
    opts->spinner = NULL;
    opts->nopts = 0;
    opts->prime = 0;

    opts->deflt = radio_default(dwinfo, step);

    if (step == DW_SET_TYPE) {
	opts->nopts = 3;
	opts->setvar = &dwinfo->structure;
    } else if (step == DW_TS_FREQUENCY) {
	opts->nopts = TS_INFO_MAX;
	opts->setvar = &dwinfo->pd;
	opts->extra = &dwinfo->structure;
    } else if (step == DW_WEEKLY_SELECT) {
	opts->nopts = 8;
	opts->setvar = &dwinfo->v;
    } else if (step == DW_PANEL_MODE) {
	opts->nopts = PANEL_INFO_MAX;
	opts->setvar = &dwinfo->structure;
	opts->prime = n_is_prime();
    } else if (step == DW_PANEL_SIZE) {
	opts->setvar = &dwinfo->pd;
    } 
}

static void dwiz_build_radios (int step, DATAINFO *dwinfo, 
			       dw_opts *opts, 
			       GtkWidget *vbox)
{
    GSList *group = NULL;
    GtkWidget *button = NULL;
    int i, setval;

    for (i=0; i<opts->nopts; i++) {
	GtkWidget *hbox;

	setval = datawiz_i_to_setval(dwinfo, step, i);

#if DWDEBUG > 1
	fprintf(stderr, "opts[%d]: setval = %d (deflt=%d)\n", i, 
		setval, opts->deflt);
#endif

	hbox = gtk_hbox_new(FALSE, 5);
	gtk_box_pack_start(GTK_BOX(vbox), hbox, TRUE, TRUE, 0);
	gtk_widget_show(hbox);

	if (button != NULL) {
	    group = gtk_radio_button_get_group(GTK_RADIO_BUTTON(button));
	} else {
	    group = NULL;
	}

	button = gtk_radio_button_new_with_label(group, 
						 _(datawiz_radio_strings(step, i)));
	gtk_box_pack_start(GTK_BOX(hbox), button, FALSE, FALSE, 5);

	if (step == DW_TS_FREQUENCY && i == opts->nopts - 1) {
	    /* time series, "other" (custom) frequency: need spinner */
	    GtkWidget *freqspin = dwiz_spinner(hbox, dwinfo, step);

	    gtk_widget_set_sensitive(freqspin, FALSE);
	    opts->spinner = freqspin;
	} 

	g_signal_connect(G_OBJECT(button), "clicked",
			 G_CALLBACK(datawiz_set_radio_opt), opts);
	g_object_set_data(G_OBJECT(button), "action", GINT_TO_POINTER(setval));

	if (opts->prime) {
	    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(button), 
					 setval == PANEL_UNKNOWN);
	    gtk_widget_set_sensitive(button, setval == PANEL_UNKNOWN);
	} else if (opts->deflt == setval) {
#if DWDEBUG
	    fprintf(stderr, "opts[%d]: setval = deflt = %d\n", i, setval);
#endif
	    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(button), TRUE);
	    if (opts->setvar != NULL && setval >= 0) {
		*opts->setvar = setval;
#if DWDEBUG
		fprintf(stderr, "button: setting setvar to %d\n", setval);
#endif
	    }
	} 

	gtk_widget_show(button);
    }
}

static void dwiz_make_spinner (int step, DATAINFO *dwinfo,
			       GtkWidget *vbox)
{
    GtkWidget *hbox = gtk_hbox_new(FALSE, 5);

    dwiz_spinner(hbox, dwinfo, step);
    gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 5);
    gtk_widget_show(hbox);
}

static void dwiz_panelvars_selector (dw_opts *opts,
				     DATAINFO *dwinfo,
				     GtkWidget *vbox)
{
    GtkWidget *hbox = gtk_hbox_new(FALSE, 5);
    GtkWidget *table = dwiz_combo(opts->vlist, dwinfo);

    gtk_box_pack_start(GTK_BOX(hbox), table, FALSE, FALSE, 5);
    gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 5);
    gtk_widget_show_all(hbox);
}

#if !USE_ASSISTANT

static void set_back_code (GtkWidget *w, int *ret)
{
    *ret = DW_BACK;
}

static int datawiz_dialog (int step, DATAINFO *dwinfo, 
			   dw_opts *opts)
{
    GtkWidget *dialog;
    GtkWidget *w;
    GtkWidget *vbox;
    int ret = DW_FORWARD;

#if DWDEBUG
    fprintf(stderr, "\n*** datawiz_dialog: step = %d\n", step);
#endif

    if (step == DW_CONFIRM && known_panel(dwinfo) &&
	test_for_unbalanced(dwinfo)) {
	return DW_BACK;
    }

    if (step == DW_PANEL_VARS) {
	if (opt->vlist == NULL) {
	    opts->vlist = panelvars_list();
	}
	if (opts->vlist == NULL) {
	    errbox(_("The data set contains no suitable index variables"));
	    return DW_BACK;
	}
    }

    dialog = gretl_dialog_new(_("Data structure wizard"), NULL,
			      GRETL_DLG_BLOCK);
    g_signal_connect(G_OBJECT(dialog), "destroy", 
		     G_CALLBACK(reactivate_main_menus), NULL);
    vbox = GTK_DIALOG(dialog)->vbox;

    set_up_dw_opts(opts, step, dwinfo);

    /* top label */
    if (step != DW_STARTING_OBS && step != DW_PANEL_SIZE) {
	w = gtk_label_new(_(wizcode_string(step)));
	gtk_box_pack_start(GTK_BOX(vbox), w, TRUE, TRUE, 5);
	gtk_widget_show(w);
    }

    /* radio options? */
    if (opts->nopts > 0) {
	dwiz_build_radios(step, dwinfo, opts, vbox);
    }

    /* spinner to select starting obs (time series), or number of
       cross sectional units (panel)
    */
    if (step == DW_STARTING_OBS || step == DW_PANEL_SIZE) {
	dwiz_make_spinner(step, dwinfo, vbox);
    }

    /* At "starting obs" stage, if we have daily data with
       missing values, offer the option to remove the missing
       rows (and treat the data as effectively continuous)
    */
    if (step == DW_STARTING_OBS && Z != NULL &&
	dataset_is_daily(dwinfo)) {
	maybe_add_missobs_purger(vbox, &opts->flags);
    }

    /* panel: selectors for unit and time index variables? */
    if (step == DW_PANEL_VARS) {
	dwiz_panelvars_selector(opts, dwinfo, vbox);
    }	

    /* confirming? */
    if (step == DW_CONFIRM) {
	char ctxt[512];

	make_confirmation_text(ctxt, dwinfo, &opts->flags);
	w = gtk_label_new(ctxt);
	gtk_label_set_justify(GTK_LABEL(w), GTK_JUSTIFY_CENTER);
	gtk_box_pack_start(GTK_BOX(vbox), w, TRUE, TRUE, 5);
	gtk_widget_show(w);
    }  

    /* "Cancel" button */
    cancel_options_button(GTK_DIALOG(dialog)->action_area, dialog, &ret);

    /* Create a "Back" button? */
    if (step > DW_SET_TYPE) {
	w = back_button(GTK_DIALOG(dialog)->action_area);
	g_signal_connect(G_OBJECT(w), "clicked", 
			 G_CALLBACK(set_back_code), 
			 &ret);
	g_signal_connect(G_OBJECT(w), "clicked", 
			 G_CALLBACK(delete_widget), 
			 dialog);
	gtk_widget_show(w);  
    }  

    /* Create the "Next" or "OK" button */
    if (step == DW_CONFIRM) {
	w = ok_button(GTK_DIALOG(dialog)->action_area);
    } else {
	w = next_button(GTK_DIALOG(dialog)->action_area);
    }
    g_signal_connect(G_OBJECT(w), "clicked", 
		     G_CALLBACK(delete_widget), 
		     dialog);
    gtk_widget_grab_default(w);
    gtk_widget_show(w);

    if (step == DW_PANEL_MODE) {
	context_help_button(GTK_DIALOG(dialog)->action_area, PANEL_MODE);
    }

    main_menus_enable(FALSE);
    gtk_widget_show(dialog);

    return ret;
}

#endif

/* Take the user through a series of dialogs to define the
   structure of the data set.  If "create" is non-zero we're
   creating a new data set, otherwise we're restructuring an
   existing data set.

   If the wizard is being used to configure a new blank dataset,
   making it a panel is an option but there is no choice of "panel
   modes": it has to be stacked time series
*/

static int dw_compute_step (int step, int ret, DATAINFO *dwinfo, 
			    dw_opts *opts)
{
    int create = opts->flags & DWIZ_CREATE;

#if DWDEBUG
    fprintf(stderr, "dw_compute_step: incoming step = %d\n", step);
#endif

    switch (ret) {

    case DW_CANCEL:
	step = DW_DONE;
	break;

    case DW_FORWARD:
	if (step == DW_CONFIRM) {
	    step = DW_DONE;
	} else if (step == DW_SET_TYPE) {
	    if (dwinfo->structure == TIME_SERIES || 
		dwinfo->structure == SPECIAL_TIME_SERIES) {
		step = DW_TS_FREQUENCY;
	    } else if (dwinfo->structure == STACKED_TIME_SERIES ||
		       dwinfo->structure == STACKED_CROSS_SECTION) {
		if (create) {
		    dwinfo->structure = STACKED_TIME_SERIES;
		    step = DW_PANEL_SIZE;
		} else {
		    step = DW_PANEL_MODE;
		}
	    } else if (dwinfo->structure == PANEL_UNKNOWN) {
		step = DW_PANEL_MODE;
	    } else {
		dwinfo->pd = 1;
		step = DW_CONFIRM;
	    }		
	} else if (step == DW_TS_FREQUENCY) {
	    if (dwinfo->structure != SPECIAL_TIME_SERIES) {
		if (dwinfo->pd == 52) {
		    step = DW_WEEKLY_SELECT;
		} else {
		    step = DW_STARTING_OBS;
		}
	    } else {
		step = DW_STARTING_OBS;
	    }
	} else if (step == DW_WEEKLY_SELECT) {
	    step = DW_STARTING_OBS;
	} else if (step == DW_PANEL_MODE) {
	    if (dwinfo->structure == PANEL_UNKNOWN) {
		step = DW_PANEL_VARS;
	    } else {
		step = DW_PANEL_SIZE;
	    }
	} else if (step == DW_PANEL_VARS) {
	    if (process_panel_vars(dwinfo)) {
		step = DW_PANEL_VARS;
	    } else {
		step = DW_CONFIRM;
	    }
	} else if (step == DW_STARTING_OBS || step == DW_PANEL_SIZE) {
	    step = DW_CONFIRM;
	} 
	break;

    case DW_BACK:
	if (step == DW_TS_FREQUENCY || step == DW_PANEL_MODE) {
	    step = DW_SET_TYPE;
	} else if (step == DW_STARTING_OBS) {
	    if (dwinfo->pd == 52) {
		step = DW_WEEKLY_SELECT;
	    } else {
		step = DW_TS_FREQUENCY;
	    }
	} else if (step == DW_WEEKLY_SELECT) {
	    step = DW_TS_FREQUENCY;
	} else if (step == DW_PANEL_SIZE) {
	    step = (create)? DW_SET_TYPE : DW_PANEL_MODE;
	} else if (step == DW_PANEL_VARS) {
	    step = DW_PANEL_MODE;
	} else if (step == DW_CONFIRM) {
	    if (dwinfo->structure == TIME_SERIES || 
		dwinfo->structure == SPECIAL_TIME_SERIES) {
		step = DW_STARTING_OBS;
	    } else if (dwinfo->structure == STACKED_TIME_SERIES ||
		       dwinfo->structure == STACKED_CROSS_SECTION) {
		step = DW_PANEL_SIZE;
	    } else {
		step = DW_SET_TYPE;
	    }
	} 
	break;
    }

#if DWDEBUG
    fprintf(stderr, "dw_compute_step: returning step = %d\n", step);
#endif

    return step;
}

#if USE_ASSISTANT

static void clear_dwiz_page (GtkWidget *page)
{
    gtk_container_foreach(GTK_CONTAINER(page),
			  (GtkCallback) gtk_widget_destroy,
			  NULL);
}

static void wizard_prepare (GtkAssistant *wiz, GtkWidget *page, 
			    DATAINFO *dwinfo)
{
    int step = gtk_assistant_get_current_page(wiz);
    dw_opts *opts = g_object_get_data(G_OBJECT(wiz), "opts");
    int err = 0;
    
    fprintf(stderr, "Got Prepare, step = %d\n", step);

    if (step == DW_CONFIRM) {
	GtkWidget *w = g_object_get_data(G_OBJECT(page), "label");
	char ctxt[512];

	make_confirmation_text(ctxt, dwinfo, &opts->flags);
	gtk_label_set_text(GTK_LABEL(w), ctxt);
    } else {
	set_up_dw_opts(opts, step, dwinfo);
	clear_dwiz_page(page);

	if (opts->nopts > 0) {
	    dwiz_build_radios(step, dwinfo, opts, page);
	}

	if (step == DW_STARTING_OBS || step == DW_PANEL_SIZE) {
	    dwiz_make_spinner(step, dwinfo, page);
	} else if (step == DW_STARTING_OBS) {
	    if (Z != NULL && dataset_is_daily(dwinfo)) {
		maybe_add_missobs_purger(page, &opts->flags);
	    }
	} else if (step == DW_PANEL_VARS) {
	    if (opts->vlist == NULL) {
		opts->vlist = panelvars_list();
	    }
	    if (opts->vlist == NULL) {
		errbox(_("The data set contains no suitable index variables"));
		err = 1;
	    } else {
		dwiz_panelvars_selector(opts, dwinfo, page);
	    }
	}
    }

    gtk_assistant_set_page_complete(GTK_ASSISTANT(wiz),
				    page, !err);
}

static void dwiz_finalize (GtkAssistant *wiz, DATAINFO *dwinfo,
			   int cancel)
{
    dw_opts *opts = g_object_get_data(G_OBJECT(wiz), "opts");

    if (!cancel && !all_done) {
	datawiz_make_changes(dwinfo, opts->flags);
    } else if (cancel && !all_done && (opts->flags & DWIZ_CREATE)) {
	gui_clear_dataset();
    }

    gtk_widget_destroy(GTK_WIDGET(wiz));
}

static void wizard_cancel (GtkAssistant *wiz, DATAINFO *dwinfo)
{
    dwiz_finalize(wiz, dwinfo, 1);
}

static void wizard_apply (GtkAssistant *wiz, DATAINFO *dwinfo)
{
    fprintf(stderr, "Apply\n");
    dwiz_finalize(wiz, dwinfo, 0);
}

static gint dwiz_set_next_page (gint oldpage, GtkAssistant *wiz)
{
    DATAINFO *dwinfo = g_object_get_data(G_OBJECT(wiz), "dwinfo");
    dw_opts *opts = g_object_get_data(G_OBJECT(wiz), "opts");

    if (oldpage == DW_CONFIRM) {
	return 0;
    } else {
	return dw_compute_step(oldpage, DW_FORWARD, dwinfo, opts);
    }
}

static void dwiz_confirm_label (GtkWidget *page)
{
    GtkWidget *label = gtk_label_new("");

    gtk_label_set_justify(GTK_LABEL(label), GTK_JUSTIFY_CENTER);
    gtk_box_pack_start(GTK_BOX(page), label, TRUE, TRUE, 5);
    g_object_set_data(G_OBJECT(page), "label", label);
}

static void free_dwinfo (GtkWidget *w, DATAINFO *dwinfo)
{
    free(dwinfo);
}

static void free_dw_opts (GtkWidget *w, dw_opts *opts)
{
    if (opts->vlist != NULL) {
	g_list_free(opts->vlist);
    }
    free(opts);
}

static void data_structure_wizard (int create)
{
    GtkWidget *wiz;
    GtkWidget *page;
    DATAINFO *dwinfo;
    dw_opts *opts;
    int i;

    dwinfo = datainfo_new();
    if (dwinfo == NULL) {
	nomem();
	return;
    }

    opts = malloc(sizeof *opts);
    if (opts == NULL) {
	free(dwinfo);
	nomem();
	return;
    }

    opts->flags = (create)? DWIZ_CREATE : 0;
    opts->vlist = NULL;

    /* copy current relevant info */
    dwinfo_init(dwinfo);

    wiz = gtk_assistant_new();
    gtk_window_set_title(GTK_WINDOW(wiz), _("Data structure wizard"));

    g_object_set_data(G_OBJECT(wiz), "dwinfo", dwinfo);
    g_object_set_data(G_OBJECT(wiz), "opts", opts);

    g_signal_connect(G_OBJECT(wiz), "destroy", 
		     G_CALLBACK(free_dwinfo), 
		     dwinfo);
    g_signal_connect(G_OBJECT(wiz), "destroy", 
		     G_CALLBACK(free_dw_opts), 
		     opts);
    g_signal_connect(G_OBJECT(wiz), "destroy", 
		     G_CALLBACK(reactivate_main_menus), 
		     NULL);

    for (i=0; i<=DW_CONFIRM; i++) {
	GtkAssistantPageType ptype;
	const gchar *title;

	page = gtk_vbox_new(FALSE, 5);
	gtk_assistant_append_page(GTK_ASSISTANT(wiz), page);
	ptype = (i == 0)? GTK_ASSISTANT_PAGE_INTRO :
	    (i == DW_CONFIRM)? GTK_ASSISTANT_PAGE_CONFIRM :
	    (i == DW_PANEL_VARS)? GTK_ASSISTANT_PAGE_PROGRESS :
	    GTK_ASSISTANT_PAGE_CONTENT;
	gtk_assistant_set_page_type(GTK_ASSISTANT(wiz), page, ptype);
	title = _(wizcode_string(i));
	gtk_assistant_set_page_title(GTK_ASSISTANT(wiz), page, title);
	if (i == DW_CONFIRM) {
	    dwiz_confirm_label(page);
	}
    }

    gtk_assistant_set_forward_page_func(GTK_ASSISTANT(wiz),
					(GtkAssistantPageFunc) dwiz_set_next_page,
					wiz, NULL);

    g_signal_connect(GTK_ASSISTANT(wiz), "prepare",
		     G_CALLBACK(wizard_prepare),
		     dwinfo);
    g_signal_connect(GTK_ASSISTANT(wiz), "cancel",
		     G_CALLBACK(wizard_cancel),
		     dwinfo);
    g_signal_connect(GTK_ASSISTANT(wiz), "apply",
		     G_CALLBACK(wizard_apply),
		     dwinfo);

    main_menus_enable(FALSE);
    gtk_window_set_position(GTK_WINDOW(wiz), GTK_WIN_POS_MOUSE);
    gtk_widget_show_all(wiz);
}

#else

static void data_structure_wizard (int create)
{
    dw_opts opts;
    DATAINFO *dwinfo;
    int step = DW_SET_TYPE;
    int ret = DW_CANCEL;

    dwinfo = datainfo_new();
    if (dwinfo == NULL) {
	nomem();
	return;
    }

    opts.flags = (create)? DWIZ_CREATE : 0;
    opts.vlist = NULL;

    /* copy current relevant info */
    dwinfo_init(dwinfo);

    while (step != DW_DONE && !all_done) {
	ret = datawiz_dialog(step, dwinfo, &opts);
	step = dw_compute_step(step, ret, dwinfo, &opts);
    }

    if (ret != DW_CANCEL && !all_done) {
	datawiz_make_changes(dwinfo, opts.flags);
    }

    if (ret == DW_CANCEL && !all_done && create) {
	gui_clear_dataset();
    }

    free(dwinfo);
    if (opts.vlist != NULL) {
	g_list_free(opts.vlist);
    }
}

#endif

void data_structure_dialog (void)
{
    data_structure_wizard(0);
}

void new_data_structure_dialog (void)
{
    data_structure_wizard(1);    
}

struct lmax_opt {
    GtkWidget *dlg;
    GtkWidget *entry;
    double *lmax;
    double ymax;
};

static void lmax_opt_finalize (GtkWidget *w, struct lmax_opt *opt)
{
    const gchar *numstr;
    char *test;
    double x;

    numstr = gtk_entry_get_text(GTK_ENTRY(opt->entry));
    x = strtod(numstr, &test);

    if (*test != 0 || x <= opt->ymax) {
	errbox(_("The maximum must be greater than %g"), opt->ymax);
	*opt->lmax = NADBL;
    } else {
	*opt->lmax = x;
	gtk_widget_destroy(opt->dlg);
    }
}

static void lmax_opt_cancel (GtkWidget *w, struct lmax_opt *opt)
{
    *opt->lmax = 0.0;
    gtk_widget_destroy(opt->dlg);
}

void lmax_dialog (double *lmax, double ymax)
{
    GtkWidget *tmp, *hbox;
    GtkWidget *entry;
    gchar *numstr;
    struct lmax_opt opt;

    opt.dlg = gretl_dialog_new(_("Logistic model"), NULL, GRETL_DLG_BLOCK);
    opt.lmax = lmax;
    opt.ymax = ymax;

    /* label */
    hbox = gtk_hbox_new(FALSE, 5);
    tmp = gtk_label_new (_("Maximum (asymptote) for the\n"
			   "dependent variable"));
    gtk_label_set_justify(GTK_LABEL(tmp), GTK_JUSTIFY_CENTER);
    gtk_box_pack_start(GTK_BOX(hbox), tmp, TRUE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(opt.dlg)->vbox), 
		       hbox, FALSE, FALSE, 5);

    /* lmax entry */
    hbox = gtk_hbox_new(FALSE, 5);
    entry = gtk_entry_new();
    gtk_entry_set_width_chars(GTK_ENTRY(entry), 12);
    numstr = g_strdup_printf("%g", *lmax);
    gtk_entry_set_text(GTK_ENTRY(entry), numstr);
    g_free(numstr);
    gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);
    gtk_editable_select_region(GTK_EDITABLE(entry), 0, -1);
    gtk_box_pack_start(GTK_BOX(hbox), entry, TRUE, FALSE, 5);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(opt.dlg)->vbox), 
		       hbox, FALSE, FALSE, 5);
    opt.entry = entry;

    /* Cancel button */
    tmp = cancel_button(GTK_DIALOG(opt.dlg)->action_area);
    g_signal_connect(G_OBJECT (tmp), "clicked", 
		     G_CALLBACK(lmax_opt_cancel), &opt);

    /* "OK" button */
    tmp = ok_button(GTK_DIALOG(opt.dlg)->action_area);
    g_signal_connect(G_OBJECT(tmp), "clicked",
		     G_CALLBACK(lmax_opt_finalize), &opt);

    gtk_widget_show_all(opt.dlg);
}

/* apparatus for setting custom format for TeX tabular model output */

struct rbin {
    GtkWidget *b[2];
};

struct tex_formatter {
    GtkWidget *custom;
    GtkWidget *show[3];
    GtkObject *adj[4];
    GtkWidget *spin[4];
    struct rbin radio[4];
};

static void activate_row (GtkWidget *w, struct tex_formatter *tf)
{
    int i = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(w), "row"));
    int s = GTK_TOGGLE_BUTTON(w)->active;

    if (tf->spin[i] != NULL) {
	gtk_widget_set_sensitive(tf->spin[i], s);
	gtk_widget_set_sensitive(tf->radio[i].b[0], s);
	gtk_widget_set_sensitive(tf->radio[i].b[1], s);
    }
}

static void toggle_tex_custom (GtkWidget *w, struct tex_formatter *tf)
{
    int s = GTK_TOGGLE_BUTTON(w)->active;
    int i;

    if (tf->spin[0] != NULL) {
	for (i=0; i<4; i++) {
	    if (i < 3) {
		gtk_widget_set_sensitive(tf->show[i], s);
	    }
	    gtk_widget_set_sensitive(tf->spin[i], s);
	    gtk_widget_set_sensitive(tf->radio[i].b[0], s);
	    gtk_widget_set_sensitive(tf->radio[i].b[1], s);
	}
    }
}

static gboolean record_tex_format (GtkWidget *w, struct tex_formatter *tf)
{
    if (GTK_TOGGLE_BUTTON(tf->custom)->active) {
	char c, bit[8], fmt[32];
	int i, p;

	*fmt = '\0';

	for (i=0; i<4; i++) {
	    if (i == 0 || GTK_TOGGLE_BUTTON(tf->show[i-1])->active) {
		p = (int) gtk_spin_button_get_value(GTK_SPIN_BUTTON(tf->spin[i]));
		if (GTK_TOGGLE_BUTTON(tf->radio[i].b[1])->active) {
		    c = 'g';
		} else {
		    c = 'f';
		}
		sprintf(bit, "%%.%d%c", p, c);
		strcat(fmt, bit);
	    } 
	    if (i < 3) {
		strcat(fmt, "|");
	    }
	}
	set_tex_param_format(fmt);
    } else {
	/* chose standard (default) format */
	set_tex_param_format(NULL);
    }

    return FALSE;
}

static int get_tex_prec (const char *s)
{
    int p = 4;

    if (s != NULL) {
	s = strchr(s, '.');
	if (s != NULL) {
	    p = atoi(s + 1);
	}
    }

    return p;
}

static char get_tex_conv (const char *s)
{
    char c = 'f';
    int n = strlen(s);

    if (n > 1) {
	c = s[n - 1];
    }

    return c;
}

void tex_format_dialog (void)
{
    const char *labels[] = {
	N_("Coefficient"),
	N_("Standard error"),
	N_("t-ratio"),
	N_("p-value")
    };
    struct tex_formatter tf;
    GtkWidget *dlg, *tbl;
    GtkWidget *tmp, *hbox, *vbox; 
    GSList *group;
    int i, nset = 0;

    for (i=0; i<4; i++) {
	const char *fmt = tex_column_format(i);

	tf.spin[i] = NULL;
	if (*fmt) nset++;
    }

    dlg = gretl_dialog_new(_("gretl: TeX tabular format"), NULL,
			   GRETL_DLG_BLOCK);

    hbox = gtk_hbox_new(FALSE, 5);
    vbox = gtk_vbox_new(FALSE, 0);
    tmp = gtk_radio_button_new_with_label(NULL, _("Standard format"));
    gtk_box_pack_start(GTK_BOX(vbox), tmp, TRUE, TRUE, 5);
    group = gtk_radio_button_get_group(GTK_RADIO_BUTTON(tmp));
    tmp = tf.custom = gtk_radio_button_new_with_label(group, _("Custom format"));
    g_signal_connect(G_OBJECT(tmp), "clicked",
		     G_CALLBACK(toggle_tex_custom), &tf);

    gtk_box_pack_start(GTK_BOX(vbox), tmp, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(hbox), vbox, TRUE, TRUE, 5);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dlg)->vbox), hbox, TRUE, TRUE, 0);
    gtk_widget_show_all(hbox);

    vbox_add_hsep(GTK_DIALOG(dlg)->vbox);    
    
    tbl = gtk_table_new(11, 2, FALSE);
    gtk_table_set_row_spacings(GTK_TABLE(tbl), 5);
    gtk_table_set_col_spacings(GTK_TABLE(tbl), 5);

    for (i=0; i<4; i++) {
	const char *curr = tex_column_format(i);
	int p = get_tex_prec(curr);
	char c = get_tex_conv(curr);
	int shown = (i == 0 || curr[0] != 0 || nset == 0);
	int j = i * 3;

	hbox = gtk_hbox_new(FALSE, 5);
	tmp = gtk_label_new(_(labels[i]));
	gtk_box_pack_start(GTK_BOX(hbox), tmp, FALSE, FALSE, 5);
	gtk_table_attach_defaults(GTK_TABLE(tbl), hbox, 0, 2, j, j+1);

	/* "show" check button */
	if (i > 0) {
	    tmp = tf.show[i-1] = gtk_check_button_new();
	    gtk_table_attach_defaults(GTK_TABLE(tbl), tmp, 0, 1, j+1, j+2);
	    g_object_set_data(G_OBJECT(tmp), "row", GINT_TO_POINTER(i));
	    g_signal_connect(G_OBJECT(tmp), "clicked",
			     G_CALLBACK(activate_row), &tf);
	    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(tmp), shown);
	}

	/* spinner for precision */
	hbox = gtk_hbox_new(FALSE, 5);
	tmp = gtk_label_new(_("Show"));
	tf.adj[i] = gtk_adjustment_new(p, 0, 15, 1, 1, 0);
	tf.spin[i] = gtk_spin_button_new(GTK_ADJUSTMENT(tf.adj[i]), 1, 0);
	gtk_box_pack_start(GTK_BOX(hbox), tmp, FALSE, FALSE, 5);
	gtk_box_pack_start(GTK_BOX(hbox), tf.spin[i], FALSE, FALSE, 5);

	/* decimal places versus significant figures */
	vbox = gtk_vbox_new(FALSE, 0);
	tf.radio[i].b[0] = gtk_radio_button_new_with_label(NULL, _("decimal places"));
	gtk_box_pack_start(GTK_BOX(vbox), tf.radio[i].b[0], TRUE, TRUE, 5);
	group = gtk_radio_button_get_group(GTK_RADIO_BUTTON(tf.radio[i].b[0]));
	tf.radio[i].b[1] = gtk_radio_button_new_with_label(group, _("significant figures"));
	gtk_box_pack_start(GTK_BOX(vbox), tf.radio[i].b[1], TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(hbox), vbox, TRUE, TRUE, 5);

	if (c == 'g') {
	    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(tf.radio[i].b[1]), TRUE);
	}

	gtk_table_attach_defaults(GTK_TABLE(tbl), hbox, 1, 2, j+1, j+2);

	gtk_widget_set_sensitive(tf.spin[i], shown);
	gtk_widget_set_sensitive(tf.radio[i].b[0], shown);
	gtk_widget_set_sensitive(tf.radio[i].b[1], shown);

	if (i < 3) {
	    tmp = gtk_hseparator_new();
	    gtk_table_attach_defaults(GTK_TABLE(tbl), tmp, 0, 2, j+2, j+3);
	}
    }

    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dlg)->vbox), tbl, TRUE, TRUE, 0);
    gtk_widget_show_all(tbl);

    if (tex_using_custom_tabular()) {
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(tf.custom), TRUE);
    } else {
	toggle_tex_custom(tf.custom, &tf);
    }

    /* Cancel button */
    cancel_options_button(GTK_DIALOG(dlg)->action_area, dlg, NULL);
   
    /* OK button */
    tmp = ok_button(GTK_DIALOG(dlg)->action_area);
    g_signal_connect(G_OBJECT(tmp), "clicked",
		     G_CALLBACK(record_tex_format), &tf);
    g_signal_connect(G_OBJECT(tmp), "clicked",
		     G_CALLBACK(delete_widget), dlg);
    gtk_widget_grab_default(tmp);
    gtk_widget_show(tmp);

    gtk_widget_show(dlg);
}
