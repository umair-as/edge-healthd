// SPDX-License-Identifier: MIT
// edge-healthd-ui: Web UI server for edge-healthd

package main

import (
	"context"
	"flag"
	"fmt"
	"log"
	"os"
	"os/signal"
	"syscall"
)

var (
	version = "dev"
)

func main() {
	cfg := &Config{}

	flag.StringVar(&cfg.ListenAddr, "listen", ":8443", "HTTPS listen address")
	flag.StringVar(&cfg.StateFile, "state", "/data/edge/health/state.json", "Path to state.json file")
	flag.BoolVar(&cfg.LocalOnly, "local-only", true, "Only accept connections from private/local IPs")
	flag.StringVar(&cfg.TLSCert, "tls-cert", "", "TLS certificate file (PEM); enables HTTPS when set")
	flag.StringVar(&cfg.TLSKey, "tls-key", "", "TLS private key file (PEM); enables HTTPS when set")
	var allowedOrigins string
	flag.StringVar(&allowedOrigins, "allowed-origins", "",
		"Comma-separated extra Origins permitted for WebSocket + mutating endpoints "+
			"(host[:port] or full URL). Same-origin is always allowed. Dev proxies "+
			"like Vite need this; production should leave empty.")
	showVersion := flag.Bool("version", false, "Show version and exit")
	flag.Parse()
	cfg.SetAllowedOrigins(allowedOrigins)

	if *showVersion {
		fmt.Printf("edge-healthd-ui %s\n", version)
		os.Exit(0)
	}

	if err := cfg.Validate(); err != nil {
		log.Fatalf("Configuration error: %v", err)
	}

	// Create server
	srv, err := NewServer(cfg)
	if err != nil {
		log.Fatalf("Failed to create server: %v", err)
	}

	// Setup signal handling
	ctx, cancel := context.WithCancel(context.Background())
	sigCh := make(chan os.Signal, 1)
	signal.Notify(sigCh, syscall.SIGINT, syscall.SIGTERM)

	go func() {
		sig := <-sigCh
		log.Printf("Received signal %v, shutting down...", sig)
		cancel()
	}()

	// Start server
	if cfg.TLSEnabled() {
		log.Printf("Starting edge-healthd-ui (HTTPS) on %s", cfg.ListenAddr)
	} else {
		log.Printf("Starting edge-healthd-ui (HTTP) on %s", cfg.ListenAddr)
	}
	log.Printf("Watching state file: %s", cfg.StateFile)

	if !cfg.LocalOnly {
		log.Printf("WARNING: -local-only=false — RFC1918 source-IP allowlist is DISABLED. " +
			"The server will accept connections from any IP. Do not ship this flag.")
	}
	if len(cfg.AllowedOrigins) > 0 {
		log.Printf("WARNING: -allowed-origins is set to %v — only use this for dev proxies "+
			"(e.g. Vite). Production should leave it empty so only same-origin is accepted.",
			cfg.AllowedOrigins)
	}

	if err := srv.Run(ctx); err != nil {
		log.Fatalf("Server error: %v", err)
	}
}
