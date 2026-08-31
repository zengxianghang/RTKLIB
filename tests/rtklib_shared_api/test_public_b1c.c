/* Public-only BDS B1C signal contract.
 *
 * This translation unit intentionally includes no RTKLIB-private header.  The
 * navigation fixture contains records, not observation epochs, so this test
 * covers public code/family/frequency/bias metadata only; real B1C OBS/PVT is
 * outside the fixture and remains NOT_RUN.
 */

#include "../../src/rtklib_shared_api.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition, message) do { \
    if (!(condition)) { fprintf(stderr, "FAIL: %s\n", message); return 1; } \
} while (0)

static void init_identity(rtklib_shared_record_identity_t *identity)
{
    memset(identity, 0, sizeof(*identity));
    identity->abi_version = RTKLIB_SHARED_ABI_VERSION;
    identity->struct_size = (uint32_t)sizeof(*identity);
}

static void init_signal(rtklib_shared_signal_result_t *result)
{
    memset(result, 0, sizeof(*result));
    result->abi_version = RTKLIB_SHARED_ABI_VERSION;
    result->struct_size = (uint32_t)sizeof(*result);
}

static void init_query(const rtklib_shared_record_identity_t *identity,
                       unsigned char code,
                       rtklib_shared_state_query_t *query)
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
    query->selected_record_id = identity->record_id;
}

static void init_bias(rtklib_shared_bias_result_t *result)
{
    memset(result, 0, sizeof(*result));
    result->abi_version = RTKLIB_SHARED_ABI_VERSION;
    result->struct_size = (uint32_t)sizeof(*result);
}

static int close_hz(double actual, double expected)
{
    return isfinite(actual) && fabs(actual - expected) <= 0.5;
}

static int has_family(uint32_t mask, uint32_t family)
{
    return (mask & family) != 0;
}

int main(int argc, char **argv)
{
    rtklib_shared_nav_store_t *store;
    rtklib_shared_record_identity_t identity;
    rtklib_shared_record_identity_t bds_d2 = {0};
    rtklib_shared_record_identity_t bds_cnv1 = {0};
    rtklib_shared_record_identity_t bds_cnv2 = {0};
    rtklib_shared_record_identity_t bds_cnv3 = {0};
    rtklib_shared_signal_result_t signal;
    rtklib_shared_state_query_t query;
    rtklib_shared_bias_result_t bias;
    size_t i, count;
    int have_d2 = 0, have_cnv1 = 0, have_cnv2 = 0, have_cnv3 = 0;

    CHECK(argc == 2, "usage: test_public_b1c RINEX4_NAV_FIXTURE");
    store = rtklib_shared_nav_create();
    CHECK(store != NULL, "shared store allocation failed");
    CHECK(rtklib_shared_nav_load_rinex(store, argv[1], "",
                                       "fixture:public-b1c") ==
              RTKLIB_SHARED_OK,
          "RINEX navigation fixture failed to load");

    count = rtklib_shared_nav_record_count(store, 0, 0);
    for (i = 0; i < count; ++i) {
        init_identity(&identity);
        CHECK(rtklib_shared_nav_record_at(store, i, &identity) ==
                  RTKLIB_SHARED_OK,
              "public catalogue lookup failed");
        if (identity.record_kind != RTKLIB_SHARED_RECORD_EPH ||
            identity.system != RTKLIB_SHARED_SYS_BDS)
            continue;
        if (identity.family == RTKLIB_SHARED_NAV_D2) {
            bds_d2 = identity;
            have_d2 = 1;
        } else if (identity.prn == 19 &&
                   identity.family == RTKLIB_SHARED_NAV_CNV1) {
            bds_cnv1 = identity;
            have_cnv1 = 1;
        } else if (identity.prn == 19 &&
                   identity.family == RTKLIB_SHARED_NAV_CNV2) {
            bds_cnv2 = identity;
            have_cnv2 = 1;
        } else if (identity.prn == 19 &&
                   identity.family == RTKLIB_SHARED_NAV_CNV3) {
            bds_cnv3 = identity;
            have_cnv3 = 1;
        }
    }
    CHECK(have_d2 && have_cnv1 && have_cnv2 && have_cnv3,
          "fixture does not contain all BDS family selectors");

    init_signal(&signal);
    CHECK(rtklib_shared_signal_query(RTKLIB_SHARED_SYS_BDS, 19, "1P",
                                     RTKLIB_SHARED_GLO_FCN_UNKNOWN, store,
                                     &signal) == RTKLIB_SHARED_OK &&
              signal.rtklib_code == RTKLIB_SHARED_CODE_RINEX_1P &&
              signal.frequency_index == 0 &&
              close_hz(signal.carrier_frequency_hz, 1575420000.0) &&
              close_hz(signal.wavelength_m, 299792458.0 / 1575420000.0) &&
              has_family(signal.family_mask, RTKLIB_SHARED_NAV_CNV1) &&
              has_family(signal.family_mask, RTKLIB_SHARED_NAV_CNV2) &&
              !has_family(signal.family_mask, RTKLIB_SHARED_NAV_D1) &&
              !has_family(signal.family_mask, RTKLIB_SHARED_NAV_D2),
          "BDS B1C pilot mapping is not canonical");

    init_signal(&signal);
    CHECK(rtklib_shared_signal_query(RTKLIB_SHARED_SYS_BDS, 19, "1D",
                                     RTKLIB_SHARED_GLO_FCN_UNKNOWN, store,
                                     &signal) == RTKLIB_SHARED_OK &&
              signal.rtklib_code == RTKLIB_SHARED_CODE_RINEX_1D &&
              close_hz(signal.carrier_frequency_hz, 1575420000.0) &&
              signal.family_mask == RTKLIB_SHARED_NAV_CNV1,
          "BDS B1C data mapping is not canonical");

    init_signal(&signal);
    CHECK(rtklib_shared_signal_query(RTKLIB_SHARED_SYS_BDS, 19, "1X",
                                     RTKLIB_SHARED_GLO_FCN_UNKNOWN, store,
                                     &signal) == RTKLIB_SHARED_OK &&
              signal.rtklib_code == RTKLIB_SHARED_CODE_RINEX_1X &&
              close_hz(signal.carrier_frequency_hz, 1575420000.0) &&
              signal.family_mask ==
                  (RTKLIB_SHARED_NAV_CNV1 | RTKLIB_SHARED_NAV_CNV2),
          "BDS B1C combined mapping is not canonical");

    init_signal(&signal);
    CHECK(rtklib_shared_signal_query(RTKLIB_SHARED_SYS_BDS, 19, "2I",
                                     RTKLIB_SHARED_GLO_FCN_UNKNOWN, store,
                                     &signal) == RTKLIB_SHARED_OK &&
              signal.rtklib_code == RTKLIB_SHARED_CODE_RINEX_2I &&
              close_hz(signal.carrier_frequency_hz, 1561098000.0) &&
              signal.family_mask == (RTKLIB_SHARED_NAV_D1 |
                                     RTKLIB_SHARED_NAV_D2 |
                                     RTKLIB_SHARED_NAV_D1D2),
          "BDS B1I mapping regressed");

    init_signal(&signal);
    CHECK(rtklib_shared_signal_query(RTKLIB_SHARED_SYS_BDS, 19, "1C",
                                     RTKLIB_SHARED_GLO_FCN_UNKNOWN, store,
                                     &signal) == RTKLIB_SHARED_UNAVAILABLE,
          "generic BDS 1C alias was accepted");
    init_signal(&signal);
    CHECK(rtklib_shared_signal_query(RTKLIB_SHARED_SYS_BDS, 19, "B1C",
                                     RTKLIB_SHARED_GLO_FCN_UNKNOWN, store,
                                     &signal) == RTKLIB_SHARED_INVALID_ARGUMENT,
          "three-character B1C alias bypassed the canonical RINEX API");

    init_query(&bds_cnv1, RTKLIB_SHARED_CODE_RINEX_1P, &query);
    init_bias(&bias);
    CHECK(rtklib_shared_bias_query(store, &query, &bias) ==
              RTKLIB_SHARED_OK &&
              bias.status == RTKLIB_SHARED_QUERY_AVAILABLE &&
              bias.identity.record_id == bds_cnv1.record_id &&
              isfinite(bias.raw_code_bias_m),
          "CNV1 B1C pilot bias was not available");

    init_query(&bds_cnv1, RTKLIB_SHARED_CODE_RINEX_1D, &query);
    init_bias(&bias);
    CHECK(rtklib_shared_bias_query(store, &query, &bias) ==
              RTKLIB_SHARED_OK &&
              bias.status == RTKLIB_SHARED_QUERY_AVAILABLE &&
              bias.identity.record_id == bds_cnv1.record_id &&
              isfinite(bias.raw_code_bias_m),
          "CNV1 B1C data bias was not available");

    init_query(&bds_cnv2, RTKLIB_SHARED_CODE_RINEX_1D, &query);
    init_bias(&bias);
    CHECK(rtklib_shared_bias_query(store, &query, &bias) ==
              RTKLIB_SHARED_UNSUPPORTED &&
              bias.status == RTKLIB_SHARED_QUERY_UNSUPPORTED &&
              bias.identity.record_id == bds_cnv2.record_id,
          "CNV2 B1C data incorrectly guessed ISC_B1Cd");

    init_query(&bds_cnv1, RTKLIB_SHARED_CODE_RINEX_1X, &query);
    init_bias(&bias);
    CHECK(rtklib_shared_bias_query(store, &query, &bias) ==
              RTKLIB_SHARED_UNSUPPORTED &&
              bias.status == RTKLIB_SHARED_QUERY_UNSUPPORTED &&
              bias.identity.record_id == bds_cnv1.record_id,
          "B1C combined bias incorrectly guessed");

    init_query(&bds_cnv3, RTKLIB_SHARED_CODE_RINEX_1P, &query);
    init_bias(&bias);
    CHECK(rtklib_shared_bias_query(store, &query, &bias) ==
              RTKLIB_SHARED_UNSUPPORTED &&
              bias.status == RTKLIB_SHARED_QUERY_UNSUPPORTED &&
              bias.identity.record_id == bds_cnv3.record_id,
          "B1C pilot incorrectly crossed into CNV3");

    init_query(&bds_d2, RTKLIB_SHARED_CODE_RINEX_2I, &query);
    init_bias(&bias);
    CHECK(rtklib_shared_bias_query(store, &query, &bias) ==
              RTKLIB_SHARED_OK &&
              bias.status == RTKLIB_SHARED_QUERY_AVAILABLE &&
              bias.identity.record_id == bds_d2.record_id &&
              isfinite(bias.raw_code_bias_m),
          "BDS D2 B1I bias regressed");

    rtklib_shared_nav_destroy(store);
    puts("public BDS B1C signal contract: PASS (real B1C OBS/PVT: NOT_RUN)");
    return 0;
}
