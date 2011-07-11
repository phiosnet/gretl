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

#ifndef PVALUES_H
#define PVALUES_H

double gamma_function (double x);

double ln_gamma (double x);

double digamma (double x);

double binomial_cdf (double p, int n, int k);

double binomial_cdf_comp (double p, int n, int k);

double binomial_pmf (double p, int n, int k);

double poisson_pmf (double lambda, int k);

double x_factorial (double x);

double log_x_factorial (double x);

double normal_pvalue_2 (double x);

double normal_pvalue_1 (double x);

double student_pvalue_2 (double df, double x);

double student_pvalue_1 (double df, double x);

double chisq_cdf (int df, double x);

double chisq_cdf_comp (int df, double x);

double snedecor_cdf_comp (int dfn, int dfd, double x);

double snedecor_critval (int dfn, int dfd, double a);

double normal_cdf (double x);

double normal_cdf_inverse (double x);

double normal_cdf_comp (double x);

double student_cdf_inverse (double df, double a);

double normal_pdf (double x);

double normal_critval (double a);

double student_critval (double df, double a);

double log_normal_pdf (double x);

double invmills (double x);

double bvnorm_cdf (double rho, double a, double b);

double gamma_cdf (double s1, double s2, double x, int control);

double gamma_cdf_comp (double s1, double s2, double x, int control);

double gamma_cdf_inverse (double shape, double scale, double p);

double GED_pdf (double nu, double x);

double GED_cdf (double nu, double x);

double GED_cdf_comp (double nu, double x);

double GED_cdf_inverse (double nu, double a);

double tcrit95 (int df);

double rhocrit95 (int n);

double cephes_gamma (double x);

double cephes_lgamma (double x); 

double gretl_get_pvalue (char st, const double *parm, double x);

double gretl_get_pdf (char st, const double *parm, double x);

int gretl_fill_pdf_array (char st, const double *parm, double *x, int n);

double gretl_get_cdf (char st, const double *parm, double x);

double gretl_get_cdf_inverse (char st, const double *parm, double a);

double gretl_get_critval (char st, const double *parm, double a);

double *gretl_get_random_series (char st, const double *parm,
				 const double *serp1, 
				 const double *serp2, 
				 const DATASET *dset,
				 int *err);

int batch_pvalue (const char *str, DATASET *dset, PRN *prn);

void print_pvalue (char st, const double *parm, double x, double pv, PRN *prn);

void print_critval (char st, const double *parm, double a, double c, PRN *prn);

gretl_matrix *gretl_get_DW (int n, int k, int *err);

#endif /* PVALUES_H */


