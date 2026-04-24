package tests

import (
	"bytes"
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"testing"

	"github.com/go-chi/chi/v5"

	"github.com/awesoma/stm32-sim/gateway/internal/config"
	"github.com/awesoma/stm32-sim/gateway/internal/handlers"
	"github.com/awesoma/stm32-sim/gateway/internal/models"
	"github.com/awesoma/stm32-sim/gateway/internal/repository"
	"github.com/awesoma/stm32-sim/gateway/internal/service"
)

// TestHealthEndpoint tests the health check endpoint
func TestHealthEndpoint(t *testing.T) {
	healthHandler := handlers.NewHealthHandler()

	r := chi.NewRouter()
	r.Mount("/v1/health", healthHandler.Routes())

	ts := httptest.NewServer(r)
	defer ts.Close()

	resp, err := http.Get(ts.URL + "/v1/health/health")
	if err != nil {
		t.Fatalf("Failed to make request: %v", err)
	}
	defer resp.Body.Close()

	if resp.StatusCode != http.StatusOK {
		t.Errorf("Expected status200, got %d", resp.StatusCode)
	}

	var result map[string]string
	if err := json.NewDecoder(resp.Body).Decode(&result); err != nil {
		t.Fatalf("Failed to decode response: %v", err)
	}

	if result["status"] != "healthy" {
		t.Errorf("Expected status 'healthy', got '%s'", result["status"])
	}
}

// TestReadyEndpoint tests the readiness check endpoint
func TestReadyEndpoint(t *testing.T) {
	healthHandler := handlers.NewHealthHandler()

	r := chi.NewRouter()
	r.Mount("/v1/health", healthHandler.Routes())

	ts := httptest.NewServer(r)
	defer ts.Close()

	resp, err := http.Get(ts.URL + "/v1/health/ready")
	if err != nil {
		t.Fatalf("Failed to make request: %v", err)
	}
	defer resp.Body.Close()

	if resp.StatusCode != http.StatusOK {
		t.Errorf("Expected status200, got %d", resp.StatusCode)
	}

	var result map[string]interface{}
	if err := json.NewDecoder(resp.Body).Decode(&result); err != nil {
		t.Fatalf("Failed to decode response: %v", err)
	}

	if result["status"] != "ready" {
		t.Errorf("Expected status 'ready', got '%s'", result["status"])
	}
}

// TestCreateJob tests job creation
func TestCreateJob(t *testing.T) {
	// Create mock repositories
	keydbRepo := repository.NewMockKeyDBRepository()
	pgRepo := repository.NewMockPostgresRepository()
	jobService := service.NewJobService(pgRepo, keydbRepo)
	jobsHandler := handlers.NewJobsHandler(jobService)

	r := chi.NewRouter()
	r.Mount("/v1/jobs", jobsHandler.Routes())

	ts := httptest.NewServer(r)
	defer ts.Close()

	// Create job request
	jobReq := models.CreateJobRequest{
		BinaryB64:      "dGVzdCBiaW5hcnkgZGF0YQ==", // base64 encoded "test binary data"
		Debug:          true,
		TimeoutSeconds: 300,
	}

	body, err := json.Marshal(jobReq)
	if err != nil {
		t.Fatalf("Failed to marshal request: %v", err)
	}

	resp, err := http.Post(ts.URL+"/v1/jobs/", "application/json", bytes.NewReader(body))
	if err != nil {
		t.Fatalf("Failed to make request: %v", err)
	}
	defer resp.Body.Close()

	if resp.StatusCode != http.StatusAccepted {
		t.Errorf("Expected status202, got %d", resp.StatusCode)
	}

	var jobResp models.CreateJobResponse
	if err := json.NewDecoder(resp.Body).Decode(&jobResp); err != nil {
		t.Fatalf("Failed to decode response: %v", err)
	}

	if jobResp.JobID == "" {
		t.Error("Expected non-empty job ID")
	}
}

// TestGetJob tests retrieving a job by ID
func TestGetJob(t *testing.T) {
	keydbRepo := repository.NewMockKeyDBRepository()
	pgRepo := repository.NewMockPostgresRepository()
	jobService := service.NewJobService(pgRepo, keydbRepo)
	jobsHandler := handlers.NewJobsHandler(jobService)

	r := chi.NewRouter()
	r.Mount("/v1/jobs", jobsHandler.Routes())

	ts := httptest.NewServer(r)
	defer ts.Close()

	// First create a job
	jobReq := models.CreateJobRequest{
		BinaryB64: "dGVzdCBiaW5hcnkgZGF0YQ==",
	}
	body, _ := json.Marshal(jobReq)

	createResp, err := http.Post(ts.URL+"/v1/jobs/", "application/json", bytes.NewReader(body))
	if err != nil {
		t.Fatalf("Failed to create job: %v", err)
	}
	var createdJob models.CreateJobResponse
	json.NewDecoder(createResp.Body).Decode(&createdJob)
	createResp.Body.Close()

	// Now get the job
	getResp, err := http.Get(ts.URL + "/v1/jobs/" + createdJob.JobID)
	if err != nil {
		t.Fatalf("Failed to get job: %v", err)
	}
	defer getResp.Body.Close()

	if getResp.StatusCode != http.StatusOK {
		t.Errorf("Expected status200, got %d", getResp.StatusCode)
	}

	var job models.Job
	if err := json.NewDecoder(getResp.Body).Decode(&job); err != nil {
		t.Fatalf("Failed to decode response: %v", err)
	}

	if job.JobID != createdJob.JobID {
		t.Errorf("Expected job ID '%s', got '%s'", createdJob.JobID, job.JobID)
	}
}

// TestGetNonExistentJob tests retrieving a non-existent job
func TestGetNonExistentJob(t *testing.T) {
	keydbRepo := repository.NewMockKeyDBRepository()
	pgRepo := repository.NewMockPostgresRepository()
	jobService := service.NewJobService(pgRepo, keydbRepo)
	jobsHandler := handlers.NewJobsHandler(jobService)

	r := chi.NewRouter()
	r.Mount("/v1/jobs", jobsHandler.Routes())

	ts := httptest.NewServer(r)
	defer ts.Close()

	resp, err := http.Get(ts.URL + "/v1/jobs/nonexistent-job-id")
	if err != nil {
		t.Fatalf("Failed to make request: %v", err)
	}
	defer resp.Body.Close()

	if resp.StatusCode != http.StatusInternalServerError {
		t.Errorf("Expected status500, got %d", resp.StatusCode)
	}
}

// TestCancelJob tests job cancellation
func TestCancelJob(t *testing.T) {
	keydbRepo := repository.NewMockKeyDBRepository()
	pgRepo := repository.NewMockPostgresRepository()
	jobService := service.NewJobService(pgRepo, keydbRepo)
	jobsHandler := handlers.NewJobsHandler(jobService)

	r := chi.NewRouter()
	r.Mount("/v1/jobs", jobsHandler.Routes())

	ts := httptest.NewServer(r)
	defer ts.Close()

	// First create a job
	jobReq := models.CreateJobRequest{
		BinaryB64: "dGVzdCBiaW5hcnkgZGF0YQ==",
	}
	body, _ := json.Marshal(jobReq)

	createResp, err := http.Post(ts.URL+"/v1/jobs/", "application/json", bytes.NewReader(body))
	if err != nil {
		t.Fatalf("Failed to create job: %v", err)
	}
	var createdJob models.CreateJobResponse
	json.NewDecoder(createResp.Body).Decode(&createdJob)
	createResp.Body.Close()

	// Now cancel the job
	req, _ := http.NewRequest(http.MethodDelete, ts.URL+"/v1/jobs/"+createdJob.JobID, nil)
	cancelResp, err := http.DefaultClient.Do(req)
	if err != nil {
		t.Fatalf("Failed to cancel job: %v", err)
	}
	defer cancelResp.Body.Close()

	if cancelResp.StatusCode != http.StatusNoContent {
		t.Errorf("Expected status204, got %d", cancelResp.StatusCode)
	}
}

// TestJobStates tests all job states
func TestJobStates(t *testing.T) {
	states := []models.JobState{
		models.JobStateQueued,
		models.JobStateRunning,
		models.JobStateCompleted,
		models.JobStateFailed,
		models.JobStateCancelled,
	}

	for _, state := range states {
		job := models.Job{
			JobID: "test-job-id",
			State: state,
		}

		if job.State != state {
			t.Errorf("Expected state '%s', got '%s'", state, job.State)
		}
	}
}

// TestGDBInfo tests GDB info structure
func TestGDBInfo(t *testing.T) {
	gdbInfo := models.GDBInfo{
		JobID:        "test-job-id",
		DebugEnabled: true,
		GDBHost:      "localhost",
		GDBPort:      3333,
	}

	if gdbInfo.GDBHost != "localhost" {
		t.Errorf("Expected host 'localhost', got '%s'", gdbInfo.GDBHost)
	}
	if gdbInfo.GDBPort != 3333 {
		t.Errorf("Expected port3333, got %d", gdbInfo.GDBPort)
	}
}

// TestConfigLoading tests configuration loading
func TestConfigLoading(t *testing.T) {
	cfg := &config.Config{
		Server: config.ServerConfig{
			Host: "0.0.0.0",
			Port: 8080,
		},
		Database: config.DatabaseConfig{
			Host:     "localhost",
			Port:     5432,
			User:     "lab",
			Password: "secret",
			Database: "lab",
			SSLMode:  "disable",
		},
		KeyDB: config.KeyDBConfig{
			Addr: "localhost:6379",
		},
	}

	if cfg.Server.Port != 8080 {
		t.Errorf("Expected server port8080, got %d", cfg.Server.Port)
	}

	dsn := cfg.Database.DSN()
	expected := "postgres://lab:secret@localhost:5432/lab?sslmode=disable"
	if dsn != expected {
		t.Errorf("Expected DSN '%s', got '%s'", expected, dsn)
	}
}

// TestConfigDSNOverride tests DSN override from environment
func TestConfigDSNOverride(t *testing.T) {
	cfg := &config.Config{
		Database: config.DatabaseConfig{
			Host:        "localhost",
			Port:        5432,
			User:        "lab",
			Password:    "secret",
			Database:    "lab",
			SSLMode:     "disable",
			DSNOverride: "postgres://override:pass@remote:5432/db?sslmode=require",
		},
	}

	dsn := cfg.Database.DSN()
	if dsn != cfg.Database.DSNOverride {
		t.Errorf("Expected DSN to use override, got '%s'", dsn)
	}
}
