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

// UI scaling for 1080p
#define STATUS_HEIGHT 140
#define LARGE_TEXT 96
#define MEDIUM_TEXT 64
#define SMALL_TEXT 48
#define ICON_SIZE 200
#define MARGIN 60

typedef enum { LOCK_SCREEN, PIN_ENTRY, HOME_SCREEN, APP_SCREEN } AppState;

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
    int start_x, start_y;
    uint64_t last_touch_time;
    int action_taken;
    int is_dragging_indicator;
    int drag_start_y;
    int indicator_x;
} TouchState;

// Global variables
uint32_t *framebuffer = NULL, *backbuffer = NULL, *home_background = NULL;
int fb_fd, screen_w, screen_h, stride;
TouchDevice touch_devices[16];
int num_touch_devices = 0;
TouchState touch = {0};
AppState current_state = LOCK_SCREEN;
AppState animation_target_state = LOCK_SCREEN;
char pin_input[5] = {0};
int current_app = -1;
int battery_level = 87;
float current_scale = 1.0f;
float target_scale = 1.0f;
int is_animating = 0;
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
    draw_rect(buf, x + radius, y, w - 2*radius, h, color);
    draw_rect(buf, x, y + radius, w, h - 2*radius, color);
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
    float text_scale = stbtt_ScaleForPixelHeight(&font, font_size);
    int ascent, descent, line_gap;
    stbtt_GetFontVMetrics(&font, &ascent, &descent, &line_gap);
    int baseline = y + (int)(ascent * text_scale);
    
    int pos_x = x;
    for (const char *p = text; *p; p++) {
        int advance, left_bearing;
        stbtt_GetCodepointHMetrics(&font, *p, &advance, &left_bearing);
        
        int c_x1, c_y1, c_x2, c_y2;
        stbtt_GetCodepointBitmapBox(&font, *p, text_scale, text_scale, &c_x1, &c_y1, &c_x2, &c_y2);
        
        int bitmap_w = c_x2 - c_x1;
        int bitmap_h = c_y2 - c_y1;
        
        if (bitmap_w > 0 && bitmap_h > 0) {
            unsigned char *bitmap = malloc(bitmap_w * bitmap_h);
            stbtt_MakeCodepointBitmap(&font, bitmap, bitmap_w, bitmap_h, bitmap_w, text_scale, text_scale, *p);
            
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
        pos_x += (int)(advance * text_scale);
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
    if (time_str) strftime(time_str, 32, "%H:%M", tm);
    if (date_str) strftime(date_str, 64, "%A, %B %d", tm);
}

void draw_status_bar(uint32_t *buf) {
    char time_str[32];
    get_current_time(time_str, NULL);
    draw_text_centered(buf, time_str, MEDIUM_TEXT, 25, COLOR_WHITE);
    
    // Battery indicator
    int bat_x = screen_w - 200, bat_y = 25;
    int bat_w = 80, bat_h = 40;
    
    draw_rounded_rect(buf, bat_x, bat_y, bat_w, bat_h, 8, COLOR_WHITE);
    draw_rounded_rect(buf, bat_x + 3, bat_y + 3, bat_w - 6, bat_h - 6, 5, COLOR_BG);
    draw_rounded_rect(buf, bat_x + bat_w, bat_y + 12, 8, 16, 4, COLOR_WHITE);
    
    int fill_width = (int)((bat_w - 6) * battery_level / 100.0f);
    uint32_t fill_color = battery_level > 20 ? COLOR_GREEN : COLOR_RED;
    if (fill_width > 0) {
        draw_rounded_rect(buf, bat_x + 3, bat_y + 3, fill_width, bat_h - 6, 5, fill_color);
    }
    
    char bat_text[8];
    snprintf(bat_text, sizeof(bat_text), "%d%%", battery_level);
    draw_text(buf, bat_text, SMALL_TEXT, bat_x - 100, bat_y + 5, COLOR_WHITE);
    
    // Cellular bars - bottom-aligned
    int bars_bottom = 65;
    for (int i = 0; i < 4; i++) {
        int bar_h = 12 + i * 8;
        int bar_y = bars_bottom - bar_h;
        draw_rect(buf, 80 + i * 20, bar_y, 12, bar_h, COLOR_WHITE);
    }
}

int is_in_bottom_area(int touch_x, int touch_y) {
    // Entire bottom 30% of screen is gesture area
    return (touch_y >= screen_h * 0.7f);
}

float calculate_scale_from_drag(int drag_distance) {
    if (drag_distance < 20) return 1.0f; // Deadzone
    float max_drag = screen_h * 0.5f;
    float normalized = (drag_distance - 20) / max_drag;
    float scale = 1.0f - normalized * 0.6f;
    if (scale < 0.4f) scale = 0.4f;
    if (scale > 1.0f) scale = 1.0f;
    return scale;
}

// Simple blur - just like drawing text over circles
void apply_simple_blur(uint32_t *buf, float blur_amount) {
    if (blur_amount < 0.1f) return;
    
    int blur_radius = (int)(blur_amount * 6.0f);
    if (blur_radius < 1) return;
    
    // Simple box blur - treat it like drawing over existing pixels
    for (int y = blur_radius; y < screen_h - blur_radius; y++) {
        for (int x = blur_radius; x < screen_w - blur_radius; x++) {
            int r = 0, g = 0, b = 0, count = 0;
            
            for (int dy = -blur_radius; dy <= blur_radius; dy++) {
                for (int dx = -blur_radius; dx <= blur_radius; dx++) {
                    uint32_t pixel = buf[(y + dy) * screen_w + (x + dx)];
                    r += (pixel >> 16) & 0xFF;
                    g += (pixel >> 8) & 0xFF;
                    b += pixel & 0xFF;
                    count++;
                }
            }
            
            if (count > 0) {
                buf[y * screen_w + x] = 0xFF000000 | 
                    ((r / count) << 16) | ((g / count) << 8) | (b / count);
            }
        }
    }
}

// Draw scaled content - just like drawing text over background
void draw_scaled_window(uint32_t *dest, uint32_t *src, float scale, int offset_x, int offset_y) {
    int scaled_w = (int)(screen_w * scale);
    int scaled_h = (int)(screen_h * scale);
    int start_x = (screen_w - scaled_w) / 2 + offset_x;
    int start_y = (screen_h - scaled_h) / 2 + offset_y;
    
    for (int y = 0; y < scaled_h; y++) {
        for (int x = 0; x < scaled_w; x++) {
            int src_x = (int)(x / scale);
            int src_y = (int)(y / scale);
            
            if (src_x >= 0 && src_x < screen_w && src_y >= 0 && src_y < screen_h) {
                int dest_x = start_x + x;
                int dest_y = start_y + y;
                
                if (dest_x >= 0 && dest_x < screen_w && dest_y >= 0 && dest_y < screen_h) {
                    dest[dest_y * screen_w + dest_x] = src[src_y * screen_w + src_x];
                }
            }
        }
    }
}

void draw_lock_screen(uint32_t *buf) {
    clear_screen(buf, COLOR_BG);
    draw_status_bar(buf);
    
    char time_str[32], date_str[64];
    get_current_time(time_str, date_str);
    
    draw_text_centered(buf, time_str, LARGE_TEXT * 2, screen_h/2 - 250, COLOR_WHITE);
    draw_text_centered(buf, date_str, MEDIUM_TEXT, screen_h/2 - 120, COLOR_LIGHT_GRAY);
    draw_text_centered(buf, "Swipe up to unlock", SMALL_TEXT, screen_h - 200, COLOR_GRAY);
    
    // Home indicator
    draw_rounded_rect(buf, screen_w/2 - 100, screen_h - 80, 200, 8, 4, COLOR_LIGHT_GRAY);
}

void draw_pin_entry(uint32_t *buf) {
    clear_screen(buf, COLOR_BG);
    draw_status_bar(buf);
    
    draw_text_centered(buf, "Enter Passcode", MEDIUM_TEXT, STATUS_HEIGHT + 50, COLOR_WHITE);
    
    // PIN dots
    int dot_spacing = 80;
    int start_x = screen_w/2 - 120;
    for (int i = 0; i < 4; i++) {
        int x = start_x + i * dot_spacing;
        int y = STATUS_HEIGHT + 150;
        if (i < (int)strlen(pin_input)) {
            draw_circle_filled(buf, x, y, 20, COLOR_WHITE);
        } else {
            draw_circle_filled(buf, x, y, 20, COLOR_GRAY);
            draw_circle_filled(buf, x, y, 16, COLOR_BG);
        }
    }
    
    // PIN pad - same principle as drawing text over circles
    char pin_labels[] = "123456789*0#";
    int pad_start_x = screen_w/2 - 240;
    int pad_start_y = STATUS_HEIGHT + 250;
    
    for (int i = 0; i < 12; i++) {
        if (pin_labels[i] == '*' || pin_labels[i] == '#') continue;
        
        int row = i / 3;
        int col = i % 3;
        int x = pad_start_x + col * 160;
        int y = pad_start_y + row * 160;
        
        // Draw circle background first
        draw_circle_filled(buf, x + 80, y + 80, 70, COLOR_GRAY);
        
        // Draw text over the circle - SAME PRINCIPLE!
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
    
    // App grid
    int apps_per_row = 3;
    int grid_width = apps_per_row * ICON_SIZE + (apps_per_row - 1) * MARGIN;
    int start_x = (screen_w - grid_width) / 2;
    int start_y = STATUS_HEIGHT + 80;
    
    for (int i = 0; i < APP_COUNT && i < 12; i++) {
        int row = i / apps_per_row;
        int col = i % apps_per_row;
        int x = start_x + col * (ICON_SIZE + MARGIN);
        int y = start_y + row * (ICON_SIZE + MARGIN * 2);
        
        draw_rounded_rect(buf, x, y, ICON_SIZE, ICON_SIZE, 40, apps[i].color);
        
        int text_w = measure_text_width(apps[i].name, SMALL_TEXT);
        draw_text(buf, apps[i].name, SMALL_TEXT, x + (ICON_SIZE - text_w)/2, y + ICON_SIZE + 20, COLOR_WHITE);
    }
    
    // Only show home indicator when NOT scaling
    if (current_scale >= 0.98f && !touch.is_dragging_indicator) {
        draw_rounded_rect(buf, screen_w/2 - 100, screen_h - 80, 200, 8, 4, COLOR_WHITE);
    }
}

void draw_app_screen(uint32_t *buf) {
    clear_screen(buf, COLOR_BG);
    draw_status_bar(buf);
    
    if (current_app >= 0 && current_app < APP_COUNT) {
        App *app = &apps[current_app];
        
        draw_text_centered(buf, app->name, LARGE_TEXT, STATUS_HEIGHT + 50, COLOR_WHITE);
        
        if (current_app == 5) { // Calculator
            draw_text_centered(buf, "0", LARGE_TEXT * 2, STATUS_HEIGHT + 200, COLOR_WHITE);
            
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
    
    // Only show home indicator when NOT scaling
    if (current_scale >= 0.98f && !touch.is_dragging_indicator) {
        draw_rounded_rect(buf, screen_w/2 - 100, screen_h - 80, 200, 8, 4, COLOR_WHITE);
    }
}

void update_animations() {
    if (is_animating) {
        float diff = target_scale - current_scale;
        if (fabs(diff) < 0.01f || (target_scale == 0.0f && current_scale < 0.05f)) {
            current_scale = target_scale;
            is_animating = 0;
            
            // Complete state transition when animation finishes
            if (animation_target_state != current_state) {
                current_state = animation_target_state;
                // Reset scale to 1.0 when entering new state
                if (current_state == HOME_SCREEN) {
                    current_scale = 1.0f;
                    target_scale = 1.0f;
                }
                printf("✅ Animation complete → %s\n", 
                    current_state == HOME_SCREEN ? "Home" : "App");
            }
        } else {
            current_scale += diff * 0.3f; // Even faster animation (60fps * 0.3 = ~3-4 frames)
        }
    }
}

void handle_touch_input() {
    // iOS-style home indicator dragging with PROPER LOCKING
    
    if (touch.pressed && !touch.last_pressed) {
        if (is_in_bottom_area(touch.x, touch.y) && 
            (current_state == HOME_SCREEN || current_state == APP_SCREEN)) {
            touch.is_dragging_indicator = 1;
            touch.drag_start_y = touch.y;
            touch.indicator_x = touch.x; // Lock to finger immediately
            printf("🎯 Started dragging - locked to finger\n");
            return;
        }
        
        // Regular swipe detection
        touch.start_x = touch.x;
        touch.start_y = touch.y;
    }
    
    // Handle indicator drag - LOCK EVERYTHING TO FINGER
    if (touch.pressed && touch.is_dragging_indicator) {
        int drag_distance = touch.drag_start_y - touch.y;
        if (drag_distance >= 0) {
            current_scale = calculate_scale_from_drag(drag_distance);
            touch.indicator_x = touch.x; // Always lock bar to finger
        }
        return;
    }
    
    // Handle indicator release - PROPER ANIMATION DIRECTION
    if (!touch.pressed && touch.last_pressed && touch.is_dragging_indicator) {
        int final_drag = touch.drag_start_y - touch.y;
        float threshold = screen_h * 0.2f;
        
        if (final_drag > threshold) {
            // Going to home - app should shrink away
            printf("🏠 App shrinking away → Home\n");
            animation_target_state = HOME_SCREEN;
            target_scale = 0.0f; // Shrink app to nothing
            is_animating = 1;
        } else {
            // Spring back - app should grow back to full size
            printf("↩️ App growing back to full size\n");
            animation_target_state = current_state; // Stay in current state
            target_scale = 1.0f; // Grow back to full
            is_animating = 1;
        }
        
        touch.is_dragging_indicator = 0;
        return;
    }
    
    // Regular swipe from lock screen
    if (touch.pressed && current_state == LOCK_SCREEN) {
        int swipe_dy = touch.start_y - touch.y;
        if (swipe_dy > 80 && is_in_bottom_area(touch.start_x, touch.start_y)) {
            printf("🔓 Lock screen swipe → PIN entry\n");
            current_state = PIN_ENTRY;
            animation_target_state = PIN_ENTRY;
            return;
        }
    }
    
    // Regular button handling
    if (!touch.pressed || touch.action_taken || touch.is_dragging_indicator) return;
    
    if (current_state == PIN_ENTRY) {
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
            
            int dx = touch.x - center_x;
            int dy = touch.y - center_y;
            if ((dx*dx + dy*dy) <= (100*100)) {
                touch.action_taken = 1;
                
                if (strlen(pin_input) < 4) {
                    char digit[2] = {pin_labels[i], 0};
                    strcat(pin_input, digit);
                    
                    if (strlen(pin_input) == 4) {
                        if (strcmp(pin_input, "1234") == 0) {
                            printf("✅ Unlocked!\n");
                            current_state = HOME_SCREEN;
                            animation_target_state = HOME_SCREEN;
                        }
                        memset(pin_input, 0, sizeof(pin_input));
                    }
                }
                return;
            }
        }
    }
    
    else if (current_state == HOME_SCREEN) {
        int apps_per_row = 3;
        int grid_width = apps_per_row * ICON_SIZE + (apps_per_row - 1) * MARGIN;
        int start_x = (screen_w - grid_width) / 2;
        int start_y = STATUS_HEIGHT + 80;
        
        for (int i = 0; i < APP_COUNT && i < 12; i++) {
            int row = i / apps_per_row;
            int col = i % apps_per_row;
            int icon_x = start_x + col * (ICON_SIZE + MARGIN);
            int icon_y = start_y + row * (ICON_SIZE + MARGIN * 2);
            
            if (touch.x >= (icon_x - 50) && touch.x < (icon_x + ICON_SIZE + 50) &&
                touch.y >= (icon_y - 50) && touch.y < (icon_y + ICON_SIZE + 50)) {
                touch.action_taken = 1;
                current_app = i;
                current_state = APP_SCREEN;
                animation_target_state = APP_SCREEN;
                printf("🚀 Launched: %s\n", apps[i].name);
                return;
            }
        }
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
                
                if (!touch.pressed && touch.last_pressed) {
                    touch.action_taken = 0;
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
    if (home_background) free(home_background);
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
    
    home_background = malloc(screen_w * screen_h * 4);
    if (!home_background) { perror("Home background allocation failed"); exit(1); }
    
    init_touch_devices();
    
    // Initialize animation state
    animation_target_state = current_state;
    
    printf("📱 iOS-Style Phone OS - PERFECTED! 🚀\n");
    printf("🎯 Bar locks to finger, window locks to bar\n");
    printf("✨ Proper animation direction - app pops away!\n");
    printf("⚡ 60fps smooth animations, hidden bar while scaling\n");
    
    while (1) {
        read_touch_events();
        handle_touch_input();
        update_animations();
        
        // CONSISTENT RENDERING - ALWAYS USE BACKBUFFER FIRST
        if (current_scale >= 0.98f && !touch.is_dragging_indicator) {
            // Normal mode - render current screen to backbuffer
            switch (current_state) {
                case LOCK_SCREEN: draw_lock_screen(backbuffer); break;
                case PIN_ENTRY: draw_pin_entry(backbuffer); break;
                case HOME_SCREEN: draw_home_screen(backbuffer); break;
                case APP_SCREEN: draw_app_screen(backbuffer); break;
            }
        } else {
            // Scaling mode - render to backbuffer as background, then overlay
            
            // Step 1: Draw home screen as background to backbuffer
            draw_home_screen(backbuffer);
            
            // Step 2: Apply blur to backbuffer if needed
            float blur_amount = (1.0f - current_scale) * 0.5f;
            if (blur_amount > 0.1f) {
                apply_simple_blur(backbuffer, blur_amount);
            }
            
            // Step 3: Draw current screen to temp buffer
            switch (current_state) {
                case LOCK_SCREEN: draw_lock_screen(home_background); break;
                case PIN_ENTRY: draw_pin_entry(home_background); break;
                case HOME_SCREEN: draw_home_screen(home_background); break;
                case APP_SCREEN: draw_app_screen(home_background); break;
            }
            
            // Step 4: Draw scaled window over backbuffer - LOCKED TO BAR POSITION
            int offset_x = 0;
            if (touch.is_dragging_indicator && touch.indicator_x > 0) {
                offset_x = (touch.indicator_x - screen_w/2); // Full horizontal tracking
            }
            
            draw_scaled_window(backbuffer, home_background, current_scale, offset_x, 0);
            
            // Step 5: Draw floating home indicator bar - LOCKED TO FINGER
            if (touch.is_dragging_indicator && touch.indicator_x > 0) {
                int bar_w = 240; // Larger when dragging
                int bar_x = touch.indicator_x - bar_w/2;
                int bar_y = screen_h - 80;
                
                // Make sure bar stays on screen
                if (bar_x < 20) bar_x = 20;
                if (bar_x + bar_w > screen_w - 20) bar_x = screen_w - 20 - bar_w;
                
                draw_rounded_rect(backbuffer, bar_x, bar_y, bar_w, 8, 4, COLOR_BLUE);
            }
        }
        
        // Debug dot - always draw to backbuffer
        if (touch.pressed) {
            draw_circle_filled(backbuffer, touch.x, touch.y, 8, COLOR_RED);
        }
        
        // SINGLE CONSISTENT COPY TO FRAMEBUFFER
        memcpy(framebuffer, backbuffer, screen_w * screen_h * 4);
        
        usleep(16666); // 60 FPS
    }
    
    return 0;
}