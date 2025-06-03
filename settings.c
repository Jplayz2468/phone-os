#include "apps.h"
#include <stdio.h>
#include <string.h>

void draw_settings_app(uint32_t *buf) {
    // Draw settings interface
    draw_text_centered(buf, "Settings", MEDIUM_TEXT, STATUS_HEIGHT + 150, COLOR_WHITE);
    
    // Settings menu items
    const char* settings[] = {
        "Wi-Fi",
        "Bluetooth", 
        "Display & Brightness",
        "Sounds & Haptics",
        "Privacy & Security",
        "Battery",
        "General",
        "About"
    };
    
    const char* values[] = {
        "MyNetwork",
        "On",
        "Auto",
        "",
        "",
        "87%",
        "",
        ""
    };
    
    int start_y = STATUS_HEIGHT + 220;
    for (int i = 0; i < 8; i++) {
        int y = start_y + i * 70;
        
        // Setting item background
        if (i < 7) {
            draw_rect(buf, 40, y + 60, screen_w - 80, 2, COLOR_GRAY);
        }
        
        // Setting icon (colored circle)
        uint32_t icon_colors[] = {COLOR_BLUE, COLOR_BLUE, COLOR_GRAY, COLOR_RED, 
                                 COLOR_BLUE, COLOR_GREEN, COLOR_GRAY, COLOR_GRAY};
        draw_circle_filled(buf, 80, y + 25, 18, icon_colors[i]);
        
        // Setting name
        draw_text(buf, settings[i], SMALL_TEXT, 120, y + 10, COLOR_WHITE);
        
        // Setting value (if any)
        if (strlen(values[i]) > 0) {
            int value_width = measure_text_width(values[i], SMALL_TEXT);
            draw_text(buf, values[i], SMALL_TEXT, screen_w - 120 - value_width, y + 10, COLOR_LIGHT_GRAY);
        }
        
        // Arrow indicator
        draw_text(buf, ">", SMALL_TEXT, screen_w - 80, y + 10, COLOR_LIGHT_GRAY);
    }
}