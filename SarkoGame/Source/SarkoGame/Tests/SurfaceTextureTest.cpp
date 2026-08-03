#include "Misc/AutomationTest.h"

#include "Engine/Texture2D.h"
#include "Map/SarkoMapBuilder.h"
#include "Map/SarkoMapPalette.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/PackageName.h"
#include "UObject/SoftObjectPath.h"

#if WITH_AUTOMATION_TESTS

namespace
{
	/**
	 * The surfaces that are meant to have NO detail map, spelled out so that
	 * adding one to a surface on this list — or dropping one from a surface that
	 * is not — fails here rather than in a frame nobody re-takes.
	 *
	 * Six of the seven are argued in Palette::DetailTexturePath's comment: three
	 * are graphic rather than material (the two water tones and the extraction
	 * pad) and three are a luminance gradient standing in for distance (the
	 * skirt bands). The seventh, Ravine, is here because a frame said so — it had
	 * a detail map and the gorge bed looked identical with and without it.
	 */
	bool IsDeliberatelyFlat(ESarkoSurface Surface)
	{
		return Surface == ESarkoSurface::Water
			|| Surface == ESarkoSurface::Shallow
			|| Surface == ESarkoSurface::Ravine
			|| Surface == ESarkoSurface::Extraction
			|| Surface == ESarkoSurface::SkirtNear
			|| Surface == ESarkoSurface::SkirtMid
			|| Surface == ESarkoSurface::SkirtFar;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoSurfaceDetailIsBounded,
	"Sarko.Config.SurfaceDetailIsBounded",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The pure half of the palette-to-material mapping: every surface either has a
 * complete, in-range detail entry or none at all, and no entry can undo the two
 * things the palette table exists to guarantee.
 *
 * Cheap and total — it loops the enum, so a surface added tomorrow with a
 * half-filled detail row fails here the same way a surface with no colour
 * already fails Sarko.Config.SurfacePaletteIsReadable.
 */
bool FSarkoSurfaceDetailIsBounded::RunTest(const FString& Parameters)
{
	using namespace SarkoMap::Palette;

	for (int32 Index = 0; Index < static_cast<int32>(ESarkoSurface::Count); ++Index)
	{
		const ESarkoSurface Surface = static_cast<ESarkoSurface>(Index);
		const FString Name = SarkoMap::SurfaceName(Surface);
		const TCHAR* Path = DetailTexturePath(Surface);

		if (IsDeliberatelyFlat(Surface))
		{
			// Flat is a decision, and a decision that saves a texture sample on
			// every pixel of the largest water plane in the sector. It must not
			// drift back on by accident.
			TestNull(*FString::Printf(TEXT("'%s' is deliberately flat"), *Name), Path);
			TestEqual(*FString::Printf(TEXT("'%s' asks for no tiling"), *Name), DetailTileUU(Surface), 0.f);
			TestEqual(*FString::Printf(TEXT("'%s' asks for no strength"), *Name), DetailStrength(Surface), 0.f);
			continue;
		}

		TestNotNull(*FString::Printf(TEXT("'%s' has a detail map"), *Name), Path);
		if (!Path)
		{
			continue;
		}

		// The tile band is the top-down camera's, not a texture artist's. Below
		// ~150 uu a tile repeats several times across one prop and reads as a
		// pattern; above ~4000 it is a stain across half the sector and stops
		// reading as material at all.
		const float Tile = DetailTileUU(Surface);
		TestTrue(*FString::Printf(TEXT("'%s' tiles between 150 and 4000 uu (%.0f)"), *Name, Tile),
			Tile >= 150.f && Tile <= 4000.f);

		// A strength of 1 would take a texel to black and to double brightness:
		// past that the map IS the base colour and the palette is decoration.
		const float Strength = DetailStrength(Surface);
		TestTrue(*FString::Printf(TEXT("'%s' modulates the palette rather than replacing it (%.2f)"), *Name, Strength),
			Strength > 0.f && Strength <= 0.7f);

		// The roughness rules in SarkoMapPalette.cpp are not taste — they are
		// what stops a 40000 uu plane catching one specular sheet across the
		// whole sector. A detail map may vary roughness; it may not take a matte
		// surface glossy at either extreme of the map.
		const float Swing = DetailRoughnessSwing(Surface);
		const float Base = RoughnessFor(Surface);
		TestTrue(*FString::Printf(TEXT("'%s' roughness swing is small (%.2f)"), *Name, Swing),
			Swing >= 0.f && Swing <= 0.4f);
		TestTrue(*FString::Printf(TEXT("'%s' stays matte at its glossiest (%.2f)"), *Name, Base - Swing * 0.5f),
			Base - Swing * 0.5f >= 0.5f);
		TestTrue(*FString::Printf(TEXT("'%s' roughness stays legal (%.2f)"), *Name, Base + Swing * 0.5f),
			Base + Swing * 0.5f <= 1.f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoSurfaceTexturesAreLinearMultipliers,
	"Sarko.Config.SurfaceTexturesAreLinearMultipliers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Every generated map exists, and carries the import settings that make the
 * palette argument true.
 *
 * sRGB is the one that matters and the one nothing else would catch. These maps
 * are a LINEAR multiplier normalised to a mean texel of 0.502, so the mean
 * multiplier is 1.0 and a textured surface averages to exactly its palette
 * colour. Decoded as sRGB that mean becomes 0.216, every surface in the sector
 * renders about a third darker than the palette says, and the symptom is
 * "the lighting looks wrong" — which is the last place anyone would look.
 *
 * Asserted on the ASSET rather than on the code, for the same reason
 * Sarko.Config.PropMeshBoundsAreNormalised is: the setting is applied by a
 * Python script outside the build, and nothing in C++ can tell whether that
 * script ran.
 */
bool FSarkoSurfaceTexturesAreLinearMultipliers::RunTest(const FString& Parameters)
{
	int32 Checked = 0;
	for (int32 Index = 0; Index < static_cast<int32>(ESarkoSurface::Count); ++Index)
	{
		const ESarkoSurface Surface = static_cast<ESarkoSurface>(Index);
		const TCHAR* Path = SarkoMap::Palette::DetailTexturePath(Surface);
		if (!Path)
		{
			continue;
		}
		const FString Name = SarkoMap::SurfaceName(Surface);

		UTexture2D* Texture = LoadObject<UTexture2D>(nullptr, Path);
		if (!TestNotNull(*FString::Printf(TEXT("'%s' detail map loads (%s)"), *Name, Path), Texture))
		{
			continue;
		}
		++Checked;

		TestFalse(*FString::Printf(TEXT("'%s' detail map is linear, not sRGB"), *Name), Texture->SRGB);
		TestEqual(*FString::Printf(TEXT("'%s' detail map is in the World texture group"), *Name),
			static_cast<int32>(Texture->LODGroup), static_cast<int32>(TEXTUREGROUP_World));
		// Wrap, or a tiling texture does not tile — it clamps, and every surface
		// in the sector becomes one enormous stretched texel outside the first
		// tile.
		TestEqual(*FString::Printf(TEXT("'%s' detail map wraps in U"), *Name),
			static_cast<int32>(Texture->AddressX), static_cast<int32>(TA_Wrap));
		TestEqual(*FString::Printf(TEXT("'%s' detail map wraps in V"), *Name),
			static_cast<int32>(Texture->AddressY), static_cast<int32>(TA_Wrap));
		// This ships to phones. The device profile caps the World group at 512
		// for iOS, and a source larger than that is memory paid for in the
		// editor and dropped in the cook.
		//
		// Source.GetSizeX(), not GetSurfaceWidth(): the latter reads the built
		// PLATFORM data, which a -nullrhi automation run has not streamed in, so
		// it answers 0 for every texture in the set and the check reads as ten
		// failures rather than as "not applicable here". The source size is
		// editor data and is always there, and it is also the number this
		// assertion is actually about — what the generator wrote.
		const int32 Width = Texture->Source.GetSizeX();
		TestTrue(*FString::Printf(TEXT("'%s' detail map is at most 512 (%d)"), *Name, Width),
			Width > 0 && Width <= 512);
	}

	TestTrue(TEXT("the generated texture set is present at all"), Checked > 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSarkoSurfaceMaterialTakesThePalette,
	"Sarko.Config.SurfaceMaterialTakesThePalette",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * THE PALETTE-TO-MATERIAL MAPPING, end to end: the parameter names C++ writes
 * are the parameter names the generated material exposes, and a shared instance
 * for a real surface comes back carrying the palette's own numbers.
 *
 * This is the test that would have caught the whole class of failure this
 * pipeline invites. UMaterialInstanceDynamic::SetScalarParameterValue on a name
 * the material does not have is a SILENT no-op — no warning, no log, nothing —
 * so a typo in "DetailTileUU" costs one texture sample per pixel and produces a
 * surface tiled at the material's default instead of at the palette's, and the
 * only evidence is that the ground looks slightly wrong in a screenshot.
 */
bool FSarkoSurfaceMaterialTakesThePalette::RunTest(const FString& Parameters)
{
	const FString Package = FSoftObjectPath(SarkoMap::SurfaceMaterialPath).GetLongPackageName();
	if (!TestTrue(TEXT("the generated surface material exists (run Scripts/generate-textures.sh)"),
		FPackageName::DoesPackageExist(Package)))
	{
		return false;
	}

	UMaterial* Material = LoadObject<UMaterial>(nullptr, SarkoMap::SurfaceMaterialPath);
	if (!TestNotNull(TEXT("the generated surface material loads"), Material))
	{
		return false;
	}

	// Without this the instanced path loses everything. ASarkoPropField paints
	// HISM components with this material, and a material that declares no
	// instanced usage compiles no instanced vertex factory — so every tree,
	// rock, wreck and crate in the sector silently falls back to the engine
	// default material, with its colour and its detail both gone.
	// GetUsageByFlag rather than the bUsedWithInstancedStaticMeshes field: the
	// field is deprecated and the accessor is the API that survives the next
	// engine upgrade.
	TestTrue(TEXT("the material is compiled for instanced static meshes"),
		Material->GetUsageByFlag(MATUSAGE_InstancedStaticMeshes));

	// The names, exactly as SharedSurfaceMaterial writes them.
	float Scalar = 0.f;
	for (const TCHAR* Name : { TEXT("Roughness"), TEXT("DetailTileUU"), TEXT("DetailStrength"), TEXT("DetailRoughness") })
	{
		TestTrue(*FString::Printf(TEXT("material exposes scalar '%s'"), Name),
			Material->GetScalarParameterValue(FMaterialParameterInfo(Name), Scalar));
	}
	FLinearColor Vector = FLinearColor::Black;
	TestTrue(TEXT("material exposes vector 'Color'"),
		Material->GetVectorParameterValue(FMaterialParameterInfo(TEXT("Color")), Vector));
	UTexture* Texture = nullptr;
	TestTrue(TEXT("material exposes texture 'Detail'"),
		Material->GetTextureParameterValue(FMaterialParameterInfo(TEXT("Detail")), Texture));

	// The strengths must DEFAULT to zero. An instance that failed to set them
	// then renders the flat colour this project shipped before, which is the
	// safe failure; a non-zero default would make an unset parameter a visible
	// art change with no code anywhere saying so.
	TestTrue(TEXT("detail strength defaults to off"),
		Material->GetScalarParameterValue(FMaterialParameterInfo(TEXT("DetailStrength")), Scalar) && Scalar == 0.f);
	TestTrue(TEXT("detail roughness defaults to off"),
		Material->GetScalarParameterValue(FMaterialParameterInfo(TEXT("DetailRoughness")), Scalar) && Scalar == 0.f);

	// And the round trip, on one textured surface and one deliberately flat one.
	UMaterialInterface* GroundMaterial = SarkoMap::SharedSurfaceMaterial(ESarkoSurface::Ground);
	if (TestNotNull(TEXT("Ground gets a shared material instance"), GroundMaterial))
	{
		TestTrue(TEXT("Ground's instance carries the palette colour"),
			GroundMaterial->GetVectorParameterValue(FMaterialParameterInfo(TEXT("Color")), Vector)
			&& Vector.Equals(SarkoMap::Palette::ColourFor(ESarkoSurface::Ground), 0.001f));
		TestTrue(TEXT("Ground's instance carries the palette tiling"),
			GroundMaterial->GetScalarParameterValue(FMaterialParameterInfo(TEXT("DetailTileUU")), Scalar)
			&& FMath::IsNearlyEqual(Scalar, SarkoMap::Palette::DetailTileUU(ESarkoSurface::Ground)));
		TestTrue(TEXT("Ground's instance carries the palette strength"),
			GroundMaterial->GetScalarParameterValue(FMaterialParameterInfo(TEXT("DetailStrength")), Scalar)
			&& FMath::IsNearlyEqual(Scalar, SarkoMap::Palette::DetailStrength(ESarkoSurface::Ground)));
		TestTrue(TEXT("Ground's instance carries a detail map"),
			GroundMaterial->GetTextureParameterValue(FMaterialParameterInfo(TEXT("Detail")), Texture) && Texture != nullptr);
	}

	// The extraction pad is the sector's one gameplay colour and it must stay
	// the flat green it has always been — on the old engine material, paying
	// nothing.
	UMaterialInterface* ExtractionMaterial = SarkoMap::SharedSurfaceMaterial(ESarkoSurface::Extraction);
	if (TestNotNull(TEXT("Extraction gets a shared material instance"), ExtractionMaterial))
	{
		TestTrue(TEXT("Extraction's instance carries the palette colour"),
			ExtractionMaterial->GetVectorParameterValue(FMaterialParameterInfo(TEXT("Color")), Vector)
			&& Vector.Equals(SarkoMap::Palette::ColourFor(ESarkoSurface::Extraction), 0.001f));
		TestFalse(TEXT("Extraction pays for no detail sample at all"),
			ExtractionMaterial->GetScalarParameterValue(FMaterialParameterInfo(TEXT("DetailStrength")), Scalar));
	}

	return true;
}

#endif
