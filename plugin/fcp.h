#ifndef FCP_H
#define FCP_H

int garch_estimate (int t1, int t2, int nobs, 
		    const double **X, int nx, double *ydet, 
		    double *coeff, int nc, double *vc, 
		    double *res2, double *res,
		    const double *ystoc, double *amax, double *b, 
		    int *iters, PRN *prn, int robust);

#endif /* FCP_H */




