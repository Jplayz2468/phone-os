#include "apps.h"
#include <stdio.h>
#include <string.h>

void draw_test_app(uint32_t *buf) {
    draw_text_centered(buf, "TEST", MEDIUM_TEXT, STATUS_HEIGHT + 150, COLOR_WHITE);
}