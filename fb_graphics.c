#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <linux/fb.h>
#include <linux/kd.h>
#include <linux/vt.h>
#include <linux/input.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <stdint.h>
#include <signal.h>
#include <termios.h>
#include <dirent.h>

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

// UI Colors - Modern phone-like palette
#define COLOR_BACKGROUND  0xFF1A1A1A
#define COLOR_SURFACE     0xFF2D2D2D  
#define COLOR_PRIMARY     0xFF0078D4
#define COLOR_SECONDARY   0xFF6B73FF
#define COLOR_SUCCESS     0xFF00C851
#define COLOR_WARNING     0xFFFF9500
#define COLOR_ERROR       0xFFFF4444
#define COLOR_TEXT        0xFFFFFFFF
#define COLOR_TEXT_LIGHT  0xFFB0B0B0
#define COLOR_BORDER      0xFF404040
#define COLOR_SHADOW      0x40000000

// Legacy colors for compatibility
#define COLOR_RED     0xFFFF0000
#define COLOR_GREEN   0xFF00FF00
#define COLOR_BLUE    0xFF0000FF
#define COLOR_WHITE   0xFFFFFFFF
#define COLOR_BLACK   0xFF000000
#define COLOR_CYAN    0xFF00FFFF
#define COLOR_MAGENTA 0xFFFF00FF
#define COLOR_YELLOW  0xFFFFFF00

#define MAX_TOUCH_EVENTS 32
#define MAX_APPS 12
#define TARGET_FPS 60
#define FRAME_TIME_NS (1000000000 / TARGET_FPS)

#define MAX_TOUCH_EVENTS 32
#define MAX_APPS 12
#define TARGET_FPS 60
#define FRAME_TIME_NS (1000000000 / TARGET_FPS)
typedef enum {
    SCREEN_HOME,
    SCREEN_CALCULATOR,
    SCREEN_CLOCK,
    SCREEN_SETTINGS,
    SCREEN_WEATHER,
    SCREEN_GALLERY,
    SCREEN_MUSIC,
    SCREEN_CONTACTS
} ScreenType;

typedef struct {
    char name[32];
    char icon[4];  // Unicode emoji or simple text
    uint32_t color;
    ScreenType screen;
} App;

typedef struct {
    ScreenType current_screen;
    ScreenType previous_screen;
    App apps[MAX_APPS];
    int num_apps;
    
    // Calculator state
    char calc_display[32];
    double calc_value;
    char calc_operation;
    int calc_new_number;
    
    // Clock state
    struct tm current_time;
    
    // UI state
    int scroll_offset;
    float animation_progress;
    int selected_app;
} PhoneUI;

typedef struct {
    uint32_t *framebuffer;
    uint32_t *backbuffer;
    int fb_fd;
    size_t screensize;
    struct fb_var_screeninfo vinfo;
    struct fb_fix_screeninfo finfo;
} FrameBuffer;

typedef struct {
    int x, y;
    int pressed;
    int touch_id;
    uint64_t timestamp;
} TouchPoint;

// UI System initialization
void init_ui() {
    ui.current_screen = SCREEN_HOME;
    ui.previous_screen = SCREEN_HOME;
    ui.num_apps = 8;
    ui.selected_app = -1;
    ui.scroll_offset = 0;
    ui.animation_progress = 0;
    
    // Initialize apps
    strcpy(ui.apps[0].name, "Calculator");
    strcpy(ui.apps[0].icon, "🔢");
    ui.apps[0].color = COLOR_PRIMARY;
    ui.apps[0].screen = SCREEN_CALCULATOR;
    
    strcpy(ui.apps[1].name, "Clock");
    strcpy(ui.apps[1].icon, "🕐");
    ui.apps[1].color = COLOR_SUCCESS;
    ui.apps[1].screen = SCREEN_CLOCK;
    
    strcpy(ui.apps[2].name, "Settings");
    strcpy(ui.apps[2].icon, "⚙️");
    ui.apps[2].color = COLOR_TEXT_LIGHT;
    ui.apps[2].screen = SCREEN_SETTINGS;
    
    strcpy(ui.apps[3].name, "Weather");
    strcpy(ui.apps[3].icon, "🌤️");
    ui.apps[3].color = COLOR_SECONDARY;
    ui.apps[3].screen = SCREEN_WEATHER;
    
    strcpy(ui.apps[4].name, "Gallery");
    strcpy(ui.apps[4].icon, "📷");
    ui.apps[4].color = COLOR_WARNING;
    ui.apps[4].screen = SCREEN_GALLERY;
    
    strcpy(ui.apps[5].name, "Music");
    strcpy(ui.apps[5].icon, "🎵");
    ui.apps[5].color = COLOR_ERROR;
    ui.apps[5].screen = SCREEN_MUSIC;
    
    strcpy(ui.apps[6].name, "Contacts");
    strcpy(ui.apps[6].icon, "👥");
    ui.apps[6].color = COLOR_PRIMARY;
    ui.apps[6].screen = SCREEN_CONTACTS;
    
    strcpy(ui.apps[7].name, "Back");
    strcpy(ui.apps[7].icon, "←");
    ui.apps[7].color = COLOR_BORDER;
    ui.apps[7].screen = SCREEN_HOME;
    
    // Initialize calculator
    strcpy(ui.calc_display, "0");
    ui.calc_value = 0;
    ui.calc_operation = 0;
    ui.calc_new_number = 1;
}

static volatile int running = 1;
static FrameBuffer fb;
static PhoneUI ui;  // Make sure this is declared
static struct termios orig_termios;
static int console_fd = -1;
static int orig_kb_mode = -1;
static int orig_console_mode = -1;
static FILE *orig_stdout = NULL;
static FILE *orig_stderr = NULL;

void signal_handler(int sig) {
    (void)sig; // Suppress unused parameter warning
    running = 0;
}

// Get high resolution timestamp
static inline uint64_t get_time_ns() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

// Setup terminal control - TTY3 optimized
void setup_terminal() {
    orig_stdout = stdout;
    orig_stderr = stderr;
    
    // For TTY3, we want to work with the current terminal
    console_fd = open("/dev/tty", O_RDWR);
    if (console_fd < 0) {
        console_fd = STDIN_FILENO;
    }
    
    if (ioctl(console_fd, KDGKBMODE, &orig_kb_mode) == 0) {
        ioctl(console_fd, KDSKBMODE, K_RAW);
    }
    
    if (ioctl(console_fd, KDGETMODE, &orig_console_mode) == 0) {
        ioctl(console_fd, KDSETMODE, KD_GRAPHICS);
    }
    
    tcgetattr(STDIN_FILENO, &orig_termios);
    struct termios raw = orig_termios;
    raw.c_lflag &= ~(ECHO | ICANON | ISIG);
    raw.c_iflag &= ~(IXON | ICRNL);
    raw.c_oflag &= ~OPOST;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
    
    // Clear screen and hide cursor
    printf("\033[2J\033[H\033[?25l");
    fflush(stdout);
}

void restore_terminal() {
    printf("Restoring system...\n");
    
    if (console_fd >= 0 && orig_console_mode >= 0) {
        ioctl(console_fd, KDSETMODE, orig_console_mode);
    }
    
    if (console_fd >= 0 && orig_kb_mode >= 0) {
        ioctl(console_fd, KDSKBMODE, orig_kb_mode);
    }
    
    printf("\033[?25h\033[2J\033[H");
    fflush(stdout);
    
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
    
    if (console_fd >= 0 && console_fd != STDIN_FILENO) {
        close(console_fd);
    }
}

// Graphics primitives
static inline void draw_pixel(uint32_t *buffer, int x, int y, uint32_t color, int screen_width, int screen_height) {
    if (x >= 0 && x < screen_width && y >= 0 && y < screen_height) {
        buffer[y * screen_width + x] = color;
    }
}

static inline void draw_rect(uint32_t *buffer, int x, int y, int w, int h, uint32_t color, int screen_width, int screen_height) {
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
        int pairs = pixels / 2;
        for (int i = 0; i < pairs; i++) {
            *line64++ = color64;
        }
        
        if (pixels & 1) {
            *(uint32_t*)line64 = color;
        }
    }
}

static inline void draw_rounded_rect(uint32_t *buffer, int x, int y, int w, int h, int radius, uint32_t color, int screen_width, int screen_height) {
    // Draw main rectangle
    draw_rect(buffer, x + radius, y, w - 2*radius, h, color, screen_width, screen_height);
    draw_rect(buffer, x, y + radius, radius, h - 2*radius, color, screen_width, screen_height);
    draw_rect(buffer, x + w - radius, y + radius, radius, h - 2*radius, color, screen_width, screen_height);
    
    // Draw corners with simple approximation
    int r2 = radius * radius;
    for (int cy = 0; cy < radius; cy++) {
        for (int cx = 0; cx < radius; cx++) {
            if (cx*cx + cy*cy <= r2) {
                // Top-left
                draw_pixel(buffer, x + radius - cx, y + radius - cy, color, screen_width, screen_height);
                // Top-right  
                draw_pixel(buffer, x + w - radius + cx, y + radius - cy, color, screen_width, screen_height);
                // Bottom-left
                draw_pixel(buffer, x + radius - cx, y + h - radius + cy, color, screen_width, screen_height);
                // Bottom-right
                draw_pixel(buffer, x + w - radius + cx, y + h - radius + cy, color, screen_width, screen_height);
            }
        }
    }
}

static inline void draw_circle(uint32_t *buffer, int cx, int cy, int radius, uint32_t color, int screen_width, int screen_height) {
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
            if (dx * dx + dy2 <= r2) {
                line[x] = color;
            }
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
        size_t pairs = total_pixels / 2;
        
        for (size_t i = 0; i < pairs; i++) {
            buffer64[i] = color64;
        }
        
        if (total_pixels & 1) {
            buffer[total_pixels - 1] = color;
        }
    }
}

// Simple text rendering using 8x16 bitmap patterns
void draw_char(uint32_t *buffer, int x, int y, char c, uint32_t color, int screen_width, int screen_height) {
    // Simple 8x16 font patterns for digits and basic chars
    static const uint64_t font_patterns[] = {
        // 0-9
        0x3C42819999818142, 0x1030303030303030, 0x3C42020C30608142, 0x3C4202021C020242,
        0x0C1C3C6CCCFF0C0C, 0x7E40407C02024242, 0x3C40407C82828242, 0x7E02040810204040,
        0x3C42423C42424242, 0x3C42423E02024242,
        // A-Z (simplified)
        0x183C6666667E6666, 0xFC6666667C666666, 0x3C62C0C0C0C06262, 0xF86C6666666C6C68,
    };
    
    uint64_t pattern = 0;
    if (c >= '0' && c <= '9') {
        pattern = font_patterns[c - '0'];
    } else if (c >= 'A' && c <= 'D') {
        pattern = font_patterns[10 + (c - 'A')];
    }
    
    for (int row = 0; row < 8; row++) {
        uint8_t line = (pattern >> (8 * (7 - row))) & 0xFF;
        for (int col = 0; col < 8; col++) {
            if (line & (1 << (7 - col))) {
                draw_pixel(buffer, x + col, y + row, color, screen_width, screen_height);
                draw_pixel(buffer, x + col, y + row + 8, color, screen_width, screen_height);
            }
        }
    }
}

void draw_text(uint32_t *buffer, int x, int y, const char *text, uint32_t color, int screen_width, int screen_height) {
    int pos_x = x;
    for (int i = 0; text[i]; i++) {
        if (text[i] == ' ') {
            pos_x += 8;
        } else {
            draw_char(buffer, pos_x, y, text[i], color, screen_width, screen_height);
            pos_x += 10;
        }
    }
}

// Touch input system - Educational implementation
typedef struct {
    int fd;                    // File descriptor for touch device
    int min_x, max_x;         // Touch device coordinate ranges
    int min_y, max_y;
    int screen_width, screen_height;  // Screen resolution for mapping
    TouchPoint current_touch;  // Current touch state
    TouchPoint last_touch;     // Previous touch state
    int touch_available;       // Whether touch device was found
} TouchSystem;

static TouchSystem touch_system;

// Find and initialize touch device
int init_touch_system(int screen_w, int screen_h) {
    touch_system.fd = -1;
    touch_system.touch_available = 0;
    touch_system.screen_width = screen_w;
    touch_system.screen_height = screen_h;
    
    // List of common touch device paths
    const char* touch_paths[] = {
        "/dev/input/event0", "/dev/input/event1", "/dev/input/event2",
        "/dev/input/event3", "/dev/input/event4", "/dev/input/event5",
        "/dev/input/event6", "/dev/input/event7"
    };
    
    printf("Searching for touch devices...\n");
    
    for (int i = 0; i < 8; i++) {
        int fd = open(touch_paths[i], O_RDONLY | O_NONBLOCK);
        if (fd < 0) continue;
        
        // Check if this device has touch capabilities
        unsigned long evbit = 0;
        if (ioctl(fd, EVIOCGBIT(0, EV_MAX), &evbit) >= 0) {
            // Check for absolute positioning (touch/tablet devices)
            if (evbit & (1 << EV_ABS)) {
                printf("Found touch device: %s\n", touch_paths[i]);
                
                // Get device capabilities
                struct input_absinfo abs_x, abs_y;
                
                if (ioctl(fd, EVIOCGABS(ABS_X), &abs_x) >= 0) {
                    touch_system.min_x = abs_x.minimum;
                    touch_system.max_x = abs_x.maximum;
                    printf("  X range: %d to %d\n", touch_system.min_x, touch_system.max_x);
                }
                
                if (ioctl(fd, EVIOCGABS(ABS_Y), &abs_y) >= 0) {
                    touch_system.min_y = abs_y.minimum;
                    touch_system.max_y = abs_y.maximum;
                    printf("  Y range: %d to %d\n", touch_system.min_y, touch_system.max_y);
                }
                
                touch_system.fd = fd;
                touch_system.touch_available = 1;
                
                // Initialize touch state
                touch_system.current_touch.x = 0;
                touch_system.current_touch.y = 0;
                touch_system.current_touch.pressed = 0;
                touch_system.last_touch = touch_system.current_touch;
                
                return 0;
            }
        }
        close(fd);
    }
    
    printf("No touch device found\n");
    return -1;
}

// Map touch coordinates to screen coordinates
void map_touch_to_screen(int touch_x, int touch_y, int *screen_x, int *screen_y) {
    // Map touch device coordinates to screen coordinates
    if (touch_system.max_x > touch_system.min_x) {
        *screen_x = (touch_x - touch_system.min_x) * touch_system.screen_width / 
                    (touch_system.max_x - touch_system.min_x);
    } else {
        *screen_x = touch_x;
    }
    
    if (touch_system.max_y > touch_system.min_y) {
        *screen_y = (touch_y - touch_system.min_y) * touch_system.screen_height / 
                    (touch_system.max_y - touch_system.min_y);
    } else {
        *screen_y = touch_y;
    }
    
    // Clamp to screen bounds
    if (*screen_x < 0) *screen_x = 0;
    if (*screen_x >= touch_system.screen_width) *screen_x = touch_system.screen_width - 1;
    if (*screen_y < 0) *screen_y = 0;
    if (*screen_y >= touch_system.screen_height) *screen_y = touch_system.screen_height - 1;
}

// Process touch input events
int process_touch_events() {
    if (!touch_system.touch_available) return 0;
    
    struct input_event ev;
    int events_processed = 0;
    static int raw_x = 0, raw_y = 0;
    static int touch_down = 0;
    
    // Read all available events
    while (read(touch_system.fd, &ev, sizeof(ev)) == sizeof(ev)) {
        events_processed++;
        
        switch (ev.type) {
            case EV_ABS:
                // Absolute positioning events (main touch coordinates)
                if (ev.code == ABS_X || ev.code == ABS_MT_POSITION_X) {
                    raw_x = ev.value;
                    printf("Touch X: %d\n", raw_x);
                } else if (ev.code == ABS_Y || ev.code == ABS_MT_POSITION_Y) {
                    raw_y = ev.value;
                    printf("Touch Y: %d\n", raw_y);
                }
                break;
                
            case EV_KEY:
                // Touch press/release events
                if (ev.code == BTN_TOUCH || ev.code == BTN_LEFT) {
                    touch_down = ev.value;
                    printf("Touch %s\n", touch_down ? "DOWN" : "UP");
                    
                    // Update touch state
                    touch_system.last_touch = touch_system.current_touch;
                    
                    // Map coordinates to screen space
                    map_touch_to_screen(raw_x, raw_y, 
                                      &touch_system.current_touch.x, 
                                      &touch_system.current_touch.y);
                    
                    touch_system.current_touch.pressed = touch_down;
                    touch_system.current_touch.timestamp = get_time_ns();
                    
                    printf("Screen coords: %d, %d\n", 
                           touch_system.current_touch.x, 
                           touch_system.current_touch.y);
                }
                break;
                
            case EV_SYN:
                // Synchronization event - marks end of touch event group
                // This is when we know we have a complete touch update
                break;
        }
    }
    
    return events_processed;
}

// Get current touch state
TouchPoint get_current_touch() {
    return touch_system.current_touch;
}

// Check if touch just happened (transition from not pressed to pressed)
int touch_just_pressed() {
    return touch_system.current_touch.pressed && !touch_system.last_touch.pressed;
}

// Check if touch just released (transition from pressed to not pressed)  
int touch_just_released() {
    return !touch_system.current_touch.pressed && touch_system.last_touch.pressed;
}

// Check if touch is currently down
int touch_is_pressed() {
    return touch_system.current_touch.pressed;
}

// Simple gesture detection
typedef enum {
    GESTURE_NONE,
    GESTURE_TAP,
    GESTURE_SWIPE_UP,
    GESTURE_SWIPE_DOWN, 
    GESTURE_SWIPE_LEFT,
    GESTURE_SWIPE_RIGHT
} SimpleGesture;

SimpleGesture detect_simple_gesture() {
    if (!touch_just_released()) return GESTURE_NONE;
    
    int dx = touch_system.current_touch.x - touch_system.last_touch.x;
    int dy = touch_system.current_touch.y - touch_system.last_touch.y;
    int distance = sqrt(dx*dx + dy*dy);
    
    uint64_t duration = touch_system.current_touch.timestamp - touch_system.last_touch.timestamp;
    
    // If touch was very short and didn't move much, it's a tap
    if (distance < 50 && duration < 500000000) { // 500ms
        return GESTURE_TAP;
    }
    
    // If moved significantly, determine swipe direction
    if (distance > 100) {
        if (abs(dx) > abs(dy)) {
            return (dx > 0) ? GESTURE_SWIPE_RIGHT : GESTURE_SWIPE_LEFT;
        } else {
            return (dy > 0) ? GESTURE_SWIPE_DOWN : GESTURE_SWIPE_UP;
        }
    }
    
    return GESTURE_NONE;
}

void cleanup_touch_system() {
    if (touch_system.fd >= 0) {
        close(touch_system.fd);
        touch_system.fd = -1;
    }
    touch_system.touch_available = 0;
}

// Updated input handling using new touch system

void draw_home_screen() {
    int screen_width = fb.vinfo.xres;
    int screen_height = fb.vinfo.yres;
    
    // Draw status bar
    draw_rect(fb.backbuffer, 0, 0, screen_width, 80, COLOR_SURFACE, screen_width, screen_height);
    
    // Draw time
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char time_str[32];
    strftime(time_str, 32, "%H:%M", tm_info);
    draw_text(fb.backbuffer, 20, 25, time_str, COLOR_TEXT, screen_width, screen_height);
    
    // Battery indicator (fake)
    draw_rounded_rect(fb.backbuffer, screen_width - 80, 25, 50, 25, 5, COLOR_SUCCESS, screen_width, screen_height);
    draw_text(fb.backbuffer, screen_width - 75, 30, "85%", COLOR_BLACK, screen_width, screen_height);
    
    // Draw app grid
    int apps_per_row = 3;
    int icon_size = 120;
    int icon_margin = 40;
    int start_y = 150;
    
    for (int i = 0; i < ui.num_apps && i < 6; i++) { // Only show first 6 apps on home
        int row = i / apps_per_row;
        int col = i % apps_per_row;
        
        int x = col * (icon_size + icon_margin) + icon_margin + (screen_width - (apps_per_row * (icon_size + icon_margin) - icon_margin)) / 2;
        int y = start_y + row * (icon_size + 100);
        
        // Draw app icon background
        draw_rounded_rect(fb.backbuffer, x, y, icon_size, icon_size, 20, ui.apps[i].color, screen_width, screen_height);
        
        // Draw app name
        int text_x = x + (icon_size - strlen(ui.apps[i].name) * 10) / 2;
        draw_text(fb.backbuffer, text_x, y + icon_size + 20, ui.apps[i].name, COLOR_TEXT, screen_width, screen_height);
    }
    
    // Draw dock/bottom apps
    int dock_y = screen_height - 200;
    draw_rounded_rect(fb.backbuffer, 40, dock_y, screen_width - 80, 120, 30, COLOR_SURFACE, screen_width, screen_height);
    
    // Settings and back icons in dock
    for (int i = 6; i < ui.num_apps; i++) {
        int x = 80 + (i - 6) * 150;
        draw_rounded_rect(fb.backbuffer, x, dock_y + 20, 80, 80, 15, ui.apps[i].color, screen_width, screen_height);
        
        int text_x = x + (80 - strlen(ui.apps[i].name) * 10) / 2;
        draw_text(fb.backbuffer, text_x, dock_y + 110, ui.apps[i].name, COLOR_TEXT, screen_width, screen_height);
    }
}

void draw_calculator_screen() {
    int screen_width = fb.vinfo.xres;
    int screen_height = fb.vinfo.yres;
    
    // Calculator display
    draw_rounded_rect(fb.backbuffer, 40, 100, screen_width - 80, 120, 15, COLOR_SURFACE, screen_width, screen_height);
    
    // Display text (right-aligned)
    int text_width = strlen(ui.calc_display) * 10;
    int text_x = screen_width - 60 - text_width;
    draw_text(fb.backbuffer, text_x, 140, ui.calc_display, COLOR_TEXT, screen_width, screen_height);
    
    // Calculator buttons
    const char* buttons[] = {
        "C", "+/-", "%", "÷",
        "7", "8", "9", "×",
        "4", "5", "6", "-",
        "1", "2", "3", "+",
        "0", ".", "="
    };
    
    int button_size = (screen_width - 100) / 4;
    int start_y = 280;
    
    for (int i = 0; i < 19; i++) {
        int row = i / 4;
        int col = i % 4;
        
        if (i == 16) { // "0" button spans 2 columns
            col = 0;
            row = 4;
            draw_rounded_rect(fb.backbuffer, 40 + col * button_size, start_y + row * (button_size + 10), 
                            button_size * 2 - 5, button_size, 15, COLOR_BORDER, screen_width, screen_height);
        } else if (i >= 17) { // "." and "=" buttons
            col = (i == 17) ? 2 : 3;
            row = 4;
            draw_rounded_rect(fb.backbuffer, 40 + col * button_size, start_y + row * (button_size + 10), 
                            button_size - 10, button_size, 15, COLOR_BORDER, screen_width, screen_height);
        } else {
            draw_rounded_rect(fb.backbuffer, 40 + col * button_size, start_y + row * (button_size + 10), 
                            button_size - 10, button_size, 15, COLOR_BORDER, screen_width, screen_height);
        }
        
        // Button text
        int text_x = 40 + col * button_size + (button_size - strlen(buttons[i]) * 10) / 2;
        int text_y = start_y + row * (button_size + 10) + (button_size - 16) / 2;
        draw_text(fb.backbuffer, text_x, text_y, buttons[i], COLOR_TEXT, screen_width, screen_height);
    }
    
    // Back button
    draw_rounded_rect(fb.backbuffer, 40, 40, 100, 50, 10, COLOR_PRIMARY, screen_width, screen_height);
    draw_text(fb.backbuffer, 65, 55, "Back", COLOR_WHITE, screen_width, screen_height);
}

void draw_clock_screen() {
    int screen_width = fb.vinfo.xres;
    int screen_height = fb.vinfo.yres;
    
    // Get current time
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    
    // Draw large clock
    char time_str[32];
    strftime(time_str, 32, "%H:%M:%S", tm_info);
    
    int text_width = strlen(time_str) * 10;
    int text_x = (screen_width - text_width) / 2;
    draw_text(fb.backbuffer, text_x, screen_height / 2 - 50, time_str, COLOR_TEXT, screen_width, screen_height);
    
    // Draw date
    char date_str[64];
    strftime(date_str, 64, "%A %B %d %Y", tm_info);
    
    text_width = strlen(date_str) * 10;
    text_x = (screen_width - text_width) / 2;
    draw_text(fb.backbuffer, text_x, screen_height / 2 + 50, date_str, COLOR_TEXT_LIGHT, screen_width, screen_height);
    
    // Analog clock representation
    int center_x = screen_width / 2;
    int center_y = screen_height / 2 - 200;
    int radius = 100;
    
    // Clock face
    draw_circle(fb.backbuffer, center_x, center_y, radius, COLOR_BORDER, screen_width, screen_height);
    
    // Hour marks
    for (int i = 0; i < 12; i++) {
        float angle = (i * 30 - 90) * M_PI / 180;
        int x1 = center_x + (radius - 20) * cos(angle);
        int y1 = center_y + (radius - 20) * sin(angle);
        int x2 = center_x + (radius - 10) * cos(angle);
        int y2 = center_y + (radius - 10) * sin(angle);
        
        // Draw hour mark as line from inner to outer circle
        for (int j = 0; j < 10; j++) {
            int x = x1 + (x2 - x1) * j / 10;
            int y = y1 + (y2 - y1) * j / 10;
            draw_pixel(fb.backbuffer, x, y, COLOR_TEXT, screen_width, screen_height);
        }
    }
    
    // Clock hands
    float hour_angle = ((tm_info->tm_hour % 12) * 30 + tm_info->tm_min * 0.5 - 90) * M_PI / 180;
    float min_angle = (tm_info->tm_min * 6 - 90) * M_PI / 180;
    float sec_angle = (tm_info->tm_sec * 6 - 90) * M_PI / 180;
    
    // Center dot
    draw_circle(fb.backbuffer, center_x, center_y, 5, COLOR_TEXT, screen_width, screen_height);
    
    // Hour hand (short, thick)
    int hour_x = center_x + 40 * cos(hour_angle);
    int hour_y = center_y + 40 * sin(hour_angle);
    for (int i = 0; i < 40; i++) {
        int x = center_x + i * cos(hour_angle);
        int y = center_y + i * sin(hour_angle);
        draw_circle(fb.backbuffer, x, y, 2, COLOR_TEXT, screen_width, screen_height);
    }
    
    // Minute hand (longer, thinner)
    int min_x = center_x + 60 * cos(min_angle);
    int min_y = center_y + 60 * sin(min_angle);
    for (int i = 0; i < 60; i++) {
        int x = center_x + i * cos(min_angle);
        int y = center_y + i * sin(min_angle);
        draw_pixel(fb.backbuffer, x, y, COLOR_TEXT, screen_width, screen_height);
    }
    
    // Second hand (longest, thin, red)
    int sec_x = center_x + 70 * cos(sec_angle);
    int sec_y = center_y + 70 * sin(sec_angle);
    for (int i = 0; i < 70; i++) {
        int x = center_x + i * cos(sec_angle);
        int y = center_y + i * sin(sec_angle);
        draw_pixel(fb.backbuffer, x, y, COLOR_ERROR, screen_width, screen_height);
    }
    
    // Back button
    draw_rounded_rect(fb.backbuffer, 40, 40, 100, 50, 10, COLOR_PRIMARY, screen_width, screen_height);
    draw_text(fb.backbuffer, 65, 55, "Back", COLOR_WHITE, screen_width, screen_height);
}

void draw_settings_screen() {
    int screen_width = fb.vinfo.xres;
    int screen_height = fb.vinfo.yres;
    
    // Settings title
    draw_text(fb.backbuffer, 40, 100, "Settings", COLOR_TEXT, screen_width, screen_height);
    
    // Settings options
    const char* settings[] = {
        "Display Brightness",
        "Sound Volume", 
        "Wi-Fi Networks",
        "Bluetooth",
        "Battery Saver",
        "System Update"
    };
    
    for (int i = 0; i < 6; i++) {
        int y = 180 + i * 80;
        
        // Setting item background
        draw_rounded_rect(fb.backbuffer, 40, y, screen_width - 80, 60, 10, COLOR_SURFACE, screen_width, screen_height);
        
        // Setting name
        draw_text(fb.backbuffer, 60, y + 22, settings[i], COLOR_TEXT, screen_width, screen_height);
        
        // Toggle indicator (fake)
        uint32_t toggle_color = (i % 2) ? COLOR_SUCCESS : COLOR_BORDER;
        draw_rounded_rect(fb.backbuffer, screen_width - 120, y + 15, 60, 30, 15, toggle_color, screen_width, screen_height);
    }
    
    // Back button
    draw_rounded_rect(fb.backbuffer, 40, 40, 100, 50, 10, COLOR_PRIMARY, screen_width, screen_height);
    draw_text(fb.backbuffer, 65, 55, "Back", COLOR_WHITE, screen_width, screen_height);
}

// Touch input handling
void handle_touch_input() {
    if (!touch_system.touch_available) return;
    
    SimpleGesture gesture = detect_simple_gesture();
    TouchPoint current = get_current_touch();
    
    int screen_width = fb.vinfo.xres;
    int screen_height = fb.vinfo.yres;
    
    // Handle gestures based on current screen
    switch (ui.current_screen) {
        case SCREEN_HOME:
            if (gesture == GESTURE_TAP) {
                printf("Tap detected at %d, %d\n", current.x, current.y);
                
                // Check app icons (3x2 grid)
                int apps_per_row = 3;
                int icon_size = 120;
                int icon_margin = 40;
                int start_y = 150;
                
                for (int i = 0; i < 6; i++) {
                    int row = i / apps_per_row;
                    int col = i % apps_per_row;
                    
                    int x = col * (icon_size + icon_margin) + icon_margin + 
                           (screen_width - (apps_per_row * (icon_size + icon_margin) - icon_margin)) / 2;
                    int y = start_y + row * (icon_size + 100);
                    
                    if (current.x >= x && current.x <= x + icon_size &&
                        current.y >= y && current.y <= y + icon_size) {
                        printf("Opening app: %s\n", ui.apps[i].name);
                        ui.current_screen = ui.apps[i].screen;
                        break;
                    }
                }
                
                // Check dock apps
                int dock_y = screen_height - 200;
                for (int i = 6; i < ui.num_apps; i++) {
                    int x = 80 + (i - 6) * 150;
                    if (current.x >= x && current.x <= x + 80 &&
                        current.y >= dock_y + 20 && current.y <= dock_y + 100) {
                        printf("Opening dock app: %s\n", ui.apps[i].name);
                        ui.current_screen = ui.apps[i].screen;
                        break;
                    }
                }
            }
            break;
            
        default:
            // Back button for all other screens
            if (gesture == GESTURE_TAP &&
                current.x >= 40 && current.x <= 140 &&
                current.y >= 40 && current.y <= 90) {
                printf("Back button pressed\n");
                ui.current_screen = SCREEN_HOME;
            }
            break;
    }
}

void handle_touch_input() {
    if (!touch_system.touch_available) return;
    
    SimpleGesture gesture = detect_simple_gesture();
    TouchPoint current = get_current_touch();
    
    int screen_width = fb.vinfo.xres;
    int screen_height = fb.vinfo.yres;
    
    // Handle gestures based on current screen
    switch (ui.current_screen) {
        case SCREEN_HOME:
            if (gesture == GESTURE_TAP) {
                printf("Tap detected at %d, %d\n", current.x, current.y);
                
                // Check app icons (3x2 grid)
                int apps_per_row = 3;
                int icon_size = 120;
                int icon_margin = 40;
                int start_y = 150;
                
                for (int i = 0; i < 6; i++) {
                    int row = i / apps_per_row;
                    int col = i % apps_per_row;
                    
                    int x = col * (icon_size + icon_margin) + icon_margin + 
                           (screen_width - (apps_per_row * (icon_size + icon_margin) - icon_margin)) / 2;
                    int y = start_y + row * (icon_size + 100);
                    
                    if (current.x >= x && current.x <= x + icon_size &&
                        current.y >= y && current.y <= y + icon_size) {
                        printf("Opening app: %s\n", ui.apps[i].name);
                        ui.current_screen = ui.apps[i].screen;
                        break;
                    }
                }
                
                // Check dock apps
                int dock_y = screen_height - 200;
                for (int i = 6; i < ui.num_apps; i++) {
                    int x = 80 + (i - 6) * 150;
                    if (current.x >= x && current.x <= x + 80 &&
                        current.y >= dock_y + 20 && current.y <= dock_y + 100) {
                        printf("Opening dock app: %s\n", ui.apps[i].name);
                        ui.current_screen = ui.apps[i].screen;
                        break;
                    }
                }
            }
            break;
            
        default:
            // Back button for all other screens
            if (gesture == GESTURE_TAP &&
                current.x >= 40 && current.x <= 140 &&
                current.y >= 40 && current.y <= 90) {
                printf("Back button pressed\n");
                ui.current_screen = SCREEN_HOME;
            }
            break;
    }
}

void render_ui() {
    int screen_width = fb.vinfo.xres;
    int screen_height = fb.vinfo.yres;
    
    // Clear screen
    clear_screen(fb.backbuffer, COLOR_BACKGROUND, screen_width, screen_height);
    
    // Render current screen
    switch (ui.current_screen) {
        case SCREEN_HOME:
            draw_home_screen();
            break;
        case SCREEN_CALCULATOR:
            draw_calculator_screen();
            break;
        case SCREEN_CLOCK:
            draw_clock_screen();
            break;
        case SCREEN_SETTINGS:
            draw_settings_screen();
            break;
        default:
            draw_home_screen();
            break;
    }
    
    // Draw touch indicator for debugging
    TouchPoint current_touch = get_current_touch();
    if (touch_is_pressed()) {
        // Draw touch point as a circle
        draw_circle(fb.backbuffer, current_touch.x, current_touch.y, 30, COLOR_PRIMARY, screen_width, screen_height);
        // Draw smaller inner circle
        draw_circle(fb.backbuffer, current_touch.x, current_touch.y, 15, COLOR_WHITE, screen_width, screen_height);
        
        // Show coordinates for debugging
        char coord_text[32];
        snprintf(coord_text, sizeof(coord_text), "%d,%d", current_touch.x, current_touch.y);
        draw_text(fb.backbuffer, current_touch.x - 30, current_touch.y - 50, coord_text, COLOR_WHITE, screen_width, screen_height);
    }
}
int init_framebuffer() {
    fb.fb_fd = open("/dev/fb0", O_RDWR);
    if (fb.fb_fd == -1) {
        perror("Error opening framebuffer");
        return -1;
    }

    if (ioctl(fb.fb_fd, FBIOGET_FSCREENINFO, &fb.finfo) == -1) {
        perror("Error reading fixed screen info");
        return -1;
    }

    if (ioctl(fb.fb_fd, FBIOGET_VSCREENINFO, &fb.vinfo) == -1) {
        perror("Error reading variable screen info");
        return -1;
    }

    printf("Current screen: %dx%d, %d bpp, line_length=%d\n", 
           fb.vinfo.xres, fb.vinfo.yres, fb.vinfo.bits_per_pixel, fb.finfo.line_length);
    
    int actual_width = fb.vinfo.xres;
    int actual_height = fb.vinfo.yres;
    
    fb.screensize = fb.finfo.line_length * fb.vinfo.yres;
    
    fb.framebuffer = (uint32_t*)mmap(0, fb.screensize, PROT_READ | PROT_WRITE, MAP_SHARED, fb.fb_fd, 0);
    if (fb.framebuffer == MAP_FAILED) {
        perror("Error mapping framebuffer");
        return -1;
    }

    if (posix_memalign((void**)&fb.backbuffer, 64, actual_width * actual_height * sizeof(uint32_t)) != 0) {
        perror("Error allocating back buffer");
        munmap(fb.framebuffer, fb.screensize);
        return -1;
    }
    
    memset(fb.framebuffer, 0, fb.screensize);
    memset(fb.backbuffer, 0, actual_width * actual_height * sizeof(uint32_t));

    printf("Framebuffer initialized: %dx%d, %zu bytes\n", actual_width, actual_height, fb.screensize);
    return 0;
}

void cleanup_framebuffer() {
    restore_terminal();
    
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

void swap_buffers() {
    int actual_width = fb.vinfo.xres;
    int actual_height = fb.vinfo.yres;
    
    if (fb.finfo.line_length == actual_width * sizeof(uint32_t)) {
        memcpy(fb.framebuffer, fb.backbuffer, actual_width * actual_height * sizeof(uint32_t));
    } else {
        uint32_t *src = fb.backbuffer;
        uint8_t *dst = (uint8_t*)fb.framebuffer;
        
        for (int y = 0; y < actual_height; y++) {
            memcpy(dst, src, actual_width * sizeof(uint32_t));
            src += actual_width;
            dst += fb.finfo.line_length;
        }
    }
}

int main() {
    printf("Orange Pi 5 Phone UI - Starting on TTY3...\n");
    printf("Features: Touch Support, Apps, Modern UI\n");
    fflush(stdout);
    
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    setup_terminal();
    
    if (init_framebuffer() < 0) {
        restore_terminal();
        return 1;
    }
    
    // Initialize touch system with screen dimensions
    int screen_w = fb.vinfo.xres;
    int screen_h = fb.vinfo.yres;
    
    if (init_touch_system(screen_w, screen_h) < 0) {
        printf("Touch input not available - UI will work without touch\n");
    }
    
    init_ui();
    
    uint64_t frame_count = 0;
    
    printf("Starting phone UI loop...\n");
    
    while (running) {
        uint64_t frame_start = get_time_ns();
        
        // Process touch input events
        process_touch_events();
        
        // Handle UI input 
        handle_touch_input();
        
        // Render UI
        render_ui();
        
        // Display
        swap_buffers();
        
        frame_count++;
        
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
    }
    
    cleanup_touch_system();
    cleanup_framebuffer();
    printf("Phone UI ended.\n");
    return 0;
}