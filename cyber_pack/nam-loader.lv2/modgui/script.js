function (event) {
    var pedal = event.icon;
    if (!pedal || !pedal.length) return;

    // 24 Professional Tone 3000 / NAM Captures in 6 organized folders
    var ampModels = [
        // --- 01 - American Clean & Vintage ---
        { id: 0,  name: "Fender '65 Deluxe Reverb", folder: "01 - American Clean & Vintage", cat: "Clean Bell", desc: "Lush sparkling American clean with bell-like top end and scooped mids" },
        { id: 1,  name: "Fender '59 Tweed Bassman", folder: "01 - American Clean & Vintage", cat: "Vintage Tweed", desc: "Warm harmonic tweed drive with dynamic touch responsiveness and punch" },
        { id: 2,  name: "Dumble ODS Clean Channel", folder: "01 - American Clean & Vintage", cat: "Boutique Clean", desc: "Ultra-rare pristine boutique clean with legendary harmonic bloom and sag" },
        { id: 3,  name: "Matchless DC-30 Clean", folder: "01 - American Clean & Vintage", cat: "Class A Clean", desc: "Boutique EL84 chime with 3D harmonic richness and glass-smooth top" },

        // --- 02 - British Chime & Crunch ---
        { id: 4,  name: "Vox AC30 Top Boost 1964", folder: "02 - British Chime & Crunch", cat: "Chime Clean", desc: "Iconic British EL84 top-end bite, chime sparkle, and singing crunch" },
        { id: 5,  name: "Marshall Bluesbreaker 1962", folder: "02 - British Chime & Crunch", cat: "Edge of Breakup", desc: "Smooth organic vintage breakup with warm singing sustain and punch" },
        { id: 6,  name: "Marshall JTM45 Plexi", folder: "02 - British Chime & Crunch", cat: "Vintage Plexi", desc: "Full-bodied KT66 tube sag with fat woody cleans pushing into roaring crunch" },
        { id: 7,  name: "Orange Rockerverb 50", folder: "02 - British Chime & Crunch", cat: "British Crunch", desc: "Thick mid-forward British crunch with velvety low-end and saturation" },

        // --- 03 - High Gain & Modern Lead ---
        { id: 8,  name: "Marshall JCM800 2203", folder: "03 - High Gain & Modern Lead", cat: "Classic 80s Rock", desc: "Tight punchy 80s British rock rhythm and aggressive roaring harmonics" },
        { id: 9,  name: "Soldano SLO-100 Super Lead", folder: "03 - High Gain & Modern Lead", cat: "High Gain Lead", desc: "Rich saturated boutique high gain with infinite singing sustain and clarity" },
        { id: 10, name: "Peavey 5150 Block Letter", folder: "03 - High Gain & Modern Lead", cat: "Modern Metal", desc: "Brutal aggressive high gain metal rhythm with razor-sharp attack" },
        { id: 11, name: "Mesa Dual Rectifier Multi-Watt", folder: "03 - High Gain & Modern Lead", cat: "Modern High Gain", desc: "Huge wall-of-sound American high gain with scooped mids and massive chunk" },
        { id: 12, name: "Friedman BE-100 Brown Eye", folder: "03 - High Gain & Modern Lead", cat: "Boutique Lead", desc: "Ultra-refined hot-rodded Plexi tone with crisp articulation" },
        { id: 13, name: "Bogner Ecstasy 101B Red", folder: "03 - High Gain & Modern Lead", cat: "Boutique Heavy", desc: "Complex woody high gain with rich harmonic overtone bloom and fat punch" },
        { id: 14, name: "ENGL Powerball II Lead", folder: "03 - High Gain & Modern Lead", cat: "Ultra Modern Metal", desc: "Precision German high gain with razor-focused mids and crushing sub punch" },
        { id: 15, name: "Diezel VH4 Channel 3", folder: "03 - High Gain & Modern Lead", cat: "Industrial Heavy", desc: "Monumental 3D high-gain punch made famous by Tool and Metallica" },

        // --- 04 - Boutique Bass & Acoustic ---
        { id: 16, name: "Ampeg SVT-CL Classic Stack", folder: "04 - Bass Rigs", cat: "Tube Bass", desc: "Massive 300W tube bass stack with thunderous low-end authority and growl" },
        { id: 17, name: "Darkglass Microtubes B7K", folder: "04 - Bass Rigs", cat: "Modern Bass Grit", desc: "Punchy modern bass preamp with aggressive clank and harmonic distortion" },
        { id: 18, name: "Acoustic Tube DI Preamp", folder: "04 - Bass Rigs", cat: "Acoustic Preamp", desc: "Warm pristine tube DI response for acoustic guitars with natural resonance" },

        // --- 05 - Overdrive & Fuzz Pedals ---
        { id: 19, name: "Klon Centaur Gold Horsie", folder: "05 - Overdrive & Boost Pedals", cat: "Transparent OD", desc: "Mythical transparent overdrive with germanium diode clipping" },
        { id: 20, name: "Ibanez TS808 Tube Screamer", folder: "05 - Overdrive & Boost Pedals", cat: "Mid Boost OD", desc: "Iconic mid-hump overdrive for tightening amp low end and pushing leads" },
        { id: 21, name: "ProCo Rat 2 Vintage LM308", folder: "05 - Overdrive & Boost Pedals", cat: "Distortion / Fuzz", desc: "Gritty versatile distortion with signature asymmetrical filter bite" },
        { id: 22, name: "Electro-Harmonix Big Muff Pi", folder: "05 - Overdrive & Boost Pedals", cat: "Fuzz Sustain", desc: "Massive scooped fuzz with infinite singing sustain and thick low-end wool" },
        { id: 23, name: "King of Tone High Gain Side", folder: "05 - Overdrive & Boost Pedals", cat: "Boutique Overdrive", desc: "Dual-stage analog overdrive with sweet dynamic touch sensitivity" }
    ];

    // Build Folder Hierarchy Tree
    var folderTree = {
        name: "Root",
        path: "/",
        folders: {},
        files: []
    };

    // Populate tree
    ampModels.forEach(function (m) {
        if (!folderTree.folders[m.folder]) {
            folderTree.folders[m.folder] = {
                name: m.folder,
                path: "/" + m.folder + "/",
                folders: {},
                files: []
            };
        }
        folderTree.folders[m.folder].files.push(m);
    });

    var currentPathStack = []; // empty = root
    var activeModelId = 8; // JCM800 default (Slot A)
    var activeSlot    = 0; // 0=A, 1=B
    var isSlim        = false;

    // UI Elements
    var nameEl  = pedal.find('#nam-model-name');
    var catEl   = pedal.find('#nam-cat-badge');
    var descEl  = pedal.find('#nam-model-desc');
    var cabEl   = pedal.find('#nam-cab-tag');
    var crumbEl = pedal.find('#nam-folder-crumb');
    var ledA    = pedal.find('#nam-led-a');
    var ledB    = pedal.find('#nam-led-b');
    var slotBtn = pedal.find('#nam-slot-toggle-btn');
    var qualBtn = pedal.find('#nam-quality-btn');

    var prevBtn = pedal.find('#nam-prev-btn');
    var nextBtn = pedal.find('#nam-next-btn');

    // ── Slot A/B toggle ────────────────────────────────────────────────
    function setActiveSlot(slot) {
        activeSlot = (slot >= 1) ? 1 : 0;
        if (activeSlot === 1) {
            ledA.removeClass('active');
            ledB.addClass('active');
            slotBtn.text('B ▶');
        } else {
            ledA.addClass('active');
            ledB.removeClass('active');
            slotBtn.text('◀ A');
        }
        var hidden = pedal.find('#nam-active-slot-hidden');
        hidden.val(activeSlot).trigger('change');
        if (event.set_port_value) event.set_port_value('active_slot', activeSlot);
        // Update OLED to show active slot's model
        updateDisplay();
    }

    slotBtn.on('click', function (e) {
        e.stopPropagation();
        setActiveSlot(activeSlot === 0 ? 1 : 0);
    });

    // ── Quality toggle ─────────────────────────────────────────────────
    function setQuality(slim) {
        isSlim = slim;
        if (slim) {
            qualBtn.text('SLIM').addClass('slim');
        } else {
            qualBtn.text('FULL').removeClass('slim');
        }
        var hidden = pedal.find('#nam-quality-hidden');
        hidden.val(slim ? 0 : 1).trigger('change');
        if (event.set_port_value) event.set_port_value('quality', slim ? 0 : 1);
    }

    qualBtn.on('click', function (e) {
        e.stopPropagation();
        setQuality(!isSlim);
    });

    // ── Display update ─────────────────────────────────────────────────
    function updateDisplay() {
        var mId = activeModelId;
        var info = ampModels[mId];
        if (info) {
            nameEl.text('[' + (activeSlot === 0 ? 'A' : 'B') + '] ' + info.name);
            catEl.text(info.cat);
            descEl.text(info.desc);
            crumbEl.text('📁 ' + (info.folder.split(' - ')[1] || info.folder));
        }
    }

    function selectModel(id) {
        var mId = Math.max(0, Math.min(ampModels.length - 1, Math.round(id)));
        activeModelId = mId;
        updateDisplay();
        var sym = (activeSlot === 1) ? 'profile_b' : 'profile_a';
        var hidden = pedal.find('.mod-knob-image[mod-port-symbol="' + sym + '"]');
        hidden.val(mId).trigger('change');
        if (event.set_port_value) event.set_port_value(sym, mId);
    }

    // ── Prev / Next ────────────────────────────────────────────────────
    prevBtn.on('click', function (e) {
        e.stopPropagation();
        selectModel((activeModelId - 1 + ampModels.length) % ampModels.length);
    });
    nextBtn.on('click', function (e) {
        e.stopPropagation();
        selectModel((activeModelId + 1) % ampModels.length);
    });

    // ── Port change handler ────────────────────────────────────────────
    function handle_event(symbol, value) {
        if (symbol === 'profile_a' || symbol === 'profile_b') {
            // Update display if this matches the active slot
            var targetSlot = (symbol === 'profile_b') ? 1 : 0;
            if (targetSlot === activeSlot) {
                activeModelId = Math.round(value);
                updateDisplay();
            }
        } else if (symbol === 'active_slot') {
            setActiveSlot(Math.round(value));
        } else if (symbol === 'quality') {
            setQuality(value < 0.5);
        } else if (symbol === 'cab_enable') {
            cabEl.text(value >= 0.5 ? '4x12 CAB IR: ON' : 'CAB IR: BYPASS');
        }
    }

    if (event.type === 'start') {
        // Initialise from current port values
        setActiveSlot(0);
        setQuality(false);
        updateDisplay();
        var ports = event.ports;
        for (var p in ports) {
            handle_event(ports[p].symbol, ports[p].value);
        }
    } else if (event.type === 'change') {
        handle_event(event.symbol, event.value);
    }
}


