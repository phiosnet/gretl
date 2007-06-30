/* gretl - The Gnu Regression, Econometrics and Time-series Library
 * Copyright (C) 1999-2006 Allin Cottrell and Riccardo "Jack" Lucchetti
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License 
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This software is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License 
 * along with this software; if not, write to the 
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

/*
  Modular GARCH routines by Jack Lucchetti, October 2006. For the 
  moment, meant to replace seamlessly fcp.c, but syntax should evolve in 
  the future.
*/

#include "libgretl.h"
#include "libset.h"    /* unused */
#include "garch.h"

#define GDEBUG 0

enum {
    INIT_VAR_THEO,
    INIT_VAR_OLS,
    INIT_VAR_RESID
};

enum {
    DIST_NORM,
    DIST_T
};

typedef struct garch_container_ garch_container;

struct garch_container_ {
    const double *y;     /* dependent variable */
    const double **X;    /* regressors (constant excluded) */
    int t1;              /* beginning of sample */
    int t2;              /* end of sample */
    int nobs;            /* number of observations */
    int ncm;             /* number of regressors (constant excluded) */
    int p;               /* GARCH p */
    int q;               /* GARCH q */
    int k;               /* total number of parameters */
    int init;            /* h0 initialisation method */
    int distrib;         /* innovations distribution (only Gaussian for now) */
    double *e;           /* residuals */
    double *e2;          /* squared residuals */
    double *h;           /* conditional variance */
    int ascore;          /* 1 for analytical score provided */
    double **score_e;    /* derivatives of the residuals wrt the parameters */
    double **score_h;    /* derivatives of the variances wrt the parameters */
    double **blockglue;  /* derivatives of the loglik wrt residuals and variances */
    double **G;          /* score matrix */
    double *tot_score;   /* score vector (sum of G) */
    double scale;        /* scale factor for dependent var */
    int boundcheck;      /* enable bounds check */
};

#if 0
static void mark (int *n) 
{ 
    if (n == NULL) {
	fprintf(stderr,"Ha!\n"); 
    } else {
	fprintf(stderr,"Ha! (%d)\n", *n); 
	*n += 1;
    }
}
#endif

#define MOD_DEBUG 0

static void free_eh_derivs (garch_container *DH)
{
    doubles_array_free(DH->score_e, DH->k);
    doubles_array_free(DH->score_h, DH->k);
    doubles_array_free(DH->G, DH->k);
    doubles_array_free(DH->blockglue, 2);
}

static int allocate_eh_derivs (garch_container *DH)
{
    int k = DH->k;
    int n = DH->nobs;
    int err = 0;

    DH->score_e = doubles_array_new(k, n);
    DH->score_h = doubles_array_new(k, n);
    DH->G = doubles_array_new(k, n);
    DH->blockglue = doubles_array_new(2, n);

    if (DH->score_e == NULL ||
	DH->score_h == NULL ||
	DH->G == NULL ||
	DH->blockglue == NULL) {
	free_eh_derivs(DH);
	err = E_ALLOC;
    } 

    return err;
}

static garch_container *
garch_container_new (const double *y, const double **X, 
		     int t1, int t2, int nobs, int nc,
		     int p, int q, int init_method, 
		     double *e, double *e2, double *h, 
		     double scale, int analytical)
{
    garch_container *DH = malloc(sizeof *DH);

    if (DH == NULL) {
	return NULL;
    }

    DH->y = y;
    DH->X = X;
    DH->t1 = t1;
    DH->t2 = t2;
    DH->nobs = nobs;
    DH->ncm = nc - 1;
    DH->p = p;
    DH->q = q;
    DH->init = init_method;
    DH->e = e;
    DH->e2 = e2;
    DH->h = h;
    DH->ascore = analytical;
    DH->k = nc + 1 + p + q;
    DH->boundcheck = 1;
    DH->scale = scale;

    DH->score_e = NULL;
    DH->score_h = NULL;
    DH->G = NULL;
    DH->blockglue = NULL;

    if (DH->ascore) {
	if (allocate_eh_derivs(DH)) {
	    free(DH);
	    DH = NULL;
	}
    }

    return DH;
}

static void garch_container_destroy (garch_container *DH)
{
    if (DH->ascore) {
	free_eh_derivs(DH);
    }
    free(DH);
}

/* *ARCH log-likelihood for Gaussian innovations */

static double normal_ll (const garch_container *DH)
{
    double e2t, ht, ll = 0.0;
    int t;

    for (t=DH->t1; t<=DH->t2; t++) {
	e2t = DH->e2[t];
	ht = DH->h[t];
	if (na(e2t) || na(ht)) {
	    return NADBL;
	}
	ll -= log(ht) + e2t / ht;
    }

    ll *= 0.5;
    ll -= (DH->t2 - DH->t1 + 1) * LN_SQRT_2_PI;

    return ll;
} 

static void normal_score (const garch_container *DH)
{
    double ut;
    int t;

    for (t=DH->t1; t<=DH->t2; t++) {
	DH->blockglue[0][t] = ut = -DH->e[t] / DH->h[t];
	DH->blockglue[1][t] = 0.5 * (ut * ut - 1.0 / DH->h[t]);
    }
} 

/* Compute the GARCH quantities */

static int check_nonnegative (const double *par, int ncm, int k)
{
    int nonzero = 1;
    int i;
    double sum = 0.0;

    nonzero = (par[ncm+1] >= 0.0);

    for (i=ncm+2; i<k; i++) {
	nonzero &= (par[i] >= 1.0e-12);
	sum += par[i];
	if (!nonzero) {
	    break;
	}
    }

    nonzero &= (sum <= 1.0);

    return nonzero;
}

static int garch_etht (const double *par, void *ptr)
{
    garch_container *DH = (garch_container *) ptr;

    int t1 = DH->t1;
    int t2 = DH->t2;
    int p = DH->p;
    int q = DH->q;

    int maxlag = (p > q)? p : q;

    int i, j, k, ret = 0;
    int ncm = DH->ncm;

    double **dedq = DH->score_e;
    double **dhdq = DH->score_h;

    int t, T = t2 - t1 + 1;
    double et, ht, tmp, h0 = 0.0;
    double u_var = 0.0;

    /* check for nonnegative params */

    if (DH->boundcheck) {
	ret = !check_nonnegative(par, ncm, DH->k);
	if (ret) {
	    return E_DATA;
	}
    }

    /* compute residuals */

    tmp = 0.0;
    for (t = t1-maxlag; t <= t2; t++) {
	if (t < t1) {
	    et = 0.0;
	} else {
	    et = DH->y[t] - par[0];
	    if (DH->X != NULL) {
		for (i=1; i<=ncm; i++) {
		    et -= DH->X[i][t]*par[i];
		}
	    }
	    DH->e[t] = et;
	    DH->e2[t] = et * et;
	    tmp += DH->e2[t]; 
	}
    }

    if (DH->ascore) {
	for (t=t-maxlag; t<t1; t++) {
	    for (i=0; i<DH->k; i++) {
		dedq[i][t] = 0.0;
	    }
	}
    }
	
    /* h0 and derivatives */

    switch (DH->init) {
    case INIT_VAR_OLS:
	h0 = 1.0;
	break;
    case INIT_VAR_RESID:
	h0 = tmp / T;
	break;
    case INIT_VAR_THEO:
	tmp = 1.0;
	for (i=ncm+2; i<DH->k; i++) {
	    tmp -= par[i];
	}
	u_var = par[ncm+1] / tmp;
	h0 = u_var;
	break;
    }

    for (t=t1-maxlag; t<t1; t++) {
	DH->h[t] = h0;
	DH->e2[t] = h0;
    }

    if (DH->ascore) {
	double dh0;

	switch (DH->init) {
	case INIT_VAR_OLS:
	    for (t=t1-maxlag; t<t1; t++) {
		for (i=0; i<DH->k; i++) {
		    dhdq[i][t] = 0.0;
		}
	    }
	    break;

	case INIT_VAR_RESID:
	    dh0 = 0.0;
	    for (t=t1; t<=t2; t++) {
		dh0 -= DH->e[t];
	    }
	    for (t=t1-maxlag; t<t1; t++) {
		dhdq[0][t] = dh0  * 2.0 / T;
	    }
	    
	    for (i=0; i<ncm; i++) {
		dh0 = 0.0;
		for (t=t1; t<=t2; t++) {
		    dh0 -= DH->e[t] * DH->X[i+1][t];
		}
		for (t=t1-maxlag; t<t1; t++) {
		    dhdq[i+1][t] = dh0  * 2.0 / T;
		}
	    }
	    
	    for (t=t1-maxlag; t<t1; t++) {
		for (i=ncm+1; i<DH->k; i++) {
		    dhdq[i][t] = 0.0;
		}
	    }
	    
	    break;
	    
	case INIT_VAR_THEO:
	    for (t=t1-maxlag; t<t1; t++) {
		for (i=0; i<=ncm; i++) {
		    dhdq[i][t] = 0.0;
		}
	    }
	    dh0 = u_var / par[ncm+1];
	    for (t=t1-maxlag; t<t1; t++) {
		dhdq[ncm+1][t] = dh0;
	    }
	    dh0 *= u_var;
	    for (t=t1-maxlag; t<t1; t++) {
		for (i=ncm+2; i<DH->k; i++) {
		    dhdq[i][t] = dh0;
		}
	    }
	    break;
	}
	
    }

    /* in-sample loop */

    for (t=t1; t<=t2; t++) {
	ht = par[ncm+1];

	for (i=1; i<=p; i++) {
	    ht += DH->e2[t-i] * par[ncm+i+1];
	}

	for (i=1; i<=q; i++) {
	    ht += DH->h[t-i] * par[ncm+i+p+1];
	}
	
	DH->h[t] = ht;
	    
	if (DH->ascore) {
	    
	    /* constant */
	    dedq[0][t] = -1.0;
	    k = ncm+1;
	    dhdq[0][t] = 0.0;
	    for (i=1; i<=p; i++) {
		if (t - p < t1 && DH->init == INIT_VAR_RESID) {
		    dhdq[0][t] += par[k+i] * dhdq[0][t1-1];
		} else {	
		    dhdq[0][t] += 2.0 * par[k+i] * DH->e[t-i] * dedq[0][t-i];
		}
	    }
	    
	    /* regressors */
	    for (i=1; i<=ncm; i++) {
		dedq[i][t] = -(DH->X[i][t]);
		k = ncm+1;
		dhdq[i][t] = 0.0;
		for (j=1; j<=p; j++) {
		    if (t - p < t1 && DH->init == INIT_VAR_RESID) { 
			// add INIT_THEO here
			dhdq[i][t] += par[k+j] * dhdq[i][t1-1];
		    } else {	
			dhdq[i][t] += 2.0 * par[k+j] * DH->e[t-j] * dedq[i][t-j];
		    }
		}
	    }
	    
	    /* garch params: omega */
	    dedq[ncm+1][t] = 0.0;
	    dhdq[ncm+1][t] = 1.0;
	    if (t - p < t1 && DH->init == INIT_VAR_THEO) {
		for (i=1; i<=p; i++) {
		    dhdq[ncm+1][t] += par[ncm+1+i] * dhdq[ncm+1][t1-1];
		}
	    }
	    
	    /* garch params: alphas */
	    k = ncm + 2;
	    for (i=1; i<=p; i++) {
		dedq[k][t] = 0.0;
		dhdq[k][t] = DH->e2[t-i];
		if (t - p < t1 && DH->init == INIT_VAR_THEO) {
		    for (j=0; j<p; j++) {
			dhdq[k][t] += par[k+j] * dhdq[k][t1-1];
		    }
		}
		k++;
	    }
	    
	    /* garch params: betas */
	    k = ncm + p + 2;
	    for (i=1; i<=q; i++) {
		dedq[k][t] = 0.0;
		dhdq[k][t] = DH->h[t-i];
		if (t - p < t1 && DH->init == INIT_VAR_THEO) {
		    for (j=0; j<p; j++) {
			dhdq[k][t] += par[k+j-p] * dhdq[k][t1-1];
		    }
		}
		k++;
	    }
	    
	    /* "real" recursive part */
	    for (i=0; i<DH->k; i++) {
		k = ncm + p + 2;
		for (j=1; j<=q; j++) {
		    dhdq[i][t] += par[k++] * dhdq[i][t-j];
		}
	    }
	    
	}	
    }
    
#if MOD_DEBUG
    fputs("\n\n", stderr);
    for (i=0; i<DH->k; i++) {
	fprintf(stderr, "garch_etht: par[%d] = %9.6f ", i, par[i]);
    }
    fputc('\n', stderr);
    for (t=t1-maxlag; t<=20; t++) {
	if (t < t1) {
	    fputc('*', stderr); 
	} else {
	    fputc(' ', stderr);
	}
	fprintf(stderr, " t:%4d ", t);
	fprintf(stderr, " %8.4f", DH->e[t]);
	fprintf(stderr, " %8.4f", DH->e2[t]);
	fprintf(stderr, " %8.4f", DH->h[t]);
	fprintf(stderr, " %12.8f", dedq[ncm+2][t]);
	fprintf(stderr, " %12.8f", dhdq[ncm+2][t]);
	fputc('\n', stderr);
    }
#endif

    return ret;
} 

static double loglik (const double *theta, void *ptr)
{
    garch_container *DH = (garch_container *) ptr;
    double ll = NADBL;
    int err;

    err = garch_etht(theta, DH);
    if (!err) {
	ll = normal_ll(DH);
    }

    return ll;
}

static int score_fill_matrices (const double *theta, void *ptr)
{
    garch_container *DH = (garch_container *) ptr;
    int i, t, err;

    err = garch_etht(theta, DH);
    if (err) {
	return err;
    }

    normal_score(DH);

    for (t=DH->t1; t<=DH->t2; t++) {
	for (i=0; i<DH->k; i++) {
	    DH->G[i][t] = DH->score_e[i][t] * DH->blockglue[0][t] + 
		DH->score_h[i][t] * DH->blockglue[1][t];
	}
    }

    return err;
}

static int anal_score (double *theta, double *s, int npar, BFGS_CRIT_FUNC ll, 
		       void *ptr)
{
    garch_container *DH = (garch_container *) ptr;
    double tmp;
    int t, i, err;

    err = score_fill_matrices(theta, DH);
    if (err) {
	return err;
    }

    for (i=0; i<npar; i++) {
	tmp = 0.0;
	for (t=DH->t1;t<=DH->t2; t++) {
	    tmp += DH->G[i][t];
	}
	s[i] = tmp;
    }

    return err;
}

static int garch_iinfo (garch_container *DH, gretl_matrix *info)
{
    double **tmp_info;
    double tmpi, tmpj, tmpx1, tmpx2, x;
    int i, j, t;

    if (info == NULL) {
	return E_ALLOC;
    }

    tmp_info = doubles_array_new(DH->k, DH->k);
    if (tmp_info == NULL) {
	return E_ALLOC;
    }

    for (i=0; i<DH->k; i++) {
	for (j=0; j<=i; j++) {
	    tmp_info[i][j] = 0.0;
	}
    }

    for (t=DH->t1; t<=DH->t2; t++) {
	for (i=0; i<=DH->ncm; i++) {
	    tmpi = DH->score_h[i][t] / DH->h[t];
	    tmpx1 = (i==0)? 2.0 : 2.0*(DH->X[i][t]);
	    tmpx1 /= DH->h[t];
	    for (j=0; j<=i; j++) {
		tmpj = DH->score_h[j][t] / DH->h[t];
		tmpx2 = (j==0)? 1.0 : 1.0*(DH->X[j][t]);
		x = tmpx1 * tmpx2 + tmpi * tmpj;
		tmp_info[i][j] += x;
	    }
	}

	for (i=DH->ncm+1; i<DH->k; i++) {
	    tmpi = DH->score_h[i][t] / DH->h[t];
	    for (j=DH->ncm+1; j<=i; j++) {
		tmpj = DH->score_h[j][t] / DH->h[t];
		x = tmpi * tmpj;
		tmp_info[i][j] += x;
	    }
	}
    }

    for (i=0; i<DH->k; i++) {
	for (j=0; j<=i; j++) {
	    gretl_matrix_set(info, i, j, 0.5*tmp_info[i][j]);
	    if (j < i) {
		gretl_matrix_set(info, j, i, 0.5*tmp_info[i][j]);
	    }
	}
    }

    doubles_array_free(tmp_info, DH->k);

#if GDEBUG
    gretl_matrix_print(info, "Information matrix");
#endif

    gretl_invert_symmetric_matrix(info);

#if GDEBUG
    gretl_matrix_print(info, "Information matrix (inverse)");
#endif

    return 0;
}

static int garch_opg (garch_container *DH, gretl_matrix *GG)
{
    double **tmp_GG;
    double tmpi, x;
    int t, i, j;

    if (GG == NULL) {
	return E_ALLOC;
    }

    tmp_GG = doubles_array_new(DH->k, DH->k);
    if (tmp_GG == NULL) {
	return E_ALLOC;
    }

    for (i=0; i<DH->k; i++) {
	for (j=0; j<DH->k; j++) {
	    tmp_GG[i][j] = 0.0;
	}
    }

    for (t=DH->t1; t<=DH->t2; t++) {
	for (i=0; i<DH->k; i++) {
	    tmpi = DH->G[i][t];
	    for (j=0; j<=i; j++) {
		x = tmpi * DH->G[j][t]; 
		tmp_GG[i][j] += x;
	    }
	}
    }

    for (i=0; i<DH->k; i++) {
	for (j=0; j<=i; j++) {
	    gretl_matrix_set(GG, i, j, tmp_GG[i][j]);
	    if (j < i) {
		gretl_matrix_set(GG, j, i, tmp_GG[i][j]);
	    }
	}
    }

    doubles_array_free(tmp_GG, DH->k);

#if GDEBUG
    gretl_matrix_print(GG, "OPG matrix");
#endif

    return 0;
}

#define ANALYTICAL_HESSIAN 1

#if ANALYTICAL_HESSIAN

static gretl_matrix * 
garch_ihess (garch_container *DH, double *theta, int *err)
{
    gretl_matrix *H;

    H = garch_analytical_hessian(DH->y, DH->X, DH->t1, DH->t2, DH->nobs, 
				 DH->ncm + 1, DH->p, DH->q, theta, DH->e, 
				 DH->e2, DH->h, DH->scale, err);

    return H;
}

#else

static gretl_matrix *
garch_ihess (garch_container *DH, double *theta, int *err)
{
    gretl_matrix *iH;
    double vij, *V;
    int i, j, k, npar = DH->k;
    int tmp = DH->boundcheck;

    iH = gretl_matrix_alloc(npar, npar);
    if (iH == NULL) {
	*err = E_ALLOC;
	return NULL;
    }

    DH->boundcheck = 0;
    V = numerical_hessian(theta, npar, loglik, DH, err);
    DH->boundcheck = tmp;

    if (*err) {
	return NULL;
    }

    k = 0;
    for (i=0; i<npar; i++) {
	for (j=i; j<npar; j++) {
	    vij = V[k++];
	    gretl_matrix_set(iH, i, j, vij);
	    if (i != j) {
		gretl_matrix_set(iH, j, i, vij);
	    }
	}
    }

# if GDEBUG
    gretl_matrix_print(iH, "Hessian (inverse)");
# endif

    free(V);

    return iH;
}

#endif

static int
garch_covariance_matrix (int vopt, double *theta, garch_container *DH,
			 gretl_matrix *V) 
{
    gretl_matrix *GG = NULL;
    gretl_matrix *iinfo = NULL;
    gretl_matrix *invhess = NULL;
    int npar = DH->k;
    int err = 0;

    if (vopt == VCV_OP || vopt == VCV_QML || vopt == VCV_BW) { 
	/* GG' needed */
	GG = gretl_matrix_alloc(npar, npar);
	err = garch_opg(DH, GG);
    }

    if (vopt == VCV_IM || vopt == VCV_BW) { 
	/* information matrix needed */
	iinfo = gretl_matrix_alloc(npar, npar);
	err = garch_iinfo(DH, iinfo);
    }

    if (vopt == VCV_QML || vopt == VCV_HESSIAN) { 
	/* Hessian matrix needed */
	invhess = garch_ihess(DH, theta, &err);
    }

    if (err) {
	goto bailout;
    }

    switch (vopt) {
    case VCV_HESSIAN:
	gretl_matrix_copy_values(V, invhess);
	break;
    case VCV_IM:
	gretl_matrix_copy_values(V, iinfo);
	break;
    case VCV_OP:
	gretl_matrix_copy_values(V, GG);
	err = gretl_invert_symmetric_matrix(V);
	break;
    case VCV_BW:
	gretl_matrix_qform(iinfo, GRETL_MOD_NONE, GG,
			   V, GRETL_MOD_NONE);
	break;
    case VCV_QML:
	gretl_matrix_qform(invhess, GRETL_MOD_NONE, GG,
			   V, GRETL_MOD_NONE);
	break;
    default: 
	break;
    }

#if GDEBUG
    gretl_matrix_print(V, "Variance-covariance matrix");
#endif

 bailout:

    gretl_matrix_free(GG);
    gretl_matrix_free(iinfo);
    gretl_matrix_free(invhess);

    return err;
}

#if GDEBUG

static void test_score (garch_container *DH, double *theta)
{
    double *testa, *testn;
    int i, testret;

    testa = malloc(DH->k * sizeof *testa);
    testn = malloc(DH->k * sizeof *testn);

    testret = anal_score(theta, testa, DH->k, loglik, DH);
    testret = BFGS_numeric_gradient(theta, testn, DH->k, loglik, DH);

    fprintf(stderr, "ret = %d:\n", testret);

    for (i=0; i<DH->k; i++) {
	fprintf(stderr, "g[%d]: analytical = %14.8f, numerical: = %14.8f, \n", 
		i, testa[i], testn[i]);
    }
    fputc('\n', stderr);

    free(testa);
    free(testn);
}

#endif

/*
   Parameters to garch_estimate_mod()

   y:     the dependent variable
   X:     the independent regressors (including the constant)
   t1:    beginning of sample relative to the arrays y, X
   t2:    end of sample
   nobs:  total number of observations in y, X
   nc:    number of columns in X
   p:     number of 'beta' variance params
   q:     number of 'alpha' variance params (excluding the constant)
   theta: full parameter vector, pre-initialized; on output, the
          estimates at convergence.
   V:     covariance matrix of parameters (all 0 on input)
   e:     vector of 0's on input, resids on output
   e2:    storage for squared residuals on output
   h:     storage conditional variances on output
   scale: factor used to scale the dependent variable
   pll:   location to receive log-likelihood on output
   fncount: 0 on input, holds number of function evaluations on output
   fncount: 0 on input, holds number of gradient evaluations on output
   vopt:  code indicating which version of the covariance
          matrix to compute in V
   prn:   print handle for info on iterations etc.

*/

int garch_estimate_mod (const double *y, const double **X,
			int t1, int t2, int nobs, int nc,
			int p, int q, double *theta,  gretl_matrix *V,
			double *e, double *e2, double *h,
			double scale, double *pll, 
			int *fncount, int *grcount,
			int vopt, PRN *prn)
{
    garch_container *DH;
    int npar = nc + 1 + p + q; 
    int analytical = 1;
    int err = 0;

    /* BFGS apparatus */
    int maxit = 10000;
    double reltol = 1.0e-13;

    DH = garch_container_new(y, X, t1, t2, nobs, nc, p, q, 
			     INIT_VAR_RESID, e, e2, h, 
			     scale, analytical);

    if (DH == NULL) {
	return E_ALLOC;
    }

    theta[0] = gretl_mean(t1, t2, y);

#if 0
    if (DH->ascore) {
	fputs("\nUsing analytical score\n", stderr);
    } else {
	fputs("\nUsing numerical score\n", stderr);
    } 
#endif

    err = BFGS_max(theta, npar, maxit, reltol, 
		   fncount, grcount, loglik, C_LOGLIK,
		   (DH->ascore)? anal_score : NULL, 
		   DH, (prn != NULL)? OPT_V : OPT_NONE, 
		   prn);
    
    *pll = loglik(theta, DH) - (t2 - t1 + 1) * log(scale);
    
#if GDEBUG
    test_score(DH, theta);
#endif

    err = garch_covariance_matrix(vopt, theta, DH, V);

    garch_container_destroy(DH);

    return err;
}
