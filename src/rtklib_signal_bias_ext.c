#include "rtklib_signal_bias_ext.h"
#include "rtklib_obs_ext.h"

#include <math.h>
#include <string.h>

static double max_eph_age_sec(int sys)
{
    if (sys==SYS_QZS) return MAXDTOE_QZS;
    if (sys==SYS_GAL) return MAXDTOE_GAL;
    if (sys==SYS_CMP) return MAXDTOE_CMP;
    return MAXDTOE;
}

static int canonical_message_type(const eph_t *eph, int sys)
{
    int type;
    if (!eph) return 0;
    type=eph->hdr.msg_type;
    if (type) return type;

    if (sys==SYS_GPS||sys==SYS_QZS) return NAV_LNAV;
    if (sys==SYS_CMP) return NAV_D1D2;
    if (sys==SYS_GAL) {
        if (eph->code&(1<<9)) return NAV_INAV;
        if (eph->code&(1<<8)) return NAV_FNAV;
        if (eph->code&((1<<0)|(1<<2))) return NAV_INAV;
        if (eph->code&(1<<1)) return NAV_FNAV;
    }
    return 0;
}

static int eph_supports_code(int sys, int type, unsigned char code)
{
    if (sys==SYS_GPS) {
        if (code==CODE_L1C) return type==NAV_LNAV||type==NAV_CNAV||type==NAV_CNV2;
        if (code==CODE_L1L) return type==NAV_CNV2;
        if (code==CODE_L2P) return type==NAV_LNAV;
        if (code==CODE_L2S) return type==NAV_CNAV||type==NAV_CNV2;
        if (code==CODE_L5Q) return type==NAV_CNAV||type==NAV_CNV2;
        return 0;
    }
    if (sys==SYS_QZS) {
        if (code==CODE_L1C) return type==NAV_LNAV||type==NAV_CNAV||type==NAV_CNV2;
        if (code==CODE_L1L) return type==NAV_CNV2;
        if (code==CODE_L2S) return type==NAV_CNAV||type==NAV_CNV2;
        if (code==CODE_L5Q) return type==NAV_CNAV||type==NAV_CNV2;
        return 0;
    }
    if (sys==SYS_GAL) {
        if (code==CODE_L1C) return type==NAV_INAV||type==NAV_FNAV;
        if (code==CODE_L5Q) return type==NAV_FNAV;
        if (code==CODE_L7Q) return type==NAV_INAV;
        return 0;
    }
    if (sys==SYS_CMP) {
        if (code==CODE_L2I||code==CODE_L6I||code==CODE_L7I) {
            return type==NAV_D1D2||type==NAV_D1||type==NAV_D2;
        }
        if (code==CODE_L1P||code==CODE_L5P) return type==NAV_CNV1||type==NAV_CNV2;
        if (code==CODE_L7D) return type==NAV_CNV3;
        return 0;
    }
    return 0;
}

static const eph_t *select_signal_eph(gtime_t time, int sat, unsigned char code,
                                      int required_message_type,
                                      const nav_t *nav, int *message_type)
{
    const eph_t *best=NULL;
    double best_age=0.0,max_age;
    int i,sys,type;

    if (!nav||(sys=satsys(sat,NULL))==SYS_NONE) return NULL;
    max_age=max_eph_age_sec(sys);

    for (i=0;i<nav->n;i++) {
        double age;
        if (nav->eph[i].sat!=sat) continue;
        type=canonical_message_type(nav->eph+i,sys);
        if (required_message_type&&type!=required_message_type) continue;
        if (!eph_supports_code(sys,type,code)) continue;
        age=fabs(timediff(nav->eph[i].toe,time));
        if (age>max_age) continue;
        if (!best||age<best_age||(fabs(age-best_age)<1E-9&&
            timediff(nav->eph[i].toc,best->toc)>0.0)) {
            best=nav->eph+i;
            best_age=age;
            if (message_type) *message_type=type;
        }
    }
    return best;
}

static const geph_t *select_signal_geph(gtime_t time, int sat,
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

static double freq_ratio_squared(double reference_hz, double signal_hz)
{
    double ratio=reference_hz/signal_hz;
    return ratio*ratio;
}

int rtklib_signal_code_bias_ext(gtime_t time, int sat, unsigned char code,
                                int required_message_type, const nav_t *nav,
                                double *raw_code_bias_m,
                                rtklib_signal_bias_info_ext_t *info)
{
    const eph_t *eph;
    const geph_t *geph;
    double bias=0.0;
    int sys,type=0;

    if (!nav||!raw_code_bias_m||sat<=0||sat>MAXSAT||code==CODE_NONE) return -1;
    sys=satsys(sat,NULL);
    if (sys==SYS_NONE) return -1;
    if (info) memset(info,0,sizeof(*info));

    if (sys==SYS_GLO) {
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

    eph=select_signal_eph(time,sat,code,required_message_type,nav,&type);
    if (!eph) return 0;

    if (sys==SYS_GPS||sys==SYS_QZS) {
        if (type==NAV_LNAV) {
            if (code==CODE_L1C) bias=CLIGHT*eph->tgd[0];
            else if (code==CODE_L2P) {
                bias=CLIGHT*freq_ratio_squared(FREQ1,FREQ2)*eph->tgd[0];
            }
            else return 0;
        }
        else {
            if (code==CODE_L1C) bias=CLIGHT*(eph->tgd[0]-eph->isc[0]);
            else if (code==CODE_L1L) bias=CLIGHT*(eph->tgd[0]-eph->isc[5]);
            else if (code==CODE_L2S) bias=CLIGHT*(eph->tgd[0]-eph->isc[1]);
            else if (code==CODE_L5Q) bias=CLIGHT*(eph->tgd[0]-eph->isc[3]);
            else return 0;
        }
    }
    else if (sys==SYS_GAL) {
        if (code==CODE_L1C) {
            if (type==NAV_FNAV) bias=CLIGHT*eph->tgd[0];
            else if (type==NAV_INAV) bias=CLIGHT*eph->tgd[1];
            else return 0;
        }
        else if (code==CODE_L5Q&&type==NAV_FNAV) {
            bias=CLIGHT*freq_ratio_squared(FREQ1,FREQ5)*eph->tgd[0];
        }
        else if (code==CODE_L7Q&&type==NAV_INAV) {
            bias=CLIGHT*freq_ratio_squared(FREQ1,FREQ7)*eph->tgd[1];
        }
        else return 0;
    }
    else if (sys==SYS_CMP) {
        if (type==NAV_D1D2||type==NAV_D1||type==NAV_D2) {
            if (code==CODE_L2I) bias=CLIGHT*eph->tgd[0];
            else if (code==CODE_L7I) bias=CLIGHT*eph->tgd[1];
            else if (code==CODE_L6I) bias=0.0;
            else return 0;
        }
        else if (type==NAV_CNV1||type==NAV_CNV2) {
            if (code==CODE_L1P) bias=CLIGHT*eph->tgd[0];
            else if (code==CODE_L5P) bias=CLIGHT*eph->tgd[1];
            else return 0;
        }
        else if (type==NAV_CNV3&&code==CODE_L7D) {
            bias=CLIGHT*eph->tgd[0];
        }
        else return 0;
    }
    else return 0;

    if (info) {
        info->system=sys;
        info->message_type=type;
        info->iode=eph->iode;
        info->raw_code_bias_m=bias;
    }
    *raw_code_bias_m=bias;
    return 1;
}
