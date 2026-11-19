<?php
declare(strict_types=1);

function env_value(string $name, string $default = ''): string {
    $file = getenv($name . '_FILE');
    if ($file !== false && $file !== '') {
        $value = file_get_contents($file);
        if ($value === false) throw new RuntimeException('Cannot read configured secret');
        return trim($value);
    }
    $value = getenv($name);
    return $value === false ? $default : $value;
}
function db(): PDO {
    static $db;
    if (!$db) {
        $socket = env_value('ZOD_DB_SOCKET');
        $address = $socket !== '' ? 'unix_socket=' . $socket : 'host=' . env_value('ZOD_DB_HOST', '127.0.0.1') . ';port=' . env_value('ZOD_DB_PORT', '3306');
        $db = new PDO('mysql:' . $address . ';dbname=' . env_value('ZOD_DB_NAME', 'zod') . ';charset=utf8mb4', env_value('ZOD_DB_USER', 'zod'), env_value('ZOD_DB_PASSWORD'), [
            PDO::ATTR_ERRMODE => PDO::ERRMODE_EXCEPTION,
            PDO::ATTR_DEFAULT_FETCH_MODE => PDO::FETCH_ASSOC,
            PDO::ATTR_EMULATE_PREPARES => false,
            PDO::ATTR_STRINGIFY_FETCHES => false,
        ]);
        $db->exec("SET time_zone='+00:00'");
        $db->exec('SET SESSION TRANSACTION ISOLATION LEVEL READ COMMITTED');
    }
    return $db;
}
function query(string $sql, array $args = []): PDOStatement {
    $statement = db()->prepare($sql); $statement->execute($args); return $statement;
}
function transaction(callable $work): mixed {
    db()->beginTransaction();
    try { $result = $work(); db()->commit(); return $result; }
    catch (Throwable $error) { if (db()->inTransaction()) db()->rollBack(); throw $error; }
}
final class ApiError extends RuntimeException {
    public function __construct(public readonly int $status, string $message) { parent::__construct($message); }
}
function fail(int $status, string $message): never { throw new ApiError($status, $message); }
function random_token(): string { return bin2hex(random_bytes(32)); }
function token_hash(string $value): string { return hash('sha256', $value); }
function is_hex_id(mixed $value): bool { return is_string($value) && preg_match('/\A[a-f0-9]{64}\z/D', $value) === 1; }
function clean_text(mixed $value, int $limit, bool $empty = false): string {
    if (!is_string($value) || (!$empty && $value === '') || strlen($value) > $limit || !mb_check_encoding($value, 'UTF-8') || preg_match('/[\x00-\x1f\x7f]/', $value)) fail(400, 'Invalid text value.');
    return $value;
}
function int_value(mixed $value, int $min, int $max): int {
    if (!is_int($value) || $value < $min || $value > $max) fail(400, 'Invalid numeric value.');
    return $value;
}
function bearer(): string {
    $header = $_SERVER['HTTP_AUTHORIZATION'] ?? '';
    return str_starts_with($header, 'Bearer ') ? substr($header, 7) : '';
}
function session_token(): string { $token=bearer() ?: ($_COOKIE['zod_session'] ?? ''); return is_string($token)?$token:''; }
function secure_cookie(): bool { return env_value('ZOD_DEV_HTTP') !== '1'; }
function session_cookie(string $token): void {
    setcookie('zod_session', $token, ['expires' => $token === '' ? time() - 3600 : time() + 86400, 'path' => '/', 'secure' => secure_cookie(), 'httponly' => true, 'samesite' => 'Strict']);
}
function proxy_contains(string $ip, string $range): bool {
    [$network, $bits] = array_pad(explode('/', trim($range), 2), 2, '');
    $address = @inet_pton($ip); $base = @inet_pton($network);
    if ($address === false || $base === false || strlen($address) !== strlen($base)) return false;
    $prefix = $bits === '' ? strlen($base) * 8 : (ctype_digit($bits) ? (int)$bits : -1);
    if ($prefix < 0 || $prefix > strlen($base) * 8) return false;
    $bytes = intdiv($prefix, 8); $remaining = $prefix % 8;
    return substr($address, 0, $bytes) === substr($base, 0, $bytes) && ($remaining === 0 || ((ord($address[$bytes]) ^ ord($base[$bytes])) & (255 << (8 - $remaining))) === 0);
}
function client_ip(): string {
    $peer = $_SERVER['REMOTE_ADDR'] ?? '127.0.0.1';
    foreach (explode(',', env_value('ZOD_TRUSTED_PROXIES')) as $range) {
        if ($range !== '' && proxy_contains($peer, $range)) {
            $forwarded = $_SERVER['HTTP_X_REAL_IP'] ?? '';
            if (filter_var($forwarded, FILTER_VALIDATE_IP)) return $forwarded;
        }
    }
    return $peer;
}
function require_origin(): void {
    $origin = $_SERVER['HTTP_ORIGIN'] ?? '';
    $allowed = rtrim(env_value('ZOD_PUBLIC_URL'), '/');
    if ($allowed === '') $allowed = (secure_cookie() ? 'https://' : 'http://') . ($_SERVER['HTTP_HOST'] ?? 'localhost');
    if ($origin !== '' && !hash_equals($allowed, $origin)) fail(403, 'This request came from another website.');
    if (!secure_cookie() && !in_array($_SERVER['REMOTE_ADDR'] ?? '', ['127.0.0.1', '::1'], true)) fail(503, 'Local HTTP mode requires a loopback connection.');
}
function csrf(): string {
    $token = $_COOKIE['zod_csrf'] ?? '';
    if (!is_hex_id($token)) {
        $token = random_token(); $_COOKIE['zod_csrf'] = $token;
        setcookie('zod_csrf', $token, ['path' => '/', 'secure' => secure_cookie(), 'httponly' => true, 'samesite' => 'Strict']);
    }
    return $token;
}
function require_csrf(): void {
    require_origin();
    $cookie = $_COOKIE['zod_csrf'] ?? ''; $submitted = $_POST['csrf'] ?? '';
    if (!is_hex_id($cookie) || !is_string($submitted) || !hash_equals($cookie, $submitted)) fail(403, 'Your form expired. Reload the page and try again.');
}
function json_input(): array {
    if (!str_starts_with(strtolower($_SERVER['CONTENT_TYPE'] ?? ''), 'application/json')) fail(415, 'Send an application/json request.');
    $raw = file_get_contents('php://input', false, null, 0, 65537);
    if ($raw === false || strlen($raw) > 65536) fail(413, 'Request is too large.');
    try { $object = json_decode($raw, false, 32, JSON_THROW_ON_ERROR); }
    catch (JsonException) { fail(400, 'Invalid JSON.'); }
    if (!$object instanceof stdClass) fail(400, 'Expected a JSON object.');
    $data = json_decode($raw, true, 32, JSON_THROW_ON_ERROR);
    $out = [];
    foreach ($data as $key => $value) { $key = strtolower($key); if (array_key_exists($key, $out)) fail(400, 'Duplicate field.'); $out[$key] = $value; }
    return $out;
}
function fields(array $data, array $allowed): void { if (array_diff(array_keys($data), $allowed)) fail(400, 'Unknown request field.'); }
function json_response(mixed $value, int $status = 200): never {
    http_response_code($status); header('Content-Type: application/json; charset=utf-8');
    echo json_encode($value, JSON_THROW_ON_ERROR | JSON_UNESCAPED_SLASHES); exit;
}
function security_headers(): void {
    header('X-Content-Type-Options: nosniff'); header('Referrer-Policy: same-origin');
    header("Content-Security-Policy: default-src 'self'; script-src 'self'; style-src 'self'; img-src 'self'; font-src 'self'; connect-src 'self'; object-src 'none'; base-uri 'none'; form-action 'self'; frame-ancestors 'none'");
    header('Cache-Control: no-store');
}
