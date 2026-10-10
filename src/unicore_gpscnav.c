#include "unicore_gpscnav.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define GPS_CNAV_A_REF 26559710.0
#define UNICORE_QZS_MIN_PRN 33
#define UNICORE_QZS_MAX_PRN 42

static unsigned int ascii_crc32(const char *buf, int len)
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

static int gpscnav_output_prn(const eph_t *eph, int *out_prn)
{
    int sys,prn,mapped_prn;

    if (!eph) return 0;

    sys=satsys(eph->sat,&prn);
    if (sys==SYS_GPS && prn>=MINPRNGPS && prn<=MAXPRNGPS) {
        mapped_prn=prn;
    }
    else if (sys==SYS_QZS && prn>=MINPRNQZS && prn<=MAXPRNQZS) {
        mapped_prn=UNICORE_QZS_MIN_PRN+(prn-MINPRNQZS);
        if (mapped_prn>UNICORE_QZS_MAX_PRN) return 0;
    }
    else {
        return 0;
    }
    if (out_prn) *out_prn=mapped_prn;
    return 1;
}

static void append_gpscnav_payload(char *body, int *pos, const eph_t *eph,
                                   int prn, int reserved0, double tow,
                                   double toe, double toc, int week, int zweek)
{
    /* GPSCNAVEPH PRN namespace: GPS 1..32, QZSS 33..42.
     * RTKLIB/RINEX QZSS PRNs 193..202 are mapped before this call. */
    append_field(body,pos,
        "%d,%d,%d,%d,0,0,0,0",
        prn,
        eph->svh,
        0,      /* ISF */
        reserved0);

    append_field(body,pos,
        ",%d,%d,%d,%d,%d,%d",
        (int)eph->top,
        (int)eph->wn_op,
        ura_index(eph->urai_ed),
        ura_index(eph->urai_ned[0]),
        ura_index(eph->urai_ned[1]),
        ura_index(eph->urai_ned[2]));

    append_field(body,pos,
        ",%d,%d,%.1f,%.1f,%.12e,%.12e,%.12e,%.12e",
        week,zweek,tow,
        toe,
        eph->A-GPS_CNAV_A_REF,
        eph->Adot,
        eph->delta_n0,
        eph->delta_n0_dot);

    append_field(body,pos,
        ",%.12e,%.12e,%.12e,%.12e,%.12e,%.12e,%.12e,%.12e",
        eph->M0,eph->e,eph->omg,
        eph->cuc,eph->cus,
        eph->crc,eph->crs,
        eph->cic);

    append_field(body,pos,
        ",%.12e,%.12e,%.12e,%.12e,%.12e,%.1f,%.12e",
        eph->cis,eph->i0,eph->idot,
        eph->OMG0,eph->OMGd,
        toc,eph->tgd[0]);

    /* Output order differs from RTKLIB isc[] order. */
    append_field(body,pos,
        ",%.12e,%.12e,%.12e,%.12e,%.12e,%.12e",
        eph->isc[5],eph->isc[4],eph->isc[0],
        eph->isc[1],eph->isc[2],eph->isc[3]);

    append_field(body,pos,
        ",%.12e,%.12e,%.12e",
        eph->f0,eph->f1,eph->f2);
}

int write_unicore_gpscnav_eph(FILE *fp, const eph_t *eph)
{
    char body[4096];
    unsigned int crc;
    unsigned int header_ms;
    double tow,toe,toc;
    int pos=0;
    int week=0,zweek=0;
    int prn;
    int leap;

    if (!fp||!eph) return 0;
    if (!gpscnav_output_prn(eph,&prn)) return 0;
    if (eph->hdr.msg_type!=NAV_CNAV) return 0;

    tow=time2gpst(eph->ttr,&zweek);
    toe=eph->toes;
    toc=time2gpst(eph->toc,&week);
    leap=gps_leap_seconds(eph->ttr);
    header_ms=(unsigned int)(tow*1000.0+0.5);
    if (header_ms>=604800000U) header_ms=604799999U;

    /* Unicore ASCII header: Message,CPUIDle,TimeRef,TimeStatus,Wn,Ms,
     * Res,Res,LeapSec,Res. */
    pos+=sprintf(body,"#GPSCNAVEPHA,97,GPS,FINE,%d,%u,0,0,%d,0;",
                 zweek,header_ms,leap);

    /* reserved[0]=1 for CNAV/L2/L5-style ephemeris. */
    append_gpscnav_payload(body,&pos,eph,prn,1,tow,toe,toc,week,zweek);

    crc=ascii_crc32(body+1,pos-1);
    fprintf(fp,"%s*%08x\n",body,crc);
    return 1;
}

int write_novatel_gpsl1c_eph(FILE *fp, const eph_t *eph)
{
    char body[4096];
    unsigned int crc;
    unsigned int header_ms;
    double tow,toe,toc,header_seconds;
    int pos=0;
    int week=0,zweek=0;
    int prn;

    if (!fp||!eph) return 0;
    if (!gpscnav_output_prn(eph,&prn)) return 0;
    if (eph->hdr.msg_type!=NAV_CNV2) return 0;

    tow=time2gpst(eph->ttr,&zweek);
    toe=eph->toes;
    toc=time2gpst(eph->toc,&week);
    header_ms=(unsigned int)(tow*1000.0+0.5);
    if (header_ms>=604800000U) header_ms=604799999U;
    header_seconds=(double)header_ms/1000.0;

    /* NovAtel-style ASCII header:
     * Message,Port,Sequence,IdleTime,TimeStatus,Week,Seconds,
     * ReceiverStatus,Reserved,ReceiverSWVersion.
     * RINEX has no receiver metadata, so deterministic zero values are used
     * for sequence/idle/status/reserved/software-version fields. */
    pos+=sprintf(body,
        "#GPSL1CEPHEMA,COM1,0,0.0,FINE,%d,%.3f,0,0,0;",
        zweek,header_seconds);

    /* GPSL1CEPHEMA body intentionally reuses the GPSCNAVEPH body layout.
     * reserved[0]=0 identifies CNV2/L1C-style ephemeris. */
    append_gpscnav_payload(body,&pos,eph,prn,0,tow,toe,toc,week,zweek);

    crc=ascii_crc32(body+1,pos-1);
    fprintf(fp,"%s*%08x\n",body,crc);
    return 1;
}
