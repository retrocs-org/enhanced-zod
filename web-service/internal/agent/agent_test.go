package agent

import (
	"strings"
	"testing"
)

func TestPublicPrivatePolicy(t *testing.T) {
	for _, mode := range []string{"", "invalid"} {
		if _, e := New(Config{Mode: mode, Port: 2137}); e == nil {
			t.Fatal("invalid mode accepted")
		}
	}
	if _, e := New(Config{Mode: "private", Port: 2137}); e != nil {
		t.Fatal(e)
	}
	base := Config{Mode: "public", Port: 2137, Hub: "https://example.com", ID: "one", Key: strings.Repeat("a", 64)}
	if _, e := New(base); e != nil {
		t.Fatal(e)
	}
	base.Hub = "http://example.com"
	if _, e := New(base); e == nil {
		t.Fatal("clear-text public API accepted")
	}
	base.Hub = "http://127.0.0.1:8088"
	if _, e := New(base); e != nil {
		t.Fatal(e)
	}
	base.Key = ""
	if _, e := New(base); e == nil {
		t.Fatal("unauthenticated public registration accepted")
	}
}
