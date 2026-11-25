// Copyright (c) 2026 @sitxovski (Discord)
// UAsdaUpgradeStoneSubsystem — перенос улучшения камнем (204/206), 6551/6552. Поток, поля и отступления — шапка .h.
#include "UI/AsdaUpgradeStoneSubsystem.h"
#include "AsdaPerf.h"   // ★ R4-b (14.11.2026): области замера времени игрового потока
#include "AsdaDevConsole.h"   // ASDA_DEV_CONSOLE — дев-команда состояния вне Shipping
#include "UI/AsdaNotice.h"    // Loc / MsgBox
#include "UI/AsdaLocIds.h"    // AsdaLoc::kBtnOk
#include "UI/AsdaInventorySubsystem.h"
#include "UI/AsdaWindowSubsystem.h"
#include "Items/AsdaItemTableSubsystem.h"
#include "Items/AsdaItemDbRows.h"
#include "World/AsdaPlayerController.h"
#include "AsdaNetSubsystem.h"
#include "AsdaBytes.h"

// Окно сумки (`INVEN_WIN`, то же имя, что у `UAsdaInventorySubsystem`) — над ним курсор взвода держится P0.
static const FName kStoneInvenWindowId(TEXT("INVEN_WIN"));
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaUpgradeStone, Log, All);

// Откат исправления 31.10.2026: 0 — двойной щелчок по камню 204/206 уходит прежним общим использованием спец-предмета.
static ASDA_DEV_CVAR(int32, CVarUpgradeStoneEnable,
	TEXT("asda.UpgradeStone.Enable"), 1,
	TEXT("Перенос улучшения камнем (special_itemlist 204/206): 1 — взвод двойным щелчком, выбор вещи, вопрос 110322, 6551/6552 "
	     "(эталон colorMix_BeginPick@0xB65530); 0 — прежнее поведение порта (общее использование, перенос не работал)."),
	ECVF_Default);
// [ИСПРАВЛЕНИЕ] Таймаут «6551 в полёте»: у эталона флаг `+437` без ответа висел вечно и глушил все следующие «ОК».
static ASDA_DEV_CVAR(float, CVarUpgradeStonePendingTimeout,
	TEXT("asda.UpgradeStone.PendingTimeoutSec"), 10.f,
	TEXT("Перенос улучшения камнем: через сколько секунд без ответа 6552 снять флаг «6551 в полёте» [ИСПРАВЛЕНИЕ]; 0 — никогда (эталон)."),
	ECVF_Default);

namespace AsdaUpgradeStoneLoc
{
	constexpr int32 kAskTransfer = 110322;   // «Применить камень улучшения к [%s]? …» — вопрос 69
	constexpr int32 kNoGear      = 108515;   // «Снаряжение не выбрано»
	constexpr int32 kNoStone     = 108516;   // «Камень улучшения не выбран»
	constexpr int32 kErr         = 108530;   // «Ошибка» (код 0)
	constexpr int32 kDone        = 108531;   // «Перенос улучшения завершён» (после кодов 1 и 5)
	constexpr int32 kBadStone    = 108532;   // код 2 — данные камня
	constexpr int32 kBadGear     = 108533;   // код 3 — данные вещи
	constexpr int32 kEmptyStone  = 108534;   // код 4 — пустой камень
	constexpr int32 kDbErr       = 108535;   // код 99 — БД
	constexpr int32 kFail        = 110323;   // код 5 — не удалось, камень пропал
	constexpr int32 kSuccess     = 110324;   // код 1 — «[%s] [+%d%%] создан»
	constexpr int32 kAskNoticeId = 69;       // пара очереди окон-сообщений (1, 69) — `CItem_BuildTooltipText(ui, 1, 69, …)`
}

void UAsdaUpgradeStoneSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency<UAsdaNetSubsystem>();
	if (UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr)
	{
		// Закон `no-reentrant-net-callbacks`: из колбэка только кладём тело в очередь.
		HResult = Net->OnUpgradeBlessResult.AddLambda([this](const TArray<uint8>& B) { PendingResults.Push(B); });
	}
}

void UAsdaUpgradeStoneSubsystem::Deinitialize()
{
	if (UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr)
	{
		Net->OnUpgradeBlessResult.Remove(HResult);
	}
	Super::Deinitialize();
}

void UAsdaUpgradeStoneSubsystem::ResetSessionState(EAsdaSessionResetReason Reason)
{
	// [ПОРТ] (шапка): соединение пересоздано — ответа на 6551 уже не будет, указатели на ячейки протухли.
	if (bPick) { CancelPick(TEXT("сброс сессии")); }
	bPending = false;
	StoneCell = -1;
	GearCell = -1;
	TArray<TArray<uint8>> Drop;
	PendingResults.Drain(Drop);
	UE_LOG(LogAsdaUpgradeStone, Verbose, TEXT("[камень улучшения] сброс сессии (%s)"), AsdaSessionResetReasonName(Reason));
}

void UAsdaUpgradeStoneSubsystem::Tick(float /*DeltaTime*/)
{
	ASDA_PERF_SCOPE("Tick.UpgradeStone");   // ★ R4-b (14.11.2026): область замера (AsdaPerf.h; в Shipping пусто)
	if (TArray<TArray<uint8>> Local; PendingResults.Drain(Local))
	{
		for (const TArray<uint8>& B : Local) { OnResult(B); }
	}
	const float Timeout = CVarUpgradeStonePendingTimeout.GetValueOnGameThread();
	if (bPending && Timeout > 0.f && FPlatformTime::Seconds() - PendingSince > (double)Timeout)
	{
		bPending = false;
		UE_LOG(LogAsdaUpgradeStone, Warning, TEXT("[камень улучшения] ответа 6552 нет %.0f с — флаг «в полёте» снят [ИСПРАВЛЕНИЕ]"), Timeout);
	}
	TickPickCursor();
}

void UAsdaUpgradeStoneSubsystem::TickPickCursor()
{
	if (!bPick) { return; }
	UGameInstance* GI = GetGameInstance();
	const UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
	if (!Inv || !Inv->IsWindowOpen())
	{
		CancelPick(TEXT("сумка закрыта [ПОРТ]"));
		return;
	}
	const UAsdaWindowSubsystem* WM = GI->GetSubsystem<UAsdaWindowSubsystem>();
	const bool bOver = WM && WM->IsCursorOverWindow(kStoneInvenWindowId);
	if (bOver != bCursorOwned) { SetPickCursor(bOver); }
}

bool UAsdaUpgradeStoneSubsystem::IsValidPair(int32 StoneCode, int32 GearCode) const
{
	// `itemIsValidColorMixPair@0xB65590`: обе записи есть; 206 + `itemType <= 8` или 204 + `itemType == 9`.
	const UAsdaItemTableSubsystem* IT = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	if (!IT || StoneCode <= 0 || GearCode <= 0) { return false; }
	const FAsdaItemRecord* Gear = IT->GetItem(GearCode);
	FAsdaSpecialItemlistRow Sp;
	if (!Gear || !IT->GetSpecialItem(StoneCode, Sp)) { return false; }
	if (Sp.SubType == 206 && Gear->ItemType <= 8) { return true; }
	return Sp.SubType == 204 && Gear->ItemType == 9;
}

bool UAsdaUpgradeStoneSubsystem::OnBagDoubleClick(int32 CellIndex, bool bActionPending, bool& bOutResult)
{
	bOutResult = false;
	if (CVarUpgradeStoneEnable.GetValueOnGameThread() == 0) { return false; }
	UGameInstance* GI = GetGameInstance();
	const UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
	const UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	const FAsdaItemInstance* Src = Inv ? Inv->GetCell(1, CellIndex) : nullptr;
	if (!Src || !IT) { return false; }
	const FAsdaItemRecord* Rec = (Src->ItemCode > 0) ? IT->GetItem(Src->ItemCode) : nullptr;
	FAsdaSpecialItemlistRow Sp;
	const bool bSpecial = Src->ItemCode > 0 && IT->GetSpecialItem(Src->ItemCode, Sp);
	// Б6: эталонный порядок (item_inventory.c:38108-38117) — спец-предметы 104 (расширитель ячеек питомца) и 107
	//   (катализатор инкубатора) разбираются РАНЬШЕ ветки камня и взвод НЕ снимают (до `sub_938F54` дело не доходит).
	if (bSpecial && (Sp.SubType == 104 || Sp.SubType == 107)) { return false; }
	const bool bStone = bSpecial && IsTransferStoneSubType(Sp.SubType);
	// Гейт `sub_93723F` стоит перед всей веткой сумки (item_inventory.c:38087) — молча, без снятия взвода.
	if (bActionPending && (bStone || (bPick && Rec)))
	{
		UE_LOG(LogAsdaUpgradeStone, Log, TEXT("[камень улучшения] ячейка %d: ответ на прошлое действие с предметом ещё не пришёл (гейт sub_93723F) — молча"), CellIndex);
		return true;
	}
	if (bStone)
	{
		BeginPick(CellIndex);
		return true;   // эталон: `return 0` — предмет не используется
	}
	if (Rec && bPick)
	{
		// Коробки и подтип 66 — молча, взвод остаётся (item_inventory.c:38131-38132).
		if (Rec->ItemSubType == 66 || Rec->ItemSubType == 200 || Rec->ItemSubType == 201) { return true; }
		OnTargetPicked(CellIndex);
		return true;   // предмет не надевается
	}
	// `sub_938F54` перед обычным путём (item_inventory.c:38136): любой другой щелчок снимает взвод.
	if (bPick) { CancelPick(TEXT("двойной щелчок по другому предмету сумки")); }
	return false;
}

void UAsdaUpgradeStoneSubsystem::BeginPick(int32 CellIndex)
{
	StoneCell = CellIndex;
	bPick = true;
	SetPickCursor(true);
	UE_LOG(LogAsdaUpgradeStone, Log, TEXT("[камень улучшения] взведён камень из ячейки %d — курсор P0, ждём двойной щелчок по вещи"), CellIndex);
}

void UAsdaUpgradeStoneSubsystem::CancelPick(const TCHAR* Why)
{
	const bool bWas = bPick;
	bPick = false;
	StoneCell = -1;   // `*(this + 108) = 0`
	if (bCursorOwned) { SetPickCursor(false); }   // Б2: чужой курсор (самоцветы, миникарта) не трогаем
	if (bWas) { UE_LOG(LogAsdaUpgradeStone, Log, TEXT("[камень улучшения] взвод снят: %s"), Why ? Why : TEXT("")); }
}

void UAsdaUpgradeStoneSubsystem::OnTargetPicked(int32 CellIndex)
{
	if (bCursorOwned) { SetPickCursor(false); }
	if (!bPick) { return; }
	bPick = false;
	UGameInstance* GI = GetGameInstance();
	const UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
	const FAsdaItemInstance* Gear  = Inv ? Inv->GetCell(1, CellIndex) : nullptr;
	const FAsdaItemInstance* Stone = Inv ? Inv->GetCell(1, StoneCell) : nullptr;
	const int32 GearCode = Gear ? Gear->ItemCode : 0;
	const int32 StoneCode = Stone ? Stone->ItemCode : 0;
	if (!IsValidPair(StoneCode, GearCode))
	{
		UE_LOG(LogAsdaUpgradeStone, Log, TEXT("[камень улучшения] вещь %d (ячейка %d) не подходит камню %d — молча, взвод снят (эталон)"),
			GearCode, CellIndex, StoneCode);
		return;
	}
	GearCell = CellIndex;
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM) { return; }
	TWeakObjectPtr<UAsdaUpgradeStoneSubsystem> WThis(this);
	WM->ShowGameConfirm(BuildConfirmText(GearCode), [WThis](bool bYes)
	{
		if (bYes && WThis.IsValid()) { WThis->SendRequest(); }
	}, /*Gauge*/ 0.f, AsdaLoc::kBtnOk, 2289, AsdaUpgradeStoneLoc::kAskNoticeId, /*EtalonKind*/ 1);
	UE_LOG(LogAsdaUpgradeStone, Log, TEXT("[камень улучшения] цель %d (ячейка %d) для камня %d (ячейка %d) — вопрос (1, 69) 110322"),
		GearCode, CellIndex, StoneCode, StoneCell);
}

FString UAsdaUpgradeStoneSubsystem::BuildConfirmText(int32 GearCode) const
{
	const UGameInstance* GI = GetGameInstance();
	const UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	const FString Name = (IT && GearCode > 0) ? IT->GetItemName(GearCode) : FString();
	return FormatC(AsdaNotice::Loc(GI, AsdaUpgradeStoneLoc::kAskTransfer), Name, 0);
}

void UAsdaUpgradeStoneSubsystem::SendRequest()
{
	// `netSendAntiCheatChallengeResponse@0xB65D30`: гейты — «в полёте» молча, затем цель, затем камень.
	if (bPending) { return; }
	UGameInstance* GI = GetGameInstance();
	const UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
	if (GearCell < 0) { MsgBox(AsdaNotice::Loc(GI, AsdaUpgradeStoneLoc::kNoGear)); return; }
	if (StoneCell < 0) { MsgBox(AsdaNotice::Loc(GI, AsdaUpgradeStoneLoc::kNoStone)); return; }
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	const FAsdaItemInstance* Gear  = Inv ? Inv->GetCell(1, GearCell) : nullptr;
	const FAsdaItemInstance* Stone = Inv ? Inv->GetCell(1, StoneCell) : nullptr;
	if (!Net || !Gear || !Stone) { return; }
	// Эталон читает поля из ячеек в момент отправки (там указатели на записи сумки).
	if (Net->SendUpgradeBless6551(Stone->ItemCode, Stone->ContainerType, Stone->SlotIndex,
	                              Gear->ItemCode,  Gear->ContainerType,  Gear->SlotIndex))
	{
		bPending = true;   // `pNetClient[437] = 1`
		PendingSince = FPlatformTime::Seconds();
	}
}

void UAsdaUpgradeStoneSubsystem::OnResult(const TArray<uint8>& Body)
{
	bPending = false;   // эталон снимает флаг первым делом, до разбора кода
	if (Body.Num() < 119) { return; }
	UGameInstance* GI = GetGameInstance();
	const int32 Code = (int32)(int8)Body[0];
	using namespace AsdaUpgradeStoneLoc;
	switch (Code)
	{
	case 0:  MsgBox(AsdaNotice::Loc(GI, kErr));        break;
	case 2:  MsgBox(AsdaNotice::Loc(GI, kBadStone));   break;
	case 3:  MsgBox(AsdaNotice::Loc(GI, kBadGear));    break;
	case 4:  MsgBox(AsdaNotice::Loc(GI, kEmptyStone)); break;
	case 99: MsgBox(AsdaNotice::Loc(GI, kDbErr));      break;
	case 1:
	case 5:
	{
		// Вес поставила сеть (`HUD_UpdateMiniWeightBar`); здесь — обе записи общей точкой, затем нотисы.
		int32 Applied = 0;
		if (UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr)
		{
			Applied = Inv->ApplyItemOptionTransRecords(Body, TEXT("[камень улучшения] 6552"));
		}
		if (Code == 1)
		{
			// Имя — по коду записи вещи @58, значение — u16 @85 (запись вещи +27, `Util_Identity16`).
			const UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
			const int32 GearCode = AsdaBytes::RdI32(Body.GetData() + 58);
			if (IT && IT->GetItem(GearCode))
			{
				const int32 Pct = (int32)(uint16)AsdaBytes::RdI16(Body.GetData() + 85);
				MsgBox(FormatC(AsdaNotice::Loc(GI, kSuccess), IT->GetItemName(GearCode), Pct));
			}
		}
		else
		{
			MsgBox(AsdaNotice::Loc(GI, kFail));
		}
		MsgBox(AsdaNotice::Loc(GI, kDone));   // эталон показывает 108531 после обоих исходов
		UE_LOG(LogAsdaUpgradeStone, Log, TEXT("[камень улучшения] 6552 код %d (%s): записей %d"),
			Code, Code == 1 ? TEXT("перенос удался") : TEXT("не удался, камень пропал"), Applied);
		break;
	}
	default:
		UE_LOG(LogAsdaUpgradeStone, Warning, TEXT("[камень улучшения] 6552 код %d — текста в эталоне нет (оригинал молчит)"), Code);
		return;
	}
	if (Code != 1 && Code != 5) { UE_LOG(LogAsdaUpgradeStone, Warning, TEXT("[камень улучшения] 6552 отказ %d"), Code); }
}

void UAsdaUpgradeStoneSubsystem::MsgBox(const FString& Text) const
{
	// `CItem_BuildTooltipText(g_pItemTooltipUI, 0, −1, текст, 1)` = ONEBUT 1500 (очередь окон-сообщений).
	if (ShownNotices.Num() >= 16) { ShownNotices.RemoveAt(0); }
	ShownNotices.Add(Text);
	AsdaNotice::MsgBox(GetGameInstance(), EAsdaMsgCategory::ItemRefused, Text);
}

void UAsdaUpgradeStoneSubsystem::SetPickCursor(bool bOn)
{
	bCursorOwned = bOn;
	UGameInstance* GI = GetGameInstance();
	UWorld* W = GI ? GI->GetWorld() : nullptr;
	AAsdaPlayerController* Pc = W ? Cast<AAsdaPlayerController>(W->GetFirstPlayerController()) : nullptr;
	if (!Pc) { return; }
	Pc->SetGameCursor(bOn ? AAsdaPlayerController::EAsdaCursor::Pick : AAsdaPlayerController::EAsdaCursor::Normal);
	Pc->bUiCursorOverride = bOn;   // пока мышь над сумкой, наведение на мир курсор не перебивает (Б1)
}

FString UAsdaUpgradeStoneSubsystem::FormatC(const FString& Fmt, const FString& S, int32 D)
{
	// Эталон: `sprintf(buf, fmt, …)` — первый `%s` ← имя, первый `%d` ← число, `%%` ← «%».
	FString Out;
	Out.Reserve(Fmt.Len() + S.Len() + 8);
	bool bS = false, bD = false;
	for (int32 i = 0; i < Fmt.Len(); ++i)
	{
		const TCHAR C = Fmt[i];
		if (C == TEXT('%') && i + 1 < Fmt.Len())
		{
			const TCHAR N = Fmt[i + 1];
			if (N == TEXT('%')) { Out.AppendChar(TEXT('%')); ++i; continue; }
			if (N == TEXT('s') && !bS) { Out += S; bS = true; ++i; continue; }
			if (N == TEXT('d') && !bD) { Out.AppendInt(D); bD = true; ++i; continue; }
		}
		Out.AppendChar(C);
	}
	return Out;
}

FString UAsdaUpgradeStoneSubsystem::DumpState() const
{
	const UAsdaInventorySubsystem* Inv = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
	const FAsdaItemInstance* Stone = (Inv && StoneCell >= 0) ? Inv->GetCell(1, StoneCell) : nullptr;
	const FAsdaItemInstance* Gear  = (Inv && GearCell >= 0) ? Inv->GetCell(1, GearCell) : nullptr;
	return FString::Printf(TEXT("камень улучшения: взвод=%s в полёте=%s камень ячейка %d код %d, цель ячейка %d код %d"),
		bPick ? TEXT("да") : TEXT("нет"), bPending ? TEXT("да") : TEXT("нет"),
		StoneCell, Stone ? Stone->ItemCode : 0, GearCell, Gear ? Gear->ItemCode : 0);
}

#if ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING
static FAutoConsoleCommandWithWorld GAsdaUpgradeStoneDump(
	TEXT("asda.UpgradeStone.Dump"),
	TEXT("Состояние переноса улучшения камнем: взвод, «в полёте», ячейки камня и цели."),
	FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* W)
	{
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		const UAsdaUpgradeStoneSubsystem* S = GI ? GI->GetSubsystem<UAsdaUpgradeStoneSubsystem>() : nullptr;
		UE_LOG(LogAsdaUpgradeStone, Display, TEXT("%s"), S ? *S->DumpState() : TEXT("подсистемы нет"));
	}));
#endif
