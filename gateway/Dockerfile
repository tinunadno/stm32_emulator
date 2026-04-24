# Build stage
FROM golang:1.23-alpine AS builder

WORKDIR /app

# Copy go.mod and go.sum first for caching
COPY go.mod go.sum* ./

# Download dependencies
RUN go mod download

# Copy source files preserving directory structure
COPY cmd/ ./cmd/
COPY internal/ ./internal/
COPY pkg/ ./pkg/
COPY config.yaml ./

# Build the application
RUN CGO_ENABLED=0 go build -ldflags="-s -w" -o gateway ./cmd/gateway

# Final stage
FROM alpine:3.19

WORKDIR /app

# Install ca-certificates for HTTPS
RUN apk --no-cache add ca-certificates

# Copy binary from builder
COPY --from=builder /app/gateway /app/gateway
COPY --from=builder /app/config.yaml /app/config.yaml

# Expose ports
EXPOSE 8080 8081

# Health check
HEALTHCHECK --interval=30s --timeout=3s \
  CMD wget -q -O /dev/null http://localhost:8080/health || exit 1

# Run the application
CMD ["/app/gateway"]
