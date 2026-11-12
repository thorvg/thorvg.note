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
        function restore(done) {
            try {
                fs.mkdirTree(root);
                fs.mount(module.IDBFS, {}, root);
                fs.syncfs(true, function (error) {
                    if (error) return done(error);
                    try {
                        fs.mkdirTree(root + '/notes');
                        fs.mkdirTree(root + '/settings');
                        fs.symlink(root + '/notes', '/notes');
                        fs.symlink(root + '/settings', '/settings');
                        if (!exists('/settings/settings.conf')) fs.writeFile('/settings/settings.conf', settings);
                        notes.forEach(function (note) {
                            var path = '/notes/' + note.name;
                            if (!exists(path)) fs.writeFile(path, note.text);
                        });
                        if (exists('/settings/session/session.conf')) module.arguments.pop();
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
                        if (file.size > 16 * 1024 * 1024) throw new Error('File is too large: ' + file.name);
                        if (!file.name || file.name === '.' || file.name === '..' || /[\/\\\x00-\x1f\x7f]/.test(file.name)) throw new Error('Invalid filename');
                        return file.arrayBuffer();
                    }));
                    var paths = files.map(function (file, index) {
                        var dot = file.name.lastIndexOf('.');
                        var stem = dot > 0 ? file.name.slice(0, dot) : file.name;
                        var extension = dot > 0 ? file.name.slice(dot) : '';
                        var path = root + '/notes/' + file.name;
                        for (var number = 2; exists(path); ++number) path = root + '/notes/' + stem + ' (' + number + ')' + extension;
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
