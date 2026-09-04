function (event) {
    var pedal = event.icon;

    // Rotary Dial Drag Interaction
    pedal.find('.custom-knob-dial').each(function () {
        var dial = $(this);
        var sym = dial.attr('data-symbol');
        var min = parseFloat(dial.attr('data-min'));
        var max = parseFloat(dial.attr('data-max'));
        var dflt = parseFloat(dial.attr('data-default'));

        function setRot(val) {
            var norm = (val - min) / (max - min);
            var deg = -140 + norm * 280;
            dial.find('.knob-rotor').css('transform', 'rotate(' + deg + 'deg)');
        }

        setRot(dflt);

        dial.on('mousedown touchstart', function (e) {
            e.preventDefault();
            var startY = e.pageY || e.originalEvent.touches[0].pageY;
            var curVal = parseFloat(dial.attr('data-value') || dflt);

            $(document).on('mousemove.thumpknob touchmove.thumpknob', function (me) {
                var pageY = me.pageY || me.originalEvent.touches[0].pageY;
                var delta = (startY - pageY) * ((max - min) / 160.0);
                var newVal = Math.max(min, Math.min(max, curVal + delta));

                dial.attr('data-value', newVal);
                setRot(newVal);
                event.set_port_value(sym, newVal);
            });

            $(document).one('mouseup touchend', function () {
                $(document).off('.thumpknob');
            });
        });
    });

    // Mode Selector Pills
    pedal.find('.mode-pill').on('click', function () {
        var pill = $(this);
        var modeVal = parseFloat(pill.attr('data-mode'));
        pedal.find('.mode-pill').removeClass('active');
        pill.addClass('active');
        event.set_port_value('listen', modeVal);
    });

    // Handle Incoming Host Events
    if (event.type === 'change') {
        if (event.symbol === 'listen') {
            var m = Math.round(event.value);
            pedal.find('.mode-pill').removeClass('active');
            pedal.find('.mode-pill[data-mode="' + m + '"]').addClass('active');
        } else if (event.symbol === 'thump_gr') {
            var led = pedal.find('#thump-act-led');
            if (event.value > 0.5) {
                var bright = Math.min(1.0, event.value / 12.0);
                led.addClass('active').css('opacity', 0.3 + 0.7 * bright);
            } else {
                led.removeClass('active').css('opacity', 0.3);
            }
        } else if (event.symbol === 'mud_gr') {
            var led = pedal.find('#mud-act-led');
            if (event.value > 0.5) {
                var bright = Math.min(1.0, event.value / 10.0);
                led.addClass('active').css('opacity', 0.3 + 0.7 * bright);
            } else {
                led.removeClass('active').css('opacity', 0.3);
            }
        } else {
            var d = pedal.find('.custom-knob-dial[data-symbol="' + event.symbol + '"]');
            if (d.length) {
                var min = parseFloat(d.attr('data-min'));
                var max = parseFloat(d.attr('data-max'));
                var norm = (event.value - min) / (max - min);
                var deg = -140 + norm * 280;
                d.find('.knob-rotor').css('transform', 'rotate(' + deg + 'deg)');
                d.attr('data-value', event.value);
            }
        }
    }
}
