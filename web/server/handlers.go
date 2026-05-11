// SPDX-License-Identifier: MIT

package main

import (
	"encoding/json"
	"net/http"
	"time"
)

// handleHealthAPI serves the current state.json
func (s *Server) handleHealthAPI(w http.ResponseWriter, r *http.Request) {
	data, mtime := s.state.Get()

	if data == nil {
		http.Error(w, `{"error": "state not available"}`, http.StatusServiceUnavailable)
		return
	}

	// Set cache headers
	w.Header().Set("Content-Type", "application/json")
	w.Header().Set("Cache-Control", "no-cache, no-store, must-revalidate")
	w.Header().Set("Last-Modified", mtime.UTC().Format(http.TimeFormat))

	// Check If-Modified-Since
	if ims := r.Header.Get("If-Modified-Since"); ims != "" {
		if t, err := time.Parse(http.TimeFormat, ims); err == nil {
			if mtime.Truncate(time.Second).Equal(t.Truncate(time.Second)) || mtime.Before(t) {
				w.WriteHeader(http.StatusNotModified)
				return
			}
		}
	}

	w.WriteHeader(http.StatusOK)
	w.Write(data)
}

// handleTrigger calls TriggerSnapshot on the edge.health D-Bus service.
// Returns {"triggered":true} if accepted, {"triggered":false} if rate-limited,
// or 503 if the daemon D-Bus service is unavailable (e.g. dev environment).
//
// CSRF defense: requires the custom header `X-Edge-Health: 1` plus a same-
// origin (or -allowed-origins) Origin. A browser cannot set X-Edge-Health on
// a cross-origin POST without a CORS preflight, and we never reply with the
// CORS allow-headers a preflight needs — so cross-site fetch() is blocked
// before it reaches the D-Bus call.
func (s *Server) handleTrigger(w http.ResponseWriter, r *http.Request) {
	if !isOriginAllowed(r, s.cfg.AllowedOrigins) {
		w.Header().Set("Content-Type", "application/json")
		w.WriteHeader(http.StatusForbidden)
		w.Write([]byte(`{"error":"forbidden: origin not allowed"}`))
		return
	}
	if r.Header.Get("X-Edge-Health") != "1" {
		w.Header().Set("Content-Type", "application/json")
		w.WriteHeader(http.StatusForbidden)
		w.Write([]byte(`{"error":"forbidden: missing X-Edge-Health header"}`))
		return
	}

	conn, err := s.dbus.get()
	if err != nil {
		w.Header().Set("Content-Type", "application/json")
		w.WriteHeader(http.StatusServiceUnavailable)
		w.Write([]byte(`{"error":"D-Bus system bus unavailable"}`))
		return
	}

	obj := conn.Object("edge.health", "/edge/health/manager")
	var triggered bool
	call := obj.Call("edge.health.Manager.TriggerSnapshot", 0)
	if call.Err != nil {
		w.Header().Set("Content-Type", "application/json")
		w.WriteHeader(http.StatusBadGateway)
		json.NewEncoder(w).Encode(map[string]string{"error": call.Err.Error()})
		return
	}
	if err := call.Store(&triggered); err != nil {
		w.Header().Set("Content-Type", "application/json")
		w.WriteHeader(http.StatusInternalServerError)
		w.Write([]byte(`{"error":"unexpected response from daemon"}`))
		return
	}

	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(map[string]bool{"triggered": triggered})
}

// handleWebSocket upgrades to WebSocket and registers the client
func (s *Server) handleWebSocket(w http.ResponseWriter, r *http.Request) {
	conn, err := s.upgrader.Upgrade(w, r, nil)
	if err != nil {
		// Upgrade already wrote error response
		return
	}

	client := &Client{
		hub:  s.hub,
		conn: conn,
		send: make(chan []byte, 256),
	}

	s.hub.register <- client

	// Start read/write pumps
	go client.writePump()
	go client.readPump()

	// Send current state immediately
	if data, _ := s.state.Get(); data != nil {
		client.send <- data
	}
}
