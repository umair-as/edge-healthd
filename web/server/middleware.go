// SPDX-License-Identifier: MIT

package main

import (
	"bufio"
	"errors"
	"log"
	"net"
	"net/http"
	"net/url"
	"time"
)

// isOriginAllowed returns true when the request's Origin header is either
// empty (non-browser client) or matches r.Host / one of the configured
// AllowedOrigins. This is the same check used for WebSocket upgrades and
// for mutating endpoints (CSRF defense). The scheme is ignored — we only
// compare host[:port] — because the LocalOnly middleware already restricts
// the source IP and the threat model here is cross-site, not on-path.
func isOriginAllowed(r *http.Request, allowed []string) bool {
	origin := r.Header.Get("Origin")
	if origin == "" {
		return true
	}
	u, err := url.Parse(origin)
	if err != nil || u.Host == "" {
		return false
	}
	if u.Host == r.Host {
		return true
	}
	for _, a := range allowed {
		if u.Host == a {
			return true
		}
	}
	return false
}

// LoggingMiddleware logs HTTP requests
func LoggingMiddleware(next http.Handler) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		start := time.Now()

		// Wrap response writer to capture status
		rw := &responseWriter{ResponseWriter: w, status: http.StatusOK}
		next.ServeHTTP(rw, r)

		log.Printf("%s %s %d %s",
			r.Method,
			r.URL.Path,
			rw.status,
			time.Since(start).Round(time.Millisecond),
		)
	})
}

type responseWriter struct {
	http.ResponseWriter
	status int
}

func (rw *responseWriter) WriteHeader(code int) {
	rw.status = code
	rw.ResponseWriter.WriteHeader(code)
}

// Hijack lets the WebSocket upgrader take over the connection through the
// logging wrapper. Without it the upgrade fails with 500 and every client
// silently falls back to HTTP polling.
func (rw *responseWriter) Hijack() (net.Conn, *bufio.ReadWriter, error) {
	h, ok := rw.ResponseWriter.(http.Hijacker)
	if !ok {
		return nil, nil, errors.New("underlying ResponseWriter does not support hijacking")
	}
	conn, brw, err := h.Hijack()
	if err == nil {
		rw.status = http.StatusSwitchingProtocols
	}
	return conn, brw, err
}

// Unwrap exposes the wrapped writer to http.ResponseController.
func (rw *responseWriter) Unwrap() http.ResponseWriter {
	return rw.ResponseWriter
}

// SecurityHeadersMiddleware adds security headers to all responses
func SecurityHeadersMiddleware(next http.Handler) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		// Content Security Policy - restrictive default
		w.Header().Set("Content-Security-Policy",
			"default-src 'self'; "+
				"script-src 'self'; "+
				"style-src 'self' 'unsafe-inline'; "+
				"img-src 'self' data:; "+
				"connect-src 'self' ws: wss:; "+
				"font-src 'self'; "+
				"object-src 'none'; "+
				"base-uri 'self'; "+
				"form-action 'self'; "+
				"frame-ancestors 'none'")

		// Prevent MIME type sniffing
		w.Header().Set("X-Content-Type-Options", "nosniff")

		// Prevent clickjacking
		w.Header().Set("X-Frame-Options", "DENY")

		// Enable XSS filter (legacy, but doesn't hurt)
		w.Header().Set("X-XSS-Protection", "1; mode=block")

		// Referrer policy
		w.Header().Set("Referrer-Policy", "strict-origin-when-cross-origin")

		// Permissions policy - disable all sensors/APIs we don't need
		w.Header().Set("Permissions-Policy",
			"accelerometer=(), camera=(), geolocation=(), gyroscope=(), magnetometer=(), microphone=(), payment=(), usb=()")

		next.ServeHTTP(w, r)
	})
}

// LocalOnlyMiddleware restricts access to local/private IP addresses
func LocalOnlyMiddleware(next http.Handler) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		clientIP := getClientIP(r)
		if clientIP == nil {
			http.Error(w, "Forbidden: unable to determine client IP", http.StatusForbidden)
			return
		}

		if !isPrivateIP(clientIP) {
			log.Printf("Rejected non-local connection from %s", clientIP)
			http.Error(w, "Forbidden: access restricted to local network", http.StatusForbidden)
			return
		}

		next.ServeHTTP(w, r)
	})
}

// getClientIP extracts the client IP from the request
func getClientIP(r *http.Request) net.IP {
	// Get IP from RemoteAddr (most reliable for local-only use)
	host, _, err := net.SplitHostPort(r.RemoteAddr)
	if err != nil {
		// Try without port
		host = r.RemoteAddr
	}

	return net.ParseIP(host)
}

// isPrivateIP checks if an IP address is private/local
func isPrivateIP(ip net.IP) bool {
	if ip == nil {
		return false
	}

	// Allow loopback
	if ip.IsLoopback() {
		return true
	}

	// Allow link-local
	if ip.IsLinkLocalUnicast() || ip.IsLinkLocalMulticast() {
		return true
	}

	// Check private ranges
	// IPv4
	if ip4 := ip.To4(); ip4 != nil {
		// 10.0.0.0/8
		if ip4[0] == 10 {
			return true
		}
		// 172.16.0.0/12
		if ip4[0] == 172 && ip4[1] >= 16 && ip4[1] <= 31 {
			return true
		}
		// 192.168.0.0/16
		if ip4[0] == 192 && ip4[1] == 168 {
			return true
		}
		// 127.0.0.0/8 (additional loopback check)
		if ip4[0] == 127 {
			return true
		}
	}

	// IPv6 private ranges (fd00::/8, fe80::/10)
	if ip6 := ip.To16(); ip6 != nil && ip.To4() == nil {
		// ULA (fd00::/8)
		if ip6[0] == 0xfd {
			return true
		}
		// Link-local (fe80::/10)
		if ip6[0] == 0xfe && (ip6[1]&0xc0) == 0x80 {
			return true
		}
	}

	// Also check with net.IP methods for completeness
	return ip.IsPrivate()
}
