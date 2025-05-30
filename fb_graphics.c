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
#define COLOR_LIGHT_GRAY 0xFFAAAAAA
#define COLOR_BLUE 0xFF007AFF
#define COLOR_GREEN 0xFF34C759
#define COLOR_RED 0xFFFF3B30
#define COLOR_ORANGE 0xFFFF9500
#define COLOR_PURPLE 0xFFAF52DE
#define COLOR_YELLOW 0xFFFFCC02

// UI scaling for 1080p (assuming ~1080x1920)
#define STATUS_HEIGHT 140
#define LARGE_TEXT 96
#define MEDIUM_TEXT 64
#define SMALL_TEXT 48
#define ICON_SIZE 200
#define BUTTON_SIZE 160
#define MARGIN 60
#define TOUCH_THRESHOLD 50
#define DEBOUNCE_MS 50

typedef enum { LOCK_SCREEN, PIN_ENTRY, HOME_SCREEN, APP_SCREEN } State;

typedef struct {
    int x, y, w, h, id;
    char text[32];
    uint32_t bg_color, text_color;
    int visible;
} UIElement;

typedef struct {
    char name[32];
    uint32_t color;
    int id;
} App;

typedef struct {
    int fd, min_x, max_x, min_y, max_y;
} TouchDevice;

typedef struct {
    int x, y, pressed, last_pressed;
    int start_x, start_y, is_swiping;
    uint64_t last_touch_time;
} Touch;

// Global variables
uint32_t *framebuffer = NULL, *backbuffer = NULL;
int fb_fd, screen_w, screen_h, stride;
TouchDevice touch_devices[16];
int num_touch_devices = 0;
Touch touch = {0};
State current_state = LOCK_SCREEN;
char pin_code[5] = {0};
int current_app_id = -1;
int battery_level = 87;
stbtt_fontinfo font;

// Apps configuration
App apps[] = {
    {"Phone", COLOR_GREEN, 0}, {"Messages", COLOR_GREEN, 1}, {"Camera", COLOR_GRAY, 2},
    {"Photos", COLOR_BLUE, 3}, {"Settings", COLOR_LIGHT_GRAY, 4}, {"Calculator", COLOR_ORANGE, 5},
    {"Clock", COLOR_PURPLE, 6}, {"Weather", COLOR_BLUE, 7}, {"Maps", COLOR_GREEN, 8},
    {"Music", COLOR_RED, 9}, {"Mail", COLOR_BLUE, 10}, {"Safari", COLOR_BLUE, 11}
};
#define APP_COUNT (sizeof(apps)/sizeof(apps[0]))

uint64_t get_time_ms() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000ULL + ts.tv_nsec / 1000000ULL;
}

void clear_screen(uint32_t *buf, uint32_t color) {
    for (int i = 0; i < screen_w * screen_h; i++) buf[i] = color;
}

void draw_rect(uint32_t *buf, int x, int y, int w, int h, uint32_t color) {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > screen_w) w = screen_w - x;
    if (y + h > screen_h) h = screen_h - y;
    
    for (int dy = 0; dy < h; dy++) {
        for (int dx = 0; dx < w; dx++) {
            buf[(y + dy) * screen_w + (x + dx)] = color;
        }
    }
}

void draw_circle_filled(uint32_t *buf, int cx, int cy, int radius, uint32_t color) {
    for (int y = -radius; y <= radius; y++) {
        for (int x = -radius; x <= radius; x++) {
            if (x*x + y*y <= radius*radius) {
                int px = cx + x, py = cy + y;
                if (px >= 0 && px < screen_w && py >= 0 && py < screen_h) {
                    buf[py * screen_w + px] = color;
                }
            }
        }
    }
}

void draw_rounded_rect(uint32_t *buf, int x, int y, int w, int h, int radius, uint32_t color) {
    // Main rectangles
    draw_rect(buf, x + radius, y, w - 2*radius, h, color);
    draw_rect(buf, x, y + radius, w, h - 2*radius, color);
    
    // Corner circles
    draw_circle_filled(buf, x + radius, y + radius, radius, color);
    draw_circle_filled(buf, x + w - radius, y + radius, radius, color);
    draw_circle_filled(buf, x + radius, y + h - radius, radius, color);
    draw_circle_filled(buf, x + w - radius, y + h - radius, radius, color);
}

int measure_text_width(const char *text, int font_size) {
    float scale = stbtt_ScaleForPixelHeight(&font, font_size);
    int width = 0;
    for (const char *p = text; *p; p++) {
        int advance;
        stbtt_GetCodepointHMetrics(&font, *p, &advance, NULL);
        width += (int)(advance * scale);
    }
    return width;
}

void draw_text(uint32_t *buf, const char *text, int font_size, int x, int y, uint32_t color) {
    float scale = stbtt_ScaleForPixelHeight(&font, font_size);
    int ascent, descent, line_gap;
    stbtt_GetFontVMetrics(&font, &ascent, &descent, &line_gap);
    int baseline = y + (int)(ascent * scale);
    
    int pos_x = x;
    for (const char *p = text; *p; p++) {
        int advance, left_bearing;
        stbtt_GetCodepointHMetrics(&font, *p, &advance, &left_bearing);
        
        int c_x1, c_y1, c_x2, c_y2;
        stbtt_GetCodepointBitmapBox(&font, *p, scale, scale, &c_x1, &c_y1, &c_x2, &c_y2);
        
        int bitmap_w = c_x2 - c_x1;
        int bitmap_h = c_y2 - c_y1;
        
        if (bitmap_w > 0 && bitmap_h > 0) {
            unsigned char *bitmap = malloc(bitmap_w * bitmap_h);
            stbtt_MakeCodepointBitmap(&font, bitmap, bitmap_w, bitmap_h, bitmap_w, scale, scale, *p);
            
            for (int row = 0; row < bitmap_h; row++) {
                for (int col = 0; col < bitmap_w; col++) {
                    unsigned char alpha = bitmap[row * bitmap_w + col];
                    if (alpha > 128) {
                        int px = pos_x + c_x1 + col;
                        int py = baseline + c_y1 + row;
                        if (px >= 0 && px < screen_w && py >= 0 && py < screen_h) {
                            buf[py * screen_w + px] = color;
                        }
                    }
                }
            }
            free(bitmap);
        }
        pos_x += (int)(advance * scale);
    }
}

void draw_text_centered(uint32_t *buf, const char *text, int font_size, int y, uint32_t color) {
    int text_width = measure_text_width(text, font_size);
    int x = (screen_w - text_width) / 2;
    draw_text(buf, text, font_size, x, y, color);
}

void get_current_time(char *time_str, char *date_str) {
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    strftime(time_str, 32, "%H:%M", tm);
    strftime(date_str, 64, "%A, %B %d", tm);
}

void draw_status_bar(uint32_t *buf) {
    char time_str[32];
    get_current_time(time_str, NULL);
    draw_text_centered(buf, time_str, MEDIUM_TEXT, 25, COLOR_WHITE);
    
    // Battery indicator
    int bat_x = screen_w - 200, bat_y = 25;
    int bat_w = 80, bat_h = 40;
    
    // Battery outline
    draw_rounded_rect(buf, bat_x, bat_y, bat_w, bat_h, 8, COLOR_WHITE);
    draw_rounded_rect(buf, bat_x + 3, bat_y + 3, bat_w - 6, bat_h - 6, 5, COLOR_BG);
    
    // Battery tip
    draw_rounded_rect(buf, bat_x + bat_w, bat_y + 12, 8, 16, 4, COLOR_WHITE);
    
    // Battery fill
    int fill_width = (int)((bat_w - 6) * battery_level / 100.0f);
    uint32_t fill_color = battery_level > 20 ? COLOR_GREEN : COLOR_RED;
    if (fill_width > 0) {
        draw_rounded_rect(buf, bat_x + 3, bat_y + 3, fill_width, bat_h - 6, 5, fill_color);
    }
    
    // Battery percentage
    char bat_text[8];
    snprintf(bat_text, sizeof(bat_text), "%d%%", battery_level);
    draw_text(buf, bat_text, SMALL_TEXT, bat_x - 100, bat_y + 5, COLOR_WHITE);
    
    // WiFi indicator
    for (int i = 0; i < 4; i++) {
        int bar_h = 12 + i * 8;
        draw_rect(buf, 80 + i * 20, 45 - bar_h/2, 12, bar_h, COLOR_WHITE);
    }
}

void draw_lock_screen(uint32_t *buf) {
    clear_screen(buf, COLOR_BG);
    draw_status_bar(buf);
    
    char time_str[32], date_str[64];
    get_current_time(time_str, date_str);
    
    // Large time display - moved down more to avoid status bar
    draw_text_centered(buf, time_str, LARGE_TEXT * 2, screen_h/2 - 250, COLOR_WHITE);
    
    // Date display - proper spacing below time
    draw_text_centered(buf, date_str, MEDIUM_TEXT, screen_h/2 - 120, COLOR_LIGHT_GRAY);
    
    // Swipe up indicator
    draw_text_centered(buf, "Swipe up to unlock", SMALL_TEXT, screen_h - 200, COLOR_GRAY);
    
    // Home indicator
    draw_rounded_rect(buf, screen_w/2 - 100, screen_h - 80, 200, 8, 4, COLOR_LIGHT_GRAY);
}

void draw_pin_entry(uint32_t *buf) {
    clear_screen(buf, COLOR_BG);
    draw_status_bar(buf);
    
    // PIN entry title - moved down to avoid status bar
    draw_text_centered(buf, "Enter Passcode", MEDIUM_TEXT, STATUS_HEIGHT + 50, COLOR_WHITE);
    
    // PIN dots - moved down accordingly
    int dot_spacing = 80;
    int start_x = screen_w/2 - 120;
    for (int i = 0; i < 4; i++) {
        int x = start_x + i * dot_spacing;
        int y = STATUS_HEIGHT + 150;
        if (i < (int)strlen(pin_code)) {
            draw_circle_filled(buf, x, y, 20, COLOR_WHITE);
        } else {
            draw_circle_filled(buf, x, y, 20, COLOR_GRAY);
            draw_circle_filled(buf, x, y, 16, COLOR_BG);
        }
    }
    
    // PIN pad - moved down accordingly
    char pin_labels[] = "123456789*0#";
    int pad_start_x = screen_w/2 - 240;
    int pad_start_y = STATUS_HEIGHT + 250;
    
    for (int i = 0; i < 12; i++) {
        int row = i / 3;
        int col = i % 3;
        int x = pad_start_x + col * 160;
        int y = pad_start_y + row * 160;
        
        // Skip * and # for now, only show 0-9
        if (pin_labels[i] == '*' || pin_labels[i] == '#') continue;
        
        draw_circle_filled(buf, x + 80, y + 80, 70, COLOR_GRAY);
        
        char btn_text[2] = {pin_labels[i], 0};
        int text_w = measure_text_width(btn_text, MEDIUM_TEXT);
        draw_text(buf, btn_text, MEDIUM_TEXT, x + 80 - text_w/2, y + 60, COLOR_WHITE);
    }
    
    // Home indicator
    draw_rounded_rect(buf, screen_w/2 - 100, screen_h - 80, 200, 8, 4, COLOR_LIGHT_GRAY);
}

void draw_home_screen(uint32_t *buf) {
    clear_screen(buf, COLOR_BG);
    draw_status_bar(buf);
    
    // App grid (4x3) - proper spacing from status bar
    int apps_per_row = 3;
    int grid_width = apps_per_row * ICON_SIZE + (apps_per_row - 1) * MARGIN;
    int start_x = (screen_w - grid_width) / 2;
    int start_y = STATUS_HEIGHT + 80; // Proper spacing below status bar
    
    for (int i = 0; i < APP_COUNT && i < 12; i++) {
        int row = i / apps_per_row;
        int col = i % apps_per_row;
        int x = start_x + col * (ICON_SIZE + MARGIN);
        int y = start_y + row * (ICON_SIZE + MARGIN * 2);
        
        // App icon background
        draw_rounded_rect(buf, x, y, ICON_SIZE, ICON_SIZE, 40, apps[i].color);
        
        // App name below icon
        int text_w = measure_text_width(apps[i].name, SMALL_TEXT);
        draw_text(buf, apps[i].name, SMALL_TEXT, x + (ICON_SIZE - text_w)/2, y + ICON_SIZE + 20, COLOR_WHITE);
    }
    
    // Home indicator
    draw_rounded_rect(buf, screen_w/2 - 100, screen_h - 80, 200, 8, 4, COLOR_WHITE);
}

void draw_app_screen(uint32_t *buf) {
    clear_screen(buf, COLOR_BG);
    draw_status_bar(buf);
    
    if (current_app_id >= 0 && current_app_id < APP_COUNT) {
        App *app = &apps[current_app_id];
        
        // App title - proper spacing from status bar
        draw_text_centered(buf, app->name, LARGE_TEXT, STATUS_HEIGHT + 50, COLOR_WHITE);
        
        // Simple app content
        if (current_app_id == 5) { // Calculator
            draw_text_centered(buf, "0", LARGE_TEXT * 2, STATUS_HEIGHT + 200, COLOR_WHITE);
            
            // Calculator buttons
            char calc_btns[] = "789+456-123*C0=";
            int calc_start_x = screen_w/2 - 320;
            int calc_start_y = STATUS_HEIGHT + 350;
            
            for (int i = 0; i < 16; i++) {
                int row = i / 4;
                int col = i % 4;
                int x = calc_start_x + col * 160;
                int y = calc_start_y + row * 120;
                
                uint32_t btn_color = (col == 3) ? COLOR_ORANGE : COLOR_GRAY;
                draw_rounded_rect(buf, x, y, 140, 100, 20, btn_color);
                
                char btn_text[2] = {calc_btns[i], 0};
                int text_w = measure_text_width(btn_text, MEDIUM_TEXT);
                draw_text(buf, btn_text, MEDIUM_TEXT, x + 70 - text_w/2, y + 30, COLOR_WHITE);
            }
        } else {
            draw_text_centered(buf, "App Content", MEDIUM_TEXT, STATUS_HEIGHT + 200, COLOR_GRAY);
        }
    }
    
    // Home indicator
    draw_rounded_rect(buf, screen_w/2 - 100, screen_h - 80, 200, 8, 4, COLOR_WHITE);
}

int point_near_circle(int px, int py, int cx, int cy, int radius) {
    int dx = px - cx, dy = py - cy;
    int distance_sq = dx*dx + dy*dy;
    int expanded_radius = radius + 30; // 30px proximity buffer
    return distance_sq <= (expanded_radius * expanded_radius);
}

int point_near_rect(int px, int py, int x, int y, int w, int h) {
    int buffer = 20; // 20px proximity buffer
    return px >= (x - buffer) && px < (x + w + buffer) && 
           py >= (y - buffer) && py < (y + h + buffer);
}

void handle_touch_input() {
    // Process ANY touch that's currently active - no barriers
    if (!touch.pressed) return;
    
    if (current_state == PIN_ENTRY) {
        // PIN pad with very generous proximity detection
        char pin_labels[] = "123456789*0#";
        int pad_start_x = screen_w/2 - 240;
        int pad_start_y = STATUS_HEIGHT + 250;
        
        for (int i = 0; i < 12; i++) {
            if (pin_labels[i] == '*' || pin_labels[i] == '#') continue;
            
            int row = i / 3;
            int col = i % 3;
            int btn_x = pad_start_x + col * 160;
            int btn_y = pad_start_y + row * 160;
            int center_x = btn_x + 80;
            int center_y = btn_y + 80;
            
            // Very generous hit area - 100px radius
            int dx = touch.x - center_x;
            int dy = touch.y - center_y;
            if ((dx*dx + dy*dy) <= (100*100)) {
                printf("PIN button %c pressed!\n", pin_labels[i]);
                if (strlen(pin_code) < 4) {
                    char digit[2] = {pin_labels[i], 0};
                    strcat(pin_code, digit);
                    
                    if (strlen(pin_code) == 4) {
                        if (strcmp(pin_code, "1234") == 0) {
                            printf("Correct PIN! Unlocking...\n");
                            current_state = HOME_SCREEN;
                        }
                        memset(pin_code, 0, sizeof(pin_code));
                    }
                }
                return;
            }
        }
    }
    
    else if (current_state == HOME_SCREEN) {
        // App icons with very generous hit areas
        int apps_per_row = 3;
        int grid_width = apps_per_row * ICON_SIZE + (apps_per_row - 1) * MARGIN;
        int start_x = (screen_w - grid_width) / 2;
        int start_y = STATUS_HEIGHT + 80;
        
        for (int i = 0; i < APP_COUNT && i < 12; i++) {
            int row = i / apps_per_row;
            int col = i % apps_per_row;
            int icon_x = start_x + col * (ICON_SIZE + MARGIN);
            int icon_y = start_y + row * (ICON_SIZE + MARGIN * 2);
            
            // Very generous hit area - 50px buffer on all sides
            if (touch.x >= (icon_x - 50) && touch.x < (icon_x + ICON_SIZE + 50) &&
                touch.y >= (icon_y - 50) && touch.y < (icon_y + ICON_SIZE + 50)) {
                printf("App %s launched!\n", apps[i].name);
                current_app_id = i;
                current_state = APP_SCREEN;
                return;
            }
        }
    }
    
    else if (current_state == APP_SCREEN && current_app_id == 5) {
        // Calculator buttons with generous hit areas
        char calc_btns[] = "789+456-123*C0=";
        int calc_start_x = screen_w/2 - 320;
        int calc_start_y = STATUS_HEIGHT + 350;
        
        for (int i = 0; i < 16; i++) {
            int row = i / 4;
            int col = i % 4;
            int btn_x = calc_start_x + col * 160;
            int btn_y = calc_start_y + row * 120;
            
            // Very generous hit area - 40px buffer
            if (touch.x >= (btn_x - 40) && touch.x < (btn_x + 140 + 40) &&
                touch.y >= (btn_y - 40) && touch.y < (btn_y + 100 + 40)) {
                printf("Calculator button %c pressed!\n", calc_btns[i]);
                return;
            }
        }
    }
}

void handle_gestures() {
    static int swipe_handled = 0;
    
    // Start tracking when touch begins
    if (touch.pressed && !touch.last_pressed) {
        touch.start_x = touch.x;
        touch.start_y = touch.y;
        touch.is_swiping = 1;
        swipe_handled = 0;
    }
    
    // Detect swipe when touch ends
    if (!touch.pressed && touch.last_pressed && touch.is_swiping && !swipe_handled) {
        int dx = touch.x - touch.start_x;
        int dy = touch.y - touch.start_y;
        int distance = sqrt(dx*dx + dy*dy);
        
        if (distance > 80) { // Very low threshold for easy swiping
            swipe_handled = 1;
            
            if (abs(dy) > abs(dx)) {
                if (dy < 0) { // Swipe up
                    if (current_state == LOCK_SCREEN) {
                        current_state = PIN_ENTRY;
                    } else if (current_state == APP_SCREEN) {
                        current_state = HOME_SCREEN;
                    }
                } else { // Swipe down
                    if (current_state == PIN_ENTRY) {
                        current_state = LOCK_SCREEN;
                        memset(pin_code, 0, sizeof(pin_code));
                    }
                }
            }
        }
    }
    
    // Reset swiping when touch ends
    if (!touch.pressed && touch.last_pressed) {
        touch.is_swiping = 0;
        swipe_handled = 0;
    }
}

void init_touch_devices() {
    for (int i = 0; i < 16; i++) {
        char path[64];
        snprintf(path, sizeof(path), "/dev/input/event%d", i);
        int fd = open(path, O_RDONLY | O_NONBLOCK);
        if (fd < 0) continue;
        
        struct input_absinfo abs_x, abs_y;
        if (ioctl(fd, EVIOCGABS(ABS_X), &abs_x) < 0 || 
            ioctl(fd, EVIOCGABS(ABS_Y), &abs_y) < 0) {
            close(fd);
            continue;
        }
        
        touch_devices[num_touch_devices++] = (TouchDevice){
            fd, abs_x.minimum, abs_x.maximum, abs_y.minimum, abs_y.maximum
        };
    }
}

void read_touch_events() {
    struct pollfd fds[16];
    for (int i = 0; i < num_touch_devices; i++) {
        fds[i] = (struct pollfd){touch_devices[i].fd, POLLIN, 0};
    }
    
    if (poll(fds, num_touch_devices, 0) <= 0) return;
    
    for (int i = 0; i < num_touch_devices; i++) {
        if (!(fds[i].revents & POLLIN)) continue;
        
        struct input_event ev;
        int raw_x = touch.x, raw_y = touch.y, tracking = touch.pressed;
        
        while (read(touch_devices[i].fd, &ev, sizeof(ev)) == sizeof(ev)) {
            if (ev.type == EV_ABS) {
                if (ev.code == ABS_X || ev.code == ABS_MT_POSITION_X) {
                    raw_x = (ev.value - touch_devices[i].min_x) * screen_w / 
                           (touch_devices[i].max_x - touch_devices[i].min_x + 1);
                }
                if (ev.code == ABS_Y || ev.code == ABS_MT_POSITION_Y) {
                    raw_y = (ev.value - touch_devices[i].min_y) * screen_h / 
                           (touch_devices[i].max_y - touch_devices[i].min_y + 1);
                }
                if (ev.code == ABS_MT_TRACKING_ID) {
                    tracking = (ev.value >= 0);
                }
            } else if (ev.type == EV_KEY && ev.code == BTN_TOUCH) {
                tracking = ev.value;
            } else if (ev.type == EV_SYN && ev.code == SYN_REPORT) {
                touch.last_pressed = touch.pressed;
                touch.pressed = tracking;
                touch.x = raw_x;
                touch.y = raw_y;
                touch.last_touch_time = get_time_ms();
                
                // Debug: Print touch events
                if (touch.pressed && !touch.last_pressed) {
                    printf("Touch started at (%d, %d)\n", touch.x, touch.y);
                }
            }
        }
    }
}

void cleanup_and_exit(int sig) {
    if (framebuffer) {
        clear_screen(framebuffer, COLOR_BG);
        munmap(framebuffer, stride * screen_h);
    }
    if (backbuffer) free(backbuffer);
    if (fb_fd > 0) close(fb_fd);
    for (int i = 0; i < num_touch_devices; i++) {
        close(touch_devices[i].fd);
    }
    exit(0);
}

int main() {
    signal(SIGINT, cleanup_and_exit);
    
    // Load font
    FILE *font_file = fopen(FONT_PATH, "rb");
    if (!font_file) { perror("Font load failed"); exit(1); }
    fseek(font_file, 0, SEEK_END);
    int font_size = ftell(font_file);
    rewind(font_file);
    unsigned char *font_data = malloc(font_size);
    fread(font_data, 1, font_size, font_file);
    fclose(font_file);
    
    if (!stbtt_InitFont(&font, font_data, 0)) {
        fprintf(stderr, "Font initialization failed\n");
        exit(1);
    }
    
    // Initialize framebuffer
    struct fb_var_screeninfo vinfo;
    struct fb_fix_screeninfo finfo;
    fb_fd = open("/dev/fb0", O_RDWR);
    if (fb_fd < 0) { perror("Framebuffer open failed"); exit(1); }
    
    ioctl(fb_fd, FBIOGET_VSCREENINFO, &vinfo);
    ioctl(fb_fd, FBIOGET_FSCREENINFO, &finfo);
    
    screen_w = vinfo.xres;
    screen_h = vinfo.yres;
    stride = finfo.line_length;
    
    framebuffer = mmap(0, stride * screen_h, PROT_READ | PROT_WRITE, MAP_SHARED, fb_fd, 0);
    if (framebuffer == MAP_FAILED) { perror("Framebuffer mmap failed"); exit(1); }
    
    backbuffer = malloc(screen_w * screen_h * 4);
    if (!backbuffer) { perror("Backbuffer allocation failed"); exit(1); }
    
    init_touch_devices();
    
    // Main loop
    printf("Phone OS started - touch anywhere!\n");
    while (1) {
        read_touch_events();
        handle_touch_input();
        handle_gestures();
        
        // Render current state
        switch (current_state) {
            case LOCK_SCREEN: draw_lock_screen(backbuffer); break;
            case PIN_ENTRY: draw_pin_entry(backbuffer); break;
            case HOME_SCREEN: draw_home_screen(backbuffer); break;
            case APP_SCREEN: draw_app_screen(backbuffer); break;
        }
        
        // Show touch position for debugging (red dot)
        if (touch.pressed) {
            for (int y = -10; y <= 10; y++) {
                for (int x = -10; x <= 10; x++) {
                    if (x*x + y*y <= 100) {
                        int px = touch.x + x, py = touch.y + y;
                        if (px >= 0 && px < screen_w && py >= 0 && py < screen_h) {
                            backbuffer[py * screen_w + px] = COLOR_RED;
                        }
                    }
                }
            }
        }
        
        // Copy to framebuffer
        memcpy(framebuffer, backbuffer, screen_w * screen_h * 4);
        
        usleep(16666); // 60 FPS
    }
    
    return 0;
}