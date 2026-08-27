#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def replace_once(rel, old, new):
    path = ROOT / rel
    text = path.read_text(encoding="utf-8")
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{rel}: expected one guarded match, found {count}")
    path.write_text(text.replace(old, new, 1), encoding="utf-8")


def write(rel, content):
    path = ROOT / rel
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content, encoding="utf-8")


# geph_t: preserve the legacy fields and add only the RINEX4 CDMA values that
# affect clock/code residuals plus the phase-centre/transmission metadata used
# by the parser regression.
replace_once(
    "src/rtklib.h",
    """    double taun,gamn;   /* SV clock bias (s)/relative freq bias */\n"
    "    double dtaun;       /* delay between L1 and L2 (s) */\n"
    "    nav_data_hdr_t hdr;\n""".replace('"\n    "', ''),
    """    double taun,gamn;   /* SV clock bias (s)/relative freq bias */
    double beta;        /* GLONASS CDMA half rate of relative freq bias (1/s) */
    double dtaun;       /* delay between L1 and L2 (s) */
    double tgd_l2ocp;   /* RINEX4 L1OC TGD_L2OCp (s) */
    double isc_l3ocp;   /* RINEX4 L3OC ISC_L3OCp = T_L3OCp-T_L3OCd (s) */
    int data_validity;  /* RINEX4 GLONASS CDMA data-validity flag */
    double pc[3];       /* GLONASS CDMA antenna phase-centre offsets (m) */
    double ttm;         /* RINEX4 raw transmission time in UTC week (s) */
    nav_data_hdr_t hdr;
""",
)

# GLONASS CDMA has a quadratic clock model. beta is zero for legacy FDMA
# records, so these changes preserve legacy behaviour.
replace_once(
    "src/ephemeris.c",
    """    for (i=0;i<2;i++) {
        t-=-geph->taun+geph->gamn*t;
    }
    return -geph->taun+geph->gamn*t;
""",
    """    for (i=0;i<2;i++) {
        t-=-geph->taun+geph->gamn*t+geph->beta*t*t;
    }
    return -geph->taun+geph->gamn*t+geph->beta*t*t;
""",
)
replace_once(
    "src/ephemeris.c",
    """    *dts=-geph->taun+geph->gamn*t;
""",
    """    *dts=-geph->taun+geph->gamn*t+geph->beta*t*t;
""",
)

# RINEX4 L1OC/L3OC is not the 19-field FDMA layout. Keep decode_geph() for
# RINEX2/3 and RINEX4 FDMA, and add a dedicated 35-field CDMA decoder.
rinex4_geph = r'''/* decode RINEX4 GLONASS CDMA ephemeris -----------------------------------*/
static int decode_rnx4_geph(int sat, gtime_t toc, const double *data,
                            const nav_data_hdr_t *hdr, geph_t *geph)
{
    geph_t geph0={0};
    gtime_t tof;
    double tow;
    int week;

    if (!hdr||satsys(sat,NULL)!=SYS_GLO||
        (hdr->msg_type!=NAV_L1OC&&hdr->msg_type!=NAV_L3OC)) {
        rtktrace(2,"glonass cdma ephemeris error: sat=%2d msg=%d\n",
                 sat,hdr?hdr->msg_type:0);
        return 0;
    }
    *geph=geph0;
    geph->sat=sat;

    /* RINEX4 CDMA Toc is UTC and is the clock reference epoch. Do not apply
       the legacy FDMA 15-minute rounding. t_tm is the last field (UTC week). */
    tow=time2gpst(toc,&week);
    geph->toe=utc2gpst(toc);
    tof=adjweek(gpst2time(week,data[34]),toc);
    geph->tof=utc2gpst(tof);
    geph->ttm=data[34];

    /* There is no legacy 7-bit tb/IODE field in the CDMA RINEX record. Keep a
       deterministic epoch tag for diagnostics/selector provenance. */
    geph->iode=(int)(fmod(tow+10800.0,86400.0)/90.0+0.5);

    geph->taun=-data[0];
    geph->gamn= data[1];
    geph->beta= data[2];

    geph->pos[0]=data[ 3]*1E3; geph->vel[0]=data[ 4]*1E3; geph->acc[0]=data[ 5]*1E3;
    geph->pos[1]=data[ 7]*1E3; geph->vel[1]=data[ 8]*1E3; geph->acc[1]=data[ 9]*1E3;
    geph->pos[2]=data[11]*1E3; geph->vel[2]=data[12]*1E3; geph->acc[2]=data[13]*1E3;

    geph->svh=(int)data[6];
    geph->data_validity=(int)data[10];
    geph->frq=0; /* CDMA: common carrier, no FDMA channel number */
    geph->flag=(int)data[16]; /* RINEX4 RT/RE source flags */
    geph->age=(int)data[17];  /* AODE, retained for diagnostics */
    geph->sva=(int)data[31];  /* orbit accuracy index */
    geph->pc[0]=data[28];
    geph->pc[1]=data[29];
    geph->pc[2]=data[30];

    if (hdr->msg_type==NAV_L1OC) geph->tgd_l2ocp=data[14];
    else                         geph->isc_l3ocp=data[14];
    return 1;
}
'''
replace_once(
    "src/rinex.c",
    "/* decode geo ephemeris ------------------------------------------------------*/\n",
    rinex4_geph + "/* decode geo ephemeris ------------------------------------------------------*/\n",
)
replace_once(
    "src/rinex.c",
    """    int max_data_cnt = 31;
    if((hdr->sys & (SYS_GPS | SYS_QZS)) && hdr->msg_type == NAV_CNAV) max_data_cnt = 35;
    else if((hdr->sys & (SYS_GPS | SYS_QZS)) && hdr->msg_type == NAV_CNV2) max_data_cnt = 39;
    else if(hdr->sys == SYS_CMP && (hdr->msg_type & ( NAV_CNV1 | NAV_CNV2))) max_data_cnt = 39;
    else if(hdr->sys == SYS_CMP && hdr->msg_type == NAV_CNV3) max_data_cnt = 35;
""",
    """    int max_data_cnt = 31;
    if(hdr->sys == SYS_GLO &&
       (hdr->msg_type == NAV_L1OC || hdr->msg_type == NAV_L3OC)) max_data_cnt = 35;
    else if(hdr->sys == SYS_GLO) max_data_cnt = 19;
    else if((hdr->sys & (SYS_GPS | SYS_QZS)) && hdr->msg_type == NAV_CNAV) max_data_cnt = 35;
    else if((hdr->sys & (SYS_GPS | SYS_QZS)) && hdr->msg_type == NAV_CNV2) max_data_cnt = 39;
    else if(hdr->sys == SYS_CMP && (hdr->msg_type & ( NAV_CNV1 | NAV_CNV2))) max_data_cnt = 39;
    else if(hdr->sys == SYS_CMP && hdr->msg_type == NAV_CNV3) max_data_cnt = 35;
""",
)
replace_once(
    "src/rinex.c",
    """            if (sys==SYS_GLO&&i>=19) {
                if (!(mask&sys)) return 0;
                *type=1;
                return decode_geph(ver,sat,toc,data,geph);
            }
""",
    """            if (sys==SYS_GLO&&i>=max_data_cnt) {
                if (!(mask&sys)) return 0;
                *type=1;
                if (hdr->msg_type==NAV_L1OC||hdr->msg_type==NAV_L3OC) {
                    return decode_rnx4_geph(sat,toc,data,hdr,geph);
                }
                return decode_geph(ver,sat,toc,data,geph);
            }
""",
)

# Make GLONASS selection family-aware for both state and code-bias APIs. 3Q is
# the pilot observable and must use L3OC; legacy 1C/2C remain FDMA-only.
replace_once(
    "src/rtkcmn.c",
    """static const geph_t *select_signal_geph(gtime_t time, int sat,
                                        const nav_t *nav)
{
    const geph_t *best=NULL;
    double best_age=0.0;
    int i;
    if (!nav) return NULL;
    for (i=0;i<nav->ng;i++) {
        double age;
        if (nav->geph[i].sat!=sat) continue;
        age=fabs(timediff(nav->geph[i].toe,time));
        if (age>MAXDTOE_GLO) continue;
        if (!best||age<best_age||(fabs(age-best_age)<1E-9&&
            timediff(nav->geph[i].tof,best->tof)>0.0)) {
            best=nav->geph+i;
            best_age=age;
        }
    }
    return best;
}
""",
    """static const geph_t *select_signal_geph(gtime_t time, int sat,
                                        unsigned char code,
                                        int required_message_mask,
                                        const nav_t *nav, int *message_type)
{
    const geph_t *best=NULL;
    double best_age=0.0;
    int i,type,compatible;
    if (!nav) return NULL;
    for (i=0;i<nav->ng;i++) {
        double age;
        if (nav->geph[i].sat!=sat) continue;
        type=nav->geph[i].hdr.msg_type?nav->geph[i].hdr.msg_type:NAV_FDMA;
        compatible=code==CODE_L3Q?type==NAV_L3OC:
                   (code==CODE_L1C||code==CODE_L2C)?type==NAV_FDMA:0;
        if (!compatible) continue;
        if (required_message_mask&&!(type&required_message_mask)) continue;
        age=fabs(timediff(nav->geph[i].toe,time));
        if (age>MAXDTOE_GLO) continue;
        if (!best||age<best_age||(fabs(age-best_age)<1E-9&&
            timediff(nav->geph[i].tof,best->tof)>0.0)) {
            best=nav->geph+i;
            best_age=age;
            if (message_type) *message_type=type;
        }
    }
    return best;
}
""",
)
replace_once(
    "src/rtkcmn.c",
    """    if (sys==SYS_GLO) {
        geph=select_signal_geph(time,sat,nav);
        if (!geph) return 0;
        if (code==CODE_L1C) bias=0.0;
        else if (code==CODE_L2C) bias=CLIGHT*geph->dtaun;
        else return 0;
        if (info) {
            info->system=sys;
            info->message_type=NAV_FDMA;
            info->iode=geph->iode;
            info->raw_code_bias_m=bias;
        }
        *raw_code_bias_m=bias;
        return 1;
    }
""",
    """    if (sys==SYS_GLO) {
        geph=select_signal_geph(time,sat,code,required_message_mask,nav,&type);
        if (!geph) return 0;
        if (code==CODE_L1C) bias=0.0;
        else if (code==CODE_L2C) bias=CLIGHT*geph->dtaun;
        else if (code==CODE_L3Q) bias=-CLIGHT*geph->isc_l3ocp;
        else return 0;
        if (info) {
            info->system=sys;
            info->message_type=type;
            info->iode=geph->iode;
            info->raw_code_bias_m=bias;
        }
        *raw_code_bias_m=bias;
        return 1;
    }
""",
)
replace_once(
    "src/rtkcmn.c",
    """    if (sys==SYS_GLO) {
        for (i=0;i<nav->ng;i++) {
            int candidate_type,compatible;
            if (nav->geph[i].sat!=sat) continue;
            candidate_type=nav->geph[i].hdr.msg_type?nav->geph[i].hdr.msg_type:NAV_FDMA;
            compatible=code==CODE_L3Q?candidate_type==NAV_L3OC:
                       (code==CODE_L1C||code==CODE_L2C)?candidate_type==NAV_FDMA:0;
            if (!compatible) continue;
            if (required_message_mask&&!(candidate_type&required_message_mask)) continue;
            age=fabs(timediff(nav->geph[i].toe,time));
            if (age>MAXDTOE_GLO) continue;
            if (!geph||age<best_age||(fabs(age-best_age)<1E-9&&
                timediff(nav->geph[i].tof,geph->tof)>0.0)) {
                geph=nav->geph+i;
                best_age=age;
                type=candidate_type;
            }
        }
        if (!geph) return 0;
        *geph_out=*geph;
        if (info) {
            info->system=sys;
            info->message_type=type;
            info->iode=geph->iode;
        }
        return 1;
    }
""",
    """    if (sys==SYS_GLO) {
        geph=select_signal_geph(time,sat,code,required_message_mask,nav,&type);
        if (!geph) return 0;
        *geph_out=*geph;
        if (info) {
            info->system=sys;
            info->message_type=type;
            info->iode=geph->iode;
        }
        return 1;
    }
""",
)

# Dedicated GLO selector/bias regression.
write("tests/signal_bias_ext/test_glo_signal_bias.c", r'''#include "rtklib.h"
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
''')
replace_once(
    "tests/signal_bias_ext/makefile",
    """all: test_signal_bias_ext

test_signal_bias_ext: test_signal_bias_ext.c $(SRC)/rtkcmn.c
	$(CC) $(CFLAGS) test_signal_bias_ext.c $(SRC)/rtkcmn.c $(LDLIBS) -o $@

test: test_signal_bias_ext
	./test_signal_bias_ext

clean:
	rm -f test_signal_bias_ext test_signal_bias_ext.exe
""",
    """all: test_signal_bias_ext test_glo_signal_bias

test_signal_bias_ext: test_signal_bias_ext.c $(SRC)/rtkcmn.c
	$(CC) $(CFLAGS) test_signal_bias_ext.c $(SRC)/rtkcmn.c $(LDLIBS) -o $@

test_glo_signal_bias: test_glo_signal_bias.c $(SRC)/rtkcmn.c
	$(CC) $(CFLAGS) test_glo_signal_bias.c $(SRC)/rtkcmn.c $(LDLIBS) -o $@

test: all
	./test_signal_bias_ext
	./test_glo_signal_bias

clean:
	rm -f test_signal_bias_ext test_signal_bias_ext.exe test_glo_signal_bias test_glo_signal_bias.exe
""",
)

# G3 truth-state code/Doppler regression plus an explicit beta-clock assertion.
write("tests/residual_ext/test_glo_l3oc_residual.c", r'''#include "rtklib.h"
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
                      expected_clk,1E-15)) return 1;

    stat=rtklib_signal_code_bias_ext(t,sat,CODE_L3Q,NAV_L3OC,&nav,&bias,&info);
    if (stat!=1||info.message_type!=NAV_L3OC||
        !expect_close("G3 pilot code bias",bias,-CLIGHT*geph.isc_l3ocp,1E-9)) return 1;

    opt.ionoopt=IONOOPT_OFF;
    opt.tropopt=TROPOPT_OFF;
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
''')
replace_once(
    "tests/residual_ext/makefile",
    """all: test_residual_ext

test_residual_ext: $(SOURCES)
	$(CC) $(CFLAGS) $(SOURCES) $(LDFLAGS) $(LDLIBS) -o $@

test: test_residual_ext
	./test_residual_ext

clean:
	rm -f test_residual_ext test_residual_ext.exe
""",
    """all: test_residual_ext test_glo_l3oc_residual

test_residual_ext: $(SOURCES)
	$(CC) $(CFLAGS) $(SOURCES) $(LDFLAGS) $(LDLIBS) -o $@

test_glo_l3oc_residual: test_glo_l3oc_residual.c unused_correction_stubs.c $(SRC)/rtkcmn.c \\
          $(SRC)/ephemeris.c $(SRC)/pntpos.c $(SRC)/rtklib_signal_state_ext.c \\
          $(SRC)/rtklib_residual_ext.c
	$(CC) $(CFLAGS) test_glo_l3oc_residual.c unused_correction_stubs.c $(SRC)/rtkcmn.c \\
          $(SRC)/ephemeris.c $(SRC)/pntpos.c $(SRC)/rtklib_signal_state_ext.c \\
          $(SRC)/rtklib_residual_ext.c $(LDFLAGS) $(LDLIBS) -o $@

test: all
	./test_residual_ext
	./test_glo_l3oc_residual

clean:
	rm -f test_residual_ext test_residual_ext.exe test_glo_l3oc_residual test_glo_l3oc_residual.exe
""",
)

# Parser-alignment regression: one full 9-line L3OC record immediately followed
# by a legacy FDMA record. This catches the previous 19-field early return.
write("tests/rinex4_glo_l3oc/test_rinex4_glo_l3oc.c", r'''#include "rtklib.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static void fields4(FILE *fp, double a, double b, double c, double d)
{
    fprintf(fp,"    %19.12E%19.12E%19.12E%19.12E\n",a,b,c,d);
}

int main(void)
{
    const char *path="rinex4_glo_l3oc_fixture.rnx";
    FILE *fp=fopen(path,"w");
    nav_t nav={0};
    geph_t *g;
    int stat,ok=1;

    if (!fp) return 1;
    fprintf(fp,"%9.2f%-11s%-20s%-20s%-20s\n",4.02,"","N: GNSS NAV DATA","M: Mixed","RINEX VERSION / TYPE");
    fprintf(fp,"%60s%-20s\n","","END OF HEADER");

    fprintf(fp,"> EPH R26 L3OC\n");
    fprintf(fp,"R26 2026 08 27 10 15 00%19.12E%19.12E%19.12E\n",-2E-5,1E-10,3E-13);
    fields4(fp,19100.0,0.0,0.0,0.0);
    fields4(fp,0.0,3.0,0.0,0.0);
    fields4(fp,15000.0,0.0,0.0,2.5E-8);
    fields4(fp,2.0,3.0,1.0,2.0);
    fields4(fp,0.0,36000.0,1.0,2.0);
    fields4(fp,0.1,0.0,0.01,0.001);
    fields4(fp,0.1,0.5,-0.25,1.0);
    fprintf(fp,"    %19.12E%19.12E%19s%19.12E\n",2.0,3.0,"",382500.0);

    fprintf(fp,"> EPH R27 FDMA\n");
    fprintf(fp,"R27 2026 08 27 10 30 00%19.12E%19.12E%19.12E\n",-1E-4,2E-10,383400.0);
    fields4(fp,19000.0,0.0,0.0,0.0);
    fields4(fp,1000.0,2.5,0.0,-4.0);
    fields4(fp,14000.0,0.0,0.0,5.0);
    fields4(fp,0.0,1E-8,2.0,0.0);
    fclose(fp);

    stat=readrnx(path,1,"",NULL,&nav,NULL);
    remove(path);
    if (!stat||nav.ng!=2) {
        fprintf(stderr,"parser alignment failed: stat=%d ng=%d\n",stat,nav.ng);
        free(nav.geph);
        return 1;
    }

    g=&nav.geph[0];
    if (g->hdr.msg_type!=NAV_L3OC||fabs(g->beta-3E-13)>1E-18||
        fabs(g->isc_l3ocp-2.5E-8)>1E-15||g->data_validity!=0||
        fabs(g->pc[0]-0.5)>1E-12||fabs(g->pc[1]+0.25)>1E-12||
        fabs(g->pc[2]-1.0)>1E-12||fabs(g->ttm-382500.0)>1E-9) {
        fprintf(stderr,"L3OC fields decoded incorrectly\n");
        ok=0;
    }
    g=&nav.geph[1];
    if (g->hdr.msg_type!=NAV_FDMA||g->frq!=-4) {
        fprintf(stderr,"record following L3OC was not aligned/decoded\n");
        ok=0;
    }
    free(nav.geph);
    if (!ok) return 1;
    puts("rinex4_glo_l3oc: PASS");
    return 0;
}
''')
write("tests/rinex4_glo_l3oc/makefile", r'''SRC = ../../src
CFLAGS = -O2 -std=c99 -Wall -Wextra -pedantic -ffunction-sections -fdata-sections \
         -I$(SRC) -DENAGLO -DENAQZS -DENAGAL -DENACMP -DNFREQ=3
LDFLAGS = -Wl,--gc-sections
LDLIBS = -lm -lpthread
SOURCES = test_rinex4_glo_l3oc.c $(SRC)/rinex.c $(SRC)/rtkcmn.c

all: test_rinex4_glo_l3oc

test_rinex4_glo_l3oc: $(SOURCES)
	$(CC) $(CFLAGS) $(SOURCES) $(LDFLAGS) $(LDLIBS) -o $@

test: test_rinex4_glo_l3oc
	./test_rinex4_glo_l3oc

clean:
	rm -f test_rinex4_glo_l3oc test_rinex4_glo_l3oc.exe rinex4_glo_l3oc_fixture.rnx

.PHONY: all test clean
''')

print("GLONASS L3OC patch applied successfully")
