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

static void init_eph(eph_t *eph, int sat, gtime_t toe, int msg_type,
                     int iode, double m0, double tgd0)
{
    memset(eph,0,sizeof(*eph));
    eph->sat=sat;
    eph->toe=eph->toc=eph->ttr=toe;
    eph->hdr.msg_type=msg_type;
    eph->iode=iode;
    eph->A=26560E3;
    eph->e=0.01;
    eph->i0=0.94;
    eph->OMG0=1.0;
    eph->omg=0.5;
    eph->M0=m0;
    eph->tgd[0]=tgd0;
}

int main(void)
{
    nav_t nav={0};
    eph_t eph[2];
    obsd_t obs={0};
    prcopt_t opt=prcopt_default;
    rtklib_signal_bias_info_ext_t info;
    gtime_t t=gpst2time(2300,100000.0);
    double rr[3],pos[3]={20.0*D2R,120.0*D2R,100.0};
    double rs[6],dts[2],var,bias=0.0,residual=0.0,azel[2];
    double e[3],range,relative_velocity[3],rate,wavelength=CLIGHT/FREQ2;
    double zero_velocity[3]={0};
    int sat=satno(SYS_GPS,3),svh=0,stat,i;

    if (!sat) return 1;
    init_eph(eph+0,sat,t,NAV_LNAV,10,0.1,1E-8);
    init_eph(eph+1,sat,t,NAV_CNAV,11,1.1,3E-8);
    eph[1].isc[1]=1E-8;
    nav.eph=eph; nav.n=nav.nmax=2;
    pos2ecef(pos,rr);

    obs.time=t;
    obs.sat=(unsigned char)sat;
    obs.code[0]=CODE_L2S;
    obs.SNR[0]=200;
    obs.P[0]=2.4E7;

    stat=rtklib_signal_state_ext(t,obs.P[0],sat,CODE_L2S,NAV_CNAV,
                                 &nav,rs,dts,&var,&svh,&info);
    if (stat!=1||info.message_type!=NAV_CNAV||info.iode!=11) {
        fprintf(stderr,"state selector did not preserve CNAV family\n");
        return 1;
    }

    stat=rtklib_signal_code_bias_ext(t,sat,CODE_L2S,NAV_CNAV,&nav,&bias,&info);
    if (stat!=1||info.iode!=11||
        !expect_close("L2C CNAV bias",bias,CLIGHT*2E-8,1E-9)) return 1;

    opt.ionoopt=IONOOPT_OFF;
    opt.tropopt=TROPOPT_OFF;
    opt.elmin=-PI/2.0;

    /* Iterate only the raw transmit-time pseudorange dependency. */
    for (i=0;i<5;i++) {
        stat=rtklib_signal_state_ext(t,obs.P[0],sat,CODE_L2S,NAV_CNAV,
                                     &nav,rs,dts,&var,&svh,&info);
        if (stat!=1) return 1;
        range=geodist(rs,rr,e);
        if (!(range>0.0)) return 1;
        obs.P[0]=range-CLIGHT*dts[0]+bias;
    }

    stat=rtklib_rescode_signal_ext(&obs,&nav,&opt,rr,0.0,0.0,
                                   NAV_CNAV,wavelength,&residual,azel,&info);
    if (stat!=1||info.message_type!=NAV_CNAV||
        !expect_close("truth-state code residual",residual,0.0,1E-4)) return 1;

    stat=rtklib_signal_state_ext(t,obs.P[0],sat,CODE_L2S,NAV_CNAV,
                                 &nav,rs,dts,&var,&svh,&info);
    if (stat!=1) return 1;
    range=geodist(rs,rr,e);
    if (!(range>0.0)) return 1;
    for (i=0;i<3;i++) relative_velocity[i]=rs[i+3];
    rate=dot(relative_velocity,e,3)+OMGE/CLIGHT*(
         rs[4]*rr[0]-rs[3]*rr[1]);
    obs.D[0]=(float)(-(rate-CLIGHT*dts[1])/wavelength);

    stat=rtklib_resdop_signal_ext(&obs,&nav,&opt,rr,zero_velocity,0.0,
                                  NAV_CNAV,wavelength,&residual,azel);
    if (stat!=1||!expect_close("truth-state Doppler residual",residual,0.0,5E-4)) return 1;

    /* Doppler does not require the signal-specific code-bias NAV family. */
    obs.code[0]=CODE_L1L;
    obs.P[0]=2.4E7;
    wavelength=CLIGHT/FREQ1;
    for (i=0;i<5;i++) {
        stat=rtklib_signal_state_ext(t,obs.P[0],sat,CODE_L1L,0,
                                     &nav,rs,dts,&var,&svh,&info);
        if (stat!=1||info.message_type!=NAV_CNAV||info.iode!=11) {
            fprintf(stderr,"generic state did not mirror stock equal-age tie selection\n");
            return 1;
        }
        range=geodist(rs,rr,e);
        if (!(range>0.0)) return 1;
        obs.P[0]=range-CLIGHT*dts[0];
    }
    stat=rtklib_signal_state_ext(t,obs.P[0],sat,CODE_L1L,NAV_LNAV,
                                 &nav,rs,dts,&var,&svh,&info);
    if (stat!=0) {
        fprintf(stderr,"forced LNAV incorrectly accepted L1C observation code\n");
        return 1;
    }
    stat=rtklib_signal_state_ext(t,obs.P[0],sat,CODE_L1L,0,
                                 &nav,rs,dts,&var,&svh,&info);
    if (stat!=1) return 1;
    range=geodist(rs,rr,e);
    if (!(range>0.0)) return 1;
    for (i=0;i<3;i++) relative_velocity[i]=rs[i+3];
    rate=dot(relative_velocity,e,3)+OMGE/CLIGHT*(
         rs[4]*rr[0]-rs[3]*rr[1]);
    obs.D[0]=(float)(-(rate-CLIGHT*dts[1])/wavelength);
    stat=rtklib_resdop_signal_ext(&obs,&nav,&opt,rr,zero_velocity,0.0,
                                  0,wavelength,&residual,azel);
    if (stat!=1||info.message_type!=NAV_CNAV||info.iode!=11||
        !expect_close("generic-state L1C Doppler residual",residual,0.0,5E-4)) return 1;

    puts("residual_ext: PASS");
    return 0;
}
