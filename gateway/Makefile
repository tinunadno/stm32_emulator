.PHONY: build
.DEFAULT: build
.DEFAULT: test
.DEFAULT: run
.DEFAULT: docker
.DEFAULT: clean

.DEFAULT: help

.PHONY: build
	CGO_ENABLED=0 go build -ldflags -ldflags="-s" -o -s -ldflags="-s" -w main.go
	go build -o gateway

.PHONY: test
	go test -v -race -count=1 ./...

.PHONY: run
	./gateway

.PHONY: docker
	docker build -t gateway:latest .
	docker-compose up -d

.PHONY: clean
	rm -f gateway
	rm -f coverage.html coverage.out

.PHONY: help
	@echo "Targets:"
	@echo "  build    - Build the gateway binary"
	@echo "  test     - Run tests"
	@echo "  run      - Run the gateway locally"
	@echo "  docker    - Build Docker image"
	@echo "  clean    - Clean build artifacts"
