.PHONY: all build test clean install run examples vmbuild cloisterbuild luxbuild replbuild buildall help

# Default: build the full toolchain into bin/.
all: buildall

# Build the nux binary
vmbuild:
	go build -o bin/nux ./cmd/nux

# Build the cloister graphical emulator
cloisterbuild:
	go build -o bin/cloister ./cmd/cloister

# Run tests
vmtest:
	cd pkg/vm && go test -v

# Run tests with coverage
vmcoverage:
	cd pkg/vm && go test -coverprofile=coverage.out
	cd pkg/vm && go tool cover -html=coverage.out

# Clean build artifacts
vmclean:
	rm -rf bin/
	rm -f pkg/vm/coverage.out

# Install nux to $GOPATH/bin
vminstall:
	go install ./cmd/nux

# Install cloister to $GOPATH/bin
cloisterinstall:
	go install ./cmd/cloister

# Run examples
vmexamples:
	cd examples && go run examples.go

# Build the converter
png2cff:
	go build -o bin/png2cff ./cmd/png2cff


buildall:
	mkdir -p bin
	go build -o bin/nux ./cmd/nux
	go build -o bin/cloister ./cmd/cloister
	go build -o bin/luxc ./cmd/luxc
	go build -o bin/luxrepl ./cmd/luxrepl
	go build -o bin/png2cff ./cmd/png2cff

luxbuild:
	go build -o bin/luxc cmd/luxc/main.go

# Run compiler tests
compilertest:
	cd pkg/lux && go test -v

replbuild:
	go build -o bin/luxrepl cmd/luxrepl/main.go

# Format code
fmt:
	go fmt ./...

# Lint code (requires golangci-lint)
lint:
	golangci-lint run

# Run all checks
check: fmt test

# Help
help:
	@echo "Available targets:"
	@echo "  all          - Build nux, cloister, luxc, luxrepl into bin/ (default)"
	@echo "  vmbuild      - Build the nux binary"
	@echo "  cloisterbuild - Build the CLOISTER graphical emulator"
	@echo "  vmtest       - Run tests"
	@echo "  vmcoverage   - Run tests with coverage report"
	@echo "  vmclean      - Remove build artifacts"
	@echo "  vminstall    - Install nux to GOPATH/bin"
	@echo "  cloisterinstall - Install cloister to GOPATH/bin"
	@echo "  vmexamples   - Run example programs"
	@echo "  compilertest - Run compiler tests"
	@echo "  buildall     - Build all the things"
	@echo "  fmt          - Format code"
	@echo "  lint         - Lint code"
	@echo "  check        - Run fmt and test"
