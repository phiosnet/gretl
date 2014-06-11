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

#ifndef GRETL_ARRAY_H_
#define GRETL_ARRAY_H_

typedef struct gretl_array_ gretl_array;

void gretl_array_destroy (gretl_array *A);

gretl_array *gretl_array_new (GretlType type, int n, int *err);

void *gretl_array_get_member (gretl_array *A, int i, 
			      GretlType *type,
			      int *err);

int gretl_array_set_string (gretl_array *A, int i, 
			    char *s, int copy);

int gretl_array_append_string (gretl_array *A,
			       char *s,
			       int copy);

int gretl_array_set_matrix (gretl_array *A, int i, 
			    gretl_matrix *m,
			    int copy);

int gretl_array_append_matrix (gretl_array *A,
			       gretl_matrix *m,
			       int copy);

int gretl_array_set_bundle (gretl_array *A, int i, 
			    gretl_bundle *b,
			    int copy);

int gretl_array_append_bundle (gretl_array *A,
			       gretl_bundle *b,
			       int copy);

gretl_array *gretl_array_copy (const gretl_array *A,
			       int *err);

gretl_array *get_array_by_name (const char *name);

gretl_array *gretl_array_pull_from_stack (const char *name,
					  int *err);

#endif /* GRETL_ARRAY_H_ */