// Forwards what the browser composes, inserts, copies and pastes in a text field to the app.
addToLibrary({
    $thornoteTextField: null,

    thornoteTextInputAttach__deps: ['$thornoteTextField', '$UTF8ToString', '$stringToUTF8OnStack', '$stackSave', '$stackRestore', 'thornoteTextEdit', 'thornoteTextCommit', 'thornoteClipboardCopy', 'thornoteClipboardPaste'],
    thornoteTextInputAttach: function (selector) {
        var field = selector ? document.querySelector(UTF8ToString(selector)) : null;
        if (!(field instanceof HTMLTextAreaElement) && !(field instanceof HTMLInputElement)) return false;
        var send = function (target, text, cursor) {
            var stack = stackSave();
            target(stringToUTF8OnStack(text), cursor);
            stackRestore(stack);
        };
        var composed = function (event) { return event.isComposing || event.keyCode === 229; };
        var apple = /Mac|iPhone|iPad/.test(navigator.platform);
        var clipboard = function (event) {
            return (apple ? event.metaKey : event.ctrlKey) && !event.altKey && event.key.length === 1 && 'cxv'.indexOf(event.key.toLowerCase()) >= 0;
        };
        // Keys the input method consumes stay with it; the app receives the text they produce instead.
        field.addEventListener('keydown', function (event) {
            // Copy, cut and paste are left to the browser too: it answers with the clipboard events below.
            if (composed(event) || clipboard(event)) event.stopImmediatePropagation();
            // A plain Enter is a key for the app, not a line break in the field.
            else if (event.key === 'Enter') event.preventDefault();
        }, true);
        field.addEventListener('keyup', function (event) {
            if (composed(event)) event.stopImmediatePropagation();
        }, true);
        // Text is taken from the input events below, never from key presses.
        field.addEventListener('keypress', function (event) { event.stopImmediatePropagation(); }, true);
        field.addEventListener('compositionupdate', function (event) {
            send(_thornoteTextEdit, event.data, Array.from(event.data).length);
        });
        field.addEventListener('compositionend', function (event) {
            if (event.data) send(_thornoteTextCommit, event.data, 0);
            else send(_thornoteTextEdit, '', 0);
        });
        field.addEventListener('input', function (event) {
            if (event.isComposing) return;
            // A key that ends a composition arrives here as the text it inserts.
            if (event.inputType === 'insertLineBreak') send(_thornoteTextCommit, '\n', 0);
            else if (event.inputType === 'insertText' && event.data) send(_thornoteTextCommit, event.data, 0);
            field.value = '';
        });
        // Without a selection in the field, some browsers only send copy and cut once these are cancelled.
        field.addEventListener('beforecopy', function (event) { event.preventDefault(); });
        field.addEventListener('beforecut', function (event) { event.preventDefault(); });
        var copy = function (event) {
            var text = _thornoteClipboardCopy(event.type === 'cut');
            if (text) event.clipboardData.setData('text/plain', UTF8ToString(text));
            event.preventDefault();
        };
        field.addEventListener('copy', copy);
        field.addEventListener('cut', copy);
        field.addEventListener('paste', function (event) {
            send(_thornoteClipboardPaste, event.clipboardData.getData('text/plain'), 0);
            event.preventDefault();
        });
        thornoteTextField = field;
        return true;
    },

    thornoteTextInputPlace__deps: ['$thornoteTextField'],
    thornoteTextInputPlace: function (x, y, height) {
        // The input method opens its candidates next to the field, so the field follows the caret.
        var canvas = Module['canvas'];
        thornoteTextField.style.left = canvas.offsetLeft + x + 'px';
        thornoteTextField.style.top = canvas.offsetTop + y + 'px';
        thornoteTextField.style.height = height + 'px';
    },
});
