<?php
declare(strict_types=1);
define('ZODEE_INTERNAL', true);
if (PHP_VERSION_ID < 80400) { http_response_code(503); exit('zod-enhanced requires PHP 8.4 or newer. Check the PHP version configured for this vhost.'); }
require __DIR__.'/_app/src/service.php';
require_once __DIR__.'/_app/src/api_catalog.php';
security_headers();
$path=rawurldecode(parse_url($_SERVER['REQUEST_URI'],PHP_URL_PATH)??'/');
// PATH_INFO works on standard Apache PHP setups without rewrite rules.
if ($path === '/index.php') $path = '/';
elseif (str_starts_with($path, '/index.php/')) $path = substr($path, strlen('/index.php'));
$path = rtrim($path, '/') ?: '/';
$method=$_SERVER['REQUEST_METHOD']??'GET';$api=str_starts_with($path,'/api/')||in_array($path,['/matches','/healthz'],true);
$service=new ZodService();
try {
    if ($api) {
        if (!in_array($method,['GET','HEAD'],true)) require_origin();
        $route=$method.' '.$path;
        $result=match($route) {
            'GET /healthz'=>['ok'=>(int)query('SELECT MAX(version) FROM schema_migrations')->fetchColumn()>=4],
            'POST /api/accounts'=>$service->register(json_input()),
            'POST /api/login'=>$service->login(json_input()),
            'POST /api/logout'=>$service->logout(),
            'POST /api/connect/start'=>$service->startNativeConnect(json_input()),
            'POST /api/connect/poll'=>$service->pollNativeConnect(json_input()),
            'POST /api/connect/cancel'=>$service->cancelNativeConnect(json_input()),
            'GET /api/admin'=>$service->moderation(),
            'POST /api/admin/actions'=>$service->moderate(json_input()),
            'POST /api/reports'=>$service->report(json_input()),
            'GET /api/community'=>$service->community(),
            'POST /api/community'=>$service->contact(json_input()),
            'GET /api/account/privacy'=>$service->preferences(),
            'POST /api/account/privacy'=>$service->setPreferences(json_input()),
            'GET /api/account/sessions'=>$service->sessions(),
            'POST /api/account/sessions'=>$service->revokeSession(json_input()),
            'POST /api/account/recovery-codes'=>$service->newRecoveryCodes(json_input()),
            'POST /api/recover'=>$service->recoverPassword(json_input()),
            'GET /api/creations'=>$service->creations(),
            'GET /api/me'=>$service->requireAccount(),
            'POST /api/account/password'=>$service->changePassword(json_input()),
            'GET /api/catalog'=>array_map(fn($entry)=>array_combine(['method','path','access','description','browsable'],$entry),api_catalog()),
            'GET /api/servers','GET /matches'=>$service->servers(),
            'POST /api/servers/heartbeat'=>$service->heartbeat(json_input()),
            'DELETE /api/servers/self'=>$service->removeServer(),
            'POST /api/tickets'=>$service->ticket(json_input()),
            'POST /api/tickets/redeem'=>$service->redeem(json_input()),
            'POST /api/results'=>$service->result(json_input()),
            'GET /api/leaderboard'=>$service->leaderboard(),
            'GET /api/statistics'=>['totals'=>$service->totals(),'maps'=>$service->maps(),'recent'=>$service->recent()],
            default=>fail(404,'Endpoint not found.'),
        };
        json_response($result,$route==='POST /api/accounts'?201:200);
    }
    if($method==='POST'&&in_array($path,['/admin/action','/reports'],true)){require_csrf();$data=$_POST;unset($data['csrf']);if($path==='/admin/action')$service->moderate($data);else $service->report($data);header('Location: '.route_url($path==='/admin/action'?'/admin':'/account').'?message=Saved',true,303);exit;}
    if($method==='POST'&&$path==='/connect/approve'){
        require_csrf();$data=$_POST;unset($data['csrf']);$service->approveNativeConnect($data);
        require_once __DIR__.'/_app/src/views.php';page_header('connect',$service->currentAccount());echo '<section class="shell section"><h1>Connection approved</h1><p>Return to your game. It will receive a one-use ticket and connect automatically.</p></section>';page_footer();exit;
    }
    if($method==='GET'&&preg_match('~^/creations/(image|download)/([a-f0-9]{64})$~',$path,$asset))$service->creationFile($asset[2],$asset[1]==='image');
    if($method==='POST'&&$path==='/creations/submit'){
        require_csrf();$data=$_POST;unset($data['csrf']);$result=$service->submitCreation($data,$_FILES);
        header('Location: '.$result['preview'],true,303);exit;
    }
    if ($method==='POST' && str_starts_with($path,'/account/')) {
        require_csrf();
        $data=$_POST;unset($data['csrf']);$returnCode=$data['return_to']??'';unset($data['return_to']);
        switch($path) {
            case '/account/privacy':$data['allow_requests']=isset($data['allow_requests']);$data['show_online']=isset($data['show_online']);$service->setPreferences($data);$message='Privacy updated.';break;
            case '/account/contact':$service->contact($data);$message='Community settings updated.';break;
            case '/account/sessions':$service->revokeSession($data);$message='Session access updated.';break;
            case '/account/recover':$service->recoverPassword($data);$message='Password recovered. Sign in with the new password.';break;
            case '/account/recovery-codes':
                $codes=$service->newRecoveryCodes($data)['codes'];require_once __DIR__.'/_app/src/views.php';page_header('account',$service->currentAccount());
                echo '<section class="shell section"><h1>Save your recovery codes</h1><p>These are shown once. Keep them offline. Each code works once; generating a new set replaces the old set.</p><pre>'.e(implode("\n",$codes)).'</pre><a href="'.e(route_url('/account')).'">Return to account</a></section>';page_footer();exit;
            case '/account/login':$service->login($data);$message='Welcome back. You are signed in.';break;
            case '/account/register':$service->register($data);$message='Account created. Sign in to join a public server.';break;
            case '/account/logout':$service->logout();$message='You are signed out.';break;
            case '/account/password':$service->changePassword($data);$message='Password updated. Other website sessions are signed out.';break;
            default:fail(404,'Page not found.');
        }
        if(preg_match('/\A[A-F0-9]{12}\z/D',is_string($returnCode)?$returnCode:'')){header('Location: '.route_url($path==='/account/login'?'/connect':'/account').'?'.($path==='/account/login'?'code=':'connect=').$returnCode,true,303);exit;}
        header('Location: '.route_url($path==='/account/contact'?'/community':'/account').'?message='.rawurlencode($message),true,303);exit;
    }
    if ($method!=='GET' && $method!=='HEAD') fail(405,'Method not allowed.');
    require __DIR__.'/_app/src/views.php';
    render_page($path,$service);
} catch(ApiError $e) {
    if ($api) json_response(['error'=>$e->getMessage()],$e->status);
    if ($method==='POST' && str_starts_with($path,'/account/')) {header('Location: '.route_url($path==='/account/contact'?'/community':'/account').'?error='.rawurlencode($e->getMessage()).(isset($returnCode)&&is_string($returnCode)&&preg_match('/\A[A-F0-9]{12}\z/D',$returnCode)?'&connect='.$returnCode:''),true,303);exit;}
    http_response_code($e->status);require_once __DIR__.'/_app/src/views.php';render_error($e->status,$e->getMessage());
} catch(Throwable $e) {
    error_log('Zod web request failed: '.$e::class.' ['.$e->getCode().']');
    if ($api) json_response(['error'=>'Service temporarily unavailable. Please retry shortly.'],503);
    http_response_code(503);require_once __DIR__.'/_app/src/views.php';render_error(503,'The service is temporarily unavailable. Please try again shortly.');
}
