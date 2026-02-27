package service

import (
	"context"
	"errors"
	"fmt"
	"time"

	"github.com/awesoma/gateway/internal/models"
)

// JobService defines the interface for job operations
type JobService interface {
	CreateJob(ctx context.Context, job *models.Job) error
	GetJob(ctx context.Context, jobID string) (*models.Job, error)
	CancelJob(ctx context.Context, jobID string) error
	ListJobsByUser(ctx context.Context, userID string, limit, offset int) ([]*models.Job, error)
}

// PostgresRepo interface for PostgreSQL operations
type PostgresRepo interface {
	CreateJob(ctx context.Context, job *models.Job) error
	GetJob(ctx context.Context, jobID string) (*models.Job, error)
	UpdateJobState(ctx context.Context, jobID string, state models.JobState, errorText *string) error
	GetJobsByUser(ctx context.Context, userID string, limit, offset int) ([]*models.Job, error)
}

// KeyDBRepo interface for KeyDB operations
type KeyDBRepo interface {
	EnqueueJob(ctx context.Context, job *models.Job) error
	GetJobHash(ctx context.Context, jobID string) (*JobHash, error)
	CancelJob(ctx context.Context, jobID string) error
	SendCommand(ctx context.Context, workerID string, command string, payload interface{}) error
}

// JobHash represents the job hash from KeyDB
type JobHash struct {
	State        string
	UserID       string
	SHA256       string
	CreatedAt    string
	Debug        bool
	WorkerID     string
	StartedAt    string
	GDBPort      int
	GDBHost      string
	GDBConnected bool
}

// jobService implements JobService
type jobService struct {
	pgRepo PostgresRepo
	keyDB  KeyDBRepo
}

// NewJobService creates a new job service
func NewJobService(pgRepo PostgresRepo, keyDB KeyDBRepo) JobService {
	return &jobService{
		pgRepo: pgRepo,
		keyDB:  keyDB,
	}
}

// CreateJob creates a new job in both PostgreSQL and KeyDB
func (s *jobService) CreateJob(ctx context.Context, job *models.Job) error {
	// Save to PostgreSQL for persistent storage
	if err := s.pgRepo.CreateJob(ctx, job); err != nil {
		return fmt.Errorf("failed to create job in PostgreSQL: %w", err)
	}

	// Enqueue in KeyDB for worker pickup
	if err := s.keyDB.EnqueueJob(ctx, job); err != nil {
		// Try to clean up PostgreSQL record
		_ = s.pgRepo.UpdateJobState(ctx, job.JobID, models.StateFailed,
			strPtr("Failed to enqueue job"))
		return fmt.Errorf("failed to enqueue job in KeyDB: %w", err)
	}

	return nil
}

// GetJob retrieves a job by ID
func (s *jobService) GetJob(ctx context.Context, jobID string) (*models.Job, error) {
	// First try to get from PostgreSQL (authoritative source)
	job, err := s.pgRepo.GetJob(ctx, jobID)
	if err != nil {
		if errors.Is(err, ErrJobNotFound) {
			return nil, ErrJobNotFound
		}
		return nil, fmt.Errorf("failed to get job: %w", err)
	}

	// Optionally merge with real-time data from KeyDB
	jobHash, err := s.keyDB.GetJobHash(ctx, jobID)
	if err == nil {
		// Update with real-time state from KeyDB if available
		if jobHash.State != "" {
			job.State = models.JobState(jobHash.State)
		}
		if jobHash.WorkerID != "" {
			job.WorkerID = &jobHash.WorkerID
		}
		if jobHash.GDBPort > 0 {
			job.GDBPort = &jobHash.GDBPort
		}
		if jobHash.GDBHost != "" {
			job.GDBHost = &jobHash.GDBHost
		}
		job.GDBConnected = jobHash.GDBConnected
	}

	return job, nil
}

// CancelJob cancels a running or queued job
func (s *jobService) CancelJob(ctx context.Context, jobID string) error {
	// Get current job state
	job, err := s.pgRepo.GetJob(ctx, jobID)
	if err != nil {
		if errors.Is(err, ErrJobNotFound) {
			return ErrJobNotFound
		}
		return fmt.Errorf("failed to get job: %w", err)
	}

	// Check if job can be cancelled
	if job.State == models.StateCompleted || job.State == models.StateFailed || job.State == models.StateCancelled {
		return ErrJobCannotBeCancelled
	}

	// Cancel in KeyDB (will notify worker)
	if err := s.keyDB.CancelJob(ctx, jobID); err != nil {
		return fmt.Errorf("failed to cancel job in KeyDB: %w", err)
	}

	// Update state in PostgreSQL
	if err := s.pgRepo.UpdateJobState(ctx, jobID, models.StateCancelled, nil); err != nil {
		return fmt.Errorf("failed to update job state: %w", err)
	}

	// If job has a worker, send cancel command
	if job.WorkerID != nil {
		_ = s.keyDB.SendCommand(ctx, *job.WorkerID, "cancel", map[string]string{
			"job_id": jobID,
		})
	}

	return nil
}

// ListJobsByUser retrieves all jobs for a user
func (s *jobService) ListJobsByUser(ctx context.Context, userID string, limit, offset int) ([]*models.Job, error) {
	jobs, err := s.pgRepo.GetJobsByUser(ctx, userID, limit, offset)
	if err != nil {
		return nil, fmt.Errorf("failed to list jobs: %w", err)
	}
	return jobs, nil
}

// Helper function
func strPtr(s string) *string {
	return &s
}

// Custom errors
var (
	ErrJobNotFound          = errors.New("job not found")
	ErrJobCannotBeCancelled = errors.New("job cannot be cancelled")
	ErrUnauthorized         = errors.New("unauthorized access")
)

// Helper for time pointer
func TimePtr(t time.Time) *time.Time {
	return &t
}
