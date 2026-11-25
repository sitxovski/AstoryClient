// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ ОКНА ОБМЕНА `TRAN_WIN` 2008, ВОЛНА О2 (29.09.2026, `Reports/trade/wave_O2.md`). [ПОРТ]
//
// ЧТО ПРОВЕРЯЕТ. Окно и модель списков/лотков `UAsdaExchangeP2PSubsystem` БОЕВЫМИ путями: входящие 5283/5285/5287/5289 — через
//   штатный `UAsdaNetSubsystem::DebugFeedPacket` → `HandlePacket` → `Tick` подсистемы; исходящие 5284/5286/5288/5290 — БОЕВЫЕ
//   отправители под перехватом (`ArmSendCapture`, в сеть ничего не уходит), тела сверяются побайтно. События окна — те же
//   методы, которые зовёт опрос мыши (`OnCellPressed` = 0xB, `OnDragBegin` = 5, `OnDrop` = 7, `OnCellDoubleClick` = 0xD,
//   `OnWheel` = 23), кнопка «Отмена» 2012 — своим `OnClicked`, нумпад — полем и `CloseNumPad`. Геометрия — ПУТЁМ ИГРОКА:
//   во второй фазе (через кадры) живые прямоугольники Slate (`GetCachedGeometry`) ячеек/кнопок сверяются с `.JUI`, а хит-тест
//   окна (`HitTestCellAt`) — с центрами ячеек.
//   У каждой проверки — КОНТРОЛЬНАЯ ПАРА (закон pixel-metric-needs-control-pairs).
//
// Консоль: `asda.Trade.SelfTestExchangeO2 [delay=N] [quit]`. Только офлайн. Строки — «САМОТЕСТ-О2», итог — «САМОТЕСТ-О2 ИТОГ».
//   Свой userNum/уровень, инвентарь, ключи, перехватчик, состояние смерти и эффекты восстанавливаются в конце.
#include "CoreMinimal.h"
#include "AsdaTestPacketsP2P.h"   // ★ R5-c (12.11.2026): общая точка самотестов вместо своих копий помощников

#if !UE_BUILD_SHIPPING

#include "UI/AsdaExchangeP2PSubsystem.h"
#include "AsdaCVarSave.h"                   // ★ ИСПРАВЛЕНИЕ 11.10.2026 (В0, N1(д)/U8): ключи консоли самотеста — запомнить и вернуть (значение + SetBy)
#include "UI/AsdaTradeStateSubsystem.h"
#include "UI/AsdaHotbarSubsystem.h"
#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaInventorySubsystem.h"
#include "UI/AsdaItemTooltipSubsystem.h"
#include "UI/AsdaTipPanel.h"
#include "UI/AsdaChatSubsystem.h"
#include "UI/AsdaEffectsSubsystem.h"
#include "UI/AsdaNotice.h"
#include "UI/AsdaUiHelpers.h"
#include "World/AsdaWorldSubsystem.h"
#include "World/AsdaPlayerController.h"
#include "World/AsdaPossessGate.h"
#include "Login/AsdaLoginData.h"
#include "Test/AsdaSelfTestNet.h"   // ★ R1: тонкий доступ самотестов к сети (вместо AsdaNetSubsystem.h)
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
#include "Misc/OutputDevice.h"
#include "Misc/OutputDeviceRedirector.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaP2PTestO2, Log, All);

namespace AsdaP2PTestO2Local
{
	// Перехватчик лога: строки веток подсистем (гейт лопат, 5042 хотбара).
	class FSink : public FOutputDevice
	{
	public:
		TArray<FString> All;    // все строки
		FCriticalSection Lock;  // лог может прийти с любого потока
		virtual void Serialize(const TCHAR* V, ELogVerbosity::Type, const FName&) override
		{
			if (!V) { return; }
			FScopeLock L(&Lock);
			All.Add(V);
		}
		virtual bool CanBeUsedOnAnyThread() const override { return true; }
		virtual bool CanBeUsedOnMultipleThreads() const override { return true; }   // ★ 05.11.2026: НЕбуферизованный список UE 5.8 — строка доходит в момент записи (иначе гонка с потоком логов: строка после RemoveOutputDevice/проверки)
		// Очистка (перед ней — доставка фонового лога).
		void Reset() { if (GLog) { GLog->Flush(); } FScopeLock L(&Lock); All.Reset(); }
		// Сколько строк содержат подстроку.
		int32 Hits(const TCHAR* Needle)
		{
			if (GLog) { GLog->Flush(); }
			FScopeLock L(&Lock);
			int32 N = 0;
			for (const FString& S : All) { if (S.Contains(Needle)) { ++N; } }
			return N;
		}
	};

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
	// Ожидаемое тело 5288 (12 б).
	using AsdaTestPacketsP2P::Want5288;   // ★ R5-c (12.11.2026): общая точка (`AsdaTestPacketsP2P.h`) — копия была дословной.
	// Ожидаемое тело 5290 (11 б).
	using AsdaTestPacketsP2P::Want5290;   // ★ R5-c (12.11.2026): общая точка (`AsdaTestPacketsP2P.h`) — копия была дословной.
}

struct FAsdaExchangeP2PSelfTestO2
{
	int32 Ok = 0;     // пройдено
	int32 Fail = 0;   // провалено
	TArray<AsdaP2PTestO2Local::FCap> Caps;   // пойманные исходящие

	// Одна проверка: «САМОТЕСТ-О2 ok/ПРОВАЛ: <имя> — <подробности>».
	void Expect(bool bCond, const FString& Name, const FString& Detail)
	{
		if (bCond) { ++Ok; } else { ++Fail; }
		UE_LOG(LogAsdaP2PTestO2, Log, TEXT("САМОТЕСТ-О2 %s: %s — %s"), bCond ? TEXT("ok") : TEXT("ПРОВАЛ"), *Name, *Detail);
	}
	// Сколько пойманных пакетов с опкодом.
	int32 Count(uint16 Op) const { int32 N = 0; for (const auto& C : Caps) { if (C.Op == Op) { ++N; } } return N; }
	// Последний пойманный пакет с опкодом (nullptr — нет).
	const AsdaP2PTestO2Local::FCap* Last(uint16 Op) const { for (int32 i = Caps.Num() - 1; i >= 0; --i) { if (Caps[i].Op == Op) { return &Caps[i]; } } return nullptr; }
	// Hex payload для подробностей.
	static FString Hex(const AsdaP2PTestO2Local::FCap* C)
	{
		if (!C) { return TEXT("—"); }
		FString S; for (uint8 B : C->P) { S += FString::Printf(TEXT("%02X"), B); }
		return S;
	}
	static FString HexA(const TArray<uint8>& A) { FString S; for (uint8 B : A) { S += FString::Printf(TEXT("%02X"), B); } return S; }
	// Payload побайтно равен ожидаемому.
	static bool Same(const AsdaP2PTestO2Local::FCap* C, const TArray<uint8>& Want) { return C && C->P == Want; }

	// Состояние, которое надо вернуть после второй фазы.
	uint16 SelfNumWas = 0; bool bSelfKnownWas = false; uint8 LevelWas = 0; uint32 Gen = 0;
	TArray<FAsdaItemInstance> Cont1Was, Cont2Was;   // инвентарь до теста
	AsdaP2PTestO2Local::FSink Sink;                  // перехват лога

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

	// Фаза 1: вся логика окна (синхронно). Возврат — false, если нечего проверять (нет подсистем).
	bool RunPhase1(UWorld* W)
	{
		using namespace AsdaP2PTestO2Local;
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaExchangeP2PSubsystem* X = GI ? GI->GetSubsystem<UAsdaExchangeP2PSubsystem>() : nullptr;
		UAsdaNetSubsystem* Net = GI ? AsdaTestNet::Get(GI) : nullptr;
		UAsdaTradeStateSubsystem* TS = GI ? GI->GetSubsystem<UAsdaTradeStateSubsystem>() : nullptr;
		UAsdaHotbarSubsystem* Hb = GI ? GI->GetSubsystem<UAsdaHotbarSubsystem>() : nullptr;
		UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
		UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
		UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
		UAsdaItemTooltipSubsystem* Tip = GI ? GI->GetSubsystem<UAsdaItemTooltipSubsystem>() : nullptr;
		UAsdaChatSubsystem* Chat = GI ? GI->GetSubsystem<UAsdaChatSubsystem>() : nullptr;
		UAsdaEffectsSubsystem* Eff = GI ? GI->GetSubsystem<UAsdaEffectsSubsystem>() : nullptr;
		if (!X || !Net || !TS || !Hb || !WM || !WS || !Inv || !Tip || !Chat || !Eff)
		{
			UE_LOG(LogAsdaP2PTestO2, Error, TEXT("САМОТЕСТ-О2 ИТОГ: ok=0 fail=1 (нет подсистем)"));
			return false;
		}
		GLog->AddOutputDevice(&Sink);
		// ── подготовка: свой userNum 500, уровень 30, соседи, инвентарь, ключи, перехват ──
		SelfNumWas = AsdaTestNet::SelfUserNum(Net); bSelfKnownWas = AsdaTestNet::bSelfUserNumKnown(Net); LevelWas = AsdaTestNet::SelfLevel(Net);
		AsdaTestNet::SelfUserNum(Net) = 500; AsdaTestNet::bSelfUserNumKnown(Net) = true; AsdaTestNet::SelfLevel(Net) = 30;
		constexpr int32 kPeer = 902;
		WS->DebugSpawnFakePeer(kPeer, TEXT("Партнёр"), FVector2D(25710.f, 25700.f));
		Hb->DumpNoticeIcons();
		IConsoleVariable* CvFix = IConsoleManager::Get().FindConsoleVariable(TEXT("asda.Trade.P2PFixDefects"));
		IConsoleVariable* CvDeath = IConsoleManager::Get().FindConsoleVariable(TEXT("asda.Trade.P2PCloseOnDeath"));
		// ★ ИСПРАВЛЕНИЕ 11.10.2026 (В0, N1(д)/U8): исходные P2PFixDefects/P2PCloseOnDeath (значение + SetBy) запоминает `AsdaCVarSave` (прежде — поля FixWas/DeathWas).
		auto SetFix = [CvFix](int32 V) { if (CvFix) { AsdaCVarSave::SetForTest(TEXT("asda.Trade.P2PFixDefects"), V); } };
		auto SetDeath = [CvDeath](int32 V) { if (CvDeath) { AsdaCVarSave::SetForTest(TEXT("asda.Trade.P2PCloseOnDeath"), V); } };
		SetFix(1); SetDeath(1);
		for (int32 i = 0; i < 60; ++i) { if (const FAsdaItemInstance* C = Inv->GetCell(2, i)) { Cont2Was.Add(*C); } }
		for (int32 i = 0; i < UAsdaInventorySubsystem::kShellSlots; ++i) { if (const FAsdaItemInstance* C = Inv->GetCell(1, i)) { Cont1Was.Add(*C); } }   // ★ 29.10.2026 (инвентарь ×2): вся сумка 60
		for (int32 i = 0; i < 60; ++i) { Inv->DebugPutItem(2, i, 0, 0); }
		for (int32 i = 0; i < UAsdaInventorySubsystem::kShellSlots; ++i) { Inv->DebugPutItem(1, i, 0, 0); }   // ★ 29.10.2026 (инвентарь ×2): вся сумка 60
		// Контейнер 2: стопка 5, одиночка, золото 1000, «снаряжение» (enchantOrSocket 0 → 5014 при переносе), 26 стопок
		//   с РАЗНЫМ кол-вом (i + 2) — надписи страниц различимы (колесо, Д17). Всего кандидатов 30 (> 24 — колесо).
		Inv->DebugPutItem(2, 0, 20571, 5);
		Inv->DebugPutItem(2, 1, 20572, 1);
		Inv->DebugPutItem(2, 2, 20551, 1000);
		Inv->DebugPutItem(2, 3, 20626, 1);
		static const int32 kCodes[] = { 20573, 20577, 20578, 20579, 20583, 20586, 20587, 20588, 20589, 20590 };
		for (int32 i = 4; i < 30; ++i) { Inv->DebugPutItem(2, i, kCodes[(i - 4) % UE_ARRAY_COUNT(kCodes)], i + 2); }
		// Контейнер 1: две вещи категории 3 (тип 1).
		Inv->DebugPutItem(1, 0, 20626, 1);
		Inv->DebugPutItem(1, 1, 20627, 1);
		Gen = AsdaSelfTestKit::ArmCapture(Net, [this](uint16 Op, const uint8* P, int32 Len, bool, TCHAR)
		{
			FCap& C = Caps.AddDefaulted_GetRef();
			C.Op = Op;
			C.P.Append(P, Len);
		}, 0, TEXT("asda.Trade.SelfTestExchangeO2"));
		auto Clean = [&]()
		{
			X->ResetSessionState(EAsdaSessionResetReason::LeftWorld);
			WM->ClearMsgBoxQueue(TEXT("самотест О2"));
			if (WM->IsNumPadOpen()) { WM->CloseNumPad(false); }
			Caps.Reset();
			Sink.Reset();
		};
		auto Feed = [&](int32 Op, const TArray<uint8>& B) { AsdaTestNet::DebugFeedPacket(Net, Op, B); X->Tick(0.f); };
		auto Loc = [GI](int32 Id) { return AsdaNotice::Loc(GI, Id); };
		// Открыть окно как ЦЕЛЬ (заявка → значок → «Принять» → 5285/1) типа Type с индексом окна Idx.
		auto OpenAsTarget = [&](int32 Type, int32 Idx)
		{
			Feed(5283, Body5283(1, kPeer, TEXT("Партнёр"), Type));
			Hb->HandleNoticeIconClick(Type ? 5 : 4);
			WM->AnswerMsgBox(true, true);
			Feed(5285, Body5285(1, Type, Idx, kPeer, TEXT("Партнёр"), 500, TEXT("Я")));
		};
		auto ListCode = [X](int32 Type, int32 Idx) { const uint8* R = X->ListRec(Type, Idx); return R ? AsdaBytes::RdI32(R) : 0; };
		auto ListCount = [X](int32 Type, int32 Idx) { const uint8* R = X->ListRec(Type, Idx); return R ? AsdaBytes::RdI32(R + 9) : 0; };
		auto TrayCode = [X](int32 K) { const uint8* R = X->TrayRec(K); return R ? AsdaBytes::RdI32(R) : 0; };
		auto TrayCount = [X](int32 K) { const uint8* R = X->TrayRec(K); return R ? AsdaBytes::RdI32(R + 9) : 0; };

		// ═══ A. Открытие типа 0: окно видно, разбор 47 привязок, подписи BuildUI, страница списка; до 5285 — окна нет ═══
		Clean();
		{
			const bool bShown0 = X->IsWindowShown();
			OpenAsTarget(0, 77);
			Expect(!bShown0 && X->IsWindowShown() && UAsdaTradeStateSubsystem::IsExchangeWindowOpen(GI) && WM->IsWindowVisible(FName(TEXT("TRAN_WIN")))
				&& X->ListCount == 30 && X->GetFoundControls() == 47 && X->GetWindowIndex() == 77,
				TEXT("A1 5285/1 тип 0 → окно 2008 видно, 47 привязок, 30 кандидатов (до 5285 окна нет — контроль)"),
				FString::Printf(TEXT("до=%d после=%d признак=%d реестр=%d кандидатов=%d привязок=%d окно#=%d"), bShown0 ? 1 : 0, X->IsWindowShown() ? 1 : 0,
					UAsdaTradeStateSubsystem::IsExchangeWindowOpen(GI) ? 1 : 0, WM->IsWindowVisible(FName(TEXT("TRAN_WIN"))) ? 1 : 0, X->ListCount, X->GetFoundControls(), X->GetWindowIndex()));
			const bool bLbl = X->DebugWidgetText(TEXT("TRAN_CANCELBUT_Text")) == Loc(2289) && X->DebugWidgetText(TEXT("Tran_GuideTxt")) == Loc(2379)
				&& X->DebugWidgetText(TEXT("Tran_SlotWin01_Tab_Text")) == Loc(13245) && X->DebugWidgetText(TEXT("Tran_SlotWin02_Tab_Text")) == Loc(13246)
				&& X->DebugWidgetText(TEXT("TRAN_BUT01_Text")) == Loc(13244) && X->DebugWidgetText(TEXT("TRAN_BUT02_Text")) == Loc(13244)
				&& X->DebugWidgetText(TEXT("TRAN_OKBUT_Text")) == Loc(6805);
			Expect(bLbl && !X->DebugIsShown(TEXT("TRAN_OKBUT")) && X->DebugIsShown(TEXT("TRAN_CANCELBUT")) && X->DebugIsShown(TEXT("Tran_Gaer_1_0"))
				&& !X->DebugIsShown(TEXT("Tran_AniGaer_2_0")) && !X->DebugIsShown(TEXT("TRAN_TRAYSEL_108_0")),
				TEXT("A2 подписи BuildUI (2289/2379/13245/13246/13244×2/6805); 2011 скрыт, 2012 виден; шестерня 2058 видна, 2059 и рамка 2047 скрыты"),
				FString::Printf(TEXT("подписи=%d «%s» 2011=%d 2012=%d 2058=%d 2059=%d 2047=%d"), bLbl ? 1 : 0, *X->DebugWidgetText(TEXT("Tran_GuideTxt")),
					X->DebugIsShown(TEXT("TRAN_OKBUT")) ? 1 : 0, X->DebugIsShown(TEXT("TRAN_CANCELBUT")) ? 1 : 0, X->DebugIsShown(TEXT("Tran_Gaer_1_0")) ? 1 : 0,
					X->DebugIsShown(TEXT("Tran_AniGaer_2_0")) ? 1 : 0, X->DebugIsShown(TEXT("TRAN_TRAYSEL_108_0")) ? 1 : 0));
			Expect(X->DebugCellHasIcon(2013) && X->DebugCellText(2013) == TEXT("5") && X->DebugCellHasIcon(2014) && X->DebugCellText(2014) == TEXT("1")
				&& X->DebugCellHasIcon(2015) && X->DebugCellText(2015).IsEmpty() && X->DebugCellHasIcon(2016) && X->DebugCellText(2016).IsEmpty()
				&& X->DebugCellText(2017) == TEXT("6") && !X->DebugCellHasIcon(2037) && !X->DebugCellHasIcon(2042),
				TEXT("A3 страница: стопка «5», одиночка «1», золото и вещь без надписи (tradable 0 / 20551), лотки пусты"),
				FString::Printf(TEXT("2013 %d «%s», 2014 %d «%s», 2015 %d «%s», 2016 %d «%s», 2017 «%s», 2037 %d, 2042 %d"),
					X->DebugCellHasIcon(2013) ? 1 : 0, *X->DebugCellText(2013), X->DebugCellHasIcon(2014) ? 1 : 0, *X->DebugCellText(2014),
					X->DebugCellHasIcon(2015) ? 1 : 0, *X->DebugCellText(2015), X->DebugCellHasIcon(2016) ? 1 : 0, *X->DebugCellText(2016),
					*X->DebugCellText(2017), X->DebugCellHasIcon(2037) ? 1 : 0, X->DebugCellHasIcon(2042) ? 1 : 0));
			const UCheckBox* C1 = Cast<UCheckBox>(X->DebugNamedWidget(TEXT("TRAN_BUT01")));
			const UCheckBox* C2 = Cast<UCheckBox>(X->DebugNamedWidget(TEXT("TRAN_BUT02")));
			Expect(C1 && C2 && C1->GetVisibility() == ESlateVisibility::Visible && C2->GetVisibility() == ESlateVisibility::HitTestInvisible && !C1->IsChecked() && !C2->IsChecked(),
				TEXT("A4 2048 кликабелен, 2049 HitTestInvisible (ассет, Д-W0-1), галки сняты"),
				FString::Printf(TEXT("2048 вид=%d 2049 вид=%d"), C1 ? (int32)C1->GetVisibility() : -1, C2 ? (int32)C2->GetVisibility() : -1));
		}
		// ═══ B. 0xB + 0xD по одиночке → 5288 (1 шт.), список до 5289 не перерисован; 5289/1 → лоток «1», кандидат пуст ═══
		{
			Caps.Reset();
			X->OnCellPressed(2014);
			const bool bSel = X->bCellTaken == 1 && X->SelSlotIndex == 1 && X->SelListCell == 1 && X->DebugIsShown(TEXT("TRAN_TRAYSEL_108_0"));
			X->OnCellDoubleClick(2014);
			const FCap* C = Last(5288);
			const bool bBefore = X->DebugCellHasIcon(2014);   // эталон перерисует список только по 5289
			Expect(bSel && Count(5288) == 1 && Same(C, Want5288(77, 0, 2, 1, 1)) && TrayCode(0) == 20572 && ListCode(0, 1) == 0 && bBefore,
				TEXT("B1 выбор 2014 (рамка 2047) + двойной ЛКМ → 5288 [#77, ячейка 0, к=2, слот 1, 1 шт.]; до 5289 список не перерисован"),
				FString::Printf(TEXT("выбор=%d 5288×%d тело %s лоток0=%d кандидат1=%d иконка2014=%d"), bSel ? 1 : 0, Count(5288), *Hex(C), TrayCode(0), ListCode(0, 1), bBefore ? 1 : 0));
			Feed(5289, Body5289(1, 2, 1, 1));
			Expect(X->DebugCellHasIcon(2037) && X->DebugCellText(2037) == TEXT("1") && !X->DebugCellHasIcon(2014) && X->bCellTaken == 0 && !X->DebugIsShown(TEXT("TRAN_TRAYSEL_108_0")),
				TEXT("B2 5289/1 → лоток 2037 «1», кандидат 2014 пуст, +0x10 = 0, рамка 2047 скрыта"),
				FString::Printf(TEXT("2037 %d «%s», 2014 %d, +0x10=%d"), X->DebugCellHasIcon(2037) ? 1 : 0, *X->DebugCellText(2037), X->DebugCellHasIcon(2014) ? 1 : 0, X->bCellTaken));
		}
		// ═══ C. Нумпад режим 3: стопка 5 → ввод 3 → 5288 (3); докладка 2 → 5288 вида 2 (+0x30); отмена — 5288 нет (контроль) ═══
		{
			Caps.Reset();
			X->OnCellPressed(2013); X->OnCellDoubleClick(2013);
			const bool bNum = WM->IsNumPadOpen();
			const int32 N0 = Count(5288);
			const bool bTyped = TypeNumPad(WM, TEXT("3"));
			WM->CloseNumPad(true);
			const FCap* C = Last(5288);
			Expect(bNum && N0 == 0 && bTyped && Count(5288) == 1 && Same(C, Want5288(77, 1, 2, 0, 3)) && TrayCount(1) == 3 && ListCount(0, 0) == 2,
				TEXT("C1 стопка 5: 0xD → нумпад режим 3 (5288 нет), ввод 3 + ОК → 5288 [ячейка 1, слот 0, 3 шт.]; кандидат 2"),
				FString::Printf(TEXT("нумпад=%d до ОК 5288×%d введено=%d тело %s лоток1=%d кандидат0=%d"), bNum ? 1 : 0, N0, bTyped ? 1 : 0, *Hex(C), TrayCount(1), ListCount(0, 0)));
			Feed(5289, Body5289(1, 2, 0, 3));
			Expect(X->DebugCellText(2038) == TEXT("3") && X->DebugCellText(2013) == TEXT("2"), TEXT("C2 5289/1 → лоток 2038 «3», кандидат 2013 «2»"),
				FString::Printf(TEXT("2038 «%s» 2013 «%s»"), *X->DebugCellText(2038), *X->DebugCellText(2013)));
			Caps.Reset();
			X->OnCellPressed(2013); X->OnCellDoubleClick(2013);
			TypeNumPad(WM, TEXT("2"));
			WM->CloseNumPad(true);
			const FCap* C2 = Last(5288);
			Expect(Count(5288) == 1 && Same(C2, Want5288(77, 1, 2, 0, 2)) && X->bStackAdd == 1 && TrayCount(1) == 5 && ListCode(0, 0) == 0,
				TEXT("C3 докладка в тот же стак (нумпад 2) → 5288 вида 2 [ячейка 1, 2 шт.], +0x30 = 1, лоток 5, кандидат пуст"),
				FString::Printf(TEXT("тело %s +0x30=%d лоток1=%d кандидат0=%d"), *Hex(C2), X->bStackAdd, TrayCount(1), ListCode(0, 0)));
			Feed(5289, Body5289(1, 2, 0, 2));
			Expect(X->DebugCellText(2038) == TEXT("5") && X->bStackAdd == 0 && X->SlotAccum[1] == 5, TEXT("C4 5289 докладки: лоток «5», накопитель 3+2 = 5, +0x30 = 0"),
				FString::Printf(TEXT("2038 «%s» +0x30=%d накопитель=%d"), *X->DebugCellText(2038), X->bStackAdd, X->SlotAccum[1]));
			Caps.Reset();
			X->OnCellPressed(2017); X->OnCellDoubleClick(2017);
			const bool bNum2 = WM->IsNumPadOpen();
			WM->CloseNumPad(false);
			Expect(bNum2 && Count(5288) == 0 && ListCount(0, 4) == 6 && X->DebugCellText(2017) == TEXT("6") && X->DebugCellAlpha(2017) > 0.99f,
				TEXT("C5 нумпад «Отмена» → 5288 нет, кандидат цел «6» (`uiNumPadCloseRefreshExchange`, контроль к C1)"),
				FString::Printf(TEXT("нумпад=%d 5288×%d кандидат4=%d «%s»"), bNum2 ? 1 : 0, Count(5288), ListCount(0, 4), *X->DebugCellText(2017)));
		}
		// ═══ D. 5289 с ошибкой 4 → 5022 (−75279), 5290 [вид 3], откат лотка, кандидат вернулся (контроль — код 1 выше) ═══
		{
			Caps.Reset();
			X->OnCellPressed(2018); X->OnCellDoubleClick(2018);   // кандидат 5: стопка 7
			TypeNumPad(WM, TEXT("7")); WM->CloseNumPad(true);
			const bool bSent = Count(5288) == 1 && TrayCount(2) == 7 && ListCode(0, 5) == 0;
			Feed(5289, Body5289(4, 2, 5, 7));
			FColor Col; const bool bCol = Chat->DebugFindLastSystemLineColor(Loc(5022).Left(12), Col);
			const FCap* C = Last(5290);
			Expect(bSent && X->GetLastNoticeLoc() == 5022 && bCol && Col == FColor(0xFE, 0xD9, 0xF1, 0xFF) && Count(5290) == 1 && Same(C, Want5290(77, 2, 5, 7))
				&& TrayCode(2) == 0 && ListCount(0, 5) == 7 && X->DebugCellText(2018) == TEXT("7"),
				TEXT("D1 5289 код 4 → 5022 цветом −75279, 5290 [#77, к=2, слот 5, 7 шт.], лоток 2 пуст, кандидат «7» вернулся"),
				FString::Printf(TEXT("отправлено=%d нотис %d цвет %s 5290×%d тело %s лоток2=%d кандидат5=%d «%s»"), bSent ? 1 : 0, X->GetLastNoticeLoc(),
					bCol ? *Col.ToHex() : TEXT("—"), Count(5290), *Hex(C), TrayCode(2), ListCount(0, 5), *X->DebugCellText(2018)));
		}
		// ═══ E. Снять с лотка двойным ЛКМ → 5290 [вид 1], запись вернулась; повтор по пустой ячейке — ничего (контроль) ═══
		{
			Caps.Reset();
			X->OnCellPressed(2037); X->OnCellDoubleClick(2037);
			const FCap* C = Last(5290);
			Expect(Count(5290) == 1 && Same(C, Want5290(77, 2, 1, 1)) && TrayCode(0) == 0 && ListCode(0, 1) == 20572 && X->DebugCellHasIcon(2014) && !X->DebugCellHasIcon(2037),
				TEXT("E1 лоток 2037 → 0xD → 5290 [к=2, слот 1, 1 шт.], кандидат 20572 вернулся на место, ячейка лотка пуста"),
				FString::Printf(TEXT("5290×%d тело %s лоток0=%d кандидат1=%d"), Count(5290), *Hex(C), TrayCode(0), ListCode(0, 1)));
			X->OnCellPressed(2037); X->OnCellDoubleClick(2037);
			Expect(Count(5290) == 1 && X->bCellTaken == 0, TEXT("E2 пустая ячейка лотка: 0xB не берёт, 0xD ничего не шлёт (контроль)"), FString::Printf(TEXT("5290×%d +0x10=%d"), Count(5290), X->bCellTaken));
		}
		// ═══ F. Перенос: 5 → заливка 5 (40 %), 7 мимо окна → возврат; гейт 5014; бросок в лоток → нумпад; лоток → список → 5290 ═══
		{
			Caps.Reset();
			X->OnCellPressed(2019);
			const bool bBeg = X->OnDragBegin(2019);
			const float A = X->DebugCellAlpha(2019);
			const bool bGhost = X->DragGhost.IsActive();
			X->OnDrop(-1);
			Expect(bBeg && bGhost && FMath::IsNearlyEqual(A, 0.4f, 0.01f) && X->DebugCellAlpha(2019) > 0.99f && X->DebugCellText(2019) == TEXT("8") && Count(5288) == 0,
				TEXT("F1 перенос кандидата: призрак, ячейка 40 % (заливка 5); бросок мимо — возврат «8», 5288 нет"),
				FString::Printf(TEXT("начат=%d призрак=%d альфа=%.2f→%.2f «%s» 5288×%d"), bBeg ? 1 : 0, bGhost ? 1 : 0, A, X->DebugCellAlpha(2019), *X->DebugCellText(2019), Count(5288)));
			X->OnCellPressed(2016);
			const int32 S = X->GetNoticeSeq();
			const bool bBeg2 = X->OnDragBegin(2016);
			Expect(!bBeg2 && X->GetLastNoticeLoc() == 5014 && X->GetNoticeSeq() == S + 1 && !X->DragGhost.IsActive(),
				TEXT("F2 перенос вещи (enchantOrSocket 0 < 4) в обмене обычных → 5014, переноса нет (контроль к F1)"),
				FString::Printf(TEXT("начат=%d нотис %d"), bBeg2 ? 1 : 0, X->GetLastNoticeLoc()));
			X->OnCellPressed(2019); X->OnDragBegin(2019); X->OnDrop(2039);
			const bool bNum = WM->IsNumPadOpen();
			TypeNumPad(WM, TEXT("1")); WM->CloseNumPad(true);
			const FCap* C = Last(5288);
			Expect(bNum && Count(5288) == 1 && Same(C, Want5288(77, 0, 2, 6, 1)) && ListCount(0, 6) == 7,
				TEXT("F3 бросок стопки 8 на лоток → нумпад, ввод 1 → 5288 [ячейка 0 (первая свободная), слот 6, 1 шт.], кандидат 7"),
				FString::Printf(TEXT("нумпад=%d тело %s кандидат6=%d"), bNum ? 1 : 0, *Hex(C), ListCount(0, 6)));
			Feed(5289, Body5289(1, 2, 6, 1));
			Caps.Reset();
			X->OnCellPressed(2038);
			const bool bBeg3 = X->OnDragBegin(2038);
			X->OnDrop(2020);
			const FCap* C3 = Last(5290);
			Expect(bBeg3 && Count(5290) == 1 && Same(C3, Want5290(77, 2, 0, 5)) && TrayCode(1) == 0 && ListCount(0, 0) == 5,
				TEXT("F4 перенос лотка 2038 (стак 5) на список → 5290 [слот 0, 5 шт.], кандидат 0 снова 5"),
				FString::Printf(TEXT("начат=%d тело %s лоток1=%d кандидат0=%d"), bBeg3 ? 1 : 0, *Hex(C3), TrayCode(1), ListCount(0, 0)));
		}
		// ═══ G. Лоток полон → 5031 (−75279), 5288 нет; свободная ячейка — 5288 (контроль) ═══
		{
			for (int32 k = 0; k < 5; ++k) { if (TrayCode(k) <= 0) { AsdaBytes::WrI32(X->TrayRec(k), 20573); AsdaBytes::WrI32(X->TrayRec(k) + 9, 1); } }
			Caps.Reset();
			X->OnCellPressed(2014); X->OnCellDoubleClick(2014);
			FColor Col; const bool bCol = Chat->DebugFindLastSystemLineColor(Loc(5031).Left(12), Col);
			const bool bFull = Count(5288) == 0 && X->GetLastNoticeLoc() == 5031 && bCol && Col == FColor(0xFE, 0xD9, 0xF1, 0xFF);
			FMemory::Memzero(X->TrayRec(4), UAsdaExchangeP2PSubsystem::kRec);
			X->OnCellPressed(2014); X->OnCellDoubleClick(2014);
			Expect(bFull && Count(5288) == 1 && Same(Last(5288), Want5288(77, 4, 2, 1, 1)),
				TEXT("G1 лоток полон → 5031 −75279, 5288 нет; ячейка 4 свободна → 5288 [ячейка 4] (контроль)"),
				FString::Printf(TEXT("полон=%d тело %s"), bFull ? 1 : 0, *Hex(Last(5288))));
		}
		// ═══ H. Колесо 2051: вниз — страница +6; вверх у края — стоит; Д17 — выбор ≥ 24 и колесо (ключ 1 листает / 0 — нет) ═══
		Clean();
		{
			OpenAsTarget(0, 79);
			const FString T0 = X->DebugCellText(2017);   // кандидат 4
			X->OnWheel(-1);
			const FString T1 = X->DebugCellText(2017);   // кандидат 10
			X->OnWheel(+5);
			Expect(T0 == TEXT("6") && T1 == TEXT("12") && X->ScrollRow == 0 && X->DebugCellText(2017) == TEXT("6"),
				TEXT("H1 колесо вниз → ряд 1 (2017 «12» = кандидат 10); вверх сверх края → ряд 0 (контроль)"),
				FString::Printf(TEXT("ряд0 «%s» ряд1 «%s» итоговый ряд %d «%s»"), *T0, *T1, X->ScrollRow, *X->DebugCellText(2017)));
			X->OnWheel(-4);   // ряд 4: кандидаты 24…47
			X->OnCellPressed(2013);   // кандидат 24 (≥ 24)
			const int32 Sel = X->SelSlotIndex;
			X->OnWheel(+1);   // ряд 3: кандидат 18 в 2013
			const FString Fix1 = X->DebugCellText(2013);
			X->OnWheel(-1);               // ключ 1: ряд 4 перерисован (2013 = кандидат 24, «26»)
			X->OnCellPressed(2013);       // снова выбран кандидат 24
			SetFix(0);
			X->OnWheel(+1);               // ключ 0: эталон страницу не перерисует
			const FString Fix0 = X->DebugCellText(2013);
			SetFix(1);
			Expect(Sel == 24 && Fix1 == TEXT("20") && Fix0 == TEXT("26"),
				TEXT("H2 Д17: выбран кандидат 24, колесо вверх — ключ 1: страница перерисована («20»); ключ 0: как эталон — осталась «26»"),
				FString::Printf(TEXT("выбор=%d ключ1 «%s» ключ0 «%s»"), Sel, *Fix1, *Fix0));
		}
		// ═══ I. Тип 1: кандидаты — вещи сумки + золото; всё золото → 5039; часть → 5288 вида 3; докладка → вида 4; вещь → 5289 ═══
		Clean();
		{
			OpenAsTarget(1, 80);
			Expect(X->ListCount == 3 && ListCode(1, 0) == 20626 && ListCode(1, 2) == 20551 && X->DebugCellHasIcon(2015) && X->DebugCellText(2015).IsEmpty(),
				TEXT("I1 тип 1: 2 вещи контейнера 1 + золото 20551, золото без надписи"), FString::Printf(TEXT("кандидатов %d, коды %d/%d/%d"),
					X->ListCount, ListCode(1, 0), ListCode(1, 1), ListCode(1, 2)));
			Caps.Reset();
			X->OnCellPressed(2015); X->OnCellDoubleClick(2015);
			TypeNumPad(WM, TEXT("1000")); WM->CloseNumPad(true);
			const bool bAll = Count(5288) == 0 && X->GetLastNoticeLoc() == 5039 && TrayCode(0) == 0;
			X->OnCellPressed(2015); X->OnCellDoubleClick(2015);
			TypeNumPad(WM, TEXT("400")); WM->CloseNumPad(true);
			const FCap* C = Last(5288);
			Expect(bAll && Count(5288) == 1 && Same(C, Want5288(80, 0, 2, 2, 400)) && TrayCount(0) == 400 && ListCount(1, 2) == 600,
				TEXT("I2 золото: ВСЁ (1000) → 5039, 5288 нет; 400 → 5288 вида 3 [ячейка 0, к=2, слот 2, 400], в списке 600"),
				FString::Printf(TEXT("всё=%d тело %s лоток=%d список=%d"), bAll ? 1 : 0, *Hex(C), TrayCount(0), ListCount(1, 2)));
			// ★ О3 (Д18): сервер отвечает 5289 на КАЖДЫЙ 5288 — подаём ответ, иначе очередь ожидания сдвинет следующие ответы.
			Feed(5289, Body5289(1, 2, 2, 400));
			X->OnCellPressed(2015); X->OnCellDoubleClick(2015);
			TypeNumPad(WM, TEXT("100")); WM->CloseNumPad(true);
			const FCap* C2 = Last(5288);
			Expect(Count(5288) == 2 && Same(C2, Want5288(80, 0, 2, 2, 100)) && TrayCount(0) == 500 && ListCount(1, 2) == 500 && X->bStackAdd == 0,
				TEXT("I3 докладка золота (100) → 5288 вида 4 [ячейка 0, 100]; лоток 500, список 500, +0x30 не взведён"),
				FString::Printf(TEXT("тело %s лоток=%d список=%d +0x30=%d"), *Hex(C2), TrayCount(0), ListCount(1, 2), X->bStackAdd));
			Feed(5289, Body5289(1, 2, 2, 100));   // ответ сервера на докладку (О3, Д18)
			X->OnCellPressed(2013); X->OnCellDoubleClick(2013);
			const FCap* C3 = Last(5288);
			Feed(5289, Body5289(1, 1, 0, 1));
			Expect(Same(C3, Want5288(80, 1, 1, 0, 1)) && TrayCode(1) == 20626 && X->DebugCellHasIcon(2038) && ListCode(1, 0) == 0,
				TEXT("I4 вещь 20626 → 5288 вида 3 [ячейка 1, к=1, слот 0, 1]; 5289/1 → лоток 2038 с иконкой, кандидат пуст"),
				FString::Printf(TEXT("тело %s лоток1=%d иконка=%d"), *Hex(C3), TrayCode(1), X->DebugCellHasIcon(2038) ? 1 : 0));
			Caps.Reset();
			X->OnCellPressed(2037); X->OnCellDoubleClick(2037);
			Expect(Count(5290) == 1 && Same(Last(5290), Want5290(80, 2, 2, 500)) && ListCount(1, 2) == 1000 && TrayCode(0) == 0,
				TEXT("I5 снять золото (тип 1) → 5290 [к=2, слот 2, 500], в списке снова весь запас 1000 (`cInven_ApplySelectedSlotItemCode`)"),
				FString::Printf(TEXT("тело %s список=%d"), *Hex(Last(5290)), ListCount(1, 2)));
		}
		// ═══ J. 2048 (с О3 — готовность, 5291; подробно — самотест О3); 2049/2011 — рендер лотка партнёра по данным ═══
		{
			Caps.Reset();
			UCheckBox* Mine = Cast<UCheckBox>(X->DebugNamedWidget(TEXT("TRAN_BUT01")));
			if (Mine) { Mine->SetIsChecked(true); }   // так галку ставит сам флажок по клику (0x10601)
			X->OnMyLockChecked(true);
			const bool bOn = Mine && X->bMyLock == 1 && X->DebugIsChecked(2048) && Count(5291) == 1;
			if (Mine) { Mine->SetIsChecked(false); }   // 0x10602 — флажок заперт (латч +609)
			X->OnMyLockChecked(false);
			const bool bHeld = X->DebugIsChecked(2048) && Count(5291) == 1;
			Expect(bOn && bHeld, TEXT("J1 2048 (О3): клик → готовность +0x4C4 = 1, галка, 5291; повторный клик — галка держится, второго 5291 нет (контроль)"),
				FString::Printf(TEXT("взвод=%d держится=%d 5291×%d"), bOn ? 1 : 0, bHeld ? 1 : 0, Count(5291)));
			// Модель готовности — назад (блок J2 рендерит лоток партнёра по счётчику сам).
			X->bMyLock = 0; X->bMyLockLatch = false; X->ReadyCounter = 0; X->bLockOff = 1; X->SetCheckState(2048, false);
			uint8* P = X->PartnerTray.GetData();
			AsdaBytes::WrI32(P, 20627); AsdaBytes::WrI32(P + 7, 1);
			X->ReadyCounter = 1; X->RefreshPartnerTray(true);
			const bool b1 = X->DebugIsChecked(2049) && X->DebugCellHasIcon(2042) && !X->DebugIsShown(TEXT("TRAN_OKBUT"));
			X->ReadyCounter = 2; X->RefreshPartnerTray(true);
			const bool b2 = X->DebugIsShown(TEXT("TRAN_OKBUT"));
			X->RefreshPartnerTray(false);
			const bool b3 = !X->DebugIsChecked(2049) && !X->DebugCellHasIcon(2042) && !X->DebugIsShown(TEXT("TRAN_OKBUT"));
			X->ReadyCounter = 0;
			Expect(b1 && b2 && b3, TEXT("J2 лоток партнёра: данные → иконка 2042 и галка 2049, 2011 скрыт при счётчике 1 / виден при 2; сброс — всё пусто"),
				FString::Printf(TEXT("счётчик1=%d счётчик2=%d сброс=%d"), b1 ? 1 : 0, b2 ? 1 : 0, b3 ? 1 : 0));
		}
		// ═══ K. Подсказка ячейки: владелец ExchangeP2P; пустая ячейка — не показывается (контроль) ═══
		{
			X->TickTooltip(-1);
			X->TickTooltip(2014);   // вещь 20627 (тип 1)
			const bool bShown = Tip->IsTooltipShown() && Tip->GetTipOwner() == EAsdaTipOwner::ExchangeP2P;
			X->TickTooltip(2046);   // пустая ячейка партнёра
			const bool bHidden = !Tip->IsTooltipShown() || Tip->GetTipOwner() != EAsdaTipOwner::ExchangeP2P;
			X->TickTooltip(-1);
			Expect(bShown && bHidden, TEXT("K1 подсказка кандидата — владелец ExchangeP2P; над пустой ячейкой — нет (контроль)"),
				FString::Printf(TEXT("показана=%d пусто-скрыта=%d"), bShown ? 1 : 0, bHidden ? 1 : 0));
		}
		// ═══ L. Закрытия: ESC и «закрыть все» эталона окно НЕ трогают; «Отмена» 2012 → 5286; смерть → 5286 (ключ 0 — окно держится);
		//     сброс сессии — окно скрыто без пакетов ═══
		{
			Caps.Reset();
			WM->CloseTopmostOnEsc();
			const bool bEsc = X->IsWindowShown() && Count(5286) == 0;
			UButton* Cancel = Cast<UButton>(X->DebugNamedWidget(TEXT("TRAN_CANCELBUT")));
			if (Cancel) { Cancel->OnClicked.Broadcast(); }
			Expect(bEsc && Cancel && Count(5286) == 1 && Same(Last(5286), TArray<uint8>({ 80, 0, 0, 0, 0, 0 })) && !X->IsWindowShown() && X->GetLastNoticeLoc() == 5041
				&& !UAsdaTradeStateSubsystem::IsExchangeWindowOpen(GI),
				TEXT("L1 ESC — окно остаётся, 5286 нет (эталон); кнопка 2012 (OnClicked) → 5286 [#80], окно скрыто, 5041"),
				FString::Printf(TEXT("после ESC=%d 5286×%d тело %s окно=%d нотис %d"), bEsc ? 1 : 0, Count(5286), *Hex(Last(5286)), X->IsWindowShown() ? 1 : 0, X->GetLastNoticeLoc()));
			Clean(); OpenAsTarget(0, 81); Caps.Reset();
			AsdaTestNet::bSelfDead(Net) = true; X->Tick(0.f); AsdaTestNet::bSelfDead(Net) = false;
			const bool bDeath = Count(5286) == 1 && Last(5286)->P[0] == 81 && !X->IsWindowShown();
			Clean(); OpenAsTarget(0, 82); Caps.Reset(); SetDeath(0);
			AsdaTestNet::bSelfDead(Net) = true; X->Tick(0.f); AsdaTestNet::bSelfDead(Net) = false;
			const bool bKeep = Count(5286) == 0 && X->IsWindowShown();
			SetDeath(1);
			Expect(bDeath && bKeep, TEXT("L2 [ПОРТ] смерть с открытым окном → 5286 [#81], окно скрыто; ключ P2PCloseOnDeath 0 — окно держится (контроль)"),
				FString::Printf(TEXT("смерть=%d ключ0=%d"), bDeath ? 1 : 0, bKeep ? 1 : 0));
			Caps.Reset();
			X->ResetSessionState(EAsdaSessionResetReason::MapChanged);
			Expect(!X->IsWindowShown() && Caps.Num() == 0 && !UAsdaTradeStateSubsystem::IsExchangeWindowOpen(GI) && X->ListCount == -1,
				TEXT("L3 сброс сессии (смена карты) → окно скрыто, пакетов нет, модель чиста"),
				FString::Printf(TEXT("окно=%d пакетов %d кандидатов %d"), X->IsWindowShown() ? 1 : 0, Caps.Num(), X->ListCount));
		}
		// ═══ M. [ПОРТ] 5288 не ушёл (нет связи) → локальный откат без 5290; с перехватом — уходит (контроль) ═══
		Clean();
		{
			OpenAsTarget(0, 83);
			AsdaSelfTestKit::DisarmCapture(Net, Gen);
			X->OnCellPressed(2014); X->OnCellDoubleClick(2014);
			const bool bRolled = TrayCode(0) == 0 && ListCode(0, 1) == 20572 && X->DebugCellHasIcon(2014);
			Gen = AsdaSelfTestKit::ArmCapture(Net, [this](uint16 Op, const uint8* P, int32 Len, bool, TCHAR) { FCap& C = Caps.AddDefaulted_GetRef(); C.Op = Op; C.P.Append(P, Len); }, 0, TEXT("asda.Trade.SelfTestExchangeO2"));
			Caps.Reset();
			X->OnCellPressed(2014); X->OnCellDoubleClick(2014);
			Expect(bRolled && Count(5288) == 1 && Count(5290) == 0 && TrayCode(0) == 20572,
				TEXT("M1 без связи 5288 не ушёл → кандидат вернулся, лоток пуст, 5290 нет; с перехватом — 5288 (контроль)"),
				FString::Printf(TEXT("откат=%d 5288×%d 5290×%d"), bRolled ? 1 : 0, Count(5288), Count(5290)));
		}
		// ═══ N. «Душа»: 5288/5290 (и с О3 — 5291) в таблице гейта; ход 4006 — нет (отрицательный контроль, ревизия О3 МЕЛК-2);
		//     блок при душе 1 / нет при 0 ═══
		{
			const uint8 SoulWas = AsdaTestNet::SelfSoulState(Net);
			const bool bG88 = AsdaSoul::IsGatedOpcode(5288), bG90 = AsdaSoul::IsGatedOpcode(5290), bG91 = AsdaSoul::IsGatedOpcode(5291);
			const bool bG4006 = AsdaSoul::IsGatedOpcode(4006);   // ход — гейта у сервера нет (контроль «не всё подряд в таблице»)
			AsdaTestNet::SetSelfSoulState(Net, 1); const bool bB1 = AsdaSoul::BlockOutgoing(Net, 5288);
			AsdaTestNet::SetSelfSoulState(Net, 0); const bool bB0 = AsdaSoul::BlockOutgoing(Net, 5288);
			AsdaTestNet::SetSelfSoulState(Net, SoulWas);
			Expect(bG88 && bG90 && bG91 && !bG4006 && bB1 && !bB0, TEXT("N1 душа: 5288, 5290 и 5291 (О3) в таблице, ход 4006 нет (контроль); блок 5288 при душе 1 / нет при 0"),
				FString::Printf(TEXT("5288=%d 5290=%d 5291=%d 4006=%d блок1=%d блок0=%d"), bG88 ? 1 : 0, bG90 ? 1 : 0, bG91 ? 1 : 0, bG4006 ? 1 : 0, bB1 ? 1 : 0, bB0 ? 1 : 0));
		}
		// ═══ O. Лопаты (ревизия О1, МЕЛК-5): авто-лопата (подтип 34) «верхом» → 2040; без транспорта — 2040 нет (контроль) ═══
		Clean();
		{
			Inv->DebugPutItem(2, 40, 20613, 1);
			Eff->AddType2(500, 15, 600, 0);
			Sink.Reset();
			Inv->UseConsumable(2, 40, -1);
			const int32 H1 = Sink.Hits(TEXT("гейт №13, нотис 2040"));
			Eff->ResetSessionState(EAsdaSessionResetReason::LeftWorld);
			Sink.Reset();
			Inv->UseConsumable(2, 40, -1);
			const int32 H0 = Sink.Hits(TEXT("гейт №13, нотис 2040"));
			Inv->DebugPutItem(2, 40, 0, 0);
			Expect(H1 == 1 && H0 == 0, TEXT("O1 авто-лопата 20613 (11/34) верхом → 2040 (как у 33); без транспорта — нет (контроль)"),
				FString::Printf(TEXT("верхом %d, без %d"), H1, H0));
		}
		// ── окно на вторую фазу: тип 0, открыто ──
		Clean();
		OpenAsTarget(0, 90);
		return true;
	}

	// Фаза 2 (через кадры): геометрия путём игрока и хит-тест окна; затем восстановление.
	void RunPhase2(UWorld* W, bool bQuit)
	{
		using namespace AsdaP2PTestO2Local;
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaExchangeP2PSubsystem* X = GI ? GI->GetSubsystem<UAsdaExchangeP2PSubsystem>() : nullptr;
		UAsdaNetSubsystem* Net = GI ? AsdaTestNet::Get(GI) : nullptr;
		UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
		UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
		if (X && X->DebugWindowWidget())
		{
			// ═══ P. Геометрия: живые прямоугольники относительно угла рамки (план TRAN_WIN_0_0), в единицах `.JUI` ═══
			UWidget* Corner = X->DebugNamedWidget(TEXT("TRAN_WIN_0_0"));
			FVector2D CMn = FVector2D::ZeroVector, CMx = FVector2D::ZeroVector;
			const bool bCorner = Corner && AsdaUi::WidgetAbsRect(Corner, CMn, CMx);
			UWidget* Cell0 = X->DebugCellWidget(2013);
			FVector2D Mn0 = FVector2D::ZeroVector, Mx0 = FVector2D::ZeroVector;
			const bool bCell0 = Cell0 && AsdaUi::WidgetAbsRect(Cell0, Mn0, Mx0);
			const float Scale = bCell0 ? (float)(Mx0.X - Mn0.X) / 32.f : 0.f;   // ячейка 32×32 в `.JUI`
			struct FRef { const TCHAR* Name; int32 IdNum; float X; float Y; };
			static const FRef kRefs[] = {
				{ nullptr, 2013, 11.f, 63.f }, { nullptr, 2036, 176.f, 162.f }, { nullptr, 2037, 11.f, 199.f }, { nullptr, 2041, 143.f, 199.f },
				{ nullptr, 2042, 11.f, 303.f }, { nullptr, 2046, 143.f, 303.f },
				{ TEXT("TRAN_BUT01"), 0, 182.f, 205.f }, { TEXT("TRAN_BUT02"), 0, 182.f, 309.f },
				{ TEXT("TRAN_CANCELBUT"), 0, 116.f, 365.f }, { TEXT("TRAN_TOPCLOSE"), 0, 204.f, 6.f } };
			int32 Good = 0, Bad = 0; FString Worst;
			for (const FRef& R : kRefs)
			{
				UWidget* Wd = R.Name ? X->DebugNamedWidget(R.Name) : X->DebugCellWidget(R.IdNum);
				FVector2D Mn = FVector2D::ZeroVector, Mx = FVector2D::ZeroVector;
				if (!bCorner || Scale <= 0.f || !Wd || !AsdaUi::WidgetAbsRect(Wd, Mn, Mx)) { ++Bad; Worst += FString::Printf(TEXT(" %s:нет"), R.Name ? R.Name : *FString::FromInt(R.IdNum)); continue; }
				const float Rx = (float)(Mn.X - CMn.X) / Scale, Ry = (float)(Mn.Y - CMn.Y) / Scale;
				if (FMath::Abs(Rx - R.X) <= 1.5f && FMath::Abs(Ry - R.Y) <= 1.5f) { ++Good; }
				else { ++Bad; Worst += FString::Printf(TEXT(" %s:(%.1f,%.1f)≠(%.0f,%.0f)"), R.Name ? R.Name : *FString::FromInt(R.IdNum), Rx, Ry, R.X, R.Y); }
			}
			Expect(bCorner && bCell0 && Bad == 0 && Good == UE_ARRAY_COUNT(kRefs),
				TEXT("P1 путь игрока: 10 узлов (ячейки 2013/2036/2037/2041/2042/2046, 2048, 2049, 2012, 2050) стоят по `.JUI` ±1,5"),
				FString::Printf(TEXT("масштаб %.3f, совпало %d, расхождения:%s"), Scale, Good, Worst.IsEmpty() ? TEXT(" нет") : *Worst));
			// ═══ Q. Хит-тест окна по центрам ячеек (тот же путь, что у мыши); точка вне ячеек — −1 (контроль) ═══
			int32 HitOk = 0; FString HitBad;
			for (int32 Id : { 2013, 2020, 2036, 2037, 2041, 2042, 2046 })
			{
				FVector2D Mn = FVector2D::ZeroVector, Mx = FVector2D::ZeroVector;
				UWidget* Wd = X->DebugCellWidget(Id);
				if (Wd && AsdaUi::WidgetAbsRect(Wd, Mn, Mx) && X->HitTestCellAt((Mn + Mx) * 0.5) == Id) { ++HitOk; }
				else { HitBad += FString::Printf(TEXT(" %d"), Id); }
			}
			const int32 Outside = bCorner ? X->HitTestCellAt(CMn - FVector2D(5.0, 5.0)) : 0;
			Expect(HitOk == 7 && Outside == -1, TEXT("Q1 хит-тест: центр ячейки → её IDNum (7 из 7); точка вне окна → −1 (контроль)"),
				FString::Printf(TEXT("совпало %d, мимо:%s, вне окна=%d"), HitOk, HitBad.IsEmpty() ? TEXT(" нет") : *HitBad, Outside));
		}
		else
		{
			Expect(false, TEXT("P0 окно для фазы 2"), TEXT("виджета нет"));
		}
		// ── восстановление ──
		if (X && WM)
		{
			X->ResetSessionState(EAsdaSessionResetReason::LeftWorld);
			WM->ClearMsgBoxQueue(TEXT("самотест О2"));
		}
		if (Net)
		{
			AsdaSelfTestKit::DisarmCapture(Net, Gen);
			AsdaTestNet::SelfUserNum(Net) = SelfNumWas; AsdaTestNet::bSelfUserNumKnown(Net) = bSelfKnownWas; AsdaTestNet::SelfLevel(Net) = LevelWas; AsdaTestNet::bSelfDead(Net) = false;
		}
		if (Inv)
		{
			for (int32 i = 0; i < Cont2Was.Num(); ++i) { if (FAsdaItemInstance* C = Inv->GetCellMutable(2, i)) { *C = Cont2Was[i]; } }
			for (int32 i = 0; i < Cont1Was.Num(); ++i) { if (FAsdaItemInstance* C = Inv->GetCellMutable(1, i)) { *C = Cont1Was[i]; } }
		}
		AsdaCVarSave::RestoreAll(TEXT("asda.Trade.SelfTestExchangeO2"));   // ★ ИСПРАВЛЕНИЕ 11.10.2026 (В0, N1(д)/U8): ключи — значение + SetBy (прежде — с SetByConsole)
		GLog->RemoveOutputDevice(&Sink);
		UE_LOG(LogAsdaP2PTestO2, Log, TEXT("САМОТЕСТ-О2 ИТОГ: ok=%d fail=%d"), Ok, Fail);
		AsdaSelfTestKit::Quit(W, bQuit);
	}
};

namespace
{
	FAutoConsoleCommandWithWorldAndArgs GAsdaTradeSelfTestExchangeO2(
		TEXT("asda.Trade.SelfTestExchangeO2"),
		TEXT("asda.Trade.SelfTestExchangeO2 [delay=N] [quit] — самотест окна обмена TRAN_WIN 2008 (волна О2): списки/лоток, 0xB/5/7/0xD/колесо, "
		     "нумпад режим 3, 5288/5290 под перехватом, 5289 через штатный приём, золото типа 1, лоток партнёра, подсказка, закрытия (ESC, "
		     "2012, смерть, сброс сессии), откат неотправленного, душа 5288/5290, лопаты; фаза 2 — геометрия путём игрока. Только офлайн."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
		{
			const FAsdaSelfTestArgs KitArgs = FAsdaSelfTestArgs::Parse(Args);   // ★ 11.10.2026 (В3 S1): каркас — отказ с ИТОГ и quit, второй барьер на все опкоды
			if (AsdaSelfTestKit::RefuseStart(W, TEXT("asda.Trade.SelfTestExchangeO2"), KitArgs)) { return; }
			const float Delay = AsdaSelfTestKit::DelayArg(Args);
			const bool bQuit = Args.Contains(TEXT("quit"));
			TWeakObjectPtr<UWorld> WW(W);
			// Офлайн своего павна нет: офлайн-спавн штатными крюками (как О1), проверки — через 2 с; фаза 2 — ещё через 1,5 с.
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WW, bQuit](float) -> bool
			{
				UWorld* Wl = WW.Get();
				if (!Wl) { return false; }
				if (AsdaSelfTestKit::RefuseDeferred(Wl, TEXT("asda.Trade.SelfTestExchangeO2"), bQuit)) { return false; }   // Р-2: офлайн повторно — на отложенном старте
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
					TSharedRef<FAsdaExchangeP2PSelfTestO2> T = MakeShared<FAsdaExchangeP2PSelfTestO2>();
					if (!T->RunPhase1(W2))
					{
						AsdaSelfTestKit::Quit(W2, bQuit);
						return false;
					}
					FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WW, bQuit, T](float) -> bool
					{
						if (UWorld* W3 = WW.Get()) { T->RunPhase2(W3, bQuit); }
						else { AsdaCVarSave::RestoreAll(TEXT("asda.Trade.SelfTestExchangeO2")); }   // ★ ИСПРАВЛЕНИЕ 11.10.2026 (В0, N1(д)/U8): ключи — и при уходе мира
						return false;
					}), 1.5f);
					return false;
				}), bNeedSpawn ? 2.f : 0.f);
				return false;
			}), Delay);
			UE_LOG(LogAsdaP2PTestO2, Log, TEXT("САМОТЕСТ-О2: запуск через %.1f с"), Delay);
		}));
}

#endif // !UE_BUILD_SHIPPING
