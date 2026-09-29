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

static int is_gps_cnav(const eph_t *eph)
{
    if (!eph) return 0;
    if (satsys(eph->sat,NULL)!=SYS_GPS) return 0;
    return eph->hdr.msg_type==NAV_CNAV || eph->hdr.msg_type==NAV_CNV2;
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
    int i,j;
    int total_cnav=0;
    int written=0;

    for (i=1;i<argc;i++) {
        if (!strcmp(argv[i],"-i") && i+1<argc) {
            if (ninput<MAX_INPUT_FILE) input[ninput++]=argv[++i];
        }
        else if (!strcmp(argv[i],"-o") && i+1<argc) {
            if (strlen(argv[i+1])>=sizeof(output)) {
                fprintf(stderr,"[ERROR] Output path is too long\n");
                return 1;
            }
            strcpy(output,argv[++i]);
        }
        else if (!strcmp(argv[i],"-h") || !strcmp(argv[i],"--help")) {
            print_help();
            return 0;
        }
        else {
            fprintf(stderr,"[ERROR] Unknown or incomplete argument: %s\n",argv[i]);
            print_help();
            return 1;
        }
    }

    if (ninput==0) {
        print_help();
        return 1;
    }

    for (i=0;i<ninput;i++) {
        int before=nav.n;
        int file_cnav=0;

        printf("[INFO] Reading %s\n",input[i]);
        if (!readrnx(input[i],0,"",&obs,&nav,&sta)) {
            fprintf(stderr,"[ERROR] Failed to read RINEX file: %s\n",input[i]);
            continue;
        }
        for (j=before;j<nav.n;j++) {
            if (is_gps_cnav(&nav.eph[j])) file_cnav++;
        }
        total_cnav+=file_cnav;
        printf("[INFO] %s: added %d broadcast ephemerides, GPS CNAV/CNV2=%d\n",
               input[i],nav.n-before,file_cnav);
    }

    printf("[INFO] Total broadcast ephemerides=%d, GPS CNAV/CNV2=%d\n",
           nav.n,total_cnav);

    if (total_cnav==0) {
        fprintf(stderr,
            "[ERROR] No GPS CNAV/CNV2 ephemeris records were found. "
            "For RINEX 4, records must have message type CNAV or CNV2.\n");
        freenav(&nav,0x3FF);
        freeobs(&obs);
        return 2;
    }

    fp=fopen(output,"w");
    if (!fp) {
        fprintf(stderr,"[ERROR] Cannot open output file: %s\n",output);
        freenav(&nav,0x3FF);
        freeobs(&obs);
        return 3;
    }

    for (i=0;i<nav.n;i++) {
        if (!is_gps_cnav(&nav.eph[i])) continue;
        if (write_unicore_gpscnav_eph(fp,&nav.eph[i])) written++;
    }

    if (fclose(fp)!=0) {
        fprintf(stderr,"[ERROR] Failed to close output file: %s\n",output);
        freenav(&nav,0x3FF);
        freeobs(&obs);
        return 4;
    }

    printf("[INFO] Wrote %d GPSCNAVEPHA records to %s\n",written,output);

    freenav(&nav,0x3FF);
    freeobs(&obs);

    if (written==0) {
        fprintf(stderr,"[ERROR] GPS CNAV/CNV2 records were parsed but none were written.\n");
        return 5;
    }
    return 0;
}
