#include "Pawn/SarkoBody.h"

#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Character.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"

namespace
{
	/** The engine cylinder is 100 uu across and 100 uu tall at unit scale. */
	constexpr float EngineCylinderSize = 100.f;

	/** A blob on top marks facing, so it is obvious which way the pawn looks. */
	constexpr float HeadScale = 0.45f;
}

void SarkoBody::AttachPlaceholderBody(ACharacter& Character, const FLinearColor& Tint)
{
	UStaticMesh* Cylinder = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	UStaticMesh* Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	UMaterialInterface* BaseMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));

	if (!Cylinder || !Sphere || !BaseMaterial)
	{
		UE_LOG(LogTemp, Error, TEXT("SarkoBody: engine primitive missing; pawns will be invisible"));
		return;
	}

	const UCapsuleComponent* Capsule = Character.GetCapsuleComponent();
	const float HalfHeight = Capsule ? Capsule->GetScaledCapsuleHalfHeight() : 88.f;
	const float Radius = Capsule ? Capsule->GetScaledCapsuleRadius() : 34.f;

	const auto AddPiece = [&Character, BaseMaterial, &Tint](UStaticMesh* Mesh, const FName Name, const FVector& Offset, const FVector& Scale, float Brightness)
	{
		UStaticMeshComponent* Component = NewObject<UStaticMeshComponent>(&Character, Name);
		if (!Component)
		{
			return;
		}
		Component->SetupAttachment(Character.GetCapsuleComponent());
		Component->SetStaticMesh(Mesh);
		Component->SetRelativeLocation(Offset);
		Component->SetRelativeScale3D(Scale);
		// Cosmetic only: the capsule owns collision, and a mesh that blocked
		// traces would let a shot hit the body instead of the pawn.
		Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Component->RegisterComponent();

		if (UMaterialInstanceDynamic* Material = Component->CreateAndSetMaterialInstanceDynamicFromMaterial(0, BaseMaterial))
		{
			// BasicShapeMaterial exposes its tint as "Color"; older copies use
			// "BaseColor". Setting both is harmless — an unknown parameter name
			// is ignored — and beats the body silently staying default grey.
			Material->SetVectorParameterValue(TEXT("Color"), Tint * Brightness);
			Material->SetVectorParameterValue(TEXT("BaseColor"), Tint * Brightness);
		}
	};

	// Body: a cylinder filling the capsule.
	AddPiece(Cylinder, TEXT("BodyMesh"),
		FVector(0.f, 0.f, -HalfHeight),
		FVector((Radius * 2.f) / EngineCylinderSize, (Radius * 2.f) / EngineCylinderSize, (HalfHeight * 2.f) / EngineCylinderSize),
		1.f);

	// Head: offset forward as well as up, so facing is readable from directly
	// above — which is the only angle this game is ever seen from.
	AddPiece(Sphere, TEXT("HeadMesh"),
		FVector(Radius * 0.5f, 0.f, HalfHeight * 0.65f),
		FVector((Radius * 2.f * HeadScale) / EngineCylinderSize),
		1.4f);
}
