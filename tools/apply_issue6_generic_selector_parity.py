from pathlib import Path


def replace_once(path_str: str, old: str, new: str) -> None:
    path = Path(path_str)
    text = path.read_text()
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{path_str}: expected replacement anchor once, found {count}")
    path.write_text(text.replace(old, new, 1))


replace_once(
    "src/rtkcmn.c",
    '''static const eph_t *select_generic_eph(gtime_t time, int sat,
                                       const nav_t *nav, int *message_type)
{
    const eph_t *best=NULL;
    double best_age=0.0,max_age;
    int i,sys,type;

    if (!nav||(sys=satsys(sat,NULL))==SYS_NONE) return NULL;
    max_age=max_eph_age_sec(sys);

    for (i=0;i<nav->n;i++) {
        double age;
        if (nav->eph[i].sat!=sat) continue;
        type=canonical_message_type(nav->eph+i,sys);
        if (!type) continue;
        age=fabs(timediff(nav->eph[i].toe,time));
        if (age>max_age) continue;
        if (!best||age<best_age||(fabs(age-best_age)<1E-9&&
            timediff(nav->eph[i].toc,best->toc)>0.0)) {
            best=nav->eph+i;
            best_age=age;
            if (message_type) *message_type=type;
        }
    }
    return best;
}''',
    '''static const eph_t *select_generic_eph(gtime_t time, int sat,
                                       const nav_t *nav, int *message_type)
{
    double age,tmax,tmin;
    int i,j=-1,sys;

    if (!nav||(sys=satsys(sat,NULL))==SYS_NONE) return NULL;

    /* Mirror ephemeris.c:seleph(time,sat,-1,nav) exactly. Generic Doppler
       state must use the same broadcast record as the simulator/stock satpos. */
    tmax=max_eph_age_sec(sys)+1.0;
    tmin=tmax+1.0;
    for (i=0;i<nav->n;i++) {
        if (nav->eph[i].sat!=sat) continue;
        if ((age=fabs(timediff(nav->eph[i].toe,time)))>tmax) continue;
        if (age<=tmin) {
            j=i;
            tmin=age;
        }
    }
    if (j<0) return NULL;
    if (message_type) *message_type=canonical_message_type(nav->eph+j,sys);
    return nav->eph+j;
}''',
)

replace_once(
    "tests/residual_ext/test_residual_ext.c",
    '''        if (stat!=1||info.message_type!=NAV_LNAV) {
            fprintf(stderr,"generic state did not select nearest LNAV ephemeris\\n");
            return 1;
        }''',
    '''        if (stat!=1||info.message_type!=NAV_CNAV||info.iode!=11) {
            fprintf(stderr,"generic state did not mirror stock equal-age tie selection\\n");
            return 1;
        }''',
)

replace_once(
    "tests/residual_ext/test_residual_ext.c",
    '''    if (stat!=1||info.message_type!=NAV_LNAV||
        !expect_close("generic-state L1C Doppler residual",residual,0.0,5E-4)) return 1;''',
    '''    if (stat!=1||info.message_type!=NAV_CNAV||info.iode!=11||
        !expect_close("generic-state L1C Doppler residual",residual,0.0,5E-4)) return 1;''',
)

print("issue #6 generic selector now mirrors stock seleph")
