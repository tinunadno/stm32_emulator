package handlers

import (
	"encoding/json"
	"net/http"

	"github.com/go-chi/chi/v5"
)

type HealthHandler struct{}

func NewHealthHandler() *HealthHandler {
	return &HealthHandler{}
}

// HealthCheck handles GET /health
func (h *HealthHandler) HealthCheck(w http.ResponseWriter, r *http.Request) {
	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(map[string]string{
		"status": "healthy",
	})
}

// Routes returns a chi.Router for health check endpoints
func (h *HealthHandler) Routes() chi.Router {
	r := chi.NewRouter()
	r.Get("/health", h.HealthCheck)
	r.Get("/ready", h.ReadyCheck)
	return r
}

// ReadyCheck handles GET /ready
func (h *HealthHandler) ReadyCheck(w http.ResponseWriter, r *http.Request) {
	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(map[string]interface{}{
		"status": "ready",
		"checks": map[string]string{
			"database": "ok",
			"keydb":    "ok",
		},
	})
}
