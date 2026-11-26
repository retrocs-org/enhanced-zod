package agent

import (
	"context"
	"encoding/json"
	"os/exec"
	"strings"
	"time"
)

func runtimeBuildID(ctx context.Context, path string, c Config) string {
	probe, cancel := context.WithTimeout(ctx, 3*time.Second)
	defer cancel()
	cmd := exec.CommandContext(probe, path, "--build-info")
	cmd.Dir = c.Data
	b, e := cmd.Output()
	if e != nil || len(b) > 8192 {
		return ""
	}
	var info struct {
		ID string `json:"build_id"`
	}
	if json.Unmarshal(b, &info) != nil || len(info.ID) > 160 {
		return ""
	}
	return info.ID
}
func runtimeLabel(r *runtimeRelease) string {
	if r == nil {
		return "None"
	}
	if r.BuildID != "" {
		return r.BuildID
	}
	version := strings.Split(r.Version, "\n")[0]
	if version == "" {
		version = "Engine"
	}
	if len(r.SHA256) >= 12 {
		return version + " / SHA-256 " + r.SHA256[:12]
	}
	return version
}
func (a *Agent) runtimeReport(status engineStatus) map[string]string {
	s, e := readRuntime(a.cfg)
	if e != nil {
		return map[string]string{"running": status.BuildID, "outcome": "Runtime metadata unreadable"}
	}
	running := status.BuildID
	if running == "" {
		running = status.Version
	}
	outcome := s.Outcome
	if outcome == "" {
		outcome = "No managed update recorded"
	}
	when := "Not recorded"
	if s.OutcomeAt > 0 {
		when = time.Unix(s.OutcomeAt, 0).UTC().Format(time.RFC3339)
	}
	return map[string]string{"running": running, "staged": runtimeLabel(s.Staged), "previous": runtimeLabel(s.Previous), "outcome": outcome, "outcome_at": when}
}
func (a *Agent) runtimeOutcome(outcome string) {
	unlock, lockError := lockRuntime(a.cfg)
	if lockError != nil {
		return
	}
	defer unlock()
	s, e := readRuntime(a.cfg)
	if e != nil {
		return
	}
	s.Outcome = outcome
	s.OutcomeAt = time.Now().Unix()
	saveRuntime(a.cfg, s)
}
