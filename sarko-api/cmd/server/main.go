// Command server runs the sarko-api HTTP service.
package main

import (
	"context"
	"errors"
	"fmt"
	"log/slog"
	"net/http"
	"os"
	"os/signal"
	"syscall"
	"time"

	"github.com/Yasuslik/sarko-api/internal/api"
	"github.com/Yasuslik/sarko-api/internal/auth"
	"github.com/Yasuslik/sarko-api/internal/config"
	"github.com/Yasuslik/sarko-api/internal/db"
	"github.com/Yasuslik/sarko-api/internal/store"
)

func main() {
	if err := run(); err != nil {
		slog.Error("fatal", "err", err)
		os.Exit(1)
	}
}

func run() error {
	cfg, err := config.Load()
	if err != nil {
		return err
	}

	ctx, stop := signal.NotifyContext(context.Background(), syscall.SIGINT, syscall.SIGTERM)
	defer stop()

	if err := db.Migrate(cfg.DatabaseURL); err != nil {
		return err
	}
	pool, err := db.Open(ctx, cfg.DatabaseURL)
	if err != nil {
		return err
	}
	defer pool.Close()

	deps := api.Deps{
		Store:      store.New(pool),
		Issuer:     auth.Issuer{Secret: cfg.JWTSecret, TTL: 30 * 24 * time.Hour},
		RaidTTL:    cfg.RaidTTL,
		PendingTTL: cfg.PendingTTL,
	}

	go store.RunSweeper(ctx, deps.Store, 15*time.Second)

	srv := &http.Server{
		Addr:              ":" + cfg.Port,
		Handler:           api.NewRouter(deps),
		ReadHeaderTimeout: 10 * time.Second,
	}

	if err := serve(ctx, cfg.Port, srv, stop); err != nil {
		return err
	}

	slog.Info("shutting down")
	shutdownCtx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
	defer cancel()
	return srv.Shutdown(shutdownCtx)
}

// serve starts the HTTP server in a goroutine and waits for either a listener
// error or a shutdown signal. If the server fails to listen, it returns the error.
func serve(ctx context.Context, port string, srv *http.Server, stop context.CancelFunc) error {
	listenErr := make(chan error, 1)
	go func() {
		slog.Info("listening", "port", port)
		if err := srv.ListenAndServe(); err != nil && !errors.Is(err, http.ErrServerClosed) {
			listenErr <- err
		}
	}()

	select {
	case err := <-listenErr:
		return fmt.Errorf("listen: %w", err)
	case <-ctx.Done():
		return nil
	}
}
