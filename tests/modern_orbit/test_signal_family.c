/*
 * Observation-code family coverage, broadcast code bias and QZSS/GPS signal
 * health through the public shared ABI.
 *
 * Usage: test_signal_family FIXTURE
 *
 * 1. Family masks: every tracking-component variant maps to the navigation
 *    family of its signal (RINEX 4 / ICD tables written out below).
 * 2. Code bias on real BRD400DLR records: variants equal their canonical
 *    code bit-for-bit, canonical values equal the ICD formula evaluated from
 *    the raw record, and combined (X) tracking of components with different
 *    ISCs, E5 AltBOC and E6 report UNSUPPORTED (no broadcast term).
 * 3. QZSS LNAV health (IS-QZSS-PNT-006 4.1.2.3(4)): real J02/J03 records
 *    with raw health 1 (L1C/B bit, L1C/A transmitted) are healthy for L1C/A
 *    and unhealthy for L1C/B; synthetic bit patterns cover every signal.
 */

#include "../../src/rtklib.h"
#include "../../src/rtklib_shared_api.h"
#include "../../src/rtklib_obs_ext.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

extern int rtklib_signal_health_ext(int system, int message_type,
                                    unsigned char code, int raw_svh);

static int failures = 0;

#define CHECK(cond, ...) \
    do { if (!(cond)) { fprintf(stderr, "FAIL: " __VA_ARGS__); \
         fputc('\n', stderr); failures++; } } while (0)

#define L RTKLIB_SHARED_NAV_LNAV
#define C RTKLIB_SHARED_NAV_CNAV
#define C2 RTKLIB_SHARED_NAV_CNV2
#define IN RTKLIB_SHARED_NAV_INAV
#define FN RTKLIB_SHARED_NAV_FNAV
#define D1 RTKLIB_SHARED_NAV_D1
#define D2 RTKLIB_SHARED_NAV_D2
#define D12 RTKLIB_SHARED_NAV_D1D2
#define B1 RTKLIB_SHARED_NAV_CNV1
#define B2 RTKLIB_SHARED_NAV_CNV2
#define B3 RTKLIB_SHARED_NAV_CNV3
#define FD RTKLIB_SHARED_NAV_FDMA
#define L3 RTKLIB_SHARED_NAV_L3OC

typedef struct { uint32_t system; const char *code; uint32_t mask; } family_case_t;

static const family_case_t FAMILY[] = {
    /* GPS */
    {RTKLIB_SHARED_SYS_GPS, "1C", L | C | C2},
    {RTKLIB_SHARED_SYS_GPS, "1P", L}, {RTKLIB_SHARED_SYS_GPS, "1W", L},
    {RTKLIB_SHARED_SYS_GPS, "1Y", L},
    {RTKLIB_SHARED_SYS_GPS, "1S", C2}, {RTKLIB_SHARED_SYS_GPS, "1L", C2},
    {RTKLIB_SHARED_SYS_GPS, "1X", C2},
    {RTKLIB_SHARED_SYS_GPS, "2P", L}, {RTKLIB_SHARED_SYS_GPS, "2W", L},
    {RTKLIB_SHARED_SYS_GPS, "2Y", L}, {RTKLIB_SHARED_SYS_GPS, "2D", L},
    {RTKLIB_SHARED_SYS_GPS, "2S", C | C2}, {RTKLIB_SHARED_SYS_GPS, "2L", C | C2},
    {RTKLIB_SHARED_SYS_GPS, "2X", C | C2},
    {RTKLIB_SHARED_SYS_GPS, "5I", C | C2}, {RTKLIB_SHARED_SYS_GPS, "5Q", C | C2},
    {RTKLIB_SHARED_SYS_GPS, "5X", C | C2},
    /* QZSS: L1C/B shares the L1C/A families; no P(Y) */
    {RTKLIB_SHARED_SYS_QZS, "1C", L | C | C2}, {RTKLIB_SHARED_SYS_QZS, "1E", L | C | C2},
    {RTKLIB_SHARED_SYS_QZS, "1L", C2}, {RTKLIB_SHARED_SYS_QZS, "2X", C | C2},
    {RTKLIB_SHARED_SYS_QZS, "5Q", C | C2}, {RTKLIB_SHARED_SYS_QZS, "2W", 0},
    /* Galileo */
    {RTKLIB_SHARED_SYS_GAL, "1B", IN | FN}, {RTKLIB_SHARED_SYS_GAL, "1C", IN | FN},
    {RTKLIB_SHARED_SYS_GAL, "1X", IN | FN},
    {RTKLIB_SHARED_SYS_GAL, "5I", FN}, {RTKLIB_SHARED_SYS_GAL, "5Q", FN},
    {RTKLIB_SHARED_SYS_GAL, "5X", FN},
    {RTKLIB_SHARED_SYS_GAL, "7I", IN}, {RTKLIB_SHARED_SYS_GAL, "7Q", IN},
    {RTKLIB_SHARED_SYS_GAL, "7X", IN},
    {RTKLIB_SHARED_SYS_GAL, "8Q", IN | FN},
    {RTKLIB_SHARED_SYS_GAL, "8X", IN | FN},
    {RTKLIB_SHARED_SYS_GAL, "6B", IN}, {RTKLIB_SHARED_SYS_GAL, "6C", IN},
    {RTKLIB_SHARED_SYS_GAL, "6X", IN},
    /* BeiDou */
    {RTKLIB_SHARED_SYS_BDS, "2I", D1 | D2 | D12}, {RTKLIB_SHARED_SYS_BDS, "2Q", D1 | D2 | D12},
    {RTKLIB_SHARED_SYS_BDS, "2X", D1 | D2 | D12}, {RTKLIB_SHARED_SYS_BDS, "7I", D1 | D2 | D12},
    {RTKLIB_SHARED_SYS_BDS, "7Q", D1 | D2 | D12}, {RTKLIB_SHARED_SYS_BDS, "7X", D1 | D2 | D12},
    {RTKLIB_SHARED_SYS_BDS, "6I", D1 | D2 | D12}, {RTKLIB_SHARED_SYS_BDS, "6Q", D1 | D2 | D12},
    {RTKLIB_SHARED_SYS_BDS, "6X", D1 | D2 | D12},
    {RTKLIB_SHARED_SYS_BDS, "1D", B1}, {RTKLIB_SHARED_SYS_BDS, "1P", B1 | B2},
    {RTKLIB_SHARED_SYS_BDS, "5P", B1 | B2}, {RTKLIB_SHARED_SYS_BDS, "5X", B1 | B2},
    {RTKLIB_SHARED_SYS_BDS, "7D", B3},
    /* GLONASS FDMA and L3OC */
    {RTKLIB_SHARED_SYS_GLO, "1C", FD}, {RTKLIB_SHARED_SYS_GLO, "1P", FD},
    {RTKLIB_SHARED_SYS_GLO, "2C", FD}, {RTKLIB_SHARED_SYS_GLO, "2P", FD},
    {RTKLIB_SHARED_SYS_GLO, "3I", L3}, {RTKLIB_SHARED_SYS_GLO, "3Q", L3},
    {RTKLIB_SHARED_SYS_GLO, "3X", L3},
};

static int signal_info(uint32_t system, uint32_t prn, const char *code, int32_t fcn,
                  rtklib_shared_signal_result_t *out)
{
    memset(out, 0, sizeof(*out));
    out->abi_version = RTKLIB_SHARED_ABI_VERSION;
    out->struct_size = (uint32_t)sizeof(*out);
    return rtklib_shared_signal_query(system, prn, code, fcn, NULL, out);
}

static void check_family_masks(void)
{
    size_t i;
    int checked = 0;
    for (i = 0; i < sizeof(FAMILY) / sizeof(FAMILY[0]); i++) {
        rtklib_shared_signal_result_t r;
        uint32_t prn = FAMILY[i].system == RTKLIB_SHARED_SYS_QZS ? 194 :
            FAMILY[i].system == RTKLIB_SHARED_SYS_BDS ? 19 : 2;
        int32_t fcn = FAMILY[i].system == RTKLIB_SHARED_SYS_GLO ? 1 :
            RTKLIB_SHARED_GLO_FCN_UNKNOWN;
        int stat = signal_info(FAMILY[i].system, prn, FAMILY[i].code, fcn, &r);
        uint32_t got = stat == RTKLIB_SHARED_OK ? r.family_mask : 0;
        CHECK(got == FAMILY[i].mask, "family sys=0x%x %s: got 0x%x expected 0x%x",
              FAMILY[i].system, FAMILY[i].code, got, FAMILY[i].mask);
        checked++;
    }
    printf("family masks: %d observation codes match the ICD table\n", checked);
}

/* Public bias of one explicit record for one RINEX code. */
static int bias_of(const rtklib_shared_nav_store_t *store,
                   const rtklib_shared_record_identity_t *id, const char *code,
                   int32_t fcn, double *bias_m)
{
    rtklib_shared_signal_result_t sig;
    rtklib_shared_state_query_t q;
    rtklib_shared_bias_result_t r;
    if (signal_info(id->system, id->prn, code, fcn, &sig) != RTKLIB_SHARED_OK)
        return -100;
    memset(&q, 0, sizeof(q));
    q.abi_version = RTKLIB_SHARED_ABI_VERSION;
    q.struct_size = (uint32_t)sizeof(q);
    q.system = id->system;
    q.prn = id->prn;
    q.rtklib_code = sig.rtklib_code;
    q.glonass_fcn = fcn;
    q.evaluation_time = id->toe;
    q.selection_time = id->toe;
    q.selected_record_id = id->record_id;
    memset(&r, 0, sizeof(r));
    r.abi_version = RTKLIB_SHARED_ABI_VERSION;
    r.struct_size = (uint32_t)sizeof(r);
    int stat = rtklib_shared_bias_query(store, &q, &r);
    *bias_m = r.raw_code_bias_m;
    return stat;
}

static int find_id(const rtklib_shared_nav_store_t *store, uint32_t system,
                   uint32_t prn, uint32_t family,
                   rtklib_shared_record_identity_t *out)
{
    size_t i, n = rtklib_shared_nav_record_count(store, 0, 0);
    for (i = 0; i < n; i++) {
        memset(out, 0, sizeof(*out));
        out->abi_version = RTKLIB_SHARED_ABI_VERSION;
        out->struct_size = (uint32_t)sizeof(*out);
        if (rtklib_shared_nav_record_at(store, i, out) == RTKLIB_SHARED_OK &&
            out->system == system && out->prn == prn && out->family == family)
            return 1;
    }
    return 0;
}

static const eph_t *private_eph(const nav_t *nav, int sys, int prn, int type,
                                rtklib_shared_time_t toe)
{
    int i, w;
    for (i = 0; i < nav->n; i++) {
        int p;
        double sow = time2gpst(nav->eph[i].toe, &w);
        if (satsys(nav->eph[i].sat, &p) == sys && p == prn &&
            nav->eph[i].hdr.msg_type == type && w == toe.week &&
            fabs(sow - toe.sow) < 1E-3) return &nav->eph[i];
    }
    return NULL;
}

typedef struct {
    uint32_t system, prn, family;
    int sys, private_prn, type;
    const char *canonical;
    const char *variants[4];
    int formula; /* 0 none, 1 tgd0, 2 gamma12 tgd0, 3 tgd1, 4 gamma15 tgd0,
                    5 gamma17 tgd1, 6 zero, 7 tgd0-isc0, 8 tgd0-isc1,
                    9 tgd0-isc2, 10 tgd0-isc3 */
} bias_case_t;

static double formula_value(const eph_t *e, int formula)
{
    const double f1 = FREQ1, f2 = FREQ2, f5 = FREQ5, f7 = FREQ7;
    switch (formula) {
        case 1: return CLIGHT * e->tgd[0];
        case 2: return CLIGHT * (f1 / f2) * (f1 / f2) * e->tgd[0];
        case 3: return CLIGHT * e->tgd[1];
        case 4: return CLIGHT * (f1 / f5) * (f1 / f5) * e->tgd[0];
        case 5: return CLIGHT * (f1 / f7) * (f1 / f7) * e->tgd[1];
        case 6: return 0.0;
        case 7: return CLIGHT * (e->tgd[0] - e->isc[0]);
        case 8: return CLIGHT * (e->tgd[0] - e->isc[1]);
        case 9: return CLIGHT * (e->tgd[0] - e->isc[2]);
        case 10: return CLIGHT * (e->tgd[0] - e->isc[3]);
        default: return NAN;
    }
}

static const bias_case_t BIAS[] = {
    {RTKLIB_SHARED_SYS_GPS, 1, L, SYS_GPS, 1, NAV_LNAV, "1C", {"1P", "1W", "1Y", NULL}, 1},
    {RTKLIB_SHARED_SYS_GPS, 1, L, SYS_GPS, 1, NAV_LNAV, "2P", {"2W", "2Y", "2D", NULL}, 2},
    {RTKLIB_SHARED_SYS_GPS, 1, C, SYS_GPS, 1, NAV_CNAV, "1C", {NULL}, 7},
    {RTKLIB_SHARED_SYS_GPS, 1, C, SYS_GPS, 1, NAV_CNAV, "2S", {"2L", "2X", NULL}, 8},
    {RTKLIB_SHARED_SYS_GPS, 1, C, SYS_GPS, 1, NAV_CNAV, "5I", {NULL}, 9},
    {RTKLIB_SHARED_SYS_GPS, 1, C, SYS_GPS, 1, NAV_CNAV, "5Q", {NULL}, 10},
    {RTKLIB_SHARED_SYS_QZS, 194, L, SYS_QZS, 194, NAV_LNAV, "1C", {"1E", NULL}, 1},
    {RTKLIB_SHARED_SYS_QZS, 194, C, SYS_QZS, 194, NAV_CNAV, "1C", {"1E", NULL}, 7},
    {RTKLIB_SHARED_SYS_GAL, 2, IN, SYS_GAL, 2, NAV_INAV, "1C", {"1B", "1X", NULL}, 3},
    {RTKLIB_SHARED_SYS_GAL, 2, FN, SYS_GAL, 2, NAV_FNAV, "1C", {"1B", "1X", NULL}, 1},
    {RTKLIB_SHARED_SYS_GAL, 2, FN, SYS_GAL, 2, NAV_FNAV, "5Q", {"5I", "5X", NULL}, 4},
    {RTKLIB_SHARED_SYS_GAL, 2, IN, SYS_GAL, 2, NAV_INAV, "7Q", {"7I", "7X", NULL}, 5},
    {RTKLIB_SHARED_SYS_BDS, 19, D1, SYS_CMP, 19, NAV_D1, "2I", {"2Q", "2X", NULL}, 1},
    {RTKLIB_SHARED_SYS_BDS, 19, D1, SYS_CMP, 19, NAV_D1, "7I", {"7Q", "7X", NULL}, 3},
    {RTKLIB_SHARED_SYS_BDS, 19, D1, SYS_CMP, 19, NAV_D1, "6I", {"6Q", "6X", NULL}, 6},
};

typedef struct { uint32_t system, prn, family; const char *code; } unsupported_case_t;

static const unsupported_case_t NO_BIAS[] = {
    {RTKLIB_SHARED_SYS_GPS, 1, C, "5X"},  /* I5/Q5 ISCs differ */
    {RTKLIB_SHARED_SYS_GAL, 2, IN, "8Q"}, /* E5 AltBOC: no BGD */
    {RTKLIB_SHARED_SYS_GAL, 2, IN, "6C"}, /* E6: no broadcast group delay */
};

static void check_bias(const char *fixture)
{
    nav_t nav = {0};
    rtklib_shared_nav_store_t *store = rtklib_shared_nav_create();
    size_t i;
    int k, checked = 0;

    if (!store || rtklib_shared_nav_load_rinex(store, fixture, "", "fixture:family")
        != RTKLIB_SHARED_OK || readrnx(fixture, 1, "", NULL, &nav, NULL) <= 0) {
        CHECK(0, "cannot load %s", fixture);
        return;
    }
    for (i = 0; i < sizeof(BIAS) / sizeof(BIAS[0]); i++) {
        const bias_case_t *c = &BIAS[i];
        rtklib_shared_record_identity_t id;
        const eph_t *e;
        double canonical = NAN, variant = NAN;
        if (!find_id(store, c->system, c->prn, c->family, &id)) {
            CHECK(0, "no record for bias case %s", c->canonical);
            continue;
        }
        e = private_eph(&nav, c->sys, c->private_prn, c->type, id.toe);
        CHECK(e != NULL, "private record missing for %s", c->canonical);
        CHECK(bias_of(store, &id, c->canonical, RTKLIB_SHARED_GLO_FCN_UNKNOWN,
                      &canonical) == RTKLIB_SHARED_OK &&
                  e && fabs(canonical - formula_value(e, c->formula)) <= 1E-9,
              "bias sys=0x%x %s = %.6f differs from ICD formula %d",
              c->system, c->canonical, canonical, c->formula);
        for (k = 0; k < 4 && c->variants[k]; k++) {
            CHECK(bias_of(store, &id, c->variants[k],
                          RTKLIB_SHARED_GLO_FCN_UNKNOWN, &variant) ==
                      RTKLIB_SHARED_OK && variant == canonical,
                  "bias sys=0x%x variant %s differs from %s", c->system,
                  c->variants[k], c->canonical);
            checked++;
        }
        checked++;
    }
    for (i = 0; i < sizeof(NO_BIAS) / sizeof(NO_BIAS[0]); i++) {
        rtklib_shared_record_identity_t id;
        double value;
        CHECK(find_id(store, NO_BIAS[i].system, NO_BIAS[i].prn,
                      NO_BIAS[i].family, &id) &&
                  bias_of(store, &id, NO_BIAS[i].code,
                          RTKLIB_SHARED_GLO_FCN_UNKNOWN, &value) ==
                      RTKLIB_SHARED_UNSUPPORTED,
              "sys=0x%x %s published a code bias", NO_BIAS[i].system,
              NO_BIAS[i].code);
        checked++;
    }
    {
        /* GLONASS: P codes equal C/A codes; L2 = +c*dtaun. */
        rtklib_shared_record_identity_t id;
        double c1 = NAN, p1 = NAN, c2 = NAN, p2 = NAN;
        CHECK(find_id(store, RTKLIB_SHARED_SYS_GLO, 2, FD, &id) &&
                  bias_of(store, &id, "1C", id.glonass_fcn, &c1) == RTKLIB_SHARED_OK &&
                  bias_of(store, &id, "1P", id.glonass_fcn, &p1) == RTKLIB_SHARED_OK &&
                  bias_of(store, &id, "2C", id.glonass_fcn, &c2) == RTKLIB_SHARED_OK &&
                  bias_of(store, &id, "2P", id.glonass_fcn, &p2) == RTKLIB_SHARED_OK &&
                  c1 == 0.0 && p1 == 0.0 && c2 == p2 && c2 != 0.0,
              "GLONASS P-code bias differs from C/A");
        checked += 4;
    }
    printf("code bias: %d (canonical, variant, unsupported) cases on real "
           "BRD400DLR records\n", checked);
    freenav(&nav, 0x3ff);
    rtklib_shared_nav_destroy(store);
}

static void check_qzss_health(const char *fixture)
{
    nav_t nav = {0};
    int i, real = 0, raw;
    static const struct { unsigned char code; int bit_mask; } SIG[] = {
        {CODE_L1C, 32 | 16}, {CODE_L1E, 32 | 1}, {CODE_L2S, 8}, {CODE_L2X, 8},
        {CODE_L5Q, 4}, {CODE_L5X, 4}, {CODE_L1L, 2}, {CODE_L1X, 2},
    };

    if (readrnx(fixture, 1, "", NULL, &nav, NULL) <= 0) {
        CHECK(0, "cannot read %s", fixture);
        return;
    }
    for (i = 0; i < nav.n; i++) {
        const eph_t *e = &nav.eph[i];
        if (satsys(e->sat, NULL) != SYS_QZS || e->hdr.msg_type != NAV_LNAV)
            continue;
        CHECK(e->svh == 1, "real QZSS LNAV raw health is %d, expected 1", e->svh);
        CHECK(rtklib_signal_health_ext(SYS_QZS, NAV_LNAV, CODE_L1C, e->svh) == 0,
              "real QZSS L1C/A reported unhealthy for raw %d", e->svh);
        CHECK(rtklib_signal_health_ext(SYS_QZS, NAV_LNAV, CODE_L1E, e->svh) == 1,
              "real QZSS L1C/B (not transmitted) reported healthy");
        real++;
    }
    for (raw = 0; raw < 64; raw++) {
        size_t k;
        for (k = 0; k < sizeof(SIG) / sizeof(SIG[0]); k++) {
            int expected = raw & SIG[k].bit_mask ? 1 : 0;
            CHECK(rtklib_signal_health_ext(SYS_QZS, NAV_LNAV, SIG[k].code, raw)
                      == expected,
                  "QZSS LNAV raw %d code %d", raw, SIG[k].code);
        }
    }
    /* GPS LNAV keeps the raw summary semantics. */
    CHECK(rtklib_signal_health_ext(SYS_GPS, NAV_LNAV, CODE_L1C, 1) == 1,
          "GPS LNAV raw health semantics changed");
    /* GPS/QZSS CNAV bits 2/1/0 = L1/L2/L5 for every component variant. */
    for (raw = 0; raw < 8; raw++) {
        int sys;
        for (sys = 0; sys < 2; sys++) {
            int s = sys ? SYS_QZS : SYS_GPS;
            CHECK(rtklib_signal_health_ext(s, NAV_CNAV, CODE_L1C, raw) == !!(raw & 4) &&
                  rtklib_signal_health_ext(s, NAV_CNAV, CODE_L2L, raw) == !!(raw & 2) &&
                  rtklib_signal_health_ext(s, NAV_CNAV, CODE_L5I, raw) == !!(raw & 1) &&
                  rtklib_signal_health_ext(s, NAV_CNV2, CODE_L1X, raw) == !!(raw & 1),
                  "CNAV/CNV2 health raw %d system %d", raw, s);
        }
    }
    printf("signal health: %d real QZSS LNAV records healthy for L1C/A; "
           "QZSS LNAV 64 x 8 and GPS/QZSS CNAV patterns match the ICD bits\n",
           real);
    freenav(&nav, 0x3ff);
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: %s FIXTURE\n", argv[0]);
        return 2;
    }
    check_family_masks();
    check_bias(argv[1]);
    check_qzss_health(argv[1]);
    if (failures) {
        fprintf(stderr, "signal_family: FAIL (%d)\n", failures);
        return 1;
    }
    printf("signal_family: PASS\n");
    return 0;
}
