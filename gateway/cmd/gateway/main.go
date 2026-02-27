package main

import (
	"context"
	"fmt"
	"log"
	"net/http"
	"os"
	"os/signal"
	"syscall"
	"time"

	"github.com/go-chi/chi/v5"
	"github.com/go-chi/chi/v5/middleware"
	"github.com/go-chi/render"
	"github.com/redis/go-redis/v9"

	"github.com/awesoma/gateway/internal/config"
	"github.com/awesoma/gateway/internal/handlers"
	"github.com/awesoma/gateway/internal/middleware"
	"github.com/awesoma/gateway/internal/repository"
	"github.com/awesoma/gateway/internal/service"
	"github.com/awesoma/gateway/pkg/sse"
)

func main() {
	// Load configuration
	cfg, err := config.Load("config.yaml")
	if err != nil {
		log.Fatalf("Failed to load config: %v", err)
	}

	// Create context with timeout for initialization
	ctx, cancel := context.WithTimeout(context.Background(), 30*time.Second)
	defer cancel()

	// Initialize repositories
	pgRepo, err := repository.NewPostgresRepository(
		ctx,
		cfg.Postgres.DSN,
		cfg.Postgres.MaxOpenConns,
		cfg.Postgres.MaxIdleConns,
		cfg.Postgres.ConnMaxLifetime,
	)
	if err != nil {
		log.Fatalf("Failed to connect to PostgreSQL: %v", err)
	}
	defer pgRepo.Close()

	// Run migrations
	if err := pgRepo.RunMigrations(ctx); err != nil {
		log.Fatalf("Failed to run migrations: %v", err)
	}
	log.Println("Database migrations completed")

	// Initialize KeyDB repository
	keyDBRepo, err := repository.NewKeyDBRepository(
		ctx,
		cfg.KeyDB.Addr,
		cfg.KeyDB.Password,
		cfg.KeyDB.DB,
		cfg.KeyDB.PoolSize,
	)
	if err != nil {
		log.Fatalf("Failed to connect to KeyDB: %v", err)
	}
	defer keyDBRepo.Close()

	// Create KeyDB client for handlers
	keyDBClient := redis.NewClient(&redis.Options{
		Addr:     cfg.KeyDB.Addr,
		Password: cfg.KeyDB.Password,
		DB:       cfg.KeyDB.DB,
		PoolSize: cfg.KeyDB.PoolSize,
	})
	defer keyDBClient.Close()

	// Initialize services
	jobService := service.NewJobService(pgRepo, keyDBRepo)

	// Initialize SSE broker
	sseBroker := sse.NewBroker()

	// Initialize handlers
	authMiddleware := middleware.NewAuthMiddleware(&cfg.Auth)
	loggingMiddleware := middleware.NewLoggingMiddleware()

	baseURL := fmt.Sprintf("http://localhost:%d", cfg.Server.HTTPPort)
	jobsHandler := handlers.NewJobsHandler(jobService, baseURL)
	eventsHandler := handlers.NewEventsHandler(jobService, sseBroker, keyDBClient)
	healthHandler := handlers.NewHealthHandler(keyDBClient, pgRepo)

	// Create main router
	r := chi.NewRouter()

	// Global middleware
	r.Use(middleware.RequestID)
	r.Use(middleware.RealIP)
	r.Use(middleware.Recoverer)
	r.Use(middleware.Compress(5))
	r.Use(render.SetContentType(render.ContentTypeJSON))
	r.Use(middleware.CORS())
	r.Use(loggingMiddleware.Logger())

	// Timeout middleware
	r.Use(middleware.Timeout(60 * time.Second))

	// Health endpoints (no auth required)
	r.Route("/health", func(r chi.Router) {
		r.Get("/live", healthHandler.Live)
		r.Get("/ready", healthHandler.Ready)
		r.Get("/details", healthHandler.Detailed)
	})

	// Metrics endpoint
	r.Handle("/metrics", handlers.MetricsHandler())

	// API v1 routes
	r.Route("/v1", func(r chi.Router) {
		// Public info endpoint
		r.Get("/info", func(w http.ResponseWriter, r *http.Request) {
			render.JSON(w, r, map[string]interface{}{
				"service":   "gateway",
				"version":   "1.0.0",
				"endpoints": []string{"/v1/jobs", "/v1/events"},
			})
		})

		// Protected routes (require API key)
		r.Group(func(r chi.Router) {
			r.Use(authMiddleware.RequireAPIKey)

			// Jobs endpoints
			r.Route("/jobs", func(r chi.Router) {
				r.Post("/", jobsHandler.CreateJob)
				r.Get("/", jobsHandler.ListJobs)

				r.Route("/{job_id}", func(r chi.Router) {
					r.Get("/", jobsHandler.GetJob)
					r.Delete("/", jobsHandler.CancelJob)

					// GDB info for debug jobs
					r.Get("/gdb-info", jobsHandler.GetGDBInfo)

					// SSE events endpoint
					r.Get("/events", eventsHandler.StreamEvents)

					// Event history
					r.Get("/events/history", eventsHandler.GetEventHistory)

					// Test event endpoint (for development)
					if os.Getenv("ENV") == "development" {
						r.Post("/events/test", eventsHandler.PublishTestEvent)
					}
				})
			})

			// Admin endpoints
			r.Group(func(r chi.Router) {
				r.Use(authMiddleware.AdminOnly)

				r.Get("/events/all", eventsHandler.StreamAllEvents)
			})
		})
	})

	// Create HTTP server
	server := &http.Server{
		Addr:         fmt.Sprintf(":%d", cfg.Server.HTTPPort),
		Handler:      r,
		ReadTimeout:  cfg.Server.ReadTimeout,
		WriteTimeout: cfg.Server.WriteTimeout,
		IdleTimeout:  cfg.Server.IdleTimeout,
	}

	// Metrics server (separate port)
	metricsMux := chi.NewRouter()
	metricsMux.Handle("/metrics", handlers.MetricsHandler())
	metricsServer := &http.Server{
		Addr:    fmt.Sprintf(":%d", cfg.Server.MetricsPort),
		Handler: metricsMux,
	}

	// Start servers in goroutines
	go func() {
		log.Printf("Starting HTTP server on port %d", cfg.Server.HTTPPort)
		if err := server.ListenAndServe(); err != nil && err != http.ErrServerClosed {
			log.Fatalf("HTTP server error: %v", err)
		}
	}()

	go func() {
		log.Printf("Starting metrics server on port %d", cfg.Server.MetricsPort)
		if err := metricsServer.ListenAndServe(); err != nil && err != http.ErrServerClosed {
			log.Fatalf("Metrics server error: %v", err)
		}
	}()

	log.Println("Gateway service started successfully")

	// Wait for shutdown signal
	quit := make(chan os.Signal, 1)
	signal.Notify(quit, syscall.SIGINT, syscall.SIGTERM)
	<-quit

	log.Println("Shutting down servers...")

	// Graceful shutdown with timeout
	shutdownCtx, shutdownCancel := context.WithTimeout(context.Background(), 30*time.Second)
	defer shutdownCancel()

	if err := server.Shutdown(shutdownCtx); err != nil {
		log.Printf("HTTP server shutdown error: %v", err)
	}

	if err := metricsServer.Shutdown(shutdownCtx); err != nil {
		log.Printf("Metrics server shutdown error: %v", err)
	}

	log.Println("Servers stopped")
}
