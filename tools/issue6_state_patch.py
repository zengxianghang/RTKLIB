from pathlib import Path

p = Path("src/rtkcmn.c")
s = p.read_text()
if "int rtklib_signal_ephemeris_ext(" not in s:
    s += r'''

/* signal/message-family broadcast ephemeris selector ------------------------*/
int rtklib_signal_ephemeris_ext(gtime_t time, int sat, unsigned char code,
                                int required_message_mask, const nav_t *nav,
                                eph_t *eph_out, geph_t *geph_out,
                                rtklib_signal_bias_info_ext_t *info)
{
    const eph_t *eph=NULL;
    const geph_t *geph=NULL;
    double age,best_age=0.0,max_age;
    int i,sys,type=0;

    if (!nav||!eph_out||!geph_out||sat<=0||sat>MAXSAT||code==CODE_NONE) return -1;
    sys=satsys(sat,NULL);
    if (sys==SYS_NONE) return -1;
    memset(eph_out,0,sizeof(*eph_out));
    memset(geph_out,0,sizeof(*geph_out));
    if (info) memset(info,0,sizeof(*info));

    if (sys==SYS_GLO) {
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

    if (!required_message_mask) {
        eph=select_signal_eph(time,sat,code,0,nav,&type);
    }
    else {
        max_age=max_eph_age_sec(sys);
        for (i=0;i<nav->n;i++) {
            int candidate_type;
            if (nav->eph[i].sat!=sat) continue;
            candidate_type=canonical_message_type(nav->eph+i,sys);
            if (!(candidate_type&required_message_mask)) continue;
            age=fabs(timediff(nav->eph[i].toe,time));
            if (age>max_age) continue;
            if (!eph||age<best_age||(fabs(age-best_age)<1E-9&&
                timediff(nav->eph[i].toc,eph->toc)>0.0)) {
                eph=nav->eph+i;
                best_age=age;
                type=candidate_type;
            }
        }
    }
    if (!eph) return 0;
    *eph_out=*eph;
    if (info) {
        info->system=sys;
        info->message_type=type;
        info->iode=eph->iode;
    }
    return 1;
}
'''
    p.write_text(s)
