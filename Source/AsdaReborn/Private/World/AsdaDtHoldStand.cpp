// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ СТЕНД: УДЕРЖАНИЕ ТАБЛИЦ ПРИ СБОРКЕ МУСОРА (15.09.2026, отчёт `Reports/dedup_audit/dt_gc_hold.md`).
//
// Команда `asda.DtHold.GcCheck` (вне Shipping, нужен ЖИВОЙ GameInstance — прогон -game). Вопрос: переживают ли
//   полный GC (`obj gc` = `CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS)`) таблицы, которые раньше держались
//   только слабыми кэшами, когда их удерживают владельцы (`FAsdaHeldTables`).
// Шаги (каждый — строки `DTHOLD_GC …` в лог):
//   1) подсистемы живого GameInstance; ожидаемые группы (свой список путей стенда — ниже сверяется с тем,
//      что владелец держит на самом деле);
//   2) КОНТРОЛЬ: таблицы групп, которые ещё НЕ удерживались, берутся одной слабой `AsdaDt::Cached`, оценка памяти
//      строк (обход свойств) и физическая память процесса, `obj gc` → сколько выгружено. 0 выгруженных = проверка слепа;
//      уцелевшая таблица = у неё есть другой сильный владелец (печатается);
//   3) удержание РЕАЛЬНЫМИ методами владельцев (теми же, что зовёт игра) → сверка «заявлено ⊆ удержано» →
//      слабые указатели → `obj gc` → те же объекты живы; повторное удержание ничего не грузит и даёт то же число.
//      + сверка цепочки имени знака: `FAsdaText::FindTable("Constellation")` возвращает ТОТ ЖЕ объект, что держит
//      подсистема знаков (хвост ревизии 15.09 п.2).
// ⚠ Шаг 2 выгружает таблицы, которые держались только слабо: сырые указатели на их строки у стендов
//   (`asda.EntityText.Stand`) после него могут висеть — запускать ПОСЛЕ них.
#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "AsdaDataTableCache.h"                 // AsdaDt::Cached — слабая загрузка контроля (та же точка, что у игры)
#include "AsdaHeldTables.h"
#include "AsdaSelfTestBatch.h"                  // ★ ревизия В2 дедупа №2 (11.10.2026): Quit — выход по итогу (перехватывается пакетом)
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMemory.h"               // физическая память процесса до/после загрузки контроля
#include "UObject/UnrealType.h"              // обход свойств строки (FProperty) — оценка памяти строк
#include "World/AsdaWorldSubsystem.h"
#include "Skill/AsdaSkillFxSubsystem.h"
#include "Quest/AsdaQuestMarkerSubsystem.h"
#include "Quest/AsdaQuestLogSubsystem.h"
#include "Npc/AsdaNpcDialogSubsystem.h"
#include "UI/AsdaNpcShopSubsystem.h"
#include "Text/AsdaTextSubsystem.h"
#include "UI/AsdaConstellationSubsystem.h"    // группа DT_Constellation (хвост ревизии 15.09 п.2)
#include "Login/AsdaText.h"                   // FAsdaText::FindTable — сверка: слабая цепочка имени знака видит удержанный объект
#include "UI/AsdaEffectsSubsystem.h"        // ★ дедуп №2 В2-C (F3): группа DT_FxLoop (эффекты) — бывший AddToRoot эффект-актора

DEFINE_LOG_CATEGORY_STATIC(LogAsdaDtHoldStand, Log, All);

namespace AsdaDtHoldStand
{
	// Одна группа удержания так, как её видит стенд.
	struct FGroup
	{
		FString Label;                                   // «владелец/группа» для лога
		TArray<FString> Paths;                           // ожидаемые объектные пути (список стенда)
		TFunction<bool()> IsHeld;                        // удерживалась ли группа до команды
		TFunction<int32()> Hold;                         // удержание РЕАЛЬНЫМ методом владельца; возврат владельца
		TFunction<const FAsdaHeldTables*()> Owner;       // набор владельца — для сверки «заявлено ⊆ удержано»
	};

	// Таблица в памяти? Только поиск, без загрузки.
	static UDataTable* InMemory(const FString& Path) { return FindObject<UDataTable>(nullptr, *Path); }

	// ★ Память строк таблицы, байт. `FArchiveCountMem` (счётчик `obj list`) строки DataTable НЕ видит — первый прогон дал
	//   0.00 МБ на всех таблицах: строки лежат в `RowMap`, а не в свойствах объекта. Поэтому считаем обходом свойств
	//   структуры строки: размер структуры + кучи `FString`/`FText`/`TArray` (рекурсивно по вложенным структурам)
	//   + запись карты строк (`FName` + указатель). Оценка СНИЗУ: без запаса аллокатора и служебных полей `TMap`.
	static int64 StructHeap(const UStruct* S, const void* Data);
	static int64 ValueHeap(const FProperty* Prop, const void* Value)   // Value — адрес самого значения
	{
		if (CastField<FStrProperty>(Prop))  { return ((const FString*)Value)->GetAllocatedSize(); }
		if (CastField<FTextProperty>(Prop)) { return ((const FText*)Value)->ToString().GetAllocatedSize(); }
		if (const FArrayProperty* AP = CastField<FArrayProperty>(Prop))
		{
			FScriptArrayHelper H(AP, Value);
			int64 N = (int64)H.Num() * AP->Inner->GetElementSize();
			for (int32 i = 0; i < H.Num(); ++i) { N += ValueHeap(AP->Inner, H.GetRawPtr(i)); }
			return N;
		}
		if (const FStructProperty* SP = CastField<FStructProperty>(Prop)) { return StructHeap(SP->Struct, Value); }
		return 0;
	}
	static int64 StructHeap(const UStruct* S, const void* Data)
	{
		int64 N = 0;
		for (TFieldIterator<FProperty> It(S); It; ++It)
		{
			for (int32 i = 0; i < It->ArrayDim; ++i) { N += ValueHeap(*It, It->ContainerPtrToValuePtr<void>(const_cast<void*>(Data), i)); }
		}
		return N;
	}
	static int64 MemBytes(UDataTable* T)
	{
		const UScriptStruct* RS = T ? T->GetRowStruct() : nullptr;
		if (!RS) { return 0; }
		int64 N = 0;
		for (const TPair<FName, uint8*>& It : T->GetRowMap())
		{
			N += RS->GetStructureSize() + StructHeap(RS, It.Value) + sizeof(FName) + sizeof(uint8*);
		}
		return N;
	}

	static void RunGcCheckImpl(const TArray<FString>& Args);

	// ★ Ревизия В2 дедупа №2 (11.10.2026): аргумент `quit` — выход после итога (пакет `asda.SelfTest.Batch` по нему узнаёт конец
	//   стенда; прежде `quit` не разбирался и пакет стоял до потолка при напечатанном ИТОГ). Выход — на любом пути итога.
	static void RunGcCheck(const TArray<FString>& Args)
	{
		bool bQuit = false;   // выйти после итога
		for (const FString& Arg : Args) { bQuit |= Arg.Equals(TEXT("quit"), ESearchCase::IgnoreCase); }
		RunGcCheckImpl(Args);
		if (!bQuit) { return; }
		UWorld* QuitWorld = nullptr;   // игровой мир — контекст команды `quit`
		for (const FWorldContext& Ctx : GEngine->GetWorldContexts()) { if (Ctx.World() && Ctx.World()->IsGameWorld()) { QuitWorld = Ctx.World(); break; } }
		AsdaSelfTestBatch::Quit(QuitWorld);
	}

	static void RunGcCheckImpl(const TArray<FString>& Args)   // мир находим сами — нужен ИГРОВОЙ
	{
		UGameInstance* GI = nullptr;
		UWorld* GameWorld = nullptr;
		for (const FWorldContext& Ctx : GEngine->GetWorldContexts())
		{
			if (Ctx.OwningGameInstance && Ctx.World() && Ctx.World()->IsGameWorld()) { GI = Ctx.OwningGameInstance; GameWorld = Ctx.World(); break; }
		}
		UAsdaWorldSubsystem*       WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
		UAsdaSkillFxSubsystem*     SF = GI ? GI->GetSubsystem<UAsdaSkillFxSubsystem>() : nullptr;
		UAsdaQuestMarkerSubsystem* QM = GI ? GI->GetSubsystem<UAsdaQuestMarkerSubsystem>() : nullptr;
		UAsdaQuestLogSubsystem*    QL = GI ? GI->GetSubsystem<UAsdaQuestLogSubsystem>() : nullptr;
		UAsdaNpcDialogSubsystem*   ND = GI ? GI->GetSubsystem<UAsdaNpcDialogSubsystem>() : nullptr;
		UAsdaNpcShopSubsystem*     NS = GI ? GI->GetSubsystem<UAsdaNpcShopSubsystem>() : nullptr;
		UAsdaTextSubsystem*        TS = GI ? GI->GetSubsystem<UAsdaTextSubsystem>() : nullptr;
		UAsdaConstellationSubsystem* CS = GI ? GI->GetSubsystem<UAsdaConstellationSubsystem>() : nullptr;
		UAsdaEffectsSubsystem*     ES = GI ? GI->GetSubsystem<UAsdaEffectsSubsystem>() : nullptr;   // ★ В2-C F3: владелец DT_FxLoop
		if (!WS || !SF || !QM || !QL || !ND || !NS || !TS || !CS || !ES)
		{
			UE_LOG(LogAsdaDtHoldStand, Warning, TEXT("DTHOLD_GC ИТОГ: ВЕРДИКТ ПРОВАЛ — нет живого GameInstance или подсистем (WS=%d SF=%d QM=%d QL=%d ND=%d NS=%d TS=%d CS=%d)"),
				WS ? 1 : 0, SF ? 1 : 0, QM ? 1 : 0, QL ? 1 : 0, ND ? 1 : 0, NS ? 1 : 0, TS ? 1 : 0, CS ? 1 : 0);
			return;
		}
		UE_LOG(LogAsdaDtHoldStand, Log, TEXT("DTHOLD_GC мир: %s (игровой=%d)"), *GameWorld->GetMapName(), GameWorld->IsGameWorld() ? 1 : 0);

		// ── 1) ожидаемые группы ──
		const FString D = TEXT("/Game/AsdaAssets/Data/");
		auto P = [](const FString& Dir, const TCHAR* Name) { return FString::Printf(TEXT("%s%s.%s"), *Dir, Name, Name); };
		TArray<FGroup> Groups;
		auto AddWorld = [&](const TCHAR* Label, EAsdaSpawnTables G, TArray<FString> Paths)
		{
			Groups.Add({ FString::Printf(TEXT("мир/%s"), Label), MoveTemp(Paths),
				[WS, G]() { return WS->IsSpawnTablesHeld(G); }, [WS, G]() { return WS->HoldSpawnTables(G); },
				[WS]() { return &WS->GetHeldSpawnTables(); } });
		};
		AddWorld(TEXT("Mob"), EAsdaSpawnTables::Mob, { P(D + TEXT("Mob/"), TEXT("DT_MobStats")), P(D + TEXT("Mob/"), TEXT("DT_MobTextures")), P(D + TEXT("Mob/"), TEXT("DT_MobNameColors")) });
		AddWorld(TEXT("Npc"), EAsdaSpawnTables::Npc, { P(D + TEXT("NPC/"), TEXT("DT_NpcQuestNpc")) });
		AddWorld(TEXT("Clan"), EAsdaSpawnTables::Clan, { P(TEXT("/Game/AsdaAssets/Text/Clan/"), TEXT("DT_Clan_Sign")), P(TEXT("/Game/AsdaAssets/Text/Clan/"), TEXT("DT_Clan_BorderImage")) });
		AddWorld(TEXT("Pet"), EAsdaSpawnTables::Pet, { P(D, TEXT("DT_PetModels")) });
		AddWorld(TEXT("Mount"), EAsdaSpawnTables::Mount, { P(D, TEXT("DT_RideEffect")) });
		AddWorld(TEXT("SoulGuardian"), EAsdaSpawnTables::SoulGuardian, { P(D, TEXT("DT_SkillFx")) });
		Groups.Add({ TEXT("навыки/SkillClips"), { P(D, TEXT("DT_SkillClips")) },
			[SF]() { return SF->GetHeldTables().IsHeld(TEXT("SkillClips")); }, [SF]() { return SF->HoldGameTables(); },
			[SF]() { return &SF->GetHeldTables(); } });
		Groups.Add({ TEXT("метки квестов/QuestMarker+QuestTracker"),
			{ P(D + TEXT("NPC/"), TEXT("DT_EpisodeTable")), P(D + TEXT("Quest/"), TEXT("DT_QuestComplete")), P(D + TEXT("NPC/"), TEXT("DT_NpcNsrHeader")),
			  P(D + TEXT("NPC/"), TEXT("DT_NpcSnq")), P(D + TEXT("NPC/"), TEXT("DT_SnqIct")), P(D + TEXT("Quest/"), TEXT("DT_QuestGuide")) },
			[QM]() { return QM->GetHeldTables().IsHeld(TEXT("QuestMarker")) || QM->GetHeldTables().IsHeld(TEXT("QuestTracker")); },
			[QM]() { return QM->HoldGameTables(); }, [QM]() { return &QM->GetHeldTables(); } });
		Groups.Add({ TEXT("журнал/QuestLog"),
			{ P(D + TEXT("NPC/"), TEXT("DT_EpisodeTable")), P(D + TEXT("Quest/"), TEXT("DT_QuestBook")), P(D + TEXT("Quest/"), TEXT("DT_QuestGuide")),
			  P(D + TEXT("NPC/"), TEXT("DT_QuestRewardTable")), P(D + TEXT("Quest/"), TEXT("DT_SmInfo")), P(D + TEXT("Quest/"), TEXT("DT_RaidQuestData")) },
			[QL]() { return QL->GetHeldTables().IsHeld(TEXT("QuestLog")); }, [QL]() { return QL->HoldGameTables(); },
			[QL]() { return &QL->GetHeldTables(); } });
		Groups.Add({ TEXT("диалог NPC/NpcDialog"),
			{ P(D + TEXT("NPC/"), TEXT("DT_NpcQuestNpc")), P(D + TEXT("NPC/"), TEXT("DT_NpcQuestScript")), P(D + TEXT("NPC/"), TEXT("DT_NpcNsrHeader")),
			  P(D + TEXT("NPC/"), TEXT("DT_NpcNsrButton")), P(D + TEXT("NPC/"), TEXT("DT_NpcNsrPage")), P(TEXT("/Game/AsdaAssets/Text/Dialogue/"), TEXT("DT_Talk")),
			  P(D + TEXT("NPC/"), TEXT("DT_WarpList")), P(D + TEXT("NPC/"), TEXT("DT_NpcSnq")), P(D + TEXT("NPC/"), TEXT("DT_SnqIct")),
			  P(D, TEXT("DT_CallMob")), P(D + TEXT("NPC/"), TEXT("DT_EventItem")),
			  // ★ Ревизия В2 дедупа №2 (11.10.2026): список стенда отстал от `HoldGameTables` — подсистема держит и серверную
			  //   таблицу обменов (`kDtEventItemServer`), стенд ждал 11 и печатал «владелец вернул 12 из 11 — ПРОВАЛ».
			  P(D + TEXT("NPC/"), TEXT("DT_EventItemServer")) },
			[ND]() { return ND->GetHeldTables().IsHeld(TEXT("NpcDialog")); }, [ND]() { return ND->HoldGameTables(); },
			[ND]() { return &ND->GetHeldTables(); } });
		Groups.Add({ TEXT("магазин NPC/NpcShop"), { P(D + TEXT("NPC/"), TEXT("DT_NpcShopList")) },
			[NS]() { return NS->GetHeldTables().IsHeld(TEXT("NpcShop")); }, [NS]() { return NS->HoldGameTables(); },
			[NS]() { return &NS->GetHeldTables(); } });
		Groups.Add({ TEXT("текст/GameText"), { P(TEXT("/Game/AsdaAssets/Text/"), TEXT("DT_GameText")) },
			[TS]() { return TS->GetHeldTables().IsHeld(TEXT("GameText")); }, [TS]() { return TS->HoldGameTextTable(); },
			[TS]() { return &TS->GetHeldTables(); } });
		Groups.Add({ TEXT("текст/Map_info (через GetText)"), { P(TEXT("/Game/AsdaAssets/Text/World/"), TEXT("DT_Map_info")) },
			[TS]() { return TS->GetHeldTables().IsHeld(TEXT("Map_info")); },
			[TS]() { TS->GetText(TEXT("Map_info"), 1, TEXT("name")); return TS->GetHeldTables().GroupNum(TEXT("Map_info")); },
			[TS]() { return &TS->GetHeldTables(); } });
		// ★ Дедуп №2, В2-C (F3): таблица ЦИКЛА 1:1 — прежде держалась КОРНЕМ (`AddToRoot` эффект-актора), теперь — подсистемой эффектов.
		Groups.Add({ TEXT("эффекты/FxLoop"), { P(D, TEXT("DT_FxLoop")) },
			[ES]() { return ES->GetHeldTables().IsHeld(TEXT("FxLoop")); }, [ES]() { return ES->HoldFxLoopTable(); },
			[ES]() { return &ES->GetHeldTables(); } });
		const FString ConsPath = P(TEXT("/Game/AsdaAssets/Text/World/"), TEXT("DT_Constellation"));
		Groups.Add({ TEXT("знаки/Constellation"), { ConsPath },
			[CS]() { return CS->GetHeldTables().IsHeld(TEXT("Constellation")); }, [CS]() { return CS->HoldGameTables(); },
			[CS]() { return &CS->GetHeldTables(); } });

		// Уникальные пути (одна таблица бывает в нескольких группах разных владельцев).
		TArray<FString> AllPaths;
		for (const FGroup& G : Groups) { for (const FString& Pa : G.Paths) { AllPaths.AddUnique(Pa); } }
		int32 NStart = 0;
		for (const FString& Pa : AllPaths) { NStart += InMemory(Pa) ? 1 : 0; }
		UE_LOG(LogAsdaDtHoldStand, Log, TEXT("DTHOLD_GC групп %d, уникальных таблиц %d, в памяти на старте %d"), Groups.Num(), AllPaths.Num(), NStart);

		// ── 2) КОНТРОЛЬ: только слабый кэш, группы ещё не удерживались ──
		TSet<FString> HeldBefore;   // пути групп, удерживавшихся до команды (контроль по ним невозможен)
		for (const FGroup& G : Groups)
		{
			const bool bHeld = G.IsHeld();
			UE_LOG(LogAsdaDtHoldStand, Log, TEXT("DTHOLD_GC группа «%s»: путей %d, удерживалась до команды=%d"), *G.Label, G.Paths.Num(), bHeld ? 1 : 0);
			if (bHeld) { for (const FString& Pa : G.Paths) { HeldBefore.Add(Pa); } }
		}
		TArray<FString> CtrlPaths;
		for (const FString& Pa : AllPaths) { if (!HeldBefore.Contains(Pa)) { CtrlPaths.Add(Pa); } }
		const uint64 PhysBefore = FPlatformMemory::GetStats().UsedPhysical;   // физическая память процесса до загрузки контроля
		int32 CtrlLoaded = 0, CtrlUnloaded = 0, CtrlSurvived = 0;
		int64 CtrlBytes = 0;
		for (const FString& Pa : CtrlPaths)
		{
			UDataTable* T = AsdaDt::Cached(*Pa);
			const int64 B = MemBytes(T);
			CtrlBytes += B;
			CtrlLoaded += T ? 1 : 0;
			UE_LOG(LogAsdaDtHoldStand, Log, TEXT("DTHOLD_GC контроль до GC: %s загружена=%d строк=%d память=%.2f МБ"),
				*Pa, T ? 1 : 0, T ? T->GetRowMap().Num() : 0, B / (1024.0 * 1024.0));
		}
		const uint64 PhysLoaded = FPlatformMemory::GetStats().UsedPhysical;
		if (CtrlPaths.Num() > 0)
		{
			GEngine->Exec(GameWorld, TEXT("obj gc"));
			for (const FString& Pa : CtrlPaths)
			{
				const bool bIn = InMemory(Pa) != nullptr;
				(bIn ? CtrlSurvived : CtrlUnloaded) += 1;
				UE_LOG(LogAsdaDtHoldStand, Log, TEXT("DTHOLD_GC контроль после obj gc: %s в памяти=%d%s"),
					*Pa, bIn ? 1 : 0, bIn ? TEXT(" (есть другой сильный владелец)") : TEXT(" (выгружена — риск реален)"));
			}
		}
		const uint64 PhysAfterGc = FPlatformMemory::GetStats().UsedPhysical;
		UE_LOG(LogAsdaDtHoldStand, Log, TEXT("DTHOLD_GC КОНТРОЛЬ память процесса: до загрузки %.1f МБ; после загрузки %+.1f МБ; после obj gc %+.1f МБ (от «до загрузки»)"),
			PhysBefore / 1048576.0, ((double)PhysLoaded - (double)PhysBefore) / 1048576.0, ((double)PhysAfterGc - (double)PhysBefore) / 1048576.0);
		UE_LOG(LogAsdaDtHoldStand, Log, TEXT("DTHOLD_GC КОНТРОЛЬ: таблиц %d (загружено %d, %.2f МБ), после obj gc выгружено %d, уцелело %d; пропущено (удерживались до команды) %d"),
			CtrlPaths.Num(), CtrlLoaded, CtrlBytes / (1024.0 * 1024.0), CtrlUnloaded, CtrlSurvived, AllPaths.Num() - CtrlPaths.Num());

		// ── 3) удержание реальными методами → сверка → obj gc → те же объекты ──
		int32 GroupsOk = 0;
		TArray<int32> Held1;
		for (const FGroup& G : Groups)
		{
			const int32 N = G.Hold();
			Held1.Add(N);
			// Сверка: всё заявленное стендом реально лежит в наборе владельца.
			const FAsdaHeldTables* O = G.Owner();
			TSet<FString> OwnerPaths;
			if (O) { for (const TObjectPtr<UDataTable>& T : O->GetTables()) { if (T) { OwnerPaths.Add(T->GetPathName()); } } }
			int32 Missing = 0;
			for (const FString& Pa : G.Paths)
			{
				if (!OwnerPaths.Contains(Pa)) { ++Missing; UE_LOG(LogAsdaDtHoldStand, Warning, TEXT("DTHOLD_GC сверка «%s»: %s НЕ в наборе владельца"), *G.Label, *Pa); }
			}
			const bool bOk = (N == G.Paths.Num()) && Missing == 0;
			GroupsOk += bOk ? 1 : 0;
			UE_LOG(LogAsdaDtHoldStand, Log, TEXT("DTHOLD_GC удержание «%s»: владелец вернул %d из %d, не в наборе %d, всего в наборе владельца %d — %s"),
				*G.Label, N, G.Paths.Num(), Missing, OwnerPaths.Num(), bOk ? TEXT("ok") : TEXT("ПРОВАЛ"));
		}
		TArray<TWeakObjectPtr<UDataTable>> Before;
		int64 HeldBytes = 0;
		for (const FString& Pa : AllPaths)
		{
			UDataTable* T = InMemory(Pa);
			Before.Add(T);
			HeldBytes += MemBytes(T);
		}
		GEngine->Exec(GameWorld, TEXT("obj gc"));
		int32 NAfter = 0, NSame = 0;
		for (int32 i = 0; i < AllPaths.Num(); ++i)
		{
			const UDataTable* Now = InMemory(AllPaths[i]);
			const bool bSame = Before[i].IsValid() && Before[i].Get() == Now;   // тот же объект, не перезагруженный
			NAfter += Now ? 1 : 0;
			NSame += bSame ? 1 : 0;
			if (!bSame) { UE_LOG(LogAsdaDtHoldStand, Warning, TEXT("DTHOLD_GC после obj gc: %s в памяти=%d тот же=0"), *AllPaths[i], Now ? 1 : 0); }
		}
		// Повторное удержание: ничего не грузит и возвращает то же число.
		int32 RepeatSame = 0;
		for (int32 i = 0; i < Groups.Num(); ++i) { RepeatSame += (Groups[i].Hold() == Held1[i]) ? 1 : 0; }

		// Цепочка имени знака (`FAsdaCharRecord::ConstellationName` → `FAsdaText::Get` → `FindTable`) — после GC видит ТОТ ЖЕ
		//   объект, что держит подсистема знаков (удержание по `kSignTablePath` покрывает слабый кэш `FAsdaText`).
		const UDataTable* ChainDt = FAsdaText::FindTable(TEXT("Constellation"));
		const UDataTable* HeldCons = InMemory(ConsPath);
		bool bChainInOwner = false;
		for (const TObjectPtr<UDataTable>& T : CS->GetHeldTables().GetTables()) { bChainInOwner |= (ChainDt && T.Get() == ChainDt); }
		const bool bChainOk = ChainDt != nullptr && ChainDt == HeldCons && bChainInOwner;
		UE_LOG(LogAsdaDtHoldStand, Log, TEXT("DTHOLD_GC цепочка FAsdaText::FindTable(\"Constellation\"): %s, тот же объект, что удержан=%d, в наборе подсистемы знаков=%d — %s"),
			ChainDt ? *ChainDt->GetPathName() : TEXT("nullptr"), (ChainDt && ChainDt == HeldCons) ? 1 : 0, bChainInOwner ? 1 : 0, bChainOk ? TEXT("ok") : TEXT("ПРОВАЛ"));

		const bool bControlOk = CtrlPaths.Num() > 0 && CtrlUnloaded > 0;
		const bool bOk = GroupsOk == Groups.Num() && NAfter == AllPaths.Num() && NSame == AllPaths.Num() && RepeatSame == Groups.Num() && bControlOk && bChainOk;
		UE_LOG(LogAsdaDtHoldStand, Log,
			TEXT("DTHOLD_GC ИТОГ: групп %d (ok %d), таблиц %d, удержано %.2f МБ; контроль без удержания: выгружено %d из %d (%s); ")
			TEXT("после obj gc в памяти %d, тех же объектов %d из %d; повтор удержания совпал %d из %d; цепочка знака %s; ВЕРДИКТ %s"),
			Groups.Num(), GroupsOk, AllPaths.Num(), HeldBytes / (1024.0 * 1024.0), CtrlUnloaded, CtrlPaths.Num(),
			bControlOk ? TEXT("поймал выгрузку") : (CtrlPaths.Num() == 0 ? TEXT("НЕВОЗМОЖЕН") : TEXT("СЛЕП")),
			NAfter, NSame, AllPaths.Num(), RepeatSame, Groups.Num(), bChainOk ? TEXT("ok") : TEXT("ПРОВАЛ"), bOk ? TEXT("УСПЕХ") : TEXT("ПРОВАЛ"));
	}
}

static FAutoConsoleCommand GAsdaDtHoldGcCheckCmd(
	TEXT("asda.DtHold.GcCheck"),
	TEXT("Проверка: таблицы, державшиеся только слабыми кэшами, переживают obj gc при удержании владельцами (FAsdaHeldTables). Нужен живой GameInstance (-game). Запускать после стендов с сырыми указателями."),
	FConsoleCommandWithArgsDelegate::CreateStatic(&AsdaDtHoldStand::RunGcCheck));

#endif // !UE_BUILD_SHIPPING
