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
    geph_t geph={0};
    obsd_t obs={0};
    prcopt_t opt=prcopt_default;
    rtklib_signal_bias_info_ext_t info;
    gtime_t t=gpst2time(2300,100000.0);
    double rr[3],pos[3]={20.0*D2R,120.0*D2R,100.0};
    double rs[6],dts[2],var,bias,residual,azel[2],e[3],range;
    double relative_velocity[3],rate,wavelength=CLIGHT/FREQ3_GLO;
    double zero_velocity[3]={0};
    double dt=100.0,expected_clk;
    int sat=satno(SYS_GLO,1),svh=0,stat,i;

    if (!sat) return 1;
    pos2ecef(pos,rr);

    geph.sat=sat;
    geph.toe=geph.tof=t;
    geph.hdr.msg_type=NAV_L3OC;
    geph.iode=17;
    geph.taun=2E-5;
    geph.gamn=1E-10;
    geph.beta=3E-13;
    geph.isc_l3ocp=2.5E-8;
    for (i=0;i<3;i++) geph.pos[i]=rr[i]*4.2;
    geph.vel[0]=0.0;
    geph.vel[1]=2500.0;
    geph.vel[2]=1000.0;
    nav.geph=&geph;
    nav.ng=nav.ngmax=1;

    expected_clk=-geph.taun+geph.gamn*dt+geph.beta*dt*dt;
    if (!expect_close("G3 beta clock",geph2clk(timeadd(t,dt),&geph),
                      expected_clk,1E-14)) return 1;

    stat=rtklib_signal_code_bias_ext(t,sat,CODE_L3Q,NAV_L3OC,&nav,&bias,&info);
    if (stat!=1||info.message_type!=NAV_L3OC||
        !expect_close("G3 pilot code bias",bias,-CLIGHT*geph.isc_l3ocp,1E-9)) return 1;

    opt.ionoopt=IONOOPT_OFF;
    opt.tropopt=TROPOPT_OFF;
    opt.navsys=SYS_GLO;
    opt.elmin=-PI/2.0;
    obs.time=t;
    obs.sat=(unsigned char)sat;
    obs.code[0]=CODE_L3Q;
    obs.SNR[0]=200;
    obs.P[0]=2.4E7;

    for (i=0;i<6;i++) {
        stat=rtklib_signal_state_ext(t,obs.P[0],sat,CODE_L3Q,NAV_L3OC,
                                     &nav,rs,dts,&var,&svh,&info);
        if (stat!=1) return 1;
        range=geodist(rs,rr,e);
        if (!(range>0.0)) return 1;
        obs.P[0]=range-CLIGHT*dts[0]+bias;
    }

    stat=rtklib_rescode_signal_ext(&obs,&nav,&opt,rr,0.0,0.0,NAV_L3OC,
                                   wavelength,&residual,azel,&info);
    if (stat!=1||info.message_type!=NAV_L3OC||
        !expect_close("G3 truth-state code residual",residual,0.0,1E-4)) return 1;

    stat=rtklib_signal_state_ext(t,obs.P[0],sat,CODE_L3Q,NAV_L3OC,
                                 &nav,rs,dts,&var,&svh,&info);
    if (stat!=1) return 1;
    range=geodist(rs,rr,e);
    if (!(range>0.0)) return 1;
    for (i=0;i<3;i++) relative_velocity[i]=rs[i+3];
    rate=dot(relative_velocity,e,3)+OMGE/CLIGHT*(
         rs[4]*rr[0]-rs[3]*rr[1]);
    obs.D[0]=(float)(-(rate-CLIGHT*dts[1])/wavelength);

    stat=rtklib_resdop_signal_ext(&obs,&nav,&opt,rr,zero_velocity,0.0,
                                  NAV_L3OC,wavelength,&residual,azel);
    if (stat!=1||!expect_close("G3 truth-state Doppler residual",residual,0.0,5E-4)) return 1;

    puts("glo_l3oc_residual: PASS");
    return 0;
}
