/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * editor_context.c - shell-owned allocation lifetime for shared document sessions.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "surface_internal.h"
#include "tiku_edit_runtime.h"
#include "tiku_pool.h"
#include <limits.h>
#include <string.h>
/** @brief One allocator remains bound while retained documents own its storage. */
struct surface_editor_context {
    tiku_owned_buffer_t self;
    unsigned references;
    tiku_edit_runtime_t runtime;
    tiku_pool_t pool;
    tiku_pool_slot_t slots[SURFACE_NOTES?2048:512];
    _Alignas(16) unsigned char arena[SURFACE_NOTES?1048576:131072];
};
int surface_editor_context_acquire(surface_t *s)
{
    if(!s)return 0;
    struct surface_editor_context *c=s->editor_context;
    if(c){if(c->references==UINT_MAX)return 0;++c->references;return 1;}
    tiku_owned_buffer_t memory;
    if(!tiku_buffer_init(&memory,s->config.allocator,_Alignof(struct surface_editor_context))||
       !tiku_buffer_resize(&memory,1,sizeof *c,0))return 0;
    c=memory.data;memset(c,0,offsetof(struct surface_editor_context,arena));c->self=memory;
    if(!tiku_pool_init(&c->pool,c->arena,sizeof c->arena,c->slots,sizeof c->slots/sizeof c->slots[0]))goto failed;
    c->runtime.allocator=tiku_pool_allocator(&c->pool);
    if(!tiku_edit_runtime_bind(&c->runtime))goto failed;
    c->references=1;s->editor_context=c;return 1;
failed:
    tiku_buffer_dispose(&memory);return 0;
}
void surface_editor_context_release(surface_t *s)
{
    struct surface_editor_context *c=s?s->editor_context:NULL;
    if(!c||!c->references||--c->references)return;
    if(!tiku_edit_runtime_bind(NULL))return;
    tiku_owned_buffer_t memory=c->self;s->editor_context=NULL;tiku_buffer_dispose(&memory);
}
