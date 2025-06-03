#include "apps.h"
#include <stdio.h>
#include <string.h>

void draw_messages_app(uint32_t *buf) {
    // Draw messages interface
    draw_text_centered(buf, "Messages", MEDIUM_TEXT, STATUS_HEIGHT + 150, COLOR_WHITE);
    
    // Message threads
    const char* contacts[] = {"Mom", "Dad", "Sarah", "Work Group"};
    const char* previews[] = {
        "Can you pick up milk?",
        "Happy birthday!",
        "See you at 8pm",
        "Meeting moved to 3pm"
    };
    
    int start_y = STATUS_HEIGHT + 220;
    for (int i = 0; i < 4; i++) {
        int y = start_y + i * 100;
        
        // Message thread background
        draw_rounded_rect(buf, 40, y, screen_w - 80, 80, 15, COLOR_GRAY);
        
        // Contact avatar (circle)
        draw_circle_filled(buf, 80, y + 40, 20, COLOR_BLUE);
        
        // Contact name
        draw_text(buf, contacts[i], SMALL_TEXT, 120, y + 15, COLOR_WHITE);
        
        // Message preview
        draw_text(buf, previews[i], SMALL_TEXT - 8, 120, y + 45, COLOR_LIGHT_GRAY);
    }
    
    // Compose button
    int compose_y = screen_h - 200;
    draw_circle_filled(buf, screen_w - 80, compose_y, 30, COLOR_GREEN);
    draw_text(buf, "+", LARGE_TEXT, screen_w - 95, compose_y - 35, COLOR_WHITE);
}