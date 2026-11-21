# Developer Calculator checksums and clipboard

The text-input candidate requires Stomme's `word-checksum-input` capability.
The Developer Calculator Checksums page accepts literal UTF-8 text or hexadecimal
byte pairs. Select the input mode with the button above the field. Each mode
retains its own draft when switching modes or applications.

The input limit is 255 source bytes. Hexadecimal input admits 127 complete byte
pairs; spaces between pairs count toward the input limit. The single-line field
refuses control characters and malformed UTF-8. Use hexadecimal pairs for binary
bytes, including line endings. The checksums cover exactly the admitted bytes,
without an implicit newline or terminator. Empty input is valid.

The variants match TikuDesktop: CRC-32/ISO-HDLC and CRC-16/CCITT-FALSE. **Copy
checksums** copies their names, fixed-width hexadecimal results and byte count.
**Copy** on the Word page copies the labeled hexadecimal, decimal, octal and
binary conversions. Command+C copies the current checksum results on the
Checksums page and conversions on the Word/Bits pages. The host preview uses
Control as Command. An active text field keeps its selection copy behavior.

Touch Erase and physical Backspace both send the toolkit Backspace key. Page
Down remains a navigation key. Field admission, undo, selection and clipboard
paste use Stomme's shared field controller.
