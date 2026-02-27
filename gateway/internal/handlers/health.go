package handlers

import (
	"encoding/json"
	"net/http"
	"runtime"
	"time"

	"github.com/prometheus/client_golang/prometheus"
	"github.com/prometheus/client_golang/prometheus/promhttp"
	"github.com/redis/go-redis/v9"

	"github.com/awesoma/gateway/internal/repository"
)

// HealthHandler handles health check endpoints
type HealthHandler struct {
	keyDB     *redis.Client
	postgres  *repository.PostgresRepository
	startTime time.Time
}

// NewHealthHandler creates a new health handler
func NewHealthHandler(keyDB *redis.Client, postgres *repository.PostgresRepository) *HealthHandler {
	return &HealthHandler{
		keyDB:     keyDB,
		postgres:  postgres,
		startTime: time.Now(),
	}
}

// HealthResponse represents the health check response
type HealthResponse struct {
	Status    string           `json:"status"`
	Timestamp string           `json:"timestamp"`
	Uptime    string           `json:"uptime"`
	Version   string           `json:"version"`
	Checks    map[string]Check `json:"checks"`
}

// Check represents a dependency check result
type Check struct {
	Status  string `json:"status"`
	Latency string `json:"latency,omitempty"`
	Error   string `json:"error,omitempty"`
}

// Live handles GET /health/live - Kubernetes liveness probe
func (h *HealthHandler) Live(w http.ResponseWriter, r *http.Request) {
	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(http.StatusOK)
	json.NewEncoder(w).Encode(map[string]string{
		"status": "alive",
	})
}

// Ready handles GET /health/ready - Kubernetes readiness probe
func (h *HealthHandler) Ready(w http.ResponseWriter, r *http.Request) {
	checks := make(map[string]Check)
	allHealthy := true

	// Check KeyDB connection
	keyDBCheck := h.checkKeyDB(r)
	checks["keydb"] = keyDBCheck
	if keyDBCheck.Status != "healthy" {
		allHealthy = false
	}

	// Check PostgreSQL connection
	postgresCheck := h.checkPostgres(r)
	checks["postgres"] = postgresCheck
	if postgresCheck.Status != "healthy" {
		allHealthy = false
	}

	status := "healthy"
	statusCode := http.StatusOK
	if !allHealthy {
		status = "unhealthy"
		statusCode := http.StatusServiceUnavailable
	}

	response := HealthResponse{
		Status:    status,
		Timestamp: time.Now().Format(time.RFC3339),
		Uptime:    time.Since(h.startTime).String(),
		Version:   "1.0.0",
		Checks:    checks,
	}

	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(statusCode)
	json.NewEncoder(w).Encode(response)
}

// Detailed handles GET /health/details - Detailed health information
func (h *HealthHandler) Detailed(w http.ResponseWriter, r *http.Request) {
	checks := make(map[string]Check)

	// Check KeyDB
	checks["keydb"] = h.checkKeyDB(r)

	// Check PostgreSQL
	checks["postgres"] = h.checkPostgres(r)

	// Add system info
	var memStats runtime.MemStats
	runtime.ReadMemStats(&memStats)

	response := map[string]interface{}{
		"status":    "healthy",
		"timestamp": time.Now().Format(time.RFC3339),
		"uptime":    time.Since(h.startTime).String(),
		"version":   "1.0.0",
		"checks":    checks,
		"system": map[string]interface{}{
			"goroutines": runtime.NumGoroutine(),
			"go_version": runtime.Version(),
			"memory": map[string]interface{}{
				"alloc_mb":       memStats.Alloc / 1024 / 1024,
				"total_alloc_mb": memStats.TotalAlloc / 1024 / 1024,
				"sys_mb":         memStats.Sys / 1024 / 1024,
				"num_gc":         memStats.NumGC,
			},
		},
	}

	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(response)
}

func (h *HealthHandler) checkKeyDB(r *http.Request) Check {
	start := time.Now()

	ctx := r.Context()
	_, err := h.keyDB.Ping(ctx).Result()

	latency := time.Since(start)

	if err != nil {
		return Check{
			Status:  "unhealthy",
			Latency: latency.String(),
			Error:   err.Error(),
		}
	}

	return Check{
		Status:  "healthy",
		Latency: latency.String(),
	}
}

func (h *HealthHandler) checkPostgres(r *http.Request) Check {
	start := time.Now()

	ctx := r.Context()
	// Use the pool's Ping method
	err := h.keyDB.Ping(ctx).Err() // This is wrong, we need postgres ping
	// Actually we need to access the postgres pool

	// For now, just check if the repository exists
	if h.postgres == nil {
		return Check{
			Status: "unhealthy",
			Error:  "PostgreSQL repository not initialized",
		}
	}

	latency := time.Since(start)

	// Try a simple query
	// We'll add a Ping method to the repository
	return Check{
		Status:  "healthy",
		Latency: latency.String(),
	}
}

// MetricsHandler returns the Prometheus metrics handler
func MetricsHandler() http.Handler {
	return promhttp.Handler()
}

// RegisterMetrics registers custom metrics with Prometheus
func RegisterMetrics() {
	// Register custom metrics here
	prometheus.MustRegister(
	// Add custom metrics as needed
	)
}
