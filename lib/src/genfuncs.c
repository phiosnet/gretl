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

/* various functions called by 'genr' */

#include "genparse.h"
#include "libset.h"
#include "monte_carlo.h"
#include "gretl_string_table.h"
#include "gretl_fft.h"
#include "estim_private.h"
#include "../../cephes/cephes.h"

#include <errno.h>

int sort_series (const double *x, double *y, int f, 
		 const DATAINFO *pdinfo)
{
    double *z = NULL;
    int n = sample_size(pdinfo);
    int i, t;

    z = malloc(n * sizeof *z);
    if (z == NULL) {
	return E_ALLOC;
    }

    i = 0;
    for (t=pdinfo->t1; t<=pdinfo->t2; t++) {
	if (!na(x[t])) {
	    z[i++] = x[t];
	}
    }

    if (f == F_DSORT) {
	qsort(z, i, sizeof *z, gretl_inverse_compare_doubles);
    } else {
	qsort(z, i, sizeof *z, gretl_compare_doubles);
    }

    i = 0;
    for (t=pdinfo->t1; t<=pdinfo->t2; t++) {
	if (na(x[t])) {
	    y[t] = NADBL;
	} else {
	    y[t] = z[i++];
	}
    }

    free(z);

    return 0;
}

struct pair_sorter {
    double x;
    double y;
};

/* sort the series y by the values of x, putting the result
   into z */

int gretl_sort_by (const double *x, const double *y, 
		   double *z, const DATAINFO *pdinfo)
{
    struct pair_sorter *xy;
    int n = sample_size(pdinfo);
    int i, t;

    for (t=pdinfo->t1; t<=pdinfo->t2; t++) {
	if (na(x[t])) {
	    return E_MISSDATA;
	}
    }

    xy = malloc(n * sizeof *xy);
    if (xy == NULL) {
	return E_ALLOC;
    }

    i = 0;
    for (t=pdinfo->t1; t<=pdinfo->t2; t++) {
	xy[i].x = x[t];
	xy[i].y = y[t];
	i++;
    }

    qsort(xy, n, sizeof *xy, gretl_compare_doubles);

    i = 0;
    for (t=pdinfo->t1; t<=pdinfo->t2; t++) {
	z[t] = xy[i++].y;
    }

    free(xy);

    return 0;
}

static void genrank (const double *sz, int m,
		     const double *z, int n,
		     double *rz)
{
    int cases, k, i, j;
    double avg, r = 1;

    for (i=0; i<m; i++) {
	/* scan sorted z */
	cases = k = 0;

	if (i > 0 && sz[i] == sz[i-1]) {
	    continue;
	}

	for (j=0; j<n; j++) {
	    /* scan raw z for matches */
	    if (!na(z[j])) {
		if (z[j] == sz[i]) {
		    rz[k] = r;
		    cases++;
		}
		k++;
	    }
	}

	if (cases > 1) {
	    avg = (r + r + cases - 1.0) / 2.0;
	    for (j=0; j<m; j++) {
		if (rz[j] == r) {
		    rz[j] = avg;
		}
	    }
	} 

	r += cases;
    }
}

int rank_series (const double *x, double *y, int f, 
		 const DATAINFO *pdinfo)
{
    double *sx = NULL;
    double *rx = NULL;
    int n = sample_size(pdinfo);
    int m = n;
    int i, t;

    for (t=pdinfo->t1; t<=pdinfo->t2; t++) {
	if (na(x[t])) m--;
    }

    sx = malloc(m * sizeof *sx);
    rx = malloc(m * sizeof *rx);

    if (sx == NULL || rx == NULL) {
	free(sx);
	free(rx);
	return E_ALLOC;
    }

    i = 0;
    for (t=pdinfo->t1; t<=pdinfo->t2; t++) {
	if (!na(x[t])) {
	    sx[i] = x[t];
	    rx[i] = 0.0;
	    i++;
	}
    }
    
    if (f == F_DSORT) {
	qsort(sx, m, sizeof *sx, gretl_inverse_compare_doubles);
    } else {
	qsort(sx, m, sizeof *sx, gretl_compare_doubles);
    }

    genrank(sx, m, x + pdinfo->t1, n, rx);

    i = 0;
    for (t=pdinfo->t1; t<=pdinfo->t2; t++) {
	if (!na(x[t])) {
	    y[t] = rx[i++];
	}
    }    

    free(sx);
    free(rx);

    return 0;
}

/**
 * diff_series:
 * @x: array of original data.
 * @y: array into which to write the result.
 * @f: function, F_DIFF, F_SDIFF or F_LDIFF.
 * @pdinfo: data set information.
 *
 * Calculates the differenced counterpart to the input 
 * series @x.  If @f = F_SDIFF, the seasonal difference is
 * computed; if @f = F_LDIFF, the log difference, and if
 * @f = F_DIFF, the ordinary first difference.
 *
 * Returns: 0 on success, non-zero error code on failure.
 */

int diff_series (const double *x, double *y, int f, 
		 const DATAINFO *pdinfo)
{
    int t1 = pdinfo->t1;
    int k = (f == F_SDIFF)? pdinfo->pd : 1;
    int t, err = 0;

    if (t1 < k) {
	t1 = k;
    }

    for (t=t1; t<=pdinfo->t2; t++) {

	if (dataset_is_panel(pdinfo) && t % pdinfo->pd == 0) {
	    /* skip first observation in panel unit */
	    continue;
	}

	if (na(x[t]) || na(x[t-k])) {
	    continue;
	}

	if (f == F_DIFF || f == F_SDIFF) {
	    y[t] = x[t] - x[t-k];
	} else if (f == F_LDIFF) {
	    if (x[t] <= 0.0 || x[t-k] <= 0.0) {
		err = E_LOGS; /* FIXME: should be warning? */
	    } else {
		y[t] = log(x[t]) - log(x[t-k]);
	    }
	}
    }

    return 0; /* see FIXME above */
}

/**
 * orthdev_series:
 * @x: array of original data.
 * @y: array into which to write the result.
 * @pdinfo: data set information.
 *
 * Calculates in @y the forward orthogonal deviations of the input 
 * series @x.  That is, y[t] is the scaled difference between x[t]
 * and the mean of the subsequent observations on x.
 *
 * Returns: 0 on success, non-zero error code on failure.
 */

int orthdev_series (const double *x, double *y, const DATAINFO *pdinfo)
{
    double xbar;
    int n, s, t, Tt;

    for (t=pdinfo->t1; t<pdinfo->t2; t++) {

	if (na(x[t])) {
	    continue;
	}

	if (dataset_is_panel(pdinfo)) {
	    Tt = pdinfo->pd - (t % pdinfo->pd) - 1;
	} else {
	    Tt = pdinfo->t2 - t;
	}

	xbar = 0.0;
	n = 0;
	for (s=1; s<=Tt; s++) {
	    if (!na(x[t+s])) {
		xbar += x[t+s];
		n++;
	    }
	}

	if (n > 0) {
	    xbar /= n;
	    /* Lead one period, for compatibility with first diffs.
	       I.e. we lose the first observation rather than the
	       last.  This is for arbond.  Cf. Doornik, Bond and
	       Arellano, DPD documentation.
	    */
	    y[t+1] = sqrt(n / (n + 1.0)) * (x[t] - xbar);
	}
    }

    return 0;
}

/**
 * fracdiff_series:
 * @x: array of original data.
 * @y: array into which to write the result.
 * @d: fraction by which to difference.
 * @diff: boolean variable 1 for fracdiff, 0 for fraclag
 * @obs: used for autoreg calculation, -1 if whole series 
 *	should be calculated otherwise just the observation for
 *	obs is calculated 
 * @pdinfo: data set information.
 *
 * Calculates the fractionally differenced or lagged 
 * counterpart to the input series @x. The fractional 
 * difference operator is defined as (1-L)^d, while the
 * fractional lag operator 1-(1-L)^d.
 *
 * Returns: 0 on success, non-zero error code on failure.
 */

int fracdiff_series (const double *x, double *y, double d,
		     int diff, int obs, const DATAINFO *pdinfo)
{
    int dd, t, tmiss, T;
    const double TOL = 1.0E-12;
    int t1 = pdinfo->t1;
    int t2 = (obs >= 0)? obs : pdinfo->t2;
    double phi = (diff)? -d : d;

#if 0
    fprintf(stderr, "Doing fracdiff_series, with d = %g\n", d);
#endif

    tmiss = array_adjust_t1t2(x, &t1, &t2);

    if (tmiss > 0 && tmiss < t2) {
	t2 = tmiss;
    } 
 
    if (obs >= 0) {
	/* doing a specific observation */
	T = obs - t1 + 1;
	for (t=0; t<pdinfo->n; t++) {
	    y[t] = NADBL;
	}
	if (obs != t1) {
	    y[obs] = (diff)? x[obs] : 0;
	    for (dd=1; dd<T && fabs(phi)>TOL; dd++) {
		y[obs] += phi * x[obs - dd];
		phi *= (dd - d)/(dd + 1);
	    }
	} else if (diff) {
	    y[obs] = x[obs];
	}
    } else {
	/* doing the whole series */
	T = t2 - t1 + 1;
	for (t=0; t<=t2; t++) {
	    if (t >= t1 && t <= t2) {
		y[t] = (diff)? x[t] : 0;
	    } else {
		y[t] = NADBL;
	    } 
	}  
	for (dd=1; dd<=T && fabs(phi)>TOL; dd++) {
	    for (t=t1+dd; t<=t2; t++) {
		y[t] += phi * x[t - dd];
	    }
	    phi *= (dd - d)/(dd + 1);
	}
    }

    return 0;
}

/**
 * boxcox_series:
 * @x: array of original data.
 * @y: array into which to write the result.
 * @d: lambda parameter.
 * @pdinfo: data set information.
 *
 * Calculates in @y the Box-Cox transformation for the 
 * input series @x.
 *
 * Returns: 0 on success, non-zero error code on failure.
 */

int boxcox_series (const double *x, double *y, double d,
		   const DATAINFO *pdinfo)
{
    int t;

    for (t=pdinfo->t1; t<=pdinfo->t2; t++) {
	if (na(x[t])) {
	    y[t] = NADBL;
	} else if (d == 0) {
	    y[t] = (x[t] > 0)? log(x[t]) : NADBL; 
	} else {
	    y[t] = (pow(x[t], d) - 1) / d;
	}
    } 
    
    return 0;
}

int cum_series (const double *x, double *y, const DATAINFO *pdinfo)
{
    int t, s = pdinfo->t1;

    for (t=pdinfo->t1; t<=pdinfo->t2 && na(x[t]); t++) {
	s++;
    }

    if (s < pdinfo->t2) {
	y[s] = x[s];
	for (t=s+1; t<=pdinfo->t2 && !na(x[t]); t++) {
	    y[t] = y[t-1] + x[t];
	}
    }

    return 0;
}

int resample_series (const double *x, double *y, const DATAINFO *pdinfo)
{
    int t1 = pdinfo->t1;
    int t2 = pdinfo->t2;
    int *z = NULL;
    int i, t, n;

    array_adjust_t1t2(x, &t1, &t2);

    n = t2 - t1 + 1;
    if (n <= 1) {
	return E_DATA;
    }

    z = malloc(n * sizeof *z);
    if (z == NULL) {
	return E_ALLOC;
    }

    /* generate n uniform drawings from [t1 .. t2] */
    gretl_rand_int_minmax(z, n, t1, t2);

    /* sample from source series x based on indices z */
    for (t=t1, i=0; t<=t2; t++, i++) {
	y[t] = x[z[i]];
    }

    free(z);

    return 0;
}

int block_resample_series (const double *x, double *y, int blocklen,
			   const DATAINFO *pdinfo)
{
    int t1 = pdinfo->t1;
    int t2 = pdinfo->t2;
    int *z = NULL;
    int m, rem, bt2, x0;
    int i, s, t, n;

    if (blocklen <= 0) {
	return E_DATA;
    }

    if (blocklen == 1) {
	return resample_series(x, y, pdinfo);
    }    

    array_adjust_t1t2(x, &t1, &t2);

    n = t2 - t1 + 1;

    m = n / blocklen;
    rem = n % blocklen;

    /* Let n now represent the number of blocks of @blocklen
       contiguous observations which we need to select; the
       last of these may not be fully used.
    */     
    n = m + (rem > 0);

    /* the last selectable starting point for a block */
    bt2 = t2 - blocklen + 1;

    if (bt2 < t1) {
	return E_DATA;
    }

    z = malloc(n * sizeof *z);
    if (z == NULL) {
	return E_ALLOC;
    }

    /* Generate uniform random series: we want n drawings from the
       range [t1 .. t2 - blocklen + 1], each of which will be
       interpreted as the starting point of a block to be used.
    */
    gretl_rand_int_minmax(z, n, t1, bt2);

    /* Sample from the source series using blocks given by the random
       indices: note that the last block will be incomplete if rem > 0
    */
    t = t1;
    for (i=0; i<n; i++) {
	x0 = z[i];
	for (s=0; s<blocklen; s++) {
	    if (t <= t2) {
		y[t++] = x[x0+s];
	    } else {
		break;
	    }
	}
    }

    free(z);

    return 0;
}

/**
 * filter_series:
 * @x: array of original data.
 * @y: array into which to write the result.
 * @pdinfo: data set information.
 * @A: vector for autoregressive polynomial.
 * @C: vector for moving average polynomial.
 * @y0: initial value of output series.
 *
 * Filters x according to y_t = C(L)/A(L) x_t.  If the intended
 * AR order is p, @A should be a vector of length p.  If the 
 * intended MA order is q, @C should be vector of length (q+1), 
 * the first entry giving the coefficient at lag 0.  However, if 
 * @C is NULL this is taken to mean that the lag-0 MA coefficient 
 * is unity (and all others are zero).
 *
 * Returns: 0 on success, non-zero error code on failure.
 */

int filter_series (const double *x, double *y, const DATAINFO *pdinfo, 
		   gretl_vector *A, gretl_vector *C, double y0)
{
    int t1 = pdinfo->t1;
    int t2 = pdinfo->t2;
    int t, s, i, n;
    int amax, cmax;
    double xlag, coef, *e;
    int err = 0;

    if (gretl_is_null_matrix(C)) {
	cmax = 0;
    } else {
	cmax = gretl_vector_get_length(C);
	if (cmax == 0) {
	    /* if present, C must be a vector */
	    return E_NONCONF;
	}
    }

    if (gretl_is_null_matrix(A)) {
	amax = 0;
    } else {
	amax = gretl_vector_get_length(A);
	if (amax == 0) {
	    /* if present, A must be a vector */
	    return E_NONCONF;
	}
    }

    err = array_adjust_t1t2(x, &t1, &t2);
    if (err) {
	return E_DATA;
    } 

    n = t2 - t1 + 1;
    e = malloc(n * sizeof *e);

    if (e == NULL) {
	return E_ALLOC;
    }

    s = 0;
    if (cmax) {
	for (t=t1; t<=t2; t++) {
	    e[s] = 0;
	    for (i=0; i<cmax; i++) {
		xlag = (t-i >= t1)? x[t-i] : 0;
		if (na(xlag)) {
		    e[s] = NADBL;
		    break;
		} else {
		    coef = gretl_vector_get(C, i);
		    e[s] += xlag * coef;
		}
	    } 
	    s++;
	}
    } else {
	/* implicitly MA(0) = 1 */
	for (t=t1; t<=t2; t++) {
	    e[s++] = x[t];
	}
    }

    s = 0;
    if (amax) {
	for (t=t1; t<=t2; t++) {
	    if (na(e[s])) {
		y[t] = NADBL;
	    } else {
		y[t] = e[s];
		for (i=0; i<amax; i++) {
		    xlag = (t-i>t1)? y[t-i-1] : y0;
		    if (na(xlag)) {
			y[t] = NADBL;
			break;
		    } else {
			coef = gretl_vector_get(A, i);
			y[t] += coef * xlag;
		    }
		} 
	    }
	    s++;
	}
    } else {
	for (t=t1; t<=t2; t++) {
	    y[t] = e[s++];
	}
    }

    free(e);

    return err;
}

/**
 * exponential_movavg_series:
 * @x: array of original data.
 * @y: array into which to write the result.
 * @pdinfo: data set information.
 * @d: coefficient on lagged @x.
 * @n: number of @x observations to average to give the
 * initial @y value.
 *
 * Returns: 0 on success, non-zero error code on failure.
 */

int exponential_movavg_series (const double *x, double *y, 
			       const DATAINFO *pdinfo,
			       double d, int n)
{
    int t1 = pdinfo->t1;
    int t2 = pdinfo->t2;
    int t, T;

    if (n < 0) {
	return E_INVARG;
    } else if (array_adjust_t1t2(x, &t1, &t2) != 0) {
	return E_MISSDATA;
    }

    T = t2 - t1 + 1;
    if (T < n) {
	return E_TOOFEW;
    }

    if (n == 0) {
	/* signal to use full sample mean */
	n = T;
    }
    
    if (n == 1) {
	/* initialize on first observation */
	y[t1] = x[t1];
    } else {
	/* initialize on mean of first n obs */
	y[t1] = 0.0;
	for (t=t1; t<t1+n; t++) {
	    y[t1] += x[t];
	}
	y[t1] /= n;
    }

    for (t=t1+1; t<=t2; t++) {
	y[t] = d * x[t] + (1-d) * y[t-1];
    }

    return 0;
}

/**
 * movavg_series:
 * @x: array of original data.
 * @y: array into which to write the result.
 * @pdinfo: data set information.
 * @k: number of terms in MA.
 * @center: if non-zero, produce centered MA.
 *
 * Returns: 0 on success, non-zero error code on failure.
 */

int movavg_series (const double *x, double *y, const DATAINFO *pdinfo,
		   int k, int center)
{
    int t1 = pdinfo->t1;
    int t2 = pdinfo->t2;
    int k1 = k-1, k2 = 0;
    int i, s, t, T;

    array_adjust_t1t2(x, &t1, &t2);
    T = t2 - t1 + 1;
    if (T < k) {
	return E_TOOFEW;
    }

    if (center) {
	k1 = k / 2;
	k2 = (k % 2 == 0)? (k1 - 1) : k1;
    }

    t1 += k1;
    t2 -= k2;

    for (t=t1; t<=t2; t++) {
	double xs, msum = 0.0;

	for (i=-k1; i<=k2; i++) {
	    s = t + i;
	    if (pdinfo->structure == STACKED_TIME_SERIES) {
		if (pdinfo->paninfo->unit[s] != 
		    pdinfo->paninfo->unit[t]) {
		    s = -1;
		}
	    }

	    if (s >= 0) {
		xs = x[s];
	    } else {
		xs = NADBL;
	    }

	    if (na(xs)) {
		msum = NADBL;
		break;
	    } else {
		msum += x[s];
	    }
	}

	if (!na(msum)) {
	    y[t] = (k > 0)? (msum / k) : msum;
	}
    }

    if (center && k % 2 == 0) {
	/* centered, with even number of terms */
	for (t=t1; t<t2; t++) {
	    if (na(y[t]) || na(y[t+1])) {
		y[t] = NADBL;
	    } else {
		y[t] = (y[t] + y[t+1]) / 2.0;
	    }
	}
	y[t2] = NADBL;
    }

    return 0;
}

int seasonally_adjust_series (const double *x, double *y, 
			      DATAINFO *pdinfo, int tramo)
{
    void *handle;
    int (*adjust_series) (const double *, double *, 
			  const DATAINFO *, int);
    int t1 = pdinfo->t1;
    int t2 = pdinfo->t2;
    int T, err = 0;

    if (!quarterly_or_monthly(pdinfo)) {
	gretl_errmsg_set(_("Input must be a monthly or quarterly time series"));	
	return E_PDWRONG;
    }

    array_adjust_t1t2(x, &t1, &t2);
    T = t2 - t1 + 1;

    if (T < pdinfo->pd * 3) {
	return E_TOOFEW;
    } else if (tramo && T > 600) {
	gretl_errmsg_set(_("TRAMO can't handle more than 600 observations.\n"
			   "Please select a smaller sample."));
	return E_EXTERNAL;
    } else if (!tramo) {
	int pdmax = get_x12a_maxpd();

	if (T > 50 * pdmax) {
	    gretl_errmsg_sprintf(_("X-12-ARIMA can't handle more than %d observations.\n"
				   "Please select a smaller sample."), 50 * pdmax);
	    return E_EXTERNAL;
	}
    }

    adjust_series = get_plugin_function("adjust_series", &handle);
    
    if (adjust_series == NULL) {
	err = E_FOPEN;
    } else {
	int save_t1 = pdinfo->t1;
	int save_t2 = pdinfo->t2;

	pdinfo->t1 = t1;
	pdinfo->t2 = t2;

	err = (*adjust_series) (x, y, pdinfo, tramo);

	pdinfo->t1 = save_t1;
	pdinfo->t2 = save_t2;

	close_plugin(handle);
    }

    return err;
}

int panel_statistic (const double *x, double *y, const DATAINFO *pdinfo, 
		     int k)
{
    const int *unit;
    double ret = NADBL;
    double xbar = NADBL;
    double aux = NADBL;
    int smin = 0;
    int s, t, Ti = 0;

    if (pdinfo->paninfo == NULL) {
	return E_DATA;
    }

    unit = pdinfo->paninfo->unit;

    switch (k) {
    case F_PNOBS:
	for (t=0; t<=pdinfo->n; t++) {
	    if (t == pdinfo->n || (t > 0 && unit[t] != unit[t-1])) {
		for (s=smin; s<t; s++) {
		    y[s] = Ti;
		}
		if (t == pdinfo->n) {
		    break;
		}
		Ti = 0;
		smin = t;
	    }
	    if (!na(x[t])) {
		Ti++;
	    }
	}
	break;
    case F_PMIN:
	for (t=0; t<=pdinfo->n; t++) {
	    if (t == pdinfo->n || (t > 0 && unit[t] != unit[t-1])) {
		for (s=smin; s<t; s++) {
		    y[s] = aux;
		}
		if (t == pdinfo->n) {
		    break;
		}
		aux = NADBL;
		smin = t;
	    }
	    if (!na(x[t])) {
		if (na(aux) || x[t]<aux) {
		    aux = x[t];
		}
	    }
	}
	break;
    case F_PMAX:
	for (t=0; t<=pdinfo->n; t++) {
	    if (t == pdinfo->n || (t > 0 && unit[t] != unit[t-1])) {
		for (s=smin; s<t; s++) {
		    y[s] = aux;
		}
		if (t == pdinfo->n) {
		    break;
		}
		aux = NADBL;
		smin = t;
	    }
	    if (!na(x[t])) {
		if (na(aux) || x[t]>aux) {
		    aux = x[t];
		}
	    }
	}
	break;
    case F_PMEAN:
	for (t=0; t<=pdinfo->n; t++) {
	    if (t == pdinfo->n || (t > 0 && unit[t] != unit[t-1])) {
		if (!na(xbar)) {
		    xbar /= Ti;
		}
		/* got a new unit (or reached the end): 
		   ship out current mean */
		for (s=smin; s<t; s++) {
		    y[s] = xbar;
		}
		if (t == pdinfo->n) {
		    break;
		}
		Ti = 0;
		xbar = NADBL;
		smin = t;
	    }
	    if (!na(x[t])) {
		if (na(xbar)) {
		    xbar = x[t];
		} else {
		    xbar += x[t];
		}
		Ti++;
	    }
	}
	break;
    case F_PSD:
	for (t=0; t<=pdinfo->n; t++) {
	    if (t == pdinfo->n || (t > 0 && unit[t] != unit[t-1])) {
		if (na(xbar)) {
		    ret = NADBL;
		} else if (Ti == 1) {
		    ret = 0.0;
		} else {
		    xbar /= Ti;
		    ret = sqrt((aux/Ti - xbar*xbar) * Ti/(Ti-1));
		}
		for (s=smin; s<t; s++) {
		    y[s] = ret;
		}
		if (t == pdinfo->n) {
		    break;
		}
		Ti = 0;
		xbar = NADBL;
		aux = NADBL;
		smin = t;
	    }
	    if (!na(x[t])) {
		if (na(xbar)) {
		    xbar = x[t];
		    aux = x[t]*x[t];
		} else {
		    xbar += x[t];
		    aux += x[t]*x[t];
		}
		Ti++;
	    }
	}
	break;
    default:
	break;
    }

    return 0;
}

static double default_hp_lambda (const DATAINFO *pdinfo)
{
    return 100 * pdinfo->pd * pdinfo->pd;
}

/**
 * hp_filter:
 * @x: array of original data.
 * @hp: array in which filtered series is computed.
 * @pdinfo: data set information.
 * @lambda: smoothing parameter (or #NADBL to use the default
 * value).
 * @opt: if %OPT_T, return the trend rather than the cycle.
 *
 * Calculates the "cycle" component of the time series in
 * array @x, using the Hodrick-Prescott filter.  Adapted from the 
 * original FORTRAN code by E. Prescott. 
 *
 * Returns: 0 on success, non-zero error code on failure.
 */

int hp_filter (const double *x, double *hp, const DATAINFO *pdinfo,
	       double lambda, gretlopt opt)
{
    int t1 = pdinfo->t1, t2 = pdinfo->t2;
    double v00 = 1.0, v11 = 1.0, v01 = 0.0;
    double det, tmp0, tmp1;
    double e0, e1, b00, b01, b11;
    double **V = NULL;
    double m[2], tmp[2];
    int i, t, T, tb;
    int err = 0;

    for (t=t1; t<=t2; t++) {
	hp[t] = NADBL;
    }

    err = array_adjust_t1t2(x, &t1, &t2);
    if (err) {
	err = E_MISSDATA;
	goto bailout;
    }

    T = t2 - t1 + 1;
    if (T < 4) {
	err = E_DATA;
	goto bailout;
    }

    if (na(lambda)) {
	lambda = default_hp_lambda(pdinfo);
    }

    V = doubles_array_new(4, T);
    if (V == NULL) {
	return E_ALLOC;
    }

    /* adjust starting points */
    x += t1;
    hp += t1;

    /* covariance matrices for each obs */

    for (t=2; t<T; t++) {
	tmp0 = v00;
	tmp1 = v01;
	v00 = 1.0 / lambda + 4.0 * (tmp0 - tmp1) + v11;
	v01 = 2.0 * tmp0 - tmp1;
	v11 = tmp0;

	det = v00 * v11 - v01 * v01;

	V[0][t] =  v11 / det;
	V[1][t] = -v01 / det;
	V[2][t] =  v00 / det;

	tmp0 = v00 + 1.0;
	tmp1 = v00;
      
	v00 -= v00 * v00 / tmp0;
	v11 -= v01 * v01 / tmp0;
	v01 -= (tmp1 / tmp0) * v01;
    }

    m[0] = x[0];
    m[1] = x[1];

    /* forward pass */
    for (t=2; t<T; t++) {
	tmp[0] = m[1];
	m[1] = 2.0 * m[1] - m[0];
	m[0] = tmp[0];

	V[3][t-1] = V[0][t] * m[1] + V[1][t] * m[0];
	hp[t-1]   = V[1][t] * m[1] + V[2][t] * m[0];
	  
	det = V[0][t] * V[2][t] - V[1][t] * V[1][t];
	  
	v00 =  V[2][t] / det;
	v01 = -V[1][t] / det;
	  
	tmp[1] = (x[t] - m[1]) / (v00 + 1.0);
	m[1] += v00 * tmp[1];
	m[0] += v01 * tmp[1];
    }

    V[3][T-2] = m[0];
    V[3][T-1] = m[1];
    m[0] = x[T-2];
    m[1] = x[T-1];

    /* backward pass */
    for (t=T-3; t>=0; t--) {
	t1 = t+1;
	tb = T - t - 1;
      
	tmp[0] = m[0];
	m[0] = 2.0 * m[0] - m[1];
	m[1] = tmp[0];

	if (t > 1) {
	    /* combine info for y < i with info for y > i */
	    e0 = V[2][tb] * m[1] + V[1][tb] * m[0] + V[3][t];
	    e1 = V[1][tb] * m[1] + V[0][tb] * m[0] + hp[t];
	    b00 = V[2][tb] + V[0][t1];
	    b01 = V[1][tb] + V[1][t1];
	    b11 = V[0][tb] + V[2][t1];
	      
	    det = b00 * b11 - b01 * b01;
	      
	    V[3][t] = (b00 * e1 - b01 * e0) / det;
	}
	  
	det = V[0][tb] * V[2][tb] - V[1][tb] * V[1][tb];
	v00 =  V[2][tb] / det;
	v01 = -V[1][tb] / det;

	tmp[1] = (x[t] - m[0]) / (v00 + 1.0);
	m[1] += v01 * tmp[1];
	m[0] += v00 * tmp[1];
    }

    V[3][0] = m[0];
    V[3][1] = m[1];

    if (opt & OPT_T) {
	for (t=0; t<T; t++) {
	    hp[t] = V[3][t];
	}
    } else {
	for (t=0; t<T; t++) {
	    hp[t] = x[t] - V[3][t];
	}
    }

 bailout:

    if (V != NULL) {
	for (i=0; i<4; i++) {
	    free(V[i]);
	}
	free(V);
    }

    return err;
}

/**
 * bkbp_filter:
 * @x: array of original data.
 * @bk: array into which to write the filtered series.
 * @pdinfo: data set information.
 * @bkl: lower frequency bound (or 0 for automatic).
 * @bku: upper frequency bound (or 0 for automatic).
 * @k: approximation order (or 0 for automatic).
 *
 * Calculates the Baxter and King bandpass filter.
 *
 * Returns: 0 on success, non-zero error code on failure.
 */

int bkbp_filter (const double *x, double *bk, const DATAINFO *pdinfo,
		 int bkl, int bku, int k)
{
    int t1 = pdinfo->t1, t2 = pdinfo->t2;
    double omubar, omlbar;
    double avg_a;
    double *a;
    int i, t, err = 0;

    if (bkl <= 0 || bku <= 0) {
	/* get user settings if available (or the defaults) */
	get_bkbp_periods(pdinfo, &bkl, &bku);
    }

    if (k <= 0) {
	k = get_bkbp_k(pdinfo);
    }

#if BK_DEBUG
    fprintf(stderr, "lower limit = %d, upper limit = %d, \n", 
	    bkl, bku);
#endif

    if (bkl >= bku) {
	gretl_errmsg_set("Error in Baxter-King frequencies");
	return 1;
    }

    err = array_adjust_t1t2(x, &t1, &t2);
    if (err) {
	return err;
    } 

    if (2 * k >= t2 - t1 + 1) {
	gretl_errmsg_set("Insufficient observations");
	return E_DATA;
    }

    a = malloc((k + 1) * sizeof *a);
    if (a == NULL) {
	return E_ALLOC;
    }
    
    omubar = M_2PI / bkl;
    omlbar = M_2PI / bku;
    
    /* first we compute the coefficients */

    avg_a = a[0] = (omubar - omlbar) / M_PI;

    for (i=1; i<=k; i++) {
	a[i] = (sin(i * omubar) - sin(i * omlbar)) / (i * M_PI);
	avg_a += 2 * a[i];
    }

    avg_a /= (2 * k + 1);

    for (i=0; i<=k; i++) {
	a[i] -= avg_a;
#if BK_DEBUG
	fprintf(stderr, "a[%d] = %#9.6g\n", i, a[i]);
#endif
    }

    /* now we filter the series, skipping the first
       and last k observations */

    for (t=0; t<pdinfo->n; t++) {
	if (t < t1 + k || t > t2 - k) {
	    bk[t] = NADBL;
	} else {
	    bk[t] = a[0] * x[t];
	    for (i=1; i<=k; i++) {
		bk[t] += a[i] * (x[t-i] + x[t+i]);
	    }
	}
    }

    free(a);

    return err;
}

/* following: material relating to the Butterworth filter */

#define Min(x,y) (((x) > (y))? (y) : (x))
#define NEAR_ZERO 1e-35

static double safe_pow (double x, int n)
{
    double lp;

    x = fabs(x);

    if (x < NEAR_ZERO) {
	return 0.0;
    } else {
	lp = n * log(x);
	if (lp < -80) {
	    return 0.0;
	} else if (lp > 80) {
	    return exp(80.0);
	} else {
	    return exp(lp);
	}
    }
}

static double cotan (double theta)
{
    double s = sin(theta);
    
    if (fabs(s) < NEAR_ZERO) {
	s = copysign(NEAR_ZERO, s);
    }

    return cos(theta) / s;
} 

/**
 * hp_gain:
 * @lambda: H-P parameter.
 * @hipass: 1 for high-pass filter, 0 for low-pass.
 *
 * Returns: a matrix holding omega values from 0 to \pi in
 * column 0, and the corresponding filter gain in column 1.
 */

gretl_matrix *hp_gain (double lambda, int hipass)
{
    gretl_matrix *G;
    int i, width = 180;
    double inc = M_PI / width;
    double x, gain, omega = 0.0;

    G = gretl_matrix_alloc(width + 1, 2);
    if (G == NULL) {
	return NULL;
    }
    
    for (i=0; i<=width; i++) {
	x = 2 * sin(omega / 2);
	gain = 1 / (1 + lambda * pow(x, 4));
	if (hipass) {
	    gain = 1.0 - gain;
	}
	gretl_matrix_set(G, i, 0, omega);
	gretl_matrix_set(G, i, 1, gain);
	omega += inc;
    } 

    return G;
} 

/**
 * butterworth_gain:
 * @n: order of the filter.
 * @cutoff: angular cutoff in radians.
 * @hipass: 1 for high-pass filter, 0 for low-pass.
 *
 * Returns: a matrix holding omega values from 0 to \pi in
 * column 0, and the corresponding filter gain in column 1.
 */

gretl_matrix *butterworth_gain (int n, double cutoff, int hipass)
{
    gretl_matrix *G;
    int i, width = 180;
    double inc = M_PI / width;
    double x, gain, omega = 0.0;

    G = gretl_matrix_alloc(width + 1, 2);
    if (G == NULL) {
	return NULL;
    }
    
    for (i=0; i<=width; i++) {
	if (hipass) {
	    x = cotan(omega / 2) * cotan((M_PI - cutoff) / 2);
	} else {
	    x = tan(omega / 2) * cotan(cutoff / 2);
	}
	gain = 1 / (1 + pow(x, 2 * n));
	gretl_matrix_set(G, i, 0, omega);
	gretl_matrix_set(G, i, 1, gain);
	omega += inc;
    } 

    return G;
} 

/* This function uses a Cholesky decomposition to find the solution of
   the equation Gx = y, where G is a symmetric Toeplitz matrix of order
   T with q supra-diagonal and q sub-diagonal bands. The coefficients of
   G are contained in g. The RHS vector y contains the elements of
   the solution vector x on completion.
*/

static int symm_toeplitz (double *g, double *y, int T, int q)
{
    int t, j, k, jmax;
    double **mu;

    mu = doubles_array_new(q+1, T);
    if (mu == NULL) {
	return E_ALLOC;
    }

    /* initialize */
    for (t=0; t<q; t++) {
	for (j=t+1; j<=q; j++) {
	    mu[j][t] = 0.0;
	}
    }

    /* factorize */
    for (t=0; t<T; t++) { 
	for (k=Min(q, t); k>=0; k--) {
	    mu[k][t] = g[k];
	    jmax = q - k;
	    for (j=1; j<=jmax && t-k-j >= 0; j++) {
		mu[k][t] -= mu[j][t-k] * mu[j+k][t] * mu[0][t-k-j];
	    }
	    if (k > 0) {
		mu[k][t] /= mu[0][t-k];
	    }
	}
    }

    /* forward solve */
    for (t=0; t<T; t++) {
	jmax = Min(t, q);
	for (j=1; j<=jmax; j++) {
	    y[t] -= mu[j][t] * y[t-j];
	}
    }

    /* divide by the diagonal */
    for (t=0; t<T; t++) {
	y[t] /= mu[0][t];
    }

    /* backsolve */
    for (t=T-1; t>=0; t--) {
	jmax = Min(q, T - 1 - t);
	for (j=1; j<=jmax; j++) {
	    y[t] -= mu[j][t+j] * y[t+j];
	}
    }

    doubles_array_free(mu, q+1);

    return 0;
}

#undef Min
#undef NEAR_ZERO

/* Form the autocovariances of an MA process of order q. */

static void form_gamma (double *g, double *mu, int q)
{
    int j, k;

    for (j=0; j<=q; j++) { 
	g[j] = 0.0;
	for (k=0; k<=q-j; k++) {
	    g[j] += mu[k] * mu[k+j];
	}
    }
}

/* Find the coefficients of the nth power of the summation 
   operator (if sign = 1) or of the difference operator (if
   sign = -1).
*/

static void sum_or_diff (double *theta, int n, int sign)
{
    int j, q;

    theta[0] = 1.0;

    for (q=1; q<=n; q++) { 
	theta[q] = 0.0;
	for (j=q; j>0; j--) {
	    theta[j] += sign * theta[j-1];
	}
    } 
} 

/* g is the target, mu is used as workspace for the
   summation coefficients */

static void form_mvec (double *g, double *mu, int n)
{
    sum_or_diff(mu, n, 1);
    form_gamma(g, mu, n);
}

/* g is the target, mu is used as workspace for the
   differencing coefficients */

static void form_svec (double *g, double *mu, int n)
{
    sum_or_diff(mu, n, -1);
    form_gamma(g, mu, n);
}

/* g is the target, mu and tmp are used as workspace */

static void form_wvec (double *g, double *mu, double *tmp,
		       int n, double lam1, double lam2)
{
    int i;

    /* svec = Q'SQ where Q is the 2nd-diff operator */

    form_svec(tmp, mu, n);
    for (i=0; i<=n; i++) {
	g[i] = lam1 * tmp[i];
    }

    form_mvec(tmp, mu, n);
    for (i=0; i<=n; i++) {
	g[i] += lam2 * tmp[i];
    }
}

/* Find the second differences of the elements of a vector.
   Notice that the first two elements of the vector are lost 
   in the process. However, we index the leading element of 
   the differenced vector by t = 0.
*/

static void QprimeY (double *y, int T)
{
    int t;

    for (t=0; t<T-2; t++) {
	y[t] += y[t+2] - 2 * y[t+1];
    }
}

/* Premultiply vector y by a symmetric banded Toeplitz matrix 
   Gamma with n nonzero sub-diagonal bands and n nonzero 
   supra-diagonal bands. The elements of Gamma are contained
   in g; tmp is used as workspace.
*/

static void GammaY (double *g, double *y, double *tmp, 
		    int T, int n)
{
    double lx, rx;
    int t, j;

    for (j=0; j<=n; j++) {
	tmp[j] = 0.0;
    }

    for (t=0; t<T; t++) {
	for (j=n; j>0; j--) {
	    tmp[j] = tmp[j-1];
	}
	tmp[0] = g[0] * y[t];
	for (j=1; j<=n; j++) {
	    lx = (t - j < 0)? 0 : y[t-j];
	    rx = (t + j >= T)? 0 : y[t+j];
	    tmp[0] += g[j] * (lx + rx);
	}
	if (t >= n) {
	    y[t-n] = tmp[n];
	}
    }

    for (j=0; j<n; j++) {
	y[T-j-1] = tmp[j];
    }
}

/* Multiply the vector y by matrix Q of order T x T-2, 
   where Q' is the matrix which finds the second 
   differences of a vector.  
*/

static void form_Qy (double *y, int T)
{
    double tmp, lag1 = 0.0, lag2 = 0.0;
    int t;

    for (t=0; t<T-2; t++) { 
	tmp = y[t];
	y[t] += lag2 - 2 * lag1;
	lag2 = lag1;
	lag1 = tmp;
    }

    y[T-2] = lag2 - 2 * lag1;
    y[T-1] = lag1;
}

/**
 * butterworth_filter:
 * @x: array of original data.
 * @bw: array into which to write the filtered series.
 * @pdinfo: data set information.
 * @order: desired lag order.
 * @cutoff: desired angular cutoff in degrees (0, 180).
 *
 * Calculates the Butterworth filter. The code that does this
 * is based on D.S.G. Pollock's IDEOLOG -- see 
 * http://www.le.ac.uk/users/dsgp1/
 *
 * Returns: 0 on success, non-zero error code on failure.
 */

int butterworth_filter (const double *x, double *bw, const DATAINFO *pdinfo,
			int order, double cutoff)
{
    double *g, *ds, *tmp, *y;
    double lam1, lam2 = 1.0;
    int t1 = pdinfo->t1, t2 = pdinfo->t2;
    int T, t, m, n = order;
    int err = 0;

    err = array_adjust_t1t2(x, &t1, &t2);
    if (err) {
	return err;
    } 

    if (2 * n >= t2 - t1) {
	gretl_errmsg_set("Insufficient observations");
	return E_DATA;
    }

    if (cutoff <= 0.0 || cutoff >= 180.0) {
	return E_INVARG;
    }

    /* the cutoff is expressed in radians internally */
    cutoff *= M_PI / 180.0;

    T = t2 - t1 + 1;
    m = 3 * (n+1);

    /* the workspace we need for everything except the
       Toeplitz solver */
    g = malloc(m * sizeof *g);
    if (g == NULL) {
	return E_ALLOC;
    }

    ds = g + n + 1;
    tmp = ds + n + 1;

    lam1 = 1 / tan(cutoff / 2);
    lam1 = safe_pow(lam1, n * 2);

    /* there's really only one "lambda": one out of
       lam1, lam2 = 1 and has no effect on the
       calculation */

    if (lam1 > 1.0) {
	lam2 = 1 / lam1;
	lam1 = 1.0;
    }

    /* sample offset */
    y = bw + t1;

    /* place a copy of the data in y */
    memcpy(y, x + t1, T * sizeof *y);

    form_wvec(g, ds, tmp, n, lam1, lam2); /* W = M + lambda * Q'SQ */
    QprimeY(y, T);

    /* solve (M + lambda*Q'SQ)x = d for x */
    err = symm_toeplitz(g, y, T-2, n);

    if (!err) {
	form_Qy(y, T);
	form_svec(g, ds, n-2);
	GammaY(g, y, tmp, T, n-2);   /* Form SQg */
#if 1
	/* write the trend into y (low-pass) */
	for (t=0; t<T; t++) {
	    y[t] = x[t] - lam1 * y[t];
	}	
#else
	/* write the cycle/residual into y (high-pass) */
	for (t=0; t<T; t++) {
	    y[t] *= lam1;
	}	
#endif
    }    

    free(g);

    return err;
}

/**
 * poly_trend:
 * @x: array of original data.
 * @fx: array into which to write the fitted series.
 * @pdinfo: data set information.
 * @order: desired polynomial order.
 *
 * Calculates a trend via the method of orthogonal polynomials.
 * Based on C code for D.S.G. Pollock's DETREND program.
 *
 * Returns: 0 on success, non-zero error code on failure.
 */

int poly_trend (const double *x, double *fx, const DATAINFO *pdinfo, int order)
{
    double tmp, denom, lagdenom = 1;
    double alpha, gamma, delta;
    double *phi, *philag;
    int t1 = pdinfo->t1;
    int t2 = pdinfo->t2;
    int i, t, T;
    int err;

    err = array_adjust_t1t2(x, &t1, &t2);
    if (err) {
	return err;
    } 

    T = t2 - t1 + 1;

    if (order > T) {
	return E_DF;
    }

    phi = malloc(2 * T * sizeof *phi);
    if (phi == NULL) {
	return E_ALLOC;
    }

    philag = phi + T;
    x = x + t1;
     
    for (t=0; t<T; t++) {
	phi[t] = 1;
	philag[t] = 0;
	fx[t] = 0;
    }

    for (i=0; i<=order; i++) {
	alpha = gamma = denom = 0.0;
     
	for (t=0; t<T; t++) {
	    alpha += x[t] * phi[t];
	    gamma += phi[t] * phi[t] * t;
	    denom += phi[t] * phi[t];
	}
          
	alpha /= denom;
	gamma /= denom;
	delta = denom / lagdenom;
	lagdenom = denom;
       
	for (t=0; t<T; t++) {
	    fx[t] += alpha * phi[t];
	    tmp = phi[t];
	    phi[t] = (t - gamma) * phi[t] - delta * philag[t];
	    philag[t] = tmp;
	} 
    }

    free(phi);

    return err;
} 

static int n_new_dummies (const DATAINFO *pdinfo,
			  int nunits, int nperiods)
{
    char dname[VNAMELEN];
    int i, nnew = nunits + nperiods;

    for (i=0; i<nunits; i++) {
	sprintf(dname, "du_%d", i + 1);
	if (gretl_is_series(dname, pdinfo)) {
	    nnew--;
	}
    }

    for (i=0; i<nperiods; i++) {
	sprintf(dname, "dt_%d", i + 1);
	if (gretl_is_series(dname, pdinfo)) {
	    nnew--;
	}
    }

    return nnew;
}

static void 
make_dummy_name_and_label (int vi, const DATAINFO *pdinfo, int center,
			   char *vname, char *vlabel)
{
    if (center > 0) {
	sprintf(vname, "S%d", vi);
	strcpy(vlabel, "centered periodic dummy");
    } else if (center < 0) {
	sprintf(vname, "S%d", vi);
	strcpy(vlabel, "uncentered periodic dummy");
    } else if (pdinfo->pd == 4 && pdinfo->structure == TIME_SERIES) {
	sprintf(vname, "dq%d", vi);
	sprintf(vlabel, _("= 1 if quarter = %d, 0 otherwise"), vi);
    } else if (pdinfo->pd == 12 && pdinfo->structure == TIME_SERIES) {
	sprintf(vname, "dm%d", vi);
	sprintf(vlabel, _("= 1 if month = %d, 0 otherwise"), vi);
    } else {
	char dumstr[8] = "dummy_";
	char numstr[8];
	int len;

	sprintf(numstr, "%d", vi);
	len = strlen(numstr);
	dumstr[8 - len] = '\0';
	sprintf(vname, "%s%d", dumstr, vi);
	sprintf(vlabel, _("%s = 1 if period is %d, 0 otherwise"), vname, vi);
    }
}

/**
 * dummy:
 * @pZ: pointer to data matrix.
 * @pdinfo: data information struct.
 * @center: if greater than zero subtract the population mean from
 * each of the generated dummies; if less than zero, do not
 * subtract the mean but generate dummies with labels on the
 * same pattern as centered dummies (for internal use in VECMs).
 * Usually this argument is set to zero.
 *
 * Adds to the data set (if these variables are not already 
 * present) a set of periodic (usually seasonal) dummy variables.
 *
 * Returns: the ID number of the first dummy variable on success,
 * or 0 on error.
 */

int dummy (double ***pZ, DATAINFO *pdinfo, int center)
{
    char vname[VNAMELEN];
    char vlabel[MAXLABEL];
    int vi, t, pp;
    int ndums = pdinfo->pd, nnew = 0;
    int di, di0 = pdinfo->v;
    double xx, dx;

    if (ndums == 1 || ndums > 99999) {
	gretl_errmsg_set(_("This command won't work with the current periodicity"));
	return 0;
    }

    for (vi=0; vi<ndums; vi++) {
	make_dummy_name_and_label(vi + 1, pdinfo, center, vname, vlabel);
	di = series_index(pdinfo, vname);
	if (di >= pdinfo->v || strcmp(vlabel, VARLABEL(pdinfo, di))) {
	    nnew++;
	} else if (vi == 0) {
	    di0 = di;
	} else if (di != di0 + vi) {
	    /* dummies not consecutive: problem */
	    di0 = pdinfo->v;
	    nnew = ndums;
	    break;
	}
    }

    if (nnew == 0) {
	/* all dummies already present */
	return di0;
    } else if (pZ == NULL) {
	return -1;
    }

    if (dataset_add_series(ndums, pZ, pdinfo)) {
	gretl_errmsg_set(_("Out of memory!"));
	return 0;
    }

    for (vi=1, di = di0; vi<=ndums; vi++, di++) {
	make_dummy_name_and_label(vi, pdinfo, center, vname, vlabel);
	strcpy(pdinfo->varname[di], vname);
	strcpy(VARLABEL(pdinfo, di), vlabel);
    }

    if (dataset_is_daily(pdinfo)) {
	int yy, mm = 10;

	pp = pdinfo->pd;
	while ((pp = pp / 10)) {
	    mm *= 10;
	}

	for (vi=1, di = di0; vi<=ndums; vi++, di++) {
	    for (t=0; t<pdinfo->n; t++) {
		xx = date(t, pdinfo->pd, pdinfo->sd0) + .1;
		yy = (int) xx;
		pp = (int) (mm * (xx - yy) + 0.5);
		dx = (pp == vi)? 1.0 : 0.0;
		(*pZ)[di][t] = dx;
	    }
	}
    } else {
	int p0 = get_subperiod(0, pdinfo, NULL);

	for (t=0; t<pdinfo->n; t++) {
	    pp = (t + p0) % pdinfo->pd;
	    for (vi=0, di = di0; vi<ndums; vi++, di++) {
		dx = (pp == vi)? 1 : 0;
		(*pZ)[di][t] = dx;
	    }
	}
    }

    if (center > 0) {
	double cx = 1.0 / pdinfo->pd;
	int vimax = di0 + pdinfo->pd - 1;

	for (vi=di0; vi<=vimax; vi++) {
	    for (t=0; t<pdinfo->n; t++) {
		(*pZ)[vi][t] -= cx;
	    }
	}	
    }

    return di0;
}

/**
 * panel_dummies:
 * @pZ: pointer to data matrix.
 * @pdinfo: data information struct.
 * @opt: %OPT_T for time dummies, otherwise unit dummies.
 *
 * Adds to the data set a set of dummy variables corresponding
 * to either the cross-sectional units in a panel, or the
 * time periods.
 *
 * Returns: 0 on successful completion, error code on error.
 */

int panel_dummies (double ***pZ, DATAINFO *pdinfo, gretlopt opt)
{
    char vname[16];
    int vi, t, yy, pp, mm;
    int orig_v = pdinfo->v;
    int ndum, nnew;
    int n_unitdum = 0;
    int n_timedum = 0;
    int newvnum;
    double xx;

    if (opt & OPT_T) {
	ndum = n_timedum = pdinfo->pd;
    } else {	
	n_unitdum = pdinfo->n / pdinfo->pd;
	if (pdinfo->n % pdinfo->pd) {
	    n_unitdum++;
	}
	ndum = n_unitdum;
    }
    
    if (ndum == 1) {
	return E_PDWRONG;
    }

    nnew = n_new_dummies(pdinfo, n_unitdum, n_timedum);

    if (nnew > 0 && dataset_add_series(nnew, pZ, pdinfo)) {
	return E_ALLOC;
    }

    pp = pdinfo->pd;
    mm = 10;
    while ((pp = pp / 10)) {
	mm *= 10;
    }

    newvnum = orig_v;

    /* generate time-based dummies, if wanted */

    for (vi=1; vi<=n_timedum; vi++) {
	int dnum;

	sprintf(vname, "dt_%d", vi);

	dnum = series_index(pdinfo, vname);
	if (dnum >= orig_v) {
	    dnum = newvnum++;
	}

	strcpy(pdinfo->varname[dnum], vname);
	sprintf(VARLABEL(pdinfo, dnum), 
		_("%s = 1 if %s is %d, 0 otherwise"), vname, 
		_("period"), vi);

	for (t=0; t<pdinfo->n; t++) {
	    xx = date(t, pdinfo->pd, pdinfo->sd0);
	    yy = (int) xx;
	    pp = (int) (mm * (xx - yy) + 0.5);
	    (*pZ)[dnum][t] = (pp == vi)? 1.0 : 0.0;
	}
    }

    /* generate unit-based dummies, if wanted */

    for (vi=1; vi<=n_unitdum; vi++) {
	int dmin = (vi - 1) * pdinfo->pd;
	int dmax = vi * pdinfo->pd;
	int dnum;

	sprintf(vname, "du_%d", vi);

	dnum = series_index(pdinfo, vname);
	if (dnum >= orig_v) {
	    dnum = newvnum++;
	}	

	strcpy(pdinfo->varname[dnum], vname);
	sprintf(VARLABEL(pdinfo, dnum), 
		_("%s = 1 if %s is %d, 0 otherwise"), vname, 
		_("unit"), vi);

	for (t=0; t<pdinfo->n; t++) {
	    (*pZ)[dnum][t] = (t >= dmin && t < dmax)? 1 : 0;
	}
    }

    return 0;
}

/**
 * gen_unit:
 * @pZ: pointer to data matrix.
 * @pdinfo: data information struct.
 *
 * (For panel data only) adds to the data set an index variable 
 * that uniquely identifies the cross-sectional units.
 *
 * Returns: 0 on successful completion, error code on error.
 */

int gen_unit (double ***pZ, DATAINFO *pdinfo)
{
    int xt = 0;
    int i, t;

    if (pdinfo->structure != STACKED_TIME_SERIES) {
	gretl_errmsg_set("'genr unit' can be used only with "
			 "panel data");
	return 1;
    }

    i = series_index(pdinfo, "unit");

    if (i == pdinfo->v && dataset_add_series(1, pZ, pdinfo)) {
	return E_ALLOC;
    }

    strcpy(pdinfo->varname[i], "unit");
    strcpy(VARLABEL(pdinfo, i), _("cross-sectional unit index"));

    for (t=0; t<pdinfo->n; t++) {
	if (t % pdinfo->pd == 0) {
	    xt++;
	}
	(*pZ)[i][t] = (double) xt;
    }

    return 0;
}

/**
 * panel_unit_first_obs:
 * @t: zero-based observation number.
 * @pdinfo: data information struct.
 *
 * Returns: 1 if observation @t is the first time-series
 * observation on a given cross-sectional unit in a
 * panel dataset, else 0.
 */

int panel_unit_first_obs (int t, const DATAINFO *pdinfo)
{
    char *p, obs[OBSLEN];
    int ret = 0;

    ntodate(obs, t, pdinfo);
    p = strchr(obs, ':');
    if (p != NULL && atoi(p + 1) == 1) {
	ret = 1;
    }

    return ret;
}

/* make special time variable for panel data */

static void make_panel_time_var (double *x, const DATAINFO *pdinfo)
{
    int t, xt = 0;

    for (t=0; t<pdinfo->n; t++) {
	if (t % pdinfo->pd == 0) {
	    xt = 1;
	}
	x[t] = (double) xt++;
    }
}

/**
 * gen_time:
 * @pZ: pointer to data array.
 * @pdinfo: data information struct.
 * @tm: if non-zero, an actual time trend is wanted,
 * otherwise just an index of observations.
 *
 * Generates (and adds to the dataset, if it's not already
 * present) a time-trend or index variable.  This function
 * is panel-data aware: if the dataset is a panel and
 * @tm is non-zero, the trend will not simply run
 * consecutively over the entire range of the data, but
 * will correctly represent the location in time of
 * each observation.  The index is 1-based.
 *
 * Returns: 0 on success, non-zero on error.
 */

int gen_time (double ***pZ, DATAINFO *pdinfo, int tm)
{
    int i, t;

    i = series_index(pdinfo, (tm)? "time" : "index");

    if (i == pdinfo->v && dataset_add_series(1, pZ, pdinfo)) {
	return E_ALLOC;
    }

    if (tm) {
	strcpy(pdinfo->varname[i], "time");
	strcpy(VARLABEL(pdinfo, i), _("time trend variable"));
    } else {
	strcpy(pdinfo->varname[i], "index");
	strcpy(VARLABEL(pdinfo, i), _("data index variable"));
    }
    
    if (tm && pdinfo->structure == STACKED_TIME_SERIES) {
	make_panel_time_var((*pZ)[i], pdinfo);
    } else {
	for (t=0; t<pdinfo->n; t++) {
	    (*pZ)[i][t] = (double) (t + 1);
	}
    }

    return 0;
}

/**
 * genr_wkday:
 * @pZ: pointer to data array.
 * @pdinfo: data information struct.
 *
 * Generates (and adds to the dataset, if it's not already
 * present) an index representing the day of the week for
 * each observation (for dated daily data only).
 * The index has value 0 for Sunday, 1 for Monday, and
 * so on.
 *
 * Returns: 0 on success, non-zero code on error.
 */

int gen_wkday (double ***pZ, DATAINFO *pdinfo)
{
    char datestr[OBSLEN];
    int i, t;

    if (!dated_daily_data(pdinfo)) {
	return E_PDWRONG;
    }

    i = series_index(pdinfo, "weekday");

    if (i == pdinfo->v && dataset_add_series(1, pZ, pdinfo)) {
	return E_ALLOC;
    }

    strcpy(pdinfo->varname[i], "weekday");
    strcpy(VARLABEL(pdinfo, i), _("day of week (1 = Monday)"));
    
    for (t=0; t<pdinfo->n; t++) {
	ntodate(datestr, t, pdinfo);
	(*pZ)[i][t] = get_day_of_week(datestr);
    }

    return 0;
}

typedef enum {
    PLOTVAR_INDEX,
    PLOTVAR_TIME,
    PLOTVAR_ANNUAL,
    PLOTVAR_QUARTERS,
    PLOTVAR_MONTHS,
    PLOTVAR_CALENDAR,
    PLOTVAR_DECADES,
    PLOTVAR_HOURLY,
    PLOTVAR_MAX
} plotvar_type; 

int plotvar_code (const DATAINFO *pdinfo)
{
    if (!dataset_is_time_series(pdinfo)) {
	return PLOTVAR_INDEX;
    } else if (pdinfo->pd == 1) {
	return PLOTVAR_ANNUAL;
    } else if (pdinfo->pd == 4) {
	return PLOTVAR_QUARTERS;
    } else if (pdinfo->pd == 12) {
	return PLOTVAR_MONTHS;
    } else if (pdinfo->pd == 24) {
	return PLOTVAR_HOURLY;
    } else if (calendar_data(pdinfo)) {
	return PLOTVAR_CALENDAR;
    } else if (dataset_is_decennial(pdinfo)) {
	return PLOTVAR_DECADES;
    } else {
	return PLOTVAR_TIME;
    }
}

/**
 * gretl_plotx:
 * @pdinfo: data information struct.
 *
 * Finds or creates a special dummy variable for use on the
 * x-axis in plotting; this will have the full length of the
 * data series as given in @pdinfo, and will be appropriately
 * configured for the data frequency.  Do not try to free this
 * variable.
 *
 * Returns: pointer to plot x-variable, or NULL on failure.
 */

const double *gretl_plotx (const DATAINFO *pdinfo)
{
    static double *x;
    static int ptype;
    static int Tbak;
    static double sd0bak;

    int t, y1, T;
    int new_ptype;
    double sd0;
    float rm;

    if (pdinfo == NULL) {
	/* cleanup signal */
	free(x);
	x = NULL;
	ptype = 0;
	T = 0;
	sd0 = 0;
	return NULL;
    }

    new_ptype = plotvar_code(pdinfo);
    T = pdinfo->n;
    sd0 = pdinfo->sd0;

    if (x != NULL && new_ptype == ptype && Tbak == T && sd0 == sd0bak) {
	/* a suitable array is already at hand */
	return x;
    }

    if (x != NULL) {
	free(x);
    }

    x = malloc(T * sizeof *x);
    if (x == NULL) {
	return NULL;
    }

    Tbak = T;
    ptype = new_ptype;
    sd0bak = sd0;

    y1 = (int) sd0;
    rm = sd0 - y1;

    switch (ptype) {
    case PLOTVAR_ANNUAL: 
	for (t=0; t<T; t++) {
	    x[t] = (double) (t + atoi(pdinfo->stobs));
	}
	break;
    case PLOTVAR_QUARTERS:
	x[0] = y1 + (10.0 * rm - 1.0) / 4.0;
	for (t=1; t<T; t++) {
	    x[t] = x[t-1] + .25;
	}
	break;
    case PLOTVAR_MONTHS:
	x[0] = y1 + (100.0 * rm - 1.0) / 12.0;
	for (t=1; t<T; t++) {
	    x[t] = x[t-1] + (1.0 / 12.0);
	}
	break;
    case PLOTVAR_HOURLY:
	x[0] = y1 + (100.0 * rm - 1.0) / 24.0;
	for (t=1; t<T; t++) {
	    x[t] = x[t-1] + (1.0 / 24.0);
	}
	break;
    case PLOTVAR_CALENDAR:
	for (t=0; t<T; t++) {
	    if (pdinfo->S != NULL) {
		x[t] = get_dec_date(pdinfo->S[t]);
	    } else {
		char datestr[OBSLEN];
		    
		calendar_date_string(datestr, t, pdinfo);
		x[t] = get_dec_date(datestr);
	    }
	}
	break;
    case PLOTVAR_DECADES:
	for (t=0; t<T; t++) {
	    x[t] = pdinfo->sd0 + 10 * t;
	}
	break;
    case PLOTVAR_INDEX:
	for (t=0; t<T; t++) {
	    x[t] = (double) (t + 1);
	}
	break;
    case PLOTVAR_TIME:
	for (t=0; t<T; t++) {
	    x[t] = (double) (t + 1);
	}
	break;
    default:
	break;
    }

    return x;
}

/**
 * get_fit_or_resid:
 * @pmod: pointer to source model.
 * @pdinfo: information on the data set.
 * @idx: %M_UHAT, %M_UHAT2, %M_YHAT, %M_AHAT or %M_H.
 * @vname: location to write series name (length %VNAMELEN)
 * @vlabel: location to write series description (length should
 * be %MAXLABEL).
 * @err: location to receive error code.
 *
 * Creates a full-length array holding the specified model
 * data, and writes name and description into the @vname and
 * @vlabel.
 * 
 * Returns: allocated array on success or NULL on failure.
 */

double *get_fit_or_resid (const MODEL *pmod, DATAINFO *pdinfo, 
			  ModelDataIndex idx, char *vname, 
			  char *vlabel, int *err)
{
    const double *src = NULL;
    double *ret = NULL;
    int t;

    if (idx == M_H) {
	src = gretl_model_get_data(pmod, "garch_h");
    } else if (idx == M_AHAT) {
	src = gretl_model_get_data(pmod, "ahat");
    } else if (idx == M_UHAT || idx == M_UHAT2) {
	src = pmod->uhat;
    } else if (idx == M_YHAT) {
	src = pmod->yhat;
    }

    if (src == NULL) {
	*err = E_BADSTAT;
	return NULL;
    }

    ret = malloc(pdinfo->n * sizeof *ret);
    if (ret == NULL) {
	*err = E_ALLOC;
	return NULL;
    }

    for (t=0; t<pdinfo->n; t++) {
	if (t >= pmod->t1 && t <= pmod->t2) {
	    if (idx == M_UHAT2) {
		ret[t] = na(src[t]) ? NADBL : (src[t] * src[t]);
	    } else {
		ret[t] = src[t];
	    }
	} else {
	    ret[t] = NADBL;
	}
    }

    if (idx == M_UHAT) {
	sprintf(vname, "uhat%d", pmod->ID);
	if (pmod->ci == GARCH && (pmod->opt & OPT_Z)) {
	    sprintf(vlabel, _("standardized residual from model %d"), pmod->ID);
	} else {
	    sprintf(vlabel, _("residual from model %d"), pmod->ID);
	}
    } else if (idx == M_YHAT) {
	sprintf(vname, "yhat%d", pmod->ID);
	sprintf(vlabel, _("fitted value from model %d"), pmod->ID);
    } else if (idx == M_UHAT2) { 
	/* squared residuals */
	sprintf(vname, "usq%d", pmod->ID);
	if (pmod->ci == GARCH && (pmod->opt & OPT_Z)) {
	    sprintf(vlabel, _("squared standardized residual from model %d"), pmod->ID);
	} else {
	    sprintf(vlabel, _("squared residual from model %d"), pmod->ID);
	}
    } else if (idx == M_H) { 
	/* garch variance */
	sprintf(vname, "h%d", pmod->ID);
	sprintf(vlabel, _("fitted variance from model %d"), pmod->ID);
    } else if (idx == M_AHAT) { 
	/* fixed-effects constants */
	sprintf(vname, "ahat%d", pmod->ID);
	sprintf(vlabel, _("per-unit constants from model %d"), pmod->ID);
    }	

    return ret;
}

/**
 * genr_fit_resid:
 * @pmod: pointer to source model.
 * @pZ: pointer to data array.
 * @pdinfo: information on the data set.
 * @idx: %M_UHAT, %M_UHAT2, %M_YHAT, %M_AHAT or %M_H.
 * 
 * Adds residuals or fitted values or squared residuals from a
 * given model to the data set.
 * 
 * Returns: 0 on successful completion, error code on error.
 */

int genr_fit_resid (const MODEL *pmod, double ***pZ, DATAINFO *pdinfo,
		    ModelDataIndex idx)
{
    char vname[VNAMELEN], vlabel[MAXLABEL];
    double *x;
    int err = 0;

    x = get_fit_or_resid(pmod, pdinfo, idx, vname, vlabel, &err);

    if (!err) {
	err = dataset_add_allocated_series(x, pZ, pdinfo);
    }

    if (err) {
	free(x);
    } else {
	int v = pdinfo->v - 1;

	strcpy(pdinfo->varname[v], vname);
	strcpy(VARLABEL(pdinfo, v), vlabel);
    }

    return err;
}

int get_observation_number (const char *s, const DATAINFO *pdinfo)
{
    char test[OBSLEN];
    size_t n;
    int t;

    *test = 0;
    strncat(test, (*s == '"')? s + 1 : s, OBSLEN - 1);

    n = strlen(test);
    if (test[n-1] == '"') {
	test[n-1] = '\0';
    }

    if (dataset_has_markers(pdinfo)) {
	for (t=0; t<pdinfo->n; t++) {
	    if (!strcmp(test, pdinfo->S[t])) {
		return t + 1;
	    }
	}
	if (calendar_data(pdinfo)) {
	    for (t=0; t<pdinfo->n; t++) {
		if (!strcmp(test, pdinfo->S[t]) ||
		    !strcmp(test, pdinfo->S[t] + 2)) {
		    return t + 1;
		}
	    }
	}
    }

    if (pdinfo->structure == TIME_SERIES) {
	t = dateton(test, pdinfo);
	if (t >= 0) {
	    return t + 1;
	}
    }

    if (calendar_data(pdinfo)) {
	char datestr[OBSLEN];

	for (t=0; t<pdinfo->n; t++) {
	    calendar_date_string(datestr, t, pdinfo);
	    if (!strcmp(test, datestr) ||
		!strcmp(test, datestr + 2)) {
		return t + 1;
	    }
	}
    }

    return 0;
}

#define OBS_DEBUG 0

static int plain_obs_number (const char *obs, const DATAINFO *pdinfo)
{
    char *test;
    int t = -1;

    errno = 0;

    strtol(obs, &test, 10);

    if (errno == 0 && *test == '\0') {
	t = atoi(obs) - 1; /* convert from 1-based to 0-based */
	if (t >= pdinfo->n) {
	    t = -1;
	}
    }

    return t;
}

/* Given what looks like an observation number or date within "[" and
   "]", try to determine the observation number.  This is quite tricky
   since we try to handle both dates and plain observation numbers
   (and in addition, variables representing the latter); and we may
   have to deal with the translation from 1-based indexing in user
   space to 0-based indexing for internal purposes.
*/

int get_t_from_obs_string (const char *s, const double **Z, 
			   const DATAINFO *pdinfo)
{
    int t;

    if (*s == '"') {
	char obs[16];
	int err = 0;

	*obs = '\0';
	strncat(obs, s, 15);
	gretl_unquote(obs, &err);
	t = dateton(obs, pdinfo);
    } else {
	t = dateton(s, pdinfo);
    }

#if OBS_DEBUG
    fprintf(stderr, "\nget_t_from_obs_string: s ='%s', dateton gives t = %d\n", 
	    s, t);
#endif

    if (t < 0) {
	if (isdigit((unsigned char) *s)) {
	    t = plain_obs_number(s, pdinfo);
#if OBS_DEBUG
	    fprintf(stderr, " plain_obs_number gives t = %d\n", t);
#endif
	} else {
	    if (gretl_is_scalar(s)) {
		t = gretl_scalar_get_value(s);
	    } 

	    if (t > pdinfo->n) {
		/* e.g. annual dates */
		char try[16];

		sprintf(try, "%d", t);
		t = dateton(try, pdinfo);
#if OBS_DEBUG
		fprintf(stderr, " revised via dateton: t = %d\n", t);
#endif
	    } else {
		/* convert to 0-based */
		t--;
	    }
	}
    }

    if (t < 0) {
	gretl_errmsg_set(_("Observation number out of bounds"));
    }

#if OBS_DEBUG
    fprintf(stderr, " return value: t = %d\n", t);
#endif

    return t;
}

int check_declarations (char ***pS, parser *p)
{
    char **S;
    const char *s;
    int i, n = 1;

    gretl_error_clear();

    if (p->lh.substr == NULL) {
	p->err = E_ALLOC;
	return 0;
    }

    s = p->lh.substr;
    s += strspn(s, " ");

    while (*s) {
	if (*s == ',' || *s == ' ') {
	    n++;
	    s++;
	    s += strspn(s, " ");
	} else {
	    s++;
	}
    }

    S = strings_array_new(n);
    if (S == NULL) {
	p->err = E_ALLOC;
	return 0;
    }

    s = p->lh.substr;
    for (i=0; i<n; i++) {
	S[i] = gretl_word_strdup(s, &s);
	if (S[i] == NULL) {
	    p->err = E_DATA;
	    break;
	}
    }

    if (*s != '\0') {
	p->err = E_DATA;
    }

    for (i=0; i<n && !p->err; i++) {
	if (gretl_is_series(S[i], p->dinfo) ||
	    gretl_is_scalar(S[i]) ||
	    gretl_is_bundle(S[i]) ||
	    get_matrix_by_name(S[i]) ||
	    get_list_by_name(S[i]) ||
	    get_string_by_name(S[i])) {
	    /* variable already exists */
	    p->err = E_DATA;
	} else if (check_varname(S[i])) {
	    /* invalid name */
	    p->err = E_DATA;
	} 
    }

    if (p->err) {
	gretl_errmsg_set(_("Invalid declaration"));
	free_strings_array(S, n);
    } else {
	*pS = S;
    }

    return n;
}

/* cross-sectional mean of observations on variables in @list
   at time @t */

static double mean_at_obs (const int *list, const double **Z, int t)
{
    double xi, xsum = 0.0;
    int i;

    for (i=1; i<=list[0]; i++) {
	xi = Z[list[i]][t];
	if (na(xi)) {
	    return NADBL;
	}
	xsum += xi;
    }

    return xsum / list[0];
}

/* weighted cross-sectional mean of observations on variables in @list
   at time @t, weights given in @wlist */

static double weighted_mean_at_obs (const int *list, const int *wlist,
				    const double **Z, int t,
				    double *pwsum, int *pm)
{
    double w, xi, wsum = 0.0, wxbar = 0.0;
    int i, m = 0;

    for (i=1; i<=list[0]; i++) {
	w = Z[wlist[i]][t];
	if (na(w) || w < 0.0) {
	    return NADBL;
	}
	if (w > 0.0) {
	    wsum += w;
	    m++;
	}
    }

    if (wsum <= 0.0) {
	return NADBL;
    }

    if (pwsum != NULL) {
	*pwsum = wsum;
    }

    if (pm != NULL) {
	*pm = m;
    }

    for (i=1; i<=list[0]; i++) {
	w = Z[wlist[i]][t] / wsum;
	if (w > 0) {
	    xi = Z[list[i]][t];
	    if (na(xi)) {
		return NADBL;
	    }
	    wxbar += xi * w;
	}
    }

    return wxbar;
}

/* Computes weighted mean of the variables in @list using the
   (possibly time-varying) weights given in @wlist, or the
   unweighted mean if @wlist is NULL.
*/

static int x_sectional_weighted_mean (double *x, const int *list, 
				      const int *wlist,
				      const double **Z, 
				      const DATAINFO *pdinfo)
{
    int n = list[0];
    int t, v;

    if (n == 0) {
	return 0; /* all NAs */
    } else if (n == 1) {
	v = list[1];
	for (t=pdinfo->t1; t<=pdinfo->t2; t++) {
	    x[t] = Z[v][t]; 
	}
	return 0;
    }

    for (t=pdinfo->t1; t<=pdinfo->t2; t++) {
	if (wlist != NULL) {
	    x[t] = weighted_mean_at_obs(list, wlist, Z, t, NULL, NULL);
	} else {
	    x[t] = mean_at_obs(list, Z, t);
	}
    }

    return 0;
}

/* Computes weighted sample variance of the variables in @list using
   the (possibly time-varying) weights given in @wlist, or the
   unweighted sample variance if @wlist is NULL
*/

static int x_sectional_wtd_variance (double *x, const int *list,
				     const int *wlist,
				     const double **Z, 
				     const DATAINFO *pdinfo)
{
    double xdev, xbar, wsum;
    int m = 0, n = list[0];
    int i, t, v;

    if (n == 0) {
	return 0; /* all NAs */
    } else if (n == 1) {
	for (t=pdinfo->t1; t<=pdinfo->t2; t++) {
	    x[t] = 0.0;
	}
	return 0;
    }

    for (t=pdinfo->t1; t<=pdinfo->t2; t++) {
	if (wlist != NULL) {
	    xbar = weighted_mean_at_obs(list, wlist, Z, t, &wsum, &m);
	} else {
	    xbar = mean_at_obs(list, Z, t);
	}
	if (na(xbar)) {
	    x[t] = NADBL;
	    continue;
	}
	if (wlist != NULL && m < 2) {
	    x[t] = (m == 1)? 0.0 : NADBL;
	    continue;
	}
	x[t] = 0.0;
	for (i=1; i<=list[0]; i++) {
	    v = list[i];
	    xdev = Z[v][t] - xbar;
	    if (wlist != NULL) {
		x[t] += xdev * xdev * Z[wlist[i]][t] / wsum;
	    } else {
		x[t] += xdev * xdev;
	    }
	}
	if (wlist != NULL) {
	    x[t] *= m / (m - 1);
	} else {
	    x[t] /= (n - 1);
	}
    }

    return 0;
}

static int x_sectional_wtd_stddev (double *x, const int *list, 
				   const int *wlist,
				   const double **Z, 
				   const DATAINFO *pdinfo)
{
    int t, err;

    err = x_sectional_wtd_variance(x, list, wlist, Z, pdinfo);

    if (!err) {
	for (t=pdinfo->t1; t<=pdinfo->t2; t++) {
	    if (!na(x[t])) {
		x[t] = sqrt(x[t]);
	    }
	}
    }

    return err;
}

static int x_sectional_extremum (int f, double *x, const int *list, 
				 const int *wlist,
				 const double **Z, 
				 const DATAINFO *pdinfo)
{
    double xit, xx;
    int i, t, err = 0;

    for (t=pdinfo->t1; t<=pdinfo->t2; t++) {
	xx = (f == F_MIN)? NADBL : -NADBL;
	for (i=1; i<=list[0]; i++) {
	    xit = Z[list[i]][t];
	    if (!na(xit)) { 
		if (f == F_MAX && xit > xx) {
		    xx = xit;
		} else if (f == F_MIN && xit < xx) {
		    xx = xit;
		}
	    }
	}
	if (xx == -NADBL) {
	    x[t] = NADBL;
	} else {
	    x[t] = xx;
	}
    }

    return err;
}

static int x_sectional_sum (double *x, const int *list, 
			    const double **Z, 
			    const DATAINFO *pdinfo)
{
    double xit, xx;
    int i, t, err = 0;

    for (t=pdinfo->t1; t<=pdinfo->t2; t++) {
	xx = 0.0;
	for (i=1; i<=list[0]; i++) {
	    xit = Z[list[i]][t];
	    if (na(xit)) { 
		xx = NADBL;
		break;
	    } else {
		xx += xit;
	    }
	}
	x[t] = xx;
    }

    return err;
}

int cross_sectional_stat (double *x, const int *list, 
			  const double **Z, 
			  const DATAINFO *pdinfo,
			  int f)
{
    if (f == F_MEAN) {
	return x_sectional_weighted_mean(x, list, NULL, Z, pdinfo);
    } else if (f == F_VCE) {
	return x_sectional_wtd_variance(x, list, NULL, Z, pdinfo);
    } else if (f == F_SD) {
	return x_sectional_wtd_stddev(x, list, NULL, Z, pdinfo);
    } else if (f == F_MIN || f == F_MAX) {
	return x_sectional_extremum(f, x, list, NULL, Z, pdinfo);
    } else if (f == F_SUM) {
	return x_sectional_sum(x, list, Z, pdinfo);
    } else {
	return E_DATA;
    }
}

int x_sectional_weighted_stat (double *x, const int *list, 
			       const int *wlist,
			       const double **Z, 
			       const DATAINFO *pdinfo,
			       int f)
{
    if (wlist[0] != list[0]) {
	gretl_errmsg_sprintf("Weighted stats: data list has %d members but weight "
			     "list has %d", list[0], wlist[0]);
	return E_DATA;
    }

    if (f == F_WMEAN) {
	return x_sectional_weighted_mean(x, list, wlist, Z, pdinfo);
    } else if (f == F_WVAR) {
	return x_sectional_wtd_variance(x, list, wlist, Z, pdinfo);
    } else if (f == F_WSD) {
	return x_sectional_wtd_stddev(x, list, wlist, Z, pdinfo);
    } else {
	return E_DATA;
    }
}

/* writes to the series @y a linear combination of the variables given
   in @list, using the coefficients given in the vector @b.
*/

int list_linear_combo (double *y, const int *list, 
		       const gretl_vector *b, const double **Z, 
		       const DATAINFO *pdinfo)
{
    int nb = gretl_vector_get_length(b);
    int nl = list[0];
    int err = 0;

    if (nb != nl) {
	err = E_DATA;
    } else {
	int i, t;
	double xit, yt;

	for (t=pdinfo->t1; t<=pdinfo->t2; t++) {
	    yt = 0;
	    for (i=0; i<nl; i++) {
		xit = Z[list[i+1]][t];
		if (na(xit)) {
		    yt = NADBL;
		    break;
		} else {
		    yt += xit * gretl_vector_get(b, i);
		}
	    }
	    y[t] = yt;
	}
    }

    return err;
}

/* Imhof: draws on the RATS code in IMHOF.SRC from Estima, 2004.

   Imhof Procedure for computing P(u'Au < x) for a quadratic form in
   Normal(0,1) variables. This can be used for ratios of quadratic
   forms as well, since P((u'Au/u'Bu) < x) = P(u'(A-xB)u < 0).

   In the Durbin-Watson context, 'B' is the identity matrix and
   'x' is the Durbin-Watson statistic.

   References:

   Imhof, J.P (1961), Computing the Distribution of Quadratic Forms of
   Normal Variables, Biometrika 48, 419-426.

   Abrahamse, A.P.J and Koerts, J. (1969), On the Theory and
   Application of the General Linear Model, Rotterdam University
   Press.
*/

static double imhof_bound (const double *lambda, int k, int *err)
{
    double e1 = 0.0001; /* Max truncation error due to finite 
			   upper bound on domain */
    double e2 = 0.0001; /* Cutoff for deciding whether an 
			   eigenvalue is effectively zero */
    double absl, bound;
    double nl = 0.0, sum = 0.0;
    int i;

    for (i=0; i<k; i++) {
	absl = fabs(lambda[i]);
	if (absl > e2) {
	    nl += 1.0;
	    sum += log(absl);
	}
    }

    if (nl == 0.0) {
	fprintf(stderr, "imhof_bound: got no non-zero eigenvalues\n");
	*err = E_DATA;
	return NADBL;
    }

    /* The key factor in the integrand is the product of 
       (1+(lambda(i)*x)**2)**(1/4) across i. Since, for those 
       factors with small |lambda(i)|, this won't go to zero very
       quickly, we count only the terms on the bigger eigenvalues.
    */

    nl = 0.5 * nl;
    sum = 0.5 * sum + log(M_PI * nl);
    bound = exp(-(sum + log(e1)) / nl);
    bound += 5.0 / nl;

    if (bound < 0) {
	fprintf(stderr, "imhof_bound: got negative result\n");
	*err = E_DATA;
	bound = NADBL;
    }

    return bound;
}

static double vecsum (const double *x, int k)
{
    double sum = 0.0;
    int i;

    for (i=0; i<k; i++) {
	sum += x[i];
    }

    return sum;
}

static double imhof_f (double u, const double *lambda, int k, double arg)
{
    double ul, rho = 0.0;
    double theta = -u * arg;
    int i;

    /* The value at zero isn't directly computable as
       it produces 0/0. The limit is computed below.
    */
    if (u == 0.0) {
	return 0.5 * (-arg + vecsum(lambda, k));
    }

    for (i=0; i<k; i++) {
	ul = u * lambda[i];
	theta += atan(ul);
	rho += log(1.0 + ul * ul);
    }

    return sin(0.5 * theta) / (u * exp(0.25 * rho));
}

#define gridlimit 2048

/*
  Adaptation of Abrahamse and Koert's Pascal code.  Evaluates the
  integral by Simpson's rule with grid size halving each time until
  the change from moving to a tighter grid is negligible. By halving
  the grid, only the odd terms need to be computed, as the old ones
  are already in the sum. Points entering get x 4 weights, which then
  get reduced to x 2 on the next iteration.
*/

static double imhof_integral (double arg, const double *lambda, int k,
			      double bound, int *err)
{
    double e3 = 0.0001;
    double base, step, sum1;
    double int0 = 0.0, int1 = 0.0;
    double eps4 = 3.0 * M_PI * e3;
    double sum4 = 0.0;
    double ret = NADBL;
    int j, n = 2;

    base = imhof_f(0, lambda, k, arg);
    base += imhof_f(bound, lambda, k, arg);

    while (n < gridlimit) {
	step = bound / n;
	sum1 = base + sum4 * 2.0;
	base = sum1;
	sum4 = 0.0;
	for (j=1; j<=n; j+=2) {
	    sum4 += imhof_f(j * step, lambda, k, arg);
	}
	int1 = (sum1 + 4 * sum4) * step;
	if (n > 8 && fabs(int1 - int0) < eps4) {
	    break;
	}
	int0 = int1;
	n *= 2;
    }

    if (n > gridlimit) {
	fprintf(stderr, "n = %d, Imhof integral failed to converge\n", n);
	*err = E_NOCONV;
    } else {
	ret = 0.5 - int1 / (3.0 * M_PI);
	if (ret < 0 && ret > -1.0e-14) {
	    ret = 0.0;
	} else if (ret < 0) {
	    fprintf(stderr, "n = %d, Imhof integral gave negative value %g\n", n, ret);
	    *err = E_DATA;
	    ret = NADBL;
	}
    }

    return ret;
}

static int imhof_get_eigenvals (const gretl_matrix *m,
				double **plam, int *pk)
{
    gretl_matrix *E, *A;
    int err = 0;

    A = gretl_matrix_copy(m);
    if (A == NULL) {
	return E_ALLOC;
    }

    E = gretl_general_matrix_eigenvals(A, 0, &err);

    if (!err) {
	*pk = E->rows;
	*plam = gretl_matrix_steal_data(E);
    }

    gretl_matrix_free(A);
    gretl_matrix_free(E);

    return err;
}

/* Implements the "imhof" function in genr: computes the probability
   P(u'Au < arg) for a quadratic form in Normal(0,1) variables.  The
   argument @m may be either the square matrix A or a column
   vector containing the precomputed eigenvalues of A.
*/

double imhof (const gretl_matrix *m, double arg, int *err)
{
    double *lambda = NULL;
    double bound, ret = NADBL;
    int k = 0, free_lambda = 0;

    errno = 0;

    if (m->cols == 1) {
	/* we'll assume m is a column vector of eigenvalues */
	lambda = m->val;
	k = m->rows;
    } else if (m->rows == m->cols) {
	/* we'll assume m is the 'A' matrix */
	*err = imhof_get_eigenvals(m, &lambda, &k);
	free_lambda = 1;
    } else {
	/* huh? */
	*err = E_DATA;
    }

    if (!*err) {
	bound = imhof_bound(lambda, k, err);
    }

    if (!*err) {
	ret = imhof_integral(arg, lambda, k, bound, err);
    }

    if (errno != 0) {
	fprintf(stderr, "imhof: %s\n", strerror(errno));
	if (!*err) {
	    *err = E_NOCONV;
	}
	ret = NADBL;
	errno = 0;
    }

    if (free_lambda) {
	free(lambda);
    }

    return ret;
}

/* Implements the "dwpval" function in genr: given the residual vector
   @u and the matrix of regressors, @X, calculates the Durbin-Watson
   statistic then finds its p-value via the Imhof/Koerts/Abrahamse
   procedure.
*/

double dw_pval (const gretl_matrix *u, const gretl_matrix *X, 
		double *pDW, int *perr)
{
    gretl_matrix *M = NULL;
    gretl_matrix *A = NULL;
    gretl_matrix *MA = NULL;
    gretl_matrix *XX = NULL;
    gretl_matrix *E = NULL;
    double uu, DW;
    double pv = NADBL;
    int k = X->cols;
    int n = X->rows;
    int i, err = 0;

    M = gretl_identity_matrix_new(n);
    A = gretl_DW_matrix_new(n);
    MA = gretl_matrix_alloc(n, n);
    XX = gretl_matrix_alloc(k, k);

    if (M == NULL || A == NULL || MA == NULL || XX == NULL) {
	err = E_ALLOC;
	goto bailout;
    }

    gretl_matrix_multiply_mod(X, GRETL_MOD_TRANSPOSE,
			      X, GRETL_MOD_NONE,
			      XX, GRETL_MOD_NONE);

    err = gretl_invert_symmetric_matrix(XX);

    if (!err) {
	/* M = I - X(X'X)^{-1}X' */
	err = gretl_matrix_qform(X, GRETL_MOD_NONE,
				 XX, M, GRETL_MOD_DECREMENT);
    }

    if (!err) {
	err = gretl_matrix_multiply(M, A, MA);
    }

    if (!err) {
	uu = gretl_matrix_dot_product(u, GRETL_MOD_TRANSPOSE,
				      u, GRETL_MOD_NONE,
				      &err);
    }

    if (!err) {
	DW = gretl_scalar_qform(u, A, &err);
    }

    if (!err) {
	DW /= uu;
	E = gretl_general_matrix_eigenvals(MA, 0, &err);
    }

    if (!err) {
	k = n - k;
	for (i=0; i<k; i++) {
	    E->val[i] -= DW;
	}
	gretl_matrix_reuse(E, k, 1);
	pv = imhof(E, 0.0, &err);
	if (!err && pDW != NULL) {
	    *pDW = DW;
	}
    }

 bailout:

    gretl_matrix_free(M);
    gretl_matrix_free(A);
    gretl_matrix_free(MA);
    gretl_matrix_free(XX);
    gretl_matrix_free(E);

    *perr = err;

    return pv;
}

/* create a matrix containing ACF and PACF values for each
   column of the input matrix, @m, with lag order @p.
*/

gretl_matrix *multi_acf (const gretl_matrix *m, 
			 const int *list,
			 const double **Z,
			 const DATAINFO *pdinfo,
			 int p, int *err)
{
    gretl_matrix *a, *A = NULL;
    const double *x;
    double xa;
    int nv, T, acol, pcol;
    int i, j;
    
    if (list == NULL && gretl_is_null_matrix(m)) {
	*err = E_DATA;
	return NULL;
    }

    if (m != NULL) {
	nv = m->cols;
    } else {
	nv = list[0];
    }

    A = gretl_matrix_alloc(p, 2 * nv);
    if (A == NULL) {
	*err = E_ALLOC;
	return NULL;
    }

    if (m != NULL) {
	x = m->val;
	T = m->rows;
    } else {
	x = Z[list[1]] + pdinfo->t1;
	T = sample_size(pdinfo);
    }

    acol = 0;
    pcol = nv;

    for (j=0; j<nv; j++) {
	/* get ACF/PACF for column/series */
	a = acf_vec(x, p, NULL, T, err);
	if (*err) {
	    gretl_matrix_free(a);
	    gretl_matrix_free(A);
	    return NULL;
	}

	/* transcribe into A matrix and free */
	for (i=0; i<p; i++) {
	    xa = gretl_matrix_get(a, i, 0);
	    gretl_matrix_set(A, i, acol, xa);
	    xa = gretl_matrix_get(a, i, 1);
	    gretl_matrix_set(A, i, pcol, xa);
	}
	acol++;
	pcol++;

	gretl_matrix_free(a);

	/* move to next data read position */
	if (j < nv - 1) {
	    if (m != NULL) {
		x += m->rows;
	    } else {
		x = Z[list[j+2]] + pdinfo->t1;
	    }
	}
    }

    return A;
}

gretl_matrix *multi_xcf (const void *px, int xtype,
			 const void *py, int ytype,
			 const double **Z,
			 const DATAINFO *pdinfo,
			 int p, int *err)
{
    const int *xlist = NULL;
    const gretl_matrix *Xmat = NULL;
    const double *xvec = NULL;
    const double *yvec = NULL;
    gretl_matrix *xj, *XCF = NULL;
    int T = sample_size(pdinfo);
    int np = 2 * p + 1;
    int Ty, nx = 1;
    int i, j;

    if (xtype == LVEC) {
	xlist = px;
	nx = xlist[0];
	if (nx < 1) {
	    *err = E_DATA;
	    return NULL;
	}	    
	xvec = Z[xlist[1]] + pdinfo->t1; 
    } else if (xtype == MAT) {
	Xmat = px;
	if (gretl_is_null_matrix(Xmat)) {
	    *err = E_DATA;
	    return NULL;
	}
	nx = Xmat->cols;
	T = Xmat->rows;
	xvec = Xmat->val;
    } else {
	/* VEC: note: px is of type *void */
	xvec = px;
	xvec += pdinfo->t1;
    }

    if (ytype == MAT) {
	const gretl_matrix *ymat = py;

	if (ymat->cols != 1) {
	    *err = E_NONCONF;
	    return NULL;
	}	  
	yvec = ymat->val;
	Ty = ymat->rows;
    } else {
	yvec = (const double *) py + pdinfo->t1;
	Ty = sample_size(pdinfo);
    }

    if (Ty != T) {
	*err = E_NONCONF;
	return NULL;
    }

    if (nx > 1) {
	XCF = gretl_matrix_alloc(np, nx);
	if (XCF == NULL) {
	    *err = E_ALLOC;
	    return NULL;
	}
    }

    for (j=0; j<nx; j++) {
	/* get XCF for left-hand column/series and y */
	xj = xcf_vec(xvec, yvec, p, NULL, T, err);
	if (*err) {
	    gretl_matrix_free(XCF);
	    return NULL;
	}

	if (nx == 1) {
	    XCF = xj;
	    break;
	} 

	/* transcribe into big XCF matrix and free */
	for (i=0; i<np; i++) {
	    gretl_matrix_set(XCF, i, j, xj->val[i]);
	}
	gretl_matrix_free(xj);

	/* move to next data read position */
	if (j < nx - 1) {
	    if (Xmat != NULL) {
		xvec += Xmat->rows;
	    } else {
		xvec = Z[xlist[j+2]] + pdinfo->t1;
	    }
	}
    }

    return XCF;
}

static int theil_decomp (double *m, double MSE,
			 const double *y, const double *f,
			 int t1, int t2)
{
    double da, dp;
    double Abar, Pbar;
    double sa, sp, r;
    int t, T = t2 - t1 + 1;
    int err = 0;

    if (MSE <= 0.0) {
	m[0] = m[1] = m[2] = M_NA;
	return E_DATA;
    }

    Abar = Pbar = 0.0;

    for (t=t1; t<=t2; t++) {
	Abar += y[t];
	Pbar += f[t];
    }

    Abar /= T;
    Pbar /= T;

    sa = sp = r = 0.0;

    for (t=t1; t<=t2; t++) {
	da = y[t] - Abar;
	sa += da * da;
	dp = f[t] - Pbar;
	sp += dp * dp;
	r += da * dp;
    }    

    sa = sqrt(sa / T);
    sp = sqrt(sp / T);
    r /= T * sa * sp;

    if (sa == 0.0 || sp == 0.0) {
	err = E_DATA;
	m[0] = m[1] = m[2] = M_NA;
    } else {
	m[0] = (Abar - Pbar) * (Abar - Pbar) / MSE; /* U^M */
	m[1] = (sp - r * sa) * (sp - r * sa) / MSE; /* U^R */
	m[2] = (1.0 - r * r) * sa * sa / MSE;       /* U^D */

	if (m[2] > 0.99999999999999) {
	    /* U^M and U^R are just machine noise? */
	    m[2] = 1.0;
	    m[0] = m[1] = 0.0;
	} 
    }

    return err;
}

/* cf. http://www.economicsnetwork.ac.uk/showcase/cook_forecast
   by Steven Cook of Swansea University <s.cook@Swansea.ac.uk>

   OPT_D indicates that we should include the Theil decomposition.
*/

gretl_matrix *forecast_stats (const double *y, const double *f,
			      int t1, int t2, gretlopt opt,
			      int *err)
{
    gretl_matrix *m = NULL;
    double ME, MSE, MAE, MPE, MAPE, U;
    double x, u[2];
    int t, T = t2 - t1 + 1;

    for (t=t1; t<=t2; t++) {
	if (na(y[t]) || na(f[t])) {
	    *err = E_MISSDATA;
	    return NULL;
	}
    }

    ME = MSE = MAE = MPE = MAPE = U = 0.0;
    u[0] = u[1] = 0.0;

    for (t=t1; t<=t2; t++) {
	x = y[t] - f[t];
	ME += x;
	MSE += x * x;
	MAE += fabs(x);
	if (y[t] == 0.0) {
	    MPE = MAPE = U = M_NA;
	} else {
	    MPE += 100 * x / y[t];
	    MAPE += 100 * fabs(x) / y[t];
	    if (t < t2) {
		x = (f[t+1] - y[t+1]) / y[t];
		u[0] += x * x;
		x = (y[t+1] - y[t]) / y[t];
		u[1] += x * x;
	    }
	}
    }

    ME /= T;
    MSE /= T;
    MAE /= T;

    if (!isnan(MPE)) {
	MPE /= T;
    }

    if (!isnan(MAPE)) {
	MAPE /= T;
    }

    if (!isnan(U) && u[1] > 0.0) {
	U = sqrt(u[0] / T) / sqrt(u[1] / T);
    }

    if (opt & OPT_D) {
	m = gretl_column_vector_alloc(9);
    } else {
	m = gretl_column_vector_alloc(6);
    }
    
    if (m == NULL) {
	*err = E_ALLOC;
	return NULL;
    }

    gretl_vector_set(m, 0, ME);
    gretl_vector_set(m, 1, MSE);
    gretl_vector_set(m, 2, MAE);
    gretl_vector_set(m, 3, MPE);
    gretl_vector_set(m, 4, MAPE);
    gretl_vector_set(m, 5, U);

    if (opt & OPT_D) {
	theil_decomp(m->val + 6, MSE, y, f, t1, t2);
    } 

    return m;
}

double gretl_round (double x)
{
    double fx = floor(x);

    if (x < 0) {
	return (x - fx <= 0.5)? fx : ceil(x);
    } else {
	return (x - fx < 0.5)? fx : ceil(x);
    }
}

#define neg_x_real_v_err(t) (t == 'J' || t == 'I')

/* evaluates bessel function for scalar nu and x */

double gretl_bessel (char type, double v, double x, int *err)
{
    if (na(x) || na(v)) {
	return NADBL;
    }

    if (x < 0) {
	/* catch invalid cases for x < 0 */
	if (type == 'K') {
	    *err = E_INVARG;
	    return NADBL;
	} else if (v != floor(v) && (type == 'J' || type == 'I')) {
	    *err = E_INVARG;
	    return NADBL;
	}
    }

    switch (type) {
    case 'J':
	return cephes_bessel_Jv(v, x);
    case 'Y':
	return cephes_bessel_Yv(v, x);
    case 'I':
	if (v == 0) {
	    return cephes_bessel_I0(x);
	} else if (v == 1) {
	    return cephes_bessel_I1(x);
	} else if (v > 0) {
	    return cephes_bessel_Iv(v, x);
	} else {
	    /* cephes_bessel_Iv is not right for v < 0 */
	    double b1 = netlib_bessel_K(-v, x, 1);
	    double b2 = cephes_bessel_Iv(-v, x);

	    return (2*b1*sin(-v*M_PI)) / M_PI + b2;
	}
	break;
    case 'K':
	/* bessel K is symmetric around v = 0 */
	v = fabs(v);
	if (v == 0) {
	    return cephes_bessel_K0(x);
	} else if (v == 1) {
	    return cephes_bessel_K1(x);
	} else if (v == floor(v) && v <= 30.0) {
	    /* cephes doesn't do non-integer v, and also loses
	       accuracy beyond |v| = 30
	    */
	    return cephes_bessel_Kn(v, x);
	} else {
	    /* accurate but expensive */
	    return netlib_bessel_K(v, x, 1);
	}
	break;
    default:
	/* unknown function type */
	return NADBL;
    }
}

/* Net Present Value: note that the first value is taken as
   occurring "now" and is not discounted */

double gretl_npv (int t1, int t2, const double *x, double r, 
		  int pd, int *err)
{
    double d, PV = 0.0;
    int i, n = 0;

    if (pd != 1 && pd != 4 && pd != 12) {
	*err = E_PDWRONG;
	return NADBL;
    }

    if (pd == 1) {
	d = 1 + r;
    } else if (r < -1.0) {
	*err = E_NAN;
	return 0.0 / 0.0;
    } else {
	d = pow(1 + r, 1.0 / pd);
    }

    for (i=t1; i<=t2; i++) {
	if (!na(x[i])) {
	    PV += x[i] / (pow(d, i-t1));
	    n++;
	}
    }

    if (n == 0) {
	PV = NADBL;
    }

    return PV;
}

/* Internal Rate of Return */

double gretl_irr (const double *x, int n, int pd, int *err)
{
    double PV, r, r1 = 0.02, r0 = -0.02;
    int gotplus = 0, gotminus = 0;
    int i, m = n;

    for (i=0; i<n; i++) {
	if (na(x[i])) {
	    m--;
	} else if (x[i] > 0) {
	    gotplus = 1;
	} else if (x[i] < 0) {
	    gotminus = 1;
	}
    }

    if (!gotplus && !gotminus) {
	/* null payment stream */
	return (m > 0)? 0 : NADBL;
    }

    if (gotplus && !gotminus) {
	/* payments all positive */
	return (x[0] > 0)? 0.0/0.0 : 1.0/0.0;
    } else if (gotminus && !gotplus) {
	/* payments all negative */
	return (x[0] < 0)? 0.0/0.0 : -1.0/0.0;
    }

    /* find (r0, r1) bracket for solution, if possible */

    while ((PV = gretl_npv(0, n-1, x, r0, pd, err)) < 0 && !*err) {
	if (r0 < -DBL_MAX / 2.0) {
	    return -1.0/0.0;
	}
	r1 = r0;
	r0 *= 2.0;
    }

    while ((PV = gretl_npv(0, n-1, x, r1, pd, err)) > 0 && !*err) {
	if (r1 > DBL_MAX / 2.0) {
	    return 1.0/0.0;
	}
	r0 = r1;
	r1 *= 2.0;
    } 

#if 0
    fprintf(stderr, "initial bracket for r: %g to %g\n", r0, r1);
#endif

    r = r1;

    /* now do binary search */

    for (i=0; i<32 && !*err; i++) {
	if (floateq(PV, 0.0)) {
	    break;
	}
	if (PV < 0) {
	    /* r is too high */
	    if (r < r1) {
		r1 = r;
	    }
	    r = (r + r0) / 2.0;
	} else {
	    /* r too low */
	    if (r > r0) {
		r0 = r;
	    }
	    r = (r + r1) / 2.0;
	}
	PV = gretl_npv(0, n-1, x, r, pd, err);
#if 0
	fprintf(stderr, "binary search: r = %.9g, PV = %g\n", r, PV);
#endif
    }

    if (*err) {
	r = NADBL;
    }

    return r;
}

double logistic_cdf (double x)
{
    double emx, ret;

    errno = 0;

    emx = exp(-x);

    if (errno) {
	errno = 0;
	return NADBL;
    }

    ret = 1.0 / (1.0 + emx);

    if (errno) {
	ret = NADBL;
	errno = 0;
    }

    return ret;
}
