/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * basic_window.c - the BASIC counter program on the local desktop.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "../../desktop/local_programs.h"
#include "tiku_draw_program.h"
#include "tiku_draw.h"
#include <kernel/vfs/tiku_vfs.h>

int surface_basic_window_start(void)
{
    static const char source[] =
        "10 WINOPEN \"BASIC Counter\"\n"
        "20 LET N = 0\n"
        "30 WINSHOW \"Count \" + STR$(N)\n"
        "40 WINWAIT\n"
        "50 IF WINLIVE() = 0 THEN END\n"
        "60 LET N = N + 1\n"
        "70 GOTO 30\n";
    if (tiku_draw_link_is_console()) return -1;
    return tiku_draw_program_start(source, TIKU_VFS_CAP_NONE);
}

void surface_basic_window_poll(void) { tiku_draw_program_poll(); }
void surface_basic_window_stop(void) { tiku_draw_program_stop(); }
int surface_basic_window_status(char *out, size_t max)
{ return tiku_draw_program_status(out, max); }
