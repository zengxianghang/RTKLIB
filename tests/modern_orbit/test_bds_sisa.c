/* BDS B-CNAV1/2 SISA public API against independent BRD400 vectors. */
#include "../../src/rtklib_shared_api.h"
#include "../../src/rtklib.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

static void fail(const char *message)
{
    fprintf(stderr, "FAIL: %s\n", message);
    failures++;
}

static uint32_t family_of(const char *name)
{
    if (!strcmp(name, "CNV1")) return RTKLIB_SHARED_NAV_CNV1;
    if (!strcmp(name, "CNV2")) return RTKLIB_SHARED_NAV_CNV2;
    return 0;
}

static uint8_t code_for(uint32_t prn, uint32_t family)
{
    rtklib_shared_signal_result_t signal = {0};
    signal.abi_version = RTKLIB_SHARED_ABI_VERSION;
    signal.struct_size = (uint32_t)sizeof(signal);
    if (rtklib_shared_signal_query(RTKLIB_SHARED_SYS_BDS, prn,
            family == RTKLIB_SHARED_NAV_CNV1 ? "1P" :
            family == RTKLIB_SHARED_NAV_CNV2 ? "5P" : "7D",
            RTKLIB_SHARED_GLO_FCN_UNKNOWN, NULL, &signal) !=
        RTKLIB_SHARED_OK) return 0;
    return signal.rtklib_code;
}

static void init_query(rtklib_shared_state_query_t *q, uint32_t prn,
                       uint32_t family, uint8_t code,
                       rtklib_shared_record_id_t id,
                       rtklib_shared_time_t time)
{
    memset(q, 0, sizeof(*q));
    q->abi_version = RTKLIB_SHARED_ABI_VERSION;
    q->struct_size = (uint32_t)sizeof(*q);
    q->system = RTKLIB_SHARED_SYS_BDS;
    q->prn = prn;
    q->family_mask = family;
    q->rtklib_code = code;
    q->glonass_fcn = RTKLIB_SHARED_GLO_FCN_UNKNOWN;
    q->evaluation_time = time;
    q->selection_time = time;
    q->selected_record_id = id;
}

static int real_vectors(const rtklib_shared_nav_store_t *store,
                        const char *path)
{
    FILE *fp = fopen(path, "r");
    char line[512], sat[8], family[8], expected_status[16], detail[256];
    int rows = 0, available = 0, unavailable = 0;
    size_t n = rtklib_shared_nav_record_count(store, 0, 0), i;

    if (!fp) { fail("cannot open SISA oracle"); return 0; }
    if (!fgets(line, sizeof(line), fp)) { fclose(fp); return 0; }
    while (fgets(line, sizeof(line), fp)) {
        int toe_week, transmit_week, t_week, oe, ocb, oc1, oc2, sismai;
        int stat, found = 0;
        double toe_sow, transmit_sow, t_sow, expected;
        uint32_t prn, family_code;
        uint8_t code;
        rtklib_shared_record_identity_t identity = {0}, selected = {0};
        rtklib_shared_state_query_t query;
        rtklib_shared_bds_sisa_result_t result = {0};
        rtklib_shared_time_t time;
        if (sscanf(line, "%7[^,],%7[^,],%d,%lf,%d,%lf,%d,%lf,%d,%d,%d,%d,%d,%15[^,],%lf",
                   sat, family, &toe_week, &toe_sow, &transmit_week,
                   &transmit_sow, &t_week, &t_sow, &oe, &ocb, &oc1, &oc2,
                   &sismai, expected_status, &expected) != 15) {
            fail("malformed SISA oracle row");
            continue;
        }
        prn = (uint32_t)atoi(sat + 1);
        family_code = family_of(family);
        code = code_for(prn, family_code);
        if (!family_code || !code) { fail("unsupported oracle signal"); continue; }
        for (i = 0; i < n; i++) {
            memset(&identity, 0, sizeof(identity));
            identity.abi_version = RTKLIB_SHARED_ABI_VERSION;
            identity.struct_size = (uint32_t)sizeof(identity);
            if (rtklib_shared_nav_record_at(store, i, &identity) ==
                    RTKLIB_SHARED_OK &&
                identity.system == RTKLIB_SHARED_SYS_BDS &&
                identity.prn == prn && identity.family == family_code &&
                identity.toe.week == toe_week &&
                fabs(identity.toe.sow - toe_sow) < 1E-3 &&
                identity.transmit_time.week == transmit_week &&
                fabs(identity.transmit_time.sow - transmit_sow) < 1E-3) {
                selected = identity;
                found = 1;
                break;
            }
        }
        if (!found) { fail("oracle record not enumerated"); continue; }
        time.week = t_week;
        time.sow = t_sow;
        init_query(&query, prn, family_code, code, selected.record_id, time);
        result.abi_version = RTKLIB_SHARED_ABI_VERSION;
        result.struct_size = (uint32_t)sizeof(result);
        stat = rtklib_shared_bds_sisa_query(store, &query, &result);
        if (result.identity.record_id != selected.record_id ||
            result.sisma_status != RTKLIB_SHARED_QUERY_UNSUPPORTED ||
            result.sisai_oe_index != oe ||
            result.sisai_ocb_index != ocb ||
            result.sisai_oc1_index != oc1 ||
            result.sisai_oc2_index != oc2 ||
            result.sismai_index != sismai) {
            fail("raw index or identity mismatch");
        }
        if (!strcmp(expected_status, "AVAILABLE")) {
            if (stat != RTKLIB_SHARED_OK ||
                result.status != RTKLIB_SHARED_QUERY_AVAILABLE ||
                !isfinite(result.sisa_m) ||
                fabs(result.sisa_m - expected) > 1E-8) {
                snprintf(detail, sizeof(detail),
                         "%s %s SISA %.12g expected %.12g (stat %d)",
                         sat, family, result.sisa_m, expected, stat);
                fail(detail);
            }
            available++;
        }
        else {
            if (stat != RTKLIB_SHARED_UNAVAILABLE ||
                result.status != RTKLIB_SHARED_QUERY_UNAVAILABLE ||
                !isnan(result.sisa_m)) fail("expected unavailable SISA");
            unavailable++;
        }
        rows++;
    }
    fclose(fp);
    printf("real BRD400 SISA: %d vectors, %d available, %d unavailable\n",
           rows, available, unavailable);
    return rows > 0 && available > 0 && unavailable > 0;
}

static void fill_input(const eph_t *e, uint32_t prn,
                       rtklib_shared_eph_input_t *in)
{
    int week;
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
    in->toe.sow = time2gpst(e->toe, &week); in->toe.week = week;
    in->toc.sow = time2gpst(e->toc, &week); in->toc.week = week;
    in->transmit_time.sow = time2gpst(e->ttr, &week);
    in->transmit_time.week = week;
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
    memcpy(in->sisai_raw, e->sisai, sizeof(in->sisai_raw));
    in->int_flag_raw = e->int_flag;
    in->receive_order = 7001;
    strcpy(in->source_id, "test:issue30:receiver");
    memcpy(in->family_subtype, e->hdr.subtype, sizeof(in->family_subtype));
    in->family_subtype[RTKLIB_SHARED_SUBTYPE_MAX - 1] = '\0';
}

static int contract_edges(const char *fixture,
                          rtklib_shared_nav_store_t *store)
{
    nav_t nav = {0};
    const eph_t *src = NULL;
    rtklib_shared_eph_input_t in;
    rtklib_shared_record_id_t id;
    rtklib_shared_state_query_t query;
    rtklib_shared_bds_sisa_result_t result;
    rtklib_shared_time_t time;
    uint8_t code = code_for(19, RTKLIB_SHARED_NAV_CNV2);
    int i, prn = 0;
    double expected;

    if (!code || readrnx(fixture, 1, "", NULL, &nav, NULL) <= 0) {
        fail("cannot load private edge fixture");
        return 0;
    }
    for (i = 0; i < nav.n && !src; i++) {
        if (satsys(nav.eph[i].sat, &prn) == SYS_CMP &&
            nav.eph[i].hdr.msg_type == NAV_CNV2 &&
            nav.eph[i].sisai[2] >= 0 && nav.eph[i].sisai[3] >= 0)
            src = &nav.eph[i];
    }
    if (!src) {
        fail("no complete B-CNAV2 record");
        freenav(&nav, 0x3ff);
        return 0;
    }
    fill_input(src, (uint32_t)prn, &in);
    in.sisai_raw[0] = -15;
    in.sisai_raw[1] = 14;
    in.sisai_raw[2] = 0;
    in.sisai_raw[3] = 0;
    in.sva_m = 15; /* SISMAI is still an index, not metres. */
    in.top_raw = in.broadcast_toe_sow;
    time = in.toe;
    if (rtklib_shared_nav_insert_eph(store, &in, &id) != RTKLIB_SHARED_OK) {
        fail("edge insertion failed");
        freenav(&nav, 0x3ff);
        return 0;
    }
    init_query(&query, (uint32_t)prn, RTKLIB_SHARED_NAV_CNV2,
               code_for((uint32_t)prn, RTKLIB_SHARED_NAV_CNV2), id, time);
    memset(&result, 0, sizeof(result));
    result.abi_version = RTKLIB_SHARED_ABI_VERSION;
    result.struct_size = (uint32_t)sizeof(result);
    expected = hypot(0.01 * sin(14.0 * D2R), 6144.0);
    if (rtklib_shared_bds_sisa_query(store, &query, &result) !=
            RTKLIB_SHARED_OK || fabs(result.sisa_m - expected) > 1E-9 ||
        result.sisma_status != RTKLIB_SHARED_QUERY_UNSUPPORTED ||
        result.sismai_index != 15) fail("SISA bound or SISMAI edge");
    query.evaluation_time.sow -= 1.0;
    if (rtklib_shared_bds_sisa_query(store, &query, &result) !=
            RTKLIB_SHARED_UNAVAILABLE || !isnan(result.sisa_m))
        fail("pre-top SISA was published");
    query.evaluation_time = time;
    result.abi_version = (1u << 16) | 2u;
    result.struct_size = (uint32_t)sizeof(result);
    if (rtklib_shared_bds_sisa_query(store, &query, &result) !=
        RTKLIB_SHARED_INVALID_ARGUMENT) fail("ABI 1.2 accepted");

    in.receive_order++;
    in.sva_m = 99; /* Unrelated SISMAI must not suppress valid SISA. */
    if (rtklib_shared_nav_insert_eph(store, &in, &id) != RTKLIB_SHARED_OK)
        fail("malformed SISMAI insertion failed");
    else {
        query.selected_record_id = id;
        result.abi_version = RTKLIB_SHARED_ABI_VERSION;
        if (rtklib_shared_bds_sisa_query(store, &query, &result) !=
                RTKLIB_SHARED_OK ||
            fabs(result.sisa_m - expected) > 1E-9 ||
            result.sismai_index != -1 ||
            result.sisma_status != RTKLIB_SHARED_QUERY_UNSUPPORTED)
            fail("malformed SISMAI suppressed SISA");
    }

    in.receive_order++;
    in.sisai_raw[0] = 15;
    if (rtklib_shared_nav_insert_eph(store, &in, &id) != RTKLIB_SHARED_OK)
        fail("no-prediction insertion failed");
    else {
        query.selected_record_id = id;
        result.abi_version = RTKLIB_SHARED_ABI_VERSION;
        if (rtklib_shared_bds_sisa_query(store, &query, &result) !=
                RTKLIB_SHARED_UNAVAILABLE || !isnan(result.sisa_m))
            fail("SISA no-prediction index accepted");
    }
    in.receive_order++;
    in.sisai_raw[0] = 0;
    in.sisai_raw[2] = 8;
    if (rtklib_shared_nav_insert_eph(store, &in, &id) != RTKLIB_SHARED_OK)
        fail("invalid-index insertion failed");
    else {
        query.selected_record_id = id;
        result.abi_version = RTKLIB_SHARED_ABI_VERSION;
        if (rtklib_shared_bds_sisa_query(store, &query, &result) !=
                RTKLIB_SHARED_CALL_FAILED || !isnan(result.sisa_m))
            fail("out-of-range rate index accepted");
    }
    in.receive_order++;
    in.prn = 59;
    in.sisai_raw[2] = 0;
    if (rtklib_shared_nav_insert_eph(store, &in, &id) != RTKLIB_SHARED_OK)
        fail("GEO insertion failed");
    else {
        init_query(&query, 59, RTKLIB_SHARED_NAV_CNV2,
                   code_for(59, RTKLIB_SHARED_NAV_CNV2), id, time);
        result.abi_version = RTKLIB_SHARED_ABI_VERSION;
        if (rtklib_shared_bds_sisa_query(store, &query, &result) !=
                RTKLIB_SHARED_UNSUPPORTED || !isnan(result.sisa_m))
            fail("GEO B-CNAV SISA was published");
    }
    {
        size_t n = rtklib_shared_nav_record_count(store, 0, 0), index;
        int checked = 0;
        for (index = 0; index < n; index++) {
            rtklib_shared_record_identity_t identity = {0};
            identity.abi_version = RTKLIB_SHARED_ABI_VERSION;
            identity.struct_size = (uint32_t)sizeof(identity);
            if (rtklib_shared_nav_record_at(store, index, &identity) !=
                    RTKLIB_SHARED_OK ||
                identity.system != RTKLIB_SHARED_SYS_BDS ||
                identity.family != RTKLIB_SHARED_NAV_CNV3) continue;
            init_query(&query, identity.prn, identity.family,
                       code_for(identity.prn, identity.family),
                       identity.record_id, identity.toe);
            result.abi_version = RTKLIB_SHARED_ABI_VERSION;
            if (rtklib_shared_bds_sisa_query(store, &query, &result) !=
                    RTKLIB_SHARED_UNSUPPORTED ||
                result.status != RTKLIB_SHARED_QUERY_UNSUPPORTED ||
                result.identity.record_id != identity.record_id)
                fail("B-CNAV3 SISA was published");
            checked = 1;
            break;
        }
        if (!checked) fail("no real B-CNAV3 record checked");
    }
    freenav(&nav, 0x3ff);
    return 1;
}

int main(int argc, char **argv)
{
    rtklib_shared_nav_store_t *store;
    int ok;
    if (argc != 3) {
        fprintf(stderr, "usage: %s FIXTURE SISA_ORACLE_CSV\n", argv[0]);
        return 2;
    }
    store = rtklib_shared_nav_create();
    if (!store || rtklib_shared_nav_load_rinex(store, argv[1], "",
                                               "fixture:issue30") !=
        RTKLIB_SHARED_OK) {
        fail("cannot load BRD400 fixture");
        return 1;
    }
    ok = real_vectors(store, argv[2]);
    ok &= contract_edges(argv[1], store);
    rtklib_shared_nav_destroy(store);
    if (!ok || failures) {
        fprintf(stderr, "bds_sisa: FAIL (%d failures)\n", failures);
        return 1;
    }
    puts("bds_sisa: PASS (SISMAI mapping and B-CNAV3: UNSUPPORTED)");
    return 0;
}
