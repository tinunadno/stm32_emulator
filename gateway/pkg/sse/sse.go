package sse

import (
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"sync"
	"time"
)

// Event represents a Server-Sent Event
type Event struct {
	ID    string
	Event string
	Data  interface{}
}

// Broker manages SSE connections and event distribution
type Broker struct {
	clients    map[string]map[chan []byte]bool
	clientsMux sync.RWMutex
}

// NewBroker creates a new SSE broker
func NewBroker() *Broker {
	return &Broker{
		clients: make(map[string]map[chan []byte]bool),
	}
}

// Subscribe creates a new client channel for a specific job
func (b *Broker) Subscribe(jobID string) chan []byte {
	b.clientsMux.Lock()
	defer b.clientsMux.Unlock()

	if b.clients[jobID] == nil {
		b.clients[jobID] = make(map[chan []byte]bool)
	}

	ch := make(chan []byte, 100)
	b.clients[jobID][ch] = true

	return ch
}

// Unsubscribe removes a client channel
func (b *Broker) Unsubscribe(jobID string, ch chan []byte) {
	b.clientsMux.Lock()
	defer b.clientsMux.Unlock()

	if b.clients[jobID] != nil {
		delete(b.clients[jobID], ch)
		if len(b.clients[jobID]) == 0 {
			delete(b.clients, jobID)
		}
	}
	close(ch)
}

// Publish sends an event to all subscribers for a job
func (b *Broker) Publish(jobID string, data []byte) {
	b.clientsMux.RLock()
	defer b.clientsMux.RUnlock()

	if b.clients[jobID] == nil {
		return
	}

	for ch := range b.clients[jobID] {
		select {
		case ch <- data:
		default:
			// Channel full, skip this client
		}
	}
}

// PublishJSON sends a JSON event to all subscribers
func (b *Broker) PublishJSON(jobID string, event string, data interface{}) error {
	jsonData, err := json.Marshal(data)
	if err != nil {
		return fmt.Errorf("failed to marshal event data: %w", err)
	}

	// Format as SSE
	sseData := formatSSE(event, jsonData)
	b.Publish(jobID, sseData)
	return nil
}

// formatSSE formats data as SSE message
func formatSSE(event string, data []byte) []byte {
	return []byte(fmt.Sprintf("event: %s\ndata: %s\n\n", event, data))
}

// Handler returns an http.Handler for SSE connections
func (b *Broker) Handler(jobIDExtractor func(*http.Request) string) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		jobID := jobIDExtractor(r)
		if jobID == "" {
			http.Error(w, "job_id required", http.StatusBadRequest)
			return
		}

		// Set SSE headers
		w.Header().Set("Content-Type", "text/event-stream")
		w.Header().Set("Cache-Control", "no-cache")
		w.Header().Set("Connection", "keep-alive")
		w.Header().Set("X-Accel-Buffering", "no") // Disable nginx buffering

		// Flush headers
		if f, ok := w.(http.Flusher); ok {
			f.Flush()
		}

		// Subscribe to events
		ch := b.Subscribe(jobID)
		defer b.Unsubscribe(jobID, ch)

		// Send initial connection event
		fmt.Fprintf(w, "event: connected\ndata: {\"job_id\":\"%s\"}\n\n", jobID)
		if f, ok := w.(http.Flusher); ok {
			f.Flush()
		}

		// Keep-alive ticker
		ticker := time.NewTicker(30 * time.Second)
		defer ticker.Stop()

		for {
			select {
			case <-r.Context().Done():
				return

			case data, ok := <-ch:
				if !ok {
					return
				}
				if _, err := w.Write(data); err != nil {
					return
				}
				if f, ok := w.(http.Flusher); ok {
					f.Flush()
				}

			case <-ticker.C:
				// Send keep-alive comment
				if _, err := io.WriteString(w, ": keep-alive\n\n"); err != nil {
					return
				}
				if f, ok := w.(http.Flusher); ok {
					f.Flush()
				}
			}
		}
	}
}

// StreamWriter helps write SSE events to http.ResponseWriter
type StreamWriter struct {
	w       http.ResponseWriter
	flusher http.Flusher
}

// NewStreamWriter creates a new SSE stream writer
func NewStreamWriter(w http.ResponseWriter) (*StreamWriter, error) {
	flusher, ok := w.(http.Flusher)
	if !ok {
		return nil, fmt.Errorf("streaming not supported")
	}

	return &StreamWriter{
		w:       w,
		flusher: flusher,
	}, nil
}

// WriteEvent writes an SSE event to the stream
func (sw *StreamWriter) WriteEvent(event string, data interface{}) error {
	jsonData, err := json.Marshal(data)
	if err != nil {
		return fmt.Errorf("failed to marshal data: %w", err)
	}

	fmt.Fprintf(sw.w, "event: %s\ndata: %s\n\n", event, jsonData)
	sw.flusher.Flush()
	return nil
}

// WriteMessage writes a raw SSE message
func (sw *StreamWriter) WriteMessage(data []byte) error {
	if _, err := sw.w.Write(data); err != nil {
		return err
	}
	sw.flusher.Flush()
	return nil
}

// WriteComment writes an SSE comment (useful for keep-alive)
func (sw *StreamWriter) WriteComment(comment string) error {
	fmt.Fprintf(sw.w, ": %s\n\n", comment)
	sw.flusher.Flush()
	return nil
}
