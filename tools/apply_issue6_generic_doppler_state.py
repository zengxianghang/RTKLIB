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
    '''static const eph_t *select_signal_eph(gtime_t time, int sat, unsigned char code,
                                      int required_message_mask,
                                      const nav_t *nav, int *message_type)
{''',
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
}

static const eph_t *select_signal_eph(gtime_t time, int sat, unsigned char code,
                                      int required_message_mask,
                                      const nav_t *nav, int *message_type)
{''',
)

replace_once(
    "src/rtkcmn.c",
    '''    if (!required_message_mask) {
        eph=select_signal_eph(time,sat,code,0,nav,&type);
    }
    else {''',
    '''    if (!required_message_mask) {
        eph=select_generic_eph(time,sat,nav,&type);
    }
    else {''',
)

replace_once(
    "src/rtklib_residual_ext.h",
    ''' * state. obs->D[0] is Doppler in Hz and wavelength_m is the actual signal
 * wavelength, including GLONASS FCN dependence where applicable. The message
 * mask constrains satellite state selection to the signal's NAV family.
 */''',
    ''' * state. obs->D[0] is Doppler in Hz and wavelength_m is the actual signal
 * wavelength, including GLONASS FCN dependence where applicable. A nonzero
 * message mask constrains satellite-state selection to a NAV family compatible
 * with obs->code[0]. A zero mask selects the nearest generic broadcast state,
 * allowing Doppler validation when signal-specific code-bias NAV is absent.
 */''',
)

replace_once(
    "tests/residual_ext/test_residual_ext.c",
    '''    stat=rtklib_resdop_signal_ext(&obs,&nav,&opt,rr,zero_velocity,0.0,
                                  NAV_CNAV,wavelength,&residual,azel);
    if (stat!=1||!expect_close("truth-state Doppler residual",residual,0.0,5E-4)) return 1;

    puts("residual_ext: PASS");''',
    '''    stat=rtklib_resdop_signal_ext(&obs,&nav,&opt,rr,zero_velocity,0.0,
                                  NAV_CNAV,wavelength,&residual,azel);
    if (stat!=1||!expect_close("truth-state Doppler residual",residual,0.0,5E-4)) return 1;

    /* Doppler does not require the signal-specific code-bias NAV family. */
    obs.code[0]=CODE_L1L;
    obs.P[0]=2.4E7;
    wavelength=CLIGHT/FREQ1;
    for (i=0;i<5;i++) {
        stat=rtklib_signal_state_ext(t,obs.P[0],sat,CODE_L1L,0,
                                     &nav,rs,dts,&var,&svh,&info);
        if (stat!=1||info.message_type!=NAV_LNAV) {
            fprintf(stderr,"generic state did not select nearest LNAV ephemeris\\n");
            return 1;
        }
        range=geodist(rs,rr,e);
        if (!(range>0.0)) return 1;
        obs.P[0]=range-CLIGHT*dts[0];
    }
    stat=rtklib_signal_state_ext(t,obs.P[0],sat,CODE_L1L,NAV_LNAV,
                                 &nav,rs,dts,&var,&svh,&info);
    if (stat!=0) {
        fprintf(stderr,"forced LNAV incorrectly accepted L1C observation code\\n");
        return 1;
    }
    stat=rtklib_signal_state_ext(t,obs.P[0],sat,CODE_L1L,0,
                                 &nav,rs,dts,&var,&svh,&info);
    if (stat!=1) return 1;
    range=geodist(rs,rr,e);
    if (!(range>0.0)) return 1;
    for (i=0;i<3;i++) relative_velocity[i]=rs[i+3];
    rate=dot(relative_velocity,e,3)+OMGE/CLIGHT*(
         rs[4]*rr[0]-rs[3]*rr[1]);
    obs.D[0]=(float)(-(rate-CLIGHT*dts[1])/wavelength);
    stat=rtklib_resdop_signal_ext(&obs,&nav,&opt,rr,zero_velocity,0.0,
                                  0,wavelength,&residual,azel);
    if (stat!=1||info.message_type!=NAV_LNAV||
        !expect_close("generic-state L1C Doppler residual",residual,0.0,5E-4)) return 1;

    puts("residual_ext: PASS");''',
)

print("issue #6 generic Doppler state patch applied")
