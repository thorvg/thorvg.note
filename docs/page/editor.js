(function () {
    var stage = document.getElementById('stage');
    var canvas = document.getElementById('canvas');
    var keyboard = document.getElementById('keyboard');
    var status = document.getElementById('status');
    var options = new URLSearchParams(window.location.search);
    var theme = options.get('theme');
    var layout = (options.get('layout') || stage.dataset.layout) === 'note' ? 'note' : 'memo';
    if (theme === 'light' || theme === 'dark') document.documentElement.dataset.theme = theme;
    var samples = ['1. Welcome.md', '2. Markdown.md', '3. Multilingual.md', '4. Layout.md', '5. Theme.md', '6. Keyboard.md', '7. README.md'];
    function originalName(name) { return name.replace(/^\d+\. /, ''); }
    var sample = options.get('sample') || stage.dataset.sample || 'Welcome.md';
    sample = samples.find(function (name) { return name === sample || originalName(name) === sample; }) || samples[0];
    // Shown alone when the sample notes cannot be fetched, as when the page is opened from disk.
    var spare = '# 👋 Welcome to thor.note\n\nYou are typing into the real editor.\nClick here and type. F1 opens the command palette.\n\nServe this folder over HTTP to load the sample notes.\n';

    function fail(message) {
        stage.dataset.state = 'failed';
        status.textContent = message;
    }

    function fetchNotes() {
        return Promise.all(samples.map(function (name) {
            return fetch('notes/' + encodeURIComponent(name)).then(function (response) {
                if (!response.ok) throw new Error(name);
                return response.text();
            }).then(function (text) { return {name: name, previous: originalName(name), text: text}; }).catch(function () {
                return name === samples[0] ? {name: name, previous: originalName(name), text: spare} : null;
            });
        })).then(function (notes) {
            return notes.filter(function (note) { return note !== null; });
        });
    }

    function fetchFonts(names) {
        return Promise.all(names.map(function (name) {
            return fetch('fonts/' + name).then(function (response) {
                if (!response.ok) throw new Error(name);
                return response.arrayBuffer();
            }).then(function (bytes) { return {name: name, bytes: new Uint8Array(bytes)}; });
        }));
    }

    function installFonts(fonts) {
        fonts.forEach(function (font) {
            Module.FS.writeFile('/assets/' + font.name, font.bytes, {canOwn: true});
        });
    }

    function scrollPage(x, y) {
        if (window.parent !== window) window.parent.scrollBy({left: x, top: y, behavior: 'instant'});
    }

    function start(notes) {
        var appArguments = ['--settings-dir', '/settings'];
        if (!notes.some(function (note) { return note.name === sample; })) {
            fail('The document could not be loaded: ' + sample);
            return;
        }
        appArguments.push('/notes/' + sample);
        var storage;
        var hanRequest;
        var fontData = fetchFonts(['Pretendard-Regular.ttf', 'Pretendard-Bold.ttf', 'DejaVuSansMono.ttf']).catch(function () {
            fail('The app fonts could not be loaded');
            return null;
        });
        window.Module = {
            canvas: canvas,
            onUnhandledWheel: scrollPage,
            onMissingHan: function () {
                if (hanRequest) return;
                hanRequest = fetchFonts(['NotoSansSC-Regular.ttf', 'NotoSansSC-Bold.ttf']).then(function (fonts) {
                    installFonts(fonts);
                    try {
                        if (!Module._thornoteLoadHanFonts()) throw new Error('Invalid Han fallback fonts');
                    } finally {
                        fonts.forEach(function (font) { Module.FS.unlink('/assets/' + font.name); });
                    }
                }).catch(function (error) {
                    console.warn('Chinese fonts could not be loaded', error);
                    hanRequest = null;
                });
            },
            arguments: appArguments,
            print: function () {},
            printErr: function () {},
            preRun: [function () {
                // Keys reach the app only while its text field has focus.
                Module.ENV.SDL_EMSCRIPTEN_KEYBOARD_ELEMENT = '#keyboard';
                var settings = 'thornote-settings 1\ncomposition ' + (layout === 'note' ? '2' : '0') + '\n' +
                    (theme === 'light' || theme === 'dark' ? 'dark ' + (theme === 'dark' ? '1' : '0') + '\n' : '') +
                    (originalName(sample) === 'README.md' || originalName(sample) === 'Markdown.md' ? 'layout 0 0 0 1 224 236 216\n' : '') +
                    'font-size 14\n' + notes.map(function (note) {
                        return 'recent-file "/notes/' + note.name + '"\n';
                    }).join('') + 'recent-directory "/notes"\n';
                storage = window.ThorNoteStorage(Module, notes, settings, originalName(sample), theme, layout);
                Module.onSyncStorage = storage.sync;
                Module.onSaveAs = storage.saveAs;
                Module.onOpenFiles = storage.openFiles;
                Module.onExport = storage.exportFile;
                Module.addRunDependency('storage');
                storage.restore(function (error) {
                    if (error) {
                        fail('Browser storage could not be opened. Reload to retry.');
                        return;
                    }
                    Module.removeRunDependency('storage');
                });
                Module.addRunDependency('fonts');
                fontData.then(function (fonts) {
                    if (!fonts) return;
                    try {
                        Module.FS.mkdir('/assets');
                        installFonts(fonts);
                    } catch (error) {
                        fail('The app fonts could not be loaded');
                        return;
                    }
                    Module.removeRunDependency('fonts');
                });
            }],
            onRuntimeInitialized: function () { storage.prepare(); },
            postRun: [function () {
                if (stage.dataset.state === 'failed') return;
                // The window starts at its default size; a resize makes it adopt the canvas box.
                window.dispatchEvent(new Event('resize'));
                storage.sync(function (error) {
                    if (error) return fail('Browser storage could not be initialized. Reload to retry.');
                    if (stage.dataset.state === 'failed') return;
                    stage.dataset.state = 'running';
                    status.textContent = '';
                });
            }],
            onAbort: function (reason) { fail(typeof reason === 'string' ? reason : 'The app stopped unexpectedly'); }
        };
        var script = document.createElement('script');
        // Threads share memory between workers, which only a cross-origin isolated page may do.
        script.src = window.crossOriginIsolated ? 'ThorNote.threads.js' : 'ThorNote.js';
        script.onerror = function () { fail('The app could not be loaded'); };
        document.body.appendChild(script);
    }

    canvas.addEventListener('webglcontextlost', function () {
        fail('The graphics context was lost. Reload to restart the editor.');
    });
    var composing = false;
    stage.addEventListener('contextmenu', function (event) { event.preventDefault(); });
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
    // Until the app is focused the wheel keeps scrolling the page.
    stage.addEventListener('wheel', function (event) {
        if (document.activeElement !== keyboard) event.stopPropagation();
    }, {capture: true});

    // The service worker isolates the page from the next load on, so a page it does not control yet reloads once under
    // it. A page it controls without isolation, as in a browser without the headers, keeps the single-thread build.
    function isolate(done) {
        var settled = false;
        function settle() {
            if (settled) return;
            settled = true;
            done();
        }
        try {
            if (window.crossOriginIsolated || window.parent !== window || !navigator.serviceWorker || navigator.serviceWorker.controller || sessionStorage.getItem('thornote-isolation')) return done();
            // Reloads once per tab, even when the worker never takes the page over.
            sessionStorage.setItem('thornote-isolation', '1');
        } catch (error) { return done(); }
        // A registration that does not finish soon leaves this load to the single-thread build.
        setTimeout(settle, 3000);
        navigator.serviceWorker.register('isolation.js').then(function () {
            return navigator.serviceWorker.ready;
        }).then(function () {
            if (settled) return;
            settled = true;
            window.location.reload();
        }, settle);
    }

    if (typeof WebAssembly !== 'object') fail('This browser cannot run WebAssembly');
    else isolate(function () { fetchNotes().then(start); });
})();
