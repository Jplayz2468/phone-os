// Compile with: gcc -O2 -o mini_ui mini_ui.c -lm
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/fb.h>
#include <linux/input.h>
#include <string.h>
#include <time.h>
#include <poll.h>
#include <signal.h>
#include <math.h>

#define COLOR_BG 0xFF1A1A1A
#define COLOR_TEXT 0xFFFFFFFF
#define COLOR_TOUCH 0xFF0078D4
#define MAX_TOUCH 8
int running = 1;

struct fb_var_screeninfo vinfo;
struct fb_fix_screeninfo finfo;
uint32_t *fbp = NULL, *back = NULL;
int fb_fd, w, h, line_len;

struct TouchDev { int fd, minx, maxx, miny, maxy; } tdev[MAX_TOUCH];
int num_touch = 0;
struct { int x, y, pressed; } touch = {0}, last = {0};

// Draw
void draw_pixel(uint32_t *buf, int x, int y, uint32_t c) {
    if (x >= 0 && x < w && y >= 0 && y < h) buf[y * w + x] = c;
}
void draw_circle(uint32_t *buf, int cx, int cy, int r, uint32_t c) {
    for (int y = -r; y <= r; y++) for (int x = -r; x <= r; x++)
        if (x*x + y*y <= r*r) draw_pixel(buf, cx + x, cy + y, c);
}
void clear(uint32_t *buf, uint32_t c) {
    for (int i = 0; i < w * h; i++) buf[i] = c;
}
void draw_text(uint32_t *buf, int x, int y, const char *t, uint32_t c) {
    while (*t) { for (int dy = 0; dy < 10; dy++) for (int dx = 0; dx < 6; dx++)
        draw_pixel(buf, x+dx, y+dy, c); x += 8; t++; }
}
void swap() {
    for (int y = 0; y < h; y++) memcpy((char*)fbp + y*line_len, &back[y*w], w*4);
}

// Touch
int init_touch() {
    for (int i = 0; i < 16 && num_touch < MAX_TOUCH; i++) {
        char path[64]; snprintf(path, 64, "/dev/input/event%d", i);
        int fd = open(path, O_RDONLY | O_NONBLOCK); if (fd < 0) continue;
        struct input_absinfo ax, ay;
        if (ioctl(fd, EVIOCGABS(ABS_X), &ax) < 0 || ioctl(fd, EVIOCGABS(ABS_Y), &ay) < 0) { close(fd); continue; }
        tdev[num_touch++] = (struct TouchDev){fd, ax.minimum, ax.maximum, ay.minimum, ay.maximum};
    }
    return num_touch > 0;
}
void read_touch() {
    struct pollfd pf[MAX_TOUCH];
    for (int i = 0; i < num_touch; i++) pf[i] = (struct pollfd){tdev[i].fd, POLLIN};
    if (poll(pf, num_touch, 0) <= 0) return;
    for (int i = 0; i < num_touch; i++) if (pf[i].revents & POLLIN) {
        struct input_event e;
        while (read(tdev[i].fd, &e, sizeof(e)) == sizeof(e)) {
            if (e.type == EV_ABS) {
                if (e.code == ABS_X || e.code == ABS_MT_POSITION_X)
                    touch.x = (e.value - tdev[i].minx) * w / (tdev[i].maxx - tdev[i].minx + 1);
                if (e.code == ABS_Y || e.code == ABS_MT_POSITION_Y)
                    touch.y = (e.value - tdev[i].miny) * h / (tdev[i].maxy - tdev[i].miny + 1);
                if (e.code == ABS_MT_TRACKING_ID)
                    touch.pressed = (e.value >= 0);
            }
            if (e.type == EV_KEY && e.code == BTN_TOUCH) touch.pressed = e.value;
        }
    }
}

// Fallback
void read_keys() {
    int ch = getchar();
    if (ch == 'q') running = 0;
    if (ch == 'w') touch.y -= 10;
    if (ch == 's') touch.y += 10;
    if (ch == 'a') touch.x -= 10;
    if (ch == 'd') touch.x += 10;
    if (ch == ' ') touch.pressed = 1;
}

// Main loop
void loop() {
    char timebuf[32];
    while (running) {
        last = touch;
        if (num_touch) read_touch(); else read_keys();
        clear(back, COLOR_BG);

        time_t now = time(NULL);
        strftime(timebuf, 31, "%H:%M:%S", localtime(&now));
        draw_text(back, 30, 30, "Hello Mini UI", COLOR_TEXT);
        draw_text(back, 30, 60, timebuf, COLOR_TEXT);

        if (touch.pressed) draw_circle(back, touch.x, touch.y, 30, COLOR_TOUCH);
        swap();
        usleep(16000);
    }
}

// Init
int main() {
    signal(SIGINT, [](int s){ running = 0; });
    fb_fd = open("/dev/fb0", O_RDWR);
    ioctl(fb_fd, FBIOGET_FSCREENINFO, &finfo);
    ioctl(fb_fd, FBIOGET_VSCREENINFO, &vinfo);
    w = vinfo.xres; h = vinfo.yres; line_len = finfo.line_length;
    fbp = mmap(0, line_len * h, PROT_READ | PROT_WRITE, MAP_SHARED, fb_fd, 0);
    posix_memalign((void**)&back, 64, w * h * 4);
    system("stty raw -echo");
    init_touch();
    loop();
    system("stty sane");
    munmap(fbp, line_len * h); free(back); close(fb_fd);
    for (int i = 0; i < num_touch; i++) close(tdev[i].fd);
    return 0;
}
