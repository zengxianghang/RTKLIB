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
 * Interpret the raw RINEX/broadcast SV-health field for one observation
 * signal and message family without modifying the stored ephemeris value.
 * GPS CNAV uses the combined L1/L2/L5 health bits; GPS CNV2 carries L1C
 * health. Other systems/families preserve their raw health semantics.
 *
 * return: 0=healthy, nonzero=unhealthy/raw health value.
 */
int rtklib_signal_health_ext(int system, int message_type,
                             unsigned char code, int raw_svh);

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
 * Select and copy the epoch/message-family broadcast ephemeris used by the
 * signal-specific bias and state paths. Exactly one of eph/geph is populated.
 * This selector has no ephemeris propagation dependency, so bias-only users
 * can continue to link rtkcmn.c without ephemeris.c.
 */
int rtklib_signal_ephemeris_ext(gtime_t time, int sat, unsigned char code,
                                int required_message_mask, const nav_t *nav,
                                eph_t *eph, geph_t *geph,
                                rtklib_signal_bias_info_ext_t *info);

/*
 * Compute the broadcast satellite state for one signal/message family with
 * the same transmit-time convention used by satposs(): receive time minus
 * raw pseudorange/c, one broadcast-clock iteration, then broadcast position
 * and clock plus a 1 ms finite difference for velocity and clock drift.
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
