package sse

import (
	"encoding/json"
	"fmt"
	"net/http"
	"time"
)

// Event represents a Server-Sent Event
type Event struct {
	Type      string      `json:"type"`
	Timestamp time.Time   `json:"timestamp"`
	Data      interface{} `json:"data,omitempty"`
}

// SSEHandler handles Server-Sent Events for job updates
type SSEHandler struct{}

func NewSSEHandler() *SSEHandler {
	return &SSEHandler{}
}

// ServeHTTP handles SSE connections for job events
func (h *SSEHandler) ServeHTTP(w http.ResponseWriter, r *http.Request) {
	// Get jobID from URL path
	jobID := r.PathValue("jobID")
	if jobID == "" {
		http.Error(w, "job_id required", http.StatusBadRequest)
		return
	}

	// Set SSE headers
	w.Header().Set("Content-Type", "text/event-stream")
	w.Header().Set("Cache-Control", "no-cache")
	w.Header().Set("Connection", "keep-alive")
	w.Header().Set("Access-Control-Allow-Origin", "*")

	// Flush headers
	if flusher, ok := w.(http.Flusher); ok {
		flusher.Flush()
	}

	// Create ticker for keepalive
	ticker := time.NewTicker(15 * time.Second)
	defer ticker.Stop()

	// Send initial connection event
	connectedEvent := &Event{
		Type:      "connected",
		Timestamp: time.Now(),
	}
	if err := h.writeEvent(w, connectedEvent); err != nil {
		return
	}

	// Note: In a real implementation, you would subscribe to KeyDB Pub/Sub
	// For now, we just send keepalive events
	for {
		select {
		case <-ticker.C:
			keepalive := &Event{
				Type:      "keepalive",
				Timestamp: time.Now(),
			}
			if err := h.writeEvent(w, keepalive); err != nil {
				return
			}
		case <-r.Context().Done():
			return
		}
	}
}

// writeEvent writes a single SSE event
func (h *SSEHandler) writeEvent(w http.ResponseWriter, event *Event) error {
	data, err := json.Marshal(event)
	if err != nil {
		return err
	}

	fmt.Fprintf(w, "event: %s\n", event.Type)
	fmt.Fprintf(w, "data: %s\n\n", string(data))

	// Flush the response
	if flusher, ok := w.(http.Flusher); ok {
		flusher.Flush()
	}

	return nil
}
