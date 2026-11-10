# Text

## Responsibility

- Manage UTF-8 document content, logical lines, revisions, selections, and edit history.
- Share one document between views while keeping each view's selections independent.
- Expose all public types through [`text.h`](text.h) in namespace `tn`.
- Require C++17 and the standard library; no UI or ThorVG dependency.

## Ownership

- The document must outlive every session and UI display that borrows it.
- `session.document()` provides const inspection; edits go through the session API.
- `EditSession` owns selections and editing state; use `readOnly(true)` for selection-only views.
- Copied sessions share the document with independent selections and undo-selection records.
- References to text and selections are borrowed. Mutations can invalidate them; retain copies when needed across edits.
- Views and autosave subscribe to document changes through `subscribe()`; the host handles redraw and IO.
- Unsubscribe before destroying captured state. Document copies do not copy subscriptions; assignment retains destination subscriptions.

## Current Limits

- Contiguous string storage; edits can move subsequent bytes and line offsets.
- Codepoint navigation, not grapheme-cluster navigation. Word movement uses character categories rather than linguistic segmentation.
- Logical lines only; soft wrapping and visual cursor movement require UI layout.
- No rich-text style spans, syntax parsing, shaping, font fallback, or rendering.
- History and splice records grow with editing; whole-document replacement clears them.
