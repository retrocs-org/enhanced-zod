<?php
declare(strict_types=1);
if (!defined('ZODEE_INTERNAL') && PHP_SAPI !== 'cli') { http_response_code(404); exit; }

const CONQUEST_BIOMES = ['desert' => 'Desert', 'volcanic' => 'Volcanic', 'arctic' => 'Arctic', 'jungle' => 'Jungle', 'city' => 'City'];
const CONQUEST_OWNERS = ['red' => 'Red', 'blue' => 'Blue', 'neutral' => 'Neutral'];

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
                if (isset($sector['start']) && !in_array($sector['start'], ['red', 'blue'], true)) return null;
                $counts[$owner]++;
            }
        }
        if (array_sum($counts) === 0) return null;
        $planets[] = ['name' => $planet['name'], 'rows' => $rows, 'counts' => $counts, 'biome' => $biome];
    }
    return ['season' => $season['season'], 'updated_at' => $updated, 'planets' => $planets,
        'battles' => $season['battles'] ?? []];
}

function conquest_preview_planets(): array {
    $regions = [
        'desert' => ['Dune Sea', 'Salt Flats', 'Red Mesa', 'Dry Basin', 'Canyon Gate', 'Sun Ridge', 'Sand Gate', 'Dust Hollow', 'Copper Wash', 'Wadi Crossing', 'Spice Dunes', 'Stone Arch'],
        'volcanic' => ['Ash Field', 'Caldera Rim', 'Lava Flats', 'Basalt Reach', 'Ember Pass', 'Cinder Ridge', 'Obsidian Bank', 'Sulfur Run', 'Crater Gate', 'Fumarole Field', 'Magma Shelf', 'Black Summit'],
        'arctic' => ['Ice Shelf', 'Frost Basin', 'Snow Pass', 'Glacier Edge', 'Frozen Lake', 'White Ridge', 'Aurora Point', 'Icefall', 'Drift Valley', 'Polar Flats', 'Shiver Coast', 'Blue Glacier'],
        'jungle' => ['Canopy Ridge', 'River Delta', 'Fern Basin', 'Mangrove Bay', 'Floodplain', 'Green Pass', 'Orchid Vale', 'Mossbank', 'Vine Bridge', 'Kapok Grove', 'Lagoon Reach', 'Rain Hollow'],
        'city' => ['North District', 'Rail Yard', 'Docklands', 'Market Ward', 'Power Station', 'South Gate', 'Civic Center', 'Foundry Row', 'Old Quarter', 'Canal Locks', 'Transit Hub', 'East Heights'],
    ];
    $layouts = [
        'desert' => ['.xxx..', 'xxxxxx', '..xxx.'],
        'volcanic' => ['.xx.', 'xxxx', 'xxxx', '.xx.'],
        'arctic' => ['xx.', 'xx.', 'xxx', 'xxx', '.xx'],
        'jungle' => ['...xx', '..xxx', '.xxxx', 'xxx..'],
        'city' => ['xxxx', 'xxxx', 'xxxx'],
    ];
    $planets = [];
    foreach ($regions as $biome => $names) {
        $owners = ['red', 'neutral', 'red', 'red', 'neutral', 'blue', 'neutral', 'red', 'blue', 'blue', 'neutral', 'blue'];
        $sectors = array_map(static fn(string $name, string $owner): array =>
            ['name' => $name, 'owner' => $owner], $names, $owners);
        $sectors[0]['start'] = 'red';
        $sectors[count($sectors) - 1]['start'] = 'blue';
        $rows = [];
        $sectorIndex = 0;
        foreach ($layouts[$biome] as $layoutRow) {
            $row = [];
            foreach (str_split($layoutRow) as $cell) {
                $row[] = $cell === 'x' ? $sectors[$sectorIndex++] : null;
            }
            $rows[] = $row;
        }
        $planets[] = [
            'name' => CONQUEST_BIOMES[$biome].' world',
            'biome' => $biome,
            'rows' => $rows,
            'counts' => array_count_values($owners),
        ];
    }
    return $planets;
}

function conquest_control_bar(array $counts): void {
    $total = array_sum($counts);
    $segments = [];
    $labels = [];
    $offset = 0;
    foreach (['red', 'neutral', 'blue'] as $owner) {
        $count = $counts[$owner];
        $percent = round(100 * $count / $total, 1);
        $labels[] = CONQUEST_OWNERS[$owner].' '.$percent.'%';
        if ($count === 0) continue;
        $segments[] = [
            'owner' => $owner, 'percent' => $percent,
            'start' => 100 * $offset / $total, 'width' => 100 * $count / $total,
        ];
        $offset += $count;
    }
    ?>
    <svg class="conquest-control-bar" role="img" aria-label="<?=e(implode(', ', $labels))?>">
        <title><?=e(implode(', ', $labels))?></title>
        <?php foreach ($segments as $segment): ?>
            <rect class="conquest-<?=$segment['owner']?>" x="<?=$segment['start']?>%" width="<?=$segment['width']?>%" height="100%"/>
            <svg x="<?=$segment['start']?>%" width="<?=$segment['width']?>%" height="100%" viewBox="0 0 40 20">
                <text x="20" y="14"><?=$segment['percent']?>%</text>
            </svg>
        <?php endforeach; ?>
    </svg>
    <?php
}

function conquest_map_point(string $planet, string $type, int $x, int $y): string {
    // Neighbors use the same corners and edge midpoints.
    $hash = crc32("$planet:$type:$x:$y");
    $dx = ($hash % 41) - 20;
    $dy = (intdiv($hash, 41) % 37) - 18;
    return (32 + 160 * $x + ($type === 'horizontal' ? 80 : 0) + $dx).','.
        (30 + 114 * $y + ($type === 'vertical' ? 57 : 0) + $dy);
}

function render_conquest_map(array $planet): void {
    $width = count($planet['rows'][0]);
    $height = count($planet['rows']);
    ?>
    <div class="conquest-region-map table-scroll biome-<?=$planet['biome']?>" role="region" tabindex="0"
        aria-label="<?=e($planet['name'])?> territory map">
        <svg class="conquest-regions<?=$width > 6 ? ' conquest-regions-dense' : ''?>"
            viewBox="0 0 <?=$width * 160 + 64?> <?=$height * 114 + 60?>" aria-label="<?=e($planet['name'])?> planetary map">
            <defs>
                <pattern id="conquest-contours" width="160" height="114" patternUnits="userSpaceOnUse">
                    <path d="M-20 24 Q32 -3 76 22 T180 23 M-20 47 Q32 20 76 45 T180 46 M-20 70 Q32 43 76 68 T180 69 M-20 93 Q32 66 76 91 T180 92 M-20 116 Q32 89 76 114 T180 115"/>
                </pattern>
            </defs>
            <?php foreach ($planet['rows'] as $y => $row): ?>
                <?php foreach ($row as $x => $sector):
                    if ($sector === null) continue;
                    $points = implode(' ', [
                        conquest_map_point($planet['name'], 'corner', $x, $y), conquest_map_point($planet['name'], 'horizontal', $x, $y),
                        conquest_map_point($planet['name'], 'corner', $x + 1, $y), conquest_map_point($planet['name'], 'vertical', $x + 1, $y),
                        conquest_map_point($planet['name'], 'corner', $x + 1, $y + 1), conquest_map_point($planet['name'], 'horizontal', $x, $y + 1),
                        conquest_map_point($planet['name'], 'corner', $x, $y + 1), conquest_map_point($planet['name'], 'vertical', $x, $y),
                    ]);
                    $owner = CONQUEST_OWNERS[$sector['owner']];
                    $start = isset($sector['start']) ? CONQUEST_OWNERS[$sector['start']].' start' : '';
                    $status = $start !== '' ? ($sector['start'] === $sector['owner'] ? $start : $owner.' · '.$start) :
                        ($sector['owner'] === 'neutral' ? 'Neutral · AI' : $owner);
                    $description = $sector['name'].' · '.$status;
                    if (isset($sector['map'])) $description .= ' · Battle map: '.$sector['map'];
                    $label = mb_strimwidth($sector['name'], 0, 17, '…', 'UTF-8');
                    $cx = 112 + 160 * $x;
                    $cy = 87 + 114 * $y;
                    ?>
                    <g class="conquest-<?=$sector['owner']?>" role="img" aria-label="<?=e($description)?>">
                        <title><?=e($description)?></title>
                        <polygon class="conquest-region-shape" points="<?=$points?>"/>
                        <polygon class="conquest-region-contours" points="<?=$points?>"/>
                        <rect class="conquest-region-label" x="<?=$cx - 64?>" y="<?=$cy - 20?>" width="128" height="42" rx="5"/>
                        <text class="conquest-region-name" x="<?=$cx?>" y="<?=$cy - 2?>"><?=e($label)?></text>
                        <text class="conquest-region-owner" x="<?=$cx?>" y="<?=$cy + 14?>"><?=e($status)?></text>
                    </g>
                <?php endforeach; ?>
            <?php endforeach; ?>
        </svg>
    </div>
    <?php
}

function render_conquest(): void {
    $season = conquest_snapshot(env_value('ZOD_CONQUEST_SNAPSHOT'));
    $planets = $season['planets'] ?? conquest_preview_planets();
    $seasonCounts = array_fill_keys(array_keys(CONQUEST_OWNERS), 0);
    foreach ($season['planets'] ?? [] as $planet) {
        foreach ($planet['counts'] as $owner => $count) $seasonCounts[$owner] += $count;
    }
    $seasonTotal = array_sum($seasonCounts);
    $planetNumber = filter_var($_GET['planet'] ?? null, FILTER_VALIDATE_INT, [
        'options' => ['min_range' => 1, 'max_range' => count($planets)],
    ]);
    $selectedIndex = $planetNumber === false ? null : $planetNumber - 1;
    $selectedPlanet = $selectedIndex === null ? null : $planets[$selectedIndex];
    ?>
    <section class="shell section page-section conquest">
        <h1>Planetary Conquest</h1>
        <h2 id="territory">Territory</h2>
        <?php if ($season === null): ?>
            <p>No live season has been published. Select a planet below to explore a preview with example ownership.</p>
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
        <?php endif; ?>
        <nav aria-label="<?=$season === null ? 'Preview planets' : 'Season planets'?>">
                <ul class="conquest-atlas conquest-atlas-season">
                    <?php foreach ($planets as $index => $planet): ?>
                        <li>
                            <a href="?planet=<?=$index + 1?>#planet-map" <?=$selectedIndex === $index ? 'aria-current="true"' : ''?>>
                                <span class="conquest-globe biome-<?=$planet['biome']?>" aria-hidden="true"></span>
                                <strong><?=e($planet['name'])?></strong>
                                <?php conquest_control_bar($planet['counts']); ?>
                            </a>
                        </li>
                    <?php endforeach; ?>
                </ul>
            </nav>
            <?php if ($selectedPlanet !== null): ?>
                <section class="conquest-planet conquest-selected-map" id="planet-map" aria-label="<?=e($selectedPlanet['name'])?> map">
                    <ul class="conquest-map-score" aria-label="Territories by owner">
                        <?php foreach (['red', 'blue', 'neutral'] as $owner): ?>
                            <li class="conquest-<?=$owner?>"><?=CONQUEST_OWNERS[$owner]?> <strong><?=number($selectedPlanet['counts'][$owner])?></strong></li>
                        <?php endforeach; ?>
                    </ul>
                    <?php render_conquest_map($selectedPlanet); ?>
                </section>
            <?php else: ?>
                <p>Select a planet to see its territory map and sector owners.</p>
            <?php endif; ?>
        <?php render_conquest_battles($season); ?>
        <section class="conquest-planet panel panel-pad" aria-labelledby="conquest-rules">
            <h2 id="conquest-rules">Proposed mode rules</h2>
            <p>Red and Blue start in opposite corners. Launch an attack from a sector
                your faction owns, and choose a neighboring enemy or Neutral sector.
                Neighboring sectors share a border. Neutral sectors are defended by AI.</p>
            <p>An attacker win removes one of a sector’s three control points, a defender
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
