#include "rtklib_residual_ext.h"

#include <math.h>

#define SQR(x) ((x)*(x))

int rtklib_rescode_signal_ext(const obsd_t *obs, const nav_t *nav,
                              const prcopt_t *opt,
                              const double receiver_ecef_m[3],
                              double receiver_clock_bias_m,
                              double receiver_system_bias_m,
                              int required_message_type, double wavelength_m,
                              double *residual_m, double azel_rad[2],
                              rtklib_signal_bias_info_ext_t *bias_info)
{
    double rs[6]={0},dts[2]={0},vare[1]={0},e[3],pos[3],azel[2];
    double r,code_bias,dion=0.0,vion=0.0,dtrp=0.0,vtrp=0.0,p;
    int svh[1]={0},stat;

    if (!obs||!nav||!opt||!receiver_ecef_m||!residual_m||
        !isfinite(wavelength_m)||wavelength_m<=0.0||obs->P[0]<=0.0) return -1;

    satposs(obs->time,obs,1,nav,opt->sateph,rs,dts,vare,svh);
    if ((r=geodist(rs,receiver_ecef_m,e))<=0.0) return 0;
    ecef2pos(receiver_ecef_m,pos);
    if (satazel(pos,e,azel)<opt->elmin||satexclude(obs->sat,svh[0],opt)) return 0;

    stat=rtklib_signal_code_bias_ext(obs->time,obs->sat,obs->code[0],
                                     required_message_type,nav,&code_bias,
                                     bias_info);
    if (stat<=0) return stat;

    if (!ionocorr(obs->time,nav,obs->sat,pos,azel,opt->ionoopt,
                  &dion,&vion)) return 0;
    dion*=SQR(wavelength_m/lam_carr[0]);
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

int rtklib_resdop_signal_ext(const obsd_t *obs, const nav_t *nav,
                             const prcopt_t *opt,
                             const double receiver_ecef_m[3],
                             const double receiver_velocity_ecef_mps[3],
                             double receiver_clock_drift_mps,
                             double wavelength_m,
                             double *residual_mps, double azel_rad[2])
{
    double rs[6]={0},dts[2]={0},vare[1]={0},e[3],pos[3],azel[2];
    double relative_velocity[3],rate,r;
    int svh[1]={0},j;

    if (!obs||!nav||!opt||!receiver_ecef_m||!receiver_velocity_ecef_mps||
        !residual_mps||!isfinite(wavelength_m)||wavelength_m<=0.0||
        !isfinite(obs->D[0])||obs->P[0]<=0.0) return -1;

    satposs(obs->time,obs,1,nav,opt->sateph,rs,dts,vare,svh);
    if ((r=geodist(rs,receiver_ecef_m,e))<=0.0) return 0;
    (void)r;
    ecef2pos(receiver_ecef_m,pos);
    if (satazel(pos,e,azel)<opt->elmin||satexclude(obs->sat,svh[0],opt)) return 0;
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
