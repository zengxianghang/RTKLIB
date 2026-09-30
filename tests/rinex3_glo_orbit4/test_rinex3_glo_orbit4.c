/* GLONASS BROADCAST ORBIT 4 (status flags, L1/L2 group delay difference,
 * URAI, health flags) exists only from RINEX 3.05.  A RINEX 3.04 record must
 * leave those fields zero instead of reading unset data, and a RINEX 3.05
 * record must consume its fourth orbit line. */
#include "rtklib.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static const char *PATH304="rinex3_glo_orbit4_304.rnx";
static const char *PATH305="rinex3_glo_orbit4_305.rnx";

static void header(FILE *fp, double ver)
{
    fprintf(fp,"%9.2f%-11s%-20s%-20s%-20s\n",ver,"","N: GNSS NAV DATA","M","RINEX VERSION / TYPE");
    fprintf(fp,"%60s%-20s\n","","END OF HEADER");
}

static void values(FILE *fp, const double *v, int n)
{
    int i;
    for (i=0;i<n;i++) {
        if (i%4==0) fprintf(fp,"\n    ");
        fprintf(fp,"%19.12E",v[i]);
    }
}

/* A GPS record whose orbit fields are all non-zero (Crs 285.53125 m). */
static void gps_record(FILE *fp)
{
    double v[28];
    int i;
    for (i=0;i<28;i++) v[i]=285.53125+i;
    fprintf(fp,"G01 2023 03 14 00 00 00%19.12E%19.12E%19.12E",1.0E-4,1.0E-12,0.0);
    values(fp,v,28);
    fprintf(fp,"\n");
}

static void glo_record(FILE *fp, const char *id, const char *epoch, int orbit4)
{
    const double orbit[12]={5763.751464844,-1.299090385437,0.0,0.0,
                            11834.32617188,2.693783760071,-9.313225746155E-10,1.0,
                            21858.87109375,-1.114941596985,-2.793967723846E-09,0.0};
    const double extra[4]={176.0,-2.793967723846E-09,3.0,0.0};
    fprintf(fp,"%s %s%19.12E%19.12E%19.12E",id,epoch,2.470612525940E-05,0.0,172830.0);
    values(fp,orbit,12);
    if (orbit4) values(fp,extra,4);
    fprintf(fp,"\n");
}

static int read_nav(const char *path, nav_t *nav)
{
    obs_t obs={0};
    sta_t sta={{0}};
    memset(nav,0,sizeof(*nav));
    return readrnx(path,1,"",&obs,nav,&sta);
}

int main(void)
{
    nav_t nav;
    FILE *fp;
    int failures=0;
    const geph_t *geph;

    fp=fopen(PATH304,"w");
    header(fp,3.04);
    gps_record(fp);
    glo_record(fp,"R01","2023 03 14 00 15 00",0);
    fclose(fp);
    if (!read_nav(PATH304,&nav)||nav.n!=1||nav.ng!=1) {
        fprintf(stderr,"3.04: n=%d ng=%d\n",nav.n,nav.ng);
        return 1;
    }
    geph=nav.geph;
    if (geph->dtaun!=0.0||geph->sva!=0||geph->flag!=0||geph->svhflag!=0) {
        fprintf(stderr,"3.04: orbit-4 fields not zero: dtaun=%g sva=%d flag=%d svhflag=%d\n",
                geph->dtaun,geph->sva,geph->flag,geph->svhflag);
        failures++;
    }
    if (fabs(geph->taun+2.470612525940E-05)>1E-18||geph->frq!=1) {
        fprintf(stderr,"3.04: taun=%g frq=%d\n",geph->taun,geph->frq);
        failures++;
    }
    freenav(&nav,0xFF);

    fp=fopen(PATH305,"w");
    header(fp,3.05);
    glo_record(fp,"R01","2023 03 14 00 15 00",1);
    glo_record(fp,"R02","2023 03 14 00 15 00",1);
    fclose(fp);
    if (!read_nav(PATH305,&nav)||nav.ng!=2) {
        fprintf(stderr,"3.05: ng=%d, expected both records\n",nav.ng);
        return 1;
    }
    geph=nav.geph;
    if (geph->flag!=176||geph->dtaun!=-2.793967723846E-09||geph->sva!=3||geph->svhflag!=0) {
        fprintf(stderr,"3.05: flag=%d dtaun=%g sva=%d svhflag=%d\n",
                geph->flag,geph->dtaun,geph->sva,geph->svhflag);
        failures++;
    }
    freenav(&nav,0xFF);

    if (failures) return 1;
    printf("rinex3_glo_orbit4: OK\n");
    return 0;
}
