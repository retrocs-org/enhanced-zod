<?php
declare(strict_types=1);
define('ZODEE_INTERNAL', true);
if (PHP_VERSION_ID < 80400) { http_response_code(503); exit('zod-ee requires PHP 8.4 or newer. Check the PHP version configured for this vhost.'); }
require __DIR__.'/_app/src/service.php';
require_once __DIR__.'/_app/src/api_catalog.php';
security_headers();
$path=rawurldecode(parse_url($_SERVER['REQUEST_URI'],PHP_URL_PATH)??'/');
$method=$_SERVER['REQUEST_METHOD']??'GET';$api=str_starts_with($path,'/api/')||in_array($path,['/matches','/healthz'],true);
$service=new ZodService();
try {
    if ($api) {
        if (!in_array($method,['GET','HEAD'],true)) require_origin();
        $route=$method.' '.$path;
        $result=match($route) {
            'GET /healthz'=>['ok'=>(int)query('SELECT MAX(version) FROM schema_migrations')->fetchColumn()>=1],
            'POST /api/accounts'=>$service->register(json_input()),
            'POST /api/login'=>$service->login(json_input()),
            'POST /api/logout'=>$service->logout(),
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
    if ($method==='POST' && str_starts_with($path,'/account/')) {
        require_csrf();
        $data=$_POST;unset($data['csrf']);
        switch($path) {
            case '/account/login':$service->login($data);$message='Welcome back. You are signed in.';break;
            case '/account/register':$service->register($data);$message='Account created. Sign in to join a public server.';break;
            case '/account/logout':$service->logout();$message='You are signed out.';break;
            case '/account/password':$service->changePassword($data);$message='Password updated. Other website sessions are signed out.';break;
            default:fail(404,'Page not found.');
        }
        header('Location: /account?message='.rawurlencode($message),true,303);exit;
    }
    if ($method!=='GET' && $method!=='HEAD') fail(405,'Method not allowed.');
    require __DIR__.'/_app/src/views.php';
    render_page($path,$service);
} catch(ApiError $e) {
    if ($api) json_response(['error'=>$e->getMessage()],$e->status);
    if ($method==='POST' && str_starts_with($path,'/account/')) {header('Location: /account?error='.rawurlencode($e->getMessage()),true,303);exit;}
    http_response_code($e->status);require_once __DIR__.'/_app/src/views.php';render_error($e->status,$e->getMessage());
} catch(Throwable $e) {
    error_log('Zod web request failed: '.$e::class.' ['.$e->getCode().']');
    if ($api) json_response(['error'=>'Service temporarily unavailable. Please retry shortly.'],503);
    http_response_code(503);require_once __DIR__.'/_app/src/views.php';render_error(503,'The service is temporarily unavailable. Please try again shortly.');
}
