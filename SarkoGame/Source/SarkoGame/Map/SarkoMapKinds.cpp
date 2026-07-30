#include "Map/SarkoMapKinds.h"

namespace
{
	const FString Cube = TEXT("/Engine/BasicShapes/Cube.Cube");
	const FString Cylinder = TEXT("/Engine/BasicShapes/Cylinder.Cylinder");

	/**
	 * The whole prop vocabulary of the Bridge sector.
	 *
	 * Sizes are chosen against a ~176 uu tall pawn: a car wreck is chest-high
	 * cover you can shoot over, a house is tall enough to break line of sight
	 * entirely, sandbags are crouch-height. That relationship is the level
	 * design — the numbers are not arbitrary.
	 */
	const TMap<FName, FSarkoPropKind>& KindTable()
	{
		static const TMap<FName, FSarkoPropKind> Table = {
			{ TEXT("wall"),        { FSoftObjectPath(Cube),     FVector(400.f, 60.f, 140.f),  true  } },
			{ TEXT("car_wreck"),   { FSoftObjectPath(Cube),     FVector(230.f, 95.f, 75.f),   true  } },
			{ TEXT("bus"),         { FSoftObjectPath(Cube),     FVector(600.f, 130.f, 160.f), true  } },
			{ TEXT("house"),       { FSoftObjectPath(Cube),     FVector(500.f, 400.f, 300.f), true  } },
			{ TEXT("fuel_pump"),   { FSoftObjectPath(Cube),     FVector(60.f, 40.f, 110.f),   true  } },
			{ TEXT("freight_car"), { FSoftObjectPath(Cube),     FVector(700.f, 150.f, 200.f), true  } },
			{ TEXT("water_tower"), { FSoftObjectPath(Cylinder), FVector(220.f, 220.f, 700.f), true  } },
			{ TEXT("sandbag"),     { FSoftObjectPath(Cube),     FVector(180.f, 70.f, 55.f),   true  } },
			{ TEXT("crate"),       { FSoftObjectPath(Cube),     FVector(70.f, 70.f, 70.f),    true  } },
			{ TEXT("pipe"),        { FSoftObjectPath(Cylinder), FVector(90.f, 90.f, 600.f),   true  } },
			{ TEXT("bridge_deck"), { FSoftObjectPath(Cube),     FVector(900.f, 300.f, 30.f),  true  } },
		};
		return Table;
	}
}

bool SarkoMap::FindPropKind(FName Kind, FSarkoPropKind& OutKind)
{
	if (const FSarkoPropKind* Found = KindTable().Find(Kind))
	{
		OutKind = *Found;
		return true;
	}
	return false;
}
