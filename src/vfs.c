#include "vfs.h"
#include "system.h"
#include "machine.h"
#include "compiler.h"
#include "vm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <dirent.h>
#include <sys/stat.h>
#include <errno.h>

// All VFS state lives in sys->vfs (per-System, so child VMs get isolated
// namespaces). Files are refcounted: each fd slot and each mount entry holds
// one reference; the file's close destructor runs when the last is released.

static void vfs_file_retain(VFSFile* file) {
    file->refcount++;
}

static void vfs_file_release(VFSFile* file) {
    if (!file) return;
    file->refcount--;
    if (file->refcount <= 0 && file->close) {
        file->close(file);
    }
}

// --- path helpers ---

static bool resolve_host_path(System* sys, const char* rel, char* out, size_t outlen) {
    if (!rel || !out || outlen == 0) return false;
    const char* root = (sys && sys->sandbox_root[0]) ? sys->sandbox_root : ".";

    if (rel[0] == '/') rel++;
    if (strstr(rel, "..") != NULL) return false;

    int n = snprintf(out, outlen, "%s/%s", root, rel);
    return n > 0 && (size_t)n < outlen;
}

static VFSFile* lookup_mount(System* sys, const char* path) {
    if (!sys) return NULL;
    for (int i = 0; i < sys->vfs.mount_count; i++) {
        if (strcmp(sys->vfs.mounts[i].path, path) == 0) {
            return sys->vfs.mounts[i].file;
        }
    }
    return NULL;
}

static int alloc_fd(System* sys, VFSFile* file) {
    if (!sys) return -1;
    for (int i = VFS_FD_BASE; i < VFS_MAX_FDS; i++) {
        if (sys->vfs.fd_table[i] == NULL) {
            file->fd = i;
            sys->vfs.fd_table[i] = file;
            vfs_file_retain(file);
            return i;
        }
    }
    return -1;
}

// --- host file ---

typedef struct {
    FILE* fp;
    char host_path[512];
} HostFileData;

static int host_read(VFSFile* file, uint8_t* buf, int len) {
    HostFileData* h = (HostFileData*)file->private_data;
    if (!h || !h->fp) return -1;
    size_t n = fread(buf, 1, (size_t)len, h->fp);
    return (int)n;
}

static int host_write(VFSFile* file, const uint8_t* buf, int len) {
    HostFileData* h = (HostFileData*)file->private_data;
    if (!h || !h->fp) return -1;
    size_t n = fwrite(buf, 1, (size_t)len, h->fp);
    return (int)n;
}

static int host_close(VFSFile* file) {
    HostFileData* h = (HostFileData*)file->private_data;
    if (h) {
        if (h->fp) fclose(h->fp);
        free(h);
    }
    free(file);
    return 0;
}

static int64_t host_seek(VFSFile* file, int64_t offset) {
    HostFileData* h = (HostFileData*)file->private_data;
    if (!h || !h->fp) return -1;
    if (fseek(h->fp, (long)offset, SEEK_SET) != 0) return -1;
    return offset;
}

static int64_t host_stat(VFSFile* file) {
    HostFileData* h = (HostFileData*)file->private_data;
    if (!h || !h->fp) return -1;
    long saved = ftell(h->fp);
    if (fseek(h->fp, 0, SEEK_END) != 0) return -1;
    long sz = ftell(h->fp);
    fseek(h->fp, saved, SEEK_SET);
    return (int64_t)sz;
}

static VFSFile* create_host_file(const char* path, int32_t flags) {
    bool want_write = (flags & 0x01) || (flags & 0x02);
    bool want_truncate = (flags & 0x04) != 0;
    const char* mode;
    if (want_truncate) {
        mode = (flags & 0x02) ? "w+b" : "wb";
    } else if (flags & 0x02) {
        mode = "r+b";
    } else if (want_write) {
        mode = "r+b";
    } else {
        mode = "rb";
    }

    FILE* f = fopen(path, mode);
    if (!f) return NULL;

    HostFileData* h = (HostFileData*)calloc(1, sizeof(HostFileData));
    VFSFile* file = (VFSFile*)calloc(1, sizeof(VFSFile));
    if (!h || !file) {
        if (f) fclose(f);
        free(h);
        free(file);
        return NULL;
    }
    h->fp = f;
    strncpy(h->host_path, path, sizeof(h->host_path) - 1);
    file->private_data = h;
    file->read = host_read;
    file->write = host_write;
    file->close = host_close;
    file->seek = host_seek;
    file->stat_size = host_stat;
    return file;
}

// --- channel ---

typedef struct {
    uint8_t* data;
    int len;
} ChanMsg;

typedef struct {
    ChanMsg queue[64];
    int head;
    int tail;
    bool closed;
} ChanEndpoint;

// Both endpoints of a pair share one block; it is freed when the last
// endpoint file is destroyed.
typedef struct {
    ChanEndpoint a;
    ChanEndpoint b;
    int alive; // endpoint files not yet destroyed
} ChanShared;

typedef struct {
    ChanShared* shared;
    ChanEndpoint* self;
    ChanEndpoint* peer;
} ChanEpRef;

static void chan_push(ChanEndpoint* ep, const uint8_t* buf, int len) {
    if (!ep || ep->closed) return;
    int next = (ep->tail + 1) % 64;
    if (next == ep->head) return;
    ChanMsg* m = &ep->queue[ep->tail];
    free(m->data);
    m->data = (uint8_t*)malloc((size_t)len);
    if (!m->data) return;
    memcpy(m->data, buf, (size_t)len);
    m->len = len;
    ep->tail = next;
}

static int chan_pop(ChanEndpoint* ep, uint8_t* buf, int len) {
    if (!ep || ep->closed || ep->head == ep->tail) return 0;
    ChanMsg* m = &ep->queue[ep->head];
    int n = m->len;
    if (n > len) n = len;
    memcpy(buf, m->data, (size_t)n);
    free(m->data);
    m->data = NULL;
    m->len = 0;
    ep->head = (ep->head + 1) % 64;
    return n;
}

static int chan_read(VFSFile* file, uint8_t* buf, int len) {
    ChanEpRef* ref = (ChanEpRef*)file->private_data;
    if (!ref || !ref->self) return 0;
    return chan_pop(ref->self, buf, len);
}

static int chan_write(VFSFile* file, const uint8_t* buf, int len) {
    ChanEpRef* ref = (ChanEpRef*)file->private_data;
    if (!ref || !ref->peer) return 0;
    chan_push(ref->peer, buf, len);
    return len;
}

static void chan_drain(ChanEndpoint* ep) {
    while (ep->head != ep->tail) {
        free(ep->queue[ep->head].data);
        ep->queue[ep->head].data = NULL;
        ep->head = (ep->head + 1) % 64;
    }
}

static int chan_close(VFSFile* file) {
    ChanEpRef* ref = (ChanEpRef*)file->private_data;
    if (ref) {
        if (ref->self) {
            ref->self->closed = true;
            chan_drain(ref->self);
        }
        if (ref->shared && --ref->shared->alive <= 0) {
            chan_drain(&ref->shared->a);
            chan_drain(&ref->shared->b);
            free(ref->shared);
        }
        free(ref);
    }
    free(file);
    return 0;
}

static VFSFile* create_chan_endpoint(ChanShared* shared, ChanEndpoint* self, ChanEndpoint* peer) {
    ChanEpRef* ref = (ChanEpRef*)calloc(1, sizeof(ChanEpRef));
    VFSFile* file = (VFSFile*)calloc(1, sizeof(VFSFile));
    if (!ref || !file) {
        free(ref);
        free(file);
        return NULL;
    }
    ref->shared = shared;
    ref->self = self;
    ref->peer = peer;
    shared->alive++;
    file->private_data = ref;
    file->read = chan_read;
    file->write = chan_write;
    file->close = chan_close;
    return file;
}

// --- snarf ---

typedef struct {
    System* sys;
    int cursor;
} SnarfFileData;

static int snarf_read(VFSFile* file, uint8_t* buf, int len) {
    SnarfFileData* s = (SnarfFileData*)file->private_data;
    if (!s || !s->sys) return 0;
    if (s->cursor >= s->sys->snarf_len) return 0;
    int avail = s->sys->snarf_len - s->cursor;
    if (avail > len) avail = len;
    memcpy(buf, s->sys->snarf_buf + s->cursor, (size_t)avail);
    s->cursor += avail;
    return avail;
}

static int snarf_write(VFSFile* file, const uint8_t* buf, int len) {
    SnarfFileData* s = (SnarfFileData*)file->private_data;
    if (!s || !s->sys) return -1;
    System* sys = s->sys;
    if (len > SYS_SNARF_MAX) len = SYS_SNARF_MAX;
    if (!sys->snarf_buf || sys->snarf_cap < len) {
        uint8_t* nb = (uint8_t*)realloc(sys->snarf_buf, (size_t)len);
        if (!nb) return -1;
        sys->snarf_buf = nb;
        sys->snarf_cap = len;
    }
    memcpy(sys->snarf_buf, buf, (size_t)len);
    sys->snarf_len = len;
    return len;
}

static int snarf_close(VFSFile* file) {
    free(file->private_data);
    free(file);
    return 0;
}

static VFSFile* create_snarf_file(System* sys) {
    SnarfFileData* d = (SnarfFileData*)calloc(1, sizeof(SnarfFileData));
    VFSFile* file = (VFSFile*)calloc(1, sizeof(VFSFile));
    if (!d || !file) {
        free(d);
        free(file);
        return NULL;
    }
    d->sys = sys;
    file->private_data = d;
    file->read = snarf_read;
    file->write = snarf_write;
    file->close = snarf_close;
    return file;
}

// --- launch ---
// Guest writes a ROM path; Cloister reads System.launch_path on HALT.

typedef struct {
    System* sys;
    int cursor;
} LaunchFileData;

static bool launch_path_ok(const char* p) {
    if (!p || !p[0]) return false;
    if (p[0] == '/') return false;
    if (strstr(p, "..") != NULL) return false;
    return true;
}

static int launch_read(VFSFile* file, uint8_t* buf, int len) {
    LaunchFileData* d = (LaunchFileData*)file->private_data;
    if (!d || !d->sys || !buf || len <= 0) return 0;
    int slen = (int)strlen(d->sys->launch_path);
    if (d->cursor >= slen) return 0;
    int avail = slen - d->cursor;
    if (avail > len) avail = len;
    memcpy(buf, d->sys->launch_path + d->cursor, (size_t)avail);
    d->cursor += avail;
    return avail;
}

static int launch_write(VFSFile* file, const uint8_t* buf, int len) {
    LaunchFileData* d = (LaunchFileData*)file->private_data;
    if (!d || !d->sys) return -1;
    if (len < 0) return -1;
    if (len >= SYS_LAUNCH_MAX) len = SYS_LAUNCH_MAX - 1;
    char tmp[SYS_LAUNCH_MAX];
    if (len > 0 && buf) memcpy(tmp, buf, (size_t)len);
    tmp[len] = '\0';
    while (len > 0 && (tmp[len - 1] == '\n' || tmp[len - 1] == '\r' || tmp[len - 1] == '\0')) {
        tmp[--len] = '\0';
    }
    if (len == 0) {
        d->sys->launch_path[0] = '\0';
        d->cursor = 0;
        return 0;
    }
    if (!launch_path_ok(tmp)) return -1;
    memcpy(d->sys->launch_path, tmp, (size_t)len + 1);
    d->cursor = 0;
    return len;
}

static int launch_close(VFSFile* file) {
    free(file->private_data);
    free(file);
    return 0;
}

static VFSFile* create_launch_file(System* sys) {
    LaunchFileData* d = (LaunchFileData*)calloc(1, sizeof(LaunchFileData));
    VFSFile* file = (VFSFile*)calloc(1, sizeof(VFSFile));
    if (!d || !file) {
        free(d);
        free(file);
        return NULL;
    }
    d->sys = sys;
    file->private_data = d;
    file->read = launch_read;
    file->write = launch_write;
    file->close = launch_close;
    return file;
}

// --- debug ---

static int debug_write(VFSFile* file, const uint8_t* buf, int len) {
    (void)file;
    fwrite(buf, 1, (size_t)len, stderr);
    return len;
}

static int generic_eof_read(VFSFile* file, uint8_t* buf, int len) {
    (void)file; (void)buf; (void)len;
    return 0;
}

static int generic_close(VFSFile* file) {
    free(file->private_data);
    free(file);
    return 0;
}

static VFSFile* create_debug_file(void) {
    VFSFile* file = (VFSFile*)calloc(1, sizeof(VFSFile));
    if (!file) return NULL;
    file->read = generic_eof_read;
    file->write = debug_write;
    file->close = generic_close;
    return file;
}

// --- kbd / mouse ---

typedef struct {
    System* sys;
} InputFileData;

static int kbd_read(VFSFile* file, uint8_t* buf, int len) {
    InputFileData* d = (InputFileData*)file->private_data;
    if (!d || !d->sys || len < 4) return -1;
    System* sys = d->sys;
    if (sys->kbd_head == sys->kbd_tail) {
        sys->yielded = true;
        return 0;
    }
    SysInputEvent* e = &sys->kbd_queue[sys->kbd_head];
    sys->kbd_head = (sys->kbd_head + 1) % SYS_INPUT_QUEUE_SZ;
    buf[0] = e->type;
    buf[1] = 0;
    buf[2] = (uint8_t)(e->x_or_key & 0xFF);
    buf[3] = (uint8_t)((e->x_or_key >> 8) & 0xFF);
    if (len >= 8) {
        buf[4] = (uint8_t)(e->modifiers & 0xFF);
        buf[5] = (uint8_t)((e->modifiers >> 8) & 0xFF);
        buf[6] = (uint8_t)((e->modifiers >> 16) & 0xFF);
        buf[7] = (uint8_t)((e->modifiers >> 24) & 0xFF);
        return 8;
    }
    return 4;
}

static int mouse_read(VFSFile* file, uint8_t* buf, int len) {
    InputFileData* d = (InputFileData*)file->private_data;
    if (!d || !d->sys || len < 8) return -1;
    System* sys = d->sys;
    if (sys->mouse_head == sys->mouse_tail) {
        sys->yielded = true;
        return 0;
    }
    SysInputEvent* e = &sys->mouse_queue[sys->mouse_head];
    sys->mouse_head = (sys->mouse_head + 1) % SYS_INPUT_QUEUE_SZ;
    buf[0] = e->type;
    buf[1] = e->btn;
    buf[2] = (uint8_t)(e->x_or_key & 0xFF);
    buf[3] = (uint8_t)((e->x_or_key >> 8) & 0xFF);
    buf[4] = (uint8_t)(e->y & 0xFF);
    buf[5] = (uint8_t)((e->y >> 8) & 0xFF);
    buf[6] = 0;
    buf[7] = 0;
    return 8;
}

static int input_write_fail(VFSFile* file, const uint8_t* buf, int len) {
    (void)file; (void)buf; (void)len;
    return 0;
}

static VFSFile* create_input_file(System* sys, bool is_kbd) {
    InputFileData* d = (InputFileData*)calloc(1, sizeof(InputFileData));
    VFSFile* file = (VFSFile*)calloc(1, sizeof(VFSFile));
    if (!d || !file) {
        free(d);
        free(file);
        return NULL;
    }
    d->sys = sys;
    file->private_data = d;
    file->read = is_kbd ? kbd_read : mouse_read;
    file->write = input_write_fail;
    file->close = generic_close;
    return file;
}

// --- dialog ---

typedef struct {
    System* sys;
} DialogFileData;

static int dialog_read(VFSFile* file, uint8_t* buf, int len) {
    DialogFileData* d = (DialogFileData*)file->private_data;
    if (!d || !d->sys || !d->sys->dialog_ready) return 0;
    int n = (int)strlen(d->sys->dialog_result);
    if (n >= len) n = len - 1;
    memcpy(buf, d->sys->dialog_result, (size_t)n);
    buf[n] = '\0';
    d->sys->dialog_ready = false;
    return n + 1;
}

static int dialog_write(VFSFile* file, const uint8_t* buf, int len) {
    DialogFileData* d = (DialogFileData*)file->private_data;
    if (!d || !d->sys) return 0;
    if (d->sys->open_file_dialog) {
        d->sys->open_file_dialog(d->sys->dialog_ctx);
    }
    (void)buf; (void)len;
    return len;
}

static VFSFile* create_dialog_file(System* sys) {
    DialogFileData* d = (DialogFileData*)calloc(1, sizeof(DialogFileData));
    VFSFile* file = (VFSFile*)calloc(1, sizeof(VFSFile));
    if (!d || !file) {
        free(d);
        free(file);
        return NULL;
    }
    d->sys = sys;
    file->private_data = d;
    file->read = dialog_read;
    file->write = dialog_write;
    file->close = generic_close;
    return file;
}

// --- dir ---

typedef struct {
    char* listing;
    int cursor;
} DirFileData;

static int dir_read(VFSFile* file, uint8_t* buf, int len) {
    DirFileData* d = (DirFileData*)file->private_data;
    if (!d || !d->listing) return 0;
    int slen = (int)strlen(d->listing);
    if (d->cursor >= slen) return 0;
    int avail = slen - d->cursor;
    if (avail > len) avail = len;
    memcpy(buf, d->listing + d->cursor, (size_t)avail);
    d->cursor += avail;
    return avail;
}

static int dir_close(VFSFile* file) {
    DirFileData* d = (DirFileData*)file->private_data;
    if (d) {
        free(d->listing);
        free(d);
    }
    free(file);
    return 0;
}

typedef struct {
    char name[256];
    int is_dir;
} DirListEnt;

static int dir_list_cmp(const void* a, const void* b) {
    const DirListEnt* ea = (const DirListEnt*)a;
    const DirListEnt* eb = (const DirListEnt*)b;
    int c = strcasecmp(ea->name, eb->name);
    if (c) return c;
    return strcmp(ea->name, eb->name);
}

static int dir_is_directory(const char* host, const struct dirent* ent) {
    if (ent->d_type == DT_DIR) return 1;
    if (ent->d_type == DT_REG) return 0;
    char full[1400];
    snprintf(full, sizeof(full), "%s/%s", host, ent->d_name);
    struct stat st;
    return stat(full, &st) == 0 && S_ISDIR(st.st_mode);
}

static VFSFile* create_dir_file(System* sys, const char* subpath) {
    char full[512];
    if (!resolve_host_path(sys, subpath, full, sizeof(full))) return NULL;

    DIR* dir = opendir(full);
    if (!dir) return NULL;

    DirListEnt* ents = NULL;
    int nent = 0, ecap = 0;
    struct dirent* ent;
    while ((ent = readdir(dir)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
        if (nent == ecap) {
            int ncap = ecap ? ecap * 2 : 32;
            DirListEnt* grown = (DirListEnt*)realloc(ents, (size_t)ncap * sizeof(DirListEnt));
            if (!grown) break;
            ents = grown;
            ecap = ncap;
        }
        strncpy(ents[nent].name, ent->d_name, sizeof(ents[nent].name) - 1);
        ents[nent].name[sizeof(ents[nent].name) - 1] = '\0';
        ents[nent].is_dir = dir_is_directory(full, ent);
        nent++;
    }
    closedir(dir);

    if (ents && nent > 1) {
        qsort(ents, (size_t)nent, sizeof(DirListEnt), dir_list_cmp);
    }

    size_t cap = 4096;
    size_t used = 0;
    char* listing = (char*)malloc(cap);
    if (!listing) {
        free(ents);
        return NULL;
    }
    listing[0] = '\0';

    for (int i = 0; i < nent; i++) {
        size_t n = strlen(ents[i].name);
        size_t extra = ents[i].is_dir ? 1 : 0;
        size_t need = used + n + extra + 2;
        if (need > cap) {
            cap *= 2;
            if (cap < need) cap = need;
            char* nb = (char*)realloc(listing, cap);
            if (!nb) break;
            listing = nb;
        }
        if (used > 0) {
            listing[used++] = '\n';
        }
        memcpy(listing + used, ents[i].name, n);
        used += n;
        if (ents[i].is_dir) listing[used++] = '/';
        listing[used] = '\0';
    }
    free(ents);

    DirFileData* d = (DirFileData*)calloc(1, sizeof(DirFileData));
    VFSFile* file = (VFSFile*)calloc(1, sizeof(VFSFile));
    if (!d || !file) {
        free(listing);
        free(d);
        free(file);
        return NULL;
    }
    d->listing = listing;
    file->private_data = d;
    file->read = dir_read;
    file->write = input_write_fail;
    file->close = dir_close;
    return file;
}

// --- vm ---

typedef struct {
    System* sys;
    int32_t vm_id;
    char kind[16];
    int32_t last_created_id;
    int32_t blit_dx, blit_dy, blit_dw, blit_dh;
    int blit_set;
} VmFileData;

static Machine* vm_child(VmFileData* d) {
    if (!d || !d->sys) return NULL;
    if (d->vm_id < 0 || d->vm_id >= SYS_MAX_CHILD_VMS) return NULL;
    return d->sys->child_vms[d->vm_id];
}

// Copy the child's presented framebuffer onto the parent's back buffer
// (the buffer Shell is composing this frame). Used by /sys/vm/<id>/fb.
// Optional dest rect from a prior 8-byte write: dx dy dw dh (LE i16).
static int vm_blit_fb(VmFileData* d) {
    Machine* ch = vm_child(d);
    if (!ch || !ch->system) return 0;

    System* parent = d->sys;
    System* child = ch->system;
    uint8_t* src = child->screen_pixels;
    uint8_t* dst = parent->back_pixels ? parent->back_pixels : parent->screen_pixels;
    if (!src || !dst) return 0;

    int32_t pw = parent->screen_width > 0 ? parent->screen_width : 960;
    int32_t ph = parent->screen_height > 0 ? parent->screen_height : 720;
    int32_t cw = child->screen_width > 0 ? child->screen_width : 960;
    int32_t chh = child->screen_height > 0 ? child->screen_height : 720;

    int32_t dx = 0, dy = 0, dw = pw, dh = ph;
    if (d->blit_set) {
        dx = d->blit_dx;
        dy = d->blit_dy;
        dw = d->blit_dw;
        dh = d->blit_dh;
    }
    int32_t sx = 0, sy = 0;
    if (dx < 0) { sx = -dx; dw += dx; dx = 0; }
    if (dy < 0) { sy = -dy; dh += dy; dy = 0; }
    if (dw <= 0 || dh <= 0) return 1;
    if (dx >= pw || dy >= ph || sx >= cw || sy >= chh) return 1;
    if (dx + dw > pw) dw = pw - dx;
    if (dy + dh > ph) dh = ph - dy;
    if (sx + dw > cw) dw = cw - sx;
    if (sy + dh > chh) dh = chh - sy;
    if (dw <= 0 || dh <= 0) return 1;

    for (int32_t y = 0; y < dh; y++) {
        memcpy(dst + ((size_t)(dy + y) * (size_t)pw + (size_t)dx) * 4,
               src + ((size_t)(sy + y) * (size_t)cw + (size_t)sx) * 4,
               (size_t)dw * 4);
    }
    return 1;
}

static int16_t rd_i16le(const uint8_t* p) {
    return (int16_t)(p[0] | (p[1] << 8));
}

static void wr_i16le(uint8_t* p, int32_t v) {
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
}

static int vm_read(VFSFile* file, uint8_t* buf, int len) {
    VmFileData* d = (VmFileData*)file->private_data;
    if (!d || !buf || len <= 0) return 0;
    if (strcmp(d->kind, "fb") == 0) {
        if (!vm_blit_fb(d)) return 0;
        buf[0] = 1;
        return 1;
    }
    if (strcmp(d->kind, "geom") == 0) {
        if (len < 4) return 0;
        int32_t w = 0, h = 0;
        Machine* ch = vm_child(d);
        if (ch && ch->system) {
            w = ch->system->screen_width;
            h = ch->system->screen_height;
        }
        wr_i16le(buf, w);
        wr_i16le(buf + 2, h);
        if (len >= 8) {
            buf[4] = buf[5] = buf[6] = buf[7] = 0;
            return 8;
        }
        return 4;
    }
    if (len < 4) return 0;
    if (strcmp(d->kind, "new") == 0) {
        buf[0] = (uint8_t)(d->last_created_id & 0xFF);
        buf[1] = (uint8_t)((d->last_created_id >> 8) & 0xFF);
        buf[2] = (uint8_t)((d->last_created_id >> 16) & 0xFF);
        buf[3] = (uint8_t)((d->last_created_id >> 24) & 0xFF);
        d->last_created_id = 0;
        return 4;
    }
    if (strcmp(d->kind, "status") == 0) {
        int32_t halted = 1;
        Machine* chm = vm_child(d);
        if (chm && chm->cpu && !chm->cpu->halted) halted = 0;
        buf[0] = (uint8_t)(halted & 0xFF);
        buf[1] = 0;
        buf[2] = 0;
        buf[3] = 0;
        return 4;
    }
    return 0;
}

static int vm_write_fb(VmFileData* d, const uint8_t* buf, int len) {
    if (len < 8) return 0;
    d->blit_dx = rd_i16le(buf);
    d->blit_dy = rd_i16le(buf + 2);
    d->blit_dw = rd_i16le(buf + 4);
    d->blit_dh = rd_i16le(buf + 6);
    d->blit_set = 1;
    return 8;
}

static int vm_write_geom(VmFileData* d, const uint8_t* buf, int len) {
    if (len < 4) return 0;
    Machine* ch = vm_child(d);
    if (!ch || !ch->system) return 0;
    int32_t w = rd_i16le(buf);
    int32_t h = rd_i16le(buf + 2);
    int32_t pw = d->sys->screen_width > 0 ? d->sys->screen_width : 960;
    int32_t ph = d->sys->screen_height > 0 ? d->sys->screen_height : 720;
    if (w < 16) w = 16;
    if (h < 16) h = 16;
    if (w > pw) w = pw;
    if (h > ph) h = ph;
    system_set_resolution(ch->system, w, h);
    return len >= 8 ? 8 : 4;
}

static int vm_write_status(VmFileData* d, int len) {
    Machine* ch = vm_child(d);
    if (ch && ch->cpu) ch->cpu->halted = true;
    return len > 0 ? len : 1;
}

static int vm_write(VFSFile* file, const uint8_t* buf, int len) {
    VmFileData* d = (VmFileData*)file->private_data;
    if (!d || !d->sys || !buf || len <= 0) return 0;
    if (strcmp(d->kind, "fb") == 0) return vm_write_fb(d, buf, len);
    if (strcmp(d->kind, "geom") == 0) return vm_write_geom(d, buf, len);
    if (strcmp(d->kind, "status") == 0) return vm_write_status(d, len);
    if (strcmp(d->kind, "new") != 0) return 0;

    char path[512];
    int plen = len;
    if (plen >= (int)sizeof(path)) plen = (int)sizeof(path) - 1;
    memcpy(path, buf, (size_t)plen);
    path[plen] = '\0';
    for (int i = plen - 1; i >= 0; i--) {
        if (path[i] == '\n' || path[i] == '\r' || path[i] == ' ') path[i] = '\0';
        else break;
    }

    uint8_t* program = NULL;
    size_t prog_len = 0;
    bool prog_owned = false;

    if (strstr(path, ".lux")) {
        FILE* f = fopen(path, "rb");
        if (!f) return 0;
        fseek(f, 0, SEEK_END);
        long fs = ftell(f);
        fseek(f, 0, SEEK_SET);
        char* src = (char*)malloc((size_t)fs + 1);
        if (!src) { fclose(f); return 0; }
        fread(src, 1, (size_t)fs, f);
        src[fs] = '\0';
        fclose(f);
        // Children launched by Shell (and similar) are graphical apps; match
        // cloister's GRAPHICAL_BASE_ADDRESS so absolute CALL/JMP targets work.
        program = compile_source(src, GRAPHICAL_BASE_ADDRESS, &prog_len, false);
        free(src);
        prog_owned = true;
    } else {
        FILE* f = fopen(path, "rb");
        if (!f) return 0;
        fseek(f, 0, SEEK_END);
        long fs = ftell(f);
        fseek(f, 0, SEEK_SET);
        program = (uint8_t*)malloc((size_t)fs);
        if (!program) { fclose(f); return 0; }
        fread(program, 1, (size_t)fs, f);
        fclose(f);
        prog_len = (size_t)fs;
        prog_owned = true;
    }

    if (!program) return 0;

    int32_t id = d->sys->next_vm_id++;
    if (id >= 0 && id < SYS_MAX_CHILD_VMS) {
        Machine* child = machine_create(program, (uint32_t)prog_len, GRAPHICAL_BASE_ADDRESS, 32 * 1024 * 1024, false);
        if (child) {
            if (child->system) {
                // Children inherit the parent's sandbox, sound, and canvas size
                // so /dev/draw and rio blit share the same 960×720 (or whatever
                // cloister set on the root).
                memcpy(child->system->sandbox_root, d->sys->sandbox_root,
                       sizeof(child->system->sandbox_root));
                child->system->play_sound = d->sys->play_sound;
                int32_t rw = d->sys->screen_width > 0 ? d->sys->screen_width : 960;
                int32_t rh = d->sys->screen_height > 0 ? d->sys->screen_height : 720;
                system_set_resolution(child->system, rw, rh);
            }
            d->sys->child_vms[id] = child;
            d->last_created_id = id;
        }
    }

    if (prog_owned) free(program);
    return len;
}

static VFSFile* create_vm_file(System* sys, const char* kind, int32_t vm_id) {
    VmFileData* d = (VmFileData*)calloc(1, sizeof(VmFileData));
    VFSFile* file = (VFSFile*)calloc(1, sizeof(VFSFile));
    if (!d || !file) {
        free(d);
        free(file);
        return NULL;
    }
    d->sys = sys;
    d->vm_id = vm_id;
    strncpy(d->kind, kind, sizeof(d->kind) - 1);
    file->private_data = d;
    file->read = vm_read;
    file->write = vm_write;
    file->close = generic_close;
    return file;
}

// --- draw / audio / font (existing logic) ---

static int draw_read(VFSFile* file, uint8_t* buf, int len) {
    System* sys = (System*)file->private_data;
    if (!sys || !buf || len < 4) return 0;
    int32_t w = sys->screen_width > 0 ? sys->screen_width : 960;
    int32_t h = sys->screen_height > 0 ? sys->screen_height : 720;
    buf[0] = (uint8_t)(w & 0xFF);
    buf[1] = (uint8_t)((w >> 8) & 0xFF);
    buf[2] = (uint8_t)(h & 0xFF);
    buf[3] = (uint8_t)((h >> 8) & 0xFF);
    return 4;
}

static int draw_write(VFSFile* file, const uint8_t* buf, int len) {
    System* sys = (System*)file->private_data;
    if (!sys || len <= 0) return len;

    int i = 0;
    while (i < len) {
        uint8_t cmd = buf[i++];
        switch (cmd) {
            case 0: {
                if (i + 12 > len) return i - 1;
                int16_t x  = (int16_t)(buf[i]   | (buf[i+1]<<8));
                int16_t y  = (int16_t)(buf[i+2] | (buf[i+3]<<8));
                int16_t w  = (int16_t)(buf[i+4] | (buf[i+5]<<8));
                int16_t h  = (int16_t)(buf[i+6] | (buf[i+7]<<8));
                uint32_t color = (uint32_t)buf[i+8] | ((uint32_t)buf[i+9]<<8) |
                                 ((uint32_t)buf[i+10]<<16) | ((uint32_t)buf[i+11]<<24);
                system_fill_rect(sys, x, y, w, h, color);
                i += 12;
                break;
            }
            case 1: {
                if (i + 10 > len) return i - 1;
                int16_t x = (int16_t)(buf[i] | (buf[i+1]<<8));
                int16_t y = (int16_t)(buf[i+2] | (buf[i+3]<<8));
                char c = (char)buf[i+4];
                uint32_t color = (uint32_t)buf[i+5] | ((uint32_t)buf[i+6]<<8) |
                                 ((uint32_t)buf[i+7]<<16) | ((uint32_t)buf[i+8]<<24);
                uint8_t scale = buf[i+9];
                if (scale == 0) scale = sys->font_size ? sys->font_size : 12;
                system_draw_char(sys, x, y, c, color, scale);
                i += 10;
                break;
            }
            case 2: {
                if (i + 11 > len) return i - 1;
                int16_t x = (int16_t)(buf[i] | (buf[i+1]<<8));
                int16_t y = (int16_t)(buf[i+2] | (buf[i+3]<<8));
                uint32_t color = (uint32_t)buf[i+4] | ((uint32_t)buf[i+5]<<8) |
                                 ((uint32_t)buf[i+6]<<16) | ((uint32_t)buf[i+7]<<24);
                uint8_t scale = buf[i+8];
                if (scale == 0) scale = sys->font_size ? sys->font_size : 12;
                int16_t strLen = (int16_t)(buf[i+9] | (buf[i+10]<<8));
                i += 11;
                if (i + strLen > len) return i - 11;
                system_draw_text_len(sys, x, y, (const char*)(buf + i), strLen, color, scale);
                i += strLen;
                break;
            }
            case 3: {
                if (i + 12 > len) return i - 1;
                int16_t x  = (int16_t)(buf[i]   | (buf[i+1]<<8));
                int16_t y  = (int16_t)(buf[i+2] | (buf[i+3]<<8));
                int16_t w  = (int16_t)(buf[i+4] | (buf[i+5]<<8));
                int16_t h  = (int16_t)(buf[i+6] | (buf[i+7]<<8));
                uint32_t color = (uint32_t)buf[i+8] | ((uint32_t)buf[i+9]<<8) |
                                 ((uint32_t)buf[i+10]<<16) | ((uint32_t)buf[i+11]<<24);
                system_draw_rect(sys, x, y, w, h, color);
                i += 12;
                break;
            }
            case 4:
                if (i + 1 > len) return i - 1;
                sys->font_size = buf[i++];
                break;
            case 5:
                if (i + 1 > len) return i - 1;
                sys->font_id = buf[i++];
                break;
            case 6:
                system_begin_frame(sys);
                break;
            case 8: {
                if (i + 13 > len) return i - 1;
                int16_t x  = (int16_t)(buf[i]   | (buf[i+1]<<8));
                int16_t y  = (int16_t)(buf[i+2] | (buf[i+3]<<8));
                int16_t w  = (int16_t)(buf[i+4] | (buf[i+5]<<8));
                int16_t h  = (int16_t)(buf[i+6] | (buf[i+7]<<8));
                uint32_t color = (uint32_t)buf[i+8] | ((uint32_t)buf[i+9]<<8) |
                                 ((uint32_t)buf[i+10]<<16) | ((uint32_t)buf[i+11]<<24);
                uint8_t pat = buf[i+12];
                system_fill_pat(sys, x, y, w, h, color, pat);
                i += 13;
                break;
            }
            case 7:
                system_end_frame(sys);
                break;
            case 9: {
                /* DrawCFFGlyph: x i16, y i16, color u32, scale u8, ch u8,
                   font_ptr u32, nbytes u16. 17 bytes including cmd. */
                if (i + 16 > len) return i - 1;
                int16_t x = (int16_t)(buf[i] | (buf[i+1] << 8));
                int16_t y = (int16_t)(buf[i+2] | (buf[i+3] << 8));
                uint32_t color = (uint32_t)buf[i+4] | ((uint32_t)buf[i+5] << 8) |
                                 ((uint32_t)buf[i+6] << 16) | ((uint32_t)buf[i+7] << 24);
                uint8_t scale = buf[i+8];
                uint8_t ch = buf[i+9];
                uint32_t font_ptr = (uint32_t)buf[i+10] | ((uint32_t)buf[i+11] << 8) |
                                    ((uint32_t)buf[i+12] << 16) | ((uint32_t)buf[i+13] << 24);
                uint16_t nbytes = (uint16_t)(buf[i+14] | (buf[i+15] << 8));
                i += 16;
                if (!sys->memory || font_ptr >= sys->memory_size) break;
                uint32_t avail = sys->memory_size - font_ptr;
                if ((uint32_t)nbytes > avail) nbytes = (uint16_t)avail;
                if (nbytes == 0) break;
                system_draw_cff(sys, sys->memory + font_ptr, (int)nbytes,
                                (char)ch, x, y, color, (int)scale);
                break;
            }
            default:
                return i - 1;
        }
    }
    return len;
}

static int draw_close(VFSFile* file) {
    free(file);
    return 0;
}

static VFSFile* create_draw_file(void* system_ctx) {
    VFSFile* file = (VFSFile*)calloc(1, sizeof(VFSFile));
    if (!file) return NULL;
    file->private_data = system_ctx;
    file->read = draw_read;
    file->write = draw_write;
    file->close = draw_close;
    return file;
}

static int audio_write(VFSFile* file, const uint8_t* buf, int len) {
    System* sys = (System*)file->private_data;
    if (len < 4) return 0;
    if (sys && sys->play_sound) {
        int32_t sound_id = (int32_t)(buf[0] | (buf[1]<<8) | (buf[2]<<16) | (buf[3]<<24));
        sys->play_sound(sound_id);
    }
    return 4;
}

static int audio_close(VFSFile* file) {
    // private_data is the System, not owned by this file
    free(file);
    return 0;
}

static VFSFile* create_audio_file(System* sys) {
    VFSFile* file = (VFSFile*)calloc(1, sizeof(VFSFile));
    if (!file) return NULL;
    file->private_data = sys;
    file->read = generic_eof_read;
    file->write = audio_write;
    file->close = audio_close;
    return file;
}

typedef struct {
    const uint8_t* data;
    int len;
    long offset;
} FontBlob;

static int font_blob_read(VFSFile* file, uint8_t* buf, int len) {
    FontBlob* b = (FontBlob*)file->private_data;
    if (!b || !b->data || b->offset >= b->len) return 0;
    long to_copy = b->len - b->offset;
    if (to_copy > len) to_copy = len;
    memcpy(buf, b->data + b->offset, (size_t)to_copy);
    b->offset += to_copy;
    return (int)to_copy;
}

static int64_t font_blob_seek(VFSFile* file, int64_t offset) {
    FontBlob* b = (FontBlob*)file->private_data;
    if (!b) return -1;
    if (offset < 0) offset = 0;
    if (offset > b->len) offset = b->len;
    b->offset = offset;
    return b->offset;
}

static int64_t font_blob_stat(VFSFile* file) {
    FontBlob* b = (FontBlob*)file->private_data;
    return b ? b->len : -1;
}

static int font_blob_close(VFSFile* file) {
    free(file->private_data);
    free(file);
    return 0;
}

static VFSFile* create_font_blob_file(const uint8_t* data, int len) {
    if (!data || len <= 0) return NULL;
    VFSFile* file = (VFSFile*)calloc(1, sizeof(VFSFile));
    FontBlob* b = (FontBlob*)calloc(1, sizeof(FontBlob));
    if (!file || !b) {
        free(file);
        free(b);
        return NULL;
    }
    b->data = data;
    b->len = len;
    file->private_data = b;
    file->read = font_blob_read;
    file->write = input_write_fail;
    file->close = font_blob_close;
    file->seek = font_blob_seek;
    file->stat_size = font_blob_stat;
    return file;
}

static VFSFile* create_font_widths_file(System* sys) {
    const uint8_t* data = system_font_data(sys);
    return create_font_blob_file(data, 256);
}

static VFSFile* create_named_font_file(int font_id) {
    return create_font_blob_file(system_font_data_id(font_id), system_font_nbytes_id(font_id));
}

static VFSFile* create_dummy_file(void) {
    VFSFile* file = (VFSFile*)calloc(1, sizeof(VFSFile));
    if (!file) return NULL;
    file->read = generic_eof_read;
    file->write = input_write_fail;
    file->close = generic_close;
    return file;
}

// --- open / bind ---

static VFSFile* open_path(System* sys, const char* path, int32_t flags) {
    // Mount table first: opening a bound path returns the same file object
    // (shared state), exactly as the Go VFS hands out the mounted VFSFile.
    VFSFile* mounted = lookup_mount(sys, path);
    if (mounted) {
        return mounted;
    }

    if (strcmp(path, "/sys/draw") == 0 || strcmp(path, "/dev/draw") == 0) {
        return create_draw_file(sys);
    }
    if (strncmp(path, "/sys/audio", 10) == 0 || strncmp(path, "/dev/audio", 10) == 0) {
        return create_audio_file(sys);
    }
    if (strcmp(path, "/sys/font/widths") == 0) {
        return create_font_widths_file(sys);
    }
    if (strcmp(path, "/sys/font/chicago") == 0) {
        return create_named_font_file(FONT_CHICAGO);
    }
    if (strcmp(path, "/sys/font/geneva") == 0) {
        return create_named_font_file(FONT_GENEVA);
    }
    if (strcmp(path, "/sys/font/monaco") == 0) {
        return create_named_font_file(FONT_MONACO);
    }
    if (strcmp(path, "/sys/snarf") == 0) {
        return create_snarf_file(sys);
    }
    if (strcmp(path, "/sys/launch") == 0 || strcmp(path, "/dev/launch") == 0) {
        return create_launch_file(sys);
    }
    if (strcmp(path, "/sys/debug") == 0) {
        return create_debug_file();
    }
    if (strcmp(path, "/sys/kbd") == 0 || strcmp(path, "/dev/kbd") == 0) {
        return create_input_file(sys, true);
    }
    if (strcmp(path, "/sys/mouse") == 0 || strcmp(path, "/dev/mouse") == 0) {
        return create_input_file(sys, false);
    }
    // /dev/menu is a bound channel from rio, not a dummy /sys file.
    // Unbound open must fail so a root app (cloister apps/UIDemo.lux) keeps
    // its local menubar and the Esc Continue/Restart/Quit overlay.
    if (strcmp(path, "/sys/menu") == 0 || strcmp(path, "/dev/menu") == 0) {
        return NULL;
    }
    if (strcmp(path, "/sys/dialog") == 0) {
        return create_dialog_file(sys);
    }
    if (strcmp(path, "/sys/chan/new") == 0) {
        ChanShared* shared = (ChanShared*)calloc(1, sizeof(ChanShared));
        if (!shared) return NULL;
        VFSFile* ep = create_chan_endpoint(shared, &shared->a, &shared->b);
        VFSFile* peer = create_chan_endpoint(shared, &shared->b, &shared->a);
        if (!ep || !peer) {
            if (ep) ep->close(ep);
            else if (peer) peer->close(peer);
            else free(shared);
            return NULL;
        }
        // Any previously unclaimed peer is orphaned; destroy it.
        if (sys && sys->vfs.last_chan_peer) {
            vfs_file_release(sys->vfs.last_chan_peer);
        }
        // The pending-peer slot holds a reference until claimed.
        vfs_file_retain(peer);
        if (sys) sys->vfs.last_chan_peer = peer;
        return ep;
    }
    if (strcmp(path, "/sys/chan/peer") == 0) {
        if (!sys || !sys->vfs.last_chan_peer) return NULL;
        VFSFile* ep = sys->vfs.last_chan_peer;
        sys->vfs.last_chan_peer = NULL;
        ep->refcount--; // transfer the pending-peer reference to the caller
        return ep;
    }
    if (strncmp(path, "/sys/vm/", 8) == 0) {
        const char* rest = path + 8;
        if (strcmp(rest, "new") == 0) {
            return create_vm_file(sys, "new", -1);
        }
        char idbuf[32] = {0};
        const char* slash = strchr(rest, '/');
        size_t idlen = slash ? (size_t)(slash - rest) : strlen(rest);
        if (idlen >= sizeof(idbuf)) return NULL;
        memcpy(idbuf, rest, idlen);
        int32_t vid = (int32_t)atoi(idbuf);
        const char* kind = "ctl";
        if (slash && slash[1]) kind = slash + 1;
        return create_vm_file(sys, kind, vid);
    }
    if (strcmp(path, "/sys/dir") == 0) {
        return create_dir_file(sys, "");
    }
    if (strncmp(path, "/sys/dir/", 9) == 0) {
        return create_dir_file(sys, path + 9);
    }
    if (strncmp(path, "/sys/file/", 10) == 0 || strncmp(path, "/dev/file/", 10) == 0) {
        char host[512];
        if (!resolve_host_path(sys, path + 10, host, sizeof(host))) return NULL;
        return create_host_file(host, flags);
    }
    if (strncmp(path, "/dev/", 5) == 0) {
        char mapped[256];
        snprintf(mapped, sizeof(mapped), "/sys/%s", path + 5);
        return open_path(sys, mapped, flags);
    }
    if (strncmp(path, "/sys/", 5) == 0) {
        return create_dummy_file();
    }

    return create_host_file(path, flags);
}

static VFSFile* get_fd(System* sys, int32_t fd) {
    if (!sys || fd < 0 || fd >= VFS_MAX_FDS) return NULL;
    return sys->vfs.fd_table[fd];
}

int32_t vfs_open(System* sys, const char* path, int32_t flags) {
    if (!sys || !path) return -1;
    VFSFile* file = open_path(sys, path, flags);
    if (!file) return -1;
    int fd = alloc_fd(sys, file);
    if (fd < 0) {
        // Fresh files (no other reference) are destroyed; mounted files live on.
        if (file->refcount <= 0 && file->close) file->close(file);
        return -1;
    }
    return fd;
}

int vfs_read(System* sys, int32_t fd, uint8_t* buf, int len) {
    VFSFile* file = get_fd(sys, fd);
    if (!file || !file->read) return -1;
    return file->read(file, buf, len);
}

int vfs_write(System* sys, int32_t fd, const uint8_t* buf, int len) {
    VFSFile* file = get_fd(sys, fd);
    if (!file || !file->write) return -1;
    return file->write(file, buf, len);
}

int vfs_close(System* sys, int32_t fd) {
    VFSFile* file = get_fd(sys, fd);
    if (!file) return -1;
    sys->vfs.fd_table[fd] = NULL;
    vfs_file_release(file);
    return 0;
}

int64_t vfs_seek(System* sys, int32_t fd, int64_t offset) {
    VFSFile* file = get_fd(sys, fd);
    if (!file || !file->seek) return -1;
    return file->seek(file, offset);
}

int64_t vfs_stat(System* sys, int32_t fd) {
    VFSFile* file = get_fd(sys, fd);
    if (!file || !file->stat_size) return -1;
    return file->stat_size(file);
}

static int vfs_bind_file(System* target, VFSFile* file, const char* mount_path) {
    if (!target || target->vfs.mount_count >= VFS_MAX_MOUNTS) return -1;
    VFSMount* m = &target->vfs.mounts[target->vfs.mount_count++];
    strncpy(m->path, mount_path, sizeof(m->path) - 1);
    m->path[sizeof(m->path) - 1] = '\0';
    m->file = file;
    vfs_file_retain(file);
    return 0;
}

int vfs_bind(System* sys, int32_t fd, const char* mount_path) {
    if (!mount_path) return -1;
    VFSFile* file = get_fd(sys, fd);
    if (!file) return -1;

    // Binding into a child namespace: /sys/vm/<id>/ns/<rest> mounts the file
    // at /<rest> inside that child's VFS (Go: VFS.BindFile).
    if (strncmp(mount_path, "/sys/vm/", 8) == 0) {
        char* end = NULL;
        long id = strtol(mount_path + 8, &end, 10);
        if (end && end != mount_path + 8 && strncmp(end, "/ns/", 4) == 0) {
            if (id < 0 || id >= SYS_MAX_CHILD_VMS || !sys->child_vms[id] ||
                !sys->child_vms[id]->system) {
                return -1;
            }
            char child_path[256];
            snprintf(child_path, sizeof(child_path), "/%s", end + 4);
            return vfs_bind_file(sys->child_vms[id]->system, file, child_path);
        }
    }

    return vfs_bind_file(sys, file, mount_path);
}

void vfs_state_free(System* sys) {
    if (!sys) return;
    for (int i = 0; i < VFS_MAX_FDS; i++) {
        if (sys->vfs.fd_table[i]) {
            VFSFile* f = sys->vfs.fd_table[i];
            sys->vfs.fd_table[i] = NULL;
            vfs_file_release(f);
        }
    }
    for (int i = 0; i < sys->vfs.mount_count; i++) {
        vfs_file_release(sys->vfs.mounts[i].file);
        sys->vfs.mounts[i].file = NULL;
    }
    sys->vfs.mount_count = 0;
    if (sys->vfs.last_chan_peer) {
        vfs_file_release(sys->vfs.last_chan_peer);
        sys->vfs.last_chan_peer = NULL;
    }
}