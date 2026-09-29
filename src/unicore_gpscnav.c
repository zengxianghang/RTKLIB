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

int write_unicore_gpscnav_eph(FILE *fp, const eph_t *eph)
{
    char body[4096];
    unsigned int crc;
    double tow,toe,toc;
    int pos=0;
    int week=0,zweek=0;
    int sys,prn;

    if (!fp||!eph) return 0;

    sys=satsys(eph->sat,&prn);
    if (sys!=SYS_GPS) return 0;
    if (eph->hdr.msg_type!=NAV_CNAV&&eph->hdr.msg_type!=NAV_CNV2) return 0;

    tow=time2gpst(eph->ttr,&zweek);
    toe=eph->toes;
    toc=time2gpst(eph->toc,&week);

    pos+=sprintf(body,"#GPSCNAVEPHA");

    /*
     * Unicore GPSCNAVEPHA payload.
     * RINEX 4.02 optional integer flags are intentionally not decoded yet;
     * the corresponding output value is fixed to zero.
     */
    append_field(body,&pos,
        ",%d,%d,%d,%d,%d,%d,%d",
        prn,
        eph->svh,
        0,      /* ISF */
        1,      /* reserved[0]: CNAV */
        0,0,0);

    append_field(body,&pos,
        ",%.12e,%d,%d,%.12e,%.12e,%.12e,%.12e",
        eph->top,
        (int)eph->wn_op,
        (int)eph->urai_ed,
        eph->urai_ned[0],
        eph->urai_ned[1],
        eph->urai_ned[2],
        toe);

    append_field(body,&pos,
        ",%d,%d,%.12e,%.12e,%.12e,%.12e,%.12e,%.12e",
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
        ",%.12e,%.12e,%.12e,%.12e,%.12e,%.12e,%.12e",
        eph->cis,eph->i0,eph->idot,
        eph->OMG0,eph->OMGd,
        toc,eph->tgd[0]);

    /* Unicore order differs from RTKLIB isc[] order. */
    append_field(body,&pos,
        ",%.12e,%.12e,%.12e,%.12e,%.12e,%.12e",
        eph->isc[5],eph->isc[4],eph->isc[0],
        eph->isc[1],eph->isc[2],eph->isc[3]);

    append_field(body,&pos,
        ",%.12e,%.12e,%.12e,0",
        eph->f0,eph->f1,eph->f2);

    crc=unicore_crc32(body+1,pos-1);

    /* Use '\n' with a text-mode stream. MSVC translates it to CRLF once. */
    fprintf(fp,"%s*%08x\n",body,crc);
    return 1;
}
