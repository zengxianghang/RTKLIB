#ifndef RTKLIB_RESIDUAL_EXT_H
#define RTKLIB_RESIDUAL_EXT_H

#include "rtklib.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int system;
    int message_type;
    int iode;
    double raw_code_bias_m;
} rtklib_signal_bias_info_ext_t;

/*
 * Return the broadcast signal-specific code bias contained in a raw
 * pseudorange.  The value follows the satellite-clock reference convention:
 * raw_P = common_range_terms + raw_code_bias_m.
 *
 * return: 1=available, 0=no applicable broadcast bias/message family,
 *        -1=invalid arguments.
 */
int rtklib_signal_code_bias_ext(gtime_t time, int sat, unsigned char code,
                                const nav_t *nav, double *raw_code_bias_m,
                                rtklib_signal_bias_info_ext_t *info);

/*
 * Evaluate a single signal with the RTKLIB rescode measurement equation at a
 * fixed receiver state.  obs->P[0] and obs->code[0] identify the signal under
 * test; wavelength_m is the actual carrier wavelength for that signal.
 * receiver_system_bias_m is the constellation-specific receiver offset used by
 * rescode (0 for GPS/QZSS, GLO-GPS for GLO, GAL-GPS for GAL, BDS-GPS for CMP).
 */
int rtklib_rescode_signal_ext(const obsd_t *obs, const nav_t *nav,
                              const prcopt_t *opt,
                              const double receiver_ecef_m[3],
                              double receiver_clock_bias_m,
                              double receiver_system_bias_m,
                              double wavelength_m,
                              double *residual_m, double azel_rad[2],
                              rtklib_signal_bias_info_ext_t *bias_info);

/*
 * Evaluate a single signal with the RTKLIB resdop equation at a fixed receiver
 * state.  obs->D[0] is Doppler in Hz and wavelength_m is the actual signal
 * wavelength (including GLONASS FCN dependence).
 */
int rtklib_resdop_signal_ext(const obsd_t *obs, const nav_t *nav,
                             const prcopt_t *opt,
                             const double receiver_ecef_m[3],
                             const double receiver_velocity_ecef_mps[3],
                             double receiver_clock_drift_mps,
                             double wavelength_m,
                             double *residual_mps, double azel_rad[2]);

#ifdef __cplusplus
}
#endif

#endif /* RTKLIB_RESIDUAL_EXT_H */
