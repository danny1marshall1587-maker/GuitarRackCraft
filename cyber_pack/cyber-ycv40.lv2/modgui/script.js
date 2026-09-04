function (event, funcs) {
    var pedal = event.icon;
    if (!pedal || !pedal.length) return;

    var ROT_MIN = -140;
    var ROT_MAX = 140;
    var ROT_RANGE = ROT_MAX - ROT_MIN; // 280 deg total arc

    var dialMap = {};

    var tubeNames = [
        "6L6GC (Stock American)",
        "EL34 (British Crunch)",
        "6V6GT (Vintage American)",
        "KT88 (Modern High Headroom)"
    ];

    var cabNames = [
        "2x10\" Celestion Tube 10 (Stock)",
        "1x12\" Celestion Vintage 30",
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

        if (symbol === 'channel') {
            updateChannelUi(fVal);
        } else if (symbol === 'bright') {
            pedal.find('#ycv-sw-bright').toggleClass('on', fVal > 0.5);
        } else if (symbol === 'boost') {
            pedal.find('#ycv-sw-boost').toggleClass('on', fVal > 0.5);
        } else if (symbol === 'scoop') {
            pedal.find('#ycv-sw-scoop').toggleClass('on', fVal > 0.5);
        } else if (symbol === 'power_tubes') {
            var idx = Math.max(0, Math.min(3, parseInt(fVal) || 0));
            pedal.find('#ycv-tube-txt').text(tubeNames[idx]);
        } else if (symbol === 'mod_c10') {
            pedal.find('#ycv-mod-c10').toggleClass('active', fVal > 0.5);
        } else if (symbol === 'mod_smooth') {
            pedal.find('#ycv-mod-smooth').toggleClass('active', fVal > 0.5);
        } else if (symbol === 'mod_chime') {
            pedal.find('#ycv-mod-chime').toggleClass('active', fVal > 0.5);
        } else if (symbol === 'speaker_cab') {
            var cidx = Math.max(0, Math.min(2, parseInt(fVal) || 0));
            pedal.find('#ycv-cab-txt').text(cabNames[cidx]);
        } else if (symbol === 'noise_gate') {
            pedal.find('#ycv-gate-btn').toggleClass('active', fVal > 0.5);
        }
    }

    function updateChannelUi(isLead) {
        var btn = pedal.find('#ycv-ch-select');
        var jewel = pedal.find('#ycv-jewel');
        if (isLead > 0.5) {
            btn.addClass('is-lead');
            jewel.removeClass('clean').addClass('lead');
        } else {
            btn.removeClass('is-lead');
            jewel.removeClass('lead').addClass('clean');
        }
    }

    // =========================================================================
    // 1. BIDIRECTIONAL SYNC: Immediate change handler from Host / Settings View
    // =========================================================================
    if (event.type === 'change' && event.symbol) {
        applyPortValue(event.symbol, event.value);
        return;
    }

    // Send control port change to host
    function sendPortValue(symbol, value) {
        if (funcs && typeof funcs.set_port_value === 'function') {
            funcs.set_port_value(symbol, value);
        } else if (event && typeof event.set_port_value === 'function') {
            event.set_port_value(symbol, value);
        }
        var w = pedal.find('.mod-knob-image[mod-port-symbol="' + symbol + '"]');
        if (w.length) {
            w.val(value).trigger('change');
        }
    }

    // Initialize interactive knobs
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

            dial.off('mousedown.ycv touchstart.ycv').on('mousedown.ycv touchstart.ycv', function (e) {
                e.preventDefault();
                e.stopPropagation();
                var startY = (e.touches && e.touches.length) ? e.touches[0].clientY : e.clientY;
                var startVal = dial.data('current-val');
                if (typeof startVal === 'undefined') startVal = parseFloat(dial.attr('data-default'));
                var range = maxVal - minVal;

                $(window).off('.ycv_drag');

                $(window).on('mousemove.ycv_drag touchmove.ycv_drag', function (ev) {
                    var currentY = (ev.touches && ev.touches.length) ? ev.touches[0].clientY : ev.clientY;
                    var deltaY = startY - currentY;
                    var sensitivity = 150.0;
                    var deltaVal = (deltaY / sensitivity) * range;
                    var newVal = Math.max(minVal, Math.min(maxVal, startVal + deltaVal));
                    newVal = Math.round(newVal * 10) / 10;

                    updateKnobDisplay(dial, newVal);
                    sendPortValue(sym, newVal);
                });

                $(window).on('mouseup.ycv_drag touchend.ycv_drag', function () {
                    $(window).off('.ycv_drag');
                });
            });

            // Double click to reset to default
            
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

            dial.off('dblclick.ycv').on('dblclick.ycv', function (e) {
                e.preventDefault();
                e.stopPropagation();
                var def = parseFloat(dial.attr('data-default'));
                updateKnobDisplay(dial, def);
                sendPortValue(sym, def);
            });
        });
    }

    initKnobs();

    // Secondary bidirectional hook: listen on MOD control widgets
    pedal.find('.mod-knob-image').off('change.ycv_sync valuechange.ycv_sync').on('change.ycv_sync valuechange.ycv_sync', function () {
        var sym = $(this).attr('mod-port-symbol');
        var val = $(this).val();
        if (sym && typeof val !== 'undefined') {
            applyPortValue(sym, val);
        }
    });

    // Prevent drag handle from intercepting button clicks
    pedal.find('.ycv-channel-button, .ycv-bat-switch, .ycv-deck-selector, .ycv-mod-switch-pill').on('mousedown touchstart pointerdown', function (e) {
        e.stopPropagation();
    });

    // Channel Switch Handler
    pedal.find('#ycv-ch-select').off('click.ycv').on('click.ycv', function (e) {
        e.stopPropagation();
        var curVal = parseFloat(pedal.find('.mod-knob-image[mod-port-symbol="channel"]').val()) || 0;
        var nextVal = (curVal > 0.5) ? 0.0 : 1.0;
        sendPortValue('channel', nextVal);
        updateChannelUi(nextVal);
    });

    // Bright Toggle Handler
    pedal.find('#ycv-sw-bright').off('click.ycv').on('click.ycv', function (e) {
        e.stopPropagation();
        var curVal = parseFloat(pedal.find('.mod-knob-image[mod-port-symbol="bright"]').val()) || 0;
        var nextVal = (curVal > 0.5) ? 0.0 : 1.0;
        sendPortValue('bright', nextVal);
        $(this).toggleClass('on', nextVal > 0.5);
    });

    // Boost Toggle Handler
    pedal.find('#ycv-sw-boost').off('click.ycv').on('click.ycv', function (e) {
        e.stopPropagation();
        var curVal = parseFloat(pedal.find('.mod-knob-image[mod-port-symbol="boost"]').val()) || 0;
        var nextVal = (curVal > 0.5) ? 0.0 : 1.0;
        sendPortValue('boost', nextVal);
        $(this).toggleClass('on', nextVal > 0.5);
    });

    // Scoop Toggle Handler
    pedal.find('#ycv-sw-scoop').off('click.ycv').on('click.ycv', function (e) {
        e.stopPropagation();
        var curVal = parseFloat(pedal.find('.mod-knob-image[mod-port-symbol="scoop"]').val()) || 0;
        var nextVal = (curVal > 0.5) ? 0.0 : 1.0;
        sendPortValue('scoop', nextVal);
        $(this).toggleClass('on', nextVal > 0.5);
    });

    // Power Tube Swapper
    pedal.find('#ycv-tube-btn').off('click.ycv').on('click.ycv', function (e) {
        e.stopPropagation();
        var curVal = parseInt(pedal.find('.mod-knob-image[mod-port-symbol="power_tubes"]').val()) || 0;
        var nextVal = (curVal + 1) % 4;
        sendPortValue('power_tubes', nextVal);
        pedal.find('#ycv-tube-txt').text(tubeNames[nextVal]);
    });

    // Circuit Mod Buttons
    pedal.find('#ycv-mod-c10').off('click.ycv').on('click.ycv', function (e) {
        e.stopPropagation();
        var curVal = parseFloat(pedal.find('.mod-knob-image[mod-port-symbol="mod_c10"]').val()) || 0;
        var nextVal = (curVal > 0.5) ? 0.0 : 1.0;
        sendPortValue('mod_c10', nextVal);
        $(this).toggleClass('active', nextVal > 0.5);
    });

    pedal.find('#ycv-mod-smooth').off('click.ycv').on('click.ycv', function (e) {
        e.stopPropagation();
        var curVal = parseFloat(pedal.find('.mod-knob-image[mod-port-symbol="mod_smooth"]').val()) || 0;
        var nextVal = (curVal > 0.5) ? 0.0 : 1.0;
        sendPortValue('mod_smooth', nextVal);
        $(this).toggleClass('active', nextVal > 0.5);
    });

    pedal.find('#ycv-mod-chime').off('click.ycv').on('click.ycv', function (e) {
        e.stopPropagation();
        var curVal = parseFloat(pedal.find('.mod-knob-image[mod-port-symbol="mod_chime"]').val()) || 0;
        var nextVal = (curVal > 0.5) ? 0.0 : 1.0;
        sendPortValue('mod_chime', nextVal);
        $(this).toggleClass('active', nextVal > 0.5);
    });

    // Speaker Cab Switcher
    pedal.find('#ycv-cab-btn').off('click.ycv').on('click.ycv', function (e) {
        e.stopPropagation();
        var curVal = parseInt(pedal.find('.mod-knob-image[mod-port-symbol="speaker_cab"]').val()) || 0;
        var nextVal = (curVal + 1) % 3;
        sendPortValue('speaker_cab', nextVal);
        pedal.find('#ycv-cab-txt').text(cabNames[nextVal]);
    });

    // Smart Zero-Noise Gate Toggle Handler
    pedal.find('#ycv-gate-btn').off('click.ycv').on('click.ycv', function (e) {
        e.stopPropagation();
        var curVal = parseFloat(pedal.find('.mod-knob-image[mod-port-symbol="noise_gate"]').val());
        if (isNaN(curVal)) curVal = 1.0;
        var nextVal = (curVal > 0.5) ? 0.0 : 1.0;
        sendPortValue('noise_gate', nextVal);
        $(this).toggleClass('active', nextVal > 0.5);
    });

    // Handle initialization on start
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
