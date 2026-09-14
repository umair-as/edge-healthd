// SPDX-License-Identifier: MIT

package main

import (
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"

	"github.com/gorilla/websocket"
)

// The production middleware chain must not break the WebSocket upgrade.
func TestWebSocketUpgradeThroughMiddleware(t *testing.T) {
	cfg := &Config{}
	upgrader := newUpgrader(cfg)
	upgrade := http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		conn, err := upgrader.Upgrade(w, r, nil)
		if err != nil {
			return
		}
		defer conn.Close()
		_ = conn.WriteMessage(websocket.TextMessage, []byte(`{"ok":true}`))
	})

	srv := httptest.NewServer(LoggingMiddleware(SecurityHeadersMiddleware(upgrade)))
	defer srv.Close()

	url := "ws" + strings.TrimPrefix(srv.URL, "http")
	conn, resp, err := websocket.DefaultDialer.Dial(url, nil)
	if err != nil {
		status := 0
		if resp != nil {
			status = resp.StatusCode
		}
		t.Fatalf("dial through middleware failed (HTTP %d): %v", status, err)
	}
	defer conn.Close()

	_, msg, err := conn.ReadMessage()
	if err != nil {
		t.Fatalf("read after upgrade: %v", err)
	}
	if string(msg) != `{"ok":true}` {
		t.Fatalf("unexpected message %q", msg)
	}
}

func TestLoggingMiddlewareRecordsSwitchingProtocols(t *testing.T) {
	status := make(chan int, 1)
	upgrader := newUpgrader(&Config{})
	inner := http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		conn, err := upgrader.Upgrade(w, r, nil)
		if err != nil {
			status <- -1
			return
		}
		defer conn.Close()
		status <- w.(*responseWriter).status
	})
	srv := httptest.NewServer(LoggingMiddleware(inner))
	defer srv.Close()

	conn, _, err := websocket.DefaultDialer.Dial("ws"+strings.TrimPrefix(srv.URL, "http"), nil)
	if err != nil {
		t.Fatalf("dial: %v", err)
	}
	defer conn.Close()

	if got := <-status; got != http.StatusSwitchingProtocols {
		t.Fatalf("logged status = %d, want %d", got, http.StatusSwitchingProtocols)
	}
}
