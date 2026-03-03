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
	showVersion := flag.Bool("version", false, "Show version and exit")
	flag.Parse()

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

	if err := srv.Run(ctx); err != nil {
		log.Fatalf("Server error: %v", err)
	}
}
