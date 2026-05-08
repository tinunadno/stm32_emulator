package queue

import (
	"context"
	"encoding/json"
	"fmt"
	"time"

	"github.com/redis/go-redis/v9"
)

const (
	keyPending    = "sim:jobs:pending"
	keyProcessing = "sim:jobs:processing"
	keyStats      = "sim:stats"
	keyHistory    = "sim:jobs:history"
	resultTTL     = time.Hour
)

type Queue struct {
	rdb *redis.Client
}

func New(addr string) *Queue {
	return &Queue{
		rdb: redis.NewClient(&redis.Options{Addr: addr}),
	}
}

func (q *Queue) Ping(ctx context.Context) error {
	return q.rdb.Ping(ctx).Err()
}

// Dequeue blocks until a job is available, moves it to the processing list,
// and returns the raw JSON bytes.
func (q *Queue) Dequeue(ctx context.Context) ([]byte, error) {
	res, err := q.rdb.BRPopLPush(ctx, keyPending, keyProcessing, 0).Bytes()
	if err != nil {
		return nil, err
	}
	return res, nil
}

// AckDone removes the job from the processing list after it has been handled.
func (q *Queue) AckDone(ctx context.Context, raw []byte) {
	q.rdb.LRem(ctx, keyProcessing, 1, string(raw))
}

// StoreResult writes the result to three places:
//   - sim:results:{id}   — with TTL (for clients polling)
//   - sim:jobs:detail:{id} — without TTL (persistent log)
//   - sim:jobs:history   — sorted set, score = unix timestamp
func (q *Queue) StoreResult(ctx context.Context, jobID string, result any) error {
	data, err := json.Marshal(result)
	if err != nil {
		return err
	}
	now := float64(time.Now().Unix())

	pipe := q.rdb.Pipeline()
	pipe.Set(ctx, "sim:results:"+jobID, data, resultTTL)
	pipe.Set(ctx, "sim:jobs:detail:"+jobID, data, 0)
	pipe.ZAdd(ctx, keyHistory, redis.Z{Score: now, Member: jobID})
	_, err = pipe.Exec(ctx)
	return err
}

// UpdateWorkerStatus sets a heartbeat key with TTL for a worker.
func (q *Queue) UpdateWorkerStatus(ctx context.Context, workerID, status string) {
	key := fmt.Sprintf("sim:worker:%s", workerID)
	q.rdb.Set(ctx, key, status, 30*time.Second)
}

// IncrStats increments aggregate counters.
func (q *Queue) IncrStats(ctx context.Context, field string, by int64) {
	q.rdb.HIncrBy(ctx, keyStats, field, by)
}
