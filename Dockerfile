# Stage 1: build C simulator
FROM ubuntu:24.04 AS c-builder
RUN apt-get update && apt-get install -y --no-install-recommends \
    clang make ca-certificates && rm -rf /var/lib/apt/lists/*
COPY src/ /build/src/
WORKDIR /build/src
RUN make clean && make

# Stage 2: build Go service
FROM golang:latest AS go-builder
WORKDIR /app
COPY go-service/go.mod go-service/go.sum ./
RUN go mod download
COPY go-service/ ./
RUN CGO_ENABLED=0 GOOS=linux go build -o sim-service ./cmd/server

# Stage 3: minimal runtime
FROM ubuntu:24.04
RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates && rm -rf /var/lib/apt/lists/*
COPY --from=c-builder /build/src/stm32sim /app/stm32sim
COPY --from=go-builder /app/sim-service   /app/sim-service
ENV SIM_BINARY=/app/stm32sim
ENTRYPOINT ["/app/sim-service"]
