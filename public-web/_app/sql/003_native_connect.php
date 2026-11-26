<?php
declare(strict_types=1);
if(PHP_SAPI!=='cli'){http_response_code(404);exit;}
return <<<'SQL'
CREATE TABLE IF NOT EXISTS native_connections (
 device_hash CHAR(64) CHARACTER SET ascii COLLATE ascii_bin PRIMARY KEY,
 user_code CHAR(12) CHARACTER SET ascii COLLATE ascii_bin NOT NULL UNIQUE,
 server_id VARCHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
 account_id CHAR(64) CHARACTER SET ascii COLLATE ascii_bin NULL,
 expires_at DATETIME(6) NOT NULL, last_poll DATETIME(6) NULL,
 FOREIGN KEY(server_id) REFERENCES server_keys(id) ON DELETE CASCADE,
 FOREIGN KEY(account_id) REFERENCES accounts(id) ON DELETE CASCADE,
 INDEX native_expiry(expires_at)
) ENGINE=InnoDB;
INSERT IGNORE INTO schema_migrations(version) VALUES(3);
SQL;
