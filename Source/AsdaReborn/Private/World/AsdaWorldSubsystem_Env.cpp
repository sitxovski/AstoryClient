// Copyright (c) 2026 @sitxovski (Discord)
// AsdaWorldSubsystem_Env.cpp — ★ R3-c 12.11.2026 (разрез AsdaWorldSubsystem.cpp, дословный перенос): окружение: готовность мира, эффекты входа на карту, пачки окружения, динамический моб.
//   Общее для частей — в AsdaWorldSubsystem_Private.h; шапка и реверс — в AsdaWorldSubsystem.cpp.
#include "World/AsdaWorldSubsystem.h"
#include "UI/AsdaEntryWindows.h"   // ★ 22.10.2026: окна при своём 4055/4056 — список эталона вместо CloseAll
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "HAL/IConsoleManager.h"        // ключ `asda.Move.RefusePlank`
#include "Resources/AsdaAssetPaths.h"   // ★ волна 2 п. 7 (14.09.2026): реестр путей и правил имён ассетов (генерируемый)
#include "Resources/AsdaModelAnimSubsystem.h"   // Д2/Д8/Д9: ресурс только через менеджер

#include "AsdaNetSubsystem.h"
#include "AsdaSelf.h"   // ★ «это я?» — единая точка своего номера (E2, 14.09.2026)
#include "World/AsdaLoadManagerSubsystem.h"
#include "World/AsdaPlayerCharacter.h"
#include "UI/AsdaWindowSubsystem.h"
#include "Framework/Application/SlateApplication.h"
#include "Npc/AsdaNpcActor.h"
#include "Npc/AsdaNpcGmOnly.h"   // ★ 09.11.2026: NPC «только для GM_»
#include "Npc/AsdaMobActor.h"
#include "World/AsdaFadeComponent.h"
#include "World/AsdaGroundTrace.h"
#include "World/AsdaCoord.h"                 // ★ Аудит №3 (B1/B2): единый перевод координат и тайловая сетка
#include "World/AsdaPlayerController.h"      // снятие таргета при смерти цели-моба (эталон SetActionState(10))
#include "Materials/MaterialInterface.h"      // материал меша стрелы (у ..._1 ссылка битая)
#include "Resources/AsdaGameUserSettings.h"  // ★ 17.10.2026 (Ф5): режим показа цифр урона `DamageTextMode`
#include "Skill/AsdaSkillFxSubsystem.h"      // ★★★ стык «урон ↔ визуал»: момент удара навыка
#include "World/AsdaFxLightSettings.h"         // ★ волна 3 п.16 (15.09.2026): AsdaWeaponGlow — имя свечения заточки (одна таблица баз)
// тип оружия атакующего (импакт battk01..04)   ★ R3: комментарий снятого включения "Items/AsdaItemTableSubsystem.h" (файл им не пользовался) — сохранён
#include "UI/AsdaSkillDatabase.h"            // ★ Аудит №3: поля записи навыка — только отсюда
#include "UI/AsdaEffectsSubsystem.h"         // ★ состояния мобов: 4017 бафф-стеки и бафф отражения
#include "AsdaBytes.h"                       // ★ единые читалки сырой записи (DataHex)
#include "Resources/AsdaSoundSubsystem.h"    // звуки ударов (S_c_*normal1, фазовые звуки мобов)
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "ProceduralMeshComponent.h"               // ★ 09.11: площадка города по рельефу
#include "Engine/StaticMesh.h"
#include "TimerManager.h"
#include "World/AsdaNameplateComponent.h"   // над-головные плашки (порт JUI-окон 1:1)
#include "World/AsdaEntityText.h"           // ★ волна 3 п.14: имена/модели/звук-базы мобов и NPC — одна точка
#include "AsdaDataTableCache.h"             // AsdaDt::Cached — объекты таблиц имён для сильного удержания (HoldEntityNameTables)
#include "World/AsdaWorldKind.h"            // ★ F7 (дедуп №2, В3): общие признаки «логин» / «загрузка»
#include "Misc/ConfigCacheIni.h"             // GConfig — опции показа ангела (FxConfig)
#include "AsdaEffectActor.h"                 // эффекты персонажей: крылья/кольца soulmate (follow-кость)
#include "AsdaFxNames.h"                     // ★ дедуп №2 В1-C (F5/F6): теги эффектов персонажа, имена одиночных эффект-BP
#include "ProceduralMeshComponent.h"         // asda.VerifyWeaponGlow: чтение мировых вершин секций эффекта
// loc-нотисы подбора (1011/1012/1030)   ★ R3: комментарий снятого включения "Text/AsdaTextSubsystem.h" (файл им не пользовался) — сохранён
// UAsdaRichText::ApplyFormatted (единая точка текста)   ★ R3: комментарий снятого включения "UI/AsdaMarkup.h" (файл им не пользовался) — сохранён
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/UObjectGlobals.h"
#include "World/AsdaCombatTrace.h"           // ★ 18.10.2026: журнал боя (ASDA_CTRACE) — полный цикл действий клиента в бою
#include "World/AsdaBlendAnimInstance.h"   // ★ 04.11.2026 (npc_talk_anim): idle NPC при «удочерении» — через AsdaAnimSmooth::PlayClip
#include "AsdaWorldSubsystemInternal.h"   // ★ 23.10.2026 разнос: общее для частей AsdaWorldSubsystem*.cpp (LogAsdaWorld, журнал боя, чистые хелперы)
// ★ 30.10.2026 тип моба → скелетный вариант «меш +0x04 + клипы базы +0x24» (АВТОГЕН `Tools/gen_mob_skill_tables.py`).
#include "AsdaMobClipBase.inl"
#include "AsdaWorldSubsystem_Private.h"   // ★ R3-c 12.11.2026: общее для частей AsdaWorldSubsystem*.cpp

// Ключ варианта клипов базы включён (аксессор — чтобы не искать ключ по имени в рабочем коде).
static bool AsdaMobClipBaseModelOn() { return GAsdaMobClipBaseModel != 0; }

// ★★★ 26.10.2026 «НЕВИДИМЫЕ МОБЫ» (жалоба игроков: Queen's Castle и не только — «моб невидим 10–15 с и бьёт», «стоит один,
//   начинаешь драться — из невидимости выходят ещё несколько»). [ИСПРАВЛЕНИЕ ДЕФЕКТА ОРИГИНАЛА]
//   Причина — шаг записи батча 4013. Сервер кладёт записи вплотную по 173 б, до 3 в пакете (GameServer.c:16471
//   `memcpy(&Src[v15], rec, 0xADu); v15 += 173;`, флаш при v15 > 0x1DD — Send_SectorInNewAddMonsterInfo), а эталон шагает
//   по 174 (network.c:2579 `noticeEntryOff += 174`) и гейтом `*(u32)(rec+27)` отбрасывает записи 2-ю и 3-ю: из каждой пачки
//   в мир попадает только первая. Остальные мобы сектора у клиента НЕ СУЩЕСТВУЮТ, пока сервер не пришлёт 4019 или их удар
//   4015 kind=3 (ветка спауна из пакета удара); погоню 4015 kind=5/7 по незнакомому rid эталон игнорирует — моб бежит и
//   бьёт невидимым. Журналы игроков 21–26.10 (46 сессий): записей 2-й/3-й позиции 12 578 из 18 905 (2/3) не применено;
//   69 из 71 мобов, «созданных из пакета удара», были как раз 2-й/3-й записью пачки; погоня невидимым — до 10 с
//   (castle_of_chess, группы «Arrogant Knight/Pawn/Bishop» по 3 в одной пачке). Сервер присылает всех — его правка не нужна.
//   1 (по умолчанию) — шаг сервера 173: применяются ВСЕ записи пачки. 0 — эталон 1:1 (шаг 174 + гейт, только первая запись;
//   решение «канон» от 02.08.2026).
static ASDA_DEV_CVAR(int32, CVarAsdaMobBatchAllRecords,
	TEXT("asda.Mobs.BatchAllRecords"), 1,
	TEXT("1 (по умолчанию) = батч мобов 4013 читается шагом сервера 173 б — в мир идут ВСЕ записи пачки (до 3; мобы группы видны сразу). ")
	TEXT("0 = эталон 1:1: шаг 174 + гейт @27 — только первая запись пачки, остальные мобы невидимы до 4019 или своего удара 4015 kind=3."),
	ECVF_Default);

// [ОРИГИНАЛ] `Recv_MobSpawn@0xF305E0`: 28 id состояний записи @63 (i16) и глубины @119 (u8) → `skillBuffAdd` для `buff0SubType == 1`.
static void AsdaApplyMobRecordStates(UGameInstance* GI, const TArray<uint8>& Batch, int32 Off, int32 Rid)
{
	if (!GI || Off + 147 > Batch.Num()) { return; }
	TArray<int32> Ids, Levels;
	Ids.Reserve(28); Levels.Reserve(28);
	for (int32 i = 0; i < 28; ++i)
	{
		Ids.Add(AsdaBytes::RdI16(Batch.GetData() + Off + 63 + 2 * i));
		Levels.Add((int32)Batch[Off + 119 + i]);
	}
	AsdaMobAddListedBuffs(GI->GetSubsystem<UAsdaEffectsSubsystem>(), Rid, Ids, Levels, /*bSpawnRecord=*/true);
}

void UAsdaWorldSubsystem::OnWorldReady()
{
	// 4055/4056: игрок «в мире» (CUser+0x141=1). Мир уже показан по 4003 (RevealWorld); здесь — страховка + батчи.
	// ★★★ 22.10.2026: своё 4055/4056 при УЖЕ ПОКАЗАННОМ мире (воскрешение, телепорт, 4056 первого входа после 4003) больше
	//   НЕ сносит все окна: эталон (`GameNet_OnLocalPlayerMapEntry@0xF43390`, `netHandler_EnterWorld@0xF43050`) закрывает
	//   только свой список — `AsdaEntryWindows::ApplySelfEntry` (разбор в `Public/UI/AsdaEntryWindows.h`). Страховка снятия
	//   загрузки (4003 не пришёл — экран загрузки ещё висит) — прежняя, с `CloseAll`. Откат: `asda.UI.EntryWindowsEtalon 0`.
	UGameInstance* GI = GetGameInstance();
	const UAsdaLoadManagerSubsystem* LM = GI ? GI->GetSubsystem<UAsdaLoadManagerSubsystem>() : nullptr;
	const UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	const bool bWorldNotShownYet = LM && LM->IsLoadingScreenVisible();
	if (!AsdaEntryWindows::IsEtalonEnabled() || !Net)
	{
		RevealWorld();   // прежнее поведение (ключ 0)
	}
	else if (bWorldNotShownYet)
	{
		// Запасной показ мира: 4003 не пришёл. Окна, запомненные перед травелом, не тратим — их вернёт показ по 4003.
		RevealWorld(/*bReopenTravelKept*/ false);
	}
	else
	{
		// Мир уже показан: фокус и режим ввода НЕ трогаем (эталон их здесь не сбрасывает) — только пересчёт модального режима
		//   ввода (меню смерти/модалки могли закрыться), затем список окон эталона. ★ Ревизия WASD М-5: если пересчёт снимает
		//   модалку (вьюпорт ещё игнорировал ввод), нажатые клавиши сбрасываются (`asda.UI.ModalFlushKeys`, [ИСПРАВЛЕНИЕ] 22.10).
		if (UAsdaWindowSubsystem* WM = GI->GetSubsystem<UAsdaWindowSubsystem>()) { WM->RefreshModalInputNow(); }
		AsdaEntryWindows::ApplySelfEntry(GI, Net->LastWorldReadyOpcode, Net->bLastWorldReadyDeadArrival, Net->bLastWorldReadyMapChange);
	}
	UE_LOG(LogAsdaWorld, Log, TEXT("4055/4056: ВХОД В МИР ЗАВЕРШЁН"));
	ProcessEnvBatches();
}

// ═══ ★ ЭФФЕКТ ПОЯВЛЕНИЯ/ВОСКРЕШЕНИЯ (spawn_revive_effect.md) ═══
// Эталон-клиент: «столб света» eff_0_char_99_cmchar01-a_00.jeb + S_c_entry.wav.
//   - GameNet_OnLocalPlayerMapEntry@0xF43390 (4056, self-ветка): звук 3D в своей позиции + Effect_SpawnByName
//     (attach=char, mode 6 — эффект живёт у персонажа) — покрывает ВСЕ воскрешения (город на той же карте,
//     на месте, кампвар: сервер CharRegenVillageMove/CharRegenSelfPosition шлёт 4056 self + всем в секторе).
//   - Та же функция, ветка ЧУЖОГО (field_40==0): звук 2D + cmchar01 на нём.
//   - CRenderer_RenderWorldFrame@0x9773B0: первый кадр мира после загрузки (вход в игру, 4055-перезагрузка).
//   Эффект играет и при прибытии МЁРТВЫМ (эталон спавнит его ДО проверки state 200) — не фильтруем.
void UAsdaWorldSubsystem::OnMapEntryFx(int32 UserIdx, bool bDeadArrival)
{
	UAsdaNetSubsystem* Net = GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>();
	if (!Net) { return; }
	const bool bSelf = AsdaSelf::IsSelfUserIdx(Net, UserIdx);   // E2
	// ★ 05.11.2026 [ОРИГИНАЛ] (`asda.Peer.Revive4056`, разбор у ключа в AsdaNetSubsystem.cpp): сосед по 4056 — перенос на тайл и поза
	//   ДО проверки GM-скрытия (эталон скрытием гейтит только столб света и звук, game_main.c:13059-13085).
	int32 PeerTX = -1, PeerTZ = -1; bool bPeerDead = false;
	if (!bSelf && AsdaPeerRevive4056On() && AsdaPeerEntry4056(UserIdx, PeerTX, PeerTZ, bPeerDead))
	{
		if (AAsdaPlayerCharacter* Peer = Cast<AAsdaPlayerCharacter>(ActorByUserIdx(UserIdx)))
		{
			Peer->StopMove();
			Peer->NetSnapHard(AsdaCoord::TileCenterGame(PeerTX, PeerTZ));   // `player_setPositionAndTarget`
			if (bPeerDead) { Peer->EnterDeadPoseInstant(); }                 // @20 == 200: прибыл мёртвым (состояние 29)
			else if (Peer->AnimState == EAsdaPlayerState::Dead) { Peer->SetAnimState(EAsdaPlayerState::Idle); }   // `CChar_SetState(…, 0)`
			UE_LOG(LogAsdaWorld, Log, TEXT("[появление] 4056 соседа userIdx=%d: тайл (%d,%d), %s"), UserIdx, PeerTX, PeerTZ,
				bPeerDead ? TEXT("прибыл мёртвым — поза трупа") : TEXT("воскрешён — поза покоя"));
		}
	}
	// ★★★ GM-СКРЫТИЕ (доводка P39, 27.09.2026). [ОРИГИНАЛ] `GameNet_OnLocalPlayerMapEntry@0xF43390`
	//   (game_main.c:12987 для себя, :13059 для чужого) спавнит столб `cmchar01` ТОЛЬКО при
	//   `(get_o103(ch) & 1) == 0` — то есть не для персонажа со снятым битом показа (GM-скрытие);
	//   сам 4056 бит не трогает — скрытый остаётся скрытым.
	//   • свой скрытый: эталон играет только 3D-звук (без проверки), эффекта нет. Фейд порта тоже
	//     не запускаем — он переписал бы полупрозрачность «своего под hide».
	//   • чужой скрытый: эффекта нет. 2D-звук `S_c_entry` эталон играет БЕЗ проверки — это выдаёт
	//     воскрешение скрытого оператора на слух; [ИСПРАВЛЕНИЕ ДЕФЕКТА ОРИГИНАЛА] не играем.
	//   Для живого сервера с P41 (AsdaPatch) 4056 скрытого соседям вообще не приходит; эта проверка —
	//   для сервера без P41 и для 4056 себе.
	if (bSelf ? Net->IsGmHidden() : IsCharHiddenByGm(UserIdx))
	{
		if (bSelf)
		{
			if (UAsdaSoundSubsystem* Snd = GetGameInstance()->GetSubsystem<UAsdaSoundSubsystem>())
			{
				if (AActor* Me = LocalPlayerActor.Get()) { Snd->PlaySound3D(FName(kAsdaEntrySound), Me->GetActorLocation()); }
			}
		}
		UE_LOG(LogAsdaWorld, Log, TEXT("[появление] 4056 userIdx=%d%s: персонаж под GM-скрытием — столб света%s не играем (эталон)"),
			UserIdx, bSelf ? TEXT(" (свой)") : TEXT(""), bSelf ? TEXT(" и фейд") : TEXT(" и звук"));
		return;
	}
	if (bSelf)
	{
		// Дедуп входа: по 4003-спавну эффект уже сыгран (в эталоне оба триггера — в одном кадре, в порте
		//   4056 приходит спустя секунды). Воскрешение/телепорт случаются много позже — окно не мешает.
		const double NowS = FPlatformTime::Seconds();
		if (NowS - LastSelfEntryFxS < 6.0)
		{
			UE_LOG(LogAsdaWorld, Log, TEXT("[появление] self 4056: эффект уже играл %.1fс назад — дедуп"),
				NowS - LastSelfEntryFxS);
			return;
		}
		PlayEntryFx(LocalPlayerActor.Get(), true);
		return;
	}
	// Чужой игрок: воскрешение/прибытие соседа по сектору.
	AActor* Ch = ActorByUserIdx(UserIdx);
	if (!Ch)
	{
		UE_LOG(LogAsdaWorld, Log, TEXT("[появление] 4056 userIdx=%d%s: актор ещё не в мире — эффект пропущен"),
			UserIdx, bDeadArrival ? TEXT(" (мёртвым)") : TEXT(""));
		return;
	}
	PlayEntryFx(Ch, false);
}

void UAsdaWorldSubsystem::PlayEntryFx(AActor* Ch, bool bSelf)
{
	if (!Ch) { return; }
	if (bSelf) { LastSelfEntryFxS = FPlatformTime::Seconds(); }
	// Эффект тем же путём, что крылья/импакты: BP_Fx_* (AsdaEffectActor) + attach к актору (эталон mode 6 —
	//   эффект следует за персонажем). Без follow-кости: столб стоит у ног.
	// ★ ИСПРАВЛЕНИЕ 11.10.2026 (дедуп №2, В2-C, F2): срок — `NaturalLifeSpan` самого эффекта (эталон отдаёт столб менеджеру
	//   эффектов без хэндла гашения — эффект живёт до конца своих эмиттеров/контейнера 0/2). Было жёсткое 5,0 с; совпадало
	//   с natural (контейнер 4,5 + 0,5) по случайности — снимок `asda.Dedup.W2C.Life`: 5,0 до и после (контроль).
	static const FName kEntryTag(AsdaCharFxTags::kEntry);   // ★ В1-C F6: общий тег
	if (AActor* Fx = SpawnCharFx(kAsdaEntryFxJeb, Ch, FVector::ZeroVector, kEntryTag))   // ★ имя — общая точка (AsdaWorldSubsystem.h)
	{
		if (AAsdaEffectActor* EFx = Cast<AAsdaEffectActor>(Fx)) { EFx->SetLifeSpan(EFx->NaturalLifeSpan()); }
	}
	// ★ ФЕЙД-ИН ПЕРСОНАЖА на тех же триггерах, что и столб света (вход в мир / воскрешение / прибытие
	//   соседа). Эталон: конец загрузки ch4-модели ставит alpha=0 (entity_char.c:21259
	//   objArr_setField7Broadcast(entity,0)), затем char-тик поднимает её по биту 1 флагов +0x1E4:
	//   `if ((flags&1) && cat==23) { f146 += dt; alpha = f146/1500; }` (entity_char.c:56885) —
	//   1500 мс ЛИНЕЙНО, та же машинерия, что у мобов (Recv_MobSpawn `mob[121]=1`). Альфа кроет ВСЕ
	//   меш-компоненты актора (тело+оружие+экип+крылья); поздний экип подхватывает текущую альфу
	//   (AdoptSlots). Компонент переиспользуем — два параллельных фейда свапнули бы MID как «оригинал».
	//   ★ E6 (14.09.2026): ручная копия find-or-create заменена общей точкой — тело `Begin` то же самое.
	UAsdaFadeComponent::Begin(Ch, true, AAsdaMobActor::FadeInSeconds);
	// Звук эталона: self = 3D в своей позиции (Audio_Play3DSound), чужой = 2D (CSoundFlag_PlaySound2D).
	if (UAsdaSoundSubsystem* Snd = GetGameInstance()->GetSubsystem<UAsdaSoundSubsystem>())
	{
		// ★ [ИСПРАВЛЕНИЕ] 30.10.2026 (`asda.Sound.WorldEvents3D`): столб входа ЧУЖОГО — 3D у него (эталон — 2D: воскрешение
		//   любого игрока в секторе звучало в ухо на полной громкости). Свой — 3D у себя, как эталон.
		if (bSelf || UAsdaSoundSubsystem::WorldEvents3DOn()) { Snd->PlaySound3DAs(FName(kAsdaEntrySound), Ch->GetActorLocation(), EAsdaSoundClass::Magic, Ch); }   // ★ имя — общая точка
		else                                                 { Snd->PlaySound2D(FName(kAsdaEntrySound)); }
	}
	UE_LOG(LogAsdaWorld, Log, TEXT("[появление] cmchar01 + S_c_entry: %s (%s)"),
		bSelf ? TEXT("SELF") : *GetNameSafe(Ch), bSelf ? TEXT("вход/воскрешение") : TEXT("сосед прибыл/воскрес"));
}

// ── ★ B5 ADOPTION: сетевые батчи 4067 (NPC) / 4013 (мобы) → запечённые акторы ──
float UAsdaWorldSubsystem::TerrainZ(const FVector2D& UeXY) const
{
	// ★ Высота ЗЕМЛИ (SM_terrain*), НЕ первого хита: спавн по одиночному трейсу сажал мобов на кроны.
	return AsdaGround::GroundZ(GetGameInstance()->GetWorld(), UeXY.X, UeXY.Y, 0.f);
}

AAsdaNpcActor* UAsdaWorldSubsystem::FindFreeNpc(int32 NpcType, int32 TileX, int32 TileZ) const
{
	UWorld* W = GetGameInstance()->GetWorld();
	if (!W) return nullptr;
	AAsdaNpcActor* Best = nullptr; int64 BestD = INT64_MAX;
	for (TActorIterator<AAsdaNpcActor> It(W); It; ++It)
	{
		AAsdaNpcActor* A = *It;
		if (A->NpcId != NpcType || A->bNetSpawned) continue;   // шаблон = запечённый превью-актор
		const int64 Dx = A->TileX - TileX, Dz = A->TileZ - TileZ;
		const int64 D = Dx * Dx + Dz * Dz;
		if (D < BestD) { BestD = D; Best = A; }
	}
	return Best;
}

AAsdaMobActor* UAsdaWorldSubsystem::FindFreeMob(int32 MobType, int32 TileX, int32 TileZ) const
{
	UWorld* W = GetGameInstance()->GetWorld();
	if (!W) return nullptr;
	AAsdaMobActor* Best = nullptr; int64 BestD = INT64_MAX;
	for (TActorIterator<AAsdaMobActor> It(W); It; ++It)
	{
		AAsdaMobActor* A = *It;
		if (A->MobId != MobType || A->bNetSpawned) continue;   // шаблон = запечённый превью-актор
		// Матч по СПАВН-тайлу актора против ТЕКУЩЕГО тайла пакета (моб бродит вокруг спавна).
		const int64 Dx = A->TileX - TileX, Dz = A->TileZ - TileZ;
		const int64 D = Dx * Dx + Dz * Dz;
		if (D < BestD) { BestD = D; Best = A; }
	}
	return Best;
}

void UAsdaWorldSubsystem::ProcessEnvBatches()
{
	UAsdaNetSubsystem* Net = GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>();
	UWorld* W = GetGameInstance()->GetWorld();
	if (!Net || !W) return;
	// До загрузки целевого уровня акторов нет — батчи остаются в Pending (обработаем по OnWorldReady).
	if (AsdaWorldKind::IsLoadingMap(W) || AsdaWorldKind::IsLoginScene(W)) return;   // ★ F7 (дедуп №2, В3): общие признаки

	// ★ N6 (В1 дедупа №2): формула чтения — `AsdaBytes`; здесь только переходник «массив + смещение» (вызовы ниже гейтятся длиной тела). Типы результата — прежние (int32).
	auto RdI16 = [](const TArray<uint8>& B, int32 O) { return AsdaBytes::RdI16(B.GetData() + O); };
	auto RdU16 = [](const TArray<uint8>& B, int32 O) { return (int32)AsdaBytes::RdU16(B.GetData() + O); };
	auto RdI32 = [](const TArray<uint8>& B, int32 O) { return AsdaBytes::RdI32(B.GetData() + O); };

	// ★★★ A2 (аудит №4 `system_dedup_audit_4.md`): ВСЕ ТРИ ДРЕНАЖА — ЧЕРЕЗ `TAsdaNetQueue::Drain`.
	//   Было: цикл прямо по чужому публичному `TArray` подсистемы сети, а `Reset()` этого поля —
	//   через сотни строк (у мобов — через 307). В окне между началом цикла и `Reset()` любой
	//   входящий пакет (`Push`) переаллоцировал массив: у NPC/игроков это инвалидировало итератор,
	//   у мобов — прямые УКАЗАТЕЛИ `&B`, которые складывались в `MobPackets`. Теперь очередь
	//   забирается ЦЕЛИКОМ и ОДНОЙ операцией под замком, дальше работа идёт по СВОЕЙ копии, а
	//   пришедшее во время спавна копится в уже пустой очереди и дренится следующим вызовом.
	TArray<TArray<uint8>> NpcBatches, MobBatches, MobUpdates;
	TArray<FAsdaRemotePlayer> RemotePlayers;
	Net->PendingNpcBatches.Drain(NpcBatches);
	Net->PendingMobBatches.Drain(MobBatches);
	Net->PendingMobUpdates.Drain(MobUpdates);
	Net->PendingRemotePlayers.Drain(RemotePlayers);

	int32 NpcOk = 0, NpcMiss = 0;
	for (const TArray<uint8>& Batch : NpcBatches)
	{
		// Записи 108б (эталон: renderId<0 → игнор; существующий renderId → обновление fade, нам не нужно).
		for (int32 Off = 0; Off + 108 <= Batch.Num(); Off += 108)
		{
			const int32 Rid  = RdI16(Batch, Off + 0);
			if (Rid < 0) continue;
			const int32 Type = RdU16(Batch, Off + 2);
			const int32 Tx   = RdI16(Batch, Off + 8);
			const int32 Tz   = RdI16(Batch, Off + 10);
			// ★ NPC = ADOPTION запечённых (юзер 2026-07-23: клиент-авторитарная позиция — NPC стоят где размещены
			//   в редакторе из .npc, сервер лишь «удочеряет»). Простейшая логика: запечённый скрыт по умолчанию,
			//   сервер прислал rid → находим свободный шаблон типа и просто ПОКАЗЫВАЕМ его (без клона/репозиции).
			if (TWeakObjectPtr<AAsdaNpcActor>* Ex = NetNpcs.Find(Rid)) { if (Ex->IsValid()) { continue; } NetNpcs.Remove(Rid); }
			AAsdaNpcActor* A = FindFreeNpc(Type, Tx, Tz);
			if (!A)
			{
				++NpcMiss;
				UE_LOG(LogAsdaWorld, Warning, TEXT("[net] NPC type=%d tile=(%d,%d) rid=%d: запечённый не найден"), Type, Tx, Tz, Rid);
				continue;
			}
			A->NetRenderId = Rid;
			A->bNetSpawned = true;
			// ★ 09.11.2026 [ПОРТ, решение владельца] NPC «только для GM_» (встроенный список AsdaNpcLists::kGmOnly, без ключа и ini,
			//   разбор — Public/Npc/AsdaNpcGmOnly.h): своему персонажу без префикса GM_ не показываем — актор остаётся скрытым и без
			//   коллизии (не кликается и не заслоняет луч), но в реестре, чтобы исчезновение 4011 и сброс сессии его снимали.
			if (AsdaNpcGmOnly::HiddenForSelf(GetGameInstance(), A->NpcId))
			{
				A->SetActorHiddenInGame(true);
				A->SetActorEnableCollision(false);
				NetNpcs.Add(Rid, A);
				++NpcOk;
				UE_LOG(LogAsdaWorld, Log, TEXT("[net] NPC %d rid=%d — только для GM_, своему персонажу не показан"), A->NpcId, Rid);
				continue;
			}
			A->SetActorHiddenInGame(false);
			A->SetActorEnableCollision(true);   // BeginPlay шаблона коллизию выключил («невидимые бугры»)
			// idle-анимация запечённого NPC (BeginPlay её пропустил, т.к. был скрыт).
			//   ★ 04.11.2026 (npc_talk_anim): через общий инстанс со смешиванием (его ставит AAsdaNpcActor::BeginPlay) — штатный
			//   PlayAnimation вернул бы одиночный режим, и смена состояний разговора снова шла бы рывком. Первый клип — без бленда.
			if (A->IdleAnim && A->Mesh) { AsdaAnimSmooth::PlayClip(A->Mesh, A->IdleAnim, /*bLoop=*/true, /*бленд*/0.f); }
			A->ApplyStaticPickBox();   // ★ 22.10.2026: статичный объект — капсула клика по коробу выделения (кости ∪ модель)
			// ★ Над-головное ИМЯ NPC (постоянно, эталон uiNameplateUpdateType14). Имя — из DataTable DT_NpcNames
			//   по NpcId (русское, из локализованных данных игры), а НЕ запечённое NpcName (могло быть английским).
			//   Фолбэк на NpcName, если в таблице нет строки. Высота считается по габаритам актора (над головой).
			{
				HoldEntityNameTables();   // ★ 15.09: таблицы имён — сильно у мира (как прежний EnsureNameTables в GetNpcName)
				HoldSpawnTables(EAsdaSpawnTables::Npc);   // ★ 15.09 (dt_gc_hold): DT_NpcQuestNpc (высота плашки) — сильно у мира
				FillNameplate(A, AsdaEntityText::NpcDisplayName(A->NpcId, A->NpcName));   // штатная плашка актора (JUI 1:1); ★ волна 3 п.14
			}
			// Фейд-ин появления (~1с).
			// ★ ИСПРАВЛЕНИЕ 11.10.2026 (дедуп №2, В2-C, F1): общая точка `UAsdaFadeComponent::Begin` («найти или создать»).
			//   Было: безусловный `NewObject` — запечённый NPC, показанный повторно поверх ещё идущего фейда, получал ВТОРОЙ
			//   компонент (два фейда принимают MID друг друга за исходник — разбор в AsdaFadeComponent.h).
			UAsdaFadeComponent::Begin(A, /*bIn*/true, 1.0f);
			NetNpcs.Add(Rid, A);
			++NpcOk;
		}
	}
	// ★ A2: `Reset()` чужого поля здесь больше нет — очередь опустошена `Drain`-ом выше.

	int32 MobOk = 0, MobMiss = 0, MobSkip = 0;
	// ★★★ ДВА РАЗНЫХ ПАКЕТА — ДВЕ РАЗНЫЕ СЕМАНТИКИ (эталон, проверено по бинарю):
	//   4013 0xFAD → Net_RecvPacketDispatch_Loop@0x96AD30 (0x96CCA7: `add eax,0AEh`) — БАТЧ, шаг записи
	//        174 б, вызывает Recv_MobSpawn@0xF305E0 (существующий rid → HP+AoI+снап на cur-тайл).
	//   4019 0xFB3 → Recv_MobSpawnLoop@0xF30330 — ОДНА запись, и для ЗАГРУЖЕННОГО моба (entity[116]==23)
	//        позиция НЕ ставится вовсе: `if (entity[116] != 23) playerSetWorldPosition(...)`.
	//        ★ R5-fix3 (13.11.2026, уточнение по декомпилу): [116] (+0x1D0) — `loadSubState`, стадия загрузки ресурсов моба
	//        (23 = загружен; `mobEffect_LoadArchiveStep`, npc_mob_pet.c:8951-9315), а не «категория»: в cur эталон ставит только
	//        ещё не загруженного (невидимого) моба. В порту моб готов сразу при создании — перестановки нет.
	// Порт до правки гнал 4019 через тот же дрен, что 4013, и снапил моба на cur-тайл пакета — сервер
	// (релей AI-сервера sub_425D40) шлёт «реген»-апдейты, у которых cur==dst==одна и та же клетка для
	// РАЗНЫХ мобов → все живые мобы телепортировались в ОДНУ ТОЧКУ (живой лог: rid 1/5/8/10..15 → (103,230)).
	// ★★★ A2 (аудит №4): тела ПО ЗНАЧЕНИЮ, а не указателем на чужой массив. Прежняя запись
	//   `MobPackets.Add({ &B, false })` держала указатели на элементы `Net->PendingMobBatches`
	//   всё время спавна акторов (~300 строк) — переаллокация очереди делала их висячими.
	//   `MoveTemp` из уже задренённых локальных массивов — перекладывание владения без копии байтов.
	struct FMobPacket { TArray<uint8> Data; bool bLoop; };
	TArray<FMobPacket> MobPackets;
	MobPackets.Reserve(MobBatches.Num() + MobUpdates.Num());
	for (TArray<uint8>& B : MobBatches) { MobPackets.Add({ MoveTemp(B), false }); }
	for (TArray<uint8>& B : MobUpdates) { MobPackets.Add({ MoveTemp(B), true  }); }
	for (const FMobPacket& Pk : MobPackets)
	{
		const TArray<uint8>& Batch = Pk.Data;
		if (Pk.bLoop) { ++MobStats.Pkt4019; } else { ++MobStats.Pkt4013; }
		// ★★★ КРУГ 9 — ШАГ ЗАПИСИ БАТЧА 4013 = 174 + ГЕЙТ *(u32)(rec+27). ОТКАТ КРУГА 8.
		//   ЭТАЛОН = СБОРКА, С КОТОРОЙ СДЕЛАН НАШ IDB (AsdaStory.exe 13 344 768 б, imagebase 0x400000) —
		//   юзер играет ИМЕННО ей (подтверждено им лично 2026-08-02). Круг 8 перевёл порт на шаг 173,
		//   ссылаясь на ДРУГИЕ сборки (WoS/арабская, 0x41A4CC = 0xAD) — это НЕ эталон и не арбитр.
		//   Цикл диспетчера эталона (network.c:2579, дизасм 0x96CC6D..0x96CC74 `add eax,0AEh`):
		//       for (off = 9; off < Size-1; off += 174) RecvPacket_MobSpawn(&pktBuf[off]);
		//   Сервер укладывает записи ВПЛОТНУЮ по 173 (Send_SectorInNewAddMonsterInfo, GameServer.c:16443
		//   `memcpy(&Src[v15], rec, 0xADu); v15 += 173;`, флаш при v15>0x1DD ⇒ ≤3 записи в кадре) ⇒
		//   у записи k≥1 гейт *(u32)(rec+27) читает байты 27+k..30+k ИСТИННОЙ записи, а они НУЛЕВЫЕ
		//   (ctor записи sub_429C10@0x429C10: `memset(this+31,0,0x20)`, смещения 28..62 не пишет никто)
		//   ⇒ ЭТАЛОН ПРИМЕНЯЕТ ТОЛЬКО ПЕРВУЮ ЗАПИСЬ КАЖДОЙ ПАЧКИ (~118 из ~351 за минуту). Это КАНОН
		//   (закон full-1to1): применённое кругом 8 «всё по 173» дало у порта ЛОКАЛЬНО БОЛЬШЕ мобов, чем
		//   на экране эталона («волков в одном месте больше», юзер 2026-08-02) — симптом 1 круга 9.
		//   Диагностический парс «что прислал сервер» (по 173) ниже ОСТАВЛЕН — но В МИР идёт только
		//   применённое эталоном (шаг 174 + гейт).
		//   Граница цикла 1:1: off(в теле) < BodyLen (эталон off<Size-1 при off от 9, BodyLen=Size-10);
		//   хвостовая НЕПОЛНАЯ запись эталоном тоже подаётся в хендлер — её гейт читает нули/чужие байты
		//   и отбрасывает; у нас чтение за телом гейтится (O+31>Num ⇒ гейт=0 ⇒ отброс) — поведение то же.
		TArray<int32> RecOffsets;
		// ★ 26.10.2026 (ключ asda.Mobs.BatchAllRecords, разбор у объявления): 1 — шаг сервера 173, только ПОЛНЫЕ записи (хвост
		//   короче 173 б сервер не шлёт; неполный кусок — не запись его раскладки); 0 — эталонный шаг 174 с подачей хвоста (как было).
		const bool bAllRecords = CVarAsdaMobBatchAllRecords.GetValueOnGameThread() != 0;
		if (Pk.bLoop) { if (Batch.Num() >= 169) { RecOffsets.Add(0); } }   // 4019 — ровно ОДНА запись (эталон зовёт хендлер 1 раз)
		else if (bAllRecords) { for (int32 O = 0; O + 173 <= Batch.Num(); O += 173) { RecOffsets.Add(O); } }
		else { for (int32 O = 0; O < Batch.Num(); O += 174) { RecOffsets.Add(O); } }
		// ★ ДОКАЗАТЕЛЬНАЯ ДИАГНОСТИКА «ЧТО РЕАЛЬНО ПРИСЛАЛ СЕРВЕР» (укладка сервера = 173, поведение
		//   не меняет). Применение — ВЫШЕ, эталонным шагом 174 + гейтом (см. RecOffsets).
		if (!Pk.bLoop && Batch.Num() > 173)
		{
			FString Tbl;
			for (int32 O = 0; O + 173 <= Batch.Num(); O += 173)
			{
				Tbl += FString::Printf(TEXT("[%d]rid=%d type=%d st=%d hp=%d cur=(%d,%d) dst=(%d,%d) fl=%d; "),
					O / 173, RdI16(Batch, O + 0), RdU16(Batch, O + 2), (int32)Batch[O + 8], RdI32(Batch, O + 9),
					RdI16(Batch, O + 19), RdI16(Batch, O + 21), RdI16(Batch, O + 23), RdI16(Batch, O + 25),
					RdI32(Batch, O + 27));
			}
			int32 WillApply = 0;
			for (const int32 O : RecOffsets)
			{
				if (O + 31 <= Batch.Num() && RdI32(Batch, O + 27) != 0) { ++WillApply; }
			}
			UE_LOG(LogAsdaWorld, Log, TEXT("[моб-байты] 4013 тело=%d б (сервер уложил %d зап. по 173): %s⇒ %s применит %d"),
				Batch.Num(), Batch.Num() / 173, *Tbl,
				bAllRecords ? TEXT("порт (шаг сервера 173, все записи)") : TEXT("эталон (шаг 174 + гейт +27)"), WillApply);
		}
		for (const int32 Off : RecOffsets)
		{
			// ★ КРУГ 9: гейт ЧИТАЕТСЯ ПЕРВЫМ (эталон Recv_MobSpawn@0xF305E0: `if (*(u32)(pkt+27))` стоит
			//   ДО лукапа rid) — хвостовая неполная запись (Off+31 за телом) эквивалентна гейту 0.
			const int32 En = (Off + 31 <= Batch.Num()) ? RdI32(Batch, Off + 27) : 0;
			const int32 Rid  = (Off + 2  <= Batch.Num()) ? RdI16(Batch, Off + 0) : -1;
			const int32 Type = (Off + 4  <= Batch.Num()) ? RdU16(Batch, Off + 2) : 0;
			const int32 Hp   = (Off + 13 <= Batch.Num()) ? RdI32(Batch, Off + 9) : 0;    // ★ curHp=maxHp при спавне (mob[283]=mob[284]=*(i32)(pkt+9))
			const int32 Spd  = (Off + 15 <= Batch.Num()) ? RdI16(Batch, Off + 13) : 0;   // знаменатель скорости (эталон: speed=100.0/val)
			const int32 Tx   = (Off + 21 <= Batch.Num()) ? RdI16(Batch, Off + 19) : 0;   // cur-тайл
			const int32 Tz   = (Off + 23 <= Batch.Num()) ? RdI16(Batch, Off + 21) : 0;
			const int32 Dx   = (Off + 25 <= Batch.Num()) ? RdI16(Batch, Off + 23) : 0;   // dst-тайл (цель движения)
			const int32 Dz   = (Off + 27 <= Batch.Num()) ? RdI16(Batch, Off + 25) : 0;
			// ★ Поворот из записи применяется ТОЛЬКО при byte@166 != 0 (эталон: `if (*((u8*)pkt+166))
			//   { mob[1747]=1; угол = *(i16*)(pkt+167); ... } else mob[1747]=0;` — иначе поле +167 не читается).
			const bool  bFace = (Off + 169 <= Batch.Num()) && Batch[Off + 166] != 0;
			const int32 Face  = bFace ? RdI16(Batch, Off + 167) : 0;
			// ★ ДИАГНОСТИКА РЕГЕН-КАНАЛА: 4019 (Recv_MobSpawnLoop) — единственная запись, здесь видно ВСЁ,
			//   что реально прислал сервер по возрождению (flags=4 = ветка «сброс на шаблон» GameServer
			//   sub_425D40, spd=0 — поле +13 в шаблоне не заполнено, cur==dst — обе точки из v7[49],v7[50]).
			if (Pk.bLoop)
			{
				UE_LOG(LogAsdaWorld, Log, TEXT("[моб-байты] 4019 тело=%d б: rid=%d type=%d st=%d hp=%d spd=%d cur=(%d,%d) dst=(%d,%d) fl=%d (в списке: %s)"),
					Batch.Num(), Rid, Type, (int32)Batch[Off + 8], Hp, Spd, Tx, Tz, Dx, Dz, En,
					NetMobs.Contains(Rid) ? TEXT("да") : TEXT("НЕТ"));
			}
			++MobStats.RecTotal;
			if (Rid < 0)  { ++MobSkip; ++MobStats.RecBadRid;  continue; }
			// ★★★ ГЕЙТ `*(u32)(pkt+27)` ПРИНАДЛЕЖИТ ТОЛЬКО 4013 (исправлено 2026-08-11, B3 перепроверки).
			//   `Recv_MobSpawn@0xF305E0` (4013) проверяет его ДО лукапа rid — там он накрывает и апдейт,
			//   и создание. А `Recv_MobSpawnLoop@0xF30330` (4019) для НАЙДЕННОГО rid не проверяет
			//   `pkt+27` вовсе (network.c:37044-37076): безусловно пишет `entity[283]/[284] = HP@9`,
			//   обновляет стеки баффов `sub_9325BE(entity, flags@27, buffs@31)` и строит траекторию.
			//   Гейт у 4019 есть ЛИШЬ в фолбэке «rid не найден» — там вызывается RecvPacket_MobSpawn,
			//   то есть тот же Recv_MobSpawn с его собственным гейтом. Порт же гейтил ЛЮБУЮ запись и
			//   молча терял законные HP-апдейты/траектории по существующим мобам.
			const TWeakObjectPtr<AAsdaMobActor>* GateEx = NetMobs.Find(Rid);
			const bool bRidKnown = GateEx && GateEx->IsValid();
			if (!En && !(Pk.bLoop && bRidKnown)) { ++MobSkip; ++MobStats.RecGateOff; continue; }
			// ★ КРУГ 10б (АВТО-AoI): наблюдение серверного AoI — ТОЛЬКО применённые записи (гейт пройден;
			//   у отброшенных гейтом при шаге 174 координаты — сдвинутый мусор, их не считаем).
			NoteAoiObservedTile(Tx, Tz);
			// ★ ОРИГИНАЛ (Recv_MobSpawn@0xF305E0 / Recv_MobSpawnLoop@0xF30330): существующий rid →
			//   HP-рефреш + AoI-таймер (i[147]=10000мс) + траектория к dst-тайлу(+23/+25) со speed=100.0/@+13;
			//   новый rid → ПОЛНОЦЕННЫЙ ДИНАМИЧЕСКИЙ спаун (CMob_Init: модель по type-id, без запечённых).
			const FVector2D CurG = AsdaCoord::TileCenterGame(Tx, Tz);   // ★ Аудит №3 (B2)
			const FVector2D DstG = AsdaCoord::TileCenterGame(Dx, Dz);   // ★ Аудит №3 (B2)
			const float MobSpd = 100.f / (float)FMath::Max(1, Spd);
			if (TWeakObjectPtr<AAsdaMobActor>* Ex = NetMobs.Find(Rid))
			{
				AAsdaMobActor* Known = Ex->Get();
				if (Known)
				{
					++MobStats.Updated;
					// ★★★ РЕГЕН ПРИХОДИТ НА ТУ ЖЕ СУЩНОСТЬ. Эталон: труп из списка сущностей НЕ удаляется
					//   (entityListUpdateFade@0xF78660, entity_char.c:54461 — удаление только по флагу 16
					//   или AoI-фейду 0x100), поэтому Entity_FindByRenderId его НАХОДИТ и Recv_MobSpawn идёт
					//   веткой «rid найден»: HP восстановлено, фейд отменён, НОВАЯ сущность НЕ создаётся.
					//   Прежний порт освобождал rid на смерти → реген уходил в ветку «новый» и сажал моба
					//   на cur-тайл ПАКЕТА, а сервер в реген-пакете (4019 flags=4, GameServer sub_425D40:
					//   `state173+19/21 = state173+23/25 = v7[49],v7[50]`) шлёт всем ОДНУ И ТУ ЖЕ клетку
					//   (живой лог 2026-08-01: 12 регенов rid 4/6/8/9 — все (103,230)) ⇒ «мобы возрождаются
					//   в одной точке». Оживляем на месте трупа — 1:1 с оригиналом.
					// ★★★ КРУГ 5 — ГЛАВНОЕ РАЗЛИЧИЕ ВЕТОК 4013 и 4019 ПО СОСТОЯНИЮ (дизасм, не декомпил):
					//   • 4013 Recv_MobSpawn@0xF305E0: общий хвост 0xF3076B заканчивается таблицей
					//     переходов 0xF309E7 с ВЫЗОВАМИ CCharacter_SetActionState@0x93C0D2
					//     (0xF309F3 / 0xF30AB6 / 0xF30BC1) — состояние снимается ПРЯМО В ХЕНДЛЕРЕ,
					//     плюс безусловный playerSetWorldPosition@0xF30899. Оживление МГНОВЕННОЕ.
					//   • 4019 Recv_MobSpawnLoop@0xF30330: во ВСЁМ теле (0xF30330..0xF304AE) НЕТ НИ ОДНОГО
					//     call 0x93C0D2 — ActionState не трогается вообще. Труп остаётся трупом до
					//     следующего кадра, когда entityListUpdateFade@0xF78660 перестаёт форсировать
					//     state 10 (условие `if (i[284] <= 0)` больше не выполняется — HP уже вернули).
					//   ⇒ у ОРИГИНАЛА реген-пакет 4015 kind=1, приходящий в ТУ ЖЕ миллисекунду (тот же
					//     кадр, живой лог 11:07:37.114 [956] / 11:07:39.612 [36] / 11:07:41.802 [106]),
					//     упирается в пролог Recv_CombatHit@0xF31123 (`GetActionState==10 → return 1`) и
					//     ОТБРАСЫВАЕТСЯ. Порт же оживлял моба прямо в хендлере 4019, гейт переставал
					//     работать, и эталонный сам по себе снап kind=1 утаскивал КАЖДОГО возрождённого
					//     моба в клетку реген-пакета — а сервер шлёт её всем одинаковой ((103,230):
					//     GameServer sub_425D40 берёт её из поля v7[49]/v7[50] реген-записи NpcServer,
					//     и она НЕ совпадает ни с одной точкой спауна alpen.svr — там 387 УНИКАЛЬНЫХ
					//     тайлов и (103,230) среди них нет). Оригинал этих координат просто не видит.
					// ★★★ КРУГ 9 — ОЖИВЛЕНИЕ ТРУПА ПО БАЙТАМ ЭТАЛОНА (исправление отсебятины круга 5):
					//   • 4019 Recv_MobSpawnLoop@0xF30330 труп НЕ ПОДНИМАЕТ НИКОГДА: пишет только
					//     entity[283]/[284]=HP@+9 (без гейта state), позицию мобу не ставит, SetActionState
					//     не зовёт (0 вхождений call 0x93C0D2 в 0xF30330..0xF304AE). Из state 10 сущность
					//     НИКТО не выводит по HP: entityListUpdateFade@0xF78660 умеет только ЗАГОНЯТЬ в 10
					//     (`if (i[284] <= 0) SetActionState(i,10)`), обратной ветки НЕТ. Труп с возвращённым
					//     HP продолжает умирать: тик state 10 (charAnimAdvanceActionTimer@0xF80930) доигрывает
					//     клип → флаг 4 → фейд → флаг 0x10 → charRemoveEntity. Прежний PendingReviveHp
					//     («поднять следующим кадром») — неверная трактовка круга 5, УДАЛЁН.
					//   • 4013 Recv_MobSpawn@0xF305E0 труп ОЖИВЛЯЕТ: общий хвост пишет HP и зовёт
					//     CCharacter_SetActionState (0xF309F3/0xF30AB6/0xF30BC1) — состояние 10 снимается
					//     ПРЯМО В ХЕНДЛЕРЕ, а пер-кадровый форс-в-10 больше не срабатывает (HP>0).
					//     НО: отменяется ТОЛЬКО AoI-фейд 0x100 (`if (mob[121]&0x100) mob[121]=1`);
					//     ПОСМЕРТНЫЙ фейд (флаг 4, взводится по КОНЦУ death-клипа) НЕ ОТМЕНЯЕТСЯ НИЧЕМ —
					//     charUpdateActionState23@0xF844F0 доводит его до флага 0x10 и удаления НЕЗАВИСИМО
					//     от состояния/HP. ⇒ труп, чей клип доигран (фейд начался), ОБРЕЧЁН: 4013 его
					//     снапнет/запишет HP, но сущность исчезнет и будет создана заново СЛЕДУЮЩИМ пакетом.
					if (Known->IsDead() && Hp > 0 && !Pk.bLoop && Known->GetFadeKind() != EAsdaMobFade::Corpse)
					{
						++MobStats.Revives;
						Known->NetRevive(Hp);   // 4013 — мгновенно (SetActionState в хвосте); внутри отменяется только AoI-фейд
						ForgetLethalMark(Rid);  // ★ ревизия 18.10: новая жизнь — метка смертельного удара прошлой смерти недействительна
						UE_LOG(LogAsdaWorld, Log, TEXT("[моб^] 4013 РЕГЕН rid=%d '%s' hp=%d — оживлён НА МЕСТЕ трупа мир=(%.0f,%.0f); тайл пакета=(%d,%d) (4013 снапнет на cur — эталон)"),
							Rid, *Known->MobName, Hp,
							Known->GetActorLocation().X, Known->GetActorLocation().Y, Tx, Tz);
						ASDA_CTRACE_MOB(Rid, EAsdaCT::Corpse, TEXT("4013 %s: ТРУП ОЖИВЛЁН регеном на месте (HP %d)"), *AsdaCtWorld::Mob(Rid, Known), Hp);
					}
					// ★ Живому мобу в AoI-фейде повторная 4013 ОТМЕНЯЕТ фейд (эталон: `if (mob[121] & 0x100)
					//   { mob[121] = 1; ... mob[146] = 0; }` — снимается 0x100, ставится бит 0 = фейд-ин).
					if (!Known->IsDead() && !Pk.bLoop && Known->GetFadeKind() == EAsdaMobFade::Aoi)
					{
						Known->CancelAoiFade();
						UE_LOG(LogAsdaWorld, Log, TEXT("[моб^] 4013 rid=%d '%s' — AoI-фейд ОТМЕНЁН повторной записью (эталон mob[121]&0x100→1, фейд-ин)"),
							Rid, *Known->MobName);
					}
					// ★ AoI-таймер (i[147]=10000мс) взводит ТОЛЬКО 4013 (Recv_MobSpawn); Recv_MobSpawnLoop
					//   (4019) поле [147] не трогает — 1:1.
					if (!Pk.bLoop) { Known->LastNetUpdateS = W->GetTimeSeconds(); }
					// ★ 17.10.2026: при НАШЕМ замахе по этой цели в полёте HP и смерть ждут попадания (разбор у `ArmOurImpactWindow`).
					// ★ Ревизия п.7: 4013 с HP > 0 при отложенной окном смерти — реген/респаун (эталон: хвост `Recv_MobSpawn`
					//   зовёт SetActionState, труп оживает; 4019 моба не оживляет — см. гейт `!Pk.bLoop` у NetRevive выше):
					//   смерть отменяется, HP применяется сразу. Без отложенной смерти — обычное придержание.
					if (Hp > 0 && !Pk.bLoop && CancelDeferredDeathOnRevive(Known, Hp, TEXT("4013")))
					{
						// отменено и применено внутри (строка в журнале — там же)
					}
					else if (Hp > 0)
					{
						// ★ Ревизия 18.10 (ключ asda.Combat.MobMaxHpFixed): повтор 4013/4019 — канал спауна: HP выше табличного max — серверный
						//   максимум (DT_MobStats синхронизирован с сервером; страховка для типов без синхронизации).
						Known->ApplyPacketHpToMax(Hp, Pk.bLoop ? TEXT("4019") : TEXT("4013"), /*bSpawnChan=*/true);
						NoteMobHpFloor(Known, Hp, /*bFrom4029=*/false, Pk.bLoop ? TEXT("4019") : TEXT("4013"));   // ★ 18.10.2026 (пол HP): только опускает
						if (!LedgerHoldServerHp(Known, Hp, Pk.bLoop ? TEXT("4019") : TEXT("4013"), /*bStaleChecked=*/false, /*bAllowNumber=*/true, /*bRegenChan=*/true))
						{
							if (Hp != Known->CurHp)
							{
								ASDA_CTRACE_MOB(Rid, EAsdaCT::HpPlate, TEXT("%s %s: HP=%d — лента не держит, применяется сразу (полоса %d)"),
									Pk.bLoop ? TEXT("4019") : TEXT("4013"), *AsdaCtWorld::Mob(Rid, Known), Hp, Known->CurHp);
							}
							ApplyMobHp(Known, Hp);   // HP из повторной 4013/4019 (+цифра урона по дельте)
						}
					}
					else if (!Known->IsDead() && LedgerDeferDeath(Known, Pk.bLoop ? TEXT("4019 HP=0") : TEXT("4013 HP=0")))
					{
						continue;
					}
					else if (!Known->IsDead())
					{
						// Эталон пишет HP безусловно; ≤0 детектится тиком → смерть (state 10). rid НЕ
						//   освобождаем: труп остаётся сущностью и примет реген (см. выше).
						++MobStats.Deaths;
						ASDA_CTRACE_MOB(Rid, EAsdaCT::Death, TEXT("%s %s: HP=0 — СМЕРТЬ применена сразу (лента не отложила), HP полосы было %d/%d → NetDeath"),
							Pk.bLoop ? TEXT("4019") : TEXT("4013"), *AsdaCtWorld::Mob(Rid, Known), Known->CurHp, Known->MaxHp);
						Known->NetDeath();
						ASDA_CTRACE_END_IF_TARGET(Rid, TEXT("цель умерла (4013/4019 HP=0)"));
						continue;
					}
					if (Pk.bLoop)
					{
						// ★★★ 4019 Recv_MobSpawnLoop, ЗАГРУЖЕННЫЙ моб (entity[116]==23): позиция НЕ ставится
						//   (`if (entity[116] != 23) playerSetWorldPosition(...)` — ветка только для моба, который ещё грузится:
						//   [116] — стадия загрузки `loadSubState`, 23 = загружен; R5-fix3, 13.11.2026);
						//   безусловно задаётся только ТРАЕКТОРИЯ к dst-тайлу (+23/+25) со speed=100.0/@+13
						//   ИЗ ТЕКУЩЕЙ позиции сущности (projInitTrajectoryToTarget), при entity[237]==0
						//   (у нас — не в замахе/хите: гард внутри NetTrajectory).
						//   Именно снап на cur из этого пакета сгонял РАЗНЫХ мобов в одну клетку.
						// ★★★ cur == dst ⇒ «СТОЯТЬ», цель НЕ назначается. Прямое правило эталона из того же
						//   подсистемного кода — Recv_MobSpawn@0xF305E0, ветка состояния 2 (network.c:37231):
						//     if (pkt@19==pkt@23 && pkt@21==pkt@25) { SetActionState(mob,1); setWorldPosition(cur); }
						//     else { setWorldPosition(cur); speed=100.0/pkt@13; projectileInitTrajectory(dst,speed); }
						//   Реген-пакет (flags&4) ВСЕГДА приходит с cur==dst и spd=0: сервер сбрасывает запись на
						//   шаблон (GameServer sub_425D40; ctor записи sub_429C10 обнуляет +13) — т.е. говорит
						//   «моб стоит вот в этой клетке», а НЕ «иди туда».
						// ★ spd<=0 — вырожденная скорость: в бинаре 0xF3046C..0xF30476 стоит
						//   `movsx ecx,[eax+0Dh]; fild [ebp-78h]; fdivr flt_1396D8C(=100.0)` БЕЗ проверки нуля,
						//   т.е. 100.0/0 = +INF, а время пути = dist/скорость = 0. Мотор-нода
						//   (sub_93D973 → 0x11D0420: node[0]=тип 2, node[5..7]=направление, node[3]=скорость,
						//   node[1]=время; effectNodeMotionUpdate@0x11D05F0: смещение = dir*скорость*время)
						//   осмысленного перемещения из пары (INF, 0) не даёт — цель не назначаем.
						//   Без этого КАЖДЫЙ моб, получивший реген-пакет, уходил в одну и ту же клетку и они
						//   сходились в кучу уже ПОСЛЕ оживления (юзер: «также в одной точке спавнятся»).
						// ★ Труп (state 10) траекторией/остановкой не управляют: в эталоне анимацией и
						//   трансформом трупа распоряжается покадровый апдейтер состояния 10, а сам
						//   Recv_MobSpawnLoop мобу позицию не ставит вовсе. КРУГ 9: труп по 4019 НЕ
						//   поднимается вовсе (SetActionState в 0xF30330..0xF304AE отсутствует) — он
						//   доигрывает смерть до удаления, дальше моба пересоздают 4013/4019/4017.
						// ★★★ ГЕЙТ ТРАЕКТОРИИ 4019 — `if (!*((_WORD *)entity + 237))`, то есть «моб НЕ в бою»
						//   (network.c:37065), а НЕ состояния 13/14 (исправлено 2026-08-11, B4 перепроверки).
						//   Прежде порт полагался на внутренний гейт NetTrajectory (`IsMoveGated()` = хит-флинч),
						//   и мобу в боевой стойке/замахе назначал цель там, где оригинал этого не делает.
						//   Гейты вложены: состояния 13/14 сами ставят ent[237]=1 (SetActionState case 0xD/0xE),
						//   поэтому эталонный `!ent[237]` строго шире — внутренний гейт остаётся как есть.
						if (!Known->IsDead() && !Known->IsCombatStance())
						{
							if ((Dx == Tx && Dz == Tz) || Spd <= 0) { Known->NetHalt(); }
							else                                    { Known->NetTrajectory(DstG, MobSpd); }
						}
					}
					else
					{
						// ★★★ 4013 ПОЗИЦИОНИРУЕТ АВТОРИТЕТНО И СУЩЕСТВУЮЩУЮ СУЩНОСТЬ (разрешение противоречия
						//   круга 2 — проверено ДИЗАСМОМ): ветка «rid найден» (0xF30631..0xF30699) заканчивается
						//   `jmp 0xF3076B`, ветка «создан» — `jmp 0xF3076B`, т.е. хвост ОБЩИЙ; в нём по
						//   0xF3086A стоит CMap_GetTileAt(cur@+13h, @+15h), а по 0xF30899 — playerSetWorldPosition.
						//   Никакого порога и никакой развилки по состоянию перед ним нет.
						const FVector WasL = Known->GetActorLocation();
						Known->ClearServerSegment();   // ★ 07.11.2026 [ИСПРАВЛЕНИЕ]: 4013 ставит моба заново, отрезок 4015 устарел
						Known->NetSnapPos(CurG);
						const FVector NowL = Known->GetActorLocation();
						if (!WasL.Equals(NowL, 50.f))
						{
							UE_LOG(LogAsdaWorld, Log, TEXT("[моб→] 4013 rid=%d '%s' снап (эталон, порога нет): (%.0f,%.0f) → (%.0f,%.0f) тайл пакета=(%d,%d)"),
								Rid, *Known->MobName, WasL.X, WasL.Y, NowL.X, NowL.Y, Tx, Tz);
						}
						// ★★★ КРУГ 8 — СЕМАНТИКА СОСТОЯНИЯ 1:1 ПО switch(--state) ЭТАЛОНА (Recv_MobSpawn,
						//   network.c:37216..37289; байт записи +8). Прежде порт для ЛЮБОГО состояния решал
						//   по cur/dst и строил траекторию — эталон строит её ТОЛЬКО в состоянии 2:
						//     st=1 → case 0: SetActionState(mob,1) + повторный снап (моб СТОИТ);
						//     st=2 → case 1: cur==dst ⇒ SetActionState(mob,1); иначе снап + projectileInitTrajectory(dst, 100/spd);
						//     st=3 → case 2: снап + SetActionState(mob,3) — ЗАМАХ, траектории НЕТ;
						//     st=4 → case 3: только снап;
						//     st=5 → case 4 и st=7 → case 6: снап + charSetTerrainCellIndex(dst) +
						//            playerCastDirectionalEffect2 / playerCastDirectionalEffect — это
						//            ТРАЕКТОРИЯ к dst со скоростью 100/spd (см. ниже);
						//     прочее → default: return (ничего сверх снапа).
						switch ((int32)Batch[Off + 8])
						{
							case 1:
								Known->NetHalt();
								break;
							case 2:
								if (Dx == Tx && Dz == Tz)   { Known->NetHalt(); }
								else if (Spd > 0)           { Known->NetTrajectory(DstG, MobSpd); }
								break;
							case 3:
								// ★★★ ИСПРАВЛЕНО 2026-08-11 (B10 перепроверки). Эталон `case 2` (network.c:37256)
								//   делает РОВНО `playerSetWorldPosition(cur); CCharacter_SetActionState(mob, 3);`
								//   — снап уже сделан общим хвостом выше, остаётся только состояние 3.
								//   `CCharacter_SetActionState@0xF876D0` case 3 = `ent[237] = 1; ent[175] = 0.0;`:
								//   ни рыка (звук замаха играет ветка ПАКЕТА 4015 kind=3), ни поворота.
								//   Прежний порт звал NetAttack(0, DstG) с рыком и разворотом, а при cur==dst
								//   вместо состояния 3 ставил NetHalt() — обе отсебятины сняты.
								Known->NetAttack(/*Variant=*/0, DstG, /*AnimRate=*/1.f, /*bRoarAndFace=*/false);
								break;
							// ★★★ ИСПРАВЛЕНО 2026-08-11 (B2 перепроверки) — ТА ЖЕ ОШИБКА, ЧТО ЧИНИЛИ В 4015.
							//   Эталон, `Recv_MobSpawn@0xF305E0` (network.c:37257-37289):
							//     case 4 (байт@8 == 5): playerSetWorldPosition(cur);
							//                           charSetTerrainCellIndex(тайл@23/@25);
							//                           playerCastDirectionalEffect2(mob, dst, 100.0/spd@13);
							//     case 6 (байт@8 == 7): то же, но playerCastDirectionalEffect (ent[120] = 7).
							//   `playerCastDirectionalEffect(2)` — НЕ «эффект каста», а тот же построитель
							//   кривой движения, что и projInitTrajectoryToTarget: он кладёт цель в
							//   ent[132..134], строит effectCurveNodeInit и ставит ent[120] = 5 (бег) / 7
							//   (боевое перемещение). Порт отправлял эти состояния в `default: break;` с
							//   подписью «траектории НЕТ» — моб стоял, пока сервер его вёл.
							//   Скорость СЫРАЯ 100/spd: нормировка по времени отрезка здесь не нужна и не
							//   применяется — позиция только что снапнута на cur, т.е. |dst−pos| = |dst−cur|.
							//   Ни проверки cur==dst, ни гейта состояния в этих ветках у эталона НЕТ.
							case 5:
							case 7:
							{
								const uint8 MoveState = (Batch[Off + 8] == 5) ? 5 : 7;
								Known->RememberServerCell(DstG);
								Known->NetChaseRaw(MoveState, DstG, MobSpd, /*AnimRate=*/1.f);
								UE_LOG(LogAsdaWorld, Log, TEXT("[моб→] 4013 rid=%d '%s' состояние %d: снап на (%d,%d) + БЕГ к (%d,%d) v=%.3f юн/мс (эталон playerCastDirectionalEffect%s)"),
									Rid, *Known->MobName, (int32)Batch[Off + 8], Tx, Tz, Dx, Dz, MobSpd,
									MoveState == 5 ? TEXT("2") : TEXT(""));
								break;
							}
							default:
								break;   // 4/6/прочее — эталон движение не назначает (case 3 = только снап)
						}
					}
					// ★ 06.11.2026 [ОРИГИНАЛ]: повторная 4013 по живущей сущности тоже несёт список (4019 по живущей — нет, `Recv_MobSpawnLoop`).
					if (!Pk.bLoop) { AsdaApplyMobRecordStates(GetGameInstance(), Batch, Off, Rid); }
					continue;
				}
				else { NetMobs.Remove(Rid); DropLedger(Rid); }   // ★ Ф1: лента прежней сущности
			}
			// ★ СОЗДАНИЕ НОВОЙ СУЩНОСТИ (эталон: `mobListSpawnAndLink` + `CMob_Init`). Общий код всех
			//   трёх каналов спауна (4013 / 4019 / 4017 flags&4) — см. CreateNetMob.
			//   ★ Spd<=0 — вырожденная скорость реген-записи (поле +13 в шаблоне не заполнено): цель
			//     движения НЕ назначается (эталон: 100.0/0 = +INF, времени пути 0 — движения нет).
			if (CreateNetMob(Rid, Type, Hp, FIntPoint(Tx, Tz), FIntPoint(Dx, Dz), Face,
			                 (Spd > 0) ? MobSpd : 0.f,
			                 (int32)Batch[Off + 8], Pk.bLoop ? TEXT("4019") : TEXT("4013")))
			{
				++MobOk;
				AsdaApplyMobRecordStates(GetGameInstance(), Batch, Off, Rid);   // ★ 06.11.2026 [ОРИГИНАЛ]: усиления записи новой сущности
			}
			else
			{
				++MobMiss;
			}
		}
	}
	// ★ A2: `Reset()` чужих полей здесь больше нет — обе очереди опустошены `Drain`-ом в начале функции.

	// ★ AoI-деспаун: тик раз в секунду (эталон entityListUpdateFade — пер-кадрово, нам хватит 1с).
	if (!W->GetTimerManager().IsTimerActive(AoiTimer))
	{
		W->GetTimerManager().SetTimer(AoiTimer, this, &UAsdaWorldSubsystem::TickAoiDespawn, 1.0f, true);
	}

	// ★ ВОЛНА 6: слив очереди 6120 — период РОВНО эталонный, 2000 мс (порог `> 0x7D0`,
	//   entity_char.c:48568). Разбор — у объявления `TickPeerPetQueue` в `.h`.
	if (!W->GetTimerManager().IsTimerActive(PeerPetTimer))
	{
		W->GetTimerManager().SetTimer(PeerPetTimer, this,
			&UAsdaWorldSubsystem::TickPeerPetQueue, 2.0f, true);
	}

	// ★ Предупреждение о сломанной вещи: период РОВНО эталонный — 10 с (0x2710 мс).
	if (!W->GetTimerManager().IsTimerActive(BrokenGearTimer))
	{
		W->GetTimerManager().SetTimer(BrokenGearTimer, this,
			&UAsdaWorldSubsystem::TickBrokenGearNotice, 10.0f, true);
	}

	// ── ★ ДРУГИЕ ИГРОКИ: дрен снапшотов 4005/4065 → спавн акторов (fade-in) ──
	int32 PlOk = 0;
	for (const FAsdaRemotePlayer& P : RemotePlayers)   // ★ A2: своя копия, задренённая в начале функции
	{
		if (Net->IsSelfUserIdx(P.UserIdx)) { continue; }
		// ★ ВТОРОЙ ГЕЙТ «это Я» — по charId. Нужен потому, что 4005 может прийти РАНЬШЕ 4003 (тогда
		//   SelfUserNum ещё 0 и фильтр по userIdx не срабатывает): свой персонаж спавнился бы вторым
		//   павном-«соседом» поверх собственного. charId своего чара известен уже с 1017.
		if (Net->SelectedCharId != 0 && P.CharId == Net->SelectedCharId)
		{
			UE_LOG(LogAsdaWorld, Warning,
				TEXT("[net] снапшот userIdx=%d — это СВОЙ персонаж (charId=%d), соседом не спавним"),
				P.UserIdx, P.CharId);
			continue;
		}
		if (FAsdaNetPlayerEntry* Ex = NetPlayers.Find(P.UserIdx))
		{
			// Уже в мире: эталон (GameNet_OnUserSpawn, ветка «найден») сущность НЕ пересоздаёт и позицию
			//   НЕ трогает (движение ведут 4007/4009) — только re-show. Ничего не делаем.
			if (Ex->Actor.IsValid()) { continue; }
			NetPlayers.Remove(P.UserIdx);
		}
		SpawnRemotePlayer(P);
		++PlOk;
	}
	// ★ A2: `Reset()` чужого поля здесь больше нет — очередь опустошена `Drain`-ом в начале функции.

	if (NpcOk + NpcMiss + MobOk + MobMiss + MobSkip + PlOk > 0)
	{
		UE_LOG(LogAsdaWorld, Log, TEXT("[adopt] NPC: связано %d, не найдено %d | МОБЫ: связано %d, не найдено %d, отключённых %d | ИГРОКИ: %d"),
			NpcOk, NpcMiss, MobOk, MobMiss, MobSkip, PlOk);
	}
}

// ── ★ ДИНАМИЧЕСКИЙ СПАУН МОБА (оригинал CMob_Init@0xF83100: модель по type-id из Mob list.BIN +0x24,
//   БЕЗ запечённых акторов; порт: имя модели = DT_MobNames.Model, ассеты = /Models/MobSkel | /Models/Mob) ──
//   ★ Волна 3 п.14: модель по type-id — `AsdaEntityText::MobModel` (бывший `GetMobModelName`, то же тело).
AAsdaMobActor* UAsdaWorldSubsystem::SpawnNetMobDynamic(int32 MobType, const FTransform& Xf)
{
	UWorld* W = GetGameInstance()->GetWorld();
	if (!W) { return nullptr; }
	HoldEntityNameTables();   // ★ 15.09: таблицы имён — сильно у мира (как прежний EnsureNameTables в GetMobModelName)
	HoldSpawnTables(EAsdaSpawnTables::Mob);   // ★ 15.09 (dt_gc_hold): DT_MobStats/DT_MobTextures/DT_MobNameColors — сильно у мира
	const FString Model = AsdaEntityText::MobModel(MobType);
	if (Model.IsEmpty()) { return nullptr; }
	// Пути ассетов — те же, что у запекания (asda_place_mobs.py): скелетал + idle anim_00, иначе статик.
	// Д8: единый путь скелеталки (объектный суффикс «.<leaf>» дописывает менеджер). Д2: загрузка через менеджер.
	static const FString kMobSkelRoot(AsdaAssetPaths::kSkelRootMobSkel);   // ★ волна 2 п. 7: корень и правило — реестр путей
	const FString StatPath = AsdaAssetPaths::MobStaticMeshObject(Model);
	// ★★★ 30.10.2026 [ОРИГИНАЛ] КЛИПЫ БАЗЫ +0x24. Оригинал берёт меш по `Mob list +0x04` (`sub_F83080`), а клипы — по +0x24
	//   (`Effect_BuildJbbSuffixByType@0xF717C0`). У 117 типов (30 пар) база ≠ меш: 28 мешей своих клипов не имеют вовсе — порт
	//   ставил их статикой без анимации (pawn02/bishp02/rook02/knigt02…, в т.ч. 64 типа с навыками). Для них собран скелетный
	//   вариант `<меш>__<база>` (`Tools/asda_build_mob_fixed.py --clipbase` + `asda_import_mob_clipbase.py`); таблица — АВТОГЕН
	//   `AsdaMobClipBase.inl`. Ключ `asda.Mob.ClipBaseModel` (0 — прежний порт: меш +0x04 со своими клипами или статика).
	FString SkelModel = Model;
	if (AsdaMobClipBaseModelOn())
	{
		for (const FAsdaMobClipBaseRow& R : kAsdaMobClipBase) { if (R.MobId == MobType) { SkelModel = R.Variant; break; } }
	}
	USkeletalMesh* Skel = UAsdaModelAnimSubsystem::LoadMeshChecked(UAsdaModelAnimSubsystem::BuildSkelPath(kMobSkelRoot, SkelModel));
	if (!Skel && SkelModel != Model)
	{
		UE_LOG(LogAsdaWorld, Warning, TEXT("[моб] тип %d: варианта клипов базы %s нет — меш %s как прежде"), MobType, *SkelModel, *Model);
		SkelModel = Model;
		Skel = UAsdaModelAnimSubsystem::LoadMeshChecked(UAsdaModelAnimSubsystem::BuildSkelPath(kMobSkelRoot, Model));
	}
	UStaticMesh*   Stat = Skel ? nullptr : LoadObject<UStaticMesh>(nullptr, *StatPath, nullptr, LOAD_NoWarn | LOAD_Quiet);   // статик-меш = не домен ModelAnim-менеджера
	if (!Skel && !Stat) { return nullptr; }

	FActorSpawnParameters SP;
	SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SP.bDeferConstruction = true;   // bNetSpawned ДО BeginPlay (иначе скроется как превью-шаблон)
	AAsdaMobActor* C = W->SpawnActor<AAsdaMobActor>(AAsdaMobActor::StaticClass(), SP);
	if (!C) { return nullptr; }
	C->MobId = MobType;
	C->bNetSpawned = true;
	if (Skel)
	{
		C->Mesh->SetSkeletalMesh(Skel);
		C->IdleAnim = UAsdaModelAnimSubsystem::LoadClipChecked(UAsdaModelAnimSubsystem::BuildSkelAnimPath(kMobSkelRoot, SkelModel, 0));
	}
	else
	{
		C->StaticMeshComp->SetStaticMesh(Stat);
	}
	// ★★★ Масштаб 0.01 для ОБЕИХ ветвей. Прежний комментарий «статик — в см как карты → 1» неверен:
	//   статик-фолбэк мобов импортирован тем же конвертером glTF, что и скелеталки, т.е. в МЕТРАХ.
	//   Замер (castle_of_chess): статик mon_s_0_pawn02 габарит 21244 ед. при scale 1, скелетал
	//   mon_s_0_mosrd01 — 20782 ед. при scale 0.01. Единицы одинаковые ⇒ при scale 1 статик-мобы
	//   выходили в 100 раз больше (1602 актора на 11 картах, жалоба «огромные мобы в редакторе»).
	FTransform FinalXf = Xf;
	FinalXf.SetScale3D(FVector(0.01f));
	C->FinishSpawning(FinalXf);
	return C;
}

// ★ Журнал боя: локальные макросы этого файла не выходят за его пределы (единая сборка склеивает .cpp).
#undef ASDA_CTRACE_END_IF_TARGET
