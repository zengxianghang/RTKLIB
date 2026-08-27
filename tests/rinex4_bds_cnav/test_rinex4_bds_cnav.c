#include "rtklib.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static void fields4(FILE *fp, double a, double b, double c, double d)
{
    fprintf(fp,"    %19.12E%19.12E%19.12E%19.12E\n",a,b,c,d);
}

static void write_common_orbit(FILE *fp, double toe)
{
    fields4(fp,1E-3,10.0,1E-9,0.1);
    fields4(fp,1E-6,0.01,2E-6,5282.6);
    fields4(fp,toe,1E-7,1.0,2E-7);
    fields4(fp,0.95,100.0,0.5,-1E-9);
    fields4(fp,1E-10,2E-14,3.0,toe);
    fields4(fp,1.0,2.0,3.0,4.0);
}

static const eph_t *find_family(const nav_t *nav, int sat, int type)
{
    int i;
    for (i=0;i<nav->n;i++) {
        if (nav->eph[i].sat==sat&&nav->eph[i].hdr.msg_type==type) return nav->eph+i;
    }
    return NULL;
}

static int check_time(const char *name, const eph_t *eph)
{
    int bdt_week=0;
    double bdt_sow;
    gtime_t toc_bdt;
    if (!eph) {
        fprintf(stderr,"%s record missing\n",name);
        return 0;
    }
    toc_bdt=gpst2bdt(eph->toc);
    bdt_sow=time2bdt(toc_bdt,&bdt_week);
    if (eph->week!=991||bdt_week!=991||fabs(bdt_sow-435600.0)>1E-6||
        fabs(timediff(eph->toe,eph->toc))>1E-6) {
        fprintf(stderr,"%s time decode failed: eph_week=%d bdt_week=%d sow=%.3f toe_minus_toc=%.3f\n",
                name,eph->week,bdt_week,bdt_sow,timediff(eph->toe,eph->toc));
        return 0;
    }
    return 1;
}

int main(void)
{
    const char *path="rinex4_bds_cnav_fixture.rnx";
    const double toe=435600.0;
    FILE *fp=fopen(path,"w");
    nav_t nav={0};
    const eph_t *cnv1,*cnv2,*cnv3;
    int c22=satno(SYS_CMP,22),c24=satno(SYS_CMP,24),stat,ok=1;

    if (!fp||!c22||!c24) return 1;
    fprintf(fp,"%9.2f%-11s%-20s%-20s%-20s\n",4.02,"","N: GNSS NAV DATA","M: Mixed","RINEX VERSION / TYPE");
    fprintf(fp,"%60s%-20s\n","","END OF HEADER");

    fprintf(fp,"> EPH C22 CNV1\n");
    fprintf(fp,"C22 2025 01 03 01 00 00%19.12E%19.12E%19.12E\n",1E-4,2E-12,0.0);
    write_common_orbit(fp,toe);
    fields4(fp,-1E-9,0.0,2E-9,3E-9);
    fields4(fp,1.0,0.0,0.0,18.0);
    fields4(fp,toe+10.0,0.0,0.0,18.0);

    fprintf(fp,"> EPH C22 CNV2\n");
    fprintf(fp,"C22 2025 01 03 01 00 00%19.12E%19.12E%19.12E\n",2E-4,3E-12,0.0);
    write_common_orbit(fp,toe);
    fields4(fp,0.0,-4E-9,5E-9,6E-9);
    fields4(fp,2.0,0.0,0.0,19.0);
    fields4(fp,toe+20.0,0.0,0.0,19.0);

    fprintf(fp,"> EPH C24 CNV3\n");
    fprintf(fp,"C24 2025 01 03 01 00 00%19.12E%19.12E%19.12E\n",3E-4,4E-12,0.0);
    write_common_orbit(fp,toe);
    fields4(fp,3.0,0.0,0.0,7E-9);
    fields4(fp,toe+30.0,0.0,0.0,0.0);
    fclose(fp);

    stat=readrnx(path,1,"",NULL,&nav,NULL);
    remove(path);
    if (!stat) {
        fprintf(stderr,"RINEX4 BDS CNAV fixture was not read\n");
        free(nav.eph);
        return 1;
    }
    uniqnav(&nav);
    cnv1=find_family(&nav,c22,NAV_CNV1);
    cnv2=find_family(&nav,c22,NAV_CNV2);
    cnv3=find_family(&nav,c24,NAV_CNV3);

    ok&=check_time("CNV1",cnv1);
    ok&=check_time("CNV2",cnv2);
    ok&=check_time("CNV3",cnv3);
    if (cnv1&&(fabs(cnv1->isc[0]+1E-9)>1E-15||fabs(cnv1->tgd[0]-2E-9)>1E-15||
               fabs(cnv1->tgd[1]-3E-9)>1E-15)) {
        fprintf(stderr,"CNV1 ISC/TGD fields decoded incorrectly\n"); ok=0;
    }
    if (cnv2&&(fabs(cnv2->isc[0]+4E-9)>1E-15||fabs(cnv2->tgd[0]-5E-9)>1E-15||
               fabs(cnv2->tgd[1]-6E-9)>1E-15)) {
        fprintf(stderr,"CNV2 ISC/TGD fields decoded incorrectly\n"); ok=0;
    }
    if (cnv3&&fabs(cnv3->tgd[0]-7E-9)>1E-15) {
        fprintf(stderr,"CNV3 TGD field decoded incorrectly\n"); ok=0;
    }
    free(nav.eph);
    if (!ok) return 1;
    puts("rinex4_bds_cnav: PASS");
    return 0;
}
