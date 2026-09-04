#ifndef ROM_H
#define ROM_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/* On-disk guest app image (the .bin next to each app source). The VM still executes raw bytecode;
 * this wrapper is stripped by Cloister, nux, /sys/vm/new, and fluxlink
 * before the payload is loaded. Library builds (`luxc -base`) stay raw.
 *
 * Layout (80 bytes, big-endian), then payload:
 *   0  4  magic          ASCII "NUXR"
 *   4  4  kelvin         guest VERSION as uint32
 *   8 32  image_sha256   digest of the header (with these 32 bytes read as
 *                        zero) followed by the payload
 *  40  4  payload_len    byte length of what follows
 *  44  4  reserved       zero
 *  48 32  source_sha256  digest of the sources the payload was compiled from
 *
 * The digest covers the header as well as the payload -- its own field is
 * canonically zeroed to break the self-reference -- so kelvin, payload_len,
 * reserved and source_sha256 are all tamper-evident, not just the bytecode.
 *
 * source_sha256 is SHA-256 of the main source text followed by the SHA-256 of
 * each file it included, in resolution order. It is an assertion by the
 * compiler: what proves it is recompiling the tree (`make verify-bins`), not
 * the field itself.
 *
 * A digest a file carries about itself only catches corruption: anyone who
 * rewrites the payload can recompute it. What checks an image against
 * something outside itself is `make verify-bins`, at build time.
 *
 * Cloister refuses a headered image whose Kelvin is illegal on this
 * platform (colder than CLOISTER_KELVIN, zero, or negative) -- the same
 * gate the compilers apply to VERSION. A hotter (older) ROM still runs.
 */

#define ROM_HEADER_SIZE 80
#define ROM_SHA256_LEN  32
#define ROM_MAGIC0      'N'
#define ROM_MAGIC1      'U'
#define ROM_MAGIC2      'X'
#define ROM_MAGIC3      'R'

/* Byte offsets into the header, so callers stop counting on their fingers. */
#define ROM_OFF_KELVIN     4
#define ROM_OFF_IMAGE_SHA  8
#define ROM_OFF_LEN        40
#define ROM_OFF_RESERVED   44
#define ROM_OFF_SOURCE_SHA 48

bool rom_has_header(const uint8_t* data, size_t len);

void rom_sha256_hex(const uint8_t sha[ROM_SHA256_LEN], char out[65]);

/* Wrap payload into a newly-allocated headered image. Caller frees.
 * source_sha may be NULL, which records 32 zero bytes ("origin unrecorded"). */
uint8_t* rom_wrap(const uint8_t* payload, size_t payload_len, int32_t kelvin,
                  const uint8_t source_sha[ROM_SHA256_LEN], size_t* out_len);

bool rom_write_file(const char* path, const uint8_t* payload, size_t payload_len,
                    int32_t kelvin, const uint8_t source_sha[ROM_SHA256_LEN],
                    uint8_t sha_out[ROM_SHA256_LEN]);

/* Split a file buffer into an executable payload.
 * Headered: the image digest must match, Kelvin must be legal, payload
 *   is copied out. *out_kelvin is the declared version; *headered is true.
 * Raw: whole buffer is copied; *out_kelvin is 0; *headered is false.
 * Returns malloc'd payload (caller frees) or NULL. err and out_source_sha are
 * optional; out_source_sha is zeroed for a raw image. */
uint8_t* rom_open_image(const uint8_t* file, size_t file_len,
                        size_t* out_len, int32_t* out_kelvin, bool* headered,
                        uint8_t out_source_sha[ROM_SHA256_LEN],
                        char* err, size_t err_cap);

/* Read a path and rom_open_image it. Prints the error to stderr on failure. */
uint8_t* rom_load_executable(const char* path, size_t* out_len, int32_t* out_kelvin);

#endif
