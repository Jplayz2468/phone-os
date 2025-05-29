// Minimal phone "OS" - using /dev/input/eventX as touchscreen (treated as mouse clicks)
// This version treats any BTN_LEFT press as a touch (like a mouse click touchscreen)

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <linux/input.h>
#include <linux/fb.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <signal.h>
#include <dirent.h>

#define WIDTH 1080
#define HEIGHT 1920
#define COLOR_BLACK 0xFF000000
#define COLOR_RED   0xFFFF0000
#define COLOR_GREEN 0xFF00FF00
#define COLOR_BLUE  0xFF0000FF
#define COLOR_WHITE 0xFFFFFFFF

typedef enum { APP_HOME, APP_CLOCK, APP_BOUNCE, APP_COLOR } AppMode;

struct {
    uint32_t *fb;
    uint32_t *bb;
    int fb_fd;
    size_t size;
    struct fb_var_screeninfo vinfo;
    struct fb_fix_screeninfo finfo;
    int input_fd;
    AppMode mode;
    int cursor_x, cursor_y;
} state;

volatile int running = 1;

void handle_sigint(int sig) { running = 0; }

void fill_rect(int x, int y, int w, int h, uint32_t color) {
    for (int j = y; j < y + h; j++) {
        for (int i = x; i < x + w; i++) {
            if (i >= 0 && j >= 0 && i < WIDTH && j < HEIGHT)
                state.bb[j * WIDTH + i] = color;
        }
    }
}

void clear(uint32_t color) {
    for (int i = 0; i < WIDTH * HEIGHT; i++)
        state.bb[i] = color;
}

void swap() {
    for (int y = 0; y < HEIGHT; y++)
        memcpy((char*)state.fb + y * state.finfo.line_length,
               state.bb + y * WIDTH, WIDTH * 4);
}

void draw_home() {
    clear(COLOR_BLACK);
    fill_rect(100, 300, 300, 300, COLOR_RED);
    fill_rect(400, 300, 300, 300, COLOR_GREEN);
    fill_rect(700, 300, 300, 300, COLOR_BLUE);
}

void draw_clock() {
    clear(COLOR_BLACK);
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    for (int i = 0; i < tm->tm_hour; i++) fill_rect(100 + i * 40, 500, 30, 30, COLOR_RED);
    for (int i = 0; i < tm->tm_min / 5; i++) fill_rect(100 + i * 20, 600, 20, 20, COLOR_WHITE);
}

void draw_color(uint64_t frame) {
    uint32_t c = 0xFF000000 | ((frame * 3) % 255) << 16 | ((frame * 7) % 255) << 8 | ((frame * 13) % 255);
    clear(c);
}

void draw_bounce(uint64_t frame) {
    clear(COLOR_BLACK);
    int x = 100 + abs((int)(frame % 800));
    int y = 300 + abs((int)((frame * 2) % 800));
    fill_rect(x, y, 100, 100, COLOR_GREEN);
}

void handle_touch(int x, int y) {
    if (state.mode == APP_HOME) {
        if (y > 300 && y < 600) {
            if (x > 100 && x < 400) state.mode = APP_CLOCK;
            else if (x > 400 && x < 700) state.mode = APP_BOUNCE;
            else if (x > 700 && x < 1000) state.mode = APP_COLOR;
        }
    } else {
        state.mode = APP_HOME;
    }
}

void poll_input() {
    struct input_event ev;
    while (read(state.input_fd, &ev, sizeof(ev)) > 0) {
        if (ev.type == EV_REL) {
            if (ev.code == REL_X) state.cursor_x += ev.value;
            if (ev.code == REL_Y) state.cursor_y += ev.value;
        } else if (ev.type == EV_ABS) {
            if (ev.code == ABS_X) state.cursor_x = ev.value;
            if (ev.code == ABS_Y) state.cursor_y = ev.value;
        } else if (ev.type == EV_KEY && ev.code == BTN_LEFT && ev.value == 1) {
            handle_touch(state.cursor_x, state.cursor_y);
        }
    }
}

int find_mouse_like_touch() {
    DIR *d = opendir("/dev/input");
    if (!d) return -1;
    struct dirent *ent;
    char path[256];
    while ((ent = readdir(d))) {
        if (strncmp(ent->d_name, "event", 5) == 0) {
            snprintf(path, sizeof(path), "/dev/input/%s", ent->d_name);
            int fd = open(path, O_RDONLY | O_NONBLOCK);
            if (fd < 0) continue;
            char name[256];
            ioctl(fd, EVIOCGNAME(sizeof(name)), name);
            if (strstr(name, "mouse") || strstr(name, "Touch") || strstr(name, "touch")) {
                closedir(d);
                return fd;
            }
            close(fd);
        }
    }
    closedir(d);
    return -1;
}

void cleanup() {
    if (state.fb) munmap(state.fb, state.size);
    if (state.bb) free(state.bb);
    if (state.fb_fd >= 0) close(state.fb_fd);
    if (state.input_fd >= 0) close(state.input_fd);
}

int main() {
    signal(SIGINT, handle_sigint);
    state.fb_fd = open("/dev/fb0", O_RDWR);
    ioctl(state.fb_fd, FBIOGET_FSCREENINFO, &state.finfo);
    ioctl(state.fb_fd, FBIOGET_VSCREENINFO, &state.vinfo);
    state.size = state.finfo.line_length * state.vinfo.yres;
    state.fb = (uint32_t*)mmap(NULL, state.size, PROT_READ | PROT_WRITE, MAP_SHARED, state.fb_fd, 0);
    state.bb = (uint32_t*)calloc(WIDTH * HEIGHT, sizeof(uint32_t));
    state.input_fd = find_mouse_like_touch();
    state.mode = APP_HOME;
    state.cursor_x = WIDTH / 2;
    state.cursor_y = HEIGHT / 2;

    uint64_t frame = 0;
    while (running) {
        poll_input();
        switch (state.mode) {
            case APP_HOME: draw_home(); break;
            case APP_CLOCK: draw_clock(); break;
            case APP_BOUNCE: draw_bounce(frame); break;
            case APP_COLOR: draw_color(frame); break;
        }
        swap();
        usleep(16000);
        frame++;
    }
    cleanup();
    return 0;
}
