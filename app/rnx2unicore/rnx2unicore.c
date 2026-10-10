#include "rtklib.h"
#include "unicore_gpscnav.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define MAX_INPUT_FILE 256
#define INPUT_PATH_SIZE 4096
#define UNICORE_QZS_MIN_PRN 33
#define UNICORE_QZS_MAX_PRN 42

typedef struct {
    eph_t *eph;
    int input_order;
} cnav_ref_t;

static void print_help(void)
{
    printf("Usage:\n");
    printf("  rnx2unicore -i <rinex_nav_or_folder> [-i <rinex_nav_or_folder> ...] -o <output>\n");
    printf("\n");
    printf("Input may be a RINEX navigation file or a folder.\n");
    printf("A folder is scanned non-recursively and all files are passed through the RTKLIB RINEX reader.\n");
}

static int path_is_directory(const char *path)
{
#ifdef WIN32
    struct _stat st;

    if (!path || !*path || _stat(path,&st)!=0) return 0;
    return (st.st_mode&_S_IFDIR)!=0;
#else
    struct stat st;

    if (!path || !*path || stat(path,&st)!=0) return 0;
    return S_ISDIR(st.st_mode);
#endif
}

static int make_directory_pattern(const char *dir, char *pattern, size_t size)
{
    size_t len;
    char sep;

    if (!dir || !pattern || size==0) return 0;

    len=strlen(dir);
    if (len==0 || len+3>size) return 0;

    memcpy(pattern,dir,len+1);
#ifdef WIN32
    sep='\\';
#else
    sep='/';
#endif
    if (pattern[len-1]!='/' && pattern[len-1]!='\\') {
        pattern[len++]=sep;
        pattern[len]='\0';
    }
    pattern[len++]='*';
    pattern[len]='\0';
    return 1;
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

static int is_supported_cnav(const eph_t *eph)
{
    if (!gpscnav_output_prn(eph,NULL)) return 0;
    return eph->hdr.msg_type==NAV_CNAV || eph->hdr.msg_type==NAV_CNV2;
}

static int compare_cnav_ref(const void *p1, const void *p2)
{
    const cnav_ref_t *a=(const cnav_ref_t *)p1;
    const cnav_ref_t *b=(const cnav_ref_t *)p2;
    double dt=timediff(a->eph->ttr,b->eph->ttr);
    int prn_a=0,prn_b=0;

    if (dt<0.0) return -1;
    if (dt>0.0) return 1;

    gpscnav_output_prn(a->eph,&prn_a);
    gpscnav_output_prn(b->eph,&prn_b);
    if (prn_a<prn_b) return -1;
    if (prn_a>prn_b) return 1;

    if (a->eph->hdr.msg_type<b->eph->hdr.msg_type) return -1;
    if (a->eph->hdr.msg_type>b->eph->hdr.msg_type) return 1;

    if (a->input_order<b->input_order) return -1;
    if (a->input_order>b->input_order) return 1;
    return 0;
}

int main(int argc, char **argv)
{
    char *input[MAX_INPUT_FILE];
    int ninput=0;
    char output[1024]="gpscnav.log";
    nav_t nav={0};
    cnav_ref_t *cnav=NULL;
    FILE *fp;
    int i,j;
    int total_cnav=0;
    int written=0;
    int written_gpscnav=0;
    int written_gpsl1c=0;
    int cnav_count=0;

    for (i=1;i<argc;i++) {
        if (!strcmp(argv[i],"-i") && i+1<argc) {
            if (ninput>=MAX_INPUT_FILE) {
                fprintf(stderr,"[ERROR] Too many -i arguments (max %d)\n",MAX_INPUT_FILE);
                return 1;
            }
            input[ninput++]=argv[++i];
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
        char pattern[INPUT_PATH_SIZE];
        const char *read_path=input[i];
        int before=nav.n;
        int file_cnav=0;
        int read_stat;
        int is_dir=path_is_directory(input[i]);

        if (is_dir) {
            if (!make_directory_pattern(input[i],pattern,sizeof(pattern))) {
                fprintf(stderr,"[ERROR] Input folder path is too long: %s\n",input[i]);
                continue;
            }
            read_path=pattern;
            printf("[INFO] Reading RINEX files from folder %s (non-recursive)\n",input[i]);
        }
        else {
            printf("[INFO] Reading %s\n",input[i]);
        }

        /* readrnx expands wildcards itself. Passing obs/sta as NULL avoids
         * retaining observation data when a folder also contains observation
         * RINEX files or unrelated files. */
        read_stat=readrnx(read_path,0,"",NULL,&nav,NULL);

        /* With a wildcard, RTKLIB returns the status of the last expanded
         * file. A non-RINEX file sorted last may therefore yield 0 even after
         * valid navigation files were appended. nav.n is the authoritative
         * indication that this input contributed navigation data. */
        if (read_stat<=0 && nav.n==before) {
            if (is_dir) {
                fprintf(stderr,
                    "[ERROR] No readable RINEX navigation data found in folder: %s\n",
                    input[i]);
            }
            else {
                fprintf(stderr,"[ERROR] Failed to read RINEX file: %s\n",input[i]);
            }
            continue;
        }

        for (j=before;j<nav.n;j++) {
            if (is_supported_cnav(&nav.eph[j])) file_cnav++;
        }
        total_cnav+=file_cnav;

        if (is_dir) {
            printf("[INFO] Folder %s: added %d broadcast ephemerides, GPS/QZSS CNAV/CNV2=%d\n",
                   input[i],nav.n-before,file_cnav);
        }
        else {
            printf("[INFO] %s: added %d broadcast ephemerides, GPS/QZSS CNAV/CNV2=%d\n",
                   input[i],nav.n-before,file_cnav);
        }
    }

    printf("[INFO] Total broadcast ephemerides=%d, GPS/QZSS CNAV/CNV2=%d\n",
           nav.n,total_cnav);

    if (total_cnav==0) {
        fprintf(stderr,
            "[ERROR] No GPS/QZSS CNAV/CNV2 ephemeris records were found. "
            "For RINEX 4, records must have message type CNAV or CNV2.\n");
        freenav(&nav,0x3FF);
        return 2;
    }

    cnav=(cnav_ref_t *)malloc(sizeof(*cnav)*(size_t)total_cnav);
    if (!cnav) {
        fprintf(stderr,"[ERROR] Cannot allocate GPS/QZSS CNAV sort buffer\n");
        freenav(&nav,0x3FF);
        return 3;
    }

    for (i=0;i<nav.n;i++) {
        if (!is_supported_cnav(&nav.eph[i])) continue;
        if (cnav_count>=total_cnav) {
            fprintf(stderr,"[ERROR] Internal GPS/QZSS CNAV count mismatch\n");
            free(cnav);
            freenav(&nav,0x3FF);
            return 4;
        }
        cnav[cnav_count].eph=&nav.eph[i];
        cnav[cnav_count].input_order=i;
        cnav_count++;
    }

    if (cnav_count!=total_cnav) {
        fprintf(stderr,
            "[ERROR] Internal GPS/QZSS CNAV count mismatch: expected %d, collected %d\n",
            total_cnav,cnav_count);
        free(cnav);
        freenav(&nav,0x3FF);
        return 4;
    }

    qsort(cnav,(size_t)cnav_count,sizeof(*cnav),compare_cnav_ref);
    printf("[INFO] Sorted %d GPS/QZSS CNAV/CNV2 records by transmission time (ttr)\n",
           cnav_count);

    fp=fopen(output,"w");
    if (!fp) {
        fprintf(stderr,"[ERROR] Cannot open output file: %s\n",output);
        free(cnav);
        freenav(&nav,0x3FF);
        return 5;
    }

    for (i=0;i<cnav_count;i++) {
        if (cnav[i].eph->hdr.msg_type==NAV_CNAV) {
            if (write_unicore_gpscnav_eph(fp,cnav[i].eph)) {
                written_gpscnav++;
                written++;
            }
        }
        else if (cnav[i].eph->hdr.msg_type==NAV_CNV2) {
            if (write_novatel_gpsl1c_eph(fp,cnav[i].eph)) {
                written_gpsl1c++;
                written++;
            }
        }
    }

    if (fclose(fp)!=0) {
        fprintf(stderr,"[ERROR] Failed to close output file: %s\n",output);
        free(cnav);
        freenav(&nav,0x3FF);
        return 6;
    }

    printf("[INFO] Wrote %d GPSCNAVEPHA and %d GPSL1CEPHEMA records to %s\n",
           written_gpscnav,written_gpsl1c,output);

    free(cnav);
    freenav(&nav,0x3FF);

    if (written==0) {
        fprintf(stderr,"[ERROR] GPS/QZSS CNAV/CNV2 records were parsed but none were written.\n");
        return 7;
    }
    if (written!=cnav_count) {
        fprintf(stderr,
            "[ERROR] GPS/QZSS CNAV/CNV2 write count mismatch: parsed %d, wrote %d.\n",
            cnav_count,written);
        return 8;
    }
    return 0;
}
