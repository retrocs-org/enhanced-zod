<?php
// Development server only. Production Apache serves public-web/ with FallbackResource.
$path=rawurldecode(parse_url($_SERVER['REQUEST_URI'],PHP_URL_PATH)??'/');
$file=realpath(__DIR__.$path);
if ($file!==false && str_starts_with($file,__DIR__.'/assets/') && is_file($file)) return false;
require __DIR__.'/index.php';
