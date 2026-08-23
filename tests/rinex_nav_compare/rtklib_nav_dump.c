/*
 * Public-API NAV exporter for the Issue #1 comparison harness.
 *
 * The program deliberately calls readrnx(); it does not contain a second
 * RINEX parser.  It emits one CSV row per stored NAV field so the Python
 * comparator can align the RTKLIB representation with raw fixed-width fields.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../src/rtklib.h"

static int compact_dump=0;

static int keep_compact_field(const char *record, const char *field)
{
    static const char *eph[] = {
        "sat","iode","iodc","sva","svh","week","code","flag","ttr",
        "sqrt_A","e","i0","OMG0","omg","M0","deln","OMGd","idot",
        "crc","crs","cuc","cus","cic","cis","toes","fit","f0","f1","f2",
        "Adot","ndot","delta_n0","top","delta_n0_dot","urai_ned","urai_ed","wn_op",
        "int_flag","tgd","isc","sisai",NULL
    };
    static const char *geph[] = {
        "sat","iode","frq","svh","sva","age","flag","svhflag","tof",
        "taun","gamn","dtaun","pos","vel","acc",NULL
    };
    static const char *seph[] = {
        "sat","svh","sva","iodn","tof","af0","af1",NULL
    };
    const char **list=strcmp(record,"EPH")==0?eph:
                     strcmp(record,"ION")==0?(const char *[]){"data",NULL}:
                     strcmp(record,"EOP")==0?(const char *[]){"x","dx","dx2","y","dy","dy2","ttr","ut","dut","dut2",NULL}:
                     (const char *[]){"corr_type","corr_id","trans_time","a0","a1","a2",NULL};
    int i;
    if (strcmp(record,"EPH")==0 && field[0]=='t') {
        /* GLONASS/SBAS use tof; GPS-family ttr is retained above. */
        if (strcmp(field,"tof")==0) list=geph;
    }
    if (strcmp(record,"EPH")==0 && (strcmp(field,"af0")==0 || strcmp(field,"af1")==0)) list=seph;
    for (i=0;list[i];i++) if (!strcmp(list[i],field)) return 1;
    return 0;
}

static void csv(const char *s)
{
    const char *p;
    putchar('"');
    for (p=s;*p;p++) {
        if (*p=='"') putchar('"');
        putchar(*p);
    }
    putchar('"');
}

static void time_text(gtime_t t, char *out)
{
    if (t.time==0 && t.sec==0.0) {
        strcpy(out,"");
    }
    else {
        time2str2(t,out,3);
    }
}

static void row(const char *file, const char *record, int sys, int prn,
                int msg, const char *subtype, int index, gtime_t epoch,
                const char *field, int slot, double value, int present)
{
    char sysid[8]="", msgid[8]="", epoch_text[64]="";
    if (compact_dump && !keep_compact_field(record,field)) return;
    sysstr2(sys,sysid);
    navmsgstr(msg,msgid);
    if (!msgid[0]) strcpy(msgid,"LEGACY");
    time_text(epoch,epoch_text);
    csv(file); printf(",%s,%s,%d,%s,%s,%d,%s,%s,%d,%.17g,%d,,\n",
        record,sysid,prn,msgid,subtype?subtype:"",index,epoch_text,
        field,slot,value,present);
}

static void text_row(const char *file, const char *record, int sys, int prn,
                     int msg, const char *subtype, int index, gtime_t epoch,
                     const char *field, const char *value, int present)
{
    char sysid[8]="", msgid[8]="", epoch_text[64]="";
    if (compact_dump && !keep_compact_field(record,field)) return;
    sysstr2(sys,sysid);
    navmsgstr(msg,msgid);
    if (!msgid[0]) strcpy(msgid,"LEGACY");
    time_text(epoch,epoch_text);
    csv(file); printf(",%s,%s,%d,%s,%s,%d,%s,%s,-1,,%d,,",
        record,sysid,prn,msgid,subtype?subtype:"",index,epoch_text,field,present);
    csv(value);
    putchar('\n');
}

static void record_info(int sat, const nav_data_hdr_t *hdr, int *sys, int *prn)
{
    int p=0;
    *sys=hdr->sys?hdr->sys:satsys(sat,&p);
    *prn=hdr->prn?hdr->prn:p;
}

#define E(file,rec,sys,prn,msg,sub,idx,epoch,name,slot,val,pres) \
    row(file,rec,sys,prn,msg,sub,idx,epoch,name,slot,(double)(val),pres)

static void dump_eph(const char *file, int i, const eph_t *e)
{
    int sys,prn;
    double ttr;
    record_info(e->sat,&e->hdr,&sys,&prn);
    /* eph_t stores BDS transmission time in GPST; convert to BDT before
     * exporting the canonical RINEX field, matching rinex.c's writer. */
    ttr=sys==SYS_CMP?time2bdt(gpst2bdt(e->ttr),NULL):time2gpst(e->ttr,NULL);
    E(file,"EPH",sys,prn,e->hdr.msg_type,e->hdr.subtype,i,e->toc,"sat",-1,e->sat,1);
    E(file,"EPH",sys,prn,e->hdr.msg_type,e->hdr.subtype,i,e->toc,"iode",-1,e->iode,1);
    E(file,"EPH",sys,prn,e->hdr.msg_type,e->hdr.subtype,i,e->toc,"iodc",-1,e->iodc,1);
    E(file,"EPH",sys,prn,e->hdr.msg_type,e->hdr.subtype,i,e->toc,"sva",-1,e->sva,1);
    E(file,"EPH",sys,prn,e->hdr.msg_type,e->hdr.subtype,i,e->toc,"svh",-1,e->svh,1);
    E(file,"EPH",sys,prn,e->hdr.msg_type,e->hdr.subtype,i,e->toc,"week",-1,e->week,1);
    E(file,"EPH",sys,prn,e->hdr.msg_type,e->hdr.subtype,i,e->toc,"code",-1,e->code,1);
    E(file,"EPH",sys,prn,e->hdr.msg_type,e->hdr.subtype,i,e->toc,"flag",-1,e->flag,1);
    E(file,"EPH",sys,prn,e->hdr.msg_type,e->hdr.subtype,i,e->toc,"toc",-1,time2gpst(e->toc,NULL),1);
    E(file,"EPH",sys,prn,e->hdr.msg_type,e->hdr.subtype,i,e->toc,"toe",-1,time2gpst(e->toe,NULL),1);
    E(file,"EPH",sys,prn,e->hdr.msg_type,e->hdr.subtype,i,e->toc,"ttr",-1,ttr,1);
    E(file,"EPH",sys,prn,e->hdr.msg_type,e->hdr.subtype,i,e->toc,"A",-1,e->A,1);
    E(file,"EPH",sys,prn,e->hdr.msg_type,e->hdr.subtype,i,e->toc,"sqrt_A",-1,sqrt(fabs(e->A)),1);
    E(file,"EPH",sys,prn,e->hdr.msg_type,e->hdr.subtype,i,e->toc,"e",-1,e->e,1);
    E(file,"EPH",sys,prn,e->hdr.msg_type,e->hdr.subtype,i,e->toc,"i0",-1,e->i0,1);
    E(file,"EPH",sys,prn,e->hdr.msg_type,e->hdr.subtype,i,e->toc,"OMG0",-1,e->OMG0,1);
    E(file,"EPH",sys,prn,e->hdr.msg_type,e->hdr.subtype,i,e->toc,"omg",-1,e->omg,1);
    E(file,"EPH",sys,prn,e->hdr.msg_type,e->hdr.subtype,i,e->toc,"M0",-1,e->M0,1);
    E(file,"EPH",sys,prn,e->hdr.msg_type,e->hdr.subtype,i,e->toc,"deln",-1,e->deln,1);
    E(file,"EPH",sys,prn,e->hdr.msg_type,e->hdr.subtype,i,e->toc,"OMGd",-1,e->OMGd,1);
    E(file,"EPH",sys,prn,e->hdr.msg_type,e->hdr.subtype,i,e->toc,"idot",-1,e->idot,1);
    E(file,"EPH",sys,prn,e->hdr.msg_type,e->hdr.subtype,i,e->toc,"crc",-1,e->crc,1);
    E(file,"EPH",sys,prn,e->hdr.msg_type,e->hdr.subtype,i,e->toc,"crs",-1,e->crs,1);
    E(file,"EPH",sys,prn,e->hdr.msg_type,e->hdr.subtype,i,e->toc,"cuc",-1,e->cuc,1);
    E(file,"EPH",sys,prn,e->hdr.msg_type,e->hdr.subtype,i,e->toc,"cus",-1,e->cus,1);
    E(file,"EPH",sys,prn,e->hdr.msg_type,e->hdr.subtype,i,e->toc,"cic",-1,e->cic,1);
    E(file,"EPH",sys,prn,e->hdr.msg_type,e->hdr.subtype,i,e->toc,"cis",-1,e->cis,1);
    E(file,"EPH",sys,prn,e->hdr.msg_type,e->hdr.subtype,i,e->toc,"toes",-1,e->toes,1);
    E(file,"EPH",sys,prn,e->hdr.msg_type,e->hdr.subtype,i,e->toc,"fit",-1,e->fit,1);
    E(file,"EPH",sys,prn,e->hdr.msg_type,e->hdr.subtype,i,e->toc,"f0",-1,e->f0,1);
    E(file,"EPH",sys,prn,e->hdr.msg_type,e->hdr.subtype,i,e->toc,"f1",-1,e->f1,1);
    E(file,"EPH",sys,prn,e->hdr.msg_type,e->hdr.subtype,i,e->toc,"f2",-1,e->f2,1);
    E(file,"EPH",sys,prn,e->hdr.msg_type,e->hdr.subtype,i,e->toc,"Adot",-1,e->Adot,1);
    E(file,"EPH",sys,prn,e->hdr.msg_type,e->hdr.subtype,i,e->toc,"ndot",-1,e->ndot,1);
    E(file,"EPH",sys,prn,e->hdr.msg_type,e->hdr.subtype,i,e->toc,"delta_n0",-1,e->delta_n0,1);
    E(file,"EPH",sys,prn,e->hdr.msg_type,e->hdr.subtype,i,e->toc,"top",-1,e->top,1);
    E(file,"EPH",sys,prn,e->hdr.msg_type,e->hdr.subtype,i,e->toc,"delta_n0_dot",-1,e->delta_n0_dot,1);
    E(file,"EPH",sys,prn,e->hdr.msg_type,e->hdr.subtype,i,e->toc,"urai_ned",0,e->urai_ned[0],1);
    E(file,"EPH",sys,prn,e->hdr.msg_type,e->hdr.subtype,i,e->toc,"urai_ned",1,e->urai_ned[1],1);
    E(file,"EPH",sys,prn,e->hdr.msg_type,e->hdr.subtype,i,e->toc,"urai_ned",2,e->urai_ned[2],1);
    E(file,"EPH",sys,prn,e->hdr.msg_type,e->hdr.subtype,i,e->toc,"urai_ed",-1,e->urai_ed,1);
    E(file,"EPH",sys,prn,e->hdr.msg_type,e->hdr.subtype,i,e->toc,"wn_op",-1,e->wn_op,1);
    E(file,"EPH",sys,prn,e->hdr.msg_type,e->hdr.subtype,i,e->toc,"int_flag",-1,e->int_flag,1);
    E(file,"EPH",sys,prn,e->hdr.msg_type,e->hdr.subtype,i,e->toc,"tgd",0,e->tgd[0],1);
    E(file,"EPH",sys,prn,e->hdr.msg_type,e->hdr.subtype,i,e->toc,"tgd",1,e->tgd[1],1);
    E(file,"EPH",sys,prn,e->hdr.msg_type,e->hdr.subtype,i,e->toc,"tgd",2,e->tgd[2],1);
    E(file,"EPH",sys,prn,e->hdr.msg_type,e->hdr.subtype,i,e->toc,"tgd",3,e->tgd[3],1);
    E(file,"EPH",sys,prn,e->hdr.msg_type,e->hdr.subtype,i,e->toc,"isc",0,e->isc[0],1);
    E(file,"EPH",sys,prn,e->hdr.msg_type,e->hdr.subtype,i,e->toc,"isc",1,e->isc[1],1);
    E(file,"EPH",sys,prn,e->hdr.msg_type,e->hdr.subtype,i,e->toc,"isc",2,e->isc[2],1);
    E(file,"EPH",sys,prn,e->hdr.msg_type,e->hdr.subtype,i,e->toc,"isc",3,e->isc[3],1);
    E(file,"EPH",sys,prn,e->hdr.msg_type,e->hdr.subtype,i,e->toc,"isc",4,e->isc[4],1);
    E(file,"EPH",sys,prn,e->hdr.msg_type,e->hdr.subtype,i,e->toc,"isc",5,e->isc[5],1);
    E(file,"EPH",sys,prn,e->hdr.msg_type,e->hdr.subtype,i,e->toc,"sisai",0,e->sisai[0],1);
    E(file,"EPH",sys,prn,e->hdr.msg_type,e->hdr.subtype,i,e->toc,"sisai",1,e->sisai[1],1);
    E(file,"EPH",sys,prn,e->hdr.msg_type,e->hdr.subtype,i,e->toc,"sisai",2,e->sisai[2],1);
    E(file,"EPH",sys,prn,e->hdr.msg_type,e->hdr.subtype,i,e->toc,"sisai",3,e->sisai[3],1);
}

static void dump_geph(const char *file, int i, const geph_t *e)
{
    int sys,prn;
    record_info(e->sat,&e->hdr,&sys,&prn);
    E(file,"EPH",sys,prn,e->hdr.msg_type,e->hdr.subtype,i,e->toe,"sat",-1,e->sat,1);
    E(file,"EPH",sys,prn,e->hdr.msg_type,e->hdr.subtype,i,e->toe,"iode",-1,e->iode,1);
    E(file,"EPH",sys,prn,e->hdr.msg_type,e->hdr.subtype,i,e->toe,"frq",-1,e->frq,1);
    E(file,"EPH",sys,prn,e->hdr.msg_type,e->hdr.subtype,i,e->toe,"svh",-1,e->svh,1);
    E(file,"EPH",sys,prn,e->hdr.msg_type,e->hdr.subtype,i,e->toe,"sva",-1,e->sva,1);
    E(file,"EPH",sys,prn,e->hdr.msg_type,e->hdr.subtype,i,e->toe,"age",-1,e->age,1);
    E(file,"EPH",sys,prn,e->hdr.msg_type,e->hdr.subtype,i,e->toe,"flag",-1,e->flag,1);
    E(file,"EPH",sys,prn,e->hdr.msg_type,e->hdr.subtype,i,e->toe,"svhflag",-1,e->svhflag,1);
    E(file,"EPH",sys,prn,e->hdr.msg_type,e->hdr.subtype,i,e->toe,"toe",-1,time2gpst(e->toe,NULL),1);
    E(file,"EPH",sys,prn,e->hdr.msg_type,e->hdr.subtype,i,e->toe,"tof",-1,time2gpst(e->tof,NULL),1);
    E(file,"EPH",sys,prn,e->hdr.msg_type,e->hdr.subtype,i,e->toe,"taun",-1,e->taun,1);
    E(file,"EPH",sys,prn,e->hdr.msg_type,e->hdr.subtype,i,e->toe,"gamn",-1,e->gamn,1);
    E(file,"EPH",sys,prn,e->hdr.msg_type,e->hdr.subtype,i,e->toe,"dtaun",-1,e->dtaun,1);
    E(file,"EPH",sys,prn,e->hdr.msg_type,e->hdr.subtype,i,e->toe,"pos",0,e->pos[0],1);
    E(file,"EPH",sys,prn,e->hdr.msg_type,e->hdr.subtype,i,e->toe,"pos",1,e->pos[1],1);
    E(file,"EPH",sys,prn,e->hdr.msg_type,e->hdr.subtype,i,e->toe,"pos",2,e->pos[2],1);
    E(file,"EPH",sys,prn,e->hdr.msg_type,e->hdr.subtype,i,e->toe,"vel",0,e->vel[0],1);
    E(file,"EPH",sys,prn,e->hdr.msg_type,e->hdr.subtype,i,e->toe,"vel",1,e->vel[1],1);
    E(file,"EPH",sys,prn,e->hdr.msg_type,e->hdr.subtype,i,e->toe,"vel",2,e->vel[2],1);
    E(file,"EPH",sys,prn,e->hdr.msg_type,e->hdr.subtype,i,e->toe,"acc",0,e->acc[0],1);
    E(file,"EPH",sys,prn,e->hdr.msg_type,e->hdr.subtype,i,e->toe,"acc",1,e->acc[1],1);
    E(file,"EPH",sys,prn,e->hdr.msg_type,e->hdr.subtype,i,e->toe,"acc",2,e->acc[2],1);
}

static void dump_seph(const char *file, int i, const seph_t *e)
{
    int sys,prn;
    record_info(e->sat,&e->hdr,&sys,&prn);
    E(file,"EPH",sys,prn,e->hdr.msg_type,e->hdr.subtype,i,e->t0,"sat",-1,e->sat,1);
    E(file,"EPH",sys,prn,e->hdr.msg_type,e->hdr.subtype,i,e->t0,"svh",-1,e->svh,1);
    E(file,"EPH",sys,prn,e->hdr.msg_type,e->hdr.subtype,i,e->t0,"sva",-1,e->sva,1);
    E(file,"EPH",sys,prn,e->hdr.msg_type,e->hdr.subtype,i,e->t0,"iodn",-1,e->iodn,1);
    E(file,"EPH",sys,prn,e->hdr.msg_type,e->hdr.subtype,i,e->t0,"t0",-1,time2gpst(e->t0,NULL),1);
    E(file,"EPH",sys,prn,e->hdr.msg_type,e->hdr.subtype,i,e->t0,"tof",-1,time2gpst(e->tof,NULL),1);
    E(file,"EPH",sys,prn,e->hdr.msg_type,e->hdr.subtype,i,e->t0,"af0",-1,e->af0,1);
    E(file,"EPH",sys,prn,e->hdr.msg_type,e->hdr.subtype,i,e->t0,"af1",-1,e->af1,1);
}

static void dump_ion(const char *file, int i, const ion_t *v)
{
    int j;
    E(file,"ION",v->hdr.sys,v->hdr.prn,v->hdr.msg_type,v->hdr.subtype,i,v->trans_time,"trans_time",-1,time2gpst(v->trans_time,NULL),1);
    for (j=0;j<v->ndata && j<32;j++)
        E(file,"ION",v->hdr.sys,v->hdr.prn,v->hdr.msg_type,v->hdr.subtype,i,v->trans_time,"data",j,v->data[j],v->present[j]);
    for (j=0;j<9;j++)
        E(file,"ION",v->hdr.sys,v->hdr.prn,v->hdr.msg_type,v->hdr.subtype,i,v->trans_time,"alpha",j,v->alpha[j],1);
    E(file,"ION",v->hdr.sys,v->hdr.prn,v->hdr.msg_type,v->hdr.subtype,i,v->trans_time,"region",-1,v->region,1);
}

static void dump_eop(const char *file, int i, const eop_t *v)
{
    double ttr;
    int week;
    if (v->hdr.sys==SYS_CMP) ttr=time2bdt(v->ttr,&week);
    else ttr=time2gpst(v->ttr,&week);
    E(file,"EOP",v->hdr.sys,v->hdr.prn,v->hdr.msg_type,v->hdr.subtype,i,v->ref_time,"x",-1,v->x,v->present[0]);
    E(file,"EOP",v->hdr.sys,v->hdr.prn,v->hdr.msg_type,v->hdr.subtype,i,v->ref_time,"dx",-1,v->dx,v->present[1]);
    E(file,"EOP",v->hdr.sys,v->hdr.prn,v->hdr.msg_type,v->hdr.subtype,i,v->ref_time,"dx2",-1,v->dx2,v->present[2]);
    E(file,"EOP",v->hdr.sys,v->hdr.prn,v->hdr.msg_type,v->hdr.subtype,i,v->ref_time,"y",-1,v->y,v->present[3]);
    E(file,"EOP",v->hdr.sys,v->hdr.prn,v->hdr.msg_type,v->hdr.subtype,i,v->ref_time,"dy",-1,v->dy,v->present[4]);
    E(file,"EOP",v->hdr.sys,v->hdr.prn,v->hdr.msg_type,v->hdr.subtype,i,v->ref_time,"dy2",-1,v->dy2,v->present[5]);
    E(file,"EOP",v->hdr.sys,v->hdr.prn,v->hdr.msg_type,v->hdr.subtype,i,v->ref_time,"ttr",-1,ttr,v->present[6]);
    E(file,"EOP",v->hdr.sys,v->hdr.prn,v->hdr.msg_type,v->hdr.subtype,i,v->ref_time,"ut",-1,v->ut,v->present[7]);
    E(file,"EOP",v->hdr.sys,v->hdr.prn,v->hdr.msg_type,v->hdr.subtype,i,v->ref_time,"dut",-1,v->dut,v->present[8]);
    E(file,"EOP",v->hdr.sys,v->hdr.prn,v->hdr.msg_type,v->hdr.subtype,i,v->ref_time,"dut2",-1,v->dut2,v->present[9]);
}

static void dump_sto(const char *file, int i, const sto_t *v)
{
    text_row(file,"STO",v->hdr.sys,v->hdr.prn,v->hdr.msg_type,v->hdr.subtype,i,v->ref_time,"corr_type",v->corr_type,1);
    text_row(file,"STO",v->hdr.sys,v->hdr.prn,v->hdr.msg_type,v->hdr.subtype,i,v->ref_time,"corr_id",v->corr_id,1);
    E(file,"STO",v->hdr.sys,v->hdr.prn,v->hdr.msg_type,v->hdr.subtype,i,v->ref_time,"trans_time",-1,v->trans_time,v->present[0]);
    E(file,"STO",v->hdr.sys,v->hdr.prn,v->hdr.msg_type,v->hdr.subtype,i,v->ref_time,"a0",-1,v->a0,v->present[1]);
    E(file,"STO",v->hdr.sys,v->hdr.prn,v->hdr.msg_type,v->hdr.subtype,i,v->ref_time,"a1",-1,v->a1,v->present[2]);
    E(file,"STO",v->hdr.sys,v->hdr.prn,v->hdr.msg_type,v->hdr.subtype,i,v->ref_time,"a2",-1,v->a2,v->present[3]);
}

int main(int argc, char **argv)
{
    const char *file;
    obs_t obs={0};
    nav_t nav={0};
    sta_t sta={""};
    int i,stat;

    if (argc<2 || argc>3) {
        fprintf(stderr,"usage: %s NAV_FILE [compact]\n",argv[0]);
        return 2;
    }
    compact_dump=argc==3 && !strcmp(argv[2],"compact");
    file=argv[1];
    stat=readrnx(file,1,"",&obs,&nav,&sta);
    if (!stat) {
        fprintf(stderr,"readrnx failed: %s\n",file);
        return 1;
    }
    printf("file,record_type,system,prn,message_type,subtype,record_index,epoch,field_name,field_index,value,presence_rtklib,source_location,text_value\n");
    for (i=0;i<nav.n;i++) dump_eph(file,i,nav.eph+i);
    for (i=0;i<nav.ng;i++) dump_geph(file,i,nav.geph+i);
    for (i=0;i<nav.ns;i++) dump_seph(file,i,nav.seph+i);
    for (i=0;i<nav.nion;i++) dump_ion(file,i,nav.ion+i);
    for (i=0;i<nav.neop;i++) dump_eop(file,i,nav.eop+i);
    for (i=0;i<nav.nsto;i++) dump_sto(file,i,nav.sto+i);

    free(obs.data);
    freenav(&nav,0x3ff);
    return 0;
}
