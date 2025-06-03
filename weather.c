#include "apps.h"
#include <stdio.h>
#include <string.h>

void draw_weather_app(uint32_t *buf) {
    // Weather app interface
    draw_text_centered(buf, "Los Altos, CA", MEDIUM_TEXT, STATUS_HEIGHT + 150, COLOR_WHITE);
    
    // Current temperature
    draw_text_centered(buf, "22°C", LARGE_TEXT * 2, STATUS_HEIGHT + 220, COLOR_WHITE);
    
    // Weather condition
    draw_text_centered(buf, "Partly Cloudy", MEDIUM_TEXT, STATUS_HEIGHT + 320, COLOR_LIGHT_GRAY);
    
    // Weather icon area (simple representation)
    int icon_x = screen_w / 2;
    int icon_y = STATUS_HEIGHT + 380;
    
    // Sun (yellow circle)
    draw_circle_filled(buf, icon_x - 30, icon_y, 25, COLOR_ORANGE);
    
    // Cloud (gray rounded rectangles)
    draw_circle_filled(buf, icon_x + 20, icon_y - 10, 30, COLOR_LIGHT_GRAY);
    draw_circle_filled(buf, icon_x + 40, icon_y, 25, COLOR_LIGHT_GRAY);
    draw_circle_filled(buf, icon_x, icon_y + 5, 35, COLOR_LIGHT_GRAY);
    
    // Today's high/low
    draw_text_centered(buf, "H: 24°  L: 18°", MEDIUM_TEXT, STATUS_HEIGHT + 450, COLOR_WHITE);
    
    // 5-day forecast
    int forecast_y = STATUS_HEIGHT + 520;
    const char* days[] = {"Today", "Tue", "Wed", "Thu", "Fri"};
    const char* highs[] = {"24°", "26°", "23°", "21°", "25°"};
    const char* lows[] = {"18°", "19°", "16°", "15°", "20°"};
    
    for (int i = 0; i < 5; i++) {
        int x = 60 + i * (screen_w - 120) / 4;
        int y = forecast_y;
        
        // Day
        draw_text(buf, days[i], SMALL_TEXT, x, y, COLOR_WHITE);
        
        // Weather icon (small)
        if (i % 2 == 0) {
            draw_circle_filled(buf, x + 20, y + 40, 8, COLOR_ORANGE); // Sun
        } else {
            draw_circle_filled(buf, x + 20, y + 40, 10, COLOR_LIGHT_GRAY); // Cloud
        }
        
        // High temp
        draw_text(buf, highs[i], SMALL_TEXT, x, y + 70, COLOR_WHITE);
        
        // Low temp
        draw_text(buf, lows[i], SMALL_TEXT, x, y + 100, COLOR_LIGHT_GRAY);
    }
}