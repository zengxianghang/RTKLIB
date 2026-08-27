#include "rtklib.h"

/*
 * The residual-extension unit test exercises only IONOOPT_OFF/TROPOPT_OFF.
 * pntpos.c still references these optional branches at link time, so provide
 * test-only stubs instead of linking unrelated SBAS/TEC/LEX implementations.
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
