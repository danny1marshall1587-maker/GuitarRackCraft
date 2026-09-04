function (event) {
    var pedal = event.icon;

    // Rotary Dial Drag Controller
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

            $(document).on('mousemove.cbknob touchmove.cbknob', function (me) {
                var pageY = me.pageY || me.originalEvent.touches[0].pageY;
                var delta = (startY - pageY) * ((max - min) / 160.0);
                var newVal = Math.max(min, Math.min(max, curVal + delta));

                dial.attr('data-value', newVal);
                setRot(newVal);
                event.set_port_value(sym, newVal);
            });

            $(document).one('mouseup touchend', function () {
                $(document).off('.cbknob');
            });
        });
    });

    // Freeze Hold Toggle Button
    var freezeBtn = pedal.find('.freeze-btn');
    freezeBtn.on('click', function () {
        var isActive = freezeBtn.hasClass('active');
        var newVal = isActive ? 0.0 : 1.0;
        freezeBtn.toggleClass('active', !isActive);
        event.set_port_value('hold', newVal);
    });

    // Handle Incoming Host Events
    if (event.type === 'change') {
        if (event.symbol === 'hold') {
            freezeBtn.toggleClass('active', event.value > 0.5);
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
