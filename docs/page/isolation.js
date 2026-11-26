// Threads need a cross-origin isolated page, but a static host cannot send the headers for that.
// This worker adds them to the editor pages and to the workers those pages start.
self.addEventListener('install', function () { self.skipWaiting(); });
self.addEventListener('fetch', function (event) {
    var request = event.request;
    var url = new URL(request.url);
    var editor = request.destination === 'document' && /\/(editor|note)(\.html)?$/.test(url.pathname);
    if (url.origin !== self.location.origin || !(editor || request.destination === 'worker')) return;
    event.respondWith(fetch(request).then(function (response) {
        // A redirect has no headers to extend; the browser follows it and asks again.
        if (!response.status) return response;
        var headers = new Headers(response.headers);
        headers.set('Cross-Origin-Opener-Policy', 'same-origin');
        headers.set('Cross-Origin-Embedder-Policy', 'require-corp');
        return new Response(response.body, {status: response.status, statusText: response.statusText, headers: headers});
    }));
});
