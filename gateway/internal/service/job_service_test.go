package service

import (
	"context"
	"errors"
	"testing"
	"time"

	"github.com/awesoma/gateway/internal/models"
)

// MockPostgresRepo implements PostgresRepo interface
type MockPostgresRepo struct {
	jobs  map[string]*models.Job
	errOn map[string]error
}

func NewMockPostgresRepo() *MockPostgresRepo {
	return &MockPostgresRepo{
		jobs:  make(map[string]*models.Job),
		errOn: make(map[string]error),
	}
}

func (m *MockPostgresRepo) CreateJob(ctx context.Context, job *models.Job) error {
	if err, ok := m.errOn["CreateJob"]; ok {
		return err
	}
	m.jobs[job.JobID] = job
	return nil
}

func (m *MockPostgresRepo) GetJob(ctx context.Context, jobID string) (*models.Job, error) {
	if err, ok := m.errOn["GetJob"]; ok {
		return nil, err
	}
	job, ok := m.jobs[jobID]
	if !ok {
		return nil, ErrJobNotFound
	}
	return job, nil
}

func (m *MockPostgresRepo) UpdateJobState(ctx context.Context, jobID string, state models.JobState, errorText *string) error {
	if err, ok := m.errOn["UpdateJobState"]; ok {
		return err
	}
	if job, ok := m.jobs[jobID]; ok {
		job.State = state
		job.ErrorText = errorText
	}
	return nil
}

func (m *MockPostgresRepo) GetJobsByUser(ctx context.Context, userID string, limit, offset int) ([]*models.Job, error) {
	var jobs []*models.Job
	for _, job := range m.jobs {
		if job.UserID == userID {
			jobs = append(jobs, job)
		}
	}
	return jobs, nil
}

// MockKeyDBRepo implements KeyDBRepo interface
type MockKeyDBRepo struct {
	jobs      map[string]*JobHash
	errOn     map[string]error
	cancelled []string
}

func NewMockKeyDBRepo() *MockKeyDBRepo {
	return &MockKeyDBRepo{
		jobs:      make(map[string]*JobHash),
		errOn:     make(map[string]error),
		cancelled: make([]string, 0),
	}
}

func (m *MockKeyDBRepo) EnqueueJob(ctx context.Context, job *models.Job) error {
	if err, ok := m.errOn["EnqueueJob"]; ok {
		return err
	}
	m.jobs[job.JobID] = &JobHash{
		State:     string(job.State),
		UserID:    job.UserID,
		SHA256:    job.SHA256,
		CreatedAt: job.CreatedAt.Format(time.RFC3339),
		Debug:     job.DebugMode,
	}
	return nil
}

func (m *MockKeyDBRepo) GetJobHash(ctx context.Context, jobID string) (*JobHash, error) {
	if err, ok := m.errOn["GetJobHash"]; ok {
		return nil, err
	}
	job, ok := m.jobs[jobID]
	if !ok {
		return nil, ErrJobNotFound
	}
	return job, nil
}

func (m *MockKeyDBRepo) CancelJob(ctx context.Context, jobID string) error {
	if err, ok := m.errOn["CancelJob"]; ok {
		return err
	}
	m.cancelled = append(m.cancelled, jobID)
	if job, ok := m.jobs[jobID]; ok {
		job.State = string(models.StateCancelled)
	}
	return nil
}

func (m *MockKeyDBRepo) SendCommand(ctx context.Context, workerID string, command string, payload interface{}) error {
	return nil
}

// Tests

func TestCreateJob_Success(t *testing.T) {
	pgRepo := NewMockPostgresRepo()
	keyDBRepo := NewMockKeyDBRepo()
	svc := NewJobService(pgRepo, keyDBRepo)

	job := &models.Job{
		JobID:          "test-job-1",
		UserID:         "user-1",
		SHA256:         "abc123",
		State:          models.StateQueued,
		CreatedAt:      time.Now(),
		TimeoutSeconds: 30,
		DebugMode:      false,
	}

	err := svc.CreateJob(context.Background(), job)
	if err != nil {
		t.Fatalf("expected no error, got: %v", err)
	}

	// Verify job was saved to PostgreSQL
	savedJob, err := pgRepo.GetJob(context.Background(), "test-job-1")
	if err != nil {
		t.Fatalf("expected job to be saved, got error: %v", err)
	}
	if savedJob.JobID != job.JobID {
		t.Errorf("expected job_id %s, got %s", job.JobID, savedJob.JobID)
	}

	// Verify job was enqueued to KeyDB
	jobHash, err := keyDBRepo.GetJobHash(context.Background(), "test-job-1")
	if err != nil {
		t.Fatalf("expected job to be enqueued, got error: %v", err)
	}
	if jobHash.State != string(models.StateQueued) {
		t.Errorf("expected state %s, got %s", models.StateQueued, jobHash.State)
	}
}

func TestCreateJob_PostgresError(t *testing.T) {
	pgRepo := NewMockPostgresRepo()
	keyDBRepo := NewMockKeyDBRepo()
	svc := NewJobService(pgRepo, keyDBRepo)

	pgRepo.errOn["CreateJob"] = errors.New("database error")

	job := &models.Job{
		JobID:     "test-job-2",
		UserID:    "user-1",
		SHA256:    "abc123",
		State:     models.StateQueued,
		CreatedAt: time.Now(),
	}

	err := svc.CreateJob(context.Background(), job)
	if err == nil {
		t.Fatal("expected error, got nil")
	}
}

func TestGetJob_Success(t *testing.T) {
	pgRepo := NewMockPostgresRepo()
	keyDBRepo := NewMockKeyDBRepo()
	svc := NewJobService(pgRepo, keyDBRepo)

	// Setup test job
	job := &models.Job{
		JobID:     "test-job-3",
		UserID:    "user-1",
		SHA256:    "abc123",
		State:     models.StateRunning,
		CreatedAt: time.Now(),
	}
	pgRepo.jobs["test-job-3"] = job
	keyDBRepo.jobs["test-job-3"] = &JobHash{
		State:    string(models.StateRunning),
		WorkerID: "worker-1",
		GDBPort:  12345,
		GDBHost:  "192.168.1.100",
	}

	retrievedJob, err := svc.GetJob(context.Background(), "test-job-3")
	if err != nil {
		t.Fatalf("expected no error, got: %v", err)
	}

	if retrievedJob.JobID != "test-job-3" {
		t.Errorf("expected job_id test-job-3, got %s", retrievedJob.JobID)
	}

	if retrievedJob.GDBPort == nil || *retrievedJob.GDBPort != 12345 {
		t.Errorf("expected GDB port 12345, got %v", retrievedJob.GDBPort)
	}
}

func TestGetJob_NotFound(t *testing.T) {
	pgRepo := NewMockPostgresRepo()
	keyDBRepo := NewMockKeyDBRepo()
	svc := NewJobService(pgRepo, keyDBRepo)

	_, err := svc.GetJob(context.Background(), "non-existent")
	if !errors.Is(err, ErrJobNotFound) {
		t.Errorf("expected ErrJobNotFound, got: %v", err)
	}
}

func TestCancelJob_Success(t *testing.T) {
	pgRepo := NewMockPostgresRepo()
	keyDBRepo := NewMockKeyDBRepo()
	svc := NewJobService(pgRepo, keyDBRepo)

	// Setup running job
	job := &models.Job{
		JobID:     "test-job-4",
		UserID:    "user-1",
		SHA256:    "abc123",
		State:     models.StateRunning,
		CreatedAt: time.Now(),
	}
	pgRepo.jobs["test-job-4"] = job

	err := svc.CancelJob(context.Background(), "test-job-4")
	if err != nil {
		t.Fatalf("expected no error, got: %v", err)
	}

	// Verify job was cancelled in PostgreSQL
	cancelledJob, _ := pgRepo.GetJob(context.Background(), "test-job-4")
	if cancelledJob.State != models.StateCancelled {
		t.Errorf("expected state %s, got %s", models.StateCancelled, cancelledJob.State)
	}

	// Verify job was cancelled in KeyDB
	found := false
	for _, id := range keyDBRepo.cancelled {
		if id == "test-job-4" {
			found = true
			break
		}
	}
	if !found {
		t.Error("expected job to be cancelled in KeyDB")
	}
}

func TestCancelJob_AlreadyCompleted(t *testing.T) {
	pgRepo := NewMockPostgresRepo()
	keyDBRepo := NewMockKeyDBRepo()
	svc := NewJobService(pgRepo, keyDBRepo)

	// Setup completed job
	job := &models.Job{
		JobID:      "test-job-5",
		UserID:     "user-1",
		SHA256:     "abc123",
		State:      models.StateCompleted,
		CreatedAt:  time.Now(),
		FinishedAt: TimePtr(time.Now()),
	}
	pgRepo.jobs["test-job-5"] = job

	err := svc.CancelJob(context.Background(), "test-job-5")
	if !errors.Is(err, ErrJobCannotBeCancelled) {
		t.Errorf("expected ErrJobCannotBeCancelled, got: %v", err)
	}
}

func TestListJobsByUser(t *testing.T) {
	pgRepo := NewMockPostgresRepo()
	keyDBRepo := NewMockKeyDBRepo()
	svc := NewJobService(pgRepo, keyDBRepo)

	// Setup test jobs
	pgRepo.jobs["job-1"] = &models.Job{JobID: "job-1", UserID: "user-1", State: models.StateCompleted}
	pgRepo.jobs["job-2"] = &models.Job{JobID: "job-2", UserID: "user-1", State: models.StateRunning}
	pgRepo.jobs["job-3"] = &models.Job{JobID: "job-3", UserID: "user-2", State: models.StateQueued}

	jobs, err := svc.ListJobsByUser(context.Background(), "user-1", 10, 0)
	if err != nil {
		t.Fatalf("expected no error, got: %v", err)
	}

	if len(jobs) != 2 {
		t.Errorf("expected 2 jobs, got %d", len(jobs))
	}
}
