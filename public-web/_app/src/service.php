<?php
declare(strict_types=1);
if (!defined('ZODEE_INTERNAL') && PHP_SAPI !== 'cli') { http_response_code(404); exit; }
require_once __DIR__ . '/bootstrap.php';
require_once __DIR__ . '/community_service.php';
require_once __DIR__ . '/content_service.php';
require_once __DIR__ . '/native_connect.php';

final class ZodService {
    use CommunityService,ContentService,NativeConnectService;
    public function currentAccount(): ?array {
        return query('SELECT a.id,a.username,a.created_at FROM sessions s JOIN accounts a ON a.id=s.account_id WHERE s.token_hash=? AND s.expires_at>UTC_TIMESTAMP(6)', [token_hash(session_token())])->fetch() ?: null;
    }
    public function requireAccount(): array { return $this->currentAccount() ?? fail(401, 'Sign in to your Zod account first.'); }
    private function server(): string {
        $id = query('SELECT id FROM server_keys WHERE key_hash=? AND enabled=1', [token_hash(bearer())])->fetchColumn();
        if (!$id) fail(401, 'Server authentication required.'); return $id;
    }
    private function limit(): void {
        $bucket = token_hash(client_ip()); $window = intdiv(time(), 60);
        $count = transaction(function () use ($bucket, $window) {
            query('INSERT INTO rate_limits (bucket,window_id,attempts) VALUES (?,?,1) ON DUPLICATE KEY UPDATE attempts=attempts+1', [$bucket, $window]);
            return (int)query('SELECT attempts FROM rate_limits WHERE bucket=? AND window_id=?', [$bucket, $window])->fetchColumn();
        });
        if ($count > 12) fail(429, 'Too many attempts. Please try again in a minute.');
    }
    private function hashPassword(string $password): string {
        if (!defined('PASSWORD_ARGON2ID')) throw new RuntimeException('PHP Argon2id support required');
        return password_hash($password, PASSWORD_ARGON2ID, ['memory_cost' => 65536, 'time_cost' => 3, 'threads' => 1]);
    }
    private function validPassword(mixed $password): string {
        if (!is_string($password) || strlen($password) < 12 || strlen($password) > 128) fail(400, 'Use a password between 12 and 128 bytes long.'); return $password;
    }
    private function verifies(string $password, array $account): bool {
        if ($account['password_scheme'] === 'pbkdf2-sha256') return hash_equals($account['password_hash'], hash_pbkdf2('sha256', $password, hex2bin($account['legacy_salt']), 600000, 64));
        return password_verify($password, $account['password_hash']);
    }
    public function register(array $in): array {
        $this->limit(); fields($in, ['username','password']); $name = $in['username'] ?? '';
        if (!is_string($name) || !preg_match('/\A[A-Za-z][A-Za-z0-9_]{2,23}\z/D', $name)) fail(400, 'Usernames use 3–24 letters, numbers or underscores, starting with a letter.');
        $hash = $this->hashPassword($this->validPassword($in['password'] ?? null)); $id = random_token();
        try { query('INSERT INTO accounts (id,username,username_key,password_hash) VALUES (?,?,?,?)', [$id,$name,strtolower($name),$hash]); }
        catch (PDOException $e) { if (($e->errorInfo[1] ?? 0) === 1062) fail(409, 'That username is already taken.'); throw $e; }
        return ['id'=>$id,'username'=>$name];
    }
    public function login(array $in): array {
        $this->limit(); fields($in, ['username','password']); $name = $in['username'] ?? ''; $pass = $in['password'] ?? '';
        if (!is_string($name) || !is_string($pass) || strlen($pass)>128) fail(401, 'Incorrect username or password.');
        $token = random_token();
        $account=transaction(function () use ($name,$pass,$token) {
            $account=query('SELECT * FROM accounts WHERE username_key=? FOR UPDATE',[strtolower($name)])->fetch();
            if (!$account) {hash_pbkdf2('sha256',$pass,str_repeat('0',32),600000,64);fail(401,'Incorrect username or password.');}
            if (!$this->verifies($pass,$account)) fail(401,'Incorrect username or password.');
            if ($account['password_scheme']!=='argon2id' || password_needs_rehash($account['password_hash'],PASSWORD_ARGON2ID,['memory_cost'=>65536,'time_cost'=>3,'threads'=>1])) {
                query("UPDATE accounts SET password_hash=?,password_scheme='argon2id',legacy_salt=NULL WHERE id=?",[$this->hashPassword($pass),$account['id']]);
            }
            query('INSERT INTO sessions (token_hash,account_id,expires_at) VALUES (?,?,UTC_TIMESTAMP(6)+INTERVAL 1 DAY)',[token_hash($token),$account['id']]);
            return $account;
        });
        session_cookie($token); return ['session'=>$token,'username'=>$account['username']];
    }
    public function logout(): array { query('DELETE FROM sessions WHERE token_hash=?', [token_hash(session_token())]); session_cookie(''); return ['ok'=>true]; }
    public function changePassword(array $in): array {
        $this->limit(); fields($in,['current_password','password']); $me=$this->requireAccount(); $pass=$this->validPassword($in['password']??null); $current=$in['current_password']??'';
        if (!is_string($current) || strlen($current)>128) fail(400,'Enter your current password.');
        $token=random_token();
        transaction(function() use($me,$current,$pass,$token) {
            $row=query('SELECT * FROM accounts WHERE id=? FOR UPDATE',[$me['id']])->fetch();
            if (!$this->verifies($current,$row)) fail(401,'Your current password is incorrect.');
            query("UPDATE accounts SET password_hash=?,password_scheme='argon2id',legacy_salt=NULL WHERE id=?",[$this->hashPassword($pass),$me['id']]);
            query('DELETE FROM native_connections WHERE account_id=?',[$me['id']]);
            query('DELETE FROM sessions WHERE account_id=?',[$me['id']]);
            query('INSERT INTO sessions (token_hash,account_id,expires_at) VALUES (?,?,UTC_TIMESTAMP(6)+INTERVAL 1 DAY)',[token_hash($token),$me['id']]);
        });
        session_cookie($token); return ['ok'=>true,'session'=>$token];
    }
    public function servers(): array {
        $rows=query('SELECT s.*,COALESCE(md.region,\'Unspecified\') AS region,COALESCE(md.game_mode,\'Skirmish\') AS game_mode FROM servers s LEFT JOIN server_metadata md ON md.server_id=s.id JOIN server_keys k ON k.id=s.id WHERE k.enabled=1 AND s.last_seen>UTC_TIMESTAMP(6)-INTERVAL 90 SECOND ORDER BY s.name')->fetchAll();
        return array_map(fn($s)=>['id'=>$s['id'],'name'=>$s['name'],'host'=>$s['host'],'port'=>(int)$s['port'],'map'=>$s['map_name'],'players'=>(int)$s['players'],'capacity'=>(int)$s['capacity'],'started'=>(bool)$s['started'],'version'=>$s['version'],'region'=>$s['region'],'game_mode'=>$s['game_mode'],'seen'=>str_replace(' ','T',$s['last_seen']).'Z'],$rows);
    }
    public function heartbeat(array $in): array {
        $id=$this->server();fields($in,['id','name','host','port','map','players','capacity','started','version','seen']);
        $name=clean_text($in['name']??null,80);$map=clean_text($in['map']??'',240,true);$version=clean_text($in['version']??'',80,true);
        $port=int_value($in['port']??null,1024,65535);$capacity=int_value($in['capacity']??null,1,8);$players=int_value($in['players']??null,0,$capacity);$started=$in['started']??false;
        if (!is_bool($started)) fail(400,'Invalid match state.');
        $source=client_ip();$host=$in['host']??'';if ($host==='') $host=$source;
        if (!is_string($host) || !filter_var($host,FILTER_VALIDATE_IP,FILTER_FLAG_IPV4) || $host!==$source) fail(400,'Advertised IPv4 address must match the announcing server.');
        query('INSERT INTO servers (id,name,host,port,map_name,players,capacity,started,version,last_seen) VALUES (?,?,?,?,?,?,?,?,?,UTC_TIMESTAMP(6)) ON DUPLICATE KEY UPDATE name=VALUES(name),host=VALUES(host),port=VALUES(port),map_name=VALUES(map_name),players=VALUES(players),capacity=VALUES(capacity),started=VALUES(started),version=VALUES(version),last_seen=UTC_TIMESTAMP(6)',[$id,$name,$host,$port,$map,$players,$capacity,(int)$started,$version]);
        return ['expires_in'=>90];
    }
    public function removeServer(): array { query('DELETE FROM servers WHERE id=?',[$this->server()]); return ['ok'=>true]; }
    public function ticket(array $in): array {
        $me=$this->requireAccount();fields($in,['server']);$id=clean_text($in['server']??null,64);$token=random_token();
        transaction(function() use($me,$id,$token) {
            // Serialize ticket issuance per account, and consume tickets atomically.
            query('SELECT id FROM accounts WHERE id=? FOR UPDATE',[$me['id']]);
            if (!query('SELECT s.id FROM servers s JOIN server_keys k ON k.id=s.id WHERE s.id=? AND k.enabled=1 AND s.last_seen>UTC_TIMESTAMP(6)-INTERVAL 90 SECOND',[$id])->fetchColumn()) fail(404,'This server is offline. Refresh the list.');
            query('DELETE FROM join_tickets WHERE account_id=?',[$me['id']]);
            query('INSERT INTO join_tickets (token_hash,account_id,server_id,expires_at) VALUES (?,?,?,UTC_TIMESTAMP(6)+INTERVAL 2 MINUTE)',[token_hash($token),$me['id'],$id]);
        });
        return ['ticket'=>$token,'server'=>$id,'expires_in'=>'120'];
    }
    public function redeem(array $in): array {
        $server=$this->server();fields($in,['ticket']);$ticket=$in['ticket']??'';if (!is_hex_id($ticket)) fail(401,'Invalid join ticket.');
        return transaction(function() use($server,$ticket) {
            $row=query('SELECT t.account_id,a.username FROM join_tickets t JOIN accounts a ON a.id=t.account_id WHERE t.token_hash=? AND t.server_id=? AND t.expires_at>UTC_TIMESTAMP(6) FOR UPDATE',[token_hash($ticket),$server])->fetch();
            if (!$row) fail(401,'Ticket expired, already used, or issued for another server.');
            query('DELETE FROM join_tickets WHERE token_hash=?',[token_hash($ticket)]);$grant=random_token();
            query('INSERT INTO connection_grants (token_hash,account_id,server_id,expires_at) VALUES (?,?,?,UTC_TIMESTAMP(6)+INTERVAL 1 DAY)',[token_hash($grant),$row['account_id'],$server]);
            return ['account'=>$row['account_id'],'username'=>$row['username'],'grant'=>$grant];
        });
    }
    public static function normalizeResult(array $in): array {
        fields($in,['id','map','duration','players','server','finished']);
        if (!is_hex_id($in['id']??null)) fail(400,'Invalid match identifier.');
        $map=clean_text($in['map']??null,240);$duration=int_value($in['duration']??null,0,86400);$players=$in['players']??null;
        if (!is_array($players)||!array_is_list($players)||count($players)<1||count($players)>8) fail(400,'Invalid player list.');
        $out=[];$seen=[];
        foreach ($players as $p) {
            if (!is_array($p)) fail(400,'Invalid player result.');fields($p,['account','grant','won','kills','lost','captures']);
            if (!is_hex_id($p['account']??null)||!is_hex_id($p['grant']??null)||!is_bool($p['won']??null)||isset($seen[$p['account']])) fail(400,'Invalid authenticated player result.');
            $seen[$p['account']]=true;
            $out[]=['account'=>$p['account'],'grant'=>$p['grant'],'won'=>$p['won'],'kills'=>int_value($p['kills']??null,0,100000),'lost'=>int_value($p['lost']??null,0,100000),'captures'=>int_value($p['captures']??null,0,100000)];
        }
        usort($out,fn($a,$b)=>strcmp($a['account'],$b['account']));
        return ['id'=>$in['id'],'map'=>$map,'duration'=>$duration,'players'=>$out];
    }
    public function result(array $in): array {
        $server=$this->server();$result=self::normalizeResult($in);$digest=token_hash(json_encode($result,JSON_THROW_ON_ERROR|JSON_UNESCAPED_SLASHES|JSON_UNESCAPED_UNICODE));
        try {
            transaction(function() use($server,$result,$digest) {
                query('INSERT INTO matches (server_id,match_id,map_name,duration,payload_hash) VALUES (?,?,?,?,?)',[$server,$result['id'],$result['map'],$result['duration'],$digest]);$match=(int)db()->lastInsertId();
                foreach ($result['players'] as $p) {
                    $valid=query('SELECT account_id FROM connection_grants WHERE token_hash=? AND server_id=? AND account_id=? AND expires_at>UTC_TIMESTAMP(6)',[token_hash($p['grant']),$server,$p['account']])->fetchColumn();
                    if (!$valid) fail(400,'Invalid or expired player grant.');
                    query('INSERT INTO match_players (match_pk,account_id,won,kills,lost,captures) VALUES (?,?,?,?,?,?)',[$match,$p['account'],(int)$p['won'],$p['kills'],$p['lost'],$p['captures']]);
                }
            });
        } catch (PDOException $e) {
            if (($e->errorInfo[1]??0)!==1062) throw $e;
            $old=query('SELECT payload_hash FROM matches WHERE server_id=? AND match_id=?',[$server,$result['id']])->fetchColumn();
            if (!$old || !hash_equals($old,$digest)) fail(409,'This match identifier already has a different result.');
        }
        return ['ok'=>true];
    }
    public function leaderboard(int $limit=50): array {
        $limit=max(1,min(100,$limit));
        $rows=query('SELECT a.username,COUNT(p.match_pk) AS matches,COALESCE(SUM(p.won),0) AS wins,COALESCE(SUM(p.kills),0) AS kills,COALESCE(SUM(p.lost),0) AS lost,COALESCE(SUM(p.captures),0) AS captures FROM accounts a LEFT JOIN match_players p ON p.account_id=a.id WHERE COALESCE((SELECT visibility FROM account_preferences WHERE account_id=a.id),\'public\')=\'public\' GROUP BY a.id,a.username HAVING matches>0 ORDER BY wins DESC,kills DESC,a.username ASC LIMIT '.$limit)->fetchAll();
        foreach ($rows as &$r) foreach (['matches','wins','kills','lost','captures'] as $key) $r[$key]=(int)$r[$key];return $rows;
    }
    public function totals(): array {
        $counts=query('SELECT COUNT(*) AS matches,COALESCE(AVG(duration),0) AS average_duration FROM matches')->fetch();
        $counts['accounts']=(int)query('SELECT COUNT(*) FROM accounts')->fetchColumn();$counts['matches']=(int)$counts['matches'];$counts['average_duration']=(int)$counts['average_duration'];
        $counts['kills']=(int)query('SELECT COALESCE(SUM(kills),0) FROM match_players')->fetchColumn();return $counts;
    }
    public function profile(string $name): ?array {
        $account=query('SELECT id,username,created_at FROM accounts WHERE username_key=?',[strtolower($name)])->fetch();if (!$account)return null;
        $viewer=$this->currentAccount();$visibility=query('SELECT visibility FROM account_preferences WHERE account_id=?',[$account['id']])->fetchColumn()?:'public';
        if(($viewer['id']??'')!==$account['id'] && ($visibility==='private'||($visibility==='friends'&&(!$viewer||!$this->areFriends($viewer['id'],$account['id'])))||($viewer&&$this->blocked($viewer['id'],$account['id']))))return null;
        $stats=query('SELECT COUNT(*) AS matches,COALESCE(SUM(won),0) AS wins,COALESCE(SUM(kills),0) AS kills,COALESCE(SUM(lost),0) AS lost,COALESCE(SUM(captures),0) AS captures FROM match_players WHERE account_id=?',[$account['id']])->fetch();
        foreach ($stats as &$n)$n=(int)$n;
        $recent=query('SELECT m.map_name,m.duration,m.finished_at,m.server_id,p.won,p.kills,p.lost,p.captures FROM match_players p JOIN matches m ON m.id=p.match_pk WHERE p.account_id=? ORDER BY m.finished_at DESC LIMIT 15',[$account['id']])->fetchAll();
        return ['account'=>$account,'stats'=>$stats,'recent'=>$recent];
    }
    public function recent(): array { return query('SELECT m.*, (SELECT GROUP_CONCAT(a.username ORDER BY a.username SEPARATOR ", ") FROM match_players p JOIN accounts a ON a.id=p.account_id WHERE p.match_pk=m.id AND p.won=1 AND COALESCE((SELECT visibility FROM account_preferences WHERE account_id=a.id),\'public\')=\'public\') AS winners FROM matches m ORDER BY m.finished_at DESC LIMIT 12')->fetchAll(); }
    public function maps(): array { return query('SELECT map_name,COUNT(*) AS matches FROM matches GROUP BY map_name ORDER BY matches DESC,map_name LIMIT 6')->fetchAll(); }
}
