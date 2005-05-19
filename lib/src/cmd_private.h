/* private stuff for gretl CMD structure */

#ifndef CMD_PRIVATE_H
#define CMD_PRIVATE_H

typedef struct Laginfo_ Laginfo;

struct _CMD {
    char word[9];               /* command word */
    int ci;                     /* command index number */
    int context;                /* context for subsetted commands */
    gretlopt opt;               /* option flags */
    char savename[MAXSAVENAME]; /* name used to save an object from the command */
    char str[4];                /* extra string: used, e.g., in "multiply" command */
    int nolist;                 /* = 1 if the command does not take a list */
    int *list;                  /* list of variables by ID number */
    char *param;                /* general-purpose parameter to command */
    int ignore;                 /* flag set to ignore comment lines in input */
    int errcode;                /* error code */
    Laginfo *linfo;             /* struct for recording info on automatically
                                   generated lags */
};

#endif /* CMD_PRIVATE_H */
