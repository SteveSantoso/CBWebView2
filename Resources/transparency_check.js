// Generic transparent hit-test bridge for CBWebView2.
(function() {
    var SCRIPT_VERSION = '2026-06-14-throttled-hit-test-2';
    window.__cbwebview2TransparencyCheckInstalled = true;
    window.__cbwebview2TransparencyCheckVersion = SCRIPT_VERSION;

    var PASS_THROUGH_CLASS = 'transparent-pass-through';
    var FORCE_INTERACTIVE_CLASS = 'transparent-force-interactive';
    var PASS_THROUGH_ATTR = 'data-transparent-pass-through';
    var FORCE_INTERACTIVE_ATTR = 'data-transparent-interactive';
    var DEBUG_OVERLAY_ID = 'cbwebview2-transparency-debug';
    var DEBUG_OVERLAY_DEFAULT_ENABLED = true;
    var allowNonInteractiveElementPassthrough = !!window.__cbwebview2TransparencyAllowNonInteractivePassthrough;

    var INTERACTIVE_ROLES = {
        button: true,
        link: true,
        checkbox: true,
        radio: true,
        switch: true,
        tab: true,
        textbox: true,
        searchbox: true,
        slider: true,
        spinbutton: true,
        combobox: true,
        listbox: true,
        option: true,
        menuitem: true,
        menuitemcheckbox: true,
        menuitemradio: true,
        treeitem: true,
        gridcell: true
    };

    var INTERACTIVE_CURSORS = {
        pointer: true,
        text: true,
        move: true,
        grab: true,
        grabbing: true,
        crosshair: true,
        'col-resize': true,
        'row-resize': true,
        'ew-resize': true,
        'ns-resize': true,
        'nesw-resize': true,
        'nwse-resize': true
    };

    // Move-type events (mousemove/wheel/touchmove) are coalesced to at most one evaluation per animation
    // frame and skipped entirely below this movement threshold, because each evaluation forces style
    // recalculation (and possibly canvas readback) in the renderer.
    var MOVE_EVAL_EPSILON_PX = 2;
    // Canvas getImageData forces a GPU sync; on move-type events reuse the last sample within this radius/age.
    var CANVAS_ALPHA_CACHE_RADIUS_PX = 6;
    var CANVAS_ALPHA_CACHE_TTL_MS = 100;
    // Press/click events evaluate immediately and bypass the canvas cache for full accuracy.
    var PRECISE_EVENTS = {
        mousedown: true,
        mouseup: true,
        click: true,
        dblclick: true,
        touchstart: true
    };

    var lastClickable = null;
    var lastPointer = null;
    var lastEvaluatedPoint = null;
    var pendingMovePoint = null;
    var moveEvaluationHandle = null;
    var lastCanvasSample = null;
    var debugOverlayEnabled = !!(window.__cbwebview2TransparencyDebugEnabled === true || DEBUG_OVERLAY_DEFAULT_ENABLED);
    var currentDebugInfo = null;

    function postInternal(payload) {
        try {
            if (window.chrome && window.chrome.webview && window.chrome.webview.postMessage) {
                payload.__cbwebview2 = true;
                window.chrome.webview.postMessage(payload);
            }
        } catch (error) {
        }
    }

    function describeElement(el) {
        if (!el || el.nodeType !== 1) {
            return '<none>';
        }

        var text = String(el.tagName || '').toLowerCase();
        if (el.id) {
            text += '#' + el.id;
        }
        if (el.className && typeof el.className === 'string') {
            text += '.' + el.className.trim().replace(/\s+/g, '.');
        }
        return text;
    }

    function buildDebugResult(clickable, reason, el, extra) {
        var result = {
            clickable: !!clickable,
            reason: reason || '',
            element: describeElement(el)
        };

        if (extra) {
            for (var key in extra) {
                if (Object.prototype.hasOwnProperty.call(extra, key)) {
                    result[key] = extra[key];
                }
            }
        }

        return result;
    }

    function updateDebugOverlay(info) {
        currentDebugInfo = info;
        if (!debugOverlayEnabled || !document.documentElement) {
            return;
        }

        var host = document.getElementById(DEBUG_OVERLAY_ID);
        var overlay = null;
        if (!host) {
            host = document.createElement('div');
            host.id = DEBUG_OVERLAY_ID;
            host.style.cssText = [
                'position:fixed',
                'left:8px',
                'bottom:8px',
                'z-index:2147483647',
                'pointer-events:none',
                'display:block',
                'visibility:visible',
                'opacity:1',
                'width:auto',
                'height:auto',
                'contain:layout style paint'
            ].join(';');

            var root = host.attachShadow ? host.attachShadow({ mode: 'open' }) : host;
            overlay = document.createElement('pre');
            overlay.id = 'panel';
            overlay.style.cssText = [
                'display:block',
                'box-sizing:border-box',
                'max-width:420px',
                'max-height:45vh',
                'overflow:auto',
                'margin:0',
                'padding:6px 8px',
                'border:1px solid rgba(255,255,255,.35)',
                'border-radius:4px',
                'font:12px/1.35 Consolas,Monaco,monospace',
                'color:#fff',
                'background:rgba(0,0,0,.82)',
                'white-space:pre-wrap',
                'text-align:left'
            ].join(';');
            root.appendChild(overlay);
            (document.body || document.documentElement).appendChild(host);
        } else {
            overlay = host.shadowRoot ? host.shadowRoot.getElementById('panel') : host.querySelector('#panel');
            if (!overlay) {
                overlay = host;
            }
        }

        host.style.display = 'block';
        host.style.visibility = 'visible';
        host.style.opacity = '1';
        overlay.textContent = JSON.stringify(info || {}, null, 2);
    }

    function setDebugOverlayEnabled(enabled) {
        debugOverlayEnabled = !!enabled;
        window.__cbwebview2TransparencyDebugEnabled = debugOverlayEnabled;

        var overlay = document.getElementById(DEBUG_OVERLAY_ID);
        if (overlay) {
            overlay.style.display = debugOverlayEnabled ? 'block' : 'none';
        }

        if (debugOverlayEnabled) {
            updateDebugOverlay(currentDebugInfo || buildDebugResult(false, 'debug-enabled', null));
        }
    }

    function refreshDebugOverlay() {
        if (!debugOverlayEnabled) {
            return;
        }

        updateDebugOverlay(currentDebugInfo || buildDebugResult(false, 'debug-enabled', null));
    }

    function scheduleDebugOverlayRefreshes() {
        if (!debugOverlayEnabled) {
            return;
        }

        [0, 50, 250, 1000].forEach(function(delay) {
            setTimeout(refreshDebugOverlay, delay);
        });

        if (document.readyState === 'loading') {
            document.addEventListener('DOMContentLoaded', refreshDebugOverlay, { once: true });
        }

        window.addEventListener('load', refreshDebugOverlay, { once: true });
    }

    function postClickableState(result, force) {
        if (!result || typeof result.clickable !== 'boolean') {
            return;
        }

        updateDebugOverlay(result);
        if (!force && lastClickable === result.clickable) {
            return;
        }

        lastClickable = result.clickable;
        postInternal({
            type: 'hitTest',
            value: result.clickable,
            reason: result.reason || '',
            element: result.element || ''
        });
    }

    function hasClass(el, className) {
        return !!(el && el.classList && el.classList.contains(className));
    }

    function hasBooleanAttribute(el, attr) {
        if (!el || !el.hasAttribute || !el.hasAttribute(attr)) {
            return false;
        }

        var value = String(el.getAttribute(attr) || '').toLowerCase();
        return value === '' || value === '1' || value === 'true' || value === attr;
    }

    function isExplicitPassThrough(el) {
        return hasClass(el, PASS_THROUGH_CLASS) || hasBooleanAttribute(el, PASS_THROUGH_ATTR);
    }

    function isExplicitInteractive(el) {
        return hasClass(el, FORCE_INTERACTIVE_CLASS) || hasBooleanAttribute(el, FORCE_INTERACTIVE_ATTR);
    }

    function isVisibleElement(el, style) {
        if (!el || el.nodeType !== 1) {
            return false;
        }

        style = style || window.getComputedStyle(el);
        if (!style || style.visibility === 'hidden' || style.display === 'none' || style.pointerEvents === 'none') {
            return false;
        }

        var rect = el.getBoundingClientRect();
        return rect.width > 0 && rect.height > 0;
    }

    function cssAlpha(value) {
        value = String(value || '').trim().toLowerCase();
        if (!value || value === 'transparent') {
            return 0;
        }

        var rgbaMatch = value.match(/^rgba?\((.+)\)$/);
        if (rgbaMatch) {
            var parts = rgbaMatch[1].split(/\s*,\s*|\s+/).filter(function(part) {
                return part !== '/' && part !== '';
            });
            if (parts.length >= 4) {
                var alpha = parseFloat(parts[3]);
                if (isFinite(alpha)) {
                    return Math.max(0, Math.min(1, alpha));
                }
            }
            return 1;
        }

        if (value.charAt(0) === '#') {
            if (value.length === 5) {
                return parseInt(value.charAt(4) + value.charAt(4), 16) / 255;
            }
            if (value.length === 9) {
                return parseInt(value.substr(7, 2), 16) / 255;
            }
            return 1;
        }

        return 1;
    }

    function isTransparentColor(value) {
        return cssAlpha(value) <= 0.02;
    }

    function cssLengthIsVisible(value) {
        var number = parseFloat(String(value || '0'));
        return isFinite(number) && number > 0.1;
    }

    function hasVisibleBackground(style) {
        if (!style) {
            return false;
        }

        if (style.backgroundImage && String(style.backgroundImage).toLowerCase() !== 'none') {
            return true;
        }

        return !isTransparentColor(style.backgroundColor);
    }

    function hasVisibleBorderSide(style, side) {
        var borderStyle = String(style['border' + side + 'Style'] || '').toLowerCase();
        if (!borderStyle || borderStyle === 'none' || borderStyle === 'hidden') {
            return false;
        }

        return cssLengthIsVisible(style['border' + side + 'Width']) &&
            !isTransparentColor(style['border' + side + 'Color']);
    }

    function hasVisibleBorder(style) {
        if (!style) {
            return false;
        }

        return hasVisibleBorderSide(style, 'Top') ||
            hasVisibleBorderSide(style, 'Right') ||
            hasVisibleBorderSide(style, 'Bottom') ||
            hasVisibleBorderSide(style, 'Left');
    }

    function hasVisibleOutline(style) {
        if (!style) {
            return false;
        }

        var outlineStyle = String(style.outlineStyle || '').toLowerCase();
        return outlineStyle !== 'none' &&
            outlineStyle !== 'hidden' &&
            cssLengthIsVisible(style.outlineWidth) &&
            !isTransparentColor(style.outlineColor);
    }

    function isVisibleMediaElement(el) {
        var tagName = String(el && el.tagName || '').toUpperCase();
        return ['IMG', 'PICTURE', 'SVG', 'VIDEO', 'IFRAME', 'EMBED', 'OBJECT'].indexOf(tagName) !== -1;
    }

    function getVisiblePaintReason(el, style) {
        if (!el || el.nodeType !== 1 || isExplicitPassThrough(el)) {
            return '';
        }

        style = style || window.getComputedStyle(el);
        if (!style || style.visibility === 'hidden' || style.display === 'none' || style.pointerEvents === 'none') {
            return '';
        }

        if ((el !== document.documentElement && el !== document.body) && !isVisibleElement(el, style)) {
            return '';
        }

        // Painted DOM boxes are solid hit-test regions even when they are not clickable controls.
        if (hasVisibleBackground(style)) {
            return 'background';
        }

        if (hasVisibleBorder(style)) {
            return 'border';
        }

        if (hasVisibleOutline(style)) {
            return 'outline';
        }

        if (style && style.boxShadow && String(style.boxShadow).toLowerCase() !== 'none') {
            return 'box-shadow';
        }

        if (isVisibleMediaElement(el)) {
            return 'media';
        }

        return '';
    }

    function isEditableElement(el) {
        if (!el) {
            return false;
        }

        if (el.isContentEditable) {
            return true;
        }

        var tagName = String(el.tagName || '').toUpperCase();
        if (tagName === 'TEXTAREA') {
            return true;
        }

        if (tagName === 'INPUT') {
            var inputType = String(el.type || 'text').toLowerCase();
            return ['button', 'checkbox', 'radio', 'range', 'reset', 'submit', 'image', 'file', 'color', 'hidden'].indexOf(inputType) === -1;
        }

        return el.getAttribute && String(el.getAttribute('contenteditable') || '').toLowerCase() === 'true';
    }

    function isNativeInteractiveElement(el) {
        if (!el || el.nodeType !== 1) {
            return false;
        }

        var tagName = String(el.tagName || '').toUpperCase();
        if (tagName === 'A' && el.hasAttribute('href')) {
            return true;
        }

        if (['BUTTON', 'SELECT', 'SUMMARY', 'LABEL', 'OPTION'].indexOf(tagName) !== -1) {
            return true;
        }

        if (tagName === 'INPUT') {
            return String(el.type || '').toLowerCase() !== 'hidden';
        }

        return isEditableElement(el);
    }

    function hasInteractiveRole(el) {
        if (!el || !el.getAttribute) {
            return false;
        }

        var role = String(el.getAttribute('role') || '').toLowerCase();
        return !!INTERACTIVE_ROLES[role];
    }

    function hasInteractiveHandlers(el) {
        if (!el) {
            return false;
        }

        if (typeof el.onclick === 'function' || typeof el.onmousedown === 'function' || typeof el.onmouseup === 'function') {
            return true;
        }

        if (el.hasAttribute && (el.hasAttribute('onclick') || el.hasAttribute('onmousedown') || el.hasAttribute('onmouseup'))) {
            return true;
        }

        return false;
    }

    function hasInteractiveCursor(style) {
        return !!(style && INTERACTIVE_CURSORS[String(style.cursor || '').toLowerCase()]);
    }

    function isInteractiveDomElement(el, style) {
        if (!isVisibleElement(el, style) || isExplicitPassThrough(el)) {
            return false;
        }

        return isExplicitInteractive(el) ||
            isNativeInteractiveElement(el) ||
            hasInteractiveRole(el) ||
            hasInteractiveHandlers(el) ||
            hasInteractiveCursor(style) ||
            (el.tabIndex >= 0 && String(el.getAttribute && el.getAttribute('tabindex') || '') !== '-1');
    }

    function stripUrlQueryAndHash(url) {
        return String(url || '').split('#')[0].split('?')[0];
    }

    function isPdfUrl(url) {
        return /\.pdf$/i.test(stripUrlQueryAndHash(url).toLowerCase());
    }

    function isPdfDocument() {
        return isPdfUrl(window.location.href) ||
            String(document.contentType || '').toLowerCase().indexOf('pdf') !== -1;
    }

    function getElementUrlAttribute(el) {
        if (!el || !el.getAttribute) {
            return '';
        }

        return el.getAttribute('src') || el.getAttribute('data') || el.getAttribute('href') || '';
    }

    function isPdfViewerElement(el) {
        if (!el || el.nodeType !== 1) {
            return false;
        }

        var tagName = String(el.tagName || '').toUpperCase();
        if (tagName === 'PDF-VIEWER' || tagName.indexOf('PDF-') === 0) {
            return true;
        }

        if (['EMBED', 'OBJECT', 'IFRAME'].indexOf(tagName) !== -1 && isPdfUrl(getElementUrlAttribute(el))) {
            return true;
        }

        return false;
    }

    function normalizeCustomHitTestResult(result, el) {
        if (typeof result === 'boolean') {
            return buildDebugResult(result, result ? 'custom-hit' : 'custom-pass-through', el);
        }

        if (result && typeof result === 'object' && typeof result.clickable === 'boolean') {
            return buildDebugResult(result.clickable, result.reason || (result.clickable ? 'custom-hit' : 'custom-pass-through'), el, result);
        }

        return null;
    }

    function runCustomHitTest(el, x, y) {
        var node = el;
        while (node && node !== document) {
            if (typeof node.customHitTest === 'function') {
                try {
                    var result = normalizeCustomHitTestResult(node.customHitTest(x, y), node);
                    if (result) {
                        return result;
                    }
                } catch (error) {
                    return buildDebugResult(false, 'custom-hit-test-error', node, { error: String(error && error.message || error) });
                }
            }

            node = node.parentNode;
        }

        if (typeof window.customHitTest === 'function') {
            try {
                return normalizeCustomHitTestResult(window.customHitTest(x, y), window);
            } catch (error) {
                return buildDebugResult(false, 'window-custom-hit-test-error', null, { error: String(error && error.message || error) });
            }
        }

        return null;
    }

    function getCanvasAlphaAtPoint(canvas, x, y) {
        var rect = canvas.getBoundingClientRect();
        if (!rect || rect.width <= 0 || rect.height <= 0 || x < rect.left || y < rect.top || x > rect.right || y > rect.bottom) {
            return null;
        }

        try {
            var context = canvas.getContext && canvas.getContext('2d', { willReadFrequently: true });
            if (!context) {
                return null;
            }

            var canvasX = Math.round((x - rect.left) * (canvas.width / rect.width));
            var canvasY = Math.round((y - rect.top) * (canvas.height / rect.height));
            canvasX = Math.max(0, Math.min(canvas.width - 1, canvasX));
            canvasY = Math.max(0, Math.min(canvas.height - 1, canvasY));

            var startX = Math.max(0, canvasX - 1);
            var startY = Math.max(0, canvasY - 1);
            var width = Math.min(3, canvas.width - startX);
            var height = Math.min(3, canvas.height - startY);
            var pixels = context.getImageData(startX, startY, width, height).data;
            var maxAlpha = 0;
            for (var i = 3; i < pixels.length; i += 4) {
                maxAlpha = Math.max(maxAlpha, pixels[i]);
            }
            return maxAlpha;
        } catch (error) {
            return null;
        }
    }

    function runCanvasPixelHitTest(canvas, x, y, precise) {
        // Move-type evaluations reuse the last readback within a small radius so a slowly moving cursor
        // does not force a GPU sync per frame; press/click events always sample fresh pixels.
        if (!precise && lastCanvasSample && lastCanvasSample.canvas === canvas &&
            (Date.now() - lastCanvasSample.time) <= CANVAS_ALPHA_CACHE_TTL_MS &&
            Math.abs(x - lastCanvasSample.x) <= CANVAS_ALPHA_CACHE_RADIUS_PX &&
            Math.abs(y - lastCanvasSample.y) <= CANVAS_ALPHA_CACHE_RADIUS_PX) {
            var cachedAlpha = lastCanvasSample.alpha;
            return buildDebugResult(cachedAlpha > 8, cachedAlpha > 8 ? 'canvas-pixel-hit' : 'canvas-alpha-pass-through', canvas, { alpha: cachedAlpha, cached: true });
        }

        var alpha = getCanvasAlphaAtPoint(canvas, x, y);
        if (alpha === null) {
            return null;
        }

        lastCanvasSample = { canvas: canvas, x: x, y: y, alpha: alpha, time: Date.now() };
        return buildDebugResult(alpha > 8, alpha > 8 ? 'canvas-pixel-hit' : 'canvas-alpha-pass-through', canvas, { alpha: alpha });
    }

    function runCanvasDescendantHitTest(el, x, y, precise) {
        if (!el || !el.querySelectorAll) {
            return null;
        }

        var canvases = el.querySelectorAll('canvas');
        for (var i = 0; i < canvases.length; i++) {
            var result = runCanvasPixelHitTest(canvases[i], x, y, precise);
            if (result && result.clickable) {
                return result;
            }
        }

        return null;
    }

    function getElementsFromPoint(x, y) {
        if (document.elementsFromPoint) {
            return document.elementsFromPoint(x, y) || [];
        }

        var element = document.elementFromPoint && document.elementFromPoint(x, y);
        return element ? [element] : [];
    }

    function evaluatePoint(x, y, precise) {
        if (precise === undefined) {
            precise = true;
        }

        if (!isFinite(x) || !isFinite(y)) {
            return buildDebugResult(false, 'invalid-point', null);
        }

        var elements = getElementsFromPoint(x, y);
        if (!elements.length) {
            return buildDebugResult(false, 'empty-point', null);
        }

        if (isPdfDocument()) {
            return buildDebugResult(true, 'pdf-document', elements[0]);
        }

        var paintedPassThroughResult = null;
        for (var i = 0; i < elements.length; i++) {
            var el = elements[i];
            if (!el) {
                continue;
            }

            if (isExplicitPassThrough(el)) {
                continue;
            }

            if (isPdfViewerElement(el)) {
                return buildDebugResult(true, 'pdf-viewer', el);
            }

            var customResult = runCustomHitTest(el, x, y);
            if (customResult) {
                return customResult;
            }

            if (String(el.tagName || '').toUpperCase() === 'CANVAS') {
                var canvasResult = runCanvasPixelHitTest(el, x, y, precise);
                if (canvasResult) {
                    return canvasResult;
                }
            }

            var descendantCanvasResult = runCanvasDescendantHitTest(el, x, y, precise);
            if (descendantCanvasResult) {
                return descendantCanvasResult;
            }

            var style = window.getComputedStyle(el);
            if (isInteractiveDomElement(el, style)) {
                return buildDebugResult(true, 'dom-interactive', el, {
                    cursor: style && style.cursor || ''
                });
            }

            var paintReason = getVisiblePaintReason(el, style);
            if (paintReason) {
                if (allowNonInteractiveElementPassthrough) {
                    paintedPassThroughResult = buildDebugResult(false, 'paint-pass-through', el, {
                        paint: paintReason,
                        backgroundColor: style && style.backgroundColor || '',
                        allowNonInteractiveElementPassthrough: true
                    });
                    continue;
                }

                return buildDebugResult(true, 'dom-painted', el, {
                    paint: paintReason,
                    backgroundColor: style && style.backgroundColor || '',
                    allowNonInteractiveElementPassthrough: false
                });
            }

            if (el === document.documentElement || el === document.body) {
                continue;
            }
        }

        return paintedPassThroughResult || buildDebugResult(false, 'pass-through', elements[0]);
    }

    function forceCheck(x, y) {
        if (typeof x !== 'number' || typeof y !== 'number') {
            if (lastPointer) {
                x = lastPointer.x;
                y = lastPointer.y;
            } else {
                x = Math.max(0, Math.min(window.innerWidth - 1, Math.floor(window.innerWidth * 0.5)));
                y = Math.max(0, Math.min(window.innerHeight - 1, Math.floor(window.innerHeight * 0.5)));
            }
        }

        var result = evaluatePoint(x, y, true);
        lastEvaluatedPoint = { x: x, y: y };
        postClickableState(result, true);
        return result;
    }

    function resetLastState() {
        lastClickable = null;
        lastEvaluatedPoint = null;
        lastCanvasSample = null;
        pendingMovePoint = null;
    }

    function setAllowNonInteractiveElementPassthrough(enabled) {
        allowNonInteractiveElementPassthrough = !!enabled;
        window.__cbwebview2TransparencyAllowNonInteractivePassthrough = allowNonInteractiveElementPassthrough;
        resetLastState();
        forceCheck();
    }

    function evaluateAndPost(x, y, precise) {
        lastEvaluatedPoint = { x: x, y: y };
        postClickableState(evaluatePoint(x, y, precise), false);
    }

    function runPendingMoveEvaluation() {
        moveEvaluationHandle = null;
        if (pendingMovePoint) {
            var point = pendingMovePoint;
            pendingMovePoint = null;
            evaluateAndPost(point.x, point.y, false);
        }
    }

    function scheduleMoveEvaluation(x, y) {
        // Stationary or near-stationary pointer: the previous result still stands, skip the evaluation.
        if (lastEvaluatedPoint &&
            Math.abs(x - lastEvaluatedPoint.x) <= MOVE_EVAL_EPSILON_PX &&
            Math.abs(y - lastEvaluatedPoint.y) <= MOVE_EVAL_EPSILON_PX) {
            return;
        }

        // Coalesce to at most one evaluation per animation frame, always using the latest position.
        pendingMovePoint = { x: x, y: y };
        if (moveEvaluationHandle !== null) {
            return;
        }

        // requestAnimationFrame must be invoked as a window method, otherwise Chromium throws
        // "Illegal invocation"; do not detach it into a bare function reference.
        if (window.requestAnimationFrame) {
            moveEvaluationHandle = window.requestAnimationFrame(runPendingMoveEvaluation);
        } else {
            moveEvaluationHandle = window.setTimeout(runPendingMoveEvaluation, 16);
        }
    }

    function handlePointer(event, eventName) {
        if (!event || typeof event.clientX !== 'number') {
            return;
        }

        lastPointer = { x: event.clientX, y: event.clientY };
        if (PRECISE_EVENTS[eventName]) {
            // Press/click accuracy matters: evaluate immediately with fresh canvas pixels and drop any
            // queued move evaluation for an older position.
            pendingMovePoint = null;
            evaluateAndPost(event.clientX, event.clientY, true);
            return;
        }

        scheduleMoveEvaluation(event.clientX, event.clientY);
    }

    ['mousemove', 'mousedown', 'mouseup', 'click', 'dblclick', 'wheel', 'touchstart', 'touchmove'].forEach(function(name) {
        document.addEventListener(name, function(event) {
            var sourceEvent = event;
            if (event.touches && event.touches.length) {
                sourceEvent = event.touches[0];
            }
            handlePointer(sourceEvent, name);
        }, true);
    });

    window.addEventListener('scroll', function() {
        forceCheck();
    }, true);
    window.addEventListener('resize', function() {
        forceCheck();
    }, true);

    window.__cbwebview2TransparencyCheck = {
        version: SCRIPT_VERSION,
        evaluatePoint: evaluatePoint,
        forceCheck: forceCheck,
        resetLastState: resetLastState,
        setAllowNonInteractiveElementPassthrough: setAllowNonInteractiveElementPassthrough,
        setDebugOverlayEnabled: setDebugOverlayEnabled,
        isDebugOverlayEnabled: function() {
            return debugOverlayEnabled;
        },
        isAllowNonInteractiveElementPassthrough: function() {
            return allowNonInteractiveElementPassthrough;
        }
    };

    setTimeout(function() {
        forceCheck();
    }, 0);
    scheduleDebugOverlayRefreshes();
})();
