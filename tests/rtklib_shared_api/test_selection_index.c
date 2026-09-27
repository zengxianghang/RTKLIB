/*
 * Equivalence of the shared store's selection index with the full scans.
 *
 * The store implementation is compiled into this translation unit, so the
 * static archive's copy of it is never linked and the test can switch the
 * index off.  Every state and bias query of the grid below runs twice -- with
 * the index, and with the legacy full scans -- and must return the same code
 * and byte-identical results.
 *
 * The store mixes the sources the index distinguishes: two RINEX fixtures,
 * the first loaded twice (duplicate entries and several records per
 * satellite), receiver-inserted copies of GPS/QZS/GAL/BDS and GLONASS
 * records, and a failed load that rolls back and rebuilds the index.
 *
 * usage: test_selection_index <rinex nav> <second rinex nav>
 */
#include "../../src/rtklib_shared_api.c"

#include <stdio.h>

static int failures;
static long compared;
static long indexed_multi;

#define CHECK(condition, ...)                                            \
    do {                                                                 \
        if (!(condition)) {                                              \
            if (failures++ < 20) {                                       \
                fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__);     \
                fprintf(stderr, __VA_ARGS__);                            \
                fputc('\n', stderr);                                     \
            }                                                            \
        }                                                                \
    } while (0)

static void fill_eph_input(const eph_t *source,
                           rtklib_shared_eph_input_t *input,
                           uint64_t receive_order)
{
    int system = satsys(source->sat, NULL), prn = 0;

    (void)satsys(source->sat, &prn);
    memset(input, 0, sizeof(*input));
    input->abi_version = RTKLIB_SHARED_ABI_VERSION;
    input->struct_size = (uint32_t)sizeof(*input);
    input->system = internal_system_to_public(system);
    input->prn = (uint32_t)prn;
    input->family = (uint32_t)source->hdr.msg_type;
    input->iode = source->iode;
    input->iodc = source->iodc;
    input->health_raw = source->svh;
    input->code = source->code;
    input->flag = source->flag;
    input->broadcast_week = source->week;
    input->broadcast_toe_sow = source->toes;
    input->broadcast_transmit_sow = system == SYS_CMP ?
        time2bdt(gpst2bdt(source->ttr), NULL) : time2gpst(source->ttr, NULL);
    input->sva_m = source->sva;
    input->toe = from_gtime(source->toe);
    input->toc = from_gtime(source->toc);
    input->transmit_time = from_gtime(source->ttr);
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
    input->receive_order = receive_order;
    strcpy(input->source_id, "receiver-copy");
    memcpy(input->family_subtype, source->hdr.subtype,
           sizeof(input->family_subtype));
    input->family_subtype[sizeof(input->family_subtype) - 1] = '\0';
}

static void fill_glo_input(const geph_t *source,
                           rtklib_shared_glo_eph_input_t *input,
                           uint64_t receive_order)
{
    int prn = 0;

    (void)satsys(source->sat, &prn);
    memset(input, 0, sizeof(*input));
    input->abi_version = RTKLIB_SHARED_ABI_VERSION;
    input->struct_size = (uint32_t)sizeof(*input);
    input->system = RTKLIB_SHARED_SYS_GLO;
    input->prn = (uint32_t)prn;
    input->family = (uint32_t)source->hdr.msg_type;
    input->iode = source->iode;
    input->health_raw = source->svh;
    input->glonass_fcn = source->frq;
    input->sva = source->sva;
    input->age = source->age;
    input->data_validity = source->data_validity;
    input->flags = source->flag;
    input->health_flags = source->svhflag;
    input->toe = from_gtime(source->toe);
    input->transmit_time = from_gtime(source->tof);
    memcpy(input->position_ecef_m, source->pos, sizeof(input->position_ecef_m));
    memcpy(input->velocity_ecef_mps, source->vel,
           sizeof(input->velocity_ecef_mps));
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
    input->receive_order = receive_order;
    strcpy(input->source_id, "receiver-copy");
    memcpy(input->family_subtype, source->hdr.subtype,
           sizeof(input->family_subtype));
    input->family_subtype[sizeof(input->family_subtype) - 1] = '\0';
}

/* Runs one query through both paths and compares the outputs byte for byte;
 * the result objects are zeroed first, so padding compares equal too. */
static void compare_query(rtklib_shared_nav_store_t *store,
                          const rtklib_shared_state_query_t *query)
{
    rtklib_shared_state_result_t indexed, legacy;
    rtklib_shared_bias_result_t indexed_bias, legacy_bias;
    int indexed_code, legacy_code, indexed_bias_code, legacy_bias_code;

    memset(&indexed, 0, sizeof(indexed));
    memset(&legacy, 0, sizeof(legacy));
    memset(&indexed_bias, 0, sizeof(indexed_bias));
    memset(&legacy_bias, 0, sizeof(legacy_bias));
    indexed.abi_version = legacy.abi_version = RTKLIB_SHARED_ABI_VERSION;
    indexed.struct_size = legacy.struct_size = (uint32_t)sizeof(indexed);
    indexed_bias.abi_version = legacy_bias.abi_version =
        RTKLIB_SHARED_ABI_VERSION;
    indexed_bias.struct_size = legacy_bias.struct_size =
        (uint32_t)sizeof(indexed_bias);

    if (!index_ready(store)) {
        CHECK(0, "index is not ready before a comparison");
        return;
    }
    indexed_code = rtklib_shared_state_query(store, query, &indexed);
    indexed_bias_code = rtklib_shared_bias_query(store, query, &indexed_bias);
    store->index.valid = 0;
    legacy_code = rtklib_shared_state_query(store, query, &legacy);
    legacy_bias_code = rtklib_shared_bias_query(store, query, &legacy_bias);
    store->index.valid = 1;

    compared++;
    CHECK(indexed_code == legacy_code &&
              memcmp(&indexed, &legacy, sizeof(indexed)) == 0,
          "state sys=%u prn=%u code=%u mask=0x%x source=%u fcn=%d id=%llu "
          "t=%d/%.3f: indexed rc=%d id=%llu legacy rc=%d id=%llu",
          query->system, query->prn, query->rtklib_code, query->family_mask,
          query->reserved[0], query->glonass_fcn,
          (unsigned long long)query->selected_record_id,
          (int)query->selection_time.week, query->selection_time.sow,
          indexed_code,
          (unsigned long long)indexed.identity.record_id, legacy_code,
          (unsigned long long)legacy.identity.record_id);
    CHECK(indexed_bias_code == legacy_bias_code &&
              memcmp(&indexed_bias, &legacy_bias, sizeof(indexed_bias)) == 0,
          "bias sys=%u prn=%u code=%u mask=0x%x source=%u: indexed rc=%d "
          "legacy rc=%d", query->system, query->prn, query->rtklib_code,
          query->family_mask, query->reserved[0], indexed_bias_code,
          legacy_bias_code);
}

static void init_query(rtklib_shared_state_query_t *query, int satellite,
                       unsigned char code, uint32_t family_mask,
                       uint32_t source_kind, int32_t fcn, gtime_t time)
{
    int prn = 0, system = satsys(satellite, &prn);

    memset(query, 0, sizeof(*query));
    query->abi_version = RTKLIB_SHARED_ABI_VERSION;
    query->struct_size = (uint32_t)sizeof(*query);
    query->system = internal_system_to_public(system);
    query->prn = (uint32_t)prn;
    query->rtklib_code = code;
    query->glonass_fcn = fcn;
    query->family_mask = family_mask;
    query->evaluation_time = from_gtime(time);
    query->selection_time = from_gtime(time);
    query->reserved[0] = source_kind;
}

static int seen(const int *values, int count, int value)
{
    int i;
    for (i = 0; i < count; ++i) if (values[i] == value) return 1;
    return 0;
}

/* The query grid: every satellite in the store plus absent ones, every
 * observation code, no family mask and each family the store holds for the
 * satellite's system (other families are rejected before any scan),
 * each source filter, the GLONASS FCN (unknown and each loaded one), and per
 * satellite a 3-hour time grid plus each of its entries' toe and toe +/- 0.5 s
 * (age ties). */
static int satellite_times(const nav_t *nav, int satellite, double first,
                           double last, gtime_t *times, int capacity)
{
    int i, n = 0;
    double value;
    for (i = 0; i < nav->n + nav->ng && n + 3 <= capacity; ++i) {
        gtime_t toe;
        if (i < nav->n) {
            if (nav->eph[i].sat != satellite) continue;
            toe = nav->eph[i].toe;
        } else {
            if (nav->geph[i - nav->n].sat != satellite) continue;
            toe = nav->geph[i - nav->n].toe;
        }
        times[n++] = toe;
        times[n++] = timeadd(toe, 0.5);
        times[n++] = timeadd(toe, -0.5);
    }
    for (value = first - 6.0 * 3600.0; value <= last + 6.0 * 3600.0 &&
         n < capacity; value += 3.0 * 3600.0) {
        times[n].time = (time_t)floor(value);
        times[n].sec = value - floor(value);
        n++;
    }
    return n;
}

static void compare_grid(rtklib_shared_nav_store_t *store)
{
    const nav_t *nav = &store->nav;
    int satellites[MAXSAT + 1], nsatellites = 0, families[64], nfamilies;
    gtime_t times[1024];
    int ntimes, i, s, c, f, k, t;
    double first = 0.0, last = 0.0;
    size_t r;

    for (i = 0; i < nav->n; ++i) {
        if (!seen(satellites, nsatellites, nav->eph[i].sat))
            satellites[nsatellites++] = nav->eph[i].sat;
    }
    for (i = 0; i < nav->ng; ++i) {
        if (!seen(satellites, nsatellites, nav->geph[i].sat))
            satellites[nsatellites++] = nav->geph[i].sat;
    }
    /* absent satellites of several systems */
    for (i = 1; i <= MAXSAT && nsatellites < MAXSAT; i += 37) {
        if (!seen(satellites, nsatellites, i)) satellites[nsatellites++] = i;
    }
    for (i = 0; i < nav->n + nav->ng; ++i) {
        gtime_t toe = i < nav->n ? nav->eph[i].toe : nav->geph[i - nav->n].toe;
        double value = toe.time + toe.sec;
        if (i == 0 || value < first) first = value;
        if (i == 0 || value > last) last = value;
    }

    for (s = 0; s < nsatellites; ++s) {
        int satellite = satellites[s];
        int system = satsys(satellite, NULL);
        int fcns[16], nfcns = 0;
        ntimes = satellite_times(nav, satellite, first, last, times, 1024);
        nfamilies = 0;
        families[nfamilies++] = 0;
        for (r = 0; r < store->nrecords; ++r) {
            int family = (int)store->records[r].identity.family;
            if (store->records[r].identity.system !=
                internal_system_to_public(system)) continue;
            if (nfamilies < 64 && !seen(families, nfamilies, family))
                families[nfamilies++] = family;
        }
        fcns[nfcns++] = RTKLIB_SHARED_GLO_FCN_UNKNOWN;
        if (system == SYS_GLO) {
            for (i = 0; i < nav->ng && nfcns < 16; ++i) {
                if (nav->geph[i].sat == satellite &&
                    !seen(fcns, nfcns, nav->geph[i].frq))
                    fcns[nfcns++] = nav->geph[i].frq;
            }
        }
        for (c = 1; c <= MAXCODE; ++c) {
            for (f = 0; f < nfamilies; ++f) {
                for (k = 0; k < SHARED_FILTERS; ++k) {
                    int n;
                    for (n = 0; n < nfcns; ++n) {
                        for (t = 0; t < ntimes; ++t) {
                            rtklib_shared_state_query_t query;
                            init_query(&query, satellite, (unsigned char)c,
                                       (uint32_t)families[f], (uint32_t)k,
                                       fcns[n], times[t]);
                            compare_query(store, &query);
                        }
                    }
                }
            }
        }
    }
    ntimes = satellite_times(nav, satellites[0], first, last, times, 1024);

    /* explicit record ids: every record, and ids that do not exist */
    for (r = 0; r < store->nrecords + 3; ++r) {
        rtklib_shared_state_query_t query;
        const shared_record_t *record =
            r < store->nrecords ? &store->records[r] : NULL;
        int satellite = record && record->kind != RTKLIB_SHARED_RECORD_ION &&
            record->index >= 0 ?
            (record->kind == RTKLIB_SHARED_RECORD_GLO_EPH ?
                 nav->geph[record->index].sat : nav->eph[record->index].sat) :
            satellites[0];
        for (c = 1; c <= MAXCODE; c += 7) {
            init_query(&query, satellite, (unsigned char)c, 0, 0,
                       RTKLIB_SHARED_GLO_FCN_UNKNOWN, times[0]);
            query.selected_record_id = record ? record->identity.record_id :
                store->next_record_id + (rtklib_shared_record_id_t)r;
            compare_query(store, &query);
        }
    }
}

static void count_multi_record_entries(const rtklib_shared_nav_store_t *store)
{
    size_t i;
    indexed_multi = 0;
    for (i = 0; i < store->index.eph_records.capacity; ++i)
        if (store->index.eph_records.count[i] > 1) indexed_multi++;
    for (i = 0; i < store->index.geph_records.capacity; ++i)
        if (store->index.geph_records.count[i] > 1) indexed_multi++;
}

int main(int argc, char **argv)
{
    rtklib_shared_nav_store_t *store;
    int i, loaded_eph, loaded_geph;
    uint64_t order = 5000;

    if (argc != 3) {
        fprintf(stderr, "usage: %s <rinex nav> <second rinex nav>\n", argv[0]);
        return 2;
    }
    store = rtklib_shared_nav_create();
    CHECK(store && index_ready(store), "empty store index");
    CHECK(rtklib_shared_nav_load_rinex(store, argv[1], "", "first") ==
              RTKLIB_SHARED_OK, "load %s", argv[1]);
    loaded_eph = store->nav.n;
    loaded_geph = store->nav.ng;
    /* receiver copies of every other loaded record */
    for (i = 0; i < loaded_eph; i += 2) {
        rtklib_shared_eph_input_t input;
        fill_eph_input(&store->nav.eph[i], &input, order++);
        (void)rtklib_shared_nav_insert_eph(store, &input, NULL);
    }
    for (i = 0; i < loaded_geph; i += 2) {
        rtklib_shared_glo_eph_input_t input;
        fill_glo_input(&store->nav.geph[i], &input, order++);
        (void)rtklib_shared_nav_insert_glo_eph(store, &input, NULL);
    }
    CHECK(rtklib_shared_nav_load_rinex(store, argv[2], "", "second") ==
              RTKLIB_SHARED_OK, "load %s", argv[2]);
    CHECK(rtklib_shared_nav_load_rinex(store, argv[1], "", "again") ==
              RTKLIB_SHARED_OK, "reload %s", argv[1]);
    CHECK(rtklib_shared_nav_load_rinex(store, "does-not-exist.rnx", "",
                                       "missing") != RTKLIB_SHARED_OK,
          "a missing file must fail");
    CHECK(index_ready(store), "index after rollback");
    count_multi_record_entries(store);
    compare_grid(store);

    /* the index follows later inserts */
    for (i = 1; i < loaded_eph; i += 2) {
        rtklib_shared_eph_input_t input;
        fill_eph_input(&store->nav.eph[i], &input, order++);
        (void)rtklib_shared_nav_insert_eph(store, &input, NULL);
    }
    CHECK(index_ready(store), "index after inserts");
    compare_grid(store);

    printf("selection index: %s (%ld queries x state+bias, %d eph + %d geph "
           "entries, %zu records, %ld multi-record entries)\n",
           failures ? "FAIL" : "PASS", compared, store->nav.n, store->nav.ng,
           store->nrecords, indexed_multi);
    rtklib_shared_nav_destroy(store);
    return failures ? 1 : 0;
}
