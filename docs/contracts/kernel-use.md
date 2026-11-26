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
| Panel scan-out | `tiku_display_init`, `_set_buffers`, `_submit`, `_poll`, `tiku_s31_psram_sync`; the driver returns its own scan-out buffer when handed the frames | `platform/tikuos/display.c` | Owner turn | Frame |
| Touch reports | `tiku_touch_poll` on the I2C bus, inside a time budget | `platform/tikuos/input.c` | Owner turn | Report |
| The camera | `tiku_s31_camera_open`, `_stream`, `_latest`, `_capture`, `_poll`, `_close`: the SC101IOT on the DVP port, its pictures or a whole frame into a buffer from the desktop's heap, its end of frame waking the owner turn; `tiku_s31_psram_invalidate` before the desktop reads a picture | `platform/tikuos/camera.c`, the kernel call and published picture number | Owner turn | Camera window |
| JPEG encoding | `tiku_s31_jpeg_encode`, `_poll` on the S31's codec | `platform/tikuos/camera.c`, the kernel camera request | Owner turn | Photo |
| Desktop memory | Two PSRAM tier arenas, `tiku_tier_arena_create_span`: the frames before the panel opens, the stacks, stages and heap after | `surface_os_reserve_frames()`, `surface_os_reserve()` in `platform/tikuos/bindings.c` | Start | Until restart |
| The desktop thread | `tiku_thread_start`, `_wait`, `_wake_all`, `_exit` | `platform/tikuos/desktop_bindings.c` | Desktop thread | Until restart |
| Native device requests | The worker calls `tiku_thread_call` to submit frames, card and camera operations, change the touch map, dequeue input, and claim or finish a diagnostic hash. The surface process advances the hardware on its scheduler turns. | `surface_os_desktop_call()`, `desktop/runtime.c` | Kernel receives the request. The desktop or job worker waits for frames, media and calibration on the kernel wait queue. | The worker retains pixels and media buffers until completion. Camera buffers remain in the desktop heap while the sensor or codec uses them. |
| The owner turn | One process, `surface_process`, woken by a timer and by polls | `platform/tikuos/bindings.c` | Kernel | Until restart |
| Local program windows | Two bounded queues over the local message link. The desktop worker calls the kernel through `tiku_thread_call`. The BASIC job owns its input and a display list. Stomme decodes that list and routes window input back to the job. | `platform/tikuos/local_programs.c`, `desktop/local_session.c` | Kernel and desktop thread | Connection |
| Local process forms | The C process publishes its rows under `/gui/demo`. Tracker enumerates the live directory and opens its described panel. Row writes check the registered owner and the desktop's VFS capability in kernel context. | `desktop/board.c`, `platform/tikuos/local_form.c` | Kernel and desktop thread | Process instance |
| Monotonic time | `tiku_clock_usec` | `surface_os_time()` | Both | Since boot |
| Wall-clock time | `tiku_rtc_read`, through TikuOS's `compat/posix` `clock_gettime`, `gettimeofday` and `time` | Linked with `TIKU_POSIX_ENABLE := 1`, `desktop/firmware.mk` | Desktop thread | Durable |
| The clock's standing | `tiku_rtc_state`: unset, set this boot, or resumed after a restart | `surface_os_clock_state()`; the Deskbar's `tiku_shell_clock_state()`; file times are 0 while unset | Desktop thread | Boot |
| SD card files | TikuOS's POSIX media group and the mounted card service. The group enumerates one catalog epoch, opens versioned readers, reads by offset, publishes staged or streamed writes and checks object IDs on rename and delete. | `desktop/posix.c` calls `tiku_posix_media_*` through the native card request. `platform/tikuos/media_io.c` sets the client's rights and checks owner context. | Desktop and job workers, serviced on the kernel thread | Card |
| Background work | Stomme's jobs (`tiku_work.c`, `TIKU_WORK_WORKERS=1`) on one thread of TikuOS's `compat/posix` threads group: a kernel worker with a 384 KiB stack in the runtime's PSRAM span, taken from the desktop's heap | `desktop/desktop.mk`, `desktop/firmware.mk`; `surface_os_desktop_jobs_memory()` | Job thread | Until restart |
| The C library's heap | TikuOS's `compat/posix` heap group (`TIKU_POSIX_HEAP`): `_sbrk_r` over the part of the runtime's PSRAM arena the desktop gives, and the allocator's lock, a short interrupt-masked section; newlib's allocator runs inside it. The kernel keeps no heap. | `surface_os_desktop_heap_memory()`, `surface_os_desktop_heap_use()`; the post-link check refuses a heap hook from anywhere else | Desktop, job and kernel threads | Until restart |
| Mutexes, condition variables, the C library's locks | TikuOS's `compat/posix` threads group: `pthread_mutex_*`, `pthread_cond_*` and newlib's stream, environment and time-zone locks on kernel wait queues | Linked with the library; `desktop/posix.c` defines none; the status line's `locks=` shows their use | Desktop and job threads | Call |
| The kernel's namespace | A Tracker device at `/devices/<name>` (`/sys/device/name`): the manifest `/sys/vfs/manifest` for nodes, descriptors, capabilities and ids, `compat/posix`'s reads, its node writes under the authority `TIKU_VFS_CAP_SYS` and `TIKU_VFS_CAP_FS`, and the file store for `/data`'s files; changes from the backend's own reader of the VFS change ring, a descriptor on `/sys/vfs/events`, beside the console's | `desktop/board.c`, mounted through Stomme's `TIKU_SHELL_LOCAL_BOARD`; `surface_os_desktop_authority()`. Writes reach `/data`'s files, editable local form rows and five reviewed settings (device name, CPU frequency target, crypto mode, radio power, time); a secret is not read | Desktop and job threads | Call |
| Touch calibration | File store record `tikusurface-cal-v1` in `/data` | `platform/tikuos/settings_io.c` | Owner turn | Durable |
| Desktop settings and session state | TikuOS's `compat/posix` store group over the file store: `TrackerSettings`, `tracker/types` and `devices/local.state` are `/data/ts/settings`, `ts/types` and `ts/session`, their temporaries the key with the same ending; written through 16 KiB stages from the runtime's arena and published by `fsync`, `close` and `rename`.  The start-up folders' extended attributes are `/data/ts/views`, written an hour after its last write and when the desktop quits | `desktop/posix.c` routes the paths and reads each file from a copy of its published content; `surface_os_desktop_store_memory()` | Desktop and job threads | Durable |
| The processor's name and clock | `/sys/device/mcu` and `/sys/cpu/freq`, the rate in force, through `compat/posix`'s reads | `tiku_sys_processor()` in `desktop/sys.c`, for the About box | Desktop thread | Call |
| The memory maps | `/sys/mem/sram_map` and `/sys/mem/nvm_map` through `compat/posix`'s reads; PSRAM's size, the tier's place and size from `tiku_s31_psram_size` and `tiku_tier_span_stats` at start, and the tier's free bytes again with each status line; the heap's bytes in use from newlib's `mallinfo` | `tiku_sys_memory()` and `tiku_sys_memory_map()` in `desktop/sys.c`, for the About box; `surface_os_psram_extent()`, `surface_os_desktop_heap_live()` | Owner turn; desktop thread | Call |
| Random devices | TikuOS VFS character devices `/dev/random` and `/dev/urandom`, using the S31 entropy driver | `desktop/posix.c` routes each open to the POSIX read group | Desktop and job threads | One sample per read |
| File digest | TikuKits streaming SHA-256 | `platform/tikuos/desktop_hash.c` reads the file in 1 KiB chunks | Desktop worker | Open file |
| Console output | `tiku_console_write` | `surface_os_desktop_print()` | Owner turn | Line |
| Diagnostics | VFS nodes under `/surface` | `platform/tikuos/diagnostics.c` | Owner turn | Until restart |

## Carried by the product

| Facility | Product code | Why it is here | Kernel provider to use |
| --- | --- | --- | --- |
| Files, directories, descriptors | A tree in the desktop's heap, `desktop/posix.c`, under one lock that a card request gives back while it waits; `/home` and `/Card` are card objects in it | Stomme and newlib call POSIX. Synthetic nodes report `ENOTSUP` for volume capacity. The mounted card and its home alias report storage space from MFS. The tree's own folders are lost at restart | The VFS, `/data` and the media store, through a POSIX library in TikuOS |
| Per-file view state | Extended attributes on the tree's nodes, `desktop/posix.c`, for Stomme built with `TIKU_STATE_XATTR`; those of `/home`, its `Desktop` and `Documents` and the Tracker desktop are kept in `/data/ts/views`, and a card Trash's original paths in its `.origins` object | The file store and the media volume have no attributes; the other attributes are lost at restart | Attributes in the file store |
| Change notices | TikuOS's POSIX media group keeps the directory change ring and each reader's cursor. The desktop supplies the directory inode for a local change and the root inode when another card client's catalog changes. | Tracker maps the same volume into its home and card trees. On refresh it merges a complete listing and preserves open writers. | Kernel VFS nodes publish through the VFS change ring. Desktop directory inodes use the media change ring. |
| Profiler samples | Reads a worker's saved context from the thread control block, `surface_os_desktop_sample()` | The kernel has no call for it | A kernel sampling call |
| Card namespace | The desktop maps `/Card` and `/home`, owns open descriptors and their staged bytes, chooses temporary stream names and manages Trash metadata and folder placeholders. | These names and open handles belong to the desktop session. | The POSIX media group supplies the underlying file, directory and change-cursor operations. |

## Local programs and the shell

The native desktop opens registered program windows through Stomme's packet
session. The numbered BASIC counter owns its window. The C form publishes
its controls under `/gui/demo`. Terminal runs Stomme's grid, VT100 parser
and menus in the desktop worker. Its second local window session carries
keyboard and menu events. A kernel process owns the shell capture and
restores the previous console on close or process exit. The capture takes
the intersection of the desktop and UART capabilities.

The connected profile selects DNS, SNTP and the kernel's `ntp` command.
Tracker reads the board manifest and exposes the Wi-Fi controls it lists.
Scan, disconnect and staged profile SSID, authentication and connect go
through the compatibility library and the kernel capability check.
Credential values remain hidden. The local profile omits the radio nodes.

Child processes and dynamically installed desktop applications need a
process loader. The current image starts its linked applications through
the Deskbar. `execvp` and `fork` return `ENOSYS`.

## Checks

The native image's post-link step refuses a system call taken from libnosys,
and a heap hook or allocator lock taken from anywhere but TikuOS's
`compat/posix` (`tools/libc_stubs.py`); without the library, `clock_gettime` would come from
libnosys and fail the link. `tests/test_posix_layer.c` builds `desktop/posix.c`
on the host under the address and undefined-behaviour sanitizers, and
`tests/test_posix_threads.c` calls it from several threads against a card
whose requests the test holds open, under the thread sanitizer and under the
address sanitizer.
