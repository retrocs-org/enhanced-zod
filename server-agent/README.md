# Dedicated-server agent

This Go client runs a dedicated game server and communicates with the PHP
network API for public server listings, join tickets and match reporting.
It is required for managed hosting; it is not a website backend.

The complete website and API live in `../public-web/`. See
[network services and hosting](../docs/NETWORK-SERVICES.md) for setup.

From the repository root:

```sh
go -C server-agent build -o ../build-web/zod-agent ./cmd/zod-agent
go -C server-agent test -race ./...
```
