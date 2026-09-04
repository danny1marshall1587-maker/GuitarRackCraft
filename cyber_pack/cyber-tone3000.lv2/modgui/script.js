function (event) {
    var pedal = event.icon;
    if (!pedal || !pedal.length) return;

    var TONE_MODELS = [
        { id: 0, name: "Martin D-45 Studio Mic (Acoustic)", author: "CyberAudio", category: "Acoustic", desc: "Warm studio condenser mic response converting raw piezo pickup into full woody Dreadnought body", defaultIr: 0, irName: "Neumann U87 Condenser" },
        { id: 1, name: "Taylor 814ce Grand Auditorium", author: "CyberAudio", category: "Acoustic", desc: "Pristine modern acoustic capture with airy top-end sparkle and balanced fingerpicking clarity", defaultIr: 1, irName: "Royer R-121 Ribbon" },
        { id: 2, name: "Gibson J-45 Vintage Round Shoulder", author: "CyberAudio", category: "Acoustic", desc: "Classic vintage woody thud with punchy low-mids for strumming and folk rhythm", defaultIr: 2, irName: "Shure SM7B Dynamic" },
        { id: 3, name: "Guild F-512 12-String Acoustic", author: "CyberAudio", category: "Acoustic", desc: "Shimmering, lush, choir-like acoustic resonance with wide stereo spread feel", defaultIr: 3, irName: "Stereo Room Pair" },
        { id: 4, name: "Fender '65 Deluxe Reverb Blackface", author: "FenderTone", category: "Clean", desc: "Sparkling American clean with scooped mids, bell-like highs, and smooth edge-of-breakup", defaultIr: 4, irName: "Jensen C12N 1x12" },
        { id: 5, name: "Fender '59 Tweed Bassman 4x10", author: "ToneLover", category: "Clean", desc: "Touch-sensitive vintage Tweed breakup with punchy low-end and singing harmonics", defaultIr: 5, irName: "Jensen P10R 4x10" },
        { id: 6, name: "Dumble Overdrive Special (Clean)", author: "BoutiqueAmps", category: "Clean", desc: "Legendary thick, blooming boutique clean with infinite headroom and touch response", defaultIr: 6, irName: "EVM12L 2x12" },
        { id: 7, name: "Matchless DC-30 Chime Channel", author: "BritishTone", category: "Clean", desc: "Iconic Class-A EL84 chime with 3D high-end clarity and harmonic richness", defaultIr: 7, irName: "Alnico Blue 2x12" },
        { id: 8, name: "Vox AC30 Top Boost 1964", author: "JMI_Vintage", category: "Crunch", desc: "The quintessential British Invasion top-boost bite, chime sparkle, and aggressive crunch", defaultIr: 7, irName: "Alnico Blue 2x12" },
        { id: 9, name: "Marshall Bluesbreaker 1962 (JTM45)", author: "PlexiFan", category: "Crunch", desc: "Warm organic tube sag with fat woody cleans pushing into singing creamy blues sustain", defaultIr: 8, irName: "Celestion V30 4x12" },
        { id: 10, name: "Orange Rockerverb 50 MkIII", author: "DoomRigs", category: "Crunch", desc: "Mid-forward British roar with thick velvety low-end and rich harmonic fuzz-edge", defaultIr: 9, irName: "Orange V30 4x12" },
        { id: 11, name: "Marshall JCM800 2203 Classic 80s", author: "HardRock80", category: "High Gain", desc: "Tight, biting 80s rock rhythm with aggressive cut and punchy low end", defaultIr: 8, irName: "Celestion V30 4x12" },
        { id: 12, name: "Soldano SLO-100 Super Lead", author: "LeadKing", category: "High Gain", desc: "Legendary singing boutique high-gain lead channel with infinite sustain and clarity", defaultIr: 10, irName: "Eminence Legend 4x12" },
        { id: 13, name: "Mesa/Boogie Dual Rectifier Multi-Watt", author: "ModernMetal", category: "High Gain", desc: "Massive American wall-of-sound rhythm tone with scooped mids and huge palm-mute thump", defaultIr: 11, irName: "Oversized V30 4x12" },
        { id: 14, name: "Ampeg SVT Classic 8x10 Tube Stack", author: "BassBeast", category: "Bass", desc: "Monumental 300W tube bass tone with earth-shaking low-end authority and growl", defaultIr: 12, irName: "Ampeg 8x10 Sealed" },
        { id: 15, name: "Darkglass Microtubes B7K Ultra", author: "BassPrecision", category: "Bass", desc: "Aggressive modern bass preamp with clinical attack clank and biting low-end definition", defaultIr: 13, irName: "Neodymium 4x10" }
    ];

    var currentModelId = 0;
    var audioCtx = null;

    function updateOled(mId) {
        currentModelId = Math.max(0, Math.min(TONE_MODELS.length - 1, mId));
        var m = TONE_MODELS[currentModelId];
        pedal.find('#t3k-screen-title').text(m.name);
        pedal.find('#t3k-screen-sub').text("Cab: " + m.irName);
        pedal.find('#t3k-screen-cat').text(m.category.toUpperCase());
    }

    function setModelToPedal(mId) {
        currentModelId = Math.max(0, Math.min(TONE_MODELS.length - 1, mId));
        updateOled(currentModelId);

        var m = TONE_MODELS[currentModelId];

        // Trigger MOD-UI port change
        pedal.find('#t3k-port-model').val(currentModelId).trigger('change');
        if (event.set_port_value) {
            event.set_port_value('model', currentModelId);
            event.set_port_value('ir', m.defaultIr);
        }
        pedal.find('#t3k-port-ir').val(m.defaultIr).trigger('change');
    }

    // Bind arrows on OLED
    pedal.find('#t3k-btn-prev').off('click.t3k').on('click.t3k', function (e) {
        e.preventDefault(); e.stopPropagation();
        var next = (currentModelId - 1 + TONE_MODELS.length) % TONE_MODELS.length;
        setModelToPedal(next);
    });

    pedal.find('#t3k-btn-next').off('click.t3k').on('click.t3k', function (e) {
        e.preventDefault(); e.stopPropagation();
        var next = (currentModelId + 1) % TONE_MODELS.length;
        setModelToPedal(next);
    });

    // ── Full-Screen Overlay Trigger ──────────────────────────────────────────
    function openOverlay() {
        var overlayId = 't3k-full-overlay';
        var modal = $('#' + overlayId);
        if (!modal.length) {
            $('body').append('<div id="' + overlayId + '" class="t3k-overlay-backdrop" style="display:none;"></div>');
            modal = $('#' + overlayId);
        }

        var mActive = TONE_MODELS[currentModelId];

        var html = [
            '<div class="t3k-modal-window">',
            '  <div class="t3k-modal-header">',
            '    <div class="t3k-modal-title-box">',
            '      <div class="t3k-modal-logo">TONE<span>3000</span></div>',
            '      <div class="t3k-modal-cloud-status">&#9679; CONNECTED TO CLOUD</div>',
            '    </div>',
            '    <button type="button" class="t3k-close-btn" id="t3k-modal-close-x">&times;</button>',
            '  </div>',
            '  <div class="t3k-signal-chain-bar">',
            '    <div class="t3k-chain-title">Signal Chain Architecture</div>',
            '    <div class="t3k-chain-blocks-wrapper">',
            '      <div class="t3k-chain-block active"><div class="t3k-block-badge">INPUT BLOCK</div><div class="t3k-block-name">1. Noise Gate</div></div>',
            '      <div class="t3k-chain-arrow">&rarr;</div>',
            '      <div class="t3k-chain-block active"><div class="t3k-block-badge">NEURAL CORE</div><div class="t3k-block-name t3k-ov-model">2. ' + mActive.name + '</div></div>',
            '      <div class="t3k-chain-arrow">&rarr;</div>',
            '      <div class="t3k-chain-block active"><div class="t3k-block-badge">CONVOLVER</div><div class="t3k-block-name t3k-ov-ir">3. ' + mActive.irName + '</div></div>',
            '      <div class="t3k-chain-arrow">&rarr;</div>',
            '      <div class="t3k-chain-block active"><div class="t3k-block-badge">MASTER EQ</div><div class="t3k-block-name">4. 3-Band Tone Stack</div></div>',
            '    </div>',
            '  </div>',
            '  <div class="t3k-browser-controls">',
            '    <div class="t3k-search-input-box">',
            '      <span class="t3k-search-icon">&#128269;</span>',
            '      <input type="text" class="t3k-search-input" id="t3k-ov-search" placeholder="Search captures (e.g. Martin D-45, Deluxe, AC30, Soldano)...">',
            '    </div>',
            '    <div class="t3k-filter-pills">',
            '      <button type="button" class="t3k-pill active" data-cat="all">All Tones</button>',
            '      <button type="button" class="t3k-pill" data-cat="Acoustic">Acoustic Guitars & Mics</button>',
            '      <button type="button" class="t3k-pill" data-cat="Clean">Clean Amps</button>',
            '      <button type="button" class="t3k-pill" data-cat="Crunch">Edge of Breakup</button>',
            '      <button type="button" class="t3k-pill" data-cat="High Gain">High Gain Leads</button>',
            '      <button type="button" class="t3k-pill" data-cat="Bass">Bass Rigs</button>',
            '    </div>',
            '  </div>',
            '  <div class="t3k-cards-grid" id="t3k-ov-grid"></div>',
            '</div>'
        ].join('\n');

        modal.html(html);

        function renderGrid(filterCat, searchStr) {
            var grid = modal.find('#t3k-ov-grid');
            grid.empty();

            var filtered = TONE_MODELS.filter(function (m) {
                var matchCat = (filterCat === 'all' || m.category === filterCat);
                var matchSearch = true;
                if (searchStr && searchStr.trim().length > 0) {
                    var s = searchStr.toLowerCase().trim();
                    matchSearch = (m.name.toLowerCase().indexOf(s) !== -1 ||
                                   m.desc.toLowerCase().indexOf(s) !== -1 ||
                                   m.author.toLowerCase().indexOf(s) !== -1);
                }
                return matchCat && matchSearch;
            });

            filtered.forEach(function (m) {
                var isCur = (m.id === currentModelId);
                var card = $([
                    '<div class="t3k-tone-card' + (isCur ? ' current-active' : '') + '" data-id="' + m.id + '">',
                    '  <div class="t3k-card-header">',
                    '    <div class="t3k-card-title">' + m.name + '</div>',
                    '    <span class="t3k-card-badge">' + m.category.toUpperCase() + '</span>',
                    '  </div>',
                    '  <div class="t3k-card-desc">' + m.desc + '</div>',
                    '  <div class="t3k-card-footer">',
                    '    <div class="t3k-card-author">Captured by ' + m.author + '</div>',
                    '    <div class="t3k-card-buttons">',
                    '      <button type="button" class="t3k-btn-preview" data-id="' + m.id + '">&#9658; Audition</button>',
                    '      <button type="button" class="t3k-btn-load' + (isCur ? ' loaded' : '') + '" data-id="' + m.id + '">' + (isCur ? 'Active &#10003;' : 'Load &rarr;') + '</button>',
                    '    </div>',
                    '  </div>',
                    '</div>'
                ].join(''));

                card.find('.t3k-btn-preview').on('click', function (e) {
                    e.stopPropagation();
                    playAudition(m.id);
                });

                card.find('.t3k-btn-load').on('click', function (e) {
                    e.stopPropagation();
                    setModelToPedal(m.id);
                    modal.find('.t3k-ov-model').text('2. ' + m.name);
                    modal.find('.t3k-ov-ir').text('3. ' + m.irName);
                    modal.find('.t3k-tone-card').removeClass('current-active');
                    card.addClass('current-active');
                    modal.find('.t3k-btn-load').removeClass('loaded').html('Load &rarr;');
                    $(this).addClass('loaded').html('Active &#10003;');
                });

                grid.append(card);
            });
        }

        renderGrid('all', '');

        modal.find('#t3k-ov-search').on('input', function () {
            var cat = modal.find('.t3k-pill.active').data('cat') || 'all';
            renderGrid(cat, $(this).val());
        });

        modal.find('.t3k-pill').on('click', function () {
            modal.find('.t3k-pill').removeClass('active');
            $(this).addClass('active');
            var search = modal.find('#t3k-ov-search').val();
            renderGrid($(this).data('cat'), search);
        });

        modal.find('#t3k-modal-close-x').on('click', function () {
            modal.fadeOut(150);
        });

        modal.off('click.bg').on('click.bg', function (e) {
            if ($(e.target).is('#' + overlayId)) modal.fadeOut(150);
        });

        modal.fadeIn(200);
    }

    function playAudition(modelId) {
        try {
            if (!audioCtx) audioCtx = new (window.AudioContext || window.webkitAudioContext)();
            var now = audioCtx.currentTime;
            var freqs = [196.00, 246.94, 293.66, 392.00, 493.88];
            freqs.forEach(function (f, i) {
                var osc = audioCtx.createOscillator();
                var gain = audioCtx.createGain();
                osc.type = (modelId < 4) ? 'triangle' : 'sawtooth';
                osc.frequency.setValueAtTime(f, now + i * 0.08);
                gain.gain.setValueAtTime(0.0001, now + i * 0.08);
                gain.gain.exponentialRampToValueAtTime(0.14, now + i * 0.08 + 0.02);
                gain.gain.exponentialRampToValueAtTime(0.0001, now + i * 0.08 + 0.85);
                osc.connect(gain);
                gain.connect(audioCtx.destination);
                osc.start(now + i * 0.08);
                osc.stop(now + i * 0.08 + 0.95);
            });
        } catch (err) {}
    }

    
    // Prevent MOD-UI draggable from intercepting button clicks
    pedal.find('#t3k-cloud-trigger-btn, #t3k-btn-prev, #t3k-btn-next').on('mousedown touchstart pointerdown', function (e) {
        e.stopPropagation();
    });

    pedal.find('#t3k-cloud-trigger-btn').off('click.t3k').on('click.t3k', function (e) {
        e.preventDefault(); e.stopPropagation();
        if (window.CyberTone3000Instance) {
            window.CyberTone3000Instance.open();
        } else {
            openOverlay();
        }
    });

    pedal.off('dblclick.t3k').on('dblclick.t3k', function (e) {
        if (!$(e.target).closest('.mod-knob, .mod-footswitch, #t3k-btn-prev, #t3k-btn-next').length) {
            if (window.CyberTone3000Instance) {
                window.CyberTone3000Instance.open();
            } else {
                openOverlay();
            }
        }
    });

    // ── Handle incoming port changes from host ───────────────────────────────
    function handle_event(symbol, value) {
        if (symbol === 'model') {
            updateOled(Math.round(value));
        }
    }

    if (event.type === 'start') {
        var ports = event.ports;
        if (ports) {
            for (var p in ports) {
                if (ports.hasOwnProperty(p) && ports[p].symbol === 'model') {
                    handle_event('model', ports[p].value);
                }
            }
        }
    } else if (event.type === 'change') {
        handle_event(event.symbol, event.value);
    }
}
