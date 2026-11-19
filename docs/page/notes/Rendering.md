# Rendering

1. The app describes a frame: panels, lists, runs of text.
2. Each piece becomes a ThorVG paint in one retained scene.
3. Paints that did not change are kept exactly as they are.
4. ThorVG draws the scene into the GL framebuffer and presents the frame.

The desktop editor uses OpenGL. The browser editor uses WebGL 2.

The same code runs on the desktop. There it talks to the system
for fonts, text input and window chrome; here it talks to a canvas.
