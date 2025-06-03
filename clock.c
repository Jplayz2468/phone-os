#include "apps.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <math.h>

void draw_clock_app(uint32_t *buf) {
    // Get current time
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    
    // Digital time display
    char time_str[32];
    strftime(time_str, sizeof(time_str), "%H:%M:%S", tm);
    draw_text_centered(buf, time_str, LARGE_TEXT, STATUS_HEIGHT + 150, COLOR_WHITE);
    
    // Date display
    char date_str[64];
    strftime(date_str, sizeof(date_str), "%A, %B %d", tm);
    draw_text_centered(buf, date_str, MEDIUM_TEXT, STATUS_HEIGHT + 220, COLOR_LIGHT_GRAY);
    
    // Analog clock
    int clock_center_x = screen_w / 2;
    int clock_center_y = STATUS_HEIGHT + 400;
    int clock_radius = 120;
    
    // Clock face
    draw_circle_filled(buf, clock_center_x, clock_center_y, clock_radius, COLOR_WHITE);
    draw_circle_filled(buf, clock_center_x, clock_center_y, clock_radius - 8, COLOR_BG);
    
    // Hour markers
    for (int i = 0; i < 12; i++) {
        float angle = (i * 30 - 90) * M_PI / 180.0f;
        int x1 = clock_center_x + (int)((clock_radius - 20) * cos(angle));
        int y1 = clock_center_y + (int)((clock_radius - 20) * sin(angle));
        int x2 = clock_center_x + (int)((clock_radius - 5) * cos(angle));
        int y2 = clock_center_y + (int)((clock_radius - 5) * sin(angle));
        
        // Draw thick line for hour marker
        for (int t = -2; t <= 2; t++) {
            draw_rect(buf, x1 + t, y1, x2 - x1, y2 - y1, COLOR_WHITE);
        }
    }
    
    // Clock hands
    float hour_angle = ((tm->tm_hour % 12) * 30 + tm->tm_min * 0.5f - 90) * M_PI / 180.0f;
    float minute_angle = (tm->tm_min * 6 - 90) * M_PI / 180.0f;
    float second_angle = (tm->tm_sec * 6 - 90) * M_PI / 180.0f;
    
    // Hour hand (thick, short)
    int hour_x = clock_center_x + (int)(60 * cos(hour_angle));
    int hour_y = clock_center_y + (int)(60 * sin(hour_angle));
    for (int t = -3; t <= 3; t++) {
        for (int s = -1; s <= 1; s++) {
            draw_rect(buf, clock_center_x + t, clock_center_y + s, 
                     hour_x - clock_center_x, hour_y - clock_center_y, COLOR_WHITE);
        }
    }
    
    // Minute hand (medium, long)
    int minute_x = clock_center_x + (int)(90 * cos(minute_angle));
    int minute_y = clock_center_y + (int)(90 * sin(minute_angle));
    for (int t = -2; t <= 2; t++) {
        draw_rect(buf, clock_center_x + t, clock_center_y, 
                 minute_x - clock_center_x, minute_y - clock_center_y, COLOR_WHITE);
    }
    
    // Second hand (thin, long)
    int second_x = clock_center_x + (int)(100 * cos(second_angle));
    int second_y = clock_center_y + (int)(100 * sin(second_angle));
    draw_rect(buf, clock_center_x, clock_center_y, 
             second_x - clock_center_x, second_y - clock_center_y, COLOR_RED);
    
    // Center dot
    draw_circle_filled(buf, clock_center_x, clock_center_y, 8, COLOR_WHITE);
    
    // Bottom tabs
    int tabs_y = screen_h - 200;
    draw_text(buf, "World Clock", SMALL_TEXT, 60, tabs_y, COLOR_WHITE);
    draw_text(buf, "Alarm", SMALL_TEXT, screen_w/2 - 30, tabs_y, COLOR_LIGHT_GRAY);
    draw_text(buf, "Timer", SMALL_TEXT, screen_w - 120, tabs_y, COLOR_LIGHT_GRAY);
}