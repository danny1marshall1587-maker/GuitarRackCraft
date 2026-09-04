function (event) {
    var pedal = event.icon;
    if (!pedal || !pedal.length) return;

    var ROT_MIN = -140;
    var ROT_MAX = 140;
    var ROT_RANGE = ROT_MAX - ROT_MIN; // 280 degrees total arc

    var dialMap = {};

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

            dial.off('mousedown.smartfizz touchstart.smartfizz').on('mousedown.smartfizz touchstart.smartfizz', function (e) {
                e.preventDefault();
                var startY = (e.touches && e.touches.length) ? e.touches[0].clientY : e.clientY;
                var startVal = dial.data('current-val');
                if (typeof startVal === 'undefined') startVal = parseFloat(dial.attr('data-default'));
                var range = maxVal - minVal;

                $(window).off('.smartfizz_drag');

                $(window).on('mousemove.smartfizz_drag touchmove.smartfizz_drag', function (ev) {
                    var currentY = (ev.touches && ev.touches.length) ? ev.touches[0].clientY : ev.clientY;
                    var deltaY = startY - currentY; // Upward drag increases value
                    var sensitivity = 150.0; // 150px drag covers the full range
                    var deltaVal = (deltaY / sensitivity) * range;
                    var newVal = Math.max(minVal, Math.min(maxVal, startVal + deltaVal));

                    // Round value based on step scale
                    if (range > 100) {
                        newVal = Math.round(newVal);
                    } else if (range > 10) {
                        newVal = Math.round(newVal * 10) / 10;
                    } else {
                        newVal = Math.round(newVal * 100) / 100;
                    }

                    updateKnobDisplay(dial, newVal);

                    // Send value change to MOD-UI engine
                    pedal.find('.mod-knob-image[mod-port-symbol="' + sym + '"]').val(newVal).trigger('change');
                    if (event.set_port_value) {
                        event.set_port_value(sym, newVal);
                    }
                });

                $(window).on('mouseup.smartfizz_drag touchend.smartfizz_drag', function () {
                    $(window).off('.smartfizz_drag');
                });
            });

            // Double click to reset to default value
            dial.off('dblclick.smartfizz').on('dblclick.smartfizz', function (e) {
                e.preventDefault();
                var def = parseFloat(dial.attr('data-default'));
                updateKnobDisplay(dial, def);
                pedal.find('.mod-knob-image[mod-port-symbol="' + sym + '"]').val(def).trigger('change');
                if (event.set_port_value) {
                    event.set_port_value(sym, def);
                }
            });
        });
    }

    function handle_event(symbol, value) {
        if (!symbol) return;
        var dial = dialMap[symbol] || pedal.find('.custom-knob-dial[data-symbol="' + symbol + '"]');
        if (dial && dial.length) {
            updateKnobDisplay(dial, parseFloat(value));
        }
    }

    // Initialize all knob positions and bind event handlers
    initKnobs();

    if (event.type === 'start') {
        var ports = event.ports;
        if (ports) {
            for (var p in ports) {
                if (ports.hasOwnProperty(p)) {
                    handle_event(ports[p].symbol, ports[p].value);
                }
            }
        }
    } else if (event.type === 'change') {
        handle_event(event.symbol, event.value);
    }
}
