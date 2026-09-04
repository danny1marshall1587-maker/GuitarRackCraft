function (event) {
    var pedal = event.icon;
    if (!pedal || !pedal.length) return;

    var canvas = pedal.find('.humkiller-canvas')[0];
    if (!canvas) return;
    var ctx = canvas.getContext('2d');

    // Plugin state - Default MAX (0.0 dB at top of faders)
    var startTime = performance.now();
    var thresholds = [-60, -60, -60, -60, -60, -60, -60, -60];
    var levels = [0, 0, 0, 0, 0, 0, 0, 0];
    var is60Hz = false;
    var labels50 = ["<40", "50", "100", "150", "250", "500", "1.2k", "3.2k"];
    var labels60 = ["<40", "60", "120", "180", "300", "600", "1.2k", "3.2k"];
    var activeDraggingBand = -1;

    // --- Procedural Liquid Amber & Spectrum Rendering ---
    function drawAmberFold(time, freq, amp, phaseOffset, yBase, colorHex, alpha) {
        var w = canvas.width;
        var h = canvas.height;
        ctx.save();
        ctx.beginPath();
        ctx.moveTo(0, h);

        for (var x = 0; x <= w + 10; x += 10) {
            var wave = Math.sin(time * 0.5 + x * freq + phaseOffset) * amp
                     + Math.sin(time * 0.82 - x * freq * 1.8 + phaseOffset * 0.5) * (amp * 0.4)
                     + Math.sin(time * 1.3 + x * freq * 0.4) * (amp * 0.2);
            var y = yBase - (x * 0.35) + wave;
            ctx.lineTo(x, y);
        }

        ctx.lineTo(w, h);
        ctx.closePath();

        var grad = ctx.createLinearGradient(0, yBase, w * 0.5, h);
        grad.addColorStop(0, colorHex);
        grad.addColorStop(1, '#0e0701');
        ctx.fillStyle = grad;
        ctx.globalAlpha = alpha;
        ctx.fill();
        ctx.restore();
    }

    function render() {
        if (!document.contains(canvas)) return;

        var w = canvas.width;
        var h = canvas.height;
        var t = (performance.now() - startTime) / 1000.0;

        // 1. Base Amber Background
        ctx.fillStyle = '#140b02';
        ctx.fillRect(0, 0, w, h);

        // 2. Liquid Amber Animated Layers
        drawAmberFold(t, 0.008, 14.0, 0.0, h * 0.95, '#331a04', 0.8);
        drawAmberFold(t, 0.012, 18.0, 2.5, h * 0.78, '#592e07', 0.6);
        drawAmberFold(t, 0.006, 12.0, 5.1, h * 0.60, '#8c480a', 0.4);
        drawAmberFold(t, 0.010, 8.0,  1.8, h * 0.42, '#bf630e', 0.25);

        // 3. Grid Lines
        ctx.strokeStyle = 'rgba(255, 170, 0, 0.04)';
        ctx.lineWidth = 1;
        for (var x = 0; x < w; x += 8) {
            ctx.beginPath();
            ctx.moveTo(x, 0);
            ctx.lineTo(x, h);
            ctx.stroke();
        }

        // 4. Glass Top Highlight
        var gloss = ctx.createLinearGradient(0, 0, 0, h * 0.45);
        gloss.addColorStop(0, 'rgba(255, 255, 255, 0.12)');
        gloss.addColorStop(1, 'rgba(255, 255, 255, 0.0)');
        ctx.fillStyle = gloss;
        ctx.fillRect(0, 0, w, h * 0.45);

        // 5. 8 Frequency Channels & Faders
        var paddingX = 16;
        var availW = w - (paddingX * 2);
        var bandW = availW / 8.0;
        var topY = 18;
        var bottomY = h - 22;
        var railH = bottomY - topY;
        var labels = is60Hz ? labels60 : labels50;

        for (var i = 0; i < 8; ++i) {
            var cx = paddingX + i * bandW + (bandW * 0.5);

            // Channel Slot Rail
            ctx.strokeStyle = 'rgba(140, 72, 10, 0.6)';
            ctx.lineWidth = 3;
            ctx.beginPath();
            ctx.moveTo(cx, topY);
            ctx.lineTo(cx, bottomY);
            ctx.stroke();

            ctx.strokeStyle = 'rgba(0, 0, 0, 0.8)';
            ctx.lineWidth = 1.5;
            ctx.beginPath();
            ctx.moveTo(cx, topY);
            ctx.lineTo(cx, bottomY);
            ctx.stroke();

            // Real-Time Signal Level Bar (Amber-to-Red)
            var level = levels[i];
            if (level > 0.02) {
                var barH = Math.min(railH, level * railH);
                var barY = bottomY - barH;

                var barGrad = ctx.createLinearGradient(0, bottomY, 0, topY);
                barGrad.addColorStop(0, '#ffaa00');
                barGrad.addColorStop(0.7, '#ff6600');
                barGrad.addColorStop(1, '#ff2200');

                ctx.fillStyle = barGrad;
                ctx.shadowColor = '#ffaa00';
                ctx.shadowBlur = 6;
                ctx.fillRect(cx - 4, barY, 8, barH);
                ctx.shadowBlur = 0;
            }

            // Threshold Slider Position (-100dB at bottom, 0dB at top)
            var threshDb = thresholds[i];
            var normThresh = (threshDb + 100.0) / 100.0;
            normThresh = Math.max(0.0, Math.min(1.0, normThresh));
            var threshY = bottomY - (normThresh * railH);

            // Glowing Threshold Fader Handle
            ctx.fillStyle = '#ffaa00';
            ctx.shadowColor = '#ffaa00';
            ctx.shadowBlur = 8;
            ctx.fillRect(cx - 8, threshY - 2, 16, 4);

            ctx.fillStyle = '#ffffff';
            ctx.fillRect(cx - 6, threshY - 1, 12, 2);
            ctx.shadowBlur = 0;

            // Frequency Label
            ctx.fillStyle = (activeDraggingBand === i) ? '#ffaa00' : 'rgba(255, 204, 128, 0.85)';
            ctx.font = 'bold 8.5px sans-serif';
            ctx.textAlign = 'center';
            ctx.fillText(labels[i], cx, h - 6);
        }

        // Title Header inside Display
        ctx.fillStyle = '#ffaa00';
        ctx.font = 'bold 7.5px sans-serif';
        ctx.textAlign = 'center';
        ctx.fillText('8-HARMONIC MAINS HUM THRESHOLDS (dB)', w * 0.5, 12);

        requestAnimationFrame(render);
    }

    // --- User Interaction with Faders on Canvas ---
    function handleCanvasPointer(e) {
        var rect = canvas.getBoundingClientRect();
        var clientX = e.touches ? e.touches[0].clientX : e.clientX;
        var clientY = e.touches ? e.touches[0].clientY : e.clientY;

        var x = (clientX - rect.left) * (canvas.width / rect.width);
        var y = (clientY - rect.top) * (canvas.height / rect.height);

        var paddingX = 16;
        var availW = canvas.width - (paddingX * 2);
        var bandW = availW / 8.0;
        var topY = 18;
        var bottomY = canvas.height - 22;
        var railH = bottomY - topY;

        var bandIndex = Math.floor((x - paddingX) / bandW);
        if (bandIndex >= 0 && bandIndex < 8) {
            activeDraggingBand = bandIndex;
            var norm = 1.0 - ((y - topY) / railH);
            norm = Math.max(0.0, Math.min(1.0, norm));
            var newDb = Math.round((norm * 100.0 - 100.0) * 10) / 10.0;
            thresholds[bandIndex] = newDb;

            // Send value to MOD-UI LV2 port
            var symbol = 't' + (bandIndex + 1);
            pedal.find('input[mod-port-symbol="' + symbol + '"]').val(newDb).trigger('change');
            if (event.set_port_value) {
                event.set_port_value(symbol, newDb);
            }
        }
    }

    $(canvas).off('mousedown touchstart').on('mousedown touchstart', function (e) {
        handleCanvasPointer(e);
        $(window).on('mousemove.humkiller touchmove.humkiller', handleCanvasPointer);
        $(window).on('mouseup.humkiller touchend.humkiller', function () {
            activeDraggingBand = -1;
            $(window).off('.humkiller');
        });
    });

    // Double-click to reset band to 0.0 dB (Max)
    $(canvas).off('dblclick').on('dblclick', function (e) {
        var rect = canvas.getBoundingClientRect();
        var x = (e.clientX - rect.left) * (canvas.width / rect.width);
        var paddingX = 16;
        var bandW = (canvas.width - (paddingX * 2)) / 8.0;
        var bandIndex = Math.floor((x - paddingX) / bandW);
        if (bandIndex >= 0 && bandIndex < 8) {
            thresholds[bandIndex] = -60.0;
            var symbol = 't' + (bandIndex + 1);
            pedal.find('input[mod-port-symbol="' + symbol + '"]').val(-60.0).trigger('change');
            if (event.set_port_value) event.set_port_value(symbol, -60.0);
        }
    });

    // --- Interactive Master Rotary Knobs ---
    pedal.find('.custom-knob-dial').each(function () {
        var dial = $(this);
        var sym = dial.attr('data-symbol');
        var minVal = parseFloat(dial.attr('data-min'));
        var maxVal = parseFloat(dial.attr('data-max'));
        var curVal = parseFloat(dial.attr('data-default'));

        function updateKnob(v) {
            curVal = Math.max(minVal, Math.min(maxVal, v));
            var norm = (curVal - minVal) / (maxVal - minVal);
            var deg = -140 + (norm * 280);
            dial.find('.knob-rotor').css('transform', 'translate(-50%, -100%) rotate(' + deg + 'deg)');
            pedal.find('.mod-knob-image[mod-port-symbol="' + sym + '"]').val(curVal).trigger('change');
            if (event.set_port_value) event.set_port_value(sym, curVal);
        }

        updateKnob(curVal);

        dial.off('mousedown touchstart').on('mousedown touchstart', function (e) {
            var startY = e.touches ? e.touches[0].clientY : e.clientY;
            var startVal = curVal;

            $(window).on('mousemove.knob touchmove.knob', function (ev) {
                var currentY = ev.touches ? ev.touches[0].clientY : ev.clientY;
                var delta = (startY - currentY) * 0.5;
                updateKnob(startVal + (delta * ((maxVal - minVal) / 100.0)));
            });

            $(window).on('mouseup.knob touchend.knob', function () {
                $(window).off('.knob');
            });
        });
    });

    // --- Interactive Switch Toggles ---
    pedal.find('.custom-switch-toggle').off('click').on('click', function () {
        var sw = $(this);
        var sym = sw.attr('data-symbol');
        var isActive = sw.hasClass('active');
        var newVal = isActive ? 0 : 1;

        if (newVal === 1) sw.addClass('active');
        else sw.removeClass('active');

        if (sym === 'grid_freq') {
            is60Hz = (newVal === 1);
            pedal.find('[mod-role="grid_title"]').text(is60Hz ? "60 HZ" : "50 HZ");
        }

        pedal.find('.mod-switch-image[mod-port-symbol="' + sym + '"]').val(newVal).trigger('change');
        if (event.set_port_value) event.set_port_value(sym, newVal);
    });

    // --- Event Dispatcher ---
    function handle_event(symbol, value) {
        if (!symbol) return;

        // Band Thresholds
        if (symbol.charAt(0) === 't' && symbol.length <= 2) {
            var idx = parseInt(symbol.substring(1), 10) - 1;
            if (idx >= 0 && idx < 8) thresholds[idx] = value;
            return;
        }

        // Live Spectrum Levels
        if (symbol.charAt(0) === 'l' && symbol.length <= 2 && symbol !== 'low_cut' && symbol !== 'learn') {
            var lIdx = parseInt(symbol.substring(1), 10) - 1;
            if (lIdx >= 0 && lIdx < 8) levels[lIdx] = value;
            return;
        }

        // Master Controls
        if (symbol === 'reduction') {
            var norm = value / 100.0;
            var deg = -140 + (norm * 280);
            pedal.find('.custom-knob-dial[data-symbol="reduction"] .knob-rotor').css('transform', 'translate(-50%, -100%) rotate(' + deg + 'deg)');
        } else if (symbol === 'grid_freq') {
            is60Hz = (value >= 0.5);
            pedal.find('[mod-role="grid_title"]').text(is60Hz ? "60 HZ" : "50 HZ");
            if (is60Hz) pedal.find('.custom-switch-toggle[data-symbol="grid_freq"]').addClass('active');
            else pedal.find('.custom-switch-toggle[data-symbol="grid_freq"]').removeClass('active');
        } else if (symbol === 'low_cut') {
            if (value >= 0.5) pedal.find('.custom-switch-toggle[data-symbol="low_cut"]').addClass('active');
            else pedal.find('.custom-switch-toggle[data-symbol="low_cut"]').removeClass('active');
        } else if (symbol === 'listen_noise') {
            if (value >= 0.5) pedal.find('.custom-switch-toggle[data-symbol="listen_noise"]').addClass('active');
            else pedal.find('.custom-switch-toggle[data-symbol="listen_noise"]').removeClass('active');
        } else if (symbol === 'learning_status') {
            var learnBtn = pedal.find('.giant-learn-btn');
            var statusText = pedal.find('.learn-btn-status');
            if (value >= 0.5) {
                learnBtn.addClass('learning-active');
                if (statusText.length) statusText.text('● CALIBRATING 8 MAINS BANDS...');
            } else {
                if (learnBtn.hasClass('learning-active')) {
                    learnBtn.removeClass('learning-active');
                    if (statusText.length) {
                        statusText.text('✔ MAINS HUM CALIBRATED & LOCKED');
                        setTimeout(function () {
                            statusText.text('CALIBRATE PICKUP HUM');
                        }, 2500);
                    }
                }
            }
        }
    }

    if (event.type === 'start') {
        var ports = event.ports;
        for (var p in ports) {
            handle_event(ports[p].symbol, ports[p].value);
        }
        requestAnimationFrame(render);
    } else if (event.type === 'change') {
        handle_event(event.symbol, event.value);
    }
}
