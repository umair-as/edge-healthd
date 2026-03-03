// SPDX-License-Identifier: MIT

package main

import (
	"errors"
	"path/filepath"
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

	return nil
}

// StateDir returns the directory containing the state file (for watching)
func (c *Config) StateDir() string {
	return filepath.Dir(c.StateFile)
}

// StateFileName returns just the filename of the state file
func (c *Config) StateFileName() string {
	return filepath.Base(c.StateFile)
}
