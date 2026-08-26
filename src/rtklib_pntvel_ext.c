#include "rtklib_pntvel_ext.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define PNTVEL_EXT_MAXITR 10

static void set_msg(char *msg, const char *text)
{
    if (msg) {
        strncpy(msg,text,127);
        msg[127]='\0';
    }
}

static int doppler_residuals(const obsd_t *obs, int n, const double *rs,
                             const double *dts, const nav_t *nav,
                             const double *rr, const int *use,
                             const double *x, double *v, double *H)
{
    double lam,rate,e[3],vs[3];
    int i,j,nv=0;

    for (i=0;i<n&&i<MAXOBS;i++) {
        if (use&&!use[i]) continue;
        if (obs[i].sat<=0||obs[i].sat>MAXSAT) continue;

        lam=nav->lam[obs[i].sat-1][0];
        if (obs[i].D[0]==0.0||lam==0.0||norm(rs+3+i*6,3)<=0.0) continue;
        if (geodist(rs+i*6,rr,e)<=0.0) continue;

        for (j=0;j<3;j++) vs[j]=rs[j+3+i*6]-x[j];

        rate=dot(vs,e,3)+OMGE/CLIGHT*(rs[4+i*6]*rr[0]+rs[1+i*6]*x[0]-
                                      rs[3+i*6]*rr[1]-rs[i*6]*x[1]);

        v[nv]=-lam*obs[i].D[0]-(rate+x[3]-CLIGHT*dts[1+i*2]);
        for (j=0;j<4;j++) H[j+nv*4]=j<3?-e[j]:1.0;
        nv++;
    }
    return nv;
}

int rtklib_pntvel_ext(const obsd_t *obs, int n, const double *rs,
                      const double *dts, const nav_t *nav, const double *rr,
                      const int *use, double *vel, double *clkdrift, int *used,
                      char *msg)
{
    double x[4]={0},dx[4],Q[16],*v,*H;
    int i,j,nv=0;

    if (used) *used=0;
    if (vel) vel[0]=vel[1]=vel[2]=0.0;
    if (clkdrift) *clkdrift=0.0;
    if (msg) msg[0]='\0';

    if (!obs||n<=0||!rs||!dts||!nav||!rr||!vel||!clkdrift) {
        set_msg(msg,"invalid point-velocity arguments");
        return 0;
    }
    if (norm(rr,3)<=0.0) {
        set_msg(msg,"invalid receiver position hint");
        return 0;
    }

    v=mat(n,1);
    H=mat(4,n);
    if (!v||!H) {
        free(v); free(H);
        set_msg(msg,"point-velocity allocation failure");
        return 0;
    }

    for (i=0;i<PNTVEL_EXT_MAXITR;i++) {
        nv=doppler_residuals(obs,n,rs,dts,nav,rr,use,x,v,H);
        if (used) *used=nv;
        if (nv<4) {
            free(v); free(H);
            set_msg(msg,"lack of valid Doppler observations");
            return 0;
        }
        if (lsq(H,v,4,nv,dx,Q)) {
            free(v); free(H);
            set_msg(msg,"point-velocity least-squares failure");
            return 0;
        }
        for (j=0;j<4;j++) x[j]+=dx[j];
        if (norm(dx,4)<1E-6) {
            for (j=0;j<3;j++) vel[j]=x[j];
            *clkdrift=x[3];
            free(v); free(H);
            return 1;
        }
    }

    free(v); free(H);
    set_msg(msg,"point-velocity iteration did not converge");
    return 0;
}
