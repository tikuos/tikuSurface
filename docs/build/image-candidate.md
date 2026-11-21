# Image candidate

`SURFACE_IMAGE=1` adds TikuImage and paged Home and Applications views. It requires
`SURFACE_NOTES=1`, `SURFACE_DOCUMENTS=1` and an explicit Stomme SDK with
`bitmap-progress`, `png-progress` and `image-view-model`. The foundation lock selects the
published SDK independently of this candidate composition.

Tracker opens BMP and PNG files in TikuImage. Open image accepts an absolute media path
through 64 bytes and detects BMP and PNG content independently of its suffix. The viewer
accepts uncompressed 24/32-bit BMP through 1 MiB encoded, 131072 decoded pixels
and 512 pixels per side. Top-down and bottom-up rows use the native decoder;
32-bit BMP's reserved fourth byte is ignored. PNG accepts grayscale and palette
samples at 1, 2, 4 or 8 bits, and RGB, grayscale-alpha and RGBA at 8 bits.
Its native chunk CRC, palette transparency, filters and Adam7 rules are shared
with desktop Image. JPEG remains unavailable.

Fit, actual size, zoom, rotation and pan use the native Image model. Toolbar
buttons, keyboard commands, primary-pointer dragging and wheel input change the
view. Shift-wheel pans horizontally; Command-wheel zooms. Plus/minus zoom, F or 0 fits,
1 selects actual size, R rotates, and arrow keys pan. Command-O opens a path;
Command-R reloads. Watch 2s rereads the local snapshot and preserves framing.
A failure stops watching and retains the previous image. The viewer does not
start a camera or fetch a network image. Captured dragging ends on release, input loss or navigation. Movement received
during rendering updates one queued desired view.

Reads compare file identity, length and version before accepting complete bytes.
A queued read retains the application until cancellation completes. Decode and
render turns process at most 256 pixels. PNG decoding processes at most 256
chunk bytes, compressed symbols, filter bytes, pixels or bounded state
transitions per turn. Parsing validates chunks before workspace allocation. Published frames are immutable and
remain owned while shell painting retains them. Repainting copies the clipped
rows of the cached image. Failed opens, allocation refusals and cancellation
preserve the previous frame. Closing retires pending input before disposal.

Home shows twelve applications per page. Applications shows ten, with Previous
and Next controls and Page Up/Page Down keys. Number keys select the current
Applications page; 0 selects its tenth entry. The Image icon uses the attributed
native HVIF artwork. Existing application sessions remain open across pages.

The LCD composition reserves 11530240 bytes of UI PSRAM, including shell pixels,
application state and allocator records. A maximum encoded/decoded image
replacement with eight Editor documents and 24 Notes reaches 10913959 bytes in
the host firmware allocator fixture. Physical parser/render timing, touch,
readability, storage endurance and dependency adoption remain qualification gates.
