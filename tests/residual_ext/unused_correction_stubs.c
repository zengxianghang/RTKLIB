#include "rtklib.h"

/*
 * The residual-extension unit test exercises only broadcast ephemerides with
 * IONOOPT_OFF/TROPOPT_OFF. The linked RTKLIB translation units still reference
 * optional SBAS/TEC/LEX/precise-orbit and antenna-offset branches, so provide
 * test-only stubs instead of linking unrelated implementations.
 */
int sbsioncorr(gtime_t time, const nav_t *nav, const double *pos,
               const double *azel, double *delay, double *var)
{
    (void)time; (void)nav; (void)pos; (void)azel; (void)delay; (void)var;
    return 0;
}

double sbstropcorr(gtime_t time, const double *pos, const double *azel,
                   double *var)
{
    (void)time; (void)pos; (void)azel; (void)var;
    return 0.0;
}

int iontec(gtime_t time, const nav_t *nav, const double *pos,
           const double *azel, int opt, double *delay, double *var)
{
    (void)time; (void)nav; (void)pos; (void)azel; (void)opt; (void)delay; (void)var;
    return 0;
}

int lexioncorr(gtime_t time, const nav_t *nav, const double *pos,
               const double *azel, double *delay, double *var)
{
    (void)time; (void)nav; (void)pos; (void)azel; (void)delay; (void)var;
    return 0;
}

void satantoff(gtime_t time, const double *rs, int sat, const nav_t *nav,
               double *dant)
{
    (void)time; (void)rs; (void)sat; (void)nav;
    if (dant) {
        dant[0]=0.0;
        dant[1]=0.0;
        dant[2]=0.0;
    }
}

int lexeph2pos(gtime_t time, int sat, const nav_t *nav, double *rs,
               double *dts, double *var)
{
    (void)time; (void)sat; (void)nav; (void)rs; (void)dts; (void)var;
    return 0;
}

int sbssatcorr(gtime_t time, int sat, const nav_t *nav, double *rs,
               double *dts, double *var)
{
    (void)time; (void)sat; (void)nav; (void)rs; (void)dts; (void)var;
    return 0;
}

int peph2pos(gtime_t time, int sat, const nav_t *nav, int opt,
             double *rs, double *dts, double *var)
{
    (void)time; (void)sat; (void)nav; (void)opt; (void)rs; (void)dts; (void)var;
    return 0;
}
