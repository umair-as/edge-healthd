// SPDX-License-Identifier: MIT

package main

import (
	"log"
	"net"
	"net/http"
	"strings"
	"time"
)

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

// isPrivateIPString is a helper that takes a string IP
func isPrivateIPString(ipStr string) bool {
	// Strip port if present
	if idx := strings.LastIndex(ipStr, ":"); idx != -1 {
		// Check if this looks like IPv6
		if strings.Count(ipStr, ":") > 1 {
			// IPv6 with brackets
			if strings.HasPrefix(ipStr, "[") {
				ipStr = strings.TrimPrefix(ipStr, "[")
				if idx := strings.Index(ipStr, "]"); idx != -1 {
					ipStr = ipStr[:idx]
				}
			}
		} else {
			// IPv4 with port
			ipStr = ipStr[:idx]
		}
	}

	ip := net.ParseIP(ipStr)
	return isPrivateIP(ip)
}
