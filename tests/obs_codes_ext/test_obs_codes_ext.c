#include "rtklib_obs_ext.h"

#include <stdio.h>
#include <string.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr,"FAIL %s:%d: %s\n",__FILE__,__LINE__,#condition); \
        return 0; \
    } \
} while (0)

static int test_bds_modern_codes(void)
{
    unsigned char code;
    const char *obs;
    int freq;

    code=obs2code_ext("1P",&freq);
    CHECK(code==CODE_L1P&&freq==1);
    CHECK(getcodepri_ext(SYS_CMP,code,NULL)>0);

    code=obs2code_ext("1D",&freq);
    CHECK(code==CODE_L1D&&freq==1);
    obs=code2obs_ext(code,&freq);
    CHECK(!strcmp(obs,"1D")&&freq==1);
    CHECK(getcodepri_ext(SYS_CMP,code,NULL)>0);
    CHECK(getcodepri_ext(SYS_GPS,code,NULL)==0);

    code=obs2code_ext("1X",&freq);
    CHECK(code==CODE_L1X&&freq==1);
    obs=code2obs_ext(code,&freq);
    CHECK(!strcmp(obs,"1X")&&freq==1);
    CHECK(getcodepri_ext(SYS_CMP,code,NULL)>0);
    CHECK(getcodepri_ext(SYS_GPS,code,NULL)==0);

    code=obs2code_ext("5P",&freq);
    CHECK(code==CODE_L5P&&freq==3);
    obs=code2obs_ext(code,&freq);
    CHECK(!strcmp(obs,"5P")&&freq==3);
    CHECK(getcodepri_ext(SYS_CMP,code,NULL)>0);
    CHECK(getcodepri_ext(SYS_GPS,code,NULL)==0);

    code=obs2code_ext("7D",&freq);
    CHECK(code==CODE_L7D&&freq==5);
    obs=code2obs_ext(code,&freq);
    CHECK(!strcmp(obs,"7D")&&freq==5);
    CHECK(getcodepri_ext(SYS_CMP,code,NULL)>0);
    CHECK(getcodepri_ext(SYS_GAL,code,NULL)==0);

    code=obs2code_ext("9Z",&freq);
    CHECK(code==CODE_NONE&&freq==0);
    return 1;
}

int main(void)
{
    if (!test_bds_modern_codes()) return 1;
    printf("modern BDS observation-code tests: 1/1 passed\n");
    return 0;
}
