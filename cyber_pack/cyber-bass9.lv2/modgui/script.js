function (event, funcs) {
    var pedal = event.icon;
    if (!pedal || !pedal.length) return;

    var ROT_MIN = -140;
    var ROT_MAX =  140;
    var ROT_RANGE = ROT_MAX - ROT_MIN;

    var dialMap = {};

    var rotaryNames = ['OFF', 'SLOW', 'FAST'];
    var rotaryClasses = ['', 'slow', 'fast'];

    // All slider ports
    var nsSymbols = [
        'ns_sub_oct', 'ns_sub_fifth', 'ns_sub_third', 'ns_root',
        'ns_third', 'ns_fifth', 'ns_oct', 'ns_oct_third', 'ns_oct_fifth', 'ns_dbl_oct'
    ];
    var dbSymbols = [
        'db_16', 'db_5_3', 'db_8', 'db_4', 'db_2_3', 'db_2', 'db_1_5', 'db_1_3', 'db_1'
    ];

    function getPct(val, min, max) {
        var clamped = Math.max(min, Math.min(max, val));
        return ((clamped - min) / (max - min)) * 100.0;
    }

    function updateSliderDisplay(sym, val) {
        var thumb = pedal.find('#bas9-thumb-' + sym);
        if (!thumb.length) return;
        var min = parseFloat(thumb.attr('data-min')) || 0;
        var max = parseFloat(thumb.attr('data-max')) || 10;
        var fVal = parseFloat(val) || 0;
        var pct = getPct(fVal, min, max);

        var track = thumb.closest('.bas9-vtrack');
        var trackH = track.height() || 100;
        var thumbH = thumb.outerHeight() || 18;
        var bottomPx = (pct / 100.0) * (trackH - thumbH);

        thumb.css('bottom', bottomPx + 'px');
        pedal.find('#bas9-fill-' + sym).css('height', (bottomPx + thumbH / 2) + 'px');
        pedal.find('#bas9-val-' + sym).text(Math.round(fVal * 10) / 10);
    }

    function updateKnobDisplay(dial, val) {
        var minVal = parseFloat(dial.attr('data-min')) || 0;
        var maxVal = parseFloat(dial.attr('data-max')) || 10;
        var clamped = Math.max(minVal, Math.min(maxVal, val));
        var norm = (clamped - minVal) / (maxVal - minVal);
        var deg = ROT_MIN + norm * ROT_RANGE;
        dial.find('.bas9-dial-rotor').css('transform', 'translate(-50%, -100%) rotate(' + deg + 'deg)');
        dial.data('current-val', clamped);

        var sym = dial.attr('data-sym');
        if (sym) {
            pedal.find('#bas9-val-' + sym).text(Math.round(clamped * 10) / 10);
        }
        return clamped;
    }

    function updateRotaryDisplay(val) {
        var idx = Math.max(0, Math.min(2, Math.round(parseFloat(val) || 0)));
        pedal.find('#bas9-rotary-txt').text(rotaryNames[idx]);
        var icon = pedal.find('#bas9-rotary-icon');
        icon.removeClass('slow fast');
        if (idx > 0) icon.addClass(rotaryClasses[idx]);
    }

    function updateThirdPills(val) {
        var v = Math.round(parseFloat(val) || 0);
        pedal.find('#bas9-q-auto, #bas9-q-min, #bas9-q-maj').removeClass('active');
        if (v === 0) pedal.find('#bas9-q-auto').addClass('active');
        else if (v === 1) pedal.find('#bas9-q-min').addClass('active');
        else pedal.find('#bas9-q-maj').addClass('active');
    }

    function sendPortValue(symbol, value) {
        if (funcs && typeof funcs.set_port_value === 'function') {
            funcs.set_port_value(symbol, value);
        } else if (event && typeof event.set_port_value === 'function') {
            event.set_port_value(symbol, value);
        }
        var w = pedal.find('.mod-knob-image[mod-port-symbol="' + symbol + '"]');
        if (w.length) w.val(value).trigger('change');
    }

    function applyPortValue(symbol, value) {
        if (!pedal || !pedal.length) return;
        var fVal = parseFloat(value);
        if (isNaN(fVal)) return;

        if (nsSymbols.indexOf(symbol) !== -1 || dbSymbols.indexOf(symbol) !== -1) {
            updateSliderDisplay(symbol, fVal);
            return;
        }

        
        if (symbol === 'preset') {
            var sel = pedal.find('#bas9-select-inst');
            if (sel.length) {
                sel.val(Math.round(fVal));
                pedal.find('#bas9-inst-display').text(sel.find('option:selected').text());
            }
            return;
        }
if (symbol === 'rotary') {
            updateRotaryDisplay(fVal);
            return;
        }

        if (symbol === 'third_mode') {
            updateThirdPills(fVal);
            return;
        }

        if (symbol === 'key_root') {
            pedal.find('#bas9-select-key').val(Math.round(fVal));
            return;
        }

        if (symbol === 'scale_mode') {
            pedal.find('#bas9-select-scale').val(Math.round(fVal));
            return;
        }

        if (symbol === 'bass_pattern') {
            pedal.find('#bas9-select-bass-pattern').val(Math.round(fVal));
            return;
        }

        if (dialMap[symbol]) {
            updateKnobDisplay(dialMap[symbol], fVal);
        } else {
            var dial = pedal.find('.bas9-dial[data-sym="' + symbol + '"]');
            if (dial.length) {
                dialMap[symbol] = dial;
                updateKnobDisplay(dial, fVal);
            }
        }

    }

    // 1. Host sync
    if (event.type === 'change' && event.symbol) {
        applyPortValue(event.symbol, event.value);
        return;
    }

    // 2. Hidden port changes
    pedal.find('.mod-knob-image').off('change.org9 valuechange.org9').on('change.org9 valuechange.org9', function () {
        var sym = $(this).attr('mod-port-symbol');
        var val = $(this).val();
        if (sym && typeof val !== 'undefined') applyPortValue(sym, val);
    });

    // 3. Slider drag interaction for Note Selector & Drawbars
    var allSliders = nsSymbols.concat(dbSymbols);
    allSliders.forEach(function (sym) {
        var thumb = pedal.find('#bas9-thumb-' + sym);
        if (!thumb.length) return;
        var track = thumb.closest('.bas9-vtrack');
        var min = parseFloat(thumb.attr('data-min')) || 0;
        var max = parseFloat(thumb.attr('data-max')) || 10;
        var range = max - min;

        thumb.off('mousedown.org9 touchstart.org9').on('mousedown.org9 touchstart.org9', function (e) {
            e.preventDefault();
            e.stopPropagation();
            var startY = (e.touches && e.touches.length) ? e.touches[0].clientY : e.clientY;
            var hiddenVal = parseFloat(pedal.find('.mod-knob-image[mod-port-symbol="' + sym + '"]').val());
            if (isNaN(hiddenVal)) hiddenVal = parseFloat(thumb.attr('data-def')) || 0;
            var startVal = hiddenVal;

            $(window).off('.org9_drag_' + sym);
            $(window).on('mousemove.org9_drag_' + sym + ' touchmove.org9_drag_' + sym, function (ev) {
                var curY = (ev.touches && ev.touches.length) ? ev.touches[0].clientY : ev.clientY;
                var deltaY = startY - curY; // up increases value
                var sensitivity = 80.0;
                var deltaVal = (deltaY / sensitivity) * range;
                var newVal = Math.max(min, Math.min(max, startVal + deltaVal));
                newVal = Math.round(newVal * 10) / 10;
                updateSliderDisplay(sym, newVal);
                sendPortValue(sym, newVal);
            });
            $(window).on('mouseup.org9_drag_' + sym + ' touchend.org9_drag_' + sym, function () {
                $(window).off('.org9_drag_' + sym);
            });
        });

        track.off('click.org9').on('click.org9', function (e) {
            if ($(e.target).hasClass('bas9-thumb')) return;
            var rect = track[0].getBoundingClientRect();
            var relY = e.clientY - rect.top;
            var trackH = rect.height;
            var pct = 1.0 - (relY / trackH);
            var newVal = Math.max(min, Math.min(max, min + pct * range));
            newVal = Math.round(newVal * 10) / 10;
            updateSliderDisplay(sym, newVal);
            sendPortValue(sym, newVal);
        });

        thumb.off('wheel.org9').on('wheel.org9', function (e) {
            e.preventDefault();
            var delta = e.originalEvent.deltaY < 0 ? (range / 20.0) : -(range / 20.0);
            var cur = parseFloat(pedal.find('.mod-knob-image[mod-port-symbol="' + sym + '"]').val()) || 0;
            var newVal = Math.max(min, Math.min(max, cur + delta));
            newVal = Math.round(newVal * 10) / 10;
            updateSliderDisplay(sym, newVal);
            sendPortValue(sym, newVal);
        });
    });

    // 4. Performance Knobs (click/drag/wheel)
    function initKnobs() {
        pedal.find('.bas9-dial').each(function () {
            var dial = $(this);
            var sym = dial.attr('data-sym');
            if (!sym) return;
            dialMap[sym] = dial;

            var minVal = parseFloat(dial.attr('data-min')) || 0;
            var maxVal = parseFloat(dial.attr('data-max')) || 10;
            var defVal = parseFloat(dial.attr('data-def')) || 5;
            var curVal = dial.data('current-val');
            if (typeof curVal === 'undefined') {
                curVal = defVal;
                updateKnobDisplay(dial, curVal);
            }

            dial.off('mousedown.org9 touchstart.org9').on('mousedown.org9 touchstart.org9', function (e) {
                e.preventDefault();
                e.stopPropagation();
                var startY = (e.touches && e.touches.length) ? e.touches[0].clientY : e.clientY;
                var startVal = dial.data('current-val');
                if (typeof startVal === 'undefined') startVal = defVal;
                var range = maxVal - minVal;

                $(window).off('.org9k_' + sym);
                $(window).on('mousemove.org9k_' + sym + ' touchmove.org9k_' + sym, function (ev) {
                    var curY = (ev.touches && ev.touches.length) ? ev.touches[0].clientY : ev.clientY;
                    var deltaY = startY - curY;
                    var sensitivity = 150.0;
                    var deltaVal = (deltaY / sensitivity) * range;
                    var newVal = Math.max(minVal, Math.min(maxVal, startVal + deltaVal));
                    newVal = Math.round(newVal * 10) / 10;
                    updateKnobDisplay(dial, newVal);
                    sendPortValue(sym, newVal);
                });
                $(window).on('mouseup.org9k_' + sym + ' touchend.org9k_' + sym, function () {
                    $(window).off('.org9k_' + sym);
                });
            });

            dial.off('wheel.org9').on('wheel.org9', function (e) {
                e.preventDefault();
                var step = (maxVal - minVal) / 50.0;
                var cur = dial.data('current-val');
                if (typeof cur === 'undefined') cur = defVal;
                var newVal = Math.max(minVal, Math.min(maxVal, cur + (e.originalEvent.deltaY < 0 ? step : -step)));
                newVal = Math.round(newVal * 10) / 10;
                updateKnobDisplay(dial, newVal);
                sendPortValue(sym, newVal);
            });

            dial.off('dblclick.org9').on('dblclick.org9', function (e) {
                e.preventDefault();
                updateKnobDisplay(dial, defVal);
                sendPortValue(sym, defVal);
            });
        });
    }

    initKnobs();

    // 5. Rotary button toggle
    pedal.find('#bas9-rotary-btn').off('click.org9').on('click.org9', function (e) {
        e.stopPropagation();
        var cur = parseInt(pedal.find('.mod-knob-image[mod-port-symbol="rotary"]').val()) || 0;
        var next = (cur + 1) % 3;
        sendPortValue('rotary', next);
        updateRotaryDisplay(next);
    });

    // 6. 3rd quality pills
    pedal.find('#bas9-q-auto').off('click.org9').on('click.org9', function (e) {
        e.stopPropagation();
        sendPortValue('third_mode', 0);
        updateThirdPills(0);
    });
    pedal.find('#bas9-q-min').off('click.org9').on('click.org9', function (e) {
        e.stopPropagation();
        sendPortValue('third_mode', 1);
        updateThirdPills(1);
    });
    pedal.find('#bas9-q-maj').off('click.org9').on('click.org9', function (e) {
        e.stopPropagation();
        sendPortValue('third_mode', 2);
        updateThirdPills(2);
    });

    // 7. Key and Scale dropdown listeners
    pedal.find('#bas9-select-key').off('change.org9').on('change.org9', function (e) {
        e.stopPropagation();
        var val = parseInt($(this).val()) || 0;
        sendPortValue('key_root', val);
    });
    pedal.find('#bas9-select-scale').off('change.org9').on('change.org9', function (e) {
        e.stopPropagation();
        var val = parseInt($(this).val()) || 0;
        sendPortValue('scale_mode', val);
    });

    // 8. Bass Pattern & Tap Tempo listeners
    pedal.find('#bas9-select-bass-pattern').off('change.org9').on('change.org9', function (e) {
        e.stopPropagation();
        var val = parseInt($(this).val()) || 0;
        sendPortValue('bass_pattern', val);
    });

    pedal.find('#bas9-tap-btn').off('click.org9').on('click.org9', function (e) {
        e.stopPropagation();
        sendPortValue('bass_tap', 1.0);
        setTimeout(function () {
            sendPortValue('bass_tap', 0.0);
        }, 50);
    });


    // Prevent drag handle clicks from stealing events on controls
    pedal.find('.bas9-pill, .bas9-rotary-btn, .bas9-dial, .bas9-thumb, .bas9-select').on('mousedown touchstart pointerdown', function (e) {
        e.stopPropagation();
    });

    // 7. Initialise on start
    if (event.type === 'start' && event.ports) {
        for (var i = 0; i < event.ports.length; i++) {
            var p = event.ports[i];
            applyPortValue(p.symbol, p.value);
        }
    }

    event.handle_event = function (symbol, value) {
        applyPortValue(symbol, value);
    };
}
