#ifndef RANGE_RATE_REFERENCE_H
#define RANGE_RATE_REFERENCE_H

#include "rtklib.h"

/*
 * Test-side reference for d/dt geodist(rs(t-rho/c),rr(t)), written from the
 * geodist() gradients rather than by calling rtklib_range_rate_ext():
 *   rho_dot = g_s.vs*(1-rho_dot/c) + g_r.vr
 *   g_s = los + omega/c*( y_r,-x_r,0),  g_r = -los + omega/c*(-y_s,x_s,0)
 */
static double reference_range_rate(const double *rs, const double *rr,
                                   const double *vr, const double *los)
{
    double gs[3],gr[3],satellite_rate=0.0,receiver_rate=0.0;
    int i;

    for (i=0;i<3;i++) {
        gs[i]=los[i];
        gr[i]=-los[i];
    }
    gs[0]+=OMGE/CLIGHT*rr[1];
    gs[1]-=OMGE/CLIGHT*rr[0];
    gr[0]-=OMGE/CLIGHT*rs[1];
    gr[1]+=OMGE/CLIGHT*rs[0];
    for (i=0;i<3;i++) {
        satellite_rate+=gs[i]*rs[i+3];
        receiver_rate+=gr[i]*vr[i];
    }
    return (satellite_rate+receiver_rate)/(1.0+satellite_rate/CLIGHT);
}

#endif /* RANGE_RATE_REFERENCE_H */
