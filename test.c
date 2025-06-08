#include "apps.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// Global state for the test app
static char ping_result[256] = "Ready to test connectivity";
static int ping_in_progress = 0;

// Circle button dimensions and position
#define BUTTON_RADIUS 100
#define BUTTON_CENTER_X (screen_w / 2)
#define BUTTON_CENTER_Y (STATUS_HEIGHT + 280)

// Function to check if touch is within circular button bounds
int is_touching_ping_button(int touch_x, int touch_y) {
    int dx = touch_x - BUTTON_CENTER_X;
    int dy = touch_y - BUTTON_CENTER_Y;
    int distance_squared = dx * dx + dy * dy;
    return distance_squared <= (BUTTON_RADIUS * BUTTON_RADIUS);
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

// Simple ping function
void simple_ping(void) {
    if (ping_in_progress) return;
    
    ping_in_progress = 1;
    
    // Use system command for ping
    int result = system("ping -c 1 -W 1 example.org >/dev/null 2>&1");
    
    if (result == 0) {
        strncpy(ping_result, "SUCCESS: example.org reachable", sizeof(ping_result) - 1);
    } else {
        strncpy(ping_result, "FAILED: example.org not reachable", sizeof(ping_result) - 1);
    }
    ping_result[sizeof(ping_result) - 1] = '\0';
    
    ping_in_progress = 0;
}

void draw_test_app(uint32_t *buf) {
    // Note: Title "Test" is already drawn by the main app system at STATUS_HEIGHT + 50
    
    // Draw subtitle below the existing title
    draw_text_centered(buf, "Network Connectivity Test", SMALL_TEXT, STATUS_HEIGHT + 120, COLOR_LIGHT_GRAY);
    
    // Draw big circular button (positioned to avoid overlaps)
    uint32_t button_color = ping_in_progress ? COLOR_GRAY : COLOR_BLUE;
    
    // Draw button shadow
    draw_circle_filled(buf, BUTTON_CENTER_X + 3, BUTTON_CENTER_Y + 3, BUTTON_RADIUS + 3, 0xFF222222);
    
    // Draw button border
    draw_circle_filled(buf, BUTTON_CENTER_X, BUTTON_CENTER_Y, BUTTON_RADIUS + 2, COLOR_WHITE);
    
    // Draw main button
    draw_circle_filled(buf, BUTTON_CENTER_X, BUTTON_CENTER_Y, BUTTON_RADIUS, button_color);
    
    // Button text - just "PING"
    const char* button_text = ping_in_progress ? "WAIT..." : "PING";
    int text_width = measure_text_width(button_text, MEDIUM_TEXT);
    int text_x = BUTTON_CENTER_X - text_width / 2;
    int text_y = BUTTON_CENTER_Y - 20; // Center in button
    draw_text(buf, button_text, MEDIUM_TEXT, text_x, text_y, COLOR_WHITE);
    
    // Draw result text below button (avoid bottom bar area)
    draw_text_centered(buf, ping_result, SMALL_TEXT, STATUS_HEIGHT + 420, COLOR_WHITE);
    
    // Instructions
    if (!ping_in_progress) {
        draw_text_centered(buf, "Tap circle to ping example.org", SMALL_TEXT, STATUS_HEIGHT + 460, COLOR_LIGHT_GRAY);
    } else {
        draw_text_centered(buf, "Testing connection...", SMALL_TEXT, STATUS_HEIGHT + 460, COLOR_BLUE);
    }
}