<?php
declare(strict_types=1);
if (!defined('ZODEE_INTERNAL') && PHP_SAPI !== 'cli') { http_response_code(404); exit; }

const CONQUEST_BIOMES = ['desert' => 'Desert', 'volcanic' => 'Volcanic', 'arctic' => 'Arctic', 'jungle' => 'Jungle', 'city' => 'City'];
const CONQUEST_OWNERS = ['red' => 'Red', 'blue' => 'Blue', 'neutral' => 'Unclaimed'];

function conquest_name(mixed $name): bool {
    return is_string($name) && trim($name) !== '' && strlen($name) <= 100;
}

function conquest_timestamp(mixed $timestamp): bool {
    if (!is_string($timestamp) || !preg_match('/\A\d{4}-\d\d-\d\dT\d\d:\d\d:\d\dZ\z/', $timestamp)) return false;
    $date = DateTimeImmutable::createFromFormat('!Y-m-d\TH:i:s\Z', $timestamp, new DateTimeZone('UTC'));
    return $date !== false && $date->format('Y-m-d\TH:i:s\Z') === $timestamp;
}

function conquest_snapshot(string $file): ?array {
    if ($file === '' || !is_file($file)) return null;
    $json = @file_get_contents($file, false, null, 0, 131073);
    if ($json === false || strlen($json) > 131072) return null;
    $season = json_decode($json, true, 16);
    if (!is_array($season) || !conquest_name($season['season'] ?? null)) return null;
    $planetData = $season['planets'] ?? null;
    $updated = $season['updated_at'] ?? null;
    if (!is_array($planetData) || !array_is_list($planetData) || count($planetData) < 1 ||
        count($planetData) > 8 || !conquest_timestamp($updated)) return null;

    $planets = [];
    foreach ($planetData as $planet) {
        if (!is_array($planet) || !conquest_name($planet['name'] ?? null)) return null;
        $biome = $planet['biome'] ?? '';
        if (!is_string($biome) || ($biome !== '' && !isset(CONQUEST_BIOMES[$biome]))) return null;
        $rows = $planet['rows'] ?? null;
        if (!is_array($rows) || !array_is_list($rows) || count($rows) < 1 || count($rows) > 12) return null;
        $counts = array_fill_keys(array_keys(CONQUEST_OWNERS), 0);
        $width = null;
        foreach ($rows as $row) {
            if (!is_array($row) || !array_is_list($row) || count($row) < 1 || count($row) > 12) return null;
            $width ??= count($row);
            if (count($row) !== $width) return null;
            foreach ($row as $sector) {
                if ($sector === null) continue;
                if (!is_array($sector) || !conquest_name($sector['name'] ?? null)) return null;
                $owner = $sector['owner'] ?? null;
                if (!is_string($owner) || !isset($counts[$owner])) return null;
                if (isset($sector['map']) && !conquest_name($sector['map'])) return null;
                $counts[$owner]++;
            }
        }
        if (array_sum($counts) === 0) return null;
        $planets[] = ['name' => $planet['name'], 'rows' => $rows, 'counts' => $counts, 'biome' => $biome];
    }
    return ['season' => $season['season'], 'updated_at' => $updated, 'planets' => $planets,
        'battles' => $season['battles'] ?? []];
}

function render_conquest(): void {
    $season = conquest_snapshot(env_value('ZOD_CONQUEST_SNAPSHOT'));
    $seasonCounts = array_fill_keys(array_keys(CONQUEST_OWNERS), 0);
    foreach ($season['planets'] ?? [] as $planet) {
        foreach ($planet['counts'] as $owner => $count) $seasonCounts[$owner] += $count;
    }
    $seasonTotal = array_sum($seasonCounts);
    $planetNumber = filter_var($_GET['planet'] ?? null, FILTER_VALIDATE_INT, [
        'options' => ['min_range' => 1, 'max_range' => count($season['planets'] ?? [])],
    ]);
    $selectedIndex = $planetNumber === false ? null : $planetNumber - 1;
    $selectedPlanet = $selectedIndex === null ? null : $season['planets'][$selectedIndex];
    ?>
    <section class="shell section page-section conquest">
        <h1>Planetary Conquest</h1>
        <h2 id="territory">Territory</h2>
        <?php if ($season === null): ?>
            <p>No season map is available. Season standings will appear when a snapshot is published.</p>
            <section aria-labelledby="conquest-terrain">
                <h3 id="conquest-terrain">Terrain types</h3>
                <ul class="conquest-atlas">
                    <?php foreach (CONQUEST_BIOMES as $biome => $label): ?>
                        <li><span class="conquest-globe biome-<?=$biome?>" aria-hidden="true"></span><span><?=$label?></span></li>
                    <?php endforeach; ?>
                </ul>
            </section>
        <?php else: ?>
            <p class="page-description">
                <?=e($season['season'])?> · Updated
                <time datetime="<?=e($season['updated_at'])?>"><?=e(stamp($season['updated_at']))?></time>
            </p>
            <section aria-labelledby="conquest-standings">
                <h2 id="conquest-standings">Season standings</h2>
                <p><?=number(count($season['planets']))?> planets · <?=number($seasonTotal)?> sectors</p>
                <ul class="conquest-control" aria-label="Season control">
                    <?php foreach (CONQUEST_OWNERS as $owner => $label): $count = $seasonCounts[$owner]; ?>
                        <li class="conquest-<?=$owner?>"><strong><?=$label?></strong>
                            <span><?=number($count)?> / <?=number($seasonTotal)?> sectors</span>
                            <meter min="0" max="<?=$seasonTotal?>" value="<?=$count?>" aria-label="<?=$label?> season sectors"></meter>
                            <span><?=round(100 * $count / $seasonTotal, 1)?>%</span>
                        </li>
                    <?php endforeach; ?>
                </ul>
            </section>
            <nav aria-label="Season planets">
                <ul class="conquest-atlas conquest-atlas-season">
                    <?php foreach ($season['planets'] as $index => $planet): $total = array_sum($planet['counts']); ?>
                        <li>
                            <a href="?planet=<?=$index + 1?>#planet-map" <?=$selectedIndex === $index ? 'aria-current="true"' : ''?>>
                                <span class="conquest-globe biome-<?=$planet['biome']?>" aria-hidden="true"></span>
                                <strong><?=e($planet['name'])?></strong>
                                <span class="conquest-atlas-control">
                                    <?php foreach (CONQUEST_OWNERS as $owner => $label): ?>
                                        <span class="conquest-<?=$owner?>"><?=$label?> <?=round(100 * $planet['counts'][$owner] / $total, 1)?>%</span>
                                    <?php endforeach; ?>
                                </span>
                            </a>
                        </li>
                    <?php endforeach; ?>
                </ul>
            </nav>
            <?php if ($selectedPlanet !== null):
                $planet = $selectedPlanet;
                $index = $selectedIndex;
                $total = array_sum($planet['counts']);
                $fullControl = array_search($total, $planet['counts'], true);
                $control = match ($fullControl) {
                    'red', 'blue' => CONQUEST_OWNERS[$fullControl].' controls all sectors',
                    'neutral' => 'Unclaimed',
                    default => 'Split control',
                };
            ?>
                <section class="conquest-planet" id="planet-map" aria-labelledby="conquest-selected-planet">
                    <h3 id="conquest-selected-planet"><?=e($planet['name'])?></h3>
                    <?php if ($planet['biome'] !== ''): ?><p><?=CONQUEST_BIOMES[$planet['biome']]?> terrain</p><?php endif; ?>
                    <p><?=e($control)?> · <?=number($total - $planet['counts']['neutral'])?> of <?=number($total)?> sectors held (<?=round(100 * ($total - $planet['counts']['neutral']) / $total, 1)?>%)</p>
                    <ul class="conquest-control" aria-label="Planet control">
                        <?php foreach (CONQUEST_OWNERS as $owner => $label): $count = $planet['counts'][$owner]; ?>
                            <li class="conquest-<?=$owner?>"><strong><?=$label?></strong>
                                <span><?=number($count)?> / <?=number($total)?> sectors</span>
                                <meter min="0" max="<?=$total?>" value="<?=$count?>" aria-label="<?=$label?> sectors"></meter>
                                <span><?=round(100 * $count / $total, 1)?>%</span>
                            </li>
                        <?php endforeach; ?>
                    </ul>
                    <div class="conquest-globe conquest-globe-map biome-<?=$planet['biome']?>">
                        <table aria-label="<?=e($planet['name'])?> planetary map">
                            <tbody>
                                <?php foreach ($planet['rows'] as $y => $row): ?>
                                    <tr>
                                        <?php foreach ($row as $x => $sector): ?>
                                            <td><?php if ($sector !== null): ?>
                                                <a class="conquest-<?=$sector['owner']?>" href="#sector-<?=$index?>-<?=$y?>-<?=$x?>"
                                                    aria-label="<?=e($sector['name'].' · '.CONQUEST_OWNERS[$sector['owner']])?>"
                                                    title="<?=e($sector['name'].' · '.CONQUEST_OWNERS[$sector['owner']])?>"></a>
                                            <?php endif; ?></td>
                                        <?php endforeach; ?>
                                    </tr>
                                <?php endforeach; ?>
                            </tbody>
                        </table>
                    </div>
                    <div class="table-scroll" tabindex="0" role="region" aria-label="<?=e($planet['name'])?> territory map">
                        <table class="conquest-map" aria-label="<?=e($planet['name'])?> territory ownership">
                            <tbody>
                                <?php foreach ($planet['rows'] as $y => $row): ?>
                                    <tr>
                                        <?php foreach ($row as $x => $sector): ?>
                                            <?php if ($sector === null): ?>
                                                <td class="conquest-void"></td>
                                            <?php else: ?>
                                                <td class="conquest-<?=$sector['owner']?>" id="sector-<?=$index?>-<?=$y?>-<?=$x?>">
                                                    <strong><?=e($sector['name'])?></strong>
                                                    <span><?=CONQUEST_OWNERS[$sector['owner']]?></span>
                                                    <?php if (isset($sector['map'])): ?><small>Battle map: <?=e($sector['map'])?></small><?php endif; ?>
                                                </td>
                                            <?php endif; ?>
                                        <?php endforeach; ?>
                                    </tr>
                                <?php endforeach; ?>
                            </tbody>
                        </table>
                    </div>
                </section>
            <?php else: ?>
                <p>Select a planet to see its territory map and sector owners.</p>
            <?php endif; ?>
        <?php endif; ?>
        <?php render_conquest_battles($season); ?>
        <section class="conquest-planet panel panel-pad" aria-labelledby="conquest-rules">
            <h2 id="conquest-rules">Proposed mode rules</h2>
            <p>Red and Blue fight for neighboring sectors. In the proposed test season,
                an attacker win removes one of a sector’s three control points, a defender
                win restores one, and a draw leaves control unchanged. Ownership transfers
                at zero control points.</p>
            <p>Capturing the opposing capital ends the season. Otherwise, the faction
                holding the most sectors at the deadline wins.</p>
        </section>
    </section>
    <?php
}


function render_conquest_battles(?array $season): void {
    $battles = $season['battles'] ?? [];
    $results = ['red' => 'Red victory', 'blue' => 'Blue victory', 'draw' => 'Draw'];
    $history = [];
    if (is_array($battles) && array_is_list($battles)) {
        foreach (array_slice($battles, 0, 200) as $battle) {
            if (!is_array($battle) || !conquest_timestamp($battle['finished_at'] ?? null) ||
                !conquest_name($battle['planet'] ?? null) || !conquest_name($battle['sector'] ?? null) ||
                !is_string($battle['result'] ?? null) || !isset($results[$battle['result']])) continue;
            $history[] = $battle;
        }
    }
    usort($history, static fn(array $a, array $b): int => strcmp($b['finished_at'], $a['finished_at']));
    ?>
    <section class="conquest-planet" aria-labelledby="battle-history">
        <h2 id="battle-history">Battle history</h2>
        <?php if ($season !== null): ?><p><?=e($season['season'])?></p><?php endif; ?>
        <?php if ($history === []): ?>
            <p>No conquest battles have been published for the current season.</p>
        <?php else: ?>
            <div class="panel table-scroll">
                <table>
                    <thead><tr><th scope="col">Finished</th><th scope="col">Planet</th><th scope="col">Sector</th><th scope="col">Result</th></tr></thead>
                    <tbody>
                        <?php foreach ($history as $battle): ?>
                            <tr>
                                <td><time datetime="<?=e($battle['finished_at'])?>"><?=e(stamp($battle['finished_at']))?></time></td>
                                <td><?=e($battle['planet'])?></td><td><?=e($battle['sector'])?></td>
                                <td><?=e($results[$battle['result']])?></td>
                            </tr>
                        <?php endforeach; ?>
                    </tbody>
                </table>
            </div>
        <?php endif; ?>
    </section>
    <?php
}
