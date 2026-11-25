// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ [ОРИГИНАЛ] 05.11.2026 — гейт «ограничивающее состояние → 7007» (шапка — AsdaItemUseStateGate.h).
#include "Items/AsdaItemUseStateGate.h"
#include "AsdaDevConsole.h"          // ASDA_DEV_CVAR — дев-ключ вне Shipping
#include "UI/AsdaEffectsSubsystem.h" // список состояний персонажа (порт `gap_96A[128]`)
#include "UI/AsdaNotice.h"           // плашка 221
#include "UI/AsdaNoticeColors.h"
#include "UI/AsdaMessageSubsystem.h"
#include "Engine/GameInstance.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaItemStateGate, Log, All);

// Ключ: 1 — как оригинал (под ограничивающим состоянием предметы вне 1…2000 не применяются, плашка 7007); 0 — прежний порт.
static ASDA_DEV_CVAR(int32, CVarStateLockGate,
	TEXT("asda.Item.StateLockGate"),
	1,
	TEXT("[ОРИГИНАЛ] 05.11.2026: 1 — под оледенением/окаменением/ловушкой/оглушением/паникой/сном расходники и предметы панели "
	     "быстрого доступа не применяются (плашка 7007), как оригинал; 0 — прежний порт, решает сервер (5029 код 12)."),
	ECVF_Default);

// Нотис отказа: «В таком состоянии ты не можешь использовать предметы.» (EN — тот же id).
static constexpr int32 kLocStateLocked = 7007;

// Счётчик показанных отказов (журнал/самотест; игровой логики не несёт).
static int32 GStateLockRefusals = 0;

int32 AsdaItemUseStateGate::GetRefusalCount() { return GStateLockRefusals; }

bool AsdaItemUseStateGate::IsSelfStateLocked(const UGameInstance* GI)
{
	const UAsdaEffectsSubsystem* Fx = GI ? const_cast<UGameInstance*>(GI)->GetSubsystem<UAsdaEffectsSubsystem>() : nullptr;
	if (!Fx) { return false; }
	const int32 Self = Fx->SelfUserIdx();
	// Ровно восемь id в порядке эталона `listProbeEntryTypesSetState@0xA38B40`, все с type == 0.
	static const int32 kIds[] = { 0, 1, 2, 9, 3, 14, 4, 8 };
	for (int32 Id : kIds)
	{
		if (Fx->FindByTypeSub(Self, Id, 0)) { return true; }
	}
	return false;
}

bool AsdaItemUseStateGate::RefuseIfStateLocked(const UGameInstance* GI, int32 ItemCode, const TCHAR* Where)
{
	if (CVarStateLockGate.GetValueOnGameThread() == 0 || !GI) { return false; }
	// Коды 1…2000 эталон пропускает при любом состоянии (`*(int*)slot < 1 || *(int*)slot > 2000`).
	if (ItemCode >= 1 && ItemCode <= 2000) { return false; }
	if (!IsSelfStateLocked(GI)) { return false; }
	AsdaNotice::PlankLoc(GI, EAsdaMsgCategory::ItemRefused, kLocStateLocked, AsdaNoticeColor::Gold, 10000.f, kLocStateLocked);
	++GStateLockRefusals;
	// Повтор автолопаты зовёт гейт КАЖДЫЙ кадр (как эталон) — журнал не чаще раза в секунду.
	static double LastLogS = 0.0;
	const double Now = FPlatformTime::Seconds();
	if (Now - LastLogS >= 1.0)
	{
		LastLogS = Now;
		UE_LOG(LogAsdaItemStateGate, Log, TEXT("[состояние] %s: предмет %d — персонаж в ограничивающем состоянии, не применяем (нотис %d)"),
			Where ? Where : TEXT("?"), ItemCode, kLocStateLocked);
	}
	return true;
}
