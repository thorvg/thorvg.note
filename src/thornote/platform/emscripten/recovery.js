// Recovery records keep the native format; large validation and hashing run outside the input thread.
addToLibrary({
    $thornoteRecovery: {
        worker: null,
        pending: new Map(),
        failed: false,
    },

    thornoteRecoveryStart__deps: ['$thornoteRecovery', '$UTF8ToString', '$FS', '$stringToUTF8OnStack', '$stackSave', '$stackRestore', 'thornoteRecoveryPrepared', 'thornoteRecoveryCommitted', 'thornoteRecoveryDone'],
    thornoteRecoveryStart: function (callback, directory) {
        if (thornoteRecovery.failed || typeof Worker === 'undefined' || !globalThis['crypto']?.['subtle']) return false;
        if (!thornoteRecovery.worker) {
            try { thornoteRecovery.worker = new Worker(locateFile('ThorNote.recovery.js')); }
            catch (error) { thornoteRecovery.failed = true; return false; }
            var finish = function (callback, error) {
                thornoteRecovery.pending.delete(callback);
                var stack = stackSave();
                try { _thornoteRecoveryDone(callback, error ? stringToUTF8OnStack(String(error)) : 0); }
                finally { stackRestore(stack); }
            };
            thornoteRecovery.worker.onmessage = function (event) {
                var result = event['data'];
                var callback = result['callback'];
                var pending = thornoteRecovery.pending.get(callback);
                if (!pending) return;
                var committed = false;
                try {
                    if (result['error']) throw new Error(result['error']);
                    if (!_thornoteRecoveryPrepared(callback)) throw new Error('Recovery baseline changed');
                    FS.mkdirTree(pending.directory + '/buffers');
                    for (var record of result['records']) {
                        FS.writeFile(pending.directory + '/buffers/' + record['id'] + '.recovery', record['bytes'], {'canOwn': true, 'mode': 384});
                    }
                    if (!_thornoteRecoveryCommitted(callback)) throw new Error('Recovery manifest could not be committed');
                    committed = true;
                    for (var id of pending.obsolete) {
                        try { FS.unlink(pending.directory + '/buffers/' + id + '.recovery'); } catch (_) {}
                    }
                    finish(callback, '');
                } catch (error) {
                    if (!committed && result['records']) {
                        for (var record of result['records']) {
                            try { FS.unlink(pending.directory + '/buffers/' + record['id'] + '.recovery'); } catch (_) {}
                        }
                    }
                    finish(callback, error);
                }
            };
            thornoteRecovery.worker.onerror = function (event) {
                thornoteRecovery.failed = true;
                thornoteRecovery.worker.terminate();
                thornoteRecovery.worker = null;
                for (var callback of Array.from(thornoteRecovery.pending.keys())) finish(callback, event.message || 'Recovery worker failed');
            };
        }
        thornoteRecovery.pending.set(callback, {directory: UTF8ToString(directory), records: [], obsolete: []});
        return true;
    },

    thornoteRecoveryAdd__deps: ['$thornoteRecovery', '$UTF8ToString', '$stringToNewUTF8'],
    thornoteRecoveryAdd: function (callback, revision, title, path, ending, bom, text, length) {
        // Wasm32 pointers above 2 GiB arrive as signed JavaScript arguments.
        text >>>= 0;
        length >>>= 0;
        var random = globalThis['crypto'].getRandomValues(new Uint8Array(16));
        var id = Array.from(random, function (byte) { return byte.toString(16).padStart(2, '0'); }).join('');
        thornoteRecovery.pending.get(callback).records.push({'id': id, 'revision': UTF8ToString(revision), 'title': UTF8ToString(title), 'path': UTF8ToString(path), 'ending': ending, 'bom': bom, 'text': HEAPU8.slice(text, text + length)});
        return stringToNewUTF8(id);
    },

    thornoteRecoverySubmit__deps: ['$thornoteRecovery', '$UTF8ToString'],
    thornoteRecoverySubmit: function (callback, obsolete) {
        var pending = thornoteRecovery.pending.get(callback);
        pending.obsolete = UTF8ToString(obsolete).split('\n').filter(Boolean);
        thornoteRecovery.worker.postMessage({'callback': callback, 'records': pending.records}, pending.records.map(function (record) { return record['text'].buffer; }));
        pending.records = [];
    },
});
