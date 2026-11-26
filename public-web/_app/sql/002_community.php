<?php
declare(strict_types=1);
if (PHP_SAPI !== 'cli') { http_response_code(404); exit; }
return <<<'SQL'
CREATE TABLE IF NOT EXISTS account_preferences (
 account_id CHAR(64) CHARACTER SET ascii COLLATE ascii_bin PRIMARY KEY,
 visibility VARCHAR(10) NOT NULL DEFAULT 'public',
 allow_requests BOOLEAN NOT NULL DEFAULT FALSE, show_online BOOLEAN NOT NULL DEFAULT FALSE,
 last_active DATETIME(6) NULL,
 FOREIGN KEY(account_id) REFERENCES accounts(id) ON DELETE CASCADE
) ENGINE=InnoDB;
CREATE TABLE IF NOT EXISTS friendships (
 sender CHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
 recipient CHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
 accepted BOOLEAN NOT NULL DEFAULT FALSE, created_at DATETIME(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
 PRIMARY KEY(sender,recipient), FOREIGN KEY(sender) REFERENCES accounts(id) ON DELETE CASCADE,
 FOREIGN KEY(recipient) REFERENCES accounts(id) ON DELETE CASCADE
) ENGINE=InnoDB;
CREATE TABLE IF NOT EXISTS account_blocks (
 owner CHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL, target CHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
 PRIMARY KEY(owner,target), FOREIGN KEY(owner) REFERENCES accounts(id) ON DELETE CASCADE, FOREIGN KEY(target) REFERENCES accounts(id) ON DELETE CASCADE
) ENGINE=InnoDB;
CREATE TABLE IF NOT EXISTS server_invitations (
 id CHAR(64) CHARACTER SET ascii COLLATE ascii_bin PRIMARY KEY,
 sender CHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL, recipient CHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
 server_id VARCHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL, expires_at DATETIME(6) NOT NULL,
 FOREIGN KEY(sender) REFERENCES accounts(id) ON DELETE CASCADE, FOREIGN KEY(recipient) REFERENCES accounts(id) ON DELETE CASCADE,
 FOREIGN KEY(server_id) REFERENCES server_keys(id) ON DELETE CASCADE, INDEX invitation_recipient(recipient,expires_at)
) ENGINE=InnoDB;
CREATE TABLE IF NOT EXISTS recovery_codes (
 code_hash CHAR(64) CHARACTER SET ascii COLLATE ascii_bin PRIMARY KEY, account_id CHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
 FOREIGN KEY(account_id) REFERENCES accounts(id) ON DELETE CASCADE
) ENGINE=InnoDB;
CREATE TABLE IF NOT EXISTS community_projects (
 id CHAR(64) CHARACTER SET ascii COLLATE ascii_bin PRIMARY KEY, owner CHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
 title VARCHAR(80) NOT NULL, description VARCHAR(2000) NOT NULL, kind VARCHAR(10) NOT NULL,
 FOREIGN KEY(owner) REFERENCES accounts(id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
CREATE TABLE IF NOT EXISTS community_versions (
 id CHAR(64) CHARACTER SET ascii COLLATE ascii_bin PRIMARY KEY, project_id CHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
 version VARCHAR(32) NOT NULL, engine_min VARCHAR(32) NOT NULL, notes VARCHAR(1000) NOT NULL,
 filename VARCHAR(100) NOT NULL, sha256 CHAR(64) NOT NULL, manifest TEXT NOT NULL,
 archive MEDIUMBLOB NOT NULL, screenshot MEDIUMBLOB NOT NULL, screenshot_type VARCHAR(20) NOT NULL,
 approved BOOLEAN NOT NULL DEFAULT FALSE, created_at DATETIME(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
 UNIQUE KEY project_version(project_id,version), FOREIGN KEY(project_id) REFERENCES community_projects(id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
CREATE TABLE IF NOT EXISTS server_metadata (
 server_id VARCHAR(64) CHARACTER SET ascii COLLATE ascii_bin PRIMARY KEY,
 region VARCHAR(32) NOT NULL DEFAULT 'Unspecified', game_mode VARCHAR(32) NOT NULL DEFAULT 'Skirmish',
 FOREIGN KEY(server_id) REFERENCES server_keys(id) ON DELETE CASCADE
) ENGINE=InnoDB;
INSERT IGNORE INTO schema_migrations(version) VALUES(2);
SQL;
