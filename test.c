#include "apps.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

// Global state for the test app
static char ping_result[256] = "Press button to ping example.org";
static int button_pressed = 0;
static int ping_in_progress = 0;

// Button dimensions and position
#define BUTTON_WIDTH 400
#define BUTTON_HEIGHT 80
#define BUTTON_X ((screen_w - BUTTON_WIDTH) / 2)
#define BUTTON_Y (STATUS_HEIGHT + 300)

// Function to check if touch is within button bounds
int is_touching_ping_button(int touch_x, int touch_y) {
    return (touch_x >= BUTTON_X && 
            touch_x <= BUTTON_X + BUTTON_WIDTH &&
            touch_y >= BUTTON_Y && 
            touch_y <= BUTTON_Y + BUTTON_HEIGHT);
}

// Function to perform ping (non-blocking)
void perform_ping(void) {
    if (ping_in_progress) return;
    
    ping_in_progress = 1;
    strcpy(ping_result, "Pinging example.org...");
    
    // Fork a child process to perform the ping
    pid_t pid = fork();
    if (pid == 0) {
        // Child process - execute ping
        int result = system("ping -c 1 -W 1 example.org > /dev/null 2>&1");
        exit(result == 0 ? 0 : 1);
    } else if (pid > 0) {
        // Parent process - we'll check the result later
        // For now, just set a simple result
        sleep(1); // Brief delay to simulate ping
        int status;
        if (waitpid(pid, &status, WNOHANG) == 0) {
            // Still running, we'll update result
            if (WEXITSTATUS(status) == 0) {
                strcpy(ping_result, "✓ example.org is reachable!");
            } else {
                strcpy(ping_result, "✗ example.org is not reachable");
            }
        } else {
            // Quick result
            strcpy(ping_result, "✓ Ping completed successfully!");
        }
    } else {
        // Fork failed
        strcpy(ping_result, "✗ Failed to start ping");
    }
    
    ping_in_progress = 0;
}

// Simplified ping function using system command
void simple_ping(void) {
    if (ping_in_progress) return;
    
    ping_in_progress = 1;
    strcpy(ping_result, "Pinging example.org...");
    
    // Use system command for simplicity
    int result = system("ping -c 1 -W 2 example.org > /dev/null 2>&1");
    
    if (result == 0) {
        strcpy(ping_result, "✓ example.org is reachable!");
    } else {
        strcpy(ping_result, "✗ example.org is not reachable");
    }
    
    ping_in_progress = 0;
}

void draw_test_app(uint32_t *buf) {
    // Draw app title
    draw_text_centered(buf, "NETWORK TEST", LARGE_TEXT, STATUS_HEIGHT + 50, COLOR_WHITE);
    
    // Draw ping button
    uint32_t button_color = ping_in_progress ? COLOR_GRAY : COLOR_BLUE;
    draw_rounded_rect(buf, BUTTON_X, BUTTON_Y, BUTTON_WIDTH, BUTTON_HEIGHT, 20, button_color);
    
    // Button text
    const char* button_text = ping_in_progress ? "Pinging..." : "PING EXAMPLE.ORG";
    int text_width = measure_text_width(button_text, MEDIUM_TEXT);
    int text_x = BUTTON_X + (BUTTON_WIDTH - text_width) / 2;
    int text_y = BUTTON_Y + 20;
    draw_text(buf, button_text, MEDIUM_TEXT, text_x, text_y, COLOR_WHITE);
    
    // Draw result text
    draw_text_centered(buf, ping_result, SMALL_TEXT, STATUS_HEIGHT + 450, COLOR_WHITE);
    
    // Instructions
    draw_text_centered(buf, "Tap the button to test connectivity", SMALL_TEXT, STATUS_HEIGHT + 500, COLOR_LIGHT_GRAY);
}

// Function to handle touch input for the test app
// You'll need to call this from your main touch handling code
void handle_test_app_touch(int touch_x, int touch_y, int is_pressed, int was_pressed) {
    // Button press detection
    if (is_pressed && !was_pressed && is_touching_ping_button(touch_x, touch_y)) {
        if (!ping_in_progress) {
            simple_ping();
        }
    }
}