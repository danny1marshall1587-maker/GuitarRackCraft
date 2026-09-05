function (event, utils) {
    var pedal = event.icon;
    if (!pedal || !pedal.length) return;

    var ROT_MIN = -140;
    var ROT_MAX = 140;
    var ROT_RANGE = ROT_MAX - ROT_MIN;

    function updateKnobDisplay(dial, val) {
        if (!dial || !dial.length) return;
        var minVal = parseFloat(dial.attr('data-min'));
        var maxVal = parseFloat(dial.attr('data-max'));
        var clamped = Math.max(minVal, Math.min(maxVal, val));
        var norm = (clamped - minVal) / (maxVal - minVal);
        var deg = ROT_MIN + (norm * ROT_RANGE);
        dial.find('.knob-rotor').css('transform', 'translate(-50%, -100%) rotate(' + deg + 'deg)');
        dial.data('current-val', clamped);
        return clamped;
    }

    function updateSwitchDisplay(sw, val) {
        if (!sw || !sw.length) return;
        var isOn = parseFloat(val) > 0.5;
        if (isOn) {
            sw.addClass('on').removeClass('off');
            sw.find('.switch-lever').css('transform', 'translate(-50%, -50%) rotate(25deg)');
            sw.find('.switch-led').addClass('lit');
        } else {
            sw.addClass('off').removeClass('on');
            sw.find('.switch-lever').css('transform', 'translate(-50%, -50%) rotate(-25deg)');
            sw.find('.switch-led').removeClass('lit');
        }
        sw.data('current-val', isOn ? 1.0 : 0.0);
    }

    function sendPortValue(sym, val) {
        if (utils && typeof utils.set_port_value === 'function') {
            utils.set_port_value(sym, val);
        } else if (typeof event.set_port_value === 'function') {
            event.set_port_value(sym, val);
        }
        pedal.find('.mod-knob-image[mod-port-symbol="' + sym + '"]').val(val).trigger('change');
        if (event.settings && event.settings.length) {
            event.settings.find('.mod-knob-image[mod-port-symbol="' + sym + '"]').val(val).trigger('change');
        }
    }

    function bindControls(container) {
        if (!container || !container.length) return;

        // Dials
        container.find('.custom-knob-dial').each(function () {
            var dial = $(this);
            var sym = dial.attr('data-symbol');
            if (!sym) return;

            var minVal = parseFloat(dial.attr('data-min'));
            var maxVal = parseFloat(dial.attr('data-max'));
            var defVal = parseFloat(dial.attr('data-default'));
            var curVal = dial.data('current-val');
            if (typeof curVal === 'undefined') {
                curVal = defVal;
                updateKnobDisplay(dial, curVal);
            }

            dial.off('mousedown.pedal_drag touchstart.pedal_drag').on('mousedown.pedal_drag touchstart.pedal_drag', function (e) {
                e.preventDefault();
                var startY = (e.touches && e.touches.length) ? e.touches[0].clientY : e.clientY;
                var startVal = dial.data('current-val');
                if (typeof startVal === 'undefined') startVal = defVal;
                var range = maxVal - minVal;

                $(window).off('.pedal_drag_window');

                $(window).on('mousemove.pedal_drag_window touchmove.pedal_drag_window', function (ev) {
                    var currentY = (ev.touches && ev.touches.length) ? ev.touches[0].clientY : ev.clientY;
                    var deltaY = startY - currentY;
                    var sensitivity = 150.0;
                    var deltaVal = (deltaY / sensitivity) * range;
                    var newVal = Math.max(minVal, Math.min(maxVal, startVal + deltaVal));

                    if (range > 100) {
                        newVal = Math.round(newVal);
                    } else if (range > 10) {
                        newVal = Math.round(newVal * 10) / 10;
                    } else {
                        newVal = Math.round(newVal * 100) / 100;
                    }

                    updateKnobDisplay(dial, newVal);
                    sendPortValue(sym, newVal);
                });

                $(window).on('mouseup.pedal_drag_window touchend.pedal_drag_window', function () {
                    $(window).off('.pedal_drag_window');
                });
            });

            dial.off('dblclick.pedal_reset').on('dblclick.pedal_reset', function (e) {
                e.preventDefault();
                updateKnobDisplay(dial, defVal);
                sendPortValue(sym, defVal);
            });
        });

        // Toggle Switch (MOD ON)
        container.find('.custom-toggle-switch').each(function () {
            var sw = $(this);
            var sym = sw.attr('data-symbol');
            if (!sym) return;

            var defVal = parseFloat(sw.attr('data-default')) || 0.0;
            var curVal = sw.data('current-val');
            if (typeof curVal === 'undefined') {
                curVal = defVal;
                updateSwitchDisplay(sw, curVal);
            }

            sw.off('click.pedal_toggle').on('click.pedal_toggle', function (e) {
                e.preventDefault();
                var current = sw.data('current-val') || 0.0;
                var next = (current > 0.5) ? 0.0 : 1.0;
                updateSwitchDisplay(sw, next);
                sendPortValue(sym, next);
            });
        });
    }

    // Initial binding on pedal
    bindControls(pedal);

    // If settings window opens
    if (event.settings && event.settings.length) {
        bindControls(event.settings);
    }

    // Sync from incoming port changes
    if (event.type === 'change') {
        var port = event.port;
        var value = event.value;
        if (port) {
            var dial = pedal.find('.custom-knob-dial[data-symbol="' + port + '"]');
            if (dial.length) {
                updateKnobDisplay(dial, value);
            }
            var sw = pedal.find('.custom-toggle-switch[data-symbol="' + port + '"]');
            if (sw.length) {
                updateSwitchDisplay(sw, value);
            }
        }
    }

    // Read initial values from backend on start
    if (event.type === 'start') {
        pedal.find('.custom-knob-dial').each(function () {
            var dial = $(this);
            var sym = dial.attr('data-symbol');
            var img = pedal.find('.mod-knob-image[mod-port-symbol="' + sym + '"]');
            var val = img.length ? parseFloat(img.val()) : parseFloat(dial.attr('data-default'));
            if (!isNaN(val)) {
                updateKnobDisplay(dial, val);
            }
        });

        pedal.find('.custom-toggle-switch').each(function () {
            var sw = $(this);
            var sym = sw.attr('data-symbol');
            var img = pedal.find('.mod-knob-image[mod-port-symbol="' + sym + '"]');
            var val = img.length ? parseFloat(img.val()) : parseFloat(sw.attr('data-default'));
            if (!isNaN(val)) {
                updateSwitchDisplay(sw, val);
            }
        });
    }
}
