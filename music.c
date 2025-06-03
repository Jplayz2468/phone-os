#include "apps.h"
#include <stdio.h>
#include <string.h>

void draw_music_app(uint32_t *buf) {
    // Music player interface
    
    // Album art area
    int art_size = 280;
    int art_x = (screen_w - art_size) / 2;
    int art_y = STATUS_HEIGHT + 180;
    
    draw_rounded_rect(buf, art_x, art_y, art_size, art_size, 20, COLOR_PURPLE);
    
    // Music note icon on album art
    draw_text_centered(buf, "♪", LARGE_TEXT * 2, art_y + art_size/2 - 50, COLOR_WHITE);
    
    // Song info
    int info_y = art_y + art_size + 40;
    draw_text_centered(buf, "Your Song", LARGE_TEXT, info_y, COLOR_WHITE);
    draw_text_centered(buf, "Artist Name", MEDIUM_TEXT, info_y + 60, COLOR_LIGHT_GRAY);
    draw_text_centered(buf, "Album Title", SMALL_TEXT, info_y + 100, COLOR_LIGHT_GRAY);
    
    // Progress bar
    int progress_y = info_y + 150;
    int progress_w = screen_w - 120;
    int progress_x = 60;
    
    draw_rounded_rect(buf, progress_x, progress_y, progress_w, 8, 4, COLOR_GRAY);
    draw_rounded_rect(buf, progress_x, progress_y, progress_w / 3, 8, 4, COLOR_WHITE);
    draw_circle_filled(buf, progress_x + progress_w / 3, progress_y + 4, 8, COLOR_WHITE);
    
    // Time labels
    draw_text(buf, "1:23", SMALL_TEXT, progress_x, progress_y + 20, COLOR_LIGHT_GRAY);
    char duration[] = "3:45";
    int duration_w = measure_text_width(duration, SMALL_TEXT);
    draw_text(buf, duration, SMALL_TEXT, progress_x + progress_w - duration_w, progress_y + 20, COLOR_LIGHT_GRAY);
    
    // Control buttons
    int controls_y = progress_y + 80;
    int center_x = screen_w / 2;
    
    // Previous button
    draw_text(buf, "⏮", LARGE_TEXT, center_x - 120, controls_y, COLOR_WHITE);
    
    // Play/pause button (large)
    draw_circle_filled(buf, center_x, controls_y + 35, 35, COLOR_WHITE);
    draw_text(buf, "⏸", LARGE_TEXT, center_x - 20, controls_y + 10, COLOR_BG);
    
    // Next button
    draw_text(buf, "⏭", LARGE_TEXT, center_x + 100, controls_y, COLOR_WHITE);
    
    // Volume and additional controls
    int bottom_controls_y = controls_y + 100;
    draw_text(buf, "🔀", MEDIUM_TEXT, 80, bottom_controls_y, COLOR_LIGHT_GRAY);
    draw_text(buf, "🔁", MEDIUM_TEXT, screen_w - 120, bottom_controls_y, COLOR_LIGHT_GRAY);
}