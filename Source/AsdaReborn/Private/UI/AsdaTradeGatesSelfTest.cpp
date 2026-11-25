// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ ГЕЙТОВ ТОРГОВЛИ (29.09.2026, волна 0 торговли, `Reports/trade/wave0.md`). [ПОРТ]
//
// ЧТО ПРОВЕРЯЕТ. Читателей общей точки `UAsdaTradeStateSubsystem` путём самого гейта (настоящие функции подсистем, без
//   копий условий), с КОНТРОЛЬНЫМИ ПАРАМИ (закон pixel-metric-needs-control-pairs): одно и то же действие при
//   противоположном признаке обязано дать противоположный исход.
//   ПД-2 (ход клавишами 9568, ремесло 5590): «заявка в полёте» гейт НЕ взводит, «окно обмена открыто» — взводит.
//   ПД-1 (сообщество 107022): открытый аукцион `EXCHANGE_WIN` окно НЕ блокирует, личная лавка (22000 или 23000) — блокирует.
//   Прочие читатели `CExchange+0x2C`: меню игрока 2004 (5042 против 5054), приём 5283 (молча против 5058), хотбар,
//   режимы аукциона, отложенное золото 9913, рюкзак, разговор с NPC, клик по земле (9568), магазин снаряжения (6947),
//   элитный магазин (6947), парикмахерская, «закрыть всё при уходе», сброс сессии.
//   Нотисы ловятся на стоке системного чата (`LogAsdaChat` «[сисчат …]»), а не вызовом проверяемой функции.
//
// Консоль: `asda.Trade.SelfTestGates [delay=N] [quit]`. Только офлайн (`FAsdaOutgoingGate::RefuseDebugWhenOnline`).
//   Строки — «САМОТЕСТ-ТОРГОВЛЯ», итог — «САМОТЕСТ-ТОРГОВЛЯ ИТОГ: ok=… fail=…». Признаки торговли, «заявка в полёте»,
//   открытые окна и защёлка нотиса клавиш восстанавливаются в конце.
#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "UI/AsdaTradeStateSubsystem.h"
#include "UI/AsdaPlayerMenuSubsystem.h"
#include "UI/AsdaExchangeP2PSubsystem.h"   // ★ 29.09.2026 (волна О1): модель обмена — нотисы 2004/5283
#include "UI/AsdaCraftWindowSubsystem.h"
#include "UI/AsdaCommunitySubsystem.h"
#include "UI/AsdaHotbarSubsystem.h"
#include "UI/AsdaExchangeSubsystem.h"
#include "UI/AsdaInventorySubsystem.h"
#include "UI/AsdaEquipShopSubsystem.h"
#include "UI/AsdaPremiumShopSubsystem.h"
#include "UI/AsdaHairShopSubsystem.h"
#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaNotice.h"
#include "UI/AsdaChatSubsystem.h"      // ревизия С-1: цвет строки журнала 2572
#include "Npc/AsdaNpcDialogSubsystem.h"
#include "World/AsdaPlayerController.h"
#include "Test/AsdaSelfTestNet.h"   // ★ R1: тонкий доступ самотестов к сети (вместо AsdaNetSubsystem.h)
#include "Test/AsdaSelfTestKit.h"   // ★ 11.10.2026 (В3 S1): общий каркас самотестов (аргументы, отказ, перехват, итог, выход)
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "HAL/IConsoleManager.h"
#include "Misc/OutputDevice.h"
#include "Misc/OutputDeviceRedirector.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaTradeTest, Log, All);

namespace AsdaTradeTestLocal
{
	// Перехватчик лога: все строки (для гейтовых строк подсистем) и отдельно строки системного чата «[сисчат …]».
	class FSink : public FOutputDevice
	{
	public:
		TArray<FString> Chat;   // строки стока системного чата (категория LogAsdaChat)
		TArray<FString> All;    // все строки любой категории
		FCriticalSection Lock;  // лог может прийти с любого потока
		virtual void Serialize(const TCHAR* V, ELogVerbosity::Type, const FName& Category) override
		{
			if (!V) { return; }
			static const FName kChat(TEXT("LogAsdaChat"));
			FScopeLock L(&Lock);
			All.Add(V);
			if (Category == kChat && FCString::Strncmp(V, TEXT("[сисчат"), 7) == 0) { Chat.Add(V); }
		}
		virtual bool CanBeUsedOnAnyThread() const override { return true; }
		virtual bool CanBeUsedOnMultipleThreads() const override { return true; }   // ★ 05.11.2026: НЕбуферизованный список UE 5.8 — строка доходит в момент записи (иначе гонка с потоком логов: строка после RemoveOutputDevice/проверки)
		// ★ Лог в UE 5 доставляется устройствам с фонового потока: без сброса строки текущего шага приходят позже и
		//   попадают в окно следующего (первый прогон: 5 ложных провалов). Перед чтением и очисткой — `GLog->Flush()`.
		void Reset() { if (GLog) { GLog->Flush(); } FScopeLock L(&Lock); Chat.Reset(); All.Reset(); }
		// Сколько строк чата содержат текст нотиса (первые 14 символов строки таблицы — без хвостов форматирования).
		int32 ChatHits(const FString& LocText)
		{
			const FString Key = LocText.Left(14);
			if (Key.IsEmpty()) { return -1; }
			if (GLog) { GLog->Flush(); }
			FScopeLock L(&Lock);
			int32 N = 0;
			for (const FString& S : Chat) { if (S.Contains(Key)) { ++N; } }
			return N;
		}
		// Сколько строк лога содержат подстроку.
		int32 LogHits(const TCHAR* Needle)
		{
			if (GLog) { GLog->Flush(); }
			FScopeLock L(&Lock);
			int32 N = 0;
			for (const FString& S : All) { if (S.Contains(Needle)) { ++N; } }
			return N;
		}
	};

	// Разбор `delay=N` (секунды до запуска) — чистая функция.
	// ★ R5-fix-c (13.11.2026): своя `ParseDelay` (дословная копия у двенадцати самотестов) убрана — разбор `delay=N` в общей точке
	//   `AsdaSelfTestKit::DelayArg` (`Test/AsdaSelfTestKit.h`), вызовы ниже идут в неё.
}

struct FAsdaTradeGatesSelfTest
{
	int32 Ok = 0;     // пройдено проверок
	int32 Fail = 0;   // провалено проверок

	// Одна проверка: строка «САМОТЕСТ-ТОРГОВЛЯ ok/ПРОВАЛ: <имя> — <подробности>».
	void Expect(bool bCond, const FString& Name, const FString& Detail)
	{
		if (bCond) { ++Ok; } else { ++Fail; }
		UE_LOG(LogAsdaTradeTest, Log, TEXT("САМОТЕСТ-ТОРГОВЛЯ %s: %s — %s"), bCond ? TEXT("ok") : TEXT("ПРОВАЛ"), *Name, *Detail);
	}

	// ★ Ревизия С-1 (29.09.2026): КАНАЛ нотиса — цвет последней строки журнала 2572 с его текстом против цвета обработчика
	//   эталона. bEqual=false — контрольная пара «цвет обязан ОТЛИЧАТЬСЯ» (доказывает, что сверка различает цвета).
	void ExpectColor(UGameInstance* GI, const FString& Name, int32 LocId, FColor Want, bool bEqual = true)
	{
		const UAsdaChatSubsystem* Chat = GI ? GI->GetSubsystem<UAsdaChatSubsystem>() : nullptr;
		FColor Got(0, 0, 0, 0);
		const FString Key = AsdaNotice::LocRaw(GI, LocId).Left(14);
		const bool bFound = Chat && !Key.IsEmpty() && Chat->DebugFindLastSystemLineColor(Key, Got);
		const bool bSame = bFound && Got.R == Want.R && Got.G == Want.G && Got.B == Want.B;
		Expect(bFound && (bEqual ? bSame : !bSame), Name,
			FString::Printf(TEXT("loc %d: цвет строки %s, %s %s"), LocId, bFound ? *Got.ToHex() : TEXT("НЕТ СТРОКИ"),
				bEqual ? TEXT("ждём") : TEXT("ждём НЕ"), *Want.ToHex()));
	}

	void Run(UWorld* W, bool bQuit)
	{
		using namespace AsdaTradeTestLocal;
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaTradeStateSubsystem* TS = GI ? GI->GetSubsystem<UAsdaTradeStateSubsystem>() : nullptr;
		UAsdaPlayerMenuSubsystem* PM = GI ? GI->GetSubsystem<UAsdaPlayerMenuSubsystem>() : nullptr;
		if (!GI || !TS || !PM)
		{
			UE_LOG(LogAsdaTradeTest, Error, TEXT("САМОТЕСТ-ТОРГОВЛЯ ИТОГ: ok=0 fail=1 (нет GameInstance/подсистем)"));
			return;
		}
		AAsdaPlayerController* Pc = Cast<AAsdaPlayerController>(W->GetFirstPlayerController());
		FSink Sink;
		GLog->AddOutputDevice(&Sink);
		const bool bReqWas = PM->IsExchangeRequestActive();
		auto SetWin = [TS](bool b) { TS->SetExchangeWindowOpen(b, TEXT("самотест")); };
		auto LocTxt = [GI](int32 Id) { return AsdaNotice::LocRaw(GI, Id); };

		// ═══ ПД-2: ход клавишами `Party_CanInviteCheck` (9568) ═══
		if (Pc)
		{
			PM->DebugSetExchangeRequestActive(true); SetWin(false);
			Pc->KbRefuseShownLoc = 0; Sink.Reset();
			const bool bA = Pc->KbCanMoveCheck();
			Expect(bA && Sink.ChatHits(LocTxt(9568)) == 0, TEXT("ПД-2 клавиши: заявка в полёте, окна нет"),
				FString::Printf(TEXT("ход разрешён=%d, строк 9568=%d (ждём 1/0: у эталона заявка ход не блокирует)"), bA ? 1 : 0, Sink.ChatHits(LocTxt(9568))));
			PM->DebugSetExchangeRequestActive(false); SetWin(true);
			Pc->KbRefuseShownLoc = 0; Sink.Reset();
			const bool bB = Pc->KbCanMoveCheck();
			Expect(!bB && Sink.ChatHits(LocTxt(9568)) == 1, TEXT("ПД-2 клавиши: окно обмена открыто (контроль)"),
				FString::Printf(TEXT("ход разрешён=%d, строк 9568=%d (ждём 0/1)"), bB ? 1 : 0, Sink.ChatHits(LocTxt(9568))));
			ExpectColor(GI, TEXT("цвет 9568 (клавиши) = −11514"), 9568, FColor(0xFF, 0xD3, 0x06, 0xFF));
			SetWin(false); Pc->KbRefuseShownLoc = 0; Sink.Reset();
			const bool bC = Pc->KbCanMoveCheck();
			Expect(bC, TEXT("ПД-2 клавиши: ни заявки, ни окна (база)"), FString::Printf(TEXT("ход разрешён=%d"), bC ? 1 : 0));
		}
		else { Expect(false, TEXT("ПД-2 клавиши"), TEXT("нет AAsdaPlayerController")); }

		// ═══ ПД-2: ремесло `uiCheckPartyInviteInvLockGuard` (5590) ═══
		if (UAsdaCraftWindowSubsystem* Cw = GI->GetSubsystem<UAsdaCraftWindowSubsystem>())
		{
			if (Cw->IsOpen()) { Cw->HideAndCleanup(); }
			PM->DebugSetExchangeRequestActive(false); SetWin(true); Sink.Reset();
			const int32 J0 = Cw->GetJournal().Num();
			const bool bT = Cw->GateAndToggle(TEXT("самотест торговли: окно открыто"));
			bool bJ = false;
			for (int32 i = J0; i < Cw->GetJournal().Num(); ++i) { if (Cw->GetJournal()[i].Contains(TEXT("гейт 5590"))) { bJ = true; } }
			Expect(!bT && !Cw->IsOpen() && bJ && Sink.ChatHits(LocTxt(5590)) == 1, TEXT("ПД-2 ремесло: окно обмена открыто"),
				FString::Printf(TEXT("тумблер=%d открыто=%d журнал 5590=%d строк 5590=%d (ждём 0/0/1/1)"), bT ? 1 : 0, Cw->IsOpen() ? 1 : 0, bJ ? 1 : 0, Sink.ChatHits(LocTxt(5590))));
			ExpectColor(GI, TEXT("цвет 5590 (ремесло) = −6881324"), 5590, FColor(0x96, 0xFF, 0xD4, 0xFF));
			PM->DebugSetExchangeRequestActive(true); SetWin(false); Sink.Reset();
			const int32 J1 = Cw->GetJournal().Num();
			Cw->GateAndToggle(TEXT("самотест торговли: заявка в полёте"));
			bool bJ2 = false;
			for (int32 i = J1; i < Cw->GetJournal().Num(); ++i) { if (Cw->GetJournal()[i].Contains(TEXT("гейт 5590"))) { bJ2 = true; } }
			const bool bOpened = Cw->IsOpen();
			Expect(!bJ2 && Sink.ChatHits(LocTxt(5590)) == 0 && bOpened, TEXT("ПД-2 ремесло: заявка в полёте (контроль)"),
				FString::Printf(TEXT("журнал 5590=%d строк 5590=%d окно 76000 открыто=%d (ждём 0/0/1: у эталона заявка ремесло не блокирует)"), bJ2 ? 1 : 0, Sink.ChatHits(LocTxt(5590)), bOpened ? 1 : 0));
			if (Cw->IsOpen()) { Cw->HideAndCleanup(); }
			PM->DebugSetExchangeRequestActive(false);
		}
		else { Expect(false, TEXT("ПД-2 ремесло"), TEXT("нет подсистемы окна 76000")); }

		// ═══ ПД-1: сообщество `communityUI_Open` (107022 — лавка, не аукцион) ═══
		UAsdaWindowSubsystem* WM = GI->GetSubsystem<UAsdaWindowSubsystem>();
		if (UAsdaCommunitySubsystem* Cm = GI->GetSubsystem<UAsdaCommunitySubsystem>())
		{
			if (Cm->IsWindowOpen()) { Cm->CloseWindow(); }
			// Аукцион открыт (то самое условие, что прежний порт ошибочно считал «обменом»).
			UUserWidget* Auc = WM ? WM->OpenWindowByPath(FName(TEXT("EXCHANGE_WIN")),
				TEXT("/Game/AsdaAssets/UI/Player_UI/Trade/WBP_EXCHANGE_WIN"), 0, ESlateVisibility::SelfHitTestInvisible, false) : nullptr;
			const bool bAucVis = WM && WM->IsWindowVisible(FName(TEXT("EXCHANGE_WIN")));
			Sink.Reset();
			Cm->OpenOnTab(0);  /* ★ R5-fix6-c (13.11.2026): вкладку 0 ставит сам тест штатным путём (хоткей 35), а не берёт оставшуюся от соседа пачки */
			const bool bOpenA = Cm->IsWindowOpen();
			Expect(Auc && bAucVis && bOpenA && Sink.ChatHits(LocTxt(107022)) == 0, TEXT("ПД-1 сообщество: открыт аукцион EXCHANGE_WIN"),
				FString::Printf(TEXT("аукцион виден=%d, сообщество открыто=%d, строк 107022=%d (ждём 1/1/0: аукциона в условии эталона нет)"), bAucVis ? 1 : 0, bOpenA ? 1 : 0, Sink.ChatHits(LocTxt(107022))));
			if (Cm->IsWindowOpen()) { Cm->CloseWindow(); }
			if (WM) { WM->CloseWindow(FName(TEXT("EXCHANGE_WIN"))); }
			// Лавка: подготовка 22000.
			TS->SetSellWindowOpen(true, TEXT("самотест")); Sink.Reset();
			Cm->OpenOnTab(0);   // (то же)
			const bool bOpenB = Cm->IsWindowOpen();
			Expect(!bOpenB && Sink.ChatHits(LocTxt(107022)) == 1, TEXT("ПД-1 сообщество: подготовка лавки 22000 (контроль)"),
				FString::Printf(TEXT("открыто=%d, строк 107022=%d (ждём 0/1)"), bOpenB ? 1 : 0, Sink.ChatHits(LocTxt(107022))));
			ExpectColor(GI, TEXT("цвет 107022 (сообщество) = −11514"), 107022, FColor(0xFF, 0xD3, 0x06, 0xFF));
			ExpectColor(GI, TEXT("цвет 107022 не белый категории Social (контроль прежнего маршрута)"), 107022, FColor(0xFF, 0xFF, 0xFF, 0xFF), false);
			TS->SetSellWindowOpen(false, TEXT("самотест"));
			// Лавка: витрина 23000 (второе слагаемое условия).
			TS->SetShopDlgOpen(true, TEXT("самотест")); Sink.Reset();
			Cm->OpenOnTab(0);   // (то же)
			const bool bOpenC = Cm->IsWindowOpen();
			Expect(!bOpenC && Sink.ChatHits(LocTxt(107022)) == 1, TEXT("ПД-1 сообщество: витрина лавки 23000"),
				FString::Printf(TEXT("открыто=%d, строк 107022=%d (ждём 0/1)"), bOpenC ? 1 : 0, Sink.ChatHits(LocTxt(107022))));
			TS->SetShopDlgOpen(false, TEXT("самотест"));
			// Окно обмена 2008 условию 107022 не принадлежит.
			SetWin(true); Sink.Reset();
			Cm->OpenOnTab(0);   // (то же)
			const bool bOpenD = Cm->IsWindowOpen();
			Expect(bOpenD && Sink.ChatHits(LocTxt(107022)) == 0, TEXT("ПД-1 сообщество: окно обмена 2008 (не читатель)"),
				FString::Printf(TEXT("открыто=%d, строк 107022=%d (ждём 1/0)"), bOpenD ? 1 : 0, Sink.ChatHits(LocTxt(107022))));
			if (Cm->IsWindowOpen()) { Cm->CloseWindow(); }
			SetWin(false);
		}
		else { Expect(false, TEXT("ПД-1 сообщество"), TEXT("нет подсистемы 6400")); }

		// ═══ Меню игрока 2004: 5042 (окно) против 5054 (заявка) ═══
		//   ★ 29.09.2026 (волна О1): обработчик 2004/2005 и его нотисы — модель обмена `UAsdaExchangeP2PSubsystem` (след — у неё).
		UAsdaExchangeP2PSubsystem* P2P = GI->GetSubsystem<UAsdaExchangeP2PSubsystem>();
		if (P2P)
		{
			SetWin(true); PM->DebugSetExchangeRequestActive(false);
			const int32 S0 = P2P->GetNoticeSeq();
			PM->ActionExchange(false);
			const int32 LA = P2P->GetLastNoticeLoc(); const bool bNewA = P2P->GetNoticeSeq() > S0;
			Expect(bNewA && LA == 5042, TEXT("меню 2004: окно обмена открыто"), FString::Printf(TEXT("нотис %d (ждём 5042, shop_trade.c:6631)"), bNewA ? LA : -1));
			ExpectColor(GI, TEXT("цвет 5042 (меню 2004) = −75279"), 5042, FColor(0xFE, 0xD9, 0xF1, 0xFF));
			SetWin(false); PM->DebugSetExchangeRequestActive(true);
			const int32 S1 = P2P->GetNoticeSeq();
			PM->ActionExchange(false);
			const int32 LB = P2P->GetLastNoticeLoc(); const bool bNewB = P2P->GetNoticeSeq() > S1;
			Expect(bNewB && LB == 5054, TEXT("меню 2004: заявка в полёте (контроль)"), FString::Printf(TEXT("нотис %d (ждём 5054)"), bNewB ? LB : -1));
			// ★ 29.09.2026 (волна О1): 5054 у эталона тоже −75279 (`CExchange_SendRequest`, shop_trade.c:7908) — прежняя «контрольная
			//   пара» ревизии С-1 держалась на неточности порта (5054 шёл цветом категории). Контроль цвета — пара 5058 ниже.
			ExpectColor(GI, TEXT("цвет 5054 = −75279 (shop_trade.c:7908)"), 5054, FColor(0xFE, 0xD9, 0xF1, 0xFF));
			PM->DebugSetExchangeRequestActive(false);
			// Меню 2000 (`cPopup_ShowClampedToScreen`): ПКМ-нажатие и отпускание над игроком 777 при открытом окне → 5042, меню не показано.
			SetWin(true);
			const int32 S2 = PM->GetNoticeSeq();
			PM->NotifyRightPress(777);
			const bool bShown = PM->NotifyRightRelease(777, false);
			const bool bNewC = PM->GetNoticeSeq() > S2;
			Expect(!bShown && !PM->IsMenuOpen() && bNewC && PM->GetLastNoticeLoc() == 5042, TEXT("меню 2000: окно обмена открыто"),
				FString::Printf(TEXT("показано=%d открыто=%d нотис %d (ждём 0/0/5042)"), bShown ? 1 : 0, PM->IsMenuOpen() ? 1 : 0, bNewC ? PM->GetLastNoticeLoc() : -1));
			ExpectColor(GI, TEXT("цвет 5042 (меню 2000) = −75279"), 5042, FColor(0xFE, 0xD9, 0xF1, 0xFF));
			SetWin(false);
		}
		else { Expect(false, TEXT("меню 2004"), TEXT("нет подсистемы обмена")); }

		// ═══ Приём 5283 при открытом окне: молча (эталон 5284/3) против 5058 ═══
		if (UAsdaNetSubsystem* Net = AsdaTestNet::Get(GI))
		{
			TArray<uint8> B; B.SetNumZeroed(24);
			B[0] = 1; B[1] = 0x09; B[2] = 0x03;   // код 1, userNum инициатора 777 (не я)
			// ★ 29.09.2026 (волна О1): приём 5283 — модель обмена; пакет копится колбэком и разбирается её `Tick` (`DebugDrainNow`).
			SetWin(true); Sink.Reset();
			const int32 S0 = P2P ? P2P->GetNoticeSeq() : 0;
			AsdaTestNet::DebugFeedPacket(Net, 5283, B);
			if (P2P) { P2P->DebugDrainNow(); }
			const bool bNoNotice = P2P && P2P->GetNoticeSeq() == S0;
			Expect(bNoNotice && Sink.LogHits(TEXT("при открытом окне обмена 2008")) == 1, TEXT("5283: окно обмена открыто"),
				FString::Printf(TEXT("новых нотисов=%d, строк ветки=%d (ждём 0/1)"), P2P ? P2P->GetNoticeSeq() - S0 : -1, Sink.LogHits(TEXT("при открытом окне обмена 2008"))));
			SetWin(false); Sink.Reset();
			const int32 S1 = P2P ? P2P->GetNoticeSeq() : 0;
			AsdaTestNet::DebugFeedPacket(Net, 5283, B);
			if (P2P) { P2P->DebugDrainNow(); }
			const int32 L1 = P2P ? P2P->GetLastNoticeLoc() : -1;
			Expect(P2P && P2P->GetNoticeSeq() > S1 && L1 == 5058, TEXT("5283: окно закрыто (контроль)"), FString::Printf(TEXT("нотис %d (ждём 5058)"), (P2P && P2P->GetNoticeSeq() > S1) ? L1 : -1));
			// Контрольная пара цвета: 5058 — 0xFF78FF3C (`Recv_PartyMember`, network.c:27353), НЕ розовый −75279.
			ExpectColor(GI, TEXT("цвет 5058 = 0xFF78FF3C"), 5058, FColor(0x78, 0xFF, 0x3C, 0xFF));
			ExpectColor(GI, TEXT("цвет 5058 ≠ −75279 (контроль)"), 5058, FColor(0xFE, 0xD9, 0xF1, 0xFF), false);
			if (P2P) { P2P->ResetSessionState(EAsdaSessionResetReason::MapChanged); }   // снять входящую заявку и шкалу

			// ═══ 9913 «лот купили»: золото откладывается при открытом окне ═══
			if (UAsdaExchangeSubsystem* Ex = GI->GetSubsystem<UAsdaExchangeSubsystem>())
			{
				TArray<uint8> G; G.SetNumZeroed(48);
				const int32 Gold = 123456;
				FMemory::Memcpy(&G[39], &Gold, 4);
				SetWin(true);
				AsdaTestNet::DebugFeedPacket(Net, 9913, G);
				const int32 D1 = Ex->GetDeferredSaleGold();
				const bool bFlushed = Ex->FlushDeferredSaleGold(TEXT("самотест"));
				Expect(D1 == Gold && bFlushed && Ex->GetDeferredSaleGold() == 0, TEXT("9913: окно обмена открыто — золото отложено"),
					FString::Printf(TEXT("отложено=%d, вывод=%d, после вывода=%d (ждём %d/1/0)"), D1, bFlushed ? 1 : 0, Ex->GetDeferredSaleGold(), Gold));
				SetWin(false);
				AsdaTestNet::DebugFeedPacket(Net, 9913, G);
				Expect(Ex->GetDeferredSaleGold() == 0, TEXT("9913: окно закрыто (контроль)"), FString::Printf(TEXT("отложено=%d (ждём 0)"), Ex->GetDeferredSaleGold()));

				// ═══ Режимы аукциона `itemMediationOpenTab` ═══
				SetWin(true);
				const bool bP1 = Ex->PassesOpenGates(4);
				SetWin(false);
				const bool bP0 = Ex->PassesOpenGates(4);
				Expect(!bP1 && bP0, TEXT("аукцион режим 4: окно открыто / закрыто"), FString::Printf(TEXT("пропуск=%d/%d (ждём 0/1)"), bP1 ? 1 : 0, bP0 ? 1 : 0));
			}
		}

		// ═══ Хотбар `Hotbar_SlotActivate` ═══
		if (const UAsdaHotbarSubsystem* Hb = GI->GetSubsystem<UAsdaHotbarSubsystem>())
		{
			FString W1, W0;
			SetWin(true);  const bool b1 = Hb->IsActivationBlockedForCheck(W1);
			SetWin(false); const bool b0 = Hb->IsActivationBlockedForCheck(W0);
			Expect(b1 && W1.Contains(TEXT("обмена 2008")) && !W0.Contains(TEXT("обмена")), TEXT("хотбар: окно открыто / закрыто"),
				FString::Printf(TEXT("блок=%d «%s» / блок=%d «%s»"), b1 ? 1 : 0, *W1, b0 ? 1 : 0, *W0));
		}

		// ═══ Рюкзак `uiOpenInventoryWindow` ═══
		if (UAsdaInventorySubsystem* Inv = GI->GetSubsystem<UAsdaInventorySubsystem>())
		{
			const bool bInvWas = Inv->IsWindowOpen();
			if (bInvWas) { Inv->CloseWindow(); }
			SetWin(true);  Inv->OpenWindow(); const bool b1 = Inv->IsWindowOpen();
			SetWin(false); Inv->OpenWindow(); const bool b0 = Inv->IsWindowOpen();
			Expect(!b1 && b0, TEXT("рюкзак: окно открыто / закрыто"), FString::Printf(TEXT("открыт=%d/%d (ждём 0/1)"), b1 ? 1 : 0, b0 ? 1 : 0));
			if (!bInvWas && Inv->IsWindowOpen()) { Inv->CloseWindow(); }
		}

		// ═══ Разговор с NPC `NPC_OpenWindow` (несуществующий npcId: после гейтов ветка всё равно выходит) ═══
		if (UAsdaNpcDialogSubsystem* Dlg = GI->GetSubsystem<UAsdaNpcDialogSubsystem>())
		{
			static const TCHAR* kNeedle = TEXT("открыто окно обмена 2008 — разговор молча не открыт");
			SetWin(true);  Sink.Reset(); Dlg->OpenForNpcId(-77); const int32 h1 = Sink.LogHits(kNeedle);
			SetWin(false); Sink.Reset(); Dlg->OpenForNpcId(-77); const int32 h0 = Sink.LogHits(kNeedle);
			Expect(h1 == 1 && h0 == 0, TEXT("NPC: окно открыто / закрыто"), FString::Printf(TEXT("строк гейта %d/%d (ждём 1/0)"), h1, h0));
		}

		// ═══ Клик по земле `Input_WorldClickHandler` (9568) ═══
		// Свой персонаж — `GetChar()`; офлайн его создаёт `Asda.World.FakeSelfSpawn` в консольной команде.
		const AActor* SelfChar = Pc ? (const AActor*)Pc->GetChar() : nullptr;   // после офлайн-спавна (см. консольную команду)
		if (SelfChar)
		{
			static const TCHAR* kNeedle = TEXT("[клик] окно обмена 2008 открыто");
			const FVector Here = SelfChar->GetActorLocation();
			SetWin(true); Sink.Reset();
			const double T0 = Pc->LastMoveSendTime;
			Pc->GroundClickMove(Here);
			const int32 h1 = Sink.LogHits(kNeedle); const int32 c1 = Sink.ChatHits(LocTxt(9568));
			const bool bNoMove = Pc->LastMoveSendTime == T0;
			SetWin(false); Sink.Reset();
			Pc->LastMoveSendTime = 0.0;
			Pc->GroundClickMove(Here);
			const int32 h0 = Sink.LogHits(kNeedle);
			const bool bPassed = Pc->LastMoveSendTime != 0.0;   // контроль: ветка дошла до троттла 150 мс
			Expect(h1 == 1 && c1 == 1 && bNoMove && h0 == 0 && bPassed, TEXT("клик по земле: окно открыто / закрыто"),
				FString::Printf(TEXT("строк гейта %d, чата 9568 %d, троттл не тронут=%d / строк гейта %d, дошёл до троттла=%d (ждём 1/1/1/0/1)"),
					h1, c1, bNoMove ? 1 : 0, h0, bPassed ? 1 : 0));
		}
		else { Expect(false, TEXT("клик по земле"), TEXT("нет своего персонажа (GetChar)")); }
		ExpectColor(GI, TEXT("цвет 9568 (клик) = −11514"), 9568, FColor(0xFF, 0xD3, 0x06, 0xFF));

		// ═══ Магазин снаряжения 6947 / элитный магазин 6947 / парикмахерская (молча) — только ветка «окно открыто»:
		//   ветка «закрыто» открывает окна с тяжёлыми побочными эффектами (снимок окон, модели) — её здесь не зовём. ═══
		SetWin(true);
		if (UAsdaEquipShopSubsystem* Es = GI->GetSubsystem<UAsdaEquipShopSubsystem>())
		{
			const bool bWas = Es->IsOpen();
			Sink.Reset(); if (!bWas) { Es->OpenWindow(); }
			Expect(bWas || (!Es->IsOpen() && Sink.ChatHits(LocTxt(6947)) == 1), TEXT("магазин снаряжения: окно обмена открыто"),
				FString::Printf(TEXT("открыт=%d, строк 6947=%d (ждём 0/1)"), Es->IsOpen() ? 1 : 0, Sink.ChatHits(LocTxt(6947))));
			if (!bWas) { ExpectColor(GI, TEXT("цвет 6947 (магазин снаряжения) = −6881324"), 6947, FColor(0x96, 0xFF, 0xD4, 0xFF)); }
			if (!bWas) { ExpectColor(GI, TEXT("цвет 6947 ≠ янтарного категории Trade (контроль прежнего маршрута)"), 6947, FColor(0xFF, 0xAD, 0x00, 0xFF), false); }
		}
		if (UAsdaPremiumShopSubsystem* Ps = GI->GetSubsystem<UAsdaPremiumShopSubsystem>())
		{
			const bool bWas = Ps->IsOpen();
			Sink.Reset(); if (!bWas) { Ps->OpenWindow(false); }
			Expect(bWas || (!Ps->IsOpen() && Sink.ChatHits(LocTxt(6947)) == 1), TEXT("элитный магазин: окно обмена открыто"),
				FString::Printf(TEXT("открыт=%d, строк 6947=%d (ждём 0/1)"), Ps->IsOpen() ? 1 : 0, Sink.ChatHits(LocTxt(6947))));
			if (!bWas) { ExpectColor(GI, TEXT("цвет 6947 (элитный магазин) = −6881324"), 6947, FColor(0x96, 0xFF, 0xD4, 0xFF)); }
		}
		if (UAsdaHairShopSubsystem* Hs = GI->GetSubsystem<UAsdaHairShopSubsystem>())
		{
			const bool bWas = Hs->IsSessionOpen();
			const bool bR = bWas ? false : Hs->OpenWindow();
			Expect(bWas || (!bR && !Hs->IsSessionOpen()), TEXT("парикмахерская: окно обмена открыто"),
				FString::Printf(TEXT("открыто=%d (ждём 0, молча)"), Hs->IsSessionOpen() ? 1 : 0));
		}
		SetWin(false);

		// ═══ «Закрыть всё при уходе» и сброс сессии ═══
		{
			SetWin(true);  const bool r1 = TS->OnCloseAllWindowsOnLeave(TEXT("самотест")); const bool f1 = UAsdaTradeStateSubsystem::IsExchangeWindowOpen(GI);
			SetWin(false); const bool r0 = TS->OnCloseAllWindowsOnLeave(TEXT("самотест"));
			Expect(r1 && !f1 && !r0, TEXT("закрыть всё при уходе"), FString::Printf(TEXT("было открыто=%d, после=%d / закрытое=%d (ждём 1/0/0)"), r1 ? 1 : 0, f1 ? 1 : 0, r0 ? 1 : 0));
			SetWin(true); TS->SetSellWindowOpen(true, TEXT("самотест")); TS->SetShopDlgOpen(true, TEXT("самотест"));
			TS->ResetSessionState(EAsdaSessionResetReason::MapChanged);
			Expect(!UAsdaTradeStateSubsystem::IsExchangeWindowOpen(GI) && !UAsdaTradeStateSubsystem::IsSelfStoreOpen(GI),
				TEXT("сброс сессии (смена карты)"), TS->DebugString());
		}

		// Восстановление.
		TS->ResetSessionState(EAsdaSessionResetReason::MapChanged);
		if (UAsdaExchangeP2PSubsystem* P2PEnd = GI->GetSubsystem<UAsdaExchangeP2PSubsystem>()) { P2PEnd->ResetSessionState(EAsdaSessionResetReason::MapChanged); }
		PM->DebugSetExchangeRequestActive(bReqWas);
		if (Pc) { Pc->KbRefuseShownLoc = 0; }
		GLog->RemoveOutputDevice(&Sink);
		UE_LOG(LogAsdaTradeTest, Log, TEXT("САМОТЕСТ-ТОРГОВЛЯ ИТОГ: ok=%d fail=%d"), Ok, Fail);
		AsdaSelfTestKit::Quit(W, bQuit);
	}
};

namespace
{
	FAutoConsoleCommandWithWorldAndArgs GAsdaTradeSelfTestGates(
		TEXT("asda.Trade.SelfTestGates"),
		TEXT("asda.Trade.SelfTestGates [delay=N] [quit] — самотест читателей признаков торговли (ПД-1 сообщество 107022, ПД-2 клавиши 9568 и "
		     "ремесло 5590, меню 5042, 5283, хотбар, аукцион, 9913, рюкзак, NPC, клик 9568, магазины 6947) с контрольными парами. Только офлайн."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
		{
			const FAsdaSelfTestArgs KitArgs = FAsdaSelfTestArgs::Parse(Args);   // ★ 11.10.2026 (В3 S1): каркас — отказ с ИТОГ и quit, второй барьер на все опкоды
			if (AsdaSelfTestKit::RefuseStart(W, TEXT("asda.Trade.SelfTestGates"), KitArgs)) { return; }
			const float Delay = AsdaSelfTestKit::DelayArg(Args);
			const bool bQuit = Args.Contains(TEXT("quit"));
			TWeakObjectPtr<UWorld> WW(W);
			// Офлайн своего павна нет: сначала офлайн-спавн штатными крюками (как `asda.Net.SelfTestSitStand`), проверки — через 2 с.
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WW, bQuit](float) -> bool
			{
				UWorld* Wl = WW.Get();
				if (!Wl) { return false; }
				if (AsdaSelfTestKit::RefuseDeferred(Wl, TEXT("asda.Trade.SelfTestGates"), bQuit)) { return false; }   // Р-2: офлайн повторно — на отложенном старте
				const AAsdaPlayerController* P = Cast<AAsdaPlayerController>(Wl->GetFirstPlayerController());
				const bool bNeedSpawn = !P || !P->GetPawn();
				if (bNeedSpawn && GEngine)
				{
					GEngine->Exec(Wl, TEXT("Asda.Load.CurrentMap 3"));
					GEngine->Exec(Wl, TEXT("Asda.World.FakeSelfSpawn 256 256"));
				}
				FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WW, bQuit](float) -> bool
				{
					if (UWorld* W2 = WW.Get())
					{
						FAsdaTradeGatesSelfTest T;
						T.Run(W2, bQuit);
					}
					return false;   // разовый запуск
				}), bNeedSpawn ? 2.f : 0.f);
				return false;
			}), Delay);
			UE_LOG(LogAsdaTradeTest, Log, TEXT("САМОТЕСТ-ТОРГОВЛЯ: запуск через %.1f с"), Delay);
		}));
}

#endif // !UE_BUILD_SHIPPING
