// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ [ИСПРАВЛЕНИЕ] 29.10.2026 — предпроверка минимального уровня предмета при применении (шапка — AsdaItemLevelGate.h).
#include "Items/AsdaItemLevelGate.h"
#include "Items/AsdaItemTableSubsystem.h"
#include "AsdaDevConsole.h"   // ASDA_DEV_CVAR — дев-ключ вне Shipping
#include "AsdaSelf.h"         // уровень своего персонажа — общая точка ядра
#include "UI/AsdaNotice.h"    // плашка 221 отказа предмета
#include "UI/AsdaNoticeColors.h"
#include "UI/AsdaMessageSubsystem.h"
#include "Engine/GameInstance.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaItemLevelGate, Log, All);

// Ключ исправления: 1 — проверять минимальный уровень при применении; 0 — как эталон (не проверять).
static ASDA_DEV_CVAR(int32, CVarUseLevelGate,
	TEXT("asda.Item.UseLevelGate"),
	1,
	TEXT("[ИСПРАВЛЕНИЕ] 29.10.2026: 1 — расходник (5028), коробка (6096) и бустер (5468) не применяются, пока уровень персонажа "
	     "ниже RequiredLevel предмета (плашка 104012); 0 — как эталон, уровень не проверяется."),
	ECVF_Default);

// Нотис отказа: «Ты не можешь использовать этот предмет: твой уровень слишком низок» (EN «Your level is too low to use the item»).
static constexpr int32 kLocLevelTooLow = 104012;

bool AsdaItemLevelGate::RefuseBelowLevel(const UGameInstance* GI, int32 ItemCode, EUse Use)
{
	if (CVarUseLevelGate.GetValueOnGameThread() == 0 || !GI) { return false; }
	const UAsdaItemTableSubsystem* IT = GI->GetSubsystem<UAsdaItemTableSubsystem>();
	const FAsdaItemRecord* Rec = IT ? IT->GetItem(ItemCode) : nullptr;
	if (!Rec) { return false; }
	const int32 Lv = AsdaSelf::GetLevel(GI);
	// Уровень ещё не пришёл — не гадаем: решит сервер (P95).
	if (Lv <= 0 || Lv >= Rec->RequiredLevel) { return false; }
	AsdaNotice::PlankLoc(GI, EAsdaMsgCategory::ItemRefused, kLocLevelTooLow, AsdaNoticeColor::Gold, 10000.f, kLocLevelTooLow);
	static const TCHAR* const kUseName[] = { TEXT("расходник 5028"), TEXT("коробка 6096"), TEXT("бустер 5468") };
	UE_LOG(LogAsdaItemLevelGate, Log, TEXT("[уровень предмета] %s: предмет %d требует уровень %d, у персонажа %d — не применяем (нотис %d)"),
		kUseName[(int32)Use], ItemCode, Rec->RequiredLevel, Lv, kLocLevelTooLow);
	return true;
}
