// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ РЫБАЛКИ, ВОЛНА В1 (08.10.2026): сеть и автомат `UAsdaFishingSubsystem`. [ПОРТ]
//
// ★ ЗАЧЕМ. Сервера и окна рыбалки (В2) офлайн нет, поэтому путь игрока проверяется так:
//   • ПРИЁМЫ — через ШТАТНЫЙ разбор `UAsdaNetSubsystem::DebugFeedPacket` → `HandlePacket` → делегат → очередь
//     подсистемы → её обработчик (второй копии логики приёма нет);
//   • ОТПРАВКИ — БОЕВЫЕ отправители под перехватом `ArmSendCapture` (в сеть ничего не уходит): опкод, длина тела,
//     конверт ('S' = с секретом: 8 секрет + 16 MD5 + тело ⇒ 30/32/31 б), каждый байт тела против раскладки
//     реверса (client_net.md §1.2-1.4), charId i32;
//   • ВРЕМЯ — часы автомата сдвигаются (`TestClockOffsetMs`), клипы закрывает [ПОРТ]-мост (`asda.Fish.ClipBridge 1`);
//   • МИНИ-ИГРА — встроенный эмулятор (до В2): раунд «отвечается» `EmuQueueRound`, 6169 шлёт гайд-эмулятор.
//   Контрольные пары — что НЕ должно сработать: 6171(2) без флага не уходит; повтор 6171 до 6172 не уходит (Р4);
//   6172 код ≥3 в 118 → 128 при ключе 0 и остаётся 118 при ключе 1 (Р2); 6183 4 вне 118 — молча; Р1/Р3/Р5 с ключами.
//   ★ fishfix 09.10 (ревизия `fishing_REVIEW.md`): G8–G10 — смерть после результата / на клипе вытаскивания не даёт нового
//   заброса (контроль — жив); G11 — повтор действия сверяется с действием персонажа (МЕЛК-2);
//   F3/F3b/F6/F6b — нотисы Р2 строками оригинала; L4 — возврат авто-удочки после смены карты.
//   ★ 09.10.2026 (аудит fmtArg): M1–M6 — текст подтверждения снасти 111026/111028/111029/111030: порядок подстановки
//   по байтам эталона (материал, удочка[, «материал +N»]) и контроль, что прежний перевёрнутый порядок не совпадает.
//
// Консоль: `asda.Fish.SelfTest [delay=N] [quit]`. Только офлайн (`FAsdaOutgoingGate::RefuseDebugWhenOnline`).
//   Строки — «САМОТЕСТ-РЫБА ok/FAIL …», итог — «САМОТЕСТ-РЫБА ИТОГ: ok=… fail=…».
#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Misc/ScopeExit.h"               // ON_SCOPE_EXIT — восстановление на любом выходе (ревизия В2 МЕЛК-8)
#include "AsdaCVarSave.h"                   // ★ ИСПРАВЛЕНИЕ 11.10.2026 (В0, N1(д)/U8): ключи консоли самотеста — запомнить и вернуть (значение + SetBy)
#include "World/AsdaFishingSubsystem.h"
#include "Test/AsdaSelfTestNet.h"   // ★ R1: тонкий доступ самотестов к сети (вместо AsdaNetSubsystem.h)
#include "Test/AsdaSelfTestKit.h"          // ★ 11.10.2026 (В3 S1): общий каркас самотестов (аргументы, отказ, перехват, итог, выход)
#include "AsdaBytes.h"
#include "AsdaSessionReset.h"
#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaInventorySubsystem.h"
#include "UI/AsdaNotice.h"
#include "UI/AsdaFishingToolSubsystem.h"     // ★ 09.10.2026 (аудит fmtArg): раздел M — текст подтверждения снасти (case 'N')
#include "UI/AsdaUiHelpers.h"                // AsdaUi::FormatPrintfArgs — ожидаемая подстановка %s
#include "UI/AsdaMarkup.h"                   // FAsdaMarkup::StripTags — видимый текст без <FC=…>
#include "Items/AsdaItemTableSubsystem.h"
#include "World/AsdaWorldSubsystem.h"
#include "World/AsdaPlayerCharacter.h"
#include "World/AsdaLoadManagerSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaFishTest, Log, All);

// Друг `UAsdaNetSubsystem` и `UAsdaFishingSubsystem` (объявлено в их заголовках).
struct FAsdaFishingSelfTest
{
	// Перехваченная отправка.
	struct FSent
	{
		int32 Op = 0;          // опкод
		TArray<uint8> P;       // тело (без конверта)
		TCHAR Env = 0;         // конверт: 'S' секрет+MD5, 'R' сырой (MD5 внутри тела)
	};

	int32 Ok = 0, Fail = 0;   // пройдено / провалено
	TArray<FSent> Sent;       // всё перехваченное за текущий шаг

	UGameInstance* GI = nullptr;            // экземпляр игры
	UAsdaNetSubsystem* Net = nullptr;        // сеть
	UAsdaFishingSubsystem* F = nullptr;      // рыбалка
	UAsdaWindowSubsystem* WM = nullptr;      // окна-сообщения
	UAsdaInventorySubsystem* Inv = nullptr;  // инвентарь
	UAsdaWorldSubsystem* WS = nullptr;       // мир
	UAsdaLoadManagerSubsystem* LM = nullptr; // карта

	// Одна проверка.
	void Expect(bool b, const FString& What)
	{
		if (b) { ++Ok; UE_LOG(LogAsdaFishTest, Log, TEXT("САМОТЕСТ-РЫБА ok   %s"), *What); }
		else   { ++Fail; UE_LOG(LogAsdaFishTest, Error, TEXT("САМОТЕСТ-РЫБА FAIL %s"), *What); }
	}

	// Шаг кадра под перехватом: очередь пакетов → автомат → очередь 6173 → мост клипов (порядок Tick подсистемы).
	void Step(int32 N = 1)
	{
		const uint32 Gen = AsdaSelfTestKit::ArmCapture(Net, [this](uint16 Op, const uint8* P, int32 Len, bool, TCHAR Env)
		{
			FSent S; S.Op = Op; S.Env = Env;
			if (P && Len > 0) { S.P.Append(P, Len); }
			Sent.Add(MoveTemp(S));
		}, 0, TEXT("asda.Fish.SelfTest"));
		for (int32 i = 0; i < N; ++i)
		{
			F->DrainPackets();
			F->UpdateFsm();
			F->ApplyPeerQueue();
			F->TickClipBridge();
		}
		AsdaSelfTestKit::DisarmCapture(Net, Gen);
	}

	// Действие под перехватом (ответ диалога, прямой вызов).
	void Capture(TFunctionRef<void()> Act)
	{
		const uint32 Gen = AsdaSelfTestKit::ArmCapture(Net, [this](uint16 Op, const uint8* P, int32 Len, bool, TCHAR Env)
		{
			FSent S; S.Op = Op; S.Env = Env;
			if (P && Len > 0) { S.P.Append(P, Len); }
			Sent.Add(MoveTemp(S));
		}, 0, TEXT("asda.Fish.SelfTest"));
		Act();
		AsdaSelfTestKit::DisarmCapture(Net, Gen);
	}

	// Подать пакет штатным разбором и разобрать очередь подсистемы.
	void Feed(int32 Op, const TArray<uint8>& Body)
	{
		AsdaTestNet::DebugFeedPacket(Net, Op, Body);
		F->DrainPackets();
	}

	// Сколько раз ушёл опкод; OutLast — последний.
	int32 Count(int32 Op, FSent* OutLast = nullptr) const
	{
		int32 N = 0;
		for (const FSent& S : Sent) { if (S.Op == Op) { ++N; if (OutLast) { *OutLast = S; } } }
		return N;
	}

	// Сдвинуть часы автомата.
	void Advance(int32 Ms) { F->TestClockOffsetMs += Ms; }

	// ★ Ревизия В1 МЕЛК-3: был ли нотис «канал:loc-id» в журнале рыбалки (после последней очистки).
	bool HadNotice(const TCHAR* Channel, int32 LocId) const
	{
		const FString Pfx = FString::Printf(TEXT("%s:%d:"), Channel, LocId);
		for (const FString& L : F->GetRecentNotices()) { if (L.StartsWith(Pfx)) { return true; } }
		return false;
	}

	// Тело 6168.
	static TArray<uint8> B6168(uint8 Code, int32 V1, int32 Bonus)
	{
		TArray<uint8> B; B.SetNumZeroed(7);
		B[0] = Code; AsdaBytes::WrI32(B.GetData() + 1, V1); AsdaBytes::WrI16(B.GetData() + 5, Bonus);
		return B;
	}
	// Тело 6170.
	static TArray<uint8> B6170(uint8 Code, int32 Phase, int32 Gauge)
	{
		TArray<uint8> B; B.SetNumZeroed(5);
		B[0] = Code; AsdaBytes::WrI16(B.GetData() + 1, Phase); AsdaBytes::WrI16(B.GetData() + 3, Gauge);
		return B;
	}
	// 57-байтовая запись предмета (+0 код, +4 контейнер, +5 слот, +9 количество, +13 прочность).
	static void Rec57(uint8* R, int32 Code, int32 Cont, int32 Slot, int32 Amount, int32 Dur = 0)
	{
		FMemory::Memzero(R, 57);
		AsdaBytes::WrI32(R + 0, Code); R[4] = (uint8)Cont; AsdaBytes::WrI16(R + 5, Slot);
		AsdaBytes::WrI32(R + 9, Amount); R[13] = (uint8)Dur;
	}
	// Тело 6172: код, навык, вес, размер, наживка @7, рыба @64.
	static TArray<uint8> B6172(uint8 Code, int32 Skill, int32 Weight, int32 Size, int32 BaitAmount, int32 FishCode, int32 FishSlot)
	{
		TArray<uint8> B; B.SetNumZeroed(121);
		B[0] = Code; AsdaBytes::WrI16(B.GetData() + 1, Skill); AsdaBytes::WrI16(B.GetData() + 3, Weight); AsdaBytes::WrI16(B.GetData() + 5, Size);
		Rec57(B.GetData() + 7, 69667, 3, 10, BaitAmount);
		if (FishCode > 0) { Rec57(B.GetData() + 64, FishCode, 1, FishSlot, 1); }
		return B;
	}
	// Тело 6173.
	static TArray<uint8> B6173(uint8 Phase, int32 CharId, int32 Fish, int32 Size)
	{
		TArray<uint8> B; B.SetNumZeroed(11);
		B[0] = Phase; AsdaBytes::WrI32(B.GetData() + 1, CharId); AsdaBytes::WrI32(B.GetData() + 5, Fish); AsdaBytes::WrI16(B.GetData() + 9, Size);
		return B;
	}

	// Прогон целиком (синхронно).
	void Run(UWorld* W);
};

void FAsdaFishingSelfTest::Run(UWorld* W)
{
	GI = W ? W->GetGameInstance() : nullptr;
	Net = GI ? AsdaTestNet::Get(GI) : nullptr;
	F = GI ? GI->GetSubsystem<UAsdaFishingSubsystem>() : nullptr;
	WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
	WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
	LM = GI ? GI->GetSubsystem<UAsdaLoadManagerSubsystem>() : nullptr;
	if (!Net || !F || !WM || !Inv || !WS || !LM)
	{
		Expect(false, TEXT("нет подсистем (сеть/рыбалка/окна/инвентарь/мир/загрузка)"));
		return;
	}
	AAsdaPlayerCharacter* Ch = F->SelfChar();
	Expect(Ch != nullptr, TEXT("свой персонаж есть (павн контроллера)"));
	if (!Ch) { return; }

	// ── Снимок (восстанавливается в конце) ──
	const int32 CharIdWas = AsdaTestNet::SelectedCharId(Net);
	const uint16 UserNumWas = AsdaTestNet::SelfUserNum(Net);
	const bool bKnownWas = AsdaTestNet::bSelfUserNumKnown(Net);
	const int32 MapWas = LM->CurrentMapId;
	const FVector2D PosWas = Ch->GetGamePosXZ();
	const int32 WeightWas = AsdaTestNet::SelfWeightCur(Net);
	IConsoleVariable* CvBridge = IConsoleManager::Get().FindConsoleVariable(TEXT("asda.Fish.ClipBridge"));
	IConsoleVariable* Cv107 = IConsoleManager::Get().FindConsoleVariable(TEXT("asda.Fish.Legacy107"));
	IConsoleVariable* Cv118 = IConsoleManager::Get().FindConsoleVariable(TEXT("asda.Fish.Legacy118"));
	IConsoleVariable* CvSpam = IConsoleManager::Get().FindConsoleVariable(TEXT("asda.Fish.LegacyCancelSpam"));
	IConsoleVariable* CvQueue = IConsoleManager::Get().FindConsoleVariable(TEXT("asda.Fish.LegacyOtherQueue"));
	if (CvBridge) { AsdaCVarSave::SetForTest(TEXT("asda.Fish.ClipBridge"), 1); }
	TArray<TPair<int32, int32>> UsedCells;   // (контейнер, ячейка), занятые прогоном

	constexpr int32 kCharId = 0x00ABCDEF;    // свой charId (младшее слово ≠ userNum)
	constexpr uint16 kMe = 321;              // свой userNum (4024)
	AsdaTestNet::SelectedCharId(Net) = kCharId;
	AsdaTestNet::SelfUserNum(Net) = kMe; AsdaTestNet::bSelfUserNumKnown(Net) = true;
	F->bTestNoNetDrain = true;
	// ★ Волна В2: зарегистрированное окно мини-игры сохраняется и возвращается в конце (тест В1 идёт на эмуляторе).
	IAsdaFishMiniGame* const MgWas = F->GetRegisteredMiniGame();
	UObject* const MgOwnerWas = F->GetRegisteredMiniGameOwner();
	F->SetMiniGame(nullptr);
	F->ResetSessionState(EAsdaSessionResetReason::Disconnected);
	WM->ClearMsgBoxQueue(TEXT("самотест рыбалки: старт"));
	// ★ Ревизия В2 МЕЛК-8 (волна В4, 08.10.2026): восстановление — на ЛЮБОМ выходе (прежде ранний `return` после A7 оставлял
	//   окно В2 снятым с регистрации, `bTestNoNetDrain` взведённым и charId подменённым).
	ON_SCOPE_EXIT
	{
		for (const TPair<int32, int32>& C : UsedCells) { Inv->DebugPutItem(C.Key, C.Value, 0, 0); }
		F->ResetSessionState(EAsdaSessionResetReason::Disconnected);
		F->TestClockOffsetMs = 0;
		F->TestDeadOverride = -1;
		F->TestWorldReadyOverride = -1;   // ★ fishfix 09.10 (МЕЛК-5)
		F->bTestNoNetDrain = false;
		AsdaTestNet::SelectedCharId(Net) = CharIdWas;
		AsdaTestNet::SelfUserNum(Net) = UserNumWas; AsdaTestNet::bSelfUserNumKnown(Net) = bKnownWas;
		AsdaTestNet::SelfWeightCur(Net) = WeightWas;
		LM->CurrentMapId = MapWas;
		Ch->NetSnap(PosWas);
		// ★ ИСПРАВЛЕНИЕ 11.10.2026 (В0, N1(д)/U8): все ключи прогона (ClipBridge, Legacy107/109/118, LegacyCancelSpam,
		//   LegacyOtherQueue, MapReturnAuto) — к исходному значению И исходному приоритету SetBy (прежде мост
		//   возвращался с SetByCode, остальные — константой 0/1).
		AsdaCVarSave::RestoreAll(TEXT("asda.Fish.SelfTest"));
		WM->ClearMsgBoxQueue(TEXT("самотест рыбалки: конец"));
		F->SetMiniGame(MgWas, MgOwnerWas);   // вернуть окно В2 (если было зарегистрировано)
	};

	// ════════════════ A. ДАННЫЕ ════════════════
	Expect(F->GetFishDataNum() == 86,
		FString::Printf(TEXT("A1 DT_FishData: %d строк (ждём 86 = Fish_data.BIN)"), F->GetFishDataNum()));
	Expect(F->GetFishNumNum() == 20, FString::Printf(TEXT("A2 DT_FishNum: %d (ждём 20)"), F->GetFishNumNum()));
	Expect(F->GetSpotNum() == 51, FString::Printf(TEXT("A3 DT_FishSpots: %d (ждём 51 спот на 7 картах)"), F->GetSpotNum()));
	{
		const FAsdaFishDataRow* R = F->FindFishData(69649);
		Expect(R && R->Order == 72, FString::Printf(TEXT("A4 код 69649 (дубль строк 72/80) → первая строка %d (ждём 72)"), R ? R->Order : -1));
		const FAsdaFishDataRow* C = F->FindFishData(69577);
		Expect(C && C->Rounds == 6 && C->RoundMs == 4000 && C->Model == TEXT("Fs_fsm_crucian_00"),
			FString::Printf(TEXT("A5 69577: раундов %d, мс %d, модель %s (ждём 6/4000/Fs_fsm_crucian_00)"),
				C ? C->Rounds : -1, C ? C->RoundMs : -1, C ? *C->Model : TEXT("-")));
		Expect(F->LookupFishNum(150) == 4 && F->LookupFishNum(5000) == 1 && F->LookupFishNum(-3) == 1,
			TEXT("A6 Fish_num: 150 → 4; вне диапазонов (5000, −3) → 1 (rangeTableLookupValue)"));
	}
	TArray<int32> Alp;
	F->GetSpotsOfMap(3, Alp);
	const FAsdaFishSpotRow* S0 = Alp.Num() ? F->GetSpot(Alp[0]) : nullptr;
	Expect(Alp.Num() == 10 && S0 && S0->X == 136 && S0->Y == 77 && S0->Radius == 10 && S0->Slot == 0,
		FString::Printf(TEXT("A7 Альпия: спотов %d, первый X %d Y %d r %d слот %d (ждём 10; 136/77/10/0)"),
			Alp.Num(), S0 ? S0->X : -1, S0 ? S0->Y : -1, S0 ? S0->Radius : -1, S0 ? S0->Slot : -1));
	if (!S0) { return; }
	const FVector2D Zone((float)S0->X * 100.f, (float)S0->Y * 100.f);
	const FVector2D Near = Zone + FVector2D(300.f, 0.f);          // внутри r·100 = 1000
	const FVector2D Far = Zone + FVector2D(0.f, -3000.f);         // вне всех зон Альпии (ближайшая — 1900+ ед.)
	LM->CurrentMapId = 3;

	// ════════════════ B. ВХОД 109 / ДИАЛОГ 81 / ДИСТАНЦИЯ ════════════════
	Ch->StopMove(); Ch->NetSnap(Far);
	Sent.Reset();
	F->ClearRecentNotices();
	Expect(!F->StartAtNearestSpot() && F->GetFishState() == 0 && HadNotice(TEXT("chat"), 110700),
		TEXT("B1 контроль: зоны в радиусе нет → чат 110700 (журнал нотисов), автомат 0"));
	Ch->NetSnap(Near);
	Expect(F->StartAtNearestSpot() && F->GetFishState() == 109 && F->GetPickedSpot() == Alp[0],
		FString::Printf(TEXT("B2 ближайшая зона → 109 (автомат %d, зона %d)"), F->GetFishState(), F->GetPickedSpot()));
	Step();
	Expect(WM->IsMsgBoxQueued(1, 81) && WM->GetMsgBoxQueueTopText().Contains(AsdaNotice::Loc(GI, 111057)),
		FString::Printf(TEXT("B3 109 → диалог (1, 81) «%s»"), *WM->GetMsgBoxQueueTopText()));
	{
		const FVector2D Fd = Ch->GetFacingDirGame();
		const FVector2D Want = (Zone - Ch->GetGamePosXZ()).GetSafeNormal();
		Expect(FVector2D::DotProduct(Fd, Want) > 0.98f, FString::Printf(TEXT("B4 персонаж повёрнут к зоне (скаляр %.3f)"), FVector2D::DotProduct(Fd, Want)));
	}
	Step(3);
	Expect(WM->GetMsgBoxQueueNum() == 1, FString::Printf(TEXT("B5 повтор 109 каждый кадр не плодит окно: в очереди %d"), WM->GetMsgBoxQueueNum()));
	WM->CloseConfirm(false);
	Expect(F->GetFishState() == 0, TEXT("B6 диалог 81 «Отмена» → 0"));

	// ════════════════ C. ПОЛНЫЙ ЦИКЛ С НАДЕТОЙ СНАСТЬЮ ════════════════
	Inv->DebugPutItem(3, UAsdaFishingSubsystem::kEquipSlotRod, 69657, 1);  UsedCells.Add({ 3, UAsdaFishingSubsystem::kEquipSlotRod });
	Inv->DebugPutItem(3, UAsdaFishingSubsystem::kEquipSlotBait, 69667, 5); UsedCells.Add({ 3, UAsdaFishingSubsystem::kEquipSlotBait });
	Expect(F->HasRodEquipped() && F->HasBaitEquipped(), TEXT("C0 удочка 69657 в слоте 9 и наживка 69667 в слоте 10 опознаны"));
	F->StartAtNearestSpot();
	Step();
	WM->CloseConfirm(true);
	Expect(F->GetFishState() == 102, FString::Printf(TEXT("C1 81 OK со снастью → 102 (автомат %d)"), F->GetFishState()));
	Step();
	Expect(F->GetFishState() == 101, FString::Printf(TEXT("C2 102 → 101 (автомат %d)"), F->GetFishState()));
	Step();
	Expect(F->GetFishState() == 100, TEXT("C3 101 без книги (В3) → 100 (эталон без подходящего предмета)"));
	Sent.Reset(); Step();
	{
		FSent S; const int32 N = Count(6167, &S);
		Expect(N == 1 && S.Env == TEXT('S') && S.P.Num() == 6 &&
			AsdaBytes::RdI32(S.P.GetData()) == kCharId && AsdaBytes::RdI16(S.P.GetData() + 4) == S0->Slot,
			FString::Printf(TEXT("C4 6167 ×%d, конверт '%c' (секрет+MD5 — общий SendSecured), тело %d б, charId 0x%X, спот %d (ждём ×1, 'S', 6, 0x%X, %d)"),
				N, S.Env ? S.Env : TEXT('-'), S.P.Num(), S.P.Num() >= 4 ? AsdaBytes::RdI32(S.P.GetData()) : 0,
				S.P.Num() >= 6 ? AsdaBytes::RdI16(S.P.GetData() + 4) : -1, kCharId, S0->Slot));
	}
	Expect(F->GetFishState() == 107, TEXT("C5 после 6167 → 107 (ждём 6168)"));
	Feed(6168, B6168(1, 69577, 120));
	Expect(F->GetFishState() == 108 && F->GetSelectedFishCode() == 69577 && (int32)F->GetGauge() == 120,
		FString::Printf(TEXT("C6 6168 код 1 → 108, FishCode %d, шкала %.0f (ждём 69577/120)"), F->GetSelectedFishCode(), F->GetGauge()));
	Step();
	Expect(F->GetFishState() == 112 && F->GetSelfFishingFlag(), TEXT("C7 108 → флаг «рыбачит», звук старта, 112"));
	Sent.Reset(); Step();
	{
		FSent S; const int32 N = Count(4024, &S);
		Expect(N == 1 && S.P.Num() == 29 && AsdaBytes::RdI16(S.P.GetData() + 16) == kMe && AsdaBytes::RdI16(S.P.GetData() + 18) == 122,
			FString::Printf(TEXT("C8 112 → действие 60 и 4024 ×%d stateId %d userNum %d (ждём ×1, 122, %d)"),
				N, S.P.Num() >= 20 ? AsdaBytes::RdI16(S.P.GetData() + 18) : -1, S.P.Num() >= 18 ? AsdaBytes::RdI16(S.P.GetData() + 16) : -1, kMe));
	}
	Sent.Reset(); Step(2);
	Expect(Count(4024) == 0 && F->GetSelfAction() == 60, TEXT("C9 контроль: 112 зовёт действие 60 каждый кадр, повтор 4024 не уходит"));
	Advance(3700); Step();
	Expect(F->GetFishState() == 110 && F->GetSelfAction() == 61, FString::Printf(TEXT("C10 конец клипа 60 (мост 3667 мс) → 110, действие 61 (автомат %d, действие %d)"), F->GetFishState(), F->GetSelfAction()));
	Step();
	Expect(F->GetFishState() == 114, TEXT("C11 110 → окно (эмулятор) → 114"));
	Step();
	Expect(F->GetFishState() == 115 && F->Emu && F->Emu->Rounds == 6, FString::Printf(TEXT("C12 114 → гайд на %d раундов (Fish_data 69577 +8 = 6) → 115"), F->Emu ? F->Emu->Rounds : -1));
	int32 GaugeNow = 120;
	for (int32 r = 0; r < 6; ++r)
	{
		F->EmuQueueRound(4, r % 5);
		Sent.Reset(); Step();
		FSent S; const int32 N = Count(6169, &S);
		Expect(N == 1 && S.Env == TEXT('S') && S.P.Num() == 8 && AsdaBytes::RdI32(S.P.GetData()) == kCharId &&
			AsdaBytes::RdI16(S.P.GetData() + 4) == GaugeNow && S.P[6] == 4 && S.P[7] == (uint8)(r % 5) &&
			(F->GetSelfAction() == 63 || F->GetSelfAction() == 64),
			FString::Printf(TEXT("C13.%d 6169 ×%d (тело %d б, конверт 'S'): charId 0x%X шкала %d всего %d верно %d; действие %d (ждём 8; %d/4/%d; 63|64)"),
				r + 1, N, S.P.Num(), S.P.Num() >= 4 ? AsdaBytes::RdI32(S.P.GetData()) : 0, S.P.Num() >= 6 ? AsdaBytes::RdI16(S.P.GetData() + 4) : -1,
				S.P.Num() >= 8 ? S.P[6] : -1, S.P.Num() >= 8 ? S.P[7] : -1, F->GetSelfAction(), GaugeNow, r % 5));
		GaugeNow += 50;
		Feed(6170, B6170(1, r + 1, GaugeNow));
	}
	Expect((int32)F->GetGauge() == 420 && F->GetFishState() == 115, FString::Printf(TEXT("C14 6170 ведёт шкалу: %.0f (ждём 420), автомат 115"), F->GetGauge()));
	Sent.Reset(); Step();
	Expect(F->GetFishState() == 116, TEXT("C15 гайд вернул 8 → 116"));
	Step(); Step();
	{
		FSent S; const int32 N = Count(6171, &S);
		Expect(N == 1 && S.Env == TEXT('S') && S.P.Num() == 7 && AsdaBytes::RdI32(S.P.GetData()) == kCharId && S.P[4] == 1 &&
			AsdaBytes::RdI16(S.P.GetData() + 5) == 420 && F->GetFishState() == 118,
				FString::Printf(TEXT("C16 117 → 6171 ×%d (тело %d б, конверт 'S') режим %d шкала %d → 118 (ждём 7; 1; 420)"),
					N, S.P.Num(), S.P.Num() >= 5 ? S.P[4] : -1, S.P.Num() >= 7 ? AsdaBytes::RdI16(S.P.GetData() + 5) : -1));
	}
	// Контроль Р4: повторный 6171 до 6172 не уходит (X → диалог 80 → OK).
	Sent.Reset();
	F->RequestCancelDialog80();
	Expect(WM->IsMsgBoxQueued(1, 80), TEXT("C17 X/ESC → диалог (1, 80) «Прекратить рыбалку?»"));
	Capture([&]() { WM->CloseConfirm(true); });
	Expect(Count(6171) == 0 && F->GetFishState() == 118, TEXT("C18 контроль Р4: 6171(2) до прихода 6172 НЕ уходит (защита от двойного улова)"));
	// Результат: поймал.
	int32 FishSlot = -1;
	for (int32 c = 0; c < Inv->GetContainerSize(1); ++c) { if (Inv->GetCellItemCode(1, c) == 0) { FishSlot = c; break; } }
	if (FishSlot >= 0) { UsedCells.Add({ 1, FishSlot }); }
	F->ClearRecentNotices();
	AsdaTestNet::SelfWeightCur(Net) = -1;
	Feed(6172, B6172(1, 37, 100, 55, 4, 69577, FishSlot));
	Expect(AsdaTestNet::SelfWeightCur(Net) == 100 && HadNotice(TEXT("chat"), 111076),
		FString::Printf(TEXT("C19a 6172 код 1: вес %d (ждём 100, ставится в Tick — ревизия МЕЛК-4/8), чат 111076 с навыком"), AsdaTestNet::SelfWeightCur(Net)));
	Expect(F->GetFishState() == 119 && F->GetFishingSkill() == 37 && F->GetCaughtItemId() == 69577 && F->GetCaughtSizeCm() == 55 &&
		F->GetSelfHasFish() && Inv->GetCellItemCode(1, FishSlot) == 69577 && Inv->GetCellAmount(3, 10) == 4,
		FString::Printf(TEXT("C19 6172 код 1 → 119; навык %d, улов %d %d см, рыба в сумке %d, наживка %d (ждём 37, 69577 55, 69577, 4)"),
			F->GetFishingSkill(), F->GetCaughtItemId(), F->GetCaughtSizeCm(), FishSlot >= 0 ? Inv->GetCellItemCode(1, FishSlot) : -1, Inv->GetCellAmount(3, 10)));
	Sent.Reset(); Step(); Step();
	{
		FSent S; const int32 N = Count(4024, &S);
		Expect(F->GetFishState() == 113 && N == 1 && AsdaBytes::RdI16(S.P.GetData() + 18) == 124,
			FString::Printf(TEXT("C20 119 → 113 → действие 62, 4024 stateId %d (ждём 124)"), S.P.Num() >= 20 ? AsdaBytes::RdI16(S.P.GetData() + 18) : -1));
	}
	Advance(2100); Step();
	Expect(F->GetFishState() == 126, TEXT("C21 конец клипа 62, улов > 0 → fish_end_01 (BP нет — не падает) → 126"));
	Sent.Reset(); Step();
	{
		FSent S; Count(4024, &S);
		Expect(F->GetSelfAction() == 65 && S.P.Num() >= 20 && AsdaBytes::RdI16(S.P.GetData() + 18) == 127, TEXT("C22 126 → действие 65, 4024 stateId 127"));
	}
	Advance(3400); Step();
	Expect(F->GetFishState() == 120 && !F->GetSelfFishingFlag() && F->GetSelfAction() == 0, TEXT("C23 конец клипа 65 → 120, флаг снят, действие 0 (4024 не уходит)"));
	Step();
	Expect(F->Emu && F->Emu->bPopup, TEXT("C24 120 → всплывашка улова показана"));
	Advance(3100); Step();
	Expect(F->GetFishState() == 121 && F->Emu && !F->Emu->bPopup, TEXT("C25 через 3 с → 121, всплывашка скрыта"));
	F->ClearRecentNotices();
	Step();
	Expect(F->GetFishState() == 121 && HadNotice(TEXT("plank"), 111050),
		TEXT("C26 121 при автоповторе (cmapInitReset = 1) идёт отсчёт; плашка 221 — 111050 (журнал нотисов)"));
	Advance(5100); Step();
	Expect(F->GetFishState() == 122, TEXT("C27 через 5 с → 122"));
	Step();
	Expect(F->GetFishState() == 100 && !F->GetSelfFishingFlag(), TEXT("C28 122 → перезаброс 100"));
	Sent.Reset(); Step();
	Expect(Count(6167) == 1 && F->GetFishState() == 107, TEXT("C29 перезаброс: 6167 снова → 107"));
	// Отмена при флаге 0 (между забросами) — эталонный ранний выход.
	Sent.Reset();
	F->RequestCancelDialog80();
	Capture([&]() { WM->CloseConfirm(true); });
	Expect(Count(6171) == 0 && F->GetFishState() == 128, TEXT("C30 контроль: 6171(2) без флага «рыбачит» НЕ уходит, автомат 128"));
	Step();
	Expect(F->GetFishState() == 0, TEXT("C31 128 → 0"));

	// ════════════════ D. 6168 — ВСЕ КОДЫ ════════════════
	struct FC { uint8 Code; int32 Loc; };
	const FC Codes[] = { {2, 111041}, {3, 1011}, {4, 111042}, {5, 111043}, {6, 111044}, {8, 111077} };
	for (const FC& C : Codes)
	{
		WM->ClearMsgBoxQueue(TEXT("самотест рыбалки: D"));
		F->FishState = 107;
		Feed(6168, B6168(C.Code, -1, 0));
		const bool bTxt = WM->GetMsgBoxQueueTopText().Contains(AsdaNotice::Loc(GI, C.Loc).Left(12));
		Expect(F->GetFishState() == 128 && bTxt, FString::Printf(TEXT("D 6168 код %d → 128 + msgbox %d «%s»"), C.Code, C.Loc, *WM->GetMsgBoxQueueTopText()));
		Step();
	}
	WM->ClearMsgBoxQueue(TEXT("самотест рыбалки: D7"));
	F->FishState = 107;
	Feed(6168, B6168(7, 450, 0));
	Expect(F->GetFishState() == 128 && WM->GetMsgBoxQueueTopText().Contains(TEXT("450")),
		FString::Printf(TEXT("D 6168 код 7 → 111049 с требованием «%s»"), *WM->GetMsgBoxQueueTopText()));
	Step();
	for (const uint8 Odd : { (uint8)0, (uint8)9 })
	{
		WM->ClearMsgBoxQueue(TEXT("самотест рыбалки: D0/9"));
		F->FishState = 107;
		Feed(6168, B6168(Odd, -1, 0));
		Expect(F->GetFishState() == 128 && WM->GetMsgBoxQueueTopText() == FString::FromInt(Odd),
			FString::Printf(TEXT("D 6168 код %d → msgbox «%s» (ждём число)"), Odd, *WM->GetMsgBoxQueueTopText()));
		Step();
	}
	WM->ClearMsgBoxQueue(TEXT("самотест рыбалки: D конец"));

	// ════════════════ E. 6170 ════════════════
	F->FishState = 115; F->SetGaugeValue(100);
	Feed(6170, B6170(2, 1, 300));
	Expect((int32)F->GetGauge() == 100 && F->GetFishState() == 115, TEXT("E1 контроль: 6170 код 2 — игнор (шкала 100)"));
	Feed(6170, B6170(0, 0, 200));
	Expect((int32)F->GetGauge() == 200, TEXT("E2 6170 код 0 тоже ставит шкалу (эталон: код ≤ 1)"));
	Feed(6170, B6170(1, 3, 500));
	Expect(F->GetFishState() == 116 && F->IsGaugeFull(), TEXT("E3 шкала 500 (полна) в 115 → 116 (досрочный конец)"));
	F->FishState = 118;
	Feed(6170, B6170(1, 3, 600));
	Expect(F->GetFishState() == 118 && (int32)F->GetGauge() == 500, TEXT("E4 контроль: в 118 полная шкала автомат не трогает; шкала клампится 500"));

	// ════════════════ F. 6172 — КОДЫ 0/2/≥3 И Р2 ════════════════
	F->FishState = 118; F->b6171InFlight = true;
	Feed(6172, B6172(0, 38, 90, 0, 3, 0, 0));
	Expect(F->GetFishState() == 119 && !F->GetSelfHasFish() && F->GetCaughtItemId() == 0 && !F->b6171InFlight && F->GetFishingSkill() == 38,
		TEXT("F1 6172 код 0 (сорвалась) → 119, рыбы нет, защёлка Р4 снята, навык 38"));
	F->FishState = 118;
	Feed(6172, B6172(2, 38, 90, 0, 2, 0, 0));
	Expect(F->GetFishState() == 128, TEXT("F2 6172 код 2 → 128"));
	Step();
	WM->ClearMsgBoxQueue(TEXT("самотест рыбалки: F3"));
	F->FishState = 118;
	const int32 WeightF3 = AsdaTestNet::SelfWeightCur(Net);
	Feed(6172, B6172(3, 0, 777, 0, 0, 0, 0));
	// ★ fishfix 09.10 (ревизия МЕЛК-1): нотис Р2 — строка оригинала по смыслу серверной ветки, а не голая цифра.
	Expect(F->GetFishState() == 128 && WM->GetMsgBoxQueueTopText().Contains(AsdaNotice::Loc(GI, 111045).Left(12)),
		FString::Printf(TEXT("F3 Р2: 6172 код 3 («Not Fishing») в 118 → 128 + msgbox 111045 «%s»"), *WM->GetMsgBoxQueueTopText()));
	Expect(AsdaTestNet::SelfWeightCur(Net) == WeightF3, TEXT("F3a контроль: 6172 код 3 вес не трогает (эталон: только код ≤ 2)"));
	Step();
	{
		// МЕЛК-1: остальные коды Recv_EndFishing → 6172: 7 «Not FishingSpot» → 111042, 8 «Wrong BaitItem» → 111056; прочий (9) — «%d».
		struct FR { uint8 Code; int32 Loc; };
		for (const FR& C : { FR{ 7, 111042 }, FR{ 8, 111056 }, FR{ 9, 0 } })
		{
			WM->ClearMsgBoxQueue(TEXT("самотест рыбалки: F3b"));
			F->FishState = 118;
			Feed(6172, B6172(C.Code, 0, 0, 0, 0, 0, 0));
			const FString Top = WM->GetMsgBoxQueueTopText();
			const bool bTxt = C.Loc ? Top.Contains(AsdaNotice::Loc(GI, C.Loc).Left(12)) : (Top == FString::FromInt(C.Code));
			Expect(F->GetFishState() == 128 && bTxt,
				FString::Printf(TEXT("F3b МЕЛК-1: 6172 код %d в 118 → 128 + msgbox %s «%s»"), C.Code,
					C.Loc ? *FString::FromInt(C.Loc) : TEXT("«%d»"), *Top));
			Step();
		}
	}
	if (Cv118) { AsdaCVarSave::SetForTest(TEXT("asda.Fish.Legacy118"), 1); }
	F->FishState = 118;
	Feed(6172, B6172(7, 0, 0, 0, 0, 0, 0));
	Expect(F->GetFishState() == 118, TEXT("F4 контроль Р2: при asda.Fish.Legacy118 1 код 7 игнорируется (как оригинал, 118)"));
	if (Cv118) { AsdaCVarSave::SetForTest(TEXT("asda.Fish.Legacy118"), 0); }
	TArray<uint8> R83; R83.SetNumZeroed(64); R83[0] = 4;
	F->FishState = 0;
	Feed(6183, R83);
	Expect(F->GetFishState() == 0, TEXT("F5 контроль: 6183 код 4 вне 118 — рыбалку не трогает (это книга)"));
	F->FishState = 118;
	WM->ClearMsgBoxQueue(TEXT("самотест рыбалки: F6"));
	Feed(6183, R83);
	Expect(F->GetFishState() == 128 && WM->GetMsgBoxQueueTopText().Contains(AsdaNotice::Loc(GI, 111045).Left(12)),
		FString::Printf(TEXT("F6 Р2: 6183 код 4 (Recv_EndFishing) в 118 → 128 + msgbox 111045 «%s» (МЕЛК-1)"), *WM->GetMsgBoxQueueTopText()));
	Step();
	{
		// МЕЛК-1: 6183 код 6 (перевес, GS:7181-7184) → 2252 «превышен вес».
		WM->ClearMsgBoxQueue(TEXT("самотест рыбалки: F6b"));
		TArray<uint8> R86; R86.SetNumZeroed(64); R86[0] = 6;
		F->FishState = 118;
		Feed(6183, R86);
		Expect(F->GetFishState() == 128 && WM->GetMsgBoxQueueTopText().Contains(AsdaNotice::Loc(GI, 2252).Left(12)),
			FString::Printf(TEXT("F6b МЕЛК-1: 6183 код 6 (перевес) в 118 → 128 + msgbox 2252 «%s»"), *WM->GetMsgBoxQueueTopText()));
		Step();
	}
	WM->ClearMsgBoxQueue(TEXT("самотест рыбалки: F конец"));

	// ════════════════ G. 6171 — Р1, Р3, Р4 ════════════════
	F->FishState = 100; F->PickedSpot = -1;
	Sent.Reset(); Step();
	Expect(Count(6167) == 0 && F->GetFishState() == 128, TEXT("G1 Р1: зона не выбрана — 6167 не уходит, автомат 128 (не тупик 107)"));
	Step();
	if (Cv107) { AsdaCVarSave::SetForTest(TEXT("asda.Fish.Legacy107"), 1); }
	F->FishState = 100;
	Sent.Reset(); Step();
	Expect(Count(6167) == 0 && F->GetFishState() == 107, TEXT("G2 контроль Р1: asda.Fish.Legacy107 1 → тупик 107, как оригинал"));
	if (Cv107) { AsdaCVarSave::SetForTest(TEXT("asda.Fish.Legacy107"), 0); }
	F->FishState = 0;
	F->PickedSpot = Alp[0];
	// Р3: смерть во время мини-игры.
	F->FishState = 115; F->bSelfFishingFlag = true; F->TestDeadOverride = 1; F->b6171InFlight = false;
	Sent.Reset(); Step(3);
	{
		FSent S; const int32 N = Count(6171, &S);
		Expect(N == 1 && S.P.Num() == 7 && S.P[4] == 2, FString::Printf(TEXT("G3 Р3: смерть → 6171(2) ×%d за 3 кадра (ждём ×1)"), N));
	}
	{
		// ★ Ревизия В1 МЕЛК-2: пара различает ключ по ЧИСЛУ ВЫЗОВОВ `Send6171`, а не по проводу (его держит Р4).
		F->DebugSend6171Calls = 0;
		Sent.Reset(); Step(3);
		const int32 Calls0 = F->DebugSend6171Calls;
		if (CvSpam) { AsdaCVarSave::SetForTest(TEXT("asda.Fish.LegacyCancelSpam"), 1); }
		F->DebugSend6171Calls = 0;
		Sent.Reset(); Step(3);
		const int32 Calls1 = F->DebugSend6171Calls;
		Expect(Calls0 == 0 && Calls1 == 3 && Count(6171) == 0,
			FString::Printf(TEXT("G4 Р3: при ключе 0 вызовов %d за 3 кадра (ждём 0 — уже вызван), asda.Fish.LegacyCancelSpam 1 — %d (ждём 3); провод держит Р4"), Calls0, Calls1));
		if (CvSpam) { AsdaCVarSave::SetForTest(TEXT("asda.Fish.LegacyCancelSpam"), 0); }
	}
	F->TestDeadOverride = -1;
	Feed(6172, B6172(3, 0, 0, 0, 0, 0, 0));       // сервер уже сбросил при смерти → «Not Fishing»
	Expect(!F->b6171InFlight, TEXT("G5 ответ 6172 снимает защёлку Р4"));
	F->FishState = 0; F->bSelfFishingFlag = false;
	WM->ClearMsgBoxQueue(TEXT("самотест рыбалки: G"));
	// ★ fishfix 09.10 (ревизия СРЕДН-1): смерть ПОСЛЕ результата (6172 пришёл, автомат 119) и ВО ВРЕМЯ клипа вытаскивания (62)
	//   — провод 6171(2) один раз (Р3), а как только флаг «рыбачит» снят (конец клипа 66 → 121), ветка эталона «режим 2 без
	//   флага → 128» каждый кадр: рыбалка кончается, автоповтор (122 → 100 → 6167) у трупа НЕ наступает. Контроль — жив:
	//   тот же эпизод доходит до перезаброса, 6167 уходит. Клипы закрывает мост (`asda.Fish.ClipBridge 1`).
	{
		// DeadMode: 0 — жив (контроль), 1 — смерть сразу после 6172 (119), 2 — смерть на клипе вытаскивания (113, действие 62).
		auto Episode = [&](int32 DeadMode, int32& N6167, int32& N6171, int32& EndState)
		{
			WM->ClearMsgBoxQueue(TEXT("самотест рыбалки: G8"));
			F->SetAutoRepeat(true);
			F->TestDeadOverride = 0;
			F->FishState = 118; F->bSelfFishingFlag = true; F->b6171InFlight = true; F->bDeathCancelCalled = false;
			Sent.Reset();
			Feed(6172, B6172(0, 38, 90, 0, 3, 0, 0));    // сорвалась → 119 (защёлка Р4 снята)
			if (DeadMode == 1) { F->TestDeadOverride = 1; }
			Step();                                       // 119 → 113
			Step();                                       // 113: действие 62 (мост 2000 мс)
			if (DeadMode == 2) { F->TestDeadOverride = 1; Step(); }
			Advance(2100); Step();                        // конец клипа 62, улова нет → 127
			Step();                                       // 127: действие 66 (мост 2333 мс)
			Advance(2400); Step();                        // конец клипа 66 → 121, флаг «рыбачит» снят
			Step(3);                                      // мёртв: 6171(2) без флага → 128 → 0; жив: отсчёт 121
			Advance(5100); Step(); Step(); Step();        // жив: 122 → 100 → 6167 → 107
			N6167 = Count(6167); N6171 = Count(6171); EndState = F->GetFishState();
			F->TestDeadOverride = -1;
		};
		int32 A67 = -1, A71 = -1, AEnd = -1;
		Episode(1, A67, A71, AEnd);
		Expect(A67 == 0 && A71 == 1 && AEnd == 0 && !F->GetSelfFishingFlag(),
			FString::Printf(TEXT("G8 СРЕДН-1: смерть после результата (119) — 6171(2) ×%d (ждём 1, Р3), 6167 ×%d (ждём 0 — нового заброса нет), автомат %d (ждём 0)"),
				A71, A67, AEnd));
		int32 B67 = -1, B71 = -1, BEnd = -1;
		Episode(2, B67, B71, BEnd);
		Expect(B67 == 0 && B71 == 1 && BEnd == 0,
			FString::Printf(TEXT("G9 СРЕДН-1: смерть на клипе вытаскивания (113, действие 62) — 6171(2) ×%d (ждём 1), 6167 ×%d (ждём 0), автомат %d (ждём 0)"),
				B71, B67, BEnd));
		int32 C67 = -1, C71 = -1, CEnd = -1;
		Episode(0, C67, C71, CEnd);
		Expect(C67 == 1 && C71 == 0 && CEnd == 107,
			FString::Printf(TEXT("G10 контроль СРЕДН-1: жив — тот же эпизод доходит до перезаброса: 6167 ×%d (ждём 1), 6171 ×%d (ждём 0), автомат %d (ждём 107)"),
				C67, C71, CEnd));
		F->FishState = 0; F->bSelfFishingFlag = false; F->b6171InFlight = false;
		WM->ClearMsgBoxQueue(TEXT("самотест рыбалки: G8 конец"));
	}
	// ★ fishfix 09.10 (ревизия МЕЛК-2): пропуск повтора действия — по действию ПЕРСОНАЖА (эталон `Net_GetCurrentMapType`).
	//   Персонаж держит 62 — повтор 113 не перезапускает клип (4024 нет); действие у персонажа снял кто-то другой — повторный
	//   вход перезапускает клип и шлёт 4024 stateId 124. Мост клипов на время пары выключен (в бою он выключен всегда).
	{
		if (CvBridge) { AsdaCVarSave::SetForTest(TEXT("asda.Fish.ClipBridge"), 0); }
		F->TestDeadOverride = 0;
		F->FishState = 113; F->bSelfFishingFlag = true;
		Sent.Reset(); Step();                                  // 113: действие 62, 4024 stateId 124
		Sent.Reset(); Step();
		const int32 NHold = Count(4024);
		Ch->StopFishAction(TEXT("самотест МЕЛК-2: действие снял другой"));
		Sent.Reset(); Step();
		FSent S; const int32 NRe = Count(4024, &S);
		Expect(NHold == 0 && NRe == 1 && S.P.Num() >= 20 && AsdaBytes::RdI16(S.P.GetData() + 18) == 124 && Ch->GetFishAction() == 62,
			FString::Printf(TEXT("G11 МЕЛК-2: персонаж держит 62 — повтора нет (4024 ×%d, ждём 0); действие снято извне — клип 62 снова, 4024 ×%d stateId 124 (ждём 1), у персонажа %d"),
				NHold, NRe, Ch->GetFishAction()));
		F->FishState = 0; F->bSelfFishingFlag = false; F->TestDeadOverride = -1;
		F->SetSelfActionState(0);
		if (CvBridge) { AsdaCVarSave::SetForTest(TEXT("asda.Fish.ClipBridge"), 1); }
	}
	// ★ Ревизия В1 ВАЖН-2: вход 109 без выбранной зоны.
	{
		IConsoleVariable* Cv109 = IConsoleManager::Get().FindConsoleVariable(TEXT("asda.Fish.Legacy109"));
		const int32 Spot = F->PickedSpot;
		F->PickedSpot = -1;
		F->ClearRecentNotices();
		F->RequestStart109();
		Expect(F->GetFishState() == 0 && HadNotice(TEXT("chat"), 110700), TEXT("G6 ВАЖН-2: 109 без зоны → чат 110700, автомат 0 (не вечный 109)"));
		if (Cv109) { AsdaCVarSave::SetForTest(TEXT("asda.Fish.Legacy109"), 1); }
		F->RequestStart109();
		Expect(F->GetFishState() == 109, TEXT("G7 контроль: asda.Fish.Legacy109 1 → 109 без зоны, как оригинал"));
		if (Cv109) { AsdaCVarSave::SetForTest(TEXT("asda.Fish.Legacy109"), 0); }
		F->FishState = 0;
		F->PickedSpot = Spot;
	}

	// ════════════════ H. 6173 — ЧУЖИЕ, Р5 ════════════════
	Feed(6173, B6173(1, 777777, 69577, 0));
	Step();
	Expect(F->PeerQueue.Num() == 1, TEXT("H1 6173 для ненайденного charId остаётся в очереди (персонаж может появиться позже)"));
	Advance(11000); Step();
	Expect(F->PeerQueue.Num() == 0, TEXT("H2 Р5: через asda.Fish.OtherQueueTtlMs (10 с) запись снята"));
	if (CvQueue) { AsdaCVarSave::SetForTest(TEXT("asda.Fish.LegacyOtherQueue"), 1); }
	Feed(6173, B6173(1, 777778, 0, 0));
	Advance(11000); Step();
	Expect(F->PeerQueue.Num() == 1, TEXT("H3 контроль Р5: asda.Fish.LegacyOtherQueue 1 — запись вечная, как оригинал"));
	if (CvQueue) { AsdaCVarSave::SetForTest(TEXT("asda.Fish.LegacyOtherQueue"), 0); }
	F->PeerQueue.Reset();
	WS->DebugSpawnFakePeer(4321, TEXT("Рыбак"), Near + FVector2D(0.f, 200.f));
	const int32 PeerCharId = 900000 + 4321;   // `DebugSpawnFakePeer`: charId = 900000 + userNum
	int32 Fired = 0;
	const FDelegateHandle H = F->OnPeerFishState.AddLambda([&Fired](AActor*, const FAsdaFishPeerState&) { ++Fired; });
	Feed(6173, B6173(1, PeerCharId, 69577, 0));
	Step();
	const FAsdaFishPeerState* PS = F->FindPeerState(PeerCharId);
	Expect(PS && PS->Action == 61 && PS->bFishingFlag && PS->bHasFish && PS->FishCode == 69577 && Fired == 1 && F->PeerQueue.Num() == 0,
		FString::Printf(TEXT("H4 6173 фаза 1 (charId %d найден) → действие 61, флаг, рыба 69577, делегат ×%d"), PeerCharId, Fired));
	Feed(6173, B6173(3, PeerCharId, 0, 0));
	Step();
	PS = F->FindPeerState(PeerCharId);
	Expect(PS && (PS->Action == 63 || PS->Action == 64) && PS->bFishingFlag && PS->FishCode == 69577,
		TEXT("H5 фаза 3 → подсечка 63|64, флаги не тронуты (эталон)"));
	Feed(6173, B6173(1, PeerCharId, 69578, 77));
	Feed(6173, B6173(2, PeerCharId, 0, 0));
	Step();
	PS = F->FindPeerState(PeerCharId);
	Expect(PS && PS->Action == 0 && !PS->bFishingFlag && !PS->bHasFish && Fired == 4, TEXT("H6 фаза 2 → действие 0, флаг снят (две записи за кадр разобраны по порядку)"));
	F->OnPeerFishState.Remove(H);

	// ════════════════ I. 6164 ════════════════
	int32 SkillSeen = -1;
	const FDelegateHandle HS = F->OnFishSkillChanged.AddLambda([&SkillSeen](int32 S) { SkillSeen = S; });
	TArray<uint8> B64; B64.SetNumZeroed(4); AsdaBytes::WrI32(B64.GetData(), 123);
	Feed(6164, B64);
	Expect(F->GetFishingSkill() == 123 && F->IsFishingSkillKnown() && SkillSeen == 123, TEXT("I1 6164 → навык 123, сигнал окну персонажа (В6)"));
	F->OnFishSkillChanged.Remove(HS);

	// ════════════════ J. 6570 — износ ════════════════
	{
		int32 Cell = -1;
		for (int32 c = 0; c < Inv->GetContainerSize(1); ++c) { if (Inv->GetCellItemCode(1, c) == 0) { Cell = c; break; } }
		if (Cell >= 0)
		{
			UsedCells.Add({ 1, Cell });
			Inv->DebugPutItem(1, Cell, 69658, 1);
			TArray<uint8> D; D.SetNumZeroed(4); D[0] = 1; AsdaBytes::WrI16(D.GetData() + 1, Cell); D[3] = 77;
			AsdaTestNet::DebugFeedPacket(Net, 6570, D);       // штатный разбор → делегат → очередь инвентаря
			Inv->DebugPumpPendingPackets();      // ★ ревизия МЕЛК-5: разбор очереди тем же методом, что Tick инвентаря
			const FAsdaItemInstance* It = Inv->GetCell(1, Cell);
			Expect(It && It->Durability == 77, FString::Printf(TEXT("J1 6570 → прочность удочки в сумке %d (ждём 77)"), It ? (int32)It->Durability : -1));
			D[0] = 1; AsdaBytes::WrI16(D.GetData() + 1, 30); D[3] = 5;
			AsdaTestNet::DebugFeedPacket(Net, 6570, D);
			Inv->DebugPumpPendingPackets();
			Expect(It && It->Durability == 77, TEXT("J2 контроль: контейнер 1 слот 30 (> 0x1D) — вне ветки эталона, ничего"));
			Inv->DebugPutItem(1, Cell, 0, 0);    // убрать: иначе в K «первой удочкой сумки» станет она (эталонный порядок)
		}
		else { Expect(false, TEXT("J нет свободной ячейки сумки")); }
	}

	// ════════════════ K. АВТО-СНАРЯЖЕНИЕ 103 → 82 → 128 ════════════════
	{
		Inv->DebugPutItem(3, UAsdaFishingSubsystem::kEquipSlotRod, 0, 0);
		int32 RodCell = -1;
		for (int32 c = 0; c < Inv->GetContainerSize(1); ++c) { if (Inv->GetCellItemCode(1, c) == 0) { RodCell = c; break; } }
		if (RodCell >= 0)
		{
			UsedCells.Add({ 1, RodCell });
			Inv->DebugPutItem(1, RodCell, 69657, 1);
			if (FAsdaItemInstance* M = Inv->GetCellMutable(1, RodCell)) { M->Raw[13] = 40; M->SyncFromRaw(); }
			WM->ClearMsgBoxQueue(TEXT("самотест рыбалки: K"));
			F->FishState = 0;
			F->StartAtNearestSpot();
			Step();
			WM->CloseConfirm(true);
			Expect(F->GetFishState() == 103, TEXT("K1 81 OK без удочки → 103"));
			Sent.Reset(); Step();
			Expect(F->GetFishState() == 104 && F->bAutoRod && F->AutoRodBagCell == RodCell && WM->IsMsgBoxQueued(1, 82) &&
					Sent.Num() == 1 && Sent[0].Op == 5000,
					FString::Printf(TEXT("K2 103 → удочка из сумки (ячейка %d) «использована» штатно (ровно 1 отправка: %d, опкод %d — ждём 5000 «надеть»), диалог 82, 104"),
						RodCell, Sent.Num(), Sent.Num() ? Sent[0].Op : 0));
			WM->CloseConfirm(false);
			Expect(F->GetFishState() == 128, TEXT("K3 диалог 82 «Отмена» → 128"));
			Sent.Reset(); F->DebugLastReturnRodCell = -1; Step();
				// Офлайн сервер не меняет местами удочку и оружие: «вернуть» = снова «использовать» исходную ячейку (эталон
				//   `invFlaggedSlotTriggerUse`, g:605334). На провод повтор не уходит — инвентарь держит свою защёлку «действие
				//   в полёте» до ответа 5001, которого офлайн нет; утверждается сам вызов (ячейка возврата).
				Expect(F->GetFishState() == 0 && !F->bAutoRod && F->DebugLastReturnRodCell == RodCell,
					FString::Printf(TEXT("K4 128 → возврат авто-надетого: «использована» ячейка %d (ждём %d), отправок %d → 0"), F->DebugLastReturnRodCell, RodCell, Sent.Num()));
			// Нет удочки вовсе → плашка 111043 и 128.
			Inv->DebugPutItem(1, RodCell, 0, 0);
			F->StartAtNearestSpot(); Step(); WM->CloseConfirm(true);
				F->ClearRecentNotices();
				Step();
				Expect(F->GetFishState() == 128 && HadNotice(TEXT("plank"), 111043), TEXT("K5 103 без удочки нигде → плашка 221 111043 (журнал нотисов) → 128"));
			Step();
		}
	}

	// ════════════════ L. СБРОС СЕССИИ ════════════════
	{
		F->FishState = 0;
		F->StartAtNearestSpot(); Step();
		Expect(WM->IsMsgBoxQueued(1, 81), TEXT("L0 диалог 81 в очереди перед сменой карты"));
		F->SetAutoRepeat(false);
		// ★ Ревизия В1 ВАЖН-1: 6173 о соседе новой карты пришёл ДО PostLoadMap (одна запись в очереди пакетов, одна — уже в
		//   очереди чужих) — смена карты их не выбрасывает.
		F->PeerQueue.Reset();
		Feed(6173, B6173(1, 900000 + 4322, 69577, 0));
		Step();                                         // переложено в очередь чужих (персонажа ещё нет)
		AsdaTestNet::DebugFeedPacket(Net, 6173, B6173(1, 900000 + 4322, 69578, 0));   // второй — ещё в очереди пакетов
		F->ResetSessionState(EAsdaSessionResetReason::MapChanged);
		Expect(F->GetFishState() == 0 && !WM->IsMsgBoxQueued(1, 81) && F->GetFishingSkill() == 123 && !F->GetAutoRepeat(),
			TEXT("L1 смена карты: автомат 0, диалог 81 снят, навык сохранён, автоповтор игрока сохранён"));
		F->DrainPackets();
		Expect(F->PeerQueue.Num() == 2, FString::Printf(TEXT("L1a ВАЖН-1: 6173 до PostLoadMap пережили смену карты (в очереди чужих %d, ждём 2)"), F->PeerQueue.Num()));
		WS->DebugSpawnFakePeer(4322, TEXT("Рыбак2"), Near + FVector2D(200.f, 200.f));
		Step();
		const FAsdaFishPeerState* PS2 = F->FindPeerState(900000 + 4322);
		Expect(PS2 && PS2->FishCode == 69578 && F->PeerQueue.Num() == 0, TEXT("L1b сосед заспавнился → обе записи применены по порядку (последняя — 69578)"));
		Feed(6173, B6173(1, 900000 + 99999, 0, 0));
		Step();
		Advance(11000); Step();
		Expect(F->PeerQueue.Num() == 0, TEXT("L1c запись о так и не появившемся соседе снята TTL Р5 (asda.Fish.LegacyOtherQueue 0)"));
		F->FishState = 115; F->b6171InFlight = true;
		F->ResetSessionState(EAsdaSessionResetReason::Disconnected);
		Expect(F->GetFishState() == 0 && !F->b6171InFlight && F->GetFishingSkill() == 0 && F->GetAutoRepeat(),
			TEXT("L2 разрыв: полный сброс (автомат 0, защёлка, навык 0, автоповтор 1 — cmapInitReset)"));
		TArray<uint8> B64b; B64b.SetNumZeroed(4); AsdaBytes::WrI32(B64b.GetData(), 55);
		AsdaTestNet::DebugFeedPacket(Net, 6164, B64b);           // пришёл до 1021 — лежит в очереди
		F->ResetSessionState(EAsdaSessionResetReason::EnteringWorld);
		F->DrainPackets();
		Expect(F->GetFishingSkill() == 55, TEXT("L3 вход в мир (1021): 6164 из очереди не выброшен (session-reset-drops-early-data)"));
		// ★ fishfix 09.10 (ревизия МЕЛК-5): портал посреди рыбалки с авто-надетой удочкой. Оригинал флаги `g_pInventoryMgr` +8/+9
		//   на портале не трогает — ближайшее 128 возвращает прежнее оружие; порт: флаги сохраняются, 128 — в покое, когда мир готов.
		{
			IConsoleVariable* CvRet = IConsoleManager::Get().FindConsoleVariable(TEXT("asda.Fish.MapReturnAuto"));
			int32 RodCell = -1;
			for (int32 c = 0; c < Inv->GetContainerSize(1); ++c) { if (Inv->GetCellItemCode(1, c) == 0) { RodCell = c; break; } }
			if (RodCell >= 0)
			{
				UsedCells.Add({ 1, RodCell });
				Inv->DebugPutItem(1, RodCell, 69657, 1);   // после «обмена» в исходной ячейке лежит снятое прежде (эталонная схема)
				F->FishState = 121; F->bAutoRod = true; F->AutoRodBagCell = RodCell;
				F->ResetSessionState(EAsdaSessionResetReason::MapChanged);
				Expect(F->GetFishState() == 0 && F->bAutoRod && F->AutoRodBagCell == RodCell && F->bReturnAutoAfterMap,
					TEXT("L4a МЕЛК-5: смена карты посреди рыбалки — автомат 0, флаг авто-удочки и ячейка СОХРАНЕНЫ (как g_pInventoryMgr+8 эталона)"));
				F->TestWorldReadyOverride = 0;
				Sent.Reset(); Step();
				Expect(F->bAutoRod && F->bReturnAutoAfterMap && Sent.Num() == 0,
					TEXT("L4b МЕЛК-5: мир ещё не готов (сессия не в мире / загрузка) — возврат ждёт, отправок нет"));
				F->TestWorldReadyOverride = 1;
				F->DebugLastReturnRodCell = -1;
				Sent.Reset(); Step();
				Expect(F->GetFishState() == 0 && !F->bAutoRod && !F->bReturnAutoAfterMap && F->DebugLastReturnRodCell == RodCell,
					FString::Printf(TEXT("L4c МЕЛК-5: мир готов — ветка 128: «использована» ячейка %d (ждём %d), флаги сняты, автомат 0"),
						F->DebugLastReturnRodCell, RodCell));
				// Контроль: ключ 0 — прежний порт (флаги забываются на смене карты).
				if (CvRet) { AsdaCVarSave::SetForTest(TEXT("asda.Fish.MapReturnAuto"), 0); }
				F->FishState = 121; F->bAutoRod = true; F->AutoRodBagCell = RodCell;
				F->ResetSessionState(EAsdaSessionResetReason::MapChanged);
				Expect(CvRet && !F->bAutoRod && F->AutoRodBagCell == -1 && !F->bReturnAutoAfterMap,
					TEXT("L4d контроль МЕЛК-5: asda.Fish.MapReturnAuto 0 — флаги авто-снаряжения забыты (прежний порт)"));
				if (CvRet) { AsdaCVarSave::SetForTest(TEXT("asda.Fish.MapReturnAuto"), 1); }
				F->TestWorldReadyOverride = -1;
				Inv->DebugPutItem(1, RodCell, 0, 0);
			}
			else { Expect(false, TEXT("L4 нет свободной ячейки сумки")); }
		}
	}

	// ════════════════ M. СНАСТЬ: ПОРЯДОК АРГУМЕНТОВ ПОДТВЕРЖДЕНИЯ 111026/111028/111029/111030 ════════════════
	// ★ 09.10.2026 (аудит fmtArg, `Reports/owner_1009/fmtarg_audit.md`). Путь игрока: двойной клик по материалу
	//   (контейнер 2) → двойной клик по удочке (контейнер 1) → диалог (1, 78). По байтам эталона (0xD6FFD4…0xD703A5)
	//   ПЕРВЫЙ `%s` — материал, второй — удочка, третий (замена) — «материал +N». Контроль: прежний порядок порта
	//   (удочка первой) с видимым текстом НЕ совпадает.
	{
		UAsdaFishingToolSubsystem* FT = GI->GetSubsystem<UAsdaFishingToolSubsystem>();   // окно снасти
		UAsdaItemTableSubsystem* IT = GI->GetSubsystem<UAsdaItemTableSubsystem>();       // имена предметов
		// N-я пара «[…]» видимого текста (0 — первая); нет пары — «<нет>».
		auto BracketArg = [](const FString& Text, int32 N) -> FString
		{
			int32 From = 0;   // позиция поиска очередной «[»
			for (int32 i = 0; ; ++i)
			{
				const int32 L = Text.Find(TEXT("["), ESearchCase::CaseSensitive, ESearchDir::FromStart, From);   // начало пары
				const int32 R = (L >= 0) ? Text.Find(TEXT("]"), ESearchCase::CaseSensitive, ESearchDir::FromStart, L + 1) : INDEX_NONE;   // конец пары
				if (L < 0 || R < 0) { return FString(TEXT("<нет>")); }
				if (i == N) { return Text.Mid(L + 1, R - L - 1); }
				From = R + 1;
			}
		};
		// Ожидаемый видимый текст loc-строки с подстановкой Args (теги сняты).
		auto Want = [this](int32 Id, const TArray<FString>& Args) -> FString
		{
			return FAsdaMarkup::StripTags(AsdaUi::FormatPrintfArgs(AsdaNotice::LocRaw(GI, Id, true), Args));
		};
		int32 RodCell = -1;   // свободная ячейка сумки под удочку
		int32 MatCell = -1;   // свободная ячейка контейнера 2 под материал
		for (int32 c = 0; c < Inv->GetContainerSize(1) && RodCell < 0; ++c) { if (Inv->GetCellItemCode(1, c) == 0) { RodCell = c; } }
		for (int32 c = 0; c < Inv->GetContainerSize(2) && MatCell < 0; ++c) { if (Inv->GetCellItemCode(2, c) == 0) { MatCell = c; } }
		if (!FT || !IT || RodCell < 0 || MatCell < 0)
		{
			Expect(false, TEXT("M нет окна снасти / таблицы предметов / свободных ячеек"));
		}
		else
		{
			UsedCells.Add({ 1, RodCell }); UsedCells.Add({ 2, MatCell });
			constexpr int32 kRod = 69657;   // «Старая удочка» (подтип 10, вариант 1)
			const FString RodName = IT->GetItemName(kRod);   // имя удочки
			// Один случай: материал, код/запас снасти в записи удочки (+41/+43 леска, +45/+47 крючок), ждём loc и «+N».
			struct FCase { const TCHAR* Tag; int32 Mat; int32 CodeOff; int32 Code; int32 Val; int32 Loc; bool bReplace; };
			const FCase Cases[] = {
				{ TEXT("M1 леска 69687 (LinkCode 1), снасти нет"),              69687, 41,  0, 0, 111026, false },
				{ TEXT("M2 леска 69687, на удочке уже органика 96 +7"),           69687, 41, 96, 7, 111028, true  },
				{ TEXT("M3 крючок 69697 (LinkCode 1), снасти нет"),             69697, 45,  0, 0, 111029, false },
				{ TEXT("M4 крючок 69697, на удочке уже органика 96 +5"),          69697, 45, 96, 5, 111030, true  },
				{ TEXT("M5 крючок 69702 (LinkCode 2), на удочке синтетика 97 +3"), 69702, 45, 97, 3, 111030, true  },
				{ TEXT("M6 контроль: крючок 69702 (LinkCode 2), на удочке органика 96 — не замена"), 69702, 45, 96, 4, 111029, false },
			};
			for (const FCase& C : Cases)
			{
				WM->ClearMsgBoxQueue(TEXT("самотест рыбалки: M"));
				FT->CancelPending(true);
				Inv->DebugPutItem(1, RodCell, kRod, 1);
				Inv->DebugPutItem(2, MatCell, C.Mat, 1);
				if (FAsdaItemInstance* R = Inv->GetCellMutable(1, RodCell))
				{
					AsdaBytes::WrI16(R->Raw.GetData() + C.CodeOff, C.Code);       // код снасти (96/97)
					AsdaBytes::WrI16(R->Raw.GetData() + C.CodeOff + 2, C.Val);    // запас снасти N
				}
				const FString MatName = IT->GetItemName(C.Mat);   // имя материала
				const bool bArmed = FT->OnInventoryDoubleClick(2, MatCell);
				const bool bConf = FT->OnInventoryDoubleClick(1, RodCell);
				const FString Top = FAsdaMarkup::StripTags(WM->GetMsgBoxQueueTopText());   // видимый текст диалога
				const FString Plus = FString::Printf(TEXT("%s +%d"), *MatName, C.Val);    // «материал +N»
				const FString Exp = C.bReplace ? Want(C.Loc, { MatName, RodName, Plus }) : Want(C.Loc, { MatName, RodName });
				const FString Old = C.bReplace ? Want(C.Loc, { Plus, RodName, MatName }) : Want(C.Loc, { RodName, MatName });
				const bool bBr = BracketArg(Top, 0) == MatName && BracketArg(Top, 1) == RodName && (!C.bReplace || BracketArg(Top, 2) == Plus);
				Expect(bArmed && bConf && WM->IsMsgBoxQueued(1, 78) && !Exp.IsEmpty() && Top == Exp && bBr && Top != Old,
					FString::Printf(TEXT("%s → диалог (1, 78) %d: [0]=«%s» (ждём материал «%s»), [1]=«%s» (ждём удочку «%s»)%s; текст «%s»"),
						C.Tag, C.Loc, *BracketArg(Top, 0), *MatName, *BracketArg(Top, 1), *RodName,
						C.bReplace ? *FString::Printf(TEXT(", [2]=«%s» (ждём «%s»)"), *BracketArg(Top, 2), *Plus) : TEXT(""), *Top));
			}
			FT->CancelPending(true);
			WM->ClearMsgBoxQueue(TEXT("самотест рыбалки: M конец"));
			Inv->DebugPutItem(1, RodCell, 0, 0);
			Inv->DebugPutItem(2, MatCell, 0, 0);
		}
	}

	// ── Восстановление — ON_SCOPE_EXIT выше (ревизия В2 МЕЛК-8: и при раннем выходе) ──
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaFishingSelfTest(
	TEXT("asda.Fish.SelfTest"),
	TEXT("asda.Fish.SelfTest [delay=N] [quit] — ОФЛАЙН-самотест рыбалки В1: данные, автомат 100…128, 6167/6169/6171 байт в байт ")
	TEXT("(30/32/31 б с конвертом, charId i32), приёмы 6164/6168/6170/6172/6173/6183/6570 через HandlePacket, Р1–Р5, сброс сессии"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		// ★ 11.10.2026 (В3 S1): аргументы, офлайн-гейт со вторым барьером и итог отказа с `quit` — общий каркас.
		const FAsdaSelfTestArgs Args = FAsdaSelfTestArgs::Parse(A);
		const float Delay = Args.Delay;   // задержка запуска, с
		const bool bQuit = Args.bQuit;    // выйти после прогона
		if (AsdaSelfTestKit::RefuseStart(W, TEXT("asda.Fish.SelfTest"), Args)) { return; }
		if (!W) { AsdaSelfTestKit::Refused(nullptr, TEXT("asda.Fish.SelfTest"), bQuit, TEXT("нет мира")); return; }
		TWeakObjectPtr<UWorld> WW = W;
		auto Go = [WW, bQuit]()
		{
			UWorld* Wd = WW.Get();
			if (!Wd) { AsdaSelfTestKit::Refused(nullptr, TEXT("asda.Fish.SelfTest"), bQuit, TEXT("мир ушёл до старта")); return; }
			FAsdaFishingSelfTest T;
			T.Run(Wd);
			AsdaSelfTestKit::Finish(Wd, TEXT("asda.Fish.SelfTest"), bQuit, LogAsdaFishTest, TEXT("САМОТЕСТ-РЫБА"), T.Ok, T.Fail);
		};
		// Свой персонаж офлайн: штатные крюки `Asda.Load.CurrentMap 3` + `Asda.World.FakeSelfSpawn` (как самотесты 4007/4008)
		//   у первого спота Альпии (тайлы 139, 77), затем прогон через 3 с (спавн занимает несколько кадров).
		auto SpawnThenGo = [WW, Go, bQuit]()
		{
			UWorld* Wd = WW.Get();
			if (!Wd) { return; }
			if (AsdaSelfTestKit::RefuseDeferred(Wd, TEXT("asda.Fish.SelfTest"), bQuit)) { return; }   // Р-2: офлайн повторно — на отложенном старте
			APlayerController* P = Wd->GetFirstPlayerController();
			if ((!P || !P->GetPawn()) && GEngine)
			{
				GEngine->Exec(Wd, TEXT("Asda.Load.CurrentMap 3"));
				GEngine->Exec(Wd, TEXT("Asda.World.FakeSelfSpawn 139 77"));
				FTimerHandle H2;
				Wd->GetTimerManager().SetTimer(H2, FTimerDelegate::CreateLambda(Go), 3.f, false);
				return;
			}
			Go();
		};
		if (Delay <= 0.f) { SpawnThenGo(); return; }
		FTimerHandle H;
		W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda(SpawnThenGo), Delay, false);
	}));

#endif // !UE_BUILD_SHIPPING
