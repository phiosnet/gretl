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

/* subsample.h for gretl */

#ifndef SUBSAMPLE_H
#define SUBSAMPLE_H

typedef enum {
    SUBSAMPLE_NONE,
    SUBSAMPLE_DROP_MISSING,
    SUBSAMPLE_USE_DUMMY,
    SUBSAMPLE_BOOLEAN,
    SUBSAMPLE_RANDOM,
    SUBSAMPLE_UNKNOWN
} SubsampleMode;

char *copy_subsample_mask (const char *src);

char *copy_datainfo_submask (const DATAINFO *pdinfo);

int attach_subsample_to_model (MODEL *pmod, const DATAINFO *pdinfo);

int restrict_sample (const char *line, const int *list,  
		     double ***pZ, DATAINFO **ppdinfo,
		     gretlopt oflag, PRN *prn);

int 
restrict_sample_from_mask (const char *mask, int mode, 
			   double ***pZ, DATAINFO **ppdinfo);

int complex_subsampled (void);

int get_full_length_n (void);

int set_sample (const char *line, const double **Z, DATAINFO *pdinfo);

int restore_full_sample (double ***pZ, DATAINFO **ppdinfo); 

int count_missing_values (double ***pZ, DATAINFO *pdinfo, PRN *prn);

int add_dataset_to_model (MODEL *pmod, const DATAINFO *pdinfo);

void free_model_dataset (MODEL *pmod);

void maybe_free_full_dataset (const DATAINFO *pdinfo);

int model_mask_leaves_balanced_panel (const MODEL *pmod,
				      const DATAINFO *pdinfo);

#endif /* SUBSAMPLE_H */
