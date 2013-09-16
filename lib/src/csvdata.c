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

#include "libgretl.h"
#include "gretl_string_table.h"
#include "libset.h"
#include "usermat.h"
#include "uservar.h"
#include "genparse.h"
#include "csvdata.h"

#include <errno.h>

#define CDEBUG 0

#define QUOTE      '\''
#define CSVSTRLEN  72
#define NON_NUMERIC 1.0e99

enum {
    CSV_HAVEDATA = 1 << 0,
    CSV_GOTDELIM = 1 << 1,
    CSV_GOTTAB   = 1 << 2,
    CSV_GOTSEMI  = 1 << 3,
    CSV_BLANK1   = 1 << 4,
    CSV_OBS1     = 1 << 5,
    CSV_TRAIL    = 1 << 6,
    CSV_AUTONAME = 1 << 7,
    CSV_REVERSED = 1 << 8,
    CSV_DOTSUB   = 1 << 9,
    CSV_ALLCOLS  = 1 << 10,
    CSV_BOM      = 1 << 11
};

enum {
    JOIN_KEY,
    JOIN_VAL,
    JOIN_LHF,
    JOIN_RHF,
    JOIN_KEY2,
    JOIN_AUX,
    JOIN_MAXCOL
};

typedef struct csvjoin_ csvjoin;
typedef struct csvdata_ csvdata;

struct csvjoin_ {
    const char *colnames[JOIN_MAXCOL];
    char colnums[JOIN_MAXCOL];
    int *tcollist;
    csvdata *c;
};    

struct csvdata_ {
    int flags;
    char delim;
    char decpoint;
    int markerpd;
    int maxlen;
    int real_n;
    char *line;
    DATASET *dset;
    int ncols, nrows;
    char str[CSVSTRLEN];
    char skipstr[8];
    int *codelist;
    char *descrip;
    const char *user_na;
    gretl_string_table *st;
    int *cols_list;
    int *width_list;
    const gretl_matrix *rowmask;
    int masklen;
    csvjoin *jspec; /* info used for "join" command */
};

#define csv_has_trailing_comma(c) (c->flags & CSV_TRAIL)
#define csv_has_obs_column(c)     (c->flags & CSV_OBS1)
#define csv_has_blank_column(c)   (c->flags & CSV_BLANK1)
#define csv_got_tab(c)            (c->flags & CSV_GOTTAB)
#define csv_got_semi(c)           (c->flags & CSV_GOTSEMI)
#define csv_got_delim(c)          (c->flags & CSV_GOTDELIM)
#define csv_autoname(c)           (c->flags & CSV_AUTONAME)
#define csv_skip_column(c)        (c->flags & (CSV_OBS1 | CSV_BLANK1))
#define csv_have_data(c)          (c->flags & CSV_HAVEDATA)
#define csv_data_reversed(c)      (c->flags & CSV_REVERSED)
#define csv_do_dotsub(c)          (c->flags & CSV_DOTSUB)
#define csv_all_cols(c)           (c->flags & CSV_ALLCOLS)
#define csv_has_bom(c)            (c->flags & CSV_BOM)

#define csv_set_trailing_comma(c)   (c->flags |= CSV_TRAIL)
#define csv_unset_trailing_comma(c) (c->flags &= ~CSV_TRAIL)
#define csv_set_obs_column(c)       (c->flags |= CSV_OBS1)
#define csv_set_blank_column(c)     (c->flags |= CSV_BLANK1)
#define csv_set_got_tab(c)          (c->flags |= CSV_GOTTAB)
#define csv_set_got_semi(c)         (c->flags |= CSV_GOTSEMI)
#define csv_set_got_delim(c)        (c->flags |= CSV_GOTDELIM)
#define csv_set_autoname(c)         (c->flags |= CSV_AUTONAME)
#define csv_set_data_reversed(c)    (c->flags |= CSV_REVERSED)
#define csv_set_dotsub(c)           (c->flags |= CSV_DOTSUB)
#define csv_set_all_cols(c)         (c->flags |= CSV_ALLCOLS)
#define csv_set_has_bom(c)          (c->flags |= CSV_BOM)

#define csv_skip_bad(c)        (*c->skipstr != '\0')
#define csv_has_non_numeric(c) (c->st != NULL)

#define fixed_format(c) (c->cols_list != NULL && c->width_list != NULL)
#define cols_subset(c) (c->cols_list != NULL && c->width_list == NULL)
#define rows_subset(c) (c->rowmask != NULL)

#define joining(c) (c->jspec != NULL)

static int
time_series_label_check (DATASET *dset, int reversed, char *skipstr, PRN *prn);

static char import_na[8];

static void csvdata_free (csvdata *c)
{
    if (c == NULL) {
	return;
    }

    if (c->descrip != NULL) {
	free(c->descrip);
    }

    if (c->st != NULL) {
	gretl_string_table_destroy(c->st);
    }

    if (c->codelist != NULL) {
	free(c->codelist);
    }

    if (c->line != NULL) {
	free(c->line);
    }  

    if (c->cols_list != NULL) {
	free(c->cols_list);
	free(c->width_list);
    }  

    destroy_dataset(c->dset);

    free(c);
}

static csvdata *csvdata_new (DATASET *dset)
{
    csvdata *c = malloc(sizeof *c);

    if (c == NULL) {
	return NULL;
    }

    c->flags = 0;
    c->delim = '\t';
    c->markerpd = -1;
    c->maxlen = 0;
    c->real_n = 0;
    c->line = NULL;
    c->dset = NULL;
    c->ncols = 0;
    c->nrows = 0;
    *c->str = '\0';
    *c->skipstr = '\0';
    c->codelist = NULL;
    c->descrip = NULL;
    c->user_na = NULL;
    c->st = NULL;
    c->cols_list = NULL;
    c->width_list = NULL;
    c->rowmask = NULL;
    c->masklen = 0;

    if (strcmp(import_na, "default")) {
	c->user_na = import_na;
    }

    c->jspec = NULL;

    c->dset = datainfo_new();

    if (c->dset == NULL) {
	free(c);
	c = NULL;
    } else {
	c->delim = get_data_export_delimiter();
	c->decpoint = get_data_export_decpoint();
	if (dset->Z != NULL) {
	    c->flags |= CSV_HAVEDATA;
	}
#if CDEBUG
	fprintf(stderr, "csvdata_new: c->delim = '%c', c->decpoint = '%c'\n",
		c->delim, c->decpoint);
#endif
    }

    return c;
}

static int *cols_list_from_matrix (const char *s, int *err)
{
    gretl_matrix *m = get_matrix_by_name(s);
    int i, n = gretl_vector_get_length(m);
    int *list = NULL;

    if (n == 0) {
	*err = E_DATA;
    } else {
	list = gretl_list_new(n);
	if (list == NULL) {
	    *err = E_ALLOC;
	} else {
	    for (i=0; i<n; i++) {
		list[i+1] = gretl_vector_get(m, i);
	    }
	}
    }

    return list;
}

/* The interpretation of the "cols" specification depends on
   @opt: if this includes OPT_L then it should provide a 1-based 
   list of columns to be read; but if @opt includes OPT_F it 
   should provide a fixed-format spec, consisting of pairs 
   (start column, width).
*/

static int csvdata_add_cols_list (csvdata *c, const char *s,
				  gretlopt opt)
{
    int delimited = (opt & OPT_L);
    int *list, *clist = NULL, *wlist = NULL;
    int i, n, m = 0;
    int err = 0;

    if (gretl_is_matrix(s)) {
	list = cols_list_from_matrix(s, &err);
    } else {
	list = gretl_list_from_string(s, &err);
    }

    if (!err) {
	n = list[0];
	if (n == 0) {
	    err = E_DATA;
	} else if (delimited) {
	    m = n;
	    clist = list;
	} else {
	    /* fixed format: we need two lists */
	    if (n % 2 != 0) {
		err = E_DATA;
	    } else {
		m = n / 2;
		clist = gretl_list_new(m);
		wlist = gretl_list_new(m);
		if (clist == NULL || wlist == NULL) {
		    err = E_ALLOC;
		} else {
		    int j = 1;
	
		    for (i=1; i<=n; i+=2, j++) {
			clist[j] = list[i];
			wlist[j] = list[i+1];
		    }
		}
	    }
	}
    }

    /* clist = column (start) list: must be a set of increasing
       positive integers; and wlist = respective column widths,
       must all be positive, if present
    */

    for (i=1; i<=m && !err; i++) {
	if (clist[i] <= 0 || (i > 1 && clist[i] <= clist[i-1])) {
	    err = E_DATA;
	} else if (wlist != NULL && wlist[i] <= 0) {
	    err = E_DATA;
	} else if (wlist != NULL && wlist[i] >= CSVSTRLEN) {
	    fprintf(stderr, "Warning: field %d too wide (%d), truncating\n", 
		    i, wlist[i]);
	    wlist[i] = CSVSTRLEN - 1;
	}
    }

    if (list != clist) {
	free(list);
    }

    if (!err) {
	c->cols_list = clist;
	c->width_list = wlist;
    } else {
	free(clist);
	free(wlist);
	if (err == E_DATA) {
	    gretl_errmsg_set(_("Invalid column specification"));
	}
    }

    return err;
}

static int csvdata_add_row_mask (csvdata *c, const char *s)
{
    int err = 0;

    c->rowmask = get_matrix_by_name(s);
    if (c->rowmask == NULL) {
	gretl_errmsg_sprintf(_("'%s': no such matrix"), s);
	err = E_DATA;
    } else {
	c->masklen = gretl_vector_get_length(c->rowmask);
	if (c->masklen == 0) {
	    err = E_NONCONF;
	}
    }

    return err;
}

static int n_from_row_mask (csvdata *c)
{
    int i, n = 0;

    for (i=0; i<c->masklen && i<=c->nrows; i++) {
	if (gretl_vector_get(c->rowmask, i) != 0) {
	    n++;
	}
    }

    return n;
}

static int add_obs_marker (DATASET *dset, int n)
{
    char **S = realloc(dset->S, n * sizeof *S);
    int err = 0;

    if (S == NULL) {
	err = E_ALLOC;
    } else {
	dset->S = S;
	dset->S[n-1] = malloc(OBSLEN);
	if (dset->S[n-1] == NULL) {
	    err = E_ALLOC;
	} else {
	    strcpy(dset->S[n-1], "NA");
	}
    }

    return err;
}

static int add_single_obs (DATASET *dset)
{
    double *x;
    int i, err = 0;

    for (i=0; i<dset->v && !err; i++) {
	x = realloc(dset->Z[i], (dset->n + 1) * sizeof *x);
	if (x != NULL) {
	    dset->Z[i] = x;
	} else {
	    err = E_ALLOC;
	}
    }

    if (!err) {
	dset->n += 1;
	dset->Z[0][dset->n - 1] = 1.0;
	for (i=1; i<dset->v; i++) {
	    dset->Z[i][dset->n - 1] = NADBL;
	}
	if (dset->S != NULL) {
	    err = add_obs_marker(dset, dset->n);
	}
    }

    return err;
}

static int pad_weekly_data (DATASET *dset, int add)
{
    int oldn = dset->n;
    int ttarg, offset = 0, skip = 0;
    int i, s, t, tc, err;

    err = dataset_add_observations(dset, add, OPT_A); 

    if (!err) {
	for (t=0; t<oldn; t++) {
	    tc = calendar_obs_number(dset->S[t], dset) - offset;
	    if (tc != t) {
		skip = tc - t;
		fprintf(stderr, "Gap of size %d at original t = %d\n", skip, t);
		offset += skip;
		ttarg = oldn - 1 + offset;
		for (s=0; s<oldn-t+skip; s++) {
		    for (i=1; i<dset->v; i++) {
			if (s < oldn - t) {
			    if (s == 0 || s == oldn-t-1) {
				fprintf(stderr, "shifting obs %d to obs %d\n",
					ttarg-skip, ttarg);
			    }
			    dset->Z[i][ttarg] = dset->Z[i][ttarg - skip];
			} else {
			    fprintf(stderr, "inserting NA at obs %d\n", ttarg);
			    dset->Z[i][ttarg] = NADBL;
			}
		    }
		    ttarg--;
		}
	    }
	}
    }

    return err;
}

/* FIXME the following needs to be made more flexible? */

static int csv_weekly_data (DATASET *dset)
{
    char *lbl2 = dset->S[dset->n - 1];
    int ret = 1;
    int misscount = 0;
    int t, tc;

    for (t=0; t<dset->n; t++) {
	tc = calendar_obs_number(dset->S[t], dset) - misscount;
	if (tc != t) {
	    misscount += tc - t;
	}
    }

    if (misscount > 0) {
	double missfrac = (double) misscount / dset->n;

	fprintf(stderr, "nobs = %d, misscount = %d (%.2f%%)\n", 
		dset->n, misscount, 100.0 * missfrac);
	if (missfrac > 0.05) {
	    ret = 0;
	} else {
	    int Tc = calendar_obs_number(lbl2, dset) + 1;
	    int altmiss = Tc - dset->n;

	    fprintf(stderr, "check: Tc = %d, missing = %d\n", Tc, altmiss);
	    if (altmiss != misscount) {
		ret = 0;
	    } else if (dset->Z != NULL) {
		int err;

		fprintf(stderr, "OK, consistent\n");
		err = pad_weekly_data(dset, misscount);
		if (err) ret = 0;
	    } 
	} 
    }
    
    return ret;
}

#define DAY_DEBUG 1

static int check_daily_dates (DATASET *dset, int *pd, 
			      int *reversed, PRN *prn)
{
    int T = dset->n;
    char *lbl1 = dset->S[0];
    char *lbl2 = dset->S[T - 1];
    int fulln = 0, n, t, nbak;
    int alt_pd = 0;
    int oldpd = dset->pd;
    double oldsd0 = dset->sd0;
    long ed1, ed2;
    int nmiss = 0, err = 0;

    *pd = 0;
    
    ed1 = get_epoch_day(lbl1);
    if (ed1 < 0) {
	err = 1;
    }

#if DAY_DEBUG    
    fprintf(stderr, "S[0] = '%s', ed1 = %ld\n", lbl1, ed1);
#endif

    dset->pd = guess_daily_pd(dset);
    dset->structure = TIME_SERIES;

#if DAY_DEBUG    
    fprintf(stderr, "guessed daily pd = %d\n", dset->pd);
#endif

    if (!err) {
	ed2 = get_epoch_day(lbl2);
	if (ed2 < 0) {
	    err = 1;
	} else if (ed2 < ed1) {
#if DAY_DEBUG    
	    fprintf(stderr, "check_daily_dates: data are reversed?\n");
#endif
	    dset->sd0 = ed2;
	    *reversed = 1;
	} else {
	    dset->sd0 = ed1;
	}
    }

 recompute:

    alt_pd = 0;
    nbak = 0;

    if (!err) {
	int n1 = calendar_obs_number((*reversed)? lbl2 : lbl1, dset);
	int n2 = calendar_obs_number((*reversed)? lbl1 : lbl2, dset);

	fulln = n2 - n1 + 1;

	if (T > fulln) {
	    err = 1;
	} else {
	    nmiss = fulln - T;
	    pprintf(prn, A_("Observations: %d; days in sample: %d\n"), 
		    T, fulln);
	    if (nmiss > 300 * T) {
		pprintf(prn, A_("Probably annual data\n"));
		*pd = 1;
	    } else if (nmiss > 50 * T) {
		pprintf(prn, A_("Probably quarterly data\n"));
		*pd = 4;
	    } else if (nmiss > 20 * T) {
		pprintf(prn, A_("Probably monthly data\n"));
		*pd = 12;
	    } else if (nmiss > 3 * T) {
		pprintf(prn, A_("Probably weekly data\n"));
		*pd = dset->pd = 52;
	    } else {
		pprintf(prn, A_("Missing daily observations: %d\n"), nmiss);
	    }
	}
    }

    nbak = 0;

    for (t=0; t<dset->n && !err; t++) {
	int wd, s = (*reversed)? (dset->n - 1 - t) : t;

	wd = get_day_of_week(dset->S[s]);

	if (dset->pd == 5 && (wd == 6 || wd == 0)) {
	    /* Got Sat or Sun, can't be 5-day daily? */
	    alt_pd = (wd == 6)? 6 : 7;
	    pprintf(prn, "Found a Saturday (%s): re-trying with pd = %d\n", 
		    dset->S[s], alt_pd);
	    break;
	} else if (dset->pd == 6 && wd == 0) {
	    /* Got Sun, can't be 6-day daily? */
	    alt_pd = 7;
	    pprintf(prn, "Found a Sunday (%s): re-trying with pd = %d\n", 
		    dset->S[s], alt_pd);
	    break;
	}
	    
	n = calendar_obs_number(dset->S[s], dset);
	if (n < t) {
	    pprintf(prn, "Daily dates error at t = %d:\n"
		    "  calendar_obs_number() for '%s' = %d but t = %d\n", 
		    t, dset->S[t], n, t);
	    err = 1;
	} else if (n > fulln - 1) {
	    pprintf(prn, "Error: date '%s' out of bounds\n", dset->S[s]);
	    err = 1;
	} else if (nbak > 0 && n == nbak) {
	    pprintf(prn, "Error: date '%s' is repeated\n", dset->S[s]);
	    err = 1;
	}
	nbak = n;
    }

    if (alt_pd > 0) {
	dset->pd = alt_pd;
	goto recompute;
    }

    if (err) {
	dset->pd = oldpd;
	dset->sd0 = oldsd0;
	dset->structure = CROSS_SECTION;
    } else {
	strcpy(dset->stobs, (*reversed)? lbl2 : lbl1);
	strcpy(dset->endobs, (*reversed)? lbl1 : lbl2);
	dset->t2 = dset->n - 1;
	if (nmiss > 0 && *pd == 0) {
	    dset->markers = DAILY_DATE_STRINGS;
	}
    }

#if DAY_DEBUG
    fprintf(stderr, "check_daily_dates: pd = %d, reversed = %d, err = %d\n", 
	    dset->pd, *reversed, err);
#endif

    return (err)? -1 : dset->pd;
}

/* There's a special case (ugh!) where observation strings are
   given as in monthly data, but the frequency is in fact
   quarterly, as in:

   1947.06
   1947.09
   1947.12
   1948.03 

   we'll make a brave attempt to handle this.
*/ 

#define fakequarter(m) (m==3 || m==6 || m==9 || m==12) 

static int complete_qm_labels (DATASET *dset, int reversed,
			       char *skipstr, int *ppd, 
			       const char *fmt,
			       PRN *prn)
{
    char bad[16], skip[8];
    int t, s, yr, per, Ey, Ep;
    int pmin = 1;
    int pd, pd0;
    int ret = 1;

    pd = pd0 = *ppd;

 restart:

    s = (reversed)? (dset->n - 1) : 0;
    if (sscanf(dset->S[s], fmt, &yr, &per) != 2) {
	return 0;
    }

    for (t=1; t<dset->n; t++) {
	s = (reversed)? (dset->n - 1 - t) : t;
	Ey = (per == pd)? yr + 1 : yr;
	Ep = (per == pd)? pmin : per + pmin;
	if (sscanf(dset->S[s], fmt, &yr, &per) != 2) {
	    ret = 0;
	} else if (Ep == 1 && pd == pd0 && per == pd + 1 
		   && skipstr != NULL) {
	    *skip = *bad = '\0';
	    strncat(skip, dset->S[s] + 4, 7); 
	    strncat(bad, dset->S[s], OBSLEN-1); 
	    pd = pd0 + 1;
	    goto restart;
	} else if (per == Ep + 2 && pmin == 1 && fakequarter(per)) {
	    *bad = '\0';
	    strncat(bad, dset->S[s], OBSLEN-1); 
	    pmin = 3;
	    goto restart;
	} else if (yr != Ey || per != Ep) {
	    ret = 0;
	}
	if (!ret) {
	    pprintf(prn, "   %s: not a consistent date\n", 
		    dset->S[s]);
	    break;
	}
    }

    if (ret) {
	if (pmin == 3) {
	    pprintf(prn, "   \"%s\": quarterly data pretending to be monthly?\n", 
		    bad);
	    *ppd = 4;
	} else if (pd == pd0 + 1) {
	    pprintf(prn, "   \"%s\": BLS-type nonsense? Trying again\n", 
		    bad);
	    strcpy(skipstr, skip);
	}
    }

    return ret;
}

static int complete_year_labels (const DATASET *dset, int reversed)
{
    int s, t, yr, yrbak;
    int ret = 1;

    s = (reversed)? (dset->n - 1) : 0;
    yrbak = atoi(dset->S[s]);

    for (t=1; t<dset->n; t++) {
	s = (reversed)? (dset->n - 1 - t) : t;
	yr = atoi(dset->S[s]);
	if (yr != yrbak + 1) {
	    ret = 0;
	    break;
	}
	yrbak = yr;
    }

    return ret;
}

static int compress_daily (DATASET *dset, int pd)
{
    int t, yr, mon, day;

    for (t=0; t<dset->n; t++) {
	sscanf(dset->S[t], YMD_READ_FMT, &yr, &mon, &day);
	if (pd == 1) {
	    sprintf(dset->S[t], "%d", yr);
	} else if (pd == 12) {
	    sprintf(dset->S[t], "%d:%02d", yr, mon);
	} else if (pd == 4) {
	    sprintf(dset->S[t], "%d:%d", yr, mon / 3 + (mon % 3 != 0));
	} 
    }

    return 0;
}

enum date_orders {
    YYYYMMDD = 1,
    MMDDYYYY,
    DDMMYYYY
};

int get_date_order (int f0, int fn) 
{
    if (f0 > 31 || fn > 31) {
	/* first field must be year */
	return YYYYMMDD;
    } else if (f0 > 12 || fn > 12) {
	/* first field must be day */
	return DDMMYYYY;
    } else {
	/* could be wrong here */
	return MMDDYYYY;
    }
}

static void retransform_daily_dates (DATASET *dset)
{
    int t, y, m, d;

    /* we apparently guessed wrongly at MMDDYYYY, so
       put the dates back as they were for another try,
       at DDMMYYYY.
    */

    for (t=0; t<dset->n; t++) {
	sscanf(dset->S[t], YMD_READ_FMT, &y, &d, &m);
	sprintf(dset->S[t], YMD_WRITE_FMT, d, m, y);
    }
}

static int transform_daily_dates (DATASET *dset, int dorder)
{
    char *label;
    int t, yr, mon, day;
    char s1, s2;
    int n, err = 0;

    for (t=0; t<dset->n && !err; t++) {
	label = dset->S[t];
	if (dorder == YYYYMMDD) {
	    n = sscanf(label, "%d%c%d%c%d", &yr, &s1, &mon, &s2, &day);
	} else if (dorder == DDMMYYYY) {
	    n = sscanf(label, "%d%c%d%c%d", &day, &s1, &mon, &s2, &yr);
	} else {
	    n = sscanf(label, "%d%c%d%c%d", &mon, &s1, &day, &s2, &yr);
	}
	if (n == 5) {
	    sprintf(label, YMD_WRITE_Y2_FMT, yr, mon, day);
	} else {
	    err = 1;
	}
    }

    return err;
}

void reverse_data (DATASET *dset, PRN *prn)
{
    char tmp[OBSLEN];
    double x;
    int T = dset->n / 2;
    int i, t, s;

    pprintf(prn, A_("reversing the data!\n"));

    for (t=0; t<T; t++) {
	s = dset->n - 1 - t;
	for (i=1; i<dset->v; i++) {
	    x = dset->Z[i][t];
	    dset->Z[i][t] = dset->Z[i][s];
	    dset->Z[i][s] = x;
	}
	if (dset->S != NULL) {
	    strcpy(tmp, dset->S[t]);
	    strcpy(dset->S[t], dset->S[s]);
	    strcpy(dset->S[s], tmp);
	}
    }
}

static int csv_daily_date_check (DATASET *dset, int *reversed,
				 char *skipstr, PRN *prn)
{
    int d1[3], d2[3];
    char s1, s2;
    char *lbl1 = dset->S[0];
    char *lbl2 = dset->S[dset->n - 1];
    int dorder = 0;

    if (sscanf(lbl1, "%d%c%d%c%d", &d1[0], &s1, &d1[1], &s2, &d1[2]) == 5 &&
	sscanf(lbl2, "%d%c%d%c%d", &d2[0], &s1, &d2[1], &s2, &d2[2]) == 5 &&
	s1 == s2 && ispunct(s1)) {
	int mon1, day1;
	int mon2, day2;
	int pd, ret = 0;

	dorder = get_date_order(d1[0], d2[0]);

    tryagain:

	if (dorder == YYYYMMDD) {
	    pputs(prn, A_("Trying date order YYYYMMDD\n"));
	    mon1 = d1[1];
	    day1 = d1[2];
	    mon2 = d2[1];
	    day2 = d2[2];
	} else if (dorder == DDMMYYYY) {
	    pputs(prn, A_("Trying date order DDMMYYYY\n"));
	    day1 = d1[0];
	    mon1 = d1[1];
	    day2 = d2[0];
	    mon2 = d2[1];
	} else {
	    pputs(prn, A_("Trying date order MMDDYYYY\n"));
	    mon1 = d1[0];
	    day1 = d1[1];
	    mon2 = d2[0];
	    day2 = d2[1];
	}		
	    
	if (mon1 > 0 && mon1 < 13 &&
	    mon2 > 0 && mon2 < 13 && 
	    day1 > 0 && day1 < 32 &&
	    day2 > 0 && day2 < 32) {
	    /* looks promising for calendar dates */
	    if (dorder != YYYYMMDD || s1 != '/' || s2 != '/') {
		if (transform_daily_dates(dset, dorder)) {
		    return -1;
		}
	    }
	    pprintf(prn, A_("Could be %s - %s\n"), lbl1, lbl2);
	    ret = check_daily_dates(dset, &pd, reversed, prn);
	    if (ret >= 0 && pd > 0) {
		if (pd == 52) {
		    if (csv_weekly_data(dset)) {
			ret = 52;
		    } else if (dorder == MMDDYYYY) {
			/* maybe we guessed wrong */
			retransform_daily_dates(dset);
			dorder = DDMMYYYY;
			goto tryagain;
		    } else {
			ret = -1;
		    }
		} else {
		    compress_daily(dset, pd);
		    ret = time_series_label_check(dset, 
						  *reversed,
						  skipstr, 
						  prn);
		}
	    } 
	    return ret;
	}
    } else {
	pprintf(prn, A_("'%s' and '%s': couldn't get dates\n"), lbl1, lbl2);
    }

    return -1;
}

static int pd_from_date_label (const char *lbl, char *year, char *subp,
			       char *format, PRN *prn)
{
    const char *subchars = ".:QqMmPp-";
    int len = strlen(lbl);
    int try, pd = -1;

    *year = '\0';
    strncat(year, lbl, 4);
    try = atoi(year);

    if (try > 0 && try < 3000) {
	pprintf(prn, A_("   %s: probably a year... "), year);
    } else {
	pprintf(prn, A_("   %s: probably not a year\n"), year);
    }

    if (len == 5) {
	pputs(prn, A_("   but I can't make sense of the extra bit\n"));
    } else if (len == 4) {
	pputs(prn, A_("and just a year\n"));
	pd = 1;
    } else {
	char sep = lbl[4];
	char sub[3], *s = NULL;
	int p;

	if (strchr(subchars, sep)) {
	    *sub = '\0';
	    strncat(sub, lbl + 5, 2);
	    s = sub;
	    if (len == 6 || (len == 7 && (sep == 'q' || sep == 'Q'))) {
		if (len == 7) s++;
		p = atoi(s);
		if (p > 0 && p < 5) {
		    pprintf(prn, A_("quarter %s?\n"), s);
		    pd = 4;
		} else {
		    pprintf(prn, "quarter %d: not possible\n", p);
		}
	    } else if (len == 7) {
		p = atoi(s);
		if (p > 0 && p < 13) {
		    pprintf(prn, A_("month %s?\n"), s);
		    pd = 12;
		} else {
		    pprintf(prn, "month %d: not possible\n", p);
		}
	    }
	    strcpy(subp, s);
	    if (format != NULL && (pd == 4 || pd == 12)) {
		sprintf(format, "%%d%c%%d", sep);
	    }
	}
    }

    return pd;
}

static int time_series_label_check (DATASET *dset, int reversed,
				    char *skipstr, PRN *prn)
{
    char year[5], sub[3];
    char format[8] = {0};
    char *lbl1 = dset->S[0];
    char *lbl2 = dset->S[dset->n - 1];
    int pd = -1;

    pd = pd_from_date_label((reversed)? lbl2 : lbl1, year, sub, 
			    format, prn);

    if (pd == 1) {
	if (complete_year_labels(dset, reversed)) {
	    dset->pd = pd;
	    strcpy(dset->stobs, year);
	    dset->sd0 = atof(dset->stobs);
	    strcpy(dset->endobs, lbl2);
	} else {
	    pputs(prn, A_("   but the dates are not complete and consistent\n"));
	    pd = -1;
	}
    } else if (pd == 4 || pd == 12) {
	int savepd = pd;

	if (complete_qm_labels(dset, reversed, skipstr, &pd, format, prn)) {
	    dset->pd = pd;
	    if (savepd == 12 && pd == 4) {
		int s = atoi(sub) / 3;

		sprintf(dset->stobs, "%s:%d", year, s);
	    } else {
		sprintf(dset->stobs, "%s:%s", year, sub);
	    }
	    dset->sd0 = obs_str_to_double(dset->stobs);
	    ntodate(dset->endobs, dset->n - 1, dset);
	} else {
	    pputs(prn, A_("   but the dates are not complete and consistent\n"));
	    pd = -1;
	}
    }

    return pd;
}

static int dates_maybe_reversed (const char *s1, const char *s2,
				 PRN *prn)
{
    char d1[5], d2[5];
    int ret = 0;

    *d1 = *d2 = '\0';

    strncat(d1, s1, 4);
    strncat(d2, s2, 4);

    ret = atoi(d1) > atoi(d2);

    if (ret) {
	pputs(prn, A_("   dates are reversed?\n"));
    }
    
    return ret;
}

/* e.g. "M1 1957", "M12 2009", with spaces removed */

static int fix_IFS_data_labels (DATASET *dset)
{
    char *s1 = dset->S[0];
    char *s2 = dset->S[dset->n - 1];
    int ret = 0;

    if ((*s1 == 'M' || *s1 == 'Q') && *s2 == *s1) {
	const char *dig = "0123456789";
	int n1 = strlen(s1);
	int n2 = strlen(s2);

	if ((n1 == 6 || n1 == 7) && (n2 == 6 || n2 == 7) &&
	    strspn(s1 + 1, dig) == n1 - 1 &&
	    strspn(s2 + 1, dig) == n2 - 1) {
	    char sp[3], tmp[8], *s;
	    int pmax = (*s1 == 'M')? 12 : 4;
	    int y, p, pbak = 0;
	    int i, n, doit = 1;

	    for (i=0; i<dset->n; i++) {
		s = dset->S[i];
		if (*s != *s1) {
		    doit = 0;
		    break;
		}
		n = strlen(s);
		if (n != 6 && n != 7) {
		    doit = 0;
		    break;
		}
		if (strspn(s + 1, dig) != n - 1) {
		    doit = 0;
		    break;
		}
		y = atoi(s + n - 4);
		*sp = '\0';
		strncat(sp, s + 1, n - 5);
		p = atoi(sp);
		if (y < 1800 || y > 2500 || p <= 0 || p > pmax) {
		    doit = 0;
		    break;
		} 
		if (i > 0 && p != pbak + 1 && p != 1) {
		    doit = 0;
		    break;
		}		    
		pbak = p;
	    }

	    if (doit) {
		for (i=0; i<dset->n; i++) {
		    s = dset->S[i];
		    n = strlen(s);
		    y = atoi(s + n - 4);
		    *sp = '\0';
		    strncat(sp, s + 1, n - 5);
		    p = atoi(sp);
		    if (pmax == 12) {
			sprintf(tmp, "%d:%02d", y, p);
		    } else {
			sprintf(tmp, "%d:%d", y, p);
		    }
		    if (strlen(tmp) > strlen(s)) {
			free(s);
			dset->S[i] = gretl_strdup(tmp);
		    } else {
			strcpy(s, tmp);
		    }		    
		}
		ret = 1;
	    }
	}
    }

    return ret;
}

static int month_number (char *s)
{
    const char *mo[] = {
	"jan", "feb", "mar", "apr",
	"may", "jun", "jul", "aug",
	"sep", "oct", "nov", "dec"
    };
    int i;

    gretl_lower(s);

    for (i=0; i<12; i++) {
	if (!strcmp(s, mo[i])) {
	    return i+1;
	}
    }

    return 0;
}

/* e.g. "Jan-1980", for monthly or quarterly data */

static int fix_mon_year_labels (DATASET *dset)
{
    char *s1 = dset->S[0];
    char *s2 = dset->S[dset->n - 1];
    char m1[4] = {0};
    char m2[4] = {0};
    int yr1 = 0, yr2 = 0;
    int ret = 0;

    if (strlen(s1) == 8 && strlen(s2) == 8 &&
	s1[3] == '-' && s2[3] == '-') {
	yr1 = atoi(s1 + 4);
	yr2 = atoi(s2 + 4);
	strncat(m1, s1, 3);
	strncat(m2, s2, 3);
    }

    if (yr1 > 999 && yr1 < 3000 && yr2 > 999 && yr2 < 3000 &&
	month_number(m1) && month_number(m2)) {
	int i, p, pbak = 0;
	int dt, pd = 0;
	char *s;

	for (i=0; i<dset->n; i++) {
	    s = dset->S[i];
	    if (strlen(s) != 8 || s[3] != '-') {
		pd = 0;
		break;
	    }
	    yr1 = atoi(s + 4);
	    *m1 = '\0';
	    strncat(m1, s, 3);
	    if (yr1 < 1000 || yr1 >= 3000 || 
		(p = month_number(m1)) < 1) {
		pd = 0;
		break;
	    }
	    if (i > 0) {
		dt = p - pbak;
		if (dt != 1 && dt != 3 && p != 1) {
		    pd = 0;
		    break;
		}
		if (pd == 0 && dt > 0) {
		    pd = (dt == 1)? 12 : 4;
		}
	    }
	    pbak = p;
	}

	if (pd > 0) {
	    for (i=0; i<dset->n; i++) {
		s = dset->S[i];
		yr1 = atoi(s + 4);
		*m1 = '\0';
		strncat(m1, s, 3);
		p = month_number(m1);
		if (pd == 12) {
		    sprintf(dset->S[i], "%d:%02d", yr1, p);
		} else {
		    sprintf(dset->S[i], "%d:%g", yr1, ceil((3+p)/4.0));
		}
	    }
	    ret = 1;
	}
    }

    return ret;
}

/* Attempt to parse CSV row labels as dates.  Return -1 if this
   doesn't work out, or 0 if the labels seem to be just integer
   observation numbers, else return the inferred data frequency.
*/

int test_markers_for_dates (DATASET *dset, int *reversed, 
			    char *skipstr, PRN *prn)
{
    char endobs[OBSLEN];
    int n = dset->n;
    char *lbl1 = dset->S[0];
    char *lbl2 = dset->S[n - 1];
    int len1 = strlen(lbl1);
    int pd = -1;

    if (skipstr != NULL && *skipstr != '\0') {
	return time_series_label_check(dset, *reversed, skipstr, prn);
    }

    pprintf(prn, A_("   first row label \"%s\", last label \"%s\"\n"), 
	    lbl1, lbl2);

    /* are the labels (probably) just 1, 2, 3 etc.? */
    sprintf(endobs, "%d", n);
    if (!strcmp(lbl1, "1") && !strcmp(lbl2, endobs)) {
	return 0;
    }

    if (fix_IFS_data_labels(dset) || fix_mon_year_labels(dset)) {
	lbl1 = dset->S[0];
	lbl2 = dset->S[n - 1];
	len1 = strlen(lbl1);
    }	

    /* labels are of different lengths? */
    if (len1 != strlen(lbl2)) {
	return -1;
    }

    pputs(prn, A_("trying to parse row labels as dates...\n"));

    if (len1 == 8 || len1 == 10) {
	/* daily data? */
	pd = csv_daily_date_check(dset, reversed, skipstr, prn);
    } else if (len1 >= 4) {
	/* annual, quarterly, monthly? */
	if (isdigit((unsigned char) lbl1[0]) &&
	    isdigit((unsigned char) lbl1[1]) &&
	    isdigit((unsigned char) lbl1[2]) && 
	    isdigit((unsigned char) lbl1[3])) {
	    *reversed = dates_maybe_reversed(lbl1, lbl2, prn);
	    pd = time_series_label_check(dset, *reversed, skipstr, prn);
	} else {
	    pputs(prn, A_("   definitely not a four-digit year\n"));
	}
    }

    if (pd <= 0 && *reversed) {
	/* give up the "reversed" notion if we didn't get
	   a workable time-series interpretation */
	*reversed = 0;
    }

    return pd;
}

static int utf8_ok (FILE *fp, int pos)
{
    long mark = ftell(fp);
    int len = pos + 9;
    char *test = malloc(len + 1);
    int i, ret = 0;

    fseek(fp, mark - pos - 1, SEEK_SET);

    for (i=0; i<len; i++) {
	test[i] = fgetc(fp);
    }
    test[i] = '\0';

    if (g_utf8_validate(test, -1, NULL)) {
	ret = 1;
    } 

    free(test);

    fseek(fp, mark, SEEK_SET);

    return ret;
}

/* check for leading BOM (thanks a lot, Microsoft!) */

static void check_for_bom (FILE *fp, csvdata *c, PRN *prn)
{
    unsigned char buf[4];
    int n;

    n = fread(buf, 1, 4, fp);

    if (n == 4 && buf[0] == 0xEF && buf[1] == 0xBB && buf[2] == 0xBF) {
	pputs(prn, "file starts with BOM!\n");
	csv_set_has_bom(c);
	fseek(fp, 3, SEEK_SET); 
    } else {
	rewind(fp);
    }
}

/* The function below checks for the maximum line length in the given
   file.  It also checks for extraneous binary data (the file is 
   supposed to be plain text), and checks whether the 'delim'
   character is present in the file, on a non-comment line (where
   a comment line is one that starts with '#').  

   In addition, we check whether the file has a trailing comma on every
   line.
*/

static int csv_max_line_length (FILE *fp, csvdata *cdata, PRN *prn)
{
    int c, c1, cbak = 0, cc = 0;
    int comment = 0, maxlen = 0;
    int lines = 0;

    csv_set_trailing_comma(cdata); /* just provisionally */

    /* check for leading BOM (oh, Microsoft!) */
    check_for_bom(fp, cdata, prn);

    while ((c = fgetc(fp)) != EOF) {
	if (c == 0x0d) {
	    /* CR */
	    c1 = fgetc(fp);
	    if (c1 == EOF) {
		break;
	    } else if (c1 == 0x0a) {
		/* CR + LF -> LF */
		c = c1;
	    } else {
		/* Mac-style: CR not followed by LF */
		c = 0x0a;
		ungetc(c1, fp);
	    }
	}
	if (c == 0x0a) {
	    if (cc > maxlen) {
		maxlen = cc;
	    }
	    cc = 0;
	    if (cbak != 0 && cbak != ',') {
		csv_unset_trailing_comma(cdata);
	    }
	    lines++;
	    continue;
	}
	cbak = c;
	if (!isspace((unsigned char) c) && !isprint((unsigned char) c) &&
	    !(c == CTRLZ) && !utf8_ok(fp, cc)) {
	    pprintf(prn, A_("Binary data (%d) encountered (line %d:%d): "
			    "this is not a valid text file\n"), 
		    c, lines, cc);
	    return -1;
	}
	if (cc == 0) {
	    comment = (c == '#');
	}
	if (!comment) {
	    if (c == '\t') {
		/* let's ignore trailing tabs in this heuristic */
		c1 = fgetc(fp);
		if (c1 != 0x0d && c1 != 0x0a) {
		    csv_set_got_tab(cdata);
		}
		ungetc(c1, fp);
	    }
	    if (c == ';') {
		csv_set_got_semi(cdata);
	    }
	    if (c == cdata->delim) {
		csv_set_got_delim(cdata);
	    }
	}
	cc++;
    }

    if (maxlen == 0) {
	pprintf(prn, A_("Data file is empty\n"));
    } else if (csv_has_trailing_comma(cdata)) {
	pprintf(prn, A_("Data file has trailing commas\n"));
    }

    if (maxlen > 0) {
	/* allow for newline and null terminator */
	maxlen += 3;
    }

    return maxlen;
}

#define nonspace_delim(d) (d != ',' && d != ';')

static int count_csv_fields (const char *s, char delim)
{
    int cbak, nf = 0;

    if (*s == delim && *s == ' ') {
	s++;
    }

    while (*s) {
	if (*s == delim) {
	    nf++;
	}
	cbak = *s;
	s++;
	/* Problem: (when) should a trailing delimiter be read as an
	   implicit NA?  For now we'll so treat it if the delimiter
	   is not white space.
	*/
	if (*s == '\0' && cbak == delim && nonspace_delim(delim)) {
	    nf--;
	}
    }

    return nf + 1;
}

static void purge_quoted_commas (char *s)
{
    int inquote = 0;

    while (*s) {
	if (*s == '"') {
	    inquote = !inquote;
	} else if (inquote && *s == ',') {
	    *s = ' ';
	}
	s++;
    }
}

static void purge_unquoted_spaces (char *s)
{
    int inquote = 0;

    while (*s) {
	if (*s == '"') {
	    inquote = !inquote;
	} else if (!inquote && *s == ' ') {
	    shift_string_left(s, 1);
	}
	s++;
    }
}

static void compress_csv_line (csvdata *c)
{
    int n = strlen(c->line);
    char *p = c->line + n - 1;

    if (*p == '\n') {
	*p = '\0';
	p--;
    }

    if (*p == '\r') *p = '\0';

    if (c->delim == ',') {
	purge_quoted_commas(c->line);
    }

    if (c->delim != ' ') {
	/* 2012-07-14: was gretl_delchar(' ', c->line); */
	purge_unquoted_spaces(c->line);
    } else {
	compress_spaces(c->line);
    }

    gretl_delchar('"', c->line);

    if (csv_has_trailing_comma(c)) {
	/* chop trailing comma */
	n = strlen(c->line);
	if (n > 0) {
	    c->line[n-1] = '\0';
	}
    }
}

int import_obs_label (const char *s)
{
    char tmp[VNAMELEN];

    if (s == NULL) {
	return 1;
    }

    if (*s == '"' || *s == '\'') s++;

    if (*s == '\0') {
	return 1;
    }

    if (strlen(s) > VNAMELEN - 1) {
	return 0;
    }

    *tmp = '\0';
    strncat(tmp, s, VNAMELEN - 1);
    gretl_lower(tmp);

    return (!strcmp(tmp, "obs") ||
	    !strcmp(tmp, "date") || 
	    !strcmp(tmp, "year") || 
	    !strcmp(tmp, "period") ||
	    !strcmp(tmp, "observation") ||
	    !strcmp(tmp, "observation_date"));
}

static int join_wants_col_zero (csvdata *c, const char *s)
{
    const char *colname;
    int i;

    if (*s == '\0') {
	return 0;
    }

    for (i=0; i<JOIN_MAXCOL; i++) {
	colname = c->jspec->colnames[i];
	if (colname != NULL && !strcmp(s, colname)) {
	    return 1;
	}
    }

    return 0;
}

static void check_first_field (const char *line, csvdata *c, PRN *prn)
{
    if (c->delim != ' ' && *line == c->delim) {
	csv_set_blank_column(c);
    } else {
	char field1[OBSLEN];
	int i = 0;

	if (c->delim == ' ' && *line == ' ') line++;

	while (*line && i < sizeof field1) {
	    if (*line == c->delim) break;
	    field1[i++] = *line++;
	}
	field1[i] = '\0';
	iso_to_ascii(field1);

	if (joining(c) && join_wants_col_zero(c, field1)) {
	    return;
	} else if (csv_all_cols(c)) {
	    /* open/append wants all columns as data */
	    return;
	}

	pprintf(prn, A_("   first field: '%s'\n"), field1);

	if (import_obs_label(field1)) {
	    pputs(prn, A_("   seems to be observation label\n"));
	    csv_set_obs_column(c);
	}
    }
}

void import_na_init (void)
{
    const char *s = get_csv_na_read_string();

    strcpy(import_na, s);
}

/* Returns 1 if the string @s should be counted representing
   an NA or missing value, 0 otherwise. If there is a user-set 
   "csv_read_na" value this is used for comparison, otherwise
   a set of default values is consulted.
*/

int import_na_string (const char *s)
{
    if (strcmp(import_na, "default")) {
	return !strcmp(s, import_na);
    } else {
	const char *defaults[] = {
	    "NA",
	    "N.A.",
	    "n.a.",
	    "na",
	    "N/A",
	    "#N/A",
	    "NaN",
	    ".NaN",
	    ".",
	    "..",
	    "-999",
	    "-9999",
	    NULL
	};
	int i;

	for (i=0; defaults[i] != NULL; i++) {
	    if (!strcmp(s, defaults[i])) {
		return 1;
	    }
	}
    }

    return 0;
}

static int csv_missval (const char *str, int i, int t, 
			int *miss_shown, PRN *prn)
{
    int miss = 0;

    if (*str == '\0') {
	if (t < 80 || *miss_shown < i) {
	    pprintf(prn, A_("   the cell for variable %d, obs %d "
			    "is empty: treating as missing value\n"), 
		    i, t);
	    *miss_shown += 1;
	}
	miss = 1;
    }

    if (import_na_string(str)) {
	if (t < 80 || *miss_shown < i) {
	    pprintf(prn, A_("   warning: missing value for variable "
			    "%d, obs %d\n"), i, t);
	    *miss_shown += 1;
	}
	miss = 1;
    }

    return miss;
}

static int non_numeric_check (csvdata *c, PRN *prn)
{
    int *list = NULL;
    int i, j, t, nn = 0;
    int err = 0;

#if CDEBUG > 1
    fprintf(stderr, "non_numeric_check: testing %d series\n", c->dset->v - 1);
#endif

    for (i=1; i<c->dset->v; i++) {
	if (series_get_flags(c->dset, i) & VAR_TIMECOL) {
	    /* we'll treat all "time columns" as string-valued */
	    for (t=0; t<c->dset->n; t++) {
		if (!na(c->dset->Z[i][t])) {
		    c->dset->Z[i][t] = NON_NUMERIC;
		}
	    }
	    nn++;
	    break;
	}
	for (t=0; t<c->dset->n; t++) {
	    if (c->dset->Z[i][t] == NON_NUMERIC) {
		nn++;
		break;
	    }
	}
    }

    if (nn > 0) {
	list = gretl_list_new(nn);
	if (list == NULL) {
	    err = E_ALLOC;
	}
    }

    if (list != NULL) {
	j = 1;
	for (i=1; i<c->dset->v; i++) {
	    for (t=0; t<c->dset->n; t++) {
		if (c->dset->Z[i][t] == NON_NUMERIC) {
		    list[j++] = i;
		    break;
		}
	    }
	}

#if CDEBUG > 1
	printlist(list, "non-numeric vars list");
#endif

	for (i=1; i<=list[0]; i++) {
	    double nnfrac;
	    int nnon = 0;
	    int nok = 0;
	    int tn = 0;
	    int v = list[i];

	    series_set_flag(c->dset, v, VAR_DISCRETE);

	    for (t=0; t<c->dset->n; t++) {
		if (c->dset->Z[v][t] == NON_NUMERIC) {
		    if (!tn) tn = t + 1;
		    nnon++;
		} else if (!na(c->dset->Z[v][t])) {
		    nok++;
		}
	    }

	    nnfrac = (nok == 0)? 1.0 : (double) nnon / (nnon + nok);
	    pprintf(prn, "variable %d (%s): non-numeric values = %d "
		    "(%.2f percent)\n", v, c->dset->varname[v], 
		    nnon, 100 * nnfrac);
	    if (nnon < 2 || nnfrac < 0.01) {
		pprintf(prn, A_("ERROR: variable %d (%s), observation %d, "
				"non-numeric value\n"), 
			v, c->dset->varname[v], tn);
		err = E_DATA;
	    }
	}

	if (!err) {
	    pputs(prn, "allocating string table\n");
	    c->st = gretl_string_table_new(list);
	    if (c->st == NULL) {
		err = E_ALLOC;
		free(list);
	    } else {
		c->codelist = list;
	    }
	}
    }

    return err;
}

static double csv_atof (csvdata *c, const char *s)
{
    double x = NON_NUMERIC;
    char *test;

    errno = 0;

    if (c->decpoint == '.' || !csv_do_dotsub(c) || strchr(s, ',') == NULL) {
	/* either we're currently set to the correct locale,
	   or there's no problematic decimal point in @s
	*/
	x = strtod(s, &test);
	if (*test == '\0' && errno == 0) {
	    return x;
	} else {
	    x = NON_NUMERIC;
	}
    } else if (csv_do_dotsub(c) && strlen(s) <= 31) {
	/* substitute dot for comma */
	char tmp[32];

	strcpy(tmp, s);
	gretl_charsub(tmp, ',', '.');

	x = strtod(tmp, &test);
	if (*test == '\0' && errno == 0) {
	    return x;
	} else {
	    x = NON_NUMERIC;
	}
    }

    if (c->decpoint == '.' && strchr(s, ',') != NULL && strlen(s) <= 31) {
	/* try remediation for decimal comma? */
	char tmp[32];

	strcpy(tmp, s);
	gretl_charsub(tmp, ',', '.');
	errno = 0;

	x = strtod(tmp, &test);
	if (*test != '\0' || errno != 0) {
	    x = NON_NUMERIC;
	} 
    }

    return x;
}

static int process_csv_obs (csvdata *c, int i, int t, int *miss_shown,
			    PRN *prn)
{
    int err = 0;

    if (c->st != NULL) {
	/* second round, handling string-valued variables */
	if (in_gretl_list(c->codelist, i)) {
	    double zit = c->dset->Z[i][t];
	    int ix;

	    if (na(zit) && *c->str != '\0' && c->user_na == NULL) {
		/* by default (no user_na) only blanks count as NAs */
		zit = NON_NUMERIC;
	    }
	    if (!na(zit)) {
		ix = gretl_string_table_index(c->st, c->str, i, 0, prn);
		if (ix > 0) {
		    c->dset->Z[i][t] = (double) ix;
		} else {
		    err = E_DATA;
		}
	    }
	}
    } else if (csv_missval(c->str, i, t+1, miss_shown, prn)) {
	c->dset->Z[i][t] = NADBL;
    } else {
	c->dset->Z[i][t] = csv_atof(c, gretl_strstrip(c->str));
    } 

    return err;
}

/* wrapper for fgets(), designed to handle any sort of line
   termination (unix, DOS, Mac or an unholy mixture)
*/

static char *csv_fgets (char *s, int n, FILE *fp)
{
    int i, c1, c = 0;

    for (i=0; i<n-1 && c!=0x0a; i++) {
	c = fgetc(fp);
	if (c == EOF) {
	    if (i == 0) {
		/* signal end of read */
		return NULL;
	    } else {
		break;
	    }
	} else if (c == 0x0d) {
	    /* CR: convert to LF and peek at next char: if it's
	       LF swallow it, otherwise put it back */
	    c = 0x0a;
	    c1 = fgetc(fp);
	    if (c1 != 0x0a) {
		ungetc(c1, fp);
	    }
	}
	s[i] = c;
    }

    s[i] = '\0';

    return s;
}

/* pick up any comments following the data block in a CSV file */

static char *get_csv_descrip (char *line, int n, FILE *fp)
{
    char *desc = NULL;
    size_t len;

    while (csv_fgets(line, n, fp)) {
	tailstrip(line);
	if (desc == NULL) {
	    len = strlen(line) + 3;
	    desc = malloc(len);
	    if (desc == NULL) {
		return NULL;
	    }
	    sprintf(desc, "%s\n", line);
	} else {
	    char *tmp;

	    len = strlen(desc) + strlen(line) + 3;
	    tmp = realloc(desc, len);
	    if (tmp == NULL) {
		free(desc);
		return NULL;
	    }
	    desc = tmp;
	    strcat(desc, line);
	    strcat(desc, "\n");
	}
    }

    if (desc != NULL && string_is_blank(desc)) {
	free(desc);
	desc = NULL;
    }

    return desc;
}

static const char *
csv_msg = N_("\nPlease note:\n"
	     "- The first row of the CSV file should contain the "
	     "names of the variables.\n"
	     "- The first column may optionally contain date "
	     "strings or other 'markers':\n  in that case its row 1 entry "
	     "should be blank, or should say 'obs' or 'date'.\n"
	     "- The remainder of the file must be a rectangular "
	     "array of data.\n");

static int csv_fields_check (FILE *fp, csvdata *c, PRN *prn)
{
    int gotdata = 0;
    int chkcols = 0;
    int err = 0;

    c->ncols = c->nrows = 0;

    if (csv_has_bom(c)) {
	fseek(fp, 3, SEEK_SET);
    }

    while (csv_fgets(c->line, c->maxlen, fp) && !err) {

	/* skip comment lines */
	if (*c->line == '#') {
	    continue;
	}

	/* skip blank lines -- but finish if the blank comes after data */
	if (string_is_blank(c->line)) {
	    if (gotdata) {
		if (!csv_have_data(c)) {
		    c->descrip = get_csv_descrip(c->line, c->maxlen, fp);
		}
		break;
	    } else {
		continue;
	    }
	}
	
	c->nrows += 1;

	if (fixed_format(c)) {
	    tailstrip(c->line);
	    gotdata = 1;
	    chkcols = strlen(c->line);
	    if (chkcols < c->cols_list[c->cols_list[0]]) {
		gretl_errmsg_set(_("Invalid column specification"));
		err = E_DATA;
		break;
	    } else {
		continue;
	    }
	}

	compress_csv_line(c);

	if (!gotdata) {
	    /* scrutinize first "real" line */
	    check_first_field(c->line, c, prn);
	    gotdata = 1;
	} 

	chkcols = count_csv_fields(c->line, c->delim);
	if (c->ncols == 0) {
	    c->ncols = chkcols;
	    pprintf(prn, A_("   number of columns = %d\n"), c->ncols);	    
	} else if (chkcols != c->ncols) {
	    pprintf(prn, A_("   ...but row %d has %d fields: aborting\n"),
		    c->nrows, chkcols);
	    err = E_DATA;
	} else if (cols_subset(c)) {
	    int datacols = csv_skip_column(c) ? (c->ncols - 1) : c->ncols;

	    if (c->cols_list[c->cols_list[0]] > datacols) {
		gretl_errmsg_set(_("Invalid column specification"));
		err = E_DATA;
	    }
	}		
    }

    if (!err && fixed_format(c)) {
	c->ncols = c->cols_list[0];
    }

    return err;
}

static void strip_illegals (char *s)
{
    int i;

    if (*s == '\0') return;

    for (i=1; s[i]!='\0'; i++) {
	if (!isalnum(s[i])) {
	    s[i] = '_';
	}
    }
}

static int csv_reconfigure_for_markers (DATASET *dset)
{
    int err = dataset_allocate_obs_markers(dset);

    if (!err) {
	err = dataset_drop_last_variables(dset, 1);
    }

    return err;
}

static int skip_data_column (csvdata *c, int k)
{
    int col = csv_skip_column(c) ? k : k + 1;

    if (!in_gretl_list(c->cols_list, col)) {
	return 1;
    } else {
	return 0;
    }
}

#if 0 /* not ready yet */

void normalize_colname (char *targ, const char *src, int k)
{
    const char *letters = "abcdefghijklmnopqrstuvwxyz"
	"ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    int i = 0;

    /* skip any leading non-letters */
    src += strcspn(src, letters);

    while (*src && i < VNAMELEN - 1) {
	if (strspn(src, letters) > 0 || isdigit(*src) || *src == '_') {
	    /* transcribe valid characters */
	    targ[i++] = *src;
	} else if (*src == ' ') {
	    /* convert space to underscore */
	    if (i > 0 && targ[i-1] == '_') {
		; /* skip */
	    } else {
		targ[i++] = '_';
	    }
	}
	src++;
    }

    if (i > 0) {
	targ[i] = '\0';
    } else {
	sprintf(targ, "col%d", k);
    }
}

#endif

static int update_join_cols_list (csvdata *c, int k)
{
    int *test;
    int err = 0;

    test = gretl_list_append_term(&c->cols_list, k);
    if (test == NULL) {
	err = E_ALLOC;
    }

#if CDEBUG
    printlist(c->cols_list, "c->cols_list for join");
#endif

    return err;
}

/* handle_join_varname: the index @k contains the column number
   relative to the entire CSV file, while @pj points to j, the column
   number relative to the reduced dataset that will be constructed by
   selection of columns from file.

   Here we're examining a column heading read from file (c->str) to
   see whether it matches any of the column-names required for an
   ongoing join operation (held in c->jspec->colnames). If so, we
   write the index j into the appropriate slot in c->jspec->colnums
   (which starts off filled with zeros), so the joiner will know where
   to find the required data. (The j value is bound to be at least 1
   since column 0 is reserved to the constant.)

   In some cases a given named column may perform a double role in a
   join operation -- for example, it may serve as an element in a
   filter and also as the auxiliary variable in an "aggregation"
   method. To allow for this we don't stop scanning at the first match
   of c->str with a required column name.

   The call to update_join_cols_list() uses the index @k to record the
   overall column position of "wanted data", for use by the CSV
   reader.
*/

static int handle_join_varname (csvdata *c, int k, int *pj)
{
    const char *colname;
    int matched = 0;
    int i, j = *pj;

#if CDEBUG
    fprintf(stderr, "join_varname: looking at '%s'\n", c->str);
#endif

    if (!csv_skip_column(c)) {
	k++;
    }

    for (i=0; i<JOIN_MAXCOL; i++) {
	/* find "wanted name" i */
	colname = c->jspec->colnames[i];
	if (colname == NULL || c->jspec->colnums[i] > 0) {
	    /* name not wanted, or already found */
	    continue;
	}
	if (!strcmp(c->str, colname)) {
#if CDEBUG
	    fprintf(stderr, " target %d matched at CSV col %d, j=%d\n", i, k, j); 
#endif
	    c->jspec->colnums[i] = j;
	    if (!matched) {
		matched = 1;
		c->dset->varname[j][0] = '\0';
		strncat(c->dset->varname[j], c->str, VNAMELEN - 1);
		update_join_cols_list(c, k);
		*pj += 1;
		if (in_gretl_list(c->jspec->tcollist, k)) {
		    series_set_flag(c->dset, j, VAR_TIMECOL);
		}
	    }
	}
    }

    return 0;
}

#define starts_number(c) (isdigit((unsigned char) c) || c == '-' || \
                          c == '+' || c == '.')

#define obs_labels_no_varnames(o,c,n)  (!o && c->v > 3 && n == c->v - 2)

static int csv_varname_scan (csvdata *c, FILE *fp, PRN *prn, PRN *mprn)
{
    char *p;
    int obscol = csv_has_obs_column(c);
    int i, j, k, numcount;
    int err = 0;

    pputs(mprn, A_("scanning for variable names...\n"));

    if (csv_has_bom(c)) {
	fseek(fp, 3, SEEK_SET);
    }

    while (csv_fgets(c->line, c->maxlen, fp)) {
	if (*c->line == '#' || string_is_blank(c->line)) {
	    continue;
	} else {
	    break;
	}
    }

    compress_csv_line(c);   

    p = c->line;
    if (c->delim == ' ' && *p == ' ') p++;
    iso_to_ascii(p);
    if (strlen(p) > 118) {
	pprintf(mprn, A_("   line: %.115s...\n"), p);
    } else {
	pprintf(mprn, A_("   line: %s\n"), p);
    }
    
    numcount = 0;
    j = 1; /* for the constant */

    for (k=0; k<c->ncols && !err; k++) {
	i = 0;
	while (*p && *p != c->delim) {
	    if (i < CSVSTRLEN - 1) {
		c->str[i++] = *p;
	    }
	    p++;
	}
	c->str[i] = '\0';
	if (*p == c->delim) p++;

	if (k == 0 && csv_skip_column(c)) {
	    ; /* no-op */
	} else if (!joining(c) && cols_subset(c) && skip_data_column(c, k)) {
	    ; /* no-op */
	} else {
	    if (*c->str == '\0') {
		pprintf(prn, A_("   variable name %d is missing: aborting\n"), j);
		pputs(prn, A_(csv_msg));
		err = E_DATA;
	    } else if (joining(c)) {
		handle_join_varname(c, k, &j);
	    } else {
		c->dset->varname[j][0] = '\0';
		strncat(c->dset->varname[j], c->str, VNAMELEN - 1);
		if (starts_number(*c->str)) {
		    numcount++;
		} else {
		    iso_to_ascii(c->dset->varname[j]);
		    strip_illegals(c->dset->varname[j]);
		    if (check_varname(c->dset->varname[j])) {
			errmsg(1, prn);
			err = E_DATA;
		    }
		}
		j++;
	    }
	}
	if (j == c->dset->v) {
#if CDEBUG
	    fprintf(stderr, "breaking on j = %d (k = %d)\n", j, k);
#endif	    
	    break;
	}
    }

    if (!err && joining(c) && c->cols_list == NULL) {
	/* no relevant columns were found */
	err = E_UNKVAR;
    }

    if (err) {
	return err;
    }

    if (numcount == c->dset->v - 1 || 
	obs_labels_no_varnames(obscol, c->dset, numcount)) {
	pputs(prn, A_("it seems there are no variable names\n"));
	/* then we undercounted the observations by one? */
	if (!rows_subset(c)) {
	    err = add_single_obs(c->dset);
	}
	if (!err) {
	    /* set up handle the no varnames case */
	    csv_set_autoname(c);
	    if (csv_has_bom(c)) {
		fseek(fp, 3, SEEK_SET);
	    } else {
		rewind(fp);
	    }
	    if (obs_labels_no_varnames(obscol, c->dset, numcount)) {
		err = csv_reconfigure_for_markers(c->dset);
		if (!err) {
		    csv_set_obs_column(c);
		}
	    }
	}
    } else if (numcount > 0) {
	for (i=1; i<c->dset->v; i++) {
	    if (check_varname(c->dset->varname[i])) {
		errmsg(1, prn);
		break;
	    }
	}
	fprintf(stderr, "numcount = %d\n", numcount);
	err = E_DATA;
    }

    return err;
}

static int row_not_wanted (csvdata *c, int t)
{
    if (c->rowmask != NULL) {
	if (t >= c->masklen) {
	    return 1;
	} else if (gretl_vector_get(c->rowmask, t) == 0) {
	    return 1;
	}
    }

    return 0;
}

/* read numerical data when we've been given a fixed column-reading
   specification */

static int fixed_format_read (csvdata *c, FILE *fp, PRN *prn)
{
    char *p;
    int miss_shown = 0;
    int t = 0, s = 0;
    int i, k, n, m;
    int err = 0;

    c->real_n = c->dset->n;

    if (csv_has_bom(c)) {
	fseek(fp, 3, SEEK_SET);
    }

    while (csv_fgets(c->line, c->maxlen, fp) && !err) {

	tailstrip(c->line);

	if (*c->line == '#' || string_is_blank(c->line)) {
	    continue;
	}

	if (row_not_wanted(c, s)) {
	    s++;
	    continue;
	}

	m = strlen(c->line);

	for (i=1; i<=c->ncols && !err; i++) {
	    k = c->cols_list[i];
	    n = c->width_list[i];
	    if (k + n - 1 > m) {
		/* attempting to read out of bounds */
		fprintf(stderr, "row %d, column %d: start=%d, width=%d, "
			"but line length = %d\n", t+1, i, k, n, m);
		err = E_DATA;
		break;
	    }		
	    p = c->line + k - 1;
	    *c->str = '\0';
	    strncat(c->str, p, n);
	    if (csv_missval(c->str, i, t+1, &miss_shown, prn)) {
		c->dset->Z[i][t] = NADBL;
	    } else {
		c->dset->Z[i][t] = csv_atof(c, c->str);
		if (c->dset->Z[i][t] == NON_NUMERIC) {
		    gretl_errmsg_sprintf(_("At row %d, column %d:\n"), t+1, k);
		    gretl_errmsg_sprintf(_("'%s' -- no numeric conversion performed!"),
					 c->str);
		    err = E_DATA;
		}
	    }
	}

	s++;
	if (++t == c->dset->n) {
	    break;
	}
    }

    if (err == E_DATA) {
	gretl_errmsg_set(_("Invalid column specification"));
    }

    return err;
}

#define XML1_OK(u) ((u>=0x0020 && u<=0xD7FF) || \
		    (u>=0xE000 && u<=0xFFFD))

/* check that an observation label contains only 
   valid UTF-8, and moreover that every character
   is valid in XML 1.0
*/

static int label_is_valid (gchar *s)
{
    if (!g_utf8_validate(s, -1, NULL)) {
	return 0;
    } else {
	int i, n = g_utf8_strlen(s, -1);
	gunichar u;

	for (i=0; i<n; i++) {
	    u = g_utf8_get_char(s);
	    if (!XML1_OK(u)) {
		return 0;
	    }
	    s = g_utf8_next_char(s);
	}
    }

    return 1;
}

static int 
real_read_labels_and_data (csvdata *c, FILE *fp, PRN *prn)
{
    char *p;
    int miss_shown = 0;
    int t = 0, s = 0;
    int i, j, k;
    int err = 0;
    int truncated = 0;

    c->real_n = c->dset->n;

    while (csv_fgets(c->line, c->maxlen, fp) && !err) {

	if (*c->line == '#' || string_is_blank(c->line)) {
	    continue;
	}

	if (*c->skipstr != '\0' && strstr(c->line, c->skipstr)) {
	    c->real_n -= 1;
	    continue;
	}

	if (row_not_wanted(c, s)) {
	    s++;
	    continue;
	}

	compress_csv_line(c);
	p = c->line;
	if (c->delim == ' ' && *p == ' ') p++;

	j = 1;
	for (k=0; k<c->ncols && !err; k++) {
	    i = 0;
	    while (*p && *p != c->delim) {
		if (i < CSVSTRLEN - 1) {
		    c->str[i++] = *p;
		} else {
		    truncated++;
		}
		p++;
	    }
	    if (*p == c->delim) {
		p++;
	    }
	    c->str[i] = '\0';
	    if (k == 0 && csv_skip_column(c) && c->dset->S != NULL) {
		char *S = c->str;

		c->dset->S[t][0] = 0;
		if (*S == '"' || *S == '\'') {
		    S++;
		}
		strncat(c->dset->S[t], S, OBSLEN - 1);
		if (!label_is_valid((gchar *) c->dset->S[t])) {
		    iso_to_ascii(c->dset->S[t]);
		} 
	    } else if (cols_subset(c) && skip_data_column(c, k)) {
		; /* no-op */
	    } else {
		err = process_csv_obs(c, j++, t, &miss_shown, prn);
	    }
	}

	s++;
	if (++t == c->dset->n) {
	    break;
	}
    }

    if (truncated) {
	pprintf(prn, A_("warning: %d labels were truncated.\n"), truncated);
    }

    if (!err && c->real_n < c->dset->n) {
	int drop = c->dset->n - c->real_n;

	err = dataset_drop_observations(c->dset, drop);
    }

    return err;
}

static int csv_read_data (csvdata *c, FILE *fp, long pos, PRN *prn, PRN *mprn)
{
    int reversed = csv_data_reversed(c);
    int err;
    
    if (mprn != NULL) {
	pputs(mprn, A_("scanning for row labels and data...\n"));
    }

    fseek(fp, pos, SEEK_SET);

    err = real_read_labels_and_data(c, fp, prn);

    if (!err && csv_skip_column(c) && !rows_subset(c) && !joining(c)) {
	c->markerpd = test_markers_for_dates(c->dset, &reversed,
					     c->skipstr, prn);
	if (reversed) {
	    csv_set_data_reversed(c);
	}
    }

    return err;
}

static void print_csv_parsing_header (const char *fname, PRN *prn)
{
    if (!g_utf8_validate(fname, -1, NULL)) {
	gchar *trfname = g_locale_to_utf8(fname, -1, NULL, NULL, NULL);

	pprintf(prn, "%s %s...\n", A_("parsing"), trfname);
	g_free(trfname);
    } else {
	pprintf(prn, "%s %s...\n", A_("parsing"), fname);
    }
}

static int join_unique_columns (csvdata *c)
{
    const char **cnames = c->jspec->colnames;
    int counted[JOIN_MAXCOL] = {0};
    int i, j, ncols = 0;

    for (i=0; i<JOIN_MAXCOL; i++) {
	if (cnames[i] != NULL && counted[i] == 0) {
	    counted[i] = 1;
	    /* mark any duplicates as counted too */
	    for (j=i+1; j<JOIN_MAXCOL; j++) {
		if (cnames[j] != NULL && !strcmp(cnames[i], cnames[j])) {
		    counted[j] = 1;
		}
	    }
#if CDEBUG
	    fprintf(stderr, "join_unique_columns: '%s'\n", cnames[i]);
#endif
	    ncols++;
	}
    }

    return ncols;
}

static int csv_set_dataset_dimensions (csvdata *c)
{
    int err = 0;

    c->dset->v = 0;

    if (rows_subset(c)) {
	c->dset->n = n_from_row_mask(c);
    }

    if (fixed_format(c)) {
	if (c->dset->n == 0) {
	    c->dset->n = c->nrows;
	}
	c->dset->v = c->ncols + 1;
    } else {
	int cols_wanted, cols_present;

	if (c->dset->n == 0) {
	    c->dset->n = c->nrows - 1; /* allow for varnames row */
	}

	cols_present = csv_skip_column(c) ? (c->ncols - 1) : c->ncols;

	if (joining(c)) {
	    cols_wanted = join_unique_columns(c);
	} else if (cols_subset(c)) {
	    cols_wanted = c->cols_list[0];
	} else {
	    cols_wanted = cols_present;
	}

	if (cols_wanted > cols_present) {
	    gretl_errmsg_set(_("Invalid column specification"));
	    err = E_DATA;
	} else {
	    /* allow for the constant */
	    c->dset->v = cols_wanted + 1;
	}
    }

#if CDEBUG
    if (joining(c)) {
	fprintf(stderr, "csv dataset dimensions: v=%d, n=%d\n",
		c->dset->v, c->dset->n);
    }
#endif

    return err;
}

/*
 * real_import_csv:
 * @fname: name of CSV file.
 * @dset: dataset struct.
 * @cols: column specification.
 * @rows: row specification.
 * @join: specification pertaining to "join" command.
 * @opt: use OPT_N to force interpretation of data colums containing
 * strings as coded (non-numeric) values and not errors; for use of 
 * OPT_T see the help for "append".
 * @prn: gretl printing struct (or NULL).
 * 
 * Open a Comma-Separated Values data file and read the data into
 * the current work space.
 * 
 * Returns: 0 on successful completion, non-zero otherwise.
 */

static int real_import_csv (const char *fname, 
			    DATASET *dset, 
			    const char *cols, 
			    const char *rows,
			    csvjoin *join,
			    gretlopt opt, 
			    PRN *prn)
{
    csvdata *c = NULL;
    FILE *fp = NULL;
    PRN *mprn = NULL;
    int newdata = (dset->Z == NULL);
    int popit = 0;
    long datapos;
    int i, err = 0;

    import_na_init();

    if (prn != NULL) {
	set_alt_gettext_mode(prn);
    }

    if (gretl_messages_on()) {
	mprn = prn;
    }

    fp = gretl_fopen(fname, "r");
    if (fp == NULL) {
	pprintf(prn, A_("Couldn't open %s\n"), fname);
	err = E_FOPEN;
	goto csv_bailout;
    }

    c = csvdata_new(dset);
    if (c == NULL) {
	err = E_ALLOC;
	goto csv_bailout;
    }

    if (cols != NULL) {
	err = csvdata_add_cols_list(c, cols, opt);
	if (err) {
	    goto csv_bailout;
	} else if (fixed_format(c)) {
	    pprintf(mprn, A_("using fixed column format\n"));
	}
    }

    if (rows != NULL) {
	err = csvdata_add_row_mask(c, rows);
	if (err) {
	    goto csv_bailout;
	} 
    }

    if (join != NULL) {
	c->jspec = join;
    } else if (opt & OPT_A) {
	csv_set_all_cols(c);
    }

    if (mprn != NULL) {
	print_csv_parsing_header(fname, mprn);
    }

    /* get line length, also check for binary data, etc. */
    c->maxlen = csv_max_line_length(fp, c, prn);    
    if (c->maxlen <= 0) {
	err = E_DATA;
	goto csv_bailout;
    }

#if CDEBUG
    fprintf(stderr, "fixed_format? %s; got_delim (%c)? %s; got_tab? %s\n",
	    fixed_format(c) ? "yes" : "no", c->delim,
	    csv_got_delim(c) ? "yes" : "no",
	    csv_got_tab(c)? "yes" : "no");
#endif    

    if (!fixed_format(c) && !csv_got_delim(c)) {
	/* set default delimiter */
	if (csv_got_tab(c)) {
	    c->delim = '\t';
	} else if (csv_got_semi(c)) {
	    c->delim = ';';
	} else {
	    c->delim = ' ';
	}
    }

    /* buffer to hold lines */
    c->line = malloc(c->maxlen);
    if (c->line == NULL) {
	err = E_ALLOC;
	goto csv_bailout;
    }  

 alt_delim:

    if (!fixed_format(c)) {
	pprintf(mprn, A_("using delimiter '%c'\n"), c->delim);
    }

    pprintf(mprn, A_("   longest line: %d characters\n"), c->maxlen - 1);

    if (csv_has_trailing_comma(c) && c->delim != ',') {
	csv_unset_trailing_comma(c);
    }

    rewind(fp);

    /* read lines, check for consistency in number of fields */
    err = csv_fields_check(fp, c, mprn);
    if (err && !fixed_format(c)) {
	if (c->delim != ';' && csv_got_semi(c)) {
	    c->delim = ';';
	    err = 0;
	    goto alt_delim;
	}
	pputs(prn, A_(csv_msg));
	goto csv_bailout;
    }

    err = csv_set_dataset_dimensions(c);
    if (err) {
	err = E_DATA;
	goto csv_bailout;
    }	

    pprintf(mprn, A_("   number of variables: %d\n"), c->dset->v - 1);
    pprintf(mprn, A_("   number of non-blank lines: %d\n"), c->nrows);

    if (c->dset->n == 0) {
	pputs(prn, A_("Invalid data file\n"));
	err = E_DATA;
	goto csv_bailout;
    }

    /* initialize CSV dataset */
    err = start_new_Z(c->dset, 0);
    if (!err && csv_skip_column(c)) {
	err = dataset_allocate_obs_markers(c->dset);
    }

    if (err) {
	goto csv_bailout;
    }

    /* second pass */

    rewind(fp);

    if (fixed_format(c)) {
	err = fixed_format_read(c, fp, prn);
	if (err) {
	    goto csv_bailout;
	} else {
	    csv_set_autoname(c);
	    goto csv_continue;
	}
    }

    err = csv_varname_scan(c, fp, prn, mprn);
    if (err) {
	goto csv_bailout;
    }

    if (c->decpoint == '.' && get_local_decpoint() == ',') {
	gretl_push_c_numeric_locale();
	popit = 1;
    } else if (c->decpoint == ',' && get_local_decpoint() == '.') {
	csv_set_dotsub(c);
    }

    datapos = ftell(fp);

    err = csv_read_data(c, fp, datapos, prn, mprn);

    if (!err && csv_skip_bad(c)) {
	/* try again */
	err = csv_read_data(c, fp, datapos, prn, NULL);
    }

    if (!err) {
	err = non_numeric_check(c, prn);
	if (!err && csv_has_non_numeric(c)) {
	    /* try once more */
	    err = csv_read_data(c, fp, datapos, prn, NULL);
	}
    }

    if (popit) {
	gretl_pop_c_numeric_locale();
    }

    if (err) {
	goto csv_bailout;
    }

    if (csv_data_reversed(c)) {
	reverse_data(c->dset, mprn);
    }

 csv_continue:

    c->dset->t1 = 0;
    c->dset->t2 = c->dset->n - 1;

    if (c->markerpd > 0) {
	pputs(mprn, A_("taking date information from row labels\n\n"));
	if (csv_skip_bad(c)) {
	    pprintf(prn, "WARNING: Check your data! gretl has stripped out "
		    "what appear to be\nextraneous lines in a %s dataset: " 
		    "this may not be right.\n\n",
		    (c->dset->pd == 4)? "quarterly" : "monthly");
	}
    } else {
	pputs(mprn, A_("treating these as undated data\n\n"));
	dataset_obs_info_default(c->dset);
    }

    if (c->dset->pd != 1 || strcmp(c->dset->stobs, "1")) { 
        c->dset->structure = TIME_SERIES;
    }

    if (c->st != NULL) {
	if (joining(c)) {
	    gretl_string_table_save(c->st, c->dset);
	} else {
	    gretl_string_table_print(c->st, c->dset, fname, prn);
	}
    }

    /* If there were observation labels and they were not interpretable
       as dates, and they weren't simply "1, 2, 3, ...", then they 
       should probably be preserved; otherwise discard them. 
    */
    if (c->dset->S != NULL && c->markerpd >= 0 && 
	c->dset->markers != DAILY_DATE_STRINGS) {
	dataset_destroy_obs_markers(c->dset);
    }

    if (csv_autoname(c)) {
	/* no variable names were found */
	for (i=1; i<c->dset->v; i++) {
	    sprintf(c->dset->varname[i], "v%d", i);
	}
    } else {
#if CDEBUG
	int ii;

	fprintf(stderr, "HERE, calling fix_varname_duplicates\n");
	for (ii=0; ii<c->dset->v; ii++) {
	    fprintf(stderr, " c->dset->varname[%d] = '%s'\n", ii, c->dset->varname[ii]);
	}
#endif	
	if (fix_varname_duplicates(c->dset)) {
	    pputs(prn, A_("warning: some variable names were duplicated\n"));
	}
    }

    if (!joining(c)) {
	/* not doing a special "join" operation */
	err = merge_or_replace_data(dset, &c->dset, opt, prn);

	if (!err && newdata && c->descrip != NULL) {
	    dset->descrip = c->descrip;
	    c->descrip = NULL;
	}

	if (!err) {
	    dataset_add_import_info(dset, fname, GRETL_CSV);
	}
    }

 csv_bailout:

    if (fp != NULL) {
	fclose(fp);
    }

    if (!err && c->jspec != NULL) {
	c->jspec->c = c;
    } else {
	csvdata_free(c);
    }

    if (err == E_ALLOC) {
	pputs(prn, A_("Out of memory\n"));
    }    

    return err;
}

/**
 * import_csv:
 * @fname: name of CSV file.
 * @dset: dataset struct.
 * @opt: use OPT_N to force interpretation of data colums containing
 * strings as coded (non-numeric) values and not errors; for use of 
 * OPT_T see the help for "append".
 * @prn: gretl printing struct (or NULL).
 * 
 * Open a Comma-Separated Values data file and read the data into
 * the current work space.
 * 
 * Returns: 0 on successful completion, non-zero otherwise.
 */

int import_csv (const char *fname, DATASET *dset, 
		gretlopt opt, PRN *prn)
{
    const char *cols = NULL;
    const char *rows = NULL;
    int ci, err;

    err = incompatible_options(opt, OPT_F | OPT_L);
    if (err) {
	/* --cols and --fixed-cols */
	return err;
    }

    ci = (dset != NULL && dset->v > 0)? APPEND : OPEN;

    if (opt & OPT_F) {
	/* we should have a "--fixed-cols=XXX" specification */
	cols = get_optval_string(ci, OPT_F);
	if (cols == NULL || *cols == '\0') {
	    return E_PARSE;
	}
    } else if (opt & OPT_L) {
	/* should have a "--cols=XXX" specification */
	cols = get_optval_string(ci, OPT_L);
	if (cols == NULL || *cols == '\0') {
	    return E_PARSE;
	}
    }	

    if (opt & OPT_M) {
	/* we should have a "--rowmask=XXX" specification */
	rows = get_optval_string(ci, OPT_M);
	if (rows == NULL || *rows == '\0') {
	    return E_PARSE;
	}
    }	

    return real_import_csv(fname, dset, cols, rows, 
			   NULL, opt, prn);
}

/* below: apparatus to implement the "join" command */

struct jr_row_ {
    int n_keys;  /* number of keys (needed for qsort callback) */
    int keyval;  /* primary key value */
    int keyval2; /* secondary key value, if applicable */
    double val;  /* data value */
    double aux;  /* auxiliary value */
};

typedef struct jr_row_ jr_row;

struct obskey_ {
    char *timefmt;
    int keycol;
    int m_means_q;
    int convert;
};

typedef struct obskey_ obskey;

struct joiner_ {
    int n_rows;     /* number of rows in data table */
    int n_keys;     /* number of keys used (1 or 2) */
    int n_unique;   /* number of unique keys found on right, primary key */
    jr_row *rows;   /* array of table rows */
    int *keys;      /* array of unique (primary) key values as integers */
    int *key_freq;  /* counts of occurrences of (primary) key values */
    int *key_row;   /* record of starting row in joiner row for primary keys */
    int str_keys;   /* flag for string comparison of primary keys */
    int str_keys2;  /* flag for string comparison of secondary keys */
    const int *l_keyno; /* list of ikey IDs in lhs dset */
    const int *r_keyno; /* list of okey IDs in rhs dset */
    AggrType aggr;    /* aggregation method for 1:n joining */
    int seqval;       /* aux. sequence number for aggregation */
    int auxcol;       /* aux. data column for aggregation */
    int valcol;       /* column of RHS dataset holding payload */
    obskey *auto_keys; /* struct to hold info on obs-based key(s) */
    DATASET *l_dset;   /* the main (left-hand) dataset */
    DATASET *r_dset;   /* the temporary CSV dataset */
};

typedef struct joiner_ joiner;

enum {
    FILT_NUMERIC,
    FILT_STRING,
    FILT_MISSMASK,
    FILT_GENR
};

struct jr_filter_ {
    const char *expr;
    const double *val;
    char *lhname;
    double lhval;
    int lhcol;
    char *rhname;
    double rhval;
    int rhcol;
    int op;
    int mode;
};

typedef struct jr_filter_ jr_filter;

static void jr_filter_destroy (jr_filter *f)
{
    if (f != NULL) {
	free(f->lhname);
	free(f->rhname);
	free(f);
    }
}

static void joiner_destroy (joiner *jr)
{
    if (jr != NULL) {
	free(jr->rows);
	free(jr->keys);
	free(jr->key_freq);
	free(jr->key_row);
	free(jr);
    }
}

static int filter_by_date (const char *s1, const char *s2, int op, 
			   int *err)
{
    const char *isofmt = "%Y-%m-%d";
    char *test;
    int i, ed[2];
    int ret = 0;

    for (i=0; i<2 && !*err; i++) {
	const char *s = i == 0 ? s1 : s2;
	struct tm t = {0};

	test = strptime(s, isofmt, &t);
	if (test == NULL || *test != '\0') {
	    gretl_errmsg_sprintf("'%s' does not match the format '%s'", s, isofmt);
	    *err = E_DATA;
	} else {
	    int y = t.tm_year + 1900;
	    int m = t.tm_mon + 1;
	    int d = t.tm_mday;

	    ed[i] = epoch_day_from_ymd(y, m, d);
	    if (ed[i] < 0) {
		gretl_errmsg_sprintf("'%s' is not a valid date", s);
		*err = E_DATA;
	    }
	}
    }

    if (!*err) {
	if (op == B_EQ) {
	    ret = ed[0] == ed[1];
	} else if (op == B_GT) {
	    ret = ed[0] > ed[1];
	} else if (op == B_LT) {
	    ret = ed[0] < ed[1];
	} else if (op == B_GTE) {
	    ret = ed[0] >= ed[1];
	} else if (op == B_LTE) {
	    ret = ed[0] <= ed[1];
	} else if (op == B_NEQ) {
	    ret = ed[0] != ed[1];
	} else {
	    *err = E_PARSE;
	}	
    }

#if CDEBUG > 1
    fprintf(stderr, "date filter: '%s' -> %d and '%s' -> %d; ret = %d\n",
	    s1, ed[0], s1, ed[1], ret);
#endif

    return ret;
}

/* In relation to join, determine whether or not row @i of the data
   read from CSV satisfies the filter criterion; return 1 if the
   condition is met, 0 otherwise.
*/

static int join_row_wanted (DATASET *dset, int i,
			    jr_filter *filter, 
			    csvjoin *jspec,
			    int *err)
{
    int ret = 0;

    if (filter == NULL) {
	/* no-op */
	return 1;
    }

    if (filter->val != NULL) {
	return filter->val[i] != 0;
    }

    if (filter->mode == FILT_STRING) {
	int lhdate = 0, rhdate = 0;
	const char *sx;
	const char *sy;
	size_t slen;

	if (filter->lhcol) {
	    sx = series_get_string_val(dset, filter->lhcol, i);
	    lhdate = series_get_flags(dset, filter->lhcol) & VAR_TIMECOL;
	} else {
	    sx = filter->lhname;
	}

	if (filter->rhcol) {
	    sy = series_get_string_val(dset, filter->rhcol, i);
	    rhdate = series_get_flags(dset, filter->rhcol) & VAR_TIMECOL;
	} else {
	    sy = filter->rhname;
	}

	if (sx == NULL || sy == NULL) {
	    gretl_errmsg_sprintf(_("%s: missing string in filtering"), "join");
	    if (filter->lhcol) {
		fprintf(stderr, "filter: LHS='%s' (from column %d),", sx, filter->lhcol);
	    } else {
		fprintf(stderr, "filter: LHS='%s' (string constant),", sx);
	    }
	    if (filter->rhcol) {
		fprintf(stderr, " RHS='%s' (from column %d)\n", sy, filter->rhcol);
	    } else {
		fprintf(stderr, " RHS='%s' (string constant)\n", sy);
	    }	    
	    *err = E_MISSDATA;
	} else if (filter->lhcol && filter->rhcol && lhdate != rhdate) {
	    gretl_errmsg_set(_("type mismatch in join filter"));
	    *err = E_TYPES;
	} else if (lhdate || rhdate) {
	    ret = filter_by_date(sx, sy, filter->op, err);
	} else if (filter->op == B_EQ) {
	    ret = (strcmp(sx, sy) == 0);
	} else if (filter->op == B_GT) {
	    slen = strlen(sy);
	    ret = (strlen(sx) > slen) && (strncmp(sx, sy, slen) == 0);
	} else if (filter->op == B_LT) {
	    slen = strlen(sx);
	    ret = (strlen(sy) > slen) && (strncmp(sx, sy, slen) == 0);
	} else if (filter->op == B_NEQ) {
	    ret = (strcmp(sx, sy) != 0);
	} else {
	    *err = E_PARSE;
	}
    } else if (filter->mode == FILT_MISSMASK) {
	double x = dset->Z[filter->lhcol][i];

	if (filter->op == B_EQ) {
	    ret = na(x);
	} else {
	    ret = !na(x);
	}
    } else {
	/* numerical comparison */
	double x = filter->lhcol ? dset->Z[filter->lhcol][i] : filter->lhval;
	double y = filter->rhcol ? dset->Z[filter->rhcol][i] : filter->rhval;
	
	if (na(x) || na(y)) {
	    gretl_errmsg_sprintf(_("%s: found NAs in filtering"), "join");
	    *err = E_MISSDATA;
	} else if (filter->op == B_EQ) {
	    ret = x == y;
	} else if (filter->op == B_GT) {
	    ret = x > y;
	} else if (filter->op == B_LT) {
	    ret = x < y;
	} else if (filter->op == B_GTE) {
	    ret = x >= y;
	} else if (filter->op == B_LTE) {
	    ret = x <= y;
	} else if (filter->op == B_NEQ) {
	    ret = x != y;
	} else {
	    *err = E_PARSE;
	}
    }

#if CDEBUG > 2
    fprintf(stderr, "join filter: %s row %d\n",
	    ret ? "keeping" : "discarding", i);
#endif

    return ret;
}

static int convert_to_string (char *targ, double x)
{
    if (na(x)) {
	return E_MISSDATA;
    } else {
	sprintf(targ, "%.16g", x);
	return 0;
    }
}

/* Parse the obs string on row @i of the outer dataset and set the
   key(s) on row @j of the joiner struct. We get here only if we have 
   verified that the obs strings exist. The indices @i and @j may not 
   be equal if a filter is being used.
*/

static int read_outer_auto_keys (joiner *jr, int j, int i)
{
    char *tfmt = jr->auto_keys->timefmt;
    int tcol = jr->auto_keys->keycol;
    struct tm t = {0};
    char sconv[32];
    const char *s;
    char *test;
    int s_src = 0;
    int err = 0;

    if (tcol >= 0) {
	/* using a specified column */
	if (jr->auto_keys->convert) {
	    convert_to_string(sconv, jr->r_dset->Z[tcol][i]);
	    s = sconv;
	    s_src = 1;
	} else {
	    /* column is string-valued, fine */
	    s = series_get_string_val(jr->r_dset, tcol, i);
	    s_src = 2;
	}
    } else {
	/* using first-column observation strings */
	s = jr->r_dset->S[i];
	s_src = 3;
    }

    test = strptime(s, tfmt, &t);

    if (test == NULL || *test != '\0') {
	err = E_DATA;
	if (i == 0 && test != NULL && jr->l_dset->pd == 12) {
	    /* if we're on the first row of the outer data, allow for
	       the possibility that we got "excess precision", i.e.
	       a daily date string when the left-hand dataset is
	       monthly
	    */
	    char *chk = strptime(s, "%Y-%m-%d", &t);

	    if (chk != NULL && *chk == '\0') {
		free(jr->auto_keys->timefmt);
		jr->auto_keys->timefmt = gretl_strdup("%Y-%m-%d");
		err = 0; /* we might be OK, cancel the error */
	    }
	}
	if (err) {
	    gretl_errmsg_sprintf("'%s' does not match the format '%s'", s, tfmt);
	    fprintf(stderr, "time-format match error in read_outer_auto_keys:\n"
		    " remainder = '%s' (source = %s)\n", test ? test : "null", 
		    s_src < 3 ? "specified time column" : "first-column strings");
	    return err;
	}
    }

    if (calendar_data(jr->l_dset)) {
	int y, m, d, eday;

	y = t.tm_year + 1900;
	m = t.tm_mon + 1;
	d = t.tm_mday;
	eday = epoch_day_from_ymd(y, m, d);
	if (eday < 0) {
	    gretl_errmsg_sprintf("'%s' is not a valid date", s);
	    err = E_DATA;
	} else {
	    jr->rows[j].n_keys = 1;
	    jr->rows[j].keyval = eday;
	    jr->rows[j].keyval2 = 0;
	}
    } else {
	int maj, min;

	maj = t.tm_year + 1900;
	min = t.tm_mon + 1;
	if (jr->auto_keys->m_means_q) {
	    /* using the gretl-specific "%q" conversion */
	    if (min > 4) {
		gretl_errmsg_sprintf("'%s' is not a valid date", s);
		err = E_DATA;
	    }
	} else if (jr->l_dset->pd == 4) {
	    /* map from month on right to quarter on left */
	    min = (int) ceil(min / 3.0);
	}
	if (!err) {
	    jr->rows[j].n_keys = 2;
	    jr->rows[j].keyval = maj;
	    jr->rows[j].keyval2 = min;
	}
    }

    return err;
}

static int verify_genr_filter (jr_filter *filter, DATASET *r_dset,
			       int *nrows)
{
    char *line;
    int i, err = 0;

    line = gretl_strdup_printf("series filtered__ = %s", filter->expr);
    if (line == NULL) {
	err = E_ALLOC;
    } else {
	err = generate(line, r_dset, OPT_P | OPT_Q, NULL);
	free(line);
    }

    if (!err) {
	int v = r_dset->v - 1;

	filter->val = r_dset->Z[v];
	*nrows = 0;
	for (i=0; i<r_dset->n; i++) {
	    if (na(filter->val[i])) {
		err = E_MISSDATA;
		break;
	    } else if (filter->val[i] != 0.0) {
		*nrows += 1;
	    }
	}
    }

    return err;
}

static int verify_filter (jr_filter *filter, int lhcol, int rhcol,
			  const DATASET *r_dset)
{
    int err = 0;

    if (filter->lhname != NULL && lhcol == 0) {
	gretl_errmsg_sprintf(_("%s: filter column '%s' was not found"),
		"join", filter->lhname);
	err = E_DATA;
    } else {
	filter->lhcol = lhcol;
	if (filter->mode != FILT_MISSMASK && 
	    series_has_string_table(r_dset, lhcol)) {
	    filter->mode = FILT_STRING;
	}
	if (filter->rhname != NULL) {
	    if (rhcol > 0) {
		filter->rhcol = rhcol;
	    } else if (filter->mode != FILT_STRING) {
		gretl_errmsg_sprintf(_("%s: filter column '%s' was not found"),
				     "join", filter->rhname);
		err = E_DATA;
	    }
	}
    }

#if CDEBUG
    fprintf(stderr, "verify_filter: mode = %d, lhhcol = %d, rhcol = %d\n", 
	    filter->mode, filter->lhcol, filter->rhcol);
#endif

    return err;
}

/* get an integer key value from a double, checking for
   pathology */

static int dtoi (double x, int obs, int *err)
{
    if (xna(x) || fabs(x) > INT_MAX) {
	if (obs > 0) {
	    gretl_errmsg_sprintf("%s: invalid outer key value on row %d",
				 "join", obs);
	}
	*err = E_DATA;
	return -1;
    } else {
	return (int) trunc(x);
    }
}

static joiner *joiner_new (int nrows)
{
    joiner *jr = malloc(sizeof *jr);

    if (jr != NULL) {
	jr->rows = malloc(nrows * sizeof *jr->rows);
	if (jr->rows == NULL) {
	    free(jr);
	    jr = NULL;
	}
    }

    if (jr != NULL) {
	jr->n_rows = nrows;
	jr->n_unique = 0;
	jr->keys = NULL;
	jr->key_freq = NULL;
	jr->key_row = NULL;
	jr->l_keyno = NULL;
	jr->r_keyno = NULL;
    }

    return jr;
}

#define using_auto_keys(j) (j->auto_keys->timefmt != NULL)

static joiner *build_joiner (csvjoin *jspec, 
			     DATASET *l_dset,
			     jr_filter *filter,
			     AggrType aggr,
			     int seqval,
			     obskey *auto_keys,
			     int *err)
{
    joiner *jr = NULL;
    DATASET *r_dset = jspec->c->dset;
    int keycol  = jspec->colnums[JOIN_KEY];
    int valcol  = jspec->colnums[JOIN_VAL];
    int lhfcol  = jspec->colnums[JOIN_LHF];
    int rhfcol  = jspec->colnums[JOIN_RHF];
    int key2col = jspec->colnums[JOIN_KEY2];
    int auxcol  = jspec->colnums[JOIN_AUX];
    int i, nrows = r_dset->n;

#if CDEBUG
    fprintf(stderr, "joiner columns:\n"
	    "KEY=%d, VAL=%d, LHF=%d, RHF=%d, KEY2=%d, AUX=%d\n",
	    keycol, valcol, lhfcol, rhfcol, key2col, auxcol);
#endif

    if (filter != NULL) {
	if (filter->mode == FILT_GENR) {
	    *err = verify_genr_filter(filter, r_dset, &nrows);
	} else {
	    *err = verify_filter(filter, lhfcol, rhfcol, r_dset);
	    if (!*err) {
		/* count the filtered rows */
		nrows = 0;
		for (i=0; i<r_dset->n && !*err; i++) {
		    if (join_row_wanted(r_dset, i, filter, jspec, err)) {
			nrows++;
		    }
		}
	    }
	}
    }

#if CDEBUG
    fprintf(stderr, "after filtering: dset->n = %d, nrows = %d\n",
	    r_dset->n, nrows);
#endif

    if (*err || nrows == 0) {
	fprintf(stderr, "No matching data after filtering\n");
	return NULL;
    }

    jr = joiner_new(nrows);

    if (jr == NULL) {
	*err = E_ALLOC;
    } else {
	double **Z = r_dset->Z;
	int j = 0;

	jr->aggr = aggr;
	jr->seqval = seqval;
	jr->auxcol = auxcol;
	jr->valcol = valcol;
	jr->l_dset = l_dset;
	jr->r_dset = r_dset;
	jr->auto_keys = auto_keys;

	/* now transcribe the rows we want */

	for (i=0; i<r_dset->n && !*err; i++) {
	    if (join_row_wanted(r_dset, i, filter, jspec, err)) {
		/* the keys */
		if (using_auto_keys(jr)) {
		    *err = read_outer_auto_keys(jr, j, i);
		} else if (keycol > 0) {
		    jr->rows[j].keyval = dtoi(Z[keycol][i], i+1, err);
		    if (key2col > 0) {
			/* double key */
			jr->rows[j].n_keys = 2;
			jr->rows[j].keyval2 = dtoi(Z[key2col][i], i+1, err);
		    } else {
			/* single key */
			jr->rows[j].n_keys = 1;
			jr->rows[j].keyval2 = 0;
		    }
		} else {
		    /* no keys have been specified */
		    jr->rows[j].n_keys = 0;
		    jr->rows[j].keyval = 0;
		    jr->rows[j].keyval2 = 0;
		}
		/* the "payload" data */
		jr->rows[j].val = valcol > 0 ? Z[valcol][i] : 0;
		/* the auxiliary data */
		jr->rows[j].aux = auxcol > 0 ? Z[auxcol][i] : 0;
		j++;
	    }
	}
    }

    return jr;
}

/* qsort callback for sorting rows of the joiner struct */

static int compare_jr_rows (const void *a, const void *b)
{
    const jr_row *ra = a;
    const jr_row *rb = b;
    int ret;

    ret = (ra->keyval > rb->keyval) - (ra->keyval < rb->keyval);

    if (ret == 0 && ra->n_keys > 1) {
	ret = (ra->keyval2 > rb->keyval2) - (ra->keyval2 < rb->keyval2);
    }

    if (ret == 0) {
	/* ensure stable sort */
	ret = a - b > 0 ? 1 : -1;
    }
    
    return ret;
}

/* Sort the rows of the joiner struct, by either one or two keys, then
   figure out how many unique (primary) key values we have and
   construct (a) an array of frequency of occurrence of these values
   and (b) an array which records the first row of the joiner on
   which each of these values is found.
*/

static int joiner_sort (joiner *jr)
{
    int matches = jr->n_rows;
    int i, err = 0;

    /* If there are string keys, we begin by mapping from the string
       indices on the right -- held in the keyval and/or keyval2
       members of the each joiner row -- to the indices for the same
       strings on the left. This enables us to avoid doing string
       comparisons when running aggr_value() later; we can just
       compare the indices of the strings. In addition, if on any
       given row we get no match for the right-hand key string on the
       left (signalled by a strmap value of -1) we can exploit this
       information by shuffling such rows to the end of the joiner
       rectangle and ignoring them when aggregating.
    */ 

    if (jr->str_keys || jr->str_keys2) {
	series_table *stl, *str;
	int *strmap;
	int k, kmin, kmax, lkeyval, rkeyval;

	kmin = jr->str_keys ? 1 : 2;
	kmax = jr->str_keys2 ? 2 : 1;

	for (k=kmin; k<=kmax; k++) {
	    stl = series_get_string_table(jr->l_dset, jr->l_keyno[k]);
	    str = series_get_string_table(jr->r_dset, jr->r_keyno[k]);
	    strmap = series_table_map(str, stl);

	    if (strmap == NULL) {
		err = E_ALLOC;
		break;
	    }

	    for (i=0; i<jr->n_rows; i++) {
		if (k == 1) {
		    rkeyval = jr->rows[i].keyval;
		} else if (jr->rows[i].keyval == INT_MAX) {
		    continue;
		} else {
		    rkeyval = jr->rows[i].keyval2;
		}
		lkeyval = strmap[rkeyval];
#if CDEBUG > 1
		fprintf(stderr, "k = %d, row %d, keyval: %d -> %d\n", k, i, rkeyval, lkeyval);
#endif
		if (lkeyval > 0) {
		    if (k == 1) {
			jr->rows[i].keyval = lkeyval;
		    } else {
			jr->rows[i].keyval2 = lkeyval;
		    }
		} else {
		    /* arrange for qsort to move row to end */
		    jr->rows[i].keyval = INT_MAX;
		    matches--;
		}
	    }

	    free(strmap);
	}
    }

    if (err) {
	return err;
    }

    qsort(jr->rows, jr->n_rows, sizeof *jr->rows, compare_jr_rows);

    if (matches < jr->n_rows) {
	jr->n_rows = matches;
    }

    jr->n_unique = 1;
    for (i=1; i<jr->n_rows; i++) {
	if (jr->rows[i].keyval != jr->rows[i-1].keyval) {
	    jr->n_unique += 1;
	}
    }

    jr->keys = malloc(jr->n_unique * sizeof *jr->keys);
    jr->key_freq = malloc(jr->n_unique * sizeof *jr->key_freq);
    jr->key_row = malloc(jr->n_unique * sizeof *jr->key_row);

    if (jr->keys == NULL || jr->key_freq == NULL || jr->key_row == NULL) {
	err = E_ALLOC;
    } else {
	int j = 0, nj = 1;

	for (i=0; i<jr->n_unique; i++) {
	    jr->key_freq[i] = 0;
	}

	jr->keys[0] = jr->rows[0].keyval;
	jr->key_row[0] = 0;

	for (i=1; i<jr->n_rows; i++) {
	    if (jr->rows[i].keyval != jr->rows[i-1].keyval) {
		/* finalize info for key j */
		jr->keys[j] = jr->rows[i-1].keyval;
		jr->key_freq[j] = nj;
		/* and initialize for next key */
		nj = 1;
		if (j < jr->n_unique - 1) {
		    jr->key_row[j+1] = i;
		}
		j++;
	    } else {
		nj++;
	    }
	}

	/* make sure the last row is right */
	jr->keys[j] = jr->rows[i-1].keyval;
	jr->key_freq[j] = nj;
    }

    return err;
}

#if CDEBUG > 1

static void joiner_print (joiner *jr)
{
    const char **labels = NULL;
    jr_row *row;
    int i;

    if (jr->str_keys) {
	labels = series_get_string_vals(jr->l_dset, jr->l_keyno[1], NULL);
    }

    fprintf(stderr, "\njoiner: n_rows = %d\n", jr->n_rows);
    for (i=0; i<jr->n_rows; i++) {
	row = &jr->rows[i];
	if (row->n_keys > 1) {
	    fprintf(stderr, " row %d: keyvals=(%d,%d), data=%.12g\n", i, 
		    row->keyval, row->keyval2, row->val);
	} else {
	    if (jr->str_keys && row->keyval >= 0) {
		fprintf(stderr, " row %d: keyval=%d (%s), data=%.12g\n", i, 
			row->keyval, labels[row->keyval - 1], row->val);
	    } else {
		fprintf(stderr, " row %d: keyval=%d, data=%.12g\n", i, row->keyval,
			row->val);
	    }
	}
    }

    if (jr->keys != NULL) {
	fprintf(stderr, " for primary key: n_unique = %d\n", jr->n_unique);
	for (i=0; i<jr->n_unique; i++) {
	    fprintf(stderr,"  key value %d: count = %d\n", 
		    jr->keys[i], jr->key_freq[i]);
	}
    }
}

#endif

static int seqval_out_of_bounds (joiner *jr, int seqmax)
{
    if (jr->seqval < 0) {
	/* counting down from last match */
	return -jr->seqval > seqmax;
    } else {
	/* counting up from first match */
	return jr->seqval > seqmax;
    }
}

static int binsearch (int targ, const int *vals, int n, int offset)
{
    int m = n/2;

    if (targ == vals[m]) {
	return m + offset;
    } else if (targ < vals[0] || targ > vals[n-1]) {
	return -1;
    } else if (targ < vals[m]) {
	return binsearch(targ, vals, m, offset);
    } else {
	return binsearch(targ, vals + m, n - m, offset + m);
    }
}

static double aggr_value (joiner *jr, int key, int key2,
			  double *xmatch, double *auxmatch, 
			  int *err)
{
    double x, y, xa;
    int imin, imax, pos;
    int i, n;

    /* find the position of the inner (primary) key in the 
       array of unique outer key values */
    pos = binsearch(key, jr->keys, jr->n_unique, 0);

#if CDEBUG
    if (pos < 0) {
	fprintf(stderr, "  no match on primary key\n");
    } else {
	fprintf(stderr, "  position on right, among unique keys = %d\n", pos);
    }
#endif    

    if (pos < 0) {
	/* (primary) inner key value not found */
	return jr->aggr == AGGR_COUNT ? 0 : NADBL;
    }

    /* how many matches at @pos? */
    n = jr->key_freq[pos];

    if (jr->n_keys == 1) {
	if (jr->aggr == AGGR_COUNT) {
	    /* simple, we're done */
	    return n;
	} else if (jr->aggr == AGGR_SEQ && seqval_out_of_bounds(jr, n)) {
	    /* out of bounds sequence index */
	    return NADBL;
	} else if (n > 1 && jr->aggr == AGGR_NONE) {
	    *err = E_DATA;
	    gretl_errmsg_set(_("You need to specify an aggregation "
			       "method for a 1:n join"));
	    return NADBL;
	}
    }

    /* set the starting row in joiner rectangle */
    imin = jr->key_row[pos];
    
    if (imin < 0) {
	/* "can't happen" */
	return NADBL;
    }

    imax = imin + n;
    n = 0; /* will now hold count of non-NA matches */

    /* If we also have a secondary key, we need to find how
       many instances of the secondary match fall under the
       primary match. While we're at it fill out the array
       @xmatch with non-missing values from the relevant
       rows.
    */

    if (jr->n_keys > 1) {
	/* note: @totcount ignores the OK/NA distinction */
	int totcount = 0;

	for (i=imin; i<imax; i++) {
	    if (key2 == jr->rows[i].keyval2) {
		totcount++;
		x = jr->rows[i].val;
		if (jr->auxcol) {
		    xa = jr->rows[i].aux;
		    if (!na(xa)) {
			auxmatch[n] = xa;
			xmatch[n++] = x;
		    }
		} else if (!na(x)) {
		    xmatch[n++] = x;
		}
	    }		
	}

	if (jr->aggr == AGGR_COUNT) {
	    return totcount;
	} else if (totcount > 1 && jr->aggr == AGGR_NONE) {
	    *err = E_DATA;
	    gretl_errmsg_set(_("You need to specify an aggregation "
			       "method for a 1:n join"));
	    return NADBL;
	}
    } else {
	/* just one key */
	for (i=imin; i<imax; i++) {
	    x = jr->rows[i].val;
	    if (jr->auxcol) {
		xa = jr->rows[i].aux;
		if (!na(xa)) {
		    auxmatch[n] = xa;
		    xmatch[n++] = x;
		}
	    } else if (!na(x)) {
		xmatch[n++] = x;
	    }
	}
    }

    x = NADBL;

    if (n == 0) {
	; /* all obs. are NAs */
    } else if (jr->aggr == AGGR_NONE) {
	x = xmatch[0];
    } else if (jr->aggr == AGGR_SEQ) {
	int sval = jr->seqval;

	i = sval < 0 ? n + sval : sval - 1;
	if (i >= 0 && i < n) {
	    x = xmatch[i];
	}	    
    } else if (jr->aggr == AGGR_MAX) {
	if (jr->auxcol) {
	    /* using the max of an auxiliary var */
	    xa = auxmatch[0];
	}
	x = xmatch[0];
	for (i=1; i<n; i++) {
	    if (jr->auxcol) {
		y = auxmatch[i];
		if (y > xa) {
		    x = xmatch[i];
		}
	    } else {
		y = xmatch[i];
		if (y > x) {
		    x = y;
		}
	    }
	}
    } else if (jr->aggr == AGGR_MIN) {
	if (jr->auxcol) {
	    /* using the min of an auxiliary var */
	    xa = auxmatch[0];
	}
	x = xmatch[0];
	for (i=1; i<n; i++) {
	    if (jr->auxcol) {
		y = auxmatch[i];
		if (y < xa) {
		    x = xmatch[i];
		}
	    } else {	    
		y = xmatch[i];
		if (y < x) {
		    x = y;
		}
	    }
	}
    } else if (jr->aggr == AGGR_SUM || jr->aggr == AGGR_AVG) {
	x = 0.0;
	for (i=0; i<n; i++) {
	    x += xmatch[i];
	}
	if (jr->aggr == AGGR_AVG) {
	    x /= n;
	}
    }

    return x;
}

/* Handle the case where (a) the value from the right, @rz, is
   actually the coding of a string value, and (b) the LHS series is
   pre-existing and already has a string table attached. The RHS
   coding must be made consistent with that on the left. We reach this
   function only if we've verified that there are string tables on
   both sides, and that @rz is not NA.  
*/

static double maybe_adjust_string_code (series_table *rst,
					series_table *lst,
					double rz, int *err)
{
    const char *rstr = series_table_get_string(rst, rz);
    double lz = series_table_get_value(lst, rstr);

    if (!na(lz)) {
	/* use the LHS encoding */
	rz = lz;
    } else {
	/* we need to append to the LHS string table */
	int n = series_table_add_string(lst, rstr);

	if (n < 0) {
	    *err = E_ALLOC;
	} else {
	    rz = n;
	}
    }

    return rz;
}

static int get_inner_key_values (joiner *jr, int i,
				 const int *ikeyvars,
				 int *pk1, int *pk2,
				 int *missing)
{
    DATASET *dset = jr->l_dset;
    int err = 0;

    *pk1 = *pk2 = 0;

    if (using_auto_keys(jr)) {
	/* using the LHS dataset obs info */
	char obs[12];

	ntodate(obs, i, dset);
	if (calendar_data(dset)) {
	    *pk1 = get_epoch_day(obs);
	} else {
	    /* monthly or quarterly (FIXME others?) */
	    *pk1 = atoi(obs);
	    *pk2 = atoi(obs + 5);
	}
    } else {
	/* using regular LHS key series */
	double dk1, dk2 = 0;
	int k1 = 0, k2 = 0;

	dk1 = dset->Z[ikeyvars[1]][i];
	if (jr->n_keys == 2) {
	    dk2 = dset->Z[ikeyvars[2]][i];
	}
	if (xna(dk1) || xna(dk2)) {
	    *missing = 1;
	} else {
	    k1 = dtoi(dk1, 0, &err);
	    if (!err && jr->n_keys == 2) {
		k2 = dtoi(dk2, 0, &err);
	    }
	}
	if (!err && !*missing) {
	    *pk1 = k1;
	    *pk2 = k2;
	}
    }

    return err;
}

static int aggregate_data (joiner *jr, const int *ikeyvars, int v)
{
    series_table *rst = NULL;
    series_table *lst = NULL;
    DATASET *dset = jr->l_dset;
    double *xmatch = NULL;
    double *auxmatch = NULL;
    int strcheck = 0;
    int i, nmax, key, key2 = 0;
    int err = 0;

#if CDEBUG
    fputs("\naggregate data:\n", stderr);
#endif

    /* find the greatest (primary) key frequency */
    nmax = 0;
    for (i=0; i<jr->n_unique; i++) {
	if (jr->key_freq[i] > nmax) {
	    nmax = jr->key_freq[i];
	}
    }

    if (nmax > 0) {
	xmatch = malloc(nmax * sizeof *xmatch);
	if (xmatch == NULL) {
	    return E_ALLOC;
	}
	if (jr->auxcol) {
	    auxmatch = malloc(nmax * sizeof *auxmatch);
	    if (auxmatch == NULL) {
		free(xmatch);
		return E_ALLOC;
	    }
	}	    
    }

    if (jr->valcol > 0) {
	rst = series_get_string_table(jr->r_dset, jr->valcol);
	lst = series_get_string_table(jr->l_dset, v);
	strcheck = (rst != NULL && lst != NULL);
    }

    /* run through the observations in the current sample range of
       the left-hand dataset, looking for key-matches on the right
    */

    for (i=dset->t1; i<=dset->t2 && !err; i++) {
	int missing = 0;
	double z;

	err = get_inner_key_values(jr, i, ikeyvars, &key, &key2, &missing);
	if (err) {
	    break;
	} else if (missing) {
	    dset->Z[v][i] = NADBL;
	    continue;
	}

	z = aggr_value(jr, key, key2, xmatch, auxmatch, &err);
#if CDEBUG
	if (na(z)) {
	    fprintf(stderr, " aggr_value: got NA (keys=%d,%d, err=%d)\n", 
		    key, key2, err);
	} else {
	    fprintf(stderr, " aggr_value: got %.12g (keys=%d,%d, err=%d)\n", 
		    z, key, key2, err);
	}
#endif
	if (!err && strcheck && !na(z)) {
	    z = maybe_adjust_string_code(rst, lst, z, &err);
	}
	if (!err) {
	    dset->Z[v][i] = z;
	}
    }

    free(xmatch);
    free(auxmatch);

    return err;
}

/* For use when no keys are given: we come here only if we've verified
   that the number of obs on the right matches the number of obs in
   the current sample range on the left.  
*/

static int join_fetch_data (joiner *jr, int v)
{
    series_table *rst = NULL;
    series_table *lst = NULL;
    DATASET *dset = jr->l_dset;
    double z;
    int strcheck = 0;
    int i, err = 0;

    if (jr->valcol > 0) {
	rst = series_get_string_table(jr->r_dset, jr->valcol);
	lst = series_get_string_table(dset, v);
	strcheck = (rst != NULL && lst != NULL);
    }

    for (i=0; i<jr->n_rows && !err; i++) {
	z = jr->rows[i].val;
	if (strcheck && !na(z)) {
	    z = maybe_adjust_string_code(rst, lst, z, &err);
	}
	if (!err) {
	    dset->Z[v][dset->t1 + i] = z;
	}
    }

    return err;
}

static jr_filter *join_filter_new (int *err)
{
    jr_filter *filter = malloc(sizeof *filter);

    if (filter == NULL) {
	*err = E_ALLOC;
    } else {
	filter->expr = NULL;
	filter->val = NULL;
	filter->lhname = NULL;
	filter->rhname = NULL;
	filter->lhval = NADBL;
	filter->rhval = NADBL;
	filter->lhcol = 0;
	filter->rhcol = 0;
	filter->mode = FILT_NUMERIC;
    }

    return filter;
}

static jr_filter *make_genr_filter (const char *s, int *err)
{
    jr_filter *filter = join_filter_new(err);

    if (filter != NULL) {
	char test[VNAMELEN];
	int n, ngot = 0;

	filter->expr = s;
	filter->mode = FILT_GENR;

	while (*s && ngot < 2) {
	    n = gretl_namechar_spn(s);
	    if (n > 0) {
		if (n < VNAMELEN && s[n] != '(') {
		    *test = '\0';
		    strncat(test, s, n);
		    if (!gretl_is_scalar(test)) {
			if (++ngot == 1) {
			    filter->lhname = gretl_strdup(test);
			} else {
			    filter->rhname = gretl_strdup(test);
			}
		    }
		}
		s += n;
	    } else {
		s++;
	    }
	}
    }

    return filter;
}

/* Check for a filter of the MISSMASK type; that is, of the
   form missing(colname), !missing(colname) or ok(colname),
   where "colname" should identify a column in the outer
   data file. Such a filter selects rows on which the
   target series is NA, or not-NA, as the case may be.
*/

static jr_filter *try_missval_filter (const char *s, int *err)
{
    jr_filter *filter = NULL;

    if (!strncmp(s, "missing(", 8) || !strncmp(s, "!missing(", 9) ||
	!strncmp(s, "ok(", 3)) {
	const char *p = strrchr(s, ')');

	if (p != NULL && *(p+1) == '\0' && strchr(s, ')') == p) {
	    int op = *s == 'm' ? B_EQ : B_NEQ;

	    filter = join_filter_new(err);
	    if (filter != NULL) {
		s = strchr(s, '(') + 1;
		filter->lhname = gretl_strndup(s, strlen(s) - 1);
		filter->op = op;
		filter->mode = FILT_MISSMASK;
	    }
	}
    }

    return filter;
}

/* parse a filter string of the form <lhs> <op> <rhs> */

static jr_filter *make_join_filter (const char *s, int *err)
{
    jr_filter *filter = NULL;
    const char *opchars = "=!><";
    char *lhs = NULL, *opstr = NULL, *rhs = NULL;
    size_t nop, len = strlen(s);
    size_t nlhs = strcspn(s, opchars);
    int op = 0;

    if (nlhs == len) {
	*err = E_PARSE;
    } else {
	lhs = gretl_strndup(s, nlhs);
	g_strstrip(lhs);
	nop = strspn(s + nlhs, opchars);
	opstr = gretl_strndup(s + nlhs, nop);
	if (nlhs + nop == len) {
	    *err = E_PARSE;
	} else {
	    rhs = gretl_strdup(s + nlhs + nop);
	    g_strstrip(rhs);
	}

#if CDEBUG
	fprintf(stderr,"lhs = '%s'\n", lhs);
	fprintf(stderr,"op = '%s'\n", opstr);
	fprintf(stderr,"rhs = '%s'\n", rhs);
#endif
    }

    if (!*err) {
	if (!strcmp(opstr, "==")) {
	    op = B_EQ;
	} else if (!strcmp(opstr, "<")) {
	    op = B_LT;
	} else if (!strcmp(opstr, ">")) {
	    op = B_GT;
	} else if (!strcmp(opstr, "<=")) {
	    op = B_LTE;
	} else if (!strcmp(opstr, ">=")) {
	    op = B_GTE;
	} else if (!strcmp(opstr, "!=")) {
	    op = B_NEQ;
	} else {
	    gretl_errmsg_sprintf(_("%s: invalid operator '%s' in filter"), 
				 "join", opstr);
	    *err = E_PARSE;
	}
    }

    if (!*err) {
	filter = join_filter_new(err);
    }

    if (!*err) {
	filter->op = op;
	if (numeric_string(lhs)) {
	    filter->lhval = dot_atof(lhs);
	} else if (gretl_is_scalar(lhs)) {
	    filter->lhval = gretl_scalar_get_value(lhs, NULL);
	} else {
	    filter->lhname = lhs;
	    lhs = NULL;
	}
	if (numeric_string(rhs)) {
	    filter->rhval = dot_atof(rhs);
	} else if (gretl_is_scalar(rhs)) {
	    filter->rhval = gretl_scalar_get_value(rhs, NULL);
	} else {
	    filter->rhname = rhs;
	    rhs = NULL;
	}
    }

    free(lhs);
    free(opstr);
    free(rhs);

    return filter;
}

/* Add a series to hold the join data and return its
   ID number, or -1 on failure. We come here only if
   the target series is not already present in the
   left-hand dataset.
*/

static int get_target_varnum (const char *vname,
			      DATASET *dset,
			      int *err)
{
    int i, targ = -1;

    *err = dataset_add_series(dset, 1);

    if (!*err) {
	targ = dset->v - 1;
	strcpy(dset->varname[targ], vname);
	for (i=0; i<dset->n; i++) {
	    dset->Z[targ][i] = NADBL;
	}
    }

    return targ;
}

/* Parse either one column-name or two comma-separated names out of
   @s. If @s contains a comma, we accept a zero-length name on either
   the left or the right -- but not both -- as indicating that we
   should use the corresponding inner key name.

   If opt contains OPT_K (--tkey) we assume there's just one 
   column name here.
*/

static int process_outer_key (const char *s, int n_keys, 
			      char *name1, char *name2,
			      gretlopt opt)
{
    int n_okeys = 0;
    int err = 0;

    if (opt & OPT_K) {
	strncat(name1, s, CSVSTRLEN - 1);
    } else if (strchr(s, ',') == NULL) {
	/* just one outer key */
	strncat(name1, s, CSVSTRLEN - 1);
	n_okeys = 1;
    } else {
	/* two comma-separated keys */
	int n2 = 0, n1 = strcspn(s, ",");

	if (n1 >= CSVSTRLEN) {
	    err = E_PARSE;
	} else {
	    strncat(name1, s, n1);
	    s += n1 + 1;
	    n2 = strlen(s);
	    if (n2 >= CSVSTRLEN) {
		err = E_PARSE;
	    } else {
		strncat(name2, s, n2);
	    }
	}

	if (!err && n1 == 0 && n2 == 0) {
	    /* both fields empty: wrong */
	    err = E_PARSE;
	}

	if (!err) {
	    n_okeys = 2;
	}
    }

    if (!err && n_okeys != n_keys) {
	err = E_PARSE;
    }

    return err;
}

static int numerical_aggr (joiner *jr, int aggr)
{
    if (aggr == AGGR_SUM || aggr == AGGR_AVG) {
	return 1;
    } else if (aggr == AGGR_MIN || aggr == AGGR_MAX) {
	return jr->auxcol == 0;
    } else {
	return 0;
    }
}

#define lr_mismatch(l,r) ((l > 0 && r == 0) || (r > 0 && l == 0))

/* Run some checks pertaining to the nature of the payload
   (string-valued vs numeric) in relation to the aggregation
   method specified, and the nature of the existing left-hand
   series, if any.
*/

static int join_data_type_check (joiner *jr, int targvar,
				 int aggr)
{
    int lstr = -1, rstr = -1;
    int err = 0;

    if (targvar > 0) {
	/* there's an existing LHS series */
	lstr = series_has_string_table(jr->l_dset, targvar);
	if (lstr && aggr == AGGR_COUNT) {
	    /* count values can't be mixed with strings */
	    err = E_TYPES;
	}
    }

    if (!err && jr->valcol > 0) {
	/* there's a payload variable on the right */
	rstr = series_has_string_table(jr->r_dset, jr->valcol);
	if (rstr && numerical_aggr(jr, aggr)) {
	    /* if the RHS series is string-valued, numerical
	       aggregation methods are not meaningful
	    */
	    err = E_TYPES;
	}
    }

    if (!err && lr_mismatch(lstr, rstr)) {
	/* one of (L, R) is string-valued, but not the other */
	err = E_TYPES;
    }    

    if (!err && jr->auxcol > 0) {
	/* we're using an aux. column for min/max aggregation:
	   that variable cannot be string-valued 
	*/
	if (series_has_string_table(jr->r_dset, jr->auxcol)) {
	    gretl_errmsg_sprintf("'%s' is a string variable: aggregation type "
				 "is not applicable", 
				 jr->r_dset->varname[jr->auxcol]);
	    err = E_TYPES;
	}
    }

    return err;
}

/* Handle the case where the user gave a "%q" conversion specifier
   (which we take to mean quarter). We convert this to %m for use with
   strptime(), but record that the fact that "month means quarter".
*/

static void check_for_quarterly (obskey *auto_keys)
{
    char *s = auto_keys->timefmt;
    int i;

    for (i=0; s[i]; i++) {
	if (s[i] == '%' && s[i+1] == 'q' && (i == 0 || s[i-1] != '%')) {
	    s[i+1] = 'm';
	    auto_keys->m_means_q = 1;
	}
    }
}

/* time-series data on the left, and no explicit keys supplied */

static int auto_keys_check (const DATASET *l_dset,
			    const DATASET *r_dset,
			    gretlopt opt,
			    obskey *auto_keys,
			    int *n_keys)
{
    int pd = l_dset->pd;
    int err = 0;

    if (!dataset_is_time_series(l_dset)) {
	/* On the left we need a time-series dataset */
	err = E_DATA;
	goto bailout;
    }

    if (r_dset->S == NULL && auto_keys->keycol < 0) {
	/* On the right, we need either obs strings or a specified
	   time column */
	err = E_DATA;
	goto bailout;
    }

    if (opt & OPT_T) {
	/* the user should have supplied a time-format spec */
	const char *s = get_optval_string(JOIN, OPT_T);

	if (s == NULL) {
	    err = E_DATA;
	} else {
	    auto_keys->timefmt = gretl_strdup(s);
	    if (auto_keys->timefmt == NULL) {
		err = E_ALLOC;
	    } else {
		if (pd == 4) {
		    check_for_quarterly(auto_keys);
		}
		if (annual_data(l_dset)) {
		    *n_keys = 1;
		} else if (calendar_data(l_dset)) {
		    *n_keys = 1;
		} else {
		    *n_keys = 2;
		}
	    }
	}
    } else {
	/* ISO 8601 defaults */
	if (calendar_data(l_dset)) {
	    auto_keys->timefmt = gretl_strdup("%Y-%m-%d");
	    if (auto_keys->timefmt == NULL) {
		err = E_ALLOC;
	    } else {
		*n_keys = 1;
	    }
	} else if (pd == 12) {
	    auto_keys->timefmt = gretl_strdup("%Y-%m");
	    if (auto_keys->timefmt == NULL) {
		err = E_ALLOC;
	    } else {
		*n_keys = 2;
	    }
	} else if (annual_data(l_dset)) {
	    auto_keys->timefmt = gretl_strdup("%Y");
	    if (auto_keys->timefmt == NULL) {
		err = E_ALLOC;
	    } else {
		*n_keys = 1;
	    }	    
	} else {
	    err = E_PDWRONG;
	}
    }

 bailout:
    
    /* we should flag an error here only if the user
       explicitly requested use of this apparatus,
       by giving --time=<format> (that is, OPT_T)
       and/or --tkey=<colname> (OPT_K)
    */
    if (err && !(opt & (OPT_T | OPT_K))) {
	err = 0;
    }

    return err;
}

static int *process_timecols (const char *s, int *err)
{
    const char *p = s;
    int *list = NULL;
    char *endptr;
    int ti, n = 1;

    while (*p) {
	if (*p == ',') {
	    n++;
	}
	p++;
    }

    list = gretl_list_new(n);
    if (list == NULL) {
	*err = E_ALLOC;
	return NULL;
    }

    n = 0;

    while (*s) {
	ti = (int) strtol(s, &endptr, 10);
	if (*endptr == '\0') {
	    list[++n] = ti;
	    break;
	} else if (*endptr != ',') {
	    *err = E_PARSE;
	    break;
	} else {
	    list[++n] = ti;
	}
	s = endptr + 1;
    }

    if (n == 0 && !*err) {
	*err = E_DATA;
    }

    if (*err) {
	free(list);
	list = NULL;
    } else {
	list[0] = n;
    }

    return list;
}

static void obskey_init (obskey *keys)
{
    keys->timefmt = NULL;
    keys->keycol = -1;
    keys->m_means_q = 0;
    keys->convert = 0;
}

static int aggr_type_check (csvjoin *jspec, int valcol)
{
    if (series_has_string_table(jspec->c->dset, valcol)) {
	gretl_errmsg_sprintf("'%s' is a string variable: aggregation type "
			     "is not applicable", jspec->colnames[JOIN_VAL]);
	return E_TYPES;
    } else {
	return 0;
    }
}

/**
 * join_from_csv:
 * @fname: name of delimited text data file.
 * @varname: name of variable to create or modify.
 * @dset: pointer to dataset.
 * @ikeyvars: list of 1 or 2 "inner" key variables, or NULL.
 * @okey: string specifying "outer" key(s) or NULL.
 * @filtstr: string specifying filter, or NULL.
 * @data: name of outer "payload" column, or NULL.
 * @aggr: aggregation method specifier.
 * @seqval: 1-based sequence number for aggregation, or 0.
 * @auxname: name of auxiliary column for max or min aggregation,
 * or NULL.
 * @timecols: list of time/date columns, or NULL.
 * @opt: may contain OPT_V for verbose operation.
 * @prn: gretl printing struct (or NULL).
 * 
 * Opens a delimited text data file and carries out a "join" operation
 * to pull data into the current working dataset.
 * 
 * Returns: 0 on successful completion, non-zero otherwise.
 */

int join_from_csv (const char *fname,
		   const char *varname,
		   DATASET *dset, 
		   const int *ikeyvars,
		   const char *okey,
		   const char *filtstr,
		   const char *dataname,
		   AggrType aggr,
		   int seqval,
		   const char *auxname,
		   const char *timecols,
		   gretlopt opt,
		   PRN *prn)
{
    csvjoin jspec = {0};
    joiner *jr = NULL;
    jr_filter *filter = NULL;
    int okeyvars[3] = {0, 0, 0};
    int *tcollist = NULL;
    char okeyname1[CSVSTRLEN] = {0};
    char okeyname2[CSVSTRLEN] = {0};
    obskey auto_keys;
    int targvar, orig_v = dset->v;
    int str_keys = 0;
    int str_keys2 = 0;
    int n_keys = 0;
    int err = 0;

    targvar = current_series_index(dset, varname);
    if (targvar == 0) {
	/* can't modify const */
	return E_DATA;
    }    

    if (ikeyvars != NULL) {
	n_keys = ikeyvars[0];
    }

    obskey_init(&auto_keys);

#if CDEBUG
    fputs("*** join_from_csv:\n", stderr);
    fprintf(stderr, " filename = '%s'\n", fname);
    fprintf(stderr, " target series name = '%s'\n", varname);
    if (n_keys > 0) {
	fprintf(stderr, " inner key series ID = %d\n", ikeyvars[1]);
	if (n_keys == 2) {
	    fprintf(stderr, " second inner key series ID = %d\n", ikeyvars[2]);
	}
    }
    if (okey != NULL) {
	fprintf(stderr, " outer key = '%s'\n", okey);
    } else if (n_keys > 0) {
	fprintf(stderr, " outer key = '%s' (from inner key)\n", 
		dset->varname[ikeyvars[1]]);
	if (n_keys == 2) {
	    fprintf(stderr, " second outer key = '%s' (from inner)\n", 
		    dset->varname[ikeyvars[2]]);
	}
    }
    if (filtstr != NULL) {
	fprintf(stderr, " filter = '%s'\n", filtstr);
    }    
    if (dataname != NULL) {
	fprintf(stderr, " source data series = '%s'\n", dataname);
    } else if (aggr != AGGR_COUNT) {
	fprintf(stderr, " source data series: assuming '%s' (from inner varname)\n", 
		varname);
    }
    fprintf(stderr, " aggregation method = %d\n", aggr);
    if (auxname != NULL) {
	fprintf(stderr, " aggr auxiliary column = '%s'\n", auxname);
    }
    if (timecols != NULL) {
	fprintf(stderr, " timecols = '%s'\n", timecols);
    }    
#endif

    if (filtstr != NULL) {
	if (getenv("GENR_FILTER") != NULL) {
	    filter = make_genr_filter(filtstr, &err);
	} else {
	    filter = try_missval_filter(filtstr, &err);
	    if (filter == NULL && !err) {
		filter = make_join_filter(filtstr, &err);
	    }
	}
	if (err) {
	    fprintf(stderr, "join: error %d processing row filter\n", err);
	}
    }

    if (!err && okey != NULL) {
	err = process_outer_key(okey, n_keys, okeyname1, okeyname2, opt);
	if (err) {
	    fprintf(stderr, "join: error %d processing outer key(s)\n", err);
	}
    }

#if 1 /* experimental, still */
    if (!err && timecols != NULL) {
	jspec.tcollist = tcollist = process_timecols(timecols, &err);
    }
#endif

    /* Below: fill out the array of required column names,
       jspec.colnames.  This array has space for JOIN_MAXCOL strings;
       we leave any unneeded elements as NULL.
    */

    if (!err) {
	/* handle the "outer" key column, if any */
	if (*okeyname1 != '\0') {
	    jspec.colnames[JOIN_KEY] = okeyname1;
	} else if (n_keys > 0) {
	    jspec.colnames[JOIN_KEY] = dset->varname[ikeyvars[1]];
	}

	/* the data or "payload" column */
	if (aggr != AGGR_COUNT) {
	    if (dataname != NULL) {
		jspec.colnames[JOIN_VAL] = dataname;
	    } else {
		jspec.colnames[JOIN_VAL] = varname;
	    }
	}

	/* handle filter columns, if applicable */
	if (filter != NULL) {
	    jspec.colnames[JOIN_LHF] = filter->lhname;
	    jspec.colnames[JOIN_RHF] = filter->rhname;
	}

	/* the second outer key, if present */
	if (*okeyname2 != '\0') {
	    jspec.colnames[JOIN_KEY2] = okeyname2;
	} else if (n_keys > 1) {
	    jspec.colnames[JOIN_KEY2] = dset->varname[ikeyvars[2]];
	}

	/* the auxiliary var for aggregation, if present */
	if (auxname != NULL) {
	    jspec.colnames[JOIN_AUX] = auxname;
	}

	err = real_import_csv(fname, dset, NULL, NULL,
			      &jspec, opt, prn);
	if (err) {
	    fprintf(stderr, "join: error %d from real_import_csv\n", err);
	}
    }

    if (!err && aggr != AGGR_COUNT) {
	/* run some sanity tests on the payload (there's no need
	   for a payload with AGGR_COUNT)
	*/
	int valcol = jspec.colnums[JOIN_VAL];

	if (valcol == 0) {
	    gretl_errmsg_sprintf("Series not found, '%s'", jspec.colnames[JOIN_VAL]);
	    err = E_UNKVAR;
	} else if (aggr != AGGR_NONE && aggr != AGGR_SEQ) {
	    err = aggr_type_check(&jspec, valcol);
	}
    }

    if (!err && auxname != NULL && jspec.colnums[JOIN_AUX] == 0) {
	/* an auxiliary variable was specified for aggr but was
	   not found */
	fprintf(stderr, "join: auxiliary column '%s' was not found\n", 
		jspec.colnames[JOIN_AUX]);
	err = E_UNKVAR;
    }	

    if (!err && jspec.colnames[0] != NULL) {
	/* check that outer key was found in the right-hand-side
	   file, and is conformable to the inner key 
	*/
	if (jspec.colnums[JOIN_KEY] > 0) {
	    okeyvars[0] += 1;
	    okeyvars[1] = jspec.colnums[JOIN_KEY];
	}

	if (jspec.colnums[JOIN_KEY2] > 0) {
	    okeyvars[0] += 1;
	    okeyvars[2] = jspec.colnums[JOIN_KEY2];
	}	

	if ((jspec.colnames[JOIN_KEY] != NULL && okeyvars[1] == 0) ||
	    (jspec.colnames[JOIN_KEY2] != NULL && okeyvars[2] == 0)) {
	    fprintf(stderr, "join: error finding outer key columns\n");
	    err = E_DATA;
	} else if (opt & OPT_K) {
	    /* time key on right */
	    int rstr = series_has_string_table(jspec.c->dset, okeyvars[1]);

	    auto_keys.keycol = okeyvars[1];
	    if (!rstr) {
		/* flag the need to convert to string later */
		auto_keys.convert = 1;
	    }
	} else {
	    int lstr = series_has_string_table(dset, ikeyvars[1]);
	    int rstr = series_has_string_table(jspec.c->dset, okeyvars[1]);

	    if (lstr != rstr) {
		if (lstr) {
		    fprintf(stderr, "key 1: string on left but not on right\n");
		} else {
		    fprintf(stderr, "key 1: string on right but not on left\n");
		}
		err = E_TYPES; 
	    } else if (lstr) {
		str_keys = 1;
	    }

	    if (!err && okeyvars[2] > 0) {
		lstr = series_has_string_table(dset, ikeyvars[2]);
		rstr = series_has_string_table(jspec.c->dset, okeyvars[2]);

		if (lstr != rstr) {
		    fprintf(stderr, "key 2: numeric/string mismatch\n");
		    err = E_TYPES; 
		} else if (lstr) {
		    str_keys2 = 1;
		}
	    }		
	}
    }

    if (!err && n_keys == 0 && 
	(dataset_is_time_series(dset) || (opt & OPT_T))) {
	err = auto_keys_check(dset, jspec.c->dset, opt, 
			      &auto_keys, &n_keys);
    }

    if (!err) {
#if CDEBUG > 1
	PRN *eprn = gretl_print_new(GRETL_PRINT_STDERR, NULL);

	pprintf(eprn, "Data extracted from %s:\n", fname);
	printdata(NULL, NULL, jspec.c->dset, OPT_O, eprn);
	gretl_print_destroy(eprn);
#endif
	jr = build_joiner(&jspec, dset, filter, aggr, seqval, &auto_keys, &err);
	if (err) {
	    fprintf(stderr, "join: error %d from build_joiner()\n", err);
	} else if (jr == NULL) {
	    /* no matching data to join */
	    goto bailout;
	}
    }

    if (!err) {
	/* initial check for mash-up of string data and numeric */
	err = join_data_type_check(jr, targvar, aggr);
    }

    if (!err) {
	jr->n_keys = n_keys;
	jr->str_keys = str_keys;
	jr->str_keys2 = str_keys2;
	jr->l_keyno = ikeyvars;
	jr->r_keyno = okeyvars;
	if (jr->n_keys > 0) {
	    err = joiner_sort(jr);
	}	
#if CDEBUG > 1
	if (!err) joiner_print(jr);
#endif
    }

    if (!err && jr->n_keys == 0 && jr->n_rows != sample_size(jr->l_dset)) {
	gretl_errmsg_set(_("Series length does not match the dataset"));
	err = E_DATA;
    }

    if (!err && targvar < 0) {
	targvar = get_target_varnum(varname, dset, &err);
	if (err) {
	    fprintf(stderr, "join: error %d from get_target_varnum()\n", err);
	}	
    }

    if (!err) {
	if (jr->n_keys == 0) {
	    err = join_fetch_data(jr, targvar);
	} else {
	    err = aggregate_data(jr, ikeyvars, targvar);
	}
	if (err) {
	    fprintf(stderr, "join: error %d from aggregate_data()\n", err);
	}
    }

    if (!err && dset->v > orig_v && jr->valcol > 0) {
	/* we got a newly added payload series */
	if (series_has_string_table(jr->r_dset, jr->valcol)) {
	    /* let the new series grab the RHS string table */
	    steal_string_table(jr->l_dset, targvar, jr->r_dset, jr->valcol); 
	}
    }

    if (err) {
	dataset_drop_last_variables(dset, dset->v - orig_v);
    }

 bailout:

    if (auto_keys.timefmt != NULL) {
	free(auto_keys.timefmt);
    }

    csvdata_free(jspec.c);
    joiner_destroy(jr);
    jr_filter_destroy(filter);
    free(tcollist);

    return err;
}

int csv_open_needs_matrix (gretlopt opt)
{
    int ret = 0;

    if (opt & OPT_M) {
	/* --rowmask=matrix */
	ret = 1;
    } else if (opt & OPT_F) {
	/* --fixed-cols=whatever */
	const char *s = get_optval_string(OPEN, OPT_F);

	ret = gretl_is_matrix(s);
    }

    return ret;
}
