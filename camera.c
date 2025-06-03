#include "apps.h"
#include <stdio.h>
#include <string.h>

void draw_camera_app(uint32_t *buf) {
    // Draw camera interface with viewfinder
    int viewfinder_margin = 60;
    int viewfinder_x = viewfinder_margin;
    int viewfinder_y = STATUS_HEIGHT + 100;
    int viewfinder_w = screen_w - 2 * viewfinder_margin;
    int viewfinder_h = screen_h - STATUS_HEIGHT - 300;
    
    // Viewfinder background (dark gray to simulate camera view)
    draw_rounded_rect(buf, viewfinder_x, viewfinder_y, viewfinder_w, viewfinder_h, 20, COLOR_GRAY);
    
    // Camera grid lines
    int grid_x1 = viewfinder_x + viewfinder_w / 3;
    int grid_x2 = viewfinder_x + 2 * viewfinder_w / 3;
    int grid_y1 = viewfinder_y + viewfinder_h / 3;
    int grid_y2 = viewfinder_y + 2 * viewfinder_h / 3;
    
    // Vertical grid lines
    for (int y = viewfinder_y; y < viewfinder_y + viewfinder_h; y += 4) {
        if (y % 8 == 0) {
            buf[y * screen_w + grid_x1] = COLOR_LIGHT_GRAY;
            buf[y * screen_w + grid_x2] = COLOR_LIGHT_GRAY;
        }
    }
    
    // Horizontal grid lines
    for (int x = viewfinder_x; x < viewfinder_x + viewfinder_w; x += 4) {
        if (x % 8 == 0) {
            buf[grid_y1 * screen_w + x] = COLOR_LIGHT_GRAY;
            buf[grid_y2 * screen_w + x] = COLOR_LIGHT_GRAY;
        }
    }
    
    // Camera controls at bottom
    int controls_y = viewfinder_y + viewfinder_h + 40;
    
    // Photo/Video toggle
    draw_text(buf, "PHOTO", SMALL_TEXT, 80, controls_y, COLOR_WHITE);
    draw_text(buf, "VIDEO", SMALL_TEXT, screen_w - 160, controls_y, COLOR_LIGHT_GRAY);
    
    // Shutter button (large white circle)
    int shutter_x = screen_w / 2;
    int shutter_y = controls_y + 60;
    draw_circle_filled(buf, shutter_x, shutter_y, 40, COLOR_WHITE);
    draw_circle_filled(buf, shutter_x, shutter_y, 35, COLOR_BG);
    draw_circle_filled(buf, shutter_x, shutter_y, 30, COLOR_WHITE);
    
    // Gallery thumbnail (small square)
    int gallery_x = 100;
    draw_rounded_rect(buf, gallery_x - 25, shutter_y - 25, 50, 50, 10, COLOR_BLUE);
    
    // Flash toggle
    int flash_x = screen_w - 100;
    draw_text(buf, "⚡", MEDIUM_TEXT, flash_x - 15, shutter_y - 20, COLOR_WHITE);
}