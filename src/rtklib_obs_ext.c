#include "rtklib_obs_ext.h"

#include <string.h>

unsigned char obs2code_ext(const char *obs, int *freq)
{
    unsigned char code;

    if (freq) *freq=0;
    if (!obs) return CODE_NONE;

    code=obs2code(obs,freq);
    if (code!=CODE_NONE) return code;

    if (!strcmp(obs,"5P")) {
        if (freq) *freq=3; /* L5 / B2a */
        return CODE_L5P;
    }
    if (!strcmp(obs,"7D")) {
        if (freq) *freq=5; /* L7 / B2b */
        return CODE_L7D;
    }
    if (!strcmp(obs,"1D")) {
        if (freq) *freq=1; /* L1 / B1C */
        return CODE_L1D;
    }
    return CODE_NONE;
}

const char *code2obs_ext(unsigned char code, int *freq)
{
    if (freq) *freq=0;

    if (code==CODE_L5P) {
        if (freq) *freq=3;
        return "5P";
    }
    if (code==CODE_L7D) {
        if (freq) *freq=5;
        return "7D";
    }
    if (code==CODE_L1D) {
        if (freq) *freq=1;
        return "1D";
    }
    return code2obs(code,freq);
}

int getcodepri_ext(int sys, unsigned char code, const char *opt)
{
    (void)opt;

    /* BDS-3 pilot/data codes absent from the legacy BDS priority strings. */
    if (sys==SYS_CMP) {
        if (code==CODE_L1D||code==CODE_L1P||code==CODE_L1X||
            code==CODE_L5P||code==CODE_L7D) return 14;
    }
    if (code==CODE_L1D||code==CODE_L5P||code==CODE_L7D) return 0;
    return getcodepri(sys,code,opt);
}
