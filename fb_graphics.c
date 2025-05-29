#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <linux/fb.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <stdint.h>
#include <signal.h>

// Define _GNU_SOURCE for aligned_alloc if not already defined
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

// Portrait mode: 1080x1920
#define WIDTH 1080
#define HEIGHT 1920
#define TARGET_FPS 60
#define FRAME_TIME_NS (1000000000 / TARGET_FPS)

// Optimized color macros for 32-bit ARGB
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

static volatile int running = 1;
static FrameBuffer fb;

void signal_handler(int sig) {
    running = 0;
}

// Fast rectangle fill using 64-bit writes where possible
static inline void fast_fill_rect(uint32_t *buffer, int x, int y, int w, int h, uint32_t color, int screen_width, int screen_height) {
    if (x < 0 || y < 0 || x + w > screen_width || y + h > screen_height) return;
    
    uint64_t color64 = ((uint64_t)color << 32) | color;
    
    for (int row = y; row < y + h; row++) {
        uint32_t *line = &buffer[row * screen_width + x];
        int pixels = w;
        
        // Align to 8-byte boundary if needed
        if ((uintptr_t)line & 4 && pixels > 0) {
            *line++ = color;
            pixels--;
        }
        
        // Fill 2 pixels at a time with 64-bit writes
        uint64_t *line64 = (uint64_t*)line;
        int pairs = pixels / 2;
        for (int i = 0; i < pairs; i++) {
            *line64++ = color64;
        }
        
        // Handle remaining pixel
        if (pixels & 1) {
            *(uint32_t*)line64 = color;
        }
    }
}

// Fast circle using integer math
static inline void fast_fill_circle(uint32_t *buffer, int cx, int cy, int radius, uint32_t color, int screen_width, int screen_height) {
    int r2 = radius * radius;
    int x_start = cx - radius;
    int x_end = cx + radius;
    int y_start = cy - radius;
    int y_end = cy + radius;
    
    // Clip to screen bounds
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
            if (dx * dx + dy2 <= r2) {
                line[x] = color;
            }
        }
    }
}

// Clear screen with efficient memset
static inline void clear_screen(uint32_t *buffer, uint32_t color, int screen_width, int screen_height) {
    int total_pixels = screen_width * screen_height;
    
    if (color == 0) {
        memset(buffer, 0, total_pixels * sizeof(uint32_t));
    } else {
        // For non-zero colors, we need to fill properly
        uint64_t color64 = ((uint64_t)color << 32) | color;
        uint64_t *buffer64 = (uint64_t*)buffer;
        size_t pairs = total_pixels / 2;
        
        for (size_t i = 0; i < pairs; i++) {
            buffer64[i] = color64;
        }
        
        // Handle odd pixel count
        if (total_pixels & 1) {
            buffer[total_pixels - 1] = color;
        }
    }
}

// Get high resolution timestamp
static inline uint64_t get_time_ns() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

int init_framebuffer() {
    // Open framebuffer device
    fb.fb_fd = open("/dev/fb0", O_RDWR);
    if (fb.fb_fd == -1) {
        perror("Error opening framebuffer");
        return -1;
    }

    // Get fixed screen information first
    if (ioctl(fb.fb_fd, FBIOGET_FSCREENINFO, &fb.finfo) == -1) {
        perror("Error reading fixed screen info");
        return -1;
    }

    // Get variable screen information
    if (ioctl(fb.fb_fd, FBIOGET_VSCREENINFO, &fb.vinfo) == -1) {
        perror("Error reading variable screen info");
        return -1;
    }

    printf("Current screen: %dx%d, %d bpp, line_length=%d\n", 
           fb.vinfo.xres, fb.vinfo.yres, fb.vinfo.bits_per_pixel, fb.finfo.line_length);
    
    // Use current resolution if different from target
    int actual_width = fb.vinfo.xres;
    int actual_height = fb.vinfo.yres;
    
    if (fb.vinfo.xres != WIDTH || fb.vinfo.yres != HEIGHT) {
        printf("Note: Using actual resolution %dx%d instead of target %dx%d\n", 
               actual_width, actual_height, WIDTH, HEIGHT);
    }

    // Calculate screen size using actual framebuffer parameters
    fb.screensize = fb.finfo.line_length * fb.vinfo.yres;
    
    printf("Framebuffer size: %zu bytes (line_length=%d)\n", fb.screensize, fb.finfo.line_length);

    // Try to memory map the framebuffer (single buffer first)
    fb.framebuffer = (uint32_t*)mmap(0, fb.screensize, PROT_READ | PROT_WRITE, MAP_SHARED, fb.fb_fd, 0);
    if (fb.framebuffer == MAP_FAILED) {
        perror("Error mapping framebuffer");
        return -1;
    }

    // Allocate back buffer in system memory for double buffering
    if (posix_memalign((void**)&fb.backbuffer, 64, actual_width * actual_height * sizeof(uint32_t)) != 0) {
        perror("Error allocating back buffer");
        munmap(fb.framebuffer, fb.screensize);
        return -1;
    }
    
    // Clear both buffers
    memset(fb.framebuffer, 0, fb.screensize);
    memset(fb.backbuffer, 0, actual_width * actual_height * sizeof(uint32_t));

    printf("Framebuffer initialized: %dx%d, %zu bytes\n", actual_width, actual_height, fb.screensize);
    return 0;
}

void cleanup_framebuffer() {
    if (fb.framebuffer != MAP_FAILED) {
        munmap(fb.framebuffer, fb.screensize);
    }
    if (fb.backbuffer) {
        free(fb.backbuffer);
    }
    if (fb.fb_fd >= 0) {
        close(fb.fb_fd);
    }
}

// Copy back buffer to front buffer (software double buffering)
void swap_buffers() {
    // Get actual dimensions
    int actual_width = fb.vinfo.xres;
    int actual_height = fb.vinfo.yres;
    
    // Copy back buffer to framebuffer
    if (fb.finfo.line_length == actual_width * sizeof(uint32_t)) {
        // Direct copy if line lengths match
        memcpy(fb.framebuffer, fb.backbuffer, actual_width * actual_height * sizeof(uint32_t));
    } else {
        // Copy line by line if there's padding
        uint32_t *src = fb.backbuffer;
        uint8_t *dst = (uint8_t*)fb.framebuffer;
        
        for (int y = 0; y < actual_height; y++) {
            memcpy(dst, src, actual_width * sizeof(uint32_t));
            src += actual_width;
            dst += fb.finfo.line_length;
        }
    }
}

void render_frame(MovingRect *rects, int num_rects, uint64_t frame_count) {
    // Get actual screen dimensions
    int screen_width = fb.vinfo.xres;
    int screen_height = fb.vinfo.yres;
    
    // Clear back buffer
    clear_screen(fb.backbuffer, COLOR_BLACK, screen_width, screen_height);
    
    // Update and draw moving rectangles
    for (int i = 0; i < num_rects; i++) {
        MovingRect *rect = &rects[i];
        
        // Update position
        rect->x += rect->dx;
        rect->y += rect->dy;
        
        // Bounce off edges
        if (rect->x <= 0 || rect->x >= screen_width - rect->size) {
            rect->dx = -rect->dx;
            rect->x = (rect->x <= 0) ? 0 : screen_width - rect->size;
        }
        if (rect->y <= 0 || rect->y >= screen_height - rect->size) {
            rect->dy = -rect->dy;
            rect->y = (rect->y <= 0) ? 0 : screen_height - rect->size;
        }
        
        // Draw rectangle
        fast_fill_rect(fb.backbuffer, (int)rect->x, (int)rect->y, rect->size, rect->size, rect->color, screen_width, screen_height);
    }
    
    // Draw some animated circles for visual interest
    int num_circles = 5;
    for (int i = 0; i < num_circles; i++) {
        float angle = (frame_count * 0.02f) + (i * 2.0f * M_PI / num_circles);
        int cx = screen_width/2 + (int)(200 * cos(angle));
        int cy = screen_height/2 + (int)(300 * sin(angle));
        int radius = 30 + (int)(20 * sin(frame_count * 0.05f + i));
        
        uint32_t colors[] = {COLOR_RED, COLOR_GREEN, COLOR_BLUE, COLOR_CYAN, COLOR_MAGENTA};
        fast_fill_circle(fb.backbuffer, cx, cy, radius, colors[i], screen_width, screen_height);
    }
    
    // Draw frame counter
    char fps_text[32];
    snprintf(fps_text, sizeof(fps_text), "Frame: %lu", frame_count);
    
    // Simple text as rectangles (just for demo)
    int text_x = 10;
    int text_y = 10;
    for (int i = 0; fps_text[i] && i < 20; i++) {
        fast_fill_rect(fb.backbuffer, text_x + i * 12, text_y, 8, 16, COLOR_WHITE, screen_width, screen_height);
    }
}

int main() {
    printf("Orange Pi 5 High-Performance Framebuffer Demo\n");
    printf("Target: 1920x1080 portrait @ 60fps\n");
    
    // Set up signal handler for clean exit
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // Initialize framebuffer
    if (init_framebuffer() < 0) {
        return 1;
    }
    
    // Initialize moving rectangles
    MovingRect rects[8];
    int screen_width = fb.vinfo.xres;
    int screen_height = fb.vinfo.yres;
    
    for (int i = 0; i < 8; i++) {
        rects[i].x = rand() % (screen_width - 100);
        rects[i].y = rand() % (screen_height - 100);
        rects[i].dx = (rand() % 10 - 5) * 2.0f;
        rects[i].dy = (rand() % 10 - 5) * 2.0f;
        rects[i].size = 50 + rand() % 50;
        
        uint32_t colors[] = {COLOR_RED, COLOR_GREEN, COLOR_BLUE, COLOR_CYAN, 
                            COLOR_MAGENTA, COLOR_YELLOW, COLOR_WHITE};
        rects[i].color = colors[i % 7];
    }
    
    uint64_t frame_count = 0;
    uint64_t last_time = get_time_ns();
    uint64_t fps_counter = 0;
    uint64_t fps_time = last_time;
    
    printf("Starting render loop...\n");
    
    while (running) {
        uint64_t frame_start = get_time_ns();
        
        // Render frame
        render_frame(rects, 8, frame_count);
        
        // Swap buffers
        swap_buffers();
        
        frame_count++;
        fps_counter++;
        
        // Print FPS every second
        if (frame_start - fps_time >= 1000000000) {
            printf("FPS: %lu, Frame: %lu\n", fps_counter, frame_count);
            fps_counter = 0;
            fps_time = frame_start;
        }
        
        // Frame rate limiting
        uint64_t frame_end = get_time_ns();
        uint64_t frame_duration = frame_end - frame_start;
        
        if (frame_duration < FRAME_TIME_NS) {
            uint64_t sleep_time = FRAME_TIME_NS - frame_duration;
            struct timespec ts = {
                .tv_sec = sleep_time / 1000000000,
                .tv_nsec = sleep_time % 1000000000
            };
            nanosleep(&ts, NULL);
        }
        
        last_time = frame_start;
    }
    
    printf("\nCleaning up...\n");
    cleanup_framebuffer();
    return 0;
}