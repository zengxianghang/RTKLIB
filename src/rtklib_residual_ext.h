#ifndef RTKLIB_RESIDUAL_EXT_H
#define RTKLIB_RESIDUAL_EXT_H

#include "rtklib.h"
#include "rtklib_signal_bias_ext.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Evaluate one signal with the RTKLIB rescode measurement equation at a fixed
 * receiver state. obs->P[0] and obs->code[0] identify the signal under test;
 * wavelength_m is the actual carrier wavelength for that signal.
 */
int rtklib_rescode_signal_ext(const obsd_t *obs, const nav_t *nav,
                              const prcopt_t *opt,
                              const double receiver_ecef_m[3],
                              double receiver_clock_bias_m,
                              double receiver_system_bias_m,
                              int required_message_mask, double wavelength_m,
                              double *residual_m, double azel_rad[2],
                              rtklib_signal_bias_info_ext_t *bias_info);

/*
 * Evaluate one signal with the RTKLIB resdop equation at a fixed receiver
 * state. obs->D[0] is Doppler in Hz and wavelength_m is the actual signal
 * wavelength, including GLONASS FCN dependence where applicable. The message
 * mask constrains satellite state selection to the signal's NAV family.
 */
int rtklib_resdop_signal_ext(const obsd_t *obs, const nav_t *nav,
                             const prcopt_t *opt,
                             const double receiver_ecef_m[3],
                             const double receiver_velocity_ecef_mps[3],
                             double receiver_clock_drift_mps,
                             int required_message_mask, double wavelength_m,
                             double *residual_mps, double azel_rad[2]);

#ifdef __cplusplus
}
#endif

#endif /* RTKLIB_RESIDUAL_EXT_H */
