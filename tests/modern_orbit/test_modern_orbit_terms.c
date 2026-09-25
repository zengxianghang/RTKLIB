/*
 * Issue #27: broadcast orbit terms of modern families in eph2pos().
 *
 * Usage:
 *   test_modern_orbit_terms FIXTURE ORACLE_CSV LEGACY_BASELINE_CSV
 *   test_modern_orbit_terms --dump-legacy FIXTURE > LEGACY_BASELINE_CSV
 *
 * The oracle vectors come from tools/oracle_vectors.py, an independent
 * implementation of the interface specifications.  The legacy baseline was
 * dumped by this program from the pre-fix library; legacy families must stay
 * bit-for-bit unchanged except the intended BDS-3 GEO D2 correction
 * (C59-C63), which is instead required to match the oracle.
 */

#include "../../src/rtklib.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ORACLE_POS_TOL_M 1E-3
#define ORACLE_CLK_TOL_S 1E-12
#define CROSS_FAMILY_TOL_M 5.0
#define CROSS_FAMILY_MAX_AGE_S 7200.0
#define GEO_CORRECTION_MIN_M 1E5

#define SQ(x) ((x) * (x))

static const double OFFSETS[] = {-1800.0, 0.0, 1800.0, 3600.0, 5400.0};

static int failures = 0;

static void fail(const char *what, const char *detail)
{
    fprintf(stderr, "FAIL: %s: %s\n", what, detail);
    failures++;
}

/* Local classification (independent of eph_modern_family()). */
static int modern_record(const eph_t *eph)
{
    int sys = satsys(eph->sat, NULL), type = eph->hdr.msg_type;
    if (sys == SYS_GPS || sys == SYS_QZS)
        return type == NAV_CNAV || type == NAV_CNV2;
    if (sys == SYS_CMP)
        return type == NAV_CNV1 || type == NAV_CNV2 || type == NAV_CNV3;
    return 0;
}

static int family_code(const char *name)
{
    static const struct { const char *name; int code; } table[] = {
        {"LNAV", NAV_LNAV}, {"CNAV", NAV_CNAV}, {"CNV2", NAV_CNV2},
        {"D1", NAV_D1}, {"D2", NAV_D2}, {"CNV1", NAV_CNV1},
        {"CNV3", NAV_CNV3}, {"INAV", NAV_INAV}, {"FNAV", NAV_FNAV},
        {"FDMA", NAV_FDMA}
    };
    size_t i;
    for (i = 0; i < sizeof(table) / sizeof(table[0]); i++)
        if (!strcmp(table[i].name, name)) return table[i].code;
    return 0;
}

static const eph_t *find_eph(const nav_t *nav, int sat, int type,
                             gtime_t toe)
{
    int i;
    for (i = 0; i < nav->n; i++) {
        if (nav->eph[i].sat == sat && nav->eph[i].hdr.msg_type == type &&
            fabs(timediff(nav->eph[i].toe, toe)) < 1E-3) return &nav->eph[i];
    }
    return NULL;
}

static void dump_legacy(const nav_t *nav)
{
    char id[8];
    double rs[3], dts, var;
    gtime_t t;
    int i, k, week;

    printf("kind,sat,family,toe_week,toe_sow,t_week,t_sow,x_m,y_m,z_m,"
           "clock_s,variance_m2\n");
    for (i = 0; i < nav->n; i++) {
        const eph_t *eph = &nav->eph[i];
        double toe_sow = time2gpst(eph->toe, &week);
        if (modern_record(eph)) continue;
        satno2id(eph->sat, id);
        for (k = 0; k < 5; k++) {
            int t_week;
            double t_sow;
            t = timeadd(eph->toe, OFFSETS[k]);
            eph2pos(t, eph, rs, &dts, &var);
            t_sow = time2gpst(t, &t_week);
            printf("EPH,%s,%d,%d,%.3f,%d,%.3f,%.17g,%.17g,%.17g,%.17g,%.17g\n",
                   id, eph->hdr.msg_type, week, toe_sow, t_week, t_sow,
                   rs[0], rs[1], rs[2], dts, var);
        }
    }
    for (i = 0; i < nav->ng; i++) {
        const geph_t *geph = &nav->geph[i];
        double toe_sow = time2gpst(geph->toe, &week);
        satno2id(geph->sat, id);
        for (k = 0; k < 5; k++) {
            int t_week;
            double t_sow;
            t = timeadd(geph->toe, OFFSETS[k]);
            geph2pos(t, geph, rs, &dts, &var);
            t_sow = time2gpst(t, &t_week);
            printf("GEPH,%s,%d,%d,%.3f,%d,%.3f,%.17g,%.17g,%.17g,%.17g,%.17g\n",
                   id, NAV_FDMA, week, toe_sow, t_week, t_sow,
                   rs[0], rs[1], rs[2], dts, var);
        }
    }
}

static int is_bds3_geo(int sat)
{
    int prn, sys = satsys(sat, &prn);
    return sys == SYS_CMP && prn >= 59;
}

static int check_oracle(const nav_t *nav, const char *path, int *geo_rows)
{
    FILE *fp = fopen(path, "r");
    char line[512], sat_id[8], family[8], detail[256];
    int rows = 0, modern_rows = 0;

    *geo_rows = 0;
    if (!fp) { fail("oracle", "cannot open"); return 0; }
    if (!fgets(line, sizeof(line), fp)) { fclose(fp); return 0; }
    while (fgets(line, sizeof(line), fp)) {
        int toe_week, t_week, sat, type;
        double toe_sow, t_sow, x[3], clk, rs[3], dts, var;
        const eph_t *eph;
        if (sscanf(line, "%7[^,],%7[^,],%d,%lf,%d,%lf,%lf,%lf,%lf,%lf",
                   sat_id, family, &toe_week, &toe_sow, &t_week, &t_sow,
                   &x[0], &x[1], &x[2], &clk) != 10) {
            fail("oracle", "malformed row");
            continue;
        }
        sat = satid2no(sat_id);
        type = family_code(family);
        eph = find_eph(nav, sat, type, gpst2time(toe_week, toe_sow));
        if (!eph) {
            snprintf(detail, sizeof(detail), "%s %s toe %d/%.3f not loaded",
                     sat_id, family, toe_week, toe_sow);
            fail("oracle", detail);
            continue;
        }
        eph2pos(gpst2time(t_week, t_sow), eph, rs, &dts, &var);
        if (!(fabs(rs[0] - x[0]) <= ORACLE_POS_TOL_M &&
              fabs(rs[1] - x[1]) <= ORACLE_POS_TOL_M &&
              fabs(rs[2] - x[2]) <= ORACLE_POS_TOL_M &&
              fabs(dts - clk) <= ORACLE_CLK_TOL_S)) {
            snprintf(detail, sizeof(detail),
                     "%s %s t=%d/%.1f dpos=(%.4f,%.4f,%.4f) dclk=%.3e",
                     sat_id, family, t_week, t_sow, rs[0] - x[0],
                     rs[1] - x[1], rs[2] - x[2], dts - clk);
            fail("oracle", detail);
        }
        rows++;
        if (modern_record(eph)) modern_rows++;
        if (is_bds3_geo(sat)) (*geo_rows)++;
    }
    fclose(fp);
    printf("oracle: %d vectors (%d modern, %d BDS-3 GEO D2) within "
           "%.0e m / %.0e s\n", rows, modern_rows, *geo_rows,
           ORACLE_POS_TOL_M, ORACLE_CLK_TOL_S);
    return rows > 0 && modern_rows > 0 && *geo_rows > 0;
}

/* Modern and legacy broadcasts of one satellite are independent fits of the
 * same real orbit; with the modern terms applied they must agree. */
static int check_cross_family(const nav_t *nav)
{
    int i, j, k, count[3] = {0}, idx;
    double worst[3] = {0};
    char id[8], detail[160];

    for (i = 0; i < nav->n; i++) {
        const eph_t *m = &nav->eph[i];
        int sys = satsys(m->sat, NULL);
        if (!modern_record(m)) continue;
        idx = sys == SYS_GPS ? 0 : sys == SYS_QZS ? 1 : 2;
        for (k = 0; k < 5; k++) {
            gtime_t t = timeadd(m->toe, OFFSETS[k]);
            for (j = 0; j < nav->n; j++) {
                const eph_t *l = &nav->eph[j];
                double a[3], b[3], da, db, va, vb, d;
                if (l->sat != m->sat || modern_record(l)) continue;
                if (fabs(timediff(t, l->toe)) > CROSS_FAMILY_MAX_AGE_S)
                    continue;
                eph2pos(t, m, a, &da, &va);
                eph2pos(t, l, b, &db, &vb);
                d = sqrt(SQ(a[0] - b[0]) + SQ(a[1] - b[1]) +
                         SQ(a[2] - b[2]));
                if (d > worst[idx]) worst[idx] = d;
                count[idx]++;
                if (d > CROSS_FAMILY_TOL_M) {
                    satno2id(m->sat, id);
                    snprintf(detail, sizeof(detail),
                             "%s family 0x%x vs legacy: %.3f m", id,
                             m->hdr.msg_type, d);
                    fail("cross-family", detail);
                }
            }
        }
    }
    printf("cross-family: GPS %d (max %.3f m), QZSS %d (max %.3f m), "
           "BDS %d (max %.3f m); bound %.1f m\n", count[0], worst[0],
           count[1], worst[1], count[2], worst[2], CROSS_FAMILY_TOL_M);
    return count[0] > 0 && count[1] > 0 && count[2] > 0;
}

static int check_legacy_baseline(const nav_t *nav, const char *path)
{
    FILE *fp = fopen(path, "r");
    char line[512], kind[8], sat_id[8], detail[256];
    int same = 0, corrected = 0;

    if (!fp) { fail("baseline", "cannot open"); return 0; }
    if (!fgets(line, sizeof(line), fp)) { fclose(fp); return 0; }
    while (fgets(line, sizeof(line), fp)) {
        int type, toe_week, t_week, sat, i;
        double toe_sow, t_sow, x[3], clk, var, rs[3], dts, v;
        gtime_t toe, t;
        if (sscanf(line, "%7[^,],%7[^,],%d,%d,%lf,%d,%lf,%lf,%lf,%lf,%lf,%lf",
                   kind, sat_id, &type, &toe_week, &toe_sow, &t_week,
                   &t_sow, &x[0], &x[1], &x[2], &clk, &var) != 12) {
            fail("baseline", "malformed row");
            continue;
        }
        sat = satid2no(sat_id);
        toe = gpst2time(toe_week, toe_sow);
        t = gpst2time(t_week, t_sow);
        if (!strcmp(kind, "GEPH")) {
            const geph_t *g = NULL;
            for (i = 0; i < nav->ng; i++)
                if (nav->geph[i].sat == sat &&
                    fabs(timediff(nav->geph[i].toe, toe)) < 1E-3)
                    g = &nav->geph[i];
            if (!g) { fail("baseline", "GLONASS record missing"); continue; }
            geph2pos(t, g, rs, &dts, &v);
        }
        else {
            const eph_t *e = find_eph(nav, sat, type, toe);
            if (!e) { fail("baseline", "record missing"); continue; }
            eph2pos(t, e, rs, &dts, &v);
        }
        if (is_bds3_geo(sat) && type == NAV_D2) {
            /* intended correction: must move far away from the pre-fix
             * value (and is separately required to match the oracle) */
            double d = sqrt(SQ(rs[0] - x[0]) + SQ(rs[1] - x[1]) +
                            SQ(rs[2] - x[2]));
            if (d < GEO_CORRECTION_MIN_M) {
                snprintf(detail, sizeof(detail), "%s GEO D2 unchanged "
                         "(%.3f m)", sat_id, d);
                fail("baseline", detail);
            }
            corrected++;
            continue;
        }
        if (rs[0] != x[0] || rs[1] != x[1] || rs[2] != x[2] ||
            dts != clk || !(v == var)) {
            snprintf(detail, sizeof(detail), "%s family 0x%x t=%d/%.1f "
                     "changed", sat_id, type, t_week, t_sow);
            fail("baseline", detail);
        }
        same++;
    }
    fclose(fp);
    printf("legacy baseline: %d rows bit-identical, %d BDS-3 GEO D2 rows "
           "intentionally corrected\n", same, corrected);
    return same > 0 && corrected > 0;
}

/* Rate terms stored on a legacy record must not leak into the model, and
 * the RINEX decoder must expose delta_n0_dot as ndot for modern records. */
static int check_field_isolation(const nav_t *nav)
{
    int i, legacy_checked = 0, modern_checked = 0;
    for (i = 0; i < nav->n; i++) {
        const eph_t *eph = &nav->eph[i];
        if (modern_record(eph)) {
            if (eph->ndot != eph->delta_n0_dot)
                fail("decode", "modern ndot differs from delta_n0_dot");
            modern_checked++;
        }
        else if (!legacy_checked) {
            eph_t copy = *eph;
            double a[3], b[3], da, db, va, vb;
            gtime_t t = timeadd(eph->toe, 3600.0);
            copy.Adot = 0.05;
            copy.ndot = 1E-12;
            eph2pos(t, eph, a, &da, &va);
            eph2pos(t, &copy, b, &db, &vb);
            if (a[0] != b[0] || a[1] != b[1] || a[2] != b[2] || da != db)
                fail("isolation", "legacy record used Adot/ndot");
            legacy_checked++;
        }
    }
    printf("field isolation: %d modern decoded, legacy rate-term leak "
           "checked\n", modern_checked);
    return legacy_checked > 0 && modern_checked > 0;
}

int main(int argc, char **argv)
{
    nav_t nav = {0};
    int geo_rows = 0, ok = 1;

    if (argc == 3 && !strcmp(argv[1], "--dump-legacy")) {
        if (readrnx(argv[2], 1, "", NULL, &nav, NULL) <= 0) return 2;
        dump_legacy(&nav);
        freenav(&nav, 0x3ff);
        return 0;
    }
    if (argc != 4) {
        fprintf(stderr, "usage: %s FIXTURE ORACLE_CSV LEGACY_BASELINE_CSV\n",
                argv[0]);
        return 2;
    }
    if (readrnx(argv[1], 1, "", NULL, &nav, NULL) <= 0) {
        fprintf(stderr, "FAIL: cannot read %s\n", argv[1]);
        return 1;
    }
    ok &= check_oracle(&nav, argv[2], &geo_rows);
    ok &= check_cross_family(&nav);
    ok &= check_legacy_baseline(&nav, argv[3]);
    ok &= check_field_isolation(&nav);
    freenav(&nav, 0x3ff);
    if (!ok || failures) {
        fprintf(stderr, "modern_orbit_terms: FAIL (%d failures)\n", failures);
        return 1;
    }
    printf("modern_orbit_terms: PASS\n");
    return 0;
}
