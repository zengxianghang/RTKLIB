#include "rtklib.h"
#include "rtklib_signal_bias_ext.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int close_to(const char *name, double actual, double expected)
{
    if (fabs(actual-expected)<=1E-9) return 1;
    fprintf(stderr,"%s: actual=%.15f expected=%.15f\n",name,actual,expected);
    return 0;
}

int main(void)
{
    nav_t nav={0};
    geph_t geph[2],selected_geph;
    eph_t selected_eph;
    rtklib_signal_bias_info_ext_t info;
    gtime_t t=gpst2time(2300,100000.0);
    double bias=0.0;
    int sat=satno(SYS_GLO,1),stat,ok=1;

    if (!sat) return 1;
    memset(geph,0,sizeof(geph));
    geph[0].sat=sat;
    geph[0].toe=geph[0].tof=t;
    geph[0].hdr.msg_type=NAV_FDMA;
    geph[0].iode=10;
    geph[0].dtaun=1E-8;

    geph[1].sat=sat;
    geph[1].toe=geph[1].tof=t;
    geph[1].hdr.msg_type=NAV_L3OC;
    geph[1].iode=11;
    geph[1].isc_l3ocp=2.5E-8;

    nav.geph=geph;
    nav.ng=nav.ngmax=2;

    stat=rtklib_signal_code_bias_ext(t,sat,CODE_L3Q,NAV_L3OC,&nav,&bias,&info);
    ok&=stat==1&&info.message_type==NAV_L3OC&&info.iode==11&&
        close_to("G3 L3OCp ISC sign",bias,-CLIGHT*2.5E-8);

    stat=rtklib_signal_code_bias_ext(t,sat,CODE_L2C,NAV_FDMA,&nav,&bias,&info);
    ok&=stat==1&&info.message_type==NAV_FDMA&&info.iode==10&&
        close_to("G2 FDMA dtaun",bias,CLIGHT*1E-8);

    stat=rtklib_signal_ephemeris_ext(t,sat,CODE_L3Q,NAV_FDMA,&nav,
                                     &selected_eph,&selected_geph,&info);
    if (stat!=0) {
        fprintf(stderr,"G3 incorrectly accepted FDMA ephemeris\n");
        ok=0;
    }
    stat=rtklib_signal_ephemeris_ext(t,sat,CODE_L1C,NAV_L3OC,&nav,
                                     &selected_eph,&selected_geph,&info);
    if (stat!=0) {
        fprintf(stderr,"G1 incorrectly accepted L3OC ephemeris\n");
        ok=0;
    }
    if (!ok) return 1;
    puts("glo_signal_bias_ext: PASS");
    return 0;
}
