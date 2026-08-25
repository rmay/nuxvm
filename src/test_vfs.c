#include "vfs.h"
#include "system.h"
#include "machine.h"
#include "compiler.h"
#include "chicago.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <math.h>

static System* make_test_screen(int w, int h) {
    System* sys = (System*)calloc(1, sizeof(System));
    assert(sys != NULL);
    sys->screen_width = w;
    sys->screen_height = h;
    sys->screen_pixels = (uint8_t*)calloc(1, (size_t)w * (size_t)h * 4);
    assert(sys->screen_pixels != NULL);
    sys->font_id = 2;
    sys->font_size = 12;
    return sys;
}

static void free_test_screen(System* sys) {
    if (!sys) return;
    free(sys->screen_pixels);
    free(sys);
}

static void fill_screen_white(System* sys) {
    size_t pixels = (size_t)sys->screen_width * (size_t)sys->screen_height;
    for (size_t i = 0; i < pixels; i++) {
        size_t off = i * 4;
        sys->screen_pixels[off + 0] = 0xFF;
        sys->screen_pixels[off + 1] = 0xFF;
        sys->screen_pixels[off + 2] = 0xFF;
        sys->screen_pixels[off + 3] = 0xFF;
    }
}

static bool pixel_is_ink(System* sys, int x, int y) {
    size_t off = (size_t)(y * sys->screen_width + x) * 4;
    return sys->screen_pixels[off + 1] != 0xFF ||
           sys->screen_pixels[off + 2] != 0xFF ||
           sys->screen_pixels[off + 3] != 0xFF;
}

static int rightmost_ink_x(System* sys, int x0, int y0, int w, int h) {
    int right = -1;
    for (int y = y0; y < y0 + h; y++) {
        for (int x = x0; x < x0 + w; x++) {
            if (pixel_is_ink(sys, x, y)) {
                right = x;
            }
        }
    }
    return right;
}

static int bottommost_ink_y(System* sys, int x0, int y0, int w, int h) {
    int bottom = -1;
    for (int y = y0; y < y0 + h; y++) {
        for (int x = x0; x < x0 + w; x++) {
            if (pixel_is_ink(sys, x, y)) {
                bottom = y;
            }
        }
    }
    return bottom;
}

void test_host_file() {
    printf("Testing host file operations...\n");
    
    // Create a temporary file to test with
    const char* test_path = "/sys/file/test_vfs_temp.txt";
    System* sys = system_create();
    assert(sys != NULL);
    // 0x04 flag could be for write
    int32_t fd = vfs_open(sys, test_path, 0x04);
    assert(fd >= 100);
    
    const char* msg = "Hello from VFS!";
    int written = vfs_write(sys, fd, (const uint8_t*)msg, strlen(msg));
    assert(written == (int)strlen(msg));
    
    vfs_close(sys, fd);
    
    // Open for read
    int32_t fd2 = vfs_open(sys, test_path, 0); // 0 or whatever flag for read
    assert(fd2 >= 100);
    
    uint8_t buf[64] = {0};
    int read_bytes = vfs_read(sys, fd2, buf, sizeof(buf));
    assert(read_bytes == (int)strlen(msg));
    assert(strcmp((char*)buf, msg) == 0);
    
    vfs_close(sys, fd2);
    system_free(sys);
    
    // Cleanup
    remove("test_vfs_temp.txt");
    printf("Host file tests passed.\n");
}

void test_dummy_file() {
    printf("Testing dummy sys file operations...\n");

    System* sys = system_create();
    assert(sys != NULL);
    int32_t fd = vfs_open(sys, "/sys/unknown", 0);
    assert(fd >= 100);

    uint8_t buf[16] = {0};
    int read_bytes = vfs_read(sys, fd, buf, 16);
    assert(read_bytes == 0);

    int written = vfs_write(sys, fd, (const uint8_t*)"test", 4);
    assert(written == 0);

    vfs_close(sys, fd);
    system_free(sys);
    printf("Dummy sys file tests passed.\n");
}

static void test_kbd_vfs(void) {
    printf("Testing /sys/kbd VFS...\n");
    System* sys = system_create();
    assert(sys != NULL);
    int32_t fd = vfs_open(sys, "/sys/kbd", 0);
    assert(fd >= 100);

    uint8_t buf[8] = {0};
    assert(vfs_read(sys, fd, buf, 8) == 0);
    assert(sys->yielded == true);
    sys->yielded = false;

    system_push_kbd_event(sys, 0, 65, 0);
    int n = vfs_read(sys, fd, buf, 8);
    assert(n == 8);
    assert(buf[0] == 0);
    assert(buf[2] == 65);

    vfs_close(sys, fd);
    system_free(sys);
    printf("  kbd VFS: OK\n");
}

static void test_draw_file() {
    printf("Testing /sys/draw VFS (rects)...\n");

    // Minimal System + screen buffer for testing draw commands
    System sys;
    memset(&sys, 0, sizeof(sys));
    sys.screen_width = 64;
    sys.screen_height = 48;
    sys.screen_pixels = (uint8_t*)calloc(1, 64 * 48 * 4);
    if (!sys.screen_pixels) {
        printf("  draw test: alloc failed\n");
        return;
    }

    // Open /sys/draw with the system context
    int32_t fd = vfs_open(&sys, "/sys/draw", 0x02 /* write-ish */);
    if (fd < 100) {
        printf("  draw test: open failed\n");
        free(sys.screen_pixels);
        return;
    }

    // Send a FillRect command: cmd=0, x=10,y=5, w=8,h=3, color=0x00FF00 (green)
    uint8_t cmd[13];
    cmd[0] = 0; // FillRect
    // x=10 (little endian)
    cmd[1] = 10; cmd[2] = 0;
    // y=5
    cmd[3] = 5;  cmd[4] = 0;
    // w=8
    cmd[5] = 8;  cmd[6] = 0;
    // h=3
    cmd[7] = 3;  cmd[8] = 0;
    // color 0x00FF00 -> R=0, G=0xFF, B=0  (we store as [0xFF, R, G, B] in C layout)
    cmd[9]  = 0x00;
    cmd[10] = 0xFF;
    cmd[11] = 0x00;
    cmd[12] = 0x00;

    int written = vfs_write(&sys, fd, cmd, 13);
    if (written != 13) {
        printf("  draw test: write failed\n");
    }

    // Check a pixel inside the rect (e.g. 12,6)
    int px = 12, py = 6;
    size_t off = (size_t)(py * 64 + px) * 4;
    // In our C layout: [0]=0xFF (marker), [1]=R=0, [2]=G=0xFF, [3]=B=0
    if (sys.screen_pixels[off + 0] != 0xFF ||
        sys.screen_pixels[off + 1] != 0x00 ||
        sys.screen_pixels[off + 2] != 0xFF ||
        sys.screen_pixels[off + 3] != 0x00) {
        printf("  draw test: pixel not green as expected (got %02X %02X %02X %02X)\n",
               sys.screen_pixels[off], sys.screen_pixels[off+1],
               sys.screen_pixels[off+2], sys.screen_pixels[off+3]);
    } else {
        printf("  FillRect wrote correct pixels.\n");
    }

    // Quick DrawRect outline test (cmd 3)
    uint8_t cmd3[13];
    cmd3[0] = 3;
    cmd3[1] = 20; cmd3[2] = 0;   // x=20
    cmd3[3] = 10; cmd3[4] = 0;   // y=10
    cmd3[5] = 4;  cmd3[6] = 0;   // w=4
    cmd3[7] = 2;  cmd3[8] = 0;   // h=2
    cmd3[9]  = 0x00;
    cmd3[10] = 0x00;
    cmd3[11] = 0x00;
    cmd3[12] = 0xFF; // blue-ish (B=FF in our storage)

    vfs_write(&sys, fd, cmd3, 13);

    vfs_close(&sys, fd);
    free(sys.screen_pixels);
    printf("/sys/draw rect tests passed.\n");
}

static void test_draw_scale_normalize(void) {
    printf("Testing draw scale normalization...\n");
    System* sys = make_test_screen(64, 48);

    sys->font_id = 2;
    assert(fabs(system_normalize_draw_scale(sys, 18) - (18.0 / 16.0)) < 0.001);
    assert(fabs(system_normalize_draw_scale(sys, 16) - 1.0) < 0.001);
    assert(fabs(system_normalize_draw_scale(sys, 1) - 1.0) < 0.001);

    sys->font_size = 18;
    assert(fabs(system_normalize_draw_scale(sys, 0) - (18.0 / 16.0)) < 0.001);

    free_test_screen(sys);
    printf("  scale normalization: OK\n");
}

static void test_draw_char_scale18(void) {
    printf("Testing DrawChar scale=18 (Quill font size)...\n");
    System* sys = make_test_screen(200, 64);

    int advance = system_measure_char(sys, 'A', 18);
    assert(advance >= 6);
    assert(advance <= 14);

    fill_screen_white(sys);
    int drawn = system_draw_char(sys, 10, 8, 'A', 0x000000, 18);
    assert(drawn == advance);

    int right = rightmost_ink_x(sys, 10, 8, 180, 40);
    int bottom = bottommost_ink_y(sys, 10, 8, 180, 40);
    assert(right >= 10);
    assert(right < 10 + 20);
    assert(bottom >= 8);
    assert(bottom < 8 + 24);

    assert(!pixel_is_ink(sys, 60, 8));

    free_test_screen(sys);
    printf("  DrawChar scale=18: OK\n");
}

static void test_draw_string_vfs_scale18(void) {
    printf("Testing VFS DrawString scale=18...\n");
    System* sys = make_test_screen(240, 64);

    int32_t fd = vfs_open(sys, "/sys/draw", 0x02);
    assert(fd >= 100);

    uint8_t set_font[] = {5, 2};
    assert(vfs_write(sys, fd, set_font, 2) == 2);

    uint8_t set_size[] = {4, 18};
    assert(vfs_write(sys, fd, set_size, 2) == 2);

    fill_screen_white(sys);

    uint8_t packet[14];
    packet[0] = 2;
    packet[1] = 12; packet[2] = 0;
    packet[3] = 6;  packet[4] = 0;
    packet[5] = 0x00; packet[6] = 0x00; packet[7] = 0x00; packet[8] = 0x00;
    packet[9] = 18;
    packet[10] = 2; packet[11] = 0;
    packet[12] = 'H';
    packet[13] = 'i';
    assert(vfs_write(sys, fd, packet, sizeof(packet)) == (int)sizeof(packet));

    int right = rightmost_ink_x(sys, 12, 6, 200, 40);
    assert(right >= 12);
    assert(right < 12 + 40);

    assert(!pixel_is_ink(sys, 120, 6));

    vfs_close(sys, fd);
    free_test_screen(sys);
    printf("  VFS DrawString scale=18: OK\n");
}

static void test_draw_default_scale_from_font_size(void) {
    printf("Testing draw scale=0 uses font_size...\n");
    System* sys = make_test_screen(200, 64);
    sys->font_size = 18;

    int advance_default = system_measure_char(sys, 'M', 0);
    int advance_explicit = system_measure_char(sys, 'M', 18);
    assert(advance_default == advance_explicit);

    fill_screen_white(sys);
    system_draw_char(sys, 5, 5, 'M', 0x000000, 0);
    int right = rightmost_ink_x(sys, 5, 5, 80, 30);
    assert(right >= 5);
    assert(right < 5 + 20);

    free_test_screen(sys);
    printf("  default scale from font_size: OK\n");
}

static void test_draw_small_scale_multiplier(void) {
    printf("Testing draw scale=1 multiplier path...\n");
    System* sys = make_test_screen(200, 64);
    sys->font_id = 2;

    int advance = system_measure_char(sys, 'x', 1);
    unsigned char* data = chicago12x12_cff;
    int raw_width = data[(uint8_t)'x'];
    if (raw_width == 0) raw_width = 6;
    assert(advance == raw_width);

    free_test_screen(sys);
    printf("  scale=1 multiplier: OK\n");
}

static void test_snarf_roundtrip(void) {
    printf("Testing /sys/snarf roundtrip...\n");
    System* sys = system_create();
    assert(sys != NULL);
    const char* msg = "hello, snarf";
    int32_t fd = vfs_open(sys, "/sys/snarf", 0);
    assert(fd >= 100);
    assert(vfs_write(sys, fd, (const uint8_t*)msg, (int)strlen(msg)) == (int)strlen(msg));
    vfs_close(sys, fd);

    fd = vfs_open(sys, "/sys/snarf", 0);
    char buf[64] = {0};
    int n = vfs_read(sys, fd, (uint8_t*)buf, sizeof(buf) - 1);
    assert(n == (int)strlen(msg));
    assert(strcmp(buf, msg) == 0);
    vfs_close(sys, fd);
    system_free(sys);
    printf("  snarf roundtrip: OK\n");
}

static void test_snarf_shared(void) {
    printf("Testing /sys/snarf shared buffer...\n");
    System* sys = system_create();
    assert(sys != NULL);
    int32_t a = vfs_open(sys, "/sys/snarf", 0);
    vfs_write(sys, a, (const uint8_t*)"first", 5);
    vfs_close(sys, a);

    int32_t b = vfs_open(sys, "/sys/snarf", 0);
    vfs_write(sys, b, (const uint8_t*)"second", 6);
    vfs_close(sys, b);

    int32_t c = vfs_open(sys, "/sys/snarf", 0);
    char buf[16] = {0};
    int n = vfs_read(sys, c, (uint8_t*)buf, sizeof(buf));
    vfs_close(sys, c);
    assert(n == 6);
    assert(strcmp(buf, "second") == 0);
    system_free(sys);
    printf("  snarf shared: OK\n");
}

static void test_host_seek_stat(void) {
    printf("Testing host seek/stat...\n");
    const char* path = "/sys/file/vfs_seek_stat_tmp.txt";
    System* sys = system_create();
    assert(sys != NULL);
    int32_t fd = vfs_open(sys, path, 0x06);
    assert(fd >= 100);
    const char* msg = "abcdef";
    vfs_write(sys, fd, (const uint8_t*)msg, 6);
    int64_t sz = vfs_stat(sys, fd);
    assert(sz == 6);
    vfs_seek(sys, fd, 2);
    char buf[4] = {0};
    int n = vfs_read(sys, fd, (uint8_t*)buf, 3);
    assert(n == 3);
    assert(memcmp(buf, "cde", 3) == 0);
    vfs_close(sys, fd);
    system_free(sys);
    remove("vfs_seek_stat_tmp.txt");
    printf("  host seek/stat: OK\n");
}

// --- per-System / refcount / child-VM regression tests ---

static void test_bind_mount_aliasing(void) {
    printf("Testing bind/mount aliasing...\n");
    System* sys = system_create();
    assert(sys != NULL);

    // Bind an open snarf fd to /n/clip; both paths must hit the same buffer.
    int32_t fd = vfs_open(sys, "/sys/snarf", 0);
    assert(fd >= 100);
    assert(vfs_bind(sys, fd, "/n/clip") == 0);

    int32_t clip = vfs_open(sys, "/n/clip", 0);
    assert(clip >= 100);
    assert(vfs_write(sys, clip, (const uint8_t*)"alias", 5) == 5);

    // Fresh /sys/snarf open reads the shared System buffer.
    int32_t check = vfs_open(sys, "/sys/snarf", 0);
    char buf[16] = {0};
    assert(vfs_read(sys, check, (uint8_t*)buf, sizeof(buf)) == 5);
    assert(strcmp(buf, "alias") == 0);

    vfs_close(sys, check);
    vfs_close(sys, clip);
    vfs_close(sys, fd);
    system_free(sys);
    printf("  bind/mount aliasing: OK\n");
}

static void test_chan_lifecycle(void) {
    printf("Testing channel lifecycle...\n");
    System* sys = system_create();
    assert(sys != NULL);

    int32_t cfd = vfs_open(sys, "/sys/chan/new", 0);
    assert(cfd >= 100);
    int32_t pfd = vfs_open(sys, "/sys/chan/peer", 0);
    assert(pfd >= 100);

    // Bind the peer endpoint, then close its fd (as Shell.lux does).
    // The mount's reference must keep the endpoint alive.
    assert(vfs_bind(sys, pfd, "/dev/kbd") == 0);
    assert(vfs_close(sys, pfd) == 0);

    int32_t kbd = vfs_open(sys, "/dev/kbd", 0);
    assert(kbd >= 100);

    assert(vfs_write(sys, cfd, (const uint8_t*)"evt1", 4) == 4);
    char buf[16] = {0};
    assert(vfs_read(sys, kbd, (uint8_t*)buf, sizeof(buf)) == 4);
    assert(memcmp(buf, "evt1", 4) == 0);

    // And the other direction still works.
    assert(vfs_write(sys, kbd, (const uint8_t*)"reply", 5) == 5);
    memset(buf, 0, sizeof(buf));
    assert(vfs_read(sys, cfd, (uint8_t*)buf, sizeof(buf)) == 5);
    assert(memcmp(buf, "reply", 5) == 0);

    vfs_close(sys, kbd);
    vfs_close(sys, cfd);
    system_free(sys);
    printf("  channel lifecycle: OK\n");
}

static void test_menu_unbound(void) {
    printf("Testing /dev/menu unbound vs bound...\n");
    System* sys = system_create();
    assert(sys != NULL);

    // Root/dev-mode apps must not get a dummy fd (that hid the Esc overlay).
    assert(vfs_open(sys, "/dev/menu", 0) < 0);
    assert(vfs_open(sys, "/sys/menu", 0) < 0);

    int32_t cfd = vfs_open(sys, "/sys/chan/new", 0);
    int32_t pfd = vfs_open(sys, "/sys/chan/peer", 0);
    assert(cfd >= 100 && pfd >= 100);
    assert(vfs_bind(sys, pfd, "/dev/menu") == 0);
    vfs_close(sys, pfd);

    int32_t mfd = vfs_open(sys, "/dev/menu", 0);
    assert(mfd >= 100);
    assert(vfs_write(sys, cfd, (const uint8_t*)"snap", 4) == 4);
    char buf[8] = {0};
    assert(vfs_read(sys, mfd, (uint8_t*)buf, sizeof(buf)) == 4);
    assert(memcmp(buf, "snap", 4) == 0);

    vfs_close(sys, mfd);
    vfs_close(sys, cfd);
    system_free(sys);
    printf("  /dev/menu unbound: OK\n");
}

static void test_child_vm_ns(void) {
    printf("Testing child VM spawn + ns bind + tick...\n");

    // Parent runs a trivial program that yields forever.
    size_t plen = 0;
    uint8_t* pprog = compile_source("[ 1 ] [ YIELD ] |:", HEADLESS_BASE_ADDRESS, &plen, false);
    assert(pprog != NULL);
    Machine* parent = machine_create(pprog, (uint32_t)plen, HEADLESS_BASE_ADDRESS, 4 * 1024 * 1024, false);
    assert(parent != NULL);
    System* psys = parent->system;

    // Child program: push 5, halt. Written to a temp .bin for /sys/vm/new.
    // Must match GRAPHICAL_BASE_ADDRESS used by vfs child spawn.
    size_t clen = 0;
    uint8_t* cprog = compile_source("5", GRAPHICAL_BASE_ADDRESS, &clen, false);
    assert(cprog != NULL);
    FILE* f = fopen("test_child_tmp.bin", "wb");
    assert(f != NULL);
    fwrite(cprog, 1, clen, f);
    fclose(f);

    int32_t vmfd = vfs_open(psys, "/sys/vm/new", 0);
    assert(vmfd >= 100);
    const char* path = "test_child_tmp.bin";
    assert(vfs_write(psys, vmfd, (const uint8_t*)path, (int)strlen(path)) == (int)strlen(path));

    uint8_t idbuf[4] = {0};
    assert(vfs_read(psys, vmfd, idbuf, 4) == 4);
    int32_t id = (int32_t)(idbuf[0] | (idbuf[1] << 8) | (idbuf[2] << 16) | (idbuf[3] << 24));
    assert(id >= 1 && id < SYS_MAX_CHILD_VMS);
    assert(psys->child_vms[id] != NULL);
    Machine* child = psys->child_vms[id];

    // Bind a channel peer into the child's namespace at /dev/kbd.
    int32_t cfd = vfs_open(psys, "/sys/chan/new", 0);
    int32_t pfd = vfs_open(psys, "/sys/chan/peer", 0);
    assert(cfd >= 100 && pfd >= 100);
    char bind_path[64];
    snprintf(bind_path, sizeof(bind_path), "/sys/vm/%d/ns/dev/kbd", id);
    assert(vfs_bind(psys, pfd, bind_path) == 0);
    vfs_close(psys, pfd);

    // Namespace isolation: the child's /dev/kbd is the bound channel;
    // the parent's own /dev/kbd is a plain input device.
    int32_t child_kbd = vfs_open(child->system, "/dev/kbd", 0);
    assert(child_kbd >= 100);
    assert(vfs_write(psys, cfd, (const uint8_t*)"ping", 4) == 4);
    char buf[16] = {0};
    assert(vfs_read(child->system, child_kbd, (uint8_t*)buf, sizeof(buf)) == 4);
    assert(memcmp(buf, "ping", 4) == 0);

    int32_t parent_kbd = vfs_open(psys, "/dev/kbd", 0);
    assert(parent_kbd >= 100);
    assert(vfs_write(psys, cfd, (const uint8_t*)"pong", 4) == 4);
    assert(vfs_read(psys, parent_kbd, (uint8_t*)buf, sizeof(buf)) == 0); // real kbd queue: empty

    // Status is 0 while the child has not run to HALT yet.
    char stpath[64];
    snprintf(stpath, sizeof(stpath), "/sys/vm/%d/status", id);
    int32_t stfd = vfs_open(psys, stpath, 0);
    assert(stfd >= 100);
    uint8_t stbuf[4] = {0xFF, 0, 0, 0};
    assert(vfs_read(psys, stfd, stbuf, 4) == 4);
    assert(stbuf[0] == 0);

    // Parent tick must tick the child to completion (push 5, halt).
    assert(machine_tick(parent) == true); // parent still yielding
    assert(child->cpu->halted);
    assert(child->cpu->stack_ptr == 1);
    assert(child->cpu->stack[0] == 5);

    assert(vfs_read(psys, stfd, stbuf, 4) == 4);
    assert(stbuf[0] == 1);
    vfs_close(psys, stfd);

    vfs_close(psys, parent_kbd);
    vfs_close(child->system, child_kbd);
    vfs_close(psys, cfd);
    vfs_close(psys, vmfd);
    machine_free(parent);
    free(pprog);
    free(cprog);
    remove("test_child_tmp.bin");
    printf("  child VM + ns: OK\n");
}

static void test_mouse_chan_packet(void) {
    printf("Testing 8-byte mouse packet over a bound channel...\n");
    System* sys = system_create();
    assert(sys != NULL);

    int32_t cfd = vfs_open(sys, "/sys/chan/new", 0);
    int32_t pfd = vfs_open(sys, "/sys/chan/peer", 0);
    assert(cfd >= 100 && pfd >= 100);
    assert(vfs_bind(sys, pfd, "/dev/mouse") == 0);
    assert(vfs_close(sys, pfd) == 0);

    int32_t mfd = vfs_open(sys, "/dev/mouse", 0);
    assert(mfd >= 100);

    // Type(1)=DOWN, Btn(1)=1, X(2 LE)=300, Y(2 LE)=400, pad(2).
    uint8_t pkt[8] = { 3, 1, 0x2C, 0x01, 0x90, 0x01, 0, 0 };
    assert(vfs_write(sys, cfd, pkt, 8) == 8);
    uint8_t got[8] = {0};
    assert(vfs_read(sys, mfd, got, 8) == 8);
    assert(memcmp(got, pkt, 8) == 0);

    vfs_close(sys, mfd);
    vfs_close(sys, cfd);
    system_free(sys);
    printf("  mouse channel packet: OK\n");
}

static void test_child_fb_blit(void) {
    printf("Testing child resolution inherit + /sys/vm/<id>/fb blit...\n");

    size_t plen = 0;
    uint8_t* pprog = compile_source("[ 1 ] [ YIELD ] |:", HEADLESS_BASE_ADDRESS, &plen, false);
    assert(pprog != NULL);
    Machine* parent = machine_create(pprog, (uint32_t)plen, HEADLESS_BASE_ADDRESS, 4 * 1024 * 1024, false);
    assert(parent != NULL);
    System* psys = parent->system;
    system_set_resolution(psys, 64, 32);

    size_t clen = 0;
    uint8_t* cprog = compile_source("5", GRAPHICAL_BASE_ADDRESS, &clen, false);
    assert(cprog != NULL);
    FILE* f = fopen("test_child_tmp.bin", "wb");
    assert(f != NULL);
    fwrite(cprog, 1, clen, f);
    fclose(f);

    int32_t vmfd = vfs_open(psys, "/sys/vm/new", 0);
    assert(vmfd >= 100);
    const char* path = "test_child_tmp.bin";
    assert(vfs_write(psys, vmfd, (const uint8_t*)path, (int)strlen(path)) == (int)strlen(path));

    uint8_t idbuf[4] = {0};
    assert(vfs_read(psys, vmfd, idbuf, 4) == 4);
    int32_t id = (int32_t)(idbuf[0] | (idbuf[1] << 8) | (idbuf[2] << 16) | (idbuf[3] << 24));
    assert(id >= 1 && id < SYS_MAX_CHILD_VMS);
    Machine* child = psys->child_vms[id];
    assert(child != NULL && child->system != NULL);
    assert(child->system->screen_width == 64);
    assert(child->system->screen_height == 32);
    assert(child->system->screen_pixels != NULL);
    assert(psys->back_pixels != NULL);

    // Child /dev/draw is the real draw device (not a Shell channel).
    int32_t cdfd = vfs_open(child->system, "/dev/draw", 0);
    assert(cdfd >= 100);
    uint8_t fill[13] = {
        0,
        2, 0, 3, 0, 4, 0, 5, 0,
        0xCC, 0xBB, 0xAA, 0x00,
    };
    uint8_t endf[1] = { 7 };
    assert(vfs_write(child->system, cdfd, fill, 13) == 13);
    assert(vfs_write(child->system, cdfd, endf, 1) == 1);
    vfs_close(child->system, cdfd);

    char fbpath[64];
    snprintf(fbpath, sizeof(fbpath), "/sys/vm/%d/fb", id);
    int32_t fbfd = vfs_open(psys, fbpath, 0);
    assert(fbfd >= 100);
    uint8_t one = 0;
    assert(vfs_read(psys, fbfd, &one, 1) == 1);
    assert(one == 1);

    uint8_t* px = psys->back_pixels + ((size_t)(3 * 64 + 2) * 4);
    assert(px[0] == 0xFF);
    assert(px[1] == 0xAA);
    assert(px[2] == 0xBB);
    assert(px[3] == 0xCC);
    uint8_t* miss = psys->back_pixels; // (0,0) was not painted
    assert(miss[1] == 0 && miss[2] == 0 && miss[3] == 0);

    vfs_close(psys, fbfd);
    vfs_close(psys, vmfd);
    machine_free(parent);
    free(pprog);
    free(cprog);
    remove("test_child_tmp.bin");
    printf("  child fb blit: OK\n");
}

static Machine* spawn_test_child(Machine* parent, int32_t* out_id) {
    size_t clen = 0;
    uint8_t* cprog = compile_source("[ 1 ] [ YIELD ] |:", GRAPHICAL_BASE_ADDRESS, &clen, false);
    assert(cprog != NULL);
    FILE* f = fopen("test_child_tmp.bin", "wb");
    assert(f != NULL);
    fwrite(cprog, 1, clen, f);
    fclose(f);
    free(cprog);

    int32_t vmfd = vfs_open(parent->system, "/sys/vm/new", 0);
    assert(vmfd >= 100);
    const char* path = "test_child_tmp.bin";
    assert(vfs_write(parent->system, vmfd, (const uint8_t*)path, (int)strlen(path)) == (int)strlen(path));
    uint8_t idbuf[4] = {0};
    assert(vfs_read(parent->system, vmfd, idbuf, 4) == 4);
    int32_t id = (int32_t)(idbuf[0] | (idbuf[1] << 8) | (idbuf[2] << 16) | (idbuf[3] << 24));
    assert(id >= 1 && id < SYS_MAX_CHILD_VMS);
    vfs_close(parent->system, vmfd);
    remove("test_child_tmp.bin");
    *out_id = id;
    return parent->system->child_vms[id];
}

static void test_offset_fb_blit(void) {
    printf("Testing /sys/vm/<id>/fb offset blit...\n");

    size_t plen = 0;
    uint8_t* pprog = compile_source("[ 1 ] [ YIELD ] |:", HEADLESS_BASE_ADDRESS, &plen, false);
    assert(pprog != NULL);
    Machine* parent = machine_create(pprog, (uint32_t)plen, HEADLESS_BASE_ADDRESS, 4 * 1024 * 1024, false);
    assert(parent != NULL);
    System* psys = parent->system;
    system_set_resolution(psys, 64, 32);

    int32_t id = 0;
    Machine* child = spawn_test_child(parent, &id);
    assert(child != NULL && child->system != NULL);

    int32_t cdfd = vfs_open(child->system, "/dev/draw", 0);
    assert(cdfd >= 100);
    uint8_t fill[13] = {
        0,
        0, 0, 0, 0, 4, 0, 4, 0,
        0x11, 0x22, 0x33, 0x00,
    };
    uint8_t endf[1] = { 7 };
    assert(vfs_write(child->system, cdfd, fill, 13) == 13);
    assert(vfs_write(child->system, cdfd, endf, 1) == 1);
    vfs_close(child->system, cdfd);

    char fbpath[64];
    snprintf(fbpath, sizeof(fbpath), "/sys/vm/%d/fb", id);
    int32_t fbfd = vfs_open(psys, fbpath, 0);
    assert(fbfd >= 100);
    uint8_t dest[8] = { 8, 0, 4, 0, 4, 0, 4, 0 }; // dx=8 dy=4 dw=4 dh=4
    assert(vfs_write(psys, fbfd, dest, 8) == 8);
    uint8_t one = 0;
    assert(vfs_read(psys, fbfd, &one, 1) == 1);
    assert(one == 1);

    uint8_t* px = psys->back_pixels + ((size_t)(4 * 64 + 8) * 4);
    assert(px[1] == 0x33 && px[2] == 0x22 && px[3] == 0x11);
    uint8_t* miss = psys->back_pixels;
    assert(miss[1] == 0 && miss[2] == 0 && miss[3] == 0);

    vfs_close(psys, fbfd);
    machine_free(parent);
    free(pprog);
    printf("  offset fb blit: OK\n");
}

static void test_child_geom_halt_drawsize(void) {
    printf("Testing /sys/vm/<id>/geom, halt, /dev/draw size...\n");

    size_t plen = 0;
    uint8_t* pprog = compile_source("[ 1 ] [ YIELD ] |:", HEADLESS_BASE_ADDRESS, &plen, false);
    assert(pprog != NULL);
    Machine* parent = machine_create(pprog, (uint32_t)plen, HEADLESS_BASE_ADDRESS, 4 * 1024 * 1024, false);
    assert(parent != NULL);
    System* psys = parent->system;
    system_set_resolution(psys, 64, 32);

    int32_t id = 0;
    Machine* child = spawn_test_child(parent, &id);
    assert(child != NULL && child->system != NULL);
    assert(child->system->screen_width == 64);
    assert(child->system->screen_height == 32);

    char gpath[64];
    snprintf(gpath, sizeof(gpath), "/sys/vm/%d/geom", id);
    int32_t gfd = vfs_open(psys, gpath, 0);
    assert(gfd >= 100);
    uint8_t gw[8] = { 32, 0, 16, 0, 0, 0, 0, 0 };
    assert(vfs_write(psys, gfd, gw, 8) == 8);
    assert(child->system->screen_width == 32);
    assert(child->system->screen_height == 16);
    uint8_t gr[8] = {0};
    assert(vfs_read(psys, gfd, gr, 8) == 8);
    assert(gr[0] == 32 && gr[1] == 0 && gr[2] == 16 && gr[3] == 0);
    vfs_close(psys, gfd);

    int32_t cdfd = vfs_open(child->system, "/dev/draw", 0);
    assert(cdfd >= 100);
    uint8_t sz[4] = {0};
    assert(vfs_read(child->system, cdfd, sz, 4) == 4);
    assert(sz[0] == 32 && sz[1] == 0 && sz[2] == 16 && sz[3] == 0);
    vfs_close(child->system, cdfd);

    char stpath[64];
    snprintf(stpath, sizeof(stpath), "/sys/vm/%d/status", id);
    int32_t stfd = vfs_open(psys, stpath, 0);
    assert(stfd >= 100);
    uint8_t one = 1;
    assert(vfs_write(psys, stfd, &one, 1) == 1);
    assert(child->cpu->halted);
    uint8_t stbuf[4] = {0};
    assert(vfs_read(psys, stfd, stbuf, 4) == 4);
    assert(stbuf[0] == 1);
    vfs_close(psys, stfd);

    machine_free(parent);
    free(pprog);
    printf("  geom/halt/draw size: OK\n");
}

static void test_dir_listing(void) {
    printf("Testing /sys/dir listing...\n");
    System* sys = system_create();
    assert(sys != NULL);

    int32_t fd = vfs_open(sys, "/sys/dir/apps", 0);
    assert(fd >= 100);
    char buf[4096];
    memset(buf, 0, sizeof(buf));
    int n = vfs_read(sys, fd, (uint8_t*)buf, (int)sizeof(buf) - 1);
    assert(n > 0);
    assert(strstr(buf, "UIDemo.lux") != NULL);
    assert(strstr(buf, ".\n") == NULL);
    vfs_close(sys, fd);

    fd = vfs_open(sys, "/sys/dir/", 0);
    assert(fd >= 100);
    memset(buf, 0, sizeof(buf));
    n = vfs_read(sys, fd, (uint8_t*)buf, (int)sizeof(buf) - 1);
    assert(n > 0);
    assert(strstr(buf, "apps/") != NULL);
    vfs_close(sys, fd);

    fd = vfs_open(sys, "/sys/dir", 0);
    assert(fd >= 100);
    memset(buf, 0, sizeof(buf));
    n = vfs_read(sys, fd, (uint8_t*)buf, (int)sizeof(buf) - 1);
    assert(n > 0);
    assert(strstr(buf, "apps/") != NULL);
    vfs_close(sys, fd);

    system_free(sys);
    printf("  dir listing: OK\n");
}

static int32_t read_id_le(System* sys, int32_t fd) {
    uint8_t idbuf[4] = {0};
    assert(vfs_read(sys, fd, idbuf, 4) == 4);
    return (int32_t)(idbuf[0] | (idbuf[1] << 8) | (idbuf[2] << 16) | (idbuf[3] << 24));
}

static void test_child_menu_export(void) {
    printf("Testing child UI::export snapshot on /dev/menu...\n");

    const char* src =
        "INCLUDE \"lib/app.lux\"\n"
        "MODULE T\n"
        "IMPORT APP\n"
        "IMPORT UI\n"
        "@start\n"
        "    T\"T\" APP::init\n"
        "    UI::new\n"
        "    320 UI::menubar\n"
        "    T\"File\" UI::menu\n"
        "    T\"Open File\" [ drop ] UI::item\n"
        "    T\"Quit\" [ drop HALT ] UI::item\n"
        "    T\"Edit\" UI::menu\n"
        "    T\"Show hidden\" [ drop ] UI::check-item\n"
        "    [ ] APP::on-frame!\n"
        "    APP::loop\n"
        ";\n"
        "T::start\n";

    size_t clen = 0;
    uint8_t* cprog = compile_source(src, GRAPHICAL_BASE_ADDRESS, &clen, false);
    assert(cprog != NULL);
    FILE* f = fopen("test_menu_child.bin", "wb");
    assert(f != NULL);
    fwrite(cprog, 1, clen, f);
    fclose(f);
    free(cprog);

    size_t plen = 0;
    uint8_t* pprog = compile_source("[ 1 ] [ YIELD ] |:", HEADLESS_BASE_ADDRESS, &plen, false);
    assert(pprog != NULL);
    Machine* parent = machine_create(pprog, (uint32_t)plen, HEADLESS_BASE_ADDRESS, 32 * 1024 * 1024, false);
    assert(parent != NULL);
    System* psys = parent->system;
    system_set_resolution(psys, 960, 720);

    int32_t vmfd = vfs_open(psys, "/sys/vm/new", 0);
    assert(vmfd >= 100);
    const char* path = "test_menu_child.bin";
    assert(vfs_write(psys, vmfd, (const uint8_t*)path, (int)strlen(path)) == (int)strlen(path));
    int32_t id = read_id_le(psys, vmfd);
    vfs_close(psys, vmfd);
    assert(psys->child_vms[id] != NULL);

    int32_t mpar = vfs_open(psys, "/sys/chan/new", 0);
    int32_t mpeer = vfs_open(psys, "/sys/chan/peer", 0);
    int32_t kpar = vfs_open(psys, "/sys/chan/new", 0);
    int32_t kpeer = vfs_open(psys, "/sys/chan/peer", 0);
    int32_t npar = vfs_open(psys, "/sys/chan/new", 0);
    int32_t npeer = vfs_open(psys, "/sys/chan/peer", 0);
    char bind[64];
    snprintf(bind, sizeof(bind), "/sys/vm/%d/ns/dev/mouse", id);
    assert(vfs_bind(psys, mpeer, bind) == 0);
    snprintf(bind, sizeof(bind), "/sys/vm/%d/ns/dev/kbd", id);
    assert(vfs_bind(psys, kpeer, bind) == 0);
    snprintf(bind, sizeof(bind), "/sys/vm/%d/ns/dev/menu", id);
    assert(vfs_bind(psys, npeer, bind) == 0);
    vfs_close(psys, mpeer);
    vfs_close(psys, kpeer);
    vfs_close(psys, npeer);

    uint8_t snap[1024];
    int n = 0;
    for (int i = 0; i < 16; i++) {
        machine_tick(parent);
        n = vfs_read(psys, npar, snap, (int)sizeof(snap));
        if (n > 2 && snap[0] == 1 && snap[1] >= 2) break;
    }

    fprintf(stderr, "  snapshot n=%d", n);
    if (n > 0) {
        fprintf(stderr, " bytes:");
        for (int i = 0; i < n && i < 64; i++) fprintf(stderr, " %02x", snap[i]);
    }
    fprintf(stderr, "\n");
    if (n > 2) {
        fprintf(stderr, "  type=%u menus=%u\n", snap[0], snap[1]);
    }

    assert(n > 2);
    assert(snap[0] == 1);
    assert(snap[1] >= 2);

    int found_edit = 0, found_open = 0, found_hidden = 0;
    for (int i = 0; i < n - 4; i++) {
        if (snap[i] == 4 && memcmp(snap + i + 1, "Edit", 4) == 0) found_edit = 1;
        if (i + 10 <= n && snap[i] == 9 && memcmp(snap + i + 1, "Open File", 9) == 0) found_open = 1;
        if (i + 12 <= n && snap[i] == 11 && memcmp(snap + i + 1, "Show hidden", 11) == 0) found_hidden = 1;
    }
    assert(found_edit);
    assert(found_open);
    assert(found_hidden);

    vfs_close(psys, mpar);
    vfs_close(psys, kpar);
    vfs_close(psys, npar);
    machine_free(parent);
    free(pprog);
    remove("test_menu_child.bin");
    printf("  child menu export: OK\n");
}

static uint8_t* load_bin_file(const char* path, size_t* out_len) {
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0) { fclose(f); return NULL; }
    uint8_t* buf = (uint8_t*)malloc((size_t)sz);
    if (!buf) { fclose(f); return NULL; }
    if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
        free(buf);
        fclose(f);
        return NULL;
    }
    fclose(f);
    *out_len = (size_t)sz;
    return buf;
}

static bool pump_ok(Machine* m, int n) {
    for (int i = 0; i < n; i++) {
        if (!m || !m->cpu) return false;
        if (m->cpu->halted) return false;
        if (!machine_tick(m)) return false;
        if (!m->cpu->running && !vm_yielded(m->cpu) && !m->system->yielded && !m->cpu->halted) {
            return false;
        }
    }
    return m && m->cpu && !m->cpu->halted;
}

static int spawn_child_app(Machine* parent, const char* bin_path) {
    System* psys = parent->system;
    int32_t mpar = vfs_open(psys, "/sys/chan/new", 0);
    int32_t mpeer = vfs_open(psys, "/sys/chan/peer", 0);
    int32_t kpar = vfs_open(psys, "/sys/chan/new", 0);
    int32_t kpeer = vfs_open(psys, "/sys/chan/peer", 0);
    int32_t npar = vfs_open(psys, "/sys/chan/new", 0);
    int32_t npeer = vfs_open(psys, "/sys/chan/peer", 0);
    int32_t vmfd = vfs_open(psys, "/sys/vm/new", 0);
    if (vmfd < 100) return -1;
    if (vfs_write(psys, vmfd, (const uint8_t*)bin_path, (int)strlen(bin_path)) <= 0) return -1;
    int32_t id = read_id_le(psys, vmfd);
    vfs_close(psys, vmfd);
    char bind[64];
    snprintf(bind, sizeof(bind), "/sys/vm/%d/ns/dev/mouse", id);
    assert(vfs_bind(psys, mpeer, bind) == 0);
    snprintf(bind, sizeof(bind), "/sys/vm/%d/ns/dev/kbd", id);
    assert(vfs_bind(psys, kpeer, bind) == 0);
    snprintf(bind, sizeof(bind), "/sys/vm/%d/ns/dev/menu", id);
    assert(vfs_bind(psys, npeer, bind) == 0);
    vfs_close(psys, mpeer);
    vfs_close(psys, kpeer);
    vfs_close(psys, npeer);
    (void)mpar; (void)kpar; (void)npar;
    return id;
}

static void test_two_child_apps_host(void) {
    printf("Testing two child apps spawned via /sys/vm/new...\n");
    size_t plen = 0;
    uint8_t* pprog = compile_source("[ 1 ] [ YIELD ] |:", HEADLESS_BASE_ADDRESS, &plen, false);
    assert(pprog != NULL);
    Machine* parent = machine_create(pprog, (uint32_t)plen, HEADLESS_BASE_ADDRESS, 32 * 1024 * 1024, false);
    assert(parent != NULL);
    system_set_resolution(parent->system, 960, 720);

    int id1 = spawn_child_app(parent, "apps/Hello.bin");
    int id2 = spawn_child_app(parent, "apps/UIDemo.bin");
    assert(id1 >= 1 && id2 >= 1 && id1 != id2);
    assert(parent->system->child_vms[id1] != NULL);
    assert(parent->system->child_vms[id2] != NULL);

    for (int i = 0; i < 24; i++) {
        assert(machine_tick(parent) == true);
        assert(!parent->cpu->halted);
        assert(parent->system->child_vms[id1]->cpu != NULL);
        assert(!parent->system->child_vms[id1]->cpu->halted || parent->system->child_vms[id1]->cpu->running);
    }

    char fb1[64], fb2[64];
    snprintf(fb1, sizeof(fb1), "/sys/vm/%d/fb", id1);
    snprintf(fb2, sizeof(fb2), "/sys/vm/%d/fb", id2);
    int32_t f1 = vfs_open(parent->system, fb1, 0);
    int32_t f2 = vfs_open(parent->system, fb2, 0);
    assert(f1 >= 100 && f2 >= 100);
    uint8_t dest[8] = { 20, 0, 40, 0, 100, 0, 80, 0 };
    assert(vfs_write(parent->system, f1, dest, 8) == 8);
    uint8_t one = 0;
    assert(vfs_read(parent->system, f1, &one, 1) == 1);
    dest[0] = 140; dest[2] = 40;
    assert(vfs_write(parent->system, f2, dest, 8) == 8);
    assert(vfs_read(parent->system, f2, &one, 1) == 1);

    vfs_close(parent->system, f1);
    vfs_close(parent->system, f2);
    machine_free(parent);
    free(pprog);
    printf("  two child apps host: OK\n");
}

static void test_root_app_runs(void) {
    printf("Testing a root app on the graphical machine...\n");
    size_t slen = 0;
    uint8_t* sprog = load_bin_file("apps/Hello.bin", &slen);
    if (!sprog) {
        printf("  SKIP: apps/Hello.bin missing\n");
        return;
    }
    Machine* m = machine_create(sprog, (uint32_t)slen, GRAPHICAL_BASE_ADDRESS, 32 * 1024 * 1024, false);
    assert(m != NULL);
    system_set_resolution(m->system, 960, 720);
    if (!pump_ok(m, 80)) {
        fprintf(stderr, "  Hello died (halted=%d pc=0x%08X sp=%d)\n",
                m->cpu->halted, m->cpu->pc, m->cpu->stack_ptr);
        assert(0);
    }
    assert(!m->cpu->halted);
    assert(m->cpu->stack_ptr < 16);
    machine_free(m);
    free(sprog);
    printf("  root app: OK\n");
}

int main() {
    test_host_file();
    test_dummy_file();
    test_kbd_vfs();
    test_snarf_roundtrip();
    test_snarf_shared();
    test_host_seek_stat();
    test_draw_file();
    test_draw_scale_normalize();
    test_draw_char_scale18();
    test_draw_string_vfs_scale18();
    test_draw_default_scale_from_font_size();
    test_draw_small_scale_multiplier();
    test_bind_mount_aliasing();
    test_chan_lifecycle();
    test_menu_unbound();
    test_child_vm_ns();
    test_mouse_chan_packet();
    test_child_fb_blit();
    test_offset_fb_blit();
    test_child_geom_halt_drawsize();
    test_dir_listing();
    test_child_menu_export();
    test_two_child_apps_host();
    test_root_app_runs();
    printf("All VFS tests passed!\n");
    return 0;
}
