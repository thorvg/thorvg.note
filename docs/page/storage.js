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
        return {restore: restore, prepare: prepare, sync: sync, saveAs: saveAs, exportFile: exportFile};
    };
})();
