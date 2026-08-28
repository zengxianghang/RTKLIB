#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rtklib.h"

#define TEST_FILE "test_nav_sva_serialization.tmp"

static int fail(const char *message)
{
    fprintf(stderr,"FAIL: %s\n",message);
    remove(TEST_FILE);
    return 1;
}

int main(void)
{
    nav_t input={0},output={0};
    FILE *fp;
    char line[4096];
    int sat=satno(SYS_GPS,1);
    int status=1;

    input.eph=(eph_t *)calloc(MAXSAT,sizeof(eph_t));
    output.eph=(eph_t *)calloc(MAXSAT,sizeof(eph_t));
    if (!input.eph||!output.eph) {
        free(input.eph);
        free(output.eph);
        return fail("allocation failed");
    }

    input.eph[sat-1].sat=sat;
    input.eph[sat-1].iode=12;
    input.eph[sat-1].iodc=34;
#if URA2URAI
    input.eph[sat-1].sva=5;
#else
    input.eph[sat-1].sva=2.75;
#endif
    input.eph[sat-1].svh=0;
    input.eph[sat-1].toe.time=(time_t)1700000000;
    input.eph[sat-1].toc.time=(time_t)1700000010;
    input.eph[sat-1].ttr.time=(time_t)1700000020;
    input.eph[sat-1].A=26560000.0;
    input.eph[sat-1].e=0.01;
    input.eph[sat-1].i0=0.95;
    input.eph[sat-1].toes=345600.0;
    input.eph[sat-1].fit=4.0;
    input.eph[sat-1].tgd[0]=-2.3E-9;
    input.eph[sat-1].code=1;
    input.eph[sat-1].flag=0;

    if (!savenav(TEST_FILE,&input)) {
        status=fail("savenav failed");
        goto cleanup;
    }

    fp=fopen(TEST_FILE,"r");
    if (!fp) {
        status=fail("saved file could not be opened");
        goto cleanup;
    }
    if (!fgets(line,sizeof(line),fp)) {
        fclose(fp);
        status=fail("saved ephemeris line missing");
        goto cleanup;
    }
    fclose(fp);
#if !URA2URAI
    if (!strstr(line,"2.75000000000000E+00")) {
        status=fail("double SVA was not serialized as a floating-point value");
        goto cleanup;
    }
#endif

    if (!readnav(TEST_FILE,&output)) {
        status=fail("readnav failed");
        goto cleanup;
    }
    if (output.eph[sat-1].sat!=sat||
        output.eph[sat-1].iode!=input.eph[sat-1].iode||
        output.eph[sat-1].iodc!=input.eph[sat-1].iodc) {
        status=fail("ephemeris identity did not round trip");
        goto cleanup;
    }
#if URA2URAI
    if (output.eph[sat-1].sva!=input.eph[sat-1].sva) {
        status=fail("integer SVA did not round trip");
        goto cleanup;
    }
#else
    if (fabs(output.eph[sat-1].sva-input.eph[sat-1].sva)>1E-12) {
        status=fail("double SVA did not round trip");
        goto cleanup;
    }
#endif

    printf("PASS: navigation SVA serialization round trip\n");
    status=0;

cleanup:
    remove(TEST_FILE);
    free(input.eph);
    free(output.eph);
    return status;
}
