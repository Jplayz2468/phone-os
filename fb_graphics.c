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

// Physics constants
#define GRAVITY 0.8f
#define BOUNCE_DAMPING 0.6f
#define FRICTION 0.95f

typedef enum { 
    LOCK_SCREEN, 
    PIN_ENTRY, 
    HOME_SCREEN, 
    APP_SCREEN 
} AppState;

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
    uint64_t touch_start_time;
    uint64_t last_touch_time;
    int action_taken;
} TouchState;

typedef struct {
    float x, y;           // Current position
    float velocity_y;     // Vertical velocity
    float target_y;       // Target position
    int is_dragging;      // Being dragged by user
    int drag_offset_y;    // Offset from touch point
    float capsule_top;    // Top of the capsule track
    float capsule_bottom; // Bottom of the capsule track
    int sphere_radius;    // Radius of the sphere
    int capsule_width;    // Width of the capsule
} PhysicsSphere;

// Apps configuration
App apps[] = {
    {"Phone", COLOR_GREEN, 0}, {"Messages", COLOR_GREEN, 1}, {"Camera", COLOR_GRAY, 2},
    {"Photos", COLOR_BLUE, 3}, {"Settings", COLOR_LIGHT_GRAY, 4}, {"Calculator", COLOR_ORANGE, 5},
    {"Clock", COLOR_PURPLE, 6}, {"Weather", COLOR_BLUE, 7}, {"Maps", COLOR_GREEN, 8},
    {"Music", COLOR_RED, 9}, {"Mail", COLOR_BLUE, 10}, {"Safari", COLOR_BLUE, 11}
};
#define APP_COUNT (sizeof(apps)/sizeof(apps[0]))

// Global variables
uint32_t *framebuffer = NULL, *backbuffer = NULL;
int fb_fd, screen_w, screen_h, stride;
TouchDevice touch_devices[16];
int num_touch_devices = 0;
TouchState touch = {0};
AppState current_state = LOCK_SCREEN;
AppState animation_target_state = LOCK_SCREEN;
char pin_input[5] = {0};
int current_app = -1;
int battery_level = 87;
float transition_progress = 0.0f;
int is_transitioning = 0;
stbtt_fontinfo font;
PhysicsSphere sphere = {0};

// Function declarations
uint64_t get_time_ms(void);
void clear_screen(uint32_t *buf, uint32_t color);
void draw_rect(uint32_t *buf, int x, int y, int w, int h, uint32_t color);
void draw_circle_filled(uint32_t *buf, int cx, int cy, int radius, uint32_t color);
void draw_rounded_rect(uint32_t *buf, int x, int y, int w, int h, int radius, uint32_t color);
int measure_text_width(const char *text, int font_size);
void draw_text(uint32_t *buf, const char *text, int font_size, int x, int y, uint32_t color);
void draw_text_centered(uint32_t *buf, const char *text, int font_size, int y, uint32_t color);
void get_current_time(char *time_str, char *date_str);
void draw_status_bar(uint32_t *buf);
void init_physics_sphere(void);
void update_physics_sphere(void);
void draw_physics_sphere(uint32_t *buf);
int is_touching_sphere(int touch_x, int touch_y);
void draw_lock_screen(uint32_t *buf);
void draw_pin_entry(uint32_t *buf);
void draw_home_screen(uint32_t *buf);
void draw_app_screen(uint32_t *buf);
void update_transitions(void);
void handle_touch_input(void);
void init_touch_devices(void);
void read_touch_events(void);
void cleanup_and_exit(int sig);
float ease_in_out_cubic(float t);

uint64_t get_time_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000ULL + ts.tv_nsec / 1000000ULL;
}

void clear_screen(uint32_t *buf, uint32_t color) {
    for (int i = 0; i < screen_w * screen_h; i++) {
        buf[i] = color;
    }
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
    
    // Cellular bars
    int bars_bottom = 65;
    for (int i = 0; i < 4; i++) {
        int bar_h = 12 + i * 8;
        int bar_y = bars_bottom - bar_h;
        draw_rect(buf, 80 + i * 20, bar_y, 12, bar_h, COLOR_WHITE);
    }
}

void init_physics_sphere(void) {
    // Initialize much bigger sphere and capsule
    sphere.x = screen_w / 2.0f;
    sphere.capsule_bottom = screen_h - 80;
    sphere.capsule_top = screen_h - 600;  // Much bigger capsule area
    sphere.sphere_radius = 60;            // Much bigger sphere
    sphere.capsule_width = 160;           // Much wider capsule
    sphere.y = sphere.capsule_bottom - sphere.sphere_radius;
    sphere.target_y = sphere.y;
    sphere.velocity_y = 0.0f;
    sphere.is_dragging = 0;
    sphere.drag_offset_y = 0;
}

void update_physics_sphere(void) {
    if (!sphere.is_dragging) {
        // Apply gravity towards bottom
        float target_y = sphere.capsule_bottom - sphere.sphere_radius;
        float force = (target_y - sphere.y) * 0.1f;
        sphere.velocity_y += force;
        sphere.velocity_y *= FRICTION;
        
        // Update position
        sphere.y += sphere.velocity_y;
        
        // Constrain to capsule bounds
        float min_y = sphere.capsule_top + sphere.sphere_radius;
        float max_y = sphere.capsule_bottom - sphere.sphere_radius;
        
        if (sphere.y < min_y) {
            sphere.y = min_y;
            sphere.velocity_y = 0;
        }
        if (sphere.y > max_y) {
            sphere.y = max_y;
            sphere.velocity_y *= -BOUNCE_DAMPING;
        }
    }
}

void draw_physics_sphere(uint32_t *buf) {
    if (current_state != LOCK_SCREEN && !is_transitioning) return;
    
    // Draw much bigger capsule track
    int capsule_x = sphere.x - sphere.capsule_width / 2;
    int capsule_h = sphere.capsule_bottom - sphere.capsule_top;
    int radius = sphere.capsule_width / 2;
    
    // Outer capsule
    draw_rounded_rect(buf, capsule_x, sphere.capsule_top, sphere.capsule_width, capsule_h, radius, COLOR_GRAY);
    
    // Inner capsule (hollow)
    int inner_margin = 12;
    draw_rounded_rect(buf, capsule_x + inner_margin, sphere.capsule_top + inner_margin, 
                     sphere.capsule_width - 2*inner_margin, capsule_h - 2*inner_margin, 
                     radius - inner_margin, COLOR_BG);
    
    // Draw much bigger sphere with glow effect
    uint32_t sphere_color = sphere.is_dragging ? COLOR_BLUE : COLOR_WHITE;
    
    // Outer glow layers
    draw_circle_filled(buf, sphere.x, sphere.y, sphere.sphere_radius + 20, 0x22FFFFFF);
    draw_circle_filled(buf, sphere.x, sphere.y, sphere.sphere_radius + 12, 0x44FFFFFF);
    draw_circle_filled(buf, sphere.x, sphere.y, sphere.sphere_radius + 6, 0x66FFFFFF);
    
    // Main sphere
    draw_circle_filled(buf, sphere.x, sphere.y, sphere.sphere_radius, sphere_color);
    
    // Inner highlight
    draw_circle_filled(buf, sphere.x - 15, sphere.y - 15, 20, 0xAAFFFFFF);
    
    // Progress indicator dots along the track
    int num_dots = 8;
    for (int i = 0; i < num_dots; i++) {
        float progress = (float)i / (num_dots - 1);
        int dot_y = sphere.capsule_top + sphere.sphere_radius + progress * (capsule_h - 2 * sphere.sphere_radius);
        
        // Highlight dots that are below current sphere position
        uint32_t dot_color = (dot_y > sphere.y) ? COLOR_GREEN : COLOR_LIGHT_GRAY;
        draw_circle_filled(buf, sphere.x - sphere.capsule_width/2 + 20, dot_y, 6, dot_color);
        draw_circle_filled(buf, sphere.x + sphere.capsule_width/2 - 20, dot_y, 6, dot_color);
    }
}

int is_touching_sphere(int touch_x, int touch_y) {
    if (current_state != LOCK_SCREEN) return 0;
    
    int dx = touch_x - sphere.x;
    int dy = touch_y - sphere.y;
    int distance_sq = dx*dx + dy*dy;
    int touch_radius = sphere.sphere_radius + 40; // Much larger touch area
    
    return distance_sq <= (touch_radius * touch_radius);
}

float ease_in_out_cubic(float t) {
    return t < 0.5f ? 4 * t * t * t : 1 - powf(-2 * t + 2, 3) / 2;
}

void draw_lock_screen(uint32_t *buf) {
    clear_screen(buf, COLOR_BG);
    
    // Apply transition effects
    float alpha = 1.0f;
    if (is_transitioning && animation_target_state == PIN_ENTRY) {
        alpha = 1.0f - transition_progress;
    }
    
    draw_status_bar(buf);
    
    char time_str[32], date_str[64];
    get_current_time(time_str, date_str);
    
    // Apply fade effect during transition
    uint32_t time_color = COLOR_WHITE;
    uint32_t date_color = COLOR_LIGHT_GRAY;
    uint32_t text_color = COLOR_GRAY;
    
    if (alpha < 1.0f) {
        int a = (int)(alpha * 255);
        time_color = (a << 24) | (time_color & 0x00FFFFFF);
        date_color = (a << 24) | (date_color & 0x00FFFFFF);
        text_color = (a << 24) | (text_color & 0x00FFFFFF);
    }
    
    draw_text_centered(buf, time_str, LARGE_TEXT * 2, screen_h/2 - 350, time_color);
    draw_text_centered(buf, date_str, MEDIUM_TEXT, screen_h/2 - 200, date_color);
    draw_text_centered(buf, "Drag sphere up to unlock", SMALL_TEXT, screen_h - 650, text_color);
    
    // Draw physics sphere
    draw_physics_sphere(buf);
}

void draw_pin_entry(uint32_t *buf) {
    clear_screen(buf, COLOR_BG);
    
    // Apply transition effects
    float alpha = 1.0f;
    int offset_x = 0;
    
    if (is_transitioning) {
        float eased_progress = ease_in_out_cubic(transition_progress);
        
        if (animation_target_state == PIN_ENTRY) {
            // Sliding in from right
            offset_x = (int)((1.0f - eased_progress) * screen_w);
            alpha = eased_progress;
        } else if (animation_target_state == HOME_SCREEN) {
            // Sliding out to left
            offset_x = -(int)(eased_progress * screen_w);
            alpha = 1.0f - eased_progress;
        }
    }
    
    draw_status_bar(buf);
    
    // Apply transition effects to colors
    uint32_t text_color = COLOR_WHITE;
    uint32_t dot_filled_color = COLOR_WHITE;
    uint32_t dot_empty_color = COLOR_GRAY;
    uint32_t button_color = COLOR_GRAY;
    
    if (alpha < 1.0f) {
        int a = (int)(alpha * 255);
        text_color = (a << 24) | (text_color & 0x00FFFFFF);
        dot_filled_color = (a << 24) | (dot_filled_color & 0x00FFFFFF);
        dot_empty_color = (a << 24) | (dot_empty_color & 0x00FFFFFF);
        button_color = (a << 24) | (button_color & 0x00FFFFFF);
    }
    
    draw_text_centered(buf, "Enter Passcode", MEDIUM_TEXT, STATUS_HEIGHT + 50 + offset_x, text_color);
    
    // PIN dots
    int dot_spacing = 80;
    int start_x = screen_w/2 - 120 + offset_x;
    for (int i = 0; i < 4; i++) {
        int x = start_x + i * dot_spacing;
        int y = STATUS_HEIGHT + 150;
        if (i < (int)strlen(pin_input)) {
            draw_circle_filled(buf, x, y, 20, dot_filled_color);
        } else {
            draw_circle_filled(buf, x, y, 20, dot_empty_color);
            draw_circle_filled(buf, x, y, 16, COLOR_BG);
        }
    }
    
    // PIN pad
    char pin_labels[] = "123456789*0#";
    int pad_start_x = screen_w/2 - 240 + offset_x;
    int pad_start_y = STATUS_HEIGHT + 250;
    
    for (int i = 0; i < 12; i++) {
        if (pin_labels[i] == '*' || pin_labels[i] == '#') continue;
        
        int row = i / 3;
        int col = i % 3;
        int x = pad_start_x + col * 160;
        int y = pad_start_y + row * 160;
        
        draw_circle_filled(buf, x + 80, y + 80, 70, button_color);
        
        char btn_text[2] = {pin_labels[i], 0};
        int text_w = measure_text_width(btn_text, MEDIUM_TEXT);
        draw_text(buf, btn_text, MEDIUM_TEXT, x + 80 - text_w/2, y + 60, text_color);
    }
}

void draw_home_screen(uint32_t *buf) {
    clear_screen(buf, COLOR_BG);
    
    // Apply transition effects
    float alpha = 1.0f;
    int offset_x = 0;
    float scale = 1.0f;
    
    if (is_transitioning) {
        float eased_progress = ease_in_out_cubic(transition_progress);
        
        if (animation_target_state == HOME_SCREEN) {
            // Scaling in from center
            scale = 0.3f + eased_progress * 0.7f;
            alpha = eased_progress;
        }
    }
    
    draw_status_bar(buf);
    
    // Apply transition effects to colors
    uint32_t text_color = COLOR_WHITE;
    uint32_t indicator_color = COLOR_WHITE;
    
    if (alpha < 1.0f) {
        int a = (int)(alpha * 255);
        text_color = (a << 24) | (text_color & 0x00FFFFFF);
        indicator_color = (a << 24) | (indicator_color & 0x00FFFFFF);
    }
    
    // App grid with scaling
    int apps_per_row = 3;
    int grid_width = apps_per_row * ICON_SIZE + (apps_per_row - 1) * MARGIN;
    int start_x = (screen_w - grid_width) / 2;
    int start_y = STATUS_HEIGHT + 80;
    
    int center_x = screen_w / 2;
    int center_y = screen_h / 2;
    
    for (int i = 0; i < APP_COUNT && i < 12; i++) {
        int row = i / apps_per_row;
        int col = i % apps_per_row;
        int x = start_x + col * (ICON_SIZE + MARGIN);
        int y = start_y + row * (ICON_SIZE + MARGIN * 2);
        
        // Apply scaling transform
        int scaled_x = center_x + (x - center_x) * scale;
        int scaled_y = center_y + (y - center_y) * scale;
        int scaled_size = ICON_SIZE * scale;
        
        draw_rounded_rect(buf, scaled_x, scaled_y, scaled_size, scaled_size, 40 * scale, apps[i].color);
        
        int text_w = measure_text_width(apps[i].name, SMALL_TEXT * scale);
        draw_text(buf, apps[i].name, SMALL_TEXT * scale, 
                 scaled_x + (scaled_size - text_w)/2, 
                 scaled_y + scaled_size + 20 * scale, text_color);
    }
    
    // Simple home indicator
    int indicator_w = 200 * scale;
    int indicator_h = 8 * scale;
    draw_rounded_rect(buf, screen_w/2 - indicator_w/2, screen_h - 80, 
                     indicator_w, indicator_h, 4 * scale, indicator_color);
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
    
    // Simple home indicator
    draw_rounded_rect(buf, screen_w/2 - 100, screen_h - 80, 200, 8, 4, COLOR_WHITE);
}

void update_transitions(void) {
    if (is_transitioning) {
        transition_progress += 0.04f; // Smooth transition speed
        
        if (transition_progress >= 1.0f) {
            transition_progress = 1.0f;
            is_transitioning = 0;
            current_state = animation_target_state;
            printf("✅ Transition complete → %s\n", 
                current_state == LOCK_SCREEN ? "Lock" :
                current_state == PIN_ENTRY ? "PIN" :
                current_state == HOME_SCREEN ? "Home" : "App");
        }
    }
}

void handle_touch_input(void) {
    // Handle sphere dragging for lock screen
    if (current_state == LOCK_SCREEN && !is_transitioning) {
        if (touch.pressed && !touch.last_pressed) {
            // Start of touch
            if (is_touching_sphere(touch.x, touch.y)) {
                sphere.is_dragging = 1;
                sphere.drag_offset_y = touch.y - (int)sphere.y;
                sphere.velocity_y = 0;
                printf("🎯 Started dragging sphere\n");
                return;
            }
        }
        
        if (touch.pressed && sphere.is_dragging) {
            // Update sphere position while dragging
            float new_y = touch.y - sphere.drag_offset_y;
            float min_y = sphere.capsule_top + sphere.sphere_radius;
            float max_y = sphere.capsule_bottom - sphere.sphere_radius;
            
            sphere.y = fmax(min_y, fmin(max_y, new_y));
            return;
        }
        
        if (!touch.pressed && touch.last_pressed && sphere.is_dragging) {
            // Released sphere
            sphere.is_dragging = 0;
            
            // Check if dragged high enough to unlock
            float unlock_threshold = sphere.capsule_top + 100;
            if (sphere.y <= unlock_threshold) {
                printf("🔓 Sphere unlocked! Going to PIN entry\n");
                animation_target_state = PIN_ENTRY;
                is_transitioning = 1;
                transition_progress = 0.0f;
                
                // Reset sphere to bottom
                sphere.y = sphere.capsule_bottom - sphere.sphere_radius;
                sphere.velocity_y = 0;
            } else {
                printf("⬇️ Sphere released, falling back down\n");
            }
            return;
        }
        
        return; // Don't process other touches while on lock screen
    }
    
    // Handle PIN entry
    if (current_state == PIN_ENTRY && !is_transitioning && touch.pressed && !touch.action_taken) {
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
                    printf("📱 PIN digit entered: %s\n", digit);
                    
                    if (strlen(pin_input) == 4) {
                        if (strcmp(pin_input, "1234") == 0) {
                            printf("✅ PIN correct! Going to home screen\n");
                            animation_target_state = HOME_SCREEN;
                            is_transitioning = 1;
                            transition_progress = 0.0f;
                        } else {
                            printf("❌ PIN incorrect!\n");
                        }
                        memset(pin_input, 0, sizeof(pin_input));
                    }
                }
                return;
            }
        }
    }
    
    // Handle home screen app launching
    if (current_state == HOME_SCREEN && !is_transitioning && touch.pressed && !touch.action_taken) {
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
                printf("🚀 Launched: %s\n", apps[i].name);
                return;
            }
        }
    }
    
    // Reset action flag when touch is released
    if (!touch.pressed && touch.last_pressed) {
        touch.action_taken = 0;
    }
}

void init_touch_devices(void) {
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

void read_touch_events(void) {
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

int main(void) {
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
    init_physics_sphere();
    
    animation_target_state = current_state;
    
    printf("📱 ENHANCED iOS Phone OS - Big Sphere & Smooth Transitions! 🚀\n");
    printf("🔵 HUGE draggable sphere (60px radius) in wide capsule\n");
    printf("🎾 Physics-based with satisfying bounce and fall\n");
    printf("✨ Smooth cubic-ease transitions between all screens\n");
    printf("🎭 Lock screen fades, PIN slides in, Home scales up\n");
    printf("🔓 Drag sphere to top → PIN → Home with animations\n");
    
    while (1) {
        read_touch_events();
        handle_touch_input();
        update_physics_sphere();
        update_transitions();
        
        // Draw current screen with transitions
        switch (current_state) {
            case LOCK_SCREEN: draw_lock_screen(backbuffer); break;
            case PIN_ENTRY: draw_pin_entry(backbuffer); break;
            case HOME_SCREEN: draw_home_screen(backbuffer); break;
            case APP_SCREEN: draw_app_screen(backbuffer); break;
        }
        
        // Draw touch indicator
        if (touch.pressed) {
            draw_circle_filled(backbuffer, touch.x, touch.y, 8, COLOR_RED);
        }
        
        memcpy(framebuffer, backbuffer, screen_w * screen_h * 4);
        
        usleep(16666); // 60 FPS
    }
    
    return 0;
}