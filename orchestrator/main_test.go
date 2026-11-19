package main

import (
	"bytes"
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"os"
	"path/filepath"
	"strings"
	"testing"
)

func fixture(t *testing.T) *Orchestrator {
	t.Helper()
	root := t.TempDir()
	if err := os.Mkdir(filepath.Join(root, "maps"), 0755); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(filepath.Join(root, "maps", "test.map"), []byte("map"), 0644); err != nil {
		t.Fatal(err)
	}
	executable := filepath.Join(root, "fake-zod")
	if err := os.WriteFile(executable, []byte("#!/bin/sh\nexit 0\n"), 0755); err != nil {
		t.Fatal(err)
	}
	o := newOrchestrator(executable, root, "127.0.0.1", 300, 23456, 23456, 2, "")
	o.mapTeams["test.map"] = []string{"red", "blue"}
	o.mapPlayers["test.map"] = 2
	return o
}
func TestSetupRejectsInvalidSettingsBeforeStarting(t *testing.T) {
	for _, extra := range []string{
		`"difficulty":-1`, `"difficulty":4`, `"game_speed":0`, `"game_speed":2.5`,
		`"human_team":"orange"`, `"human_team":"purple"`, `"bots":["green"]`, `"human_team":"red","bots":["red"]`, `"bots":["blue","blue"]`,
	} {
		t.Run(extra, func(t *testing.T) {
			o := fixture(t)
			request := httptest.NewRequest("POST", "/matches", strings.NewReader(`{"map":"test.map",`+extra+`}`))
			response := httptest.NewRecorder()
			o.handleCreate(response, request)
			if response.Code != http.StatusBadRequest {
				t.Fatalf("got %d: %s", response.Code, response.Body.String())
			}
			if len(o.freePorts) != 1 {
				t.Fatal("invalid request consumed a port")
			}
		})
	}
}
func TestSetupPassesDifficultyAndSpeedToChild(t *testing.T) {
	o := fixture(t)
	match, err := o.createMatch("custom", "test.map", []string{"red"}, false, matchSettings{3, 1.5})
	if err != nil {
		t.Fatal(err)
	}
	if !strings.Contains(strings.Join(match.cmd.Args, " "), "-D 3") {
		t.Fatal(match.cmd.Args)
	}
	if !strings.Contains(strings.Join(match.cmd.Env, "\n"), "ZOD_GAMESPEED=1.5") {
		t.Fatal("speed missing from child environment")
	}
	data, err := json.Marshal(match)
	if err != nil || !bytes.Contains(data, []byte(`"difficulty":3`)) || !bytes.Contains(data, []byte(`"game_speed":1.5`)) {
		t.Fatalf("wrong response: %s %v", data, err)
	}
}
