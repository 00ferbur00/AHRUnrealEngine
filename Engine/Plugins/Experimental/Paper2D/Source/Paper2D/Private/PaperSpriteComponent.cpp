// Copyright 1998-2014 Epic Games, Inc. All Rights Reserved.

#include "Paper2DPrivatePCH.h"
#include "PaperSpriteSceneProxy.h"
#include "PaperSpriteComponent.h"
#include "PhysicsEngine/BodySetup2D.h"

//////////////////////////////////////////////////////////////////////////
// UPaperSpriteComponent

UPaperSpriteComponent::UPaperSpriteComponent(const FPostConstructInitializeProperties& PCIP)
	: Super(PCIP)
{
	SetCollisionProfileName(UCollisionProfile::BlockAllDynamic_ProfileName);

	MaterialOverride = nullptr;

	SpriteColor = FLinearColor::White;
}

#if WITH_EDITOR
void UPaperSpriteComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	FBodyInstanceEditorHelpers::EnsureConsistentMobilitySimulationSettingsOnPostEditChange(this, PropertyChangedEvent);

	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif

FPrimitiveSceneProxy* UPaperSpriteComponent::CreateSceneProxy()
{
	FPaperSpriteSceneProxy* NewProxy = new FPaperSpriteSceneProxy(this);
	FSpriteDrawCallRecord DrawCall;
	DrawCall.BuildFromSprite(SourceSprite);
	DrawCall.Color = SpriteColor;
	NewProxy->SetDrawCall_RenderThread(DrawCall);
	return NewProxy;
}

FBoxSphereBounds UPaperSpriteComponent::CalcBounds(const FTransform& LocalToWorld) const
{
	if (SourceSprite != NULL)
	{
		// Graphics bounds.
		FBoxSphereBounds NewBounds = SourceSprite->GetRenderBounds().TransformBy(LocalToWorld);

		// Add bounds of collision geometry (if present).
		if (UBodySetup* BodySetup = SourceSprite->BodySetup)
		{
			const FBox AggGeomBox = BodySetup->AggGeom.CalcAABB(LocalToWorld);
			if (AggGeomBox.IsValid)
			{
				NewBounds = Union(NewBounds,FBoxSphereBounds(AggGeomBox));
			}
		}

		// Apply bounds scale
		NewBounds.BoxExtent *= BoundsScale;
		NewBounds.SphereRadius *= BoundsScale;

		return NewBounds;
	}
	else
	{
		return FBoxSphereBounds(LocalToWorld.GetLocation(), FVector::ZeroVector, 0.f);
	}
}

void UPaperSpriteComponent::SendRenderDynamicData_Concurrent()
{
	if (SceneProxy != NULL)
	{
		FSpriteDrawCallRecord DrawCall;
		DrawCall.BuildFromSprite(SourceSprite);
		DrawCall.Color = SpriteColor;

		ENQUEUE_UNIQUE_RENDER_COMMAND_TWOPARAMETER(
				FSendPaperSpriteComponentDynamicData,
				FPaperRenderSceneProxy*,InSceneProxy,(FPaperRenderSceneProxy*)SceneProxy,
				FSpriteDrawCallRecord,InSpriteToSend,DrawCall,
			{
				InSceneProxy->SetDrawCall_RenderThread(InSpriteToSend);
			});
	}
}

bool UPaperSpriteComponent::HasAnySockets() const
{
	if (SourceSprite != NULL)
	{
		return SourceSprite->HasAnySockets();
	}

	return false;
}

FTransform UPaperSpriteComponent::GetSocketTransform(FName InSocketName, ERelativeTransformSpace TransformSpace) const
{
	if (SourceSprite != NULL)
	{
		if (FPaperSpriteSocket* Socket = SourceSprite->FindSocket(InSocketName))
		{
			FTransform SocketLocalTransform = Socket->LocalTransform;
			SocketLocalTransform.ScaleTranslation(SourceSprite->GetUnrealUnitsPerPixel());

			switch (TransformSpace)
			{
				case RTS_World:
					return SocketLocalTransform * ComponentToWorld;

				case RTS_Actor:
					if (const AActor* Actor = GetOwner())
					{
						const FTransform SocketTransform = SocketLocalTransform * ComponentToWorld;
						return SocketTransform.GetRelativeTransform(Actor->GetTransform());
					}
					break;

				case RTS_Component:
					return SocketLocalTransform;

				default:
					check(false);
			}
		}
	}

	return Super::GetSocketTransform(InSocketName, TransformSpace);
}

void UPaperSpriteComponent::QuerySupportedSockets(TArray<FComponentSocketDescription>& OutSockets) const
{
	if (SourceSprite != NULL)
	{
		return SourceSprite->QuerySupportedSockets(OutSockets);
	}
}

UBodySetup* UPaperSpriteComponent::GetBodySetup()
{
	return (SourceSprite != nullptr) ? SourceSprite->BodySetup : nullptr;
}

bool UPaperSpriteComponent::SetSprite(class UPaperSprite* NewSprite)
{
	if (NewSprite != SourceSprite)
	{
		// Don't allow changing the sprite if we are "static".
		AActor* Owner = GetOwner();
		if (!IsRegistered() || (Owner == NULL) || (Mobility != EComponentMobility::Static))
		{
			SourceSprite = NewSprite;

			// Need to send this to render thread at some point
			MarkRenderStateDirty();

			// Update physics representation right away
			RecreatePhysicsState();

			// Since we have new mesh, we need to update bounds
			UpdateBounds();

			return true;
		}
	}

	return false;
}

UPaperSprite* UPaperSpriteComponent::GetSprite()
{
	return SourceSprite;
}

void UPaperSpriteComponent::SetSpriteColor(FLinearColor NewColor)
{
	// Can't set color on a static component
	if (!(IsRegistered() && (Mobility == EComponentMobility::Static)) && (SpriteColor != NewColor))
	{
		SpriteColor = NewColor;

		//@TODO: Should we send immediately?
		MarkRenderDynamicDataDirty();
	}
}

FLinearColor UPaperSpriteComponent::GetWireframeColor() const
{
	return FLinearColor::Yellow;
}

const UObject* UPaperSpriteComponent::AdditionalStatObject() const
{
	return SourceSprite;
}
