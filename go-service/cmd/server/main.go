package main

import (
	"context"
	"log/slog"
	"os"
	"os/signal"
	"strconv"
	"syscall"
	"time"

	"stm32sim-service/internal/queue"
	"stm32sim-service/internal/simulator"
	"stm32sim-service/internal/telemetry"
	"stm32sim-service/internal/worker"
)

func getenv(key, fallback string) string {
	if v := os.Getenv(key); v != "" {
		return v
	}
	return fallback
}

func getenvInt(key string, fallback int) int {
	if v := os.Getenv(key); v != "" {
		if n, err := strconv.Atoi(v); err == nil {
			return n
		}
	}
	return fallback
}

func getenvUint64(key string, fallback uint64) uint64 {
	if v := os.Getenv(key); v != "" {
		if n, err := strconv.ParseUint(v, 10, 64); err == nil {
			return n
		}
	}
	return fallback
}

func main() {
	slog.SetDefault(slog.New(slog.NewJSONHandler(os.Stdout, nil)))

	keydbAddr   := getenv("KEYDB_ADDR", "localhost:6379")
	workerCount := getenvInt("WORKER_COUNT", 4)
	simBinary   := getenv("SIM_BINARY", "./stm32sim")
	maxCycles   := getenvUint64("SIM_MAX_CYCLES", 10_000_000)
	timeoutSec  := getenvInt("SIM_TIMEOUT_SEC", 30)
	otelEndpoint := getenv("OTEL_EXPORTER_OTLP_ENDPOINT", "")

	ctx, stop := signal.NotifyContext(context.Background(), syscall.SIGTERM, syscall.SIGINT)
	defer stop()

	// --- Telemetry ---
	var tel *telemetry.Provider
	if otelEndpoint != "" {
		var err error
		tel, err = telemetry.Init(ctx, otelEndpoint)
		if err != nil {
			slog.Error("failed to init telemetry", "err", err)
			os.Exit(1)
		}
		defer tel.Shutdown(context.Background())
		slog.Info("OpenTelemetry enabled", "endpoint", otelEndpoint)
	} else {
		slog.Warn("OTEL_EXPORTER_OTLP_ENDPOINT not set — telemetry disabled (using no-op provider)")
		tel = &telemetry.Provider{}
		if err := telemetry.InitNoOp(tel); err != nil {
			slog.Error("failed to init no-op telemetry", "err", err)
			os.Exit(1)
		}
	}

	// --- KeyDB ---
	q := queue.New(keydbAddr)
	if err := q.Ping(ctx); err != nil {
		slog.Error("cannot connect to KeyDB", "addr", keydbAddr, "err", err)
		os.Exit(1)
	}
	slog.Info("KeyDB connected", "addr", keydbAddr)

	// --- Simulator config ---
	simCfg := simulator.Config{
		BinaryPath: simBinary,
		MaxCycles:  maxCycles,
		Timeout:    time.Duration(timeoutSec) * time.Second,
	}

	// --- Worker pool ---
	pool := worker.NewPool(workerCount, q, simCfg, tel)
	pool.Start(ctx)
	slog.Info("worker pool started", "workers", workerCount)

	// Block until signal
	<-ctx.Done()
	slog.Info("shutdown signal received, draining workers...")
	pool.Shutdown()
	slog.Info("shutdown complete")
}
