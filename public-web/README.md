# Self-contained PHP website

Copy this entire directory, including dotfiles and `_app/`, into the vhost
document root. No sibling repository directory or generated PHP wrapper is needed.
The same PHP entry point handles the website, network services and JSON API.

`_app/` contains internal PHP code and CLI administration tools. Its `.htaccess`
denies HTTP access; the packaged Apache configuration also denies that directory
explicitly. Internal PHP files refuse direct HTTP execution. Schema migrations
are guarded PHP files rather than downloadable SQL. Credentials remain outside
the document root, configured through environment variables.

See [configuration and database setup](../web-service/README.md). Local preview:

```sh
php -S 127.0.0.1:8088 -t public-web public-web/router.php
```

Production requires PHP 8.4+, PDO MySQL, mbstring, MariaDB and Apache/PHP-FPM.
Route non-file requests to `/index.php` and deny HTTP access to `_app/`.
