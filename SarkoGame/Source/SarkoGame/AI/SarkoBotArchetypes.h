#pragma once

#include "CoreMinimal.h"

/**
 * One kind of enemy, as numbers.
 *
 * A plain struct and not a USTRUCT: nothing reflects it, nothing replicates it
 * and nothing edits it in a details panel — the encounter director reads a row
 * at spawn time and pushes the values into the components that already own
 * them. Same reasoning as FSarkoPropKind's table being C++ rather than data: an
 * archetype is vocabulary, and vocabulary belongs next to the code that speaks
 * it, while the *placement* of an archetype is map data and lives in
 * bridge.json.
 *
 * No assets, deliberately (spec §2): an archetype is speed, health, damage,
 * cadence and perception. What it looks like arrives with the art.
 */
struct FSarkoBotArchetype
{
	FName Id;

	/** Chase and patrol speed, uu/s. */
	float WalkSpeed = 340.f;

	float MaxHealth = 60.f;

	/** Per hit. The player's own damage is USarkoRaidSettings::WeaponDamage. */
	float Damage = 22.f;

	/** Minimum seconds between this bot's shots. */
	float FireIntervalSeconds = 2.f;

	/**
	 * How well this bot listens, as a multiplier on a noise event's own radius
	 * (spec §7). Never a licence to shoot — see FiringRangeUU.
	 *
	 * This used to be `HearingRadiusUU`, a distance, and that was the shape of the
	 * bug the noise model fixes: a radius owned by the listener means the only
	 * variable is how close the player is, and the player cannot spend distance.
	 * How far a sound carries now belongs to the sound
	 * (USarkoRaidSettings::NoiseLoudRadiusUU and friends); how much of it reaches
	 * this bot belongs here.
	 */
	float HearingSensitivity = 1.f;

	/**
	 * How far it will open fire, line of sight permitting. The player's portrait
	 * camera shows about 1380 uu ahead, so anything above that is a shot from
	 * off-screen, which ТЗ §11 forbids.
	 */
	float FiringRangeUU = 1100.f;
};

namespace SarkoAI
{
	/**
	 * THE ARCHETYPE TABLE. Three rows, and the differences between them are the
	 * whole point:
	 *
	 * | id          | speed | hp | dmg | fire | hearing | range |
	 * |-------------|-------|----|-----|------|---------|-------|
	 * | scav_pistol |  300  | 60 |  22 | 2.0  |  0.90x  | 1100  |
	 * | scav_smg    |  340  | 70 |  16 | 1.3  |  1.00x  | 1100  |
	 * | scout       |  420  | 45 |  14 | 1.8  |  1.25x  |  900  |
	 *
	 * `scav_pistol` is the tutorial's teacher: slower than the player (440),
	 * loud, and it has to be close to hurt you. Sixty hp is four of the player's
	 * shots, so a fight is a magazine and a decision, not a duel of attrition.
	 *
	 * `scav_smg` trades reach for cadence — the depot's answer to a player who
	 * has learned to stand still and trade.
	 *
	 * `scout` listens a quarter better than anything else while shooting from the
	 * shortest range in the table — the one row where those two numbers disagree
	 * on purpose: it is the archetype that finds you and then has to survive being
	 * right. At 1.25x it hears a run from 1375 uu and a shot from 3250.
	 *
	 * The hearing column is a MULTIPLIER since spec §7, not a distance. The
	 * ordering it used to express (1400 / 1600 / 2000) is preserved; what changed
	 * is that the distance now comes from what the player did.
	 */
	const TArray<FSarkoBotArchetype>& GetBotArchetypes();

	/** False for a name the table does not know — the parser rejects the map for it. */
	bool FindBotArchetype(FName Id, FSarkoBotArchetype& Out);
}
