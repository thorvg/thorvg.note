(function () {
    var stage = document.getElementById('stage');
    var canvas = document.getElementById('canvas');
    var keyboard = document.getElementById('keyboard');
    var status = document.getElementById('status');
    var options = new URLSearchParams(window.location.search);
    var theme = options.get('theme');
    if (theme === 'light' || theme === 'dark') document.documentElement.dataset.theme = theme;
    var samples = ['Welcome.md', 'Rendering.md', 'Keyboard.md', 'Layout.md', 'Multilingual.md'];
    // Command is the app's primary modifier on Apple systems, Ctrl everywhere else.
    var apple = /Mac|iPhone|iPad/.test(navigator.platform);
    // Shown alone when the sample notes cannot be fetched, as when the page is opened from disk.
    var spare = '# Welcome to thor.note\n\nYou are typing into the real editor.\nClick here and type. F1 opens the command palette.\n\nServe this folder over HTTP to load the sample notes.\n';

    function fail(message) {
        stage.dataset.state = 'failed';
        status.textContent = message;
    }

    function fetchNotes() {
        return Promise.all(samples.map(function (name) {
            return fetch('notes/' + encodeURIComponent(name)).then(function (response) {
                if (!response.ok) throw new Error(name);
                return response.text();
            }).then(function (text) { return {name: name, text: text}; });
        })).catch(function () { return [{name: 'Welcome.md', text: spare}]; });
    }

    function fetchFonts() {
        return Promise.all(['Pretendard-Regular.ttf', 'Pretendard-Bold.ttf', 'DejaVuSansMono.ttf'].map(function (name) {
            return fetch('fonts/' + name).then(function (response) {
                if (!response.ok) throw new Error(name);
                return response.arrayBuffer();
            }).then(function (bytes) { return {name: name, bytes: new Uint8Array(bytes)}; });
        }));
    }

    function start(notes) {
        var appArguments = ['--fresh', '--settings-dir', '/settings'];
        var sample = options.get('sample');
        if (samples.indexOf(sample) !== -1) appArguments.push('--layout', 'memo', '/notes/' + sample);
        var fontData = fetchFonts().catch(function () {
            fail('The app fonts could not be loaded');
            return null;
        });
        window.Module = {
            canvas: canvas,
            arguments: appArguments,
            print: function () {},
            printErr: function () {},
            preRun: [function () {
                // Keys reach the app only while its text field has focus.
                Module.ENV.SDL_EMSCRIPTEN_KEYBOARD_ELEMENT = '#keyboard';
                Module.FS.mkdir('/notes');
                notes.forEach(function (note) { Module.FS.writeFile('/notes/' + note.name, note.text); });
                // The app opens on its welcome screen; its recent lists lead to the sample notes.
                Module.FS.mkdir('/settings');
                Module.FS.writeFile('/settings/settings.conf', 'thornote-settings 1\n' +
                    (theme === 'light' || theme === 'dark' ? 'dark ' + (theme === 'dark' ? '1' : '0') + '\n' : '') +
                    (samples.indexOf(sample) !== -1 ? 'font-size 14\n' : '') + notes.map(function (note) {
                    return 'recent-file "/notes/' + note.name + '"\n';
                }).join('') + 'recent-directory "/notes"\n');
                Module.addRunDependency('fonts');
                fontData.then(function (fonts) {
                    if (!fonts) return;
                    try {
                        Module.FS.mkdir('/assets');
                        fonts.forEach(function (font) {
                            Module.FS.writeFile('/assets/' + font.name, font.bytes, {canOwn: true});
                        });
                    } catch (error) {
                        fail('The app fonts could not be loaded');
                        return;
                    }
                    Module.removeRunDependency('fonts');
                });
            }],
            postRun: [function () {
                // The window starts at its default size; a resize makes it adopt the canvas box.
                window.dispatchEvent(new Event('resize'));
                stage.dataset.state = 'running';
                status.textContent = '';
            }],
            onAbort: function () { fail('The app stopped unexpectedly'); }
        };
        var script = document.createElement('script');
        script.src = 'ThorNote.js';
        script.onerror = function () { fail('The app could not be loaded'); };
        document.body.appendChild(script);
    }

    var composing = false;
    keyboard.addEventListener('compositionstart', function () { composing = true; });
    keyboard.addEventListener('compositionend', function () { composing = false; });
    canvas.addEventListener('mousedown', function (event) {
        // Focus stays on the text field; a click also ends the composition in progress.
        event.preventDefault();
        if (composing) keyboard.blur();
        keyboard.focus({preventScroll: true});
    });
    keyboard.addEventListener('focus', function () { stage.dataset.focus = ''; });
    keyboard.addEventListener('blur', function () { delete stage.dataset.focus; });
    // Shortcuts pressed in the app stay there; the browser would act on them as well.
    keyboard.addEventListener('keydown', function (event) {
        if (event.ctrlKey || event.metaKey || /^F\d+$/.test(event.key)) event.preventDefault();
    });
    if (apple) document.querySelectorAll('kbd.mod').forEach(function (key) { key.textContent = '\u2318'; });
    // Until the app is focused the wheel keeps scrolling the page.
    stage.addEventListener('wheel', function (event) {
        if (document.activeElement !== keyboard) event.stopPropagation();
    }, {capture: true});

    if (typeof WebAssembly !== 'object') fail('This browser cannot run WebAssembly');
    else fetchNotes().then(start);
})();
