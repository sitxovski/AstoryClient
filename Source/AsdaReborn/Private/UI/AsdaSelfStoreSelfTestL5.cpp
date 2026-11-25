// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ ВОЛНЫ Л5 ТОРГОВЛИ — ГЕЙТЫ «ЛИЧНАЯ ЛАВКА ОТКРЫТА» (30.09.2026, `Reports/trade/wave_L45.md`). [ПОРТ]
//
// ЧТО ПРОВЕРЯЕТ. Читателей общей точки `UAsdaTradeStateSubsystem` (подготовка 22000 / витрина 23000) ПУТЁМ САМОГО ГЕЙТА — настоящие
//   функции открытия окон и хода, без копий условий. Каждый читатель — в трёх состояниях (подготовка / витрина / лавки нет) с
//   КОНТРОЛЬНЫМИ ПАРАМИ (закон pixel-metric-needs-control-pairs). Нотисы ловятся на общей точке `ChatStoreGate` (счётчик, loc,
//   читатель) и на маршрутизаторе сообщений («[msg] … сисчат … #FFD306»).
//   Читатели (B §6, эталон): книга навыков, журнал заданий, характеристики (107022); молл — только витрина (К-11); «Следовать»;
//   кнопка каналов 17500; автопуть по карте (`beginMoveToTile`) и путь к точке (`mapClick_BeginPathMove`) — молча; клик по земле
//   107012 / 107011; рюкзак 107022 / 13106 (К-17); окно ремесла 76000 — молча (пара к P35 С-13); разговор с NPC — молча только при
//   витрине; верхний гейт клавиш хода; гейт окон h (фокус в 23403); сброс сессии.
//   Фаза «путём игрока»: ESC-клавиша над витриной посетителя → 6305 ↔ под msgbox ESC съеден; клик по кнопке каналов 17500 по живой
//   геометрии → 107022 ↔ без лавки гейта нет; клик по миру сидя — при витрине «встать» не выполняется ↔ без витрины 4024/109;
//   клавиша W — при витрине 4006 нет ↔ без лавки 4006 уходит. Отправки — БОЕВЫЕ отправители под перехватом (`ArmSendCapture`).
//
// Консоль: `asda.Trade.SelfTestStoreL5 [delay=N] [quit]`. Только офлайн (`FAsdaOutgoingGate::RefuseDebugWhenOnline`).
//   Строки — «САМОТЕСТ-ЛАВКА-Л5», итог — «САМОТЕСТ-ЛАВКА-Л5 ИТОГ: ok=… fail=…».
#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "UI/AsdaSelfStoreSubsystem.h"
#include "AsdaCVarSave.h"                   // ★ ИСПРАВЛЕНИЕ 11.10.2026 (В0, N1(д)/U8): ключи консоли самотеста — запомнить и вернуть (значение + SetBy)
#include "UI/AsdaTradeStateSubsystem.h"
#include "UI/AsdaSkillWindowSubsystem.h"
#include "Quest/AsdaQuestLogSubsystem.h"
#include "UI/AsdaCharaInfoSubsystem.h"
#include "UI/AsdaPremiumMallSubsystem.h"
#include "UI/AsdaPlayerMenuSubsystem.h"
#include "UI/AsdaMinimapSubsystem.h"
#include "UI/AsdaInventorySubsystem.h"
#include "UI/AsdaCraftWindowSubsystem.h"
#include "UI/AsdaWarehouseSubsystem.h"
#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaNotice.h"
#include "Npc/AsdaNpcDialogSubsystem.h"
#include "World/AsdaPlayerController.h"
#include "World/AsdaPlayerCharacter.h"
#include "World/AsdaWorldSubsystem.h"
#include "Test/AsdaSelfTestNet.h"   // ★ R1: тонкий доступ самотестов к сети (вместо AsdaNetSubsystem.h)
#include "AsdaOutgoingGate.h"
#include "Test/AsdaSelfTestKit.h"   // ★ 11.10.2026 (В3 S1): общий каркас самотестов (аргументы, отказ, перехват, итог, выход)
#include "AsdaBytes.h"
#include "Login/AsdaLoginData.h"
#include "Blueprint/UserWidget.h"
#include "Components/Button.h"
#include "Components/EditableText.h"
#include "Engine/GameViewportClient.h"
#include "Framework/Application/SlateApplication.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Misc/OutputDevice.h"
#include "Misc/OutputDeviceRedirector.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaStoreTestL5, Log, All);

namespace AsdaStoreL5Local
{
	// Перехватчик лога: все строки (журналы подсистем) и строки маршрутизатора «[msg] …» (канал и цвет нотиса).
	class FSink : public FOutputDevice
	{
	public:
		TArray<FString> All;    // все строки
		TArray<FString> Msg;    // строки LogAsdaMsg «[msg] …»
		FCriticalSection Lock;  // лог приходит с любого потока
		virtual void Serialize(const TCHAR* V, ELogVerbosity::Type, const FName& Category) override
		{
			if (!V) { return; }
			static const FName kMsg(TEXT("LogAsdaMsg"));
			FScopeLock L(&Lock);
			All.Add(V);
			if (Category == kMsg && FCString::Strncmp(V, TEXT("[msg] "), 6) == 0) { Msg.Add(V); }
		}
		virtual bool CanBeUsedOnAnyThread() const override { return true; }
		virtual bool CanBeUsedOnMultipleThreads() const override { return true; }   // ★ 05.11.2026: НЕбуферизованный список UE 5.8 — строка доходит в момент записи (иначе гонка с потоком логов: строка после RemoveOutputDevice/проверки)
		void Reset() { if (GLog) { GLog->Flush(); } FScopeLock L(&Lock); All.Reset(); Msg.Reset(); }
		// Строк лога с подстрокой.
		int32 LogHits(const TCHAR* Needle)
		{
			if (GLog) { GLog->Flush(); }
			FScopeLock L(&Lock);
			int32 N = 0;
			for (const FString& S : All) { if (S.Contains(Needle)) { ++N; } }
			return N;
		}
		// Строк маршрутизатора с текстом (первые 14 символов) и всеми подстроками.
		int32 MsgHits(const FString& Text, std::initializer_list<const TCHAR*> Needles)
		{
			if (GLog) { GLog->Flush(); }
			const FString Key = Text.Left(14);
			FScopeLock L(&Lock);
			int32 N = 0;
			for (const FString& S : Msg)
			{
				if (!Key.IsEmpty() && !S.Contains(Key)) { continue; }
				bool bAll = true;
				for (const TCHAR* Nd : Needles) { if (!S.Contains(Nd)) { bAll = false; break; } }
				if (bAll) { ++N; }
			}
			return N;
		}
	};

	constexpr int32 kSelfCharId = 424242;   // свой charId
	constexpr int32 kSelfUserNum = 17;      // свой userNum
	constexpr int32 kHostUserNum = 55;      // фейковый сосед-продавец (charId = 900000 + userNum)
	constexpr int32 kHostCharId = 900055;

	// Тело 6308 (628 б) — витрина посетителя штатным приёмом (как в самотесте Л2).
	TArray<uint8> Make6308()
	{
		TArray<uint8> B; B.SetNumZeroed(628);
		B[0] = 1;
		AsdaBytes::WrI32(B.GetData() + 1, kHostCharId);
		AsdaBytes::WrI16(B.GetData() + 5, kHostUserNum);
		B[7] = 1;
		FAsdaLoginData::EncodeCp1251(TEXT("Лавка Л5"), B.GetData() + 8, 49);
		FAsdaLoginData::EncodeCp1251(TEXT("Продавец"), B.GetData() + 58, 19);
		uint8* P = B.GetData() + 78;
		AsdaBytes::WrI32(P + 0, 20592); AsdaBytes::WrI32(P + 4, 5); AsdaBytes::WrI32(P + 45, 1000); P[8] = 30;
		return B;
	}
}

struct FAsdaSelfStoreL5SelfTest
{
	// ★ 11.10.2026 (В3 S1, N1(г)): «свой» персонаж (charId, userNum, уровень…) подменяется в Setup — возврат RAII каркаса.
	FAsdaNetSelfScope SelfScope;
	int32 Ok = 0;     // пройдено проверок
	int32 Fail = 0;   // провалено проверок
	struct FCap { uint16 Op = 0; TArray<uint8> P; };           // перехваченный исходящий пакет
	TSharedRef<TArray<FCap>> Caps = MakeShared<TArray<FCap>>();
	uint32 Gen = 0;                                              // поколение перехватчика
	TArray<TFunction<void(UWorld*)>> Steps;                      // шаги фазы «путём игрока»
	AsdaStoreL5Local::FSink Sink;                                // перехватчик лога
	int32 Seq0 = 0;                                              // счётчик отказов «лавка» до действия
	int32 SitPolls = 0;                                          // ожидание состояния «сидит» (8)
	bool bWasInGame = false;                                     // `bInGameConnected` до фазы клавиш

	void Expect(bool bCond, const FString& Name, const FString& Detail)
	{
		if (bCond) { ++Ok; } else { ++Fail; }
		UE_LOG(LogAsdaStoreTestL5, Log, TEXT("САМОТЕСТ-ЛАВКА-Л5 %s: %s — %s"), bCond ? TEXT("ok") : TEXT("ПРОВАЛ"), *Name, *Detail);
	}
	int32 Count(uint16 Op, TArray<uint8>* Out = nullptr) const
	{
		int32 N = 0;
		for (const FCap& C : *Caps) { if (C.Op == Op) { ++N; if (Out) { *Out = C.P; } } }
		return N;
	}
	// Состояние лавки для гейтов: 0 — нет, 1 — подготовка 22000, 2 — витрина 23000 (писатель признаков — общая точка).
	static void SetStore(UAsdaTradeStateSubsystem* TS, int32 Mode)
	{
		TS->SetSellWindowOpen(Mode == 1, TEXT("самотест Л5"));
		TS->SetShopDlgOpen(Mode == 2, TEXT("самотест Л5"));
	}
	// Итог одного действия в состоянии Mode: сколько отказов «лавка» прошло, последний loc и читатель.
	struct FRes { int32 N = 0; int32 Loc = 0; FString Who; };
	static FRes Probe(UAsdaTradeStateSubsystem* TS, int32 Mode, TFunctionRef<void()> Act)
	{
		SetStore(TS, Mode);
		const int32 S0 = TS->GetStoreGateSeq();
		Act();
		FRes R; R.N = TS->GetStoreGateSeq() - S0; R.Loc = R.N ? TS->GetLastStoreGateLoc() : 0; R.Who = R.N ? TS->GetLastStoreGateWho() : FString();
		SetStore(TS, 0);
		return R;
	}
	static FString D(const FRes& R) { return FString::Printf(TEXT("%d×loc %d «%s»"), R.N, R.Loc, *R.Who); }
	static FVector2D AbsCenter(const UWidget* W)
	{
		if (!W) { return FVector2D::ZeroVector; }
		const FGeometry G = W->GetCachedGeometry();
		return G.GetAbsolutePosition() + G.GetAbsoluteSize() * 0.5f;
	}

	void Run(UWorld* W)
	{
		using namespace AsdaStoreL5Local;
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaTradeStateSubsystem* TS = GI ? GI->GetSubsystem<UAsdaTradeStateSubsystem>() : nullptr;
		UAsdaSelfStoreSubsystem* S = GI ? GI->GetSubsystem<UAsdaSelfStoreSubsystem>() : nullptr;
		UAsdaNetSubsystem* Net = GI ? AsdaTestNet::Get(GI) : nullptr;
		UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
		UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
		AAsdaPlayerController* PC = W ? Cast<AAsdaPlayerController>(W->GetFirstPlayerController()) : nullptr;
		if (!TS || !S || !Net || !WM || !WS || !PC)
		{
			UE_LOG(LogAsdaStoreTestL5, Error, TEXT("САМОТЕСТ-ЛАВКА-Л5 ИТОГ: ok=0 fail=1 (нет подсистем/контроллера)"));
			return;
		}
		SelfScope.Capture(Net, TEXT("asda.Trade.SelfTestStoreL5"));   // ★ В3 S1: снимок «своего» до подмены
		AsdaTestNet::SelectedCharId(Net) = kSelfCharId;
		AsdaTestNet::SetSelfUserNumForDebug(Net, kSelfUserNum);
		AsdaTestNet::SelfLevel(Net) = 30;
		WS->DebugSpawnFakePeer(kHostUserNum, TEXT("Продавец"), FVector2D(258.f, 256.f));
		S->ResetSessionState(EAsdaSessionResetReason::MapChanged);
		Caps->Reset();
		TSharedRef<TArray<FCap>> C = Caps;
		Gen = AsdaSelfTestKit::ArmCapture(Net, [C](uint16 Op, const uint8* P, int32 Len, bool, TCHAR)
		{
			FCap X; X.Op = Op; if (P && Len > 0) { X.P.Append(P, Len); } C->Add(MoveTemp(X));
		}, 0, TEXT("asda.Trade.SelfTestStoreL5"));
		WM->ClearMsgBoxQueue(TEXT("самотест лавки Л5"));
		GLog->AddOutputDevice(&Sink);
		SetStore(TS, 0);

		// ═══ 1. Книга навыков `skillwin_OpenSkillWindow` — 107022 при подготовке и витрине ↔ без лавки гейта нет ═══
		if (UAsdaSkillWindowSubsystem* SW = GI->GetSubsystem<UAsdaSkillWindowSubsystem>())
		{
			auto Act = [SW]() { if (!SW->IsWindowOpen()) { SW->OpenWindow(); } };
			const FRes P = Probe(TS, 1, Act); const bool bP = !SW->IsWindowOpen();
			const FRes V = Probe(TS, 2, Act); const bool bV = !SW->IsWindowOpen();
			const FRes N = Probe(TS, 0, Act);
			if (SW->IsWindowOpen()) { SW->CloseWindow(); }
			Expect(P.N == 1 && P.Loc == 107022 && P.Who == TEXT("skillwin_OpenSkillWindow") && bP && V.N == 1 && bV && N.N == 0,
				TEXT("книга навыков: подготовка / витрина — 107022, окно не открыто ↔ без лавки — гейта нет"),
				FString::Printf(TEXT("подг %s закр=%d; витр %s закр=%d; нет %s"), *D(P), bP ? 1 : 0, *D(V), bV ? 1 : 0, *D(N)));
		}
		else { Expect(false, TEXT("книга навыков"), TEXT("нет подсистемы")); }

		// ═══ 2. Журнал заданий `uiQuestGuide_Open` — то же ═══
		if (UAsdaQuestLogSubsystem* QL = GI->GetSubsystem<UAsdaQuestLogSubsystem>())
		{
			auto Act = [QL]() { QL->Open(); };
			const FRes P = Probe(TS, 1, Act); const bool bP = !QL->IsOpen();
			const FRes V = Probe(TS, 2, Act); const bool bV = !QL->IsOpen();
			const FRes N = Probe(TS, 0, Act);
			if (QL->IsOpen()) { QL->Close(); }
			Expect(P.N == 1 && P.Loc == 107022 && P.Who == TEXT("uiQuestGuide_Open") && bP && V.N == 1 && bV && N.N == 0,
				TEXT("журнал заданий: подготовка / витрина — 107022, окно не открыто ↔ без лавки — гейта нет"),
				FString::Printf(TEXT("подг %s; витр %s; нет %s"), *D(P), *D(V), *D(N)));
		}
		else { Expect(false, TEXT("журнал заданий"), TEXT("нет подсистемы")); }

		// ═══ 3. Характеристики `uiWindow_OpenIfAllowed` — то же, 5466 не уходит ═══
		if (UAsdaCharaInfoSubsystem* CI = GI->GetSubsystem<UAsdaCharaInfoSubsystem>())
		{
			auto Act = [CI]() { if (!CI->IsWindowOpen()) { CI->OpenWindow(); } };
			Caps->Reset();
			const FRes P = Probe(TS, 1, Act);
			const FRes V = Probe(TS, 2, Act);
			const int32 N5466 = Count(5466);
			const bool bClosed = !CI->IsWindowOpen();
			const FRes N = Probe(TS, 0, Act);
			if (CI->IsWindowOpen()) { CI->CloseWindow(); }
			Expect(P.N == 1 && P.Loc == 107022 && P.Who == TEXT("uiWindow_OpenIfAllowed") && V.N == 1 && N5466 == 0 && bClosed && N.N == 0,
				TEXT("характеристики: подготовка / витрина — 107022, ни окна, ни 5466 ↔ без лавки — гейта нет"),
				FString::Printf(TEXT("подг %s; витр %s; 5466=%d; нет %s"), *D(P), *D(V), N5466, *D(N)));
		}
		else { Expect(false, TEXT("характеристики"), TEXT("нет подсистемы")); }

		// ═══ 4. Молл `uiInventoryConfirmAction` — ТОЛЬКО витрина → 107022 (К-11 [ОРИГИНАЛ]) ↔ подготовка — гейта нет ═══
		if (UAsdaPremiumMallSubsystem* ML = GI->GetSubsystem<UAsdaPremiumMallSubsystem>())
		{
			if (ML->IsOpen()) { ML->CloseMall(); }
			const FRes V = Probe(TS, 2, [ML]() { ML->Toggle(); }); const bool bV = !ML->IsOpen();
			const FRes P = Probe(TS, 1, [ML]() { ML->Toggle(); });
			if (ML->IsOpen()) { ML->CloseMall(); }
			Expect(V.N == 1 && V.Loc == 107022 && V.Who == TEXT("uiInventoryConfirmAction") && bV && P.N == 0,
				TEXT("молл: витрина — 107022, не открыт ↔ подготовка 22000 — гейта нет (К-11, оригинал)"),
				FString::Printf(TEXT("витр %s закр=%d; подг %s"), *D(V), bV ? 1 : 0, *D(P)));
		}
		else { Expect(false, TEXT("молл"), TEXT("нет подсистемы")); }

		// ═══ 5. «Следовать» `uiPreparePartyTradeRequest` — 107022, следование не взведено ↔ без лавки — следование взведено ═══
		if (UAsdaPlayerMenuSubsystem* PM = GI->GetSubsystem<UAsdaPlayerMenuSubsystem>())
		{
			auto Act = [PM]() { PM->StartFollow(kHostUserNum, kHostCharId, TEXT("Продавец"), 0); };
			const FRes P = Probe(TS, 1, Act); const bool bP = !PM->IsFollowing();
			const FRes V = Probe(TS, 2, Act); const bool bV = !PM->IsFollowing();
			const FRes N = Probe(TS, 0, Act); const bool bN = PM->IsFollowing();
			if (PM->IsFollowing()) { PM->CancelFollow(TEXT("самотест Л5")); }
			PC->CancelRoute();
			Expect(P.N == 1 && P.Loc == 107022 && P.Who == TEXT("uiPreparePartyTradeRequest") && bP && V.N == 1 && bV && N.N == 0 && bN,
				TEXT("«Следовать»: подготовка / витрина — 107022, следования нет ↔ без лавки — следование взведено"),
				FString::Printf(TEXT("подг %s; витр %s; нет %s следует=%d"), *D(P), *D(V), *D(N), bN ? 1 : 0));
		}
		else { Expect(false, TEXT("«Следовать»"), TEXT("нет подсистемы")); }

		// ═══ 6. Автопуть `beginMoveToTile` и путь к точке `mapClick_BeginPathMove` — молча ↔ без лавки — заявка/маршрут есть ═══
		if (UAsdaMinimapSubsystem* MM = GI->GetSubsystem<UAsdaMinimapSubsystem>())
		{
			SetStore(TS, 2);
			const bool bT1 = MM->BeginMapTravel(3, 100, 100);
			const bool bReq1 = MM->IsTravelRequested();
			SetStore(TS, 0);
			const bool bT0 = MM->BeginMapTravel(3, 100, 100);
			const bool bReq0 = MM->IsTravelRequested();
			MM->CancelTravelRequest();
			const FVector2D Here = PC->GetChar() ? PC->GetChar()->GetGamePosXZ() : FVector2D::ZeroVector;
			PC->CancelRoute();
			const FRes P = Probe(TS, 1, [PC, Here]() { PC->MoveToGamePoint(Here + FVector2D(300.f, 0.f)); });
			const bool bNoRoute = !PC->HasRoute();
			SetStore(TS, 0);
			PC->MoveToGamePoint(Here + FVector2D(300.f, 0.f));
			const bool bRoute = PC->HasRoute();
			PC->CancelRoute();
			Expect(!bT1 && !bReq1 && bT0 && bReq0 && P.N == 0 && bNoRoute && bRoute,
				TEXT("автопуть по карте и путь к точке: лавка открыта — молча, ни заявки, ни маршрута ↔ без лавки — заявка и маршрут есть"),
				FString::Printf(TEXT("лавка: заявка=%d/%d маршрут=%d нотисов=%d; нет: заявка=%d/%d маршрут=%d"), bT1 ? 1 : 0, bReq1 ? 1 : 0,
					bNoRoute ? 0 : 1, P.N, bT0 ? 1 : 0, bReq0 ? 1 : 0, bRoute ? 1 : 0));
		}
		else { Expect(false, TEXT("автопуть"), TEXT("нет подсистемы мини-карты")); }

		// ═══ 7. Клик по земле `Input_WorldClickHandler`: подготовка — 107012, витрина — 107011, хода нет ↔ без лавки — дошёл до троттла ═══
		if (PC->GetChar())
		{
			const FVector Here = PC->GetChar()->GetActorLocation();
			const double T0 = PC->LastMoveSendTime;
			const FRes P = Probe(TS, 1, [PC, Here]() { PC->GroundClickMove(Here); });
			const FRes V = Probe(TS, 2, [PC, Here]() { PC->GroundClickMove(Here); });
			const bool bNoMove = PC->LastMoveSendTime == T0;
			PC->LastMoveSendTime = 0.0;
			const FRes N = Probe(TS, 0, [PC, Here]() { PC->GroundClickMove(Here); });
			const bool bPassed = PC->LastMoveSendTime != 0.0;
			PC->CancelRoute();
			Sink.Reset();
			SetStore(TS, 2); PC->LastMoveSendTime = T0; PC->GroundClickMove(Here); SetStore(TS, 0);
			const FString T107011 = AsdaNotice::Loc(GI, 107011);
			const int32 Col = Sink.MsgHits(T107011, { TEXT("сисчат"), TEXT("#FFD306") });
			Expect(P.N == 1 && P.Loc == 107012 && V.N == 1 && V.Loc == 107011 && bNoMove && N.N == 0 && bPassed && Col == 1,
				TEXT("клик по земле: подготовка — 107012, витрина — 107011 (сисчат #FFD306), хода нет ↔ без лавки — дошёл до троттла 150 мс"),
				FString::Printf(TEXT("подг %s; витр %s; троттл не тронут=%d; нет %s дошёл=%d; строк цвета %d"), *D(P), *D(V), bNoMove ? 1 : 0, *D(N),
					bPassed ? 1 : 0, Col));
		}
		else { Expect(false, TEXT("клик по земле"), TEXT("нет своего персонажа")); }

		// ═══ 8. Рюкзак `uiOpenInventoryWindow`: подготовка — 107022; витрина — 107022 (К-17) ↔ ключ 0 — 13106 (оригинал) ↔ нет — открыт ═══
		//   (msgbox прежних шагов — например 9421 книги навыков без профессии — модален и закрыл бы рюкзак гейтом 110644: снимаем.)
		WM->ClearMsgBoxQueue(TEXT("самотест Л5: перед рюкзаком"));
		if (UAsdaInventorySubsystem* Inv = GI->GetSubsystem<UAsdaInventorySubsystem>())
		{
			if (Inv->IsWindowOpen()) { Inv->CloseWindow(); }
			auto Act = [Inv]() { Inv->OpenWindow(); };
			const FRes P = Probe(TS, 1, Act); const bool bP = !Inv->IsWindowOpen();
			const FRes V = Probe(TS, 2, Act); const bool bV = !Inv->IsWindowOpen();
			IConsoleVariable* Cv = IConsoleManager::Get().FindConsoleVariable(TEXT("asda.Trade.StoreFixK17"));
			if (Cv) { AsdaCVarSave::SetForTest(TEXT("asda.Trade.StoreFixK17"), 0); }
			const FRes V0 = Probe(TS, 2, Act);
			if (Cv) { AsdaCVarSave::SetForTest(TEXT("asda.Trade.StoreFixK17"), 1); }
			const FRes N = Probe(TS, 0, Act); const bool bN = Inv->IsWindowOpen();
			if (Inv->IsWindowOpen()) { Inv->CloseWindow(); }
			Expect(P.N == 1 && P.Loc == 107022 && bP && V.N == 1 && V.Loc == 107022 && bV && V0.N == 1 && V0.Loc == 13106 && N.N == 0 && bN,
				TEXT("рюкзак: подготовка — 107022; витрина — 107022 ([ИСПРАВЛЕНИЕ] К-17) ↔ ключ 0 — 13106 (оригинал: текст бустера) ↔ без лавки — открыт"),
				FString::Printf(TEXT("подг %s; витр %s; витр/ключ0 %s; нет %s открыт=%d"), *D(P), *D(V), *D(V0), *D(N), bN ? 1 : 0));
		}
		else { Expect(false, TEXT("рюкзак"), TEXT("нет подсистемы")); }

		// ═══ 9. Окно ремесла 76000 `tileInteractGateAndOpen` — молча при подготовке и витрине (пара к P35 С-13) ↔ без лавки — открыто ═══
		if (UAsdaCraftWindowSubsystem* CW = GI->GetSubsystem<UAsdaCraftWindowSubsystem>())
		{
			if (CW->IsOpen()) { CW->GateAndToggle(TEXT("самотест Л5")); }
			Sink.Reset();
			const FRes P = Probe(TS, 1, [CW]() { CW->GateAndToggle(TEXT("самотест Л5")); }); const bool bP = !CW->IsOpen();
			const FRes V = Probe(TS, 2, [CW]() { CW->GateAndToggle(TEXT("самотест Л5")); }); const bool bV = !CW->IsOpen();
			const int32 hP = Sink.LogHits(TEXT("молча: подготовка личной лавки 22000"));
			const int32 hV = Sink.LogHits(TEXT("молча: витрина личной лавки 23000"));
			const FRes N = Probe(TS, 0, [CW]() { CW->GateAndToggle(TEXT("самотест Л5")); }); const bool bN = CW->IsOpen();
			if (CW->IsOpen()) { CW->GateAndToggle(TEXT("самотест Л5")); }
			Expect(P.N == 0 && V.N == 0 && bP && bV && hP >= 1 && hV >= 1 && bN,
				TEXT("окно ремесла 76000: подготовка / витрина — молча, не открыто ↔ без лавки — открыто"),
				FString::Printf(TEXT("нотисов %d/%d, строк гейта %d/%d, закрыто %d/%d; нет: открыто=%d"), P.N, V.N, hP, hV, bP ? 1 : 0, bV ? 1 : 0, bN ? 1 : 0));
		}
		else { Expect(false, TEXT("окно ремесла"), TEXT("нет подсистемы")); }

		// ═══ 10. Разговор с NPC `regionWindow_OpenAndPathMove` — витрина: молча ↔ подготовка: гейта нет (эталон проверяет только витрину) ═══
		if (UAsdaNpcDialogSubsystem* Dlg = GI->GetSubsystem<UAsdaNpcDialogSubsystem>())
		{
			static const TCHAR* kNeedle = TEXT("открыта витрина личной лавки 23000 — разговор молча не открыт");
			Sink.Reset(); SetStore(TS, 2); Dlg->OpenForNpcId(-77); SetStore(TS, 0); const int32 hV = Sink.LogHits(kNeedle);
			Sink.Reset(); SetStore(TS, 1); Dlg->OpenForNpcId(-77); SetStore(TS, 0); const int32 hP = Sink.LogHits(kNeedle);
			Expect(hV == 1 && hP == 0 && !Dlg->IsDialogOpen(), TEXT("разговор с NPC: витрина 23000 — молча не открыт ↔ подготовка 22000 — гейта витрины нет"),
				FString::Printf(TEXT("строк гейта: витрина %d, подготовка %d"), hV, hP));
		}

		// ═══ 11. Верхний гейт клавиш хода `updatePlayerMovement` (реестр «Ход клавишами» п.1) — подготовка/витрина ↔ нет ═══
		{
			SetStore(TS, 1); const bool b1 = PC->KbTopGateBlocked();
			SetStore(TS, 2); const bool b2 = PC->KbTopGateBlocked();
			SetStore(TS, 0); const bool b0 = PC->KbTopGateBlocked();
			Expect(b1 && b2 && !b0, TEXT("верхний гейт клавиш хода: подготовка / витрина — клавиши не двигают ↔ без лавки — гейт снят"),
				FString::Printf(TEXT("%d/%d/%d (ждём 1/1/0)"), b1 ? 1 : 0, b2 ? 1 : 0, b0 ? 1 : 0));
		}

		// ═══ 12. Гейт окон h: фокус в поле чата 23403 — клавиши хода «не нажаты» ↔ фокус у вьюпорта — разрешены ═══
		{
			AsdaTestNet::DebugFeedPacket(Net, 6308, Make6308());
			S->PumpPending();
			bool bFocus = false, bAllowF = true;
			if (S->EdChat) { S->EdChat->SetKeyboardFocus(); bFocus = S->IsChatInputFocused(); bAllowF = PC->KbWindowGateAllows(); }
			FSlateApplication::Get().SetAllUserFocusToGameViewport();
			const bool bAllowV = PC->KbWindowGateAllows();
			Expect(S->IsVisitorShowcaseOpen() && bFocus && !bAllowF && bAllowV,
				TEXT("гейт окон h: фокус в поле чата лавки 23403 — клавиши хода «не нажаты» ↔ фокус у вьюпорта — разрешены"),
				FString::Printf(TEXT("витрина=%d фокус=%d разрешено(фокус)=%d разрешено(вьюпорт)=%d"), S->IsVisitorShowcaseOpen() ? 1 : 0,
					bFocus ? 1 : 0, bAllowF ? 1 : 0, bAllowV ? 1 : 0));
		}

		// ═══ 14. ★ Ревизия Л4/Л5 (СРЕДН-1): гейт склада 10305 (свиток возврата, `CObject_GetField5581`) у тех же читателей ═══
		//   «Следовать» и 17500 → 10305 в чат; клик по земле → 10305, хода нет; путь по карте и автопуть — молча ↔ свиток снят — гейта нет.
		S->ResetSessionState(EAsdaSessionResetReason::MapChanged);   // витрина п. 12 закрыта: гейт лавки не должен заслонять склад
		SetStore(TS, 0);
		if (UAsdaWarehouseSubsystem* Ware = GI->GetSubsystem<UAsdaWarehouseSubsystem>())
		{
			UAsdaPlayerMenuSubsystem* PM = GI->GetSubsystem<UAsdaPlayerMenuSubsystem>();
			UAsdaMinimapSubsystem* MM = GI->GetSubsystem<UAsdaMinimapSubsystem>();
			const FString T10305 = AsdaNotice::Loc(GI, 10305);
			const FVector2D Here = PC->GetChar() ? PC->GetChar()->GetGamePosXZ() : FVector2D::ZeroVector;
			const FVector HereUe = PC->GetChar() ? PC->GetChar()->GetActorLocation() : FVector::ZeroVector;
			auto Run5 = [&](bool bRecall, int32& OutNotes, bool& bFollow, bool& bRoute, bool& bTravel, bool& bClickPassed)
			{
				if (bRecall) { Ware->OpenRecallPanel(); } else if (Ware->IsRecallActive()) { Ware->CloseRecallPanel(); }
				Sink.Reset();
				if (PM) { PM->StartFollow(kHostUserNum, kHostCharId, TEXT("Продавец"), 0); bFollow = PM->IsFollowing(); if (bFollow) { PM->CancelFollow(TEXT("самотест Л5")); } }
				if (MM) { MM->OnChannelBtnClicked(); if (!bRecall) { MM->OnChannelBtnClicked(); } }   // без свитка — вернуть панель
				PC->CancelRoute();
				PC->MoveToGamePoint(Here + FVector2D(300.f, 0.f)); bRoute = PC->HasRoute(); PC->CancelRoute();
				bTravel = MM && MM->BeginMapTravel(3, 100, 100); if (MM && MM->IsTravelRequested()) { MM->CancelTravelRequest(); }
				const double T0 = PC->LastMoveSendTime = 0.0;
				PC->GroundClickMove(HereUe); bClickPassed = PC->LastMoveSendTime != T0; PC->CancelRoute();
				OutNotes = Sink.MsgHits(T10305, { TEXT("сисчат"), TEXT("#FFD306") });
			};
			int32 N1 = 0, N0 = 0; bool F1 = false, R1 = false, Tr1 = false, C1 = false, F0 = false, R0 = false, Tr0 = false, C0 = false;
			Run5(true, N1, F1, R1, Tr1, C1);
			const bool bWasRecall = Ware->IsRecallActive();
			Run5(false, N0, F0, R0, Tr0, C0);
			Expect(bWasRecall && N1 == 3 && !F1 && !R1 && !Tr1 && !C1 && N0 == 0 && F0 && R0 && Tr0 && C0,
				TEXT("СРЕДН-1: свиток возврата склада — «Следовать», 17500, клик по земле → 10305 (сисчат #FFD306), путь по карте и автопуть молча ↔ свитка нет — всё проходит"),
				FString::Printf(TEXT("свиток=%d: 10305×%d следует=%d маршрут=%d заявка=%d клик дошёл=%d; без свитка: 10305×%d следует=%d маршрут=%d заявка=%d клик дошёл=%d"),
					bWasRecall ? 1 : 0, N1, F1 ? 1 : 0, R1 ? 1 : 0, Tr1 ? 1 : 0, C1 ? 1 : 0, N0, F0 ? 1 : 0, R0 ? 1 : 0, Tr0 ? 1 : 0, C0 ? 1 : 0));
			// МЕЛК-3: молл при свитке (гейт 1 эталона) молчит даже при витрине ↔ без свитка — 107022.
			if (UAsdaPremiumMallSubsystem* ML = GI->GetSubsystem<UAsdaPremiumMallSubsystem>())
			{
				Ware->OpenRecallPanel();
				const FRes Vr = Probe(TS, 2, [ML]() { ML->Toggle(); }); const bool bClosedR = !ML->IsOpen();
				Ware->CloseRecallPanel();
				const FRes V = Probe(TS, 2, [ML]() { ML->Toggle(); });
				if (ML->IsOpen()) { ML->CloseMall(); }
				Expect(Vr.N == 0 && bClosedR && V.N == 1 && V.Loc == 107022,
					TEXT("МЕЛК-3: молл при витрине и свитке склада — молча (гейт 1 эталона раньше 107022) ↔ без свитка — 107022"),
					FString::Printf(TEXT("свиток: %s закрыт=%d; без: %s"), *D(Vr), bClosedR ? 1 : 0, *D(V)));
			}
			WM->ClearMsgBoxQueue(TEXT("самотест Л5: склад"));
		}
		else { Expect(false, TEXT("СРЕДН-1 склад"), TEXT("нет подсистемы склада")); }

		// ═══ 13. Сброс сессии: признаки лавки гаснут, окна скрыты, пакетов нет ═══
		{
			Caps->Reset();
			SetStore(TS, 1);
			TS->ResetSessionState(EAsdaSessionResetReason::MapChanged);
			S->ResetSessionState(EAsdaSessionResetReason::MapChanged);
			Expect(!UAsdaTradeStateSubsystem::IsSelfStoreOpen(GI) && !S->IsShowcaseOpen() && Caps->Num() == 0,
				TEXT("сброс сессии: признаки 22000/23000 погашены, витрина скрыта, пакетов нет"),
				FString::Printf(TEXT("%s; пакетов %d"), *TS->DebugString(), Caps->Num()));
		}
	}

	// ═══ ФАЗА «ПУТЁМ ИГРОКА» — шаги раз в 0,4 с, настоящие события ввода ═══
	void BuildInputPhase()
	{
		using namespace AsdaStoreL5Local;
		auto Ctx = [](UWorld* W, UGameInstance*& GI, UAsdaSelfStoreSubsystem*& S, UAsdaNetSubsystem*& Net, UAsdaWindowSubsystem*& WM,
			UAsdaTradeStateSubsystem*& TS, AAsdaPlayerController*& PC)
		{
			GI = W ? W->GetGameInstance() : nullptr;
			S = GI ? GI->GetSubsystem<UAsdaSelfStoreSubsystem>() : nullptr;
			Net = GI ? AsdaTestNet::Get(GI) : nullptr;
			WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
			TS = GI ? GI->GetSubsystem<UAsdaTradeStateSubsystem>() : nullptr;
			PC = W ? Cast<AAsdaPlayerController>(W->GetFirstPlayerController()) : nullptr;
			return S && Net && WM && TS && PC;
		};
		auto Key = [](APlayerController* PC, const FKey& K, bool bDown)
		{
			if (PC) { PC->InputKey(FInputKeyEventArgs::CreateSimulated(K, bDown ? IE_Pressed : IE_Released, bDown ? 1.f : 0.f)); }
		};
#define L5_CTX UGameInstance* GI; UAsdaSelfStoreSubsystem* S; UAsdaNetSubsystem* Net; UAsdaWindowSubsystem* WM; UAsdaTradeStateSubsystem* TS; \
		AAsdaPlayerController* PC; if (!Ctx(W, GI, S, Net, WM, TS, PC)) { return; }

		// 1. ESC-клавиша над витриной посетителя: под однокнопочным msgbox ESC съеден модалкой ↔ без него — 6305 хозяина.
		Steps.Add([this, Ctx, Key](UWorld* W)
		{
			L5_CTX
			AsdaTestNet::SelectedCharId(Net) = kSelfCharId; AsdaTestNet::SetSelfUserNumForDebug(Net, kSelfUserNum);
			AsdaTestNet::DebugFeedPacket(Net, 6308, Make6308());
			S->PumpPending();
			FSlateApplication::Get().SetAllUserFocusToGameViewport();
			AsdaNotice::MsgBox(GI, EAsdaMsgCategory::System, TEXT("самотест Л5: модалка"));
			Caps->Reset();
			Key(PC, EKeys::Escape, true);
		});
		Steps.Add([Key, Ctx](UWorld* W) { L5_CTX Key(PC, EKeys::Escape, false); });
		Steps.Add([this, Ctx, Key](UWorld* W)
		{
			L5_CTX
			const int32 NModal = Count(6305);
			WM->ClearMsgBoxQueue(TEXT("самотест Л5"));
			Seq0 = NModal;
			Key(PC, EKeys::Escape, true);
		});
		Steps.Add([Key, Ctx](UWorld* W) { L5_CTX Key(PC, EKeys::Escape, false); });
		Steps.Add([this, Ctx](UWorld* W)
		{
			L5_CTX
			TArray<uint8> P; const int32 N = Count(6305, &P);
			Expect(Seq0 == 0 && N == 1 && P.Num() == 6 && AsdaBytes::RdI32(P.GetData()) == kHostCharId && AsdaBytes::RdI16(P.GetData() + 4) == kHostUserNum,
				TEXT("путь игрока: клавиша ESC над витриной посетителя → 6305 хозяина (900055/55) ↔ под msgbox ESC съеден модалкой"),
				FString::Printf(TEXT("под msgbox 6305=%d; без — 6305=%d тело %d б"), Seq0, N, P.Num()));
			S->ResetSessionState(EAsdaSessionResetReason::MapChanged);
		});
		// 2. Клик по кнопке каналов 17500 мини-карты по живой геометрии: лавка открыта — 107022 ↔ без лавки гейта нет.
		Steps.Add([this, Ctx](UWorld* W)
		{
			L5_CTX
			UUserWidget* MW = WM->FindWindow(FName(TEXT("MINIMAP_WIN")));   // окно мини-карты (реестр окон; открывает `UAsdaMinimapSubsystem`)
			const UWidget* Btn = MW ? MW->GetWidgetFromName(TEXT("Change_Channel_Btn")) : nullptr;
			ChannelAt = AbsCenter(Btn);
			SetStore(TS, 2);
			Seq0 = TS->GetStoreGateSeq();
			if (!ChannelAt.IsZero()) { WM->SimulateClickAt(ChannelAt, 3); }
		});
		Steps.Add([](UWorld*) {});   // запас на клик
		Steps.Add([this, Ctx](UWorld* W)
		{
			L5_CTX
			const int32 N1 = TS->GetStoreGateSeq() - Seq0;
			const FString Who = TS->GetLastStoreGateWho();
			SetStore(TS, 0);
			SeqCtl = N1; WhoCtl = Who;
			Seq0 = TS->GetStoreGateSeq();
			if (!ChannelAt.IsZero()) { WM->SimulateClickAt(ChannelAt, 3); }
		});
		Steps.Add([](UWorld*) {});   // запас на клик
		Steps.Add([this, Ctx](UWorld* W)
		{
			L5_CTX
			const int32 N0 = TS->GetStoreGateSeq() - Seq0;
			if (!ChannelAt.IsZero()) { WM->SimulateClickAt(ChannelAt, 3); }   // свернуть панель обратно
			Expect(!ChannelAt.IsZero() && SeqCtl == 1 && WhoCtl.StartsWith(TEXT("MiniMap_BigMapWindowProc")) && N0 == 0,
				TEXT("путь игрока: клик по кнопке каналов 17500 при витрине — 107022 ↔ без лавки — гейта нет (панель раскрывается)"),
				FString::Printf(TEXT("точка (%.0f,%.0f): лавка %d «%s»; нет %d"), ChannelAt.X, ChannelAt.Y, SeqCtl, *WhoCtl, N0));
		});
		Steps.Add([](UWorld*) {});   // запас на клик
		// 3. Клик по миру сидя: сесть (4024/107), дождаться состояния 8; при витрине клик — «встать» не выполняется ↔ без витрины 4024/109.
		Steps.Add([this, Ctx](UWorld* W)
		{
			L5_CTX
			PC->ApplySitPose4024(/*bSit*/ true, TEXT("самотест Л5"));
			SitPolls = 0;
		});
		for (int32 k = 0; k < 8; ++k)
		{
			Steps.Add([this, Ctx, k](UWorld* W)
			{
				L5_CTX
				if (SitPolls < 0) { return; }
				if (PC->KbEtalonActionState() != 8 && k < 7) { ++SitPolls; return; }
				SitPolls = -1;
				SitOk = PC->KbEtalonActionState() == 8;
				SetStore(TS, 2);
				Caps->Reset();
				WM->SimulateClickAt(WorldPoint(W), 3);
			});
		}
		Steps.Add([](UWorld*) {});   // запас на клик
		Steps.Add([this, Ctx](UWorld* W)
		{
			L5_CTX
			int32 N109 = 0;
			for (const FCap& Cp : *Caps) { if (Cp.Op == 4024 && Cp.P.Num() >= 20 && AsdaBytes::RdI16(Cp.P.GetData() + 18) == 109) { ++N109; } }   // код позы @18 (как самотест Л1)
			SitBlocked = (N109 == 0) && PC->KbEtalonActionState() == 8;
			SetStore(TS, 0);
			Caps->Reset();
			WM->SimulateClickAt(WorldPoint(W), 3);
		});
		Steps.Add([](UWorld*) {});   // запас на клик
		Steps.Add([this, Ctx](UWorld* W)
		{
			L5_CTX
			int32 N109 = 0;
			for (const FCap& Cp : *Caps) { if (Cp.Op == 4024 && Cp.P.Num() >= 20 && AsdaBytes::RdI16(Cp.P.GetData() + 18) == 109) { ++N109; } }   // код позы @18 (как самотест Л1)
			Expect(SitOk && SitBlocked && N109 == 1,
				TEXT("путь игрока: клик по миру сидя при витрине — «встать» не выполняется (4024/109 нет) ↔ без витрины — 4024/109"),
				FString::Printf(TEXT("сидит=%d, витрина: заблокировано=%d; без витрины 4024/109=%d"), SitOk ? 1 : 0, SitBlocked ? 1 : 0, N109));
		});
		Steps.Add([](UWorld*) {});   // пусть встанет
		Steps.Add([](UWorld*) {});
		// 4. Клавиша W (штатный `UPlayerInput`): при витрине 4006 нет ↔ без лавки — 4006 уходит.
		Steps.Add([this, Ctx, Key](UWorld* W)
		{
			L5_CTX
			bWasInGame = AsdaTestNet::bInGameConnected(Net);
			AsdaTestNet::bInGameConnected(Net) = true;
			SetStore(TS, 2);
			Caps->Reset();
			Key(PC, EKeys::W, true);
		});
		Steps.Add([](UWorld*) {});
		Steps.Add([this, Ctx, Key](UWorld* W)
		{
			L5_CTX
			KbGated = Count(4006);
			Key(PC, EKeys::W, false);
		});
		Steps.Add([this, Ctx, Key](UWorld* W)
		{
			L5_CTX
			SetStore(TS, 0);
			Caps->Reset();
			Key(PC, EKeys::W, true);
		});
		Steps.Add([](UWorld*) {});
		Steps.Add([this, Ctx, Key](UWorld* W)
		{
			L5_CTX
			const int32 NFree = Count(4006);
			Key(PC, EKeys::W, false);
			Expect(KbGated == 0 && NFree >= 1, TEXT("путь игрока: клавиша W при витрине — 4006 нет (верхний гейт) ↔ без лавки — 4006 уходит"),
				FString::Printf(TEXT("витрина 4006=%d; без лавки 4006=%d"), KbGated, NFree));
		});
		Steps.Add([this, Ctx](UWorld* W)
		{
			L5_CTX
			AsdaTestNet::bInGameConnected(Net) = bWasInGame;
			PC->CancelRoute();
		});
#undef L5_CTX
	}

	// Точка мира для клика: верхняя треть вьюпорта по центру (над своим персонажем, без окон), в абсолютных координатах Slate.
	static FVector2D WorldPoint(UWorld* W)
	{
		UGameViewportClient* VC = W ? W->GetGameViewport() : nullptr;
		TSharedPtr<SViewport> VW = VC ? VC->GetGameViewportWidget() : nullptr;
		if (!VW.IsValid()) { return FVector2D(400.f, 200.f); }
		const FGeometry G = VW->GetCachedGeometry();
		return G.GetAbsolutePosition() + FVector2D(G.GetAbsoluteSize().X * 0.5f, G.GetAbsoluteSize().Y * 0.3f);
	}

	FVector2D ChannelAt = FVector2D::ZeroVector;   // центр кнопки 17500 (живая геометрия)
	int32 SeqCtl = 0;                               // отказов при лавке (клик 17500)
	FString WhoCtl;                                 // читатель отказа (клик 17500)
	bool SitOk = false;                             // персонаж дошёл до состояния 8
	bool SitBlocked = false;                        // при витрине «встать» не выполнилось
	int32 KbGated = 0;                              // 4006 при витрине (клавиша W)

	void Disarm(UAsdaNetSubsystem* Net)
	{
		if (Net) { AsdaSelfTestKit::DisarmCapture(Net, Gen); }
		if (GLog) { GLog->RemoveOutputDevice(&Sink); }
	}
};

namespace
{
	// ★ R5-fix-c (13.11.2026): своя `ParseStoreL5Delay` (дословная копия у двенадцати самотестов) убрана — разбор `delay=N` в общей точке
	//   `AsdaSelfTestKit::DelayArg` (`Test/AsdaSelfTestKit.h`), вызовы ниже идут в неё.

	FAutoConsoleCommandWithWorldAndArgs GAsdaTradeSelfTestStoreL5(
		TEXT("asda.Trade.SelfTestStoreL5"),
		TEXT("asda.Trade.SelfTestStoreL5 [delay=N] [quit] — самотест гейтов личной лавки (волна Л5): книга навыков, журнал, характеристики, "
		     "молл (К-11), «Следовать», автопуть, клик по земле 107012/107011, рюкзак (К-17), ремесло, NPC, клавиши, фокус 23403, сброс; путь "
		     "игрока — ESC → 6305, кнопка 17500, клик по миру сидя, клавиша W. Только офлайн."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
		{
			const FAsdaSelfTestArgs KitArgs = FAsdaSelfTestArgs::Parse(Args);   // ★ 11.10.2026 (В3 S1): каркас — отказ с ИТОГ и quit, второй барьер на все опкоды
			if (AsdaSelfTestKit::RefuseStart(W, TEXT("asda.Trade.SelfTestStoreL5"), KitArgs)) { return; }
			const float Delay = AsdaSelfTestKit::DelayArg(Args);
			const bool bQuit = Args.Contains(TEXT("quit"));
			TWeakObjectPtr<UWorld> WW(W);
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WW, bQuit](float) -> bool
			{
				UWorld* Wl = WW.Get();
				if (!Wl) { return false; }
				if (AsdaSelfTestKit::RefuseDeferred(Wl, TEXT("asda.Trade.SelfTestStoreL5"), bQuit)) { return false; }   // Р-2: офлайн повторно — на отложенном старте
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
					TSharedRef<FAsdaSelfStoreL5SelfTest> T = MakeShared<FAsdaSelfStoreL5SelfTest>();
					T->Run(W2);
					T->BuildInputPhase();
					TSharedRef<int32> Idx = MakeShared<int32>(0);
					FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WW, T, Idx, bQuit](float) -> bool
					{
						UWorld* W3 = WW.Get();
						if (!W3) { AsdaCVarSave::RestoreAll(TEXT("asda.Trade.SelfTestStoreL5")); return false; }   // ★ ИСПРАВЛЕНИЕ 11.10.2026 (В0, N1(д)/U8): ключи — и при уходе мира
						if (*Idx < T->Steps.Num()) { T->Steps[(*Idx)++](W3); return true; }
						UGameInstance* GI3 = W3->GetGameInstance();
						if (UAsdaNetSubsystem* Net3 = GI3 ? AsdaTestNet::Get(GI3) : nullptr) { T->Disarm(Net3); }
						if (UAsdaTradeStateSubsystem* TS3 = GI3 ? GI3->GetSubsystem<UAsdaTradeStateSubsystem>() : nullptr) { TS3->ResetSessionState(EAsdaSessionResetReason::MapChanged); }
						if (UAsdaSelfStoreSubsystem* S3 = GI3 ? GI3->GetSubsystem<UAsdaSelfStoreSubsystem>() : nullptr) { S3->ResetSessionState(EAsdaSessionResetReason::MapChanged); }
						if (UAsdaWindowSubsystem* WM3 = GI3 ? GI3->GetSubsystem<UAsdaWindowSubsystem>() : nullptr) { WM3->ClearMsgBoxQueue(TEXT("самотест Л5: итог")); }
						// ★ ИСПРАВЛЕНИЕ 11.10.2026 (В0, N1(д)/U8): ключ Trade.StoreFixK17 — к исходному значению И приоритету SetBy (прежде шаги
						//   возвращали константу 1 с SetByConsole).
						AsdaCVarSave::RestoreAll(TEXT("asda.Trade.SelfTestStoreL5"));
						UE_LOG(LogAsdaStoreTestL5, Log, TEXT("САМОТЕСТ-ЛАВКА-Л5 ИТОГ: ok=%d fail=%d"), T->Ok, T->Fail);
						T->SelfScope.Restore();   // ★ В3 S1 (N1(г)): «свой» — как до прогона
						AsdaSelfTestKit::Quit(W3, bQuit);
						return false;
					}), 0.4f);
					return false;
				}), bNeedSpawn ? 2.f : 0.f);
				return false;
			}), Delay);
			UE_LOG(LogAsdaStoreTestL5, Log, TEXT("САМОТЕСТ-ЛАВКА-Л5: запуск через %.1f с"), Delay);
		}));
}

#endif // !UE_BUILD_SHIPPING
