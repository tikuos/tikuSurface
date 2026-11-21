/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * runtime_documents.c - full document capacity through the firmware allocator.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "../platform/tikuos/runtime.c"
#include "surface_internal.h"
#include "tiku_event.h"
#include <assert.h>
/** @brief Retain eight full documents with independent replacement history. */
void surface_test_fill_editor_documents(void)
{
    surface_app_state_t *s=runtime->shell->active.state;assert(s&&s->id==SURFACE_EDITOR_APP);
    static char text[8193];memset(text,'a',sizeof text-1);text[8192]=0;
    for(unsigned i=0;i<8;++i){
        if(i)surface_editor_action(s,ACTION_EDITOR_SESSION_NEW);
        assert(surface_editor_accept(s,text));
        surface_editor_key(s,&(tiku_input_event_t){.type=TIKU_EVENT_KEY_DOWN,.key='a',.modifiers=TIKU_MOD_CMD});
        text[0]=(char)('b'+i);assert(surface_editor_accept(s,text));
        surface_editor_action(s,ACTION_EDITOR_UNDO);surface_editor_action(s,ACTION_EDITOR_REDO);
        surface_view_t v={0};surface_editor_view(s,&v);assert(v.editor_bytes==8192&&v.editor_count==i+1&&v.editor_dirty);
    }
#if SURFACE_EDITOR_LINES
    surface_editor_action(s,ACTION_EDITOR_DUPLICATE);surface_view_t v={0};surface_editor_view(s,&v);
    assert(v.editor_bytes==8192&&v.editor_error==TIKU_EDIT_LIMIT);
#endif

}
