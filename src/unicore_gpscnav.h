#ifndef UNICORE_GPSC_NAV_H
#define UNICORE_GPSC_NAV_H

#include "rtklib.h"

#ifdef __cplusplus
extern "C" {
#endif

/* write Unicore GPSCNAVEPHA ASCII sentence */
int write_unicore_gpscnav_eph(FILE *fp, const eph_t *eph);

#ifdef __cplusplus
}
#endif

#endif
