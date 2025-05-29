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
#include <string.h>
#include <math.h>
#include <time.h>

#define FONT_PATH "phone-os/Inter-Regular.otf"
#define COLOR_BG 0xFF1A1A1A
#define COLOR_TEXT 0xFFFFFFFF
#define FONT_HEIGHT 64.0f
#define LETTER_SPACING 2.0f

uint32_t *fbp = NULL;
int fb_fd, screen_w, screen_h, stride;

int touch_x = -1, touch_y = -1;
int touch_down = 0;

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

void draw_text(uint32_t *buf, stbtt_fontinfo *font, const char *text, float scale, int x, int y, uint32_t color) {
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

void init_fb() {
    struct fb_var_screeninfo vinfo;
    struct fb_fix_screeninfo finfo;
    fb_fd = open("/dev/fb0", O_RDWR);
    ioctl(fb_fd, FBIOGET_VSCREENINFO, &vinfo);
    ioctl(fb_fd, FBIOGET_FSCREENINFO, &finfo);
    screen_w = vinfo.xres;
    screen_h = vinfo.yres;
    stride = finfo.line_length;
    fbp = mmap(0, stride * screen_h, PROT_READ | PROT_WRITE, MAP_SHARED, fb_fd, 0);
}

void init_touch() {
    for (int i = 0; i < 16; i++) {
        char path[64];
        snprintf(path, sizeof(path), "/dev/input/event%d", i);
        int fd = open(path, O_RDONLY | O_NONBLOCK);
        if (fd < 0) continue;

        struct input_absinfo abs_x, abs_y;
        if (ioctl(fd, EVIOCGABS(ABS_X), &abs_x) == 0 && ioctl(fd, EVIOCGABS(ABS_Y), &abs_y) == 0) {
            printf("Using touch device: %s\n", path);
            struct input_event ev;
            while (1) {
                if (read(fd, &ev, sizeof(ev)) == sizeof(ev)) {
                    if (ev.type == EV_ABS) {
                        if (ev.code == ABS_X || ev.code == ABS_MT_POSITION_X)
                            touch_x = ev.value * screen_w / (abs_x.maximum + 1);
                        if (ev.code == ABS_Y || ev.code == ABS_MT_POSITION_Y)
                            touch_y = ev.value * screen_h / (abs_y.maximum + 1);
                    }
                    if (ev.type == EV_KEY && ev.code == BTN_TOUCH)
                        touch_down = ev.value;
                    if (touch_down) printf("Touch at %d,%d\n", touch_x, touch_y);
                } else {
                    usleep(16000);
                }
            }
        }
        close(fd);
    }
}

int main() {
    init_fb();

    FILE *f = fopen(FONT_PATH, "rb");
    if (!f) { perror("Font load failed"); return 1; }
    fseek(f, 0, SEEK_END);
    int size = ftell(f);
    rewind(f);
    unsigned char *ttf = malloc(size);
    fread(ttf, 1, size, f);
    fclose(f);

    stbtt_fontinfo font;
    stbtt_InitFont(&font, ttf, 0);
    float scale = stbtt_ScaleForPixelHeight(&font, FONT_HEIGHT);

    clear(fbp, COLOR_BG);
    const char *msg = "Welcome to Phone OS";
    int text_w = measure_text_width(&font, msg, scale);
    int x = (screen_w - text_w) / 2;
    int y = screen_h / 2;
    draw_text(fbp, &font, msg, scale, x, y, COLOR_TEXT);

    init_touch();

    munmap(fbp, stride * screen_h);
    close(fb_fd);
    free(ttf);
    return 0;
}
