// Copyright (c) 2026 @sitxovski (Discord)
// ★ САМОТЕСТ «ТЕЛО И ТРАНСПОРТ ПОСЛЕ ВХОДА И ПОРТАЛА» (30.10.2026): `asda.SelfTest.PossessMountPortal [quit]`.
//   Два бага игроков/владельца, общий корень — что свой павн получает на КАЖДОМ спавне (вход, портал, смена канала):
//   • баг 5 «прошёл портал верхом — на новой карте транспорт скинут» (ключ `asda.Mount.PortalReapply`, разбор у ключа в
//     AsdaWorldSubsystem_Peers.cpp): сервер своему клиенту при портале транспорт не шлёт, оригинал вешает его сам из своего списка
//     состояний (`CCharacter_StopAllActiveBuffActions@0xA2C480`, портальный загрузчик шаг 12, game_main.c:3762);
//   • баг 2 «вход одержимым — не та модель и не то имя» (ключ `asda.Possess.BodyIdentity`, разбор у ключа в
//     AsdaNetSubsystem_Session.cpp): своя запись char-list с тем же номером слота — другой персонаж; тело — из 1017/1027/1024.
//   Офлайн (`-AsdaNoNet`), на карте мира. Реальный путь (закон diagnostic-must-use-real-path): посадка и снимки соседей — кадрами
//   5453/6154/1021 в общий разбор входящих (`DebugFeedPacket`); смена карты — штатная рассылка сброса `MapChanged`
//   (`UAsdaSessionResetSubsystem::Broadcast`, её шлёт `PostLoadMapWithWorld` нового уровня) + уничтожение павна, как травел;
//   4003 — свой номер + обе подписки `OnSelfSpawn` (мир — `DebugSpawnSelfAt` → `OnSelfSpawn`, список — `OnSelfSpawnCarry`) в обоих
//   порядках, потому что порядок подписчиков делегата не задан.
//   Проверяется:
//     M) для КАЖДОГО транспорта (оригинальные и маунты Fable 559–637): посадка 5453 → портал с новым номером (мир первым / список
//        первым) → свой павн верхом; ключ 0 — пеший (воспроизведение бага); портал с тем же номером (кэш «уже применён») — верхом;
//     R) перезаход: 1021 со слотом 15 → 4003 в обоих порядках → верхом (кэш прошлой сессии с тем же кодом не мешает);
//     N) соседи: устаревший кэш прошлой карты не сажает на транспорт игрока с тем же номером; 6154 новой карты — сажает;
//     P) одержимость: имя и надетое тела (1017 + 1024/контейнер 3), а не своего персонажа с тем же номером слота; то же после портала;
//        смена вещей не пишет в свою запись; ключ 0 — своё имя (воспроизведение бага); обычный вход — своя запись.
//   Итог: «ТЕЛО-ТРАНСПОРТ ИТОГ: ok=N fail=M».
#include "CoreMinimal.h"
#include "AsdaDevConsole.h"

#if ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING

#include "AsdaNetSubsystem.h"
#include "AsdaBytes.h"
#include "AsdaSelf.h"
#include "AsdaSessionReset.h"
#include "UI/AsdaEffectsSubsystem.h"
#include "UI/AsdaInventorySubsystem.h"
#include "World/AsdaWorldSubsystem.h"
#include "World/AsdaPlayerCharacter.h"
#include "Test/AsdaSelfTestKit.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaPossessMountTest, Log, All);

namespace AsdaPossessMountTest
{
	static const TCHAR* kCmd      = TEXT("asda.SelfTest.PossessMountPortal");   // имя команды
	static const TCHAR* kMountKey = TEXT("asda.Mount.PortalReapply");           // ключ правки транспорта
	static const TCHAR* kBodyKey  = TEXT("asda.Possess.BodyIdentity");          // ключ правки тела одержимости
	constexpr int32 kBodyLen1021 = 686;   // тело 1021 (LoginManager `sub_415A40(1021, 686, …)`)
	constexpr int32 kItemOff1021 = 368;   // 30 слотов функциональных предметов по 10 б
	constexpr int32 kPeer = 77;           // номер соседа прогона
	constexpr uint8 kRawSlot = 10;        // сырой Charidx: свой персонаж и тело соулмейта — оба 10 (как на живой базе)
	constexpr int32 kBodyHelmet = 41782;  // шлем тела (живой код шлема из 1027)

	// Состояние прогона.
	struct FRun
	{
		UWorld* W = nullptr;                          // мир прогона
		UAsdaNetSubsystem* Net = nullptr;             // сеть
		UAsdaWorldSubsystem* WS = nullptr;            // мир
		UAsdaEffectsSubsystem* Fx = nullptr;          // список состояний
		UAsdaSessionResetSubsystem* SR = nullptr;     // рассылка сброса сессии
		bool bQuit = false;                           // выйти по итогу
		int32 Ok = 0, Fail = 0;                       // счётчики
		TUniquePtr<FAsdaNetSelfScope> SelfScope;      // свой номер/charId/уровень
		TUniquePtr<FAsdaCVarScope> CVars;             // ключи
		// Что вернуть сети по итогу.
		EAsdaNetState StateWas = EAsdaNetState::Idle;
		int32 EnterMapIdWas = -1;
		TArray<uint8> SnapshotWas;
		TArray<FAsdaCharRecord> CharListWas;
		TArray<FAsdaSoulmateSharedChar> SharedWas;
		TArray<uint8> CharSelectWas;
		uint8 RawSlotWas = 0;
		uint8 SoulWas = 0;
		uint8 HairWas = 0xFF, HairColorWas = 0xFF, FaceWas = 0xFF;

		// Записать итог проверки.
		void Check(bool b, const FString& What)
		{
			if (b) { ++Ok;   UE_LOG(LogAsdaPossessMountTest, Log,   TEXT("ТЕЛО-ТРАНСПОРТ ok   %s"), *What); }
			else   { ++Fail; UE_LOG(LogAsdaPossessMountTest, Error, TEXT("ТЕЛО-ТРАНСПОРТ FAIL %s"), *What); }
		}
	};

	// Свой павн сейчас (или null).
	static AAsdaPlayerCharacter* SelfPawn(const FRun& R)
	{
		return R.WS ? Cast<AAsdaPlayerCharacter>(R.WS->LocalPlayerActor.Get()) : nullptr;
	}

	// Кадр 5453 (тело 10 б: i16 userIdx, i32 код, i32 срок) — посадка/предметный усилитель штатным разбором.
	static void Feed5453(FRun& R, int32 Uidx, int32 Code)
	{
		TArray<uint8> B; B.SetNumZeroed(10);
		AsdaBytes::WrI16(B.GetData(), (int16)Uidx); AsdaBytes::WrI32(B.GetData() + 2, Code); AsdaBytes::WrI32(B.GetData() + 6, -1);
		R.Net->DebugFeedPacket(5453, B);
	}

	// Кадр 6154 (u16 userIdx + запись 12 б: type 2, код, срок −1, величина 80) — снимок соседа при появлении.
	static void Feed6154(FRun& R, int32 Uidx, int32 Code)
	{
		TArray<uint8> B; B.SetNumZeroed(14);
		AsdaBytes::WrI16(B.GetData(), (int16)Uidx);
		B[2] = 2; AsdaBytes::WrI32(B.GetData() + 4, Code); AsdaBytes::WrI32(B.GetData() + 8, -1); AsdaBytes::WrI16(B.GetData() + 12, 80);
		R.Net->DebugFeedPacket(6154, B);
	}

	// Смена карты: павн умирает с миром (травел) и рассылается штатный сброс `MapChanged`.
	static void Portal(FRun& R)
	{
		if (AAsdaPlayerCharacter* Old = SelfPawn(R)) { Old->Destroy(); }
		R.SR->Broadcast(EAsdaSessionResetReason::MapChanged);
	}

	// 4003 нового входа: свой номер и обе подписки `OnSelfSpawn` в заданном порядке.
	static void Self4003(FRun& R, int32 Me, bool bWorldFirst)
	{
		R.Net->SetSelfUserNumForDebug((uint16)Me);
		if (bWorldFirst) { R.WS->DebugSpawnSelfAt(256, 256); R.Fx->OnSelfSpawnCarry(); }
		else             { R.Fx->OnSelfSpawnCarry(); R.WS->DebugSpawnSelfAt(256, 256); }
	}

	// Вернуть сеть и ключи, выйти.
	static void Finish(FRun& R)
	{
		if (R.Net)
		{
			R.Net->OnMountState.Broadcast((int32)R.Net->SelfUserNum, 0, false);
			R.Net->OnMountState.Broadcast(kPeer, 0, false);
			R.Net->State = R.StateWas;
			R.Net->EnterMapId = R.EnterMapIdWas;
			R.Net->EnterWorldSnapshot = R.SnapshotWas;
			R.Net->bPendingEnterWorld = false;
			R.Net->CharList = R.CharListWas;
			R.Net->SoulmateSharedChars = R.SharedWas;
			R.Net->CharSelectData = R.CharSelectWas;
			R.Net->LastSelectedRawSlot = R.RawSlotWas;
			R.Net->SetSelfSoulState(R.SoulWas);
			R.Net->SelfHair = R.HairWas; R.Net->SelfHairColor = R.HairColorWas; R.Net->SelfFace = R.FaceWas;
		}
		if (R.Fx)
		{
			for (int32 Idx : { 4321, 4322, 4323, 4324, 4325, 4326 }) { R.Fx->ClearCharEffects(Idx); }
		}
		R.SelfScope.Reset();
		R.CVars.Reset();
		AsdaSelfTestKit::Finish(R.W, kCmd, R.bQuit, LogAsdaPossessMountTest, TEXT("ТЕЛО-ТРАНСПОРТ"), R.Ok, R.Fail);
	}

	// ═══ M. Транспорт своего павна через портал ═══
	static void StepMount(FRun& R, int32 Code)
	{
		FString Model, AnimSet; float H = 0.f;
		const bool bIsMount = R.WS->LookupMount(Code, Model, AnimSet, H);
		R.Check(bIsMount, FString::Printf(TEXT("M0 [%d] предмет — транспорт (special_itemlist SubType 15), модель %s"), Code, *Model));
		if (!bIsMount) { return; }

		// Вход пешком, посадка 5453 (тот же кадр сервер шлёт себе и соседям).
		R.CVars->Set(kMountKey, 1);
		Portal(R);
		Self4003(R, 4321, /*bWorldFirst=*/true);
		Feed5453(R, 4321, Code);
		AAsdaPlayerCharacter* A = SelfPawn(R);
		R.Check(A && A->IsMounted(), FString::Printf(TEXT("M1 [%d] посадка 5453: свой павн верхом"), Code));
		const FAsdaEffectEntry* E = R.Fx->FindByTypeSub(4321, 15, 2);
		R.Check(E && E->ItemCode == Code, FString::Printf(TEXT("M2 [%d] запись списка type 2 / id 15 с кодом (%d)"), Code, E ? E->ItemCode : -1));

		// Портал, новый номер, мир первым.
		Portal(R);
		Self4003(R, 4322, /*bWorldFirst=*/true);
		AAsdaPlayerCharacter* B = SelfPawn(R);
		R.Check(B && B != A && B->IsMounted(), FString::Printf(TEXT("M3 [%d] портал (номер 4321→4322, спавн раньше списка): верхом"), Code));
		// Портал, новый номер, список первым.
		Portal(R);
		Self4003(R, 4323, /*bWorldFirst=*/false);
		AAsdaPlayerCharacter* C = SelfPawn(R);
		R.Check(C && C != B && C->IsMounted(), FString::Printf(TEXT("M4 [%d] портал (номер →4323, список раньше спавна): верхом"), Code));
		// Портал с ТЕМ ЖЕ номером (кэш «код тот же» — прежняя проверка отсекала новый павн).
		Portal(R);
		Self4003(R, 4323, /*bWorldFirst=*/true);
		AAsdaPlayerCharacter* D = SelfPawn(R);
		R.Check(D && D != C && D->IsMounted(), FString::Printf(TEXT("M5 [%d] портал с тем же номером 4323: верхом"), Code));
		// Ключ 0 — прежний порт: после портала пешком (воспроизведение бага владельца).
		R.CVars->Set(kMountKey, 0);
		Portal(R);
		Self4003(R, 4324, /*bWorldFirst=*/true);
		AAsdaPlayerCharacter* F = SelfPawn(R);
		R.Check(F && !F->IsMounted(), FString::Printf(TEXT("M6 [%d] ключ 0 (было): после портала павн пеший — баг воспроизводится"), Code));
		R.CVars->Set(kMountKey, 1);
		// Высадка 5454 — запись снята, следующий портал пешком.
		Portal(R);
		Self4003(R, 4325, /*bWorldFirst=*/true);
		TArray<uint8> B6; B6.SetNumZeroed(6);
		AsdaBytes::WrI16(B6.GetData(), (int16)4325); AsdaBytes::WrI32(B6.GetData() + 2, Code);
		R.Net->DebugFeedPacket(5454, B6);
		Portal(R);
		Self4003(R, 4326, /*bWorldFirst=*/false);
		AAsdaPlayerCharacter* G = SelfPawn(R);
		R.Check(G && !G->IsMounted(), FString::Printf(TEXT("M7 [%d] слез (5454) до портала — после портала пеший"), Code));
		R.Fx->ClearCharEffects(4326);
	}

	// ═══ R. Перезаход: 1021 со слотом 15 → 4003 в обоих порядках ═══
	static void StepRelog(FRun& R, int32 Code, bool bWorldFirst)
	{
		R.CVars->Set(kMountKey, 1);
		R.CVars->Set(TEXT("asda.Buff.EnterWorldRestore"), 1);
		// Прошлая сессия: верхом на том же коде с тем же номером (кэш мира «4321 → код»).
		Portal(R);
		Self4003(R, 4321, true);
		Feed5453(R, 4321, Code);
		if (AAsdaPlayerCharacter* Old = SelfPawn(R)) { Old->Destroy(); }
		TArray<uint8> Body; Body.SetNumZeroed(kBodyLen1021);
		for (int32 i = 0; i < 28; ++i) { AsdaBytes::WrI16(Body.GetData() + 32 + 12 * i, -1); }
		for (int32 k = 0; k < 30; ++k)
		{
			uint8* P = Body.GetData() + kItemOff1021 + 10 * k;
			AsdaBytes::WrI32(P, k == 15 ? Code : -1); AsdaBytes::WrI32(P + 4, k == 15 ? -1 : 0); AsdaBytes::WrI16(P + 8, k == 15 ? 80 : -1);
		}
		R.Net->DebugFeedPacket(1021, Body);   // штатный разбор: снимок + полный сброс «вход в мир»
		R.Net->bPendingEnterWorld = false;
		FAsdaSelfTestArgs A; A.bQuit = false;
		if (AsdaSelfTestKit::RefuseStart(R.W, kCmd, A)) { R.Check(false, TEXT("R отказ: появилось соединение")); return; }
		Self4003(R, 4321, bWorldFirst);
		AAsdaPlayerCharacter* P = SelfPawn(R);
		R.Check(P && P->IsMounted(), FString::Printf(TEXT("R1 [%d] перезаход верхом (1021 слот 15, %s): павн верхом"), Code,
			bWorldFirst ? TEXT("спавн раньше списка") : TEXT("список раньше спавна")));
		R.Fx->ClearCharEffects(4321);
		R.Net->OnMountState.Broadcast(4321, 0, false);
	}

	// ═══ N. Соседи на новой карте ═══
	static void StepPeers(FRun& R, int32 Code)
	{
		R.CVars->Set(kMountKey, 1);
		Portal(R);
		Self4003(R, 4321, true);
		Feed5453(R, kPeer, Code);   // сосед прошлой карты сел (актора у нас нет — только кэш)
		Portal(R);                  // смена карты
		Self4003(R, 4322, true);
		R.WS->DebugSpawnFakePeer(kPeer, TEXT("ПешийСосед"), FVector2D(25650.f, 25650.f));
		AAsdaPlayerCharacter* P1 = Cast<AAsdaPlayerCharacter>(R.WS->ActorByUserIdx(kPeer));
		R.Check(P1 && !P1->IsMounted(), TEXT("N1 игрок новой карты с номером соседа прошлой карты (появился без транспорта) — пеший"));
		Feed6154(R, kPeer, Code);
		R.Check(P1 && P1->IsMounted(), FString::Printf(TEXT("N2 [%d] 6154 новой карты: сосед верхом"), Code));
		R.Net->OnMountState.Broadcast(kPeer, 0, false);
		R.CVars->Set(kMountKey, 0);
		Feed5453(R, kPeer + 1, Code);
		Portal(R);
		Self4003(R, 4323, true);
		R.WS->DebugSpawnFakePeer(kPeer + 1, TEXT("ПешийСосед2"), FVector2D(25850.f, 25650.f));
		AAsdaPlayerCharacter* P2 = Cast<AAsdaPlayerCharacter>(R.WS->ActorByUserIdx(kPeer + 1));
		R.Check(P2 && P2->IsMounted(), TEXT("N3 ключ 0 (было): кэш прошлой карты сажал пешего игрока на чужой транспорт — воспроизводится"));
		R.Net->OnMountState.Broadcast(kPeer + 1, 0, false);
		R.CVars->Set(kMountKey, 1);
	}

	// Запись своего персонажа и тела соулмейта с одинаковым номером слота (как на живой базе).
	static void SetupPossess(FRun& R)
	{
		FAsdaCharRecord Own;
		Own.Slot = 0; Own.RawSlotId = kRawSlot; Own.Name = TEXT("OwnChar"); Own.Sex = 1; Own.Level = 10;
		Own.HpCur = 111; Own.HpMax = 222; Own.Equip.SetNum(11);
		R.Net->CharList = { Own };
		FAsdaSoulmateSharedChar Body;
		Body.Charidx = kRawSlot; Body.CharName = TEXT("BodyFriend"); Body.Gender = 1; Body.Level = 20;
		Body.Hp = 333; Body.MaxHp = 444; Body.OwnerUserIndex = 2;
		Body.Equip.SetNum(11); Body.Equip[0].ItemId = kBodyHelmet;
		R.Net->SoulmateSharedChars = { Body };
		TArray<uint8> Blk; Blk.SetNumZeroed(68);
		AsdaBytes::WrI32(Blk.GetData(), 750001);
		const char* N = "BodyFriend"; FMemory::Memcpy(Blk.GetData() + 4, N, FCStringAnsi::Strlen(N));
		Blk[27] = 1;
		R.Net->CharSelectData = Blk;
		R.Net->LastSelectedRawSlot = kRawSlot;
	}

	// ═══ P. Одержимость: имя и вид тела ═══
	static void StepPossess(FRun& R)
	{
		SetupPossess(R);
		R.Net->SetSelfSoulState(2);
		R.CVars->Set(kBodyKey, 1);
		// Ожидаемый шлем: контейнер 3, если надетое уже применено, иначе запись тела 1024.
		const UAsdaInventorySubsystem* Inv = R.W->GetGameInstance()->GetSubsystem<UAsdaInventorySubsystem>();
		bool bWorn = Inv && Inv->IsContainerReceived(3);
		for (int32 S = 0; Inv && !bWorn && S < 11; ++S) { const FAsdaItemInstance* C = Inv->GetCell(3, S); bWorn = C && !C->IsEmpty(); }
		const FAsdaItemInstance* C0 = Inv ? Inv->GetCell(3, 0) : nullptr;
		const int32 WantHelmet = bWorn ? ((C0 && !C0->IsEmpty()) ? C0->ItemCode : 0) : kBodyHelmet;

		Portal(R);
		Self4003(R, 4321, true);
		R.Check(AsdaSelf::FindSelectedCharRecord(R.Net) == nullptr, TEXT("P1 одержимость: своя запись с тем же номером слота не выдаётся"));
		R.Check(R.WS->GetSelfCharName() == TEXT("BodyFriend"), FString::Printf(TEXT("P2 вход: имя своего павна = имя тела из 1017 («%s»)"), *R.WS->GetSelfCharName()));
		R.Check(R.WS->SelfParts.Helmet == WantHelmet, FString::Printf(TEXT("P3 вход: шлем павна %d = тела %d (%s)"), R.WS->SelfParts.Helmet, WantHelmet,
			bWorn ? TEXT("контейнер 3") : TEXT("запись 1024")));
		R.Check(R.Net->GetSelfCharNameForOperatorCheck() == TEXT("BodyFriend"), TEXT("P4 имя для критерия оператора — тело"));
		// Портал: признак одержимости переживает смену карты, павн снова телом.
		Portal(R);
		R.Check(R.Net->SelfSoulState == 2, TEXT("P5 смена карты признак одержимости не снимает"));
		Self4003(R, 4322, false);
		R.Check(R.WS->GetSelfCharName() == TEXT("BodyFriend") && R.WS->SelfParts.Helmet == WantHelmet,
			FString::Printf(TEXT("P6 портал: имя «%s», шлем %d — тело"), *R.WS->GetSelfCharName(), R.WS->SelfParts.Helmet));
		// Смена вещи при одержимости не пишет в запись СВОЕГО персонажа.
		R.WS->ApplySelfEquipPart(0, kBodyHelmet + 1, 0);
		R.Check(R.Net->CharList[0].Equip[0].ItemId == 0, FString::Printf(TEXT("P7 смена шлема телом: своя запись не тронута (шлем %d)"), R.Net->CharList[0].Equip[0].ItemId));
		// Ключ 0 — прежний порт: своё имя на теле (воспроизведение бага игроков).
		R.CVars->Set(kBodyKey, 0);
		Portal(R);
		Self4003(R, 4323, true);
		R.Check(R.WS->GetSelfCharName() == TEXT("OwnChar"), FString::Printf(TEXT("P8 ключ 0 (было): имя своего персонажа «%s» на теле — баг воспроизводится"), *R.WS->GetSelfCharName()));
		R.CVars->Set(kBodyKey, 1);
		// Обычный вход: своя запись по-прежнему используется.
		R.Net->SetSelfSoulState(0);
		Portal(R);
		Self4003(R, 4324, true);
		R.Check(R.WS->GetSelfCharName() == TEXT("OwnChar") && AsdaSelf::FindSelectedCharRecord(R.Net) == &R.Net->CharList[0],
			TEXT("P9 обычный вход: имя и запись своего персонажа"));
	}

	// Запуск прогона.
	static void Run(UWorld* W, const FAsdaSelfTestArgs& Args, const TArray<int32>& Codes)
	{
		FRun R;
		R.W = W;
		R.bQuit = Args.bQuit;
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		R.Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
		R.WS  = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
		R.Fx  = GI ? GI->GetSubsystem<UAsdaEffectsSubsystem>() : nullptr;
		R.SR  = GI ? GI->GetSubsystem<UAsdaSessionResetSubsystem>() : nullptr;
		if (!R.Net || !R.WS || !R.Fx || !R.SR)
		{
			AsdaSelfTestKit::Refused(W, kCmd, Args.bQuit, TEXT("нет подсистем"));
			return;
		}
		R.SelfScope = MakeUnique<FAsdaNetSelfScope>(R.Net, kCmd);
		R.CVars = MakeUnique<FAsdaCVarScope>(W, kCmd);
		R.StateWas = R.Net->State; R.EnterMapIdWas = R.Net->EnterMapId; R.SnapshotWas = R.Net->EnterWorldSnapshot;
		R.CharListWas = R.Net->CharList; R.SharedWas = R.Net->SoulmateSharedChars; R.CharSelectWas = R.Net->CharSelectData;
		R.RawSlotWas = R.Net->LastSelectedRawSlot; R.SoulWas = R.Net->SelfSoulState;
		R.HairWas = R.Net->SelfHair; R.HairColorWas = R.Net->SelfHairColor; R.FaceWas = R.Net->SelfFace;
		if (GEngine) { GEngine->Exec(W, TEXT("Asda.Load.CurrentMap 3")); }

		for (int32 Code : Codes) { StepMount(R, Code); }
		if (Codes.Num() > 0)
		{
			StepRelog(R, Codes[0], true);
			StepRelog(R, Codes.Last(), false);
			StepPeers(R, Codes[0]);
		}
		StepPossess(R);
		Finish(R);
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestPossessMountPortal(
	TEXT("asda.SelfTest.PossessMountPortal"),
	TEXT("asda.SelfTest.PossessMountPortal [quit] [код транспорта …] — тело при одержимости и транспорт после входа/портала (офлайн)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& RawArgs, UWorld* W)
	{
		const FAsdaSelfTestArgs Args = FAsdaSelfTestArgs::Parse(RawArgs);
		if (AsdaSelfTestKit::RefuseStart(W, AsdaPossessMountTest::kCmd, Args)) { return; }
		// Транспорт: оригинальные (533 — живой лог, 126 — облако) и маунты Fable (559, 600, 637 — первый, средний, последний).
		TArray<int32> Codes;
		for (const FString& S : Args.Pos) { if (S.IsNumeric()) { Codes.Add(FCString::Atoi(*S)); } }
		if (Codes.Num() == 0) { Codes = { 533, 126, 559, 600, 637 }; }
		AsdaPossessMountTest::Run(W, Args, Codes);
	}));

#endif // ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING
