<?php
declare(strict_types=1);
if (!defined('ZODEE_INTERNAL') && PHP_SAPI !== 'cli') { http_response_code(404); exit; }

function feature_section(): void {
    $history=[
        ['1996','Original game','Z','The Bitmap Brothers','Territory control, robots and factories: the game that started it all.','original-z','original'],
        ['2010','Open-source remake','Zod Engine','Michael Bok / Nighsoft','A C++ / SDL remake with multiplayer, bots and map tools.','zod-engine','engine'],
        ['JUN 2026','SDL3 fork','fenio / zod','Modernizing the engine','Native SDL3 rendering, input and audio, plus modern build support.','sdl3-fork','sdl'],
        ['NOV 2026','Our community fork','zod-enhanced','Play, create and preserve','Campaign authoring, integrated editing, recordings and expanded player tools.','enhanced','enhanced'],
    ];
    $features=[
        ['CAMPAIGNS','Keep the original. Add new stories.','The Original Campaign remains a separate choice, with its own progress. Community packs can introduce their own mission sequences and presentation.',[
            'Browse completed missions and the next mission in line; replay completed battles.',
            'Track campaign records and medals, and revisit unlocked cinematics.',
            'Use mission briefings, character portraits and optional user-owned cutscenes.',
        ]],
        ['CAMPAIGN AUTHORING','Give missions more to do.','Build a campaign around objectives and events, rather than just a collection of maps.',[
            'Author timed, capture and destruction events with reinforcement groups.',
            'Add escort objectives and choose different win/loss mission branches.',
            'Import, export and update campaign packs with dependency and version checks.',
        ]],
        ['INTEGRATED MAP EDITOR','Create without leaving the game.','Work in the full-screen editor and playtest maps in an isolated run before returning to your project.',[
            'Open original levels as starting points and save edited copies separately.',
            'Use terrain smoothing, road/river tools, templates and reusable object groups.',
            'Edit groups, copy terrain and recover work with undo/redo and recovery tools.',
        ]],
        ['LOCAL BATTLE RECORDINGS','Return to the decisive moment.','Record local battle state for later review, without uploading it to a service.',[
            'Pause, change playback speed and seek through a recording.',
            'Switch team views using the information captured during that recording.',
            'Recordings require the matching engine version; they are not video files.',
        ]],
        ['INTERFACE, AUDIO & ACCESSIBILITY','Set up a comfortable command post.','A broader set of interface and control options makes the game easier to fit to your display and input preferences.',[
            'Scale menus, remap keys and configure controller navigation and dead zones.',
            'Adjust music and sound effects independently with live volume sliders.',
            'Choose larger cursors, team symbols, subtitle settings and accessibility presets.',
        ]],
        ['ORIGINAL MEDIA & CINEMATICS','Bring your own copy of Z.','The offline importer converts supported original DOS disc images into per-user game data.',[
            'Import ISO or BIN/CUE media with progress and cancellation.',
            'Play videos inside the game with scaling, subtitle and volume controls.',
            'Campaigns and private/LAN play do not require a cloud account.',
        ]],
        ['HOSTING & COMMUNITY','Private games or a public battlefield.','Dedicated hosting and optional web services support different ways to play together.',[
            'Discover LAN games, connect directly or host a headless Linux server.',
            'Private servers welcome guests; approved public servers use accounts and join tickets.',
            'The PHP/MariaDB website provides public listings, profiles and unranked match statistics.',
        ]],
        ['RECOVERY & DIAGNOSTICS','Keep progress. Find problems.','Save integrity checks, recovery paths and local diagnostics support longer campaigns and creator workflows.',[
            'Checksummed saves and backup generations help detect damage and recover earlier states.',
            'Save previews and explicit overwrite actions make slot management clearer.',
            'Opt-in profiling and diagnostic exports help investigate performance and gameplay issues.',
        ]],
    ];
    ?><section class="shell section page-section features-page history-page">
    <?php title_block('THE GAME / THE ENGINE / THE NEXT CHAPTER','From Z to zod-enhanced.','A classic strategy game, kept alive through open-source work. Follow the projects that brought us here, then explore what our fork adds.'); ?>
    <nav class="history-jumps" aria-label="On this page"><a href="#lineage">Project timeline</a><a href="#original-z">Original Z</a><a href="#zod-engine">Zod Engine</a><a href="#sdl3-fork">SDL3 modernization</a><a href="#enhanced">Our additions</a></nav>

    <section class="lineage-section" id="lineage" aria-labelledby="lineage-title">
        <div class="section-heading"><div><span class="eyebrow">A SHARED HISTORY</span><h2 id="lineage-title">Four chapters. One lineage.</h2></div></div>
        <ol class="project-timeline"><?php foreach($history as [$date,$kind,$name,$by,$summary,$target,$tone]): ?><li class="timeline-stage stage-<?=e($tone)?>"><a href="#<?=e($target)?>"><span class="timeline-date"><?=e($date)?></span><span class="timeline-mark" aria-hidden="true"></span><span class="timeline-kind"><?=e($kind)?></span><h3><?=e($name)?></h3><span class="timeline-by"><?=e($by)?></span><p><?=e($summary)?></p><span class="timeline-more">Explore this chapter</span></a></li><?php endforeach ?></ol>
        <p class="timeline-caption">This timeline follows the direct lineage behind zod-enhanced. The fork dates mark public repository creation; the original engine date marks its SourceForge registration.</p>
    </section>

    <section class="history-chapter" id="original-z" aria-labelledby="original-title">
        <div class="chapter-heading"><span class="eyebrow">1996 / THE BITMAP BROTHERS</span><h2 id="original-title">The original Z.</h2><span class="chapter-note">The creative foundation</span></div>
        <div class="chapter-body"><p>Z puts robot armies into a fight for territory. Capturing sectors and their factories drives production, so holding the map matters more than gathering resources.</p><p>Its characters, humour, artwork, music and cinematic personality belong to the original game and its creators. The engine projects that followed build on that foundation.</p><ul class="heritage-points"><li>Capture territories and control factories.</li><li>Command robot squads and crew neutral vehicles.</li><li>Push the front line toward the enemy fort.</li></ul><a class="text-link" href="/credits#original">Original creators and credits</a></div>
    </section>

    <section class="history-chapter" id="zod-engine" aria-labelledby="engine-title">
        <div class="chapter-heading"><span class="eyebrow">2010 / MICHAEL BOK · NIGHSOFT</span><h2 id="engine-title">Opening up the engine.</h2><span class="chapter-note">The open-source foundation</span></div>
        <div class="chapter-body"><p>Zod Engine recreates Z in C++ using SDL and makes the engine available under GPLv3. Its project page emphasizes multiplayer and expanding how maps can be played.</p><ul class="heritage-points"><li>Multiplayer games, bot opponents and shared maps.</li><li>Cross-platform engine work and map-editing foundations.</li><li>The gameplay code on which later community forks build.</li></ul><a class="text-link" href="https://zod.sourceforge.net/">Original Zod Engine project</a></div>
    </section>

    <section class="history-chapter" id="sdl3-fork" aria-labelledby="sdl-title">
        <div class="chapter-heading"><span class="eyebrow">2026 / FENIO &amp; CONTRIBUTORS</span><h2 id="sdl-title">Bringing Zod to SDL3.</h2><span class="chapter-note">Our direct upstream fork</span></div>
        <div class="chapter-body"><p>fenio’s fork focuses on building and playing the engine on current systems. This is the modernization that zod-enhanced inherits.</p><ul class="heritage-points"><li>Native SDL3 rendering, input, threads and audio.</li><li>Scaled, HiDPI-aware output, trackpad panning and rendering/input fixes.</li><li>CMake builds and support for current macOS, Linux and Windows systems.</li></ul><a class="text-link" href="https://github.com/fenio/zod">Explore the SDL3 fork</a></div>
    </section>

    <section class="enhanced-chapter" id="enhanced" aria-labelledby="enhanced-title">
        <div class="enhanced-heading"><span class="eyebrow">2026 / ZOD ENGINE ENHANCED</span><h2 id="enhanced-title">Our chapter: more ways to play and create.</h2><p>zod-enhanced keeps those foundations and extends campaign creation, editing, playback and everyday usability. These are the additions and workflows developed in this fork.</p></div>
        <div class="enhancement-grid"><?php foreach($features as [$label,$title,$description,$items]): ?><article class="feature-card"><span class="eyebrow"><?=e($label)?></span><h3><?=e($title)?></h3><p><?=e($description)?></p><ul class="feature-details"><?php foreach($items as $item): ?><li><?=e($item)?></li><?php endforeach ?></ul></article><?php endforeach ?></div>
        <p class="features-note">An evolving community fork. See the <a href="https://github.com/retrocs-org/zodee/blob/main/docs/RELEASE-NOTES.md">release notes</a> for details and current limitations, or visit <a href="/credits">Credits</a> for the full acknowledgements.</p>
    </section>

    <details class="history-sources"><summary>Sources and date notes</summary><ul>
        <li>The original <a href="https://zod.sourceforge.net/">Zod Engine site</a> identifies Z as the 1996 Bitmap Brothers game and describes its remake.</li>
        <li><a href="https://sourceforge.net/projects/zod/">SourceForge</a> records Zod Engine’s registration on 16 January 2010. This is not a claim about its first playable release.</li>
        <li><a href="https://github.com/fenio/zod#readme">fenio’s README</a> describes the SDL3 changes. Its <a href="https://api.github.com/repos/fenio/zod">repository metadata</a> records creation on 9 June 2026.</li>
        <li>Our <a href="https://api.github.com/repos/retrocs-org/zodee">repository metadata</a> records creation in November 2026. The displayed name is now zod-enhanced; the public repository address is retained.</li>
    </ul></details>
    </section><?php
}
