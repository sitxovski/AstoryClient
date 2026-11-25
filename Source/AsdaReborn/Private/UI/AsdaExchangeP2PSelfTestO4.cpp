// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ ОБМЕНА, ВОЛНА О4 (30.09.2026, `Reports/trade/wave_O4.md`): итог 5294 / 5295 и исправления по ревизии О3. [ПОРТ]
//
// ЧТО ПРОВЕРЯЕТ. Полный цикл обмена БОЕВЫМИ путями: заявка (меню 2004 → 5282), окно (5285), лоток (0xB/0xD → 5288, 5289), готовность
//   (2048 → 5291, 5292 партнёра), ОК (2011 → 5293), итог 5295 (тип 0) / 5294 (тип 1) — входящие через `UAsdaNetSubsystem::DebugFeedPacket`
//   → `HandlePacket` → очередь → `Tick` подсистемы; исходящие — боевые отправители под перехватом (`ArmSendCapture`, в сеть ничего не
//   уходит). Применение итога — по ячейкам инвентаря (общая точка `ApplyExternalRecord`/`ClearCell`), золото — поле денег и ячейка
//   20551, отложенное золото аукциона 9913, вес. Отказы итога 0…4 и «чужой» код, Д21 (откат готовности после отказа фиксации),
//   Д18 по эху 5289, откат только своего инкремента счётчика, сброс сессии/ESC/«закрыть всё» при ожидании итога. Фаза 2 — ПУТЁМ ИГРОКА:
//   настоящие клики Slate по живой геометрии (ячейка 2014 двойным нажатием, 2048, 2011) и контроль «второе нажатие после интервала
//   двойного щелчка — одиночный клик» (ревизия О3 МЕЛК-4). У каждой проверки — КОНТРОЛЬНАЯ ПАРА.
//
// Консоль: `asda.Trade.SelfTestExchangeO4 [delay=N] [quit]`. Только офлайн. Строки — «САМОТЕСТ-О4», итог — «САМОТЕСТ-О4 ИТОГ».
//   Свой userNum/уровень, инвентарь, золото, вес, отложенное золото, ключи, перехватчик и пик восстанавливаются в конце.
#include "CoreMinimal.h"
#include "AsdaTestPacketsP2P.h"   // ★ R5-c (12.11.2026): общая точка самотестов вместо своих копий помощников

#if !UE_BUILD_SHIPPING

#include "UI/AsdaExchangeP2PSubsystem.h"
#include "AsdaCVarSave.h"                   // ★ ИСПРАВЛЕНИЕ 11.10.2026 (В0, N1(д)/U8): ключи консоли самотеста — запомнить и вернуть (значение + SetBy)
#include "UI/AsdaTradeStateSubsystem.h"
#include "UI/AsdaHotbarSubsystem.h"
#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaInventorySubsystem.h"
#include "UI/AsdaExchangeSubsystem.h"
#include "UI/AsdaChatSubsystem.h"
#include "UI/AsdaPlayerMenuSubsystem.h"
#include "UI/AsdaNotice.h"
#include "UI/AsdaUiHelpers.h"
#include "World/AsdaWorldSubsystem.h"
#include "World/AsdaPlayerController.h"
#include "Login/AsdaLoginData.h"
#include "AsdaNetSubsystem.h"
#include "Test/AsdaSelfTestKit.h"   // ★ 11.10.2026 (В3 S1): общий каркас самотестов (аргументы, отказ, перехват, итог, выход)
#include "AsdaBytes.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/CheckBox.h"
#include "Components/EditableText.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include "Windows/MinWindows.h"             // `GetDoubleClickTime` — интервал двойного щелчка этой машины (шаг «после интервала»)
#include "Windows/HideWindowsPlatformTypes.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogAsdaP2PTestO4, Log, All);

namespace AsdaP2PTestO4Local
{
	// Разбор `delay=N` (секунды до запуска).
	// ★ R5-fix-c (13.11.2026): своя `ParseDelay` (дословная копия у двенадцати самотестов) убрана — разбор `delay=N` в общей точке
	//   `AsdaSelfTestKit::DelayArg` (`Test/AsdaSelfTestKit.h`), вызовы ниже идут в неё.
	// Интервал двойного щелчка Windows (с) — тот же, что у окна 2008 (`DoubleClickSec`).
	double DblSec()
	{
#if PLATFORM_WINDOWS
		return (double)::GetDoubleClickTime() / 1000.0;
#else
		return 0.5;
#endif
	}

	// Пойманный исходящий пакет: опкод и payload (без конверта).
	struct FCap { uint16 Op = 0; TArray<uint8> P; };

	// Тело 5283 (24 б): [0] код, [1..2] userNum инициатора, [3..22] имя cp1251, [23] тип.
	using AsdaTestPacketsP2P::Body5283;   // ★ R5-c (12.11.2026): общая точка (`AsdaTestPacketsP2P.h`) — копия была дословной.
	// Тело 5285 (50 б): [0] код, [1] тип, [2..5] windowIndex, [6..7]/[8..27] сторона 1, [28..29]/[30..49] сторона 2.
	using AsdaTestPacketsP2P::Body5285;   // ★ R5-c (12.11.2026): общая точка (`AsdaTestPacketsP2P.h`) — копия была дословной.
	// Тело 5289 (8 б): [0] код, [1] invType, [2..3] invSlot, [4..7] кол-во.
	using AsdaTestPacketsP2P::Body5289;   // ★ R5-c (12.11.2026): общая точка (`AsdaTestPacketsP2P.h`) — копия была дословной.
	// Тело 5292 (261 б): [0..3] windowIndex, [4] действие, [5] lock, 5 × 51 (пустая — код −1).
	using AsdaTestPacketsP2P::Body5292;   // ★ R5-c (12.11.2026): общая точка (`AsdaTestPacketsP2P.h`) — копия была дословной.
	// Запись 5295 (13 б): код, тип, слот, кол-во, «+14».
	struct FE13 { int32 Code = 0; int32 Type = 0; int32 Slot = 0; int32 Count = 0; int32 W14 = 0; };
	// Тело 5295 (70 б): [0] код, [1..4] вес, 5 × 13 с [5] (раскладка `recvDBCharNormalExchangeComplete`: `sub_4C28F0(…, 5295, 70, …)`).
	TArray<uint8> Body5295(int32 Code, int32 Weight, const TArray<FE13>& E)
	{
		TArray<uint8> B; B.SetNumZeroed(70);
		B[0] = (uint8)Code;
		AsdaBytes::WrI32(B.GetData() + 1, Weight);
		for (int32 i = 0; i < E.Num() && i < 5; ++i)
		{
			uint8* R = B.GetData() + 5 + 13 * i;
			AsdaBytes::WrI32(R + 0, E[i].Code); R[4] = (uint8)E[i].Type; AsdaBytes::WrI16(R + 5, E[i].Slot);
			AsdaBytes::WrI32(R + 7, E[i].Count); AsdaBytes::WrI16(R + 11, E[i].W14);
		}
		return B;
	}
	// Запись 5294 (54 б) — поля, которые читает клиент (network.c:14976-15000).
	struct FE54 { int32 Code = 0; int32 Type = 0; int32 Slot = 0; int32 Count = 0; int32 Dur = 0; int32 W14 = 0; int32 Opt0 = 0; uint8 Gem0 = 0; };
	// Тело 5294 (275 б): [0] код, [1..4] вес, 5 × 54 с [5].
	TArray<uint8> Body5294(int32 Code, int32 Weight, const TArray<FE54>& E)
	{
		TArray<uint8> B; B.SetNumZeroed(275);
		B[0] = (uint8)Code;
		AsdaBytes::WrI32(B.GetData() + 1, Weight);
		for (int32 i = 0; i < E.Num() && i < 5; ++i)
		{
			uint8* R = B.GetData() + 5 + 54 * i;
			AsdaBytes::WrI32(R + 0, E[i].Code); R[4] = (uint8)E[i].Type; AsdaBytes::WrI16(R + 5, E[i].Slot);
			R[7] = (uint8)E[i].Dur; AsdaBytes::WrI16(R + 8, E[i].W14);
			R[10] = 3; R[11] = 4; R[12] = 5; AsdaBytes::WrI16(R + 13, 7);          // +24 / +26 / +25 / +27
			AsdaBytes::WrI32(R + 21, E[i].Opt0);                                   // опция 0 → +29
			AsdaBytes::WrI32(R + 42, E[i].Count);                                  // кол-во → +9
			R[46] = E[i].Gem0;                                                     // самоцветы → +16
		}
		return B;
	}
	// Тело 9913 (48 б): золото @39 (`uiHandleItemSaleResult`, как в самотесте ворот).
	TArray<uint8> Body9913(int32 Gold) { TArray<uint8> B; B.SetNumZeroed(48); AsdaBytes::WrI32(B.GetData() + 39, Gold); return B; }
	// Ожидаемое тело 5288 (12 б).
	using AsdaTestPacketsP2P::Want5288;   // ★ R5-c (12.11.2026): общая точка (`AsdaTestPacketsP2P.h`) — копия была дословной.
	// Ожидаемое тело 5293 (4 б).
	using AsdaTestPacketsP2P::Want5293;   // ★ R5-c (12.11.2026): общая точка (`AsdaTestPacketsP2P.h`) — копия была дословной.
}

struct FAsdaExchangeP2PSelfTestO4
{
	int32 Ok = 0;     // пройдено
	int32 Fail = 0;   // провалено
	TArray<AsdaP2PTestO4Local::FCap> Caps;          // пойманные исходящие
	TArray<TFunction<bool(UWorld*)>> Steps;         // шаги пути игрока: true — шаг окончен (false — повторить на следующем тике)
	double WaitFrom = 0.0;                          // метка времени шага «после интервала двойного щелчка»

	// Одна проверка: «САМОТЕСТ-О4 ok/ПРОВАЛ: <имя> — <подробности>».
	void Expect(bool bCond, const FString& Name, const FString& Detail)
	{
		if (bCond) { ++Ok; } else { ++Fail; }
		UE_LOG(LogAsdaP2PTestO4, Log, TEXT("САМОТЕСТ-О4 %s: %s — %s"), bCond ? TEXT("ok") : TEXT("ПРОВАЛ"), *Name, *Detail);
	}
	// Сколько пойманных пакетов с опкодом.
	int32 Count(uint16 Op) const { int32 N = 0; for (const auto& C : Caps) { if (C.Op == Op) { ++N; } } return N; }
	// Последний пойманный пакет с опкодом (nullptr — нет).
	const AsdaP2PTestO4Local::FCap* Last(uint16 Op) const { for (int32 i = Caps.Num() - 1; i >= 0; --i) { if (Caps[i].Op == Op) { return &Caps[i]; } } return nullptr; }
	// Hex payload для подробностей.
	static FString Hex(const AsdaP2PTestO4Local::FCap* C)
	{
		if (!C) { return TEXT("—"); }
		FString S; for (uint8 B : C->P) { S += FString::Printf(TEXT("%02X"), B); }
		return S;
	}
	// Payload побайтно равен ожидаемому.
	static bool Same(const AsdaP2PTestO4Local::FCap* C, const TArray<uint8>& Want) { return C && C->P == Want; }

	// Состояние, которое надо вернуть в конце.
	uint16 SelfNumWas = 0; bool bSelfKnownWas = false; uint8 LevelWas = 0; uint32 Gen = 0;
	int32 PickWas = 0; int32 GoldWas = 0; int32 WeightCurWas = -1; int32 WeightMaxWas = -1;
	TArray<FAsdaItemInstance> Cont1Was, Cont2Was;   // инвентарь до теста
	static constexpr int32 kPeer = 902;             // сосед-партнёр (фейковый)

	// Перехват всего исходящего (офлайн сокета нет).
	void Arm(UAsdaNetSubsystem* Net)
	{
		Gen = AsdaSelfTestKit::ArmCapture(Net, [this](uint16 Op, const uint8* P, int32 Len, bool, TCHAR)
		{
			AsdaP2PTestO4Local::FCap& C = Caps.AddDefaulted_GetRef();
			C.Op = Op;
			C.P.Append(P, Len);
		}, 0, TEXT("asda.Trade.SelfTestExchangeO4"));
	}
	// Снять перехватчик.
	void Disarm(UAsdaNetSubsystem* Net) { if (Net) { AsdaSelfTestKit::DisarmCapture(Net, Gen); } }

	// Контекст подсистем.
	struct FCtx
	{
		UGameInstance* GI = nullptr; UAsdaExchangeP2PSubsystem* X = nullptr; UAsdaNetSubsystem* Net = nullptr; UAsdaWindowSubsystem* WM = nullptr;
		UAsdaHotbarSubsystem* Hb = nullptr; UAsdaWorldSubsystem* WS = nullptr; UAsdaInventorySubsystem* Inv = nullptr; UAsdaChatSubsystem* Chat = nullptr;
		UAsdaPlayerMenuSubsystem* PM = nullptr; UAsdaExchangeSubsystem* Au = nullptr;
		bool Valid() const { return GI && X && Net && WM && Hb && WS && Inv && Chat && PM && Au; }
	};
	static FCtx Ctx(UWorld* W)
	{
		FCtx C;
		C.GI = W ? W->GetGameInstance() : nullptr;
		if (!C.GI) { return C; }
		C.X = C.GI->GetSubsystem<UAsdaExchangeP2PSubsystem>(); C.Net = C.GI->GetSubsystem<UAsdaNetSubsystem>();
		C.WM = C.GI->GetSubsystem<UAsdaWindowSubsystem>(); C.Hb = C.GI->GetSubsystem<UAsdaHotbarSubsystem>();
		C.WS = C.GI->GetSubsystem<UAsdaWorldSubsystem>(); C.Inv = C.GI->GetSubsystem<UAsdaInventorySubsystem>();
		C.Chat = C.GI->GetSubsystem<UAsdaChatSubsystem>(); C.PM = C.GI->GetSubsystem<UAsdaPlayerMenuSubsystem>();
		C.Au = C.GI->GetSubsystem<UAsdaExchangeSubsystem>();
		return C;
	}
	// Эталонный инвентарь теста: контейнер 2 — стопка 20571×5 (0), 20572×1 (1), золото 1000 (2), 20626×1 (3), стопки 4…29 (кол-во
	//   слот+2); контейнер 1 — 20626 (0), 20627 (1). Деньги 1000, отложенного золота нет.
	static void SetupInv(const FCtx& C)
	{
		for (int32 i = 0; i < 60; ++i) { C.Inv->DebugPutItem(2, i, 0, 0); }
		for (int32 i = 0; i < UAsdaInventorySubsystem::kShellSlots; ++i) { C.Inv->DebugPutItem(1, i, 0, 0); }   // ★ 29.10.2026 (инвентарь ×2): вся сумка 60
		C.Inv->DebugPutItem(2, 0, 20571, 5);
		C.Inv->DebugPutItem(2, 1, 20572, 1);
		C.Inv->DebugPutItem(2, 2, 20551, 1000);
		C.Inv->DebugPutItem(2, 3, 20626, 1);
		static const int32 kCodes[] = { 20573, 20577, 20578, 20579, 20583, 20586, 20587, 20588, 20589, 20590 };
		for (int32 i = 4; i < 30; ++i) { C.Inv->DebugPutItem(2, i, kCodes[(i - 4) % UE_ARRAY_COUNT(kCodes)], i + 2); }
		C.Inv->DebugPutItem(1, 0, 20626, 1);
		C.Inv->DebugPutItem(1, 1, 20627, 1);
		C.Inv->SetGoldDisplay(1000);
		C.Au->ResetDeferredSaleGold(TEXT("самотест О4"));
	}
	// Сброс между блоками: модель, очередь окон-сообщений, нумпад, пойманное, инвентарь.
	void Clean(const FCtx& C)
	{
		C.X->ResetSessionState(EAsdaSessionResetReason::LeftWorld);
		C.WM->ClearMsgBoxQueue(TEXT("самотест О4"));
		if (C.WM->IsNumPadOpen()) { C.WM->CloseNumPad(false); }
		SetupInv(C);
		Caps.Reset();
	}
	// Приём штатным путём: пакет → `HandlePacket` → очередь подсистемы → её `Tick`.
	static void Feed(const FCtx& C, int32 Op, const TArray<uint8>& B) { C.Net->DebugFeedPacket(Op, B); C.X->Tick(0.f); }
	// Открыть окно как ЦЕЛЬ (заявка → значок → «Принять» → 5285/1).
	static void OpenAsTarget(const FCtx& C, int32 Type, int32 Idx)
	{
		using namespace AsdaP2PTestO4Local;
		Feed(C, 5283, Body5283(1, kPeer, TEXT("Партнёр"), Type));
		C.Hb->HandleNoticeIconClick(Type ? 5 : 4);
		C.WM->AnswerMsgBox(true, true);
		Feed(C, 5285, Body5285(1, Type, Idx, kPeer, TEXT("Партнёр"), 500, TEXT("Я")));
	}
	// Положить кандидата-одиночку (ячейка `IdNum`) двойным щелчком модели и подтвердить 5289/1 с эхом.
	static void PutSingle(const FCtx& C, int32 IdNum, int32 InvType, int32 InvSlot)
	{
		using namespace AsdaP2PTestO4Local;
		C.X->OnCellPressed(IdNum); C.X->OnCellDoubleClick(IdNum);
		Feed(C, 5289, Body5289(1, InvType, InvSlot, 1));
	}
	// Положить часть стопки: двойной щелчок → нумпад режим 3 → число → ОК; ответ 5289/1 с эхом.
	static void PutQty(const FCtx& C, int32 IdNum, int32 InvType, int32 InvSlot, int32 Qty)
	{
		using namespace AsdaP2PTestO4Local;
		C.X->OnCellPressed(IdNum); C.X->OnCellDoubleClick(IdNum);
		TypeNumPad(C.WM, FString::FromInt(Qty)); C.WM->CloseNumPad(true);
		Feed(C, 5289, Body5289(1, InvType, InvSlot, Qty));
	}
	// Клик по флажку 2048 так, как его доставляет UE.
	static void ToggleMine(const FCtx& C, bool bChecked)
	{
		if (UCheckBox* Mine = Cast<UCheckBox>(C.X->DebugNamedWidget(TEXT("TRAN_BUT01")))) { Mine->SetIsChecked(bChecked); }
		C.X->OnMyLockChecked(bChecked);
	}
	// Клик по кнопке 2011 её `OnClicked`.
	static void ClickOk(const FCtx& C)
	{
		if (UButton* B = Cast<UButton>(C.X->DebugNamedWidget(TEXT("TRAN_OKBUT")))) { B->OnClicked.Broadcast(); }
	}
	// Ввести число в поле нумпада 1412 (`NUMPAD_NUMBOX`) — путём окна.
	static bool TypeNumPad(UAsdaWindowSubsystem* WM, const FString& Txt)
	{
		UUserWidget* NW = WM ? WM->FindWindow(FName(TEXT("NUMPAD_WIN"))) : nullptr;
		if (!NW || !NW->WidgetTree) { return false; }
		TArray<UWidget*> All; NW->WidgetTree->GetAllWidgets(All);
		for (UWidget* X : All)
		{
			if (UEditableText* E = Cast<UEditableText>(X)) { if (X->GetName().Contains(TEXT("NUMPAD_NUMBOX"))) { E->SetText(FText::FromString(Txt)); return true; } }
		}
		return false;
	}
	// Готовность обеих сторон и ОК: мой 2048 → 5291, 5292 партнёра lock 1, 2011 → 5293.
	static void ReadyAndOk(const FCtx& C)
	{
		using namespace AsdaP2PTestO4Local;
		ToggleMine(C, true);
		Feed(C, 5292, Body5292(1, 1, 20573, 1));
		ClickOk(C);
	}
	// Ячейка инвентаря: код и кол-во.
	static int32 CellCode(const FCtx& C, int32 Cont, int32 Slot) { const FAsdaItemInstance* I = C.Inv->GetCell(Cont, Slot); return I ? I->ItemCode : -1; }
	static int32 CellAmt(const FCtx& C, int32 Cont, int32 Slot) { const FAsdaItemInstance* I = C.Inv->GetCell(Cont, Slot); return I ? I->Amount : -1; }

	// Фаза 1: логика (синхронно). Возврат — false, если нечего проверять.
	bool RunPhase1(UWorld* W)
	{
		using namespace AsdaP2PTestO4Local;
		const FCtx C = Ctx(W);
		if (!C.Valid())
		{
			UE_LOG(LogAsdaP2PTestO4, Error, TEXT("САМОТЕСТ-О4 ИТОГ: ok=0 fail=1 (нет подсистем)"));
			return false;
		}
		UAsdaExchangeP2PSubsystem* X = C.X;
		UAsdaNetSubsystem* Net = C.Net;
		UAsdaWindowSubsystem* WM = C.WM;
		UAsdaInventorySubsystem* Inv = C.Inv;
		UGameInstance* GI = C.GI;
		// ── подготовка: свой userNum 500, уровень 30, сосед 902, инвентарь, золото, вес, ключи, перехват ──
		SelfNumWas = Net->SelfUserNum; bSelfKnownWas = Net->bSelfUserNumKnown; LevelWas = Net->SelfLevel;
		WeightCurWas = Net->SelfWeightCur; WeightMaxWas = Net->SelfWeightMax;
		Net->SelfUserNum = 500; Net->bSelfUserNumKnown = true; Net->SelfLevel = 30;
		C.WS->DebugSpawnFakePeer(kPeer, TEXT("Партнёр"), FVector2D(25710.f, 25700.f));
		IConsoleVariable* CvFix = IConsoleManager::Get().FindConsoleVariable(TEXT("asda.Trade.P2PFixDefects"));
		// ★ ИСПРАВЛЕНИЕ 11.10.2026 (В0, N1(д)/U8): исходное P2PFixDefects (значение + SetBy) запоминает `AsdaCVarSave` (прежде — поле FixWas).
		auto SetFix = [CvFix](int32 V) { if (CvFix) { AsdaCVarSave::SetForTest(TEXT("asda.Trade.P2PFixDefects"), V); } };
		SetFix(1);
		PickWas = C.PM->GetPickedUserIdx();
		GoldWas = Inv->GetGold();
		for (int32 i = 0; i < 60; ++i) { if (const FAsdaItemInstance* Cl = Inv->GetCell(2, i)) { Cont2Was.Add(*Cl); } }
		for (int32 i = 0; i < UAsdaInventorySubsystem::kShellSlots; ++i) { if (const FAsdaItemInstance* Cl = Inv->GetCell(1, i)) { Cont1Was.Add(*Cl); } }   // ★ 29.10.2026 (инвентарь ×2): вся сумка 60
		Arm(Net);
		auto Loc = [GI](int32 Id) { return AsdaNotice::Loc(GI, Id); };
		auto TrayCode = [X](int32 K) { const uint8* R = X->TrayRec(K); return R ? AsdaBytes::RdI32(R) : 0; };
		auto ListCode = [X](int32 Type, int32 Idx) { const uint8* R = X->ListRec(Type, Idx); return R ? AsdaBytes::RdI32(R) : 0; };
		auto ListCnt = [X](int32 Type, int32 Idx) { const uint8* R = X->ListRec(Type, Idx); return R ? AsdaBytes::RdI32(R + 9) : 0; };
		auto ChatColorIs = [&](int32 LocId, const FColor& Want) { FColor Col; return C.Chat->DebugFindLastSystemLineColor(Loc(LocId).Left(12), Col) && Col == Want; };
		auto WinOpen = [&]() { return X->IsWindowShown() && UAsdaTradeStateSubsystem::IsExchangeWindowOpen(GI); };
		const FColor kDef(0xFE, 0xD9, 0xF1, 0xFF);    // −75279
		const FColor kFull(0xFF, 0xAD, 0x00, 0xFF);   // −21248
		const FColor kRed(0xFF, 0x00, 0x00, 0xFF);    // −65536

		// ═══ A. Полный цикл типа 0 (заявка из меню → 5285 → лоток → готовность → ОК → 5295 код 5) ═══
		Clean(C);
		{
			const bool bSent = X->OnMenuCommand(false, kPeer, kPeer, TEXT("Партнёр"));
			const bool bReq = bSent && Count(5282) == 1 && X->IsRequestActive();
			Feed(C, 5285, Body5285(1, 0, 100, 500, TEXT("Я"), kPeer, TEXT("Партнёр")));
			const bool bWin = WinOpen() && X->GetWindowIndex() == 100;
			PutSingle(C, 2014, 2, 1);   // 20572 (к=2, слот 1)
			PutSingle(C, 2016, 2, 3);   // 20626 (к=2, слот 3)
			Expect(bReq && bWin && Count(5288) == 2 && Same(Last(5288), Want5288(100, 1, 2, 3, 1)) && TrayCode(0) == 20572 && TrayCode(1) == 20626,
				TEXT("A1 заявка меню 2004 → 5282, заявка в полёте; 5285/1 → окно #100 открыто; две одиночки → 5288 ×2, лоток 20572 / 20626"),
				FString::Printf(TEXT("5282×%d заявка=%d окно=%d 5288×%d тело %s лоток %d/%d"), Count(5282), bReq ? 1 : 0, bWin ? 1 : 0, Count(5288), *Hex(Last(5288)),
					TrayCode(0), TrayCode(1)));
			ReadyAndOk(C);
			const bool bInvSame = CellCode(C, 2, 1) == 20572 && CellCode(C, 2, 3) == 20626 && CellAmt(C, 2, 4) == 6;
			Expect(Count(5291) == 1 && Count(5293) == 1 && Same(Last(5293), Want5293(100)) && X->bCompleteSent == 1 && bInvSame,
				TEXT("A2 2048 → 5291, партнёр готов, 2011 → 5293 [#100], +0x28 = 1; инвентарь до итога НЕ меняется (20572/20626 на месте)"),
				FString::Printf(TEXT("5291×%d 5293×%d +0x28=%d инвентарь цел=%d"), Count(5291), Count(5293), X->bCompleteSent, bInvSame ? 1 : 0));
			// Итог: получено 20573×3 в ячейку 4 (там стопка 6 → 9) и новое 20580×2 в ячейку 40; пустые записи (0,0,0) и (0,0,−1).
			const TArray<uint8> B = Body5295(5, 4321, { {20573, 2, 4, 3, 12}, {20580, 2, 40, 2, 4}, {0, 0, 0, 0, 0}, {0, 0, -1, 0, 0}, {0, 0, 0, 0, 0} });
			const int32 CapsBefore = Caps.Num();
			const int32 Seq0 = X->GetNoticeSeq();
			Net->DebugFeedPacket(5295, B);   // колбэк сети: только очередь
			const bool bQueued = WinOpen() && CellCode(C, 2, 1) == 20572 && X->GetNoticeSeq() == Seq0;
			X->Tick(0.f);                    // разбор — в `Tick`
			const bool bApplied = CellCode(C, 2, 1) == 0 && CellCode(C, 2, 3) == 0 && CellCode(C, 2, 4) == 20573 && CellAmt(C, 2, 4) == 9
				&& CellCode(C, 2, 40) == 20580 && CellAmt(C, 2, 40) == 2 && CellCode(C, 2, 0) == 20571 && CellAmt(C, 2, 0) == 5
				&& Inv->GetGold() == 1000 && CellAmt(C, 2, 2) == 1000;
			Expect(bQueued && bApplied, TEXT("A3 5295 код 5: в колбэке — только очередь (окно открыто, инвентарь цел — контроль); в Tick — отданные 1:1 и 2:3 сняты, 20573 в ячейке 4: стопка 6 → 9, 20580×2 в 40, 20571×5 и золото 1000 не тронуты"),
				FString::Printf(TEXT("очередь=%d 2:1=%d 2:3=%d 2:4=%d×%d 2:40=%d×%d 2:0=%d×%d золото=%d"), bQueued ? 1 : 0, CellCode(C, 2, 1), CellCode(C, 2, 3),
					CellCode(C, 2, 4), CellAmt(C, 2, 4), CellCode(C, 2, 40), CellAmt(C, 2, 40), CellCode(C, 2, 0), CellAmt(C, 2, 0), Inv->GetGold()));
			Expect(!WinOpen() && X->ListCount == -1 && X->bCompleteSent == 0 && X->GetLastNoticeLoc() == 5030 && ChatColorIs(5030, kDef)
				&& Net->SelfWeightCur == 4321 && Caps.Num() == CapsBefore && CellCode(C, 2, 5) == 20577,
				TEXT("A4 итог: окно закрыто (+0x2C = 0), модель сброшена, +0x28 = 0, чат 5030 цветом −75279, вес 4321, пакетов в ответ нет; пустые записи ничего не тронули (2:5 цела)"),
				FString::Printf(TEXT("окно=%d кандидатов %d +0x28=%d нотис %d вес %d пакетов +%d 2:5=%d"), WinOpen() ? 1 : 0, X->ListCount, X->bCompleteSent,
					X->GetLastNoticeLoc(), Net->SelfWeightCur, Caps.Num() - CapsBefore, CellCode(C, 2, 5)));
		}
		// ═══ B. Тип 0: часть стопки остаётся; золото — отложенная сумма аукциона 9913 (пара: без отложенного — полученное золото) ═══
		Clean(C);
		{
			OpenAsTarget(C, 0, 101);
			PutQty(C, 2013, 2, 0, 2);   // 2 из 20571×5
			const bool bList = ListCnt(0, 0) == 3 && CellAmt(C, 2, 0) == 5;
			Net->DebugFeedPacket(9913, Body9913(5000));   // лот продан при открытом окне → отложено
			const bool bDeferred = C.Au->GetDeferredSaleGold() == 5000 && Inv->GetGold() == 1000;
			ReadyAndOk(C);
			Feed(C, 5295, Body5295(5, 100, {}));
			Expect(bList && bDeferred && CellCode(C, 2, 0) == 20571 && CellAmt(C, 2, 0) == 3 && Inv->GetGold() == 5000 && CellAmt(C, 2, 2) == 5000
				&& C.Au->GetDeferredSaleGold() == 0,
				TEXT("B1 отдано 2 из 5: в инвентаре остаток 3 (не снят); 9913 при открытом окне отложил 5000 (деньги 1000) → по итогу деньги и ячейка 20551 = 5000, отложенное 0"),
				FString::Printf(TEXT("список=%d отложено=%d 2:0=%d×%d деньги=%d ячейка=%d отложено после=%d"), bList ? 1 : 0, bDeferred ? 1 : 0, CellCode(C, 2, 0),
					CellAmt(C, 2, 0), Inv->GetGold(), CellAmt(C, 2, 2), C.Au->GetDeferredSaleGold()));
			Clean(C);
			OpenAsTarget(C, 0, 102);
			PutSingle(C, 2014, 2, 1);
			ReadyAndOk(C);
			Feed(C, 5295, Body5295(5, 100, { {20551, 2, 2, 250, 0} }));
			Expect(Inv->GetGold() == 1250 && CellAmt(C, 2, 2) == 1250 && CellCode(C, 2, 1) == 0,
				TEXT("B2 контроль: без отложенного получено золото 250 → стопка 20551 списка (1000) + 250 = 1250 (деньги и ячейка)"),
				FString::Printf(TEXT("деньги=%d ячейка=%d 2:1=%d"), Inv->GetGold(), CellAmt(C, 2, 2), CellCode(C, 2, 1)));
			// ★ Ревизия О4 (МЕЛК-5): немедленная ветка 9913 (окно обмена закрыто) — `uiSetHonorPointsDisplay(v)` + `sub_92104D(win, 0)`
			//   (ui_core.c:50742-50746): деньги и ячейка 20551 = X, отложенного нет. Контроль — при открытом окне деньги не меняются.
			Clean(C);
			Net->DebugFeedPacket(9913, Body9913(8888));
			const bool bNow = !WinOpen() && Inv->GetGold() == 8888 && CellAmt(C, 2, 2) == 8888 && C.Au->GetDeferredSaleGold() == 0;
			const int32 NowGold = Inv->GetGold(), NowCell = CellAmt(C, 2, 2);
			Clean(C);
			OpenAsTarget(C, 0, 104);
			Net->DebugFeedPacket(9913, Body9913(9999));
			const bool bHold = WinOpen() && Inv->GetGold() == 1000 && CellAmt(C, 2, 2) == 1000 && C.Au->GetDeferredSaleGold() == 9999;
			Expect(bNow && bHold,
				TEXT("B3 9913 при закрытом окне обмена → деньги и ячейка 20551 = 8888 сразу, отложенного нет; контроль: при открытом окне деньги 1000, отложено 9999"),
				FString::Printf(TEXT("закрыто: деньги=%d ячейка=%d; открыто: деньги=%d ячейка=%d отложено=%d"), NowGold, NowCell, Inv->GetGold(),
					CellAmt(C, 2, 2), C.Au->GetDeferredSaleGold()));
		}
		// ═══ C. Отказы итога: 1/2/3/4 — нотис, окно и готовность остаются, +0x28 = 0 (2011 снова шлёт 5293); 7 — ничего; 0 — закрытие + 9571 ═══
		Clean(C);
		{
			OpenAsTarget(C, 0, 103);
			PutSingle(C, 2014, 2, 1);
			ReadyAndOk(C);
			ClickOk(C);   // второй клик при +0x28 = 1 — 5293 не повторяется (контроль к «после отказа — снова»)
			const bool bOnce = Count(5293) == 1;
			static const int32 kLoc[5] = { 0, 5026, 1012, 5028, 5029 };
			bool bAll = true; FString Det;
			for (int32 Code = 1; Code <= 4; ++Code)
			{
				Feed(C, 5295, Body5295(Code, 0, {}));
				const bool bCol = ChatColorIs(kLoc[Code], Code == 2 ? kFull : kDef);
				const bool bLoc = X->GetLastNoticeLoc() == kLoc[Code];
				const bool bKeep = WinOpen() && X->bMyLock == 1 && X->ReadyCounter == 2 && X->bCompleteSent == 0 && CellCode(C, 2, 1) == 20572;
				const int32 N0 = Count(5293);
				ClickOk(C);
				const bool bAgain = Count(5293) == N0 + 1;
				bAll &= bLoc && bCol && bKeep && bAgain;
				Det += FString::Printf(TEXT("[%d: loc=%d цвет=%d держится=%d снова5293=%d] "), Code, bLoc ? 1 : 0, bCol ? 1 : 0, bKeep ? 1 : 0, bAgain ? 1 : 0);
			}
			Expect(bOnce && bAll, TEXT("C1 5295 коды 1/2/3/4 → 5026 / 1012 (−21248) / 5028 / 5029; окно, готовность и инвентарь держатся; +0x28 = 0 — 2011 снова шлёт 5293 (до отказа второй клик не слал — контроль)"),
				FString::Printf(TEXT("один=%d %s"), bOnce ? 1 : 0, *Det));
			const int32 Seq0 = X->GetNoticeSeq();
			Feed(C, 5295, Body5295(7, 0, {}));
			Expect(X->GetNoticeSeq() == Seq0 && WinOpen() && X->bCompleteSent == 0,
				TEXT("C2 код 7 — ветки нет: нотисов нет, окно открыто; +0x28 сброшен до switch (как эталон)"),
				FString::Printf(TEXT("нотисов +%d окно=%d +0x28=%d"), X->GetNoticeSeq() - Seq0, WinOpen() ? 1 : 0, X->bCompleteSent));
			Feed(C, 5294, Body5294(3, 0, {}));
			const bool b94 = X->GetLastNoticeLoc() == 5028 && WinOpen();
			WM->ClearMsgBoxQueue(TEXT("самотест О4"));
			Feed(C, 5295, Body5295(0, 0, {}));
			const bool bRed = ChatColorIs(9571, kRed) && X->GetLastNoticeLoc() == 9571;
			const bool bBox = WM->GetMsgBoxQueueNum() > 0 && WM->GetMsgBoxQueueTopText().Contains(Loc(9571).Left(10));
			Expect(b94 && bRed && bBox && ChatColorIs(5025, kDef) && !WinOpen() && X->ListCount == -1 && CellCode(C, 2, 1) == 20572,
				TEXT("C3 5294 код 3 → 5028 (тот же обработчик); 5295 код 0 → 5025, окно закрыто, 9571 красным (чат + окно-сообщение), инвентарь не тронут"),
				FString::Printf(TEXT("5294=%d 9571 красн=%d окно-сообщение=%d «%s» окно=%d 2:1=%d"), b94 ? 1 : 0, bRed ? 1 : 0, bBox ? 1 : 0,
					*WM->GetMsgBoxQueueTopText().Left(30), WinOpen() ? 1 : 0, CellCode(C, 2, 1)));
			WM->ClearMsgBoxQueue(TEXT("самотест О4"));
		}
		// ═══ D. Тип 1 (5294): вещь сумки + золото из лотка; полученная вещь — полная запись в сумку; отложенное; полученное золото ═══
		Clean(C);
		{
			OpenAsTarget(C, 1, 110);
			PutSingle(C, 2013, 1, 0);          // 20626 (к=1, слот 0)
			PutQty(C, 2015, 2, 2, 300);        // золото 300
			const bool bTray = TrayCode(0) == 20626 && TrayCode(1) == 20551;
			ReadyAndOk(C);
			FE54 E; E.Code = 20628; E.Type = 1; E.Slot = 5; E.Count = 1; E.Dur = 77; E.W14 = 12; E.Opt0 = 555; E.Gem0 = 9;
			Feed(C, 5294, Body5294(5, 777, { E }));
			const FAsdaItemInstance* R = Inv->GetCell(1, 5);
			const bool bRec = R && R->ItemCode == 20628 && R->Raw.Num() >= 57 && R->Raw[13] == 77 && AsdaBytes::RdI16(R->Raw.GetData() + 14) == 12
				&& R->Raw[24] == 3 && R->Raw[25] == 5 && R->Raw[26] == 4 && AsdaBytes::RdI16(R->Raw.GetData() + 27) == 7
				&& AsdaBytes::RdI32(R->Raw.GetData() + 29) == 555 && R->Raw[16] == 9 && R->Amount == 1;
			Expect(bTray && bRec && CellCode(C, 1, 0) == 0 && Inv->GetGold() == 700 && CellAmt(C, 2, 2) == 700 && Net->SelfWeightCur == 777
				&& !WinOpen() && X->GetLastNoticeLoc() == 5030,
				TEXT("D1 5294 код 5: отдана 20626 (1:0 снята) и 300 золота (деньги и ячейка 20551 = 700); получена 20628 в 1:5 — прочность 77, +14, +24/+25/+26, +27, опция, самоцвет; вес 777, окно закрыто, 5030"),
				FString::Printf(TEXT("лоток=%d запись=%d 1:0=%d деньги=%d ячейка=%d вес=%d окно=%d нотис %d"), bTray ? 1 : 0, bRec ? 1 : 0, CellCode(C, 1, 0),
					Inv->GetGold(), CellAmt(C, 2, 2), Net->SelfWeightCur, WinOpen() ? 1 : 0, X->GetLastNoticeLoc()));
			Clean(C);
			OpenAsTarget(C, 1, 111);
			PutQty(C, 2015, 2, 2, 300);
			Net->DebugFeedPacket(9913, Body9913(5000));
			ReadyAndOk(C);
			Feed(C, 5294, Body5294(5, 0, {}));
			const bool bDef = Inv->GetGold() == 4700 && CellAmt(C, 2, 2) == 4700 && C.Au->GetDeferredSaleGold() == 0;
			Clean(C);
			OpenAsTarget(C, 1, 112);
			PutSingle(C, 2014, 1, 1);          // 20627 (к=1, слот 1)
			ReadyAndOk(C);
			FE54 G; G.Code = 20551; G.Type = 2; G.Slot = 2; G.Count = 1500;
			Feed(C, 5294, Body5294(5, 0, { G }));
			Expect(bDef && Inv->GetGold() == 1500 && CellAmt(C, 2, 2) == 1500 && CellCode(C, 1, 1) == 0,
				TEXT("D2 отдано 300 при отложенных 5000 → 4700 (отложенное обнулено; контроль D1 без отложенного — 700); D3 получено золото — сервер шлёт итог 1500: деньги и ячейка 1500"),
				FString::Printf(TEXT("отложенное=%d деньги=%d ячейка=%d 1:1=%d"), bDef ? 1 : 0, Inv->GetGold(), CellAmt(C, 2, 2), CellCode(C, 1, 1)));
		}
		// ═══ E. Д21 (ревизия О3 СРЕДН-1): отказ фиксации до 5293 → готовность назад, 2048 свободна, повторный 5291; после 5293 — нет;
		//     ключ 0 — как эталон; счётчик: откат снимает только свой инкремент (ревизия О3 МЕЛК-3) ═══
		Clean(C);
		{
			OpenAsTarget(C, 0, 120);
			PutSingle(C, 2014, 2, 1);
			ToggleMine(C, true);
			Feed(C, 5292, Body5292(2, 0));
			const bool bRolled = X->bMyLock == 0 && !X->bMyLockLatch && !X->DebugIsChecked(2048) && X->ReadyCounter == 0 && X->bLockOff == 1
				&& X->GetLastNoticeLoc() == 5073;
			ToggleMine(C, true);
			Expect(bRolled && Count(5291) == 2 && X->bMyLock == 1 && X->ReadyCounter == 1,
				TEXT("E1 ключ 1: 5291 → 5292 [4]=2 → 5073, готовность снята (латч, галка, счётчик 0, +0x0C = 1); повторный клик 2048 шлёт 5291 снова"),
				FString::Printf(TEXT("откат=%d 5291×%d +4C4=%d счётчик=%d"), bRolled ? 1 : 0, Count(5291), X->bMyLock, X->ReadyCounter));
			Clean(C);
			OpenAsTarget(C, 0, 121);
			PutSingle(C, 2014, 2, 1);
			Feed(C, 5292, Body5292(1, 1, 20573, 1));
			ToggleMine(C, true);
			const bool bTwo = X->ReadyCounter == 2 && X->DebugIsShown(TEXT("TRAN_OKBUT"));
			Feed(C, 5292, Body5292(0, 0));
			Expect(bTwo && X->ReadyCounter == 1 && !X->DebugIsShown(TEXT("TRAN_OKBUT")) && X->bMyLock == 0 && X->GetLastNoticeLoc() == 5071,
				TEXT("E2 партнёр готов, мой взвод → счётчик 2, 2011; 5292 [4]=0 → 5071, счётчик 1, 2011 скрыт, готовность снята"),
				FString::Printf(TEXT("до=%d счётчик=%d 2011=%d +4C4=%d"), bTwo ? 1 : 0, X->ReadyCounter, X->DebugIsShown(TEXT("TRAN_OKBUT")) ? 1 : 0, X->bMyLock));
			Clean(C);
			OpenAsTarget(C, 0, 122);
			PutSingle(C, 2014, 2, 1);
			ReadyAndOk(C);
			Feed(C, 5292, Body5292(0, 0));
			Expect(X->bCompleteSent == 1 && X->bMyLock == 1 && X->ReadyCounter == 2 && X->bMyLockLatch,
				TEXT("E3 контроль: 5293 уже ушёл (+0x28 = 1) → отказ фиксации готовность НЕ откатывает (подтверждение на сервере)"),
				FString::Printf(TEXT("+0x28=%d +4C4=%d счётчик=%d латч=%d"), X->bCompleteSent, X->bMyLock, X->ReadyCounter, X->bMyLockLatch ? 1 : 0));
			Clean(C);
			OpenAsTarget(C, 0, 123);
			PutSingle(C, 2014, 2, 1);
			SetFix(0);
			ToggleMine(C, true);
			Feed(C, 5292, Body5292(2, 0));
			const bool bKey0 = X->bMyLock == 1 && X->bMyLockLatch && X->ReadyCounter == 1;
			SetFix(1);
			Clean(C);
			OpenAsTarget(C, 0, 124);
			PutSingle(C, 2014, 2, 1);
			Feed(C, 5292, Body5292(1, 1, 20573, 1));
			Feed(C, 5292, Body5292(1, 1, 20573, 1));   // дубль готовности партнёра — счётчик уже 2
			ToggleMine(C, true);                        // мой взвод счётчик не увеличил
			const bool bNoInc = X->ReadyCounter == 2 && !X->bLockCounted;
			Feed(C, 5292, Body5292(2, 0));
			Expect(bKey0 && bNoInc && X->ReadyCounter == 2 && X->bMyLock == 0,
				TEXT("E4 ключ 0 — как эталон (готовность остаётся); счётчик был 2 до моего взвода → откат его не трогает (2, а не 1; контроль — E1/E2 снимают)"),
				FString::Printf(TEXT("ключ0=%d без-инкремента=%d счётчик=%d +4C4=%d"), bKey0 ? 1 : 0, bNoInc ? 1 : 0, X->ReadyCounter, X->bMyLock));
		}
		// ═══ F. Д18 по эху 5289 (ревизия О3 МЕЛК-1): сервер промолчал на первый 5288 — ответ на второй идёт ко второй ячейке ═══
		Clean(C);
		{
			OpenAsTarget(C, 0, 130);
			X->OnCellPressed(2014); X->OnCellDoubleClick(2014);   // 5288 А: ячейка 0, к=2 слот 1 — ответа не будет
			X->OnCellPressed(2016); X->OnCellDoubleClick(2016);   // 5288 Б: ячейка 1, к=2 слот 3
			const bool bQ2 = X->AddQueue.Num() == 2;
			Feed(C, 5289, Body5289(4, 2, 3, 1));                   // отказ по Б (эхо к=2 слот 3)
			const bool bB = TrayCode(1) == 0 && ListCode(0, 3) == 20626 && TrayCode(0) == 20572 && X->AddQueue.Num() == 0
				&& Count(5290) == 1 && Last(5290) && Last(5290)->P.Num() == 11 && Last(5290)->P[4] == 2 && AsdaBytes::RdI16(Last(5290)->P.GetData() + 5) == 3;
			Clean(C);
			OpenAsTarget(C, 0, 131);
			X->OnCellPressed(2014); X->OnCellDoubleClick(2014);
			Feed(C, 5289, Body5289(1, 2, 59, 1));                  // эхо не совпадает ни с одним запросом
			const bool bKeep = X->AddQueue.Num() == 1;
			Expect(bQ2 && bB && bKeep,
				TEXT("F1 два 5288, ответ только на второй: откат ячейки 1 (20626 вернулась в список), ячейка 0 (20572) цела, первая запись снята, 5290 [к=2 слот 3]; контроль — эхо без пары: очередь не тронута (как эталон)"),
				FString::Printf(TEXT("очередь2=%d Б=%d 5290×%d без-пары=%d"), bQ2 ? 1 : 0, bB ? 1 : 0, Count(5290), bKeep ? 1 : 0));
		}
		// ═══ G. Сброс сессии / ESC / «закрыть всё» при ожидании итога ═══
		Clean(C);
		{
			OpenAsTarget(C, 0, 140);
			PutSingle(C, 2014, 2, 1);
			ReadyAndOk(C);
			WM->CloseTopmostOnEsc();
			const bool bEsc = WinOpen() && Count(5286) == 0;
			Net->DebugFeedPacket(5295, Body5295(5, 0, { {20580, 2, 40, 2, 0} }));   // итог в очереди, кадр ещё не разобран
			X->ResetSessionState(EAsdaSessionResetReason::MapChanged);
			X->Tick(0.f);
			Expect(bEsc && !WinOpen() && CellCode(C, 2, 1) == 20572 && CellCode(C, 2, 40) == 0 && X->GetLastNoticeLoc() != 5030 && Count(5286) == 0,
				TEXT("G1 ожидание итога: ESC окно не закрывает (эталон); сброс сессии с 5295 в очереди — окно закрыто молча, итог старого соединения не применён"),
				FString::Printf(TEXT("ESC=%d окно=%d 2:1=%d 2:40=%d нотис %d 5286×%d"), bEsc ? 1 : 0, WinOpen() ? 1 : 0, CellCode(C, 2, 1), CellCode(C, 2, 40),
					X->GetLastNoticeLoc(), Count(5286)));
			Clean(C);
			OpenAsTarget(C, 0, 141);
			ReadyAndOk(C);
			const bool bLeave = X->OnCloseAllWindowsOnLeave(TEXT("самотест О4"));
			Expect(bLeave && Count(5286) == 1 && Last(5286)->P.Num() >= 4 && AsdaBytes::RdI32(Last(5286)->P.GetData()) == 141 && !WinOpen(),
				TEXT("G2 «закрыть всё при уходе» при ожидании итога → 5286 [#141] (Д1), окно закрыто"),
				FString::Printf(TEXT("было=%d 5286×%d тело %s окно=%d"), bLeave ? 1 : 0, Count(5286), *Hex(Last(5286)), WinOpen() ? 1 : 0));
		}
		// ── окно на фазу 2: инициатор из меню, тип 0, #150 ──
		Clean(C);
		X->OnMenuCommand(false, kPeer, kPeer, TEXT("Партнёр"));
		Feed(C, 5285, Body5285(1, 0, 150, 500, TEXT("Я"), kPeer, TEXT("Партнёр")));
		Caps.Reset();
		return true;
	}

	// Центр живого узла окна 2008 (абсолютные координаты Slate).
	static bool CenterOf(UAsdaExchangeP2PSubsystem* X, const TCHAR* Name, int32 CellId, FVector2D& Out)
	{
		UWidget* Wd = Name ? X->DebugNamedWidget(Name) : X->DebugCellWidget(CellId);
		FVector2D Mn, Mx;
		if (!Wd || !AsdaUi::WidgetAbsRect(Wd, Mn, Mx)) { return false; }
		Out = (Mn + Mx) * 0.5;
		return true;
	}
	// Настоящий клик ЛКМ Slate по узлу (или ячейке).
	bool Click(UWorld* W, const TCHAR* Name, int32 CellId = 0)
	{
		const FCtx C = Ctx(W);
		FVector2D P;
		if (!C.Valid() || !CenterOf(C.X, Name, CellId, P))
		{
			Expect(false, FString::Printf(TEXT("путь игрока: клик по %s"), Name ? Name : *FString::FromInt(CellId)), TEXT("нет живой геометрии"));
			return false;
		}
		C.WM->SimulateClickAt(P, /*HoldFrames*/ 2, /*bRight*/ false);
		return true;
	}

	// Фаза 2 — путь игрока: шаги с паузами, настоящие события Slate.
	void BuildInputPhase()
	{
		using namespace AsdaP2PTestO4Local;
		// 1. Первое нажатие по одиночке 2014.
		Steps.Add([this](UWorld* W) { Click(W, nullptr, 2014); return true; });
		// 2. Второе нажатие в окне двойного щелчка → 0xD → 5288.
		Steps.Add([this](UWorld* W) { Click(W, nullptr, 2014); return true; });
		// 3. Проверка 5288; ответ 5289; клик по 2048.
		Steps.Add([this](UWorld* W)
		{
			const FCtx C = Ctx(W); if (!C.Valid()) { return true; }
			Expect(Count(5288) == 1 && Same(Last(5288), Want5288(150, 0, 2, 1, 1)), TEXT("путь игрока: два нажатия по 2014 → 0xD → 5288 [#150, ячейка 0, к=2 слот 1, 1]"),
				FString::Printf(TEXT("5288×%d тело %s"), Count(5288), *Hex(Last(5288))));
			Feed(C, 5289, Body5289(1, 2, 1, 1));
			Click(W, TEXT("TRAN_BUT01"));
			return true;
		});
		// 4. Проверка 5291; готовность партнёра; клик по 2011.
		Steps.Add([this](UWorld* W)
		{
			const FCtx C = Ctx(W); if (!C.Valid()) { return true; }
			Expect(Count(5291) == 1 && C.X->bMyLock == 1 && C.X->DebugIsChecked(2048), TEXT("путь игрока: клик по 2048 → 5291, галка, готовность"),
				FString::Printf(TEXT("5291×%d +4C4=%d"), Count(5291), C.X->bMyLock));
			Feed(C, 5292, Body5292(1, 1, 20573, 1));   // 2011 появится — живая геометрия будет к следующему шагу
			return true;
		});
		// 4б. Клик по 2011 (виден после готовности партнёра).
		Steps.Add([this](UWorld* W) { Click(W, TEXT("TRAN_OKBUT")); return true; });
		// 5. Проверка 5293 и «инвентарь цел до итога»; итог 5295.
		Steps.Add([this](UWorld* W)
		{
			const FCtx C = Ctx(W); if (!C.Valid()) { return true; }
			const bool bSame = CellCode(C, 2, 1) == 20572 && CellAmt(C, 2, 4) == 6;
			Expect(Count(5293) == 1 && Same(Last(5293), Want5293(150)) && bSame, TEXT("путь игрока: клик по 2011 → 5293 [#150]; инвентарь до итога цел (контроль к итогу)"),
				FString::Printf(TEXT("5293×%d тело %s инвентарь цел=%d"), Count(5293), *Hex(Last(5293)), bSame ? 1 : 0));
			Feed(C, 5295, Body5295(5, 2222, { {20573, 2, 4, 3, 0} }));
			return true;
		});
		// 6. Проверка итога; новое окно (цель, #151) для контроля двойного щелчка.
		Steps.Add([this](UWorld* W)
		{
			const FCtx C = Ctx(W); if (!C.Valid()) { return true; }
			Expect(!C.X->IsWindowShown() && CellCode(C, 2, 1) == 0 && CellAmt(C, 2, 4) == 9 && C.X->GetLastNoticeLoc() == 5030 && C.Net->SelfWeightCur == 2222,
				TEXT("путь игрока: итог 5295 → окно закрыто, 20572 отдана, стопка 20573 = 9, 5030, вес 2222"),
				FString::Printf(TEXT("окно=%d 2:1=%d 2:4=%d нотис %d вес %d"), C.X->IsWindowShown() ? 1 : 0, CellCode(C, 2, 1), CellAmt(C, 2, 4),
					C.X->GetLastNoticeLoc(), C.Net->SelfWeightCur));
			Clean(C);
			OpenAsTarget(C, 0, 151);
			Caps.Reset();
			return true;
		});
		// 7. Первое нажатие по 2014 — метка времени.
		Steps.Add([this](UWorld* W) { Click(W, nullptr, 2014); WaitFrom = FPlatformTime::Seconds(); return true; });
		// 8. Ждать дольше интервала двойного щелчка, затем второе нажатие (ревизия О3 МЕЛК-4).
		Steps.Add([this](UWorld* W)
		{
			if (FPlatformTime::Seconds() - WaitFrom < DblSec() + 0.25) { return false; }
			Click(W, nullptr, 2014);
			return true;
		});
		// 9. Проверка: одиночный клик — 5288 нет, выбор есть (контроль — шаг 2).
		Steps.Add([this](UWorld* W)
		{
			const FCtx C = Ctx(W); if (!C.Valid()) { return true; }
			Expect(Count(5288) == 0 && C.X->bCellTaken == 1 && C.X->SelSlotIndex == 1,
				TEXT("путь игрока: второе нажатие ПОСЛЕ интервала двойного щелчка — одиночный клик (выбор), 5288 нет (контроль к шагу 2)"),
				FString::Printf(TEXT("5288×%d +0x10=%d выбор=%d (интервал %.0f мс)"), Count(5288), C.X->bCellTaken, C.X->SelSlotIndex, DblSec() * 1000.0));
			return true;
		});
	}

	// Восстановление всего, что трогал тест.
	void Restore(UWorld* W)
	{
		const FCtx C = Ctx(W);
		if (C.X && C.WM) { C.X->ResetSessionState(EAsdaSessionResetReason::LeftWorld); C.WM->ClearMsgBoxQueue(TEXT("самотест О4")); }
		if (C.WM && C.WM->IsNumPadOpen()) { C.WM->CloseNumPad(false); }
		if (C.Net)
		{
			Disarm(C.Net);
			C.Net->SelfUserNum = SelfNumWas; C.Net->bSelfUserNumKnown = bSelfKnownWas; C.Net->SelfLevel = LevelWas;
			C.Net->SelfWeightCur = WeightCurWas; C.Net->SelfWeightMax = WeightMaxWas;
		}
		if (C.Inv)
		{
			for (int32 i = 0; i < Cont2Was.Num(); ++i) { if (FAsdaItemInstance* Cl = C.Inv->GetCellMutable(2, i)) { *Cl = Cont2Was[i]; } }
			for (int32 i = 0; i < Cont1Was.Num(); ++i) { if (FAsdaItemInstance* Cl = C.Inv->GetCellMutable(1, i)) { *Cl = Cont1Was[i]; } }
			C.Inv->SetGoldDisplay(GoldWas);
		}
		if (C.Au) { C.Au->ResetDeferredSaleGold(TEXT("самотест О4: восстановление")); }
		if (C.PM) { C.PM->NotePickedUser(PickWas); }
		AsdaCVarSave::RestoreAll(TEXT("asda.Trade.SelfTestExchangeO4"));   // ★ ИСПРАВЛЕНИЕ 11.10.2026 (В0, N1(д)/U8): ключ P2PFixDefects — значение + SetBy (прежде — с SetByConsole)
	}
};

namespace
{
	FAutoConsoleCommandWithWorldAndArgs GAsdaTradeSelfTestExchangeO4(
		TEXT("asda.Trade.SelfTestExchangeO4"),
		TEXT("asda.Trade.SelfTestExchangeO4 [delay=N] [quit] — самотест обмена, волна О4: полный цикл (меню → 5282, 5285, лоток 5288/5289, "
		     "готовность 5291/5292, ОК 5293) и итог 5295/5294 (применение записей в инвентарь, золото, отложенное золото 9913, вес, закрытие, "
		     "5030), отказы 0…4 (5025+9571 / 5026 / 1012 / 5028 / 5029), Д21, Д18 по эху, счётчик готовности, сброс/ESC/«закрыть всё»; "
		     "фаза путём игрока — клики Slate по живой геометрии. Только офлайн."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
		{
			const FAsdaSelfTestArgs KitArgs = FAsdaSelfTestArgs::Parse(Args);   // ★ 11.10.2026 (В3 S1): каркас — отказ с ИТОГ и quit, второй барьер на все опкоды
			if (AsdaSelfTestKit::RefuseStart(W, TEXT("asda.Trade.SelfTestExchangeO4"), KitArgs)) { return; }
			const float Delay = AsdaSelfTestKit::DelayArg(Args);
			const bool bQuit = Args.Contains(TEXT("quit"));
			TWeakObjectPtr<UWorld> WW(W);
			// Офлайн-спавн штатными крюками (как О1–О3), логика — через 2 с; шаги пути игрока — по кадрам (планировщик О3).
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WW, bQuit](float) -> bool
			{
				UWorld* Wl = WW.Get();
				if (!Wl) { return false; }
				if (AsdaSelfTestKit::RefuseDeferred(Wl, TEXT("asda.Trade.SelfTestExchangeO4"), bQuit)) { return false; }   // Р-2: офлайн повторно — на отложенном старте
				const AAsdaPlayerController* P = Cast<AAsdaPlayerController>(Wl->GetFirstPlayerController());
				const bool bNeedSpawn = !P || !P->GetPawn();
				if (bNeedSpawn && GEngine)
				{
					GEngine->Exec(Wl, TEXT("Asda.Load.CurrentMap 3"));
					GEngine->Exec(Wl, TEXT("Asda.World.FakeSelfSpawn 256 256"));
				}
				FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WW, bQuit](float) -> bool
				{
					UWorld* W2 = WW.Get();
					if (!W2) { return false; }
					TSharedRef<FAsdaExchangeP2PSelfTestO4> T = MakeShared<FAsdaExchangeP2PSelfTestO4>();
					if (!T->RunPhase1(W2))
					{
						AsdaSelfTestKit::Quit(W2, bQuit);
						return false;
					}
					T->BuildInputPhase();
					// Шаги по КАДРАМ: сначала 20 «быстрых» кадров подряд (< 0,1 с), затем шаг не чаще раза в 8 кадров; шаг, вернувший
					//   false (ожидание интервала), повторяется.
					struct FSched { int32 Idx = 0; int32 Fast = 0; uint64 LastFrame = 0; };
					TSharedRef<FSched> Sch = MakeShared<FSched>();
					FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WW, T, Sch, bQuit](float Dt) -> bool
					{
						UWorld* W3 = WW.Get();
						if (!W3) { AsdaCVarSave::RestoreAll(TEXT("asda.Trade.SelfTestExchangeO4")); return false; }   // ★ ИСПРАВЛЕНИЕ 11.10.2026 (В0, N1(д)/U8): ключи — и при уходе мира
						if (Sch->Fast < 20) { Sch->Fast = (Dt < 0.1f) ? Sch->Fast + 1 : 0; return true; }
						if (GFrameCounter - Sch->LastFrame < 8) { return true; }
						Sch->LastFrame = GFrameCounter;
						if (Sch->Idx < T->Steps.Num()) { if (T->Steps[Sch->Idx](W3)) { ++Sch->Idx; } return true; }
						T->Restore(W3);
						UE_LOG(LogAsdaP2PTestO4, Log, TEXT("САМОТЕСТ-О4 ИТОГ: ok=%d fail=%d"), T->Ok, T->Fail);
						AsdaSelfTestKit::Quit(W3, bQuit);
						return false;
					}), 0.f);
					return false;
				}), bNeedSpawn ? 2.f : 0.f);
				return false;
			}), Delay);
			UE_LOG(LogAsdaP2PTestO4, Log, TEXT("САМОТЕСТ-О4: запуск через %.1f с"), Delay);
		}));
}

#endif // !UE_BUILD_SHIPPING
