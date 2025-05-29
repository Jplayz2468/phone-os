#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/fb.h>

uint32_t *fb;
int fb_fd, width, height, stride;

void draw_char(uint32_t *fb, int x0, int y0, stbtt_bakedchar *cdata, char ch, unsigned char *bitmap, int b_w, int b_h, uint32_t color) {
    stbtt_bakedchar *b = &cdata[ch - 32];
    int w = b->x1 - b->x0;
    int h = b->y1 - b->y0;
    for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
        int alpha = bitmap[(y + b->y0) * b_w + (x + b->x0)];
        if (alpha > 128) fb[(y0 + y) * width + (x0 + x)] = color;
    }
}
void draw_text(uint32_t *fb, int x, int y, const char *text, stbtt_bakedchar *cdata, unsigned char *bitmap, int bw, int bh, uint32_t color) {
    while (*text) {
        draw_char(fb, x, y, cdata, *text, bitmap, bw, bh, color);
        x += 20;
        text++;
    }
}

int main() {
    // Load font
    FILE *f = fopen("Inter-Regular.otf", "rb");
    fseek(f, 0, SEEK_END); int size = ftell(f); rewind(f);
    unsigned char *ttf = malloc(size); fread(ttf, 1, size, f); fclose(f);

    stbtt_bakedchar cdata[96];
    unsigned char *bitmap = malloc(512 * 512);
    stbtt_BakeFontBitmap(ttf, 0, 32.0f, bitmap, 512, 512, 32, 96, cdata);
    free(ttf);

    // Init FB
    struct fb_var_screeninfo vinfo;
    struct fb_fix_screeninfo finfo;
    fb_fd = open("/dev/fb0", O_RDWR);
    ioctl(fb_fd, FBIOGET_VSCREENINFO, &vinfo);
    ioctl(fb_fd, FBIOGET_FSCREENINFO, &finfo);
    width = vinfo.xres; height = vinfo.yres; stride = finfo.line_length;
    fb = mmap(0, stride * height, PROT_READ | PROT_WRITE, MAP_SHARED, fb_fd, 0);

    for (int i = 0; i < width * height; i++) fb[i] = 0xFF111111;
    draw_text(fb, 100, 100, "Hello, Phone OS!", cdata, bitmap, 512, 512, 0xFFFFFFFF);

    sleep(5);
    munmap(fb, stride * height); close(fb_fd);
    free(bitmap);
    return 0;
}
