package models

import (
	"time"
)

type JobState string

const (
	JobStateQueued    JobState = "queued"
	JobStateRunning   JobState = "running"
	JobStateCompleted JobState = "completed"
	JobStateFailed    JobState = "failed"
	JobStateCancelled JobState = "cancelled"
)

type Job struct {
	JobID          string     `json:"job_id" db:"job_id"`
	UserID         string     `json:"user_id" db:"user_id"`
	SHA256         string     `json:"sha256" db:"sha256"`
	State          JobState   `json:"state" db:"state"`
	WorkerID       *string    `json:"worker_id,omitempty" db:"worker_id"`
	CreatedAt      time.Time  `json:"created_at" db:"created_at"`
	StartedAt      *time.Time `json:"started_at,omitempty" db:"started_at"`
	FinishedAt     *time.Time `json:"finished_at,omitempty" db:"finished_at"`
	TimeoutSeconds int        `json:"timeout_seconds" db:"timeout_seconds"`
	ErrorText      *string    `json:"error_text,omitempty" db:"error_text"`
	DebugMode      bool       `json:"debug_mode" db:"debug_mode"`
	GDBPort        *int       `json:"gdb_port,omitempty" db:"gdb_port"`
	GDBHost        *string    `json:"gdb_host,omitempty" db:"gdb_host"`
	GDBConnected   bool       `json:"gdb_connected" db:"gdb_connected"`
	GDBConnectedAt *time.Time `json:"gdb_connected_at,omitempty" db:"gdb_connected_at"`
}

type CreateJobRequest struct {
	BinaryB64      string `json:"binary_b64"`
	Debug          bool   `json:"debug"`
	TimeoutSeconds int    `json:"timeout_seconds"`
}

type CreateJobResponse struct {
	JobID     string `json:"job_id"`
	SHA256    string `json:"sha256"`
	Debug     bool   `json:"debug"`
	StatusURL string `json:"status_url"`
	EventsURL string `json:"events_url"`
}

type GDBInfo struct {
	JobID            string `json:"job_id"`
	DebugEnabled     bool   `json:"debug_enabled"`
	GDBHost          string `json:"gdb_host,omitempty"`
	GDBPort          int    `json:"gdb_port,omitempty"`
	ConnectionString string `json:"connection_string,omitempty"`
	Status           string `json:"status"`
	Connected        bool   `json:"connected"`
}

type Event struct {
	Type      string      `json:"type"`
	Timestamp time.Time   `json:"timestamp"`
	Data      interface{} `json:"data,omitempty"`

	// For status events
	State *JobState `json:"state,omitempty"`
	Debug *bool     `json:"debug,omitempty"`
	GDB   *GDBInfo  `json:"gdb,omitempty"`

	// For log events
	Message *string `json:"message,omitempty"`
}

type SSEEvent struct {
	Event string
	Data  []byte
}
