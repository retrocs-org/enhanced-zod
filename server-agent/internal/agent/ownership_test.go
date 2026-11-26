package agent

import (
	"bytes"
	"context"
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"os"
	"path/filepath"
	"strings"
	"sync/atomic"
	"testing"
	"time"
)

func TestOwnerAuthorizationPersistsOnlyScopedCredential(t *testing.T) {
	var key string
	hub := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Content-Type", "application/json")
		switch r.URL.Path {
		case "/api/server-auth/start":
			var in map[string]string
			if json.NewDecoder(r.Body).Decode(&in) != nil || in["server"] != "owned" || len(in["key"]) != 64 || len(in) != 3 {
				t.Error("invalid enrollment payload")
			}
			key = in["key"]
			json.NewEncoder(w).Encode(map[string]string{"device": strings.Repeat("a", 64), "user_code": "ABCDEF123456"})
		case "/api/server-auth/poll":
			json.NewEncoder(w).Encode(map[string]string{"state": "approved", "server": "owned", "owner": strings.Repeat("b", 64)})
		default:
			t.Errorf("unexpected endpoint %s", r.URL.Path)
			http.Error(w, "not found", 404)
		}
	}))
	defer hub.Close()
	c := Config{Mode: "public", Hub: hub.URL, ID: "owned", Name: "Example", Port: 2137, State: t.TempDir()}
	var output bytes.Buffer
	ctx, cancel := context.WithTimeout(context.Background(), 6*time.Second)
	defer cancel()
	if e := Authorize(ctx, &c, &output); e != nil {
		t.Fatal(e)
	}
	if c.Key != key || strings.Contains(output.String(), c.Key) || strings.Contains(output.String(), strings.Repeat("a", 64)) {
		t.Fatal("host/device credential leaked to console")
	}
	saved := c
	saved.Key = ""
	if e := LoadIdentity(&saved); e != nil || saved.Key != c.Key {
		t.Fatal("identity did not survive restart", e)
	}
	data, e := os.ReadFile(identityPath(c))
	if e != nil {
		t.Fatal(e)
	}
	var fields map[string]any
	if json.Unmarshal(data, &fields) != nil || len(fields) != 3 {
		t.Fatal("unexpected persisted credentials")
	}
	info, _ := os.Stat(identityPath(c))
	if info.Mode().Perm()&0077 != 0 {
		t.Fatal("credential readable by other users")
	}
	saved.Key = ""
	saved.ID = "another-server"
	if LoadIdentity(&saved) == nil {
		t.Fatal("server identity reused across IDs")
	}
}
func TestRolesRequireAgentSecretAndReflectRevocation(t *testing.T) {
	var delegated atomic.Bool
	delegated.Store(true)
	hub := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if r.Header.Get("Authorization") != "Bearer "+strings.Repeat("c", 64) {
			t.Error("wrong server credential")
		}
		admins := []string{}
		if delegated.Load() {
			admins = append(admins, strings.Repeat("b", 64))
		}
		json.NewEncoder(w).Encode(map[string]any{"owner": strings.Repeat("a", 64), "admins": admins})
	}))
	defer hub.Close()
	a, e := New(Config{Mode: "public", Hub: hub.URL, ID: "owned", Key: strings.Repeat("c", 64), Port: 2137})
	if e != nil {
		t.Fatal(e)
	}
	request := func(secret string) *httptest.ResponseRecorder {
		r := httptest.NewRequest("POST", "/roles", strings.NewReader(`{"Secret":"`+secret+`"}`))
		w := httptest.NewRecorder()
		a.Roles(w, r)
		return w
	}
	if request("wrong").Code != 401 {
		t.Fatal("unauthenticated local role lookup")
	}
	if w := request(a.secret); w.Code != 200 || !strings.Contains(w.Body.String(), strings.Repeat("b", 64)) {
		t.Fatal("delegated role missing")
	}
	delegated.Store(false)
	if w := request(a.secret); w.Code != 200 || strings.Contains(w.Body.String(), strings.Repeat("b", 64)) {
		t.Fatal("revoked role retained")
	}
	hub.Close()
	if request(a.secret).Code != 503 {
		t.Fatal("portal outage did not fail closed")
	}
}
func TestUnownedPublicHostCannotStartEngine(t *testing.T) {
	hub := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		json.NewEncoder(w).Encode(map[string]string{"id": "owned", "owner": ""})
	}))
	defer hub.Close()
	state := filepath.Join(t.TempDir(), "state")
	a, e := New(Config{Mode: "public", Hub: hub.URL, ID: "owned", Key: strings.Repeat("c", 64), Port: 2137, Engine: "must-not-launch", State: state})
	if e != nil {
		t.Fatal(e)
	}
	if e = a.Run(context.Background()); e == nil || !strings.Contains(e.Error(), "owner") {
		t.Fatal("unowned engine was started", e)
	}
	if _, e = os.Stat(filepath.Join(state, "outbox")); !os.IsNotExist(e) {
		t.Fatal("host launch proceeded before owner check")
	}
}
