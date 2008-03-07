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

/* lexer module for 'genr' and related commands */

#include "genparse.h"
#include "usermat.h"
#include "loop_private.h"
#include "gretl_func.h"
#include "gretl_string_table.h"

#include <glib.h>

#define NUMLEN 32
#define MAXQUOTE 64

#if GENDEBUG
# define LDEBUG 1
#else
# define LDEBUG 0
#endif

const char *wordchars = "abcdefghijklmnopqrstuvwxyz"
                        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                        "0123456789_";

static char *fromdbl (double x)
{ 
    static char num[NUMLEN];
   
    sprintf(num, "%g", x);
    return num;
}

struct str_table {
    int id;
    const char *str;
};

struct str_table consts[] = {
    { CONST_PI,    "pi" },
    { CONST_NA,    "NA" },
    { CONST_WIN32, "WIN32" },
    { 0,        NULL }
};

struct str_table dummies[] = {
    { DUM_NULL,    "null" },
    { DUM_DIAG,    "diag" },
    { DUM_DATASET, "dataset" },
    { 0,        NULL }
};

struct str_table dvars[] = {
    { R_NOBS,      "$nobs" },
    { R_NVARS,     "$nvars" },
    { R_PD,        "$pd" },
    { R_TEST_STAT, "$test" },
    { R_TEST_PVAL, "$pvalue" },
    { R_TEST_LNL,  "$rlnl" },
    { R_INDEX,     "t" },
    { R_INDEX,     "obs" },
    { R_T1,        "$t1" },
    { R_T2,        "$t2" },
    { R_STOPWATCH, "$stopwatch" },
    { R_NSCAN,     "$nscan" },
    { 0,           NULL },
};

struct str_table mvars[] = {
    { M_ESS,     "$ess" },
    { M_T,       "$T" },
    { M_RSQ,     "$rsq" },
    { M_SIGMA,   "$sigma" },
    { M_DF,      "$df" },
    { M_NCOEFF,  "$ncoeff" },
    { M_LNL,     "$lnl" },
    { M_GMMCRIT, "$gmmcrit" },
    { M_AIC,     "$aic" },
    { M_BIC,     "$bic" },
    { M_HQC,     "$hqc" },
    { M_TRSQ,    "$trsq" },
    { M_UHAT,    "$uhat" },
    { M_YHAT,    "$yhat" },
    { M_AHAT,    "$ahat" },
    { M_H,       "$h" },
    { M_COEFF,   "$coeff" },
    { M_SE,      "$stderr" },
    { M_VCV,     "$vcv" },
    { M_RHO,     "$rho" },
    { M_COMPAN,  "$compan" },
    { M_JALPHA,  "$jalpha" }, 
    { M_JBETA,   "$jbeta" },
    { M_JVBETA,  "$jvbeta" },
    { M_JS00,    "$s00" },
    { M_JS11,    "$s11" },
    { M_JS01,    "$s01" },
    { M_HAUSMAN, "$hausman" },
    { M_SARGAN,  "$sargan" },
    { M_SYSGAM,  "$sysGamma" },
    { M_SYSA,    "$sysA" },
    { M_SYSB,    "$sysB" },
    { M_FCAST,   "$fcast" },
    { M_FCERR,   "$fcerr" },
    { 0,         NULL }
};

struct str_table funcs[] = {
    { F_ABS,      "abs" },
    { F_SIN,      "sin" },
    { F_COS,      "cos" },
    { F_TAN,      "tan" },
    { F_ASIN,     "asin" },
    { F_ACOS,     "acos" },
    { F_ATAN,     "atan" },
    { F_LOG,      "log" },
    { F_LOG,      "ln" },
    { F_LOG10,    "log10" },
    { F_LOG2,     "log2" },
    { F_EXP,      "exp" },
    { F_SQRT,     "sqrt" },
    { F_DIFF,     "diff" },
    { F_LDIFF,    "ldiff" },
    { F_SDIFF,    "sdiff" },
    { F_LLAG,     "lags" },
    { F_TOINT,    "int" },
    { F_ROUND,    "round" },
    { F_CEIL,     "ceil" },
    { F_FLOOR,    "floor" },
    { F_SORT,     "sort" }, 
    { F_DSORT,    "dsort" }, 
    { F_SORTBY,   "sortby" }, 
    { F_RANKING,  "ranking" },
    { F_ODEV,     "orthdev" },
    { F_NOBS,     "nobs" },
    { F_T1,       "firstobs" },
    { F_T2,       "lastobs" },
    { F_RUNIFORM, "uniform" }, 
    { F_RNORMAL,  "normal" }, 
    { F_RPOISSON, "genpois" },
    { F_CUM,      "cum" }, 
    { F_MISSING,  "missing" },
    { F_DATAOK,   "ok" },        /* opposite of missing */
    { F_MISSZERO, "misszero" },
    { F_LRVAR,    "lrvar" },
    { F_QUANTILE, "quantile" },
    { F_MEDIAN,   "median" },
    { F_GINI,     "gini" },
    { F_ZEROMISS, "zeromiss" },
    { F_SUM,      "sum" },
    { F_MEAN,     "mean" },
    { F_MIN,      "min" },
    { F_MAX,      "max" },
    { F_SD,       "sd" },
    { F_VCE,      "var" },
    { F_SST,      "sst" },
    { F_CNORM,    "cnorm" },
    { F_DNORM,    "dnorm" },
    { F_QNORM,    "qnorm" },
    { F_GAMMA,    "gammafun" },
    { F_LNGAMMA,  "lngamma" },
    { F_RESAMPLE, "resample" },
    { F_PMEAN,    "pmean" },     /* panel mean */
    { F_PSD,      "psd" },       /* panel std dev */
    { F_HPFILT,   "hpfilt" },    /* Hodrick-Prescott filter */
    { F_BKFILT,   "bkfilt" },    /* Baxter-King filter */
    { F_FRACDIFF, "fracdiff" },  /* fractional difference */
    { F_COV,      "cov" },
    { F_COR,      "corr" },
    { F_MOVAVG,   "movavg" },
    { F_IMAT,     "I" },
    { F_ZEROS,    "zeros" },
    { F_ONES,     "ones" },
    { F_SEQ,      "seq" },
    { F_MUNIF,    "muniform" },
    { F_MNORM,    "mnormal" },
    { F_SUMR,     "sumr" },
    { F_SUMC,     "sumc" },
    { F_MEANR,    "meanr" },
    { F_MEANC,    "meanc" },
    { F_MINC,     "minc" },
    { F_MAXC,     "maxc" },
    { F_MINR,     "minr" },
    { F_MAXR,     "maxr" },
    { F_IMINC,    "iminc" },
    { F_IMAXC,    "imaxc" },
    { F_IMINR,    "iminr" },
    { F_IMAXR,    "imaxr" }, 
    { F_FFT,      "fft" },
    { F_FFTI,     "ffti" },
    { F_CMULT,    "cmult" },
    { F_CDIV,     "cdiv" },
    { F_MCOV,     "mcov" },
    { F_MCORR,    "mcorr" },
    { F_MXTAB,    "mxtab" },
    { F_CDEMEAN,  "cdemean" },
    { F_CHOL,     "cholesky" },
    { F_INV,      "inv" },
    { F_INVPD,    "invpd" },
    { F_GINV,     "ginv" },
    { F_DIAG,     "diag" },
    { F_TRANSP,   "transp" },
    { F_VEC,      "vec" },
    { F_VECH,     "vech" },
    { F_UNVECH,   "unvech" },
    { F_UPPER,    "upper" },
    { F_LOWER,    "lower" },
    { F_ROWS,     "rows" },
    { F_COLS,     "cols" },
    { F_DET,      "det" },
    { F_LDET,     "ldet" },
    { F_TRACE,    "tr" },
    { F_NORM1,    "onenorm" },
    { F_INFNORM,  "infnorm" },
    { F_RCOND,    "rcond" },
    { F_RANK,     "rank" },
    { F_QFORM,    "qform" },
    { F_MLAG,     "mlag" },
    { F_QR,       "qrdecomp" },
    { F_EIGSYM,   "eigensym" },
    { F_EIGGEN,   "eigengen" },
    { F_NULLSPC,  "nullspace" },
    { F_PRINCOMP, "princomp" },
    { F_MEXP,     "mexp" },
    { F_FDJAC,    "fdjac" },
    { F_BFGSMAX,  "BFGSmax" },
    { F_OBSNUM,   "obsnum" },
    { F_ISSERIES, "isseries" },
    { F_ISLIST,   "islist" },
    { F_ISSTRING, "isstring" },
    { F_ISNULL,   "isnull" },
    { F_LISTLEN,  "nelem" },
    { F_CDF,      "cdf" },
    { F_INVCDF,   "invcdf" },
    { F_PVAL,     "pvalue" },
    { F_CRIT,     "critical" },
    { F_RANDGEN,  "randgen" },
    { F_MAKEMASK, "makemask" },
    { F_VALUES,   "values" },
    { F_MSHAPE,   "mshape" },
    { F_SVD,      "svd" },
    { F_MOLS,     "mols" },
    { F_MREAD,    "mread" },
    { F_MWRITE,   "mwrite" },
    { F_MRSEL,    "selifr" },
    { F_MCSEL,    "selifc" },
    { F_POLROOTS, "polroots" },
    { F_DUMIFY,   "dummify" },
    { F_WMEAN,    "wmean" },
    { F_WVAR,     "wvar" },
    { F_WSD,      "wsd" },
    { F_XPX,      "xpx" },
    { F_FILTER,   "filter" },
    { F_TRIMR,    "trimr" },
    { F_GETENV,   "getenv" },
    { F_ARGNAME,  "argname" },
    { F_OBSLABEL, "obslabel" },
    { F_READFILE, "readfile" },
    { F_BACKTICK, "grab" },
    { F_STRSTR,   "strstr" },
    { F_STRLEN,   "strlen" },
    { F_VARNAME,  "varname" },
    { 0,        NULL }
};

struct str_table func_alias[] = {
    { F_GAMMA,     "gammafunc" },
    { F_GAMMA,     "gamma" },
    { F_RPOISSON,  "poisson" },
    { F_PVAL,      "pval" },
    { F_LOG,       "logs" },
    { F_OBSLABEL,  "date" },
    { F_BACKTICK,  "$" },
    { 0,          NULL }
};

int const_lookup (const char *s)
{
    int i;

    for (i=0; consts[i].id != 0; i++) {
	if (!strcmp(s, consts[i].str)) {
	    return consts[i].id;
	}
    }

    return 0;
}

const char *constname (int c)
{
    int i;

    for (i=0; consts[i].id != 0; i++) {
	if (c == consts[i].id) {
	    return consts[i].str;
	}
    }

    return "unknown";
}

static GHashTable *gretl_function_hash_init (void)
{
    GHashTable *ht;
    int i;

    ht = g_hash_table_new(g_str_hash, g_str_equal);

    for (i=0; funcs[i].str != NULL; i++) {
	g_hash_table_insert(ht, (gpointer) funcs[i].str, 
			    GINT_TO_POINTER(funcs[i].id));
    }

    return ht;
}

enum {
    NO_ALIAS,
    ALLOW_ALIAS
};

static int real_function_lookup (const char *s, int a)
{
    static GHashTable *fht;
    gpointer p;
    int ret = 0;

    if (s == NULL) {
	/* cleanup signal */
	if (fht != NULL) {
	    g_hash_table_destroy(fht);
	    fht = NULL;
	}
	return 0;
    }

    if (fht == NULL) {
	fht = gretl_function_hash_init();
    }
    
    p = g_hash_table_lookup(fht, s);
    if (p != NULL) {
	ret = GPOINTER_TO_INT(p);
    }

    if (ret == 0 && a == ALLOW_ALIAS) {
	int i;

	for (i=0; func_alias[i].id != 0; i++) {
	    if (!strcmp(s, func_alias[i].str)) {
		return func_alias[i].id;
	    }
	} 
    }   

    return ret;
}

void gretl_function_hash_cleanup (void)
{
    real_function_lookup(NULL, 0);
}

int function_lookup (const char *s)
{
    return real_function_lookup(s, NO_ALIAS);
}

static int function_lookup_with_alias (const char *s)
{
    return real_function_lookup(s, ALLOW_ALIAS);
}

static const char *funname (int t)
{
    int i;

    for (i=0; funcs[i].id != 0; i++) {
	if (t == funcs[i].id) {
	    return funcs[i].str;
	}
    }

    return "unknown";
}

/* for external purposes (.lang file, manual) */

int gen_func_count (void)
{
    int i;

    for (i=0; funcs[i].id != 0; i++) ;
    return i;
}

const char *gen_func_name (int i)
{
    return funcs[i].str;
}

int model_var_count (void)
{
    int i;

    for (i=0; mvars[i].id != 0; i++) ;
    return i;
}

const char *model_var_name (int i)
{
    return mvars[i].str;
}

int data_var_count (void)
{
    int i, n = 0;

    for (i=0; dvars[i].id != 0; i++) {
	if (dvars[i].str[0] == '$') {
	    n++;
	}
    }

    return n;
}

const char *data_var_name (int i)
{
    return dvars[i].str;
}

/* end external stuff */

static int dummy_lookup (const char *s)
{
    int i;

    for (i=0; dummies[i].id != 0; i++) {
	if (!strcmp(s, dummies[i].str)) {
	    return dummies[i].id;
	}
    }

    return 0;
}

const char *dumname (int t)
{
    int i;

    for (i=0; dummies[i].id != 0; i++) {
	if (t == dummies[i].id) {
	    return dummies[i].str;
	}
    }

    return "unknown";
}

static int dvar_lookup (const char *s)
{
    int i;

    for (i=0; dvars[i].id != 0; i++) {
	if (!strcmp(s, dvars[i].str)) {
	    return dvars[i].id;
	}
    }

    return 0;
}

const char *dvarname (int t)
{
    int i;

    for (i=0; dvars[i].id != 0; i++) {
	if (t == dvars[i].id) {
	    return dvars[i].str;
	}
    }

    return "unknown";
}

static int mvar_lookup (const char *s)
{
    int i;

    for (i=0; mvars[i].id != 0; i++) {
	if (!strcmp(s, mvars[i].str)) {
	    return mvars[i].id;
	}
    }

    if (!strcmp(s, "$nrsq")) {
	/* alias */
	return M_TRSQ;
    }

    return 0;
}

const char *mvarname (int t)
{
    int i;

    for (i=0; mvars[i].id != 0; i++) {
	if (t == mvars[i].id) {
	    return mvars[i].str;
	}
    }

    return "unknown";
}

int genr_function_word (const char *s)
{
    int ret = 0;

    ret = real_function_lookup(s, NO_ALIAS);
    if (!ret) {
	ret = dvar_lookup(s);
    }
    if (!ret) {
	ret = mvar_lookup(s);
    }

    return ret;
}

static void undefined_symbol_error (const char *s, parser *p)
{
    parser_print_input(p);
    pprintf(p->prn, _("The symbol '%s' is undefined\n"), s);
    sprintf(gretl_errmsg, _("The symbol '%s' is undefined\n"), s);
    p->err = E_UNKVAR;
}

static void function_noargs_error (const char *s, parser *p)
{
    parser_print_input(p);
    pprintf(p->prn, _("'%s': no argument was given\n"), s);
    sprintf(gretl_errmsg, _("'%s': no argument was given\n"), s);
    p->err = 1;
}

void context_error (int c, parser *p)
{
    parser_print_input(p);
    if (c != 0) {
	pprintf(p->prn, _("The symbol '%c' is not valid in this context\n"), c);
    } else {
	pprintf(p->prn, _("The symbol '%s' is not valid in this context\n"), 
		getsymb(p->sym, p));
    }
    if (p->err == 0) {
	p->err = 1;
    }
}

static char *get_quoted_string (parser *p)
{
    int n = parser_charpos(p, '"');
    char *s = NULL;

    if (n >= 0) {
	s = gretl_strndup(p->point, n);
	parser_advance(p, n + 1);
    } else {
	parser_print_input(p);
	pprintf(p->prn, _("Unmatched '%c'\n"), '"');
	p->err = E_PARSE;
    }

    return s;
}

static int might_be_date_string (const char *s, int n)
{
    char test[12];
    int y, m, d;

#if LDEBUG
    fprintf(stderr, "might_be_date_string: s='%s', n=%d\n", s, n);
#endif
    
    if (n > 10) {
	return 0;
    }

    *test = 0;
    strncat(test, s, n);

    if (strspn(s, "1234567890") == n) {
	/* plain integer */
	return 1;
    } else if (sscanf(s, "%d:%d", &y, &m) == 2) {
	/* quarterly, monthly date */
	return 1;
    } else if (sscanf(s, "%d/%d/%d", &y, &m, &d) == 3) {
	/* daily date */
	return 1;
    }

    return 0;
}

NODE *obs_node (parser *p)
{
    NODE *ret = NULL;
    char word[OBSLEN + 2] = {0};
    const char *s = p->point - 1;
    int close;
    int special = 0;
    int t = -1;

    close = haschar(']', s);

#if LDEBUG
    fprintf(stderr, "obs_node: s='%s', ch='%c', close=%d\n", 
	    s, (char) p->ch, close);
#endif

    if (close == 0) {
	pprintf(p->prn, _("Empty observation []\n"));
	p->err = E_PARSE;
    } else if (close < 0) {
	pprintf(p->prn, _("Unmatched '%c'\n"), '[');
	p->err = E_PARSE;
    } else if (*s == '"' && close < OBSLEN + 2 &&
	       haschar('"', s+1) == close - 2) {
	/* quoted observation label? */
	strncat(word, s, close);
	special = 1;
    } else if (might_be_date_string(s, close)) {
	strncat(word, s, close);
	special = 1;
    } 

    if (special && !p->err) {
	t = get_t_from_obs_string(word, (const double **) *p->Z, 
				  p->dinfo);
	if (t >= 0) {
	    /* convert to use-style 1-based index */
	    t++;
	}
    }

    if (t > 0) {
	parser_advance(p, close - 1);
	lex(p);
	ret = newdbl(t);
    } else if (!p->err) {
#if LDEBUG
	fprintf(stderr, "obs_node: first try failed, going for expr\n");
#endif
	lex(p);
	ret = expr(p);
    }

    return ret;
}

static void look_up_string_variable (const char *s, parser *p)
{
    const char *val = get_string_by_name(s + 1);

    if (val != NULL) {
	p->idstr = gretl_strdup(val);
	if (p->idstr == NULL) {
	    p->err = E_ALLOC;
	} else {
	    p->sym = STR;
	}
    } else {
	undefined_symbol_error(s, p);
    }
}

static void look_up_dollar_word (const char *s, parser *p)
{
    p->idnum = dvar_lookup(s);
    if (p->idnum > 0) {
	p->sym = DVAR;
    } else {
	p->idnum = mvar_lookup(s);
	if (p->idnum > 0) {
	    p->sym = MVAR;
	} else {
	    undefined_symbol_error(s, p);
	}
    }
}

static void look_up_word (const char *s, parser *p)
{
    int fsym, err = 0;

    fsym = p->sym = function_lookup_with_alias(s);

    if (p->sym == 0 || p->ch != '(') {
	p->idnum = const_lookup(s);
	if (p->idnum > 0) {
	    p->sym = CON;
	} else {
	    p->idnum = dummy_lookup(s);
	    if (p->idnum > 0) {
		p->sym = DUM;
	    } else {
		p->idnum = varindex(p->dinfo, s);
		if (p->idnum < p->dinfo->v) {
		    p->sym = (var_is_scalar(p->dinfo, p->idnum))?
			USCALAR : USERIES;
		} else if (!strcmp(s, "time")) {
		    p->sym = DUM;
		    p->idnum = DUM_TREND;
		} else if (get_matrix_by_name(s)) {
		    p->sym = UMAT;
		    p->idstr = gretl_strdup(s);
		} else if (gretl_get_object_by_name(s)) {
		    p->sym = UOBJ;
		    p->idstr = gretl_strdup(s);
		} else if (get_list_by_name(s)) {
		    p->sym = LIST;
		    p->idstr = gretl_strdup(s);
		} else if (get_user_function_by_name(s)) {
		    p->sym = UFUN;
		    p->idstr = gretl_strdup(s);
		} else if (string_is_defined(s)) {
		    p->sym = STR;
		    p->idstr = gretl_strdup(get_string_by_name(s));
		} else if (p->targ == LIST &&
			   varname_match_any(p->dinfo, s)) {
		    p->sym = LIST;
		    p->idstr = gretl_strdup(s);
		} else {
		    err = 1;
		}
	    }
	}
    }

    if (err) {
	if (fsym) {
	    function_noargs_error(s, p);
	} else {
	    undefined_symbol_error(s, p);
	}
    }
}

#define could_be_matrix(t) (model_data_matrix(t) || t == M_UHAT)

static void word_check_next_char (const char *s, parser *p)
{
#if LDEBUG
    if (p->ch) fprintf(stderr, "word_check_next_char: ch = '%c'\n", p->ch);
    else fprintf(stderr, "word_check_next_char: ch = NUL\n");
#endif

    if (p->ch == '(') {
	/* series (lag) or function */
	if (p->sym == USERIES) {
	    if (p->idnum == p->lh.v) {
		p->flags |= P_AUTOREG;
	    }
	    p->sym = LAG;
	} else if (p->sym == MVAR && model_data_matrix(p->idnum)) {
	    /* old-style "$coeff(x1)" etc. */
	    p->sym = DMSTR;
	} else if (!func1_symb(p->sym) && !func2_symb(p->sym) &&
		   !funcn_symb(p->sym) && p->sym != UFUN) {
	    p->err = 1;
	} 
    } else if (p->ch == '[') {
	if (p->sym == UMAT) {
	    /* slice of user matrix */
	    p->sym = MSL;
	} else if (p->sym == MVAR && could_be_matrix(p->idnum)) {
	    /* slice of $ matrix */
	    p->sym = DMSL;
	} else if (p->sym == USERIES) {
	    /* observation from series */
	    p->sym = OBS;
	} else {
	    p->err = 1;
	} 
    } else if (p->ch == '.' && parser_charpos(p, '$') == 0) {
	if (p->sym == UOBJ) {
	    /* name of saved object followed by dollar variable? */
	    p->sym = OVAR;
	} else {
	    p->err = 1;
	}	    
    }

    if (p->err) {
	context_error(p->ch, p);
    }
}

static int is_word_char (parser *p)
{
    if (strchr(wordchars, p->ch) != NULL) {
	return 1;
    } else if (p->targ == LIST && p->ch == '*') {
	return 1;
    }

    return 0;
}

static void getword (parser *p)
{  
    char word[32];
    int i = 0;

    /* we know the first char is acceptable (and might be '$' or '@') */
    word[i++] = p->ch;
    parser_getc(p);

    while (p->ch != 0 && is_word_char(p) && i < 31) {
	word[i++] = p->ch;
	parser_getc(p);
    }

    word[i] = '\0';

#if LDEBUG
    fprintf(stderr, "getword: word = '%s'\n", word);
#endif

    while (p->ch != 0 && strchr(wordchars, p->ch) != NULL) {
	/* flush excess word characters */
	parser_getc(p);
    }

    if (p->getstr) {
	/* uninterpreted string wanted */
	p->sym = STR;
	p->idstr = gretl_strdup(word);
	p->getstr = 0;
	return;
    }

    /* handle loop index scalar */
    if (word[1] == '\0' && is_active_index_loop_char(word[0])) {
	p->sym = LOOPIDX;
	p->idstr = gretl_strdup(word);
	return;
    }

    if ((*word == '$' && word[1]) || !strcmp(word, "t") || !strcmp(word, "obs")) {
	look_up_dollar_word(word, p);
    } else if (*word == '@') {
	look_up_string_variable(word, p);
    } else {
	look_up_word(word, p);
    }

    if (!p->err && *word != '@') {
	word_check_next_char(word, p);
    }

#if LDEBUG
    fprintf(stderr, "getword: p->err = %d\n", p->err);
#endif
}

static int doing_matrix_slice;

void set_matrix_slice_on (void)
{
    doing_matrix_slice = 1;
}

void set_matrix_slice_off (void)
{
    doing_matrix_slice = 0;
}

static int colon_ok (char *s, int n)
{
    int i;

    if (doing_matrix_slice) {
	/* colon is a separator in this context */
#if LDEBUG
	fprintf(stderr, "colon_ok: doing matrix slice\n");
#endif
	return 0;
    }

    if (n != 1 && n != 3) {
	return 0;
    }

    for (i=0; i<=n; i++) {
	if (!isdigit(s[i])) {
	    return 0;
	}
    }

    return 1;
}

/* below: we're testing 'ch' for validity, given what we've already
   packed into string 's' up to element 'i'
*/

static int ok_dbl_char (int ch, char *s, int i)
{
    if (i < 0) {
	return 1;
    }

    if (ch >= '0' && ch <= '9') {
	return 1;
    }

    switch (ch) {
    case '+':
    case '-':
	return s[i] == 'e' || s[i] == 'E';
    case '.':
	return !strchr(s, '.') && !strchr(s, ':') &&
	    !strchr(s, 'e') && !strchr(s, 'E');
    case 'e':
    case 'E':
	return !strchr(s, 'e') && !strchr(s, 'E') && 
	    !strchr(s, ':');
    case ':':
	/* allow for obs numbers in the form, e.g., "1995:10" */
	return colon_ok(s, i);
    default:
	break;
    }

    return 0;
}

static double getdbl (parser *p)
{
    char xstr[NUMLEN] = {0};
    double d = NADBL;
    int i = 0;

    while (ok_dbl_char(p->ch, xstr, i - 1) && i < NUMLEN - 1) {
	xstr[i++] = p->ch;
	parser_getc(p);
    }  

    while (p->ch >= '0' && p->ch <= '9') {
	/* flush excess numeric characters */
	parser_getc(p);
    } 

#if LDEBUG
    fprintf(stderr, "getdbl: xstr = '%s'\n", xstr);
#endif
    
    if (strchr(xstr, ':')) {
	if (p->dinfo->pd == 1) {
	    p->err = E_PDWRONG;
	} else {
	    d = (double) dateton(xstr, p->dinfo);
	    if (d < 0) {
		p->err = E_DATA;
		d = NADBL;
	    } else {
		d += 1.0;
	    }
	}
    } else {
	d = dot_atof(xstr);
    }
    
    return d;
}

#if 0 /* later? */
static void deprecation_note (parser *p)
{
    if (p->sym == B_AND) {
	pprintf(p->prn, "Note: the recommended form for logical "
		"AND is '&&'\n");
    } else if (p->sym == B_OR) {
	pprintf(p->prn, "Note: the recommended form for logical "
		"OR is '||'\n");
    }
}
#endif

#define matrix_gen(p) (p->lh.t == MAT || p->targ == MAT)

#define unary_context(p) (p->sym < F2_MAX || p->sym == COM)

#define lag_range_sym(p) (p->ch == 't' && *p->point == 'o' && \
			  *(p->point + 1) == ' ')

void lex (parser *p)
{
#if LDEBUG
    if (p->ch) fprintf(stderr, "lex: p->ch = '%c'\n", p->ch);
    else fprintf(stderr, "lex: p->ch = NUL\n");
#endif

    if (p->ch == 0) {
	p->sym = EOT;
	return;
    }

    while (p->ch != 0) {
	switch (p->ch) {
	case ' ':
	case '\t':
	case '\r':
        case '\n': 
	    parser_getc(p);
	    break;
        case '+': 
	    p->sym = B_ADD;
	    parser_getc(p);
	    return;
        case '-': 
	    p->sym = B_SUB;
	    parser_getc(p);
	    return;
        case '*': 
	    if (p->targ == LIST) {
		/* treat '*' as wildcard */
		getword(p);
		return;
	    }
	    parser_getc(p);
	    if (p->ch == '*') {
		p->sym = B_KRON;
		parser_getc(p);
	    } else {
		p->sym = B_MUL;
	    }
	    return;
	case '\'':
	    p->sym = B_TRMUL;
	    parser_getc(p);
	    return;
        case '/': 
	    p->sym = B_DIV;
	    parser_getc(p);
	    return;
        case '%': 
	    p->sym = B_MOD;
	    parser_getc(p);
	    return;
        case '^': 
	    p->sym = B_POW;
	    parser_getc(p);
	    return;
        case '&': 
#if 0 /* for later, but modified */
	    if (unary_context(p)) {
		p->sym = U_ADDR;
		parser_getc(p);
		return;
	    }
	    p->sym = B_AND;
	    parser_getc(p);
	    if (p->ch == '&') {
		parser_getc(p);
	    } else {
		deprecation_note(p);
	    }
#else
	    p->sym = B_AND;
	    parser_getc(p);
	    if (p->ch == '&') {
		/* make "&&" equal to "&" */
		parser_getc(p);
	    }
#endif
	    return;
        case '|': 
	    p->sym = (matrix_gen(p))? B_MRCAT : B_OR;
	    parser_getc(p);
	    if (p->ch == '|') {
		p->sym = B_OR;
		parser_getc(p);
	    }
	    return;
        case '!': 
	    parser_getc(p);
	    if (p->ch == '=') {
		p->sym = B_NEQ;
		parser_getc(p);
	    } else {
		p->sym = U_NOT;
	    }
	    return;
        case '=': 
	    parser_getc(p);
	    if (p->ch == '=') {
		parser_getc(p);
	    }
	    p->sym = B_EQ;
	    return;
        case '>': 
	    parser_getc(p);
	    if (p->ch == '=') {
		p->sym = B_GTE;
		parser_getc(p);
	    } else {
		p->sym = B_GT;
	    }
	    return;
        case '<': 
	    parser_getc(p);
	    if (p->ch == '=') {
		p->sym = B_LTE;
		parser_getc(p);
	    } else if (p->ch == '>') {
		p->sym = B_NEQ;
		parser_getc(p);
	    } else {
		p->sym = B_LT;
	    }
	    return;
        case '(': 
	    p->sym = G_LPR;
	    parser_getc(p);
	    return;
        case ')': 
	    p->sym = G_RPR;
	    parser_getc(p);
	    return;
        case '[': 
	    p->sym = G_LBR;
	    parser_getc(p);
	    return;
        case '{': 
	    p->sym = G_LCB;
	    parser_getc(p);
	    return;
        case '}': 
	    p->sym = G_RCB;
	    parser_getc(p);
	    return;
        case ']': 
	    p->sym = G_RBR;
	    parser_getc(p);
	    return;
        case '~':
	    p->sym = B_MCCAT;
	    parser_getc(p);
	    return;
        case '`': 
	    p->sym = B_MRCAT;
	    parser_getc(p);
	    return;
        case ',': 
	    p->sym = P_COM;
	    parser_getc(p);
	    return;
        case ';': 
	    p->sym = P_SEMI;
	    parser_getc(p);
	    return;
        case ':': 
	    p->sym = P_COL;
	    parser_getc(p);
	    return;
        case '?': 
	    p->sym = QUERY;
	    parser_getc(p);
	    return;
	case '.':
	    if (*p->point == '$') {
		p->sym = P_DOT;
		parser_getc(p);
		return;
	    }
	    parser_getc(p);
	    if (p->ch == '*') {
		p->sym = B_DOTMULT;
		parser_getc(p);
		return;
	    } else if (p->ch == '/') {
		p->sym = B_DOTDIV;
		parser_getc(p);
		return;
	    } else if (p->ch == '^') {
		p->sym = B_DOTPOW;
		parser_getc(p);
		return;
	    } else if (p->ch == '+') {
		p->sym = B_DOTADD;
		parser_getc(p);
		return;
	    } else if (p->ch == '-') {
		p->sym = B_DOTSUB;
		parser_getc(p);
		return;
	    } else if (p->ch == '=') {
		p->sym = B_DOTEQ;
		parser_getc(p);
		return;
	    } else if (p->ch == '>') {
		p->sym = B_DOTGT;
		parser_getc(p);
		return;
	    } else if (p->ch == '<') {
		p->sym = B_DOTLT;
		parser_getc(p);
		return;
	    } else {
		/* not a "dot operator", back up */
		parser_ungetc(p);
	    }
        default: 
	    if (p->targ == LIST && lag_range_sym(p)) {
		p->sym = B_RANGE;
		parser_getc(p);
		parser_getc(p);
		return;
	    }
	    if (bare_data_type(p->sym) || closing_sym(p->sym) ||
		(p->targ == LIST && p->sym == LAG)) {
		p->sym = B_LCAT;
		return;
	    }
	    if (isdigit(p->ch) || (p->ch == '.' && isdigit(*p->point))) {
		p->xval = getdbl(p);
		p->sym = NUM;
		return;
	    } else if (islower(p->ch) || isupper(p->ch) || 
		       p->ch == '$' || p->ch == '@') {
		getword(p);
		return;
	    } else if (p->ch == '"') {
		p->idstr = get_quoted_string(p);
		p->sym = STR;
		return;
	    } else {
		parser_print_input(p);
		pprintf(p->prn, _("Invalid character '%c'\n"), p->ch);
		p->err = 1;
		return;
	    }
	} /* end ch switch */
    } /* end while ch != 0 */
}

const char *getsymb (int t, const parser *p)
{  
    if ((t > F1_MIN && t < F1_MAX) ||
	(t > F1_MAX && t < F2_MAX) ||
	(t > F2_MAX && t < FN_MAX)) {
	return funname(t);
    }

    if (t == EOT) {
	return "";
    }

    /* yes, well */
    if (t == OBS) {
	return "OBS";
    } else if (t == MSL) {
	return "MSL";
    } else if (t == DMSL) {
	return "DMSL";
    } else if (t == DMSTR) {
	return "DMSTR";
    } else if (t == MSL2) {
	return "MSL2";
    } else if (t == MSPEC) {
	return "MSPEC";
    } else if (t == SUBSL) {
	return "SUBSL";
    } else if (t == MDEF) {
	return "MDEF";
    } else if (t == FARGS) {
	return "FARGS";
    } else if (t == LIST) {
	return "LIST";
    } else if (t == OVAR) {
	return "OVAR";
    }

    if (p != NULL) {
	if (t == NUM) {
	    return fromdbl(p->xval); 
	} else if (t == USCALAR || t == USERIES) {
	    return p->dinfo->varname[p->idnum];
	} else if (t == UMAT || t == UOBJ ||
		   t == LOOPIDX) {
	    return p->idstr;
	} else if (t == CON) {
	    return constname(p->idnum);
	} else if (t == DUM) {
	    return dumname(p->idnum);
	} else if (t == DVAR) {
	    return dvarname(p->idnum);
	} else if (t == MVAR) {
	    return mvarname(p->idnum);
	} else if (t == UFUN) {
	    return p->idstr;
	} else if (t == STR) {
	    return p->idstr;
	}
    } 

    switch (t) {
    case B_ASN:
	return "=";
    case B_ADD: 
    case U_POS:
	return "+";
    case B_SUB: 
    case U_NEG:
	return "-";
    case B_MUL: 
	return "*";
    case B_TRMUL: 
	return "'";
    case B_DIV: 
	return "/";
    case B_MOD: 
	return "%";
    case B_POW: 
	return "^";
    case B_EQ: 
	return "=";
    case B_NEQ: 
	return "!=";
    case B_GT: 
	return ">";
    case B_LT: 
	return "<";
    case B_GTE: 
	return ">=";
    case B_LTE: 
	return "<=";
    case B_AND: 
	return "&&";
    case B_RANGE:
	return " to ";
    case U_ADDR:
	return "&";
    case B_OR: 
	return "||";	
    case U_NOT: 
	return "!";
    case G_LPR: 
	return "(";
    case G_RPR: 
	return ")";
    case G_LBR: 
	return "[";
    case G_RBR: 
	return "]";
    case G_LCB: 
	return "{";
    case G_RCB: 
	return "}";
    case B_DOTMULT: 
	return ".*";
    case B_DOTDIV: 
	return "./";
    case B_DOTPOW: 
	return ".^";
    case B_DOTADD: 
	return ".+";
    case B_DOTSUB: 
	return ".-";
    case B_DOTEQ: 
	return ".=";
    case B_DOTGT: 
	return ".>";
    case B_DOTLT: 
	return ".<";
    case B_KRON: 
	return "**";
    case B_MCCAT: 
	return "~";
    case B_MRCAT: 
    case B_LCAT:
	return "|";
    case P_COM: 
	return ",";
    case P_DOT: 
	return ".";
    case P_SEMI: 
	return ";";
    case P_COL: 
	return ":";
    case QUERY: 
	return "?";
    case LAG:
	return "lag";
    default: 
	break;
    }

    return "unknown";
}


