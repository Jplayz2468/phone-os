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
#define COLOR_BG 0xFF000000
#define COLOR_WHITE 0xFFFFFFFF
#define COLOR_GRAY 0xFF666666
#define COLOR_BLUE 0xFF007AFF
#define COLOR_GREEN 0xFF34C759
#define COLOR_RED 0xFFFF3B30
#define COLOR_ORANGE 0xFFFF9500
#define COLOR_PURPLE 0xFFAF52DE
#define FONT_HEIGHT 32.0f
#define MAX_TOUCH_DEVICES 32
#define PIN_LENGTH 4

typedef enum { STATE_LOCK, STATE_HOME, STATE_APP } AppState;

typedef struct {
    int x, y, w, h;
    char *text;
    uint32_t color;
    int id;
} Button;

typedef struct {
    char *name;
    uint32_t color;
    int id;
} App;

typedef struct {
    int fd, min_x, max_x, min_y, max_y;
} TouchDev;

typedef struct {
    int x, y, pressed, last_pressed, just_pressed;
    int swipe_start_x, swipe_start_y, swiping;
} TouchState;

uint32_t *fbp = NULL, *backbuffer = NULL;
int fb_fd, screen_w, screen_h, stride;
TouchDev touch_devs[MAX_TOUCH_DEVICES];
int num_touch = 0;
TouchState touch = {0};
AppState current_state = STATE_LOCK;
char pin_input[PIN_LENGTH + 1] = {0};
int current_app = -1;
stbtt_fontinfo font;
float scale;

App apps[] = {
    {"Phone", COLOR_GREEN, 0}, {"Messages", COLOR_GREEN, 1}, {"Camera", COLOR_GRAY, 2},
    {"Photos", COLOR_BLUE, 3}, {"Settings", COLOR_GRAY, 4}, {"Calculator", COLOR_ORANGE, 5},
    {"Clock", COLOR_PURPLE, 6}, {"Weather", COLOR_BLUE, 7}, {"Maps", COLOR_GREEN, 8}
};
#define NUM_APPS (sizeof(apps)/sizeof(apps[0]))

Button pin_buttons[] = {
    {0,0,80,80,"1",COLOR_WHITE,1}, {0,0,80,80,"2",COLOR_WHITE,2}, {0,0,80,80,"3",COLOR_WHITE,3},
    {0,0,80,80,"4",COLOR_WHITE,4}, {0,0,80,80,"5",COLOR_WHITE,5}, {0,0,80,80,"6",COLOR_WHITE,6},
    {0,0,80,80,"7",COLOR_WHITE,7}, {0,0,80,80,"8",COLOR_WHITE,8}, {0,0,80,80,"9",COLOR_WHITE,9},
    {0,0,80,80,"0",COLOR_WHITE,0}
};
#define NUM_PIN_BUTTONS (sizeof(pin_buttons)/sizeof(pin_buttons[0]))

void clear(uint32_t *buf, uint32_t color) {
    for (int i = 0; i < screen_w * screen_h; i++) buf[i] = color;
}

void draw_rect(uint32_t *buf, int x, int y, int w, int h, uint32_t color) {
    for (int dy = 0; dy < h; dy++) {
        for (int dx = 0; dx < w; dx++) {
            int px = x + dx, py = y + dy;
            if (px >= 0 && px < screen_w && py >= 0 && py < screen_h) {
                buf[py * screen_w + px] = color;
            }
        }
    }
}

void draw_circle(uint32_t *buf, int cx, int cy, int r, uint32_t color) {
    for (int y = -r; y <= r; y++) {
        for (int x = -r; x <= r; x++) {
            if (x*x + y*y <= r*r) {
                int px = cx + x, py = cy + y;
                if (px >= 0 && px < screen_w && py >= 0 && py < screen_h) {
                    buf[py * screen_w + px] = color;
                }
            }
        }
    }
}

void draw_text(uint32_t *buf, const char *text, float text_scale, int x, int y, uint32_t color) {
    int ascent;
    stbtt_GetFontVMetrics(&font, &ascent, NULL, NULL);
    int baseline = (int)(ascent * text_scale);
    int px = x;
    
    for (const char *p = text; *p; p++) {
        int ax;
        stbtt_GetCodepointHMetrics(&font, *p, &ax, NULL);
        int c_x1, c_y1, c_x2, c_y2;
        stbtt_GetCodepointBitmapBox(&font, *p, text_scale, text_scale, &c_x1, &c_y1, &c_x2, &c_y2);
        int w = c_x2 - c_x1, h = c_y2 - c_y1;
        if (w <= 0 || h <= 0) { px += (int)(ax * text_scale); continue; }
        
        unsigned char *bitmap = malloc(w * h);
        stbtt_MakeCodepointBitmap(&font, bitmap, w, h, w, text_scale, text_scale, *p);
        
        for (int row = 0; row < h; row++) {
            for (int col = 0; col < w; col++) {
                int a = bitmap[row * w + col];
                if (a > 0) {
                    int dx = px + c_x1 + col, dy = y + baseline + c_y1 + row;
                    if (dx >= 0 && dx < screen_w && dy >= 0 && dy < screen_h) {
                        buf[dy * screen_w + dx] = color;
                    }
                }
            }
        }
        free(bitmap);
        px += (int)(ax * text_scale);
    }
}

int text_width(const char *text, float text_scale) {
    int width = 0;
    for (const char *p = text; *p; p++) {
        int ax;
        stbtt_GetCodepointHMetrics(&font, *p, &ax, NULL);
        width += (int)(ax * text_scale);
    }
    return width;
}

void draw_button(uint32_t *buf, Button *btn) {
    draw_circle(buf, btn->x + btn->w/2, btn->y + btn->h/2, btn->w/2, 0xFF333333);
    int tw = text_width(btn->text, scale);
    draw_text(buf, btn->text, scale, btn->x + (btn->w - tw)/2, btn->y + btn->h/2 - 16, btn->color);
}

void draw_app_icon(uint32_t *buf, App *app, int x, int y, int size) {
    int radius = size/8;
    // Draw rounded rect background
    draw_rect(buf, x + radius, y, size - 2*radius, size, app->color);
    draw_rect(buf, x, y + radius, size, size - 2*radius, app->color);
    draw_circle(buf, x + radius, y + radius, radius, app->color);
    draw_circle(buf, x + size - radius, y + radius, radius, app->color);
    draw_circle(buf, x + radius, y + size - radius, radius, app->color);
    draw_circle(buf, x + size - radius, y + size - radius, radius, app->color);
    
    int tw = text_width(app->name, scale * 0.4f);
    draw_text(buf, app->name, scale * 0.4f, x + (size - tw)/2, y + size/2 - 8, COLOR_WHITE);
}

void get_time_str(char *buf) {
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    strftime(buf, 16, "%H:%M", tm);
}

void draw_status_bar(uint32_t *buf) {
    char time_str[16];
    get_time_str(time_str);
    int tw = text_width(time_str, scale * 0.6f);
    draw_text(buf, time_str, scale * 0.6f, (screen_w - tw)/2, 40, COLOR_WHITE);
    
    // Battery icon (placeholder)
    draw_rect(buf, screen_w - 60, 30, 40, 20, COLOR_WHITE);
    draw_rect(buf, screen_w - 60 + 2, 32, 36, 16, COLOR_BG);
    draw_rect(buf, screen_w - 18, 35, 4, 10, COLOR_WHITE);
    draw_text(buf, "87%", scale * 0.4f, screen_w - 110, 35, COLOR_WHITE);
    
    // WiFi icon (placeholder)
    for (int i = 0; i < 3; i++) {
        draw_rect(buf, 20 + i*8, 40 - i*3, 4, 6 + i*3, COLOR_WHITE);
    }
}

void draw_lock_screen(uint32_t *buf) {
    clear(buf, COLOR_BG);
    draw_status_bar(buf);
    
    char time_str[16];
    get_time_str(time_str);
    int tw = text_width(time_str, scale * 2.0f);
    draw_text(buf, time_str, scale * 2.0f, (screen_w - tw)/2, screen_h/2 - 100, COLOR_WHITE);
    
    char date_str[32];
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    strftime(date_str, sizeof(date_str), "%A, %B %d", tm);
    int dw = text_width(date_str, scale * 0.8f);
    draw_text(buf, date_str, scale * 0.8f, (screen_w - dw)/2, screen_h/2 - 40, COLOR_GRAY);
    
    // PIN dots
    for (int i = 0; i < PIN_LENGTH; i++) {
        int x = screen_w/2 - 60 + i * 30;
        int y = screen_h/2 + 20;
        if (i < strlen(pin_input)) {
            draw_circle(buf, x, y, 8, COLOR_WHITE);
        } else {
            draw_circle(buf, x, y, 8, COLOR_GRAY);
            draw_circle(buf, x, y, 6, COLOR_BG);
        }
    }
    
    // PIN pad
    int start_x = screen_w/2 - 120, start_y = screen_h/2 + 80;
    for (int i = 0; i < NUM_PIN_BUTTONS; i++) {
        int row = (i < 9) ? i / 3 : 3;
        int col = (i < 9) ? i % 3 : 1;
        pin_buttons[i].x = start_x + col * 80;
        pin_buttons[i].y = start_y + row * 80;
        draw_button(buf, &pin_buttons[i]);
    }
    
    int tw_swipe = text_width("Swipe up to unlock", scale * 0.5f);
    draw_text(buf, "Swipe up to unlock", scale * 0.5f, (screen_w - tw_swipe)/2, screen_h - 80, COLOR_GRAY);
}

void draw_home_screen(uint32_t *buf) {
    clear(buf, COLOR_BG);
    draw_status_bar(buf);
    
    // App grid
    int apps_per_row = 3;
    int icon_size = 80;
    int spacing = 40;
    int start_x = (screen_w - (apps_per_row * icon_size + (apps_per_row - 1) * spacing)) / 2;
    int start_y = 120;
    
    for (int i = 0; i < NUM_APPS; i++) {
        int row = i / apps_per_row;
        int col = i % apps_per_row;
        int x = start_x + col * (icon_size + spacing);
        int y = start_y + row * (icon_size + spacing + 20);
        draw_app_icon(buf, &apps[i], x, y, icon_size);
    }
    
    // Home indicator
    draw_rect(buf, screen_w/2 - 60, screen_h - 20, 120, 4, COLOR_WHITE);
}

void draw_app_screen(uint32_t *buf) {
    clear(buf, COLOR_BG);
    draw_status_bar(buf);
    
    App *app = &apps[current_app];
    int tw = text_width(app->name, scale * 1.5f);
    draw_text(buf, app->name, scale * 1.5f, (screen_w - tw)/2, 120, COLOR_WHITE);
    
    // Simple app content based on app type
    if (current_app == 5) { // Calculator
        char calc_display[] = "0";
        int cw = text_width(calc_display, scale * 2.0f);
        draw_text(buf, calc_display, scale * 2.0f, (screen_w - cw)/2, 200, COLOR_WHITE);
        
        char calc_buttons[] = "789+456-123*0=./";
        for (int i = 0; i < 16; i++) {
            int row = i / 4, col = i % 4;
            int x = 50 + col * 80, y = 300 + row * 80;
            char btn_text[2] = {calc_buttons[i], 0};
            draw_circle(buf, x + 30, y + 30, 30, COLOR_GRAY);
            int btw = text_width(btn_text, scale);
            draw_text(buf, btn_text, scale, x + 30 - btw/2, y + 20, COLOR_WHITE);
        }
    } else {
        draw_text(buf, "App Content", scale, 50, 200, COLOR_GRAY);
    }
    
    // Home indicator
    draw_rect(buf, screen_w/2 - 60, screen_h - 20, 120, 4, COLOR_WHITE);
}

int point_in_rect(int px, int py, int x, int y, int w, int h) {
    return px >= x && px < x + w && py >= y && py < y + h;
}

void handle_touch() {
    if (!touch.just_pressed) return;
    
    if (current_state == STATE_LOCK) {
        // Check PIN buttons
        for (int i = 0; i < NUM_PIN_BUTTONS; i++) {
            Button *btn = &pin_buttons[i];
            if (point_in_rect(touch.x, touch.y, btn->x, btn->y, btn->w, btn->h)) {
                if (strlen(pin_input) < PIN_LENGTH) {
                    char digit[2] = {'0' + btn->id, 0};
                    strcat(pin_input, digit);
                    if (strlen(pin_input) == PIN_LENGTH) {
                        if (strcmp(pin_input, "1234") == 0) {
                            current_state = STATE_HOME;
                            strcpy(pin_input, "");
                        } else {
                            strcpy(pin_input, ""); // Wrong PIN, clear
                        }
                    }
                }
                break;
            }
        }
    } else if (current_state == STATE_HOME) {
        // Check app icons
        int apps_per_row = 3, icon_size = 80, spacing = 40;
        int start_x = (screen_w - (apps_per_row * icon_size + (apps_per_row - 1) * spacing)) / 2;
        int start_y = 120;
        
        for (int i = 0; i < NUM_APPS; i++) {
            int row = i / apps_per_row, col = i % apps_per_row;
            int x = start_x + col * (icon_size + spacing);
            int y = start_y + row * (icon_size + spacing + 20);
            if (point_in_rect(touch.x, touch.y, x, y, icon_size, icon_size)) {
                current_app = i;
                current_state = STATE_APP;
                break;
            }
        }
    }
}

void handle_gestures() {
    if (touch.just_pressed) {
        touch.swipe_start_x = touch.x;
        touch.swipe_start_y = touch.y;
        touch.swiping = 1;
    }
    
    if (!touch.pressed && touch.swiping) {
        int dx = touch.x - touch.swipe_start_x;
        int dy = touch.y - touch.swipe_start_y;
        
        if (abs(dy) > abs(dx) && abs(dy) > 100) {
            if (dy < 0 && current_state == STATE_LOCK) { // Swipe up from lock
                current_state = STATE_HOME;
                strcpy(pin_input, "");
            } else if (dy < 0 && current_state == STATE_APP) { // Swipe up from app
                current_state = STATE_HOME;
            }
        }
        touch.swiping = 0;
    }
}

void init_touch() {
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
    }
}

void read_touch() {
    touch.just_pressed = 0;
    struct pollfd fds[MAX_TOUCH_DEVICES];
    for (int i = 0; i < num_touch; i++) fds[i] = (struct pollfd){touch_devs[i].fd, POLLIN, 0};
    if (poll(fds, num_touch, 0) <= 0) return;

    for (int i = 0; i < num_touch; i++) {
        if (!(fds[i].revents & POLLIN)) continue;
        struct input_event ev;
        int raw_x = touch.x, raw_y = touch.y, tracking = touch.pressed;
        
        while (read(touch_devs[i].fd, &ev, sizeof(ev)) == sizeof(ev)) {
            if (ev.type == EV_ABS) {
                if (ev.code == ABS_X || ev.code == ABS_MT_POSITION_X)
                    raw_x = (ev.value - touch_devs[i].min_x) * screen_w / (touch_devs[i].max_x - touch_devs[i].min_x + 1);
                if (ev.code == ABS_Y || ev.code == ABS_MT_POSITION_Y)
                    raw_y = (ev.value - touch_devs[i].min_y) * screen_h / (touch_devs[i].max_y - touch_devs[i].min_y + 1);
                if (ev.code == ABS_MT_TRACKING_ID) tracking = (ev.value >= 0);
            } else if (ev.type == EV_KEY && ev.code == BTN_TOUCH) {
                tracking = ev.value;
            } else if (ev.type == EV_SYN && ev.code == SYN_REPORT) {
                touch.last_pressed = touch.pressed;
                touch.pressed = tracking;
                touch.just_pressed = (touch.pressed && !touch.last_pressed);
                touch.x = raw_x; touch.y = raw_y;
            }
        }
    }
}

void cleanup(int sig) {
    if (fbp) clear(fbp, 0x00000000);
    if (backbuffer) free(backbuffer);
    if (fbp) munmap(fbp, stride * screen_h);
    if (fb_fd > 0) close(fb_fd);
    exit(0);
}

int main() {
    signal(SIGINT, cleanup);
    
    FILE *f = fopen(FONT_PATH, "rb");
    if (!f) { perror("Font load failed"); exit(1); }
    fseek(f, 0, SEEK_END); int size = ftell(f); rewind(f);
    unsigned char *ttf = malloc(size); fread(ttf, 1, size, f); fclose(f);
    
    if (!stbtt_InitFont(&font, ttf, 0)) { fprintf(stderr, "Failed to initialize font\n"); return -1; }
    scale = stbtt_ScaleForPixelHeight(&font, FONT_HEIGHT);
    
    struct fb_var_screeninfo vinfo;
    struct fb_fix_screeninfo finfo;
    fb_fd = open("/dev/fb0", O_RDWR);
    ioctl(fb_fd, FBIOGET_VSCREENINFO, &vinfo);
    ioctl(fb_fd, FBIOGET_FSCREENINFO, &finfo);
    screen_w = vinfo.xres; screen_h = vinfo.yres; stride = finfo.line_length;
    fbp = mmap(0, stride * screen_h, PROT_READ | PROT_WRITE, MAP_SHARED, fb_fd, 0);
    backbuffer = malloc(screen_w * screen_h * 4);
    
    init_touch();
    
    while (1) {
        read_touch();
        handle_touch();
        handle_gestures();
        
        if (current_state == STATE_LOCK) draw_lock_screen(backbuffer);
        else if (current_state == STATE_HOME) draw_home_screen(backbuffer);
        else if (current_state == STATE_APP) draw_app_screen(backbuffer);
        
        memcpy(fbp, backbuffer, screen_w * screen_h * 4);
        usleep(16666); // 60 FPS
    }
    
    return 0;
}