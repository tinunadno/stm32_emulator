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

	"github.com/awesoma/stm32-sim/gateway/internal/config"
	"github.com/awesoma/stm32-sim/gateway/internal/handlers"
	"github.com/awesoma/stm32-sim/gateway/internal/repository"
	"github.com/awesoma/stm32-sim/gateway/internal/service"
	"github.com/awesoma/stm32-sim/gateway/pkg/sse"
)

func main() {
	// Load config
	configPath := os.Getenv("CONFIG_PATH")
	if configPath == "" {
		configPath = "config.yaml"
	}

	cfg, err := config.Load(configPath)
	if err != nil {
		log.Printf("Warning: could not load config file: %v, using defaults", err)
		cfg = config.Default()
	}

	// Initialize repositories
	ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
	defer cancel()

	// Connect to Postgres
	pgRepo, err := repository.NewPostgresRepo(ctx, cfg.Database.DSN())
	if err != nil {
		log.Fatalf("Failed to connect to Postgres: %v", err)
	}
	defer pgRepo.Close()

	// Initialize database schema
	if err := pgRepo.InitSchema(ctx); err != nil {
		log.Fatalf("Failed to initialize database schema: %v", err)
	}

	// Connect to KeyDB
	keydbRepo := repository.NewKeyDBRepo(cfg.KeyDB.Addr, cfg.KeyDB.Password, cfg.KeyDB.DB)
	defer keydbRepo.Close()

	if err := keydbRepo.Ping(ctx); err != nil {
		log.Fatalf("Failed to connect to KeyDB: %v", err)
	}

	// Initialize services
	jobService := service.NewJobService(pgRepo, keydbRepo)

	// Initialize handlers
	jobsHandler := handlers.NewJobsHandler(jobService)
	healthHandler := handlers.NewHealthHandler()
	sseHandler := sse.NewSSEHandler()

	// Setup router
	r := chi.NewRouter()
	r.Use(middleware.Logger)
	r.Use(middleware.Recoverer)
	r.Use(middleware.RequestID)
	r.Use(middleware.Timeout(60 * time.Second))

	// Routes
	r.Route("/v1", func(r chi.Router) {
		r.Mount("/health", healthHandler.Routes())
		r.Mount("/jobs", jobsHandler.Routes())
	})

	// SSE endpoint for job events
	r.Get("/v1/jobs/{jobID}/events", sseHandler.ServeHTTP)

	// Start server
	addr := fmt.Sprintf("%s:%d", cfg.Server.Host, cfg.Server.Port)
	srv := &http.Server{
		Addr:    addr,
		Handler: r,
	}

	// Graceful shutdown
	done := make(chan os.Signal, 1)
	signal.Notify(done, os.Interrupt, syscall.SIGTERM)

	go func() {
		<-done
		log.Println("Shutting down server...")
		shutdownCtx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
		defer cancel()
		srv.Shutdown(shutdownCtx)
		log.Println("Server stopped")
	}()

	log.Printf("Starting gateway server on %s", addr)
	if err := srv.ListenAndServe(); err != nil {
		log.Fatalf("Server error: %v", err)
	}
}
