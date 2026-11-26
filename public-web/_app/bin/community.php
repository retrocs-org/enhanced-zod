<?php
declare(strict_types=1);
if(PHP_SAPI!=='cli'){http_response_code(404);exit;}
require __DIR__.'/../src/service.php';
$args=getopt('',['id:','approve','hide','region:','mode:','server:','list','export:']);
try{
 if(isset($args['list'])){echo json_encode(query('SELECT p.title,a.username,v.id,v.version,v.approved,v.created_at FROM community_versions v JOIN community_projects p ON p.id=v.project_id JOIN accounts a ON a.id=p.owner ORDER BY v.created_at DESC LIMIT 100')->fetchAll(),JSON_PRETTY_PRINT|JSON_UNESCAPED_SLASHES)."\n";exit;}
 if(isset($args['export'])){
  if(!is_hex_id($args['id']??null))throw new RuntimeException('Use --id REVISION with --export DIRECTORY.');
  $row=query('SELECT filename,archive,screenshot,screenshot_type FROM community_versions WHERE id=?',[$args['id']])->fetch();if(!$row)throw new RuntimeException('Revision not found.');
  $folder=$args['export'];if(is_link($folder)||(!is_dir($folder)&&!mkdir($folder,0700,true)))throw new RuntimeException('Choose a real writable export directory.');
  foreach([$row['filename']=>$row['archive'],'preview.'.($row['screenshot_type']==='image/png'?'png':'jpg')=>$row['screenshot']] as $name=>$bytes){$path=$folder.'/'.$args['id'].'-'.$name;$file=fopen($path,'x');if(!$file)throw new RuntimeException('Refusing to overwrite an export.');fwrite($file,$bytes);fclose($file);chmod($path,0600);}echo "Review files exported. No approval performed.\n";exit;
 }
 if(isset($args['server'])){query('INSERT INTO server_metadata(server_id,region,game_mode) VALUES(?,?,?) ON DUPLICATE KEY UPDATE region=VALUES(region),game_mode=VALUES(game_mode)',[clean_text($args['server'],64),clean_text($args['region']??'Unspecified',32),clean_text($args['mode']??'Skirmish',32)]);echo "Server metadata updated.\n";exit;}
 if(!is_hex_id($args['id']??null)||isset($args['approve'])===isset($args['hide']))throw new RuntimeException('Use --id REVISION --approve or --hide; review author permissions and files before approval.');
 $result=query('UPDATE community_versions SET approved=? WHERE id=?',[isset($args['approve'])?1:0,$args['id']]);if(!$result->rowCount())throw new RuntimeException('Revision missing or unchanged.');echo "Creation visibility updated.\n";
}catch(Throwable $error){fwrite(STDERR,$error->getMessage()."\n");exit(1);}
