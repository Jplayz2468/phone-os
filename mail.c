#include "apps.h"
#include <stdio.h>
#include <string.h>

void draw_mail_app(uint32_t *buf) {
    // Mail app interface
    draw_text_centered(buf, "Inbox", MEDIUM_TEXT, STATUS_HEIGHT + 150, COLOR_WHITE);
    
    // Email list
    const char* senders[] = {
        "John Smith",
        "Newsletter",
        "Work Team",
        "Mom",
        "GitHub"
    };
    
    const char* subjects[] = {
        "Meeting Tomorrow",
        "Weekly Update",
        "Project Status",
        "Dinner Plans",
        "Security Alert"
    };
    
    const char* previews[] = {
        "Hi, can we reschedule our meeting...",
        "Here's what happened this week...",
        "The project is on track for...",
        "Would you like to come over for...",
        "We detected a new sign-in to your..."
    };
    
    const char* times[] = {
        "2:30 PM",
        "1:15 PM",
        "12:45 PM",
        "11:30 AM",
        "10:15 AM"
    };
    
    int start_y = STATUS_HEIGHT + 220;
    for (int i = 0; i < 5; i++) {
        int y = start_y + i * 100;
        
        // Email item background
        if (i == 0) {
            // Unread email - slightly highlighted
            draw_rect(buf, 0, y - 10, screen_w, 90, 0xFF111111);
        }
        
        // Unread indicator (blue dot)
        if (i == 0) {
            draw_circle_filled(buf, 30, y + 15, 6, COLOR_BLUE);
        }
        
        // Sender name
        draw_text(buf, senders[i], SMALL_TEXT, 60, y, COLOR_WHITE);
        
        // Time
        int time_w = measure_text_width(times[i], SMALL_TEXT);
        draw_text(buf, times[i], SMALL_TEXT, screen_w - 60 - time_w, y, COLOR_LIGHT_GRAY);
        
        // Subject
        draw_text(buf, subjects[i], SMALL_TEXT, 60, y + 30, i == 0 ? COLOR_WHITE : COLOR_LIGHT_GRAY);
        
        // Preview
        draw_text(buf, previews[i], SMALL_TEXT - 4, 60, y + 55, COLOR_GRAY);
        
        // Separator line
        if (i < 4) {
            draw_rect(buf, 60, y + 85, screen_w - 120, 1, COLOR_GRAY);
        }
    }
    
    // Compose button
    int compose_x = screen_w - 80;
    int compose_y = screen_h - 180;
    draw_circle_filled(buf, compose_x, compose_y, 30, COLOR_BLUE);
    draw_text(buf, "✉", MEDIUM_TEXT, compose_x - 15, compose_y - 20, COLOR_WHITE);
    
    // Bottom toolbar
    int toolbar_y = screen_h - 120;
    draw_rect(buf, 0, toolbar_y, screen_w, 2, COLOR_GRAY);
    
    // Mailbox list indicator
    draw_text(buf, "📧 Inbox (2)", SMALL_TEXT, 60, toolbar_y + 20, COLOR_WHITE);
}