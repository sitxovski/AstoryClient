// Copyright (c) 2026 @sitxovski (Discord)
// AsdaWorldSubsystem_Hit.cpp — ★ 23.10.2026 часть UAsdaWorldSubsystem, вынесенная из AsdaWorldSubsystem.cpp БАЙТ В БАЙТ
//   (разнос по смысловым группам; доказательство — Tools/asda_split_cpp.py verify, план Reports/split_2026-10-23/AsdaWorldSubsystem.plan.json).
//   Приём попаданий 4015/4017 (OnCombatHit, отражение, оглушение), снаряды, показ результата удара, OnCombatAnim и очередь уронов.
//   #include — те же, что у AsdaWorldSubsystem.cpp, в том же порядке; общее — AsdaWorldSubsystemInternal.h.
#include "World/AsdaWorldSubsystem.h"
#include "World/AsdaCombatChat.h"   // ★ R5: AsdaCombatChat::Line — строка боя в системный чат (одна копия)
#include "UI/AsdaEntryWindows.h"   // ★ 22.10.2026: окна при своём 4055/4056 — список эталона вместо CloseAll
#include "UI/AsdaEscChain.h"      // ★ 25.10.2026: «кто ударил своего» (эталонное netWord54) для веток состояния ESC
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
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
#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaHudSubsystem.h"             // HERO_NOTICE_WIN — только чужой редкий дроп (6131)
#include "UI/AsdaChatSubsystem.h"            // ★ системный чат CHAT_NEW_SYSTEM + оверлеи A/B — все лут-строки
// ★★★ ЕДИНЫЙ маршрутизатор сообщений (категория → окно по таблице)   ★ R3: комментарий снятого включения "UI/AsdaMessageSubsystem.h" (файл им не пользовался) — сохранён
#include "Framework/Application/SlateApplication.h"
#include "Npc/AsdaNpcActor.h"
#include "Npc/AsdaMobActor.h"
#include "World/AsdaFadeComponent.h"
#include "World/AsdaGroundTrace.h"
#include "World/AsdaCoord.h"                 // ★ Аудит №3 (B1/B2): единый перевод координат и тайловая сетка
#include "World/AsdaPlayerController.h"      // снятие таргета при смерти цели-моба (эталон SetActionState(10))
#include "World/AsdaProjectileActor.h"       // ★ снаряды дальнобойного оружия (projectile_re.md)
#include "World/AsdaNockedArrow.h"           // ★ 05.11.2026: жизненный цикл стрелы на тетиве (снятие по событиям эталона)
#include "Materials/MaterialInterface.h"      // материал меша стрелы (у ..._1 ссылка битая)
#include "World/AsdaFloatingTextSubsystem.h" // цифры урона (порт CFloatingText)
#include "Resources/AsdaGameUserSettings.h"  // ★ 17.10.2026 (Ф5): режим показа цифр урона `DamageTextMode`
#include "Skill/AsdaSkillFxSubsystem.h"      // ★★★ стык «урон ↔ визуал»: момент удара навыка
#include "World/AsdaFxLightSettings.h"         // ★ волна 3 п.16 (15.09.2026): AsdaWeaponGlow — имя свечения заточки (одна таблица баз)
// тип оружия атакующего (импакт battk01..04)   ★ R3: комментарий снятого включения "Items/AsdaItemTableSubsystem.h" (файл им не пользовался) — сохранён
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
#include "World/AsdaSoulGuardianActor.h"     // ★ компаньон-хранитель «ангел» (6156, soul_guardian_companion_re.md)
#include "Misc/ConfigCacheIni.h"             // GConfig — опции показа ангела (FxConfig)
#include "AsdaEffectActor.h"                 // эффекты персонажей: крылья/кольца soulmate (follow-кость)
#include "AsdaFxNames.h"                     // ★ дедуп №2 В1-C (F5/F6): теги эффектов персонажа, имена одиночных эффект-BP
#include "Resources/AsdaMaterialNames.h"   // ★ дедуп №2 В1-C (F10/F16): пути мастеров, имена параметров, экранные ассеты
#include "ProceduralMeshComponent.h"         // asda.VerifyWeaponGlow: чтение мировых вершин секций эффекта
#include "Text/AsdaTextSubsystem.h"          // loc-нотисы подбора (1011/1012/1030)
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
// ★ С-Р1: общий офлайн-гейт отладочных команд (RefuseDebugWhenOnline)   ★ R3: комментарий снятого включения "AsdaOutgoingGate.h" (файл им не пользовался) — сохранён
#include "World/AsdaCombatTrace.h"           // ★ 18.10.2026: журнал боя (ASDA_CTRACE) — полный цикл действий клиента в бою
#include "World/AsdaBlendAnimInstance.h"     // ★ 29.10.2026: вздрагивание персонажа от удара моба (AsdaAnimSmooth::StartHitFlinch)
#include "AsdaWorldSubsystemInternal.h"   // ★ 23.10.2026 разнос: общее для частей AsdaWorldSubsystem*.cpp (LogAsdaWorld, журнал боя, чистые хелперы)
#include "AsdaWorldSubsystemHitInternal.h"   // ★ R2: общие помощники и объявления частей AsdaWorldSubsystem_Hit*.cpp (после включений пролога)

// ★ Аварийный ключ на случай спорной трактовки кодов −2/0 (по умолчанию — как в эталоне: MISS).
ASDA_DEV_CVAR(int32, CVarShowMissOnCode2,
	TEXT("asda.Combat.ShowMissOnCode2"), 1,
	TEXT("1 = коды -2 и 0 рисуют «MISS» (эталон); 0 = не рисовать ничего."),
	ECVF_Default);
// ★★★ 18.10.2026 (ревизия Ф4, ВАЖНО 1–2; `Reports/combat_sync_2026-10-17/09_REVIEW_F4.md`) — ГРАНИЦЫ PvP-СЕРИИ.
//   [ОРИГИНАЛ] у персонажа ОДНА очередь ударов: 4027 case 1 и 4202 её сбрасывают (network.c:40916, 41224), покой `CChar_SetState(…, 0)`
//   — тоже (entity_char.c:19301–19305); значение снимает тик ТЕКУЩЕГО замаха по ТЕКУЩЕЙ цели. Порт держит PvP-серию отдельно от
//   очереди моба (`DmgBuffers`), и без границ её остатки доживали до замаха атакующего по мобу: тик PvP снимал их на кадрах ЛЮБОГО
//   замаха — цифры над прежней жертвой и вычет её HP; поздний свой 4204 после конца атаки заводил новую серию. Ключ включает:
//   • PvP-серия атакующего снимается его боем с мобом (принятый 4027/4029, отказ 4027/4029 — `ResetPvpSeriesOnMobFight`);
//   • тик PvP снимает значения только на кадрах замаха по цели серии (свой — идущая PvP-атака по этой цели; сосед — цель разворота);
//   • свой 4204 код 0 принимается только во время своей PvP-атаки по той же цели, иначе выбрасывается с записью в лог.
//   0 = как до 18.10.2026 (дефекты видны: остатки серии цифрами над прежней жертвой во время боя с мобом, поздний 4204 — серия).
// ★★★ 25.10.2026 [ИСПРАВЛЕНИЕ] БАГ ИГРОКОВ «ФАНТОМНЫЙ УРОН» (отчёт `Reports/phantom_damage/REPORT.md`): «обычная атака, затем
//   микроход — удар бьёт 4 раза». Свой ответ 4029 на 4026 несёт все 4 значения очереди сервера, а сервер применил из них только
//   `+65117` (2 у ближнего боя, 1 у дальнего; остальное спишет следующий принятый 4028). Порт ставил на кадры замаха все
//   `StepCount` значений (меч — 4), и таймеры показа переживали остановку замаха ходом, сменой цели и новым 4026. Эталон снимает
//   значение очереди только на кадре удара в состоянии атаки 5 (`CCharacter_ResolveBasicAttack`, entity_char.c:26886), а покой
//   `CChar_SetState(…, 0)` очередь сбрасывает (entity_char.c:19301-19305) — прерванный удар он не показывает никогда.
//   Ключ: 1 — значение своего ответа показывается, если сервер его УЖЕ применил (номер в серии < принятых ударов × `+65117`,
//   `UAsdaAttackRhythmSubsystem::ServerCommittedEntries`) или серия ещё идёт (бой не прерван — следующий 4028 его применит);
//   иначе цифры, выстрела и вычета полосы нет, непоказанный урон снимается с ленты попаданий. 0 — прежнее поведение (показ всех
//   поставленных значений). Удары соседа и PvP не затрагиваются: они снимаются по кадрам отрезка/клипа (эталонная схема).
ASDA_DEV_CVAR(int32, CVarAsdaOwnHitsServerCommit,
	TEXT("asda.Combat.OwnHitsServerCommit"), 1,
	TEXT("1 (по умолчанию) = своё значение ответа 4029 показывается, только если сервер его уже применил или бой ещё идёт ")
	TEXT("(ход/смена цели/новый 4026 гасят непримененные — «фантомный урон»); 0 = прежнее поведение (показ всех поставленных)."),
	ECVF_Default);

int32 GAsdaOwnPhantomDropped = 0;   // сколько своих непримененных значений не показано (наблюдение самотеста)
int32 AsdaOwnPhantomDroppedCount() { return GAsdaOwnPhantomDropped; }   // для самотеста `asda.Combat.SelfTestPhantom`

// ── ★ Экшен 4015 (эталон Recv_CombatHit@0xF310E0): HP цели БЕЗУСЛОВНО из @+25 (шапка хендлера),
//   затем switch по kind: 1=стоп/снап на cur-тайл, 2=движение cur→dst (SetActionState(2) +
//   projInitTrajectoryToTarget — ОСНОВНОЕ движение мобов), 3=удар (SetActionState(3) цели + звук
//   хита + позиц-коррекция при расхождении ≥3 тайлов), 5/7=каст. state==10 → пакет игнор. ──
void UAsdaWorldSubsystem::OnCombatHit(const FAsdaCombatHit& H)
{
	UAsdaNetSubsystem* Net = GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>();
	// ★ @0 — всегда ИГРОК (эталон CharManager_FindByUserIdx). Незнакомый → запросить снапшот 5416
	//   (как при 4007-незнакомце): иначе дерущиеся рядом игроки остаются невидимками.
	if (Net && H.AttackerUserIdx >= 0 && !Net->IsSelfUserIdx(H.AttackerUserIdx) &&
		!NetPlayers.Contains(H.AttackerUserIdx) && !Net->RequestedUserInfo.Contains(H.AttackerUserIdx))
	{
		if (Net->SendAgainUserInfo((uint16)H.AttackerUserIdx)) { Net->RequestedUserInfo.Add(H.AttackerUserIdx); }   // ★ ревизия волны 1: в кэш — только отправленный (гейт перехода/обрыв не должны «залипать» номер)
	}
	// ★★★ ПРОТУХШАЯ СЛАБАЯ ССЫЛКА = «СУЩНОСТИ НЕТ», А НЕ «ВЫХОД» (правка 2026-08-11, mobhit-agent).
	//   Актор мог быть уничтожен фейдом (AoI 0x100 → `charDespawnRemove`, посмертный 4 → 0x10 →
	//   `charRemoveEntity`), и запись в `NetMobs` осталась с нулевым weak-ptr. У эталона такой сущности
	//   в списке уже НЕТ ⇒ `Entity_FindByRenderId_thunk` возвращает 0 и работает ветка спауна на лету
	//   (game.c:548185+). Прежний код здесь молча возвращался — ровно тот сценарий №2 из отчёта
	//   («клиент удалил моба по AoI-фейду, а сервер продолжает слать действия»).
	if (TWeakObjectPtr<AAsdaMobActor>* Ex = NetMobs.Find(H.TargetRid))
	{
		if (!Ex->Get())
		{
			NetMobs.Remove(H.TargetRid);
			UE_LOG(LogAsdaWorld, Verbose,
				TEXT("[моб?] 4015 rid=%d: актор уже уничтожен фейдом — запись снята, идём в ветку «сущности нет»"),
				H.TargetRid);
		}
	}
	if (TWeakObjectPtr<AAsdaMobActor>* Ex = NetMobs.Find(H.TargetRid))
	{
		AAsdaMobActor* M = Ex->Get();
		if (M->IsDead())
		{
			// ★ Журнал боя: «почему НЕ» — пакет по трупу (частый канал: уровень по цели цикла).
			ASDA_CTRACE_MOB(H.TargetRid, EAsdaCT::Recv, TEXT("4015 kind=%d %s: моб уже мёртв (сост.10) — пакет игнорируется (HP@25=%d)"),
				(int32)H.Kind, *AsdaCtWorld::Mob(H.TargetRid, M), H.NewHp);
			return;
		}   // эталон: state==10 → пакет игнорируется
		// ★★★ ПОРЯДОК «HP ↔ КАДР УДАРА» (поправка 2026-08-11, aoedmg-agent). Эталон здесь смерть НЕ
		//   ставит сам (`Recv_CombatHit@0xF310E0` во всех 15 ветках делает только
		//   `targetEntity[284] = *(_DWORD *)(pPacket + 25)`), но покадровый обход
		//   `entityListUpdateFade@0xF78660` валит ЛЮБУЮ сущность с `i[284] <= 0`
		//   (`charStopAllBuffAndSlotEffects → sub_92C3AD(i,32) → SetActionState(i,10)`) — то есть
		//   нулевой HP из 4015 убивает цель СЛЕДУЮЩИМ КАДРОМ. Наш вызов NetDeath здесь эквивалентен.
		//   ⚠ Но если по этой цели уже принята, но ЕЩЁ НЕ ВЫПУЩЕНА смертельная запись каста, и HP, и
		//   смерть обязаны дождаться кадра удара — иначе полоса обнуляется и труп ложится раньше
		//   цифры урона. ⚠ ВЫКЛЮЧЕНО ПО УМОЛЧАНИЮ (§28: гейт рассинхронизировал клиентов), ключ
		//   возврата — `asda.Skill.HoldMobDeath 1`; разбор — в шапке `OnCombatHitAux`.
		//   Гейт снимает ТОЛЬКО обнуление HP и смерть — движение/флинч этого же кадра (switch ниже)
		//   идут как обычно, иначе моб замер бы на месте до кадра удара.
		const bool bHoldZeroHp = (H.NewHp <= 0) && IsMobDeathHeldByPendingHit(H.TargetRid);
		if (bHoldZeroHp)
		{
			UE_LOG(LogAsdaWorld, Log,
				TEXT("[net] 4015 rid=%d '%s': HP=0 ОТЛОЖЕН до кадра удара — по цели припаркована ")
				TEXT("смертельная запись каста (режим asda.Skill.HoldMobDeath 1)"), H.TargetRid, *M->MobName);
			ASDA_CTRACE_MOB(H.TargetRid, EAsdaCT::Death, TEXT("4015 %s: HP=0 и смерть ПРИДЕРЖАНЫ до кадра удара навыка (смертельная запись каста на полке; asda.Skill.HoldMobDeath 1), HP полосы %d/%d"),
				*AsdaCtWorld::Mob(H.TargetRid, M), M->CurHp, M->MaxHp);
		}
		// ★★★ Тот же фильтр устаревшего снимка, что в `ApplyMobHp`: 4015 несёт ЖИВОЕ поле HP моба
		//   (`mon+30853`, GameServer `Recv_MonsterUpdateAction@0x426B00` @+25), а NpcServer держит
		//   урон в накопителе до 2000 мс — снимок ВЫШЕ прогноза каста применять нельзя.
		// ★★★ ОДНО ПРАВИЛО НА ВЕСЬ ПОРТ: серверное здоровье цели не показывается РАНЬШЕ, чем
		//   пришёлся визуальный удар текущего замаха.
		//   Сервер регистрирует урон через ~30 мс после нашей заявки, а оружие опускается
		//   (у дальнобойного — снаряд долетает) через 1.2-1.7 с. Пакет движения 4015 приходит
		//   десятки раз в секунду и приносит свежее здоровье немедленно — отсюда «урон
		//   начислился сразу при начале атаки, не дожидаясь взмахивания» (владелец 24.08.2026).
		//   Пока НАШ замах по ЭТОЙ цели в полёте, здоровье из 4015 не применяем: то же самое
		//   число приедет по боевому каналу ровно в момент попадания. Для всех прочих целей и
		//   когда своего замаха нет — применяем сразу, как раньше.
		else if (!IsServerHpSnapshotStale(H.TargetRid, H.NewHp))
		{
			// ★ 17.10.2026: придержанное значение запоминается и применяется на попадании — прежде оно выбрасывалось, и полоса
			//   ждала следующего пакета. HP = 0 здесь не держим: это смерть, её ведёт ветка ниже.
			// ★ Ф1: лента попаданий моба — для ЛЮБОГО атакующего (свой и чужой замах, навык), разбор у `ArmImpact`.
			const bool bLedgerKey = CVarAsdaHitFrameAllChannels.GetValueOnGameThread() != 0;   // лента включена (ключ отката 0 — окно до 17.10)
			if (H.NewHp > 0) { NoteMobHpFloor(M, H.NewHp, /*bFrom4029=*/false, TEXT("4015 @25")); }   // ★ 18.10.2026: пол только опускается
			if (H.NewHp > 0 && LedgerHoldServerHp(M, H.NewHp, TEXT("4015"), /*bStaleChecked=*/true, /*bAllowNumber=*/false))
			{
				// придержан и запомнен — применится на попадании (строка в журнале — внутри)
			}
			else if ((!bLedgerKey && IsOurSwingInFlightAgainst(H.TargetRid)) || (bLedgerKey && H.NewHp <= 0 && HasPendingImpacts(H.TargetRid)))
			{
				UE_LOG(LogAsdaWorld, VeryVerbose,
					TEXT("[net] 4015 rid=%d: HP=%d придержан до попадания нашего замаха"),
					H.TargetRid, H.NewHp);
				// ★ Журнал боя: придержано (только если значение отличается от полосы — 4015 повторяет одно и то же десятки раз).
				if (H.NewHp != M->CurHp)
				{
					ASDA_CTRACE_MOB(H.TargetRid, EAsdaCT::HpPlate, TEXT("4015 %s: HP=%d ПРИДЕРЖАН до попадания (%s), полоса остаётся %d/%d"),
						*AsdaCtWorld::Mob(H.TargetRid, M), H.NewHp,
						bLedgerKey ? TEXT("HP=0 при записях ленты — смерть ведёт ветка ниже") : TEXT("окно «наш замах в полёте», ключ HitFrameAllChannels 0"),
						M->CurHp, M->MaxHp);
				}
			}
			else
			{
				if (FMath::Max(0, H.NewHp) != M->CurHp)
				{
					ASDA_CTRACE_MOB(H.TargetRid, EAsdaCT::HpPlate, TEXT("4015 %s: HP полосы %d → %d / %d (серверный снимок kind=%d, применён сразу — лента не держит)"),
						*AsdaCtWorld::Mob(H.TargetRid, M), M->CurHp, FMath::Max(0, H.NewHp), M->MaxHp, (int32)H.Kind);
				}
				// ★ 18.10.2026 (пол HP): в бою подъём полосы откладывается до выхода из боя.
				if (!HoldMobHpRaise(M, FMath::Max(0, H.NewHp), TEXT("4015")))
				{
					M->CurHp = FMath::Max(0, H.NewHp);
					UpdateMobHpBar(M);
				}
			}
		}
		// ★★★ П45 (T4, 2026-08-11): ОТБРОС СНИМКА БОЛЬШЕ НЕ МОЛЧИТ. Механизм остаётся (он помеченное
		//   исправление дефекта оригинала, откат — `asda.Skill.LegacyDeath 1`; у эталона такого фильтра
		//   нет вовсе), но раньше эта ветка выбрасывала серверный HP БЕЗ СЛЕДА в логе — по логу нельзя
		//   было отличить «фильтр сработал» от «пакет не пришёл». В `ApplyMobHp` строка уже была;
		//   здесь — второе место чтения того же фильтра, и оно оставалось немым.
		else
		{
			const FAsdaMobHpForecast* Fc = MobHpForecast.Find(H.TargetRid);
			UE_LOG(LogAsdaWorld, Log,
				TEXT("[net] 4015 rid=%d '%s': снимок HP=%d ОТБРОШЕН — сервер сам прогнозировал HP=%d ")
				TEXT("(накопитель урона NpcServer ещё не слит; откат asda.Skill.LegacyDeath 1)"),
				H.TargetRid, *M->MobName, H.NewHp, Fc ? Fc->Hp : -1);
			ASDA_CTRACE_MOB(H.TargetRid, EAsdaCT::HpPlate, TEXT("4015 %s: снимок HP=%d ОТБРОШЕН как устаревший (прогноз сервера %d; накопитель NpcServer не слит), полоса остаётся %d/%d"),
				*AsdaCtWorld::Mob(H.TargetRid, M), H.NewHp, Fc ? Fc->Hp : -1, M->CurHp, M->MaxHp);
		}
		// ★ 17.10.2026: HP = 0 при НАШЕМ замахе в полёте — смерть ждёт попадания (разбор у `ArmOurImpactWindow`); движение
		//   и флинч этого же пакета (switch ниже) идут как обычно.
		const bool bDeathDeferred = (H.NewHp <= 0) && !bHoldZeroHp && LedgerDeferDeath(M, TEXT("4015 HP=0"));
		if (H.NewHp <= 0 && !bHoldZeroHp && !bDeathDeferred)
		{
			// ★ СМЕРТЬ (эталон: покадровый `entityListUpdateFade` → SetActionState(10)@0xF876D0): труп
			//   ЛЕЖИТ, сущность из списка НЕ удаляется (удаление только по флагу 16 или AoI-фейду 0x100),
			//   поэтому rid ОСТАЁТСЯ за трупом и реген (4013/4019) оживляет ЭТУ ЖЕ сущность НА ЕЁ МЕСТЕ.
			//   4011 мобов в оригинале НЕ удаляет.
			UE_LOG(LogAsdaWorld, Log, TEXT("[net] смерть моба rid=%d '%s' (клип смерти; труп остаётся сущностью — rid сохранён под реген)"), H.TargetRid, *M->MobName);
			ASDA_CTRACE_MOB(H.TargetRid, EAsdaCT::Death, TEXT("4015 %s: HP=0 — СМЕРТЬ применена сразу (лента не отложила; NetDeath, труп остаётся под реген)%s"),
				*AsdaCtWorld::Mob(H.TargetRid, M), (H.TargetRid == AsdaCombatTrace::CurrentCycleTarget()) ? TEXT(", цель цикла") : TEXT(""));
			// Эталон: убитая ЦЕЛЬ снимается (SetActionState(10) сбрасывает таргет-HUD).
			if (AAsdaPlayerController* PC = Cast<AAsdaPlayerController>(GetGameInstance()->GetFirstLocalPlayerController()))
			{
				if (PC->TargetActor.Get() == M) { PC->ClearTarget(); }
			}
			M->NetDeath();
			ASDA_CTRACE_END_IF_TARGET(H.TargetRid, TEXT("цель умерла (4015 HP=0)"));
			return;
		}
		const FVector2D CurG = AsdaCoord::TileCenterGame(H.CurTile);   // ★ Аудит №3 (B2)
		const FVector2D DstG = AsdaCoord::TileCenterGame(H.DstTile);   // ★ Аудит №3 (B2)
		// ★★★ AoI-таймер (сущность +0x24C = 10000.0f) взводит ТОЛЬКО Recv_MobSpawn (4013):
		//   `mob[147] = 1176256512` — в Recv_CombatHit@0xF310E0 записи в [147] НЕТ ни в одной из 15 веток.
		//   Прежний рефреш по 4015 держал в мире мобов, которых оригинал к этому моменту уже убрал
		//   (entityListUpdateFade: таймер клампится только пока моб в пределах ±1 сектора). Убрано — 1:1.
		// ★★★ ЗАМЕР РАССИНХРОНА (диагностика): насколько наш актор разошёлся с серверной клеткой cur.
		//   Пишем ДО применения пакета — это и есть честная метрика «совпало/не совпало».
		//   ★ Расширено на боевые каналы движения 5 (CHASE), 7 (BATTLEMOVE) и 0xE (FEAR) — именно они
		//     раньше выбрасывались в `default:` и потому в метрику не попадали.
		if (H.Kind == 1 || H.Kind == 2 || H.Kind == 3 || H.Kind == 5 || H.Kind == 7 || H.Kind == 0xE)
		{
			const float D = M->NoteDesync(CurG);
			if (D > 100.f)
			{
				UE_LOG(LogAsdaWorld, Log, TEXT("[рассинхрон] rid=%d '%s' kind=%d: наш мир=(%.0f,%.0f) тайл сервера=(%d,%d) расхождение=%.0f юн (%.2f тайла); средн=%.0f макс=%.0f по %d замерам"),
					H.TargetRid, *M->MobName, (int32)H.Kind, M->GetActorLocation().X, M->GetActorLocation().Y,
					H.CurTile.X, H.CurTile.Y, D, D / 100.f,
					M->DesyncN ? (float)(M->DesyncSumU / M->DesyncN) : 0.f, M->DesyncMaxU, M->DesyncN);
			}
		}
		// ★ 07.11.2026 [ИСПРАВЛЕНИЕ] отрезок сервера — до гейтов: пакет важен и когда моб в клипе попадания.
		M->NoteServerMovePacket(H.Kind, CurG, DstG, H.Speed, H.MoveAnimRate);
		switch (H.Kind)
		{
		case 1:   // ★ СТОП. Эталон Recv_CombatHit@0xF310E0 case 1: гейт state 13/14, затем
			      //   dist²(текущая, тайл@7/@9) сравнивается с flt_13AB6A8 = 22500.0 = 150², и ТОЛЬКО
			      //   если БОЛЬШЕ — SetActionState(цель,1) + playerSetWorldPosition(тайл пакета).
			      //   Порог порта (150 юнитов) — ровно этот же константный порог, совпадение точное.
		{
			const FVector Was = M->GetActorLocation();
			M->NetStop(CurG);
			const FVector Now = M->GetActorLocation();
			if (!Was.Equals(Now, 1.f))
			{
				UE_LOG(LogAsdaWorld, Log, TEXT("[моб→] 4015 kind=1 rid=%d '%s' СНАП (рассинхрон>150, эталон flt_13AB6A8=22500): (%.0f,%.0f) → (%.0f,%.0f) тайл пакета=(%d,%d)"),
					H.TargetRid, *M->MobName, Was.X, Was.Y, Now.X, Now.Y, H.CurTile.X, H.CurTile.Y);
			}
			break;
		}
		case 2:   // ★★★ ДВИЖЕНИЕ. Эталон Recv_CombatHit@0xF310E0 case 2 — ДОСЛОВНО:
			      //     if (@7 == @11 && @9 == @13) return;              // cur == dst ⇒ ничего
			      //     pTileAt = CMap_GetTileAt(@7, @9);
			      //     if ( targetEntity[116] != 23 )                    // 23 = категория МОБ
			      //       ★ R5-fix2 (13.11.2026, уточнение по декомпилу): [116] — `loadSubState`, стадия загрузки ресурсов моба
			      //         (23 = загружен), а не категория: эталон переставляет в cur только ещё НЕ загруженного (невидимого) моба.
			      //         То же условие стоит у kind 7 (network.c:37958); у case 5 перестановки нет вовсе. В порту моб готов сразу —
			      //         перестановки нет ни в одной ветке, это соответствует эталону для загруженного моба.
			      //         playerSetWorldPosition(targetEntity, cur);    // ← МОБУ НЕ СТАВИТСЯ
			      //     pTileAt2 = CMap_GetTileAt(@11, @13);
			      //     if (state == 13 || state == 14) return;
			      //     CCharacter_SetActionState(targetEntity, 2); ... траектория к dst
			      //   ⇒ у моба позиция из 4015 kind=2 НЕ применяется — тот же закон, что в 4019
			      //   (Recv_MobSpawnLoop) и в 4013 её НЕТ только для kind=2. Порт звал NetMove(cur,dst),
			      //   а тот при рассинхроне > 150 СНАПИЛ моба на cur-тайл пакета. После регена сервер
			      //   держит в своей записи клетку регена (живой лог: (103,230)), поэтому каждый
			      //   идущий моб телепортировался туда — «мобы в одной точке».
			      // ★★★ КРУГ 7 — НАЙДЕН МЕХАНИЗМ СИНХРОНИЗАЦИИ. Хвост той же ветки (network.c:37613-37637,
			      //   дизасм 0xF3131C..0xF313AA) НЕ передаёт в траекторию сырую 100/spd, а НОРМИРУЕТ ЕЁ
			      //   ПО ВРЕМЕНИ ОТРЕЗКА:
			      //     var_2C = 100.0 / spd;                         // «скорость пакета»
			      //     var_34 = |dst − curПАКЕТА|;  var_30 = var_34 / var_2C;   // ВРЕМЯ отрезка по серверу
			      //     var_34 = |dst − НАША позиция|;
			      //     projInitTrajectoryToTarget(target, dst, var_34 / var_30);
			      //   ⇒ моб приходит в dst в тот же МОМЕНТ, что и на сервере, из какой бы точки он ни
			      //   стартовал. Это и есть непрерывная синхронизация без снапов: отставание, набранное
			      //   за хит-флинч/замах/потерянный пакет, гасится за ОДИН отрезок. Порт передавал сырую
			      //   скорость — и отставание оставалось навсегда, накапливаясь с каждым отрезком:
			      //   «моб стоит — позиция совпадает, моб идёт — расходится» (юзер 2026-08-02).
			if (H.DstTile != H.CurTile) { M->NetTrajectoryTimed(CurG, DstG, H.Speed, H.MoveAnimRate); }
			break;
		case 3:   // ★ УДАР МОБА ПО ИГРОКУ @0 (эталон SetActionState(3) + Combat_ApplyDamage@0xF7C470):
			      //   моб останавливается, лицом к жертве, клип 5+вариант; на «кадре удара» — цифры+импакт.
			      // ★★★ Тело вынесено в `ApplyCombatHitKind3`: эталон исполняет ТОТ ЖЕ код второй раз —
			      //   в ветке «сущности не было», сразу после спауна моба из пакета удара
			      //   (game.c:548243-548303 — дословный повтор game.c:547748-547821).
			ApplyCombatHitKind3(M, H, /*bAllowReflect=*/true);
			break;
		// ══════════════════════════════════════════════════════════════════════════════════════════
		// ★★★ 5 = MONSTER_ACTION_CHASE, 7 = BATTLEMOVE, 0xE = FEAR — БОЕВЫЕ КАНАЛЫ ДВИЖЕНИЯ.
		//   До 2026-08-11 все три уходили в `default: break;` с подписью «каст/спец-эффекты» — это была
		//   ошибка разбора: байт @6 пакета 4015 равен `mon->m_Action` NpcServer-а
		//   (`AlarmMonsterActionToGameServer@0x460990`, запись 43 б), а диспетчер действий
		//   `ProcessAction@0x44BBA0` расшифровывает 5/13 → CHASE, 7 → BATTLEMOVE, 14 → FEAR.
		//   Пока моб бежал к игроку (kind=5, скорость 100/250 = 0.4 юн/мс против прогулочной
		//   100/1666 = 0.06), наш актор СТОЯЛ, расхождение росло до 8-12 тайлов, и на пакете удара
		//   срабатывало эталонное правило «≥3 тайлов → поставить на серверную клетку» — моб рывком
		//   оказывался вплотную к игроку. Полный разбор: Reports/mob_movement_analysis.md.
		// ══════════════════════════════════════════════════════════════════════════════════════════
		case 5:    // ★★★ БЕГ К ЦЕЛИ (эталон Recv_CombatHit@0xF310E0 case 5, network.c:37841-37917)
		case 0xE:  // ★ FEAR (case 0xE, :38007-38143) — движение строится ТЕМ ЖЕ playerCastDirectionalEffect2
		{          //   (состояние 5, клип бега), отличается двумя деталями:
			//   · гейт буфера короче — только состояния 13/14 (в ЗАМАХЕ пакет применяется сразу);
			//   · в состоянии 3 эталон дополнительно ВЫВОДИТ ОТЛОЖЕННЫЕ ЦИФРЫ УРОНА по игроку из
			//     ent[1743] (значение, сохранённое веткой kind=3) — у порта цифры показываются сразу
			//     на кадре удара в case 3, отдельного «отложенного показа» в порту нет и он не нужен;
			//   · `playerSetWorldPosition(cur)` в той же ветке стоит под гейтом `ent[116] != 23`
			//     (не-моб), а здесь мы работаем только с мобами ⇒ позиция из пакета не ставится.
			//   В живом логе 2026-08-11 (3901 пакет) kind=0xE не встречался ни разу.
			// Эталон: `if (state == 3 || state == 13 || state == 14) { ent[1753] = 1; memcpy(ent+1754, pkt, 37); }`
			//   (для 0xE гейт короче — только 13/14: во время ЗАМАХА пакет FEAR применяется сразу).
			const bool bGate = (H.Kind == 5)
				? (M->IsAttackAnimActive() || M->IsHitAnimActive())   // состояния 3, 13, 14
				: M->IsHitAnimActive();                               // состояния 13, 14
			if (bGate)
			{
				M->BufferNetAction(H.Kind, CurG, DstG, H.Speed, H.MoveAnimRate);
				UE_LOG(LogAsdaWorld, Log, TEXT("[моб→] 4015 kind=%d rid=%d '%s' ОТЛОЖЕН в буфер (моб в замахе/флинче): (%d,%d)→(%d,%d)"),
					(int32)H.Kind, H.TargetRid, *M->MobName, H.CurTile.X, H.CurTile.Y, H.DstTile.X, H.DstTile.Y);
				break;
			}
			if (H.DstTile == H.CurTile) { break; }   // эталон: cur == dst ⇒ `return 1`, ничего не делаем
			// Эталон: `ent[1749] = pkt@0` (userIdx цели преследования) + charSetTerrainCellIndex(dst).
			//   ★ 2026-08-11 (B5): поле 1749 теперь ДЕЙСТВИТЕЛЬНО хранится — его читает доводчик
			//     `charCastDirectionalEffectIfFar` (AsdaMobActor::CastDirectionalEffectIfFar).
			M->SetChaseTargetUserIdx(H.AttackerUserIdx);
			M->RememberServerCell(DstG);
			// Эталон: `if (!ent[237]) { звук фазы 3; ent[237] = 1; }` — рык агра ровно один раз.
			//   Для 0xE рыка нет (эталон в этой ветке звук не строит).
			if (H.Kind == 5) { M->PlayAggroRoarOnce(); }
			// Траектория к dst с НОРМИРОВКОЙ СКОРОСТИ ПО ВРЕМЕНИ ОТРЕЗКА (тот же механизм, что kind=2:
			//   v' = |dst − наша позиция| / (|dst − curПакета| / (100/spd)) ) — отставание, набранное за
			//   замах/флинч/потерянный пакет, гасится за ОДИН отрезок и снапы становятся не нужны.
			M->NetChaseTimed(/*MoveState=*/5, CurG, DstG, H.Speed, H.MoveAnimRate);
			// ★★★ ХВОСТ ВЕТКИ 0xE (исправлено 2026-08-11, B7 перепроверки): эталон в самом конце
			//   `case 0xE` (network.c:38182) ставит `*((_WORD *)targetEntity + 3492) = 1`. По этому флагу
			//   тик прибытия `charAnim_UpdateActionTick@0xF80A50` гасит его и ОСТАВЛЯЕТ ТЕКУЩУЮ позицию
			//   вместо снапа на конец кривой. У case 5 такой записи нет — флаг ставит только FEAR.
			if (H.Kind == 0xE) { M->MarkKeepPosOnArrive(); }
			UE_LOG(LogAsdaWorld, Log, TEXT("[моб→] 4015 kind=%d rid=%d '%s' БЕГ К ЦЕЛИ: (%d,%d)→(%d,%d) v=%.3f юн/мс (сост.5, клип бега)"),
				(int32)H.Kind, H.TargetRid, *M->MobName, H.CurTile.X, H.CurTile.Y, H.DstTile.X, H.DstTile.Y, H.Speed);
			ASDA_CTRACE_MOB(H.TargetRid, EAsdaCT::MobAttack, TEXT("4015 kind=%d %s: моб %s, игрок uidx=%d%s: (%d,%d)→(%d,%d)"),
				(int32)H.Kind, *AsdaCtWorld::Mob(H.TargetRid, M), (H.Kind == 5) ? TEXT("бежит к игроку (агро)") : TEXT("бежит в страхе (FEAR)"), H.AttackerUserIdx,
				(Net && Net->IsSelfUserIdx(H.AttackerUserIdx)) ? TEXT(" (я)") : TEXT(""), H.CurTile.X, H.CurTile.Y, H.DstTile.X, H.DstTile.Y);
			break;
		}
		case 7:   // ★★★ БОЕВОЕ ПЕРЕМЕЩЕНИЕ (эталон case 7, network.c:37919-38005)
		{
			// Эталон, ровно в этом порядке:
			//   if (state == 13 || state == 14) { ent[1749] = pkt@0; charSetTerrainCellIndex(dst); return 1; }
			//                                     ← БУФЕРА ЗДЕСЬ НЕТ: пакет теряется, но клетка запоминается
			//   if (state == 3)                 { ent[1753] = 1; memcpy(ent+1754, pkt, 37); return 0; }
			if (M->IsHitAnimActive())
			{
				M->SetChaseTargetUserIdx(H.AttackerUserIdx);   // эталон: `ent[1749] = pkt@0` и в этой ветке
				M->RememberServerCell(DstG);
				break;
			}
			if (M->IsAttackAnimActive())
			{
				M->BufferNetAction(7, CurG, DstG, H.Speed, H.MoveAnimRate);
				break;
			}
			if (H.DstTile == H.CurTile) { break; }   // cur == dst ⇒ ничего
			// Эталон: `if (ent[116] != 23) playerSetWorldPosition(cur)` — категория 23 = МОБ, значит
			//   позиция из пакета мобу НЕ ставится (тот же закон, что в kind=2 и в 4019). Здесь мы
			//   работаем только с мобами (NetMobs), поэтому снапа нет по построению.
			M->SetChaseTargetUserIdx(H.AttackerUserIdx);   // эталон: `targetEntity[1749] = *pPacket`
			M->RememberServerCell(DstG);
			M->NetChaseTimed(/*MoveState=*/7, CurG, DstG, H.Speed, H.MoveAnimRate);
			UE_LOG(LogAsdaWorld, Log, TEXT("[моб→] 4015 kind=7 rid=%d '%s' БОЕВОЕ ПЕРЕМЕЩЕНИЕ: (%d,%d)→(%d,%d) v=%.3f юн/мс (сост.7, клип бега)"),
				H.TargetRid, *M->MobName, H.CurTile.X, H.CurTile.Y, H.DstTile.X, H.DstTile.Y, H.Speed);
			break;
		}
		// ★ kind = 13 (тоже CHASE у NpcServer) и любые прочие: эталонный `switch` их НЕ обрабатывает —
		//   у него `default: return 0;` (network.c:38190), никакого действия. Воспроизводим 1:1.
		//   В живом логе 2026-08-11 (3901 пакет 4015) значения 13 не встречалось ни разу.
		default:
			UE_LOG(LogAsdaWorld, Verbose, TEXT("[моб→] 4015 kind=%d rid=%d '%s' — эталон эту ветку не обрабатывает (default: return 0)"),
				(int32)H.Kind, H.TargetRid, *M->MobName);
			break;
		}
		return;
	}
	// ══════════════════════════════════════════════════════════════════════════════════════════════
	// ★★★ СУЩНОСТИ НЕТ — ЭТАЛОН СОЗДАЁТ МОБА ПРЯМО ИЗ ПАКЕТА УДАРА (game.c:548185-548385).
	//   Раньше порт здесь молча выходил (`if (NetMobs.Find(...))` без `else`), и игрок терял HP от
	//   моба, которого клиент не знает: ни модели, ни цифры, ни звука — «HP тает из воздуха».
	//   Ситуация ШТАТНАЯ, а не аварийная: `Send_SectorInUpdateMonster_NPC_Info@0x41D870`
	//   (GameServer.c:12998) рассылает 4015 ВСЕМ игрокам в 9 секторах вокруг моба, без проверки,
	//   знает ли клиент эту сущность. Отсюда четыре нормальных пути сюда: вход в сектор/на карту
	//   (4015 обгоняет батч 4013), AoI-фейд у клиента, переход моба в соседний сектор, респавн.
	//   Дословная последовательность эталона:
	//     if (pkt@6 != 3)              return 0;                     // C1 — только удар
	//     if (i32 pkt@25 <= 0) { … соул-путь … }                     // C2 — см. ниже
	//     idx = mobTableFindIndexById(g_pMobMgr, i16 pkt@2);         // C3 — id ТИПА → индекс строки
	//     ent = mobListSpawnAndLink(g_pCharManager, idx, i16 pkt@4); // renderId из пакета
	//     playerSetWorldPosition(ent, тайл(@7,@9));  ent[1765] = -1;  ent[282] = ent[144];
	//     ent[285] = MaxHp типа (Mob list.BIN +0x94);  ent[283] = ent[284] = i32 pkt@25;
	//     ент+6978 ← +0xF8 (ранг), +6977 ← +0x100 («бьёт первым»), +6979 ← +0xFC (агрессия);
	//     ent[106..108] = полу-размер AABB (+0x84);  ent[109..111] = позиция;  ent[110] += ent[107];
	//     далее — ТОТ ЖЕ код, что у ветки `case 3` найденной сущности.
	// ══════════════════════════════════════════════════════════════════════════════════════════════
	if (H.Kind != 3)
	{
		// C1: эталон `if (*((_BYTE *)pPacket + 6) != 3) return 0;` — любые другие действия по
		//   неизвестной сущности игнорируются целиком (движение «в никуда» не создаёт моба).
		UE_LOG(LogAsdaWorld, Verbose,
			TEXT("[моб?] 4015 kind=%d rid=%d: сущности нет — эталон здесь ничего не делает (return 0)"),
			(int32)H.Kind, H.TargetRid);
		return;
	}
	if (H.NewHp <= 0)
	{
		// ★★★ C2 — СОУЛ-ПУТЬ. Эталон: найти игрока @0; если у него взведено поле `attacker[47565]`,
		//   создать моба тем же `mobTableFindIndexById` + `mobListSpawnAndLink` и передать его в
		//   `cCharHandleSoulmateSkillTarget(attacker, mob)` (game.c:548189-548201). Ни позиции, ни HP,
		//   ни состояния эталон в этой ветке НЕ ставит — сущность создаётся ТОЛЬКО как цель
		//   соулмейт-навыка и её HP остаётся нулевым.
		// ⚠ ЧЕСТНАЯ ДЫРА, а не упрощение: системы соулмейта (`cCharHandleSoulmateSkillTarget@0xA2A980`)
		//   в порту нет вовсе, а создать моба с HP ≤ 0 без неё значит положить в мир труп, которого у
		//   эталона в этом сценарии не видно (он немедленно уходит в state 10 покадровым
		//   `entityListUpdateFade@0xF78660`). Поэтому здесь — ЗАПИСЬ В ЛОГ, а не спавн: по логу видно
		//   и что ветка сработала, и чего именно не хватает.
		UE_LOG(LogAsdaWorld, Log,
			TEXT("[моб?] 4015 kind=3 rid=%d type=%d: сущности нет и HP=%d ≤ 0 — эталон создал бы моба ")
			TEXT("для соул-цели (cCharHandleSoulmateSkillTarget@0xA2A980); системы соулмейта в порту нет"),
			H.TargetRid, H.MobType, H.NewHp);
		return;
	}
	if (H.MobType <= 0)
	{
		// `mobTableFindIndexById@0xF707D0` при ненайденном id зовёт ShaderError_ReportCode(500) +
		//   Show_DisconnectPopup(500) и возвращает 0 — то есть эталон считает это фатальной ошибкой
		//   данных. Порт до дисконнекта не доводит, но и молчать не имеет права.
		UE_LOG(LogAsdaWorld, Warning,
			TEXT("[моб?] 4015 kind=3 rid=%d: сущности нет, а тип моба @2 = %d (эталон здесь ушёл бы в ")
			TEXT("Show_DisconnectPopup(500) из mobTableFindIndexById@0xF707D0)"), H.TargetRid, H.MobType);
		return;
	}
	// C3: спавн на лету. `StateByte = 3` — эталон в C3c зовёт `SetActionState(mob, 3)` и траекторию НЕ
	//   строит; `CreateNetMob` строит её только при `StateByte == 2`, так что канал совпадает 1:1.
	//   Позиция — клетка @7/@9 (`playerSetWorldPosition(тайл(@7,@9))`), поэтому cur передаётся и как dst.
	//   MaxHp типа (`ent[285] = Mob list.BIN +0x94`) ставит сам `CreateNetMob` — общая правка Д7.
	if (AAsdaMobActor* Born = CreateNetMob(H.TargetRid, H.MobType, H.NewHp, H.CurTile, H.CurTile,
	                                       /*FaceDeg=*/0, /*Speed=*/0.f, /*StateByte=*/3, TEXT("4015-удар")))
	{
		UE_LOG(LogAsdaWorld, Log,
			TEXT("[моб^] 4015 kind=3 rid=%d type=%d hp=%d — СОЗДАН из пакета удара в клетке (%d,%d) ")
			TEXT("(эталон mobTableFindIndexById+mobListSpawnAndLink, game.c:548203-548242)"),
			H.TargetRid, H.MobType, H.NewHp, H.CurTile.X, H.CurTile.Y);
		// ★ ОТРАЖЕНИЕ ЗДЕСЬ НЕ ПРИМЕНЯЕТСЯ: ветка C3c эталона (game.c:548247-548248) пишет в сущность
		//   ТОЛЬКО `ent[1743] = @21` и `+6982 = @30` — записей `word9346 = @31` / `ent[4674] = @33`
		//   в ней НЕТ (в отличие от B6, game.c:547752-547753). Только что рождённый моб не мог никого
		//   ударить, так что и отражать ему нечего.
		ApplyCombatHitKind3(Born, H, /*bAllowReflect=*/false);
	}
}

// ══════════════════════════════════════════════════════════════════════════════════════════════════
// ★★★ ВЕТКА B4 — УДАР ПРИШЁЛ, ПОКА МОБ ВО ФЛИНЧЕ / «ОТЛОЖЕННОМ ДЕЙСТВИИ» (game.c:547635-547742).
//   Эталон: `if (state == 13 || 14 || 17 || 16 || 15) { … ; return 1; }` — ЗАМАХ НЕ ЗАПУСКАЕТСЯ.
//   Делается ровно четыре вещи:
//     1) `ent[1743] = i32 @21` — запомнить урон (его потом покажет ветка kind=0xE в состоянии 3);
//     2) показ результата удара по игроку СРАЗУ (DODGE / MISS / цифра; `-3` — вообще ничего);
//     3) `ent[1749] = userIdx игрока`, `word3676 = 0`;
//     4) `charSetTerrainCellIndex(cur)` всегда + `playerSetWorldPosition(cur)` при расхождении ≥3 тайлов.
//   Без этой ветки порт перезапускал клип замаха поверх хит-флинча: анимация дёргалась, а при частых
//   ударах замах не доигрывался никогда.
// ⚠ ЧЕСТНЫЙ ОСТАТОК: состояний 15/16/17 (система «отложенного действия») у `AAsdaMobActor` нет вовсе,
//   поэтому гейт работает по 13/14 — ровно тем состояниям, которые порт различает
//   (`IsHitAnimActive` = эталонные 13/14, см. AsdaMobActor.h). Зафиксировано в отчёте.
// ══════════════════════════════════════════════════════════════════════════════════════════════════
bool UAsdaWorldSubsystem::ApplyCombatHitFlinched(AAsdaMobActor* M, const FAsdaCombatHit& H)
{
	// ⚠ ГЕЙТ УЖЕ, ЧЕМ В ЭТАЛОНЕ. Оригинал (`Recv_CombatHit` case 3, network.c:37646) уходит в
	//   мгновенную ветку при состоянии моба 13, 14, **15, 16 ИЛИ 17**. У нас смоделированы только
	//   13/14 (флинч, `HitAnimUntilS`) — состояний 15/16/17 в порту НЕТ вовсе, поэтому расширять
	//   условие нечем. Когда они появятся — добавить их сюда, а не заводить второй гейт.
	if (!M || !M->IsHitAnimActive()) { return false; }
	UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	AActor* Victim = nullptr;
	if (AsdaSelf::IsSelfUserIdx(Net, H.AttackerUserIdx)) { Victim = LocalPlayerActor; }   // E2
	else if (FAsdaNetPlayerEntry* PE = NetPlayers.Find(H.AttackerUserIdx)) { Victim = PE->Actor.Get(); }
	// Эталон показывает урон НЕМЕДЛЕННО (никакого таймера кадра удара — клипа замаха здесь нет).
	//   `-3` эталон в этой ветке пропускает явно (`if (hpDelta != -3)`) — `ShowPlayerHit` делает то же.
	ASDA_CTRACE_IF(AsdaCtWorld::Ours(H.TargetRid, H.AttackerUserIdx), EAsdaCT::MobAttack, TEXT("4015 kind=3 %s бьёт игрока uidx=%d%s, моб во ФЛИНЧЕ — замаха нет, результат %s сразу (тип удара %d)%s"),
		*AsdaCtWorld::Mob(H.TargetRid, M), H.AttackerUserIdx, AsdaSelf::IsSelfUserIdx(Net, H.AttackerUserIdx) ? TEXT(" (я)") : TEXT(""),
		*AsdaCtWorld::Hit(H.Damage), (int32)H.HitType, Victim ? TEXT("") : TEXT(" — актора жертвы нет, цифры не будет"));
	if (Victim) { ShowPlayerHit(Victim, H.Damage, H.HitType, H.AttackerUserIdx, M ? M->MobName : FString()); }
	// `ent[1749] = LOWORD(attackerChar->…field_5B8)` (game.c:547718) — то же поле, что
	//   `Target_GetSelectedUserIdx@0x9254E5 → Char_GetHoveredUserIdx@0x966490` = `char + 190188`,
	//   т.е. userIdx самого игрока, найденного по @0. Значит это @0 (см. Д5 ниже).
	M->SetChaseTargetUserIdx(H.AttackerUserIdx);
	// Снап ≥3 тайлов + запоминание клетки — тот же хвост, что в B6 (game.c:547721-547741).
	const FVector2D CurG = AsdaCoord::TileCenterGame(H.CurTile);
	const FIntPoint NowTile = AsdaCoord::GameToTile(AsdaCoord::UeToGame2D(M->GetActorLocation()));
	if (FMath::Abs(NowTile.X - H.CurTile.X) >= 3 || FMath::Abs(NowTile.Y - H.CurTile.Y) >= 3)
	{
		UE_LOG(LogAsdaWorld, Log, TEXT("[моб→] 4015 kind=3 rid=%d '%s' СНАП ≥3 тайлов во ФЛИНЧЕ (эталон B4): тайл наш=(%d,%d) → пакета=(%d,%d)"),
			H.TargetRid, *M->MobName, NowTile.X, NowTile.Y, H.CurTile.X, H.CurTile.Y);
		M->NetSnapOrGlide(CurG);   // ★ 07.11.2026 [ИСПРАВЛЕНИЕ]: asda.Mob.SnapAsGlide 0 — мгновенно
	}
	M->RememberServerCell(CurG);
	UE_LOG(LogAsdaWorld, Log,
		TEXT("[бой] 4015 kind=3 rid=%d '%s': моб во ФЛИНЧЕ (сост.13/14) — замах НЕ перезапускаем, ")
		TEXT("показан только результат удара (урон=%d) [эталон game.c:547635]"),
		H.TargetRid, *M->MobName, H.Damage);
	return true;
}

// ══════════════════════════════════════════════════════════════════════════════════════════════════
// ★★★ 29.10.2026 РЕАКЦИЯ ЖЕРТВЫ НА УДАР МОБА (баг игроков «персонажи стоят как вкопанные, когда их бьют мобы») [ОРИГИНАЛ].
//   Эталон — кадр удара замаха моба `Combat_ApplyDamage@0xF7C470` (combat_pvp.c:7620-8160), после цифры/HP/звука:
//     · урон (обычный и крит): состояние 51 на жертве → `S_c_barrior3.wav` + `eff_1_char_06_brieg02-a_00.jeb` на кости оружия;
//       иначе 547 → `eff_1_char_99_defen01-a_00.jeb` + barrior3; иначе ВЗДРАГИВАНИЕ `gfxBias_LazyInitHalfTexel@0xA2C7B0` (МИСНЕЙМ;
//       свой :7852/:7949, чужой :8159 — только при собранной модели `categoryFlag == 32`). DODGE/MISS/IMMUNE — без вздрагивания.
//     · после ЛЮБОГО исхода: если действие жертвы 0 (покой) — `CharFx_AutoSabzil_State(жертва, 0, 0, 20)` / `CChar_SetState(…, 20)` =
//       БОЕВАЯ СТОЙКА (свой :7966-7967, чужой :8055-8057). Клипа «получил удар» у игрока в оригинале нет (загрузчики клипов
//       `CCharacter_LoadMotionAnims@0x9B84D0` / `Char_LoadMotionAnim@0x9B9890`; в exe ни одного `_damage/_dmg/_hit`).
//   Мгновенная ветка (моб во флинче, `Recv_CombatHit` case 3) вздрагивания и стойки НЕ делает — здесь только кадр замаха.
//   Эффекты: brieg02/defen01 — BP_Fx_* (Tools/asda_build_char_fx.py), cattk01 — BP_ImpactMobCrit (Tools/asda_build_combat_impacts.py).
//   Ключи: `asda.Char.HitFlinch` (вздрагивание и покраснение, AsdaBlendAnimInstance.cpp), `asda.Char.HitStance` (стойка),
//   `asda.Char.HitVictimFx` (эффекты барьеров и крита).
static ASDA_DEV_CVAR(int32, CVarAsdaCharHitStance,
	TEXT("asda.Char.HitStance"), 1,
	TEXT("1 = как оригинал: персонаж в покое, по которому ударил моб (в т.ч. промах/уклонение), переходит в боевую стойку; 0 = прежний порт."),
	ECVF_Default);

// ★ 29.10.2026 (разрешение владельца «пусть всё делает»): эффекты исхода по жертве — барьеры 51/547 (`brieg02` на кости оружия,
//   `defen01` в точке удара) и крит моба (`cattk01` вместо `battk01` + `S_c_cri1/2`). 0 — прежний порт (только battk01).
ASDA_DEV_CVAR(int32, CVarAsdaCharHitVictimFx,
	TEXT("asda.Char.HitVictimFx"), 1,
	TEXT("1 = как оригинал: барьеры 51/547 — эффекты brieg02/defen01, крит моба — cattk01 и звук S_c_cri1/2; 0 = прежний порт (всегда battk01, без звука крита)."),
	ECVF_Default);

static void AsdaMobHitVictimReaction(UGameInstance* GI, AActor* V, int32 Dmg, uint8 Ht, int32 VictimUidx, const AActor* Mob)
{
	AAsdaPlayerCharacter* Ch = Cast<AAsdaPlayerCharacter>(V);
	if (!GI || !Ch || Ch->IsIncapacitated()) { return; }
	// Чужой: только при собранной модели (`categoryFlag == 32`, иначе эталон уходит в LABEL_115 — одно отражение).
	if (!Ch->BaseSkel || !Ch->BaseSkel->GetSkeletalMeshAsset()) { return; }
	const UAsdaNetSubsystem* Net = GI->GetSubsystem<UAsdaNetSubsystem>();
	const bool bMe = AsdaSelf::IsSelfUserIdx(Net, VictimUidx);
	const bool bDamage = !(Dmg == -1 || Dmg == -2 || Dmg == 0 || (Dmg >= -5 && Dmg <= -3) || Ht == 16);
	const TCHAR* What = TEXT("без вздрагивания (уклонение/промах/неуязвимость)");
	if (bDamage)
	{
		// Крит моба (бит 31): `Audio_Play3DSound_v2(…, gender == 1 ? "S_c_cri1.wav" : "S_c_cri2.wav", позиция жертвы)` (combat_pvp.c:7885-7888);
		//   эффект `eff_0_mon_99_cattk01-a_00.jeb` ставит `ShowPlayerHit` (BP_ImpactMobCrit вместо BP_ImpactMob).
		if (((uint32)Dmg & 0x80000000u) != 0 && AsdaHitVictimFxOn())
		{
			if (UAsdaSoundSubsystem* Snd = GI->GetSubsystem<UAsdaSoundSubsystem>())
			{
				Snd->PlaySound3DAs(FName(Ch->Sex == 2 ? TEXT("s_c_cri2") : TEXT("s_c_cri1")), Ch->GetActorLocation(), EAsdaSoundClass::Voice, Ch);   // ★ 30.10: класс «голос»
			}
		}
		const UAsdaEffectsSubsystem* Fx = GI->GetSubsystem<UAsdaEffectsSubsystem>();
		const bool bBarrier51 = Fx && Fx->FindByTypeSub(VictimUidx, 51, 0) != nullptr;
		const bool bBarrier547 = !bBarrier51 && Fx && Fx->FindByTypeSub(VictimUidx, 547, 0) != nullptr;
		if (bBarrier51 || bBarrier547)
		{
			// `sub_9265F2(g_pSoundMgr, 14, "S_c_barrior3.wav")` — канал 14 (2D).
			// ★ [ИСПРАВЛЕНИЕ] 30.10.2026 (`asda.Sound.WorldEvents3D`): барьер ЧУЖОГО — 3D у жертвы (как ветки эталона
			//   combat_pvp.c:8044/8117 `sound3DPlayAtOrigin(…, pTarget->worldPos…, "S_c_barrior3.wav")`); прежде 2D на любую жертву.
			if (UAsdaSoundSubsystem* Snd = GI->GetSubsystem<UAsdaSoundSubsystem>())
			{
				if (!bMe && UAsdaSoundSubsystem::WorldEvents3DOn()) { Snd->PlaySound3DAs(FName(TEXT("s_c_barrior3")), Ch->GetActorLocation(), EAsdaSoundClass::Hit, Ch); }
				else                                                { Snd->PlaySound2D(FName(TEXT("s_c_barrior3"))); }
			}
			bool bFx = false;
			if (AsdaHitVictimFxOn() && bBarrier51)
			{
				// 51: `Effect_SpawnByName("eff_1_char_06_brieg02-a_00.jeb", 1, кость оружия field_32DCC × мир, …, 7, 0, 1.0)` — attach 7 (позиция кости +15).
				if (UAsdaEffectsSubsystem* FxW = GI->GetSubsystem<UAsdaEffectsSubsystem>())
				{
					bFx = FxW->SpawnDummyBoneFx(VictimUidx, TEXT("eff_1_char_06_brieg02-a_00"), FString(), EAsdaFxAttachMode::BonePos, Ch->CurrentWeaponBone()) != nullptr;
				}
			}
			else if (AsdaHitVictimFxOn() && bBarrier547)
			{
				// 547: `effectMgrSpawnJEB("eff_1_char_99_defen01-a_00.jeb", матрица моба с позицией = пересечение луча «моб → жертва» с боксом
				//   жертвы (`collideRayAabbAllFaces`), иначе позиция звука жертвы)` — у нас точка на габарите жертвы со стороны моба, поворот моба.
				FVector O, E; Ch->GetActorBounds(false, O, E);
				FVector Loc = O;
				FRotator Rot = FRotator::ZeroRotator;
				if (Mob)
				{
					FVector Dir = Mob->GetActorLocation() - O; Dir.Z = 0.f;
					if (Dir.Normalize()) { Loc = O + Dir * FMath::Min(E.X, E.Y); }
					Rot = FRotator(0.f, Mob->GetActorRotation().Yaw, 0.f);
				}
				if (UWorld* W = Ch->GetWorld())
				{
					if (AAsdaEffectActor* FxA = AAsdaEffectActor::SpawnByBPName(W, TEXT("BP_Fx_eff_1_char_99_defen01_a_00"), Loc, Rot, nullptr))
					{
						FxA->SetLifeSpan(FxA->NaturalLifeSpan());
						bFx = true;
					}
				}
			}
			What = bBarrier51 ? (bFx ? TEXT("барьер 51: звук barrior3 + эффект brieg02 на кости оружия") : TEXT("барьер 51: звук barrior3, эффекта НЕТ (BP brieg02 не найден или ключ 0)"))
				: (bFx ? TEXT("барьер 547: звук barrior3 + эффект defen01") : TEXT("барьер 547: звук barrior3, эффекта НЕТ (BP defen01 не найден или ключ 0)"));
		}
		else if (AsdaAnimSmooth::HitFlinchOn())
		{
			const bool bStarted = AsdaAnimSmooth::StartHitFlinch(Ch->BaseSkel);
			What = bStarted ? TEXT("ВЗДРАГИВАНИЕ (кость «4», −0.5 рад, 125 мс)") : TEXT("вздрагивание уже идёт — не перезапускается");
		}
	}
	// Боевая стойка при ударе в покое (действие 0).
	bool bStance = false;
	if (CVarAsdaCharHitStance.GetValueOnGameThread() != 0 && Ch->AnimState == EAsdaPlayerState::Idle && !Ch->IsMoving()
		&& !Ch->IsAttackAnimActive() && !Ch->IsSkillAnimActive() && !Ch->IsFishActionActive())
	{
		Ch->SetCombatStance(true);
		if (bMe)
		{
			if (AAsdaPlayerController* PC = Cast<AAsdaPlayerController>(Ch->GetController()))
			{
				PC->CombatStanceUntil = FPlatformTime::Seconds() + PC->CombatStanceHold;   // выход — тот же кулдаун 10 с, что у боя
			}
		}
		bStance = true;
	}
	ASDA_CTRACE_UID(VictimUidx, EAsdaCT::MobAttack, TEXT("реакция жертвы uidx=%d%s на удар моба (%s): %s%s"), VictimUidx, bMe ? TEXT(" (я)") : TEXT(""),
		*AsdaCtWorld::Hit(Dmg), What, bStance ? TEXT(", покой → БОЕВАЯ СТОЙКА") : TEXT(""));
}

// ══════════════════════════════════════════════════════════════════════════════════════════════════
// ★★★ ВЕТКА B6 / C3c — ПОЛНЫЙ УДАР МОБА ПО ИГРОКУ @0 (game.c:547748-547821 и повтор :548243-548303).
//   Порядок действий эталона сохранён: сначала поля сущности, потом замах со звуком, потом снап.
// ══════════════════════════════════════════════════════════════════════════════════════════════════
void UAsdaWorldSubsystem::ApplyCombatHitKind3(AAsdaMobActor* M, const FAsdaCombatHit& H, bool bAllowReflect)
{
	if (!M) { return; }
	// B4: моб во флинче ⇒ замаха нет (см. ApplyCombatHitFlinched).
	if (ApplyCombatHitFlinched(M, H)) { return; }
	UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	const FVector2D CurG = AsdaCoord::TileCenterGame(H.CurTile);   // ★ Аудит №3 (B2)
	AActor* Victim = nullptr;
	if (AsdaSelf::IsSelfUserIdx(Net, H.AttackerUserIdx)) { Victim = LocalPlayerActor; }   // E2
	else if (FAsdaNetPlayerEntry* PE = NetPlayers.Find(H.AttackerUserIdx)) { Victim = PE->Actor.Get(); }
	FVector2D VictimG = CurG;
	if (Victim)
	{
		const FVector VL = Victim->GetActorLocation();
		VictimG = AsdaCoord::UeToGame2D(VL);   // ★ Аудит №3 (B1)
	}
	// ★★★ Д5: `ent[1749] = Target_GetSelectedUserIdx(игрок@0)` (game.c:547776). Цепочка
	//   `Target_GetSelectedUserIdx@0x9254E5` → `Char_GetHoveredUserIdx@0x966490` = `*(WORD*)(char+190188)`
	//   отдаёт userIdx САМОГО найденного игрока, а игрок найден по @0 ⇒ значение равно @0. Ровно то же
	//   поле ветки 5/7/0xE пишут напрямую из `pkt@0`. Читатель — доводчик поворота
	//   `charCastDirectionalEffectIfFar@0xF869E0`: без записи моб в состоянии 3 «не смотрел» на жертву.
	M->SetChaseTargetUserIdx(H.AttackerUserIdx);
	// ⚠ ОТСТУПЛЕНИЕ ПОРТА, зафиксировано осознанно: эталон при ненайденном игроке @0
	//   (`if (!attackerChar_2) return 0;`, game.c:547823-547825) НЕ запускает замах вовсе. Порт замах
	//   играет всегда: сосед за границей AoI у нас может быть ещё не заспавнен (порт как раз шлёт по
	//   нему 5416 в шапке OnCombatHit), и 1:1-гейт заморозил бы мобов на краю зоны видимости.
	//   Разница видна только в отсутствии цифры урона по чужому игроку — она и так не показывается.
	const float SwingLen = M->NetAttack(H.HitVariant, VictimG, H.AtkAnimRate, /*bRoarAndFace=*/true, /*AttackResult=*/H.Damage);   // ★ 30.10: слот 7 при @21 < −1
	UE_LOG(LogAsdaWorld, Log, TEXT("[бой] моб rid=%d '%s' бьёт игрока uidx=%d: вариант=%d урон=%d клип=%.2fс"),
		H.TargetRid, *M->MobName, H.AttackerUserIdx, H.HitVariant, H.Damage, SwingLen);
	ASDA_CTRACE_IF(AsdaCtWorld::Ours(H.TargetRid, H.AttackerUserIdx), EAsdaCT::MobAttack, TEXT("4015 kind=3 %s ЗАМАХ по игроку uidx=%d%s: вариант=%d, результат %s, тип удара %d, клип %.2f с → кадр удара через %.2f с%s%s"),
		*AsdaCtWorld::Mob(H.TargetRid, M), H.AttackerUserIdx, AsdaSelf::IsSelfUserIdx(Net, H.AttackerUserIdx) ? TEXT(" (я)") : TEXT(""),
		(int32)H.HitVariant, *AsdaCtWorld::Hit(H.Damage), (int32)H.HitType, SwingLen, SwingLen * 0.45f,
		(SwingLen > 0.f) ? TEXT("") : TEXT(" — клипа нет, кадра удара НЕ будет (цифры нет)"),
		Victim ? TEXT("") : TEXT(" — актора жертвы нет у нас (цифры не будет)"));
	// «Кадр удара» ≈ 45% клипа замаха (точные тайминги — mob-таблица pCaster[648+], TODO реверс).
	if (SwingLen > 0.f)
	{
		TWeakObjectPtr<AActor> WeakV = Victim;
		TWeakObjectPtr<AAsdaMobActor> WeakM2 = M;
		const int32 Dmg = H.Damage; const uint8 Ht = H.HitType;
		const int32 VictimUidx = H.AttackerUserIdx;   // @0 = userIdx ЖЕРТВЫ (kind=3), см. FAsdaCombatHit
		// ★ Фаза звука моба: обычный удар — 5+вариант (эталон combat_pvp.c:7789), КРИТ — жёстко 7
		//   (:7857 `Sound_BuildEntryWavName(g_pMobMgr, mobTypeId, 7, …)`). Крит опознаётся битом 31.
		const bool bCritHit = ((uint32)H.Damage & 0x80000000u) != 0;
		const int32 SndPhase = bCritHit ? 7 : (5 + FMath::Clamp((int32)H.HitVariant, 0, 2));
		// ★★★ Д4: ОТРАЖЕНИЕ УРОНА едет тем же кадром. У эталона поля @31/@33 кладутся в сущность
		//   (`word9346` / `ent[4674]`), а срабатывают в покадровом тике сразу ПОСЛЕ показа цифры
		//   урона по игроку (game.c:576963 → LABEL_115 :576977). Мы держим тот же порядок внутри
		//   одного таймера, поэтому промежуточное поле сущности не нужно.
		const int32 ReflSkill = bAllowReflect ? H.ReflectSkillId : 0;
		const int32 ReflDmg   = H.ReflectDamage;
		FTimerHandle T;
		GetGameInstance()->GetWorld()->GetTimerManager().SetTimer(T,
			[this, WeakV, WeakM2, Dmg, Ht, SndPhase, VictimUidx, ReflSkill, ReflDmg]()
		{
			if (AAsdaMobActor* Mm = WeakM2.Get()) { Mm->PlayMobSound(SndPhase); }
			AAsdaMobActor* Mn = WeakM2.Get();
#if ASDA_COMBAT_TRACE
			// Журнал боя (волна 2, дубль): при живой жертве тот же кадр пишет `ShowPlayerHit` («удар моба … по игроку») —
			//   здесь только частый уровень; уровень 1 (наш бой) — лишь когда показа не будет (жертвы нет).
			if (WeakV.Get())
			{
				ASDA_CTRACE_V(EAsdaCT::HitFrame, TEXT("кадр удара моба %s по игроку uidx=%d: результат %s"),
					*AsdaCtWorld::Mob(Mn ? Mn->NetRenderId : -1, Mn), VictimUidx, *AsdaCtWorld::Hit(Dmg));
			}
			else
			{
				ASDA_CTRACE_IF(AsdaCtWorld::Ours(Mn ? Mn->NetRenderId : -1, VictimUidx), EAsdaCT::HitFrame,
					TEXT("кадр удара моба %s по игроку uidx=%d: результат %s — жертвы уже нет в мире, цифры нет"),
					*AsdaCtWorld::Mob(Mn ? Mn->NetRenderId : -1, Mn), VictimUidx, *AsdaCtWorld::Hit(Dmg));
			}
#endif
			if (AActor* V = WeakV.Get())
			{
				ShowPlayerHit(V, Dmg, Ht, VictimUidx, Mn ? Mn->MobName : FString());
				AsdaMobHitVictimReaction(GetGameInstance(), V, Dmg, Ht, VictimUidx, Mn);   // ★ 29.10.2026: вздрагивание / барьер / боевая стойка
				// ★ 25.10.2026 [ОРИГИНАЛ]: `Combat_ApplyDamage` (combat_pvp.c:7799/7889) пишет жертве `[1].netWord54 = renderId моба`;
				//   у своего это «в бою» для веток ESC (`objStateAllowsRender`). Мгновенная ветка флинча (выше) поля не пишет — как эталон.
				if (Mn && V == LocalPlayerActor) { AsdaEscChain::NoteSelfHitByMob(Mn->NetRenderId, Dmg); }
			}
			if (AAsdaMobActor* Mm = WeakM2.Get()) { ApplyReflectSkillDamage(Mm, ReflSkill, ReflDmg, VictimUidx); }
		}, SwingLen * 0.45f, false);
	}
	// ★★★ ВТОРОЙ КАНАЛ СИНХРОНИЗАЦИИ — снап при расхождении ≥3 ТАЙЛОВ (эталон, network.c:37729-37747):
	//     v20 = CClass_SetField177(target, …);                       // текущая позиция сущности
	//     tileDx = |helper_scaleIfNonzero(v20.x) − pkt@7|;           // в ТАЙЛАХ, по каждой оси
	//     tileDz = |ratioIfFieldSet(v20.z)      − pkt@9|;
	//     charSetTerrainCellIndex(target, тайл(@7,@9));              // всегда
	//     if (tileDx >= 3 || tileDz >= 3) playerSetWorldPosition(target, тайл(@7,@9));
	//   Гейта «не для мобов» тут НЕТ — kind=3 снапит и моба. Состояние при этом НЕ меняется
	//   (никакого SetActionState), поэтому здесь именно NetSnapPos, а не NetStop: прежний
	//   NetStop дополнительно гасил движение и сбивал цель — отсебятина.
	const FVector L = M->GetActorLocation();
	const FIntPoint NowTile = AsdaCoord::GameToTile(AsdaCoord::UeToGame2D(L));   // ★ Аудит №3 (B1/B2)
	const int32 NowTx = NowTile.X;
	const int32 NowTz = NowTile.Y;
	if (FMath::Abs(NowTx - H.CurTile.X) >= 3 || FMath::Abs(NowTz - H.CurTile.Y) >= 3)
	{
		UE_LOG(LogAsdaWorld, Log, TEXT("[моб→] 4015 kind=3 rid=%d '%s' СНАП ≥3 тайлов (эталон): тайл наш=(%d,%d) → пакета=(%d,%d)"),
			H.TargetRid, *M->MobName, NowTx, NowTz, H.CurTile.X, H.CurTile.Y);
		M->NetSnapOrGlide(CurG);   // ★ 07.11.2026 [ИСПРАВЛЕНИЕ]: asda.Mob.SnapAsGlide 0 — мгновенно
	}
	// ★ Эталон зовёт charSetTerrainCellIndex(cur) ВСЕГДА, до проверки порога — запоминаем клетку.
	M->RememberServerCell(CurG);
}

// ★★★ `SkillRecord->buff0SkillId` — id СОСТОЯНИЯ, которое навык вешает первым слотом.
//   Смещение `.sif +0x48` (i16) снято в `Reports/skill_fx_anim.md:1429` и подтверждено якорями
//   (навык 787 → состояние 169, 784 → 185, 786 → 104; `Reports/skill_system_final_verify.md:360`).
//   Соседние поля той же записи порт уже читает по этой же раскладке: +0x46 buff0DamageType,
//   +0x47 buff0SubType (`UAsdaEffectsSubsystem::EnsureSkillMetaLoaded`).
// ★ Аудит №3 (A12/A13, доведено): поле берётся у `UAsdaSkillDatabase` — она разбирает ту же запись
//   `skill_info` один раз при подъёме и держит её в `FAsdaSkillDef`. Здесь была ТРЕТЬЯ читалка того
//   же файла (своя карта `skillId → +0x48`), заведённая только потому, что база лежит в чужом каталоге.
// Возврат false = записи навыка в `skill_info` НЕТ (эталон: `if (!pSkillRecord) return`).
//   Отдельный флаг, а не сентинел −1: −1 — совершенно законное значение поля («у навыка нет
//   состояния в слоте 0»), и путать его с «строки нет» нельзя.
static bool AsdaSkillBuff0StateId(UGameInstance* GI, int32 SkillId, int32& OutStateId)
{
	const UAsdaSkillDatabase* Db = GI ? GI->GetSubsystem<UAsdaSkillDatabase>() : nullptr;
	const FAsdaSkillDef* D = Db ? Db->Get(SkillId) : nullptr;
	if (!D) { return false; }
	OutStateId = D->Buff0StateId;
	return true;
}

// ══════════════════════════════════════════════════════════════════════════════════════════════════
// ★★★ Д4 — ОТРАЖЕНИЕ УРОНА / КОНТРУДАР. Порт `cclassApplyReflectSkillDamage@0xF8A970`
//   (game.c:581532-581686). До этой правки поля @31/@33 не читались вовсе: моб не терял HP от
//   рефлекта, не получал наложенный баф и не показывал цифру — полоса моба расходилась с серверной.
//   Вход у эталона (game.c:576977-576981, покадровый тик):
//       reflectFlag = ent.word[9346];                        // @31 пакета
//       if (reflectFlag < 0) {                               // бит 0x8000 = «применить»
//           ent.word[9346] &= ~0x8000;                       // одноразовость
//           cclassApplyReflectSkillDamage(ent, skillId = ent.word[9346], a4 = ent[4674],
//                                         a5 = ent.word[3498]); }
//   `ent.word[3498]` — то же слово, что `ent[1749]` (3498·2 == 1749·4 == 6996), т.е. userIdx игрока.
//   Тело эталона:
//       rec      = CSkillMgr_GetSkillRecord(skillId);            если нет — выход
//       stateId  = rec->buff0SkillId;                            // .sif +0x48, i16
//       recBuff  = CSkillMgr_GetSkillRecord(stateId);            если нет — выход
//       buff_ApplySkillBuff(МОБ, stateId, userIdx, rec->skillCode);   // состояние вешается НА МОБА
//       switch (recBuff->buff0SubType) {                          // .sif +0x47
//         3: dmg = a4;                                            // как есть
//         5: dmg = a4 * (стата ОТРАЖАЮЩЕГО / 2) / 100;            // buff0DamageType 1 → word569, 2 → word571
//            ★ стату берёт CharManager_FindByUserIdx(a5) — a5 это userIdx ЖЕРТВЫ удара моба, т.е.
//              носителя баффа отражения (чаще всего ЛОКАЛЬНОГО игрока). Имя `pAttackerChar` в
//              декомпиле — мисномер. Слова 569/571 = CCharacter+1138/+1142 = макс. физ./маг. атака;
//              каналы доставки — 4060 (свой) и 4036 (любой userIdx, включая соседа). Подробности
//              и доказательства — `Reports/reflect_subtype5.md`.
//         7: dmg = cclassCalcPercentOfStat(МОБ, a4) = a4% ТЕКУЩЕГО HP моба; }
//       CClass_SubtractStatClamped(МОБ, dmg, 0);                  // ent[283] −= dmg, кламп в 0
//       FloatingText_Spawn(стиль 1, "%d") над МОБОМ + нотис 20013 в чат
//   ⚠ Все три ветки требуют `a4 > 0` — при нулевом/отрицательном @33 не делается НИЧЕГО (кроме уже
//     наложенного состояния).
// ⚠ ОДНО РАСХОЖДЕНИЕ С ДЕКОМПИЛОМ, зафиксировано: в ветке подтипа 7 декомпил показывает
//   `CClass_SubtractStatClamped` + `Entity_GetScreenPos` + `sprintf` ДВАЖДЫ подряд (game.c:581651-581657).
//   Вычитаем ОДИН раз: двойное вычитание удвоило бы урон против серверной полосы, а сам блок
//   выглядит дублем хвоста базового блока в выводе Hex-Rays. Отмечено в отчёте как непроверяемое
//   без живого прогона (подтип 7 в живом логе 2026-08-11 не встречался).
// ══════════════════════════════════════════════════════════════════════════════════════════════════
void UAsdaWorldSubsystem::ApplyReflectSkillDamage(AAsdaMobActor* M, int32 ReflectSkillIdRaw,
                                                  int32 ReflectDamage, int32 AttackerUserIdx)
{
	// Эталон: применять только при взведённом бите 0x8000 (`reflectFlag < 0` у __int16).
	if (!M || (ReflectSkillIdRaw & 0x8000) == 0) { return; }
	const int32 SkillId = ReflectSkillIdRaw & 0x7FFF;   // эталон: `word9346 &= ~0x8000`
	UGameInstance* GI = GetGameInstance();
	UAsdaEffectsSubsystem* Fx = GI ? GI->GetSubsystem<UAsdaEffectsSubsystem>() : nullptr;
	if (!Fx) { return; }
	int32 StateId = -1;   // .sif +0x48 i16 — id СОСТОЯНИЯ (0..349), которое навык вешает слотом 0
	if (!AsdaSkillBuff0StateId(GI, SkillId, StateId))
	{
		// Эталон: `if (!pSkillRecord)` — тихий выход. Но немой отброс неотличим от «поле не пришло»,
		//   поэтому пишем след (закон «нет» = ошибка метода).
		UE_LOG(LogAsdaWorld, Warning,
			TEXT("[рефлект] rid=%d: навыка отражения %d нет в skill_info — эталон здесь выходит без действия"),
			M->NetRenderId, SkillId);
		return;
	}
	if (StateId < 0 || StateId >= 350)
	{
		UE_LOG(LogAsdaWorld, Warning, TEXT("[рефлект] rid=%d навык=%d: buff0 = %d вне [0,350) — состояние не вешаем"),
			M->NetRenderId, SkillId, StateId);
		return;
	}
	const FAsdaSkillEffectMeta& BuffMeta = Fx->GetSkillMeta(StateId);
	if (!BuffMeta.bValid)
	{
		// Эталон: второй `CSkillMgr_GetSkillRecord` не нашёл запись ⇒ ни баффа, ни урона.
		UE_LOG(LogAsdaWorld, Warning, TEXT("[рефлект] rid=%d навык=%d: записи состояния %d нет — выход (эталон)"),
			M->NetRenderId, SkillId, StateId);
		return;
	}
	// Состояние вешается НА МОБА — эталон `buff_ApplySkillBuff(pVictimChar = моб, stateId, userIdx, skillCode)`.
	//   Порт мобов держит их состояния по renderId (у сущности нет userIdx — закон charid-vs-usernum).
	//   Длительность в этом канале эталон не передаёт вовсе (см. AddOrUpdateMobSkill: 0 = «без срока»).
	Fx->AddOrUpdateMobSkill(M->NetRenderId, StateId, /*Stack=*/0, /*DurationSec=*/0, AttackerUserIdx);
	ASDA_CTRACE_IF(AsdaCtWorld::Ours(M->NetRenderId, AttackerUserIdx), EAsdaCT::Fx, TEXT("отражение: на %s повешено состояние %d (buff0 навыка %d, подтип %d) от uidx=%d; величина @33=%d%s"),
		*AsdaCtWorld::Mob(M->NetRenderId, M), StateId, SkillId, BuffMeta.Buff0SubType, AttackerUserIdx, ReflectDamage,
		(ReflectDamage <= 0) ? TEXT(" — урона нет (эталон: только при @33 > 0)") : TEXT(""));
	if (ReflectDamage <= 0) { return; }   // эталон: все три ветки под `if (a4 > 0)`
	int32 Dmg = 0;
	switch (BuffMeta.Buff0SubType)
	{
	case 3:   // прямое отражение: величина из пакета как есть
		Dmg = ReflectDamage;
		break;
	case 5:   // процент от статы отражающего игрока: `reflectDmg = a4 * (стата/2) / 100`
	{
		// ★★★ РЕШЕНО 2026-08-12 (прежняя запись «данных нет» — О3 отчёта net_4015_holes_fix.md —
		//   была ЛОЖНЫМ ОТРИЦАНИЕМ; закон false-negatives-method).
		// КТО ТАКОЙ `pAttackerChar`. Имя в декомпиле — мисномер. Эталон
		//   (`cclassApplyReflectSkillDamage@0xF8A970`, game.c:581616) берёт
		//   `CharManager_FindByUserIdx(a5)`, где `a5` = `ent.word[3498]` = userIdx ЖЕРТВЫ УДАРА
		//   МОБА, то есть ИГРОКА-ОТРАЖАТЕЛЯ (носителя баффа), а вовсе не «нападавшего». В
		//   подавляющем большинстве случаев это ЛОКАЛЬНЫЙ игрок — его боевой блок клиент получает
		//   всегда (4060). Тем же полем эталон дальше проверяет `pAttackerChar[47565]`
		//   (= байт +190260, флаг «этот объект — локальный игрок», см. `SkillDamageTextStyle`).
		// КАКИЕ ПОЛЯ. `buff0DamageType` берётся у записи САМОГО НАВЫКА (`pSkillRecord`, .sif +0x46),
		//   а `buff0SubType` — у записи СОСТОЯНИЯ (`pReflectSkillRecord`, .sif +0x47); в эталоне это
		//   ДВЕ РАЗНЫЕ записи, путать нельзя (game.c:581619/581621 против :581604).
		//     1 → `*((__int16*)char + 569)` = `CCharacter+1138` = **макс. физ. атака**
		//     2 → `*((__int16*)char + 571)` = `CCharacter+1142` = **макс. маг. атака**
		//   Смысл пар доказан `charCalcScaledValueByStatRange@0xA89410`: `(a1[568]+a1[569])/2` —
		//   физическая ветка типов 1/4/5/8, `(a1[570]+a1[571])/2` — магическая 2/3/6/11.
		// ОТКУДА ЧИСЛА В ПОРТУ. `UAsdaNetSubsystem::CombatStatsByUserIdx` — заполняется 4060
		//   (свой, `RecvPacket_CharStatBlockA@0xB93E30`) и 4036 (любой userIdx, включая соседа,
		//   `Recv_LevelUp@0xF424C0`; сервер шлёт 4036 всему сектору через
		//   `SendPacket_ClippingSectorInOtherUser`). Подробный вывод каналов — в шапке структуры.
		UAsdaNetSubsystem* Net = GI->GetSubsystem<UAsdaNetSubsystem>();
		const UAsdaNetSubsystem::FAsdaCombatStats* CS = Net ? Net->GetCombatStats(AttackerUserIdx) : nullptr;
		if (!CS)
		{
			// Соседу, не поднявшему уровень у нас на глазах, боевой блок не приходит НИ ОДНИМ
			//   каналом — и в оригинале тоже: там поля нули после `memset(&pChar->maxHp, 0, 0x44)`
			//   (`game.c:79354`) и отражение считает 0. Дефект оригинала; воспроизводим численно
			//   (урона нет), но пишем след, а не молчим.
			UE_LOG(LogAsdaWorld, Warning,
				TEXT("[рефлект] rid=%d навык=%d состояние=%d подтип 5: боевого блока игрока uidx=%d ")
				TEXT("ещё не было (каналы 4060/4036) — в оригинале там нули, урон 0"),
				M->NetRenderId, SkillId, StateId, AttackerUserIdx);
			return;
		}
		// `buff0DamageType` записи НАВЫКА (не состояния) — та же таблица skill_info, что и выше.
		const int32 DamageType = Fx->GetSkillMeta(SkillId).Buff0DamageType;
		int32 StatHalf = 0;
		if (DamageType == 1)      { StatHalf = CS->PhysAtkMax / 2; }   // эталон: word569 / 2
		else if (DamageType == 2) { StatHalf = CS->MagAtkMax  / 2; }   // эталон: word571 / 2
		else
		{
			// ⚠ ДЕФЕКТ ОРИГИНАЛА, ИСПРАВЛЕН (закон fix-original-defects): при buff0DamageType вне
			//   {1,2} эталон оставляет `reflectDmg` НЕИНИЦИАЛИЗИРОВАННЫМ (обе `if`-ветки мимо) и
			//   умножает на мусор со стека. Берём 0 и выходим — вместо случайного урона.
			UE_LOG(LogAsdaWorld, Warning,
				TEXT("[рефлект] rid=%d навык=%d подтип 5: buff0DamageType=%d вне {1,2} — у эталона тут ")
				TEXT("неинициализированный множитель (дефект оригинала), берём 0"),
				M->NetRenderId, SkillId, DamageType);
			return;
		}
		// Эталон: `reflectDmg = (unsigned __int64)((double)a4 * (double)reflectDmg / 100.0)` —
		//   сначала целочисленная половина статы, затем умножение на @33 и деление на 100 в double.
		Dmg = (int32)((double)ReflectDamage * (double)StatHalf / 100.0);
		UE_LOG(LogAsdaWorld, Log,
			TEXT("[рефлект] подтип 5: игрок uidx=%d, тип урона %d, стата %d (блок из %s), половина %d, ")
			TEXT("@33=%d ⇒ отражено %d"),
			AttackerUserIdx, DamageType, (DamageType == 1) ? CS->PhysAtkMax : CS->MagAtkMax,
			*CS->Src, StatHalf, ReflectDamage, Dmg);
		break;
	}
	case 7:   // процент от ТЕКУЩЕГО HP моба — `cclassCalcPercentOfStat@0xF8A660`: a4/100 * ent[283]
		Dmg = (int32)((double)ReflectDamage / 100.0 * (double)M->CurHp);
		Dmg = FMath::Clamp(Dmg, 0, FMath::Max(0, M->CurHp));
		break;
	default:  // прочие подтипы эталон не обрабатывает вовсе (ни одной ветки switch)
		UE_LOG(LogAsdaWorld, Verbose, TEXT("[рефлект] rid=%d навык=%d состояние=%d подтип %d — эталон эту ветку не обрабатывает"),
			M->NetRenderId, SkillId, StateId, BuffMeta.Buff0SubType);
		return;
	}
	// ⚠ ОТСТУПЛЕНИЕ ПОРТА, зафиксировано: у эталона проверки `dmg > 0` ПОСЛЕ расчёта нет ни в одной
	//   из трёх веток — при нулевом результате он вычитает 0 и рисует над жертвой цифру «0»
	//   (и строку 20013 с нулём). Нулевую цифру не показываем: она бывает только когда данных о
	//   множителе нет, и «0» над мобом читается как баг, а не как механика. Причина отказа при этом
	//   всегда попадает в лог выше — немого отброса нет.
	if (Dmg <= 0)
	{
		ASDA_CTRACE_IF(AsdaCtWorld::Ours(M->NetRenderId, AttackerUserIdx), EAsdaCT::Damage, TEXT("отражение по %s: навык %d подтип %d — расчётный урон %d ≤ 0, HP не меняется, цифры нет"),
			*AsdaCtWorld::Mob(M->NetRenderId, M), SkillId, BuffMeta.Buff0SubType, Dmg);
		return;
	}
	// `CClass_SubtractStatClamped(моб, dmg, 0)` = `ent[283] -= dmg` с клампом в 0 — ЛОКАЛЬНОЕ вычитание,
	//   а не серверный снимок, поэтому идём тем же путём, что `ShowHitResult` (прямое поле + полоса), а
	//   НЕ через `ApplyMobHp`: там стоит фильтр устаревшего СНИМКА (`IsServerHpSnapshotStale`), которому
	//   локальный урон не подчиняется.
	// ⚠ Смерти отсюда не наступает и у эталона: рефлект правит `ent[283]`, а валит сущность покадровый
	//   `entityListUpdateFade@0xF78660` по ДРУГОМУ полю — `ent[284]` (HP из пакета).
	const int32 NewHp = FMath::Max(0, M->CurHp - Dmg);
	ASDA_CTRACE_IF(AsdaCtWorld::Ours(M->NetRenderId, AttackerUserIdx), EAsdaCT::Damage, TEXT("ОТРАЖЕНИЕ по %s: навык %d → состояние %d (подтип %d), отражает uidx=%d, урон=%d, HP полосы %d → %d / %d (локальный вычет)"),
		*AsdaCtWorld::Mob(M->NetRenderId, M), SkillId, StateId, BuffMeta.Buff0SubType, AttackerUserIdx, Dmg, M->CurHp, NewHp, M->MaxHp);
	M->CurHp = NewHp;
	UpdateMobHpBar(M);
	// ★ 17.10.2026 (Ф5) [ОРИГИНАЛ]: цифра рефлекта — «отражаю я» или режим показа 1 (game.c:581577, `CClass_GetField10112 == 1`).
	UAsdaNetSubsystem* NetRf = GI->GetSubsystem<UAsdaNetSubsystem>();
	if (UAsdaFloatingTextSubsystem* FT = GI->GetSubsystem<UAsdaFloatingTextSubsystem>();
		FT && ShouldShowDamageText(NetRf && NetRf->IsSelfUserIdx(AttackerUserIdx)))
	{
		FT->SpawnOverActor(0x1, FString::FromInt(Dmg), M);
		ASDA_CTRACE_IF(AsdaCtWorld::Ours(M->NetRenderId, AttackerUserIdx), EAsdaCT::DmgText, TEXT("цифра «%d» стиль 1 над %s (отражение)"), Dmg, *AsdaCtWorld::Mob(M->NetRenderId, M));
	}
	// ★★★ СТРОКА В ЧАТ — нотис 20013, ТОЛЬКО когда отражает ЛОКАЛЬНЫЙ игрок. Эталон во всех трёх
	//   ветках (3/5/7) делает одно и то же и в одном и том же порядке (game.c:581577-581586,
	//   :581629-581638, :581664-581673):
	//       if ( pAttackerChar[47565] )                       // +190260 — «этот объект = локальный игрок»
	//       {   FloatingText_Spawn(поз жертвы, стиль 1, "%d");
	//           CObject_SetSlotString(ctx, 0, имя ЖЕРТВЫ);    // CClass_GetResourceFromField(pVictimChar)
	//           CClass_SetSlotPair  (ctx, 0, dmg);
	//           CChat_AddTokenizedLines(UI_FormatLocalizedNotice(20013), -1, 10000, 0); }
	//       else if ( CClass_GetField10112(&g_InventoryBag) == 1 )   // настройка «чужой урон»
	//       {   FloatingText_Spawn(поз жертвы, стиль 1, "%d"); }     // ← цифра есть, строки НЕТ
	//   Жертва здесь — МОБ, поэтому слот-строка = его имя. Цвет −1 = белый, TTL 10 000 мс, дорожка A —
	//   те же параметры, что у канала 4102 (см. `Damage4102TextStyle` ниже).
	//   Цифру над мобом порт рисует в обеих ветках (настройки «показывать чужой урон» у нас нет —
	//   отступление уже зафиксировано в блоке `ApplyCombatHitKind3`); различается только чат-строка.
	{
		UAsdaNetSubsystem* NetSelf = GI->GetSubsystem<UAsdaNetSubsystem>();
		const bool bReflectorIsMe = NetSelf && NetSelf->IsSelfUserIdx(AttackerUserIdx);
		if (bReflectorIsMe)
		{
			// ★ R5 (12.11.2026): формат и отправка — общая `AsdaCombatChat::Line`; предупреждение о пустом нотисе — как было.
			if (AsdaCombatChat::Line(GI, 20013, { M->MobName }, { Dmg }, UAsdaChatSubsystem::ColWhite()) == EAsdaCombatChat::EmptyLine)
			{
				UE_LOG(LogAsdaWorld, Warning, TEXT("[рефлект] нотис 20013 пуст в таблице текста — строка в чат не пошла"));
			}
		}
	}
	UE_LOG(LogAsdaWorld, Log,
		TEXT("[рефлект] rid=%d '%s': навык %d → состояние %d (подтип %d), отражено %d ед. — HP %d ")
		TEXT("(эталон cclassApplyReflectSkillDamage@0xF8A970)"),
		M->NetRenderId, *M->MobName, SkillId, StateId, BuffMeta.Buff0SubType, Dmg, NewHp);
}
