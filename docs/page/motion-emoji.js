(function () {
    var section = document.getElementById('motion-emoji');
    var sheet = section.querySelector('.emoji-sheet'), layer = sheet.querySelector('canvas'), canvas;
    var reduced = window.matchMedia('(prefers-reduced-motion: reduce)');
    var playing = new Set(), entries = [], request = 0, previous = 0, engine;

    function runtime() {
        if (!engine) engine = import('./vendor/thorvg-webcanvas-1.1.3/webcanvas.esm.js').then(function (module) {
            return module.default.init({renderer: 'gl', locateFile: function () {
                return new URL('vendor/thorvg-webcanvas-1.1.3/thorvg.wasm', document.baseURI).href;
            }});
        }).then(function (TVG) {
            canvas = new TVG.Canvas(layer, {width: sheet.clientWidth, height: sheet.clientHeight});
            return TVG;
        });
        return engine;
    }

    function position(entry) {
        var slot = entry.element.getBoundingClientRect(), origin = layer.getBoundingClientRect();
        entry.animation.picture.translate(slot.left - origin.left + 4, slot.top - origin.top + 4);
    }

    function tick(now) {
        request = 0;
        var delta = previous ? (now - previous) / 1000 : 0;
        previous = now;
        var changed = false;
        playing.forEach(function (entry) {
            entry.elapsed = (entry.elapsed + delta) % entry.info.duration;
            // Sample authored frames to avoid gaps at fractional layer out-points.
            var frame = Math.floor(entry.elapsed * entry.info.fps);
            if (frame === entry.frame) return;
            entry.animation.frame(frame);
            entry.frame = frame;
            changed = true;
        });
        if (changed) canvas.update().render();
        if (playing.size) request = requestAnimationFrame(tick);
    }

    function update(entry) {
        entry.card.setAttribute('aria-pressed', String(entry.paused));
        entry.card.setAttribute('aria-label', (entry.paused ? 'Play ' : 'Pause ') + entry.label + ' animation');
        var active = entry.animation && entry.visible && !entry.paused && !document.hidden;
        if (active) playing.add(entry);
        else playing.delete(entry);
        if (playing.size && !request) {
            previous = 0;
            request = requestAnimationFrame(tick);
        } else if (!playing.size && request) {
            cancelAnimationFrame(request);
            request = 0;
        }
    }

    async function load(entry) {
        entry.card.dataset.loaded = 'loading';
        try {
            var url = 'https://fonts.gstatic.com/s/e/notoemoji/latest/' + entry.element.dataset.emoji + '/lottie.json';
            var results = await Promise.all([runtime(), fetch(url).then(function (response) {
                if (!response.ok) throw new Error('Emoji could not be loaded');
                return response.text();
            })]);
            var TVG = results[0];
            entry.animation = new TVG.LottieAnimation().load(results[1]);
            entry.info = entry.animation.info();
            entry.animation.picture.size(56, 56);
            position(entry);
            canvas.add(entry.animation.picture);
            entry.animation.frame(0);
            canvas.update().render();
            entry.card.dataset.loaded = 'true';
            update(entry);
        } catch (error) {
            playing.delete(entry);
            if (canvas && entry.animation) canvas.remove(entry.animation.picture);
            if (entry.animation) entry.animation.dispose();
            entry.animation = null;
            entry.card.dataset.loaded = 'failed';
            entry.card.disabled = true;
            entry.card.title = 'This emoji could not be loaded. Reload to try again.';
            section.querySelector('.motion-hint').textContent = 'Some emoji could not be loaded.';
        }
    }

    section.querySelectorAll('.emoji-card').forEach(function (card) {
        var entry = {card: card, element: card.querySelector('.motion-glyph'), visible: false,
            paused: reduced.matches, elapsed: 0, frame: 0,
            label: card.querySelector('.motion-label').textContent.toLowerCase()};
        entries.push(entry);
        update(entry);
        card.addEventListener('click', function () { entry.paused = !entry.paused; update(entry); });
    });

    if ('IntersectionObserver' in window) {
        var observer = new IntersectionObserver(function (changes) {
            changes.forEach(function (change) {
                var entry = entries.find(function (item) { return item.card === change.target; });
                entry.visible = change.isIntersecting;
                if (entry.visible && !entry.card.dataset.loaded) load(entry);
                update(entry);
            });
        });
        entries.forEach(function (entry) { observer.observe(entry.card); });
    } else entries.forEach(function (entry) { entry.visible = true; load(entry); });

    reduced.addEventListener('change', function () {
        entries.forEach(function (entry) { entry.paused = reduced.matches; update(entry); });
    });
    document.addEventListener('visibilitychange', function () { entries.forEach(update); });
    new ResizeObserver(function () {
        if (!canvas) return;
        canvas.resize(sheet.clientWidth, sheet.clientHeight);
        entries.forEach(function (entry) { if (entry.animation) position(entry); });
        canvas.update().render();
    }).observe(sheet);
})();
