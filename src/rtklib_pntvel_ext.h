#ifndef RTKLIB_PNTVEL_EXT_H
#define RTKLIB_PNTVEL_EXT_H

#include "rtklib.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Independent Doppler-only receiver velocity estimator.
 *
 * The equations match pntpos.c/resdop()+estvel(), while the caller supplies
 * the receiver ECEF position used for LOS linearization and an explicit
 * per-observation validity mask. This allows velocity to remain solvable when
 * the current pseudorange position solution is invalid but a position hint is
 * available.
 *
 * rs:  satellite {x,y,z,vx,vy,vz}, 6*n (m|m/s)
 * dts: satellite clock {bias,drift}, 2*n (s|s/s)
 * rr:  receiver ECEF position hint (m)
 * use: optional n-entry mask; NULL means all observations are candidates
 * vel: receiver ECEF velocity (m/s)
 * clkdrift: receiver clock drift in range-rate units (m/s)
 * used: number of Doppler observations used in the converged iteration
 * msg: optional diagnostic buffer, at least 128 bytes if non-NULL
 */
int rtklib_pntvel_ext(const obsd_t *obs, int n, const double *rs,
                      const double *dts, const nav_t *nav, const double *rr,
                      const int *use, double *vel, double *clkdrift, int *used,
                      char *msg);

#ifdef __cplusplus
}
#endif

#endif /* RTKLIB_PNTVEL_EXT_H */
