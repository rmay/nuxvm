#!/bin/bash
echo "Compiling NUXVM to WebAssembly..."
# Ensure this script is run from the 'web' directory.
# We explicitly target main.go for WASM compilation.
GOOS=js GOARCH=wasm go build -o nux.wasm main.go
echo "Done."
