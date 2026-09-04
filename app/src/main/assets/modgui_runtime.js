(function() {
    var ports = $PORTS_JSON;
    var portMap = {};
    ports.forEach(function(p) { portMap[p.symbol] = p; });

    var KNOB_FRAMES = 65;
    var DRAG_SENSITIVITY = 110;
    var activeKnob = null;
    var startY = 0, startVal = 0, activeMeta = null, activeSymbol = null;

    var activeCustomDial = null;
    var dialStartY = 0, dialStartVal = 0, dialMeta = null, dialSymbol = null;

    // Prevent scrolling when actively dragging any knob or dial
    document.addEventListener('touchmove', function(e) {
        if (activeKnob || activeCustomDial) e.preventDefault();
    }, {passive: false});

    // Claim touch and disallow Android Compose parent scroll from intercepting controls
    document.addEventListener('touchstart', function(e) {
        var t = e.target;
        if (t && t.closest('.custom-knob-dial, .mod-knob, .knob-rotor, .mod-control-group, button, .ycv-bat-switch, .friedman-bat-switch, .ycv-deck-selector, .deck-selector, .ycv-mod-switch-pill, .ycv-channel-button, .channel-btn, .mod-footswitch, canvas')) {
            if (typeof AndroidHost !== 'undefined' && typeof AndroidHost.setKnobActive === 'function') {
                AndroidHost.setKnobActive(true);
            }
        }
    }, { passive: true });

    document.addEventListener('touchend', function() {
        if (!activeKnob && !activeCustomDial) {
            if (typeof AndroidHost !== 'undefined' && typeof AndroidHost.setKnobActive === 'function') {
                AndroidHost.setKnobActive(false);
            }
        }
    });
    document.addEventListener('touchcancel', function() {
        if (!activeKnob && !activeCustomDial) {
            if (typeof AndroidHost !== 'undefined' && typeof AndroidHost.setKnobActive === 'function') {
                AndroidHost.setKnobActive(false);
            }
        }
    });

    function dispatchPluginChangeEvent(sym, val) {
        if (window._modPluginFn) {
            var $pedal = (typeof jQuery !== 'undefined') ? jQuery('.mod-pedal') : null;
            var chgEv = {
                type: 'change',
                symbol: sym,
                value: val,
                icon: $pedal,
                set_port_value: function(s, v) { AndroidHost.setParameter(s, parseFloat(v)); }
            };
            try {
                window._modPluginFn(chgEv, window._modPluginFuncs);
                if (window._modPluginEvent && typeof window._modPluginEvent.handle_event === 'function') {
                    window._modPluginEvent.handle_event(sym, val);
                }
            } catch(e) {}
        }
    }

    function setCustomDialValue(dial, val, fireEvents) {
        var min = parseFloat(dial.getAttribute('data-min'));
        var max = parseFloat(dial.getAttribute('data-max'));
        if (isNaN(min)) min = dial._modMeta ? dial._modMeta.min : 0;
        if (isNaN(max)) max = dial._modMeta ? dial._modMeta.max : 10;
        val = Math.max(min, Math.min(max, val));
        dial._currentVal = val;
        var norm = (max > min) ? (val - min) / (max - min) : 0;
        var deg = -140 + norm * 280;
        var rotor = dial.querySelector('.knob-rotor');
        if (rotor) {
            rotor.style.transform = 'translate(-50%, -100%) rotate(' + deg + 'deg)';
        }
        var sym = dial.getAttribute('data-symbol') || dial._modSymbol;
        if (sym) {
            var hiddenInput = document.querySelector('.mod-knob-image[mod-port-symbol="' + sym + '"]');
            if (hiddenInput) {
                hiddenInput.value = val;
            }
            if (fireEvents) {
                AndroidHost.setParameter(sym, val);
                dispatchPluginChangeEvent(sym, val);
            }
        }
    }

    // 1. Initialize all custom rotary dials (.custom-knob-dial)
    document.querySelectorAll('.custom-knob-dial').forEach(function(dial) {
        var sym = dial.getAttribute('data-symbol');
        if (!sym) {
            var parent = dial.parentElement;
            if (parent) {
                var hidden = parent.querySelector('[mod-port-symbol]');
                if (hidden) sym = hidden.getAttribute('mod-port-symbol');
            }
        }
        if (!sym) return;
        dial._modSymbol = sym;
        var meta = portMap[sym] || {
            min: parseFloat(dial.getAttribute('data-min')) || 0,
            max: parseFloat(dial.getAttribute('data-max')) || 10,
            default: parseFloat(dial.getAttribute('data-default')) || 5
        };
        dial._modMeta = meta;

        var val = AndroidHost.getParameter(sym);
        if (val === 0 && meta['default'] !== 0) val = meta['default'];
        setCustomDialValue(dial, val, false);

        function onDialStart(e) {
            e.preventDefault();
            e.stopPropagation();
            var pt = e.touches ? e.touches[0] : e;
            activeCustomDial = dial;
            dialStartY = pt.clientY;
            dialStartVal = (typeof dial._currentVal !== 'undefined') ? dial._currentVal : (meta['default'] || 0);
            dialMeta = meta;
            dialSymbol = sym;
            if (typeof AndroidHost.setKnobActive === 'function') AndroidHost.setKnobActive(true);
        }

        dial.addEventListener('touchstart', onDialStart, {passive: false});
        dial.addEventListener('mousedown', onDialStart);
    });

    // 2. Initialize standard MOD input control ports
    document.querySelectorAll('[mod-role="input-control-port"]').forEach(function(el) {
        var symbol = el.getAttribute('mod-port-symbol');
        if (!symbol || !portMap[symbol]) return;
        var meta = portMap[symbol];
        var value = AndroidHost.getParameter(symbol);
        if (value === 0 && meta['default'] !== 0) value = meta['default'];

        el._modSymbol = symbol;
        el._modMeta = meta;
        el._modValue = value;

        // Enumerated select (custom-select): populate dropdown from scalePoints
        var isCustomSelect = el.getAttribute('mod-widget') === 'custom-select';
        if (isCustomSelect && meta.scalePoints && meta.scalePoints.length > 0) {
            var selected = el.querySelector('.mod-enumerated-selected');
            var list = el.querySelector('.mod-enumerated-list');
            function labelForValue(v) {
                var best = meta.scalePoints[0];
                for (var i = 0; i < meta.scalePoints.length; i++) {
                    if (Math.abs(meta.scalePoints[i].value - v) < 0.001) return meta.scalePoints[i].label;
                    if (Math.abs(meta.scalePoints[i].value - v) < Math.abs(best.value - v)) best = meta.scalePoints[i];
                }
                return best.label;
            }
            if (selected) selected.textContent = labelForValue(value);
            if (list) {
                list.innerHTML = '';
                meta.scalePoints.forEach(function(sp) {
                    var div = document.createElement('div');
                    div.textContent = sp.label;
                    div.addEventListener('click', function(e) {
                        e.stopPropagation();
                        el._modValue = sp.value;
                        if (selected) selected.textContent = sp.label;
                        list.style.display = 'none';
                        el.style.overflow = '';
                        AndroidHost.setParameter(symbol, sp.value);
                        dispatchPluginChangeEvent(symbol, sp.value);
                    });
                    list.appendChild(div);
                });
            }
            if (selected && list) {
                selected.style.cursor = 'pointer';
                list.style.position = 'absolute';
                list.style.left = '0';
                selected.addEventListener('click', function(e) {
                    e.preventDefault();
                    e.stopPropagation();
                    if (list.style.display === 'block') {
                        list.style.display = 'none';
                        el.style.overflow = '';
                        return;
                    }
                    el.style.overflow = 'visible';
                    list.style.display = 'block';
                    var selRect = selected.getBoundingClientRect();
                    var scale = selRect.height / (selected.offsetHeight || 1);
                    if (scale < 0.01) scale = 1;
                    var vpH = document.documentElement.clientHeight;
                    var roomBelow = (vpH - selRect.bottom) / scale;
                    var roomAbove = selRect.top / scale;
                    if (roomAbove > roomBelow) {
                        list.style.bottom = selected.offsetHeight + 'px';
                        list.style.top = 'auto';
                        list.style.maxHeight = Math.min(180, roomAbove) + 'px';
                    } else {
                        list.style.top = selected.offsetHeight + 'px';
                        list.style.bottom = 'auto';
                        list.style.maxHeight = Math.min(180, roomBelow) + 'px';
                    }
                });
            }
            el._modIsSelect = true;
            el._modUpdateSelect = function() {
                if (selected) selected.textContent = labelForValue(el._modValue);
            };
            return;
        }

        var isToggleElement = meta.toggle ||
                              el.classList.contains('mod-on-off-image') ||
                              el.classList.contains('mod-switch-image') ||
                              el.getAttribute('mod-widget') === 'switch';
        el._modIsToggle = isToggleElement;

        el.style.touchAction = 'none';
        el.style.userSelect = 'none';
        el.style.webkitUserSelect = 'none';
        el.style.cursor = 'pointer';

        var parentKnob = el.closest('.mod-knob, .mod-knob-trim');
        if (parentKnob) {
            parentKnob.style.touchAction = 'none';
            parentKnob.style.cursor = 'pointer';
        }

        if (isToggleElement) {
            updateToggleVisual(el);
            function onToggle(e) {
                e.preventDefault();
                e.stopPropagation();
                var newVal = el._modValue > 0.5 ? meta.min : meta.max;
                el._modValue = newVal;
                updateToggleVisual(el);
                AndroidHost.setParameter(symbol, newVal);
                dispatchPluginChangeEvent(symbol, newVal);
            }
            el.addEventListener('click', onToggle);
            if (parentKnob && parentKnob !== el) {
                parentKnob.addEventListener('click', onToggle);
            }
        } else {
            // Continuous port: knob drag
            updateKnobVisual(el);

            function onStart(e) {
                e.preventDefault();
                e.stopPropagation();
                var pt = e.touches ? e.touches[0] : e;
                activeKnob = el;
                activeMeta = meta;
                activeSymbol = symbol;
                startY = pt.clientY;
                startVal = el._modValue;
                if (typeof AndroidHost.setKnobActive === 'function') AndroidHost.setKnobActive(true);
            }

            el.addEventListener('touchstart', onStart, {passive: false});
            el.addEventListener('mousedown', onStart);

            if (parentKnob && parentKnob !== el) {
                parentKnob.addEventListener('touchstart', onStart, {passive: false});
                parentKnob.addEventListener('mousedown', onStart);
            }
        }
    });

    // Global move/end handlers
    function onMove(e) {
        if (activeCustomDial) {
            e.preventDefault();
            var pt = e.touches ? e.touches[0] : e;
            var dy = dialStartY - pt.clientY;
            var range = dialMeta.max - dialMeta.min;
            var newVal = dialStartVal + (dy / 140.0) * range;
            newVal = Math.max(dialMeta.min, Math.min(dialMeta.max, newVal));
            newVal = Math.round(newVal * 100) / 100;
            setCustomDialValue(activeCustomDial, newVal, true);
            return;
        }
        if (!activeKnob) return;
        e.preventDefault();
        var pt = e.touches ? e.touches[0] : e;
        var dy = startY - pt.clientY;
        var range = activeMeta.max - activeMeta.min;
        var newVal = startVal + (dy / DRAG_SENSITIVITY) * range;
        newVal = Math.max(activeMeta.min, Math.min(activeMeta.max, newVal));
        activeKnob._modValue = newVal;
        updateKnobVisual(activeKnob);
        AndroidHost.setParameter(activeSymbol, newVal);
        dispatchPluginChangeEvent(activeSymbol, newVal);
    }

    function onEnd(e) {
        if (activeCustomDial) {
            if (typeof AndroidHost.setKnobActive === 'function') AndroidHost.setKnobActive(false);
            activeCustomDial = null;
            dialMeta = null;
            dialSymbol = null;
        }
        if (activeKnob) {
            if (typeof AndroidHost.setKnobActive === 'function') AndroidHost.setKnobActive(false);
            activeKnob = null;
            activeMeta = null;
            activeSymbol = null;
        }
    }

    document.addEventListener('touchmove', onMove, {passive: false});
    document.addEventListener('touchend', onEnd);
    document.addEventListener('touchcancel', onEnd);
    document.addEventListener('mousemove', onMove);
    document.addEventListener('mouseup', onEnd);

    // Called from Android to push parameter updates from other UIs (X11, sliders)
    window._modRefreshPorts = function() {
        document.querySelectorAll('[mod-role="input-control-port"]').forEach(function(el) {
            if (!el._modSymbol || !el._modMeta) return;
            var newVal = AndroidHost.getParameter(el._modSymbol);
            if (Math.abs(newVal - el._modValue) > 0.0001) {
                el._modValue = newVal;
                if (el._modIsSelect && el._modUpdateSelect) {
                    el._modUpdateSelect();
                } else if (el._modIsToggle) {
                    updateToggleVisual(el);
                } else {
                    updateKnobVisual(el);
                }
            }
        });
        document.querySelectorAll('.custom-knob-dial').forEach(function(dial) {
            var sym = dial._modSymbol || dial.getAttribute('data-symbol');
            if (!sym) return;
            var currentHostVal = AndroidHost.getParameter(sym);
            if (typeof dial._currentVal === 'undefined' || Math.abs(currentHostVal - dial._currentVal) > 0.0001) {
                setCustomDialValue(dial, currentHostVal, false);
            }
        });
        if (window._modPluginFn) {
            ports.forEach(function(p) {
                var hostVal = AndroidHost.getParameter(p.symbol);
                if (typeof p._lastReportedVal === 'undefined' || Math.abs(hostVal - p._lastReportedVal) > 0.0001) {
                    p._lastReportedVal = hostVal;
                    dispatchPluginChangeEvent(p.symbol, hostVal);
                }
            });
        }
        if (typeof window._modRefreshBypass === 'function') window._modRefreshBypass();
    };

    // Bypass footswitch
    var bypassPort = null;
    ports.forEach(function(p) {
        if (p.toggle) bypassPort = p;
    });
    document.querySelectorAll('[mod-role="bypass"]').forEach(function(el) {
        var enabled = bypassPort ? AndroidHost.getParameter(bypassPort.symbol) > 0.5 : true;
        el.style.cursor = 'pointer';
        el.style.touchAction = 'none';
        var lights = document.querySelectorAll('[mod-role="bypass-light"]');
        function updateBypassVisual() {
            el.classList.toggle('on', enabled);
            el.classList.toggle('off', !enabled);
            lights.forEach(function(light) {
                light.classList.toggle('on', enabled);
                light.classList.toggle('off', !enabled);
            });
        }
        updateBypassVisual();
        el.addEventListener('click', function() {
            enabled = !enabled;
            updateBypassVisual();
            if (bypassPort) {
                AndroidHost.setParameter(bypassPort.symbol, enabled ? bypassPort.max : bypassPort.min);
                dispatchPluginChangeEvent(bypassPort.symbol, enabled ? bypassPort.max : bypassPort.min);
            }
        });
        window._modRefreshBypass = function() {
            if (bypassPort) {
                var newEnabled = AndroidHost.getParameter(bypassPort.symbol) > 0.5;
                if (newEnabled !== enabled) {
                    enabled = newEnabled;
                    updateBypassVisual();
                }
            }
        };
    });

    function updateToggleVisual(el) {
        var isOn = el._modValue > 0.5;
        if (el.classList.contains('mod-on-off-image')) {
            var cs = window.getComputedStyle(el);
            var w = el.offsetWidth || parseInt(cs.width) || 60;
            var bgPos = cs.backgroundPosition || '0px 0px';
            var yPos = bgPos.split(/\s+/)[1] || '0px';
            el.style.backgroundPosition = (isOn ? -w : 0) + 'px ' + yPos;
        }
        el.classList.toggle('on', isOn);
        el.classList.toggle('off', !isOn);
    }

    function updateKnobVisual(el) {
        var meta = el._modMeta;
        var norm = (el._modValue - meta.min) / (meta.max - meta.min);
        norm = Math.max(0, Math.min(1, norm));

        var rotRange = el.getAttribute('mod-widget-rotation');
        if (rotRange) {
            var range = parseFloat(rotRange) || 270;
            var angle = -(range / 2) + norm * range;
            el.style.transform = 'rotate(' + angle + 'deg)';
            return;
        }

        var cs = window.getComputedStyle(el);
        var w = el.offsetWidth || parseInt(cs.width) || 64;

        var bgSize = cs.backgroundSize || '';
        var bgParts = bgSize.trim().split(/\s+/);
        var bgW = parseInt(bgParts[0]);
        if (isNaN(bgW)) {
            if (!el._modSpriteW) {
                var urlMatch = (cs.backgroundImage || '').match(/url\(['"]?([^'")\s]+)['"]?\)/);
                if (urlMatch) {
                    var spriteImg = new Image();
                    spriteImg.onload = function() {
                        var bph = parseInt(bgParts[1]) || el.offsetHeight || w;
                        el._modSpriteW = spriteImg.naturalWidth * (bph / spriteImg.naturalHeight);
                        updateKnobVisual(el);
                    };
                    spriteImg.src = urlMatch[1];
                    if (spriteImg.complete && spriteImg.naturalWidth > 0) {
                        spriteImg.onload = null;
                        var targetH = parseInt(bgParts[1]) || el.offsetHeight || w;
                        el._modSpriteW = spriteImg.naturalWidth * (targetH / spriteImg.naturalHeight);
                    }
                }
            }
            bgW = el._modSpriteW || (KNOB_FRAMES * w);
        }
        var frames = Math.max(1, Math.round(bgW / w));
        var frame = Math.round(norm * (frames - 1));
        el.style.backgroundPosition = (-frame * w) + 'px center';
    }

    // Path parameter support (NAM model selector)
    document.querySelectorAll('[mod-widget="custom-select-path"]').forEach(function(el) {
        var selected = el.querySelector('.mod-enumerated-selected');
        var list = el.querySelector('.mod-enumerated-list');
        if (selected && list) {
            selected.style.cursor = 'pointer';
            selected.addEventListener('click', function(e) {
                e.preventDefault();
                e.stopPropagation();
                var modelCount = list.querySelectorAll('[mod-role="enumeration-option"]').length;
                if (modelCount === 0) {
                    AndroidHost.requestFilePicker();
                } else {
                    list.style.display = list.style.display === 'block' ? 'none' : 'block';
                }
            });
        }
    });

    document.addEventListener('click', function() {
        document.querySelectorAll('.mod-enumerated-list').forEach(function(listEl) {
            listEl.style.display = 'none';
        });
        document.querySelectorAll('.mod-enumerated').forEach(function(enumEl) {
            enumEl.style.overflow = '';
        });
    });

    document.querySelectorAll('.mod-enumerated-list').forEach(function(el) {
        el.addEventListener('touchstart', function(e) {
            e.stopPropagation();
            if (typeof AndroidHost.setKnobActive === 'function') AndroidHost.setKnobActive(true);
        }, {passive: true});
        el.addEventListener('touchend', function() {
            if (typeof AndroidHost.setKnobActive === 'function') AndroidHost.setKnobActive(false);
        });
        el.addEventListener('touchcancel', function() {
            if (typeof AndroidHost.setKnobActive === 'function') AndroidHost.setKnobActive(false);
        });
    });

    window._modSetModelList = function(modelsJson) {
        var models = JSON.parse(modelsJson);
        document.querySelectorAll('.mod-enumerated-list').forEach(function(listEl) {
            listEl.innerHTML = '';
            var browseDiv = document.createElement('div');
            browseDiv.textContent = '\u2026';
            browseDiv.style.fontWeight = 'bold';
            browseDiv.addEventListener('click', function(e) {
                e.stopPropagation();
                listEl.style.display = 'none';
                AndroidHost.requestFilePicker();
            });
            listEl.appendChild(browseDiv);
            models.forEach(function(m) {
                var div = document.createElement('div');
                div.textContent = m.name;
                div.setAttribute('mod-role', 'enumeration-option');
                div.setAttribute('mod-parameter-value', m.path);
                div.addEventListener('click', function(e) {
                    e.stopPropagation();
                    listEl.style.display = 'none';
                    var parent = listEl.closest('[mod-widget="custom-select-path"]');
                    if (parent) {
                        var sel = parent.querySelector('.mod-enumerated-selected');
                        if (sel) sel.textContent = m.name;
                    }
                    listEl.querySelectorAll('div').forEach(function(d) { d.classList.remove('selected'); });
                    div.classList.add('selected');
                    AndroidHost.selectModelPath(m.path);
                });
                listEl.appendChild(div);
            });
        });
    };

    window._modSetPathDisplay = function(displayName) {
        document.querySelectorAll('[mod-role="input-parameter-value"]').forEach(function(el) {
            el.textContent = displayName;
        });
    };

    // 3. Initialize plugin's script.js if present
    if (typeof window._modPluginRawScript === 'function') {
        var jsPorts = [];
        ports.forEach(function(p) {
            var v = AndroidHost.getParameter(p.symbol);
            if (v === 0 && p['default'] !== 0) v = p['default'];
            p._lastReportedVal = v;
            jsPorts.push({ symbol: p.symbol, value: v });
            if (typeof jQuery !== 'undefined') {
                jQuery('.mod-knob-image[mod-port-symbol="' + p.symbol + '"]').val(v);
            }
        });

        var jsFuncs = {
            set_port_value: function(symbol, value) {
                AndroidHost.setParameter(symbol, parseFloat(value));
            }
        };

        var $pedal = (typeof jQuery !== 'undefined') ? jQuery('.mod-pedal') : null;
        if (!$pedal || !$pedal.length) $pedal = (typeof jQuery !== 'undefined') ? jQuery(document.body) : null;

        var startEvent = {
            type: 'start',
            icon: $pedal,
            ports: jsPorts,
            api_version: 3,
            set_port_value: function(symbol, value) {
                AndroidHost.setParameter(symbol, parseFloat(value));
            }
        };

        try {
            window._modPluginRawScript(startEvent, jsFuncs);
        } catch(e) {
            console.error("Error executing plugin script.js start event:", e);
        }

        window._modPluginFn = window._modPluginRawScript;
        window._modPluginEvent = startEvent;
        window._modPluginFuncs = jsFuncs;
    }
})();
