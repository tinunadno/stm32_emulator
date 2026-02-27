package models

import (
	"time"
)

// JobState represents the current state of a job
type JobState string

const (
	StateQueued    JobState = "queued"
	StateRunning   JobState = "running"
	StateCompleted JobState = "completed"
	StateFailed    JobState = "failed"
	StateCancelled JobState = "cancelled"
	StateTimeout   JobState = "timeout"
)

// Job represents a simulation job
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
	Metadata       *JSONB     `json:"metadata,omitempty" db:"metadata"`
}

// JSONB represents a JSON/JSONB field for PostgreSQL
type JSONB map[string]interface{}

// CreateJobRequest represents the request body for creating a new job
type CreateJobRequest struct {
	BinaryB64      string `json:"binary_b64" validate:"required"`
	Debug          bool   `json:"debug"`
	TimeoutSeconds int    `json:"timeout_seconds"`
}

// CreateJobResponse represents the response after creating a job
type CreateJobResponse struct {
	JobID     string `json:"job_id"`
	SHA256    string `json:"sha256"`
	Debug     bool   `json:"debug"`
	StatusURL string `json:"status_url"`
	EventsURL string `json:"events_url"`
}

// JobStatusResponse represents the job status response
type JobStatusResponse struct {
	JobID      string     `json:"job_id"`
	State      JobState   `json:"state"`
	WorkerID   *string    `json:"worker_id,omitempty"`
	CreatedAt  time.Time  `json:"created_at"`
	StartedAt  *time.Time `json:"started_at,omitempty"`
	FinishedAt *time.Time `json:"finished_at,omitempty"`
	DebugMode  bool       `json:"debug_mode"`
	ErrorText  *string    `json:"error_text,omitempty"`
}

// GDBInfoResponse represents GDB connection information
type GDBInfoResponse struct {
	JobID            string  `json:"job_id"`
	DebugEnabled     bool    `json:"debug_enabled"`
	GDBHost          *string `json:"gdb_host,omitempty"`
	GDBPort          *int    `json:"gdb_port,omitempty"`
	ConnectionString string  `json:"connection_string,omitempty"`
	Status           string  `json:"status"`
	Connected        bool    `json:"connected"`
}

// EventType represents the type of SSE event
type EventType string

const (
	EventTypeStatus    EventType = "status"
	EventTypeLog       EventType = "log"
	EventTypeTelemetry EventType = "telemetry"
	EventTypeGDBInfo   EventType = "gdb_info"
	EventTypeError     EventType = "error"
)

// Event represents an SSE event
type Event struct {
	Type      EventType              `json:"type"`
	JobID     string                 `json:"job_id"`
	Timestamp time.Time              `json:"timestamp"`
	Data      map[string]interface{} `json:"data"`
}

// StatusEventData represents status event data
type StatusEventData struct {
	State    JobState `json:"state"`
	WorkerID string   `json:"worker_id,omitempty"`
	Debug    bool     `json:"debug"`
	GDB      *GDBInfo `json:"gdb,omitempty"`
}

// GDBInfo represents GDB connection details
type GDBInfo struct {
	Host     string `json:"host"`
	Port     int    `json:"port"`
	Protocol string `json:"protocol"`
}

// LogEventData represents log event data
type LogEventData struct {
	Level   string `json:"level"`
	Message string `json:"message"`
	Source  string `json:"source,omitempty"`
}

// TelemetryEventData represents telemetry event data
type TelemetryEventData struct {
	Type string                 `json:"type"` // gpio, uart, i2c, spi
	Data map[string]interface{} `json:"data"`
}

// Worker represents a worker instance
type Worker struct {
	WorkerID   string    `json:"worker_id" redis:"worker_id"`
	IPAddress  string    `json:"ip_address" redis:"ip_address"`
	LastSeen   time.Time `json:"last_seen" redis:"last_seen"`
	Status     string    `json:"status" redis:"status"`
	CurrentJob *string   `json:"current_job,omitempty" redis:"current_job"`
}

// CancelJobResponse represents the response for job cancellation
type CancelJobResponse struct {
	JobID     string `json:"job_id"`
	Cancelled bool   `json:"cancelled"`
	Message   string `json:"message"`
}
