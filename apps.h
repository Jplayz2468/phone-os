#ifndef APPS_H
#define APPS_H

#include <stdint.h>

// Forward declaration only - no implementation
struct stbtt_fontinfo;

// Forward declarations for graphics functions
extern int screen_w, screen_h;
extern struct stbtt_fontinfo font;

// Graphics function prototypes that apps can use
void clear_screen(uint32_t *buf, uint32_t color);
void draw_rect(uint32_t *buf, int x, int y, int w, int h, uint32_t color);
void draw_circle_filled(uint32_t *buf, int cx, int cy, int radius, uint32_t color);
void draw_rounded_rect(uint32_t *buf, int x, int y, int w, int h, int radius, uint32_t color);
int measure_text_width(const char *text, int font_size);
void draw_text(uint32_t *buf, const char *text, int font_size, int x, int y, uint32_t color);
void draw_text_centered(uint32_t *buf, const char *text, int font_size, int y, uint32_t color);

// Color definitions
#define COLOR_BG 0xFF000000
#define COLOR_WHITE 0xFFFFFFFF
#define COLOR_GRAY 0xFF666666
#define COLOR_LIGHT_GRAY 0xFFAAAAAA
#define COLOR_BLUE 0xFF007AFF
#define COLOR_GREEN 0xFF34C759
#define COLOR_RED 0xFFFF3B30
#define COLOR_ORANGE 0xFFFF9500
#define COLOR_PURPLE 0xFFAF52DE

// UI scaling constants
#define STATUS_HEIGHT 140
#define LARGE_TEXT 96
#define MEDIUM_TEXT 64
#define SMALL_TEXT 48
#define ICON_SIZE 200
#define MARGIN 60

// App drawing function prototypes
void draw_phone_app(uint32_t *buf);
void draw_messages_app(uint32_t *buf);
void draw_camera_app(uint32_t *buf);
void draw_photos_app(uint32_t *buf);
void draw_settings_app(uint32_t *buf);
void draw_calculator_app(uint32_t *buf);
void draw_clock_app(uint32_t *buf);
void draw_weather_app(uint32_t *buf);
void draw_maps_app(uint32_t *buf);
void draw_music_app(uint32_t *buf);
void draw_mail_app(uint32_t *buf);
void draw_safari_app(uint32_t *buf);

// App drawing function pointer array
typedef void (*AppDrawFunction)(uint32_t *buf);
extern AppDrawFunction app_draw_functions[12];

#endif // APPS_H