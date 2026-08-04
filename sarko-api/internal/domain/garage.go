package domain

// Tier is a step on the garage progression. Vehicles are never lost (§concept);
// only the parts found in a raid are at risk.
type Tier string

const (
	TierNone       Tier = "none"
	TierBicycle    Tier = "bicycle"
	TierMotorcycle Tier = "motorcycle"
	TierCar        Tier = "car"
	TierHelicopter Tier = "helicopter"
)

// tierOrder is the progression order; index also encodes how far a player has come.
var tierOrder = []Tier{TierNone, TierBicycle, TierMotorcycle, TierCar, TierHelicopter}

// mapsByTier lists the map each tier adds. Access is cumulative.
// Tier none owns the bridge as the on-foot starting zone: it is the sector the
// client actually ships (SarkoGame/Content/Data/Maps/bridge.json), and the wire id has
// to be the map the player is standing in, or /v1/raid/start answers
// map_locked for the only sector that exists.
var mapsByTier = map[Tier]string{
	TierNone:       "bridge",
	TierBicycle:    "swamp",
	TierMotorcycle: "mountains",
	TierCar:        "industrial",
	TierHelicopter: "airbase",
}

// recipes lists the parts consumed to build each tier.
var recipes = map[Tier][]ItemStack{
	TierBicycle: {
		{ItemID: "bike_frame", Quantity: 1},
		{ItemID: "wheel_small", Quantity: 2},
		{ItemID: "chain", Quantity: 1},
	},
	TierMotorcycle: {
		{ItemID: "engine_small", Quantity: 1},
		{ItemID: "wheel_medium", Quantity: 2},
		{ItemID: "fuel_tank", Quantity: 1},
	},
	TierCar: {
		{ItemID: "engine_large", Quantity: 1},
		{ItemID: "wheel_large", Quantity: 4},
		{ItemID: "gearbox", Quantity: 1},
		{ItemID: "battery", Quantity: 1},
	},
	TierHelicopter: {
		{ItemID: "turbine", Quantity: 1},
		{ItemID: "rotor_blade", Quantity: 2},
		{ItemID: "avionics", Quantity: 1},
		{ItemID: "fuel_tank", Quantity: 2},
	},
}

func tierIndex(t Tier) int {
	for i, candidate := range tierOrder {
		if candidate == t {
			return i
		}
	}
	return -1
}

// UnlockedMaps returns every map available at this tier, cumulatively.
func UnlockedMaps(t Tier) []string {
	idx := tierIndex(t)
	if idx < 0 {
		return nil
	}
	maps := make([]string, 0, idx+1)
	for _, tier := range tierOrder[:idx+1] {
		maps = append(maps, mapsByTier[tier])
	}
	return maps
}

// NextTier returns the tier a player may build next. ok is false at the top.
func NextTier(t Tier) (Tier, bool) {
	idx := tierIndex(t)
	if idx < 0 || idx+1 >= len(tierOrder) {
		return "", false
	}
	return tierOrder[idx+1], true
}

// Recipe returns the parts needed to build a tier. TierNone is not buildable.
func Recipe(t Tier) ([]ItemStack, bool) {
	parts, ok := recipes[t]
	if !ok {
		return nil, false
	}
	out := make([]ItemStack, len(parts))
	copy(out, parts)
	return out, true
}

// IsValidTier reports whether s names a known tier.
func IsValidTier(s string) bool { return tierIndex(Tier(s)) >= 0 }
