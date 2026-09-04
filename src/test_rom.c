#include "rom.h"
#include "sha256.h"
#include "kelvin.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

static int tests_run = 0;
static int tests_failed = 0;

#define CHECK(cond, msg) do { \
    tests_run++; \
    if (!(cond)) { \
        tests_failed++; \
        fprintf(stderr, "  FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__); \
    } \
} while (0)

static void test_sha256_known_answers(void) {
    printf("Testing SHA-256 known answers...\n");
    uint8_t out[32];
    char hex[65];

    sha256((const uint8_t*)"", 0, out);
    rom_sha256_hex(out, hex);
    CHECK(strcmp(hex, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855") == 0,
          "SHA-256 of empty string");

    sha256((const uint8_t*)"abc", 3, out);
    rom_sha256_hex(out, hex);
    CHECK(strcmp(hex, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad") == 0,
          "SHA-256 of 'abc'");
}

static void test_wrap_roundtrip(void) {
    printf("Testing NUXR wrap/unwrap roundtrip...\n");
    const uint8_t payload[] = { 0x00, 0x00, 0x00, 0x00, 0x2a, 0x36 }; /* PUSH 42; HALT */
    size_t wrapped_len = 0;
    uint8_t* wrapped = rom_wrap(payload, sizeof(payload), CLOISTER_KELVIN, NULL, &wrapped_len);
    CHECK(wrapped != NULL, "rom_wrap returns an image");
    CHECK(wrapped_len == ROM_HEADER_SIZE + sizeof(payload), "wrapped size is header + payload");
    CHECK(rom_has_header(wrapped, wrapped_len), "wrapped image starts with NUXR");

    char err[256];
    size_t out_len = 0;
    int32_t kelvin = 0;
    bool headered = false;
    uint8_t* got = rom_open_image(wrapped, wrapped_len, &out_len, &kelvin, &headered, NULL, err, sizeof(err));
    CHECK(got != NULL, "rom_open_image accepts a well-formed ROM");
    CHECK(headered, "headered flag is set");
    CHECK(kelvin == CLOISTER_KELVIN, "Kelvin round-trips");
    CHECK(out_len == sizeof(payload), "payload length round-trips");
    CHECK(got && memcmp(got, payload, sizeof(payload)) == 0, "payload bytes round-trip");
    free(got);
    free(wrapped);
}

static void test_sha_mismatch(void) {
    printf("Testing NUXR SHA-256 mismatch is rejected...\n");
    uint8_t payload[] = { 0x36 };
    size_t wrapped_len = 0;
    uint8_t* wrapped = rom_wrap(payload, sizeof(payload), CLOISTER_KELVIN, NULL, &wrapped_len);
    CHECK(wrapped != NULL, "rom_wrap for tamper test");
    wrapped[ROM_HEADER_SIZE] ^= 0xff;
    char err[256] = {0};
    uint8_t* got = rom_open_image(wrapped, wrapped_len, NULL, NULL, NULL, NULL, err, sizeof(err));
    CHECK(got == NULL, "tampered payload is rejected");
    CHECK(strstr(err, "SHA-256") != NULL, "tamper error names SHA-256");
    free(got);
    free(wrapped);
}

static void test_kelvin_gate(void) {
    printf("Testing NUXR Kelvin gate on open...\n");
    uint8_t payload[] = { 0x36 };
    char err[256];

    size_t wlen = 0;
    uint8_t* cold = rom_wrap(payload, sizeof(payload), CLOISTER_KELVIN - 1, NULL, &wlen);
    uint8_t* got = rom_open_image(cold, wlen, NULL, NULL, NULL, NULL, err, sizeof(err));
    CHECK(got == NULL, "colder-than-platform ROM is rejected");
    CHECK(strstr(err, "rule 5") != NULL, "cold ROM cites Kelvin rule 5");
    CHECK(strstr(err, "AGENTS.md") != NULL, "cold ROM points at AGENTS.md");
    free(got);
    free(cold);

    uint8_t* hot = rom_wrap(payload, sizeof(payload), CLOISTER_KELVIN + 100000, NULL, &wlen);
    int32_t kelvin = 0;
    got = rom_open_image(hot, wlen, NULL, &kelvin, NULL, NULL, err, sizeof(err));
    CHECK(got != NULL, "hotter-than-platform ROM still opens");
    CHECK(kelvin == CLOISTER_KELVIN + 100000, "hot Kelvin is preserved");
    free(got);
    free(hot);

    uint8_t* zero = rom_wrap(payload, sizeof(payload), 0, NULL, &wlen);
    got = rom_open_image(zero, wlen, NULL, NULL, NULL, NULL, err, sizeof(err));
    CHECK(got == NULL, "absolute-zero ROM is rejected");
    CHECK(strstr(err, "absolute zero") != NULL, "zero ROM cites rule 3");
    free(got);
    free(zero);
}

static void test_raw_passthrough(void) {
    printf("Testing raw (unheadered) images still load...\n");
    uint8_t raw[] = { 0x00, 0x00, 0x00, 0x00, 0x05, 0x36 };
    bool headered = true;
    int32_t kelvin = 99;
    size_t out_len = 0;
    char err[64];
    uint8_t* got = rom_open_image(raw, sizeof(raw), &out_len, &kelvin, &headered, NULL, err, sizeof(err));
    CHECK(got != NULL, "raw snippet opens");
    CHECK(!headered, "raw snippet is not headered");
    CHECK(kelvin == 0, "raw snippet has no Kelvin");
    CHECK(out_len == sizeof(raw) && memcmp(got, raw, sizeof(raw)) == 0, "raw bytes unchanged");
    free(got);
}

static void test_truncated_header(void) {
    printf("Testing truncated NUXR file is rejected...\n");
    uint8_t payload[] = { 0x36, 0x36 };
    size_t wlen = 0;
    uint8_t* wrapped = rom_wrap(payload, sizeof(payload), CLOISTER_KELVIN, NULL, &wlen);
    char err[256] = {0};
    uint8_t* got = rom_open_image(wrapped, wlen - 1, NULL, NULL, NULL, NULL, err, sizeof(err));
    CHECK(got == NULL, "truncated headered file is rejected");
    CHECK(strstr(err, "length") != NULL, "truncation names the length field");
    free(got);
    free(wrapped);

    uint8_t stub[10] = { 'N', 'U', 'X', 'R', 0, 0, 0, 1, 0, 0 };
    got = rom_open_image(stub, sizeof(stub), NULL, NULL, NULL, NULL, err, sizeof(err));
    CHECK(got == NULL, "NUXR magic shorter than a header is rejected, not run as bytecode");
    CHECK(strstr(err, "truncated") != NULL, "short NUXR names truncation");
    free(got);
}


/* The v1 header hashed only the payload, so kelvin could be edited freely and
 * the image still loaded. The digest now covers the header with its own field
 * zeroed, which is what these check. */
static void test_header_field_tamper(void) {
    printf("Testing NUXR header fields are covered by the digest...\n");
    const uint8_t payload[] = { 0x36 };
    uint8_t src[ROM_SHA256_LEN];
    memset(src, 0xa5, sizeof(src));

    struct { const char* what; size_t off; } spots[] = {
        { "kelvin",        ROM_OFF_KELVIN },
        { "source_sha256", ROM_OFF_SOURCE_SHA },
    };
    for (size_t i = 0; i < sizeof(spots) / sizeof(spots[0]); i++) {
        size_t wlen = 0;
        uint8_t* wrapped = rom_wrap(payload, sizeof(payload), CLOISTER_KELVIN, src, &wlen);
        CHECK(wrapped != NULL, "rom_wrap for header tamper test");
        if (!wrapped) continue;
        wrapped[spots[i].off] ^= 0x01;
        char err[256] = {0};
        uint8_t* got = rom_open_image(wrapped, wlen, NULL, NULL, NULL, NULL, err, sizeof(err));
        CHECK(got == NULL, spots[i].what);
        CHECK(strstr(err, "SHA-256") != NULL, "header tamper is reported as a digest mismatch");
        free(got);
        free(wrapped);
    }

    /* payload_len is caught earlier, by the file-size cross-check. */
    size_t wlen = 0;
    uint8_t* wrapped = rom_wrap(payload, sizeof(payload), CLOISTER_KELVIN, src, &wlen);
    if (wrapped) {
        wrapped[ROM_OFF_LEN + 3] ^= 0x01;
        char err[256] = {0};
        uint8_t* got = rom_open_image(wrapped, wlen, NULL, NULL, NULL, NULL, err, sizeof(err));
        CHECK(got == NULL, "edited payload_len is rejected");
        CHECK(strstr(err, "length") != NULL, "payload_len tamper names the length");
        free(got);
        free(wrapped);
    }
}

static void test_reserved_nonzero(void) {
    printf("Testing a non-zero reserved field is rejected...\n");
    const uint8_t payload[] = { 0x36 };
    size_t wlen = 0;
    uint8_t* wrapped = rom_wrap(payload, sizeof(payload), CLOISTER_KELVIN, NULL, &wlen);
    CHECK(wrapped != NULL, "rom_wrap for reserved test");
    if (!wrapped) return;
    wrapped[ROM_OFF_RESERVED + 3] = 0x01;
    char err[256] = {0};
    uint8_t* got = rom_open_image(wrapped, wlen, NULL, NULL, NULL, NULL, err, sizeof(err));
    CHECK(got == NULL, "non-zero reserved is rejected");
    CHECK(strstr(err, "reserved") != NULL, "reserved error names the field");
    free(got);
    free(wrapped);
}

static void test_source_sha_roundtrip(void) {
    printf("Testing source_sha256 round-trips...\n");
    const uint8_t payload[] = { 0x36 };
    uint8_t src[ROM_SHA256_LEN];
    for (int i = 0; i < ROM_SHA256_LEN; i++) src[i] = (uint8_t)(i * 7 + 1);

    size_t wlen = 0;
    uint8_t* wrapped = rom_wrap(payload, sizeof(payload), CLOISTER_KELVIN, src, &wlen);
    CHECK(wrapped != NULL, "rom_wrap with a source digest");
    if (!wrapped) return;
    uint8_t got_src[ROM_SHA256_LEN];
    char err[256];
    uint8_t* got = rom_open_image(wrapped, wlen, NULL, NULL, NULL, got_src, err, sizeof(err));
    CHECK(got != NULL, "image with a source digest opens");
    CHECK(memcmp(got_src, src, ROM_SHA256_LEN) == 0, "source_sha256 round-trips");
    free(got);
    free(wrapped);

    /* A raw image has no header to carry one, so the out-param is zeroed. */
    uint8_t raw[] = { 0x36 };
    memset(got_src, 0xff, sizeof(got_src));
    got = rom_open_image(raw, sizeof(raw), NULL, NULL, NULL, got_src, err, sizeof(err));
    uint8_t zeroes[ROM_SHA256_LEN] = {0};
    CHECK(got != NULL, "raw image still opens");
    CHECK(memcmp(got_src, zeroes, ROM_SHA256_LEN) == 0, "raw image reports a zero source digest");
    free(got);
}

int main(void) {
    printf("\n=== ROM header / SHA-256 tests ===\n\n");
    test_sha256_known_answers();
    test_wrap_roundtrip();
    test_sha_mismatch();
    test_kelvin_gate();
    test_raw_passthrough();
    test_truncated_header();
    test_header_field_tamper();
    test_reserved_nonzero();
    test_source_sha_roundtrip();

    printf("\n%d/%d ROM checks passed.\n", tests_run - tests_failed, tests_run);
    if (tests_failed > 0) {
        fprintf(stderr, "ROM TESTS FAILED\n");
        return 1;
    }
    printf("All ROM tests passed!\n");
    return 0;
}
