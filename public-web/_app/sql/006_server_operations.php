<?php
declare(strict_types=1);
if(PHP_SAPI!=='cli'){http_response_code(404);exit;}
return <<<'SQL'
ALTER TABLE server_admins ADD COLUMN IF NOT EXISTS role VARCHAR(16) NOT NULL DEFAULT 'admin';
ALTER TABLE server_owners ADD COLUMN IF NOT EXISTS hosting_enabled BOOLEAN NOT NULL DEFAULT TRUE;
ALTER TABLE server_owners ADD COLUMN IF NOT EXISTS host_revoked BOOLEAN NOT NULL DEFAULT FALSE;
CREATE TABLE IF NOT EXISTS server_health (
 server_id VARCHAR(64) CHARACTER SET ascii COLLATE ascii_bin PRIMARY KEY,
 snapshot MEDIUMTEXT NOT NULL, seen DATETIME(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
 FOREIGN KEY(server_id) REFERENCES server_keys(id) ON DELETE CASCADE
) ENGINE=InnoDB;
CREATE TABLE IF NOT EXISTS server_configs (
 server_id VARCHAR(64) CHARACTER SET ascii COLLATE ascii_bin PRIMARY KEY,
 revision BIGINT UNSIGNED NOT NULL DEFAULT 1, settings TEXT NOT NULL,
 FOREIGN KEY(server_id) REFERENCES server_keys(id) ON DELETE CASCADE
) ENGINE=InnoDB;
CREATE TABLE IF NOT EXISTS server_presets (
 server_id VARCHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
 name VARCHAR(40) NOT NULL, settings TEXT NOT NULL,
 PRIMARY KEY(server_id,name), FOREIGN KEY(server_id) REFERENCES server_keys(id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
CREATE TABLE IF NOT EXISTS server_bans (
 server_id VARCHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
 account_id CHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
 reason VARCHAR(160) NOT NULL, expires_at DATETIME(6) NOT NULL,
 PRIMARY KEY(server_id,account_id), FOREIGN KEY(server_id) REFERENCES server_keys(id) ON DELETE CASCADE,
 FOREIGN KEY(account_id) REFERENCES accounts(id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
CREATE TABLE IF NOT EXISTS server_commands (
 id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
 server_id VARCHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
 actor CHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
 target CHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
 reason VARCHAR(160) NOT NULL, status VARCHAR(12) NOT NULL DEFAULT 'pending',
 expires_at DATETIME(6) NOT NULL, created_at DATETIME(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
 FOREIGN KEY(server_id) REFERENCES server_keys(id) ON DELETE CASCADE,
 FOREIGN KEY(actor) REFERENCES accounts(id), INDEX command_queue(server_id,status,expires_at)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
CREATE TABLE IF NOT EXISTS server_map_votes (
 server_id VARCHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
 round_id VARCHAR(40) CHARACTER SET ascii NOT NULL,
 account_id CHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
 map_id CHAR(64) CHARACTER SET ascii NOT NULL,
 PRIMARY KEY(server_id,round_id,account_id),
 FOREIGN KEY(server_id) REFERENCES server_keys(id) ON DELETE CASCADE,
 FOREIGN KEY(account_id) REFERENCES accounts(id) ON DELETE CASCADE
) ENGINE=InnoDB;
INSERT IGNORE INTO schema_migrations(version) VALUES(6);
SQL;
