<?php
declare(strict_types=1);
if(!defined('ZODEE_INTERNAL')&&PHP_SAPI!=='cli'){http_response_code(404);exit;}
trait ServerOwnershipService {
    public function serverAuthStart(array $in): array {
        $this->limit();fields($in,['server','name','key']);$id=clean_text($in['server']??null,64);$name=clean_text($in['name']??null,80);
        if(!preg_match('/\A[A-Za-z0-9_-]{1,64}\z/D',$id)||!is_hex_id($in['key']??null))fail(400,'Invalid server identity.');
        $existing=query('SELECT key_hash FROM server_keys WHERE id=?',[$id])->fetchColumn();$hash=token_hash($in['key']);
        $owned=query('SELECT 1 FROM server_owners WHERE server_id=?',[$id])->fetchColumn();
        if($existing!==false&&!$owned&&!hash_equals($existing,$hash))fail(401,'An unclaimed legacy ID requires its existing server key.');
        query('DELETE FROM server_auth_requests WHERE expires_at<UTC_TIMESTAMP(6)');
        $device=random_token();$code=strtoupper(bin2hex(random_bytes(6)));
        query('INSERT INTO server_auth_requests(device_hash,user_code,server_id,server_name,key_hash,prior_key_hash,expires_at) VALUES(?,?,?,?,?,?,UTC_TIMESTAMP(6)+INTERVAL 5 MINUTE)',[token_hash($device),$code,$id,$name,$hash,$existing===false?null:$existing]);
        return ['device'=>$device,'user_code'=>$code,'expires_in'=>300,'interval'=>2];
    }
    public function serverAuthInfo(string $code): array {
        if(!preg_match('/\A[A-F0-9]{12}\z/D',$code))fail(404,'Server sign-in request not found.');
        $row=query('SELECT user_code,server_id,server_name,account_id FROM server_auth_requests WHERE user_code=? AND expires_at>UTC_TIMESTAMP(6)',[$code])->fetch();
        if(!$row)fail(404,'Server sign-in expired or was cancelled. Start again on the dedicated host.');return $row;
    }
    public function approveServerAuth(array $in): array {
        $this->limit();$me=$this->requireAccount();fields($in,['code']);$code=clean_text($in['code']??null,12);$this->serverAuthInfo($code);
        try{return transaction(function()use($me,$code){
            query('SELECT id FROM accounts WHERE id=? FOR UPDATE',[$me['id']]);
            $request=query('SELECT * FROM server_auth_requests WHERE user_code=? AND expires_at>UTC_TIMESTAMP(6) FOR UPDATE',[$code])->fetch();
            if(!$request)fail(404,'Server sign-in expired or was cancelled.');
            if($request['account_id']!==null&&$request['account_id']!==$me['id'])fail(409,'Another account approved this request.');
            $id=$request['server_id'];$key=query('SELECT key_hash FROM server_keys WHERE id=? FOR UPDATE',[$id])->fetchColumn();
            if($key!==false&&($request['prior_key_hash']===null||!hash_equals($key,$request['prior_key_hash'])))fail(409,'The server credential changed. Start again on the host.');
            $owner=query('SELECT account_id FROM server_owners WHERE server_id=? FOR UPDATE',[$id])->fetchColumn();
            if($owner!==false&&$owner!==$me['id'])fail(403,'Only this server’s owner can authorize its host. Administrators cannot take ownership.');
            if($owner===false&&query('SELECT COUNT(*) FROM server_owners WHERE account_id=?',[$me['id']])->fetchColumn()>=32)fail(429,'Account server limit reached.');
            if($key===false)query('INSERT INTO server_keys(id,name,key_hash) VALUES(?,?,?)',[$id,$request['server_name'],$request['key_hash']]);
            if($owner===false)query('INSERT INTO server_owners(server_id,account_id) VALUES(?,?)',[$id,$me['id']]);
            if($key!==false&&!hash_equals($key,$request['key_hash'])){
                query('UPDATE server_keys SET key_hash=? WHERE id=?',[$request['key_hash'],$id]);
                query('DELETE FROM servers WHERE id=?',[$id]);query('DELETE FROM join_tickets WHERE server_id=?',[$id]);query('DELETE FROM native_connections WHERE server_id=?',[$id]);
            }
            query('UPDATE server_owners SET host_revoked=0 WHERE server_id=?',[$id]);
            query('UPDATE server_auth_requests SET account_id=? WHERE user_code=?',[$me['id'],$code]);
            query('INSERT INTO moderation_log(actor,action,target) VALUES(?,?,?)',[$me['id'],'authorize-host',$id]);return ['ok'=>true];
        });}catch(PDOException $error){if((string)$error->getCode()==='23000')fail(409,'This server was claimed concurrently. Start sign-in again with the owner account.');throw $error;}
    }
    public function serverAuthPoll(array $in): array {
        fields($in,['device']);if(!is_hex_id($in['device']??null))fail(401,'Invalid host sign-in request.');
        return transaction(function()use($in){
            $hash=token_hash($in['device']);$row=query('SELECT *,last_poll>UTC_TIMESTAMP(6)-INTERVAL 1 SECOND AS too_fast FROM server_auth_requests WHERE device_hash=? AND expires_at>UTC_TIMESTAMP(6) FOR UPDATE',[$hash])->fetch();
            if(!$row)fail(401,'Server sign-in expired or was cancelled.');if($row['too_fast'])fail(429,'Wait two seconds before polling.');
            query('UPDATE server_auth_requests SET last_poll=UTC_TIMESTAMP(6) WHERE device_hash=?',[$hash]);
            if($row['account_id']===null)return ['state'=>'pending'];
            query('DELETE FROM server_auth_requests WHERE device_hash=?',[$hash]);return ['state'=>'approved','server'=>$row['server_id'],'owner'=>$row['account_id']];
        });
    }
    public function serverAuthCancel(array $in): array {
        fields($in,['device']);if(!is_hex_id($in['device']??null))fail(401,'Invalid host sign-in request.');
        query('DELETE FROM server_auth_requests WHERE device_hash=?',[token_hash($in['device'])]);return ['ok'=>true];
    }
    public function serverIdentity(): array {
        $id=$this->server();return query('SELECT k.id,k.name,o.account_id AS owner,a.username FROM server_keys k JOIN server_owners o ON o.server_id=k.id JOIN accounts a ON a.id=o.account_id WHERE k.id=?',[$id])->fetch();
    }
    public function serverRoles(): array {
        $identity=$this->serverIdentity();return ['owner'=>$identity['owner'],'admins'=>array_column(query("SELECT account_id FROM server_admins WHERE server_id=? AND role='admin' ORDER BY account_id",[$identity['id']])->fetchAll(),'account_id'),'moderators'=>array_column(query("SELECT account_id FROM server_admins WHERE server_id=? AND role='moderator' ORDER BY account_id",[$identity['id']])->fetchAll(),'account_id')];
    }
    public function ownedServers(): array {
        $me=$this->requireAccount();$rows=query('SELECT k.id,k.name,k.enabled,o.account_id AS owner,a.username AS owner_name,IF(o.account_id=?,\'owner\',IF(d.role=\'moderator\',\'moderator\',\'administrator\')) AS role FROM server_keys k JOIN server_owners o ON o.server_id=k.id JOIN accounts a ON a.id=o.account_id LEFT JOIN server_admins d ON d.server_id=k.id AND d.account_id=? WHERE o.account_id=? OR d.account_id IS NOT NULL ORDER BY k.name',[$me['id'],$me['id'],$me['id']])->fetchAll();
        foreach($rows as &$row)$row['admins']=query('SELECT a.username,d.role FROM server_admins d JOIN accounts a ON a.id=d.account_id WHERE d.server_id=? ORDER BY a.username',[$row['id']])->fetchAll();return $rows;
    }
    public function manageServerAdmin(array $in): array {
        $this->limit();$me=$this->requireAccount();fields($in,['server','username','action','role']);$server=clean_text($in['server']??null,64);$name=clean_text($in['username']??null,24);$action=$in['action']??'';$role=$in['role']??'admin';if(!in_array($role,['admin','moderator'],true))fail(400,'Choose admin or moderator.');
        if(!in_array($action,['add','remove'],true))fail(400,'Choose add or remove.');
        return transaction(function()use($me,$server,$name,$action,$role){
            $target=query('SELECT id FROM accounts WHERE username_key=?',[strtolower($name)])->fetchColumn();if(!$target)fail(404,'Cloud account not found.');$lock=[$me['id'],$target];sort($lock);foreach(array_unique($lock) as $account)query('SELECT id FROM accounts WHERE id=? FOR UPDATE',[$account]);query('SELECT id FROM server_keys WHERE id=? FOR UPDATE',[$server]);
            $owner=query('SELECT account_id FROM server_owners WHERE server_id=? FOR UPDATE',[$server])->fetchColumn();if($owner!==$me['id'])fail(403,'Only the owner can add or remove this server’s administrators.');
            $target=query('SELECT id FROM accounts WHERE username_key=?',[strtolower($name)])->fetchColumn();if(!$target)fail(404,'Cloud account not found.');if($target===$owner)fail(400,'The owner is always an administrator and cannot be removed.');
            if($action==='add'){
                if(query('SELECT COUNT(*) FROM server_admins WHERE server_id=?',[$server])->fetchColumn()>=32&&!query('SELECT 1 FROM server_admins WHERE server_id=? AND account_id=?',[$server,$target])->fetchColumn())fail(429,'Server administrator limit reached.');
                query('INSERT INTO server_admins(server_id,account_id,role) VALUES(?,?,?) ON DUPLICATE KEY UPDATE role=VALUES(role)',[$server,$target,$role]);
            }else query('DELETE FROM server_admins WHERE server_id=? AND account_id=?',[$server,$target]);
            query('INSERT INTO moderation_log(actor,action,target,details) VALUES(?,?,?,?)',[$me['id'],'server-admin-'.$action,$server,'account:'.$target.' role:'.$role]);return ['ok'=>true];
        });
    }
}
function server_auth_page(ZodService $service,string $csrf):void {
    $code=is_string($_GET['code']??null)?strtoupper($_GET['code']):'';$request=$service->serverAuthInfo($code);$me=$service->currentAccount();?><section class="shell section page-section"><?php title_block('PUBLIC DEDICATED SERVER / SIGN IN','Authorize your server host.','Approve only a request you started on your own dedicated host. That machine receives a server credential, never your cloud password.');?><article class="panel panel-pad"><h2><?=e($request['server_name'])?></h2><p>Server ID: <?=e($request['server_id'])?> · Code: <strong><?=e($code)?></strong></p><?php if(!$me):?><a class="button" href="/account?server_connect=<?=e($code)?>">Sign in to continue</a><?php elseif($request['account_id']!==null):?><p>Approved. Return to the dedicated host.</p><?php else:?><p>You will own this server as <?=e($me['username'])?> and automatically administer it. Existing ownership cannot be replaced. Authorizing a new host credential disconnects the previous host from cloud services.</p><form action="/server-auth/approve" method="post"><input type="hidden" name="csrf" value="<?=e($csrf)?>"><input type="hidden" name="code" value="<?=e($code)?>"><button class="button button-primary">Authorize this host</button></form><?php endif?></article></section><?php
}
function owned_servers_page(ZodService $service,string $csrf):void {
    $rows=$service->ownedServers();?><section class="shell section page-section"><?php title_block('CLOUD / MY SERVERS','Your dedicated servers.','Owners are always administrators. Only owners can add or remove delegated administrators. Roles update on running public servers within about ten seconds; a portal outage suspends administrative actions.');?><p>On the dedicated host, run <code>zod-agent --mode public --hub https://zodee.retrocs.org/index.php --id YOUR-SERVER --login</code> to sign in through your browser.</p><?php if(!$rows):?><p>No owned or administered servers yet.</p><?php endif?><?php foreach($rows as $row):?><article class="panel panel-pad"><h2><?=e($row['name'])?></h2><p><a class="button" href="/server-console?server=<?=e($row['id'])?>">Server console, health &amp; audit</a></p><p><?=e($row['id'])?> · <?=e($row['role'])?> · <?=$row['enabled']?'Enabled':'Disabled by site operator'?></p><p>Owner: <?=e($row['owner_name'])?> (permanent administrator)</p><h3>Delegated administrators</h3><?php if(!$row['admins']):?><p>None added.</p><?php endif?><?php foreach($row['admins'] as $admin):?><div class="button-row"><span><?=e($admin['username'])?> · <?=e($admin['role'])?></span><?php if($row['role']==='owner'):?><form action="/my-servers/admin" method="post"><input type="hidden" name="csrf" value="<?=e($csrf)?>"><input type="hidden" name="server" value="<?=e($row['id'])?>"><input type="hidden" name="username" value="<?=e($admin['username'])?>"><input type="hidden" name="action" value="remove"><button class="button">Remove administrator</button></form><?php endif?></div><?php endforeach?><?php if($row['role']==='owner'):?><form action="/my-servers/admin" method="post" class="account-form"><input type="hidden" name="csrf" value="<?=e($csrf)?>"><input type="hidden" name="server" value="<?=e($row['id'])?>"><input type="hidden" name="action" value="add"><label>Existing cloud username<input name="username" maxlength="24" required></label><label>Server role<select name="role"><option value="admin">Administrator — match settings and moderation</option><option value="moderator">Moderator — kick and ban only</option></select></label><button class="button">Assign server role</button></form><?php endif?></article><?php endforeach?></section><?php
}
