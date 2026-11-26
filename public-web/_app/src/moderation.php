<?php
declare(strict_types=1);
if(!defined('ZODEE_INTERNAL')&&PHP_SAPI!=='cli'){http_response_code(404);exit;}
trait ModerationService {
    public function isOperator(): bool {
        $me=$this->currentAccount();if(!$me)return false;
        $names=array_filter(array_map(fn($name)=>strtolower(trim($name)),explode(',',env_value('ZOD_ADMIN_USERS'))));
        return in_array(strtolower($me['username']),$names,true);
    }
    private function requireOperator(): array { $me=$this->requireAccount();if(!$this->isOperator())fail(403,'Operator access required.');return $me; }
    public function report(array $in): array {
        $this->limit();$me=$this->requireAccount();fields($in,['kind','target','reason']);
        $kind=$in['kind']??'';$target=clean_text($in['target']??null,64);$reason=clean_text($in['reason']??null,1000);
        if(!in_array($kind,['server','creation'],true))fail(400,'Choose a server or creation.');
        $exists=$kind==='server'?query('SELECT id FROM server_keys WHERE id=?',[$target])->fetchColumn():query('SELECT id FROM community_versions WHERE id=? AND approved=1',[$target])->fetchColumn();
        if(!$exists)fail(404,'Public item not found.');
        return transaction(function()use($me,$kind,$target,$reason){
            query('SELECT id FROM accounts WHERE id=? FOR UPDATE',[$me['id']]);
            if(query('SELECT COUNT(*) FROM community_reports WHERE reporter=? AND created_at>UTC_TIMESTAMP(6)-INTERVAL 1 DAY',[$me['id']])->fetchColumn()>=10)fail(429,'Daily report limit reached.');
            query('INSERT INTO community_reports(id,reporter,kind,target,reason) VALUES(?,?,?,?,?)',[random_token(),$me['id'],$kind,$target,$reason]);return ['ok'=>true];
        });
    }
    public function moderation(): array {
        $this->requireOperator();return [
            'creations'=>query('SELECT v.id,v.version,v.approved,v.filename,p.title,p.kind,a.username FROM community_versions v JOIN community_projects p ON p.id=v.project_id JOIN accounts a ON a.id=p.owner ORDER BY v.created_at DESC LIMIT 100')->fetchAll(),
            'servers'=>query('SELECT k.id,k.name,k.enabled,s.host,s.port,s.last_seen FROM server_keys k LEFT JOIN servers s ON s.id=k.id ORDER BY k.name LIMIT 100')->fetchAll(),
            'reports'=>query('SELECT r.*,a.username FROM community_reports r JOIN accounts a ON a.id=r.reporter WHERE r.resolved=0 ORDER BY r.created_at LIMIT 100')->fetchAll(),
            'audit'=>query('SELECT l.action,l.target,l.created_at,a.username FROM moderation_log l JOIN accounts a ON a.id=l.actor ORDER BY l.id DESC LIMIT 30')->fetchAll()];
    }
    public function moderate(array $in): array {
        $me=$this->requireOperator();fields($in,['action','target']);$action=$in['action']??'';$target=clean_text($in['target']??null,64);
        transaction(function()use($me,$action,$target){
            if(in_array($action,['approve','hide'],true)){
                if(!is_hex_id($target)||!query('SELECT id FROM community_versions WHERE id=? FOR UPDATE',[$target])->fetchColumn())fail(404,'Creation revision not found.');
                query('UPDATE community_versions SET approved=? WHERE id=?',[$action==='approve'?1:0,$target]);
            }elseif(in_array($action,['enable-server','disable-server'],true)){
                if(!query('SELECT id FROM server_keys WHERE id=? FOR UPDATE',[$target])->fetchColumn())fail(404,'Server not found.');
                query('UPDATE server_keys SET enabled=? WHERE id=?',[$action==='enable-server'?1:0,$target]);
                if($action==='disable-server'){query('DELETE FROM servers WHERE id=?',[$target]);query('DELETE FROM native_connections WHERE server_id=?',[$target]);query('DELETE FROM join_tickets WHERE server_id=?',[$target]);}
            }elseif($action==='resolve'){
                if(!is_hex_id($target)||!query('SELECT id FROM community_reports WHERE id=? FOR UPDATE',[$target])->fetchColumn())fail(404,'Report not found.');
                query('UPDATE community_reports SET resolved=1 WHERE id=?',[$target]);
            }else fail(400,'Unknown moderation action.');
            query('INSERT INTO moderation_log(actor,action,target) VALUES(?,?,?)',[$me['id'],$action,$target]);
        });return ['ok'=>true];
    }
}
function moderation_form(string $csrf,string $action,string $target,string $label):void { ?>
<form action="/admin/action" method="post"><input type="hidden" name="csrf" value="<?=e($csrf)?>"><input type="hidden" name="action" value="<?=e($action)?>"><input type="hidden" name="target" value="<?=e($target)?>"><button class="button"><?=e($label)?></button></form><?php }
function moderation_page(ZodService $service,string $csrf):void {
    $queue=$service->moderation();?><section class="shell section page-section"><?php title_block('OPERATOR / MODERATION','Review the community.','Review files and author permissions before approval. Existing server credentials are enabled here; new keys remain provisioned on the server.');?><h2>Community revisions</h2><div class="enhancement-grid"><?php foreach($queue['creations'] as $v):?><article class="panel panel-pad"><h3><?=e($v['title'])?> · <?=e($v['version'])?></h3><p><?=e($v['username'])?> · <?=e($v['kind'])?> · <?=$v['approved']?'Visible':'Pending / hidden'?></p><a href="/creations/download/<?=e($v['id'])?>" class="text-link">Download for review</a> <a href="/creations?version=<?=e($v['id'])?>" class="text-link">Preview revision</a><?php moderation_form($csrf,$v['approved']?'hide':'approve',$v['id'],$v['approved']?'Hide revision':'Approve revision');?></article><?php endforeach?></div><h2>Server approvals</h2><div class="enhancement-grid"><?php foreach($queue['servers'] as $s):?><article class="panel panel-pad"><h3><?=e($s['name'])?></h3><p><?=e($s['id'])?> · <?=$s['enabled']?'Enabled':'Disabled'?> · <?=e($s['host']??'Offline')?><?php if($s['port']):?>:<?=e($s['port'])?><?php endif?></p><?php moderation_form($csrf,$s['enabled']?'disable-server':'enable-server',$s['id'],$s['enabled']?'Disable listing and new joins':'Enable existing credential');?></article><?php endforeach?></div><h2>Open reports</h2><?php if(!$queue['reports']):?><p>No open reports.</p><?php endif?><?php foreach($queue['reports'] as $r):?><article class="panel panel-pad"><p><strong><?=e($r['kind'])?>: <?=e($r['target'])?></strong> · <?=e($r['username'])?></p><p><?=e($r['reason'])?></p><?php moderation_form($csrf,'resolve',$r['id'],'Mark reviewed');?></article><?php endforeach?><h2>Recent operator actions</h2><ul><?php foreach($queue['audit'] as $a):?><li><?=e($a['created_at'])?> · <?=e($a['username'])?> · <?=e($a['action'])?> · <?=e($a['target'])?></li><?php endforeach?></ul></section><?php
}
function report_page(ZodService $service,string $csrf):void {
    $service->requireAccount();?><section class="shell section page-section"><?php title_block('COMMUNITY / REPORT','Report a public item.','Reports are private to operators. Include a server ID or creation revision ID and explain the issue.');?><form action="/reports" method="post" class="panel panel-pad"><input type="hidden" name="csrf" value="<?=e($csrf)?>"><label>Item type<select name="kind"><option value="server">Server</option><option value="creation" <?=($_GET['kind']??'')==='creation'?'selected':''?>>Creation revision</option></select></label><label>Item ID<input name="target" value="<?=e(is_string($_GET['target']??null)?substr($_GET['target'],0,64):'')?>" maxlength="64" required></label><label>Reason<textarea name="reason" maxlength="1000" required rows="5"></textarea></label><button class="button button-primary">Send report</button></form></section><?php
}
