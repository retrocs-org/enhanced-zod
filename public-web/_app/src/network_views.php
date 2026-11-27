<?php
declare(strict_types=1);
if (!defined('ZODEE_INTERNAL') && PHP_SAPI !== 'cli') { http_response_code(404); exit; }
require_once __DIR__.'/api_catalog.php';
function render_network_services(ZodService $service): void {
    try {
        $servers = $service->servers();
        $totals = $service->totals();
    } catch (PDOException $error) {
        error_log('War Room statistics unavailable ['.$error->getCode().']');
        $servers = $totals = null;
    }
    ?><section class="shell section page-section"><?php title_block('MULTIPLAYER HUB','The War Room','Find a game, follow public battles, and see who holds territory in Planetary Conquest.'); ?>
    <article class="panel panel-pad war-room-mode">
        <div><h2>Planetary Conquest</h2><p>See the current season’s planetary maps, sector owners, and faction standings.</p></div>
        <a class="button button-primary" href="/war-room/conquest">View Planetary Conquest</a>
    </article>
    <?php if ($totals !== null): ?>
    <div class="stat-grid"><?php foreach([['PUBLIC SERVERS',count($servers)],['PLAYERS ONLINE',array_sum(array_column($servers,'players'))],['REGISTERED COMMANDERS',$totals['accounts']],['RECORDED BATTLES',$totals['matches']]] as [$label,$value]): ?><div class="stat-card"><span class="micro"><?=e($label)?></span><strong><?=number($value)?></strong></div><?php endforeach ?></div>
    <?php endif ?>
    <div class="info-grid">
    <?php foreach([
        ['Server directory','Live lobbies, player counts, connection addresses and join tickets. Listings expire after 90 seconds without a heartbeat.','/servers','Browse servers'],
        ['Match history','Completed public battles, teams and privacy-aware participant results.','/match-history','Browse match records'],
        ['Accounts & access','Your commander identity, public match history and single-use join tickets. Private games remain independent.','/account','Your account'],
        ['Battle records','Public-server results power commander profiles, leaderboards and map statistics. Private matches are not uploaded.','/statistics','Network statistics'],
        ['Commander leaderboard','See public match records and find commanders by their victories, units destroyed and captured territory.','/leaderboard','View leaderboard'],
    ] as [$title,$description,$href,$label]): ?><article class="panel panel-pad"><h2><?=e($title)?></h2><p><?=e($description)?></p><a class="button" href="<?=e($href)?>"><?=e($label)?></a></article><?php endforeach ?>
    </div><section class="section"><div class="section-heading"><h2>On the network now.</h2><a class="text-link" href="/servers">Full server directory</a></div><div class="panel"><?php if ($servers === null): ?><p class="panel-pad">Live server listings are temporarily unavailable.</p><?php else: server_table($servers); endif ?></div></section>
    <div class="panel panel-pad"><h2>Hosting a server?</h2><p>The dedicated-server agent announces account-owned public servers to this website. Sign in on the dedicated host with --login, then manage administrators under My servers. Accounts and statistics stay in MariaDB; game clients never connect to the database.</p><div class="button-row"><a class="button" href="/guide#hosting">Hosting guide</a><a class="text-link" href="/leaderboard">Commander leaderboard</a></div></div></section><?php
}
function render_api_browser(): void {
    ?><section class="shell section page-section"><?php title_block('NETWORK / DEVELOPER API','Explore the API.','Inspect public JSON from this website. The explorer only runs public GET requests; account and operator actions are documented below.'); ?>
    <div class="panel panel-pad api-explorer"><form id="api-explorer"><label for="api-endpoint">Public endpoint</label><div class="button-row"><select id="api-endpoint" name="endpoint"><?php foreach(api_catalog() as [$method,$path,$access,$description,$browse]): if(!$browse)continue; ?><option value="<?=e($path)?>"><?=e($method.' '.$path)?></option><?php endforeach ?></select><button class="button button-primary" type="submit">Send request</button><a id="api-raw" class="text-link" href="/healthz">Open JSON</a></div></form><p id="api-status" role="status" aria-live="polite">Choose an endpoint, then send a request.</p><pre class="api-output" tabindex="0" aria-label="JSON response"><code id="api-output">No request sent.</code></pre><noscript><p>JavaScript is disabled. Open the public endpoint links below to view their JSON responses.</p></noscript></div>
    <section class="section"><div class="section-heading"><h2>Endpoint reference.</h2></div><div class="panel table-scroll"><table class="api-reference"><thead><tr><th>Method &amp; path</th><th>Access</th><th>Purpose</th></tr></thead><tbody><?php foreach(api_catalog() as [$method,$path,$access,$description,$browse]): ?><tr><td><strong class="mono"><?=e($method)?></strong><br><?php if($browse): ?><a class="text-link mono" href="<?=e($path)?>"><?=e($path)?></a><?php else: ?><code><?=e($path)?></code><?php endif ?></td><td><?=e($access)?></td><td><?=e($description)?></td></tr><?php endforeach ?></tbody></table></div></section>
    <div class="info-grid"><article class="panel panel-pad"><h2>Account requests.</h2><p>JSON requests use <code>Content-Type: application/json</code>. Sign in through the account page or login endpoint. State-changing browser requests must originate from this site. Session cookies are never displayed by this explorer.</p><a class="button" href="/account">Account settings</a></article><article class="panel panel-pad"><h2>Dedicated servers.</h2><p>Heartbeat, ticket redemption and match-result submission require an approved server bearer key. Configure the dedicated-server agent with its private key file. Keep keys out of URLs and public pages.</p><a class="button" href="/guide#hosting">Server setup</a></article></div></section><?php
}
