/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * test_basic_workflow.c - draft execution, retained edits and owned cancellation.
 * SPDX-License-Identifier: Apache-2.0
 */
#define main terminal_fixture_main
#include "test_terminal.c"
#undef main
int main(void)
{
    surface_host_t h;surface_config_t c=surface_host_config(&h);surface_t *s=surface_create(&c);assert(s);
    assert(surface_launch(s,SURFACE_EDITOR_APP));surface_step(s);
    surface_app_state_t *editor=s->active.state;
    assert(surface_editor_accept(editor,"10 PRINT 42\n20 END\n"));assert(surface_editor_modified(editor));
    click(s,705,390);assert(s->active.state->id==SURFACE_TERMINAL_APP&&h.program_state==SURFACE_PROGRAM_RUNNING);
    assert(!strcmp(h.program_source,"10 PRINT 42\n20 END\n"));
    surface_view_t v;surface_view(s,&v);assert(v.program_state==SURFACE_PROGRAM_RUNNING);
    key(s,'x',0);assert(!h.terminal_in_size);assert(!surface_terminal_command(s->active.state,"echo x"));
    assert(surface_launch(s,SURFACE_EDITOR_APP));surface_step(s);assert(surface_editor_accept(editor,"30 REM changed\n"));
    assert(!strstr(h.program_source,"changed")&&surface_editor_modified(editor));
    assert(surface_launch(s,SURFACE_TERMINAL_APP));surface_step(s);click(s,200,420);
    surface_view(s,&v);assert(v.program_state==SURFACE_PROGRAM_STOPPED&&h.program_stops==1);
    assert(surface_launch(s,SURFACE_EDITOR_APP));surface_step(s);h.program_busy=1;click(s,705,390);
    surface_view(s,&v);assert(v.program_state==5&&h.program_stops==1);h.program_busy=0;
    assert(surface_launch(s,SURFACE_EDITOR_APP));surface_step(s);click(s,705,390);
    assert(h.program_state==SURFACE_PROGRAM_RUNNING&&strstr(h.program_source,"changed"));
    h.program_state=SURFACE_PROGRAM_DONE;surface_step(s);surface_view(s,&v);assert(v.program_state==SURFACE_PROGRAM_DONE);
    assert(surface_launch(s,SURFACE_EDITOR_APP));surface_step(s);click(s,705,390);assert(h.program_state==SURFACE_PROGRAM_RUNNING);
    h.terminal_close_delay=4;assert(surface_close(s)==SURFACE_CLOSE_ACCEPTED);surface_step(s);
    assert(h.program_state==SURFACE_PROGRAM_STOPPED&&h.program_stops==2&&h.terminal_open);
    for(unsigned n=0;n<20;++n)surface_step(s);
    assert(!h.terminal_open);
    assert(surface_launch(s,SURFACE_EDITOR_APP));surface_step(s);assert(surface_editor_modified(s->active.state));
    assert(surface_close(s)==SURFACE_CLOSE_CONFIRM);assert(surface_resolve_close(s,0));
    finish(&h,&s);puts("BASIC workflow: current draft, copied source, retained edits, busy refusal, results, stop and close ownership passed");return 0;
}
