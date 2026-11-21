/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * test_editor_context.c - retained model sessions sharing the shell allocator.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "surface_internal.h"
#include "host.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
/** @brief Read the current document length through its presentation adapter. */
static size_t bytes(surface_app_state_t *s)
{surface_view_t v={0};surface_editor_view(s,&v);return v.editor_bytes;}
/** @brief Dispose a child whose storage session has no outstanding request. */
static void dispose(surface_app_state_t *s)
{tiku_paged_storage_close(s->store);assert(tiku_paged_storage_destroy(&s->store));surface_editor_dispose(s);}
int main(void)
{
    surface_host_t host;surface_config_t cfg=surface_host_config(&host);surface_t *s=surface_create(&cfg);assert(s);
    assert(surface_launch(s,SURFACE_EDITOR_APP));surface_step(s);assert(s->active.state&&s->editor_context);
    surface_app_state_t child={.shell=s,.id=SURFACE_EDITOR_APP};assert(surface_editor_start(&child));
    assert(surface_editor_accept(s->active.state,"first")&&surface_editor_accept(&child,"second"));
    assert(bytes(s->active.state)==5&&bytes(&child)==6);
    struct surface_editor_context *context=s->editor_context;
    assert(surface_close(s)==SURFACE_CLOSE_CONFIRM);assert(surface_resolve_close(s,1));
    for(unsigned i=0;i<10;++i)surface_step(s);
    assert(s->editor_context==context&&bytes(&child)==6);
    assert(surface_launch(s,SURFACE_EDITOR_APP));surface_step(s);assert(s->editor_context==context);
    assert(surface_editor_accept(s->active.state,"third")&&bytes(s->active.state)==5);
    surface_editor_action(&child,ACTION_EDITOR_UNDO);assert(!bytes(&child)&&bytes(s->active.state)==5);
    surface_editor_action(&child,ACTION_EDITOR_REDO);assert(bytes(&child)==6);
    surface_host_t other_host;surface_config_t other_cfg=surface_host_config(&other_host);surface_t *other=surface_create(&other_cfg);assert(other);
    assert(surface_launch(other,SURFACE_EDITOR_APP));surface_step(other);surface_view_t v;surface_view(other,&v);assert(v.foreground!=SURFACE_EDITOR_APP&&!other->editor_context);
    surface_stop(other);for(unsigned i=0;i<30&&!surface_destroy(&other);++i){surface_step(other);surface_host_poll(&other_host);}assert(!other&&!other_host.memory.live);
    assert(surface_editor_accept(&child,"!")&&bytes(&child)==7);
    surface_stop(s);for(unsigned i=0;i<10;++i){surface_step(s);surface_host_poll(&host);}assert(!surface_destroy(&s)&&s->editor_context==context);
    dispose(&child);assert(!s->editor_context);
    for(unsigned i=0;i<30&&!surface_destroy(&s);++i){surface_step(s);surface_host_poll(&host);}assert(!s&&!host.memory.live);
    puts("Editor context: independent documents, close/reopen, retained allocator, second-shell refusal and teardown drain passed");return 0;
}
