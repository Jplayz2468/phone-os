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
#define MAX_TOUCH_DEVICES 32
#define FONT_HEIGHT 64.0f
#define LETTER_SPACING 2.0f

uint32_t *fbp = NULL;
int fb_fd, screen_w, screen_h, stride;

typedef struct {
    int fd, min_x, max_x, min_y, max_y;
} TouchDev;

TouchDev touch_devs[MAX_TOUCH_DEVICES];
int num_touch = 0;

typedef struct {
    int x, y, pressed, just_pressed;
} TouchState;

TouchState touch = {0};

void clear(uint32_t *buf, uint32_t color) {
    for (int i = 0; i < screen_w * screen_h; i++) buf[i] = color;
}

int measure_text_width(stbtt_fontinfo *font, const char *text, float scale) {
    int width = 0;
    for (const char *p = text; *p; p++) {
        int ax;
        stbtt_GetCodepointHMetrics(font, *p, &ax, NULL);
        width += (int)(ax * scale + LETTER_SPACING);
    }
    return width;
}

void draw_text(uint32_t *buf, stbtt_fontinfo *font, const char *text, float scale, int x, int y, float alpha, uint32_t color) {
    int ascent;
    stbtt_GetFontVMetrics(font, &ascent, NULL, NULL);
    int baseline = (int)(ascent * scale);

    int px = x;
    for (const char *p = text; *p; p++) {
        int ch = *p;
        int ax;
        stbtt_GetCodepointHMetrics(font, ch, &ax, NULL);
        int c_x1, c_y1, c_x2, c_y2;
        stbtt_GetCodepointBitmapBox(font, ch, scale, scale, &c_x1, &c_y1, &c_x2, &c_y2);
        int w = c_x2 - c_x1;
        int h = c_y2 - c_y1;
        unsigned char *bitmap = malloc(w * h);
        stbtt_MakeCodepointBitmap(font, bitmap, w, h, w, scale, scale, ch);
        for (int row = 0; row < h; row++) {
            for (int col = 0; col < w; col++) {
                int a = bitmap[row * w + col];
                if (a > 0) {
                    int dx = px + c_x1 + col;
                    int dy = y + baseline + c_y1 + row;
                    if (dx >= 0 && dx < screen_w && dy >= 0 && dy < screen_h) {
                        buf[dy * screen_w + dx] = color;
                    }
                }
            }
        }
        free(bitmap);
        px += (int)(ax * scale + LETTER_SPACING);
    }
}

void init_touch() {
    num_touch = 0;
    for (int i = 0; i < 32; i++) {
        char path[64];
        snprintf(path, sizeof(path), "/dev/input/event%d", i);
        int fd = open(path, O_RDONLY | O_NONBLOCK);
        if (fd < 0) continue;

        struct input_absinfo ax, ay;
        if (ioctl(fd, EVIOCGABS(ABS_X), &ax) < 0 || ioctl(fd, EVIOCGABS(ABS_Y), &ay) < 0) {
            printf("Skipped non-touch device: %s\n", path);
            close(fd);
            continue;
        }

        touch_devs[num_touch++] = (TouchDev){fd, ax.minimum, ax.maximum, ay.minimum, ay.maximum};
        printf("Added touch device %s (fd=%d)\n", path, fd);
    }
}

void read_touch() {
    touch.just_pressed = 0;
    struct pollfd fds[MAX_TOUCH_DEVICES];
    for (int i = 0; i < num_touch; i++) {
        fds[i] = (struct pollfd){touch_devs[i].fd, POLLIN, 0};
    }
    if (poll(fds, num_touch, 0) <= 0) return;

    for (int i = 0; i < num_touch; i++) {
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
    if (touch.just_pressed) printf("Touch at %d,%d\n", touch.x, touch.y);
}

uint64_t now_ns() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

int main() {
    FILE *f = fopen(FONT_PATH, "rb");
    if (!f) { perror("Font load failed"); exit(1); }

    fseek(f, 0, SEEK_END); int size = ftell(f); rewind(f);
    unsigned char *ttf = malloc(size); fread(ttf, 1, size, f); fclose(f);

    stbtt_fontinfo font;
    if (!stbtt_InitFont(&font, ttf, 0)) {
        fprintf(stderr, "Failed to initialize font\n");
        return -1;
    }

    float scale = stbtt_ScaleForPixelHeight(&font, FONT_HEIGHT);

    struct fb_var_screeninfo vinfo;
    struct fb_fix_screeninfo finfo;
    fb_fd = open("/dev/fb0", O_RDWR);
    ioctl(fb_fd, FBIOGET_VSCREENINFO, &vinfo);
    ioctl(fb_fd, FBIOGET_FSCREENINFO, &finfo);
    screen_w = vinfo.xres; screen_h = vinfo.yres; stride = finfo.line_length;
    fbp = mmap(0, stride * screen_h, PROT_READ | PROT_WRITE, MAP_SHARED, fb_fd, 0);

    uint32_t *backbuf = malloc(screen_w * screen_h * sizeof(uint32_t));

    init_touch();
    uint64_t start = now_ns();
    float pulse = 0.0f;

    while (1) {
        read_touch();
        if (touch.just_pressed) pulse = 1.0f;
        if (pulse > 0) pulse -= 0.05f;

        float elapsed = (now_ns() - start) / 1e9f;
        float alpha = fmin(1.0f, elapsed / 0.3f);
        float scroll_y = elapsed < 0.3f ? (1.0f - alpha) * screen_h : 0.0f;
        float scale_mod = 1.0f + 0.2f * sinf(pulse * 3.14f);

        clear(backbuf, COLOR_BG);

        const char *text = "Welcome to Phone OS";
        float final_scale = scale * scale_mod;
        int text_w = measure_text_width(&font, text, final_scale);
        int x = (screen_w - text_w) / 2;
        int y = screen_h - (screen_h / 6) + (int)scroll_y;

        draw_text(backbuf, &font, text, final_scale, x, y, alpha, COLOR_TEXT);
        memcpy(fbp, backbuf, screen_w * screen_h * sizeof(uint32_t));

        usleep(16000);
    }

    munmap(fbp, stride * screen_h);
    close(fb_fd);
    free(ttf);
    free(backbuf);
    return 0;
}
