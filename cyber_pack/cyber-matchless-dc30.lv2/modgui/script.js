function (event, funcs) {
    var pedal = event.icon;
    if (!pedal || !pedal.length) return;

    var ROT_MIN = -140;
    var ROT_MAX = 140;
    var ROT_RANGE = ROT_MAX - ROT_MIN;

    var dialMap = {};

    var cabNames = [
        "Matchless 2x12 Mismatched (Stock)",
        "2x12\" Celestion Alnico Silver",
        "Direct Out / Cab Bypass"
    ];

    var toneLabels = ["TONE 1", "TONE 2", "TONE 3", "TONE 4", "TONE 5", "TONE 6"];

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

        if (symbol === 'channel') {
            var isEf86 = fVal > 0.5;
            pedal.find('#matchless-ch-btn').toggleClass('is-ef86', isEf86);
            pedal.find('#matchless-ch-txt').text(isEf86 ? "CH 2 (EF86)" : "CH 1 (12AX7)");
        } else if (symbol === 'ch2_tone') {
            var tidx = Math.max(0, Math.min(5, parseInt(fVal) || 0));
            pedal.find('#matchless-tone-btn').text(toneLabels[tidx]);
        } else if (symbol === 'speaker_cab') {
            var sc = Math.max(0, Math.min(2, parseInt(fVal) || 0));
            pedal.find('#matchless-cab-txt').text(cabNames[sc]);
        } else if (symbol === 'noise_gate') {
            pedal.find('#matchless-gate-btn').toggleClass('active', fVal > 0.5);
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

            dial.off('mousedown.matchless touchstart.matchless').on('mousedown.matchless touchstart.matchless', function (e) {
                e.preventDefault();
                e.stopPropagation();
                var startY = (e.touches && e.touches.length) ? e.touches[0].clientY : e.clientY;
                var startVal = dial.data('current-val');
                if (typeof startVal === 'undefined') startVal = parseFloat(dial.attr('data-default'));
                var range = maxVal - minVal;

                $(window).off('.matchless_drag');

                $(window).on('mousemove.matchless_drag touchmove.matchless_drag', function (ev) {
                    var currentY = (ev.touches && ev.touches.length) ? ev.touches[0].clientY : ev.clientY;
                    var deltaY = startY - currentY;
                    var sensitivity = 150.0;
                    var deltaVal = (deltaY / sensitivity) * range;
                    var newVal = Math.max(minVal, Math.min(maxVal, startVal + deltaVal));
                    newVal = Math.round(newVal * 10) / 10;

                    updateKnobDisplay(dial, newVal);
                    sendPortValue(sym, newVal);
                });

                $(window).on('mouseup.matchless_drag touchend.matchless_drag', function () {
                    $(window).off('.matchless_drag');
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

            dial.off('dblclick.matchless').on('dblclick.matchless', function (e) {
                e.preventDefault();
                e.stopPropagation();
                var def = parseFloat(dial.attr('data-default'));
                updateKnobDisplay(dial, def);
                sendPortValue(sym, def);
            });
        });
    }

    initKnobs();

    pedal.find('.mod-knob-image').off('change.matchless_sync valuechange.matchless_sync').on('change.matchless_sync valuechange.matchless_sync', function () {
        var sym = $(this).attr('mod-port-symbol');
        var val = $(this).val();
        if (sym && typeof val !== 'undefined') applyPortValue(sym, val);
    });

    pedal.find('.matchless-ch-btn, .matchless-mod-switch-pill, .matchless-deck-selector').on('mousedown touchstart pointerdown', function (e) {
        e.stopPropagation();
    });

    // Channel Toggle
    pedal.find('#matchless-ch-btn').off('click.matchless').on('click.matchless', function (e) {
        e.stopPropagation();
        var curVal = parseFloat(pedal.find('.mod-knob-image[mod-port-symbol="channel"]').val()) || 0;
        var nextVal = (curVal > 0.5) ? 0.0 : 1.0;
        sendPortValue('channel', nextVal);
        $(this).toggleClass('is-ef86', nextVal > 0.5);
        pedal.find('#matchless-ch-txt').text(nextVal > 0.5 ? "CH 2 (EF86)" : "CH 1 (12AX7)");
    });

    // 6-Position Tone Rotary Switch Clicker
    pedal.find('#matchless-tone-btn').off('click.matchless').on('click.matchless', function (e) {
        e.stopPropagation();
        var curVal = parseInt(pedal.find('.mod-knob-image[mod-port-symbol="ch2_tone"]').val()) || 0;
        var nextVal = (curVal + 1) % 6;
        sendPortValue('ch2_tone', nextVal);
        $(this).text(toneLabels[nextVal]);
    });

    // Speaker Cab Switcher
    pedal.find('#matchless-cab-btn').off('click.matchless').on('click.matchless', function (e) {
        e.stopPropagation();
        var curVal = parseInt(pedal.find('.mod-knob-image[mod-port-symbol="speaker_cab"]').val()) || 0;
        var nextVal = (curVal + 1) % 3;
        sendPortValue('speaker_cab', nextVal);
        pedal.find('#matchless-cab-txt').text(cabNames[nextVal]);
    });

    // Zero-Noise Gate Toggle Handler
    pedal.find('#matchless-gate-btn').off('click.matchless').on('click.matchless', function (e) {
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
