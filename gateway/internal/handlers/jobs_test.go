package handlers

import (
	"bytes"
	"context"
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"testing"
	"time"

	"github.com/go-chi/chi/v5"
	"github.com/go-chi/chi/v5/middleware"

	"github.com/awesoma/gateway/internal/models"
	"github.com/awesoma/gateway/internal/service"
)

// MockJobService implements service.JobService for testing
type MockJobService struct {
	jobs  map[string]*models.Job
	errOn map[string]error
}

func NewMockJobService() *MockJobService {
	return &MockJobService{
		jobs:  make(map[string]*models.Job),
		errOn: make(map[string]error),
	}
}

func (m *MockJobService) CreateJob(ctx context.Context, job *models.Job) error {
	if err, ok := m.errOn["CreateJob"]; ok {
		return err
	}
	m.jobs[job.JobID] = job
	return nil
}

func (m *MockJobService) GetJob(ctx context.Context, jobID string) (*models.Job, error) {
	if err, ok := m.errOn["GetJob"]; ok {
		return nil, err
	}
	job, ok := m.jobs[jobID]
	if !ok {
		return nil, service.ErrJobNotFound
	}
	return job, nil
}

func (m *MockJobService) CancelJob(ctx context.Context, jobID string) error {
	if err, ok := m.errOn["CancelJob"]; ok {
		return err
	}
	if job, ok := m.jobs[jobID]; ok {
		job.State = models.StateCancelled
	}
	return nil
}

func (m *MockJobService) ListJobsByUser(ctx context.Context, userID string, limit, offset int) ([]*models.Job, error) {
	if err, ok := m.errOn["ListJobsByUser"]; ok {
		return nil, err
	}
	var jobs []*models.Job
	for _, job := range m.jobs {
		if job.UserID == userID {
			jobs = append(jobs, job)
		}
	}
	return jobs, nil
}

// Custom error for mock
var ErrJobNotFound = &JobNotFoundError{}

type JobNotFoundError struct{}

func (e *JobNotFoundError) Error() string {
	return "job not found"
}

func setupTestRouter() *chi.Mux {
	r := chi.NewRouter()
	r.Use(middleware.SetHeader("Content-Type", "application/json"))
	return r
}

func TestCreateJob_Success(t *testing.T) {
	mockSvc := NewMockJobService()
	handler := NewJobsHandler(mockSvc, "http://localhost:8080")

	router := setupTestRouter()
	router.Post("/v1/jobs", handler.CreateJob)

	// Create request
	reqBody := map[string]interface{}{
		"binary_b64":      "SGVsbG8gV29ybGQ=", // "Hello World" in base64
		"debug":           false,
		"timeout_seconds": 30,
	}
	body, _ := json.Marshal(reqBody)

	req := httptest.NewRequest("POST", "/v1/jobs", bytes.NewReader(body))
	req.Header.Set("Content-Type", "application/json")
	req.Header.Set("X-API-Key", "test-api-key")

	// Set user_id in context
	req = req.WithContext(context.WithValue(req.Context(), "user_id", "user-1"))

	rec := httptest.NewRecorder()
	router.ServeHTTP(rec, req)

	if rec.Code != http.StatusAccepted {
		t.Errorf("expected status %d, got %d", http.StatusAccepted, rec.Code)
	}

	var resp map[string]interface{}
	json.Unmarshal(rec.Body.Bytes(), &resp)

	if resp["job_id"] == "" {
		t.Error("expected job_id in response")
	}

	if resp["sha256"] == "" {
		t.Error("expected sha256 in response")
	}
}

func TestCreateJob_MissingBinary(t *testing.T) {
	mockSvc := NewMockJobService()
	handler := NewJobsHandler(mockSvc, "http://localhost:8080")

	router := setupTestRouter()
	router.Post("/v1/jobs", handler.CreateJob)

	reqBody := map[string]interface{}{
		"debug": false,
	}
	body, _ := json.Marshal(reqBody)

	req := httptest.NewRequest("POST", "/v1/jobs", bytes.NewReader(body))
	req.Header.Set("Content-Type", "application/json")
	req = req.WithContext(context.WithValue(req.Context(), "user_id", "user-1"))

	rec := httptest.NewRecorder()
	router.ServeHTTP(rec, req)

	if rec.Code != http.StatusBadRequest {
		t.Errorf("expected status %d, got %d", http.StatusBadRequest, rec.Code)
	}
}

func TestCreateJob_InvalidBase64(t *testing.T) {
	mockSvc := NewMockJobService()
	handler := NewJobsHandler(mockSvc, "http://localhost:8080")

	router := setupTestRouter()
	router.Post("/v1/jobs", handler.CreateJob)

	reqBody := map[string]interface{}{
		"binary_b64": "!!!invalid-base64!!!",
	}
	body, _ := json.Marshal(reqBody)

	req := httptest.NewRequest("POST", "/v1/jobs", bytes.NewReader(body))
	req.Header.Set("Content-Type", "application/json")
	req = req.WithContext(context.WithValue(req.Context(), "user_id", "user-1"))

	rec := httptest.NewRecorder()
	router.ServeHTTP(rec, req)

	if rec.Code != http.StatusBadRequest {
		t.Errorf("expected status %d, got %d", http.StatusBadRequest, rec.Code)
	}
}

func TestGetJob_Success(t *testing.T) {
	mockSvc := NewMockJobService()
	mockSvc.jobs["test-job-1"] = &models.Job{
		JobID:     "test-job-1",
		UserID:    "user-1",
		SHA256:    "abc123",
		State:     models.StateRunning,
		CreatedAt: time.Now(),
	}

	handler := NewJobsHandler(mockSvc, "http://localhost:8080")

	router := setupTestRouter()
	router.Get("/v1/jobs/{job_id}", handler.GetJob)

	req := httptest.NewRequest("GET", "/v1/jobs/test-job-1", nil)
	req = req.WithContext(context.WithValue(req.Context(), "user_id", "user-1"))

	// Set chi route params
	rctx := chi.NewRouteContext()
	rctx.URLParams.Add("job_id", "test-job-1")
	req = req.WithContext(context.WithValue(req.Context(), chi.RouteCtxKey, rctx))

	rec := httptest.NewRecorder()
	router.ServeHTTP(rec, req)

	if rec.Code != http.StatusOK {
		t.Errorf("expected status %d, got %d: %s", http.StatusOK, rec.Code, rec.Body.String())
	}

	var resp map[string]interface{}
	json.Unmarshal(rec.Body.Bytes(), &resp)

	if resp["job_id"] != "test-job-1" {
		t.Errorf("expected job_id test-job-1, got %v", resp["job_id"])
	}
}

func TestGetJob_NotFound(t *testing.T) {
	mockSvc := NewMockJobService()
	handler := NewJobsHandler(mockSvc, "http://localhost:8080")

	router := setupTestRouter()
	router.Get("/v1/jobs/{job_id}", handler.GetJob)

	req := httptest.NewRequest("GET", "/v1/jobs/non-existent", nil)
	req = req.WithContext(context.WithValue(req.Context(), "user_id", "user-1"))

	rctx := chi.NewRouteContext()
	rctx.URLParams.Add("job_id", "non-existent")
	req = req.WithContext(context.WithValue(req.Context(), chi.RouteCtxKey, rctx))

	rec := httptest.NewRecorder()
	router.ServeHTTP(rec, req)

	if rec.Code != http.StatusNotFound {
		t.Errorf("expected status %d, got %d", http.StatusNotFound, rec.Code)
	}
}

func TestGetJob_WrongUser(t *testing.T) {
	mockSvc := NewMockJobService()
	mockSvc.jobs["test-job-2"] = &models.Job{
		JobID:     "test-job-2",
		UserID:    "user-1",
		SHA256:    "abc123",
		State:     models.StateRunning,
		CreatedAt: time.Now(),
	}

	handler := NewJobsHandler(mockSvc, "http://localhost:8080")

	router := setupTestRouter()
	router.Get("/v1/jobs/{job_id}", handler.GetJob)

	req := httptest.NewRequest("GET", "/v1/jobs/test-job-2", nil)
	req = req.WithContext(context.WithValue(req.Context(), "user_id", "user-2")) // Different user

	rctx := chi.NewRouteContext()
	rctx.URLParams.Add("job_id", "test-job-2")
	req = req.WithContext(context.WithValue(req.Context(), chi.RouteCtxKey, rctx))

	rec := httptest.NewRecorder()
	router.ServeHTTP(rec, req)

	if rec.Code != http.StatusForbidden {
		t.Errorf("expected status %d, got %d", http.StatusForbidden, rec.Code)
	}
}

func TestCancelJob_Success(t *testing.T) {
	mockSvc := NewMockJobService()
	mockSvc.jobs["test-job-3"] = &models.Job{
		JobID:     "test-job-3",
		UserID:    "user-1",
		SHA256:    "abc123",
		State:     models.StateRunning,
		CreatedAt: time.Now(),
	}

	handler := NewJobsHandler(mockSvc, "http://localhost:8080")

	router := setupTestRouter()
	router.Delete("/v1/jobs/{job_id}", handler.CancelJob)

	req := httptest.NewRequest("DELETE", "/v1/jobs/test-job-3", nil)
	req = req.WithContext(context.WithValue(req.Context(), "user_id", "user-1"))

	rctx := chi.NewRouteContext()
	rctx.URLParams.Add("job_id", "test-job-3")
	req = req.WithContext(context.WithValue(req.Context(), chi.RouteCtxKey, rctx))

	rec := httptest.NewRecorder()
	router.ServeHTTP(rec, req)

	if rec.Code != http.StatusOK {
		t.Errorf("expected status %d, got %d", http.StatusOK, rec.Code)
	}

	// Verify job was cancelled
	if mockSvc.jobs["test-job-3"].State != models.StateCancelled {
		t.Error("expected job to be cancelled")
	}
}

func TestListJobs(t *testing.T) {
	mockSvc := NewMockJobService()
	mockSvc.jobs["job-1"] = &models.Job{JobID: "job-1", UserID: "user-1", State: models.StateCompleted}
	mockSvc.jobs["job-2"] = &models.Job{JobID: "job-2", UserID: "user-1", State: models.StateRunning}
	mockSvc.jobs["job-3"] = &models.Job{JobID: "job-3", UserID: "user-2", State: models.StateQueued}

	handler := NewJobsHandler(mockSvc, "http://localhost:8080")

	router := setupTestRouter()
	router.Get("/v1/jobs", handler.ListJobs)

	req := httptest.NewRequest("GET", "/v1/jobs", nil)
	req = req.WithContext(context.WithValue(req.Context(), "user_id", "user-1"))

	rec := httptest.NewRecorder()
	router.ServeHTTP(rec, req)

	if rec.Code != http.StatusOK {
		t.Errorf("expected status %d, got %d", http.StatusOK, rec.Code)
	}

	var resp map[string]interface{}
	json.Unmarshal(rec.Body.Bytes(), &resp)

	jobs, ok := resp["jobs"].([]interface{})
	if !ok {
		t.Fatal("expected jobs array in response")
	}

	// Should only return user-1's jobs
	if len(jobs) != 2 {
		t.Errorf("expected 2 jobs, got %d", len(jobs))
	}
}
