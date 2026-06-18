(function() {
	if (window.__cbwebview2InputBridgeInstalled) {
		return;
	}

	window.__cbwebview2InputBridgeInstalled = true;
	var state = window.__cbwebview2InputBridge = window.__cbwebview2InputBridge || {
		dblclickCount: 0,
		lastDblClickTime: 0,
		lastDblClickButton: -1,
		lastMouseEventTime: 0,
		lastMouseButton: -1,
		lastMouseTarget: null
	};

	function rememberMouseTarget(event) {
		state.lastMouseEventTime = Date.now();
		state.lastMouseButton = event.button || 0;
		state.lastMouseTarget = event.target || null;
	}

	function isEditableElement(element) {
		if (!element) {
			return false;
		}

		if (element.isContentEditable) {
			return true;
		}

		var tagName = String(element.tagName || '').toUpperCase();
		if (tagName === 'TEXTAREA') {
			return true;
		}

		if (tagName === 'INPUT') {
			var inputType = String(element.type || 'text').toLowerCase();
			return ['button', 'checkbox', 'radio', 'range', 'reset', 'submit', 'image', 'file', 'color', 'hidden'].indexOf(inputType) === -1;
		}

		return element.getAttribute && String(element.getAttribute('contenteditable') || '').toLowerCase() === 'true';
	}

	function getSelectionCaretRect() {
		var selection = window.getSelection && window.getSelection();
		if (!selection || selection.rangeCount <= 0) {
			return null;
		}

		var range = selection.getRangeAt(0).cloneRange();
		range.collapse(false);
		var rect = range.getClientRects && range.getClientRects()[0];
		if (rect && (rect.width || rect.height)) {
			return rect;
		}

		var marker = document.createElement('span');
		marker.textContent = '\u200b';
		range.insertNode(marker);
		rect = marker.getBoundingClientRect();
		if (marker.parentNode) {
			marker.parentNode.removeChild(marker);
		}
		return rect;
	}

	function copyComputedStyle(source, target) {
		var computed = window.getComputedStyle(source);
		var properties = [
			'boxSizing', 'width', 'height', 'fontFamily', 'fontSize', 'fontWeight', 'fontStyle',
			'letterSpacing', 'textTransform', 'textAlign', 'textIndent', 'lineHeight', 'paddingTop',
			'paddingRight', 'paddingBottom', 'paddingLeft', 'borderTopWidth', 'borderRightWidth',
			'borderBottomWidth', 'borderLeftWidth', 'whiteSpace', 'wordWrap', 'overflowWrap'
		];
		for (var i = 0; i < properties.length; ++i) {
			target.style[properties[i]] = computed[properties[i]];
		}
	}

	function getTextControlCaretRect(element) {
		if (typeof element.selectionStart !== 'number') {
			return null;
		}

		var tagName = String(element.tagName || '').toUpperCase();
		var text = String(element.value || '');
		var selectionStart = Math.max(0, Math.min(element.selectionStart, text.length));
		var mirror = document.createElement('div');
		copyComputedStyle(element, mirror);
		mirror.style.position = 'fixed';
		mirror.style.left = '-10000px';
		mirror.style.top = '0';
		mirror.style.visibility = 'hidden';
		mirror.style.pointerEvents = 'none';
		mirror.style.overflow = 'hidden';
		mirror.style.whiteSpace = tagName === 'TEXTAREA' ? 'pre-wrap' : 'pre';
		if (tagName !== 'TEXTAREA') {
			mirror.style.width = Math.max(element.scrollWidth, element.clientWidth) + 'px';
		}

		var before = text.slice(0, selectionStart).replace(/\n$/g, '\n\u200b');
		var after = text.slice(selectionStart) || '\u200b';
		mirror.textContent = before;
		var marker = document.createElement('span');
		marker.textContent = after.charAt(0);
		mirror.appendChild(marker);
		document.body.appendChild(mirror);

		var elementRect = element.getBoundingClientRect();
		var markerRect = marker.getBoundingClientRect();
		var mirrorRect = mirror.getBoundingClientRect();
		var result = {
			left: elementRect.left + (markerRect.left - mirrorRect.left) - (element.scrollLeft || 0),
			top: elementRect.top + (markerRect.top - mirrorRect.top) - (element.scrollTop || 0),
			height: markerRect.height || parseFloat(window.getComputedStyle(element).lineHeight) || 18
		};
		document.body.removeChild(mirror);
		return result;
	}

	function getEditableCaretRect(element) {
		if (!element || !isEditableElement(element)) {
			return null;
		}

		var tagName = String(element.tagName || '').toUpperCase();
		if (tagName === 'INPUT' || tagName === 'TEXTAREA') {
			return getTextControlCaretRect(element) || element.getBoundingClientRect();
		}

		return getSelectionCaretRect() || element.getBoundingClientRect();
	}

	function postImeCaretRect(element) {
		if (!window.chrome || !window.chrome.webview || typeof window.chrome.webview.postMessage !== 'function') {
			return;
		}

		var target = element || document.activeElement;
		var rect = getEditableCaretRect(target);
		if (!rect) {
			return;
		}

		window.chrome.webview.postMessage({
			__cbwebview2: true,
			type: 'imeCaret',
			x: Number(rect.left) || 0,
			y: Number(rect.top) || 0,
			height: Math.max(1, Number(rect.height) || 18),
			viewportWidth: Math.max(1, Number((window.visualViewport && window.visualViewport.width) || window.innerWidth || document.documentElement.clientWidth) || 1),
			viewportHeight: Math.max(1, Number((window.visualViewport && window.visualViewport.height) || window.innerHeight || document.documentElement.clientHeight) || 1),
			devicePixelRatio: Math.max(1, Number(window.devicePixelRatio) || 1)
		});
	}

	function postEditableFocusState(element) {
		if (!window.chrome || !window.chrome.webview || typeof window.chrome.webview.postMessage !== 'function') {
			return;
		}

		window.chrome.webview.postMessage({
			__cbwebview2: true,
			type: 'editableFocus',
			value: isEditableElement(element || document.activeElement)
		});
		postImeCaretRect(element || document.activeElement);
	}

	function forceTransparencyRefresh(event, defer) {
		if (!event || !window.__cbwebview2TransparencyCheck || typeof window.__cbwebview2TransparencyCheck.forceCheck !== 'function') {
			return;
		}

		var x = Number(event.clientX);
		var y = Number(event.clientY);
		if (!isFinite(x) || !isFinite(y)) {
			return;
		}

		window.__cbwebview2TransparencyCheck.forceCheck(x, y);
		if (!defer) {
			return;
		}

		window.setTimeout(function() {
			if (window.__cbwebview2TransparencyCheck && typeof window.__cbwebview2TransparencyCheck.forceCheck === 'function') {
				window.__cbwebview2TransparencyCheck.forceCheck(x, y);
			}
		}, 0);
	}

	function handlePointerState(event, defer) {
		rememberMouseTarget(event);
		forceTransparencyRefresh(event, defer);
	}
	

	document.addEventListener('mousedown', function(event) {
		handlePointerState(event, false);
	}, true);
	document.addEventListener('mouseup', function(event) {
		handlePointerState(event, true);
	}, true);
	document.addEventListener('click', function(event) {
		handlePointerState(event, true);
	}, true);
	document.addEventListener('pointerdown', function(event) {
		handlePointerState(event, false);
	}, true);
	document.addEventListener('pointerup', function(event) {
		handlePointerState(event, true);
	}, true);

	document.addEventListener('dblclick', function(event) {
		handlePointerState(event, true);
		state.dblclickCount += 1;
		state.lastDblClickTime = Date.now();
		state.lastDblClickButton = event.button || 0;
	}, true);

	document.addEventListener('focusin', function(event) {
		postEditableFocusState(event.target);
	}, true);
	document.addEventListener('input', function(event) {
		postImeCaretRect(event.target || document.activeElement);
	}, true);
	document.addEventListener('keyup', function(event) {
		postImeCaretRect(event.target || document.activeElement);
	}, true);
	document.addEventListener('mouseup', function(event) {
		postImeCaretRect(event.target || document.activeElement);
	}, true);
	document.addEventListener('compositionstart', function(event) {
		postImeCaretRect(event.target || document.activeElement);
	}, true);
	document.addEventListener('compositionupdate', function(event) {
		postImeCaretRect(event.target || document.activeElement);
	}, true);
	document.addEventListener('selectionchange', function() {
		postImeCaretRect(document.activeElement);
	}, true);

	document.addEventListener('focusout', function() {
		window.setTimeout(function() {
			postEditableFocusState(document.activeElement);
		}, 0);
	}, true);

	window.setTimeout(function() {
		postEditableFocusState(document.activeElement);
	}, 0);

	window.__cbwebview2DispatchDblClickFallback = function(clientX, clientY, button, screenX, screenY, ctrlKey, shiftKey, altKey, metaKey) {
		var requestCount = state.dblclickCount;
		var requestTime = Date.now();
		var x = Number(clientX) || 0;
		var y = Number(clientY) || 0;
		var domButton = Number(button) || 0;
		var sx = Number(screenX) || 0;
		var sy = Number(screenY) || 0;

		window.setTimeout(function() {
			var sawRecentNativeDblClick = state.lastDblClickTime >= requestTime - 120 &&
				state.lastDblClickButton === domButton;
			var sawDeferredNativeDblClick = state.dblclickCount !== requestCount && sawRecentNativeDblClick;
			if (sawRecentNativeDblClick || sawDeferredNativeDblClick) {
				return;
			}

			var target = document.elementFromPoint(x, y) || document.documentElement || document.body || document;
			var hasRecentMouseTarget = state.lastMouseTarget &&
				state.lastMouseTarget.isConnected !== false &&
				Date.now() - state.lastMouseEventTime <= 800 &&
				state.lastMouseButton === domButton;
			if (hasRecentMouseTarget) {
				target = state.lastMouseTarget;
			}

			var fallbackEvent = new MouseEvent('dblclick', {
				bubbles: true,
				cancelable: true,
				composed: true,
				view: window,
				detail: 2,
				screenX: sx,
				screenY: sy,
				clientX: x,
				clientY: y,
				button: domButton,
				buttons: 0,
				ctrlKey: !!ctrlKey,
				shiftKey: !!shiftKey,
				altKey: !!altKey,
				metaKey: !!metaKey
			});
			target.dispatchEvent(fallbackEvent);
		}, 80);
	};
})();
