#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/fb.h>
#include <sys/mman.h>
#include <string.h>
#include <time.h>

#define FONT_PATH "phone-os/Inter-Regular.otf"
#define COLOR_BG 0xFF1A1A1A
#define COLOR_TEXT 0xFFFFFFFF

uint32_t *fbp = NULL;
int fb_fd, screen_w, screen_h, stride;

void clear(uint32_t *buf, uint32_t color) {
    for (int i = 0; i < screen_w * screen_h; i++) buf[i] = color;
}

void draw_glyph(uint32_t *buf, int x, int y, unsigned char *bitmap, int bw, int bh, float alpha, uint32_t color) {
    for (int dy = 0; dy < bh; dy++) {
        for (int dx = 0; dx < bw; dx++) {
            int a = bitmap[dy * bw + dx];
            if (a > 16) {
                int px = x + dx, py = y + dy;
                if (px >= 0 && px < screen_w && py >= 0 && py < screen_h) {
                    uint32_t c = color & 0xFFFFFF;
                    int af = (int)(a * alpha);
                    buf[py * screen_w + px] =
                        ((af & 0xFF) << 24) | c;
                }
            }
        }
    }
}

void draw_text(uint32_t *buf, float x, float y, const char *text, stbtt_bakedchar *cdata, float alpha, uint32_t color) {
    while (*text) {
        if (*text >= 32 && *text < 128) {
            stbtt_aligned_quad q;
            stbtt_GetBakedQuad(cdata, 1024, 1024, *text - 32, &x, &y, &q, 1);
            int w = (int)(q.x1 - q.x0);
            int h = (int)(q.y1 - q.y0);
            int ix = (int)q.x0;
            int iy = (int)q.y0;
            draw_glyph(buf, ix, iy, NULL, 0, 0, alpha, color);  // placeholder if you want per-char effects
        }
        text++;
    }
}

int main() {
    FILE *f = fopen(FONT_PATH, "rb");
    if (!f) { perror("Font load failed"); exit(1); }

    fseek(f, 0, SEEK_END); int size = ftell(f); rewind(f);
    unsigned char *ttf = malloc(size);
    fread(ttf, 1, size, f); fclose(f);

    unsigned char *bitmap = calloc(1, 1024 * 1024);
    stbtt_bakedchar cdata[96];
    stbtt_BakeFontBitmap(ttf, 0, 64.0, bitmap, 1024, 1024, 32, 96, cdata);
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

    int cx = screen_w / 2 - 200;
    int cy = screen_h / 2 - 50;

    for (int frame = 0; frame < 90; frame++) {
        float alpha = frame < 30 ? (frame / 30.0f) : (frame > 60 ? (1 - (frame - 60) / 30.0f) : 1);
        int offset = (frame < 20) ? (20 - frame) * 2 : 0;

        clear(fbp, COLOR_BG);
        draw_text(fbp, cx, cy + offset, "Welcome to Phone OS", cdata, alpha, COLOR_TEXT);
        usleep(16000);
    }

    munmap(fbp, stride * screen_h);
    close(fb_fd);
    free(bitmap);
    return 0;
}
