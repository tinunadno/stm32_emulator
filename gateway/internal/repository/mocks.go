package repository

import (
	"context"
	"errors"
	"sync"

	"github.com/awesoma/stm32-sim/gateway/internal/models"
)

// Errors
var (
	ErrJobNotFound     = errors.New("job not found")
	ErrBinaryNotFound  = errors.New("binary not found")
	ErrGDBInfoNotFound = errors.New("gdb info not found")
)

// JobRepository is the interface for job storage operations
type JobRepository interface {
	CreateJob(ctx context.Context, job *models.Job) error
	GetJob(ctx context.Context, jobID string) (*models.Job, error)
	UpdateJobState(ctx context.Context, jobID string, state models.JobState, errorText *string) error
	ListJobs(ctx context.Context, userID string, limit, offset int) ([]*models.Job, error)
}

// KeyDBRepository is the interface for KeyDB operations
type KeyDBRepository interface {
	EnqueueJob(ctx context.Context, job *models.Job) error
	DequeueJob(ctx context.Context, workerID string) (*models.Job, error)
	PublishEvent(ctx context.Context, event *models.Event) error
	Subscribe(ctx context.Context, handler func(event *models.Event)) error
	StoreBinary(ctx context.Context, jobID string, data []byte) error
	GetBinary(ctx context.Context, jobID string) ([]byte, error)
	SetGDBInfo(ctx context.Context, jobID string, info *models.GDBInfo) error
	GetGDBInfo(ctx context.Context, jobID string) (*models.GDBInfo, error)
	Ping(ctx context.Context) error
}

// MockPostgresRepository is a mock implementation of JobRepository
type MockPostgresRepository struct {
	mu   sync.RWMutex
	jobs map[string]*models.Job
}

// NewMockPostgresRepository creates a new mock repository
func NewMockPostgresRepository() *MockPostgresRepository {
	return &MockPostgresRepository{
		jobs: make(map[string]*models.Job),
	}
}

func (m *MockPostgresRepository) CreateJob(ctx context.Context, job *models.Job) error {
	m.mu.Lock()
	defer m.mu.Unlock()
	m.jobs[job.JobID] = job
	return nil
}

func (m *MockPostgresRepository) GetJob(ctx context.Context, jobID string) (*models.Job, error) {
	m.mu.RLock()
	defer m.mu.RUnlock()
	job, ok := m.jobs[jobID]
	if !ok {
		return nil, ErrJobNotFound
	}
	return job, nil
}

func (m *MockPostgresRepository) UpdateJobState(ctx context.Context, jobID string, state models.JobState, errorText *string) error {
	m.mu.Lock()
	defer m.mu.Unlock()
	job, ok := m.jobs[jobID]
	if !ok {
		return ErrJobNotFound
	}
	job.State = state
	if errorText != nil {
		job.ErrorText = errorText
	}
	return nil
}

func (m *MockPostgresRepository) ListJobs(ctx context.Context, userID string, limit, offset int) ([]*models.Job, error) {
	m.mu.RLock()
	defer m.mu.RUnlock()
	jobs := make([]*models.Job, 0)
	for _, job := range m.jobs {
		if userID == "" || job.UserID == userID {
			jobs = append(jobs, job)
		}
	}
	return jobs, nil
}

// MockKeyDBRepository is a mock implementation of KeyDBRepository
type MockKeyDBRepository struct {
	mu      sync.RWMutex
	queue   []*models.Job
	pubsub  []func(event *models.Event)
	binary  map[string][]byte
	gdbInfo map[string]*models.GDBInfo
}

// NewMockKeyDBRepository creates a new mock repository
func NewMockKeyDBRepository() *MockKeyDBRepository {
	return &MockKeyDBRepository{
		queue:   make([]*models.Job, 0),
		pubsub:  make([]func(event *models.Event), 0),
		binary:  make(map[string][]byte),
		gdbInfo: make(map[string]*models.GDBInfo),
	}
}

func (m *MockKeyDBRepository) EnqueueJob(ctx context.Context, job *models.Job) error {
	m.mu.Lock()
	defer m.mu.Unlock()
	m.queue = append(m.queue, job)
	return nil
}

func (m *MockKeyDBRepository) DequeueJob(ctx context.Context, workerID string) (*models.Job, error) {
	m.mu.Lock()
	defer m.mu.Unlock()
	if len(m.queue) == 0 {
		return nil, nil
	}
	job := m.queue[0]
	m.queue = m.queue[1:]
	return job, nil
}

func (m *MockKeyDBRepository) PublishEvent(ctx context.Context, event *models.Event) error {
	m.mu.RLock()
	defer m.mu.RUnlock()
	for _, handler := range m.pubsub {
		handler(event)
	}
	return nil
}

func (m *MockKeyDBRepository) Subscribe(ctx context.Context, handler func(event *models.Event)) error {
	m.mu.Lock()
	defer m.mu.Unlock()
	m.pubsub = append(m.pubsub, handler)
	return nil
}

func (m *MockKeyDBRepository) StoreBinary(ctx context.Context, jobID string, data []byte) error {
	m.mu.Lock()
	defer m.mu.Unlock()
	m.binary[jobID] = data
	return nil
}

func (m *MockKeyDBRepository) GetBinary(ctx context.Context, jobID string) ([]byte, error) {
	m.mu.RLock()
	defer m.mu.RUnlock()
	data, ok := m.binary[jobID]
	if !ok {
		return nil, ErrBinaryNotFound
	}
	return data, nil
}

func (m *MockKeyDBRepository) SetGDBInfo(ctx context.Context, jobID string, info *models.GDBInfo) error {
	m.mu.Lock()
	defer m.mu.Unlock()
	m.gdbInfo[jobID] = info
	return nil
}

func (m *MockKeyDBRepository) GetGDBInfo(ctx context.Context, jobID string) (*models.GDBInfo, error) {
	m.mu.RLock()
	defer m.mu.RUnlock()
	info, ok := m.gdbInfo[jobID]
	if !ok {
		return nil, ErrGDBInfoNotFound
	}
	return info, nil
}

func (m *MockKeyDBRepository) Ping(ctx context.Context) error {
	return nil
}
