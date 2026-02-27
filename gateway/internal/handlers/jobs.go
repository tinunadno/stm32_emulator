package handlers

import (
	"crypto/sha256"
	"encoding/base64"
	"encoding/hex"
	"encoding/json"
	"errors"
	"net/http"
	"time"

	"github.com/go-chi/chi/v5"
	"github.com/go-chi/render"
	"github.com/google/uuid"

	"github.com/awesoma/gateway/internal/models"
	"github.com/awesoma/gateway/internal/service"
)

// JobsHandler handles job-related HTTP requests
type JobsHandler struct {
	jobService service.JobService
	baseURL    string
}

// NewJobsHandler creates a new jobs handler
func NewJobsHandler(jobService service.JobService, baseURL string) *JobsHandler {
	return &JobsHandler{
		jobService: jobService,
		baseURL:    baseURL,
	}
}

// CreateJob handles POST /v1/jobs
func (h *JobsHandler) CreateJob(w http.ResponseWriter, r *http.Request) {
	var req models.CreateJobRequest
	if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
		render.Render(w, r, ErrBadRequest(err, "Invalid request body"))
		return
	}

	// Validate request
	if req.BinaryB64 == "" {
		render.Render(w, r, ErrBadRequest(errors.New("binary_b64 is required"), "Missing required field"))
		return
	}

	// Decode base64 binary
	binaryData, err := base64.StdEncoding.DecodeString(req.BinaryB64)
	if err != nil {
		render.Render(w, r, ErrBadRequest(err, "Invalid base64 encoding"))
		return
	}

	// Calculate SHA-256 hash
	hash := sha256.Sum256(binaryData)
	sha256Hex := hex.EncodeToString(hash[:])

	// Set default timeout
	if req.TimeoutSeconds == 0 {
		req.TimeoutSeconds = 30
	}

	// Validate timeout range
	if req.TimeoutSeconds < 5 || req.TimeoutSeconds > 300 {
		req.TimeoutSeconds = 30
	}

	// Get user ID from context (set by auth middleware)
	userID := getUserID(r)

	// Create job
	job := &models.Job{
		JobID:          generateJobID(),
		UserID:         userID,
		SHA256:         sha256Hex,
		State:          models.StateQueued,
		CreatedAt:      time.Now(),
		TimeoutSeconds: req.TimeoutSeconds,
		DebugMode:      req.Debug,
	}

	// Save job
	if err := h.jobService.CreateJob(r.Context(), job); err != nil {
		render.Render(w, r, ErrInternal(err))
		return
	}

	// Build response
	resp := &models.CreateJobResponse{
		JobID:     job.JobID,
		SHA256:    sha256Hex,
		Debug:     req.Debug,
		StatusURL: h.baseURL + "/v1/jobs/" + job.JobID,
		EventsURL: h.baseURL + "/v1/jobs/" + job.JobID + "/events",
	}

	render.Status(r, http.StatusAccepted)
	render.Render(w, r, &JobCreationResponse{resp})
}

// GetJob handles GET /v1/jobs/{job_id}
func (h *JobsHandler) GetJob(w http.ResponseWriter, r *http.Request) {
	jobID := chi.URLParam(r, "job_id")
	if jobID == "" {
		render.Render(w, r, ErrBadRequest(errors.New("job_id required"), "Missing job_id"))
		return
	}

	job, err := h.jobService.GetJob(r.Context(), jobID)
	if err != nil {
		if errors.Is(err, service.ErrJobNotFound) {
			render.Render(w, r, ErrNotFound)
			return
		}
		render.Render(w, r, ErrInternal(err))
		return
	}

	// Check ownership
	userID := getUserID(r)
	if job.UserID != userID {
		render.Render(w, r, ErrForbidden)
		return
	}

	resp := &models.JobStatusResponse{
		JobID:      job.JobID,
		State:      job.State,
		WorkerID:   job.WorkerID,
		CreatedAt:  job.CreatedAt,
		StartedAt:  job.StartedAt,
		FinishedAt: job.FinishedAt,
		DebugMode:  job.DebugMode,
		ErrorText:  job.ErrorText,
	}

	render.Render(w, r, &JobStatusResponseWrapper{resp})
}

// GetGDBInfo handles GET /v1/jobs/{job_id}/gdb-info
func (h *JobsHandler) GetGDBInfo(w http.ResponseWriter, r *http.Request) {
	jobID := chi.URLParam(r, "job_id")
	if jobID == "" {
		render.Render(w, r, ErrBadRequest(errors.New("job_id required"), "Missing job_id"))
		return
	}

	job, err := h.jobService.GetJob(r.Context(), jobID)
	if err != nil {
		if errors.Is(err, service.ErrJobNotFound) {
			render.Render(w, r, ErrNotFound)
			return
		}
		render.Render(w, r, ErrInternal(err))
		return
	}

	// Check ownership
	userID := getUserID(r)
	if job.UserID != userID {
		render.Render(w, r, ErrForbidden)
		return
	}

	resp := &models.GDBInfoResponse{
		JobID:        job.JobID,
		DebugEnabled: job.DebugMode,
		GDBHost:      job.GDBHost,
		GDBPort:      job.GDBPort,
		Connected:    job.GDBConnected,
	}

	// Build connection string if GDB info is available
	if job.GDBHost != nil && job.GDBPort != nil {
		resp.ConnectionString = "target remote " + *job.GDBHost + ":" + string(rune(*job.GDBPort))
	}

	// Determine status
	if !job.DebugMode {
		resp.Status = "disabled"
	} else if job.State != models.StateRunning {
		resp.Status = string(job.State)
	} else if job.GDBConnected {
		resp.Status = "connected"
	} else {
		resp.Status = "listening"
	}

	render.Render(w, r, &GDBInfoResponseWrapper{resp})
}

// CancelJob handles DELETE /v1/jobs/{job_id}
func (h *JobsHandler) CancelJob(w http.ResponseWriter, r *http.Request) {
	jobID := chi.URLParam(r, "job_id")
	if jobID == "" {
		render.Render(w, r, ErrBadRequest(errors.New("job_id required"), "Missing job_id"))
		return
	}

	// Check job exists and user owns it
	job, err := h.jobService.GetJob(r.Context(), jobID)
	if err != nil {
		if errors.Is(err, service.ErrJobNotFound) {
			render.Render(w, r, ErrNotFound)
			return
		}
		render.Render(w, r, ErrInternal(err))
		return
	}

	userID := getUserID(r)
	if job.UserID != userID {
		render.Render(w, r, ErrForbidden)
		return
	}

	// Cancel the job
	if err := h.jobService.CancelJob(r.Context(), jobID); err != nil {
		render.Render(w, r, ErrInternal(err))
		return
	}

	resp := &models.CancelJobResponse{
		JobID:     jobID,
		Cancelled: true,
		Message:   "Job cancelled successfully",
	}

	render.Render(w, r, &CancelJobResponseWrapper{resp})
}

// ListJobs handles GET /v1/jobs
func (h *JobsHandler) ListJobs(w http.ResponseWriter, r *http.Request) {
	userID := getUserID(r)

	jobs, err := h.jobService.ListJobsByUser(r.Context(), userID, 50, 0)
	if err != nil {
		render.Render(w, r, ErrInternal(err))
		return
	}

	render.Render(w, r, &JobListResponse{Jobs: jobs})
}

// Helper functions
func generateJobID() string {
	// Generate ULID-like ID
	return uuid.New().String()[:26]
}

func getUserID(r *http.Request) string {
	if userID := r.Context().Value("user_id"); userID != nil {
		return userID.(string)
	}
	return "anonymous"
}

// Response types for render
type JobCreationResponse struct {
	*models.CreateJobResponse
}

func (resp *JobCreationResponse) Render(w http.ResponseWriter, r *http.Request) error {
	return nil
}

type JobStatusResponseWrapper struct {
	*models.JobStatusResponse
}

func (resp *JobStatusResponseWrapper) Render(w http.ResponseWriter, r *http.Request) error {
	return nil
}

type GDBInfoResponseWrapper struct {
	*models.GDBInfoResponse
}

func (resp *GDBInfoResponseWrapper) Render(w http.ResponseWriter, r *http.Request) error {
	return nil
}

type CancelJobResponseWrapper struct {
	*models.CancelJobResponse
}

func (resp *CancelJobResponseWrapper) Render(w http.ResponseWriter, r *http.Request) error {
	return nil
}

type JobListResponse struct {
	Jobs []*models.Job `json:"jobs"`
}

func (resp *JobListResponse) Render(w http.ResponseWriter, r *http.Request) error {
	return nil
}

// Error responses
type ErrResponse struct {
	Err            error `json:"-"`
	HTTPStatusCode int   `json:"-"`

	StatusText string `json:"status"`
	AppCode    int    `json:"code,omitempty"`
	ErrorText  string `json:"error,omitempty"`
}

func (e *ErrResponse) Render(w http.ResponseWriter, r *http.Request) error {
	render.Status(r, e.HTTPStatusCode)
	return nil
}

func ErrInvalidRequest(err error) render.Renderer {
	return &ErrResponse{
		Err:            err,
		HTTPStatusCode: http.StatusBadRequest,
		StatusText:     "Invalid request",
		ErrorText:      err.Error(),
	}
}

func ErrBadRequest(err error, message string) render.Renderer {
	return &ErrResponse{
		Err:            err,
		HTTPStatusCode: http.StatusBadRequest,
		StatusText:     message,
		ErrorText:      err.Error(),
	}
}

func ErrInternal(err error) render.Renderer {
	return &ErrResponse{
		Err:            err,
		HTTPStatusCode: http.StatusInternalServerError,
		StatusText:     "Internal server error",
		ErrorText:      err.Error(),
	}
}

var (
	ErrNotFound = &ErrResponse{
		HTTPStatusCode: http.StatusNotFound,
		StatusText:     "Resource not found",
	}
	ErrForbidden = &ErrResponse{
		HTTPStatusCode: http.StatusForbidden,
		StatusText:     "Access forbidden",
	}
	ErrUnauthorized = &ErrResponse{
		HTTPStatusCode: http.StatusUnauthorized,
		StatusText:     "Unauthorized",
	}
)
