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
                     double tgd0, double tgd1)
{
    memset(eph,0,sizeof(*eph));
    eph->sat=sat;
    eph->toe=eph->toc=eph->ttr=toe;
    eph->hdr.msg_type=msg_type;
    eph->iode=1;
    eph->A=26560E3;
    eph->e=0.01;
    eph->i0=0.94;
    eph->OMG0=1.0;
    eph->omg=0.5;
    eph->M0=0.1;
    eph->tgd[0]=tgd0;
    eph->tgd[1]=tgd1;
}

int main(void)
{
    nav_t nav={0};
    eph_t eph[1];
    obsd_t obs={0};
    prcopt_t opt=prcopt_default;
    double rr[3],pos[3]={20.0*D2R,120.0*D2R,100.0};
    double bias=0.0,wavelength=CLIGHT/FREQ1_CMP;
    rtklib_signal_bias_info_ext_t info;
    gtime_t t=gpst2time(2300,100000.0);
    int sat=satno(SYS_CMP,1),stat;

    if (!sat) return 1;
    init_eph(eph,sat,t,NAV_D1D2,1E-8,2E-8);
    nav.eph=eph; nav.n=nav.nmax=1;
    uniqnav(&nav);
    pos2ecef(pos,rr);

    obs.time=t;
    obs.sat=(unsigned char)sat;
    obs.code[0]=CODE_L2I;
    obs.SNR[0]=200;

    stat=rtklib_signal_code_bias_ext(t,sat,CODE_L2I,NAV_D1D2,&nav,&bias,&info);
    if (stat!=1||!expect_close("B1I bias",bias,CLIGHT*1E-8,1E-9)) return 1;

    /* This test intentionally validates the public all-signal residual API only
       for argument/bias plumbing. Real satellite-state zero residuals are
       covered by the simulator's checked-in NAV fixtures and WHU validation. */
    opt.ionoopt=IONOOPT_OFF;
    opt.tropopt=TROPOPT_OFF;
    opt.elmin=-PI/2.0;
    obs.P[0]=1.0; /* nonzero input required; geometry may be unavailable */
    {
        double residual=0.0;
        stat=rtklib_rescode_signal_ext(&obs,&nav,&opt,rr,0.0,0.0,
                                       NAV_D1D2,wavelength,&residual,NULL,&info);
        if (stat<0) {
            fprintf(stderr,"rescode extension rejected valid signal arguments\n");
            return 1;
        }
    }
    puts("residual_ext: PASS");
    return 0;
}
