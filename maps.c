#include "apps.h"
#include <stdio.h>
#include <string.h>

void draw_maps_app(uint32_t *buf) {
    // Maps interface with simulated map view
    int map_margin = 20;
    int map_x = map_margin;
    int map_y = STATUS_HEIGHT + 100;
    int map_w = screen_w - 2 * map_margin;
    int map_h = screen_h - STATUS_HEIGHT - 250;
    
    // Map background (light gray to simulate map)
    draw_rect(buf, map_x, map_y, map_w, map_h, COLOR_LIGHT_GRAY);
    
    // Simulate roads/streets with darker lines
    for (int i = 0; i < map_w; i += 80) {
        draw_rect(buf, map_x + i, map_y, 4, map_h, COLOR_GRAY);
    }
    for (int i = 0; i < map_h; i += 60) {
        draw_rect(buf, map_x, map_y + i, map_w, 4, COLOR_GRAY);
    }
    
    // Current location pin (blue dot)
    int pin_x = map_x + map_w / 2;
    int pin_y = map_y + map_h / 2;
    draw_circle_filled(buf, pin_x, pin_y, 12, COLOR_BLUE);
    draw_circle_filled(buf, pin_x, pin_y, 6, COLOR_WHITE);
    
    // Search bar at top
    int search_y = STATUS_HEIGHT + 150;
    draw_rounded_rect(buf, 40, search_y, screen_w - 80, 60, 20, COLOR_WHITE);
    draw_text(buf, "Search for a place", SMALL_TEXT, 60, search_y + 20, COLOR_GRAY);
    
    // Location name overlay
    int overlay_y = map_y + map_h - 100;
    draw_rounded_rect(buf, map_x + 20, overlay_y, map_w - 40, 80, 15, COLOR_WHITE);
    draw_text(buf, "Los Altos, CA", MEDIUM_TEXT, map_x + 40, overlay_y + 15, COLOR_BG);
    draw_text(buf, "Current Location", SMALL_TEXT, map_x + 40, overlay_y + 45, COLOR_GRAY);
    
    // Bottom controls
    int controls_y = screen_h - 200;
    
    // Directions button
    draw_rounded_rect(buf, 60, controls_y, 120, 60, 15, COLOR_BLUE);
    draw_text(buf, "Directions", SMALL_TEXT, 75, controls_y + 20, COLOR_WHITE);
    
    // Current location button
    draw_circle_filled(buf, screen_w - 80, controls_y + 30, 25, COLOR_WHITE);
    draw_circle_filled(buf, screen_w - 80, controls_y + 30, 15, COLOR_BLUE);
    
    // Zoom controls
    draw_circle_filled(buf, screen_w - 80, map_y + 60, 25, COLOR_WHITE);
    draw_text(buf, "+", MEDIUM_TEXT, screen_w - 90, map_y + 40, COLOR_BG);
    
    draw_circle_filled(buf, screen_w - 80, map_y + 130, 25, COLOR_WHITE);
    draw_text(buf, "-", MEDIUM_TEXT, screen_w - 88, map_y + 110, COLOR_BG);
}