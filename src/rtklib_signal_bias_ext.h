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
 * required_message_type=0 allows any message family compatible with code.
 * Otherwise only the requested RINEX-4 navigation message family is eligible.
 *
 * return: 1=available, 0=no applicable broadcast bias/message family,
 *        -1=invalid arguments.
 */
int rtklib_signal_code_bias_ext(gtime_t time, int sat, unsigned char code,
                                int required_message_type, const nav_t *nav,
                                double *raw_code_bias_m,
                                rtklib_signal_bias_info_ext_t *info);

#ifdef __cplusplus
}
#endif

#endif /* RTKLIB_SIGNAL_BIAS_EXT_H */
