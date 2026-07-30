// Package auth issues device-scoped JWTs and one-time raid session tokens.
package auth

import (
	"fmt"
	"time"

	"github.com/golang-jwt/jwt/v5"
)

// Issuer signs and verifies player tokens with an HMAC secret.
type Issuer struct {
	Secret []byte
	TTL    time.Duration
}

// Issue returns a signed token whose subject is the player id.
func (i Issuer) Issue(playerID string) (string, error) {
	now := time.Now()
	claims := jwt.RegisteredClaims{
		Subject:   playerID,
		IssuedAt:  jwt.NewNumericDate(now),
		ExpiresAt: jwt.NewNumericDate(now.Add(i.TTL)),
	}
	signed, err := jwt.NewWithClaims(jwt.SigningMethodHS256, claims).SignedString(i.Secret)
	if err != nil {
		return "", fmt.Errorf("sign token: %w", err)
	}
	return signed, nil
}

// Verify returns the player id carried by a valid token.
func (i Issuer) Verify(token string) (string, error) {
	parsed, err := jwt.ParseWithClaims(token, &jwt.RegisteredClaims{},
		func(t *jwt.Token) (any, error) { return i.Secret, nil },
		jwt.WithValidMethods([]string{jwt.SigningMethodHS256.Alg()}),
	)
	if err != nil {
		return "", fmt.Errorf("parse token: %w", err)
	}
	claims, ok := parsed.Claims.(*jwt.RegisteredClaims)
	if !ok || claims.Subject == "" {
		return "", fmt.Errorf("token has no subject")
	}
	return claims.Subject, nil
}
