// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ ВОЛНЫ Л2 ТОРГОВЛИ — ЛИЧНАЯ ЛАВКА, ПОСЕТИТЕЛЬ (30.09.2026, `Reports/trade/wave_L2.md`). [ПОРТ]
//
// ЧТО ПРОВЕРЯЕТ. Путь посетителя через НАСТОЯЩИЕ функции: вход `RequestVisit` (гейты + 6307), приём 6308/6310/6311/6312/6315/6306
//   через ШТАТНЫЙ `UAsdaNetSubsystem::DebugFeedPacket` → `HandlePacket` (длины, дополнение 6308, делегат, очередь, разбор),
//   отправки 6307/6309/6305 и 5455 — БОЕВЫЕ отправители под перехватом (`ArmSendCapture`, весь трафик: офлайн сокета нет),
//   корзину (нумпад 8, ПКМ, дубль, распродан, 7-я позиция, клик по ячейке, урна, сумма и цвет, 107056), msgbox 62, 107084/107081,
//   раскладку 6309 (пустые позиции — код 0), `StoreBuyGuard`, 6310 все коды, 6311 (продавец, К-8), 6312 (К-2), 6315, журнал 23402,
//   6306 гостю, ESC, смерть гостя, сброс сессии, 5451 0x16/0x17 (5455 только при транспорте). Затем — фаза «путём игрока»:
//   настоящие клики `SimulateClickAt` по живой геометрии (ЛКМ/ПКМ по лотам, ячейка корзины, кнопки окна, кнопки нумпада и msgbox),
//   наведение (подсказка), и геометрия узлов посетителя против `.JUI`.
//   ★ Контрольные пары (закон pixel-metric-needs-control-pairs): каждое действие проверяется и в противоположном условии.
//
// Консоль: `asda.Trade.SelfTestStoreL2 [delay=N] [quit]`. Только офлайн (`FAsdaOutgoingGate::RefuseDebugWhenOnline`).
//   Строки — «САМОТЕСТ-ЛАВКА-Л2», итог — «САМОТЕСТ-ЛАВКА-Л2 ИТОГ: ok=… fail=…».
#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "UI/AsdaSelfStoreSubsystem.h"
#include "AsdaCVarSave.h"                   // ★ ИСПРАВЛЕНИЕ 11.10.2026 (В0, N1(д)/U8): ключи консоли самотеста — запомнить и вернуть (значение + SetBy)
#include "UI/AsdaTradeStateSubsystem.h"
#include "UI/AsdaInventorySubsystem.h"
#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaEffectsSubsystem.h"
#include "UI/AsdaNotice.h"
#include "UI/AsdaUiHelpers.h"
#include "UI/AsdaShopDblClick.h"     // AsdaSimDoubleClickAt — лот в корзину двойным щелчком (10.10.2026)
#include "World/AsdaPlayerController.h"
#include "World/AsdaWorldSubsystem.h"
#include "AsdaNetSubsystem.h"
#include "Test/AsdaSelfTestKit.h"   // ★ 11.10.2026 (В3 S1): общий каркас самотестов (аргументы, отказ, перехват, итог, выход)
#include "AsdaSelf.h"
#include "AsdaBytes.h"
#include "Login/AsdaLoginData.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/EditableText.h"
#include "Framework/Application/SlateApplication.h"   // курсор для наведения (путь игрока)
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Misc/OutputDevice.h"
#include "Misc/OutputDeviceRedirector.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaStoreTestL2, Log, All);

namespace AsdaStoreL2Local
{
	// Перехватчик строк маршрутизатора сообщений «[msg] …» (канал и цвет нотиса) — как в самотесте Л1.
	class FMsgSink : public FOutputDevice
	{
	public:
		TArray<FString> Lines;   // строки категории LogAsdaMsg, начинающиеся с «[msg] »
		FCriticalSection Lock;   // лог приходит с любого потока
		virtual void Serialize(const TCHAR* V, ELogVerbosity::Type, const FName& Category) override
		{
			static const FName kMsg(TEXT("LogAsdaMsg"));
			if (!V || Category != kMsg || FCString::Strncmp(V, TEXT("[msg] "), 6) != 0) { return; }
			FScopeLock L(&Lock);
			Lines.Add(V);
		}
		virtual bool CanBeUsedOnAnyThread() const override { return true; }
		virtual bool CanBeUsedOnMultipleThreads() const override { return true; }   // ★ 05.11.2026: НЕбуферизованный список UE 5.8 — строка доходит в момент записи (иначе гонка с потоком логов: строка после RemoveOutputDevice/проверки)
		void Reset() { if (GLog) { GLog->Flush(); } FScopeLock L(&Lock); Lines.Reset(); }
		// Сколько строк несут текст (первые 14 символов) и все подстроки Needles.
		int32 Hits(const FString& Text, std::initializer_list<const TCHAR*> Needles)
		{
			if (GLog) { GLog->Flush(); }
			const FString Key = Text.Left(14);
			FScopeLock L(&Lock);
			int32 N = 0;
			for (const FString& S : Lines)
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
	constexpr int32 kBuyerUserNum = 56;     // фейковый сосед-покупатель
	constexpr int32 kHostCharId = 900055;
	constexpr int32 kBuyerCharId = 900056;

	// Запись 45 б пакетов 6311/6312: код, индекс лота, остаток (−1 — распродан).
	struct FSoldRec { int32 Code; int32 Idx; int32 Rem; };
	// Лот 6308 (55 б с +78): код, кол-во, цена; прочие поля — заточка/гнёзда для подсказки.
	struct FLotSpec { int32 Code; int32 Qty; int32 Price; };

	// Тело 6308 (628 б): [0] код, @1 charId хозяина, @5 userNum, @7 число лотов, @8 название[50], @58 имя[20], лоты с @78.
	TArray<uint8> Make6308(uint8 Code, const FString& Title, std::initializer_list<FLotSpec> LotsIn, int32 HostCharId = kHostCharId, int32 HostUserNum = kHostUserNum)
	{
		TArray<uint8> B; B.SetNumZeroed(628);
		B[0] = Code;
		AsdaBytes::WrI32(B.GetData() + 1, HostCharId);
		AsdaBytes::WrI16(B.GetData() + 5, HostUserNum);
		B[7] = (uint8)LotsIn.size();
		FAsdaLoginData::EncodeCp1251(Title, B.GetData() + 8, 49);
		FAsdaLoginData::EncodeCp1251(TEXT("Продавец"), B.GetData() + 58, 19);
		int32 i = 0;
		for (const FLotSpec& L : LotsIn)
		{
			uint8* P = B.GetData() + 78 + 55 * i++;
			AsdaBytes::WrI32(P + 0, L.Code);
			AsdaBytes::WrI32(P + 4, L.Qty);
			AsdaBytes::WrI32(P + 45, L.Price);
			P[8] = 30;                       // прочность
		}
		return B;
	}
}

struct FAsdaSelfStoreL2SelfTest
{
	// ★ 11.10.2026 (В3 S1, N1(г)): «свой» персонаж (charId, userNum, уровень…) подменяется в Setup — возврат RAII каркаса.
	FAsdaNetSelfScope SelfScope;
	int32 Ok = 0;     // пройдено проверок
	int32 Fail = 0;   // провалено проверок

	// Перехваченный исходящий пакет.
	struct FCap { uint16 Op = 0; TArray<uint8> P; };
	TSharedRef<TArray<FCap>> Caps = MakeShared<TArray<FCap>>();
	uint32 Gen = 0;   // поколение перехватчика
	// ★ 30.09.2026 (ревизия О4, МЕЛК-4): деньги и ячейка 20551 до теста — `SetGoldDisplay` с О4 пишет и ячейку (`invGetFreeSlotByMarker`),
	//   поэтому в конце возвращаем оба (−1 — ячейки денег не было).
	int32 GoldWas = 0;
	int32 GoldCellWas = -1;
	TArray<TFunction<void(UWorld*)>> Steps;   // шаги фазы «путём игрока» (по одному за тик планировщика)
	TSharedPtr<FAsdaSimChainResult> Chain;    // ★ 10.10.2026: итог текущей цепочки быстрых щелчков (планировщик ждёт её конца)
	//   ★ R5-fix-b 13.11.2026 (заключительный заход): случай-цепочка доказывает «НЕ кладёт» только если щелчок перед снимком отпущен ВНУТРИ окна двойного
	//   (0,5 с). Под нагрузкой (три игры, ~15 к/с) четыре щелчка по 4 кадра не укладывались (0,595 / 0,521 с) — опыт не
	//   состоялся, а проверка падала. Такой случай повторяется заново (до kChainTries попыток, пауза — окно двойного + 0,35 с);
	//   проверка и её условия те же, в счёт идёт последняя попытка.
	static constexpr int32 kChainTries = 4;   // попыток случая-цепочки
	int32 ChainTries = 0;                     // сделано повторов текущего случая
	int32 Rewind = 0;                         // планировщику: вернуться на столько шагов назад (повтор случая)
	double HoldUntil = 0.0;                   // планировщику: не раньше этого момента (пауза окна двойного перед повтором)
	int32 SnapCart = -1;                      // корзина в момент снимка (после щелчка перед снимком)
	bool bSnapNum = false;                    // нумпад был открыт в момент снимка

	// Одна проверка.
	void Expect(bool bCond, const FString& Name, const FString& Detail)
	{
		if (bCond) { ++Ok; } else { ++Fail; }
		UE_LOG(LogAsdaStoreTestL2, Log, TEXT("САМОТЕСТ-ЛАВКА-Л2 %s: %s — %s"), bCond ? TEXT("ok") : TEXT("ПРОВАЛ"), *Name, *Detail);
	}

	// Сколько перехвачено пакетов опкода; последний — в Out.
	int32 Count(uint16 Op, TArray<uint8>* Out = nullptr) const
	{
		int32 N = 0;
		for (const FCap& C : *Caps) { if (C.Op == Op) { ++N; if (Out) { *Out = C.P; } } }
		return N;
	}

	// Подать пакет штатным приёмом и разобрать очередь лавки (её разбирает Tick; здесь — сразу).
	static void Feed(UAsdaNetSubsystem* Net, UAsdaSelfStoreSubsystem* S, int32 Op, const TArray<uint8>& Body)
	{
		Net->DebugFeedPacket(Op, Body);
		S->PumpPending();
	}
	static TArray<uint8> Zero(int32 N) { TArray<uint8> B; B.SetNumZeroed(N); return B; }

	// Верхний msgbox очереди содержит текст loc-строки (первые 14 символов).
	static bool TopMsgIs(UGameInstance* GI, UAsdaWindowSubsystem* WM, int32 LocId)
	{
		const FString Want = AsdaNotice::Loc(GI, LocId).Left(14);
		return WM && !Want.IsEmpty() && WM->GetMsgBoxQueueTopText().Contains(Want);
	}
	static bool JournalHas(const UAsdaSelfStoreSubsystem* S, const TCHAR* Needle)
	{
		for (const FString& L : S->GetJournal()) { if (L.Contains(Needle)) { return true; } }
		return false;
	}
	static bool LogHas(const UAsdaSelfStoreSubsystem* S, const FString& Needle)
	{
		for (const FString& L : S->DebugLogLines()) { if (L.Contains(Needle)) { return true; } }
		return false;
	}
	// Ввести число в поле нумпада 1412 и нажать «OK» 1415 (делегатом кнопки — фаза «путём игрока» кликает по-настоящему).
	static bool NumPadEnter(UAsdaWindowSubsystem* WM, int32 Qty)
	{
		UUserWidget* NW = WM->FindWindow(FName(TEXT("NUMPAD_WIN")));
		UEditableText* E = NW ? Cast<UEditableText>(NW->GetWidgetFromName(TEXT("NUMPAD_NUMBOX"))) : nullptr;
		if (!E) { return false; }
		E->SetText(FText::FromString(FString::Printf(TEXT("%d"), Qty)));
		WM->CloseNumPad(true);
		return true;
	}
	// Открыть витрину гостя штатным 6308/1 (сбросив прежнее состояние).
	static void OpenVisitor(UAsdaNetSubsystem* Net, UAsdaSelfStoreSubsystem* S, std::initializer_list<AsdaStoreL2Local::FLotSpec> Lots)
	{
		S->ResetSessionState(EAsdaSessionResetReason::MapChanged);
		Feed(Net, S, 6308, AsdaStoreL2Local::Make6308(1, TEXT("Лавка Гостя"), Lots));
	}

	void Run(UWorld* W)
	{
		using namespace AsdaStoreL2Local;
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaSelfStoreSubsystem* S = GI ? GI->GetSubsystem<UAsdaSelfStoreSubsystem>() : nullptr;
		UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
		UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
		UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
		UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
		UAsdaEffectsSubsystem* Eff = GI ? GI->GetSubsystem<UAsdaEffectsSubsystem>() : nullptr;
		if (!S || !Net || !WM || !Inv || !WS || !Eff)
		{
			UE_LOG(LogAsdaStoreTestL2, Error, TEXT("САМОТЕСТ-ЛАВКА-Л2 ИТОГ: ok=0 fail=1 (нет подсистем)"));
			return;
		}
		// ── Подготовка: свой персонаж, соседи (продавец 55, покупатель 56), пустая сумка, золото, перехват всего трафика ──
		SelfScope.Capture(Net, TEXT("asda.Trade.SelfTestStoreL2"));   // ★ В3 S1: снимок «своего» до подмены
		Net->SelectedCharId = kSelfCharId;
		Net->SetSelfUserNumForDebug(kSelfUserNum);
		Net->SelfLevel = 30;
		WS->DebugSpawnFakePeer(kHostUserNum, TEXT("Продавец"), FVector2D(258.f, 256.f));
		WS->DebugSpawnFakePeer(kBuyerUserNum, TEXT("Покупатель"), FVector2D(254.f, 256.f));
		S->ResetSessionState(EAsdaSessionResetReason::MapChanged);
		GoldWas = Inv->GetGold();   // ревизия О4 МЕЛК-4: деньги и ячейка 20551 — вернуть в конце
		for (int32 i = 0; i < 60 && GoldCellWas < 0; ++i) { if (const FAsdaItemInstance* G = Inv->GetCell(2, i)) { if (G->ItemCode == 20551) { GoldCellWas = G->Amount; } } }
		for (int32 i = 0; i < 60; ++i) { Inv->DebugPutItem(2, i, 0, 0); }
		for (int32 i = 0; i < 30; ++i) { Inv->DebugPutItem(1, i, 0, 0); }
		Inv->SetGoldDisplay(100000);
		Caps->Reset();
		TSharedRef<TArray<FCap>> C = Caps;
		Gen = AsdaSelfTestKit::ArmCapture(Net, [C](uint16 Op, const uint8* P, int32 Len, bool, TCHAR)
		{
			FCap X; X.Op = Op; if (P && Len > 0) { X.P.Append(P, Len); } C->Add(MoveTemp(X));
		}, 0, TEXT("asda.Trade.SelfTestStoreL2"));
		WM->ClearMsgBoxQueue(TEXT("самотест лавки Л2"));
		Expect(WS->GetNetPlayerCharId(kHostUserNum) == kHostCharId && WS->GetNetPlayerCharId(kBuyerUserNum) == kBuyerCharId,
			TEXT("подготовка: соседи 55/56 в мире (charId 900055/900056)"),
			FString::Printf(TEXT("55→%d 56→%d"), WS->GetNetPlayerCharId(kHostUserNum), WS->GetNetPlayerCharId(kBuyerUserNum)));

		// ═══ 1. Вход: гейты `tradeRequestTargetByServerIdx` + 6307 ═══
		{
			S->LastVisitReqAt = -10.0; Caps->Reset();
			const bool bNone = S->RequestVisit(99, TEXT("самотест: сосед 99"));
			const int32 N0 = Count(6307);
			const bool bOk = S->RequestVisit(kHostUserNum, TEXT("самотест: сосед 55"));
			TArray<uint8> P; const int32 N1 = Count(6307, &P);
			Expect(!bNone && N0 == 0 && bOk && N1 == 1 && P.Num() == 10 && AsdaBytes::RdI32(P.GetData()) == kSelfCharId
				&& AsdaBytes::RdI32(P.GetData() + 4) == kHostCharId && AsdaBytes::RdI16(P.GetData() + 8) == kHostUserNum,
				TEXT("6307: соседа нет — молча ↔ сосед 55 → тело 10 б (свой charId @0, charId хозяина @4, userNum @8)"),
				FString::Printf(TEXT("нет=%d/%d есть=%d/%d длина=%d @0=%d @4=%d @8=%d"), bNone ? 1 : 0, N0, bOk ? 1 : 0, N1, P.Num(),
					P.Num() >= 4 ? AsdaBytes::RdI32(P.GetData()) : 0, P.Num() >= 8 ? AsdaBytes::RdI32(P.GetData() + 4) : 0, P.Num() >= 10 ? AsdaBytes::RdI16(P.GetData() + 8) : 0));
			Caps->Reset();
			const bool bThr = S->RequestVisit(kHostUserNum, TEXT("самотест: повтор < 1 с"));
			S->LastVisitReqAt = -10.0;
			const bool bAfter = S->RequestVisit(kHostUserNum, TEXT("самотест: повтор > 1 с"));
			Expect(!bThr && bAfter && Count(6307) == 1, TEXT("6307: троттл 1000 мс — молча ↔ по истечении — шлёт"),
				FString::Printf(TEXT("сразу=%d потом=%d 6307=%d"), bThr ? 1 : 0, bAfter ? 1 : 0, Count(6307)));
			// Режим хозяина открытой лавки — молча (контроль — выше).
			S->bSpecialShop = true; S->LastVisitReqAt = -10.0; Caps->Reset();
			const bool bOwner = S->RequestVisit(kHostUserNum, TEXT("самотест: я хозяин"));
			S->bSpecialShop = false;
			Expect(!bOwner && Count(6307) == 0, TEXT("6307: я хозяин открытой лавки — молча"), FString::Printf(TEXT("6307=%d"), Count(6307)));
		}

		// ═══ 2. 6308: коды, укороченный пакет, разбор, режим посетителя ═══
		{
			WM->ClearMsgBoxQueue(TEXT("самотест"));
			Feed(Net, S, 6308, Make6308(0, TEXT(""), {}));
			const bool b0 = TopMsgIs(GI, WM, 107070) && !S->IsShowcaseOpen();
			WM->ClearMsgBoxQueue(TEXT("самотест"));
			Feed(Net, S, 6308, Make6308(2, TEXT(""), {}));
			const bool b2 = TopMsgIs(GI, WM, 107072) && !S->IsShowcaseOpen();
			WM->ClearMsgBoxQueue(TEXT("самотест"));
			TArray<uint8> Short; Short.SetNumZeroed(4);   // «Same User Error»: сервер шлёт 6308 длиной 4
			Feed(Net, S, 6308, Short);
			const bool bShort = TopMsgIs(GI, WM, 107070) && !S->IsShowcaseOpen();
			WM->ClearMsgBoxQueue(TEXT("самотест"));
			Feed(Net, S, 6308, Make6308(7, TEXT(""), {}));
			const bool b7 = WM->GetMsgBoxQueueNum() == 0 && !S->IsShowcaseOpen();
			Expect(b0 && b2 && bShort && b7, TEXT("6308: 0 → 107070, 2 → 107072, длина 4 (дополнение нулями) → 107070, 7 → молча; витрина закрыта"),
				FString::Printf(TEXT("0=%d 2=%d короткий=%d 7=%d"), b0 ? 1 : 0, b2 ? 1 : 0, bShort ? 1 : 0, b7 ? 1 : 0));
			// Код 1 (контроль): лоты — 20592 ×7 по 1000, 20571 ×3 по 50, снаряжение 20552 кол-во 0 по 5000, 20572 распродан, ещё 5 лотов.
			OpenVisitor(Net, S, { {20592, 7, 1000}, {20571, 3, 50}, {20552, 0, 5000}, {20572, -1, 10}, {20573, 2, 10}, {20577, 2, 10},
				{20578, 2, 10}, {20579, 2, 10}, {20583, 2, 10} });
			Expect(S->IsVisitorShowcaseOpen() && UAsdaTradeStateSubsystem::IsSelfStoreOpen(GI) && S->GetLotCount() == 9 && S->GetHostCharId() == kHostCharId
				&& S->GetHostUserNum() == kHostUserNum && S->GetStoreTitle() == TEXT("Лавка Гостя") && S->IsBuyPending() && S->GetCartCount() == 0,
				TEXT("6308 код 1 → витрина посетителя, признак «в лавке», хозяин 900055/55, название, 9 лотов, «можно спросить 62»"), S->DumpState());
			Expect(S->DebugShowcaseText(23305) == AsdaNotice::Loc(GI, 107006) && S->DebugShowcaseText(23301) == AsdaNotice::Loc(GI, 107004)
				&& S->DebugListHdrShown() && S->DebugTrashShown() && S->DebugShowcaseText(23006) == TEXT("Лавка Гостя"),
				TEXT("режим посетителя: 23305 «OK» 107006, 23301 107004, группа корзины 23240 и урна видны ↔ у хозяина (Л1) скрыты"),
				FString::Printf(TEXT("23305 «%s» 23301 «%s» 23240=%d урна=%d"), *S->DebugShowcaseText(23305), *S->DebugShowcaseText(23301),
					S->DebugListHdrShown() ? 1 : 0, S->DebugTrashShown() ? 1 : 0));
			Expect(S->DebugShowcaseText(23122) == TEXT("1,000") && S->DebugShowcaseText(23123) == TEXT("7,000") && S->DebugShowcaseText(23121) == TEXT("7")
				&& S->DebugLotStock(2) == 1 && S->DebugLotStock(3) == -1
				&& FMath::IsNearlyEqual(S->DebugLotAlpha(3), 125.f / 255.f, 0.01f) && FMath::IsNearlyEqual(S->DebugLotAlpha(1), 1.f, 0.01f),
				TEXT("лоты: цена/итог/остаток; кол-во 0 → 1; распроданный лот тускнеет (0x7D) ↔ соседний — нет"),
				FString::Printf(TEXT("23122 «%s» 23123 «%s» 23121 «%s» остаток2=%d остаток3=%d альфа3=%.3f альфа1=%.3f"), *S->DebugShowcaseText(23122),
					*S->DebugShowcaseText(23123), *S->DebugShowcaseText(23121), S->DebugLotStock(2), S->DebugLotStock(3), S->DebugLotAlpha(3), S->DebugLotAlpha(1)));
			Expect(S->GetFoundControlsVisitor() == 3, TEXT("разбор 23000 (посетитель): урна 23243, «в конец» 23404, журнал 23402 (строится кодом)"), S->DumpControls());
			// 6308 при открытой витрине — пакет целиком игнорируется.
			Feed(Net, S, 6308, Make6308(1, TEXT("Другая"), { {20571, 1, 1} }));
			Expect(S->GetLotCount() == 9 && S->GetStoreTitle() == TEXT("Лавка Гостя"), TEXT("6308 при открытой витрине — игнор"),
				FString::Printf(TEXT("лотов=%d «%s»"), S->GetLotCount(), *S->GetStoreTitle()));
			// Гейт 6307 при открытой витрине — молча.
			S->LastVisitReqAt = -10.0; Caps->Reset();
			const bool bOpenV = S->RequestVisit(kHostUserNum, TEXT("самотест: витрина открыта"));
			Expect(!bOpenV && Count(6307) == 0, TEXT("6307 при открытой витрине — молча"), FString::Printf(TEXT("6307=%d"), Count(6307)));
		}

		// ═══ 3. Корзина: нумпад 8, ПКМ, дубль, распродан, штучный, 7-я позиция, сумма ═══
		{
			using namespace AsdaStoreL2Local;
			FMsgSink Sink; GLog->AddOutputDevice(&Sink);
			WM->ClearMsgBoxQueue(TEXT("самотест"));
			S->OnLotClicked(0, false);
			const bool bNum = WM->IsNumPadOpen();
			const int32 Cart0 = S->GetCartCount();
			const bool bTyped = NumPadEnter(WM, 5);
			Expect(bNum && Cart0 == 0 && bTyped && S->GetCartCount() == 1 && S->DebugCartQty(0) == 5 && S->DebugCartText(0) == TEXT("5")
				&& S->DebugCartCode(0) == 20592 && FMath::IsNearlyEqual(S->DebugLotAlpha(0), 125.f / 255.f, 0.01f),
				TEXT("ЛКМ по лоту 20592 ×7 → нумпад режим 8; ввод 5 + OK → позиция корзины ×5, лот в витрине тускнеет"),
				FString::Printf(TEXT("нумпад=%d до=%d введено=%d корзина=%d кол-во=%d «%s» код=%d альфа=%.3f"), bNum ? 1 : 0, Cart0, bTyped ? 1 : 0,
					S->GetCartCount(), S->DebugCartQty(0), *S->DebugCartText(0), S->DebugCartCode(0), S->DebugLotAlpha(0)));
			S->OnLotClicked(1, true);
			Expect(!WM->IsNumPadOpen() && S->GetCartCount() == 2 && S->DebugCartQty(1) == 3 && S->DebugShowcaseText(23302) == TEXT("5,150"),
				TEXT("ПКМ (0xE) по лоту 20571 ×3 → весь остаток без нумпада; сумма 23302 = 5×1000 + 3×50 = 5,150"),
				FString::Printf(TEXT("нумпад=%d корзина=%d кол-во=%d сумма «%s»"), WM->IsNumPadOpen() ? 1 : 0, S->GetCartCount(), S->DebugCartQty(1), *S->DebugShowcaseText(23302)));
			const FLinearColor Yellow = FLinearColor::FromSRGBColor(FColor(0xFF, 0xFF, 0x00, 0xFF));
			Expect(S->DebugTotalColor().Equals(Yellow, 0.01f) && WM->GetMsgBoxQueueNum() == 0, TEXT("золота 100 000 ≥ сумме → сумма жёлтым, без 107056"),
				FString::Printf(TEXT("цвет=%s msgbox=%d"), *S->DebugTotalColor().ToString(), WM->GetMsgBoxQueueNum()));
			S->OnLotClicked(0, true);   // дубль (код + строка) — молча
			S->OnLotClicked(3, true);   // распродан — молча
			Expect(S->GetCartCount() == 2 && JournalHas(S, TEXT("распродан")), TEXT("дубль позиции и распроданный лот — молча"),
				FString::Printf(TEXT("корзина=%d"), S->GetCartCount()));
			S->OnLotClicked(2, false);  // снаряжение 20552 (tradableFlag 0) — без нумпада, кол-во = остаток 1
			Expect(!WM->IsNumPadOpen() && S->GetCartCount() == 3 && S->DebugCartQty(2) == 1, TEXT("ЛКМ по снаряжению (не торгуемое штучно) → без нумпада, кол-во 1"),
				FString::Printf(TEXT("нумпад=%d корзина=%d кол-во=%d"), WM->IsNumPadOpen() ? 1 : 0, S->GetCartCount(), S->DebugCartQty(2)));
			S->OnLotClicked(4, true); S->OnLotClicked(5, true); S->OnLotClicked(6, true);
			const int32 Full = S->GetCartCount();
			Sink.Reset();
			S->OnLotClicked(7, true);
			const FString T107086 = AsdaNotice::Loc(GI, 107086);
			const int32 Pl = Sink.Hits(T107086, { TEXT("WARMESSAGE221"), TEXT("#FFFF00") });
			const int32 Ch = Sink.Hits(T107086, { TEXT("сисчат"), TEXT("#FFFF00") });
			Expect(Full == 6 && S->GetCartCount() == 6 && Pl == 1 && Ch == 1, TEXT("7-я позиция → 107086: строка сисчата + плашка 221, жёлтым; позиций 6"),
				FString::Printf(TEXT("до=%d после=%d плашка=%d чат=%d"), Full, S->GetCartCount(), Pl, Ch));
			GLog->RemoveOutputDevice(&Sink);
		}

		// ═══ 4. Мало золота → красным + 107056; клик по ячейке корзины (выбор + снять), урна ═══
		{
			WM->ClearMsgBoxQueue(TEXT("самотест"));
			Inv->SetGoldDisplay(100);
			S->OnCartCellClicked(5);   // снять 6-ю позицию: перерисовка суммы при золоте 100 < суммы
			const FLinearColor Red = FLinearColor::FromSRGBColor(FColor(0xFF, 0x00, 0x00, 0xFF));
			const bool bRed = S->DebugTotalColor().Equals(Red, 0.01f);
			const bool bMsg = TopMsgIs(GI, WM, 107056);
			Expect(S->GetCartCount() == 5 && bRed && bMsg && !S->DebugCartSelShown(),
				TEXT("клик по ячейке корзины 5 → позиция снята, рамка скрыта; золото 100 < суммы → сумма красным + msgbox 107056"),
				FString::Printf(TEXT("корзина=%d красный=%d 107056=%d рамка=%d"), S->GetCartCount(), bRed ? 1 : 0, bMsg ? 1 : 0, S->DebugCartSelShown() ? 1 : 0));
			WM->ClearMsgBoxQueue(TEXT("самотест"));
			Inv->SetGoldDisplay(100000);
			S->OnLotClicked(6, true);   // снятый лот снова в корзине → он же «выбранный» ⇒ рамка на его ячейке
			Expect(S->GetCartCount() == 6 && S->DebugCartSelShown() && WM->GetMsgBoxQueueNum() == 0,
				TEXT("выбранный лот снова в корзине → рамка 23241 видна (контроль); золота хватает — без 107056"),
				FString::Printf(TEXT("корзина=%d рамка=%d msgbox=%d"), S->GetCartCount(), S->DebugCartSelShown() ? 1 : 0, WM->GetMsgBoxQueueNum()));
			S->OnTrashClicked();
			Expect(S->GetCartCount() == 5 && !S->DebugCartSelShown(), TEXT("урна 23243 → снят выбранный"), FString::Printf(TEXT("корзина=%d"), S->GetCartCount()));
		}

		// ═══ 5. 23305: msgbox 62 → 6309 (раскладка), 107081 ↔ после 6310 снова 62; пустая корзина → 107084 ═══
		{
			WM->ClearMsgBoxQueue(TEXT("самотест")); Caps->Reset();
			S->OnShowcaseMainButton();
			const bool b62 = WM->IsMsgBoxTop(1, 62) && WM->GetMsgBoxQueueTopText().Contains(AsdaNotice::Loc(GI, 6888).Left(10));
			WM->CloseConfirm(false);   // «Отмена» — пакета нет
			const int32 NCancel = Count(6309);
			S->OnShowcaseMainButton();
			WM->CloseConfirm(true);    // «OK» → 6309
			TArray<uint8> P; const int32 N = Count(6309, &P);
			Expect(b62 && NCancel == 0 && N == 1, TEXT("23305 → msgbox 62 (6888); «Отмена» — без пакета ↔ «OK» → 6309"),
				FString::Printf(TEXT("62=%d отмена→6309=%d OK→6309=%d верх «%s»"), b62 ? 1 : 0, NCancel, N, *WM->GetMsgBoxQueueTopText()));
			const bool bLay = P.Num() == 281 && AsdaBytes::RdI32(P.GetData()) == kSelfCharId && AsdaBytes::RdI32(P.GetData() + 4) == kHostCharId
				&& AsdaBytes::RdI16(P.GetData() + 8) == kHostUserNum && P[10] == 5
				&& AsdaBytes::RdI32(P.GetData() + 11) == 20592 && AsdaBytes::RdI32(P.GetData() + 11 + 7) == 5 && AsdaBytes::RdI16(P.GetData() + 11 + 11) == 0
				&& AsdaBytes::RdI32(P.GetData() + 11 + 13) == 1000
				&& AsdaBytes::RdI32(P.GetData() + 11 + 90) == 20552 && AsdaBytes::RdI32(P.GetData() + 11 + 90 + 7) == 0 && AsdaBytes::RdI16(P.GetData() + 11 + 90 + 11) == 2
				&& AsdaBytes::RdI32(P.GetData() + 11 + 225) == 0 && AsdaBytes::RdI16(P.GetData() + 11 + 225 + 5) == -1 && AsdaBytes::RdI16(P.GetData() + 11 + 225 + 11) == -1;
			Expect(bLay, TEXT("6309: 281 б; свой charId @0, хозяин @4/@8, число 5; позиция 0 — 20592 ×5 индекс 0 цена 1000; нештучное снаряжение — кол-во 0, "
				"индекс 2; пустая позиция 5 — код 0 (не −1), @5/@11 = −1 (`slotStructReset`)"),
				FString::Printf(TEXT("длина=%d число=%d п0: %d×%d и%d ц%d; п2: %d×%d и%d; п5: код %d @5=%d @11=%d"), P.Num(), P.Num() > 10 ? P[10] : -1,
					P.Num() >= 281 ? AsdaBytes::RdI32(P.GetData() + 11) : 0, P.Num() >= 281 ? AsdaBytes::RdI32(P.GetData() + 18) : 0,
					P.Num() >= 281 ? AsdaBytes::RdI16(P.GetData() + 22) : 0, P.Num() >= 281 ? AsdaBytes::RdI32(P.GetData() + 24) : 0,
					P.Num() >= 281 ? AsdaBytes::RdI32(P.GetData() + 101) : 0, P.Num() >= 281 ? AsdaBytes::RdI32(P.GetData() + 108) : 0,
					P.Num() >= 281 ? AsdaBytes::RdI16(P.GetData() + 112) : 0, P.Num() >= 281 ? AsdaBytes::RdI32(P.GetData() + 236) : 0,
					P.Num() >= 281 ? AsdaBytes::RdI16(P.GetData() + 241) : 0, P.Num() >= 281 ? AsdaBytes::RdI16(P.GetData() + 247) : 0));
			WM->ClearMsgBoxQueue(TEXT("самотест")); Caps->Reset();
			S->OnShowcaseMainButton();
			const bool b81 = TopMsgIs(GI, WM, 107081) && !S->IsBuyPending() && Count(6309) == 0;
			WM->ClearMsgBoxQueue(TEXT("самотест"));
			TArray<uint8> R = Zero(350); R[0] = 5;   // 6310 код 5 → 107055 и снова «можно спросить»
			Feed(Net, S, 6310, R);
			const bool b55 = TopMsgIs(GI, WM, 107055) && S->IsBuyPending();
			WM->ClearMsgBoxQueue(TEXT("самотест"));
			S->OnShowcaseMainButton();
			const bool b62b = WM->IsMsgBoxTop(1, 62);
			WM->CloseConfirm(false);
			Expect(b81 && b55 && b62b, TEXT("после 6309: 23305 → 107081 без пакета ↔ после 6310 (код 5 → 107055) — снова msgbox 62"),
				FString::Printf(TEXT("107081=%d 107055=%d 62=%d"), b81 ? 1 : 0, b55 ? 1 : 0, b62b ? 1 : 0));
		}

		// ═══ 6. 6310: все коды; код 1 — вес/золото/записи, корзина пуста ═══
		{
			static const int32 kLoc[10] = { 107050, 0, 107052, 107053, 107054, 107055, 107056, 107057, 107058, 107048 };
			bool bAll = true; FString Det;
			for (int32 Code = 0; Code <= 9; ++Code)
			{
				if (Code == 1) { continue; }
				WM->ClearMsgBoxQueue(TEXT("самотест"));
				TArray<uint8> R = Zero(350); R[0] = (uint8)Code;
				Feed(Net, S, 6310, R);
				const bool bHit = TopMsgIs(GI, WM, kLoc[Code]);
				bAll &= bHit;
				Det += FString::Printf(TEXT("%d→%d:%s "), Code, kLoc[Code], bHit ? TEXT("да") : TEXT("НЕТ"));
			}
			WM->ClearMsgBoxQueue(TEXT("самотест"));
			TArray<uint8> R10 = Zero(350); R10[0] = 10;
			Feed(Net, S, 6310, R10);
			Expect(bAll && WM->GetMsgBoxQueueNum() == 0, TEXT("6310 коды 0/2…9 → 107050/107052…107058/107048 (код 6 — и отказ P43 G4) ↔ 10 — молча"), Det);
			TArray<uint8> R = Zero(350); R[0] = 1;
			AsdaBytes::WrI16(R.GetData() + 1, 321);
			AsdaBytes::WrI32(R.GetData() + 3, 77777);
			uint8* Rec = R.GetData() + 8;
			AsdaBytes::WrI32(Rec, 20571); Rec[4] = 2; AsdaBytes::WrI16(Rec + 5, 5); AsdaBytes::WrI32(Rec + 9, 3);
			const int32 CartWas = S->GetCartCount();
			Feed(Net, S, 6310, R);
			const FAsdaItemInstance* It = Inv->GetCell(2, 5);
			Expect(CartWas > 0 && S->GetCartCount() == 0 && Inv->GetGold() == 77777 && Net->SelfWeightCur == 321 && It && It->ItemCode == 20571 && It->Amount == 3
				&& S->IsBuyPending() && S->DebugShowcaseText(23302) == TEXT("0"),
				TEXT("6310 код 1 → вес 321, золото 77 777, запись 20571 ×3 в ячейку 2:5, корзина пуста, сумма 0"),
				FString::Printf(TEXT("корзина %d→%d золото=%d вес=%d ячейка=%d×%d сумма «%s»"), CartWas, S->GetCartCount(), Inv->GetGold(), Net->SelfWeightCur,
					It ? It->ItemCode : -1, It ? It->Amount : -1, *S->DebugShowcaseText(23302)));
			Inv->DebugPutItem(2, 5, 0, 0);
			Inv->SetGoldDisplay(100000);
		}

		// ═══ 7. [ПОРТ] StoreBuyGuard: кол-во × цена > 2^31−1 не отправляется ↔ ≤ — уходит; ключ 0 — как оригинал ═══
		//   ★ Волна Л4/Л5 (ревизия Л3, МЕЛК-2): цена лота — в домене сервера (≤ 1e8 за единицу, S:100221; P43 не установлен).
		//   ★ Ревизия Л4/Л5 (МЕЛК-1): порог защиты — заворот i32 (22 × 1e8 = 2,2 млрд > 2^31−1 — блок; 21 × 1e8 — уходит). Строку > 1e8
		//   (кол-во × цена, v80, S:100886/100891) сервер и так отвергает кодом 0 — пара проверяет только порог защиты; опасен заворот
		//   (43 × 1e8 → 5 032 704 — такую строку сервер ПРИМЕТ).
		{
			OpenVisitor(Net, S, { {20592, 22, 100000000}, {20571, 21, 100000000} });
			S->OnLotClicked(0, true);   // 22 × 100 млн
			WM->ClearMsgBoxQueue(TEXT("самотест")); Caps->Reset();
			S->OnBuyConfirmed();
			const bool bBlocked = Count(6309) == 0 && TopMsgIs(GI, WM, 107050) && S->IsBuyPending();
			S->OnCartCellClicked(0);
			S->OnLotClicked(1, true);   // 21 × 100 млн = 2 100 000 000 < 2^31−1
			WM->ClearMsgBoxQueue(TEXT("самотест")); Caps->Reset();
			S->OnBuyConfirmed();
			const bool bSent = Count(6309) == 1;
			Expect(bBlocked && bSent, TEXT("[ПОРТ] 6309: 22 × 100 млн > 2^31−1 → не ушёл, 107050, «можно спросить» цел ↔ 21 × 100 млн → ушёл"),
				FString::Printf(TEXT("блок=%d ушёл=%d"), bBlocked ? 1 : 0, bSent ? 1 : 0));
			IConsoleVariable* Cv = IConsoleManager::Get().FindConsoleVariable(TEXT("asda.Trade.StoreBuyGuard"));
			OpenVisitor(Net, S, { {20592, 22, 100000000} });
			S->OnLotClicked(0, true);
			if (Cv) { AsdaCVarSave::SetForTest(TEXT("asda.Trade.StoreBuyGuard"), 0); }
			Caps->Reset();
			S->OnBuyConfirmed();
			const int32 NOrig = Count(6309);
			if (Cv) { AsdaCVarSave::SetForTest(TEXT("asda.Trade.StoreBuyGuard"), 1); }
			Expect(NOrig == 1, TEXT("ключ StoreBuyGuard = 0 → как оригинал (6309 уходит, сервер ответит 6310/0)"), FString::Printf(TEXT("6309=%d"), NOrig));
			WM->ClearMsgBoxQueue(TEXT("самотест"));
			// ★ Ревизия Л2 (МЕЛК-2): стоимость — по правилу СЕРВЕРА. 20562 («заточка 4 + тип 10» — сервер берёт цену лота) и спец-предмет 18
			//   (не `item_list`, лежит в сумке — цена лота) 22 × 100 млн уходят ↔ 20592 (`item_list`, тип 11 — сервер умножает) — блок (выше).
			auto BuyAll = [&](int32 Code) -> int32
			{
				OpenVisitor(Net, S, { {Code, 22, 100000000} });
				S->OnLotClicked(0, true);
				WM->ClearMsgBoxQueue(TEXT("самотест")); Caps->Reset();
				S->OnBuyConfirmed();
				const int32 N = Count(6309);
				WM->ClearMsgBoxQueue(TEXT("самотест"));
				return N;
			};
			const int32 NEx = BuyAll(20562);
			const int32 NSp = BuyAll(18);
			Expect(NEx == 1 && NSp == 1,
				TEXT("[ПОРТ] StoreBuyGuard по правилу сервера: 20562 (цена лота у сервера) 22 × 100 млн → ушёл; спец-предмет 18 22 × 100 млн → ушёл ↔ 20592 → блок"),
				FString::Printf(TEXT("20562: 6309=%d; 18: 6309=%d"), NEx, NSp));
			// ★ Волна Л4/Л5 (ревизия Л3, МЕЛК-1): коды правила — из ДАННЫХ сервера (`DefaultAsdaTrade.ini`). 69682 у клиента 4/11, у сервера
			//   4/10 — сервер берёт цену лота ⇒ 22 × 100 млн уходит (прежний порт давал ложный 107050) ↔ 69681 (4/11 у обоих — сервер
			//   умножает) — блок.
			bool bData = false;
			const bool bRule82 = S->IsServerLotPriceCode(69682, bData);
			const int32 N82 = BuyAll(69682);
			const int32 N81 = BuyAll(69681);
			Expect(bData && bRule82 && N82 == 1 && N81 == 0,
				TEXT("[ПОРТ] МЕЛК-1: правило из данных сервера — 69682 (клиент 4/11, сервер 4/10) 22 × 100 млн → ушёл ↔ 69681 (4/11 у обоих) → блок"),
				FString::Printf(TEXT("данные=%d правило(69682)=%d; 69682: 6309=%d; 69681: 6309=%d"), bData ? 1 : 0, bRule82 ? 1 : 0, N82, N81));
			WM->ClearMsgBoxQueue(TEXT("самотест"));
		}

		// ═══ 8. 6312 (гостям): чужая лавка — игнор ↔ своя: журнал 107082, остатки, К-2 ═══
		{
			auto Make6312 = [](int32 Host, int32 Buyer, std::initializer_list<AsdaStoreL2Local::FSoldRec> Recs)
			{
				TArray<uint8> B; B.SetNumZeroed(281);
				AsdaBytes::WrI32(B.GetData(), Host);
				AsdaBytes::WrI32(B.GetData() + 7, Buyer);
				for (int32 n = 0; n < 6; ++n) { uint8* P = B.GetData() + 11 + 45 * n; AsdaBytes::WrI16(P + 5, -1); AsdaBytes::WrI16(P + 11, -1); }
				int32 n = 0;
				for (const auto& R : Recs)
				{
					uint8* P = B.GetData() + 11 + 45 * n++;
					AsdaBytes::WrI32(P, R.Code); AsdaBytes::WrI32(P + 7, R.Rem); AsdaBytes::WrI16(P + 11, R.Idx);
				}
				return B;
			};
			// ★ StoreStrayGuard (ревизия Л2, СРЕДН-2): витрины посетителя нет — 6312 молча ↔ с витриной (ниже) — разбор.
			S->ResetSessionState(EAsdaSessionResetReason::MapChanged);
			S->DebugSetHost(kHostCharId, kHostUserNum);
			S->ClearJournal();
			Feed(Net, S, 6312, Make6312(kHostCharId, kBuyerCharId, { {20592, 0, 4} }));
			const bool bStray = JournalHas(S, TEXT("StoreStrayGuard")) && S->DebugLogLines().Num() == 0;
			Expect(bStray, TEXT("[ИСПРАВЛЕНИЕ] 6312 без открытой витрины посетителя — молча (StoreStrayGuard)"), S->DumpState());
			OpenVisitor(Net, S, { {20592, 7, 1000}, {20571, 3, 50}, {20573, 2, 10} });
			S->OnLotClicked(0, true); S->OnLotClicked(1, true); S->OnLotClicked(2, true);
			Feed(Net, S, 6312, Make6312(123, kBuyerCharId, { {20592, 0, 4} }));
			const bool bIgn = S->GetCartCount() == 3 && S->DebugLotStock(0) == 7 && S->DebugLogLines().Num() == 0;
			// Проданы лот 1 (вторая запись) и лот 0 (первая): К-2 — обе позиции уходят из корзины.
			Feed(Net, S, 6312, Make6312(kHostCharId, kBuyerCharId, { {20592, 0, 4}, {20571, 1, -1} }));
			// ★ 05.11.2026: ключ — самый длинный текст между метками %s/%d (их подставляет `SubstPrintf` лавки;
			//   RU «' покупает предмет '», EN «' purchased '»); прежний Left(6) в EN давал «'%s' p» — метку, которой
			//   в готовой строке нет (ложный FAIL).
			FString L82;
			{
				TArray<FString> Parts;
				static const TCHAR* const kMarks[] = { TEXT("%s"), TEXT("%d") };   // метки printf строки 107082
				AsdaNotice::Loc(GI, 107082).ParseIntoArray(Parts, kMarks, UE_ARRAY_COUNT(kMarks), true);
				for (const FString& P : Parts) { if (P.Len() > L82.Len()) { L82 = P; } }
			}
			const bool bLog = LogHas(S, TEXT("Покупатель")) && !L82.IsEmpty() && LogHas(S, L82) && S->DebugLogLines().Num() == 2;
			Expect(bIgn && S->GetCartCount() == 1 && S->DebugLotStock(0) == 4 && S->DebugLotStock(1) == -1 && bLog
				&& FMath::IsNearlyEqual(S->DebugLotAlpha(1), 125.f / 255.f, 0.01f),
				TEXT("6312: чужой хозяин — игнор ↔ свой: остатки 4 и −1 (тускнеет), журнал 23402 — 2 строки 107082 «Покупатель…», К-2: из корзины ушли ОБЕ позиции"),
				FString::Printf(TEXT("игнор=%d корзина=%d остатки %d/%d журнал=%d [%s]"), bIgn ? 1 : 0, S->GetCartCount(), S->DebugLotStock(0), S->DebugLotStock(1),
					S->DebugLogLines().Num(), S->DebugLogLines().Num() ? *S->DebugLogLines()[0] : TEXT("")));
			// Контроль К-2 = 0 (оригинал): убирается только позиция с индексом ПЕРВОЙ записи.
			IConsoleVariable* Cv = IConsoleManager::Get().FindConsoleVariable(TEXT("asda.Trade.StoreFixK2"));
			OpenVisitor(Net, S, { {20592, 7, 1000}, {20571, 3, 50}, {20573, 2, 10} });
			S->OnLotClicked(0, true); S->OnLotClicked(1, true); S->OnLotClicked(2, true);
			if (Cv) { AsdaCVarSave::SetForTest(TEXT("asda.Trade.StoreFixK2"), 0); }
			Feed(Net, S, 6312, Make6312(kHostCharId, kBuyerCharId, { {20592, 0, 4}, {20571, 1, -1} }));
			if (Cv) { AsdaCVarSave::SetForTest(TEXT("asda.Trade.StoreFixK2"), 1); }
			Expect(S->GetCartCount() == 2, TEXT("К-2 = 0 (оригинал): из корзины ушла только позиция первой записи"), FString::Printf(TEXT("корзина=%d"), S->GetCartCount()));
			// Строка журнала длиннее 406 px — обрезка с «...».
			S->AddLogLine(FString(), FString::ChrN(200, TEXT('Ж')), FColor::White);
			const TArray<FString> Ls = S->DebugLogLines();
			Expect(Ls.Num() > 0 && Ls.Last().EndsWith(TEXT("...")) && Ls.Last().Len() < 200, TEXT("журнал 23402: строка шире 406 px обрезается «...»"),
				FString::Printf(TEXT("длина=%d хвост «%s»"), Ls.Num() ? Ls.Last().Len() : 0, Ls.Num() ? *Ls.Last().Right(5) : TEXT("")));
		}

		// ═══ 9. 6315: вход/выход гостя ═══
		{
			auto Make6315 = [](uint8 N, int32 Who) { TArray<uint8> B; B.SetNumZeroed(5); B[0] = N; AsdaBytes::WrI32(B.GetData() + 1, Who); return B; };
			// ★ StoreStrayGuard: витрина закрыта — 6315 молча (счётчик не меняется) ↔ с витриной (ниже) — строки журнала.
			S->ResetSessionState(EAsdaSessionResetReason::MapChanged);
			Feed(Net, S, 6315, Make6315(3, kBuyerCharId));
			Expect(S->GetGuestCount() == 0 && S->DebugLogLines().Num() == 0, TEXT("[ИСПРАВЛЕНИЕ] 6315 без открытой витрины — молча, счётчик гостей не меняется (StoreStrayGuard)"),
				FString::Printf(TEXT("гостей=%d строк=%d"), S->GetGuestCount(), S->DebugLogLines().Num()));
			OpenVisitor(Net, S, { {20592, 7, 1000} });
			Feed(Net, S, 6315, Make6315(1, kBuyerCharId));
			const bool bIn = LogHas(S, SubstPrintfL2(AsdaNotice::Loc(GI, 107087), TEXT("Покупатель"))) && S->GetGuestCount() == 1;
			Feed(Net, S, 6315, Make6315(0, kBuyerCharId));
			const bool bOut = LogHas(S, SubstPrintfL2(AsdaNotice::Loc(GI, 107088), TEXT("Покупатель"))) && S->GetGuestCount() == 0;
			const int32 N0 = S->DebugLogLines().Num();
			Feed(Net, S, 6315, Make6315(1, 777777));   // персонажа нет в мире — строки нет
			Expect(bIn && bOut && S->DebugLogLines().Num() == N0 && S->GetGuestCount() == 1,
				TEXT("6315: 0→1 «Входит Покупатель'» (107087) ↔ 1→0 «Покупатель' выходит» (107088); невидимый — без строки, счётчик обновлён"),
				FString::Printf(TEXT("вход=%d выход=%d строк %d→%d гостей=%d"), bIn ? 1 : 0, bOut ? 1 : 0, N0, S->DebugLogLines().Num(), S->GetGuestCount()));
		}

		// ═══ 10. 6311 (продавцу): вес/золото, журнал, остатки, К-8 ═══
		{
			S->ResetSessionState(EAsdaSessionResetReason::MapChanged);
			Inv->DebugPutItem(2, 0, 20592, 10);
			S->DebugSetHost(kSelfCharId, kSelfUserNum);
			TArray<uint8> R = Zero(452); R[0] = 1; R[1] = 1;
			uint8* L = R.GetData() + 2;
			AsdaBytes::WrI32(L, 20592); L[4] = 2; AsdaBytes::WrI16(L + 5, 0); AsdaBytes::WrI32(L + 7, 7); AsdaBytes::WrI16(L + 11, 0); AsdaBytes::WrI32(L + 13, 1000);
			Feed(Net, S, 6303, R);
			// ★ Ревизия Л2 (СРЕДН-1): запись — КАК ЕЁ ШЛЁТ СЕРВЕР (RecvDB `sub_506A00`, S:101438-101452): при остатке > 0 — только код,
			//   индекс и остаток, контейнер/слот от `sub_508F20` (+4 = 0, +5 = −1); при распродаже — настоящие контейнер 2 / слот 0.
			auto Make6311 = [](int32 Rem)
			{
				TArray<uint8> B; B.SetNumZeroed(281);
				AsdaBytes::WrI16(B.GetData(), 222); AsdaBytes::WrI32(B.GetData() + 2, 55555); AsdaBytes::WrI32(B.GetData() + 7, kBuyerCharId);
				for (int32 n = 0; n < 6; ++n) { uint8* P = B.GetData() + 11 + 45 * n; AsdaBytes::WrI16(P + 5, -1); AsdaBytes::WrI16(P + 11, -1); }
				uint8* P = B.GetData() + 11;
				AsdaBytes::WrI32(P, 20592); AsdaBytes::WrI32(P + 7, Rem); AsdaBytes::WrI16(P + 11, 0);
				if (Rem == -1) { P[4] = 2; AsdaBytes::WrI16(P + 5, 0); }   // распродажа: контейнер/слот есть
				else { P[4] = 0; AsdaBytes::WrI16(P + 5, -1); }            // частичная: +4 = 0, +5 = −1
				return B;
			};
			const bool bOwner = S->IsOwnerShowcaseOpen();
			Feed(Net, S, 6311, Make6311(4));
			const FAsdaItemInstance* It = Inv->GetCell(2, 0);
			const int32 A1 = It ? It->Amount : -1;
			const bool bLog = LogHas(S, TEXT("Покупатель")) && Inv->GetGold() == 55555 && Net->SelfWeightCur == 222 && S->DebugLotStock(0) == 4;
			Feed(Net, S, 6311, Make6311(-1));   // остальные 4 проданы — лот распродан, но в стопке сумки 10 − 3 − 4 = 3
			It = Inv->GetCell(2, 0);
			const int32 A2 = It ? It->Amount : -1;
			Expect(bOwner && bLog && A1 == 7 && A2 == 3 && S->DebugLotStock(0) == -1,
				TEXT("6311 (К-8): вес 222, золото 55 555, журнал 107082; стопка сумки 10 → 7 (продано 3) → 3 (распродан лот 7 из 10), остаток лота −1"),
				FString::Printf(TEXT("хозяин=%d журнал/золото/вес=%d стопка %d → %d остаток=%d"), bOwner ? 1 : 0, bLog ? 1 : 0, A1, A2, S->DebugLotStock(0)));
			// Контроль К-8 = 0 (оригинал): распродажа лота чистит слот сумки целиком.
			IConsoleVariable* Cv = IConsoleManager::Get().FindConsoleVariable(TEXT("asda.Trade.StoreFixK8"));
			S->ResetSessionState(EAsdaSessionResetReason::MapChanged);
			Inv->DebugPutItem(2, 0, 20592, 10);
			S->DebugSetHost(kSelfCharId, kSelfUserNum);
			Feed(Net, S, 6303, R);
			if (Cv) { AsdaCVarSave::SetForTest(TEXT("asda.Trade.StoreFixK8"), 0); }
			Feed(Net, S, 6311, Make6311(4));
			const FAsdaItemInstance* It3 = Inv->GetCell(2, 0);
			const int32 B1 = It3 ? It3->Amount : -1;
			Feed(Net, S, 6311, Make6311(-1));
			const FAsdaItemInstance* It4 = Inv->GetCell(2, 0);
			if (Cv) { AsdaCVarSave::SetForTest(TEXT("asda.Trade.StoreFixK8"), 1); }
			Expect(B1 == 10 && (!It4 || It4->ItemCode == 0), TEXT("К-8 = 0 (оригинал): частичная продажа (запись сервера без контейнера) — 10 не меняется, распродажа лота — слот пуст"),
				FString::Printf(TEXT("после 3: %d; после распродажи: код %d"), B1, It4 ? It4->ItemCode : 0));
			Inv->DebugPutItem(2, 0, 0, 0);
			S->ResetSessionState(EAsdaSessionResetReason::MapChanged);
			Inv->SetGoldDisplay(100000);
		}

		// ═══ 11. 6306 гостю, ESC, смерть гостя, сброс сессии ═══
		{
			OpenVisitor(Net, S, { {20592, 7, 1000} });
			WM->ClearMsgBoxQueue(TEXT("самотест")); Caps->Reset();
			WM->CloseTopmostOnEsc();
			TArray<uint8> P; const int32 NEsc = Count(6305, &P);
			Expect(NEsc == 1 && P.Num() == 6 && AsdaBytes::RdI32(P.GetData()) == kHostCharId && AsdaBytes::RdI16(P.GetData() + 4) == kHostUserNum && S->IsShowcaseOpen(),
				TEXT("ESC у витрины гостя → 6305 (charId/userNum ХОЗЯИНА лавки), окно ждёт 6306"),
				FString::Printf(TEXT("6305=%d длина=%d @0=%d @4=%d"), NEsc, P.Num(), P.Num() >= 4 ? AsdaBytes::RdI32(P.GetData()) : 0, P.Num() >= 6 ? AsdaBytes::RdI16(P.GetData() + 4) : 0));
			TArray<uint8> R = Zero(571); R[0] = 2;
			Feed(Net, S, 6306, R);
			Expect(!S->IsShowcaseOpen() && TopMsgIs(GI, WM, 107031) && !UAsdaTradeStateSubsystem::IsSelfStoreOpen(GI),
				TEXT("6306 код 2 (хозяин закрыл лавку) → 107031, витрина скрыта, признак снят"), S->DumpState());
			// ★ StoreStrayGuard (ревизия Л2, СРЕДН-2): витрины посетителя и подготовки нет — 6306/2 молча ↔ с витриной (выше) — 107031.
			WM->ClearMsgBoxQueue(TEXT("самотест"));
			Feed(Net, S, 6306, R);
			Expect(WM->GetMsgBoxQueueNum() == 0 && JournalHas(S, TEXT("StoreStrayGuard")),
				TEXT("[ИСПРАВЛЕНИЕ] 6306 код 2 без витрины посетителя и подготовки — молча (StoreStrayGuard)"),
				FString::Printf(TEXT("msgbox=%d"), WM->GetMsgBoxQueueNum()));
			WM->ClearMsgBoxQueue(TEXT("самотест"));
			OpenVisitor(Net, S, { {20592, 7, 1000} });
			R[0] = 1; Feed(Net, S, 6306, R);
			Expect(!S->IsShowcaseOpen() && WM->GetMsgBoxQueueNum() == 0, TEXT("6306 код 1 (гость вышел) → витрина скрыта молча, позы нет"), S->DumpState());
			// Смерть гостя: 6305 раз в 4 с ↔ жив — нет.
			OpenVisitor(Net, S, { {20592, 7, 1000} });
			Caps->Reset();
			S->Tick(0.f);
			const int32 NAlive = Count(6305);
			const bool bDeadWas = Net->bSelfDead;
			Net->bSelfDead = true;
			S->Tick(0.f);
			const int32 NDead = Count(6305);
			S->Tick(0.f);
			const int32 NDead2 = Count(6305);
			Net->bSelfDead = bDeadWas;
			Expect(NAlive == 0 && NDead == 1 && NDead2 == 1, TEXT("[ПОРТ] смерть гостя при открытой витрине → 6305 (раз в 4 с) ↔ жив — нет"),
				FString::Printf(TEXT("жив=%d мёртв=%d повтор=%d"), NAlive, NDead, NDead2));
			// Сброс сессии: витрина и корзина — без пакетов.
			S->OnLotClicked(0, true);
			Caps->Reset();
			S->ResetSessionState(EAsdaSessionResetReason::Disconnected);
			Expect(!S->IsShowcaseOpen() && S->GetCartCount() == 0 && S->IsBuyPending() && S->GetGuestCount() == 0 && S->DebugLogLines().Num() == 0 && Caps->Num() == 0,
				TEXT("сброс сессии (разрыв): витрина гостя скрыта, корзина/журнал/гости пусты, пакетов нет"),
				FString::Printf(TEXT("%s пакетов=%d"), *S->DumpState(), Caps->Num()));
		}

		// ═══ 12. Вне волны: 5451 0x16/0x17 — 5455 ТОЛЬКО при транспорте (15, 2), 0x16 чистит слот ═══
		{
			auto Feed5451 = [Net, Inv](uint8 Result, int32 Code, uint8 Cont, int16 Slot)
			{
				TArray<uint8> B = Zero(68); B[0] = Result; AsdaBytes::WrI32(B.GetData() + 3, Code); B[7] = Cont; AsdaBytes::WrI16(B.GetData() + 8, Slot);
				Net->DebugFeedPacket(5451, B);
				Inv->DebugPumpPendingPackets();
			};
			Eff->RemoveBySkillState(kSelfUserNum, 15, 2);
			Caps->Reset();
			Feed5451(0x17, 124, 1, 3);
			const int32 NNo = Count(5455);
			Eff->AddType2(kSelfUserNum, 15, 600, 123);
			Feed5451(0x17, 124, 1, 3);
			TArray<uint8> P; const int32 NYes = Count(5455, &P);
			const bool bRemoved = Eff->FindByTypeSub(kSelfUserNum, 15, 2) == nullptr;
			Expect(NNo == 0 && NYes == 1 && P.Num() >= 4 && AsdaBytes::RdI32(P.GetData()) == 123 && bRemoved,
				TEXT("5451 0x17: транспорта нет — 5455 не шлётся ↔ транспорт (15, 2) висит → 5455 кодом маунта 123, баф 15 снят"),
				FString::Printf(TEXT("без=%d с=%d код=%d снят=%d"), NNo, NYes, P.Num() >= 4 ? AsdaBytes::RdI32(P.GetData()) : 0, bRemoved ? 1 : 0));
			Inv->DebugPutItem(1, 3, 124, 1);
			Caps->Reset();
			Feed5451(0x16, 124, 1, 3);
			const FAsdaItemInstance* It = Inv->GetCell(1, 3);
			Expect((!It || It->ItemCode == 0) && Count(5455) == 0, TEXT("5451 0x16: слот 1:3 очищен (`OnRecv_RemoveSlot`), без транспорта 5455 нет"),
				FString::Printf(TEXT("ячейка=%d 5455=%d"), It ? It->ItemCode : 0, Count(5455)));
		}

		// ═══ 13. Раскладка тела 6309 без корзины (стенд) и пустые позиции ═══
		{
			const TArray<uint8> B = UAsdaNetSubsystem::BuildSelfStoreBuy6309Body(1, 2, 3, {}, {}, {}, {});
			bool bEmpty = B.Num() == 281 && B[10] == 0;
			for (int32 n = 0; n < 6 && bEmpty; ++n)
			{
				const uint8* L = B.GetData() + 11 + 45 * n;
				bEmpty &= AsdaBytes::RdI32(L) == 0 && AsdaBytes::RdI16(L + 5) == -1 && AsdaBytes::RdI16(L + 11) == -1 && AsdaBytes::RdI16(L + 17) == -1;
			}
			Expect(bEmpty, TEXT("6309 без позиций: 281 б, число 0, все 6 позиций — код 0, @5/@11/@17 = −1"), FString::Printf(TEXT("длина=%d"), B.Num()));
			// ★ Ревизия Л2 (МЕЛК-1): @25 записи — 5 пар {i16 −1, i16 0} (`slotStructReset` → `0x974480`), у пустых И у заполненных позиций
			//   (байты тела @36…@55 — первая позиция); то же у 6302 (@30…@49 — первый лот).
			const TArray<uint8> B1 = UAsdaNetSubsystem::BuildSelfStoreBuy6309Body(1, 2, 3, { 20592 }, { 4 }, { 0 }, { 1000 });
			const TArray<uint8> O1 = UAsdaNetSubsystem::BuildSelfStoreOpen6302Body(1, { 20592 }, { 2 }, { 0 }, { 4 }, { 1000 }, TEXT("Лавка"));
			bool bPairs = B1.Num() == 281 && O1.Num() == 505;
			FString Hex;
			for (int32 n = 0; n < 6 && bPairs; ++n)
			{
				for (int32 k = 0; k < 5; ++k) { const uint8* E = B1.GetData() + 11 + 45 * n + 25 + 4 * k; bPairs &= AsdaBytes::RdI16(E) == -1 && AsdaBytes::RdI16(E + 2) == 0; }
			}
			for (int32 n = 0; n < 10 && bPairs; ++n)
			{
				for (int32 k = 0; k < 5; ++k) { const uint8* E = O1.GetData() + 5 + 45 * n + 25 + 4 * k; bPairs &= AsdaBytes::RdI16(E) == -1 && AsdaBytes::RdI16(E + 2) == 0; }
			}
			for (int32 i = 36; i < 56 && B1.Num() >= 56; ++i) { Hex += FString::Printf(TEXT("%02X"), B1[i]); }
			const bool bKept = B1.Num() == 281 && AsdaBytes::RdI32(B1.GetData() + 11) == 20592 && AsdaBytes::RdI32(B1.GetData() + 11 + 7) == 4
				&& AsdaBytes::RdI16(B1.GetData() + 11 + 11) == 0 && AsdaBytes::RdI32(B1.GetData() + 11 + 13) == 1000;
			Expect(bPairs && bKept, TEXT("[ПРОТОКОЛ] 6309/6302: @25 каждой записи — 5 пар {−1, 0} (как `slotStructReset`), поля позиции целы"),
				FString::Printf(TEXT("6309 @36..@55 = %s"), *Hex));
		}
		WM->ClearMsgBoxQueue(TEXT("самотест лавки Л2"));
	}

	// «%s» по порядку (как `sprintf` эталона) — для ожиданий 107087/107088.
	static FString SubstPrintfL2(const FString& Fmt, const FString& A)
	{
		const int32 At = Fmt.Find(TEXT("%s"));
		return At == INDEX_NONE ? Fmt : Fmt.Left(At) + A + Fmt.Mid(At + 2);
	}

	// ═══ ФАЗА «ПУТЁМ ИГРОКА» — шаги с паузами, настоящие события Slate (`SimulateClickAt` по живой геометрии) ═══
	static bool CenterOf(UUserWidget* Root, const TCHAR* Name, FVector2D& Out)
	{
		UWidget* W = Root ? Root->GetWidgetFromName(FName(Name)) : nullptr;
		FVector2D Mn, Mx;
		if (!W || !AsdaUi::WidgetAbsRect(W, Mn, Mx)) { return false; }
		Out = (Mn + Mx) * 0.5f;
		return true;
	}
	// Клик по узлу окна витрины (bShop) или по окну из реестра (`WinId`).
	bool Click(UWorld* W, const TCHAR* Name, bool bRight = false, const TCHAR* WinId = nullptr)
	{
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaSelfStoreSubsystem* S = GI ? GI->GetSubsystem<UAsdaSelfStoreSubsystem>() : nullptr;
		UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
		UUserWidget* Root = (WM && WinId) ? WM->FindWindow(FName(WinId)) : (S ? S->DebugShopWidget() : nullptr);
		FVector2D C;
		if (!S || !WM || !CenterOf(Root, Name, C))
		{
			Expect(false, FString::Printf(TEXT("клик по %s"), Name), TEXT("нет живой геометрии"));
			return false;
		}
		WM->SimulateClickAt(C, /*HoldFrames*/ 3, bRight);
		return true;
	}
	// 10.10.2026: НАСТОЯЩИЙ двойной щелчок по узлу витрины — лот в корзину гостя кладётся только им (требование владельца).
	bool DblClick(UWorld* W, const TCHAR* Name, bool bRight = false)
	{
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaSelfStoreSubsystem* S = GI ? GI->GetSubsystem<UAsdaSelfStoreSubsystem>() : nullptr;
		UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
		FVector2D C;
		if (!S || !WM || !CenterOf(S->DebugShopWidget(), Name, C))
		{
			Expect(false, FString::Printf(TEXT("двойной щелчок по %s"), Name), TEXT("нет живой геометрии"));
			return false;
		}
		AsdaSimDoubleClickAt(WM, C, bRight);
		return true;
	}

	void BuildInputPhase()
	{
		using namespace AsdaStoreL2Local;
		auto Ctx = [](UWorld* W, UGameInstance*& GI, UAsdaSelfStoreSubsystem*& S, UAsdaNetSubsystem*& Net, UAsdaWindowSubsystem*& WM)
		{
			GI = W ? W->GetGameInstance() : nullptr;
			S = GI ? GI->GetSubsystem<UAsdaSelfStoreSubsystem>() : nullptr;
			Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
			WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
			return GI && S && Net && WM;
		};
		struct FHover { int32 Kind = -1; int32 Idx = -1; };   // подсказка, замеченная над лотом (шаг наведения)
		TSharedRef<FHover> Hover = MakeShared<FHover>();
		// 0. Вход: `RequestVisit` (как клик по вывеске) → 6307; витрина штатным 6308/1.
		Steps.Add([this, Ctx](UWorld* W)
		{
			UGameInstance* GI; UAsdaSelfStoreSubsystem* S; UAsdaNetSubsystem* Net; UAsdaWindowSubsystem* WM;
			if (!Ctx(W, GI, S, Net, WM)) { return; }
			Net->SelectedCharId = kSelfCharId; Net->SetSelfUserNumForDebug(kSelfUserNum); Net->SelfLevel = 30;
			S->ResetSessionState(EAsdaSessionResetReason::MapChanged);
			WM->ClearMsgBoxQueue(TEXT("самотест"));
			if (UAsdaInventorySubsystem* Inv = GI->GetSubsystem<UAsdaInventorySubsystem>()) { Inv->SetGoldDisplay(100000); }
			S->LastVisitReqAt = -10.0; Caps->Reset();
			const bool bV = S->RequestVisit(kHostUserNum, TEXT("путь игрока: клик по вывеске"));
			Expect(bV && Count(6307) == 1, TEXT("путь игрока: вход в лавку → 6307"), FString::Printf(TEXT("6307=%d"), Count(6307)));
			Net->DebugFeedPacket(6308, Make6308(1, TEXT("Лавка Гостя"), { {20592, 7, 1000}, {20571, 3, 50}, {20573, 2, 10} }));
			S->PumpPending();
		});
		// 0а–0в. ★ Ревизия 10.10.2026 (МЕЛК-1/МЕЛК-2): пару двойного щелчка по лоту рвут нажатие мимо лотов (ЛКМ и ПКМ) и
		//   перерисовка списка лотов (6312 между щелчками). Цепочка «лот → <разрыв> → лот → снимок → лот» идёт быстрее окна
		//   двойного; снимок — ничего не положено и нумпада нет; последний щелчок вместе с предыдущим — двойной (контроль:
		//   ЛКМ → нумпад 8, ПКМ → весь остаток). После каждого случая витрина открывается заново тем же 6308 — дальнейшие шаги
		//   идут с состояния шага 0.
		auto ReopenVisitor = [](UAsdaSelfStoreSubsystem* S, UAsdaNetSubsystem* Net, UAsdaWindowSubsystem* WM)
		{
			if (WM->IsNumPadOpen()) { WM->CloseNumPad(false); }
			S->ResetSessionState(EAsdaSessionResetReason::MapChanged);
			Net->DebugFeedPacket(6308, Make6308(1, TEXT("Лавка Гостя"), { {20592, 7, 1000}, {20571, 3, 50}, {20573, 2, 10} }));
			S->PumpPending();
		};
		// Запуск цепочки по лоту `Lot`: разрыв — щелчок в промежутке между лотами 0 и 1 (bMiss) либо действие Break.
		auto StartChain = [this, Ctx](UWorld* W, const TCHAR* Lot, bool bRight, bool bMiss, TFunction<void()> Break)
		{
			UGameInstance* GI; UAsdaSelfStoreSubsystem* S; UAsdaNetSubsystem* Net; UAsdaWindowSubsystem* WM;
			if (!Ctx(W, GI, S, Net, WM)) { return; }
			SnapCart = -1; bSnapNum = false; Chain.Reset();
			UUserWidget* Root = S->DebugShopWidget();
			FVector2D P = FVector2D::ZeroVector, Miss = FVector2D::ZeroVector;   // ★ R5-fix-c (13.11.2026): начальные значения — C4701 (при отказе геометрии числа уходили в строку проверки неинициализированными)
			const bool bGeo = CenterOf(Root, Lot, P) && (!bMiss || AsdaSimGapPoint(Root ? Root->GetWidgetFromName(TEXT("Stall_SellIcon00_28_0")) : nullptr,
				Root ? Root->GetWidgetFromName(TEXT("Stall_SellIcon01_34_0")) : nullptr, Miss));
			if (!bGeo) { Expect(false, TEXT("путь игрока: цепочка щелчков по лоту"), TEXT("нет живой геометрии лота/промежутка")); return; }
			FAsdaSimChainLink Item; Item.Pos = P; Item.bRight = bRight;
			FAsdaSimChainLink Brk; Brk.bRight = bRight;
			if (bMiss) { Brk.Pos = Miss; } else { Brk.Action = Break; }
			TWeakObjectPtr<UAsdaSelfStoreSubsystem> WS(S);
			TWeakObjectPtr<UAsdaWindowSubsystem> WW(WM);
			FAsdaSimChainLink Snap; Snap.Action = [this, WS, WW]()
			{
				SnapCart = WS.IsValid() ? WS->GetCartCount() : -1;
				bSnapNum = WW.IsValid() && WW->IsNumPadOpen();
			};
			Chain = MakeShared<FAsdaSimChainResult>();
			AsdaSimClickChain(WM, { Item, Brk, Item, Snap, Item }, Chain);
		};
		// Проверка случая: снимок «пусто», затем контроль; bRight — ПКМ (ждём позицию ×3), иначе ЛКМ (ждём нумпад 8).
		auto CheckChain = [this, Ctx, ReopenVisitor](UWorld* W, const TCHAR* Name, bool bRight)
		{
			UGameInstance* GI; UAsdaSelfStoreSubsystem* S; UAsdaNetSubsystem* Net; UAsdaWindowSubsystem* WM;
			if (!Ctx(W, GI, S, Net, WM)) { return; }
			if (!Chain.IsValid()) { return; }   // запуск уже засчитан провалом
			const TArray<double>& T = Chain->ClickDoneSec;
			const double Span = (T.Num() >= 3) ? T[T.Num() - 2] : -1.0;   // щелчок перед снимком — предпоследний
			const double Win = FAsdaShopDblClick::WindowSec();
			// ★ R5-fix-b 13.11.2026 (заключительный заход, третья запись), ревизия REVIEW6 С-1: повтор — только при чистом снимке (корзина 0, нумпада нет): иначе дефект при любой скорости.
			if (Chain->bDone && !(Span >= 0.0 && Span <= Win) && SnapCart == 0 && !bSnapNum && ChainTries < kChainTries - 1)   // ★ R5-fix-b 13.11.2026 (заключительный заход): опыт не состоялся — случай заново
			{
				++ChainTries;
				UE_LOG(LogAsdaStoreTestL2, Log, TEXT("САМОТЕСТ-ЛАВКА-Л2 повтор %d/%d: %s — щелчок перед снимком через %.3f с при окне %.3f с (кадры медленные), случай заново"),
					ChainTries, kChainTries - 1, Name, Span, Win);
				Chain.Reset();
				ReopenVisitor(S, Net, WM);
				Rewind = 2;
				HoldUntil = FPlatformTime::Seconds() + Win + 0.35;
				return;
			}
			ChainTries = 0;
			const bool bCtl = bRight ? (S->GetCartCount() == 1 && !WM->IsNumPadOpen()) : (WM->IsNumPadOpen() && S->GetCartCount() == 0);
			Expect(Chain->bDone && Span >= 0.0 && Span <= Win && SnapCart == 0 && !bSnapNum && bCtl, Name,
				FString::Printf(TEXT("до последнего щелчка: корзина=%d нумпад=%d (ждём 0/0); после: корзина=%d нумпад=%d (ждём %s); щелчок перед снимком через %.3f с при окне %.3f с, щелчков %d"),
					SnapCart, bSnapNum ? 1 : 0, S->GetCartCount(), WM->IsNumPadOpen() ? 1 : 0, bRight ? TEXT("1/0") : TEXT("0/1"), Span, Win, T.Num()));
			Chain.Reset();
			ReopenVisitor(S, Net, WM);
		};
		// 0а. ЛКМ: «лот 0 → мимо → лот 0».
		Steps.Add([StartChain](UWorld* W) { StartChain(W, TEXT("Stall_SellIcon00_28_0"), false, true, nullptr); });
		Steps.Add([CheckChain](UWorld* W) { CheckChain(W, TEXT("путь игрока: «ЛКМ по лоту → ЛКМ мимо лотов → ЛКМ по лоту» быстрее окна двойного — НЕ кладёт, нумпада нет (МЕЛК-1); следующий ЛКМ — двойной → нумпад 8 (контроль)"), false); });
		Steps.Add([](UWorld*) {});   // пауза окна двойного щелчка
		// 0б. ПКМ: «лот 1 → ПКМ мимо → лот 1» — прежде у ПКМ сброса не было и весь остаток уходил в корзину.
		Steps.Add([StartChain](UWorld* W) { StartChain(W, TEXT("Stall_SellIcon01_34_0"), true, true, nullptr); });
		Steps.Add([CheckChain](UWorld* W) { CheckChain(W, TEXT("путь игрока: «ПКМ по лоту → ПКМ мимо лотов → ПКМ по лоту» — НЕ кладёт (МЕЛК-1); следующий ПКМ — двойной → весь остаток (контроль)"), true); });
		Steps.Add([](UWorld*) {});
		// 0в. Список лотов перерисован между щелчками: 6312 «витрина изменилась» (лот 0 продан до 6 шт.) — пара рвётся.
		Steps.Add([this, Ctx, StartChain](UWorld* W)
		{
			UGameInstance* GI; UAsdaSelfStoreSubsystem* S; UAsdaNetSubsystem* Net; UAsdaWindowSubsystem* WM;
			if (!Ctx(W, GI, S, Net, WM)) { return; }
			TWeakObjectPtr<UAsdaSelfStoreSubsystem> WS(S);
			TWeakObjectPtr<UAsdaNetSubsystem> WN(Net);
			StartChain(W, TEXT("Stall_SellIcon01_34_0"), true, false, [WS, WN]()
			{
				if (!WS.IsValid() || !WN.IsValid()) { return; }
				TArray<uint8> B; B.SetNumZeroed(281);   // тело 6312: @0 хозяин, @7 покупатель, 6 записей по 45 б с @11
				AsdaBytes::WrI32(B.GetData(), kHostCharId);
				AsdaBytes::WrI32(B.GetData() + 7, kBuyerCharId);
				for (int32 n = 0; n < 6; ++n) { uint8* P = B.GetData() + 11 + 45 * n; AsdaBytes::WrI16(P + 5, -1); AsdaBytes::WrI16(P + 11, -1); }
				AsdaBytes::WrI32(B.GetData() + 11, 20592); AsdaBytes::WrI32(B.GetData() + 11 + 7, 6); AsdaBytes::WrI16(B.GetData() + 11 + 11, 0);
				WN->DebugFeedPacket(6312, B);
				WS->PumpPending();
			});
		});
		Steps.Add([CheckChain](UWorld* W) { CheckChain(W, TEXT("путь игрока: «ПКМ по лоту → 6312 перерисовал витрину → ПКМ по лоту» — НЕ кладёт (МЕЛК-2); следующий ПКМ — двойной → весь остаток (контроль)"), true); });
		Steps.Add([](UWorld*) {});
		// 1. ★ 10.10.2026: ОДИНОЧНЫЙ ЛКМ по лоту 0 — в корзину не кладёт и нумпад не открывает (требование владельца, [ПОРТ]).
		Steps.Add([this](UWorld* W) { Click(W, TEXT("Stall_SellIcon00_28_0")); });
		Steps.Add([this, Ctx](UWorld* W)
		{
			UGameInstance* GI; UAsdaSelfStoreSubsystem* S; UAsdaNetSubsystem* Net; UAsdaWindowSubsystem* WM;
			if (!Ctx(W, GI, S, Net, WM)) { return; }
			Expect(!WM->IsNumPadOpen() && S->GetCartCount() == 0,
				TEXT("путь игрока: ОДИНОЧНЫЙ ЛКМ по лоту — ничего не кладёт, нумпада нет (в корзину — двойным)"),
				FString::Printf(TEXT("нумпад=%d корзина=%d"), WM->IsNumPadOpen() ? 1 : 0, S->GetCartCount()));
		});
		Steps.Add([](UWorld*) {});   // пауза: следующий щелчок по тому же лоту уже не «второй» (окно двойного 0,5 с)
		// 1б. ДВОЙНОЙ ЛКМ по лоту 0 (стопка 7) → нумпад 8.
		Steps.Add([this](UWorld* W) { DblClick(W, TEXT("Stall_SellIcon00_28_0")); });
		Steps.Add([this, Ctx](UWorld* W)
		{
			UGameInstance* GI; UAsdaSelfStoreSubsystem* S; UAsdaNetSubsystem* Net; UAsdaWindowSubsystem* WM;
			if (!Ctx(W, GI, S, Net, WM)) { return; }
			Expect(WM->IsNumPadOpen() && S->GetCartCount() == 0,
				TEXT("путь игрока: ДВОЙНОЙ ЛКМ по лоту → нумпад режим 8, корзина пуста"),
				FString::Printf(TEXT("нумпад=%d корзина=%d"), WM->IsNumPadOpen() ? 1 : 0, S->GetCartCount()));
			UUserWidget* NW = WM->FindWindow(FName(TEXT("NUMPAD_WIN")));
			if (UEditableText* E = NW ? Cast<UEditableText>(NW->GetWidgetFromName(TEXT("NUMPAD_NUMBOX"))) : nullptr) { E->SetText(FText::FromString(TEXT("4"))); }
		});
		// 2. Клик по «OK» нумпада → позиция ×4.
		Steps.Add([this](UWorld* W) { Click(W, TEXT("NUMPAD_OK"), false, TEXT("NUMPAD_WIN")); });
		Steps.Add([this, Ctx](UWorld* W)
		{
			UGameInstance* GI; UAsdaSelfStoreSubsystem* S; UAsdaNetSubsystem* Net; UAsdaWindowSubsystem* WM;
			if (!Ctx(W, GI, S, Net, WM)) { return; }
			Expect(!WM->IsNumPadOpen() && S->GetCartCount() == 1 && S->DebugCartQty(0) == 4, TEXT("путь игрока: ввод 4 + клик «OK» нумпада → позиция корзины ×4"),
				FString::Printf(TEXT("нумпад=%d корзина=%d кол-во=%d"), WM->IsNumPadOpen() ? 1 : 0, S->GetCartCount(), S->DebugCartQty(0)));
		});
		// 3. ПКМ по лоту 1 → весь остаток (латч ПКМ, событие 0xE). ★ 10.10.2026: одиночный ПКМ не кладёт, кладёт двойной.
		Steps.Add([this](UWorld* W) { Click(W, TEXT("Stall_SellIcon01_34_0"), /*bRight*/ true); });
		Steps.Add([this, Ctx](UWorld* W)
		{
			UGameInstance* GI; UAsdaSelfStoreSubsystem* S; UAsdaNetSubsystem* Net; UAsdaWindowSubsystem* WM;
			if (!Ctx(W, GI, S, Net, WM)) { return; }
			Expect(S->GetCartCount() == 1, TEXT("путь игрока: ОДИНОЧНЫЙ ПКМ по лоту — ничего не кладёт"),
				FString::Printf(TEXT("корзина=%d"), S->GetCartCount()));
		});
		Steps.Add([](UWorld*) {});   // пауза окна двойного щелчка
		Steps.Add([this](UWorld* W) { DblClick(W, TEXT("Stall_SellIcon01_34_0"), /*bRight*/ true); });
		Steps.Add([this, Ctx](UWorld* W)
		{
			UGameInstance* GI; UAsdaSelfStoreSubsystem* S; UAsdaNetSubsystem* Net; UAsdaWindowSubsystem* WM;
			if (!Ctx(W, GI, S, Net, WM)) { return; }
			Expect(!WM->IsNumPadOpen() && S->GetCartCount() == 2 && S->DebugCartQty(1) == 3, TEXT("путь игрока: ДВОЙНОЙ ПКМ по лоту (контроль) → весь остаток 3 без нумпада"),
				FString::Printf(TEXT("нумпад=%d корзина=%d кол-во=%d"), WM->IsNumPadOpen() ? 1 : 0, S->GetCartCount(), S->DebugCartQty(1)));
		});
		// 4. Клик по ячейке корзины 23250 → позиция снята (выбор + `shopDlgOnCartRemoveButton`).
		Steps.Add([this](UWorld* W) { Click(W, TEXT("Stall_PurItem00_103_0")); });
		Steps.Add([this, Ctx](UWorld* W)
		{
			UGameInstance* GI; UAsdaSelfStoreSubsystem* S; UAsdaNetSubsystem* Net; UAsdaWindowSubsystem* WM;
			if (!Ctx(W, GI, S, Net, WM)) { return; }
			Expect(S->GetCartCount() == 1 && S->DebugCartCode(0) == 20571, TEXT("путь игрока: клик по ячейке корзины 23250 → позиция 20592 снята, осталась 20571"),
				FString::Printf(TEXT("корзина=%d ячейка0=%d"), S->GetCartCount(), S->DebugCartCode(0)));
		});
		// 5. ПКМ по лоту 0 снова → выбранный вернулся (рамка), клик по урне → снят. ★ 10.10.2026: двойным ПКМ.
		Steps.Add([this](UWorld* W) { DblClick(W, TEXT("Stall_SellIcon00_28_0"), /*bRight*/ true); });
		Steps.Add([this, Ctx](UWorld* W)
		{
			UGameInstance* GI; UAsdaSelfStoreSubsystem* S; UAsdaNetSubsystem* Net; UAsdaWindowSubsystem* WM;
			if (!Ctx(W, GI, S, Net, WM)) { return; }
			Expect(S->GetCartCount() == 2 && S->DebugCartSelShown(), TEXT("путь игрока: выбранный лот снова в корзине → рамка 23241 на его ячейке"),
				FString::Printf(TEXT("корзина=%d рамка=%d"), S->GetCartCount(), S->DebugCartSelShown() ? 1 : 0));
			Click(W, TEXT("Stall_Pur_TrashBtn"));
		});
		Steps.Add([this, Ctx](UWorld* W)
		{
			UGameInstance* GI; UAsdaSelfStoreSubsystem* S; UAsdaNetSubsystem* Net; UAsdaWindowSubsystem* WM;
			if (!Ctx(W, GI, S, Net, WM)) { return; }
			Expect(S->GetCartCount() == 1 && !S->DebugCartSelShown(), TEXT("путь игрока: клик по урне 23243 → выбранная позиция снята"),
				FString::Printf(TEXT("корзина=%d рамка=%d"), S->GetCartCount(), S->DebugCartSelShown() ? 1 : 0));
			// Наведение (событие 8): курсор над лотом 2 без клика.
			FVector2D C;
			if (CenterOf(S->DebugShopWidget(), TEXT("Stall_SellIcon02_40_0"), C)) { FSlateApplication::Get().SetCursorPos(C); }
		});
		Steps.Add([this, Ctx, Hover](UWorld* W)
		{
			UGameInstance* GI; UAsdaSelfStoreSubsystem* S; UAsdaNetSubsystem* Net; UAsdaWindowSubsystem* WM;
			if (!Ctx(W, GI, S, Net, WM)) { return; }
			Hover->Kind = S->TipKind; Hover->Idx = S->TipIdx;
			FVector2D C;
			if (CenterOf(S->DebugShopWidget(), TEXT("Stall_Pur_TrashBtn"), C)) { FSlateApplication::Get().SetCursorPos(C); }
		});
		Steps.Add([this, Ctx, Hover](UWorld* W)
		{
			UGameInstance* GI; UAsdaSelfStoreSubsystem* S; UAsdaNetSubsystem* Net; UAsdaWindowSubsystem* WM;
			if (!Ctx(W, GI, S, Net, WM)) { return; }
			Expect(Hover->Kind == 2 && Hover->Idx == 2 && S->TipKind == -1,
				TEXT("путь игрока: наведение на лот 2 → подсказка лота (8) ↔ курсор над урной → подсказка скрыта (0xA)"),
				FString::Printf(TEXT("над лотом: вид %d индекс %d; над урной: вид %d"), Hover->Kind, Hover->Idx, S->TipKind));
			Caps->Reset();
			Click(W, TEXT("Stall_EditBtn"));
		});
		// 6. 23305 → msgbox 62; клик «OK» (TWOBUT_BUT00) → 6309.
		Steps.Add([this, Ctx](UWorld* W)
		{
			UGameInstance* GI; UAsdaSelfStoreSubsystem* S; UAsdaNetSubsystem* Net; UAsdaWindowSubsystem* WM;
			if (!Ctx(W, GI, S, Net, WM)) { return; }
			Expect(WM->IsMsgBoxTop(1, 62) && WM->IsOpen(FName(TEXT("TWOBUT_MSGBOX"))), TEXT("путь игрока: клик 23305 «OK» → msgbox 62 открыт"),
				FString::Printf(TEXT("верх «%s»"), *WM->GetMsgBoxQueueTopText()));
			Click(W, TEXT("TWOBUT_BUT00"), false, TEXT("TWOBUT_MSGBOX"));
		});
		Steps.Add([this, Ctx](UWorld* W)
		{
			UGameInstance* GI; UAsdaSelfStoreSubsystem* S; UAsdaNetSubsystem* Net; UAsdaWindowSubsystem* WM;
			if (!Ctx(W, GI, S, Net, WM)) { return; }
			TArray<uint8> P; const int32 N = Count(6309, &P);
			Expect(N == 1 && P.Num() == 281 && P[10] == 1 && AsdaBytes::RdI32(P.GetData() + 11) == 20571 && !S->IsBuyPending(),
				TEXT("путь игрока: клик «OK» msgbox 62 → 6309 (1 позиция 20571)"), FString::Printf(TEXT("6309=%d"), N));
			Click(W, TEXT("Stall_EditBtn"));
		});
		// 7. Повторный 23305 до 6310 → 107081; клик «ОК» ONEBUT — окно закрыто.
		Steps.Add([this, Ctx](UWorld* W)
		{
			UGameInstance* GI; UAsdaSelfStoreSubsystem* S; UAsdaNetSubsystem* Net; UAsdaWindowSubsystem* WM;
			if (!Ctx(W, GI, S, Net, WM)) { return; }
			Expect(TopMsgIs(GI, WM, 107081) && Count(6309) == 1, TEXT("путь игрока: 23305 до ответа 6310 → msgbox 107081, пакета нет"),
				FString::Printf(TEXT("верх «%s» 6309=%d"), *WM->GetMsgBoxQueueTopText(), Count(6309)));
			Click(W, TEXT("ONEBUT_BUT00"), false, TEXT("ONEBUT_MSGBOX"));
		});
		// 8. «в конец» журнала 23404 и «Отмена» 23306 → 6305.
		Steps.Add([this, Ctx](UWorld* W)
		{
			UGameInstance* GI; UAsdaSelfStoreSubsystem* S; UAsdaNetSubsystem* Net; UAsdaWindowSubsystem* WM;
			if (!Ctx(W, GI, S, Net, WM)) { return; }
			Expect(WM->GetMsgBoxQueueNum() == 0, TEXT("путь игрока: клик «ОК» msgbox → окно закрыто"), FString::Printf(TEXT("очередь=%d"), WM->GetMsgBoxQueueNum()));
			S->ClearJournal();
			Click(W, TEXT("Stall_Chat_HomeBtn"));
		});
		Steps.Add([this, Ctx](UWorld* W)
		{
			UGameInstance* GI; UAsdaSelfStoreSubsystem* S; UAsdaNetSubsystem* Net; UAsdaWindowSubsystem* WM;
			if (!Ctx(W, GI, S, Net, WM)) { return; }
			Expect(JournalHas(S, TEXT("23404")), TEXT("путь игрока: клик 23404 → журнал в конец"), TEXT(""));
			Caps->Reset();
			Click(W, TEXT("Stall_ClsBtn1"));
		});
		Steps.Add([this, Ctx](UWorld* W)
		{
			UGameInstance* GI; UAsdaSelfStoreSubsystem* S; UAsdaNetSubsystem* Net; UAsdaWindowSubsystem* WM;
			if (!Ctx(W, GI, S, Net, WM)) { return; }
			TArray<uint8> P; const int32 N = Count(6305, &P);
			Expect(N == 1 && P.Num() == 6 && AsdaBytes::RdI32(P.GetData()) == kHostCharId && S->IsVisitorShowcaseOpen(),
				TEXT("путь игрока: клик «Отмена» 23306 → 6305 (хозяин 900055), витрина ждёт 6306"), FString::Printf(TEXT("6305=%d"), N));
		});
	}

	// Снять свой перехватчик (доступ к приватному `DisarmSendCapture` — через `friend struct FAsdaSelfStoreL2SelfTest`).
	void Disarm(UAsdaNetSubsystem* Net) { if (Net) { AsdaSelfTestKit::DisarmCapture(Net, Gen); } }

	// Геометрия путём игрока (кадр уже отрисован): узлы посетителя относительно левого-верхнего плана рамки против `.JUI`.
	void CheckGeometry(UWorld* W)
	{
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaSelfStoreSubsystem* S = GI ? GI->GetSubsystem<UAsdaSelfStoreSubsystem>() : nullptr;
		UUserWidget* Root = S ? S->DebugShopWidget() : nullptr;
		if (!Root || !Root->WidgetTree) { Expect(false, TEXT("геометрия 23000 (посетитель)"), TEXT("окна нет")); return; }
		TMap<FString, UWidget*> ByName;
		TArray<UWidget*> All; Root->WidgetTree->GetAllWidgets(All);
		for (UWidget* X : All) { if (X) { ByName.Add(X->GetName(), X); } }
		UWidget* const* F0 = ByName.Find(TEXT("StreetStall_Win_0_0"));
		UWidget* const* F2 = ByName.Find(TEXT("StreetStall_Win_0_2"));
		if (!F0 || !F2) { Expect(false, TEXT("геометрия 23000 (посетитель)"), TEXT("планов рамки нет")); return; }
		const FVector2D O = (*F0)->GetCachedGeometry().GetAbsolutePosition();
		const FGeometry G2 = (*F2)->GetCachedGeometry();
		const float Scale = (G2.GetAbsolutePosition().X + G2.GetAbsoluteSize().X - O.X) / 438.f;
		struct FPt { const TCHAR* Name; float X, Y, Wd, Ht; };
		// `.JUI` (w0/jui_tree.txt): 23121 (14,108) 32×32; 23250 (13,306) 32×32; 23243 (231,320) 22×22; 23404 (418,452) 13×13;
		//   23402 (19,452) 412×71 — список строится кодом по этому прямоугольнику.
		static const FPt kPts[] = {
			{ TEXT("Stall_SellIcon00_28_0"), 14, 108, 32, 32 }, { TEXT("Stall_PurItem00_103_0"), 13, 306, 32, 32 },
			{ TEXT("Stall_Pur_TrashBtn"), 231, 320, 22, 22 }, { TEXT("Stall_Chat_HomeBtn"), 418, 452, 13, 13 }, { TEXT("Stall_Chat_History"), 19, 452, 412, 71 } };
		float MaxD = 0.f; FString Det;
		for (const FPt& P : kPts)
		{
			UWidget* const* X = ByName.Find(P.Name);
			if (!X) { MaxD = 999.f; Det += FString::Printf(TEXT("%s: нет; "), P.Name); continue; }
			const FGeometry G = (*X)->GetCachedGeometry();
			const FVector2D Rel = (G.GetAbsolutePosition() - O) / FMath::Max(Scale, 0.01f);
			const FVector2D Sz = G.GetAbsoluteSize() / FMath::Max(Scale, 0.01f);
			const float D = FMath::Max(FMath::Max(FMath::Abs(Rel.X - P.X), FMath::Abs(Rel.Y - P.Y)), FMath::Max(FMath::Abs(Sz.X - P.Wd), FMath::Abs(Sz.Y - P.Ht)));
			MaxD = FMath::Max(MaxD, D);
			Det += FString::Printf(TEXT("%s (%.1f,%.1f) %.1fx%.1f [JUI %.0f,%.0f %.0fx%.0f]; "), P.Name, Rel.X, Rel.Y, Sz.X, Sz.Y, P.X, P.Y, P.Wd, P.Ht);
		}
		Expect(Scale > 0.1f && MaxD <= 1.5f, FString::Printf(TEXT("геометрия 23000 посетителя путём игрока (масштаб %.3f, Δ ≤ 1,5 px)"), Scale), Det);
	}
};

namespace
{
	// Разбор `delay=N`.
	// ★ R5-fix-c (13.11.2026): своя `ParseStoreL2Delay` (дословная копия у двенадцати самотестов) убрана — разбор `delay=N` в общей точке
	//   `AsdaSelfTestKit::DelayArg` (`Test/AsdaSelfTestKit.h`), вызовы ниже идут в неё.

	FAutoConsoleCommandWithWorldAndArgs GAsdaTradeSelfTestStoreL2(
		TEXT("asda.Trade.SelfTestStoreL2"),
		TEXT("asda.Trade.SelfTestStoreL2 [delay=N] [quit] — самотест посетителя личной лавки (волна Л2): вход 6307, приём 6308/6310/6311/6312/6315/6306 "
		     "штатным путём, корзина (нумпад 8, 107086, 107056, урна), msgbox 62 → 6309, StoreBuyGuard, К-2/К-8/К-10, журнал 23402, ESC, смерть, "
		     "сброс сессии, 5451 0x16/0x17, фаза путём игрока (клики по живой геометрии) и геометрия. Только офлайн."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
		{
			const FAsdaSelfTestArgs KitArgs = FAsdaSelfTestArgs::Parse(Args);   // ★ 11.10.2026 (В3 S1): каркас — отказ с ИТОГ и quit, второй барьер на все опкоды
			if (AsdaSelfTestKit::RefuseStart(W, TEXT("asda.Trade.SelfTestStoreL2"), KitArgs)) { return; }
			const float Delay = AsdaSelfTestKit::DelayArg(Args);
			const bool bQuit = Args.Contains(TEXT("quit"));
			TWeakObjectPtr<UWorld> WW(W);
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WW, bQuit](float) -> bool
			{
				UWorld* Wl = WW.Get();
				if (!Wl) { return false; }
				if (AsdaSelfTestKit::RefuseDeferred(Wl, TEXT("asda.Trade.SelfTestStoreL2"), bQuit)) { return false; }   // Р-2: офлайн повторно — на отложенном старте
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
					TSharedRef<FAsdaSelfStoreL2SelfTest> T = MakeShared<FAsdaSelfStoreL2SelfTest>();
					T->Run(W2);
					T->BuildInputPhase();
					// Шаги фазы «путём игрока» — по одному раз в 0,4 с (клику нужно ≥ 3 кадров удержания + кадр опроса окна).
					TSharedRef<int32> Idx = MakeShared<int32>(0);
					// ★ R5-fix8-b 14.11.2026 (третья запись, добавка координатора: ClickThrough): шаг — не раньше 0,4 с после прежнего И после доставки симввода + 3 кадра (прежде — по часам раз в 0,4 с:
					//   при 15 к/с проверки урны и msgbox 62 шли до обработки щелчка). Тикер — каждый кадр.
					TSharedRef<double> NextAt = MakeShared<double>(FPlatformTime::Seconds() + 0.4);   // первый шаг — как прежде, через 0,4 с
					TSharedRef<int32> Quiet = MakeShared<int32>(3);
					TSharedRef<double> InputSince = MakeShared<double>(0.0);   // ★ R5-fix8-b 14.11.2026 (пятая запись, ревизия REVIEW8b) (М-2): с какого момента ждём доставки
					FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WW, T, Idx, NextAt, Quiet, InputSince, bQuit](float) -> bool
					{
						UWorld* W3 = WW.Get();
						if (!W3) { AsdaCVarSave::RestoreAll(TEXT("asda.Trade.SelfTestStoreL2")); return false; }   // ★ ИСПРАВЛЕНИЕ 11.10.2026 (В0, N1(д)/U8): ключи — и при уходе мира
						// ★ R5-fix8-b 14.11.2026 (пятая запись, ревизия REVIEW8b) (М-1): доставка симввода — ДО цепочки щелчков; цепочка тоже сбрасывает тихие кадры.
						{
							const UGameInstance* GIq = W3->GetGameInstance();
							const UAsdaWindowSubsystem* WMq = GIq ? GIq->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
							const double NowQ = FPlatformTime::Seconds();
							if (WMq && WMq->IsSimClickPending())
							{
								if (*InputSince <= 0.0) { *InputSince = NowQ; }
								if (NowQ - *InputSince < 20.0) { *Quiet = 0; return true; }   // ★ R5-fix8-b 14.11.2026 (пятая запись, ревизия REVIEW8b) (М-2): потолок 20 с — дальше проверки покажут
							}
							else { *InputSince = 0.0; }
						}
						if (T->Chain.IsValid() && !T->Chain->bDone) { *Quiet = 0; return true; }   // ★ 10.10.2026: идёт цепочка щелчков — ждём её конца
						if (FPlatformTime::Seconds() < T->HoldUntil) { return true; }   // ★ R5-fix-b 13.11.2026 (заключительный заход): пауза перед повтором случая
						if (*Quiet < 3) { ++*Quiet; return true; }   // ★ R5-fix8-b 14.11.2026 (третья запись, добавка координатора: ClickThrough): 3 кадра после ввода (и после цепочки — М-1)
						if (FPlatformTime::Seconds() < *NextAt) { return true; }
						if (*Idx < T->Steps.Num())
						{
							*NextAt = FPlatformTime::Seconds() + 0.4;   // ★ R5-fix8-b 14.11.2026 (третья запись, добавка координатора: ClickThrough): прежний шаг тикера 0,4 с
							T->Steps[(*Idx)++](W3);
							if (T->Rewind > 0) { *Idx -= T->Rewind; T->Rewind = 0; }   // ★ R5-fix-b 13.11.2026 (заключительный заход): повтор случая-цепочки
							return true;
						}
						T->CheckGeometry(W3);
						UGameInstance* GI3 = W3->GetGameInstance();
						if (UAsdaNetSubsystem* Net3 = GI3 ? GI3->GetSubsystem<UAsdaNetSubsystem>() : nullptr) { T->Disarm(Net3); }
						if (UAsdaSelfStoreSubsystem* S3 = GI3 ? GI3->GetSubsystem<UAsdaSelfStoreSubsystem>() : nullptr)
						{
							S3->ResetSessionState(EAsdaSessionResetReason::MapChanged);
						}
						if (UAsdaWindowSubsystem* WM3 = GI3 ? GI3->GetSubsystem<UAsdaWindowSubsystem>() : nullptr) { WM3->ClearMsgBoxQueue(TEXT("самотест Л2: итог")); }
						// ★ Ревизия О4 (МЕЛК-4): деньги и ячейка 20551 — в исходное.
						if (UAsdaInventorySubsystem* Inv3 = GI3 ? GI3->GetSubsystem<UAsdaInventorySubsystem>() : nullptr)
						{
							Inv3->SetGoldDisplay(T->GoldWas);
							for (int32 i = 0; i < 60 && T->GoldCellWas >= 0; ++i)
							{
								FAsdaItemInstance* G = Inv3->GetCellMutable(2, i);
								if (G && G->ItemCode == 20551) { G->SetAmount(T->GoldCellWas); break; }
							}
						}
						// ★ ИСПРАВЛЕНИЕ 11.10.2026 (В0, N1(д)/U8): ключи прогона (Trade.StoreBuyGuard, StoreFixK2, StoreFixK8) — к исходному значению
						//   И приоритету SetBy (прежде блоки возвращали константу 1 с SetByConsole).
						AsdaCVarSave::RestoreAll(TEXT("asda.Trade.SelfTestStoreL2"));
						UE_LOG(LogAsdaStoreTestL2, Log, TEXT("САМОТЕСТ-ЛАВКА-Л2 ИТОГ: ok=%d fail=%d"), T->Ok, T->Fail);
						T->SelfScope.Restore();   // ★ В3 S1 (N1(г)): «свой» — как до прогона
						AsdaSelfTestKit::Quit(W3, bQuit);
						return false;
					}), 0.0f);   // ★ R5-fix8-b 14.11.2026 (третья запись, добавка координатора: ClickThrough): каждый кадр (шаг 0,4 с — через NextAt)
					return false;
				}), bNeedSpawn ? 2.f : 0.f);
				return false;
			}), Delay);
			UE_LOG(LogAsdaStoreTestL2, Log, TEXT("САМОТЕСТ-ЛАВКА-Л2: запуск через %.1f с"), Delay);
		}));
}

#endif // !UE_BUILD_SHIPPING
