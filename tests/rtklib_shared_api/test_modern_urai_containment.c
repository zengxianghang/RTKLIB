/*
 * Public-only containment contract for Issue #20 Phase A.
 *
 * This translation unit includes only the downstream shared ABI header.  It
 * deliberately verifies the selected-record API after a real RINEX load and
 * never uses a private RTKLIB type as an oracle.
 */

#include "../../src/rtklib_shared_api.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FIXTURE_SOURCE_ID "fixture:BRD400DLR:2025001"

typedef struct {
    const char *label;
    uint32_t system;
    uint32_t prn;
    uint32_t family;
    int health_raw;
    int health;
} modern_target_t;

static int fail(const char *message)
{
    fprintf(stderr, "FAIL: %s\n", message);
    return 1;
}

#define CHECK(condition, message) \
    do { if (!(condition)) return fail(message); } while (0)

static int finite_close(double actual, double expected, double tolerance)
{
    return isfinite(actual) && isfinite(expected) &&
           fabs(actual - expected) <= tolerance;
}

static int same_time(rtklib_shared_time_t actual,
                     rtklib_shared_time_t expected)
{
    return actual.week == expected.week && isfinite(actual.sow) &&
           isfinite(expected.sow) && fabs(actual.sow - expected.sow) <= 1E-6;
}

/* Compare the complete public identity except reserved ABI bytes. */
static int same_identity(const rtklib_shared_record_identity_t *actual,
                         const rtklib_shared_record_identity_t *expected)
{
    return actual && expected &&
           actual->abi_version == expected->abi_version &&
           actual->struct_size == expected->struct_size &&
           actual->record_id == expected->record_id &&
           actual->record_kind == expected->record_kind &&
           actual->source_kind == expected->source_kind &&
           actual->system == expected->system &&
           actual->prn == expected->prn &&
           actual->family == expected->family &&
           actual->iode == expected->iode &&
           actual->iodc == expected->iodc &&
           actual->health_raw == expected->health_raw &&
           actual->glonass_fcn == expected->glonass_fcn &&
           actual->receive_order == expected->receive_order &&
           same_time(actual->toe, expected->toe) &&
           same_time(actual->toc, expected->toc) &&
           same_time(actual->transmit_time, expected->transmit_time) &&
           strcmp(actual->source_id, expected->source_id) == 0 &&
           strcmp(actual->family_subtype, expected->family_subtype) == 0;
}

static void init_state_query(rtklib_shared_state_query_t *query,
                             const rtklib_shared_record_identity_t *identity,
                             unsigned char code,
                             rtklib_shared_record_id_t selected_record_id)
{
    memset(query, 0, sizeof(*query));
    query->abi_version = RTKLIB_SHARED_ABI_VERSION;
    query->struct_size = (uint32_t)sizeof(*query);
    query->system = identity->system;
    query->prn = identity->prn;
    query->rtklib_code = code;
    query->glonass_fcn = RTKLIB_SHARED_GLO_FCN_UNKNOWN;
    query->family_mask = identity->family;
    query->evaluation_time = identity->toe;
    query->selection_time = identity->toe;
    query->selected_record_id = selected_record_id;
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

static int find_identity(const rtklib_shared_nav_store_t *store,
                         uint32_t system, uint32_t prn, uint32_t family,
                         rtklib_shared_record_identity_t *found)
{
    size_t i, count;
    rtklib_shared_record_identity_t identity;

    if (!found) return 0;
    count = rtklib_shared_nav_record_count(store, 0, 0);
    for (i = 0; i < count; ++i) {
        memset(&identity, 0, sizeof(identity));
        identity.abi_version = RTKLIB_SHARED_ABI_VERSION;
        identity.struct_size = (uint32_t)sizeof(identity);
        if (rtklib_shared_nav_record_at(store, i, &identity) !=
                RTKLIB_SHARED_OK)
            return 0;
        if (identity.record_kind == RTKLIB_SHARED_RECORD_EPH &&
            identity.system == system && identity.prn == prn &&
            identity.family == family) {
            *found = identity;
            return 1;
        }
    }
    return 0;
}

static int all_state_numbers_nan(const rtklib_shared_state_result_t *result)
{
    return result && isnan(result->position_ecef_m[0]) &&
           isnan(result->position_ecef_m[1]) &&
           isnan(result->position_ecef_m[2]) &&
           isnan(result->velocity_ecef_mps[0]) &&
           isnan(result->velocity_ecef_mps[1]) &&
           isnan(result->velocity_ecef_mps[2]) &&
           isnan(result->clock_bias_s) &&
           isnan(result->clock_drift_sps) &&
           isnan(result->variance_m2);
}

static int all_state_numbers_finite(const rtklib_shared_state_result_t *result)
{
    return result && isfinite(result->position_ecef_m[0]) &&
           isfinite(result->position_ecef_m[1]) &&
           isfinite(result->position_ecef_m[2]) &&
           isfinite(result->velocity_ecef_mps[0]) &&
           isfinite(result->velocity_ecef_mps[1]) &&
           isfinite(result->velocity_ecef_mps[2]) &&
           isfinite(result->clock_bias_s) &&
           isfinite(result->clock_drift_sps) &&
           isfinite(result->variance_m2);
}

static int check_modern_state(const rtklib_shared_nav_store_t *store,
                              const rtklib_shared_record_identity_t *identity,
                              const modern_target_t *target)
{
    rtklib_shared_state_query_t query;
    rtklib_shared_state_result_t state;
    rtklib_shared_bias_result_t bias;
    int status;

    init_state_query(&query, identity, 1 /* CODE_L1C */, identity->record_id);
    init_state_result(&state);
    status = rtklib_shared_state_query(store, &query, &state);
    if (status != RTKLIB_SHARED_UNSUPPORTED ||
        state.status != RTKLIB_SHARED_QUERY_UNSUPPORTED) {
        fprintf(stderr, "FAIL: %s modern state was published as available\n",
                target->label);
        return 0;
    }
    if (!same_identity(&state.identity, identity) ||
        state.identity.source_kind != RTKLIB_SHARED_SOURCE_RINEX ||
        strcmp(state.identity.source_id, FIXTURE_SOURCE_ID) != 0 ||
        state.health_raw != target->health_raw ||
        state.health != target->health || state.state_valid != 0 ||
        !all_state_numbers_nan(&state)) {
        fprintf(stderr, "FAIL: %s modern containment result lost fields\n",
                target->label);
        return 0;
    }

    /* Accuracy containment must not disable the independently selected bias
     * operation or cause it to select a different record. */
    init_bias_result(&bias);
    status = rtklib_shared_bias_query(store, &query, &bias);
    if (status != RTKLIB_SHARED_OK ||
        bias.status != RTKLIB_SHARED_QUERY_AVAILABLE ||
        !same_identity(&bias.identity, identity) ||
        !isfinite(bias.raw_code_bias_m)) {
        fprintf(stderr, "FAIL: %s selected bias lost independence\n",
                target->label);
        return 0;
    }
    return 1;
}

static int check_available_state(const rtklib_shared_nav_store_t *store,
                                 const rtklib_shared_record_identity_t *identity,
                                 unsigned char code, double expected_variance)
{
    rtklib_shared_state_query_t query;
    rtklib_shared_state_result_t result;
    int status;

    init_state_query(&query, identity, code, identity->record_id);
    init_state_result(&result);
    status = rtklib_shared_state_query(store, &query, &result);
    CHECK(status == RTKLIB_SHARED_OK &&
          result.status == RTKLIB_SHARED_QUERY_AVAILABLE &&
          result.state_valid == 1 && all_state_numbers_finite(&result),
          "legacy/BDS state was not available with finite fields");
    CHECK(same_identity(&result.identity, identity) &&
          result.health_raw == identity->health_raw,
          "legacy/BDS state lost selected identity or raw health");
    CHECK(finite_close(result.variance_m2, expected_variance, 1E-9),
          "legacy/BDS metric variance changed");
    return 0;
}

int main(int argc, char **argv)
{
    static const modern_target_t targets[] = {
        {"GPS G01 CNAV", RTKLIB_SHARED_SYS_GPS, 1,
         RTKLIB_SHARED_NAV_CNAV, 7, RTKLIB_SHARED_HEALTH_UNHEALTHY},
        {"QZSS J02 CNAV", RTKLIB_SHARED_SYS_QZS, 194,
         RTKLIB_SHARED_NAV_CNAV, 0, RTKLIB_SHARED_HEALTH_HEALTHY},
        {"QZSS J02 CNV2", RTKLIB_SHARED_SYS_QZS, 194,
         RTKLIB_SHARED_NAV_CNV2, 0, RTKLIB_SHARED_HEALTH_HEALTHY}
    };
    rtklib_shared_nav_store_t *store;
    rtklib_shared_record_identity_t modern[3];
    rtklib_shared_record_identity_t g01_lnav;
    rtklib_shared_record_identity_t bds_cnv2;
    rtklib_shared_state_query_t query;
    rtklib_shared_state_result_t state;
    rtklib_shared_bias_result_t bias;
    FILE *fixture;
    size_t i;
    int status;

    if (argc != 2)
        return fail("usage: test_modern_urai_containment <RINEX4>");
    fixture = fopen(argv[1], "rb");
    if (!fixture) return fail("RINEX fixture path could not be opened");
    fclose(fixture);

    store = rtklib_shared_nav_create();
    if (!store) return fail("shared NAV store could not be created");
    status = rtklib_shared_nav_load_rinex(store, argv[1], "",
                                          FIXTURE_SOURCE_ID);
    if (status != RTKLIB_SHARED_OK) {
        rtklib_shared_nav_destroy(store);
        return fail("RINEX fixture load failed closed");
    }

    for (i = 0; i < sizeof(targets) / sizeof(targets[0]); ++i) {
        if (!find_identity(store, targets[i].system, targets[i].prn,
                           targets[i].family, &modern[i]) ||
            !check_modern_state(store, &modern[i], &targets[i])) {
            rtklib_shared_nav_destroy(store);
            return 1;
        }
    }

    /* Exercise the declared default selector separately from explicit ID
     * selection.  Exact CNAV family selection must not fall back to LNAV. */
    init_state_query(&query, &modern[0], 1 /* CODE_L1C */, 0);
    query.family_mask = 0; /* exercise the true default family policy */
    init_state_result(&state);
    status = rtklib_shared_state_query(store, &query, &state);
    CHECK(status == RTKLIB_SHARED_UNSUPPORTED &&
          state.status == RTKLIB_SHARED_QUERY_UNSUPPORTED &&
          state.state_valid == 0 && all_state_numbers_nan(&state) &&
          same_identity(&state.identity, &modern[0]) &&
          state.health_raw == targets[0].health_raw &&
          state.health == targets[0].health,
          "default modern selection did not remain contained");

    /* A selected record with a wrong family is an error, not a re-selection.
     * Bias follows the same selected-ID rule independently. */
    init_state_query(&query, &modern[0], 1 /* CODE_L1C */, modern[0].record_id);
    query.family_mask = RTKLIB_SHARED_NAV_CNV2;
    init_state_result(&state);
    CHECK(rtklib_shared_state_query(store, &query, &state) ==
              RTKLIB_SHARED_UNSUPPORTED &&
          state.status == RTKLIB_SHARED_QUERY_UNSUPPORTED &&
          state.state_valid == 0 && all_state_numbers_nan(&state) &&
          same_identity(&state.identity, &modern[0]),
          "wrong-family modern selection was silently replaced");
    init_bias_result(&bias);
    CHECK(rtklib_shared_bias_query(store, &query, &bias) ==
              RTKLIB_SHARED_UNSUPPORTED &&
          bias.status == RTKLIB_SHARED_QUERY_UNSUPPORTED &&
          isnan(bias.raw_code_bias_m) &&
          same_identity(&bias.identity, &modern[0]),
          "wrong-family bias selection was silently replaced");

    /* A stale ID must never fall back and must not leave stale identity or
     * finite output in a caller-owned result. */
    init_state_query(&query, &modern[0], 1 /* CODE_L1C */, UINT64_MAX);
    init_state_result(&state);
    status = rtklib_shared_state_query(store, &query, &state);
    CHECK(status == RTKLIB_SHARED_INVALID_ARGUMENT &&
          state.status == RTKLIB_SHARED_QUERY_FAILED &&
          state.identity.record_id == 0 && all_state_numbers_nan(&state),
          "stale selected ID did not fail closed");

    CHECK(find_identity(store, RTKLIB_SHARED_SYS_GPS, 1,
                        RTKLIB_SHARED_NAV_LNAV, &g01_lnav) &&
          find_identity(store, RTKLIB_SHARED_SYS_BDS, 19,
                        RTKLIB_SHARED_NAV_CNV2, &bds_cnv2),
          "legacy GPS or BDS CNV2 identity was not loaded");
    CHECK(check_available_state(store, &g01_lnav, 1 /* CODE_L1C */, 4.0) == 0,
          "GPS LNAV metric SVA regression");
    CHECK(check_available_state(store, &bds_cnv2, 2 /* CODE_L1P */, 225.0) == 0,
          "BDS CNV2 metric SVA regression");

    rtklib_shared_nav_destroy(store);
    puts("modern_urai_containment: PASS (GPS/QZSS Phase A; GPS CNV2 coverage gap: real provenance fixture not available; NOT_RUN)");
    return 0;
}
