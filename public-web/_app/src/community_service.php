<?php
declare(strict_types=1);
if (!defined('ZODEE_INTERNAL') && PHP_SAPI !== 'cli') { http_response_code(404); exit; }
trait CommunityService {
    public function preferences(): array {
        $me=$this->requireAccount();
        return query('SELECT visibility,allow_requests,show_online FROM account_preferences WHERE account_id=?',[$me['id']])->fetch() ?: ['visibility'=>'public','allow_requests'=>0,'show_online'=>0];
    }
    public function setPreferences(array $in): array {
        $me=$this->requireAccount();fields($in,['visibility','allow_requests','show_online']);
        $visibility=$in['visibility']??'';if(!in_array($visibility,['public','friends','private'],true))fail(400,'Choose public, friends or private.');
        foreach(['allow_requests','show_online'] as $key)if(!is_bool($in[$key]??null))fail(400,'Invalid privacy option.');
        transaction(function()use($me,$visibility,$in){query('SELECT id FROM accounts WHERE id=? FOR UPDATE',[$me['id']]);query('INSERT INTO account_preferences(account_id,visibility,allow_requests,show_online) VALUES(?,?,?,?) ON DUPLICATE KEY UPDATE visibility=VALUES(visibility),allow_requests=VALUES(allow_requests),show_online=VALUES(show_online)',[$me['id'],$visibility,(int)$in['allow_requests'],(int)$in['show_online']]);});return ['ok'=>true];
    }
    private function blocked(string $a,string $b): bool {return (bool)query('SELECT 1 FROM account_blocks WHERE (owner=? AND target=?) OR (owner=? AND target=?)',[$a,$b,$b,$a])->fetchColumn();}
    private function areFriends(string $a,string $b): bool {return !$this->blocked($a,$b)&&(bool)query('SELECT 1 FROM friendships WHERE accepted=1 AND ((sender=? AND recipient=?) OR (sender=? AND recipient=?))',[$a,$b,$b,$a])->fetchColumn();}
    private function otherAccount(array $in): array {
        $name=clean_text($in['username']??null,24);$other=query('SELECT id,username FROM accounts WHERE username_key=?',[strtolower($name)])->fetch();
        if(!$other||$other['id']===$this->requireAccount()['id'])fail(404,'Commander not available.');return $other;
    }
    public function contact(array $in): array {
        $this->limit();fields($in,['action','username','server','invitation']);$me=$this->requireAccount();$action=$in['action']??'';
        if($action==='dismiss'){query('DELETE FROM server_invitations WHERE id=? AND recipient=?',[$in['invitation']??'',$me['id']]);return ['ok'=>true];}
        $other=$this->otherAccount($in);$a=$me['id'];$b=$other['id'];
        // Serialize both users in a consistent order: block/request/invite races cannot bypass privacy.
        return transaction(function()use($a,$b,$action,$in){
            foreach(array_unique([$a<$b?$a:$b,$a<$b?$b:$a]) as $id)query('SELECT id FROM accounts WHERE id=? FOR UPDATE',[$id]);
            if($action==='block'){
                query('INSERT IGNORE INTO account_blocks(owner,target) VALUES(?,?)',[$a,$b]);
                query('DELETE FROM friendships WHERE (sender=? AND recipient=?) OR (sender=? AND recipient=?)',[$a,$b,$b,$a]);
                query('DELETE FROM server_invitations WHERE (sender=? AND recipient=?) OR (sender=? AND recipient=?)',[$a,$b,$b,$a]);return ['ok'=>true];
            }
            if($action==='unblock'){query('DELETE FROM account_blocks WHERE owner=? AND target=?',[$a,$b]);return ['ok'=>true];}
            if($action==='remove'){query('DELETE FROM server_invitations WHERE (sender=? AND recipient=?) OR (sender=? AND recipient=?)',[$a,$b,$b,$a]);query('DELETE FROM friendships WHERE (sender=? AND recipient=?) OR (sender=? AND recipient=?)',[$a,$b,$b,$a]);return ['ok'=>true];}
            if($this->blocked($a,$b))fail(403,'Contact is unavailable.');
            if($action==='request'){
                if(!query('SELECT allow_requests FROM account_preferences WHERE account_id=?',[$b])->fetchColumn())fail(403,'This commander is not accepting requests.');
                if($this->areFriends($a,$b)||query('SELECT 1 FROM friendships WHERE sender=? AND recipient=?',[$b,$a])->fetchColumn())return ['ok'=>true];
                query('INSERT IGNORE INTO friendships(sender,recipient) VALUES(?,?)',[$a,$b]);
            }elseif($action==='accept'){
                if(!query('SELECT 1 FROM friendships WHERE sender=? AND recipient=?',[$b,$a])->fetchColumn())fail(404,'No incoming request.');
                query('UPDATE friendships SET accepted=1 WHERE sender=? AND recipient=?',[$b,$a]);query('DELETE FROM friendships WHERE sender=? AND recipient=?',[$a,$b]);
            }elseif($action==='invite'){
                if(!$this->areFriends($a,$b))fail(403,'Accept a friend request before inviting.');
                $server=clean_text($in['server']??null,64);$live=array_column($this->servers(),'id');if(!in_array($server,$live,true))fail(404,'That server is offline.');
                query('DELETE FROM server_invitations WHERE sender=? AND recipient=?',[$a,$b]);
                query('INSERT INTO server_invitations(id,sender,recipient,server_id,expires_at) VALUES(?,?,?,?,UTC_TIMESTAMP(6)+INTERVAL 15 MINUTE)',[random_token(),$a,$b,$server]);
            }else fail(400,'Unknown community action.');return ['ok'=>true];
        });
    }
    public function community(): array {
        $me=$this->requireAccount();$id=$me['id'];
        query('INSERT INTO account_preferences(account_id,last_active) VALUES(?,UTC_TIMESTAMP(6)) ON DUPLICATE KEY UPDATE last_active=UTC_TIMESTAMP(6)',[$id]);
        $rows=query('SELECT a.username,f.sender,f.recipient,f.accepted,COALESCE(p.show_online,0) AND p.last_active>UTC_TIMESTAMP(6)-INTERVAL 3 MINUTE AS online FROM friendships f JOIN accounts a ON a.id=IF(f.sender=?,f.recipient,f.sender) LEFT JOIN account_preferences p ON p.account_id=a.id WHERE f.sender=? OR f.recipient=?',[$id,$id,$id])->fetchAll();
        $friends=[];foreach($rows as $row){$other=$row['sender']===$id?$row['recipient']:$row['sender'];if(!$this->blocked($id,$other))$friends[]=['username'=>$row['username'],'status'=>$row['accepted']?'friend':($row['recipient']===$id?'incoming':'outgoing'),'online'=>(bool)$row['online']&&(bool)$row['accepted']];}
        $invitations=query('SELECT i.id,a.id AS sender_id,a.username,i.server_id,i.expires_at FROM server_invitations i JOIN accounts a ON a.id=i.sender WHERE i.recipient=? AND i.expires_at>UTC_TIMESTAMP(6) ORDER BY i.expires_at DESC',[$id])->fetchAll();
        $invitations=array_values(array_filter($invitations,fn($i)=>$this->areFriends($id,$i['sender_id'])));foreach($invitations as &$invite)unset($invite['sender_id']);
        return ['friends'=>$friends,'invitations'=>$invitations,'blocked'=>query('SELECT a.username FROM account_blocks b JOIN accounts a ON a.id=b.target WHERE b.owner=?',[$id])->fetchAll()];
    }
    public function sessions(): array {
        $me=$this->requireAccount();$rows=query('SELECT token_hash AS id,expires_at FROM sessions WHERE account_id=? AND expires_at>UTC_TIMESTAMP(6) ORDER BY expires_at DESC',[$me['id']])->fetchAll();
        foreach($rows as &$row)$row['current']=hash_equals(token_hash(session_token()),$row['id']);return $rows;
    }
    public function revokeSession(array $in): array {
        $me=$this->requireAccount();fields($in,['id']);$id=$in['id']??'';
        if($id==='others')query('DELETE FROM sessions WHERE account_id=? AND token_hash<>?',[$me['id'],token_hash(session_token())]);
        else {if(!is_hex_id($id))fail(400,'Invalid session.');query('DELETE FROM sessions WHERE account_id=? AND token_hash=?',[$me['id'],$id]);if(hash_equals(token_hash(session_token()),$id))session_cookie('');}return ['ok'=>true];
    }
    public function newRecoveryCodes(array $in): array {
        $this->limit();$me=$this->requireAccount();fields($in,['password']);if(!is_string($in['password']??null)||strlen($in['password'])>128)fail(401,'Current password is incorrect.');
        $codes=[];for($i=0;$i<8;++$i)$codes[]=bin2hex(random_bytes(16));
        transaction(function()use($me,$codes,$in){$row=query('SELECT * FROM accounts WHERE id=? FOR UPDATE',[$me['id']])->fetch();if(!$this->verifies($in['password'],$row))fail(401,'Current password is incorrect.');query('DELETE FROM recovery_codes WHERE account_id=?',[$me['id']]);foreach($codes as $code)query('INSERT INTO recovery_codes(code_hash,account_id) VALUES(?,?)',[token_hash($code),$me['id']]);});return ['codes'=>$codes];
    }
    public function recoverPassword(array $in): array {
        $this->limit();fields($in,['username','code','password']);$password=$this->validPassword($in['password']??null);$name=clean_text($in['username']??null,24);$code=clean_text($in['code']??null,64);
        transaction(function()use($name,$code,$password){
            $account=query('SELECT * FROM accounts WHERE username_key=? FOR UPDATE',[strtolower($name)])->fetch();
            if(!$account||!query('SELECT 1 FROM recovery_codes WHERE account_id=? AND code_hash=?',[$account['id'],token_hash($code)])->fetchColumn())fail(401,'Recovery details are incorrect or the code was already used.');
            query('DELETE FROM recovery_codes WHERE account_id=? AND code_hash=?',[$account['id'],token_hash($code)]);
            query("UPDATE accounts SET password_hash=?,password_scheme='argon2id',legacy_salt=NULL WHERE id=?",[$this->hashPassword($password),$account['id']]);
            query('DELETE FROM sessions WHERE account_id=?',[$account['id']]);query('DELETE FROM join_tickets WHERE account_id=?',[$account['id']]);
        });session_cookie('');return ['ok'=>true];
    }
}
