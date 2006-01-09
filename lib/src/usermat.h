/*
 *  Copyright (c) by Allin Cottrell
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

#ifndef USERMAT_H_
#define USERMAT_H_

gretl_matrix *get_matrix_by_name (const char *name);

int add_or_replace_user_matrix (gretl_matrix *M, const char *name);

void destroy_user_matrices (void);

int destroy_user_matrices_at_level (int level);

int is_user_matrix (gretl_matrix *m);

int matrix_command (const char *line, double ***pZ, DATAINFO *pdinfo, PRN *prn);

gretl_matrix *matrix_calc_AB (gretl_matrix *A, gretl_matrix *B, 
			      char op, int *err);

double user_matrix_get_determinant (gretl_matrix *m);

gretl_matrix *user_matrix_get_determinant_as_matrix (gretl_matrix *m);

gretl_matrix *user_matrix_get_inverse (gretl_matrix *m);

gretl_matrix *user_matrix_get_log_matrix (gretl_matrix *m);

#endif /* USERMAT_H_ */
