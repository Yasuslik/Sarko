package main

import (
	"context"
	"errors"
	"net"
	"net/http"
	"testing"
	"time"
)

func TestServeListenerError(t *testing.T) {
	// Bind a port first to ensure the listener will fail
	listener, err := net.Listen("tcp", ":0")
	if err != nil {
		t.Fatalf("failed to bind port: %v", err)
	}
	defer listener.Close()

	// Get the bound address
	addr := listener.Addr().(*net.TCPAddr)
	port := addr.Port

	// Create a server on the same port
	srv := &http.Server{
		Addr:              net.JoinHostPort("", ""),
		Handler:           http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {}),
		ReadHeaderTimeout: 10 * time.Second,
	}
	srv.Addr = listener.Addr().String()

	// Create a context for the test
	ctx, cancel := context.WithCancel(context.Background())

	// Call serve and expect it to return an error
	// (the port is already bound, so ListenAndServe should fail)
	err = serve(ctx, "", srv, cancel)

	if err == nil {
		t.Fatal("expected serve to return an error when listener fails, got nil")
	}

	if !errors.Is(err, net.ErrClosed) && !errors.Is(err, errors.New("listen")) {
		// The error should be related to the port being in use
		t.Logf("got expected listener error: %v", err)
	}

	_ = port // silence unused warning if needed
}

func TestServeContextCancel(t *testing.T) {
	// Create a server on a free port
	srv := &http.Server{
		Addr:              ":0",
		Handler:           http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {}),
		ReadHeaderTimeout: 10 * time.Second,
	}

	// Create a context that we can cancel
	ctx, cancel := context.WithCancel(context.Background())

	// Cancel immediately after a short delay
	go func() {
		time.Sleep(100 * time.Millisecond)
		cancel()
	}()

	// Call serve and expect it to return nil when context is done
	err := serve(ctx, "0", srv, cancel)

	if err != nil {
		t.Fatalf("expected serve to return nil on context cancellation, got: %v", err)
	}
}
