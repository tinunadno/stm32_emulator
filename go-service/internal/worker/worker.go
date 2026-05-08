package worker

import (
	"context"
	"encoding/base64"
	"encoding/json"
	"fmt"
	"log/slog"
	"os"
	"time"

	"go.opentelemetry.io/otel"
	"go.opentelemetry.io/otel/attribute"
	"go.opentelemetry.io/otel/metric"

	"stm32sim-service/internal/queue"
	"stm32sim-service/internal/simulator"
	"stm32sim-service/internal/telemetry"
)

var tracer = otel.Tracer("worker")

// Job is the incoming task from KeyDB.
type Job struct {
	ID          string `json:"id"`
	BinaryB64   string `json:"binary_b64"`
	SubmittedAt string `json:"submitted_at,omitempty"`
}

// Result is the full output stored in KeyDB.
type Result struct {
	JobID          string            `json:"job_id"`
	Status         string            `json:"status"`
	CompletedAt    time.Time         `json:"completed_at"`
	WallDurationMs int64             `json:"wall_duration_ms"`
	Sim            simulator.SimOutput `json:"sim"`
	ErrorMessage   string            `json:"error_message,omitempty"`
}

type Worker struct {
	id     string
	q      *queue.Queue
	simCfg simulator.Config
	tel    *telemetry.Provider
}

func New(id string, q *queue.Queue, simCfg simulator.Config, tel *telemetry.Provider) *Worker {
	return &Worker{id: id, q: q, simCfg: simCfg, tel: tel}
}

// Run blocks, continuously pulling jobs from the queue until ctx is cancelled.
func (w *Worker) Run(ctx context.Context) {
	slog.Info("worker started", "id", w.id)
	for {
		select {
		case <-ctx.Done():
			slog.Info("worker stopping", "id", w.id)
			return
		default:
		}

		raw, err := w.q.Dequeue(ctx)
		if err != nil {
			if ctx.Err() != nil {
				return
			}
			slog.Error("dequeue error", "worker", w.id, "err", err)
			time.Sleep(time.Second)
			continue
		}

		w.process(ctx, raw)
	}
}

func (w *Worker) process(ctx context.Context, raw []byte) {
	ctx, span := tracer.Start(ctx, "job.process")
	defer span.End()

	w.q.UpdateWorkerStatus(ctx, w.id, "running")
	w.tel.ActiveWorkers.Add(ctx, 1)
	defer func() {
		w.q.UpdateWorkerStatus(ctx, w.id, "idle")
		w.tel.ActiveWorkers.Add(ctx, -1)
	}()

	// Parse job
	var job Job
	if err := json.Unmarshal(raw, &job); err != nil {
		slog.Error("failed to parse job", "worker", w.id, "err", err)
		w.q.AckDone(ctx, raw)
		return
	}
	if job.ID == "" {
		job.ID = fmt.Sprintf("auto-%d", time.Now().UnixNano())
	}

	span.SetAttributes(attribute.String("job.id", job.ID))

	// Decode binary
	_, decodeSpan := tracer.Start(ctx, "job.dequeue")
	decodeSpan.SetAttributes(attribute.String("job.id", job.ID))
	firmware, err := base64.StdEncoding.DecodeString(job.BinaryB64)
	decodeSpan.End()
	if err != nil {
		slog.Error("base64 decode failed", "job", job.ID, "err", err)
		w.storeError(ctx, job.ID, "error", "invalid base64 binary", raw)
		return
	}

	// Write firmware to temp file
	tmpPath, err := simulator.WriteTempFile(firmware)
	if err != nil {
		slog.Error("temp file creation failed", "job", job.ID, "err", err)
		w.storeError(ctx, job.ID, "error", err.Error(), raw)
		return
	}
	defer os.Remove(tmpPath)

	// Execute simulator
	execCtx, execSpan := tracer.Start(ctx, "job.execute")
	execSpan.SetAttributes(
		attribute.String("job.id", job.ID),
		attribute.Int64("sim.binary_size_bytes", int64(len(firmware))),
	)
	runResult := simulator.Run(execCtx, w.simCfg, tmpPath)
	execSpan.End()

	// Record metrics
	statusAttr := attribute.String("status", runResult.Status)
	w.tel.JobsProcessed.Add(ctx, 1, metric.WithAttributes(statusAttr))
	w.tel.ExecDuration.Record(ctx, float64(runResult.WallDurationMs), metric.WithAttributes(statusAttr))
	if runResult.Sim.Cycles > 0 {
		w.tel.CyclesTotal.Add(ctx, int64(runResult.Sim.Cycles))
	}

	// Build and store result
	result := Result{
		JobID:          job.ID,
		Status:         runResult.Status,
		CompletedAt:    runResult.CompletedAt,
		WallDurationMs: runResult.WallDurationMs,
		Sim:            runResult.Sim,
		ErrorMessage:   runResult.ErrorMessage,
	}

	storeCtx, storeSpan := tracer.Start(ctx, "job.result.store")
	storeSpan.SetAttributes(
		attribute.String("job.id", job.ID),
		attribute.String("result.status", result.Status),
	)
	if err := w.q.StoreResult(storeCtx, job.ID, result); err != nil {
		slog.Error("failed to store result", "job", job.ID, "err", err)
	}
	storeSpan.End()

	w.q.AckDone(ctx, raw)

	// Update stats
	w.q.IncrStats(ctx, "jobs_total", 1)
	if result.Status != "ok" {
		w.q.IncrStats(ctx, "jobs_failed", 1)
	}
	if result.Sim.Cycles > 0 {
		w.q.IncrStats(ctx, "cycles_total", int64(result.Sim.Cycles))
	}

	slog.Info("job completed",
		"job", job.ID,
		"status", result.Status,
		"cycles", result.Sim.Cycles,
		"duration_ms", result.WallDurationMs,
	)
}

func (w *Worker) storeError(ctx context.Context, jobID, status, msg string, raw []byte) {
	result := Result{
		JobID:        jobID,
		Status:       status,
		CompletedAt:  time.Now(),
		ErrorMessage: msg,
	}
	_ = w.q.StoreResult(ctx, jobID, result)
	w.q.AckDone(ctx, raw)
	w.q.IncrStats(ctx, "jobs_total", 1)
	w.q.IncrStats(ctx, "jobs_failed", 1)
}
