#include "apps.h"
#include <stdio.h>
#include <string.h>

void draw_phone_app(uint32_t *buf) {
    // Draw phone interface
    draw_text_centered(buf, "Recent Calls", MEDIUM_TEXT, STATUS_HEIGHT + 150, COLOR_WHITE);
    
    // Call history entries
    const char* calls[] = {
        "Mom - 2:30 PM",
        "Dad - 11:45 AM", 
        "Work - 9:15 AM",
        "Sarah - Yesterday"
    };
    
    int start_y = STATUS_HEIGHT + 220;
    for (int i = 0; i < 4; i++) {
        int y = start_y + i * 80;
        
        // Call entry background
        draw_rounded_rect(buf, 60, y, screen_w - 120, 60, 15, COLOR_GRAY);
        
        // Green call icon
        draw_circle_filled(buf, 100, y + 30, 15, COLOR_GREEN);
        
        // Call text
        draw_text(buf, calls[i], SMALL_TEXT, 140, y + 20, COLOR_WHITE);
    }
    
    // Dialpad button
    int dialpad_y = screen_h - 300;
    draw_rounded_rect(buf, screen_w/2 - 100, dialpad_y, 200, 80, 20, COLOR_GREEN);
    draw_text_centered(buf, "Dialpad", MEDIUM_TEXT, dialpad_y + 25, COLOR_WHITE);
}