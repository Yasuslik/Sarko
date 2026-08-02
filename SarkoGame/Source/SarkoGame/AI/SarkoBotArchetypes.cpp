#include "AI/SarkoBotArchetypes.h"

namespace
{
	FSarkoBotArchetype Row(const TCHAR* Id, float WalkSpeed, float MaxHealth, float Damage,
		float FireIntervalSeconds, float HearingSensitivity, float FiringRangeUU)
	{
		FSarkoBotArchetype Archetype;
		Archetype.Id = FName(Id);
		Archetype.WalkSpeed = WalkSpeed;
		Archetype.MaxHealth = MaxHealth;
		Archetype.Damage = Damage;
		Archetype.FireIntervalSeconds = FireIntervalSeconds;
		Archetype.HearingSensitivity = HearingSensitivity;
		Archetype.FiringRangeUU = FiringRangeUU;
		return Archetype;
	}
}

const TArray<FSarkoBotArchetype>& SarkoAI::GetBotArchetypes()
{
	// Built once, on first use, and handed out by const reference: the table is
	// vocabulary, not state, and nothing may edit it at runtime.
	static const TArray<FSarkoBotArchetype> Archetypes = {
		//   id              speed   hp   dmg  fire  hearing  range
		Row(TEXT("scav_pistol"), 300.f, 60.f, 22.f, 2.0f, 0.90f, 1100.f),
		Row(TEXT("scav_smg"),    340.f, 70.f, 16.f, 1.3f, 1.00f, 1100.f),
		Row(TEXT("scout"),       420.f, 45.f, 14.f, 1.8f, 1.25f,  900.f),
	};
	return Archetypes;
}

bool SarkoAI::FindBotArchetype(FName Id, FSarkoBotArchetype& Out)
{
	for (const FSarkoBotArchetype& Archetype : GetBotArchetypes())
	{
		if (Archetype.Id == Id)
		{
			Out = Archetype;
			return true;
		}
	}
	return false;
}
