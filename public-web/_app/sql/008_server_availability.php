<?php
declare(strict_types=1);
if(PHP_SAPI!=='cli'){http_response_code(404);exit;}
return <<<'SQL'
CREATE TABLE IF NOT EXISTS server_availability (
 server_id VARCHAR(64) CHARACTER SET ascii COLLATE ascii_bin PRIMARY KEY,
 player_slots TINYINT UNSIGNED NOT NULL, spectators INT UNSIGNED NOT NULL,
 bots TINYINT UNSIGNED NOT NULL, difficulty TINYINT UNSIGNED NOT NULL,
 speed_percent SMALLINT UNSIGNED NOT NULL,
 FOREIGN KEY(server_id) REFERENCES server_keys(id) ON DELETE CASCADE
) ENGINE=InnoDB;
INSERT IGNORE INTO schema_migrations(version) VALUES(8);
SQL;
