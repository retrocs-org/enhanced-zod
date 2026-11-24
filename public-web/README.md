# Public PHP website

This directory is the website document root for zod-ee. It serves the public
pages, Network Services dashboard, API Browser, accounts, statistics, server
listings and JSON API through `index.php`.

The private PHP application is in the sibling `../web-service/php/` directory.
Deploy both directories while exposing only this directory over HTTP. See
[deployment and configuration](../web-service/README.md).

Local development, after configuring MariaDB and running migrations:

```sh
php -S 127.0.0.1:8088 -t public-web public-web/router.php
```

Production requires PHP 8.4+, PDO MySQL, mbstring and an Apache/PHP-FPM front
controller. No Node.js or Go server is required for the website.
