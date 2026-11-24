# Self-contained PHP website

Copy this entire directory, including dotfiles and `_app/`, into the vhost
document root. No sibling repository directory or generated PHP wrapper is needed.
The same PHP entry point handles the website, network services and JSON API.

`_app/` contains internal PHP code and CLI administration tools. Its `.htaccess`
denies HTTP access; the packaged Apache configuration also denies that directory
explicitly. Internal PHP files refuse direct HTTP execution. Schema migrations
are guarded PHP files rather than downloadable SQL. Database and website settings are in `config.inc.php`. Edit the installed copy;
refreshes preserve it. Environment variables override file settings. You can
keep the password in an external file using `ZOD_DB_PASSWORD_FILE` instead.

See [configuration and database setup](../web-service/README.md). Local preview:

```sh
php -S 127.0.0.1:8088 -t public-web public-web/router.php
```

Production requires PHP 8.4+, PDO MySQL, mbstring, MariaDB and Apache/PHP-FPM.
Route non-file requests to `/index.php` and deny HTTP access to `_app/`.

Configure `ZOD_DB_PASSWORD` in `/srv/www/vhosts/zodee.retrocs.org/config.inc.php`
with your application password, then run `php _app/bin/migrate.php` from the
vhost directory. The same configuration is used by website, API and CLI tools.
Do not commit your configured production file to Git.
