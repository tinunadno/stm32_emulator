package handlers

import (
	"encoding/json"
	"fmt"
	"net/http"

	"github.com/go-chi/chi/v5"

	"github.com/awesoma/stm32-sim/gateway/internal/models"
	"github.com/awesoma/stm32-sim/gateway/internal/service"
)

type JobsHandler struct {
	jobService *service.JobService
}

func NewJobsHandler(jobService *service.JobService) *JobsHandler {
	return &JobsHandler{jobService: jobService}
}

// CreateJob handles POST /v1/jobs
func (h *JobsHandler) CreateJob(w http.ResponseWriter, r *http.Request) {
	// Get user ID from header (set by auth middleware)
	userID := r.Header.Get("X-User-ID")
	if userID == "" {
		userID = "anonymous"
	}

	var req models.CreateJobRequest
	if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
		writeErrorResponse(w, http.StatusBadRequest, fmt.Sprintf("invalid request body: %s", err.Error()))
		return
	}

	if req.BinaryB64 == "" {
		writeErrorResponse(w, http.StatusBadRequest, "binary_b64 is required")
		return
	}

	resp, err := h.jobService.CreateJob(r.Context(), userID, &req)
	if err != nil {
		writeErrorResponse(w, http.StatusInternalServerError, err.Error())
		return
	}

	w.Header().Set("Location", resp.StatusURL)
	w.WriteHeader(http.StatusAccepted)
	json.NewEncoder(w).Encode(resp)
}

// GetJob handles GET /v1/jobs/{jobID}
func (h *JobsHandler) GetJob(w http.ResponseWriter, r *http.Request) {
	jobID := chi.URLParam(r, "jobID")

	job, err := h.jobService.GetJob(r.Context(), jobID)
	if err != nil {
		if err == service.ErrJobNotFound {
			writeErrorResponse(w, http.StatusNotFound, "job not found")
			return
		}
		writeErrorResponse(w, http.StatusInternalServerError, err.Error())
		return
	}

	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(job)
}

// GetGDBInfo handles GET /v1/jobs/{jobID}/gdb-info
func (h *JobsHandler) GetGDBInfo(w http.ResponseWriter, r *http.Request) {
	jobID := chi.URLParam(r, "jobID")

	info, err := h.jobService.GetGDBInfo(r.Context(), jobID)
	if err != nil {
		if err == service.ErrJobNotFound {
			writeErrorResponse(w, http.StatusNotFound, "job not found")
			return
		}
		writeErrorResponse(w, http.StatusInternalServerError, err.Error())
		return
	}

	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(info)
}

// CancelJob handles DELETE /v1/jobs/{jobID}
func (h *JobsHandler) CancelJob(w http.ResponseWriter, r *http.Request) {
	jobID := chi.URLParam(r, "jobID")

	if err := h.jobService.CancelJob(r.Context(), jobID); err != nil {
		if err == service.ErrJobNotFound {
			writeErrorResponse(w, http.StatusNotFound, "job not found")
			return
		}
		writeErrorResponse(w, http.StatusBadRequest, err.Error())
		return
	}

	w.WriteHeader(http.StatusNoContent)
}

// writeErrorResponse writes an error response
func writeErrorResponse(w http.ResponseWriter, code int, message string) {
	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(code)
	json.NewEncoder(w).Encode(map[string]string{"error": message})
}

// Routes returns a chi.Router for job endpoints
func (h *JobsHandler) Routes() chi.Router {
	r := chi.NewRouter()
	r.Post("/", h.CreateJob)
	r.Get("/{jobID}", h.GetJob)
	r.Get("/{jobID}/gdb-info", h.GetGDBInfo)
	r.Delete("/{jobID}", h.CancelJob)
	return r
}
