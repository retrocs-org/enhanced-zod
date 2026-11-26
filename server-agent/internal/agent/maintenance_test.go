package agent

import (
	"context"
	"crypto/sha256"
	"encoding/hex"
	"os"
	"path/filepath"
	"runtime"
	"strings"
	"testing"
)

func TestStagedRuntimeVerifiedActivationAndRollback(t *testing.T) {
	if runtime.GOOS == "windows" {
		t.Skip("POSIX executable fixture")
	}
	dir := t.TempDir()
	source := filepath.Join(dir, "engine")
	body := []byte("#!/bin/sh\ncase \"$1\" in\n--version) echo 'zod-enhanced test';;\n--map-info) echo '{\"players\":2}';;\nesac\n")
	if e := os.WriteFile(source, body, 0700); e != nil {
		t.Fatal(e)
	}
	sum := sha256.Sum256(body)
	hash := hex.EncodeToString(sum[:])
	c := Config{Engine: source, State: filepath.Join(dir, "state"), Data: dir, Map: "map.map"}
	if StageRuntime(context.Background(), c, source, strings.Repeat("0", 64)) == nil {
		t.Fatal("bad checksum accepted")
	}
	// Bad staging leaves no selected runtime.
	s, _ := readRuntime(c)
	if s.Staged != nil {
		t.Fatal("failed stage changed active state")
	}
	if e := StageRuntime(context.Background(), c, source, hash); e != nil {
		t.Fatal(e)
	}
	a := &Agent{cfg: c}
	if e := a.activateRuntime(context.Background(), true); e != nil {
		t.Fatal(e)
	}
	s, _ = readRuntime(c)
	if s.Previous == nil || s.Current == nil || s.Staged != nil || a.cfg.Engine == source {
		t.Fatal("activation did not retain previous runtime")
	}
	if e := RollbackRuntime(c); e != nil {
		t.Fatal(e)
	}
	s, _ = readRuntime(c)
	if s.Current.SHA256 != hash {
		t.Fatal("rollback selected wrong runtime")
	}
	if e := StageRuntime(context.Background(), c, source, hash); e != nil {
		t.Fatal(e)
	}
	s, _ = readRuntime(c)
	if e := os.WriteFile(s.Staged.Path, []byte("tampered"), 0700); e != nil {
		t.Fatal(e)
	}
	if a.activateRuntime(context.Background(), true) == nil {
		t.Fatal("tampered staged binary activated")
	}
}

func TestFailedUpdatedProcessRollsBack(t *testing.T) {
	if runtime.GOOS == "windows" {
		t.Skip("POSIX executable fixture")
	}
	dir := t.TempDir()
	old := filepath.Join(dir, "old")
	next := filepath.Join(dir, "next")
	prefix := "#!/bin/sh\ncase \"$1\" in\n--version) echo test;exit 0;;\n--map-info) echo '{\"players\":2}';exit 0;;\nesac\n"
	os.WriteFile(old, []byte(prefix+"exit 0\n"), 0700)
	body := []byte(prefix + "exit 1\n")
	os.WriteFile(next, body, 0700)
	sum := sha256.Sum256(body)
	c := Config{Mode: "private", Engine: old, State: filepath.Join(dir, "state"), Data: dir, Map: "one.map", Port: 2137}
	if e := StageRuntime(context.Background(), c, next, hex.EncodeToString(sum[:])); e != nil {
		t.Fatal(e)
	}
	a, e := New(c)
	if e != nil {
		t.Fatal(e)
	}
	if e = a.activateRuntime(context.Background(), true); e != nil {
		t.Fatal(e)
	}
	if e = a.Run(context.Background()); e != nil {
		t.Fatal(e)
	}
	s, _ := readRuntime(c)
	if s.Current.SHA256 == hex.EncodeToString(sum[:]) {
		t.Fatal("failed updated process retained")
	}
}

func TestRuntimeStatusOmitsPrivatePaths(t *testing.T) {
	c := Config{State: t.TempDir()}
	s := runtimeState{Staged: &runtimeRelease{Path: "/secret/runtime", BuildID: "next-build"}, Previous: &runtimeRelease{Path: "/secret/previous", Version: "old", SHA256: strings.Repeat("a", 64)}, Outcome: "Staged", OutcomeAt: 1700000000}
	if e := saveRuntime(c, s); e != nil {
		t.Fatal(e)
	}
	a := &Agent{cfg: c}
	report := a.runtimeReport(engineStatus{BuildID: "running-build"})
	if report["running"] != "running-build" || report["staged"] != "next-build" || strings.Contains(report["previous"], "/secret") {
		t.Fatal("incorrect or private status", report)
	}
	a.runtimeOutcome("Running; readiness verified")
	current, _ := readRuntime(c)
	if current.Staged == nil || current.Outcome != "Running; readiness verified" {
		t.Fatal("outcome lost runtime metadata")
	}
}
