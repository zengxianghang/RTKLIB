#include "rtklib.h"
#include "rtklib_residual_ext.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int expect_close(const char *name, double actual, double expected, double tol)
{
    if (fabs(actual-expected)<=tol) return 1;
    fprintf(stderr,"%s: actual=%.15f expected=%.15f tol=%.3e\n",
            name,actual,expected,tol);
    return 0;
}

int main(void)
{
    nav_t nav={0};
    obsd_t obs={0};
    prcopt_t opt=prcopt_default;
    double receiver_pos[3]={20.0*D2R,120.0*D2R,100.0};
    double receiver_ecef[3],receiver_velocity[3]={10.0,-4.0,1.0};
    double satellite_state[6],satellite_clock[2]={3E-6,-2E-10};
    double relative_velocity[3],los[3],range,rate,residual,azel[2];
    double receiver_clock_bias_m=15.0,receiver_system_bias_m=-2.0;
    double receiver_clock_drift_mps=0.25,code_bias_m=-0.4;
    double wavelength=CLIGHT/FREQ6;
    int sat=satno(SYS_GAL,2),i,stat;

    if (!sat) return 1;
    pos2ecef(receiver_pos,receiver_ecef);
    for (i=0;i<3;i++) satellite_state[i]=receiver_ecef[i]*4.2;
    satellite_state[3]=-500.0;
    satellite_state[4]=1200.0;
    satellite_state[5]=2200.0;

    obs.time=gpst2time(2400,1000.0);
    obs.sat=(unsigned char)sat;
    obs.code[0]=CODE_L6C;
    opt.navsys=SYS_GAL;
    opt.ionoopt=IONOOPT_OFF;
    opt.tropopt=TROPOPT_OFF;
    opt.elmin=0.0;

    range=geodist(satellite_state,receiver_ecef,los);
    if (!(range>0.0)) return 1;
    obs.P[0]=range+receiver_clock_bias_m-CLIGHT*satellite_clock[0]+
             receiver_system_bias_m+code_bias_m;

    stat=rtklib_rescode_state_ext(
        &obs,&nav,&opt,receiver_ecef,receiver_clock_bias_m,
        receiver_system_bias_m,satellite_state,satellite_clock,0,
        code_bias_m,wavelength,&residual,azel);
    if (stat!=1||!expect_close("external-state code residual",residual,0.0,1E-8)) return 1;

    /* The explicit code bias follows the same P-code_bias convention. */
    obs.P[0]+=1.25;
    stat=rtklib_rescode_state_ext(
        &obs,&nav,&opt,receiver_ecef,receiver_clock_bias_m,
        receiver_system_bias_m,satellite_state,satellite_clock,0,
        code_bias_m,wavelength,&residual,azel);
    if (stat!=1||!expect_close("external-state code bias sign",residual,1.25,1E-8)) return 1;
    obs.P[0]-=1.25;

    for (i=0;i<3;i++) {
        relative_velocity[i]=satellite_state[i+3]-receiver_velocity[i];
    }
    rate=dot(relative_velocity,los,3)+OMGE/CLIGHT*(
         satellite_state[4]*receiver_ecef[0]+
         satellite_state[1]*receiver_velocity[0]-
         satellite_state[3]*receiver_ecef[1]-
         satellite_state[0]*receiver_velocity[1]);
    obs.D[0]=(float)(-(rate+receiver_clock_drift_mps-
                       CLIGHT*satellite_clock[1])/wavelength);

    stat=rtklib_resdop_state_ext(
        &obs,&opt,receiver_ecef,receiver_velocity,receiver_clock_drift_mps,
        satellite_state,satellite_clock,0,wavelength,&residual,azel);
    if (stat!=1||!expect_close("external-state Doppler residual",residual,0.0,5E-4)) return 1;

    opt.exsats[sat-1]=1;
    stat=rtklib_rescode_state_ext(
        &obs,&nav,&opt,receiver_ecef,receiver_clock_bias_m,
        receiver_system_bias_m,satellite_state,satellite_clock,0,
        code_bias_m,wavelength,&residual,azel);
    if (stat!=0) {
        fprintf(stderr,"external-state code residual ignored explicit satellite exclusion\n");
        return 1;
    }
    opt.exsats[sat-1]=0;

    stat=rtklib_rescode_state_ext(
        &obs,&nav,&opt,receiver_ecef,receiver_clock_bias_m,
        receiver_system_bias_m,satellite_state,satellite_clock,1,
        code_bias_m,wavelength,&residual,azel);
    if (stat!=0) {
        fprintf(stderr,"external-state code residual ignored supplied satellite health\n");
        return 1;
    }

    puts("external_state_residual: PASS");
    return 0;
}
