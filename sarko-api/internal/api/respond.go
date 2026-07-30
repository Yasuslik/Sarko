package api

import (
	"encoding/json"
	"errors"
	"log/slog"
	"net/http"
)

// maxBodyBytes caps every request body this service will read.
//
// POST /v1/auth/anonymous is unauthenticated by design and the service runs on
// a public HTTPS domain, so an unbounded json.Decoder is a free way for anyone
// to make the process allocate. 64 KB is far larger than any legitimate
// request here (the biggest is a raid result, a few dozen item stacks) and
// small enough that a flood costs the attacker more than it costs us.
const maxBodyBytes = 64 << 10

type errorBody struct {
	Error errorDetail `json:"error"`
}

type errorDetail struct {
	Code    string `json:"code"`
	Message string `json:"message"`
}

// WriteJSON writes v as JSON with the given status.
func WriteJSON(w http.ResponseWriter, status int, v any) {
	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(status)
	if v == nil {
		return
	}
	if err := json.NewEncoder(w).Encode(v); err != nil {
		slog.Error("write json", "err", err)
	}
}

// WriteError writes the single error envelope used by every endpoint.
func WriteError(w http.ResponseWriter, status int, code, message string) {
	WriteJSON(w, status, errorBody{Error: errorDetail{Code: code, Message: message}})
}

// decodeJSON reads at most maxBodyBytes from r's body and decodes it into v.
// It is the only place any handler reads a request body, so the cap cannot be
// forgotten on a new endpoint.
//
// On failure it has already written the response and returns false: an
// oversized body is a client error, so it gets the same 400 envelope as
// malformed JSON rather than surfacing as a 500.
func decodeJSON(w http.ResponseWriter, r *http.Request, v any) bool {
	r.Body = http.MaxBytesReader(w, r.Body, maxBodyBytes)
	if err := json.NewDecoder(r.Body).Decode(v); err != nil {
		var tooLarge *http.MaxBytesError
		if errors.As(err, &tooLarge) {
			WriteError(w, http.StatusBadRequest, "bad_request", "body is too large")
			return false
		}
		WriteError(w, http.StatusBadRequest, "bad_request", "body must be JSON")
		return false
	}
	return true
}
