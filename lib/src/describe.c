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

/* describe.c - gretl descriptive statistics */

#include "libgretl.h"
#include "gretl_matrix.h"
#include "gretl_fft.h"
#include "matrix_extra.h"
#include "gretl_panel.h"
#include "libset.h"
#include "compat.h"
#include "plotspec.h"
#include "usermat.h"

#include <unistd.h>

#ifdef WIN32
# include <windows.h>
#endif

#include <glib.h>

/* return 1 if series x has any missing observations, 0 if it does
   not */

static int missvals (const double *x, int n)
{
    int t;
    
    for (t=0; t<n; t++) {
	if (na(x[t])) {
	    return 1;
	}
    }
	    
    return 0;
}

/* return the number of "good" (non-missing) observations in series x;
   also (optionally) write to x0 the first non-missing value */

static int good_obs (const double *x, int n, double *x0)
{
    int t, good = n;

    if (x0 != NULL) {
	*x0 = NADBL;
    }

    for (t=0; t<n; t++) {
	if (na(x[t])) {
	    good--;
	} else if (x0 != NULL && na(*x0)) {
	    *x0 = x[t];
	}
    }

    return good;
}

/**
 * gretl_minmax:
 * @t1: starting observation.
 * @t2: ending observation.
 * @x: data series.
 * @min: location to receive minimum value.
 * @max: location to receive maximum value.
 *
 * Puts the minimum and maximum values of the series @x,
 * from obs @t1 to obs @t2, into the variables @min and @max.
 *
 * Returns: the number of valid observations in the given
 * data range.
 */

int gretl_minmax (int t1, int t2, const double *x, 
		  double *min, double *max)
{
    int t, n = 0;

    while (na(x[t1]) && t1 <= t2) {
	t1++;
    }

    if (t1 > t2) {
        *min = *max = NADBL;
        return 0;
    }

    *min = *max = x[t1];

    for (t=t1; t<=t2; t++) {
	if (!(na(x[t]))) {
	    if (x[t] > *max) *max = x[t];
	    if (x[t] < *min) *min = x[t];
	    n++;
	}
    }

    return n;
}

/**
 * gretl_min:
 * @t1: starting observation.
 * @t2: ending observation.
 * @x: data series.
 *
 * Returns: the minimum value of @x over the given range,
 * or #NADBL if no valid vaues are found.
 */

double gretl_min (int t1, int t2, const double *x)
{
    double min, max;

    gretl_minmax(t1, t2, x, &min, &max);
    return min;
}

/**
 * gretl_max:
 * @t1: starting observation.
 * @t2: ending observation.
 * @x: data series.
 *
 * Returns: the maximum value of @x over the given range,
 * or #NADBL if no valid vaues are found.
 */

double gretl_max (int t1, int t2, const double *x)
{
    double min, max;

    gretl_minmax(t1, t2, x, &min, &max);
    return max;
}

/**
 * gretl_sum:
 * @t1: starting observation.
 * @t2: ending observation.
 * @x: data series.
 *
 * Returns: the sum of the series @x from obs
 * @t1 to obs @t2, skipping any missing values, or #NADBL 
 * in case there are no valid observations.
 */

double gretl_sum (int t1, int t2, const double *x)
{
    double sum = 0.0;
    int t, n = 0;

    for (t=t1; t<=t2; t++) {
	if (!(na(x[t]))) {
	    sum += x[t];
	    n++;
	}
    }
    
    return (n == 0)? NADBL : sum;
}

/**
 * gretl_mean:
 * @t1: starting observation.
 * @t2: ending observation.
 * @x: data series.
 *
 * Returns: the arithmetic mean of the series @x from obs
 * @t1 to obs @t2, skipping any missing values, or #NADBL 
 * in case there are no valid observations.
 */

double gretl_mean (int t1, int t2, const double *x)
{
    double xbar, sum = 0.0;
    int t, n = 0;

    for (t=t1; t<=t2; t++) {
	if (!(na(x[t]))) {
	    sum += x[t];
	    n++;
	} 
    }

    if (n == 0) {
	return NADBL;
    }

    xbar = sum / n;
    sum = 0.0;

    for (t=t1; t<=t2; t++) {
	if (!(na(x[t]))) {
	    sum += (x[t] - xbar); 
	}
    }

    return xbar + sum / n;
}

/**
 * eval_ytest:
 * @y: reference value.
 * @op: operator.
 * @test: test value.
 *
 * Returns: 1 if the expression @y @yop @test (for example
 * "y = 2" or "y <= 45") evaluates as true, else 0.
 */

int eval_ytest (double y, GretlOp op, double test)
{
    int ret = 0;

    switch (op) {
    case OP_EQ:
	ret = (y == test);
	break;
    case OP_GT:
	ret = (y > test);
	break;
    case OP_LT:
	ret = (y < test);
	break;
    case OP_NEQ:
	ret = (y != test);
	break;
    case OP_GTE:
	ret = (y >= test);
	break;
    case OP_LTE:
	ret = (y <= test);
	break;
    default:
	break;
    }

    return ret;
}

/**
 * gretl_restricted_mean:
 * @t1: starting observation.
 * @t2: ending observation.
 * @x: data series.
 * @y: criterion series.
 * @yop: criterion operator.
 * @yval: criterion value.
 *
 * Returns: the arithmetic mean of the series @x in the
 * range @t1 to @t2 (inclusive), but including only
 * observations where the criterion variable @y bears the
 * relationship @yop to the value @yval -- or #NADBL in case 
 * there are no observations that satisfy the restriction.
 */

double gretl_restricted_mean (int t1, int t2, const double *x,
			      const double *y, GretlOp yop, 
			      double yval)
{
    int n;
    register int t;
    double xbar, sum = 0.0;

    n = t2 - t1 + 1;
    if (n <= 0) {
	return NADBL;
    }

    for (t=t1; t<=t2; t++) {
	if (!na(x[t]) && eval_ytest(y[t], yop, yval)) {
	    sum += x[t];
	} else {
	    n--;
	}
    }

    if (n == 0) {
	return NADBL;
    }

    xbar = sum / n;
    sum = 0.0;

    for (t=t1; t<=t2; t++) {
	if (!na(x[t]) && eval_ytest(y[t], yop, yval)) {
	    sum += (x[t] - xbar); 
	}
    }

    return xbar + sum / n;
}

/* 
   For an array a with n elements, find the element which would be a[k]
   if the array were sorted from smallest to largest (without the need
   to do a full sort).  The algorithm is due to C. A. R. Hoare; the C
   implementation is shamelessly copied, with minor adaptations, from
   http://www.astro.northwestern.edu/~ato/um/programming/sorting.html
   (Atakan G\"urkan).
*/

static double find_hoare (double *a, int n, int k)
{
    double w, x;
    int l = 0;
    int r = n - 1;
    int i, j;

    while (l < r) {
	x = a[k]; 
	i = l; 
	j = r;
	while (j >= i) {
	    while (a[i] < x) i++;
	    while (x < a[j]) j--;
	    if (i <= j) {
		w = a[i]; 
		a[i++] = a[j]; 
		a[j--] = w;
	    }
	}
	if (j < k) l = i;
	if (k < i) r = j;
    }

    return a[k];
}

static double find_hoare_inexact (double *a, double p,
				  double xmin, double xmax,
				  double frac, int n, 
				  int nl, int nh)
{
    double tmp, high, low;
    int i;

    if (p < 0.5) {
	high = find_hoare(a, n, nh);
	tmp = xmin;
	for (i=0; i<nh; i++) {
	    if (a[i] > tmp) {
		tmp = a[i];
	    }
	}
	low = tmp;
    } else {
	low = find_hoare(a, n, nl);
	tmp = xmax;
	for (i=nh; i<n; i++) {
	    if (a[i] < tmp) {
		tmp = a[i];
	    }
	}
	high = tmp;
    }

    return low + frac * (high - low);
}

/**
 * gretl_quantile:
 * @t1: starting observation.
 * @t2: ending observation.
 * @x: data series.
 * @p: probability.
 * @err: location to receive error code.
 *
 * Returns: the @p quantile of the series @x from obs
 * @t1 to obs @t2, skipping any missing values, or #NADBL 
 * on failure.
 */

double gretl_quantile (int t1, int t2, const double *x, double p,
		       int *err)
{
    double *a = NULL;
    double xmin, xmax;
    double N, ret;
    int nl, nh;
    int t, n = 0;

    if (p <= 0 || p >= 1) {
	/* sanity check */
	*err = E_DATA;
	return NADBL;
    }

    n = gretl_minmax(t1, t2, x, &xmin, &xmax);
    if (n == 0) {
	*err = E_DATA;
	return NADBL;
    }

    a = malloc(n * sizeof *a);
    if (a == NULL) {
	*err = E_ALLOC;
	return NADBL;
    }

    n = 0;
    for (t=t1; t<=t2; t++) {
	if (!na(x[t])) {
	    a[n++] = x[t];
	}
    }

    N = (n + 1) * p - 1;
    nl = floor(N);
    nh = ceil(N);

    if (nh == 0 || nh == n) {
	/* too few usable observations for such an extreme 
	   quantile */
	*err = E_DATA;
	fprintf(stderr, "n = %d: not enough data for %g quantile\n",
		n, p);
	free(a);
	return NADBL;
    }

#if 0
    fprintf(stderr, "\tp = %12.10f, n = %d, N = %12.10f, nl = %d, nh = %d\n", 
	    p, n, N, nl, nh);
#endif
    
    if (nl == nh) {
	/* "exact" */
	ret = find_hoare(a, n, nl);
    } else {
	ret = find_hoare_inexact(a, p, xmin, xmax, N - nl, 
				 n, nl, nh);
    }
    
    free(a);

    return ret;
}

/**
 * gretl_array_quantiles:
 * @a: data array (this gets re-ordered).
 * @n: length of array.
 * @p: array of probabilities (over-written by quantiles).
 * @k: number of probabilities.
 *
 * Computes @k quantiles (given by the elements of @p) for the 
 * first n elements of the array @a, which is re-ordered in
 * the process.  On successful exit, @p contains the quantiles.
 *
 * Returns: 0 on success, non-zero code on error.
 */

int gretl_array_quantiles (double *a, int n, double *p, int k)
{
    double N, xmin, xmax = NADBL;
    int nl, nh, i;
    int err = 0;

    if (n <= 0 || k <= 0) {
	return E_DATA;
    }

    for (i=0; i<k; i++) {
	if (p[i] <= 0.0 || p[i] >= 1.0) {
	    p[i] = NADBL;
	    err = 1;
	    continue;
	}

	N = (n + 1) * p[i] - 1;
	nl = floor(N);
	nh = ceil(N);

	if (nh == 0 || nh == n) {
	    p[i] = NADBL;
	} else if (nl == nh) {
	    p[i] = find_hoare(a, n, nl);
	} else {
	    if (na(xmax)) {
		gretl_minmax(0, n-1, a, &xmin, &xmax);
	    }
	    p[i] = find_hoare_inexact(a, p[i], xmin, xmax, N - nl, 
				      n, nl, nh);
	}
    }

    return err;
}

/**
 * gretl_array_quantile:
 * @a: array on which to operate.
 * @n: number of elements in @a.
 * @p: probability.
 *
 * Returns: the @p quantile of the first @n elements in @a,
 * which is re-ordered in the process, or #NADBL on
 * failure.
 */

double gretl_array_quantile (double *a, int n, double p)
{
    gretl_array_quantiles(a, n, &p, 1);
    return p;
}

/**
 * gretl_median:
 * @t1: starting observation.
 * @t2: ending observation.
 * @x: data series.
 *
 * Returns: the median value of the series @x from obs
 * @t1 to obs @t2, skipping any missing values, or #NADBL 
 * on failure.
 */

double gretl_median (int t1, int t2, const double *x)
{
    int err = 0;

    return gretl_quantile(t1, t2, x, 0.5, &err);
}

/**
 * gretl_sst:
 * @t1: starting observation.
 * @t2: ending observation.
 * @x: data series.
 *
 * Returns: the sum of squared deviations from the mean for
 * the series @x from obs @t1 to obs @t2, skipping any missing 
 * values, or #NADBL on failure.
 */

double gretl_sst (int t1, int t2, const double *x)
{
    int t, n = t2 - t1 + 1;
    double sumsq, xx, xbar;

    if (n == 0) {
	return NADBL;
    }

    xbar = gretl_mean(t1, t2, x);
    if (na(xbar)) {
	return NADBL;
    }

    sumsq = 0.0;

    for (t=t1; t<=t2; t++) {
	if (!na(x[t])) {
	    xx = x[t] - xbar;
	    sumsq += xx * xx;
	} 
    }

    return sumsq;
}

/**
 * gretl_variance:
 * @t1: starting observation.
 * @t2: ending observation.
 * @x: data series.
 *
 * Returns: the variance of the series @x from obs
 * @t1 to obs @t2, skipping any missing values, or #NADBL 
 * on failure.
 */

double gretl_variance (int t1, int t2, const double *x)
{
    int t, n = t2 - t1 + 1;
    double v, xx, xbar;

    if (n == 0) {
	/* void sample */
	return NADBL;
    }

    xbar = gretl_mean(t1, t2, x);
    if (na(xbar)) {
	return NADBL;
    }

    v = 0.0;
    n = 0;

    for (t=t1; t<=t2; t++) {
	if (!na(x[t])) {
	    xx = x[t] - xbar;
	    v += xx * xx;
	    n++;
	}
    }

    v = (n > 1)? v / (n - 1) : 0.0;

    return (v >= 0)? v : NADBL;
}

/**
 * gretl_restricted_variance:
 * @t1: starting observation.
 * @t2: ending observation.
 * @x: data series.
 * @y: criterion series.
 * @yop: criterion operator. 
 * @yval: criterion value.
 *
 * Returns: the variance of the series @x from obs
 * @t1 to obs @t2, skipping any missing values and
 * observations where the series @y does not bear the
 * relationship @yop to the value @yval, or #NADBL on 
 * failure.
 */

double gretl_restricted_variance (int t1, int t2, const double *x,
				  const double *y, GretlOp yop, 
				  double yval)
{
    int t, n = t2 - t1 + 1;
    double sumsq, xx, xbar;

    if (n == 0) {
	return NADBL;
    }

    xbar = gretl_restricted_mean(t1, t2, x, y, yop, yval);
    if (na(xbar)) {
	return NADBL;
    }

    sumsq = 0.0;

    for (t=t1; t<=t2; t++) {
	if (!na(x[t]) && eval_ytest(y[t], yop, yval)) {
	    xx = x[t] - xbar;
	    sumsq += xx * xx;
	} else {
	    n--;
	}
    }

    sumsq = (n > 1)? sumsq / (n - 1) : 0.0;

    return (sumsq >= 0)? sumsq : NADBL;
}

/**
 * gretl_stddev:
 * @t1: starting observation.
 * @t2: ending observation.
 * @x: data series.
 *
 * Returns: the standard deviation of the series @x from obs
 * @t1 to obs @t2, skipping any missing values, or #NADBL 
 * on failure.
 */

double gretl_stddev (int t1, int t2, const double *x)
{
    double xx = gretl_variance(t1, t2, x);

    return (na(xx))? xx : sqrt(xx);
}

/**
 * gretl_restricted_stddev:
 * @t1: starting observation.
 * @t2: ending observation.
 * @x: data series.
 * @y: criterion series.
 * @yop: criterion operator.
 * @yval: criterion value.
 *
 * Returns: the standard deviation of the series @x from obs
 * @t1 to obs @t2, skipping any missing values and observations
 * where the series @y does not bear the relationship @yop to
 * the value @yval, or #NADBL on failure.
 */

double gretl_restricted_stddev (int t1, int t2, const double *x,
				const double *y, GretlOp yop, 
				double yval)
{
    double xx = gretl_restricted_variance(t1, t2, x, y, yop, yval);

    return (na(xx))? xx : sqrt(xx);
}

/**
* gretl_long_run_variance:
* @t1: starting observation.
* @t2: ending observation.
* @x: data series.
* @m: bandwidth.
*
* Returns: the long-run variance of the series @x from obs
* @t1 to obs @t2, using Bartlett kernel weights, or #NADBL 
* on failure (which includes encountering missing values). 
*/

double gretl_long_run_variance (int t1, int t2, const double *x, int m)
{
    double zt, wt, xbar, s2 = 0.0;
    double *autocov;
    int i, t, n, order;

    if (array_adjust_t1t2(x, &t1, &t2)) {
	return NADBL;
    }

    n = t2 - t1 + 1;

    if (n < 2) {
	return NADBL;
    }

    xbar = gretl_mean(t1, t2, x);

    if (m<0) {
	order = (int) exp(log(n) / 3.0);
    } else {
	order = m;
    }

    autocov = malloc(order * sizeof *autocov);
    if (autocov == NULL) {
	return NADBL;
    }
  
    for (i=0; i<order; i++) {
	autocov[i] = 0.0;
    }

    for (t=t1; t<=t2; t++) {
	zt = x[t] - xbar;
	s2 += zt * zt;
	for (i=1; i<=order; i++) {
	    if (t - i >= t1) {
		autocov[i-1] += zt * (x[t - i] - xbar);
	    }
	}
    }

    for (i=0; i<order; i++) {
	wt = 1.0 - ((double) (i + 1)) / (order + 1.0);
	s2 += 2.0 * wt * autocov[i];
    }

    s2 /= n;

    free(autocov);

    return s2;
}

/**
 * gretl_covar:
 * @t1: starting observation.
 * @t2: ending observation.
 * @x: data series.
 * @y: data series.
 * @missing: location to receive information on the number
 * of missing observations that were skipped, or %NULL.
 *
 * Returns: the covariance of the series @x and @y from obs
 * @t1 to obs @t2, skipping any missing values, or #NADBL 
 * on failure.
 */

double gretl_covar (int t1, int t2, const double *x, const double *y,
		    int *missing)
{
    double sx, sy, sxy, xbar, ybar;
    int t, nn, n = t2 - t1 + 1;

    if (n == 0) {
	/* void sample */
	return NADBL;
    }

    nn = 0;
    sx = sy = 0.0;

    for (t=t1; t<=t2; t++) {
        if (!na(x[t]) && !na(y[t])) {
	    sx += x[t];
	    sy += y[t];
	    nn++;
	}
    }

    if (nn < 2) {
	return NADBL;
    }

    xbar = sx / nn;
    ybar = sy / nn;
    sxy = 0.0;

    for (t=t1; t<=t2; t++) {
        if (!na(x[t]) && !na(y[t])) {
	    sx = x[t] - xbar;
	    sy = y[t] - ybar;
	    sxy = sxy + (sx * sy);
	}
    }

    if (missing != NULL) {
	/* record number of missing obs */
	*missing = n - nn;
    }

    return sxy / (nn - 1);
}

/**
 * gretl_corr:
 * @t1: starting observation.
 * @t2: ending observation.
 * @x: data series.
 * @y: data series.
 * @missing: location to receive information on the number
 * of missing observations that were skipped, or %NULL.
 *
 * Returns: the correlation coefficient for the series @x and @y 
 * from obs @t1 to obs @t2, skipping any missing values, or #NADBL 
 * on failure.
 */

double gretl_corr (int t1, int t2, const double *x, const double *y,
		   int *missing)
{
    int t, nn, n = t2 - t1 + 1;
    double sx, sy, sxx, syy, sxy, den, xbar, ybar;
    double cval = 0.0;

    if (n == 0) {
	/* void sample */
	return NADBL;
    }

    if (gretl_isconst(t1, t2, x) || gretl_isconst(t1, t2, y)) {
	/* correlation between any x and a constant is
	   undefined */
	return NADBL;
    }

    nn = 0;
    sx = sy = 0.0;

    for (t=t1; t<=t2; t++) {
        if (!na(x[t]) && !na(y[t])) {
  	    sx += x[t];
	    sy += y[t];
	    nn++;
	}
    }

    if (nn < 2) {
	/* zero or 1 observations: no go! */
	return NADBL;
    }

    xbar = sx / nn;
    ybar = sy / nn;
    sxx = syy = sxy = 0.0;

    for (t=t1; t<=t2; t++) {
        if (!na(x[t]) && !na(y[t])) {
	    sx = x[t] - xbar;
	    sy = y[t] - ybar;
	    sxx += sx * sx;
	    syy += sy * sy;
	    sxy += sx * sy;
	}
    }

    if (sxy != 0.0) {
        den = sxx * syy;
        if (den > 0.0) {
	    cval = sxy / sqrt(den);
        } else {
	    cval = NADBL;
	}
    }

    if (missing != NULL) {
	/* record number of missing observations */
	*missing = n - nn;
    }

    return cval;
}

/**
 * gretl_corr_rsq:
 * @t1: starting observation.
 * @t2: ending observation.
 * @x: data series.
 * @y: data series.
 *
 * Returns: the square of the correlation coefficient for the series 
 * @x and @y from obs @t1 to obs @t2, skipping any missing values, 
 * or #NADBL on failure.  Used as alternative value for R^2 in a
 * regression without an intercept.
 */

double gretl_corr_rsq (int t1, int t2, const double *x, const double *y)
{
    double r = gretl_corr(t1, t2, x, y, NULL);

    return (na(r))? r : r * r;
}

/* we're supposing a variance smaller than this is just noise */
#define TINYVAR 1.0e-36

/**
 * gretl_moments:
 * @t1: starting observation.
 * @t2: ending observation.
 * @x: data series.
 * @xbar: pointer to receive mean.
 * @sd: pointer to receive standard deviation.
 * @skew: pointer to receive skewness.
 * @kurt: pointer to receive excess kurtosis.
 * @k: degrees of freedom loss (generally 1).
 *
 * Calculates sample moments for series @x from obs @t1 to obs
 * @t2.  
 *
 * Returns: 0 on success, 1 on error.
 */

int gretl_moments (int t1, int t2, const double *x, 
		   double *xbar, double *std, 
		   double *skew, double *kurt, int k)
{
    int t, n;
    double dev, var;
    double s, s2, s3, s4;
    int allstats = 1;

    if (skew == NULL && kurt == NULL) {
	allstats = 0;
    }

    while (na(x[t1]) && t1 <= t2) {
	/* skip missing values at start of sample */
	t1++;
    }

    if (gretl_isconst(t1, t2, x)) {
	/* no variation in x */
	*xbar = x[t1];
	*std = 0.0;
	if (allstats) {
	    *skew = *kurt = NADBL;
	}
	return 1;
    }

    /* get number and sum of valid observations */
    s = 0.0;
    n = 0;
    for (t=t1; t<=t2; t++) {
	if (!na(x[t])) {
	    s += x[t];
	    n++;
	}
    }

    if (n == 0) {
	/* no valid data */
	*xbar = *std = NADBL;
	if (allstats) {
	    *skew = *kurt = 0.0;
	}	
	return 1;
    }

    /* calculate mean and initialize other stats */
    *xbar = s / n;
    var = 0.0;
    if (allstats) {
	*skew = *kurt = 0.0;
    }

    /* compute quantities needed for higher moments */
    s2 = s3 = s4 = 0.0;
    for (t=t1; t<=t2; t++) {
	if (!na(x[t])) {
	    dev = x[t] - *xbar;
	    s2 += dev * dev;
	    if (allstats) {
		s3 += pow(dev, 3);
		s4 += pow(dev, 4);
	    }
	}
    }

    /* variance */
    var = s2 / (n - k);

    if (var < 0.0) {
	/* in principle impossible, but this is a computer */
	*std = NADBL;
	if (allstats) {
	    *skew = *kurt = NADBL;
	}
	return 1;
    }

    if (var > TINYVAR) {
	*std = sqrt(var);
    } else {
	/* screen out minuscule variance */
	*std = 0.0;
    }

    if (allstats) {
	if (var > TINYVAR) {
	    *skew = (s3 / n) / pow(s2 / n, 1.5);
	    *kurt = ((s4 / n) / pow(s2 / n, 2)) - 3.0; /* excess kurtosis */
	} else {
	    /* if variance is effectively zero, these should be undef'd */
	    *skew = *kurt = NADBL;
	}
    }

    return 0;
}

/**
 * free_freq:
 * @freq: gretl frequency distribution struct
 *
 * Frees all malloced elements of the struct.
 *
 */

void free_freq (FreqDist *freq)
{
    if (freq == NULL) {
	return;
    }

    free(freq->midpt);
    free(freq->endpt);
    free(freq->f);

    free(freq);
}

static FreqDist *freq_new (void)
{
    FreqDist *freq;

    freq = malloc(sizeof *freq);
    if (freq == NULL) return NULL;

    freq->midpt = NULL;
    freq->endpt = NULL;
    freq->f = NULL;

    freq->dist = 0;
    freq->discrete = 0;

    freq->xbar = NADBL;
    freq->sdx = NADBL;
    freq->test = NADBL;

    return freq;
}

/**
 * dh_root_b1_to_z1:
 * @rb1: square root b1, skewness.
 * @n: number of observations.
 *
 * Performs the transformation from skewness, root b1, to the 
 * normal score, z1, as set out in Doornik and Hansen, "An Omnibus 
 * Test for Normality", 1994.  The transformation is originally 
 * due to D'Agostino (Biometrika, 1970).
 *
 * Returns: the z1 value.
 */

double dh_root_b1_to_z1 (double rb1, double n)
{
    double b, w2, d, y, z1;

    b = 3.0 * (n*n + 27*n - 70) * (n+1) * (n+3) /
	((n-2) * (n+5) * (n+7) * (n+9));

    w2 = -1.0 + sqrt(2 * (b-1));
    d = 1.0 / sqrt(log(sqrt(w2)));
    y = rb1 * sqrt(((w2-1.0)/2.0) * ((n+1.0)*(n+3.0))/(6.0*(n-2)));
    z1 = d * log(y + sqrt(y*y + 1));

    return z1;
}

/**
 * dh_b2_to_z2:
 * @b1: skewness.
 * @b2: kurtosis.
 * @n: number of observations.
 *
 * Performs the transformation from kurtosis, b2, to the 
 * normal score, z2, as set out in Doornik and Hansen, "An Omnibus 
 * Test for Normality", 1994.
 *
 * Returns: the z2 value, or #NADBL on failure.
 */

double dh_b2_to_z2 (double b1, double b2, double n)
{
    double d, a, c, k, alpha, chi, z2;
    double n2 = n * n;

    d = (n-3) * (n+1) * (n2 + 15*n - 4.0);
    a = ((n-2) * (n+5) * (n+7) * (n2 + 27*n - 70.0)) / (6.0 * d);
    c = ((n-7) * (n+5) * (n+7) * (n2 + 2*n - 5.0)) / (6.0 * d);
    k = ((n+5) * (n+7) * (n2*n + 37*n2 + 11*n - 313.0)) / (12.0 * d);

    alpha = a + b1 * c;
    z2 =  (1.0 / (9.0 * alpha)) - 1.0;
    chi = (b2 - 1.0 - b1) * 2.0 * k;

    if (chi > 0.0) {
       z2 += pow(chi/(2*alpha), 1.0 / 3.0);
    }

    z2 *= sqrt(9.0*alpha);

    return z2;
}

/**
 * doornik_chisq:
 * @skew: skewness.
 * @xkurt: excess kurtosis.
 * @n: number of observations.
 *
 * Calculates the Chi-square test for normality as set out by
 * Doornik and Hansen, "An Omnibus Test for Normality", 1994.
 * This is a modified version of the test proposed by Bowman and
 * Shenton (Biometrika, 1975).
 *
 * Returns: the Chi-square value, which has 2 degrees of freedom.
 */

double doornik_chisq (double skew, double xkurt, int n)
{
    double rb1, b1, b2, z1, z2;

    rb1 = skew;
    b1 = skew * skew;
    b2 = xkurt + 3.0; /* Note: convert from "excess" to regular */

    z1 = dh_root_b1_to_z1(rb1, (double) n);
    z2 = dh_b2_to_z2(b1, b2, (double) n);

    if (!na(z2)) {
	z2 = z1*z1 + z2*z2;
    }

    return z2;
}

static int
series_get_moments (int t1, int t2, const double *x, 
		    double *skew, double *xkurt,
		    int *pn)
{
    double xbar, dev, var;
    double s = 0.0;
    double s2 = 0.0;
    double s3 = 0.0;
    double s4 = 0.0;
    int i, n = 0;
    int err = 0;

    for (i=t1; i<=t2; i++) {
	if (!na(x[i])) {
	    s += x[i];
	    n++;
	}
    }

    *pn = n;
    if (n < 3) {
	return E_DATA;
    }
    
    xbar = s / n;

    for (i=t1; i<=t2; i++) {
	if (!na(x[i])) {
	    dev = x[i] - xbar;
	    s2 += dev * dev;
	    s3 += pow(dev, 3);
	    s4 += pow(dev, 4);
	}
    }

    var = s2 / n;

    if (var > 0.0) {
	*skew = (s3 / n) / pow(s2 / n, 1.5);
	*xkurt = ((s4 / n) / pow(s2 / n, 2)) - 3.0;
    } else {
	/* if variance is effectively zero, these should be undef'd */
	*skew = *xkurt = NADBL;
	err = 1;
    }

    return err;
}

static int
get_moments (const gretl_matrix *M, int row, double *skew, double *kurt)
{
    int j, n = gretl_matrix_cols(M);
    double xi, xbar, dev, var;
    double s = 0.0;
    double s2 = 0.0;
    double s3 = 0.0;
    double s4 = 0.0;
    int err = 0;
    
    for (j=0; j<n; j++) {
	s += gretl_matrix_get(M, row, j);
    }

    xbar = s / n;

    for (j=0; j<n; j++) {
	xi = gretl_matrix_get(M, row, j);
	dev = xi - xbar;
	s2 += dev * dev;
	s3 += pow(dev, 3);
	s4 += pow(dev, 4);
    }

    var = s2 / n;

    if (var > 0.0) {
	*skew = (s3 / n) / pow(s2 / n, 1.5);
	*kurt = ((s4 / n) / pow(s2 / n, 2));
    } else {
	/* if variance is effectively zero, these should be undef'd */
	*skew = *kurt = NADBL;
	err = 1;
    }

    return err;
}

int 
multivariate_normality_test (const gretl_matrix *E, 
			     const gretl_matrix *Sigma, 
			     PRN *prn)
{
    gretl_matrix *S = NULL;
    gretl_matrix *V = NULL;
    gretl_matrix *C = NULL;
    gretl_matrix *X = NULL;
    gretl_matrix *R = NULL;
    gretl_matrix *evals = NULL;
    gretl_matrix *tmp = NULL;

    /* convenience pointers: do not free! */
    gretl_matrix *H;
    gretl_vector *Z1;
    gretl_vector *Z2;

    double x, skew, kurt;
    double X2 = NADBL;
    int n, p;
    int i, j;
    int err = 0;

    if (E == NULL || Sigma == NULL) {
	err = 1;
	goto bailout;
    }

    p = gretl_matrix_cols(E);
    n = gretl_matrix_rows(E);

    clear_gretl_matrix_err();

    S = gretl_matrix_copy(Sigma);
    V = gretl_vector_alloc(p);
    C = gretl_matrix_alloc(p, p);
    X = gretl_matrix_copy_transpose(E);
    R = gretl_matrix_alloc(p, n);
    tmp = gretl_matrix_alloc(p, p);

    err = get_gretl_matrix_err();
    if (err) {
	goto bailout;
    }

    for (i=0; i<p; i++) {
	x = gretl_matrix_get(S, i, i);
	gretl_vector_set(V, i, 1.0 / sqrt(x));
    }

    err = gretl_matrix_diagonal_sandwich(V, S, C);
    if (err) {
	goto bailout;
    }

    gretl_matrix_print_to_prn(C, "\nResidual correlation matrix, C", prn);

    evals = gretl_symmetric_matrix_eigenvals(C, 1, &err);
    if (err) {
	goto bailout;
    }

    pputs(prn, "Eigenvalues of the correlation matrix:\n\n");
    for (i=0; i<p; i++) {
	pprintf(prn, " %10g\n", evals->val[i]);
    }
    pputc(prn, '\n');

    /* C should now contain eigenvectors of the original C:
       relabel as 'H' for perspicuity */
    H = C;
#if 0
    gretl_matrix_print_to_prn(H, "Eigenvectors, H", prn);
#endif
    gretl_matrix_copy_values(tmp, H);

    /* make "tmp" into $H \Lambda^{-1/2}$ */
    for (i=0; i<p; i++) {
	for (j=0; j<p; j++) {
	    x = gretl_matrix_get(tmp, i, j);
	    x *= 1.0 / sqrt(evals->val[j]);
	    gretl_matrix_set(tmp, i, j, x);
	}
    }

    /* make S into $H \Lambda^{-1/2} H'$ */
    gretl_matrix_multiply_mod(tmp, GRETL_MOD_NONE,
			      H, GRETL_MOD_TRANSPOSE,
			      S, GRETL_MOD_NONE);

#if 1
    gretl_matrix_demean_by_row(X);
#endif

    /* compute VX', in X (don't need to subtract means, because these
       are OLS residuals)
    */
    for (i=0; i<p; i++) {
	for (j=0; j<n; j++) {
	    x = gretl_matrix_get(X, i, j);
	    x *= gretl_vector_get(V, i);
	    gretl_matrix_set(X, i, j, x);
	}
    }

    /* finally, compute $R' = H  \Lambda^{-1/2} H' V X'$ 
       Doornik and Hansen, 1994, section 3 */
    gretl_matrix_multiply(S, X, R);

    /* Z_1 and Z_2 are p-vectors: use existing storage */
    Z1 = V;
    Z2 = gretl_matrix_reuse(tmp, p, 1);

    for (i=0; i<p && !err; i++) {
	get_moments(R, i, &skew, &kurt);
	if (na(skew) || na(kurt)) {
	    err = 1;
	} else {
	    double z1i = dh_root_b1_to_z1(skew, n);
	    double z2i = dh_b2_to_z2(skew * skew, kurt, n);

	    if (na(z2i)) {
		X2 = NADBL;
		err = E_NAN;
	    } else {
		gretl_vector_set(Z1, i, z1i);
		gretl_vector_set(Z2, i, z2i);
	    }
	}
    }
	
    if (!err) {
	X2 = gretl_vector_dot_product(Z1, Z1, &err);
	X2 += gretl_vector_dot_product(Z2, Z2, &err);
    }

    if (na(X2)) {
	pputs(prn, "Calculation of test statistic failed\n");
    } else {
	pputs(prn, "Test for multivariate normality of residuals\n");
	pprintf(prn, "Doornik-Hansen Chi-square(%d) = %g, ", 2 * p, X2);
	pprintf(prn, "with p-value = %g\n", chisq_cdf_comp(2 * p, X2));
    }

 bailout:

    gretl_matrix_free(S);
    gretl_matrix_free(V);
    gretl_matrix_free(C);
    gretl_matrix_free(X);
    gretl_matrix_free(R);
    gretl_matrix_free(evals);
    gretl_matrix_free(tmp);

    return err;
}

static int freq_add_arrays (FreqDist *freq, int n)
{
    int err = 0;

    if (!freq->discrete) {
	freq->endpt = malloc((n + 1) * sizeof *freq->endpt);
    }
	
    freq->midpt = malloc(n * sizeof *freq->midpt);
    freq->f = malloc(n * sizeof *freq->f);
	
    if ((!freq->discrete && freq->endpt == NULL) || 
	freq->midpt == NULL || freq->f == NULL) {
	err = E_ALLOC;
    } else {
	freq->numbins = n;
    }

    return err;
}

int freq_setup (int v, const double **Z, const DATAINFO *pdinfo,
		int *pn, double *pxmax, double *pxmin, int *nbins, 
		double *binwidth)
{
    const double *x = Z[v];
    double xrange, xmin = 0, xmax = 0;
    int t, k = 0, n = 0;

    for (t=pdinfo->t1; t<=pdinfo->t2; t++) {
	if (!na(x[t])) {
	    if (n == 0) {
		xmax = xmin = x[t];
	    } else {
		if (x[t] > xmax) xmax = x[t];
		if (x[t] < xmin) xmin = x[t];
	    }
	    n++;
	}
    }

    if (n < 8) {
	gretl_errmsg_sprintf(_("Insufficient data to build frequency "
			       "distribution for variable %s"), 
			     pdinfo->varname[v]);
	return E_TOOFEW;
    }

    xrange = xmax - xmin;
    if (xrange == 0) {
	gretl_errmsg_sprintf(_("%s is a constant"), pdinfo->varname[v]);
	return E_DATA;
    }

    if (nbins != NULL && *nbins > 0) {
	k = *nbins;
	if (k % 2 == 0) {
	    k++;
	}
    } else if (n < 16) {
	k = 5; 
    } else if (n < 50) {
	k = 7;
    } else if (n > 850) {
	k = 29;
    } else {
	k = (int) sqrt((double) n);
	if (k % 2 == 0) {
	    k++;
	}
    }

    if (pn != NULL) {
	*pn = n;
    }
    if (pxmax != NULL) {
	*pxmax = xmax;
    }
    if (pxmin != NULL) {
	*pxmin = xmin;
    }
    if (nbins != NULL) {
	*nbins = k;
    }
    if (binwidth != NULL) {
	*binwidth = xrange / (k - 1);
    }

    return 0;
}

/* calculate test stat for distribution, if the sample 
   is big enough */

static void
freq_dist_stat (FreqDist *freq, const double *x, gretlopt opt, int k)
{
    double skew, kurt;

    freq->test = NADBL;
    freq->dist = 0;

    gretl_moments(freq->t1, freq->t2, x, 
		  &freq->xbar, &freq->sdx, 
		  &skew, &kurt, k);

    if (freq->n > 7) {
	if (opt & OPT_O) {
	    freq->test = lockes_test(x, freq->t1, freq->t2);
	    freq->dist = D_GAMMA;
	} else if (opt & OPT_Z) {
	    freq->test = doornik_chisq(skew, kurt, freq->n); 
	    freq->dist = D_NORMAL;
	}
    } 
}

FreqDist *
get_discrete_freq (int v, const double **Z, const DATAINFO *pdinfo, 
		   gretlopt opt, int *err)
{
    FreqDist *freq;
    const double *x = Z[v];
    int *ifreq = NULL;
    double *ivals = NULL;
    double *sorted = NULL;
    double last;
    int i, t, nv;

    freq = freq_new();
    if (freq == NULL) {
	*err = E_ALLOC;
	return NULL;
    }

    freq->t1 = pdinfo->t1; 
    freq->t2 = pdinfo->t2;

    freq->n = 0;
    for (t=freq->t1; t<=freq->t2; t++) {
	if (!na(x[t])) {
	    freq->n += 1;
	}
    }

    if (freq->n < 3) {
	gretl_errmsg_sprintf(_("Insufficient data to build frequency "
			       "distribution for variable %s"), 
			     pdinfo->varname[v]);
	*err = E_TOOFEW;
	goto bailout;
    }

    strcpy(freq->varname, pdinfo->varname[v]);
    freq->discrete = 1;
    freq->test = NADBL;
    freq->dist = 0;
    
    sorted = malloc(freq->n * sizeof *sorted);
    if (sorted == NULL) {
	*err = E_ALLOC;
	goto bailout;
    }

    i = 0;
    for (t=freq->t1; t<=freq->t2; t++) {
	if (!na(x[t])) {
	    sorted[i++] = x[t];
	}
    }
	
    qsort(sorted, freq->n, sizeof *sorted, gretl_compare_doubles); 
    nv = count_distinct_values(sorted, freq->n);

    if (nv >= 10 && !(opt & OPT_X)) {
	freq_dist_stat(freq, x, opt, 1);
    } else if (opt & (OPT_Z | OPT_O)) {
	freq->xbar = gretl_mean(freq->t1, freq->t2, x);
	freq->sdx = gretl_stddev(freq->t1, freq->t2, x);
    }

    ifreq = malloc(nv * sizeof *ifreq);
    ivals = malloc(nv * sizeof *ivals);
    if (ifreq == NULL || ivals == NULL) {
	*err = E_ALLOC;
	goto bailout;
    }

    ivals[0] = last = sorted[0];
    ifreq[0] = i = 1;

    for (t=1; t<freq->n; t++) {
	if (sorted[t] != last) {
	    last = sorted[t];
	    ifreq[i] = 1;
	    ivals[i++] = last;
	} else {
	    ifreq[i-1] += 1;
	}
    }

    if (freq_add_arrays(freq, nv)) {
	*err = E_ALLOC;
    } else {
	for (i=0; i<nv; i++) {
	    freq->midpt[i] = ivals[i];
	    freq->f[i] = ifreq[i];
	}
    }

 bailout:

    free(sorted);
    free(ivals);
    free(ifreq);
	
    if (*err && freq != NULL) {
	free_freq(freq);
	freq = NULL;
    }
	
    return freq;
}

/**
 * get_freq:
 * @varno: ID number of variable to process.
 * @Z: data array.
 * @pdinfo: information on the data set.
 * @fmin: lower limit of left-most bin (or #NADBL for automatic).
 * @fwid: bin width (or #NADBL for automatic).
 * @nbins: number of bins to use (or 0 for automatic).
 * @params: degrees of freedom loss (generally = 1 unless we're dealing
 * with the residual from a regression).
 * @opt: if includes %OPT_Z, set up for comparison with normal dist; 
 * if includes %OPT_O, compare with gamma distribution;
 * if includes %OPT_Q, do not show a graph; if includes %OPT_D,
 * treat the variable as discrete; %OPT_X indicates that this function
 * is called as part of a cross-tabulation.
 * @err: location to receive error code.
 *
 * Calculates the frequency distribution for the specified variable.
 *
 * Returns: pointer to struct containing the distribution.
 */

FreqDist *get_freq (int varno, const double **Z, const DATAINFO *pdinfo, 
		    double fmin, double fwid, int nbins, int params, 
		    gretlopt opt, int *err)
{
    FreqDist *freq;
    const double *x;
    double xx, xmin, xmax;
    double binwidth = fwid;
    int t, k, n;

    if (var_is_discrete(pdinfo, varno) || (opt & OPT_D)) {
	return get_discrete_freq(varno, Z, pdinfo, opt, err);
    }

    if (gretl_isdiscrete(pdinfo->t1, pdinfo->t2, Z[varno])) {
	return get_discrete_freq(varno, Z, pdinfo, opt, err);
    }

    freq = freq_new();
    if (freq == NULL) {
	*err = E_ALLOC;
	return NULL;
    }

    *err = freq_setup(varno, Z, pdinfo, &n, &xmax, &xmin, &nbins, &binwidth);

    if (*err) {
	goto bailout;
    }

    if (!na(fmin) && !na(fwid)) {
	/* endogenous implied number of bins */
	nbins = (int) ceil((xmax - fmin) / fwid);
	binwidth = fwid;
    }

    freq->t1 = pdinfo->t1; 
    freq->t2 = pdinfo->t2;
    freq->n = n;

    strcpy(freq->varname, pdinfo->varname[varno]);

    x = Z[varno];
    freq_dist_stat(freq, x, opt, params);

    if (opt & OPT_S) {
	/* silent operation */
	return freq;
    }

    if (freq_add_arrays(freq, nbins)) {
	*err = E_ALLOC;
	goto bailout;
    }

    if (na(fmin) || na(fwid)) {
	freq->endpt[0] = xmin - .5 * binwidth;
	
	if (xmin > 0.0 && freq->endpt[0] < 0.0) {
	    double rshift;
	    
	    freq->endpt[0] = 0.0;
	    rshift = 1.0 - xmin / binwidth;
	    freq->endpt[freq->numbins] = xmax + rshift * binwidth;
	} else {
	    freq->endpt[freq->numbins] = xmax + .5 * binwidth;
	}
    } else {
	freq->endpt[0] = fmin;
	freq->endpt[freq->numbins] = fmin + nbins * fwid;
    }
	
    for (k=0; k<freq->numbins; k++) {
	freq->f[k] = 0;
	if (k > 0) {
	    freq->endpt[k] = freq->endpt[k-1] + binwidth;
	}
	freq->midpt[k] = freq->endpt[k] + .5 * binwidth;
    }
	
    for (t=pdinfo->t1; t<=pdinfo->t2; t++) {
	xx = x[t];
	if (na(xx)) {
	    continue;
	}
	if (xx < freq->endpt[1]) {
	    freq->f[0] += 1;
	} else if (xx >= freq->endpt[freq->numbins]) {
	    freq->f[freq->numbins - 1] += 1;
	    continue;
	} else {
	    for (k=1; k<freq->numbins; k++) {
		if (freq->endpt[k] <= xx && xx < freq->endpt[k+1]) {
		    freq->f[k] += 1;
		    break;
		}
	    }
	}
    }

 bailout:

    if (*err && freq != NULL) {
	free_freq(freq);
	freq = NULL;
    }
	
    return freq;
}

static void record_freq_test (const FreqDist *freq)
{
    double pval = NADBL;

    if (freq->dist == D_NORMAL) {
	pval = chisq_cdf_comp(2, freq->test);
    } else if (freq->dist == D_GAMMA) {
	pval = normal_pvalue_2(freq->test);
    }	

    if (!na(pval)) {
	record_test_result(freq->test, pval, 
			   (freq->dist == D_NORMAL)? 
			   "normality" : "gamma");
    }
}

/* wrapper function: get the distribution, print it, graph it
   if wanted, then free stuff.  OPT_Q = quiet; OPT_S =
   silent.
*/

int freqdist (int varno, const double **Z, const DATAINFO *pdinfo,
	      int graph, gretlopt opt, PRN *prn)
{
    FreqDist *freq;
    int realgraph = graph && !(opt & (OPT_Q|OPT_S));
    int err = 0;
    DistCode dist = D_NONE;

    if (opt & OPT_O) {
	dist = D_GAMMA; 
    } else if (opt & OPT_Z) {
	dist = D_NORMAL;
    }

    freq = get_freq(varno, Z, pdinfo, NADBL, NADBL, 0, 1, opt, &err); 

    if (err) {
	return err;
    }

    if (!(opt & OPT_S)) {
	print_freq(freq, prn);
    } else if (dist) {
	record_freq_test(freq);
    }

    if (realgraph && plot_freq(freq, dist)) {
	pputs(prn, _("gnuplot command failed\n"));
    }

    free_freq(freq);

    return 0;
}

gretl_matrix *xtab_to_matrix (const Xtab *tab)
{
    gretl_matrix *m;
    double x;
    int i, j;

    if (tab == NULL) {
	return NULL;
    }

    m = gretl_matrix_alloc(tab->rows, tab->cols);
    if (m == NULL) {
	return NULL;
    }

    for (j=0; j<tab->cols; j++) {
	for (i=0; i<tab->rows; i++) {
	    x = (double) tab->f[i][j];
	    gretl_matrix_set(m, i, j, x);
	}
    }

    return m;
}

/**
 * free_xtab:
 * @tab: gretl crosstab struct.
 *
 * Frees all malloced elements of the struct, and then
 * the pointer itself.
 */

void free_xtab (Xtab *tab)
{
    int i;

    if (tab == NULL) {
	return;
    }

    free(tab->rtotal);
    free(tab->ctotal);
    free(tab->rval);
    free(tab->cval);

    if (tab->f != NULL) {
	for (i=0; i<tab->rows; i++) {
	    free(tab->f[i]);
	}
	free(tab->f);
    }

    free(tab);
}

static Xtab *xtab_new (int n, int t1, int t2)
{
    Xtab *tab = malloc(sizeof *tab);

    if (tab == NULL) return NULL;

    tab->rtotal = NULL;
    tab->ctotal = NULL;
    tab->rval = NULL;
    tab->cval = NULL;
    tab->f = NULL;

    tab->n = n;
    tab->t1 = t1;
    tab->t2 = t2;
    tab->missing = 0;

    *tab->rvarname = '\0';
    *tab->cvarname = '\0';

    return tab;
}

static int xtab_allocate_arrays (Xtab *tab, int rows, int cols)
{
    int i, j;

    tab->rows = rows;
    tab->cols = cols;

    tab->rval = malloc(rows * sizeof *tab->rval);
    tab->rtotal = malloc(rows * sizeof *tab->rtotal);

    tab->cval = malloc(cols * sizeof *tab->cval);
    tab->ctotal = malloc(cols * sizeof *tab->ctotal);

    tab->f = malloc(rows * sizeof *tab->f);

    if (tab->rval == NULL || tab->rtotal == NULL ||
	tab->cval == NULL || tab->ctotal == NULL ||
	tab->f == NULL) {
	return E_ALLOC;
    }

    for (i=0; i<rows; i++) {
	tab->f[i] = NULL;
    }

    for (i=0; i<rows; i++) {
	tab->f[i] = malloc(cols * sizeof *tab->f[i]);
	if (tab->f[i] == NULL) {
	    return E_ALLOC;
	} else {
	    for (j=0; j<cols; j++) {
		tab->f[i][j] = 0;
	    }
	}
    }

    return 0;
}

/* also used in gretl_matrix.c */

int compare_xtab_rows (const void *a, const void *b) 
{
    const double **da = (const double **) a;
    const double **db = (const double **) b;
    int ret;
    
    ret = da[0][0] - db[0][0];

    if (ret == 0) {
	ret = da[0][1] - db[0][1];
    }
    
    return ret;
}

#define complete_obs(x,y,t) (!na(x[t]) && !na(y[t]))

/* crosstab struct creation function */

static Xtab *get_xtab (int rvarno, int cvarno, const double **Z, 
		       const DATAINFO *pdinfo, int *err)
{
    Xtab *tab = NULL;
    double **X = NULL;
    int ri = 0, cj = 0;
    int rows, cols;

    FreqDist *rowfreq = NULL, *colfreq = NULL;

    double xr = 0.0, xc = 0.0;
    int t1 = pdinfo->t1;
    int t2 = pdinfo->t2;
    int i, t, n = 0;

    /* count non-missing values */
    for (t=t1; t<=t2; t++) {
	if (complete_obs(Z[rvarno], Z[cvarno], t)) {
	    n++;
	}
    }

    if (n == 0) {
	fprintf(stderr, "All values invalid!\n");
	*err = E_MISSDATA;
	return NULL;
    }

    /* Put in some info we already know */

    tab = xtab_new(n, t1, t2);
    if (tab == NULL) {
	*err = E_ALLOC;
	return NULL;
    }

    tab->missing = (t2 - t1 + 1) - n;
    strcpy(tab->rvarname, pdinfo->varname[rvarno]);
    strcpy(tab->cvarname, pdinfo->varname[cvarno]);

    /* 
       start allocating stuff; we use temporary FreqDists for rows 
       and columns to retrieve values with non-zero frequencies
       and get dimensions for the cross table
     */

    X = doubles_array_new(n, 2);
    if (X == NULL) {
	*err = E_ALLOC;
	goto bailout;
    }

    rowfreq = get_freq(rvarno, Z, pdinfo, NADBL, NADBL, 0, 
		       0, OPT_D | OPT_X, err); 
    if (!*err) {
	colfreq = get_freq(cvarno, Z, pdinfo, NADBL, NADBL, 0, 
			   0, OPT_D | OPT_X, err); 
    }

    if (*err) {
	goto bailout;
    }

    rows = rowfreq->numbins;
    cols = colfreq->numbins;

    if (xtab_allocate_arrays(tab, rows, cols)) {
	*err = E_ALLOC;
	goto bailout;
    }

    for (i=0; i<rows; i++) {
	tab->rval[i] = rowfreq->midpt[i];
	tab->rtotal[i] = 0;
    }

    for (i=0; i<cols; i++) {
	tab->cval[i] = colfreq->midpt[i];
	tab->ctotal[i] = 0;
    }

    /* matrix X holds the values to be sorted */

    i = 0;
    for (t=t1; t<=t2 && i<n; t++) {
	if (complete_obs(Z[rvarno], Z[cvarno], t)) {
	    X[i][0] = Z[rvarno][t];
	    X[i][1] = Z[cvarno][t];
	    i++;
	}
    }

    qsort(X, n, sizeof *X, compare_xtab_rows);
    ri = cj = 0;
    xr = tab->rval[0];
    xc = tab->cval[0];

    /* compute frequencies by going through sorted X */

    for (i=0; i<n; i++) {
	while (X[i][0] > xr) { 
	    /* skip row */
	    xr = tab->rval[++ri];
	    cj = 0;
	    xc = tab->cval[0];
	}
	while (X[i][1] > xc) { 
	    /* skip column */
	    xc = tab->cval[++cj];
	}
#if XTAB_DEBUG
	fprintf(stderr,"%d: (%d,%d) [%g,%g] %g,%g\n",
		i, ri, cj, xr, xc, X[i][0], X[i][1]);
#endif
	tab->f[ri][cj] += 1;
	tab->rtotal[ri] += 1;
	tab->ctotal[cj] += 1;
    }

 bailout:
    
    doubles_array_free(X, n);
    free_freq(rowfreq);
    free_freq(colfreq);

    if (*err) {
	free_xtab(tab);	
	tab = NULL;
    }

    return tab;
}

int crosstab_from_matrix (gretlopt opt, PRN *prn)
{
    const char *mname;
    const gretl_matrix *m;
    Xtab *tab;
    int i, j, nvals, n = 0;
    double x;
    int err = 0;

    mname = get_optval_string(XTAB, OPT_M);
    if (mname == NULL) {
	return E_DATA;
    }    

    m = get_matrix_by_name(mname);
    if (m == NULL) {
	return E_UNKVAR;
    }

    if (m->rows < 2 || m->cols < 2) {
	err = E_DATA;
    }

    nvals = m->rows * m->cols;

    for (i=0; i<nvals && !err; i++) {
	x = m->val[i];
	if (x < 0 || x != floor(x) || x > INT_MAX) {
	    err = E_DATA;
	}
	n += x;
    }

    if (err) {
	gretl_errmsg_sprintf(_("Matrix %s does not represent a "
			       "contingency table"), mname);
	return err;
    }

    tab = xtab_new(n, 0, 0);
    if (tab == NULL) {
	return E_ALLOC;
    }

    if (xtab_allocate_arrays(tab, m->rows, m->cols)) {
	free_xtab(tab);
	return E_ALLOC;
    }

    for (i=0; i<m->rows; i++) {
	tab->rval[i] = i + 1;
	tab->rtotal[i] = 0.0;
	for (j=0; j<m->cols; j++) {
	    tab->f[i][j] = gretl_matrix_get(m, i, j);
	    tab->rtotal[i] += tab->f[i][j];
	}
    }

    for (j=0; j<m->cols; j++) {
	tab->cval[j] = j + 1;
	tab->ctotal[j] = 0.0;
	for (i=0; i<m->rows; i++) {
	    tab->ctotal[j] += tab->f[i][j];
	}
    }  

    print_xtab(tab, opt, prn);
    free_xtab(tab);	

    return err;    
}

int crosstab (const int *list, const double **Z, 
	      const DATAINFO *pdinfo, gretlopt opt, 
	      PRN *prn)
{
    Xtab *tab;
    int *rowvar = NULL;
    int *colvar = NULL;
    int i, j, k;

    int pos = gretl_list_separator_position(list);
    int err = 0;
    int blanket = 0;
    int nrv, ncv;

    if (pos == 0) {
	nrv = list[0];
	ncv = nrv - 1;
	blanket = 1;
    } else {
	nrv = pos - 1;
	ncv = list[0] - pos;
    } 

    if (nrv == 0 || ncv == 0) {
	return E_PARSE;
    }

    rowvar = gretl_list_new(nrv);
    if (rowvar == NULL) {
	return E_ALLOC;
    }

    j = 1;
    for (i=1; i<=nrv; i++) {
	k = list[i];
	if (var_is_discrete(pdinfo, k) ||
	    gretl_isdiscrete(pdinfo->t1, pdinfo->t2, Z[k])) {
	    rowvar[j++] = k;
	} else {
	    rowvar[0] -= 1;
	}
    }

    if (rowvar[0] == 0 || (blanket && rowvar[0] == 1)) {
	gretl_errmsg_set("xtab: variables must be discrete");
	free(rowvar);
	return E_DATATYPE;
    }
    
    if (!blanket) {
	colvar = gretl_list_new(ncv);
	if (colvar == NULL) {
	    err = E_ALLOC;
	} else {
	    j = 1;
	    for (i=1; i<=ncv; i++) {
		k = pos + i;
		if (var_is_discrete(pdinfo, list[k]) ||
		    gretl_isdiscrete(pdinfo->t1, pdinfo->t2, Z[list[k]])) {
		    colvar[j++] = list[k];
		} else {
		    colvar[0] -= 1;
		}
	    }
	    if (colvar[0] == 0) {
		err = E_DATATYPE;
	    }
	}
    }

    for (i=1; i<=rowvar[0] && !err; i++) {
	if (blanket) {
	    for (j=1; j<i && !err; j++) {
		tab = get_xtab(rowvar[j], rowvar[i], Z, pdinfo, &err); 
		if (!err) {
		    print_xtab(tab, opt, prn); 
		    free_xtab(tab);
		}
	    }
	} else {
	    for (j=1; j<=colvar[0] && !err; j++) {
		tab = get_xtab(rowvar[i], colvar[j], Z, pdinfo, &err); 
		if (!err) {
		    print_xtab(tab, opt, prn); 
		    free_xtab(tab);
		}
	    }
	}
    }

    free(rowvar);
    free(colvar);

    return err;
}

Xtab *single_crosstab (const int *list, const double **Z, 
		       const DATAINFO *pdinfo, gretlopt opt, 
		       PRN *prn, int *err)
{
    Xtab *tab = NULL;
    int rv, cv;

    if (list[0] != 2) {
	*err = E_DATA;
	return NULL;
    }

    rv = list[1];
    cv = list[2];

    if (!var_is_discrete(pdinfo, rv) &&
	!gretl_isdiscrete(pdinfo->t1, pdinfo->t2, Z[rv])) {
	*err = E_DATATYPE;
	return NULL;
    }

    if (!var_is_discrete(pdinfo, cv) &&
	!gretl_isdiscrete(pdinfo->t1, pdinfo->t2, Z[cv])) {
	*err = E_DATATYPE;
	return NULL;
    }

    tab = get_xtab(rv, cv, Z, pdinfo, err);
    if (!*err) {
	print_xtab(tab, opt, prn); 
    }

    return tab;
}

int model_error_dist (const MODEL *pmod, double ***pZ,
		      DATAINFO *pdinfo, gretlopt opt,
		      PRN *prn)
{
    FreqDist *freq = NULL;
    gretlopt fopt = OPT_Z; /* show normality test */
    int save_t1 = pdinfo->t1;
    int save_t2 = pdinfo->t2;
    int err = 0;

    if (pmod == NULL || pmod->uhat == NULL) {
	return E_DATA;
    }

    if (exact_fit_check(pmod, prn)) {
	return 0;
    }

    if (genr_fit_resid(pmod, pZ, pdinfo, M_UHAT, 1)) {
	return E_ALLOC;
    }

    if (!err) {
	pdinfo->t1 = pmod->t1;
	pdinfo->t2 = pmod->t2;
	freq = get_freq(pdinfo->v - 1, (const double **) *pZ, pdinfo, 
			NADBL, NADBL, 0, pmod->ncoeff, fopt, &err);
    }

    if (!err) {
	if (opt & OPT_Q) {
	    print_freq_test(freq, prn);
	} else {
	    print_freq(freq, prn); 
	}
	free_freq(freq);
    }

    dataset_drop_last_variables(1, pZ, pdinfo);

    pdinfo->t1 = save_t1;
    pdinfo->t2 = save_t2;

    return err;
}

/* PACF via Durbin-Levinson algorithm */

static int get_pacf (double *pacf, const double *acf, int m)
{
    gretl_matrix *phi;
    double x, num, den;
    int i, j;

    phi = gretl_matrix_alloc(m, m);
    if (phi == NULL) {
	return E_ALLOC;
    }

    pacf[0] = acf[0];
    gretl_matrix_set(phi, 0, 0, acf[0]);

    for (i=1; i<m; i++) {
	num = acf[i];
	for (j=0; j<i; j++) {
	    num -= gretl_matrix_get(phi, i-1, j) * acf[i-j-1];
	}
	den = 1.0;
	for (j=0; j<i; j++) {
	    den -= gretl_matrix_get(phi, i-1, j) * acf[j];
	}
	pacf[i] = num / den;
	gretl_matrix_set(phi, i, i, pacf[i]);
	for (j=0; j<i; j++) {
	    x = gretl_matrix_get(phi, i-1, j);
	    x -= pacf[i] * gretl_matrix_get(phi, i-1, i-j-1);
	    gretl_matrix_set(phi, i, j, x);
	}
    }
	
    gretl_matrix_free(phi);

    return 0;
}

/* also used in GUI, library.c */

int auto_acf_order (int pd, int n)
{
    int m = 10 * log10(n);

    if (m > n / 5) {
	/* restrict to 20 percent of data (Tadeusz) */
	m = n / 5;
    }

    return m;
}

/**
 * gretl_acf:
 * @k: lag order.
 * @t1: starting observation.
 * @t2: ending observation.
 * @y: data series.
 * @ybar: mean of @y over range @t1 to @t2.
 *
 * Returns: the autocorrelation at lag @k for the series @y over
 * the range @t1 to @t2, or #NADBL on failure.
 */

static double gretl_acf (int k, int t1, int t2, const double *y,
			 double ybar)
{
    double z, num = 0, den = 0;
    int t, n = 0;

    for (t=t1; t<=t2; t++) {
	if (na(y[t])) {
	    return NADBL;
	}
	z = y[t] - ybar;
	den += z * z;
	if (t - k >= t1) {
	    if (na(y[t-k])) {
		return NADBL;
	    }
	    num += z * (y[t-k] - ybar);
	    n++;
	}
    }

    if (n == 0) {
	return NADBL;
    }

    return num / den;
}

/**
 * ljung_box:
 * @m: maximum lag.
 * @t1: starting observation.
 * @t2: ending observation.
 * @y: data series.
 * @err: location to receive erro code.
 *
 * Returns: the Ljung-Box statistic for lag order @m for
 * the series @y over the sample @t1 to @t2, or #NADBL 
 * on failure.
 */

double ljung_box (int m, int t1, int t2, const double *y, int *err)
{
    double ybar, acf, LB = 0.0;
    int k, n = t2 - t1 + 1;

    *err = 0;

    if (n == 0 || gretl_isconst(t1, t2, y)) {
	*err = E_DATA;
	return NADBL;
    }  

    ybar = gretl_mean(t1, t2, y);
    if (na(ybar)) {
	*err = E_DATA;
	return NADBL;
    }

    /* calculate acf up to lag m, cumulating LB */
    for (k=1; k<=m; k++) {
	acf = gretl_acf(k, t1, t2, y, ybar);
	if (na(acf)) {
	    *err = E_MISSDATA;
	    break;
	}
	LB += acf * acf / (n - k);
    }

    if (*err) {
	LB = NADBL;
    } else {
	LB *= n * (n + 2.0);
    }

    return LB;
}

/**
 * gretl_xcf:
 * @k: lag order (or lead order if < 0).
 * @t1: starting observation.
 * @t2: ending observation.
 * @x: first data series.
 * @y: second data series.
 * @xbar: mean of first series.
 * @ybar: mean of second series.
 *
 * Returns: the cross-correlation at lag (or lead) @k for the 
 * series @x and @y over the range @t1 to @t2, or #NADBL on failure.
 */

static double 
gretl_xcf (int k, int t1, int t2, const double *x, const double *y,
	   double xbar, double ybar)
{
    double num = 0, den1 = 0, den2 = 0;
    double zx, zy;
    int t;

    for (t=t1; t<=t2; t++) {
	if (na(x[t]) || na(y[t])) {
	    return NADBL;
	}
	zx = x[t] - xbar;
	zy = y[t] - ybar;
	den1 += zx * zx;
	den2 += zy * zy;
	if ((k >= 0 && t - k >= t1) || (k < 0 && t - k <= t2)) {
	    num += zx * (y[t-k] - ybar);
	}
    }

    return num / sqrt(den1 * den2);
}

static int corrgram_graph (const char *vname, double *acf, int m,
			   double *pacf, double *pm, gretlopt opt)
{
    char crit_string[16];
    FILE *fp = NULL;
    int k, err;

    err = gnuplot_init(PLOT_CORRELOGRAM, &fp);
    if (err) {
	return err;
    }

    sprintf(crit_string, "%.2f/T^%.1f", 1.96, 0.5);

    gretl_push_c_numeric_locale();

    /* create two separate plots, if both are OK */
    if (pacf != NULL) {
	fputs("set size 1.0,1.0\nset multiplot\nset size 1.0,0.48\n", fp);
    }
    fputs("set xzeroaxis\n", fp);
    print_keypos_string(GP_KEY_RIGHT_TOP, fp);
    fprintf(fp, "set xlabel '%s'\n", _("lag"));
    fputs("set yrange [-1.1:1.1]\n", fp);

    /* upper plot: Autocorrelation Function or ACF */
    if (pacf != NULL) {
	fputs("set origin 0.0,0.50\n", fp);
    }
    if (opt & OPT_R) {
	fprintf(fp, "set title '%s'\n", _("Residual ACF"));
    } else {
	fprintf(fp, "set title '%s %s'\n", _("ACF for"), vname);
    }
    fprintf(fp, "set xrange [0:%d]\n", m + 1);
    fprintf(fp, "plot \\\n"
	    "'-' using 1:2 notitle w impulses lw 5, \\\n"
	    "%g title '+- %s' lt 2, \\\n"
	    "%g notitle lt 2\n", pm[1], crit_string, -pm[1]);
    for (k=0; k<m; k++) {
	fprintf(fp, "%d %g\n", k + 1, acf[k]);
    }
    fputs("e\n", fp);

    if (pacf != NULL) {
	/* lower plot: Partial Autocorrelation Function or PACF */
	fputs("set origin 0.0,0.0\n", fp);
	if (opt & OPT_R) {
	    fprintf(fp, "set title '%s'\n", _("Residual PACF"));
	} else {
	    fprintf(fp, "set title '%s %s'\n", _("PACF for"), vname);
	}
	fprintf(fp, "set xrange [0:%d]\n", m + 1);
	fprintf(fp, "plot \\\n"
		"'-' using 1:2 notitle w impulses lw 5, \\\n"
		"%g title '+- %s' lt 2, \\\n"
		"%g notitle lt 2\n", pm[1], crit_string, -pm[1]);
	for (k=0; k<m; k++) {
	    fprintf(fp, "%d %g\n", k + 1, pacf[k]);
	}
	fputs("e\n", fp);
    }

    if (pacf != NULL) {
	fputs("set nomultiplot\n", fp);
    }

    gretl_pop_c_numeric_locale();

    fclose(fp);

    return gnuplot_make_graph();
}

static int corrgm_ascii_plot (const char *vname,
			      const double *acf, int m,
			      PRN *prn)
{
    double *xk = malloc(m * sizeof *xk);
    int k;

    if (xk == NULL) {
	return E_ALLOC;
    }

    for (k=0; k<m; k++) {
	xk[k] = k + 1.0;
    }

    pprintf(prn, "\n\n%s\n\n", _("Correlogram"));
    graphyx(acf, xk, m, vname, _("lag"), prn);

    free(xk);

    return 0;
}

/**
 * corrgram:
 * @varno: ID number of variable to process.
 * @order: integer order for autocorrelation function.
 * @nparam: number of estimated parameters (e.g. for the
 * case of ARMA), used to correct the degrees of freedom 
 * for Q test.
 * @Z: data array.
 * @pdinfo: information on the data set.
 * @prn: gretl printing struct.
 * @opt: if includes %OPT_Q, no plot, else if includes %OPT_A, 
 * use ASCII graphics; if includes %OPT_R, variable in question 
 * is a model residual generated "on the fly".
 *
 * Computes the autocorrelation function and plots the correlogram for
 * the variable specified by @varno.
 *
 * Returns: 0 on successful completion, error code on error.
 */

int corrgram (int varno, int order, int nparam, const double **Z, 
	      DATAINFO *pdinfo, PRN *prn, gretlopt opt)
{
    double ybar, box, pm[3];
    double *acf = NULL;
    double *pacf = NULL;
    const char *vname;
    int k, m, T, dfQ;
    int t1 = pdinfo->t1, t2 = pdinfo->t2;
    int list[2] = { 1, varno };
    int err = 0, pacf_err = 0;

    gretl_error_clear();

    varlist_adjust_sample(list, &t1, &t2, Z);
    T = t2 - t1 + 1;

    if (missvals(Z[varno] + t1, T)) {
	return E_MISSDATA;
    }

    if (T < 4) {
	return E_TOOFEW;
    }

    if (gretl_isconst(t1, t2, Z[varno])) {
	gretl_errmsg_sprintf(_("%s is a constant"), pdinfo->varname[varno]);
	return E_DATA;
    }

    ybar = gretl_mean(t1, t2, Z[varno]);
    if (na(ybar)) {
	return E_DATA;
    }

    vname = var_get_graph_name(pdinfo, varno);

    /* lag order for acf */
    m = order;
    if (m == 0) {
	m = auto_acf_order(pdinfo->pd, T);
    } else if (m > T - pdinfo->pd) {
	int mmax = T - 1; 

	if (m > mmax) {
	    m = mmax;
	}
    }

    acf = malloc(m * sizeof *acf);
    pacf = malloc(m * sizeof *pacf); 
    if (acf == NULL || pacf == NULL) {
	err = E_ALLOC;   
	goto bailout;
    }

    /* calculate acf up to order acf_m */
    for (k=1; k<=m; k++) {
	acf[k-1] = gretl_acf(k, t1, t2, Z[varno], ybar);
    }

    if ((opt & OPT_A) && !(opt & OPT_Q)) { 
	/* use ASCII graphics, not gnuplot */
	corrgm_ascii_plot(vname, acf, m, prn);
    } 

    if (opt & OPT_R) {
	pprintf(prn, "\n%s\n\n", _("Residual autocorrelation function"));
    } else {
	pputc(prn, '\n');
	pprintf(prn, _("Autocorrelation function for %s"), vname);
	pputs(prn, "\n\n");
    }

    /* for confidence bands */
    pm[0] = 1.65 / sqrt((double) T);
    pm[1] = 1.96 / sqrt((double) T);
    pm[2] = 2.58 / sqrt((double) T);

    /* generate (and if not in batch mode) plot partial 
       autocorrelation function */

    err = pacf_err = get_pacf(pacf, acf, m);

    pputs(prn, _("  LAG      ACF          PACF         Q-stat. [p-value]"));
    pputs(prn, "\n\n");

    box = 0.0;
    dfQ = 1;

    for (k=0; k<m; k++) {
	if (na(acf[k])) {
	    pprintf(prn, "%5d\n", k + 1);
	    continue;
	}

	pprintf(prn, "%5d%9.4f ", k + 1, acf[k]);

	if (fabs(acf[k]) > pm[2]) {
	    pputs(prn, " ***");
	} else if (fabs(acf[k]) > pm[1]) {
	    pputs(prn, " ** ");
	} else if (fabs(acf[k]) > pm[0]) {
	    pputs(prn, " *  ");
	} else {
	    pputs(prn, "    ");
	}

	if (na(pacf[k])) {
	    bufspace(13, prn);
	} else {
	    pprintf(prn, "%9.4f", pacf[k]);
	    if (fabs(pacf[k]) > pm[2]) {
		pputs(prn, " ***");
	    } else if (fabs(pacf[k]) > pm[1]) {
		pputs(prn, " ** ");
	    } else if (fabs(pacf[k]) > pm[0]) {
		pputs(prn, " *  ");
	    } else {
		pputs(prn, "    ");
	    }
	} 

	box += (T * (T + 2.0)) * acf[k] * acf[k] / (T - (k + 1));
	pprintf(prn, "%12.4f", box);
	if (k >= nparam) {
	    pprintf(prn, "  [%5.3f]", chisq_cdf_comp(dfQ++, box));
	}
	pputc(prn, '\n');
    }
    pputc(prn, '\n');

    if (!(opt & OPT_A) && !(opt & OPT_Q)) {
	err = corrgram_graph(vname, acf, m, (pacf_err)? NULL : pacf, 
			     pm, opt);
    }

 bailout:

    free(acf);
    free(pacf);

    return err;
}

/**
 * acf_vec:
 * @x: series to analyse.
 * @order: maximum lag for autocorrelation function.
 * @pdinfo: information on the data set, or %NULL.
 * @n: length of series (required if @pdinfo is %NULL).
 * @err: location to receive error code.
 *
 * Computes the autocorrelation function for series @x with
 * maximum lag @order.
 *
 * Returns: two-column matrix containing the values of the
 * ACF and PACF at the successive lags, or %NULL on error.
 */

gretl_matrix *acf_vec (const double *x, int order,
		       const DATAINFO *pdinfo, int n,
		       int *err)
{
    int t1, t2;
    gretl_matrix *acf = NULL;
    double xbar;
    int m, k, t, T;

    if (pdinfo != NULL) {
	t1 = pdinfo->t1;
	t2 = pdinfo->t2;

	while (na(x[t1])) t1++;
	while (na(x[t2])) t2--;

	T = t2 - t1 + 1;
    } else {
	t1 = 0;
	t2 = n - 1;
	T = n;
    }

    if (T < 4) {
	*err = E_TOOFEW;
	return NULL;
    }

    for (t=t1; t<=t2; t++) {
	if (na(x[t])) {
	    *err = E_MISSDATA;
	    return NULL;
	}
    }

    if (gretl_isconst(t1, t2, x)) {
	gretl_errmsg_set(_("Argument is a constant"));
	*err = E_DATA;
	return NULL;
    }

    xbar = gretl_mean(t1, t2, x);
    if (na(xbar)) {
	*err = E_DATA;
	return NULL;
    }	

    m = order;

    if (pdinfo == NULL) {
	if (m < 1 || m > T) {
	    *err = E_DATA;
	    return NULL;
	}
    } else {
	if (m == 0) {
	    m = auto_acf_order(pdinfo->pd, T);
	} else if (m > T - pdinfo->pd) {
	    int mmax = T - 1; 

	    if (m > mmax) {
		m = mmax;
	    }
	}
    }

    acf = gretl_matrix_alloc(m, 2);

    if (acf == NULL) {
	*err = E_ALLOC;   
	return NULL;
    }

    /* calculate ACF up to order m */
    for (k=1; k<=m && !*err; k++) {
	acf->val[k-1] = gretl_acf(k, t1, t2, x, xbar);
	if (na(acf->val[k-1])) {
	    *err = E_DATA;
	}
    }

    /* add PACF */
    if (!*err) {
	*err = get_pacf(acf->val + m, acf->val, m);
    }
    

    if (*err) {
	gretl_matrix_free(acf);
	acf = NULL;
    }

    return acf;
}

static int xcorrgm_graph (const char *xname, const char *yname,
			  double *xcf, int m, double *pm,
			  int allpos)
{
    char crit_string[16];
    char title[128]; 
    FILE *fp = NULL;
    int k, err;

    err = gnuplot_init(PLOT_CORRELOGRAM, &fp);
    if (err) {
	return err;
    }

    sprintf(crit_string, "%.2f/T^%.1f", 1.96, 0.5);

    gretl_push_c_numeric_locale();

    fputs("set xzeroaxis\n", fp);
    fputs("set yzeroaxis\n", fp);
    print_keypos_string(GP_KEY_RIGHT_TOP, fp);
    fprintf(fp, "set xlabel '%s'\n", _("lag"));
    if (allpos) {
	fputs("set yrange [-0.1:1.1]\n", fp);
    } else {
	fputs("set yrange [-1.1:1.1]\n", fp);
    } 
    sprintf(title, _("Correlations of %s and lagged %s"),
	    xname, yname);
    fprintf(fp, "set title '%s'\n", title);
    fprintf(fp, "set xrange [%d:%d]\n", -(m + 1), m + 1);
    if (allpos) {
	fprintf(fp, "plot \\\n"
		"'-' using 1:2 notitle w impulses lw 5, \\\n"
		"%g title '%s' lt 2\n", pm[1], crit_string);
    } else {
	fprintf(fp, "plot \\\n"
		"'-' using 1:2 notitle w impulses lw 5, \\\n"
		"%g title '+- %s' lt 2, \\\n"
		"%g notitle lt 2\n", pm[1], crit_string, -pm[1]);
    }	

    for (k=-m; k<=m; k++) {
	fprintf(fp, "%d %g\n", k, xcf[k+m]);
    }
    fputs("e\n", fp);

    gretl_pop_c_numeric_locale();

    fclose(fp);

    return gnuplot_make_graph();
}

static int xcorrgm_ascii_plot (double *xcf, int xcf_m, PRN *prn)
{
    double *xk = malloc((xcf_m * 2 + 1) * sizeof *xk);
    int k;

    if (xk == NULL) {
	return E_ALLOC;
    }

    for (k=-xcf_m; k<=xcf_m; k++) {
	xk[k+xcf_m] = k;
    }

    pprintf(prn, "\n\n%s\n\n", _("Cross-correlogram"));
    graphyx(xcf, xk, 2 * xcf_m + 1, "", _("lag"), prn); 

    free(xk);

    return 0;
}

/* We assume here that all data issues have already been
   assessed (lag length, missing values etc.) and we just
   get on with the job.
*/

static gretl_matrix *real_xcf_vec (const double *x, const double *y,
				   int p, int T, int *err)
{
    gretl_matrix *xcf;
    double xbar, ybar;
    int i;

    xbar = gretl_mean(0, T-1, x);
    if (na(xbar)) {
	*err = E_DATA;
	return NULL;
    }

    ybar = gretl_mean(0, T-1, y);
    if (na(ybar)) {
	*err = E_DATA;
	return NULL;
    }

    xcf = gretl_column_vector_alloc(p * 2 + 1);
    if (xcf == NULL) {
	*err = E_ALLOC;  
	return NULL;
    }

    for (i=-p; i<=p; i++) {
	xcf->val[i+p] = gretl_xcf(i, 0, T - 1, x, y, xbar, ybar);
    }

    return xcf;
}

/* for arrays @x and @y, check that there are no missing values
   and that neither series has a constant value 
*/

static int xcf_data_check (const double *x, const double *y, int T,
			   int *badvar)
{
    int xconst = 1, yconst = 1;
    int t;

    if (T < 5) {
	return E_TOOFEW;
    }

    for (t=0; t<T; t++) {
	if (na(x[t]) || na(y[t])) {
	    return E_MISSDATA;
	}
	if (t > 0 && x[t] != x[0]) {
	    xconst = 0;
	}
	if (t > 0 && y[t] != y[0]) {
	    yconst = 0;
	}	
    }

    if (xconst) {
	*badvar = 1;
	return E_DATA;
    } else if (yconst) {
	*badvar = 2;
	return E_DATA;
    }

    return 0;
}

/**
 * xcf_vec:
 * @x: first series.
 * @y: second series.
 * @p: maximum lag for cross-correlation function.
 * @pdinfo: information on the data set, or %NULL.
 * @n: length of series (required if @pdinfo is %NULL).
 * @err: location to receive error code.
 *
 * Computes the cross-correlation function for series @x with
 * series @y up to maximum lag @order.
 *
 * Returns: column vector containing the values of the
 * cross-correlation function, or %NULL on error.
 */

gretl_matrix *xcf_vec (const double *x, const double *y,
		       int p, const DATAINFO *pdinfo,
		       int n, int *err)
{
    gretl_matrix *xcf = NULL;
    int t1, t2;
    int T, badvar = 0;

    if (p <= 0) {
	*err = E_DATA;
	return NULL;
    }	

    if (pdinfo != NULL) {
	int yt1, yt2;

	t1 = yt1 = pdinfo->t1;
	t2 = yt2 = pdinfo->t2;

	while (na(x[t1])) t1++;
	while (na(y[yt1])) yt1++;

	while (na(x[t2])) t2--;
	while (na(y[yt2])) yt2--;

	t1 = (yt1 > t1)? yt1 : t1;
	t2 = (yt2 < t2)? yt2 : t2;

	T = t2 - t1 + 1;
    } else {
	t1 = 0;
	t2 = n - 1;
	T = n;
    }

#if 0
    fprintf(stderr, "t1=%d, t2=%d, T=%d\n", t1, t2, T);
    fprintf(stderr, "x[t1]=%g, y[t1]=%g\n\n", x[t1], y[t1]);
#endif

    if (pdinfo != NULL) {
	if (2 * p > T - pdinfo->pd) { /* ?? */
	    *err = E_DATA;
	}
    } else if (2 * p > T) {
	*err = E_DATA;
    }

    if (!*err) { 
	*err = xcf_data_check(x + t1, y + t1, T, &badvar);
	if (badvar) {
	    gretl_errmsg_sprintf(_("xcf: argument %d is a constant"), 
				 badvar);
	}
    }

    if (!*err) {
	xcf = real_xcf_vec(x + t1, y + t1, p, T, err);
    }

    return xcf;
}

/**
 * xcorrgram:
 * @list: should contain ID numbers of two variables.
 * @order: integer order for autocorrelation function.
 * @Z: data array.
 * @pdinfo: information on the data set.
 * @prn: gretl printing struct.
 * @opt: if includes %OPT_Q, no graphics, else if includes 
 * %OPT_A, use ASCII graphics.
 *
 * Computes the cross-cocorrelation function and plots the 
 * cross-correlogram for the specified variables.
 *
 * Returns: 0 on successful completion, error code on error.
 */

int xcorrgram (const int *list, int order, const double **Z, 
	       DATAINFO *pdinfo, PRN *prn, gretlopt opt)
{
    gretl_matrix *xcf = NULL;
    double pm[3];
    const char *xname, *yname;
    const double *x, *y;
    int k, p;
    int t1 = pdinfo->t1, t2 = pdinfo->t2;
    int badvar = 0;
    int T, err = 0;

    gretl_error_clear();

    if (list[0] != 2) {
	return E_DATA;
    }

    x = Z[list[1]];
    y = Z[list[2]];
    
    varlist_adjust_sample(list, &t1, &t2, Z);
    T = t2 - t1 + 1;

    err = xcf_data_check(x + t1, y + t1, T, &badvar);

    if (err) {
	if (badvar) {
	    gretl_errmsg_sprintf(_("%s is a constant"), 
				 pdinfo->varname[list[badvar]]);
	}
	return err;
    }

    xname = pdinfo->varname[list[1]];
    yname = pdinfo->varname[list[2]];

    p = order;
    if (p == 0) {
	p = auto_acf_order(pdinfo->pd, T) / 2;
    } else if (2 * p > T - pdinfo->pd) {
	p = (T - 1) / 2; /* ?? */
    }

    xcf = real_xcf_vec(x + t1, y + t1, p, T, &err);
    if (err) {
	return err;    
    }

    if ((opt & OPT_A) && !(opt & OPT_Q)) { 
	/* use ASCII graphics, not gnuplot */
	xcorrgm_ascii_plot(xcf->val, p, prn);
    } 

    /* for confidence bands */
    pm[0] = 1.65 / sqrt((double) T);
    pm[1] = 1.96 / sqrt((double) T);
    pm[2] = 2.58 / sqrt((double) T);

    pputc(prn, '\n');
    pprintf(prn, _("Cross-correlation function for %s and %s"), 
	    xname, yname);
    pputs(prn, "\n\n");
    pputs(prn, _("  LAG      XCF"));
    pputs(prn, "\n\n");

    for (k=-p; k<=p; k++) {
	double x = xcf->val[k + p];

	pprintf(prn, "%5d%9.4f", k, x);
	if (fabs(x) > pm[2]) {
	    pputs(prn, " ***");
	} else if (fabs(x) > pm[1]) {
	    pputs(prn, " **");
	} else if (fabs(x) > pm[0]) {
	    pputs(prn, " *");
	} 
	pputc(prn, '\n');
    }
    pputc(prn, '\n');

    if (!(opt & OPT_A) && !(opt & OPT_Q)) {
	int allpos = 1;

	for (k=-p; k<=p; k++) {
	    if (xcf->val[k+p] < 0) {
		allpos = 0;
		break;
	    }
	}
	err = xcorrgm_graph(xname, yname, xcf->val, p, pm, allpos);
    }

    gretl_matrix_free(xcf);

    return err;
}

static int roundup_mod (int i, double x)
{
    return (int) ceil((double) x * i);
}

static int fract_int_GPH (int m, double *hhat, double *omega, PRN *prn)
{
    gretl_matrix *y = NULL;
    gretl_matrix *X = NULL;
    gretl_matrix *b = NULL;
    gretl_matrix *V = NULL;
    double x;
    int t, err = 0;

    y = gretl_column_vector_alloc(m);
    X = gretl_unit_matrix_new(m, 2);
    b = gretl_column_vector_alloc(2);
    V = gretl_matrix_alloc(2, 2);

    if (y == NULL || X == NULL || b == NULL || V == NULL) {
	err = E_ALLOC;
	goto bailout;
    }

    /* Test from Geweke and Porter-Hudak, as set out in
       Greene, Econometric Analysis 4e, p. 787 */

    for (t=0; t<m; t++) {
	y->val[t] = log(hhat[t]);
	x = sin(omega[t] / 2);
	gretl_matrix_set(X, t, 1, log(4 * x * x));
    }    

    err = gretl_matrix_ols(y, X, b, V, NULL, &x);

    if (!err) {
	double bi = -b->val[1];
	double se = sqrt(gretl_matrix_get(V, 1, 1));
	double tval = bi / se;
	int df = m - 2;

	pprintf(prn, "%s (m = %d)\n"
		"  %s = %g (%g)\n"
		"  %s: t(%d) = %g, %s %.4f\n",
		_("GPH test for fractional integration"), m,
		_("Estimated degree of integration"), bi, se,
		_("test statistic"), df, tval, 
		_("with p-value"), student_pvalue_2(df, tval));
    } 

 bailout:

    gretl_matrix_free(y);
    gretl_matrix_free(X);
    gretl_matrix_free(b);
    gretl_matrix_free(V);

    return err;
}

static gretl_matrix *gretl_matrix_pergm (const gretl_matrix *x, int m)
{
    gretl_matrix *p = NULL;
    gretl_matrix *f = NULL;
    int T = gretl_vector_get_length(x);
    double re, im, scale = M_2PI * T;
    int i, err = 0;

    p = gretl_column_vector_alloc(m);
    if (p == NULL) {
	return NULL;
    }
    
    f = gretl_matrix_fft(x, &err);
    if (err) {
	gretl_matrix_free(p);
	return NULL;
    }

    for (i=0; i<m; i++) {
	re = gretl_matrix_get(f, i+1, 0);
	im = gretl_matrix_get(f, i+1, 1);
	p->val[i] = (re*re + im*im) / scale;
    }

    gretl_matrix_free(f);

    return p;
}

struct LWE_helper {
    gretl_matrix *lambda;
    gretl_matrix *lpow;
    gretl_matrix *I;
    gretl_matrix *I2;
    double lcm;
};

static void LWE_free (struct LWE_helper *L)
{
    gretl_matrix_free(L->lambda);
    gretl_matrix_free(L->lpow);
    gretl_matrix_free(L->I);
    gretl_matrix_free(L->I2);
}

static double LWE_obj_func (struct LWE_helper *L, double d)
{
    double dd = 2.0 * d;
    int i;

    gretl_matrix_copy_values(L->lpow, L->lambda);
    gretl_matrix_raise(L->lpow, dd);

    for (i=0; i<L->I->rows; i++) {
	L->I2->val[i] = L->I->val[i] * L->lpow->val[i];
    }

    return -(log(gretl_vector_mean(L->I2)) - dd * L->lcm);
}

static gretl_matrix *LWE_lambda (const gretl_matrix *I, int n)
{
    gretl_matrix *lambda;
    int i, m = gretl_vector_get_length(I);

    lambda = gretl_column_vector_alloc(m);

    if (lambda != NULL) {
	for (i=0; i<m; i++) {
	    gretl_vector_set(lambda, i, (M_2PI / n) * (i + 1));
	}
    }

    return lambda;
}

static int 
LWE_init (struct LWE_helper *L, const gretl_matrix *X, int m)
{
    int i, err = 0;

    L->I2 = L->lpow = NULL;

    L->I = gretl_matrix_pergm(X, m);
    if (L->I == NULL) {
	return E_ALLOC;
    } 

    L->lambda = LWE_lambda(L->I, X->rows);
    if (L->lambda == NULL) {
	gretl_matrix_free(L->I);
	return E_ALLOC;
    }

    L->lpow = gretl_matrix_copy(L->lambda);
    L->I2 = gretl_matrix_copy(L->I);
    if (L->lpow == NULL || L->I2 == NULL) {
	err = E_ALLOC;
	LWE_free(L);
    } else {
	L->lcm = 0.0;
	for (i=0; i<m; i++) {
	    L->lcm += log(L->lambda->val[i]);
	}
	L->lcm /= m;
    }   

    return err;
}

static double LWE (const gretl_matrix *X, int m)
{
    struct LWE_helper L;
    double d = 0, dd = 1.0;
    double eps = 1.0e-05;
    double f, incr, incl, deriv, h;
    int iter = 0;
    const int MAX_ITER = 100;
    int err;

    err = LWE_init(&L, X, m);
    if (err) {
	return NADBL;
    }

    while (fabs(dd) > 1.0e-06 && iter < MAX_ITER) {
	f = LWE_obj_func(&L, d);
	incr = LWE_obj_func(&L, d + eps) / eps;
	incl = LWE_obj_func(&L, d - eps) / eps;

	deriv = (incr - incl) / 2.0;
	h = (0.5 * (incr + incl) - f / eps) / eps;

	if (h >= 0) {
	    dd = deriv;
	} else {
	    dd = -deriv / h;
	}

	if (fabs(dd) > 1) {
	    dd = (dd > 0) ? 1 : -1;
	}
	
	d += 0.5 * dd;
	iter++;
    }

    if (err) {
	d = NADBL;
    } else if (iter == MAX_ITER) {
	fprintf(stderr, "Maximum number of iterations reached\n");
	d = NADBL;
    } 

    LWE_free(&L);

    return d;
}

int auto_spectrum_order (int T, gretlopt opt)
{
    int m;

    if (opt & OPT_O) {
	/* Bartlett */
	m = (int) 2.0 * sqrt((double) T);
    } else {
	/* fractional integration test */
	double m1 = floor((double) T / 2.0);
	double m2 = floor(pow((double) T, 0.6));

	m = (m1 < m2)? m1 : m2;
	m--;
    }

    return m;
}

static int fract_int_LWE (const double *x, int m, int t1, int t2,
			  PRN *prn)
{
    gretl_matrix *X;
    double d, se, z;
    int T;

    X = gretl_vector_from_series(x, t1, t2);
    if (X == NULL) {
	return 1;
    }

    T = gretl_vector_get_length(X);

    if (m <= 0) {
	m = auto_spectrum_order(T, OPT_NONE);
    } else if (m > T / 2.0) {
	m = T / 2.0;
    }

    d = LWE(X, m);
    if (na(d)) {
	gretl_matrix_free(X);
	return 1;
    }

    se = 1 / (2.0 * sqrt((double) m));
    z = d / se;

    pprintf(prn, "\n%s (m = %d)\n"
	    "  %s = %g (%g)\n"
	    "  %s: z = %g, %s %.4f\n\n",
	    _("Local Whittle Estimator"), m,
	    _("Estimated degree of integration"), d, se,
	    _("test statistic"), z, 
	    _("with p-value"), normal_pvalue_2(z));    

    gretl_matrix_free(X);

    return 0;
}

static int pergm_graph (const char *vname,
			const DATAINFO *pdinfo, 
			int T, int L, const double *x,
			gretlopt opt)
{
    FILE *fp = NULL;
    const char *pstr;
    char s[80];
    int k, t, err;

    err = gnuplot_init(PLOT_PERIODOGRAM, &fp);
    if (err) {
	return err;
    }

    fputs("# literal lines = 4\n", fp);
    fputs("set xtics nomirror\n", fp); 

    if (pdinfo->pd == 4) {
	pstr = N_("quarters");
    } else if (pdinfo->pd == 12) {
	pstr = N_("months");
    } else if (pdinfo->pd == 1 && pdinfo->structure == TIME_SERIES) {
	pstr = N_("years");
    } else {
	pstr = N_("periods");
    }

    fprintf(fp, "set x2label '%s'\n", _(pstr));
    fprintf(fp, "set x2range [0:%d]\n", roundup_mod(T, 2.0));

    fputs("set x2tics(", fp);
    k = (T / 2) / 6;
    for (t = 1; t <= T/2; t += k) {
	fprintf(fp, "\"%.1f\" %d, ", (double) T / t, 4 * t);
    }
    fprintf(fp, "\"\" %d)\n", 2 * T);

    fprintf(fp, "set xlabel '%s'\n", _("scaled frequency"));
    fputs("set xzeroaxis\n", fp);
    fputs("set nokey\n", fp);

    fputs("set title '", fp);

    if (opt & OPT_R) {
	fputs(_("Residual spectrum"), fp);
    } else {
	sprintf(s, _("Spectrum of %s"), vname);
	fputs(s, fp);
    }

    if (opt & OPT_O) {
	fputs(" (", fp);
	fprintf(fp, _("Bartlett window, length %d"), L);
	fputc(')', fp);
    } 

    if (opt & OPT_L) {
	fputs(" (", fp);
	fputs(_("log scale"), fp);
	fputc(')', fp);
    }

    fputs("'\n", fp);

    gretl_push_c_numeric_locale();

    fprintf(fp, "set xrange [0:%d]\n", roundup_mod(T, 0.5));
    if (!(opt & OPT_L)) {
	fprintf(fp, "set yrange [0:%g]\n", 1.2 * gretl_max(0, T/2, x));
    }

    fputs("plot '-' using 1:2 w lines\n", fp);

    for (t=1; t<=T/2; t++) {
	fprintf(fp, "%d %g\n", t, (opt & OPT_L)? log(x[t]) : x[t]);
    }

    gretl_pop_c_numeric_locale();

    fputs("e\n", fp);
    fclose(fp);

    return gnuplot_make_graph();
}

static void 
pergm_print_header (const char *vname, int T, int L,
		    gretlopt opt, PRN *prn)
{
    if (opt & OPT_R) {
	pprintf(prn, "\n%s\n", _("Residual periodogram"));
    } else {
	pprintf(prn, _("\nPeriodogram for %s\n"), vname);
    }

    pprintf(prn, _("Number of observations = %d\n"), T);
    if (opt & OPT_O) {
	pprintf(prn, _("Using Bartlett lag window, length %d\n\n"), L);
    } else {
	pputc(prn, '\n');
    }
}

/* if @pmat is non-NULL, we're filling out a matrix for the 
   pergm() function, otherwise we're responding to the 
   pergm command
*/

static int real_periodogram (const double *x, int varno, int width,
			     const DATAINFO *pdinfo, gretlopt opt, 
			     PRN *prn, gretl_matrix **pmat)
{
    double *acov = NULL;
    double *omega = NULL;
    double *hhat = NULL;
    double *sdy = NULL;
    double *xvec = NULL;
    const char *vname;
    char dstr[32];
    double xx, yy, varx, sdx, w;
    int k, L, m, T, t; 
    int t1 = pdinfo->t1, t2 = pdinfo->t2;
    int window = (opt & OPT_O);
    int err = 0;

    gretl_error_clear();

    array_adjust_t1t2(x, &t1, &t2);
    T = t2 - t1 + 1;

    if (missvals(x + t1, T)) {
	return E_MISSDATA;
    }    

    if (T < 12) {
	return E_TOOFEW;
    }

    if (gretl_isconst(t1, t2, x)) {
	if (varno >= 0) {
	    gretl_errmsg_sprintf(_("'%s' is a constant"), 
				 pdinfo->varname[varno]);
	}
	return E_DATA;
    }

    /* Chatfield (1996); William Greene, 4e, p. 772 */
    if (window) {
	if (width <= 0) {
	    L = auto_spectrum_order(T, opt);
	} else {
	    L = (width > T / 2)? T / 2 : width;
	} 
    } else {
	L = T - 1; 
    }

    /* prepare for fractional integration test */
    if (width <= 0) {
	m = auto_spectrum_order(T, OPT_NONE);
    } else {
	m = (width > T / 2)? T / 2 : width;
    } 
    
    acov = malloc((L + 1) * sizeof *acov);
    omega = malloc(m * sizeof *omega);
    hhat = malloc(m * sizeof *hhat);
    sdy = malloc(T * sizeof *sdy);
    xvec = malloc((1 + T/2) * sizeof *xvec);

    if (acov == NULL || omega == NULL || hhat == NULL || 
	sdy == NULL || xvec == NULL) {
	err = E_ALLOC;
	goto bailout;
    }

    xx = gretl_mean(t1, t2, x);
    varx = gretl_variance(t1, t2, x);
    varx *= (double) (T - 1) / T;
    sdx = sqrt(varx);

    for (t=t1; t<=t2; t++) {
	sdy[t-t1] = (x[t] - xx) / sdx;
    }

    /* find autocovariances */
    for (k=1; k<=L; k++) {
	acov[k] = 0.0;
	for (t=k; t<T; t++) {
	    acov[k] += sdy[t] * sdy[t-k];
	}
	acov[k] /= T;
    }

    for (t=1; t<=T/2; t++) {
	yy = M_2PI * t / (double) T;
	xx = 1.0; 
	for (k=1; k<=L; k++) {
	    w = (window)? 1 - (double) k/(L + 1) : 1;
	    xx += 2.0 * w * acov[k] * cos(yy * k);
	}
	xx *= varx / M_2PI;
	xvec[t] = xx;
	if (t <= m) {
	    omega[t-1] = yy;
	    hhat[t-1] = xx;
	}
    }

    if (pmat != NULL) {
	/* making a matrix */
	int T2 = T / 2;
	gretl_matrix *m;

	m = gretl_matrix_alloc(T2, 2);

	if (m == NULL) {
	    err = E_ALLOC;
	} else {
	    for (t=1; t<=T2; t++) {
		gretl_matrix_set(m, t-1, 0, M_2PI * t / (double) T);
		gretl_matrix_set(m, t-1, 1, xvec[t]);
	    }
	}

	*pmat = m;
	goto bailout;
    }

    vname = var_get_graph_name(pdinfo, varno);
    pergm_print_header(vname, T, L, opt, prn);

    if (!window) {
	if (fract_int_GPH(m, hhat, omega, prn)) {
	    pprintf(prn, "\n%s\n", _("Fractional integration test failed"));
	}
	fract_int_LWE(x, width, t1, t2, prn);
    }

    if (opt & OPT_L) {
	pputs(prn, _(" omega  scaled frequency  periods  log spectral density\n\n"));
    } else {
	pputs(prn, _(" omega  scaled frequency  periods  spectral density\n\n"));
    }

    for (t=1; t<=T/2; t++) {
	yy = M_2PI * t / (double) T;
	xx = (opt & OPT_L)? log(xvec[t]) : xvec[t];
	pprintf(prn, " %.4f%9d%16.2f", yy, t, (double) T / t);
	sprintf(dstr, "%#.5g", xx);
	gretl_fix_exponent(dstr);
	pprintf(prn, "%16s\n", dstr);
    }

    pputc(prn, '\n');

    if (!(opt & OPT_N)) {
	pergm_graph(vname, pdinfo, T, L, xvec, opt);
    }

 bailout:

    free(acov);
    free(omega);
    free(hhat);
    free(sdy);
    free(xvec);

    return err;
}

/**
 * periodogram:
 * @varno: ID number of variable to process.
 * @width: width of window.
 * @Z: data array.
 * @pdinfo: information on the data set.
 * @opt: if includes %OPT_O, use Bartlett lag window for periodogram;
 * if includes %OPT_N, don't display gnuplot graph; if includes
 * %OPT_R, the variable is a model residual; %OPT_L, use log scale.
 * @prn: gretl printing struct.
 *
 * Computes and displays the periodogram for the variable specified 
 * by @varno.
 *
 * Returns: 0 on successful completion, error code on error.
 */

int periodogram (int varno, int width, 
		 const double **Z, const DATAINFO *pdinfo, 
		 gretlopt opt, PRN *prn)
{
    return real_periodogram(Z[varno], varno, width, 
			    pdinfo, opt, prn, NULL);
}

gretl_matrix *periodogram_func (const double *x, const DATAINFO *pdinfo,
				int width, int *err)
{
    gretlopt opt = (width < 0)? OPT_NONE : OPT_O;
    gretl_matrix *m = NULL;

    *err = real_periodogram(x, -1, width, pdinfo, opt, NULL, &m);

    return m;
}

static void printf15 (double x, PRN *prn)
{
    if (na(x)) {
	pputc(prn, ' ');
	pprintf(prn, "%*s", UTF_WIDTH(_("NA"), 14), _("NA"));
    } else {
	pputc(prn, ' ');
#if 1
	pprintf(prn, "%#14.5g", x);
#else
	gretl_print_fullwidth_double(x, 5, prn);
#endif	
    }
}

static void output_line (char *str, PRN *prn, int dblspc)
{
    pputs(prn, str);
    if (dblspc) {
	pputs(prn, "\n\n");
    } else {
	pputc(prn, '\n');
    }
}

static void prhdr (const char *str, const DATAINFO *pdinfo, 
		   int ci, int missing, PRN *prn)
{
    char date1[OBSLEN], date2[OBSLEN], tmp[96];

    ntodate(date1, pdinfo->t1, pdinfo);
    ntodate(date2, pdinfo->t2, pdinfo);

    pputc(prn, '\n');

    sprintf(tmp, _("%s, using the observations %s - %s"), str, date1, date2);
    output_line(tmp, prn, 0);

    if (missing) {
	strcpy(tmp, _("(missing values were skipped)"));
	output_line(tmp, prn, 1);
    }
}

static void print_summary_single (const Summary *s, int j,
				  const DATAINFO *pdinfo,
				  PRN *prn)
{
    char obs1[OBSLEN], obs2[OBSLEN], tmp[128];
    double vals[8];
    const char *labels[] = {
	N_("Mean"),
	N_("Median"),
	N_("Minimum"),
	N_("Maximum"),
	N_("Standard deviation"),
	N_("C.V."),
	N_("Skewness"),
	N_("Ex. kurtosis")
    };
    const char *wstr = N_("Within s.d.");
    const char *bstr = N_("Between s.d.");
    int simple_skip[] = {0,1,0,0,0,1,1,1};
    int slen = 0, i = 0;

    ntodate(obs1, pdinfo->t1, pdinfo);
    ntodate(obs2, pdinfo->t2, pdinfo);

    prhdr(_("Summary statistics"), pdinfo, SUMMARY, 0, prn);
    sprintf(tmp, _("for the variable '%s' (%d valid observations)"), 
	    pdinfo->varname[s->list[j+1]], s->n);
    output_line(tmp, prn, 1);

    vals[0] = s->mean[j];
    vals[1] = s->median[j];
    vals[2] = s->low[j];
    vals[3] = s->high[j];
    vals[4] = s->sd[j];
    vals[5] = s->cv[j];
    vals[6] = s->skew[j];
    vals[7] = s->xkurt[j];

    for (i=0; i<8; i++) {
	if ((s->opt & OPT_S) && simple_skip[i]) {
	    continue;
	}	
	if (strlen(_(labels[i])) > slen) {
	    slen = g_utf8_strlen(_(labels[i]), -1);	    
	}
    }
    slen++;

    for (i=0; i<8; i++) {
	if ((s->opt & OPT_S) && simple_skip[i]) {
	    continue;
	}
	pprintf(prn, "  %-*s", UTF_WIDTH(_(labels[i]), slen), _(labels[i]));
	printf15(vals[i], prn);
	pputc(prn, '\n');
    }

    if (!na(s->sw) && !na(s->sb)) {
	pputc(prn, '\n');
	pprintf(prn, "  %-*s", UTF_WIDTH(_(wstr), slen), _(wstr));
	printf15(s->sw, prn);
	pputc(prn, '\n');
	pprintf(prn, "  %-*s", UTF_WIDTH(_(bstr), slen), _(bstr));
	printf15(s->sb, prn);
    }  

    pputs(prn, "\n\n");    
}

/**
 * print_summary:
 * @summ: gretl summary statistics struct.
 * @pdinfo: information on the data set.
 * @prn: gretl printing struct.
 *
 * Prints the summary statistics for a given variable.
 */

void print_summary (const Summary *summ,
		    const DATAINFO *pdinfo,
		    PRN *prn)
{
    int len, maxlen = 0;
    int i, vi;

    if (summ->list == NULL || summ->list[0] == 0) {
	return;
    }

    if (summ->list[0] == 1 && !(summ->opt & OPT_B)) {
	print_summary_single(summ, 0, pdinfo, prn);
	return;
    }

    for (i=1; i<=summ->list[0]; i++) {
	vi = summ->list[i];
	len = strlen(pdinfo->varname[vi]);
	if (len > maxlen) {
	    maxlen = len;
	}
    }

    len = (maxlen <= 8)? 10 : (maxlen + 1);

    if (!(summ->opt & OPT_B)) {
	prhdr(_("Summary statistics"), pdinfo, SUMMARY, summ->missing, prn);
    }

    pputc(prn, '\n');

    if (summ->opt & OPT_S) {
	/* the "simple" option */
	const char *h[] = {
	    N_("Mean"),
	    N_("Minimum"),
	    N_("Maximum"),
	    N_("Std. Dev.")
	};

	pprintf(prn, "%*s%*s%*s%*s%*s\n", len, " ",
		UTF_WIDTH(_(h[0]), 15), _(h[0]),
		UTF_WIDTH(_(h[0]), 15), _(h[1]),
		UTF_WIDTH(_(h[0]), 15), _(h[2]),
		UTF_WIDTH(_(h[0]), 15), _(h[3]));

	for (i=0; i<summ->list[0]; i++) {
	    vi = summ->list[i + 1];
	    pprintf(prn, "%-*s", len, pdinfo->varname[vi]);
	    printf15(summ->mean[i], prn);
	    printf15(summ->low[i], prn);
	    printf15(summ->high[i], prn);
	    printf15(summ->sd[i], prn);
	    pputc(prn, '\n');
	}
    } else {
	/* print all available stats */
	const char *ha[] = {
	    N_("Mean"),
	    N_("Median"),
	    N_("Minimum"),
	    N_("Maximum"),
	};
	const char *hb[] = {
	    N_("Std. Dev."),
	    N_("C.V."),
	    N_("Skewness"),
	    N_("Ex. kurtosis")
	};

	pprintf(prn, "%*s%*s%*s%*s%*s\n", len, " ",
		UTF_WIDTH(_(ha[0]), 15), _(ha[0]),
		UTF_WIDTH(_(ha[1]), 15), _(ha[1]),
		UTF_WIDTH(_(ha[2]), 15), _(ha[2]),
		UTF_WIDTH(_(ha[3]), 15), _(ha[3]));

	for (i=0; i<summ->list[0]; i++) {
	    vi = summ->list[i + 1];
	    pprintf(prn, "%-*s", len, pdinfo->varname[vi]);
	    printf15(summ->mean[i], prn);
	    printf15(summ->median[i], prn);
	    printf15(summ->low[i], prn);
	    printf15(summ->high[i], prn);
	    pputc(prn, '\n');
	}
	pputc(prn, '\n');

	pprintf(prn, "%*s%*s%*s%*s%*s\n", len, " ",
		UTF_WIDTH(_(hb[0]), 15), _(hb[0]),
		UTF_WIDTH(_(hb[1]), 15), _(hb[1]),
		UTF_WIDTH(_(hb[2]), 15), _(hb[2]),
		UTF_WIDTH(_(hb[3]), 15), _(hb[3]));

	for (i=0; i<summ->list[0]; i++) {
	    double cv;

	    vi = summ->list[i + 1];
	    pprintf(prn, "%-*s", len, pdinfo->varname[vi]);

	    if (floateq(summ->mean[i], 0.0)) {
		cv = NADBL;
	    } else if (floateq(summ->sd[i], 0.0)) {
		cv = 0.0;
	    } else {
		cv = fabs(summ->sd[i] / summ->mean[i]);
	    } 

	    printf15(summ->sd[i], prn);
	    printf15(cv, prn);
	    printf15(summ->skew[i], prn);
	    printf15(summ->xkurt[i], prn);
	    pputc(prn, '\n');
	}
    }

    pputc(prn, '\n');
}

/**
 * free_summary:
 * @summ: gretl summary statistics struct
 *
 * Frees all malloced elements of the struct.
 */

void free_summary (Summary *summ)
{
    free(summ->list);
    free(summ->stats);

    free(summ);
}

static Summary *summary_new (const int *list, gretlopt opt)
{
    Summary *s;
    int nv = list[0];

    s = malloc(sizeof *s);
    if (s == NULL) {
	return NULL;
    }

    s->list = gretl_list_copy(list);
    if (s->list == NULL) {
	free(s);
	return NULL;
    }

    s->opt = opt;
    s->n = 0;
    s->missing = 0;

    s->stats = malloc(8 * nv * sizeof *s->stats);
    if (s->stats == NULL) {
	free_summary(s);
	return NULL;
    }

    s->mean = s->stats;
    s->median = s->mean + nv;
    s->sd = s->median + nv;
    s->skew = s->sd + nv;
    s->xkurt = s->skew + nv;
    s->low = s->xkurt + nv;
    s->high = s->low + nv;
    s->cv = s->high + nv;

    s->sb = s->sw = NADBL;

    return s;
}

/**
 * get_summary:
 * @list: list of variables to process.
 * @Z: data matrix.
 * @pdinfo: information on the data set.
 * @opt: may include %OPT_S for "simple" version.
 * @prn: gretl printing struct.
 * @err: location to receive error code.
 *
 * Calculates descriptive summary statistics for the specified variables.
 *
 * Returns: struct containing the summary statistics, or %NULL
 * on failure.
 */

Summary *get_summary (const int *list, const double **Z, 
		      const DATAINFO *pdinfo, 
		      gretlopt opt, PRN *prn, 
		      int *err) 
{
    Summary *s;
    int i, vi, ni, nmax;

    s = summary_new(list, opt);

    if (s == NULL) {
	*err = E_ALLOC;
	return NULL;
    }

    nmax = sample_size(pdinfo);

    for (i=0; i<s->list[0]; i++)  {
	double *pskew = NULL, *pkurt = NULL;
	double x0;

	vi = s->list[i + 1];
	ni = good_obs(Z[vi] + pdinfo->t1, nmax, &x0);

	if (ni < nmax) {
	    s->missing = 1;
	}

	if (ni > s->n) {
	    s->n = ni;
	}

	if (ni == 0) { 
	    pprintf(prn, _("Dropping %s: sample range contains no valid "
			   "observations\n"), pdinfo->varname[vi]);
	    gretl_list_delete_at_pos(s->list, i + 1);
	    if (s->list[0] == 0) {
		return s;
	    } else {
		i--;
		continue;
	    }
	}

	if (opt & OPT_S) {
	    s->skew[i] = NADBL;
	    s->xkurt[i] = NADBL;
	    s->cv[i] = NADBL;
	    s->median[i] = NADBL;
	} else {
	    pskew = &s->skew[i];
	    pkurt = &s->xkurt[i];
	}

	gretl_minmax(pdinfo->t1, pdinfo->t2, Z[vi], 
		     &s->low[i], 
		     &s->high[i]);
	
	gretl_moments(pdinfo->t1, pdinfo->t2, Z[vi], 
		      &s->mean[i], 
		      &s->sd[i], 
		      pskew, pkurt, 
		      1);

	if (!(opt & OPT_S)) {
	    if (floateq(s->mean[i], 0.0)) {
		s->cv[i] = NADBL;
	    } else if (floateq(s->sd[i], 0.0)) {
		s->cv[i] = 0.0;
	    } else {
		s->cv[i] = fabs(s->sd[i] / s->mean[i]);
	    } 
	    s->median[i] = gretl_median(pdinfo->t1, pdinfo->t2, Z[vi]);
	}
    } 

    if (dataset_is_panel(pdinfo) && list[0] == 1) {
	panel_variance_info(Z[list[1]], pdinfo, s->mean[0], &s->sw, &s->sb);
    }

    return s;
}

int list_summary (const int *list, const double **Z, 
		  const DATAINFO *pdinfo, 
		  gretlopt opt, PRN *prn)
{
    Summary *summ;
    int err = 0;

    if (list[0] == 0) {
	return 0;
    }

    summ = get_summary(list, Z, pdinfo, opt, prn, &err);

    if (!err) {
	print_summary(summ, pdinfo, prn);
	free_summary(summ);
    }

    return err;
}

/**
 * vmatrix_new:
 *
 * Returns: an allocated and initialized #VMatrix, or
 * %NULL on failure.
 */

VMatrix *vmatrix_new (void)
{
    VMatrix *v = malloc(sizeof *v);

    if (v != NULL) {
	v->vec = NULL;
	v->list = NULL;
	v->names = NULL;
	v->ci = 0;
	v->dim = 0;
	v->t1 = 0;
	v->t2 = 0;
	v->n = 0;
	v->missing = 0;
    }

    return v;
}

/**
 * free_vmatrix:
 * @vmat: gretl correlation matrix struct
 *
 * Frees all malloced elements of the struct.
 */

void free_vmatrix (VMatrix *vmat)
{
    if (vmat != NULL) {
	if (vmat->names != NULL) {
	    free_strings_array(vmat->names, vmat->dim);
	}
	if (vmat->vec != NULL) {
	    free(vmat->vec);
	}
	if (vmat->list != NULL) {
	    free(vmat->list);
	}
	free(vmat);
    }
}

enum {
    CORRMAT = 0,
    COVMAT
};

/* compute correlation or covariance matrix, using maximum possible
   sample for each coefficient */

static int max_corrcov_matrix (VMatrix *v, const double **Z, int flag)
{
    int i, j, vi, vj, nij;
    int nmaxmin = v->t2 - v->t1 + 1;
    int nmaxmax = 0;
    int m = v->dim;
    int missing;

    for (i=0; i<m; i++) {  
	vi = v->list[i+1];
	for (j=i; j<m; j++)  {
	    vj = v->list[j+1];
	    nij = ijton(i, j, m);
	    missing = 0;
	    if (i == j && flag == CORRMAT) {
		v->vec[nij] = 1.0;
	    } else {
		missing = 0;
		if (flag == COVMAT) {
		    v->vec[nij] = gretl_covar(v->t1, v->t2, Z[vi], Z[vj], 
					      &missing);
		} else {
		    v->vec[nij] = gretl_corr(v->t1, v->t2, Z[vi], Z[vj], 
					     &missing);
		}
		if (missing > 0) {
		    int n = v->t2 - v->t1 + 1 - missing;

		    if (n < nmaxmin && n > 0) {
			nmaxmin = n;
		    } 
		    if (n > nmaxmax) {
			nmaxmax = n;
		    }
		    v->missing = 1;
		} else {
		    nmaxmax = v->t2 - v->t1 + 1;
		}
	    }
	}
    }

    /* we'll record an "n" value if there's something resembling
       a common value across the coefficients */

    if (!v->missing) {
	v->n = v->t2 - v->t1 + 1;
    } else if (nmaxmax > 0) {
	double d = (nmaxmax - nmaxmin) / (double) nmaxmax;

	if (d < .10) {
	    v->n = nmaxmin;
	}
    } 

    return 0;
}

/* compute correlation or covariance matrix, ensuring we
   use the same sample for all coefficients */

static int uniform_corrcov_matrix (VMatrix *v, const double **Z, int flag)
{
    double *xbar = NULL, *ssx = NULL;
    int m = v->dim;
    int mm = (m * (m + 1)) / 2;
    double d1, d2;
    int i, j, jmin, nij, t;
    int miss, n = 0;

    xbar = malloc(m * sizeof *xbar);
    if (xbar == NULL) {
	return E_ALLOC;
    }

    if (flag == CORRMAT) {
	ssx = malloc(m * sizeof *ssx);
	if (ssx == NULL) {
	    free(xbar);
	    return E_ALLOC;
	}
	for (i=0; i<m; i++) {
	    ssx[i] = 0.0;
	}
    }    

    for (i=0; i<m; i++) {
	xbar[i] = 0.0;
    }       

    /* first pass: get sample size and sums */

    for (t=v->t1; t<=v->t2; t++) {
	miss = 0;
	for (i=0; i<m; i++) {
	    if (na(Z[v->list[i+1]][t])) {
		miss = 1;
		v->missing = 1;
		break;
	    }
	}
	if (!miss) {
	    n++;
	    for (i=0; i<m; i++) {
		xbar[i] += Z[v->list[i+1]][t];
	    }	    
	}
    }
		
    if (n < 2) {
	free(xbar);
	free(ssx);
	return E_MISSDATA;
    }

    for (i=0; i<m; i++) {
	xbar[i] /= n;
    }    

    for (i=0; i<mm; i++) {
	v->vec[i] = 0.0;
    }

    /* second pass: get deviations from means and cumulate */

    for (t=v->t1; t<=v->t2; t++) {
	miss = 0;
	for (i=0; i<m; i++) {
	    if (na(Z[v->list[i+1]][t])) {
		miss = 1;
		break;
	    }
	}
	if (!miss) {
	    for (i=0; i<m; i++) {
		d1 = Z[v->list[i+1]][t] - xbar[i];
		if (ssx != NULL) {
		    ssx[i] += d1 * d1;
		}
		jmin = (flag == COVMAT)? i : i + 1;
		for (j=jmin; j<m; j++) {
		    nij = ijton(i, j, m);
		    d2 = Z[v->list[j+1]][t] - xbar[j];
		    v->vec[nij] += d1 * d2;
		}
	    }	    
	}
    } 

    /* finalize: compute correlations or covariances */

    if (flag == CORRMAT) {
	for (i=0; i<m; i++) {  
	    for (j=i; j<m; j++)  {
		nij = ijton(i, j, m);
		if (i == j) {
		    v->vec[nij] = 1.0;
		} else if (ssx[i] == 0.0 || ssx[j] == 0.0) {
		    v->vec[nij] = NADBL;
		} else {
		    v->vec[nij] /= sqrt(ssx[i] * ssx[j]);
		}
	    }
	}
    } else {
	for (i=0; i<mm; i++) {
	    v->vec[i] /= (n - 1);
	}
    }

    v->n = n;

    free(xbar);
    if (ssx != NULL) {
	free(ssx);
    }

    return 0;
}

/**
 * corrlist:
 * @list: list of variables to process, by ID number.
 * @Z: data matrix.
 * @pdinfo: data information struct.
 * @opt: option flags.
 * @err: location to receive error code.
 *
 * Computes pairwise correlation coefficients for the variables
 * specified in @list, skipping any constants.  If the option
 * flags contain %OPT_U, a uniform sample is ensured: only those
 * observations for which all the listed variables have valid
 * values are used.  If %OPT_C is included, we actually calculate
 * covariances rather than correlations.
 *
 * Returns: gretl correlation matrix struct, or %NULL on failure.
 */

VMatrix *corrlist (int *list, const double **Z, const DATAINFO *pdinfo,
		   gretlopt opt, int *err)
{
    int flag = (opt & OPT_C)? COVMAT : CORRMAT;
    VMatrix *v;
    int i, m, mm;

    v = vmatrix_new();
    if (v == NULL) {
	*err = E_ALLOC;
	return NULL;
    }

    /* drop any constants from list */
    for (i=1; i<=list[0]; i++) {
	if (gretl_isconst(pdinfo->t1, pdinfo->t2, Z[list[i]])) {
	    gretl_list_delete_at_pos(list, i);
	    i--;
	}
    }

    if (list[0] == 0) {
	*err = E_DATA;
	goto bailout;
    }	

    v->dim = m = list[0];  
    mm = (m * (m + 1)) / 2;

    v->names = strings_array_new(m);
    v->vec = malloc(mm * sizeof *v->vec);
    v->list = gretl_list_copy(list);

    if (v->names == NULL || v->vec == NULL || v->list == NULL) {
	*err = E_ALLOC;
	goto bailout;
    }

    v->t1 = pdinfo->t1;
    v->t2 = pdinfo->t2;

    if (opt & OPT_U) {
	/* impose uniform sample size */
	*err = uniform_corrcov_matrix(v, Z, flag);
    } else {
	/* sample sizes may differ */
	*err = max_corrcov_matrix(v, Z, flag);
    }

    if (!*err) {
	for (i=0; i<m; i++) {  
	    v->names[i] = gretl_strdup(pdinfo->varname[list[i+1]]);
	    if (v->names[i] == NULL) {
		*err = E_ALLOC;
		goto bailout;
	    }
	}
    }	

    v->ci = (flag == CORRMAT)? CORR : 0;

 bailout:
    
    if (*err) {
	free_vmatrix(v);
	v = NULL;
    }

    return v;
}

static void printcorr (const VMatrix *v, PRN *prn)
{
    double r = v->vec[1];

    pprintf(prn, "\ncorr(%s, %s)", v->names[0], v->names[1]);

    if (na(r)) {
	pprintf(prn, ": %s\n\n", _("undefined"));
    } else {
	pprintf(prn, " = %.8f\n", r);
	if (fabs(r) < 1.0) {
	    int n2 = v->n - 2;
	    double tval = r * sqrt(n2 / (1 - r*r)); 

	    pputs(prn, _("Under the null hypothesis of no correlation:\n "));
	    pprintf(prn, _("t(%d) = %g, with two-tailed p-value %.4f\n"), n2,
		    tval, student_pvalue_2(n2, tval));
	    pputc(prn, '\n');
	} else {
	    pprintf(prn, _("5%% critical value (two-tailed) = "
		       "%.4f for n = %d"), rhocrit95(v->n), v->n);
	    pputs(prn, "\n\n");
	}
    }
}

/**
 * print_corrmat:
 * @corr: gretl correlation matrix.
 * @pdinfo: dataset information.
 * @prn: gretl printing struct.
 *
 * Prints a gretl correlation matrix to @prn.
 */

void print_corrmat (VMatrix *corr, const DATAINFO *pdinfo, PRN *prn)
{
    if (corr->dim == 2) {
	printcorr(corr, prn);
    } else {
	char date1[OBSLEN], date2[OBSLEN], tmp[96];

	ntodate(date1, corr->t1, pdinfo);
	ntodate(date2, corr->t2, pdinfo);

	pputc(prn, '\n');

	sprintf(tmp, _("%s, using the observations %s - %s"), 
		_("Correlation Coefficients"), date1, date2);
	output_line(tmp, prn, 0);

	if (corr->missing) {
	    strcpy(tmp, _("(missing values were skipped)"));
	    output_line(tmp, prn, 1);
	}	

	if (corr->n > 0) {
	    sprintf(tmp, _("5%% critical value (two-tailed) = "
			   "%.4f for n = %d"), rhocrit95(corr->n), 
		    corr->n);
	    output_line(tmp, prn, 1);
	}

	text_print_vmatrix(corr, prn);
    }
}

/**
 * gretl_corrmx:
 * @list: gives the ID numbers of the variables to process.
 * @Z: data array.
 * @pdinfo: data information struct.
 * @opt: option flags: %OPT_U = use uniform sample size.
 * @prn: gretl printing struct.
 *
 * Computes and prints the correlation matrix for the specified list
 * of variables.
 *
 * Returns: 0 on successful completion, 1 on error.
 */

int gretl_corrmx (int *list, const double **Z, const DATAINFO *pdinfo, 
		  gretlopt opt, PRN *prn)
{
    VMatrix *corr;
    int err = 0;

    if (list[0] == 0) {
	return 0;
    }

    corr = corrlist(list, Z, pdinfo, opt, &err);
    if (err) {
	return err;
    }

    print_corrmat(corr, pdinfo, prn);
    free_vmatrix(corr);

    return 0;
}

/**
 * means_test:
 * @list: gives the ID numbers of the variables to compare.
 * @Z: data matrix.
 * @pdinfo: data information struct.
 * @opt: if OPT_O, assume population variances are different.
 * @prn: gretl printing struct.
 *
 * Carries out test of the null hypothesis that the means of two
 * variables are equal.
 *
 * Returns: 0 on successful completion, error code on error.
 */

int means_test (const int *list, const double **Z, const DATAINFO *pdinfo, 
		gretlopt opt, PRN *prn)
{
    double m1, m2, s1, s2, skew, kurt, se, mdiff, t, pval;
    double v1, v2;
    double *x = NULL, *y = NULL;
    int vardiff = (opt & OPT_O);
    int df, n1, n2, n = pdinfo->n;

    if (list[0] < 2) {
	return E_ARGS;
    }

    x = malloc(n * sizeof *x);
    if (x == NULL) {
	return E_ALLOC;
    }

    y = malloc(n * sizeof *y);
    if (y == NULL) {
	free(x);
	return E_ALLOC;
    }    

    n1 = ztox(list[1], x, Z, pdinfo);
    n2 = ztox(list[2], y, Z, pdinfo);

    if (n1 == 0 || n2 == 0) {
	pputs(prn, _("Sample range has no valid observations."));
	free(x); free(y);
	return 1;
    }

    if (n1 == 1 || n2 == 1) {
	pputs(prn, _("Sample range has only one observation."));
	free(x); free(y);
	return 1;
    }

    df = n1 + n2 - 2;

    gretl_moments(0, n1-1, x, &m1, &s1, &skew, &kurt, 1);
    gretl_moments(0, n2-1, y, &m2, &s2, &skew, &kurt, 1);
    mdiff = m1 - m2;

    v1 = s1 * s1;
    v2 = s2 * s2;

    if (vardiff) {
	se = sqrt((v1 / n1) + (v2 / n2));
    } else {
	/* form pooled estimate of variance */
	double sp2;

	sp2 = ((n1-1) * v1 + (n2-1) * v2) / df;
	se = sqrt(sp2 / n1 + sp2 / n2);
    }

    t = mdiff / se;
    pval = student_pvalue_2(df, t);

    pprintf(prn, _("\nEquality of means test "
	    "(assuming %s variances)\n\n"), (vardiff)? _("unequal") : _("equal"));
    pprintf(prn, "   %s: ", pdinfo->varname[list[1]]);
    pprintf(prn, _("Number of observations = %d\n"), n1);
    pprintf(prn, "   %s: ", pdinfo->varname[list[2]]);
    pprintf(prn, _("Number of observations = %d\n"), n2);
    pprintf(prn, _("   Difference between sample means = %g - %g = %g\n"), 
	    m1, m2, mdiff);
    pputs(prn, _("   Null hypothesis: The two population means are the same.\n"));
    pprintf(prn, _("   Estimated standard error = %g\n"), se);
    pprintf(prn, _("   Test statistic: t(%d) = %g\n"), df, t);
    pprintf(prn, _("   p-value (two-tailed) = %g\n\n"), pval);
    if (pval > .10)
	pputs(prn, _("   The difference is not statistically significant.\n\n"));

    record_test_result(t, pval, _("difference of means"));

    free(x);
    free(y);

    return 0;
}

/**
 * vars_test:
 * @list: gives the ID numbers of the variables to compare.
 * @Z: data matrix.
 * @pdinfo: data information struct.
 * @prn: gretl printing struct.
 *
 * Carries out test of the null hypothesis that the variances of two
 * variables are equal.
 *
 * Returns: 0 on successful completion, error code on error.
 */

int vars_test (const int *list, const double **Z, const DATAINFO *pdinfo, 
	       PRN *prn)
{
    double m, skew, kurt, s1, s2, var1, var2;
    double F, pval;
    double *x = NULL, *y = NULL;
    int dfn, dfd, n1, n2, n = pdinfo->n;

    if (list[0] < 2) return E_ARGS;

    if ((x = malloc(n * sizeof *x)) == NULL) return E_ALLOC;
    if ((y = malloc(n * sizeof *y)) == NULL) return E_ALLOC;

    n1 = ztox(list[1], x, Z, pdinfo);
    n2 = ztox(list[2], y, Z, pdinfo);

    if (n1 == 0 || n2 == 0) {
	pputs(prn, _("Sample range has no valid observations."));
	free(x); free(y);
	return 1;
    }

    if (n1 == 1 || n2 == 1) {
	pputs(prn, _("Sample range has only one observation."));
	free(x); free(y);
	return 1;
    }
    
    gretl_moments(0, n1-1, x, &m, &s1, &skew, &kurt, 1);
    gretl_moments(0, n2-1, y, &m, &s2, &skew, &kurt, 1);

    var1 = s1*s1;
    var2 = s2*s2;
    if (var1 > var2) { 
	F = var1/var2;
	dfn = n1 - 1;
	dfd = n2 - 1;
    } else {
	F = var2/var1;
	dfn = n2 - 1;
	dfd = n1 - 1;
    }

    pval = snedecor_cdf_comp(dfn, dfd, F);

    pputs(prn, _("\nEquality of variances test\n\n"));
    pprintf(prn, "   %s: ", pdinfo->varname[list[1]]);
    pprintf(prn, _("Number of observations = %d\n"), n1);
    pprintf(prn, "   %s: ", pdinfo->varname[list[2]]);
    pprintf(prn, _("Number of observations = %d\n"), n2);
    pprintf(prn, _("   Ratio of sample variances = %g\n"), F);
    pprintf(prn, "   %s: %s\n", _("Null hypothesis"), 
	    _("The two population variances are equal"));
    pprintf(prn, "   %s: F(%d,%d) = %g\n", _("Test statistic"), dfn, dfd, F);
    pprintf(prn, _("   p-value (two-tailed) = %g\n\n"), pval);
    if (snedecor_cdf_comp(dfn, dfd, F) > .10)
	pputs(prn, _("   The difference is not statistically significant.\n\n"));

    record_test_result(F, pval, _("difference of variances"));

    free(x);
    free(y);

    return 0;
}

struct MahalDist_ {
    int *list;
    int n;
    double *d;
};

const double *mahal_dist_get_distances (const MahalDist *md)
{
    return md->d;
}

int mahal_dist_get_n (const MahalDist *md)
{
    return md->n;
}

const int *mahal_dist_get_varlist(const MahalDist *md)
{
    return md->list;
}

void free_mahal_dist (MahalDist *md)
{
    free(md->list);
    free(md->d);
    free(md);
}

static MahalDist *mahal_dist_new (const int *list, int n)
{
    MahalDist *md = malloc(sizeof *md);
    
    if (md != NULL) {
	md->d = malloc(n * sizeof *md->d);
	if (md->d == NULL) {
	    free(md);
	    md = NULL;
	} else {
	    md->list = gretl_list_copy(list);
	    if (md->list == NULL) {
		free(md->d);
		free(md);
		md = NULL;
	    } else {
		md->n = n;
	    }
	}
    }

    if (md != NULL) {
	int t;

	for (t=0; t<n; t++) {
	    md->d[t] = NADBL;
	}
    }

    return md;
}

static int mdist_saver (double ***pZ, DATAINFO *pdinfo)
{
    int t, v, err;

    err = dataset_add_series(1, pZ, pdinfo);

    if (err) {
	v = 0;
    } else {
	v = pdinfo->v - 1;

	for (t=0; t<pdinfo->n; t++) {
	    (*pZ)[v][t] = NADBL;
	}

	strcpy(pdinfo->varname[v], "mdist");
	make_varname_unique(pdinfo->varname[v], v, pdinfo);

	strcpy(VARLABEL(pdinfo, v), _("Mahalanobis distances"));	
    }
		
    return v;
}

static int 
real_mahalanobis_distance (const int *list, double ***pZ,
			   DATAINFO *pdinfo, gretlopt opt,
			   MahalDist *md, PRN *prn)
{
    gretl_matrix *S = NULL;
    gretl_vector *means = NULL;
    gretl_vector *xdiff;
    int orig_t1 = pdinfo->t1;
    int orig_t2 = pdinfo->t2;
    int n, err = 0;

    varlist_adjust_sample(list, &pdinfo->t1, &pdinfo->t2, 
			  (const double **) *pZ);

    n = sample_size(pdinfo);
    if (n < 2) {
	pdinfo->t1 = orig_t1;
	pdinfo->t2 = orig_t2;
	return E_DATA;
    }

    xdiff = gretl_column_vector_alloc(list[0]);
    if (xdiff == NULL) {
	pdinfo->t1 = orig_t1;
	pdinfo->t2 = orig_t2;
	return E_ALLOC;
    }

    S = gretl_covariance_matrix_from_varlist(list, 
					     (const double **) *pZ, 
					     pdinfo, 
					     &means,
					     &err);

    if (!err) {
	if (opt & OPT_V) {
	    gretl_matrix_print_to_prn(S, _("Covariance matrix"), prn);
	}
	err = gretl_invert_symmetric_matrix(S);
	if (err) {
	    fprintf(stderr, "error inverting covariance matrix\n");
	} else if (opt & OPT_V) {
	    gretl_matrix_print_to_prn(S, _("Inverse of covariance matrix"), prn);
	}
    }

    if (!err) {
	int k = gretl_vector_get_length(means);
	char obs[OBSLEN];
	int miss, savevar = 0;
	double m, x, xbar;
	int i, t, vi;

	if (opt & OPT_S) {
	    /* save the results to a data series */
	    savevar = mdist_saver(pZ, pdinfo);
	}

	pprintf(prn, "%s\n", _("Mahalanobis distances from the centroid"));
	pprintf(prn, "%s\n", _("using the variables:"));
	for (i=1; i<=list[0]; i++) {
	    pprintf(prn, " %s\n", pdinfo->varname[list[i]]);
	}
	pputc(prn, '\n');

	for (t=pdinfo->t1; t<=pdinfo->t2; t++) {

	    miss = 0;

	    /* write vector of deviations from centroid for
	       observation t */

	    for (i=0; i<k; i++) {
		vi = list[i+1];
		xbar = gretl_vector_get(means, i);
		x = (*pZ)[vi][t];
		miss += na(x);
		if (!miss) {
		    gretl_vector_set(xdiff, i,  x - xbar);
		} 
	    }

	    m = miss ? NADBL : gretl_scalar_qform(xdiff, S, &err);

	    pprintf(prn, "%8s ", get_obs_string(obs, t, pdinfo));

	    if (err || miss) {
		pprintf(prn, "NA\n");
	    } else {
		m = sqrt(m);
		pprintf(prn, "%9.6f\n", m);
		if (savevar > 0) {
		    (*pZ)[savevar][t] = m;
		} else if (md != NULL) {
		    md->d[t] = m;
		}
	    }
	}

	if (savevar > 0) {
	    pputc(prn, '\n');
	    pprintf(prn, _("Distances saved as '%s'"), 
		    pdinfo->varname[savevar]);
	    pputc(prn, '\n');
	}

	pputc(prn, '\n');
    }

    gretl_matrix_free(xdiff);
    gretl_matrix_free(means);
    gretl_matrix_free(S);

    pdinfo->t1 = orig_t1;
    pdinfo->t2 = orig_t2;

    return err;
}

int mahalanobis_distance (const int *list, double ***pZ,
			  DATAINFO *pdinfo, gretlopt opt, 
			  PRN *prn)
{
    return real_mahalanobis_distance(list, pZ, pdinfo, opt, 
				     NULL, prn);
}

MahalDist *get_mahal_distances (const int *list, double ***pZ,
				DATAINFO *pdinfo, gretlopt opt,
				PRN *prn, int *err)
{
    MahalDist *md = mahal_dist_new(list, pdinfo->n);

    if (md == NULL) {
	*err = E_ALLOC;
    } else {
	*err = real_mahalanobis_distance(list, pZ, pdinfo, opt, 
					 md, prn);
	if (*err) {
	    free_mahal_dist(md);
	    md = NULL;
	}
    }

    return md;
}

/* 
   G = [(2 * \sum_i^n (i * x_i)) / (n * \sum_i^n x_i )] - [(n + 1)/n] 

   where x is sorted: x_i <= x_{i+1}
*/

static double gini_coeff (const double *x, int t1, int t2, double **plz,
			  int *pn, int *err)
{
    int m = t2 - t1 + 1;
    double *sx = NULL;
    double csx = 0.0, sumx = 0.0, sisx = 0.0;
    double G;
    int t, n = 0;

    gretl_error_clear();

    sx = malloc(m * sizeof *sx);

    if (sx == NULL) {
	*err = E_ALLOC;
	return NADBL;
    }

    n = 0;
    for (t=t1; t<=t2; t++) {
	if (na(x[t])) {
	    continue;
	} else if (x[t] < 0.0) {
	    gretl_errmsg_set(_("illegal negative value"));
	    *err = E_DATA;
	    break;
	} else {
	    sx[n++] = x[t];
	    sumx += x[t];
	}
    }

    if (*err) {
	free(sx);
	return NADBL;
    } else if (n == 0) {
	free(sx);
	*err = E_DATA;
	return NADBL;
    }

    qsort(sx, n, sizeof *sx, gretl_compare_doubles); 

#if 0
    if (1) {
	/* just testing alternative calculation for equivalence */
	double num, S[2];
	int s, fyi;
    
	num = S[0] = S[1] = 0.0;

	for (t=0; t<n; t++) {
	    fyi = 0;
	    s = t;
	    while (s < n && sx[s] == sx[t]) {
		fyi++;
		s++; 
	    }
	    S[1] += (double) fyi/n * sx[t];
	    num += (double) fyi/n * (S[0] + S[1]);
	    t = s - 1;
	    S[0] = S[1];
	}

	G = 1.0 - num / S[1];
	fprintf(stderr, "G = %g\n", G);
    }
#endif

    for (t=0; t<n; t++) {
	csx += sx[t];
	sisx += (t + 1) * sx[t];
	sx[t] = csx / sumx; /* sx now = Lorenz curve */
    }

    G = 2.0 * sisx / (n * sumx) - ((double) n + 1) / n;

    if (plz != NULL) {
	*plz = sx;
	*pn = n;
    } else {
	free(sx);
    }

    return G;
}

/**
 * gretl_gini:
 * @t1: starting observation.
 * @t2: ending observation.
 * @x: data series.
 *
 * Returns: the Gini coefficient for the series @x from obs
 * @t1 to obs @t2, skipping any missing values, or #NADBL 
 * on failure.
 */

double gretl_gini (int t1, int t2, const double *x)
{
    double g;
    int err = 0;

    g = gini_coeff(x, t1, t2, NULL, NULL, &err);

    if (err) {
	g = NADBL;
    }

    return g;
}

static int lorenz_graph (const char *vname, double *lz, int n)
{
    FILE *fp;
    double idx;
    int t, err = 0;

    if (gnuplot_init(PLOT_REGULAR, &fp)) {
	return E_FOPEN;
    }

    print_keypos_string(GP_KEY_LEFT_TOP, fp);

    fprintf(fp, "set title '%s'\n", vname);
    fprintf(fp, "plot \\\n"
	    "'-' using 1:2 title '%s' w lines, \\\n"
	    "'-' using 1:2 notitle w lines\n",
	    _("Lorenz curve"));

    gretl_push_c_numeric_locale();

    for (t=0; t<n; t++) {
	idx = t + 1;
	fprintf(fp, "%g %g\n", idx / n, lz[t]);
    }    
    fputs("e\n", fp);

    for (t=0; t<n; t++) {
	idx = ((double) t + 1) / n;
	fprintf(fp, "%g %g\n", idx, idx);
    }    
    fputs("e\n", fp);

    gretl_pop_c_numeric_locale();

    fclose(fp);

    err = gnuplot_make_graph();

    return err;
}

/**
 * gini:
 * @vnum: ID number of variable to examine.
 * @Z: data array
 * @pdinfo: data information struct.
 * @opt: not used yet.
 * @prn: gretl printing struct.
 *
 * Graphs the Lorenz curve for variable @vnum and prints the
 * Gini coefficient.
 *
 * Returns: 0 on successful completion, error code on error.
 */

int gini (int vnum, const double **Z, DATAINFO *pdinfo, 
	  gretlopt opt, PRN *prn)
{
    double *lz = NULL;
    double gini;
    int n, fulln;
    int err = 0;

    gini = gini_coeff(Z[vnum], pdinfo->t1, pdinfo->t2, 
		      &lz, &n, &err);

    if (err) {
	return err;
    }

    fulln = pdinfo->t2 - pdinfo->t1 - 1;
    pprintf(prn, "%s\n", pdinfo->varname[vnum], n);
    pprintf(prn, _("Number of observations = %d\n"), n);

    if (n < fulln) {
	pputs(prn, _("Warning: there were missing values\n"));
    } 

    pputc(prn, '\n');

    pprintf(prn, "%s = %g\n", _("Sample Gini coefficient"), gini);
    pprintf(prn, "%s = %g\n", _("Estimate of population value"), 
	    gini * (double) n / (n - 1));

    err = lorenz_graph(pdinfo->varname[vnum], lz, n);

    free(lz);

    return err;
}

/* Following: apparatus for Shapiro-Wilk normality test.
   Thanks to Marcin Blazejowski for the contribution.
*/

#ifndef min
# define min(a, b) ((a) > (b) ? (b) : (a))
#endif

static int sign (int a) 
{
    return (a == 0)? 0 : ((a < 0)? -1 : 1);
}

static int compare_floats (const void *a, const void *b) 
{
    const float *fa = (const float *) a;
    const float *fb = (const float *) b;

    return (*fa > *fb) - (*fa < *fb);
}

/* Algorithm AS 181.2 Appl. Statist. (1982) Vol. 31, No. 2
   Calculates the algebraic polynomial of order n-1 with
   array of coefficients cc.  Zero-order coefficient is 
   cc(1) = cc[0]
*/

static double poly (const float *cc, int n, float x) {
    double p, ret = cc[0]; /* preserve precision! */
    int j;
	
    if (n > 1) {
	p = x * cc[n-1];
	for (j=n-2; j>0; j--) {
	    p = (p + cc[j]) * x;
	}
	ret += p;
    }
	
    return ret;
}

/* Calculate coefficients for the Shapiro-Wilk test */

static void sw_coeff (int n, int n1, int n2, float *a) 
{
    const float c1[6] = { 0.f,.221157f,-.147981f,-2.07119f, 4.434685f, -2.706056f };
    const float c2[6] = { 0.f,.042981f,-.293762f,-1.752461f,5.682633f, -3.582633f };
    float summ2, ssumm2;
    float a0, a1, an = (float) n;
    float fac, an25, rsn;
    int i, i1, nn2 = n / 2;
	
    if (n == 3) {
	a[0] = sqrt(0.5);
    } else {
	an25 = an + .25;
	summ2 = 0.0;
	for (i=0; i<n2; i++) {
	    a[i] = (float) normal_cdf_inverse((i + 1 - .375f) / an25);
	    summ2 += a[i] * a[i];
	}
	summ2 *= 2.0;
	ssumm2 = sqrt(summ2);
	rsn = 1.0 / sqrt(an);
	a0 = poly(c1, 6, rsn) - a[0] / ssumm2;

	/* Normalize array a */
	if (n > 5) {
	    i1 = 2;
	    a1 = -a[1] / ssumm2 + poly(c2, 6, rsn);
	    fac = sqrt((summ2 - 2.0 * (a[0] * a[0]) - 2.0 * (a[1] * a[1])) /
		       (1.0 - 2.0 * (a0 * a0) - 2.0 * (a1 * a1)));
	    a[1] = a1;
	} else {
	    i1 = 1;
	    fac = sqrt((summ2 - 2.0 * (a[0] * a[0])) / (1.0 - 2.0 * (a0 * a0)));
	}
	a[0] = a0;
	for (i=i1; i<nn2; i++) {
	    a[i] /= -fac;
	}
    }
}

/* ALGORITHM AS R94 APPL. STATIST. (1995) vol. 44, no. 4, 547-551.
   Calculates the Shapiro-Wilk W test and its significance level,
   Rendered into C by Marcin Blazejowski, May 2008.
*/

static int sw_w (float *x, int n, int n1, int n2, float *a, 
		 double *W, double *pval)
{
    const float z90 = 1.2816f;
    const float z95 = 1.6449f;
    const float z99 = 2.3263f;
    const float zm = 1.7509f;
    const float zss = .56268f;
    const float bf1 = .8378f;
    const double xx90 = .556;
    const double xx95 = .622;
    const float little = 1e-19f; /* "small" is reserved on win32 */
    const float pi6 = 1.909859f;
    const float stqr = 1.047198f;
	
    /* polynomial coefficients */
    const float  g[2] = { -2.273f, .459f };
    const float c3[4] = { .544f, -.39978f, .025054f, -6.714e-4f };
    const float c4[4] = { 1.3822f, -.77857f, .062767f, -.0020322f };
    const float c5[4] = { -1.5861f, -.31082f, -.083751f, .0038915f };
    const float c6[3] = { -.4803f, -.082676f, .0030302f };
    const float c7[2] = { .164f, .533f };
    const float c8[2] = { .1736f, .315f };
    const float c9[2] = { .256f, -.00635f };
	
    float r1, zbar, ssassx, gamma, range;
    float bf, ld, m, s, sa, xi, sx, xx, y, w1;
    float asa, ssa, z90f, sax, zfm, z95f, zsd, z99f, ssx, xsx;
    float an = (float) n;
    int i, j, ncens = n - n1;
	
    /* Check for zero range */
    range = x[n1 - 1] - x[0];
    if (range < little) {
	fprintf(stderr, "sw_w: range is too small\n");
	return 1;
    }

    /* Check for correct sort order on range-scaled X */
    /* In fact we don't need do this, since we use qsort() from libc 
       and we can suppose, that qsort() can sort... */
    xx = x[0] / range;
    sx = xx;
    sa = -a[0];
    j = n - 1;
    for (i = 1; i < n1; --j) {
	xi = x[i] / range;
	sx += xi;
	++i;
	if (i != j) {
	    sa += sign(i - j) * a[min(i,j) - 1];
	}
	xx = xi;
    }
	
    /* Calculate W statistic as squared correlation between data and 
       coefficients */
    sa /= n1;
    sx /= n1;
    ssa = ssx = sax = 0.0;
    j = n - 1;
    for (i=0; i<n1; i++, j--) {
	if (i != j) {
	    asa = sign(i - j) * a[min(i,j)] - sa;
	} else {
	    asa = -sa;
	}
	xsx = x[i] / range - sx;
	ssa += asa * asa;
	ssx += xsx * xsx;
	sax += asa * xsx;
    }

    /* W1 equals (1-W) calculated to avoid excessive rounding error
       for W very near 1 (a potential problem in very large samples) 
    */
    ssassx = sqrt(ssa * ssx);
    w1 = (ssassx - sax) * (ssassx + sax) / (ssa * ssx);
    *W = 1 - w1;
	
    /* Calculate significance level for W */
    if (n == 3) { 
	/* exact P value */
	*pval = pi6 * (asin(sqrt(*W)) - stqr);
	return 0;
    }

    y = log(w1);
    xx = log(an);

    if (n <= 11) {
	gamma = poly(g, 2, an);
	if (y >= gamma) {
	    /* FIXME: rather use an even smaller value, or NA? */
	    *pval = little;
	    return 0;
	}
	y = -log(gamma - y);
	m = poly(c3, 4, an);
	s = exp(poly(c4, 4, an));
    } else { 
	/* n >= 12 */
	m = poly(c5, 4, xx);
	s = exp(poly(c6, 3, xx));
    }
	
    if (ncens > 0) { 
	/* Censoring by proportion ncens/n.
	   Calculate mean and sd of normal equivalent deviate of W */
	float delta = (float) ncens / an;

	ld = -log(delta);
	bf = 1.0 + xx * bf1;
	r1 = pow(xx90, (double) xx);
	z90f = z90 + bf * pow(poly(c7, 2, r1), (double) ld);
	r1 = pow(xx95, (double) xx);
	z95f = z95 + bf * pow(poly(c8, 2, r1), (double) ld);
	z99f = z99 + bf * pow(poly(c9, 2, xx), (double) ld);

	/* Regress Z90F,...,Z99F on normal deviates Z90,...,Z99 to get
	   pseudo-mean and pseudo-sd of z as the slope and intercept */
	zfm = (z90f + z95f + z99f) / 3.0;
	zsd = (z90 * (z90f - zfm) + z95 * (z95f - zfm) + z99 * (z99f - zfm)) / zss;
	zbar = zfm - zsd * zm;
	m += zbar * s;
	s *= zsd;
    }
	
    *pval = normal_cdf_comp(((double) y - (double) m) / (double) s);
	
    return 0;
}

static int sw_sample_check (int n, int n1)
{
    int ncens = n - n1;
    float delta, an = n;

    if (n < 3 || n1 < 3) {
	fprintf(stderr, "There is no way to run SW test for less then 3 obs\n");
	return E_DATA;
    }

    if (ncens < 0 || (ncens > 0 && n < 20)) {
	fprintf(stderr, "sw_w: not enough uncensored obserations\n");
	return E_DATA;
    }
	
    delta = (float) ncens / an;
    if (delta > .8f) {
	fprintf(stderr, "sw_w: too many censored obserations\n");
	return E_DATA;
    }

    return 0;
}

/**
 * shapiro_wilk:
 * @x: data array.
 * @t1: starting observation.
 * @t2: ending observation.
 * @W: location to receive test statistic.
 * @pval: location to receive p-value.
 * 
 * Returns: 0 on success, non-zero on failure.
*/

int shapiro_wilk (const double *x, int t1, int t2, double *W, double *pval) 
{
    int n1 = 0;         /* number of uncensored obs: we may make use of this later */
    float *a = NULL;	/* array of coefficients */
    float *xf = NULL;   /* copy of x in float format */
    int i, t, n2, n = 0;
    int err = 0;

    *W = *pval = NADBL;

    for (t=t1; t<=t2; t++) {
	if (!na(x[t])) n++;
    }

    /* for now we assume all obs are uncensored */
    n1 = n;

    err = sw_sample_check(n, n1);
    if (err) {
	return err;
    }
	
    /* How many coeffs should be computed? */
    n2 = fmod(n, 2.0);
    n2 = (n2 == 0)?  n / 2 : (n - 1) / 2;

    xf = malloc(n * sizeof *xf);
    a = malloc(n2 * sizeof *a);

    if (xf == NULL || a == NULL) {
	err = E_ALLOC;
    } else {
	i = 0;
	for (t=t1; t<=t2; t++) {
	    if (!na(x[t])) {
		xf[i++] = x[t];
	    }
	}
	qsort(xf, n, sizeof *xf, compare_floats);
	/* Main job: compute W stat and its p-value */
	sw_coeff(n, n1, n2, a);
	err = sw_w(xf, n, n1, n2, a, W, pval);
    } 
	
    free(a);
    free(xf);
	
    return err;
}

static double round2 (double x)
{
    x *= 100.0;
    x = (x - floor(x) < 0.5)? floor(x) : ceil(x);
    return x / 100;
}

/* Polynomial approximation to the p-value for the Lilliefors test,
   said to be good to 2 decimal places.  See Abdi and Molin,
   "Lilliefors/Van Soest's test of normality", in Salkind (ed.)
   Encyclopedia of Measurement and Statistics, Sage, 2007; also
   http://www.utd.edu/~herve/Abdi-Lillie2007-pretty.pdf
 */

static double lilliefors_pval (double L, int N)
{
    double b0 = 0.37872256037043;
    double b1 = 1.30748185078790;
    double b2 = 0.08861783849346;
    double b1pN = b1 + N;
    double Lm2 = 1.0 / (L * L);
    double A, pv;

    A = (-b1pN + sqrt(b1pN * b1pN - 4 * b2 * (b0 - Lm2))) / (2 * b2);

    pv = -0.37782822932809 + 1.67819837908004*A 
	- 3.02959249450445*A*A + 2.80015798142101*pow(A, 3.) 
	- 1.39874347510845*pow(A, 4.) + 0.40466213484419*pow(A, 5.) 
	- 0.06353440854207*pow(A, 6.) + 0.00287462087623*pow(A, 7.) 
	+ 0.00069650013110*pow(A, 8.) - 0.00011872227037*pow(A, 9.)
	+ 0.00000575586834*pow(A, 10.);

    if (pv < 0) {
	pv = 0;
    } else if (pv > 1) {
	pv = 1;
    } else {
	/* round to 2 digits */
	pv = round2(pv);
    }

    return pv;
}

static int lilliefors_test (const double *x, int t1, int t2, 
			    double *L, double *pval)
{
    double *zx;   /* copy of x for sorting, z-scoring */
    int i, t, n = 0;
    int err = 0;

    *L = *pval = NADBL;

    for (t=t1; t<=t2; t++) {
	if (!na(x[t])) n++;
    }

    if (n < 5) {
	/* we need more than 4 data points */
	return E_DATA;
    }

    zx = malloc(n * sizeof *zx);

    if (zx == NULL) {
	err = E_ALLOC;
    } else {
	double dx, mx = 0.0, sx = 0.0;
	double Phi, Dp = 0.0, Dm = 0.0;
	double Dmax = 0.0;

	/* compute sample mean and standard deviation plus
	   sorted z-scores */

	i = 0;
	for (t=t1; t<=t2; t++) {
	    if (!na(x[t])) {
		zx[i++] = x[t];
		mx += x[t];
	    }
	}
	mx /= n;

	for (t=t1; t<=t2; t++) {
	    if (!na(x[t])) {
		dx = x[t] - mx;
		sx += dx * dx;
	    }
	}
	sx = sqrt(sx / (n - 1));

	qsort(zx, n, sizeof *zx, gretl_compare_doubles);

	for (i=0; i<n; i++) {
	    zx[i] = (zx[i] - mx) / sx;
	}

	/* The following is based on the formula given in the
	   "nortest" package for GNU R, function lillie.test (written
	   by Juergen Gross).  It differs from the algorithm given by
	   Abdi and Molin (whose p-value approximation is used above).
	   The A & M account of the statistic itself seems to be
	   wrong: it consistently over-rejects (i.e. around 8 percent
	   of the time for a nominal 5 percent significance level).
	*/

	for (i=0; i<n; i++) {
	    Phi = normal_cdf(zx[i]);
	    Dp = (double) (i+1) / n - Phi;
	    Dm = Phi - (double) i / n;
	    if (Dp > Dmax) Dmax = Dp;
	    if (Dm > Dmax) Dmax = Dm;
	}

	*L = Dmax;
	*pval = lilliefors_pval(Dmax, n);
    } 
	
    free(zx);
	
    return err;
}

static int skew_kurt_test (const double *x, int t1, int t2, 
			   double *test, double *pval,
			   gretlopt opt)
{
    double skew, xkurt;
    int n, err = 0;

    err = series_get_moments(t1, t2, x, &skew, &xkurt, &n);

    if (!err) {
	if (opt & OPT_J) {
	    /* Jarque-Bera */
	    *test = (n / 6.0) * (skew * skew + xkurt * xkurt/ 4.0);
	} else {
	    /* Doornik-Hansen */
	    *test = doornik_chisq(skew, xkurt, n);
	}
    }

    if (!err && xna(*test)) {
	*test = NADBL;
	err = E_NAN;
    }

    if (!na(*test)) {
	*pval = chisq_cdf_comp(2, *test);
    }

    return err;
}

static void print_normality_stat (double test, double pval,
				  gretlopt opt, PRN *prn)
{
    const char *tstrs[] = {
	N_("Shapiro-Wilk W"),
	N_("Jarque-Bera test"),
	N_("Lilliefors test"),
	N_("Doornik-Hansen test")
    };
    int i = (opt & OPT_W)? 0 : (opt & OPT_J)? 1 :
	(opt & OPT_L)? 2 : 3;

    if (na(test) || na(pval)) {
	return;
    }

    if (opt & OPT_L) {
	pprintf(prn, " %s = %g, %s ~= %g\n\n", 
		tstrs[i], test, _("with p-value"), pval);
    } else {
	pprintf(prn, " %s = %g, %s %g\n\n", 
		tstrs[i], test, _("with p-value"), pval);
    }
}

/* OPT_A: all tests
   OPT_D: Doornik-Hansen
   OPT_W: Shapiro-Wilk
   OPT_D: Jarque-Bera
   OPT_W: Lilliefors
   OPT_Q: quiet
*/

int gretl_normality_test (const char *param,
			  const double **Z,
			  const DATAINFO *pdinfo,
			  gretlopt opt,
			  PRN *prn)
{
    gretlopt alltests = OPT_D | OPT_W | OPT_J | OPT_L;
    double test = NADBL;
    double pval = NADBL;
    double trec = NADBL;
    double pvrec = NADBL;
    int v, err;

    err = incompatible_options(opt, OPT_A | alltests);

    if (!err) {
	v = current_series_index(pdinfo, param);
	if (v < 0) {
	    err = E_UNKVAR;
	} 
    }

    if (err) {
	return err;
    }

    if (opt & OPT_A) {
	/* show all tests */
	opt |= alltests;
    }

    if (!(opt & alltests)) {
	/* no method selected: use Doornik-Hansen */
	opt |= OPT_D;
    }

    if (!(opt & OPT_Q)) {
	pprintf(prn, _("Test for normality of %s:"), param);
	if (opt & OPT_A) {
	    pputs(prn, "\n\n");
	} else {
	    pputc(prn, '\n');
	}
    }

    if (opt & OPT_D) {
	err = skew_kurt_test(Z[v], pdinfo->t1, pdinfo->t2, 
			     &test, &pval, OPT_D);
	if (!err && !(opt & OPT_Q)) {
	    print_normality_stat(test, pval, OPT_D, prn);
	}
	if (!err) {
	    /* record Doornik-Hansen result by default */
	    trec = test;
	    pvrec = pval;
	}
    }    

    if (opt & OPT_W) {
	err = shapiro_wilk(Z[v], pdinfo->t1, pdinfo->t2, 
			   &test, &pval);
	if (!err && !(opt & OPT_Q)) {
	    print_normality_stat(test, pval, OPT_W, prn);
	}
    } 

    if (opt & OPT_L) {
	err = lilliefors_test(Z[v], pdinfo->t1, pdinfo->t2, 
			      &test, &pval);
	if (!err && !(opt & OPT_Q)) {
	    print_normality_stat(test, pval, OPT_L, prn);
	}
    } 

    if (opt & OPT_J) {
	err = skew_kurt_test(Z[v], pdinfo->t1, pdinfo->t2, 
			     &test, &pval, OPT_J);
	if (!err && !(opt & OPT_Q)) {
	    print_normality_stat(test, pval, OPT_J, prn);
	}
    }

    if (na(trec) && !na(test)) {
	trec = test;
    }

    if (na(pvrec) && !na(pval)) {
	pvrec = pval;
    }

    if (!na(trec) && !na(pvrec)) {
	record_test_result(trec, pvrec, "Normality");
    }
    
    return err;
}
