<?php
declare(strict_types=1);
// Edit this file on the webserver. Refreshes preserve the installed copy.
// Never commit a real password. Environment variables override these settings.
if (!defined('ZODEE_INTERNAL') && PHP_SAPI !== 'cli') { http_response_code(404); exit; }
return [
    // Comma-separated existing account usernames allowed to moderate. Empty disables admin access.
    'ZOD_ADMIN_USERS' => '',
    // MariaDB: database host and the application account (not the root account).
    'ZOD_DB_HOST' => '172.16.1.121',
    'ZOD_DB_PORT' => '3306',
    'ZOD_DB_NAME' => 'enhanced-zod',
    'ZOD_DB_USER' => 'enhanced-zod',
    'ZOD_DB_PASSWORD' => '', // Set your generated application password here.
    'ZOD_DB_PASSWORD_FILE' => '', // Optional alternative: /etc/zodee/db-password.
    'ZOD_DB_SOCKET' => '', // Leave empty for a remote database.

    // Website and integrated network/API services.
    'ZOD_PUBLIC_URL' => 'https://enhanced-zod.retrocs.org',
    'ZOD_CLEAN_URLS' => '0', // Set to '1' only when Apache routes all URLs to index.php.
    'ZOD_DEV_HTTP' => '0', // Set to '1' only for local HTTP development.
    'ZOD_TRUSTED_PROXIES' => '', // Comma-separated trusted proxy IPs/CIDRs.
    'ZOD_CREATION_QUOTA_MB' => '128', // Per-author creation storage, in MiB.
    'ZOD_CONQUEST_SNAPSHOT' => '', // Optional absolute path to public season snapshot JSON.
    'ZOD_DOWNLOADS_FILE' => '', // Optional absolute path to release-download JSON.
];
