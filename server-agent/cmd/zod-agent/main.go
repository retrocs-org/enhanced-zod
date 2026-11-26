package main

import (
	"context"
	"flag"
	"log"
	"os"
	"os/signal"
	"strings"
	"syscall"
	"zod-server-agent/internal/agent"
)

func main() {
	var c agent.Config
	var keyfile, stageEngine, checksum string
	var rollback bool
	var login, loginOnly bool
	flag.StringVar(&c.Mode, "mode", "private", "public or private")
	flag.StringVar(&c.Engine, "engine", "/app/build/zod-enhanced", "engine executable")
	flag.StringVar(&c.Data, "data", "/app", "game data directory")
	flag.StringVar(&c.Map, "map", "maps/p02_bb_orig01.map", "map path relative to data")
	flag.StringVar(&c.State, "state", "/var/lib/zod", "private writable state directory")
	flag.StringVar(&c.Hub, "hub", "", "Zod web service HTTPS URL")
	flag.StringVar(&c.ID, "id", "", "unique public server ID bound to its cloud owner")
	flag.StringVar(&keyfile, "key-file", "", "file containing this server's public-service key")
	flag.StringVar(&c.Host, "public-ip", "", "advertise IP (default: address observed by web service)")
	flag.StringVar(&c.Name, "name", "Zod dedicated server", "public display name")
	flag.IntVar(&c.Port, "port", 2137, "game TCP port")
	flag.IntVar(&c.Capacity, "capacity", 2, "map fort slots, 2-8")
	flag.BoolVar(&login, "login", false, "authorize this public host with its cloud owner in a browser")
	flag.BoolVar(&loginOnly, "login-only", false, "authorize this public host and exit without starting the engine")
	flag.StringVar(&stageEngine, "stage-engine", "", "stage a local dedicated engine for the next scheduled restart")
	flag.StringVar(&checksum, "sha256", "", "trusted SHA-256 of the staged engine")
	flag.BoolVar(&rollback, "rollback-engine", false, "select the retained previous engine, then exit; stop the agent first")
	flag.Parse()
	c.Extra = flag.Args()
	if keyfile != "" {
		b, e := os.ReadFile(keyfile)
		if e != nil {
			log.Fatal(e)
		}
		c.Key = strings.TrimSpace(string(b))
	}
	ctx, stop := signal.NotifyContext(context.Background(), os.Interrupt, syscall.SIGTERM)
	defer stop()
	if stageEngine != "" {
		if e := agent.StageRuntime(ctx, c, stageEngine, checksum); e != nil {
			log.Fatal(e)
		}
		log.Print("Engine staged and verified; schedule restart after this round.")
		return
	}
	if rollback {
		if e := agent.RollbackRuntime(c); e != nil {
			log.Fatal(e)
		}
		log.Print("Previous engine selected; start the agent normally.")
		return
	}
	if e := agent.LoadIdentity(&c); e != nil {
		log.Fatal(e)
	}
	if login || loginOnly {
		if e := agent.Authorize(ctx, &c, os.Stdout); e != nil {
			log.Fatal(e)
		}
	}
	if loginOnly {
		return
	}
	a, e := agent.New(c)
	if e != nil {
		log.Fatal(e)
	}
	if e = a.Run(ctx); e != nil {
		log.Fatal(e)
	}
}
