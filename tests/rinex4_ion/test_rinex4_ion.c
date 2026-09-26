/* Issue #34: RINEX 4 "> ION" records fill the per-system model arrays. */
#include "rtklib.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static const char *PATH="rinex4_ion_fixture.rnx";

/* One ION record: header line, epoch + first three values, then four per line. */
static void ion_record(FILE *fp, const char *header, const char *epoch,
                       const double *values, int count)
{
    int i;
    fprintf(fp,"> ION %s\n    %s",header,epoch);
    for (i=0;i<count;i++) {
        if (i==3||(i>3&&(i-3)%4==0)) fprintf(fp,"\n    ");
        fprintf(fp,"%19.12E",values[i]);
    }
    fprintf(fp,"\n");
}

static void set8(double *v, double scale)
{
    static const double base[8]={1.1E-8,1.5E-8,-6.0E-8,-1.2E-7,
                                 9.0E4,1.3E5,-6.5E4,-3.9E5};
    int i;
    for (i=0;i<8;i++) v[i]=base[i]*scale;
}

static int same(const double *a, const double *b, int n, const char *what)
{
    int i;
    for (i=0;i<n;i++) {
        if (fabs(a[i]-b[i])>1E-12*fmax(1.0,fabs(b[i]))) {
            fprintf(stderr,"%s[%d]=%.12e, expected %.12e\n",what,i,a[i],b[i]);
            return 0;
        }
    }
    return 1;
}

static void write_fixture(void)
{
    double gps_cnvx[8],gps_lnav_early[8],gps_lnav_late[8];
    double qzs_wide[8],qzs_japn[8],qzs_cnvx[8];
    double bds_d1d2[8],bdgim[9]={1.0,2.0,3.0,4.0,5.0,6.0,7.0,8.0,9.0};
    double gal[4]={92.5,0.41015625,0.0162658691406,0.0};
    FILE *fp=fopen(PATH,"w");

    set8(gps_cnvx,3.0); set8(gps_lnav_early,1.0); set8(gps_lnav_late,2.0);
    set8(qzs_wide,4.0); set8(qzs_japn,5.0); set8(qzs_cnvx,6.0);
    set8(bds_d1d2,7.0);
    fprintf(fp,"%9.2f%-11s%-20s%-20s%-20s\n",4.02,"","N: GNSS NAV DATA","M: Mixed","RINEX VERSION / TYPE");
    fprintf(fp,"%60s%-20s\n","","END OF HEADER");
    /* CNVX is earlier than every LNAV record but LNAV is preferred. */
    ion_record(fp,"G10 CNVX","2026 04 30 00 00 00",gps_cnvx,8);
    ion_record(fp,"G20 LNAV","2026 04 30 23 56 24",gps_lnav_late,8);
    ion_record(fp,"G05 LNAV","2026 04 30 00 10 00",gps_lnav_early,8);
    /* QZSS: JAPN before WIDE, LNAV before CNVX, regardless of time. */
    ion_record(fp,"J02 CNVX JAPN","2026 04 30 00 00 00",qzs_cnvx,8);
    ion_record(fp,"J02 LNAV WIDE","2026 04 30 00 05 00",qzs_wide,8);
    ion_record(fp,"J03 LNAV JAPN","2026 04 30 06 00 00",qzs_japn,8);
    /* BDS: BDGIM (CNVX) never enters the Klobuchar array. */
    ion_record(fp,"C12 CNVX","2026 04 30 00 00 00",bdgim,9);
    ion_record(fp,"C01 D1D2","2026 04 30 00 58 00",bds_d1d2,8);
    ion_record(fp,"E09 IFNV","2026 04 30 05 31 04",gal,4);
    fclose(fp);
}

int main(void)
{
    double expect_gps[8],expect_qzs[8],expect_bds[8],header[8],expect_gal[4]={92.5,0.41015625,0.0162658691406,0.0};
    double pos[3]={30.5*D2R,114.3*D2R,50.0},azel[2]={0.0,45.0*D2R},ion_m,var;
    nav_t nav={0},preset={0};
    gtime_t t;
    int ok=1,i;

    write_fixture();
    if (!readrnx(PATH,1,"",NULL,&nav,NULL)) {
        fprintf(stderr,"RINEX 4 ION fixture was not read\n");
        remove(PATH);
        return 1;
    }
    set8(expect_gps,1.0); set8(expect_qzs,5.0); set8(expect_bds,7.0);
    ok&=nav.nion==9;
    ok&=same(nav.ion_gps,expect_gps,8,"ion_gps (earliest LNAV)");
    ok&=same(nav.ion_qzs,expect_qzs,8,"ion_qzs (LNAV JAPN)");
    ok&=same(nav.ion_cmp,expect_bds,8,"ion_cmp (D1D2, not BDGIM)");
    ok&=same(nav.ion_gal,expect_gal,4,"ion_gal (IFNV)");

    /* ionocorr(IONOOPT_BRDC) calls ionmodel(nav->ion_gps), which falls back
     * to RTKLIB's built-in ion_default only for an all-zero array.  The loaded
     * array must now give the file's model, not that default. */
    {
        const double zero[8]={0};
        t=epoch2time((double[]){2026,4,30,6,0,0});
        ion_m=ionmodel(t,nav.ion_gps,pos,azel);
        var=ionmodel(t,zero,pos,azel);
        if (fabs(ion_m-ionmodel(t,expect_gps,pos,azel))>1E-9||fabs(ion_m-var)<1E-3) {
            fprintf(stderr,"ionmodel still evaluates the default set: %.6f vs %.6f\n",ion_m,var);
            ok=0;
        }
    }
    free(nav.eph); free(nav.geph); free(nav.ion);

    /* A set already present (RINEX 2/3 header or an earlier file) is kept. */
    for (i=0;i<8;i++) preset.ion_gps[i]=header[i]=0.5+i;
    if (!readrnx(PATH,1,"",NULL,&preset,NULL)) ok=0;
    ok&=same(preset.ion_gps,header,8,"preset ion_gps");
    ok&=same(preset.ion_qzs,expect_qzs,8,"ion_qzs with preset ion_gps");
    free(preset.eph); free(preset.geph); free(preset.ion);

    remove(PATH);
    if (!ok) return 1;
    printf("RINEX 4 ION model arrays: PASS\n");
    return 0;
}
