#include "rtklib.h"
#include "rtklib_obs_ext.h"
#include "rtklib_signal_bias_ext.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define EPS_M 1E-9

static int expect_close(const char *name, double actual, double expected)
{
    if (fabs(actual-expected)<=EPS_M) return 1;
    fprintf(stderr,"%s: actual=%.15f expected=%.15f\n",name,actual,expected);
    return 0;
}

static void init_eph(eph_t *eph, int sat, gtime_t toe, int msg_type,
                     int iode, double tgd0, double tgd1)
{
    memset(eph,0,sizeof(*eph));
    eph->sat=sat;
    eph->toe=toe;
    eph->toc=toe;
    eph->ttr=toe;
    eph->iode=iode;
    eph->hdr.msg_type=msg_type;
    eph->tgd[0]=tgd0;
    eph->tgd[1]=tgd1;
}

int main(void)
{
    nav_t nav={0};
    eph_t eph[7],selected_eph;
    geph_t selected_geph;
    rtklib_signal_bias_info_ext_t info;
    gtime_t t0=gpst2time(2300,100000.0);
    int c01=satno(SYS_CMP,1),c02=satno(SYS_CMP,2);
    double bias=0.0;
    int ok=1,stat;

    if (!c01||!c02) {
        fprintf(stderr,"BeiDou satellite numbering unavailable\n");
        return 1;
    }

    init_eph(eph+0,c01,t0,NAV_D1D2,10,1E-8,2E-8);
    init_eph(eph+1,c01,timeadd(t0,3600.0),NAV_D1D2,11,3E-8,4E-8);
    /* Deliberately closer than the second legacy record: B1I must ignore it. */
    init_eph(eph+2,c01,timeadd(t0,3590.0),NAV_CNV1,12,99E-8,98E-8);

    init_eph(eph+3,c02,t0,NAV_CNV1,20,5E-8,6E-8);
    eph[3].isc[0]=1E-8; /* ISC_B1Cd, CNV1 only */
    init_eph(eph+4,c02,t0,NAV_CNV2,21,7E-8,8E-8);
    eph[4].isc[0]=2E-8; /* ISC_B2ad, not B1Cd */
    init_eph(eph+5,c02,t0,NAV_CNV3,22,9E-8,0.0);
    init_eph(eph+6,c02,timeadd(t0,1800.0),NAV_CNV1,23,10E-8,11E-8);

    nav.eph=eph;
    nav.n=nav.nmax=(int)(sizeof(eph)/sizeof(eph[0]));

    stat=rtklib_signal_code_bias_ext(t0,c01,CODE_L2I,NAV_D1D2,&nav,&bias,&info);
    ok&=stat==1&&info.iode==10&&expect_close("B1I TGD1",bias,CLIGHT*1E-8);

    stat=rtklib_signal_code_bias_ext(t0,c01,CODE_L6I,NAV_D1D2,&nav,&bias,&info);
    ok&=stat==1&&expect_close("B3I clock reference",bias,0.0);

    stat=rtklib_signal_code_bias_ext(t0,c01,CODE_L7I,NAV_D1D2,&nav,&bias,&info);
    ok&=stat==1&&expect_close("B2I TGD2",bias,CLIGHT*2E-8);

    stat=rtklib_signal_code_bias_ext(timeadd(t0,3550.0),c01,CODE_L2I,0,&nav,&bias,&info);
    ok&=stat==1&&info.iode==11&&expect_close("B1I epoch selects legacy",bias,CLIGHT*3E-8);

    /* A legacy-family mask accepts D1/D2/D1D2 but must reject the closer CNAV record. */
    eph[0].hdr.msg_type=NAV_D1;
    stat=rtklib_signal_code_bias_ext(timeadd(t0,10.0),c01,CODE_L2I,
        NAV_D1|NAV_D2|NAV_D1D2,&nav,&bias,&info);
    ok&=stat==1&&info.iode==10&&
        expect_close("B1I legacy mask",bias,CLIGHT*1E-8);
    eph[0].hdr.msg_type=NAV_D1D2;

    stat=rtklib_signal_code_bias_ext(t0,c02,CODE_L1P,NAV_CNV1,&nav,&bias,&info);
    ok&=stat==1&&info.message_type==NAV_CNV1&&
        expect_close("B1C CNV1",bias,CLIGHT*5E-8);

    stat=rtklib_signal_code_bias_ext(t0,c02,CODE_L1P,NAV_CNV2,&nav,&bias,&info);
    ok&=stat==1&&info.message_type==NAV_CNV2&&
        expect_close("B1C CNV2",bias,CLIGHT*7E-8);

    stat=rtklib_signal_code_bias_ext(t0,c02,CODE_L1D,NAV_CNV1,&nav,&bias,&info);
    ok&=stat==1&&info.message_type==NAV_CNV1&&
        expect_close("B1C data CNV1",bias,CLIGHT*(5E-8+1E-8));

    /* CNV2 reuses isc[0] for ISC_B2ad, so it must not be guessed as
     * ISC_B1Cd.  The combined 1X observable also has no scalar broadcast
     * bias field in this adapter. */
    stat=rtklib_signal_code_bias_ext(t0,c02,CODE_L1D,NAV_CNV2,&nav,&bias,&info);
    ok&=stat==0;
    stat=rtklib_signal_code_bias_ext(t0,c02,CODE_L1X,NAV_CNV1|NAV_CNV2,
                                      &nav,&bias,&info);
    ok&=stat==0;

    stat=rtklib_signal_code_bias_ext(t0,c02,CODE_L5P,NAV_CNV1,&nav,&bias,&info);
    ok&=stat==1&&expect_close("B2a CNV1",bias,CLIGHT*6E-8);

    stat=rtklib_signal_code_bias_ext(t0,c02,CODE_L5P,NAV_CNV2,&nav,&bias,&info);
    ok&=stat==1&&expect_close("B2a CNV2",bias,CLIGHT*8E-8);

    stat=rtklib_signal_code_bias_ext(t0,c02,CODE_L7D,NAV_CNV3,&nav,&bias,&info);
    ok&=stat==1&&expect_close("B2b CNV3",bias,CLIGHT*9E-8);

    /* A forced message family must still be compatible with the signal code. */
    stat=rtklib_signal_ephemeris_ext(t0,c02,CODE_L1P,NAV_CNV3,&nav,
                                     &selected_eph,&selected_geph,&info);
    if (stat!=0) {
        fprintf(stderr,"B1C incorrectly accepted CNV3 ephemeris\n");
        ok=0;
    }

    stat=rtklib_signal_ephemeris_ext(t0,c02,CODE_L1D,NAV_CNV2,&nav,
                                     &selected_eph,&selected_geph,&info);
    if (stat!=0) {
        fprintf(stderr,"B1C data incorrectly accepted CNV2 ephemeris\n");
        ok=0;
    }

    stat=rtklib_signal_code_bias_ext(timeadd(t0,1700.0),c02,CODE_L1P,NAV_CNV1,&nav,&bias,&info);
    ok&=stat==1&&info.iode==23&&expect_close("B1C epoch selection",bias,CLIGHT*10E-8);

    if (!ok) return 1;
    puts("signal_bias_ext: PASS");
    return 0;
}
