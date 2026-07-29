.PHONY: test-db test-db-down test run tidy

TEST_DATABASE_URL ?= postgres://sarko:sarko@localhost:5455/sarko_test?sslmode=disable

test-db:
	docker compose up -d
	@echo "waiting for postgres on :5455"
	@until docker compose exec -T postgres pg_isready -U sarko -d sarko_test >/dev/null 2>&1; do sleep 1; done
	@echo "ready"

test-db-down:
	docker compose down -v

# -p 1 runs one package at a time. Every DB-backed package shares one test
# database and truncates it in testutil.Pool, so Go's default parallel package
# execution makes them clobber each other's rows (~40-50% flake once three
# packages do DB work). Serial packages is the cheap, deterministic fix; the
# alternative is a schema per package, which this suite is too small to need.
test: test-db
	TEST_DATABASE_URL="$(TEST_DATABASE_URL)" go test ./... -count=1 -p 1

run:
	go run ./cmd/server

tidy:
	go mod tidy
