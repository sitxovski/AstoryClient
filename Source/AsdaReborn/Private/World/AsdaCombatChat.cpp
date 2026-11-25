// Copyright (c) 2026 @sitxovski (Discord)
// ★ R5 (12.11.2026, дубли): строка боя в системный чат — см. AsdaCombatChat.h.
#include "World/AsdaCombatChat.h"
#include "Engine/GameInstance.h"
#include "Text/AsdaTextSubsystem.h"
#include "UI/AsdaChatSubsystem.h"

EAsdaCombatChat AsdaCombatChat::Line(UGameInstance* GI, int32 NoticeId, const TArray<FString>& Strs, const TArray<int32>& Nums,
	const FColor& Col, FString* OutLine)
{
	UAsdaTextSubsystem* Txt = GI ? GI->GetSubsystem<UAsdaTextSubsystem>() : nullptr;
	UAsdaChatSubsystem* Chat = GI ? GI->GetSubsystem<UAsdaChatSubsystem>() : nullptr;
	if (!Txt || !Chat) { return EAsdaCombatChat::NoSubsystems; }
	const FString Line = Txt->FormatById(NoticeId, Strs, Nums);
	if (OutLine) { *OutLine = Line; }
	if (Line.IsEmpty()) { return EAsdaCombatChat::EmptyLine; }
	Chat->AddSystemMessage(Line, Col, EAsdaSysTrack::A, 10000.f);   // `CChat_AddTokenizedLines(…, цвет, 10000, 0)`
	return EAsdaCombatChat::Posted;
}
