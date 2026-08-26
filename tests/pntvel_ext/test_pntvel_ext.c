#include "rtklib.h"
#include "rtklib_pntvel_ext.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int nearly_equal(double a, double b, double tol)
{
    return fabs(a-b)<=tol;
}

static void fill_case(obsd_t *obs, double *rs, double *dts, nav_t *nav,
                      const double *rr, const double *true_vel,
                      double true_clkdrift)
{
    static const double sat_state[5][6]={
        {20200000.0, 14000000.0, 21000000.0, -1200.0,  2100.0,  900.0},
        {21100000.0,-13000000.0, 19000000.0,  1600.0,  1200.0,-700.0},
        {-18000000.0,17000000.0, 22000000.0, -900.0, -1800.0, 1400.0},
        {-20000000.0,-15000000.0,21000000.0, 1300.0, -1500.0,-1100.0},
        {13000000.0,22000000.0,-17000000.0, -700.0, 1700.0, 1500.0}
    };
    double e[3],vs[3],rate,lam;
    int i,j;

    memset(obs,0,sizeof(obsd_t)*5);
    memset(rs,0,sizeof(double)*30);
    memset(dts,0,sizeof(double)*10);
    memset(nav,0,sizeof(*nav));

    lam=CLIGHT/FREQ1;
    for (i=0;i<5;i++) {
        obs[i].sat=(unsigned char)(i+1);
        nav->lam[obs[i].sat-1][0]=lam;
        for (j=0;j<6;j++) rs[i*6+j]=sat_state[i][j];
        if (geodist(rs+i*6,rr,e)<=0.0) continue;
        for (j=0;j<3;j++) vs[j]=rs[i*6+3+j]-true_vel[j];
        rate=dot(vs,e,3)+OMGE/CLIGHT*(rs[i*6+4]*rr[0]+rs[i*6+1]*true_vel[0]-
                                      rs[i*6+3]*rr[1]-rs[i*6]*true_vel[1]);
        obs[i].D[0]=-(rate+true_clkdrift)/lam;
    }
}

int main(void)
{
    obsd_t obs[5];
    nav_t nav;
    double rs[30],dts[10];
    const double rr[3]={6378137.0,0.0,0.0};
    const double expected_vel[3]={12.3,-4.5,1.2};
    const double expected_clkdrift=0.7;
    double vel[3],clkdrift;
    int use[5]={1,1,1,1,1},used=0;
    char msg[128];

    fill_case(obs,rs,dts,&nav,rr,expected_vel,expected_clkdrift);
    if (!rtklib_pntvel_ext(obs,5,rs,dts,&nav,rr,use,vel,&clkdrift,&used,msg)) {
        fprintf(stderr,"velocity solve failed: %s\n",msg);
        return 1;
    }
    if (used!=5||!nearly_equal(vel[0],expected_vel[0],1E-6)||
        !nearly_equal(vel[1],expected_vel[1],1E-6)||
        !nearly_equal(vel[2],expected_vel[2],1E-6)||
        !nearly_equal(clkdrift,expected_clkdrift,1E-6)) {
        fprintf(stderr,"unexpected solution used=%d vel=%.9f %.9f %.9f drift=%.9f\n",
                used,vel[0],vel[1],vel[2],clkdrift);
        return 2;
    }

    use[4]=0;
    if (!rtklib_pntvel_ext(obs,5,rs,dts,&nav,rr,use,vel,&clkdrift,&used,msg)||used!=4) {
        fprintf(stderr,"four-observation masked solve failed: %s used=%d\n",msg,used);
        return 3;
    }

    use[3]=0;
    if (rtklib_pntvel_ext(obs,5,rs,dts,&nav,rr,use,vel,&clkdrift,&used,msg)) {
        fprintf(stderr,"three-observation solve unexpectedly succeeded\n");
        return 4;
    }
    if (used!=3) {
        fprintf(stderr,"unexpected insufficient-observation count: %d\n",used);
        return 5;
    }

    puts("pntvel extension tests passed");
    return 0;
}
