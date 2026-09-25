#include "rtklib_signal_bias_ext.h"

#include <math.h>
#include <string.h>

/* Signal-level health from the raw RINEX SV-health field.  The raw value
 * stays untouched in nav_t; only the health returned for the selected
 * observation signal is normalized.
 * - GPS/QZSS CNAV: L1/L2/L5 health in bits 2/1/0 (IS-GPS-200 30.3.3.1.1.2,
 *   IS-QZSS-PNT-006 4.3.2).
 * - GPS/QZSS CNAV-2: the L1C health bit.
 * - QZSS LNAV (IS-QZSS-PNT-006 4.1.2.3(4)): bit 5 is the health of the
 *   transmitted L1C/A or L1C/B signal; bits 4..0 are L1C/A, L2C, L5, L1C and
 *   L1C/B.  The health bit of the L1 legacy signal that is not transmitted
 *   is always 1, so a nonzero raw value is not by itself unhealthy.
 * Other families keep the raw value (nonzero = unhealthy). */
static int gps_qzs_band(int system, unsigned char code)
{
    if (code==CODE_L1C||code==CODE_L1S||code==CODE_L1L||code==CODE_L1X||
        (system==SYS_QZS&&code==CODE_L1E)) return 1;
    if (code==CODE_L2S||code==CODE_L2L||code==CODE_L2X) return 2;
    if (code==CODE_L5I||code==CODE_L5Q||code==CODE_L5X) return 5;
    return 0;
}

int rtklib_signal_health_ext(int system, int message_type,
                             unsigned char code, int raw_svh)
{
    int band;

    if (system!=SYS_GPS&&system!=SYS_QZS) return raw_svh;
    band=gps_qzs_band(system,code);

    if (message_type==NAV_CNAV) {
        if (band==1) return raw_svh&4?1:0;
        if (band==2) return raw_svh&2?1:0;
        if (band==5) return raw_svh&1?1:0;
    }
    else if (message_type==NAV_CNV2) {
        if (band==1) return raw_svh&1?1:0;
    }
    else if (system==SYS_QZS&&message_type==NAV_LNAV&&raw_svh>=0&&
             raw_svh<64) {
        if (code==CODE_L1C) return raw_svh&(32|16)?1:0;
        if (code==CODE_L1E) return raw_svh&(32|1)?1:0;
        if (band==2) return raw_svh&8?1:0;
        if (band==5) return raw_svh&4?1:0;
        if (band==1) return raw_svh&2?1:0;
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
    gtime_t raw_transmit_time,selection_time,transmit_time;
    int i,stat;

    if (!nav||!rs||!dts||!var||!svh||sat<=0||sat>MAXSAT||
        code==CODE_NONE||!isfinite(pseudorange_m)||pseudorange_m<=0.0) return -1;

    /* Derive the raw transmit epoch from the observed pseudorange first. For a
     * forced signal/message family, select its broadcast record at that signal
     * epoch so a receive-time NAV handover cannot mix a new record with a signal
     * transmitted under the preceding record. A zero message mask is the generic
     * RTKLIB path and intentionally preserves stock satposs() semantics: teph is
     * the observation receive epoch, while orbit/clock propagation still occurs
     * at transmit time. */
    raw_transmit_time=timeadd(receive_time,-pseudorange_m/CLIGHT);
    selection_time=required_message_mask?raw_transmit_time:receive_time;

    memset(&eph,0,sizeof(eph));
    memset(&geph,0,sizeof(geph));
    memset(&selected_info,0,sizeof(selected_info));
    stat=rtklib_signal_ephemeris_ext(selection_time,sat,code,
                                     required_message_mask,nav,&eph,&geph,
                                     &selected_info);
    if (stat<=0) return stat;

    if (selected_info.system==SYS_GLO) dt=geph2clk(raw_transmit_time,&geph);
    else                              dt=eph2clk (raw_transmit_time,&eph );
    transmit_time=timeadd(raw_transmit_time,-dt);

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
