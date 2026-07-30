package domain

import "testing"

func TestIsValidOutcome(t *testing.T) {
	cases := []struct {
		name string
		in   string
		want bool
	}{
		{"extracted", "extracted", true},
		{"died", "died", true},
		{"empty string", "", false},
		{"bogus", "bogus", false},
		{"Extracted capitalized", "Extracted", false},
		{"random string", "unknown", false},
	}
	for _, tc := range cases {
		t.Run(tc.name, func(t *testing.T) {
			got := IsValidOutcome(tc.in)
			if got != tc.want {
				t.Errorf("IsValidOutcome(%q) = %v, want %v", tc.in, got, tc.want)
			}
		})
	}
}
