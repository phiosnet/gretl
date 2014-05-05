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

/*  gretl_normal.c - "advanced" routines relating to the normal
    distribution
*/  

#include "libgretl.h"
#include "libset.h"
#include "../../cephes/libprob.h"

#if defined(_OPENMP) && !defined(OS_OSX)
# include <omp.h>
#endif

/**
 * invmills:
 * @x: double-precision value.
 *
 * Adapted by putting together code from gsl and TDA (Univ. Bochum). 
 * The latter is, in turn, based on 
 * A. V. Swan, The Reciprocal of Mills's Ratio, Algorithm AS 17,
 * Journal of the Royal Statistical Society. Series C (Applied Statistics), 
 * Vol. 18, No. 1 (1969), 115-116.
 *
 * Returns: the inverse Mills ratio, that is the ratio between the
 * normal density function and the complement of the distribution 
 * function, both evaluated at @x.
 */

#define SQRT_HALF_PI 1.2533141373155002512078826424
#define MILLS_BOTTOM -22.9
#define MILLS_TOP 25
#define MILLS_EPS 1.0e-09

double invmills (double x)
{
    double a, a0, a1, a2;
    double b, b0, b1, b2;
    double r, s, t, d;
    double ret;

    if (x == 0.0) {
        return 1.0 / SQRT_HALF_PI;
    }

    if (x < MILLS_BOTTOM) {
	return 0;
    }

    if (x > MILLS_TOP) {
	a0 = 1.0/(x * x);
	a1 = 1.0 - 9.0 * a0 * (1.0 - 11.0 * a0);
	a2 = 1.0 - 5.0 * a0 * (1.0 - 7.0 * a0 * a1);
	d = 1.0 - a0 * (1.0 - 3.0 * a0 * a2);
	return x / d;
    }

    d = (x < 0.0)? -1.0 : 1.0;
    x = fabs(x);

    if (x <= 2.0) {
	s = 0.0;
	a = 1.0;
	r = t = x;
	b = x * x;
	while (fabs(s-t) > MILLS_EPS) {
	    a += 2.0;
	    s = t;
	    r *= b / a;
	    t += r;
	}
	ret = 1.0 / (SQRT_HALF_PI * exp(0.5 * b) - d * t);
    } else {
	a = 2.0;
	r = s = b1 = x;
	a1 = x * x + 1.0;
	a2 = x * (a1 + 2.0);
	b2 = a1 + 1.0;
	t  = a2 / b2;
	while (fabs(r-t) > MILLS_EPS && fabs(s-t) > MILLS_EPS) {
	    a += 1.0;
	    a0 = a1;
	    a1 = a2;
	    a2 = x * a1 + a * a0;
	    b0 = b1;
	    b1 = b2;
	    b2 = x * b1 + a * b0;
	    r  = s;
	    s  = t;
	    t  = a2 / b2;
	}
	ret = t;
	if (d < 0.0) {
	    ret /= (2.0 * SQRT_HALF_PI * exp(0.5 * x * x) * t - 1.0);
	}
    }

    return ret;
}

#define GENZ_BVN 1

#if GENZ_BVN

/**
 * genz04:
 * @rho: correlation coefficient.
 * @limx: abscissa value, first Gaussian r.v.
 * @limy: abscissa value, second Gaussian r.v.
 *
 * Based on FORTRAN code by Alan Genz, with minor adaptations.
 * Original source at 
 * http://www.math.wsu.edu/faculty/genz/software/fort77/tvpack.f
 * No apparent license.
 *
 * The algorithm is from Drezner and Wesolowsky (1989), 'On the
 * Computation of the Bivariate Normal Integral', Journal of
 * Statist. Comput. Simul. 35 pp. 101-107, with major modifications
 * for double precision, and for |R| close to 1.
 *
 * Returns: for (x, y) a bivariate standard Normal rv with correlation
 * coefficient @rho, the joint probability that (x < @limx) and (y < @limy),
 * or #NADBL on failure.
 */

static double genz04 (double rho, double limx, double limy)
{
    double w[10], x[10];
    double absrho = fabs(rho);
    double h, k, hk, bvn, hs, asr;
    double a, b, as, d1, bs, c, d, tmp;
    double sn, xs, rs;
    int i, lg, j;

    if (absrho < 0.3) {

	w[0] = .1713244923791705;
	w[1] = .3607615730481384;
	w[2] = .4679139345726904;

	x[0] = -.9324695142031522;
	x[1] = -.6612093864662647;
	x[2] = -.238619186083197;

	lg = 3;
    } else if (absrho < 0.75) {

	w[0] = .04717533638651177;
	w[1] = .1069393259953183;
	w[2] = .1600783285433464;
	w[3] = .2031674267230659;
	w[4] = .2334925365383547;
	w[5] = .2491470458134029;

	x[0] = -.9815606342467191;
	x[1] = -.904117256370475;
	x[2] = -.769902674194305;
	x[3] = -.5873179542866171;
	x[4] = -.3678314989981802;
	x[5] = -.1252334085114692;

	lg = 6;
    } else {

	w[0] = .01761400713915212;
	w[1] = .04060142980038694;
	w[2] = .06267204833410906;
	w[3] = .08327674157670475;
	w[4] = .1019301198172404;
	w[5] = .1181945319615184;
	w[6] = .1316886384491766;
	w[7] = .1420961093183821;
	w[8] = .1491729864726037;
	w[9] = .1527533871307259;

	x[0] = -.9931285991850949;
	x[1] = -.9639719272779138;
	x[2] = -.9122344282513259;
	x[3] = -.8391169718222188;
	x[4] = -.7463319064601508;
	x[5] = -.636053680726515;
	x[6] = -.5108670019508271;
	x[7] = -.3737060887154196;
	x[8] = -.2277858511416451;
	x[9] = -.07652652113349733;

	lg = 10;
    }

    h = -limx;
    k = -limy;
    hk = h * k;
    bvn = 0.0;

    if (absrho < 0.925) {
	hs = (h * h + k * k) / 2;
	asr = asin(rho);
	for (i=0; i<lg; i++) {
	    for (j=0; j<=1; j++) {
		sn = sin(asr * (1 + (2*j-1)*x[i]) / 2);
		bvn += w[i] * exp((sn * hk - hs) / (1 - sn * sn));
	    }
	}
	bvn = bvn * asr / (2 * M_2PI); 

	d1 = -h;
	bvn += normal_cdf(d1) * normal_cdf(-k);
    } else {
	if (rho < 0.0) {
	    k = -k;
	    hk = -hk;
	}

	as = (1 - rho) * (1 + rho);
	a = sqrt(as);
	bs = (h - k) * (h - k);
	c = (4 - hk) / 8;
	d = (12 - hk) / 16;
	asr = -(bs / as + hk) / 2;
	if (asr > -100.0) {
	    bvn = a * exp(asr) * (1 - c * (bs - as) * (1 - d * bs / 5)
				  / 3 + c * d * as * as / 5);
	}

	if (-hk < 100.0) {
	    b = sqrt(bs);
	    d1 = -b / a;
	    /*
	      Note: the condition below was not in the original
	      FORTRAN code this was ripped off from. Without it, there
	      are a few problems for rho very near -1; the version
	      below seems to work ok. Could it be a problem with
	      normal_cdf in the left tail?
	    */
	    if (d1 > -12.0) {
		bvn -= exp(-hk / 2) * SQRT_2_PI * normal_cdf(d1) * b * 
		    (1 - c * bs * (1 - d * bs / 5) / 3);
	    }
	}

	a /= 2;

	for (i=0; i<lg; i++) {
	    for (j=0; j<=1; j++) {
		d1 = a * (1 + (2*j-1)*x[i]);
		xs = d1 * d1;
		rs = sqrt(1 - xs);
		asr = -(bs / xs + hk) / 2;
		if (asr > -100.0) {
		    tmp = exp(-hk * (1 - rs) / ((rs + 1) * 2)) / 
			rs - (c * xs * (d * xs + 1) + 1);
		    bvn += a * w[i] * exp(asr) * tmp;
		}
	    }
	}
	
	bvn = -bvn / M_2PI;

	if (rho > 0.0) {
	    bvn += normal_cdf((h > k) ? -h : -k);
	} else {
	    bvn = -bvn;
	    if (k > h) {
		bvn = bvn + normal_cdf(k) - normal_cdf(h);
	    }
	}
    }

    /* sanity check */
    return (bvn < 0) ? 0 : bvn;
}

#else

/**
 * drezner78:
 * @rho: correlation coefficient.
 * @a: abscissa value, first Gaussian r.v.
 * @b: abscissa value, second Gaussian r.v.
 *
 * Ripped and adapted from Gnumeric, with a bug corrected for the case
 * (a * b < 0) && (rho < 0).
 *
 * The algorithm is from Drezner (1978), 'Computation of the Bivariate
 * Normal Integral', Mathematics of Computation, volume 32, number 141.
 *
 * Returns: for (x, y) a bivariate standard Normal rv with correlation
 * coefficient @rho, the joint probability that (x < @a) and (y < @b), or
 * #NADBL on failure.
 */

static double drezner78 (double rho, double a, double b)
{
    static const double x[] = {0.24840615, 0.39233107, 0.21141819, 
			       0.03324666, 0.00082485334};
    static const double y[] = {0.10024215, 0.48281397, 1.0609498, 
			       1.7797294, 2.6697604};
    double ret = NADBL;
    double a1, b1, den;
    int i, j;

    den = sqrt(2.0 * (1 - rho * rho));

    a1 = a / den;
    b1 = b / den;

    if (a <= 0 && b <= 0 && rho < 0) {
	/* standard case */
	double sum = 0.0;

	for (i=0; i<5; i++) {
	    for (j=0; j<5; j++) {
		sum += x[i] * x[j] * 
		    exp (a1 * (2 * y[i] - a1) + 
			 b1 * (2 * y[j] - b1) + 
			 2 * rho * (y[i] - a1) * (y[j] - b1));
	    }
	}
	ret = (sqrt(1 - (rho * rho)) / M_PI * sum);
    } else if (a <= 0 && b >= 0 && rho > 0) {
	ret = normal_cdf(a) - bvnorm_cdf(-rho, a, -b);
    } else if (a >= 0 && b <= 0 && rho > 0) {
	ret = normal_cdf(b) - bvnorm_cdf(-rho, -a, b);
    } else if (a >= 0 && b >= 0 && rho < 0) {
	ret = normal_cdf(a) + normal_cdf(b) - 1 + bvnorm_cdf(rho, -a, -b);
    } else if ((a * b * rho) > 0) {
	int sgna = (a < 0)? -1 : 1;
	int sgnb = (b < 0)? -1 : 1;
	double rho1, rho2, tmp, delta;

	tmp = sqrt((a * a) - 2 * rho * a * b + (b * b));
	rho1 = (rho * a - b) * sgna / tmp;
	rho2 = (rho * b - a) * sgnb / tmp;
	delta = (sgna * sgnb && (rho > 0))? 0 : 0.5;

	ret = (bvnorm_cdf(rho1, a, 0) + bvnorm_cdf(rho2, b, 0) - delta);
    }    

    return ret;
}

#endif /* bvnorm variants */

/**
 * bvnorm_cdf:
 * @rho: correlation coefficient.
 * @a: abscissa value, first Gaussian r.v.
 * @b: abscissa value, second Gaussian r.v.
 *
 * Returns: for (x, y) a bivariate standard Normal rv with correlation
 * coefficient @rho, the joint probability that (x < @a) and (y < @b), or
 * #NADBL on failure.
 */

double bvnorm_cdf (double rho, double a, double b)
{
    if (fabs(rho) > 1) {
	return NADBL;
    }	

    if (rho == 0.0) {
	/* joint prob is just the product of the marginals */
	return normal_cdf(a) * normal_cdf(b);
    }

    if (rho == 1.0) {
	/* the two variables are in fact the same */
	return normal_cdf(a < b ? a : b);
    }
    
    if (rho == -1.0) {
	/* the two variables are perfectly negatively correlated: 
	   P(x<a, y<b) = P((x<a) && (x>b)) = P(x \in (b,a))
	*/
	return (a <= b) ? 0 : normal_cdf(a) - normal_cdf(b);
    }

#if GENZ_BVN
    return genz04(rho, a, b);
#else 
    return drezner78(rho, a, b);
#endif
}

/*
  C  Lower triangular Cholesky factor of \Sigma, m x m
  A  Lower bound of rectangle, m x 1
  B  Upper bound of rectangle, m x 1
  U  Random variates, m x r
*/

static double GHK_1 (const gretl_matrix *C, 
		     const gretl_matrix *A, 
		     const gretl_matrix *B, 
		     const gretl_matrix *U,
		     gretl_matrix *TA,
		     gretl_matrix *TB,
		     gretl_matrix *WGT,
		     gretl_matrix *TT,
		     double huge)
{
    int m = C->rows; /* Dimension of the multivariate normal */
    int r = U->cols; /* Number of repetitions */
    double P, den = gretl_matrix_get(C, 0, 0);
    double ui, x, z, cjk, tki;
    int i, j, k;

    z = A->val[0];
    TA->val[0] = (z == -huge) ? 0 : ndtr(z / den);
    z = B->val[0];
    TB->val[0] = (z == huge) ? 1 : ndtr(z / den);
    
    for (i=1; i<r; i++) {
	TA->val[i] = TA->val[0];
	TB->val[i] = TB->val[0];
    }

    /* form WGT = TB - TA */
    gretl_matrix_copy_values(WGT, TB);
    gretl_matrix_subtract_from(WGT, TA);
    gretl_matrix_zero(TT);

    for (i=0; i<r; i++) {
	ui = gretl_matrix_get(U, 0, i);
	x = TB->val[i] - ui * (TB->val[i] - TA->val[i]);
	gretl_matrix_set(TT, 0, i, ndtri(x));
    }

    for (j=1; j<m; j++) {
	den = gretl_matrix_get(C, j, j);

	for (i=0; i<r; i++) {
	    if (WGT->val[i] == 0) {
		/* If WGT[i] ever comes to be zero, it cannot in
		   principle be modified by the code below; in fact,
		   however, running through the computations
		   regardless may produce a NaN (since 0 * NaN = NaN).
		   This becomes more likely for large dimension @m.
		*/
		continue;
	    }
	    x = 0.0;
	    for (k=0; k<j; k++) {
		cjk = gretl_matrix_get(C, j, k);
		tki = gretl_matrix_get(TT, k, i);
		x += cjk * tki;
	    }

	    if (A->val[j] == -huge) {
		TA->val[i] = 0.0;
	    } else {
		TA->val[i] = ndtr((A->val[j] - x) / den);
	    }

	    if (B->val[j] == huge) {
		TB->val[i] = 1.0;
	    } else {
		TB->val[i] = ndtr((B->val[j] - x) / den);
	    }

	    /* component j draw */
	    ui = gretl_matrix_get(U, j, i);
	    x = TB->val[i] - ui * (TB->val[i] - TA->val[i]);
	    gretl_matrix_set(TT, j, i, ndtri(x));
	}

	/* accumulate weight */
	gretl_matrix_subtract_from(TB, TA);
	for (i=0; i<r; i++) {
	    WGT->val[i] *= TB->val[i];
	}
    }

    P = 0.0;
    for (i=0; i<r; i++) {
	P += WGT->val[i];
    }
    P /= r;

    if (P < 0.0 || P > 1.0) {
	fprintf(stderr, "*** ghk error: P = %g\n", P);
	P = 0.0/0.0; /* force a NaN */
    }	

    return P;
}

/**
 * gretl_GHK:
 * @C: Cholesky decomposition of covariance matrix, lower triangular,
 * m x m.
 * @A: Lower bounds, n x m; in case a lower bound is minus infinity
 * this should be represented as -1.0E10.
 * @B: Upper bounds, n x m; in case an upper bound is plus infinity
 * this should be represented as +1.0E10.
 * @U: Uniform random matrix, m x r.
 *
 * Computes the GHK (Geweke, Hajivassiliou, Keane) approximation to 
 * the multivariate normal distribution function for n observations 
 * on m variates, using r draws. 
 *
 * Returns: an n x 1 vector of probabilities.
 */

/* Note: using openmp on this function requires thread-local
   storage of static variables in gretl_matrix.c, and at
   present we can't arrange that on OS X */

#if defined(_OPENMP) && !defined(OS_OSX)

gretl_matrix *gretl_GHK (const gretl_matrix *C,
			 const gretl_matrix *A,
			 const gretl_matrix *B,
			 const gretl_matrix *U,
			 int *err)
{
    gretl_matrix_block *Bk = NULL;
    gretl_matrix *P = NULL;
    gretl_matrix *Ai, *Bi;
    gretl_matrix *TA, *TB;
    gretl_matrix *WT, *TT;
    double huge;
    int dim, nobs, ndraws;
    int ierr, ghk_err = 0;
    int ABok, pzero;
    int i, j;

    if (gretl_is_null_matrix(C) ||
	gretl_is_null_matrix(A) ||
	gretl_is_null_matrix(B) ||
	gretl_is_null_matrix(U)) {
	*err = E_DATA;
	return NULL;
    }

    if (A->rows != B->rows ||
	A->cols != B->cols ||
	C->rows != A->cols ||
	C->cols != A->cols ||
	U->rows != A->cols) {
	*err = E_NONCONF;
	return NULL;
    }

    huge = libset_get_double(CONV_HUGE);

    dim = C->rows;
    nobs = A->rows;
    ndraws = U->cols;

    P = gretl_matrix_alloc(nobs, 1);
    if (P == NULL) {
	*err = E_ALLOC;
	return NULL;
    }

    set_cephes_hush(1);

#pragma omp parallel if (nobs>256) private(i,j,Bk,Ai,Bi,TA,TB,WT,TT,ABok,pzero,ierr)
    {
	Bk = gretl_matrix_block_new(&Ai, dim, 1,
				    &Bi, dim, 1,
				    &TA, 1, ndraws,
				    &TB, 1, ndraws,
				    &WT, 1, ndraws,
				    &TT, dim, ndraws,
				    NULL);
	if (Bk == NULL) {
	    ierr = E_ALLOC;
	    goto omp_end;
	} else {
	    ierr = 0;
	}

        #pragma omp for
	for (i=0; i<nobs; i++) {
	    ABok = 1; pzero = 0;

	    for (j=0; j<dim && !ierr; j++) {
		Ai->val[j] = gretl_matrix_get(A, i, j);
		Bi->val[j] = gretl_matrix_get(B, i, j);
		ABok = !(isnan(Ai->val[j]) || isnan(Bi->val[j]));

		if (!ABok) {
		    /* If there are any NaNs in A or B, there's no
		       point in continuing
		    */
		    P->val[i] = 0.0/0.0; /* NaN */
		    break;
		} else if (Bi->val[j] < Ai->val[j]) {
		    gretl_errmsg_sprintf("ghk: inconsistent bounds: B[%d,%d] < A[%d,%d]",
					 i+1, j+1, i+1, j+1);
		    ierr = E_DATA;
		} else if (Bi->val[j] == Ai->val[j]) {
		    P->val[i] = 0.0;
		    pzero = 1;
		    break;
		}
	    }
	    if (!ierr && !pzero && ABok) {
		P->val[i] = GHK_1(C, Ai, Bi, U, TA, TB, WT, TT, huge);
	    }
	}

    omp_end:

	if (ierr) {
	    ghk_err = ierr;
	}

	gretl_matrix_block_destroy(Bk);
    }

    set_cephes_hush(0);

    if (ghk_err) {
	*err = ghk_err;
	gretl_matrix_free(P);
	P = NULL;
    }

    return P;
}

#else /* non-MP version */

gretl_matrix *gretl_GHK (const gretl_matrix *C,
			 const gretl_matrix *A,
			 const gretl_matrix *B,
			 const gretl_matrix *U,
			 int *err)
{
    gretl_matrix *P = NULL;
    gretl_matrix_block *Bk = NULL;
    gretl_matrix *Ai, *Bi;
    gretl_matrix *TA, *TB;
    gretl_matrix *WT, *TT;
    double huge;
    int dim, nobs, ndraws;
    int i, j;

    if (gretl_is_null_matrix(C) ||
	gretl_is_null_matrix(A) ||
	gretl_is_null_matrix(B) ||
	gretl_is_null_matrix(U)) {
	*err = E_DATA;
	return NULL;
    }

    if (A->rows != B->rows ||
	A->cols != B->cols ||
	C->rows != A->cols ||
	C->cols != A->cols ||
	U->rows != A->cols) {
	*err = E_NONCONF;
	return NULL;
    }

    huge = libset_get_double(CONV_HUGE);

    dim = C->rows;
    nobs = A->rows;
    ndraws = U->cols;

    if (!*err) {
	Bk = gretl_matrix_block_new(&Ai, dim, 1,
				    &Bi, dim, 1,
				    &TA, 1, ndraws,
				    &TB, 1, ndraws,
				    &WT, 1, ndraws,
				    &TT, dim, ndraws,
				    NULL);
	if (Bk == NULL) {
	    *err = E_ALLOC;
	}
    }

    if (!*err) {
	P = gretl_matrix_alloc(nobs, 1);
	if (P == NULL) {
	    *err = E_ALLOC;
	}
    }

    set_cephes_hush(1);

    for (i=0; i<nobs && !*err; i++) {
	int ABok = 1, pzero = 0;

	for (j=0; j<dim && !*err; j++) {
	    Ai->val[j] = gretl_matrix_get(A, i, j);
	    Bi->val[j] = gretl_matrix_get(B, i, j);
	    ABok = !(isnan(Ai->val[j]) || isnan(Bi->val[j]));

	    if (!ABok) {
		/* If there are any NaNs in A or B, there's no
		   point in continuing
		*/
		P->val[i] = 0.0/0.0; /* NaN */
		break;
	    } else if (Bi->val[j] < Ai->val[j]) {
		gretl_errmsg_sprintf("ghk: inconsistent bounds: B[%d,%d] < A[%d,%d]",
				     i+1, j+1, i+1, j+1);
		*err = E_DATA;
		gretl_matrix_free(P);
		P = NULL;
	    } else if (Bi->val[j] == Ai->val[j]) {
		P->val[i] = 0.0;
		pzero = 1;
		break;
	    }
	}
	if (!*err && !pzero && ABok) {
	    P->val[i] = GHK_1(C, Ai, Bi, U, TA, TB, WT, TT, huge);
	}
    }

    set_cephes_hush(0);

    gretl_matrix_block_destroy(Bk);

    return P;
}

#endif /* OPENMP or not */

/* below: revised version of GHK (plus score), currently in testing */

static void scaled_convex_combo (gretl_matrix *targ, double w,
				 const gretl_matrix *m1,
				 const gretl_matrix *m2,
				 double scale)
{
    int i, n = gretl_vector_get_length(targ);

    for (i=0; i<n; i++) {
	targ->val[i] = m2->val[i] - w * (m2->val[i] - m1->val[i]);
	targ->val[i] *= scale;
    }
}

static void combo_2 (gretl_matrix *targ,
		     double w1, const gretl_matrix *m1,
		     double w2, const gretl_matrix *m2)
{
    int i, n = gretl_vector_get_length(targ);

    for (i=0; i<n; i++) {
	targ->val[i] = w1 * m1->val[i] + w2 * m2->val[i];
    }
}

static void vector_copy_mul (gretl_matrix *targ,
			     const gretl_matrix *src,
			     double w)
{
    int i, n = gretl_vector_get_length(targ);

    for (i=0; i<n; i++) {
	targ->val[i] = w * src->val[i];
    }
}

static void vector_diff (gretl_matrix *targ,
			 const gretl_matrix *m1,
			 const gretl_matrix *m2)
{
    int i, n = gretl_vector_get_length(targ);

    for (i=0; i<n; i++) {
	targ->val[i] = m1->val[i] - m2->val[i];
    }
}


static void vector_subtract (gretl_matrix *targ,
			     const gretl_matrix *src)
{
    int i, n = gretl_vector_get_length(targ);

    for (i=0; i<n; i++) {
	targ->val[i] -= src->val[i];
    }
}

static double ghk_tj (const gretl_matrix *C,
		      const double *a,
		      const double *b,
		      const double *u,
		      gretl_matrix *dWT,
		      gretl_matrix_block *Bk,
		      double huge,
		      int *err)
{
    gretl_matrix *dTA = NULL;
    gretl_matrix *dTB = NULL;
    gretl_matrix *dm = NULL;
    gretl_matrix *dx = NULL;
    gretl_matrix *tmp = NULL;
    gretl_matrix *cj = NULL;
    gretl_matrix *TT = NULL;
    gretl_matrix *dTT = NULL;
    double TA, TB, WT;
    double z, x, fx, den;
    int m = C->rows;
    int npar = m + m + m*(m+1)/2;
    int do_score = (dWT != NULL);
    int inicol, j, i;

    if (do_score) {
	dTA = gretl_matrix_block_get_matrix(Bk, 0);
	dTB = gretl_matrix_block_get_matrix(Bk, 1);
	dm  = gretl_matrix_block_get_matrix(Bk, 2);
	dx  = gretl_matrix_block_get_matrix(Bk, 3);
	tmp = gretl_matrix_block_get_matrix(Bk, 4);
	cj  = gretl_matrix_block_get_matrix(Bk, 5);
	TT  = gretl_matrix_block_get_matrix(Bk, 6);
	dTT = gretl_matrix_block_get_matrix(Bk, 7);
    } else {
	cj = gretl_matrix_block_get_matrix(Bk, 0);
	TT = gretl_matrix_block_get_matrix(Bk, 1);
    }

    gretl_matrix_block_zero(Bk);
    den = C->val[0];

    if (do_score) {
	gretl_matrix_reuse(dTT, npar, 1);
    }

    if (a[0] == -huge) {
	TA = 0.0;
    } else {
	z = a[0] / den;
	TA = normal_cdf(z);
	if (do_score) {
	    dTA->val[0] = normal_pdf(z) / den;
	    dTA->val[2*m] = -normal_pdf(z) * z/den;
	}
    }

    if (b[0] == huge) {
	TB = 1.0;
    } else {
	z = b[0] / den;
	TB = normal_cdf(z);
	if (do_score) {
	    dTB->val[m] = normal_pdf(z) / den;
	    dTB->val[2*m] = -normal_pdf(z) * z/den;
	}
    }

    WT = TB - TA;
    x = TB - u[0] * (TB - TA);
    TT->val[0] = normal_cdf_inverse(x);

    if (do_score) {
	fx = normal_pdf(TT->val[0]);
	scaled_convex_combo(dTT, u[0], dTA, dTB, 1/fx);
	vector_diff(dWT, dTB, dTA);
    }

    /* first column of the gradient which refers to C */
    inicol = 2*m + 1;

    for (j=1; j<m; j++) {
	double mj = 0.0;

	gretl_matrix_reuse(TT, j, 1);
	gretl_matrix_reuse(cj, 1, j);

	for (i=0; i<j; i++) {
	    cj->val[i] = gretl_matrix_get(C, j, i);
	    mj += cj->val[i] * TT->val[i];
	}

	if (do_score) {
	    int k = 0;

	    gretl_matrix_zero(dx);
	    gretl_matrix_zero(dm);

	    for (i=inicol; i<=inicol+j-1; i++) {
		dm->val[i] = TT->val[k++];
	    }
	    gretl_matrix_multiply_mod(cj, GRETL_MOD_NONE,
				      dTT, GRETL_MOD_TRANSPOSE,
				      tmp, GRETL_MOD_NONE);
	    gretl_matrix_add_to(dm, tmp);
	}

        den = gretl_matrix_get(C, j, j);

	if (a[j] == -huge) {
            TA = 0.0;
	    if (do_score) {
		gretl_matrix_zero(dTA);
	    }
	} else {	    
            x = (a[j] - mj) / den;
            TA = normal_cdf(x);
	    if (do_score) {
		fx = normal_pdf(x);
		dx->val[j] = 1;
		vector_subtract(dx, dm);
		dx->val[inicol+j] -= x;
		vector_copy_mul(dTA, dx, fx/den);
	    }
	}

	if (do_score) {
	    gretl_matrix_zero(dx);
	}

 	if (b[j] == huge) {
            TB = 1.0;
	    if (do_score) {
		gretl_matrix_zero(dTB);
	    }
	} else {	    
            x = (b[j] - mj) / den;
            TB = normal_cdf(x);
	    if (do_score) {
		fx = normal_pdf(x);
		dx->val[m+j] = 1;
		vector_subtract(dx, dm);
		dx->val[inicol+j] -= x;
		vector_copy_mul(dTB, dx, fx/den);
	    }
	}

	x = TB - u[j] * (TB - TA);
	TT->val[j] = normal_cdf_inverse(x);

	if (do_score) {
	    fx = normal_pdf(TT->val[j]);
	    scaled_convex_combo(tmp, u[j], dTA, dTB, 1/fx);
	    gretl_matrix_reuse(dTT, npar, j+1);
	    gretl_matrix_inscribe_matrix(dTT, tmp, 0, j, GRETL_MOD_TRANSPOSE);
	    vector_diff(tmp, dTB, dTA);
	    combo_2(dWT, WT, tmp, TB-TA, dWT);
	}

        WT *= TB -TA; /* accumulate weight */
        inicol += j+1;
    }

    return WT;
}

/* This function translates column positions
   for the derivatives of elements of C from the
   "horizontal vech" into the "proper vech"
*/

static int *column_indices (int m)
{
    int i, j, pos, k = 0;
    int *ret;

    ret = malloc((m * (m+1))/2 * sizeof *ret);
    if (ret == NULL) {
	return ret;
    }

    for (i=0; i<m; i++) {
	for (j=0; j<=i; j++) {
	    pos = j*(j+1)/2 + j*(m-j-1) + i;
	    ret[k++] = pos;
	}
    }

    return ret;
}

static int reorder_dP (gretl_matrix *dP, int m)
{
    int *ndx = column_indices(m);
    int nc = dP->cols - 2*m;
    gretl_matrix *tmp;
    double xij;
    int i, j, k;

    tmp = gretl_matrix_alloc(dP->rows, nc);
    if (tmp == NULL) {
	return E_ALLOC;
    }

    gretl_matrix_extract_matrix(tmp, dP, 0, nc, GRETL_MOD_NONE);

    for (j=0; j<nc; j++) {
	if (ndx[j] != j) {
	    k = ndx[j] + 2*m;
	    for (i=0; i<dP->rows; i++) {
		xij = gretl_matrix_get(tmp, i, j);
		gretl_matrix_set(dP, i, k, xij);
	    }
	}
    }

    gretl_matrix_free(tmp);

    return 0;
}

gretl_matrix *gretl_GHK2 (const gretl_matrix *C,
			  const gretl_matrix *A,
			  const gretl_matrix *B,
			  const gretl_matrix *U,
			  gretl_matrix **pdP,
			  int *err)
{
    gretl_matrix_block *Bk;
    gretl_matrix *a, *b, *u;
    gretl_matrix *P = NULL;
    gretl_matrix *dP = NULL;
    gretl_matrix *dpj = NULL;

    /* for passing to ghk_tj */
    gretl_matrix_block *Bk2;
    gretl_matrix *dTA;
    gretl_matrix *dTB;
    gretl_matrix *dTT;
    gretl_matrix *dm;
    gretl_matrix *dx;
    gretl_matrix *tmp;
    gretl_matrix *TT;
    gretl_matrix *cj;

    int r = U->cols;
    int n = A->rows;
    int m = C->rows;
    int npar = m + m + m*(m+1)/2;
    int do_score = (pdP != NULL);
    double huge;
    GretlMatrixMod mod;
    int ghk_err = 0;
    int t, i, j;

    /* work space for this function */
    Bk = gretl_matrix_block_new(&a, 1, m,
				&b, 1, m,
				&u, m, 1,
				NULL);
    if (Bk == NULL) {
	*err = E_ALLOC;
	return NULL;
    }

    if (do_score) {
	/* work space for ghk_tj, with score */
	Bk2 = gretl_matrix_block_new(&dTA, 1, npar,
				     &dTB, 1, npar,
				     &dm,  1, npar,
				     &dx,  1, npar,
				     &tmp, 1, npar,
				     &cj,  1, m,
				     &TT,  m, 1,
				     &dTT, npar, m,
				     NULL);
    } else {
	/* work space for ghk_tj, prob only */
	Bk2 = gretl_matrix_block_new(&cj,  1, m,
				     &TT,  m, 1,
				     NULL);
    }

    if (Bk2 == NULL) {
	*err = E_ALLOC;
	goto bailout;
    }

    P = gretl_matrix_alloc(n, 1);
    if (P == NULL) {
	*err = E_ALLOC;
	goto bailout;
    }

    if (do_score) {
	dP = gretl_zero_matrix_new(n, npar);
	dpj = gretl_matrix_alloc(1, npar);
	if (dP == NULL || dpj == NULL) {
	    *err = E_ALLOC;
	    goto bailout;
	}	    
    }

    huge = libset_get_double(CONV_HUGE);
#if 0
    set_cephes_hush(1);
#endif

    for (t=0; t<n; t++) {
	/* loop across observations */
	int err_t = 0;

	for (i=0; i<m; i++) {
	    /* transcribe and check bounds at current obs */
	    a->val[i] = gretl_matrix_get(A, t, i);
	    b->val[i] = gretl_matrix_get(B, t, i);
	    if (isnan(a->val[i]) || isnan(b->val[i])) {
		err_t = E_MISSDATA;
		break;
	    } else if (b->val[i] < a->val[i]) {
		ghk_err = err_t = E_DATA;
		break;
	    }
	}
	if (err_t == E_DATA) {
	    gretl_errmsg_sprintf("ghk: inconsistent bounds: B[%d,%d] < A[%d,%d]",
				 t+1, i+1, t+1, i+1);
	    break;
	} else if (err_t == E_MISSDATA) {
	    P->val[t] = 0.0/0.0; /* NaN */
	    if (do_score) {
		for (j=0; j<npar; j++) {
		    gretl_matrix_set(dP, t, j, 0.0/0.0);
		}
	    }
	    continue;
	}
	P->val[t] = 0.0;
	for (j=0; j<r; j++) {
	    /* Monte Carlo iterations, using successive columns of U */
	    gretl_matrix_extract_matrix(u, U, 0, j, GRETL_MOD_NONE);
            P->val[t] += ghk_tj(C, a->val, b->val, u->val, dpj, Bk2, huge, err);
	    if (do_score) {
		mod = (j == 0)? GRETL_MOD_NONE : GRETL_MOD_CUMULATE;
		gretl_matrix_inscribe_matrix(dP, dpj, t, 0, mod);
	    }
	}
	P->val[t] /= r;
	if (do_score) {
	    double x;

	    for (j=0; j<npar; j++) {
		x = gretl_matrix_get(dP, t, j);
		gretl_matrix_set(dP, t, j, x / r);
	    }
	}
    }

#if 0
    set_cephes_hush(0);
#endif

    if (do_score) {
	reorder_dP(dP, m);
	*pdP = dP;
    }

 bailout:

    gretl_matrix_block_destroy(Bk);
    gretl_matrix_block_destroy(Bk2);
    gretl_matrix_free(dpj);

    if (!*err && ghk_err) {
	*err = ghk_err;
    }

    if (*err) {
	gretl_matrix_free(dP);
	gretl_matrix_free(P);
	P = NULL;
    }

    return P;
}