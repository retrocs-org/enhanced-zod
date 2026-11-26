<?php
declare(strict_types=1);
if(PHP_SAPI!=='cli'){http_response_code(404);exit;}
return <<<'SQL'
CREATE TABLE IF NOT EXISTS server_owners (
 server_id VARCHAR(64) CHARACTER SET ascii COLLATE ascii_bin PRIMARY KEY,
 account_id CHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
 created_at DATETIME(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
 FOREIGN KEY(server_id) REFERENCES server_keys(id) ON DELETE CASCADE,
 FOREIGN KEY(account_id) REFERENCES accounts(id), INDEX owner_servers(account_id)
) ENGINE=InnoDB;
CREATE TABLE IF NOT EXISTS server_admins (
 server_id VARCHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
 account_id CHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
 created_at DATETIME(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
 PRIMARY KEY(server_id,account_id),
 FOREIGN KEY(server_id) REFERENCES server_owners(server_id) ON DELETE CASCADE,
 FOREIGN KEY(account_id) REFERENCES accounts(id) ON DELETE CASCADE
) ENGINE=InnoDB;
CREATE TABLE IF NOT EXISTS server_auth_requests (
 device_hash CHAR(64) CHARACTER SET ascii COLLATE ascii_bin PRIMARY KEY,
 user_code CHAR(12) CHARACTER SET ascii COLLATE ascii_bin NOT NULL UNIQUE,
 server_id VARCHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
 server_name VARCHAR(80) NOT NULL,
 key_hash CHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
 prior_key_hash CHAR(64) CHARACTER SET ascii COLLATE ascii_bin NULL,
 account_id CHAR(64) CHARACTER SET ascii COLLATE ascii_bin NULL,
 expires_at DATETIME(6) NOT NULL, last_poll DATETIME(6) NULL,
 FOREIGN KEY(account_id) REFERENCES accounts(id) ON DELETE CASCADE,
 INDEX auth_expiry(expires_at)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
ALTER TABLE moderation_log ADD COLUMN IF NOT EXISTS details VARCHAR(160) NOT NULL DEFAULT '';
INSERT IGNORE INTO schema_migrations(version) VALUES(5);
SQL;
