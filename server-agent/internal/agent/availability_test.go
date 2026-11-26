package agent

import (
	"context"
	"encoding/json"
	"net"
	"net/http"
	"net/http/httptest"
	"os"
	"path/filepath"
	"runtime"
	"strings"
	"testing"
)

func TestHeartbeatPreservesAvailabilityAndLegacyUnknowns(t *testing.T) {
	if runtime.GOOS == "windows" {
		t.Skip("engine status sockets are Unix-only")
	}
	for _, modern := range []bool{true, false} {
		t.Run(map[bool]string{true: "modern", false: "legacy"}[modern], func(t *testing.T) {
			// Keep the socket path short enough for macOS sockaddr_un.
			state, err := os.MkdirTemp("/tmp", "zod-av-")
			if err != nil {
				t.Fatal(err)
			}
			defer os.RemoveAll(state)
			listener, err := net.Listen("unix", filepath.Join(state, "status.sock"))
			if err != nil {
				t.Fatal(err)
			}
			defer listener.Close()
			go func() {
				connection, e := listener.Accept()
				if e != nil {
					return
				}
				defer connection.Close()
				snapshot := map[string]any{"players": 1, "capacity": 2, "map": "maps/test.map", "version": "test"}
				if modern {
					snapshot["player_slots"] = 0
					snapshot["spectators"] = 3
					snapshot["bots"] = 1
					snapshot["difficulty"] = 2
					snapshot["speed_percent"] = 125
				}
				json.NewEncoder(connection).Encode(snapshot)
			}()
			seen := make(chan bool, 1)
			hub := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
				if r.URL.Path != "/api/servers/heartbeat" {
					t.Errorf("unexpected endpoint %s", r.URL.Path)
				}
				var payload map[string]any
				if err := json.NewDecoder(r.Body).Decode(&payload); err != nil {
					t.Error(err)
				}
				_, present := payload["player_slots"]
				if present != modern {
					t.Error("legacy availability was invented or modern availability omitted")
				}
				if modern && (payload["player_slots"] != float64(0) || payload["spectators"] != float64(3) || payload["bots"] != float64(1) || payload["difficulty"] != float64(2) || payload["speed_percent"] != float64(125)) {
					t.Error("live rules lost", payload)
				}
				seen <- true
				w.Write([]byte(`{}`))
			}))
			defer hub.Close()
			a, e := New(Config{Mode: "public", Hub: hub.URL, ID: "test", Key: strings.Repeat("a", 64), Host: "127.0.0.1", Port: 2137, Capacity: 2, State: state})
			if e != nil {
				t.Fatal(e)
			}
			a.publish(context.Background())
			select {
			case <-seen:
			default:
				t.Fatal("heartbeat not received")
			}
		})
	}
}
