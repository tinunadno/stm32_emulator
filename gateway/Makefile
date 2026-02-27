.PHONY: build run test clean docker-build docker-up docker-down

# Binary name
BINARY_NAME=gateway

# Go parameters
GOCMD=go
GOBUILD=$(GOCMD) build
GOCLEAN=$(GOCMD) clean
GOTEST=$(GOCMD) test
GOGET=$(GOCMD) get
GOMOD=$(GOCMD) mod

# Main package
MAIN_PACKAGE=./cmd/gateway

# Build the binary
build:
	$(GOBUILD) -o bin/$(BINARY_NAME) $(MAIN_PACKAGE)

# Run the service locally
run: build
	./bin/$(BINARY_NAME)

# Download dependencies
deps:
	$(GOMOD) download
	$(GOMOD) tidy

# Run tests
test:
	$(GOTEST) -v -race ./...

# Clean build artifacts
clean:
	$(GOCLEAN)
	rm -rf bin/

# Docker build
docker-build:
	docker build -t gateway:latest .

# Run with docker-compose
docker-up:
	docker-compose up -d

# Stop docker-compose
docker-down:
	docker-compose down

# View logs
docker-logs:
	docker-compose logs -f gateway

# Development mode with hot reload (requires air)
dev:
	air

# Format code
fmt:
	$(GOCMD) fmt ./...

# Lint code (requires golangci-lint)
lint:
	golangci-lint run

# Generate swagger docs (requires swag)
swagger:
	swag init -g cmd/gateway/main.go -o ./docs

# Help
help:
	@echo "Available targets:"
	@echo "  build        - Build the binary"
	@echo "  run          - Build and run the service"
	@echo "  deps         - Download dependencies"
	@echo "  test         - Run tests"
	@echo "  clean        - Clean build artifacts"
	@echo "  docker-build - Build Docker image"
	@echo "  docker-up    - Start with docker-compose"
	@echo "  docker-down  - Stop docker-compose"
	@echo "  docker-logs  - View container logs"
	@echo "  dev          - Run in development mode"
	@echo "  fmt          - Format code"
	@echo "  lint         - Lint code"
	@echo "  swagger      - Generate swagger docs"
