// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ: КНОПКА «РАСШИРИТЬ КЛАН» У ЭЛЛЫ (107) И ПРИЁМ 6099 (27.09.2026, отчёт `Reports/owner_0927/clan_btn_6099.md`). [ПОРТ]
//
// ★ ЧТО ПРОВЕРЯЕТ.
//   A. Путь игрока «разговор с NPC 107 → кнопка меню → окно 14450» — ТЕМ ЖЕ кодом, что мышь: `OpenForNpcId(107)` строит окно 570
//      из `DT_NpcNsrHeader`/`DT_NpcNsrButton`, нажатие идёт `OnDialogButton(584)` (кнопка меню 3 = `NPC_SHOP_BUT03`) →
//      `RunMenuButton` → `ExecuteCommand(30027)` → `UAsdaClanSubsystem::OpenIncreaseWindow`. Разметку окна читаем разбором
//      `DumpWindow` (эффективная видимость + подпись + код кнопки).
//      Контрольные пары: кнопки 0…2 Эллы остались на своих местах и с прежними кодами; скрытая кнопка данных («Долина Ночи»,
//      30031, теперь слот 5) по-прежнему СКРЫТА; без клана то же нажатие даёт отказ 12431 и окна НЕТ, с кланом — окно есть.
//   A2. ★ 27.09.2026 (`lostvalley_entry.md`) — кнопка 4 Эллы «В Забытую Лощину» (loc 991201 с 06.11, код 30037): видна, подпись из таблицы,
//      нажатие `OnDialogButton(585)` ТЕМ ЖЕ кодом, что мышь, отправляет РОВНО ОДИН 5477 со словом 5 (LostValley) — эталон
//      game_main.c:10111 `netSendReqWord_op5477(5)` — и закрывает окно. Отправка ловится перехватом исходящих
//      (`UAsdaPetSubsystem::BeginSendCapture`, в сеть не уходит ничего). Контроль: 30031 (скрытая «Долина Ночи») в окне НЕ показан.
//      Подпись поля 14454 окна = loc 12445 «- Новый лимит членов клана:» (правка владельца).
//   B. Приём 6099 через ШТАТНЫЙ разбор (`UAsdaNetSubsystem::DebugFeedPacket` → `HandlePacket`), вывод ловим в очереди окон-
//      сообщений (`IsMsgBoxQueued(0, 24)` + текст верхней записи). Ожидания заданы ЯВНО по эталону `netHandleTradeStatusNotice`
//      (network.c:3444-3495), а не вызовом проверяемого кода. Контрольные пары: код 7/255 и «свой персонаж не в мире» — молчат;
//      чужой userNum — 133 вместо 7502 на том же коде; тело 2 б — отброшено; повтор того же (0, 24) — не плодит окно.
//   C. ★ 27.09.2026 (`clan_btn_6099_fix.md`, МЕЛК-6 ревизии) — отступления вокруг 6098 закрыты, проверка тем же кодом, что кнопка:
//      без своего userNum 6098 НЕ уходит и окна нет (эталон `LABEL_133`) — контроль: с userNum уходит ровно один 6098 со словом
//      +0 = userNum; нехватка предметов — ОКНО (0, −1) с 98101 / 98102 (эталон `CItem_BuildTooltipText(…, 0, −1, …)`), не лента;
//      отказ сторожа 6098 — ОКНО (0, 24) с 7502, как отказ сервера у эталона.
//   D. ★ МЕЛК-4: ГЕОМЕТРИЯ путём игрока — окно Эллы открыто, через кадры (таймер 0,5 с) снимаются живые прямоугольники Slate
//      (`DumpWindow` → `AsdaUi::WidgetAbsRect` = `GetCachedGeometry`): 5 кнопок меню видимы, непустые, внутри
//      `NPC_Function_Box_grp` и попарно не пересекаются. Контроль: у Иана (NPC 3, MenuCount 3) кнопка 3 скрыта, кнопка 2 видна.
//   E. ★ МЕЛК-5: 6099 НЕ сбрасывает и НЕ продлевает троттлинг 3 с (эталон `this[27930]`). 6098 №1 в T0; в T0+~1,5 с — приём 6099 и
//      повторная отправка → НЕ уходит (сброс дал бы пакет); в T0+~3,4 с (≈1,9 с после 6099) → уходит (продление дало бы тишину).
//      Время сверяется по реальным часам (`FPlatformTime::Seconds` — ими считает троттлинг), а не только по таймеру мира.
//   Всё состояние (userNum, клан, очередь, окна) восстанавливается в конце.
//
// Консоль: `asda.Clan.BtnSelfTest [delay=N] [quit]`. Только офлайн (`FAsdaOutgoingGate::RefuseDebugWhenOnline`).
//   Прогон многоэтапный (D/E ждут кадры и секунды), `quit` — после последнего этапа.
//   Строки проверок — «САМОТЕСТ-6099», итог — «САМОТЕСТ-6099 ИТОГ: ok=… fail=…».
#include "CoreMinimal.h"
#include "Test/AsdaSelfTestCtx.h"   // ★ R5-c (12.11.2026): общая точка самотестов вместо своих копий помощников
#include "Test/AsdaSelfTestInput.h"   // ★ R5-c (12.11.2026): общая точка самотестов вместо своих копий помощников

#if !UE_BUILD_SHIPPING

#include "AsdaNetSubsystem.h"
// RefuseDebugWhenOnline — офлайн-гейт самотестов   ★ R3: комментарий снятого включения "AsdaOutgoingGate.h" (файл им не пользовался) — сохранён
#include "Test/AsdaSelfTestKit.h"   // ★ 11.10.2026 (В3 S1): общий каркас самотестов (аргументы, отказ, перехват, итог, выход)
#include "Npc/AsdaNpcDialogSubsystem.h"        // окно разговора 570: OpenForNpcId / OnDialogButton / DumpWindow / RequestEventExchange
#include "UI/AsdaClanSubsystem.h"              // окно 14450: IsIncreaseOpen / CloseIncreaseWindow
#include "Pet/AsdaPetSubsystem.h"              // BeginSendCapture / EndSendCapture — перехват исходящих (5477, 6098)
#include "UI/AsdaWindowSubsystem.h"            // очередь окон-сообщений: IsMsgBoxQueued / GetMsgBoxQueueTopText
#include "World/AsdaWorldSubsystem.h"          // GetSelfClanId — восстановить свой клан после проверки
#include "UI/AsdaNotice.h"                     // AsdaNotice::Loc — ожидаемый текст нотиса
#include "Text/AsdaLang.h"                     // ★ 06.11.2026: FAsdaLang::IsRu — ожидаемые подписи окна 14450 по языку сессии
#include "Components/TextBlock.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"                  // FPlatformTime::Seconds — часы троттлинга 6098
#include "Math/Box2D.h"                        // прямоугольники кнопок (этап D)
#include "TimerManager.h"
#include "UObject/UObjectIterator.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaClanBtn6099Test, Log, All);

namespace AsdaClanBtn6099TestLocal
{
	// Счётчики прогона.
	struct FRun
	{
		int32 Ok = 0, Fail = 0;   // пройдено / провалено
		// Одна проверка: печать и счёт.
		void Expect(bool b, const FString& What)
		{
			if (b) { ++Ok; UE_LOG(LogAsdaClanBtn6099Test, Log, TEXT("САМОТЕСТ-6099 ok   %s"), *What); }
			else   { ++Fail; UE_LOG(LogAsdaClanBtn6099Test, Error, TEXT("САМОТЕСТ-6099 FAIL %s"), *What); }
		}
	};

	// Общий контекст многоэтапного прогона (живёт между таймерами).
	struct FCtx
	{
		FRun R;                                   // счётчики
		TWeakObjectPtr<UWorld> W;                 // мир прогона
		bool bQuit = false;                       // выйти после последнего этапа
		uint16 UserNumWas = 0;                    // свой userNum до прогона
		bool bKnownWas = false;                   // «userNum пришёл» до прогона
		uint8 SoulWas = 0;                        // силуэт «Друг» до прогона
		double T0 = 0.0;                          // реальное время отправки 6098 №1 (часы троттлинга)
		double T6099 = 0.0;                       // реальное время приёма 6099 на этапе E
	};

	// Тестовый свой userNum.
	static constexpr uint16 kMe = 321;

	// Эталонная таблица `netHandleTradeStatusNotice` (свой userNum): код → loc; 0 — молча (`default: return`).
	static int32 Expected6099(uint8 Code)
	{
		switch (Code)
		{
		case 0: case 2: case 3: return 7502;
		case 1:                 return 7506;
		case 4: case 5:         return 7507;
		case 6:                 return 7505;
		default:                return 0;
		}
	}

	// Строка разбора окна 570 по тегу (`DumpWindow`), пусто — нет строки.
	using AsdaSelfTestInput::DumpLine;   // ★ R5-c (12.11.2026): общая точка (`Test/AsdaSelfTestInput.h`) — копия была дословной.

	// Прямоугольник из строки дампа «… rect=(x0,y0)-(x1,y1) …»; false — «rect=нет», нулевой размер или строки нет.
	static bool ParseRect(const FString& Line, FBox2D& Out)
	{
		const int32 At = Line.Find(TEXT("rect=("));
		if (At == INDEX_NONE) { return false; }
		const FString Rest = Line.Mid(At + 6);                        // «x0,y0)-(x1,y1) …»
		FString A, B;                                                 // левая и правая точки
		if (!Rest.Split(TEXT(")-("), &A, &B)) { return false; }
		int32 CloseAt = INDEX_NONE;                                   // конец правой точки
		if (!B.FindChar(TEXT(')'), CloseAt)) { return false; }
		B = B.Left(CloseAt);
		FString Ax, Ay, Bx, By;                                       // координаты
		if (!A.Split(TEXT(","), &Ax, &Ay) || !B.Split(TEXT(","), &Bx, &By)) { return false; }
		Out = FBox2D(FVector2D(FCString::Atod(*Ax), FCString::Atod(*Ay)), FVector2D(FCString::Atod(*Bx), FCString::Atod(*By)));
		return Out.GetSize().X > 0.0 && Out.GetSize().Y > 0.0;
	}

	// Пересечение прямоугольников с ненулевой площадью (касание краями — не пересечение).
	static bool Overlaps(const FBox2D& A, const FBox2D& B)
	{
		return A.Min.X < B.Max.X - 0.5 && B.Min.X < A.Max.X - 0.5 && A.Min.Y < B.Max.Y - 0.5 && B.Min.Y < A.Max.Y - 0.5;
	}

	// A лежит внутри B (допуск 1 px на округление дампа).
	static bool Inside(const FBox2D& A, const FBox2D& B)
	{
		return A.Min.X >= B.Min.X - 1.0 && A.Min.Y >= B.Min.Y - 1.0 && A.Max.X <= B.Max.X + 1.0 && A.Max.Y <= B.Max.Y + 1.0;
	}

	// Перехваченная отправка: опкод и тело.
	using FSent = TArray<TPair<int32, TArray<uint8>>>;

	// Действие под перехватом исходящих (в сеть не уходит ничего); bOk=false — перехват не взвёлся.
	static FSent Capture(UGameInstance* GI, TFunctionRef<void()> Act, bool& bOk)
	{
		FSent Sent;                                                   // перехваченные исходящие
		UAsdaPetSubsystem* Pet = GI ? GI->GetSubsystem<UAsdaPetSubsystem>() : nullptr;
		bOk = Pet && Pet->BeginSendCapture([&Sent](int32 Op, const TArray<uint8>& Body, bool, TCHAR)
		{
			Sent.Add(TPair<int32, TArray<uint8>>(Op, Body));
		}, 0);
		Act();
		if (bOk) { Pet->EndSendCapture(); }
		return Sent;
	}

	// Сколько раз среди отправок встретился опкод Op; OutWords — первые три u16 тела последнего такого пакета.
	static int32 CountOp(const FSent& Sent, int32 Op, int32* OutWords = nullptr)
	{
		int32 N = 0;
		for (const TPair<int32, TArray<uint8>>& S : Sent)
		{
			if (S.Key != Op) { continue; }
			++N;
			if (OutWords)
			{
				for (int32 k = 0; k < 3; ++k) { OutWords[k] = S.Value.Num() >= 2 * k + 2 ? (S.Value[2 * k] | (S.Value[2 * k + 1] << 8)) : -1; }
			}
		}
		return N;
	}

	// Текст живого `UTextBlock` с таким именем в мире W (подпись окна 14450); пусто — не найден.
	static FString LiveText(UWorld* W, const TCHAR* Name)
	{
		for (TObjectIterator<UTextBlock> It; It; ++It)
		{
			UTextBlock* Tb = *It;
			if (!Tb || Tb->HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject) || Tb->GetWorld() != W) { continue; }
			if (Tb->GetName() == Name) { return Tb->GetText().ToString(); }
		}
		return FString();
	}

	static void StageGeometry(TSharedRef<FCtx> C);
	static void StageThrottle6099(TSharedRef<FCtx> C);
	static void StageThrottleAfter(TSharedRef<FCtx> C);
	static void Finish(TSharedRef<FCtx> C);

	// Запустить этап через Sec секунд мирового таймера (кадры идут, геометрия успевает отрисоваться).
	using AsdaSelfTestCtx::ByStage::After;   // ★ R5-c (12.11.2026): общая точка (`Test/AsdaSelfTestCtx.h`) — копия была дословной.

	// Этап 1 (синхронный): A, A2, клан, B, C. В конце — окно Эллы открыто и заведён этап D.
	static void Run(TSharedRef<FCtx> C)
	{
		UWorld* W = C->W.Get();
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
		UAsdaNpcDialogSubsystem* Dlg = GI ? GI->GetSubsystem<UAsdaNpcDialogSubsystem>() : nullptr;
		UAsdaClanSubsystem* Clan = GI ? GI->GetSubsystem<UAsdaClanSubsystem>() : nullptr;
		UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
		UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
		FRun& R = C->R;
		if (!Net || !Dlg || !Clan || !WM)
		{
			R.Expect(false, TEXT("нет подсистем (сеть/диалог/клан/окна)"));
			Finish(C);
			return;
		}
		C->UserNumWas = Net->SelfUserNum;          // восстановить в конце
		C->bKnownWas = Net->bSelfUserNumKnown;     // восстановить в конце
		C->SoulWas = Net->SelfSoulSilhouette;      // гейт «Друг» окна 14450
		WM->ClearMsgBoxQueue(TEXT("самотест 6099: старт"));

		// ════════════════ A. NPC 107 → кнопка → окно 14450 ════════════════
		const FString CapExp = AsdaNotice::Loc(GI, 990349);   // «Расширить клан»
		R.Expect(!CapExp.IsEmpty(), FString::Printf(TEXT("строка порта 990349 есть в DT_GameText: «%s»"), *CapExp));
		// ★ 06.11.2026: подписи окна 14450 показывают ЛИМИТ мест, а не вакансии; ожидание — по языку сессии (прогон --lang en).
		const FString WantNow = FAsdaLang::IsRu() ? TEXT("- Текущий лимит членов клана:") : TEXT("■ Current clan member limit");
		const FString WantNew = FAsdaLang::IsRu() ? TEXT("- Новый лимит членов клана:") : TEXT("■ New clan member limit");
		R.Expect(AsdaNotice::Loc(GI, 12444) == WantNow, FString::Printf(TEXT("loc 12444 = «%s» (ждём «%s»)"), *AsdaNotice::Loc(GI, 12444), *WantNow));
		R.Expect(AsdaNotice::Loc(GI, 12445) == WantNew, FString::Printf(TEXT("loc 12445 = «%s» (ждём «%s»)"), *AsdaNotice::Loc(GI, 12445), *WantNew));
		R.Expect(Dlg->OpenForNpcId(107), TEXT("окно разговора с NPC 107 открылось"));
		{
			const FString D = Dlg->DumpWindow();
			const FString B0 = DumpLine(D, TEXT("NPC_SHOP_BUT00")), B1 = DumpLine(D, TEXT("NPC_SHOP_BUT01"));
			const FString B2 = DumpLine(D, TEXT("NPC_SHOP_BUT02")), B3 = DumpLine(D, TEXT("NPC_SHOP_BUT03"));
			const FString B4 = DumpLine(D, TEXT("NPC_SHOP_BUT04"));
			R.Expect(B3.Contains(TEXT("видим=1")) && B3.Contains(FString::Printf(TEXT("подпись=«%s»"), *CapExp)) && B3.Contains(TEXT("код=30027")),
				FString::Printf(TEXT("кнопка 3 видна, подпись 990349, код 30027: %s"), *B3.TrimStart()));
			R.Expect(B0.Contains(TEXT("видим=1")) && B0.Contains(TEXT("код=30016")), FString::Printf(TEXT("контроль: кнопка 0 на месте (30016): %s"), *B0.TrimStart()));
			R.Expect(B1.Contains(TEXT("видим=1")) && B1.Contains(TEXT("код=30015")), FString::Printf(TEXT("контроль: кнопка 1 на месте (30015): %s"), *B1.TrimStart()));
			R.Expect(B2.Contains(TEXT("видим=1")) && B2.Contains(TEXT("код=30068")), FString::Printf(TEXT("контроль: кнопка 2 на месте (30068): %s"), *B2.TrimStart()));
			const FString LvCap = AsdaNotice::Loc(GI, 991201);   // ★ 06.11.2026 «В Забытую Лощину» (было loc 605, голое имя карты)
			R.Expect(!LvCap.IsEmpty() && B4.Contains(TEXT("видим=1")) && B4.Contains(FString::Printf(TEXT("подпись=«%s»"), *LvCap)) && B4.Contains(TEXT("код=30037")),
				FString::Printf(TEXT("кнопка 4 видна, подпись loc 991201 «%s», код 30037: %s"), *LvCap, *B4.TrimStart()));
			// ★ 06.11.2026: подписи кнопок 3 и 4 влезают в кнопку (JUI `NPC_SHOP_BUT0x` Size 182×24): текст длиннее — обрезается краем.
			for (const FString* Bn : { &B3, &B4 })
			{
				int32 At = Bn->Find(TEXT("ширина="));
				const float Wd = At >= 0 ? FCString::Atof(*Bn->Mid(At + 7)) : -1.f;
				R.Expect(Wd > 0.f && Wd <= 170.f, FString::Printf(TEXT("подпись с полями влезает в кнопку 182 (не больше 170): ширина %.0f (%s)"), Wd, *Bn->TrimStart()));
			}
			R.Expect(!D.Contains(TEXT("код=30031")),
				TEXT("контроль: скрытая кнопка данных «Долина Ночи» (30031, слот 5) в окне НЕ показана"));
		}
		// ── A2. Нажатие кнопки 4 (585) → 5477 слово 5 + окно закрыто. ──
		{
			bool bCap = false;   // перехват взведён
			const FSent Sent = Capture(GI, [&]() { Dlg->OnDialogButton(585); }, bCap);
			R.Expect(bCap, TEXT("перехват исходящих взведён"));
			if (bCap)
			{
				int32 Wd[3] = { -1, -1, -1 };   // слова тела 5477
				const int32 N5477 = CountOp(Sent, 5477, Wd);   // ДО Expect (слова заполняются здесь)
				R.Expect(N5477 == 1 && Wd[0] == 5,
					FString::Printf(TEXT("кнопка «Забытая Лощина» → 5477 ×%d, слово %d (ждём ×1, слово 5 = LostValley); всего исходящих %d"), N5477, Wd[0], Sent.Num()));
				R.Expect(!Dlg->IsDialogOpen(), TEXT("после телепорта окно разговора закрыто (эталон: UI_resetAllWindowsAndLeave_5045)"));
			}
			if (Dlg->IsDialogOpen()) { Dlg->Close(); }
		}
		R.Expect(Dlg->OpenForNpcId(107), TEXT("окно разговора с NPC 107 открыто повторно (для части клана)"));
		// Без клана: нажатие доходит до окна, а оно отказывает 12431 (эталонный первый гейт) — окна нет.
		const int32 ClanWas = WS ? WS->GetSelfClanId() : -1;   // свой клан до проверки
		Net->SelfSoulSilhouette = 0;
		Net->OnClanInfo.Broadcast(-1, -1, FString(), FString());
		Dlg->OnDialogButton(584);
		R.Expect(!Clan->IsIncreaseOpen(), TEXT("контроль: без клана окно 14450 НЕ открылось"));
		// ★ 08.11.2026 [ИСПРАВЛЕНИЕ] D8: без клана — 991210 «Ты не состоишь в клане.» (ключ asda.Clan.NoClanNotice; 0 = 12431).
		const int32 WantNoClan = UAsdaClanSubsystem::NoClanLocOr(GI, 12431);
		R.Expect(WM->GetMsgBoxQueueTopText() == AsdaNotice::Loc(GI, WantNoClan),
			FString::Printf(TEXT("контроль: без клана нотис %d «%s»"), WantNoClan, *WM->GetMsgBoxQueueTopText()));
		WM->ClearMsgBoxQueue(TEXT("самотест 6099: после 12431"));
		// С кланом: блок клана штатным 4132 (лимит 30, расширений 2), свой клан 7 — окно открывается нажатием.
		{
			TArray<uint8> Body; Body.SetNumZeroed(192);
			Body[0] = 1; Body[1] = 7;
			const char* Name = "SelfTest";
			for (int32 i = 0; Name[i]; ++i) { Body[3 + i] = (uint8)Name[i]; }
			Body[1 + 45] = 30; Body[1 + 47] = 2;
			Net->DebugFeedPacket(4132, Body);
		}
		Net->OnClanInfo.Broadcast(-1, 7, TEXT("SelfTest"), FString());
		if (!Dlg->IsDialogOpen()) { Dlg->OpenForNpcId(107); }
		Dlg->OnDialogButton(584);
		R.Expect(Clan->IsIncreaseOpen(), TEXT("с кланом: кнопка «Расширить клан» открыла окно 14450"));
		R.Expect(WM->GetMsgBoxQueueNum() == 0, FString::Printf(TEXT("с кланом: отказов нет (очередь %d)"), WM->GetMsgBoxQueueNum()));
		{
			const FString Lb = LiveText(W, TEXT("CLAN_INC_MEMINC"));
			R.Expect(Lb == WantNew, FString::Printf(TEXT("14454 CLAN_INC_MEMINC = «%s»"), *Lb));
		}
		Clan->CloseIncreaseWindow();
		Dlg->Close();
		// Восстановление клана: 4132 код 2 (клана нет) и свой клан как был.
		{ TArray<uint8> Body; Body.SetNumZeroed(192); Body[0] = 2; Net->DebugFeedPacket(4132, Body); }
		Net->OnClanInfo.Broadcast(-1, ClanWas, FString(), FString());
		WM->ClearMsgBoxQueue(TEXT("самотест 6099: после окна 14450"));

		// ════════════════ B. 6099 через HandlePacket ════════════════
		const uint16 Me = kMe;   // тестовый свой userNum
		Net->SelfUserNum = Me; Net->bSelfUserNumKnown = true;
		auto Feed = [Net](uint16 U, uint8 Code, int32 Len = 3)
		{
			TArray<uint8> B; B.SetNumZeroed(Len);
			if (Len > 0) { B[0] = (uint8)(U & 0xFF); }
			if (Len > 1) { B[1] = (uint8)(U >> 8); }
			if (Len > 2) { B[2] = Code; }
			Net->DebugFeedPacket(6099, B);
		};
		for (int32 Cd = 0; Cd <= 7; ++Cd)
		{
			WM->ClearMsgBoxQueue(TEXT("самотест 6099: шаг"));
			Feed(Me, (uint8)Cd);
			const int32 Exp = Expected6099((uint8)Cd);
			if (Exp)
			{
				R.Expect(WM->IsMsgBoxQueued(0, 24) && WM->GetMsgBoxQueueTopText() == AsdaNotice::Loc(GI, Exp),
					FString::Printf(TEXT("код %d → нотис %d в окне (0, 24): «%s»"), Cd, Exp, *WM->GetMsgBoxQueueTopText()));
			}
			else
			{
				R.Expect(WM->GetMsgBoxQueueNum() == 0, FString::Printf(TEXT("контроль: код %d → молча (очередь %d)"), Cd, WM->GetMsgBoxQueueNum()));
			}
		}
		WM->ClearMsgBoxQueue(TEXT("самотест 6099: 255"));
		Feed(Me, 255);
		R.Expect(WM->GetMsgBoxQueueNum() == 0, TEXT("контроль: код 255 → молча"));
		// Чужой userNum: на коде 2 вместо 7502 — 133.
		WM->ClearMsgBoxQueue(TEXT("самотест 6099: чужой"));
		Feed((uint16)(Me + 1), 2);
		R.Expect(WM->IsMsgBoxQueued(0, 24) && WM->GetMsgBoxQueueTopText() == AsdaNotice::Loc(GI, 133),
			FString::Printf(TEXT("чужой userNum, код 2 → 133 «%s» (не 7502)"), *WM->GetMsgBoxQueueTopText()));
		// Чужой userNum с «молчащим» кодом — всё равно 133 (эталон сверяет адресата ДО разбора кода).
		WM->ClearMsgBoxQueue(TEXT("самотест 6099: чужой 7"));
		Feed((uint16)(Me + 1), 7);
		R.Expect(WM->GetMsgBoxQueueTopText() == AsdaNotice::Loc(GI, 133), TEXT("чужой userNum, код 7 → 133 (адресат сверяется до кода)"));
		// Повтор одного и того же (0, 24) — окно одно (очередь эталона глушит пару, уже стоящую в стеке).
		WM->ClearMsgBoxQueue(TEXT("самотест 6099: повтор"));
		Feed(Me, 2); Feed(Me, 4);
		R.Expect(WM->GetMsgBoxQueueNum() == 1, FString::Printf(TEXT("повтор пары (0, 24): записей %d (ждём 1)"), WM->GetMsgBoxQueueNum()));
		// Короткое тело — отброшено.
		WM->ClearMsgBoxQueue(TEXT("самотест 6099: короткое"));
		Feed(Me, 2, 2);
		R.Expect(WM->GetMsgBoxQueueNum() == 0, TEXT("контроль: тело 2 б — отброшено"));
		// Свой персонаж не в мире (userNum не пришёл) — молча.
		Net->bSelfUserNumKnown = false;
		Feed(Me, 2);
		R.Expect(WM->GetMsgBoxQueueNum() == 0, TEXT("контроль: своего персонажа нет → молча"));

		// ════════════════ C. Отступления 6098 (МЕЛК-6): гейт userNum, окна нехватки и отказа сторожа ════════════════
		{
			bool bCap = false;   // перехват взведён
			// C1. Отказ сторожа: строки 999 нет ни в одной таблице → сторож отказывает → окно (0, 24) с 7502, пакета нет.
			Net->SelfUserNum = Me; Net->bSelfUserNumKnown = true;
			WM->ClearMsgBoxQueue(TEXT("самотест 6099: сторож"));
			FSent S = Capture(GI, [&]() { Dlg->RequestEventExchange(999, 107); }, bCap);
			R.Expect(bCap && CountOp(S, 6098) == 0 && WM->IsMsgBoxQueued(0, 24) && WM->GetMsgBoxQueueTopText() == AsdaNotice::Loc(GI, 7502),
				FString::Printf(TEXT("сторож 6098 (строка 999) → окно (0, 24) 7502 «%s», 6098 ×%d (ждём 0)"), *WM->GetMsgBoxQueueTopText(), CountOp(S, 6098)));
			// C2. Нехватка по строке 13 (53819 ×10; офлайн сумка пуста) → окно (0, −1) с 98101, пакета нет.
			R.Expect(Dlg->IsEventExchangeServerSafe(13), TEXT("предусловие: строку 13 сторож пропускает (клиент = сервер)"));
			WM->ClearMsgBoxQueue(TEXT("самотест 6099: 98101"));
			S = Capture(GI, [&]() { Dlg->RequestEventExchange(13, 107); }, bCap);
			R.Expect(CountOp(S, 6098) == 0 && WM->IsMsgBoxQueued(0, -1) && !WM->IsMsgBoxQueued(0, 24) && WM->GetMsgBoxQueueTopText() == AsdaNotice::Loc(GI, 98101),
				FString::Printf(TEXT("нехватка строки 13 → окно (0, −1) 98101 «%s» (не лента), 6098 ×%d"), *WM->GetMsgBoxQueueTopText(), CountOp(S, 6098)));
			// C3. 30048/30049 без своего userNum — молча: ни окна, ни пакета (эталон LABEL_133).
			for (const int32 Code : { 30048, 30049 })
			{
				Net->bSelfUserNumKnown = false;
				WM->ClearMsgBoxQueue(TEXT("самотест 6099: без userNum"));
				S = Capture(GI, [&]() { Dlg->RunCommandForCheck(Code, -1); }, bCap);
				R.Expect(CountOp(S, 6098) == 0 && WM->GetMsgBoxQueueNum() == 0,
					FString::Printf(TEXT("%d без своего userNum → молча: 6098 ×%d, окон %d (ждём 0/0)"), Code, CountOp(S, 6098), WM->GetMsgBoxQueueNum()));
				// Контрольная пара: со своим userNum то же нажатие ДОХОДИТ до проверок — окно (нехватки 98102 или отказа сторожа 7502).
				Net->bSelfUserNumKnown = true;
				WM->ClearMsgBoxQueue(TEXT("самотест 6099: с userNum"));
				// ★ 27.09.2026 (toolsleft): ожидание — из ТОЙ ЖЕ точки, что проверяет сама кнопка (КРИТ-2 itemsdatafix):
				//   сторож сравнивает с сервером зашитое требование КНОПКИ, а не строку DT_EventItem[3/4]. Прежнее
				//   `IsEventExchangeServerSafe(Code == 30048 ? 3 : 4)` для 30049 ждало 98102, а кнопка отказывает окном 7502.
				FString SafeWhy;   // причина отказа сторожа (пусто, если пропустил)
				const bool bSafe = Dlg->IsExchangeButtonServerSafe(Code, &SafeWhy);   // пропустит ли сторож требование кнопки
				const bool bRowSafe = Dlg->IsEventExchangeServerSafe(Code == 30048 ? 3 : 4);   // прежнее (строковое) ожидание — для сверки
				UE_LOG(LogAsdaClanBtn6099Test, Log, TEXT("САМОТЕСТ-6099 инфо %d: сторож кнопки %s, по строке %s%s%s"), Code,
					bSafe ? TEXT("пропустил") : TEXT("отказал"), bRowSafe ? TEXT("пропустил") : TEXT("отказал"),
					SafeWhy.IsEmpty() ? TEXT("") : TEXT(" — "), *SafeWhy);
				S = Capture(GI, [&]() { Dlg->RunCommandForCheck(Code, -1); }, bCap);
				const int32 ExpLoc = bSafe ? 98102 : 7502;              // сторож пропустил → нехватка; иначе отказ сторожа
				const int32 ExpId = bSafe ? -1 : 24;                     // ключ очереди эталона
				R.Expect(CountOp(S, 6098) == 0 && WM->IsMsgBoxQueued(0, ExpId) && WM->GetMsgBoxQueueTopText() == AsdaNotice::Loc(GI, ExpLoc),
					FString::Printf(TEXT("%d со своим userNum (сторож %s) → окно (0, %d) %d «%s», 6098 ×%d"),
						Code, bSafe ? TEXT("пропустил") : TEXT("отказал"), ExpId, ExpLoc, *WM->GetMsgBoxQueueTopText(), CountOp(S, 6098)));
			}
			// C3-контроль: код, не являющийся кнопкой обмена, сторож кнопок ОТКЛОНЯЕТ с причиной (отрицательная пара к 30048/30049).
			{
				FString NotBtnWhy;   // причина для «не кнопки»
				const bool bNotBtn = Dlg->IsExchangeButtonServerSafe(30047, &NotBtnWhy);
				R.Expect(!bNotBtn && !NotBtnWhy.IsEmpty(),
					FString::Printf(TEXT("контроль: код 30047 не кнопка обмена → сторож кнопок отказал «%s»"), *NotBtnWhy));
			}
			// C4. Строка 0 (требований нет ни у клиента, ни у сервера): без userNum — молча; с userNum — ровно один 6098, слово +0 = userNum.
			R.Expect(Dlg->IsEventExchangeServerSafe(0), TEXT("предусловие: строку 0 сторож пропускает"));
			Net->bSelfUserNumKnown = false;
			WM->ClearMsgBoxQueue(TEXT("самотест 6099: 0 без userNum"));
			S = Capture(GI, [&]() { Dlg->RequestEventExchange(0, 107); }, bCap);
			R.Expect(CountOp(S, 6098) == 0 && WM->GetMsgBoxQueueNum() == 0,
				FString::Printf(TEXT("обмен 0 без своего userNum → 6098 ×%d, окон %d (ждём 0/0; прежде уходил с userNum 0)"), CountOp(S, 6098), WM->GetMsgBoxQueueNum()));
			Net->bSelfUserNumKnown = true;
			int32 Wd[3] = { -1, -1, -1 };   // слова тела 6098
			S = Capture(GI, [&]() { Dlg->RequestEventExchange(0, 107); }, bCap);
			C->T0 = FPlatformTime::Seconds();
			const int32 N6098 = CountOp(S, 6098, Wd);   // ДО Expect: порядок вычисления аргументов не задан — слова должны быть заполнены
			R.Expect(bCap && N6098 == 1 && Wd[0] == Me && Wd[1] == 107 && Wd[2] == 0,
				FString::Printf(TEXT("контроль: обмен 0 со своим userNum → 6098 ×%d, слова %d/%d/%d (ждём ×1, %u/107/0)"),
					N6098, Wd[0], Wd[1], Wd[2], (uint32)Me));
			WM->ClearMsgBoxQueue(TEXT("самотест 6099: после C"));
		}

		// ── к этапу D: окно Эллы открыто, геометрию снимаем через кадры ──
		R.Expect(Dlg->OpenForNpcId(107), TEXT("окно разговора с NPC 107 открыто для геометрии"));
		After(C, 0.5f, &StageGeometry);
	}

	// Этап D (МЕЛК-4): живая геометрия кнопок меню Эллы после отрисовки.
	static void StageGeometry(TSharedRef<FCtx> C)
	{
		UWorld* W = C->W.Get();
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaNpcDialogSubsystem* Dlg = GI ? GI->GetSubsystem<UAsdaNpcDialogSubsystem>() : nullptr;
		FRun& R = C->R;
		if (!Dlg) { R.Expect(false, TEXT("D: нет подсистемы диалога")); Finish(C); return; }
		const FString D = Dlg->DumpWindow();
		const FString BoxLine = DumpLine(D, TEXT("NPC_Function_Box_grp"));   // строка группы кнопок
		FBox2D Box(ForceInit);   // прямоугольник NPC_Function_Box_grp
		const bool bBox = ParseRect(BoxLine, Box);
		R.Expect(bBox, FString::Printf(TEXT("D: NPC_Function_Box_grp отрисован: %s"), *BoxLine.TrimStart()));
		TArray<FBox2D> Rc;       // прямоугольники кнопок 0…4
		for (int32 i = 0; i < 5; ++i)
		{
			const FString L = DumpLine(D, FString::Printf(TEXT("NPC_SHOP_BUT%02d"), i));
			FBox2D B(ForceInit);
			const bool bR = ParseRect(L, B);
			Rc.Add(B);
			R.Expect(bR && L.Contains(TEXT("видим=1")) && bBox && Inside(B, Box),
				FString::Printf(TEXT("D: кнопка %d видима, rect непустой и внутри NPC_Function_Box_grp: %s"), i, *L.TrimStart()));
		}
		int32 Bad = 0;           // пересекающихся пар
		FString Pairs;           // перечень пересечений
		for (int32 i = 0; i < 5; ++i)
		{
			for (int32 j = i + 1; j < 5; ++j)
			{
				if (Overlaps(Rc[i], Rc[j])) { ++Bad; Pairs += FString::Printf(TEXT(" %d×%d"), i, j); }
			}
		}
		R.Expect(Bad == 0, FString::Printf(TEXT("D: кнопки 0…4 попарно не пересекаются (пересечений %d:%s)"), Bad, *Pairs));
		R.Expect(!Overlaps(Rc[3], Rc[2]) && !Overlaps(Rc[3], Rc[4]), TEXT("D: кнопка 3 «Расширить клан» не наезжает на 2 и 4"));
		Dlg->Close();
		// Контроль: у Иана (NPC 3, MenuCount 3) кнопка 3 скрыта, кнопка 2 видна.
		R.Expect(Dlg->OpenForNpcId(3), TEXT("D-контроль: окно разговора с NPC 3 (Иан, MenuCount 3) открылось"));
		{
			const FString D3 = Dlg->DumpWindow();
			const FString B2 = DumpLine(D3, TEXT("NPC_SHOP_BUT02")), B3 = DumpLine(D3, TEXT("NPC_SHOP_BUT03"));
			R.Expect(B2.Contains(TEXT("видим=1")) && B3.Contains(TEXT("видим=0")),
				FString::Printf(TEXT("D-контроль: у Иана кнопка 2 видна, кнопка 3 скрыта: %s | %s"), *B2.TrimStart(), *B3.TrimStart()));
		}
		Dlg->Close();
		After(C, 1.0f, &StageThrottle6099);
	}

	// Этап E1 (МЕЛК-5): через ~1,5 с после 6098 №1 — приём 6099 и повторная отправка: троттлинг должен СТОЯТЬ.
	static void StageThrottle6099(TSharedRef<FCtx> C)
	{
		UWorld* W = C->W.Get();
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
		UAsdaNpcDialogSubsystem* Dlg = GI ? GI->GetSubsystem<UAsdaNpcDialogSubsystem>() : nullptr;
		UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
		FRun& R = C->R;
		if (!Net || !Dlg || !WM) { R.Expect(false, TEXT("E1: нет подсистем")); Finish(C); return; }
		Net->SelfUserNum = kMe; Net->bSelfUserNumKnown = true;
		TArray<uint8> B; B.SetNumZeroed(3); B[0] = (uint8)(kMe & 0xFF); B[1] = (uint8)(kMe >> 8); B[2] = 2;
		Net->DebugFeedPacket(6099, B);
		C->T6099 = FPlatformTime::Seconds();
		const double Since = C->T6099 - C->T0;   // с момента 6098 №1, реальные секунды
		R.Expect(Since < 3.0, FString::Printf(TEXT("E1: проверка в пределах окна троттлинга (%.2f с после 6098 №1 < 3)"), Since));
		R.Expect(WM->IsMsgBoxQueued(0, 24), TEXT("E1: 6099 код 2 принят (окно (0, 24))"));
		WM->ClearMsgBoxQueue(TEXT("самотест 6099: E1"));
		bool bCap = false;   // перехват взведён
		const FSent S = Capture(GI, [&]() { Dlg->RequestEventExchange(0, 107); }, bCap);
		R.Expect(bCap && CountOp(S, 6098) == 0,
			FString::Printf(TEXT("E1: после приёма 6099 повторный 6098 НЕ ушёл (×%d) — 6099 троттлинг не сбросил"), CountOp(S, 6098)));
		After(C, 1.9f, &StageThrottleAfter);
	}

	// Этап E2 (МЕЛК-5): ≥ 3 с после 6098 №1 и < 3 с после 6099 — отправка проходит: 6099 троттлинг не продлил.
	static void StageThrottleAfter(TSharedRef<FCtx> C)
	{
		UWorld* W = C->W.Get();
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaNpcDialogSubsystem* Dlg = GI ? GI->GetSubsystem<UAsdaNpcDialogSubsystem>() : nullptr;
		FRun& R = C->R;
		if (!Dlg) { R.Expect(false, TEXT("E2: нет подсистемы диалога")); Finish(C); return; }
		const double Now = FPlatformTime::Seconds();
		const double Since0 = Now - C->T0;         // с момента 6098 №1
		const double Since6099 = Now - C->T6099;   // с момента приёма 6099
		R.Expect(Since0 >= 3.0 && Since6099 < 3.0,
			FString::Printf(TEXT("E2: момент проверки — %.2f с после 6098 №1 (≥ 3) и %.2f с после 6099 (< 3)"), Since0, Since6099));
		bool bCap = false;   // перехват взведён
		int32 Wd[3] = { -1, -1, -1 };   // слова тела 6098
		const FSent S = Capture(GI, [&]() { Dlg->RequestEventExchange(0, 107); }, bCap);
		const int32 N6098 = CountOp(S, 6098, Wd);   // ДО Expect (слова заполняются здесь)
		R.Expect(bCap && N6098 == 1 && Wd[0] == kMe,
			FString::Printf(TEXT("E2: контроль — 6098 ушёл (×%d, слово +0 = %d) — 6099 троттлинг не продлил"), N6098, Wd[0]));
		Finish(C);
	}

	// Восстановление состояния, итог, выход.
	static void Finish(TSharedRef<FCtx> C)
	{
		UWorld* W = C->W.Get();
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
		UAsdaNpcDialogSubsystem* Dlg = GI ? GI->GetSubsystem<UAsdaNpcDialogSubsystem>() : nullptr;
		UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
		if (Dlg && Dlg->IsDialogOpen()) { Dlg->Close(); }
		if (Net)
		{
			Net->SelfUserNum = C->UserNumWas; Net->bSelfUserNumKnown = C->bKnownWas;
			Net->SelfSoulSilhouette = C->SoulWas;
		}
		if (WM) { WM->ClearMsgBoxQueue(TEXT("самотест 6099: конец")); }
		UE_LOG(LogAsdaClanBtn6099Test, Log, TEXT("САМОТЕСТ-6099 ИТОГ: ok=%d fail=%d"), C->R.Ok, C->R.Fail);
		if (C->bQuit && GEngine && W) { GEngine->Exec(W, TEXT("quit")); }
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaClanBtn6099SelfTest(
	TEXT("asda.Clan.BtnSelfTest"),
	TEXT("asda.Clan.BtnSelfTest [delay=N] [quit] — ОФЛАЙН-самотест: кнопки Эллы (107) → окно 14450 / 5477, приём 6099, гейты и окна 6098, геометрия меню, троттлинг"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		const FAsdaSelfTestArgs KitArgs = FAsdaSelfTestArgs::Parse(A);   // ★ 11.10.2026 (В3 S1): каркас — отказ с ИТОГ и quit, второй барьер на все опкоды
		if (AsdaSelfTestKit::RefuseStart(W, TEXT("asda.Clan.BtnSelfTest"), KitArgs)) { return; }
		if (!W) { return; }
		float Delay = 0.f;   // задержка запуска, с
		bool bQuit = false;  // выйти после прогона
		for (const FString& Arg : A)
		{
			if (Arg.Equals(TEXT("quit"), ESearchCase::IgnoreCase)) { bQuit = true; }
		}
		Delay = AsdaSelfTestKit::DelayArgOr(A, Delay);   // ★ R5-fix2-c (13.11.2026): `delay=N` — общая точка (была ветка цикла аргументов)
		TWeakObjectPtr<UWorld> WW = W;
		auto Go = [WW, bQuit]()   // прогон (сразу или по таймеру); выход — в последнем этапе
		{
			if (!WW.Get()) { return; }
			if (AsdaSelfTestKit::RefuseDeferred(WW.Get(), TEXT("asda.Clan.BtnSelfTest"), bQuit)) { return; }   // Р-2: офлайн повторно — на отложенном старте
			TSharedRef<AsdaClanBtn6099TestLocal::FCtx> C = MakeShared<AsdaClanBtn6099TestLocal::FCtx>();
			C->W = WW; C->bQuit = bQuit;
			AsdaClanBtn6099TestLocal::Run(C);
		};
		if (Delay <= 0.f) { Go(); return; }
		FTimerHandle H;
		W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda(Go), Delay, false);
	}));

#endif // !UE_BUILD_SHIPPING
