package repository

import (
	"context"
	"time"

	"github.com/jackc/pgx/v5"
	"github.com/jackc/pgx/v5/pgxpool"

	"github.com/awesoma/stm32-sim/gateway/internal/models"
)

type PostgresRepo struct {
	pool *pgxpool.Pool
}

func NewPostgresRepo(ctx context.Context, dsn string) (*PostgresRepo, error) {
	pool, err := pgxpool.New(ctx, dsn)
	if err != nil {
		return nil, err
	}
	return &PostgresRepo{pool: pool}, nil
}

func (r *PostgresRepo) Close() {
	r.pool.Close()
}

func (r *PostgresRepo) CreateJob(ctx context.Context, job *models.Job) error {
	query := `
		INSERT INTO jobs (job_id, user_id, sha256, state, created_at, timeout_seconds, debug_mode)
		VALUES ($1, $2, $3, $4, $5, $6, $7)
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
	return err
}

func (r *PostgresRepo) GetJob(ctx context.Context, jobID string) (*models.Job, error) {
	query := `
		SELECT job_id, user_id, sha256, state, worker_id, created_at, started_at, 
		       finished_at, timeout_seconds, error_text, debug_mode, gdb_port, 
		       gdb_host, gdb_connected, gdb_connected_at
		FROM jobs WHERE job_id = $1
	`
	job := &models.Job{}
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
	)
	if err != nil {
		return nil, err
	}
	return job, nil
}

func (r *PostgresRepo) UpdateJobState(ctx context.Context, jobID string, state models.JobState, errorText *string) error {
	query := `
		UPDATE jobs SET state = $1, error_text = $2, finished_at = $3 WHERE job_id = $4
	`
	_, err := r.pool.Exec(ctx, query, state, errorText, time.Now(), jobID)
	return err
}

func (r *PostgresRepo) UpdateJobStarted(ctx context.Context, jobID string, workerID string) error {
	query := `
		UPDATE jobs SET state = $1, worker_id = $2, started_at = $3 WHERE job_id = $4
	`
	_, err := r.pool.Exec(ctx, query, models.JobStateRunning, workerID, time.Now(), jobID)
	return err
}

func (r *PostgresRepo) UpdateGDBInfo(ctx context.Context, jobID string, host string, port int) error {
	query := `
		UPDATE jobs SET gdb_host = $1, gdb_port = $2 WHERE job_id = $3
	`
	_, err := r.pool.Exec(ctx, query, host, port, jobID)
	return err
}

func (r *PostgresRepo) UpdateGDBConnected(ctx context.Context, jobID string) error {
	query := `
		UPDATE jobs SET gdb_connected = true, gdb_connected_at = $1 WHERE job_id = $2
	`
	_, err := r.pool.Exec(ctx, query, time.Now(), jobID)
	return err
}

func (r *PostgresRepo) CreateDebugSession(ctx context.Context, jobID string, userID string, gdbPort int, clientIP string) error {
	query := `
		INSERT INTO debug_sessions (job_id, user_id, gdb_port, client_ip, connected_at)
		VALUES ($1, $2, $3, $4, $5)
	`
	_, err := r.pool.Exec(ctx, query, jobID, userID, gdbPort, clientIP, time.Now())
	return err
}

// InitSchema creates the required tables if they don't exist
func (r *PostgresRepo) InitSchema(ctx context.Context) error {
	schema := `
	CREATE TABLE IF NOT EXISTS jobs (
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
		gdb_connected_at TIMESTAMPTZ
	);

	CREATE INDEX IF NOT EXISTS idx_jobs_user_id ON jobs(user_id);
	CREATE INDEX IF NOT EXISTS idx_jobs_state ON jobs(state);
	CREATE INDEX IF NOT EXISTS idx_jobs_debug ON jobs(debug_mode);

	CREATE TABLE IF NOT EXISTS debug_sessions (
		id BIGSERIAL PRIMARY KEY,
		job_id VARCHAR(26) NOT NULL,
		user_id VARCHAR(255) NOT NULL,
		gdb_port INT,
		client_ip INET,
		connected_at TIMESTAMPTZ,
		disconnected_at TIMESTAMPTZ,
		commands_executed INT DEFAULT 0,
		FOREIGN KEY (job_id) REFERENCES jobs(job_id)
	);

	CREATE INDEX IF NOT EXISTS idx_debug_sessions_job_id ON debug_sessions(job_id);
	`
	_, err := r.pool.Exec(ctx, schema)
	return err
}

// Run migration in transaction
func (r *PostgresRepo) RunInTx(ctx context.Context, fn func(pgx.Tx) error) error {
	tx, err := r.pool.Begin(ctx)
	if err != nil {
		return err
	}
	defer tx.Rollback(ctx)

	if err := fn(tx); err != nil {
		return err
	}

	return tx.Commit(ctx)
}
