package repository

import (
	"context"
	"fmt"
	"time"

	"github.com/jackc/pgx/v5"
	"github.com/jackc/pgx/v5/pgxpool"

	"github.com/awesoma/gateway/internal/models"
)

// PostgresRepository handles database operations
type PostgresRepository struct {
	pool *pgxpool.Pool
}

// NewPostgresRepository creates a new PostgreSQL repository
func NewPostgresRepository(ctx context.Context, dsn string, maxOpenConns, maxIdleConns int, connMaxLifetime time.Duration) (*PostgresRepository, error) {
	config, err := pgxpool.ParseConfig(dsn)
	if err != nil {
		return nil, fmt.Errorf("failed to parse database DSN: %w", err)
	}

	config.MaxConns = int32(maxOpenConns)
	config.MinConns = int32(maxIdleConns)
	config.MaxConnLifetime = connMaxLifetime

	pool, err := pgxpool.NewWithConfig(ctx, config)
	if err != nil {
		return nil, fmt.Errorf("failed to create connection pool: %w", err)
	}

	// Test connection
	if err := pool.Ping(ctx); err != nil {
		return nil, fmt.Errorf("failed to ping database: %w", err)
	}

	return &PostgresRepository{pool: pool}, nil
}

// Close closes the database connection pool
func (r *PostgresRepository) Close() {
	r.pool.Close()
}

// CreateJob creates a new job record in the database
func (r *PostgresRepository) CreateJob(ctx context.Context, job *models.Job) error {
	query := `
		INSERT INTO jobs (
			job_id, user_id, sha256, state, created_at,
			timeout_seconds, debug_mode
		) VALUES ($1, $2, $3, $4, $5, $6, $7)
	`

	_, err := r.pool.Exec(ctx, query,
		job.JobID,
		job.UserID,
		job.SHA256,
		job.State,
		job.CreatedAt,
		job.TimeoutSeconds,
		job.DebugMode,
	)

	if err != nil {
		return fmt.Errorf("failed to create job: %w", err)
	}

	return nil
}

// GetJob retrieves a job by ID
func (r *PostgresRepository) GetJob(ctx context.Context, jobID string) (*models.Job, error) {
	query := `
		SELECT job_id, user_id, sha256, state, worker_id,
			   created_at, started_at, finished_at, timeout_seconds,
			   error_text, debug_mode, gdb_port, gdb_host,
			   gdb_connected, gdb_connected_at, metadata
		FROM jobs
		WHERE job_id = $1
	`

	var job models.Job
	var metadata []byte

	err := r.pool.QueryRow(ctx, query, jobID).Scan(
		&job.JobID,
		&job.UserID,
		&job.SHA256,
		&job.State,
		&job.WorkerID,
		&job.CreatedAt,
		&job.StartedAt,
		&job.FinishedAt,
		&job.TimeoutSeconds,
		&job.ErrorText,
		&job.DebugMode,
		&job.GDBPort,
		&job.GDBHost,
		&job.GDBConnected,
		&job.GDBConnectedAt,
		&metadata,
	)

	if err != nil {
		if err == pgx.ErrNoRows {
			return nil, ErrJobNotFound
		}
		return nil, fmt.Errorf("failed to get job: %w", err)
	}

	return &job, nil
}

// UpdateJobState updates the job state
func (r *PostgresRepository) UpdateJobState(ctx context.Context, jobID string, state models.JobState, errorText *string) error {
	query := `
		UPDATE jobs
		SET state = $2, error_text = $3, finished_at = $4
		WHERE job_id = $1
	`

	var finishedAt *time.Time
	if state == models.StateCompleted || state == models.StateFailed || state == models.StateCancelled || state == models.StateTimeout {
		now := time.Now()
		finishedAt = &now
	}

	_, err := r.pool.Exec(ctx, query, jobID, state, errorText, finishedAt)
	if err != nil {
		return fmt.Errorf("failed to update job state: %w", err)
	}

	return nil
}

// GetJobsByUser retrieves all jobs for a user
func (r *PostgresRepository) GetJobsByUser(ctx context.Context, userID string, limit, offset int) ([]*models.Job, error) {
	query := `
		SELECT job_id, user_id, sha256, state, worker_id,
			   created_at, started_at, finished_at, timeout_seconds,
			   error_text, debug_mode, gdb_port, gdb_host,
			   gdb_connected, gdb_connected_at
		FROM jobs
		WHERE user_id = $1
		ORDER BY created_at DESC
		LIMIT $2 OFFSET $3
	`

	rows, err := r.pool.Query(ctx, query, userID, limit, offset)
	if err != nil {
		return nil, fmt.Errorf("failed to get jobs: %w", err)
	}
	defer rows.Close()

	var jobs []*models.Job
	for rows.Next() {
		var job models.Job
		err := rows.Scan(
			&job.JobID,
			&job.UserID,
			&job.SHA256,
			&job.State,
			&job.WorkerID,
			&job.CreatedAt,
			&job.StartedAt,
			&job.FinishedAt,
			&job.TimeoutSeconds,
			&job.ErrorText,
			&job.DebugMode,
			&job.GDBPort,
			&job.GDBHost,
			&job.GDBConnected,
			&job.GDBConnectedAt,
		)
		if err != nil {
			return nil, fmt.Errorf("failed to scan job: %w", err)
		}
		jobs = append(jobs, &job)
	}

	return jobs, nil
}

// CreateDebugSession logs a debug session
func (r *PostgresRepository) CreateDebugSession(ctx context.Context, jobID, userID string, gdbPort int, clientIP string) error {
	query := `
		INSERT INTO debug_sessions (job_id, user_id, gdb_port, client_ip, connected_at)
		VALUES ($1, $2, $3, $4, $5)
	`

	_, err := r.pool.Exec(ctx, query, jobID, userID, gdbPort, clientIP, time.Now())
	if err != nil {
		return fmt.Errorf("failed to create debug session: %w", err)
	}

	return nil
}

// UpdateDebugSessionDisconnect updates the debug session with disconnect time
func (r *PostgresRepository) UpdateDebugSessionDisconnect(ctx context.Context, jobID string, commandsExecuted int) error {
	query := `
		UPDATE debug_sessions
		SET disconnected_at = $2, commands_executed = $3
		WHERE job_id = $1 AND disconnected_at IS NULL
	`

	_, err := r.pool.Exec(ctx, query, jobID, time.Now(), commandsExecuted)
	if err != nil {
		return fmt.Errorf("failed to update debug session: %w", err)
	}

	return nil
}

// RunMigrations runs database migrations
func (r *PostgresRepository) RunMigrations(ctx context.Context) error {
	migrations := []string{
		`CREATE TABLE IF NOT EXISTS jobs (
			job_id VARCHAR(26) PRIMARY KEY,
			user_id VARCHAR(255) NOT NULL,
			sha256 CHAR(64) NOT NULL,
			state VARCHAR(20) NOT NULL,
			worker_id VARCHAR(255),
			created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
			started_at TIMESTAMPTZ,
			finished_at TIMESTAMPTZ,
			timeout_seconds INT NOT NULL DEFAULT 30,
			error_text TEXT,
			debug_mode BOOLEAN DEFAULT false,
			gdb_port INT,
			gdb_host VARCHAR(255),
			gdb_connected BOOLEAN DEFAULT false,
			gdb_connected_at TIMESTAMPTZ,
			metadata JSONB
		)`,
		`CREATE INDEX IF NOT EXISTS idx_jobs_user_id ON jobs(user_id)`,
		`CREATE INDEX IF NOT EXISTS idx_jobs_state ON jobs(state)`,
		`CREATE INDEX IF NOT EXISTS idx_jobs_debug ON jobs(debug_mode)`,
		`CREATE TABLE IF NOT EXISTS debug_sessions (
			id BIGSERIAL PRIMARY KEY,
			job_id VARCHAR(26) NOT NULL,
			user_id VARCHAR(255) NOT NULL,
			gdb_port INT,
			client_ip INET,
			connected_at TIMESTAMPTZ,
			disconnected_at TIMESTAMPTZ,
			commands_executed INT DEFAULT 0,
			FOREIGN KEY (job_id) REFERENCES jobs(job_id)
		)`,
		`CREATE INDEX IF NOT EXISTS idx_debug_sessions_job_id ON debug_sessions(job_id)`,
	}

	for _, migration := range migrations {
		_, err := r.pool.Exec(ctx, migration)
		if err != nil {
			return fmt.Errorf("failed to run migration: %w", err)
		}
	}

	return nil
}

// Custom errors
var (
	ErrJobNotFound = fmt.Errorf("job not found")
)
