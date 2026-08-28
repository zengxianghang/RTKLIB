#include "rtklib.h"
#include "rtklib_residual_ext.h"
#include "rtklib_signal_bias_ext.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static void init_cnav_eph(eph_t *eph, int sat, gtime_t toe, int iode,
                          double m0, double tgd0, double isc_l2c)
{
    memset(eph,0,sizeof(*eph));
    eph->sat=sat;
    eph->toe=eph->toc=eph->ttr=toe;
    eph->hdr.msg_type=NAV_CNAV;
    eph->iode=iode;
    eph->A=26560E3;
    eph->e=0.01;
    eph->i0=0.94;
    eph->OMG0=1.0;
    eph->omg=0.5;
    eph->M0=m0;
    eph->tgd[0]=tgd0;
    eph->isc[1]=isc_l2c;
}

int main(void)
{
    nav_t nav={0};
    eph_t eph[2],selected_eph;
    geph_t selected_geph;
    obsd_t obs={0};
    prcopt_t opt=prcopt_default;
    rtklib_signal_bias_info_ext_t info;
    gtime_t receive_time=gpst2time(2300,100000.020);
    gtime_t old_toe=gpst2time(2300,99999.900);
    gtime_t new_toe=gpst2time(2300,100000.100);
    gtime_t signal_time;
    double rr[3],pos[3]={20.0*D2R,120.0*D2R,100.0};
    double rs[6],dts[2],var,bias,range,e[3],residual,azel[2];
    double wavelength=CLIGHT/FREQ2;
    int sat=satno(SYS_GPS,3),svh=0,stat,i;

    if (!sat) return 1;

    /* Receive time is after the equal-age handover midpoint, while an
     * approximately 80 ms signal flight still places transmit time before it.
     * The records deliberately carry different orbit and L2C bias values so a
     * receive-time selector cannot accidentally pass the residual check. */
    init_cnav_eph(eph+0,sat,old_toe,21,0.10,1E-8,2E-9);
    init_cnav_eph(eph+1,sat,new_toe,22,0.11,4E-8,1E-9);
    nav.eph=eph;
    nav.n=nav.nmax=2;

    obs.time=receive_time;
    obs.sat=(unsigned char)sat;
    obs.code[0]=CODE_L2S;
    obs.P[0]=2.4E7;
    obs.SNR[0]=200;

    memset(&selected_eph,0,sizeof(selected_eph));
    memset(&selected_geph,0,sizeof(selected_geph));
    stat=rtklib_signal_ephemeris_ext(receive_time,sat,CODE_L2S,NAV_CNAV,
                                     &nav,&selected_eph,&selected_geph,&info);
    if (stat!=1||info.iode!=22) {
        fprintf(stderr,"test setup did not select new record at receive epoch: stat=%d iode=%d\n",
                stat,info.iode);
        return 1;
    }

    signal_time=timeadd(receive_time,-obs.P[0]/CLIGHT);
    memset(&selected_eph,0,sizeof(selected_eph));
    memset(&selected_geph,0,sizeof(selected_geph));
    stat=rtklib_signal_ephemeris_ext(signal_time,sat,CODE_L2S,NAV_CNAV,
                                     &nav,&selected_eph,&selected_geph,&info);
    if (stat!=1||info.iode!=21) {
        fprintf(stderr,"test setup did not select old record at signal epoch: stat=%d iode=%d\n",
                stat,info.iode);
        return 1;
    }

    stat=rtklib_signal_state_ext(receive_time,obs.P[0],sat,CODE_L2S,NAV_CNAV,
                                 &nav,rs,dts,&var,&svh,&info);
    if (stat!=1||info.iode!=21) {
        fprintf(stderr,"signal state selected receive-epoch record: stat=%d iode=%d\n",
                stat,info.iode);
        return 1;
    }

    pos2ecef(pos,rr);
    opt.ionoopt=IONOOPT_OFF;
    opt.tropopt=TROPOPT_OFF;
    opt.elmin=-PI/2.0;

    /* Build a self-consistent observation from the pre-handover record. */
    for (i=0;i<5;i++) {
        stat=rtklib_signal_state_ext(receive_time,obs.P[0],sat,CODE_L2S,
                                     NAV_CNAV,&nav,rs,dts,&var,&svh,&info);
        if (stat!=1||info.iode!=21||svh!=0) return 1;
        signal_time=timeadd(receive_time,-obs.P[0]/CLIGHT);
        stat=rtklib_signal_code_bias_ext(signal_time,sat,CODE_L2S,NAV_CNAV,
                                         &nav,&bias,&info);
        if (stat!=1||info.iode!=21) return 1;
        range=geodist(rs,rr,e);
        if (!(range>0.0)) return 1;
        obs.P[0]=range-CLIGHT*dts[0]+bias;
    }

    stat=rtklib_rescode_signal_ext(&obs,&nav,&opt,rr,0.0,0.0,NAV_CNAV,
                                   wavelength,&residual,azel,&info);
    if (stat!=1||info.iode!=21||fabs(residual)>=1E-4) {
        fprintf(stderr,"handover residual mismatch: stat=%d iode=%d residual=%.9f\n",
                stat,info.iode,residual);
        return 1;
    }

    puts("transmit_epoch_handover: PASS");
    return 0;
}
