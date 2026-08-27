from pathlib import Path

p = Path("src/rtkcmn.c")
s = p.read_text()
if "int rtklib_signal_state_ext(" not in s:
    s += r'''

/* signal/message-family broadcast satellite state ---------------------------*/
int rtklib_signal_state_ext(gtime_t receive_time, double pseudorange_m,
                            int sat, unsigned char code,
                            int required_message_mask, const nav_t *nav,
                            double rs[6], double dts[2], double *var,
                            int *svh, rtklib_signal_bias_info_ext_t *info)
{
    const eph_t *eph=NULL;
    const geph_t *geph=NULL;
    double rst[3]={0},dtst=0.0,vart=0.0,dt,age,best_age=0.0;
    gtime_t transmit_time;
    int i,sys,type=0;

    if (!nav||!rs||!dts||!var||!svh||sat<=0||sat>MAXSAT||
        code==CODE_NONE||!isfinite(pseudorange_m)||pseudorange_m<=0.0) return -1;

    sys=satsys(sat,NULL);
    if (sys==SYS_NONE) return -1;
    if (info) memset(info,0,sizeof(*info));

    if (sys==SYS_GLO) {
        for (i=0;i<nav->ng;i++) {
            int candidate_type;
            if (nav->geph[i].sat!=sat) continue;
            candidate_type=nav->geph[i].hdr.msg_type?nav->geph[i].hdr.msg_type:NAV_FDMA;
            if (required_message_mask&&!(candidate_type&required_message_mask)) continue;
            age=fabs(timediff(nav->geph[i].toe,receive_time));
            if (age>MAXDTOE_GLO) continue;
            if (!geph||age<best_age||(fabs(age-best_age)<1E-9&&
                timediff(nav->geph[i].tof,geph->tof)>0.0)) {
                geph=nav->geph+i;
                best_age=age;
                type=candidate_type;
            }
        }
        if (!geph) return 0;
    }
    else {
        eph=select_signal_eph(receive_time,sat,code,required_message_mask,
                              nav,&type);
        if (!eph) return 0;
    }

    transmit_time=timeadd(receive_time,-pseudorange_m/CLIGHT);
    if (geph) dt=geph2clk(transmit_time,geph);
    else      dt=eph2clk (transmit_time,eph );
    transmit_time=timeadd(transmit_time,-dt);

    if (geph) {
        geph2pos(transmit_time,geph,rs,dts,var);
        geph2pos(timeadd(transmit_time,1E-3),geph,rst,&dtst,&vart);
        *svh=geph->svh;
        if (info) {
            info->system=sys;
            info->message_type=type;
            info->iode=geph->iode;
        }
    }
    else {
        eph2pos(transmit_time,eph,rs,dts,var);
        eph2pos(timeadd(transmit_time,1E-3),eph,rst,&dtst,&vart);
        *svh=eph->svh;
        if (info) {
            info->system=sys;
            info->message_type=type;
            info->iode=eph->iode;
        }
    }
    for (i=0;i<3;i++) rs[i+3]=(rst[i]-rs[i])/1E-3;
    dts[1]=(dtst-dts[0])/1E-3;
    return 1;
}
'''
    p.write_text(s)
