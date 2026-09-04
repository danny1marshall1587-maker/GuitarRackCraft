function (event) {
    var pedal = event.icon;
    if (!pedal || !pedal.length) return;

    var ccBadge = pedal.find('[mod-role="cc_badge"]');
    var chBadge = pedal.find('[mod-role="ch_badge"]');
    var meterFill = pedal.find('[mod-role="meter_fill"]');
    var voltDisplay = pedal.find('[mod-role="volt_display"]');
    var pctDisplay = pedal.find('[mod-role="pct_display"]');
    var invDisplay = pedal.find('[mod-role="inv_display"]');
    var curveBadge = pedal.find('[mod-role="curve_badge"]');
    var smoothBadge = pedal.find('[mod-role="smooth_badge"]');
    var presetBtns = pedal.find('.expr-btn-preset');

    var currentCC = 7;
    var currentCh = 0;
    var currentMin = 0.0;
    var currentMax = 10.0;
    var currentSmooth = 15.0;
    var currentCurve = 0;
    var currentManual = 0.0;

    var curveNames = ["LINEAR", "LOG (AUDIO)", "ANTI-LOG", "S-CURVE"];

    function updateVisuals() {
        var ccLabel = "CC #" + (currentCC < 10 ? "0" + currentCC : currentCC);
        if (currentCC === 7) ccLabel += " (EXP/VOL)";
        else if (currentCC === 11) ccLabel += " (EXPRESSION)";
        else if (currentCC === 1) ccLabel += " (MODWHEEL)";
        else if (currentCC === 2) ccLabel += " (BREATH)";
        ccBadge.text(ccLabel);

        chBadge.text(currentCh === 0 ? "CH: OMNI" : "CH: " + currentCh);
        smoothBadge.text("SMOOTH: " + Math.round(currentSmooth) + "ms");
        curveBadge.text(curveNames[currentCurve] || "LINEAR");

        // Update active preset button highlight
        presetBtns.removeClass('active');
        presetBtns.each(function () {
            if (parseInt($(this).attr('data-cc'), 10) === currentCC) {
                $(this).addClass('active');
            }
        });

        // Simulate meter display if manual override is used or default
        var pct = currentManual;
        meterFill.css('width', pct + '%');
        pctDisplay.text(Math.round(pct) + '%');

        var norm = pct / 100.0;
        var volt1 = currentMin + norm * (currentMax - currentMin);
        var volt2 = currentMax - norm * (currentMax - currentMin);
        voltDisplay.text("CV 1: " + volt1.toFixed(2) + " V");
        invDisplay.text("INV: " + volt2.toFixed(2) + " V");
    }

    // Quick CC Preset Buttons Click
    presetBtns.on('click', function (e) {
        e.stopPropagation();
        var targetCC = parseInt($(this).attr('data-cc'), 10);
        currentCC = targetCC;
        pedal.find('.mod-knob-image[mod-port-symbol="cc_number"]').val(targetCC).trigger('change');
        if (event.set_port_value) {
            event.set_port_value('cc_number', targetCC);
        }
        updateVisuals();
    });

    function handle_event(symbol, value) {
        if (symbol === 'cc_number') {
            currentCC = Math.round(value);
            updateVisuals();
        } else if (symbol === 'midi_channel') {
            currentCh = Math.round(value);
            updateVisuals();
        } else if (symbol === 'min_val') {
            currentMin = value;
            updateVisuals();
        } else if (symbol === 'max_val') {
            currentMax = value;
            updateVisuals();
        } else if (symbol === 'smoothing') {
            currentSmooth = value;
            updateVisuals();
        } else if (symbol === 'curve') {
            currentCurve = Math.round(value);
            updateVisuals();
        } else if (symbol === 'manual_pos') {
            currentManual = value;
            updateVisuals();
        }
    }

    if (event.type === 'start') {
        var ports = event.ports;
        for (var p in ports) {
            handle_event(ports[p].symbol, ports[p].value);
        }
    } else if (event.type === 'change') {
        handle_event(event.symbol, event.value);
    }
}
