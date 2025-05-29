#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/fb.h>
#include <linux/input.h>
#include <sys/mman.h>
#include <poll.h>
#include <string.h>
#include <math.h>
#include <time.h>

#define FONT_PATH "phone-os/Inter-Regular.otf"
#define COLOR_BG 0xFF1A1A1A
#define COLOR_TEXT 0xFFFFFFFF
#define MAX_TOUCH_DEVICES 8

uint32_t *fbp = NULL;
int fb_fd, screen_w, screen_h, stride;

typedef struct {
    int fd;
    int min_x, max_x, min_y, max_y;
} TouchDev;

TouchDev touch_devs[MAX_TOUCH_DEVICES];
int num_touch_devs = 0;

typedef struct {
    int x, y;
    int pressed;
    int just_pressed;
} TouchState;

TouchState touch = {0};

void clear(uint32_t *buf, uint32_t color) {
    for (int i = 0; i < screen_w * screen_h; i++) buf[i] = color;
}

void draw_glyph(uint32_t *buf, int x, int y, unsigned char *bitmap, int bw, int bh, float alpha, uint32_t color) {
    for (int dy = 0; dy < bh; dy++) {
        for (int dx = 0; dx < bw; dx++) {
            int a = bitmap[dy * bw + dx];
            if (a > 32) {
                int px = x + dx, py = y + dy;
                if (px >= 0 && px < screen_w && py >= 0 && py < screen_h)
                    buf[py * screen_w + px] = color;
            }
        }
    }
}

void draw_text_centered(uint32_t *buf, const char *text, stbtt_bakedchar *cdata, float scale, int center_x, int center_y, float alpha, uint32_t color) {
    float x = 0;
    for (const char *t = text; *t; t++) {
        stbtt_aligned_quad q;
        stbtt_GetBakedQuad(cdata, 1024, 1024, *t - 32, &x, 0, &q, 1);
    }

    float total_width = x;
    float draw_x = center_x - total_width * scale / 2;
    float draw_y = center_y - 32 * scale / 2;

    x = draw_x;
    for (const char *t = text; *t; t++) {
        stbtt_aligned_quad q;
        stbtt_GetBakedQuad(cdata, 1024, 1024, *t - 32, &x, &draw_y, &q, 1);
        int w = (int)(q.x1 - q.x0), h = (int)(q.y1 - q.y0);
        int ix = (int)q.x0, iy = (int)q.y0;
        draw_glyph(buf, ix, iy, NULL, 0, 0, alpha, color); // optional effect spot
    }
}

void init_touch(int screen_w, int screen_h) {
    num_touch_devs = 0;
    for (int i = 0; i < 16 && num_touch_devs < MAX_TOUCH_DEVICES; i++) {
        char path[64];
        snprintf(path, sizeof(path), "/dev/input/event%d", i);
        int fd = open(path, O_RDONLY | O_NONBLOCK);
        if (fd < 0) continue;

        struct input_absinfo ax, ay;
        if (ioctl(fd, EVIOCGABS(ABS_X), &ax) < 0 || ioctl(fd, EVIOCGABS(ABS_Y), &ay) < 0) {
            close(fd); continue;
        }

        touch_devs[num_touch_devs++] = (TouchDev){fd, ax.minimum, ax.maximum, ay.minimum, ay.maximum};
    }
}

void read_touch() {
    touch.just_pressed = 0;

    struct pollfd fds[MAX_TOUCH_DEVICES];
    for (int i = 0; i < num_touch_devs; i++) {
        fds[i].fd = touch_devs[i].fd;
        fds[i].events = POLLIN;
    }

    if (poll(fds, num_touch_devs, 0) <= 0) return;

    for (int i = 0; i < num_touch_devs; i++) {
        if (!(fds[i].revents & POLLIN)) continue;
        struct input_event ev;
        while (read(touch_devs[i].fd, &ev, sizeof(ev)) == sizeof(ev)) {
            if (ev.type == EV_ABS) {
                if (ev.code == ABS_X || ev.code == ABS_MT_POSITION_X)
                    touch.x = (ev.value - touch_devs[i].min_x) * screen_w / (touch_devs[i].max_x - touch_devs[i].min_x + 1);
                if (ev.code == ABS_Y || ev.code == ABS_MT_POSITION_Y)
                    touch.y = (ev.value - touch_devs[i].min_y) * screen_h / (touch_devs[i].max_y - touch_devs[i].min_y + 1);
                if (ev.code == ABS_MT_TRACKING_ID) {
                    if (ev.value == -1) touch.pressed = 0;
                    else { touch.pressed = 1; touch.just_pressed = 1; }
                }
            } else if (ev.type == EV_KEY && ev.code == BTN_TOUCH) {
                if (!touch.pressed && ev.value) touch.just_pressed = 1;
                touch.pressed = ev.value;
            }
        }
    }
}

uint64_t now_ns() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

int main() {
    // Load font
    FILE *f = fopen(FONT_PATH, "rb");
    if (!f) { perror("Font load failed"); exit(1); }

    fseek(f, 0, SEEK_END);
    int size = ftell(f);
    rewind(f);
    unsigned char *ttf = malloc(size);
    fread(ttf, 1, size, f);
    fclose(f);

    unsigned char *bitmap = calloc(1, 1024 * 1024);
    stbtt_bakedchar cdata[96];
    stbtt_BakeFontBitmap(ttf, 0, 64.0f, bitmap, 1024, 1024, 32, 96, cdata);
    free(ttf);

    struct fb_var_screeninfo vinfo;
    struct fb_fix_screeninfo finfo;
    fb_fd = open("/dev/fb0", O_RDWR);
    ioctl(fb_fd, FBIOGET_VSCREENINFO, &vinfo);
    ioctl(fb_fd, FBIOGET_FSCREENINFO, &finfo);
    screen_w = vinfo.xres;
    screen_h = vinfo.yres;
    stride = finfo.line_length;

    fbp = mmap(0, stride * screen_h, PROT_READ | PROT_WRITE, MAP_SHARED, fb_fd, 0);

    init_touch(screen_w, screen_h);

    uint64_t start = now_ns();
    float base_scale = 1.0f;
    float pulse = 0.0f;

    while (1) {
        read_touch();

        if (touch.just_pressed) pulse = 1.0f;

        if (pulse > 0) {
            base_scale = 1.0f + 0.3f * sinf(pulse * 3.14f);
            pulse -= 0.05f;
        } else {
            base_scale = 1.0f;
        }

        uint64_t elapsed = now_ns() - start;
        float alpha = elapsed < 2e9 ? elapsed / 2e9f : 1.0f;

        clear(fbp, COLOR_BG);
        draw_text_centered(fbp, "Welcome to Phone OS", cdata, base_scale, screen_w / 2, screen_h / 2, alpha, COLOR_TEXT);

        usleep(16000);
    }

    munmap(fbp, stride * screen_h);
    close(fb_fd);
    free(bitmap);
    return 0;
}
