// Serialize recovery bytes without blocking editor input.
var encode = new TextEncoder();
var magic = 'thornote-recovery 1\n';
var hex = function (bytes) { return Array.from(bytes, function (byte) { return byte.toString(16).padStart(2, '0'); }).join(''); };
self.onmessage = async function (event) {
    var request = event['data'];
    try {
        var records = [];
        for (var doc of request['records']) {
            var title = encode.encode(doc['title']);
            var path = encode.encode(doc['path']);
            var header = encode.encode(doc['id'] + '\n' + doc['revision'] + '\n' + Date.now() + '\n' + doc['ending'] + '\n' + (doc['bom'] ? '1\n' : '0\n') + hex(title) + '\n' + hex(path) + '\n' + doc['text'].byteLength + '\n');
            if (header.byteLength + magic.length + 65 > 65536) throw new Error('Recovery metadata exceeds limit');
            var prefixLength = magic.length + 65;
            var bytes = new Uint8Array(prefixLength + header.byteLength + doc['text'].byteLength);
            bytes.set(header, prefixLength);
            bytes.set(doc['text'], prefixLength + header.byteLength);
            doc['text'] = null;
            var digest = hex(new Uint8Array(await self['crypto']['subtle'].digest('SHA-256', bytes.subarray(prefixLength))));
            bytes.set(encode.encode(magic + digest + '\n'));
            records.push({'id': doc['id'], 'bytes': bytes});
        }
        self.postMessage({'callback': request['callback'], 'records': records}, records.map(function (record) { return record['bytes'].buffer; }));
    } catch (error) {
        self.postMessage({'callback': request['callback'], 'error': String(error)});
    }
};
