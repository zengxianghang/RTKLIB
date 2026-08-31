/* Public-only ABI consumer: no RTKLIB private header is allowed here. */
#include "../../src/rtklib_shared_api.h"

#include <stdint.h>

int main(void)
{
    rtklib_shared_eph_input_t eph = {0};
    rtklib_shared_glo_eph_input_t glo = {0};
    rtklib_shared_ion_input_t ion = {0};
    rtklib_shared_record_identity_t identity = {0};
    rtklib_shared_state_query_t state_query = {0};
    rtklib_shared_state_result_t state_result = {0};
    rtklib_shared_signal_result_t signal_result = {0};
    rtklib_shared_bias_result_t bias_result = {0};
    rtklib_shared_ion_result_t ion_result = {0};
    rtklib_shared_nav_store_t *store;
    rtklib_shared_record_id_t record_id = 0;
    rtklib_shared_time_t time = {0, 0.0};
    double llh[3] = {0.0, 0.0, 0.0};
    double ecef[3] = {0.0, 0.0, 0.0};
    double satellite[3] = {20200000.0, 0.0, 0.0};
    double los[3] = {0.0, 0.0, 0.0};
    double azel[2] = {0.0, 1.0};
    double range = 0.0, delay = 0.0, variance = 0.0;
    uint32_t satellite_number = 0, system = 0, prn = 0;
    char id[4] = {0};
    int result = 0;

    eph.abi_version = RTKLIB_SHARED_ABI_VERSION;
    eph.struct_size = (uint32_t)sizeof(eph);
    identity.abi_version = RTKLIB_SHARED_ABI_VERSION;
    identity.struct_size = (uint32_t)sizeof(identity);
    state_query.abi_version = RTKLIB_SHARED_ABI_VERSION;
    state_query.struct_size = (uint32_t)sizeof(state_query);
    state_result.abi_version = RTKLIB_SHARED_ABI_VERSION;
    state_result.struct_size = (uint32_t)sizeof(state_result);
    signal_result.abi_version = RTKLIB_SHARED_ABI_VERSION;
    signal_result.struct_size = (uint32_t)sizeof(signal_result);
    bias_result.abi_version = RTKLIB_SHARED_ABI_VERSION;
    bias_result.struct_size = (uint32_t)sizeof(bias_result);
    ion_result.abi_version = RTKLIB_SHARED_ABI_VERSION;
    ion_result.struct_size = (uint32_t)sizeof(ion_result);

    if (rtklib_shared_abi_version() != (int)RTKLIB_SHARED_ABI_VERSION)
        return 1;
    store = rtklib_shared_nav_create();
    if (!store) return 2;

    /* Exercise every public symbol with ABI-safe arguments.  Invalid input
     * returns are deliberately ignored here: this is a downstream compile
     * and link consumer, while semantic statuses belong to the functional
     * test using the real fixture. */
    (void)rtklib_shared_nav_load_rinex(store, NULL, NULL, NULL);
    (void)rtklib_shared_nav_insert_eph(store, &eph, &record_id);
    (void)rtklib_shared_nav_insert_glo_eph(store, &glo, &record_id);
    (void)rtklib_shared_nav_insert_ion(store, &ion, &record_id);
    (void)rtklib_shared_nav_record_count(store, 0, 0);
    (void)rtklib_shared_nav_record_at(store, 0, &identity);
    (void)rtklib_shared_nav_record(store, record_id, &identity);
    (void)rtklib_shared_state_query(store, &state_query, &state_result);
    (void)rtklib_shared_signal_query(RTKLIB_SHARED_SYS_GPS, 1, "1C",
                                     RTKLIB_SHARED_GLO_FCN_UNKNOWN, store,
                                     &signal_result);
    (void)rtklib_shared_bias_query(store, &state_query, &bias_result);
    (void)rtklib_shared_ion_query(store, RTKLIB_SHARED_SYS_GPS,
                                  RTKLIB_SHARED_NAV_LNAV, time, 0,
                                  &ion_result);
    (void)rtklib_shared_llh_to_ecef(llh, ecef);
    (void)rtklib_shared_ecef_to_llh(ecef, llh);
    (void)rtklib_shared_geometric_range(satellite, ecef, &range, los);
    (void)rtklib_shared_azel(llh, los, azel);
    (void)rtklib_shared_iono(store, time, RTKLIB_SHARED_SYS_GPS,
                             RTKLIB_SHARED_NAV_LNAV, 1, llh, azel,
                             RTKLIB_SHARED_IONO_OFF, 0, &delay, &variance,
                             &identity);
    (void)rtklib_shared_tropo(store, time, llh, azel,
                              RTKLIB_SHARED_TROPO_OFF, &delay, &variance);
    (void)rtklib_shared_satellite_number(RTKLIB_SHARED_SYS_GPS, 1,
                                         &satellite_number);
    (void)rtklib_shared_satellite_id(RTKLIB_SHARED_SYS_GPS, 1, id);
    (void)rtklib_shared_satellite_from_id(id, &system, &prn,
                                          &satellite_number);
    rtklib_shared_nav_destroy(store);
    rtklib_shared_nav_destroy(NULL);

    /* Keep the objects live in the consumer so this remains a meaningful
     * public layout compile check under aggressive optimization. */
    result |= (int)(eph.struct_size == 0 || identity.struct_size == 0);
    result |= (int)(sizeof(id) != 4 || id[3] != '\0');
    return result;
}
