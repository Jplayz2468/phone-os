#include "apps.h"
#include <stdio.h>
#include <string.h>

void draw_photos_app(uint32_t *buf) {
    // Draw photos gallery interface
    draw_text_centered(buf, "Photos", MEDIUM_TEXT, STATUS_HEIGHT + 150, COLOR_WHITE);
    
    // Photo grid (3x3)
    int photos_per_row = 3;
    int photo_size = 180;
    int photo_margin = 20;
    int grid_width = photos_per_row * photo_size + (photos_per_row - 1) * photo_margin;
    int start_x = (screen_w - grid_width) / 2;
    int start_y = STATUS_HEIGHT + 220;
    
    // Different colored "photos" to simulate gallery
    uint32_t photo_colors[] = {
        COLOR_BLUE, COLOR_GREEN, COLOR_ORANGE,
        COLOR_PURPLE, COLOR_RED, COLOR_GRAY,
        COLOR_LIGHT_GRAY, COLOR_BLUE, COLOR_GREEN
    };
    
    for (int i = 0; i < 9; i++) {
        int row = i / photos_per_row;
        int col = i % photos_per_row;
        int x = start_x + col * (photo_size + photo_margin);
        int y = start_y + row * (photo_size + photo_margin);
        
        // Photo thumbnail
        draw_rounded_rect(buf, x, y, photo_size, photo_size, 15, photo_colors[i % 9]);
        
        // Add a small white corner to make it look like a photo
        draw_rect(buf, x + photo_size - 40, y + photo_size - 40, 35, 35, COLOR_WHITE);
    }
    
    // Bottom toolbar
    int toolbar_y = screen_h - 200;
    draw_rect(buf, 0, toolbar_y, screen_w, 100, COLOR_GRAY);
    
    // Toolbar buttons
    draw_text(buf, "Library", SMALL_TEXT, 60, toolbar_y + 30, COLOR_WHITE);
    draw_text(buf, "Albums", SMALL_TEXT, screen_w/2 - 40, toolbar_y + 30, COLOR_WHITE);
    draw_text(buf, "Search", SMALL_TEXT, screen_w - 120, toolbar_y + 30, COLOR_WHITE);
}