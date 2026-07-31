#pragma once

#include "CoreMinimal.h"

/**
 * Level travel, in one place.
 *
 * This project ships exactly one map (/Engine/Maps/Entry) because it authors no
 * .umap, so "going to the shelter" and "going on a raid" are the same level
 * loaded twice with a different game mode. The game mode is chosen by the travel
 * URL's `game=` option, which UGameInstance::CreateGameModeForURL resolves ahead
 * of GlobalDefaultGameMode — so the raid names its class and the shelter names
 * nothing and falls through to the ini.
 *
 * The URL builders are pure so the one string that decides which game the player
 * gets is unit tested; TravelTo is the single impure line, which is also the seam
 * to change to ServerTravel the day a dedicated server exists.
 */
namespace SarkoTravel
{
	/** Full package path, never a bare name — a bare name resolves against the
	 *  project's own content directory and only *warns* when it is missing. */
	FString ShelterMapName();

	/**
	 * `game=<raid game mode>`, plus `?Seed=<n>` when SeedOverride is non-zero.
	 *
	 * The class path comes from ASarkoRaidGameMode::StaticClass() rather than a
	 * literal, so renaming the class cannot silently route every raid back into
	 * the shelter.
	 *
	 * SeedOverride is the already-wrapped int32 that ASarkoRaidGameMode::Seed
	 * holds — not the backend's raw uint32, about half of which does not fit an
	 * int32 at all. SarkoBackend::SeedToInt32 is the wrap, and InitGame applies it
	 * again on the way in, so a negative number here round-trips exactly.
	 */
	FString RaidOptions(int32 SeedOverride);

	/** Empty, on purpose: the absence of `game=` is what selects the shelter. */
	FString ShelterOptions();

	/**
	 * Absolute travel to ShelterMapName() with the given options.
	 *
	 * **Absolute, always.** FURL's relative constructor copies the base URL's
	 * option list, so a relative return trip would inherit `game=…RaidGameMode`
	 * from the outbound URL and start another raid — an infinite raid loop with
	 * nothing in any log to explain it.
	 */
	void TravelTo(UObject* WorldContext, const FString& Options);
}
