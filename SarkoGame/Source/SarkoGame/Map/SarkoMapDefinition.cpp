#include "Map/SarkoMapDefinition.h"

// Included for the same reason the item catalog is: an archetype name that the
// table does not know is checked HERE, at load, where the message reaches a
// person — not at spawn time, four minutes into a raid, as a bot that silently
// does not appear.
#include "AI/SarkoBotArchetypes.h"
#include "Dom/JsonObject.h"
#include "Loot/SarkoItemCatalog.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	/** An extraction zone smaller than this could be walked through without the
	 *  overlap ever registering, so it could never trigger. */
	constexpr float MinExtractionRadiusUU = 100.f;

	/**
	 * Reads a fixed-length numeric array, naming the field and the index of any
	 * element that is not a number. Shared by ReadVector and ReadVector2 so a
	 * pair and a triple cannot drift apart in strictness.
	 *
	 * The element type is checked directly rather than through TryGetNumber, for
	 * the same reason ReadOptionalNumber below does it: TJsonValueString overrides
	 * TryGetNumber and runs the text through LexTryParseString, so `"pos":
	 * ["0",0,0]` and `"size": ["2000",1500]` would parse silently and the map file
	 * would grow strings where numbers belong with nothing to grep for.
	 */
	bool ReadNumberArray(const TSharedPtr<FJsonObject>& Object, const FString& Field,
		int32 Expected, double* Out, FString& OutError)
	{
		const TArray<TSharedPtr<FJsonValue>>* Array = nullptr;
		if (!Object->TryGetArrayField(Field, Array) || !Array)
		{
			OutError = FString::Printf(TEXT("'%s' is missing or not an array"), *Field);
			return false;
		}
		if (Array->Num() != Expected)
		{
			OutError = FString::Printf(TEXT("'%s' must have exactly %d numbers, found %d"),
				*Field, Expected, Array->Num());
			return false;
		}
		for (int32 Index = 0; Index < Expected; ++Index)
		{
			const TSharedPtr<FJsonValue>& Element = (*Array)[Index];
			if (!Element.IsValid() || Element->Type != EJson::Number)
			{
				OutError = FString::Printf(TEXT("'%s[%d]' is not a number"), *Field, Index);
				return false;
			}
			Out[Index] = Element->AsNumber();
		}
		return true;
	}

	/** Reads a [x, y, z] array into a vector. */
	bool ReadVector(const TSharedPtr<FJsonObject>& Object, const FString& Field, FVector& Out, FString& OutError)
	{
		double Components[3] = { 0.0, 0.0, 0.0 };
		if (!ReadNumberArray(Object, Field, 3, Components, OutError))
		{
			return false;
		}
		Out = FVector(
			static_cast<float>(Components[0]),
			static_cast<float>(Components[1]),
			static_cast<float>(Components[2]));
		return true;
	}

	/** Reads an [x, y] pair. Same discipline as ReadVector, one axis shorter. */
	bool ReadVector2(const TSharedPtr<FJsonObject>& Object, const FString& Field, FVector2D& Out, FString& OutError)
	{
		double Components[2] = { 0.0, 0.0 };
		if (!ReadNumberArray(Object, Field, 2, Components, OutError))
		{
			return false;
		}
		Out = FVector2D(static_cast<float>(Components[0]), static_cast<float>(Components[1]));
		return true;
	}

	/**
	 * Reads an optional array field. Absence is legitimate (the caller decides
	 * what an absent section means); a value present under this key but not an
	 * array is not, and must not be indistinguishable from zero entries.
	 */
	bool TryGetOptionalArrayField(const TSharedPtr<FJsonObject>& Object, const FString& Field,
		const TArray<TSharedPtr<FJsonValue>>*& OutArray, FString& OutError)
	{
		OutArray = nullptr;
		if (!Object->HasField(Field))
		{
			return true;
		}
		if (!Object->TryGetArrayField(Field, OutArray) || !OutArray)
		{
			OutError = FString::Printf(TEXT("'%s' is present but not an array"), *Field);
			return false;
		}
		return true;
	}

	/**
	 * Reads an optional numeric field. A genuinely absent field keeps Out at
	 * whatever default the caller preset it to; a field that is present but is
	 * not a JSON number is a named error, not a silent 0.0.
	 *
	 * The JSON type is checked directly, for the same reason ReadOptionalString
	 * and ReadOptionalBool below do it: TryGetNumberField is not a type check.
	 * TJsonValueString overrides TryGetNumber and runs the text through
	 * LexTryParseString, so `"yaw": "45"` and `"radiusUU": "400"` used to parse
	 * as 45 and 400 — and that is the *dangerous* half of this defect, not the
	 * harmless one. `"yaw": "abc"` was already rejected because the text does
	 * not parse; a quoted numeral succeeded silently, so the map file grew a
	 * string where a number belongs, nothing warned, and the next tool that
	 * reads the file strictly (or the next value that stops being a clean
	 * numeral) breaks with no clue where the quotes came from.
	 */
	bool ReadOptionalNumber(const TSharedPtr<FJsonObject>& Object, const FString& Field, double& Out, FString& OutError)
	{
		const TSharedPtr<FJsonValue> Value = Object->TryGetField(Field);
		if (!Value.IsValid())
		{
			return true;
		}
		if (Value->Type != EJson::Number)
		{
			OutError = FString::Printf(TEXT("'%s' is present but not a number"), *Field);
			return false;
		}
		Out = Value->AsNumber();
		return true;
	}

	/**
	 * Reads a REQUIRED numeric field, with the same JSON-type strictness as
	 * ReadOptionalNumber and for the same reason: TryGetNumberField is not a type
	 * check. `"extentUU": "20000"` and `"qty": "3"` used to parse as 20000 and 3,
	 * because TJsonValueString overrides TryGetNumber and runs the text through
	 * LexTryParseString. That is the silent half of the defect — the sector really
	 * did come out 400 m across and the container really did hand out three
	 * items — so nothing warned, nothing was greppable, and the map file quietly
	 * held strings where numbers belong until something read it strictly.
	 *
	 * The only difference from the optional form is that absence is an error here.
	 * Context prefixes the message so a nested field can name its path; callers
	 * pass an empty string for a root field.
	 */
	bool ReadRequiredNumber(const TSharedPtr<FJsonObject>& Object, const FString& Field,
		const FString& Context, double& Out, FString& OutError)
	{
		const TSharedPtr<FJsonValue> Value = Object->TryGetField(Field);
		if (!Value.IsValid())
		{
			OutError = FString::Printf(TEXT("%s'%s' is missing"), *Context, *Field);
			return false;
		}
		if (Value->Type != EJson::Number)
		{
			OutError = FString::Printf(TEXT("%s'%s' is present but not a number"), *Context, *Field);
			return false;
		}
		Out = Value->AsNumber();
		return true;
	}

	/**
	 * Reads an optional string field the same way: absent keeps the caller's
	 * default, present-but-not-a-string is a named error rather than silently
	 * collapsing to something indistinguishable from "not set".
	 *
	 * The JSON type is checked directly rather than through TryGetStringField,
	 * for the same reason ReadOptionalId does it (see below): TryGetStringField
	 * is not a type check. FJsonValueNumber and FJsonValueBoolean both override
	 * TryGetString and stringify, so `"tier": 7` used to parse as the tier "7"
	 * and `"name": true` as the extraction called "true" — values that look
	 * authored, cannot be found by grepping the map file, and change
	 * representation the moment anyone re-serialises it.
	 */
	bool ReadOptionalString(const TSharedPtr<FJsonObject>& Object, const FString& Field, FString& Out, FString& OutError)
	{
		const TSharedPtr<FJsonValue> Value = Object->TryGetField(Field);
		if (!Value.IsValid())
		{
			return true;
		}
		if (Value->Type != EJson::String)
		{
			OutError = FString::Printf(TEXT("'%s' is present but not a string"), *Field);
			return false;
		}
		Out = Value->AsString();
		return true;
	}

	/**
	 * Reads an optional bool with the same discipline: absent keeps the caller's
	 * default, present-but-not-a-bool is a named error rather than a silent
	 * false.
	 *
	 * Strict on the JSON type, and not via TryGetBoolField, because
	 * TJsonValueString overrides TryGetBool and runs FString::ToBool() over the
	 * text. `"blocksMovement": "no"` would therefore succeed and yield false;
	 * worse, `"blocksMovement": "ture"` would succeed and yield false too — a
	 * typo that silently deletes a wall's collision and leaves it visible.
	 */
	bool ReadOptionalBool(const TSharedPtr<FJsonObject>& Object, const FString& Field, bool& Out, FString& OutError)
	{
		const TSharedPtr<FJsonValue> Value = Object->TryGetField(Field);
		if (!Value.IsValid())
		{
			return true;
		}
		if (Value->Type != EJson::Boolean)
		{
			OutError = FString::Printf(TEXT("'%s' is present but not a boolean"), *Field);
			return false;
		}
		Out = Value->AsBool();
		return true;
	}

	/**
	 * Reads an optional surface name. An unlisted name is an error: falling
	 * back to grey would turn a typo in "asphalt" into a light highway across a
	 * dark map, which reads as a lighting bug and not as a data bug.
	 */
	bool ReadOptionalSurface(const TSharedPtr<FJsonObject>& Object, ESarkoSurface& Out, FString& OutError)
	{
		FString Name;
		if (!ReadOptionalString(Object, TEXT("surface"), Name, OutError))
		{
			return false;
		}
		if (Name.IsEmpty())
		{
			return true;
		}
		if (!SarkoMap::ParseSurfaceName(Name, Out))
		{
			OutError = FString::Printf(TEXT("'surface' is not a known surface: '%s'"), *Name);
			return false;
		}
		return true;
	}

	/**
	 * Reads an optional stable id. Absent is fine; present-but-empty is not —
	 * an empty id is a name nothing can be found by, and two of them collide.
	 *
	 * Separate from ReadOptionalString only for the non-empty rule and the
	 * fixed field name: both check the JSON type directly rather than trusting
	 * TryGetStringField, which is not the type check it looks like — see
	 * ReadOptionalString above for why `"id": 7` used to parse as the id "7".
	 */
	bool ReadOptionalId(const TSharedPtr<FJsonObject>& Object, FString& Out, FString& OutError)
	{
		if (!ReadOptionalString(Object, TEXT("id"), Out, OutError))
		{
			return false;
		}
		if (!Object->HasField(TEXT("id")))
		{
			return true;
		}
		if (Out.IsEmpty())
		{
			OutError = TEXT("'id' is present but empty");
			return false;
		}
		return true;
	}

	/**
	 * Reads a REQUIRED whole number into an int32. Same JSON-type strictness as
	 * ReadRequiredNumber, plus the rule that makes an int an int: JSON has one
	 * number type, so `"budgetCost": 1.5` would otherwise be truncated to 1 and
	 * the author's mistake would become the raid's budget with nothing logged —
	 * exactly the failure `qty` already has a named error for.
	 */
	bool ReadRequiredWholeNumber(const TSharedPtr<FJsonObject>& Object, const FString& Field,
		const FString& Context, int32& Out, FString& OutError)
	{
		double Value = 0.0;
		if (!ReadRequiredNumber(Object, Field, Context, Value, OutError))
		{
			return false;
		}
		if (Value != FMath::TruncToDouble(Value))
		{
			OutError = FString::Printf(TEXT("%s'%s' must be a whole number, not %g — a fraction would be silently truncated"),
				*Context, *Field, Value);
			return false;
		}
		Out = static_cast<int32>(Value);
		return true;
	}

	/**
	 * Reads a REQUIRED bool. Absence is an error here, unlike ReadOptionalBool:
	 * `oneShot` decides whether a POI can hand out enemies twice, and a field
	 * that quietly defaults is a field nobody reads before it matters.
	 */
	bool ReadRequiredBool(const TSharedPtr<FJsonObject>& Object, const FString& Field,
		const FString& Context, bool& Out, FString& OutError)
	{
		const TSharedPtr<FJsonValue> Value = Object->TryGetField(Field);
		if (!Value.IsValid())
		{
			OutError = FString::Printf(TEXT("%s'%s' is missing"), *Context, *Field);
			return false;
		}
		if (Value->Type != EJson::Boolean)
		{
			OutError = FString::Printf(TEXT("%s'%s' is present but not a boolean"), *Context, *Field);
			return false;
		}
		Out = Value->AsBool();
		return true;
	}

	/** Reads a REQUIRED, non-empty string, with the same type strictness as the optional form. */
	bool ReadRequiredString(const TSharedPtr<FJsonObject>& Object, const FString& Field,
		const FString& Context, FString& Out, FString& OutError)
	{
		if (!ReadOptionalString(Object, Field, Out, OutError))
		{
			OutError = FString::Printf(TEXT("%s%s"), *Context, *OutError);
			return false;
		}
		if (Out.IsEmpty())
		{
			OutError = FString::Printf(TEXT("%s'%s' is missing, empty or not a string"), *Context, *Field);
			return false;
		}
		return true;
	}

	/** Reads a yaw in degrees, defaulting to 0.0 when the field is absent. */
	bool ReadYaw(const TSharedPtr<FJsonObject>& Object, double& OutYaw, FString& OutError)
	{
		OutYaw = 0.0;
		return ReadOptionalNumber(Object, TEXT("yaw"), OutYaw, OutError);
	}

	/** Reads a doorway object. `side` is required for a perimeter door only. */
	bool ReadDoor(const TSharedPtr<FJsonObject>& Object, bool bNeedsSide, FSarkoBuildingDoor& Out, FString& OutError)
	{
		if (bNeedsSide)
		{
			FString SideName;
			if (!ReadOptionalString(Object, TEXT("side"), SideName, OutError))
			{
				return false;
			}
			if (SideName.IsEmpty())
			{
				OutError = TEXT("'side' is missing or empty");
				return false;
			}
			if (!SarkoMap::ParseBuildingSide(SideName, Out.Side))
			{
				OutError = FString::Printf(TEXT("'side' must be N, E, S or W, found '%s'"), *SideName);
				return false;
			}
		}
		double Offset = 0.0;
		if (!ReadOptionalNumber(Object, TEXT("offset"), Offset, OutError))
		{
			return false;
		}
		Out.OffsetUU = static_cast<float>(Offset);

		// Width has a real default (300, the ТЗ's preferred opening) but a
		// present-and-unparseable value is still an error, not a silent 300.
		double Width = static_cast<double>(Out.WidthUU);
		if (!ReadOptionalNumber(Object, TEXT("width"), Width, OutError))
		{
			return false;
		}
		Out.WidthUU = static_cast<float>(Width);
		return true;
	}
}

bool SarkoMap::ParseDefinition(const FString& Json, FSarkoMapDefinition& OutDefinition, FString& OutError)
{
	OutDefinition = FSarkoMapDefinition();
	OutError.Reset();

	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		OutError = TEXT("not valid JSON");
		return false;
	}

	// Not TryGetStringField: FJsonValueNumber and FJsonValueBoolean both override
	// TryGetString and stringify, so `"id": 7` became the map id "7" — a map that
	// loads, runs, and cannot be found by grepping the file for its own name.
	if (!ReadOptionalString(Root, TEXT("id"), OutDefinition.Id, OutError) || OutDefinition.Id.IsEmpty())
	{
		OutError = TEXT("'id' is missing, empty or not a string");
		return false;
	}

	double Extent = 0.0;
	if (!ReadRequiredNumber(Root, TEXT("extentUU"), FString(), Extent, OutError))
	{
		return false;
	}
	if (Extent <= 0.0)
	{
		OutError = TEXT("'extentUU' is not positive");
		return false;
	}
	OutDefinition.ExtentUU = static_cast<float>(Extent);

	double Duration = 0.0;
	if (!ReadRequiredNumber(Root, TEXT("raidDurationSeconds"), FString(), Duration, OutError))
	{
		return false;
	}
	if (Duration <= 0.0)
	{
		OutError = TEXT("'raidDurationSeconds' is not positive");
		return false;
	}
	OutDefinition.RaidDurationSeconds = static_cast<float>(Duration);

	// blocks
	const TArray<TSharedPtr<FJsonValue>>* Blocks = nullptr;
	if (!TryGetOptionalArrayField(Root, TEXT("blocks"), Blocks, OutError))
	{
		return false;
	}
	if (Blocks)
	{
		for (int32 Index = 0; Index < Blocks->Num(); ++Index)
		{
			const TSharedPtr<FJsonValue>& Value = (*Blocks)[Index];
			const TSharedPtr<FJsonObject>* Object = nullptr;
			if (!Value->TryGetObject(Object) || !Object)
			{
				OutError = FString::Printf(TEXT("blocks[%d]: not an object"), Index);
				return false;
			}
			FSarkoCoverBlock Block;
			if (!ReadVector(*Object, TEXT("pos"), Block.Location, OutError) ||
				!ReadVector(*Object, TEXT("extent"), Block.Extent, OutError))
			{
				OutError = FString::Printf(TEXT("blocks[%d]: %s"), Index, *OutError);
				return false;
			}
			if (!ReadOptionalId(*Object, Block.Id, OutError))
			{
				OutError = FString::Printf(TEXT("blocks[%d]: %s"), Index, *OutError);
				return false;
			}
			// A block with a non-positive extent describes geometry that either
			// cannot exist or has collapsed to nothing, and is never intentional.
			if (Block.Extent.X <= 0.f || Block.Extent.Y <= 0.f || Block.Extent.Z <= 0.f)
			{
				OutError = FString::Printf(TEXT("blocks[%d]: 'extent' components must all be positive, found (%.1f, %.1f, %.1f)"),
					Index, Block.Extent.X, Block.Extent.Y, Block.Extent.Z);
				return false;
			}
			double Yaw = 0.0;
			if (!ReadYaw(*Object, Yaw, OutError))
			{
				OutError = FString::Printf(TEXT("blocks[%d]: %s"), Index, *OutError);
				return false;
			}
			Block.Rotation = FRotator(0.f, static_cast<float>(Yaw), 0.f);
			// Both optional, both defaulted on the struct, so a block written
			// before surfaces existed keeps the grey cover it always had.
			if (!ReadOptionalSurface(*Object, Block.Surface, OutError) ||
				!ReadOptionalBool(*Object, TEXT("blocksMovement"), Block.bBlocksMovement, OutError))
			{
				OutError = FString::Printf(TEXT("blocks[%d]: %s"), Index, *OutError);
				return false;
			}
			OutDefinition.Blocks.Add(Block);
		}
	}

	// buildings
	const TArray<TSharedPtr<FJsonValue>>* Buildings = nullptr;
	if (!TryGetOptionalArrayField(Root, TEXT("buildings"), Buildings, OutError))
	{
		return false;
	}
	if (Buildings)
	{
		for (int32 Index = 0; Index < Buildings->Num(); ++Index)
		{
			const TSharedPtr<FJsonObject>* Object = nullptr;
			if (!(*Buildings)[Index]->TryGetObject(Object) || !Object)
			{
				OutError = FString::Printf(TEXT("buildings[%d]: not an object"), Index);
				return false;
			}
			FSarkoBuilding Building;
			// Required, unlike every other section's id: a building's id is the
			// prefix of every wall id it generates, so an anonymous building
			// would emit walls named "_north_0" and collide with the next one.
			if (!ReadOptionalString(*Object, TEXT("id"), Building.Id, OutError) || Building.Id.IsEmpty())
			{
				OutError = FString::Printf(TEXT("buildings[%d]: 'id' is missing, empty or not a string"), Index);
				return false;
			}
			if (!ReadVector(*Object, TEXT("pos"), Building.Location, OutError) ||
				!ReadVector2(*Object, TEXT("size"), Building.SizeUU, OutError))
			{
				OutError = FString::Printf(TEXT("buildings[%d] ('%s'): %s"), Index, *Building.Id, *OutError);
				return false;
			}
			double Yaw = 0.0;
			double WallHeight = static_cast<double>(Building.WallHeightUU);
			double WallThickness = static_cast<double>(Building.WallThicknessUU);
			if (!ReadOptionalNumber(*Object, TEXT("yaw"), Yaw, OutError) ||
				!ReadOptionalNumber(*Object, TEXT("wallHeight"), WallHeight, OutError) ||
				!ReadOptionalNumber(*Object, TEXT("wallThickness"), WallThickness, OutError) ||
				!ReadOptionalSurface(*Object, Building.Surface, OutError))
			{
				OutError = FString::Printf(TEXT("buildings[%d] ('%s'): %s"), Index, *Building.Id, *OutError);
				return false;
			}
			Building.Yaw = static_cast<float>(Yaw);
			Building.WallHeightUU = static_cast<float>(WallHeight);
			Building.WallThicknessUU = static_cast<float>(WallThickness);

			const TArray<TSharedPtr<FJsonValue>>* Doors = nullptr;
			if (!TryGetOptionalArrayField(*Object, TEXT("doors"), Doors, OutError))
			{
				OutError = FString::Printf(TEXT("buildings[%d] ('%s'): %s"), Index, *Building.Id, *OutError);
				return false;
			}
			if (Doors)
			{
				for (int32 DoorIndex = 0; DoorIndex < Doors->Num(); ++DoorIndex)
				{
					const TSharedPtr<FJsonObject>* DoorObject = nullptr;
					if (!(*Doors)[DoorIndex]->TryGetObject(DoorObject) || !DoorObject)
					{
						OutError = FString::Printf(TEXT("buildings[%d] ('%s'): doors[%d] is not an object"),
							Index, *Building.Id, DoorIndex);
						return false;
					}
					FSarkoBuildingDoor Door;
					if (!ReadDoor(*DoorObject, /*bNeedsSide*/ true, Door, OutError))
					{
						OutError = FString::Printf(TEXT("buildings[%d] ('%s'): doors[%d]: %s"),
							Index, *Building.Id, DoorIndex, *OutError);
						return false;
					}
					Building.Doors.Add(Door);
				}
			}

			const TArray<TSharedPtr<FJsonValue>>* Walls = nullptr;
			if (!TryGetOptionalArrayField(*Object, TEXT("interiorWalls"), Walls, OutError))
			{
				OutError = FString::Printf(TEXT("buildings[%d] ('%s'): %s"), Index, *Building.Id, *OutError);
				return false;
			}
			if (Walls)
			{
				for (int32 WallIndex = 0; WallIndex < Walls->Num(); ++WallIndex)
				{
					const TSharedPtr<FJsonObject>* WallObject = nullptr;
					if (!(*Walls)[WallIndex]->TryGetObject(WallObject) || !WallObject)
					{
						OutError = FString::Printf(TEXT("buildings[%d] ('%s'): interiorWalls[%d] is not an object"),
							Index, *Building.Id, WallIndex);
						return false;
					}
					FSarkoBuildingInteriorWall Wall;
					if (!ReadVector2(*WallObject, TEXT("from"), Wall.From, OutError) ||
						!ReadVector2(*WallObject, TEXT("to"), Wall.To, OutError))
					{
						OutError = FString::Printf(TEXT("buildings[%d] ('%s'): interiorWalls[%d]: %s"),
							Index, *Building.Id, WallIndex, *OutError);
						return false;
					}
					if ((*WallObject)->HasField(TEXT("door")))
					{
						const TSharedPtr<FJsonObject>* DoorObject = nullptr;
						if (!(*WallObject)->TryGetObjectField(TEXT("door"), DoorObject) || !DoorObject)
						{
							OutError = FString::Printf(
								TEXT("buildings[%d] ('%s'): interiorWalls[%d]: 'door' is present but not an object"),
								Index, *Building.Id, WallIndex);
							return false;
						}
						if (!ReadDoor(*DoorObject, /*bNeedsSide*/ false, Wall.Door, OutError))
						{
							OutError = FString::Printf(TEXT("buildings[%d] ('%s'): interiorWalls[%d]: %s"),
								Index, *Building.Id, WallIndex, *OutError);
							return false;
						}
						Wall.bHasDoor = true;
					}
					Building.InteriorWalls.Add(Wall);
				}
			}

			// Expand here and throw the result away. ToLayout has no error
			// channel, so every geometric rule the expander enforces has to be
			// checked at load time, where the message reaches a person — and the
			// only way to check them all is to run the real function.
			TArray<FSarkoCoverBlock> Scratch;
			FString ExpandError;
			if (!ExpandBuilding(Building, Scratch, ExpandError))
			{
				OutError = FString::Printf(TEXT("buildings[%d]: %s"), Index, *ExpandError);
				return false;
			}
			OutDefinition.Buildings.Add(Building);
		}
	}

	// props
	const TArray<TSharedPtr<FJsonValue>>* Props = nullptr;
	if (!TryGetOptionalArrayField(Root, TEXT("props"), Props, OutError))
	{
		return false;
	}
	if (Props)
	{
		for (int32 Index = 0; Index < Props->Num(); ++Index)
		{
			const TSharedPtr<FJsonValue>& Value = (*Props)[Index];
			const TSharedPtr<FJsonObject>* Object = nullptr;
			if (!Value->TryGetObject(Object) || !Object)
			{
				OutError = FString::Printf(TEXT("props[%d]: not an object"), Index);
				return false;
			}
			FSarkoMapProp Prop;
			if (!ReadVector(*Object, TEXT("pos"), Prop.Location, OutError))
			{
				OutError = FString::Printf(TEXT("props[%d]: %s"), Index, *OutError);
				return false;
			}
			if (!ReadOptionalId(*Object, Prop.Id, OutError))
			{
				OutError = FString::Printf(TEXT("props[%d]: %s"), Index, *OutError);
				return false;
			}
			// An empty/missing kind means no mesh is chosen downstream, which is
			// the same silent no-op that already cost a session once — and this is
			// the worst of the three stringifying reads, not merely the untidiest:
			// `"kind": 7` used to become FName("7"), FindPropKind found no such
			// kind, and SpawnProps skipped the prop. A prop that never appears, from
			// a file that parses, with nothing pointing at the typo.
			FString Kind;
			if (!ReadOptionalString(*Object, TEXT("kind"), Kind, OutError) || Kind.IsEmpty())
			{
				OutError = FString::Printf(TEXT("props[%d]: 'kind' is missing, empty or not a string"), Index);
				return false;
			}
			Prop.Kind = FName(*Kind);
			double Yaw = 0.0;
			if (!ReadYaw(*Object, Yaw, OutError))
			{
				OutError = FString::Printf(TEXT("props[%d]: %s"), Index, *OutError);
				return false;
			}
			Prop.Yaw = static_cast<float>(Yaw);
			OutDefinition.Props.Add(Prop);
		}
	}

	// containers
	const TArray<TSharedPtr<FJsonValue>>* Containers = nullptr;
	if (!TryGetOptionalArrayField(Root, TEXT("containers"), Containers, OutError))
	{
		return false;
	}
	if (Containers)
	{
		for (int32 Index = 0; Index < Containers->Num(); ++Index)
		{
			const TSharedPtr<FJsonValue>& Value = (*Containers)[Index];
			const TSharedPtr<FJsonObject>* Object = nullptr;
			if (!Value->TryGetObject(Object) || !Object)
			{
				OutError = FString::Printf(TEXT("containers[%d]: not an object"), Index);
				return false;
			}
			FSarkoLootContainerSpot Spot;
			if (!ReadVector(*Object, TEXT("pos"), Spot.Location, OutError))
			{
				OutError = FString::Printf(TEXT("containers[%d]: %s"), Index, *OutError);
				return false;
			}
			if (!ReadOptionalId(*Object, Spot.Id, OutError))
			{
				OutError = FString::Printf(TEXT("containers[%d]: %s"), Index, *OutError);
				return false;
			}
			// Unlike a prop's kind, nothing downstream reads a container's tier
			// yet (ToLayout drops Containers entirely), so an absent tier is
			// left as NAME_None rather than rejected. A present-but-wrong-typed
			// value is still a named error, matching the rule for every other
			// optional field above.
			FString Tier;
			if (!ReadOptionalString(*Object, TEXT("tier"), Tier, OutError))
			{
				OutError = FString::Printf(TEXT("containers[%d]: %s"), Index, *OutError);
				return false;
			}
			Spot.Tier = FName(*Tier);

			// fixedItems — optional, and every failure is named. A silently
			// shortened list is a teaching beat that quietly stops happening.
			const TArray<TSharedPtr<FJsonValue>>* FixedItems = nullptr;
			if (!TryGetOptionalArrayField(*Object, TEXT("fixedItems"), FixedItems, OutError))
			{
				OutError = FString::Printf(TEXT("containers[%d]: %s"), Index, *OutError);
				return false;
			}
			if (FixedItems)
			{
				// An empty list is a named error, not "no fixed items". Written to
				// mean "this container is empty during the tutorial" it would do the
				// opposite: FSarkoLootContainerSpot::FixedItems would be empty,
				// indistinguishable from an absent key, so SarkoLoot::RollContainerFor
				// falls through to a seeded roll and the container hands out random
				// loot. It would also cost Stage C its acceptance signal —
				// SetTutorialLoot counts containers with Num() > 0, so the "none of N
				// containers carries fixedItems" Warning would keep firing for a map
				// that had in fact been authored.
				if (FixedItems->Num() == 0)
				{
					OutError = FString::Printf(
						TEXT("containers[%d]: 'fixedItems' is present but empty — an empty list cannot mean 'holds nothing' (the container would roll against the raid seed instead); omit the key to roll, or list what it holds"),
						Index);
					return false;
				}
				for (int32 ItemIndex = 0; ItemIndex < FixedItems->Num(); ++ItemIndex)
				{
					const TSharedPtr<FJsonObject>* ItemObject = nullptr;
					if (!(*FixedItems)[ItemIndex]->TryGetObject(ItemObject) || !ItemObject)
					{
						OutError = FString::Printf(TEXT("containers[%d].fixedItems[%d]: not an object"),
							Index, ItemIndex);
						return false;
					}
					// Type-checked here rather than left to the catalog lookup below:
					// `"item": 7` used to reach it as the id "7", which is not in
					// items.json, so the file was rejected with "'7' is not in
					// Data/Items/items.json" — a true statement that sends the
					// reader looking for a missing item instead of a stray quote.
					FString ItemId;
					if (!ReadOptionalString(*ItemObject, TEXT("item"), ItemId, OutError) || ItemId.IsEmpty())
					{
						OutError = FString::Printf(TEXT("containers[%d].fixedItems[%d]: 'item' is missing, empty or not a string"),
							Index, ItemIndex);
						return false;
					}
					double Quantity = 0.0;
					const FString QuantityContext = FString::Printf(
						TEXT("containers[%d].fixedItems[%d] ('%s'): "), Index, ItemIndex, *ItemId);
					if (!ReadRequiredNumber(*ItemObject, TEXT("qty"), QuantityContext, Quantity, OutError))
					{
						return false;
					}
					if (Quantity < 1.0)
					{
						OutError = FString::Printf(TEXT("%s'qty' is less than 1"), *QuantityContext);
						return false;
					}
					// JSON has one number type, so 1.7 sails through the check above
					// and is then truncated to 1 by the cast below — the author asks
					// for one thing and the tutorial hands out another, with nothing
					// logged. Every other malformation here is named; this one is too.
					if (Quantity != FMath::TruncToDouble(Quantity))
					{
						OutError = FString::Printf(
							TEXT("containers[%d].fixedItems[%d] ('%s'): 'qty' must be a whole number, not %g — a fraction would be silently truncated"),
							Index, ItemIndex, *ItemId, Quantity);
						return false;
					}
					// Checked here, at load, and not at loot time: the backend's
					// plausibility gate rejects an unknown id and would reject the
					// whole haul at the end of the raid instead.
					if (SarkoLoot::GetItemCatalog().Find(FName(*ItemId)) == nullptr)
					{
						OutError = FString::Printf(
							TEXT("containers[%d].fixedItems[%d]: '%s' is not in Data/Items/items.json"),
							Index, ItemIndex, *ItemId);
						return false;
					}
					Spot.FixedItems.Add(FSarkoItemStack{ FName(*ItemId), static_cast<int32>(Quantity) });
				}
			}

			OutDefinition.Containers.Add(Spot);
		}
	}

	// playerSpawns — at least one, or there is nowhere to put the player
	const TArray<TSharedPtr<FJsonValue>>* Spawns = nullptr;
	if (!TryGetOptionalArrayField(Root, TEXT("playerSpawns"), Spawns, OutError))
	{
		return false;
	}
	if (Spawns)
	{
		for (int32 Index = 0; Index < Spawns->Num(); ++Index)
		{
			const TSharedPtr<FJsonValue>& Value = (*Spawns)[Index];
			const TSharedPtr<FJsonObject>* Object = nullptr;
			if (!Value->TryGetObject(Object) || !Object)
			{
				OutError = FString::Printf(TEXT("playerSpawns[%d]: not an object"), Index);
				return false;
			}
			FVector Location;
			if (!ReadVector(*Object, TEXT("pos"), Location, OutError))
			{
				OutError = FString::Printf(TEXT("playerSpawns[%d]: %s"), Index, *OutError);
				return false;
			}
			double Yaw = 0.0;
			if (!ReadYaw(*Object, Yaw, OutError))
			{
				OutError = FString::Printf(TEXT("playerSpawns[%d]: %s"), Index, *OutError);
				return false;
			}
			FString SpawnId;
			if (!ReadOptionalId(*Object, SpawnId, OutError))
			{
				OutError = FString::Printf(TEXT("playerSpawns[%d]: %s"), Index, *OutError);
				return false;
			}
			const FRotator Rotation(0.f, static_cast<float>(Yaw), 0.f);
			OutDefinition.PlayerSpawns.Add(FTransform(Rotation, Location));
			// Both arrays, same iteration, no early exit between them — the one
			// discipline that keeps the two index-aligned. An id read that fails
			// returns above, before either array has been touched.
			OutDefinition.PlayerSpawnIds.Add(SpawnId);
		}
	}
	if (OutDefinition.PlayerSpawns.Num() == 0)
	{
		OutError = TEXT("'playerSpawns' must contain at least one entry");
		return false;
	}

	// botSpawns
	const TArray<TSharedPtr<FJsonValue>>* Bots = nullptr;
	if (!TryGetOptionalArrayField(Root, TEXT("botSpawns"), Bots, OutError))
	{
		return false;
	}
	if (Bots)
	{
		for (int32 Index = 0; Index < Bots->Num(); ++Index)
		{
			const TSharedPtr<FJsonValue>& Value = (*Bots)[Index];
			const TSharedPtr<FJsonObject>* Object = nullptr;
			if (!Value->TryGetObject(Object) || !Object)
			{
				OutError = FString::Printf(TEXT("botSpawns[%d]: not an object"), Index);
				return false;
			}
			FSarkoBotSpot Spot;
			if (!ReadVector(*Object, TEXT("pos"), Spot.Location, OutError))
			{
				OutError = FString::Printf(TEXT("botSpawns[%d]: %s"), Index, *OutError);
				return false;
			}
			if (!ReadOptionalId(*Object, Spot.Id, OutError))
			{
				OutError = FString::Printf(TEXT("botSpawns[%d]: %s"), Index, *OutError);
				return false;
			}
			// Same reasoning as a container's tier: nothing downstream reads
			// Zone yet, so absence defaults to NAME_None; a wrong-typed value
			// is still a named error.
			FString Zone;
			if (!ReadOptionalString(*Object, TEXT("zone"), Zone, OutError))
			{
				OutError = FString::Printf(TEXT("botSpawns[%d]: %s"), Index, *OutError);
				return false;
			}
			Spot.Zone = FName(*Zone);
			OutDefinition.BotSpawns.Add(Spot);
		}
	}

	// extractions
	const TArray<TSharedPtr<FJsonValue>>* Extractions = nullptr;
	if (!TryGetOptionalArrayField(Root, TEXT("extractions"), Extractions, OutError))
	{
		return false;
	}
	if (Extractions)
	{
		for (int32 Index = 0; Index < Extractions->Num(); ++Index)
		{
			const TSharedPtr<FJsonValue>& Value = (*Extractions)[Index];
			const TSharedPtr<FJsonObject>* Object = nullptr;
			if (!Value->TryGetObject(Object) || !Object)
			{
				OutError = FString::Printf(TEXT("extractions[%d]: not an object"), Index);
				return false;
			}
			FSarkoExtractionSpot Spot;
			if (!ReadVector(*Object, TEXT("pos"), Spot.Location, OutError))
			{
				OutError = FString::Printf(TEXT("extractions[%d]: %s"), Index, *OutError);
				return false;
			}
			if (!ReadOptionalId(*Object, Spot.Id, OutError))
			{
				OutError = FString::Printf(TEXT("extractions[%d]: %s"), Index, *OutError);
				return false;
			}
			// Absence keeps the struct's own default (400 uu); presence must
			// parse and clear a sane minimum, or the zone could be too small
			// to ever register an overlap.
			double RadiusUU = static_cast<double>(Spot.RadiusUU);
			if (!ReadOptionalNumber(*Object, TEXT("radiusUU"), RadiusUU, OutError))
			{
				OutError = FString::Printf(TEXT("extractions[%d]: %s"), Index, *OutError);
				return false;
			}
			if (RadiusUU < MinExtractionRadiusUU)
			{
				OutError = FString::Printf(TEXT("extractions[%d]: 'radiusUU' must be at least %.0f, found %.1f"),
					Index, MinExtractionRadiusUU, RadiusUU);
				return false;
			}
			Spot.RadiusUU = static_cast<float>(RadiusUU);

			// Optional, and absent means "open from the first frame" — which is
			// three of the four zones on this map. Negative is refused rather than
			// clamped: it can only be a typo, and a zone that reads as "opens
			// 600 seconds ago" is a zone whose author meant something else.
			double OpensAfterSeconds = 0.0;
			if (!ReadOptionalNumber(*Object, TEXT("opensAfterSeconds"), OpensAfterSeconds, OutError))
			{
				OutError = FString::Printf(TEXT("extractions[%d]: %s"), Index, *OutError);
				return false;
			}
			if (OpensAfterSeconds < 0.0)
			{
				OutError = FString::Printf(
					TEXT("extractions[%d]: 'opensAfterSeconds' must not be negative, found %.1f"),
					Index, OpensAfterSeconds);
				return false;
			}
			Spot.OpensAfterSeconds = static_cast<float>(OpensAfterSeconds);

			if (!ReadOptionalString(*Object, TEXT("name"), Spot.Name, OutError))
			{
				OutError = FString::Printf(TEXT("extractions[%d]: %s"), Index, *OutError);
				return false;
			}
			OutDefinition.Extractions.Add(Spot);
		}
	}

	// encounterBudget — optional as a section, all-or-nothing inside it. A map
	// with encounters and no budget would be a map whose enemy count is bounded
	// by nothing, which is the exact thing the encounter system exists to stop,
	// so that combination is refused below once both sections are read.
	if (Root->HasField(TEXT("encounterBudget")))
	{
		const TSharedPtr<FJsonObject>* BudgetObject = nullptr;
		if (!Root->TryGetObjectField(TEXT("encounterBudget"), BudgetObject) || !BudgetObject)
		{
			OutError = TEXT("'encounterBudget' is present but not an object");
			return false;
		}
		const FString Context = TEXT("encounterBudget: ");
		if (!ReadRequiredWholeNumber(*BudgetObject, TEXT("tutorial"), Context, OutDefinition.EncounterBudget.Tutorial, OutError) ||
			!ReadRequiredWholeNumber(*BudgetObject, TEXT("normal"), Context, OutDefinition.EncounterBudget.Normal, OutError) ||
			!ReadRequiredWholeNumber(*BudgetObject, TEXT("firstFightMaxAlive"), Context, OutDefinition.EncounterBudget.FirstFightMaxAlive, OutError))
		{
			return false;
		}
		if (OutDefinition.EncounterBudget.Tutorial < 1 || OutDefinition.EncounterBudget.Normal < 1 ||
			OutDefinition.EncounterBudget.FirstFightMaxAlive < 1)
		{
			OutError = FString::Printf(
				TEXT("encounterBudget: 'tutorial' (%d), 'normal' (%d) and 'firstFightMaxAlive' (%d) must each be at least 1 — a zero budget is a map with encounters that can never fire, which is indistinguishable from a map with none"),
				OutDefinition.EncounterBudget.Tutorial, OutDefinition.EncounterBudget.Normal,
				OutDefinition.EncounterBudget.FirstFightMaxAlive);
			return false;
		}
		OutDefinition.EncounterBudget.bAuthored = true;
	}

	// encounters
	const TArray<TSharedPtr<FJsonValue>>* Encounters = nullptr;
	if (!TryGetOptionalArrayField(Root, TEXT("encounters"), Encounters, OutError))
	{
		return false;
	}
	if (Encounters)
	{
		for (int32 Index = 0; Index < Encounters->Num(); ++Index)
		{
			const TSharedPtr<FJsonObject>* Object = nullptr;
			if (!(*Encounters)[Index]->TryGetObject(Object) || !Object)
			{
				OutError = FString::Printf(TEXT("encounters[%d]: not an object"), Index);
				return false;
			}
			FSarkoEncounter Encounter;

			// Required, like a building's id and unlike every optional-id
			// section: an encounter's id is what the arm/spawn/budget log lines
			// print, and an anonymous event cannot be traced through a raid log.
			FString IdContext = FString::Printf(TEXT("encounters[%d]: "), Index);
			if (!ReadRequiredString(*Object, TEXT("id"), IdContext, Encounter.Id, OutError))
			{
				return false;
			}
			const FString Context = FString::Printf(TEXT("encounters[%d] ('%s'): "), Index, *Encounter.Id);

			if (!ReadRequiredWholeNumber(*Object, TEXT("order"), Context, Encounter.Order, OutError) ||
				!ReadRequiredWholeNumber(*Object, TEXT("budgetCost"), Context, Encounter.BudgetCost, OutError) ||
				!ReadRequiredWholeNumber(*Object, TEXT("maxAlive"), Context, Encounter.MaxAlive, OutError) ||
				!ReadRequiredBool(*Object, TEXT("oneShot"), Context, Encounter.bOneShot, OutError))
			{
				return false;
			}
			if (Encounter.BudgetCost < 1)
			{
				OutError = FString::Printf(TEXT("%s'budgetCost' must be at least 1, found %d"), *Context, Encounter.BudgetCost);
				return false;
			}
			if (Encounter.MaxAlive < 1)
			{
				OutError = FString::Printf(TEXT("%s'maxAlive' must be at least 1, found %d"), *Context, Encounter.MaxAlive);
				return false;
			}

			// trigger
			const TSharedPtr<FJsonObject>* TriggerObject = nullptr;
			if (!(*Object)->TryGetObjectField(TEXT("trigger"), TriggerObject) || !TriggerObject)
			{
				OutError = FString::Printf(TEXT("%s'trigger' is missing or not an object"), *Context);
				return false;
			}
			{
				const FString TriggerContext = FString::Printf(TEXT("%strigger: "), *Context);
				FString KindName;
				if (!ReadRequiredString(*TriggerObject, TEXT("kind"), TriggerContext, KindName, OutError))
				{
					return false;
				}
				// An unlisted kind is an error rather than a fallback to radius,
				// for exactly the reason an unlisted surface is: a typo would
				// become a trigger of the wrong shape in the right place, which
				// reads as a gameplay bug and not as a data bug.
				if (KindName != TEXT("radius"))
				{
					OutError = FString::Printf(TEXT("%s'kind' must be \"radius\" (the only trigger shape today), found '%s'"),
						*TriggerContext, *KindName);
					return false;
				}
				Encounter.Trigger.Kind = ESarkoTriggerKind::Radius;

				if (!ReadVector2(*TriggerObject, TEXT("pos"), Encounter.Trigger.Location, OutError))
				{
					OutError = FString::Printf(TEXT("%s%s"), *TriggerContext, *OutError);
					return false;
				}
				double RadiusUU = 0.0;
				double ArmAfterUU = 0.0;
				if (!ReadRequiredNumber(*TriggerObject, TEXT("radiusUU"), TriggerContext, RadiusUU, OutError) ||
					!ReadRequiredNumber(*TriggerObject, TEXT("armAfterUU"), TriggerContext, ArmAfterUU, OutError))
				{
					return false;
				}
				if (RadiusUU <= 0.0)
				{
					OutError = FString::Printf(TEXT("%s'radiusUU' is not positive"), *TriggerContext);
					return false;
				}
				// The hysteresis band has to BE a band. Equal values are a band of
				// zero width, which is the pumping bug this field exists to stop.
				if (ArmAfterUU <= RadiusUU)
				{
					OutError = FString::Printf(
						TEXT("%s'armAfterUU' (%.1f) must be greater than 'radiusUU' (%.1f) — it is the hysteresis band, and a band of zero width lets a player standing on the boundary arm the trigger several times a second"),
						*TriggerContext, ArmAfterUU, RadiusUU);
					return false;
				}
				Encounter.Trigger.RadiusUU = static_cast<float>(RadiusUU);
				Encounter.Trigger.ArmAfterUU = static_cast<float>(ArmAfterUU);
			}

			// spawns (named SpawnRows, not Spawns: the playerSpawns array above is
			// still in scope and -Wshadow is an error in this build)
			const TArray<TSharedPtr<FJsonValue>>* SpawnRows = nullptr;
			if (!(*Object)->TryGetArrayField(TEXT("spawns"), SpawnRows) || !SpawnRows)
			{
				OutError = FString::Printf(TEXT("%s'spawns' is missing or not an array"), *Context);
				return false;
			}
			if (SpawnRows->Num() == 0)
			{
				OutError = FString::Printf(TEXT("%s'spawns' is empty — an encounter with no authored spawn point can never fire"), *Context);
				return false;
			}
			for (int32 SpawnIndex = 0; SpawnIndex < SpawnRows->Num(); ++SpawnIndex)
			{
				const TSharedPtr<FJsonObject>* SpawnObject = nullptr;
				if (!(*SpawnRows)[SpawnIndex]->TryGetObject(SpawnObject) || !SpawnObject)
				{
					OutError = FString::Printf(TEXT("%sspawns[%d] is not an object"), *Context, SpawnIndex);
					return false;
				}
				FSarkoEncounterSpawn Spawn;
				const FString SpawnContext = FString::Printf(TEXT("%sspawns[%d]: "), *Context, SpawnIndex);
				if (!ReadRequiredString(*SpawnObject, TEXT("id"), SpawnContext, Spawn.Id, OutError))
				{
					return false;
				}
				if (!ReadVector(*SpawnObject, TEXT("pos"), Spawn.Location, OutError) ||
					!ReadVector2(*SpawnObject, TEXT("postPos"), Spawn.PostPos, OutError))
				{
					OutError = FString::Printf(TEXT("%s%s"), *SpawnContext, *OutError);
					return false;
				}
				FString ArchetypeName;
				if (!ReadRequiredString(*SpawnObject, TEXT("archetype"), SpawnContext, ArchetypeName, OutError))
				{
					return false;
				}
				// Checked against the table here, at load, for the same reason a
				// fixedItems id is checked against the catalog here: the
				// alternative is a bot that silently never appears, in a raid,
				// from a file that parses.
				FSarkoBotArchetype Archetype;
				if (!SarkoAI::FindBotArchetype(FName(*ArchetypeName), Archetype))
				{
					OutError = FString::Printf(TEXT("%s'%s' is not a known bot archetype"), *SpawnContext, *ArchetypeName);
					return false;
				}
				Spawn.Archetype = FName(*ArchetypeName);

				double LeashUU = 0.0;
				if (!ReadRequiredNumber(*SpawnObject, TEXT("leashUU"), SpawnContext, LeashUU, OutError))
				{
					return false;
				}
				if (LeashUU <= 0.0)
				{
					OutError = FString::Printf(TEXT("%s'leashUU' is not positive — a bot with no leash is the bug the leash exists to fix"), *SpawnContext);
					return false;
				}
				Spawn.LeashUU = static_cast<float>(LeashUU);
				Encounter.Spawns.Add(Spawn);
			}

			// A ceiling higher than the number of doors is a ceiling that can
			// never be reached, and it reads as an authoring intent that the map
			// silently does not honour.
			if (Encounter.MaxAlive > Encounter.Spawns.Num())
			{
				OutError = FString::Printf(
					TEXT("%s'maxAlive' is %d but only %d spawn point(s) are authored — the encounter could never reach its own ceiling"),
					*Context, Encounter.MaxAlive, Encounter.Spawns.Num());
				return false;
			}

			OutDefinition.Encounters.Add(Encounter);
		}
	}

	// The one cross-section rule: encounters without a budget is a map whose
	// enemy count is bounded by nothing.
	if (OutDefinition.Encounters.Num() > 0 && !OutDefinition.EncounterBudget.bAuthored)
	{
		OutError = FString::Printf(
			TEXT("the file authors %d encounter(s) but no 'encounterBudget' — the budget is the ceiling for the raid, and without it the enemy count is bounded by nothing"),
			OutDefinition.Encounters.Num());
		return false;
	}

	// Uniqueness is a parse-time rule rather than a caller's responsibility:
	// every consumer of a definition assumes an id names one object, and there
	// is no safe behaviour for the case where it names two.
	TArray<FString> Ids;
	if (!CollectIds(OutDefinition, Ids, OutError))
	{
		return false;
	}

	// The GENERATED wall ids share that one namespace, and nothing above can see
	// them: CollectIds walks the authored file, while ExpandBuilding invents
	// "<buildingId>_<side>_<n>" and "<buildingId>_interior<w>_<n>" for every
	// segment. Both end up in Layout.Cover side by side, so an authored block
	// called "shed_north_0" next to a building called "shed" would put two
	// different boxes under one name and "the wall at shed_north_0" would mean
	// whichever the reader found first. Checked here, once, at load: a building's
	// own id is already unique, but the derived names are not implied by that —
	// a building "a" with an interior wall and a building "a_interior0" would
	// both be legal ids and could still collide downstream.
	{
		TSet<FString> Taken(Ids);
		TArray<FSarkoCoverBlock> Walls;
		FString ExpandError;
		if (!ExpandBuildings(OutDefinition.Buildings, Walls, ExpandError))
		{
			OutError = ExpandError;
			return false;
		}
		for (const FSarkoCoverBlock& Wall : Walls)
		{
			if (Taken.Contains(Wall.Id))
			{
				OutError = FString::Printf(
					TEXT("the wall id '%s' generated by a building is already used elsewhere in the file; rename the building or the entry that claims that name"),
					*Wall.Id);
				return false;
			}
			Taken.Add(Wall.Id);
		}
	}

	return true;
}

FSarkoMapLayout SarkoMap::ToLayout(const FSarkoMapDefinition& Definition)
{
	FSarkoMapLayout Layout;
	Layout.Extent = Definition.ExtentUU;
	Layout.Cover = Definition.Blocks;

	// Authored blocks first, then every building's walls in author order, so an
	// index into Layout.Cover means the same thing on every machine. Expansion
	// cannot fail here: ParseDefinition already ran the expander on every
	// building and refused the file if any of them was broken.
	TArray<FSarkoCoverBlock> BuildingWalls;
	FString ExpandError;
	if (ExpandBuildings(Definition.Buildings, BuildingWalls, ExpandError))
	{
		Layout.Cover.Append(BuildingWalls);
	}
	else
	{
		// Unreachable via LoadDefinitionFromDisk. Reachable if someone hands
		// ToLayout a definition they built in code, which is why it logs instead
		// of silently producing a map with no buildings in it.
		UE_LOG(LogTemp, Error, TEXT("SarkoMap: building expansion failed in ToLayout: %s"), *ExpandError);
	}

	Layout.PlayerStarts.Reserve(Definition.PlayerSpawns.Num());
	for (const FTransform& Spawn : Definition.PlayerSpawns)
	{
		Layout.PlayerStarts.Add(Spawn.GetLocation());
	}

	Layout.EnemySpawns.Reserve(Definition.BotSpawns.Num());
	for (const FSarkoBotSpot& Spot : Definition.BotSpawns)
	{
		Layout.EnemySpawns.Add(Spot.Location);
	}

	return Layout;
}

bool SarkoMap::LoadDefinitionFromDisk(const FString& MapId, FSarkoMapDefinition& OutDefinition, FString& OutError)
{
	const FString Path = FPaths::ProjectDir() / TEXT("Data") / TEXT("Maps") / (MapId + TEXT(".json"));

	FString Json;
	if (!FFileHelper::LoadFileToString(Json, *Path))
	{
		OutError = FString::Printf(TEXT("could not read map file at %s"), *Path);
		return false;
	}
	if (!ParseDefinition(Json, OutDefinition, OutError))
	{
		OutError = FString::Printf(TEXT("%s: %s"), *Path, *OutError);
		return false;
	}
	// Stricter than the pure parser on purpose (see RequireIdentifiedEntries).
	// A shipped map with an anonymous container cannot be audited against the
	// ТЗ's loot ledger, and Stage C authors 42 of them.
	FString IdError;
	if (!RequireIdentifiedEntries(OutDefinition, IdError))
	{
		OutError = FString::Printf(TEXT("%s: %s"), *Path, *IdError);
		OutDefinition = FSarkoMapDefinition();
		return false;
	}
	return true;
}

bool SarkoMap::CollectIds(const FSarkoMapDefinition& Definition, TArray<FString>& OutIds, FString& OutError)
{
	OutIds.Reset();
	OutError.Reset();

	TSet<FString> Seen;
	const auto Take = [&OutIds, &OutError, &Seen](const FString& Id, const TCHAR* Section, int32 Index) -> bool
	{
		if (Id.IsEmpty())
		{
			return true;
		}
		if (Seen.Contains(Id))
		{
			OutError = FString::Printf(TEXT("duplicate id '%s' (%s[%d])"), *Id, Section, Index);
			return false;
		}
		Seen.Add(Id);
		OutIds.Add(Id);
		return true;
	};

	for (int32 I = 0; I < Definition.Blocks.Num(); ++I)         { if (!Take(Definition.Blocks[I].Id, TEXT("blocks"), I))            { return false; } }
	for (int32 I = 0; I < Definition.Buildings.Num(); ++I)      { if (!Take(Definition.Buildings[I].Id, TEXT("buildings"), I))      { return false; } }
	for (int32 I = 0; I < Definition.Props.Num(); ++I)          { if (!Take(Definition.Props[I].Id, TEXT("props"), I))              { return false; } }
	for (int32 I = 0; I < Definition.Containers.Num(); ++I)     { if (!Take(Definition.Containers[I].Id, TEXT("containers"), I))    { return false; } }
	for (int32 I = 0; I < Definition.PlayerSpawnIds.Num(); ++I) { if (!Take(Definition.PlayerSpawnIds[I], TEXT("playerSpawns"), I)) { return false; } }
	for (int32 I = 0; I < Definition.BotSpawns.Num(); ++I)      { if (!Take(Definition.BotSpawns[I].Id, TEXT("botSpawns"), I))      { return false; } }
	for (int32 I = 0; I < Definition.Extractions.Num(); ++I)    { if (!Take(Definition.Extractions[I].Id, TEXT("extractions"), I))  { return false; } }
	// Encounters and the spawn points inside them share the one namespace with
	// everything above, for the reason the namespace is one: a report, a test or
	// a person reading "the thing called X" does not know which section X was
	// declared in, and there is no safe behaviour when the name means two things.
	for (int32 I = 0; I < Definition.Encounters.Num(); ++I)
	{
		if (!Take(Definition.Encounters[I].Id, TEXT("encounters"), I))
		{
			return false;
		}
		for (int32 S = 0; S < Definition.Encounters[I].Spawns.Num(); ++S)
		{
			if (!Take(Definition.Encounters[I].Spawns[S].Id, TEXT("encounters[].spawns"), S))
			{
				return false;
			}
		}
	}
	return true;
}

bool SarkoMap::RequireIdentifiedEntries(const FSarkoMapDefinition& Definition, FString& OutError)
{
	OutError.Reset();

	const auto Require = [&OutError](const FString& Id, const TCHAR* Section, int32 Index) -> bool
	{
		if (!Id.IsEmpty())
		{
			return true;
		}
		OutError = FString::Printf(TEXT("%s[%d] has no 'id'; containers, spawns, extractions and buildings must be named"),
			Section, Index);
		return false;
	};

	// PlayerSpawnIds is index-aligned with PlayerSpawns, so a spawn that never
	// reached the id array would be invisible to the loop below. Checked first,
	// because a shorter id array makes every later "every spawn is named"
	// verdict a statement about fewer spawns than the map has.
	if (Definition.PlayerSpawnIds.Num() != Definition.PlayerSpawns.Num())
	{
		OutError = FString::Printf(TEXT("playerSpawns: %d spawns but %d ids — the arrays must stay index-aligned"),
			Definition.PlayerSpawns.Num(), Definition.PlayerSpawnIds.Num());
		return false;
	}

	for (int32 I = 0; I < Definition.Buildings.Num(); ++I)      { if (!Require(Definition.Buildings[I].Id, TEXT("buildings"), I))      { return false; } }
	for (int32 I = 0; I < Definition.Containers.Num(); ++I)     { if (!Require(Definition.Containers[I].Id, TEXT("containers"), I))    { return false; } }
	for (int32 I = 0; I < Definition.PlayerSpawnIds.Num(); ++I) { if (!Require(Definition.PlayerSpawnIds[I], TEXT("playerSpawns"), I)) { return false; } }
	for (int32 I = 0; I < Definition.BotSpawns.Num(); ++I)      { if (!Require(Definition.BotSpawns[I].Id, TEXT("botSpawns"), I))      { return false; } }
	for (int32 I = 0; I < Definition.Extractions.Num(); ++I)    { if (!Require(Definition.Extractions[I].Id, TEXT("extractions"), I))  { return false; } }
	return true;
}
