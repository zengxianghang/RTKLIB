#ifndef RTKLIB_SIGNAL_BIAS_EXT_H
#define RTKLIB_SIGNAL_BIAS_EXT_H

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
 * pseudorange. The value follows the broadcast satellite-clock reference:
 * raw_P = common_range_terms + raw_code_bias_m.
 *
 * required_message_mask=0 allows any message family compatible with code.
 * Otherwise any compatible NAV_* bit present in the mask is eligible.
 *
 * return: 1=available, 0=no applicable broadcast bias/message family,
 *        -1=invalid arguments.
 */
int rtklib_signal_code_bias_ext(gtime_t time, int sat, unsigned char code,
                                int required_message_mask, const nav_t *nav,
                                double *raw_code_bias_m,
                                rtklib_signal_bias_info_ext_t *info);

/*
 * Compute the broadcast satellite state for one signal/message family with
 * the same transmit-time convention used by satposs(): receive time minus
 * raw pseudorange/c, one broadcast-clock iteration, then broadcast position
 * and clock plus a 1 ms finite difference for velocity and clock drift.
 *
 * The ephemeris selector is the same signal/message-family selector used by
 * rtklib_signal_code_bias_ext(), preventing mixed-family residuals such as a
 * CNAV code bias evaluated with an LNAV satellite state.
 */
int rtklib_signal_state_ext(gtime_t receive_time, double pseudorange_m,
                            int sat, unsigned char code,
                            int required_message_mask, const nav_t *nav,
                            double rs[6], double dts[2], double *var,
                            int *svh, rtklib_signal_bias_info_ext_t *info);

#ifdef __cplusplus
}
#endif

#endif /* RTKLIB_SIGNAL_BIAS_EXT_H */
