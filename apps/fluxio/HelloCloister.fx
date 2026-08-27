/* Graphical Fluxio demo for the Cloister picker.
 *
 *   make all
 *   ./bin/cloister            (pick HelloCloister.bin from the Fluxio column)
 *
 * Loops long enough to see, then HALTs back to the picker.
 */

/** entry point */
int main() {
    int fd = vfs_open("/dev/draw");
    if (fd < 0) {
        return -1;
    }

    set_window_title("Hello from Fluxio");

    int size = canvas_size(fd);
    int w = size >> 16;
    int h = size & 0xFFFF;
    int text_x = (w - 220) / 2;
    int text_y = h / 3;

    int frame = 0;
    while (frame < 600) {
        begin_frame(fd);
        fill_rect(fd, 0, 0, w, h, 0x102030);
        fill_rect(fd, 40 + (frame % 80), h / 2, 48, 48, 0xFF4020);
        draw_str(fd, text_x, text_y, 0xFFFFFF, 16, "Hello from Fluxio");
        end_frame(fd);
        yield();
        frame = frame + 1;
    }

    vfs_close(fd);
    return 0;
}
