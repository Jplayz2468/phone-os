#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <linux/fb.h>
#include <linux/kd.h>
#include <linux/vt.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <stdint.h>
#include <signal.h>
#include <termios.h>

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#define WIDTH 1080
#define HEIGHT 1920
#define TARGET_FPS 60
#define FRAME_TIME_NS (1000000000 / TARGET_FPS)

#define COLOR_RED     0xFFFF0000
#define COLOR_GREEN   0xFF00FF00
#define COLOR_BLUE    0xFF0000FF
#define COLOR_WHITE   0xFFFFFFFF
#define COLOR_BLACK   0xFF000000
#define COLOR_CYAN    0xFF00FFFF
#define COLOR_MAGENTA 0xFFFF00FF
#define COLOR_YELLOW  0xFFFFFF00

typedef struct {
    uint32_t *framebuffer;
    uint32_t *backbuffer;
    int fb_fd;
    size_t screensize;
    struct fb_var_screeninfo vinfo;
    struct fb_fix_screeninfo finfo;
} FrameBuffer;

typedef struct {
    float x, y;
    float dx, dy;
    uint32_t color;
    int size;
} MovingRect;

typedef enum {
    APP_HOME,
    APP_CLOCK,
    APP_BOUNCE,
    APP_COLOR
} AppMode;

static volatile int running = 1;
static FrameBuffer fb;
static struct termios orig_termios;
static int console_fd = -1;
static int orig_kb_mode = -1;
static int orig_console_mode = -1;
static FILE *orig_stdout = NULL;
static FILE *orig_stderr = NULL;
AppMode current_app = APP_HOME;

void signal_handler(int sig) {
    running = 0;
}

void setup_terminal() {
    orig_stdout = stdout;
    orig_stderr = stderr;
    console_fd = open("/dev/console", O_RDWR);
    if (console_fd < 0) console_fd = open("/dev/tty0", O_RDWR);
    if (console_fd < 0) console_fd = STDIN_FILENO;
    if (ioctl(console_fd, KDGKBMODE, &orig_kb_mode) == 0) ioctl(console_fd, KDSKBMODE, K_RAW);
    if (ioctl(console_fd, KDGETMODE, &orig_console_mode) == 0) ioctl(console_fd, KDSETMODE, KD_GRAPHICS);
    tcgetattr(STDIN_FILENO, &orig_termios);
    struct termios raw = orig_termios;
    raw.c_lflag &= ~(ECHO | ICANON | ISIG);
    raw.c_iflag &= ~(IXON | ICRNL);
    raw.c_oflag &= ~OPOST;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
    printf("\033[?25l\033[?7l\033[?47h\033[2J\033[H\033[0m");
    fflush(stdout);
    freopen("/dev/null", "w", stdout);
    freopen("/dev/null", "w", stderr);
}

void restore_terminal() {
    if (orig_stdout) {
        stdout = orig_stdout;
        stderr = orig_stderr;
    }
    printf("Restoring terminal...\n");
    if (console_fd >= 0 && orig_console_mode >= 0) ioctl(console_fd, KDSETMODE, orig_console_mode);
    if (console_fd >= 0 && orig_kb_mode >= 0) ioctl(console_fd, KDSKBMODE, orig_kb_mode);
    printf("\033[?47l\033[?25h\033[?7h\033[0m\033[2J\033[H");
    fflush(stdout);
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
    if (console_fd >= 0 && console_fd != STDIN_FILENO) close(console_fd);
}

static inline void fast_fill_rect(uint32_t *buffer, int x, int y, int w, int h, uint32_t color, int screen_width, int screen_height) {
    if (x < 0 || y < 0 || x + w > screen_width || y + h > screen_height) return;
    uint64_t color64 = ((uint64_t)color << 32) | color;
    for (int row = y; row < y + h; row++) {
        uint32_t *line = &buffer[row * screen_width + x];
        int pixels = w;
        if ((uintptr_t)line & 4 && pixels > 0) {
            *line++ = color;
            pixels--;
        }
        uint64_t *line64 = (uint64_t*)line;
        for (int i = 0; i < pixels / 2; i++) {
            *line64++ = color64;
        }
        if (pixels & 1) {
            *(uint32_t*)line64 = color;
        }
    }
}

static inline void fast_fill_circle(uint32_t *buffer, int cx, int cy, int radius, uint32_t color, int screen_width, int screen_height) {
    int r2 = radius * radius;
    int x_start = cx - radius;
    int x_end = cx + radius;
    int y_start = cy - radius;
    int y_end = cy + radius;
    if (x_start < 0) x_start = 0;
    if (x_end >= screen_width) x_end = screen_width - 1;
    if (y_start < 0) y_start = 0;
    if (y_end >= screen_height) y_end = screen_height - 1;
    for (int y = y_start; y <= y_end; y++) {
        int dy = y - cy;
        int dy2 = dy * dy;
        uint32_t *line = &buffer[y * screen_width];
        for (int x = x_start; x <= x_end; x++) {
            int dx = x - cx;
            if (dx * dx + dy2 <= r2) line[x] = color;
        }
    }
}

static inline void clear_screen(uint32_t *buffer, uint32_t color, int screen_width, int screen_height) {
    int total_pixels = screen_width * screen_height;
    if (color == 0) {
        memset(buffer, 0, total_pixels * sizeof(uint32_t));
    } else {
        uint64_t color64 = ((uint64_t)color << 32) | color;
        uint64_t *buffer64 = (uint64_t*)buffer;
        for (size_t i = 0; i < total_pixels / 2; i++) {
            buffer64[i] = color64;
        }
        if (total_pixels & 1) {
            buffer[total_pixels - 1] = color;
        }
    }
}

static inline uint64_t get_time_ns() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

int init_framebuffer() {
    fb.fb_fd = open("/dev/fb0", O_RDWR);
    if (fb.fb_fd == -1) return -1;
    if (ioctl(fb.fb_fd, FBIOGET_FSCREENINFO, &fb.finfo) == -1) return -1;
    if (ioctl(fb.fb_fd, FBIOGET_VSCREENINFO, &fb.vinfo) == -1) return -1;
    int w = fb.vinfo.xres;
    int h = fb.vinfo.yres;
    fb.screensize = fb.finfo.line_length * h;
    fb.framebuffer = (uint32_t*)mmap(0, fb.screensize, PROT_READ | PROT_WRITE, MAP_SHARED, fb.fb_fd, 0);
    if (fb.framebuffer == MAP_FAILED) return -1;
    if (posix_memalign((void**)&fb.backbuffer, 64, w * h * sizeof(uint32_t)) != 0) return -1;
    memset(fb.framebuffer, 0, fb.screensize);
    memset(fb.backbuffer, 0, w * h * sizeof(uint32_t));
    return 0;
}

void cleanup_framebuffer() {
    restore_terminal();
    if (fb.framebuffer != MAP_FAILED) munmap(fb.framebuffer, fb.screensize);
    if (fb.backbuffer) free(fb.backbuffer);
    if (fb.fb_fd >= 0) close(fb.fb_fd);
}

void swap_buffers() {
    int w = fb.vinfo.xres;
    int h = fb.vinfo.yres;
    if (fb.finfo.line_length == w * sizeof(uint32_t)) {
        memcpy(fb.framebuffer, fb.backbuffer, w * h * sizeof(uint32_t));
    } else {
        uint32_t *src = fb.backbuffer;
        uint8_t *dst = (uint8_t*)fb.framebuffer;
        for (int y = 0; y < h; y++) {
            memcpy(dst, src, w * sizeof(uint32_t));
            src += w;
            dst += fb.finfo.line_length;
        }
    }
}

void draw_home_screen() {
    clear_screen(fb.backbuffer, COLOR_BLACK, fb.vinfo.xres, fb.vinfo.yres);
    fast_fill_rect(fb.backbuffer, 100, 300, 300, 300, COLOR_RED, fb.vinfo.xres, fb.vinfo.yres);
    fast_fill_rect(fb.backbuffer, 400, 300, 300, 300, COLOR_GREEN, fb.vinfo.xres, fb.vinfo.yres);
    fast_fill_rect(fb.backbuffer, 700, 300, 300, 300, COLOR_BLUE, fb.vinfo.xres, fb.vinfo.yres);
}

void draw_clock(uint64_t frame_count) {
    clear_screen(fb.backbuffer, COLOR_BLACK, fb.vinfo.xres, fb.vinfo.yres);
    time_t t = time(NULL);
    struct tm *tm_info = localtime(&t);
    int hour = tm_info->tm_hour;
    int minute = tm_info->tm_min;
    for (int i = 0; i < hour; ++i) fast_fill_circle(fb.backbuffer, 100 + i * 40, 500, 15, COLOR_RED, fb.vinfo.xres, fb.vinfo.yres);
    for (int i = 0; i < minute; i += 5) fast_fill_circle(fb.backbuffer, 100 + i * 10, 600, 10, COLOR_WHITE, fb.vinfo.xres, fb.vinfo.yres);
}

void draw_color_cycle(uint64_t frame_count) {
    uint32_t color = 0xFF000000 | (((frame_count * 5) % 256) << 16) | (((frame_count * 3) % 256) << 8) | ((frame_count * 2) % 256);
    clear_screen(fb.backbuffer, color, fb.vinfo.xres, fb.vinfo.yres);
}

void render_frame(MovingRect *rects, int num_rects, uint64_t frame_count) {
    clear_screen(fb.backbuffer, COLOR_BLACK, fb.vinfo.xres, fb.vinfo.yres);
    for (int i = 0; i < num_rects; i++) {
        rects[i].x += rects[i].dx;
        rects[i].y += rects[i].dy;
        if (rects[i].x <= 0 || rects[i].x >= fb.vinfo.xres - rects[i].size) rects[i].dx = -rects[i].dx;
        if (rects[i].y <= 0 || rects[i].y >= fb.vinfo.yres - rects[i].size) rects[i].dy = -rects[i].dy;
        fast_fill_rect(fb.backbuffer, (int)rects[i].x, (int)rects[i].y, rects[i].size, rects[i].size, rects[i].color, fb.vinfo.xres, fb.vinfo.yres);
    }
}

int main() {
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    setup_terminal();
    if (init_framebuffer() < 0) {
        restore_terminal();
        return 1;
    }

    MovingRect rects[8];
    for (int i = 0; i < 8; i++) {
        rects[i].x = rand() % (fb.vinfo.xres - 100);
        rects[i].y = rand() % (fb.vinfo.yres - 100);
        rects[i].dx = (rand() % 10 - 5) * 2.0f;
        rects[i].dy = (rand() % 10 - 5) * 2.0f;
        rects[i].size = 50 + rand() % 50;
        uint32_t colors[] = {COLOR_RED, COLOR_GREEN, COLOR_BLUE, COLOR_CYAN, COLOR_MAGENTA, COLOR_YELLOW, COLOR_WHITE};
        rects[i].color = colors[i % 7];
    }

    uint64_t frame_count = 0;

    while (running) {
        uint64_t start = get_time_ns();
        switch (current_app) {
            case APP_HOME: draw_home_screen(); break;
            case APP_CLOCK: draw_clock(frame_count); break;
            case APP_BOUNCE: render_frame(rects, 8, frame_count); break;
            case APP_COLOR: draw_color_cycle(frame_count); break;
        }
        swap_buffers();

        if (read(STDIN_FILENO, &start, 1) == 1) {
            char c = (char)start;
            if (c == '0') current_app = APP_HOME;
            if (c == '1') current_app = APP_CLOCK;
            if (c == '2') current_app = APP_BOUNCE;
            if (c == '3') current_app = APP_COLOR;
        }

        frame_count++;
        uint64_t duration = get_time_ns() - start;
        if (duration < FRAME_TIME_NS) {
            struct timespec ts = { .tv_sec = 0, .tv_nsec = FRAME_TIME_NS - duration };
            nanosleep(&ts, NULL);
        }
    }

    cleanup_framebuffer();
    return 0;
}
