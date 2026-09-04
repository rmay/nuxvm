#ifndef SHA256_H
#define SHA256_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/* Host-side SHA-256 (FIPS 180-4). Used to fingerprint guest ROM payloads and
 * the sources they were compiled from. Not a VM opcode and not available to
 * guest code. */

typedef struct {
    uint32_t h[8];
    uint8_t  buf[64];
    size_t   buflen;
    uint64_t total;   /* bytes absorbed so far */
} Sha256Ctx;

/* Streaming form. Lets a caller hash several disjoint runs -- a canonical
 * header followed by a payload, say -- without first splicing them into one
 * buffer. */
void sha256_init(Sha256Ctx* c);
void sha256_update(Sha256Ctx* c, const uint8_t* data, size_t len);
void sha256_final(Sha256Ctx* c, uint8_t out[32]);

/* One-shot convenience. `data` may be NULL when len is 0. */
void sha256(const uint8_t* data, size_t len, uint8_t out[32]);

/* Digest of a file's contents, read in chunks. Returns false (and zeroes out)
 * if the file cannot be opened or read. */
bool sha256_file(const char* path, uint8_t out[32]);

#endif
