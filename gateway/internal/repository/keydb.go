package repository

import (
	"context"
	"encoding/json"
	"fmt"
	"time"

	"github.com/redis/go-redis/v9"

	"github.com/awesoma/stm32-sim/gateway/internal/models"
)

type KeyDBRepo struct {
	client *redis.Client
}

func NewKeyDBRepo(addr, password string, db int) *KeyDBRepo {
	return &KeyDBRepo{
		client: redis.NewClient(&redis.Options{
			Addr:     addr,
			Password: password,
			DB:       db,
		}),
	}
}

func (r *KeyDBRepo) Close() error {
	return r.client.Close()
}

func (r *KeyDBRepo) Ping(ctx context.Context) error {
	return r.client.Ping(ctx).Err()
}

// Job queue operations

// PushJobToPending adds a job ID to the pending queue
func (r *KeyDBRepo) PushJobToPending(ctx context.Context, jobID string) error {
	return r.client.LPush(ctx, "jobs:pending", jobID).Err()
}

// PopJobFromPending moves a job from pending to processing queue (blocking)
// Returns jobID or error
func (r *KeyDBRepo) PopJobFromPending(ctx context.Context, timeout time.Duration) (string, error) {
	result, err := r.client.BRPopLPush(ctx, "jobs:pending", "jobs:processing", timeout).Result()
	if err == redis.Nil {
		return "", nil // timeout, no job available
	}
	return result, err
}

// RemoveFromProcessing removes a job from the processing queue
func (r *KeyDBRepo) RemoveFromProcessing(ctx context.Context, jobID string) error {
	return r.client.LRem(ctx, "jobs:processing", 0, jobID).Err()
}

// RequeueJob moves a job back to pending (e.g., on worker failure)
func (r *KeyDBRepo) RequeueJob(ctx context.Context, jobID string) error {
	pipe := r.client.Pipeline()
	pipe.LRem(ctx, "jobs:processing", 0, jobID)
	pipe.LPush(ctx, "jobs:pending", jobID)
	_, err := pipe.Exec(ctx)
	return err
}

// Job metadata operations

// SetJobMetadata stores job metadata in a hash
func (r *KeyDBRepo) SetJobMetadata(ctx context.Context, jobID string, data map[string]interface{}) error {
	key := fmt.Sprintf("job:%s", jobID)
	return r.client.HSet(ctx, key, data).Err()
}

// GetJobMetadata retrieves job metadata from hash
func (r *KeyDBRepo) GetJobMetadata(ctx context.Context, jobID string) (map[string]string, error) {
	key := fmt.Sprintf("job:%s", jobID)
	return r.client.HGetAll(ctx, key).Result()
}

// SetJobField sets a single field in job metadata
func (r *KeyDBRepo) SetJobField(ctx context.Context, jobID, field string, value interface{}) error {
	key := fmt.Sprintf("job:%s", jobID)
	return r.client.HSet(ctx, key, field, value).Err()
}

// GetJobField gets a single field from job metadata
func (r *KeyDBRepo) GetJobField(ctx context.Context, jobID, field string) (string, error) {
	key := fmt.Sprintf("job:%s", jobID)
	return r.client.HGet(ctx, key, field).Result()
}

// Event publishing (Pub/Sub)

// PublishEvent publishes an event to a job's event channel
func (r *KeyDBRepo) PublishEvent(ctx context.Context, jobID string, event *models.Event) error {
	channel := fmt.Sprintf("events:job:%s", jobID)
	data, err := json.Marshal(event)
	if err != nil {
		return err
	}
	return r.client.Publish(ctx, channel, data).Err()
}

// SubscribeEvents returns a channel for job events
func (r *KeyDBRepo) SubscribeEvents(ctx context.Context, jobID string) *redis.PubSub {
	channel := fmt.Sprintf("events:job:%s", jobID)
	return r.client.Subscribe(ctx, channel)
}

// Worker registration

// RegisterWorker registers a worker with heartbeat
func (r *KeyDBRepo) RegisterWorker(ctx context.Context, workerID string, metadata map[string]interface{}) error {
	key := fmt.Sprintf("worker:%s", workerID)
	data := make(map[string]interface{})
	for k, v := range metadata {
		data[k] = v
	}
	data["last_heartbeat"] = time.Now().Unix()
	return r.client.HSet(ctx, key, data).Err()
}

// WorkerHeartbeat updates worker heartbeat timestamp
func (r *KeyDBRepo) WorkerHeartbeat(ctx context.Context, workerID string) error {
	key := fmt.Sprintf("worker:%s", workerID)
	return r.client.HSet(ctx, key, "last_heartbeat", time.Now().Unix()).Err()
}

// GetWorkerInfo retrieves worker information
func (r *KeyDBRepo) GetWorkerInfo(ctx context.Context, workerID string) (map[string]string, error) {
	key := fmt.Sprintf("worker:%s", workerID)
	return r.client.HGetAll(ctx, key).Result()
}

// ListActiveWorkers returns list of active worker IDs
func (r *KeyDBRepo) ListActiveWorkers(ctx context.Context) ([]string, error) {
	keys, err := r.client.Keys(ctx, "worker:*").Result()
	if err != nil {
		return nil, err
	}
	workers := make([]string, 0, len(keys))
	for _, key := range keys {
		// Extract worker ID from key
		var workerID string
		fmt.Sscanf(key, "worker:%s", &workerID)
		workers = append(workers, workerID)
	}
	return workers, nil
}

// Binary storage (temporary)

// StoreBinary stores a binary temporarily (for processing)
func (r *KeyDBRepo) StoreBinary(ctx context.Context, jobID string, binary []byte, ttl time.Duration) error {
	key := fmt.Sprintf("binary:%s", jobID)
	return r.client.Set(ctx, key, binary, ttl).Err()
}

// GetBinary retrieves a stored binary
func (r *KeyDBRepo) GetBinary(ctx context.Context, jobID string) ([]byte, error) {
	key := fmt.Sprintf("binary:%s", jobID)
	return r.client.Get(ctx, key).Bytes()
}

// DeleteBinary removes a stored binary
func (r *KeyDBRepo) DeleteBinary(ctx context.Context, jobID string) error {
	key := fmt.Sprintf("binary:%s", jobID)
	return r.client.Del(ctx, key).Err()
}
