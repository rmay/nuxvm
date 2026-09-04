/* Graphical Fluxio demo for the Cloister picker.
 *
 *   make all
 *   ./bin/cloister            (pick HelloCloister.bin from the Fluxio column)
 *
 * Loops long enough to see, then HALTs back to the picker -- or press
 * Esc any time for the System 6 System Menu (lib/escape_menu.fx),
 * which every Fluxio app should include the same way: poll kbd/mouse,
 * feed each event to escmenu_key/escmenu_mouse first (skipping the app's
 * own handling of anything they consume), draw the app's own frame, then
 * escmenu_draw() last so the panel sits on top, and check
 * escmenu_wants_quit() to exit the loop.
 */
include "../../lib/escape_menu.fx";

version 400000;

/** entry point */
int main() {
    int fd = vfs_open("/dev/draw");
    if (fd < 0) {
        return -1;
    }
    set_chan(fd, 1); /* k8 grayscale */
    int kfd = vfs_open("/dev/kbd");
    int mfd = vfs_open("/dev/mouse");

    set_window_title("Hello from Fluxio");

    int size = canvas_size(fd);
    int w = size >> 16;
    int h = size & 0xFFFF;
    int text_x = (w - 220) / 2;
    int text_y = h / 3;
    escmenu_init(w, h);

    int frame = 0;
    while (frame < 600) {
        if (poll_kbd(kfd)) {
            escmenu_key(kbd_key());
        }
        if (poll_mouse(mfd)) {
            escmenu_mouse(mouse_type(), mouse_button(), mouse_x(), mouse_y());
        }
        if (escmenu_wants_quit()) {
            frame = 600;
        } else {
            begin_frame(fd);
            fill_rect(fd, 0, 0, w, h, 0x102030);
            fill_rect(fd, 40 + (frame % 80), h / 2, 48, 48, 0xFF4020);
            draw_str(fd, text_x, text_y, 0xFFFFFF, 16, "Hello from Fluxio");
            escmenu_draw(fd);
            end_frame(fd);
            yield();
            frame = frame + 1;
        }
    }

    vfs_close(mfd);
    vfs_close(kfd);
    vfs_close(fd);
    return 0;
}
