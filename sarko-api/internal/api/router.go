package api

import "net/http"

// Deps carries everything the handlers need. Later tasks add fields.
type Deps struct{}

// NewRouter builds the HTTP route table.
func NewRouter(deps Deps) http.Handler {
	mux := http.NewServeMux()

	mux.HandleFunc("GET /healthz", func(w http.ResponseWriter, r *http.Request) {
		WriteJSON(w, http.StatusOK, map[string]string{"status": "ok"})
	})

	// Anything unmatched gets the JSON error envelope, not Go's text 404.
	mux.HandleFunc("/", func(w http.ResponseWriter, r *http.Request) {
		WriteError(w, http.StatusNotFound, "not_found", "no such endpoint")
	})

	return mux
}
