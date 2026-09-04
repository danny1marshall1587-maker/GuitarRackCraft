function (event, funcs) {
    var pedal = event.icon;
    if (!pedal || !pedal.length) return;

    var ROT_MIN = -140;
    var ROT_MAX = 140;
    var ROT_RANGE = ROT_MAX - ROT_MIN;

    var dialMap = {};

    var cabNames = [
        "Hiwatt SE4122 Fane Purple-Backs (Stock)",
        "2x12\" Fane Crescendo (Gilmour Pulse)",
        "Direct Out / Cab Bypass"
    ];

    function updateKnobDisplay(dial, val) {
        var minVal = parseFloat(dial.attr('data-min'));
        var maxVal = parseFloat(dial.attr('data-max'));
        var clamped = Math.max(minVal, Math.min(maxVal, val));
        var norm = (clamped - minVal) / (maxVal - minVal);
        var deg = ROT_MIN + (norm * ROT_RANGE);
        dial.find('.knob-rotor').css('transform', 'translate(-50%, -100%) rotate(' + deg + 'deg)');
        dial.data('current-val', clamped);
        return clamped;
    }

    function applyPortValue(symbol, value) {
        if (!pedal || !pedal.length) return;
        var fVal = parseFloat(value);
        if (isNaN(fVal)) return;

        if (dialMap[symbol]) {
            updateKnobDisplay(dialMap[symbol], fVal);
        } else {
            var dial = pedal.find('.custom-knob-dial[data-symbol="' + symbol + '"]');
            if (dial.length) {
                dialMap[symbol] = dial;
                updateKnobDisplay(dial, fVal);
            }
        }

        if (symbol === 'input_link') {
            pedal.find('#hiwatt-link-btn').toggleClass('active', fVal > 0.5);
        } else if (symbol === 'speaker_cab') {
            var sc = Math.max(0, Math.min(2, parseInt(fVal) || 0));
            pedal.find('#hiwatt-cab-txt').text(cabNames[sc]);
        } else if (symbol === 'noise_gate') {
            pedal.find('#hiwatt-gate-btn').toggleClass('active', fVal > 0.5);
        }
    }

    if (event.type === 'change' && event.symbol) {
        applyPortValue(event.symbol, event.value);
        return;
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

    function initKnobs() {
        var dials = pedal.find('.custom-knob-dial');
        if (!dials.length) return;

        dials.each(function () {
            var dial = $(this);
            var sym = dial.attr('data-symbol');
            if (!sym) return;

            dialMap[sym] = dial;

            var minVal = parseFloat(dial.attr('data-min'));
            var maxVal = parseFloat(dial.attr('data-max'));
            var defVal = parseFloat(dial.attr('data-default'));
            var curVal = dial.data('current-val');
            if (typeof curVal === 'undefined') {
                curVal = defVal;
                updateKnobDisplay(dial, curVal);
            }

            dial.off('mousedown.hiwatt touchstart.hiwatt').on('mousedown.hiwatt touchstart.hiwatt', function (e) {
                e.preventDefault();
                e.stopPropagation();
                var startY = (e.touches && e.touches.length) ? e.touches[0].clientY : e.clientY;
                var startVal = dial.data('current-val');
                if (typeof startVal === 'undefined') startVal = parseFloat(dial.attr('data-default'));
                var range = maxVal - minVal;

                $(window).off('.hiwatt_drag');

                $(window).on('mousemove.hiwatt_drag touchmove.hiwatt_drag', function (ev) {
                    var currentY = (ev.touches && ev.touches.length) ? ev.touches[0].clientY : ev.clientY;
                    var deltaY = startY - currentY;
                    var sensitivity = 150.0;
                    var deltaVal = (deltaY / sensitivity) * range;
                    var newVal = Math.max(minVal, Math.min(maxVal, startVal + deltaVal));
                    newVal = Math.round(newVal * 10) / 10;

                    updateKnobDisplay(dial, newVal);
                    sendPortValue(sym, newVal);
                });

                $(window).on('mouseup.hiwatt_drag touchend.hiwatt_drag', function () {
                    $(window).off('.hiwatt_drag');
                });
            });

            
            // Mouse Wheel Hover Scrolling
            dial.off('wheel.amp_scroll').on('wheel.amp_scroll', function (e) {
                e.preventDefault();
                e.stopPropagation();
                var oe = e.originalEvent || e;
                var delta = -oe.deltaY;
                if (delta === 0) return;
                var step = (maxVal - minVal) / 50.0;
                if (oe.shiftKey) step = (maxVal - minVal) / 200.0;
                var curVal = dial.data('current-val');
                if (typeof curVal === 'undefined') curVal = parseFloat(dial.attr('data-default'));
                var newVal = Math.max(minVal, Math.min(maxVal, curVal + (delta > 0 ? step : -step)));
                newVal = Math.round(newVal * 100) / 100;
                updateKnobDisplay(dial, newVal);
                sendPortValue(sym, newVal);
            });

            dial.off('dblclick.hiwatt').on('dblclick.hiwatt', function (e) {
                e.preventDefault();
                e.stopPropagation();
                var def = parseFloat(dial.attr('data-default'));
                updateKnobDisplay(dial, def);
                sendPortValue(sym, def);
            });
        });
    }

    initKnobs();

    pedal.find('.mod-knob-image').off('change.hiwatt_sync valuechange.hiwatt_sync').on('change.hiwatt_sync valuechange.hiwatt_sync', function () {
        var sym = $(this).attr('mod-port-symbol');
        var val = $(this).val();
        if (sym && typeof val !== 'undefined') applyPortValue(sym, val);
    });

    pedal.find('.hiwatt-deck-selector, .hiwatt-mod-switch-pill').on('mousedown touchstart pointerdown', function (e) {
        e.stopPropagation();
    });

    // Link toggle
    pedal.find('#hiwatt-link-btn').off('click.hiwatt').on('click.hiwatt', function (e) {
        e.stopPropagation();
        var curVal = parseFloat(pedal.find('.mod-knob-image[mod-port-symbol="input_link"]').val()) || 0;
        var nextVal = (curVal > 0.5) ? 0.0 : 1.0;
        sendPortValue('input_link', nextVal);
        $(this).toggleClass('active', nextVal > 0.5);
    });

    // Speaker Cab Switcher
    pedal.find('#hiwatt-cab-btn').off('click.hiwatt').on('click.hiwatt', function (e) {
        e.stopPropagation();
        var curVal = parseInt(pedal.find('.mod-knob-image[mod-port-symbol="speaker_cab"]').val()) || 0;
        var nextVal = (curVal + 1) % 3;
        sendPortValue('speaker_cab', nextVal);
        pedal.find('#hiwatt-cab-txt').text(cabNames[nextVal]);
    });

    // Zero-Noise Gate Toggle Handler
    pedal.find('#hiwatt-gate-btn').off('click.hiwatt').on('click.hiwatt', function (e) {
        e.stopPropagation();
        var curVal = parseFloat(pedal.find('.mod-knob-image[mod-port-symbol="noise_gate"]').val());
        if (isNaN(curVal)) curVal = 1.0;
        var nextVal = (curVal > 0.5) ? 0.0 : 1.0;
        sendPortValue('noise_gate', nextVal);
        $(this).toggleClass('active', nextVal > 0.5);
    });

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
