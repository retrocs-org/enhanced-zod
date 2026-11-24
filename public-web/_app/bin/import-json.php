<?php
declare(strict_types=1);
if (PHP_SAPI!=='cli')exit(1);
require __DIR__.'/../src/service.php';
$options=getopt('',['file:','keys-json:','commit']);
function legacy_time(string $value): string {return (new DateTimeImmutable($value))->setTimezone(new DateTimeZone('UTC'))->format('Y-m-d H:i:s.u');}
try {
    if (!isset($options['file'])) throw new RuntimeException('Usage: php bin/import-json.php --file old-zod.json [--keys-json server-keys.json] [--commit]');
    $state=json_decode(file_get_contents($options['file']),true,128,JSON_THROW_ON_ERROR);
    foreach(['Accounts','Sessions','Tickets','Grants','Results'] as $key)if(!isset($state[$key])||!is_array($state[$key]))throw new RuntimeException('Invalid legacy database: '.$key);
    $keys=isset($options['keys-json'])?json_decode(file_get_contents($options['keys-json']),true,32,JSON_THROW_ON_ERROR):[];
    printf("Legacy data: %d accounts, %d sessions, %d tickets, %d grants, %d results.\n",count($state['Accounts']),count($state['Sessions']),count($state['Tickets']),count($state['Grants']),count($state['Results']));
    if (!isset($options['commit'])) {echo "Dry run only. Back up both stores, then pass --commit to import into an empty account database.\n";exit;}
    transaction(function() use($state,$keys) {
        if ((int)query('SELECT COUNT(*) FROM accounts')->fetchColumn()!==0 || (int)query('SELECT COUNT(*) FROM matches')->fetchColumn()!==0)throw new RuntimeException('Import requires an empty account and match database.');
        foreach($state['Accounts'] as $a) {
            if(!is_hex_id($a['ID']??null)||!is_hex_id($a['Salt']??null)||!is_hex_id($a['Password']??null)||!preg_match('/\A[A-Za-z][A-Za-z0-9_]{2,23}\z/D',$a['Username']??''))throw new RuntimeException('Invalid legacy account record.');
            query("INSERT INTO accounts (id,username,username_key,password_hash,password_scheme,legacy_salt,created_at) VALUES (?,?,?,?,'pbkdf2-sha256',?,?)",[$a['ID'],$a['Username'],strtolower($a['Username']),$a['Password'],$a['Salt'],legacy_time($a['Created'])]);
        }
        $ensureServer=function(string $id) use($keys):void {
            if(!preg_match('/\A[A-Za-z0-9_-]{1,64}\z/D',$id))throw new RuntimeException('Invalid legacy server ID.');
            $key=$keys[$id]??null;if($key!==null&&!is_hex_id($key))throw new RuntimeException('Invalid legacy server key.');
            query('INSERT IGNORE INTO server_keys (id,name,key_hash,enabled) VALUES (?,?,?,?)',[$id,$id,token_hash($key??random_token()),$key!==null?1:0]);
        };
        foreach($keys as $id=>$key)$ensureServer($id);
        foreach($state['Sessions'] as $hash=>$s)if(strtotime($s['Expires'])>time())query('INSERT INTO sessions (token_hash,account_id,expires_at) VALUES (?,?,?)',[$hash,$s['Account'],legacy_time($s['Expires'])]);
        foreach($state['Tickets'] as $hash=>$t)if(strtotime($t['Expires'])>time()){$ensureServer($t['Server']);query('INSERT INTO join_tickets (token_hash,account_id,server_id,expires_at) VALUES (?,?,?,?)',[$hash,$t['Account'],$t['Server'],legacy_time($t['Expires'])]);}
        foreach($state['Grants'] as $grant=>$g)if(strtotime($g['Expires'])>time()){$ensureServer($g['Server']);query('INSERT INTO connection_grants (token_hash,account_id,server_id,expires_at) VALUES (?,?,?,?)',[token_hash($grant),$g['Account'],$g['Server'],legacy_time($g['Expires'])]);}
        foreach($state['Results'] as $r) {
            $result=ZodService::normalizeResult($r);$digest=token_hash(json_encode($result,JSON_THROW_ON_ERROR|JSON_UNESCAPED_SLASHES|JSON_UNESCAPED_UNICODE));
            query('INSERT INTO matches (server_id,match_id,map_name,duration,payload_hash,finished_at) VALUES (?,?,?,?,?,?)',[$r['server'],$r['id'],$r['map'],$r['duration'],$digest,legacy_time($r['finished'])]);$match=(int)db()->lastInsertId();
            foreach($result['players'] as $p)query('INSERT INTO match_players (match_pk,account_id,won,kills,lost,captures) VALUES (?,?,?,?,?,?)',[$match,$p['account'],(int)$p['won'],$p['kills'],$p['lost'],$p['captures']]);
        }
    });
    echo "Import committed. Account IDs and statistics preserved. Legacy passwords upgrade to Argon2id on next sign-in.\n";
} catch(Throwable $error) {fwrite(STDERR,"Import failed; transaction rolled back: ".$error->getMessage()."\n");exit(1);}
