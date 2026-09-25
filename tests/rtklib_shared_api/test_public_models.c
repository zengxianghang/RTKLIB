/*
 * ABI 1.2 explicit-parameter correction models.
 *
 * rtklib_shared_tropo_saastamoinen and rtklib_shared_klobuchar must be the
 * RTKLIB tropmodel/ionmodel with the caller's parameters (bit-exact over a
 * receiver/geometry/time grid), must not fall back to built-in defaults,
 * must reproduce rtklib_shared_tropo at its fixed humidity, and must follow
 * the models' zero-delay behaviour for non-positive elevations.
 */

#include "../../src/rtklib.h"
#include "../../src/rtklib_shared_api.h"

#include <math.h>
#include <stdio.h>

static int failures = 0;

#define CHECK(condition, message) \
    do { if (!(condition)) { fprintf(stderr, "FAIL: %s\n", message); \
         failures++; } } while (0)

int main(void)
{
    /* Real GPS broadcast Klobuchar coefficients (BRDC00IGS 2026-120 header
     * values are of this magnitude); any nonzero set exercises the wiring. */
    const double ion[8] = {
        2.6077E-08, 7.4506E-09, -1.1921E-07, 0.0,
        1.2698E+05, 0.0, -1.9661E+05, -6.5536E+04
    };
    const double zero[8] = {0};
    const double lats[] = {-1.2, -0.3, 0.0, 0.53, 1.4};
    const double lons[] = {-3.0, -1.0, 0.0, 1.99, 3.1};
    const double hgts[] = {-50.0, 0.0, 30.5, 2500.0};
    const double azs[] = {0.0, 1.1, 3.3, 5.9};
    const double els[] = {0.02, 0.2, 0.7, 1.4, 1.5707963267948966};
    const double humidity[] = {0.0, 0.3, 0.7, 1.0};
    rtklib_shared_time_t t = {2416, 345612.5};
    int a, b, c, d, e, h, checked = 0;
    double delay, shared_delay, variance;

    for (a = 0; a < 5; a++) for (b = 0; b < 5; b++) for (c = 0; c < 4; c++)
    for (d = 0; d < 4; d++) for (e = 0; e < 5; e++) {
        double pos[3] = {lats[a], lons[b], hgts[c]};
        double azel[2] = {azs[d], els[e]};
        gtime_t gt = gpst2time(t.week, t.sow);
        CHECK(rtklib_shared_klobuchar(t, ion, pos, azel, &delay) ==
                  RTKLIB_SHARED_OK &&
              delay == ionmodel(gt, ion, pos, azel),
              "Klobuchar differs from ionmodel");
        for (h = 0; h < 4; h++) {
            CHECK(rtklib_shared_tropo_saastamoinen(t, pos, azel, humidity[h],
                                                   &delay) ==
                      RTKLIB_SHARED_OK &&
                  delay == tropmodel(gt, pos, azel, humidity[h]),
                  "Saastamoinen differs from tropmodel");
        }
        CHECK(rtklib_shared_tropo_saastamoinen(t, pos, azel, 0.7, &delay) ==
                  RTKLIB_SHARED_OK &&
              rtklib_shared_tropo(NULL, t, pos, azel, RTKLIB_SHARED_TROPO_SAAS,
                                  &shared_delay, &variance) ==
                  RTKLIB_SHARED_OK && delay == shared_delay,
              "humidity 0.7 differs from the store-free SAAS helper");
        checked++;
    }
    {
        double pos[3] = {0.53, 1.99, 30.5}, azel[2] = {1.1, 0.7};
        double horizon[2] = {1.1, 0.0}, below[2] = {1.1, -0.1};
        CHECK(rtklib_shared_klobuchar(t, zero, pos, azel, &delay) ==
                  RTKLIB_SHARED_UNAVAILABLE,
              "all-zero Klobuchar coefficients fell back to defaults");
        CHECK(rtklib_shared_tropo_saastamoinen(t, pos, azel, -0.01, &delay) ==
                  RTKLIB_SHARED_INVALID_ARGUMENT &&
              rtklib_shared_tropo_saastamoinen(t, pos, azel, 1.01, &delay) ==
                  RTKLIB_SHARED_INVALID_ARGUMENT &&
              rtklib_shared_tropo_saastamoinen(t, pos, azel, NAN, &delay) ==
                  RTKLIB_SHARED_INVALID_ARGUMENT,
              "out-of-range humidity was accepted");
        CHECK(rtklib_shared_tropo_saastamoinen(t, pos, horizon, 0.0, &delay) ==
                  RTKLIB_SHARED_OK && delay == 0.0 &&
              rtklib_shared_klobuchar(t, ion, pos, below, &delay) ==
                  RTKLIB_SHARED_OK && delay == 0.0,
              "non-positive elevation did not follow the model's zero delay");
        CHECK(rtklib_shared_klobuchar(t, ion, pos, azel, NULL) ==
                  RTKLIB_SHARED_INVALID_ARGUMENT,
              "missing output accepted");
    }
    {
        /* Azimuth/elevation: bit-exact with satazel, including a line of
         * sight below the horizon (negative elevation is valid geometry). */
        double pos[3] = {0.53, 1.99, 30.5}, rr[3], sat_up[3], sat_down[3];
        double e_up[3], e_down[3], ref[2], azel[2];
        int i;
        pos2ecef(pos, rr);
        for (i = 0; i < 3; i++) {
            sat_up[i] = rr[i] * 4.0;           /* toward the zenith */
            sat_down[i] = -rr[i] * 3.0 + 1.0E6; /* through the earth */
        }
        geodist(sat_up, rr, e_up);
        geodist(sat_down, rr, e_down);
        CHECK(rtklib_shared_azel(pos, e_up, azel) == RTKLIB_SHARED_OK &&
              satazel(pos, e_up, ref) > 0.0 && azel[0] == ref[0] &&
              azel[1] == ref[1],
              "above-horizon azel differs from satazel");
        CHECK(rtklib_shared_azel(pos, e_down, azel) == RTKLIB_SHARED_OK &&
              satazel(pos, e_down, ref) < 0.0 && azel[0] == ref[0] &&
              azel[1] == ref[1] && azel[1] < 0.0,
              "below-horizon azel was not returned as negative elevation");
    }
    if (failures) {
        fprintf(stderr, "public_models: FAIL (%d)\n", failures);
        return 1;
    }
    printf("public_models: PASS (%d grid points; Klobuchar and Saastamoinen "
           "bit-exact with ionmodel/tropmodel; azel incl. below horizon)\n",
           checked);
    return 0;
}
