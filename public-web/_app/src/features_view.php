<?php
declare(strict_types=1);
if (!defined('ZODEE_INTERNAL') && PHP_SAPI !== 'cli') { http_response_code(404); exit; }

function feature_section(): void {
    $history=[
        ['1996','Original game','Z','The Bitmap Brothers','Robot armies, territory control and factory production.','https://www.mobygames.com/game/346/z/','original','Z on MobyGames'],
        ['2010','Open-source remake','Zod Engine','Michael Bok / Nighsoft','Multiplayer, bot opponents and custom maps.','https://zod.sourceforge.net/','engine','Zod Engine website'],
        ['JUN 2026','SDL3 fork','fenio / zod','Modernizing the engine','Native SDL3 and modern macOS, Windows and Linux support.','https://github.com/fenio/zod','sdl','Visit the SDL3 fork'],
        ['NOV 2026','Our community fork','zod-enhanced','Play together. Create more.','Online services, server discovery and expanded map and campaign editors.','#enhanced','enhanced','See our multiplayer & creator features'],
    ];
    $features=[
        ['MULTIPLAYER','Find a battle. Bring your friends.','Play over LAN, connect directly or join a public server. Host private games with guests, or run a headless Linux dedicated server.','/servers','Browse servers'],
        ['NETWORKING & COMMUNITY','A home for players and servers.','Use the networking portal to discover public servers, manage opt-in friends and server invitations, and explore profiles, leaderboards and unranked match statistics. Public servers use accounts and join tickets; private and LAN play work without a cloud account.','/network','Explore the online portal'],
        ['MAP & CAMPAIGN EDITORS','Build more than a battlefield.','Create and playtest in the integrated full-screen editor, with terrain tools, object groups and undo/redo. Campaign Studio adds mission sequences, objectives and briefings. Open original maps as starting points and save your own copies.','https://github.com/retrocs-org/zodee/blob/main/docs/Campaigns.md','Explore the creator tools'],
        ['macOS · WINDOWS · LINUX','Classic strategy on modern systems.','Built on the upstream SDL3 port, with modern desktop builds, scalable menus and configurable controls. Import your own original game data, then play the Original Campaign or add community campaigns.','/downloads','Get the game'],
    ];
    ?><section class="shell section page-section features-page history-page">
    <?php title_block('THE GAME / THE ENGINE / THE NEXT CHAPTER','From Z to zod-enhanced.','From the original robot RTS to modern multiplayer and creator tools.'); ?>
    <section class="lineage-section" id="lineage" aria-labelledby="lineage-title">
        <div class="section-heading"><div><span class="eyebrow">A SHARED HISTORY</span><h2 id="lineage-title">Four chapters. One lineage.</h2></div></div>
        <ol class="project-timeline"><?php foreach($history as [$date,$kind,$name,$by,$summary,$target,$tone,$linkLabel]): ?><li class="timeline-stage stage-<?=e($tone)?>"><a href="<?=e($target)?>"><span class="timeline-date"><?=e($date)?></span><span class="timeline-mark" aria-hidden="true"></span><span class="timeline-kind"><?=e($kind)?></span><h3><?=e($name)?></h3><span class="timeline-by"><?=e($by)?></span><p><?=e($summary)?></p><span class="timeline-more"><?=e($linkLabel)?></span></a></li><?php endforeach ?></ol>
        <p class="timeline-caption">This timeline follows the direct lineage behind zod-enhanced. The fork dates mark public repository creation; the original engine date marks its SourceForge registration.</p>
    </section>

    <section class="enhanced-chapter" id="enhanced" aria-labelledby="enhanced-title">
        <div class="enhanced-heading"><span class="eyebrow">2026 / ZOD ENGINE ENHANCED</span><h2 id="enhanced-title">Play together. Make it your own.</h2><p>Our fork builds on that history with connected multiplayer, a community portal and more capable editors — on macOS, Windows and Linux.</p></div>
        <div class="enhancement-grid"><?php foreach($features as [$label,$title,$description,$href,$linkLabel]): ?><article class="feature-card"><span class="eyebrow"><?=e($label)?></span><h3><?=e($title)?></h3><p><?=e($description)?></p><a class="text-link" href="<?=e($href)?>"><?=e($linkLabel)?></a></article><?php endforeach ?></div>
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
