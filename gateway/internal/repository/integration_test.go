//go:build integration
// +build integration

package repository

import (
	"context"
	"os"
	"testing"
	"time"

	"github.com/awesoma/gateway/internal/models"
)

// Run with: go test -tags=integration ./...
// Requires running databases: docker-compose -f docker-compose.test.yml up -d

func TestPostgresRepository_Integration(t *testing.T) {
	dsn := os.Getenv("TEST_DB_DSN")
	if dsn == "" {
		dsn = "postgres://lab:secret@localhost:5432/lab_test?sslmode=disable"
	}

	ctx, cancel := context.WithTimeout(context.Background(), 30*time.Second)
	defer cancel()

	repo, err := NewPostgresRepository(ctx, dsn, 5, 2, 5*time.Minute)
	if err != nil {
		t.Fatalf("failed to create repository: %v", err)
	}
	defer repo.Close()

	// Run migrations
	if err := repo.RunMigrations(ctx); err != nil {
		t.Fatalf("failed to run migrations: %v", err)
	}

	t.Run("CreateJob", func(t *testing.T) {
		job := &models.Job{
			JobID:          "test-integration-1",
			UserID:         "test-user",
			SHA256:         "abc123def456",
			State:          models.StateQueued,
			CreatedAt:      time.Now(),
			TimeoutSeconds: 30,
			DebugMode:      false,
		}

		err := repo.CreateJob(ctx, job)
		if err != nil {
			t.Fatalf("failed to create job: %v", err)
		}

		// Cleanup
		defer func() {
			// In real test, you'd have a DeleteJob method
		}()
	})

	t.Run("GetJob", func(t *testing.T) {
		// First create a job
		job := &models.Job{
			JobID:          "test-integration-2",
			UserID:         "test-user",
			SHA256:         "xyz789",
			State:          models.StateQueued,
			CreatedAt:      time.Now(),
			TimeoutSeconds: 30,
			DebugMode:      true,
		}
		repo.CreateJob(ctx, job)

		// Then retrieve it
		retrieved, err := repo.GetJob(ctx, "test-integration-2")
		if err != nil {
			t.Fatalf("failed to get job: %v", err)
		}

		if retrieved.JobID != job.JobID {
			t.Errorf("expected job_id %s, got %s", job.JobID, retrieved.JobID)
		}
		if retrieved.DebugMode != true {
			t.Error("expected debug_mode to be true")
		}
	})

	t.Run("GetJob_NotFound", func(t *testing.T) {
		_, err := repo.GetJob(ctx, "non-existent-job")
		if err != ErrJobNotFound {
			t.Errorf("expected ErrJobNotFound, got: %v", err)
		}
	})

	t.Run("UpdateJobState", func(t *testing.T) {
		// Create job
		job := &models.Job{
			JobID:          "test-integration-3",
			UserID:         "test-user",
			SHA256:         "aaa111",
			State:          models.StateQueued,
			CreatedAt:      time.Now(),
			TimeoutSeconds: 30,
		}
		repo.CreateJob(ctx, job)

		// Update state
		err := repo.UpdateJobState(ctx, "test-integration-3", models.StateRunning, nil)
		if err != nil {
			t.Fatalf("failed to update job state: %v", err)
		}

		// Verify
		updated, _ := repo.GetJob(ctx, "test-integration-3")
		if updated.State != models.StateRunning {
			t.Errorf("expected state %s, got %s", models.StateRunning, updated.State)
		}
	})

	t.Run("GetJobsByUser", func(t *testing.T) {
		// Create multiple jobs for same user
		for i := 0; i < 3; i++ {
			job := &models.Job{
				JobID:          "test-user-jobs-" + string(rune('a'+i)),
				UserID:         "test-user-list",
				SHA256:         "hash",
				State:          models.StateQueued,
				CreatedAt:      time.Now(),
				TimeoutSeconds: 30,
			}
			repo.CreateJob(ctx, job)
		}

		jobs, err := repo.GetJobsByUser(ctx, "test-user-list", 10, 0)
		if err != nil {
			t.Fatalf("failed to get jobs: %v", err)
		}

		if len(jobs) < 3 {
			t.Errorf("expected at least 3 jobs, got %d", len(jobs))
		}
	})
}

func TestKeyDBRepository_Integration(t *testing.T) {
	addr := os.Getenv("TEST_KEYDB_ADDR")
	if addr == "" {
		addr = "localhost:6379"
	}

	ctx, cancel := context.WithTimeout(context.Background(), 30*time.Second)
	defer cancel()

	repo, err := NewKeyDBRepository(ctx, addr, "", 0, 10)
	if err != nil {
		t.Fatalf("failed to create repository: %v", err)
	}
	defer repo.Close()

	t.Run("EnqueueJob", func(t *testing.T) {
		job := &models.Job{
			JobID:          "test-keydb-1",
			UserID:         "test-user",
			SHA256:         "abc123",
			State:          models.StateQueued,
			CreatedAt:      time.Now(),
			TimeoutSeconds: 30,
			DebugMode:      true,
		}

		err := repo.EnqueueJob(ctx, job)
		if err != nil {
			t.Fatalf("failed to enqueue job: %v", err)
		}
	})

	t.Run("GetJobHash", func(t *testing.T) {
		// First enqueue
		job := &models.Job{
			JobID:     "test-keydb-2",
			UserID:    "test-user",
			SHA256:    "xyz789",
			State:     models.StateQueued,
			CreatedAt: time.Now(),
			DebugMode: false,
		}
		repo.EnqueueJob(ctx, job)

		// Then retrieve
		hash, err := repo.GetJobHash(ctx, "test-keydb-2")
		if err != nil {
			t.Fatalf("failed to get job hash: %v", err)
		}

		if hash.UserID != "test-user" {
			t.Errorf("expected user_id test-user, got %s", hash.UserID)
		}
	})

	t.Run("PublishEvent", func(t *testing.T) {
		event := &models.Event{
			Type:      models.EventTypeStatus,
			JobID:     "test-keydb-3",
			Timestamp: time.Now(),
			Data: map[string]interface{}{
				"state": "running",
			},
		}

		err := repo.PublishEvent(ctx, "test-keydb-3", event)
		if err != nil {
			t.Fatalf("failed to publish event: %v", err)
		}
	})

	t.Run("CancelJob", func(t *testing.T) {
		// First enqueue
		job := &models.Job{
			JobID:     "test-keydb-cancel",
			UserID:    "test-user",
			SHA256:    "cancel",
			State:     models.StateRunning,
			CreatedAt: time.Now(),
		}
		repo.EnqueueJob(ctx, job)

		// Then cancel
		err := repo.CancelJob(ctx, "test-keydb-cancel")
		if err != nil {
			t.Fatalf("failed to cancel job: %v", err)
		}

		// Verify state changed
		hash, _ := repo.GetJobHash(ctx, "test-keydb-cancel")
		if hash.State != string(models.StateCancelled) {
			t.Errorf("expected state %s, got %s", models.StateCancelled, hash.State)
		}
	})
}
