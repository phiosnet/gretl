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

#include "libgretl.h"
#include "gretl_matrix.h"
#include "matrix_extra.h"
#include "gretl_func.h"
#include "libset.h"
#include "usermat.h"
#include "gretl_xml.h"

#define MDEBUG 0
#define LEVEL_AUTO -1

struct user_matrix_ {
    gretl_matrix *M;
    int level;
    char name[VNAMELEN];
};

static user_matrix **matrices;
static int n_matrices;

#if MDEBUG > 1
static void print_matrix_stack (const char *msg)
{
    int i;

    fprintf(stderr, "\nmatrix stack, %s:\n", msg);
    for (i=0; i<n_matrices; i++) {
	if (matrices[i] == NULL) {
	    fprintf(stderr, " %d: NULL\n", i);
	} else {
	    fprintf(stderr, " %d: '%s' at %p (%d x %d)\n",
		    i, matrices[i]->name, (void *) matrices[i]->M,
		    matrices[i]->M->rows, matrices[i]->M->cols);
	}
    }
    fputc('\n', stderr);
}
#endif

int n_user_matrices (void)
{
    return n_matrices;
}

const char *get_matrix_name_by_index (int idx)
{
    if (idx >= 0 && idx < n_matrices) {
	return matrices[idx]->name;
    } else {
	return NULL;
    }
}

static user_matrix *user_matrix_new (gretl_matrix *M, const char *name)
{
    user_matrix *u;

    u = malloc(sizeof *u);
    if (u == NULL) {
	return NULL;
    }

    u->M = M;
    u->level = gretl_function_depth();
    *u->name = '\0';
    strncat(u->name, name, VNAMELEN - 1);

    return u;
}

static int matrix_is_user_matrix (const gretl_matrix *m)
{
    int i;

    for (i=0; i<n_matrices; i++) {
	if (m == matrices[i]->M) {
	    return 1;
	}
    }

    return 0;
}

/* callback for adding icons representing matrices to the GUI
   session window */

static void (*matrix_add_callback)(void);

void set_matrix_add_callback (void (*callback))
{
    matrix_add_callback = callback; 
}

static int real_user_matrix_add (gretl_matrix *M, const char *name,
				 int callback_ok)
{
    user_matrix **tmp;

    if (M == NULL) {
	return 0;
    }

#if 0
    if (check_varname(name)) {
	return E_DATA;
    }
#endif

    tmp = realloc(matrices, (n_matrices + 1) * sizeof *tmp);
    if (tmp == NULL) {
	return E_ALLOC;
    } else {
	matrices = tmp;
    }

    if (matrix_is_user_matrix(M)) {
	/* ensure uniqueness of matrix pointers */
	gretl_matrix *Mcpy = gretl_matrix_copy(M);

	if (Mcpy == NULL) {
	    return E_ALLOC;
	}
	M = Mcpy;
    }

    matrices[n_matrices] = user_matrix_new(M, name);

    if (matrices[n_matrices] == NULL) {
	return E_ALLOC;
    }

    n_matrices++;

#if MDEBUG
    fprintf(stderr, "add_user_matrix: allocated '%s' at %p (M at %p, %dx%d)\n",
	    name, (void *) matrices[n_matrices-1], (void *) M,
	    gretl_matrix_rows(M), gretl_matrix_cols(M));
#endif

    if (callback_ok && 
	matrix_add_callback != NULL && 
	gretl_function_depth() == 0) {
	(*matrix_add_callback)();
    }

    return 0;
}

int user_matrix_add (gretl_matrix *M, const char *name)
{
    return real_user_matrix_add(M, name, 1);
}

int matrix_copy_add (gretl_matrix *M, const char *name)
{
    return real_user_matrix_add(M, name, 0);
}

static int 
matrix_insert_diagonal (gretl_matrix *M, const gretl_matrix *S,
			int mr, int mc)
{
    int i, n = gretl_vector_get_length(S);
    int k = (mr < mc)? mr : mc;

    if (n != k) {
	return E_NONCONF;
    }

    for (i=0; i<n; i++) {
	gretl_matrix_set(M, i, i, S->val[i]);
    }
    
    return 0;
}

/* If slevel is LEVEL_AUTO, search at the current function execution
   depth, otherwise search at the function execution depth given by
   slevel.
*/

static gretl_matrix *
real_get_matrix_by_name (const char *name, int slevel)
{
    int level, i;

    if (slevel == LEVEL_AUTO) {
	level = gretl_function_depth();
    } else {
	level = slevel;
    }

    for (i=0; i<n_matrices; i++) {
	if (!strcmp(name, matrices[i]->name) &&
	    matrices[i]->level == level) {
	    return matrices[i]->M;
	}
    }

    return NULL;
}

user_matrix *get_user_matrix_by_name (const char *name)
{
    int level = gretl_function_depth();
    int i;

    for (i=0; i<n_matrices; i++) {
	if (!strcmp(name, matrices[i]->name) &&
	    matrices[i]->level == level) {
	    return matrices[i];
	}
    }

    return NULL;
}

user_matrix *get_user_matrix_by_index (int idx)
{
    if (idx >= 0 && idx < n_matrices) {
	return matrices[idx];
    } else {
	return NULL;
    }
}

int user_matrix_replace_matrix (user_matrix *u, gretl_matrix *M)
{
    if (u == NULL) {
	return E_UNKVAR;
    }

    if (M != u->M) {
#if MDEBUG
	fprintf(stderr, " freeing u->M at %p, replacing with "
		"matrix at %p\n", u->M, M);
#endif
	gretl_matrix_free(u->M);
	u->M = M;
    }

    return 0;
}

int user_matrix_adjust_level (user_matrix *u, int adj)
{
    if (u == NULL) {
	return E_UNKVAR;
    }

    u->level += adj;

#if MDEBUG
    fprintf(stderr, " user matrix at %p, new level = %d\n", (void *) u,
	    u->level);
#endif

    return 0;
}

int user_matrix_set_name (user_matrix *u, const char *name)
{
    if (u == NULL) {
	return E_UNKVAR;
    }

    *u->name = '\0';
    strncat(u->name, name, VNAMELEN - 1);

#if MDEBUG
    fprintf(stderr, " user matrix at %p, new name = '%s'\n", (void *) u,
	    name);
#endif

    return 0;
}

const char *user_matrix_get_name (user_matrix *u)
{
    return (u == NULL)? NULL : u->name;
}

int user_matrix_replace_matrix_by_name (const char *name, 
					gretl_matrix *M)
{
    user_matrix *u = get_user_matrix_by_name(name);  

    return user_matrix_replace_matrix(u, M);
}

static user_matrix *get_user_matrix_by_data (const gretl_matrix *M)
{
    int level = gretl_function_depth();
    int i;

    for (i=0; i<n_matrices; i++) {
	if (matrices[i]->M == M && matrices[i]->level == level) {
	    return matrices[i];
	}
    }

    return NULL;
}

/**
 * get_matrix_by_name:
 * @name: name of the matrix.
 *
 * Looks up a user-defined matrix by name.
 *
 * Returns: pointer to matrix, or %NULL if not found.
 */

gretl_matrix *get_matrix_by_name (const char *name)
{
    return real_get_matrix_by_name(name, LEVEL_AUTO);
}

/**
 * get_matrix_copy_by_name:
 * @name: name of the matrix.
 * @err: location to receive error code;
 *
 * Looks up a user-defined matrix by name.
 *
 * Returns: a copy of the named matrix, or %NULL if not found.
 */

gretl_matrix *get_matrix_copy_by_name (const char *name, int *err)
{
    gretl_matrix *m;

    m = real_get_matrix_by_name(name, LEVEL_AUTO);

    if (m == NULL) {
	*err = E_UNKVAR;
    } else {
	m = gretl_matrix_copy(m);
	if (m == NULL) {
	    *err = E_ALLOC;
	}
    }

    return m;
}

/**
 * get_matrix_by_name_at_level:
 * @name: name of the matrix.
 * @level: level of function execution at which to search.
 *
 * Looks up a user-defined matrix by @name, at the given
 * @level of function execution.
 *
 * Returns: pointer to matrix, or %NULL if not found.
 */

gretl_matrix *get_matrix_by_name_at_level (const char *name, int level)
{
    return real_get_matrix_by_name(name, level);
}

/**
 * user_matrix_get_matrix:
 * @u: user-matrix pointer.
 *
 * Returns: pointer to matrix, or %NULL if not found.
 */

gretl_matrix *user_matrix_get_matrix (user_matrix *u)
{
    int i;

    for (i=0; i<n_matrices; i++) {
	if (matrices[i] == u) {
	    return matrices[i]->M;
	}
    }

    return NULL;
}

/**
 * copy_named_matrix_as:
 * @orig: the name of the original matrix.
 * @new: the name to be given to the copy.
 *
 * If a saved matrix is found by the name @orig, a copy of
 * this matrix is added to the stack of saved matrices under the
 * name @new.  This is intended for use when a matrix is given
 * as the argument to a user-defined function: it is copied
 * under the name assigned by the function's parameter list.
 *
 * Returns: 0 on success, non-zero on error.
 */

int copy_named_matrix_as (const char *orig, const char *new)
{
    user_matrix *u;
    int err = 0;

    u = get_user_matrix_by_name(orig);
    if (u == NULL) {
	err = 1;
    } else {
	gretl_matrix *M = gretl_matrix_copy(u->M);

	if (M == NULL) {
	    err = E_ALLOC;
	} else {
	    err = user_matrix_add(M, new);
	}
	if (!err) {
	    /* for use in functions: increment level of last-added matrix */
	    u = matrices[n_matrices - 1];
	    u->level += 1;
	}
    }

    return err;
}

/**
 * copy_matrix_as:
 * @m: the original matrix.
 * @new: the name to be given to the copy.
 *
 * A copy of matrix @m is added to the stack of saved matrices
 * under the name @new.  This is intended for use when a matrix is given
 * is given as the argument to a user-defined function: it is copied
 * under the name assigned by the function's parameter list.
 *
 * Returns: 0 on success, non-zero on error.
 */

int copy_matrix_as (const gretl_matrix *m, const char *new)
{
    gretl_matrix *m2 = gretl_matrix_copy(m);
    int err;

    if (m2 == NULL) {
	err = E_ALLOC;
    } else {
	err = matrix_copy_add(m2, new);
    }

    if (!err) {
	/* increment level of last-added matrix */
	user_matrix *u = matrices[n_matrices - 1];

	u->level += 1;
    }

    return err;
}

/**
 * user_matrix_set_name_and_level:
 * @m: matrix to be reconfigured.
 * @name: new name to be given to matrix.
 * @level: function execution level to be assigned to matrix.
 *
 * If matrix @m is found on the stack of saved matrices, change
 * its name to @newname and change its level to @level.
 *
 * Returns: 0 on success, 1 if the matrix is not found.
 */

int user_matrix_set_name_and_level (const gretl_matrix *M, char *name, 
				    int level)
{
    user_matrix *u = get_user_matrix_by_data(M);
    int err = 0;

    if (u == NULL) {
	err = 1;
    } else {
	*u->name = '\0';
	strncat(u->name, name, VNAMELEN - 1);
	u->level = level;
    }

    return err;
}

static int *mspec_to_list (int type, union msel *sel, int n,
			   int *err)
{
    int *slice = NULL;
    int i, ns = 0;

    if (type == SEL_ALL || type == SEL_NULL) {
	return NULL;
    }

    if (type == SEL_RANGE) {
	ns = sel->range[1] - sel->range[0] + 1;
    } else {
	ns = gretl_vector_get_length(sel->m);
    }

    if (ns <= 0) {
	*err = E_DATA;
	return NULL;
    }

    slice = gretl_list_new(ns);
    if (slice == NULL) {
	*err = E_ALLOC;
	return NULL;
    }

    for (i=0; i<slice[0]; i++) {
	if (type == SEL_RANGE) {
	    slice[i+1] = sel->range[0] + i;
	} else {
	    slice[i+1] = sel->m->val[i];
	}
    }

    for (i=1; i<=slice[0] && !*err; i++) {
	if (slice[i] < 1 || slice[i] > n) {
	    fprintf(stderr, "index value %d is out of bounds\n", 
		    slice[i]);
	    *err = 1;
	}
    }

    if (*err) {
	free(slice);
	slice = NULL;
    }

    return slice;
}

static int get_slices (matrix_subspec *spec, 
		       int **rslice, int **cslice,
		       const gretl_matrix *M)
{
    int err = 0;

    if (spec->type[1] == SEL_NULL) {
	/* we were given, e.g., "M[2]" */
	if (M->rows == 1) {
	    /* treat the one selection as column selection */
	    *rslice = NULL;
	    *cslice = mspec_to_list(spec->type[0], &spec->sel[0], 
				    M->cols, &err);
	    return err;
	} 
    }

    *rslice = mspec_to_list(spec->type[0], &spec->sel[0], 
			    M->rows, &err);
    if (err) {
	return err;
    }

    *cslice = mspec_to_list(spec->type[1], &spec->sel[1], 
			    M->cols, &err);
    if (err) {
	free(*rslice);
	*rslice = NULL;
    }

    return err;
}

static int matrix_replace_submatrix (gretl_matrix *M,
				     const gretl_matrix *S,
				     matrix_subspec *spec)
{
    int mr = gretl_matrix_rows(M);
    int mc = gretl_matrix_cols(M);
    int sr = gretl_matrix_rows(S);
    int sc = gretl_matrix_cols(S);
    int *rslice = NULL;
    int *cslice = NULL;
    int sscalar = 0;
    int err = 0;

    if (sr > mr || sc > mc) {
	return E_NONCONF;
    }

    if (spec->type[0] == SEL_DIAG) {
	return matrix_insert_diagonal(M, S, mr, mc);
    }

    err = get_slices(spec, &rslice, &cslice, M);
    if (err) {
	return err;
    }

#if MDEBUG
    printlist(rslice, "rslice");
    printlist(cslice, "cslice");
    fprintf(stderr, "M = %d x %d, S = %d x %d\n", mr, mc, sr, sc);
#endif

    if (sr == 1 && sc == 1) {
	sscalar = 1;
	sr = (rslice == NULL)? mr : rslice[0];
	sc = (cslice == NULL)? mc : cslice[0];
    } else if (rslice != NULL && rslice[0] != sr) {
	err = E_NONCONF;
    } else if (cslice != NULL && cslice[0] != sc) {
	err = E_NONCONF;
    }

    if (!err) {
	int i, j, l, k = 0;
	int mi, mj;
	double x;

	x = (sscalar)? S->val[0] : 0.0;

	for (i=0; i<sr; i++) {
	    mi = (rslice == NULL)? k++ : rslice[i+1] - 1;
	    l = 0;
	    for (j=0; j<sc; j++) {
		mj = (cslice == NULL)? l++ : cslice[j+1] - 1;
		if (!sscalar) {
		    x = gretl_matrix_get(S, i, j);
		}
		gretl_matrix_set(M, mi, mj, x);
	    }
	}
    }

    free(rslice);
    free(cslice);

    return err;
}

gretl_matrix *matrix_get_submatrix (const gretl_matrix *M, 
				    matrix_subspec *spec,
				    int *err)
{
    gretl_matrix *S;
    int *rslice = NULL;
    int *cslice = NULL;
    int r, c;

    if (gretl_is_null_matrix(M)) {
	*err = E_DATA;
	return NULL;
    }

    if (spec->type[0] == SEL_DIAG) {
	return gretl_matrix_get_diagonal(M, err);
    }

    *err = get_slices(spec, &rslice, &cslice, M);
    if (*err) {
	return NULL;
    }

#if MDEBUG
    printlist(rslice, "rslice");
    printlist(cslice, "cslice");
    fprintf(stderr, "M = %d x %d\n", M->rows, M->cols);
#endif

    r = (rslice == NULL)? M->rows : rslice[0];
    c = (cslice == NULL)? M->cols : cslice[0];

    S = gretl_matrix_alloc(r, c);
    if (S == NULL) {
	*err = E_ALLOC;	
    }

    if (S != NULL) {
	int i, j, k, l;
	int mi, mj;
	double x;

	k = 0;
	for (i=0; i<r && !*err; i++) {
	    mi = (rslice == NULL)? k++ : rslice[i+1] - 1;
	    l = 0;
	    for (j=0; j<c && !*err; j++) {
		mj = (cslice == NULL)? l++ : cslice[j+1] - 1;
		x = gretl_matrix_get(M, mi, mj);
		gretl_matrix_set(S, i, j, x);
	    }
	}
    }

    if (S != NULL && S->rows == M->rows) {
	S->t1 = M->t1;
    }

    free(rslice);
    free(cslice);

    return S;
}

gretl_matrix *user_matrix_get_submatrix (const char *name, 
					 matrix_subspec *spec,
					 int *err)
{
    gretl_matrix *S = NULL;
    gretl_matrix *M;

    M = real_get_matrix_by_name(name, LEVEL_AUTO); 
    if (M == NULL) {
	*err = E_UNKVAR;
    } else {
	S = matrix_get_submatrix(M, spec, err);
    }

    return S;
}

int user_matrix_replace_submatrix (const char *name, gretl_matrix *S,
				   matrix_subspec *spec)
{
    gretl_matrix *M;

    M = real_get_matrix_by_name(name, LEVEL_AUTO); 
    if (M == NULL) {
	return E_UNKVAR;
    }

    return matrix_replace_submatrix(M, S, spec);
}

/**
 * add_or_replace_user_matrix:
 * @M: gretl matrix.
 * @name: name for the matrix.
 *
 * Checks whether a matrix of the given @name already exists.
 * If so, the original matrix is replaced by @M; if not, the
 * the matrix @M is added to the stack of user-defined
 * matrices.
 *
 * Returns: 0 on success, %E_ALLOC on failure.
 */

int add_or_replace_user_matrix (gretl_matrix *M, const char *name)
{
    int err = 0;

    if (get_user_matrix_by_name(name) != NULL) {
	err = user_matrix_replace_matrix_by_name(name, M);
    } else {
	err = user_matrix_add(M, name);
    }

    return err;
}

static void destroy_user_matrix (user_matrix *u)
{
    if (u == NULL) {
	return;
    }

#if MDEBUG
    fprintf(stderr, "destroy_user_matrix: freeing matrix at %p...", 
	    (void *) u->M);
#endif
    gretl_matrix_free(u->M);
    free(u);
#if MDEBUG
    fprintf(stderr, " done\n");
#endif
}

#define LEV_PRIVATE -1

static int levels_match (user_matrix *u, int lev)
{
    if (u->level == lev) {
	return 1;
    } else if (lev == LEV_PRIVATE && *u->name == '$') {
	return 1;
    }

    return 0;
}

/**
 * destroy_user_matrices_at_level:
 * @level: stack level of function execution.
 *
 * Destroys and removes from the stack of user matrices all
 * matrices that were created at the given @level.  This is 
 * part of the cleanup that is performed when a user-defined
 * function terminates.
 *
 * Returns: 0 on success, non-zero on error.
 */

int destroy_user_matrices_at_level (int level)
{
    user_matrix **tmp;
    int i, j, nm = 0;
    int err = 0;

#if MDEBUG
    fprintf(stderr, "destroy_user_matrices_at_level: level = %d, "
	    "total n_matrices = %d\n", level, n_matrices);
#endif
#if MDEBUG > 1
    print_matrix_stack("at top of destroy_user_matrices_at_level");
#endif

    for (i=0; i<n_matrices; i++) {
	if (matrices[i] == NULL) {
	    break;
	}
	if (levels_match(matrices[i], level)) {
#if MDEBUG
	    fprintf(stderr, "destroying matrix[%d] ('%s' at %p)\n",
		    i, matrices[i]->name, (void *) matrices[i]->M);
#endif
	    destroy_user_matrix(matrices[i]);
	    for (j=i; j<n_matrices - 1; j++) {
		matrices[j] = matrices[j+1];
	    }
	    matrices[n_matrices - 1] = NULL;
	    i--;
	} else {
	    nm++;
	}
    }

    if (nm < n_matrices) {
	n_matrices = nm;
	if (nm == 0) {
	    free(matrices);
	    matrices = NULL;
	} else {
	    tmp = realloc(matrices, nm * sizeof *tmp);
	    if (tmp == NULL) {
		err = E_ALLOC;
	    } else {
		matrices = tmp;
	    }
	}
    }

#if MDEBUG > 1
    print_matrix_stack("at end of destroy_user_matrices_at_level");
#endif

    return err;
}

int destroy_private_matrices (void)
{
    return destroy_user_matrices_at_level(LEV_PRIVATE);    
}

/**
 * destroy_user_matrices:
 *
 * Frees all resources associated with the stack of user-
 * defined matrices.
 */

void destroy_user_matrices (void)
{
    int i;

#if MDEBUG
    fprintf(stderr, "destroy_user_matrices called, n_matrices = %d\n",
	    n_matrices);
#endif

    if (matrices == NULL) {
	return;
    }

    for (i=0; i<n_matrices; i++) {
#if MDEBUG
	fprintf(stderr, "destroying user_matrix %d (%s) at %p\n", i,
		matrices[i]->name, (void *) matrices[i]);
#endif
	destroy_user_matrix(matrices[i]);
    }

    free(matrices);
    matrices = NULL;
    n_matrices = 0;
}

int user_matrix_destroy (user_matrix *u)
{
    int err = 0;

    if (u == NULL) {
	err = E_UNKVAR;
    } else {
	int i, j, nm = n_matrices - 1;

	for (i=0; i<n_matrices; i++) {
	    if (matrices[i] == u) {
		destroy_user_matrix(matrices[i]);
		for (j=i; j<nm; j++) {
		    matrices[j] = matrices[j+1];
		}
		matrices[nm] = NULL;
		break;
	    }
	} 

	if (nm == 0) {
	    free(matrices);
	    matrices = NULL;
	} else {
	    user_matrix **tmp = realloc(matrices, nm * sizeof *tmp);

	    if (tmp == NULL) {
		err = E_ALLOC;
	    } else {
		matrices = tmp;
	    }
	}

	n_matrices = nm;
    }

    return err;
}

int user_matrix_destroy_by_name (const char *name, PRN *prn)
{
    user_matrix *u = get_user_matrix_by_name(name);
    int err;

    err = user_matrix_destroy(u);

    if (!err && prn != NULL) {
	pprintf(prn, _("Deleted matrix %s"), name);
	pputc(prn, '\n');
    }

    return err;
}

static double 
real_user_matrix_get_determinant (const gretl_matrix *m, int log, int *err)
{
    double d = NADBL;

    if (!gretl_is_null_matrix(m)) {
	gretl_matrix *tmp = gretl_matrix_copy(m);

	if (tmp != NULL) {
	    if (log) {
		d = gretl_matrix_log_determinant(tmp, err);
	    } else {
		d = gretl_matrix_determinant(tmp, err);
	    }
	    gretl_matrix_free(tmp);
	}
    }

    return d;
}

double user_matrix_get_determinant (const gretl_matrix *m, int *err)
{
    return real_user_matrix_get_determinant(m, 0, err);
}

double user_matrix_get_log_determinant (const gretl_matrix *m, int *err)
{
    return real_user_matrix_get_determinant(m, 1, err);
}

static gretl_matrix *
real_user_matrix_get_inverse (const gretl_matrix *m, int moore, int *err)
{
    gretl_matrix *R = NULL;

    if (gretl_is_null_matrix(m)) {
	*err = E_DATA;
    } else {
	R = gretl_matrix_copy(m);
	if (R == NULL) {
	    *err = E_ALLOC;
	} else {
	    if (moore) {
		*err = gretl_matrix_moore_penrose(R);
	    } else {
		*err = gretl_invert_matrix(R);
	    } 
	    if (*err) {
		gretl_matrix_free(R);
		R = NULL;
	    }
	} 
    } 
   
    return R;
}

gretl_matrix *user_matrix_get_inverse (const gretl_matrix *m, int *err)
{
    return real_user_matrix_get_inverse(m, 0, err);
}

gretl_matrix *user_matrix_moore_penrose (const gretl_matrix *m, int *err)
{
    return real_user_matrix_get_inverse(m, 1, err);
}

static void matrix_cannibalize (gretl_matrix *targ, gretl_matrix *src)
{
    targ->rows = src->rows;
    targ->cols = src->cols;

    free(targ->val);
    targ->val = src->val;
    src->val = NULL;

#if USE_COLS
    free(targ->col);
    targ->col = src->col;
    src->col = NULL;
#endif
}

int matrix_invert_in_place (gretl_matrix *m)
{
    gretl_matrix *R = gretl_matrix_copy(m);
    int err = 0;

    if (R == NULL) {
	err = E_ALLOC;
    } else {
	err = gretl_invert_matrix(R);
	if (!err) {
	    matrix_cannibalize(m, R);
	}
	gretl_matrix_free(R);
    } 

    return err;
}

int matrix_cholesky_in_place (gretl_matrix *m)
{
    gretl_matrix *R = gretl_matrix_copy(m);
    int err = 0;

    if (R == NULL) {
	err = E_ALLOC;
    } else {
	err = gretl_matrix_cholesky_decomp(R);
	if (!err) {
	    matrix_cannibalize(m, R);
	}
	gretl_matrix_free(R);
    } 

    return err;
}

int matrix_transpose_in_place (gretl_matrix *m)
{
    gretl_matrix *R = gretl_matrix_copy_transpose(m);
    int err = 0;

    if (R == NULL) {
	err = E_ALLOC;
    } else {
	matrix_cannibalize(m, R);
	gretl_matrix_free(R);
    }

    return err;
}

int matrix_XTX_in_place (gretl_matrix *m)
{
    gretl_matrix *R = gretl_matrix_alloc(m->cols, m->cols);
    int err;

    if (R == NULL) {
	err = E_ALLOC;
    } else {
	err = gretl_matrix_multiply_mod(m, GRETL_MOD_TRANSPOSE,
					m, GRETL_MOD_NONE,
					R, GRETL_MOD_NONE);
    }

    if (!err) {
	matrix_cannibalize(m, R);
    }

    gretl_matrix_free(R);

    return err;
}

gretl_matrix *user_matrix_cholesky_decomp (const gretl_matrix *m,
					   int *err)
{
    gretl_matrix *R = NULL;

    if (gretl_is_null_matrix(m)) {
	*err = E_DATA;
    } else {
	R = gretl_matrix_copy(m);
	if (R == NULL) {
	    *err = E_ALLOC;
	} else {
	    *err = gretl_matrix_cholesky_decomp(R);
	    if (*err) {
		gretl_matrix_free(R);
		R = NULL;
	    }
	} 
    }

    return R;
}

gretl_matrix *user_matrix_column_demean (const gretl_matrix *m, int *err)
{
    gretl_matrix *R = NULL;

    if (gretl_is_null_matrix(m)) {
	*err = E_DATA;
    } else {
	R = gretl_matrix_copy(m);
	if (R == NULL) {
	    *err = E_ALLOC;
	} else {
	    gretl_matrix_demean_by_column(R);
	} 
    }

    return R;
}

gretl_matrix *user_matrix_vec (const gretl_matrix *m, int *err)
{
    gretl_matrix *R = NULL;

    if (gretl_is_null_matrix(m)) {
	R = gretl_null_matrix_new();
    } else {
	R = gretl_matrix_alloc(m->rows * m->cols, 1);
	if (R != NULL) {
	    gretl_matrix_vectorize(R, m);
	} 
    }

    if (R == NULL) {
	*err = E_ALLOC;
    }

    return R;
}

gretl_matrix *user_matrix_vech (const gretl_matrix *m, int *err)
{
    gretl_matrix *R = NULL;

    if (gretl_is_null_matrix(m)) {
	R = gretl_null_matrix_new();
    } else if (m->rows != m->cols) {
	*err = E_NONCONF;
    } else {
	int n = m->rows;
	int k = n * (n + 1) / 2;

	R = gretl_matrix_alloc(k, 1);
	if (R != NULL) {
	    *err = gretl_matrix_vectorize_h(R, m);
	}
    } 

    if (R == NULL && !*err) {
	*err = E_ALLOC;
    }

    return R;
}

gretl_matrix *user_matrix_unvech (const gretl_matrix *m, int *err)
{
    gretl_matrix *R = NULL;

    if (gretl_is_null_matrix(m)) {
	R = gretl_null_matrix_new();
    } else if (m->cols != 1) {
	*err = E_NONCONF;
    } else {
	int n = (int) ((sqrt(1.0 + 8.0 * m->rows) - 1.0) / 2.0);

	R = gretl_matrix_alloc(n, n);
	if (R != NULL) {
	    *err = gretl_matrix_unvectorize_h(R, m);
	} 
    }

    if (R == NULL && !*err) {
	*err = E_ALLOC;
    }

    return R;
}

gretl_matrix *user_matrix_upper (const gretl_matrix *m, int *err)
{
    gretl_matrix *R = gretl_matrix_copy(m);

    *err = gretl_matrix_zero_lower(R);
    return R;
}

gretl_matrix *user_matrix_lower (const gretl_matrix *m, int *err)
{
    gretl_matrix *R = gretl_matrix_copy(m);

    *err = gretl_matrix_zero_upper(R);
    return R;
}

static int 
real_user_matrix_QR_decomp (const gretl_matrix *m, gretl_matrix **Q, 
			    gretl_matrix **R)
{
    int mc = gretl_matrix_cols(m);
    int err = 0;

    *Q = gretl_matrix_copy(m);

    if (*Q == NULL) {
	err = E_ALLOC;
    } else if (R != NULL) {
	*R = gretl_matrix_alloc(mc, mc);
	if (*R == NULL) {
	    err = E_ALLOC;
	}
    }

    if (!err) {
	err = gretl_matrix_QR_decomp(*Q, (R == NULL)? NULL : *R);
    }

    if (err) {
	strcpy(gretl_errmsg, _("Matrix decomposition failed"));
	gretl_matrix_free(*Q);
	*Q = NULL;
	if (R != NULL) {
	    gretl_matrix_free(*R);
	    *R = NULL;
	}
    }

    return err;
}

gretl_matrix *
user_matrix_QR_decomp (const gretl_matrix *m, const char *rname, int *err)
{
    gretl_matrix *Q = NULL;
    gretl_matrix *R = NULL;
    int wantR = 0;

    if (gretl_is_null_matrix(m)) {
	*err = E_DATA;
	return NULL;
    }

    if (rname != NULL && strcmp(rname, "null")) {
	wantR = 1;
	if (get_matrix_by_name(rname) == NULL) {
	    *err = E_UNKVAR;
	}
    }

    if (!*err) {
	*err = real_user_matrix_QR_decomp(m, &Q, (wantR)? &R : NULL);
    }

    if (!*err && wantR) {
	user_matrix_replace_matrix_by_name(rname, R);
    }

    return Q;
}

static int revise_SVD_V (gretl_matrix **pV, int r, int c)
{
    gretl_matrix *V;
    double x;
    int i, j;

    V = gretl_matrix_alloc(r, c);
    if (V == NULL) {
	return E_ALLOC;
    }

    for (i=0; i<r; i++) {
	for (j=0; j<c; j++) {
	    x = gretl_matrix_get((*pV), i, j);
	    gretl_matrix_set(V, i, j, x);
	}
    }

    gretl_matrix_free(*pV);
    *pV = V;

    return 0;
}

gretl_matrix *user_matrix_SVD (const gretl_matrix *m, 
			       const char *uname, 
			       const char *vname, 
			       int *err)
{
    gretl_matrix *U = NULL;
    gretl_matrix *S = NULL;
    gretl_matrix *V = NULL;
    gretl_matrix **pU = NULL;
    gretl_matrix **pV = NULL;
    int tall, minrc;
    int wantU = 0;
    int wantV = 0;

    if (gretl_is_null_matrix(m)) {
	*err = E_DATA;
	return NULL;
    }

    tall = m->rows - m->cols;
    minrc = (m->rows > m->cols)? m->cols : m->rows;

    if (uname != NULL && strcmp(uname, "null")) {
	if (get_matrix_by_name(uname) == NULL) {
	    *err = E_UNKVAR;
	} else {
	    wantU = 1;
	    pU = &U;
	}
    }

    if (vname != NULL && strcmp(vname, "null")) {
	if (get_matrix_by_name(vname) == NULL) {
	    *err = E_UNKVAR;
	} else {
	    wantV = 1;
	    pV = &V;
	}
    }

    if (!*err) {
	*err = gretl_matrix_SVD(m, pU, &S, pV);
    }

    if (!*err) {
	if (wantU) {
	    if (tall > 0) {
		*err = gretl_matrix_realloc(U, m->rows, minrc);
	    }
	    if (!*err) {
		user_matrix_replace_matrix_by_name(uname, U);
	    }
	}
	if (wantV) {
	    if (tall < 0) {
		*err = revise_SVD_V(&V, minrc, m->cols);
	    } 
	    if (!*err) {
		user_matrix_replace_matrix_by_name(vname, V);
	    }
	}
    }

    return S;
}

gretl_matrix *user_matrix_ols (const gretl_matrix *Y, 
			       const gretl_matrix *X, 
			       const char *Uname, 
			       int *err)
{
    gretl_matrix *B = NULL;
    gretl_matrix *U = NULL;
    int newU = 0;

    if (gretl_is_null_matrix(X) || gretl_is_null_matrix(X)) {
	*err = E_DATA;
	return NULL;
    }

    if (X->rows != Y->rows) {
	*err = E_NONCONF;
	return NULL;
    }

    if (Uname != NULL && strcmp(Uname, "null")) {
	U = get_matrix_by_name(Uname);
	if (U == NULL) {
	    *err = E_UNKVAR;
	    return NULL;
	} 
	if (U->rows != Y->rows || U->cols != Y->cols) {
	    newU = 1;
	}
    }

    if (newU) {
	U = gretl_matrix_alloc(Y->rows, Y->cols);
	if (U == NULL) {
	    *err = E_ALLOC;
	    return NULL;
	}
    }

    B = gretl_matrix_alloc(X->cols, Y->cols);
    if (B == NULL) {
	*err = E_ALLOC;
    }

    if (!*err) {
	*err = gretl_matrix_multi_ols(Y, X, B, U, NULL);
    }

    if (*err) {
	gretl_matrix_free(B);
	if (newU) {
	    gretl_matrix_free(U);
	}
    } else if (newU) {
	user_matrix_replace_matrix_by_name(Uname, U);
    }

    return B;
}

gretl_matrix *
user_matrix_eigen_analysis (const gretl_matrix *m, const char *rname, int symm,
			    int *err)
{
    gretl_matrix *C = NULL;
    gretl_matrix *E = NULL;
    int vecs = 0;

    if (gretl_is_null_matrix(m)) {
	*err = E_DATA;
	return NULL;
    }

    if (rname != NULL && strcmp(rname, "null")) {
	vecs = 1;
	if (get_matrix_by_name(rname) == NULL) {
	    *err = E_UNKVAR;
	    return NULL;
	}
    }

    C = gretl_matrix_copy(m);
    if (C == NULL) {
	*err = E_ALLOC;
    }

    if (!*err) {
	if (symm) {
	    E = gretl_symmetric_matrix_eigenvals(C, vecs, err);
	} else {
	    E = gretl_general_matrix_eigenvals(C, vecs, err);
	}
    }

    if (!*err && vecs) {
	user_matrix_replace_matrix_by_name(rname, C);
    }

    if (!vecs) {
	gretl_matrix_free(C);
    }

    return E;
}

void write_matrices_to_file (FILE *fp)
{
    int i;

    gretl_xml_header(fp);
    fprintf(fp, "<gretl-matrices count=\"%d\">\n", n_matrices);

    gretl_push_c_numeric_locale();

    for (i=0; i<n_matrices; i++) {
	if (matrices[i]->M != NULL) {
	    gretl_xml_put_matrix(matrices[i]->M, matrices[i]->name, fp);
	}
    }

    gretl_pop_c_numeric_locale();

    fputs("</gretl-matrices>\n", fp);
}
