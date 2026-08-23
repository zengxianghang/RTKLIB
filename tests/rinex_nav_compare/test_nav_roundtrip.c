/* Focused regression checks for the RINEX 4 NAV storage fixes. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../src/rtklib.h"

int main(int argc, char **argv)
{
    obs_t obs={0};
    nav_t nav={0};
    sta_t sta={0};
    int i,j,types=0,stat;

    if (argc!=2) {
        fprintf(stderr,"usage: %s RINEX4_NAV\n",argv[0]);
        return 2;
    }
    stat=readrnx(argv[1],1,"",&obs,&nav,&sta);
    if (!stat || nav.n<=0 || nav.ng<=0 || nav.ns<=0) {
        fprintf(stderr,"basic NAV counts failed: stat=%d n=%d ng=%d ns=%d\n",
                stat,nav.n,nav.ng,nav.ns);
        return 1;
    }
    if (nav.nion<=0 || nav.neop<=0 || nav.nsto<=0) {
        fprintf(stderr,"independent ION/EOP/STO counts failed: nion=%d neop=%d nsto=%d\n",
                nav.nion,nav.neop,nav.nsto);
        return 1;
    }
    if (!nav.sto[0].corr_type[0] || !nav.sto[0].present[0] ||
        !nav.sto[0].present[1]) {
        fprintf(stderr,"STO fields were not preserved\n");
        return 1;
    }
    if (!nav.eop[0].present[0] || !nav.eop[0].present[3] ||
        !nav.eop[0].present[6]) {
        fprintf(stderr,"EOP fields were not preserved\n");
        return 1;
    }
    if (nav.ion[0].ndata<=0 || !nav.ion[0].present[0]) {
        fprintf(stderr,"ION fields were not preserved\n");
        return 1;
    }
    for (i=0;i<nav.n;i++) {
        for (j=0;j<i;j++) {
            if (nav.eph[j].sat==nav.eph[i].sat &&
                nav.eph[j].hdr.msg_type!=nav.eph[i].hdr.msg_type) {
                types=1;
                break;
            }
        }
        if (types) break;
    }
    if (!types) {
        fprintf(stderr,"RINEX 4 message type identity was not retained\n");
        return 1;
    }
    printf("PASS n=%d ng=%d ns=%d nion=%d neop=%d nsto=%d\n",
           nav.n,nav.ng,nav.ns,nav.nion,nav.neop,nav.nsto);
    free(obs.data);
    freenav(&nav,0x3ff);
    return 0;
}
