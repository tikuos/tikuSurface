/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * test_navigation.c - logical navigation keys stay distinct from editing controls.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "host.h"
#include "surface_internal.h"
#include "tiku_event.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
/** @brief Dispatch one toolkit key without legacy character-code conversion. */
static void key(surface_t *s,unsigned code)
{tiku_input_event_t e={.type=TIKU_EVENT_KEY_DOWN,.key=code};assert(surface_post(s,&e));surface_step(s);}
/** @brief Read the current copied presentation. */
static surface_view_t view(surface_t *s){surface_view_t v;surface_view(s,&v);return v;}
int main(void)
{
    surface_host_t host;surface_config_t cfg=surface_host_config(&host);surface_t *s=surface_create(&cfg);assert(s&&surface_launch(s,SURFACE_CALCULATOR));surface_step(s);
    key(s,'F');key(s,'F');key(s,TIKU_KEY_PAGE_DOWN);assert(!strcmp(view(s).hex,"FF"));key(s,TIKU_KEY_BACKSPACE);assert(!strcmp(view(s).hex,"F"));
    key(s,'F');surface_app_action(s->active.state,ACTION_ERASE);assert(!strcmp(view(s).hex,"F"));
#if SURFACE_EDITOR
    assert(surface_launch(s,SURFACE_EDITOR_APP));surface_step(s);surface_app_state_t *e=s->active.state;
    assert(surface_editor_accept(e,"0\n1\n2\n3\n4\n5\n6\n7\n8\n9\n10\n11\n12\n13\n14\n15"));
    surface_editor_dialog(e,6);assert(surface_editor_accept(e,"1"));tiku_field_stop(&s->text_field);
    key(s,TIKU_KEY_PAGE_DOWN);surface_view_t v=view(s);assert(v.editor_grid.first_line+v.editor_grid.caret_y==8);
    size_t bytes=v.editor_bytes;key(s,TIKU_KEY_TAB);v=view(s);assert(v.editor_bytes==bytes+(SURFACE_EDITOR_LINES?4:1)&&v.editor_grid.first_line+v.editor_grid.caret_y==8&&v.editor_grid.caret_x==4);
    surface_editor_dialog(e,3);key(s,'a');key(s,'b');key(s,TIKU_KEY_PAGE_DOWN);assert(!strcmp(view(s).text_value,"ab"));
    key(s,TIKU_KEY_TAB);assert(view(s).text_entry);key(s,TIKU_KEY_BACKSPACE);assert(!strcmp(view(s).text_value,"a"));key(s,TIKU_KEY_ESCAPE);assert(!view(s).text_entry);
#endif
    surface_stop(s);assert(surface_destroy(&s));assert(!host.memory.live);puts("Logical input: Page Down, Tab, Backspace and Escape remain distinct across document and field controls");return 0;
}
