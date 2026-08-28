#include "rtklib_signal_bias_ext.h"

#include <math.h>
#include <string.h>

/* GPS CNAV stores L1/L2/L5 health in bits 2/1/0 of the RINEX SV-health
 * field. GPS CNV2 carries the L1C health bit. Keep the raw eph->svh value
 * untouched in nav_t and normalize only the health returned for the selected
 * observation signal. Other message families retain the legacy semantics. */
int rtklib_signal_health_ext(int system, int message_type,
                             unsigned char code, int raw_svh)
{
    if (system!=SYS_GPS) return raw_svh;

    if (message_type==NAV_CNAV) {
        if (code==CODE_L1C||code==CODE_L1L) return raw_svh&4?1:0;
        if (code==CODE_L2S)                 return raw_svh&2?1:0;
        if (code==CODE_L5Q)                 return raw_svh&1?1:0;
    }
    else if (message_type==NAV_CNV2) {
        if (code==CODE_L1C||code==CODE_L1L) return raw_svh&1?1:0;
    }
    return raw_svh;
}

int rtklib_signal_state_ext(gtime_t receive_time, double pseudorange_m,
                            int sat, unsigned char code,
                            int required_message_mask, const nav_t *nav,
                            double rs[6], double dts[2], double *var,
                            int *svh, rtklib_signal_bias_info_ext_t *info)
{
    eph_t eph;
    geph_t geph;
    rtklib_signal_bias_info_ext_t selected_info;
    double rst[3]={0},dtst=0.0,vart=0.0,dt;
    gtime_t transmit_time;
    int i,stat;

    if (!nav||!rs||!dts||!var||!svh||sat<=0||sat>MAXSAT||
        code==CODE_NONE||!isfinite(pseudorange_m)||pseudorange_m<=0.0) return -1;

    memset(&eph,0,sizeof(eph));
    memset(&geph,0,sizeof(geph));
    memset(&selected_info,0,sizeof(selected_info));
    stat=rtklib_signal_ephemeris_ext(receive_time,sat,code,
                                     required_message_mask,nav,&eph,&geph,
                                     &selected_info);
    if (stat<=0) return stat;

    transmit_time=timeadd(receive_time,-pseudorange_m/CLIGHT);
    if (selected_info.system==SYS_GLO) dt=geph2clk(transmit_time,&geph);
    else                              dt=eph2clk (transmit_time,&eph );
    transmit_time=timeadd(transmit_time,-dt);

    if (selected_info.system==SYS_GLO) {
        geph2pos(transmit_time,&geph,rs,dts,var);
        geph2pos(timeadd(transmit_time,1E-3),&geph,rst,&dtst,&vart);
        *svh=geph.svh;
    }
    else {
        eph2pos(transmit_time,&eph,rs,dts,var);
        eph2pos(timeadd(transmit_time,1E-3),&eph,rst,&dtst,&vart);
        *svh=rtklib_signal_health_ext(selected_info.system,
                                      selected_info.message_type,
                                      code,eph.svh);
    }
    for (i=0;i<3;i++) rs[i+3]=(rst[i]-rs[i])/1E-3;
    dts[1]=(dtst-dts[0])/1E-3;
    if (info) *info=selected_info;
    return 1;
}
