package service

import (
	"context"
	"crypto/sha256"
	"encoding/base64"
	"encoding/hex"
	"errors"
	"fmt"
	"time"

	"github.com/google/uuid"

	"github.com/awesoma/stm32-sim/gateway/internal/models"
	"github.com/awesoma/stm32-sim/gateway/internal/repository"
)

var (
	ErrJobNotFound      = errors.New("job not found")
	ErrInvalidBinary    = errors.New("invalid binary data")
	ErrJobAlreadyExists = errors.New("job already exists")
)

// JobService implements the business logic for job management
type JobService struct {
	pg    repository.JobRepository
	keydb repository.KeyDBRepository
}

// NewJobService creates a new job service
func NewJobService(pg repository.JobRepository, keydb repository.KeyDBRepository) *JobService {
	return &JobService{
		pg:    pg,
		keydb: keydb,
	}
}

// CreateJob creates a new job
func (s *JobService) CreateJob(ctx context.Context, userID string, req *models.CreateJobRequest) (*models.CreateJobResponse, error) {
	// Decode base64 binary
	binary, err := base64.StdEncoding.DecodeString(req.BinaryB64)
	if err != nil {
		return nil, fmt.Errorf("%w: %v", ErrInvalidBinary, err)
	}

	if len(binary) == 0 {
		return nil, ErrInvalidBinary
	}

	// Calculate SHA-256 hash
	hash := sha256.Sum256(binary)
	sha256Hex := hex.EncodeToString(hash[:])

	// Generate job ID
	jobID := uuid.New().String()

	// Create job model
	now := time.Now()
	job := &models.Job{
		JobID:          jobID,
		UserID:         userID,
		SHA256:         sha256Hex,
		State:          models.JobStateQueued,
		CreatedAt:      now,
		TimeoutSeconds: req.TimeoutSeconds,
		DebugMode:      req.Debug,
	}

	// Store job in database
	if err := s.pg.CreateJob(ctx, job); err != nil {
		return nil, err
	}

	// Store binary in KeyDB
	if err := s.keydb.StoreBinary(ctx, jobID, binary); err != nil {
		return nil, err
	}

	// Enqueue job for processing
	if err := s.keydb.EnqueueJob(ctx, job); err != nil {
		return nil, err
	}

	return &models.CreateJobResponse{
		JobID:     jobID,
		SHA256:    sha256Hex,
		Debug:     req.Debug,
		StatusURL: fmt.Sprintf("/v1/jobs/%s", jobID),
		EventsURL: fmt.Sprintf("/v1/jobs/%s/events", jobID),
	}, nil
}

// GetJob retrieves a job by ID
func (s *JobService) GetJob(ctx context.Context, jobID string) (*models.Job, error) {
	job, err := s.pg.GetJob(ctx, jobID)
	if err != nil {
		return nil, err
	}
	return job, nil
}

// GetGDBInfo retrieves GDB connection info for a job
func (s *JobService) GetGDBInfo(ctx context.Context, jobID string) (*models.GDBInfo, error) {
	// First check if job exists
	job, err := s.pg.GetJob(ctx, jobID)
	if err != nil {
		return nil, err
	}

	if !job.DebugMode {
		return nil, errors.New("job is not in debug mode")
	}

	return s.keydb.GetGDBInfo(ctx, jobID)
}

// CancelJob cancels a running job
func (s *JobService) CancelJob(ctx context.Context, jobID string) error {
	// Check if job exists
	job, err := s.pg.GetJob(ctx, jobID)
	if err != nil {
		return err
	}

	// Only allow cancellation of queued or running jobs
	if job.State != models.JobStateQueued && job.State != models.JobStateRunning {
		return errors.New("job cannot be cancelled in current state")
	}

	// Update job state
	errText := "cancelled by user"
	if err := s.pg.UpdateJobState(ctx, jobID, models.JobStateCancelled, &errText); err != nil {
		return err
	}

	// Publish cancellation event
	event := &models.Event{
		Type:      "job_cancelled",
		Timestamp: time.Now(),
		Data: map[string]interface{}{
			"job_id": jobID,
		},
	}
	return s.keydb.PublishEvent(ctx, event)
}

// ListJobs lists jobs for a user
func (s *JobService) ListJobs(ctx context.Context, userID string, limit, offset int) ([]*models.Job, error) {
	return s.pg.ListJobs(ctx, userID, limit, offset)
}
