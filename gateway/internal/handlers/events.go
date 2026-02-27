package handlers

import (
	"context"
	"encoding/json"
	"net/http"
	"time"

	"github.com/go-chi/chi/v5"
	"github.com/redis/go-redis/v9"

	"github.com/awesoma/gateway/internal/models"
	"github.com/awesoma/gateway/internal/service"
	"github.com/awesoma/gateway/pkg/sse"
)

// EventsHandler handles SSE event streaming
type EventsHandler struct {
	jobService service.JobService
	sseBroker  *sse.Broker
	keyDB      *redis.Client
}

// NewEventsHandler creates a new events handler
func NewEventsHandler(jobService service.JobService, broker *sse.Broker, keyDB *redis.Client) *EventsHandler {
	return &EventsHandler{
		jobService: jobService,
		sseBroker:  broker,
		keyDB:      keyDB,
	}
}

// StreamEvents handles GET /v1/jobs/{job_id}/events (SSE endpoint)
func (h *EventsHandler) StreamEvents(w http.ResponseWriter, r *http.Request) {
	jobID := chi.URLParam(r, "job_id")
	if jobID == "" {
		http.Error(w, "job_id required", http.StatusBadRequest)
		return
	}

	// Verify job exists and user has access
	job, err := h.jobService.GetJob(r.Context(), jobID)
	if err != nil {
		if err == service.ErrJobNotFound {
			http.Error(w, "Job not found", http.StatusNotFound)
			return
		}
		http.Error(w, "Internal server error", http.StatusInternalServerError)
		return
	}

	userID := getUserID(r)
	if job.UserID != userID {
		http.Error(w, "Forbidden", http.StatusForbidden)
		return
	}

	// Set SSE headers
	w.Header().Set("Content-Type", "text/event-stream")
	w.Header().Set("Cache-Control", "no-cache")
	w.Header().Set("Connection", "keep-alive")
	w.Header().Set("X-Accel-Buffering", "no")

	// Flush headers
	if f, ok := w.(http.Flusher); ok {
		f.Flush()
	}

	// Create stream writer
	sw, err := sse.NewStreamWriter(w)
	if err != nil {
		http.Error(w, "Streaming not supported", http.StatusInternalServerError)
		return
	}

	// Send initial connection event
	sw.WriteEvent("connected", map[string]string{
		"job_id": jobID,
		"time":   time.Now().Format(time.RFC3339),
	})

	// Subscribe to KeyDB events
	pubsub := h.keyDB.Subscribe(r.Context(), "events:job:"+jobID)
	defer pubsub.Close()

	// Also subscribe to local broker (for gateway-generated events)
	localCh := h.sseBroker.Subscribe(jobID)
	defer h.sseBroker.Unsubscribe(jobID, localCh)

	// Get message channel from pubsub
	msgCh := pubsub.Channel()

	// Keep-alive ticker
	ticker := time.NewTicker(30 * time.Second)
	defer ticker.Stop()

	for {
		select {
		case <-r.Context().Done():
			// Client disconnected
			return

		case msg, ok := <-msgCh:
			if !ok {
				return
			}
			// Parse and forward the event
			var event models.Event
			if err := json.Unmarshal([]byte(msg.Payload), &event); err != nil {
				// Forward raw data if parsing fails
				sw.WriteEvent("message", map[string]string{"data": msg.Payload})
			} else {
				sw.WriteEvent(string(event.Type), event)
			}

		case data, ok := <-localCh:
			if !ok {
				return
			}
			// Forward local broker messages
			w.Write(data)
			if f, ok := w.(http.Flusher); ok {
				f.Flush()
			}

		case <-ticker.C:
			// Send keep-alive
			sw.WriteComment("keep-alive")
		}
	}
}

// StreamAllEvents handles GET /v1/events (admin/monitoring SSE endpoint)
func (h *EventsHandler) StreamAllEvents(w http.ResponseWriter, r *http.Request) {
	// This is an admin endpoint - should be protected
	// For now, we'll just implement the streaming logic

	// Set SSE headers
	w.Header().Set("Content-Type", "text/event-stream")
	w.Header().Set("Cache-Control", "no-cache")
	w.Header().Set("Connection", "keep-alive")
	w.Header().Set("X-Accel-Buffering", "no")

	if f, ok := w.(http.Flusher); ok {
		f.Flush()
	}

	sw, err := sse.NewStreamWriter(w)
	if err != nil {
		http.Error(w, "Streaming not supported", http.StatusInternalServerError)
		return
	}

	sw.WriteEvent("connected", map[string]string{
		"message": "Connected to all events stream",
		"time":    time.Now().Format(time.RFC3339),
	})

	// Subscribe to all job events
	pubsub := h.keyDB.PSubscribe(r.Context(), "events:job:*")
	defer pubsub.Close()

	msgCh := pubsub.Channel()
	ticker := time.NewTicker(30 * time.Second)
	defer ticker.Stop()

	for {
		select {
		case <-r.Context().Done():
			return

		case msg, ok := <-msgCh:
			if !ok {
				return
			}
			// Extract job ID from channel name
			// Format: events:job:{job_id}
			var eventData map[string]interface{}
			if err := json.Unmarshal([]byte(msg.Payload), &eventData); err != nil {
				continue
			}
			eventData["channel"] = msg.Channel
			sw.WriteEvent("event", eventData)

		case <-ticker.C:
			sw.WriteComment("keep-alive")
		}
	}
}

// PublishTestEvent handles POST /v1/jobs/{job_id}/events/test (for testing)
func (h *EventsHandler) PublishTestEvent(w http.ResponseWriter, r *http.Request) {
	jobID := chi.URLParam(r, "job_id")
	if jobID == "" {
		http.Error(w, "job_id required", http.StatusBadRequest)
		return
	}

	// Create test event
	event := models.Event{
		Type:      models.EventTypeLog,
		JobID:     jobID,
		Timestamp: time.Now(),
		Data: map[string]interface{}{
			"level":   "info",
			"message": "Test event from gateway",
			"source":  "gateway",
		},
	}

	// Publish to local broker
	h.sseBroker.PublishJSON(jobID, string(event.Type), event)

	// Also publish to KeyDB for persistence/other subscribers
	eventData, _ := json.Marshal(event)
	h.keyDB.Publish(r.Context(), "events:job:"+jobID, eventData)

	w.WriteHeader(http.StatusOK)
	json.NewEncoder(w).Encode(map[string]string{
		"status": "event published",
		"job_id": jobID,
	})
}

// GetEventHistory handles GET /v1/jobs/{job_id}/events/history
func (h *EventsHandler) GetEventHistory(w http.ResponseWriter, r *http.Request) {
	jobID := chi.URLParam(r, "job_id")
	if jobID == "" {
		http.Error(w, "job_id required", http.StatusBadRequest)
		return
	}

	// Verify job exists and user has access
	job, err := h.jobService.GetJob(r.Context(), jobID)
	if err != nil {
		if err == service.ErrJobNotFound {
			http.Error(w, "Job not found", http.StatusNotFound)
			return
		}
		http.Error(w, "Internal server error", http.StatusInternalServerError)
		return
	}

	userID := getUserID(r)
	if job.UserID != userID {
		http.Error(w, "Forbidden", http.StatusForbidden)
		return
	}

	// Get event history from KeyDB (if stored)
	// This is optional - events might not be persisted
	ctx := context.Background()
	events, err := h.keyDB.LRange(ctx, "events:history:"+jobID, 0, 100).Result()
	if err != nil {
		http.Error(w, "Failed to get event history", http.StatusInternalServerError)
		return
	}

	var eventList []map[string]interface{}
	for _, e := range events {
		var event map[string]interface{}
		if err := json.Unmarshal([]byte(e), &event); err == nil {
			eventList = append(eventList, event)
		}
	}

	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(map[string]interface{}{
		"job_id": jobID,
		"events": eventList,
		"count":  len(eventList),
	})
}
