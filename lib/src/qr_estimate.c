/*
 *   Copyright (c) 2003-2004 by Allin Cottrell
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

#include "libgretl.h"
#include "qr_estimate.h"
#include "gretl_matrix.h"
#include "gretl_matrix_private.h"
#include "gretl_private.h"
#include "libset.h"

#define QR_RCOND_MIN 1e-15 /* experiment with this? */
#define ESSZERO      1e-22 /* SSR < this counts as zero */

/* In fortran arrays, column entries are contiguous.
   Columns of data matrix X hold variables, rows hold observations.
   So in a fortran array, entries for a given variable are
   contiguous.
*/

static double get_tss (const double *y, int n, int ifc)
{
    double ymean = 0.0;
    double x, tss = 0.0;
    int i;

    if (ifc) {
	ymean = gretl_mean(0, n-1, y);
    }

    for (i=0; i<n; i++) {
	x = y[i] - ymean;
	tss += x * x;
    }

    return tss;
}

static void qr_compute_f_stat (MODEL *pmod, gretlopt opts)
{
    if (pmod->ncoeff == 1 && pmod->ifc) {
	pmod->fstt = NADBL;
	return;
    }

    if (pmod->dfd > 0 && pmod->dfn > 0) {
	if (opts & OPT_R) {
	    pmod->fstt = robust_omit_F(NULL, pmod);
	} else {
	    pmod->fstt = (pmod->tss - pmod->ess) * pmod->dfd / 
		(pmod->ess * pmod->dfn);
	}
    } else {
	pmod->fstt = NADBL;
    }
}

static void qr_compute_r_squared (MODEL *pmod, const double *y, int n)
{
    if (pmod->dfd > 0) {
	if (pmod->ifc) {
	    double den = pmod->tss * pmod->dfd;

	    pmod->rsq = 1.0 - (pmod->ess / pmod->tss);
	    pmod->adjrsq = 1.0 - (pmod->ess * (n - 1) / den);
	} else {
	    double alt = corrrsq(n, y + pmod->t1, pmod->yhat + pmod->t1);

	    if (na(alt)) {
		pmod->rsq = pmod->adjrsq = NADBL;
	    } else {
		pmod->rsq = alt;
		pmod->adjrsq = 1.0 - ((1 - alt) * (n - 1.0) / pmod->dfd);
	    }
	}
    } else {
	pmod->rsq = 1.0;
    }
}

int get_vcv_index (MODEL *pmod, int i, int j, int n)
{
    int k, vi, vj;

    if (pmod->ifc) {
	vi = (i + 1) % n;
	vj = (j + 1) % n;
    } else {
	vi = i;
	vj = j;
    }

    k = ijton(vi, vj, n);

    return k;
}

static int qr_make_vcv (MODEL *pmod, gretl_matrix *v, int robust)
{
    const int nv = pmod->ncoeff;
    const int nterms = nv * (nv + 1) / 2;
    double x;
    int i, j, k;

    pmod->vcv = malloc(nterms * sizeof *pmod->vcv);
    if (pmod->vcv == NULL) return 1;  

    for (i=0; i<nv; i++) {
	for (j=0; j<=i; j++) {
	    k = get_vcv_index(pmod, i, j, nv);
	    x = gretl_matrix_get(v, i, j);
	    if (!robust) {
		x *= pmod->sigma * pmod->sigma;
	    }
	    pmod->vcv[k] = x;
	}
    }

    return 0;
}

static void get_resids_and_SSR (MODEL *pmod, const double **Z,
				gretl_matrix *yhat, int fulln)
{
    int t, i = 0;
    int dwt = gretl_model_get_int(pmod, "wt_dummy");
    int qdiff = (pmod->rho != 0.0);
    int pwe = gretl_model_get_int(pmod, "pwe");
    int yvar = pmod->list[1];
    double u, y;

    if (dwt) dwt = pmod->nwt;

    pmod->ess = 0.0;

    if (qdiff) {
	for (t=0; t<fulln; t++) {
	    if (t < pmod->t1 || t > pmod->t2) {
		pmod->yhat[t] = pmod->uhat[t] = NADBL;
	    } else {
		y = Z[yvar][t];
		if (t == pmod->t1 && pwe) {
		    y *= sqrt(1.0 - pmod->rho * pmod->rho);
		} else {
		    y -= pmod->rho * Z[yvar][t-1];
		}
		u = y - yhat->val[i];
		pmod->uhat[t] = u;
		pmod->ess += u * u;
		i++;
	    }
	}
    } else if (pmod->nwt) {
	for (t=0; t<fulln; t++) {
	    if (t < pmod->t1 || t > pmod->t2 || model_missing(pmod, t)) {
		pmod->yhat[t] = pmod->uhat[t] = NADBL;
	    } else {
		y = Z[yvar][t];
		if (dwt && Z[dwt][t] == 0.0) {
		    pmod->yhat[t] = NADBL;
		} else {
		    if (!dwt) {
			y *= Z[pmod->nwt][t];
		    }
		    pmod->yhat[t] = yhat->val[i];
		    pmod->uhat[t] = y - yhat->val[i];
		    pmod->ess += pmod->uhat[t] * pmod->uhat[t];
		    i++;
		}
	    }
	}
    } else {
	for (t=0; t<fulln; t++) {
	    if (t < pmod->t1 || t > pmod->t2 || model_missing(pmod, t)) {
		pmod->yhat[t] = pmod->uhat[t] = NADBL;
	    } else {
		pmod->yhat[t] = yhat->val[i];
		pmod->uhat[t] = Z[yvar][t] - yhat->val[i];
		pmod->ess += pmod->uhat[t] * pmod->uhat[t];
		i++;
	    }
	}
    }

    /* if SSR is small enough, treat it as zero */
    if (pmod->ess < ESSZERO && pmod->ess > (-ESSZERO)) {
	pmod->ess = 0.0;
    } 
}

static void 
get_data_X (gretl_matrix *X, const MODEL *pmod, const double **Z)
{
    int i, j, t;
    int start = (pmod->ifc)? 3 : 2;

    /* copy independent vars into matrix X */
    j = 0;
    for (i=start; i<=pmod->list[0]; i++) {
	for (t=pmod->t1; t<=pmod->t2; t++) {
	    if (!model_missing(pmod, t)) {
		X->val[j++] = Z[pmod->list[i]][t];
	    }
	}
    }

    /* insert constant */
    if (pmod->ifc) {
	for (t=pmod->t1; t<=pmod->t2; t++) {
	    if (!model_missing(pmod, t)) {
		X->val[j++] = 1.0;
	    }
	}
    }
}

static gretl_matrix *make_data_X (const MODEL *pmod, const double **Z)
{
    gretl_matrix *X;

    X = gretl_matrix_alloc(pmod->nobs, pmod->ncoeff);
    if (X != NULL) {
	get_data_X(X, pmod, Z);
    }

    return X;
}

/* Calculate W(t)-transpose * W(t-lag) */

static void wtw (gretl_matrix *wt, gretl_matrix *X, 
		 int n, int t, int lag)
{
    int i, j;
    double xi, xj;

    for (i=0; i<n; i++) {
	xi = X->val[mdx(X, t, i)];
	for (j=0; j<n; j++) {
	    xj = X->val[mdx(X, t - lag, j)];
	    wt->val[mdx(wt, i, j)] = xi * xj;
	}
    }
}

/* Calculate the Newey-West HAC covariance matrix.  Algorithm and
   (basically) notation taken from Davidson and MacKinnon (DM), 
   Econometric Theory and Methods, chapter 9.
*/

static int qr_make_hac (MODEL *pmod, const double **Z, gretl_matrix *xpxinv)
{
    gretl_matrix *vcv = NULL, *wtj = NULL, *gammaj = NULL;
    gretl_matrix *X;
    int m = pmod->nobs;
    int n = pmod->ncoeff;
    int p, i, j, t;
    double weight, uu;
    double *uhat;
    int err = 0;

    X = make_data_X(pmod, Z);
    if (X == NULL) return 1;

    /* pmod->uhat is a full-length series: we must take an offset
       into it, equal to the offset of the data on which the model
       is actually estimated.
    */
    uhat = pmod->uhat + pmod->t1;

    /* get the user's preferred maximum lag setting */
    p = get_hac_lag(m);
    gretl_model_set_int(pmod, "hac_lag", p);

    vcv = gretl_matrix_alloc(n, n);
    wtj = gretl_matrix_alloc(n, n);
    gammaj = gretl_matrix_alloc(n, n);
    if (vcv == NULL || wtj == NULL || gammaj == NULL) {
	err = 1;
	goto bailout;
    }

    gretl_matrix_zero(vcv);

    for (j=0; j<=p; j++) {
	/* cumulate running sum of Gamma-hat terms */
	gretl_matrix_zero(gammaj);
	for (t=j; t<m; t++) {
	    /* W(t)-transpose * W(t-j) */
	    wtw(wtj, X, n, t, j);
	    uu = uhat[t] * uhat[t-j];
	    gretl_matrix_multiply_by_scalar(wtj, uu);
	    /* DM equation (9.36), p. 363 */
	    gretl_matrix_add_to(gammaj, wtj);
	}

	if (j > 0) {
	    /* Gamma(j) = Gamma(j) + Gamma(j)-transpose */
	    gretl_matrix_add_self_transpose(gammaj);
	    weight = 1.0 - (double) j / (p + 1.0);
	    /* multiply by Newey-West weight */
	    gretl_matrix_multiply_by_scalar(gammaj, weight);
	}

	/* DM equation (9.38), p. 364 */
	gretl_matrix_add_to(vcv, gammaj);
    }

    gretl_matrix_multiply_mod(xpxinv, GRETL_MOD_TRANSPOSE,
			      vcv, GRETL_MOD_NONE,
			      wtj);
    gretl_matrix_multiply(wtj, xpxinv, vcv);

    /* vcv now holds HAC */
    for (i=0; i<n; i++) {
	double x = gretl_matrix_get(vcv, i, i);

	j = (pmod->ifc)? (i + 1) % n : i;
	pmod->sderr[j] = sqrt(x);
    }

    /* Transcribe vcv into triangular representation */
    err = qr_make_vcv(pmod, vcv, 1);

 bailout:

    gretl_matrix_free(wtj);
    gretl_matrix_free(gammaj);
    gretl_matrix_free(vcv);
    gretl_matrix_free(X);

    return err;
}

/* Multiply X transpose into D, treating D as if it were
   a diagonal matrix -- although in fact it is just a vector,
   for economy of storage.  Result into R.
*/

static void do_X_prime_diag (const gretl_matrix *X,
			     const gretl_vector *D,
			     gretl_matrix *R)
{
    const double *d;
    double x;
    int i, j;

    for (i=0; i<R->rows; i++) {
	d = D->val;
	for (j=0; j<R->cols; j++) {
	    x = X->val[mdx(X, j, i)];
	    R->val[mdx(R, i, j)] = x * (*d++);
	}
    }
}

/* Heteroskedasticity-Consistent Covariance Matrices: See Davidson
   and MacKinnon, Econometric Theory and Methods, chapter 5, esp.
   page 200.  Implements HC0, HC1, HC2 and HC3.
*/

static int qr_make_hccme (MODEL *pmod, const double **Z, 
			  gretl_matrix *Q, gretl_matrix *xpxinv)
{
    gretl_matrix *X;
    gretl_matrix *diag = NULL;
    gretl_matrix *tmp1 = NULL, *tmp2 = NULL, *tmp3 = NULL;
    int T = pmod->nobs; 
    int k = pmod->list[0] - 1;
    int hc_version;
    int i, j, t;
    int err = 0;

    X = make_data_X(pmod, Z);
    if (X == NULL) return 1;

    diag = gretl_column_vector_from_array(pmod->uhat + pmod->t1, T,
					  GRETL_MOD_SQUARE);
    if (diag == NULL) {
	err = 1;
	goto bailout;
    }  

    tmp1 = gretl_matrix_alloc(k, T);
    tmp2 = gretl_matrix_alloc(k, k);
    tmp3 = gretl_matrix_alloc(k, k);
    if (tmp1 == NULL || tmp2 == NULL || tmp3 == NULL) {
	err = 1;
	goto bailout;
    }  

    hc_version = get_hc_version();
    gretl_model_set_int(pmod, "hc", 1);
    if (hc_version > 0) {
	gretl_model_set_int(pmod, "hc_version", hc_version);
    }

    if (hc_version == 1) {
	for (t=0; t<T; t++) {
	    diag->val[t] *= (double) T / (T - k);
	}
    } else if (hc_version > 1) {
	/* do the h_t calculations */
	for (t=0; t<T; t++) {
	    double q, ht = 0.0;

	    for (i=0; i<k; i++) {
		q = Q->val[mdx(Q, t, i)];
		ht += q * q;
	    }
	    if (hc_version == 2) {
		diag->val[t] /= (1.0 - ht);
	    } else { /* HC3 */
		diag->val[t] /= (1.0 - ht) * (1.0 - ht);
	    }
	}
    }

    do_X_prime_diag(X, diag, tmp1);

    gretl_matrix_multiply(tmp1, X, tmp2);
    gretl_matrix_multiply(xpxinv, tmp2, tmp3); 
    gretl_matrix_multiply(tmp3, xpxinv, tmp2);

    /* tmp2 now holds HCCM */
    for (i=0; i<k; i++) {
	double x = tmp2->val[mdx(tmp2, i, i)];

	j = (pmod->ifc)? (i + 1) % k : i;
	pmod->sderr[j] = sqrt(x);
    }

    err = qr_make_vcv(pmod, tmp2, 1);

    bailout:

    gretl_matrix_free(X);
    gretl_matrix_free(diag);
    gretl_matrix_free(tmp1);
    gretl_matrix_free(tmp2);
    gretl_matrix_free(tmp3);

    return err;
}

static int qr_make_regular_vcv (MODEL *pmod, gretl_matrix *v)
{
    int i, k, n = pmod->ncoeff;
    int err = 0;

    for (i=0; i<n; i++) {
	double x = v->val[mdx(v, i, i)];

	k = (pmod->ifc)? (i + 1) % n : i;
	pmod->sderr[k] = pmod->sigma * sqrt(x);
    }

    err = qr_make_vcv(pmod, v, 0);

    return err;
}

static void get_model_data (MODEL *pmod, const double **Z, 
			    gretl_matrix *Q, gretl_matrix *y)
{
    int i, j, t, start;
    double x;
    int dwt = gretl_model_get_int(pmod, "wt_dummy");
    int qdiff = (pmod->rho != 0.0);
    int pwe = gretl_model_get_int(pmod, "pwe");
    double pw1 = 0.0;

    if (pwe) {
	pw1 = sqrt(1.0 - pmod->rho * pmod->rho);
    } 

    start = (pmod->ifc)? 3 : 2;

    if (dwt) dwt = pmod->nwt;

    /* copy independent vars into matrix Q */
    j = 0;
    for (i=start; i<=pmod->list[0]; i++) {
	for (t=pmod->t1; t<=pmod->t2; t++) {
	    if (model_missing(pmod, t)) {
		continue;
	    }
	    x = Z[pmod->list[i]][t];
	    if (dwt) {
		if (Z[dwt][t] == 0.0) continue;
	    } else if (pmod->nwt) {
		x *= Z[pmod->nwt][t];
	    } else if (qdiff) {
		if (pwe && t == pmod->t1) {
		    x *= pw1;
		} else {
		    x -= pmod->rho * Z[pmod->list[i]][t-1];
		}
	    }
	    Q->val[j++] = x;
	}
    }

    /* insert constant last (numerical issues) */
    if (pmod->ifc) {
	for (t=pmod->t1; t<=pmod->t2; t++) {
	    if (model_missing(pmod, t)) {
		continue;
	    }	    
	    x = Z[0][t]; /* in some special cases the constant is pre-transformed */
	    if (dwt) {
		if (Z[dwt][t] == 0.0) continue;
	    } else if (pmod->nwt) {
		x = Z[pmod->nwt][t];
	    } else if (qdiff) {
		if (pwe && t == pmod->t1) {
		    x = pw1;
		} else {
		    x = 1.0 - pmod->rho;
		}
	    } 
	    Q->val[j++] = x;
	}
    }

    /* copy dependent variable into y vector */
    j = 0;
    for (t=pmod->t1; t<=pmod->t2; t++) {
	if (model_missing(pmod, t)) {
	    continue;
	}		
	x = Z[pmod->list[1]][t];
	if (dwt) {
	    if (Z[dwt][t] == 0.0) continue;
	} else if (pmod->nwt) {
	    x *= Z[pmod->nwt][t];
	} else if (qdiff) {
	    if (pwe && t == pmod->t1) {
		x *= pw1;
	    } else {
		x -= pmod->rho * Z[pmod->list[1]][t-1];
	    }
	}
	y->val[j++] = x;
    }

    /* fetch tss based on the (possibly transformed) y */
    pmod->tss = get_tss(y->val, pmod->nobs, pmod->ifc);
}

static void save_coefficients (MODEL *pmod, gretl_matrix *b,
			       int n)
{
    pmod->coeff = gretl_matrix_steal_data(b);

    if (pmod->ifc) {
	int i;
	double tmp = pmod->coeff[n-1];

	for (i=n-1; i>0; i--) {
	    pmod->coeff[i] = pmod->coeff[i-1];
	}
	pmod->coeff[0] = tmp;
    }
}

static int 
allocate_model_arrays (MODEL *pmod, int n, int m)
{
    pmod->sderr = malloc(n * sizeof *pmod->sderr);
    pmod->yhat = malloc(m * sizeof *pmod->yhat);
    pmod->uhat = malloc(m * sizeof *pmod->uhat);

    if (pmod->sderr == NULL || pmod->yhat == NULL || pmod->uhat == NULL) {
	return 1;
    }

    return 0;
}

int gretl_qr_regress (MODEL *pmod, const double **Z, int fulln,
		      gretlopt opts)
{
    integer info, lwork;
    integer m, n, lda;
    integer *iwork = NULL;
    gretl_matrix *Q = NULL, *y = NULL;
    gretl_matrix *R = NULL, *g = NULL, *b = NULL;
    gretl_matrix *xpxinv = NULL;
    doublereal *tau = NULL, *work = NULL;
    doublereal *work2;
    doublereal rcond;
    char uplo = 'U';
    char diag = 'N';
    char norm = '1';
    int i, j;
    int err = 0;

    m = pmod->nobs;               /* # of rows = # of observations */
    lda = m;                      /* leading dimension of Q */
    n = pmod->list[0] - 1;        /* # of cols = # of variables */

    Q = gretl_matrix_alloc(m, n);
    y = gretl_matrix_alloc(m, 1);

    /* dim of tau is min (m, n) */
    tau = malloc(n * sizeof *tau);

    work = malloc(sizeof *work);
    iwork = malloc(n * sizeof *iwork);

    if (Q == NULL || y == NULL || tau == NULL || work == NULL ||
	iwork == NULL) {
	err = E_ALLOC;
	goto qr_cleanup;
    }

    get_model_data(pmod, Z, Q, y);

    /* do a workspace size query */
    lwork = -1;
    info = 0;
    dgeqrf_(&m, &n, Q->val, &lda, tau, work, &lwork, &info);
    if (info != 0) {
	fprintf(stderr, "dgeqrf: info = %d\n", (int) info);
	err = 1;
	goto qr_cleanup;
    }

    /* set up optimally sized work array */
    lwork = (integer) work[0];
    work2 = realloc(work, (size_t) lwork * sizeof *work);
    if (work2 == NULL) {
	err = E_ALLOC;
	goto qr_cleanup;
    }

    work = work2;

    /* run actual QR factorization */
    dgeqrf_(&m, &n, Q->val, &lda, tau, work, &lwork, &info);
    if (info != 0) {
	fprintf(stderr, "dgeqrf: info = %d\n", (int) info);
	err = 1;
	goto qr_cleanup;
    }

    /* check reciprocal condition number of R */
    dtrcon_(&norm, &uplo, &diag, &n, Q->val, &lda, &rcond, work, 
	    iwork, &info);
    if (info != 0) {
	fprintf(stderr, "dtrcon: info = %d\n", (int) info);
	err = 1;
	goto qr_cleanup;
    }

    if (rcond < QR_RCOND_MIN) {
	fprintf(stderr, "dtrcon: rcond = %g, but min is %g\n", rcond,
		QR_RCOND_MIN);
	err = E_SINGULAR;
	goto qr_cleanup;
    }

    /* allocate temporary auxiliary matrices */
    R = gretl_matrix_alloc(n, n);
    g = gretl_matrix_alloc(n, 1);
    b = gretl_matrix_alloc(n, 1);
    if (R == NULL || g == NULL || b == NULL) {
	err = E_ALLOC;
	goto qr_cleanup;
    }

    if (allocate_model_arrays(pmod, n, fulln)) {
	err = E_ALLOC;
	goto qr_cleanup;
    }

    /* invert R */
    dtrtri_(&uplo, &diag, &n, Q->val, &lda, &info);
    if (info != 0) {
	fprintf(stderr, "dtrtri: info = %d\n", (int) info);
	err = 1;
	goto qr_cleanup;
    }

    /* copy the upper triangular R-inverse out of Q */
    for (i=0; i<n; i++) {
	for (j=0; j<n; j++) {
	    if (i <= j) {
		gretl_matrix_set(R, i, j, 
				 gretl_matrix_get(Q, i, j));
	    } else {
		gretl_matrix_set(R, i, j, 0.0);
	    }
	}
    }

    /* obtain the real "Q" matrix */
    dorgqr_(&m, &n, &n, Q->val, &lda, tau, work, &lwork, &info);
    if (info != 0) {
	fprintf(stderr, "dorgqr: info = %d\n", (int) info);
	err = 1;
	goto qr_cleanup;
    }

    /* make "g" into gamma-hat */    
    gretl_matrix_multiply_mod(Q, GRETL_MOD_TRANSPOSE,
			      y, GRETL_MOD_NONE, g);

    /* OLS coefficients */
    gretl_matrix_multiply(R, g, b);
    save_coefficients(pmod, b, n);

    /* write vector of fitted values into y */
    gretl_matrix_multiply(Q, g, y);    

    /* get vector of residuals and SSR */
    get_resids_and_SSR(pmod, Z, y, fulln);

    /* standard error of regression */
    if (m - n > 0) {
	if (gretl_model_get_int(pmod, "no-df-corr")) {
	    pmod->sigma = sqrt(pmod->ess / m);
	} else {
	    pmod->sigma = sqrt(pmod->ess / (m - n));
	}
    } else {
	pmod->sigma = 0.0;
    }

    /* create (X'X)^{-1} */
    xpxinv = gretl_matrix_alloc(n, n);
    if (xpxinv == NULL) {
	err = E_ALLOC;
	goto qr_cleanup;
    }

    gretl_matrix_multiply_mod(R, GRETL_MOD_NONE,
			      R, GRETL_MOD_TRANSPOSE,
			      xpxinv);

    /* VCV and standard errors */
    if (opts & OPT_R) { 
	gretl_model_set_int(pmod, "robust", 1);
	if ((opts & OPT_T) && !get_force_hc()) {
	    qr_make_hac(pmod, Z, xpxinv);
	} else {
	    qr_make_hccme(pmod, Z, Q, xpxinv);
	}
    } else {
	qr_make_regular_vcv(pmod, xpxinv);
    }

    gretl_matrix_free(Q);
    Q = NULL;

    /* get R^2 */
    qr_compute_r_squared(pmod, Z[pmod->list[1]], m);
    qr_compute_f_stat(pmod, opts);

 qr_cleanup:
    gretl_matrix_free(Q);
    gretl_matrix_free(y);

    free(tau); free(work);
    free(iwork);

    gretl_matrix_free(R);
    gretl_matrix_free(g);
    gretl_matrix_free(b);
    gretl_matrix_free(xpxinv);

    pmod->errcode = err;

    return err;    
}

#undef USE_SVD

#ifdef USE_SVD

static void svd_get_hvec (const gretl_matrix *X,
			  const gretl_matrix *V,
			  gretl_vector *h)
{
    double ht, htj;
    int k = X->cols;
    int T = X->rows;
    int i, j, t;

    for (t=0; t<T; t++) {
	ht = 0.0;
	for (j=0; j<k; j++) {
	    htj = 0.0;
	    for (i=0; i<k; i++) {
		htj += X->val[mdx(X, T, i)] * V->val[mdx(V, i, j)];
	    }
	    htj *= X->val[mdx(X, T, j)];
	    ht += htj;
	}
	gretl_vector_set(h, t, ht);
    }
}

static int svd_make_hccme (MODEL *pmod, const double **Z, 
			   gretl_matrix *X, gretl_matrix *xpxinv)
{
    gretl_matrix *diag = NULL;
    gretl_matrix *h = NULL;
    gretl_matrix *tmp1 = NULL, *tmp2 = NULL, *tmp3 = NULL;
    int T = pmod->nobs; 
    int k = pmod->list[0] - 1;
    int hc_version;
    int i, j, t;
    int err = 0;

    diag = gretl_column_vector_from_array(pmod->uhat + pmod->t1, T,
					  GRETL_MOD_SQUARE);
    if (diag == NULL) {
	err = 1;
	goto bailout;
    }  

    tmp1 = gretl_matrix_alloc(k, T);
    tmp2 = gretl_matrix_alloc(k, k);
    tmp3 = gretl_matrix_alloc(k, k);
    h = gretl_column_vector_alloc(T);
    if (tmp1 == NULL || tmp2 == NULL || tmp3 == NULL || h == NULL) {
	err = 1;
	goto bailout;
    } 

    get_data_X(X, pmod, Z);
    svd_get_hvec(X, xpxinv, h);

    hc_version = get_hc_version();
    gretl_model_set_int(pmod, "hc", 1);
    if (hc_version > 0) {
	gretl_model_set_int(pmod, "hc_version", hc_version);
    }

    if (hc_version == 1) {
	for (t=0; t<T; t++) {
	    diag->val[t] *= (double) T / (T - k);
	}
    } else if (hc_version > 1) {
	/* do the h_t calculations */
	for (t=0; t<T; t++) {
	    double ht = h->val[t];

	    if (hc_version == 2) {
		diag->val[t] /= (1.0 - ht);
	    } else { /* HC3 */
		diag->val[t] /= (1.0 - ht) * (1.0 - ht);
	    }
	}
    }

    do_X_prime_diag(X, diag, tmp1);

    gretl_matrix_multiply(tmp1, X, tmp2);
    gretl_matrix_multiply(xpxinv, tmp2, tmp3); 
    gretl_matrix_multiply(tmp3, xpxinv, tmp2);

    /* tmp2 now holds HCCM */
    for (i=0; i<k; i++) {
	double x = tmp2->val[mdx(tmp2, i, i)];

	j = (pmod->ifc)? (i + 1) % k : i;
	pmod->sderr[j] = sqrt(x);
    }

    err = qr_make_vcv(pmod, tmp2, 1);

    bailout:

    gretl_matrix_free(diag);
    gretl_matrix_free(tmp1);
    gretl_matrix_free(tmp2);
    gretl_matrix_free(tmp3);
    gretl_vector_free(h);

    return err;
}

static void svd_yhat_uhat (MODEL *pmod, const double **Z, int fulln)
{
    int i, t;
    int l0 = pmod->list[0];
    int dwt = gretl_model_get_int(pmod, "wt_dummy");
    int qdiff = (pmod->rho != 0.0);
    int pwe = gretl_model_get_int(pmod, "pwe");
    int yvar = pmod->list[1];
    double u, y, yhat;

    if (dwt) dwt = pmod->nwt;

    pmod->ess = 0.0;

    if (qdiff) {
	for (t=0; t<fulln; t++) {
	    if (t < pmod->t1 || t > pmod->t2) {
		pmod->yhat[t] = pmod->uhat[t] = NADBL;
	    } else {
		y = Z[yvar][t];
		if (t == pmod->t1 && pwe) {
		    y *= sqrt(1.0 - pmod->rho * pmod->rho);
		} else {
		    y -= pmod->rho * Z[yvar][t-1];
		}
		yhat = 0.0;
		for (i=2; i<=l0; i++) {
		    yhat += pmod->coeff[i-2] * Z[pmod->list[i]][t];
		}
		u = y - yhat;
		pmod->uhat[t] = u;
		pmod->ess += u * u;
	    }
	}
    } else if (pmod->nwt) {
	for (t=0; t<fulln; t++) {
	    if (t < pmod->t1 || t > pmod->t2 || model_missing(pmod, t)) {
		pmod->yhat[t] = pmod->uhat[t] = NADBL;
	    } else {
		y = Z[yvar][t];
		if (dwt && Z[dwt][t] == 0.0) {
		    pmod->yhat[t] = NADBL;
		} else {
		    if (!dwt) {
			y *= Z[pmod->nwt][t];
		    }
		    yhat = 0.0;
		    for (i=2; i<=l0; i++) {
			yhat += pmod->coeff[i-2] * Z[pmod->list[i]][t];
		    }
		    pmod->yhat[t] = yhat;
		    pmod->uhat[t] = y - yhat;
		    pmod->ess += pmod->uhat[t] * pmod->uhat[t];
		}
	    }
	}
    } else {
	for (t=0; t<fulln; t++) {
	    if (t < pmod->t1 || t > pmod->t2 || model_missing(pmod, t)) {
		pmod->yhat[t] = pmod->uhat[t] = NADBL;
	    } else {
		yhat = 0.0;
		for (i=2; i<=l0; i++) {
		    yhat += pmod->coeff[i-2] * Z[pmod->list[i]][t];
		}
		pmod->yhat[t] = yhat;
		pmod->uhat[t] = Z[yvar][t] - yhat;
		pmod->ess += pmod->uhat[t] * pmod->uhat[t];
	    }
	}
    }

    /* if SSR is small enough, treat it as zero */
    if (pmod->ess < ESSZERO && pmod->ess > (-ESSZERO)) {
	pmod->ess = 0.0;
    } 
}

static void
svd_xpxinv (const gretl_matrix *A, const gretl_matrix *B,
	    const double *s, gretl_matrix *xpxinv)
{
    double aik, ajk, vij;
    int m = A->cols;
    int i, j, k;

    /* Get X'X{-1}, based on the work done by the SV decomp:
       reciprocals of the squares of the (positive) singular values,
       premultiplied by V and postmultiplied by V-transpose
    */

    for (i=0; i<m; i++) {
	for (j=i; j<m; j++) {
	    vij = 0.0;
	    for (k=0; k<m; k++) {
		if (s[k] > 0.0) {
		    aik = A->val[mdx(A, k, i)];
		    ajk = A->val[mdx(A, k, j)];
		    vij += aik * ajk / (s[k] * s[k]);
		}
	    }
	    xpxinv->val[mdx(xpxinv, i, j)] = vij;
	    if (j != i) {
		xpxinv->val[mdx(xpxinv, j, i)] = vij;
	    }
	}
    }	    
}

int gretl_svd_regress (MODEL *pmod, const double **Z, int fulln,
		       gretlopt opts)
{
    integer info, lwork, rank;
    integer m, n, lda, ldb;
    integer nrhs = 1;
    gretl_matrix *A = NULL, *B = NULL;
    gretl_matrix *b = NULL;
    gretl_matrix *xpxinv = NULL;
    doublereal *work = NULL, *s = NULL;
    doublereal *work2;
    doublereal rcond = -1.0;
    int i;
    int err = 0;

    m = pmod->nobs;               /* # of rows = # of observations */
    lda = m;                      /* leading dimension of A */
    ldb = m;                      /* leading dimension of B */
    n = pmod->list[0] - 1;        /* # of cols = # of variables */

    A = gretl_matrix_alloc(m, n);
    B = gretl_matrix_alloc(m, 1);

    work = malloc(sizeof *work);
    s = malloc(n * sizeof *s);

    if (A == NULL || B == NULL || work == NULL || s == NULL) {
	err = E_ALLOC;
	goto svd_cleanup;
    }

    get_model_data(pmod, Z, A, B);

    /* workspace query */
    lwork = -1;
    info = 0;
    dgelss_(&m, &n, &nrhs, A->val, &lda, B->val, &ldb, s, &rcond,
	    &rank, work, &lwork, &info);
    if (info != 0) {
	fprintf(stderr, "dgelss: info = %d\n", (int) info);
	err = 1;
	goto svd_cleanup;
    }	

    /* set up optimally sized work array */
    lwork = (integer) work[0];
    work2 = realloc(work, (size_t) lwork * sizeof *work);
    if (work2 == NULL) {
	err = E_ALLOC;
	goto svd_cleanup;
    }
    work = work2;

    /* get actual SVD solution */
    dgelss_(&m, &n, &nrhs, A->val, &lda, B->val, &ldb, s, &rcond,
	    &rank, work, &lwork, &info);
    if (info != 0) {
	fprintf(stderr, "dgelss: info = %d\n", (int) info);
	err = 1;
	goto svd_cleanup;
    }

    /* allocate temporary coeff vector */
    b = gretl_matrix_alloc(n, 1);
    if (b == NULL) {
	err = E_ALLOC;
	goto svd_cleanup;
    }

    /* allocate space in model struct */
    if (allocate_model_arrays(pmod, n, fulln)) {
	err = E_ALLOC;
	goto svd_cleanup;
    }

    /* copy and arrange OLS coefficients */
    for (i=0; i<n; i++) {
	b->val[i] = B->val[i];
    }
    save_coefficients(pmod, b, n);

    /* calculate fitted vals and residuals */
    svd_yhat_uhat(pmod, Z, fulln);

    /* standard error of regression */
    if (m - n > 0) {
	if (gretl_model_get_int(pmod, "no-df-corr")) {
	    pmod->sigma = sqrt(pmod->ess / m);
	} else {
	    pmod->sigma = sqrt(pmod->ess / (m - n));
	}
    } else {
	pmod->sigma = 0.0;
    }

    /* allocate and compute (X'X)^{-1} */
    xpxinv = gretl_matrix_alloc(n, n);
    if (xpxinv == NULL) {
	err = E_ALLOC;
	goto svd_cleanup;
    }
    svd_xpxinv(A, B, s, xpxinv);

    /* VCV and standard errors */
    if (opts & OPT_R) { 
	gretl_model_set_int(pmod, "robust", 1);
	if ((opts & OPT_T) && !get_force_hc()) {
	    qr_make_hac(pmod, Z, xpxinv);
	} else {
	    svd_make_hccme(pmod, Z, A, xpxinv);
	}
    } else {
	qr_make_regular_vcv(pmod, xpxinv);
    }

    gretl_matrix_free(A);
    A = NULL;

    /* get R^2 */
    qr_compute_r_squared(pmod, Z[pmod->list[1]], m);
    qr_compute_f_stat(pmod, opts);

 svd_cleanup:

    gretl_matrix_free(A);
    gretl_matrix_free(B);

    free(work);
    free(s);

    gretl_matrix_free(b);
    gretl_matrix_free(xpxinv);

    pmod->errcode = err;

    return err;    
}

int gretl_lapack_regress (MODEL *pmod, const double **Z, int fulln,
			  gretlopt opts)
{
    return gretl_svd_regress(pmod, Z, fulln, opts);
}

#else /* !USE_SVD */

int gretl_lapack_regress (MODEL *pmod, const double **Z, int fulln,
			  gretlopt opts)
{
    return gretl_qr_regress(pmod, Z, fulln, opts);
}

#endif

int qr_tsls_vcv (MODEL *pmod, const double **Z, gretlopt opts)
{
    integer info, lwork;
    integer m, n, lda;
    integer *iwork = NULL;
    gretl_matrix *Q = NULL, *R = NULL;
    gretl_matrix *xpxinv = NULL;
    doublereal *tau = NULL, *work = NULL;
    doublereal rcond;
    char uplo = 'U';
    char diag = 'N';
    char norm = '1';
    int i, j;
    int err = 0;

    m = pmod->nobs;               /* # of rows = # of observations */
    lda = m;                      /* leading dimension of Q */
    n = pmod->list[0] - 1;        /* # of cols = # of variables */

    Q = make_data_X(pmod, Z);
    if (Q == NULL) {
	err = E_ALLOC;
	goto qr_cleanup;
    }	

    /* dim of tau is min (m, n) */
    tau = malloc(n * sizeof *tau);
    work = malloc(sizeof *work);
    iwork = malloc(n * sizeof *iwork);

    if (tau == NULL || work == NULL || iwork == NULL) {
	err = E_ALLOC;
	goto qr_cleanup;
    }

    /* do a workspace size query */
    lwork = -1;
    info = 0;
    dgeqrf_(&m, &n, Q->val, &lda, tau, work, &lwork, &info);
    if (info != 0) {
	fprintf(stderr, "dgeqrf: info = %d\n", (int) info);
	err = 1;
	goto qr_cleanup;
    }

    /* set up optimally sized work array */
    lwork = (integer) work[0];
    work = realloc(work, (size_t) lwork * sizeof *work);
    if (work == NULL) {
	err = E_ALLOC;
	goto qr_cleanup;
    }

    /* run QR factorization */
    dgeqrf_(&m, &n, Q->val, &lda, tau, work, &lwork, &info);
    if (info != 0) {
	fprintf(stderr, "dgeqrf: info = %d\n", (int) info);
	err = 1;
	goto qr_cleanup;
    }

    /* check reciprocal condition number of R */
    dtrcon_(&norm, &uplo, &diag, &n, Q->val, &lda, &rcond, work, 
	    iwork, &info);
    if (info != 0) {
	fprintf(stderr, "dtrcon: info = %d\n", (int) info);
	err = 1;
	goto qr_cleanup;
    }

    if (rcond < QR_RCOND_MIN) {
	fprintf(stderr, "dtrcon: rcond = %g, but min is %g\n", rcond,
		QR_RCOND_MIN);
	err = E_SINGULAR;
	goto qr_cleanup;
    }

    /* allocate temporary auxiliary matrix */
    R = gretl_matrix_alloc(n, n);
    if (R == NULL) {
	err = E_ALLOC;
	goto qr_cleanup;
    }

    /* invert R */
    dtrtri_(&uplo, &diag, &n, Q->val, &lda, &info);
    if (info != 0) {
	fprintf(stderr, "dtrtri: info = %d\n", (int) info);
	err = 1;
	goto qr_cleanup;
    }

    /* copy the upper triangular R-inverse out of Q */
    for (i=0; i<n; i++) {
	for (j=0; j<n; j++) {
	    if (i <= j) {
		gretl_matrix_set(R, i, j, 
				 gretl_matrix_get(Q, i, j));
	    } else {
		gretl_matrix_set(R, i, j, 0.0);
	    }
	}
    }

    /* obtain the real "Q" matrix */
    dorgqr_(&m, &n, &n, Q->val, &lda, tau, work, &lwork, &info);
    if (info != 0) {
	fprintf(stderr, "dorgqr: info = %d\n", (int) info);
	err = 1;
	goto qr_cleanup;
    }

    /* create (X'X)^{-1} */
    xpxinv = gretl_matrix_alloc(n, n);
    gretl_matrix_multiply_mod(R, GRETL_MOD_NONE,
			      R, GRETL_MOD_TRANSPOSE,
			      xpxinv);

    /* VCV and standard errors */
    if (opts & OPT_R) { 
	gretl_model_set_int(pmod, "robust", 1);
	if (opts & OPT_T) {
	    qr_make_hac(pmod, Z, xpxinv);
	} else {
	    qr_make_hccme(pmod, Z, Q, xpxinv);
	}
    } else {
	qr_make_regular_vcv(pmod, xpxinv);
    }

 qr_cleanup:
    gretl_matrix_free(Q);

    free(tau); free(work);
    free(iwork);

    gretl_matrix_free(R);
    gretl_matrix_free(xpxinv);

    pmod->errcode = err;

    return err;    
}

