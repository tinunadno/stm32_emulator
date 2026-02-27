package middleware

import (
	"net/http"
	"time"

	"github.com/go-chi/chi/v5/middleware"
	"github.com/go-chi/cors"
	"go.opentelemetry.io/otel/trace"
)

// LoggingMiddleware logs HTTP requests
type LoggingMiddleware struct {
	// Add logger interface if needed
}

// NewLoggingMiddleware creates a new logging middleware
func NewLoggingMiddleware() *LoggingMiddleware {
	return &LoggingMiddleware{}
}

// Logger returns a chi middleware for request logging
func (m *LoggingMiddleware) Logger() func(http.Handler) http.Handler {
	return middleware.Logger
}

// RequestLogger logs detailed request information
func (m *LoggingMiddleware) RequestLogger(next http.Handler) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		start := time.Now()

		// Wrap response writer to capture status code
		ww := middleware.NewWrapResponseWriter(w, r.ProtoMajor)

		// Process request
		next.ServeHTTP(ww, r)

		// Log after request is processed
		duration := time.Since(start)
		status := ww.Status()

		// Get trace ID if available
		traceID := ""
		if span := trace.SpanFromContext(r.Context()); span != nil {
			traceID = span.SpanContext().TraceID().String()
		}

		// Structured logging (you can replace with your logger)
		logFields := map[string]interface{}{
			"method":      r.Method,
			"path":        r.URL.Path,
			"status":      status,
			"duration_ms": duration.Milliseconds(),
			"remote_addr": r.RemoteAddr,
			"user_agent":  r.UserAgent(),
			"request_id":  middleware.GetReqID(r.Context()),
		}

		if traceID != "" {
			logFields["trace_id"] = traceID
		}

		// In production, use proper structured logging like zap or zerolog
		// For now, we'll just use the default chi logger
		_ = logFields
	})
}

// Recoverer recovers from panics and logs them
func Recoverer(next http.Handler) http.Handler {
	return middleware.Recoverer(next)
}

// RequestID adds a unique request ID to each request
func RequestID(next http.Handler) http.Handler {
	return middleware.RequestID(next)
}

// Timeout sets a timeout for the request
func Timeout(timeout time.Duration) func(http.Handler) http.Handler {
	return func(next http.Handler) http.Handler {
		return http.TimeoutHandler(next, timeout, `{"error":"Request timeout","status":"timeout"}`)
	}
}

// CORS returns a CORS middleware with default settings
func CORS() func(http.Handler) http.Handler {
	return cors.Handler(cors.Options{
		AllowedOrigins:   []string{"*"},
		AllowedMethods:   []string{"GET", "POST", "PUT", "DELETE", "OPTIONS"},
		AllowedHeaders:   []string{"Accept", "Authorization", "Content-Type", "X-API-Key", "X-Request-ID"},
		ExposedHeaders:   []string{"Link", "X-Request-ID"},
		AllowCredentials: true,
		MaxAge:           300,
	})
}
