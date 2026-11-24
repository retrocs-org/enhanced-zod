<?php
declare(strict_types=1);
if (PHP_SAPI !== 'cli') { http_response_code(404); exit; }
require __DIR__.'/../src/bootstrap.php';
try {
    foreach (glob(__DIR__.'/../sql/*.php') as $file) db()->exec(require $file);
    echo "MariaDB schema is ready.\n";
} catch (Throwable $e) { fwrite(STDERR,"Schema migration failed: ".$e->getMessage()."\n");exit(1); }
