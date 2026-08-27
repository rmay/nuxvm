/* Fluxio "hello world" for Cloister -- the real windowed version.
 *
 * Opens the draw device, clears the window to a dark background, and draws
 * "Hello, Cloister!" centered near the top, once per frame, for a couple
 * hundred frames (long enough to actually see it on screen when run
 * through the real graphical host rather than headless nux).
 *
 * Run for real, in a window:
 *   bin/fluxioc -target graphical -o examples/fluxio/hello_cloister.bin examples/fluxio/hello_cloister.fx
 *   bin/cloister examples/fluxio/hello_cloister.bin
 *
 * Note: when a .bin is passed directly on the command line, cloister quits
 * the instant the program halts (src/cloister.c: `from_argv` -> `quit =
 * true` as soon as machine_tick() reports halted) -- it doesn't linger on
 * the picker screen the way launching it with no args does. That's why
 * this loops for a few hundred frames rather than just drawing one frame
 * and returning immediately.
 *
 * Run headless (compiles/executes identically, just no window to look at --
 * see docs/fluxio-language-plan.md's v2b section for how this was verified
 * pixel-exact against the framebuffer without a display):
 *   bin/fluxioc -target headless -o examples/fluxio/hello_cloister.bin examples/fluxio/hello_cloister.fx
 *   bin/nux examples/fluxio/hello_cloister.bin
 */

/** entry point */
int main() {
    int fd = vfs_open("/dev/draw");
    if (fd < 0) {
        return -1;
    }

    set_window_title("Hello, Cloister!");

    int size = canvas_size(fd);
    int w = size >> 16;
    int h = size & 0xFFFF;
    int text_x = (w - 200) / 2;
    int text_y = h / 3;

    int frame = 0;
    while (frame < 600) {
        begin_frame(fd);
        fill_rect(fd, 0, 0, w, h, 0x102030);
        draw_str(fd, text_x, text_y, 0xFFFFFF, 16, "Hello, Cloister!");
        end_frame(fd);
        yield();
        frame = frame + 1;
    }

    vfs_close(fd);
    return 0;
}
