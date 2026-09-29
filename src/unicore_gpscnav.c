#include "unicore_gpscnav.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define GPS_CNAV_A_REF 26559710.0

static unsigned int unicore_crc32(const char *buf, int len)
{
    unsigned int crc=0;
    int i,j;

    for (i=0;i<len;i++) {
        crc^=(unsigned char)buf[i];
        for (j=0;j<8;j++) {
            crc=(crc&1U)?((crc>>1)^0xEDB88320U):(crc>>1);
        }
    }
    return crc;
}

static void append_field(char *out, int *pos, const char *fmt, ...)
{
    va_list ap;

    va_start(ap,fmt);
    *pos+=vsprintf(out+*pos,fmt,ap);
    va_end(ap);
}

static int ura_index(double value)
{
    int index;

    /* Unicore URAIndex[] fields are UCHAR. Per converter policy, RINEX
     * negative/unknown URA components are exported as zero. */
    if (value<0.0) return 0;
    index=(int)value;
    if (index>255) return 255;
    return index;
}

static int gps_leap_seconds(gtime_t gpst)
{
    double dt=timediff(gpst,gpst2utc(gpst));
    int leap=(int)(dt>=0.0?dt+0.5:dt-0.5);

    return leap>0?leap:0;
}

int write_unicore_gpscnav_eph(FILE *fp, const eph_t *eph)
{
    char body[4096];
    unsigned int crc;
    unsigned int header_ms;
    double tow,toe,toc;
    int pos=0;
    int week=0,zweek=0;
    int sys,prn;
    int leap;
    int reserved0;

    if (!fp||!eph) return 0;

    sys=satsys(eph->sat,&prn);
    if (sys!=SYS_GPS) return 0;
    if (eph->hdr.msg_type!=NAV_CNAV&&eph->hdr.msg_type!=NAV_CNV2) return 0;

    tow=time2gpst(eph->ttr,&zweek);
    toe=eph->toes;
    toc=time2gpst(eph->toc,&week);
    leap=gps_leap_seconds(eph->ttr);
    header_ms=(unsigned int)(tow*1000.0+0.5);
    if (header_ms>=604800000U) header_ms=604799999U;

    /* Unicore ASCII header: Message,CPUIDle,TimeRef,TimeStatus,Wn,Ms,
     * Res,Res,LeapSec,Res; RINEX has no receiver CPU/time-status metadata,
     * so deterministic converter values are used for those fields. */
    pos+=sprintf(body,"#GPSCNAVEPHA,97,GPS,FINE,%d,%u,0,0,%d,0;",
                 zweek,header_ms,leap);

    /* reserved[0]: 1 for CNAV/L5-style ephemeris, 0 for CNAV-2/L1C.
     * The remaining four reserved bytes are zero. */
    reserved0=eph->hdr.msg_type==NAV_CNV2?0:1;
    append_field(body,&pos,
        "%d,%d,%d,%d,0,0,0,0",
        prn,
        eph->svh,
        0,      /* ISF */
        reserved0);

    append_field(body,&pos,
        ",%d,%d,%d,%d,%d,%d",
        (int)eph->top,
        (int)eph->wn_op,
        ura_index(eph->urai_ed),
        ura_index(eph->urai_ned[0]),
        ura_index(eph->urai_ned[1]),
        ura_index(eph->urai_ned[2]));

    append_field(body,&pos,
        ",%d,%d,%.1f,%.1f,%.12e,%.12e,%.12e,%.12e",
        week,zweek,tow,
        toe,
        eph->A-GPS_CNAV_A_REF,
        eph->Adot,
        eph->delta_n0,
        eph->delta_n0_dot);

    append_field(body,&pos,
        ",%.12e,%.12e,%.12e,%.12e,%.12e,%.12e,%.12e,%.12e",
        eph->M0,eph->e,eph->omg,
        eph->cuc,eph->cus,
        eph->crc,eph->crs,
        eph->cic);

    append_field(body,&pos,
        ",%.12e,%.12e,%.12e,%.12e,%.12e,%.1f,%.12e",
        eph->cis,eph->i0,eph->idot,
        eph->OMG0,eph->OMGd,
        toc,eph->tgd[0]);

    /* Unicore order differs from RTKLIB isc[] order. */
    append_field(body,&pos,
        ",%.12e,%.12e,%.12e,%.12e,%.12e,%.12e",
        eph->isc[5],eph->isc[4],eph->isc[0],
        eph->isc[1],eph->isc[2],eph->isc[3]);

    /* Af2 is the final GPSCNAVEPH payload field. */
    append_field(body,&pos,
        ",%.12e,%.12e,%.12e",
        eph->f0,eph->f1,eph->f2);

    crc=unicore_crc32(body+1,pos-1);

    /* Use '\n' with a text-mode stream. MSVC translates it to CRLF once. */
    fprintf(fp,"%s*%08x\n",body,crc);
    return 1;
}
