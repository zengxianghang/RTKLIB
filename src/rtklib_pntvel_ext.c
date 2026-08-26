#include "rtklib_pntvel_ext.h"

#define PNTVEL_MAXITR 10

static int velocity_residuals(const obsd_t *obs, const unsigned char *doppler_valid,
                              int n, const double *rs, const double *dts,
                              const nav_t *nav, const double *receiver_ecef_m,
                              const double *state, const double *azel,
                              const int *vsat, double *residual,
                              double *design)
{
    double pos[3],E[9],a[3],e[3],relative_velocity[3],cosel,lambda,rate;
    int i,j,nv=0;

    ecef2pos(receiver_ecef_m,pos);
    xyz2enu(pos,E);

    for (i=0;i<n&&i<MAXOBS;i++) {
        if (!doppler_valid[i]||!vsat[i]) continue;

        lambda=nav->lam[obs[i].sat-1][0];
        if (lambda==0.0||norm(rs+3+i*6,3)<=0.0) continue;

        cosel=cos(azel[1+i*2]);
        a[0]=sin(azel[i*2])*cosel;
        a[1]=cos(azel[i*2])*cosel;
        a[2]=sin(azel[1+i*2]);
        matmul("TN",3,1,3,1.0,E,a,0.0,e);

        for (j=0;j<3;j++) relative_velocity[j]=rs[j+3+i*6]-state[j];
        rate=dot(relative_velocity,e,3)+OMGE/CLIGHT*(
             rs[4+i*6]*receiver_ecef_m[0]+rs[1+i*6]*state[0]-
             rs[3+i*6]*receiver_ecef_m[1]-rs[i*6]*state[1]);

        residual[nv]=-lambda*obs[i].D[0]-
                     (rate+state[3]-CLIGHT*dts[1+i*2]);
        for (j=0;j<4;j++) design[j+nv*4]=j<3?-e[j]:1.0;
        nv++;
    }
    return nv;
}

int rtklib_pntvel_ext(const obsd_t *obs, const unsigned char *doppler_valid,
                      int n, const nav_t *nav, const prcopt_t *opt,
                      const double *receiver_ecef_m, double *velocity_ecef_mps,
                      double *receiver_clock_drift_mps, int *used_satellites,
                      char *msg)
{
    double state[4]={0},delta[4],covariance[16],*rs,*dts,*var,*azel,*residual,*design;
    double pos[3],los[3],range;
    int i,j,nv,nobs,svh[MAXOBS],vsat[MAXOBS]={0};

    if (msg) msg[0]='\0';
    if (used_satellites) *used_satellites=0;
    if (!obs||!doppler_valid||n<=0||!nav||!opt||!receiver_ecef_m||
        !velocity_ecef_mps||!receiver_clock_drift_mps) {
        if (msg) strcpy(msg,"invalid point-velocity arguments");
        return 0;
    }
    if (norm(receiver_ecef_m,3)<=0.0) {
        if (msg) strcpy(msg,"invalid receiver position hint");
        return 0;
    }

    nobs=n<MAXOBS?n:MAXOBS;
    rs=mat(6,nobs); dts=mat(2,nobs); var=mat(1,nobs);
    azel=zeros(2,nobs); residual=mat(nobs,1); design=mat(4,nobs);

    satposs(obs[0].time,obs,nobs,nav,opt->sateph,rs,dts,var,svh);
    ecef2pos(receiver_ecef_m,pos);

    for (i=0;i<nobs;i++) {
        if (!doppler_valid[i]||obs[i].sat<=0||obs[i].sat>MAXSAT) continue;
        if ((range=geodist(rs+i*6,receiver_ecef_m,los))<=0.0) continue;
        (void)range;
        if (satazel(pos,los,azel+i*2)<opt->elmin) continue;
        if (satexclude(obs[i].sat,svh[i],opt)) continue;
        if (nav->lam[obs[i].sat-1][0]==0.0||norm(rs+3+i*6,3)<=0.0) continue;
        vsat[i]=1;
    }

    for (i=0;i<PNTVEL_MAXITR;i++) {
        nv=velocity_residuals(obs,doppler_valid,nobs,rs,dts,nav,
                              receiver_ecef_m,state,azel,vsat,residual,design);
        if (nv<4) {
            if (msg) sprintf(msg,"lack of valid Doppler observations ns=%d",nv);
            break;
        }
        if (lsq(design,residual,4,nv,delta,covariance)) {
            if (msg) strcpy(msg,"velocity least-squares error");
            break;
        }
        for (j=0;j<4;j++) state[j]+=delta[j];
        if (norm(delta,4)<1E-6) {
            for (j=0;j<3;j++) velocity_ecef_mps[j]=state[j];
            *receiver_clock_drift_mps=state[3];
            if (used_satellites) *used_satellites=nv;
            free(rs); free(dts); free(var); free(azel); free(residual); free(design);
            return 1;
        }
    }

    free(rs); free(dts); free(var); free(azel); free(residual); free(design);
    return 0;
}
