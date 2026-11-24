<?php
declare(strict_types=1);
if (PHP_SAPI!=='cli')exit(1);
require __DIR__.'/../src/bootstrap.php';
foreach(['sessions','join_tickets','connection_grants'] as $table)query("DELETE FROM $table WHERE expires_at<UTC_TIMESTAMP(6)");
query('DELETE FROM rate_limits WHERE window_id<?',[intdiv(time(),60)-60]);
query('DELETE FROM servers WHERE last_seen<UTC_TIMESTAMP(6)-INTERVAL 1 DAY');
echo "Expired credentials and transient listings cleaned. Match history retained.\n";
