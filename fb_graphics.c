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
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <poll.h>

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

// UI Colors
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
#define COLOR_WHITE       0xFFFFFFFF
#define COLOR_BLACK       0xFF000000

#define MAX_APPS 12
#define TARGET_FPS 60
#define FRAME_TIME_NS (1000000000 / TARGET_FPS)
#define MAX_TOUCH_DEVICES 16

// Touch device management
static struct {
    int fd;
    char name[256];
    int min_x, max_x, min_y, max_y;
} touch_devs[MAX_TOUCH_DEVICES];

static int num_touch_devs = 0;

// Type definitions
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
    uint64_t timestamp;
} TouchPoint;

typedef struct {
    int fd;
    int min_x, max_x;
    int min_y, max_y;
    int screen_width, screen_height;
    TouchPoint current_touch;
    TouchPoint last_touch;
    int touch_available;
} TouchSystem;

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
    char icon[4];
    uint32_t color;
    ScreenType screen;
} App;

typedef struct {
    ScreenType current_screen;
    App apps[MAX_APPS];
    int num_apps;
    char calc_display[32];
    double calc_value;
    char calc_operation;
    int calc_new_number;
} PhoneUI;

typedef enum {
    GESTURE_NONE,
    GESTURE_TAP,
    GESTURE_SWIPE_UP,
    GESTURE_SWIPE_DOWN, 
    GESTURE_SWIPE_LEFT,
    GESTURE_SWIPE_RIGHT
} SimpleGesture;

// Global variables
static volatile int running = 1;
static FrameBuffer fb;
static TouchSystem touch_system;
static PhoneUI ui;
static struct termios orig_termios;
static int console_fd = -1;
static int orig_kb_mode = -1;
static int orig_console_mode = -1;

// Signal handler
void signal_handler(int sig) {
    (void)sig;
    running = 0;
}

// High resolution timer
static inline uint64_t get_time_ns() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

// Terminal setup
void setup_terminal() {
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
    
    for (int row = y; row < y + h; row++) {
        uint32_t *line = &buffer[row * screen_width + x];
        for (int col = 0; col < w; col++) {
            line[col] = color;
        }
    }
}

static inline void draw_rounded_rect(uint32_t *buffer, int x, int y, int w, int h, int radius, uint32_t color, int screen_width, int screen_height) {
    draw_rect(buffer, x + radius, y, w - 2*radius, h, color, screen_width, screen_height);
    draw_rect(buffer, x, y + radius, radius, h - 2*radius, color, screen_width, screen_height);
    draw_rect(buffer, x + w - radius, y + radius, radius, h - 2*radius, color, screen_width, screen_height);
    
    int r2 = radius * radius;
    for (int cy = 0; cy < radius; cy++) {
        for (int cx = 0; cx < radius; cx++) {
            if (cx*cx + cy*cy <= r2) {
                draw_pixel(buffer, x + radius - cx, y + radius - cy, color, screen_width, screen_height);
                draw_pixel(buffer, x + w - radius + cx, y + radius - cy, color, screen_width, screen_height);
                draw_pixel(buffer, x + radius - cx, y + h - radius + cy, color, screen_width, screen_height);
                draw_pixel(buffer, x + w - radius + cx, y + h - radius + cy, color, screen_width, screen_height);
            }
        }
    }
}

static inline void draw_circle(uint32_t *buffer, int cx, int cy, int radius, uint32_t color, int screen_width, int screen_height) {
    int r2 = radius * radius;
    for (int y = cy - radius; y <= cy + radius; y++) {
        for (int x = cx - radius; x <= cx + radius; x++) {
            int dx = x - cx;
            int dy = y - cy;
            if (dx * dx + dy * dy <= r2) {
                draw_pixel(buffer, x, y, color, screen_width, screen_height);
            }
        }
    }
}

static inline void clear_screen(uint32_t *buffer, uint32_t color, int screen_width, int screen_height) {
    int total_pixels = screen_width * screen_height;
    for (int i = 0; i < total_pixels; i++) {
        buffer[i] = color;
    }
}

// Simple text rendering
void draw_char(uint32_t *buffer, int x, int y, char c, uint32_t color, int screen_width, int screen_height) {
    static const uint64_t font_patterns[] = {
        0x3C42819999818142, 0x1030303030303030, 0x3C42020C30608142, 0x3C4202021C020242,
        0x0C1C3C6CCCFF0C0C, 0x7E40407C02024242, 0x3C40407C82828242, 0x7E02040810204040,
        0x3C42423C42424242, 0x3C42423E02024242
    };
    
    uint64_t pattern = 0;
    if (c >= '0' && c <= '9') {
        pattern = font_patterns[c - '0'];
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

// Touch system - User's improved implementation
int init_touch_system(int screen_w, int screen_h) {
    touch_system.touch_available = 0;
    touch_system.screen_width = screen_w;
    touch_system.screen_height = screen_h;
    num_touch_devs = 0;

    printf("=== IMPROVED TOUCH SYSTEM INITIALIZATION ===\n");
    printf("Screen resolution: %dx%d\n", screen_w, screen_h);

    for (int i = 0; i < MAX_TOUCH_DEVICES; i++) {
        char path[256];
        snprintf(path, sizeof(path), "/dev/input/event%d", i);
        
        int fd = open(path, O_RDONLY | O_NONBLOCK);
        if (fd < 0) continue;

        unsigned long evbits[(EV_MAX+7)/8/sizeof(long)] = {0};
        if (ioctl(fd, EVIOCGBIT(0, sizeof(evbits)), evbits) < 0) {
            close(fd);
            continue;
        }

        if (!((evbits[EV_ABS/sizeof(long)] >> (EV_ABS % (8 * sizeof(long)))) & 1)) {
            close(fd);
            continue;
        }

        char name[256] = "Unknown";
        ioctl(fd, EVIOCGNAME(sizeof(name)), name);

        struct input_absinfo abs_x, abs_y;
        ioctl(fd, EVIOCGABS(ABS_X), &abs_x);
        ioctl(fd, EVIOCGABS(ABS_Y), &abs_y);

        printf("Touch device: %s (%s)\n", path, name);
        printf("  X range: %d to %d\n", abs_x.minimum, abs_x.maximum);
        printf("  Y range: %d to %d\n", abs_y.minimum, abs_y.maximum);

        touch_devs[num_touch_devs].fd = fd;
        strncpy(touch_devs[num_touch_devs].name, name, sizeof(name)-1);
        touch_devs[num_touch_devs].name[sizeof(name)-1] = '\0';
        touch_devs[num_touch_devs].min_x = abs_x.minimum;
        touch_devs[num_touch_devs].max_x = abs_x.maximum;
        touch_devs[num_touch_devs].min_y = abs_y.minimum;
        touch_devs[num_touch_devs].max_y = abs_y.maximum;
        num_touch_devs++;
    }

    touch_system.touch_available = (num_touch_devs > 0);
    
    if (touch_system.touch_available) {
        printf("=== TOUCH SYSTEM READY ===\n");
        printf("Found %d touch device(s)\n", num_touch_devs);
        
        // Initialize touch state
        touch_system.current_touch.x = screen_w / 2;
        touch_system.current_touch.y = screen_h / 2;
        touch_system.current_touch.pressed = 0;
        touch_system.last_touch = touch_system.current_touch;
    } else {
        printf("=== NO TOUCH DEVICES FOUND ===\n");
    }
    
    return touch_system.touch_available ? 0 : -1;
}

void map_touch_to_screen(int touch_x, int touch_y, int *screen_x, int *screen_y) {
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
    
    if (*screen_x < 0) *screen_x = 0;
    if (*screen_x >= touch_system.screen_width) *screen_x = touch_system.screen_width - 1;
    if (*screen_y < 0) *screen_y = 0;
    if (*screen_y >= touch_system.screen_height) *screen_y = touch_system.screen_height - 1;
}

int process_touch_events() {
    if (!touch_system.touch_available) return 0;

    struct pollfd fds[MAX_TOUCH_DEVICES];
    for (int i = 0; i < num_touch_devs; i++) {
        fds[i].fd = touch_devs[i].fd;
        fds[i].events = POLLIN;
    }

    int changed = poll(fds, num_touch_devs, 0);
    if (changed <= 0) return 0;

    int found = 0;
    for (int i = 0; i < num_touch_devs; i++) {
        if (!(fds[i].revents & POLLIN)) continue;

        struct input_event ev;
        while (read(fds[i].fd, &ev, sizeof(ev)) == sizeof(ev)) {
            printf("Touch event from device %d: type=%d code=%d value=%d\n", i, ev.type, ev.code, ev.value);
            
            if (ev.type == EV_ABS) {
                if (ev.code == ABS_X || ev.code == ABS_MT_POSITION_X) {
                    int range = touch_devs[i].max_x - touch_devs[i].min_x + 1;
                    if (range > 0) {
                        touch_system.current_touch.x = (ev.value - touch_devs[i].min_x) * touch_system.screen_width / range;
                        printf("  -> X mapped: %d -> %d\n", ev.value, touch_system.current_touch.x);
                    }
                }
                if (ev.code == ABS_Y || ev.code == ABS_MT_POSITION_Y) {
                    int range = touch_devs[i].max_y - touch_devs[i].min_y + 1;
                    if (range > 0) {
                        touch_system.current_touch.y = (ev.value - touch_devs[i].min_y) * touch_system.screen_height / range;
                        printf("  -> Y mapped: %d -> %d\n", ev.value, touch_system.current_touch.y);
                    }
                }
                if (ev.code == ABS_MT_TRACKING_ID && ev.value == -1) {
                    touch_system.current_touch.pressed = 0;
                    printf("  -> Touch RELEASED (tracking ID)\n");
                } else if (ev.code == ABS_MT_TRACKING_ID && ev.value >= 0) {
                    touch_system.current_touch.pressed = 1;
                    printf("  -> Touch PRESSED (tracking ID)\n");
                }
            } else if (ev.type == EV_KEY && ev.code == BTN_TOUCH) {
                touch_system.current_touch.pressed = ev.value;
                printf("  -> Touch %s (BTN_TOUCH)\n", ev.value ? "PRESSED" : "RELEASED");
            } else if (ev.type == EV_SYN && ev.code == SYN_REPORT) {
                touch_system.last_touch = touch_system.current_touch;
                touch_system.current_touch.timestamp = get_time_ns();
                printf("  -> SYN_REPORT: Touch at (%d,%d) pressed=%d\n", 
                       touch_system.current_touch.x, touch_system.current_touch.y, touch_system.current_touch.pressed);
                found = 1;
            }
        }
    }
    return found;
}

TouchPoint get_current_touch() {
    return touch_system.current_touch;
}

int touch_just_pressed() {
    return touch_system.current_touch.pressed && !touch_system.last_touch.pressed;
}

int touch_just_released() {
    return !touch_system.current_touch.pressed && touch_system.last_touch.pressed;
}

int touch_is_pressed() {
    return touch_system.current_touch.pressed;
}

SimpleGesture detect_simple_gesture() {
    if (!touch_just_released()) return GESTURE_NONE;
    
    int dx = touch_system.current_touch.x - touch_system.last_touch.x;
    int dy = touch_system.current_touch.y - touch_system.last_touch.y;
    int distance = sqrt(dx*dx + dy*dy);
    
    uint64_t duration = touch_system.current_touch.timestamp - touch_system.last_touch.timestamp;
    
    if (distance < 50 && duration < 500000000) {
        return GESTURE_TAP;
    }
    
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

// Enhanced input processing
void process_all_input() {
    if (touch_system.touch_available) {
        // Process real touch events
        int events = process_touch_events();
        if (events > 0) {
            printf("Processed %d touch events\n", events);
        }
    } else {
        // Fallback to keyboard simulation
        simulate_touch_with_keyboard();
    }
}

// Fallback: Simulate touch with keyboard for testing
void simulate_touch_with_keyboard() {
    static int sim_x = 400;
    static int sim_y = 300;
    
    // Set stdin to non-blocking
    int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);
    
    // Read keyboard input (non-blocking)
    int ch = getchar();
    if (ch != EOF) {
        printf("=== KEYBOARD INPUT: %c (%d) ===\n", ch, ch);
        
        touch_system.last_touch = touch_system.current_touch;
        
        switch (ch) {
            case 'w': case 'W':
                sim_y -= 50;
                printf("Moving UP to %d,%d\n", sim_x, sim_y);
                break;
            case 's': case 'S':
                sim_y += 50;
                printf("Moving DOWN to %d,%d\n", sim_x, sim_y);
                break;
            case 'a': case 'A':
                sim_x -= 50;
                printf("Moving LEFT to %d,%d\n", sim_x, sim_y);
                break;
            case 'd': case 'D':
                sim_x += 50;
                printf("Moving RIGHT to %d,%d\n", sim_x, sim_y);
                break;
            case ' ':  // Spacebar = tap
                touch_system.current_touch.x = sim_x;
                touch_system.current_touch.y = sim_y;
                touch_system.current_touch.pressed = 1;
                touch_system.current_touch.timestamp = get_time_ns();
                printf("=== SIMULATED TAP at %d, %d ===\n", sim_x, sim_y);
                // Restore blocking mode before returning
                fcntl(STDIN_FILENO, F_SETFL, flags);
                return;
            case 't': case 'T':  // Test mode
                printf("=== RUNNING TOUCH TEST ===\n");
                test_touch_system();
                break;
            case 'c': case 'C':  // Center cursor
                sim_x = touch_system.screen_width / 2;
                sim_y = touch_system.screen_height / 2;
                printf("Centered cursor at %d,%d\n", sim_x, sim_y);
                break;
            case 'q': case 'Q': // Quit
                printf("=== QUIT REQUESTED ===\n");
                running = 0;
                fcntl(STDIN_FILENO, F_SETFL, flags);
                return;
            default:
                printf("Unknown key. Controls: WASD=move, SPACE=tap, T=test, C=center, Q=quit\n");
                break;
        }
        
        // Clamp coordinates
        if (sim_x < 0) sim_x = 0;
        if (sim_x >= touch_system.screen_width) sim_x = touch_system.screen_width - 1;
        if (sim_y < 0) sim_y = 0;
        if (sim_y >= touch_system.screen_height) sim_y = touch_system.screen_height - 1;
        
        // Update cursor position
        touch_system.current_touch.x = sim_x;
        touch_system.current_touch.y = sim_y;
        touch_system.current_touch.pressed = 0; // Not pressed, just moving
        touch_system.current_touch.timestamp = get_time_ns();
        
        printf("Cursor position: %d, %d\n", sim_x, sim_y);
    }
    
    // Restore blocking mode
    fcntl(STDIN_FILENO, F_SETFL, flags);
}

// UI System
void init_ui() {
    ui.current_screen = SCREEN_HOME;
    ui.num_apps = 8;
    
    strcpy(ui.apps[0].name, "Calculator");
    strcpy(ui.apps[0].icon, "CALC");
    ui.apps[0].color = COLOR_PRIMARY;
    ui.apps[0].screen = SCREEN_CALCULATOR;
    
    strcpy(ui.apps[1].name, "Clock");
    strcpy(ui.apps[1].icon, "TIME");
    ui.apps[1].color = COLOR_SUCCESS;
    ui.apps[1].screen = SCREEN_CLOCK;
    
    strcpy(ui.apps[2].name, "Settings");
    strcpy(ui.apps[2].icon, "SETT");
    ui.apps[2].color = COLOR_TEXT_LIGHT;
    ui.apps[2].screen = SCREEN_SETTINGS;
    
    strcpy(ui.apps[3].name, "Weather");
    strcpy(ui.apps[3].icon, "WEAT");
    ui.apps[3].color = COLOR_SECONDARY;
    ui.apps[3].screen = SCREEN_WEATHER;
    
    strcpy(ui.apps[4].name, "Gallery");
    strcpy(ui.apps[4].icon, "PICS");
    ui.apps[4].color = COLOR_WARNING;
    ui.apps[4].screen = SCREEN_GALLERY;
    
    strcpy(ui.apps[5].name, "Music");
    strcpy(ui.apps[5].icon, "SONG");
    ui.apps[5].color = COLOR_ERROR;
    ui.apps[5].screen = SCREEN_MUSIC;
    
    strcpy(ui.apps[6].name, "Contacts");
    strcpy(ui.apps[6].icon, "PPLE");
    ui.apps[6].color = COLOR_PRIMARY;
    ui.apps[6].screen = SCREEN_CONTACTS;
    
    strcpy(ui.apps[7].name, "Back");
    strcpy(ui.apps[7].icon, "BACK");
    ui.apps[7].color = COLOR_BORDER;
    ui.apps[7].screen = SCREEN_HOME;
    
    strcpy(ui.calc_display, "0");
    ui.calc_value = 0;
    ui.calc_operation = 0;
    ui.calc_new_number = 1;
}

// Screen drawing functions
void draw_home_screen() {
    int screen_width = fb.vinfo.xres;
    int screen_height = fb.vinfo.yres;
    
    // Status bar
    draw_rect(fb.backbuffer, 0, 0, screen_width, 80, COLOR_SURFACE, screen_width, screen_height);
    
    // Time
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char time_str[32];
    strftime(time_str, 32, "%H:%M", tm_info);
    draw_text(fb.backbuffer, 20, 25, time_str, COLOR_TEXT, screen_width, screen_height);
    
    // Battery
    draw_rounded_rect(fb.backbuffer, screen_width - 80, 25, 50, 25, 5, COLOR_SUCCESS, screen_width, screen_height);
    draw_text(fb.backbuffer, screen_width - 75, 30, "85%", COLOR_BLACK, screen_width, screen_height);
    
    // App grid
    int apps_per_row = 3;
    int icon_size = 120;
    int icon_margin = 40;
    int start_y = 150;
    
    for (int i = 0; i < ui.num_apps && i < 6; i++) {
        int row = i / apps_per_row;
        int col = i % apps_per_row;
        
        int x = col * (icon_size + icon_margin) + icon_margin + 
               (screen_width - (apps_per_row * (icon_size + icon_margin) - icon_margin)) / 2;
        int y = start_y + row * (icon_size + 100);
        
        draw_rounded_rect(fb.backbuffer, x, y, icon_size, icon_size, 20, ui.apps[i].color, screen_width, screen_height);
        
        int text_x = x + (icon_size - strlen(ui.apps[i].name) * 10) / 2;
        draw_text(fb.backbuffer, text_x, y + icon_size + 20, ui.apps[i].name, COLOR_TEXT, screen_width, screen_height);
    }
    
    // Dock
    int dock_y = screen_height - 200;
    draw_rounded_rect(fb.backbuffer, 40, dock_y, screen_width - 80, 120, 30, COLOR_SURFACE, screen_width, screen_height);
    
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
    
    // Display
    draw_rounded_rect(fb.backbuffer, 40, 100, screen_width - 80, 120, 15, COLOR_SURFACE, screen_width, screen_height);
    
    int text_width = strlen(ui.calc_display) * 10;
    int text_x = screen_width - 60 - text_width;
    draw_text(fb.backbuffer, text_x, 140, ui.calc_display, COLOR_TEXT, screen_width, screen_height);
    
    // Buttons
    const char* buttons[] = {
        "C", "+/-", "%", "/",
        "7", "8", "9", "*",
        "4", "5", "6", "-",
        "1", "2", "3", "+",
        "0", ".", "="
    };
    
    int button_size = (screen_width - 100) / 4;
    int start_y = 280;
    
    for (int i = 0; i < 19; i++) {
        int row = i / 4;
        int col = i % 4;
        
        if (i == 16) {
            col = 0;
            row = 4;
            draw_rounded_rect(fb.backbuffer, 40 + col * button_size, start_y + row * (button_size + 10), 
                            button_size * 2 - 5, button_size, 15, COLOR_BORDER, screen_width, screen_height);
        } else if (i >= 17) {
            col = (i == 17) ? 2 : 3;
            row = 4;
            draw_rounded_rect(fb.backbuffer, 40 + col * button_size, start_y + row * (button_size + 10), 
                            button_size - 10, button_size, 15, COLOR_BORDER, screen_width, screen_height);
        } else {
            draw_rounded_rect(fb.backbuffer, 40 + col * button_size, start_y + row * (button_size + 10), 
                            button_size - 10, button_size, 15, COLOR_BORDER, screen_width, screen_height);
        }
        
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
    
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    
    char time_str[32];
    strftime(time_str, 32, "%H:%M:%S", tm_info);
    
    int text_width = strlen(time_str) * 10;
    int text_x = (screen_width - text_width) / 2;
    draw_text(fb.backbuffer, text_x, screen_height / 2 - 50, time_str, COLOR_TEXT, screen_width, screen_height);
    
    char date_str[64];
    strftime(date_str, 64, "%A %B %d %Y", tm_info);
    
    text_width = strlen(date_str) * 10;
    text_x = (screen_width - text_width) / 2;
    draw_text(fb.backbuffer, text_x, screen_height / 2 + 50, date_str, COLOR_TEXT_LIGHT, screen_width, screen_height);
    
    // Back button
    draw_rounded_rect(fb.backbuffer, 40, 40, 100, 50, 10, COLOR_PRIMARY, screen_width, screen_height);
    draw_text(fb.backbuffer, 65, 55, "Back", COLOR_WHITE, screen_width, screen_height);
}

void draw_settings_screen() {
    int screen_width = fb.vinfo.xres;
    int screen_height = fb.vinfo.yres;
    
    draw_text(fb.backbuffer, 40, 100, "Settings", COLOR_TEXT, screen_width, screen_height);
    
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
        
        draw_rounded_rect(fb.backbuffer, 40, y, screen_width - 80, 60, 10, COLOR_SURFACE, screen_width, screen_height);
        draw_text(fb.backbuffer, 60, y + 22, settings[i], COLOR_TEXT, screen_width, screen_height);
        
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
    
    switch (ui.current_screen) {
        case SCREEN_HOME:
            if (gesture == GESTURE_TAP) {
                printf("Tap detected at %d, %d\n", current.x, current.y);
                
                // Check app icons
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
    
    clear_screen(fb.backbuffer, COLOR_BACKGROUND, screen_width, screen_height);
    
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
    
    // Touch/cursor indicator
    TouchPoint current_touch = get_current_touch();
    
    if (touch_system.touch_available) {
        // Real touch device - show when pressed
        if (touch_is_pressed()) {
            draw_circle(fb.backbuffer, current_touch.x, current_touch.y, 30, COLOR_PRIMARY, screen_width, screen_height);
            draw_circle(fb.backbuffer, current_touch.x, current_touch.y, 15, COLOR_WHITE, screen_width, screen_height);
            
            char coord_text[32];
            snprintf(coord_text, sizeof(coord_text), "TOUCH %d,%d", current_touch.x, current_touch.y);
            draw_text(fb.backbuffer, current_touch.x - 50, current_touch.y - 50, coord_text, COLOR_WHITE, screen_width, screen_height);
        }
        
        // Show touch device info
        draw_text(fb.backbuffer, 10, 10, "TOUCH: ACTIVE", COLOR_SUCCESS, screen_width, screen_height);
    } else {
        // Keyboard fallback mode
        if (current_touch.pressed) {
            // Pressed state - filled circle with tap indicator
            draw_circle(fb.backbuffer, current_touch.x, current_touch.y, 25, COLOR_PRIMARY, screen_width, screen_height);
            draw_circle(fb.backbuffer, current_touch.x, current_touch.y, 15, COLOR_WHITE, screen_width, screen_height);
            
            // Show "TAP" text
            draw_text(fb.backbuffer, current_touch.x - 20, current_touch.y - 40, "TAP", COLOR_WHITE, screen_width, screen_height);
        } else {
            // Cursor state - outline only with crosshair
            for (int r = 20; r <= 25; r++) {
                draw_circle(fb.backbuffer, current_touch.x, current_touch.y, r, COLOR_PRIMARY, screen_width, screen_height);
            }
            draw_circle(fb.backbuffer, current_touch.x, current_touch.y, 5, COLOR_WHITE, screen_width, screen_height);
            
            // Draw crosshair
            draw_rect(fb.backbuffer, current_touch.x - 10, current_touch.y - 1, 20, 2, COLOR_WHITE, screen_width, screen_height);
            draw_rect(fb.backbuffer, current_touch.x - 1, current_touch.y - 10, 2, 20, COLOR_WHITE, screen_width, screen_height);
        }
        
        // Status information
        draw_text(fb.backbuffer, 10, 10, "TOUCH: KEYBOARD MODE", COLOR_WARNING, screen_width, screen_height);
        
        char coord_text[64];
        snprintf(coord_text, sizeof(coord_text), "Cursor: %d,%d", current_touch.x, current_touch.y);
        draw_text(fb.backbuffer, 10, screen_height - 70, coord_text, COLOR_TEXT_LIGHT, screen_width, screen_height);
        
        // Show controls
        draw_text(fb.backbuffer, 10, screen_height - 50, "WASD=move SPACE=tap T=test C=center Q=quit", COLOR_TEXT_LIGHT, screen_width, screen_height);
        
        // Show current screen info
        const char* screen_names[] = {"HOME", "CALC", "CLOCK", "SETTINGS", "WEATHER", "GALLERY", "MUSIC", "CONTACTS"};
        char screen_text[64];
        snprintf(screen_text, sizeof(screen_text), "Screen: %s", screen_names[ui.current_screen]);
        draw_text(fb.backbuffer, 10, screen_height - 30, screen_text, COLOR_TEXT_LIGHT, screen_width, screen_height);
    }
}

// Framebuffer
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

    printf("Current screen: %dx%d, %d bpp\n", 
           fb.vinfo.xres, fb.vinfo.yres, fb.vinfo.bits_per_pixel);
    
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

    printf("Framebuffer initialized: %dx%d\n", actual_width, actual_height);
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

// Main
int main() {
    printf("=== Orange Pi 5 Touch UI - Ultra Debug Mode ===\n");
    printf("Timestamp: %ld\n", time(NULL));
    printf("Process ID: %d\n", getpid());
    printf("User ID: %d\n", getuid());
    printf("Group ID: %d\n", getgid());
    fflush(stdout);
    
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    setup_terminal();
    
    if (init_framebuffer() < 0) {
        restore_terminal();
        return 1;
    }
    
    int screen_w = fb.vinfo.xres;
    int screen_h = fb.vinfo.yres;
    
    printf("Screen dimensions: %dx%d\n", screen_w, screen_h);
    
    // Try to initialize touch system
    int touch_result = init_touch_system(screen_w, screen_h);
    
    if (touch_result < 0) {
        printf("\n=== TOUCH INITIALIZATION FAILED ===\n");
        printf("Reasons this might happen:\n");
        printf("1. No touch hardware connected\n");
        printf("2. Permission issues (try running as root)\n");
        printf("3. Touch device not recognized\n");
        printf("4. Driver issues\n");
        printf("\nFalling back to keyboard control...\n");
        printf("Controls: WASD=move, SPACE=tap, T=test, Q=quit\n");
        
        // Initialize fallback system
        touch_system.touch_available = 0;
        touch_system.screen_width = screen_w;
        touch_system.screen_height = screen_h;
        touch_system.current_touch.x = screen_w / 2;
        touch_system.current_touch.y = screen_h / 2;
        touch_system.current_touch.pressed = 0;
    } else {
        printf("\n=== TOUCH SYSTEM READY ===\n");
        printf("Running touch test in 3 seconds...\n");
        sleep(3);
        test_touch_system();
    }
    
    init_ui();
    
    printf("\n=== STARTING MAIN LOOP ===\n");
    printf("Watch this output for touch events...\n");
    
    uint64_t frame_count = 0;
    uint64_t last_status_time = get_time_ns();
    
    while (running) {
        uint64_t frame_start = get_time_ns();
        
        // Process input with enhanced debugging
        process_all_input();
        
        // Handle UI input 
        handle_touch_input();
        
        // Render UI
        render_ui();
        
        // Display
        swap_buffers();
        
        frame_count++;
        
        // Print status every 5 seconds
        if (frame_start - last_status_time > 5000000000ULL) {
            printf("=== STATUS: Frame %lu, Touch available: %s ===\n", 
                   frame_count, touch_system.touch_available ? "YES" : "NO");
            
            if (touch_system.touch_available) {
                printf("Touch state: pos(%d,%d) pressed=%d\n",
                       touch_system.current_touch.x,
                       touch_system.current_touch.y,
                       touch_system.current_touch.pressed);
            }
            
            last_status_time = frame_start;
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
    }
    
    cleanup_touch_system();
    cleanup_framebuffer();
    printf("Phone UI ended.\n");
    return 0;
}