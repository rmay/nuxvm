#ifndef FLUXLINK_H
#define FLUXLINK_H

/* The linking core behind bin/fluxlink, factored out of its CLI so the same
 * merge can happen in-process. Two callers:
 *
 *   - src/fluxlink.c   -- the build-time tool: reads app.bin, merges, writes
 *                         a NUXR image, and enforces the append-only export
 *                         policy (docs/quill_fluxio.md Phase 0.5 point 2).
 *   - src/cloister.c   -- on-the-fly `.fx` launch: compiles the app to a raw
 *                         blob in memory, merges, and runs the result without
 *                         ever touching the filesystem.
 *
 * The append-only check deliberately stays a *build-time* concern and is not
 * called on the launch path: a load-time gate could reject a legitimate image.
 *
 * See abi/nux-abi.json "linked_image_format" for the output layout.
 */

#include "rom.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* The one library Fluxio apps link against today (built by the Makefile).
 * Paths are repo-relative, like cloister's other asset paths. */
#define FLUXLINK_UISF_LIB     "lib/uisf.bin"
#define FLUXLINK_UISF_SYMTAB  "lib/uisf.symtab.json"
#define FLUXLINK_UISF_EXPORTS "abi/uisf.exports.json"

typedef struct {
    const char* lib_path;      /* compiled via `luxc -base 0x<MM_ABI_LIBRARY_CODE_BASE>` */
    const char* symtab_path;   /* full dictionary dump, from `luxc -symbols` */
    const char* exports_path;  /* committed, append-only ordered export list */
    int32_t app_base;          /* where the app image loads */
    int32_t lib_base;          /* trampoline table base */
} FluxlinkSpec;

/* Fills spec with the uisf library paths and the standard graphical bases. */
void fluxlink_spec_defaults(FluxlinkSpec* spec);

/* Merges the library named by `spec` into `app_bytes` (a raw, header-free
 * image of app_len bytes loaded at spec->app_base) and returns the merged
 * blob, *out_len bytes long, ready to hand to machine_create() or to write
 * out as a ROM. Caller must free() it.
 *
 * app_source_sha is the app's own recorded source digest (pass NULL if it has
 * none) and feeds out_source_sha, the provenance digest of everything that
 * went into the merge. Both out-params and out_export_count may be NULL.
 *
 * Returns NULL on any error, with a message in err. */
uint8_t* fluxlink_merge(const FluxlinkSpec* spec,
                        const uint8_t* app_bytes, size_t app_len,
                        const uint8_t* app_source_sha,
                        size_t* out_len,
                        uint8_t out_source_sha[ROM_SHA256_LEN],
                        int* out_export_count,
                        char* err, size_t err_cap);

/* Verifies that exports_path only *appends* relative to prev_exports_path:
 * existing slots may never be reordered, renamed, or removed. Build-time
 * only. Reports the slot counts through the out-params (either may be NULL)
 * so the caller can log what it checked. */
bool fluxlink_check_append_only(const char* exports_path, const char* prev_exports_path,
                                int* out_prev_count, int* out_count,
                                char* err, size_t err_cap);

#endif /* FLUXLINK_H */
