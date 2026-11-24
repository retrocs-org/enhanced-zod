<?php
declare(strict_types=1);
if (PHP_SAPI !== 'cli') { http_response_code(404); exit; }
return <<<'ZODEE_SCHEMA'
-- MariaDB 10.6+, InnoDB, UTC timestamps. Run using bin/migrate.php.
CREATE TABLE IF NOT EXISTS schema_migrations (
 version INT PRIMARY KEY, applied_at DATETIME(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6)
) ENGINE=InnoDB;
CREATE TABLE IF NOT EXISTS accounts (
 id CHAR(64) CHARACTER SET ascii COLLATE ascii_bin PRIMARY KEY,
 username VARCHAR(24) CHARACTER SET ascii NOT NULL,
 username_key VARCHAR(24) CHARACTER SET ascii COLLATE ascii_bin NOT NULL UNIQUE,
 password_hash VARCHAR(255) CHARACTER SET ascii NOT NULL,
 password_scheme VARCHAR(24) CHARACTER SET ascii NOT NULL DEFAULT 'argon2id',
 legacy_salt CHAR(64) CHARACTER SET ascii NULL,
 created_at DATETIME(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6)
) ENGINE=InnoDB;
CREATE TABLE IF NOT EXISTS sessions (
 token_hash CHAR(64) CHARACTER SET ascii COLLATE ascii_bin PRIMARY KEY,
 account_id CHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
 expires_at DATETIME(6) NOT NULL,
 FOREIGN KEY (account_id) REFERENCES accounts(id) ON DELETE CASCADE,
 INDEX session_expiry (expires_at)
) ENGINE=InnoDB;
CREATE TABLE IF NOT EXISTS server_keys (
 id VARCHAR(64) CHARACTER SET ascii COLLATE ascii_bin PRIMARY KEY,
 name VARCHAR(80) NOT NULL,
 key_hash CHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL UNIQUE,
 enabled BOOLEAN NOT NULL DEFAULT TRUE,
 created_at DATETIME(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
CREATE TABLE IF NOT EXISTS servers (
 id VARCHAR(64) CHARACTER SET ascii COLLATE ascii_bin PRIMARY KEY,
 name VARCHAR(80) NOT NULL, host VARCHAR(45) CHARACTER SET ascii NOT NULL,
 port SMALLINT UNSIGNED NOT NULL, map_name VARCHAR(240) NOT NULL DEFAULT '',
 players TINYINT UNSIGNED NOT NULL, capacity TINYINT UNSIGNED NOT NULL,
 started BOOLEAN NOT NULL DEFAULT FALSE, version VARCHAR(80) NOT NULL DEFAULT '',
 last_seen DATETIME(6) NOT NULL,
 FOREIGN KEY (id) REFERENCES server_keys(id) ON DELETE CASCADE,
 INDEX live_servers (last_seen)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
CREATE TABLE IF NOT EXISTS join_tickets (
 token_hash CHAR(64) CHARACTER SET ascii COLLATE ascii_bin PRIMARY KEY,
 account_id CHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
 server_id VARCHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
 expires_at DATETIME(6) NOT NULL,
 FOREIGN KEY (account_id) REFERENCES accounts(id) ON DELETE CASCADE,
 FOREIGN KEY (server_id) REFERENCES server_keys(id) ON DELETE CASCADE,
 INDEX ticket_expiry (expires_at), INDEX account_tickets (account_id)
) ENGINE=InnoDB;
CREATE TABLE IF NOT EXISTS connection_grants (
 token_hash CHAR(64) CHARACTER SET ascii COLLATE ascii_bin PRIMARY KEY,
 account_id CHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
 server_id VARCHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
 expires_at DATETIME(6) NOT NULL,
 FOREIGN KEY (account_id) REFERENCES accounts(id) ON DELETE CASCADE,
 FOREIGN KEY (server_id) REFERENCES server_keys(id) ON DELETE CASCADE,
 INDEX grant_expiry (expires_at)
) ENGINE=InnoDB;
CREATE TABLE IF NOT EXISTS matches (
 id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
 server_id VARCHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
 match_id CHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
 map_name VARCHAR(240) NOT NULL, duration INT UNSIGNED NOT NULL,
 payload_hash CHAR(64) CHARACTER SET ascii NOT NULL,
 finished_at DATETIME(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
 UNIQUE KEY report_identity (server_id,match_id), INDEX recent_matches (finished_at)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
CREATE TABLE IF NOT EXISTS match_players (
 match_pk BIGINT UNSIGNED NOT NULL,
 account_id CHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
 won BOOLEAN NOT NULL, kills INT UNSIGNED NOT NULL, lost INT UNSIGNED NOT NULL, captures INT UNSIGNED NOT NULL,
 PRIMARY KEY (match_pk,account_id),
 FOREIGN KEY (match_pk) REFERENCES matches(id) ON DELETE CASCADE,
 FOREIGN KEY (account_id) REFERENCES accounts(id),
 INDEX player_matches (account_id,match_pk)
) ENGINE=InnoDB;
CREATE TABLE IF NOT EXISTS rate_limits (
 bucket CHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
 window_id BIGINT UNSIGNED NOT NULL, attempts INT UNSIGNED NOT NULL,
 PRIMARY KEY (bucket,window_id), INDEX rate_expiry (window_id)
) ENGINE=InnoDB;
INSERT IGNORE INTO schema_migrations (version) VALUES (1);

ZODEE_SCHEMA;
