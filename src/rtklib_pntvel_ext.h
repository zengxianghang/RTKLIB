#ifndef RTKLIB_PNTVEL_EXT_H
#define RTKLIB_PNTVEL_EXT_H

#include "rtklib.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Standalone single-point velocity estimate from Doppler.
 *
 * `receiver_ecef_m` is a position hint used only to form line-of-sight vectors
 * and apply the same Earth-rotation term as pntpos.c/resdop(). A current
 * pseudorange position fix is not required.
 *
 * `doppler_valid` contains one byte per observation (0=ignore, nonzero=use).
 * Each observation must still carry a nonzero P[0] so RTKLIB satposs() can
 * derive signal transmission time from Receiver NAV.
 *
 * `receiver_clock_drift_mps` is returned in range-rate units (m/s), matching
 * the fourth state of pntpos.c/resdop().
 */
int rtklib_pntvel_ext(const obsd_t *obs, const unsigned char *doppler_valid,
                      int n, const nav_t *nav, const prcopt_t *opt,
                      const double *receiver_ecef_m, double *velocity_ecef_mps,
                      double *receiver_clock_drift_mps, int *used_satellites,
                      char *msg);

#ifdef __cplusplus
}
#endif

#endif /* RTKLIB_PNTVEL_EXT_H */
