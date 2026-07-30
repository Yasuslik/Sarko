// Package store owns every SQL statement. All raid accounting is transactional.
package store

import (
	"errors"

	"github.com/jackc/pgx/v5/pgxpool"
)

// ErrNotFound means the requested row does not exist.
var ErrNotFound = errors.New("not found")

// Store is the data access layer over a Postgres pool.
type Store struct {
	pool *pgxpool.Pool
}

func New(pool *pgxpool.Pool) *Store { return &Store{pool: pool} }
