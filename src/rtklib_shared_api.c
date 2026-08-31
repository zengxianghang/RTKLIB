#include "rtklib_shared_api.h"

#include "rtklib.h"
#include "rtklib_obs_ext.h"
#include "rtklib_signal_bias_ext.h"

#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SHARED_SQR(x) ((x) * (x))
#define SHARED_ERR_ION 5.0
#define SHARED_ERR_SAAS 0.3
#define SHARED_ERR_BRDCI 0.5
#define SHARED_REL_HUMI 0.7
#define SHARED_NAV_FREE_ALL (0x01 | 0x02 | 0x04 | 0x08 | 0x10 | \
                             0x20 | 0x40 | 0x80 | 0x100 | 0x200)

typedef struct {
    rtklib_shared_record_identity_t identity;
    uint32_t kind;
    int32_t index;
} shared_record_t;

struct rtklib_shared_nav_store {
    nav_t nav;
    shared_record_t *records;
    size_t nrecords;
    size_t record_capacity;
    ion_t *ion_records;
    size_t nion_records;
    size_t ion_capacity;
    rtklib_shared_record_id_t next_record_id;
    uint64_t next_rinex_order;
};

static int finite_value(double value)
{
    return isfinite(value) != 0;
}

static int valid_header(uint32_t abi_version, uint32_t struct_size,
                        size_t expected_size)
{
    return abi_version == RTKLIB_SHARED_ABI_VERSION &&
           (size_t)struct_size >= expected_size;
}

static int copy_text(char *destination, size_t capacity, const char *source,
                     int required)
{
    size_t length;

    if (!destination || capacity == 0) return 0;
    destination[0] = '\0';
    if (!source) return required ? 0 : 1;
    length = strlen(source);
    if (length >= capacity) return 0;
    memcpy(destination, source, length + 1);
    return !required || length != 0;
}

static int copy_fixed_text(char *destination, size_t capacity,
                           const char *source)
{
    size_t i;

    if (!destination || capacity == 0 || !source) return 0;
    for (i = 0; i < capacity; ++i) {
        destination[i] = source[i];
        if (source[i] == '\0') return 1;
    }
    destination[capacity - 1] = '\0';
    return 0;
}

static int valid_fixed_text(const char *source, size_t capacity)
{
    size_t i;

    if (!source || capacity == 0) return 0;
    for (i = 0; i < capacity; ++i) {
        if (source[i] == '\0') return 1;
    }
    return 0;
}

static int valid_shared_time(rtklib_shared_time_t time)
{
    return time.week >= 0 && time.week <= RTKLIB_SHARED_MAX_WEEK &&
           finite_value(time.sow) &&
           time.sow >= 0.0 && time.sow < 604800.0;
}

/* Keep the same one-week rollover rule used by the RINEX decoder.  The
 * decoder's helper is private to rinex.c, so the shared adapter must keep a
 * local copy instead of comparing the native and normalized week numbers
 * literally. */
static gtime_t shared_adjweek(gtime_t time, gtime_t reference)
{
    double difference = timediff(time, reference);
    if (difference < -302400.0) return timeadd(time, 604800.0);
    if (difference > 302400.0) return timeadd(time, -604800.0);
    return time;
}

static gtime_t to_gtime(rtklib_shared_time_t time)
{
    return gpst2time((int)time.week, time.sow);
}

static rtklib_shared_time_t from_gtime(gtime_t time)
{
    rtklib_shared_time_t result;
    int week = 0;

    result.sow = time2gpst(time, &week);
    result.week = (int32_t)week;
    return result;
}

static int public_system_to_internal(uint32_t system)
{
    switch (system) {
    case RTKLIB_SHARED_SYS_GPS: return SYS_GPS;
    case RTKLIB_SHARED_SYS_SBS: return SYS_SBS;
    case RTKLIB_SHARED_SYS_GLO: return SYS_GLO;
    case RTKLIB_SHARED_SYS_GAL: return SYS_GAL;
    case RTKLIB_SHARED_SYS_QZS: return SYS_QZS;
    case RTKLIB_SHARED_SYS_BDS: return SYS_CMP;
    default: return SYS_NONE;
    }
}

static uint32_t internal_system_to_public(int system)
{
    switch (system) {
    case SYS_GPS: return RTKLIB_SHARED_SYS_GPS;
    case SYS_SBS: return RTKLIB_SHARED_SYS_SBS;
    case SYS_GLO: return RTKLIB_SHARED_SYS_GLO;
    case SYS_GAL: return RTKLIB_SHARED_SYS_GAL;
    case SYS_QZS: return RTKLIB_SHARED_SYS_QZS;
    case SYS_CMP: return RTKLIB_SHARED_SYS_BDS;
    default: return 0;
    }
}

static int valid_family(uint32_t family)
{
    const uint32_t all = RTKLIB_SHARED_NAV_LNAV |
        RTKLIB_SHARED_NAV_FDMA | RTKLIB_SHARED_NAV_FNAV |
        RTKLIB_SHARED_NAV_INAV | RTKLIB_SHARED_NAV_D1 |
        RTKLIB_SHARED_NAV_D2 | RTKLIB_SHARED_NAV_SBAS |
        RTKLIB_SHARED_NAV_CNAV | RTKLIB_SHARED_NAV_CNV1 |
        RTKLIB_SHARED_NAV_CNV2 | RTKLIB_SHARED_NAV_CNV3 |
        RTKLIB_SHARED_NAV_D1D2 | RTKLIB_SHARED_NAV_IFNV |
        RTKLIB_SHARED_NAV_CNVX | RTKLIB_SHARED_NAV_L1NV |
        RTKLIB_SHARED_NAV_L1OC | RTKLIB_SHARED_NAV_L3OC |
        RTKLIB_SHARED_NAV_LXOC;

    return family != 0 && (family & ~all) == 0 &&
           (family & (family - 1u)) == 0;
}

static int valid_family_mask(uint32_t family_mask)
{
    const uint32_t all = RTKLIB_SHARED_NAV_LNAV |
        RTKLIB_SHARED_NAV_FDMA | RTKLIB_SHARED_NAV_FNAV |
        RTKLIB_SHARED_NAV_INAV | RTKLIB_SHARED_NAV_D1 |
        RTKLIB_SHARED_NAV_D2 | RTKLIB_SHARED_NAV_SBAS |
        RTKLIB_SHARED_NAV_CNAV | RTKLIB_SHARED_NAV_CNV1 |
        RTKLIB_SHARED_NAV_CNV2 | RTKLIB_SHARED_NAV_CNV3 |
        RTKLIB_SHARED_NAV_D1D2 | RTKLIB_SHARED_NAV_IFNV |
        RTKLIB_SHARED_NAV_CNVX | RTKLIB_SHARED_NAV_L1NV |
        RTKLIB_SHARED_NAV_L1OC | RTKLIB_SHARED_NAV_L3OC |
        RTKLIB_SHARED_NAV_LXOC;

    return family_mask != 0 && (family_mask & ~all) == 0;
}

static int valid_family_for_system(int system, uint32_t family)
{
    if (!valid_family(family)) return 0;
    switch (system) {
    case SYS_GPS:
    case SYS_QZS:
        return family == NAV_LNAV || family == NAV_CNAV ||
               family == NAV_CNV2;
    case SYS_GAL:
        return family == NAV_INAV || family == NAV_FNAV;
    case SYS_CMP:
        return family == NAV_D1 || family == NAV_D2 || family == NAV_D1D2 ||
               family == NAV_CNV1 || family == NAV_CNV2 || family == NAV_CNV3;
    case SYS_GLO:
        return family == NAV_FDMA || family == NAV_L3OC;
    case SYS_SBS:
        return family == NAV_SBAS;
    default:
        return 0;
    }
}

/* ION records use the RINEX navigation-message family namespace too, but
 * their family set is broader than the ephemeris set.  Keep this validation
 * separate: accepting an ION CNVX/IFNV record must not make a CNVX/IFNV eph
 * look like a state-propagation record.  Model evaluation below makes the
 * narrower, explicit choice of families understood by ionmodel(). */
static int valid_ion_family_for_system(int system, uint32_t family)
{
    if (!valid_family(family)) return 0;
    switch (system) {
    case SYS_GPS:
    case SYS_QZS:
        return family == NAV_LNAV || family == NAV_CNVX ||
               family == NAV_L1NV;
    case SYS_GAL:
        return family == NAV_IFNV || family == NAV_L1NV;
    case SYS_CMP:
        return family == NAV_D1 || family == NAV_D2 ||
               family == NAV_D1D2 || family == NAV_CNVX ||
               family == NAV_L1NV;
    case SYS_SBS:
        return family == NAV_SBAS;
    default:
        return 0;
    }
}

static int family_for_eph(const eph_t *eph, int system)
{
    int family;

    if (!eph) return 0;
    family = eph->hdr.msg_type;
    if (family) return family;
    if (system == SYS_GPS || system == SYS_QZS) return NAV_LNAV;
    if (system == SYS_CMP) return NAV_D1D2;
    if (system == SYS_GAL) {
        if (eph->code & (1 << 9)) return NAV_INAV;
        if (eph->code & (1 << 8)) return NAV_FNAV;
        if (eph->code & ((1 << 0) | (1 << 2))) return NAV_INAV;
        if (eph->code & (1 << 1)) return NAV_FNAV;
    }
    return 0;
}

static int family_for_geph(const geph_t *geph)
{
    if (!geph) return 0;
    return geph->hdr.msg_type ? geph->hdr.msg_type : NAV_FDMA;
}

static int valid_satellite(uint32_t system, uint32_t prn, int *satellite)
{
    int internal_system;
    int sat;

    internal_system = public_system_to_internal(system);
    if (!internal_system || prn > (uint32_t)INT_MAX) return 0;
    sat = satno(internal_system, (int)prn);
    if (sat <= 0 || sat > MAXSAT) return 0;
    if (satellite) *satellite = sat;
    return 1;
}

static int finite_array(const double *values, size_t count)
{
    size_t i;
    if (!values) return 0;
    for (i = 0; i < count; ++i) {
        if (!finite_value(values[i])) return 0;
    }
    return 1;
}

static int valid_source(const char *source)
{
    size_t i;
    if (!source || source[0] == '\0') return 0;
    for (i = 0; i < RTKLIB_SHARED_SOURCE_ID_MAX; ++i) {
        if (source[i] == '\0') return 1;
    }
    return 0;
}

/* readrnx(), readrnxt() and uncompress() use MAXSTRPATH-sized buffers.  The
 * public loader therefore accepts only a nonempty, NUL-terminated path that
 * fits in that contract, and validates it before dereferencing path[0]. */
static int valid_rinex_path(const char *path)
{
    size_t i;

    if (!path) return 0;
    for (i = 0; i < MAXSTRPATH; ++i) {
        if (path[i] == '\0') return i != 0;
    }
    return 0;
}

static void rollback_rinex_load(rtklib_shared_nav_store_t *store,
                                int old_eph, int old_geph, int old_ion,
                                int old_ns, int old_neop, int old_nsto,
                                size_t old_records, size_t old_ion_records,
                                rtklib_shared_record_id_t old_next_record_id,
                                uint64_t old_rinex_order)
{
    int lost_existing_storage;
    int cleanup_mask = 0;

    if (!store) return;
    lost_existing_storage =
        (old_eph > 0 && !store->nav.eph) ||
        (old_geph > 0 && !store->nav.geph) ||
        (old_ion > 0 && !store->nav.ion) ||
        (old_ns > 0 && !store->nav.seph) ||
        (old_neop > 0 && !store->nav.eop) ||
        (old_nsto > 0 && !store->nav.sto);
    /* No public records can refer to an array whose pre-load count was zero.
     * Release arrays created by this failed attempt immediately, including
     * private EOP/STO allocations; preexisting nonempty arrays remain owned by
     * the store and are released by destroy. */
    if (old_eph == 0) cleanup_mask |= 0x01;
    if (old_geph == 0) cleanup_mask |= 0x02;
    if (old_ion == 0) cleanup_mask |= 0x80;
    if (old_ns == 0) cleanup_mask |= 0x04;
    if (old_neop == 0) cleanup_mask |= 0x100;
    if (old_nsto == 0) cleanup_mask |= 0x200;
    if (cleanup_mask) freenav(&store->nav, cleanup_mask);
    if (lost_existing_storage) {
        /* A failed RTKLIB realloc may already have freed an old array.  Do
         * not leave catalogue entries pointing into that storage.  The
         * arrays that remain allocated are still owned by nav_t and are
         * released by the full destroy mask. */
        store->nav.n = store->nav.ng = store->nav.nion = 0;
        store->nav.ns = store->nav.neop = store->nav.nsto = 0;
        store->nrecords = 0;
        store->nion_records = 0;
        /* Keep caller-visible high-water marks so an old record id cannot be
         * reused after this safety reset (ABA stale-id ambiguity). */
        store->next_record_id = old_next_record_id;
        store->next_rinex_order = old_rinex_order;
        return;
    }
    /* An RTKLIB reserve failure can free the affected array and clear its
     * count.  Never restore a nonzero count against a NULL array. */
    store->nav.n = store->nav.eph ? old_eph : 0;
    store->nav.ng = store->nav.geph ? old_geph : 0;
    store->nav.nion = store->nav.ion ? old_ion : 0;
    store->nav.ns = store->nav.seph ? old_ns : 0;
    store->nav.neop = store->nav.eop ? old_neop : 0;
    store->nav.nsto = store->nav.sto ? old_nsto : 0;
    store->nrecords = old_records;
    store->nion_records = old_ion_records;
    store->next_record_id = old_next_record_id;
    store->next_rinex_order = old_rinex_order;
}

static int reserve_records(rtklib_shared_nav_store_t *store, size_t needed)
{
    shared_record_t *records;
    size_t capacity;

    if (needed <= store->record_capacity) return 1;
    capacity = store->record_capacity ? store->record_capacity : 16;
    while (capacity < needed) {
        if (capacity > SIZE_MAX / 2) return 0;
        capacity *= 2;
    }
    records = (shared_record_t *)realloc(store->records,
                                         capacity * sizeof(*records));
    if (!records) return 0;
    store->records = records;
    store->record_capacity = capacity;
    return 1;
}

static int reserve_ions(rtklib_shared_nav_store_t *store, size_t needed)
{
    ion_t *records;
    size_t capacity;

    if (needed <= store->ion_capacity) return 1;
    capacity = store->ion_capacity ? store->ion_capacity : 8;
    while (capacity < needed) {
        if (capacity > SIZE_MAX / 2) return 0;
        capacity *= 2;
    }
    records = (ion_t *)realloc(store->ion_records,
                               capacity * sizeof(*records));
    if (!records) return 0;
    store->ion_records = records;
    store->ion_capacity = capacity;
    return 1;
}

static int reserve_nav_eph(nav_t *nav, size_t needed)
{
    eph_t *records;
    size_t capacity;

    if (needed <= (size_t)nav->nmax) return 1;
    capacity = nav->nmax > 0 ? (size_t)nav->nmax : 16;
    while (capacity < needed) {
        if (capacity > (size_t)INT_MAX / 2) return 0;
        capacity *= 2;
    }
    records = (eph_t *)realloc(nav->eph, capacity * sizeof(*records));
    if (!records) return 0;
    nav->eph = records;
    nav->nmax = (int)capacity;
    return 1;
}

static int reserve_nav_geph(nav_t *nav, size_t needed)
{
    geph_t *records;
    size_t capacity;

    if (needed <= (size_t)nav->ngmax) return 1;
    capacity = nav->ngmax > 0 ? (size_t)nav->ngmax : 16;
    while (capacity < needed) {
        if (capacity > (size_t)INT_MAX / 2) return 0;
        capacity *= 2;
    }
    records = (geph_t *)realloc(nav->geph, capacity * sizeof(*records));
    if (!records) return 0;
    nav->geph = records;
    nav->ngmax = (int)capacity;
    return 1;
}

static void init_identity(rtklib_shared_record_identity_t *identity)
{
    memset(identity, 0, sizeof(*identity));
    identity->abi_version = RTKLIB_SHARED_ABI_VERSION;
    identity->struct_size = (uint32_t)sizeof(*identity);
    identity->iode = -1;
    identity->iodc = -1;
    identity->health_raw = -1;
    identity->glonass_fcn = RTKLIB_SHARED_GLO_FCN_UNKNOWN;
}

static rtklib_shared_record_id_t next_id(rtklib_shared_nav_store_t *store)
{
    rtklib_shared_record_id_t id = store->next_record_id;
    if (id == 0) id = 1;
    store->next_record_id = id == UINT64_MAX ? 1 : id + 1;
    return id;
}

static int append_meta(rtklib_shared_nav_store_t *store, uint32_t kind,
                       int32_t index, uint32_t source_kind,
                       uint32_t system, uint32_t prn, uint32_t family,
                       int32_t iode, int32_t iodc, int32_t health_raw,
                       int32_t glonass_fcn, uint64_t receive_order,
                       rtklib_shared_time_t toe, rtklib_shared_time_t toc,
                       rtklib_shared_time_t transmit_time,
                       const char *source_id, const char *subtype,
                       rtklib_shared_record_id_t *record_id)
{
    shared_record_t *record;

    if (!store || !source_id || !valid_source(source_id) ||
        !reserve_records(store, store->nrecords + 1)) return 0;
    record = &store->records[store->nrecords];
    init_identity(&record->identity);
    record->identity.record_id = next_id(store);
    record->identity.record_kind = kind;
    record->identity.source_kind = source_kind;
    record->identity.system = internal_system_to_public((int)system);
    record->identity.prn = prn;
    record->identity.family = family;
    record->identity.iode = iode;
    record->identity.iodc = iodc;
    record->identity.health_raw = health_raw;
    record->identity.glonass_fcn = glonass_fcn;
    record->identity.receive_order = receive_order;
    record->identity.toe = toe;
    record->identity.toc = toc;
    record->identity.transmit_time = transmit_time;
    if (!copy_text(record->identity.source_id,
                   sizeof(record->identity.source_id), source_id, 1) ||
        !copy_fixed_text(record->identity.family_subtype,
                         sizeof(record->identity.family_subtype),
                         subtype ? subtype : "")) return 0;
    record->kind = kind;
    record->index = index;
    store->nrecords++;
    if (record_id) *record_id = record->identity.record_id;
    return 1;
}

static int append_ion_payload(rtklib_shared_nav_store_t *store,
                              const ion_t *ion, uint32_t source_kind,
                              uint64_t receive_order, const char *source_id,
                              rtklib_shared_record_id_t *record_id)
{
    size_t index;
    int system;
    uint32_t family;
    int prn;
    rtklib_shared_time_t trans;

    if (!store || !ion || !source_id || !valid_source(source_id) ||
        !reserve_ions(store, store->nion_records + 1)) return 0;
    index = store->nion_records;
    store->ion_records[index] = *ion;
    store->nion_records++;
    system = ion->hdr.sys;
    family = ion->hdr.msg_type ? (uint32_t)ion->hdr.msg_type : NAV_LNAV;
    prn = ion->hdr.prn > 0 ? ion->hdr.prn : 0;
    trans = from_gtime(ion->trans_time);
    if (!append_meta(store, RTKLIB_SHARED_RECORD_ION, (int32_t)index,
                     source_kind, (uint32_t)system, (uint32_t)prn, family,
                     -1, -1, -1, INT32_MIN, receive_order,
                     trans, trans, trans, source_id, ion->hdr.subtype,
                     record_id)) {
        store->nion_records--;
        return 0;
    }
    return 1;
}

static int append_eph_meta(rtklib_shared_nav_store_t *store, int index,
                           uint32_t source_kind, uint64_t receive_order,
                           const char *source_id,
                           rtklib_shared_record_id_t *record_id)
{
    const eph_t *eph = &store->nav.eph[index];
    int system = satsys(eph->sat, NULL);
    uint32_t family = (uint32_t)family_for_eph(eph, system);
    int prn = 0;

    (void)satsys(eph->sat, &prn);
    if (!system || !family || !valid_source(source_id)) return 0;
    return append_meta(store, RTKLIB_SHARED_RECORD_EPH, index, source_kind,
                       (uint32_t)system, (uint32_t)prn, family, eph->iode,
                       eph->iodc, eph->svh, INT32_MIN, receive_order,
                       from_gtime(eph->toe), from_gtime(eph->toc),
                       from_gtime(eph->ttr), source_id, eph->hdr.subtype,
                       record_id);
}

static int append_geph_meta(rtklib_shared_nav_store_t *store, int index,
                            uint32_t source_kind, uint64_t receive_order,
                            const char *source_id,
                            rtklib_shared_record_id_t *record_id)
{
    const geph_t *geph = &store->nav.geph[index];
    int system = satsys(geph->sat, NULL);
    int prn = 0;
    uint32_t family = (uint32_t)family_for_geph(geph);

    (void)satsys(geph->sat, &prn);
    if (system != SYS_GLO || !family || !valid_source(source_id)) return 0;
    return append_meta(store, RTKLIB_SHARED_RECORD_GLO_EPH, index,
                       source_kind, (uint32_t)system, (uint32_t)prn, family,
                       geph->iode, -1, geph->svh, geph->frq, receive_order,
                       from_gtime(geph->toe), from_gtime(geph->toe),
                       from_gtime(geph->tof), source_id, geph->hdr.subtype,
                       record_id);
}

static int identity_for_record(const shared_record_t *record,
                               rtklib_shared_record_identity_t *identity)
{
    if (!record || !identity) return 0;
    *identity = record->identity;
    return 1;
}

static const shared_record_t *find_record_const(
    const rtklib_shared_nav_store_t *store,
    rtklib_shared_record_id_t record_id)
{
    size_t i;
    if (!store || record_id == 0) return NULL;
    for (i = 0; i < store->nrecords; ++i) {
        if (store->records[i].identity.record_id == record_id)
            return &store->records[i];
    }
    return NULL;
}

int rtklib_shared_abi_version(void)
{
    return (int)RTKLIB_SHARED_ABI_VERSION;
}

rtklib_shared_nav_store_t *rtklib_shared_nav_create(void)
{
    rtklib_shared_nav_store_t *store;

    store = (rtklib_shared_nav_store_t *)calloc(1, sizeof(*store));
    if (!store) return NULL;
    store->next_record_id = 1;
    store->next_rinex_order = 1;
    return store;
}

void rtklib_shared_nav_destroy(rtklib_shared_nav_store_t *store)
{
    if (!store) return;
    /* freenav owns every dynamic member reachable from nav_t, including the
     * RINEX4 EOP/STO arrays even though this ABI does not expose those kinds.
     */
    freenav(&store->nav, SHARED_NAV_FREE_ALL);
    free(store->records);
    free(store->ion_records);
    free(store);
}

static int append_loaded_header_ion(rtklib_shared_nav_store_t *store,
                                   int system, const double *values,
                                   size_t count, uint32_t family,
                                   const char *source_id,
                                   uint64_t receive_order)
{
    ion_t ion;

    if (!values || count == 0 || count > 32 ||
        !finite_array(values, count)) return 0;
    memset(&ion, 0, sizeof(ion));
    ion.hdr.data_type = NAV_ION;
    ion.hdr.sys = system;
    ion.hdr.msg_type = (int)family;
    ion.trans_time = gpst2time(0, 0.0);
    ion.ndata = (int)count;
    memcpy(ion.data, values, count * sizeof(values[0]));
    memset(ion.present, 1, count * sizeof(ion.present[0]));
    memcpy(ion.alpha, values, (count < 9 ? count : 9) * sizeof(values[0]));
    return append_ion_payload(store, &ion, RTKLIB_SHARED_SOURCE_RINEX,
                              receive_order, source_id, NULL);
}

static int append_loaded_metadata(rtklib_shared_nav_store_t *store,
                                  int old_eph, int old_geph, int old_ion,
                                  const char *source_id)
{
    int i;
    uint64_t order = store->next_rinex_order;
    int added_ion_for_system[6] = {0, 0, 0, 0, 0, 0};

    if (!store || !source_id || !valid_source(source_id)) return 0;
    for (i = old_eph; i < store->nav.n; ++i) {
        if (!append_eph_meta(store, i, RTKLIB_SHARED_SOURCE_RINEX,
                             order++, source_id, NULL)) return 0;
    }
    for (i = old_geph; i < store->nav.ng; ++i) {
        if (!append_geph_meta(store, i, RTKLIB_SHARED_SOURCE_RINEX,
                              order++, source_id, NULL)) return 0;
    }
    for (i = old_ion; i < store->nav.nion; ++i) {
        int system = store->nav.ion[i].hdr.sys;
        if (!append_ion_payload(store, &store->nav.ion[i],
                                RTKLIB_SHARED_SOURCE_RINEX, order++,
                                source_id, NULL)) return 0;
        if (system == SYS_GPS) added_ion_for_system[0] = 1;
        else if (system == SYS_GAL) added_ion_for_system[1] = 1;
        else if (system == SYS_QZS) added_ion_for_system[2] = 1;
        else if (system == SYS_CMP) added_ion_for_system[3] = 1;
    }

    /* Legacy RINEX headers store the broadcast alpha/beta arrays directly in
     * nav_t rather than as ion_t records. Preserve those values in the same
     * opaque record catalogue so an ION query can still report provenance. */
    if (!added_ion_for_system[0] && norm(store->nav.ion_gps, 8) > 0.0 &&
        !append_loaded_header_ion(store, SYS_GPS, store->nav.ion_gps, 8,
                                  NAV_LNAV, source_id, order++)) return 0;
    if (!added_ion_for_system[1] && norm(store->nav.ion_gal, 4) > 0.0 &&
        !append_loaded_header_ion(store, SYS_GAL, store->nav.ion_gal, 4,
                                  NAV_INAV, source_id, order++)) return 0;
    if (!added_ion_for_system[2] && norm(store->nav.ion_qzs, 8) > 0.0 &&
        !append_loaded_header_ion(store, SYS_QZS, store->nav.ion_qzs, 8,
                                  NAV_LNAV, source_id, order++)) return 0;
    if (!added_ion_for_system[3] && norm(store->nav.ion_cmp, 8) > 0.0 &&
        !append_loaded_header_ion(store, SYS_CMP, store->nav.ion_cmp, 8,
                                  NAV_D1D2, source_id, order++)) return 0;
    store->next_rinex_order = order;
    return 1;
}

int rtklib_shared_nav_load_rinex(rtklib_shared_nav_store_t *store,
                                 const char *path, const char *options,
                                 const char *source_id)
{
    int old_eph, old_geph, old_ion, old_ns, old_neop, old_nsto;
    size_t old_records, old_ion_records;
    rtklib_shared_record_id_t old_next_record_id;
    uint64_t old_rinex_order;
    int stat;
    const char *source;

    if (!store || !valid_rinex_path(path))
        return RTKLIB_SHARED_INVALID_ARGUMENT;
    source = source_id && source_id[0] ? source_id : path;
    if (!valid_source(source)) return RTKLIB_SHARED_INVALID_ARGUMENT;
    old_eph = store->nav.n;
    old_geph = store->nav.ng;
    old_ion = store->nav.nion;
    old_ns = store->nav.ns;
    old_neop = store->nav.neop;
    old_nsto = store->nav.nsto;
    old_records = store->nrecords;
    old_ion_records = store->nion_records;
    old_next_record_id = store->next_record_id;
    old_rinex_order = store->next_rinex_order;
    stat = readrnx(path, 0, options ? options : "", NULL,
                   &store->nav, NULL);
    if (stat <= 0) {
        /* readrnx may have allocated records before a later file/parse
         * failure.  Do not publish those records; any arrays remain owned by
         * nav_t and are released by freenav() at destruction. */
        rollback_rinex_load(store, old_eph, old_geph, old_ion, old_ns,
                            old_neop, old_nsto, old_records, old_ion_records,
                            old_next_record_id, old_rinex_order);
        return RTKLIB_SHARED_IO_ERROR;
    }
    if (!append_loaded_metadata(store, old_eph, old_geph, old_ion, source)) {
        rollback_rinex_load(store, old_eph, old_geph, old_ion, old_ns,
                            old_neop, old_nsto, old_records, old_ion_records,
                            old_next_record_id, old_rinex_order);
        return RTKLIB_SHARED_ALLOCATION_ERROR;
    }
    return RTKLIB_SHARED_OK;
}

static int valid_common_eph_input(const rtklib_shared_eph_input_t *input,
                                  int *system, int *satellite)
{
    gtime_t native_toe, native_transmit;
    if (!input || !valid_header(input->abi_version, input->struct_size,
                               sizeof(*input)) ||
        !valid_shared_time(input->toe) || !valid_shared_time(input->toc) ||
        !valid_shared_time(input->transmit_time) ||
        !valid_source(input->source_id) ||
        !valid_fixed_text(input->family_subtype,
                          sizeof(input->family_subtype)) ||
        input->receive_order == 0 ||
        !valid_satellite(input->system, input->prn, satellite)) return 0;
    *system = public_system_to_internal(input->system);
    if (!valid_family_for_system(*system, input->family)) return 0;
    if (input->broadcast_week < 0 ||
        input->broadcast_week > RTKLIB_SHARED_MAX_WEEK ||
        !finite_value(input->broadcast_toe_sow) ||
        input->broadcast_toe_sow < 0.0 || input->broadcast_toe_sow >= 604800.0 ||
        !finite_value(input->broadcast_transmit_sow) ||
        input->broadcast_transmit_sow < 0.0 ||
        input->broadcast_transmit_sow >= 604800.0) return 0;
    native_toe = *system == SYS_CMP ?
        bdt2gpst(bdt2time(input->broadcast_week,
                          input->broadcast_toe_sow)) :
        gpst2time(input->broadcast_week, input->broadcast_toe_sow);
    native_transmit = *system == SYS_CMP ?
        bdt2gpst(bdt2time(input->broadcast_week,
                          input->broadcast_transmit_sow)) :
        gpst2time(input->broadcast_week, input->broadcast_transmit_sow);
    /* The normalized GPST identities are authoritative for the private
     * gtime_t fields.  Native week/SOW values are retained for broadcast
     * semantics and cross-checked after RTKLIB's one-week rollover
     * normalization against each corresponding GPST instant. */
    native_toe = shared_adjweek(native_toe, to_gtime(input->toe));
    native_transmit = shared_adjweek(native_transmit,
                                     to_gtime(input->transmit_time));
    if (fabs(timediff(native_toe, to_gtime(input->toe))) > 1E-6 ||
        fabs(timediff(native_transmit, to_gtime(input->transmit_time))) >
            1E-6) return 0;
    /* RTKLIB uses negative SVA for an unknown/out-of-range accuracy value;
     * preserve that decoded sentinel instead of rejecting a valid record. */
    if (!finite_value(input->sva_m) ||
        !finite_value(input->semi_major_axis_m) ||
        input->semi_major_axis_m <= 0.0 ||
        !finite_value(input->eccentricity) || input->eccentricity < 0.0 ||
        input->eccentricity >= 1.0) return 0;
    if (!finite_value(input->inclination_rad) ||
        !finite_value(input->raan_rad) ||
        !finite_value(input->arg_perigee_rad) ||
        !finite_value(input->mean_anomaly_rad) ||
        !finite_value(input->delta_n_rad_s) ||
        !finite_value(input->raan_rate_rad_s) ||
        !finite_value(input->inclination_rate_rad_s) ||
        !finite_value(input->crc_m) || !finite_value(input->crs_m) ||
        !finite_value(input->cuc_rad) || !finite_value(input->cus_rad) ||
        !finite_value(input->cic_rad) || !finite_value(input->cis_rad))
        return 0;
    if (!finite_value(input->fit_interval_h) ||
        !finite_value(input->clock_bias_s) ||
        !finite_value(input->clock_drift_sps) ||
        !finite_value(input->clock_drift_rate_sps2) ||
        !finite_value(input->additional_rate_m_s) ||
        !finite_value(input->additional_mean_motion_rate_rad_s2) ||
        !finite_value(input->ura_index) ||
        !finite_array(input->tgd_s, 4) || !finite_array(input->isc_s, 6) ||
        !finite_value(input->delta_n0_raw) || !finite_value(input->top_raw) ||
        !finite_value(input->delta_n0_dot_raw) ||
        !finite_array(input->urai_ned_raw, 3) ||
        !finite_value(input->urai_ed_raw) || !finite_value(input->wn_op_raw) ||
        !finite_array(input->sisai_raw, 4) || !finite_value(input->int_flag_raw))
        return 0;
    return 1;
}

static void fill_eph_from_input(eph_t *eph,
                                const rtklib_shared_eph_input_t *input,
                                int system, int satellite)
{
    memset(eph, 0, sizeof(*eph));
    eph->sat = satellite;
    eph->iode = input->iode;
    eph->iodc = input->iodc;
    eph->sva = input->sva_m;
    eph->svh = input->health_raw;
    eph->week = input->broadcast_week;
    eph->code = input->code;
    eph->flag = input->flag;
    /* Keep the source-native week/SOW fields above, but use the already
     * normalized GPST identities as the authoritative query times. */
    eph->toe = to_gtime(input->toe);
    eph->toc = to_gtime(input->toc);
    eph->ttr = to_gtime(input->transmit_time);
    eph->toes = input->broadcast_toe_sow;
    eph->A = input->semi_major_axis_m;
    eph->e = input->eccentricity;
    eph->i0 = input->inclination_rad;
    eph->OMG0 = input->raan_rad;
    eph->omg = input->arg_perigee_rad;
    eph->M0 = input->mean_anomaly_rad;
    eph->deln = input->delta_n_rad_s;
    eph->OMGd = input->raan_rate_rad_s;
    eph->idot = input->inclination_rate_rad_s;
    eph->crc = input->crc_m;
    eph->crs = input->crs_m;
    eph->cuc = input->cuc_rad;
    eph->cus = input->cus_rad;
    eph->cic = input->cic_rad;
    eph->cis = input->cis_rad;
    eph->fit = input->fit_interval_h;
    eph->f0 = input->clock_bias_s;
    eph->f1 = input->clock_drift_sps;
    eph->f2 = input->clock_drift_rate_sps2;
    memcpy(eph->tgd, input->tgd_s, sizeof(eph->tgd));
    memcpy(eph->isc, input->isc_s, sizeof(eph->isc));
    eph->Adot = input->additional_rate_m_s;
    eph->ndot = input->additional_mean_motion_rate_rad_s2;
    eph->delta_n0 = input->delta_n0_raw;
    eph->top = input->top_raw;
    eph->delta_n0_dot = input->delta_n0_dot_raw;
    memcpy(eph->urai_ned, input->urai_ned_raw, sizeof(eph->urai_ned));
    eph->urai_ed = input->urai_ed_raw;
    eph->wn_op = input->wn_op_raw;
    memcpy(eph->sisai, input->sisai_raw, sizeof(eph->sisai));
    eph->int_flag = input->int_flag_raw;
    eph->hdr.data_type = NAV_EPH;
    eph->hdr.sys = system;
    eph->hdr.prn = (int)input->prn;
    eph->hdr.msg_type = (int)input->family;
    copy_fixed_text(eph->hdr.subtype, sizeof(eph->hdr.subtype),
                    input->family_subtype);
}

int rtklib_shared_nav_insert_eph(rtklib_shared_nav_store_t *store,
                                 const rtklib_shared_eph_input_t *input,
                                 rtklib_shared_record_id_t *record_id)
{
    eph_t eph;
    int system, satellite;
    int index;

    if (!store || !valid_common_eph_input(input, &system, &satellite))
        return RTKLIB_SHARED_INVALID_ARGUMENT;
    if (!reserve_records(store, store->nrecords + 1) ||
        !reserve_nav_eph(&store->nav, (size_t)store->nav.n + 1))
        return RTKLIB_SHARED_ALLOCATION_ERROR;
    fill_eph_from_input(&eph, input, system, satellite);
    index = store->nav.n++;
    store->nav.eph[index] = eph;
    if (!append_eph_meta(store, index, RTKLIB_SHARED_SOURCE_RECEIVER,
                         input->receive_order, input->source_id,
                         record_id)) {
        store->nav.n--;
        return RTKLIB_SHARED_ALLOCATION_ERROR;
    }
    return RTKLIB_SHARED_OK;
}

static int valid_glo_input(const rtklib_shared_glo_eph_input_t *input,
                           int *satellite)
{
    if (!input || !valid_header(input->abi_version, input->struct_size,
                               sizeof(*input)) ||
        input->system != RTKLIB_SHARED_SYS_GLO ||
        !valid_satellite(input->system, input->prn, satellite) ||
        !valid_shared_time(input->toe) ||
        !valid_shared_time(input->transmit_time) ||
        !valid_source(input->source_id) ||
        !valid_fixed_text(input->family_subtype,
                          sizeof(input->family_subtype)) ||
        input->receive_order == 0 ||
        !valid_family_for_system(SYS_GLO, input->family) ||
        input->glonass_fcn < -7 || input->glonass_fcn > 13 ||
        input->sva < 0 || input->age < 0 ||
        !finite_array(input->position_ecef_m, 3) ||
        !finite_array(input->velocity_ecef_mps, 3) ||
        !finite_array(input->acceleration_ecef_mps2, 3) ||
        !finite_value(input->clock_bias_s) ||
        !finite_value(input->relative_frequency_bias) ||
        !finite_value(input->beta) || !finite_value(input->dtaun_s) ||
        !finite_value(input->tgd_l2ocp_s) ||
        !finite_value(input->isc_l3ocp_s) ||
        !finite_array(input->antenna_phase_center_offset_m, 3) ||
        !finite_value(input->raw_transmit_sow) ||
        input->raw_transmit_sow < 0.0 || input->raw_transmit_sow >= 604800.0)
        return 0;
    return 1;
}

int rtklib_shared_nav_insert_glo_eph(rtklib_shared_nav_store_t *store,
                                     const rtklib_shared_glo_eph_input_t *input,
                                     rtklib_shared_record_id_t *record_id)
{
    geph_t geph;
    int satellite;
    int index;

    if (!store || !valid_glo_input(input, &satellite))
        return RTKLIB_SHARED_INVALID_ARGUMENT;
    if (!reserve_records(store, store->nrecords + 1) ||
        !reserve_nav_geph(&store->nav, (size_t)store->nav.ng + 1))
        return RTKLIB_SHARED_ALLOCATION_ERROR;
    memset(&geph, 0, sizeof(geph));
    geph.sat = satellite;
    geph.iode = input->iode;
    geph.frq = input->glonass_fcn;
    geph.svh = input->health_raw;
    geph.sva = input->sva;
    geph.age = input->age;
    geph.flag = input->flags;
    geph.svhflag = input->health_flags;
    geph.data_validity = input->data_validity;
    geph.toe = to_gtime(input->toe);
    geph.tof = to_gtime(input->transmit_time);
    memcpy(geph.pos, input->position_ecef_m, sizeof(geph.pos));
    memcpy(geph.vel, input->velocity_ecef_mps, sizeof(geph.vel));
    memcpy(geph.acc, input->acceleration_ecef_mps2, sizeof(geph.acc));
    geph.taun = -input->clock_bias_s;
    geph.gamn = input->relative_frequency_bias;
    geph.beta = input->beta;
    geph.dtaun = input->dtaun_s;
    geph.tgd_l2ocp = input->tgd_l2ocp_s;
    geph.isc_l3ocp = input->isc_l3ocp_s;
    memcpy(geph.pc, input->antenna_phase_center_offset_m, sizeof(geph.pc));
    geph.ttm = input->raw_transmit_sow;
    geph.hdr.data_type = NAV_EPH;
    geph.hdr.sys = SYS_GLO;
    geph.hdr.prn = (int)input->prn;
    geph.hdr.msg_type = (int)input->family;
    copy_fixed_text(geph.hdr.subtype, sizeof(geph.hdr.subtype),
                    input->family_subtype);
    index = store->nav.ng++;
    store->nav.geph[index] = geph;
    if (!append_geph_meta(store, index, RTKLIB_SHARED_SOURCE_RECEIVER,
                          input->receive_order, input->source_id,
                          record_id)) {
        store->nav.ng--;
        return RTKLIB_SHARED_ALLOCATION_ERROR;
    }
    return RTKLIB_SHARED_OK;
}

int rtklib_shared_nav_insert_ion(rtklib_shared_nav_store_t *store,
                                 const rtklib_shared_ion_input_t *input,
                                 rtklib_shared_record_id_t *record_id)
{
    ion_t ion;
    int system;
    size_t i;

    if (!store || !input || !valid_header(input->abi_version,
                                          input->struct_size, sizeof(*input)) ||
        !(system = public_system_to_internal(input->system)) ||
        !valid_ion_family_for_system(system, input->family) ||
        !valid_shared_time(input->transmit_time) ||
        !valid_source(input->source_id) ||
        !valid_fixed_text(input->family_subtype,
                          sizeof(input->family_subtype)) ||
        input->receive_order == 0 ||
        input->value_count == 0 || input->value_count > 32 ||
        !finite_array(input->values, input->value_count))
        return RTKLIB_SHARED_INVALID_ARGUMENT;
    for (i = 0; i < input->value_count; ++i) {
        if (input->present[i] > 1) return RTKLIB_SHARED_INVALID_ARGUMENT;
    }
    if (!reserve_records(store, store->nrecords + 1) ||
        !reserve_ions(store, store->nion_records + 1))
        return RTKLIB_SHARED_ALLOCATION_ERROR;
    memset(&ion, 0, sizeof(ion));
    ion.hdr.data_type = NAV_ION;
    ion.hdr.sys = system;
    ion.hdr.msg_type = (int)input->family;
    ion.hdr.prn = 0;
    copy_fixed_text(ion.hdr.subtype, sizeof(ion.hdr.subtype),
                    input->family_subtype);
    ion.trans_time = to_gtime(input->transmit_time);
    ion.ndata = (int)input->value_count;
    memcpy(ion.data, input->values, sizeof(ion.data));
    memcpy(ion.present, input->present, sizeof(ion.present));
    memcpy(ion.alpha, input->values,
           (input->value_count < 9 ? input->value_count : 9) *
           sizeof(input->values[0]));
    store->ion_records[store->nion_records] = ion;
    store->nion_records++;
    if (!append_meta(store, RTKLIB_SHARED_RECORD_ION,
                     (int32_t)(store->nion_records - 1),
                     RTKLIB_SHARED_SOURCE_RECEIVER, (uint32_t)system, 0,
                     input->family, -1, -1, -1, INT32_MIN,
                     input->receive_order, input->transmit_time,
                     input->transmit_time, input->transmit_time,
                     input->source_id, input->family_subtype, record_id)) {
        store->nion_records--;
        return RTKLIB_SHARED_ALLOCATION_ERROR;
    }
    return RTKLIB_SHARED_OK;
}

size_t rtklib_shared_nav_record_count(const rtklib_shared_nav_store_t *store,
                                      uint32_t record_kind, uint32_t system)
{
    size_t i, count = 0;
    if (!store) return 0;
    for (i = 0; i < store->nrecords; ++i) {
        if (record_kind && store->records[i].identity.record_kind != record_kind)
            continue;
        if (system && store->records[i].identity.system != system) continue;
        ++count;
    }
    return count;
}

int rtklib_shared_nav_record_at(const rtklib_shared_nav_store_t *store,
                                size_t index,
                                rtklib_shared_record_identity_t *identity)
{
    if (!store || !identity ||
        !valid_header(identity->abi_version, identity->struct_size,
                      sizeof(*identity)) || index >= store->nrecords)
        return RTKLIB_SHARED_INVALID_ARGUMENT;
    return identity_for_record(&store->records[index], identity) ?
        RTKLIB_SHARED_OK : RTKLIB_SHARED_CALL_FAILED;
}

int rtklib_shared_nav_record(const rtklib_shared_nav_store_t *store,
                             rtklib_shared_record_id_t record_id,
                             rtklib_shared_record_identity_t *identity)
{
    const shared_record_t *record = find_record_const(store, record_id);
    if (!record || !identity ||
        !valid_header(identity->abi_version, identity->struct_size,
                      sizeof(*identity))) return RTKLIB_SHARED_INVALID_ARGUMENT;
    return identity_for_record(record, identity) ?
        RTKLIB_SHARED_OK : RTKLIB_SHARED_CALL_FAILED;
}

static void init_state_result(rtklib_shared_state_result_t *result)
{
    int i;
    memset(result, 0, sizeof(*result));
    result->abi_version = RTKLIB_SHARED_ABI_VERSION;
    result->struct_size = (uint32_t)sizeof(*result);
    result->status = RTKLIB_SHARED_QUERY_FAILED;
    result->health = RTKLIB_SHARED_HEALTH_UNKNOWN;
    result->health_raw = -1;
    result->variance_m2 = NAN;
    result->clock_bias_s = NAN;
    result->clock_drift_sps = NAN;
    for (i = 0; i < 3; ++i) {
        result->position_ecef_m[i] = NAN;
        result->velocity_ecef_mps[i] = NAN;
    }
    init_identity(&result->identity);
}

static void init_bias_result(rtklib_shared_bias_result_t *result)
{
    memset(result, 0, sizeof(*result));
    result->abi_version = RTKLIB_SHARED_ABI_VERSION;
    result->struct_size = (uint32_t)sizeof(*result);
    result->status = RTKLIB_SHARED_QUERY_FAILED;
    result->raw_code_bias_m = NAN;
    init_identity(&result->identity);
}

/* GPS/QZSS CNAV accuracy fields are raw URAI components.  They do not carry
 * the legacy metric-SVA contract consumed by eph2pos()'s variance output. */
static int is_modern_gps_qzs_urai_record(const shared_record_t *record)
{
    if (!record || record->kind != RTKLIB_SHARED_RECORD_EPH) return 0;
    if (record->identity.system != RTKLIB_SHARED_SYS_GPS &&
        record->identity.system != RTKLIB_SHARED_SYS_QZS) return 0;
    return record->identity.family == RTKLIB_SHARED_NAV_CNAV ||
           record->identity.family == RTKLIB_SHARED_NAV_CNV2;
}

static void set_state_health(rtklib_shared_state_result_t *result,
                             int system, uint32_t family, unsigned char code)
{
    int signal_health;

    if (!result) return;
    if (result->health_raw < 0) {
        result->health = RTKLIB_SHARED_HEALTH_UNKNOWN;
        return;
    }
    signal_health = rtklib_signal_health_ext(system, (int)family, code,
                                             result->health_raw);
    result->health = signal_health < 0 ? RTKLIB_SHARED_HEALTH_UNKNOWN :
        signal_health == 0 ? RTKLIB_SHARED_HEALTH_HEALTHY :
        RTKLIB_SHARED_HEALTH_UNHEALTHY;
}

static const shared_record_t *select_default_record(
    const rtklib_shared_nav_store_t *store,
    const rtklib_shared_state_query_t *query, int satellite)
{
    int stat, eph_index = -1, geph_index = -1, type = 0;
    int system, prn = 0, required_mask;
    const shared_record_t *record;
    size_t i;

    system = satsys(satellite, &prn);
    required_mask = query->family_mask ? (int)query->family_mask :
        rtklib_signal_family_mask_ext(system, query->rtklib_code);
    if (required_mask == 0) return NULL;
    stat = rtklib_signal_select_record_ext(
        to_gtime(query->selection_time), satellite, query->rtklib_code,
        required_mask,
        system == SYS_GLO ? query->glonass_fcn : INT_MIN, &store->nav,
        &eph_index, &geph_index, &type);
    if (stat <= 0) return NULL;
    (void)prn;
    for (i = 0; i < store->nrecords; ++i) {
        record = &store->records[i];
        if ((system == SYS_GLO && record->kind ==
             RTKLIB_SHARED_RECORD_GLO_EPH && record->index == geph_index) ||
            (system != SYS_GLO && record->kind ==
             RTKLIB_SHARED_RECORD_EPH && record->index == eph_index)) {
            if (record->identity.family == (uint32_t)type) return record;
        }
    }
    return NULL;
}

static int request_is_valid(const rtklib_shared_state_query_t *query,
                            int *satellite)
{
    if (!query || !valid_header(query->abi_version, query->struct_size,
                               sizeof(*query)) ||
        !valid_shared_time(query->evaluation_time) ||
        !valid_shared_time(query->selection_time) || query->rtklib_code == 0 ||
        !valid_satellite(query->system, query->prn, satellite)) return 0;
    if (query->family_mask != 0 && !valid_family_mask(query->family_mask))
        return 0;
    if (query->glonass_fcn != RTKLIB_SHARED_GLO_FCN_UNKNOWN &&
        (query->glonass_fcn < -7 || query->glonass_fcn > 13)) return 0;
    return 1;
}

static int record_matches_request(const shared_record_t *record,
                                  const rtklib_shared_state_query_t *query,
                                  int satellite)
{
    int system;
    if (!record || !query) return 0;
    system = satsys(satellite, NULL);
    if (record->identity.system != query->system ||
        (int)record->identity.prn != (int)query->prn ||
        ((system == SYS_GLO && record->kind != RTKLIB_SHARED_RECORD_GLO_EPH) ||
         (system != SYS_GLO && record->kind != RTKLIB_SHARED_RECORD_EPH)))
        return 0;
    if (system == SYS_GLO && query->glonass_fcn !=
        RTKLIB_SHARED_GLO_FCN_UNKNOWN &&
        record->identity.glonass_fcn != query->glonass_fcn) return 0;
    if (query->family_mask && !(record->identity.family & query->family_mask))
        return 0;
    if (!rtklib_signal_code_supported_ext(system,
                                          (int)record->identity.family,
                                          query->rtklib_code)) return 0;
    return 1;
}

static const shared_record_t *record_for_query(
    const rtklib_shared_nav_store_t *store,
    const rtklib_shared_state_query_t *query, int satellite,
    int *selection_error)
{
    const shared_record_t *record;

    if (selection_error) *selection_error = RTKLIB_SHARED_OK;
    if (query->selected_record_id != 0) {
        record = find_record_const(store, query->selected_record_id);
        if (!record) {
            if (selection_error) *selection_error =
                RTKLIB_SHARED_INVALID_ARGUMENT;
            return NULL;
        }
        if (!record_matches_request(record, query, satellite)) {
            if (selection_error) *selection_error = RTKLIB_SHARED_UNSUPPORTED;
            return record;
        }
        return record;
    }
    record = select_default_record(store, query, satellite);
    if (!record && selection_error)
        *selection_error = RTKLIB_SHARED_UNAVAILABLE;
    return record;
}

static int evaluate_record(const rtklib_shared_nav_store_t *store,
                           const shared_record_t *record,
                           rtklib_shared_time_t evaluation_time,
                           unsigned char code,
                           rtklib_shared_state_result_t *result)
{
    double state[6] = {0}, next_state[6] = {0};
    double dts[2] = {0}, next_dts[2] = {0}, variance = 0.0, next_var = 0.0;
    gtime_t time = to_gtime(evaluation_time);
    gtime_t next_time = timeadd(time, 1E-3);
    int system, stat = 1;

    if (!store || !record || !result) return RTKLIB_SHARED_INVALID_ARGUMENT;
    system = public_system_to_internal(record->identity.system);
    if (record->kind == RTKLIB_SHARED_RECORD_EPH) {
        const eph_t *eph;
        if (record->index < 0 || record->index >= store->nav.n) return 0;
        eph = &store->nav.eph[record->index];
        if (is_modern_gps_qzs_urai_record(record)) {
            /* The state query is deliberately all-or-nothing for this
             * unsupported accuracy model.  Identity and health were bound
             * by rtklib_shared_state_query() before reaching here; leave the
             * initialized NaN state fields untouched and do not reselect. */
            result->health_raw = eph->svh;
            set_state_health(result, system, record->identity.family, code);
            return RTKLIB_SHARED_UNSUPPORTED;
        }
        eph2pos(time, eph, state, dts, &variance);
        eph2pos(next_time, eph, next_state, next_dts, &next_var);
        result->health_raw = eph->svh;
    } else if (record->kind == RTKLIB_SHARED_RECORD_GLO_EPH) {
        const geph_t *geph;
        if (record->index < 0 || record->index >= store->nav.ng) return 0;
        geph = &store->nav.geph[record->index];
        geph2pos(time, geph, state, dts, &variance);
        geph2pos(next_time, geph, next_state, next_dts, &next_var);
        result->health_raw = geph->svh;
    } else return RTKLIB_SHARED_UNSUPPORTED;

    if (!finite_array(state, 3) || !finite_array(next_state, 3) ||
        !finite_value(dts[0]) || !finite_value(next_dts[0]) ||
        !finite_value(variance) || norm(state, 3) < RE_WGS84) return 0;
    result->position_ecef_m[0] = state[0];
    result->position_ecef_m[1] = state[1];
    result->position_ecef_m[2] = state[2];
    result->velocity_ecef_mps[0] = (next_state[0] - state[0]) / 1E-3;
    result->velocity_ecef_mps[1] = (next_state[1] - state[1]) / 1E-3;
    result->velocity_ecef_mps[2] = (next_state[2] - state[2]) / 1E-3;
    result->clock_bias_s = dts[0];
    result->clock_drift_sps = (next_dts[0] - dts[0]) / 1E-3;
    result->variance_m2 = variance;
    result->state_valid = 1;
    set_state_health(result, system, record->identity.family, code);
    return stat;
}

int rtklib_shared_state_query(const rtklib_shared_nav_store_t *store,
                              const rtklib_shared_state_query_t *query,
                              rtklib_shared_state_result_t *result)
{
    const shared_record_t *record;
    int satellite, explicit_id, stat;

    if (!result || !valid_header(result->abi_version, result->struct_size,
                                 sizeof(*result)))
        return RTKLIB_SHARED_INVALID_ARGUMENT;
    init_state_result(result);
    if (!store || !request_is_valid(query, &satellite))
        return RTKLIB_SHARED_INVALID_ARGUMENT;
    explicit_id = query->selected_record_id != 0;
    if (explicit_id) {
        record = find_record_const(store, query->selected_record_id);
        if (!record) return RTKLIB_SHARED_INVALID_ARGUMENT;
        result->identity = record->identity;
        if (!record_matches_request(record, query, satellite)) {
            result->status = RTKLIB_SHARED_QUERY_UNSUPPORTED;
            return RTKLIB_SHARED_UNSUPPORTED;
        }
    } else {
        record = select_default_record(store, query, satellite);
        if (!record) {
            result->status = RTKLIB_SHARED_QUERY_UNAVAILABLE;
            return RTKLIB_SHARED_UNAVAILABLE;
        }
        result->identity = record->identity;
    }
    stat = evaluate_record(store, record, query->evaluation_time,
                           query->rtklib_code, result);
    if (stat == RTKLIB_SHARED_UNSUPPORTED) {
        result->status = RTKLIB_SHARED_QUERY_UNSUPPORTED;
        return RTKLIB_SHARED_UNSUPPORTED;
    }
    if (stat <= 0) {
        result->status = RTKLIB_SHARED_QUERY_FAILED;
        return RTKLIB_SHARED_CALL_FAILED;
    }
    result->status = RTKLIB_SHARED_QUERY_AVAILABLE;
    return RTKLIB_SHARED_OK;
}

int rtklib_shared_bias_query(const rtklib_shared_nav_store_t *store,
                             const rtklib_shared_state_query_t *query,
                             rtklib_shared_bias_result_t *result)
{
    const shared_record_t *record;
    int satellite, selection_error, stat;
    double bias = NAN;

    if (!result || !valid_header(result->abi_version, result->struct_size,
                                 sizeof(*result)))
        return RTKLIB_SHARED_INVALID_ARGUMENT;
    init_bias_result(result);
    if (!store || !request_is_valid(query, &satellite))
        return RTKLIB_SHARED_INVALID_ARGUMENT;
    record = record_for_query(store, query, satellite, &selection_error);
    if (!record) {
        if (selection_error == RTKLIB_SHARED_INVALID_ARGUMENT)
            return RTKLIB_SHARED_INVALID_ARGUMENT;
        result->status = RTKLIB_SHARED_QUERY_UNAVAILABLE;
        return RTKLIB_SHARED_UNAVAILABLE;
    }
    result->identity = record->identity;
    if (selection_error == RTKLIB_SHARED_UNSUPPORTED) {
        result->status = RTKLIB_SHARED_QUERY_UNSUPPORTED;
        return RTKLIB_SHARED_UNSUPPORTED;
    }
    if (record->kind == RTKLIB_SHARED_RECORD_GLO_EPH) {
        if (record->index < 0 || record->index >= store->nav.ng) {
            result->status = RTKLIB_SHARED_QUERY_FAILED;
            return RTKLIB_SHARED_CALL_FAILED;
        }
        stat = rtklib_signal_code_bias_selected_ext(
            SYS_GLO, (int)record->identity.family, query->rtklib_code, NULL,
            &store->nav.geph[record->index], &bias, NULL);
    } else if (record->kind == RTKLIB_SHARED_RECORD_EPH) {
        if (record->index < 0 || record->index >= store->nav.n) {
            result->status = RTKLIB_SHARED_QUERY_FAILED;
            return RTKLIB_SHARED_CALL_FAILED;
        }
        stat = rtklib_signal_code_bias_selected_ext(
            public_system_to_internal(record->identity.system),
            (int)record->identity.family, query->rtklib_code,
            &store->nav.eph[record->index], NULL, &bias, NULL);
    } else {
        result->status = RTKLIB_SHARED_QUERY_UNSUPPORTED;
        return RTKLIB_SHARED_UNSUPPORTED;
    }
    if (stat < 0) {
        result->status = RTKLIB_SHARED_QUERY_FAILED;
        return RTKLIB_SHARED_CALL_FAILED;
    }
    if (stat == 0) {
        result->status = RTKLIB_SHARED_QUERY_UNSUPPORTED;
        return RTKLIB_SHARED_UNSUPPORTED;
    }
    result->raw_code_bias_m = bias;
    result->status = RTKLIB_SHARED_QUERY_AVAILABLE;
    return RTKLIB_SHARED_OK;
}

static uint32_t signal_family_mask(int system, unsigned char code)
{
    return (uint32_t)rtklib_signal_family_mask_ext(system, code);
}

static int signal_frequency_hz(int system, unsigned char code, int fcn,
                               double *frequency, int *frequency_index)
{
    int freq = 0;

    (void)code2obs_ext(code, &freq);
    if (freq <= 0 || freq > 6 || !frequency || !frequency_index) return 0;
    *frequency_index = freq - 1;
    if (system == SYS_GLO) {
        if (fcn == RTKLIB_SHARED_GLO_FCN_UNKNOWN || fcn < -7 || fcn > 13)
            return 0;
        if (code == CODE_L1C || code == CODE_L1P || code == CODE_L2C ||
            code == CODE_L2P) {
            if (freq == 1) *frequency = FREQ1_GLO + DFRQ1_GLO * fcn;
            else if (freq == 2) *frequency = FREQ2_GLO + DFRQ2_GLO * fcn;
            else return 0;
        } else if (code == CODE_L3Q && freq == 3) {
            *frequency = FREQ3_GLO;
        } else return 0;
    } else if (system == SYS_CMP) {
        /* The legacy table numbers BDS signals by RINEX band.  Resolve the
         * physical carrier from the code so B1I/B1C/B2/B3 and newer
         * B2a/B2b do not inherit the GPS frequency table by accident. */
        if (code == CODE_L2I || code == CODE_L2Q || code == CODE_L1I ||
            code == CODE_L1Q) *frequency = FREQ1_CMP;
        else if (code == CODE_L1D || code == CODE_L1P || code == CODE_L1X)
            *frequency = FREQ1;
        else if (code == CODE_L7I || code == CODE_L7Q) *frequency = FREQ2_CMP;
        else if (code == CODE_L6I || code == CODE_L6Q) *frequency = FREQ3_CMP;
        else if (code == CODE_L5P) *frequency = FREQ5;
        else if (code == CODE_L7D) *frequency = FREQ7;
        else return 0;
    } else {
        switch (freq) {
        case 1: *frequency = FREQ1; break;
        case 2: *frequency = FREQ2; break;
        case 3: *frequency = FREQ5; break;
        case 4: *frequency = FREQ6; break;
        case 5: *frequency = FREQ7; break;
        case 6: *frequency = FREQ8; break;
        default: return 0;
        }
    }
    return finite_value(*frequency) && *frequency > 0.0;
}

int rtklib_shared_signal_query(uint32_t system, uint32_t prn,
                               const char *rinex_code, int32_t glonass_fcn,
                               const rtklib_shared_nav_store_t *store,
                               rtklib_shared_signal_result_t *result)
{
    char code_text[4] = {0};
    int internal_system, satellite, frequency_index;
    unsigned char code;
    double frequency;

    if (!result || !valid_header(result->abi_version, result->struct_size,
                                 sizeof(*result)))
        return RTKLIB_SHARED_INVALID_ARGUMENT;
    memset(result, 0, sizeof(*result));
    result->abi_version = RTKLIB_SHARED_ABI_VERSION;
    result->struct_size = (uint32_t)sizeof(*result);
    result->glonass_fcn = glonass_fcn;
    if (!rinex_code || strlen(rinex_code) != 2 ||
        !copy_text(code_text, sizeof(code_text), rinex_code, 1) ||
        !valid_satellite(system, prn, &satellite))
        return RTKLIB_SHARED_INVALID_ARGUMENT;
    internal_system = public_system_to_internal(system);
    code = obs2code_ext(code_text, NULL);
    if (code == CODE_NONE) return RTKLIB_SHARED_UNSUPPORTED;
    if (internal_system == SYS_GLO && glonass_fcn !=
        RTKLIB_SHARED_GLO_FCN_UNKNOWN &&
        (glonass_fcn < -7 || glonass_fcn > 13))
        return RTKLIB_SHARED_INVALID_ARGUMENT;
    if (!signal_frequency_hz(internal_system, code, glonass_fcn,
                             &frequency, &frequency_index))
        return RTKLIB_SHARED_UNAVAILABLE;
    if (internal_system == SYS_GLO && store) {
        size_t i;
        int found = 0;
        for (i = 0; i < store->nrecords; ++i) {
            const shared_record_t *record = &store->records[i];
            if (record->kind == RTKLIB_SHARED_RECORD_GLO_EPH &&
                record->identity.system == system &&
                record->identity.prn == prn &&
                record->identity.glonass_fcn == glonass_fcn) {
                found = 1;
                break;
            }
        }
        if (!found) return RTKLIB_SHARED_UNAVAILABLE;
    }
    result->system = system;
    result->prn = prn;
    result->rtklib_code = code;
    result->frequency_index = frequency_index;
    result->carrier_frequency_hz = frequency;
    result->wavelength_m = CLIGHT / frequency;
    result->family_mask = signal_family_mask(internal_system, code);
    copy_text(result->rinex_code, sizeof(result->rinex_code), code_text, 1);
    return result->family_mask ? RTKLIB_SHARED_OK : RTKLIB_SHARED_UNSUPPORTED;
}

static void init_ion_result(rtklib_shared_ion_result_t *result)
{
    memset(result, 0, sizeof(*result));
    result->abi_version = RTKLIB_SHARED_ABI_VERSION;
    result->struct_size = (uint32_t)sizeof(*result);
    result->status = RTKLIB_SHARED_QUERY_FAILED;
    result->health = RTKLIB_SHARED_HEALTH_UNKNOWN;
    result->health_raw = -1;
    init_identity(&result->identity);
}

/* The only broadcast models evaluated by this API are the eight-parameter
 * Klobuchar records that RTKLIB's ionmodel() accepts.  Model identity comes
 * from the decoded system/message family, never from the number of values:
 * BDS D1/D2 uses BDT at the ICD model boundary while ionmodel() uses GPST, so
 * it remains raw-query-only until a time-scale-correct model adapter exists.
 * BDS CNVX is a nine-parameter BDGIM record and must not be interpreted as
 * Klobuchar merely because its prefix is long enough. */
static int shared_iono_model_supported(
    const rtklib_shared_record_identity_t *identity, int32_t iono_option)
{
    if (!identity) return 0;
    if (iono_option == RTKLIB_SHARED_IONO_QZS)
        return identity->system == RTKLIB_SHARED_SYS_QZS &&
               identity->family == RTKLIB_SHARED_NAV_LNAV;
    if (iono_option != RTKLIB_SHARED_IONO_BRDC) return 0;
    return identity->system == RTKLIB_SHARED_SYS_GPS &&
           identity->family == RTKLIB_SHARED_NAV_LNAV;
}

static const shared_record_t *select_default_ion(
    const rtklib_shared_nav_store_t *store, uint32_t system,
    uint32_t family_mask, rtklib_shared_time_t evaluation_time)
{
    const shared_record_t *best = NULL;
    double best_age = 0.0;
    size_t i;

    for (i = 0; i < store->nrecords; ++i) {
        const shared_record_t *record = &store->records[i];
        double age;
        if (record->kind != RTKLIB_SHARED_RECORD_ION ||
            record->identity.system != system ||
            (family_mask && !(record->identity.family & family_mask))) continue;
        age = fabs(timediff(to_gtime(record->identity.transmit_time),
                            to_gtime(evaluation_time)));
        if (!best || age < best_age ||
            (fabs(age - best_age) < 1E-9 &&
             record->identity.receive_order > best->identity.receive_order)) {
            best = record;
            best_age = age;
        }
    }
    return best;
}

int rtklib_shared_ion_query(const rtklib_shared_nav_store_t *store,
                            uint32_t system, uint32_t family_mask,
                            rtklib_shared_time_t evaluation_time,
                            rtklib_shared_record_id_t selected_record_id,
                            rtklib_shared_ion_result_t *result)
{
    const shared_record_t *record;
    const ion_t *ion;
    int internal_system;
    size_t i;

    if (!result || !valid_header(result->abi_version, result->struct_size,
                                 sizeof(*result)))
        return RTKLIB_SHARED_INVALID_ARGUMENT;
    init_ion_result(result);
    internal_system = public_system_to_internal(system);
    if (!store || !internal_system || !valid_shared_time(evaluation_time) ||
        (family_mask && !valid_family_mask(family_mask)))
        return RTKLIB_SHARED_INVALID_ARGUMENT;
    if (selected_record_id != 0) {
        record = find_record_const(store, selected_record_id);
        if (!record) return RTKLIB_SHARED_INVALID_ARGUMENT;
        result->identity = record->identity;
        if (record->kind != RTKLIB_SHARED_RECORD_ION ||
            record->identity.system != system ||
            (family_mask && !(record->identity.family & family_mask))) {
            result->status = RTKLIB_SHARED_QUERY_UNSUPPORTED;
            return RTKLIB_SHARED_UNSUPPORTED;
        }
    } else {
        record = select_default_ion(store, system, family_mask, evaluation_time);
        if (!record) {
            result->status = RTKLIB_SHARED_QUERY_UNAVAILABLE;
            return RTKLIB_SHARED_UNAVAILABLE;
        }
        result->identity = record->identity;
    }
    if (record->index < 0 || (size_t)record->index >= store->nion_records) {
        result->status = RTKLIB_SHARED_QUERY_FAILED;
        return RTKLIB_SHARED_CALL_FAILED;
    }
    ion = &store->ion_records[record->index];
    result->value_count = ion->ndata > 32 ? 32u : (uint32_t)ion->ndata;
    for (i = 0; i < result->value_count; ++i) {
        result->values[i] = ion->data[i];
        result->present[i] = ion->present[i];
    }
    result->status = RTKLIB_SHARED_QUERY_AVAILABLE;
    return RTKLIB_SHARED_OK;
}

static int valid_vector(const double *values, size_t count)
{
    return finite_array(values, count);
}

int rtklib_shared_llh_to_ecef(const double llh_rad_m[3], double ecef_m[3])
{
    if (!llh_rad_m || !ecef_m || !valid_vector(llh_rad_m, 3))
        return RTKLIB_SHARED_INVALID_ARGUMENT;
    if (llh_rad_m[2] < -1000.0 || llh_rad_m[2] > 100000.0)
        return RTKLIB_SHARED_INVALID_ARGUMENT;
    pos2ecef(llh_rad_m, ecef_m);
    return valid_vector(ecef_m, 3) ? RTKLIB_SHARED_OK : RTKLIB_SHARED_CALL_FAILED;
}

int rtklib_shared_ecef_to_llh(const double ecef_m[3], double llh_rad_m[3])
{
    if (!ecef_m || !llh_rad_m || !valid_vector(ecef_m, 3) ||
        norm(ecef_m, 3) < RE_WGS84 - 100000.0)
        return RTKLIB_SHARED_INVALID_ARGUMENT;
    ecef2pos(ecef_m, llh_rad_m);
    return valid_vector(llh_rad_m, 3) ? RTKLIB_SHARED_OK : RTKLIB_SHARED_CALL_FAILED;
}

int rtklib_shared_geometric_range(const double satellite_ecef_m[3],
                                  const double receiver_ecef_m[3],
                                  double *range_m, double los[3])
{
    double range;
    if (!satellite_ecef_m || !receiver_ecef_m || !range_m || !los ||
        !valid_vector(satellite_ecef_m, 3) ||
        !valid_vector(receiver_ecef_m, 3))
        return RTKLIB_SHARED_INVALID_ARGUMENT;
    range = geodist(satellite_ecef_m, receiver_ecef_m, los);
    if (!finite_value(range) || range <= 0.0 || !valid_vector(los, 3))
        return RTKLIB_SHARED_CALL_FAILED;
    *range_m = range;
    return RTKLIB_SHARED_OK;
}

int rtklib_shared_azel(const double receiver_llh_rad_m[3],
                       const double los[3], double azel_rad[2])
{
    if (!receiver_llh_rad_m || !los || !azel_rad ||
        !valid_vector(receiver_llh_rad_m, 3) || !valid_vector(los, 3) ||
        norm(los, 3) <= 0.0)
        return RTKLIB_SHARED_INVALID_ARGUMENT;
    if (satazel(receiver_llh_rad_m, los, azel_rad) < 0.0 ||
        !valid_vector(azel_rad, 2)) return RTKLIB_SHARED_CALL_FAILED;
    return RTKLIB_SHARED_OK;
}

int rtklib_shared_iono(const rtklib_shared_nav_store_t *store,
                       rtklib_shared_time_t time, uint32_t system,
                       uint32_t family_mask, uint32_t prn,
                       const double receiver_llh_rad_m[3],
                       const double azel_rad[2], int32_t iono_option,
                       rtklib_shared_record_id_t selected_record_id,
                       double *delay_m, double *variance_m2,
                       rtklib_shared_record_identity_t *identity)
{
    rtklib_shared_ion_result_t ion_result;
    int stat, satellite;

    if (identity && !valid_header(identity->abi_version, identity->struct_size,
                                  sizeof(*identity)))
        return RTKLIB_SHARED_INVALID_ARGUMENT;
    if (!delay_m || !variance_m2 || !receiver_llh_rad_m || !azel_rad ||
        !valid_vector(receiver_llh_rad_m, 3) || !valid_vector(azel_rad, 2) ||
        !valid_satellite(system, prn, &satellite) || azel_rad[1] <= 0.0)
        return RTKLIB_SHARED_INVALID_ARGUMENT;
    if (iono_option == RTKLIB_SHARED_IONO_OFF) {
        *delay_m = 0.0;
        *variance_m2 = SHARED_SQR(SHARED_ERR_ION);
        if (identity) init_identity(identity);
        return RTKLIB_SHARED_OK;
    }
    if (iono_option != RTKLIB_SHARED_IONO_BRDC &&
        iono_option != RTKLIB_SHARED_IONO_QZS)
        return RTKLIB_SHARED_UNSUPPORTED;
    /* ion_query validates caller-owned result headers.  This result is an
     * internal temporary, so initialize its header before crossing that
     * public-style boundary. */
    init_ion_result(&ion_result);
    stat = rtklib_shared_ion_query(store, system, family_mask, time,
                                   selected_record_id, &ion_result);
    if (stat != RTKLIB_SHARED_OK) {
        if (identity) *identity = ion_result.identity;
        return stat;
    }
    if (identity) *identity = ion_result.identity;
    if (!shared_iono_model_supported(&ion_result.identity, iono_option))
        return RTKLIB_SHARED_UNSUPPORTED;
    if (ion_result.value_count < 8) return RTKLIB_SHARED_UNSUPPORTED;
    if (ion_result.present[0] == 0 || ion_result.present[1] == 0 ||
        ion_result.present[2] == 0 || ion_result.present[3] == 0 ||
        ion_result.present[4] == 0 || ion_result.present[5] == 0 ||
        ion_result.present[6] == 0 || ion_result.present[7] == 0)
        return RTKLIB_SHARED_UNAVAILABLE;
    *delay_m = ionmodel(to_gtime(time), ion_result.values,
                        receiver_llh_rad_m, azel_rad);
    if (!finite_value(*delay_m)) return RTKLIB_SHARED_CALL_FAILED;
    *variance_m2 = SHARED_SQR(*delay_m * SHARED_ERR_BRDCI);
    return RTKLIB_SHARED_OK;
}

int rtklib_shared_tropo(const rtklib_shared_nav_store_t *store,
                        rtklib_shared_time_t time,
                        const double receiver_llh_rad_m[3],
                        const double azel_rad[2], int32_t tropo_option,
                        double *delay_m, double *variance_m2)
{
    (void)store;
    if (!delay_m || !variance_m2 || !valid_shared_time(time) ||
        !receiver_llh_rad_m || !azel_rad ||
        !valid_vector(receiver_llh_rad_m, 3) || !valid_vector(azel_rad, 2) ||
        receiver_llh_rad_m[2] < -1000.0 || receiver_llh_rad_m[2] > 20000.0 ||
        azel_rad[1] <= 0.0) return RTKLIB_SHARED_INVALID_ARGUMENT;
    if (tropo_option == RTKLIB_SHARED_TROPO_OFF) {
        *delay_m = 0.0;
        *variance_m2 = 0.0;
        return RTKLIB_SHARED_OK;
    }
    if (tropo_option != RTKLIB_SHARED_TROPO_SAAS &&
        tropo_option != RTKLIB_SHARED_TROPO_EST &&
        tropo_option != RTKLIB_SHARED_TROPO_ESTG)
        return RTKLIB_SHARED_UNSUPPORTED;
    *delay_m = tropmodel(to_gtime(time), receiver_llh_rad_m, azel_rad,
                         SHARED_REL_HUMI);
    if (!finite_value(*delay_m)) return RTKLIB_SHARED_CALL_FAILED;
    *variance_m2 = SHARED_SQR(SHARED_ERR_SAAS / (sin(azel_rad[1]) + 0.1));
    return RTKLIB_SHARED_OK;
}

int rtklib_shared_satellite_number(uint32_t system, uint32_t prn,
                                   uint32_t *satellite_number)
{
    int satellite;
    if (!satellite_number || !valid_satellite(system, prn, &satellite))
        return RTKLIB_SHARED_INVALID_ARGUMENT;
    *satellite_number = (uint32_t)satellite;
    return RTKLIB_SHARED_OK;
}

int rtklib_shared_satellite_id(uint32_t system, uint32_t prn, char id[4])
{
    int satellite;
    if (!id || !valid_satellite(system, prn, &satellite))
        return RTKLIB_SHARED_INVALID_ARGUMENT;
    satno2id(satellite, id);
    return id[0] && id[1] && id[2] ? RTKLIB_SHARED_OK :
        RTKLIB_SHARED_CALL_FAILED;
}

int rtklib_shared_satellite_from_id(const char id[4], uint32_t *system,
                                    uint32_t *prn,
                                    uint32_t *satellite_number)
{
    int satellite, internal_system, internal_prn = 0;
    char local_id[4];

    if (!id || !system || !prn || !satellite_number || id[3] != '\0')
        return RTKLIB_SHARED_INVALID_ARGUMENT;
    memcpy(local_id, id, sizeof(local_id));
    satellite = satid2no(local_id);
    if (satellite <= 0 || satellite > MAXSAT)
        return RTKLIB_SHARED_INVALID_ARGUMENT;
    internal_system = satsys(satellite, &internal_prn);
    if (!internal_system || !internal_prn)
        return RTKLIB_SHARED_INVALID_ARGUMENT;
    *system = internal_system_to_public(internal_system);
    *prn = (uint32_t)internal_prn;
    *satellite_number = (uint32_t)satellite;
    return *system ? RTKLIB_SHARED_OK : RTKLIB_SHARED_CALL_FAILED;
}
