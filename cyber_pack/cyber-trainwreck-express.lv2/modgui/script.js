function (event, funcs) {
    var pedal = event.icon;
    if (!pedal || !pedal.length) return;

    var ROT_MIN = -140;
    var ROT_MAX = 140;
    var ROT_RANGE = ROT_MAX - ROT_MIN;

    var dialMap = {};

    var cabNames = [
        "4x12\" Greenback Pine (Stock)",
        "2x12\" Celestion Alnico Gold",
        "Direct Out / Cab Bypass"
    ];

    var brightNames = ["OFF", "CHIME", "SPARKLE"];

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

        if (symbol === 'bright') {
            var bidx = Math.max(0, Math.min(2, parseInt(fVal) || 0));
            pedal.find('#tw-sw-bright').removeClass('pos-0 pos-1 pos-2').addClass('pos-' + bidx);
            pedal.find('#tw-bright-lbl').text(brightNames[bidx]);
        } else if (symbol === 'edge_mod') {
            pedal.find('#tw-edge-btn').toggleClass('active', fVal > 0.5);
        } else if (symbol === 'speaker_cab') {
            var cidx = Math.max(0, Math.min(2, parseInt(fVal) || 0));
            pedal.find('#tw-cab-txt').text(cabNames[cidx]);
        } else if (symbol === 'noise_gate') {
            pedal.find('#tw-gate-btn').toggleClass('active', fVal > 0.5);
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

            dial.off('mousedown.tw touchstart.tw').on('mousedown.tw touchstart.tw', function (e) {
                e.preventDefault();
                e.stopPropagation();
                var startY = (e.touches && e.touches.length) ? e.touches[0].clientY : e.clientY;
                var startVal = dial.data('current-val');
                if (typeof startVal === 'undefined') startVal = parseFloat(dial.attr('data-default'));
                var range = maxVal - minVal;

                $(window).off('.tw_drag');

                $(window).on('mousemove.tw_drag touchmove.tw_drag', function (ev) {
                    var currentY = (ev.touches && ev.touches.length) ? ev.touches[0].clientY : ev.clientY;
                    var deltaY = startY - currentY;
                    var sensitivity = 150.0;
                    var deltaVal = (deltaY / sensitivity) * range;
                    var newVal = Math.max(minVal, Math.min(maxVal, startVal + deltaVal));
                    newVal = Math.round(newVal * 10) / 10;

                    updateKnobDisplay(dial, newVal);
                    sendPortValue(sym, newVal);
                });

                $(window).on('mouseup.tw_drag touchend.tw_drag', function () {
                    $(window).off('.tw_drag');
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

            dial.off('dblclick.tw').on('dblclick.tw', function (e) {
                e.preventDefault();
                e.stopPropagation();
                var def = parseFloat(dial.attr('data-default'));
                updateKnobDisplay(dial, def);
                sendPortValue(sym, def);
            });
        });
    }

    initKnobs();

    pedal.find('.mod-knob-image').off('change.tw_sync valuechange.tw_sync').on('change.tw_sync valuechange.tw_sync', function () {
        var sym = $(this).attr('mod-port-symbol');
        var val = $(this).val();
        if (sym && typeof val !== 'undefined') applyPortValue(sym, val);
    });

    pedal.find('.trainwreck-bat-switch, .trainwreck-deck-selector, .trainwreck-mod-switch-pill').on('mousedown touchstart pointerdown', function (e) {
        e.stopPropagation();
    });

    // 3-way bright switch toggle
    pedal.find('#tw-sw-bright').off('click.tw').on('click.tw', function (e) {
        e.stopPropagation();
        var curVal = parseInt(pedal.find('.mod-knob-image[mod-port-symbol="bright"]').val()) || 0;
        var nextVal = (curVal + 1) % 3;
        sendPortValue('bright', nextVal);
        $(this).removeClass('pos-0 pos-1 pos-2').addClass('pos-' + nextVal);
        pedal.find('#tw-bright-lbl').text(brightNames[nextVal]);
    });

    // Wild Bloom Toggle
    pedal.find('#tw-edge-btn').off('click.tw').on('click.tw', function (e) {
        e.stopPropagation();
        var curVal = parseFloat(pedal.find('.mod-knob-image[mod-port-symbol="edge_mod"]').val()) || 0;
        var nextVal = (curVal > 0.5) ? 0.0 : 1.0;
        sendPortValue('edge_mod', nextVal);
        $(this).toggleClass('active', nextVal > 0.5);
    });

    // Speaker Cab Switcher
    pedal.find('#tw-cab-btn').off('click.tw').on('click.tw', function (e) {
        e.stopPropagation();
        var curVal = parseInt(pedal.find('.mod-knob-image[mod-port-symbol="speaker_cab"]').val()) || 0;
        var nextVal = (curVal + 1) % 3;
        sendPortValue('speaker_cab', nextVal);
        pedal.find('#tw-cab-txt').text(cabNames[nextVal]);
    });

    // Zero-Noise Gate Toggle Handler
    pedal.find('#tw-gate-btn').off('click.tw').on('click.tw', function (e) {
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
