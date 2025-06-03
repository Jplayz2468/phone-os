#include "apps.h"
#include <stdio.h>
#include <string.h>

void draw_safari_app(uint32_t *buf) {
    // Safari browser interface
    
    // Address bar
    int addr_bar_y = STATUS_HEIGHT + 160;
    draw_rounded_rect(buf, 40, addr_bar_y, screen_w - 80, 60, 20, COLOR_GRAY);
    draw_text(buf, "🔒 google.com", SMALL_TEXT, 60, addr_bar_y + 20, COLOR_WHITE);
    
    // Reload button
    draw_text(buf, "↻", MEDIUM_TEXT, screen_w - 100, addr_bar_y + 15, COLOR_WHITE);
    
    // Web page content area
    int content_y = addr_bar_y + 80;
    int content_h = screen_h - content_y - 200;
    
    // Web page background
    draw_rect(buf, 20, content_y, screen_w - 40, content_h, COLOR_WHITE);
    
    // Simulate web page content
    // Header
    draw_rect(buf, 20, content_y, screen_w - 40, 80, COLOR_BLUE);
    draw_text(buf, "Google", LARGE_TEXT, 60, content_y + 25, COLOR_WHITE);
    
    // Search box
    int search_y = content_y + 120;
    draw_rounded_rect(buf, 60, search_y, screen_w - 120, 50, 25, COLOR_LIGHT_GRAY);
    draw_text(buf, "Search or type a URL", SMALL_TEXT, 80, search_y + 15, COLOR_GRAY);
    
    // Navigation buttons
    int nav_y = search_y + 80;
    draw_rounded_rect(buf, 80, nav_y, 120, 40, 20, COLOR_LIGHT_GRAY);
    draw_text(buf, "Google Search", SMALL_TEXT - 4, 90, nav_y + 15, COLOR_BG);
    
    draw_rounded_rect(buf, 220, nav_y, 120, 40, 20, COLOR_LIGHT_GRAY);
    draw_text(buf, "I'm Feeling Lucky", SMALL_TEXT - 6, 225, nav_y + 15, COLOR_BG);
    
    // Some placeholder links
    int links_y = nav_y + 80;
    draw_text(buf, "About Google", SMALL_TEXT, 60, links_y, COLOR_BLUE);
    draw_text(buf, "Privacy", SMALL_TEXT, 180, links_y, COLOR_BLUE);
    draw_text(buf, "Terms", SMALL_TEXT, 260, links_y, COLOR_BLUE);
    
    // Bottom navigation bar
    int nav_bar_y = screen_h - 160;
    draw_rect(buf, 0, nav_bar_y, screen_w, 100, COLOR_GRAY);
    
    // Navigation buttons
    draw_text(buf, "◀", LARGE_TEXT, 60, nav_bar_y + 25, COLOR_WHITE);
    draw_text(buf, "▶", LARGE_TEXT, 120, nav_bar_y + 25, COLOR_LIGHT_GRAY);
    draw_text(buf, "⤴", LARGE_TEXT, 180, nav_bar_y + 25, COLOR_WHITE);
    draw_text(buf, "📖", LARGE_TEXT, screen_w/2 - 20, nav_bar_y + 25, COLOR_WHITE);
    draw_text(buf, "⋯", LARGE_TEXT, screen_w - 80, nav_bar_y + 25, COLOR_WHITE);
    
    // Tab indicator
    draw_rect(buf, screen_w - 140, nav_bar_y + 10, 40, 30, COLOR_WHITE);
    draw_text(buf, "1", SMALL_TEXT, screen_w - 125, nav_bar_y + 20, COLOR_BG);
}