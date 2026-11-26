package agent

import (
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"
)

func TestOperationsWireAndScopedAcknowledgement(t *testing.T) {
	owner, moderator, player := strings.Repeat("a", 64), strings.Repeat("b", 64), strings.Repeat("c", 64)
	acked := false
	hub := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if r.Header.Get("Authorization") != "Bearer "+strings.Repeat("d", 64) {
			t.Error("missing scoped host credential")
		}
		if r.URL.Path == "/api/server/control/ack" {
			var in struct {
				Results []struct {
					ID      string
					Applied bool
				}
			}
			if json.NewDecoder(r.Body).Decode(&in) != nil || len(in.Results) != 1 || in.Results[0].ID != "42" || !in.Results[0].Applied {
				t.Error("incorrect acknowledgement")
			}
			acked = true
			w.Write([]byte(`{"ok":true}`))
			return
		}
		json.NewEncoder(w).Encode(map[string]any{"owner": owner, "admins": []string{}, "moderators": []string{moderator}, "bans": []map[string]any{{"account": player, "reason": "Abuse", "expires": 1900000000}}, "commands": []map[string]any{{"id": "42", "actor": moderator, "target": player, "reason": "Abuse"}}, "config": map[string]any{"revision": 2, "settings": map[string]any{"rotation": []string{"maps/a.map"}, "bots": []int{2}, "difficulty": 1, "speed": 125, "voting": true}}})
	}))
	defer hub.Close()
	a, e := New(Config{Mode: "public", Hub: hub.URL, ID: "owned", Key: strings.Repeat("d", 64), Port: 2137})
	if e != nil {
		t.Fatal(e)
	}
	w := httptest.NewRecorder()
	a.Roles(w, httptest.NewRequest("POST", "/roles", strings.NewReader(`{"Secret":"`+a.secret+`"}`)))
	if w.Code != 200 || !strings.Contains(w.Body.String(), "m:"+moderator) || !strings.Contains(w.Body.String(), "k:42:"+moderator+":"+player) || !strings.Contains(w.Body.String(), "c:2:1:125:1") {
		t.Fatal("operations wire missing", w.Code, w.Body.String())
	}
	w = httptest.NewRecorder()
	a.Ack(w, httptest.NewRequest("POST", "/ack", strings.NewReader(`{"Secret":"wrong","results":[]}`)))
	if w.Code != 401 || acked {
		t.Fatal("unauthorized acknowledgement")
	}
	w = httptest.NewRecorder()
	a.Ack(w, httptest.NewRequest("POST", "/ack", strings.NewReader(`{"Secret":"`+a.secret+`","results":[{"id":"42","applied":true}]}`)))
	if w.Code != 204 || !acked {
		t.Fatal("acknowledgement failed", w.Code)
	}
}
