// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ «ТОЧКА СОХРАНЕНИЯ У NPC» (26.10.2026, savepoint; баг игроков «Setting or saving a respawn location fails to
//   register»). [ИСПРАВЛЕНИЕ] Кнопка NPC 30022 прежде показывала баннер 6178 и НИЧЕГО не слала — точка в БД оставалась
//   стартовой (Альпен 125/390), возрождение и свиток возврата вели туда же. Ключ `asda.Npc.SavePointSend` (1 — как оригинал).
//
// ★ ЧТО ПРОВЕРЯЕТ (реальный путь игрока, без сети — закон diagnostic-must-use-real-path):
//   A. Тексты 6178/6179/6180 на языке сессии (RU — дословно GameText.csv, EN — DT_Loc_GameText_en.csv) и подпись кнопки.
//   B. Разговор с NPC 101 «Эко» (Альпен) `OpenForNpcId(101)` → кнопка 0 (`OnDialogButton(581)`, код 30022) — ТЕМ ЖЕ кодом, что мышь:
//      вопрос TWOBUT в очереди парой (8, 6178) с текстом 6178, пакета ещё нет, разговор открыт; повторное нажатие очередь не
//      плодит (`objList_ContainsPair`).
//   C. «Да» (`AnswerMsgBox(true, true)`) → ровно один 5280: конверт MD5 без секрета ('5'), пад, тело 6 б
//      {i16 npcId 101, i16 тайл X, i16 тайл Z} = тайл персонажа, посчитанный здесь НЕЗАВИСИМО (floor(game/100));
//      журнал +1 строка 6179 белым, плашка 221 +1 строка 6179; вопроса в очереди больше нет.
//   D. «Нет» → ни 5280, ни 6179. E. Ключ 0 → вопроса нет, 5280 нет (прежний порт).
//   F. МОДЕЛЬ СЕРВЕРА по его коду (GameServer.c): `sub_52A360` пишет точку {карта сервера, X, Z} при X>0 && Z>0;
//      смерть → 4054 → `CharRegenVillage` (карта точки == карта сервера → `CharRegenVillageMove` → 4056 {uidx, X, Z, HP, MP, exp});
//      свиток возврата (тип +27 == 16) → `CharSpendItemUse` → тот же `CharRegenVillage(…, 1)`.
//      F1. «отошёл» на (X+15, Z+15) серверным 4056 → умер → 4056 точки → персонаж на тайле точки, «мёртв» снят.
//      F2. «отошёл» → свиток → 4056 точки → персонаж на тайле точки.
//      F3. Точка на ДРУГОЙ карте → 4055 {карта точки, IP:порт канала, X, Z} — канал 1 (15601) и канал 2 (15621): клиент берёт
//          карту/тайл/адрес ИЗ ПАКЕТА (точка от канала не зависит: сервер резолвит IP/порт своего канала по VillageServerConfig
//          при сохранении и при каждом входе, DB `VillagePos` общая) — состояние перехода снимается сразу (реконнекта нет).
//   Состояние (свой номер, смерть, тайл, адрес сервера, позиция павна, ключи, очередь окон) возвращается в конце.
//
// Консоль: `asda.SelfTest.SavePoint [quit]`. Только офлайн (-AsdaNoNet), карта мира (alpen). Строки «ТОЧКА-СОХР», итог
//   «ТОЧКА-СОХР ИТОГ: ok=… fail=…». EN — `--lang en` пакетного прогона.
#include "CoreMinimal.h"
#include "AsdaDevConsole.h"
#include "Test/AsdaSelfTestInput.h"   // ★ R5-c (12.11.2026): общая точка самотестов вместо своих копий помощников

#if ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING

#include "AsdaNetSubsystem.h"
#include "AsdaBytes.h"
#include "Test/AsdaSelfTestKit.h"
#include "Npc/AsdaNpcDialogSubsystem.h"
#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaChatSubsystem.h"
#include "UI/AsdaNotice.h"
#include "Text/AsdaLang.h"
#include "World/AsdaPlayerCharacter.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Engine/Engine.h"                 // GEngine->Exec — офлайн-спавн своего павна
#include "TimerManager.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaSavePointTest, Log, All);

namespace AsdaSavePointSelfTest
{
	static const TCHAR* kCmd = TEXT("asda.SelfTest.SavePoint");   // имя команды (итог, отказ, ключи)
	static constexpr int32 kNpcEco = 101;                          // «Эко», Альпен (tile 111,395), кнопка 0 = 30022
	static constexpr uint16 kOp5280 = 5280;                        // C→S «сохранить местоположение»

	// Один перехваченный исходящий пакет.
	struct FSent { uint16 Op = 0; TArray<uint8> Body; bool bPad = false; TCHAR Env = 0; };

	// Строка дампа окна разговора по имени контрола.
	using AsdaSelfTestInput::DumpLine;   // ★ R5-c (12.11.2026): общая точка (`Test/AsdaSelfTestInput.h`) — копия была дословной.

	// Тело 4056 (22 б) — `CharRegenVillageMove@0x52AB80` (GameServer.c:113521-113528): uidx, тайл X/Z, HP i32, MP i16, exp i64, @20 0.
	static TArray<uint8> Body4056(int32 Uidx, int32 X, int32 Z, int32 Hp, int32 Mp, int64 Exp)
	{
		TArray<uint8> B; B.SetNumZeroed(22);
		AsdaBytes::WrI16(&B[0], (int16)Uidx); AsdaBytes::WrI16(&B[2], (int16)X); AsdaBytes::WrI16(&B[4], (int16)Z);
		AsdaBytes::WrI32(&B[6], Hp); AsdaBytes::WrI16(&B[10], (int16)Mp); FMemory::Memcpy(&B[12], &Exp, 8);
		return B;
	}

	// Тело 4055 (43 б) — `CharRegenVillage@0x52A780` (GameServer.c:113409-113440, v21..v28): uidx, карта u8, IP char[20],
	//   порт u16, тайл X/Z, HP i32, MP i16, exp i64.
	static TArray<uint8> Body4055(int32 Uidx, int32 Map, const char* Ip, int32 Port, int32 X, int32 Z, int32 Hp, int32 Mp, int64 Exp)
	{
		TArray<uint8> B; B.SetNumZeroed(43);
		AsdaBytes::WrI16(&B[0], (int16)Uidx); B[2] = (uint8)Map;
		FMemory::Memcpy(&B[3], Ip, FMath::Min<int32>(FCStringAnsi::Strlen(Ip), 19));
		AsdaBytes::WrI16(&B[23], (int16)(uint16)Port); AsdaBytes::WrI16(&B[25], (int16)X); AsdaBytes::WrI16(&B[27], (int16)Z);
		AsdaBytes::WrI32(&B[29], Hp); AsdaBytes::WrI16(&B[33], (int16)Mp); FMemory::Memcpy(&B[35], &Exp, 8);
		return B;
	}

	static void Body(const FAsdaSelfTestArgs& Args, UWorld* W);

	// Старт: офлайн-гейт; нет своего павна — офлайн-спавн штатными крюками (как у asda.Action.SelfTest) на Альпене рядом с «Эко»
	//   (NPC 101 стоит на тайле 111,395); прогон — через 2 с (павн и HUD успевают подняться).
	static void Run(const TArray<FString>& A, UWorld* W)
	{
		const FAsdaSelfTestArgs Args = FAsdaSelfTestArgs::Parse(A);
		const uint16 Extra[] = { kOp5280 };
		if (AsdaSelfTestKit::RefuseStart(W, kCmd, Args, 0.f, Extra)) { return; }
		if (!W) { return; }
		const bool bSpawn = AsdaSelfTestKit::NeedSelfSpawn(W) && GEngine;
		if (bSpawn)
		{
			GEngine->Exec(W, TEXT("Asda.Load.CurrentMap 3"));
			GEngine->Exec(W, TEXT("Asda.World.FakeSelfSpawn 115 396"));
		}
		TWeakObjectPtr<UWorld> WW(W);
		FTimerHandle H;
		W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda([WW, Args]()
		{
			if (UWorld* Wd = WW.Get()) { Body(Args, Wd); }
		}), bSpawn ? 2.f : 0.05f, false);
	}

	static void Body(const FAsdaSelfTestArgs& Args, UWorld* W)
	{
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
		UAsdaNpcDialogSubsystem* Dlg = GI ? GI->GetSubsystem<UAsdaNpcDialogSubsystem>() : nullptr;
		UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
		UAsdaChatSubsystem* Chat = GI ? GI->GetSubsystem<UAsdaChatSubsystem>() : nullptr;
		APlayerController* PC = GI ? GI->GetFirstLocalPlayerController() : nullptr;
		AAsdaPlayerCharacter* Ch = PC ? Cast<AAsdaPlayerCharacter>(PC->GetPawn()) : nullptr;
		if (!Net || !Dlg || !WM || !Chat || !Ch)
		{
			AsdaSelfTestKit::Refused(W, kCmd, Args.bQuit, TEXT("нет подсистем (сеть/диалог/окна/чат) или своего персонажа — нужна карта мира"));
			return;
		}

		int32 Ok = 0, Fail = 0;
		auto Expect = [&Ok, &Fail](bool b, const FString& What)
		{
			if (b) { ++Ok; UE_LOG(LogAsdaSavePointTest, Log, TEXT("ТОЧКА-СОХР ok   %s"), *What); }
			else { ++Fail; UE_LOG(LogAsdaSavePointTest, Error, TEXT("ТОЧКА-СОХР FAIL %s"), *What); }
		};

		FAsdaNetSelfScope SelfScope(Net, kCmd);   // свой номер, смерть, … возвращаются на выходе
		FAsdaCVarScope Cv(W, kCmd);
		Cv.Set(TEXT("asda.Npc.SavePointSend"), 1);
		if (!Net->bSelfUserNumKnown) { Net->SelfUserNum = 7; Net->bSelfUserNumKnown = true; }
		const int32 Uidx = (int32)Net->SelfUserNum;
		const FVector2D PosWas = Ch->GetGamePosXZ();
		const int32 TileXWas = Net->SelfTileX, TileZWas = Net->SelfTileZ;
		const FString IpWas = Net->GameServerIP; const int32 PortWas = Net->GameServerPort;
		const int32 MapWas = Net->EnterMapId; const bool bConnWas = Net->bInGameConnected;
		if (Net->EnterMapId < 0) { Net->EnterMapId = 3; }   // офлайн-спавн на Альпене (карта 3) — «карта сервера» модели; вернётся в конце
		const int32 CurMap = Net->EnterMapId;
		const UAsdaNetSubsystem::FAsdaVitals* V = Net->GetVitals(Uidx);
		const int32 Hp = (V && V->HpCur >= 0) ? V->HpCur : 100, Mp = (V && V->MpCur >= 0) ? V->MpCur : 50;
		const int64 Exp = Net->SelfExp >= 0 ? Net->SelfExp : 0;
		WM->ClearMsgBoxQueue(TEXT("самотест точки сохранения: старт"));
		if (Dlg->IsDialogOpen()) { Dlg->Close(); }

		// ════════ A. Тексты на языке сессии ════════
		const bool bEn = !FAsdaLang::IsRu();   // язык сессии (-AsdaLang=ru|en)
		const FString T6178 = AsdaNotice::Loc(GI, 6178), T6179 = AsdaNotice::Loc(GI, 6179), T6180 = AsdaNotice::Loc(GI, 6180);
		const TCHAR* E6178 = bEn ? TEXT("Save your current location.") : TEXT("Сохранить текущее местоположение.");
		const TCHAR* E6179 = bEn ? TEXT("Your current location has been saved.") : TEXT("Текущее местоположение сохранено.");
		const TCHAR* E6180 = bEn ? TEXT("Cannot save a location while NPC window is closed.")
		                         : TEXT("При закрытом окне персонажа сохранить местоположение не удастся.");
		Expect(T6178 == E6178, FString::Printf(TEXT("A: [%s] loc 6178 «%s»"), bEn ? TEXT("en") : TEXT("ru"), *T6178));
		Expect(T6179 == E6179, FString::Printf(TEXT("A: [%s] loc 6179 «%s»"), bEn ? TEXT("en") : TEXT("ru"), *T6179));
		Expect(T6180 == E6180, FString::Printf(TEXT("A: [%s] loc 6180 «%s»"), bEn ? TEXT("en") : TEXT("ru"), *T6180));

		// ════════ B. Разговор → кнопка 30022 → вопрос (8, 6178) ════════
		TArray<FSent> Sent;
		const uint32 Gen = AsdaSelfTestKit::ArmCapture(Net, [&Sent](uint16 Op, const uint8* B, int32 N, bool bPad, TCHAR Env)
		{
			FSent S; S.Op = Op; S.bPad = bPad; S.Env = Env; if (B && N > 0) { S.Body.Append(B, N); } Sent.Add(MoveTemp(S));
		}, 0, kCmd);
		Expect(Gen != 0, TEXT("перехват исходящих взведён (весь трафик — офлайн связь только через перехват)"));
		auto Count5280 = [&Sent]() { int32 N = 0; for (const FSent& S : Sent) { if (S.Op == kOp5280) { ++N; } } return N; };

		Expect(Dlg->OpenForNpcId(kNpcEco), TEXT("B: окно разговора с NPC 101 «Эко» открылось"));
		{
			const FString B0 = DumpLine(Dlg->DumpWindow(), TEXT("NPC_SHOP_BUT00"));
			Expect(B0.Contains(TEXT("видим=1")) && B0.Contains(TEXT("код=30022")), FString::Printf(TEXT("B: кнопка 0 видна, код 30022: %s"), *B0.TrimStart()));
		}
		Dlg->OnDialogButton(581);
		Expect(WM->IsMsgBoxQueued(8, 6178), TEXT("B: в очереди вопрос TWOBUT (8, 6178)"));
		Expect(WM->GetMsgBoxQueueTopText() == T6178, FString::Printf(TEXT("B: текст вопроса «%s»"), *WM->GetMsgBoxQueueTopText()));
		Expect(Count5280() == 0, TEXT("B: до ответа 5280 не ушёл"));
		Expect(Dlg->IsDialogOpen(), TEXT("B: разговор с NPC остаётся открытым (эталон его не трогает)"));
		const int32 QN = WM->GetMsgBoxQueueNum();
		Dlg->OnDialogButton(581);
		Expect(WM->GetMsgBoxQueueNum() == QN, FString::Printf(TEXT("B: повторное нажатие очередь не плодит (%d → %d)"), QN, WM->GetMsgBoxQueueNum()));

		// ════════ C. «Да» → 5280 + 6179 ════════
		const FIntPoint ExpTile(FMath::FloorToInt(PosWas.X / 100.f), FMath::FloorToInt(PosWas.Y / 100.f));   // независимо от боевого кода
		const int32 ChatBefore = Chat->DebugCountSystemLines(T6179), WarBefore = Chat->DebugCountWarLines(T6179);
		WM->AnswerMsgBox(/*bTwoBut*/true, /*bYes*/true);
		int32 SaveX = -1, SaveZ = -1;
		{
			const FSent* P = Sent.FindByPredicate([](const FSent& S) { return S.Op == kOp5280; });
			Expect(Count5280() == 1, FString::Printf(TEXT("C: «Да» → ровно один 5280 (ушло %d)"), Count5280()));
			if (P)
			{
				const int32 Npc = P->Body.Num() >= 6 ? AsdaBytes::RdI16(&P->Body[0]) : -99;
				SaveX = P->Body.Num() >= 6 ? AsdaBytes::RdI16(&P->Body[2]) : -1;
				SaveZ = P->Body.Num() >= 6 ? AsdaBytes::RdI16(&P->Body[4]) : -1;
				Expect(P->Env == TEXT('5') && P->bPad && P->Body.Num() == 6,
					FString::Printf(TEXT("C: конверт MD5 без секрета ('%c'), пад %d, тело %d б (ждём '5', 1, 6)"), P->Env ? P->Env : TEXT('?'), P->bPad ? 1 : 0, P->Body.Num()));
				Expect(Npc == kNpcEco && SaveX == ExpTile.X && SaveZ == ExpTile.Y,
					FString::Printf(TEXT("C: тело {npcId %d, X %d, Z %d} (ждём {101, %d, %d} — тайл персонажа)"), Npc, SaveX, SaveZ, ExpTile.X, ExpTile.Y));
			}
		}
		FColor Col(0, 0, 0, 0);
		const bool bHasCol = Chat->DebugFindLastSystemLineColor(T6179, Col);
		Expect(Chat->DebugCountSystemLines(T6179) == ChatBefore + 1 && bHasCol && Col == FColor(0xFF, 0xFF, 0xFF, 0xFF),
			FString::Printf(TEXT("C: журнал +1 строка 6179 белым (%d → %d, цвет %s)"), ChatBefore, Chat->DebugCountSystemLines(T6179), *Col.ToHex()));
		Expect(Chat->DebugCountWarLines(T6179) == WarBefore + 1, FString::Printf(TEXT("C: плашка 221 +1 строка 6179 (%d → %d)"), WarBefore, Chat->DebugCountWarLines(T6179)));
		Expect(!WM->IsMsgBoxQueued(8, 6178), TEXT("C: вопроса (8, 6178) в очереди больше нет"));

		// ════════ D. «Нет» → ничего ════════
		{
			const int32 N0 = Count5280(), C0 = Chat->DebugCountSystemLines(T6179);
			Dlg->OnDialogButton(581);
			Expect(WM->IsMsgBoxQueued(8, 6178), TEXT("D: вопрос снова в очереди"));
			WM->AnswerMsgBox(true, false);
			Expect(Count5280() == N0 && Chat->DebugCountSystemLines(T6179) == C0, TEXT("D: «Нет» → ни 5280, ни 6179"));
		}
		// ════════ E. Ключ 0 → прежний порт (вопроса нет, пакета нет) ════════
		{
			Cv.Set(TEXT("asda.Npc.SavePointSend"), 0);
			const int32 N0 = Count5280();
			Dlg->OnDialogButton(581);
			Expect(!WM->IsMsgBoxQueued(8, 6178) && Count5280() == N0, TEXT("E: ключ 0 → вопроса (8, 6178) нет, 5280 нет (прежний порт)"));
			Cv.Set(TEXT("asda.Npc.SavePointSend"), 1);
		}
		AsdaSelfTestKit::DisarmCapture(Net, Gen);
		if (Dlg->IsDialogOpen()) { Dlg->Close(); }
		WM->ClearMsgBoxQueue(TEXT("самотест точки сохранения: после диалога"));

		// ════════ F. Модель сервера: возрождение и свиток возврата ════════
		const bool bServerSaved = SaveX > 0 && SaveZ > 0;   // `sub_52A360`: «if( (sReq.iPosX <= 0) || (sReq.iPosZ <= 0) )» → не пишет
		Expect(bServerSaved, FString::Printf(TEXT("F: сервер принял бы точку (X %d, Z %d > 0) — карта сервера %d"), SaveX, SaveZ, CurMap));
		auto TileOfPawn = [Ch]() { const FVector2D P = Ch->GetGamePosXZ(); return FIntPoint(FMath::FloorToInt(P.X / 100.f), FMath::FloorToInt(P.Y / 100.f)); };
		if (bServerSaved)
		{
			// F1. Отошёл → умер → 4054 → CharRegenVillage → (карта точки == карта сервера) CharRegenVillageMove → 4056 точки.
			Net->DebugFeedPacket(4056, Body4056(Uidx, SaveX + 15, SaveZ + 15, Hp, Mp, Exp));
			Expect(TileOfPawn() == FIntPoint(SaveX + 15, SaveZ + 15), FString::Printf(TEXT("F1: персонаж отошёл на (%d,%d)"), TileOfPawn().X, TileOfPawn().Y));
			Net->bSelfDead = true;
			Net->DebugFeedPacket(4056, Body4056(Uidx, SaveX, SaveZ, Hp, Mp, Exp));
			Expect(!Net->bSelfDead && TileOfPawn() == FIntPoint(SaveX, SaveZ) && Net->SelfTileX == SaveX && Net->SelfTileZ == SaveZ,
				FString::Printf(TEXT("F1: смерть → возрождение в точке сохранения (%d,%d): персонаж на (%d,%d), мёртв=%d"),
					SaveX, SaveZ, TileOfPawn().X, TileOfPawn().Y, Net->bSelfDead ? 1 : 0));
			// F2. Отошёл → свиток возврата (тип +27 == 16) → CharSpendItemUse → CharRegenVillage(…, 1) → тот же 4056.
			Net->DebugFeedPacket(4056, Body4056(Uidx, SaveX + 15, SaveZ + 15, Hp, Mp, Exp));
			Net->DebugFeedPacket(4056, Body4056(Uidx, SaveX, SaveZ, Hp, Mp, Exp));
			Expect(TileOfPawn() == FIntPoint(SaveX, SaveZ), FString::Printf(TEXT("F2: свиток возврата → персонаж в точке (%d,%d)"), TileOfPawn().X, TileOfPawn().Y));
			// F3. Точка на ДРУГОЙ карте → 4055 по каналу (порт 15601 / 15621): карта, тайл и адрес — из пакета.
			const int32 OtherMap = (CurMap == 3) ? 0 : 3;   // точка сохранена на другой карте
			for (const int32 Port : { 15601, 15621 })
			{
				Net->DebugFeedPacket(4055, Body4055(Uidx, OtherMap, "@@ASTORY_SERVER_WAN_IP@@", Port, SaveX, SaveZ, Hp, Mp, Exp));
				Expect(Net->bPortalPending && Net->PortalTileX == SaveX && Net->PortalTileZ == SaveZ && Net->EnterMapId == OtherMap
					&& Net->GameServerPort == Port,
					FString::Printf(TEXT("F3: точка на карте %d, канал порта %d → переход: карта %d, тайл (%d,%d), сервер :%d"),
						OtherMap, Port, Net->EnterMapId, Net->PortalTileX, Net->PortalTileZ, Net->GameServerPort));
				Net->ClearPortalTransferState(TEXT("самотест точки сохранения: F3"));
				Net->EnterMapId = CurMap; Net->GameServerIP = IpWas; Net->GameServerPort = PortWas; Net->bInGameConnected = bConnWas;
			}
		}

		// ── Возврат состояния ──
		Ch->NetSnapHard(PosWas);
		Net->SelfTileX = TileXWas; Net->SelfTileZ = TileZWas; Net->EnterMapId = MapWas;
		WM->ClearMsgBoxQueue(TEXT("самотест точки сохранения: конец"));
		SelfScope.Restore();
		Cv.Restore();
		AsdaSelfTestKit::Finish(W, kCmd, Args.bQuit, LogAsdaSavePointTest, TEXT("ТОЧКА-СОХР"), Ok, Fail);
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestSavePoint(
	TEXT("asda.SelfTest.SavePoint"),
	TEXT("Самотест точки сохранения у NPC (30022): вопрос (8, 6178) → «Да» → 5280 + 6179; «Нет»/ключ 0 — ничего; модель сервера: "
	     "смерть и свиток возврата → 4056 в точку, точка на другой карте → 4055 (оба канала). Только офлайн, карта мира. `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		AsdaSavePointSelfTest::Run(A, W);
	}));

#endif // ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING
