# UI update
- Theme: repaint all child controls, owner-draw view combo entries.
- New tabs explicitly use no color; menu indicates current no-color state.
- Scroll track: viewport bounds, proportional thumb, no arrows, track clicks.
- Shortcuts: two slots, key capture, Esc clears, OK commits, legacy JSON Key/Key2 support.
- Page Up clamps to index zero.
- Linux core tests passed; Windows UI not compiled or executed here.
- Rebuild all targets cleanly because ShortcutItem layout changed.
