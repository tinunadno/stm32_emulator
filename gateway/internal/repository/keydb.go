package repository

import (
	"context"
	"encoding/json"
	"fmt"
	"time"

	"github.com/redis/go-redis/v9"

	"github.com/awesoma/gateway/internal/models"
)

// KeyDBRepository handles KeyDB/Redis operations
type KeyDBRepository struct {
	client *redis.Client
}

// NewKeyDBRepository creates a new KeyDB repository
func NewKeyDBRepository(ctx context.Context, addr, password string, db, poolSize int) (*KeyDBRepository, error) {
	client := redis.NewClient(&redis.Options{
		Addr:     addr,
		Password: password,
		DB:       db,
		PoolSize: poolSize,
	})

	// Test connection
	if err := client.Ping(ctx).Err(); err != nil {
		return nil, fmt.Errorf("failed to connect to KeyDB: %w", err)
	}

	return &KeyDBRepository{client: client}, nil
}

// Close closes the KeyDB connection
func (r *KeyDBRepository) Close() error {
	return r.client.Close()
}

// JobHash represents the job hash stored in KeyDB
type JobHash struct {
	State        string `redis:"state"`
	UserID       string `redis:"user_id"`
	SHA256       string `redis:"sha256"`
	CreatedAt    string `redis:"created_at"`
	Debug        bool   `redis:"debug"`
	WorkerID     string `redis:"worker_id"`
	StartedAt    string `redis:"started_at"`
	GDBPort      int    `redis:"gdb_port"`
	GDBHost      string `redis:"gdb_host"`
	GDBConnected bool   `redis:"gdb_connected"`
}

// EnqueueJob adds a job to the pending queue
func (r *KeyDBRepository) EnqueueJob(ctx context.Context, job *models.Job) error {
	// Use transaction to ensure atomicity
	pipe := r.client.Pipeline()

	// Create job hash
	jobData := map[string]interface{}{
		"state":      string(job.State),
		"user_id":    job.UserID,
		"sha256":     job.SHA256,
		"created_at": job.CreatedAt.Format(time.RFC3339),
		"debug":      job.DebugMode,
	}

	if job.TimeoutSeconds > 0 {
		jobData["timeout_seconds"] = job.TimeoutSeconds
	}

	// Set job hash
	pipe.HSet(ctx, r.jobKey(job.JobID), jobData)

	// Add to pending queue
	pipe.LPush(ctx, "jobs:pending", job.JobID)

	// Execute transaction
	_, err := pipe.Exec(ctx)
	if err != nil {
		return fmt.Errorf("failed to enqueue job: %w", err)
	}

	return nil
}

// GetJobHash retrieves job data from KeyDB
func (r *KeyDBRepository) GetJobHash(ctx context.Context, jobID string) (*JobHash, error) {
	key := r.jobKey(jobID)
	result, err := r.client.HGetAll(ctx, key).Result()
	if err != nil {
		return nil, fmt.Errorf("failed to get job hash: %w", err)
	}

	if len(result) == 0 {
		return nil, ErrJobNotFound
	}

	var jobHash JobHash
	jobHash.State = result["state"]
	jobHash.UserID = result["user_id"]
	jobHash.SHA256 = result["sha256"]
	jobHash.CreatedAt = result["created_at"]
	jobHash.Debug = result["debug"] == "1" || result["debug"] == "true"
	jobHash.WorkerID = result["worker_id"]
	jobHash.StartedAt = result["started_at"]
	if port := result["gdb_port"]; port != "" {
		fmt.Sscanf(port, "%d", &jobHash.GDBPort)
	}
	jobHash.GDBHost = result["gdb_host"]
	jobHash.GDBConnected = result["gdb_connected"] == "1" || result["gdb_connected"] == "true"

	return &jobHash, nil
}

// UpdateJobState updates the job state in KeyDB
func (r *KeyDBRepository) UpdateJobState(ctx context.Context, jobID string, state models.JobState) error {
	key := r.jobKey(jobID)
	return r.client.HSet(ctx, key, "state", string(state)).Err()
}

// PublishEvent publishes an event to the job's event channel
func (r *KeyDBRepository) PublishEvent(ctx context.Context, jobID string, event *models.Event) error {
	channel := r.eventChannel(jobID)

	data, err := json.Marshal(event)
	if err != nil {
		return fmt.Errorf("failed to marshal event: %w", err)
	}

	return r.client.Publish(ctx, channel, data).Err()
}

// SubscribeEvents subscribes to events for a specific job
func (r *KeyDBRepository) SubscribeEvents(ctx context.Context, jobID string) *redis.PubSub {
	channel := r.eventChannel(jobID)
	return r.client.Subscribe(ctx, channel)
}

// SubscribeAllEvents subscribes to all job events (for monitoring)
func (r *KeyDBRepository) SubscribeAllEvents(ctx context.Context) *redis.PubSub {
	return r.client.PSubscribe(ctx, "events:job:*")
}

// RegisterWorker registers a worker in KeyDB
func (r *KeyDBRepository) RegisterWorker(ctx context.Context, worker *models.Worker) error {
	key := r.workerKey(worker.WorkerID)

	data := map[string]interface{}{
		"worker_id":  worker.WorkerID,
		"ip_address": worker.IPAddress,
		"last_seen":  worker.LastSeen.Format(time.RFC3339),
		"status":     worker.Status,
	}

	if worker.CurrentJob != nil {
		data["current_job"] = *worker.CurrentJob
	}

	// Set with expiration (worker must heartbeat)
	pipe := r.client.Pipeline()
	pipe.HSet(ctx, key, data)
	pipe.Expire(ctx, key, 30*time.Second) // 30 second TTL

	_, err := pipe.Exec(ctx)
	return err
}

// GetWorker retrieves worker information
func (r *KeyDBRepository) GetWorker(ctx context.Context, workerID string) (*models.Worker, error) {
	key := r.workerKey(workerID)
	result, err := r.client.HGetAll(ctx, key).Result()
	if err != nil {
		return nil, fmt.Errorf("failed to get worker: %w", err)
	}

	if len(result) == 0 {
		return nil, ErrWorkerNotFound
	}

	var worker models.Worker
	worker.WorkerID = result["worker_id"]
	worker.IPAddress = result["ip_address"]
	worker.Status = result["status"]

	if lastSeen := result["last_seen"]; lastSeen != "" {
		worker.LastSeen, _ = time.Parse(time.RFC3339, lastSeen)
	}

	if currentJob := result["current_job"]; currentJob != "" {
		worker.CurrentJob = &currentJob
	}

	return &worker, nil
}

// GetActiveWorkers retrieves all active workers
func (r *KeyDBRepository) GetActiveWorkers(ctx context.Context) ([]*models.Worker, error) {
	keys, err := r.client.Keys(ctx, "worker:*").Result()
	if err != nil {
		return nil, fmt.Errorf("failed to get worker keys: %w", err)
	}

	var workers []*models.Worker
	for _, key := range keys {
		result, err := r.client.HGetAll(ctx, key).Result()
		if err != nil {
			continue
		}

		if len(result) == 0 {
			continue
		}

		var worker models.Worker
		worker.WorkerID = result["worker_id"]
		worker.IPAddress = result["ip_address"]
		worker.Status = result["status"]

		if lastSeen := result["last_seen"]; lastSeen != "" {
			worker.LastSeen, _ = time.Parse(time.RFC3339, lastSeen)
		}

		if currentJob := result["current_job"]; currentJob != "" {
			worker.CurrentJob = &currentJob
		}

		workers = append(workers, &worker)
	}

	return workers, nil
}

// SendCommand sends a command to a worker
func (r *KeyDBRepository) SendCommand(ctx context.Context, workerID string, command string, payload interface{}) error {
	channel := fmt.Sprintf("commands:worker:%s", workerID)

	data, err := json.Marshal(map[string]interface{}{
		"command": command,
		"payload": payload,
	})
	if err != nil {
		return fmt.Errorf("failed to marshal command: %w", err)
	}

	return r.client.Publish(ctx, channel, data).Err()
}

// CancelJob cancels a job by moving it from processing to cancelled
func (r *KeyDBRepository) CancelJob(ctx context.Context, jobID string) error {
	// Update job state
	key := r.jobKey(jobID)
	if err := r.client.HSet(ctx, key, "state", string(models.StateCancelled)).Err(); err != nil {
		return fmt.Errorf("failed to update job state: %w", err)
	}

	// Remove from processing queue
	if err := r.client.LRem(ctx, "jobs:processing", 0, jobID).Err(); err != nil {
		return fmt.Errorf("failed to remove from processing queue: %w", err)
	}

	// Publish cancellation event
	event := &models.Event{
		Type:      models.EventTypeStatus,
		JobID:     jobID,
		Timestamp: time.Now(),
		Data: map[string]interface{}{
			"state":   string(models.StateCancelled),
			"message": "Job cancelled by user",
		},
	}

	return r.PublishEvent(ctx, jobID, event)
}

// Helper functions
func (r *KeyDBRepository) jobKey(jobID string) string {
	return fmt.Sprintf("job:%s", jobID)
}

func (r *KeyDBRepository) eventChannel(jobID string) string {
	return fmt.Sprintf("events:job:%s", jobID)
}

func (r *KeyDBRepository) workerKey(workerID string) string {
	return fmt.Sprintf("worker:%s", workerID)
}

// Custom errors
var (
	ErrWorkerNotFound = fmt.Errorf("worker not found")
)
