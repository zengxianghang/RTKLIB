/* Private implementation regression for the legacy GPS LNAV RINEX 2 metric
 * SVA path.  This does not define GLO, SBAS, or modern URAI/SISA behavior. */
#include "../../src/rtklib.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static int fail(const char *message)
{
    fprintf(stderr, "FAIL: %s\n", message);
    return 1;
}

static int close_finite(double actual, double expected, double tolerance)
{
    return isfinite(actual) && isfinite(expected) &&
           fabs(actual - expected) <= tolerance;
}

int main(int argc, char **argv)
{
    nav_t nav = {0};
    const eph_t *target = NULL;
    double state[6] = {0.0};
    double dts[2] = {0.0};
    double variance = 0.0;
    int sat;
    int i;
    int status;

    if (argc != 2)
        return fail("usage: test_rinex2_legacy_sva_metric <RINEX2 NAV>");
    sat = satno(SYS_GPS, 1);
    if (sat <= 0) return fail("G01 satellite number is unavailable");

    status = readrnx(argv[1], 1, "", NULL, &nav, NULL);
    if (status <= 0) {
        freenav(&nav, 0x3ff);
        return fail("RINEX2 fixture could not be parsed");
    }
    for (i = 0; i < nav.n; ++i) {
        const eph_t *eph = nav.eph + i;
        if (eph->sat == sat && eph->week == 1590 &&
            close_finite(eph->toes, 345600.0, 1E-6) && eph->iode == 63) {
            target = eph;
            break;
        }
    }
    if (!target) {
        freenav(&nav, 0x3ff);
        return fail("RINEX2 fixture G01 record was not found");
    }
    if (!close_finite(target->sva, 2.0, 1E-12)) {
        freenav(&nav, 0x3ff);
        return fail("RINEX2 G01 SVA was not retained as 2.0 metres");
    }

    eph2pos(target->toe, target, state, dts, &variance);
    if (!close_finite(variance, 4.0, 1E-12)) {
        freenav(&nav, 0x3ff);
        return fail("RINEX2 G01 SVA variance was not 4.0 m^2");
    }
    freenav(&nav, 0x3ff);
    puts("rinex2_legacy_sva_metric: PASS (GPS LNAV unit check; not cross-backend parity)");
    return 0;
}
