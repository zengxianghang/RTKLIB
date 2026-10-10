#ifndef UNICORE_GPSC_NAV_H
#define UNICORE_GPSC_NAV_H

#include "rtklib.h"

#ifdef __cplusplus
extern "C" {
#endif

/* write Unicore GPSCNAVEPHA ASCII sentence for CNAV ephemeris */
int write_unicore_gpscnav_eph(FILE *fp, const eph_t *eph);

/* write GPSL1CEPHEMA with a NovAtel-style ASCII header for CNV2/L1C */
int write_novatel_gpsl1c_eph(FILE *fp, const eph_t *eph);

#ifdef __cplusplus
}
#endif

#endif
