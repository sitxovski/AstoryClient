// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ ВОЛНЫ Л1 ТОРГОВЛИ — ЛИЧНАЯ ЛАВКА, ПРОДАВЕЦ (29.09.2026, `Reports/trade/wave_L1.md`). [ПОРТ]
//
// ЧТО ПРОВЕРЯЕТ. Путь игрока через НАСТОЯЩИЕ функции: входы (функции 2/7 — эмоция 207 / хоткей 65 / доп-меню 2), гейты
//   6300, приём 6301/6303/6306 через ШТАТНЫЙ `UAsdaNetSubsystem::DebugFeedPacket` → `HandlePacket` (длины, делегат, очередь,
//   разбор в Tick), отправки 6300/6302/6305/4024 — БОЕВЫЕ `SendSelfStore*`/`SendCharStatePos4024` под перехватом
//   (`ArmSendCapture`, весь трафик: офлайн сокета нет), окно 22000 (сетка, нумпад 7, строки, цены, потолок 1 млрд, сумма),
//   витрину хозяина 23000 (подписи, лоты, поза 107/109), «Изменить», билет, ESC, смерть, сброс сессии, геометрию путём игрока.
//   Ожидания заданы ЯВНО по эталону (номера loc, смещения полей), а не вызовом проверяемой функции.
//   ★ Контрольные пары (закон pixel-metric-needs-control-pairs): каждое действие проверяется и в противоположном условии.
//
// Консоль: `asda.Trade.SelfTestStoreL1 [delay=N] [quit]`. Только офлайн (`FAsdaOutgoingGate::RefuseDebugWhenOnline`).
//   Строки — «САМОТЕСТ-ЛАВКА», итог — «САМОТЕСТ-ЛАВКА ИТОГ: ok=… fail=…».
#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING
#include "UI/AsdaUiHelpers.h"   // ★ R1: прямое включение (раньше приходило транзитивно)

#include "UI/AsdaSelfStoreSubsystem.h"
#include "AsdaCVarSave.h"                   // ★ ИСПРАВЛЕНИЕ 11.10.2026 (В0, N1(д)/U8): ключи консоли самотеста — запомнить и вернуть (значение + SetBy)
#include "UI/AsdaStallSignSubsystem.h"     // волна Л3: смерть → 6305 из рендера своей вывески
#include "UI/AsdaTradeStateSubsystem.h"
#include "UI/AsdaCharaInfoSubsystem.h"
#include "UI/AsdaExtraMenuSubsystem.h"
#include "UI/AsdaCraftWindowSubsystem.h"
#include "UI/AsdaInventorySubsystem.h"
#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaNotice.h"
#include "World/AsdaPlayerController.h"
#include "Test/AsdaSelfTestNet.h"   // ★ R1: тонкий доступ самотестов к сети (вместо AsdaNetSubsystem.h)
#include "Test/AsdaSelfTestKit.h"   // ★ 11.10.2026 (В3 S1): общий каркас самотестов (аргументы, отказ, перехват, итог, выход)
#include "AsdaSelf.h"
#include "AsdaBytes.h"
#include "Login/AsdaLoginData.h"   // DecodeCp1251 — название лавки в теле 6302
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/EditableText.h"
#include "Containers/Ticker.h"
#include "Framework/Application/SlateApplication.h"   // курсор для колеса (путь игрока)
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Misc/OutputDevice.h"
#include "Misc/OutputDeviceRedirector.h"
#include "UI/AsdaHotbarSubsystem.h"   // ★ 24.10.2026: откат предмета для проверки формата нотиса 8114

DEFINE_LOG_CATEGORY_STATIC(LogAsdaStoreTest, Log, All);

namespace AsdaStoreTestLocal
{
	// Перехватчик строк маршрутизатора сообщений «[msg] <категория> → <приёмники> | цвет=… TTL=… | «текст»» (канал и цвет нотиса).
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
}

struct FAsdaSelfStoreL1SelfTest
{
	// ★ 11.10.2026 (В3 S1, N1(г)): «свой» персонаж (charId, userNum, уровень…) подменяется в Setup — возврат RAII каркаса.
	FAsdaNetSelfScope SelfScope;
	// ★ 31.10.2026 (порядок пакета): сумка (1) и расходники (2) до прогона — Run их перезаливает, а прежде не возвращал:
	//   снаряжение 20552 в сумке 0 оставалось следующим самотестам (`asda.Upgrade.SelfTest` Д17 видел лишнюю вещь в сетке).
	TArray<TArray<uint8>> SavedBag;     // сумка 0…59 (сырые записи; пусто — ячейка пуста)
	TArray<TArray<uint8>> SavedSpend;   // расходники 0…59
	int32 Ok = 0;     // пройдено проверок
	int32 Fail = 0;   // провалено проверок

	// Перехваченный исходящий пакет.
	struct FCap { uint16 Op = 0; TArray<uint8> P; TCHAR Env = 0; };
	TSharedRef<TArray<FCap>> Caps = MakeShared<TArray<FCap>>();
	uint32 Gen = 0;   // поколение перехватчика

	// Одна проверка.
	void Expect(bool bCond, const FString& Name, const FString& Detail)
	{
		if (bCond) { ++Ok; } else { ++Fail; }
		UE_LOG(LogAsdaStoreTest, Log, TEXT("САМОТЕСТ-ЛАВКА %s: %s — %s"), bCond ? TEXT("ok") : TEXT("ПРОВАЛ"), *Name, *Detail);
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
		AsdaTestNet::DebugFeedPacket(Net, Op, Body);
		S->PumpPending();
	}
	static TArray<uint8> Body1(uint8 Code) { TArray<uint8> B; B.Add(Code); return B; }
	static TArray<uint8> Zero(int32 N) { TArray<uint8> B; B.SetNumZeroed(N); return B; }

	// Верхний msgbox очереди содержит текст loc-строки (первые 14 символов, без хвостов форматирования).
	static bool TopMsgIs(UGameInstance* GI, UAsdaWindowSubsystem* WM, int32 LocId)
	{
		const FString Want = AsdaNotice::Loc(GI, LocId).Left(14);
		return WM && !Want.IsEmpty() && WM->GetMsgBoxQueueTopText().Contains(Want);
	}

	void Run(UWorld* W)
	{
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaSelfStoreSubsystem* S = GI ? GI->GetSubsystem<UAsdaSelfStoreSubsystem>() : nullptr;
		UAsdaNetSubsystem* Net = GI ? AsdaTestNet::Get(GI) : nullptr;
		UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
		UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
		UAsdaCharaInfoSubsystem* CI = GI ? GI->GetSubsystem<UAsdaCharaInfoSubsystem>() : nullptr;
		if (!S || !Net || !WM || !Inv || !CI)
		{
			UE_LOG(LogAsdaStoreTest, Error, TEXT("САМОТЕСТ-ЛАВКА ИТОГ: ok=0 fail=1 (нет подсистем)"));
			return;
		}
		// ── Подготовка: свой персонаж (charId/userNum/уровень), предметы, перехват всего трафика ──
		const uint8 LevelWas = AsdaTestNet::SelfLevel(Net);
		const int32 CharIdWas = AsdaTestNet::SelectedCharId(Net);
		SelfScope.Capture(Net, TEXT("asda.Trade.SelfTestStoreL1"));   // ★ В3 S1: снимок «своего» до подмены
		AsdaTestNet::SelectedCharId(Net) = 424242;
		AsdaTestNet::SetSelfUserNumForDebug(Net, 17);
		AsdaTestNet::SelfLevel(Net) = 30;
		S->ResetSessionState(EAsdaSessionResetReason::MapChanged);
		SaveBags(Inv);
		for (int32 i = 0; i < 60; ++i) { Inv->DebugPutItem(2, i, 0, 0); }
		for (int32 i = 0; i < 30; ++i) { Inv->DebugPutItem(1, i, 0, 0); }
		// Расходники: 20592 (цена 2100, стопка 7), 20571…(цена 50…) по одной ячейке; всего 36 кандидатов (>30 — колесо).
		Inv->DebugPutItem(2, 0, 20592, 7);
		static const int32 kCodes[] = { 20571, 20572, 20573, 20577, 20578, 20579, 20583, 20586, 20587, 20588, 20589, 20590 };
		for (int32 i = 1; i < 36; ++i) { Inv->DebugPutItem(2, i, kCodes[(i - 1) % UE_ARRAY_COUNT(kCodes)], 3); }
		Inv->DebugPutItem(1, 0, 20552, 1);   // снаряжение (tradableFlag 0 ⇒ без нумпада)
		Caps->Reset();
		TSharedRef<TArray<FCap>> C = Caps;
		Gen = AsdaSelfTestKit::ArmCapture(Net, [C](uint16 Op, const uint8* P, int32 Len, bool, TCHAR Env)
		{
			FCap X; X.Op = Op; X.Env = Env; if (P && Len > 0) { X.P.Append(P, Len); } C->Add(MoveTemp(X));
		}, 0, TEXT("asda.Trade.SelfTestStoreL1"));
		WM->ClearMsgBoxQueue(TEXT("самотест лавки"));

		// ═══ 1. Входы и гейты 6300 ═══
		{
			AsdaTestNet::SelfLevel(Net) = 9; Caps->Reset(); WM->ClearMsgBoxQueue(TEXT("самотест"));
			const bool bA = CI->PlayEmotion(UAsdaCharaInfoSubsystem::MakeEmotionCode(2, 7));
			Expect(!bA && Count(6300) == 0 && TopMsgIs(GI, WM, 107080), TEXT("6300: уровень 9 → 107080, пакета нет"),
				FString::Printf(TEXT("возврат=%d 6300=%d msgbox=«%s»"), bA ? 1 : 0, Count(6300), *WM->GetMsgBoxQueueTopText()));
			AsdaTestNet::SelfLevel(Net) = 30; Caps->Reset(); WM->ClearMsgBoxQueue(TEXT("самотест"));
			const bool bB = CI->PlayEmotion(UAsdaCharaInfoSubsystem::MakeEmotionCode(2, 7));
			TArray<uint8> P; const int32 N = Count(6300, &P);
			Expect(bB && N == 1 && P.Num() == 70 && AsdaBytes::RdI32(P.GetData()) == 424242 && P[4] == 0
				&& AsdaBytes::RdI16(P.GetData() + 25 + 5) == -1 && AsdaBytes::RdI16(P.GetData() + 25 + 11) == -1,
				TEXT("6300 (контроль): уровень 30 → тело 70 б, charId @0, премиум @4 = 0, запись билета @25 сброшена"),
				FString::Printf(TEXT("6300=%d длина=%d charId=%d прем=%d билет.слот=%d билет.индекс=%d"), N, P.Num(),
					P.Num() >= 4 ? AsdaBytes::RdI32(P.GetData()) : -1, P.Num() > 4 ? P[4] : -1,
					P.Num() >= 32 ? AsdaBytes::RdI16(P.GetData() + 30) : 0, P.Num() >= 38 ? AsdaBytes::RdI16(P.GetData() + 36) : 0));
			// Доп-меню cmd 2 и хоткей 65 — тот же вход (функция 2/7).
			Caps->Reset();
			if (UAsdaExtraMenuSubsystem* EM = GI->GetSubsystem<UAsdaExtraMenuSubsystem>()) { EM->DispatchCommand(2, TEXT("самотест: доп-меню «личная лавка»")); }
			Expect(Count(6300) == 1, TEXT("доп-меню cmd 2 → 6300"), FString::Printf(TEXT("6300=%d"), Count(6300)));
			// Гейт ремесла: окно 76000 открыто → молча; закрыто → 6300 (контрольная пара).
			if (UAsdaCraftWindowSubsystem* Cw = GI->GetSubsystem<UAsdaCraftWindowSubsystem>())
			{
				if (!Cw->IsOpen()) { Cw->GateAndToggle(TEXT("самотест лавки")); }
				Caps->Reset();
				const bool bC = S->DispatchPrivateShop(TEXT("самотест: ремесло открыто"));
				const int32 N1 = Count(6300);
				if (Cw->IsOpen()) { Cw->HideAndCleanup(); }
				Caps->Reset();
				const bool bD = S->DispatchPrivateShop(TEXT("самотест: ремесло закрыто"));
				Expect(Cw && !bC && N1 == 0 && bD && Count(6300) == 1, TEXT("гейт ремесла 6300: открыто молча ↔ закрыто шлёт"),
					FString::Printf(TEXT("открыто: возврат=%d 6300=%d; закрыто: возврат=%d 6300=%d"), bC ? 1 : 0, N1, bD ? 1 : 0, Count(6300)));
			}
		}

		// ═══ 2. 6301: коды → окно / нотисы ═══
		{
			static const int32 kLoc[8] = { 107021, 0, 107022, 107023, 107024, 112500, 112501, 112502 };
			bool bAll = true; FString Det;
			for (int32 Code = 0; Code <= 7; ++Code)
			{
				if (Code == 1) { continue; }
				WM->ClearMsgBoxQueue(TEXT("самотест"));
				Feed(Net, S, 6301, Body1((uint8)Code));
				const bool bHit = TopMsgIs(GI, WM, kLoc[Code]) && !S->IsSellWindowOpen();
				bAll &= bHit;
				Det += FString::Printf(TEXT("%d→%d:%s "), Code, kLoc[Code], bHit ? TEXT("да") : *FString::Printf(TEXT("НЕТ«%s»"), *WM->GetMsgBoxQueueTopText()));
			}
			Expect(bAll, TEXT("6301 коды 0/2…7 → msgbox по обработчику, окно закрыто"), Det);
			WM->ClearMsgBoxQueue(TEXT("самотест"));
			Feed(Net, S, 6301, Body1(9));
			Expect(WM->GetMsgBoxQueueTopText() == TEXT("9") && !S->IsSellWindowOpen(), TEXT("6301 код 9 → msgbox с числом «9»"),
				FString::Printf(TEXT("верх «%s»"), *WM->GetMsgBoxQueueTopText()));
			WM->ClearMsgBoxQueue(TEXT("самотест"));
			Feed(Net, S, 6301, Body1(1));
			Expect(S->IsSellWindowOpen() && UAsdaTradeStateSubsystem::IsSellWindowOpen(GI) && WM->IsWindowVisible(FName(TEXT("StreetStall_Open_Win"))),
				TEXT("6301 код 1 (контроль) → окно 22000 открыто, признак 22000 в общей точке"),
				FString::Printf(TEXT("%s | %s"), *S->DumpState(), *S->DumpControls()));
			Expect(S->GetFoundControls22000() == 127, TEXT("разбор 22000: все 127 привязок (22199 — колесо кодом)"), S->DumpControls());
			// Функция 2/7 при открытом окне — молча (контроль к п.1).
			Caps->Reset();
			const bool bE = S->DispatchPrivateShop(TEXT("самотест: окно открыто"));
			Expect(!bE && Count(6300) == 0, TEXT("функция 2/7 при открытом 22000 — молча"), FString::Printf(TEXT("6300=%d"), Count(6300)));
			// 6301 код 1 при открытом окне → 6305 (тумблер `sellWindow_Open`).
			Caps->Reset();
			Feed(Net, S, 6301, Body1(1));
			TArray<uint8> P; const int32 N = Count(6305, &P);
			Expect(N == 1 && P.Num() == 6 && AsdaBytes::RdI32(P.GetData()) == 424242 && AsdaBytes::RdI16(P.GetData() + 4) == 17 && S->IsSellWindowOpen(),
				TEXT("6301 код 1 при открытом окне → 6305 (charId хозяина @0, userNum @4), окно ещё открыто"),
				FString::Printf(TEXT("6305=%d длина=%d"), N, P.Num()));
		}

		// ═══ 3. Сетка, нумпад 7, строки ═══
		{
			Expect(S->DebugGridText(0) == TEXT("7") && S->DebugGridText(1) == TEXT("3"), TEXT("сетка вкладки 0: надписи количества"),
				FString::Printf(TEXT("ячейка0 «%s» ячейка1 «%s»"), *S->DebugGridText(0), *S->DebugGridText(1)));
			// ЛКМ по стопке 7 штучного торгуемого → нумпад, строки нет; ПКМ → весь стек (контрольная пара).
			S->OnGridCellClicked(0, false);
			const bool bNum = WM->IsNumPadOpen();
			const int32 Rows0 = S->GetSellCount();
			// Ввести 5 в поле нумпада (1412 `NUMPAD_NUMBOX`) и нажать OK — путём окна.
			bool bTyped = false;
			if (UUserWidget* NW = WM->FindWindow(FName(TEXT("NUMPAD_WIN"))))
			{
				TArray<UWidget*> All; if (NW->WidgetTree) { NW->WidgetTree->GetAllWidgets(All); }
				for (UWidget* X : All) { if (UEditableText* E = Cast<UEditableText>(X)) { if (X->GetName().Contains(TEXT("NUMPAD_NUMBOX"))) { E->SetText(FText::FromString(TEXT("5"))); bTyped = true; } } }
			}
			WM->CloseNumPad(true);
			Expect(bNum && Rows0 == 0 && bTyped && S->GetSellCount() == 1 && S->DebugRowText(0, 1) == TEXT("5"),
				TEXT("ЛКМ по стопке 7 → нумпад режим 7; ввод 5 + OK → строка с кол-вом 5"),
				FString::Printf(TEXT("нумпад=%d строк до=%d введено=%d строк=%d кол-во «%s»"), bNum ? 1 : 0, Rows0, bTyped ? 1 : 0, S->GetSellCount(), *S->DebugRowText(0, 1)));
			const float A0 = S->DebugGridAlpha(0), A1 = S->DebugGridAlpha(1);
			Expect(FMath::IsNearlyEqual(A0, 125.f / 255.f, 0.01f) && FMath::IsNearlyEqual(A1, 1.f, 0.01f),
				TEXT("выставленная ячейка тускнеет (0x7D) ↔ соседняя — нет"), FString::Printf(TEXT("альфа0=%.3f альфа1=%.3f"), A0, A1));
			S->OnGridCellClicked(0, true);   // дубликат — молча
			Expect(S->GetSellCount() == 1, TEXT("повторный клик по выставленному — молча"), FString::Printf(TEXT("строк=%d"), S->GetSellCount()));
			S->OnGridCellClicked(1, true);   // ПКМ — вся стопка 3 без нумпада
			Expect(!WM->IsNumPadOpen() && S->GetSellCount() == 2 && S->DebugRowText(1, 1) == TEXT("3"),
				TEXT("ПКМ (0xE) по стопке 3 → вся стопка без нумпада"), FString::Printf(TEXT("строк=%d кол-во «%s»"), S->GetSellCount(), *S->DebugRowText(1, 1)));
			// Цена по умолчанию = `ItemRecord.price` (20592 — 2100); шаг ▲ = 5 × (2100/100) = 105; ▼ возвращает.
			const FString P0 = S->DebugRowText(0, 0);
			S->OnRowButton(0, 1);
			const FString P1 = S->DebugRowText(0, 0);
			S->OnRowButton(0, 2);
			Expect(P0 == TEXT("2,100") && P1 == TEXT("2,205") && S->DebugRowText(0, 0) == TEXT("2,100"),
				TEXT("цена по таблице 2,100; ▲ +5 % базы = 2,205; ▼ обратно"), FString::Printf(TEXT("«%s» → «%s» → «%s»"), *P0, *P1, *S->DebugRowText(0, 0)));
			// Итог строки = цена × кол-во; сумма.
			Expect(S->DebugRowText(0, 2) == TEXT("10,500") && S->DebugSumText() == TEXT("10,650"),
				TEXT("итог строки 2 100 × 5 = 10,500; сумма с ещё 50 × 3 = 10,650"), FString::Printf(TEXT("итог «%s» сумма «%s»"), *S->DebugRowText(0, 2), *S->DebugSumText()));
			// Кол-во ▲ выше стопки упирается в [5] (7): 5 → 6 → 7 → 7.
			S->OnRowButton(0, 3); S->OnRowButton(0, 3); S->OnRowButton(0, 3);
			Expect(S->DebugRowText(0, 1) == TEXT("7"), TEXT("кол-во ▲ упирается в размер стопки 7"), S->DebugRowText(0, 1));
			// Ввод цены полем: «2,000,000,000» → потолок P43 1,000,000,000 ↔ «123» → 123.
			if (UUserWidget* SW = S->DebugSellWidget())
			{
				UEditableText* Pe = nullptr;
				TArray<UWidget*> All; if (SW->WidgetTree) { SW->WidgetTree->GetAllWidgets(All); }
				for (UWidget* X : All) { if (X->GetName() == TEXT("Stall_Open_SellPrice00")) { Pe = Cast<UEditableText>(X); } }
				if (Pe)
				{
					Pe->SetText(FText::FromString(TEXT("2,000,000,000"))); S->OnRowEditText(0, true);
					const FString Cap = S->DebugRowText(0, 0);
					Pe->SetText(FText::FromString(TEXT("123"))); S->OnRowEditText(0, true);
					Expect(Cap == TEXT("1,000,000,000") && S->DebugRowText(0, 0) == TEXT("123"),
						TEXT("ввод цены: 2 млрд → потолок 1 000 000 000 (P43) ↔ 123 → 123"), FString::Printf(TEXT("«%s» / «%s»"), *Cap, *S->DebugRowText(0, 0)));
					// К-5: три строки по 1 млрд — сумма 3,000,000,000 видна целиком (оригинал оборвал бы перерисовку).
					Pe->SetText(FText::FromString(TEXT("1000000000"))); S->OnRowEditText(0, true);
				}
				else { Expect(false, TEXT("поле цены строки 0"), TEXT("Stall_Open_SellPrice00 не найден")); }
			}
			// Клик по полю цены — цена 0 (`sellWnd_ClearSlotQtyByButton`, оригинал).
			S->OnPriceFieldClicked(1);
			Expect(S->DebugRowText(1, 0) == TEXT("0"), TEXT("клик по полю цены строки 1 → 0 (оригинал)"), S->DebugRowText(1, 0));
			// Снаряжение (вкладка 1): tradableFlag 0 ⇒ ЛКМ без нумпада.
			S->OnTabSelected(1);
			const FString G0 = S->DebugGridText(0);
			S->OnGridCellClicked(0, false);
			Expect(S->GetSourceTab() == 1 && !WM->IsNumPadOpen() && S->GetSellCount() == 3, TEXT("вкладка 1: снаряжение ЛКМ — сразу в список"),
				FString::Printf(TEXT("вкладка=%d нумпад=%d строк=%d надпись0 «%s»"), S->GetSourceTab(), WM->IsNumPadOpen() ? 1 : 0, S->GetSellCount(), *G0));
			// Колесо: вкладка 0 — листает (36 кандидатов > 30). ★ 29.10.2026 (инвентарь ×2): при сумке 60 вкладка 1 тоже
			//   листается по рядам (`AsdaSellScrollMax`); ключ `asda.Inven.ShellX2 0` (сумка 30) — как оригинал, не листает.
			S->OnWheel(-1);
			const int32 Pos1 = S->GetScrollPos();
			S->OnWheel(+1);
			AsdaCVarSave::SetForTest(TEXT("asda.Inven.ShellX2"), 0);
			S->OnWheel(-1);
			const int32 Pos1Legacy = S->GetScrollPos();
			AsdaCVarSave::SetForTest(TEXT("asda.Inven.ShellX2"), 1);
			S->OnTabSelected(0);
			S->OnWheel(-1);
			Expect(Pos1 == 1 && Pos1Legacy == 0 && S->GetScrollPos() == 1,
				TEXT("колесо: вкладка 1 — ряд 1 при сумке 60 / нет при ShellX2=0 ↔ вкладка 0 — позиция 1"),
				FString::Printf(TEXT("вкладка1=%d вкладка1(ключ 0)=%d вкладка0=%d"), Pos1, Pos1Legacy, S->GetScrollPos()));
			S->OnWheel(+1);
			// Заполнить до 10 и 11-я → 107010 (чат+плашка), строк остаётся 10.
			for (int32 c = 2; c < 12 && S->GetSellCount() < 10; ++c) { S->OnGridCellClicked(c, true); }
			const int32 Full = S->GetSellCount();
			S->OnGridCellClicked(20, true);
			bool bJ = false; for (const FString& L : S->GetJournal()) { if (L.Contains(TEXT("107010"))) { bJ = true; } }
			Expect(Full == 10 && S->GetSellCount() == 10 && bJ, TEXT("11-я строка → 107010, строк 10"), FString::Printf(TEXT("строк=%d журнал107010=%d"), S->GetSellCount(), bJ ? 1 : 0));
			// Снять строку кликом по иконке 22308.
			S->OnSellIconClicked(9);
			Expect(S->GetSellCount() == 9, TEXT("клик по иконке строки 9 — снято"), FString::Printf(TEXT("строк=%d"), S->GetSellCount()));
		}

		// ═══ 4. 6302: пусто/название/флаг/раскладка ═══
		{
			// Пустое название → 107062, 6302 нет; затем название → 6302 (К-16: второй «OK» работает ↔ ключ 0 — нет).
			UEditableText* NameEd = nullptr;
			if (UUserWidget* SW = S->DebugSellWidget())
			{
				TArray<UWidget*> All; if (SW->WidgetTree) { SW->WidgetTree->GetAllWidgets(All); }
				for (UWidget* X : All) { if (X->GetName() == TEXT("Stall_Open_NameInput")) { NameEd = Cast<UEditableText>(X); } }
			}
			if (NameEd) { NameEd->SetText(FText::GetEmpty()); }
			Caps->Reset(); WM->ClearMsgBoxQueue(TEXT("самотест"));
			S->OnOkClicked();
			const bool bEmpty = TopMsgIs(GI, WM, 107062) && Count(6302) == 0;
			if (NameEd) { NameEd->SetText(FText::FromString(TEXT("Лавка <Тест>"))); }
			// Ревизия Л1 (МЕЛК-3): лот 0 — 7 × 1 000 000 000 > 2^31−1 ⇒ 6302 не уходит, 107040 ([ПОРТ]) ↔ цена 300 000 000 — уходит.
			WM->ClearMsgBoxQueue(TEXT("самотест"));
			S->OnOkClicked();
			const bool bOver = TopMsgIs(GI, WM, 107040) && Count(6302) == 0;
			UEditableText* Pe0 = nullptr;
			if (UUserWidget* SW = S->DebugSellWidget())
			{
				TArray<UWidget*> All; if (SW->WidgetTree) { SW->WidgetTree->GetAllWidgets(All); }
				for (UWidget* X : All) { if (X->GetName() == TEXT("Stall_Open_SellPrice00")) { Pe0 = Cast<UEditableText>(X); } }
			}
			if (Pe0) { Pe0->SetText(FText::FromString(TEXT("300,000,000"))); S->OnRowEditText(0, true); }
			Expect(bOver && Pe0 && S->DebugRowText(0, 0) == TEXT("300,000,000"), TEXT("МЕЛК-3: лот 7 × 1 млрд > 2^31−1 → 107040 без 6302 ([ПОРТ])"),
				FString::Printf(TEXT("107040=%d 6302=%d цена стала «%s»"), bOver ? 1 : 0, Count(6302), *S->DebugRowText(0, 0)));
			S->OnOkClicked();
			TArray<uint8> P; const int32 N = Count(6302, &P);
			Expect(NameEd && bEmpty && N == 1, TEXT("6302: пустое название → 107062 без пакета; с названием и ценой 3e8 → 6302 (К-16, контроль МЕЛК-3)"),
				FString::Printf(TEXT("пусто=%d 6302=%d"), bEmpty ? 1 : 0, N));
			// Раскладка: 505 б; @0 charId; @4 число 9; лот 0: код 20592 к=2 сл=0 кол=7 индекс 0 цена 3e8; лот 1 цена 0 → 1;
			//   @17 4×i16 −1; лот 9 (пустой) — сброс (@5/@11 = −1); @455 название без «<>».
			const uint8* B = P.GetData();
			const bool bLay = P.Num() == 505 && AsdaBytes::RdI32(B) == 424242 && B[4] == 9
				&& AsdaBytes::RdI32(B + 5) == 20592 && B[9] == 2 && AsdaBytes::RdI16(B + 10) == 0 && AsdaBytes::RdI32(B + 12) == 7
				&& AsdaBytes::RdI16(B + 16) == 0 && AsdaBytes::RdI32(B + 18) == 300000000 && AsdaBytes::RdI16(B + 22) == -1
				&& AsdaBytes::RdI32(B + 50 + 13) == 1 && AsdaBytes::RdI16(B + 50 + 11) == 1
				&& AsdaBytes::RdI16(B + 5 + 45 * 9 + 5) == -1 && AsdaBytes::RdI16(B + 5 + 45 * 9 + 11) == -1;
			const FString Title = P.Num() == 505 ? FAsdaLoginData::DecodeCp1251(B + 455, 50) : FString();
			Expect(bLay && Title == TEXT("Лавка Тест"), TEXT("6302 раскладка: charId, число, лоты (код/к/слот/кол/индекс/цена ≤0→1), сброс, название"),
				FString::Printf(TEXT("длина=%d число=%d лот0 код=%d к=%d сл=%d кол=%d инд=%d цена=%d; лот1 цена=%d; название «%s»"),
					P.Num(), P.Num() > 4 ? B[4] : -1, P.Num() >= 9 ? AsdaBytes::RdI32(B + 5) : 0, P.Num() > 9 ? B[9] : -1,
					P.Num() >= 12 ? AsdaBytes::RdI16(B + 10) : 0, P.Num() >= 16 ? AsdaBytes::RdI32(B + 12) : 0,
					P.Num() >= 18 ? AsdaBytes::RdI16(B + 16) : 0, P.Num() >= 22 ? AsdaBytes::RdI32(B + 18) : 0,
					P.Num() >= 67 ? AsdaBytes::RdI32(B + 63) : 0, *Title));
			// Флаг +0x388: второй «OK» до 6303 — пакета нет ↔ после 6303 код 2 (107042) — снова уходит.
			Caps->Reset(); S->OnOkClicked();
			const int32 NBefore = Count(6302);
			WM->ClearMsgBoxQueue(TEXT("самотест"));
			TArray<uint8> R2 = Zero(452); R2[0] = 2;
			Feed(Net, S, 6303, R2);
			const bool bMsg = TopMsgIs(GI, WM, 107042);
			S->OnOkClicked();
			Expect(NBefore == 0 && bMsg && Count(6302) == 1 && S->IsSellWindowOpen(), TEXT("флаг 6302: до ответа молчит ↔ 6303 код 2 (107042) снова разрешает"),
				FString::Printf(TEXT("до=%d 107042=%d после=%d"), NBefore, bMsg ? 1 : 0, Count(6302)));
		}

		// ═══ 5. 6303 код 1 → витрина хозяина, поза 107; «Изменить»; 6306 → поза 109; переоткрытие хранит список ═══
		{
			Caps->Reset();
			TArray<uint8> R = Zero(452); R[0] = 1; R[1] = 2;
			AsdaBytes::WrI32(R.GetData() + 2 + 0, 20592); R[2 + 4] = 2; AsdaBytes::WrI16(R.GetData() + 2 + 5, 0); AsdaBytes::WrI32(R.GetData() + 2 + 7, 7); AsdaBytes::WrI32(R.GetData() + 2 + 13, 1000);
			AsdaBytes::WrI32(R.GetData() + 47 + 0, 20571); R[47 + 4] = 2; AsdaBytes::WrI16(R.GetData() + 47 + 5, 1); AsdaBytes::WrI32(R.GetData() + 47 + 7, 0); AsdaBytes::WrI32(R.GetData() + 47 + 13, 25);
			Feed(Net, S, 6303, R);
			TArray<uint8> Pose; const int32 NPose = Count(4024, &Pose);
			Expect(!S->IsSellWindowOpen() && S->IsOwnerShowcaseOpen() && UAsdaTradeStateSubsystem::IsShopDlgOpen(GI) && !UAsdaTradeStateSubsystem::IsSellWindowOpen(GI)
				&& NPose == 1 && Pose.Num() >= 20 && AsdaBytes::RdI16(Pose.GetData() + 18) == 107,
				TEXT("6303 код 1 → 22000 скрыто, витрина хозяина открыта, признаки, поза 107 (4024)"),
				FString::Printf(TEXT("%s; 4024=%d код=%d"), *S->DumpState(), NPose, Pose.Num() >= 20 ? AsdaBytes::RdI16(Pose.GetData() + 18) : -1));
			Expect(S->DebugShowcaseText(23305) == AsdaNotice::Loc(GI, 107060) && S->DebugShowcaseText(23301) == AsdaNotice::Loc(GI, 107009)
				&& !S->DebugListHdrShown() && S->DebugShowcaseText(23006) == TEXT("Лавка Тест"),
				TEXT("витрина хозяина: 23305 «Изменить» (107060), 23301 = 107009, корзина 23240 скрыта, заголовок = название"),
				FString::Printf(TEXT("23305 «%s» 23301 «%s» 23240 видна=%d 23006 «%s»"), *S->DebugShowcaseText(23305), *S->DebugShowcaseText(23301),
					S->DebugListHdrShown() ? 1 : 0, *S->DebugShowcaseText(23006)));
			Expect(S->DebugShowcaseText(23122) == TEXT("1,000") && S->DebugShowcaseText(23123) == TEXT("7,000") && S->DebugShowcaseText(23121) == TEXT("7")
				&& S->DebugShowcaseText(23133) == TEXT("25") && S->DebugShowcaseText(23302) == TEXT("7,025"),
				TEXT("лоты витрины: цена/итог/остаток (кол-во 0 → 1), сумма хозяина 23302"),
				FString::Printf(TEXT("23122 «%s» 23123 «%s» 23121 «%s» 23133 «%s» 23302 «%s»"), *S->DebugShowcaseText(23122), *S->DebugShowcaseText(23123),
					*S->DebugShowcaseText(23121), *S->DebugShowcaseText(23133), *S->DebugShowcaseText(23302)));
			Expect(S->GetFoundControls23000() == 59, TEXT("разбор 23000 (режим хозяина): 59 привязок"), S->DumpControls());
			// «Изменить»: 6305 + 6300, переоткрытие.
			const int32 RowsKeep = S->GetSellCount();
			Caps->Reset();
			S->OnShowcaseMainButton();
			Expect(Count(6305) == 1 && Count(6300) == 1 && S->IsReopenPending(), TEXT("«Изменить» 23305 → 6305 + 6300, переоткрытие"),
				FString::Printf(TEXT("6305=%d 6300=%d переоткрытие=%d"), Count(6305), Count(6300), S->IsReopenPending() ? 1 : 0));
			Caps->Reset();
			TArray<uint8> Cl = Zero(571); Cl[0] = 1;
			Feed(Net, S, 6306, Cl);
			TArray<uint8> Pose2; const int32 NPose2 = Count(4024, &Pose2);
			Expect(!S->IsShowcaseOpen() && !UAsdaTradeStateSubsystem::IsSelfStoreOpen(GI) && NPose2 == 1 && Pose2.Num() >= 20 && AsdaBytes::RdI16(Pose2.GetData() + 18) == 109,
				TEXT("6306 код 1 → витрина скрыта, признаки сняты, поза 109"), FString::Printf(TEXT("4024=%d код=%d"), NPose2, Pose2.Num() >= 20 ? AsdaBytes::RdI16(Pose2.GetData() + 18) : -1));
			Feed(Net, S, 6301, Body1(1));
			Expect(S->IsSellWindowOpen() && S->GetSellCount() == RowsKeep && !S->IsReopenPending(), TEXT("переоткрытие после «Изменить» — список сохранён"),
				FString::Printf(TEXT("строк=%d (было %d)"), S->GetSellCount(), RowsKeep));
			// Контроль: обычное закрытие (6305 кнопкой) и новое открытие — список пуст.
			Feed(Net, S, 6306, Cl);
			Feed(Net, S, 6301, Body1(1));
			Expect(S->IsSellWindowOpen() && S->GetSellCount() == 0, TEXT("обычное открытие (контроль) — список очищен"), FString::Printf(TEXT("строк=%d"), S->GetSellCount()));
		}

		// ═══ 6. 6306: коды 0/2/3, записи К-1 ═══
		{
			WM->ClearMsgBoxQueue(TEXT("самотест"));
			TArray<uint8> Cl = Zero(571);
			Cl[0] = 0; Feed(Net, S, 6306, Cl);
			const bool b0 = TopMsgIs(GI, WM, 107030) && S->IsSellWindowOpen();
			WM->ClearMsgBoxQueue(TEXT("самотест"));
			Cl[0] = 3; Feed(Net, S, 6306, Cl);
			const bool b3 = WM->GetMsgBoxQueueNum() == 0 && S->IsSellWindowOpen();
			Cl[0] = 2; Feed(Net, S, 6306, Cl);
			const bool b2 = TopMsgIs(GI, WM, 107031) && !S->IsSellWindowOpen();
			Expect(b0 && b3 && b2, TEXT("6306: 0 → 107030 (окно остаётся), 3 → молча (К-7), 2 → 107031 и окно скрыто"),
				FString::Printf(TEXT("0=%d 3=%d 2=%d"), b0 ? 1 : 0, b3 ? 1 : 0, b2 ? 1 : 0));
			// К-1: 10 записей (контейнер 2, ячейки 40…49) → все 10 применены ↔ ключ 0 — только 6.
			auto MakeClose = []()
			{
				TArray<uint8> B = Zero(571); B[0] = 1;
				for (int32 i = 0; i < 10; ++i)
				{
					uint8* Rec = B.GetData() + 1 + 57 * i;
					AsdaBytes::WrI32(Rec, 20572); Rec[4] = 2; AsdaBytes::WrI16(Rec + 5, 40 + i); AsdaBytes::WrI32(Rec + 9, 2);
				}
				return B;
			};
			auto Filled = [Inv]() { int32 N = 0; for (int32 i = 40; i < 50; ++i) { const FAsdaItemInstance* It = Inv->GetCell(2, i); if (It && It->ItemCode == 20572) { ++N; } } return N; };
			for (int32 i = 40; i < 50; ++i) { Inv->DebugPutItem(2, i, 0, 0); }
			Feed(Net, S, 6306, MakeClose());
			const int32 NFix = Filled();
			for (int32 i = 40; i < 50; ++i) { Inv->DebugPutItem(2, i, 0, 0); }
			IConsoleVariable* K1 = IConsoleManager::Get().FindConsoleVariable(TEXT("asda.Trade.StoreFixK1"));
			if (K1) { AsdaCVarSave::SetForTest(TEXT("asda.Trade.StoreFixK1"), 0); }   // ★ ИСПРАВЛЕНИЕ 11.10.2026 (В0, N1(д)/U8): запомнить исходное (прежде Set(0) с SetByCode)
			Feed(Net, S, 6306, MakeClose());
			const int32 NOrig = Filled();
			if (K1) { AsdaCVarSave::SetForTest(TEXT("asda.Trade.StoreFixK1"), 1); }
			for (int32 i = 40; i < 50; ++i) { Inv->DebugPutItem(2, i, 0, 0); }
			Expect(NFix == 10 && NOrig == 6, TEXT("6306 код 1: записи — исправление К-1 10 из 10 ↔ оригинал 6"), FString::Printf(TEXT("исправл.=%d оригинал=%d"), NFix, NOrig));
		}

		// ═══ 7. Билет премиум-лавки, К-3 ═══
		{
			Caps->Reset();
			uint8 Rec[57] = {}; AsdaBytes::WrI32(Rec, 343); Rec[4] = 1; AsdaBytes::WrI16(Rec + 5, 12); AsdaBytes::WrI32(Rec + 9, 1);
			S->OnTicketUsed(Rec);
			TArray<uint8> P; const int32 N = Count(6300, &P);
			Expect(S->IsPremium() && N == 1 && P.Num() == 70 && P[4] == 1 && AsdaBytes::RdI32(P.GetData() + 25) == 343 && P[29] == 1 && AsdaBytes::RdI16(P.GetData() + 30) == 12,
				TEXT("билет (5451 подтип 65): премиум 1, 6300 с премиумом @4 и записью билета @25 (код/контейнер/слот)"),
				FString::Printf(TEXT("6300=%d прем=%d билет код=%d к=%d сл=%d"), N, P.Num() > 4 ? P[4] : -1, P.Num() >= 29 ? AsdaBytes::RdI32(P.GetData() + 25) : 0,
					P.Num() > 29 ? P[29] : -1, P.Num() >= 32 ? AsdaBytes::RdI16(P.GetData() + 30) : 0));
			TArray<uint8> Cl = Zero(571); Cl[0] = 1;
			// К-3: без переоткрытия 6306 сбрасывает премиум (оригинал и исправление совпадают) ↔ при переоткрытии — сохраняет.
			Feed(Net, S, 6306, Cl);
			const bool bReset = !S->IsPremium();
			// Ревизия Л1 (СРЕДН-1 а): витрина хозяина премиум-лавки; «Изменить» без билета в сумке → 5450 нет, переоткрытие НЕ
			//   взведено ↔ билет в сумке (переложен в ячейку 5) → 5450 по этой ячейке, переоткрытие взведено.
			S->OnTicketUsed(Rec);
			Feed(Net, S, 6301, Body1(1));
			TArray<uint8> R = Zero(452); R[0] = 1; R[1] = 0; Feed(Net, S, 6303, R);
			Inv->DebugPutItem(1, 12, 0, 0);
			Caps->Reset();
			S->OnShowcaseMainButton();
			const int32 N5450a = Count(5450); const bool bReopenA = S->IsReopenPending();
			Inv->DebugPutItem(1, 5, 343, 1);
			Caps->Reset();
			S->OnShowcaseMainButton();
			TArray<uint8> U; const int32 N5450b = Count(5450, &U);
			const bool bReopenB = S->IsReopenPending();
			Expect(N5450a == 0 && !bReopenA && N5450b == 1 && bReopenB && U.Num() >= 8 && AsdaBytes::RdI32(U.GetData() + 2) == 343 && AsdaBytes::RdI16(U.GetData() + 6) == 5,
				TEXT("СРЕДН-1: «Изменить» премиум без билета — 5450 нет, переоткрытие не взведено ↔ билет в сумке — 5450 (код 343, ячейка 5), взведено"),
				FString::Printf(TEXT("без: 5450=%d переоткр=%d; с: 5450=%d переоткр=%d код=%d ячейка=%d"), N5450a, bReopenA ? 1 : 0, N5450b, bReopenB ? 1 : 0,
					U.Num() >= 6 ? AsdaBytes::RdI32(U.GetData() + 2) : 0, U.Num() >= 8 ? AsdaBytes::RdI16(U.GetData() + 6) : 0));
			// К-3: 6306/1 при переоткрытии сохраняет премиум (пара к bReset выше).
			Feed(Net, S, 6306, Cl);
			const bool bKept = S->IsPremium();
			// СРЕДН-1 б: переоткрытие отвергнуто 6301 кодом 3 → пометка и премиум сняты ↔ (контроль) 6301 код 1 — окно открыто, премиум цел.
			Feed(Net, S, 6301, Body1(3));
			const bool bClear = !S->IsPremium() && !S->IsReopenPending();
			Expect(bReset && bKept && bClear, TEXT("К-3: 6306 без переоткрытия сбрасывает премиум ↔ при «Изменить» сохраняет; 6301/3 затем снимает премиум и пометку"),
				FString::Printf(TEXT("сброшен=%d сохранён=%d снят после 6301/3=%d"), bReset ? 1 : 0, bKept ? 1 : 0, bClear ? 1 : 0));
			S->OnTicketUsed(Rec);
			Feed(Net, S, 6301, Body1(1));
			Feed(Net, S, 6303, R);
			Inv->DebugPutItem(1, 12, 343, 1);
			S->OnShowcaseMainButton();
			Feed(Net, S, 6306, Cl);
			Feed(Net, S, 6301, Body1(1));
			Expect(S->IsPremium() && S->IsSellWindowOpen() && !S->IsReopenPending(), TEXT("СРЕДН-1 контроль: 6301/1 после «Изменить» — окно открыто, премиум цел"),
				S->DumpState());
			Inv->DebugPutItem(1, 5, 0, 0); Inv->DebugPutItem(1, 12, 0, 0);
			S->ResetSessionState(EAsdaSessionResetReason::LeftWorld);
		}

		// ═══ 8. ESC, смерть, сброс сессии ═══
		{
			S->DebugSetHost(424242, 17);   // полный сброс шага 7 стёр кэш хозяина (как выход в выбор персонажа)
			Feed(Net, S, 6301, Body1(1));
			// Контрольная пара: поверх — однокнопочный msgbox (модалка съедает ESC и ничего не закрывает, эталон) ↔ без него — 6305.
			WM->ClearMsgBoxQueue(TEXT("самотест"));
			AsdaNotice::MsgBoxLoc(GI, EAsdaMsgCategory::Trade, 107085);
			Caps->Reset();
			const bool bEscModal = WM->CloseTopmostOnEsc();
			const int32 NModal = Count(6305);
			WM->ClearMsgBoxQueue(TEXT("самотест"));
			Caps->Reset();
			const bool bEsc = WM->CloseTopmostOnEsc();
			Expect(NModal == 0 && Count(6305) == 1 && S->IsSellWindowOpen(), TEXT("ESC над 22000 → 6305, окно ждёт 6306 ↔ под msgbox ESC съеден модалкой"),
				FString::Printf(TEXT("msgbox: съедено=%d 6305=%d; без: съедено=%d 6305=%d открыто=%d"), bEscModal ? 1 : 0, NModal,
					bEsc ? 1 : 0, Count(6305), S->IsSellWindowOpen() ? 1 : 0));
			TArray<uint8> R = Zero(452); R[0] = 1; Feed(Net, S, 6303, R);
			// ★ Волна Л3 (реестр Л1-2 закрыт): «свой и состояние 6 → 6305 раз в 4 с» живёт в рендере СВОЕЙ вывески
			//   (`UAsdaStallSignSubsystem::RenderSigns`, эталон `Chat_RenderHeadBubble`). Вывеска есть, когда пришёл 6304 «открыта»
			//   (сервер шлёт его сектору, и себе тоже, сразу за 6303 — GameServer.c:100286) — подаём штатным приёмом.
			UAsdaStallSignSubsystem* Sign = GI->GetSubsystem<UAsdaStallSignSubsystem>();
			TArray<uint8> P6304 = Zero(56); P6304[0] = 1; AsdaBytes::WrI32(P6304.GetData() + 2, 424242);
			FAsdaLoginData::EncodeCp1251(TEXT("Лавка"), P6304.GetData() + 6, 49);
			if (Sign)
			{
				Sign->ResetSessionState(EAsdaSessionResetReason::MapChanged);
				AsdaTestNet::DebugFeedPacket(Net, 6304, P6304);
				Sign->PumpPending();
				Sign->UpdateNameStatusList(true);
				Sign->UpdateStalls(true);
			}
			Caps->Reset();
			if (Sign) { Sign->DebugExpireTimers(); Sign->RenderSigns(); }
			S->Tick(0.f);
			const int32 NAlive = Count(6305);
			const bool bDeadWas = AsdaTestNet::bSelfDead(Net);
			AsdaTestNet::bSelfDead(Net) = true;
			if (Sign) { Sign->DebugExpireTimers(); Sign->RenderSigns(); }
			S->Tick(0.f);
			const int32 NDead = Count(6305);
			if (Sign) { Sign->RenderSigns(); }   // троттл 4 с
			S->Tick(0.f);
			const int32 NDead2 = Count(6305);
			AsdaTestNet::bSelfDead(Net) = bDeadWas;
			Expect(Sign && Sign->DebugSignWidget(17) && NAlive == 0 && NDead == 1 && NDead2 == 1,
				TEXT("смерть при витрине хозяина → 6305 (раз в 4 с, рендер своей вывески — Л3) ↔ жив — нет"),
				FString::Printf(TEXT("вывеска=%d жив=%d мёртв=%d повтор=%d"), (Sign && Sign->DebugSignWidget(17)) ? 1 : 0, NAlive, NDead, NDead2));
			if (Sign) { Sign->ResetSessionState(EAsdaSessionResetReason::MapChanged); }
			Caps->Reset();
			S->ResetSessionState(EAsdaSessionResetReason::MapChanged);
			Expect(!S->IsShowcaseOpen() && !S->IsSellWindowOpen() && !UAsdaTradeStateSubsystem::IsSelfStoreOpen(GI) && Count(6305) == 0 && Count(4024) == 0,
				TEXT("сброс сессии (смена карты): окна скрыты БЕЗ 6305/4024, признаки сняты"), FString::Printf(TEXT("6305=%d 4024=%d"), Count(6305), Count(4024)));
		}

		// ═══ 9. Канал отказов 5451 (ревизия Л1, СРЕДН-3): плашка 221 цветом −11514 (#FFD306), 10 с; 0x19 — плашка + чат + msgbox;
		//   0x1A — только если уровень вне диапазона предмета. Приём — штатный `DebugFeedPacket(5451)` → очередь инвентаря. ═══
		{
			using namespace AsdaStoreTestLocal;
			FMsgSink Sink;
			GLog->AddOutputDevice(&Sink);
			auto Feed5451 = [Net, Inv](uint8 Result, int32 Code)
			{
				TArray<uint8> B = Zero(68); B[0] = Result; AsdaBytes::WrI32(B.GetData() + 3, Code);
				AsdaTestNet::DebugFeedPacket(Net, 5451, B);
				Inv->DebugPumpPendingPackets();
			};
			auto T = [GI](int32 Id) { return AsdaNotice::Loc(GI, Id); };
			AsdaTestNet::SelfLevel(Net) = 30;
			WM->ClearMsgBoxQueue(TEXT("самотест"));
			Sink.Reset(); Feed5451(0x1B, 343);
			const int32 P1 = Sink.Hits(T(107090), { TEXT("WARMESSAGE221"), TEXT("#FFD306"), TEXT("TTL=10000") });
			const int32 C1 = Sink.Hits(T(107090), { TEXT("сисчат") });
			Sink.Reset(); Feed5451(0x1C, 343);
			const int32 P2 = Sink.Hits(T(107091), { TEXT("WARMESSAGE221"), TEXT("#FFD306") });
			Sink.Reset(); Feed5451(0x02, 343);
			const int32 P3 = Sink.Hits(T(12838), { TEXT("WARMESSAGE221"), TEXT("#FFD306") });
			const int32 C3 = Sink.Hits(T(12838), { TEXT("сисчат") });
			Expect(P1 == 1 && C1 == 0 && P2 == 1 && P3 == 1 && C3 == 0 && WM->GetMsgBoxQueueNum() == 0,
				TEXT("5451 отказы 0x1B/0x1C/0x02 → плашка 221 #FFD306 10 с, без строки сисчата и без msgbox"),
				FString::Printf(TEXT("107090 плашка=%d чат=%d; 107091 плашка=%d; 12838 плашка=%d чат=%d; msgbox=%d"), P1, C1, P2, P3, C3, WM->GetMsgBoxQueueNum()));
			Sink.Reset(); Feed5451(0x19, 343);
			const int32 P4 = Sink.Hits(T(2254), { TEXT("WARMESSAGE221"), TEXT("#FFD306") });
			const int32 C4 = Sink.Hits(T(2254), { TEXT("сисчат"), TEXT("#FFD306") });
			const bool M4 = TopMsgIs(GI, WM, 2254);
			Expect(P4 == 1 && C4 == 1 && M4, TEXT("5451 0x19 (контроль) → плашка + строка сисчата (#FFD306) + msgbox 2254"),
				FString::Printf(TEXT("плашка=%d чат=%d msgbox=%d"), P4, C4, M4 ? 1 : 0));
			WM->ClearMsgBoxQueue(TEXT("самотест"));
			Sink.Reset(); Feed5451(0x1A, 20664);   // item_list 20664: уровни 45…150, свой 30 — вне
			const int32 P5 = Sink.Hits(T(12857), { TEXT("WARMESSAGE221"), TEXT("#FFD306") });
			Sink.Reset(); Feed5451(0x1A, 20551);   // золото: 1…150 — в диапазоне
			const int32 P6 = Sink.Hits(T(12857), { TEXT("WARMESSAGE221") });
			Expect(P5 == 1 && P6 == 0, TEXT("5451 0x1A: уровень вне диапазона → плашка 12857 ↔ в диапазоне — молча"),
				FString::Printf(TEXT("вне=%d в=%d"), P5, P6));
			// ★ 24.10.2026 [ИСПРАВЛЕНИЕ] формата отката (asda-ключ формата нотиса = 1): остаток неизвестен → законченная фраза 9558;
			//   откат заведён → «<8114 без хвостового двоеточия>: N <6824>» (RU «Откат: 30 сек.», EN «Cooling time remains: 30 Seconds»).
			Sink.Reset(); Feed5451(0x08, 343);
			const int32 P7 = Sink.Hits(T(9558), { TEXT("WARMESSAGE221"), TEXT("#FFD306") });
			const int32 C7 = Sink.Hits(T(9558), { TEXT("сисчат") });
			Expect(P7 == 1 && C7 == 0, TEXT("5451 0x08, остаток неизвестен → плашка 9558 (законченная фраза), не чат"),
				FString::Printf(TEXT("плашка=%d чат=%d «%s»"), P7, C7, *T(9558)));
			if (UAsdaHotbarSubsystem* Hb = GI->GetSubsystem<UAsdaHotbarSubsystem>())
			{
				FString Head = T(8114);
				Head.TrimEndInline();
				while (Head.EndsWith(TEXT(":"))) { Head.LeftChopInline(1); Head.TrimEndInline(); }
				const FString Want = FString::Printf(TEXT("%s: 30 %s"), *Head, *T(6824));
				Hb->PushItemCooldownRaw(343, -1, -1, 0, 30000);
				Sink.Reset(); Feed5451(0x08, 343);
				const int32 P8 = Sink.Hits(Want, { TEXT("WARMESSAGE221"), TEXT("#FFD306"), *Want });
				Sink.Reset(); Feed5451(0x0C, 343);
				const int32 P9 = Sink.Hits(Want, { TEXT("WARMESSAGE221"), *Want });
				Hb->ClearItemCooldownByCode(343);
				Expect(P8 == 1 && P9 == 1 && !Want.Contains(TEXT(": :")) && !Head.IsEmpty(),
					TEXT("5451 0x08/0x0C, откат заведён (30 с) → плашка «<8114>: 30 <6824>» с разделителем"),
					FString::Printf(TEXT("0x08=%d 0x0C=%d «%s»"), P8, P9, *Want));
			}
			else { Expect(false, TEXT("5451 0x08: подсистема панели (откаты) есть"), TEXT("UAsdaHotbarSubsystem нет")); }
			GLog->RemoveOutputDevice(&Sink);
		}

		// Восстановление полей сети (окна для геометрии открываются вызывающим).
		AsdaTestNet::SelfLevel(Net) = LevelWas;
		AsdaTestNet::SelectedCharId(Net) = CharIdWas;
		WM->ClearMsgBoxQueue(TEXT("самотест лавки"));
	}

	// ═══ ФАЗА «ПУТЁМ ИГРОКА» (ревизия Л1, СРЕДН-2) — шаги с паузами, настоящие события Slate (`SimulateClickAt` ЛКМ/ПКМ по
	//   живой геометрии), колесо через реестр окна, опрос полей в Tick, кнопки через реле, входы 207 / 65 / доп-меню 2. ═══
	TArray<TFunction<void(UWorld*)>> Steps;   // шаги фазы (по одному за тик планировщика)

	// Центр виджета окна 22000/23000 по имени (абсолютные координаты Slate); false — нет виджета/геометрии.
	static bool Center(UUserWidget* Root, const TCHAR* Name, FVector2D& Out)
	{
		UWidget* W = Root ? Root->GetWidgetFromName(FName(Name)) : nullptr;
		FVector2D Mn, Mx;
		if (!W || !AsdaUi::WidgetAbsRect(W, Mn, Mx)) { return false; }
		Out = (Mn + Mx) * 0.5f;
		return true;
	}
	// Заказать настоящий клик по виджету окна.
	bool Click(UWorld* W, bool bShop, const TCHAR* Name, bool bRight = false)
	{
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaSelfStoreSubsystem* S = GI ? GI->GetSubsystem<UAsdaSelfStoreSubsystem>() : nullptr;
		UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
		FVector2D C;
		if (!S || !WM || !Center(bShop ? S->DebugShopWidget() : S->DebugSellWidget(), Name, C))
		{
			Expect(false, FString::Printf(TEXT("клик по %s"), Name), TEXT("нет живой геометрии"));
			return false;
		}
		WM->SimulateClickAt(C, /*HoldFrames*/ 3, bRight);
		return true;
	}

	void BuildInputPhase()
	{
		// Состояние, общее между шагами.
		struct FShared { int32 Rows0 = 0; int32 Pos0 = 0; FString Price0; };
		TSharedRef<FShared> Sh = MakeShared<FShared>();
		auto Ctx = [](UWorld* W, UGameInstance*& GI, UAsdaSelfStoreSubsystem*& S, UAsdaNetSubsystem*& Net, UAsdaWindowSubsystem*& WM)
		{
			GI = W ? W->GetGameInstance() : nullptr;
			S = GI ? GI->GetSubsystem<UAsdaSelfStoreSubsystem>() : nullptr;
			Net = GI ? AsdaTestNet::Get(GI) : nullptr;
			WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
			return GI && S && Net && WM;
		};
		// 0. Входы путём игрока: функция 2/7 (эмоция 207), хоткей 65 (`ExecuteCommand`), доп-меню cmd 2 → по одному 6300.
		Steps.Add([this, Ctx](UWorld* W)
		{
			UGameInstance* GI; UAsdaSelfStoreSubsystem* S; UAsdaNetSubsystem* Net; UAsdaWindowSubsystem* WM;
			if (!Ctx(W, GI, S, Net, WM)) { return; }
			AsdaTestNet::SelectedCharId(Net) = 424242; AsdaTestNet::SetSelfUserNumForDebug(Net, 17); AsdaTestNet::SelfLevel(Net) = 30;
			S->ResetSessionState(EAsdaSessionResetReason::MapChanged);
			UAsdaInventorySubsystem* Inv = GI->GetSubsystem<UAsdaInventorySubsystem>();
			for (int32 i = 0; i < 60; ++i) { Inv->DebugPutItem(2, i, 0, 0); }
			Inv->DebugPutItem(2, 0, 20592, 7);
			static const int32 kCodes[] = { 20571, 20572, 20573, 20577, 20578, 20579, 20583, 20586, 20587, 20588, 20589, 20590 };
			for (int32 i = 1; i < 36; ++i) { Inv->DebugPutItem(2, i, kCodes[(i - 1) % UE_ARRAY_COUNT(kCodes)], 3); }
			WM->ClearMsgBoxQueue(TEXT("самотест"));
			int32 N[3] = { 0, 0, 0 };
			Caps->Reset();
			if (UAsdaCharaInfoSubsystem* CI = GI->GetSubsystem<UAsdaCharaInfoSubsystem>()) { CI->PlayEmotion(UAsdaCharaInfoSubsystem::MakeEmotionCode(2, 7)); }
			N[0] = Count(6300); Caps->Reset();
			if (AAsdaPlayerController* Pc = Cast<AAsdaPlayerController>(W->GetFirstPlayerController())) { Pc->RunCommandForCheck(65); }
			N[1] = Count(6300); Caps->Reset();
			if (UAsdaExtraMenuSubsystem* EM = GI->GetSubsystem<UAsdaExtraMenuSubsystem>()) { EM->DispatchCommand(2, TEXT("самотест: доп-меню")); }
			N[2] = Count(6300); Caps->Reset();
			Expect(N[0] == 1 && N[1] == 1 && N[2] == 1, TEXT("путь игрока: эмоция 207 / клавиша действия 65 / доп-меню cmd 2 → по одному 6300"),
				FString::Printf(TEXT("207=%d 65=%d доп-меню=%d"), N[0], N[1], N[2]));
			// Открыть окно штатным приёмом (кадр отрисуется до следующего шага).
			TArray<uint8> One; One.Add(1);
			AsdaTestNet::DebugFeedPacket(Net, 6301, One); S->PumpPending();
		});
		// 1. ЛКМ по ячейке со стопкой 7 → нумпад, строки нет.
		Steps.Add([this](UWorld* W) { Click(W, false, TEXT("Stall_Open_Inven00_52_0")); });
		Steps.Add([this, Ctx, Sh](UWorld* W)
		{
			UGameInstance* GI; UAsdaSelfStoreSubsystem* S; UAsdaNetSubsystem* Net; UAsdaWindowSubsystem* WM;
			if (!Ctx(W, GI, S, Net, WM)) { return; }
			const bool bNum = WM->IsNumPadOpen();
			Expect(bNum && S->GetSellCount() == 0, TEXT("путь игрока: ЛКМ по ячейке (стопка 7) → нумпад режим 7, строки нет"),
				FString::Printf(TEXT("нумпад=%d строк=%d"), bNum ? 1 : 0, S->GetSellCount()));
			if (bNum) { WM->CloseNumPad(false); }
		});
		// 2. ПКМ по той же ячейке → вся стопка без нумпада (латч ПКМ `Mouse.bRmbDown`, событие 0xE).
		Steps.Add([this](UWorld* W) { Click(W, false, TEXT("Stall_Open_Inven00_52_0"), /*bRight*/ true); });
		Steps.Add([this, Ctx](UWorld* W)
		{
			UGameInstance* GI; UAsdaSelfStoreSubsystem* S; UAsdaNetSubsystem* Net; UAsdaWindowSubsystem* WM;
			if (!Ctx(W, GI, S, Net, WM)) { return; }
			Expect(!WM->IsNumPadOpen() && S->GetSellCount() == 1 && S->DebugRowText(0, 1) == TEXT("7"),
				TEXT("путь игрока: ПКМ по ячейке (контроль) → вся стопка 7 без нумпада"),
				FString::Printf(TEXT("нумпад=%d строк=%d кол-во «%s»"), WM->IsNumPadOpen() ? 1 : 0, S->GetSellCount(), *S->DebugRowText(0, 1)));
		});
		// 3. Колесо: курсор над сеткой → позиция 1 ↔ курсор над «OK» (вне сетки) → колесо окну не нужно, позиция прежняя.
		Steps.Add([this, Ctx](UWorld* W)
		{
			UGameInstance* GI; UAsdaSelfStoreSubsystem* S; UAsdaNetSubsystem* Net; UAsdaWindowSubsystem* WM;
			if (!Ctx(W, GI, S, Net, WM)) { return; }
			FVector2D Cg, Co;
			const bool bG = Center(S->DebugSellWidget(), TEXT("Stall_Open_Inven07_59_0"), Cg);
			const bool bO = Center(S->DebugSellWidget(), TEXT("Stall_Open_OKBtn"), Co);
			if (!bG || !bO) { Expect(false, TEXT("колесо"), TEXT("нет геометрии")); return; }
			FSlateApplication::Get().SetCursorPos(Co);
			const bool bOut = WM->HandleMouseWheel(-1);
			const int32 PosOut = S->GetScrollPos();
			FSlateApplication::Get().SetCursorPos(Cg);
			const bool bIn = WM->HandleMouseWheel(-1);
			const int32 PosIn = S->GetScrollPos();
			Expect(PosOut == 0 && bIn && PosIn == 1, TEXT("путь игрока: колесо над сеткой → позиция 1 ↔ над «OK» → позиция 0"),
				FString::Printf(TEXT("вне: съедено=%d поз=%d; над сеткой: съедено=%d поз=%d"), bOut ? 1 : 0, PosOut, bIn ? 1 : 0, PosIn));
			FSlateApplication::Get().SetCursorPos(Cg);
			WM->HandleMouseWheel(+1);
		});
		// 4. Ввод в поле цены → опрос в Tick → потолок 1 млрд ↔ обычное число.
		Steps.Add([this, Ctx](UWorld* W)
		{
			UGameInstance* GI; UAsdaSelfStoreSubsystem* S; UAsdaNetSubsystem* Net; UAsdaWindowSubsystem* WM;
			if (!Ctx(W, GI, S, Net, WM)) { return; }
			UEditableText* Pe = S->DebugSellWidget() ? Cast<UEditableText>(S->DebugSellWidget()->GetWidgetFromName(TEXT("Stall_Open_SellPrice00"))) : nullptr;
			if (Pe) { Pe->SetText(FText::FromString(TEXT("5,000,000,000"))); }
		});
		Steps.Add([this, Ctx, Sh](UWorld* W)
		{
			UGameInstance* GI; UAsdaSelfStoreSubsystem* S; UAsdaNetSubsystem* Net; UAsdaWindowSubsystem* WM;
			if (!Ctx(W, GI, S, Net, WM)) { return; }
			Sh->Price0 = S->DebugRowText(0, 0);
			UEditableText* Pe = S->DebugSellWidget() ? Cast<UEditableText>(S->DebugSellWidget()->GetWidgetFromName(TEXT("Stall_Open_SellPrice00"))) : nullptr;
			if (Pe) { Pe->SetText(FText::FromString(TEXT("4321"))); }
		});
		Steps.Add([this, Ctx, Sh](UWorld* W)
		{
			UGameInstance* GI; UAsdaSelfStoreSubsystem* S; UAsdaNetSubsystem* Net; UAsdaWindowSubsystem* WM;
			if (!Ctx(W, GI, S, Net, WM)) { return; }
			Expect(Sh->Price0 == TEXT("1,000,000,000") && S->DebugRowText(0, 0) == TEXT("4,321"),
				TEXT("путь игрока: ввод в поле цены, опрос Tick → «5 млрд» прижато к 1,000,000,000 ↔ «4321» → 4,321"),
				FString::Printf(TEXT("«%s» / «%s»"), *Sh->Price0, *S->DebugRowText(0, 0)));
		});
		// 5. Кнопка ▲ цены (22302) настоящим кликом → +5 % базы (2100 → шаг 105).
		Steps.Add([this](UWorld* W) { Click(W, false, TEXT("Stall_Open_SellBtn00_0")); });
		Steps.Add([this, Ctx](UWorld* W)
		{
			UGameInstance* GI; UAsdaSelfStoreSubsystem* S; UAsdaNetSubsystem* Net; UAsdaWindowSubsystem* WM;
			if (!Ctx(W, GI, S, Net, WM)) { return; }
			Expect(S->DebugRowText(0, 0) == TEXT("4,426"), TEXT("путь игрока: клик ▲ 22302 → 4,321 + 105 = 4,426"), S->DebugRowText(0, 0));
		});
		// 6. Вкладка «Снаряжение» 22200 (радио `_OPT1`) → вкладка 1 ↔ `_OPT0` → 0.
		Steps.Add([this](UWorld* W) { Click(W, false, TEXT("Stall_Open_InvenRadioBtn_OPT1")); });
		Steps.Add([this, Ctx, Sh](UWorld* W)
		{
			UGameInstance* GI; UAsdaSelfStoreSubsystem* S; UAsdaNetSubsystem* Net; UAsdaWindowSubsystem* WM;
			if (!Ctx(W, GI, S, Net, WM)) { return; }
			Sh->Pos0 = S->GetSourceTab();
			Click(W, false, TEXT("Stall_Open_InvenRadioBtn_OPT0"));
		});
		Steps.Add([this, Ctx, Sh](UWorld* W)
		{
			UGameInstance* GI; UAsdaSelfStoreSubsystem* S; UAsdaNetSubsystem* Net; UAsdaWindowSubsystem* WM;
			if (!Ctx(W, GI, S, Net, WM)) { return; }
			Expect(Sh->Pos0 == 1 && S->GetSourceTab() == 0, TEXT("путь игрока: радио 22200 → вкладка 1 ↔ вкладка 0"),
				FString::Printf(TEXT("после OPT1=%d после OPT0=%d"), Sh->Pos0, S->GetSourceTab()));
			UEditableText* Ne = S->DebugSellWidget() ? Cast<UEditableText>(S->DebugSellWidget()->GetWidgetFromName(TEXT("Stall_Open_NameInput"))) : nullptr;
			if (Ne) { Ne->SetText(FText::FromString(TEXT("Путь игрока"))); }
			Click(W, false, TEXT("Stall_Open_NameSave"));
		});
		// 7. «Изменить» названия 22012 → название; «OK» 22500 → 6302.
		Steps.Add([this, Ctx](UWorld* W)
		{
			UGameInstance* GI; UAsdaSelfStoreSubsystem* S; UAsdaNetSubsystem* Net; UAsdaWindowSubsystem* WM;
			if (!Ctx(W, GI, S, Net, WM)) { return; }
			Expect(S->GetStoreTitle() == TEXT("Путь игрока"), TEXT("путь игрока: клик 22012 «Изменить» → название"), S->GetStoreTitle());
			Caps->Reset();
			Click(W, false, TEXT("Stall_Open_OKBtn"));
		});
		Steps.Add([this, Ctx](UWorld* W)
		{
			UGameInstance* GI; UAsdaSelfStoreSubsystem* S; UAsdaNetSubsystem* Net; UAsdaWindowSubsystem* WM;
			if (!Ctx(W, GI, S, Net, WM)) { return; }
			Expect(Count(6302) == 1, TEXT("путь игрока: клик «OK» 22500 → 6302"), FString::Printf(TEXT("6302=%d"), Count(6302)));
			Caps->Reset();
			Click(W, false, TEXT("Stall_Open_CancelBtn"));
		});
		// 8. «Отмена» 22501 и крестик 22004 → 6305 (окно ждёт 6306).
		Steps.Add([this, Ctx](UWorld* W)
		{
			UGameInstance* GI; UAsdaSelfStoreSubsystem* S; UAsdaNetSubsystem* Net; UAsdaWindowSubsystem* WM;
			if (!Ctx(W, GI, S, Net, WM)) { return; }
			const int32 NCancel = Count(6305);
			Caps->Reset();
			Click(W, false, TEXT("Stall_Open_ClsBtn"));
			Expect(NCancel == 1 && S->IsSellWindowOpen(), TEXT("путь игрока: клик «Отмена» 22501 → 6305, окно ждёт 6306"), FString::Printf(TEXT("6305=%d"), NCancel));
		});
		Steps.Add([this, Ctx](UWorld* W)
		{
			UGameInstance* GI; UAsdaSelfStoreSubsystem* S; UAsdaNetSubsystem* Net; UAsdaWindowSubsystem* WM;
			if (!Ctx(W, GI, S, Net, WM)) { return; }
			Expect(Count(6305) == 1, TEXT("путь игрока: клик крестика 22004 → 6305"), FString::Printf(TEXT("6305=%d"), Count(6305)));
			// Витрина хозяина штатным приёмом 6303/1.
			TArray<uint8> R; R.SetNumZeroed(452); R[0] = 1; R[1] = 1;
			AsdaBytes::WrI32(R.GetData() + 2, 20592); R[6] = 2; AsdaBytes::WrI16(R.GetData() + 7, 0); AsdaBytes::WrI32(R.GetData() + 9, 7); AsdaBytes::WrI32(R.GetData() + 15, 1000);
			AsdaTestNet::DebugFeedPacket(Net, 6303, R); S->PumpPending();
			Caps->Reset();
		});
		// 9. Витрина: «Изменить» 23305 → 6305 + 6300.
		Steps.Add([this](UWorld* W) { Click(W, true, TEXT("Stall_EditBtn")); });
		Steps.Add([this, Ctx](UWorld* W)
		{
			UGameInstance* GI; UAsdaSelfStoreSubsystem* S; UAsdaNetSubsystem* Net; UAsdaWindowSubsystem* WM;
			if (!Ctx(W, GI, S, Net, WM)) { return; }
			Expect(Count(6305) == 1 && Count(6300) == 1 && S->IsReopenPending(), TEXT("путь игрока: клик 23305 «Изменить» → 6305 + 6300"),
				FString::Printf(TEXT("6305=%d 6300=%d"), Count(6305), Count(6300)));
			Caps->Reset();
			Click(W, true, TEXT("Stall_ClsBtn1"));
		});
		// 10. Витрина: «Отмена» 23306 и крестик 23005 → 6305.
		Steps.Add([this, Ctx](UWorld* W)
		{
			UGameInstance* GI; UAsdaSelfStoreSubsystem* S; UAsdaNetSubsystem* Net; UAsdaWindowSubsystem* WM;
			if (!Ctx(W, GI, S, Net, WM)) { return; }
			const int32 N1 = Count(6305);
			Caps->Reset();
			Click(W, true, TEXT("Stall_ClsBtn0"));
			Expect(N1 == 1 && S->IsOwnerShowcaseOpen(), TEXT("путь игрока: клик 23306 «Отмена» → 6305, витрина ждёт 6306"), FString::Printf(TEXT("6305=%d"), N1));
		});
		Steps.Add([this, Ctx](UWorld* W)
		{
			UGameInstance* GI; UAsdaSelfStoreSubsystem* S; UAsdaNetSubsystem* Net; UAsdaWindowSubsystem* WM;
			if (!Ctx(W, GI, S, Net, WM)) { return; }
			Expect(Count(6305) == 1, TEXT("путь игрока: клик крестика 23005 → 6305"), FString::Printf(TEXT("6305=%d"), Count(6305)));
			// Окна для геометрии: витрина уже открыта; 22000 — штатным 6301/1 (переоткрытие хранит список).
			TArray<uint8> One; One.Add(1);
			AsdaTestNet::DebugFeedPacket(Net, 6301, One); S->PumpPending();
		});
	}

	// Снимок сумки и расходников (повторный вызов ничего не делает — первое состояние главное).
	void SaveBags(const UAsdaInventorySubsystem* Inv)
	{
		if (!Inv || SavedBag.Num() > 0) { return; }
		for (int32 I = 0; I < UAsdaInventorySubsystem::kShellSlots; ++I) { const FAsdaItemInstance* X = Inv->GetCell(1, I); SavedBag.Add(X && !X->IsEmpty() ? X->Raw : TArray<uint8>()); }
		for (int32 I = 0; I < UAsdaInventorySubsystem::kSpendSlots; ++I) { const FAsdaItemInstance* X = Inv->GetCell(2, I); SavedSpend.Add(X && !X->IsEmpty() ? X->Raw : TArray<uint8>()); }
	}
	// Вернуть сумку и расходники как до прогона (тем же путём, что `asda.Craft.SelfTestDisassembly`).
	void RestoreBags(UAsdaInventorySubsystem* Inv)
	{
		if (!Inv) { return; }
		for (int32 I = 0; I < SavedBag.Num(); ++I)
		{
			if (SavedBag[I].Num() >= 57) { Inv->ApplyExternalRecord(SavedBag[I], UAsdaInventorySubsystem::EExternalRecordMode::AsIs); } else { Inv->ClearCell(1, I); }
		}
		for (int32 I = 0; I < SavedSpend.Num(); ++I)
		{
			if (SavedSpend[I].Num() >= 57) { Inv->ApplyExternalRecord(SavedSpend[I], UAsdaInventorySubsystem::EExternalRecordMode::AsIs); } else { Inv->ClearCell(2, I); }
		}
		SavedBag.Reset(); SavedSpend.Reset();
	}

	// Снять свой перехватчик (доступ к приватному `DisarmSendCapture` — через `friend struct FAsdaSelfStoreL1SelfTest`).
	void Disarm(UAsdaNetSubsystem* Net) { if (Net) { AsdaSelfTestKit::DisarmCapture(Net, Gen); } }

	// Геометрия путём игрока (кадр уже отрисован): позиции ключевых узлов относительно левого-верхнего плана рамки против `.JUI`.
	void CheckGeometry(UWorld* W)
	{
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaSelfStoreSubsystem* S = GI ? GI->GetSubsystem<UAsdaSelfStoreSubsystem>() : nullptr;
		if (!S) { return; }
		struct FPt { const TCHAR* Name; float X, Y, Wd, Ht; };
		auto Check = [this](UUserWidget* Root, const TCHAR* Frame, float FrameW, TArrayView<const FPt> Pts, const TCHAR* Win)
		{
			if (!Root || !Root->WidgetTree) { Expect(false, FString::Printf(TEXT("геометрия %s"), Win), TEXT("окна нет")); return; }
			TMap<FString, UWidget*> ByName;
			TArray<UWidget*> All; Root->WidgetTree->GetAllWidgets(All);
			for (UWidget* X : All) { if (X) { ByName.Add(X->GetName(), X); } }
			UWidget* const* F0 = ByName.Find(FString::Printf(TEXT("%s_0_0"), Frame));
			UWidget* const* F2 = ByName.Find(FString::Printf(TEXT("%s_0_2"), Frame));
			if (!F0 || !F2) { Expect(false, FString::Printf(TEXT("геометрия %s"), Win), TEXT("планов рамки нет")); return; }
			const FGeometry G0 = (*F0)->GetCachedGeometry();
			const FGeometry G2 = (*F2)->GetCachedGeometry();
			const FVector2D O = G0.GetAbsolutePosition();
			const float RightAbs = G2.GetAbsolutePosition().X + G2.GetAbsoluteSize().X;
			const float Scale = (RightAbs - O.X) / FrameW;
			float MaxD = 0.f; FString Det;
			for (const FPt& P : Pts)
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
			Expect(Scale > 0.1f && MaxD <= 1.5f, FString::Printf(TEXT("геометрия %s путём игрока (масштаб %.3f, Δ ≤ 1,5 px)"), Win, Scale), Det);
		};
		// `.JUI` (w0/jui_tree.txt): отн. позиция и размер относительно корня окна.
		static const FPt kSell[] = {
			{ TEXT("Stall_Open_OKBtn"), 378, 471, 100, 23 }, { TEXT("Stall_Open_NameSave"), 518, 43, 60, 20 },
			{ TEXT("Stall_Open_SellPrice00"), 52, 120, 90, 20 }, { TEXT("Stall_Open_ClsBtn"), 569, 6, 18, 18 } };
		static const FPt kShop[] = {
			{ TEXT("Stall_EditBtn"), 120, 392, 100, 23 }, { TEXT("Stall_ClsBtn1"), 225, 392, 100, 23 }, { TEXT("Stall_ClsBtn0"), 415, 6, 18, 18 } };
		Check(S->DebugSellWidget(), TEXT("StreetStall_Open_Win"), 593.f, kSell, TEXT("22000"));
		Check(S->DebugShopWidget(), TEXT("StreetStall_Win"), 438.f, kShop, TEXT("23000"));
	}
};

namespace
{
	// Разбор `delay=N`.
	// ★ R5-fix-c (13.11.2026): своя `ParseStoreDelay` (дословная копия у двенадцати самотестов) убрана — разбор `delay=N` в общей точке
	//   `AsdaSelfTestKit::DelayArg` (`Test/AsdaSelfTestKit.h`), вызовы ниже идут в неё.

	FAutoConsoleCommandWithWorldAndArgs GAsdaTradeSelfTestStoreL1(
		TEXT("asda.Trade.SelfTestStoreL1"),
		TEXT("asda.Trade.SelfTestStoreL1 [delay=N] [quit] — самотест продавца личной лавки (волна Л1): входы и гейты 6300, приём 6301/6303/6306 "
		     "штатным путём, 6302/6305/4024 под перехватом, окно 22000 (сетка, нумпад 7, цены, потолок 1 млрд), витрина хозяина 23000, "
		     "«Изменить», билет, К-1/К-3/К-16, ESC, смерть, сброс сессии, геометрия путём игрока. Только офлайн."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
		{
			const FAsdaSelfTestArgs KitArgs = FAsdaSelfTestArgs::Parse(Args);   // ★ 11.10.2026 (В3 S1): каркас — отказ с ИТОГ и quit, второй барьер на все опкоды
			if (AsdaSelfTestKit::RefuseStart(W, TEXT("asda.Trade.SelfTestStoreL1"), KitArgs)) { return; }
			const float Delay = AsdaSelfTestKit::DelayArg(Args);
			const bool bQuit = Args.Contains(TEXT("quit"));
			TWeakObjectPtr<UWorld> WW(W);
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WW, bQuit](float) -> bool
			{
				UWorld* Wl = WW.Get();
				if (!Wl) { return false; }
				if (AsdaSelfTestKit::RefuseDeferred(Wl, TEXT("asda.Trade.SelfTestStoreL1"), bQuit)) { return false; }   // Р-2: офлайн повторно — на отложенном старте
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
					TSharedRef<FAsdaSelfStoreL1SelfTest> T = MakeShared<FAsdaSelfStoreL1SelfTest>();
					T->Run(W2);
					T->BuildInputPhase();
					// Шаги фазы «путём игрока» — по одному раз в 0,4 с (клику нужно ≥ 3 кадров удержания + кадр опроса окна).
					TSharedRef<int32> Idx = MakeShared<int32>(0);
					FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WW, T, Idx, bQuit](float) -> bool
					{
						UWorld* W3 = WW.Get();
						if (!W3) { AsdaCVarSave::RestoreAll(TEXT("asda.Trade.SelfTestStoreL1")); return false; }   // ★ ИСПРАВЛЕНИЕ 11.10.2026 (В0, N1(д)/U8): ключи — и при уходе мира
						if (*Idx < T->Steps.Num()) { T->Steps[(*Idx)++](W3); return true; }
						T->CheckGeometry(W3);
						UGameInstance* GI3 = W3->GetGameInstance();
						if (UAsdaNetSubsystem* Net3 = GI3 ? AsdaTestNet::Get(GI3) : nullptr) { T->Disarm(Net3); }
						if (UAsdaSelfStoreSubsystem* S3 = GI3 ? GI3->GetSubsystem<UAsdaSelfStoreSubsystem>() : nullptr)
						{
							S3->ResetSessionState(EAsdaSessionResetReason::MapChanged);
						}
						T->RestoreBags(GI3 ? GI3->GetSubsystem<UAsdaInventorySubsystem>() : nullptr);   // ★ 31.10.2026: сумка — как до прогона
						// ★ ИСПРАВЛЕНИЕ 11.10.2026 (В0, N1(д)/U8): ключ Trade.StoreFixK1 — к исходному значению И приоритету SetBy (прежде шаги
						//   возвращали константу 1 с SetByCode).
						AsdaCVarSave::RestoreAll(TEXT("asda.Trade.SelfTestStoreL1"));
						UE_LOG(LogAsdaStoreTest, Log, TEXT("САМОТЕСТ-ЛАВКА ИТОГ: ok=%d fail=%d"), T->Ok, T->Fail);
						T->SelfScope.Restore();   // ★ В3 S1 (N1(г)): «свой» — как до прогона
						AsdaSelfTestKit::Quit(W3, bQuit);
						return false;
					}), 0.4f);
					return false;
				}), bNeedSpawn ? 2.f : 0.f);
				return false;
			}), Delay);
			UE_LOG(LogAsdaStoreTest, Log, TEXT("САМОТЕСТ-ЛАВКА: запуск через %.1f с"), Delay);
		}));
}

#endif // !UE_BUILD_SHIPPING
