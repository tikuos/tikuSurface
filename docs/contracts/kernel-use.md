# Kernel use

TikuSurface runs on TikuOS and takes from it what TikuOS provides. A need the
kernel answers is answered by the kernel, through the bindings in
`platform/tikuos/`. A need it does not answer is raised in TikuOS and closed
there; until then the product's own code for it is listed below with its
reason. TikuSurface's own code is what no kernel has: panel presentation,
gestures, and the adapter that lets a desktop written for POSIX run. The
adapter adapts and does not store: state that must outlast the desktop
thread belongs in a kernel store.

This page lists, for the native desktop (`SURFACE_NATIVE_DESKTOP=1`), where
each operating-system facility comes from, the context that uses it, and how
long what it holds lasts. Change it in the same commit as the code it
describes.

## From the kernel

| Facility | Kernel provider | Binding | Context | Lifetime |
| --- | --- | --- | --- | --- |
| Panel scan-out | `tiku_display_init`, `_submit`, `_poll`, `tiku_s31_psram_sync` | `platform/tikuos/display.c` | Owner turn | Frame |
| Touch reports | `tiku_touch_poll` on the I2C bus, inside a time budget | `platform/tikuos/input.c` | Owner turn | Report |
| Desktop memory | One PSRAM tier arena, `tiku_tier_arena_create_span` | `platform/tikuos/bindings.c` | Start | Until restart |
| The desktop thread | `tiku_thread_start`, `_wait`, `_wake_all`, `_exit` | `platform/tikuos/desktop_bindings.c` | Desktop thread | Until restart |
| The owner turn | One process, `surface_process`, woken by a timer and by polls | `platform/tikuos/bindings.c` | Kernel | Until restart |
| Monotonic time | `tiku_clock_usec` | `surface_os_time()` | Both | Since boot |
| Wall-clock time | `tiku_rtc_read`, through TikuOS's `compat/posix` `clock_gettime`, `gettimeofday` and `time` | Linked with `TIKU_POSIX_ENABLE := 1`, `desktop/firmware.mk` | Desktop thread | Durable |
| The clock's standing | `tiku_rtc_state`: unset, set this boot, or resumed after a restart | `surface_os_clock_state()`; the Deskbar's `tiku_shell_clock_state()`; file times are 0 while unset | Desktop thread | Boot |
| SD card, reading | Media card service: mount, enumerate, open, read | `platform/tikuos/media_io.c` | Owner turn | Card |
| Touch calibration | File store record `tikusurface-cal-v1` in `/data` | `platform/tikuos/settings_io.c` | Owner turn | Durable |
| Random source | `tiku_trng_arch_read_bytes` | `surface_os_desktop_random()` | Desktop thread | Seed |
| Console output | `tiku_console_write` | `surface_os_desktop_print()` | Owner turn | Line |
| Diagnostics | VFS nodes under `/surface` | `platform/tikuos/diagnostics.c` | Owner turn | Until restart |

## Carried by the product

| Facility | Product code | Why it is here | Kernel provider to use |
| --- | --- | --- | --- |
| Files, directories, descriptors | A tree in the desktop's heap, `desktop/posix.c` | Stomme and newlib call POSIX; the tree is lost at every restart | The VFS, `/data` and the media store, through a POSIX library in TikuOS |
| Desktop settings and per-file state | Files in that tree: `TrackerSettings`, `tracker/types`, `devices/local.state` | As above; lost at every restart | `/data` |
| The desktop's heap | newlib's allocator over the arena, `_sbrk_r` in `desktop/posix.c` | The kernel gives no C-library heap by design; the arena is the kernel's | Stays |
| Random numbers for the desktop | SHA-256 in counter mode over a TRNG seed, `desktop/posix.c` | `/dev/urandom` for the desktop's tree | Stays with the file layer |
| Requests from the desktop thread to the owner | Token pairs in `desktop/bridge.h`: frames, card, touch map, hash | TikuOS has no path for a worker's request into kernel context | A kernel request path |
| Change notices | A counter of tree changes the desktop polls, `tiku_posix_mutations()` | Changes happen only in the desktop's own tree | `tiku_vfs_watch` and the VFS change ring |
| Locks | `pthread_mutex_*` that succeed and do nothing, `desktop/posix.c` | Valid only while one thread calls the layer | Kernel wait queues |
| Background work | Stomme's jobs run on the desktop thread (`tiku_work_inline.c`) | The threaded pool needs pthreads | Kernel worker threads |
| Profiler samples | Reads a worker's saved context from the thread control block, `surface_os_desktop_sample()` | The kernel has no call for it | A kernel sampling call |
| SD card view | `/Card` as a read-only tree in `desktop/posix.c` | Writes are not bound | The media store's write operations |

## Not reached by the native desktop

The kernel's namespace (`/sys`, `/dev`, `/data`), its processes and shell, the
network and programs that open windows on a desktop are not visible to the
native desktop. Starting a program is refused: `execvp` returns `ENOSYS`,
and `fork` reaches libnosys's `_fork`, which does the same. Remote sessions are
answered as absent (`desktop/absent.c`).
The core image's shell reaches the namespace, the process table, the kernel's
shell, BASIC jobs, Wi-Fi status and card writes through `vfs_io.c`,
`process_io.c`, `console_io.c`, `services.c` and `media.c` in
`platform/tikuos/`; the native image links none of them.

## Checks

The native image's post-link step refuses a system call taken from libnosys
(`tools/libc_stubs.py`); without the library, `clock_gettime` would come from
libnosys and fail the link. `tests/test_posix_layer.c` builds `desktop/posix.c`
on the host under the address and undefined-behaviour sanitizers.
