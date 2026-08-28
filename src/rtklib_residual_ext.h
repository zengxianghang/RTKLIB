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
 * Diagnostic variant for pre-operational/developmental signals whose broadcast
 * navigation message intentionally reports unhealthy. This bypasses only the
 * broadcast-health exclusion. All other signal-family, state, code-bias,
 * elevation-mask, opt->navsys, and opt->exsats checks remain active.
 */
int rtklib_rescode_signal_diagnostic_ext(
    const obsd_t *obs, const nav_t *nav, const prcopt_t *opt,
    const double receiver_ecef_m[3], double receiver_clock_bias_m,
    double receiver_system_bias_m, int required_message_mask,
    double wavelength_m, double *residual_m, double azel_rad[2],
    rtklib_signal_bias_info_ext_t *bias_info);

/*
 * Evaluate code residual with an externally supplied satellite state and code
 * bias while retaining RTKLIB geometry, elevation, atmosphere, exclusion, and
 * residual sign conventions. satellite_state_ecef must be evaluated at signal
 * transmit time and is {x,y,z,vx,vy,vz} in m|m/s. satellite_clock is
 * {bias,drift} in s|s/s at the same transmit time. This API is intended for
 * coherent external products such as Galileo HAS precise orbit/clock + OSB;
 * it does not select or reinterpret a broadcast NAV family.
 */
int rtklib_rescode_state_ext(
    const obsd_t *obs, const nav_t *nav, const prcopt_t *opt,
    const double receiver_ecef_m[3], double receiver_clock_bias_m,
    double receiver_system_bias_m, const double satellite_state_ecef[6],
    const double satellite_clock[2], int satellite_health,
    double code_bias_m, double wavelength_m, double *residual_m,
    double azel_rad[2]);

/*
 * Evaluate one signal with the RTKLIB resdop equation at a fixed receiver
 * state. obs->D[0] is Doppler in Hz and wavelength_m is the actual signal
 * wavelength, including GLONASS FCN dependence where applicable. A nonzero
 * message mask constrains satellite-state selection to a NAV family compatible
 * with obs->code[0]. A zero mask selects the nearest generic broadcast state,
 * allowing Doppler validation when signal-specific code-bias NAV is absent.
 */
int rtklib_resdop_signal_ext(const obsd_t *obs, const nav_t *nav,
                             const prcopt_t *opt,
                             const double receiver_ecef_m[3],
                             const double receiver_velocity_ecef_mps[3],
                             double receiver_clock_drift_mps,
                             int required_message_mask, double wavelength_m,
                             double *residual_mps, double azel_rad[2]);

/*
 * Doppler diagnostic counterpart to rtklib_rescode_signal_diagnostic_ext().
 * Broadcast health is ignored only for residual-model validation; explicit
 * satellite/system exclusions and all geometric/state checks remain active.
 */
int rtklib_resdop_signal_diagnostic_ext(
    const obsd_t *obs, const nav_t *nav, const prcopt_t *opt,
    const double receiver_ecef_m[3],
    const double receiver_velocity_ecef_mps[3],
    double receiver_clock_drift_mps, int required_message_mask,
    double wavelength_m, double *residual_mps, double azel_rad[2]);

/*
 * Explicit-state Doppler counterpart to rtklib_rescode_state_ext(). The
 * satellite state/clock must refer to signal transmit time. No code bias is
 * consumed by the Doppler equation.
 */
int rtklib_resdop_state_ext(
    const obsd_t *obs, const prcopt_t *opt,
    const double receiver_ecef_m[3],
    const double receiver_velocity_ecef_mps[3],
    double receiver_clock_drift_mps, const double satellite_state_ecef[6],
    const double satellite_clock[2], int satellite_health,
    double wavelength_m, double *residual_mps, double azel_rad[2]);

#ifdef __cplusplus
}
#endif

#endif /* RTKLIB_RESIDUAL_EXT_H */
