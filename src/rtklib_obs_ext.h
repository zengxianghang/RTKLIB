#ifndef RTKLIB_OBS_EXT_H
#define RTKLIB_OBS_EXT_H

#include "rtklib.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * RINEX 4.02 BeiDou observation codes required by the GNSS simulator but not
 * represented by the legacy RTKLIB observation-code table.
 *
 * These values intentionally continue the legacy CODE_* numbering.  They are
 * handled by the *_ext helpers below because the legacy MAXCODE remains 48.
 */
#ifndef CODE_L5P
#define CODE_L5P 49 /* BDS B2a pilot: 5P */
#endif
#ifndef CODE_L7D
#define CODE_L7D 50 /* BDS B2b data: 7D */
#endif
#ifndef CODE_L1D
#define CODE_L1D 51 /* BDS B1C data: 1D */
#endif

unsigned char obs2code_ext(const char *obs, int *freq);
const char *code2obs_ext(unsigned char code, int *freq);
int getcodepri_ext(int sys, unsigned char code, const char *opt);

#ifdef __cplusplus
}
#endif

#endif /* RTKLIB_OBS_EXT_H */
