// Copyright (c) 2026 @sitxovski (Discord)
// КАМЕНЬ ДУШИ: привязка к слоту Дружбы 0 и банка опыта. Разбор потока — в шапке заголовка.
#include "UI/AsdaSoulStoneSubsystem.h"
#include "AsdaBytes.h"
#include "AsdaDevConsole.h"
#include "AsdaNetSubsystem.h"
#include "Blueprint/UserWidget.h"
#include "Components/TextBlock.h"
#include "Engine/DataTable.h"
#include "Engine/GameInstance.h"
#include "Items/AsdaItemDbRows.h"
#include "Items/AsdaItemTableSubsystem.h"
#include "Text/AsdaTextSubsystem.h"
#include "TimerManager.h"
#include "UI/AsdaEffectsSubsystem.h"
#include "UI/AsdaEnterGuard.h"
#include "UI/AsdaHudSubsystem.h"
#include "UI/AsdaInventorySubsystem.h"
#include "UI/AsdaJuiWindowHandle.h"
#include "UI/AsdaLocIds.h"
#include "UI/AsdaNotice.h"
#include "UI/AsdaNoticeColors.h"
#include "UI/AsdaSoulSlotSubsystem.h"
#include "UI/AsdaSoulmateSubsystem.h"
#include "UI/AsdaHotbarSubsystem.h"   // ★ 06.11.2026: AsdaSoulSlotTruthOn
#include "UI/AsdaSowelSubsystem.h"
#include "UI/AsdaUiHelpers.h"
#include "UI/AsdaWindowSubsystem.h"
#include "World/AsdaWorldSubsystem.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaSoulStone, Log, All);

// Мир годен для окон (не загрузка и не сцена входа). Определён в `AsdaSoulmatePopBar.cpp`.
bool AsdaSoulPopWorldReady(const UGameInstance* GI);

// ★ 06.11.2026 [ОРИГИНАЛ] Камень души: проверки двойного щелчка, ворота 5029, канал 5419…5425, окно 9125. 1 — как эталон, плюс
//   [ИСПРАВЛЕНИЕ]: с панели быстрого доступа камень идёт тем же путём (оригинал шлёт 5028 и теряет накопленный опыт). 0 — прежний порт.
static ASDA_DEV_CVAR(int32, CVarSoulStoneBind, TEXT("asda.Soul.StoneBind"), 1,
	TEXT("[ОРИГИНАЛ] Камень души и банка опыта: 1 = как эталон, 0 = прежний порт (камень сразу уходит в 5028)."),
	ECVF_Default);

// ★ 06.11.2026 [ИСПРАВЛЕНИЕ ДЕФЕКТА ОРИГИНАЛА]: эталон ставит +637 и +640 на слот 0 ЛЮБОГО персонажа, и Друг рядом с духом
//   не мог ответить на 5421 (`netSendItemMoveSecure` выходит при +637). 1 — только свой слот, 0 — как оригинал.
static ASDA_DEV_CVAR(int32, CVarSoulStoneSelfSlot, TEXT("asda.Soul.StoneSelfSlotOnly"), 1,
	TEXT("[ИСПРАВЛЕНИЕ] Флаги камня от слота 0: 1 = только свой слот, 0 = слот любого персонажа, как оригинал."),
	ECVF_Default);

bool AsdaSoulStoneOn() { return CVarSoulStoneBind.GetValueOnGameThread() != 0; }

namespace
{
	constexpr int32 kSpendContainer = 2;     // контейнер расходников, `g_InventoryArray2`
	constexpr int32 kSpendCells = 60;        // ячеек в скане эталона
	constexpr int32 kStoneSubType = 32;      // `itemSubType` камня
	constexpr int32 kBoundByte = 50;         // байт записи: 3 — камень активирован
	constexpr int32 kGhostZ = 40;            // слой окна 9125, как у панели Друга
	const TCHAR* const kGhostWbp = TEXT("/Game/AsdaAssets/UI/Player_UI/Soulmate/WBP_SOULMATE_GHOST_WIN.WBP_SOULMATE_GHOST_WIN_C");   // ассет окна
	const FName kGhostId(TEXT("SOULMATE_GHOST_WIN"));     // окно 9125
	const FName kPopId(TEXT("SOULMATE_POP_WIN"));         // панель Друга 6500
	const TCHAR* const kExpFx = TEXT("eff_1_char_99_expmovg-a_01");   // эффект подарка
	const TCHAR* const kExpSnd = TEXT("S_c_SmExpGift");               // звук подарка
}

void UAsdaSoulStoneSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	ResetBlock();
}

void UAsdaSoulStoneSubsystem::Deinitialize()
{
	if (UGameInstance* GI = GetGameInstance()) { GI->GetTimerManager().ClearTimer(GhostRetry); }
	Super::Deinitialize();
}

void UAsdaSoulStoneSubsystem::ResetSessionState(EAsdaSessionResetReason Reason)
{
	// Виджет окна уходит с уровнем, кэш указателей нельзя держать через переход.
	GhostWnd = nullptr; GhostTxt = nullptr; GhostName = nullptr; GhostTitle = nullptr; GhostIcon = nullptr;
	bGhostBound = false;
	PendingCell = -1;
	PendingCode = 0;
	if (AsdaIsFullSessionReset(Reason))
	{
		ResetBlock();
		bFlag640 = false;
		bGhostWanted = false;
		PercentText.Reset();
	}
}

void UAsdaSoulStoneSubsystem::Note(const FString& What)
{
	ShowLog.Add(What);
	UE_LOG(LogAsdaSoulStone, Log, TEXT("[камень души] %s"), *What);
}

void UAsdaSoulStoneSubsystem::ChatNotice(int32 LocId)
{
	const UGameInstance* GI = GetGameInstance();
	const FString Text = AsdaNotice::Loc(GI, LocId);
	if (Text.IsEmpty()) { AsdaNotice::Chat(GI, EAsdaMsgCategory::System, LocId); }
	else { AsdaNotice::ChatColored(GI, EAsdaMsgCategory::System, Text, AsdaNoticeColor::Gold, 10000.f); }
	Note(FString::Printf(TEXT("chat:%d"), LocId));
}

void UAsdaSoulStoneSubsystem::BoxNotice(int32 LocId, const FString& Text)
{
	const UGameInstance* GI = GetGameInstance();
	if (Text.IsEmpty()) { AsdaNotice::MsgBoxLoc(GI, EAsdaMsgCategory::System, LocId, AsdaLoc::kBtnOk, /*EtalonNoticeId*/-1); }
	else if (UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr) { WM->ShowGameMessage(Text, AsdaLoc::kBtnOk, EAsdaMsgCategory::System); }
	Note(FString::Printf(TEXT("box:%d"), LocId));
}

int32 UAsdaSoulStoneSubsystem::BondIndex() const
{
	const UGameInstance* GI = GetGameInstance();
	const UAsdaSoulmateSubsystem* Sm = GI ? GI->GetSubsystem<UAsdaSoulmateSubsystem>() : nullptr;
	return Sm ? Sm->GetSoulSlotIndex() : -1;
}

bool UAsdaSoulStoneSubsystem::PartnerOnline() const
{
	const UGameInstance* GI = GetGameInstance();
	const UAsdaSoulmateSubsystem* Sm = GI ? GI->GetSubsystem<UAsdaSoulmateSubsystem>() : nullptr;
	return Sm && Sm->HasSoulmatePair();
}

int32 UAsdaSoulStoneSubsystem::SelfSlot0State() const
{
	const UGameInstance* GI = GetGameInstance();
	const UAsdaEffectsSubsystem* Fx = GI ? GI->GetSubsystem<UAsdaEffectsSubsystem>() : nullptr;
	const UAsdaSoulSlotSubsystem* Slots = GI ? GI->GetSubsystem<UAsdaSoulSlotSubsystem>() : nullptr;
	return (Fx && Slots) ? Slots->GetSlotState(Fx->SelfUserIdx(), 0) : 3;
}

int32 UAsdaSoulStoneSubsystem::FindActivatedCell() const
{
	const UGameInstance* GI = GetGameInstance();
	const UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
	const UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	if (!Inv || !IT) { return -1; }
	for (int32 i = 0; i < kSpendCells; ++i)
	{
		const FAsdaItemInstance* It = Inv->GetCell(kSpendContainer, i);
		if (!It || It->IsEmpty() || It->Raw.Num() <= kBoundByte) { continue; }
		const FAsdaItemRecord* R = IT->GetItem(It->ItemCode);
		if (R && R->ItemSubType == kStoneSubType && It->Raw[kBoundByte] == 3) { return i; }
	}
	return -1;
}

bool UAsdaSoulStoneSubsystem::FindActivatedStone()
{
	const int32 Cell = FindActivatedCell();
	if (Cell < 0) { return false; }
	bBound = true;
	StoneCell = Cell;
	return true;
}

void UAsdaSoulStoneSubsystem::OnSlot0Started(bool bSelf)
{
	if (!AsdaSoulStoneOn()) { return; }
	if (!bSelf && CVarSoulStoneSelfSlot.GetValueOnGameThread() != 0) { return; }
	bBound = true;
	bFlag640 = true;
}

void UAsdaSoulStoneSubsystem::ResetBlock()
{
	bSendArmed = false;
	bBound = false;
	bAsked = false;
	StoneCell = -1;
	Requester = -1;
	FMemory::Memzero(ReqPos, sizeof(ReqPos));
}

void UAsdaSoulStoneSubsystem::ResetRequest()
{
	bSendArmed = false;
	bAsked = false;
	Requester = -1;
	FMemory::Memzero(ReqPos, sizeof(ReqPos));
}

bool UAsdaSoulStoneSubsystem::IsBagSlotActive() const
{
	return BondIndex() > 0 && !PartnerOnline() && bBound;
}

bool UAsdaSoulStoneSubsystem::CheckGuard()
{
	const int32 Bond = BondIndex();
	if (Bond > 0 && !PartnerOnline()) { return true; }
	if (Bond < 0) { ChatNotice(8152); }
	if (SelfSlot0State() != 2) { ChatNotice(8153); }
	if (FindActivatedCell() >= 0) { ChatNotice(8154); }
	return false;
}

void UAsdaSoulStoneSubsystem::TryUseStone(int32 Container, int32 Cell)
{
	if (FindActivatedCell() >= 0)
	{
		SendJarRequest();
		return;
	}
	if (!CheckGuard()) { return; }
	UGameInstance* GI = GetGameInstance();
	const UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	const FAsdaItemInstance* It = Inv ? Inv->GetCell(Container, Cell) : nullptr;
	if (!It || !WM) { return; }
	PendingCell = Cell;
	PendingCode = It->ItemCode;
	TWeakObjectPtr<UAsdaSoulStoneSubsystem> WeakThis(this);
	AsdaEnterGuard::NoEnter(WM)->ShowGameConfirm(AsdaNotice::Loc(GI, 8157), [WeakThis](bool bYes)
	{
		if (UAsdaSoulStoneSubsystem* S = WeakThis.Get()) { S->OnActivateConfirm(bYes); }
	}, 0.f, 2288, 2289, 8157, 1);
	Note(TEXT("ask:8157"));
}

void UAsdaSoulStoneSubsystem::OnActivateConfirm(bool bYes)
{
	const int32 Cell = PendingCell;
	const int32 Code = PendingCode;
	PendingCell = -1;
	PendingCode = 0;
	if (!bYes) { return; }
	if (SelfSlot0State() != 2 || !bBound) { ChatNotice(8153); return; }
	if (BondIndex() < 0) { ChatNotice(8152); return; }
	UGameInstance* GI = GetGameInstance();
	const UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	const FAsdaItemInstance* It = Inv ? Inv->GetCell(kSpendContainer, Cell) : nullptr;
	// [ИСПРАВЛЕНИЕ] Пока окно было открыто, предмет могли переложить: эталон отправил бы то, что лежит в ячейке теперь.
	if (!It || It->IsEmpty() || It->ItemCode != Code || !Net) { return; }
	Net->SendSpendItemUse5028(It->ContainerType, It->SlotIndex, /*Target*/ 0);
	Note(TEXT("send:5028"));
}

bool UAsdaSoulStoneSubsystem::SendJarRequest()
{
	FindActivatedStone();
	if (StoneCell <= -1 || !bBound || BondIndex() < 0) { return false; }
	// ★ 06.11.2026 [ИСПРАВЛЕНИЕ] (`asda.Soul.SlotTruth`): эталон (`net_sendEquipItemRequest@0xD22D50`) при Друге вне игры печатает 6035 «Без Друга ничего
	//   не выйдет», хотя Друг есть (слот ≥ 0 проверен выше). Отказ тот же, строка оригинала про офлайн — 8101 «Друга нет в игре».
	if (!PartnerOnline()) { ChatNotice(AsdaSoulSlotTruthOn() ? 8101 : 6035); return false; }
	UGameInstance* GI = GetGameInstance();
	const UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
	const UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	const FAsdaItemInstance* It = Inv ? Inv->GetCell(kSpendContainer, StoneCell) : nullptr;
	if (!It || !IT || !Net || !IT->GetItem(It->ItemCode) || It->Raw.Num() <= kBoundByte) { return false; }
	if (It->Raw[kBoundByte] != 3) { ChatNotice(6955); return false; }
	const TArray<uint8>& R = It->Raw;
	uint8 Body[11] = {};
	Body[0] = R[5]; Body[1] = R[6];     // запись +5, ячейка
	Body[2] = R[7]; Body[3] = R[8];     // запись +7
	Body[4] = R[4];                     // запись +4, контейнер
	FMemory::Memcpy(&Body[5], &R[9], 4);    // запись +9
	FMemory::Memcpy(&Body[9], &R[14], 2);   // запись +14
	LastBody = TArray<uint8>(Body, 11);
	Net->SendSoulJarRequest5419(Body);
	Note(TEXT("send:5419"));
	ChatNotice(8150);
	return true;
}

bool UAsdaSoulStoneSubsystem::SendPartnerAnswer(uint8 Mode)
{
	if (bBound || BondIndex() < 0) { return false; }
	bAsked = false;
	UGameInstance* GI = GetGameInstance();
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net) { return false; }
	LastBody.Reset();
	LastBody.Add(Mode);
	LastBody.Add((uint8)(Requester & 0xFF));
	LastBody.Add((uint8)((Requester >> 8) & 0xFF));
	LastBody.Append(ReqPos, 11);
	Net->SendSoulJarResponse5422(Mode, Requester, ReqPos);
	Note(FString::Printf(TEXT("send:5422:%d"), (int32)Mode));
	if (Mode == 2)
	{
		bBound = false;
		Requester = -1;
		FMemory::Memzero(ReqPos, sizeof(ReqPos));
	}
	return true;
}

void UAsdaSoulStoneSubsystem::OnPartnerAskConfirm(bool bYes)
{
	SendPartnerAnswer(bYes ? 1 : 2);
}

void UAsdaSoulStoneSubsystem::OnSendConfirmOk()
{
	if (!bSendArmed) { return; }
	UGameInstance* GI = GetGameInstance();
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	const UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
	if (!Net) { return; }
	Net->SendSoulJarForwardExp5423();
	Note(TEXT("send:5423"));
	bSendArmed = false;
	const FAsdaItemInstance* It = Inv ? Inv->GetCell(kSpendContainer, StoneCell) : nullptr;
	if (!It || It->IsEmpty()) { return; }
	Net->SendSpendItemUse5028(It->ContainerType, It->SlotIndex, /*Target*/ 0);
	Note(TEXT("send:5028"));
}

void UAsdaSoulStoneSubsystem::OnRecvStatus5420(const uint8* B, int32 Len)
{
	if (!AsdaSoulStoneOn() || Len < 14) { return; }
	const UGameInstance* GI = GetGameInstance();
	const UAsdaSoulmateSubsystem* Sm = GI ? GI->GetSubsystem<UAsdaSoulmateSubsystem>() : nullptr;
	switch (B[0])
	{
	case 0: ResetRequest(); BoxNotice(8132); break;
	case 1: ResetRequest(); BoxNotice(8133); break;
	case 2: ResetRequest(); BoxNotice(8134); break;
	case 3:
	{
		bAsked = false;
		if (!bBound || !Sm) { break; }
		const UAsdaWorldSubsystem* WS = GI->GetSubsystem<UAsdaWorldSubsystem>();
		if (!WS || !WS->GetPlayerActorByUserNum(Sm->GetRecPartnerUserNum())) { break; }
		bSendArmed = true;
		FMemory::Memcpy(ReqPos, B + 3, 11);
		const FString Text = AsdaNotice::LocSlot(GI, 20161, Sm->GetPartnerName()).Replace(TEXT("\\n"), TEXT("\n"));
		UAsdaWindowSubsystem* WM = GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>();
		if (!WM) { break; }
		TWeakObjectPtr<UAsdaSoulStoneSubsystem> WeakThis(this);
		WM->ShowGameMessage(Text, AsdaLoc::kBtnOk, EAsdaMsgCategory::System, [WeakThis]()
		{
			if (UAsdaSoulStoneSubsystem* S = WeakThis.Get()) { S->OnSendConfirmOk(); }
		});
		Note(TEXT("box:20161"));
		break;
	}
	case 4:
	{
		ResetRequest();
		// [ИСПРАВЛЕНИЕ] В строке стоит буквальное «(name)»: эталон подстановки не делает, игрок видел заготовку.
		const FString Name = Sm ? Sm->GetPartnerName() : FString();
		BoxNotice(8135, AsdaNotice::Loc(GI, 8135).Replace(TEXT("(name)"), *Name));
		break;
	}
	case 5: ResetRequest(); BoxNotice(8136); break;
	case 6: ResetRequest(); BoxNotice(8140); break;
	default: break;
	}
}

void UAsdaSoulStoneSubsystem::OnRecvPartnerAsk5421(const uint8* B, int32 Len)
{
	if (!AsdaSoulStoneOn() || Len < 13 || BondIndex() <= 0) { return; }
	UGameInstance* GI = GetGameInstance();
	const UAsdaSoulmateSubsystem* Sm = GI ? GI->GetSubsystem<UAsdaSoulmateSubsystem>() : nullptr;
	const UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	const int32 Num = AsdaBytes::RdI16(B);
	if (!Sm || !WS || !WM || Num != Sm->GetRecPartnerUserNum() || !WS->GetPlayerActorByUserNum(Num)) { return; }
	Requester = Num;
	FMemory::Memcpy(ReqPos, B + 2, 11);
	bAsked = true;
	const FString Text = AsdaNotice::LocSlot(GI, 20160, Sm->GetPartnerName()) + TEXT(" \n") + AsdaNotice::Loc(GI, 8131);
	TWeakObjectPtr<UAsdaSoulStoneSubsystem> WeakThis(this);
	AsdaEnterGuard::NoEnter(WM)->ShowGameConfirm(Text, [WeakThis](bool bYes)
	{
		if (UAsdaSoulStoneSubsystem* S = WeakThis.Get()) { S->OnPartnerAskConfirm(bYes); }
	}, 0.f, 2288, 2289, 20160, 1);
	Note(TEXT("ask:20160"));
}

void UAsdaSoulStoneSubsystem::OnRecvExpGift5424(const uint8* B, int32 Len)
{
	if (!AsdaSoulStoneOn() || Len < 8) { return; }
	int64 Gift = 0;
	FMemory::Memcpy(&Gift, B, 8);
	UGameInstance* GI = GetGameInstance();
	const UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	const UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	// Доля подарка от размаха уровня, как эталон: `statGetScaledValueByIndex(level)`. Полосу опыта не трогаем: следом сервер шлёт 4038 с итогом.
	double Pct = 0.0;
	const UDataTable* Dt = IT ? IT->GetDataTable(FName(TEXT("charexp"))) : nullptr;
	if (Dt && Net)
	{
		const int32 Lv = (int32)Net->SelfLevel;
		const FAsdaCharexpRow* Prev = Dt->FindRow<FAsdaCharexpRow>(FName(*FString::FromInt(FMath::Max(Lv - 1, 0))), TEXT("soul-stone"), false);
		const FAsdaCharexpRow* Cur = Dt->FindRow<FAsdaCharexpRow>(FName(*FString::FromInt(Lv)), TEXT("soul-stone"), false);
		const double Span = (Prev && Cur) ? (double)(UAsdaHudSubsystem::CharexpCumExp(*Cur) - UAsdaHudSubsystem::CharexpCumExp(*Prev)) : 0.0;
		if (Span > 0.0) { Pct = (double)Gift / Span * 100.0; }
	}
	if (UAsdaTextSubsystem* Txt = GI ? GI->GetSubsystem<UAsdaTextSubsystem>() : nullptr)
	{
		Txt->NoticeSetFloat(0, (float)Pct);
		GiftText = Txt->FormatNotice(20162);
		AsdaNotice::ChatColored(GI, EAsdaMsgCategory::System, GiftText, AsdaNoticeColor::Gold, 10000.f);
		Note(TEXT("chat:20162"));
	}
	ResetBlock();
	if (UAsdaEffectsSubsystem* Eff = GI ? GI->GetSubsystem<UAsdaEffectsSubsystem>() : nullptr)
	{
		Eff->SpawnItemUseFx(Eff->SelfUserIdx(), kExpFx, /*SoundId*/ -1, kExpSnd);
		Note(TEXT("fx"));
	}
}

void UAsdaSoulStoneSubsystem::OnRecvBar5425(const uint8* B, int32 Len)
{
	if (!AsdaSoulStoneOn() || Len < 22 || !bBound || FindActivatedCell() < 0) { return; }
	int64 Max = 0, Cur = 0;
	FMemory::Memcpy(&Max, B + 6, 8);
	FMemory::Memcpy(&Cur, B + 14, 8);
	// [ИСПРАВЛЕНИЕ] Эталон делит без проверки: при нулевом потолке в окне было бы «inf».
	const double Pct = Max > 0 ? (double)Cur / (double)Max * 100.0 : 0.0;
	SetGhostPercent(FString::Printf(TEXT("%.2f %%"), Pct));
}

void UAsdaSoulStoneSubsystem::EnsureGhost()
{
	UGameInstance* GI = GetGameInstance();
	if (!AsdaSoulPopWorldReady(GI)) { return; }
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM) { return; }
	FAsdaJuiWindowHandle(kGhostId, kGhostWbp, kGhostZ).EnsureLazy(*WM, GhostWnd, bGhostBound,
		ESlateVisibility::Collapsed,
		[]() {},
		[]() { UE_LOG(LogAsdaSoulStone, Error, TEXT("[камень души] WBP_SOULMATE_GHOST_WIN не загрузился")); },
		[this, GI](UAsdaWindowSubsystem&)
		{
			auto Txt = [this](const TCHAR* N) -> UTextBlock*
			{
				if (UTextBlock* T = Cast<UTextBlock>(GhostWnd->GetWidgetFromName(FName(N)))) { return T; }
				return Cast<UTextBlock>(GhostWnd->GetWidgetFromName(FName(FString(N) + TEXT("_Text"))));
			};
			GhostTxt = Txt(TEXT("SOULMATE_GHOST_TXT0"));
			GhostName = Txt(TEXT("SoulGhost_Name"));
			GhostTitle = Txt(TEXT("SoulGhost_Title"));
			GhostIcon = AsdaUi::FindNodeByName(GhostWnd, TEXT(""), TEXT("SoulGhost_Ani_Icon"));
			if (GhostTitle) { GhostTitle->SetText(FText::FromString(AsdaNotice::Loc(GI, 8172))); }
			bGhostBound = true;
		});
}

void UAsdaSoulStoneSubsystem::ShowGhostZero()
{
	UGameInstance* GI = GetGameInstance();
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM || WM->IsWindowVisible(kPopId)) { return; }
	PercentText = TEXT("0 %");
	bGhostWanted = true;
	ApplyGhost();
}

void UAsdaSoulStoneSubsystem::ApplyGhost()
{
	UGameInstance* GI = GetGameInstance();
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM) { return; }
	EnsureGhost();
	if (!GhostWnd || !WM->IsWindowLive(GhostWnd))
	{
		// Загрузка карты ещё идёт: окно поднимется, когда мир будет готов.
		if (GI && !GI->GetTimerManager().IsTimerActive(GhostRetry))
		{
			GI->GetTimerManager().SetTimer(GhostRetry, FTimerDelegate::CreateWeakLambda(this, [this]()
			{
				UGameInstance* G = GetGameInstance();
				if (!bGhostWanted) { if (G) { G->GetTimerManager().ClearTimer(GhostRetry); } return; }
				ApplyGhost();
			}), 1.f, true);
		}
		return;
	}
	if (GI) { GI->GetTimerManager().ClearTimer(GhostRetry); }
	if (GhostIcon) { GhostIcon->SetVisibility(ESlateVisibility::SelfHitTestInvisible); }
	if (GhostTxt) { GhostTxt->SetText(FText::FromString(PercentText)); }
	if (const UAsdaSoulmateSubsystem* Sm = GI->GetSubsystem<UAsdaSoulmateSubsystem>())
	{
		if (GhostName) { GhostName->SetText(FText::FromString(Sm->GetPartnerName())); }
	}
	WM->SetWindowVisible(kGhostId, true);
	Note(TEXT("ghost+"));
}

void UAsdaSoulStoneSubsystem::SetGhostPercent(const FString& Text)
{
	PercentText = Text;
	if (GhostTxt) { GhostTxt->SetText(FText::FromString(Text)); }
}

void UAsdaSoulStoneSubsystem::HideGhostWindow()
{
	bGhostWanted = false;
	UGameInstance* GI = GetGameInstance();
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM || !GhostWnd || !WM->IsWindowLive(GhostWnd) || !WM->IsWindowVisible(kGhostId)) { return; }
	WM->SetWindowVisible(kGhostId, false);
	Note(TEXT("ghost-"));
}

void UAsdaSoulStoneSubsystem::ClearGhost()
{
	if (GhostIcon) { GhostIcon->SetVisibility(ESlateVisibility::Collapsed); }
	if (GhostTxt) { GhostTxt->SetText(FText::GetEmpty()); }
	PercentText.Reset();
	HideGhostWindow();
}

bool UAsdaSoulStoneSubsystem::DebugGhostVisible() const
{
	const UGameInstance* GI = GetGameInstance();
	const UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	return WM && GhostWnd && WM->IsWindowVisible(kGhostId);
}

void UAsdaSoulStoneSubsystem::OnStoneActivated(int32 Cell)
{
	StoneCell = Cell;
	UGameInstance* GI = GetGameInstance();
	const UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
	// `uiSelectionHighlightShow@0xD26290`: эффект только на вкладке расходников.
	if (Inv && Inv->GetGridContainer() == kSpendContainer)
	{
		if (UAsdaSowelSubsystem* Sw = GI->GetSubsystem<UAsdaSowelSubsystem>()) { Sw->ShowSoulStoneEffect(Cell); }
	}
	ShowGhostZero();
}

void UAsdaSoulStoneSubsystem::OnWorldReady()
{
	if (!AsdaSoulStoneOn()) { return; }
	if (SelfSlot0State() == 2) { bBound = true; }
	else { ResetBlock(); }
	if (bBound)
	{
		const int32 Cell = FindActivatedCell();
		if (Cell >= 0) { StoneCell = Cell; }
	}
	// Окно 9125 эталона живёт через смену карты, у порта виджет ушёл с уровнем.
	if (bGhostWanted) { ApplyGhost(); }
}
