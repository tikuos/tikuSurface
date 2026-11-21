# Hex keyboard input

The expanded desktop candidate requires the Stomme `hex-key-input` capability
when `SURFACE_HEX=1`. The native and portable Hex applications share byte-grid
movement and editing. The candidate uses 128 bytes per page.

TikuHex opens in keyboard navigation mode. Choose **Keyboard** and select
hexadecimal or ASCII editing to type bytes. Hexadecimal editing stages the first
digit; the second replaces one byte as one undo transaction. Navigation, focus
loss, switching applications and application commands clear a staged digit.
ASCII editing admits one printable ASCII byte. UTF-8 sequences are refused.
Touch byte editing remains available in every keyboard mode.

Arrow keys move by a byte or a 16-byte row. Home, End, Page Up and Page Down move
within the document. Command shortcuts provide Open (`O`), Save (`S`), Save as
(`Shift+S`), Find (`F`), Go to (`G`), Copy byte (`C`), Undo (`Z`) and Redo
(`Shift+Z` or `Y`). The host preview uses Control as Command. Save and load jobs
retain their document ownership and refuse keyboard edits while busy. Resource
permissions govern saved changes.

The host adapter emits toolkit logical keys. Backspace, Return and Tab have
distinct key identities from Page Down and other navigation keys. Product
applications and fields consume those identities. Hexadecimal document size,
history, persistence and dirty-close limits remain those of the desktop
candidate. Physical keyboard transport and panel/touch acceptance require their
own qualification.
