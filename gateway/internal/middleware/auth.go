package middleware

import (
	"context"
	"net/http"
	"strings"

	"github.com/awesoma/gateway/internal/config"
)

// AuthMiddleware handles API key authentication
type AuthMiddleware struct {
	cfg *config.AuthConfig
}

// NewAuthMiddleware creates a new authentication middleware
func NewAuthMiddleware(cfg *config.AuthConfig) *AuthMiddleware {
	return &AuthMiddleware{
		cfg: cfg,
	}
}

// RequireAPIKey requires a valid API key for access
func (m *AuthMiddleware) RequireAPIKey(next http.Handler) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		apiKey := r.Header.Get(m.cfg.APIKeyHeader)

		// Also check Authorization header with Bearer prefix
		if apiKey == "" {
			authHeader := r.Header.Get("Authorization")
			if strings.HasPrefix(authHeader, "Bearer ") {
				apiKey = strings.TrimPrefix(authHeader, "Bearer ")
			}
		}

		if apiKey == "" {
			http.Error(w, `{"error":"API key required","status":"unauthorized"}`, http.StatusUnauthorized)
			return
		}

		// Validate API key
		if !m.isValidAPIKey(apiKey) {
			http.Error(w, `{"error":"Invalid API key","status":"unauthorized"}`, http.StatusUnauthorized)
			return
		}

		// Extract user ID from API key (in production, this would be a database lookup)
		userID := m.getUserIDFromAPIKey(apiKey)

		// Add user ID to context
		ctx := context.WithValue(r.Context(), "user_id", userID)
		ctx = context.WithValue(ctx, "api_key", apiKey)

		next.ServeHTTP(w, r.WithContext(ctx))
	})
}

// isValidAPIKey checks if the API key is valid
func (m *AuthMiddleware) isValidAPIKey(apiKey string) bool {
	for _, validKey := range m.cfg.ValidAPIKeys {
		if apiKey == validKey {
			return true
		}
	}
	return false
}

// getUserIDFromAPIKey extracts user ID from API key
// In production, this would query a database or cache
func (m *AuthMiddleware) getUserIDFromAPIKey(apiKey string) string {
	// Simple implementation: use the API key as user ID
	// In production, you'd look up the user associated with this key
	return "user_" + apiKey[len(apiKey)-8:]
}

// OptionalAPIKey optionally extracts user info if API key is present
func (m *AuthMiddleware) OptionalAPIKey(next http.Handler) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		apiKey := r.Header.Get(m.cfg.APIKeyHeader)

		if apiKey == "" {
			authHeader := r.Header.Get("Authorization")
			if strings.HasPrefix(authHeader, "Bearer ") {
				apiKey = strings.TrimPrefix(authHeader, "Bearer ")
			}
		}

		if apiKey != "" && m.isValidAPIKey(apiKey) {
			userID := m.getUserIDFromAPIKey(apiKey)
			ctx := context.WithValue(r.Context(), "user_id", userID)
			ctx = context.WithValue(ctx, "api_key", apiKey)
			r = r.WithContext(ctx)
		}

		next.ServeHTTP(w, r)
	})
}

// AdminOnly restricts access to admin users
func (m *AuthMiddleware) AdminOnly(next http.Handler) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		apiKey := r.Header.Get(m.cfg.APIKeyHeader)

		// Check if this is an admin API key
		if !m.isAdminAPIKey(apiKey) {
			http.Error(w, `{"error":"Admin access required","status":"forbidden"}`, http.StatusForbidden)
			return
		}

		next.ServeHTTP(w, r)
	})
}

// isAdminAPIKey checks if the API key has admin privileges
func (m *AuthMiddleware) isAdminAPIKey(apiKey string) bool {
	// In production, you'd check against a database or role-based system
	// For now, we'll consider keys ending with "_admin" as admin keys
	return strings.HasSuffix(apiKey, "_admin")
}
