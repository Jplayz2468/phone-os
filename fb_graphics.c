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
#include <signal.h>

#define FONT_PATH "phone-os/Inter-Regular.otf"
#define COLOR_BG 0xFF1A1A1A
#define COLOR_TEXT 0xFFFFFFFF
#define COLOR_INFO 0xFFAAAAAA
#define FONT_HEIGHT 64.0f
#define LETTER_SPACING 2.0f
#define MAX_TOUCH_DEVICES 32

uint32_t *fbp = NULL;
uint32_t *backbuffer = NULL;
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
    for (int i = 0; i < MAX_TOUCH_DEVICES; i++) {
        char path[64];
        snprintf(path, sizeof(path), "/dev/input/event%d", i);
        int fd = open(path, O_RDONLY | O_NONBLOCK);
        if (fd < 0) continue;

        struct input_absinfo ax, ay;
        if (ioctl(fd, EVIOCGABS(ABS_X), &ax) < 0 || ioctl(fd, EVIOCGABS(ABS_Y), &ay) < 0) {
            close(fd); continue;
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
}

uint64_t now_ns() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

float get_cpu_usage_percent() {
    static unsigned long long last_total = 0, last_idle = 0;
    FILE *f = fopen("/proc/stat", "r");
    if (!f) return 0;
    char line[256]; fgets(line, sizeof(line), f); fclose(f);
    unsigned long long user, nice, system, idle, iowait, irq, softirq;
    sscanf(line, "cpu %llu %llu %llu %llu %llu %llu %llu", &user, &nice, &system, &idle, &iowait, &irq, &softirq);
    unsigned long long total = user + nice + system + idle + iowait + irq + softirq;

    float usage = 0;
    if (last_total) {
        unsigned long long delta_total = total - last_total;
        unsigned long long delta_idle = idle - last_idle;
        usage = 100.0f * (delta_total - delta_idle) / delta_total;
    }
    last_total = total; last_idle = idle;
    return usage;
}

void cleanup(int sig) {
    if (fbp) clear(fbp, 0x00000000);
    if (backbuffer) free(backbuffer);
    if (fbp) munmap(fbp, stride * screen_h);
    if (fb_fd > 0) close(fb_fd);
    printf("\n🧹 Cleaned up framebuffer\n");
    exit(0);
}

int main() {
    signal(SIGINT, cleanup);

    FILE *f = fopen(FONT_PATH, "rb");
    if (!f) { perror("Font load failed"); exit(1); }
    fseek(f, 0, SEEK_END); int size = ftell(f); rewind(f);
    unsigned char *ttf = malloc(size); fread(ttf, 1, size, f); fclose(f);

    stbtt_fontinfo font;
    if (!stbtt_InitFont(&font, ttf, 0)) {
        fprintf(stderr, "Failed to initialize font\n"); return -1;
    }

    float scale = stbtt_ScaleForPixelHeight(&font, FONT_HEIGHT);

    struct fb_var_screeninfo vinfo;
    struct fb_fix_screeninfo finfo;
    fb_fd = open("/dev/fb0", O_RDWR);
    ioctl(fb_fd, FBIOGET_VSCREENINFO, &vinfo);
    ioctl(fb_fd, FBIOGET_FSCREENINFO, &finfo);
    screen_w = vinfo.xres; screen_h = vinfo.yres; stride = finfo.line_length;
    fbp = mmap(0, stride * screen_h, PROT_READ | PROT_WRITE, MAP_SHARED, fb_fd, 0);
    backbuffer = malloc(screen_w * screen_h * 4);

    init_touch();

    uint64_t last_time = now_ns(), last_fps_time = last_time;
    int frame_count = 0, fps = 0;
    float cpu = 0;

    while (1) {
        uint64_t frame_start = now_ns();
        read_touch();

        clear(backbuffer, COLOR_BG);

        uint64_t now = now_ns();
        float t = (now - last_time) / 1e9f;
        frame_count++;

        if ((now - last_fps_time) > 1e9f) {
            fps = frame_count;
            cpu = get_cpu_usage_percent();
            frame_count = 0;
            last_fps_time = now;
        }

        char info[128];
        snprintf(info, sizeof(info), "CPU: %.1f%%  FPS: %d", cpu, fps);
        draw_text(backbuffer, &font, info, scale * 0.3f, 20, 20, 1.0f, COLOR_INFO);

        float anim_scale = 1.0f + 0.3f * sinf(t * 2);
        int y_offset = (int)(60 * sinf(t * 1.5f));
        int x_offset = (int)(60 * cosf(t * 1.1f));
        float final_scale = scale * anim_scale;
        const char *msg = "Welcome to Phone OS";
        int text_w = measure_text_width(&font, msg, final_scale);
        int x = (screen_w - text_w) / 2 + x_offset;
        int y = screen_h / 2 + y_offset;
        draw_text(backbuffer, &font, msg, final_scale, x, y, 1.0f, COLOR_TEXT);

        memcpy(fbp, backbuffer, screen_w * screen_h * 4);

        uint64_t frame_end = now_ns();
        int sleep_us = 16666 - (int)((frame_end - frame_start) / 1000);
        if (sleep_us > 0) usleep(sleep_us);
    }

    return 0;
}
