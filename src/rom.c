#include "rom.h"
#include "sha256.h"
#include "kelvin.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void wr_be32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static uint32_t rd_be32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static void set_err(char* err, size_t err_cap, const char* msg) {
    if (!err || err_cap == 0) return;
    snprintf(err, err_cap, "%s", msg);
}

/* The digest an image carries about itself: the whole header with its own
 * digest field read as zero, then the payload. Zeroing the field is what makes
 * hashing the header possible at all -- otherwise the value would have to
 * contain itself. Streamed, so a loader never has to copy the payload just to
 * hash it alongside the header. */
static void rom_digest(const uint8_t* header, const uint8_t* payload, size_t payload_len,
                       uint8_t out[ROM_SHA256_LEN]) {
    uint8_t canon[ROM_HEADER_SIZE];
    memcpy(canon, header, ROM_HEADER_SIZE);
    memset(canon + ROM_OFF_IMAGE_SHA, 0, ROM_SHA256_LEN);
    Sha256Ctx c;
    sha256_init(&c);
    sha256_update(&c, canon, ROM_HEADER_SIZE);
    sha256_update(&c, payload, payload_len);
    sha256_final(&c, out);
}

bool rom_has_header(const uint8_t* data, size_t len) {
    return data && len >= ROM_HEADER_SIZE &&
           data[0] == ROM_MAGIC0 && data[1] == ROM_MAGIC1 &&
           data[2] == ROM_MAGIC2 && data[3] == ROM_MAGIC3;
}

void rom_sha256_hex(const uint8_t sha[ROM_SHA256_LEN], char out[65]) {
    static const char hexd[] = "0123456789abcdef";
    for (int i = 0; i < ROM_SHA256_LEN; i++) {
        out[i * 2]     = hexd[sha[i] >> 4];
        out[i * 2 + 1] = hexd[sha[i] & 0x0f];
    }
    out[64] = '\0';
}

uint8_t* rom_wrap(const uint8_t* payload, size_t payload_len, int32_t kelvin,
                  const uint8_t source_sha[ROM_SHA256_LEN], size_t* out_len) {
    if (!payload && payload_len != 0) return NULL;
    if (payload_len > (size_t)0xffffffffu - ROM_HEADER_SIZE) return NULL;
    size_t total = ROM_HEADER_SIZE + payload_len;
    uint8_t* out = (uint8_t*)malloc(total);
    if (!out) return NULL;
    out[0] = ROM_MAGIC0;
    out[1] = ROM_MAGIC1;
    out[2] = ROM_MAGIC2;
    out[3] = ROM_MAGIC3;
    wr_be32(out + ROM_OFF_KELVIN, (uint32_t)kelvin);
    memset(out + ROM_OFF_IMAGE_SHA, 0, ROM_SHA256_LEN);
    wr_be32(out + ROM_OFF_LEN, (uint32_t)payload_len);
    wr_be32(out + ROM_OFF_RESERVED, 0);
    if (source_sha) {
        memcpy(out + ROM_OFF_SOURCE_SHA, source_sha, ROM_SHA256_LEN);
    } else {
        memset(out + ROM_OFF_SOURCE_SHA, 0, ROM_SHA256_LEN);
    }
    if (payload_len) memcpy(out + ROM_HEADER_SIZE, payload, payload_len);
    /* Header is complete bar the digest field, which rom_digest reads as zero. */
    uint8_t digest[ROM_SHA256_LEN];
    rom_digest(out, out + ROM_HEADER_SIZE, payload_len, digest);
    memcpy(out + ROM_OFF_IMAGE_SHA, digest, ROM_SHA256_LEN);
    if (out_len) *out_len = total;
    return out;
}

bool rom_write_file(const char* path, const uint8_t* payload, size_t payload_len,
                    int32_t kelvin, const uint8_t source_sha[ROM_SHA256_LEN],
                    uint8_t sha_out[ROM_SHA256_LEN]) {
    size_t wrapped_len = 0;
    uint8_t* wrapped = rom_wrap(payload, payload_len, kelvin, source_sha, &wrapped_len);
    if (!wrapped) return false;
    FILE* f = fopen(path, "wb");
    if (!f) {
        free(wrapped);
        return false;
    }
    size_t n = fwrite(wrapped, 1, wrapped_len, f);
    fclose(f);
    if (sha_out) memcpy(sha_out, wrapped + ROM_OFF_IMAGE_SHA, ROM_SHA256_LEN);
    free(wrapped);
    return n == wrapped_len;
}

uint8_t* rom_open_image(const uint8_t* file, size_t file_len,
                        size_t* out_len, int32_t* out_kelvin, bool* headered,
                        uint8_t out_source_sha[ROM_SHA256_LEN],
                        char* err, size_t err_cap) {
    if (out_len) *out_len = 0;
    if (out_kelvin) *out_kelvin = 0;
    if (headered) *headered = false;
    if (out_source_sha) memset(out_source_sha, 0, ROM_SHA256_LEN);
    if (!file) {
        set_err(err, err_cap, "empty image");
        return NULL;
    }

    if (file_len >= 4 &&
        file[0] == ROM_MAGIC0 && file[1] == ROM_MAGIC1 &&
        file[2] == ROM_MAGIC2 && file[3] == ROM_MAGIC3) {
        if (file_len < ROM_HEADER_SIZE) {
            set_err(err, err_cap, "truncated NUXR header");
            return NULL;
        }
    } else {
        uint8_t* copy = (uint8_t*)malloc(file_len ? file_len : 1);
        if (!copy) {
            set_err(err, err_cap, "out of memory");
            return NULL;
        }
        if (file_len) memcpy(copy, file, file_len);
        if (out_len) *out_len = file_len;
        return copy;
    }

    if (headered) *headered = true;
    int32_t kelvin = (int32_t)rd_be32(file + ROM_OFF_KELVIN);
    uint32_t payload_len = rd_be32(file + ROM_OFF_LEN);
    uint32_t reserved = rd_be32(file + ROM_OFF_RESERVED);
    if (reserved != 0) {
        set_err(err, err_cap, "NUXR header reserved field is not zero");
        return NULL;
    }
    if ((size_t)ROM_HEADER_SIZE + (size_t)payload_len != file_len) {
        set_err(err, err_cap, "NUXR payload length does not match file size");
        return NULL;
    }
    const uint8_t* payload = file + ROM_HEADER_SIZE;
    uint8_t digest[ROM_SHA256_LEN];
    rom_digest(file, payload, payload_len, digest);
    if (memcmp(digest, file + ROM_OFF_IMAGE_SHA, ROM_SHA256_LEN) != 0) {
        set_err(err, err_cap, "NUXR SHA-256 does not match the image");
        return NULL;
    }
    const char* why = kelvin_reject_reason(kelvin);
    if (why) {
        if (err && err_cap) {
            snprintf(err, err_cap,
                     "VERSION %d is not a legal Kelvin version: %s. "
                     "This platform is %dK (Nux %dK); a guest must be at least as hot. "
                     "See AGENTS.md's versioning section.",
                     kelvin, why, CLOISTER_KELVIN / 1000, NUX_KELVIN / 1000);
        }
        return NULL;
    }
    uint8_t* copy = (uint8_t*)malloc(payload_len ? payload_len : 1);
    if (!copy) {
        set_err(err, err_cap, "out of memory");
        return NULL;
    }
    if (payload_len) memcpy(copy, payload, payload_len);
    if (out_len) *out_len = payload_len;
    if (out_kelvin) *out_kelvin = kelvin;
    if (out_source_sha) memcpy(out_source_sha, file + ROM_OFF_SOURCE_SHA, ROM_SHA256_LEN);
    return copy;
}

uint8_t* rom_load_executable(const char* path, size_t* out_len, int32_t* out_kelvin) {
    if (out_len) *out_len = 0;
    FILE* f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "rom: cannot open %s\n", path);
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        fprintf(stderr, "rom: cannot seek %s\n", path);
        return NULL;
    }
    long n = ftell(f);
    if (n < 0) {
        fclose(f);
        fprintf(stderr, "rom: cannot size %s\n", path);
        return NULL;
    }
    fseek(f, 0, SEEK_SET);
    uint8_t* buf = (uint8_t*)malloc((size_t)n ? (size_t)n : 1);
    if (!buf) {
        fclose(f);
        fprintf(stderr, "rom: out of memory reading %s\n", path);
        return NULL;
    }
    if (n > 0 && fread(buf, 1, (size_t)n, f) != (size_t)n) {
        fclose(f);
        free(buf);
        fprintf(stderr, "rom: cannot read %s\n", path);
        return NULL;
    }
    fclose(f);
    char err[256];
    size_t payload_len = 0;
    uint8_t* payload = rom_open_image(buf, (size_t)n, &payload_len, out_kelvin, NULL,
                                      NULL, err, sizeof(err));
    free(buf);
    if (!payload) {
        fprintf(stderr, "rom: %s: %s\n", path, err);
        return NULL;
    }
    if (out_len) *out_len = payload_len;
    return payload;
}
