#include "apps.h"
#include <stdio.h>
#include <string.h>

void draw_calculator_app(uint32_t *buf) {
    // Calculator display
    draw_text_centered(buf, "0", LARGE_TEXT * 2, STATUS_HEIGHT + 200, COLOR_WHITE);
    
    // Calculator buttons
    char calc_btns[] = "789+456-123*C0=/";
    int calc_start_x = screen_w/2 - 320;
    int calc_start_y = STATUS_HEIGHT + 350;
    
    for (int i = 0; i < 16; i++) {
        int row = i / 4;
        int col = i % 4;
        int x = calc_start_x + col * 160;
        int y = calc_start_y + row * 120;
        
        uint32_t btn_color = COLOR_GRAY;
        
        // Operator buttons get orange color
        if (col == 3 && row < 3) btn_color = COLOR_ORANGE;
        
        // Equals button gets orange color
        if (calc_btns[i] == '=') btn_color = COLOR_ORANGE;
        
        // Clear and zero get different styling
        if (calc_btns[i] == 'C') btn_color = COLOR_LIGHT_GRAY;
        if (calc_btns[i] == '0') {
            // Make 0 button wider (spans 2 columns)
            draw_rounded_rect(buf, x, y, 300, 100, 20, btn_color);
            char btn_text[2] = {calc_btns[i], 0};
            int text_w = measure_text_width(btn_text, MEDIUM_TEXT);
            draw_text(buf, btn_text, MEDIUM_TEXT, x + 150 - text_w/2, y + 30, COLOR_WHITE);
            i++; // Skip next position since 0 is wide
            continue;
        }
        
        draw_rounded_rect(buf, x, y, 140, 100, 20, btn_color);
        
        char btn_text[2] = {calc_btns[i], 0};
        int text_w = measure_text_width(btn_text, MEDIUM_TEXT);
        draw_text(buf, btn_text, MEDIUM_TEXT, x + 70 - text_w/2, y + 30, COLOR_WHITE);
    }
}