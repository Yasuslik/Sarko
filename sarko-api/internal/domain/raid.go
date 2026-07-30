package domain

// RaidState mirrors the raid_state enum in Postgres.
type RaidState string

const (
	// StatePending means the loadout is debited but the client has not entered yet.
	StatePending RaidState = "pending"
	// StateActive means the raid is running.
	StateActive RaidState = "active"
	// StateClosed means the raid finished and was accounted.
	StateClosed RaidState = "closed"
	// StateVoided means the raid never started and the loadout was returned.
	StateVoided RaidState = "voided"
)

// RaidOutcome mirrors the raid_outcome enum in Postgres.
type RaidOutcome string

const (
	OutcomeExtracted RaidOutcome = "extracted"
	OutcomeDied      RaidOutcome = "died"
)

// IsValidOutcome guards client input.
func IsValidOutcome(s string) bool {
	return RaidOutcome(s) == OutcomeExtracted || RaidOutcome(s) == OutcomeDied
}
