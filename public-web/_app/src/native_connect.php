<?php
declare(strict_types=1);
if(!defined('ZODEE_INTERNAL')&&PHP_SAPI!=='cli'){http_response_code(404);exit;}
trait NativeConnectService {
    public function startNativeConnect(array $in): array {
        $this->limit();fields($in,['server']);$server=clean_text($in['server']??null,64);
        if(!in_array($server,array_column($this->servers(),'id'),true))fail(404,'This server is offline. Refresh the directory.');
        $device=random_token();$code=strtoupper(bin2hex(random_bytes(6)));
        query('DELETE FROM native_connections WHERE expires_at<UTC_TIMESTAMP(6)');
        query('INSERT INTO native_connections(device_hash,user_code,server_id,expires_at) VALUES(?,?,?,UTC_TIMESTAMP(6)+INTERVAL 5 MINUTE)',[token_hash($device),$code,$server]);
        return ['device'=>$device,'user_code'=>$code,'expires_in'=>300,'interval'=>2];
    }
    public function nativeConnectInfo(string $code): array {
        if(!preg_match('/\A[A-F0-9]{12}\z/D',$code))fail(404,'Connection request not found.');
        $row=query('SELECT n.user_code,n.account_id,s.id,s.name,s.host,s.port,s.map_name FROM native_connections n JOIN servers s ON s.id=n.server_id JOIN server_keys k ON k.id=s.id WHERE n.user_code=? AND n.expires_at>UTC_TIMESTAMP(6) AND k.enabled=1 AND s.last_seen>UTC_TIMESTAMP(6)-INTERVAL 90 SECOND',[$code])->fetch();
        if(!$row)fail(404,'This request expired or the server is offline. Start again from the game.');return $row;
    }
    public function approveNativeConnect(array $in): array {
        $this->limit();$me=$this->requireAccount();fields($in,['code']);$code=clean_text($in['code']??null,12);
        return transaction(function()use($me,$code){
            query('SELECT id FROM accounts WHERE id=? FOR UPDATE',[$me['id']]);
            $row=query('SELECT account_id FROM native_connections WHERE user_code=? AND expires_at>UTC_TIMESTAMP(6) FOR UPDATE',[$code])->fetch();
            if(!$row)fail(404,'This request expired or was cancelled. Start again from the game.');
            $this->nativeConnectInfo($code);
            if($row['account_id']!==null&&$row['account_id']!==$me['id'])fail(409,'This request was approved by another account.');
            query('UPDATE native_connections SET account_id=? WHERE user_code=?',[$me['id'],$code]);
            return ['ok'=>true];
        });
    }

    public function pollNativeConnect(array $in): array {
        fields($in,['device']);if(!is_hex_id($in['device']??null))fail(401,'Invalid connection request.');$hash=token_hash($in['device']);
        return transaction(function()use($hash){
            $snapshot=query('SELECT account_id FROM native_connections WHERE device_hash=? AND expires_at>UTC_TIMESTAMP(6)',[$hash])->fetch();
            if(!$snapshot)fail(401,'Connection request expired or was cancelled.');
            // Account-before-device matches password-reset/revocation lock order.
            if($snapshot['account_id']!==null)query('SELECT id FROM accounts WHERE id=? FOR UPDATE',[$snapshot['account_id']]);
            $row=query('SELECT *,last_poll>UTC_TIMESTAMP(6)-INTERVAL 1 SECOND AS too_fast FROM native_connections WHERE device_hash=? AND expires_at>UTC_TIMESTAMP(6) FOR UPDATE',[$hash])->fetch();
            if(!$row)fail(401,'Connection request expired or was cancelled.');
            if($row['too_fast'])fail(429,'Poll no more than once every two seconds.');
            query('UPDATE native_connections SET last_poll=UTC_TIMESTAMP(6) WHERE device_hash=?',[$hash]);
            if($row['account_id']===null||$snapshot['account_id']===null)return ['state'=>'pending'];
            if(!query('SELECT s.id FROM servers s JOIN server_keys k ON k.id=s.id WHERE s.id=? AND k.enabled=1 AND s.last_seen>UTC_TIMESTAMP(6)-INTERVAL 90 SECOND',[$row['server_id']])->fetchColumn())fail(404,'Server is offline. Refresh and retry.');
            // Match regular ticket issuance: serialize per account and replace unused tickets.
            query('SELECT id FROM accounts WHERE id=? FOR UPDATE',[$row['account_id']]);$ticket=random_token();
            query('DELETE FROM join_tickets WHERE account_id=?',[$row['account_id']]);
            query('INSERT INTO join_tickets(token_hash,account_id,server_id,expires_at) VALUES(?,?,?,UTC_TIMESTAMP(6)+INTERVAL 2 MINUTE)',[token_hash($ticket),$row['account_id'],$row['server_id']]);
            query('DELETE FROM native_connections WHERE device_hash=?',[$hash]);return ['state'=>'approved','ticket'=>$ticket,'server'=>$row['server_id']];
        });
    }
    public function cancelNativeConnect(array $in): array {
        fields($in,['device']);if(!is_hex_id($in['device']??null))fail(401,'Invalid connection request.');
        query('DELETE FROM native_connections WHERE device_hash=?',[token_hash($in['device'])]);return ['ok'=>true];
    }
}
function native_connect_page(ZodService $service,string $csrf): void {
    $code=is_string($_GET['code']??null)?strtoupper($_GET['code']):'';$server=$service->nativeConnectInfo($code);$me=$service->currentAccount();
    ?><section class="shell section page-section"><?php title_block('NATIVE GAME / CONNECTION APPROVAL','Join from your game.','Check the code and server below. Approval creates one connection ticket for this server; your password stays on the website.');?><article class="panel panel-pad"><h2><?=e($server['name'])?></h2><p><?=e($server['host'])?>:<?=e($server['port'])?> · <?=e(map_title($server['map_name']))?></p><p class="mono">Code: <strong><?=e($code)?></strong></p>
    <?php if(!$me):?><p>Sign in, then return here to approve the request.</p><a class="button" href="/account?connect=<?=e($code)?>">Sign in to continue</a>
    <?php elseif($server['account_id']!==null):?><p role="status">Approved. Return to the game; it will connect automatically.</p>
    <?php else:?><p>Continue as <?=e($me['username'])?>. Only approve if you started this connection in your game.</p><form action="/connect/approve" method="post"><input type="hidden" name="csrf" value="<?=e($csrf)?>"><input type="hidden" name="code" value="<?=e($code)?>"><button class="button button-primary">Approve this server connection</button></form><?php endif?><p>To cancel, use Back or close the Multiplayer window in the game. Requests expire after five minutes.</p></article></section><?php
}
