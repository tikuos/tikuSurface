# Application artwork

These Apache-2.0 HVIF assets contain TikuDesktop-authored artwork from revision
`6622c2a49225af5fba3e928e46e70069d86c6491`. Authors: Ambuj Varshney
<ambuj@tiku-os.org>. The source paths, blob hashes and raster hashes are recorded
in [provenance.json](provenance.json).

| Application | Artwork |
|------------|---------|
| Word calculator | `app_devcalc.hvif` |
| Media browser | `compare_folders.hvif` |
| Device information | `section_identity.hvif` |

The media browser uses the Compare Folders symbol for file navigation.

`python3 tools/build_icons.py` converts the assets to the checked-in
`shell/icons.h` using the pinned Stomme HVIF renderer. Add `--check` to compare
the generated header without writing it. This offline command requires a host
C compiler. Runtime builds consume the 27,648 bytes of frozen pixels and do not
link the HVIF parser or rasterizer. Pixels are premultiplied ARGB8888 at 48 by 48.

The expanded application grid uses twelve 32-pixel icons, with source and raster
hashes in [provenance-grid.json](provenance-grid.json). Calculator, Editor,
Terminal, Device Monitor, VFS Inspector, Reader, TikuHex, TikuDiff and Notes use their TikuDesktop application artwork from the
same revision and license. `python3 tools/build_icons.py --grid --check` checks
`shell/icons_grid.h`. Its 49,152 frozen pixel bytes replace the three-icon raster
in device-enabled compositions. The runtime still uses no vector decoder.
