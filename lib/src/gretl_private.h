/* gretl - The Gnu Regression, Econometrics and Time-series Library
 * Copyright (C) 1999-2000 Ramu Ramanathan and Allin Cottrell
 *
 * This library is free software; you can redistribute it and/or
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

/* functions shared internally by library translation units */

#ifndef GRETL_PRIVATE_H
#define GRETL_PRIVATE_H

extern int newlag; /* transforms. c */

int dataset_stack_vars (double ***pZ, DATAINFO *pdinfo, 
			char *newvar, char *s);

void gretl_printxs (double xx, int n, int ci, PRN *prn);

void bufspace (int n, PRN *prn);

void gretl_print_ar (MODEL *pmod, PRN *prn);

void gretl_delete (char *str, int indx, int count);

int numeric_string (const char *str);

void gretl_trunc (char *str, size_t n);

int count_fields (const char *str);

void shift_left (char *str, size_t move);

double gretl_corr (int n, const double *x, const double *y);

double gretl_covar (int n, const double *x, const double *y);

int gretl_iszero (int t1, int t2, const double *x);

int gretl_isconst (int t1, int t2, const double *x);

double gretl_mean (int t1, int t2, const double *x);

void gretl_minmax (int t1, int t2, const double *x, 
		   double *min, double *max);

int gretl_hasconst (const int *list);

int gretl_compare_doubles (const void *a, const void *b);

int gretl_criteria (double ess, int nobs, int ncoeff, PRN *prn);

int calculate_criteria (double *x, double ess, int nobs, int ncoeff);

int adjust_t1t2 (MODEL *pmod, const int *list, int *t1, int *t2, 
		 const double **Z, int *misst);

int undo_daily_repack (MODEL *pmod, double **Z, 
		       const DATAINFO *pdinfo);

int repack_missing_daily_obs (MODEL *pmod, double **Z, 
			      const DATAINFO *pdinfo);

void set_reference_mask (const MODEL *pmod);

int model_mask_leaves_balanced_panel (const MODEL *pmod,
				      const DATAINFO *pdinfo);

int list_dups (const int *list, int ci);

int *augment_regression_list (const int *orig, int aux, 
			      double ***pZ, DATAINFO *pdinfo);

int gretl_forecast (int t1, int t2, int nv, 
		    const MODEL *pmod, double ***pZ);

int gretl_is_reserved (const char *str);

int vars_identical (const double *x, const double *y, int n);

int real_list_laggenr (const int *list, double ***pZ, 
		       DATAINFO *pdinfo, int maxlag, 
		       int **lagnums);

int lagvarnum (int v, int l, const DATAINFO *pdinfo);

int get_genr_function (const char *s);

void gretl_varinfo_init (VARINFO *vinfo);

double corrrsq (int nobs, const double *y, const double *yhat);

int get_hac_lag (int m);

int get_hc_version (void);

int get_use_qr (void);

char *copy_subdum (const char *src, int n);

void maybe_free_full_dataset (const DATAINFO *pdinfo);

int get_vcv_index (MODEL *pmod, int i, int j, int n);

int path_append (char *file, const char *path);

void gretl_list_diff (int *targ, const int *biglist, const int *sublist);

int takenotes (int quit_opt);

char *get_month_name (char *mname, int m);

int gretl_function_stack_depth (void);

/* init and cleanup functions */

int gretl_cmd_init (CMD *cmd);

void gretl_cmd_free (CMD *cmd);

void gretl_functions_cleanup (void);

void gretl_rand_init (void);

void gretl_rand_free (void);

double *testvec (int n);

#endif /* GRETL_PRIVATE_H */
