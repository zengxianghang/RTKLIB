/*
 * Offline regression test for the versioned shared NAV ABI.
 *
 * The RINEX excerpts in fixtures/ consist only of complete, verbatim records
 * copied from their documented real sources.  Their byte ranges, source
 * hashes and selection criteria are in the adjacent provenance files.  This
 * test intentionally uses rtklib.h only in this C oracle to obtain the exact
 * decoded record for insertion and bias/state comparisons.  A downstream
 * consumer must use the public header alone; test_public_c.c and
 * test_public_cpp.cpp enforce that boundary.
 */

#include "../../src/rtklib_shared_api.h"
#include "../../src/rtklib.h"
#include "../../src/rtklib_obs_ext.h"
#include "../../src/rtklib_signal_bias_ext.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FIXTURE_SOURCE_ID "fixture:BRD400DLR:2025001"
#define RECEIVER_SOURCE_ID "receiver:private-oracle"
#define MAX_RECORDS 128

static int fail_at(const char *file, int line, const char *message)
{
    fprintf(stderr, "FAIL %s:%d: %s\n", file, line, message);
    return 1;
}

#define CHECK(condition, message) \
    do { if (!(condition)) return fail_at(__FILE__, __LINE__, message); } \
    while (0)

static uint32_t public_system(int system)
{
    switch (system) {
    case SYS_GPS: return RTKLIB_SHARED_SYS_GPS;
    case SYS_GLO: return RTKLIB_SHARED_SYS_GLO;
    case SYS_GAL: return RTKLIB_SHARED_SYS_GAL;
    case SYS_QZS: return RTKLIB_SHARED_SYS_QZS;
    case SYS_CMP: return RTKLIB_SHARED_SYS_BDS;
    case SYS_SBS: return RTKLIB_SHARED_SYS_SBS;
    default: return 0;
    }
}

static rtklib_shared_time_t shared_time(gtime_t time)
{
    rtklib_shared_time_t result;
    int week = 0;
    result.sow = time2gpst(time, &week);
    result.week = (int32_t)week;
    return result;
}

static double time_difference(rtklib_shared_time_t first,
                              rtklib_shared_time_t second)
{
    gtime_t a = gpst2time(first.week, first.sow);
    gtime_t b = gpst2time(second.week, second.sow);
    return timediff(a, b);
}

static int same_time(rtklib_shared_time_t first,
                     rtklib_shared_time_t second)
{
    return first.week == second.week &&
           fabs(first.sow - second.sow) < 1E-6;
}

static int bytes_are(const void *data, size_t size, unsigned char value)
{
    const unsigned char *bytes = (const unsigned char *)data;
    size_t i;
    for (i = 0; i < size; ++i) {
        if (bytes[i] != value) return 0;
    }
    return 1;
}

static const char *short_fixture_path(const char *fallback)
{
    static const char *candidates[] = {
        "fixtures/brd400_selected.rnx",
        "tests/rtklib_shared_api/fixtures/brd400_selected.rnx"
    };
    size_t i;
    FILE *file;

    for (i = 0; i < sizeof(candidates) / sizeof(candidates[0]); ++i) {
        file = fopen(candidates[i], "rb");
        if (file) {
            fclose(file);
            return candidates[i];
        }
    }
    return fallback;
}

/* This deliberately contains only the two ABI header words plus a canary.
 * Passing it as a result object must be safe when struct_size is short: the
 * implementation must validate before it writes any result byte. */
typedef struct {
    uint32_t abi_version;
    uint32_t struct_size;
    unsigned char canary[64];
} short_result_buffer_t;

static void init_identity(rtklib_shared_record_identity_t *identity)
{
    memset(identity, 0, sizeof(*identity));
    identity->abi_version = RTKLIB_SHARED_ABI_VERSION;
    identity->struct_size = (uint32_t)sizeof(*identity);
}

static void init_state_query(rtklib_shared_state_query_t *query,
                             uint32_t system, uint32_t prn, uint32_t family,
                             unsigned char code,
                             rtklib_shared_time_t time,
                             rtklib_shared_record_id_t selected)
{
    memset(query, 0, sizeof(*query));
    query->abi_version = RTKLIB_SHARED_ABI_VERSION;
    query->struct_size = (uint32_t)sizeof(*query);
    query->system = system;
    query->prn = prn;
    query->rtklib_code = code;
    query->glonass_fcn = RTKLIB_SHARED_GLO_FCN_UNKNOWN;
    query->family_mask = family;
    query->evaluation_time = time;
    query->selection_time = time;
    query->selected_record_id = selected;
}

static void init_state_result(rtklib_shared_state_result_t *result)
{
    memset(result, 0, sizeof(*result));
    result->abi_version = RTKLIB_SHARED_ABI_VERSION;
    result->struct_size = (uint32_t)sizeof(*result);
}

static void init_bias_result(rtklib_shared_bias_result_t *result)
{
    memset(result, 0, sizeof(*result));
    result->abi_version = RTKLIB_SHARED_ABI_VERSION;
    result->struct_size = (uint32_t)sizeof(*result);
}

static void init_ion_result(rtklib_shared_ion_result_t *result)
{
    memset(result, 0, sizeof(*result));
    result->abi_version = RTKLIB_SHARED_ABI_VERSION;
    result->struct_size = (uint32_t)sizeof(*result);
}

static void copy_subtype(char destination[RTKLIB_SHARED_SUBTYPE_MAX],
                         const char source[RTKLIB_SHARED_SUBTYPE_MAX])
{
    memcpy(destination, source, RTKLIB_SHARED_SUBTYPE_MAX);
    destination[RTKLIB_SHARED_SUBTYPE_MAX - 1] = '\0';
}

static int eph_prn(const eph_t *eph)
{
    int prn = 0;
    (void)satsys(eph->sat, &prn);
    return prn > 0 ? prn : eph->hdr.prn;
}

static int geph_prn(const geph_t *geph)
{
    int prn = 0;
    (void)satsys(geph->sat, &prn);
    return prn > 0 ? prn : geph->hdr.prn;
}

static void fill_eph_input(const eph_t *source,
                           rtklib_shared_eph_input_t *input)
{
    int system = satsys(source->sat, NULL);
    double native_transmit_sow;

    memset(input, 0, sizeof(*input));
    input->abi_version = RTKLIB_SHARED_ABI_VERSION;
    input->struct_size = (uint32_t)sizeof(*input);
    input->system = public_system(system);
    input->prn = (uint32_t)eph_prn(source);
    input->family = (uint32_t)source->hdr.msg_type;
    input->iode = source->iode;
    input->iodc = source->iodc;
    input->health_raw = source->svh;
    input->code = source->code;
    input->flag = source->flag;
    input->broadcast_week = source->week;
    input->broadcast_toe_sow = source->toes;
    if (system == SYS_CMP)
        native_transmit_sow = time2bdt(gpst2bdt(source->ttr), NULL);
    else
        native_transmit_sow = time2gpst(source->ttr, NULL);
    input->broadcast_transmit_sow = native_transmit_sow;
    input->sva_m = source->sva;
    input->toe = shared_time(source->toe);
    input->toc = shared_time(source->toc);
    input->transmit_time = shared_time(source->ttr);
    input->semi_major_axis_m = source->A;
    input->eccentricity = source->e;
    input->inclination_rad = source->i0;
    input->raan_rad = source->OMG0;
    input->arg_perigee_rad = source->omg;
    input->mean_anomaly_rad = source->M0;
    input->delta_n_rad_s = source->deln;
    input->raan_rate_rad_s = source->OMGd;
    input->inclination_rate_rad_s = source->idot;
    input->crc_m = source->crc;
    input->crs_m = source->crs;
    input->cuc_rad = source->cuc;
    input->cus_rad = source->cus;
    input->cic_rad = source->cic;
    input->cis_rad = source->cis;
    input->fit_interval_h = source->fit;
    input->clock_bias_s = source->f0;
    input->clock_drift_sps = source->f1;
    input->clock_drift_rate_sps2 = source->f2;
    memcpy(input->tgd_s, source->tgd, sizeof(input->tgd_s));
    memcpy(input->isc_s, source->isc, sizeof(input->isc_s));
    input->additional_rate_m_s = source->Adot;
    input->additional_mean_motion_rate_rad_s2 = source->ndot;
    input->delta_n0_raw = source->delta_n0;
    input->top_raw = source->top;
    input->delta_n0_dot_raw = source->delta_n0_dot;
    memcpy(input->urai_ned_raw, source->urai_ned,
           sizeof(input->urai_ned_raw));
    input->urai_ed_raw = source->urai_ed;
    input->wn_op_raw = source->wn_op;
    memcpy(input->sisai_raw, source->sisai, sizeof(input->sisai_raw));
    input->int_flag_raw = source->int_flag;
    input->ura_index = 0.0;
    input->receive_order = 1001;
    strcpy(input->source_id, RECEIVER_SOURCE_ID);
    copy_subtype(input->family_subtype, source->hdr.subtype);
}

static void fill_glo_input(const geph_t *source,
                           rtklib_shared_glo_eph_input_t *input)
{
    memset(input, 0, sizeof(*input));
    input->abi_version = RTKLIB_SHARED_ABI_VERSION;
    input->struct_size = (uint32_t)sizeof(*input);
    input->system = RTKLIB_SHARED_SYS_GLO;
    input->prn = (uint32_t)geph_prn(source);
    input->family = (uint32_t)source->hdr.msg_type;
    input->iode = source->iode;
    input->health_raw = source->svh;
    input->glonass_fcn = source->frq;
    input->sva = source->sva;
    input->age = source->age;
    input->data_validity = source->data_validity;
    input->flags = source->flag;
    input->health_flags = source->svhflag;
    input->toe = shared_time(source->toe);
    input->transmit_time = shared_time(source->tof);
    memcpy(input->position_ecef_m, source->pos, sizeof(input->position_ecef_m));
    memcpy(input->velocity_ecef_mps, source->vel, sizeof(input->velocity_ecef_mps));
    memcpy(input->acceleration_ecef_mps2, source->acc,
           sizeof(input->acceleration_ecef_mps2));
    input->clock_bias_s = -source->taun;
    input->relative_frequency_bias = source->gamn;
    input->beta = source->beta;
    input->dtaun_s = source->dtaun;
    input->tgd_l2ocp_s = source->tgd_l2ocp;
    input->isc_l3ocp_s = source->isc_l3ocp;
    memcpy(input->antenna_phase_center_offset_m, source->pc,
           sizeof(input->antenna_phase_center_offset_m));
    input->raw_transmit_sow = source->ttm;
    input->receive_order = 1002;
    strcpy(input->source_id, RECEIVER_SOURCE_ID);
    copy_subtype(input->family_subtype, source->hdr.subtype);
}

static void fill_ion_input(const ion_t *source,
                           rtklib_shared_ion_input_t *input)
{
    size_t count = source->ndata < 32 ? (size_t)source->ndata : 32;

    memset(input, 0, sizeof(*input));
    input->abi_version = RTKLIB_SHARED_ABI_VERSION;
    input->struct_size = (uint32_t)sizeof(*input);
    input->system = public_system(source->hdr.sys);
    input->family = source->hdr.msg_type ? (uint32_t)source->hdr.msg_type :
                    RTKLIB_SHARED_NAV_LNAV;
    input->transmit_time = shared_time(source->trans_time);
    input->value_count = (uint32_t)count;
    memcpy(input->values, source->data, count * sizeof(input->values[0]));
    memcpy(input->present, source->present, count * sizeof(input->present[0]));
    input->receive_order = 1003;
    strcpy(input->source_id, RECEIVER_SOURCE_ID);
    copy_subtype(input->family_subtype, source->hdr.subtype);
}

static int load_private_nav(const char *path, nav_t *nav)
{
    obs_t obs = {0};
    sta_t sta = {0};
    int stat = readrnx(path, 1, "", &obs, nav, &sta);
    free(obs.data);
    return stat > 0;
}

static size_t collect_identities(const rtklib_shared_nav_store_t *store,
                                 rtklib_shared_record_identity_t *identities,
                                 size_t capacity)
{
    size_t i;
    for (i = 0; i < capacity; ++i) {
        init_identity(&identities[i]);
        if (rtklib_shared_nav_record_at(store, i, &identities[i]) !=
            RTKLIB_SHARED_OK)
            return i;
    }
    return capacity;
}

static const rtklib_shared_record_identity_t *find_identity(
    const rtklib_shared_record_identity_t *identities, size_t count,
    uint32_t kind, uint32_t system, uint32_t prn, uint32_t family,
    int health_raw)
{
    size_t i;
    for (i = 0; i < count; ++i) {
        if (identities[i].record_kind == kind &&
            identities[i].system == system &&
            identities[i].prn == prn && identities[i].family == family &&
            (health_raw < -1 || identities[i].health_raw == health_raw))
            return &identities[i];
    }
    return NULL;
}

static const rtklib_shared_record_identity_t *find_identity_time(
    const rtklib_shared_record_identity_t *identities, size_t count,
    uint32_t kind, uint32_t system, uint32_t prn, uint32_t family,
    int iode, rtklib_shared_time_t toe)
{
    size_t i;
    for (i = 0; i < count; ++i) {
        if (identities[i].record_kind == kind &&
            identities[i].system == system &&
            identities[i].prn == prn && identities[i].family == family &&
            identities[i].iode == iode && same_time(identities[i].toe, toe))
            return &identities[i];
    }
    return NULL;
}

static int find_transition(const rtklib_shared_record_identity_t *identities,
                           size_t count, uint32_t system, uint32_t prn,
                           uint32_t family,
                           const rtklib_shared_record_identity_t **before,
                           const rtklib_shared_record_identity_t **after)
{
    size_t i, j;
    for (i = 0; i < count; ++i) {
        if (identities[i].record_kind != RTKLIB_SHARED_RECORD_EPH ||
            identities[i].system != system || identities[i].prn != prn ||
            identities[i].family != family)
            continue;
        for (j = i + 1; j < count; ++j) {
            if (identities[j].record_kind != RTKLIB_SHARED_RECORD_EPH ||
                identities[j].system != system || identities[j].prn != prn ||
                identities[j].family != family ||
                identities[i].iode == identities[j].iode ||
                fabs(time_difference(identities[i].toe, identities[j].toe)) <
                    1.0)
                continue;
            if (time_difference(identities[i].toe, identities[j].toe) < 0.0) {
                *before = &identities[i];
                *after = &identities[j];
            } else {
                *before = &identities[j];
                *after = &identities[i];
            }
            return 1;
        }
    }
    return 0;
}

static int check_state(const rtklib_shared_nav_store_t *store,
                       const rtklib_shared_record_identity_t *identity,
                       unsigned char code,
                       const eph_t *expected_eph,
                       const geph_t *expected_geph)
{
    rtklib_shared_state_query_t query;
    rtklib_shared_state_result_t result;
    double rs[6] = {0}, dts[2] = {0}, variance = 0.0;
    int stat;

    init_state_query(&query, identity->system, identity->prn,
                     identity->family, code, identity->toe,
                     identity->record_id);
    init_state_result(&result);
    stat = rtklib_shared_state_query(store, &query, &result);
    CHECK(stat == RTKLIB_SHARED_OK &&
          result.status == RTKLIB_SHARED_QUERY_AVAILABLE,
          "selected real NAV state is unavailable");
    CHECK(result.identity.record_id == identity->record_id &&
          result.identity.family == identity->family,
          "state result lost selected record identity");
    CHECK(result.state_valid != 0, "state result is not marked valid");
    if (expected_eph) {
        eph2pos(expected_eph->toe, expected_eph, rs, dts, &variance);
    } else {
        geph2pos(expected_geph->toe, expected_geph, rs, dts, &variance);
    }
    if (!(fabs(result.position_ecef_m[0] - rs[0]) < 1E-2 &&
          fabs(result.position_ecef_m[1] - rs[1]) < 1E-2 &&
          fabs(result.position_ecef_m[2] - rs[2]) < 1E-2)) {
        fprintf(stderr,
                "STATE_MISMATCH id=%llu sys=0x%x prn=%u family=0x%x "
                "got=(%.6f,%.6f,%.6f) expected=(%.6f,%.6f,%.6f)\n",
                (unsigned long long)identity->record_id, identity->system,
                identity->prn, identity->family, result.position_ecef_m[0],
                result.position_ecef_m[1], result.position_ecef_m[2], rs[0],
                rs[1], rs[2]);
        return fail_at(__FILE__, __LINE__,
                       "selected state differs from private RTKLIB propagation");
    }
    /* The real C19 CNV2 record in this fixture stores SVA as 15 metres
     * (URA2URAI=0).  Its public state variance must therefore be 15^2 m^2;
     * treating that metric value as an index reaches ura_value[15]. */
    if (identity->system == RTKLIB_SHARED_SYS_BDS &&
        identity->prn == 19 && identity->family == RTKLIB_SHARED_NAV_CNV2 &&
        expected_eph && expected_eph->sva == 15.0) {
        CHECK(fabs(result.variance_m2 - 225.0) < 1E-9,
              "real BDS CNV2 SVA metres were treated as a URA index");
    }
    return 0;
}

static int check_health(const rtklib_shared_nav_store_t *store,
                        const rtklib_shared_record_identity_t *identity,
                        unsigned char code, int expected_raw,
                        int expected_health)
{
    rtklib_shared_state_query_t query;
    rtklib_shared_state_result_t result;
    int stat;

    init_state_query(&query, identity->system, identity->prn,
                     identity->family, code, identity->toe,
                     identity->record_id);
    init_state_result(&result);
    stat = rtklib_shared_state_query(store, &query, &result);
    CHECK(stat == RTKLIB_SHARED_OK &&
          result.status == RTKLIB_SHARED_QUERY_AVAILABLE &&
          result.identity.record_id == identity->record_id,
          "selected real NAV health query is unavailable");
    CHECK(result.health_raw == expected_raw && result.health == expected_health,
          "real NAV health did not survive the selected-record query");
    return 0;
}

static int check_bias(const rtklib_shared_nav_store_t *store,
                      const rtklib_shared_record_identity_t *identity,
                      unsigned char code, int system, const eph_t *eph,
                      const geph_t *geph)
{
    rtklib_shared_state_query_t query;
    rtklib_shared_bias_result_t result;
    rtklib_signal_bias_info_ext_t info = {0};
    double expected = 0.0, actual;
    int stat;

    init_state_query(&query, identity->system, identity->prn,
                     identity->family, code, identity->toe,
                     identity->record_id);
    init_bias_result(&result);
    stat = rtklib_shared_bias_query(store, &query, &result);
    CHECK(stat == RTKLIB_SHARED_OK &&
          result.status == RTKLIB_SHARED_QUERY_AVAILABLE,
          "selected real NAV bias is unavailable");
    CHECK(result.identity.record_id == identity->record_id,
          "bias result lost selected record identity");
    stat = rtklib_signal_code_bias_selected_ext(
        system, (int)identity->family, code, eph, geph, &expected, &info);
    CHECK(stat == 1, "private signal bias oracle has no selected mapping");
    actual = result.raw_code_bias_m;
    CHECK(fabs(actual - expected) < 1E-10,
          "shared bias differs from selected RTKLIB signal-bias mapping");
    return 0;
}

static int check_ion(rtklib_shared_nav_store_t *store,
                     const rtklib_shared_record_identity_t *identity,
                     const ion_t *expected)
{
    rtklib_shared_ion_result_t result, real_result;
    rtklib_shared_ion_input_t input;
    rtklib_shared_record_id_t inserted_id = 0;
    size_t i, before;
    int stat;

    init_ion_result(&result);
    stat = rtklib_shared_ion_query(store, identity->system, identity->family,
                                   identity->transmit_time,
                                   identity->record_id, &result);
    CHECK(stat == RTKLIB_SHARED_OK &&
          result.status == RTKLIB_SHARED_QUERY_AVAILABLE,
          "selected real ION record is unavailable");
    CHECK(result.identity.record_id == identity->record_id,
          "ION query lost selected record identity");
    CHECK(result.value_count == (uint32_t)expected->ndata,
          "ION value count changed across the public ABI");
    for (i = 0; i < result.value_count; ++i) {
        CHECK(result.present[i] == expected->present[i] &&
              fabs(result.values[i] - expected->data[i]) < 1E-15,
              "ION field changed across the public ABI");
    }
    real_result = result;

    /* With no fixed ID, the declared evaluation time selects the same real
     * record.  This is kept separate from the fixed-ID query above. */
    init_ion_result(&result);
    stat = rtklib_shared_ion_query(store, identity->system, identity->family,
                                   identity->transmit_time, 0, &result);
    CHECK(stat == RTKLIB_SHARED_OK &&
          result.identity.record_id == identity->record_id,
          "ION default evaluation selection did not choose the real record");

    before = rtklib_shared_nav_record_count(store,
                                             RTKLIB_SHARED_RECORD_ION,
                                             identity->system);
    fill_ion_input(expected, &input);
    stat = rtklib_shared_nav_insert_ion(store, &input, &inserted_id);
    CHECK(stat == RTKLIB_SHARED_OK && inserted_id != 0,
          "same normalized ION record could not be inserted");
    CHECK(rtklib_shared_nav_record_count(store,
                                         RTKLIB_SHARED_RECORD_ION,
                                         identity->system) == before + 1,
          "same instance ION injection did not append one record");
    init_ion_result(&result);
    stat = rtklib_shared_ion_query(store, identity->system, identity->family,
                                   input.transmit_time, inserted_id, &result);
    CHECK(stat == RTKLIB_SHARED_OK &&
          result.identity.record_id == inserted_id &&
          result.value_count == input.value_count,
          "injected ION selected identity was not retained");
    for (i = 0; i < result.value_count; ++i) {
        CHECK(result.present[i] == real_result.present[i] &&
              fabs(result.values[i] - real_result.values[i]) < 1E-15,
              "same normalized ION query changed its payload");
    }
    return 0;
}

static int run_delft_week_boundary(const char *path)
{
    nav_t private_nav = {0};
    rtklib_shared_nav_store_t *store;
    rtklib_shared_record_identity_t loaded, injected;
    rtklib_shared_eph_input_t input, bad_input;
    rtklib_shared_state_query_t query;
    rtklib_shared_state_result_t loaded_state, injected_state;
    rtklib_shared_bias_result_t loaded_bias, injected_bias;
    rtklib_shared_record_id_t inserted_id = 0;
    const eph_t *expected;
    int parser_week = 0;
    double parser_ttr_sow;
    int stat, i;

    CHECK(load_private_nav(path, &private_nav),
          "Delft boundary fixture could not be parsed by RTKLIB");
    CHECK(private_nav.n == 1 && private_nav.ng == 0 && private_nav.nion == 0,
          "Delft boundary fixture did not contain exactly one GPS record");
    expected = &private_nav.eph[0];
    parser_ttr_sow = time2gpst(expected->ttr, &parser_week);
    CHECK(expected->sat == satno(SYS_GPS, 2) && expected->week == 2005 &&
          fabs(expected->toes) < 1E-9 && parser_week == 2004 &&
          fabs(parser_ttr_sow - 598206.0) < 1E-9,
          "RTKLIB did not normalize the real Delft GPS transmit week");

    store = rtklib_shared_nav_create();
    CHECK(store != NULL, "Delft boundary shared store allocation failed");
    CHECK(rtklib_shared_nav_load_rinex(store, path, "",
                                       "fixture:TUDELFT:2018161") ==
              RTKLIB_SHARED_OK,
          "Delft boundary fixture failed through shared RINEX loading");
    init_identity(&loaded);
    CHECK(rtklib_shared_nav_record_at(store, 0, &loaded) ==
              RTKLIB_SHARED_OK && loaded.system == RTKLIB_SHARED_SYS_GPS &&
          loaded.prn == 2 && loaded.family == RTKLIB_SHARED_NAV_LNAV &&
          loaded.iode == expected->iode && loaded.toe.week == 2005 &&
          fabs(loaded.toe.sow) < 1E-9 && loaded.transmit_time.week == 2004 &&
          fabs(loaded.transmit_time.sow - 598206.0) < 1E-9,
          "shared identity lost the real Delft normalized transmit time");

    fill_eph_input(expected, &input);
    /* RINEX 3 legacy records carry no RINEX 4 envelope message type; the
     * public normalized caller identity supplies the unambiguous GPS LNAV
     * family here without changing any navigation value. */
    input.family = RTKLIB_SHARED_NAV_LNAV;
    CHECK(input.system == RTKLIB_SHARED_SYS_GPS &&
          input.prn == 2 && input.family == RTKLIB_SHARED_NAV_LNAV &&
          input.broadcast_week == 2005 &&
          fabs(input.broadcast_toe_sow) < 1E-9 &&
          fabs(input.broadcast_transmit_sow - 598206.0) < 1E-9 &&
          input.transmit_time.week == 2004 &&
          fabs(input.transmit_time.sow - 598206.0) < 1E-9 &&
          fabs(time_difference(input.transmit_time,
                               (rtklib_shared_time_t){2005, 598206.0}) +
               604800.0) < 1E-6,
          "normalized input did not retain the real native GPST week pair");
    CHECK(rtklib_shared_nav_insert_eph(store, &input, &inserted_id) ==
              RTKLIB_SHARED_OK && inserted_id != 0,
          "normalized Delft boundary EPH insertion failed");
    init_identity(&injected);
    CHECK(rtklib_shared_nav_record(store, inserted_id, &injected) ==
              RTKLIB_SHARED_OK && injected.record_id == inserted_id &&
          injected.source_kind == RTKLIB_SHARED_SOURCE_RECEIVER &&
          injected.system == loaded.system && injected.prn == loaded.prn &&
          injected.family == loaded.family && injected.iode == loaded.iode &&
          same_time(injected.toe, loaded.toe) &&
          same_time(injected.transmit_time, loaded.transmit_time),
          "injected Delft identity differs from loaded real identity");

    CHECK(check_state(store, &loaded, CODE_L1C, expected, NULL) == 0 &&
          check_state(store, &injected, CODE_L1C, expected, NULL) == 0,
          "loaded or injected Delft boundary state query failed");
    init_state_query(&query, loaded.system, loaded.prn, loaded.family,
                     CODE_L1C, loaded.toe, loaded.record_id);
    init_state_result(&loaded_state);
    stat = rtklib_shared_state_query(store, &query, &loaded_state);
    CHECK(stat == RTKLIB_SHARED_OK, "loaded Delft state query failed");
    query.selected_record_id = injected.record_id;
    init_state_result(&injected_state);
    stat = rtklib_shared_state_query(store, &query, &injected_state);
    CHECK(stat == RTKLIB_SHARED_OK &&
          loaded_state.identity.record_id == loaded.record_id &&
          injected_state.identity.record_id == injected.record_id,
          "Delft loaded/injected state identities were not selected");
    for (i = 0; i < 3; ++i) {
        CHECK(fabs(loaded_state.position_ecef_m[i] -
                   injected_state.position_ecef_m[i]) < 1E-6 &&
              fabs(loaded_state.velocity_ecef_mps[i] -
                   injected_state.velocity_ecef_mps[i]) < 1E-6,
              "Delft loaded/injected state query changed its result");
    }
    CHECK(fabs(loaded_state.clock_bias_s - injected_state.clock_bias_s) < 1E-15 &&
          fabs(loaded_state.clock_drift_sps - injected_state.clock_drift_sps) <
              1E-15 &&
          fabs(loaded_state.variance_m2 - injected_state.variance_m2) < 1E-9,
          "Delft loaded/injected clock or variance changed");

    CHECK(check_bias(store, &loaded, CODE_L1C, SYS_GPS, expected, NULL) == 0 &&
          check_bias(store, &injected, CODE_L1C, SYS_GPS, expected, NULL) == 0,
          "loaded or injected Delft boundary bias query failed");
    init_bias_result(&loaded_bias);
    query.selected_record_id = loaded.record_id;
    stat = rtklib_shared_bias_query(store, &query, &loaded_bias);
    CHECK(stat == RTKLIB_SHARED_OK, "loaded Delft bias query failed");
    init_bias_result(&injected_bias);
    query.selected_record_id = injected.record_id;
    stat = rtklib_shared_bias_query(store, &query, &injected_bias);
    CHECK(stat == RTKLIB_SHARED_OK &&
          fabs(loaded_bias.raw_code_bias_m - injected_bias.raw_code_bias_m) <
              1E-12,
          "Delft loaded/injected bias query changed its result");

    bad_input = input;
    bad_input.transmit_time.sow += 1.0;
    CHECK(rtklib_shared_nav_insert_eph(store, &bad_input, NULL) ==
              RTKLIB_SHARED_INVALID_ARGUMENT,
          "contradictory Delft normalized transmit time was accepted");
    bad_input = input;
    bad_input.broadcast_transmit_sow -= 1.0;
    CHECK(rtklib_shared_nav_insert_eph(store, &bad_input, NULL) ==
              RTKLIB_SHARED_INVALID_ARGUMENT,
          "contradictory Delft native transmit SOW was accepted");

    printf("PASS delft_week_boundary loaded_ttr=%d/%.3f native=%d/%.3f "
           "adjustment=-604800 injected_id=%llu\n",
           loaded.transmit_time.week, loaded.transmit_time.sow,
           input.broadcast_week, input.broadcast_transmit_sow,
           (unsigned long long)inserted_id);
    rtklib_shared_nav_destroy(store);
    freenav(&private_nav, 0x3ff);
    return 0;
}

int main(int argc, char **argv)
{
    const char *path = argc > 1 ? argv[1] :
        "tests/rtklib_shared_api/fixtures/brd400_selected.rnx";
    nav_t private_nav = {0};
    rtklib_shared_nav_store_t *store;
    rtklib_shared_record_identity_t identities[MAX_RECORDS];
    rtklib_shared_record_identity_t loaded;
    const rtklib_shared_record_identity_t *before, *after;
    const rtklib_shared_record_identity_t *g_lnav, *g_cnav, *gal_inav;
    const rtklib_shared_record_identity_t *gal_fnav, *bds_d1, *bds_d2;
    const rtklib_shared_record_identity_t *bds_cnv1, *bds_cnv2, *bds_cnv3;
    const rtklib_shared_record_identity_t *qzs_lnav, *qzs_cnav, *qzs_cnv2;
    const rtklib_shared_record_identity_t *glo_healthy, *glo_unhealthy;
    const rtklib_shared_record_identity_t *glo_zero;
    const rtklib_shared_record_identity_t *ion_gps, *ion_bds_d1d2;
    const rtklib_shared_record_identity_t *ion_bds_bdgim;
    const eph_t *e_g_lnav, *e_g_cnav, *e_gal_inav, *e_gal_fnav;
    const eph_t *e_bds_d1, *e_bds_d2, *e_bds_cnv1, *e_bds_cnv2, *e_bds_cnv3;
    const eph_t *e_qzs_lnav, *e_qzs_cnav, *e_qzs_cnv2;
    const geph_t *e_glo_healthy, *e_glo_unhealthy, *e_glo_zero;
    const ion_t *i_gps, *i_bds_d1d2, *i_bds_bdgim;
    rtklib_shared_eph_input_t eph_input, bad_eph;
    rtklib_shared_eph_input_t unknown_health_input;
    rtklib_shared_glo_eph_input_t glo_input;
    rtklib_shared_ion_input_t ion_input, bad_ion;
    rtklib_shared_state_query_t query;
    rtklib_shared_state_result_t state_result;
    rtklib_shared_bias_result_t bias_result;
    rtklib_shared_ion_result_t ion_result;
    rtklib_shared_record_id_t inserted_id = 0;
    size_t count, i, before_count;
    int stat;

    CHECK(argc <= 2, "usage: test_rtklib_shared_api [RINEX_NAV]");
    {
        FILE *file = fopen(path, "rb");
        if (!file) {
            fprintf(stderr, "SKIP missing offline NAV fixture: %s\n", path);
            return 77;
        }
        fclose(file);
    }
    if (strstr(path, "dlf100_g02_week_boundary.rnx"))
        return run_delft_week_boundary(path);
    CHECK(rtklib_shared_abi_version() == (int)RTKLIB_SHARED_ABI_VERSION,
          "ABI version mismatch");
    CHECK(load_private_nav(path, &private_nav),
          "private RTKLIB oracle could not parse offline fixture");
    CHECK(private_nav.n == 14 && private_nav.ng == 3 &&
          private_nav.nion == 4,
          "offline fixture no longer has the expected real record set");

    store = rtklib_shared_nav_create();
    CHECK(store != NULL, "shared NAV store allocation failed");
    CHECK(rtklib_shared_nav_load_rinex(store, path, "", FIXTURE_SOURCE_ID) ==
          RTKLIB_SHARED_OK, "shared NAV RINEX load failed");

    /* The loader must validate the path before any fixed-buffer consumer is
     * called, while a NULL/empty source id remains a supported fallback for
     * ordinary short fixture paths. */
    {
        rtklib_shared_nav_store_t *source_store;
        const char *source_path = strlen(path) <
            RTKLIB_SHARED_SOURCE_ID_MAX ? path : short_fixture_path(path);
        char too_long_path[MAXSTRPATH + 1];

        CHECK(strlen(source_path) < RTKLIB_SHARED_SOURCE_ID_MAX,
              "source fallback test has no bounded fixture path");
        source_store = rtklib_shared_nav_create();
        CHECK(source_store != NULL, "source fallback store allocation failed");
        CHECK(rtklib_shared_nav_load_rinex(source_store, source_path, "",
                                           NULL) == RTKLIB_SHARED_OK,
              "NULL source id was not replaced by the validated path");
        CHECK(rtklib_shared_nav_record_count(source_store,
                                             RTKLIB_SHARED_RECORD_EPH,
                                             RTKLIB_SHARED_SYS_GPS) == 3,
              "NULL source id load lost GPS records");
        rtklib_shared_nav_destroy(source_store);

        source_store = rtklib_shared_nav_create();
        CHECK(source_store != NULL, "short source store allocation failed");
        CHECK(rtklib_shared_nav_load_rinex(source_store, source_path, "",
                                           "x") == RTKLIB_SHARED_OK,
              "short source id was rejected");
        rtklib_shared_nav_destroy(source_store);

        memset(too_long_path, 'x', sizeof(too_long_path));
        too_long_path[MAXSTRPATH] = '\0';
        CHECK(rtklib_shared_nav_load_rinex(store, NULL, "", "x") ==
                  RTKLIB_SHARED_INVALID_ARGUMENT,
              "NULL RINEX path was dereferenced");
        CHECK(rtklib_shared_nav_load_rinex(store, "", "", "x") ==
                  RTKLIB_SHARED_INVALID_ARGUMENT,
              "empty RINEX path was accepted");
        CHECK(rtklib_shared_nav_load_rinex(store, too_long_path, "", "x") ==
                  RTKLIB_SHARED_INVALID_ARGUMENT,
              "overlong RINEX path reached readrnx");
    }
    CHECK(rtklib_shared_nav_record_count(store, RTKLIB_SHARED_RECORD_EPH,
                                         RTKLIB_SHARED_SYS_GPS) == 3 &&
          rtklib_shared_nav_record_count(store, RTKLIB_SHARED_RECORD_GLO_EPH,
                                         RTKLIB_SHARED_SYS_GLO) == 3 &&
          rtklib_shared_nav_record_count(store, RTKLIB_SHARED_RECORD_EPH,
                                         RTKLIB_SHARED_SYS_GAL) == 2 &&
          rtklib_shared_nav_record_count(store, RTKLIB_SHARED_RECORD_EPH,
                                         RTKLIB_SHARED_SYS_BDS) == 6 &&
          rtklib_shared_nav_record_count(store, RTKLIB_SHARED_RECORD_EPH,
                                         RTKLIB_SHARED_SYS_QZS) == 3,
          "loaded family/system counts do not match verbatim fixture");
    CHECK(rtklib_shared_nav_record_count(store, RTKLIB_SHARED_RECORD_ION,
                                         RTKLIB_SHARED_SYS_GPS) == 1 &&
          rtklib_shared_nav_record_count(store, RTKLIB_SHARED_RECORD_ION,
                                         RTKLIB_SHARED_SYS_GAL) == 1 &&
          rtklib_shared_nav_record_count(store, RTKLIB_SHARED_RECORD_ION,
                                         RTKLIB_SHARED_SYS_BDS) == 2,
          "loaded ION counts do not match verbatim fixture");

    count = collect_identities(store, identities, MAX_RECORDS);
    CHECK(count == 21, "record catalogue count changed unexpectedly");
    for (i = 0; i < count; ++i) {
        CHECK(identities[i].abi_version == RTKLIB_SHARED_ABI_VERSION &&
              identities[i].struct_size == sizeof(identities[i]) &&
              identities[i].record_id != 0 &&
              identities[i].source_kind == RTKLIB_SHARED_SOURCE_RINEX &&
              !strcmp(identities[i].source_id, FIXTURE_SOURCE_ID),
              "loaded identity lacks ABI or source provenance");
    }

    g_lnav = find_identity(identities, count, RTKLIB_SHARED_RECORD_EPH,
                           RTKLIB_SHARED_SYS_GPS, 1,
                           RTKLIB_SHARED_NAV_LNAV, -2);
    g_cnav = find_identity(identities, count, RTKLIB_SHARED_RECORD_EPH,
                           RTKLIB_SHARED_SYS_GPS, 1,
                           RTKLIB_SHARED_NAV_CNAV, -2);
    gal_inav = find_identity(identities, count, RTKLIB_SHARED_RECORD_EPH,
                             RTKLIB_SHARED_SYS_GAL, 2,
                             RTKLIB_SHARED_NAV_INAV, -2);
    gal_fnav = find_identity(identities, count, RTKLIB_SHARED_RECORD_EPH,
                             RTKLIB_SHARED_SYS_GAL, 2,
                             RTKLIB_SHARED_NAV_FNAV, -2);
    bds_d1 = find_identity(identities, count, RTKLIB_SHARED_RECORD_EPH,
                           RTKLIB_SHARED_SYS_BDS, 6,
                           RTKLIB_SHARED_NAV_D1, -2);
    bds_d2 = find_identity(identities, count, RTKLIB_SHARED_RECORD_EPH,
                           RTKLIB_SHARED_SYS_BDS, 1,
                           RTKLIB_SHARED_NAV_D2, -2);
    bds_cnv1 = find_identity(identities, count, RTKLIB_SHARED_RECORD_EPH,
                             RTKLIB_SHARED_SYS_BDS, 19,
                             RTKLIB_SHARED_NAV_CNV1, -2);
    bds_cnv2 = find_identity(identities, count, RTKLIB_SHARED_RECORD_EPH,
                             RTKLIB_SHARED_SYS_BDS, 19,
                             RTKLIB_SHARED_NAV_CNV2, -2);
    bds_cnv3 = find_identity(identities, count, RTKLIB_SHARED_RECORD_EPH,
                             RTKLIB_SHARED_SYS_BDS, 19,
                             RTKLIB_SHARED_NAV_CNV3, -2);
    qzs_lnav = find_identity(identities, count, RTKLIB_SHARED_RECORD_EPH,
                             RTKLIB_SHARED_SYS_QZS, 194,
                             RTKLIB_SHARED_NAV_LNAV, -2);
    qzs_cnav = find_identity(identities, count, RTKLIB_SHARED_RECORD_EPH,
                             RTKLIB_SHARED_SYS_QZS, 194,
                             RTKLIB_SHARED_NAV_CNAV, -2);
    qzs_cnv2 = find_identity(identities, count, RTKLIB_SHARED_RECORD_EPH,
                             RTKLIB_SHARED_SYS_QZS, 194,
                             RTKLIB_SHARED_NAV_CNV2, -2);
    glo_healthy = find_identity(identities, count, RTKLIB_SHARED_RECORD_GLO_EPH,
                                RTKLIB_SHARED_SYS_GLO, 2,
                                RTKLIB_SHARED_NAV_FDMA, 0);
    glo_unhealthy = find_identity(identities, count,
                                  RTKLIB_SHARED_RECORD_GLO_EPH,
                                  RTKLIB_SHARED_SYS_GLO, 2,
                                  RTKLIB_SHARED_NAV_FDMA, 1);
    glo_zero = find_identity(identities, count, RTKLIB_SHARED_RECORD_GLO_EPH,
                             RTKLIB_SHARED_SYS_GLO, 11,
                             RTKLIB_SHARED_NAV_FDMA, 0);
    ion_gps = find_identity(identities, count, RTKLIB_SHARED_RECORD_ION,
                            RTKLIB_SHARED_SYS_GPS, 3,
                            RTKLIB_SHARED_NAV_LNAV, -2);
    ion_bds_d1d2 = find_identity(identities, count, RTKLIB_SHARED_RECORD_ION,
                                 RTKLIB_SHARED_SYS_BDS, 21,
                                 RTKLIB_SHARED_NAV_D1D2, -2);
    ion_bds_bdgim = find_identity(identities, count, RTKLIB_SHARED_RECORD_ION,
                                  RTKLIB_SHARED_SYS_BDS, 29,
                                  RTKLIB_SHARED_NAV_CNVX, -2);
    CHECK(g_lnav && g_cnav && gal_inav && gal_fnav && bds_d1 && bds_d2 &&
          bds_cnv1 && bds_cnv2 && bds_cnv3 && qzs_lnav && qzs_cnav &&
          qzs_cnv2 && glo_healthy && glo_unhealthy && glo_zero && ion_gps &&
          ion_bds_d1d2 && ion_bds_bdgim,
          "one or more real NAV family identities were lost");

    CHECK(find_transition(identities, count, RTKLIB_SHARED_SYS_GPS, 1,
                          RTKLIB_SHARED_NAV_LNAV, &before, &after) &&
          before->iode == 39 && after->iode == 40,
          "GPS LNAV real IODE transition was lost");
    CHECK(find_transition(identities, count, RTKLIB_SHARED_SYS_BDS, 19,
                          RTKLIB_SHARED_NAV_CNV1, &before, &after) &&
          before->iode == 16 && after->iode == 17,
          "BDS CNV1 real IODE transition was lost");
    CHECK(time_difference(before->toe, after->toe) < 0.0,
          "real transition order is not chronological");
    CHECK(glo_healthy->record_id != glo_unhealthy->record_id &&
          glo_healthy->health_raw == 0 && glo_unhealthy->health_raw == 1,
          "real later GLONASS unhealthy identity was lost");
    CHECK(glo_zero->glonass_fcn == 0,
          "real GLONASS FCN=0 was conflated with unknown");

    e_g_lnav = NULL; e_g_cnav = NULL; e_gal_inav = NULL; e_gal_fnav = NULL;
    e_bds_d1 = NULL; e_bds_d2 = NULL; e_bds_cnv1 = NULL; e_bds_cnv2 = NULL;
    e_bds_cnv3 = NULL; e_qzs_lnav = NULL; e_qzs_cnav = NULL;
    e_qzs_cnv2 = NULL; e_glo_healthy = NULL; e_glo_unhealthy = NULL;
    e_glo_zero = NULL; i_gps = NULL; i_bds_d1d2 = NULL; i_bds_bdgim = NULL;
    for (i = 0; i < (size_t)private_nav.n; ++i) {
        const eph_t *e = &private_nav.eph[i];
        int system = satsys(e->sat, NULL);
        int prn = eph_prn(e);
        if (system == SYS_GPS && prn == 1 && e->hdr.msg_type == NAV_LNAV &&
            e->iode == g_lnav->iode)
            e_g_lnav = e;
        else if (system == SYS_GPS && prn == 1 && e->hdr.msg_type == NAV_CNAV)
            e_g_cnav = e;
        else if (system == SYS_GAL && prn == 2 && e->hdr.msg_type == NAV_INAV)
            e_gal_inav = e;
        else if (system == SYS_GAL && prn == 2 && e->hdr.msg_type == NAV_FNAV)
            e_gal_fnav = e;
        else if (system == SYS_CMP && prn == 6 && e->hdr.msg_type == NAV_D1)
            e_bds_d1 = e;
        else if (system == SYS_CMP && prn == 1 && e->hdr.msg_type == NAV_D2)
            e_bds_d2 = e;
        else if (system == SYS_CMP && prn == 19 && e->hdr.msg_type == NAV_CNV1 &&
                 e->iode == bds_cnv1->iode)
            e_bds_cnv1 = e;
        else if (system == SYS_CMP && prn == 19 && e->hdr.msg_type == NAV_CNV2)
            e_bds_cnv2 = e;
        else if (system == SYS_CMP && prn == 19 && e->hdr.msg_type == NAV_CNV3)
            e_bds_cnv3 = e;
        else if (system == SYS_QZS && prn == 194 && e->hdr.msg_type == NAV_LNAV)
            e_qzs_lnav = e;
        else if (system == SYS_QZS && prn == 194 && e->hdr.msg_type == NAV_CNAV)
            e_qzs_cnav = e;
        else if (system == SYS_QZS && prn == 194 && e->hdr.msg_type == NAV_CNV2)
            e_qzs_cnv2 = e;
    }
    for (i = 0; i < (size_t)private_nav.ng; ++i) {
        const geph_t *e = &private_nav.geph[i];
        if (geph_prn(e) == 2 && e->svh == 0) e_glo_healthy = e;
        if (geph_prn(e) == 2 && e->svh == 1) e_glo_unhealthy = e;
        if (geph_prn(e) == 11 && e->frq == 0) e_glo_zero = e;
    }
    for (i = 0; i < (size_t)private_nav.nion; ++i) {
        if (private_nav.ion[i].hdr.sys == SYS_GPS &&
            private_nav.ion[i].hdr.msg_type == NAV_LNAV) {
            i_gps = &private_nav.ion[i];
        } else if (private_nav.ion[i].hdr.sys == SYS_CMP &&
                   private_nav.ion[i].hdr.msg_type == NAV_D1D2) {
            i_bds_d1d2 = &private_nav.ion[i];
        } else if (private_nav.ion[i].hdr.sys == SYS_CMP &&
                   private_nav.ion[i].hdr.msg_type == NAV_CNVX) {
            i_bds_bdgim = &private_nav.ion[i];
        }
    }
    CHECK(e_g_lnav && e_g_cnav && e_gal_inav && e_gal_fnav && e_bds_d1 &&
          e_bds_d2 &&
          e_bds_cnv1 && e_bds_cnv2 && e_bds_cnv3 && e_qzs_lnav &&
          e_qzs_cnav && e_qzs_cnv2 &&
          e_glo_healthy && e_glo_unhealthy && e_glo_zero && i_gps &&
          i_bds_d1d2 && i_bds_bdgim,
          "private oracle could not locate selected real records");

    CHECK(find_identity_time(identities, count, RTKLIB_SHARED_RECORD_EPH,
                             RTKLIB_SHARED_SYS_BDS, 19,
                             RTKLIB_SHARED_NAV_CNV1, e_bds_cnv1->iode,
                             shared_time(e_bds_cnv1->toe)) == bds_cnv1,
          "BDS native BDT Toe was not represented by GPST identity");
    CHECK(check_state(store, g_lnav, CODE_L1C, e_g_lnav, NULL) == 0 &&
          check_state(store, g_cnav, CODE_L1C, e_g_cnav, NULL) == 0 &&
          check_state(store, gal_inav, CODE_L7Q, e_gal_inav, NULL) == 0 &&
          check_state(store, gal_fnav, CODE_L5Q, e_gal_fnav, NULL) == 0 &&
          check_state(store, bds_d1, CODE_L2I, e_bds_d1, NULL) == 0 &&
          check_state(store, bds_d2, CODE_L2I, e_bds_d2, NULL) == 0 &&
          check_state(store, bds_cnv1, CODE_L1P, e_bds_cnv1, NULL) == 0 &&
          check_state(store, bds_cnv2, CODE_L1P, e_bds_cnv2, NULL) == 0 &&
          check_state(store, bds_cnv3, CODE_L7D, e_bds_cnv3, NULL) == 0 &&
          check_state(store, qzs_lnav, CODE_L1C, e_qzs_lnav, NULL) == 0 &&
          check_state(store, qzs_cnav, CODE_L1C, e_qzs_cnav, NULL) == 0 &&
          check_state(store, qzs_cnv2, CODE_L1C, e_qzs_cnv2, NULL) == 0 &&
          check_state(store, glo_healthy, CODE_L1C, NULL, e_glo_healthy) == 0 &&
          check_health(store, glo_healthy, CODE_L1C, 0,
                       RTKLIB_SHARED_HEALTH_HEALTHY) == 0 &&
          check_health(store, glo_unhealthy, CODE_L1C, 1,
                       RTKLIB_SHARED_HEALTH_UNHEALTHY) == 0,
          "a selected real family state failed");

    CHECK(check_bias(store, g_lnav, CODE_L1C, SYS_GPS, e_g_lnav, NULL) == 0 &&
          check_bias(store, g_cnav, CODE_L1C, SYS_GPS, e_g_cnav, NULL) == 0 &&
          check_bias(store, gal_inav, CODE_L1C, SYS_GAL, e_gal_inav, NULL) == 0 &&
          check_bias(store, gal_fnav, CODE_L1C, SYS_GAL, e_gal_fnav, NULL) == 0 &&
          check_bias(store, bds_d1, CODE_L2I, SYS_CMP, e_bds_d1, NULL) == 0 &&
          check_bias(store, bds_d2, CODE_L2I, SYS_CMP, e_bds_d2, NULL) == 0 &&
          check_bias(store, bds_cnv1, CODE_L1P, SYS_CMP, e_bds_cnv1, NULL) == 0 &&
          check_bias(store, bds_cnv2, CODE_L1P, SYS_CMP, e_bds_cnv2, NULL) == 0 &&
          check_bias(store, bds_cnv3, CODE_L7D, SYS_CMP, e_bds_cnv3, NULL) == 0 &&
          check_bias(store, qzs_lnav, CODE_L1C, SYS_QZS, e_qzs_lnav, NULL) == 0 &&
          check_bias(store, qzs_cnav, CODE_L1C, SYS_QZS, e_qzs_cnav, NULL) == 0 &&
          check_bias(store, qzs_cnv2, CODE_L1C, SYS_QZS, e_qzs_cnv2, NULL) == 0 &&
          check_bias(store, glo_healthy, CODE_L1C, SYS_GLO, NULL,
                     e_glo_healthy) == 0,
          "selected family bias did not use the RTKLIB extension mapping");
    CHECK(check_ion(store, ion_gps, i_gps) == 0,
          "selected ION or same-record insertion failed");
    CHECK(check_ion(store, ion_bds_bdgim, i_bds_bdgim) == 0,
          "selected BDS CNVX ION or same-record insertion failed");

    /* The real BDS C21 D1/D2 coefficients use BDT SOW.  This ABI's only
     * evaluator is RTKLIB's GPST-based Klobuchar wrapper, so accepting this
     * record would apply a 14-second time-scale error.  Preserve the source
     * identity for a caller that wants to handle the model itself. */
    {
        const double receiver_llh[3] = {0.5, 1.0, 100.0};
        const double azel[2] = {0.2, 0.8};
        double delay = -1.0, variance = -1.0;
        init_identity(&loaded);
        CHECK(rtklib_shared_iono(store, ion_bds_d1d2->transmit_time,
                                  RTKLIB_SHARED_SYS_BDS,
                                  RTKLIB_SHARED_NAV_D1D2, 21,
                                  receiver_llh, azel,
                                  RTKLIB_SHARED_IONO_BRDC,
                                  ion_bds_d1d2->record_id, &delay, &variance,
                                  &loaded) == RTKLIB_SHARED_UNSUPPORTED &&
              loaded.record_id == ion_bds_d1d2->record_id &&
              loaded.system == RTKLIB_SHARED_SYS_BDS && loaded.prn == 21 &&
              loaded.family == RTKLIB_SHARED_NAV_D1D2,
              "real BDS D1/D2 was evaluated with the wrong time scale");
    }

    /* Real RINEX 4 BDS CNVX carries BDGIM coefficients (nine values).  The
     * shared ABI currently exposes only the RTKLIB Klobuchar wrappers, so a
     * selected BDGIM record must be rejected as unsupported while retaining
     * the selected source identity. */
    {
        const double receiver_llh[3] = {0.5, 1.0, 100.0};
        const double azel[2] = {0.2, 0.8};
        double delay = -1.0, variance = -1.0;
        init_identity(&loaded);
        CHECK(rtklib_shared_iono(store, ion_bds_bdgim->transmit_time,
                                  RTKLIB_SHARED_SYS_BDS,
                                  RTKLIB_SHARED_NAV_CNVX, 29,
                                  receiver_llh, azel,
                                  RTKLIB_SHARED_IONO_BRDC,
                                  ion_bds_bdgim->record_id, &delay, &variance,
                                  &loaded) == RTKLIB_SHARED_UNSUPPORTED &&
              loaded.record_id == ion_bds_bdgim->record_id &&
              loaded.system == RTKLIB_SHARED_SYS_BDS &&
              loaded.prn == 29 &&
              loaded.family == RTKLIB_SHARED_NAV_CNVX,
              "real BDS BDGIM was treated as a Klobuchar model");
    }

    fill_eph_input(e_bds_cnv1, &eph_input);
    /* Synthetic time-only boundary regression built from the real C19 CNV1
     * record above: BDT SOW 604790 converts to GPST week+1/SOW 4.  This does
     * not add a NAV record to the fixture or claim real week-boundary data;
     * it proves the normalized GPST/native BDT pair accepts a valid rollover
     * and rejects a one-second contradictory native value. */
    {
        rtklib_shared_eph_input_t boundary = eph_input;
        rtklib_shared_eph_input_t invalid_boundary;
        rtklib_shared_record_id_t boundary_id = 0;
        boundary.broadcast_toe_sow = 604790.0;
        boundary.broadcast_transmit_sow = 604790.0;
        boundary.toe.week += 1;
        boundary.toe.sow = 4.0;
        boundary.toc = boundary.toe;
        boundary.transmit_time = boundary.toe;
        boundary.receive_order = 1004;
        strcpy(boundary.source_id, "receiver:synthetic-boundary");
        CHECK(rtklib_shared_nav_insert_eph(store, &boundary, &boundary_id) ==
                  RTKLIB_SHARED_OK && boundary_id != 0,
              "valid BDS BDT to GPST week-boundary input was rejected");
        invalid_boundary = boundary;
        invalid_boundary.broadcast_toe_sow = 604789.0;
        CHECK(rtklib_shared_nav_insert_eph(store, &invalid_boundary, NULL) ==
                  RTKLIB_SHARED_INVALID_ARGUMENT,
              "contradictory BDS week-boundary native Toe was accepted");
    }
    before_count = rtklib_shared_nav_record_count(store,
                                                   RTKLIB_SHARED_RECORD_EPH,
                                                   RTKLIB_SHARED_SYS_BDS);
    stat = rtklib_shared_nav_insert_eph(store, &eph_input, &inserted_id);
    if (stat != RTKLIB_SHARED_OK || inserted_id == 0 ||
        rtklib_shared_nav_record_count(store, RTKLIB_SHARED_RECORD_EPH,
                                       RTKLIB_SHARED_SYS_BDS) != before_count + 1)
        fprintf(stderr, "BDS_INSERT stat=%d id=%llu before=%zu after=%zu week=%d toe=%.6f native_tx=%.6f gpst_toe=%d/%.6f gpst_tx=%d/%.6f A=%.6f e=%.12g sva=%.6g fit=%.6g f0=%.6g f1=%.6g f2=%.6g Adot=%.6g ndot=%.6g raw=(%.6g %.6g %.6g %.6g %.6g %.6g) source=%s\n",
                stat, (unsigned long long)inserted_id, before_count,
                rtklib_shared_nav_record_count(store, RTKLIB_SHARED_RECORD_EPH,
                                               RTKLIB_SHARED_SYS_BDS),
                eph_input.broadcast_week, eph_input.broadcast_toe_sow,
                eph_input.broadcast_transmit_sow, eph_input.toe.week,
                eph_input.toe.sow, eph_input.transmit_time.week,
                eph_input.transmit_time.sow, eph_input.semi_major_axis_m,
                eph_input.eccentricity, eph_input.sva_m, eph_input.fit_interval_h,
                eph_input.clock_bias_s, eph_input.clock_drift_sps,
                eph_input.clock_drift_rate_sps2, eph_input.additional_rate_m_s,
                eph_input.additional_mean_motion_rate_rad_s2,
                eph_input.delta_n0_raw, eph_input.top_raw,
                eph_input.delta_n0_dot_raw, eph_input.urai_ned_raw[0],
                eph_input.urai_ned_raw[1], eph_input.urai_ned_raw[2],
                eph_input.source_id);
    CHECK(stat == RTKLIB_SHARED_OK && inserted_id != 0 &&
          rtklib_shared_nav_record_count(store, RTKLIB_SHARED_RECORD_EPH,
                                         RTKLIB_SHARED_SYS_BDS) == before_count + 1,
          "same normalized BDS record could not be injected");
    init_identity(&loaded);
    CHECK(rtklib_shared_nav_record(store, inserted_id, &loaded) ==
          RTKLIB_SHARED_OK && loaded.record_id == inserted_id &&
          loaded.source_kind == RTKLIB_SHARED_SOURCE_RECEIVER &&
          !strcmp(loaded.source_id, RECEIVER_SOURCE_ID) &&
          loaded.system == bds_cnv1->system && loaded.prn == bds_cnv1->prn &&
          loaded.family == bds_cnv1->family && loaded.iode == bds_cnv1->iode &&
          same_time(loaded.toe, bds_cnv1->toe) &&
          same_time(loaded.transmit_time, bds_cnv1->transmit_time),
          "injected BDS identity differs from the real normalized record");
    CHECK(check_state(store, &loaded, CODE_L1P, e_bds_cnv1, NULL) == 0 &&
          check_bias(store, &loaded, CODE_L1P, SYS_CMP, e_bds_cnv1, NULL) == 0,
          "injected BDS selected state/bias failed");

    /* A decoded negative health value is the explicit public UNKNOWN
     * sentinel.  It must not be passed through family bit tests that turn
     * -1 into UNHEALTHY, while state and selected identity remain usable. */
    fill_eph_input(e_g_lnav, &unknown_health_input);
    unknown_health_input.health_raw = -1;
    unknown_health_input.receive_order = 1005;
    strcpy(unknown_health_input.source_id, "receiver:unknown-health");
    CHECK(rtklib_shared_nav_insert_eph(store, &unknown_health_input,
                                       &inserted_id) == RTKLIB_SHARED_OK &&
          inserted_id != 0, "unknown-health EPH insertion failed");
    init_state_query(&query, unknown_health_input.system,
                     unknown_health_input.prn, unknown_health_input.family,
                     CODE_L1C, unknown_health_input.toe, inserted_id);
    init_state_result(&state_result);
    CHECK(rtklib_shared_state_query(store, &query, &state_result) ==
              RTKLIB_SHARED_OK && state_result.state_valid == 1 &&
          state_result.health_raw == -1 &&
          state_result.health == RTKLIB_SHARED_HEALTH_UNKNOWN &&
          state_result.identity.record_id == inserted_id &&
          state_result.identity.health_raw == -1,
          "negative health sentinel was not preserved as UNKNOWN");

    fill_glo_input(e_glo_zero, &glo_input);
    before_count = rtklib_shared_nav_record_count(store,
                                                   RTKLIB_SHARED_RECORD_GLO_EPH,
                                                   RTKLIB_SHARED_SYS_GLO);
    stat = rtklib_shared_nav_insert_glo_eph(store, &glo_input, &inserted_id);
    CHECK(stat == RTKLIB_SHARED_OK && inserted_id != 0 &&
          rtklib_shared_nav_record_count(store,
                                         RTKLIB_SHARED_RECORD_GLO_EPH,
                                         RTKLIB_SHARED_SYS_GLO) == before_count + 1,
          "same normalized GLONASS FCN=0 record could not be injected");
    init_identity(&loaded);
    CHECK(rtklib_shared_nav_record(store, inserted_id, &loaded) ==
              RTKLIB_SHARED_OK && loaded.record_id == inserted_id &&
          loaded.source_kind == RTKLIB_SHARED_SOURCE_RECEIVER &&
          !strcmp(loaded.source_id, RECEIVER_SOURCE_ID) &&
          loaded.system == RTKLIB_SHARED_SYS_GLO && loaded.prn == 11 &&
          loaded.family == RTKLIB_SHARED_NAV_FDMA && loaded.glonass_fcn == 0 &&
          loaded.health_raw == e_glo_zero->svh,
          "injected GLONASS identity did not retain FCN=0 and health");
    CHECK(check_state(store, &loaded, CODE_L1C, NULL, e_glo_zero) == 0 &&
          check_bias(store, &loaded, CODE_L1C, SYS_GLO, NULL, e_glo_zero) == 0,
          "injected GLONASS selected state/bias failed");

    init_state_query(&query, RTKLIB_SHARED_SYS_GLO, 2,
                     RTKLIB_SHARED_NAV_FDMA, CODE_L1C,
                     glo_healthy->toe, glo_healthy->record_id);
    query.glonass_fcn = -4;
    init_state_result(&state_result);
    CHECK(rtklib_shared_state_query(store, &query, &state_result) ==
              RTKLIB_SHARED_OK && state_result.identity.record_id ==
              glo_healthy->record_id,
          "selected GLONASS FCN=-4 record failed");
    query.glonass_fcn = 0;
    init_state_result(&state_result);
    CHECK(rtklib_shared_state_query(store, &query, &state_result) ==
              RTKLIB_SHARED_UNSUPPORTED,
          "FCN=0 incorrectly matched the selected FCN=-4 record");
    init_state_query(&query, RTKLIB_SHARED_SYS_GLO, 11,
                     RTKLIB_SHARED_NAV_FDMA, CODE_L1C, glo_zero->toe,
                     glo_zero->record_id);
    query.glonass_fcn = 0;
    init_state_result(&state_result);
    CHECK(rtklib_shared_state_query(store, &query, &state_result) ==
              RTKLIB_SHARED_OK && state_result.identity.record_id ==
              glo_zero->record_id,
          "real GLONASS FCN=0 could not be selected");

    /* A fixed record ID remains authoritative when evaluation moves to the
     * later real unhealthy R02 record.  Without a fixed ID, the same
     * selection time must choose that later record instead of falling back to
     * the earlier healthy one. */
    init_state_query(&query, RTKLIB_SHARED_SYS_GLO, 2,
                     RTKLIB_SHARED_NAV_FDMA, CODE_L1C,
                     glo_unhealthy->toe, glo_healthy->record_id);
    query.glonass_fcn = -4;
    init_state_result(&state_result);
    CHECK(rtklib_shared_state_query(store, &query, &state_result) ==
              RTKLIB_SHARED_OK &&
          state_result.identity.record_id == glo_healthy->record_id &&
          state_result.health_raw == 0,
          "fixed GLONASS selection was replaced by evaluation selection");
    query.selected_record_id = 0;
    init_state_result(&state_result);
    CHECK(rtklib_shared_state_query(store, &query, &state_result) ==
              RTKLIB_SHARED_OK &&
          state_result.identity.record_id == glo_unhealthy->record_id &&
          state_result.health_raw == 1,
          "default GLONASS evaluation selection did not retain later health");

    /* FCN 0 is a real channel; values outside the public [-7,13] range are
     * malformed arguments and must be rejected before selection in both
     * state and bias queries. */
    query.glonass_fcn = 14;
    init_state_result(&state_result);
    CHECK(rtklib_shared_state_query(store, &query, &state_result) ==
              RTKLIB_SHARED_INVALID_ARGUMENT,
          "out-of-range GLONASS FCN was accepted by state query");
    init_bias_result(&bias_result);
    CHECK(rtklib_shared_bias_query(store, &query, &bias_result) ==
              RTKLIB_SHARED_INVALID_ARGUMENT,
          "out-of-range GLONASS FCN was accepted by bias query");

    init_state_query(&query, g_lnav->system, g_lnav->prn, g_lnav->family,
                     CODE_L1C, g_lnav->toe, UINT64_MAX);
    init_state_result(&state_result);
    stat = rtklib_shared_state_query(store, &query, &state_result);
    CHECK(stat != RTKLIB_SHARED_OK &&
          state_result.status != RTKLIB_SHARED_QUERY_AVAILABLE,
          "stale selected identity fell back to another state");
    init_bias_result(&bias_result);
    CHECK(rtklib_shared_bias_query(store, &query, &bias_result) ==
              RTKLIB_SHARED_INVALID_ARGUMENT &&
          bias_result.status != RTKLIB_SHARED_QUERY_AVAILABLE,
          "stale selected identity returned a bias failure instead of an argument error");
    init_state_query(&query, RTKLIB_SHARED_SYS_BDS, 19,
                     RTKLIB_SHARED_NAV_CNV1, CODE_L1P, bds_cnv1->toe,
                     g_lnav->record_id);
    init_state_result(&state_result);
    CHECK(rtklib_shared_state_query(store, &query, &state_result) ==
              RTKLIB_SHARED_UNSUPPORTED &&
          state_result.status == RTKLIB_SHARED_QUERY_UNSUPPORTED,
          "selected identity from another satellite was silently replaced");

    bad_eph = eph_input;
    bad_eph.abi_version = 0;
    CHECK(rtklib_shared_nav_insert_eph(store, &bad_eph, NULL) ==
              RTKLIB_SHARED_INVALID_ARGUMENT,
          "wrong ABI version was accepted for EPH input");
    bad_eph = eph_input;
    bad_eph.struct_size = (uint32_t)(sizeof(bad_eph) - 1);
    CHECK(rtklib_shared_nav_insert_eph(store, &bad_eph, NULL) ==
              RTKLIB_SHARED_INVALID_ARGUMENT,
          "short EPH struct was accepted");
    bad_eph = eph_input;
    bad_eph.toe.sow += 1.0;
    CHECK(rtklib_shared_nav_insert_eph(store, &bad_eph, NULL) ==
              RTKLIB_SHARED_INVALID_ARGUMENT,
          "contradictory GPST/native EPH Toe was accepted");
    bad_eph = eph_input;
    bad_eph.broadcast_toe_sow += 1.0;
    CHECK(rtklib_shared_nav_insert_eph(store, &bad_eph, NULL) ==
              RTKLIB_SHARED_INVALID_ARGUMENT,
          "inconsistent native BDS Toe was accepted");
    bad_eph = eph_input;
    bad_eph.sva_m = NAN;
    CHECK(rtklib_shared_nav_insert_eph(store, &bad_eph, NULL) ==
              RTKLIB_SHARED_INVALID_ARGUMENT,
          "NaN EPH accuracy was accepted");
    bad_eph = eph_input;
    memset(bad_eph.family_subtype, 'X', sizeof(bad_eph.family_subtype));
    CHECK(rtklib_shared_nav_insert_eph(store, &bad_eph, NULL) ==
              RTKLIB_SHARED_INVALID_ARGUMENT,
          "non-NUL EPH family subtype was silently truncated");

    memset(glo_input.family_subtype, 'X', sizeof(glo_input.family_subtype));
    CHECK(rtklib_shared_nav_insert_glo_eph(store, &glo_input, NULL) ==
              RTKLIB_SHARED_INVALID_ARGUMENT,
          "non-NUL GLONASS family subtype was silently truncated");

    fill_ion_input(i_gps, &ion_input);
    bad_ion = ion_input;
    bad_ion.values[0] = NAN;
    CHECK(rtklib_shared_nav_insert_ion(store, &bad_ion, NULL) ==
              RTKLIB_SHARED_INVALID_ARGUMENT,
          "NaN ION coefficient was accepted");
    bad_ion = ion_input;
    memset(bad_ion.family_subtype, 'X', sizeof(bad_ion.family_subtype));
    CHECK(rtklib_shared_nav_insert_ion(store, &bad_ion, NULL) ==
              RTKLIB_SHARED_INVALID_ARGUMENT,
          "non-NUL ION family subtype was silently truncated");

    query.evaluation_time.sow = NAN;
    init_state_result(&state_result);
    CHECK(rtklib_shared_state_query(store, &query, &state_result) ==
              RTKLIB_SHARED_INVALID_ARGUMENT,
          "NaN state evaluation time was accepted");
    CHECK(rtklib_shared_nav_record(store, inserted_id,
                                   &(rtklib_shared_record_identity_t){0}) ==
              RTKLIB_SHARED_INVALID_ARGUMENT,
          "zero sized identity output was accepted");
    init_identity(&loaded);
    loaded.abi_version = 0;
    CHECK(rtklib_shared_nav_record(store, inserted_id, &loaded) ==
              RTKLIB_SHARED_INVALID_ARGUMENT,
          "wrong identity output ABI version was accepted");
    init_identity(&loaded);
    loaded.struct_size = (uint32_t)(sizeof(loaded) - 1);
    CHECK(rtklib_shared_nav_record(store, inserted_id, &loaded) ==
              RTKLIB_SHARED_INVALID_ARGUMENT,
          "short identity output struct was accepted");

    /* Every caller-owned result is validated before the implementation may
     * initialize it.  Exercise both a wrong ABI version in a full object and
     * a genuinely short object surrounded by a canary.  The latter is also a
     * sanitizer guard against an accidental pre-validation memset. */
    {
        rtklib_shared_state_query_t valid_query;
        rtklib_shared_state_result_t full_state;
        rtklib_shared_bias_result_t full_bias;
        rtklib_shared_ion_result_t full_ion;
        rtklib_shared_signal_result_t full_signal;
        rtklib_shared_record_identity_t full_identity;
        unsigned char snapshot[sizeof(full_state)];
        short_result_buffer_t short_result;
        const double receiver_llh[3] = {0.5, 1.0, 100.0};
        const double azel[2] = {0.2, 0.8};
        double delay = 0.0, variance = 0.0;

        init_state_query(&valid_query, g_lnav->system, g_lnav->prn,
                         g_lnav->family, CODE_L1C, g_lnav->toe,
                         g_lnav->record_id);

        memset(&full_state, 0xA5, sizeof(full_state));
        full_state.abi_version = 0;
        full_state.struct_size = (uint32_t)sizeof(full_state);
        memcpy(snapshot, &full_state, sizeof(snapshot));
        CHECK(rtklib_shared_state_query(store, &valid_query, &full_state) ==
                  RTKLIB_SHARED_INVALID_ARGUMENT &&
              !memcmp(snapshot, &full_state, sizeof(snapshot)),
              "bad state-result ABI was written before validation");
        memset(&short_result, 0xA5, sizeof(short_result));
        short_result.abi_version = RTKLIB_SHARED_ABI_VERSION;
        short_result.struct_size = (uint32_t)(2 * sizeof(uint32_t));
        CHECK(rtklib_shared_state_query(store, &valid_query,
                                        (rtklib_shared_state_result_t *)&short_result) ==
                  RTKLIB_SHARED_INVALID_ARGUMENT &&
              bytes_are(short_result.canary, sizeof(short_result.canary), 0xA5),
              "short state-result buffer was written before validation");

        memset(&full_bias, 0xA5, sizeof(full_bias));
        full_bias.abi_version = 0;
        full_bias.struct_size = (uint32_t)sizeof(full_bias);
        CHECK(rtklib_shared_bias_query(store, &valid_query, &full_bias) ==
                  RTKLIB_SHARED_INVALID_ARGUMENT &&
              full_bias.abi_version == 0 && full_bias.status ==
                  (int32_t)0xA5A5A5A5,
              "bad bias-result ABI was written before validation");
        memset(&short_result, 0xA5, sizeof(short_result));
        short_result.abi_version = RTKLIB_SHARED_ABI_VERSION;
        short_result.struct_size = (uint32_t)(2 * sizeof(uint32_t));
        CHECK(rtklib_shared_bias_query(store, &valid_query,
                                       (rtklib_shared_bias_result_t *)&short_result) ==
                  RTKLIB_SHARED_INVALID_ARGUMENT &&
              bytes_are(short_result.canary, sizeof(short_result.canary), 0xA5),
              "short bias-result buffer was written before validation");

        memset(&full_ion, 0xA5, sizeof(full_ion));
        full_ion.abi_version = 0;
        full_ion.struct_size = (uint32_t)sizeof(full_ion);
        CHECK(rtklib_shared_ion_query(store, ion_gps->system,
                                      ion_gps->family, ion_gps->transmit_time,
                                      ion_gps->record_id, &full_ion) ==
                  RTKLIB_SHARED_INVALID_ARGUMENT && full_ion.abi_version == 0 &&
              full_ion.status == (int32_t)0xA5A5A5A5,
              "bad ION-result ABI was written before validation");
        memset(&short_result, 0xA5, sizeof(short_result));
        short_result.abi_version = RTKLIB_SHARED_ABI_VERSION;
        short_result.struct_size = (uint32_t)(2 * sizeof(uint32_t));
        CHECK(rtklib_shared_ion_query(store, ion_gps->system,
                                      ion_gps->family, ion_gps->transmit_time,
                                      ion_gps->record_id,
                                      (rtklib_shared_ion_result_t *)&short_result) ==
                  RTKLIB_SHARED_INVALID_ARGUMENT &&
              bytes_are(short_result.canary, sizeof(short_result.canary), 0xA5),
              "short ION-result buffer was written before validation");

        memset(&full_signal, 0xA5, sizeof(full_signal));
        full_signal.abi_version = 0;
        full_signal.struct_size = (uint32_t)sizeof(full_signal);
        CHECK(rtklib_shared_signal_query(RTKLIB_SHARED_SYS_GLO, 11, "1C", 0,
                                         store, &full_signal) ==
                  RTKLIB_SHARED_INVALID_ARGUMENT &&
              full_signal.abi_version == 0 && full_signal.system ==
                  (uint32_t)0xA5A5A5A5,
              "bad signal-result ABI was written before validation");
        memset(&short_result, 0xA5, sizeof(short_result));
        short_result.abi_version = RTKLIB_SHARED_ABI_VERSION;
        short_result.struct_size = (uint32_t)(2 * sizeof(uint32_t));
        CHECK(rtklib_shared_signal_query(RTKLIB_SHARED_SYS_GLO, 11, "1C", 0,
                                         store,
                                         (rtklib_shared_signal_result_t *)&short_result) ==
                  RTKLIB_SHARED_INVALID_ARGUMENT &&
              bytes_are(short_result.canary, sizeof(short_result.canary), 0xA5),
              "short signal-result buffer was written before validation");

        memset(&full_identity, 0xA5, sizeof(full_identity));
        full_identity.abi_version = 0;
        full_identity.struct_size = (uint32_t)sizeof(full_identity);
        CHECK(rtklib_shared_nav_record(store, g_lnav->record_id,
                                       &full_identity) ==
                  RTKLIB_SHARED_INVALID_ARGUMENT && full_identity.abi_version == 0 &&
              full_identity.record_id == (rtklib_shared_record_id_t)
                  UINT64_C(0xA5A5A5A5A5A5A5A5),
              "bad identity ABI was written before validation");
        memset(&short_result, 0xA5, sizeof(short_result));
        short_result.abi_version = RTKLIB_SHARED_ABI_VERSION;
        short_result.struct_size = (uint32_t)(2 * sizeof(uint32_t));
        CHECK(rtklib_shared_nav_record(store, g_lnav->record_id,
                                       (rtklib_shared_record_identity_t *)&short_result) ==
                  RTKLIB_SHARED_INVALID_ARGUMENT &&
              bytes_are(short_result.canary, sizeof(short_result.canary), 0xA5),
              "short identity buffer was written before validation");

        memset(&short_result, 0xA5, sizeof(short_result));
        short_result.abi_version = RTKLIB_SHARED_ABI_VERSION;
        short_result.struct_size = (uint32_t)(2 * sizeof(uint32_t));
        CHECK(rtklib_shared_iono(store, g_lnav->toe, g_lnav->system,
                                 g_lnav->family, g_lnav->prn,
                                 receiver_llh, azel, RTKLIB_SHARED_IONO_OFF,
                                 0, &delay, &variance,
                                 (rtklib_shared_record_identity_t *)&short_result) ==
                  RTKLIB_SHARED_INVALID_ARGUMENT &&
              bytes_are(short_result.canary, sizeof(short_result.canary), 0xA5),
              "short ION identity buffer was written before validation");
    }

    {
        rtklib_shared_signal_result_t signal = {0};
        uint32_t satellite = 0, parsed_system = 0, parsed_prn = 0;
        uint32_t qzs_satellite = 0, qzs_system = 0, qzs_prn = 0;
        char id[4] = {0}, qzs_id[4] = {0};
        CHECK(rtklib_shared_satellite_id(RTKLIB_SHARED_SYS_GPS, 1, id) ==
                  RTKLIB_SHARED_OK && !memcmp(id, "G01", 4) &&
              rtklib_shared_satellite_from_id(id, &parsed_system,
                                               &parsed_prn, &satellite) ==
                  RTKLIB_SHARED_OK && parsed_system == RTKLIB_SHARED_SYS_GPS &&
              parsed_prn == 1 && satellite != 0,
              "public satellite mapping is not round-trip stable");
        CHECK(rtklib_shared_satellite_id(RTKLIB_SHARED_SYS_QZS, 194, qzs_id) ==
                  RTKLIB_SHARED_OK && !memcmp(qzs_id, "J02", 4) &&
              rtklib_shared_satellite_from_id(qzs_id, &qzs_system, &qzs_prn,
                                               &qzs_satellite) ==
                  RTKLIB_SHARED_OK && qzs_system == RTKLIB_SHARED_SYS_QZS &&
              qzs_prn == 194 && qzs_satellite != 0,
              "public QZSS J02 mapping is not round-trip stable");
        signal.abi_version = RTKLIB_SHARED_ABI_VERSION;
        signal.struct_size = (uint32_t)sizeof(signal);
        CHECK(rtklib_shared_signal_query(RTKLIB_SHARED_SYS_GLO, 11, "1C", 0,
                                          store, &signal) == RTKLIB_SHARED_OK &&
              signal.glonass_fcn == 0 && signal.carrier_frequency_hz > 0.0,
              "FCN=0 signal mapping was not preserved");
    }

    /* Week 3551 overflows gpst2time()/bdt2time()'s historical int product on
     * this RTKLIB build.  Reject it at the public boundary rather than
     * allowing UBSan-visible arithmetic in a query implementation.  Keep
     * this last so the other independent ABI guards still execute on builds
     * where the production bound has not yet been fixed. */
    init_state_query(&query, g_lnav->system, g_lnav->prn, g_lnav->family,
                     CODE_L1C, g_lnav->toe, g_lnav->record_id);
    query.evaluation_time.week = 3551;
    query.selection_time.week = 3551;
    init_state_result(&state_result);
    CHECK(rtklib_shared_state_query(store, &query, &state_result) ==
              RTKLIB_SHARED_INVALID_ARGUMENT,
          "week 3551 reached RTKLIB integer-overflow time conversion");
    init_bias_result(&bias_result);
    CHECK(rtklib_shared_bias_query(store, &query, &bias_result) ==
              RTKLIB_SHARED_INVALID_ARGUMENT,
          "week 3551 reached bias-query time conversion");
    init_ion_result(&ion_result);
    {
        rtklib_shared_time_t bad_week_time = ion_gps->transmit_time;
        bad_week_time.week = 3551;
        CHECK(rtklib_shared_ion_query(store, RTKLIB_SHARED_SYS_GPS,
                                      RTKLIB_SHARED_NAV_LNAV,
                                      bad_week_time, 0, &ion_result) ==
                  RTKLIB_SHARED_INVALID_ARGUMENT,
              "week 3551 reached ION-query time conversion");
    }

    printf("PASS records=%zu eph_gps=%zu glo=%zu eph_gal=%zu eph_bds=%zu eph_qzs=%zu ion=%zu\n",
           rtklib_shared_nav_record_count(store, 0, 0),
           rtklib_shared_nav_record_count(store,
           RTKLIB_SHARED_RECORD_EPH, RTKLIB_SHARED_SYS_GPS),
           rtklib_shared_nav_record_count(store, RTKLIB_SHARED_RECORD_GLO_EPH,
           RTKLIB_SHARED_SYS_GLO), rtklib_shared_nav_record_count(store,
           RTKLIB_SHARED_RECORD_EPH, RTKLIB_SHARED_SYS_GAL),
           rtklib_shared_nav_record_count(store, RTKLIB_SHARED_RECORD_EPH,
           RTKLIB_SHARED_SYS_BDS), rtklib_shared_nav_record_count(store,
           RTKLIB_SHARED_RECORD_EPH, RTKLIB_SHARED_SYS_QZS),
           rtklib_shared_nav_record_count(store, RTKLIB_SHARED_RECORD_ION, 0));
    rtklib_shared_nav_destroy(store);
    freenav(&private_nav, 0x3ff);
    return 0;
}
