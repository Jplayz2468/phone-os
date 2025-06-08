#include "apps.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// Global state for the test app
static char ping_result[256] = "Ready to test network connectivity";
static int ping_in_progress = 0;

// Circle button dimensions and position
#define BUTTON_RADIUS 120
#define BUTTON_CENTER_X (screen_w / 2)
#define BUTTON_CENTER_Y (STATUS_HEIGHT + 350)

// Function to check if touch is within circular button bounds
int is_touching_ping_button(int touch_x, int touch_y) {
    int dx = touch_x - BUTTON_CENTER_X;
    int dy = touch_y - BUTTON_CENTER_Y;
    int distance_squared = dx * dx + dy * dy;
    return distance_squared <= (BUTTON_RADIUS * BUTTON_RADIUS);
}

// Simple ping function
void simple_ping(void) {
    if (ping_in_progress) return;
    
    ping_in_progress = 1;
    
    // Use system command for ping
    int result = system("ping -c 1 -W 1 example.org >/dev/null 2>&1");
    
    if (result == 0) {
        strncpy(ping_result, "✓ Connection successful!", sizeof(ping_result) - 1);
    } else {
        strncpy(ping_result, "✗ Connection failed", sizeof(ping_result) - 1);
    }
    ping_result[sizeof(ping_result) - 1] = '\0';
    
    ping_in_progress = 0;
}

void draw_test_app(uint32_t *buf) {
    // Draw main title with proper spacing
    draw_text_centered(buf, "Network Tester", MEDIUM_TEXT, STATUS_HEIGHT + 80, COLOR_WHITE);
    
    // Draw subtitle
    draw_text_centered(buf, "Test connectivity to example.org", SMALL_TEXT, STATUS_HEIGHT + 140, COLOR_LIGHT_GRAY);
    
    // Draw big circular button
    uint32_t button_color = ping_in_progress ? COLOR_GRAY : COLOR_BLUE;
    uint32_t button_border_color = ping_in_progress ? COLOR_LIGHT_GRAY : COLOR_WHITE;
    
    // Draw button shadow/border
    draw_circle_filled(buf, BUTTON_CENTER_X + 4, BUTTON_CENTER_Y + 4, BUTTON_RADIUS + 4, 0xFF333333);
    
    // Draw button border
    draw_circle_filled(buf, BUTTON_CENTER_X, BUTTON_CENTER_Y, BUTTON_RADIUS + 2, button_border_color);
    
    // Draw main button
    draw_circle_filled(buf, BUTTON_CENTER_X, BUTTON_CENTER_Y, BUTTON_RADIUS, button_color);
    
    // Button text
    const char* button_text = ping_in_progress ? "PINGING..." : "PING";
    int text_width = measure_text_width(button_text, LARGE_TEXT);
    int text_x = BUTTON_CENTER_X - text_width / 2;
    int text_y = BUTTON_CENTER_Y - 30; // Center vertically in button
    draw_text(buf, button_text, LARGE_TEXT, text_x, text_y, COLOR_WHITE);
    
    // Draw result text with better positioning
    draw_text_centered(buf, ping_result, MEDIUM_TEXT, STATUS_HEIGHT + 550, COLOR_WHITE);
    
    // Draw status indicator
    if (ping_in_progress) {
        // Spinning indicator
        draw_text_centered(buf, "⟳", LARGE_TEXT, STATUS_HEIGHT + 600, COLOR_BLUE);
    } else {
        // Ready indicator
        draw_text_centered(buf, "Tap the button above to test", SMALL_TEXT, STATUS_HEIGHT + 620, COLOR_LIGHT_GRAY);
    }
    
    // Draw connection info at bottom
    draw_text_centered(buf, "Testing: example.org (port 80)", SMALL_TEXT, screen_h - 120, COLOR_GRAY);
}

// Function to handle touch input for the test app
void handle_test_app_touch(int touch_x, int touch_y, int is_pressed, int was_pressed) {
    // Button press detection - only trigger on press down, not while held
    if (is_pressed && !was_pressed && is_touching_ping_button(touch_x, touch_y)) {
        if (!ping_in_progress) {
            simple_ping();
        }
    }
}