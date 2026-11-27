<?php
declare(strict_types=1);
if (!defined('ZODEE_INTERNAL') && PHP_SAPI !== 'cli') { http_response_code(404); exit; }

const CONQUEST_OWNERS = ['red' => 'Red', 'blue' => 'Blue', 'neutral' => 'Unclaimed'];

function conquest_name(mixed $name): bool {
    return is_string($name) && trim($name) !== '' && strlen($name) <= 100;
}

function conquest_snapshot(string $file): ?array {
    if ($file === '' || !is_file($file)) return null;
    $json = @file_get_contents($file, false, null, 0, 131073);
    if ($json === false || strlen($json) > 131072) return null;
    $season = json_decode($json, true, 16);
    if (!is_array($season) || !conquest_name($season['season'] ?? null)) return null;
    $planetData = $season['planets'] ?? null;
    $updated = $season['updated_at'] ?? null;
    if (!is_array($planetData) || !array_is_list($planetData) || count($planetData) < 1 || count($planetData) > 8 ||
        !is_string($updated) || !preg_match('/\A\d{4}-\d\d-\d\dT\d\d:\d\d:\d\dZ\z/D', $updated)) return null;
    $date = DateTimeImmutable::createFromFormat('!Y-m-d\TH:i:s\Z', $updated, new DateTimeZone('UTC'));
    if ($date === false || $date->format('Y-m-d\TH:i:s\Z') !== $updated) return null;

    $planets = [];
    foreach ($planetData as $planet) {
        if (!is_array($planet) || !conquest_name($planet['name'] ?? null)) return null;
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
                $counts[$owner]++;
            }
        }
        if (array_sum($counts) === 0) return null;
        $planets[] = ['name' => $planet['name'], 'rows' => $rows, 'counts' => $counts];
    }
    return ['season' => $season['season'], 'updated_at' => $updated, 'planets' => $planets];
}

function render_conquest(): void {
    $season = conquest_snapshot(env_value('ZOD_CONQUEST_SNAPSHOT'));
    $seasonCounts = array_fill_keys(array_keys(CONQUEST_OWNERS), 0);
    foreach ($season['planets'] ?? [] as $planet) {
        foreach ($planet['counts'] as $owner => $count) $seasonCounts[$owner] += $count;
    }
    $seasonTotal = array_sum($seasonCounts);
    ?>
    <section class="shell section page-section conquest">
        <p><a class="text-link" href="/war-room">← The War Room</a></p>
        <h1>Planetary Conquest</h1>
        <?php if ($season === null): ?>
            <p>No season map is available. Season standings will appear when a snapshot is published.</p>
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
            <?php foreach ($season['planets'] as $planet):
                $total = array_sum($planet['counts']);
                $fullControl = array_search($total, $planet['counts'], true);
                $control = match ($fullControl) {
                    'red', 'blue' => CONQUEST_OWNERS[$fullControl].' controls all sectors',
                    'neutral' => 'Unclaimed',
                    default => 'Split control',
                };
            ?>
                <section class="conquest-planet">
                    <h2><?=e($planet['name'])?></h2>
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
                    <div class="table-scroll" tabindex="0" role="region" aria-label="<?=e($planet['name'])?> territory map">
                        <table class="conquest-map" aria-label="<?=e($planet['name'])?> territory ownership">
                            <tbody>
                                <?php foreach ($planet['rows'] as $row): ?>
                                    <tr>
                                        <?php foreach ($row as $sector): ?>
                                            <?php if ($sector === null): ?>
                                                <td class="conquest-void"></td>
                                            <?php else: ?>
                                                <td class="conquest-<?=$sector['owner']?>">
                                                    <strong><?=e($sector['name'])?></strong>
                                                    <span><?=CONQUEST_OWNERS[$sector['owner']]?></span>
                                                </td>
                                            <?php endif; ?>
                                        <?php endforeach; ?>
                                    </tr>
                                <?php endforeach; ?>
                            </tbody>
                        </table>
                    </div>
                </section>
            <?php endforeach; ?>
        <?php endif; ?>
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
