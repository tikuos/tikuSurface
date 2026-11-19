/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * calculator.c - bounded hexadecimal word input.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "surface_internal.h"
#include <string.h>

void surface_calculator_key(surface_app_state_t *state, unsigned key)
{
    size_t length = strlen(state->hex);
    state->status = TIKU_RESOURCE_OK;
    if (key == 8 || key == 127) { if (length) state->hex[length - 1] = 0; }
    else if (key == 'x' || key == 'X') state->hex[0] = 0;
    else {
        if (key >= 'a' && key <= 'f') key -= 'a' - 'A';
        if (!((key >= '0' && key <= '9') || (key >= 'A' && key <= 'F'))) state->status = TIKU_RESOURCE_INVALID;
        else if (length == 16) state->status = TIKU_RESOURCE_LIMIT;
        if (length < 16 && ((key >= '0' && key <= '9') || (key >= 'A' && key <= 'F'))) {
            state->hex[length] = (char)key;
            state->hex[length + 1] = 0;
        }
    }
    state->shell->dirty = 1;
}
