// Copyright (c) 2026 @sitxovski (Discord)
// AsdaWorldSubsystem_Peers.cpp — ★ 23.10.2026 часть UAsdaWorldSubsystem, вынесенная из AsdaWorldSubsystem.cpp БАЙТ В БАЙТ
//   (разнос по смысловым группам; доказательство — Tools/asda_split_cpp.py verify, план Reports/split_2026-10-23/AsdaWorldSubsystem.plan.json).
//   Соулмейт, выбор рамкой, одержимость, фракция, питомцы и транспорт, ангел-хранитель, консоли крыльев/войны/урона.
//   #include — те же, что у AsdaWorldSubsystem.cpp, в том же порядке; общее — AsdaWorldSubsystemInternal.h.
#include "World/AsdaWorldSubsystem.h"
#include "World/AsdaPvpRelation.h"   // ★ bowfx: фильтр площадного сбора персонажей (`charManager_collectTargetsInRect`)
#include "UI/AsdaEntryWindows.h"   // ★ 22.10.2026: окна при своём 4055/4056 — список эталона вместо CloseAll
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "UI/AsdaPartySubsystem.h"   // ★ 04.11.2026: ростер отряда по charId — сбор согруппников группового баффа
#include "Text/AsdaLang.h"   // ★ N9 (дедуп №2, В2-A): FAsdaLang::MissingTag — единая заглушка «[#id]»
#include "UI/AsdaNoticeColors.h"   // ★ U12 (В1 дедупа №2): эталонные цвета нотисов одной точкой
#include "UI/AsdaNotice.h"              // ★ 27.09.2026: плашка 221 для отказов 4007 (канал обработчика `Recv_EntityMove`)
#include "HAL/IConsoleManager.h"        // ключ `asda.Move.RefusePlank`
#include "Resources/AsdaAssetPaths.h"   // ★ волна 2 п. 7 (14.09.2026): реестр путей и правил имён ассетов (генерируемый)
#include "Resources/AsdaModelAnimSubsystem.h"   // Д2/Д8/Д9: ресурс только через менеджер
#include "AsdaNetSubsystem.h"
#include "AsdaSelf.h"   // ★ «это я?» — единая точка своего номера (E2, 14.09.2026)
#include "World/AsdaMapRegistry.h"   // ★ волна 2 (14.09.2026): реестр карт — генерируемый заголовок из Tools/data/asda_map_registry.json (путь DA_WalkGrid)
#include "World/AsdaPlayerCharacter.h"
#include "World/AsdaAttackRhythmSubsystem.h"   // ★ 15.10.2026: ключ отката ритма (темп предметов), кадры ударов своего замаха
#include "World/AsdaWalkGrid.h"
#include "Characters/AsdaCharAssembly.h"      // кости/attach-матрицы оружия «в руке ↔ за спиной»
#include "Camera/CameraComponent.h"
#include "UI/AsdaHudSubsystem.h"             // HERO_NOTICE_WIN — только чужой редкий дроп (6131)
// ★ системный чат CHAT_NEW_SYSTEM + оверлеи A/B — все лут-строки   ★ R3: комментарий снятого включения "UI/AsdaChatSubsystem.h" (файл им не пользовался) — сохранён
#include "UI/AsdaMessageSubsystem.h"         // ★★★ ЕДИНЫЙ маршрутизатор сообщений (категория → окно по таблице)
#include "Framework/Application/SlateApplication.h"
#include "Npc/AsdaNpcActor.h"
#include "Npc/AsdaMobActor.h"
#include "World/AsdaFadeComponent.h"
#include "World/AsdaGroundTrace.h"
#include "World/AsdaCoord.h"                 // ★ Аудит №3 (B1/B2): единый перевод координат и тайловая сетка
#include "World/AsdaPlayerController.h"      // снятие таргета при смерти цели-моба (эталон SetActionState(10))
#include "World/AsdaProjectileActor.h"       // ★ снаряды дальнобойного оружия (projectile_re.md)
#include "Materials/MaterialInterface.h"      // материал меша стрелы (у ..._1 ссылка битая)
#include "World/AsdaFloatingTextSubsystem.h" // цифры урона (порт CFloatingText)
#include "Resources/AsdaGameUserSettings.h"  // ★ 17.10.2026 (Ф5): режим показа цифр урона `DamageTextMode`
#include "Skill/AsdaSkillFxSubsystem.h"      // ★★★ стык «урон ↔ визуал»: момент удара навыка
#include "World/AsdaFxLightSettings.h"         // ★ волна 3 п.16 (15.09.2026): AsdaWeaponGlow — имя свечения заточки (одна таблица баз)
#include "Items/AsdaItemTableSubsystem.h"    // тип оружия атакующего (импакт battk01..04)
// надетое (контейнер 3): проверка сломанных вещей   ★ R3: комментарий снятого включения "UI/AsdaInventorySubsystem.h" (файл им не пользовался) — сохранён
#include "Items/AsdaItemDbRows.h"            // ★ сырая запись skill_info (.sif +0x48 = buff0 состояния отражения)
#include "UI/AsdaSkillDatabase.h"            // ★ Аудит №3: поля записи навыка — только отсюда
#include "UI/AsdaEffectsSubsystem.h"         // ★ состояния мобов: 4017 бафф-стеки и бафф отражения
#include "AsdaBytes.h"                       // ★ единые читалки сырой записи (DataHex)
#include "Engine/DataTable.h"
#include "Resources/AsdaSoundSubsystem.h"    // звуки ударов (S_c_*normal1, фазовые звуки мобов)
#include "Components/MeshComponent.h"          // подошва цели: низ ВИДИМОЙ геометрии (TargetSoleZ)
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/WidgetComponent.h"          // ★ плашка имени — тоже UMeshComponent, но не тело
#include "Engine/StaticMesh.h"
#include "TimerManager.h"
#include "World/AsdaNameplateComponent.h"   // над-головные плашки (порт JUI-окон 1:1)
#include "World/AsdaEntityText.h"           // ★ волна 3 п.14: имена/модели/звук-базы мобов и NPC — одна точка
#include "AsdaDataTableCache.h"             // AsdaDt::Cached — объекты таблиц имён для сильного удержания (HoldEntityNameTables)
#include "World/AsdaWorldKind.h"            // ★ F7 (дедуп №2, В3): общие признаки «логин» / «загрузка»
#include "AsdaHeldTables.h"                  // FAsdaHeldTables — удержание таблиц спауна (15.09.2026, dt_gc_hold)
#include "World/AsdaLootActor.h"             // наземный лут (порт DropItemMgr, loot_system_re.md)
#include "World/AsdaPetActor.h"              // питомцы (6120, pet_system_re.md)
#include "Pet/AsdaPetSubsystem.h"            // ★ 30.10.2026: призванный питомец из списка 6106 (RestoreSelfPetFromList)
#include "Misc/ScopeExit.h"                  // ★ 30.10.2026: ON_SCOPE_EXIT в TickPeerPetQueue
#include "World/AsdaSoulGuardianActor.h"     // ★ компаньон-хранитель «ангел» (6156, soul_guardian_companion_re.md)
#include "Misc/ConfigCacheIni.h"             // GConfig — опции показа ангела (FxConfig)
#include "AsdaEffectActor.h"                 // эффекты персонажей: крылья/кольца soulmate (follow-кость)
#include "AsdaFxNames.h"                     // ★ дедуп №2 В1-C (F5/F6): теги эффектов персонажа, имена одиночных эффект-BP
#include "Resources/AsdaMaterialNames.h"   // ★ дедуп №2 В1-C (F10/F16): пути мастеров, имена параметров, экранные ассеты
#include "ProceduralMeshComponent.h"         // asda.VerifyWeaponGlow: чтение мировых вершин секций эффекта
// loc-нотисы подбора (1011/1012/1030)   ★ R3: комментарий снятого включения "Text/AsdaTextSubsystem.h" (файл им не пользовался) — сохранён
// UAsdaRichText::ApplyFormatted (единая точка текста)   ★ R3: комментарий снятого включения "UI/AsdaMarkup.h" (файл им не пользовался) — сохранён
#include "Engine/TextureRenderTarget2D.h"   // RT эмблемы клана — это UTexture-наследник
#include "World/AsdaClanFlag.h"              // FAsdaClanFlagData::FromWire (флаг клана из 6542)
#include "Login/AsdaLoginData.h"             // DecodeCp1251 (имена монстров в cp1251)
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Animation/AnimSequence.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/UObjectGlobals.h"
#include "AsdaOutgoingGate.h"               // ★ С-Р1: общий офлайн-гейт отладочных команд (RefuseDebugWhenOnline)
#include "World/AsdaCombatTrace.h"           // ★ 18.10.2026: журнал боя (ASDA_CTRACE) — полный цикл действий клиента в бою
#include "AsdaWorldSubsystemInternal.h"   // ★ 23.10.2026 разнос: общее для частей AsdaWorldSubsystem*.cpp (LogAsdaWorld, журнал боя, чистые хелперы)


// ★★★ К-3: СБРОС СОСТОЯНИЯ СЕССИИ (шапка `AsdaSessionReset.h`).
//   Реестры мира (`NetMobs`/`NetNpcs`/`NetPlayers`/`NetLoot`) уже чистятся по 4003 — «начало
//   сессии карты», см. `OnSelfSpawn` ниже. Но 4003 приходит ТОЛЬКО при входе в мир, поэтому
//   выход в выбор персонажа и разрыв связи оставляли и реестры, и зеркала своего персонажа
//   (клан, золото, имя, экипировка, фракция) от ПРЕДЫДУЩЕЙ сессии.
// ★ ВОЛНА 6: ключи очереди 6120. Разбор механизма — у объявления `TickPeerPetQueue` в `.h`.
static ASDA_DEV_CVAR(int32, CVarPeerPetQueue,
	TEXT("asda.Pet.PeerQueue"), 1,
	TEXT("1 (по умолчанию) — приход 6120 кладётся в очередь и разбирается раз в 2000 мс, ")
	TEXT("как у эталона (CPetData::EnqueuePeerPet@0xDCA620 + слив @0xDCA6B0, порог 0x7D0). ")
	TEXT("0 — применять сразу (поведение порта до волны 6)."),
	ECVF_Default);

// ★★★ 30.10.2026 [ИСПРАВЛЕНИЕ] свой питомец после входа и портала — разбор у `RestoreSelfPetFromList` в `.h`.
static ASDA_DEV_CVAR(int32, CVarPetSelfRestore,
	TEXT("asda.Pet.SelfRestore"), 1,
	TEXT("1 (по умолчанию) — призванный питомец из списка 6106 ставится в мир у своего персонажа, если его 6120 не пришёл ")
	TEXT("(сервер шлёт свой 6120 при входе раньше, чем персонаж в секторе, и после портала не шлёт вовсе). ")
	TEXT("0 — как раньше: свой питомец только по 6120 (после входа/портала его нет, пока не вызвать заново)."),
	ECVF_Default);

// ★★★ 29.10.2026 [ОРИГИНАЛ]+[ИСПРАВЛЕНИЕ] баг игроков «кольцо соулмейта остаётся после разрыва (Друг был вне игры)».
//   Оригинал держит номер пары в ОДНОМ поле персонажа `CCharacter+2215` (`gap_878[47]`): его пишут и 6152
//   (`PartyMark_Helper2@0xE64750`, social.c:7081), и 5220 (`memcpy(char+2215, Src+6, 0x5F)`), а 5207 ставит −1 себе и
//   партнёру (`soulmateWnd_handleResponsePacket@0xE67590`, social.c:7760/7766); объекты персонажей живут одну сессию.
//   У порта два кэша: `SoulByUserIdx` (5220) и `SoulIdxByCharId` (6152). Второй НЕ трогали ни 5220, ни 5207, и он НЕ
//   чистился при сбросе сессии ⇒ после разрыва (своим 5207 или пока был вне игры — сервер при входе честно шлёт 5220
//   со Smidx −1, `UPCharSoulMate` берёт только State=1) свой спавн (вход без перезапуска клиента, портал) снова
//   зажигал кольцо из устаревшего номера 6152; лечил только перезапуск клиента.
//   1 = как оригинал: 5220 пишет и кэш по charId, 5207 ставит −1 себе и партнёру, полный сброс сессии чистит кэш.
//   0 = прежнее поведение порта.
static ASDA_DEV_CVAR(int32, CVarSoulRingCacheSync,
	TEXT("asda.Soulmate.RingCacheSync"), 1,
	TEXT("1 (по умолчанию) — номер пары соулмейта (кольцо) одним полем, как у оригинала: 5220 и 6152 пишут его оба,\n")
	TEXT("разрыв 5207 ставит −1 себе и партнёру, выход из мира/разрыв связи чистит кэш (кольцо не возвращается).\n")
	TEXT("0 — прежний порт: кэш 6152 переживал разрыв пары и выход из мира."),
	ECVF_Default);
// Значение `asda.Soulmate.RingCacheSync` для других частей мира (сброс сессии в AsdaWorldSubsystem.cpp).
bool AsdaSoulRingCacheSyncOn() { return CVarSoulRingCacheSync.GetValueOnGameThread() != 0; }

AActor* UAsdaWorldSubsystem::ActorByUserIdx(int32 UserIdx) const
{
	// ★★★ ЖИВОЙ ТЕСТ 2026-08-03: гард `SelfUserNum != 0` ЛОМАЛ поиск СВОЕГО павна — наш сервер выдаёт
	//   userNum = 0 (лог `SpawnSelf 4003: userNum=0`), и «свой» никогда не совпадал. Поиск проваливался
	//   в NetPlayers (реестр ЧУЖИХ игроков), где своего нет по определению → «актора нет в мире»,
	//   из-за чего не игрались ни клип гибели, ни поза, ни обесцвечивание. userNum 0 — ВАЛИДНЫЙ.
	if (const UAsdaNetSubsystem* Net = GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>())
	{
		if (Net->IsSelfUserIdx(UserIdx) && ::IsValid(LocalPlayerActor.Get())) { return LocalPlayerActor.Get(); }
	}
	if (const FAsdaNetPlayerEntry* E = NetPlayers.Find(UserIdx)) { return E->Actor.Get(); }
	return nullptr;
}

void UAsdaWorldSubsystem::OnSoulmateState(int32 CharId, int32 UserIdx, int32 SoulIdx, int32 PartnerUserIdx)
{
	FAsdaSoulState& S = SoulByUserIdx.FindOrAdd(UserIdx);
	S.SoulIdx = SoulIdx; S.PartnerUserIdx = PartnerUserIdx;
	// ★ 29.10.2026 [ОРИГИНАЛ] (ключ asda.Soulmate.RingCacheSync): 5220 пишет то же поле +2215, что и 6152 — последний пакет
	//   решает. Иначе Smidx −1 из 5220 (вход после разрыва) не перебивал прежний номер 6152, и спавн зажигал кольцо.
	if (CharId != 0 && AsdaSoulRingCacheSyncOn()) { SoulIdxByCharId.Add(CharId, SoulIdx); }
	ApplySoulRingToActor(ActorByUserIdx(UserIdx), SoulIdx > 0);
}

// Сердечки-линка (эталон Char_SpawnSoulLinkEffect@0xA2CC10, тик rand()%5000 мин 1000): снаряд soullink02
//   летит к партнёру, по прибытии — импакт soullink03. ★ СКОРОСТЬ ДОРЕВЕРСНУТА (projectile_re.md §7):
//   projectileEffectInit@0xA5AC80 ставит this[473] = 0x3E99999A = 0.3 юнита/мс и dur = dist/0.3,
//   а deltaTime движка — МИЛЛИСЕКУНДЫ ⇒ 300 uu/с (было приближение 400). Зовётся из TickAoiDespawn.
void UAsdaWorldSubsystem::TickSoulHearts()
{
	UWorld* W = GetGameInstance()->GetWorld();
	if (!W) { return; }
	const double Now = W->GetTimeSeconds();
	for (TPair<int32, FAsdaSoulState>& It : SoulByUserIdx)
	{
		FAsdaSoulState& S = It.Value;
		if (S.SoulIdx <= 0 || S.PartnerUserIdx <= 0) { continue; }
		AActor* A = ActorByUserIdx(It.Key);
		AActor* B = ActorByUserIdx(S.PartnerUserIdx);
		if (!A || !B) { continue; }
		if (Now < S.NextHeartS) { continue; }
		S.NextHeartS = Now + 1.0 + (FMath::Rand() % 4000) / 1000.0;   // эталон: rand()%5000, минимум 1000мс
		const FVector From = A->GetActorLocation() + FVector(0.f, 0.f, 60.f);
		// ★ Аудит №3 (A10): спавн через ЕДИНУЮ точку по .jeb-базе (было своё `LoadClass` полного пути BP).
		AActor* Heart = AAsdaEffectActor::SpawnByJebName(W, TEXT("eff_1_char_99_soullink02-a_00"),
			From, FRotator::ZeroRotator, nullptr);
		if (!Heart) { continue; }
		Heart->SetLifeSpan(6.f);   // страховка от зависших снарядов
		TWeakObjectPtr<AActor> WeakHeart = Heart, WeakTo = B;
		TWeakObjectPtr<UAsdaWorldSubsystem> WeakThis = this;
		// ★ ИСПРАВЛЕНИЕ 11.10.2026 (В0 аудита дублей №2, F14): зацикленный таймер полёта сердца раньше НЕ снимался —
		//   ни при попадании, ни при исчезновении снаряда/партнёра: каждое сердце оставляло вечный таймер 33 Гц
		//   (рост числа таймеров за сессию карты). Теперь таймер снимает себя на любом конце полёта; счётчик
		//   `ActiveSoulHeartTimers` — для проверки (самотест `asda.Soul.SelfTestWave1A`, этап N).
		TSharedRef<FTimerHandle> Timer = MakeShared<FTimerHandle>();   // ручка таймера — в замыкании, чтобы снять себя
		TWeakObjectPtr<UWorld> WeakWorld = W;                          // мир таймера (ручка снимается в его менеджере)
		++ActiveSoulHeartTimers;
		FTimerDelegate Move = FTimerDelegate::CreateLambda([WeakHeart, WeakTo, WeakThis, Timer, WeakWorld]()
		{
			// Конец полёта: снять свой таймер и уменьшить счётчик (один раз — ручка после снятия невалидна).
			//   ★ Снятие исполняемого таймера уничтожает его делегат (а с ним и это замыкание) прямо внутри
			//   `ClearTimer` — поэтому всё нужное копируется в локальные ДО снятия, после него захваты не трогаются.
			auto Stop = [&Timer, &WeakWorld, &WeakThis]()
			{
				UWorld* TW = WeakWorld.Get();
				UAsdaWorldSubsystem* WS = WeakThis.Get();
				FTimerHandle Local = *Timer;   // копия ручки: сама ручка живёт в замыкании
				if (!TW || !Local.IsValid()) { return; }
				if (WS) { --WS->ActiveSoulHeartTimers; }
				Timer->Invalidate();
				TW->GetTimerManager().ClearTimer(Local);   // последняя операция: дальше захваты могут быть уже уничтожены
			};
			AActor* H = WeakHeart.Get();
			AActor* T = WeakTo.Get();
			if (!H) { Stop(); return; }
			if (!T) { H->Destroy(); Stop(); return; }
			const FVector Target = T->GetActorLocation() + FVector(0.f, 0.f, 60.f);
			const FVector Cur = H->GetActorLocation();
			const FVector Delta = Target - Cur;
			const float Step = 300.f * 0.03f;   // 0.3 юнита/мс (эталон projectileEffectInit this[473])
			if (Delta.Size() <= Step)
			{
				H->Destroy();
				if (UAsdaWorldSubsystem* WS = WeakThis.Get())
				{
					// ★ ИСПРАВЛЕНИЕ 11.10.2026 (дедуп №2, В2-C, F2): срок — `NaturalLifeSpan` (эталон: импакт по прилёту снаряда
					//   отдаётся менеджеру эффектов, `projectileEffectInit` → конечный эффект без хэндла). Было жёсткое 1,5 с при
					//   natural 4,1 с (контейнер 3,6 с endMode 0 + 0,5) — хвост в 2,6 с обрезался (снимок `asda.Dedup.W2C.Life`).
					if (AAsdaEffectActor* Fx = Cast<AAsdaEffectActor>(WS->SpawnCharFx(TEXT("eff_1_char_99_soullink03-a_00"), T,
						FVector(0.f, 0.f, 60.f), FName(TEXT("SoulImpactFx")))))
					{
						Fx->SetLifeSpan(Fx->NaturalLifeSpan());
					}
				}
				Stop();
				return;
			}
			H->SetActorLocation(Cur + Delta.GetSafeNormal() * Step);
		});
		W->GetTimerManager().SetTimer(*Timer, Move, 0.03f, true);
	}
}

// ── 5203/5207: церемония бонда и разрыв пары (wings_soulmate_re.md §F) ──

// ★ Эталонный CharManager_FindByCharId: сначала свой персонаж (charId выбран на char-select),
//   затем реестр соседей. Возвращает nullptr, если такого персонажа нет в мире (эталон в этом
//   случае просто не рисует облачко — `if (g_bShowEntityNames && a3)`).
AActor* UAsdaWorldSubsystem::FindPlayerActorByCharId(int32 CharId) const
{
	return ActorByCharId(CharId);   // ★ Дедуп №2, В2-A (N4): прежнее имя — форвард общей точки (тело перенесено без изменений)
}

// ★ Дедуп №2, В2-A (N4): единый поиск актора по charId — разбор у объявления.
AActor* UAsdaWorldSubsystem::ActorByCharId(int32 CharId) const
{
	if (CharId == 0) { return nullptr; }
	if (const UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr)
	{
		if (AsdaSelf::IsSelfCharId(Net, CharId) && ::IsValid(LocalPlayerActor.Get())) { return LocalPlayerActor.Get(); }
	}
	for (const TPair<int32, FAsdaNetPlayerEntry>& It : NetPlayers)
	{
		if (It.Value.CharId == CharId) { return It.Value.Actor.Get(); }
	}
	return nullptr;
}

// ★ 27.09.2026 Обход персонажей мира с charId (обоснование — в заголовке).
void UAsdaWorldSubsystem::ForEachPlayerActorWithCharId(TFunctionRef<void(int32 CharId, AActor* Actor, bool bSelf)> Fn) const
{
	if (const UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr)
	{
		AActor* Self = LocalPlayerActor.Get();   // свой павн
		if (Net->SelectedCharId != 0 && ::IsValid(Self)) { Fn((int32)Net->SelectedCharId, Self, true); }
	}
	for (const TPair<int32, FAsdaNetPlayerEntry>& It : NetPlayers)
	{
		AActor* A = It.Value.Actor.Get();   // чужой игрок
		if (It.Value.CharId != 0 && ::IsValid(A)) { Fn(It.Value.CharId, A, false); }
	}
}

void UAsdaWorldSubsystem::ForEachPlayerActorWithIds(TFunctionRef<void(int32 UserIdx, int32 CharId, AActor* Actor, bool bSelf)> Fn) const
{
	if (const UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr)
	{
		AActor* Self = LocalPlayerActor.Get();   // свой павн
		if (::IsValid(Self) && Net->IsSelfUserNumKnown()) { Fn((int32)Net->SelfUserNum, (int32)Net->SelectedCharId, Self, true); }
	}
	for (const TPair<int32, FAsdaNetPlayerEntry>& It : NetPlayers)
	{
		AActor* A = It.Value.Actor.Get();   // чужой игрок
		if (::IsValid(A)) { Fn(It.Key, It.Value.CharId, A, false); }
	}
}

// ★ Эталонный Entity_FindByRenderId (менеджер мобов): renderId → рантайм-копия моба.
AAsdaMobActor* UAsdaWorldSubsystem::FindNetMobByRid(int32 Rid) const
{
	if (const TWeakObjectPtr<AAsdaMobActor>* M = NetMobs.Find(Rid)) { return M->Get(); }
	return nullptr;
}

// ★ Аудит №3 (B6): ЕДИНАЯ развилка «(вид, индекс) → актор» (объявление и доказательство — в заголовке).
//   Ровно то, что эталон пишет двумя строками в каждой из шести своих точек: ветка сущности идёт в
//   `Entity_FindByRenderId@0xF772D0`, ветка персонажа — в `CharManager_FindByUserIdx@0x9D1240`.
//   Отрицательный индекс: у эталона это сторож «записи нет» (`rec[1] == -1` обрывает цикл разбора,
//   skill_buff.c:19016). Здесь он тоже отсекается сразу — поведение прежнее, потому что оба реестра
//   (`NetPlayers`, `NetMobs`) на отрицательный ключ и так отдают nullptr.
AActor* UAsdaWorldSubsystem::ActorByTarget(EAsdaTargetKind Kind, int32 Idx) const
{
	if (Idx < 0) { return nullptr; }
	if (Kind == EAsdaTargetKind::Character) { return ActorByUserIdx(Idx); }   // индекс = userNum
	return FindNetMobByRid(Idx);                                             // индекс = renderId
}

// ══ ★★★ П34 (T4, 2026-08-11): ГРАНИЦЫ БОКСА У ЭТАЛОНА СТРОГИЕ ═══════════════════════════════════
//   Все три эталонных сборщика целей проверяют попадание ОДНОЙ И ТОЙ ЖЕ строкой:
//       `if ( a3 < (double)pos[0] && pos[0] < (double)a5 && a4 < (double)pos[2] && pos[2] < (double)a6 )`
//     · `entityQueryInRectExcl@0xF77390`      — entity_char.c:54094  (сущности/мобы)
//     · `charManager_collectTargetsInRect@0x9DB560` — entity_char.c:54139 (игроки, PvP)
//     · `charCollectPartyMembersInBox@0x9DB8E0`     — entity_char.c:54180 (согруппники)
//   Обе границы ИСКЛЮЧАЮЩИЕ: цель РОВНО на грани бокса в выборку НЕ попадает.
//   Порт сравнивал `Abs(P − Center) > HalfExtent`, то есть грань ВКЛЮЧАЛ. Расхождение стало значимым
//   после П1 (центр бокса переехал на центр тайла: `cx·100 + 50`, полуразмер `R·100 + 50`) — теперь
//   грань бокса ложится ровно на границу тайла, где цели и стоят чаще всего.
//   ⇒ ниже везде строгое `Min < x && x < Max`.
//
// ★★★ Порт `objectsPickInBoundingBox` — цели площадного каста (эталон
//   `Skill_SendAreaSkillTargetsCast@0xA01140`, skill_buff.c:3186-3200). Эталон строит бокс от
//   СВОЕГО тайла ± `skillRecord->maxSkillLevel[0]` (это `.sif +0x37`), переводит углы в мир и
//   расширяет на ±50; сюда приходит уже готовый полу-размер в игровых единицах.
//   Общая проверка «строго внутри бокса» — одна на все три сборщика (закон дедупа).
static FORCEINLINE bool AsdaIsStrictlyInBox(const FVector2D& P, const FVector2D& Center, float HalfExtent)
{
	const float MinX = Center.X - HalfExtent, MaxX = Center.X + HalfExtent;
	const float MinZ = Center.Y - HalfExtent, MaxZ = Center.Y + HalfExtent;
	return (MinX < P.X) && (P.X < MaxX) && (MinZ < P.Y) && (P.Y < MaxZ);
}

int32 UAsdaWorldSubsystem::PickEntityRenderIdsInGameBox(const FVector2D& Center, float HalfExtent,
	TArray<int32>& OutRenderIds, int32 MaxCount, int32 ExcludeRenderId) const
{
	OutRenderIds.Reset();
	for (const TPair<int32, TWeakObjectPtr<AAsdaMobActor>>& It : NetMobs)
	{
		const AAsdaMobActor* M = It.Value.Get();
		if (!M) { continue; }
		// ★ `entityQueryInRectExcl@0xF77390`: `if (a7 != renderId)` — основную цель, уже занявшую
		//   запись №0, повторно не берём.
		if (ExcludeRenderId >= 0 && M->NetRenderId == ExcludeRenderId) { continue; }
		const FVector L = M->GetActorLocation();
		const FVector2D P = AsdaCoord::UeToGame2D(L);       // ★ Аудит №3 (B1): UE → игровые координаты
		if (!AsdaIsStrictlyInBox(P, Center, HalfExtent)) { continue; }   // П34: границы СТРОГИЕ
		OutRenderIds.Add(M->NetRenderId);
		if (OutRenderIds.Num() >= MaxCount) { break; }      // эталонный потолок `targetIds[20]`
	}
	return OutRenderIds.Num();
}

// ★ Сбор userIdx ИГРОКОВ в игровом боксе — порт `charManager_collectTargetsInRect@0x9DB560`
//   (entity_char.c:9612, зовётся из `skillCastAreaTargetsAndSend@0xA12800`): в PvP-режиме площадной
//   каст добавляет записи ПЕРСОНАЖЕЙ (`rec[0] = 1`, userIdx).
//   ★ ПОПРАВКА 2026-08-11: прежняя версия клала в выборку СЕБЯ — это неверно. Эталон первым делом
//   сравнивает `LOWORD(localPlayer->userNum) != LOWORD(i->userNum)` и своего персонажа пропускает;
//   выборка — это ПРОТИВНИКИ (ветка живёт только при `buffFxSlots[92] != 0`, т.е. в PvP).
int32 UAsdaWorldSubsystem::PickPlayerUserIdxsInGameBox(const FVector2D& Center, float HalfExtent,
	TArray<int32>& OutUserIdxs, int32 MaxCount, int32 PrimaryUserIdx) const
{
	OutUserIdxs.Reset();
	const UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	const int32 SelfIdx = Net ? (int32)Net->SelfUserNum : -1;
	const AsdaPvpRel::FSelf Me = AsdaPvpRel::GetSelf(GetGameInstance());   // ★ bowfx: свой PvP-срез (режим/соперник/команда)
	for (const TPair<int32, FAsdaNetPlayerEntry>& It : NetPlayers)
	{
		if (It.Key == SelfIdx) { continue; }                // эталон: свой персонаж не берётся
		if (PrimaryUserIdx >= 0 && It.Key == PrimaryUserIdx) { continue; }   // запись №0 уже занята
		// ★★★ [ОРИГИНАЛ] GM-СКРЫТЫЙ В ПЛОЩАДНОЙ СБОР НЕ ПОПАДАЕТ (27.09.2026, ревизия gm_hide_npc_2 СРЕДН-2):
		//   entity_char.c:9657 — `… || (sub_93B218(i) & 1) != 0) continue;`. Бит проверяется БЕЗУСЛОВНО —
		//   в отличие от отрисовки, где скрытый оператор видит других скрытых полупрозрачными, в сбор
		//   целей скрытый не попадает никогда. Источник бита у порта — набор `GmHiddenUserIdx`
		//   (единая точка `SetCharHiddenByGm`, живёт и без актора).
		//   ⚠ Прочие фильтры этой строки эталона (a7, режим PvP [92] и его ветки 1/2/4, состояния 6/29)
		//   здесь по-прежнему НЕ перенесены — у порта нет их данных; разбор и план — отчёт
		//   `Reports/owner_0927/pick_hidden_filter.md` §5.
		if (IsCharHiddenByGm(It.Key)) { continue; }
		const AAsdaPlayerCharacter* P = It.Value.Actor.Get();
		if (!P) { continue; }
		const FVector L = P->GetActorLocation();
		const FVector2D G = AsdaCoord::UeToGame2D(L);       // ★ Аудит №3 (B1): UE → игровые координаты
		if (!AsdaIsStrictlyInBox(G, Center, HalfExtent)) { continue; }   // П34: границы СТРОГИЕ
		// ★★★ 26.10.2026 (bowfx, баг «вражеская площадь задевает своих») [ОРИГИНАЛ] остальные фильтры строки эталона
		//   (entity_char.c:9657-9690): режим [92] 0 — никого, 1 — только соперник (сбор на нём кончается), 4 — без своей команды,
		//   2 — без своего клана/Друга/согруппников; мёртвые (6/29) не берутся. Сервер из-за ОДНОЙ недопустимой записи отклоняет
		//   всю заявку кодом 0 (нотис 1017, GameServer.c:45946-45955) — без фильтра площадь по врагу не срабатывала совсем.
		//   Ключ `asda.Skill.TargetRelation 0` — прежний сбор (все персонажи бокса).
		if (AsdaPvpRel::IsOn())
		{
			FString Why;
			const AsdaPvpRel::EArea Dec = AsdaPvpRel::AreaCandidate(GetGameInstance(), Me, It.Key, PrimaryUserIdx, &Why);
			if (Dec == AsdaPvpRel::EArea::Skip)
			{
				ASDA_CTRACE_V(EAsdaCT::Skill, TEXT("площадь: персонаж uidx=%d не берётся — %s"), It.Key, *Why);
				continue;
			}
			OutUserIdxs.Add(It.Key);
			if (Dec == AsdaPvpRel::EArea::AddAndStop) { break; }
			if (OutUserIdxs.Num() >= MaxCount) { break; }
			continue;
		}
		OutUserIdxs.Add(It.Key);
		if (OutUserIdxs.Num() >= MaxCount) { break; }       // тот же потолок 20 записей
	}
	return OutUserIdxs.Num();
}

// ★★★ Порт `charCollectPartyMembersInBox@0x9DB8E0` (entity_char.c:9704) — цели ГРУППОВОГО баффа.
//   Эталон дословно:
//     if (a7 >= 0) { *a2++ = a7; ++count; }                       // кастер идёт ПЕРВОЙ записью
//     for (каждый персонаж в боксе)
//         if (!Party_IsInParty()) return 0;
//         if (Party_FindMemberByUserIdx(j->userIdx) && a7 != j->userNum) { *a2++ = j->userNum; ++count; }
//         if (count >= 20) return count;
// ★★★ 04.11.2026 [ИСПРАВЛЕНИЕ] (баг игроков «Party Howl of Stamina 932 — согруппники бафф не получают»): `Party_FindMemberByUserIdx`
//   эталона (social.c:574) — МИСНЕЙМ, ключ поиска — charId записи отряда (+4), а передаётся `j->userIdx` = `CCharacter+0x41C` = charId
//   персонажа; в запись заявки уходит его userNum (`effectSlots[111].field_5B8`). Порт сверял ростер по userNum, а у участника из
//   полного состава 5107 userNum НЕТ (−1: пакет несёт только charId и имя) ⇒ согруппник в боксе не собирался, 5256 уходил одной записью
//   кастера, сервер (`sub_477F60` case 2 — цель только из записей) баффал одного кастера.
//   1 — согруппник распознаётся и по charId (как эталон); 0 — прежний порт (только userNum).
static int32 GAsdaPartyMatchByCharId = 1;
static ASDA_DEV_CVAR_REF(CVarAsdaPartyMatchByCharId,
	TEXT("asda.Party.MatchByCharId"),
	GAsdaPartyMatchByCharId,
	TEXT("1 — сбор согруппников группового баффа узнаёт участника отряда по charId (эталон Party_FindMemberByUserIdx); 0 — прежний порт (только userNum, участники из 5107 не собирались)."),
	ECVF_Default);

int32 UAsdaWorldSubsystem::PickPartyUserIdxsInGameBox(const FVector2D& Center, float HalfExtent,
	TArray<int32>& OutUserIdxs, int32 MaxCount, int32 PrimaryUserIdx) const
{
	const UAsdaPartySubsystem* Party = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaPartySubsystem>() : nullptr;
	OutUserIdxs.Reset();
	// ⚠ [ОРИГИНАЛ] фильтра GM-скрытия здесь у эталона НЕТ (entity_char.c:9704-9749 — только бокс, отряд и
	//   a7): скрытый СОГРУППНИК групповой бафф получает. Сохранено 1:1 — это не выбор цели врага.
	if (PrimaryUserIdx >= 0) { OutUserIdxs.Add(PrimaryUserIdx); }
	for (const TPair<int32, FAsdaNetPlayerEntry>& It : NetPlayers)
	{
		if (OutUserIdxs.Num() >= MaxCount) { break; }
		if (It.Key == PrimaryUserIdx) { continue; }
		// `Party_FindMemberByUserIdx(charId)` — по charId (★ 04.11), userNum остаётся вторым признаком (ростер самотестов, 5093/5097).
		const bool bByCharId = GAsdaPartyMatchByCharId != 0 && Party && It.Value.CharId != 0 && Party->IsMemberCharId(It.Value.CharId);
		if (!bByCharId && !IsPartyMember(It.Key)) { continue; }
		const AAsdaPlayerCharacter* P = It.Value.Actor.Get();
		if (!P) { continue; }
		const FVector L = P->GetActorLocation();
		const FVector2D G = AsdaCoord::UeToGame2D(L);   // ★ Аудит №3 (B1)
		if (!AsdaIsStrictlyInBox(G, Center, HalfExtent)) { continue; }   // П34: границы СТРОГИЕ
		OutUserIdxs.Add(It.Key);
	}
	return OutUserIdxs.Num();
}

void UAsdaWorldSubsystem::SetPartyRoster(const TArray<int32>& UserIdxs)
{
	PartyUserIdxs.Reset();
	for (int32 U : UserIdxs) { if (U >= 0) { PartyUserIdxs.Add(U); } }
	UE_LOG(LogAsdaWorld, Log, TEXT("[отряд] ростер группы: %d согруппник(ов)"), PartyUserIdxs.Num());
}

AActor* UAsdaWorldSubsystem::FindPlayerActorByName(const FString& Name) const
{
	if (Name.IsEmpty()) { return nullptr; }
	for (const TPair<int32, FAsdaNetPlayerEntry>& It : NetPlayers)
	{
		if (It.Value.Name.Equals(Name, ESearchCase::IgnoreCase)) { return It.Value.Actor.Get(); }
	}
	return nullptr;
}

// 5203 subcode 1: у ОБОИХ (LocalPlayer + партнёр по имени) — эффект soulmatec (attach 5 = следует за
//   чаром, кость не используется) + 3D-звук S_c_SMMate01; кольцо ON партнёру (своё придёт 5220/5110).
void UAsdaWorldSubsystem::OnSoulmateBond(int32 Subcode, int32 /*PairIdx*/, int32 /*InitiatorUidx*/, FString PartnerName)
{
	if (Subcode != 1) { return; }   // 0=предложение, 2/3=отказ — UI-нотисы (фаза окна)
	UAsdaSoundSubsystem* Snd = GetGameInstance()->GetSubsystem<UAsdaSoundSubsystem>();
	auto Ceremony = [this, Snd](AActor* A)
	{
		if (!A) { return; }
		// ★ ИСПРАВЛЕНИЕ 11.10.2026 (дедуп №2, В2-C, F2): срок одноразовой церемонии — `NaturalLifeSpan` (до конца контейнера
		//   0/2, как эталон). Было жёсткое 3,5 с при natural 4,6 с (контейнер 4,1 с endMode 0 + 0,5) — конец церемонии обрезался на 1,1 с.
		if (AAsdaEffectActor* Fx = Cast<AAsdaEffectActor>(SpawnCharFx(TEXT("eff_1_char_99_soulmatec-a_01"), A,
			FVector(0.f, 0.f, 5.f), FName(TEXT("SoulBondFx")))))
		{
			Fx->SetLifeSpan(Fx->NaturalLifeSpan());
		}
		// ★ [ОРИГИНАЛ] 30.10.2026 (`asda.Sound.WorldEvents3D`): 3D у каждого из пары, как сказано выше (эталон 5203); прежде 2D.
		if (Snd)
		{
			if (UAsdaSoundSubsystem::WorldEvents3DOn()) { Snd->PlaySound3DAs(FName(TEXT("S_c_SMMate01")), A->GetActorLocation(), EAsdaSoundClass::Magic, A); }
			else                                        { Snd->PlaySound2D(FName(TEXT("S_c_SMMate01"))); }
		}
	};
	Ceremony(LocalPlayerActor.Get());
	AActor* Partner = FindPlayerActorByName(PartnerName);
	Ceremony(Partner);
	if (Partner)
	{
		ApplySoulRingToActor(Partner, true);   // партнёру кольцо ON сразу (эталон)
	}
	UE_LOG(LogAsdaWorld, Log, TEXT("[soulmate] бонд: церемония (партнёр '%s' %s)"),
		*PartnerName, Partner ? TEXT("найден") : TEXT("вне зоны"));
}

// 5207 subcode 1/3: кольцо OFF у себя и партнёра (userIdx партнёра — из сохранённого soulmate-стейта).
void UAsdaWorldSubsystem::OnSoulmateBreak(int32 Subcode, FString Name)
{
	if (Subcode != 1 && Subcode != 3) { return; }   // 0/2 = нотисы-отказы
	UAsdaNetSubsystem* Net = GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>();
	// ★ ИСПРАВЛЕНИЕ 11.10.2026 (В0 аудита дублей №2, N3): условие было `SelfUserNum != 0`, а ноль — ЗАКОННЫЙ номер
	//   (наш сервер выдаёт его штатно, см. `ActorByUserIdx`): при userNum 0 разрыв пары не снимал своё кольцо и не
	//   чистил свою запись `SoulByUserIdx`. Теперь — общая точка «номер известен» (`AsdaSelf`, пакет 4003 пришёл).
	const bool bSelfKnown = AsdaSelf::IsUserIdxKnown(Net);
	const int32 SelfUidx = bSelfKnown ? (int32)Net->SelfUserNum : -1;
	// Своя пара + партнёр по сохранённому userIdx.
	// ★ 29.10.2026 [ОРИГИНАЛ] (ключ asda.Soulmate.RingCacheSync): 5207 ставит −1 в поле +2215 себе и партнёру (social.c:7760/7766) —
	//   у порта это и кэш 6152 по charId, иначе следующий свой спавн (портал) или спавн партнёра вернул бы кольцо.
	const bool bRingSync = AsdaSoulRingCacheSyncOn();
	auto ForgetCharIdOfUser = [this, bRingSync](int32 Uidx)
	{
		if (!bRingSync) { return; }
		if (const FAsdaNetPlayerEntry* E = NetPlayers.Find(Uidx)) { if (E->CharId != 0) { SoulIdxByCharId.Add(E->CharId, -1); } }
	};
	if (bRingSync && Net && Net->SelectedCharId != 0) { SoulIdxByCharId.Add((int32)Net->SelectedCharId, -1); }
	if (bSelfKnown)
	{
		if (FAsdaSoulState* S = SoulByUserIdx.Find(SelfUidx))
		{
			ApplySoulRingToActor(ActorByUserIdx(S->PartnerUserIdx), false);
			ForgetCharIdOfUser(S->PartnerUserIdx);
			SoulByUserIdx.Remove(S->PartnerUserIdx);
			S->SoulIdx = -1; S->PartnerUserIdx = -1;
		}
		ApplySoulRingToActor(LocalPlayerActor.Get(), false);
	}
	// Партнёр мог быть только по имени (5220 ещё не дошёл) — снять и по имени.
	if (AActor* ByName = FindPlayerActorByName(Name))
	{
		ApplySoulRingToActor(ByName, false);
		if (bRingSync)
		{
			for (const TPair<int32, FAsdaNetPlayerEntry>& It : NetPlayers)
			{
				if (It.Value.Actor.Get() == ByName) { ForgetCharIdOfUser(It.Key); SoulByUserIdx.Remove(It.Key); break; }
			}
		}
	}
	UE_LOG(LogAsdaWorld, Log, TEXT("[soulmate] разрыв пары (sub=%d, '%s')"), Subcode, *Name);
}

// ★ 6152: соулмейт-стейт СОСЕДА (основной канал чужих колец — wings_soulmate_re.md §J).
//   Ключ — charId (эталон FindByCharId), НЕ userIdx. Приходит в AoI-бандле сразу после 4005,
//   поэтому может опередить спавн актора → кэшируем и применяем при спавне.
void UAsdaWorldSubsystem::OnSoulmatePeer(int32 CharId, int32 SoulIdx, int32 /*PartnerCharId*/)
{
	SoulIdxByCharId.Add(CharId, SoulIdx);
	const bool bOn = (SoulIdx > 0);
	UAsdaNetSubsystem* Net = GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>();
	// Свой персонаж.
	if (AsdaSelf::IsSelfCharId(Net, CharId))   // ★ ИСПРАВЛЕНИЕ 11.10.2026 (дедуп №2, В2-A, N2): «charId мой?» — `AsdaSelf::IsSelfCharId` (свой ещё неизвестен ⇒ не мой; было 0 == 0 ⇒ мой)
	{
		ApplySoulRingToActor(LocalPlayerActor.Get(), bOn);
		return;
	}
	// Сосед по charId из реестра игроков.
	for (TPair<int32, FAsdaNetPlayerEntry>& It : NetPlayers)
	{
		if (It.Value.CharId == CharId)
		{
			ApplySoulRingToActor(It.Value.Actor.Get(), bOn);
			return;
		}
	}
	// Актора ещё нет — применится при спавне из SoulIdxByCharId.
}

// ═════════ ★★★ ОДЕРЖИМОСТЬ (SoulState) — ПОКАЗ У СЕБЯ И У СОСЕДЕЙ ═══════════════════════════════
//   Ground truth: `Reports/possess_broadcast_soulstate.md`. Байт `CUser+99027` сервера =
//   `CCharacter+2241` эталона; 6152 несёт его в теле @12 (ключ charId), 5220 — в записи @32
//   (ключ userIdx). Эталон при значении 2 красит имя цели в 0xFF6AFFF4; наша метка «(Одержим)»
//   под ником — ДОБАВЛЕНИЕ владельца поверх эталона (закон fix-original-defects).

// Приоритет источника — как у колец: 6152 (charId) основной, 5220 (userIdx) резервный.
int32 UAsdaWorldSubsystem::ResolveSoulStateFor(int32 CharId, int32 UserIdx) const
{
	const UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net) { return 0; }
	const int32 ByChar = (CharId != 0) ? Net->GetSoulStateByCharId(CharId) : -1;
	if (ByChar >= 0) { return ByChar; }
	const int32 ByUser = (UserIdx >= 0) ? Net->GetSoulStateByUserIdx(UserIdx) : -1;
	return (ByUser >= 0) ? ByUser : 0;
}

// ★★★ ДВЕ НЕЗАВИСИМЫЕ ВЕТКИ ПО ЗНАЧЕНИЮ (обе — эталон):
//   • 2 (одержимость): плашка — цвет имени 0xFF6AFFF4 (`:20902`, `:21139`) + наша метка; материалы не трогаются;
//   • 1 («душа»):     плашка ОБЫЧНАЯ (окраски для 1 в эталоне нет), а модель — тело + все надетые части —
//                      полупрозрачная α=0.3 (`charEntityUpdateTick :80703`, разбор soul_silhouette_visual_re.md);
//   • 0:               и плашка обычная, и материалы исходные.
//   Плашке передаётся значение как есть (она сама реагирует только на 2), актору — признак `== 1`.
void UAsdaWorldSubsystem::ApplySoulStateToActor(AActor* Ch, int32 SoulState)
{
	if (!::IsValid(Ch)) { return; }
	if (UAsdaNameplateComponent* NP = Ch->FindComponentByClass<UAsdaNameplateComponent>())
	{
		NP->SetSoulState(SoulState);   // внутри — ранний выход, если значение не изменилось
	}
	if (AAsdaPlayerCharacter* PC = Cast<AAsdaPlayerCharacter>(Ch))
	{
		PC->SetSoulTranslucent(SoulState == 1);   // внутри — ранний выход, если признак не изменился
	}
}

// ★★★ СВОЙ SoulState — ЕДИНСТВЕННЫЙ ИСТОЧНИК ПРАВДЫ = `UAsdaNetSubsystem::SelfSoulState` (0/1/2, ставится
//   по ответу 1017 на 1016 с режимом, гасится по 1027 и в сбросе сессии). Провод (6152 @12 / 5220 @32)
//   несёт для себя то же значение, но позже; прежнее выражение `bSelfPossessed ? 2 : NetSoul` пропускало
//   «1 от себя» только через сетевой кэш, и до прихода 6152 своя «душа» оставалась плотной.
//   Расхождение с проводом — только в журнал (диагностика), значение поля не переписываем.
int32 UAsdaWorldSubsystem::SelfSoulStateAuthoritative(const TCHAR* Source, int32 WireValue) const
{
	const UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net) { return 0; }
	const int32 Self = (int32)Net->SelfSoulState;
	if (WireValue >= 0 && WireValue != Self)
	{
		UE_LOG(LogAsdaWorld, Warning, TEXT("[SoulState self] %s: провод говорит %d, своё поле SelfSoulState=%d — берём своё"),
			Source, WireValue, Self);
	}
	return Self;
}

// 6152: ключ charId (эталонный FindByCharId). Свой персонаж ищется отдельно — он не в NetPlayers.
void UAsdaWorldSubsystem::OnPeerSoulStateByCharId(int32 CharId, int32 SoulState)
{
	UAsdaNetSubsystem* Net = GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>();
	if (AsdaSelf::IsSelfCharId(Net, CharId))   // ★ ИСПРАВЛЕНИЕ 11.10.2026 (дедуп №2, В2-A, N2): «charId мой?» — `AsdaSelf::IsSelfCharId` (свой ещё неизвестен ⇒ не мой; было 0 == 0 ⇒ мой)
	{
		// Свой признак — из единого поля `SelfSoulState` (известно с ответа 1017, до любого 6152).
		ApplySoulStateToActor(LocalPlayerActor.Get(), SelfSoulStateAuthoritative(TEXT("6152"), SoulState));
		return;
	}
	for (TPair<int32, FAsdaNetPlayerEntry>& It : NetPlayers)
	{
		if (It.Value.CharId == CharId)
		{
			ApplySoulStateToActor(It.Value.Actor.Get(), SoulState);
			return;
		}
	}
	// Актора ещё нет — применится при спавне (ResolveSoulStateFor читает кэш сетевого слоя).
}

// 5220: ключ userIdx (эталон FindByUserIdx). Свой userIdx законно бывает нулём — сравниваем
//   ТОЛЬКО через IsSelfUserIdx (закон self-usernum-zero-is-valid).
void UAsdaWorldSubsystem::OnPeerSoulStateByUserIdx(int32 UserIdx, int32 SoulState)
{
	UAsdaNetSubsystem* Net = GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>();
	if (Net && Net->IsSelfUserIdx(UserIdx))
	{
		ApplySoulStateToActor(LocalPlayerActor.Get(), SelfSoulStateAuthoritative(TEXT("5220"), SoulState));
		return;
	}
	ApplySoulStateToActor(ActorByUserIdx(UserIdx), SoulState);
}

// ★ 6722: фракция/звание СОСЕДА (faction_peer_re.md). Ключ — charId (эталон FindByCharId; userIdx@4
//   клиент игнорирует). Цвета полос: 0=Свет/СИНИЙ(1671), 1=Тьма/красный(1670), 2=Хаос/зелёный(1672) —
//   маппинг НЕ монотонный, в UpdateFactionUI он уже верный. Звание → ячейка faction_grade.tex max(g,1)−1.
void UAsdaWorldSubsystem::OnPeerFaction(int32 CharId, int32 CampId, int32 Grade)
{
	FactionByCharId.Add(CharId, FIntPoint(CampId, Grade));
	UAsdaNetSubsystem* Net = GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>();
	// Свой персонаж (на всякий случай — свою фракцию штатно приносит 6713).
	if (AsdaSelf::IsSelfCharId(Net, CharId))   // ★ ИСПРАВЛЕНИЕ 11.10.2026 (дедуп №2, В2-A, N2): «charId мой?» — `AsdaSelf::IsSelfCharId` (свой ещё неизвестен ⇒ не мой; было 0 == 0 ⇒ мой)
	{
		if (AActor* Me = LocalPlayerActor.Get())
		{
			if (UAsdaNameplateComponent* NP = Me->FindComponentByClass<UAsdaNameplateComponent>())
			{
				NP->SetCamp(CampId, Grade);
			}
		}
		return;
	}
	// Сосед по charId.
	for (TPair<int32, FAsdaNetPlayerEntry>& It : NetPlayers)
	{
		if (It.Value.CharId != CharId) { continue; }
		if (AAsdaPlayerCharacter* A = It.Value.Actor.Get())
		{
			if (UAsdaNameplateComponent* NP = A->FindComponentByClass<UAsdaNameplateComponent>())
			{
				NP->SetCamp(CampId, Grade);
			}
		}
		return;
	}
	// Актора ещё нет — применится при спавне из FactionByCharId (эталон держит запись в списке).
}

// ── ★ ПИТОМЦЫ (6120) — pet_system_re.md ──

// Спавн/замена/удаление питомца у владельца. PetTypeId < 0 → убрать.
void UAsdaWorldSubsystem::ApplyPetToActor(AActor* Owner, int32 PetTypeId, const FString& Name)
{
	if (!Owner) { return; }
	UWorld* W = GetGameInstance()->GetWorld();
	if (!W) { return; }
	// Найти charId владельца (ключ реестра питомцев).
	int32 OwnerCharId = 0;
	if (UAsdaNetSubsystem* Net = GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>())
	{
		if (Owner == LocalPlayerActor.Get()) { OwnerCharId = (int32)Net->SelectedCharId; }
	}
	if (OwnerCharId == 0)
	{
		for (const TPair<int32, FAsdaNetPlayerEntry>& It : NetPlayers)
		{
			if (It.Value.Actor.Get() == Owner) { OwnerCharId = It.Value.CharId; break; }
		}
	}
	// Старого питомца убираем всегда (смена типа = пересоздание).
	if (TWeakObjectPtr<AAsdaPetActor>* Ex = PetActors.Find(OwnerCharId))
	{
		if (AAsdaPetActor* Old = Ex->Get()) { Old->Destroy(); }
		PetActors.Remove(OwnerCharId);
	}
	if (PetTypeId < 0) { return; }   // питомец отозван

	FActorSpawnParameters SP;
	SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SP.Owner = Owner;
	AAsdaPetActor* Pet = W->SpawnActor<AAsdaPetActor>(AAsdaPetActor::StaticClass(),
		Owner->GetActorLocation(), FRotator::ZeroRotator, SP);
	if (!Pet) { return; }
	HoldSpawnTables(EAsdaSpawnTables::Pet);   // ★ 15.09 (dt_gc_hold): DT_PetModels — сильно у мира
	Pet->InitPet(PetTypeId, Name, Owner);
	if (OwnerCharId != 0) { PetActors.Add(OwnerCharId, Pet); }
}

// Применить одну запись очереди. Возвращает true, если владелец найден (запись отработана).
bool UAsdaWorldSubsystem::ApplyPeerPetEntry(int32 CharId, int32 PetTypeId, const FString& Name)
{
	UAsdaNetSubsystem* Net = GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>();
	// Свой персонаж: эталонный `CharManager_FindByCharId` находит и локального игрока тоже.
	if (AsdaSelf::IsSelfCharId(Net, CharId) && IsValid(LocalPlayerActor))   // ★ ИСПРАВЛЕНИЕ 11.10.2026 (дедуп №2, В2-A, N2): «charId мой?» — `AsdaSelf::IsSelfCharId` (свой ещё неизвестен ⇒ не мой; было 0 == 0 ⇒ мой)
	{
		ApplyPetToActor(LocalPlayerActor.Get(), PetTypeId, Name);
		return true;
	}
	// Сосед по charId.
	for (TPair<int32, FAsdaNetPlayerEntry>& It : NetPlayers)
	{
		if (It.Value.CharId != CharId) { continue; }
		if (!It.Value.Actor.IsValid()) { return false; }   // актор уже умер — ждём, как эталон
		ApplyPetToActor(It.Value.Actor.Get(), PetTypeId, Name);
		return true;
	}
	return false;   // владельца ещё нет — запись остаётся в очереди (эталон :48610-48611)
}

// Свой призванный питомец из списка 6106, если 6120 его не поставил (разбор и эталон — у объявления в `.h`).
void UAsdaWorldSubsystem::RestoreSelfPetFromList(const TCHAR* Why)
{
	if (CVarPetSelfRestore.GetValueOnGameThread() == 0) { return; }
	AActor* Me = LocalPlayerActor.Get();
	UGameInstance* GI = GetGameInstance();
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	const UAsdaPetSubsystem* PS = GI ? GI->GetSubsystem<UAsdaPetSubsystem>() : nullptr;
	if (!IsValid(Me) || !Net || !PS) { return; }
	const int32 SelfId = (int32)Net->SelectedCharId;
	if (SelfId == 0) { return; }
	const FAsdaPetSlot* Act = PS->GetActivePet();
	if (!Act || Act->PetIndex < 0) { return; }   // никто не призван — убирать не наше дело (уберёт 6120 −1)
	for (const TTuple<int32, int32, FString>& E : PeerPetQueue)
	{
		if (E.Get<0>() == SelfId) { return; }   // свой 6120 уже ждёт слива — он главнее списка
	}
	if (const TWeakObjectPtr<AAsdaPetActor>* Ex = PetActors.Find(SelfId))
	{
		if (const AAsdaPetActor* P = Ex->Get())
		{
			if (P->PetTypeId == Act->PetIndex) { return; }   // уже стоит — не пересоздаём (мигание)
		}
	}
	ApplyPetToActor(Me, Act->PetIndex, Act->Name);
	UE_LOG(LogAsdaWorld, Log, TEXT("[питомцы] свой питомец восстановлен из списка 6106 (%s): uid=%d порода=%d «%s» — 6120 своего не было"),
		Why, Act->UniqueId, Act->PetIndex, *Act->Name);
}

// Слив очереди 6120 — порт `@0xDCA6B0` (entity_char.c:48549-48613). Период таймера 2.0 с равен
//   эталонному порогу `> 0x7D0` мс; эталон проверяет порог каждый кадр, то есть срабатывает на
//   первом кадре после 2000 мс — разница в пределах одного кадра.
void UAsdaWorldSubsystem::TickPeerPetQueue()
{
	// ★ 30.10.2026: свой питомец из списка 6106 — ПОСЛЕ слива (свой 6120 из очереди главнее), см. `.h`.
	ON_SCOPE_EXIT { RestoreSelfPetFromList(TEXT("слив очереди")); };
	if (PeerPetQueue.Num() == 0) { return; }
	// ★★★ ВОЛНА 9, ПУНКТ П8-2: ОБХОД ИДЁТ ВПЕРЁД, А НЕ НАЗАД — ИСПРАВЛЕНО РАСХОЖДЕНИЕ.
	//   БЫЛО: `for (i = Num()-1; i >= 0; --i)` — обратный обход.
	//   ПОЧЕМУ ЭТО БЫЛО НЕВЕРНО. Тип контейнера эталона теперь УСТАНОВЛЕН (прежняя запись
	//   «не установлен» закрыта): это `std::list` — доказано тремя методами:
	//     · `sub_92C70E` → `0xDCC4F0` = `insert(end(), v)` = `push_back`, а `sub_93A86D` →
	//       `0xDCC920` трогает РОВНО два указателя узла (`_Next`@+0, `_Prev`@+4), без ротаций
	//       и перекраски ⇒ двусвязный список, а не красно-чёрное дерево (`stl_runtime.c:18365-18384`);
	//     · внутри `_Incsize` лежит литерал `"list<T> too long"` (`stl_runtime.c:18526`) —
	//       у `std::map`/`std::set` там было бы `"map/set<T> too long"`;
	//     · в живом дампе сторож контейнера имеет РОВНО ДВА self-указателя (`_Next`/`_Prev`),
	//       тогда как у `_Tree` MSVC их было бы ТРИ (`_Left`/`_Parent`/`_Right`).
	//   ⇒ ДЕДУПЛИКАЦИИ ПО charId У ЭТАЛОНА НЕТ (приёмник `@0xDCA620` делает безусловный
	//     `push_back`, `entity_char.c:48536-48546`), и наша вставка в конец `TArray` — уже 1:1.
	//   НО ПОРЯДОК СЛИВА РАСХОДИЛСЯ: эталон идёт `begin()` → `end()` вперёд
	//   (`entity_char.c:48579-48610`), удаляя применённую запись. При ДВУХ записях по одному
	//   charId (а они законны — дедупликации нет) эталон применяет их в порядке прихода, и
	//   побеждает НОВАЯ; обратный обход применял их наоборот, и побеждала СТАРАЯ — питомец
	//   соседа после быстрой смены залипал на прежней породе/имени.
	//   ⚠ Индекс при удалении НЕ двигаем — это и есть семантика `list::erase`.
	int32 Applied = 0;
	int32 i = 0;
	while (i < PeerPetQueue.Num())
	{
		const TTuple<int32, int32, FString>& E = PeerPetQueue[i];
		if (ApplyPeerPetEntry(E.Get<0>(), E.Get<1>(), E.Get<2>()))
		{
			PeerPetQueue.RemoveAt(i);   // порядок остальных сохраняется
			++Applied;
		}
		else
		{
			++i;
		}
	}
	if (Applied > 0 || PeerPetQueue.Num() > 0)
	{
		UE_LOG(LogAsdaWorld, Verbose,
			TEXT("[питомцы] слив очереди 6120: применено %d, осталось ждать владельца %d"),
			Applied, PeerPetQueue.Num());
	}
}

void UAsdaWorldSubsystem::OnPeerPet(int32 CharId, int32 PetTypeId, FString Name)
{
	// Кэш «последнее известное состояние» — его читает спавн соседа (`SpawnRemotePlayer`)
	//   и чистит выход игрока из мира (см. чистку кэшей по charId).
	PetByCharId.Add(CharId, TPair<int32, FString>(PetTypeId, Name));
	if (CVarPeerPetQueue.GetValueOnGameThread() == 0)
	{
		// Ключ отката: поведение порта до волны 6 — применить немедленно.
		(void)ApplyPeerPetEntry(CharId, PetTypeId, Name);
		return;
	}
	// ★ 1:1 с эталоном: приёмник ТОЛЬКО кладёт запись в очередь, ничего не применяя
	//   (`CPetData::EnqueuePeerPet@0xDCA620`, entity_char.c:48536-48545).
	//   Порядок прихода сохраняем (эталон тоже добавляет в конец контейнера) — две быстрые
	//   смены питомца применятся в том же порядке одним проходом слива.
	// ★★★ ВОЛНА 9: ТИП КОНТЕЙНЕРА УСТАНОВЛЕН — ЭТО `std::list`, И ДЕДУПЛИКАЦИИ У ЭТАЛОНА НЕТ.
	//   ⚠ ПРЕЖНЯЯ ПОПРАВКА ВОЛНЫ 8 («тел функций в срезе нет, тип НЕ УСТАНОВЛЕН, если это map —
	//   дедупликация БЫЛА БЫ») СНЯТА как ложное «нет»: `sub_92C70E` и `sub_935318` — ПЕРЕХОДНИКИ,
	//   и тела за ними в срезе ЕСТЬ, просто под другими именами
	//   (`excluded_thunks.tsv:10518` → `0xDCC4F0`, `:16861` → `0xB7C290`, оба ведут в `stl_runtime.c`).
	//   Доказано тремя независимыми методами:
	//     1) `sub_92C70E` → `insert(end(), v)` = `push_back`; `_Insert` (`stl_runtime.c:18365-18384`)
	//        трогает РОВНО два указателя узла (`_Next`@+0, `_Prev`@+4) — без ротаций и перекраски,
	//        то есть двусвязный список, а не красно-чёрное дерево;
	//     2) внутри `_Incsize` (`stlListIncSize_CFD0 @0xDCCFD0`, объявление `stl_runtime.c:18514`)
	//        лежит литерал `"list<T> too long"` — `stl_runtime.c:18526` —
	//        у `std::map`/`std::set` там было бы `"map/set<T> too long"`;
	//     3) в живом дампе сторож контейнера имеет РОВНО ДВА self-указателя, у `_Tree` MSVC
	//        их было бы ТРИ (`_Left`/`_Parent`/`_Right`).
	//   Узел 32 б, элемент 22 б — сходится с `memcpy(..., 0x16u)` приёмника и с раскладкой 6120.
	//   ⇒ приёмник эталона делает БЕЗУСЛОВНЫЙ `push_back`, дубликаты по charId у него ЗАКОННЫ,
	//     и наш `TArray::Add` в конец — уже 1:1. Пересматривать это место больше не нужно.
	PeerPetQueue.Add(TTuple<int32, int32, FString>(CharId, PetTypeId, Name));
	// ★ УТЕЧКА ОРИГИНАЛА, ПРИВОДИМАЯ КАК ЕСТЬ. Записи, чей владелец так и не появился (игрок
	//   ушёл из AoI, не дойдя до спавна), у эталона из очереди НЕ уходят никогда: слив удаляет
	//   запись только по факту применения (entity_char.c:48606-48611), а чистки по выходу
	//   игрока в приёмнике нет. Дедупликации у эталона НЕТ — установлено волной 9 (см. выше).
	//   Повторяем 1:1; у нас очередь дополнительно уходит на полном сбросе сессии. Чтобы утечка
	//   не осталась незамеченной, шумим в лог на пороге — выдумывать чистку не будем.
	if (PeerPetQueue.Num() == 256)
	{
		UE_LOG(LogAsdaWorld, Warning,
			TEXT("[питомцы] очередь 6120 доросла до 256 записей — это утечка ОРИГИНАЛА ")
			TEXT("(владелец так и не появился, эталон такие записи не чистит). Лечить только на сервере."));
	}
}

// ★ N12 (дедуп №2, В3, 12.10.2026): ИНДЕКС вместо линейного перебора всей таблицы на каждый вызов (A12 аудита №1).
//   Семантика прежняя: берётся ПЕРВАЯ в порядке строк таблицы строка с SubType 15 и этим кодом (дубли кода — первая), её
//   модель/набор/высота отдаются как есть, возврат — «модель не пустая»; кода нет — пустые выходы и false. Доказательство —
//   дамп `asda.W3.MountDump` по всем кодам таблицы до/после (отчёт `wave3_S10.md`).
bool UAsdaWorldSubsystem::LookupMount(int32 ItemCode, FString& OutModel, FString& OutAnimSet, float& OutHeight)
{
	OutModel.Reset(); OutAnimSet.Reset(); OutHeight = 0.f;
	const UDataTable* T = AsdaDt::Cached(kSpecialItemlistDT);
	if (!T) { return false; }
	if (MountIndexSource.Get() != T)
	{
		HoldSpawnTables(EAsdaSpawnTables::MountIndex);   // сильное удержание на сессию (таблица уже загружена — без второй загрузки)
		MountIndex.Reset();
		for (const TPair<FName, uint8*>& It : T->GetRowMap())
		{
			const FAsdaSpecialItemlistRow* R = reinterpret_cast<const FAsdaSpecialItemlistRow*>(It.Value);
			if (!R || R->SubType != 15 || MountIndex.Contains(R->Code)) { continue; }   // первая строка кода — как прежний перебор
			FAsdaMountIndexEntry E;
			E.Model   = R->ModelEntry;
			E.AnimSet = R->EffectEntry;
			E.Height  = (float)R->HeightOffset;
			// EffectEntry вида «Ch1_n_sit_0007» → нам нужен только номер набора («0007»).
			int32 Us;
			if (E.AnimSet.FindLastChar('_', Us)) { E.AnimSet = E.AnimSet.Mid(Us + 1); }
			MountIndex.Add(R->Code, MoveTemp(E));
		}
		MountIndexSource = T;
		UE_LOG(LogAsdaWorld, Log, TEXT("[транспорт] индекс DT_special_itemlist построен: строк %d, транспортов (SubType 15) %d"),
			T->GetRowMap().Num(), MountIndex.Num());
	}
	const FAsdaMountIndexEntry* E = MountIndex.Find(ItemCode);
	if (!E) { return false; }
	OutModel   = E->Model;
	OutAnimSet = E->AnimSet;
	OutHeight  = E->Height;
	return !OutModel.IsEmpty();
}

// ★★★ 30.10.2026 [ОРИГИНАЛ] + [ИСПРАВЛЕНИЕ] — ТРАНСПОРТ ПЕРЕЖИВАЕТ ПОРТАЛ (баг владельца: «прошёл портал верхом — на новой карте
//   транспорт скинут и не виден, хотя применён»). Сервер при портале своему клиенту транспорт НЕ присылает: `CharPortalLogonStateRes@0x52D740`
//   (GameServer.c:114557) кладёт 30 премиум-слотов из БД в `CUser+48629` (слот 15 = транспорт), а `CharPortalMake@0x5444A0` шлёт себе
//   только 4003 — соседям же 4005 + 6154 (`Send_MoveSectorInClippingUserInfo`, type-2 записи из того же `CUser+48629`), они видят верхом.
//   Свой клиент оригинала вешает транспорт САМ: портальный загрузчик, шаг 12 (game_main.c:3762), зовёт
//   `CCharacter_StopAllActiveBuffActions@0xA2C480` (entity_char.c:25924) — для записи списка type 2 / id 15 снова
//   `CCharacter_SetParts_RidingSystem(char, node+32)`; вход из выбора (шаг 12, game_main.c:3249) — `charReapplyActiveSkillEffects@0xA2BA10`.
//   У порта павн своего персонажа умирает с картой, а (1) `TrySpawnLocalPlayer` транспорт не применял вовсе, (2) кэш `MountByUserIdx`
//   (номера сессии!) жил через карты и перезаходы, (3) проверка «кэш == код ⇒ уже применён» пропускала НОВЫЙ павн с тем же номером.
//   1 = как оригинал: свой список состояний повторно вешает транспорт на первой своей 4003 (`UAsdaEffectsSubsystem::OnSelfSpawnCarry`),
//   спавн своего павна применяет кэш, повтор отсекается по факту посадки актора, кэш чистится на смене карты и полном сбросе.
//   0 = прежнее поведение порта.
static ASDA_DEV_CVAR(int32, CVarMountPortalReapply,
	TEXT("asda.Mount.PortalReapply"), 1,
	TEXT("1 (по умолчанию) — транспорт своего персонажа вешается заново после портала и перезахода (как CCharacter_StopAllActiveBuffActions ")
	TEXT("оригинала), кэш транспорта по номерам сессии чистится при смене карты; 0 — прежний порт (после портала пешком)."),
	ECVF_Default);
// Значение `asda.Mount.PortalReapply` для других частей (спавн/сброс мира, список состояний).
bool AsdaMountPortalReapplyOn() { return CVarMountPortalReapply.GetValueOnGameThread() != 0; }

// ★★ 04.11.2026 [ОРИГИНАЛ]: снятие предметного усилителя (5454) ссаживает только при коде транспорта (subType 15) — разбор у ветки !bOn.
static ASDA_DEV_CVAR(int32, CVarMountRemoveOnlyMountCode,
	TEXT("asda.Mount.RemoveOnlyMountCode"), 1,
	TEXT("[ОРИГИНАЛ] 1 (по умолчанию) — 5454 ссаживает с транспорта только при коде транспорта (subType 15), как CItem_OnRemoveByTable2Record; ")
	TEXT("0 — прежний порт (любой снятый усилитель, например истёкший бустер скорости, ссаживал)."),
	ECVF_Default);

// ★★★ 06.11.2026 [ОРИГИНАЛ] ПОКА ТРАНСПОРТ ЧИСЛИТСЯ ЗА ИГРОКОМ, ЕГО МОДЕЛЬ НЕ СНИМАЕТСЯ (баг: «выбрал моба верхом, модель пропала, нотис
//   2038 "You cannot attack while riding a vehicle.", атака не идёт, пока не сядешь и не слезешь снова»; живой журнал 03.11 11:21:11).
//   Эталон: `charResetMountAndRebuildModel@0x9EC850` (зовут `CChar_SetState` 5/6/24, смена оружия и щита `SetPartsModelByType` 6/7) зовёт
//   `CCharacter_SetupModelFromItem`, а тот в конце, если в списке есть (15, 2), снова вешает транспорт (entity_char.c:15579, 15646).
//   Снять транспорт по-настоящему может только `CItem_OnRemoveByTable2Record` (5454) — он сперва убирает запись из списка. Свой замах
//   эталон вообще ставит прямой записью состояния без `CChar_SetState` (input.c:1647), поэтому верхом 4026 уходит, сервер отвечает
//   4027 код 8 (`sub_4F2330(…, 15)`, GameServer.c:78374; PvP — 4202 код 8) и всадник остаётся в седле.
//   Порт снимал модель на локальном замахе, на смерти и при смене вещей: сервер держал транспорт, каждая атака получала отказ 8, а
//   игрок видел себя пешим. 1 = правило эталона: замах своего при живом транспорте не стартует, смерть и смена вещей модель не снимают,
//   гейт навыков верхом смотрит на то же состояние; 0 = прежнее.
static ASDA_DEV_CVAR(int32, CVarMountListHoldsModel,
	TEXT("asda.Mount.ListHoldsModel"), 1,
	TEXT("[ОРИГИНАЛ 06.11.2026] 1 = пока транспорт числится за игроком (до 5454), локальные замах/смерть/смена вещей модель не снимают; ")
	TEXT("0 = прежнее (модель снималась локально, сервер оставался верхом и отказывал атаке кодом 8)."),
	ECVF_Default);

// ★ 07.11.2026 [ОРИГИНАЛ]: у своего персонажа транспорт числится по списку состояний (15, 2), как у эталона, а не по кэшу мира.
//   Нажатие «слезть» снимает запись сразу, вместе с 5455 (`charRemoveBuffBySkillId`, item_inventory.c:3334), поэтому гейт навыка
//   (skill_buff.c:31555) и замах в окне до прихода 5454 уже свободны. По кэшу порт ещё 100–400 мс отказывал нотисом 2038 и терял нажатие
//   (живые журналы: 5 случаев). 0 = прежнее (кэш мира, для своего тоже).
static ASDA_DEV_CVAR(int32, CVarMountSelfListGate,
	TEXT("asda.Mount.SelfListGate"), 1,
	TEXT("[ОРИГИНАЛ 07.11.2026] 1 = свой транспорт для гейтов — запись (15, 2) списка состояний, она снимается вместе с 5455; ")
	TEXT("0 = прежнее (кэш мира до прихода 5454, навык сразу после «слезть» получал отказ 2038)."),
	ECVF_Default);

// Доступ к ключу из гейта навыков (ключ `asda.Mount.ListHoldsModel` 0 — прежний гейт по модели).
bool AsdaMountSelfListGateOn() { return CVarMountSelfListGate.GetValueOnGameThread() != 0 && CVarMountListHoldsModel.GetValueOnGameThread() != 0; }

bool AAsdaPlayerCharacter::IsMountHeldByServer() const
{
	if (CVarMountListHoldsModel.GetValueOnGameThread() == 0) { return false; }
	UGameInstance* GI = GetGameInstance();
	const UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
	if (!WS) { return false; }
	const int32 Idx = WS->FindUserIdxByActor(this);
	if (Idx < 0) { return false; }
	if (IsLocalPlayerCharacter() && CVarMountSelfListGate.GetValueOnGameThread() != 0)
	{
		const UAsdaEffectsSubsystem* Fx = GI->GetSubsystem<UAsdaEffectsSubsystem>();
		return Fx && Fx->FindByTypeSub(Idx, 15, 2) != nullptr;
	}
	return WS->HasServerMount(Idx);
}

void UAsdaWorldSubsystem::OnMountState(int32 UserIdx, int32 ItemCode, bool bOn)
{
	AAsdaPlayerCharacter* Ch = Cast<AAsdaPlayerCharacter>(ActorByUserIdx(UserIdx));
	if (!bOn)
	{
		if (!MountByUserIdx.Contains(UserIdx)) { return; }   // и так не на транспорте
		// ★★ 04.11.2026 [ОРИГИНАЛ] (`asda.Mount.RemoveOnlyMountCode`): 5454 приходит на снятие ЛЮБОГО предметного усилителя, а эталон
		//   `CItem_OnRemoveByTable2Record@0xF264F0` ссаживает только при `subType == 15` (нет записи таблицы — ничего). Порт ссаживал
		//   на любой код: истёк бустер скорости (subType 4) верхом — транспорт пропадал, а сервер оставлял всадника со скоростью
		//   транспорта. Код 0 — источники без кода (снимок 4005/6154 без слота 15) — как прежде.
		if (ItemCode != 0 && CVarMountRemoveOnlyMountCode.GetValueOnGameThread() != 0)
		{
			FString M, A; float H = 0.f;
			if (!LookupMount(ItemCode, M, A, H))
			{
				UE_LOG(LogAsdaWorld, Log, TEXT("[транспорт] 5454 uidx=%d код %d — не транспорт (subType ≠ 15): транспорт остаётся (эталон)"), UserIdx, ItemCode);
				return;
			}
		}
		MountByUserIdx.Remove(UserIdx);
		if (Ch) { Ch->ClearMount(); }
		return;
	}
	// ★ Валидация ДО кеширования: 5453/6154 несут ЛЮБЫЕ премиум-предметы (бустеры и т.п.) —
	//   эталон charBuff_ApplyPMBuffFromPacket зовёт RidingSystem только при rec->SubType==15.
	//   Не-маунт код НЕ должен попадать в MountByUserIdx (иначе затрёт транспорт при спавне).
	FString Model, AnimSet; float Height = 0.f;
	if (!LookupMount(ItemCode, Model, AnimSet, Height))
	{
		UE_LOG(LogAsdaWorld, Log, TEXT("[транспорт] itemCode=%d — не SubType-15 (или нет в таблице), пропуск"), ItemCode);
		return;
	}
	// Идемпотентность: повторные снапшоты (4005+6154 сервер шлёт на стоп/реген) не пересобирают меш.
	//   Инвариант: актор существует && кэш==код ⇒ транспорт уже применён (спавн применяет кэш).
	//   ★ 30.10.2026 [ИСПРАВЛЕНИЕ] (`asda.Mount.PortalReapply`): «применён» — это посаженный актор, а не совпавший кэш: после портала
	//   и перезахода номер сессии тот же, кэш тот же, а павн новый и пеший.
	const bool bActorAlreadyMounted = AsdaMountPortalReapplyOn() ? (Ch && Ch->IsMounted()) : (Ch != nullptr);
	if (bActorAlreadyMounted && MountByUserIdx.FindRef(UserIdx) == ItemCode) { return; }
	MountByUserIdx.Add(UserIdx, ItemCode);
	if (!Ch) { return; }   // актора нет — применится при спавне из MountByUserIdx
	HoldSpawnTables(EAsdaSpawnTables::Mount);   // ★ 15.09 (dt_gc_hold): DT_RideEffect — сильно у мира
	FString RideFx, RideSnd, RideSnd2, RideSnd3;
	if (!LookupRideFx(ItemCode, RideFx, RideSnd, RideSnd2, RideSnd3))
	{
		UE_LOG(LogAsdaWorld, Warning, TEXT("[транспорт] itemCode=%d отсутствует в Ride_effect_table — шлейфа/звука не будет"), ItemCode);
	}
	else
	{
		UE_LOG(LogAsdaWorld, Log, TEXT("[транспорт] Ride_effect_table[%d]: шлейф='%s' звук='%s'"),
			ItemCode, *RideFx, *RideSnd);
	}
	Ch->SetMount(Model, AnimSet, Height, RideFx, RideSnd, RideSnd2, RideSnd3);
}

// ═══════════════════════════════════════════════════════════════════════════════════════════════
// ★★★ ВИЗУАЛЬНЫЙ КОМПАНЬОН «ХРАНИТЕЛЬ» (Soul Guardian / «ангел»), опкод 6156.
//   Полный реверс: Reports/soul_guardian_companion_re.md. Эталонные функции:
//     charHandleSlotKeyPacket@0xEF6D00        → OnSoulGuardIndex    (пакет только кладёт пару в карту)
//     charVisibilityStateRefresh@0xEF72C0     → TickSoulGuardians   (применение, КАЖДЫЙ КАДР)
//     CCharManager_AddOrUpdateObject@0xFA6B90 → RefreshSoulGuardian (создать / пересоздать / убрать)
//     charMgr_RemoveSummonById@0xFA7260       → BeginSoftRemove (мягко) / Destroy (жёстко)
//   ★ Компаньон полагается ОБЕИМ сторонам: сервер шлёт 6156 и про своего игрока, и про каждого
//     соседа при входе в AoI / пересчёте сектора (§2.4) — модель у чужого ТА ЖЕ, полная.
// ═══════════════════════════════════════════════════════════════════════════════════════════════

// ★ [ИСПРАВЛЕНИЕ] 25.10.2026 (ревизия `Reports/wave_1025b_review/REVIEW.md`, 3.1) — ОДИН ИСТОЧНИК ГАЛОК «ПОКАЗЫВАТЬ ХРАНИТЕЛЕЙ».
//   Было два хранилища: поля профиля окна настроек (`InterfaceOther[3]/[4]`) и [FxConfig] ShowSelfAngel/ShowOtherAngel здесь. Окно
//   настроек на КАЖДОМ новом мире гнало профиль в `SetShowAngel`, а тот писал ini и делал `Flush` — значение `asda.SoulFx.Show*`
//   затиралось на следующей карте, а файл переписывался на каждой загрузке карты. Теперь источник один — это значение (флаги
//   `bShowSelfAngel/bShowOtherAngel`, на диске [FxConfig], как `./system/FxConfig.ini` эталона): оно читается на первом тике
//   (`TickSoulGuardians`), окно настроек берёт его отсюда и зовёт `SetShowAngel` только при смене, а ini пишется только при смене
//   значения. Поля профиля [3]/[4] остаются зеркалом раскладки массива и для показа не читаются.
//   0 — прежнее (накат профиля на каждом мире, запись ini на каждый вызов).
static ASDA_DEV_CVAR(int32, CVarSoulFxSingleSource,
	TEXT("asda.SoulFx.SingleSource"), 1,
	TEXT("[ИСПРАВЛЕНИЕ 25.10.2026] Галки «показывать моего/чужих хранителей»: 1 = один источник ([FxConfig] подсистемы мира), запись "
	     "ini только при смене; 0 = прежнее (профиль окна настроек накатывается на каждом новом мире, ini пишется каждый раз)."),
	ECVF_Default);

// Аксессор для окна настроек (чужой ключ — функцией, не поиском по имени).
bool AsdaSoulFxSingleSourceOn() { return CVarSoulFxSingleSource.GetValueOnGameThread() != 0; }

void UAsdaWorldSubsystem::LoadAngelOptions()
{
	if (bAngelOptionsLoaded) { return; }
	bAngelOptionsLoaded = true;
	// Эталон читает ./system/FxConfig.ini секция [FxConfig], ключи ShowSelfAngel / ShowOtherAngel.
	//   У нас та же секция и те же ключи, но в GGameUserSettingsIni (канон настроек порта).
	// ★ Дефект оригинала (§2.5) НЕ воспроизводим: там значение "0" ВКЛЮЧАЛО показ (сравнение шло с
	//   "true" ИЛИ "0"), а выключала только строка "FALSE". У нас честный bool; дефолт true —
	//   это и есть фактическое поведение оригинала при отсутствии ключа.
	if (GConfig)
	{
		GConfig->GetBool(TEXT("FxConfig"), TEXT("ShowSelfAngel"),  bShowSelfAngel,  GGameUserSettingsIni);
		GConfig->GetBool(TEXT("FxConfig"), TEXT("ShowOtherAngel"), bShowOtherAngel, GGameUserSettingsIni);
	}
}

void UAsdaWorldSubsystem::SetShowAngel(bool bSelf, bool bOn)
{
	LoadAngelOptions();
	bool& Flag = bSelf ? bShowSelfAngel : bShowOtherAngel;
	// ★ Ревизия 3.1 (ключ `asda.SoulFx.SingleSource`): то же значение — ни записи ini, ни `Flush`.
	if (AsdaSoulFxSingleSourceOn() && Flag == bOn) { return; }
	Flag = bOn;
	if (GConfig)
	{
		GConfig->SetBool(TEXT("FxConfig"), bSelf ? TEXT("ShowSelfAngel") : TEXT("ShowOtherAngel"),
			bOn, GGameUserSettingsIni);
		GConfig->Flush(false, GGameUserSettingsIni);
	}
	// Эталон дублирует гейт в UpdateList_ModelAnim: опция выключена → char[0x4F8] = −1, и на следующем
	//   кадре срабатывает МЯГКОЕ удаление. У нас то же сделает пер-кадровое доведение ниже.
	UE_LOG(LogAsdaWorld, Log, TEXT("[хранитель-3D] показ %s: %s"),
		bSelf ? TEXT("СВОЕГО") : TEXT("ЧУЖИХ"), bOn ? TEXT("вкл") : TEXT("выкл"));
	TickSoulGuardians();
}

// `charHandleSlotKeyPacket@0xEF6D00`: пакет НИЧЕГО не применяет — только кладёт пару в карту.
//   ★ ЗАКОН no-reentrant-net-callbacks: из колбэка пакета актора НЕ спавним — это делает тикер.
// ★★★ КЛЮЧ = charId (`CUser+372`). Обоснование — у делегата `UAsdaNetSubsystem::OnSoulGuardIdx`.
void UAsdaWorldSubsystem::OnSoulGuardIndex(int32 CharId, int32 SoulGuardIdx)
{
	// Источник прислал −1 («адресата нет», консольная эмуляция) → это ЛОКАЛЬНЫЙ игрок:
	//   берём его charId из char-select (эталонный `CharManager_FindByCharId` сверяется с ним же).
	int32 Key = CharId;
	if (Key < 0)
	{
		const UAsdaNetSubsystem* Net = GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>();
		Key = Net ? Net->SelectedCharId : 0;
	}
	const int32* Old = SoulGuardIdxByCharId.Find(Key);
	if (Old && *Old == SoulGuardIdx) { return; }
	SoulGuardIdxByCharId.Add(Key, SoulGuardIdx);
	UE_LOG(LogAsdaWorld, Log, TEXT("[хранитель-3D] 6156: charId=%d SoulGuardIndex=%d"), Key, SoulGuardIdx);
}

// Владелец по ключу карты 6156. Живой сервер шлёт charId ⇒ эталонный путь — FindPlayerActorByCharId.
//   Фолбэк «ключ = userIdx» нужен консольным эмуляциям (`asda.SoulFx.Peer <userIdx> <idx>`) и старым
//   вызовам `HasSoulGuardian(SelfUserIdx())`; на живых данных он не срабатывает, т.к. первый уже нашёл.
AActor* UAsdaWorldSubsystem::OwnerByGuardKey(int32 Key) const
{
	if (AActor* A = FindPlayerActorByCharId(Key)) { return A; }
	return ActorByUserIdx(Key);
}

AAsdaSoulGuardianActor* UAsdaWorldSubsystem::FindSoulGuardian(int32 CharId) const
{
	if (const TWeakObjectPtr<AAsdaSoulGuardianActor>* P = SoulGuardians.Find(CharId)) { return P->Get(); }
	// Ключ пришёл в «другом пространстве» (userIdx вместо charId или наоборот) — резолвим через актора.
	if (const AActor* A = OwnerByGuardKey(CharId)) { return FindSoulGuardianByOwner(A); }
	return nullptr;
}

AAsdaSoulGuardianActor* UAsdaWorldSubsystem::FindSoulGuardianByOwner(const AActor* Owner) const
{
	if (!Owner) { return nullptr; }
	for (const TPair<int32, TWeakObjectPtr<AAsdaSoulGuardianActor>>& It : SoulGuardians)
	{
		AAsdaSoulGuardianActor* G = It.Value.Get();
		if (G && G->GetOwner() == Owner) { return G; }
	}
	return nullptr;
}

bool UAsdaWorldSubsystem::HasSoulGuardian(int32 CharId) const
{
	// ★ Эталон проверяет ТОЛЬКО СУЩЕСТВОВАНИЕ актёра в списке менеджера, а не флаг «активен» (+1744):
	//   `ui_core.c:92471` — `if (charMgrFindObjByField756(&g_CharManager_0, userIdx)) Y += 400; else Y += 250;`.
	//   Раньше здесь стояло `&& !G->bRemoving`, из-за чего плашка «SOUL GUARDER» прыгала на 150 юнитов
	//   вниз В МОМЕНТ роспуска, хотя компаньон ещё доигрывает уход и полностью виден.
	return FindSoulGuardian(CharId) != nullptr;
}

// `CCharManager_AddOrUpdateObject@0xFA6B90` + гейты Self/Other из `charVisibilityStateRefresh`.
void UAsdaWorldSubsystem::RefreshSoulGuardian(int32 CharId)
{
	const int32 UserIdx = CharId;   // ключ реестра (см. OwnerByGuardKey): живой сервер даёт charId
	LoadAngelOptions();
	UWorld* W = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	if (!W) { return; }

	AActor* Own = OwnerByGuardKey(UserIdx);
	AAsdaSoulGuardianActor* Cur = FindSoulGuardian(UserIdx);

	// Владельца в мире нет → компаньон уходит ЖЁСТКО (эталон charMgr_RemoveSummonById(..., hard=1)
	//   из entity_char.c:7078 при удалении сущности). Запись карты 6156 при этом СОХРАНЯЕМ —
	//   отступление от эталона (§13 п.8): иначе хранитель соседа не вернётся после ре-спавна в AoI.
	if (!Own)
	{
		if (Cur) { Cur->Destroy(); }
		SoulGuardians.Remove(UserIdx);
		return;
	}

	int32 Idx = SoulGuardIdxByCharId.Contains(UserIdx) ? SoulGuardIdxByCharId.FindRef(UserIdx) : -1;

	// Гейт опций показа (эталон: mgr[0x1FC] для своего, mgr[0x1FD] для чужих; иначе char[0x4F8] = −1).
	const bool bSelf = (Own == LocalPlayerActor.Get());
	if (Idx >= 0 && !(bSelf ? bShowSelfAngel : bShowOtherAngel)) { Idx = -1; }

	// Мёртвый владелец: эталонный UpdateList_ModelAnim при состоянии 6 сам ставит char[0x4F8] = −1,
	//   не дожидаясь 6156 от сервера (§1.3).
	if (const AAsdaPlayerCharacter* PC = Cast<AAsdaPlayerCharacter>(Own))
	{
		if (PC->AnimState == EAsdaPlayerState::Dead) { Idx = -1; }
	}

	if (Idx < 0)
	{
		// Мягкое удаление: клип «off» + гашение (эталон bHard = 0). Уже уходящего не трогаем.
		if (Cur && !Cur->bRemoving) { Cur->BeginSoftRemove(); }
		return;
	}

	if (Cur)
	{
		if (Cur->SoulGuardIndex == Idx && !Cur->bRemoving) { return; }   // всё уже так — ничего не делаем
		// ★ СМЕНА ХРАНИТЕЛЯ (повышение/понижение уровня): эталон уничтожает старого ЖЁСТКО, без
		//   «off»-анимации, и тут же создаёт нового (obj[432] != idx → RemoveSummonById(hard=1)).
		Cur->Destroy();
		SoulGuardians.Remove(UserIdx);
	}

	FActorSpawnParameters SP;
	SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SP.Owner = Own;
	AAsdaSoulGuardianActor* G = W->SpawnActor<AAsdaSoulGuardianActor>(
		AAsdaSoulGuardianActor::StaticClass(), Own->GetActorLocation(), FRotator::ZeroRotator, SP);
	if (!G) { return; }
	HoldSpawnTables(EAsdaSpawnTables::SoulGuardian);   // ★ 15.09 (dt_gc_hold): DT_SkillFx — сильно у мира
	G->InitGuardian(Idx, UserIdx, Own);
	// InitGuardian уничтожает актора, если записи в SoulGuarderTable нет — в реестр такого не кладём.
	if (::IsValid(G))
	{
		SoulGuardians.Add(UserIdx, G);
		// ★★★ [ПОРТ] Хранитель, созданный (или пересозданный сменой уровня по 6156 — сервер шлёт её и
		//   скрытому раз в 15 с распадом монет) ПОКА владелец под GM-скрытием, рождается спрятанным —
		//   иначе первый кадр он висел бы над пустым местом. Дальше видимость ведёт его собственный тик.
		static const FName kGmTag(kAsdaGmHiddenTagName);
		if (Own->Tags.Contains(kGmTag))
		{
			G->SetActorHiddenInGame(true);
			AsdaSetAttachedFxHidden(G, true);
		}
	}
}

// `charVisibilityStateRefresh@0xEF72C0` — пер-кадровое доведение (§2.2/§2.3).
void UAsdaWorldSubsystem::TickSoulGuardians()
{
	// ★ Ревизия 3.1: источник галок читается на первом тике — окно настроек и гейт ниже видят сохранённое, а не умолчание.
	if (AsdaSoulFxSingleSourceOn()) { LoadAngelOptions(); }
	DrainPendingGuardSkills();   // ★ до раннего выхода: заявки приходят и когда карта ещё пуста
	if (SoulGuardIdxByCharId.Num() == 0 && SoulGuardians.Num() == 0) { return; }
	// Протухшие записи реестра (актор уничтожился сам — доигранный фейд, смена карты).
	for (auto It = SoulGuardians.CreateIterator(); It; ++It)
	{
		if (!It->Value.IsValid()) { It.RemoveCurrent(); }
	}
	TArray<int32> Keys;
	SoulGuardIdxByCharId.GetKeys(Keys);
	for (int32 Uid : Keys) { RefreshSoulGuardian(Uid); }
	// Компаньоны, чьих владельцев уже нет в карте 6156 (например, реестры очищены по 4003).
	for (auto It = SoulGuardians.CreateIterator(); It; ++It)
	{
		if (!SoulGuardIdxByCharId.Contains(It->Key))
		{
			if (AAsdaSoulGuardianActor* G = It->Value.Get()) { G->Destroy(); }
			It.RemoveCurrent();
		}
	}
}

void UAsdaWorldSubsystem::NotifySoulGuardianSkill(int32 CharId, int32 Variant)
{
	if (AAsdaSoulGuardianActor* G = FindSoulGuardian(CharId)) { G->NotifyGuardianSkill(Variant); }
}

// ★★★ СЕТЕВОЙ ВХОД скилла хранителя (свой И ЧУЖОЙ). Кастера 6159/6160/6161 адресуют СЕССИОННЫМ
//   userIdx, реестр компаньонов — charId, поэтому резолвим через актора владельца.
//   Эталон приходит к тому же результату иначе: пакет 6159 ставит персонажу состояние действия 24
//   (`Recv_SkillCast` → `CChar_SetState(pChar, …, 24)`, network.c:44938) и запоминает activeId
//   (`recordTable_FindIndexById`), а `UpdateList_ModelAnim` СЛЕДУЮЩИМ КАДРОМ видит смену состояния
//   на 13/14/24/27, читает `SkillCoinTable[activeId] +16` и зовёт `Char_SetMotion(angel, 5/10/11, 1)`
//   (entity_char.c:61304-61332). Состояний скилл-каста у персонажей в порту ещё нет (система скиллов
//   не портирована), поэтому вариант считается там же, где эталон, но применяется напрямую —
//   РЕЗУЛЬТАТ идентичен (тот же мотион, тот же эффект слота 31/32/33, тем же кадром позже).
void UAsdaWorldSubsystem::NotifySoulGuardianSkillByUserIdx(int32 UserIdx, int32 Variant)
{
	if (Variant < 1 || Variant > 3) { return; }
	AActor* A = ActorByUserIdx(UserIdx);
	if (!A)
	{
		UE_LOG(LogAsdaWorld, Verbose, TEXT("[хранитель-3D] скилл: кастер userIdx=%d не найден в мире"), UserIdx);
		return;
	}
	if (AAsdaSoulGuardianActor* G = FindSoulGuardianByOwner(A))
	{
		UE_LOG(LogAsdaWorld, Log, TEXT("[хранитель-3D] боевой мотион: кастер userIdx=%d (%s), вариант %d"),
			UserIdx, *A->GetName(), Variant);
		G->NotifyGuardianSkill(Variant);
	}
	else
	{
		// Эталон в этой ситуации тоже ничего не играет: мотион ставится существующему компаньону,
		//   а его нет, пока сервер не прислал 6156 с индексом >= 0.
		UE_LOG(LogAsdaWorld, Verbose, TEXT("[хранитель-3D] скилл: у кастера userIdx=%d компаньона нет"), UserIdx);
	}
}

// Заявки, снятые с 6159/6160/6161 в колбэке пакета, применяются здесь — вне разбора кадров.
void UAsdaWorldSubsystem::QueueSoulGuardianSkill(int32 UserIdx, int32 Variant)
{
	if (Variant < 1 || Variant > 3) { return; }
	PendingGuardSkill.Emplace(UserIdx, Variant);
}

void UAsdaWorldSubsystem::DrainPendingGuardSkills()
{
	if (PendingGuardSkill.Num() == 0) { return; }
	TArray<TPair<int32, int32>> Batch = MoveTemp(PendingGuardSkill);
	PendingGuardSkill.Reset();
	for (const TPair<int32, int32>& It : Batch) { NotifySoulGuardianSkillByUserIdx(It.Key, It.Value); }
}

FString UAsdaWorldSubsystem::DumpSoulGuardians() const
{
	FString S = TEXT("── ХРАНИТЕЛЬ: 3D-компаньон («ангел») ──\n");
	S += FString::Printf(TEXT("  показ: свой=%s, чужие=%s (FxConfig ShowSelfAngel/ShowOtherAngel)\n"),
		bShowSelfAngel ? TEXT("вкл") : TEXT("ВЫКЛ"), bShowOtherAngel ? TEXT("вкл") : TEXT("ВЫКЛ"));
	S += FString::Printf(TEXT("  карта 6156: %d записей, живых компаньонов: %d\n"),
		SoulGuardIdxByCharId.Num(), SoulGuardians.Num());
	for (const TPair<int32, int32>& It : SoulGuardIdxByCharId)
	{
		const AAsdaSoulGuardianActor* G = FindSoulGuardian(It.Key);
		const AActor* Own = OwnerByGuardKey(It.Key);
		S += FString::Printf(TEXT("  charId=%d индекс=%d владелец=%s компаньон=%s\n"),
			It.Key, It.Value, Own ? *Own->GetName() : TEXT("НЕТ В МИРЕ"), G ? TEXT("есть") : TEXT("нет"));
		if (G) { S += G->DumpLine() + TEXT("\n"); }
	}
	S += FString::Printf(TEXT("  заявок на боевой мотион в очереди: %d\n"), PendingGuardSkill.Num());
	return S;
}

// ★ ТЕСТОВЫЙ КРЮК ДЛЯ ЖИВЫХ ПРОВЕРОК, а не игровая механика.
//   В игре павн своего персонажа спавнит `TrySpawnLocalPlayer` по пакету 4003 (он приносит
//   тайл-позицию в `UAsdaNetSubsystem::SelfTileX/Z`). В headless-PIE сервера нет, павна нет —
//   и всё, что опирается на персонажа (движение, автопуть, расстояния трекера), в живом тесте
//   давало бы ложное «не работает» (закон `false-negatives-method`). Команда кладёт ТУ ЖЕ
//   тайл-позицию в то же поле и зовёт ТОТ ЖЕ спавнер: своей ветки спавна не заводим.
// ★ ПОКАЗ КРЫЛЬЕВ Plus Package — по просьбе владельца. Только локально: серверу ничего не
//   уходит, срок действия пакета не трогается, соседи крылья видят. Со скрытыми крыльями
//   персонаж снова бежит по земле — возвращаются следы и звук шагов.
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs CmdAsdaWingsHide(
	TEXT("asda.Wings.Hide"),
	TEXT("asda.Wings.Hide <0|1> — скрыть (1) или вернуть (0) крылья своего персонажа. Только у себя."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(
		[](const TArray<FString>& Args, UWorld* World)
		{
			UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
			UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
			if (!WS) { return; }
			const bool bHide = (Args.Num() > 0) ? (FCString::Atoi(*Args[0]) != 0) : true;
			WS->SetLocalWingsHidden(bHide);
		}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs CmdAsdaWingsToggle(
	TEXT("asda.Wings.Toggle"),
	TEXT("asda.Wings.Toggle — переключить показ крыльев своего персонажа."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(
		[](const TArray<FString>&, UWorld* World)
		{
			UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
			UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
			if (!WS) { return; }
			WS->SetLocalWingsHidden(!WS->AreLocalWingsHidden());
		}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs CmdAsdaFakeSelfSpawn(
	TEXT("Asda.World.FakeSelfSpawn"),
	TEXT("Asda.World.FakeSelfSpawn <тайлX> <тайлY> [имяПерсонажа] — офлайн-спавн своего павна (эмуляция 4003)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(
		[](const TArray<FString>& Args, UWorld* World)
		{
			// ★ С-Р1 (26.09.2026), офлайн-гейт: эмуляция 4003 зовёт боевой OnSelfSpawn → SendEntryFinalize: при живом соединении ушли бы настоящие 4122/4124/4301, а тайл/userNum живой сессии были бы подменены, реестры мира очищены.
			static const uint16 kGuardOps[] = { 4122, 4124, 4301 };
			if (FAsdaOutgoingGate::RefuseDebugWhenOnline(World, TEXT("Asda.World.FakeSelfSpawn"), 10.f, MakeArrayView(kGuardOps))) { return; }
			UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
			UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
			UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
			if (!WS || !Net || Args.Num() < 2)
			{
				UE_LOG(LogAsdaWorld, Warning, TEXT("[офлайн-спавн] нужны подсистемы и два аргумента"));
				return;
			}
			(void)Net;
			WS->DebugSpawnSelfAt(FCString::Atoi(*Args[0]), FCString::Atoi(*Args[1]),
				Args.Num() > 2 ? Args[2] : FString());
		}));
#endif // ASDA_DEV_CONSOLE

void UAsdaWorldSubsystem::DebugSpawnSelfAt(int32 TileX, int32 TileY, const FString& CharName)
{
	UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net) { UE_LOG(LogAsdaWorld, Warning, TEXT("[офлайн-спавн] нет сетевой подсистемы")); return; }
	Net->SelfTileX = TileX;
	Net->SelfTileZ = TileY;
	// ★★★ ДОБАВЛЕНО 2026-08-30. Настоящий 4003 кладёт НЕ ТОЛЬКО тайл: он же объявляет свой
	//   `userNum` известным (`bSelfUserNumKnown = true`, `AsdaNetSubsystem.cpp:4084`). Без этого
	//   `IsSelfUserIdx` возвращает false ДАЖЕ ДЛЯ СВОЕГО павна, `ActorByUserIdx` не находит его,
	//   и любая живая проверка канала «эффект НА персонаже» даёт ложное «персонажа нет в мире»
	//   (закон `false-negatives-known`… точнее `false-negatives-method`) — именно на этом
	//   споткнулась проверка эффектов званий. Ноль — ЗАКОННЫЙ номер ([[self-usernum-zero-is-valid]]),
	//   поэтому взводим ИМЕННО признак, а не значение. Реальный вход перезапишет его пакетом.
	if (!Net->IsSelfUserNumKnown()) { Net->SetSelfUserNumForDebug(Net->SelfUserNum); }
	// ★ Имя ставим ДО спавна: если список персонажей всё-таки есть (реальный вход), штатный путь
	//   внутри `TrySpawnLocalPlayer` перепишет его настоящим значением — тестовый крюк не перебивает игру.
	if (!CharName.IsEmpty()) { SelfCharName = CharName; }
	OnSelfSpawn();          // ★ ТА ЖЕ точка, что и приход пакета 4003 — своей ветки спавна нет
	UE_LOG(LogAsdaWorld, Log, TEXT("[офлайн-спавн] тайл (%d,%d), имя «%s», павн %s"),
		TileX, TileY, *SelfCharName, IsValid(LocalPlayerActor) ? TEXT("создан") : TEXT("НЕ создан"));
}

// ═══════════ КОНСОЛЬ: ФЛАГИ И ПЛОЩАДКИ ВОЙНЫ ЗА ГОРОДА ═════════════════════════════════════════
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GWarFlags(
	TEXT("asda.War.Flags"),
	TEXT("asda.War.Flags — раскатать текстуры флагов/площадок по текущему владельцу города и "
	     "показать, что получилось (перед этим удобно подать состояние командой asda.War.Fake)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W)
	{
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
		if (!WS) { UE_LOG(LogAsdaWorld, Warning, TEXT("[война] WorldSubsystem недоступен")); return; }
		const int32 Slot = WS->GetCityWarSlotForCurrentMap();
		if (Slot < 0)
		{
			UE_LOG(LogAsdaWorld, Warning,
				TEXT("[война] текущая карта НЕ военная (флаги есть только на 3 Альпия / 0 Силарис / 7 Пламения)"));
			return;
		}
		WS->ApplyCityWarTextures();
	}));
#endif // ASDA_DEV_CONSOLE

// ═══════════════════════════════════════════════════════════════════════════════════════════════════════════════════════
// ★★★ 17.10.2026 СИНХРОНИЗАЦИЯ БОЯ, Ф5 — РЕЖИМ ПОКАЗА ЦИФР УРОНА (поле профиля `DamageTextMode`, разбор у поля в
//   `Public/Resources/AsdaGameUserSettings.h`; ТЗ `Reports/combat_sync_2026-10-17/05_TZ_PVP_FIELD10112.md` §Ф5).
// ═══════════════════════════════════════════════════════════════════════════════════════════════════════════════════════

int32 UAsdaWorldSubsystem::GetDamageTextMode()
{
	const UAsdaGameUserSettings* S = UAsdaGameUserSettings::Get();
	return S ? FMath::Clamp(S->DamageTextMode, 0, 2) : 1;   // профиля нет — умолчание порта «все»
}

bool UAsdaWorldSubsystem::ShouldShowDamageText(bool bConcernsMe)
{
	// [ОРИГИНАЛ] Шаблон всех мест показа: `if (касается меня) показать; else if (Field10112 == 1) показать;` (entity_char.c:848,
	//   :2098; combat_pvp.c:1603, :7998; network.c:37667), а при 2 надпись глушит сам `FloatingText_Spawn` (entity_char.c:3214).
	const int32 M = GetDamageTextMode();
	return M == 1 || (M == 0 && bConcernsMe);
}

int32 UAsdaWorldSubsystem::GetSelectedTargetUserIdx() const
{
	const AAsdaPlayerController* PC = GetGameInstance() ? Cast<AAsdaPlayerController>(GetGameInstance()->GetFirstLocalPlayerController()) : nullptr;
	const AActor* T = PC ? PC->TargetActor.Get() : nullptr;
	return (T && T != LocalPlayerActor.Get() && Cast<AAsdaPlayerCharacter>(T)) ? FindUserIdxByActor(T) : -1;
}

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется (там режим — из профиля)
static FAutoConsoleCommand GAsdaDamageTextModeCmd(
	TEXT("asda.Combat.DamageTextMode"),
	TEXT("[ПОРТ] Режим показа всплывающих цифр урона (Ф5, поле профиля DamageTextMode; оригинал — Field10112): 0 — только касающиеся ")
	TEXT("меня, 1 — все (умолчание порта, запрос владельца 17.10.2026), 2 — не показывать вовсе (строки в чат остаются). Без ")
	TEXT("аргумента — текущее значение; с аргументом — записать в профиль (GameUserSettings.ini) и сохранить."),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& A)
	{
		UAsdaGameUserSettings* S = UAsdaGameUserSettings::Get();
		if (!S) { UE_LOG(LogAsdaWorld, Warning, TEXT("[цифры урона] профиль UAsdaGameUserSettings не создан")); return; }
		if (A.Num() > 0)
		{
			const int32 V = FCString::Atoi(*A[0]);
			if (V < 0 || V > 2) { UE_LOG(LogAsdaWorld, Warning, TEXT("[цифры урона] режим %d вне 0…2 — не записан"), V); return; }
			S->DamageTextMode = V;
			S->SaveSettings();
		}
		UE_LOG(LogAsdaWorld, Log, TEXT("[цифры урона] asda.Combat.DamageTextMode = %d (0 — только мои, 1 — все, 2 — нет)"), S->DamageTextMode);
	}));
#endif
