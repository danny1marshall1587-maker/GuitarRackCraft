function (event) {
    var pedal = event.icon;
    if (!pedal || !pedal.length) return;

    var noteNames = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"];
    var extNames = ["Auto", "Triad", "7th", "9th", "Sus4", "Power5", "Lush Pad"];

    var chordEl = pedal.find('[mod-role="display_chord"]');
    var notesEl = pedal.find('[mod-role="display_notes"]');
    var extBadge = pedal.find('[mod-role="ext_tag"]');
    var bassBadge = pedal.find('[mod-role="bass_tag"]');
    var holdStatus = pedal.find('[mod-role="hold_status"]');
    var keys = pedal.find('.piano-key');

    var currentRoot = -1;
    var currentQual = 0;
    var currentExt = 2; // 7th
    var currentBass = 1;
    var currentHold = 1;

    function refreshChord() {
        if (currentRoot < 0) {
            chordEl.text('--');
            notesEl.text('PLAY GUITAR TO TRIGGER CHORD');
            keys.removeClass('active');
            return;
        }

        var semi = currentRoot % 12;
        var rootName = noteNames[semi];
        var qualSuffix = "";

        // Intervals for piano roll keys
        var activeSemis = [semi];

        var third = (currentQual === 1 || currentQual === 3) ? 3 : 4; // minor=3, major=4
        var fifth = (currentQual === 3) ? 6 : 7; // dim=6, perf=7
        var seventh = (currentQual === 0) ? 11 : 10; // maj7=11, dom/min=10
        var ninth = 2;

        if (currentExt === 4) { // Sus4
            third = 5;
        }

        if (currentExt === 1) { // Triad
            if (currentQual === 1) qualSuffix = "m";
            else if (currentQual === 3) qualSuffix = "dim";
            else qualSuffix = "";
            activeSemis.push((semi + third) % 12);
            activeSemis.push((semi + fifth) % 12);
        } else if (currentExt === 2) { // 7th
            if (currentQual === 0) qualSuffix = "maj7";
            else if (currentQual === 1) qualSuffix = "m7";
            else if (currentQual === 2) qualSuffix = "7";
            else if (currentQual === 3) qualSuffix = "m7b5";
            else qualSuffix = "7";
            activeSemis.push((semi + third) % 12);
            activeSemis.push((semi + fifth) % 12);
            activeSemis.push((semi + seventh) % 12);
        } else if (currentExt === 3) { // 9th
            if (currentQual === 1) qualSuffix = "m9";
            else qualSuffix = "maj9";
            activeSemis.push((semi + third) % 12);
            activeSemis.push((semi + fifth) % 12);
            activeSemis.push((semi + seventh) % 12);
            activeSemis.push((semi + ninth) % 12);
        } else if (currentExt === 4) { // Sus
            qualSuffix = "sus4";
            activeSemis.push((semi + 5) % 12);
            activeSemis.push((semi + 7) % 12);
        } else if (currentExt === 5) { // Power
            qualSuffix = "5";
            activeSemis.push((semi + 7) % 12);
        } else { // Lush Pad
            if (currentQual === 1) qualSuffix = "m (Pad)";
            else qualSuffix = "maj (Pad)";
            activeSemis.push((semi + fifth) % 12);
            activeSemis.push((semi + third) % 12);
            activeSemis.push((semi + seventh) % 12);
            activeSemis.push((semi + ninth) % 12);
        }

        chordEl.text(rootName + qualSuffix);

        // Spell out active chord notes
        var noteSpelling = activeSemis.map(function(s) { return noteNames[s]; }).join(" - ");
        if (currentBass) {
            noteSpelling += " (+" + rootName + " Bass)";
        }
        notesEl.text(noteSpelling);

        // Highlight piano keys
        keys.removeClass('active');
        for (var i = 0; i < activeSemis.length; i++) {
            keys.filter('[data-semi="' + activeSemis[i] + '"]').addClass('active');
        }
    }

    function handle_event(symbol, value) {
        if (symbol === 'detected_root') {
            currentRoot = Math.round(value);
            refreshChord();
        } else if (symbol === 'detected_qual') {
            currentQual = Math.round(value);
            refreshChord();
        } else if (symbol === 'chord_extension') {
            currentExt = Math.round(value);
            extBadge.text((extNames[currentExt] || "7th") + " EXTENSION");
            refreshChord();
        } else if (symbol === 'bass_enable') {
            currentBass = value >= 0.5 ? 1 : 0;
            bassBadge.text(currentBass ? "+ SUB BASS" : "NO BASS");
            refreshChord();
        } else if (symbol === 'latch_mode') {
            currentHold = value >= 0.5 ? 1 : 0;
            holdStatus.text(currentHold ? "HOLD ON" : "TRACKING");
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
