// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ: АУДИТ ОТПРАВИТЕЛЕЙ charId / userNum (27.09.2026, отчёт `Reports/owner_0927/charid_senders_audit.md`). [ПОРТ]
//
// ★ ЗАЧЕМ. В теле пакетов оригинала два РАЗНЫХ номера своего персонажа (закон `charid-vs-usernum`):
//   charId (i32, `CCharacter::userIdx` = `char+1052` — миснейм IDA) и userNum (i16, `effectSlots[111].field_5B8` = `char+190188`).
//   Аудит нашёл отправители, где порт клал не тот номер. Самотест гоняет БОЕВЫЕ отправители (тем же кодом, что кнопка / окно)
//   под перехватом исходящих (`UAsdaNetSubsystem::ArmSendCapture`, в сеть ничего не уходит) и сверяет байты с эталоном.
//   Приёмы — через ШТАТНЫЙ разбор `DebugFeedPacket` → `HandlePacket`, вывод — в очереди окон-сообщений.
//
// ★ ЧТО ПРОВЕРЯЕТ (ожидания заданы ЯВНО по эталону, контрольные пары у каждого пункта):
//   A. 6059 (код 30038): слово +0 = свой userNum (не младшее слово charId); суточный отказ «раз в день на цель» → окно (0, 24) 7501,
//      пакета нет; другой день — уходит; 6098 перезаписывает общую цель (`self[24116]`) — после него 6059 уходит; без своего
//      персонажа — ни пакета, ни окна.
//   B. 6124 (код 30061 и отправитель): +0 = charId; пустые записи {−1, 0, −1}; контроль — +0 ≠ userNum.
//   C. 6125 (приём): чужой charId → 133; коды 0/2/6 → 98104/133/98106, 7/8/9/10/11/255 → 98104 [ИСПРАВЛЕНИЕ 09.11] («9» при ключе 0); код 1 → снята ТОЛЬКО совпавшая ячейка, вес, 98105.
//   D. 6060 (приём): таблица кодов 0…10, чужой userNum → 133, без своего персонажа — молча.
//   E. 6091 (30043) и 6146 (30071): +0 / +1 = свой userNum; без своего персонажа — пакета нет.
//   F. 4136 / 4138 / 4050 / 6586 @0 и шапка аукциона 9907 @5 = charId; 4120 (клан-чат, путь Enter) @2 = charId.
//   ★ 27.09.2026 (исправление ревизии charid_senders_REVIEW.md):
//   C+. 6125 код 4 → 98104; контроль «тот же код и слот, но другой контейнер» — ячейка остаётся.
//   G. 6096 (вскрытие коробки 20816): @0 = свой userNum, @2 = charId; без своего персонажа — ни пакета (эталон молчит).
//   K. 6126 «корона»: гейт CallMobTable[0].+16; не найден → глобальный владелец; свой → «ждёт показа» → эффект king_cap01
//      показан; ветка глобального владельца (исправление: видимость из байта; откат `asda.Crown.GlobalUsesStoredByte 0` —
//      погашена); сброс смены карты гасит эффект и хранит свои поля; разрыв — всё к старту. Контроль — закрытый гейт.
//   H. Время жизни суточного отказа 6059: смена карты и выход в выбор персонажа его НЕ сбрасывают, разрыв — сбрасывает.
//   ★ Дедуп №2, В2-A (11.10.2026) — S. «это я?»: charId 0, свой павн по charId (N4), userNum 0, устаревшая запись NetPlayers с
//   моим номером (N4), класс ≠ профессия (U5d), 5105 без своего номера (U5b); ключи отката StrictUnknownNum/CharIdLookupSelf/UserIdxLookupSelfFirst.
//   Всё состояние (номера, суточный отказ, ячейки, клан, очередь окон, диалог, вес) восстанавливается в конце.
//
// Консоль: `asda.Net.CharIdSendersSelfTest [delay=N] [quit]`. Только офлайн (`FAsdaOutgoingGate::RefuseDebugWhenOnline`).
//   Строки проверок — «САМОТЕСТ-CHARID», итог — «САМОТЕСТ-CHARID ИТОГ: ok=… fail=…».
#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "AsdaNetSubsystem.h"
#include "AsdaCVarSave.h"                   // ★ ИСПРАВЛЕНИЕ 11.10.2026 (В0, N1(д)/U8): ключи консоли самотеста — запомнить и вернуть (значение + SetBy)
// RefuseDebugWhenOnline — офлайн-гейт самотестов   ★ R3: комментарий снятого включения "AsdaOutgoingGate.h" (файл им не пользовался) — сохранён
#include "Test/AsdaSelfTestKit.h"   // ★ 11.10.2026 (В3 S1): общий каркас самотестов (аргументы, отказ, перехват, итог, выход)
#include "AsdaBytes.h"                         // RdI16 / RdI32 / WrI16 / WrI32 — тела пакетов
#include "Npc/AsdaNpcDialogSubsystem.h"        // OpenForNpcId / RunCommandForCheck — коды диспетчера тем же кодом, что кнопка
#include "UI/AsdaInventorySubsystem.h"         // DebugPutItem / GetCellItemCode / CountItemById — ячейки контейнера 2
#include "UI/AsdaWindowSubsystem.h"            // очередь окон-сообщений: IsMsgBoxQueued / GetMsgBoxQueueTopText
#include "UI/AsdaChatWindowSubsystem.h"        // SetInputState / DebugSubmit — путь Enter клан-чата 4120
#include "World/AsdaWorldSubsystem.h"          // GetSelfClanId — восстановить клан
#include "UI/AsdaNotice.h"                     // AsdaNotice::Loc — ожидаемый текст нотиса
#include "World/AsdaKingCrownSubsystem.h"      // K. приём 6126 и показ короны
#include "AsdaEffectActor.h"                   // K. эффект короны (скрыт/показан)
#include "AsdaSessionReset.h"                  // H/K. причины сброса сессии
#include "AsdaSelf.h"                          // S. «это я?» (дедуп №2, В2-A)
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaCharIdTest, Log, All);

// Друг `UAsdaNetSubsystem` (объявлено в его заголовке): доступ к перехватчику и полям суточного отказа 6059.
struct FAsdaCharIdSendersSelfTest
{
	// Перехваченная отправка: опкод и payload.
	struct FSent
	{
		int32 Op = 0;          // опкод
		TArray<uint8> P;       // payload (без 24 байт конверта)
	};

	int32 Ok = 0, Fail = 0;   // пройдено / провалено

	// Одна проверка: печать и счёт.
	void Expect(bool b, const FString& What)
	{
		if (b) { ++Ok; UE_LOG(LogAsdaCharIdTest, Log, TEXT("САМОТЕСТ-CHARID ok   %s"), *What); }
		else   { ++Fail; UE_LOG(LogAsdaCharIdTest, Error, TEXT("САМОТЕСТ-CHARID FAIL %s"), *What); }
	}

	// Действие под перехватом исходящих; Only = 0 — весь трафик, ≠0 — только этот опкод (остальное идёт обычным путём).
	static TArray<FSent> Capture(UAsdaNetSubsystem* Net, uint16 Only, TFunctionRef<void()> Act)
	{
		TArray<FSent> Out;   // перехваченные отправки
		const uint32 Gen = AsdaSelfTestKit::ArmCapture(Net, [&Out](uint16 Op, const uint8* P, int32 Len, bool, TCHAR)
		{
			FSent S;
			S.Op = Op;
			if (P && Len > 0) { S.P.Append(P, Len); }
			Out.Add(MoveTemp(S));
		}, Only, TEXT("asda.Net.CharIdSendersSelfTest"));
		Act();
		AsdaSelfTestKit::DisarmCapture(Net, Gen);
		return Out;
	}

	// Сколько раз встретился опкод; OutLast — payload последнего такого пакета.
	static int32 CountOp(const TArray<FSent>& S, int32 Op, TArray<uint8>* OutLast = nullptr)
	{
		int32 N = 0;
		for (const FSent& X : S) { if (X.Op == Op) { ++N; if (OutLast) { *OutLast = X.P; } } }
		return N;
	}

	// u16 по смещению (−1 — тела не хватает).
	static int32 W16(const TArray<uint8>& P, int32 Off) { return P.Num() >= Off + 2 ? (int32)(uint16)AsdaBytes::RdI16(P.GetData() + Off) : -1; }
	// i32 по смещению (INT32_MIN — тела не хватает).
	static int32 D32(const TArray<uint8>& P, int32 Off) { return P.Num() >= Off + 4 ? AsdaBytes::RdI32(P.GetData() + Off) : MIN_int32; }

	// Прогон целиком (синхронно).
	void Run(UWorld* W);
};

void FAsdaCharIdSendersSelfTest::Run(UWorld* W)
{
	UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	UAsdaNpcDialogSubsystem* Dlg = GI ? GI->GetSubsystem<UAsdaNpcDialogSubsystem>() : nullptr;
	UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	UAsdaChatWindowSubsystem* Chat = GI ? GI->GetSubsystem<UAsdaChatWindowSubsystem>() : nullptr;
	UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
	if (!Net || !Dlg || !Inv || !WM)
	{
		Expect(false, TEXT("нет подсистем (сеть/диалог/инвентарь/окна)"));
		return;
	}

	// ── Снимок состояния (восстанавливается в конце) ──
	const uint16 UserNumWas = Net->SelfUserNum;             // свой userNum
	const bool bKnownWas = Net->bSelfUserNumKnown;          // «userNum пришёл»
	const int32 CharIdWas = Net->SelectedCharId;            // свой charId
	const int32 ArmedWas = Net->EventNpcDayArmed;           // суточный отказ 6059
	const int32 TargetWas = Net->EventNpcLastTarget;
	const int32 DayWas = Net->EventNpcDay;
	const double ExchWas = Net->LastNpcExchangeSendTime;    // троттлинг 6098
	const int32 WeightWas = Net->SelfWeightCur;             // вес
	const int32 ClanWas = WS ? WS->GetSelfClanId() : -1;    // свой клан
	TArray<int32> UsedCells;                                // ячейки контейнера 2, занятые прогоном

	// Тестовые номера: младшее слово charId (0xCDEF) заведомо ≠ userNum — путаница видна по первому слову.
	constexpr uint16 kMe = 321;                 // свой userNum
	constexpr int32 kCharId = 0x00ABCDEF;       // свой charId
	Net->SelfUserNum = kMe; Net->bSelfUserNumKnown = true;
	Net->SelectedCharId = kCharId;
	WM->ClearMsgBoxQueue(TEXT("самотест charId: старт"));

	// Свободная ячейка контейнера 2 (−1 — нет); занятую прогоном запоминаем для очистки.
	auto FreeCell = [&]() -> int32
	{
		for (int32 c = 0; c < Inv->GetContainerSize(2); ++c)
		{
			if (Inv->GetCellItemCode(2, c) == 0 && !UsedCells.Contains(c)) { return c; }
		}
		return -1;
	};
	auto Put = [&](int32 Code, int32 Amount) -> int32
	{
		const int32 c = FreeCell();
		if (c >= 0 && Inv->DebugPutItem(2, c, Code, Amount)) { UsedCells.Add(c); return c; }
		return -1;
	};
	// Окно (0, 24) с текстом loc Id на верху очереди.
	auto Box24 = [&](int32 Id) { return WM->IsMsgBoxQueued(0, 24) && WM->GetMsgBoxQueueTopText() == AsdaNotice::Loc(GI, Id); };

	// ════════════════ A. 6059 — код 30038 ════════════════
	Net->ResetEventNpcDayThrottle();
	Net->EventNpcDayOverrideForTest = 15;
	Expect(Dlg->OpenForNpcId(107), TEXT("окно разговора с NPC 107 открыто (код 30038 идёт тем же диспетчером, что кнопка)"));
	{
		TArray<uint8> P;
		const TArray<FSent> S = Capture(Net, 0, [&]() { Dlg->RunCommandForCheck(30038, 0); });
		const int32 N = CountOp(S, 6059, &P);
		Expect(N == 1 && P.Num() == 6 && W16(P, 0) == kMe && W16(P, 2) == 107 && W16(P, 4) == 1,
			FString::Printf(TEXT("A1 30038 → 6059 ×%d, тело %d б, слова %d/%d/%d (ждём ×1, 6 б, %d/107/1)"), N, P.Num(), W16(P, 0), W16(P, 2), W16(P, 4), kMe));
		Expect(W16(P, 0) != (kCharId & 0xFFFF), FString::Printf(TEXT("A1 контроль: слово +0 ≠ младшее слово charId (0x%04X)"), kCharId & 0xFFFF));
	}
	WM->ClearMsgBoxQueue(TEXT("самотест charId: A2"));
	{
		const TArray<FSent> S = Capture(Net, 0, [&]() { Dlg->RunCommandForCheck(30038, 0); });
		Expect(CountOp(S, 6059) == 0 && Box24(7501),
			FString::Printf(TEXT("A2 повтор в тот же день → 6059 ×%d, окно (0, 24) «%s» (ждём ×0 и 7501)"), CountOp(S, 6059), *WM->GetMsgBoxQueueTopText()));
	}
	WM->ClearMsgBoxQueue(TEXT("самотест charId: A3"));
	Net->EventNpcDayOverrideForTest = 16;
	{
		const TArray<FSent> S = Capture(Net, 0, [&]() { Dlg->RunCommandForCheck(30038, 0); });
		Expect(CountOp(S, 6059) == 1 && WM->GetMsgBoxQueueNum() == 0,
			FString::Printf(TEXT("A3 контроль: на другой день 6059 ×%d, окон %d (ждём ×1, 0)"), CountOp(S, 6059), WM->GetMsgBoxQueueNum()));
	}
	{
		// 6098 пишет общее поле цели — отказ 6059 по прежней цели снимается (эталон network.c:23434).
		Net->LastNpcExchangeSendTime = 0.0;
		const TArray<FSent> S = Capture(Net, 0, [&]() { Net->SendNpcExchange6098(kMe, 107, 3); });
		Expect(CountOp(S, 6098) == 1 && Net->EventNpcLastTarget == 3,
			FString::Printf(TEXT("A4 6098 ×%d, общая цель стала %d (ждём ×1, 3)"), CountOp(S, 6098), Net->EventNpcLastTarget));
		const TArray<FSent> S2 = Capture(Net, 0, [&]() { Dlg->RunCommandForCheck(30038, 0); });
		Expect(CountOp(S2, 6059) == 1, FString::Printf(TEXT("A4 после 6098 тот же день: 6059 ×%d (ждём ×1 — цель перезаписана)"), CountOp(S2, 6059)));
	}
	WM->ClearMsgBoxQueue(TEXT("самотест charId: A5"));
	Net->ResetEventNpcDayThrottle();
	Net->bSelfUserNumKnown = false;
	{
		const TArray<FSent> S = Capture(Net, 0, [&]() { Dlg->RunCommandForCheck(30038, 0); });
		Expect(CountOp(S, 6059) == 0 && WM->GetMsgBoxQueueNum() == 0 && Net->EventNpcDayArmed == 0,
			FString::Printf(TEXT("A5 без своего персонажа: 6059 ×%d, окон %d, отказ не взведён=%d (ждём 0/0/1 — эталон LABEL_133)"),
				CountOp(S, 6059), WM->GetMsgBoxQueueNum(), Net->EventNpcDayArmed == 0 ? 1 : 0));
	}
	Net->bSelfUserNumKnown = true;

	// ════════════════ E. 6091 (30043) и 6146 (30071) — свой userNum ════════════════
	for (int32 k = 0; k < 5; ++k) { Put(20977 + k, 1); }
	{
		TArray<uint8> P;
		const TArray<FSent> S = Capture(Net, 0, [&]() { Dlg->RunCommandForCheck(30043, 0); });
		const int32 N = CountOp(S, 6091, &P);
		Expect(N == 1 && W16(P, 0) == kMe && W16(P, 2) == 107 && W16(P, 4) == 1,
			FString::Printf(TEXT("E1 30043 → 6091 ×%d, слова %d/%d/%d (ждём ×1, %d/107/1; прежде +0 = цель −1)"), N, W16(P, 0), W16(P, 2), W16(P, 4), kMe));
	}
	{
		TArray<uint8> P;
		const TArray<FSent> S = Capture(Net, 0, [&]() { Dlg->RunCommandForCheck(30071, 0); });
		const int32 N = CountOp(S, 6146, &P);
		Expect(N == 1 && P.Num() == 27 && P[0] == 0 && W16(P, 1) == kMe && D32(P, 3) == 259,
			FString::Printf(TEXT("E2 30071 → 6146 ×%d, режим %d, userNum %d, первая константа %d (ждём ×1, 0, %d, 259)"),
				N, P.Num() ? (int32)P[0] : -1, W16(P, 1), D32(P, 3), kMe));
	}
	Net->bSelfUserNumKnown = false;
	{
		const TArray<FSent> S = Capture(Net, 0, [&]() { Dlg->RunCommandForCheck(30071, 0); Dlg->RunCommandForCheck(30043, 0); });
		Expect(CountOp(S, 6146) == 0 && CountOp(S, 6091) == 0,
			FString::Printf(TEXT("E3 контроль: без своего персонажа 6146 ×%d, 6091 ×%d (ждём 0/0)"), CountOp(S, 6146), CountOp(S, 6091)));
	}
	Net->bSelfUserNumKnown = true;

	// ════════════════ B. 6124 — charId и пустые записи ════════════════
	const int32 CellA = Put(20966, 1);   // одна ячейка — у записи 1 совпадение, остальные 5 пустые
	{
		TArray<uint8> P;
		const TArray<FSent> S = Capture(Net, 6124, [&]() { Net->SendRaidMonsterCall6124(Net->SelectedCharId, 20966, 6); });
		const int32 N = CountOp(S, 6124, &P);
		bool bTail = P.Num() == 46;
		for (int32 e = 1; e < 6 && bTail; ++e)
		{
			bTail = D32(P, 4 + 7 * e) == -1 && P[4 + 7 * e + 4] == 0 && (int16)W16(P, 4 + 7 * e + 5) == -1;
		}
		Expect(N == 1 && P.Num() == 46 && D32(P, 0) == kCharId,
			FString::Printf(TEXT("B1 отправитель 6124 ×%d, тело %d б, +0 = %d (ждём ×1, 46, charId %d)"), N, P.Num(), D32(P, 0), kCharId));
		Expect(P.Num() == 46 && D32(P, 4) == 20966 && P[8] == 2 && W16(P, 9) == CellA,
			FString::Printf(TEXT("B1 запись 0: код %d, контейнер %d, слот %d (ждём 20966, 2, %d)"), D32(P, 4), P.Num() > 8 ? (int32)P[8] : -1, W16(P, 9), CellA));
		Expect(bTail, TEXT("B1 записи 1…5 пустые = {−1, 0, −1} (эталон sub_973DF0; нулевой код сервер читал как ячейку)"));
	}
	// Докладываем до 6 штук (стопка или 6 ячеек — как считает CountItemById) и жмём 30061 тем же диспетчером.
	while (Inv->CountItemById(20966) < 6 && Put(20966, 5) >= 0) {}
	{
		TArray<uint8> P;
		const TArray<FSent> S = Capture(Net, 6124, [&]() { Dlg->RunCommandForCheck(30061, 0); });
		const int32 N = CountOp(S, 6124, &P);
		Expect(N == 1 && D32(P, 0) == kCharId && D32(P, 0) != (int32)kMe,
			FString::Printf(TEXT("B2 30061 → 6124 ×%d, +0 = %d (ждём ×1, charId %d, не userNum %d; предметов 20966: %d)"),
				N, D32(P, 0), kCharId, kMe, Inv->CountItemById(20966)));
	}

	// ════════════════ C. 6125 — приём ответа на призыв ════════════════
	auto Feed6125 = [&](int32 CharId, uint8 Code, int32 Code0, int32 Slot0, int32 Code1, int32 Slot1, int32 Weight)
	{
		TArray<uint8> B; B.SetNumZeroed(51);
		AsdaBytes::WrI32(B.GetData(), CharId);
		for (int32 e = 0; e < 6; ++e) { AsdaBytes::WrI32(B.GetData() + 4 + 7 * e, -1); AsdaBytes::WrI16(B.GetData() + 4 + 7 * e + 5, -1); }
		if (Code0) { AsdaBytes::WrI32(B.GetData() + 4, Code0); B[8] = 2; AsdaBytes::WrI16(B.GetData() + 9, Slot0); }
		if (Code1) { AsdaBytes::WrI32(B.GetData() + 11, Code1); B[15] = 2; AsdaBytes::WrI16(B.GetData() + 16, Slot1); }
		B[46] = Code;
		AsdaBytes::WrI32(B.GetData() + 47, Weight);
		Net->DebugFeedPacket(6125, B);
	};
	WM->ClearMsgBoxQueue(TEXT("самотест charId: C1"));
	Feed6125(999, 1, 0, 0, 0, 0, 0);
	Expect(Box24(133), FString::Printf(TEXT("C1 6125 чужой charId → 133 «%s»"), *WM->GetMsgBoxQueueTopText()));
	const struct { uint8 Code; int32 Loc; } kC[] = { { 0, 98104 }, { 3, 98104 }, { 4, 98104 }, { 5, 98104 }, { 2, 133 }, { 6, 98106 } };
	for (const auto& X : kC)
	{
		WM->ClearMsgBoxQueue(TEXT("самотест charId: C2"));
		Feed6125(kCharId, X.Code, 0, 0, 0, 0, 0);
		Expect(Box24(X.Loc), FString::Printf(TEXT("C2 6125 код %d → нотис %d «%s»"), X.Code, X.Loc, *WM->GetMsgBoxQueueTopText()));
	}
	// ★ 09.11.2026 [ИСПРАВЛЕНИЕ]: коды без строки (сервер шлёт 7/8/9/10) → 98104; при asda.Raid.CallUnknownCodeAsFail 0 — номер, как эталон.
	{
		IConsoleVariable* CvUnk = IConsoleManager::Get().FindConsoleVariable(TEXT("asda.Raid.CallUnknownCodeAsFail"));
		const int32 SavedUnk = CvUnk ? CvUnk->GetInt() : 1;   // вернуть после проверки
		const uint8 kUnknown[] = { 7, 8, 9, 10, 11, 255 };
		for (const uint8 U : kUnknown)
		{
			WM->ClearMsgBoxQueue(TEXT("самотест charId: C3"));
			Feed6125(kCharId, U, 0, 0, 0, 0, 0);
			Expect(Box24(98104), FString::Printf(TEXT("C3 6125 код %d → нотис 98104 «%s» [ИСПРАВЛЕНИЕ]"), U, *WM->GetMsgBoxQueueTopText()));
		}
		if (CvUnk) { CvUnk->Set(0, ECVF_SetByConsole); }
		WM->ClearMsgBoxQueue(TEXT("самотест charId: C3e"));
		Feed6125(kCharId, 9, 0, 0, 0, 0, 0);
		Expect(CvUnk && WM->IsMsgBoxQueued(0, 24) && WM->GetMsgBoxQueueTopText() == TEXT("9"),
			FString::Printf(TEXT("C3e ключ 0: код 9 → текст «%s» (ждём «9», эталон sprintf(\"%%d\"))"), *WM->GetMsgBoxQueueTopText()));
		if (CvUnk) { CvUnk->Set(SavedUnk, ECVF_SetByConsole); }
	}
	{
		const int32 CellB = Put(20966, 1);   // вторая ячейка — запись с ЧУЖИМ кодом на неё указывает, снята быть не должна
		WM->ClearMsgBoxQueue(TEXT("самотест charId: C4"));
		Feed6125(kCharId, 1, 20966, CellA, 20967, CellB, 777);
		Expect(Inv->GetCellItemCode(2, CellA) == 0, FString::Printf(TEXT("C4 код 1: совпавшая ячейка %d снята (код теперь %d)"), CellA, Inv->GetCellItemCode(2, CellA)));
		Expect(CellB >= 0 && Inv->GetCellItemCode(2, CellB) == 20966,
			FString::Printf(TEXT("C4 контроль: ячейка %d с несовпавшим кодом записи осталась (%d)"), CellB, CellB >= 0 ? Inv->GetCellItemCode(2, CellB) : -1));
		Expect(Net->SelfWeightCur == 777 && Box24(98105),
			FString::Printf(TEXT("C4 вес %d (ждём 777), нотис «%s» (ждём 98105)"), Net->SelfWeightCur, *WM->GetMsgBoxQueueTopText()));
		// C5 контроль: тот же код и слот, но контейнер записи 3 — ячейка контейнера 2 остаётся (эталон сверяет и тип).
		const int32 CellC = Put(20966, 1);   // ячейка с тем же кодом
		{
			TArray<uint8> Bc; Bc.SetNumZeroed(51);   // тело 6125
			AsdaBytes::WrI32(Bc.GetData(), kCharId);
			for (int32 e = 0; e < 6; ++e) { AsdaBytes::WrI32(Bc.GetData() + 4 + 7 * e, -1); AsdaBytes::WrI16(Bc.GetData() + 4 + 7 * e + 5, -1); }
			AsdaBytes::WrI32(Bc.GetData() + 4, 20966); Bc[8] = 3; AsdaBytes::WrI16(Bc.GetData() + 9, CellC);   // контейнер 3 ≠ 2
			Bc[46] = 1; AsdaBytes::WrI32(Bc.GetData() + 47, 779);
			WM->ClearMsgBoxQueue(TEXT("самотест charId: C5"));
			Net->DebugFeedPacket(6125, Bc);
		}
		Expect(CellC >= 0 && Inv->GetCellItemCode(2, CellC) == 20966,
			FString::Printf(TEXT("C5 контроль: тот же код/слот, контейнер 3 → ячейка %d осталась (код %d)"), CellC, CellC >= 0 ? Inv->GetCellItemCode(2, CellC) : -1));
	}

	// ════════════════ D. 6060 — приём ответа событийного NPC ════════════════
	auto Feed6060 = [&](uint16 U, uint8 Code)
	{
		TArray<uint8> B; B.SetNumZeroed(3);
		AsdaBytes::WrI16(B.GetData(), U);
		B[2] = Code;
		Net->DebugFeedPacket(6060, B);
	};
	const int32 kD[] = { 0, 7500, 7501, 7502, 7502, 7503, 7504, 7502, 7505, 7502, 0 };   // код 0…10 → loc (0 — молча)
	for (int32 c = 0; c <= 10; ++c)
	{
		WM->ClearMsgBoxQueue(TEXT("самотест charId: D"));
		Feed6060(kMe, (uint8)c);
		if (kD[c]) { Expect(Box24(kD[c]), FString::Printf(TEXT("D 6060 код %d → нотис %d «%s»"), c, kD[c], *WM->GetMsgBoxQueueTopText())); }
		else       { Expect(WM->GetMsgBoxQueueNum() == 0, FString::Printf(TEXT("D 6060 контроль: код %d → молча (окон %d)"), c, WM->GetMsgBoxQueueNum())); }
	}
	WM->ClearMsgBoxQueue(TEXT("самотест charId: D чужой"));
	Feed6060((uint16)(kMe + 1), 1);
	Expect(Box24(133), FString::Printf(TEXT("D 6060 чужой userNum, код 1 → 133 «%s» (не 7500)"), *WM->GetMsgBoxQueueTopText()));
	WM->ClearMsgBoxQueue(TEXT("самотест charId: D без персонажа"));
	Net->bSelfUserNumKnown = false;
	Feed6060(kMe, 1);
	Expect(WM->GetMsgBoxQueueNum() == 0, TEXT("D 6060 без своего персонажа → молча"));
	Net->bSelfUserNumKnown = true;

	// ════════════════ F. @0 = charId у 4136 / 4138 / 4050 / 6586, шапка аукциона @5, клан-чат @2 ════════════════
	{
		TArray<uint8> P;
		TArray<FSent> S = Capture(Net, 0, [&]() { Net->SendClanIncrease4136(7); });
		CountOp(S, 4136, &P);
		Expect(D32(P, 0) == kCharId, FString::Printf(TEXT("F 4136 @0 = %d (ждём charId %d)"), D32(P, 0), kCharId));
		S = Capture(Net, 0, [&]() { Net->SendClanRosterRefresh4138(); });
		CountOp(S, 4138, &P);
		Expect(D32(P, 0) == kCharId, FString::Printf(TEXT("F 4138 @0 = %d (ждём charId %d)"), D32(P, 0), kCharId));
		S = Capture(Net, 0, [&]() { Net->SendStoreHousePageReq4050(1); });
		CountOp(S, 4050, &P);
		Expect(D32(P, 0) == kCharId, FString::Printf(TEXT("F 4050 @0 = %d (ждём charId %d)"), D32(P, 0), kCharId));
		S = Capture(Net, 0, [&]() { Net->SendConstellationSelect6586(3); });
		CountOp(S, 6586, &P);
		Expect(D32(P, 0) == kCharId, FString::Printf(TEXT("F 6586 @0 = %d (ждём charId %d)"), D32(P, 0), kCharId));
		S = Capture(Net, 0, [&]() { Net->SendExchangeMyLots9907(0); });
		const int32 N9907 = CountOp(S, 9907, &P);
		Expect(N9907 == 1 && P.Num() == 16 && D32(P, 5) == kCharId,
			FString::Printf(TEXT("F 9907 ×%d, тело %d б, шапка @5 = %d (ждём ×1, 16, charId %d)"), N9907, P.Num(), D32(P, 5), kCharId));
	}
	if (Chat)
	{
		Net->OnClanInfo.Broadcast(-1, 7, TEXT("SelfTest"), FString());
		const int32 StateWas = Chat->GetInputState();
		TArray<uint8> P;
		const TArray<FSent> S = Capture(Net, 0, [&]() { Chat->SetInputState(6); Chat->DebugSubmit(TEXT("клан")); });
		const int32 N = CountOp(S, 4120, &P);
		Expect(N == 1 && D32(P, 2) == kCharId,
			FString::Printf(TEXT("F 4120 (Enter в клан-чате) ×%d, @2 = %d (ждём ×1, charId %d; прежде userNum %d)"), N, D32(P, 2), kCharId, kMe));
		Chat->SetInputState(StateWas);
		Net->OnClanInfo.Broadcast(-1, ClanWas, FString(), FString());
	}
	else
	{
		Expect(false, TEXT("F 4120: нет подсистемы окна чата"));
	}

	// ════════════════ G. 6096 — вскрытие коробки: @0 свой userNum, @2 charId; без своего персонажа — молча ════════════════
	{
		constexpr int32 kBox = 20816;   // коробка с составом в packagetable (пример `asda.Inven.Package 20816`)
		const int32 BoxCell = Put(kBox, 1);
		TArray<uint8> P;
		bool bRet = false;   // возврат отправителя
		TArray<FSent> S = Capture(Net, 6096, [&]() { bRet = Inv->SendOpenPackage6096(kBox, 2, BoxCell); });
		const int32 N = CountOp(S, 6096, &P);
		Expect(BoxCell >= 0 && N == 1 && W16(P, 0) == kMe && D32(P, 2) == kCharId,
			FString::Printf(TEXT("G1 6096 ×%d, @0 = %d, @2 = %d (ждём ×1, userNum %d, charId %d; ячейка %d)"), N, W16(P, 0), D32(P, 2), kMe, kCharId, BoxCell));
		Net->bSelfUserNumKnown = false;
		S = Capture(Net, 6096, [&]() { bRet = Inv->SendOpenPackage6096(kBox, 2, BoxCell); });
		Expect(CountOp(S, 6096) == 0 && !bRet,
			FString::Printf(TEXT("G2 без своего персонажа: 6096 ×%d, возврат %d (ждём 0/0 — эталон `if (pLocalPlayer)` молчит)"), CountOp(S, 6096), bRet ? 1 : 0));
		Net->bSelfUserNumKnown = true;
	}

	// ════════════════ K. 6126 — «корона» владельца добычи рейд-босса ════════════════
	UAsdaKingCrownSubsystem* Crown = GI->GetSubsystem<UAsdaKingCrownSubsystem>();
	if (Crown && WS)
	{
		// Свой павн нужен ветке «найден» (`CharManager_FindByCharId`). Офлайн — штатный крюк 4003.
		if (!::IsValid(WS->LocalPlayerActor.Get()) && GEngine)
		{
			GEngine->Exec(W, TEXT("Asda.Load.CurrentMap 3"));
			WS->DebugSpawnSelfAt(256, 256);
		}
		Net->SelfUserNum = kMe; Net->bSelfUserNumKnown = true; Net->SelectedCharId = kCharId;
		Expect(::IsValid(WS->LocalPlayerActor.Get()), TEXT("K подготовка: свой павн в мире"));
		Crown->DebugResetAll();
		Expect(Crown->IsGateOpen(), TEXT("K0 гейт: DT_CallMob запись 0, SpawnArgByte == 1 (эталон *(dword_34A0490+16) == 1)"));
		auto Feed6126 = [&](int32 CharId, uint8 Byte)
		{
			TArray<uint8> B; B.SetNumZeroed(5);   // тело 6126
			AsdaBytes::WrI32(B.GetData(), CharId);
			B[4] = Byte;
			Net->DebugFeedPacket(6126, B);
		};
		bool bPend = false; int32 Vis = -1; AAsdaEffectActor* Fx = nullptr;   // состояние персонажа
		Feed6126(987654, 1);
		Expect(Crown->GetCrownOwnerCharId() == 987654 && Crown->GetCrownOwnerByte() == 1 && !Crown->GetCrownState(987654, bPend, Vis, Fx),
			FString::Printf(TEXT("K1 не найден → глобальный владелец %d, байт %u (ждём 987654, 1, записи нет)"), Crown->GetCrownOwnerCharId(), (uint32)Crown->GetCrownOwnerByte()));
		Crown->GateOverrideForTest = 0;
		Feed6126(555, 1);
		Expect(Crown->GetCrownOwnerCharId() == 987654, FString::Printf(TEXT("K2 контроль: гейт закрыт → пакет пропущен, владелец %d"), Crown->GetCrownOwnerCharId()));
		Crown->GateOverrideForTest = -1;
		Feed6126(kCharId, 1);
		const bool bHas = Crown->GetCrownState(kCharId, bPend, Vis, Fx);
		Expect(bHas && bPend && Vis == 1 && Fx == nullptr && Crown->GetCrownOwnerCharId() == 987654,
			FString::Printf(TEXT("K3 свой найден: ждёт=%d видимость=%d эффект=%d, владелец не тронут %d (ждём 1/1/0)"), bPend ? 1 : 0, Vis, Fx ? 1 : 0, Crown->GetCrownOwnerCharId()));
		Crown->UpdateAllCrowns();
		Crown->GetCrownState(kCharId, bPend, Vis, Fx);
		AAsdaEffectActor* Fx1 = Fx;   // первый эффект
		Expect(!bPend && Fx1 && !Fx1->IsHidden(),
			FString::Printf(TEXT("K4 тик: ждёт=%d, эффект king_cap01 %s, скрыт=%d (ждём 0, есть, 0)"), bPend ? 1 : 0, Fx1 ? TEXT("есть") : TEXT("НЕТ (BP_Fx_king_cap01 не собран?)"), Fx1 && Fx1->IsHidden() ? 1 : 0));
		Crown->UpdateAllCrowns();
		Crown->GetCrownState(kCharId, bPend, Vis, Fx);
		Expect(Fx == Fx1, TEXT("K4 контроль: второй тик эффект не пересоздаёт (+0x33088 >= 0)"));
		// Ветка 2) — глобальный владелец = свой charId, записи «ждёт» нет.
		auto GlobalSelf = [&](int32 CvarValue) -> AAsdaEffectActor*
		{
			IConsoleVariable* Cv = IConsoleManager::Get().FindConsoleVariable(TEXT("asda.Crown.GlobalUsesStoredByte"));
			if (Cv) { AsdaCVarSave::SetForTest(TEXT("asda.Crown.GlobalUsesStoredByte"), CvarValue); }
			Crown->DebugResetAll();
			Net->SelectedCharId = kCharId;
			Feed6126(kCharId + 1, 1);            // пока свой charId другой → «не найден»
			Net->SelectedCharId = kCharId + 1;   // теперь свой = владелец
			Crown->UpdateAllCrowns();
			bool bP = false; int32 V = -1; AAsdaEffectActor* F = nullptr;
			Crown->GetCrownState(kCharId + 1, bP, V, F);
			if (Cv) { AsdaCVarSave::SetForTest(TEXT("asda.Crown.GlobalUsesStoredByte"), 1); }
			return F;
		};
		AAsdaEffectActor* FxG = GlobalSelf(1);
		Expect(FxG && !FxG->IsHidden(), FString::Printf(TEXT("K5 глобальный владелец = свой: эффект %s, скрыт=%d (ждём есть/0 — [ИСПРАВЛЕНИЕ] видимость из байта)"), FxG ? TEXT("есть") : TEXT("НЕТ"), FxG && FxG->IsHidden() ? 1 : 0));
		AAsdaEffectActor* FxO = GlobalSelf(0);
		Expect(FxO && FxO->IsHidden(), FString::Printf(TEXT("K6 откат asda.Crown.GlobalUsesStoredByte 0: эффект %s, скрыт=%d (ждём есть/1 — как оригинал: +0x33094 = 0)"), FxO ? TEXT("есть") : TEXT("НЕТ"), FxO && FxO->IsHidden() ? 1 : 0));
		// Сбросы: смена карты гасит эффект, свои поля и владелец живут; разрыв — всё к старту.
		Net->SelectedCharId = kCharId;
		Crown->DebugResetAll();
		Feed6126(kCharId, 1);
		Feed6126(424242, 1);
		Crown->ResetSessionState(EAsdaSessionResetReason::MapChanged);
		Crown->GetCrownState(kCharId, bPend, Vis, Fx);
		Expect(bPend && Vis == 1 && !Fx && Crown->GetCrownOwnerCharId() == 424242,
			FString::Printf(TEXT("K7 смена карты: свой ждёт=%d видимость=%d, владелец %d (ждём 1/1/424242)"), bPend ? 1 : 0, Vis, Crown->GetCrownOwnerCharId()));
		Crown->ResetSessionState(EAsdaSessionResetReason::Disconnected);
		Crown->GetCrownState(kCharId, bPend, Vis, Fx);
		Expect(!bPend && Vis == 0 && Crown->GetCrownOwnerCharId() == -1,
			FString::Printf(TEXT("K8 разрыв: ждёт=%d видимость=%d владелец %d (ждём 0/0/−1)"), bPend ? 1 : 0, Vis, Crown->GetCrownOwnerCharId()));
		// K9 выход в выбор персонажа: новый CCharacter у оригинала (questCreateAndRegister@0x9D1490) — свои поля к нулю,
		//   глобальный владелец (.data) жив. Контроль — K7 (смена карты свои поля сохраняет).
		Crown->DebugResetAll();
		Feed6126(kCharId, 1);
		Feed6126(424242, 1);
		Crown->ResetSessionState(EAsdaSessionResetReason::LeftWorld);
		Crown->GetCrownState(kCharId, bPend, Vis, Fx);
		Expect(!bPend && Vis == 0 && !Fx && Crown->GetCrownOwnerCharId() == 424242,
			FString::Printf(TEXT("K9 выход в выбор персонажа: свой ждёт=%d видимость=%d, владелец %d (ждём 0/0/424242)"), bPend ? 1 : 0, Vis, Crown->GetCrownOwnerCharId()));
		// K10–K12 чужой персонаж: фейковый сосед штатным путём снапшота (charId = 900000 + userNum), корона по «найден»,
		//   уход из мира 4011 гасит его корону (аналог CharManager_RemoveNode); своя при этом цела (контроль).
		Crown->DebugResetAll();
		constexpr int32 kPeerNum = 777;                 // userNum соседа
		constexpr int32 kPeerCharId = 900000 + kPeerNum; // charId соседа (DebugSpawnFakePeer)
		WS->DebugSpawnFakePeer(kPeerNum, TEXT("КоронаСосед"), FVector2D(25700.f, 25700.f));
		Expect(WS->FindPlayerActorByCharId(kPeerCharId) != nullptr, TEXT("K10 подготовка: сосед в реестре мира по charId"));
		Feed6126(kPeerCharId, 1);
		Feed6126(kCharId, 1);
		Crown->UpdateAllCrowns();
		AAsdaEffectActor* PeerFx = nullptr; AAsdaEffectActor* SelfFx = nullptr;
		bool bPeerRec = Crown->GetCrownState(kPeerCharId, bPend, Vis, PeerFx);
		Crown->GetCrownState(kCharId, bPend, Vis, SelfFx);
		Expect(bPeerRec && PeerFx && !PeerFx->IsHidden() && SelfFx && PeerFx != SelfFx && Crown->GetCrownOwnerCharId() == -1,
			FString::Printf(TEXT("K10 чужой найден: запись=%d эффект=%d скрыт=%d, свой эффект отдельный=%d, владелец %d (ждём 1/1/0/1/−1)"),
				bPeerRec ? 1 : 0, PeerFx ? 1 : 0, PeerFx && PeerFx->IsHidden() ? 1 : 0, (SelfFx && PeerFx != SelfFx) ? 1 : 0, Crown->GetCrownOwnerCharId()));
		TWeakObjectPtr<AAsdaEffectActor> PeerFxW = PeerFx;   // эффект соседа
		{
			TArray<uint8> B; B.SetNumZeroed(2);   // тело 4011: renderId i16 = userNum игрока
			AsdaBytes::WrI16(B.GetData(), kPeerNum);
			Net->DebugFeedPacket(4011, B);
		}
		Crown->UpdateAllCrowns();
		bPeerRec = Crown->GetCrownState(kPeerCharId, bPend, Vis, PeerFx);
		Expect(!bPeerRec && (!PeerFxW.IsValid() || PeerFxW->IsActorBeingDestroyed()),
			FString::Printf(TEXT("K11 уход соседа (4011): запись=%d, эффект жив=%d (ждём 0/0 — RemoveNode гасит)"),
				bPeerRec ? 1 : 0, (PeerFxW.IsValid() && !PeerFxW->IsActorBeingDestroyed()) ? 1 : 0));
		Crown->GetCrownState(kCharId, bPend, Vis, Fx);
		Expect(Fx == SelfFx && Fx && !Fx->IsHidden(), TEXT("K12 контроль: своя корона после ухода соседа цела"));
		Crown->DebugResetAll();
		Net->SelectedCharId = kCharId;
	}
	else
	{
		Expect(false, TEXT("K: нет подсистемы короны или мира"));
	}

	// ════════════════ S. «Это я?» — дедуп №2, В2-A (N2 / N4 / U5b / U5d), 11.10.2026 ════════════════
	//   S1 charId 0 · S2 свой павн по charId (N4) · S3 userNum 0 · S4 устаревшая запись NetPlayers с МОИМ номером (N4) ·
	//   S5 класс ≠ профессия (U5d) · S6 исходящий 5105 без своего номера (U5b). Ожидания — по эталону (см. `AsdaSelf.h`).
	if (WS)
	{
		AActor* Me = WS->LocalPlayerActor.Get();
		Expect(::IsValid(Me), TEXT("S подготовка: свой павн в мире"));
		// S1: charId 0 — свой ещё не выбран.
		Net->SelectedCharId = 0;
		Expect(!AsdaSelf::IsSelfCharId(Net, 0) && AsdaSelf::IsSelfCharId(Net, 0, AsdaSelf::EAsdaUnknownSelf::AcceptAll) && !AsdaSelf::IsSelfCharId(Net, 5),
			TEXT("S1 charId 0: «мой?» Reject → нет (было 0 == 0 → да), AcceptAll → да, чужой 5 → нет"));
		Expect(WS->ActorByCharId(0) == nullptr && WS->GetPlayerActorByCharId(0) == nullptr && WS->FindPlayerActorByCharId(0) == nullptr,
			TEXT("S1 charId 0: актора нет ни у одной из трёх функций"));
		AsdaCVarSave::SetForTest(TEXT("asda.Self.StrictUnknownNum"), 0);
		Expect(AsdaSelf::IsSelfCharId(Net, 0), TEXT("S1 ключ отката asda.Self.StrictUnknownNum 0: открытое сравнение 0 == 0 → «мой» (как было)"));
		AsdaCVarSave::SetForTest(TEXT("asda.Self.StrictUnknownNum"), 1);
		// S2: свой павн по charId.
		Net->SelectedCharId = kCharId;
		Expect(AsdaSelf::IsSelfCharId(Net, kCharId) && !AsdaSelf::IsSelfCharId(Net, kCharId + 1), TEXT("S2 свой charId → мой, соседний → нет"));
		Expect(WS->GetPlayerActorByCharId(kCharId) == Me && WS->FindPlayerActorByCharId(kCharId) == Me && WS->ActorByCharId(kCharId) == Me,
			TEXT("S2 N4: свой павн по charId находят все три функции (GetPlayerActorByCharId раньше себя не находил)"));
		AsdaCVarSave::SetForTest(TEXT("asda.World.CharIdLookupSelf"), 0);
		Expect(WS->GetPlayerActorByCharId(kCharId) == nullptr, TEXT("S2 ключ отката asda.World.CharIdLookupSelf 0: прежнее — себя не находит"));
		AsdaCVarSave::SetForTest(TEXT("asda.World.CharIdLookupSelf"), 1);
		// S3: userNum 0 — законный номер (сервер выдаёт его штатно).
		Net->SelfUserNum = 0; Net->bSelfUserNumKnown = true;
		Expect(AsdaSelf::IsSelfUserIdx(Net, 0) && AsdaSelf::GetUserIdxOr(Net, -1) == 0 && WS->GetPlayerActorByUserNum(0) == Me && WS->ActorByUserIdx(0) == Me,
			TEXT("S3 userNum 0 известен: «я» = 0, GetUserIdxOr = 0, актор по номеру 0 — свой"));
		Net->bSelfUserNumKnown = false;
		Expect(!AsdaSelf::IsSelfUserIdx(Net, 0) && AsdaSelf::GetUserIdxOr(Net, -1) == -1 && WS->GetPlayerActorByUserNum(0) == nullptr,
			TEXT("S3 userNum неизвестен (до 4003): номер 0 не «я», GetUserIdxOr = −1, актора по 0 нет"));
		// S6 (U5b): без своего номера 5105 (чат отряда) не уходит — раньше уходил с номером 0 (чужого игрока).
		//   ★ 12.10.2026 (чат отряда оживлён): поле @0 пакета 5105 — индекс ОТРЯДА, а не свой userNum; ×0 здесь даёт гейт
		//   «в отряде» (`Party_IsInParty`, офлайн — не в отряде). Положительный и отрицательный контроль — `asda.Party.ChatSelfTest`.
		if (Chat)
		{
			const int32 StateWas = Chat->GetInputState();
			const TArray<FSent> S = Capture(Net, 0, [&]() { Chat->SetInputState(5); Chat->DebugSubmit(TEXT("отряд")); });
			Expect(CountOp(S, 5105) == 0, FString::Printf(TEXT("S6: вне отряда (номер неизвестен) → 5105 ×%d (ждём ×0; гейт отряда)"), CountOp(S, 5105)));
			Chat->SetInputState(StateWas);
		}
		// S4: устаревшая запись соседа с МОИМ номером (сосед ушёл, 4011 потерян, мне выдали его номер).
		Net->SelfUserNum = kMe; Net->bSelfUserNumKnown = true;
		WS->DebugSpawnFakePeer(kMe, TEXT("УстаревшийСосед"), FVector2D(25800.f, 25800.f));
		AActor* Stale = WS->ActorByCharId(900000 + kMe);   // фейковый сосед: charId = 900000 + userNum
		Expect(Stale && Stale != Me, TEXT("S4 подготовка: в NetPlayers запись соседа с моим userNum"));
		Expect(WS->GetPlayerActorByUserNum(kMe) == Me && WS->ActorByUserIdx(kMe) == Me,
			TEXT("S4 N4: актор по моему номеру — СВОЙ павн, а не устаревший сосед (было: GetPlayerActorByUserNum отдавал соседа)"));
		AsdaCVarSave::SetForTest(TEXT("asda.World.UserIdxLookupSelfFirst"), 0);
		Expect(Stale && WS->GetPlayerActorByUserNum(kMe) == Stale, TEXT("S4 ключ отката asda.World.UserIdxLookupSelfFirst 0: прежнее — отдаёт соседа"));
		AsdaCVarSave::SetForTest(TEXT("asda.World.UserIdxLookupSelfFirst"), 1);
		{
			TArray<uint8> B; B.SetNumZeroed(2);   // тело 4011: renderId i16 = userNum — сосед уходит
			AsdaBytes::WrI16(B.GetData(), kMe);
			Net->DebugFeedPacket(4011, B);
		}
		// S5 (U5d): класс = байт +0x438 (`SelfClassId`), до него — запись выбранного персонажа (rec+24), НЕ профессия `SelfJob`.
		const int32 ClsWas = Net->SelfClassId, JobWas = Net->SelfJob;
		const TArray<FAsdaCharRecord> ListWas = Net->CharList;
		Net->SelfClassId = -1; Net->SelfJob = 7; Net->CharList.Reset();
		Expect(AsdaSelf::GetClassId(Net) == -1, FString::Printf(TEXT("S5 класс неизвестен, записи нет → −1 (профессия 7 НЕ подставляется), получено %d"), AsdaSelf::GetClassId(Net)));
		FAsdaCharRecord Rec; Rec.Slot = 0; Rec.RawSlotId = Net->LastSelectedRawSlot; Rec.Job = 23;
		Net->CharList.Add(Rec);
		Expect(AsdaSelf::GetClassId(Net) == 23, FString::Printf(TEXT("S5 класс до 4005 — запись выбранного персонажа rec+24 = 23, получено %d"), AsdaSelf::GetClassId(Net)));
		Net->SelfClassId = 13;
		Expect(AsdaSelf::GetClassId(Net) == 13, TEXT("S5 класс пришёл (+0x438 = 13) — берётся он"));
		Net->SelfClassId = ClsWas; Net->SelfJob = JobWas; Net->CharList = ListWas;
		// S7 (N7): IP из тела с хвостовыми пробелами. Общая точка `AsdaBytes::AnsiFieldTrim` (оракул — литерал) и путь 4055
		//   (воскрешение в другом городе; mapId = текущий ⇒ без смены карты, тайл прежний ⇒ нулевой телепорт). Строка лога
		//   «[воскрешение] … ip=10.0.0.1:15009 …» проверяется в логе прогона (до правки шла с хвостом «10.0.0.1   :15009»).
		{
			uint8 Ip[20] = {};
			FMemory::Memcpy(Ip, "10.0.0.1   ", 11);
			Expect(AsdaBytes::AnsiFieldTrim(Ip, 20) == TEXT("10.0.0.1"), TEXT("S7 N7: «10.0.0.1␣␣␣» + нули → «10.0.0.1» (общая точка разбора IP)"));
			uint8 Full[16]; FMemory::Memcpy(Full, "192.168.100.200 ", 16);   // поле без нуля — длина ограничена полем
			Expect(AsdaBytes::AnsiFieldTrim(Full, 16) == TEXT("192.168.100.200"), TEXT("S7 N7: поле без нуль-терминатора — длина по полю, хвост обрезан"));
			const int32 TileXWas = Net->SelfTileX, TileZWas = Net->SelfTileZ, MapWas = Net->EnterMapId;
			const int64 ExpWas = Net->SelfExp;   // 4055 несёт опыт @35 (здесь 0) — вернуть
			TArray<uint8> B; B.SetNumZeroed(43);
			AsdaBytes::WrI16(B.GetData(), kMe);
			B[2] = (uint8)FMath::Max(Net->EnterMapId, 0); Net->EnterMapId = (int32)B[2];
			FMemory::Memcpy(B.GetData() + 3, Ip, 20);
			B[23] = (uint8)(15009 & 0xFF); B[24] = (uint8)(15009 >> 8);
			AsdaBytes::WrI16(B.GetData() + 25, FMath::Max(TileXWas, 0));
			AsdaBytes::WrI16(B.GetData() + 27, FMath::Max(TileZWas, 0));
			AsdaBytes::WrI32(B.GetData() + 29, -1);   // HP −1 — зеркала витальности не трогаем
			UE_LOG(LogAsdaCharIdTest, Log, TEXT("САМОТЕСТ-CHARID S7 подача 4055 с IP «10.0.0.1␣␣␣» — см. строку «[воскрешение] … ip=…» ниже"));
			Net->DebugFeedPacket(4055, B);
			Net->SelfTileX = TileXWas; Net->SelfTileZ = TileZWas; Net->EnterMapId = MapWas; Net->SelfExp = ExpWas;
		}
		Net->SelfUserNum = kMe; Net->bSelfUserNumKnown = true; Net->SelectedCharId = kCharId;
	}
	else
	{
		Expect(false, TEXT("S: нет подсистемы мира"));
	}

	// ════════════════ H. Время жизни суточного отказа 6059 (последним: сброс сессии чистит зеркала) ════════════════
	{
		auto Arm = [&]() { Net->EventNpcDayArmed = 1; Net->EventNpcLastTarget = 3; Net->EventNpcDay = 16; };
		Arm();
		Net->ResetSessionState(EAsdaSessionResetReason::MapChanged);
		Expect(Net->EventNpcDayArmed == 1 && Net->EventNpcLastTarget == 3, TEXT("H1 смена карты: суточный отказ жив (g_pNpcWnd живёт весь процесс)"));
		Net->ResetSessionState(EAsdaSessionResetReason::LeftWorld);
		Expect(Net->EventNpcDayArmed == 1 && Net->EventNpcLastTarget == 3, TEXT("H2 выход в выбор персонажа: суточный отказ жив"));
		Net->ResetSessionState(EAsdaSessionResetReason::Disconnected);
		Expect(Net->EventNpcDayArmed == 0 && Net->EventNpcLastTarget == -1 && Net->EventNpcDay == -1,
			TEXT("H3 контроль: разрыв (= конец процесса оригинала) → к значениям конструктора 0/−1/−1"));
	}

	// ── Восстановление ──
	for (const int32 c : UsedCells) { Inv->DebugPutItem(2, c, 0, 0); }
	if (Dlg->IsDialogOpen()) { Dlg->Close(); }
	Net->SelfUserNum = UserNumWas; Net->bSelfUserNumKnown = bKnownWas;
	Net->SelectedCharId = CharIdWas;
	Net->EventNpcDayArmed = ArmedWas; Net->EventNpcLastTarget = TargetWas; Net->EventNpcDay = DayWas;
	Net->EventNpcDayOverrideForTest = 0;
	Net->LastNpcExchangeSendTime = ExchWas;
	Net->SelfWeightCur = WeightWas;
	WM->ClearMsgBoxQueue(TEXT("самотест charId: конец"));
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaCharIdSendersSelfTest(
	TEXT("asda.Net.CharIdSendersSelfTest"),
	TEXT("asda.Net.CharIdSendersSelfTest [delay=N] [quit] — ОФЛАЙН-самотест аудита charId/userNum: 6059 (+суточный отказ 7501), 6124/6125, 6060, 6091, 6146, 4136/4138/4050/6586/9907/4120"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		const FAsdaSelfTestArgs KitArgs = FAsdaSelfTestArgs::Parse(A);   // ★ 11.10.2026 (В3 S1): каркас — отказ с ИТОГ и quit, второй барьер на все опкоды
		if (AsdaSelfTestKit::RefuseStart(W, TEXT("asda.Net.CharIdSendersSelfTest"), KitArgs)) { return; }
		if (!W) { return; }
		float Delay = 0.f;   // задержка запуска, с
		bool bQuit = false;  // выйти после прогона
		for (const FString& Arg : A)
		{
			if (Arg.Equals(TEXT("quit"), ESearchCase::IgnoreCase)) { bQuit = true; }
		}
		Delay = AsdaSelfTestKit::DelayArgOr(A, Delay);   // ★ R5-fix2-c (13.11.2026): `delay=N` — общая точка (была ветка цикла аргументов)
		TWeakObjectPtr<UWorld> WW = W;
		auto Go = [WW, bQuit]()   // прогон и (по флагу) выход
		{
			UWorld* Wd = WW.Get();
			if (!Wd) { return; }
			if (AsdaSelfTestKit::RefuseDeferred(Wd, TEXT("asda.Net.CharIdSendersSelfTest"), bQuit)) { return; }   // Р-2: офлайн повторно — на отложенном старте
			FAsdaCharIdSendersSelfTest T;
			T.Run(Wd);
			// ★ ИСПРАВЛЕНИЕ 11.10.2026 (В0, N1(д)/U8): ключ Crown.GlobalUsesStoredByte — к исходному значению И приоритету SetBy (прежде K5/K6
			//   возвращали константу 1 с SetByConsole). После `Run` — и при любом раннем выходе из него.
			AsdaCVarSave::RestoreAll(TEXT("asda.Net.CharIdSendersSelfTest"));
			UE_LOG(LogAsdaCharIdTest, Log, TEXT("САМОТЕСТ-CHARID ИТОГ: ok=%d fail=%d"), T.Ok, T.Fail);
			AsdaSelfTestKit::Quit(Wd, bQuit);
		};
		if (Delay <= 0.f) { Go(); return; }
		FTimerHandle H;
		W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda(Go), Delay, false);
	}));

#endif // !UE_BUILD_SHIPPING
