#include "rtklib.h"
#include "rtklib_pntvel_ext.h"
#include "rtklib_residual_ext.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/*
 * Physics check for rtklib_range_rate_ext() and rtklib_pntvel_ext(): the
 * Doppler range rate must be the rate of the simulated light-time range
 * rho(t)=geodist(rs(t-rho/c),rr(t)), evaluated here by finite differences of
 * eph2pos() + geodist() only.
 */

#define NSAT 8

static void init_eph(eph_t *eph, int sat, gtime_t toe, double m0, double omg0)
{
    memset(eph,0,sizeof(*eph));
    eph->sat=sat;
    eph->toe=eph->toc=eph->ttr=toe;
    eph->hdr.msg_type=NAV_LNAV;
    eph->iode=eph->iodc=1;
    eph->A=26560E3;
    eph->e=0.02;
    eph->i0=0.96;
    eph->OMG0=omg0;
    eph->omg=0.5;
    eph->M0=m0;
    eph->f0=1E-5;
    eph->f1=3E-12;
}

static void receiver_at(const double *rr0, const double *vr, double dt,
                        double *rr)
{
    int i;
    for (i=0;i<3;i++) rr[i]=rr0[i]+vr[i]*dt;
}

/* Light-time range at receive time t0+dt; optional transmit-time state. */
static double light_time_range(const eph_t *eph, gtime_t t0, double dt,
                               const double *rr0, const double *vr,
                               double *rs_out, double *dts_out,
                               double *los_out)
{
    double rr[3],rs[3],dts,var,e[3],rho=0.075*CLIGHT;
    gtime_t t=timeadd(t0,dt);
    int i;

    receiver_at(rr0,vr,dt,rr);
    for (i=0;i<20;i++) {
        eph2pos(timeadd(t,-rho/CLIGHT),eph,rs,&dts,&var);
        rho=geodist(rs,rr,e);
    }
    if (rs_out) memcpy(rs_out,rs,sizeof(rs));
    if (dts_out) *dts_out=dts;
    if (los_out) memcpy(los_out,e,sizeof(e));
    return rho;
}

static double stock_resdop_rate(const double *rs, const double *rr,
                                const double *vr, const double *e)
{
    double relative_velocity[3];
    int i;
    for (i=0;i<3;i++) relative_velocity[i]=rs[i+3]-vr[i];
    return dot(relative_velocity,e,3)+OMGE/CLIGHT*(
           rs[4]*rr[0]+rs[1]*vr[0]-rs[3]*rr[1]-rs[0]*vr[1]);
}

static int check_range_rate(const eph_t *eph, gtime_t t0, const double *rr0,
                            const double *vr, double *max_stock_error)
{
    const double h=0.1,hv=1E-3;
    double rs[6],before[3],after[3],dts,var,los[3],fd,rate;
    gtime_t tt;
    int i;

    fd=(light_time_range(eph,t0,h,rr0,vr,NULL,NULL,NULL)-
        light_time_range(eph,t0,-h,rr0,vr,NULL,NULL,NULL))/(2.0*h);
    light_time_range(eph,t0,0.0,rr0,vr,rs,&dts,los);

    /* Central-difference satellite velocity isolates the range-rate model
     * from the 1 ms forward difference used inside satposs(). */
    tt=timeadd(t0,-geodist(rs,rr0,los)/CLIGHT);
    eph2pos(timeadd(tt,-hv),eph,before,&dts,&var);
    eph2pos(timeadd(tt,hv),eph,after,&dts,&var);
    for (i=0;i<3;i++) rs[i+3]=(after[i]-before[i])/(2.0*hv);

    rate=rtklib_range_rate_ext(rs,rr0,vr,los);
    if (fabs(rate-fd)>2E-5) {
        fprintf(stderr,"sat %d range rate %.9f vs finite difference %.9f\n",
                eph->sat,rate,fd);
        return 0;
    }
    if (fabs(stock_resdop_rate(rs,rr0,vr,los)-fd)>*max_stock_error) {
        *max_stock_error=fabs(stock_resdop_rate(rs,rr0,vr,los)-fd);
    }
    return 1;
}

static int check_point_velocity(const nav_t *nav, const eph_t *eph,
                                 gtime_t t0, const double *rr0,
                                 const double *vr)
{
    const double h=0.1,wavelength=CLIGHT/FREQ1;
    obsd_t obs[NSAT];
    unsigned char doppler_valid[NSAT];
    double wavelengths[NSAT],velocity[3],drift,rho,rho_rate,dts,before,after;
    double receiver_drift_mps=0.25;
    prcopt_t opt=prcopt_default;
    char msg[128];
    int i,used=0;

    opt.elmin=-PI/2.0;
    memset(obs,0,sizeof(obs));
    for (i=0;i<NSAT;i++) {
        rho=light_time_range(eph+i,t0,0.0,rr0,vr,NULL,&dts,NULL);
        rho_rate=(light_time_range(eph+i,t0,h,rr0,vr,NULL,&after,NULL)-
                  light_time_range(eph+i,t0,-h,rr0,vr,NULL,&before,NULL))/(2.0*h);
        obs[i].time=t0;
        obs[i].sat=(unsigned char)eph[i].sat;
        obs[i].code[0]=CODE_L1C;
        obs[i].P[0]=rho-CLIGHT*dts;
        obs[i].D[0]=(float)(-(rho_rate+receiver_drift_mps-
                              CLIGHT*(after-before)/(2.0*h))/wavelength);
        doppler_valid[i]=1;
        wavelengths[i]=wavelength;
    }
    if (!rtklib_pntvel_ext(obs,doppler_valid,wavelengths,NSAT,nav,&opt,rr0,
                           velocity,&drift,&used,msg)||used!=NSAT) {
        fprintf(stderr,"pntvel failed: %s used=%d\n",msg,used);
        return 0;
    }
    /* Residual error: satposs() 1 ms forward-difference velocity and float
     * Doppler; the stock first-order model leaves several mm/s. */
    for (i=0;i<3;i++) {
        if (fabs(velocity[i]-vr[i])>1.5E-3) {
            fprintf(stderr,"pntvel velocity[%d]=%.6f expected %.6f\n",
                    i,velocity[i],vr[i]);
            return 0;
        }
    }
    if (fabs(drift-receiver_drift_mps)>1.5E-3) {
        fprintf(stderr,"pntvel drift=%.6f expected %.6f\n",drift,
                receiver_drift_mps);
        return 0;
    }
    return 1;
}

int main(void)
{
    nav_t nav={0};
    eph_t eph[NSAT];
    gtime_t t0=gpst2time(2300,100000.0);
    double pos[3]={30.5*D2R,114.4*D2R,70.0},rr0[3];
    double static_velocity[3]={0},moving_velocity[3]={15.0,-7.0,3.0};
    double max_stock_error=0.0;
    int i;

    pos2ecef(pos,rr0);
    for (i=0;i<NSAT;i++) {
        init_eph(eph+i,satno(SYS_GPS,i+1),t0,0.8*i,0.785*i);
    }
    nav.eph=eph; nav.n=nav.nmax=NSAT;

    for (i=0;i<NSAT;i++) {
        if (!check_range_rate(eph+i,t0,rr0,static_velocity,&max_stock_error)||
            !check_range_rate(eph+i,t0,rr0,moving_velocity,&max_stock_error)) {
            return 1;
        }
    }
    /* The stock first-order term must be measurably wrong on this geometry,
     * otherwise the checks above would not discriminate the models. */
    if (max_stock_error<2E-3) {
        fprintf(stderr,"stock resdop error unexpectedly small: %.6f\n",
                max_stock_error);
        return 1;
    }
    if (!check_point_velocity(&nav,eph,t0,rr0,static_velocity)||
        !check_point_velocity(&nav,eph,t0,rr0,moving_velocity)) return 1;

    printf("range_rate_ext: PASS (stock resdop max error %.4f m/s)\n",
           max_stock_error);
    return 0;
}
