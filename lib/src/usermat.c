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

#include "libgretl.h"
#include "gretl_matrix.h"
#include "gretl_func.h"
#include "usermat.h"

#include <errno.h>

#define MDEBUG 0

#define MNAMELEN 32

typedef struct user_matrix_ user_matrix;

struct user_matrix_ {
    gretl_matrix *M;
    int level;
    char name[MNAMELEN];
};

static user_matrix **matrices;
static int n_matrices;

static user_matrix *user_matrix_new (gretl_matrix *M, const char *name)
{
    user_matrix *u;

    u = malloc(sizeof *u);
    if (u == NULL) {
	return NULL;
    }

    u->M = M;

    u->level = gretl_function_stack_depth();

    *u->name = '\0';
    strncat(u->name, name, MNAMELEN - 1);

    return u;
}

static int add_user_matrix (gretl_matrix *M, const char *name)
{
    user_matrix **tmp;

    tmp = realloc(matrices, (n_matrices + 1) * sizeof *tmp);
    if (tmp == NULL) {
	return E_ALLOC;
    }

    matrices = tmp;
    matrices[n_matrices] = user_matrix_new(M, name);

    if (matrices[n_matrices] == NULL) {
	return E_ALLOC;
    }

    n_matrices++;

#if MDEBUG
    fprintf(stderr, "add_user_matrix\n");
    debug_print_matrix(M, name);
#endif

    return 0;
}

static int replace_user_matrix (user_matrix *u, gretl_matrix *M)
{
    gretl_matrix_free(u->M);
    u->M = M;

    return 0;
}

int is_user_matrix (gretl_matrix *m)
{
    int i;

    for (i=0; i<n_matrices; i++) {
	if (m == matrices[i]->M) {
	    return 1;
	}
    }

    return 0;
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
    char test[MNAMELEN];
    int level = gretl_function_stack_depth();
    int i;

    *test = '\0';
    strncat(test, name, MNAMELEN - 1);
    if (test[strlen(test) - 1] == '\'') {
	test[strlen(test) - 1] = '\0';
    }

    for (i=0; i<n_matrices; i++) {
	if (!strcmp(test, matrices[i]->name) &&
	    matrices[i]->level == level) {
	    return matrices[i]->M;
	}
    }

    return NULL;
}

static user_matrix *get_user_matrix_by_name (const char *name)
{
    int level = gretl_function_stack_depth();
    int i;

    for (i=0; i<n_matrices; i++) {
	if (!strcmp(name, matrices[i]->name) &&
	    matrices[i]->level == level) {
	    return matrices[i];
	}
    }

    return NULL;
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
    user_matrix *u;
    int err = 0;

    u = get_user_matrix_by_name(name);
    if (u != NULL) {
	err = replace_user_matrix(u, M);
    } else {
	err = add_user_matrix(M, name);
    }

    return err;
}

static void destroy_user_matrix (user_matrix *u)
{
    gretl_matrix_free(u->M);
    free(u);
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

    for (i=0; i<n_matrices; i++) {
	if (matrices[i]->level == level) {
	    destroy_user_matrix(matrices[i]);
	    for (j=i; j<n_matrices - 1; j++) {
		matrices[j] = matrices[j+1];
	    }
	    matrices[n_matrices - 1] = NULL;
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

    return err;
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

    for (i=0; i<n_matrices; i++) {
	destroy_user_matrix(matrices[i]);
    }

    free(matrices);
    matrices = NULL;
    n_matrices = 0;
}

#define numeric_start(s) (strcspn(s, "+-0123456789") == 0)

static int 
get_rows_cols (const char *str, int *r, int *c, int *numeric)
{
    const char *p = str;
    char *s;
    int nf0 = 0, nf1;
    int i, len;
    int err = 0;

    *r = 1;
    while (*p) {
	if (*p == ';') {
	    *r += 1;
	}
	p++;
    }

    for (i=0; i<*r && !err; i++) {
	len = strcspn(str, ";");

	s = gretl_strndup(str, len);
	if (s == NULL) {
	    err = E_ALLOC;
	    break;
	}

	/* clean up */
	charsub(s, ',', ' ');
	charsub(s, '}', ' ');
	charsub(s, '\'', ' ');

	nf1 = count_fields(s);
	if (i > 0 && nf1 != nf0) {
	    strcpy(gretl_errmsg, "Inconsistent matrix specification");
	    err = 1;
	    break;
	}

	nf0 = nf1;

	/* peek at first field: is it numeric? */
	if (i == 0) {
	    p = s;
	    while (isspace(*p)) p++;
	    *numeric = numeric_start(p);
	}

	str += len + 1;

	free(s);
    }

    if (!err) {
	*c = nf0;
    }

    return err;
}

static int 
get_varnum (const char **s, const DATAINFO *pdinfo, int *err)
{
    char vname[12];
    int v = 0;

    if (sscanf(*s, "%11s", vname)) {
	int len = strlen(vname);
	char *p = strrchr(vname, '}');
	
	if (p != NULL) {
	    *p = '\0';
	}

	v = varindex(pdinfo, vname);
	if (v >= pdinfo->v) {
	    sprintf(gretl_errmsg, _("Unknown variable '%s'"), vname);
	    *err = 1;
	} else {
	    *s += len;
	}
    } else {
	*err = 1;
    }

    return v;
}

static double get_double (const char **s, int *err)
{
    double x = NADBL;
    char *p;

    x = strtod(*s, &p);

    if (!strcmp(*s, p)) {
	sprintf(gretl_errmsg, _("'%s' -- no numeric conversion performed!"), *s);
	*err = 1;
    } else if (*p != '\0' && *p != ',' && *p != ';' && *p != ' ' && *p != '}') {
	if (isprint(*p)) {
	    sprintf(gretl_errmsg, _("Extraneous character '%c' in data"), *p);
	} else {
	    sprintf(gretl_errmsg, _("Extraneous character (0x%x) in data"), *p);
	}
	*err = 1;
    } else if (errno == ERANGE) {
	sprintf(gretl_errmsg, _("'%s' -- number out of range!"), *s);
	*err = 1;
    }

    *s = p;

    return x;
}

static int 
fill_matrix_from_numbers (gretl_matrix *M, const char *s, 
			  int r, int c, int transp)
{
    double x;
    int i, j;
    int err = 0;

    if (transp) {
	for (j=0; j<c && !err; j++) {
	    for (i=0; i<r && !err; i++) {
		x = get_double(&s, &err);
		if (!err) {
		    gretl_matrix_set(M, i, j, x);
		    s += strspn(s, " ,;");
		}
	    }
	}
    } else {
	for (i=0; i<r && !err; i++) {
	    for (j=0; j<c && !err; j++) {
		x = get_double(&s, &err);
		if (!err) {
		    gretl_matrix_set(M, i, j, x);	
		    s += strspn(s, " ,;");
		}
	    }
	}
    }

    return err;
}

static int fill_matrix_from_series (gretl_matrix *M, const char *s,
				    int r, int c, int transp,
				    const double **Z, const DATAINFO *pdinfo)
{
    int T = pdinfo->t2 - pdinfo->t1 + 1;
    int nvr = r, nvc = c;
    int i, j, k, t, v;
    double x;
    int err = 0;

    s += strspn(s, " ");

    if (transp) {
	nvr /= T;
	for (j=0; j<c && !err; j++) {
	    for (i=0; i<nvr && !err; i++) {
		v = get_varnum(&s, pdinfo, &err);
		if (!err) {
		    for (t=0; t<T; t++) {
			if (pdinfo->vector[v]) {
			    x = Z[v][t + pdinfo->t1];
			} else {
			    x = Z[v][0];
			}
			k = i * T + t;
			gretl_matrix_set(M, k, j, x);
		    }
		    s += strspn(s, " ,;");
		}
	    }
	}
    } else {
	nvc /= T;
	for (i=0; i<r && !err; i++) {
	    for (j=0; j<nvc && !err; j++) {
		v = get_varnum(&s, pdinfo, &err);
		if (!err) {
		    for (t=0; t<T; t++) {
			if (pdinfo->vector[v]) {
			    x = Z[v][t + pdinfo->t1]; 
			} else {
			    x = Z[v][0];
			}
			k = j * T + t;
			gretl_matrix_set(M, i, k, x);
		    }
		    s += strspn(s, " ,;");
		}
	    }
	}
    }

    return err;
}

static int matrix_genr (const char *name, const char *s, 
			double ***pZ, DATAINFO *pdinfo)
{
    char genline[MAXLINE];
    int err;

    if (strlen(name) + strlen(s) + 7 > MAXLINE - 1) {
	err = 1;
    } else {
	sprintf(genline, "genr %s %s", name, s);
	err = generate(genline, pZ, pdinfo, OPT_M);
    }

    return err;
}

static int create_matrix (const char *name, const char *s, 
			  double ***pZ, DATAINFO *pdinfo,
			  PRN *prn)
{
    gretl_matrix *M = NULL;
    char *p;
    int nm = n_matrices;
    int numeric = 0;
    int transp = 0;
    int r = 0, c = 0;
    int err = 0;

    p = strchr(s, '{');
    if (p == NULL) {
	err = matrix_genr(name, s, pZ, pdinfo);
	goto message;
    }
    s = p + 1;

    if (!err) {
	p = strchr(s, '}');
	if (p == NULL) {
	    err = 1;
	}
	if (*(p+1) == '\'') {
	    transp = 1;
	}
    }

    if (!err) {
	err = get_rows_cols(s, &r, &c, &numeric);
	if (!err && c == 0) {
	    err = 1;
	}
    }

    if (!err && !numeric) {
	c *= pdinfo->t2 - pdinfo->t1 + 1;
    }

    if (!err && transp) {
	int tmp = r;

	r = c;
	c = tmp;
    }

#if MDEBUG
    fprintf(stderr, "r=%d, c=%d, transp=%d, numeric=%d\n",
	    r, c, transp, numeric);
#endif

    if (!err) {
	M = gretl_matrix_alloc(r, c);
	if (M == NULL) {
	   err = E_ALLOC;
	} 
    }

    if (!err) {
	if (numeric) {
	    err = fill_matrix_from_numbers(M, s, r, c, transp);
	} else {
	    err = fill_matrix_from_series(M, s, r, c, transp, 
					  (const double **) *pZ, 
					  pdinfo);
	}
    }
    
    if (!err) {
	err = add_or_replace_user_matrix(M, name);
    }

 message:

    if (!err) {
	if (n_matrices > nm) {
	    pprintf(prn, "Added matrix '%s'\n", name);
	} else {
	    pprintf(prn, "Replaced matrix '%s'\n", name);
	}
    } else {
	pprintf(prn, "Error adding matrix '%s'\n", name);
    }

    return err;
}

static int print_matrix_by_name (const char *name, PRN *prn)
{
    gretl_matrix *M;
    int err = 0;

    M = get_matrix_by_name(name);
    if (M == NULL) {
	pprintf(prn, _("'%s': no such matrix\n"), name);
	err = 1;
    } else {
	gretl_matrix_print_to_prn(M, name, prn);
    }

    return err;
}

/**
 * matrix_command:
 * @s: string that specifies matrix command.
 * @prn: pointer to printing struct.
 *
 * To be written.
 * 
 * Returns: 0 on success, non-zero code on failure.
 */

int matrix_command (const char *line, double ***pZ, DATAINFO *pdinfo, PRN *prn)
{
    char name[MNAMELEN];
    char word[9];
    int err = 0;

    if (!strncmp(line, "matrix ", 7)) line += 7;
    while (isspace(*line)) line++;

    if (!sscanf(line, "%31s", name)) {
	return 1;
    } 

    line += strlen(name);
    while (isspace(*line)) line++;

    if (*line == '=') {
	/* defining a matrix */
	err = create_matrix(name, line, pZ, pdinfo, prn);
    } else {
	*word = '\0';
	sscanf(line, "%8s", word);
	if (*word == '\0' || !strcmp(word, "print")) {
	    err = print_matrix_by_name(name, prn);
	} else {
	    /* no other commands available yet */
	    err = 1;
	}
    } 

    return err;
}
