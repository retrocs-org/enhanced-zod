package agent

import (
	"context"
	"crypto/rand"
	"crypto/subtle"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"net/http"
	"net/url"
	"os"
	"path/filepath"
	"strings"
	"time"
)

type hostIdentity struct{ Hub, Server, Key string }

func identityPath(c Config) string { return filepath.Join(c.State, "credentials", "identity.json") }

// Only server-scoped credentials are persisted. Cloud passwords/sessions are never used here.
func LoadIdentity(c *Config) error {
	if c.Mode != "public" || c.Key != "" {
		return nil
	}
	file := identityPath(*c)
	info, e := os.Lstat(file)
	if os.IsNotExist(e) {
		return nil
	}
	if e != nil {
		return e
	}
	if !info.Mode().IsRegular() || info.Size() > 4096 {
		return errors.New("server identity must be a regular file")
	}
	data, e := os.ReadFile(file)
	if e != nil {
		return e
	}
	var saved hostIdentity
	if json.Unmarshal(data, &saved) != nil || saved.Server != c.ID || strings.TrimRight(saved.Hub, "/") != strings.TrimRight(c.Hub, "/") || len(saved.Key) != 64 {
		return errors.New("stored server identity does not match this hub and server ID; use a separate --state directory")
	}
	if _, e = hex.DecodeString(saved.Key); e != nil {
		return errors.New("invalid stored server credential")
	}
	c.Key = saved.Key
	return nil
}
func saveIdentity(c Config) error {
	file := identityPath(c)
	if e := os.MkdirAll(filepath.Dir(file), 0700); e != nil {
		return e
	}
	if e := os.Chmod(filepath.Dir(file), 0700); e != nil {
		return e
	}
	nonce := make([]byte, 8)
	if _, e := rand.Read(nonce); e != nil {
		return e
	}
	temporary := file + ".tmp-" + hex.EncodeToString(nonce)
	out, e := os.OpenFile(temporary, os.O_CREATE|os.O_EXCL|os.O_WRONLY, 0600)
	if e != nil {
		return e
	}
	defer os.Remove(temporary)
	e = json.NewEncoder(out).Encode(hostIdentity{c.Hub, c.ID, c.Key})
	if e == nil {
		e = out.Sync()
	}
	closeErr := out.Close()
	if e != nil {
		return e
	}
	if closeErr != nil {
		return closeErr
	}
	return os.Rename(temporary, file)
}
func Authorize(ctx context.Context, c *Config, output io.Writer) error {
	if c.Mode != "public" {
		return errors.New("--login is only required for public servers")
	}
	if c.Key == "" {
		key := make([]byte, 32)
		if _, e := rand.Read(key); e != nil {
			return e
		}
		c.Key = hex.EncodeToString(key)
	}
	a, e := New(*c)
	if e != nil {
		return e
	}
	// Keep the host key before approval so interruption cannot orphan a registered server.
	if e = saveIdentity(*c); e != nil {
		return fmt.Errorf("save private server credential: %w", e)
	}
	var start struct {
		Device string `json:"device"`
		Code   string `json:"user_code"`
	}
	if e = a.request(ctx, "POST", "/api/server-auth/start", map[string]string{"server": c.ID, "name": c.Name, "key": c.Key}, &start); e != nil {
		return fmt.Errorf("start server sign-in: %w", e)
	}
	if len(start.Device) != 64 || len(start.Code) != 12 {
		return errors.New("invalid server sign-in response")
	}
	if _, e = hex.DecodeString(start.Device); e != nil {
		return errors.New("invalid device secret")
	}
	if _, e = hex.DecodeString(start.Code); e != nil {
		return errors.New("invalid sign-in code")
	}
	link := strings.TrimRight(c.Hub, "/") + "/server-auth?code=" + url.QueryEscape(start.Code)
	fmt.Fprintf(output, "Open this URL in your browser and sign in as the server owner:\n%s\nCode: %s\nWaiting for explicit approval (five minutes).\n", link, start.Code)
	consumed := false
	defer func() {
		if !consumed {
			cancelCtx, cancel := context.WithTimeout(context.Background(), 3*time.Second)
			defer cancel()
			a.request(cancelCtx, "POST", "/api/server-auth/cancel", map[string]string{"device": start.Device}, nil)
		}
	}()
	timeout := time.NewTimer(5 * time.Minute)
	defer timeout.Stop()
	tick := time.NewTicker(2 * time.Second)
	defer tick.Stop()
	for {
		select {
		case <-ctx.Done():
			return ctx.Err()
		case <-timeout.C:
			return errors.New("server sign-in expired; retry --login")
		case <-tick.C:
			var result struct{ State, Server, Owner string }
			if e = a.request(ctx, "POST", "/api/server-auth/poll", map[string]string{"device": start.Device}, &result); e != nil {
				var status *hubStatusError
				if errors.As(e, &status) && status.Code == 429 {
					continue
				}
				return fmt.Errorf("server sign-in failed: %w", e)
			}
			if result.State == "pending" {
				continue
			}
			if result.State != "approved" || result.Server != c.ID || len(result.Owner) != 64 {
				return errors.New("invalid server ownership approval")
			}
			if _, e = hex.DecodeString(result.Owner); e != nil {
				return errors.New("invalid owner identity")
			}
			consumed = true
			fmt.Fprintln(output, "Server ownership confirmed. The owner can manage administrators under My servers.")
			return nil
		}
	}
}
func (a *Agent) Roles(w http.ResponseWriter, r *http.Request) {
	if r.Method != "POST" {
		http.Error(w, "POST required", 405)
		return
	}
	var in struct{ Secret string }
	if json.NewDecoder(http.MaxBytesReader(w, r.Body, 1024)).Decode(&in) != nil || subtle.ConstantTimeCompare([]byte(in.Secret), []byte(a.secret)) != 1 {
		http.Error(w, "denied", 401)
		return
	}
	var roles struct {
		Owner  string
		Admins []string
	}
	if e := a.request(r.Context(), "GET", "/api/server/admins", nil, &roles); e != nil {
		http.Error(w, "server roles unavailable", 503)
		return
	}
	if len(roles.Owner) != 64 || len(roles.Admins) > 32 {
		http.Error(w, "invalid roles", 502)
		return
	}
	if _, e := hex.DecodeString(roles.Owner); e != nil {
		http.Error(w, "invalid owner", 502)
		return
	}
	for _, id := range roles.Admins {
		if len(id) != 64 {
			http.Error(w, "invalid role", 502)
			return
		}
		if _, e := hex.DecodeString(id); e != nil {
			http.Error(w, "invalid role", 502)
			return
		}
	}
	w.Header().Set("Content-Type", "text/plain")
	fmt.Fprintln(w, roles.Owner)
	for _, id := range roles.Admins {
		fmt.Fprintln(w, id)
	}
}
