/*
 * Issue #27 through the public shared ABI.
 *
 * Usage: test_shared_modern_orbit FIXTURE ORACLE_CSV
 *
 * - BDS B-CNAV1/2/3 (MEO/IGSO), BDS D1/D2 (including BDS-3 GEO C59-C63) and
 *   GPS/QZSS LNAV states published by rtklib_shared_state_query() match the
 *   independent specification oracle.
 * - GPS/QZSS CNAV/CNV2: ABI 1.1 callers get the oracle state with an
 *   UNSUPPORTED variance metric; ABI 1.0 callers keep the #20 Phase A
 *   containment (UNSUPPORTED, identity preserved).
 * - A receiver-injected B-CNAV record carries Adot/ndot through the public
 *   input and reproduces the RINEX-loaded state bit-for-bit.
 * - B-CNAV records of BDS GEO satellites are contained (no real evidence).
 */

#include "../../src/rtklib.h"
#include "../../src/rtklib_shared_api.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ORACLE_POS_TOL_M 1E-3
#define ORACLE_CLK_TOL_S 1E-12

static int failures = 0;

static void fail(const char *what, const char *detail)
{
    fprintf(stderr, "FAIL: %s: %s\n", what, detail);
    failures++;
}

static uint32_t family_code(const char *name)
{
    if (!strcmp(name, "LNAV")) return RTKLIB_SHARED_NAV_LNAV;
    if (!strcmp(name, "CNAV")) return RTKLIB_SHARED_NAV_CNAV;
    if (!strcmp(name, "CNV1")) return RTKLIB_SHARED_NAV_CNV1;
    if (!strcmp(name, "CNV2")) return RTKLIB_SHARED_NAV_CNV2;
    if (!strcmp(name, "CNV3")) return RTKLIB_SHARED_NAV_CNV3;
    if (!strcmp(name, "D1")) return RTKLIB_SHARED_NAV_D1;
    if (!strcmp(name, "D2")) return RTKLIB_SHARED_NAV_D2;
    return 0;
}

/* One observation code per family so the family/code compatibility check
 * of the explicit-ID query accepts the record. */
static const char *family_signal(uint32_t system, uint32_t family)
{
    if (system == RTKLIB_SHARED_SYS_BDS) {
        if (family == RTKLIB_SHARED_NAV_CNV1) return "1P";
        if (family == RTKLIB_SHARED_NAV_CNV2) return "5P";
        if (family == RTKLIB_SHARED_NAV_CNV3) return "7D";
        return "2I";
    }
    if (family == RTKLIB_SHARED_NAV_CNV2) return "1L";
    if (family == RTKLIB_SHARED_NAV_CNAV) return "2S";
    return "1C";
}

static uint32_t public_system(char c)
{
    switch (c) {
        case 'G': return RTKLIB_SHARED_SYS_GPS;
        case 'J': return RTKLIB_SHARED_SYS_QZS;
        case 'C': return RTKLIB_SHARED_SYS_BDS;
        default: return 0;
    }
}

static int same_time(rtklib_shared_time_t a, int week, double sow)
{
    return a.week == week && fabs(a.sow - sow) < 1E-3;
}

static int code_for(uint32_t system, uint32_t prn, const char *signal,
                    uint8_t *code)
{
    rtklib_shared_signal_result_t result;
    memset(&result, 0, sizeof(result));
    result.abi_version = RTKLIB_SHARED_ABI_VERSION;
    result.struct_size = (uint32_t)sizeof(result);
    if (rtklib_shared_signal_query(system, prn, signal,
                                   RTKLIB_SHARED_GLO_FCN_UNKNOWN, NULL,
                                   &result) != RTKLIB_SHARED_OK) return 0;
    *code = result.rtklib_code;
    return 1;
}

static int query_as(const rtklib_shared_nav_store_t *store,
                    const rtklib_shared_record_identity_t *id,
                    rtklib_shared_time_t t, uint32_t abi_version,
                    rtklib_shared_state_result_t *out)
{
    rtklib_shared_state_query_t q;
    memset(&q, 0, sizeof(q));
    memset(out, 0, sizeof(*out));
    q.abi_version = RTKLIB_SHARED_ABI_VERSION;
    q.struct_size = (uint32_t)sizeof(q);
    q.system = id->system;
    q.prn = id->prn;
    q.glonass_fcn = RTKLIB_SHARED_GLO_FCN_UNKNOWN;
    q.evaluation_time = t;
    q.selection_time = t;
    q.selected_record_id = id->record_id;
    if (!code_for(id->system, id->prn, family_signal(id->system, id->family),
                  &q.rtklib_code)) return RTKLIB_SHARED_INVALID_ARGUMENT;
    out->abi_version = abi_version;
    out->struct_size = (uint32_t)sizeof(*out);
    return rtklib_shared_state_query(store, &q, out);
}

static int query(const rtklib_shared_nav_store_t *store,
                 const rtklib_shared_record_identity_t *id,
                 rtklib_shared_time_t t, rtklib_shared_state_result_t *out)
{
    return query_as(store, id, t, RTKLIB_SHARED_ABI_VERSION, out);
}

static const rtklib_shared_record_identity_t *find_record(
    const rtklib_shared_record_identity_t *ids, size_t n, uint32_t system,
    uint32_t prn, uint32_t family, int toe_week, double toe_sow)
{
    size_t i;
    for (i = 0; i < n; i++)
        if (ids[i].system == system && ids[i].prn == prn &&
            ids[i].family == family &&
            same_time(ids[i].toe, toe_week, toe_sow)) return &ids[i];
    return NULL;
}

static int check_oracle(const rtklib_shared_nav_store_t *store,
                        const rtklib_shared_record_identity_t *ids, size_t n,
                        const char *path)
{
    FILE *fp = fopen(path, "r");
    char line[512], sat[8], family[8], detail[256];
    int available = 0, contained = 0, geo = 0;

    if (!fp) { fail("oracle", "cannot open"); return 0; }
    if (!fgets(line, sizeof(line), fp)) { fclose(fp); return 0; }
    while (fgets(line, sizeof(line), fp)) {
        int toe_week, t_week, stat;
        double toe_sow, t_sow, x[3], clk;
        uint32_t system, prn, fam;
        const rtklib_shared_record_identity_t *id;
        rtklib_shared_state_result_t r;
        rtklib_shared_time_t t;
        if (sscanf(line, "%7[^,],%7[^,],%d,%lf,%d,%lf,%lf,%lf,%lf,%lf", sat,
                   family, &toe_week, &toe_sow, &t_week, &t_sow, &x[0], &x[1],
                   &x[2], &clk) != 10) {
            fail("oracle", "malformed row");
            continue;
        }
        system = public_system(sat[0]);
        prn = (uint32_t)atoi(sat + 1);
        if (sat[0] == 'J') prn += 192; /* public QZSS PRN is 193-202 */
        fam = family_code(family);
        id = find_record(ids, n, system, prn, fam, toe_week, toe_sow);
        if (!id) {
            snprintf(detail, sizeof(detail), "%s %s not enumerated", sat,
                     family);
            fail("oracle", detail);
            continue;
        }
        t.week = t_week;
        t.sow = t_sow;
        if ((system == RTKLIB_SHARED_SYS_GPS ||
             system == RTKLIB_SHARED_SYS_QZS) &&
            (fam == RTKLIB_SHARED_NAV_CNAV || fam == RTKLIB_SHARED_NAV_CNV2)) {
            stat = query_as(store, id, t, RTKLIB_SHARED_ABI_VERSION_1_0, &r);
            if (stat != RTKLIB_SHARED_UNSUPPORTED ||
                r.status != RTKLIB_SHARED_QUERY_UNSUPPORTED ||
                r.identity.record_id != id->record_id) {
                snprintf(detail, sizeof(detail),
                         "%s %s not contained for ABI 1.0 (stat=%d)", sat,
                         family, stat);
                fail("containment", detail);
            }
            contained++;
        }
        stat = query(store, id, t, &r);
        if (stat == RTKLIB_SHARED_OK &&
            (fam == RTKLIB_SHARED_NAV_CNAV || fam == RTKLIB_SHARED_NAV_CNV1 ||
             fam == RTKLIB_SHARED_NAV_CNV2 || fam == RTKLIB_SHARED_NAV_CNV3) &&
            (r.variance_status != RTKLIB_SHARED_QUERY_UNSUPPORTED ||
             !isnan(r.variance_m2))) {
            snprintf(detail, sizeof(detail), "%s %s variance published", sat,
                     family);
            fail("variance", detail);
        }
        if (stat != RTKLIB_SHARED_OK || !r.state_valid ||
            fabs(r.position_ecef_m[0] - x[0]) > ORACLE_POS_TOL_M ||
            fabs(r.position_ecef_m[1] - x[1]) > ORACLE_POS_TOL_M ||
            fabs(r.position_ecef_m[2] - x[2]) > ORACLE_POS_TOL_M ||
            fabs(r.clock_bias_s - clk) > ORACLE_CLK_TOL_S) {
            snprintf(detail, sizeof(detail),
                     "%s %s t=%d/%.1f stat=%d dpos=(%.4f,%.4f,%.4f)", sat,
                     family, t_week, t_sow, stat, r.position_ecef_m[0] - x[0],
                     r.position_ecef_m[1] - x[1], r.position_ecef_m[2] - x[2]);
            fail("oracle", detail);
        }
        available++;
        if (system == RTKLIB_SHARED_SYS_BDS && prn >= 59) geo++;
    }
    fclose(fp);
    printf("public oracle: %d ABI 1.1 states match (%d BDS-3 GEO D2); "
           "%d GPS/QZSS CNAV/CNV2 contained for ABI 1.0 callers\n",
           available, geo, contained);
    return available > 0 && geo > 0 && contained > 0;
}

static void fill_input(const eph_t *e, uint32_t prn,
                       rtklib_shared_eph_input_t *in)
{
    int w;
    memset(in, 0, sizeof(*in));
    in->abi_version = RTKLIB_SHARED_ABI_VERSION;
    in->struct_size = (uint32_t)sizeof(*in);
    in->system = RTKLIB_SHARED_SYS_BDS;
    in->prn = prn;
    in->family = (uint32_t)e->hdr.msg_type;
    in->iode = e->iode;
    in->iodc = e->iodc;
    in->health_raw = e->svh;
    in->code = e->code;
    in->flag = e->flag;
    in->broadcast_week = e->week;
    in->broadcast_toe_sow = e->toes;
    in->broadcast_transmit_sow = time2bdt(gpst2bdt(e->ttr), NULL);
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
    memcpy(in->sisai_raw, e->sisai, sizeof(in->sisai_raw));
    in->int_flag_raw = e->int_flag;
    in->receive_order = 7001;
    strcpy(in->source_id, "test:issue27:receiver");
    memcpy(in->family_subtype, e->hdr.subtype, sizeof(in->family_subtype));
    in->family_subtype[RTKLIB_SHARED_SUBTYPE_MAX - 1] = '\0';
}

static int check_injected(const char *fixture,
                          const rtklib_shared_nav_store_t *loaded,
                          const rtklib_shared_record_identity_t *ids,
                          size_t n)
{
    nav_t nav = {0};
    const eph_t *src = NULL;
    rtklib_shared_nav_store_t *store = rtklib_shared_nav_create();
    rtklib_shared_eph_input_t in;
    rtklib_shared_record_identity_t id;
    rtklib_shared_record_id_t meo_id = 0, geo_id = 0;
    rtklib_shared_state_result_t a, b, g;
    const rtklib_shared_record_identity_t *ref;
    rtklib_shared_time_t t;
    int i, w, ok = 1;

    if (!store || readrnx(fixture, 1, "", NULL, &nav, NULL) <= 0) {
        fail("inject", "setup");
        return 0;
    }
    for (i = 0; i < nav.n && !src; i++) {
        int prn;
        if (satsys(nav.eph[i].sat, &prn) == SYS_CMP && prn == 19 &&
            nav.eph[i].hdr.msg_type == NAV_CNV1) src = &nav.eph[i];
    }
    if (!src || src->Adot == 0.0 || src->ndot == 0.0) {
        fail("inject", "no C19 CNV1 record with nonzero rate terms");
        freenav(&nav, 0x3ff);
        rtklib_shared_nav_destroy(store);
        return 0;
    }
    fill_input(src, 19, &in);
    if (rtklib_shared_nav_insert_eph(store, &in, &meo_id) != RTKLIB_SHARED_OK)
        fail("inject", "C19 CNV1 insert rejected");
    fill_input(src, 59, &in);
    if (rtklib_shared_nav_insert_eph(store, &in, &geo_id) != RTKLIB_SHARED_OK)
        fail("inject", "C59 CNV1 insert rejected");

    t.sow = time2gpst(timeadd(src->toe, 3600.0), &w);
    t.week = w;
    ref = find_record(ids, n, RTKLIB_SHARED_SYS_BDS, 19,
                      RTKLIB_SHARED_NAV_CNV1, w, time2gpst(src->toe, NULL));
    memset(&id, 0, sizeof(id));
    id.abi_version = RTKLIB_SHARED_ABI_VERSION;
    id.struct_size = (uint32_t)sizeof(id);
    if (!ref || rtklib_shared_nav_record(store, meo_id, &id) !=
        RTKLIB_SHARED_OK) {
        fail("inject", "reference record missing");
        ok = 0;
    }
    else if (query(loaded, ref, t, &a) != RTKLIB_SHARED_OK ||
             query(store, &id, t, &b) != RTKLIB_SHARED_OK ||
             memcmp(a.position_ecef_m, b.position_ecef_m,
                    sizeof(a.position_ecef_m)) ||
             a.clock_bias_s != b.clock_bias_s) {
        fail("inject", "receiver-injected B-CNAV state differs from RINEX");
        ok = 0;
    }
    if (rtklib_shared_nav_record(store, geo_id, &id) != RTKLIB_SHARED_OK ||
        query(store, &id, t, &g) != RTKLIB_SHARED_UNSUPPORTED ||
        g.status != RTKLIB_SHARED_QUERY_UNSUPPORTED || g.state_valid ||
        g.identity.record_id != geo_id || g.identity.prn != 59) {
        fail("inject", "BDS GEO B-CNAV state was not contained");
        ok = 0;
    }
    printf("public injection: B-CNAV Adot/ndot carried bit-exactly; BDS GEO "
           "B-CNAV contained\n");
    freenav(&nav, 0x3ff);
    rtklib_shared_nav_destroy(store);
    return ok;
}

int main(int argc, char **argv)
{
    rtklib_shared_nav_store_t *store;
    rtklib_shared_record_identity_t *ids;
    size_t n, i;
    int ok = 1;

    if (argc != 3) {
        fprintf(stderr, "usage: %s FIXTURE ORACLE_CSV\n", argv[0]);
        return 2;
    }
    store = rtklib_shared_nav_create();
    if (!store || rtklib_shared_nav_load_rinex(store, argv[1], "",
                                               "fixture:issue27") !=
        RTKLIB_SHARED_OK) {
        fprintf(stderr, "FAIL: cannot load %s\n", argv[1]);
        return 1;
    }
    n = rtklib_shared_nav_record_count(store, 0, 0);
    ids = (rtklib_shared_record_identity_t *)calloc(n, sizeof(*ids));
    for (i = 0; ids && i < n; i++) {
        ids[i].abi_version = RTKLIB_SHARED_ABI_VERSION;
        ids[i].struct_size = (uint32_t)sizeof(ids[i]);
        if (rtklib_shared_nav_record_at(store, i, &ids[i]) != RTKLIB_SHARED_OK)
            fail("enumerate", "record_at failed");
    }
    ok &= ids != NULL && check_oracle(store, ids, n, argv[2]);
    ok &= ids != NULL && check_injected(argv[1], store, ids, n);
    free(ids);
    rtklib_shared_nav_destroy(store);
    if (!ok || failures) {
        fprintf(stderr, "shared_modern_orbit: FAIL (%d failures)\n", failures);
        return 1;
    }
    printf("shared_modern_orbit: PASS\n");
    return 0;
}
