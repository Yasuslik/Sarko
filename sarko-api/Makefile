.PHONY: test-db test-db-down test run migrate tidy

TEST_DATABASE_URL ?= postgres://sarko:sarko@localhost:5433/sarko_test?sslmode=disable

test-db:
	docker compose up -d
	@echo "waiting for postgres on :5433"
	@until docker compose exec -T postgres pg_isready -U sarko -d sarko_test >/dev/null 2>&1; do sleep 1; done
	@echo "ready"

test-db-down:
	docker compose down -v

test: test-db
	TEST_DATABASE_URL="$(TEST_DATABASE_URL)" go test ./... -count=1

run:
	go run ./cmd/server

tidy:
	go mod tidy
