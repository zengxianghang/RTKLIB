#include "rtklib.h"
#include "unicore_gpscnav.h"
#include <stdio.h>
#include <string.h>

#define MAX_INPUT_FILE 256

static void print_help(void)
{
    printf("Usage:\n");
    printf("  rnx2unicore -i <rinex_nav> [-i <rinex_nav> ...] -o <output>\n");
}

int main(int argc, char **argv)
{
    char *input[MAX_INPUT_FILE];
    int ninput=0;
    char output[1024]="gpscnav.log";
    nav_t nav={0};
    obs_t obs={0};
    sta_t sta={0};
    FILE *fp;
    int i;

    for (i=1;i<argc;i++) {
        if (!strcmp(argv[i],"-i") && i+1<argc) {
            if (ninput<MAX_INPUT_FILE) input[ninput++]=argv[++i];
        }
        else if (!strcmp(argv[i],"-o") && i+1<argc) {
            strcpy(output,argv[++i]);
        }
        else if (!strcmp(argv[i],"-h")) {
            print_help();
            return 0;
        }
    }

    if (ninput==0) {
        print_help();
        return -1;
    }

    for (i=0;i<ninput;i++) {
        if (!readrnx(input[i],0,"",&obs,&nav,&sta)) {
            fprintf(stderr,"failed to read %s\n",input[i]);
        }
    }

    fp=fopen(output,"w");
    if (!fp) return -1;

    for (i=0;i<nav.n;i++) {
        eph_t *eph=&nav.eph[i];
        if (satsys(eph->sat,NULL)!=SYS_GPS) continue;
        if (!(eph->code&NAV_CNAV) && !(eph->code&NAV_CNV2)) continue;
        write_unicore_gpscnav_eph(fp,eph);
    }

    fclose(fp);
    free(nav.eph);
    return 0;
}
