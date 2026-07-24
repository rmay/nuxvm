#ifndef VFS_H
#define VFS_H

#include <stdint.h>
#include <stdbool.h>

// NOTE: vfs.h must not include system.h (System embeds VFSState).
typedef struct System System;

typedef struct VFSFile VFSFile;

struct VFSFile {
    int32_t fd;
    int refcount;   // fds + mounts holding this file; destroyed at zero
    void* private_data;
    int (*read)(VFSFile* file, uint8_t* buf, int len);
    int (*write)(VFSFile* file, const uint8_t* buf, int len);
    int (*close)(VFSFile* file);
    int64_t (*seek)(VFSFile* file, int64_t offset);
    int64_t (*stat_size)(VFSFile* file);
};

#define VFS_MAX_FDS    1024
#define VFS_FD_BASE    100
#define VFS_MAX_MOUNTS 32

typedef struct {
    char path[256];
    VFSFile* file;
} VFSMount;

// Per-System VFS state (fd table + per-process mount namespace).
// An all-zero VFSState is valid and empty, so calloc'd Systems need no init.
typedef struct VFSState {
    VFSFile* fd_table[VFS_MAX_FDS];
    VFSMount mounts[VFS_MAX_MOUNTS];
    int mount_count;
    VFSFile* last_chan_peer;
} VFSState;

int32_t vfs_open(System* sys, const char* path, int32_t flags);
int vfs_read(System* sys, int32_t fd, uint8_t* buf, int len);
int vfs_write(System* sys, int32_t fd, const uint8_t* buf, int len);
int vfs_close(System* sys, int32_t fd);
int64_t vfs_seek(System* sys, int32_t fd, int64_t offset);
int64_t vfs_stat(System* sys, int32_t fd);
int vfs_bind(System* sys, int32_t fd, const char* mount_path);

// Release every open fd and mount owned by sys (called from system_free).
void vfs_state_free(System* sys);

#endif // VFS_H
