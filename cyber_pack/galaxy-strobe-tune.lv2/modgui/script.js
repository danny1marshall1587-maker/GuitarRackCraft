function (event) {
    var noteNames = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"];

    function getOrCreateState(pedal) {
        var state = pedal.data('galaxyTunerState');
        if (!state) {
            state = {
                currentAngle: 0.0,
                currentSpeed: 0.0,
                targetCents: 0.0,
                smoothedCents: 0.0,
                stabilityAlpha: 0.95,
                signalLevel: 0.0,
                hasSignal: false,
                inTune: false,
                lastTime: performance.now(),
                animId: null
            };
            pedal.data('galaxyTunerState', state);
            initCanvasRenderer(pedal, state);
        }
        return state;
    }

    function initCanvasRenderer(pedal, state) {
        var canvas = pedal.find('.strobe-laser-canvas')[0];
        if (!canvas) return;
        var ctx = canvas.getContext('2d');

        function renderFrame(now) {
            var dt = (now - state.lastTime) / 1000.0;
            state.lastTime = now;
            if (dt > 0.1) dt = 0.1;

            // Stage Stability Smoothing: filter out pick flutter & hand vibrato
            var alpha = state.stabilityAlpha;
            state.smoothedCents = state.smoothedCents * alpha + state.targetCents * (1.0 - alpha);

            var absCents = Math.abs(state.smoothedCents);
            var inTune = state.hasSignal && (absCents < 1.0);

            // Progressive Velocity Physics (Peterson mechanical strobe emulation):
            // - Sharp (>0): Rotates Clockwise (Right)
            // - Flat (<0): Rotates Counter-Clockwise (Left)
            // - Power curve (cents/50)^1.25: Speed drops progressively as pitch nears 0
            // - In-Tune (< 0.75 cents): Stops dead-still and locks emerald green
            if (state.hasSignal) {
                if (absCents >= 0.75) {
                    var sign = state.smoothedCents >= 0 ? 1 : -1;
                    var normalizedError = Math.min(1.0, absCents / 50.0);
                    var velocityFactor = Math.pow(normalizedError, 1.25);
                    var targetSpeed = sign * velocityFactor * 4.5; // Radians / sec
                    state.currentSpeed = state.currentSpeed * 0.82 + targetSpeed * 0.18;
                } else {
                    state.currentSpeed = state.currentSpeed * 0.75;
                    if (Math.abs(state.currentSpeed) < 0.05) state.currentSpeed = 0.0;
                }
                state.currentAngle += state.currentSpeed * dt;
            } else {
                state.currentSpeed *= 0.92;
                state.currentAngle += state.currentSpeed * dt;
            }

            var w = canvas.width;
            var h = canvas.height;
            var cx = w / 2;
            var cy = h / 2;
            var radius = w * 0.44;

            ctx.clearRect(0, 0, w, h);

            // 12 Bold, High-Contrast Segmented Wedges (Cleaner, Uncluttered Display)
            var numSegments = 12;
            var barColor = inTune ? 'rgba(0, 255, 102, 0.95)' : (state.hasSignal ? 'rgba(0, 229, 255, 0.90)' : 'rgba(0, 229, 255, 0.35)');
            var glowColor = inTune ? '#00ff66' : '#00e5ff';
            var alphaMultiplier = state.hasSignal ? 1.0 : 0.35;

            ctx.save();
            ctx.shadowBlur = state.hasSignal ? (inTune ? 22 : 14) : 4;
            ctx.shadowColor = glowColor;

            // Outer Strobe Ring: 12 thick, bold high-visibility blocks
            var arcSpan = (2 * Math.PI / numSegments) * 0.55; // 55% block, 45% clear gap

            for (var i = 0; i < numSegments; ++i) {
                var startAngle = state.currentAngle + (2 * Math.PI * i / numSegments);
                var endAngle = startAngle + arcSpan;

                ctx.beginPath();
                ctx.arc(cx, cy, radius * 0.88, startAngle, endAngle);
                ctx.lineWidth = inTune ? 14.0 : 11.0;
                ctx.strokeStyle = barColor;
                ctx.lineCap = 'round';
                ctx.globalAlpha = alphaMultiplier;
                ctx.stroke();
            }

            // Inner Offset Ring: 12 smaller alternating blocks for depth
            var innerRadius = radius * 0.64;
            var innerSpan = (2 * Math.PI / numSegments) * 0.45;
            for (var j = 0; j < numSegments; ++j) {
                var innerStartAngle = state.currentAngle + (2 * Math.PI * (j + 0.5) / numSegments);
                var innerEndAngle = innerStartAngle + innerSpan;

                ctx.beginPath();
                ctx.arc(cx, cy, innerRadius, innerStartAngle, innerEndAngle);
                ctx.lineWidth = inTune ? 8.0 : 6.0;
                ctx.strokeStyle = inTune ? 'rgba(0, 255, 102, 0.80)' : 'rgba(0, 229, 255, 0.65)';
                ctx.lineCap = 'round';
                ctx.globalAlpha = alphaMultiplier * 0.85;
                ctx.stroke();
            }

            // Outer Decorative Laser Track
            ctx.beginPath();
            ctx.arc(cx, cy, radius, 0, 2 * Math.PI);
            ctx.lineWidth = inTune ? 3.5 : 1.5;
            ctx.strokeStyle = inTune ? 'rgba(0, 255, 102, 0.85)' : 'rgba(0, 229, 255, 0.45)';
            ctx.stroke();

            // Center Ring
            ctx.beginPath();
            ctx.arc(cx, cy, radius * 0.48, 0, 2 * Math.PI);
            ctx.lineWidth = inTune ? 2.5 : 1.0;
            ctx.strokeStyle = inTune ? 'rgba(0, 255, 102, 0.70)' : 'rgba(0, 229, 255, 0.35)';
            ctx.stroke();

            ctx.restore();

            state.animId = requestAnimationFrame(renderFrame);
        }

        state.lastTime = performance.now();
        state.animId = requestAnimationFrame(renderFrame);
    }

    function handle_event(symbol, value) {
        var pedal = event.icon;
        if (!pedal || !pedal.length) return;
        var state = getOrCreateState(pedal);

        switch (symbol) {
            case 'stability':
                state.stabilityAlpha = Math.max(0.5, Math.min(0.99, value));
                break;

            case 'signal_level':
                state.signalLevel = value;
                state.hasSignal = (value > 0.04);
                var meterFill = pedal.find('[mod-role=signal_meter]');
                var statusTag = pedal.find('[mod-role=signal_status]');
                var pct = Math.min(100, Math.max(0, Math.round(value * 100)));
                if (meterFill.length) {
                    meterFill.css('width', pct + '%');
                }
                if (statusTag.length) {
                    if (state.hasSignal) {
                        statusTag.text('TRACKING');
                        statusTag.css('color', '#00ff66');
                    } else {
                        statusTag.text('READY');
                        statusTag.css('color', '#00e5ff');
                    }
                }
                break;

            case 'detected_note':
                var noteEl = pedal.find('[mod-role=detected_note]');
                var noteIdx = Math.round(value);
                if (noteIdx >= 0 && noteIdx < 12) {
                    noteEl.text(noteNames[noteIdx]);
                    noteEl.css('opacity', '1');
                    pedal.find('[mod-role=tuner-display]').removeClass('nosignal');
                } else {
                    noteEl.text('--');
                    noteEl.css('opacity', '0.35');
                    pedal.find('[mod-role=tuner-display]').addClass('nosignal');
                }
                break;

            case 'cents_offset':
                var centsEl = pedal.find('[mod-role=cents_offset]');
                var needleEl = pedal.find('[mod-role=scale_needle]');
                var noteEl = pedal.find('[mod-role=detected_note]');

                if (noteEl.text() !== '--' && value !== undefined && !isNaN(value)) {
                    state.targetCents = value;
                    var sign = value >= 0 ? '+' : '';
                    centsEl.text(sign + value.toFixed(1) + 'c');
                    centsEl.css('opacity', '1');

                    // Update Scale Needle Position (-50c = 0%, 0c = 50%, +50c = 100%)
                    if (needleEl.length) {
                        var needlePct = 50.0 + (value / 50.0) * 50.0;
                        needlePct = Math.max(2.0, Math.min(98.0, needlePct));
                        needleEl.css('left', needlePct + '%');
                    }
                } else {
                    state.targetCents = 0.0;
                    centsEl.text('--');
                    centsEl.css('opacity', '0.35');
                    if (needleEl.length) needleEl.css('left', '50%');
                }
                break;

            case 'in_tune':
                state.inTune = (value >= 0.5);
                var lockEl = pedal.find('[mod-role=in_tune]');
                if (state.inTune) {
                    lockEl.addClass('locked');
                } else {
                    lockEl.removeClass('locked');
                }
                break;

            case 'detected_freq':
                break;

            default:
                break;
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
