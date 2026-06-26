#ifndef VFS_H
#define VFS_H

#include <stdint.h>
#include <stdbool.h>

typedef struct VFSFile VFSFile;

struct VFSFile {
    int32_t fd;
    void* private_data;
    int (*read)(VFSFile* file, uint8_t* buf, int len);
    int (*write)(VFSFile* file, const uint8_t* buf, int len);
    int (*close)(VFSFile* file);
    int64_t (*seek)(VFSFile* file, int64_t offset);
    int64_t (*stat_size)(VFSFile* file);
};

void vfs_init(void);
void vfs_set_sound_handler(void (*fn)(int32_t sound_id));

int32_t vfs_open(const char* path, int32_t flags, void* system_ctx);
int vfs_read(int32_t fd, uint8_t* buf, int len);
int vfs_write(int32_t fd, const uint8_t* buf, int len);
int vfs_close(int32_t fd);
int64_t vfs_seek(int32_t fd, int64_t offset);
int64_t vfs_stat(int32_t fd);
int vfs_bind(int32_t fd, const char* mount_path, void* system_ctx);

#endif // VFS_H