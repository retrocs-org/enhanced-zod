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
	var keyfile string
	flag.StringVar(&c.Mode, "mode", "private", "public or private")
	flag.StringVar(&c.Engine, "engine", "/app/build/zod-ee", "engine executable")
	flag.StringVar(&c.Data, "data", "/app", "game data directory")
	flag.StringVar(&c.Map, "map", "maps/p02_bb_orig01.map", "map path relative to data")
	flag.StringVar(&c.State, "state", "/var/lib/zod", "private writable state directory")
	flag.StringVar(&c.Hub, "hub", "", "Zod web service HTTPS URL")
	flag.StringVar(&c.ID, "id", "", "approved public server ID")
	flag.StringVar(&keyfile, "key-file", "", "file containing this server's public-service key")
	flag.StringVar(&c.Host, "public-ip", "", "advertise IP (default: address observed by web service)")
	flag.StringVar(&c.Name, "name", "Zod dedicated server", "public display name")
	flag.IntVar(&c.Port, "port", 2137, "game TCP port")
	flag.IntVar(&c.Capacity, "capacity", 2, "map fort slots, 2-8")
	flag.Parse()
	c.Extra = flag.Args()
	if keyfile != "" {
		b, e := os.ReadFile(keyfile)
		if e != nil {
			log.Fatal(e)
		}
		c.Key = strings.TrimSpace(string(b))
	}
	a, e := agent.New(c)
	if e != nil {
		log.Fatal(e)
	}
	ctx, stop := signal.NotifyContext(context.Background(), os.Interrupt, syscall.SIGTERM)
	defer stop()
	if e = a.Run(ctx); e != nil {
		log.Fatal(e)
	}
}
