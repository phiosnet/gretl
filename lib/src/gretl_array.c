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
#include "uservar.h"
#include "gretl_func.h"
#include "gretl_array.h"

/**
 * gretl_array:
 *
 * An opaque type; use the relevant accessor functions.
 */

struct gretl_array_ {
    GretlType type;  /* type of data */
    int n;           /* number of elements */
    void **data;     /* actual data array */
};

static void gretl_array_destroy_data (gretl_array *A)
{
    int i;

    if (A->data != NULL) {
	if (A->type == GRETL_TYPE_STRINGS) {
	    for (i=0; i<A->n; i++) {
		free(A->data[i]);
	    }
	} else if (A->type == GRETL_TYPE_MATRICES) {
	    for (i=0; i<A->n; i++) {
		gretl_matrix_free(A->data[i]);
	    }
	} else {
	    for (i=0; i<A->n; i++) {
		gretl_bundle_destroy(A->data[i]);
	    }
	}   
	free(A->data);
	A->data = NULL;
    }
}

void gretl_array_destroy (gretl_array *A)
{
    if (A != NULL) {
	gretl_array_destroy_data(A);
	free(A);
    }
}

void gretl_array_void_content (gretl_array *A)
{
    if (A != NULL) {
	gretl_array_destroy_data(A);
	A->n = 0;
    }
}

static int array_allocate_content (gretl_array *A)
{
    A->data = malloc(A->n * sizeof *A->data);
    
    if (A->data == NULL) {
	return E_ALLOC;
    } else {
	int i;
    
	for (i=0; i<A->n; i++) {
	    A->data[i] = NULL;
	}   

	return 0;
    }
}

static int array_extend_content (gretl_array *A)
{
    void **data;
    int n = A->n + 1;
    int err = 0;

    data = realloc(A->data, n * sizeof *A->data);
    
    if (data == NULL) {
	err = E_ALLOC;
    } else {
	data[n-1] = NULL;
	A->data = data;
	A->n = n;
    }

    return err;
}

/* Create a new array of type @type. The idea is that it's OK
   to have n = 0: in that case the array starts empty but can
   be extended by the += operator. Or it can be sized in
   advance.
*/

gretl_array *gretl_array_new (GretlType type, int n, int *err)
{
    gretl_array *A;

    if (type != GRETL_TYPE_STRINGS &&
	type != GRETL_TYPE_MATRICES &&
	type != GRETL_TYPE_BUNDLES) {
	*err = E_TYPES;
	return NULL;
    } else if (n < 0) {
	*err = E_DATA;
	return NULL;
    }

    A = malloc(sizeof *A);
    
    if (A == NULL) {
	*err = E_ALLOC;
    } else {
	A->type = type;
	A->n = n;
	A->data = NULL;
	if (n > 0) {
	    *err = array_allocate_content(A);
	    if (*err) {
		gretl_array_destroy(A);
		A = NULL;
	    }
	}
    }

    return A;
}

void *gretl_array_get_element (gretl_array *A, int i, 
			       GretlType *type,
			       int *err)
{
    void *ret = NULL;

    /* Note that the data returned here are not "deep copied",
       we just pass the pointer. It's up to geneval.c to
       decide if a copy has to be made, given that the
       pointer from here should not be modified.
    */

    if (A == NULL || i < 0 || i >= A->n) {
	*err = E_DATA;
    } else {
	if (A->type == GRETL_TYPE_STRINGS) {
	    *type = GRETL_TYPE_STRING;
	    if (A->data[i] == NULL) {
		A->data[i] = gretl_strdup("");
	    }
	} else if (A->type == GRETL_TYPE_MATRICES) {
	    *type = GRETL_TYPE_MATRIX;
	    if (A->data[i] == NULL) {
		A->data[i] = gretl_null_matrix_new();
	    }	    
	} else {
	    *type = GRETL_TYPE_BUNDLE;
	    if (A->data[i] == NULL) {
		A->data[i] = gretl_bundle_new();
	    }	    
	}
	ret = A->data[i];
	if (ret == NULL) {
	    *err = E_ALLOC;
	}
    }

    return ret;
}

GretlType gretl_array_get_type (gretl_array *A)
{
    return (A != NULL)? A->type : GRETL_TYPE_NONE;
}

int gretl_array_get_length (gretl_array *A)
{
    return (A != NULL)? A->n : 0;
}

static int set_string (gretl_array *A, int i,
		       char *s, int copy)
{
    int err = 0;

    if (copy) {
	A->data[i] = gretl_strdup(s);
	if (A->data[i] == NULL) {
	    err = E_ALLOC;
	}
    } else {
	A->data[i] = s;
    }

    return err;
}

static int set_matrix (gretl_array *A, int i,
		       gretl_matrix *m, int copy)
{
    int err = 0;

    if (copy) {
	A->data[i] = gretl_matrix_copy(m);
	if (A->data[i] == NULL) {
	    err = E_ALLOC;
	}
    } else {
	A->data[i] = m;
    }

    return err;
}

static int set_bundle (gretl_array *A, int i,
		       gretl_bundle *b, int copy)
{
    int err = 0;

    if (copy) {
	A->data[i] = gretl_bundle_copy(b, &err);
    } else {
	A->data[i] = b;
    }

    return err;
}

/* In the functions below I assume the @copy parameter
   will be set appropriately by "genr": if the incoming
   object is a named variable in its own right it should
   be copied, but if it's on on-the-fly thing there's 
   no need to copy it, just "donate" it to the array.
*/

/* respond to A[i] = s */

int gretl_array_set_string (gretl_array *A, int i, 
			    char *s, int copy)
{
    int err = 0;

    if (A == NULL || i < 0 || i >= A->n) {
	err = E_DATA;
    } else if (A->type != GRETL_TYPE_STRINGS) {
	err = E_TYPES;
    } else {
	free(A->data[i]);
	err = set_string(A, i, s, copy);
    }

    return err;
}

/* respond to A += s */

int gretl_array_append_string (gretl_array *A,
			       char *s,
			       int copy)
{
    int err = 0;

    if (A == NULL) {
	err = E_DATA;
    } else if (A->type != GRETL_TYPE_STRINGS) {
	err = E_TYPES;
    } else {
	err = array_extend_content(A);
	if (!err) {
	    err = set_string(A, A->n - 1, s, copy);
	}
    }

    return err;
}

/* respond to A[i] = m */

int gretl_array_set_matrix (gretl_array *A, int i, 
			    gretl_matrix *m,
			    int copy)
{
    int err = 0;

    if (A == NULL || i < 0 || i >= A->n) {
	err = E_DATA;
    } else if (A->type != GRETL_TYPE_MATRICES) {
	err = E_TYPES;
    } else {
	gretl_matrix_free(A->data[i]);
	err = set_matrix(A, i, m, copy);
    }

    return err;
}

/* respond to A += m */

int gretl_array_append_matrix (gretl_array *A,
			       gretl_matrix *m,
			       int copy)
{
    int err = 0;

    if (A == NULL) {
	err = E_DATA;
    } else if (A->type != GRETL_TYPE_MATRICES) {
	err = E_TYPES;
    } else {
	err = array_extend_content(A);
	if (!err) {
	    err = set_matrix(A, A->n - 1, m, copy);
	}
    }

    return err;
}

/* respond to A[i] = b */

int gretl_array_set_bundle (gretl_array *A, int i, 
			    gretl_bundle *b,
			    int copy)
{
    int err = 0;

    if (A == NULL || i < 0 || i >= A->n) {
	err = E_DATA;
    } else if (A->type != GRETL_TYPE_BUNDLES) {
	err = E_TYPES;
    } else {
	gretl_bundle_destroy(A->data[i]);
	err = set_bundle(A, i, b, copy);
    }

    return err;
}

/* respond to A += b */

int gretl_array_append_bundle (gretl_array *A,
			       gretl_bundle *b,
			       int copy)
{
    int err = 0;

    if (A == NULL) {
	err = E_DATA;
    } else if (A->type != GRETL_TYPE_BUNDLES) {
	err = E_TYPES;
    } else {
	err = array_extend_content(A);
	if (!err) {
	    err = set_bundle(A, A->n - 1, b, copy);
	}
    }

    return err;
}

static int 
gretl_array_copy_content (gretl_array *Acpy, const gretl_array *A)
{
    int i, err = 0;

    for (i=0; i<A->n && !err; i++) {
	if (A->data[i] != NULL) {
	    if (A->type == GRETL_TYPE_STRINGS) {
		Acpy->data[i] = gretl_strdup(A->data[i]);
	    } else if (A->type == GRETL_TYPE_MATRICES) {
		Acpy->data[i] = gretl_matrix_copy(A->data[i]);
	    } else {
		Acpy->data[i] = gretl_bundle_copy(A->data[i], &err);
	    }
	    if (Acpy->data[i] == NULL) {
		err = E_ALLOC;
	    }
	}
    }

    return err;
}

gretl_array *gretl_array_copy (const gretl_array *A,
			       int *err)
{
    gretl_array *Acpy = NULL;

    if (A != NULL) {
	Acpy = gretl_array_new(A->type, A->n, err);
	if (!*err) {
	    *err = gretl_array_copy_content(Acpy, A);
	}
    }

    return Acpy;
}

/**
 * gretl_array_copy_as:
 * @name: name of source array.
 * @copyname: name for copy.
 * @copytpe: the type specified for the copied array, or 0.
 *
 * Look for a saved array specified by @name, and if found,
 * make a full copy and save it under @copyname. This is
 * called from geneval.c on completion of assignment to a
 * array named @copyname, where the returned value on the
 * right-hand side is a pre-existing array.
 *
 * Returns: 0 on success, non-zero code on error.
 */

int gretl_array_copy_as (const char *name, const char *copyname,
			 GretlType copytype)
{
    gretl_array *A0, *A1 = NULL;
    user_var *u;
    int err = 0;

    u = get_user_var_of_type_by_name(name, GRETL_TYPE_ARRAY);
    if (u == NULL) {
	return E_DATA;
    } else {
	A0 = user_var_get_value(u);
    }

    if (copytype > 0 && A0->type != copytype) {
	return E_TYPES;
    }

    /* is there a pre-existing array named @copyname? */
    u = get_user_var_of_type_by_name(copyname, GRETL_TYPE_ARRAY);
    if (u != NULL) {
	A1 = user_var_get_value(u);
    }

    if (A1 != NULL) {
	if (A1->type != A0->type) {
	    err = E_TYPES;
	} else {
	    gretl_array_void_content(A1);
	    A1->n = A0->n;
	    err = array_allocate_content(A1);
	    if (!err) {
		err = gretl_array_copy_content(A1, A0);
	    }
	}
    } else {
	A1 = gretl_array_copy(A0, &err);
	if (!err) {
	    err = user_var_add(copyname, A1->type, A1);
	}
    }

    return err;
}

/**
 * get_array_by_name:
 * @name: the name to look up.
 *
 * Returns: pointer to a saved array, if found, else NULL.
 */

gretl_array *get_array_by_name (const char *name)
{
    gretl_array *a = NULL;

    if (name != NULL && *name != '\0') {
	user_var *u = 
	    get_user_var_of_type_by_name(name, GRETL_TYPE_ARRAY);

	if (u != NULL) {
	    a = user_var_get_value(u);
	}
    }

    return a;
}

gretl_array *gretl_array_pull_from_stack (const char *name,
					  int *err)
{
    gretl_array *a = NULL;
    user_var *u;

    u = get_user_var_of_type_by_name(name, GRETL_TYPE_ARRAY);

    if (u != NULL) {
	a = user_var_unstack_value(u);
    }

    if (a == NULL) {
	*err = E_DATA;
    } 

    return a;
}

int gretl_array_print (gretl_array *A, PRN *prn)
{
    if (A != NULL) {
	const char *s = gretl_arg_type_name(A->type);

	pprintf(prn, _("Array of %s, length %d\n"), s, A->n);
    }

    return 0;
}
