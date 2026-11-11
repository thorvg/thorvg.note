(function () {
    var section = document.getElementById('motion-icons');
    var reduced = window.matchMedia('(prefers-reduced-motion: reduce)');
    var pending = new Set(), entries = [], request = 0, engine;

    function runtime() {
        if (!engine) engine = import('./vendor/thorvg-webcanvas-1.1.3/webcanvas.esm.js').then(function (module) {
            return module.default.init({renderer: 'sw', locateFile: function () {
                return new URL('vendor/thorvg-webcanvas-1.1.3/thorvg.wasm', document.baseURI).href;
            }});
        });
        return engine;
    }

    function mix(from, to, progress) {
        if (typeof from === 'number') return from + (to - from) * progress;
        if (Array.isArray(from)) return from.map(function (value, index) { return mix(value, to[index], progress); });
        if (from && typeof from === 'object') {
            var value = {};
            Object.keys(from).forEach(function (key) { value[key] = mix(from[key], to[key], progress); });
            return value;
        }
        return to;
    }

    function sample(entry, now) {
        var motion = entry.motion;
        if (!motion) return entry.current;
        var progress = Math.min(1, Math.max(0, (now - motion.start) / motion.duration));
        var from = motion.from;
        if (motion.peak) {
            if (progress < 0.55) return mix(from, motion.peak, 1 - Math.pow(1 - progress / 0.55, 3));
            from = motion.peak;
            progress = (progress - 0.55) / 0.45;
        }
        return mix(from, motion.to, progress * progress * (3 - 2 * progress));
    }

    function render(entry) { entry.canvas.update().render(); }

    function settle(entry) {
        if (!entry.motion) return;
        entry.animation.frame(entry.motion.duration * 60 / 1000);
        entry.current = entry.motion.to;
        entry.motion = null;
        pending.delete(entry);
        entry.card.dataset.animating = 'false';
        render(entry);
    }

    function tick(now) {
        request = 0;
        pending.forEach(function (entry) {
            var motion = entry.motion;
            if (now >= motion.start + motion.duration) settle(entry);
            else { entry.animation.frame((now - motion.start) * 60 / 1000); render(entry); }
        });
        if (pending.size) request = requestAnimationFrame(tick);
    }

    function update(entry) {
        if (!entry.animation) return;
        var state = entry.card.disabled ? 'disabled' : entry.pressed ? 'pressed' : entry.active ? 'active'
            : entry.hovered ? 'hover' : entry.focused ? 'focused' : 'idle';
        if (state === entry.state) return;
        var now = performance.now(), from = sample(entry, now), to = entry.poses[state];
        var peak = entry.poses[state + '-overshoot'];
        var duration = peak ? (state === 'hover' ? 280 : state === 'active' ? 320 : 240)
            : state === 'pressed' ? 120 : state === 'idle' ? 200 : 160;
        var moving = !reduced.matches && entry.visible && !document.hidden && entry.state !== null;
        var slots = {'icon-ink': {p: {a: 0, k: [43 / 255, 47 / 255, 54 / 255]}}};
        to.forEach(function (value, index) {
            var property = {a: 0, k: value.length === 1 ? value[0] : value};
            if (moving) {
                var keys = [{t: 0, s: from[index], o: {x: 1 / 3, y: peak ? 1 : 0}, i: {x: 2 / 3, y: 1}}];
                if (peak) keys.push({t: duration * 0.55 * 60 / 1000, s: peak[index], o: {x: 1 / 3, y: 0}, i: {x: 2 / 3, y: 1}});
                keys.push({t: duration * 60 / 1000, s: value});
                property = {a: 1, k: keys};
            }
            slots['motion-' + index] = {p: property};
        });
        if (entry.slot) entry.animation.del(entry.slot);
        entry.slot = entry.animation.gen(slots);
        entry.animation.apply(entry.slot).frame(0);
        entry.state = state;
        entry.card.dataset.state = state;
        entry.card.dataset.animating = String(moving);
        entry.current = to;
        entry.motion = moving ? {from: from, to: to, peak: peak, start: now, duration: duration} : null;
        render(entry);
        if (moving) { pending.add(entry); if (!request) request = requestAnimationFrame(tick); }
        else pending.delete(entry);
    }

    async function load(entry) {
        try {
            var results = await Promise.all([runtime(), fetch(entry.element.dataset.icon).then(function (response) {
                if (!response.ok) throw new Error('Icon could not be loaded');
                return response.json();
            })]);
            var TVG = results[0], data = results[1], tracks = [];
            // Slots let interrupted transitions start from their current geometry.
            function track(value) {
                if (!value || typeof value !== 'object') return;
                if (value.a === 1 && Array.isArray(value.k)) {
                    value.sid = 'motion-' + tracks.length;
                    tracks.push(value.k);
                } else Object.values(value).forEach(track);
            }
            track(data);
            entry.poses = {};
            data.markers.forEach(function (marker) {
                entry.poses[marker.cm] = tracks.map(function (keys) {
                    var key = keys[0];
                    keys.forEach(function (candidate) { if (candidate.t <= marker.tm) key = candidate; });
                    return key.s;
                });
            });
            entry.current = entry.poses.idle;
            entry.animation = new TVG.LottieAnimation().load(JSON.stringify(data));
            entry.animation.picture.size(48, 48).translate(8, 8);
            entry.canvas = new TVG.Canvas(entry.element, {width: 64, height: 64});
            entry.canvas.add(entry.animation.picture);
            update(entry);
            entry.card.dataset.loaded = 'true';
        } catch (error) {
            entry.card.dataset.loaded = 'failed';
            entry.card.disabled = true;
            entry.card.title = 'This icon could not be loaded. Reload to try again.';
            section.querySelector('.motion-hint').textContent = 'Some icons could not be loaded.';
        }
    }

    section.querySelectorAll('.motion-icon').forEach(function (card) {
        var entry = {card: card, element: card.querySelector('canvas'), state: null, visible: false,
            hovered: false, pressed: false, active: false, focused: false, loading: false};
        entries.push(entry);
        card.addEventListener('pointerenter', function (event) { entry.hovered = event.pointerType !== 'touch'; update(entry); });
        card.addEventListener('pointerleave', function () { entry.hovered = false; update(entry); });
        card.addEventListener('pointerdown', function (event) {
            if (event.button !== 0) return;
            card.setPointerCapture(event.pointerId);
            entry.pressed = true;
            update(entry);
        });
        function release() { entry.pressed = false; update(entry); }
        card.addEventListener('pointerup', release);
        card.addEventListener('pointercancel', release);
        card.addEventListener('lostpointercapture', release);
        card.addEventListener('focus', function () { entry.focused = card.matches(':focus-visible'); update(entry); });
        card.addEventListener('blur', function () { entry.focused = entry.pressed = false; update(entry); });
        card.addEventListener('keydown', function (event) {
            if (event.key === ' ' || event.key === 'Enter') { entry.pressed = true; update(entry); }
        });
        card.addEventListener('keyup', release);
        card.addEventListener('click', function () {
            entry.active = !entry.active;
            card.setAttribute('aria-pressed', String(entry.active));
            update(entry);
        });
    });

    if ('IntersectionObserver' in window) {
        var observer = new IntersectionObserver(function (changes) {
            changes.forEach(function (change) {
                var entry = entries.find(function (item) { return item.card === change.target; });
                entry.visible = change.isIntersecting;
                if (!entry.visible) settle(entry);
                else if (!entry.loading) { entry.loading = true; load(entry); }
            });
        });
        entries.forEach(function (entry) { observer.observe(entry.card); });
    } else entries.forEach(function (entry) { entry.visible = entry.loading = true; load(entry); });

    reduced.addEventListener('change', function () { if (reduced.matches) entries.forEach(settle); });
    document.addEventListener('visibilitychange', function () { if (document.hidden) entries.forEach(settle); });
    window.addEventListener('resize', function () {
        entries.forEach(function (entry) { if (entry.canvas) { entry.canvas.resize(64, 64); render(entry); } });
    });
})();
