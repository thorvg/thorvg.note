# Rendering

1. The app describes a frame: panels, lists, runs of text.
2. Each piece becomes a ThorVG paint in one retained scene.
3. Paints that did not change are kept exactly as they are.
4. Only the dirty regions are rasterized and presented.

Type one letter and a few dozen pixels are redrawn, not the window.

The same code runs on the desktop. There it talks to the system
for fonts, text input and window chrome; here it talks to a canvas.
