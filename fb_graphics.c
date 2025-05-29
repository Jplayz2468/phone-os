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

#define FONT_PATH "phone-os/Inter-Regular.otf"
#define COLOR_BG 0xFF1A1A1A
#define COLOR_TEXT 0xFFFFFFFF

uint32_t *fbp = NULL;
int fb_fd, screen_w, screen_h, stride;

void draw_char(uint32_t *fb, int x0, int y0, stbtt_bakedchar *cdata, char ch, unsigned char *bitmap, int bw, int bh, uint32_t color) {
    stbtt_bakedchar *b = &cdata[ch - 32];
    int w = b->x1 - b->x0, h = b->y1 - b->y0;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int alpha = bitmap[(y + b->y0) * bw + (x + b->x0)];
            if (alpha > 64) {
                int px = x0 + x, py = y0 + y;
                if (px >= 0 && px < screen_w && py >= 0 && py < screen_h)
                    fb[py * screen_w + px] = color;
            }
        }
    }
}

void draw_text(uint32_t *fb, int x, int y, const char *text, stbtt_bakedchar *cdata, unsigned char *bitmap, int bw, int bh, uint32_t color) {
    while (*text) {
        draw_char(fb, x, y, cdata, *text, bitmap, bw, bh, color);
        x += 18;
        text++;
    }
}

int main() {
    // Load font from correct path
    FILE *f = fopen(FONT_PATH, "rb");
    if (!f) { perror("Font load failed"); exit(1); }

    fseek(f, 0, SEEK_END);
    int size = ftell(f);
    rewind(f);
    unsigned char *ttf = malloc(size);
    fread(ttf, 1, size, f);
    fclose(f);

    stbtt_bakedchar cdata[96];
    unsigned char *bitmap = malloc(512 * 512);
    stbtt_BakeFontBitmap(ttf, 0, 32.0, bitmap, 512, 512, 32, 96, cdata);
    free(ttf);

    // Framebuffer setup
    struct fb_var_screeninfo vinfo;
    struct fb_fix_screeninfo finfo;
    fb_fd = open("/dev/fb0", O_RDWR);
    if (fb_fd < 0) { perror("Framebuffer open failed"); return 1; }

    ioctl(fb_fd, FBIOGET_VSCREENINFO, &vinfo);
    ioctl(fb_fd, FBIOGET_FSCREENINFO, &finfo);
    screen_w = vinfo.xres;
    screen_h = vinfo.yres;
    stride = finfo.line_length;

    fbp = mmap(0, stride * screen_h, PROT_READ | PROT_WRITE, MAP_SHARED, fb_fd, 0);

    // Clear screen
    for (int i = 0; i < screen_w * screen_h; i++) fbp[i] = COLOR_BG;

    // Draw text
    draw_text(fbp, 100, 100, "Hello from Phone OS", cdata, bitmap, 512, 512, COLOR_TEXT);

    sleep(5);

    munmap(fbp, stride * screen_h);
    close(fb_fd);
    free(bitmap);

    return 0;
}
