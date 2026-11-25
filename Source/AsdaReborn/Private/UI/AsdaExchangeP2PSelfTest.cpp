// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ ОБМЕНА ИГРОК-ИГРОК, ВОЛНА О1 (29.09.2026, `Reports/trade/wave_O1.md`). [ПОРТ]
//
// ЧТО ПРОВЕРЯЕТ. Модель `CExchange` (`UAsdaExchangeP2PSubsystem`) БОЕВЫМИ путями: входящие пакеты 5283/5285/5287 — через
//   штатный `UAsdaNetSubsystem::HandlePacket` (`DebugFeedPacket`) и `Tick` подсистемы; исходящие 5282/5284/5286 — БОЕВЫЕ
//   отправители под перехватом (`ArmSendCapture`, в сеть ничего не уходит), тела сверяются побайтно с эталоном. Входы —
//   настоящие: пункт меню 2004 жмётся своим `OnClicked` (`ClickByNodeName`), функции 202/203 — `PlayEmotion`, клавиши
//   57/58 — `RunCommandForCheck`, ответ на вопрос 1520 — `AnswerMsgBox` очереди окон-сообщений.
//   У каждой проверки — КОНТРОЛЬНАЯ ПАРА (закон pixel-metric-needs-control-pairs): противоположный вход обязан дать
//   противоположный исход (гейт есть / нет, дефект исправлен / как эталон, 29 с / 31 с, 32 с / 34 с и т. д.).
//
// Консоль: `asda.Trade.SelfTestExchangeO1 [delay=N] [quit]`. Только офлайн (`FAsdaOutgoingGate::RefuseDebugWhenOnline`).
//   Строки — «САМОТЕСТ-О1», итог — «САМОТЕСТ-О1 ИТОГ: ok=… fail=…». Свой userNum, душа, ключи, модель, очередь сообщений и
//   перехватчик восстанавливаются в конце.
#include "CoreMinimal.h"
#include "AsdaTestPacketsP2P.h"   // ★ R5-c (12.11.2026): общая точка самотестов вместо своих копий помощников

#if !UE_BUILD_SHIPPING

#include "UI/AsdaExchangeP2PSubsystem.h"
#include "AsdaCVarSave.h"                   // ★ ИСПРАВЛЕНИЕ 11.10.2026 (В0, N1(д)/U8): ключи консоли самотеста — запомнить и вернуть (значение + SetBy)
#include "UI/AsdaTradeStateSubsystem.h"
#include "UI/AsdaPlayerMenuSubsystem.h"
#include "UI/AsdaHotbarSubsystem.h"
#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaInventorySubsystem.h"
#include "UI/AsdaCharaInfoSubsystem.h"
#include "UI/AsdaNotice.h"
#include "World/AsdaWorldSubsystem.h"
#include "World/AsdaPlayerController.h"
#include "World/AsdaPossessGate.h"
#include "Login/AsdaLoginData.h"
#include "Test/AsdaSelfTestNet.h"   // ★ R1: тонкий доступ самотестов к сети (вместо AsdaNetSubsystem.h)
#include "Test/AsdaSelfTestKit.h"   // ★ 11.10.2026 (В3 S1): общий каркас самотестов (аргументы, отказ, перехват, итог, выход)
#include "AsdaBytes.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "HAL/IConsoleManager.h"
#include "Misc/OutputDevice.h"
#include "Misc/OutputDeviceRedirector.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaP2PTest, Log, All);

namespace AsdaP2PTestLocal
{
	// Перехватчик лога: все строки (для строк-веток подсистемы).
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
		// ★ Лог доставляется с фонового потока — перед чтением и очисткой `GLog->Flush()` (урок волны 0).
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
	// Тело 5285 (50 б): [0] код, [1] тип, [2..5] windowIndex, [6..7] userNum 1, [8..27] имя 1, [28..29] userNum 2, [30..49] имя 2.
	using AsdaTestPacketsP2P::Body5285;   // ★ R5-c (12.11.2026): общая точка (`AsdaTestPacketsP2P.h`) — копия была дословной.
}

struct FAsdaExchangeP2PSelfTest
{
	int32 Ok = 0;     // пройдено
	int32 Fail = 0;   // провалено
	TArray<AsdaP2PTestLocal::FCap> Caps;   // пойманные исходящие

	// Одна проверка: «САМОТЕСТ-О1 ok/ПРОВАЛ: <имя> — <подробности>».
	void Expect(bool bCond, const FString& Name, const FString& Detail)
	{
		if (bCond) { ++Ok; } else { ++Fail; }
		UE_LOG(LogAsdaP2PTest, Log, TEXT("САМОТЕСТ-О1 %s: %s — %s"), bCond ? TEXT("ok") : TEXT("ПРОВАЛ"), *Name, *Detail);
	}
	// Сколько пойманных пакетов с опкодом.
	int32 Count(uint16 Op) const { int32 N = 0; for (const auto& C : Caps) { if (C.Op == Op) { ++N; } } return N; }
	// Последний пойманный пакет с опкодом (nullptr — нет).
	const AsdaP2PTestLocal::FCap* Last(uint16 Op) const { for (int32 i = Caps.Num() - 1; i >= 0; --i) { if (Caps[i].Op == Op) { return &Caps[i]; } } return nullptr; }
	// Hex payload для подробностей.
	static FString Hex(const AsdaP2PTestLocal::FCap* C)
	{
		if (!C) { return TEXT("—"); }
		FString S; for (uint8 B : C->P) { S += FString::Printf(TEXT("%02X"), B); }
		return S;
	}
	// Payload побайтно равен ожидаемому.
	static bool Same(const AsdaP2PTestLocal::FCap* C, const TArray<uint8>& Want) { return C && C->P == Want; }

	void Run(UWorld* W, bool bQuit)
	{
		using namespace AsdaP2PTestLocal;
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaExchangeP2PSubsystem* X = GI ? GI->GetSubsystem<UAsdaExchangeP2PSubsystem>() : nullptr;
		UAsdaNetSubsystem* Net = GI ? AsdaTestNet::Get(GI) : nullptr;
		UAsdaTradeStateSubsystem* TS = GI ? GI->GetSubsystem<UAsdaTradeStateSubsystem>() : nullptr;
		UAsdaPlayerMenuSubsystem* PM = GI ? GI->GetSubsystem<UAsdaPlayerMenuSubsystem>() : nullptr;
		UAsdaHotbarSubsystem* Hb = GI ? GI->GetSubsystem<UAsdaHotbarSubsystem>() : nullptr;
		UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
		UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
		UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
		UAsdaCharaInfoSubsystem* CI = GI ? GI->GetSubsystem<UAsdaCharaInfoSubsystem>() : nullptr;
		AAsdaPlayerController* Pc = W ? Cast<AAsdaPlayerController>(W->GetFirstPlayerController()) : nullptr;
		if (!X || !Net || !TS || !PM || !Hb || !WM || !WS || !Inv || !CI || !Pc)
		{
			UE_LOG(LogAsdaP2PTest, Error, TEXT("САМОТЕСТ-О1 ИТОГ: ok=0 fail=1 (нет подсистем/контроллера)"));
			AsdaSelfTestKit::Quit(W, bQuit);
			return;
		}
		FSink Sink;
		GLog->AddOutputDevice(&Sink);
		// ── подготовка: свой userNum 500, два фейковых соседа, панель хотбара, ключи ──
		const uint16 SelfNumWas = AsdaTestNet::SelfUserNum(Net); const bool bSelfKnownWas = AsdaTestNet::bSelfUserNumKnown(Net);
		AsdaTestNet::SelfUserNum(Net) = 500; AsdaTestNet::bSelfUserNumKnown(Net) = true;
		constexpr int32 kPeerA = 901;   // цель моих заявок
		constexpr int32 kPeerB = 902;   // инициатор входящих
		constexpr int32 kPeerC = 903;   // второй инициатор (авто-«занят»)
		WS->DebugSpawnFakePeer(kPeerA, TEXT("Партнёр"), FVector2D(25700.f, 25700.f));
		WS->DebugSpawnFakePeer(kPeerB, TEXT("Инициатор"), FVector2D(25710.f, 25700.f));
		Hb->DumpNoticeIcons();   // поднимает панель значков уведомлений (в мире она поднимается лениво)
		IConsoleVariable* CvFix = IConsoleManager::Get().FindConsoleVariable(TEXT("asda.Trade.P2PFixDefects"));
		IConsoleVariable* CvSame = IConsoleManager::Get().FindConsoleVariable(TEXT("asda.PlayerMenu.RequireSamePress"));
		// ★ ИСПРАВЛЕНИЕ 11.10.2026 (В0, N1(д)/U8): исходное P2PFixDefects (значение + SetBy) запоминает `AsdaCVarSave` (прежде — FixWas).
		auto SetFix = [CvFix](int32 V) { if (CvFix) { AsdaCVarSave::SetForTest(TEXT("asda.Trade.P2PFixDefects"), V); } };
		SetFix(1);
		(void)CvSame;
		const uint32 Gen = AsdaSelfTestKit::ArmCapture(Net, [this](uint16 Op, const uint8* P, int32 Len, bool, TCHAR)
		{
			FCap& C = Caps.AddDefaulted_GetRef();
			C.Op = Op;
			C.P.Append(P, Len);
		}, 0, TEXT("asda.Trade.SelfTestExchangeO1"));
		auto Clean = [&]()
		{
			X->ResetSessionState(EAsdaSessionResetReason::LeftWorld);
			WM->ClearMsgBoxQueue(TEXT("самотест О1"));
			Caps.Reset();
			Sink.Reset();
			if (Inv->IsWindowOpen()) { Inv->CloseWindow(); }
			if (PM->IsMenuOpen()) { PM->CloseMenu(); }
		};
		auto Feed = [&](int32 Op, const TArray<uint8>& B) { AsdaTestNet::DebugFeedPacket(Net, Op, B); X->Tick(0.f); };   // штатный приём + `Tick`
		auto LocTxt = [GI](int32 Id) { return AsdaNotice::LocRaw(GI, Id); };
		auto WantBody = [](std::initializer_list<uint8> L) { return TArray<uint8>(L); };

		// ═══ A. Меню 2004: ПКМ по соседу → пункт своим `OnClicked` → 5282; повтор → 5054 (контроль) ═══
		Clean();
		{
			PM->NotifyRightPress(kPeerA);
			const bool bShown = PM->NotifyRightRelease(kPeerA, false);
			const int32 S0 = X->GetNoticeSeq();
			PM->ClickByNodeName(TEXT("TRANTOOL_BUT04"));
			const FCap* C = Last(5282);
			Expect(bShown && Count(5282) == 1 && Same(C, WantBody({ 0x85, 0x03, 0x00 })) && X->IsRequestActive() && X->IsRequestTimerActive()
				&& X->GetLastNoticeLoc() == 20090 && X->GetLastNoticeSlot() == TEXT("Партнёр") && X->GetNoticeSeq() == S0 + 1 && !PM->IsMenuOpen(),
				TEXT("A1 меню 2004 → 5282"), FString::Printf(TEXT("меню показано=%d, 5282×%d тело %s (ждём 850300), +0x14=%d +0x38=%d, нотис %d «%s», меню открыто=%d"),
					bShown ? 1 : 0, Count(5282), *Hex(C), X->IsRequestActive() ? 1 : 0, X->IsRequestTimerActive() ? 1 : 0, X->GetLastNoticeLoc(), *X->GetLastNoticeSlot(), PM->IsMenuOpen() ? 1 : 0));
			Expect(X->TimeoutTargetNum == kPeerA, TEXT("A2 +0x2708 = цель ПКМ (set_o9C2)"), FString::Printf(TEXT("%d (ждём %d)"), X->TimeoutTargetNum, kPeerA));
			PM->NotifyRightPress(kPeerA); PM->NotifyRightRelease(kPeerA, false);
			PM->ClickByNodeName(TEXT("TRANTOOL_BUT05"));
			Expect(Count(5282) == 1 && X->GetLastNoticeLoc() == 5054, TEXT("A3 повтор при заявке в полёте → 5054 (контроль)"),
				FString::Printf(TEXT("5282×%d (ждём 1), нотис %d (ждём 5054)"), Count(5282), X->GetLastNoticeLoc()));
		}
		// ═══ B. Гейт меню «рюкзак открыт» → 5032, заявки нет, модель сброшена; без рюкзака — 5282 (контроль) ═══
		Clean();
		{
			Inv->OpenWindow();
			const bool bInv = Inv->IsWindowOpen();
			PM->NotifyRightPress(kPeerA); PM->NotifyRightRelease(kPeerA, false);
			PM->ClickByNodeName(TEXT("TRANTOOL_BUT04"));
			Expect(bInv && Count(5282) == 0 && X->GetLastNoticeLoc() == 5032 && !PM->IsMenuOpen() && !X->IsRequestActive(),
				TEXT("B1 гейт меню: рюкзак открыт → 5032"), FString::Printf(TEXT("рюкзак=%d, 5282×%d, нотис %d, меню=%d"), bInv ? 1 : 0, Count(5282), X->GetLastNoticeLoc(), PM->IsMenuOpen() ? 1 : 0));
			Inv->CloseWindow();
			PM->NotifyRightPress(kPeerA); PM->NotifyRightRelease(kPeerA, false);
			PM->ClickByNodeName(TEXT("TRANTOOL_BUT04"));
			Expect(Count(5282) == 1, TEXT("B2 рюкзак закрыт → 5282 (контроль)"), FString::Printf(TEXT("5282×%d"), Count(5282)));
			// Окно обмена открыто → 5042 и меню ОСТАЁТСЯ (гейты меню пройдены).
			X->DebugSetRequestActive(false);
			TS->SetExchangeWindowOpen(true, TEXT("самотест О1"));
			const int32 N = Count(5282);
			// Меню показывается только без окна: ставим окно после показа.
			TS->SetExchangeWindowOpen(false, TEXT("самотест О1"));
			PM->NotifyRightPress(kPeerA); PM->NotifyRightRelease(kPeerA, false);
			TS->SetExchangeWindowOpen(true, TEXT("самотест О1"));
			PM->ClickByNodeName(TEXT("TRANTOOL_BUT04"));
			Expect(Count(5282) == N && X->GetLastNoticeLoc() == 5042 && PM->IsMenuOpen(), TEXT("B3 окно обмена открыто → 5042, меню остаётся"),
				FString::Printf(TEXT("новых 5282=%d, нотис %d, меню открыто=%d (ждём 0/5042/1)"), Count(5282) - N, X->GetLastNoticeLoc(), PM->IsMenuOpen() ? 1 : 0));
			TS->SetExchangeWindowOpen(false, TEXT("самотест О1"));
		}
		// ═══ C. Таймаут 33 с → 5286(−2, userNum); 32 с — ничего (контроль); 5287 при «закрыл я» игнорируется ═══
		Clean();
		{
			PM->NotifyRightPress(kPeerA); PM->NotifyRightRelease(kPeerA, false);
			PM->ClickByNodeName(TEXT("TRANTOOL_BUT04"));
			X->RequestStartReal = FPlatformTime::Seconds() - 32.0; X->Tick(0.f);
			const int32 N32 = Count(5286);
			X->RequestStartReal = FPlatformTime::Seconds() - 34.0; X->Tick(0.f);
			const FCap* C = Last(5286);
			Expect(N32 == 0 && Count(5286) == 1 && Same(C, WantBody({ 0xFE, 0xFF, 0xFF, 0xFF, 0x85, 0x03 })) && !X->IsRequestActive() && X->GetLastNoticeLoc() == 5053,
				TEXT("C1 таймаут 33 с → 5286(−2) + 5053"), FString::Printf(TEXT("на 32 с 5286×%d (ждём 0), на 34 с ×%d тело %s (ждём FEFFFFFF8503), +0x14=%d, нотис %d"),
					N32, Count(5286), *Hex(C), X->IsRequestActive() ? 1 : 0, X->GetLastNoticeLoc()));
			const int32 S = X->GetNoticeSeq();
			Feed(5287, TArray<uint8>());
			Expect(X->GetNoticeSeq() == S && X->bClosedByMe == 1, TEXT("C2 5287 после своего закрытия (+0x08=1) — игнор"),
				FString::Printf(TEXT("новых нотисов %d (ждём 0)"), X->GetNoticeSeq() - S));
			X->InvItemListDialogInit(TEXT("самотест"));
			Feed(5287, TArray<uint8>());
			Expect(X->GetLastNoticeLoc() == 5040, TEXT("C3 5287 при +0x08=0 → 5040 (контроль)"), FString::Printf(TEXT("нотис %d"), X->GetLastNoticeLoc()));
		}
		// ═══ D. Входящая 5283 → 5048 + 5058 цветом, значок 4, шкала; эхо своей — пропуск (контроль) ═══
		Clean();
		{
			const int32 S = X->GetNoticeSeq();
			Feed(5283, Body5283(1, kPeerB, TEXT("Инициатор"), 0));
			const bool bIcon = Hb->HasNoticeIcon(4);
			Expect(X->GetNoticeSeq() == S + 2 && X->GetLastNoticeLoc() == 5058 && bIcon && X->IsRequestActive() && X->IsIncomingGaugeActive()
				&& X->GetRole() == 0 && X->InitiatorNum == kPeerB && X->HotbarNoticeName == TEXT("Инициатор") && Sink.Hits(TEXT("звук 215")) == 1,
				TEXT("D1 5283 код 1 → 5048+5058, значок 4, шкала, звук 215"), FString::Printf(TEXT("нотисов +%d (ждём 2), последний %d, значок 4=%d, +0x14=%d +0x1C=%d роль=%d +0x3E=%d имя «%s», строк «звук 215» %d"),
					X->GetNoticeSeq() - S, X->GetLastNoticeLoc(), bIcon ? 1 : 0, X->IsRequestActive() ? 1 : 0, X->IsIncomingGaugeActive() ? 1 : 0, X->GetRole(), X->InitiatorNum, *X->HotbarNoticeName, Sink.Hits(TEXT("звук 215"))));
			Clean();
			const int32 S2 = X->GetNoticeSeq();
			X->MyNum = 500;   // +0x3C = я (как после своей заявки)
			Feed(5283, Body5283(1, 500, TEXT("Я"), 0));
			Expect(X->GetNoticeSeq() == S2 && !Hb->HasNoticeIcon(4), TEXT("D2 эхо своей заявки (+0x3C == +0x3E) — пропуск (контроль)"),
				FString::Printf(TEXT("нотисов +%d, значок 4=%d (ждём 0/0)"), X->GetNoticeSeq() - S2, Hb->HasNoticeIcon(4) ? 1 : 0));
		}
		// ═══ E. Вторая заявка при висящем значке → 5284/3; Д15: поля первой заявки целы; при `FixDefects 0` — сброшены (контроль) ═══
		Clean();
		{
			Feed(5283, Body5283(1, kPeerB, TEXT("Инициатор"), 0));
			const int32 S = X->GetNoticeSeq();
			Feed(5283, Body5283(1, kPeerC, TEXT("Третий"), 1));
			const FCap* C = Last(5284);
			Expect(Count(5284) == 1 && Same(C, WantBody({ 0x03, 0x87, 0x03, 0x01 })) && X->GetNoticeSeq() == S && X->IsIncomingGaugeActive() && X->IsRequestActive() && X->InitiatorNum == kPeerB,
				TEXT("E1 вторая заявка при значке → 5284/3; первая цела [ПОРТ, Д15]"), FString::Printf(TEXT("5284×%d тело %s (ждём 03870301), нотисов +%d, +0x1C=%d +0x14=%d +0x3E=%d"),
					Count(5284), *Hex(C), X->GetNoticeSeq() - S, X->IsIncomingGaugeActive() ? 1 : 0, X->IsRequestActive() ? 1 : 0, X->InitiatorNum));
			SetFix(0);
			Feed(5283, Body5283(1, kPeerC, TEXT("Третий"), 1));
			Expect(Count(5284) == 2 && !X->IsIncomingGaugeActive() && !X->IsRequestActive(), TEXT("E2 то же при FixDefects 0 — поля сброшены, как эталон (контроль)"),
				FString::Printf(TEXT("5284×%d, +0x1C=%d +0x14=%d (ждём 2/0/0)"), Count(5284), X->IsIncomingGaugeActive() ? 1 : 0, X->IsRequestActive() ? 1 : 0));
			SetFix(1);
			// Окно обмена открыто (без значка) → тот же авто-ответ (Т0-3).
			Clean();
			TS->SetExchangeWindowOpen(true, TEXT("самотест О1"));
			Feed(5283, Body5283(1, kPeerC, TEXT("Третий"), 0));
			Expect(Count(5284) == 1 && !Hb->HasNoticeIcon(4) && Sink.Hits(TEXT("при открытом окне обмена 2008")) == 1, TEXT("E3 окно открыто → 5284/3 без значка"),
				FString::Printf(TEXT("5284×%d, значок=%d"), Count(5284), Hb->HasNoticeIcon(4) ? 1 : 0));
			TS->SetExchangeWindowOpen(false, TEXT("самотест О1"));
		}
		// ═══ F. Значок → вопрос 1520 (5001, кнопки 5003/5004, шкала); «Отказать» → 5284/0; «Принять» → 5284/1 (контроль) ═══
		Clean();
		{
			Feed(5283, Body5283(1, kPeerB, TEXT("Инициатор"), 0));
			// ★ Ревизия О1 (МЕЛК-4): клик по значку — путём хотбара (`HandleNoticeIconClick` = тело `TickNoticeClick` после
			//   хит-теста): гейт 5042 при открытом окне, делегат, снятие значка. Сначала — пара «окно обмена открыто».
			TS->SetExchangeWindowOpen(true, TEXT("самотест О1"));
			const int32 S5042 = X->GetNoticeSeq();
			const bool bClick0 = Hb->HandleNoticeIconClick(4);
			const bool bGateOk = bClick0 && Hb->HasNoticeIcon(4) && !WM->IsMsgBoxQueued(5, 5001) && Sink.Hits(TEXT("нотис 5042, значок остаётся")) == 1;
			TS->SetExchangeWindowOpen(false, TEXT("самотест О1"));
			Expect(bGateOk && X->GetNoticeSeq() == S5042, TEXT("F0 значок при открытом окне 2008 → 5042 (хотбар), значок висит, вопроса нет (контроль к F1)"),
				FString::Printf(TEXT("клик=%d значок=%d вопрос=%d строк «5042» %d"), bClick0 ? 1 : 0, Hb->HasNoticeIcon(4) ? 1 : 0, WM->IsMsgBoxQueued(5, 5001) ? 1 : 0,
					Sink.Hits(TEXT("нотис 5042, значок остаётся"))));
			Hb->HandleNoticeIconClick(4);   // `uiNoticeButtonHandler` → `uiNoticeButtonDispatch` + `buffSlotCompact`
			const bool bQ = WM->IsMsgBoxQueued(5, 5001);
			const FString Top = WM->GetMsgBoxQueueTopText();
			Expect(bQ && Top.StartsWith(TEXT("Инициатор ")) && Top.Contains(LocTxt(5001).Left(12)), TEXT("F1 значок 4 → TWOBUT (5, 5001) «имя + 5001»"),
				FString::Printf(TEXT("в очереди=%d, текст «%s»"), bQ ? 1 : 0, *Top));
			WM->ResetMsgBoxSfxCounts();
			WM->AnswerMsgBox(true, false);
			const FCap* C0 = Last(5284);
			Expect(Count(5284) == 1 && Same(C0, WantBody({ 0x00, 0x86, 0x03, 0x00 })) && !X->IsIncomingGaugeActive() && !X->IsRequestActive() && !WM->IsMsgBoxQueued(5, 5001)
				&& WM->GetMsgBoxSfxCount(5) >= 1, TEXT("F2 «Отказать» → 5284/0, звук 5"), FString::Printf(TEXT("5284×%d тело %s (ждём 00860300), +0x1C=%d, запись=%d, звук 5 ×%d"),
					Count(5284), *Hex(C0), X->IsIncomingGaugeActive() ? 1 : 0, WM->IsMsgBoxQueued(5, 5001) ? 1 : 0, WM->GetMsgBoxSfxCount(5)));
			Clean();
			const int32 S5 = X->GetNoticeSeq();
			Feed(5283, Body5283(1, kPeerB, TEXT("Инициатор"), 1));
			// ★ Ревизия О1 (МЕЛК-4): входящая ТИПА 1 — 5049 + 5059, значок 5 (контроль к D1 с 5048/5058/значком 4).
			Expect(X->GetNoticeSeq() == S5 + 2 && X->GetLastNoticeLoc() == 5059 && Hb->HasNoticeIcon(5) && !Hb->HasNoticeIcon(4) && Sink.Hits(TEXT("нотис 5049")) >= 1,
				TEXT("F3a 5283 тип 1 → 5049 + 5059, значок 5"), FString::Printf(TEXT("нотисов +%d, последний %d, значок 5=%d 4=%d"),
					X->GetNoticeSeq() - S5, X->GetLastNoticeLoc(), Hb->HasNoticeIcon(5) ? 1 : 0, Hb->HasNoticeIcon(4) ? 1 : 0));
			Hb->HandleNoticeIconClick(5);
			WM->AnswerMsgBox(true, true);
			const FCap* C1 = Last(5284);
			Expect(Count(5284) == 1 && Same(C1, WantBody({ 0x01, 0x86, 0x03, 0x01 })) && X->bAcceptSent == 1 && !X->IsIncomingGaugeActive(),
				TEXT("F3 «Принять» (тип 1) → 5284/1 (контроль)"), FString::Printf(TEXT("5284×%d тело %s (ждём 01860301), +0x18=%d"), Count(5284), *Hex(C1), X->bAcceptSent));
			// Гейт «Принять»: рюкзак открыт → 5032 и 5284/0 (не 5284/1).
			Clean();
			Feed(5283, Body5283(1, kPeerB, TEXT("Инициатор"), 0));
			Inv->OpenWindow();   // до показа вопроса: при модальном 1520 рюкзак не открывается (110644)
			Hb->HandleNoticeIconClick(4);
			WM->AnswerMsgBox(true, true);
			const FCap* C2 = Last(5284);
			Expect(Inv->IsWindowOpen() && Count(5284) == 1 && C2 && C2->P.Num() == 4 && C2->P[0] == 0 && X->GetLastNoticeLoc() == 5032,
				TEXT("F4 «Принять» при открытом рюкзаке → 5032 + 5284/0"), FString::Printf(TEXT("5284×%d подкод %d, нотис %d"), Count(5284), C2 && C2->P.Num() ? (int32)C2->P[0] : -1, X->GetLastNoticeLoc()));
			Inv->CloseWindow();
		}
		// ═══ G. Шкала 30 с от ПРИХОДА: 29 с — ничего; 31 с — 5284/0 и значок снят [ПОРТ, Д5/Д6]; при FixDefects 0 — значок остаётся ═══
		Clean();
		{
			Feed(5283, Body5283(1, kPeerB, TEXT("Инициатор"), 0));
			X->IncomingGaugeStartReal = FPlatformTime::Seconds() - 29.0; X->Tick(0.f);
			const int32 N29 = Count(5284);
			X->IncomingGaugeStartReal = FPlatformTime::Seconds() - 31.0; X->Tick(0.f);
			Expect(N29 == 0 && Count(5284) == 1 && Last(5284)->P[0] == 0 && !Hb->HasNoticeIcon(4) && !X->IsIncomingGaugeActive(),
				TEXT("G1 шкала: 29 с — ничего, 31 с — 5284/0 и значок снят [ПОРТ, Д5/Д6]"), FString::Printf(TEXT("29 с 5284×%d, 31 с ×%d, значок=%d"), N29, Count(5284), Hb->HasNoticeIcon(4) ? 1 : 0));
			Clean();
			SetFix(0);
			Feed(5283, Body5283(1, kPeerB, TEXT("Инициатор"), 0));
			X->IncomingGaugeStartReal = FPlatformTime::Seconds() - 31.0; X->Tick(0.f);
			Expect(Count(5284) == 0 && Hb->HasNoticeIcon(4) && X->IsIncomingGaugeActive(), TEXT("G2 то же при FixDefects 0 (вопрос не открыт) — как эталон: ответа нет, значок висит (контроль)"),
				FString::Printf(TEXT("5284×%d, значок=%d, +0x1C=%d (ждём 0/1/1)"), Count(5284), Hb->HasNoticeIcon(4) ? 1 : 0, X->IsIncomingGaugeActive() ? 1 : 0));
			// ★ Ревизия О1 (МЕЛК-3): ключ 0, вопрос в очереди, но НАВЕРХУ чужое окно — эталон по `displayNoticeId` не отвечает;
			//   контроль — свой вопрос наверху → 5284/0.
			Clean();
			Feed(5283, Body5283(1, kPeerB, TEXT("Инициатор"), 0));
			Hb->HandleNoticeIconClick(4);
			WM->ShowGameConfirm(TEXT("Самотест О1: чужой вопрос сверху"), [](bool) {}, 0.f, 2288, 2289, 7778, 1);
			X->IncomingGaugeStartReal = FPlatformTime::Seconds() - 31.0; X->Tick(0.f);
			const int32 NUnder = Count(5284);
			WM->RemoveMsgBoxEntries(1, 7778, TEXT("самотест О1"));
			X->IncomingGaugeStartReal = FPlatformTime::Seconds() - 31.0; X->Tick(0.f);
			Expect(NUnder == 0 && Count(5284) == 1 && Last(5284)->P[0] == 0, TEXT("G3 ключ 0: свой вопрос ПОД чужим — ответа нет; наверху — 5284/0 (МЕЛК-3)"),
				FString::Printf(TEXT("под чужим 5284×%d (ждём 0), наверху ×%d (ждём 1)"), NUnder, Count(5284)));
			SetFix(1);
			// ★ Ревизия О1 (МЕЛК-2): вторая заявка при идущей шкале (вопрос открыт кликом) шкалу НЕ перезапускает;
			//   контроль — после ответа шкала стоит, новая заявка берёт старт заново.
			Clean();
			Feed(5283, Body5283(1, kPeerB, TEXT("Инициатор"), 0));
			Hb->HandleNoticeIconClick(4);
			const double Start = FPlatformTime::Seconds() - 10.0;
			X->IncomingGaugeStartReal = Start;
			Feed(5283, Body5283(1, kPeerC, TEXT("Третий"), 0));
			const bool bKept = X->IncomingGaugeStartReal == Start && X->IsIncomingGaugeActive();
			WM->AnswerMsgBox(true, false);   // «Отказать» — шкала стоп
			Hb->RemoveNoticeIcon(4);          // значок второй заявки (иначе третья уйдёт в авто-«занят»)
			X->IncomingGaugeStartReal = Start;   // шкала стоит (+0x1C = 0), старое значение старта — новая заявка обязана его сбросить
			Feed(5283, Body5283(1, kPeerC, TEXT("Третий"), 0));
			// Сброс старта (−1) тот же `Tick` сразу превращает в «сейчас» (старт берётся в первом тике после +0x1C = 1).
			const double NewStart = X->IncomingGaugeStartReal;
			Expect(bKept && NewStart > Start + 5.0 && X->IsIncomingGaugeActive(), TEXT("G4 вторая заявка при идущей шкале — старт не сброшен; после ответа — новый старт (МЕЛК-2)"),
				FString::Printf(TEXT("старт сохранён=%d, после ответа старт моложе прежнего на %.1f с (ждём ≈10)"), bKept ? 1 : 0, NewStart - Start));
		}
		// ═══ H. 5285 у инициатора: коды 0/2/3; у цели: 2 (5036), 3 (молча) ═══
		auto MakeRequest = [&]() { PM->NotifyRightPress(kPeerA); PM->NotifyRightRelease(kPeerA, false); PM->ClickByNodeName(TEXT("TRANTOOL_BUT04")); };
		Clean();
		{
			MakeRequest(); Feed(5285, Body5285(0, 0, -1, 0, TEXT(""), 0, TEXT("")));
			Expect(X->GetLastNoticeLoc() == 20091 && X->GetLastNoticeSlot() == TEXT("Партнёр") && !X->IsRequestTimerActive() && !X->IsRequestActive(),
				TEXT("H1 инициатор 5285 код 0 → 20091 «Партнёр»"), FString::Printf(TEXT("нотис %d «%s», +0x38=%d"), X->GetLastNoticeLoc(), *X->GetLastNoticeSlot(), X->IsRequestTimerActive() ? 1 : 0));
			Clean(); MakeRequest(); Feed(5285, Body5285(2, 0, -1, 0, TEXT(""), 0, TEXT("")));
			Expect(X->GetLastNoticeLoc() == 5006, TEXT("H2 инициатор код 2 → 5006"), FString::Printf(TEXT("нотис %d"), X->GetLastNoticeLoc()));
			Clean(); MakeRequest(); Feed(5285, Body5285(3, 0, -1, 0, TEXT(""), 0, TEXT("")));
			Expect(X->GetLastNoticeLoc() == 5074, TEXT("H3 инициатор код 3 → 5074"), FString::Printf(TEXT("нотис %d"), X->GetLastNoticeLoc()));
			Clean(); Feed(5283, Body5283(1, kPeerB, TEXT("Инициатор"), 0)); Feed(5285, Body5285(2, 0, -1, 0, TEXT(""), 0, TEXT("")));
			Expect(X->GetLastNoticeLoc() == 5036, TEXT("H4 цель код 2 → 5036"), FString::Printf(TEXT("нотис %d"), X->GetLastNoticeLoc()));
			Clean(); Feed(5283, Body5283(1, kPeerB, TEXT("Инициатор"), 0));
			const int32 S = X->GetNoticeSeq();
			Feed(5285, Body5285(3, 0, -1, 0, TEXT(""), 0, TEXT("")));
			Expect(X->GetNoticeSeq() == S && X->IsRequestActive(), TEXT("H5 цель код 3 — молча, заявка цела (контроль к H3)"),
				FString::Printf(TEXT("нотисов +%d, +0x14=%d (ждём 0/1)"), X->GetNoticeSeq() - S, X->IsRequestActive() ? 1 : 0));
		}
		// ═══ I. 5285 код 1: окно открыто, стороны/индекс разобраны, вопрос закрыт со звуком 5 (W6-4); отмена 2012 → 5286(idx) ═══
		Clean();
		{
			Feed(5283, Body5283(1, kPeerB, TEXT("Инициатор"), 0));
			Hb->OnNoticeIconClicked.Broadcast(4); Hb->RemoveNoticeIcon(4);
			WM->AnswerMsgBox(true, true);
			// Чужой вопрос (вид 1, id 7777) в очереди: эталон `CGameUI_CloseWindow(5)` спрятал бы его без ответа (Д12), порт — нет.
			WM->ShowGameConfirm(TEXT("Самотест О1: чужой вопрос"), [](bool) {}, 0.f, 2288, 2289, 7777, 1);
			WM->ResetMsgBoxSfxCounts();
			Caps.Reset();
			Feed(5285, Body5285(1, 0, 42, kPeerB, TEXT("Инициатор"), 500, TEXT("Я")));
			const bool bOpen = UAsdaTradeStateSubsystem::IsExchangeWindowOpen(GI);
			Expect(bOpen && X->GetWindowIndex() == 42 && X->GetExchangeType() == 0 && X->PartnerNum1 == kPeerB && X->PartnerNum2 == 500 && X->SideName1 == TEXT("Инициатор")
				&& !X->IsRequestActive() && !X->IsIncomingGaugeActive() && WM->GetMsgBoxSfxCount(5) >= 1 && Count(5286) == 0 && WM->IsMsgBoxQueued(1, 7777),
				TEXT("I1 5285 код 1 → окно 2008 открыто (признак), #42, стороны, звук 5 (W6-4), чужой вопрос цел [ПОРТ, Д12]"),
				FString::Printf(TEXT("окно=%d #%d тип %d стороны %d/%d «%s», +0x14=%d, звук 5 ×%d, 5286×%d, чужой вопрос=%d"),
					bOpen ? 1 : 0, X->GetWindowIndex(), X->GetExchangeType(), X->PartnerNum1, X->PartnerNum2, *X->SideName1, X->IsRequestActive() ? 1 : 0, WM->GetMsgBoxSfxCount(5), Count(5286),
					WM->IsMsgBoxQueued(1, 7777) ? 1 : 0));
			X->Tick(0.f);   // О2: временной отмены без окна больше нет — окно держится, 5286 не уходит
			Expect(UAsdaTradeStateSubsystem::IsExchangeWindowOpen(GI) && X->IsWindowShown() && Count(5286) == 0, TEXT("I2 окно 2008 показано и держится после тика, 5286 нет (мера О1-1 снята)"),
				FString::Printf(TEXT("признак=%d окно показано=%d 5286×%d"), UAsdaTradeStateSubsystem::IsExchangeWindowOpen(GI) ? 1 : 0, X->IsWindowShown() ? 1 : 0, Count(5286)));
			X->CloseByCancelButton(TEXT("самотест: кнопка 2012"));
			const FCap* C = Last(5286);
			Expect(Count(5286) == 1 && Same(C, WantBody({ 42, 0, 0, 0, 0, 0 })) && !UAsdaTradeStateSubsystem::IsExchangeWindowOpen(GI) && X->GetLastNoticeLoc() == 5041 && X->bClosedByMe == 0,
				TEXT("I3 отмена 2012 → 5286(#42) + 5041, окно закрыто"), FString::Printf(TEXT("5286×%d тело %s (ждём 2A0000000000), нотис %d, +0x08=%d"),
					Count(5286), *Hex(C), X->GetLastNoticeLoc(), X->bClosedByMe));
			// Контроль к I3: окно открыто, 5287 от партнёра — окно закрыто без своего 5286 (ревизия О1, СРЕДН-1: закрытие шлёт
			//   только одна сторона).
			Caps.Reset();
			Feed(5285, Body5285(1, 1, 43, kPeerB, TEXT("Инициатор"), 500, TEXT("Я")));
			const bool bBefore = UAsdaTradeStateSubsystem::IsExchangeWindowOpen(GI) && X->IsWindowShown();
			X->Tick(0.f);
			Feed(5287, TArray<uint8>());
			Expect(bBefore && !UAsdaTradeStateSubsystem::IsExchangeWindowOpen(GI) && !X->IsWindowShown() && Count(5286) == 0 && X->GetLastNoticeLoc() == 5040,
				TEXT("I4 5287 от партнёра → окно закрыто, 5040, своего 5286 нет (одна сторона шлёт закрытие)"),
				FString::Printf(TEXT("до=%d, после признак=%d окно=%d, 5286×%d, нотис %d"), bBefore ? 1 : 0, UAsdaTradeStateSubsystem::IsExchangeWindowOpen(GI) ? 1 : 0,
					X->IsWindowShown() ? 1 : 0, Count(5286), X->GetLastNoticeLoc()));
		}
		// ═══ J. Гейт открытия (рюкзак) после 5285/1: 5032 и 5286(idx) [ПОРТ, Д2]; при FixDefects 0 — 5286 нет (контроль) ═══
		Clean();
		{
			Feed(5283, Body5283(1, kPeerB, TEXT("Инициатор"), 0));
			Inv->OpenWindow();
			Caps.Reset();
			Feed(5285, Body5285(1, 0, 44, kPeerB, TEXT("Инициатор"), 500, TEXT("Я")));
			Expect(!UAsdaTradeStateSubsystem::IsExchangeWindowOpen(GI) && X->GetLastNoticeLoc() == 5032 && Count(5286) == 1 && Last(5286)->P[0] == 44,
				TEXT("J1 гейт открытия «рюкзак» → 5032 + 5286(#44) [ПОРТ, Д2]"), FString::Printf(TEXT("окно=%d, нотис %d, 5286×%d"), UAsdaTradeStateSubsystem::IsExchangeWindowOpen(GI) ? 1 : 0, X->GetLastNoticeLoc(), Count(5286)));
			Clean(); SetFix(0);
			Feed(5283, Body5283(1, kPeerB, TEXT("Инициатор"), 0));
			Inv->OpenWindow(); Caps.Reset();
			Feed(5285, Body5285(1, 0, 45, kPeerB, TEXT("Инициатор"), 500, TEXT("Я")));
			Expect(X->GetLastNoticeLoc() == 5032 && Count(5286) == 0, TEXT("J2 то же при FixDefects 0 — 5286 нет, как эталон (контроль)"), FString::Printf(TEXT("5286×%d"), Count(5286)));
			SetFix(1);
			Inv->CloseWindow();
		}
		// ═══ K. «Закрыть всё при уходе»: окно открыто → 5286(idx) [ПОРТ, Д1]; при FixDefects 0 — 5284/0 (контроль) ═══
		Clean();
		{
			Feed(5283, Body5283(1, kPeerB, TEXT("Инициатор"), 0));
			Feed(5285, Body5285(1, 0, 46, kPeerB, TEXT("Инициатор"), 500, TEXT("Я")));
			Caps.Reset();
			const bool r = TS->OnCloseAllWindowsOnLeave(TEXT("самотест О1"));
			Expect(r && Count(5286) == 1 && Last(5286)->P[0] == 46 && Count(5284) == 0 && !UAsdaTradeStateSubsystem::IsExchangeWindowOpen(GI),
				TEXT("K1 закрыть всё при уходе → 5286(#46) [ПОРТ, Д1]"), FString::Printf(TEXT("5286×%d 5284×%d окно=%d"), Count(5286), Count(5284), UAsdaTradeStateSubsystem::IsExchangeWindowOpen(GI) ? 1 : 0));
			Clean(); SetFix(0);
			Feed(5283, Body5283(1, kPeerB, TEXT("Инициатор"), 0));
			Feed(5285, Body5285(1, 0, 47, kPeerB, TEXT("Инициатор"), 500, TEXT("Я")));
			// Эталон: `SendWindowCancel` берёт +0x3E; после 5285 он жив у цели (сбросы 5285/1 его не трогают).
			Caps.Reset();
			TS->OnCloseAllWindowsOnLeave(TEXT("самотест О1"));
			Expect(Count(5284) == 1 && Last(5284)->P[0] == 0 && Count(5286) == 0, TEXT("K2 то же при FixDefects 0 → 5284/0, как эталон (контроль)"),
				FString::Printf(TEXT("5284×%d 5286×%d"), Count(5284), Count(5286)));
			SetFix(1);
		}
		// ═══ L. Функции 202/203 и клавиши 57/58: выбранная цель → 5282 тип 0/1, +0x2708 = цель [ПОРТ, Д11]; без цели — ничего ═══
		Clean();
		{
			AActor* PeerA = WS->GetPlayerActorByUserNum(kPeerA);
			Pc->TargetActor = PeerA;
			CI->PlayEmotion(UAsdaCharaInfoSubsystem::MakeEmotionCode(2, 2));
			const FCap* C = Last(5282);
			Expect(PeerA && Count(5282) == 1 && Same(C, WantBody({ 0x85, 0x03, 0x00 })) && X->TimeoutTargetNum == kPeerA && X->GetLastNoticeLoc() == 20090,
				TEXT("L1 функция 202 → 5282 тип 0, +0x2708 = цель [ПОРТ, Д11]"), FString::Printf(TEXT("сосед=%d 5282×%d тело %s, +0x2708=%d"), PeerA ? 1 : 0, Count(5282), *Hex(C), X->TimeoutTargetNum));
			// ★ Ревизия О1 (МЕЛК-4): контрольная пара Д11 — при ключе 0 эталон +0x2708 не пишет.
			Clean(); Pc->TargetActor = PeerA; SetFix(0);
			CI->PlayEmotion(UAsdaCharaInfoSubsystem::MakeEmotionCode(2, 2));
			Expect(Count(5282) == 1 && X->TimeoutTargetNum == -1, TEXT("L1b то же при FixDefects 0 — +0x2708 не записан, как эталон (контроль к L1)"),
				FString::Printf(TEXT("5282×%d, +0x2708=%d (ждём 1/−1)"), Count(5282), X->TimeoutTargetNum));
			SetFix(1);
			Clean(); Pc->TargetActor = PeerA;
			Pc->RunCommandForCheck(58);
			const FCap* C58 = Last(5282);
			Expect(Count(5282) == 1 && Same(C58, WantBody({ 0x85, 0x03, 0x01 })), TEXT("L2 клавиша 58 (B) → 5282 тип 1"), FString::Printf(TEXT("5282×%d тело %s"), Count(5282), *Hex(C58)));
			Clean(); Pc->TargetActor = nullptr;
			Pc->RunCommandForCheck(57);
			Expect(Count(5282) == 0, TEXT("L3 клавиша 57 без цели — ничего (контроль)"), FString::Printf(TEXT("5282×%d"), Count(5282)));
			Clean(); Pc->TargetActor = PeerA;
			Pc->RunCommandForCheck(57);
			Expect(Count(5282) == 1 && Last(5282)->P[2] == 0, TEXT("L4 клавиша 57 (N) → 5282 тип 0"), FString::Printf(TEXT("5282×%d"), Count(5282)));
			Pc->TargetActor = nullptr;
		}
		// ═══ M. 5283 отказ сервера инициатору: код 4 → 5010, заявка снята; код 12 → 5064 ═══
		Clean();
		{
			MakeRequest(); Feed(5283, Body5283(4, 500, TEXT(""), 0));
			Expect(X->GetLastNoticeLoc() == 5010 && !X->IsRequestActive() && !X->IsRequestTimerActive(), TEXT("M1 5283 код 4 → 5010, заявка снята"),
				FString::Printf(TEXT("нотис %d, +0x14=%d +0x38=%d"), X->GetLastNoticeLoc(), X->IsRequestActive() ? 1 : 0, X->IsRequestTimerActive() ? 1 : 0));
			Clean(); MakeRequest(); Feed(5283, Body5283(12, 500, TEXT(""), 0));
			Expect(X->GetLastNoticeLoc() == 5064, TEXT("M2 5283 код 12 → 5064 (loc по обработчику)"), FString::Printf(TEXT("нотис %d"), X->GetLastNoticeLoc()));
			// ★ Ревизия О1 (МЕЛК-4): контроль к M1 — эхо своей заявки (код 1, userNum = я) заявку не снимает и молчит.
			Clean(); MakeRequest();
			const int32 SM = X->GetNoticeSeq();
			Feed(5283, Body5283(1, 500, TEXT("Я"), 0));
			Expect(X->IsRequestActive() && X->IsRequestTimerActive() && X->GetNoticeSeq() == SM, TEXT("M3 эхо своей заявки (код 1) — заявка цела, молча (контроль к M1)"),
				FString::Printf(TEXT("+0x14=%d +0x38=%d нотисов +%d"), X->IsRequestActive() ? 1 : 0, X->IsRequestTimerActive() ? 1 : 0, X->GetNoticeSeq() - SM));
		}
		// ═══ N. Гейт «души» 5284 (kSoulGatedOpcodes) — пара 5284 / 4006 (ход — гейта у сервера нет) и душа 1 / 0.
		//   ★ 30.09.2026 (О3): прежний контроль 5291 с волны О3 сам в таблице (5288/5290 — с О2) — заменён на 4006. ═══
		{
			const uint8 SoulWas = AsdaTestNet::SelfSoulState(Net);
			const bool bG84 = AsdaSoul::IsGatedOpcode(5284), bG88 = AsdaSoul::IsGatedOpcode(4006);
			AsdaTestNet::SetSelfSoulState(Net, 1); const bool bB1 = AsdaSoul::BlockOutgoing(Net, 5284);
			AsdaTestNet::SetSelfSoulState(Net, 0); const bool bB0 = AsdaSoul::BlockOutgoing(Net, 5284);
			AsdaTestNet::SetSelfSoulState(Net, SoulWas);
			Expect(bG84 && !bG88 && bB1 && !bB0, TEXT("N1 душа: 5284 в таблице, ход 4006 нет (контроль); блок при душе 1 / нет при 0"),
				FString::Printf(TEXT("таблица 5284=%d 4006=%d, блок душа1=%d душа0=%d (ждём 1/0/1/0)"), bG84 ? 1 : 0, bG88 ? 1 : 0, bB1 ? 1 : 0, bB0 ? 1 : 0));
		}
		// ═══ O. Сброс сессии: входящая заявка, значок и вопрос сняты молча; ответа серверу нет ═══
		Clean();
		{
			Feed(5283, Body5283(1, kPeerB, TEXT("Инициатор"), 0));
			Hb->OnNoticeIconClicked.Broadcast(4);   // вопрос на экране, значок НЕ снят (клик мимо хотбара — намеренно)
			// ★ Поправка к комментарию (ревизия О1, МЕЛК-4): второй 5283 уходит в авто-«занят» 5284/3 именно потому, что первый
			//   значок висит; до сброса остаётся ровно первый значок и открытый вопрос.
			Feed(5283, Body5283(1, kPeerB, TEXT("Инициатор"), 0));
			const bool bIconBefore = Hb->HasNoticeIcon(4);
			Caps.Reset();
			X->ResetSessionState(EAsdaSessionResetReason::MapChanged);
			Expect(bIconBefore && !Hb->HasNoticeIcon(4) && !WM->IsMsgBoxQueued(5, 5001) && !X->IsRequestActive() && !X->IsIncomingGaugeActive() && Caps.Num() == 0,
				TEXT("O1 сброс сессии (смена карты): значок, вопрос, шкала сняты, пакетов нет"), FString::Printf(TEXT("значок=%d вопрос=%d +0x14=%d пакетов %d"),
					Hb->HasNoticeIcon(4) ? 1 : 0, WM->IsMsgBoxQueued(5, 5001) ? 1 : 0, X->IsRequestActive() ? 1 : 0, Caps.Num()));
		}

		// ── восстановление ──
		Clean();
		AsdaSelfTestKit::DisarmCapture(Net, Gen);
		AsdaTestNet::SelfUserNum(Net) = SelfNumWas; AsdaTestNet::bSelfUserNumKnown(Net) = bSelfKnownWas;
		AsdaCVarSave::RestoreAll(TEXT("asda.Trade.SelfTestExchangeO1"));   // ★ ИСПРАВЛЕНИЕ 11.10.2026 (В0, N1(д)/U8): ключ — значение + SetBy (прежде — с SetByConsole)
		Pc->TargetActor = nullptr;
		GLog->RemoveOutputDevice(&Sink);
		UE_LOG(LogAsdaP2PTest, Log, TEXT("САМОТЕСТ-О1 ИТОГ: ok=%d fail=%d"), Ok, Fail);
		AsdaSelfTestKit::Quit(W, bQuit);
	}
};

namespace
{
	FAutoConsoleCommandWithWorldAndArgs GAsdaTradeSelfTestExchangeO1(
		TEXT("asda.Trade.SelfTestExchangeO1"),
		TEXT("asda.Trade.SelfTestExchangeO1 [delay=N] [quit] — самотест обмена игрок-игрок (волна О1): 5282/5284/5286 под перехватом, "
		     "5283/5285/5287 через штатный приём, меню 2004, функции 202/203, клавиши 57/58, вопрос 1520, шкала 30 с, таймаут 33 с, "
		     "гейты, исправленные дефекты (пары с asda.Trade.P2PFixDefects 0), душа 5284, сброс сессии. Только офлайн."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
		{
			const FAsdaSelfTestArgs KitArgs = FAsdaSelfTestArgs::Parse(Args);   // ★ 11.10.2026 (В3 S1): каркас — отказ с ИТОГ и quit, второй барьер на все опкоды
			if (AsdaSelfTestKit::RefuseStart(W, TEXT("asda.Trade.SelfTestExchangeO1"), KitArgs)) { return; }
			const float Delay = AsdaSelfTestKit::DelayArg(Args);
			const bool bQuit = Args.Contains(TEXT("quit"));
			TWeakObjectPtr<UWorld> WW(W);
			// Офлайн своего павна нет: офлайн-спавн штатными крюками (как `asda.Trade.SelfTestGates`), проверки — через 2 с.
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WW, bQuit](float) -> bool
			{
				UWorld* Wl = WW.Get();
				if (!Wl) { return false; }
				if (AsdaSelfTestKit::RefuseDeferred(Wl, TEXT("asda.Trade.SelfTestExchangeO1"), bQuit)) { return false; }   // Р-2: офлайн повторно — на отложенном старте
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
						FAsdaExchangeP2PSelfTest T;
						T.Run(W2, bQuit);
					}
					return false;   // разовый запуск
				}), bNeedSpawn ? 2.f : 0.f);
				return false;
			}), Delay);
			UE_LOG(LogAsdaP2PTest, Log, TEXT("САМОТЕСТ-О1: запуск через %.1f с"), Delay);
		}));
}

#endif // !UE_BUILD_SHIPPING
