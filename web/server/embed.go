// SPDX-License-Identifier: MIT

package main

import (
	"embed"
	"io/fs"
	"log"
)

//go:embed dist/*
var distFS embed.FS

// staticFS is the filesystem for serving static files
var staticFS fs.FS

func init() {
	var err error
	staticFS, err = fs.Sub(distFS, "dist")
	if err != nil {
		log.Fatalf("Failed to create static filesystem: %v", err)
	}
}
