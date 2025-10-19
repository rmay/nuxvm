.PHONY: build test clean install run examples

# Build the nux binary
vmbuild:
	go build -o nux ./cmd/nux

# Run tests
vmtest:
	cd pkg/vm && go test -v

# Run tests with coverage
vmcoverage:
	cd pkg/vm && go test -coverprofile=coverage.out
	cd pkg/vm && go tool cover -html=coverage.out

# Clean build artifacts
vmclean:
	rm -f nux
	rm -f pkg/vm/coverage.out

# Install nux to $GOPATH/bin
vminstall:
	go install ./cmd/nux

# Run examples
vmexamples:
	cd examples && go run examples.go

buildall:
	go build -o nux ./cmd/nux
	go build -o luxc cmd/luxc/main.go
	go build -o luxrepl cmd/luxrepl/main.go

luxbuild:
	go build -o luxc cmd/luxc/main.go

# Run compiler tests
compilertest:
	cd pkg/lux && go test -v

replbuild:
	go build -o luxrepl cmd/luxrepl/main.go

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
	@echo "  vmbuild     - Build the nux binary"
	@echo "  vmtest      - Run tests"
	@echo "  vmcoverage  - Run tests with coverage report"
	@echo "  vmclean     - Remove build artifacts"
	@echo "  vminstall   - Install nux to GOPATH/bin"
	@echo "  vmexamples  - Run example programs"
	@echo "  compilertest - Run compiler tests"
	@echo "  fmt       - Format code"
	@echo "  lint      - Lint code"
	@echo "  check     - Run fmt and test"
