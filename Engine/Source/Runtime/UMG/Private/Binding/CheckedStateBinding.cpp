// Copyright 1998-2015 Epic Games, Inc. All Rights Reserved.

#include "UMGPrivatePCH.h"
#include "CheckedStateBinding.h"

#define LOCTEXT_NAMESPACE "UMG"

UCheckedStateBinding::UCheckedStateBinding()
{
}

bool UCheckedStateBinding::IsSupportedSource(UProperty* Property) const
{
	return IsSupportedDestination(Property) || IsConcreteTypeCompatibleWithReflectedType<bool>(Property);
}

bool UCheckedStateBinding::IsSupportedDestination(UProperty* Property) const
{
	static const FName CheckBoxStateEnum(TEXT("ESlateCheckBoxState"));

	if ( UByteProperty* ByteProperty = Cast<UByteProperty>(Property) )
	{
		if ( ByteProperty->IsEnum() )
		{
			return ByteProperty->Enum->GetFName() == CheckBoxStateEnum;
		}
	}

	return false;
}

ECheckBoxState UCheckedStateBinding::GetValue() const
{
	if ( UObject* Source = SourceObject.Get() )
	{
		if ( bConversion.Get(EConversion::None) == EConversion::None )
		{
			uint8 Value;
			if ( SourcePath.GetValue<uint8>(Source, Value) )
			{
				bConversion = EConversion::None;
				return static_cast<ECheckBoxState>(Value);
			}
		}

		if ( bConversion.Get(EConversion::Bool) == EConversion::Bool )
		{
			bool Value;
			if ( SourcePath.GetValue<bool>(Source, Value) )
			{
				bConversion = EConversion::Bool;
				return Value ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
			}
		}
	}

	return ECheckBoxState::Unchecked;
}

#undef LOCTEXT_NAMESPACE
