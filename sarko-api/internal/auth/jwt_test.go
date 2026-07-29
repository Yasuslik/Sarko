package auth

import (
	"crypto/subtle"
	"testing"
	"time"
)

func TestIssueThenVerifyRoundTrips(t *testing.T) {
	iss := Issuer{Secret: []byte("test-secret"), TTL: time.Hour}

	token, err := iss.Issue("player-123")
	if err != nil {
		t.Fatalf("Issue: %v", err)
	}
	got, err := iss.Verify(token)
	if err != nil {
		t.Fatalf("Verify: %v", err)
	}
	if got != "player-123" {
		t.Errorf("player id = %q, want player-123", got)
	}
}

func TestVerifyRejectsWrongSecret(t *testing.T) {
	good := Issuer{Secret: []byte("right"), TTL: time.Hour}
	bad := Issuer{Secret: []byte("wrong"), TTL: time.Hour}

	token, err := good.Issue("player-1")
	if err != nil {
		t.Fatalf("Issue: %v", err)
	}
	if _, err := bad.Verify(token); err == nil {
		t.Fatal("expected verification to fail with a different secret")
	}
}

func TestVerifyRejectsExpiredToken(t *testing.T) {
	iss := Issuer{Secret: []byte("s"), TTL: -time.Minute} // already expired

	token, err := iss.Issue("player-1")
	if err != nil {
		t.Fatalf("Issue: %v", err)
	}
	if _, err := iss.Verify(token); err == nil {
		t.Fatal("expected expired token to be rejected")
	}
}

func TestSessionTokenHashIsStableAndSecretIsRandom(t *testing.T) {
	plainA, hashA, err := NewSessionToken()
	if err != nil {
		t.Fatalf("NewSessionToken: %v", err)
	}
	plainB, _, err := NewSessionToken()
	if err != nil {
		t.Fatalf("NewSessionToken: %v", err)
	}

	if plainA == plainB {
		t.Fatal("two session tokens must never be equal")
	}
	if subtle.ConstantTimeCompare(hashA, HashToken(plainA)) != 1 {
		t.Error("HashToken must reproduce the hash returned by NewSessionToken")
	}
	if subtle.ConstantTimeCompare(hashA, HashToken(plainB)) == 1 {
		t.Error("different tokens must hash differently")
	}
}
