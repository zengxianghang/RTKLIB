#include "rtklib.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static void fields4(FILE *fp, double a, double b, double c, double d)
{
    fprintf(fp,"    %19.12E%19.12E%19.12E%19.12E\n",a,b,c,d);
}

int main(void)
{
    const char *path="rinex4_glo_l3oc_fixture.rnx";
    FILE *fp=fopen(path,"w");
    nav_t nav={0};
    geph_t *g;
    int stat,ok=1;

    if (!fp) return 1;
    fprintf(fp,"%9.2f%-11s%-20s%-20s%-20s\n",4.02,"","N: GNSS NAV DATA","M: Mixed","RINEX VERSION / TYPE");
    fprintf(fp,"%60s%-20s\n","","END OF HEADER");

    fprintf(fp,"> EPH R26 L3OC\n");
    fprintf(fp,"R26 2026 08 27 10 15 00%19.12E%19.12E%19.12E\n",-2E-5,1E-10,3E-13);
    fields4(fp,19100.0,0.0,0.0,0.0);
    fields4(fp,0.0,3.0,0.0,0.0);
    fields4(fp,15000.0,0.0,0.0,2.5E-8);
    fields4(fp,2.0,3.0,1.0,2.0);
    fields4(fp,0.0,36000.0,1.0,2.0);
    fields4(fp,0.1,0.0,0.01,0.001);
    fields4(fp,0.1,0.5,-0.25,1.0);
    fprintf(fp,"    %19.12E%19.12E%19s%19.12E\n",2.0,3.0,"",382500.0);

    fprintf(fp,"> EPH R27 FDMA\n");
    fprintf(fp,"R27 2026 08 27 10 30 00%19.12E%19.12E%19.12E\n",-1E-4,2E-10,383400.0);
    fields4(fp,19000.0,0.0,0.0,0.0);
    fields4(fp,1000.0,2.5,0.0,-4.0);
    fields4(fp,14000.0,0.0,0.0,5.0);
    fields4(fp,0.0,1E-8,2.0,0.0);
    fclose(fp);

    stat=readrnx(path,1,"",NULL,&nav,NULL);
    remove(path);
    if (!stat||nav.ng!=2) {
        fprintf(stderr,"parser alignment failed: stat=%d ng=%d\n",stat,nav.ng);
        free(nav.geph);
        return 1;
    }

    g=&nav.geph[0];
    if (g->hdr.msg_type!=NAV_L3OC||fabs(g->beta-3E-13)>1E-18||
        fabs(g->isc_l3ocp-2.5E-8)>1E-15||g->data_validity!=0||
        fabs(g->pc[0]-0.5)>1E-12||fabs(g->pc[1]+0.25)>1E-12||
        fabs(g->pc[2]-1.0)>1E-12||fabs(g->ttm-382500.0)>1E-9) {
        fprintf(stderr,"L3OC fields decoded incorrectly\n");
        ok=0;
    }
    g=&nav.geph[1];
    if (g->hdr.msg_type!=NAV_FDMA||g->frq!=-4) {
        fprintf(stderr,"record following L3OC was not aligned/decoded\n");
        ok=0;
    }
    free(nav.geph);
    if (!ok) return 1;
    puts("rinex4_glo_l3oc: PASS");
    return 0;
}
