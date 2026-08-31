/*
 * Private RINEX-parse contract for Issue #20 Phase A.
 *
 * This test is deliberately separate from the public-only containment test:
 * raw URAI components are decoder-native private fields and must not be
 * mistaken for the public metric-SVA variance contract.
 */

#include "../../src/rtklib.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define EXPECTED_FIXTURE_SHA256 \
    "05c3d4f9e0a7750aa48ed8890879817e83e17e84183dbb491757eec3951edc67"
#define EXPECTED_PROVENANCE_SHA256 \
    "50be1a6928f901b9cc90b02582784a1c99d70b575b686795a8a00d84b2405355"

static int fail(const char *message)
{
    fprintf(stderr, "FAIL: %s\n", message);
    return 1;
}

static int finite_close(double actual, double expected, double tolerance)
{
    return isfinite(actual) && isfinite(expected) &&
           fabs(actual - expected) <= tolerance;
}

typedef struct {
    uint32_t state[8];
    uint64_t bit_length;
    unsigned char block[64];
    size_t block_length;
} sha256_context_t;

static uint32_t sha256_rotr(uint32_t value, unsigned int count)
{
    return (value >> count) | (value << (32U - count));
}

static uint32_t sha256_load_be32(const unsigned char *data)
{
    return ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) |
           ((uint32_t)data[2] << 8) | (uint32_t)data[3];
}

static void sha256_store_be32(unsigned char *data, uint32_t value)
{
    data[0] = (unsigned char)(value >> 24);
    data[1] = (unsigned char)(value >> 16);
    data[2] = (unsigned char)(value >> 8);
    data[3] = (unsigned char)value;
}

static void sha256_transform(sha256_context_t *context,
                             const unsigned char block[64])
{
    static const uint32_t constants[64] = {
        0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U,
        0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
        0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U,
        0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
        0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU,
        0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
        0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U,
        0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
        0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U,
        0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
        0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U,
        0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
        0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U,
        0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
        0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
        0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U
    };
    uint32_t words[64];
    uint32_t a, b, c, d, e, f, g, h;
    uint32_t t1, t2, s0, s1, ch, maj;
    int i;

    for (i = 0; i < 16; ++i) words[i] = sha256_load_be32(block + i * 4);
    for (i = 16; i < 64; ++i) {
        s0 = sha256_rotr(words[i - 15], 7) ^
             sha256_rotr(words[i - 15], 18) ^ (words[i - 15] >> 3);
        s1 = sha256_rotr(words[i - 2], 17) ^
             sha256_rotr(words[i - 2], 19) ^ (words[i - 2] >> 10);
        words[i] = words[i - 16] + s0 + words[i - 7] + s1;
    }
    a = context->state[0];
    b = context->state[1];
    c = context->state[2];
    d = context->state[3];
    e = context->state[4];
    f = context->state[5];
    g = context->state[6];
    h = context->state[7];
    for (i = 0; i < 64; ++i) {
        s1 = sha256_rotr(e, 6) ^ sha256_rotr(e, 11) ^ sha256_rotr(e, 25);
        ch = (e & f) ^ ((~e) & g);
        t1 = h + s1 + ch + constants[i] + words[i];
        s0 = sha256_rotr(a, 2) ^ sha256_rotr(a, 13) ^ sha256_rotr(a, 22);
        maj = (a & b) ^ (a & c) ^ (b & c);
        t2 = s0 + maj;
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }
    context->state[0] += a;
    context->state[1] += b;
    context->state[2] += c;
    context->state[3] += d;
    context->state[4] += e;
    context->state[5] += f;
    context->state[6] += g;
    context->state[7] += h;
}

static void sha256_init(sha256_context_t *context)
{
    static const uint32_t initial_state[8] = {
        0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
        0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U
    };
    memcpy(context->state, initial_state, sizeof(initial_state));
    context->bit_length = 0;
    context->block_length = 0;
}

static void sha256_update(sha256_context_t *context,
                          const unsigned char *data, size_t length)
{
    size_t take;

    context->bit_length += (uint64_t)length * 8U;
    while (length != 0) {
        take = 64 - context->block_length;
        if (take > length) take = length;
        memcpy(context->block + context->block_length, data, take);
        context->block_length += take;
        data += take;
        length -= take;
        if (context->block_length == 64) {
            sha256_transform(context, context->block);
            context->block_length = 0;
        }
    }
}

static void sha256_final(sha256_context_t *context, unsigned char digest[32])
{
    size_t i;

    i = context->block_length;
    context->block[i++] = 0x80;
    if (i > 56) {
        while (i < 64) context->block[i++] = 0;
        sha256_transform(context, context->block);
        i = 0;
    }
    while (i < 56) context->block[i++] = 0;
    for (i = 0; i < 8; ++i)
        context->block[63 - i] =
            (unsigned char)(context->bit_length >> (i * 8));
    sha256_transform(context, context->block);
    for (i = 0; i < 8; ++i)
        sha256_store_be32(digest + i * 4, context->state[i]);
}

static int sha256_file(const char *path, char hex[65])
{
    static const char digits[] = "0123456789abcdef";
    unsigned char data[4096];
    unsigned char digest[32];
    sha256_context_t context;
    FILE *file;
    size_t count, i;

    file = fopen(path, "rb");
    if (!file) return 0;
    sha256_init(&context);
    while ((count = fread(data, 1, sizeof(data), file)) != 0)
        sha256_update(&context, data, count);
    if (ferror(file)) {
        fclose(file);
        return 0;
    }
    if (fclose(file) != 0) return 0;
    sha256_final(&context, digest);
    for (i = 0; i < sizeof(digest); ++i) {
        hex[i * 2] = digits[digest[i] >> 4];
        hex[i * 2 + 1] = digits[digest[i] & 0x0f];
    }
    hex[64] = '\0';
    return 1;
}

static int provenance_matches_fixture_hash(const char *path,
                                            const char fixture_hash[65])
{
    FILE *file;
    char line[512];
    const char *prefix = "\"fixture_sha256\": \"";
    const char *value;

    file = fopen(path, "rb");
    if (!file) return 0;
    while (fgets(line, sizeof(line), file)) {
        value = strstr(line, prefix);
        if (value) {
            value += strlen(prefix);
            fclose(file);
            return strncmp(value, fixture_hash, 64) == 0 && value[64] == '"';
        }
    }
    fclose(file);
    return 0;
}

static int file_contains(const char *path, const char *needle)
{
    FILE *file;
    char line[512];

    file = fopen(path, "rb");
    if (!file) return 0;
    while (fgets(line, sizeof(line), file)) {
        if (strstr(line, needle)) {
            fclose(file);
            return 1;
        }
    }
    fclose(file);
    return 0;
}

static const eph_t *find_eph(const nav_t *nav, int system, int prn,
                             int family)
{
    int i;

    if (!nav) return NULL;
    for (i = 0; i < nav->n; ++i) {
        const eph_t *eph = nav->eph + i;
        int decoded_prn = 0;
        if (satsys(eph->sat, &decoded_prn) != system ||
            decoded_prn != prn || eph->hdr.msg_type != family) continue;
        return eph;
    }
    return NULL;
}

static int check_modern(const eph_t *eph, int system, int prn, int family,
                        const double expected_ned[3], double expected_ed,
                        int expected_health, const char *label)
{
    int i, decoded_prn = 0;

    if (!eph) {
        fprintf(stderr, "FAIL: missing %s record\n", label);
        return 0;
    }
    if (satsys(eph->sat, &decoded_prn) != system || decoded_prn != prn ||
        eph->hdr.msg_type != family) {
        fprintf(stderr, "FAIL: %s system/family identity changed\n", label);
        return 0;
    }
    for (i = 0; i < 3; ++i) {
        if (!finite_close(eph->urai_ned[i], expected_ned[i], 1E-12)) {
            fprintf(stderr, "FAIL: %s URAI-NED[%d] changed\n", label, i);
            return 0;
        }
    }
    if (!finite_close(eph->urai_ed, expected_ed, 1E-12)) {
        fprintf(stderr, "FAIL: %s URAI-ED changed\n", label);
        return 0;
    }
    if (eph->svh != expected_health) {
        fprintf(stderr, "FAIL: %s raw health changed\n", label);
        return 0;
    }
    /* Phase A must clear the accidental legacy scalar slot.  A raw URAI
     * component is an index, not metres and not a public variance input. */
    if (!finite_close(eph->sva, -1.0, 0.0)) {
        fprintf(stderr, "FAIL: %s retained a misleading legacy SVA\n", label);
        return 0;
    }
    return 1;
}

int main(int argc, char **argv)
{
    nav_t nav = {0};
    const eph_t *g01_cnav;
    const eph_t *j02_cnav;
    const eph_t *j02_cnv2;
    const eph_t *g01_lnav;
    const eph_t *c19_cnv2;
    static const double g01_ned[3] = {-5.0, 1.0, 7.0};
    static const double j02_ned[3] = {-3.0, 0.0, 0.0};
    char fixture_hash[65];
    char provenance_hash[65];
    int status;

    if (argc != 3)
        return fail("usage: test_modern_urai_private_parse <RINEX4> <provenance>");

    /* Do not silently fall back to another fixture.  The provenance file is
     * required to identify the exact RINEX 4.02 excerpt used below. */
    if (!sha256_file(argv[1], fixture_hash) ||
        strcmp(fixture_hash, EXPECTED_FIXTURE_SHA256) != 0 ||
        !sha256_file(argv[2], provenance_hash) ||
        strcmp(provenance_hash, EXPECTED_PROVENANCE_SHA256) != 0 ||
        !provenance_matches_fixture_hash(argv[2], fixture_hash) ||
        !file_contains(argv[2], "\"fixture\": \"brd400_selected.rnx\"") ||
        !file_contains(argv[2], "\"rinex_version\": \"4.02\"") ||
        !file_contains(argv[2], "\"record_header\": \"EPH G01 CNAV\"") ||
        !file_contains(argv[2], "\"record_header\": \"EPH J02 CNAV\"") ||
        !file_contains(argv[2], "\"record_header\": \"EPH J02 CNV2\""))
        return fail("fixture provenance is missing or does not identify BRD400 RINEX 4.02");

    status = readrnx(argv[1], 1, "", NULL, &nav, NULL);
    if (status <= 0) {
        freenav(&nav, 0x3ff);
        return fail("RINEX4 fixture could not be parsed");
    }

    g01_cnav = find_eph(&nav, SYS_GPS, 1, NAV_CNAV);
    j02_cnav = find_eph(&nav, SYS_QZS, 194, NAV_CNAV);
    j02_cnv2 = find_eph(&nav, SYS_QZS, 194, NAV_CNV2);
    g01_lnav = find_eph(&nav, SYS_GPS, 1, NAV_LNAV);
    c19_cnv2 = find_eph(&nav, SYS_CMP, 19, NAV_CNV2);

    if (!check_modern(g01_cnav, SYS_GPS, 1, NAV_CNAV, g01_ned, -3.0,
                      7, "GPS G01 CNAV") ||
        !check_modern(j02_cnav, SYS_QZS, 194, NAV_CNAV, j02_ned, -9.0,
                      0, "QZSS J02 CNAV") ||
        !check_modern(j02_cnv2, SYS_QZS, 194, NAV_CNV2, j02_ned, -9.0,
                      0, "QZSS J02 CNV2")) {
        freenav(&nav, 0x3ff);
        return 1;
    }
    if (!g01_lnav || !finite_close(g01_lnav->sva, 2.0, 1E-12)) {
        freenav(&nav, 0x3ff);
        return fail("GPS G01 LNAV metric SVA regressed");
    }
    if (!c19_cnv2 || !finite_close(c19_cnv2->sva, 15.0, 1E-12)) {
        freenav(&nav, 0x3ff);
        return fail("BDS C19 CNV2 metric SVA regressed");
    }

    freenav(&nav, 0x3ff);
    puts("modern_urai_private_parse: PASS (GPS/QZSS raw URAI; GPS CNV2 coverage gap: real provenance fixture not available; NOT_RUN)");
    return 0;
}
