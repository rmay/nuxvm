#include "fluxio_include.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>

typedef struct { char** items; int count, cap; } PathSet;

static void pathset_add(PathSet* s, const char* p) {
    if (s->count == s->cap) {
        s->cap = s->cap ? s->cap * 2 : 8;
        s->items = realloc(s->items, sizeof(char*) * s->cap);
    }
    s->items[s->count++] = strdup(p);
}

static bool pathset_contains(PathSet* s, const char* p) {
    for (int i = 0; i < s->count; i++) {
        if (strcmp(s->items[i], p) == 0) return true;
    }
    return false;
}

static void pathset_remove_last(PathSet* s) {
    if (s->count > 0) free(s->items[--s->count]);
}

static void pathset_free(PathSet* s) {
    for (int i = 0; i < s->count; i++) free(s->items[i]);
    free(s->items);
}

static char* read_file(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long size = ftell(f);
    if (size < 0) { fclose(f); return NULL; }
    fseek(f, 0, SEEK_SET);
    char* buf = malloc((size_t) size + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t n = fread(buf, 1, (size_t) size, f);
    fclose(f);
    buf[n] = '\0';
    return buf;
}

static char* dirname_of(const char* path) {
    const char* slash = strrchr(path, '/');
    if (!slash) return strdup(".");
    size_t len = (size_t) (slash - path);
    char* out = malloc(len + 1);
    memcpy(out, path, len);
    out[len] = '\0';
    return out;
}

static char* join_path(const char* dir, const char* rel) {
    if (rel[0] == '/') return strdup(rel);
    size_t dl = strlen(dir), rl = strlen(rel);
    char* out = malloc(dl + 1 + rl + 1);
    memcpy(out, dir, dl);
    out[dl] = '/';
    memcpy(out + dl + 1, rel, rl);
    out[dl + 1 + rl] = '\0';
    return out;
}

static void list_push_move(FxTokenList* dst, FxToken t) {
    if (dst->count == dst->capacity) {
        dst->capacity = dst->capacity ? dst->capacity * 2 : 64;
        dst->tokens = realloc(dst->tokens, sizeof(FxToken) * dst->capacity);
    }
    dst->tokens[dst->count++] = t;
}

/* Returns a token list (owned by the caller) for `path` with every include
 * directive it contains (transitively) resolved and spliced in, or NULL on
 * error. `stack` tracks the current include chain (cycle detection);
 * `seen` tracks every path already included anywhere in this compile
 * (once-only inclusion, like a header guard). Never includes a trailing
 * EOF token -- the top-level caller appends exactly one at the very end. */
static FxTokenList* load_resolved(const char* path, PathSet* stack, PathSet* seen) {
    char resolved[PATH_MAX];
    if (!realpath(path, resolved)) {
        fprintf(stderr, "fluxio: cannot resolve include path '%s': %s\n", path, strerror(errno));
        return NULL;
    }

    if (pathset_contains(stack, resolved)) {
        fprintf(stderr, "fluxio: circular include detected involving '%s'\n", resolved);
        return NULL;
    }

    FxTokenList* out = calloc(1, sizeof(FxTokenList));

    if (pathset_contains(seen, resolved)) {
        return out; /* already included elsewhere in this compile: contributes zero tokens */
    }
    pathset_add(seen, resolved);
    pathset_add(stack, resolved);

    char* source = read_file(path);
    if (!source) {
        fprintf(stderr, "fluxio: cannot read include file '%s'\n", path);
        free(out);
        pathset_remove_last(stack);
        return NULL;
    }
    FxTokenList* toks = fx_tokenize(source);
    free(source);
    if (!toks) {
        free(out);
        pathset_remove_last(stack);
        return NULL;
    }

    char* dir = dirname_of(resolved);
    bool ok = true;

    for (size_t i = 0; i < toks->count && ok; i++) {
        if (toks->tokens[i].type == FXTOK_KW_INCLUDE) {
            if (i + 2 >= toks->count ||
                toks->tokens[i + 1].type != FXTOK_STRING_LIT ||
                toks->tokens[i + 2].type != FXTOK_SEMI) {
                fprintf(stderr, "fluxio: malformed include directive at line %d "
                        "(expected: include \"path\";)\n", toks->tokens[i].line);
                ok = false;
                break;
            }
            char* child_path = join_path(dir, toks->tokens[i + 1].value);
            FxTokenList* child = load_resolved(child_path, stack, seen);
            free(child_path);
            if (!child) { ok = false; break; }
            for (size_t k = 0; k < child->count; k++) {
                if (child->tokens[k].type == FXTOK_EOF) continue;
                list_push_move(out, child->tokens[k]);
            }
            free(child->tokens);
            free(child);
            i += 2; /* skip the STRING_LIT and SEMI already consumed above */
        } else if (toks->tokens[i].type != FXTOK_EOF) {
            list_push_move(out, toks->tokens[i]);
        }
    }

    free(dir);
    /* Every real token was moved (not copied) into `out`, so free just the
     * source list's shell -- freeing its strings too would leave `out`
     * holding dangling pointers. */
    free(toks->tokens);
    free(toks);

    pathset_remove_last(stack);

    if (!ok) {
        fx_token_list_free(out);
        return NULL;
    }
    return out;
}

FxTokenList* fx_load_with_includes(const char* main_path) {
    PathSet stack = { 0 };
    PathSet seen = { 0 };
    FxTokenList* result = load_resolved(main_path, &stack, &seen);
    pathset_free(&stack);
    pathset_free(&seen);
    if (!result) return NULL;

    FxToken eof;
    memset(&eof, 0, sizeof(eof));
    eof.type = FXTOK_EOF;
    list_push_move(result, eof);
    return result;
}
