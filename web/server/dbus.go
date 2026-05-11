// SPDX-License-Identifier: MIT

package main

import (
	"sync"

	"github.com/godbus/dbus/v5"
)

// dbusClient holds a single long-lived connection to the system bus and
// reconnects lazily when the previous connection has been closed. Mirrors
// the C++ daemon's shared-connection refactor (PR #33) so /api/trigger no
// longer opens a fresh FD against dbus-daemon per request.
//
// All exported methods are safe for concurrent callers; the underlying
// *dbus.Conn is itself safe for concurrent Send/Call, the mutex only
// guards the connect / reconnect window.
type dbusClient struct {
	mu   sync.Mutex
	conn *dbus.Conn
}

// get returns a live connection, dialing or reconnecting if needed.
func (c *dbusClient) get() (*dbus.Conn, error) {
	c.mu.Lock()
	defer c.mu.Unlock()

	if c.conn != nil && c.conn.Connected() {
		return c.conn, nil
	}
	if c.conn != nil {
		_ = c.conn.Close()
		c.conn = nil
	}

	conn, err := dbus.ConnectSystemBus()
	if err != nil {
		return nil, err
	}
	c.conn = conn
	return conn, nil
}

// close tears down the connection on shutdown.
func (c *dbusClient) close() {
	c.mu.Lock()
	defer c.mu.Unlock()
	if c.conn != nil {
		_ = c.conn.Close()
		c.conn = nil
	}
}
