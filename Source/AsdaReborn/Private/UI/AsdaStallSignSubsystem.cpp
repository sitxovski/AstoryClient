// Copyright (c) 2026 @sitxovski (Discord)
// Вывески и прилавки личных лавок (`CShopMgr`) и очередь 6304. Назначение, эталонные адреса, решения порта — в шапке
//   `Public/UI/AsdaStallSignSubsystem.h`. Волна Л3 торговли (30.09.2026), отчёт `Reports/trade/wave_L3.md`.
#include "UI/AsdaStallSignSubsystem.h"
#include "AsdaPerf.h"   // ★ R4-b 14.11.2026: области замера времени игрового потока
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "UI/AsdaSelfStoreSubsystem.h"      // `RequestVisit` (клик), `SendClose6305` (смерть), премиум/название (реклама)
#include "UI/AsdaWindowSubsystem.h"         // зоны интерфейса (мировой клик), «чужой UI над курсором»
#include "UI/AsdaUiHelpers.h"               // проекция, обводка, выравнивание, геометрия
#include "World/AsdaWorldSubsystem.h"       // персонажи мира (charId / userNum / GM-скрытие / имя своего)
#include "World/AsdaPlayerCharacter.h"      // `MeshRoot` (матрица тела), высота транспорта
#include "World/AsdaPlayerController.h"     // «мы мертвы»
#include "World/AsdaNameplateComponent.h"   // плашка имени: титул/клан/верх AABB, признак лавки
#include "World/AsdaKingCrownSubsystem.h"   // эталонное состояние действия персонажа (8 — сидит)
#include "World/AsdaCoord.h"                // координаты игры для рекламы
#include "AsdaEffectActor.h"                // kAsdaGmHiddenTagName — метка GM-скрытого персонажа (общая точка)
#include "Resources/AsdaAssetPaths.h"       // путь жёсткой модели персонажа
#include "Login/AsdaLoginData.h"            // DecodeCp1251 — название из 6304
#include "AsdaNetSubsystem.h"
#include "AsdaSelf.h"
#include "AsdaBytes.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextBlock.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaStallSign, Log, All);

namespace
{
	// Шаблоны вывесок (импорт 1:1 из `.JUI`, `w0/wbp_tree_headless.txt`): 23450 `StreetStall_DisplayOnChar` (190×25, скин
	//   `Box_3WhiteRound_GrayFill`) и 23470 `StreetStall_DisplayOnChar1` (192×27, скин `StreetStall_SP_Win`, премиум).
	const TCHAR* kSignWbp  = TEXT("/Game/AsdaAssets/UI/Player_UI/Trade/WBP_StreetStall_DisplayOnChar.WBP_StreetStall_DisplayOnChar_C");
	const TCHAR* kSignWbp1 = TEXT("/Game/AsdaAssets/UI/Player_UI/Trade/WBP_StreetStall_DisplayOnChar1.WBP_StreetStall_DisplayOnChar1_C");
	const TCHAR* kFrame    = TEXT("StreetStall_DisplayOnChar_0_");      // 9 планов рамки 23450 (`_0_0`…`_0_8`)
	const TCHAR* kFrame1   = TEXT("StreetStall_DisplayOnChar1_0_");     // 9 планов рамки 23470
	const TCHAR* kText     = TEXT("StreetStall_DisplayTxt");            // 23455
	const TCHAR* kText1    = TEXT("StreetStall_DisplayTxt1");           // 23475
	constexpr float kTextJuiH = 17.f;                                    // `.JUI`: 23455/23475 `Size = 160 17`
	constexpr int32 kTextAlign = 5;                                      // `CControl_SetField924(text, 5)` — центр по обеим осям
	const TCHAR* kShopModel = TEXT("shop");                             // `sprintf(modelName, "shop.jbm")`
	const FName kMeshTag(TEXT("AsdaStallShop"));                        // метка компонента модели прилавка

	ASDA_DEV_CVAR(int32, CVarStallSigns, TEXT("asda.Trade.StallSigns"), 1,
		TEXT("Вывески и прилавки личных лавок (`CShopMgr`, волна Л3): 1 = как оригинал; 0 = прилавки и вывески не ставятся (отладка; "
		     "очередь 6304 и признаки персонажей ведутся)."),
		ECVF_Default);

	// ★ Волна Л4/Л5 (ревизия Л3, §3): [ИСПРАВЛЕНИЕ] К-9 — очередь 6304 эталона не чистится для персонажа, так и не появившегося
	//   в мире: застрявшая «открыта» (закрытие не дошло — клиент ушёл из сектора, S:100286/100428) применится при позднем появлении
	//   персонажа без лавки — фантомные прилавок и вывеска, клик по ним → 6307 → отказ сервера.
	// ★ [ПОРТ] 08.10.2026 (баг владельца п.3, `Reports/owner_1008/ui_bugs.md`): строка названия вывески по центру рамки.
	//   Эталон: текст 23455 (Origin «15 4», 160×17) центрируется в своём прямоугольнике (`SetField924(5)`), затем TextPos.y +1 и
	//   `SetField74_75(x, y + 1)` — строка на 2 px ниже центра рамки; у порта к этому добавлялась ошибка высоты строки (замер
	//   без обводки: 13 против живых 16) — итого 3.5 px ниже, видно глазом. 1 — центр строки = центр рамки (по живой высоте);
	//   0 — прежнее (эталонные смещения поверх выравнивания).
	ASDA_DEV_CVAR(int32, CVarSignTextCenter, TEXT("asda.Trade.SignTextCenter"), 1,
		TEXT("[ПОРТ] Вывеска лавки: 1 = строка названия по центру рамки по вертикали (по живой высоте строки); 0 = прежнее (+TextPos +1)."),
		ECVF_Default);

	ASDA_DEV_CVAR(int32, CVarStallQueueFixK9, TEXT("asda.Trade.StallQueueFixK9"), 1,
		TEXT("[ИСПРАВЛЕНИЕ] К-9 очереди вывесок 6304: 1 = при появлении персонажа (4005, маркер в порядке потока) из очереди снимаются все "
		     "прежние записи его charId (сервер досылает 6304 после 4005, только если лавка открыта — GameServer.c:11935-11943/12695-12703), "
		     "и очередь схлопывается по charId (новая запись заменяет прежнюю того же персонажа); 0 = как оригинал (записи живут до сброса "
		     "сессии, копятся)."),
		ECVF_Default);
}

// ═════════════════════════════ ЖИЗНЕННЫЙ ЦИКЛ ═════════════════════════════════════════════════

void UAsdaStallSignSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	// ★ 21.10.2026 (Alt+Z): вывеска — «мировое» окно оригинала (рисуется до блока бита 128), при скрытом интерфейсе видна и кликается.
	Mouse.bIgnoreUiHidden = true;
	Super::Initialize(Collection);
	Collection.InitializeDependency(UAsdaNetSubsystem::StaticClass());
	Collection.InitializeDependency(UAsdaWindowSubsystem::StaticClass());
	Collection.InitializeDependency(UAsdaSelfStoreSubsystem::StaticClass());
	if (UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr)
	{
		HNet = Net->OnSelfStorePacket.AddUObject(this, &UAsdaStallSignSubsystem::OnPacket);
	}
	UE_LOG(LogAsdaStallSign, Log, TEXT("[вывески] подсистема поднята (6304, прилавки shop.jbm, вывески 23450/23470, клик → 6307, реклама 5086)"));
}

void UAsdaStallSignSubsystem::Deinitialize()
{
	if (UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr)
	{
		Net->OnSelfStorePacket.Remove(HNet);
	}
	CloseAll(TEXT("выгрузка подсистемы"));
	Super::Deinitialize();
}

void UAsdaStallSignSubsystem::ResetSessionState(EAsdaSessionResetReason Reason)
{
	// Чисто локально (закон сброса): ни 6305, ни 5086 — сервер при разрыве/портале лавку закрывает сам (`DisConnectUser` S:111416).
	const int32 NStalls = Stalls.Num(), NQueue = Queue.Num(), NFlags = Flags.Num();
	Flags.Reset();                     // сначала признаки: перераскладка плашек при снятии прилавков увидит «не в лавке»
	CloseAll(TEXT("сброс сессии"));
	Queue.Reset();
	PendingPackets.Clear();
	LastTopLeft.Reset();
	LastAnchor.Reset();
	LastWorldPt.Reset();
	OpenFailLogged.Reset();
	LastQueueAt = -1.0e9;
	LastSelfAt = -1.0e9;
	Mouse.Reset();
	if (NStalls || NQueue || NFlags)
	{
		JournalAdd(FString::Printf(TEXT("сброс сессии (%s): прилавков %d, очередь 6304 %d, признаков %d — сняты без пакетов"),
			AsdaSessionResetReasonName(Reason), NStalls, NQueue, NFlags));
	}
}

// ★ R4-b 14.11.2026 [ПОРТ] (R4, доля b, B4): шаг (3) `UpdateStalls` на каждого соседа искал его userIdx линейным обходом реестра игроков
//   (`FindUserIdxByActor`) — квадратично по числу игроков; плюс две подсистемы на каждого. Теперь userIdx даёт сам обход реестра
//   (`ForEachPlayerActorWithIds`, тот же фильтр `charId != 0`, свой павн — прежним поиском), подсистемы — раз на проход.
// ★ R4-b закрытие (15.11.2026): ключ `asda.Perf.StallSignIds` — старый путь и ключ удалены — только новое (решение владельца).

void UAsdaStallSignSubsystem::Tick(float /*DeltaTime*/)
{
	ASDA_PERF_SCOPE("Tick.StallSign");   // ★ R4-b (14.11.2026): область замера (AsdaPerf.h; в Shipping пусто)
	const UWorld* W = GetWorld();
	if (!W || !W->IsGameWorld()) { return; }
	PumpPending();
	UpdateNameStatusList(false);   // главный цикл, game_main.c:1505
	UpdateStalls(false);           // `LocalPlayer_UpdateRender` + `CharManager_UpdateAll`
	RenderSigns();                 // `Chat_RenderHeadBubble` (трамплины 0x92985B/0x932FDC из главного цикла)
	TickClick();                   // событие 11 по клону
}

// ═════════════════════════════ 6304 И ОЧЕРЕДЬ ═════════════════════════════════════════════════

void UAsdaStallSignSubsystem::OnPacket(int32 Op, const TArray<uint8>& Body)
{
	// Колбэк сети: только копим (закон `no-reentrant-net-callbacks`). Делегат общий с витриной — берём 6304 и (★ Л4/Л5, К-9)
	//   маркер 4005 «появление charId» (4 б), который сетевой слой кладёт в тот же поток; порядок пакетов сохраняется.
	if (Op != 6304 && Op != 4005) { return; }
	FPending P; P.Op = Op; P.Body = Body;
	PendingPackets.Push(MoveTemp(P));
}

void UAsdaStallSignSubsystem::PumpPending()
{
	// ★ U7 (В1 дедупа №2): общая очередь `TAsdaNetQueue` (замок + Drain) вместо своего массива.
	TArray<FPending> Batch;
	if (!PendingPackets.Drain(Batch)) { return; }
	const bool bFixK9 = CVarStallQueueFixK9.GetValueOnGameThread() != 0;
	for (const FPending& P : Batch)
	{
		if (P.Op == 4005)
		{
			// ★ [ИСПРАВЛЕНИЕ] К-9 (п. 1 рецепта ревизии Л3): персонаж X вошёл в обзор — прежние записи X устарели. 6304 из того же
			//   снапшота (если лавка открыта) придёт СЛЕДОМ и ляжет в очередь заново. Уже применённые признаки не трогаются: у нового
			//   актора они и так с нуля (`FlagsOfActor` сверяет носителя), как у нового `CCharacter` эталона.
			if (!bFixK9 || P.Body.Num() < 4) { continue; }
			const int32 CharId = AsdaBytes::RdI32(P.Body.GetData());
			const int32 NRem = Queue.RemoveAll([CharId](const FStatusEntry& E) { return E.CharId == CharId; });
			if (NRem > 0)
			{
				JournalAdd(FString::Printf(TEXT("4005 charId %d: сняты устаревшие записи 6304 — %d (К-9, записей %d)"), CharId, NRem, Queue.Num()));
			}
			continue;
		}
		if (P.Body.Num() < 56) { continue; }
		// `charMgrAddNameStatusEntry@0xDDA2E0`: `memcpy(v3, Src + 9, 56)` → `sub_92C6DC(this + 160, v3)` — в конец очереди.
		FStatusEntry E;
		E.Open = P.Body[0];
		E.Premium = P.Body[1];
		E.CharId = AsdaBytes::RdI32(P.Body.GetData() + 2);
		E.Title = FAsdaLoginData::DecodeCp1251(P.Body.GetData() + 6, 50);
		// ★ [ИСПРАВЛЕНИЕ] К-9 (п. 2 рецепта): схлопывание по charId — проход эталона применяет записи по порядку, и последняя
		//   затирает все три поля (+225921/+225922/+225923), поэтому прежняя запись того же персонажа ничего не меняет в итоге;
		//   очередь ограничена числом разных charId.
		if (bFixK9) { Queue.RemoveAll([&E](const FStatusEntry& Q) { return Q.CharId == E.CharId; }); }
		Queue.Add(E);
		JournalAdd(FString::Printf(TEXT("6304 в очередь: открыта=%d премиум=%d charId=%d «%s» (записей %d)"), (int32)E.Open, (int32)E.Premium,
			E.CharId, *E.Title, Queue.Num()));
	}
}

void UAsdaStallSignSubsystem::UpdateNameStatusList(bool bForce)
{
	// `charManager_UpdateNameStatusList@0xDDA3C0` (entity_char.c:48723-48771): очередь не пуста И прошло > 1000 мс ⇒ метка времени,
	//   проход: `CharManager_FindByCharId(+2)` найден ⇒ +225921 = [0] ? 1 : 0, +225923 = название, +225922 = ([1] == 1), запись
	//   удаляется; не найден — остаётся (К-9 [ОРИГИНАЛ]: у персонажа, так и не появившегося в мире, запись живёт до сброса сессии;
	//   ★ Л4/Л5 — [ИСПРАВЛЕНИЕ] ключом `asda.Trade.StallQueueFixK9`: чистка по 4005 и схлопывание по charId в `PumpPending`).
	if (Queue.Num() == 0) { return; }
	const double Now = FPlatformTime::Seconds();
	if (!bForce && Now - LastQueueAt <= kQueuePeriodS) { return; }
	LastQueueAt = Now;
	UGameInstance* GI = GetGameInstance();
	const UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
	const UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!WS) { return; }
	const int32 SelfCharId = Net ? AsdaSelf::GetCharId(Net) : 0;
	for (int32 i = 0; i < Queue.Num(); )
	{
		const FStatusEntry& E = Queue[i];
		AActor* A = WS->FindPlayerActorByCharId(E.CharId);
		if (!A) { ++i; continue; }
		FCharFlags& F = Flags.FindOrAdd(E.CharId);
		F.Open = E.Open ? 1 : 0;
		F.Title = E.Title;
		F.bPremium = (E.Premium == 1);
		F.Actor = A;
		F.bSelf = (SelfCharId != 0 && E.CharId == SelfCharId);
		JournalAdd(FString::Printf(TEXT("6304 применён к charId %d%s: в лавке=%d премиум=%d «%s»"), E.CharId, F.bSelf ? TEXT(" (свой)") : TEXT(""),
			(int32)F.Open, F.bPremium ? 1 : 0, *F.Title));
		Queue.RemoveAt(i);
	}
}

bool UAsdaStallSignSubsystem::GetCharFlags(int32 CharId, bool& bOutOpen, bool& bOutPremium, FString& OutTitle) const
{
	const FCharFlags* F = Flags.Find(CharId);
	if (!F) { return false; }
	bOutOpen = (F->Open == 1);
	bOutPremium = F->bPremium;
	OutTitle = F->Title;
	return true;
}

const UAsdaStallSignSubsystem::FCharFlags* UAsdaStallSignSubsystem::FlagsOfActor(const AActor* A, int32 UserNum) const
{
	// Поля `CCharacter` носителя: свой — по своему charId (объект локального игрока один на процесс); чужой — по charId соседа,
	//   и только если признаки поставлены ЭТОМУ актору (новый актор = новый `CCharacter`, признаки с нуля).
	UGameInstance* GI = GetGameInstance();
	const UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	const UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
	return FlagsOfActorWith(Net, WS, A, UserNum);   // ★ R4-b 14.11.2026: тело — в `FlagsOfActorWith` (дословно)
}

const UAsdaStallSignSubsystem::FCharFlags* UAsdaStallSignSubsystem::FlagsOfActorWith(const UAsdaNetSubsystem* Net, const UAsdaWorldSubsystem* WS,
	const AActor* A, int32 UserNum) const
{
	if (!Net || !WS || !A) { return nullptr; }
	if (AsdaSelf::IsSelfUserIdx(Net, UserNum))
	{
		const FCharFlags* F = Flags.Find(AsdaSelf::GetCharId(Net));
		return (F && F->bSelf) ? F : nullptr;
	}
	const FCharFlags* F = Flags.Find(WS->GetNetPlayerCharId(UserNum));
	return (F && !F->bSelf && F->Actor.Get() == A) ? F : nullptr;
}

// ═════════════════════════════ ПРИЛАВКИ ═══════════════════════════════════════════════════════

FAsdaStallObj* UAsdaStallSignSubsystem::FindStall(int32 UserNum)
{
	for (FAsdaStallObj& S : Stalls) { if (S.UserNum == UserNum) { return &S; } }
	return nullptr;
}

const FAsdaStallObj* UAsdaStallSignSubsystem::FindStall(int32 UserNum) const
{
	for (const FAsdaStallObj& S : Stalls) { if (S.UserNum == UserNum) { return &S; } }
	return nullptr;
}

void UAsdaStallSignSubsystem::UpdateStalls(bool bForceSelf)
{
	UGameInstance* GI = GetGameInstance();
	UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
	const UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!WS || !Net) { return; }
	const bool bEnabled = CVarStallSigns.GetValueOnGameThread() != 0;

	// (1) Уход персонажа из мира — `CharManager_RemoveNode` зовёт `npcShopMgr_CloseShopByUserIdx` сразу (entity_char.c:7079). В порту
	//   сосед уходит из реестра по 4011 мгновенно, а актор ещё секунду тает — поэтому сверяем «актор по userNum» с носителем.
	for (int32 i = Stalls.Num() - 1; i >= 0; --i)
	{
		const FAsdaStallObj& S = Stalls[i];
		AActor* Now = WS->ActorByUserIdx(S.UserNum);
		if (!bEnabled || !Now || Now != S.Owner.Get()) { CloseStall(S.UserNum, bEnabled ? TEXT("персонаж ушёл из мира") : ASDA_DEV_TEXT(TEXT("asda.Trade.StallSigns 0"), TEXT("вывески выключены"))); }
	}
	// Признаки чужого персонажа живут с его объектом `CCharacter`: актор ушёл/сменился — записи нет.
	for (auto It = Flags.CreateIterator(); It; ++It)
	{
		if (It.Value().bSelf) { continue; }
		AActor* A = It.Value().Actor.Get();
		if (!A || WS->FindPlayerActorByCharId(It.Key()) != A) { It.RemoveCurrent(); }
	}
	if (!bEnabled) { return; }

	// (2) Свой — раз в > 500 мс (`LocalPlayer_UpdateRender`, entity_char.c:7772-7778): признак == 1 ⇒ открыть, иначе снять.
	const double TNow = FPlatformTime::Seconds();
	if (bForceSelf || TNow - LastSelfAt > kSelfPeriodS)
	{
		LastSelfAt = TNow;
		const int32 SelfNum = AsdaSelf::GetUserIdxOr(Net, -1);
		AActor* Self = (SelfNum >= 0) ? WS->ActorByUserIdx(SelfNum) : nullptr;
		if (Self)
		{
			const FCharFlags* F = FlagsOfActor(Self, SelfNum);
			if (F && F->Open == 1) { OpenStall(SelfNum, Self); }
			else { CloseStall(SelfNum, TEXT("свой признак лавки 0")); }
		}
	}
	// (3) Чужие — каждый кадр (`CharManager_UpdateAll`, entity_char.c:8473): признак ≠ 1 ИЛИ GM-скрыт (`sub_93B218 & 1`) ⇒ снять.
	ASDA_PERF_SCOPE("StallSign.Peers");   // ★ R4-b 14.11.2026: область замера (AsdaPerf.h; в Shipping пусто)
	// ★ R4-b 14.11.2026 (B4): userIdx даёт обход реестра, подсистемы — раз на проход. ★ R4-b закрытие (15.11.2026): старый путь и ключ удалены — только новое (решение владельца).
	//   GM-скрытие: общая точка — метка актора `kAsdaGmHiddenTagName`, её ставит/снимает `SetCharHiddenByGm` (4025 300/301, 4103).
	const AActor* LocalA = WS->LocalPlayerActor.Get();
	WS->ForEachPlayerActorWithIds([this, WS, Net, LocalA](int32 Key, int32 CharId, AActor* A, bool bSelf)
	{
		if (bSelf || CharId == 0) { return; }   // фильтр прежнего обхода `ForEachPlayerActorWithCharId`
		// Запись реестра соседей с актором своего павна — прежний поиск отдавал свой номер; оставлен он же.
		const int32 Num = (A == LocalA) ? WS->FindUserIdxByActor(A) : Key;
		if (Num < 0) { return; }
		const FCharFlags* F = FlagsOfActorWith(Net, WS, A, Num);
		static const FName kGmTag(kAsdaGmHiddenTagName);
		if (F && F->Open == 1 && !A->ActorHasTag(kGmTag)) { OpenStall(Num, A); }
		else { CloseStall(Num, TEXT("признак лавки 0 / GM-скрыт")); }
	});
}


void UAsdaStallSignSubsystem::OpenStall(int32 UserNum, AActor* Owner)
{
	// `CShopMgr_OpenNpcShop@0xDE0390` (shop_trade.c:18555-18640): прилавок с этим userNum уже есть ⇒ ничего; иначе объект с моделью
	//   `shop.jbm` (`meshLoad_BindSubsetTextures`), провал загрузки ⇒ `ErrorDetect_LogWrite("Shop open fail")` и объекта нет;
	//   успех ⇒ в список и перераскладка плашки владельца.
	if (!Owner || FindStall(UserNum)) { return; }
	if (!ShopMesh)
	{
		ShopMesh = LoadObject<UStaticMesh>(nullptr, *AsdaAssetPaths::CharStaticMeshObject(kShopModel), nullptr, LOAD_NoWarn | LOAD_Quiet);
	}
	if (!ShopMesh)
	{
		if (!OpenFailLogged.Contains(UserNum))
		{
			OpenFailLogged.Add(UserNum);
			UE_LOG(LogAsdaStallSign, Warning, TEXT("[вывески] Shop open fail: модель %s не загружена (userNum %d)"),
				*AsdaAssetPaths::CharStaticMeshObject(kShopModel), UserNum);
		}
		return;
	}
	// [ПОРТ] Матрица модели = матрица персонажа (`charRenderList_ApplyTransformColors`): крепим к `MeshRoot` в нуле — тот же масштаб
	//   (glTF метры → см), зеркало Y и курс, что у тела. У актора без `MeshRoot` — к корню.
	USceneComponent* Parent = Owner->GetRootComponent();
	if (AAsdaPlayerCharacter* Ch = Cast<AAsdaPlayerCharacter>(Owner)) { if (Ch->MeshRoot) { Parent = Ch->MeshRoot; } }
	UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(Owner, NAME_None, RF_Transient);
	C->SetStaticMesh(ShopMesh);
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->SetCastShadow(true);
	C->ComponentTags.Add(kMeshTag);
	C->SetupAttachment(Parent);
	C->SetRelativeTransform(FTransform::Identity);
	C->RegisterComponent();
	FAsdaStallObj S;
	S.UserNum = UserNum;
	S.Owner = Owner;
	S.Mesh = C;
	S.Zone = FName(*FString::Printf(TEXT("StallSign_%d"), UserNum));
	Stalls.Add(S);   // `sub_93CA8C(npcShopList, &shopObj)` — в конец списка
	OpenFailLogged.Remove(UserNum);
	RelayoutPlate(Owner, UserNum);
	JournalAdd(FString::Printf(TEXT("прилавок userNum %d поставлен (shop.jbm на %s)"), UserNum, *Owner->GetName()));
}

void UAsdaStallSignSubsystem::CloseStall(int32 UserNum, const TCHAR* Why)
{
	// `npcShopMgr_CloseShopByUserIdx@0xDE0760` (npc_mob_pet.c:5701-5782): узлы модели из сцены, перераскладка плашки владельца (если
	//   он в мире), клон вывески и его дети — в очередь удаления окон, объект освобождается.
	int32 Idx = INDEX_NONE;
	for (int32 i = 0; i < Stalls.Num(); ++i) { if (Stalls[i].UserNum == UserNum) { Idx = i; break; } }
	if (Idx == INDEX_NONE) { return; }
	FAsdaStallObj S = Stalls[Idx];
	Stalls.RemoveAt(Idx);
	if (S.Mesh) { S.Mesh->DestroyComponent(); }
	if (S.Sign)
	{
		S.Sign->RemoveFromParent();
		if (UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr) { WM->UnregisterUiZone(S.Zone); }
	}
	LastTopLeft.Remove(UserNum);
	LastAnchor.Remove(UserNum);
	LastWorldPt.Remove(UserNum);
	UAsdaWorldSubsystem* WS = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
	if (AActor* A = WS ? WS->ActorByUserIdx(UserNum) : nullptr) { RelayoutPlate(A, UserNum); }
	JournalAdd(FString::Printf(TEXT("прилавок userNum %d снят (%s)"), UserNum, Why));
}

void UAsdaStallSignSubsystem::CloseAll(const TCHAR* Why)
{
	while (Stalls.Num() > 0) { CloseStall(Stalls.Last().UserNum, Why); }
}

void UAsdaStallSignSubsystem::RelayoutPlate(AActor* A, int32 UserNum)
{
	// `charNamePlate_LayoutControls@0xA4B4E0` (чужой) / `uiItemIconLayoutAndDraw` (свой): бейдж плашки −34 при +225921, ещё −5 без
	//   титула и клана. Порт: признак — в `UAsdaNameplateComponent::bShopOpen`, раскладка — общая точка `RefreshNameplate`.
	UAsdaNameplateComponent* NP = A ? A->FindComponentByClass<UAsdaNameplateComponent>() : nullptr;
	if (!NP) { return; }
	const FCharFlags* F = FlagsOfActor(A, UserNum);
	NP->bShopOpen = (F && F->Open == 1);
	NP->RefreshNameplate();
}

// ═════════════════════════════ ВЫВЕСКИ ════════════════════════════════════════════════════════

bool UAsdaStallSignSubsystem::CreateSign(FAsdaStallObj& S, bool bPremium)
{
	// `CControl_InstantiateChild` шаблона 23450 (или 23470 при премиум-признаке персонажа) и его детей: болты 23451/23452, текст 23455;
	//   `Class_GetVec2_74(текст)` → `SetField74_75(текст, x, y + 1)` (текст на 1 px ниже), видимость 1, текст, `SetField926(0x800000)`
	//   (обводка), `SetField924(5)` (центр), `SetField923(−1)` (белый). Шаблона 23450 нет ⇒ клона нет (`*(this + 377)`).
	UGameInstance* GI = GetGameInstance();
	UWorld* W = GetWorld();
	UClass* Cls0 = LoadClass<UUserWidget>(nullptr, kSignWbp);
	UClass* Cls = bPremium ? LoadClass<UUserWidget>(nullptr, kSignWbp1) : Cls0;
	if (!Cls0 || !Cls || !W)
	{
		if (!bSignClassFailLogged)
		{
			bSignClassFailLogged = true;
			UE_LOG(LogAsdaStallSign, Warning, TEXT("[вывески] шаблон вывески не загружен (%s) — вывесок нет"), bPremium ? kSignWbp1 : kSignWbp);
		}
		return false;
	}
	APlayerController* PC = W->GetFirstPlayerController();
	UUserWidget* Wd = PC ? CreateWidget<UUserWidget>(PC, Cls) : CreateWidget<UUserWidget>(GI, Cls);
	if (!Wd) { return false; }
	Wd->AddToViewport(kSignZ);
	Wd->SetVisibility(ESlateVisibility::HitTestInvisible);
	S.OccOpacity = 1.f;        // новый клон непрозрачен; перекрытие мерится заново на первом кадре показа
	S.OccState.Reset();
	const FString FramePrefix = bPremium ? kFrame1 : kFrame;
	S.Frame0 = Wd->GetWidgetFromName(FName(*(FramePrefix + TEXT("0"))));   // левый-верхний план
	S.Frame8 = Wd->GetWidgetFromName(FName(*(FramePrefix + TEXT("8"))));   // правый-нижний план
	S.Text = Cast<UTextBlock>(Wd->GetWidgetFromName(FName(bPremium ? kText1 : kText)));
	const UCanvasPanelSlot* S0 = AsdaUi::SlotOf(S.Frame0);
	const UCanvasPanelSlot* S8 = AsdaUi::SlotOf(S.Frame8);
	if (S0 && S8)
	{
		S.Origin = S0->GetPosition();
		S.Size = S8->GetPosition() + S8->GetSize() - S.Origin;
	}
	if (S.Text)
	{
		AsdaUi::ApplyJuiTextAlign(S.Text, kTextAlign, kTextJuiH);       // `SetField924(5)` — по маске эталона, один раз на клон
		if (UCanvasPanelSlot* ST = AsdaUi::SlotOf(S.Text))
		{
			FMargin O = ST->GetOffsets();
			O.Top += 1.f;                                                   // `SetField74_75(текст, x, y + 1)`
			ST->SetOffsets(O);
		}
		AsdaUi::SetTextOutline(S.Text, 1);                                 // `SetField926(0x800000)` — обводка
		S.Text->SetColorAndOpacity(FSlateColor(FLinearColor::White));      // `SetField923(−1)` = #FFFFFFFF
	}
	S.Sign = Wd;
	S.bPremiumSign = bPremium;
	// Зона интерфейса: клик по вывеске в мир не проходит (у эталона клик по контролу UI мир не видит).
	if (UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		const int32 Num = S.UserNum;
		WM->RegisterUiZone(S.Zone, FAsdaUiZoneRect::CreateWeakLambda(this, [this, Num](FVector2D& Mn, FVector2D& Mx) -> bool
		{
			if (bZoneProbe) { return false; }   // опрос «чужой UI над курсором» — своя зона молчит
			return SignAbsRect(Num, Mn, Mx);
		}));
	}
	JournalAdd(FString::Printf(TEXT("вывеска userNum %d: клон %s, название «%s»"), S.UserNum, bPremium ? TEXT("23470 (премиум)") : TEXT("23450"), *S.Title));
	return true;
}

void UAsdaStallSignSubsystem::ApplyTruncated(FAsdaStallObj& S)
{
	// `uiSetTruncatedMarkupText@0xDE1E10` (ui_core.c:65432-65459): ширина названия ≤ 145 ⇒ целиком; иначе посимвольно, и каждый
	//   символ, после которого строка шире 125, заменяется точкой — на третьей точке стоп («…» из трёх точек в хвосте).
	if (!S.Text) { return; }
	const FSlateFontInfo Font = S.Text->GetFont();
	auto Wd = [&Font](const FString& X) { return AsdaUi::MeasureTextWidth(X, Font, 7.f * X.Len()); };
	FString Buf;
	if (Wd(S.Title) <= 145.f)
	{
		Buf = S.Title;
	}
	else
	{
		int32 Dots = 0;
		for (int32 i = 0; i < S.Title.Len(); ++i)
		{
			Buf.AppendChar(S.Title[i]);
			if (Wd(Buf) > 125.f)
			{
				++Dots;
				Buf[Buf.Len() - 1] = TEXT('.');
				if (Dots == 3) { break; }
			}
		}
	}
	S.Text->SetText(FText::FromString(Buf));
	CenterSignText(S);   // ★ 08.10.2026: высота строки известна только после текста и обводки
}

void UAsdaStallSignSubsystem::CenterSignText(FAsdaStallObj& S)
{
	if (!S.Text || CVarSignTextCenter.GetValueOnGameThread() == 0) { return; }
	UCanvasPanelSlot* ST = AsdaUi::SlotOf(S.Text);
	if (!ST) { return; }
	S.Text->ForceLayoutPrepass();                                      // живая высота строки (шрифт + обводка)
	const float LineH = (float)S.Text->GetDesiredSize().Y;
	if (LineH <= 1.f) { return; }
	FMargin O = ST->GetOffsets();                                     // слот точечный: (x, y, ширина, высота)
	const float Top = S.Origin.Y + (S.Size.Y - LineH) * 0.5f;         // центр строки = центр рамки (Origin/Size — планы 0 и 8)
	if (FMath::IsNearlyEqual(O.Top, Top, 0.01f) && FMath::IsNearlyEqual(O.Bottom, LineH, 0.01f)) { return; }
	O.Top = Top;
	O.Bottom = LineH;
	ST->SetOffsets(O);
}

void UAsdaStallSignSubsystem::RenderSigns()
{
	// `Chat_RenderHeadBubble@0xDE10E0` (chat.c:6423-6660) — по списку прилавков.
	UGameInstance* GI = GetGameInstance();
	UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	UAsdaSelfStoreSubsystem* Store = GI ? GI->GetSubsystem<UAsdaSelfStoreSubsystem>() : nullptr;
	UWorld* W = GetWorld();
	if (!WS || !Net || !Store || !W) { return; }
	const double Now = FPlatformTime::Seconds();
	for (int32 i = 0; i < Stalls.Num(); ++i)
	{
		FAsdaStallObj& S = Stalls[i];
		AActor* Speaker = WS->ActorByUserIdx(S.UserNum);   // `CharManager_FindByUserIdx(entry+460)`
		if (!Speaker) { continue; }
		const bool bSelf = AsdaSelf::IsSelfUserIdx(Net, S.UserNum);   // `effectSlots[112].field_40 == 1` — локальный игрок
		if (!S.Sign)
		{
			// Первый кадр: название — из персонажа (`sprintf(entry+488, char+225923)`), вид клона — по его премиум-признаку.
			const FCharFlags* F = FlagsOfActor(Speaker, S.UserNum);
			S.Title = F ? F->Title : FString();
			CreateSign(S, F && F->bPremium);
		}
		if (!S.Sign) { continue; }
		if (Now - S.T4 > kSignPeriodS)
		{
			// Раз в 4 с: обрезка названия заново; «свой и состояние 6 (смерть)» ⇒ `antiCheat_SendPositionReport` = 6305
			//   (★ реестр Л1-2: правило переехало сюда из Tick витрины хозяина — там, где оно у эталона).
			S.T4 = Now;
			ApplyTruncated(S);
			if (bSelf)
			{
				const AAsdaPlayerController* PC = Cast<AAsdaPlayerController>(W->GetFirstPlayerController());
				if (PC && PC->IsSelfDead())
				{
					Store->SendClose6305(TEXT("смерть при открытой лавке (рендер своей вывески, раз в 4 с)"));
					JournalAdd(FString::Printf(TEXT("своя вывеска userNum %d: состояние 6 (смерть) → 6305"), S.UserNum));
				}
			}
		}
		if (bSelf && Now - S.TAd > kAdvertPeriodS)
		{
			// Раз в 300 с метка ставится ВСЕГДА, реклама — только премиум (`byte_2C2F759` = `CSellWindow+0x389`).
			S.TAd = Now;
			if (Store->IsPremium()) { SendAdvert(Speaker); }
		}
		PlaceSign(S, Speaker);
	}
}

void UAsdaStallSignSubsystem::SendAdvert(AActor* Self)
{
	// chat.c:6497-6520: заголовок 28 б (`sub_931204`, [2] = 0, имя с +3), текст `"%s [X:%d.Y:%d]"` = название витрины (`sub_924793`
	//   = `CShopDlg+0x171`), `(u64)(x / 100)`, `(u64)(z / 100)`; `CNetwork_Send(…, 5086, 0, 1, 1, 0, 0)` — без секрета и без пада.
	//   Порт — общая точка `SendChatNormal5086` (тот же заголовок 27 б + текст, пад выключен).
	UGameInstance* GI = GetGameInstance();
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	const UAsdaSelfStoreSubsystem* Store = GI ? GI->GetSubsystem<UAsdaSelfStoreSubsystem>() : nullptr;
	const UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
	if (!Net || !Store || !WS || !Self) { return; }
	const FVector2D G = AsdaCoord::UeToGame2D(Self->GetActorLocation());   // (gameX, gameZ)
	const uint32 X = (uint32)(uint64)(int64)(G.X / 100.0);
	const uint32 Y = (uint32)(uint64)(int64)(G.Y / 100.0);
	const FString Text = FString::Printf(TEXT("%s [X:%d.Y:%d]"), *Store->GetStoreTitle(), (int32)X, (int32)Y);
	const bool bSent = Net->SendChatNormal5086(Text, WS->GetSelfCharName());
	JournalAdd(FString::Printf(TEXT("реклама премиум-лавки 5086 %s: «%s»"), bSent ? TEXT("отправлена") : TEXT("НЕ отправлена"), *Text));
}

void UAsdaStallSignSubsystem::PlaceSign(FAsdaStallObj& S, AActor* Speaker)
{
	// Хвост `Chat_RenderHeadBubble`: персонаж во фрустуме И рисуется ⇒ видима, иначе скрыта; точка: у сидящего (состояние 8) —
	//   `soundPos` (центр AABB), иначе верх AABB (`helper_getSumF(+0x1B0)`) + 10; + `heightOffset` транспорта (Table2 15/2); точка во
	//   фрустуме ⇒ экран: X − ширина/2, Y − (сдвиг + 62); сдвиг: +16 за титул, +16 за клан, +10 без того и другого.
	UWorld* W = GetWorld();
	APlayerController* PC = W ? W->GetFirstPlayerController() : nullptr;
	if (!S.Sign || !PC) { return; }
	FVector2D Feet;
	const bool bCull = !Speaker->IsHidden() && AsdaUi::WorldToViewportFollow(PC, W, Speaker->GetActorLocation(), S.Size.X + 64.f, Feet);
	if (!bCull)
	{
		S.bShown = false;
		S.Sign->SetVisibility(ESlateVisibility::Collapsed);
		return;
	}
	const UAsdaNameplateComponent* NP = Speaker->FindComponentByClass<UAsdaNameplateComponent>();
	// ★ «Верх AABB» — единая точка с плашкой имени и облачком чата (высота из данных, закон `nameplate-height-from-data`);
	//   центр AABB = половина высоты (AABB стоит на земле: полу-размер = H/2, центр = земля + H/2).
	const float TopZ = NP ? NP->GetEntityTopZ() : 150.f;
	const int32 State = UAsdaKingCrownSubsystem::EtalonActionStateOf(Speaker);
	float Z = (State == 8) ? TopZ * 0.5f : TopZ + 10.f;
	if (const AAsdaPlayerCharacter* Ch = Cast<AAsdaPlayerCharacter>(Speaker)) { if (Ch->IsMounted()) { Z += Ch->MountHeightOffset; } }
	FVector Pt = Speaker->GetActorLocation();
	Pt.Z += Z;
	// ★★★ [ОРИГИНАЛ] 29.10.2026 (баг игроков 3, ключ asda.Overhead.Occlude): вывеска перекрывается геометрией мира на глубине
	//   этой точки (у оригинала — z-тест планов, chat.c:6586). Перекрыта целиком — скрыта и не кликается [ПОРТ] (клик по
	//   невидимой вывеске у оригинала проходил бы — хит-тест JUI глубину не смотрит; кликать то, чего не видно, нельзя).
	// ★★★ [ОРИГИНАЛ] 05.11.2026 (asda.Overhead.OccludeAtText): z-тест — в пикселях самой вывески (она стоит на (сдвиг + 62) px выше
	//   проекции точки), а не в точке: решение по строке центра вывески (разбор — `AsdaOverheadOcclusion::EvaluateText`).
	const bool bTitleO = NP && (NP->TitleId0 >= 0 || NP->TitleId1 >= 0);
	const bool bClanO = NP && NP->ClanId != -1;
	const float OffO = (bTitleO ? 16.f : 0.f) + (bClanO ? 16.f : 0.f) + ((!bTitleO && !bClanO) ? 10.f : 0.f);
	const float DPIo = FMath::Max(UWidgetLayoutLibrary::GetViewportScale(W), 0.01f);
	const float OccA = AsdaOverheadOcclusion::EvaluateText(S.OccState, W, Pt,
		FVector2D(0.f, (S.Size.Y * 0.5f - (OffO + 62.f)) * DPIo), S.Size.X * 0.5f * DPIo, Speaker);
	S.bShown = OccA > 0.f;
	S.Sign->SetVisibility(S.bShown ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	if (!S.bShown) { return; }
	if (OccA != S.OccOpacity)
	{
		S.OccOpacity = OccA;
		S.Sign->SetRenderOpacity(OccA);
	}
	FVector2D Screen;
	if (!AsdaUi::WorldToViewportFollow(PC, W, Pt, S.Size.X + 64.f, Screen)) { return; }   // `cullVisible2 == 0` — позиция прежняя
	const bool bTitle = NP && (NP->TitleId0 >= 0 || NP->TitleId1 >= 0);
	const bool bClan = NP && NP->ClanId != -1;
	int32 Off = 0;
	if (bTitle) { Off += 16; }
	if (bClan) { Off += 16; }
	if (!bTitle && !bClan) { Off += 10; }
	const float DPI = FMath::Max(UWidgetLayoutLibrary::GetViewportScale(W), 0.01f);
	const FVector2D TopLeft(FMath::FloorToFloat(Screen.X / DPI - S.Size.X * 0.5f), FMath::FloorToFloat(Screen.Y / DPI - (float)(Off + 62)));
	S.Sign->SetPositionInViewport(TopLeft - S.Origin, /*bRemoveDPIScale=*/false);
	S.Sign->SetDesiredSizeInViewport(S.Origin + S.Size);
	LastTopLeft.Add(S.UserNum, TopLeft);
	LastAnchor.Add(S.UserNum, Screen / DPI);
	LastWorldPt.Add(S.UserNum, Pt);
}

// ═════════════════════════════ КЛИК ПО ВЫВЕСКЕ ════════════════════════════════════════════════

bool UAsdaStallSignSubsystem::SignAbsRect(int32 UserNum, FVector2D& OutMin, FVector2D& OutMax) const
{
	const FAsdaStallObj* S = FindStall(UserNum);
	if (!S || !S->Sign || !S->bShown || !S->Frame0 || !S->Frame8) { return false; }
	FVector2D Mn0, Mx0, Mn8, Mx8;
	if (!AsdaUi::WidgetAbsRect(S->Frame0, Mn0, Mx0) || !AsdaUi::WidgetAbsRect(S->Frame8, Mn8, Mx8)) { return false; }
	OutMin = Mn0;
	OutMax = Mx8;
	return OutMax.X > OutMin.X && OutMax.Y > OutMin.Y;
}

void UAsdaStallSignSubsystem::TickClick()
{
	// `ui_DispatchCommandEvent` (ui_core.c:34313): событие 11 (нажатие ЛКМ) по контролу → `tradeRequestTargetByServerIdx(контрол)`:
	//   прилавок, чей клон вывески = контрол, → его userNum → гейты и 6307 (`UAsdaSelfStoreSubsystem::RequestVisit`). Дочерние контролы
	//   вывески `NoSelect = 1` — клик ловит рамка целиком. Над вывеской другой интерфейс или модалка ⇒ клик не её.
	Mouse.Update();
	if (!Mouse.bLmbDown || Stalls.Num() == 0) { return; }
	UGameInstance* GI = GetGameInstance();
	if (AsdaUi::IsModalDispatchBlocked(GI)) { return; }
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	UAsdaSelfStoreSubsystem* Store = GI ? GI->GetSubsystem<UAsdaSelfStoreSubsystem>() : nullptr;
	if (!WM || !Store) { return; }
	bZoneProbe = true;
	const bool bOther = WM->IsCursorOverAnyUiWindow();
	bZoneProbe = false;
	if (bOther) { return; }
	// Позже созданная вывеска лежит выше (порядок добавления во вьюпорт при равном z) — проверяем с конца.
	for (int32 i = Stalls.Num() - 1; i >= 0; --i)
	{
		FVector2D Mn, Mx;
		if (!SignAbsRect(Stalls[i].UserNum, Mn, Mx)) { continue; }
		const FVector2D C = Mouse.Cursor;
		if (C.X < Mn.X || C.X > Mx.X || C.Y < Mn.Y || C.Y > Mx.Y) { continue; }
		const int32 Num = Stalls[i].UserNum;
		JournalAdd(FString::Printf(TEXT("клик по вывеске userNum %d"), Num));
		Store->RequestVisit(Num, TEXT("клик по вывеске (ЛКМ, событие 11)"));
		return;
	}
}

// ═════════════════════════════ ДИАГНОСТИКА ════════════════════════════════════════════════════

UStaticMeshComponent* UAsdaStallSignSubsystem::DebugStallMesh(int32 UserNum) const
{
	const FAsdaStallObj* S = FindStall(UserNum);
	return S ? S->Mesh.Get() : nullptr;
}

UUserWidget* UAsdaStallSignSubsystem::DebugSignWidget(int32 UserNum) const
{
	const FAsdaStallObj* S = FindStall(UserNum);
	return S ? S->Sign.Get() : nullptr;
}

FString UAsdaStallSignSubsystem::DebugSignText(int32 UserNum) const
{
	const FAsdaStallObj* S = FindStall(UserNum);
	return (S && S->Text) ? S->Text->GetText().ToString() : FString();
}

bool UAsdaStallSignSubsystem::DebugSignPremium(int32 UserNum) const
{
	const FAsdaStallObj* S = FindStall(UserNum);
	return S && S->bPremiumSign;
}

bool UAsdaStallSignSubsystem::DebugSignShown(int32 UserNum) const
{
	const FAsdaStallObj* S = FindStall(UserNum);
	return S && S->Sign && S->bShown;
}

bool UAsdaStallSignSubsystem::DebugSignPlacement(int32 UserNum, FVector2D& OutTopLeft, FVector2D& OutAnchor) const
{
	const FVector2D* T = LastTopLeft.Find(UserNum);
	const FVector2D* A = LastAnchor.Find(UserNum);
	if (!T || !A) { return false; }
	OutTopLeft = *T;
	OutAnchor = *A;
	return true;
}

bool UAsdaStallSignSubsystem::DebugSignWorldPoint(int32 UserNum, FVector& OutPt) const
{
	const FVector* P = LastWorldPt.Find(UserNum);
	if (!P) { return false; }
	OutPt = *P;
	return true;
}

void UAsdaStallSignSubsystem::DebugExpireTimers()
{
	LastQueueAt = -1.0e9;
	LastSelfAt = -1.0e9;
	for (FAsdaStallObj& S : Stalls) { S.T4 = -1.0e9; S.TAd = -1.0e9; }
}

FString UAsdaStallSignSubsystem::DumpState() const
{
	FString Out = FString::Printf(TEXT("вывески: прилавков %d, очередь 6304 %d, признаков %d"), Stalls.Num(), Queue.Num(), Flags.Num());
	for (const FAsdaStallObj& S : Stalls)
	{
		Out += FString::Printf(TEXT(" | userNum %d: модель=%d вывеска=%s видна=%d «%s»"), S.UserNum, S.Mesh ? 1 : 0,
			S.Sign ? (S.bPremiumSign ? TEXT("23470") : TEXT("23450")) : TEXT("нет"), S.bShown ? 1 : 0, *S.Title);
	}
	return Out;
}

void UAsdaStallSignSubsystem::JournalAdd(const FString& Line)
{
	Journal.Add(Line);
	UE_LOG(LogAsdaStallSign, Log, TEXT("[вывески] %s"), *Line);
}

// ═════════════════════════════ КОНСОЛЬ ════════════════════════════════════════════════════════

namespace
{
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
	FAutoConsoleCommandWithWorldAndArgs GAsdaTradeStallSignsDump(
		TEXT("asda.Trade.StallSignsDump"),
		TEXT("asda.Trade.StallSignsDump — состояние вывесок и прилавков личных лавок (`CShopMgr`): прилавки, клоны вывесок, очередь 6304."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W)
		{
			UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
			const UAsdaStallSignSubsystem* S = GI ? GI->GetSubsystem<UAsdaStallSignSubsystem>() : nullptr;
			UE_LOG(LogAsdaStallSign, Display, TEXT("%s"), S ? *S->DumpState() : TEXT("подсистемы нет"));
		}));
#endif // ASDA_DEV_CONSOLE
}
