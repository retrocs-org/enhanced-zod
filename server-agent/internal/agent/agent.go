package agent

import (
	"bytes"
	"context"
	"crypto/rand"
	"crypto/subtle"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"log"
	"net"
	"net/http"
	"net/url"
	"os"
	"os/exec"
	"path/filepath"
	"strconv"
	"strings"
	"time"
)

type Config struct {
	Mode, Engine, Data, Map, State, Hub, ID, Key, Host, Name string
	Port, Capacity                                           int
	Extra                                                    []string
}
type Agent struct {
	cfg    Config
	client *http.Client
	secret string
}

func New(c Config) (*Agent, error) {
	if c.Mode != "public" && c.Mode != "private" {
		return nil, errors.New("mode must be public or private")
	}
	if c.Port < 1024 || c.Port > 65535 {
		return nil, errors.New("invalid game port")
	}
	if c.Mode == "public" {
		u, e := url.Parse(c.Hub)
		if e != nil || u.Host == "" || u.User != nil || u.RawQuery != "" || u.Fragment != "" {
			return nil, errors.New("invalid hub URL")
		}
		if u.Scheme != "https" && !(u.Scheme == "http" && net.ParseIP(u.Hostname()).IsLoopback()) {
			return nil, errors.New("hub requires HTTPS; HTTP is allowed only on loopback for local tests")
		}
		if c.ID == "" || len(c.Key) < 64 {
			return nil, errors.New("public mode requires a server ID and credential; run with --login to authorize its cloud owner")
		}
		if c.Host != "" && (net.ParseIP(c.Host) == nil || net.ParseIP(c.Host).To4() == nil) {
			return nil, errors.New("public host must be an IP address")
		}
	}
	b := make([]byte, 32)
	if _, e := rand.Read(b); e != nil {
		return nil, e
	}
	return &Agent{c, &http.Client{Timeout: 5 * time.Second, CheckRedirect: func(*http.Request, []*http.Request) error { return http.ErrUseLastResponse }}, hex.EncodeToString(b)}, nil
}

type hubStatusError struct{ Code int }

func (e *hubStatusError) Error() string { return fmt.Sprintf("hub returned HTTP %d", e.Code) }

func (a *Agent) request(ctx context.Context, method, path string, in, out any) error {
	var b bytes.Buffer
	if in != nil {
		if e := json.NewEncoder(&b).Encode(in); e != nil {
			return e
		}
	}
	r, e := http.NewRequestWithContext(ctx, method, strings.TrimRight(a.cfg.Hub, "/")+path, &b)
	if e != nil {
		return e
	}
	r.Header.Set("Content-Type", "application/json")
	r.Header.Set("Authorization", "Bearer "+a.cfg.Key)
	resp, e := a.client.Do(r)
	if e != nil {
		return e
	}
	defer resp.Body.Close()
	if resp.StatusCode < 200 || resp.StatusCode >= 300 {
		return &hubStatusError{resp.StatusCode}
	}
	if out != nil {
		return json.NewDecoder(io.LimitReader(resp.Body, 65536)).Decode(out)
	}
	return nil
}
func (a *Agent) Verify(w http.ResponseWriter, r *http.Request) {
	if r.Method != "POST" {
		http.Error(w, "POST required", 405)
		return
	}
	var in struct{ Secret, Ticket string }
	if json.NewDecoder(http.MaxBytesReader(w, r.Body, 1024)).Decode(&in) != nil || subtle.ConstantTimeCompare([]byte(in.Secret), []byte(a.secret)) != 1 {
		http.Error(w, "denied", 401)
		return
	}
	var claim struct{ Account, Username, Grant string }
	if e := a.request(r.Context(), "POST", "/api/tickets/redeem", map[string]string{"Ticket": in.Ticket}, &claim); e != nil {
		http.Error(w, "Cloud login failed. Get a new ticket or retry when the service is available.", 401)
		return
	}
	if len(claim.Account) != 64 || len(claim.Grant) != 64 || strings.ContainsAny(claim.Username, "\r\n") {
		http.Error(w, "invalid claim", 502)
		return
	}
	w.Header().Set("Content-Type", "text/plain")
	fmt.Fprintf(w, "%s\n%s\n%s", claim.Account, claim.Username, claim.Grant)
}
func (a *Agent) status() (int, bool, string, string, error) {
	c, e := net.DialTimeout("unix", filepath.Join(a.cfg.State, "status.sock"), time.Second)
	if e != nil {
		return 0, false, "", "", e
	}
	defer c.Close()
	c.SetReadDeadline(time.Now().Add(time.Second))
	var v struct {
		Players, Started, Capacity int
		Map, Version               string
	}
	e = json.NewDecoder(io.LimitReader(c, 65536)).Decode(&v)
	if v.Capacity > 0 {
		a.cfg.Capacity = v.Capacity
	}
	return v.Players, v.Started != 0, v.Map, v.Version, e
}
func (a *Agent) publish(ctx context.Context) {
	n, started, m, version, e := a.status()
	if e != nil {
		return
	}
	if e = a.request(ctx, "POST", "/api/servers/heartbeat", map[string]any{"name": a.cfg.Name, "host": a.cfg.Host, "port": a.cfg.Port, "map": m, "players": n, "capacity": a.cfg.Capacity, "started": started, "version": version}, nil); e != nil {
		log.Printf("public listing: %v", e)
	}
	entries, e := os.ReadDir(filepath.Join(a.cfg.State, "outbox"))
	if e != nil {
		return
	}
	for _, entry := range entries {
		if entry.IsDir() || !strings.HasSuffix(entry.Name(), ".json") {
			continue
		}
		path := filepath.Join(a.cfg.State, "outbox", entry.Name())
		info, e := entry.Info()
		if e != nil || info.Size() > 65536 {
			continue
		}
		b, e := os.ReadFile(path)
		if e != nil {
			continue
		}
		var result map[string]any
		if json.Unmarshal(b, &result) != nil {
			log.Printf("invalid result retained in outbox: %s", entry.Name())
			continue
		}
		if e = a.request(ctx, "POST", "/api/results", result, nil); e == nil {
			os.Remove(path)
		} else {
			log.Printf("result pending: %v", e)
		}
	}
}
func (a *Agent) Run(ctx context.Context) error {
	if a.cfg.Mode == "public" {
		var identity struct{ ID, Owner string }
		if e := a.request(ctx, "GET", "/api/server/identity", nil, &identity); e != nil {
			return fmt.Errorf("public host needs owner authorization (--login), or is disabled by a site operator: %w", e)
		}
		if identity.ID != a.cfg.ID || len(identity.Owner) != 64 {
			return errors.New("public server has no verified cloud owner; run --login")
		}
	}
	var e error
	a.cfg.State, e = filepath.Abs(a.cfg.State)
	if e != nil {
		return e
	}
	if e = os.MkdirAll(filepath.Join(a.cfg.State, "outbox"), 0700); e != nil {
		return e
	}
	os.Remove(filepath.Join(a.cfg.State, "status.sock"))
	listener, e := net.Listen("tcp", "127.0.0.1:0")
	if e != nil {
		return e
	}
	defer listener.Close()
	mux := http.NewServeMux()
	mux.HandleFunc("/verify", a.Verify)
	mux.HandleFunc("/roles", a.Roles)
	srv := &http.Server{Handler: mux, ReadHeaderTimeout: 2 * time.Second, ReadTimeout: 3 * time.Second, WriteTimeout: 7 * time.Second, MaxHeaderBytes: 4096}
	defer srv.Close()
	go srv.Serve(listener)
	args := []string{"-d", "-m", a.cfg.Map}
	args = append(args, a.cfg.Extra...)
	cmd := exec.Command(a.cfg.Engine, args...)
	cmd.Dir = a.cfg.Data
	cmd.Stdout = os.Stdout
	cmd.Stderr = os.Stderr
	env := []string{}
	for _, v := range os.Environ() {
		key, _, _ := strings.Cut(v, "=")
		if !strings.HasPrefix(key, "ZOD_CLOUD_") && key != "ZOD_REQUIRE_CLOUD" && key != "ZOD_STATUS_SOCK" && key != "ZOD_PORT" && key != "ZOD_EMPTY_EXIT_SECS" && key != "ZOD_PERSISTENT_SERVER" {
			env = append(env, v)
		}
	}
	required := "0"
	if a.cfg.Mode == "public" {
		required = "1"
	}
	cmd.Env = append(env, "ZOD_PORT="+strconv.Itoa(a.cfg.Port), "ZOD_EMPTY_EXIT_SECS=0", "ZOD_PERSISTENT_SERVER=1", "ZOD_REQUIRE_CLOUD="+required, "ZOD_STATUS_SOCK="+filepath.Join(a.cfg.State, "status.sock"), "ZOD_CLOUD_AGENT_PORT="+strconv.Itoa(listener.Addr().(*net.TCPAddr).Port), "ZOD_CLOUD_AGENT_SECRET="+a.secret, "ZOD_CLOUD_OUTBOX="+filepath.Join(a.cfg.State, "outbox"))
	if e = cmd.Start(); e != nil {
		return e
	}
	done := make(chan error, 1)
	go func() { done <- cmd.Wait() }()
	ticker := time.NewTicker(20 * time.Second)
	defer ticker.Stop()
	initial := time.NewTimer(time.Second)
	defer initial.Stop()
	for {
		select {
		case e := <-done:
			if a.cfg.Mode == "public" {
				c, cancel := context.WithTimeout(context.Background(), 3*time.Second)
				a.request(c, "DELETE", "/api/servers/self", nil, nil)
				cancel()
			}
			return e
		case <-initial.C:
			if a.cfg.Mode == "public" {
				a.publish(ctx)
			}
		case <-ticker.C:
			if a.cfg.Mode == "public" {
				a.publish(ctx)
			}
		case <-ctx.Done():
			cmd.Process.Signal(os.Interrupt)
			select {
			case <-done:
			case <-time.After(5 * time.Second):
				cmd.Process.Kill()
				<-done
			}
			if a.cfg.Mode == "public" {
				c, cancel := context.WithTimeout(context.Background(), 3*time.Second)
				a.request(c, "DELETE", "/api/servers/self", nil, nil)
				cancel()
			}
			return nil
		}
	}
}
