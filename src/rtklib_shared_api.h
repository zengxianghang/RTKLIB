#ifndef RTKLIB_SHARED_API_H
#define RTKLIB_SHARED_API_H

/*
 * Versioned, downstream-facing C ABI for the GNSS operations owned by this
 * RTKLIB fork.  This header deliberately does not include rtklib.h: no
 * RTKLIB-private type or layout is part of this ABI.
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RTKLIB_SHARED_ABI_MAJOR 1u
#define RTKLIB_SHARED_ABI_MINOR 2u
#define RTKLIB_SHARED_ABI_VERSION \
    ((RTKLIB_SHARED_ABI_MAJOR << 16) | RTKLIB_SHARED_ABI_MINOR)
/* Every ABI 1.x POD keeps the 1.0 layout and size.  A caller may keep
 * declaring 1.0 in abi_version; the library then preserves 1.0 semantics
 * exactly (see rtklib_shared_state_result_t.variance_status). */
#define RTKLIB_SHARED_ABI_VERSION_1_0 ((RTKLIB_SHARED_ABI_MAJOR << 16) | 0u)
#define RTKLIB_SHARED_SOURCE_ID_MAX 128u
#define RTKLIB_SHARED_SUBTYPE_MAX 5u
#define RTKLIB_SHARED_GLO_FCN_UNKNOWN INT32_MIN
/* Keep the shared time conversion in the range supported by RTKLIB's
 * historical int-sized whole-week-second interface. */
#define RTKLIB_SHARED_MAX_WEEK (INT32_MAX / (7 * 86400))

/* Stable RTKLIB observation-code byte values accepted by state/bias queries.
 * The public signal query takes the corresponding two-character RINEX code.
 * CODE_L1D is an extension value because the legacy RTKLIB table stops at
 * MAXCODE=48; it is never used as an index into a legacy fixed-size array. */
#define RTKLIB_SHARED_CODE_RINEX_1C 1u
#define RTKLIB_SHARED_CODE_RINEX_1P 2u
#define RTKLIB_SHARED_CODE_RINEX_1X 12u
#define RTKLIB_SHARED_CODE_RINEX_2I 40u
#define RTKLIB_SHARED_CODE_RINEX_1D 51u

typedef struct rtklib_shared_nav_store rtklib_shared_nav_store_t;
typedef uint64_t rtklib_shared_record_id_t;

/* Return values.  Query results also carry one of the query status values. */
enum {
    RTKLIB_SHARED_OK = 1,
    RTKLIB_SHARED_NO_MATCH = 0,
    RTKLIB_SHARED_INVALID_ARGUMENT = -1,
    RTKLIB_SHARED_ALLOCATION_ERROR = -2,
    RTKLIB_SHARED_IO_ERROR = -3,
    RTKLIB_SHARED_UNAVAILABLE = -4,
    RTKLIB_SHARED_UNSUPPORTED = -5,
    RTKLIB_SHARED_CALL_FAILED = -6
};

enum {
    RTKLIB_SHARED_QUERY_AVAILABLE = 1,
    RTKLIB_SHARED_QUERY_UNAVAILABLE = 0,
    RTKLIB_SHARED_QUERY_UNSUPPORTED = -1,
    RTKLIB_SHARED_QUERY_FAILED = -2
};

enum {
    RTKLIB_SHARED_HEALTH_UNKNOWN = -1,
    RTKLIB_SHARED_HEALTH_HEALTHY = 0,
    RTKLIB_SHARED_HEALTH_UNHEALTHY = 1
};

/* Stable system bit values. */
enum {
    RTKLIB_SHARED_SYS_GPS = 0x01u,
    RTKLIB_SHARED_SYS_SBS = 0x02u,
    RTKLIB_SHARED_SYS_GLO = 0x04u,
    RTKLIB_SHARED_SYS_GAL = 0x08u,
    RTKLIB_SHARED_SYS_QZS = 0x10u,
    RTKLIB_SHARED_SYS_BDS = 0x20u
};

/* Stable message-family bit values.  A mask may contain more than one family. */
enum {
    RTKLIB_SHARED_NAV_LNAV = 0x00000001u,
    RTKLIB_SHARED_NAV_FDMA = 0x00000002u,
    RTKLIB_SHARED_NAV_FNAV = 0x00000004u,
    RTKLIB_SHARED_NAV_INAV = 0x00000008u,
    RTKLIB_SHARED_NAV_D1 = 0x00000010u,
    RTKLIB_SHARED_NAV_D2 = 0x00000020u,
    RTKLIB_SHARED_NAV_SBAS = 0x00000040u,
    RTKLIB_SHARED_NAV_CNAV = 0x00000080u,
    RTKLIB_SHARED_NAV_CNV1 = 0x00000100u,
    RTKLIB_SHARED_NAV_CNV2 = 0x00000200u,
    RTKLIB_SHARED_NAV_CNV3 = 0x00000400u,
    RTKLIB_SHARED_NAV_D1D2 = 0x00000800u,
    RTKLIB_SHARED_NAV_IFNV = 0x00001000u,
    RTKLIB_SHARED_NAV_CNVX = 0x00002000u,
    RTKLIB_SHARED_NAV_L1NV = 0x00004000u,
    RTKLIB_SHARED_NAV_L1OC = 0x00008000u,
    RTKLIB_SHARED_NAV_L3OC = 0x00010000u,
    RTKLIB_SHARED_NAV_LXOC = 0x00020000u
};

enum {
    RTKLIB_SHARED_RECORD_EPH = 1,
    RTKLIB_SHARED_RECORD_GLO_EPH = 2,
    RTKLIB_SHARED_RECORD_ION = 3
};

enum {
    RTKLIB_SHARED_SOURCE_RINEX = 1,
    RTKLIB_SHARED_SOURCE_RECEIVER = 2
};

/* Public model options.  Other RTKLIB processing options are intentionally
 * not part of this ABI and return UNSUPPORTED from the corresponding helper. */
enum {
    RTKLIB_SHARED_IONO_OFF = 0,
    RTKLIB_SHARED_IONO_BRDC = 1,
    RTKLIB_SHARED_IONO_QZS = 6
};

enum {
    RTKLIB_SHARED_TROPO_OFF = 0,
    RTKLIB_SHARED_TROPO_SAAS = 1,
    RTKLIB_SHARED_TROPO_EST = 3,
    RTKLIB_SHARED_TROPO_ESTG = 4
};

/* GPST week and seconds of week.  week is in [0, RTKLIB_SHARED_MAX_WEEK] and
 * sow is finite and in [0, 604800). */
typedef struct {
    int32_t week;
    double sow;
} rtklib_shared_time_t;

typedef struct {
    uint32_t abi_version;
    uint32_t struct_size;
    rtklib_shared_record_id_t record_id;
    uint32_t record_kind;
    uint32_t source_kind;
    uint32_t system;
    uint32_t prn;
    uint32_t family;
    int32_t iode;
    int32_t iodc;
    int32_t health_raw;
    /* -7..13 are valid GLONASS FDMA channels, including 0.  Unknown is
     * RTKLIB_SHARED_GLO_FCN_UNKNOWN; zero is never an unknown sentinel. */
    int32_t glonass_fcn;
    uint64_t receive_order;
    rtklib_shared_time_t toe;
    rtklib_shared_time_t toc;
    rtklib_shared_time_t transmit_time;
    char source_id[RTKLIB_SHARED_SOURCE_ID_MAX];
    /* NUL must occur within this fixed five-byte field on every input. */
    char family_subtype[RTKLIB_SHARED_SUBTYPE_MAX];
    uint8_t reserved[32];
} rtklib_shared_record_identity_t;

/* RINEX/receiver normalized broadcast GPS/Galileo/BeiDou/QZSS record. */
typedef struct {
    uint32_t abi_version;
    uint32_t struct_size;
    uint32_t system;
    uint32_t prn;
    uint32_t family;
    int32_t iode;
    int32_t iodc;
    int32_t health_raw;
    int32_t code;
    int32_t flag;
    /* broadcast_week/toe/transmit are the native decoded system-week fields;
     * toe/toc/transmit_time below are authoritative GPST query/identity
     * times.  Implementations retain both and validate them with RTKLIB
     * week-rollover normalization; native fields are never rebuilt from GPST
     * and used to overwrite these identities. */
    int32_t broadcast_week;
    double broadcast_toe_sow;
    double broadcast_transmit_sow;
    double sva_m;
    rtklib_shared_time_t toe;
    rtklib_shared_time_t toc;
    rtklib_shared_time_t transmit_time;
    double semi_major_axis_m;
    double eccentricity;
    double inclination_rad;
    double raan_rad;
    double arg_perigee_rad;
    double mean_anomaly_rad;
    double delta_n_rad_s;
    double raan_rate_rad_s;
    double inclination_rate_rad_s;
    double crc_m;
    double crs_m;
    double cuc_rad;
    double cus_rad;
    double cic_rad;
    double cis_rad;
    double fit_interval_h;
    double clock_bias_s;       /* broadcast satellite-clock bias (seconds) */
    double clock_drift_sps;
    double clock_drift_rate_sps2;
    double tgd_s[4];
    double isc_s[6];
    double additional_rate_m_s; /* native eph.Adot, m/s */
    double additional_mean_motion_rate_rad_s2; /* native eph.ndot, rad/s^2 */
    /* These are raw modern message fields in the decoder's documented native
     * units.  The adapter does not relabel a source-defined week/index as a
     * physical unit. */
    double delta_n0_raw;
    double top_raw;
    double delta_n0_dot_raw;
    double urai_ned_raw[3];
    double urai_ed_raw;
    double wn_op_raw;
    double sisai_raw[4];
    double int_flag_raw;
    double ura_index;
    uint64_t receive_order;
    char source_id[RTKLIB_SHARED_SOURCE_ID_MAX];
    /* NUL is required within this fixed-width input field. */
    char family_subtype[RTKLIB_SHARED_SUBTYPE_MAX];
    uint8_t reserved[32];
} rtklib_shared_eph_input_t;

/* Normalized GLONASS FDMA/CDMA broadcast record. */
typedef struct {
    uint32_t abi_version;
    uint32_t struct_size;
    uint32_t system;
    uint32_t prn;
    uint32_t family;
    int32_t iode;
    int32_t health_raw;
    int32_t glonass_fcn;
    int32_t sva;
    int32_t age;
    int32_t data_validity;
    int32_t flags;
    int32_t health_flags;
    rtklib_shared_time_t toe;
    rtklib_shared_time_t transmit_time;
    double position_ecef_m[3];
    double velocity_ecef_mps[3];
    double acceleration_ecef_mps2[3];
    double clock_bias_s;
    double relative_frequency_bias;
    double beta;
    double dtaun_s;             /* L1-L2 delay, seconds; bias uses +c*dtaun */
    double tgd_l2ocp_s;
    double isc_l3ocp_s;
    double antenna_phase_center_offset_m[3];
    double raw_transmit_sow;
    uint64_t receive_order;
    char source_id[RTKLIB_SHARED_SOURCE_ID_MAX];
    /* NUL is required within this fixed-width input field. */
    char family_subtype[RTKLIB_SHARED_SUBTYPE_MAX];
    uint8_t reserved[32];
} rtklib_shared_glo_eph_input_t;

/* Normalized RINEX 4 ION record. Values retain the source's field order. */
typedef struct {
    uint32_t abi_version;
    uint32_t struct_size;
    uint32_t system;
    uint32_t family;
    rtklib_shared_time_t transmit_time;
    uint32_t value_count;
    double values[32];
    uint8_t present[32];
    uint64_t receive_order;
    char source_id[RTKLIB_SHARED_SOURCE_ID_MAX];
    /* NUL is required within this fixed-width input field. */
    char family_subtype[RTKLIB_SHARED_SUBTYPE_MAX];
    uint8_t reserved[32];
} rtklib_shared_ion_input_t;

typedef struct {
    uint32_t abi_version;
    uint32_t struct_size;
    uint32_t system;
    uint32_t prn;
    uint8_t rtklib_code;
    uint8_t reserved0[3];
    int32_t glonass_fcn;
    uint32_t family_mask;
    rtklib_shared_time_t evaluation_time;
    rtklib_shared_time_t selection_time;
    rtklib_shared_record_id_t selected_record_id;
    /* Default selection only (selected_record_id == 0): reserved[0] is a
     * source-kind filter.  0 keeps the ABI 1.0 unrestricted selector;
     * RTKLIB_SHARED_SOURCE_RINEX / _RECEIVER restrict candidates before
     * the existing family, age and tie-break rules.  Explicit IDs ignore
     * this filter.  reserved[1..31] remain zero for forward compatibility. */
    uint8_t reserved[32];
} rtklib_shared_state_query_t;

/* Result PODs are caller-owned inputs at entry: initialize abi_version and
 * struct_size before every query.  Implementations validate those fields
 * before writing any part of the result. */
typedef struct {
    uint32_t abi_version;
    uint32_t struct_size;
    int32_t status;
    int32_t health;
    int32_t health_raw;
    uint32_t state_valid;
    double position_ecef_m[3];
    double velocity_ecef_mps[3];
    double clock_bias_s;
    double clock_drift_sps;
    double variance_m2;
    rtklib_shared_record_identity_t identity;
    /* ABI 1.1 (occupies the first four bytes of the 1.0 reserved area).
     * For callers declaring abi_version >= 1.1 it is a query status for the
     * scalar variance_m2 metric, independent of the state status:
     *   AVAILABLE    variance_m2 is the broadcast metric SVA/URA variance;
     *   UNSUPPORTED  the record's accuracy is not a scalar metric SVA
     *                (GPS/QZSS CNAV/CNAV-2 URAI indices, BDS B-CNAV SISAI/
     *                SISMAI indices); variance_m2 is NaN while the position
     *                and clock state may still be valid.  GPS/QZSS CNAV
     *                accuracy is available from rtklib_shared_modern_ura_query;
     *   UNAVAILABLE  no state was evaluated.
     * Callers declaring 1.0 see 1.0 behaviour: GPS/QZSS CNAV/CNAV-2 states
     * stay UNSUPPORTED (Issue #20 Phase A containment), B-CNAV variance is
     * the decoded scalar, and this field is left zero. */
    int32_t variance_status;
    uint8_t reserved[28];
} rtklib_shared_state_result_t;

typedef struct {
    uint32_t abi_version;
    uint32_t struct_size;
    uint32_t system;
    uint32_t prn;
    uint8_t rtklib_code;
    uint8_t reserved0[3];
    int32_t frequency_index;        /* zero-based L1/L2/L5/L6/L7/L8 */
    int32_t glonass_fcn;
    uint32_t family_mask;
    double carrier_frequency_hz;    /* Hz; GLO includes the selected FCN */
    double wavelength_m;            /* c / carrier_frequency_hz, metres */
    char rinex_code[4];
    uint8_t reserved[32];
} rtklib_shared_signal_result_t;

typedef struct {
    uint32_t abi_version;
    uint32_t struct_size;
    int32_t status;
    /* Additive broadcast term: raw pseudorange = common range terms + this
     * value.  A caller removing the term subtracts raw_code_bias_m. */
    double raw_code_bias_m;
    rtklib_shared_record_identity_t identity;
    uint8_t reserved[32];
} rtklib_shared_bias_result_t;

/* ABI 1.1: GPS/QZSS CNAV/CNAV-2 user range accuracy (IS-GPS-200N
 * 30.3.3.1.1.4 and 30.3.3.2.4, IS-QZSS-PNT-006 5.4.3.2):
 *   URA(t, E) = sqrt((URA_ED * sin(E + 90 deg))^2 + URA_NED(t)^2)
 *   URA_NED(t) = URA_NED0 + URA_NED1 * dt [+ URA_NED2 * (dt - 93600)^2 if
 *                dt > 93600 s], dt = t - t_op + 604800 * (WN - WN_op)
 * with URA_ED and URA_NED0 the nominal values X of their indices (2^(1+N/2)
 * for N <= 6, 2^(N-2) for N >= 6, N = 1/3/5 rounded to 2.8/5.7/11.3 m),
 * URA_NED1 = 2^-(14+N1) m/s and URA_NED2 = 2^-(28+N2) m/s^2.  Status is
 * UNAVAILABLE when an index signals no accuracy prediction (15 or -16) or
 * t precedes t_op; UNSUPPORTED for any other family. */
typedef struct {
    uint32_t abi_version;
    uint32_t struct_size;
    int32_t status;
    int32_t ura_ed_index;
    int32_t ura_ned0_index;
    int32_t ura_ned1_index;
    int32_t ura_ned2_index;
    double elapsed_since_top_s;   /* dt above, seconds */
    double nominal_ura_ed_m;      /* X(URA_ED index), metres */
    double adjusted_ura_ed_m;     /* nominal_ura_ed_m * sin(E + 90 deg) */
    double ura_ned_m;             /* URA_NED(t), metres */
    double ura_m;                 /* composite URA(t, E), metres */
    double variance_m2;           /* ura_m^2 */
    rtklib_shared_record_identity_t identity;
    uint8_t reserved[32];
} rtklib_shared_modern_ura_result_t;

typedef struct {
    uint32_t abi_version;
    uint32_t struct_size;
    int32_t status;
    int32_t health;
    int32_t health_raw;
    uint32_t value_count;
    double values[32];
    uint8_t present[32];
    rtklib_shared_record_identity_t identity;
    uint8_t reserved[32];
} rtklib_shared_ion_result_t;

/* The RINEX loader requires a nonempty NUL-terminated path shorter than
 * MAXSTRPATH.  Wildcard expansion rejects any matching directory/name result
 * that would not fit the same bound; it never truncates a path.  source_id
 * may be NULL or empty, in which case the validated path is used as the
 * source identity.  A failed load never publishes newly decoded catalogue
 * records; if a low-level reserve failure has already invalidated preexisting
 * storage, the public catalogue is cleared rather than left dangling.
 *
 * The query can use selected_record_id=0 to apply the caller's declared
 * default policy. A nonzero stale id is an error and never triggers fallback. */
int rtklib_shared_abi_version(void);
rtklib_shared_nav_store_t *rtklib_shared_nav_create(void);
void rtklib_shared_nav_destroy(rtklib_shared_nav_store_t *store);
int rtklib_shared_nav_load_rinex(rtklib_shared_nav_store_t *store,
                                 const char *path, const char *options,
                                 const char *source_id);
int rtklib_shared_nav_insert_eph(rtklib_shared_nav_store_t *store,
                                 const rtklib_shared_eph_input_t *input,
                                 rtklib_shared_record_id_t *record_id);
int rtklib_shared_nav_insert_glo_eph(rtklib_shared_nav_store_t *store,
                                     const rtklib_shared_glo_eph_input_t *input,
                                     rtklib_shared_record_id_t *record_id);
int rtklib_shared_nav_insert_ion(rtklib_shared_nav_store_t *store,
                                 const rtklib_shared_ion_input_t *input,
                                 rtklib_shared_record_id_t *record_id);
size_t rtklib_shared_nav_record_count(const rtklib_shared_nav_store_t *store,
                                      uint32_t record_kind, uint32_t system);
int rtklib_shared_nav_record_at(const rtklib_shared_nav_store_t *store,
                                size_t index,
                                rtklib_shared_record_identity_t *identity);
int rtklib_shared_nav_record(const rtklib_shared_nav_store_t *store,
                             rtklib_shared_record_id_t record_id,
                             rtklib_shared_record_identity_t *identity);

int rtklib_shared_state_query(const rtklib_shared_nav_store_t *store,
                              const rtklib_shared_state_query_t *query,
                              rtklib_shared_state_result_t *result);
int rtklib_shared_signal_query(uint32_t system, uint32_t prn,
                               const char *rinex_code, int32_t glonass_fcn,
                               const rtklib_shared_nav_store_t *store,
                               rtklib_shared_signal_result_t *result);
int rtklib_shared_bias_query(const rtklib_shared_nav_store_t *store,
                             const rtklib_shared_state_query_t *query,
                             rtklib_shared_bias_result_t *result);
/* ABI 1.1.  query selects the record exactly as rtklib_shared_state_query
 * (explicit selected_record_id or the default policy); query->evaluation_time
 * is t and elevation_rad is the satellite elevation E in [0, pi/2]. */
int rtklib_shared_modern_ura_query(const rtklib_shared_nav_store_t *store,
                                   const rtklib_shared_state_query_t *query,
                                   double elevation_rad,
                                   rtklib_shared_modern_ura_result_t *result);
int rtklib_shared_ion_query(const rtklib_shared_nav_store_t *store,
                            uint32_t system, uint32_t family_mask,
                            rtklib_shared_time_t evaluation_time,
                            rtklib_shared_record_id_t selected_record_id,
                            rtklib_shared_ion_result_t *result);

int rtklib_shared_llh_to_ecef(const double llh_rad_m[3],
                              double ecef_m[3]);
int rtklib_shared_ecef_to_llh(const double ecef_m[3],
                              double llh_rad_m[3]);
int rtklib_shared_geometric_range(const double satellite_ecef_m[3],
                                  const double receiver_ecef_m[3],
                                  double *range_m, double los[3]);
int rtklib_shared_azel(const double receiver_llh_rad_m[3],
                       const double los[3], double azel_rad[2]);
int rtklib_shared_iono(const rtklib_shared_nav_store_t *store,
                       rtklib_shared_time_t time, uint32_t system,
                       uint32_t family_mask, uint32_t prn,
                       const double receiver_llh_rad_m[3],
                       const double azel_rad[2], int32_t iono_option,
                       rtklib_shared_record_id_t selected_record_id,
                       double *delay_m, double *variance_m2,
                       rtklib_shared_record_identity_t *identity);
int rtklib_shared_tropo(const rtklib_shared_nav_store_t *store,
                        rtklib_shared_time_t time,
                        const double receiver_llh_rad_m[3],
                        const double azel_rad[2], int32_t tropo_option,
                        double *delay_m, double *variance_m2);

/* ABI 1.2 explicit-parameter correction models.  Unlike the store-bound
 * helpers above, the caller supplies every model parameter, so no store
 * record or library-internal default is consulted:
 * - Saastamoinen troposphere with standard atmosphere and the caller's
 *   relative humidity in [0, 1] (RTKLIB tropmodel);
 * - Klobuchar reference-frequency ionosphere delay from the caller's eight
 *   broadcast coefficients alpha0..3, beta0..3 (RTKLIB ionmodel).  All-zero
 *   coefficients return UNAVAILABLE instead of falling back to RTKLIB's
 *   built-in 2004 defaults.
 * Both return OK with a zero delay for a non-positive elevation or an
 * out-of-model receiver height, exactly as the underlying RTKLIB models. */
int rtklib_shared_tropo_saastamoinen(rtklib_shared_time_t time,
                                     const double receiver_llh_rad_m[3],
                                     const double azel_rad[2],
                                     double relative_humidity,
                                     double *delay_m);
int rtklib_shared_klobuchar(rtklib_shared_time_t time,
                            const double coefficients[8],
                            const double receiver_llh_rad_m[3],
                            const double azel_rad[2], double *delay_m);

/* Stable satellite-number and three-character identifier mappings. */
int rtklib_shared_satellite_number(uint32_t system, uint32_t prn,
                                   uint32_t *satellite_number);
int rtklib_shared_satellite_id(uint32_t system, uint32_t prn, char id[4]);
int rtklib_shared_satellite_from_id(const char id[4], uint32_t *system,
                                    uint32_t *prn,
                                    uint32_t *satellite_number);

#ifdef __cplusplus
}
#endif

#endif /* RTKLIB_SHARED_API_H */
