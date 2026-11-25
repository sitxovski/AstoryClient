// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ ОБМЕНА, ВОЛНА О3 (30.09.2026, `Reports/trade/wave_O3.md`): готовность 5291 / приём 5292 / итог 5293 и
//   исправления по ревизии О2 (Д18 очередь 5289, Д20 золото, `g_nPickedUserIdx`, снятие без отправки, двойной щелчок на втором
//   нажатии, окно не поднялось, нумпад, самоцветы подсказки партнёра, `IsMsgBoxDisplayed`, смерть обеих сторон). [ПОРТ]
//
// ЧТО ПРОВЕРЯЕТ. Модель `UAsdaExchangeP2PSubsystem` БОЕВЫМИ путями: входящие 5283/5285/5289/5292 — `UAsdaNetSubsystem::DebugFeedPacket`
//   → `HandlePacket` → `Tick` подсистемы; исходящие 5286/5288/5290/5291/5293 — БОЕВЫЕ отправители под перехватом (`ArmSendCapture`,
//   в сеть ничего не уходит), тела сверяются побайтно. Фаза 1 — события окна теми же методами, что зовёт опрос мыши; фаза 2 — ПУТЁМ
//   ИГРОКА: настоящие клики Slate (`SimulateClickAt`) по живой геометрии флажков 2048/2049, кнопки 2011 и ячейки списка (двойной
//   щелчок — два нажатия), геометрия 2011 против `.JUI`. У каждой проверки — КОНТРОЛЬНАЯ ПАРА (закон pixel-metric-needs-control-pairs).
//
// Консоль: `asda.Trade.SelfTestExchangeO3 [delay=N] [quit]`. Только офлайн. Строки — «САМОТЕСТ-О3», итог — «САМОТЕСТ-О3 ИТОГ».
//   Свой userNum/уровень, инвентарь, ключи, перехватчик, смерть, поза соседа и пик восстанавливаются в конце.
#include "CoreMinimal.h"
#include "AsdaTestPacketsP2P.h"   // ★ R5-c (12.11.2026): общая точка самотестов вместо своих копий помощников

#if !UE_BUILD_SHIPPING

#include "UI/AsdaExchangeP2PSubsystem.h"
#include "AsdaCVarSave.h"                   // ★ ИСПРАВЛЕНИЕ 11.10.2026 (В0, N1(д)/U8): ключи консоли самотеста — запомнить и вернуть (значение + SetBy)
#include "UI/AsdaTradeStateSubsystem.h"
#include "UI/AsdaHotbarSubsystem.h"
#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaInventorySubsystem.h"
#include "UI/AsdaChatSubsystem.h"
#include "UI/AsdaPlayerMenuSubsystem.h"
#include "UI/AsdaNotice.h"
#include "UI/AsdaUiHelpers.h"
#include "World/AsdaWorldSubsystem.h"
#include "World/AsdaPlayerController.h"
#include "World/AsdaPlayerCharacter.h"
#include "World/AsdaPossessGate.h"
#include "Login/AsdaLoginData.h"
#include "AsdaNetSubsystem.h"
#include "Test/AsdaSelfTestKit.h"   // ★ 11.10.2026 (В3 S1): общий каркас самотестов (аргументы, отказ, перехват, итог, выход)
#include "AsdaBytes.h"
#include "Blueprint/UserWidget.h"
#include "Components/Button.h"
#include "Components/CheckBox.h"
#include "Components/EditableText.h"
#include "Blueprint/WidgetTree.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include "Windows/MinWindows.h"             // `GetDoubleClickTime` — в лог: окно двойного щелчка на этой машине
#include "Windows/HideWindowsPlatformTypes.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogAsdaP2PTestO3, Log, All);

namespace AsdaP2PTestO3Local
{
	// Разбор `delay=N` (секунды до запуска).
	// ★ R5-fix-c (13.11.2026): своя `ParseDelay` (дословная копия у двенадцати самотестов) убрана — разбор `delay=N` в общей точке
	//   `AsdaSelfTestKit::DelayArg` (`Test/AsdaSelfTestKit.h`), вызовы ниже идут в неё.

	// Пойманный исходящий пакет: опкод и payload (без конверта).
	struct FCap { uint16 Op = 0; TArray<uint8> P; };

	// Тело 5283 (24 б): [0] код, [1..2] userNum инициатора, [3..22] имя cp1251, [23] тип.
	using AsdaTestPacketsP2P::Body5283;   // ★ R5-c (12.11.2026): общая точка (`AsdaTestPacketsP2P.h`) — копия была дословной.
	// Тело 5285 (50 б): [0] код, [1] тип, [2..5] windowIndex, [6..7]/[8..27] сторона 1, [28..29]/[30..49] сторона 2.
	using AsdaTestPacketsP2P::Body5285;   // ★ R5-c (12.11.2026): общая точка (`AsdaTestPacketsP2P.h`) — копия была дословной.
	// Тело 5289 (8 б): [0] код, [1] invType, [2..3] invSlot, [4..7] кол-во.
	using AsdaTestPacketsP2P::Body5289;   // ★ R5-c (12.11.2026): общая точка (`AsdaTestPacketsP2P.h`) — копия была дословной.
	// Тело 5292 (261 б) — раскладка сервера `CharExchangeFixReq` (GameServer.c:89195-89212, `sub_4ED270`): [0..3] windowIndex,
	//   [4] действие, [5] lock, 5 × 51 с [6]: код @0 (пустая −1), кол-во @7, заточка @11, «+N%» @14.
	using AsdaTestPacketsP2P::Body5292;   // ★ R5-c (12.11.2026): общая точка (`AsdaTestPacketsP2P.h`) — копия была дословной.
	// Ожидаемое тело 5288 (12 б).
	using AsdaTestPacketsP2P::Want5288;   // ★ R5-c (12.11.2026): общая точка (`AsdaTestPacketsP2P.h`) — копия была дословной.
	// Ожидаемое тело 5290 (11 б).
	using AsdaTestPacketsP2P::Want5290;   // ★ R5-c (12.11.2026): общая точка (`AsdaTestPacketsP2P.h`) — копия была дословной.
	// Ожидаемое тело 5291 (40 б): ячейка 0 — (Type, Slot, Count), остальные пусты (0, −1, 0).
	TArray<uint8> Want5291(int32 Idx, int32 Lock, int32 Type, int32 Slot, int32 Count)
	{
		TArray<uint8> B; B.SetNumZeroed(40);
		AsdaBytes::WrI32(B.GetData(), Idx); B[4] = (uint8)Lock;
		for (int32 k = 0; k < 5; ++k) { AsdaBytes::WrI16(B.GetData() + 5 + 7 * k + 1, -1); }
		B[5] = (uint8)Type; AsdaBytes::WrI16(B.GetData() + 6, Slot); AsdaBytes::WrI32(B.GetData() + 8, Count);
		return B;
	}
	// Ожидаемое тело 5293 (4 б).
	using AsdaTestPacketsP2P::Want5293;   // ★ R5-c (12.11.2026): общая точка (`AsdaTestPacketsP2P.h`) — копия была дословной.
}

struct FAsdaExchangeP2PSelfTestO3
{
	int32 Ok = 0;     // пройдено
	int32 Fail = 0;   // провалено
	TArray<AsdaP2PTestO3Local::FCap> Caps;          // пойманные исходящие
	TArray<TFunction<void(UWorld*)>> Steps;         // шаги фазы «путём игрока» (по одному за тик планировщика)

	// Одна проверка: «САМОТЕСТ-О3 ok/ПРОВАЛ: <имя> — <подробности>».
	void Expect(bool bCond, const FString& Name, const FString& Detail)
	{
		if (bCond) { ++Ok; } else { ++Fail; }
		UE_LOG(LogAsdaP2PTestO3, Log, TEXT("САМОТЕСТ-О3 %s: %s — %s"), bCond ? TEXT("ok") : TEXT("ПРОВАЛ"), *Name, *Detail);
	}
	// Сколько пойманных пакетов с опкодом.
	int32 Count(uint16 Op) const { int32 N = 0; for (const auto& C : Caps) { if (C.Op == Op) { ++N; } } return N; }
	// Последний пойманный пакет с опкодом (nullptr — нет).
	const AsdaP2PTestO3Local::FCap* Last(uint16 Op) const { for (int32 i = Caps.Num() - 1; i >= 0; --i) { if (Caps[i].Op == Op) { return &Caps[i]; } } return nullptr; }
	// Hex payload для подробностей.
	static FString Hex(const AsdaP2PTestO3Local::FCap* C)
	{
		if (!C) { return TEXT("—"); }
		FString S; for (uint8 B : C->P) { S += FString::Printf(TEXT("%02X"), B); }
		return S;
	}
	// Payload побайтно равен ожидаемому.
	static bool Same(const AsdaP2PTestO3Local::FCap* C, const TArray<uint8>& Want) { return C && C->P == Want; }

	// Состояние, которое надо вернуть в конце.
	uint16 SelfNumWas = 0; bool bSelfKnownWas = false; uint8 LevelWas = 0; uint32 Gen = 0;
	int32 QueueFixWas = 1; int32 PickWas = 0;
	TArray<FAsdaItemInstance> Cont1Was, Cont2Was;   // инвентарь до теста
	static constexpr int32 kPeer = 902;             // сосед-партнёр (фейковый, уровень 10)

	// Поставить перехватчик всего исходящего (офлайн сокета нет — иначе отправители вернут «не ушёл»).
	void Arm(UAsdaNetSubsystem* Net)
	{
		Gen = AsdaSelfTestKit::ArmCapture(Net, [this](uint16 Op, const uint8* P, int32 Len, bool, TCHAR)
		{
			AsdaP2PTestO3Local::FCap& C = Caps.AddDefaulted_GetRef();
			C.Op = Op;
			C.P.Append(P, Len);
		}, 0, TEXT("asda.Trade.SelfTestExchangeO3"));
	}
	// Снять перехватчик (офлайн — отправка «не уходит»: путь неотправленного).
	void Disarm(UAsdaNetSubsystem* Net) { if (Net) { AsdaSelfTestKit::DisarmCapture(Net, Gen); } }

	// Контекст подсистем.
	struct FCtx
	{
		UGameInstance* GI = nullptr; UAsdaExchangeP2PSubsystem* X = nullptr; UAsdaNetSubsystem* Net = nullptr; UAsdaWindowSubsystem* WM = nullptr;
		UAsdaHotbarSubsystem* Hb = nullptr; UAsdaWorldSubsystem* WS = nullptr; UAsdaInventorySubsystem* Inv = nullptr; UAsdaChatSubsystem* Chat = nullptr;
		UAsdaPlayerMenuSubsystem* PM = nullptr;
		bool Valid() const { return GI && X && Net && WM && Hb && WS && Inv && Chat && PM; }
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
		return C;
	}
	// Сброс между блоками: модель, очередь окон-сообщений, нумпад, пойманное.
	void Clean(const FCtx& C)
	{
		C.X->ResetSessionState(EAsdaSessionResetReason::LeftWorld);
		C.WM->ClearMsgBoxQueue(TEXT("самотест О3"));
		if (C.WM->IsNumPadOpen()) { C.WM->CloseNumPad(false); }
		Caps.Reset();
	}
	// Приём штатным путём: пакет → `HandlePacket` → очередь подсистемы → её `Tick`.
	static void Feed(const FCtx& C, int32 Op, const TArray<uint8>& B) { C.Net->DebugFeedPacket(Op, B); C.X->Tick(0.f); }
	// Открыть окно как ЦЕЛЬ (заявка → значок → «Принять» → 5285/1) типа Type с индексом окна Idx.
	static void OpenAsTarget(const FCtx& C, int32 Type, int32 Idx)
	{
		using namespace AsdaP2PTestO3Local;
		Feed(C, 5283, Body5283(1, kPeer, TEXT("Партнёр"), Type));
		C.Hb->HandleNoticeIconClick(Type ? 5 : 4);
		C.WM->AnswerMsgBox(true, true);
		Feed(C, 5285, Body5285(1, Type, Idx, kPeer, TEXT("Партнёр"), 500, TEXT("Я")));
	}
	// Положить кандидата-одиночку списка (ячейка `IdNum`) двойным щелчком модели и подтвердить его 5289/1.
	static void PutSingle(const FCtx& C, int32 IdNum, int32 InvType, int32 InvSlot)
	{
		using namespace AsdaP2PTestO3Local;
		C.X->OnCellPressed(IdNum); C.X->OnCellDoubleClick(IdNum);
		Feed(C, 5289, Body5289(1, InvType, InvSlot, 1));
	}
	// Клик по флажку 2048 так, как его доставляет UE: флажок сам меняет галку и шлёт 0x10601/0x10602.
	static void ToggleMine(const FCtx& C, bool bChecked)
	{
		if (UCheckBox* Mine = Cast<UCheckBox>(C.X->DebugNamedWidget(TEXT("TRAN_BUT01")))) { Mine->SetIsChecked(bChecked); }
		C.X->OnMyLockChecked(bChecked);
	}
	// Клик по кнопке 2011 её `OnClicked` (реле кнопки окна).
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

	// Фаза 1: логика (синхронно). Возврат — false, если нечего проверять.
	bool RunPhase1(UWorld* W)
	{
		using namespace AsdaP2PTestO3Local;
		const FCtx C = Ctx(W);
		if (!C.Valid())
		{
			UE_LOG(LogAsdaP2PTestO3, Error, TEXT("САМОТЕСТ-О3 ИТОГ: ok=0 fail=1 (нет подсистем)"));
			return false;
		}
		UAsdaExchangeP2PSubsystem* X = C.X;
		UAsdaNetSubsystem* Net = C.Net;
		UAsdaWindowSubsystem* WM = C.WM;
		UGameInstance* GI = C.GI;
		// ── подготовка: свой userNum 500, уровень 30, сосед 902 (уровень 10), инвентарь, ключи, перехват ──
		SelfNumWas = Net->SelfUserNum; bSelfKnownWas = Net->bSelfUserNumKnown; LevelWas = Net->SelfLevel;
		Net->SelfUserNum = 500; Net->bSelfUserNumKnown = true; Net->SelfLevel = 30;
		C.WS->DebugSpawnFakePeer(kPeer, TEXT("Партнёр"), FVector2D(25710.f, 25700.f));
		IConsoleVariable* CvFix = IConsoleManager::Get().FindConsoleVariable(TEXT("asda.Trade.P2PFixDefects"));
		IConsoleVariable* CvDeath = IConsoleManager::Get().FindConsoleVariable(TEXT("asda.Trade.P2PCloseOnDeath"));
		IConsoleVariable* CvQueue = IConsoleManager::Get().FindConsoleVariable(TEXT("asda.MsgBox.QueueFixes"));
		QueueFixWas = CvQueue ? CvQueue->GetInt() : 1;   // ★ ИСПРАВЛЕНИЕ 11.10.2026 (В0, N1(д)/U8): исходные P2PFixDefects/P2PCloseOnDeath запоминает `AsdaCVarSave`
		PickWas = C.PM->GetPickedUserIdx();
		auto SetFix = [CvFix](int32 V) { if (CvFix) { AsdaCVarSave::SetForTest(TEXT("asda.Trade.P2PFixDefects"), V); } };
		auto SetQueue = [CvQueue](int32 V) { if (CvQueue) { AsdaCVarSave::SetForTest(TEXT("asda.MsgBox.QueueFixes"), V); } };
		SetFix(1);
		if (CvDeath) { AsdaCVarSave::SetForTest(TEXT("asda.Trade.P2PCloseOnDeath"), 1); }
		for (int32 i = 0; i < 60; ++i) { if (const FAsdaItemInstance* Cl = C.Inv->GetCell(2, i)) { Cont2Was.Add(*Cl); } }
		for (int32 i = 0; i < UAsdaInventorySubsystem::kShellSlots; ++i) { if (const FAsdaItemInstance* Cl = C.Inv->GetCell(1, i)) { Cont1Was.Add(*Cl); } }   // ★ 29.10.2026 (инвентарь ×2): вся сумка 60
		for (int32 i = 0; i < 60; ++i) { C.Inv->DebugPutItem(2, i, 0, 0); }
		for (int32 i = 0; i < UAsdaInventorySubsystem::kShellSlots; ++i) { C.Inv->DebugPutItem(1, i, 0, 0); }   // ★ 29.10.2026 (инвентарь ×2): вся сумка 60
		// Контейнер 2: стопка 5, одиночка 20572 (слот 1), золото 1000 (слот 2), одиночка 20626 (слот 3), стопки 6…
		C.Inv->DebugPutItem(2, 0, 20571, 5);
		C.Inv->DebugPutItem(2, 1, 20572, 1);
		C.Inv->DebugPutItem(2, 2, 20551, 1000);
		C.Inv->DebugPutItem(2, 3, 20626, 1);
		static const int32 kCodes[] = { 20573, 20577, 20578, 20579, 20583, 20586, 20587, 20588, 20589, 20590 };
		for (int32 i = 4; i < 30; ++i) { C.Inv->DebugPutItem(2, i, kCodes[(i - 4) % UE_ARRAY_COUNT(kCodes)], i + 2); }
		// Контейнер 1: две вещи категории 3 (зелье 336 для фильтра пика кладётся только в блоке K — список типа 1 блока I без него).
		C.Inv->DebugPutItem(1, 0, 20626, 1);
		C.Inv->DebugPutItem(1, 1, 20627, 1);
		Arm(Net);
		auto Loc = [GI](int32 Id) { return AsdaNotice::Loc(GI, Id); };
		auto ListCode = [X](int32 Type, int32 Idx) { const uint8* R = X->ListRec(Type, Idx); return R ? AsdaBytes::RdI32(R) : 0; };
		auto ListCount = [X](int32 Type, int32 Idx) { const uint8* R = X->ListRec(Type, Idx); return R ? AsdaBytes::RdI32(R + 9) : 0; };
		auto TrayCode = [X](int32 K) { const uint8* R = X->TrayRec(K); return R ? AsdaBytes::RdI32(R) : 0; };
		auto TrayCount = [X](int32 K) { const uint8* R = X->TrayRec(K); return R ? AsdaBytes::RdI32(R + 9) : 0; };
		auto HasCode = [&](int32 Type, int32 Code) { for (int32 i = 0; i < 31; ++i) { if (ListCode(Type, i) == Code) { return true; } } return false; };
		auto ChatColorIs = [&](int32 LocId, const FColor& Want) { FColor Col; return C.Chat->DebugFindLastSystemLineColor(Loc(LocId).Left(12), Col) && Col == Want; };
		const FColor kDef(0xFE, 0xD9, 0xF1, 0xFF);   // −75279
		const FColor kLock(0xFF, 0xD3, 0x06, 0xFF);  // −11514

		// ═══ A. Готовность 2048 → 5291; запертый флажок; перенос при готовности — 5045 ═══
		Clean(C);
		{
			OpenAsTarget(C, 0, 70);
			PutSingle(C, 2014, 2, 1);   // лоток 0: 20572 (к=2, слот 1)
			Caps.Reset();
			const int32 Seq0 = X->GetNoticeSeq();
			const bool bBefore = Count(5291) == 0 && X->bMyLock == 0 && !X->DebugIsChecked(2048);
			ToggleMine(C, true);
			const FCap* L = Last(5291);
			Expect(bBefore && Count(5291) == 1 && Same(L, Want5291(70, 1, 2, 1, 1)) && X->bMyLock == 1 && X->bMyLockLatch && X->DebugIsChecked(2048)
				&& X->ReadyCounter == 1 && X->bLockOff == 0 && !X->DebugIsShown(TEXT("TRAN_OKBUT")) && X->GetNoticeSeq() == Seq0,
				TEXT("A1 клик 2048 → +0x4C4 = 1, галка, латч +609, 5291 [#70, lock 1, ячейка 0: к=2 слот 1 ×1, прочие 0/−1/0], счётчик 1, 2011 скрыт, нотисов нет (до клика 5291 нет — контроль)"),
				FString::Printf(TEXT("до=%d тело %s +4C4=%d латч=%d галка=%d счётчик=%d +0C=%d 2011=%d нотисов +%d"), bBefore ? 1 : 0, *Hex(L), X->bMyLock,
					X->bMyLockLatch ? 1 : 0, X->DebugIsChecked(2048) ? 1 : 0, X->ReadyCounter, X->bLockOff, X->DebugIsShown(TEXT("TRAN_OKBUT")) ? 1 : 0, X->GetNoticeSeq() - Seq0));
			ToggleMine(C, false);   // 0x10602 по запертому флажку
			Expect(Count(5291) == 1 && X->DebugIsChecked(2048) && X->bMyLock == 1 && X->ReadyCounter == 1,
				TEXT("A2 повторный клик по запертому 2048 — события нет: галка держится, второго 5291 нет, счётчик 1"),
				FString::Printf(TEXT("5291×%d галка=%d счётчик=%d"), Count(5291), X->DebugIsChecked(2048) ? 1 : 0, X->ReadyCounter));
			X->OnCellPressed(2019);   // стопка 8 (20578): `itemEquipValidate` пропускает (самотест О2 F1) — отказ даёт именно готовность
			const bool bDrag = X->OnDragBegin(2019);
			const int32 NoteAfterDrag = X->GetLastNoticeLoc();
			X->CancelDragVisual();
			X->OnCellPressed(2013); X->OnCellDoubleClick(2013);
			Expect(!bDrag && NoteAfterDrag == 5045 && Count(5288) == 0 && !WM->IsNumPadOpen(),
				TEXT("A3 при готовности: перенос кандидата → 5045 (переноса нет), двойной щелчок — 5288 нет (до готовности 5288 ушёл — контроль A)"),
				FString::Printf(TEXT("перенос=%d нотис %d 5288×%d нумпад=%d"), bDrag ? 1 : 0, NoteAfterDrag, Count(5288), WM->IsNumPadOpen() ? 1 : 0));
		}
		// ═══ B. 5292 партнёра (lock 1): лоток партнёра, 2049, счётчик 2, 2011, 5046 + 5069; 2011 → 5293; повтор — нет ═══
		{
			const int32 Seq0 = X->GetNoticeSeq();
			Feed(C, 5292, Body5292(1, 1, 20573, 3));
			const bool bNotes = X->GetNoticeSeq() == Seq0 + 2 && X->GetLastNoticeLoc() == 5069 && ChatColorIs(5046, kDef) && ChatColorIs(5069, kDef);
			Expect(X->ReadyCounter == 2 && X->DebugIsChecked(2049) && X->DebugCellHasIcon(2042) && X->DebugCellText(2042) == TEXT("3")
				&& X->DebugIsShown(TEXT("TRAN_OKBUT")) && bNotes,
				TEXT("B1 5292 [1, lock 1, 20573 ×3] → лоток партнёра 2042 «3», галка 2049 кодом, счётчик 2, 2011 виден, чат 5046 + 5069 цветом −75279"),
				FString::Printf(TEXT("счётчик=%d 2049=%d 2042 иконка=%d «%s» 2011=%d нотисы=%d (последний %d)"), X->ReadyCounter, X->DebugIsChecked(2049) ? 1 : 0,
					X->DebugCellHasIcon(2042) ? 1 : 0, *X->DebugCellText(2042), X->DebugIsShown(TEXT("TRAN_OKBUT")) ? 1 : 0, bNotes ? 1 : 0, X->GetLastNoticeLoc()));
			Caps.Reset();
			ClickOk(C);
			const bool bFirst = Count(5293) == 1 && Same(Last(5293), Want5293(70)) && X->bCompleteSent == 1;
			ClickOk(C);
			Expect(bFirst && Count(5293) == 1, TEXT("B2 кнопка 2011 (OnClicked) → 5293 [#70], +0x28 = 1; второй клик — 5293 не повторяется (контроль)"),
				FString::Printf(TEXT("первый=%d 5293×%d тело %s +0x28=%d"), bFirst ? 1 : 0, Count(5293), *Hex(Last(5293)), X->bCompleteSent));
			const int32 Seq1 = X->GetNoticeSeq();
			Feed(C, 5292, Body5292(1, 1, 20573, 3));   // дубль готовности при +0x28 = 1
			Expect(X->ReadyCounter == 2 && X->GetNoticeSeq() == Seq1, TEXT("B3 повторный 5292 lock 1 при +0x28 = 1 — счётчик остаётся 2 (потолок), 5046/5069 не печатаются (контроль к B1)"),
				FString::Printf(TEXT("счётчик=%d нотисов +%d"), X->ReadyCounter, X->GetNoticeSeq() - Seq1));
		}
		// ═══ C. 5292 lock 0 (партнёр снял): счётчик 1, 2011 скрыт, 2049 снята, лоток партнёра пуст, +0x28 = 0; снова lock 1 — 5046 ═══
		{
			Feed(C, 5292, Body5292(1, 0));
			Expect(X->ReadyCounter == 1 && !X->DebugIsShown(TEXT("TRAN_OKBUT")) && !X->DebugIsChecked(2049) && !X->DebugCellHasIcon(2042) && X->bCompleteSent == 0,
				TEXT("C1 5292 lock 0 → счётчик 1, 2011 скрыт, 2049 снята, лоток партнёра пуст, +0x28 = 0"),
				FString::Printf(TEXT("счётчик=%d 2011=%d 2049=%d 2042=%d +0x28=%d"), X->ReadyCounter, X->DebugIsShown(TEXT("TRAN_OKBUT")) ? 1 : 0,
					X->DebugIsChecked(2049) ? 1 : 0, X->DebugCellHasIcon(2042) ? 1 : 0, X->bCompleteSent));
			const int32 Seq0 = X->GetNoticeSeq();
			Feed(C, 5292, Body5292(1, 1, 20573, 2));
			Expect(X->ReadyCounter == 2 && X->GetNoticeSeq() == Seq0 + 2 && X->DebugIsShown(TEXT("TRAN_OKBUT")),
				TEXT("C2 снова lock 1 при +0x28 = 0 → счётчик 2, 2011, 5046 + 5069 (контроль к B3)"),
				FString::Printf(TEXT("счётчик=%d нотисов +%d"), X->ReadyCounter, X->GetNoticeSeq() - Seq0));
		}
		// ═══ D. Отказы фиксации: [4] = 0 → 5071, [4] = 2 → 5073 (−11514), готовность/счётчик не трогаются (эталон) ═══
		//   ★ 30.09.2026 (О4): с ключом 1 отказ до 5293 теперь откатывает готовность (Д21) — блок проверяет ветку эталона при
		//   `asda.Trade.P2PFixDefects 0`; пара «ключ 1 — откат / после 5293 — нет» — в самотесте О4 (блок E).
		{
			const int32 Cnt = X->ReadyCounter;
			SetFix(0);
			Feed(C, 5292, Body5292(0, 0));
			const bool b0 = X->GetLastNoticeLoc() == 5071 && ChatColorIs(5071, kLock);
			Feed(C, 5292, Body5292(2, 0));
			const bool b2 = X->GetLastNoticeLoc() == 5073 && ChatColorIs(5073, kLock);
			SetFix(1);
			Expect(b0 && b2 && X->ReadyCounter == Cnt && X->bMyLock == 1 && X->DebugIsChecked(2048) && X->DebugIsShown(TEXT("TRAN_OKBUT")),
				TEXT("D1 ключ 0 (эталон): 5292 [4]=0 → 5071, [4]=2 → 5073 цветом −11514; готовность, счётчик и 2011 — как были (действие 1 выше меняет их — контроль)"),
				FString::Printf(TEXT("5071=%d 5073=%d счётчик %d→%d +4C4=%d"), b0 ? 1 : 0, b2 ? 1 : 0, Cnt, X->ReadyCounter, X->bMyLock));
		}
		// ═══ E. Партнёр готов первым: 5292 → счётчик 1 (2011 скрыт, 5046 нет); мой 2048 → счётчик 2, 2011, 5046 + 5069 ═══
		Clean(C);
		{
			OpenAsTarget(C, 0, 71);
			PutSingle(C, 2014, 2, 1);
			const int32 Seq0 = X->GetNoticeSeq();
			Feed(C, 5292, Body5292(1, 1, 20573, 1));
			const bool bOne = X->ReadyCounter == 1 && !X->DebugIsShown(TEXT("TRAN_OKBUT")) && X->DebugIsChecked(2049) && X->GetNoticeSeq() == Seq0;
			Caps.Reset();
			ToggleMine(C, true);
			Expect(bOne && Count(5291) == 1 && X->ReadyCounter == 2 && X->DebugIsShown(TEXT("TRAN_OKBUT")) && X->GetNoticeSeq() == Seq0 + 2 && X->GetLastNoticeLoc() == 5069,
				TEXT("E1 партнёр первым: счётчик 1, 2011 скрыт, нотисов нет; мой 2048 → 5291, счётчик 2, 2011, 5046 + 5069 (`exchangeOnLockToggleMsg`)"),
				FString::Printf(TEXT("после партнёра=%d 5291×%d счётчик=%d 2011=%d нотисов +%d"), bOne ? 1 : 0, Count(5291), X->ReadyCounter,
					X->DebugIsShown(TEXT("TRAN_OKBUT")) ? 1 : 0, X->GetNoticeSeq() - Seq0));
		}
		// ═══ F. [ПОРТ] 5291 / 5293 не ушли (нет связи) → готовность не взведена / +0x28 не взведён; с перехватом — ушли. Душа ═══
		Clean(C);
		{
			OpenAsTarget(C, 0, 72);
			PutSingle(C, 2014, 2, 1);
			Disarm(Net);
			ToggleMine(C, true);
			const bool bRolled = X->bMyLock == 0 && !X->bMyLockLatch && !X->DebugIsChecked(2048) && X->ReadyCounter == 0 && X->bLockOff == 1;
			Arm(Net); Caps.Reset();
			ToggleMine(C, true);
			Expect(bRolled && Count(5291) == 1 && X->bMyLock == 1, TEXT("F1 без связи 5291 не ушёл → готовность 0, латч снят, галка снята, счётчик 0; с перехватом — 5291 (контроль)"),
				FString::Printf(TEXT("откат=%d 5291×%d +4C4=%d"), bRolled ? 1 : 0, Count(5291), X->bMyLock));
			Feed(C, 5292, Body5292(1, 1, 20573, 1));
			Disarm(Net);
			ClickOk(C);
			const bool bNot = X->bCompleteSent == 0;
			Arm(Net); Caps.Reset();
			ClickOk(C);
			Expect(bNot && Count(5293) == 1 && X->bCompleteSent == 1, TEXT("F2 без связи 5293 не ушёл → +0x28 = 0 (повтор возможен); с перехватом — 5293, +0x28 = 1 (контроль)"),
				FString::Printf(TEXT("+0x28 без связи=%d 5293×%d"), X->bCompleteSent, Count(5293)));
			const uint8 SoulWas = Net->SelfSoulState;
			const bool bG91 = AsdaSoul::IsGatedOpcode(5291), bG93 = AsdaSoul::IsGatedOpcode(5293);
			Net->SetSelfSoulState(1); const bool bB1 = AsdaSoul::BlockOutgoing(Net, 5291) && AsdaSoul::BlockOutgoing(Net, 5293);
			Net->SetSelfSoulState(0); const bool bB0 = AsdaSoul::BlockOutgoing(Net, 5291) || AsdaSoul::BlockOutgoing(Net, 5293);
			Net->SetSelfSoulState(SoulWas);
			Expect(bG91 && bG93 && bB1 && !bB0, TEXT("F3 душа: 5291 и 5293 в таблице гейта (case 0x14AB/0x14AD под `sub_52EEA0`); блок при душе 1 / нет при 0"),
				FString::Printf(TEXT("5291=%d 5293=%d блок1=%d блок0=%d"), bG91 ? 1 : 0, bG93 ? 1 : 0, bB1 ? 1 : 0, bB0 ? 1 : 0));
		}
		// ═══ G. P44 «в полёте» — отказы штатными путями: 5288 → 5289 код 0 (5019 + 5290 + откат), 5291 → 5292 [4]=0 (5071) ═══
		Clean(C);
		{
			OpenAsTarget(C, 0, 73);
			Caps.Reset();
			X->OnCellPressed(2014); X->OnCellDoubleClick(2014);
			Feed(C, 5289, Body5289(0, 2, 1, 1));
			const bool b88 = X->GetLastNoticeLoc() == 5019 && Count(5290) == 1 && Same(Last(5290), Want5290(73, 2, 1, 1)) && TrayCode(0) == 0 && ListCode(0, 1) == 20572;
			PutSingle(C, 2014, 2, 1);
			ToggleMine(C, true);
			Feed(C, 5292, Body5292(0, 0));
			// ★ О4 (Д21, ключ 1): отказ фиксации до 5293 снимает готовность — 2048 снова свободна (ключ 0 — блок D).
			Expect(b88 && X->GetLastNoticeLoc() == 5071 && X->bMyLock == 0 && !X->bMyLockLatch,
				TEXT("G1 P44: 5289 код 0 → 5019, 5290 [#73, к=2 слот 1], кандидат вернулся; 5292 [4]=0 → 5071 (обработчики эталона), готовность снята (Д21, О4)"),
				FString::Printf(TEXT("5288-отказ=%d нотис %d +4C4=%d латч=%d"), b88 ? 1 : 0, X->GetLastNoticeLoc(), X->bMyLock, X->bMyLockLatch ? 1 : 0));
		}
		// ═══ H. Д18: две 5288 подряд → 5289 код 4 откатывает ПЕРВУЮ ячейку (ключ 1); ключ 0 — как эталон, вторую ═══
		for (int32 Key = 1; Key >= 0; --Key)
		{
			Clean(C);
			SetFix(Key);
			OpenAsTarget(C, 0, 74);
			Caps.Reset();
			X->OnCellPressed(2014); X->OnCellDoubleClick(2014);   // ячейка 0: 20572 (слот 1)
			X->OnCellPressed(2016); X->OnCellDoubleClick(2016);   // ячейка 1: 20626 (слот 3)
			const bool bTwo = Count(5288) == 2 && TrayCode(0) == 20572 && TrayCode(1) == 20626 && X->AddQueue.Num() == (Key ? 2 : 0);
			Feed(C, 5289, Body5289(4, 2, 1, 1));
			if (Key)
			{
				const bool bRoll = Same(Last(5290), Want5290(74, 2, 1, 1)) && TrayCode(0) == 0 && TrayCode(1) == 20626 && ListCode(0, 1) == 20572 && X->AddQueue.Num() == 1;
				Feed(C, 5289, Body5289(1, 2, 3, 1));
				Expect(bTwo && bRoll && TrayCode(1) == 20626 && X->AddQueue.Num() == 0 && X->DebugCellHasIcon(2038),
					TEXT("H1 [ПОРТ, Д18] две 5288 (ячейки 0 и 1) → 5289/4 откатывает ПЕРВУЮ: 5290 [к=2 слот 1], кандидат 20572 вернулся, ячейка 1 цела; следующий 5289/1 — ячейке 1"),
					FString::Printf(TEXT("две=%d откат=%d тело %s лоток0=%d лоток1=%d очередь=%d"), bTwo ? 1 : 0, bRoll ? 1 : 0, *Hex(Last(5290)), TrayCode(0), TrayCode(1), X->AddQueue.Num()));
			}
			else
			{
				Expect(bTwo && Same(Last(5290), Want5290(74, 2, 3, 1)) && TrayCode(1) == 0 && TrayCode(0) == 20572,
					TEXT("H2 ключ 0 (эталон): тот же 5289/4 откатывает ТЕКУЩУЮ ячейку 1 — 5290 [к=2 слот 3], ячейка 0 осталась (контроль к H1)"),
					FString::Printf(TEXT("тело %s лоток0=%d лоток1=%d"), *Hex(Last(5290)), TrayCode(0), TrayCode(1)));
			}
		}
		SetFix(1);
		// ═══ I. Д20: тип 1, отказ докладки золота → в списке весь запас (ключ 1); ключ 0 — копия до докладки ═══
		for (int32 Key = 1; Key >= 0; --Key)
		{
			Clean(C);
			SetFix(Key);
			OpenAsTarget(C, 1, 75);
			X->OnCellPressed(2015); X->OnCellDoubleClick(2015);
			TypeNumPad(WM, TEXT("400")); WM->CloseNumPad(true);
			Feed(C, 5289, Body5289(1, 2, 2, 400));
			X->OnCellPressed(2015); X->OnCellDoubleClick(2015);
			TypeNumPad(WM, TEXT("100")); WM->CloseNumPad(true);
			const bool bAdded = TrayCount(0) == 500 && ListCount(1, 2) == 500;
			Caps.Reset();
			Feed(C, 5289, Body5289(4, 2, 2, 100));
			const bool bSent90 = Same(Last(5290), Want5290(75, 2, 2, 500)) && TrayCode(0) == 0;
			if (Key)
			{
				Expect(bAdded && bSent90 && ListCount(1, 2) == 1000, TEXT("I1 [ПОРТ, Д20] тип 1: 400 принято, докладка 100 отклонена (5289/4) → 5290 вида 4 [к=2 слот 2, 500], в списке весь запас 1000"),
					FString::Printf(TEXT("докладка=%d 5290 %s список=%d"), bAdded ? 1 : 0, *Hex(Last(5290)), ListCount(1, 2)));
			}
			else
			{
				Expect(bAdded && bSent90 && ListCount(1, 2) == 600, TEXT("I2 ключ 0 (эталон): та же ветка → в списке копия до докладки 600 (400 «пропали» с экрана — контроль к I1)"),
					FString::Printf(TEXT("список=%d"), ListCount(1, 2)));
			}
		}
		SetFix(1);
		// ═══ J. Ревизия О2 МЕЛК-1: снятие без связи — лоток и список не меняются; с перехватом — 5290, ячейка пуста ═══
		Clean(C);
		{
			OpenAsTarget(C, 0, 76);
			PutSingle(C, 2014, 2, 1);
			Disarm(Net);
			X->OnCellPressed(2037); X->OnCellDoubleClick(2037);
			const bool bKept = TrayCode(0) == 20572 && ListCode(0, 1) == 0;
			Arm(Net); Caps.Reset();
			X->OnCellPressed(2037); X->OnCellDoubleClick(2037);
			Expect(bKept && Count(5290) == 1 && Same(Last(5290), Want5290(76, 2, 1, 1)) && TrayCode(0) == 0 && ListCode(0, 1) == 20572,
				TEXT("J1 [ПОРТ] 5290 не ушёл → ячейка лотка и список не тронуты; с перехватом — 5290 [к=2 слот 1], ячейка пуста, кандидат вернулся (контроль)"),
				FString::Printf(TEXT("без связи лоток0=%d список1=%d; 5290×%d лоток0=%d список1=%d"), bKept ? 20572 : TrayCode(0), bKept ? 0 : ListCode(0, 1),
					Count(5290), TrayCode(0), ListCode(0, 1)));
		}
		// ═══ K. `g_nPickedUserIdx` (ревизия О2 СРЕДН-2): пик себя (30 ур.) → 336 скрыт; пик партнёра (10 ур.) → виден; промах и цель меню — не влияют ═══
		{
			// «Специальное зелье здоровья» 336 — вторая таблица (item_list его не знает: коды предметов с 20551), категория 4.
			C.Inv->DebugPutItem(1, 2, 336, 1);
			Clean(C);
			C.PM->NotePickedUser(500);
			X->MenuTargetIdx = kPeer;   // цель меню — низкоуровневый партнёр: фильтр её НЕ читает
			OpenAsTarget(C, 1, 77);
			const bool bHidden = !HasCode(1, 336) && HasCode(1, 20626);
			Clean(C);
			C.PM->NotePickedUser(kPeer);
			OpenAsTarget(C, 1, 78);
			const bool bShown = HasCode(1, 336);
			Clean(C);
			C.PM->NotifyRightPress(-1);   // промах ПКМ (меню): пик не трогается
			C.PM->NotifyRightRelease(-1, false);
			OpenAsTarget(C, 1, 79);
			const bool bMiss = HasCode(1, 336) && C.PM->GetPickedUserIdx() == kPeer;
			Expect(bHidden && bShown && bMiss, TEXT("K1 фильтр типа 1 по `g_nPickedUserIdx`: пик себя (30 ур.) → 336 скрыт (цель меню 10 ур. не читается); пик партнёра (10 ур.) → виден; промах пик не меняет"),
				FString::Printf(TEXT("скрыт=%d виден=%d после промаха=%d пик=%d"), bHidden ? 1 : 0, bShown ? 1 : 0, bMiss ? 1 : 0, C.PM->GetPickedUserIdx()));
			C.Inv->DebugPutItem(1, 2, 0, 0);
		}
		// ═══ L. Смерть обеих сторон (ревизия О2 МЕЛК-3): цель ждёт 5287; по истечении — сама; инициатор — сразу; партнёр жив — сразу ═══
		{
			AAsdaPlayerCharacter* Peer = Cast<AAsdaPlayerCharacter>(C.WS->GetPlayerActorByUserNum(kPeer));
			Clean(C);
			OpenAsTarget(C, 0, 80);
			if (Peer) { Peer->SetAnimState(EAsdaPlayerState::Dead); }
			Net->bSelfDead = true; X->Tick(0.f);
			const bool bWait = Peer && Count(5286) == 0 && X->IsWindowShown() && X->DeathGraceStart >= 0.0;
			X->DeathGraceStart -= 4.0; X->Tick(0.f);
			const bool bLate = Count(5286) == 1 && Last(5286)->P[0] == 80 && !X->IsWindowShown();
			Net->bSelfDead = false; X->Tick(0.f);
			Clean(C);
			OpenAsTarget(C, 0, 81);
			X->Role = 1;   // сторона-инициатор
			Net->bSelfDead = true; X->Tick(0.f);
			const bool bInit = Count(5286) == 1 && Last(5286)->P[0] == 81;
			Net->bSelfDead = false; X->Tick(0.f);
			if (Peer) { Peer->SetAnimState(EAsdaPlayerState::Idle); }
			Clean(C);
			OpenAsTarget(C, 0, 82);
			Net->bSelfDead = true; X->Tick(0.f);
			const bool bAlive = Count(5286) == 1 && Last(5286)->P[0] == 82;
			Net->bSelfDead = false; X->Tick(0.f);
			Expect(bWait && bLate && bInit && bAlive,
				TEXT("L1 [ПОРТ] смерть обеих: цель — 5286 не сразу (ждёт 5287), через 3 с — сама [#80]; инициатор — сразу [#81]; партнёр жив — цель сразу [#82] (контроль)"),
				FString::Printf(TEXT("сосед=%d ждёт=%d поздно=%d инициатор=%d жив=%d"), Peer ? 1 : 0, bWait ? 1 : 0, bLate ? 1 : 0, bInit ? 1 : 0, bAlive ? 1 : 0));
		}
		// ═══ M. WBP окна не поднялся (ревизия О2 МЕЛК-4): 5286 + сброс модели; контроль — окно открывается ═══
		{
			Clean(C);
			X->bDebugFailEnsure = true;
			OpenAsTarget(C, 0, 83);
			X->bDebugFailEnsure = false;
			const bool bFail = Count(5286) == 1 && Last(5286)->P[0] == 83 && !X->IsWindowShown() && !UAsdaTradeStateSubsystem::IsExchangeWindowOpen(GI) && X->GetWindowIndex() == -1;
			Clean(C);
			OpenAsTarget(C, 0, 84);
			Expect(bFail && X->IsWindowShown() && UAsdaTradeStateSubsystem::IsExchangeWindowOpen(GI) && Count(5286) == 0,
				TEXT("M1 [ПОРТ] окно 2008 не поднялось → 5286 [#83], признак +0x2C не взведён, модель сброшена; штатно — окно открыто, 5286 нет (контроль)"),
				FString::Printf(TEXT("отказ=%d окно=%d"), bFail ? 1 : 0, X->IsWindowShown() ? 1 : 0));
		}
		// ═══ N. Нумпад (ревизия О2 МЕЛК-5): открытие не трогает +0x26D8; подсказка партнёра — самоцветы только у ItemRecord типа 1 (МЕЛК-7) ═══
		{
			X->SelectedQty = 77;
			X->OpenNumPad(5);
			const bool bQty = WM->IsNumPadOpen() && X->SelectedQty == 77;
			WM->CloseNumPad(false);
			Expect(bQty, TEXT("N1 открытие нумпада режима 3 кол-во (+0x26D8) не обнуляет (эталон пишет +0x26D4 у вызывающих)"),
				FString::Printf(TEXT("нумпад=%d кол-во=%d"), bQty ? 1 : 0, X->SelectedQty));
			Clean(C);
			OpenAsTarget(C, 1, 85);
			uint8* P = X->PartnerTray.GetData();
			AsdaBytes::WrI32(P, 20627); AsdaBytes::WrI16(P + 43, 111);                   // ячейка 0: вещь item_list, самоцвет 111
			AsdaBytes::WrI32(P + 51, 336); AsdaBytes::WrI16(P + 51 + 43, 222);           // ячейка 1: вторая таблица, «самоцвет» 222
			const TArray<uint8> R0 = X->BuildPartnerRec57(0), R1 = X->BuildPartnerRec57(1);
			Clean(C);
			OpenAsTarget(C, 0, 86);
			P = X->PartnerTray.GetData();
			AsdaBytes::WrI32(P, 20627); AsdaBytes::WrI16(P + 43, 111);
			const TArray<uint8> R2 = X->BuildPartnerRec57(0);
			Expect(AsdaBytes::RdI16(R0.GetData() + 16) == 111 && AsdaBytes::RdI16(R1.GetData() + 16) == 0 && AsdaBytes::RdI16(R2.GetData() + 16) == 0,
				TEXT("N2 подсказка лотка партнёра: самоцветы (+43 → +16) — только ItemRecord типа 1; вторая таблица и тип 0 — без них (контроль)"),
				FString::Printf(TEXT("тип1 вещь=%d тип1 таблица2=%d тип0=%d"), AsdaBytes::RdI16(R0.GetData() + 16), AsdaBytes::RdI16(R1.GetData() + 16), AsdaBytes::RdI16(R2.GetData() + 16)));
		}
		// ═══ O. `IsMsgBoxDisplayed` (ревизия О2 МЕЛК-9): QueueFixes 0 показывает ПЕРВУЮ запись, 1 — верхнюю ═══
		for (int32 Q = 0; Q <= 1; ++Q)
		{
			Clean(C);
			SetQueue(Q);
			WM->ShowGameConfirm(TEXT("Самотест О3: вопрос A"), [](bool) {}, 0.f, 5003, 5004, 5001, 5);
			WM->ShowGameConfirm(TEXT("Самотест О3: вопрос B"), [](bool) {}, 0.f, 2288, 2289, 7781, 1);
			WM->ShowGameConfirm(TEXT("Самотест О3: вопрос C"), [](bool) {}, 0.f, 2288, 2289, 7782, 1);
			WM->AnswerMsgBox(true, false);   // снять C
			const bool bDisp = WM->IsMsgBoxDisplayed(5, 5001), bTop = WM->IsMsgBoxTop(5, 5001);
			if (Q == 0)
			{
				Expect(bDisp && !bTop, TEXT("O1 QueueFixes 0: после ответа на C на экране ПЕРВАЯ запись (A, 5001) — `IsMsgBoxDisplayed` да, `IsMsgBoxTop` нет"),
					FString::Printf(TEXT("показана=%d верхняя=%d"), bDisp ? 1 : 0, bTop ? 1 : 0));
			}
			else
			{
				Expect(!bDisp && !bTop && WM->IsMsgBoxDisplayed(1, 7781), TEXT("O2 QueueFixes 1: на экране верхняя (B) — наш вопрос не показан (контроль к O1)"),
					FString::Printf(TEXT("показана=%d верхняя=%d B=%d"), bDisp ? 1 : 0, bTop ? 1 : 0, WM->IsMsgBoxDisplayed(1, 7781) ? 1 : 0));
			}
			WM->ClearMsgBoxQueue(TEXT("самотест О3"));
		}
		SetQueue(QueueFixWas);
		// ── окно на фазу 2: тип 0, открыто, в лотке одиночка ──
		Clean(C);
		OpenAsTarget(C, 0, 90);
		PutSingle(C, 2014, 2, 1);
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
		using namespace AsdaP2PTestO3Local;
		// 1. Клик по флажку 2048 (живой).
		Steps.Add([this](UWorld* W) { Click(W, TEXT("TRAN_BUT01")); });
		// 2. Проверка: 5291, галка, готовность. Клик по 2049 (HitTestInvisible).
		Steps.Add([this](UWorld* W)
		{
			const FCtx C = Ctx(W); if (!C.Valid()) { return; }
			Expect(Count(5291) == 1 && Same(Last(5291), Want5291(90, 1, 2, 1, 1)) && C.X->bMyLock == 1 && C.X->DebugIsChecked(2048),
				TEXT("путь игрока: клик по флажку 2048 → 5291 [#90, lock 1], галка, готовность"),
				FString::Printf(TEXT("5291×%d тело %s +4C4=%d галка=%d"), Count(5291), *Hex(Last(5291)), C.X->bMyLock, C.X->DebugIsChecked(2048) ? 1 : 0));
			Click(W, TEXT("TRAN_BUT02"));
		});
		// 3. Проверка 2049: ничего. Партнёр готов (5292) — 2011 появляется.
		Steps.Add([this](UWorld* W)
		{
			const FCtx C = Ctx(W); if (!C.Valid()) { return; }
			Expect(!C.X->DebugIsChecked(2049) && Caps.Num() == 1, TEXT("путь игрока: клик по 2049 (индикатор партнёра, HitTestInvisible) — галки нет, пакетов нет (контроль к 2048)"),
				FString::Printf(TEXT("2049=%d пакетов %d"), C.X->DebugIsChecked(2049) ? 1 : 0, Caps.Num()));
			Feed(C, 5292, Body5292(1, 1, 20573, 1));
		});
		// 4. Геометрия 2011 против `.JUI` (12,365) 100×23; клик по 2011.
		Steps.Add([this](UWorld* W)
		{
			const FCtx C = Ctx(W); if (!C.Valid()) { return; }
			UWidget* Corner = C.X->DebugNamedWidget(TEXT("TRAN_WIN_0_0"));
			UWidget* Cell0 = C.X->DebugCellWidget(2013);
			UWidget* Ok = C.X->DebugNamedWidget(TEXT("TRAN_OKBUT"));
			FVector2D CMn = FVector2D::ZeroVector, CMx = FVector2D::ZeroVector, EMn = FVector2D::ZeroVector, EMx = FVector2D::ZeroVector;
			FVector2D OMn = FVector2D::ZeroVector, OMx = FVector2D::ZeroVector;
			const bool bG = Corner && Cell0 && Ok && AsdaUi::WidgetAbsRect(Corner, CMn, CMx) && AsdaUi::WidgetAbsRect(Cell0, EMn, EMx) && AsdaUi::WidgetAbsRect(Ok, OMn, OMx);
			const float Scale = bG ? (float)(EMx.X - EMn.X) / 32.f : 0.f;
			const float Rx = bG ? (float)(OMn.X - CMn.X) / Scale : -1.f, Ry = bG ? (float)(OMn.Y - CMn.Y) / Scale : -1.f;
			const float Wd = bG ? (float)(OMx.X - OMn.X) / Scale : -1.f, Ht = bG ? (float)(OMx.Y - OMn.Y) / Scale : -1.f;
			Expect(bG && C.X->DebugIsShown(TEXT("TRAN_OKBUT")) && FMath::Abs(Rx - 12.f) <= 1.5f && FMath::Abs(Ry - 365.f) <= 1.5f && FMath::Abs(Wd - 100.f) <= 1.5f && FMath::Abs(Ht - 23.f) <= 1.5f,
				TEXT("путь игрока: 2011 виден после 5292 и стоит по `.JUI` (12,365) 100×23 ±1,5"),
				FString::Printf(TEXT("масштаб %.3f, (%.1f,%.1f) %.1f×%.1f"), Scale, Rx, Ry, Wd, Ht));
			Click(W, TEXT("TRAN_OKBUT"));
		});
		// 5. Проверка 5293; повторный клик по запертому 2048.
		Steps.Add([this](UWorld* W)
		{
			const FCtx C = Ctx(W); if (!C.Valid()) { return; }
			Expect(Count(5293) == 1 && Same(Last(5293), Want5293(90)) && C.X->bCompleteSent == 1, TEXT("путь игрока: клик по 2011 → 5293 [#90], +0x28 = 1"),
				FString::Printf(TEXT("5293×%d тело %s"), Count(5293), *Hex(Last(5293))));
			Click(W, TEXT("TRAN_BUT01"));
		});
		// 6. Проверка: латч держит галку. Новое окно без готовности; первое нажатие по кандидату 2017 (стопка) — одиночный клик.
		Steps.Add([this](UWorld* W)
		{
			const FCtx C = Ctx(W); if (!C.Valid()) { return; }
			Expect(C.X->DebugIsChecked(2048) && Count(5291) == 1, TEXT("путь игрока: клик по запертому 2048 — галка держится, второго 5291 нет"),
				FString::Printf(TEXT("галка=%d 5291×%d"), C.X->DebugIsChecked(2048) ? 1 : 0, Count(5291)));
			Clean(C);
			OpenAsTarget(C, 0, 91);
			Caps.Reset();
		});
		// 7. Первое нажатие по одиночке 2014.
		Steps.Add([this](UWorld* W) { Click(W, nullptr, 2014); });
		// 8. Проверка одиночного (5288 нет, выбор есть) → второе нажатие по 2014.
		Steps.Add([this](UWorld* W)
		{
			const FCtx C = Ctx(W); if (!C.Valid()) { return; }
			Expect(Count(5288) == 0 && C.X->bCellTaken == 1 && C.X->SelSlotIndex == 1,
				TEXT("путь игрока: одиночный щелчок по 2014 — выбор (+0x10 = 1), 5288 нет (контроль к двойному)"),
				FString::Printf(TEXT("5288×%d +0x10=%d выбор=%d"), Count(5288), C.X->bCellTaken, C.X->SelSlotIndex));
			Click(W, nullptr, 2014);
		});
		// 9. Проверка двойного на втором НАЖАТИИ.
		Steps.Add([this](UWorld* W)
		{
			const FCtx C = Ctx(W); if (!C.Valid()) { return; }
#if PLATFORM_WINDOWS
			const uint32 Dbl = ::GetDoubleClickTime();
#else
			const uint32 Dbl = 500;
#endif
			Expect(Count(5288) == 1 && Same(Last(5288), Want5288(91, 0, 2, 1, 1)), TEXT("путь игрока: второе нажатие по 2014 в окне `GetDoubleClickTime` → 0xD → 5288 [#91, ячейка 0, к=2 слот 1, 1]"),
				FString::Printf(TEXT("5288×%d тело %s (окно двойного %u мс)"), Count(5288), *Hex(Last(5288)), Dbl));
		});
	}

	// Восстановление всего, что трогал тест.
	void Restore(UWorld* W)
	{
		const FCtx C = Ctx(W);
		if (C.X && C.WM) { C.X->ResetSessionState(EAsdaSessionResetReason::LeftWorld); C.WM->ClearMsgBoxQueue(TEXT("самотест О3")); }
		if (C.Net)
		{
			Disarm(C.Net);
			C.Net->SelfUserNum = SelfNumWas; C.Net->bSelfUserNumKnown = bSelfKnownWas; C.Net->SelfLevel = LevelWas; C.Net->bSelfDead = false;
		}
		if (C.Inv)
		{
			for (int32 i = 0; i < Cont2Was.Num(); ++i) { if (FAsdaItemInstance* Cl = C.Inv->GetCellMutable(2, i)) { *Cl = Cont2Was[i]; } }
			for (int32 i = 0; i < Cont1Was.Num(); ++i) { if (FAsdaItemInstance* Cl = C.Inv->GetCellMutable(1, i)) { *Cl = Cont1Was[i]; } }
		}
		if (C.PM) { C.PM->NotePickedUser(PickWas); }
		// ★ ИСПРАВЛЕНИЕ 11.10.2026 (В0, N1(д)/U8): ключи прогона (Trade.P2PFixDefects, Trade.P2PCloseOnDeath, MsgBox.QueueFixes) — к исходному значению
		//   И приоритету SetBy (прежде — прежние значения, но с SetByConsole).
		AsdaCVarSave::RestoreAll(TEXT("asda.Trade.SelfTestExchangeO3"));
	}
};

namespace
{
	FAutoConsoleCommandWithWorldAndArgs GAsdaTradeSelfTestExchangeO3(
		TEXT("asda.Trade.SelfTestExchangeO3"),
		TEXT("asda.Trade.SelfTestExchangeO3 [delay=N] [quit] — самотест обмена, волна О3: готовность 2048 → 5291, приём 5292 (лоток партнёра, 2049, "
		     "счётчик, 2011, 5046/5069, отказы 5071/5073), итог 2011 → 5293, душа 5291/5293, неотправленные пакеты, P44-отказы, исправления "
		     "ревизии О2 (Д18, Д20, g_nPickedUserIdx, снятие без связи, двойной щелчок на нажатии, окно не поднялось, нумпад, самоцветы, "
		     "IsMsgBoxDisplayed, смерть обеих сторон); фаза путём игрока — клики Slate по живой геометрии. Только офлайн."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
		{
			const FAsdaSelfTestArgs KitArgs = FAsdaSelfTestArgs::Parse(Args);   // ★ 11.10.2026 (В3 S1): каркас — отказ с ИТОГ и quit, второй барьер на все опкоды
			if (AsdaSelfTestKit::RefuseStart(W, TEXT("asda.Trade.SelfTestExchangeO3"), KitArgs)) { return; }
			const float Delay = AsdaSelfTestKit::DelayArg(Args);
			const bool bQuit = Args.Contains(TEXT("quit"));
			TWeakObjectPtr<UWorld> WW(W);
			// Офлайн своего павна нет: офлайн-спавн штатными крюками (как О1/О2), логика — через 2 с; шаги пути игрока — по кадрам
			//   (см. планировщик ниже: клику нужно 2 кадра удержания + кадр опроса окна; два шага подряд — в окне двойного щелчка).
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WW, bQuit](float) -> bool
			{
				UWorld* Wl = WW.Get();
				if (!Wl) { return false; }
				if (AsdaSelfTestKit::RefuseDeferred(Wl, TEXT("asda.Trade.SelfTestExchangeO3"), bQuit)) { return false; }   // Р-2: офлайн повторно — на отложенном старте
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
					TSharedRef<FAsdaExchangeP2PSelfTestO3> T = MakeShared<FAsdaExchangeP2PSelfTestO3>();
					if (!T->RunPhase1(W2))
					{
						AsdaSelfTestKit::Quit(W2, bQuit);
						return false;
					}
					T->BuildInputPhase();
					// Шаги идут по КАДРАМ, а не по секундам: в начале прогона кадры длятся секунды (компиляция шейдеров), и клик
					//   (нажатие → 2 кадра удержания → отпускание → кадр опроса окна) не успевал бы между шагами. Сначала ждём
					//   20 «быстрых» кадров подряд (< 0,1 с), затем шаг — не чаще раза в 8 кадров (≈ 0,15–0,3 с — в окне двойного щелчка).
					struct FSched { int32 Idx = 0; int32 Fast = 0; uint64 LastFrame = 0; };
					TSharedRef<FSched> Sch = MakeShared<FSched>();
					FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WW, T, Sch, bQuit](float Dt) -> bool
					{
						UWorld* W3 = WW.Get();
						if (!W3) { AsdaCVarSave::RestoreAll(TEXT("asda.Trade.SelfTestExchangeO3")); return false; }   // ★ ИСПРАВЛЕНИЕ 11.10.2026 (В0, N1(д)/U8): ключи — и при уходе мира
						if (Sch->Fast < 20) { Sch->Fast = (Dt < 0.1f) ? Sch->Fast + 1 : 0; return true; }
						if (GFrameCounter - Sch->LastFrame < 8) { return true; }
						Sch->LastFrame = GFrameCounter;
						if (Sch->Idx < T->Steps.Num()) { T->Steps[Sch->Idx++](W3); return true; }
						T->Restore(W3);
						UE_LOG(LogAsdaP2PTestO3, Log, TEXT("САМОТЕСТ-О3 ИТОГ: ok=%d fail=%d"), T->Ok, T->Fail);
						AsdaSelfTestKit::Quit(W3, bQuit);
						return false;
					}), 0.f);
					return false;
				}), bNeedSpawn ? 2.f : 0.f);
				return false;
			}), Delay);
			UE_LOG(LogAsdaP2PTestO3, Log, TEXT("САМОТЕСТ-О3: запуск через %.1f с"), Delay);
		}));
}

#endif // !UE_BUILD_SHIPPING
