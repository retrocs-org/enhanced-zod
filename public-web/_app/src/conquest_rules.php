<?php
declare(strict_types=1);
if (!defined('ZODEE_INTERNAL') && PHP_SAPI !== 'cli') { http_response_code(404); exit; }

function conquest_attackers(array $rows, int $x, int $y): array {
    $target = $rows[$y][$x] ?? null;
    if ($target === null) return [];

    $attackers = [];
    foreach ([[$x - 1, $y], [$x + 1, $y], [$x, $y - 1], [$x, $y + 1]] as [$adjacentX, $adjacentY]) {
        $owner = $rows[$adjacentY][$adjacentX]['owner'] ?? null;
        if (($owner === 'red' || $owner === 'blue') && $owner !== $target['owner']) {
            $attackers[$owner] = true;
        }
    }
    return array_keys($attackers);
}

// The caller must authenticate and deduplicate the battle before applying its result.
function conquest_resolve_battle(array $rows, int $x, int $y, string $attacker, string $outcome): array {
    if (!in_array($attacker, conquest_attackers($rows, $x, $y), true)) {
        throw new DomainException('The attacker no longer borders the target.');
    }
    $sector = $rows[$y][$x];
    $points = $sector['control_points'] ?? 3;
    $points = match ($outcome) {
        'attacker_win' => $points - 1,
        'defender_win' => min(3, $points + 1),
        'draw' => $points,
        default => throw new DomainException('Invalid conquest battle outcome.'),
    };
    if ($points === 0) {
        $sector['owner'] = $attacker;
        $points = 3;
    }
    $sector['control_points'] = $points;
    $rows[$y][$x] = $sector;
    return $rows;
}
