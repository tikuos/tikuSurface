# Appearance and touch adaptation

TikuSurface uses the BeOS R5 theme exposed by its pinned Stomme SDK. The shell
draws the blue desktop backdrop, gray raised panels, yellow active title tabs,
black text and white document fields through shared theme roles and controls.
The launcher uses `tiku_ui_window` for its application frame; the desktop profile
uses the native Stomme workspace. `tiku_ui_button` draws controls.
The plain and bold 12-pixel fonts come from the SDK font subset.

The visual reference is TikuDesktop revision
`6622c2a49225af5fba3e928e46e70069d86c6491`, with Stomme revision
`6f67e29a42b6f8e65e45b228ba8748039071c200` supplying the portable renderer.
The [artwork record](../../assets/icons/README.md) identifies copied source assets.

The default launcher profile's fixed 800 by 480 layout has one foreground application and retains open
applications in the background. Home, Back, Applications and Close occupy the
top toolbar. Modal menus and close decisions capture input until resolved.
The title tab identifies the foreground application;
it has no close, resize or zoom action. Launcher targets are 232 by 144 pixels
with 48-pixel icons. Calibration controls have 44-pixel target heights. These
dimensions adapt the desktop components for touch. The task menu restores open
applications; Close asks before discarding an unsaved settings draft. Desktop
window movement/resizing and writable file-manager operations require the
[desktop and Tracker profile](../build/core-desktop.md).

The desktop profile has Volumes and Settings icons, a right-hand Deskbar and
retained Tracker and Settings windows. Native title tabs provide close and zoom;
dragging the tab moves a window and its lower-right corner resizes it. Hide
minimizes a window; its Deskbar entry restores it. Tracker supports file and
folder creation, copying, moving, information and confirmed permanent deletion
through the queued storage provider. The profile retains one Tracker window;
folder navigation reuses it. Touch text entry supplies names and paths.

The desktop requires a matching SDK with `window-workspace` and `deskbar-paint`.
Its build guide specifies the dependency override until those capabilities are
adopted through the foundation gitlink and lock. The default pinned SDK does not
export them. The embedded renderer freezes window geometry and content for each
frame so two-row painting retains a consistent composition.

Device information shows the product revision and, on firmware builds, the
TikuOS revision. Modified source trees carry a `-modified` suffix. Defaults sets
the editable calibration to panel coordinates, and Save submits it to storage.
Pending saves and unavailable or incompatible settings disable editing. The
screen labels an edited draft as unsaved. Defaults requires a usable input map
to reach the control and does not provide recovery from an unreachable screen.

Host acceptance checks cover theme colors, control actions and identical output
for full-frame and two-row painting. Host captures establish source-rendered
appearance. Panel color, text readability, touch reachability and visible latency
require physical display checks for the selected firmware build.
