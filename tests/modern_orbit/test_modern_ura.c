/*
 * Issue #20 Phase B: GPS/QZSS CNAV/CNAV-2 user range accuracy through the
 * public ABI 1.1 helper rtklib_shared_modern_ura_query().
 *
 * Usage: test_modern_ura FIXTURE URA_ORACLE_CSV
 *
 * 1. Real BRD400DLR CNAV/CNV2 records agree with tools/ura_oracle.py, an
 *    independent implementation of the specification text.
 * 2. Specification boundary cases on receiver-injected copies of a real
 *    record, with expected values derived by hand from IS-GPS-200N:
 *    nominal index values (including the rounded N = 1, 3, 5), the NED rate
 *    terms and the 93600 s breakpoint, the elevation factor, the
 *    no-prediction indices and t < t_op.
 * 3. Contract edges: legacy families are UNSUPPORTED, an ABI 1.0 result is
 *    rejected, an out-of-range elevation is an invalid argument, and a
 *    WN_op inconsistent with the record week fails closed.
 */

#include "../../src/rtklib.h"
#include "../../src/rtklib_shared_api.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ORACLE_TOL_M 1E-9

static int failures = 0;

static void fail(const char *what, const char *detail)
{
    fprintf(stderr, "FAIL: %s: %s\n", what, detail);
    failures++;
}

static void init_query(rtklib_shared_state_query_t *q, uint32_t system,
                       uint32_t prn, rtklib_shared_record_id_t id,
                       rtklib_shared_time_t t)
{
    memset(q, 0, sizeof(*q));
    q->abi_version = RTKLIB_SHARED_ABI_VERSION;
    q->struct_size = (uint32_t)sizeof(*q);
    q->system = system;
    q->prn = prn;
    q->rtklib_code = 16; /* L2S: carried by GPS/QZSS CNAV and LNAV codes */
    q->glonass_fcn = RTKLIB_SHARED_GLO_FCN_UNKNOWN;
    q->evaluation_time = t;
    q->selection_time = t;
    q->selected_record_id = id;
}

static int ura_query(const rtklib_shared_nav_store_t *store, uint32_t system,
                     uint32_t prn, rtklib_shared_record_id_t id,
                     rtklib_shared_time_t t, double elevation_deg,
                     rtklib_shared_modern_ura_result_t *r)
{
    rtklib_shared_state_query_t q;
    init_query(&q, system, prn, id, t);
    memset(r, 0, sizeof(*r));
    r->abi_version = RTKLIB_SHARED_ABI_VERSION;
    r->struct_size = (uint32_t)sizeof(*r);
    return rtklib_shared_modern_ura_query(store, &q, elevation_deg * D2R, r);
}

static int check_real_oracle(const rtklib_shared_nav_store_t *store,
                             const char *path)
{
    FILE *fp = fopen(path, "r");
    char line[512], name[128], sat[8], family[8], status[16], detail[256];
    size_t n = rtklib_shared_nav_record_count(store, 0, 0), i;
    int rows = 0, available = 0;

    if (!fp) { fail("oracle", "cannot open"); return 0; }
    if (!fgets(line, sizeof(line), fp)) { fclose(fp); return 0; }
    while (fgets(line, sizeof(line), fp)) {
        int toe_week, t_week, stat;
        double toe_sow, t_sow, elevation, expected;
        uint32_t system, prn, fam;
        rtklib_shared_record_identity_t id, found;
        rtklib_shared_modern_ura_result_t r;
        rtklib_shared_time_t t;
        int matched = 0;
        if (sscanf(line, "%127[^,],%7[^,],%7[^,],%d,%lf,%d,%lf,%lf,%15[^,],%lf",
                   name, sat, family, &toe_week, &toe_sow, &t_week, &t_sow,
                   &elevation, status, &expected) != 10) {
            fail("oracle", "malformed row");
            continue;
        }
        system = sat[0] == 'G' ? RTKLIB_SHARED_SYS_GPS : RTKLIB_SHARED_SYS_QZS;
        prn = (uint32_t)atoi(sat + 1) + (sat[0] == 'J' ? 192u : 0u);
        fam = !strcmp(family, "CNAV") ? RTKLIB_SHARED_NAV_CNAV :
              RTKLIB_SHARED_NAV_CNV2;
        for (i = 0; i < n && !matched; i++) {
            memset(&id, 0, sizeof(id));
            id.abi_version = RTKLIB_SHARED_ABI_VERSION;
            id.struct_size = (uint32_t)sizeof(id);
            if (rtklib_shared_nav_record_at(store, i, &id) ==
                    RTKLIB_SHARED_OK && id.system == system &&
                id.prn == prn && id.family == fam &&
                id.toe.week == toe_week && fabs(id.toe.sow - toe_sow) < 1E-3) {
                found = id;
                matched = 1;
            }
        }
        if (!matched) { fail("oracle", "record not enumerated"); continue; }
        t.week = t_week;
        t.sow = t_sow;
        stat = ura_query(store, system, prn, found.record_id, t, elevation,
                         &r);
        if (!strcmp(status, "AVAILABLE")) {
            if (stat != RTKLIB_SHARED_OK ||
                r.status != RTKLIB_SHARED_QUERY_AVAILABLE ||
                fabs(r.ura_m - expected) > ORACLE_TOL_M ||
                fabs(r.variance_m2 - r.ura_m * r.ura_m) > 1E-12 ||
                r.identity.record_id != found.record_id) {
                snprintf(detail, sizeof(detail),
                         "%s %s t=%d/%.0f E=%.0f stat=%d ura=%.9f exp=%.9f",
                         sat, family, t_week, t_sow, elevation, stat, r.ura_m,
                         expected);
                fail("oracle", detail);
            }
            available++;
        }
        else if (stat != RTKLIB_SHARED_UNAVAILABLE ||
                 r.status != RTKLIB_SHARED_QUERY_UNAVAILABLE) {
            fail("oracle", "expected UNAVAILABLE");
        }
        rows++;
    }
    fclose(fp);
    printf("real CNAV/CNV2 URA: %d vectors (%d available) match the "
           "specification oracle within %.0e m\n", rows, available,
           ORACLE_TOL_M);
    return rows > 0 && available > 0;
}

/* Build a receiver-injected copy of a real GPS CNAV record with chosen
 * accuracy indices. */
static void fill_input(const eph_t *e, rtklib_shared_eph_input_t *in)
{
    int w;
    memset(in, 0, sizeof(*in));
    in->abi_version = RTKLIB_SHARED_ABI_VERSION;
    in->struct_size = (uint32_t)sizeof(*in);
    in->system = RTKLIB_SHARED_SYS_GPS;
    satsys(e->sat, &w);
    in->prn = (uint32_t)w;
    in->family = (uint32_t)e->hdr.msg_type;
    in->iode = e->iode;
    in->iodc = e->iodc;
    in->health_raw = e->svh;
    in->code = e->code;
    in->flag = e->flag;
    in->broadcast_week = e->week;
    in->broadcast_toe_sow = e->toes;
    in->broadcast_transmit_sow = time2gpst(e->ttr, NULL);
    in->sva_m = e->sva;
    in->toe.sow = time2gpst(e->toe, &w); in->toe.week = w;
    in->toc.sow = time2gpst(e->toc, &w); in->toc.week = w;
    in->transmit_time.sow = time2gpst(e->ttr, &w); in->transmit_time.week = w;
    in->semi_major_axis_m = e->A;
    in->eccentricity = e->e;
    in->inclination_rad = e->i0;
    in->raan_rad = e->OMG0;
    in->arg_perigee_rad = e->omg;
    in->mean_anomaly_rad = e->M0;
    in->delta_n_rad_s = e->deln;
    in->raan_rate_rad_s = e->OMGd;
    in->inclination_rate_rad_s = e->idot;
    in->crc_m = e->crc; in->crs_m = e->crs;
    in->cuc_rad = e->cuc; in->cus_rad = e->cus;
    in->cic_rad = e->cic; in->cis_rad = e->cis;
    in->fit_interval_h = e->fit;
    in->clock_bias_s = e->f0;
    in->clock_drift_sps = e->f1;
    in->clock_drift_rate_sps2 = e->f2;
    memcpy(in->tgd_s, e->tgd, sizeof(in->tgd_s));
    memcpy(in->isc_s, e->isc, sizeof(in->isc_s));
    in->additional_rate_m_s = e->Adot;
    in->additional_mean_motion_rate_rad_s2 = e->ndot;
    in->delta_n0_raw = e->delta_n0;
    in->top_raw = e->top;
    in->delta_n0_dot_raw = e->delta_n0_dot;
    memcpy(in->urai_ned_raw, e->urai_ned, sizeof(in->urai_ned_raw));
    in->urai_ed_raw = e->urai_ed;
    in->wn_op_raw = e->wn_op;
    in->receive_order = 9001;
    strcpy(in->source_id, "test:issue20:spec");
    memcpy(in->family_subtype, e->hdr.subtype, sizeof(in->family_subtype));
    in->family_subtype[RTKLIB_SHARED_SUBTYPE_MAX - 1] = '\0';
}

typedef struct {
    const char *label;
    int ed, ned0, ned1, ned2;
    double dt_s, elevation_deg;
    int expect_available;
    double expected_ura_m; /* hand-derived from IS-GPS-200N */
} spec_case_t;

/* Expected values: X(N) = 2^(1+N/2) for N <= 6, 2^(N-2) for N >= 6, with
 * N = 1, 3, 5 rounded to 2.8, 5.7, 11.3; NED1 = 2^-(14+i) m/s; NED2 =
 * 2^-(28+i) m/s^2 applied to (dt - 93600)^2 only for dt > 93600 s; ED is
 * scaled by sin(E + 90 deg); the composite is the RSS. */
static const spec_case_t CASES[] = {
    /* E = 90 deg removes the ED term: URA = NED = X(0) = 2.0 */
    {"ned0=0 at t_op, zenith", 0, 0, 0, 0, 0.0, 90.0, 1, 2.0},
    /* E = 0: RSS(X(ED)=X(1)=2.8, X(0)=2.0) = sqrt(7.84 + 4) */
    {"rounded N=1 at horizon", 1, 0, 0, 0, 0.0, 0.0, 1, 3.4409301068170506},
    /* rounded N=3 and N=5 */
    {"rounded N=3 at horizon", 3, 0, 7, 7, 0.0, 0.0, 1,
     6.040695324215583},
    {"rounded N=5 zenith NED", 0, 5, 0, 0, 0.0, 90.0, 1, 11.3},
    /* N = 6 from both branches: 16 m; N = 14: 4096 m */
    {"N=6 zenith", 0, 6, 0, 0, 0.0, 90.0, 1, 16.0},
    {"N=14 zenith", 0, 14, 0, 0, 0.0, 90.0, 1, 4096.0},
    /* N = -3: 2^(-0.5); N = -15: 2^(-6.5) */
    {"N=-3 zenith", 0, -3, 0, 0, 0.0, 90.0, 1, 0.70710678118654752},
    {"N=-15 zenith", 0, -15, 0, 0, 0.0, 90.0, 1, 0.011048543456039806},
    /* NED1 index 0: 2^-14 m/s over 16384 s adds exactly 1 m */
    {"NED1 rate", 0, 0, 0, 0, 16384.0, 90.0, 1, 3.0},
    /* NED1 index 7: 2^-21 m/s over 93600 s */
    {"NED1 slow rate at breakpoint", 0, 0, 7, 0, 93600.0, 90.0, 1,
     2.0446319580078125},
    /* past the breakpoint: + 2^-28 * 16384^2 = 1 m */
    {"NED2 after breakpoint", 0, 0, 7, 0, 109984.0, 90.0, 1,
     3.0524444580078125},
    /* elevation 30 deg: sin(120 deg) = sqrt(3)/2; ED X(4)=8 -> 4*sqrt(3) */
    {"elevation factor", 4, 0, 0, 0, 0.0, 30.0, 1,
     7.2111025509279782},
    {"ED no prediction (15)", 15, 0, 0, 0, 0.0, 30.0, 0, 0.0},
    {"ED no prediction (-16)", -16, 0, 0, 0, 0.0, 30.0, 0, 0.0},
    {"NED0 no prediction (15)", 0, 15, 0, 0, 0.0, 30.0, 0, 0.0},
    {"before t_op", 0, 0, 0, 0, -1.0, 30.0, 0, 0.0},
};

static int check_spec_cases(const char *fixture)
{
    nav_t nav = {0};
    const eph_t *src = NULL;
    rtklib_shared_nav_store_t *store = rtklib_shared_nav_create();
    rtklib_shared_eph_input_t in;
    rtklib_shared_record_id_t id;
    rtklib_shared_modern_ura_result_t r;
    rtklib_shared_time_t t;
    char detail[256];
    size_t k;
    int i, prn, stat;

    if (!store || readrnx(fixture, 1, "", NULL, &nav, NULL) <= 0) {
        fail("spec", "setup");
        return 0;
    }
    for (i = 0; i < nav.n && !src; i++)
        if (satsys(nav.eph[i].sat, NULL) == SYS_GPS &&
            nav.eph[i].hdr.msg_type == NAV_CNAV) src = &nav.eph[i];
    if (!src) { fail("spec", "no GPS CNAV record"); return 0; }
    satsys(src->sat, &prn);
    for (k = 0; k < sizeof(CASES) / sizeof(CASES[0]); k++) {
        const spec_case_t *c = &CASES[k];
        double t_abs;
        fill_input(src, &in);
        in.urai_ed_raw = c->ed;
        in.urai_ned_raw[0] = c->ned0;
        in.urai_ned_raw[1] = c->ned1;
        in.urai_ned_raw[2] = c->ned2;
        in.receive_order = 9001 + k;
        if (rtklib_shared_nav_insert_eph(store, &in, &id) !=
            RTKLIB_SHARED_OK) {
            fail("spec", "insert rejected");
            continue;
        }
        t_abs = src->wn_op * 604800.0 + src->top + c->dt_s;
        t.week = (int32_t)floor(t_abs / 604800.0);
        t.sow = t_abs - t.week * 604800.0;
        stat = ura_query(store, RTKLIB_SHARED_SYS_GPS, (uint32_t)prn, id, t,
                         c->elevation_deg, &r);
        if (c->expect_available) {
            if (stat != RTKLIB_SHARED_OK ||
                fabs(r.ura_m - c->expected_ura_m) > 1E-12 * (1.0 + r.ura_m) ||
                fabs(r.elapsed_since_top_s - c->dt_s) > 1E-9 ||
                r.ura_ed_index != c->ed || r.ura_ned0_index != c->ned0 ||
                r.ura_ned1_index != c->ned1 || r.ura_ned2_index != c->ned2) {
                snprintf(detail, sizeof(detail), "%s: stat=%d ura=%.17g "
                         "expected=%.17g", c->label, stat, r.ura_m,
                         c->expected_ura_m);
                fail("spec", detail);
            }
        }
        else if (stat != RTKLIB_SHARED_UNAVAILABLE ||
                 r.status != RTKLIB_SHARED_QUERY_UNAVAILABLE ||
                 !isnan(r.ura_m) || r.identity.record_id != id) {
            snprintf(detail, sizeof(detail), "%s: stat=%d", c->label, stat);
            fail("spec", detail);
        }
    }

    /* contract edges */
    fill_input(src, &in);
    in.receive_order = 9901;
    in.wn_op_raw = src->wn_op - 186.0;
    if (rtklib_shared_nav_insert_eph(store, &in, &id) == RTKLIB_SHARED_OK) {
        t.sow = time2gpst(src->toe, &i);
        t.week = i;
        if (ura_query(store, RTKLIB_SHARED_SYS_GPS, (uint32_t)prn, id, t,
                      30.0, &r) != RTKLIB_SHARED_CALL_FAILED ||
            r.status != RTKLIB_SHARED_QUERY_FAILED)
            fail("edge", "inconsistent WN_op was not failed closed");
        if (ura_query(store, RTKLIB_SHARED_SYS_GPS, (uint32_t)prn, id, t,
                      91.0, &r) != RTKLIB_SHARED_INVALID_ARGUMENT)
            fail("edge", "elevation above 90 deg was accepted");
    }
    else fail("edge", "insert rejected");
    {
        rtklib_shared_state_query_t q;
        init_query(&q, RTKLIB_SHARED_SYS_GPS, (uint32_t)prn, id, t);
        memset(&r, 0, sizeof(r));
        r.abi_version = RTKLIB_SHARED_ABI_VERSION_1_0;
        r.struct_size = (uint32_t)sizeof(r);
        if (rtklib_shared_modern_ura_query(store, &q, 0.5, &r) !=
            RTKLIB_SHARED_INVALID_ARGUMENT)
            fail("edge", "ABI 1.0 result accepted by a 1.1 helper");
    }
    for (i = 0; i < nav.n; i++) {
        if (nav.eph[i].sat == src->sat && nav.eph[i].hdr.msg_type == NAV_LNAV) {
            eph_t lnav = nav.eph[i];
            fill_input(&lnav, &in);
            in.receive_order = 9950;
            if (rtklib_shared_nav_insert_eph(store, &in, &id) !=
                    RTKLIB_SHARED_OK ||
                ura_query(store, RTKLIB_SHARED_SYS_GPS, (uint32_t)prn, id,
                          in.toe, 30.0, &r) != RTKLIB_SHARED_UNSUPPORTED ||
                r.status != RTKLIB_SHARED_QUERY_UNSUPPORTED)
                fail("edge", "legacy LNAV record was not UNSUPPORTED");
            break;
        }
    }
    printf("specification cases: %d boundary vectors and 4 contract edges "
           "checked\n", (int)(sizeof(CASES) / sizeof(CASES[0])));
    freenav(&nav, 0x3ff);
    rtklib_shared_nav_destroy(store);
    return 1;
}

int main(int argc, char **argv)
{
    rtklib_shared_nav_store_t *store;
    int ok = 1;

    if (argc != 3) {
        fprintf(stderr, "usage: %s FIXTURE URA_ORACLE_CSV\n", argv[0]);
        return 2;
    }
    store = rtklib_shared_nav_create();
    if (!store || rtklib_shared_nav_load_rinex(store, argv[1], "",
                                               "fixture:issue20") !=
        RTKLIB_SHARED_OK) {
        fprintf(stderr, "FAIL: cannot load %s\n", argv[1]);
        return 1;
    }
    ok &= check_real_oracle(store, argv[2]);
    ok &= check_spec_cases(argv[1]);
    rtklib_shared_nav_destroy(store);
    if (!ok || failures) {
        fprintf(stderr, "modern_ura: FAIL (%d failures)\n", failures);
        return 1;
    }
    printf("modern_ura: PASS (GPS CNV2 real record: NOT_RUN, no verbatim "
           "BRD400 source available)\n");
    return 0;
}
