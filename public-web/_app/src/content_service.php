<?php
declare(strict_types=1);
if (!defined('ZODEE_INTERNAL') && PHP_SAPI !== 'cli') { http_response_code(404); exit; }
trait ContentService {
    public function creations(): array {
        $owner=$this->currentAccount()['id']??'';
        return query('SELECT p.id,p.title,p.description,p.kind,a.username,v.id AS revision,v.version,v.engine_min,v.notes,v.sha256,v.approved,v.created_at FROM community_projects p JOIN accounts a ON a.id=p.owner JOIN community_versions v ON v.project_id=p.id WHERE v.approved=1 OR p.owner=? ORDER BY v.created_at DESC LIMIT 200',[$owner])->fetchAll();
    }
    public function creationVersion(string $id): array {
        $row=query('SELECT p.*,a.username,v.id AS revision,v.version,v.engine_min,v.notes,v.sha256,v.filename,v.manifest,v.approved,v.created_at FROM community_versions v JOIN community_projects p ON p.id=v.project_id JOIN accounts a ON a.id=p.owner WHERE v.id=?',[$id])->fetch();
        if(!$row||(!$row['approved']&&($this->currentAccount()['id']??'')!==$row['owner']&&!$this->isOperator()))fail(404,'Creation not found.');return $row;
    }
    public function creationHistory(string $project): array {
        return query('SELECT v.id,v.version,v.notes,v.created_at FROM community_versions v JOIN community_projects p ON p.id=v.project_id WHERE p.id=? AND (v.approved=1 OR p.owner=?) ORDER BY v.created_at DESC LIMIT 100',[$project,$this->currentAccount()['id']??''])->fetchAll();
    }
    public function creationFile(string $id,bool $image): never {
        $row=$this->creationVersion($id);$file=query('SELECT archive,screenshot,screenshot_type FROM community_versions WHERE id=?',[$id])->fetch();
        header('Content-Type: '.($image?$file['screenshot_type']:'application/octet-stream'));
        header('Content-Disposition: '.($image?'inline':'attachment').'; filename="'.($image?'preview':$row['filename']).'"');
        header('Cache-Control: private, no-store');header('X-Content-Type-Options: nosniff');echo $file[$image?'screenshot':'archive'];exit;
    }
    public static function inspectCreation(string $filename,string $bytes): array {
        if(strlen($bytes)>8*1024*1024)fail(400,'Creations may be at most 8 MiB.');
        if(str_ends_with(strtolower($filename),'.map')){
            if(strlen($bytes)<64)fail(400,'Invalid map header.');$dimensions=unpack('vwidth/vheight',$bytes);
            if(!$dimensions['width']||!$dimensions['height']||$dimensions['width']>2048||$dimensions['height']>2048||strlen($bytes)<64+2*$dimensions['width']*$dimensions['height'])fail(400,'Invalid map dimensions or truncated terrain.');
            return ['kind'=>'map','files'=>[$filename],'width'=>$dimensions['width'],'height'=>$dimensions['height'],'note'=>'Preview only; the game performs full map validation when opened.'];
        }
        if(!str_ends_with(strtolower($filename),'.zip'))fail(400,'Upload a .map file or campaign .zip.');
        if(!class_exists(ZipArchive::class))fail(503,'Campaign sharing requires the PHP zip extension.');
        $temp=tempnam(sys_get_temp_dir(),'zod-share-');if(!$temp)throw new RuntimeException('No temporary storage');
        $zip=new ZipArchive();$opened=false;try{
            file_put_contents($temp,$bytes);$opened=$zip->open($temp)===true;if(!$opened||$zip->numFiles<1||$zip->numFiles>1024)fail(400,'Invalid campaign ZIP.');
            $files=[];$seen=[];$total=0;$manifest='';$manifestName='';
            for($i=0;$i<$zip->numFiles;++$i){
                $stat=$zip->statIndex($i);$name=$stat['name'];$normalized=strtolower(rtrim($name,'/'));
                if(!preg_match('~\A[A-Za-z0-9 _./-]{1,240}\z~D',$name)||str_contains($name,'//')||preg_match('~(^|/)\.{1,2}(/|$)~',$name)||str_starts_with($name,'/')||isset($seen[$normalized]))fail(400,'Unsafe or duplicate campaign path.');
                $seen[$normalized]=true;$opsys=0;$attrs=0;$zip->getExternalAttributesIndex($i,$opsys,$attrs);if((($attrs>>16)&0170000)===0120000)fail(400,'Linked campaign entries are unsupported.');
                if(($stat['encryption_method']??0)!==0)fail(400,'Encrypted campaign entries are unsupported.');
                $total+=$stat['size'];if($total>64*1024*1024||$stat['size']>16*1024*1024)fail(400,'Expanded campaign is too large.');
                if(str_ends_with($name,'/'))continue;
                if(!in_array(strtolower(pathinfo($name,PATHINFO_EXTENSION)),['map','ini','txt','md','png','jpg','jpeg','mp4','webm','srt'],true))fail(400,'Unsupported campaign file type.');
                $files[]=$name;
                if(basename($name)==='campaign.ini'){if($manifestName!=='')fail(400,'Use exactly one campaign.ini.');$manifest=$zip->getFromIndex($i,65537);$manifestName=$name;if(!is_string($manifest)||!mb_check_encoding($manifest,'UTF-8'))fail(400,'Invalid campaign manifest text.');if(strlen($manifest)>65536)fail(400,'Campaign manifest too large.');}
            }
            if($manifestName===''||!preg_match('/^format=1\s*$/m',$manifest)||!preg_match('/^id=[a-z0-9][a-z0-9_-]{0,63}\s*$/m',$manifest)||!preg_match('/^title=.+$/m',$manifest)||!str_contains($manifest,'[mission]')||!preg_match('/^map=.+\.map\s*$/m',$manifest))fail(400,'Missing campaign format or mission maps.');
            $prefix=dirname($manifestName);$prefix=$prefix==='.'?'':$prefix.'/';
            foreach($files as $file)if(!str_starts_with($file,$prefix))fail(400,'Files must stay inside the campaign folder.');
            preg_match_all('/^map=(.+)\s*$/m',$manifest,$matches);
            foreach($matches[1] as $map)if(!in_array($prefix.trim($map),$files,true))fail(400,'Campaign references a missing map.');
            preg_match('/^engine_min=(.+)$/m',$manifest,$min);preg_match('/^version=(.+)$/m',$manifest,$packVersion);preg_match('/^engine_max=(.+)$/m',$manifest,$max);preg_match_all('/^requires=(.+)$/m',$manifest,$dependencies);
            return ['kind'=>'campaign','files'=>$files,'missions'=>count($matches[1]),'version'=>trim($packVersion[1]??''),'engine_min'=>trim($min[1]??''),'engine_max'=>trim($max[1]??''),'requires'=>array_map('trim',$dependencies[1]),'note'=>'Preview only; the game checks objectives, required media and compatibility before installation.'];
        }finally{if($opened)$zip->close();unlink($temp);}
    }
    public function submitCreation(array $in,array $uploads): array {
        $this->limit();$me=$this->requireAccount();fields($in,['project','title','description','version','engine_min','notes','rights']);
        if(($in['rights']??'')!=='yes')fail(400,'Confirm permission to redistribute every file and screenshot.');
        $title=clean_text($in['title']??null,80);$description=clean_text($in['description']??null,2000);$version=clean_text($in['version']??null,32);$engine=clean_text($in['engine_min']??null,32);$notes=clean_text($in['notes']??'',1000,true);
        foreach([$version,$engine] as $v)if(!preg_match('/\A[0-9]+\.[0-9]+(?:\.[0-9]+)?\z/D',$v))fail(400,'Use numeric versions such as 1.0.0.');
        foreach(['creation','screenshot'] as $key){$file=$uploads[$key]??[];if(($file['error']??1)!==UPLOAD_ERR_OK||!is_uploaded_file($file['tmp_name']??''))fail(400,'Choose a creation and a screenshot.');}
        $archive=$uploads['creation'];$filename=basename($archive['name']);if(!preg_match('/\A[A-Za-z0-9_. -]{1,100}\z/D',$filename))fail(400,'Invalid filename.');
        if(filesize($archive['tmp_name'])>8*1024*1024||filesize($uploads['screenshot']['tmp_name'])>2*1024*1024)fail(400,'Creation limit: 8 MiB. Screenshot limit: 2 MiB.');
        $bytes=file_get_contents($archive['tmp_name']);$manifest=self::inspectCreation($filename,$bytes);if(!empty($manifest['version'])&&$manifest['version']!==$version)fail(400,'Version must match the campaign manifest.');if(!empty($manifest['engine_min'])&&version_compare($engine,$manifest['engine_min'],'<'))fail(400,'Minimum engine version cannot be lower than the campaign manifest requirement.');$image=file_get_contents($uploads['screenshot']['tmp_name']);$info=@getimagesizefromstring($image);
        if(!$info||!in_array($info['mime'],['image/png','image/jpeg'],true)||$info[0]>2048||$info[1]>2048)fail(400,'Screenshot must be PNG/JPEG, at most 2048 × 2048.');
        $project=$in['project']??'';if($project!==''&&!is_hex_id($project))fail(400,'Invalid project identifier.');$id=random_token();
        try{transaction(function()use(&$project,$me,$title,$description,$manifest,$id,$version,$engine,$notes,$filename,$bytes,$image,$info){
            query('SELECT id FROM accounts WHERE id=? FOR UPDATE',[$me['id']]);
            $usage=query('SELECT COALESCE(SUM(OCTET_LENGTH(v.archive)+OCTET_LENGTH(v.screenshot)),0) AS bytes,SUM(v.approved=0) AS pending FROM community_versions v JOIN community_projects p ON p.id=v.project_id WHERE p.owner=?',[$me['id']])->fetch();
            $quota=max(8,min(4096,(int)env_value('ZOD_CREATION_QUOTA_MB','128')))*1024*1024;
            if((int)$usage['bytes']+strlen($bytes)+strlen($image)>$quota)fail(413,'Your creation storage quota is full. Contact the site operator before uploading more versions.');
            if((int)$usage['pending']>=10)fail(429,'Wait for existing submissions to be reviewed before uploading more.');
            if($project===''){$project=random_token();query('INSERT INTO community_projects(id,owner,title,description,kind) VALUES(?,?,?,?,?)',[$project,$me['id'],$title,$description,$manifest['kind']]);}
            else {$row=query('SELECT owner,kind FROM community_projects WHERE id=? FOR UPDATE',[$project])->fetch();if(!$row||$row['owner']!==$me['id']||$row['kind']!==$manifest['kind'])fail(403,'Only the author may add a version of this creation.');}
            query('INSERT INTO community_versions(id,project_id,version,engine_min,notes,filename,sha256,manifest,archive,screenshot,screenshot_type) VALUES(?,?,?,?,?,?,?,?,?,?,?)',[$id,$project,$version,$engine,$notes,$filename,hash('sha256',$bytes),json_encode($manifest,JSON_THROW_ON_ERROR),$bytes,$image,$info['mime']]);
        });}catch(PDOException $error){if(($error->errorInfo[1]??0)===1062)fail(409,'That version already exists.');throw $error;}
        return ['id'=>$id,'project'=>$project,'status'=>'pending','preview'=>route_url('/creations').'?version='.$id];
    }
}
