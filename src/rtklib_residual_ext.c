#include "rtklib_residual_ext.h"

#include <math.h>

static int rescode_signal_ext_impl(
    const obsd_t *obs, const nav_t *nav, const prcopt_t *opt,
    const double receiver_ecef_m[3], double receiver_clock_bias_m,
    double receiver_system_bias_m, int required_message_mask,
    double wavelength_m, int ignore_broadcast_health, double *residual_m,
    double azel_rad[2], rtklib_signal_bias_info_ext_t *bias_info)
{
    double rs[6]={0},dts[2]={0},vare=0.0,e[3],pos[3],azel[2];
    double r,code_bias,dion=0.0,vion=0.0,dtrp=0.0,vtrp=0.0,p;
    int svh=0,stat;

    if (!obs||!nav||!opt||!receiver_ecef_m||!residual_m||
        !isfinite(wavelength_m)||wavelength_m<=0.0||obs->P[0]<=0.0) return -1;

    stat=rtklib_signal_state_ext(obs->time,obs->P[0],obs->sat,obs->code[0],
                                 required_message_mask,nav,rs,dts,&vare,&svh,
                                 NULL);
    if (stat<=0) return stat;
    if ((r=geodist(rs,receiver_ecef_m,e))<=0.0) return 0;
    ecef2pos(receiver_ecef_m,pos);
    if (satazel(pos,e,azel)<opt->elmin||
        satexclude(obs->sat,ignore_broadcast_health?0:svh,opt)) return 0;

    stat=rtklib_signal_code_bias_ext(obs->time,obs->sat,obs->code[0],
                                     required_message_mask,nav,&code_bias,
                                     bias_info);
    if (stat<=0) return stat;

    if (!ionocorr(obs->time,nav,obs->sat,pos,azel,opt->ionoopt,
                  &dion,&vion)) return 0;
    dion*=(wavelength_m/lam_carr[0])*(wavelength_m/lam_carr[0]);
    if (!tropcorr(obs->time,nav,pos,azel,opt->tropopt,&dtrp,&vtrp)) return 0;

    p=obs->P[0]-code_bias;
    *residual_m=p-(r+receiver_clock_bias_m-CLIGHT*dts[0]+dion+dtrp)-
                receiver_system_bias_m;
    if (azel_rad) {
        azel_rad[0]=azel[0];
        azel_rad[1]=azel[1];
    }
    return isfinite(*residual_m)?1:-1;
}

int rtklib_rescode_signal_ext(const obsd_t *obs, const nav_t *nav,
                              const prcopt_t *opt,
                              const double receiver_ecef_m[3],
                              double receiver_clock_bias_m,
                              double receiver_system_bias_m,
                              int required_message_mask, double wavelength_m,
                              double *residual_m, double azel_rad[2],
                              rtklib_signal_bias_info_ext_t *bias_info)
{
    return rescode_signal_ext_impl(obs,nav,opt,receiver_ecef_m,
                                   receiver_clock_bias_m,
                                   receiver_system_bias_m,
                                   required_message_mask,wavelength_m,0,
                                   residual_m,azel_rad,bias_info);
}

int rtklib_rescode_signal_diagnostic_ext(
    const obsd_t *obs, const nav_t *nav, const prcopt_t *opt,
    const double receiver_ecef_m[3], double receiver_clock_bias_m,
    double receiver_system_bias_m, int required_message_mask,
    double wavelength_m, double *residual_m, double azel_rad[2],
    rtklib_signal_bias_info_ext_t *bias_info)
{
    return rescode_signal_ext_impl(obs,nav,opt,receiver_ecef_m,
                                   receiver_clock_bias_m,
                                   receiver_system_bias_m,
                                   required_message_mask,wavelength_m,1,
                                   residual_m,azel_rad,bias_info);
}

int rtklib_rescode_state_ext(
    const obsd_t *obs, const nav_t *nav, const prcopt_t *opt,
    const double receiver_ecef_m[3], double receiver_clock_bias_m,
    double receiver_system_bias_m, const double satellite_state_ecef[6],
    const double satellite_clock[2], int satellite_health,
    double code_bias_m, double wavelength_m, double *residual_m,
    double azel_rad[2])
{
    double e[3],pos[3],azel[2],dion=0.0,vion=0.0,dtrp=0.0,vtrp=0.0;
    double r,p;
    int i;

    if (!obs||!nav||!opt||!receiver_ecef_m||!satellite_state_ecef||
        !satellite_clock||!residual_m||obs->sat<=0||obs->P[0]<=0.0||
        !isfinite(receiver_clock_bias_m)||!isfinite(receiver_system_bias_m)||
        !isfinite(code_bias_m)||!isfinite(wavelength_m)||wavelength_m<=0.0||
        !isfinite(satellite_clock[0])) return -1;
    for (i=0;i<3;i++) {
        if (!isfinite(receiver_ecef_m[i])||
            !isfinite(satellite_state_ecef[i])) return -1;
    }

    if ((r=geodist(satellite_state_ecef,receiver_ecef_m,e))<=0.0) return 0;
    ecef2pos(receiver_ecef_m,pos);
    if (satazel(pos,e,azel)<opt->elmin||
        satexclude(obs->sat,satellite_health,opt)) return 0;

    if (!ionocorr(obs->time,nav,obs->sat,pos,azel,opt->ionoopt,
                  &dion,&vion)) return 0;
    dion*=(wavelength_m/lam_carr[0])*(wavelength_m/lam_carr[0]);
    if (!tropcorr(obs->time,nav,pos,azel,opt->tropopt,&dtrp,&vtrp)) return 0;

    p=obs->P[0]-code_bias_m;
    *residual_m=p-(r+receiver_clock_bias_m-CLIGHT*satellite_clock[0]+
                  dion+dtrp)-receiver_system_bias_m;
    if (azel_rad) {
        azel_rad[0]=azel[0];
        azel_rad[1]=azel[1];
    }
    return isfinite(*residual_m)?1:-1;
}

static int resdop_signal_ext_impl(
    const obsd_t *obs, const nav_t *nav, const prcopt_t *opt,
    const double receiver_ecef_m[3],
    const double receiver_velocity_ecef_mps[3],
    double receiver_clock_drift_mps, int required_message_mask,
    double wavelength_m, int ignore_broadcast_health, double *residual_mps,
    double azel_rad[2])
{
    double rs[6]={0},dts[2]={0},vare=0.0,e[3],pos[3],azel[2];
    double relative_velocity[3],rate,r;
    int svh=0,j,stat;

    if (!obs||!nav||!opt||!receiver_ecef_m||!receiver_velocity_ecef_mps||
        !residual_mps||!isfinite(wavelength_m)||wavelength_m<=0.0||
        !isfinite(obs->D[0])||obs->P[0]<=0.0) return -1;

    stat=rtklib_signal_state_ext(obs->time,obs->P[0],obs->sat,obs->code[0],
                                 required_message_mask,nav,rs,dts,&vare,&svh,
                                 NULL);
    if (stat<=0) return stat;
    if ((r=geodist(rs,receiver_ecef_m,e))<=0.0) return 0;
    (void)r;
    ecef2pos(receiver_ecef_m,pos);
    if (satazel(pos,e,azel)<opt->elmin||
        satexclude(obs->sat,ignore_broadcast_health?0:svh,opt)) return 0;
    if (norm(rs+3,3)<=0.0) return 0;

    for (j=0;j<3;j++) {
        relative_velocity[j]=rs[j+3]-receiver_velocity_ecef_mps[j];
    }
    rate=dot(relative_velocity,e,3)+OMGE/CLIGHT*(
         rs[4]*receiver_ecef_m[0]+rs[1]*receiver_velocity_ecef_mps[0]-
         rs[3]*receiver_ecef_m[1]-rs[0]*receiver_velocity_ecef_mps[1]);

    *residual_mps=-wavelength_m*obs->D[0]-
                  (rate+receiver_clock_drift_mps-CLIGHT*dts[1]);
    if (azel_rad) {
        azel_rad[0]=azel[0];
        azel_rad[1]=azel[1];
    }
    return isfinite(*residual_mps)?1:-1;
}

int rtklib_resdop_signal_ext(const obsd_t *obs, const nav_t *nav,
                             const prcopt_t *opt,
                             const double receiver_ecef_m[3],
                             const double receiver_velocity_ecef_mps[3],
                             double receiver_clock_drift_mps,
                             int required_message_mask, double wavelength_m,
                             double *residual_mps, double azel_rad[2])
{
    return resdop_signal_ext_impl(obs,nav,opt,receiver_ecef_m,
                                  receiver_velocity_ecef_mps,
                                  receiver_clock_drift_mps,
                                  required_message_mask,wavelength_m,0,
                                  residual_mps,azel_rad);
}

int rtklib_resdop_signal_diagnostic_ext(
    const obsd_t *obs, const nav_t *nav, const prcopt_t *opt,
    const double receiver_ecef_m[3],
    const double receiver_velocity_ecef_mps[3],
    double receiver_clock_drift_mps, int required_message_mask,
    double wavelength_m, double *residual_mps, double azel_rad[2])
{
    return resdop_signal_ext_impl(obs,nav,opt,receiver_ecef_m,
                                  receiver_velocity_ecef_mps,
                                  receiver_clock_drift_mps,
                                  required_message_mask,wavelength_m,1,
                                  residual_mps,azel_rad);
}

int rtklib_resdop_state_ext(
    const obsd_t *obs, const prcopt_t *opt,
    const double receiver_ecef_m[3],
    const double receiver_velocity_ecef_mps[3],
    double receiver_clock_drift_mps, const double satellite_state_ecef[6],
    const double satellite_clock[2], int satellite_health,
    double wavelength_m, double *residual_mps, double azel_rad[2])
{
    double e[3],pos[3],azel[2],relative_velocity[3],rate,r;
    int i;

    if (!obs||!opt||!receiver_ecef_m||!receiver_velocity_ecef_mps||
        !satellite_state_ecef||!satellite_clock||!residual_mps||obs->sat<=0||
        obs->P[0]<=0.0||!isfinite(obs->D[0])||
        !isfinite(receiver_clock_drift_mps)||!isfinite(wavelength_m)||
        wavelength_m<=0.0||!isfinite(satellite_clock[1])) return -1;
    for (i=0;i<6;i++) {
        if (!isfinite(satellite_state_ecef[i])) return -1;
    }
    for (i=0;i<3;i++) {
        if (!isfinite(receiver_ecef_m[i])||
            !isfinite(receiver_velocity_ecef_mps[i])) return -1;
    }

    if ((r=geodist(satellite_state_ecef,receiver_ecef_m,e))<=0.0) return 0;
    (void)r;
    ecef2pos(receiver_ecef_m,pos);
    if (satazel(pos,e,azel)<opt->elmin||
        satexclude(obs->sat,satellite_health,opt)) return 0;
    if (norm(satellite_state_ecef+3,3)<=0.0) return 0;

    for (i=0;i<3;i++) {
        relative_velocity[i]=satellite_state_ecef[i+3]-
                             receiver_velocity_ecef_mps[i];
    }
    rate=dot(relative_velocity,e,3)+OMGE/CLIGHT*(
         satellite_state_ecef[4]*receiver_ecef_m[0]+
         satellite_state_ecef[1]*receiver_velocity_ecef_mps[0]-
         satellite_state_ecef[3]*receiver_ecef_m[1]-
         satellite_state_ecef[0]*receiver_velocity_ecef_mps[1]);

    *residual_mps=-wavelength_m*obs->D[0]-
                  (rate+receiver_clock_drift_mps-CLIGHT*satellite_clock[1]);
    if (azel_rad) {
        azel_rad[0]=azel[0];
        azel_rad[1]=azel[1];
    }
    return isfinite(*residual_mps)?1:-1;
}
