/* Snake (Fluxio port of apps/Snake.lux).
 *
 *   make all
 *   ./bin/cloister            (pick Snake.bin from the Fluxio column)
 *
 * Same rules as the Lux original: 20x20 field, Start / Play Again overlays,
 * arrows or WASD, high score in /sys/file/.snake_hi, crawl paced on a
 * 16 ms simulation clock so speed is not tied to the host refresh rate.
 * Pause is the shared Fluxio Esc menu (lib/escape_menu.fx), drawn as the
 * same System 6 "System Menu" sheet Lux Snake gets from APP (Continue /
 * Restart App / Quit). Cells are solid / hollow / inset fill_rect
 * instead of TILEMAP::fill-pat -- Fluxio has no fill-pat builtin.
 */
include "../../lib/escape_menu.fx";

version 400000;

int grid = 20;
int segs = 400;
int bar_h = 28;
int step_ms = 16;
int max_steps = 5;
int max_dt = 100;

int canvas_w;
int canvas_h;
int tile_px;
int off_x;
int off_y;

int head_i;
int snake_len;
int dir;
int want;
int food_x;
int food_y;
int state;
int move_tick;
int speed;
int score;
int best;
int seed;
int overlay_kind;
int tick_acc;
int tick_ms;
int tfd;
int afd;
int draw_fd;

int body_x[400];
int body_y[400];

byte time_buf[16];
byte audio_pkt[4];
byte num_buf[16];
byte hi_buf[4];
byte hi_path[] = "/sys/file/.snake_hi";
int hi_path_len = 19;

/** absolute value */
int fx_abs(int n) {
    if (n < 0) {
        return 0 - n;
    }
    return n;
}

/** wrap a ring index into 0 .. segs-1 */
int wrap_seg(int i) {
    int r = i % segs;
    if (r < 0) {
        r = r + segs;
    }
    return r;
}

/** monotonic milliseconds from /dev/time, or tick_ms if the device is missing */
int now_ms() {
    if (tfd <= 0) {
        return tick_ms;
    }
    int n = vfs_read(tfd, time_buf, 16);
    if (n != 16) {
        return tick_ms;
    }
    int v = time_buf[12];
    v = v | (time_buf[13] << 8);
    v = v | (time_buf[14] << 16);
    v = v | (time_buf[15] << 24);
    return v;
}

/** LCG in 0 .. max-1, matching Snake.lux */
int random_n(int max) {
    seed = seed * 1103515245 + 12345;
    int r = seed & 0x7FFFFFFF;
    return r % max;
}

/** true if (x, y) is occupied by any live segment */
int on_snake(int x, int y) {
    int i = 0;
    int hit = 0;
    while (i < snake_len) {
        int idx = wrap_seg(head_i - i);
        if (body_x[idx] == x) {
            if (body_y[idx] == y) {
                hit = 1;
            }
        }
        i = i + 1;
    }
    return hit;
}

/** place food on an empty cell, up to 40 attempts */
int spawn_food() {
    int tries = 40;
    while (tries > 0) {
        food_x = random_n(grid);
        food_y = random_n(grid);
        if (on_snake(food_x, food_y)) {
            tries = tries - 1;
        } else {
            tries = 0;
        }
    }
    return 0;
}

/** play a short tone; no-op if /dev/audio is missing */
int beep(int id) {
    if (afd <= 0) {
        return 0;
    }
    audio_pkt[0] = id & 255;
    audio_pkt[1] = (id >> 8) & 255;
    audio_pkt[2] = (id >> 16) & 255;
    audio_pkt[3] = (id >> 24) & 255;
    vfs_write(afd, audio_pkt, 4);
    return 0;
}

/** load the high score from /sys/file/.snake_hi (big-endian i32) */
int load_best() {
    best = 0;
    int fd = vfs_open_buf(hi_path, hi_path_len, 0);
    if (fd < 0) {
        return 0;
    }
    int n = vfs_read(fd, hi_buf, 4);
    vfs_close(fd);
    if (n == 4) {
        best = (hi_buf[0] << 24) | (hi_buf[1] << 16) | (hi_buf[2] << 8) | hi_buf[3];
    }
    return 0;
}

/** write the high score */
int save_best() {
    hi_buf[0] = (best >> 24) & 255;
    hi_buf[1] = (best >> 16) & 255;
    hi_buf[2] = (best >> 8) & 255;
    hi_buf[3] = best & 255;
    int fd = vfs_open_buf(hi_path, hi_path_len, 6);
    if (fd < 0) {
        return 0;
    }
    vfs_write(fd, hi_buf, 4);
    vfs_close(fd);
    return 0;
}

/** fit a grid x grid field under the status bar */
int layout() {
    int avail_h = canvas_h - bar_h;
    int tw = canvas_w / grid;
    int th = avail_h / grid;
    tile_px = tw;
    if (th < tw) {
        tile_px = th;
    }
    off_x = (canvas_w - tile_px * grid) / 2;
    off_y = (avail_h - tile_px * grid) / 2 + bar_h;
    return 0;
}

/** left of cell gx */
int cell_x(int gx) {
    return gx * tile_px + off_x + 1;
}

/** top of cell gy */
int cell_y(int gy) {
    return gy * tile_px + off_y + 1;
}

/** inset cell size */
int cell_s() {
    return tile_px - 2;
}

/** start a new round at the centre, moving right */
int reset_round() {
    head_i = 10;
    snake_len = 3;
    dir = 1;
    want = 1;
    move_tick = 0;
    speed = 10;
    score = 0;
    body_x[10] = 10;
    body_y[10] = 10;
    body_x[9] = 9;
    body_y[9] = 10;
    body_x[8] = 8;
    body_y[8] = 10;
    spawn_food();
    return 0;
}

/** start or restart from an overlay / Enter */
int begin_round() {
    reset_round();
    state = 1;
    overlay_kind = 0;
    return 0;
}

/** game over, maybe save the high score */
int die() {
    state = 2;
    if (score > best) {
        best = score;
        save_best();
    }
    beep(110);
    overlay_kind = 2;
    return 0;
}

/** one crawl: apply want, move, eat or die */
int step_snake() {
    dir = want;
    int nx = body_x[head_i];
    int ny = body_y[head_i];
    if (dir == 0) {
        ny = ny - 1;
    }
    if (dir == 2) {
        ny = ny + 1;
    }
    if (dir == 3) {
        nx = nx - 1;
    }
    if (dir == 1) {
        nx = nx + 1;
    }
    if (nx < 0 || nx >= grid || ny < 0 || ny >= grid) {
        die();
        return 0;
    }
    if (on_snake(nx, ny)) {
        die();
        return 0;
    }
    head_i = wrap_seg(head_i + 1);
    body_x[head_i] = nx;
    body_y[head_i] = ny;
    if (food_x == nx && food_y == ny) {
        snake_len = snake_len + 1;
        score = score + 1;
        if (score % 5 == 0) {
            speed = speed - 1;
            if (speed < 3) {
                speed = 3;
            }
        }
        spawn_food();
        beep(440);
    }
    return 0;
}

/** queue a heading unless it is a 180-degree reverse */
int steer(int d) {
    if (fx_abs(dir - d) == 2) {
        return 0;
    }
    want = d;
    return 0;
}

/** write val as decimal digits into buf; returns length */
int int_to_str(int val, byte buf[]) {
    if (val == 0) {
        buf[0] = 48;
        return 1;
    }
    int n = val;
    if (n < 0) {
        n = 0 - n;
    }
    byte tmp[12];
    int len = 0;
    while (n > 0) {
        tmp[len] = 48 + (n % 10);
        n = n / 10;
        len = len + 1;
    }
    int i = 0;
    while (i < len) {
        buf[i] = tmp[len - 1 - i];
        i = i + 1;
    }
    return len;
}

/** solid black cell (head) */
int paint_cell_solid(int gx, int gy) {
    fill_rect(draw_fd, cell_x(gx), cell_y(gy), cell_s(), cell_s(), 0x000000);
    return 0;
}

/** hollow black cell (body) */
int paint_cell_hollow(int gx, int gy) {
    int x = cell_x(gx);
    int y = cell_y(gy);
    int s = cell_s();
    fill_rect(draw_fd, x, y, s, s, 0x000000);
    if (s > 4) {
        fill_rect(draw_fd, x + 2, y + 2, s - 4, s - 4, 0xFFFFFF);
    }
    return 0;
}

/** inset black square (food) */
int paint_cell_food(int gx, int gy) {
    int s = cell_s();
    int pad = s / 4;
    if (pad < 1) {
        pad = 1;
    }
    fill_rect(draw_fd, cell_x(gx) + pad, cell_y(gy) + pad, s - pad * 2, s - pad * 2, 0x000000);
    return 0;
}

/** score / best strip */
int paint_bar() {
    fill_rect(draw_fd, 0, 0, canvas_w, bar_h, 0xFFFFFF);
    fill_rect(draw_fd, 0, bar_h - 1, canvas_w, 1, 0x000000);
    draw_str(draw_fd, 8, 8, 0x000000, 16, "Score");
    int n = int_to_str(score, num_buf);
    draw_bytes(draw_fd, 72, 8, 0x000000, 16, num_buf, n);
    draw_str(draw_fd, canvas_w - 140, 8, 0x000000, 16, "Best");
    n = int_to_str(best, num_buf);
    draw_bytes(draw_fd, canvas_w - 84, 8, 0x000000, 16, num_buf, n);
    return 0;
}

/** field, snake, food */
int paint_field() {
    int field = tile_px * grid;
    fill_rect(draw_fd, off_x - 2, off_y - 2, field + 4, field + 4, 0x000000);
    fill_rect(draw_fd, off_x - 1, off_y - 1, field + 2, field + 2, 0xFFFFFF);
    int i = 0;
    while (i < snake_len) {
        int idx = wrap_seg(head_i - i);
        if (i == 0) {
            paint_cell_solid(body_x[idx], body_y[idx]);
        } else {
            paint_cell_hollow(body_x[idx], body_y[idx]);
        }
        i = i + 1;
    }
    paint_cell_food(food_x, food_y);
    return 0;
}

/** centred 220x120 title card; kind 0 = SNAKE, 1 = GAME OVER */
int paint_banner(int kind) {
    int bx = (canvas_w - 220) / 2;
    int by = (canvas_h - 120) / 2;
    fill_rect(draw_fd, bx, by, 220, 120, 0x000000);
    fill_rect(draw_fd, bx + 2, by + 2, 216, 116, 0xFFFFFF);
    if (kind == 0) {
        draw_str(draw_fd, bx + 70, by + 20, 0x000000, 16, "SNAKE");
    } else {
        draw_str(draw_fd, bx + 36, by + 20, 0x000000, 16, "GAME OVER");
    }
    return 0;
}

/** overlay button width */
int overlay_w() {
    if (overlay_kind == 2) {
        return 100;
    }
    return 80;
}

/** overlay button left */
int overlay_x() {
    return (canvas_w - overlay_w()) / 2;
}

/** overlay button top -- same as Snake.lux (height/2 + 16) */
int overlay_y() {
    return canvas_h / 2 + 16;
}

/** Start / Play Again button */
int paint_overlay() {
    if (overlay_kind == 0) {
        return 0;
    }
    int x = overlay_x();
    int y = overlay_y();
    int w = overlay_w();
    fill_rect(draw_fd, x, y, w, 20, 0x000000);
    fill_rect(draw_fd, x + 1, y + 1, w - 2, 18, 0xFFFFFF);
    if (overlay_kind == 2) {
        draw_str(draw_fd, x + 8, y + 4, 0x000000, 16, "Play Again");
    } else {
        draw_str(draw_fd, x + 16, y + 4, 0x000000, 16, "Start");
    }
    return 0;
}

/** true if (px, py) is inside the overlay button */
int overlay_hit(int px, int py) {
    if (overlay_kind == 0) {
        return 0;
    }
    int x = overlay_x();
    int y = overlay_y();
    int w = overlay_w();
    if (px < x) {
        return 0;
    }
    if (px >= x + w) {
        return 0;
    }
    if (py < y) {
        return 0;
    }
    if (py >= y + 20) {
        return 0;
    }
    return 1;
}

/** one keydown */
int handle_key(int key) {
    if (state == 1) {
        if (key == 17 || key == 119) {
            steer(0);
        }
        if (key == 18 || key == 115) {
            steer(2);
        }
        if (key == 19 || key == 97) {
            steer(3);
        }
        if (key == 20 || key == 100) {
            steer(1);
        }
    } else {
        if (key == 13) {
            begin_round();
        }
    }
    return 0;
}

/** overlay click (button-down) */
int handle_mouse(int mtype, int mx, int my) {
    if (state == 1) {
        return 0;
    }
    if (mtype == 3) {
        if (overlay_hit(mx, my)) {
            begin_round();
        }
    }
    return 0;
}

/** catch up the 16 ms crawl clock */
int run_ticks() {
    if (escmenu_is_open()) {
        return 0;
    }
    if (state != 1) {
        return 0;
    }
    int now = now_ms();
    int dt = now - tick_ms;
    tick_ms = now;
    if (dt < 0) {
        dt = 0;
    }
    if (dt > max_dt) {
        dt = max_dt;
    }
    tick_acc = tick_acc + dt;
    int steps = 0;
    int keep = 1;
    while (keep) {
        if (tick_acc < step_ms) {
            keep = 0;
        } else {
            if (steps >= max_steps) {
                tick_acc = 0;
                keep = 0;
            } else {
                tick_acc = tick_acc - step_ms;
                steps = steps + 1;
                if (state == 1) {
                    move_tick = move_tick + 1;
                    if (move_tick >= speed) {
                        move_tick = 0;
                        step_snake();
                    }
                } else {
                    keep = 0;
                }
            }
        }
    }
    return 0;
}

/** one painted frame */
int draw_frame() {
    begin_frame(draw_fd);
    fill_rect(draw_fd, 0, 0, canvas_w, canvas_h, 0xFFFFFF);
    paint_bar();
    if (state == 0) {
        paint_banner(0);
        paint_overlay();
    } else {
        paint_field();
        if (state == 2) {
            paint_banner(1);
            paint_overlay();
        }
    }
    escmenu_draw(draw_fd);
    end_frame(draw_fd);
    return 0;
}

/** entry point */
int main() {
    draw_fd = vfs_open("/dev/draw");
    if (draw_fd < 0) {
        return -1;
    }
    int kfd = vfs_open("/dev/kbd");
    int mfd = vfs_open("/dev/mouse");
    tfd = vfs_open("/dev/time");
    afd = vfs_open("/dev/audio");

    set_window_title("Snake");

    int size = canvas_size(draw_fd);
    canvas_w = size >> 16;
    canvas_h = size & 0xFFFF;
    escmenu_init(canvas_w, canvas_h);
    layout();

    seed = 1;
    state = 0;
    overlay_kind = 1;
    load_best();
    reset_round();
    tick_acc = 0;
    tick_ms = now_ms();

    while (!escmenu_wants_quit()) {
        if (escmenu_wants_restart()) {
            escmenu_ack_restart();
            seed = 1;
            state = 0;
            overlay_kind = 1;
            reset_round();
            tick_acc = 0;
            tick_ms = now_ms();
        }
        int got_k = poll_kbd(kfd);
        while (got_k) {
            if (kbd_type() == 0) {
                if (!escmenu_key(kbd_key())) {
                    handle_key(kbd_key());
                }
            }
            got_k = poll_kbd(kfd);
        }
        int got_m = poll_mouse(mfd);
        while (got_m) {
            if (!escmenu_mouse(mouse_type(), mouse_button(), mouse_x(), mouse_y())) {
                handle_mouse(mouse_type(), mouse_x(), mouse_y());
            }
            got_m = poll_mouse(mfd);
        }
        run_ticks();
        draw_frame();
        yield();
    }

    vfs_close(mfd);
    vfs_close(kfd);
    vfs_close(draw_fd);
    return 0;
}
