#!/usr/bin/env php
<?php
declare(strict_types=1);
if (PHP_SAPI !== 'cli') exit(1);
require __DIR__.'/../src/bootstrap.php';
try {
    foreach (glob(__DIR__.'/../sql/*.sql') as $file) db()->exec(file_get_contents($file));
    echo "MariaDB schema is ready.\n";
} catch (Throwable $e) { fwrite(STDERR,"Schema migration failed: ".$e->getMessage()."\n");exit(1); }
