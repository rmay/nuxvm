/* A real windowed Cloister "hello world" in Fluxio -- opens the draw
 * device, draws a filled rectangle and a text string, and loops for a
 * fixed number of frames (moving the rectangle each time) before exiting.
 *
 * Compiles and runs under bin/nux -- machine_create() wires up the same
 * System/VFS/draw device nux always has, so vfs_open("/dev/draw") and every
 * draw call here work headlessly (verified pixel-exact in
 * test_fluxio_compiler.c's test_fill_rect_pixel_exact). There's just no SDL
 * window in headless mode to look at it in, and plain `bin/nux` is a
 * one-shot runner: OP_YIELD stops it after the FIRST yield() (it doesn't
 * pump frames the way a live host does), so running this via `bin/nux`
 * directly draws exactly one frame and then exits -- it will not visibly
 * loop through all 30 iterations that way. A real host (like
 * src/cloister.c does for Lux via repeated machine_tick() calls) would
 * pump this every frame and show the rectangle actually moving.
 *
 *   bin/fluxioc -target headless -o examples/fluxio/cloister_hello.bin examples/fluxio/cloister_hello.fx
 *   bin/nux examples/fluxio/cloister_hello.bin
 *
 * See docs/fluxio-language-plan.md for what's built (and what's still out
 * of scope, e.g. dynamic/runtime text) in this v2b Cloister-bindings slice.
 */

version 400000;

/** entry point */
int main() {
    int fd = vfs_open("/dev/draw");
    if (fd < 0) {
        return -1;
    }

    set_window_title("Fluxio says hello");

    int frame = 0;
    while (frame < 30) {
        begin_frame(fd);
        fill_rect(fd, 0, 0, 320, 200, 0x001020);
        fill_rect(fd, frame * 8, 80, 40, 40, 0xFF4020);
        draw_str(fd, 20, 20, 0xFFFFFF, 12, "Hello, Cloister!");
        end_frame(fd);
        yield();
        frame = frame + 1;
    }

    vfs_close(fd);
    return 0;
}
