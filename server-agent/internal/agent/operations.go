package agent

import (
	"context"
	"crypto/sha256"
	"crypto/subtle"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"io"
	"net"
	"net/http"
	"os"
	"os/exec"
	"path/filepath"
	"time"
)

type engineStatus struct {
	PlayerSlots                *int `json:"player_slots"`
	Spectators                 *int `json:"spectators"`
	Bots                       *int `json:"bots"`
	Difficulty                 *int `json:"difficulty"`
	SpeedPercent               *int `json:"speed_percent"`
	Players, Started, Capacity int
	Map, Version, Round        string
	BuildID                    string `json:"build_id"`
	Maps                       []string
	Roster                     []map[string]string
	Intermission               bool
	VoteRemaining              float64 `json:"vote_remaining"`
	TickMS                     float64 `json:"tick_ms"`
	Uptime                     float64
	RoleAge                    float64 `json:"role_age"`
	NextMap                    string  `json:"next_map"`
	SyncChecks                 int64   `json:"sync_checks"`
	SyncMismatches             int64   `json:"sync_mismatches"`
	AppliedRevision            int64   `json:"applied_revision"`
}

func (a *Agent) readStatus() (engineStatus, error) {
	var status engineStatus
	c, e := net.DialTimeout("unix", filepath.Join(a.cfg.State, "status.sock"), time.Second)
	if e != nil {
		return status, e
	}
	defer c.Close()
	c.SetReadDeadline(time.Now().Add(time.Second))
	e = json.NewDecoder(io.LimitReader(c, 65536)).Decode(&status)
	return status, e
}
func (a *Agent) publishHealth(ctx context.Context) {
	status, e := a.readStatus()
	if e != nil {
		return
	}
	if a.catalog == nil {
		a.catalog = map[string]map[string]any{}
	}
	maps := []map[string]any{}
	seen := map[string]bool{}
	for _, path := range append([]string{status.Map}, status.Maps...) {
		if seen[path] || path == "" || len(maps) >= 64 {
			continue
		}
		seen[path] = true
		full := path
		if !filepath.IsAbs(full) {
			full = filepath.Join(a.cfg.Data, path)
		}
		info, e := os.Stat(full)
		if e != nil {
			continue
		}
		cache := a.catalog[path]
		stamp := fmt.Sprintf("%d:%d", info.ModTime().UnixNano(), info.Size())
		if cache == nil || cache["stamp"] != stamp {
			if info.Size() > 16*1024*1024 {
				continue
			}
			call, cancel := context.WithTimeout(ctx, 2*time.Second)
			cmd := exec.CommandContext(call, a.cfg.Engine, "--map-info", full)
			cmd.Dir = a.cfg.Data
			data, err := cmd.Output()
			cancel()
			if err != nil || len(data) > 16384 {
				continue
			}
			var preview struct {
				Width, Height int
				Forts, Zones  [][]int
			}
			if json.Unmarshal(data, &preview) != nil || preview.Width < 1 || preview.Height < 1 {
				continue
			}
			bytes, readError := os.ReadFile(full)
			if readError != nil || len(bytes) > 16*1024*1024 {
				continue
			}
			sum := sha256.Sum256(bytes)
			cache = map[string]any{"sha256": hex.EncodeToString(sum[:]), "map": path, "title": filepath.Base(path), "width": preview.Width, "height": preview.Height, "forts": preview.Forts, "zones": preview.Zones, "stamp": stamp}
			a.catalog[path] = cache
		}
		copy := map[string]any{}
		for key, value := range cache {
			if key != "stamp" {
				copy[key] = value
			}
		}
		maps = append(maps, copy)
	}
	outbox, _ := os.ReadDir(filepath.Join(a.cfg.State, "outbox"))
	pending := 0
	for _, file := range outbox {
		if filepath.Ext(file.Name()) == ".json" {
			pending++
		}
	}
	roster := status.Roster
	if roster == nil {
		roster = []map[string]string{}
	}
	snapshot := map[string]any{"sync_checks": status.SyncChecks, "sync_mismatches": status.SyncMismatches, "build_id": status.BuildID, "engine_version": status.Version, "runtime": a.runtimeReport(status), "maps": maps, "roster": roster, "round": status.Round, "intermission": status.Intermission, "vote_remaining": status.VoteRemaining, "tick_ms": status.TickMS, "uptime": status.Uptime, "role_age": status.RoleAge, "next_map": status.NextMap, "outbox": pending, "cloud_ok": time.Now().Unix()-a.lastRoleSuccess.Load() < 15, "applied_revision": status.AppliedRevision}
	// Keep telemetry below the portal's normal request bound.
	for {
		data, _ := json.Marshal(snapshot)
		if len(data) < 60000 || len(maps) == 0 {
			break
		}
		maps = maps[:len(maps)-1]
		snapshot["maps"] = maps
	}
	if e = a.request(ctx, "POST", "/api/server/health", map[string]any{"snapshot": snapshot}, nil); e != nil {
		a.lastRoleSuccess.Store(0)
	}
}
func (a *Agent) Ack(w http.ResponseWriter, r *http.Request) {
	if r.Method != "POST" {
		http.Error(w, "POST required", 405)
		return
	}
	var in struct {
		Secret  string
		Results []struct {
			ID      string `json:"id"`
			Applied bool   `json:"applied"`
		} `json:"results"`
	}
	if json.NewDecoder(http.MaxBytesReader(w, r.Body, 8192)).Decode(&in) != nil || subtle.ConstantTimeCompare([]byte(in.Secret), []byte(a.secret)) != 1 || len(in.Results) > 32 {
		http.Error(w, "denied", 401)
		return
	}
	if e := a.request(r.Context(), "POST", "/api/server/control/ack", map[string]any{"results": in.Results}, nil); e != nil {
		http.Error(w, "acknowledgement unavailable", 503)
		return
	}
	w.WriteHeader(204)
}
func validAccount(id string) bool {
	if len(id) != 64 {
		return false
	}
	_, e := hex.DecodeString(id)
	return e == nil
}
