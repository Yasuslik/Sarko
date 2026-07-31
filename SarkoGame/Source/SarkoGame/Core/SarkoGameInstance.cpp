#include "Core/SarkoGameInstance.h"

#include "Net/SarkoBackendClient.h"

TSharedPtr<FSarkoBackendClient> USarkoGameInstance::EnsureBackend()
{
	if (!Backend.IsValid())
	{
		Backend = MakeShared<FSarkoBackendClient>();
	}
	return Backend;
}

void USarkoGameInstance::RecordRaidOutcome(ESarkoRaidOutcome Outcome, const TArray<FSarkoItemStack>& Haul,
	bool bPersisted)
{
	LastRaid.Outcome = Outcome;
	LastRaid.Haul = Haul;
	LastRaid.bPersisted = bPersisted;

	// The profile is now stale by definition — the raid just changed the stash —
	// so the cached copy is marked unloaded rather than left to be drawn as if it
	// were current. The shelter re-fetches on entry; this is what stops it drawing
	// yesterday's stash for the one frame before that lands.
	bProfileLoaded = false;

	UE_LOG(LogTemp, Display, TEXT("SarkoGameInstance: raid ended %s with %d stacks, persisted: %s"),
		*UEnum::GetValueAsString(Outcome), Haul.Num(), bPersisted ? TEXT("yes") : TEXT("NO"));
}

void USarkoGameInstance::RecordProfile(const FSarkoProfile& Profile)
{
	CachedProfile = Profile;
	bProfileLoaded = true;
}
