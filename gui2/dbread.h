/*
 *   Copyright (c) by Allin Cottrell
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

enum {
    DB_OK = 0,
    DB_MISSING_DATA,
    DB_NOT_FOUND
};

enum compaction_methods {
    COMPACT_NONE,
    COMPACT_SUM,
    COMPACT_AVG,
    COMPACT_SOP,
    COMPACT_EOP
}; 

typedef struct _db_table_row db_table_row;
typedef struct _db_table db_table;

struct _db_table_row {
    char *varname;
    char *comment;
    char *obsinfo;
};

struct _db_table {
    int nrows;
    db_table_row *rows;
};

typedef struct _SERIESINFO SERIESINFO;

struct _SERIESINFO {
    char varname[16];
    char descrip[MAXLABEL];
    int nobs;
    char stobs[9];
    char endobs[9];
    int pd;
    int offset;
    int err;
    int undated;
};

db_table *read_RATS_db (FILE *fp);

int get_rats_data (const char *fname, const int series_number,
		   SERIESINFO *sinfo, double ***pZ);

int mon_to_quart (double **pq, double *mvec, SERIESINFO *sinfo,
		  gint method);

int to_annual (double **pq, double *mvec, SERIESINFO *sinfo,
	       gint method);

