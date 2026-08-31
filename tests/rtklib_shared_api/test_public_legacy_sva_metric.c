/* Public-only legacy-family metric SVA regression; this file must not include
 * rtklib.h.  These tests are unit/adapter checks, not cross-backend parity. */
#include "../../src/rtklib_shared_api.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fail(const char *message)
{
    fprintf(stderr, "FAIL: %s\n", message);
    return 1;
}

static int close_finite(double actual, double expected, double tolerance)
{
    return isfinite(actual) && isfinite(expected) &&
           fabs(actual - expected) <= tolerance;
}

static int same_time(rtklib_shared_time_t first,
                     rtklib_shared_time_t second)
{
    return first.week == second.week &&
           close_finite(first.sow, second.sow, 1E-6);
}

static int same_identity(const rtklib_shared_record_identity_t *first,
                         const rtklib_shared_record_identity_t *second)
{
    return first && second &&
           first->abi_version == second->abi_version &&
           first->struct_size == second->struct_size &&
           first->record_id == second->record_id &&
           first->record_kind == second->record_kind &&
           first->source_kind == second->source_kind &&
           first->system == second->system &&
           first->prn == second->prn &&
           first->family == second->family &&
           first->iode == second->iode && first->iodc == second->iodc &&
           first->health_raw == second->health_raw &&
           first->glonass_fcn == second->glonass_fcn &&
           first->receive_order == second->receive_order &&
           same_time(first->toe, second->toe) &&
           same_time(first->toc, second->toc) &&
           same_time(first->transmit_time, second->transmit_time) &&
           strcmp(first->source_id, second->source_id) == 0 &&
           strcmp(first->family_subtype, second->family_subtype) == 0;
}

static void init_eph(rtklib_shared_eph_input_t *eph, double sva_m,
                     uint64_t receive_order)
{
    memset(eph, 0, sizeof(*eph));
    eph->abi_version = RTKLIB_SHARED_ABI_VERSION;
    eph->struct_size = (uint32_t)sizeof(*eph);
    eph->system = RTKLIB_SHARED_SYS_GPS;
    eph->prn = 1;
    eph->family = RTKLIB_SHARED_NAV_LNAV;
    eph->iode = 63;
    eph->iodc = 63;
    eph->health_raw = 0;
    eph->broadcast_week = 2000;
    eph->broadcast_toe_sow = 345600.0;
    eph->broadcast_transmit_sow = 341670.0;
    eph->sva_m = sva_m;
    eph->toe.week = 2000;
    eph->toe.sow = 345600.0;
    eph->toc.week = 2000;
    eph->toc.sow = 0.0;
    eph->transmit_time.week = 2000;
    eph->transmit_time.sow = 341670.0;
    eph->semi_major_axis_m = 26560000.0;
    eph->eccentricity = 0.01;
    eph->inclination_rad = 0.95;
    eph->raan_rad = 1.0;
    eph->arg_perigee_rad = 0.5;
    eph->mean_anomaly_rad = 0.2;
    eph->fit_interval_h = 4.0;
    eph->source_id[0] = 't';
    eph->source_id[1] = '0';
    eph->family_subtype[0] = 'L';
    eph->receive_order = receive_order;
}

static int query_state_for_record(rtklib_shared_nav_store_t *store,
                                  rtklib_shared_record_id_t record_id,
                                  rtklib_shared_state_result_t *result)
{
    rtklib_shared_record_identity_t identity;
    rtklib_shared_state_query_t query;
    int status;

    if (!result) return RTKLIB_SHARED_INVALID_ARGUMENT;
    memset(&identity, 0, sizeof(identity));
    identity.abi_version = RTKLIB_SHARED_ABI_VERSION;
    identity.struct_size = (uint32_t)sizeof(identity);
    status = rtklib_shared_nav_record(store, record_id, &identity);
    if (status != RTKLIB_SHARED_OK) return fail("public record lookup failed");

    memset(&query, 0, sizeof(query));
    query.abi_version = RTKLIB_SHARED_ABI_VERSION;
    query.struct_size = (uint32_t)sizeof(query);
    query.system = identity.system;
    query.prn = identity.prn;
    query.family_mask = identity.family;
    query.rtklib_code = 1; /* CODE_L1C, kept numeric for public-only ABI. */
    query.evaluation_time = identity.toe;
    query.selection_time = identity.toe;
    query.selected_record_id = record_id;

    memset(result, 0, sizeof(*result));
    result->abi_version = RTKLIB_SHARED_ABI_VERSION;
    result->struct_size = (uint32_t)sizeof(*result);
    status = rtklib_shared_state_query(store, &query, result);
    return status;
}

static int same_state_result(const rtklib_shared_state_result_t *first,
                             const rtklib_shared_state_result_t *second)
{
    int i;

    if (!first || !second ||
        first->status != second->status ||
        first->health != second->health ||
        first->health_raw != second->health_raw ||
        first->state_valid != second->state_valid ||
        !same_identity(&first->identity, &second->identity) ||
        !close_finite(first->variance_m2, second->variance_m2, 1E-9) ||
        !close_finite(first->clock_bias_s, second->clock_bias_s, 1E-15) ||
        !close_finite(first->clock_drift_sps, second->clock_drift_sps, 1E-18))
        return 0;
    for (i = 0; i < 3; ++i) {
        if (!close_finite(first->position_ecef_m[i],
                         second->position_ecef_m[i], 1E-6) ||
            !close_finite(first->velocity_ecef_mps[i],
                         second->velocity_ecef_mps[i], 1E-9))
            return 0;
    }
    return 1;
}

static int query_variance(rtklib_shared_nav_store_t *store,
                          rtklib_shared_record_id_t record_id,
                          double expected_variance,
                          int expected_health_raw, int expected_health)
{
    rtklib_shared_state_result_t result;
    int status = query_state_for_record(store, record_id, &result);

    if (status != RTKLIB_SHARED_OK ||
        result.status != RTKLIB_SHARED_QUERY_AVAILABLE ||
        result.health_raw != expected_health_raw ||
        result.health != expected_health || result.state_valid == 0 ||
        !close_finite(result.variance_m2, expected_variance, 1E-9)) {
        return fail("public state identity, health, or SVA variance mismatch");
    }
    return 0;
}

static int check_normalized_variance(double sva_m, double expected_variance,
                                     uint64_t receive_order)
{
    rtklib_shared_eph_input_t eph;
    rtklib_shared_nav_store_t *store;
    rtklib_shared_record_id_t record_id = 0;
    int status;

    init_eph(&eph, sva_m, receive_order);
    store = rtklib_shared_nav_create();
    if (!store) return fail("normalized SVA store allocation failed");
    status = rtklib_shared_nav_insert_eph(store, &eph, &record_id);
    if (status != RTKLIB_SHARED_OK || record_id == 0) {
        rtklib_shared_nav_destroy(store);
        return fail("normalized SVA insertion failed");
    }
    status = query_variance(store, record_id, expected_variance, 0,
                            RTKLIB_SHARED_HEALTH_HEALTHY);
    rtklib_shared_nav_destroy(store);
    return status;
}

static int check_nonfinite_sva_rejected(void)
{
    const double values[] = {NAN, INFINITY, -INFINITY};
    rtklib_shared_nav_store_t *store;
    size_t before, after, i;
    int status;

    store = rtklib_shared_nav_create();
    if (!store) return fail("nonfinite SVA store allocation failed");
    before = rtklib_shared_nav_record_count(store,
                                             RTKLIB_SHARED_RECORD_EPH,
                                             RTKLIB_SHARED_SYS_GPS);
    for (i = 0; i < sizeof(values) / sizeof(values[0]); ++i) {
        rtklib_shared_eph_input_t eph;
        init_eph(&eph, values[i], 4 + (uint64_t)i);
        status = rtklib_shared_nav_insert_eph(store, &eph, NULL);
        after = rtklib_shared_nav_record_count(store,
                                                RTKLIB_SHARED_RECORD_EPH,
                                                RTKLIB_SHARED_SYS_GPS);
        if (status != RTKLIB_SHARED_INVALID_ARGUMENT || after != before) {
            rtklib_shared_nav_destroy(store);
            return fail("nonfinite SVA was not rejected without publishing a record");
        }
    }
    rtklib_shared_nav_destroy(store);
    return 0;
}

static int check_loaded_fixture(const char *path)
{
    rtklib_shared_nav_store_t *store;
    rtklib_shared_record_identity_t identity;
    rtklib_shared_record_id_t record_id = 0;
    size_t i, count;
    int status;

    store = rtklib_shared_nav_create();
    if (!store) return fail("RINEX2 public store allocation failed");
    status = rtklib_shared_nav_load_rinex(store, path, "", "fixture:brdc1820");
    if (status != RTKLIB_SHARED_OK) {
        rtklib_shared_nav_destroy(store);
        return fail("RINEX2 fixture failed through public loader");
    }
    count = rtklib_shared_nav_record_count(store, 0, 0);
    for (i = 0; i < count; ++i) {
        memset(&identity, 0, sizeof(identity));
        identity.abi_version = RTKLIB_SHARED_ABI_VERSION;
        identity.struct_size = (uint32_t)sizeof(identity);
        if (rtklib_shared_nav_record_at(store, i, &identity) !=
                RTKLIB_SHARED_OK)
            continue;
        if (identity.record_kind == RTKLIB_SHARED_RECORD_EPH &&
            identity.system == RTKLIB_SHARED_SYS_GPS && identity.prn == 1 &&
            identity.family == RTKLIB_SHARED_NAV_LNAV &&
            identity.iode == 63 && identity.toe.week == 1590 &&
            close_finite(identity.toe.sow, 345600.0, 1E-6)) {
            record_id = identity.record_id;
            break;
        }
    }
    if (!record_id) {
        rtklib_shared_nav_destroy(store);
        return fail("RINEX2 G01 public identity was not found");
    }
    if (identity.record_kind != RTKLIB_SHARED_RECORD_EPH ||
        identity.source_kind != RTKLIB_SHARED_SOURCE_RINEX ||
        identity.system != RTKLIB_SHARED_SYS_GPS || identity.prn != 1 ||
        identity.family != RTKLIB_SHARED_NAV_LNAV || identity.iode != 63 ||
        identity.iodc != 63 || identity.health_raw != 63 ||
        identity.glonass_fcn != RTKLIB_SHARED_GLO_FCN_UNKNOWN ||
        identity.receive_order != 1 ||
        !same_time(identity.toe, (rtklib_shared_time_t){1590, 345600.0}) ||
        !same_time(identity.toc, (rtklib_shared_time_t){1590, 345600.0}) ||
        !same_time(identity.transmit_time,
                   (rtklib_shared_time_t){1590, 341670.0}) ||
        strcmp(identity.source_id, "fixture:brdc1820") != 0 ||
        identity.family_subtype[0] != '\0') {
        rtklib_shared_nav_destroy(store);
        return fail("RINEX2 G01 public identity or source metadata mismatch");
    }
    status = query_variance(store, record_id, 4.0, 63,
                            RTKLIB_SHARED_HEALTH_UNHEALTHY);
    rtklib_shared_nav_destroy(store);
    return status;
}

static int check_failed_load_rollback(const char *path)
{
    rtklib_shared_nav_store_t *store;
    rtklib_shared_record_identity_t *before;
    rtklib_shared_record_identity_t after;
    rtklib_shared_state_result_t before_state, after_state;
    rtklib_shared_record_id_t state_record_id = 0;
    size_t count, i;
    int status;

    store = rtklib_shared_nav_create();
    if (!store) return fail("rollback store allocation failed");
    status = rtklib_shared_nav_load_rinex(store, path, "",
                                           "fixture:rollback");
    if (status != RTKLIB_SHARED_OK) {
        rtklib_shared_nav_destroy(store);
        return fail("rollback baseline fixture load failed");
    }
    count = rtklib_shared_nav_record_count(store, 0, 0);
    if (count == 0 || count > SIZE_MAX / sizeof(*before)) {
        rtklib_shared_nav_destroy(store);
        return fail("rollback baseline catalogue is invalid");
    }
    before = (rtklib_shared_record_identity_t *)calloc(count, sizeof(*before));
    if (!before) {
        rtklib_shared_nav_destroy(store);
        return fail("rollback catalogue snapshot allocation failed");
    }
    for (i = 0; i < count; ++i) {
        memset(&before[i], 0, sizeof(before[i]));
        before[i].abi_version = RTKLIB_SHARED_ABI_VERSION;
        before[i].struct_size = (uint32_t)sizeof(before[i]);
        if (rtklib_shared_nav_record_at(store, i, &before[i]) !=
                RTKLIB_SHARED_OK) {
            free(before);
            rtklib_shared_nav_destroy(store);
            return fail("rollback catalogue snapshot failed");
        }
    }
    for (i = 0; i < count; ++i) {
        if (before[i].record_kind == RTKLIB_SHARED_RECORD_EPH &&
            before[i].system == RTKLIB_SHARED_SYS_GPS && before[i].prn == 1) {
            state_record_id = before[i].record_id;
            break;
        }
    }
    if (!state_record_id ||
        query_state_for_record(store, state_record_id, &before_state) !=
            RTKLIB_SHARED_OK) {
        free(before);
        rtklib_shared_nav_destroy(store);
        return fail("rollback baseline state query failed");
    }
    status = rtklib_shared_nav_load_rinex(
        store, "fixtures/does-not-exist-for-sva-rollback.rnx", "",
        "fixture:failed-load");
    if (status != RTKLIB_SHARED_IO_ERROR ||
        rtklib_shared_nav_record_count(store, 0, 0) != count) {
        free(before);
        rtklib_shared_nav_destroy(store);
        return fail("failed RINEX load changed the public catalogue");
    }
    for (i = 0; i < count; ++i) {
        memset(&after, 0, sizeof(after));
        after.abi_version = RTKLIB_SHARED_ABI_VERSION;
        after.struct_size = (uint32_t)sizeof(after);
        if (rtklib_shared_nav_record_at(store, i, &after) !=
                RTKLIB_SHARED_OK || !same_identity(&before[i], &after)) {
            free(before);
            rtklib_shared_nav_destroy(store);
            return fail("failed RINEX load changed a catalogue identity");
        }
    }
    if (query_state_for_record(store, state_record_id, &after_state) !=
            RTKLIB_SHARED_OK || !same_state_result(&before_state, &after_state)) {
        free(before);
        rtklib_shared_nav_destroy(store);
        return fail("failed RINEX load changed the selected state result");
    }
    free(before);
    rtklib_shared_nav_destroy(store);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc != 2)
        return fail("usage: test_public_legacy_sva_metric <RINEX2 NAV>");
    if (check_loaded_fixture(argv[1])) return 1;
    if (check_failed_load_rollback(argv[1])) return 1;
    if (check_normalized_variance(15.0, 225.0, 1)) return 1;
    if (check_normalized_variance(0.0, 0.0, 2)) return 1;
    if (check_normalized_variance(-1.0, 6144.0 * 6144.0, 3)) return 1;
    if (check_nonfinite_sva_rejected()) return 1;
    puts("public_legacy_sva_metric: PASS (unit/adapter checks; not cross-backend parity)");
    return 0;
}
