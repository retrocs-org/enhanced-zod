<?php
declare(strict_types=1);
if(PHP_SAPI!=='cli'){http_response_code(404);exit;}
return <<<'SQL'
ALTER TABLE match_players ADD COLUMN IF NOT EXISTS team TINYINT UNSIGNED NOT NULL DEFAULT 0;
ALTER TABLE server_owners ADD COLUMN IF NOT EXISTS maintenance_at BIGINT UNSIGNED NOT NULL DEFAULT 0;
ALTER TABLE server_owners ADD COLUMN IF NOT EXISTS maintenance_action VARCHAR(12) NOT NULL DEFAULT 'stop';
ALTER TABLE server_owners ADD COLUMN IF NOT EXISTS maintenance_reason VARCHAR(160) NOT NULL DEFAULT '';
INSERT IGNORE INTO schema_migrations(version) VALUES(7);
SQL;
