//go:build !wasm

package main

import (
	"fmt"
	"net/http"
	"os"
)

func main() {
	port := "8080"
	if len(os.Args) > 1 {
		port = os.Args[1]
	}

	dir := "web"
	fmt.Printf("Serving NUXVM showcase at http://localhost:%s", port)

	fs := http.FileServer(http.Dir(dir))
	// Custom handler to ensure correct MIME type for WASM files.
	http.HandleFunc("/", func(w http.ResponseWriter, r *http.Request) {
		if r.URL.Path == "/nux.wasm" {
			w.Header().Set("Content-Type", "application/wasm")
		}
		fs.ServeHTTP(w, r)
	})

	err := http.ListenAndServe(":"+port, nil)
	if err != nil {
		fmt.Printf("Error starting server: %v", err)
		os.Exit(1)
	}
}
