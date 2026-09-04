function (event) {
    var pedal = event.icon;
    if (!pedal || !pedal.length) return;

    var modelNames = [
        "SUB STOMP BOX",
        "ACOUSTIC CAJON",
        "ROCK KICK 22\"",
        "VINTAGE 808 SUB",
        "TAMBOURINE STOMP",
        "FOOT SNARE"
    ];

    var modelDescs = [
        "Deep Stage Foot-Plate Low End",
        "Woody Resonant Chamber & Slap",
        "Punchy Studio Bass Drum & Click",
        "Deep Analog Sine Sub Boom",
        "Acoustic Jingle Backbeat Pulse",
        "Acoustic Rimshot & Wire Sizzle"
    ];

    var displayModel = pedal.find('[mod-role="display_model"]');
    var displayDesc = pedal.find('[mod-role="display_desc"]');
    var hitStatus = pedal.find('[mod-role="hit_status"]');
    var hitLed = pedal.find('[mod-role="hit_led"]');
    var velBadge = pedal.find('[mod-role="vel_badge"]');
    var routeBadge = pedal.find('[mod-role="route_badge"]');

    var currentModel = 0;
    var currentVelMode = 1;
    var currentRoute = 0;

    function refreshDisplay() {
        var mIdx = Math.max(0, Math.min(Math.round(currentModel), modelNames.length - 1));
        displayModel.text(modelNames[mIdx]);
        displayDesc.text(modelDescs[mIdx]);

        velBadge.text(currentVelMode >= 0.5 ? "FIXED VEL 100%" : "DYNAMIC VEL");
        routeBadge.text(currentRoute >= 0.5 ? "SPLIT (L=GTR, R=PA)" : "MIX OUT L/R");
    }

    function flashHitLed(isActive) {
        if (isActive) {
            hitLed.addClass('hit');
            hitStatus.text("STOMP!").addClass('flashing');
        } else {
            hitLed.removeClass('hit');
            hitStatus.text("READY").removeClass('flashing');
        }
    }

    function handle_event(symbol, value) {
        if (symbol === 'model') {
            currentModel = value;
            refreshDisplay();
        } else if (symbol === 'fixed_vel') {
            currentVelMode = value;
            refreshDisplay();
        } else if (symbol === 'output_mode') {
            currentRoute = value;
            refreshDisplay();
        } else if (symbol === 'led_activity') {
            flashHitLed(value > 0.05);
        } else if (symbol === 'trigger') {
            if (value > 0.0f) {
                flashHitLed(true);
                setTimeout(function() { flashHitLed(false); }, 120);
            }
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
