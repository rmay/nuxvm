#ifndef DIALOG_H
#define DIALOG_H

#include <stdint.h>
#include <stdbool.h>
#include "system.h"

// In-process software file-open dialog, drawn over the app framebuffer.
// Port of the Go fileDialogModal (pkg/system/dialog.go): same geometry,
// same keyboard/mouse behavior, result delivered via
// system_set_dialog_result ("cancel" or a sandbox-relative path).

typedef struct {
    char name[256];
    bool is_dir;
} DialogEntry;

typedef struct FileDialog {
    bool active;
    System* sys;
    char path[512];          // sandbox-relative, starts at "."
    DialogEntry* entries;
    int count;
    int selected;
    int scroll;
    int last_click_idx;
    uint32_t last_click_ms;
} FileDialog;

void dialog_open(FileDialog* d, System* sys);
void dialog_key(FileDialog* d, int32_t code);      // translated keycodes: 17/18/13/27
void dialog_mouse_down(FileDialog* d, int32_t x, int32_t y);
void dialog_wheel(FileDialog* d, int32_t dy);
void dialog_draw(FileDialog* d);                   // draws into sys->screen_pixels
void dialog_free(FileDialog* d);

#endif // DIALOG_H
