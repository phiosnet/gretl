/*
 *  Copyright (c) 2002-2004 by Allin Cottrell
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
#include "gretl_matrix_private.h"

typedef struct fiml_system_ fiml_system;

struct fiml_system_ {
    int n;
    int g;
    int gn;
    gretl_equation_system *sys;
    gretl_vector *ydot;
    gretl_matrix *xdot;
    gretl_matrix *sdot;
};

static int 
fill_fiml_ydot (fiml_system *fsys, const double **Z, int t1)
{
    int i, t;

    /* stack the dependent variables vertically */
    for (i=0; i<fsys->g; i++) {
	int k = system_get_depvar(fsys->sys, i);

	for (t=0; t<fsys->n; t++) {
	    int gt = i * fsys->n + t;

	    gretl_vector_set(fsys->ydot, gt, Z[k][t + t1]);
	}
    }

    return 0;
}

static void fiml_system_destroy (fiml_system *fsys)
{
    if (fsys->ydot != NULL) {
	gretl_vector_free(fsys->ydot);
    }

    if (fsys->xdot != NULL) {
	gretl_matrix_free(fsys->xdot);
    }    

    if (fsys->sdot != NULL) {
	gretl_matrix_free(fsys->sdot);
    } 

    free(fsys);
}

static fiml_system *fiml_system_new (gretl_equation_system *sys)
{
    fiml_system *fsys;

    fsys = malloc(sizeof *fsys);
    if (fsys == NULL) return NULL;

    fsys->sys = sys;

    fsys->g = system_n_equations(sys);
    fsys->n = system_n_obs(sys);
    fsys->gn = fsys->g * fsys->n;

    fsys->ydot = NULL;
    fsys->xdot = NULL;
    fsys->sdot = NULL;

    fsys->ydot = gretl_vector_alloc(fsys->gn);
    if (fsys->ydot == NULL) goto bailout;

    fsys->sdot = gretl_matrix_alloc(fsys->gn, fsys->gn);
    if (fsys->sdot == NULL) goto bailout;
	
    fsys->xdot = gretl_matrix_alloc(fsys->gn, fsys->gn); /* FIXME dimensions */
    if (fsys->sdot == NULL) goto bailout;

    return fsys;

 bailout:
    fiml_system_destroy(fsys);
    return NULL;
}

static int 
make_fiml_dataset (fiml_system *fsys, const double **Z, const DATAINFO *pdinfo)
{
    double **fZ;
    DATAINFO *finfo;
    int nv;

    /* FIXME: calculate nv, the total number of variables required */

    finfo = create_new_dataset(&fZ, nv, fsys->gn, 0);
    if (finfo == NULL) return 1;

    /* work in progress */

    return 0;
}

int fiml_driver (gretl_equation_system *sys, const double **Z, const DATAINFO *pdinfo)
{
    fiml_system *fsys;
    int err = 0;

    fsys = fiml_system_new(sys);
    if (fsys == NULL) {
	return E_ALLOC;
    }

    err = fill_fiml_ydot(fsys, Z, pdinfo->t1); 

    /* work in progress */

    return err;
}

    
	

    
    
    

    
    
