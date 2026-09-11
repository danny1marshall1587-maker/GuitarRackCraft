/*
 * Grains of Sand — MOD modgui controller.
 * Wires: quantize + fb_mode <select>s, the FREEZE button, and the
 * freeze_state-driven LED (freeze_state is a monitored output port).
 *
 * NOTE: mod-ui does NOT echo this GUI's own set_port_value calls back as
 * 'change' events (setPortWidgetsValue skips triggerJS for "from-js"
 * writes), so the freeze button updates its local state optimistically.
 * 'change' events only arrive for external changes (footswitch, preset,
 * settings dialog) — and programmatic .val() never fires DOM change
 * handlers, so no suppress-emit guard is needed anywhere here.
 */
function (event, funcs) {

    var QUANT = ['Free', 'Semitones', 'Octaves', '5ths+Octaves', 'Intervals'];
    var FBMODE = ['Recycle', 'Post-delay', 'Repeat', 'PP Delay', 'Repeat #'];

    if (event.type === 'start') {
        var icon = event.icon;
        icon.data('gos-frozen-param', false);

        var $q = icon.find('[mod-role=gos-quantize]');
        var $f = icon.find('[mod-role=gos-fbmode]');
        var $btn = icon.find('[mod-role=gos-freeze-btn]');

        $q.on('change', function () {
            funcs.set_port_value('quantize', QUANT.indexOf(this.value));
        });
        $f.on('change', function () {
            funcs.set_port_value('fb_mode', FBMODE.indexOf(this.value));
        });
        $btn.on('click', function () {
            // optimistic: our own write does not come back as a 'change'
            var next = icon.data('gos-frozen-param') ? 0 : 1;
            icon.data('gos-frozen-param', next > 0.5);
            funcs.set_port_value('freeze', next);
        });

        // RANDOM: dice-roll every sound-design knob. Deliberately leaves
        // mix/level (monitoring), freeze and thresh (surprise freezes)
        // untouched. mod-ui's own knob widgets follow set_port_value; only
        // our custom <select>s need a local sync.
        icon.find('[mod-role=gos-random-btn]').on('click', function () {
            var logRand = function (lo, hi) { return lo * Math.pow(hi / lo, Math.random()); };
            var pct = function () { return Math.random() * 100; };
            var vals = {
                delay: logRand(1, 2500),
                spray: Math.random() * 500,
                stretch: logRand(0.0625, 16),
                size: logRand(10, 500),
                density: logRand(0.5, 40),
                jitter: pct(), shape: pct(), reverse: pct(), spread: pct(),
                pitch: Math.random() * 24 - 12,
                quantize: Math.floor(Math.random() * 5),
                detune: pct(), pitch_rnd: pct(),
                feedback: pct(),
                fb_mode: Math.floor(Math.random() * 5),
                tone: logRand(500, 12000),
                repeat_dice: pct()
            };
            for (var sym in vals)
                funcs.set_port_value(sym, vals[sym]);
            icon.find('[mod-role=gos-quantize]').val(QUANT[vals.quantize]);
            icon.find('[mod-role=gos-fbmode]').val(FBMODE[vals.fb_mode]);
        });

        // initialise from current port values
        if (event.ports) {
            for (var i = 0; i < event.ports.length; ++i) {
                var p = event.ports[i];
                if (p.symbol === 'quantize') $q.val(QUANT[Math.round(p.value)]);
                if (p.symbol === 'fb_mode') $f.val(FBMODE[Math.round(p.value)]);
                if (p.symbol === 'freeze') icon.data('gos-frozen-param', p.value > 0.5);
                if (p.symbol === 'freeze_state')
                    icon.find('[mod-role=gos-freeze-led]').toggleClass('on', p.value > 0.5);
            }
        }
        return;
    }

    if (event.type === 'change') {
        var icon2 = event.icon;
        if (event.symbol === 'quantize') {
            icon2.find('[mod-role=gos-quantize]').val(QUANT[Math.round(event.value)]);
        } else if (event.symbol === 'fb_mode') {
            icon2.find('[mod-role=gos-fbmode]').val(FBMODE[Math.round(event.value)]);
        } else if (event.symbol === 'freeze') {
            icon2.data('gos-frozen-param', event.value > 0.5);
        } else if (event.symbol === 'freeze_state') {
            // LED reflects freeze from ANY source (latch or threshold)
            icon2.find('[mod-role=gos-freeze-led]')
                 .toggleClass('on', event.value > 0.5);
        }
        return;
    }
}
