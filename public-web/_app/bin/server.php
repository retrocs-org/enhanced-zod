<?php
declare(strict_types=1);
if (PHP_SAPI!=='cli') { http_response_code(404); exit; }
require __DIR__.'/../src/bootstrap.php';
$options=getopt('',['id:','name:','key-file:','disable']);
$id=$options['id']??'';
if (!is_string($id)||!preg_match('/\A[A-Za-z0-9_-]{1,64}\z/D',$id)) {fwrite(STDERR,"Usage: php bin/server.php --id europe-1 --name 'Europe' --key-file /private/server-key [--disable]\n");exit(1);}
try {
    if (isset($options['disable'])) {query('UPDATE server_keys SET enabled=0 WHERE id=?',[$id]);query('DELETE FROM servers WHERE id=?',[$id]);echo "Server disabled.\n";exit;}
    $key=trim(file_get_contents($options['key-file']??''));if (!is_hex_id($key)) throw new RuntimeException('Key file must contain 64 random hexadecimal characters.');
    $name=clean_text($options['name']??$id,80);
    transaction(function() use($id,$name,$key) {
        $hash=token_hash($key);
        $owner=query('SELECT id FROM server_keys WHERE key_hash=? FOR UPDATE',[$hash])->fetchColumn();
        if ($owner!==false && $owner!==$id) throw new RuntimeException('This key is already assigned to a different server. Use a unique key.');
        $existing=query('SELECT id FROM server_keys WHERE id=? FOR UPDATE',[$id])->fetchColumn();
        if ($existing) query('UPDATE server_keys SET name=?,key_hash=?,enabled=1 WHERE id=?',[$name,$hash,$id]);
        else query('INSERT INTO server_keys (id,name,key_hash) VALUES (?,?,?)',[$id,$name,$hash]);
    });
    echo "Server credential provisioned. The key was not stored in plaintext. Public hosting also requires cloud ownership: authorize the dedicated host with --login and this --key-file.\n";
} catch(Throwable $e) {fwrite(STDERR,"Provisioning failed: ".$e->getMessage()."\n");exit(1);}
