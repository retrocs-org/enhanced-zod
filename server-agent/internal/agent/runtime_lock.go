package agent

import (
	"errors"
	"os"
	"path/filepath"
	"time"
)

// Serialize read/modify/write operations across the service and local staging CLI.
func lockRuntime(c Config) (func(), error) {
	dir := filepath.Dir(runtimePath(c))
	if e := os.MkdirAll(dir, 0700); e != nil {
		return nil, e
	}
	lock := filepath.Join(dir, ".state-lock")
	deadline := time.Now().Add(2 * time.Second)
	for {
		if e := os.Mkdir(lock, 0700); e == nil {
			return func() { os.Remove(lock) }, nil
		} else if !os.IsExist(e) {
			return nil, e
		}
		if time.Now().After(deadline) {
			return nil, errors.New("another runtime change holds .state-lock; retry, or check for an interrupted staging process")
		}
		time.Sleep(20 * time.Millisecond)
	}
}
func (a *Agent) markRuntimeRolledBack() error {
	unlock, e := lockRuntime(a.cfg)
	if e != nil {
		return e
	}
	defer unlock()
	s, e := readRuntime(a.cfg)
	if e != nil {
		return e
	}
	s.Previous = nil
	s.Outcome = "Automatic rollback after startup failure"
	s.OutcomeAt = time.Now().Unix()
	return saveRuntime(a.cfg, s)
}
