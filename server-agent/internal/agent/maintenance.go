package agent

import (
	"context"
	"crypto/sha256"
	"crypto/subtle"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"log"
	"net/http"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"time"
)

var restartEngine = errors.New("scheduled engine restart")

type runtimeRelease struct {
	Path    string `json:"path"`
	SHA256  string `json:"sha256"`
	Version string `json:"version"`
}
type runtimeState struct{ Current, Previous, Staged *runtimeRelease }

func runtimePath(c Config) string { return filepath.Join(c.State, "runtime", "state.json") }
func readRuntime(c Config) (runtimeState, error) {
	var s runtimeState
	b, e := os.ReadFile(runtimePath(c))
	if os.IsNotExist(e) {
		return s, nil
	}
	if e != nil {
		return s, e
	}
	e = json.Unmarshal(b, &s)
	return s, e
}
func saveRuntime(c Config, s runtimeState) error {
	p := runtimePath(c)
	if e := os.MkdirAll(filepath.Dir(p), 0700); e != nil {
		return e
	}
	b, e := json.MarshalIndent(s, "", "  ")
	if e != nil {
		return e
	}
	if e = os.WriteFile(p+".tmp", b, 0600); e != nil {
		return e
	}
	return os.Rename(p+".tmp", p)
}
func verifyRuntime(r *runtimeRelease) error {
	if r == nil {
		return errors.New("no staged runtime")
	}
	info, e := os.Lstat(r.Path)
	if e != nil || !info.Mode().IsRegular() {
		return errors.New("runtime missing or linked")
	}
	f, e := os.Open(r.Path)
	if e != nil {
		return e
	}
	defer f.Close()
	h := sha256.New()
	if _, e = io.Copy(h, f); e != nil {
		return e
	}
	if hex.EncodeToString(h.Sum(nil)) != r.SHA256 {
		return errors.New("runtime checksum changed")
	}
	return nil
}
func inspectRuntime(ctx context.Context, path string, c Config) (string, error) {
	check, cancel := context.WithTimeout(ctx, 5*time.Second)
	defer cancel()
	cmd := exec.CommandContext(check, path, "--version")
	cmd.Dir = c.Data
	b, e := cmd.Output()
	if e != nil || len(b) > 1024 {
		return "", errors.New("runtime version check failed")
	}
	version := strings.TrimSpace(string(b))
	if version == "" {
		return "", errors.New("empty runtime version")
	}
	checkMap, cancelMap := context.WithTimeout(ctx, 10*time.Second)
	defer cancelMap()
	cmd = exec.CommandContext(checkMap, path, "--map-info", c.Map)
	cmd.Dir = c.Data
	b, e = cmd.Output()
	var mapInfo struct{ Players int }
	if e != nil || len(b) > 16384 || json.Unmarshal(b, &mapInfo) != nil || mapInfo.Players < 2 {
		return "", errors.New("runtime could not read the configured multiplayer map")
	}
	return version, nil
}

// Copy into a private immutable-by-name cache only after verifying the full hash.
func cacheRuntime(source, path, expected string) error {
	info, e := os.Lstat(source)
	if e != nil || !info.Mode().IsRegular() || info.Size() > 256*1024*1024 {
		return errors.New("runtime must be a regular executable under 256 MB")
	}
	in, e := os.Open(source)
	if e != nil {
		return e
	}
	defer in.Close()
	out, e := os.CreateTemp(filepath.Dir(path), ".staging-*")
	if e != nil {
		return e
	}
	defer os.Remove(out.Name())
	if e = out.Chmod(0700); e != nil {
		out.Close()
		return e
	}
	h := sha256.New()
	_, e = io.Copy(io.MultiWriter(out, h), io.LimitReader(in, 256*1024*1024+1))
	if e == nil {
		e = out.Sync()
	}
	closed := out.Close()
	if e != nil {
		return e
	}
	if closed != nil {
		return closed
	}
	if hex.EncodeToString(h.Sum(nil)) != expected {
		return errors.New("runtime checksum does not match trusted release")
	}
	if _, e = os.Lstat(path); e == nil {
		return verifyRuntime(&runtimeRelease{Path: path, SHA256: expected})
	} else if !os.IsNotExist(e) {
		return e
	}
	return os.Rename(out.Name(), path)
}

// Staging is a local operator action. Cloud users cannot supply binaries or shell commands.
func StageRuntime(ctx context.Context, c Config, source, expected string) error {
	if len(expected) != 64 {
		return errors.New("--sha256 must contain the trusted release SHA-256")
	}
	if _, e := hex.DecodeString(expected); e != nil {
		return e
	}
	source, e := filepath.Abs(source)
	if e != nil {
		return e
	}
	info, e := os.Lstat(source)
	if e != nil || !info.Mode().IsRegular() {
		return errors.New("stage a regular executable file")
	}
	dir := filepath.Join(c.State, "runtime")
	if e = os.MkdirAll(dir, 0700); e != nil {
		return e
	}
	path := filepath.Join(dir, strings.ToLower(expected))
	path, e = filepath.Abs(path)
	if e != nil {
		return e
	}
	if e = cacheRuntime(source, path, strings.ToLower(expected)); e != nil {
		return e
	}
	r := &runtimeRelease{Path: path, SHA256: strings.ToLower(expected)}
	if e = verifyRuntime(r); e != nil {
		return e
	}
	r.Version, e = inspectRuntime(ctx, path, c)
	if e != nil {
		return e
	}
	state, e := readRuntime(c)
	if e != nil {
		return e
	}
	state.Staged = r
	return saveRuntime(c, state)
}
func RollbackRuntime(c Config) error {
	s, e := readRuntime(c)
	if e != nil {
		return e
	}
	if e = verifyRuntime(s.Previous); e != nil {
		return fmt.Errorf("rollback unavailable: %w", e)
	}
	s.Current, s.Previous = s.Previous, s.Current
	s.Staged = nil
	return saveRuntime(c, s)
}
func (a *Agent) activateRuntime(ctx context.Context, activate bool) error {
	s, e := readRuntime(a.cfg)
	if e != nil {
		return e
	}
	if activate && s.Staged != nil {
		if e = verifyRuntime(s.Staged); e != nil {
			return e
		}
		if _, e = inspectRuntime(ctx, s.Staged.Path, a.cfg); e != nil {
			return e
		}
		if s.Current == nil {
			f, e := os.Open(a.cfg.Engine)
			if e != nil {
				return e
			}
			h := sha256.New()
			_, e = io.Copy(h, f)
			f.Close()
			if e != nil {
				return e
			}
			checksum := hex.EncodeToString(h.Sum(nil))
			path, e := filepath.Abs(filepath.Join(a.cfg.State, "runtime", checksum))
			if e != nil {
				return e
			}
			if e = cacheRuntime(a.cfg.Engine, path, checksum); e != nil {
				return e
			}
			s.Current = &runtimeRelease{Path: path, SHA256: checksum}
		}
		s.Previous, s.Current, s.Staged = s.Current, s.Staged, nil
		if e = saveRuntime(a.cfg, s); e != nil {
			return e
		}
	}
	if s.Current != nil {
		if e = verifyRuntime(s.Current); e != nil {
			return e
		}
		a.cfg.Engine = s.Current.Path
		a.runtimeTrial = s.Previous != nil
	}
	return nil
}
func (a *Agent) Run(ctx context.Context) error {
	if e := a.activateRuntime(ctx, false); e != nil {
		return e
	}
	for {
		started := time.Now()
		e := a.runOnce(ctx)
		if a.runtimeTrial && e == nil && ctx.Err() == nil {
			e = errors.New("updated engine exited before readiness check")
		}
		if errors.Is(e, restartEngine) {
			if err := a.activateRuntime(ctx, true); err != nil {
				log.Printf("Staged update rejected; keeping current engine: %v", err)
			}
			continue
		}
		// A failed or unexpectedly short-lived updated process returns to the retained runtime once.
		if e != nil && time.Since(started) < 30*time.Second && ctx.Err() == nil {
			s, err := readRuntime(a.cfg)
			if err == nil && s.Previous != nil {
				if err = RollbackRuntime(a.cfg); err == nil {
					if err = a.activateRuntime(ctx, false); err == nil {
						a.runtimeTrial = false
						s, _ = readRuntime(a.cfg)
						s.Previous = nil
						if err = saveRuntime(a.cfg, s); err != nil {
							return err
						}
						log.Print("Updated engine failed startup; restored previous verified engine")
						return a.runOnce(ctx)
					}
				}
			}
		}
		return e
	}
}
func (a *Agent) Moderate(w http.ResponseWriter, r *http.Request) {
	if r.Method != "POST" {
		http.Error(w, "POST required", 405)
		return
	}
	var in struct {
		Secret, Actor, Target, Action, Reason string
		Minutes                               int
	}
	if json.NewDecoder(http.MaxBytesReader(w, r.Body, 2048)).Decode(&in) != nil || subtle.ConstantTimeCompare([]byte(in.Secret), []byte(a.secret)) != 1 {
		http.Error(w, "denied", 401)
		return
	}
	if e := a.request(r.Context(), "POST", "/api/server/moderate", map[string]any{"actor": in.Actor, "target": in.Target, "action": in.Action, "reason": in.Reason, "minutes": in.Minutes}, nil); e != nil {
		http.Error(w, "moderation unavailable", 503)
		return
	}
	w.WriteHeader(204)
}
