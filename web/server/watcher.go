// SPDX-License-Identifier: MIT

package main

import (
	"context"
	"log"
	"os"
	"path/filepath"
	"time"

	"github.com/fsnotify/fsnotify"
)

const (
	// Debounce duration for file changes
	debounceDuration = 100 * time.Millisecond
)

// StateWatcher watches the state file for changes
type StateWatcher struct {
	cfg     *Config
	cache   *StateCache
	hub     *WebSocketHub
	watcher *fsnotify.Watcher
}

// NewStateWatcher creates a new state watcher
func NewStateWatcher(cfg *Config, cache *StateCache, hub *WebSocketHub) (*StateWatcher, error) {
	watcher, err := fsnotify.NewWatcher()
	if err != nil {
		return nil, err
	}

	sw := &StateWatcher{
		cfg:     cfg,
		cache:   cache,
		hub:     hub,
		watcher: watcher,
	}

	return sw, nil
}

// Watch starts watching the state file directory
func (sw *StateWatcher) Watch(ctx context.Context) {
	defer sw.watcher.Close()

	// Read initial state
	sw.loadState()

	// Add directory to watcher (watching file directly can miss renames)
	dir := sw.cfg.StateDir()
	if err := sw.watcher.Add(dir); err != nil {
		log.Printf("Failed to watch directory %s: %v", dir, err)
		// Continue with polling fallback
		sw.pollLoop(ctx)
		return
	}

	log.Printf("Watching directory: %s", dir)

	// Debounce timer
	var debounceTimer *time.Timer
	filename := sw.cfg.StateFileName()

	for {
		select {
		case <-ctx.Done():
			if debounceTimer != nil {
				debounceTimer.Stop()
			}
			return

		case event, ok := <-sw.watcher.Events:
			if !ok {
				return
			}

			// Only care about our specific file
			if filepath.Base(event.Name) != filename {
				continue
			}

			// Check for write or create events
			if event.Op&(fsnotify.Write|fsnotify.Create) == 0 {
				continue
			}

			// Debounce rapid changes
			if debounceTimer != nil {
				debounceTimer.Stop()
			}
			debounceTimer = time.AfterFunc(debounceDuration, func() {
				sw.loadState()
			})

		case err, ok := <-sw.watcher.Errors:
			if !ok {
				return
			}
			log.Printf("Watcher error: %v", err)
		}
	}
}

// pollLoop is a fallback polling mechanism
func (sw *StateWatcher) pollLoop(ctx context.Context) {
	ticker := time.NewTicker(5 * time.Second)
	defer ticker.Stop()

	var lastMtime time.Time

	for {
		select {
		case <-ctx.Done():
			return
		case <-ticker.C:
			info, err := os.Stat(sw.cfg.StateFile)
			if err != nil {
				continue
			}

			if info.ModTime().After(lastMtime) {
				lastMtime = info.ModTime()
				sw.loadState()
			}
		}
	}
}

// loadState reads the state file and updates the cache
func (sw *StateWatcher) loadState() {
	data, err := os.ReadFile(sw.cfg.StateFile)
	if err != nil {
		if !os.IsNotExist(err) {
			log.Printf("Failed to read state file: %v", err)
		}
		return
	}

	info, err := os.Stat(sw.cfg.StateFile)
	if err != nil {
		log.Printf("Failed to stat state file: %v", err)
		return
	}

	// Update cache
	sw.cache.Set(data, info.ModTime())

	// Broadcast to WebSocket clients
	sw.hub.Broadcast(data)

	log.Printf("State updated (size: %d bytes)", len(data))
}
