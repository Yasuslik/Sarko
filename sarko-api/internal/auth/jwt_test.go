package auth

import (
	"crypto/subtle"
	"testing"
	"time"

	"github.com/golang-jwt/jwt/v5"
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

// TestVerifyRejectsUnexpectedSigningMethod covers the canonical JWT
// vulnerability class: algorithm confusion.
//
// jwt.WithValidMethods in Verify already makes this correct, so this test does
// not fix anything — it stops the option being dropped later as a
// "simplification". The two attacks it pins:
//
//   - alg "none": the attacker strips the signature entirely and, without a
//     method allow-list, the parser accepts an unsigned token. Every player id
//     becomes forgeable with no secret at all.
//   - a different HMAC algorithm: the same secret under HS512 still verifies
//     if the allow-list is gone. Harmless on its own, but it is the same hole
//     that becomes critical the day this moves to RS256, where an attacker can
//     re-sign with the *public* key as an HMAC secret.
//
// Both must be rejected even though the secret is correct — which is what
// makes the failure silent: the round-trip tests above would all still pass.
func TestVerifyRejectsUnexpectedSigningMethod(t *testing.T) {
	secret := []byte("test-secret")
	iss := Issuer{Secret: secret, TTL: time.Hour}

	claims := jwt.RegisteredClaims{
		Subject:   "player-victim",
		IssuedAt:  jwt.NewNumericDate(time.Now()),
		ExpiresAt: jwt.NewNumericDate(time.Now().Add(time.Hour)),
	}

	cases := []struct {
		name  string
		build func(t *testing.T) string
	}{
		{
			name: "alg none, no signature at all",
			build: func(t *testing.T) string {
				t.Helper()
				token, err := jwt.NewWithClaims(jwt.SigningMethodNone, claims).
					SignedString(jwt.UnsafeAllowNoneSignatureType)
				if err != nil {
					t.Fatalf("sign with alg none: %v", err)
				}
				return token
			},
		},
		{
			name: "correct secret, wrong HMAC algorithm",
			build: func(t *testing.T) string {
				t.Helper()
				token, err := jwt.NewWithClaims(jwt.SigningMethodHS512, claims).SignedString(secret)
				if err != nil {
					t.Fatalf("sign with HS512: %v", err)
				}
				return token
			},
		},
	}

	for _, tc := range cases {
		t.Run(tc.name, func(t *testing.T) {
			token := tc.build(t)

			// Sanity: the token is well-formed, so a rejection below is about
			// the signing method and not about a malformed string.
			if token == "" {
				t.Fatal("test built an empty token")
			}

			got, err := iss.Verify(token)
			if err == nil {
				t.Fatalf("Verify accepted a token signed with an unexpected method, returning %q", got)
			}
			if got != "" {
				t.Errorf("Verify returned player id %q alongside an error", got)
			}
		})
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
