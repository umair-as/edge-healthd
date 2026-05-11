// SPDX-License-Identifier: MIT

package main

import (
	"errors"
	"net/url"
	"path/filepath"
	"strings"
)

// Config holds the server configuration
type Config struct {
	// ListenAddr is the address to listen on (e.g., ":8443", "127.0.0.1:8443")
	ListenAddr string

	// StateFile is the path to the state.json file written by edge-healthd
	StateFile string

	// LocalOnly restricts access to private/local IP addresses only
	LocalOnly bool

	// TLSCert is the path to the TLS certificate file (PEM). Empty disables TLS.
	TLSCert string

	// TLSKey is the path to the TLS private key file (PEM). Empty disables TLS.
	TLSKey string

	// AllowedOrigins is the parsed list of additional origins permitted for
	// WebSocket upgrades and mutating endpoints, beyond same-origin. Populated
	// from -allowed-origins (comma-separated). Each entry is host[:port] —
	// the scheme is ignored. Primarily needed when a dev proxy (e.g. Vite's
	// changeOrigin) makes Origin differ from r.Host.
	AllowedOrigins []string

	// allowedOriginsRaw is the raw flag string before parsing.
	allowedOriginsRaw string
}

// TLSEnabled reports whether TLS is configured.
func (c *Config) TLSEnabled() bool {
	return c.TLSCert != "" && c.TLSKey != ""
}

// Validate checks the configuration for errors
func (c *Config) Validate() error {
	if c.ListenAddr == "" {
		return errors.New("listen address cannot be empty")
	}

	if c.StateFile == "" {
		return errors.New("state file path cannot be empty")
	}

	// Normalize state file path
	c.StateFile = filepath.Clean(c.StateFile)

	// TLS cert and key must be specified together
	if (c.TLSCert == "") != (c.TLSKey == "") {
		return errors.New("both -tls-cert and -tls-key must be specified together")
	}

	if c.TLSEnabled() {
		c.TLSCert = filepath.Clean(c.TLSCert)
		c.TLSKey = filepath.Clean(c.TLSKey)
	}

	if c.allowedOriginsRaw != "" {
		for _, raw := range strings.Split(c.allowedOriginsRaw, ",") {
			entry := strings.TrimSpace(raw)
			if entry == "" {
				continue
			}
			host, err := normalizeOriginHost(entry)
			if err != nil {
				return err
			}
			c.AllowedOrigins = append(c.AllowedOrigins, host)
		}
	}

	return nil
}

// SetAllowedOrigins records the raw flag value so Validate can parse it.
func (c *Config) SetAllowedOrigins(raw string) {
	c.allowedOriginsRaw = raw
}

// normalizeOriginHost extracts the host[:port] from an origin entry. Accepts
// either a full URL ("http://example.com:8080") or a bare host[:port]
// ("example.com:8080"). Rejects entries with a path/query/fragment so the
// allowlist matches an unambiguous browser Origin.
func normalizeOriginHost(entry string) (string, error) {
	if strings.Contains(entry, "://") {
		u, err := url.Parse(entry)
		if err != nil || u.Host == "" {
			return "", errors.New("invalid origin: " + entry)
		}
		if u.Path != "" && u.Path != "/" {
			return "", errors.New("origin must not include a path: " + entry)
		}
		return u.Host, nil
	}
	if strings.ContainsAny(entry, "/?#") {
		return "", errors.New("invalid origin (use host[:port] or full URL): " + entry)
	}
	return entry, nil
}

// StateDir returns the directory containing the state file (for watching)
func (c *Config) StateDir() string {
	return filepath.Dir(c.StateFile)
}

// StateFileName returns just the filename of the state file
func (c *Config) StateFileName() string {
	return filepath.Base(c.StateFile)
}
