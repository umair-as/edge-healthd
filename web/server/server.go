// SPDX-License-Identifier: MIT

package main

import (
	"context"
	"net/http"
	"sync"
	"time"
)

// Server is the main HTTP server
type Server struct {
	cfg     *Config
	mux     *http.ServeMux
	httpSrv *http.Server
	hub     *WebSocketHub
	watcher *StateWatcher
	state   *StateCache
}

// StateCache holds the cached state with atomic access
type StateCache struct {
	mu    sync.RWMutex
	data  []byte
	mtime time.Time
}

// Get returns a copy of the cached state
func (sc *StateCache) Get() ([]byte, time.Time) {
	sc.mu.RLock()
	defer sc.mu.RUnlock()
	if sc.data == nil {
		return nil, time.Time{}
	}
	cpy := make([]byte, len(sc.data))
	copy(cpy, sc.data)
	return cpy, sc.mtime
}

// Set updates the cached state
func (sc *StateCache) Set(data []byte, mtime time.Time) {
	sc.mu.Lock()
	defer sc.mu.Unlock()
	sc.data = make([]byte, len(data))
	copy(sc.data, data)
	sc.mtime = mtime
}

// NewServer creates a new server instance
func NewServer(cfg *Config) (*Server, error) {
	s := &Server{
		cfg:   cfg,
		mux:   http.NewServeMux(),
		state: &StateCache{},
	}

	// Create WebSocket hub
	s.hub = NewWebSocketHub()

	// Create state watcher
	watcher, err := NewStateWatcher(cfg, s.state, s.hub)
	if err != nil {
		return nil, err
	}
	s.watcher = watcher

	// Setup routes
	s.setupRoutes()

	// Create HTTP server
	s.httpSrv = &http.Server{
		Addr:         cfg.ListenAddr,
		Handler:      s.applyMiddleware(s.mux),
		ReadTimeout:  15 * time.Second,
		WriteTimeout: 15 * time.Second,
		IdleTimeout:  60 * time.Second,
	}

	return s, nil
}

// setupRoutes configures all HTTP routes
func (s *Server) setupRoutes() {
	// API endpoints
	s.mux.HandleFunc("GET /api/health", s.handleHealthAPI)
	s.mux.HandleFunc("POST /api/trigger", s.handleTrigger)

	// WebSocket endpoint
	s.mux.HandleFunc("GET /ws/health", s.handleWebSocket)

	// Static files (SPA)
	s.mux.Handle("GET /", http.FileServer(http.FS(staticFS)))
}

// applyMiddleware wraps the handler with all middleware
func (s *Server) applyMiddleware(h http.Handler) http.Handler {
	// Apply in reverse order (last applied runs first)
	h = SecurityHeadersMiddleware(h)
	if s.cfg.LocalOnly {
		h = LocalOnlyMiddleware(h)
	}
	h = LoggingMiddleware(h)
	return h
}

// Run starts the server and blocks until context is cancelled
func (s *Server) Run(ctx context.Context) error {
	// Start WebSocket hub
	go s.hub.Run(ctx)

	// Start file watcher
	go s.watcher.Watch(ctx)

	// Start HTTP(S) server in goroutine
	errCh := make(chan error, 1)
	go func() {
		var err error
		if s.cfg.TLSEnabled() {
			err = s.httpSrv.ListenAndServeTLS(s.cfg.TLSCert, s.cfg.TLSKey)
		} else {
			err = s.httpSrv.ListenAndServe()
		}
		if err != http.ErrServerClosed {
			errCh <- err
		}
		close(errCh)
	}()

	// Wait for shutdown signal or error
	select {
	case <-ctx.Done():
		// Graceful shutdown
		shutdownCtx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
		defer cancel()
		return s.httpSrv.Shutdown(shutdownCtx)
	case err := <-errCh:
		return err
	}
}
