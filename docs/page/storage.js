(function () {
    window.ThorNoteStorage = function (module, notes, settings, sample, theme, layout) {
        var fs = module.FS;
        var root = '/persist/' + encodeURIComponent(location.pathname + '|' + sample + '|' + (theme || 'default') + '|' + layout);
        var callbacks = [];
        var syncing = false;
        function exists(path) { return fs.analyzePath(path).exists; }
        function flush() {
            if (syncing || !callbacks.length) return;
            syncing = true;
            var pending = callbacks.splice(0);
            function finish(error) {
                syncing = false;
                pending.forEach(function (done) { done(error ? String(error) : ''); });
                flush();
            }
            try { fs.syncfs(false, finish); }
            catch (error) { finish(error); }
        }
        function sync(done) {
            callbacks.push(done);
            flush();
        }
        async function installNotes() {
            var config = '/settings/settings.conf';
            var sessionPath = '/settings/session/session.conf';
            var session = exists(sessionPath) ? fs.readFile(sessionPath, {encoding: 'utf8'}) : '';
            var preferences = exists(config) ? fs.readFile(config, {encoding: 'utf8'}) : settings;
            var marker = '/settings/sample-renames.json';
            var renamed = exists(marker) ? JSON.parse(fs.readFile(marker, {encoding: 'utf8'})) : {};
            var changed = false;
            var previousHashes = {
                'Multilingual.md': ['ca7f6d87e23db41ce7e6c06421737cd98787339fa35beb262840b9bc797c1d96', '3bb5a9e5acd77d9dbcc4d3ee591cb96a993204cee6eaf65662d6acc16d7c4e32'],
                'Keyboard.md': ['ac4e06515b4f74c00bb4573d3685bb330c0d51a05d19a1ab991bd7741f030feb', '5afb9729a0d5192185ac10ab57b9b014f248a03724ea09c20bfeb21173ab66f7'],
                'Markdown.md': ['2eb571d754e0edf97b8947c0d470289a64321f245f775d2753ff0d94fb89a7b4']
            };
            async function refreshShippedText(note, path, old) {
                var hashes = previousHashes[note.previous];
                if (!hashes || !crypto.subtle) return;
                var stat = fs.stat(path);
                if (stat.size >= 4096) return;
                var bytes = fs.readFile(path);
                if (new TextDecoder().decode(bytes) === note.text) return;
                var digest = new Uint8Array(await crypto.subtle.digest('SHA-256', bytes));
                var hash = Array.from(digest, function (b) { return b.toString(16).padStart(2, '0'); }).join('');
                if (!hashes.includes(hash)) return;
                var dirty = session.split('\n').some(function (line) {
                    var record = /^document \d+ "[0-9a-f]+" "([^"]+)" "(?:\\.|[^"\\])*" \d+ \d+ \d+ \d+ (\d+) (\d+)/.exec(line);
                    return record && (record[2] === '1' || record[3] === '1') &&
                        [old, root + old, path, root + path].includes(record[1]);
                });
                if (dirty) return;
                fs.writeFile(path, note.text);
                // IDBFS detects changes by mtime. Keep the guide in its reading-order slot.
                var modified = stat.mtime.getTime() + 1;
                fs.utime(path, modified, modified);
            }
            // Install the highest number first. A newest-first listing then follows reading order.
            var installedAt = exists(marker) ? fs.stat(marker).mtime.getTime() : Date.now();
            for (var i = notes.length - 1; i >= 0; --i) {
                var note = notes[i], path = '/notes/' + note.name, old = '/notes/' + note.previous;
                var firstInstall = !Object.hasOwn(renamed, note.previous);
                if (firstInstall) {
                    renamed[note.previous] = null;
                    changed = true;
                }
                if (exists(path)) {
                    if (!firstInstall && note.previous === 'Multilingual.md') await refreshShippedText(note, path, old);
                    continue;
                }
                if (firstInstall && exists(old)) {
                    fs.rename(old, path);
                    renamed[note.previous] = note.name;
                    ['', root].forEach(function (prefix) {
                        preferences = preferences.replaceAll('recent-file "' + prefix + old + '"', 'recent-file "' + prefix + path + '"');
                    });
                    await refreshShippedText(note, path, old);
                    if (fs.stat(path).size !== new TextEncoder().encode(note.text).length ||
                        fs.readFile(path, {encoding: 'utf8'}) !== note.text) continue;
                } else {
                    fs.writeFile(path, note.text);
                }
                var modified = installedAt - (parseInt(note.name, 10) - 1) * 1000;
                fs.utime(path, modified, modified);
            }
            if (changed) {
                fs.writeFile(marker, JSON.stringify(renamed));
                fs.utime(marker, installedAt, installedAt);
            }
            if (!exists(config) || changed) fs.writeFile(config, preferences);
            module.onRestoreFile = function (path) {
                var slash = path.lastIndexOf('/'), folder = path.slice(0, slash);
                if (folder !== '/notes' && folder !== root + '/notes') return;
                var name = renamed[path.slice(slash + 1)];
                if (typeof name === 'string' && !exists(path) && exists(folder + '/' + name)) return folder + '/' + name;
            };
            if (session) module.arguments.pop();
        }
        function restore(done) {
            try {
                fs.mkdirTree(root);
                fs.mount(module.IDBFS, {}, root);
                fs.syncfs(true, async function (error) {
                    if (error) return done(error);
                    try {
                        fs.mkdirTree(root + '/notes');
                        fs.mkdirTree(root + '/settings');
                        fs.symlink(root + '/notes', '/notes');
                        fs.symlink(root + '/settings', '/settings');
                        await installNotes();
                        module.storageRoot = root;
                        sync(done);
                    } catch (failure) { done(failure); }
                });
            } catch (error) { done(error); }
        }
        function prepare() {
            fs.mkdirTree('/settings/themes');
            ['light.lua', 'dark.lua'].forEach(function (name) {
                var path = '/settings/themes/' + name;
                if (!exists(path)) fs.writeFile(path, fs.readFile('/themes/' + name));
            });
        }
        function saveAs(suggested) {
            var name = suggested.slice(suggested.lastIndexOf('/') + 1);
            while (true) {
                name = window.prompt('Save document in this browser', name);
                if (name === null) return null;
                name = name.trim();
                if (!name || name === '.' || name === '..' || /[\/\\\x00-\x1f\x7f]/.test(name)) {
                    window.alert('Enter a filename without path separators.');
                    continue;
                }
                var path = root + '/notes/' + name;
                if (!exists(path) || window.confirm('Replace "' + name + '"?')) return path;
            }
        }
        function openFiles(done) {
            var input = document.createElement('input');
            input.type = 'file';
            input.accept = '.txt,.md';
            input.multiple = true;
            input.hidden = true;
            document.body.appendChild(input);
            input.addEventListener('cancel', function () {
                input.remove();
                done([], '');
            }, {once: true});
            input.addEventListener('change', async function () {
                var files = Array.from(input.files);
                input.remove();
                if (!files.length) return done([], '');
                try {
                    var contents = await Promise.all(files.map(function (file) {
                        if (!/\.(txt|md)$/i.test(file.name)) throw new Error('Unsupported file: ' + file.name);
                        if (file.size > 16 * 1024 * 1024) throw new Error('File is too large: ' + file.name);
                        if (!file.name || file.name === '.' || file.name === '..' || /[\/\\\x00-\x1f\x7f]/.test(file.name)) throw new Error('Invalid filename');
                        return file.arrayBuffer();
                    }));
                    var directory = root + '/notes';
                    fs.mkdirTree(directory);
                    var paths = files.map(function (file, index) {
                        var dot = file.name.lastIndexOf('.');
                        var stem = dot > 0 ? file.name.slice(0, dot) : file.name;
                        var extension = dot > 0 ? file.name.slice(dot) : '';
                        var path = directory + '/' + file.name;
                        for (var number = 2; exists(path); ++number) path = directory + '/' + stem + ' (' + number + ')' + extension;
                        fs.writeFile(path, new Uint8Array(contents[index]));
                        return path;
                    });
                    sync(function (error) { done(error ? [] : paths, error ? 'Could not save imported files: ' + error : ''); });
                } catch (error) { done([], 'Could not import files: ' + error.message); }
            }, {once: true});
            try { input.click(); }
            catch (error) {
                input.remove();
                done([], 'Could not open file picker: ' + error.message);
            }
        }
        async function exportFile(name, bytes) {
            try {
                if (window.showSaveFilePicker) {
                    try {
                        var handle = await window.showSaveFilePicker({suggestedName: name});
                        var writer = await handle.createWritable();
                        await writer.write(bytes);
                        await writer.close();
                        return;
                    } catch (error) {
                        if (error.name === 'AbortError') return;
                        if (error.name !== 'SecurityError' && error.name !== 'NotAllowedError') throw error;
                    }
                }
                var url = URL.createObjectURL(new Blob([bytes], {type: 'text/plain;charset=utf-8'}));
                var link = document.createElement('a');
                link.href = url;
                link.download = name;
                document.body.appendChild(link);
                link.click();
                link.remove();
                setTimeout(function () { URL.revokeObjectURL(url); }, 60000);
            } catch (error) { window.alert('Could not export document: ' + error.message); }
        }
        return {restore: restore, prepare: prepare, sync: sync, saveAs: saveAs, openFiles: openFiles, exportFile: exportFile};
    };
})();
