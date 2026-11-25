// Copyright (c) 2026 @sitxovski (Discord)
// AsdaWorldSubsystem_CombatAnim.cpp — часть `AsdaWorldSubsystem_Hit.cpp`: OnCombatAnim (117 КБ, одна функция), коррекция клетки моба, полоса HP, проверка снаряда.
//   ★ R2 рефакторинга (10.11.2026): определения перенесены из `AsdaWorldSubsystem_Hit.cpp` ДОСЛОВНО (сверка `Tools/asda_split_check.py`);
//   общие помощники и объявления — `AsdaWorldSubsystemHitInternal.h`. Шапка системы и ссылки на эталон — в `AsdaWorldSubsystem_Hit.cpp` и заголовке класса.

#include "World/AsdaWorldSubsystem.h"
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
// ★ системный чат CHAT_NEW_SYSTEM + оверлеи A/B — все лут-строки   ★ R3: комментарий снятого включения "UI/AsdaChatSubsystem.h" (файл им не пользовался) — сохранён
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
#include "Items/AsdaItemTableSubsystem.h"    // тип оружия атакующего (импакт battk01..04)
#include "UI/AsdaInventorySubsystem.h"       // надетое (контейнер 3): проверка сломанных вещей
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
// ★ С-Р1: общий офлайн-гейт отладочных команд (RefuseDebugWhenOnline)   ★ R3: комментарий снятого включения "AsdaOutgoingGate.h" (файл им не пользовался) — сохранён
#include "World/AsdaCombatTrace.h"           // ★ 18.10.2026: журнал боя (ASDA_CTRACE) — полный цикл действий клиента в бою
#include "World/AsdaBlendAnimInstance.h"     // ★ 29.10.2026: вздрагивание персонажа от удара моба (AsdaAnimSmooth::StartHitFlinch)
#include "AsdaWorldSubsystemInternal.h"   // ★ 23.10.2026 разнос: общее для частей AsdaWorldSubsystem*.cpp (LogAsdaWorld, журнал боя, чистые хелперы)
#include "AsdaWorldSubsystemHitInternal.h"   // ★ R2: общие помощники и объявления частей AsdaWorldSubsystem_Hit*.cpp (после включений пролога)

// ★ Возврат к прежней заплатке «считать число показанных ударов по дельте здоровья».
//   0 = как в эталоне: ровно StepCount ударов за замах, здоровье не спрашиваем (см. место чтения).
static ASDA_DEV_CVAR(int32, CVarLegacyHpDeltaHeuristic,
	TEXT("asda.Combat.LegacyHpDeltaHeuristic"), 0,
	TEXT("1 = число показанных ударов считать по дельте здоровья (прежняя заплатка); 0 = эталон."),
	ECVF_Default);

// ★ Отложенное применение остатка здоровья цели из пакета замаха (см. место использования).
//   1 = применять на кадре удара (наше исправление дефекта оригинала), 0 = эталон: не применять.
static ASDA_DEV_CVAR(int32, CVarHpSnapshotOnHitFrame,
	TEXT("asda.Combat.HpSnapshotOnHitFrame"), 1,
	TEXT("1 = остаток HP цели из пакета замаха применяется на кадре удара; 0 = эталон (не применяется)."),
	ECVF_Default);
static ASDA_DEV_CVAR(int32, CVarAsdaOwnHitsFifo,
	TEXT("asda.Combat.OwnHitsFifo"), 1,
	TEXT("1 (по умолчанию) = удары нового ответа своего замаха ставятся ПОСЛЕ уже занятых кадров удара (одно значение на кадр, эталон ")
	TEXT("entity_char.c:829); 0 = прежний план «h-й ближайший кадр от сейчас» (двойной старт серии — по две цифры на кадр)."),
	ECVF_Default);

// Применено ли сервером значение СВОЕЙ очереди с номером Ordinal в серии Gen (или серия ещё идёт). OutWhy — причина отказа.
static bool AsdaOwnHitServerLive(UGameInstance* GI, uint32 Gen, int32 Ordinal, const TCHAR** OutWhy)
{
	*OutWhy = TEXT("");
	if (CVarAsdaOwnHitsServerCommit.GetValueOnGameThread() == 0 || Gen == 0 || Ordinal < 0 || !GI) { return true; }
	const UAsdaAttackRhythmSubsystem* Rh = GI->GetSubsystem<UAsdaAttackRhythmSubsystem>();
	if (!Rh) { return true; }
	const int32 Committed = Rh->ServerCommittedEntries(Gen);   // сколько значений серии сервер применил (−1 — серия забыта)
	if (Committed >= 0 && Ordinal < Committed) { return true; }
	// Серия брошена только по СЕТЕВЫМ фактам: новый 4026/4201 (сервер пересчитал очередь — хвост выброшен) или ход 4006 не к цели
	//   (сервер в ходу, следующий удар — новая серия). Подход к убежавшей цели серию не бросает — по возвращении 4028 спишет эти
	//   значения; смерть цели гасит удары после смертельного своим правилом (`asda.Combat.DeathAtLastPendingHit`).
	if (Gen != Rh->GetSeriesGen()) { *OutWhy = TEXT("серия заменена новым 4026 — сервер этот хвост очереди выбросил"); return false; }
	if (Rh->IsSeriesAbandoned(Gen)) { *OutWhy = TEXT("ход не к цели до удара, который применил бы значение"); return false; }
	return true;   // серия жива — следующий принятый 4028 применит значение
}

// ── ★ 4027 case 1: очередь ударов замаха → цифры над целью-мобом с раскладкой по времени.
//   (Эталон вяжет цифры к кадрам клипа атаки атакующего — интервал 0.35с = приближение.) ──
// ★ СЕНТИНЕЛЫ ОЧЕРЕДИ УРОНОВ 1:1 (эталон entity_char.c:26912-26965, switch по dmgFromQueue):
//     -1 → DODGE;   -2 и 0 → НИЧЕГО (условие `dmgFromQueue != -2 && dmgFromQueue`);
//     -3/-4/-5 → IMMUNE;   -6 → MISS;   всё остальное — урон (бит 31 = крит).
//   Порт раньше считал MISS-ом любое значение <= 0 — из-за этого -2 («пусто») рисовал MISS, а -6
//   (настоящий MISS) не отличался от -4/-5 (IMMUNE).
// ★ Диапазон расширен до −7 (24.08.2026): код −7 существует и обрабатывается эталоном в
//   `Skill_SpawnHitFeedback` (skill_buff.c:445-456). Без этого он попадал в ветку урона, где бит
//   31 отрицательного числа читается как крит, и над целью печаталось 2 147 483 641.
static bool AsdaQueueIsSentinel(int32 V) { return V == 0 || (V >= -7 && V <= -1); }
// Урон, который несёт элемент очереди (сентинелы не несут; крит — бит 31 на значении).
static int32 AsdaQueueDamage(int32 V) { return AsdaQueueIsSentinel(V) ? 0 : (V & 0x7FFFFFFF); }

// ═══ ★★★ ЖИВАЯ КЛЕТКА МОБА ОТ NpcServer → ВИДИМАЯ ПОЗИЦИЯ (15.10.2026, волна mobpos_swing, `wave_PORT_mobpos_swing.md` §1) ═══
//   Живой случай 15.10 (карта rein, GapeCrab rid=212): при входе на карту бёрст 4013 принёс запись «действие 2 (ход)
//   cur=(164,403) dst=(170,403)»; клиент, как и эталон (`Recv_MobSpawn` case 1 → `projectileInitTrajectory`), довёл моба до
//   (170,403). Сервер же моба не двигал: ни одного 4015 по нему за 3.5 мин (рассылка 4015 при секторе 256 — вся карта,
//   `Send_SectorInUpdateMonster_NPC_Info@0x41D870`, радиуса нет), а живая клетка в каждом ответе 4027 и в 4017 — (164,403).
//   Кэш GameServer хранит последнюю тревогу «пошёл 164→170», а NpcServer по одному из «немых» путей (вечный MOVE без
//   тревоги — блок статусом `Move_Tile`, ошибка A* «%d Move() Error», `ProcessAction_Move sub_43E580`) моба не повёл.
//   Видимая дистанция 16 клеток против серверных 22 — шесть отказов 4029 код 1 «вне дальности» подряд.
//   Эталон живые клетки из 4027 (@5/@7) и 4017 (@96/@98, ветки без бита 4) не читает — у оригинала был бы тот же «призрак».
//   [ПОРТ] исправление: сверяем видимую позицию с живой клеткой по ЭТАЛОННОМУ правилу синхронизации `Recv_CombatHit` kind=3
//   («расхождение ≥ 3 клеток по любой оси → поставить на клетку пакета», network.c:37729-37747) и вдобавок гасим устаревшую
//   траекторию (иначе актор снова ушёл бы к её концу). Ниже порога — ничего (обычное отставание интерполяции ходьбы).
//   Боевой тик (`AAsdaPlayerController::AttackTick`) на следующем кадре видит настоящую дистанцию: вне дальности — эталонный
//   подход (4006 с номером цели) к СЕРВЕРНОЙ клетке и удар по прибытии; 4028 «в пустоту» больше не уходит.
static ASDA_DEV_CVAR(int32, CVarAsdaMobLiveCellSnap,
	TEXT("asda.Mob.LiveCellSnap"), 1,
	TEXT("1 (по умолчанию) = видимая позиция моба сверяется с живой клеткой NpcServer из 4027 (@5/@7) и 4017 (@96/@98): ")
	TEXT("расхождение ≥ 3 клеток → моб на клетку сервера и стоп (эталонный порог kind=3); 0 = как эталон (поля не читаются)."),
	ECVF_Default);

bool UAsdaWorldSubsystem::CorrectMobToServerCell(AAsdaMobActor* M, FIntPoint LiveCell, const TCHAR* Src)
{
	if (!M || M->IsDead() || CVarAsdaMobLiveCellSnap.GetValueOnGameThread() == 0) { return false; }
	if (LiveCell.X < 0 || LiveCell.Y < 0 || LiveCell.X > 4096 || LiveCell.Y > 4096) { return false; }   // мусор/нет поля
	const FVector2D CellG = AsdaCoord::TileCenterGame(LiveCell);
	const FVector WasL = M->GetActorLocation();
	const FIntPoint NowTile = AsdaCoord::GameToTile(AsdaCoord::UeToGame2D(WasL));
	M->NoteDesync(CellG);   // диагностика `[рассинхрон]`/`asda.Mobs.Dump` — та же метрика, что у 4015
	// [ПОРТ] Отступление от эталона (ревизия 15.10, НИЗК-8): клетка из 4017/4027 становится «запомненной серверной клеткой»
	//   (`ent[282]`), которую читает доводчик моба `CastDirectionalEffectIfFar` по концу замаха/флинча. У эталона её пишут
	//   только 4015 kind 3/5/7 и 4013 5/7 (`charSetTerrainCellIndex`). Клетка свежая (момент удара), kind=3 перед замахом всё
	//   равно перезапишет её — но без этой записи доводчик мог бы вернуть моба на СТАРУЮ клетку после нашей поправки.
	M->RememberServerCell(CellG);
	if (FMath::Abs(NowTile.X - LiveCell.X) < 3 && FMath::Abs(NowTile.Y - LiveCell.Y) < 3) { return false; }
	// ★ Ревизия 15.10 (СРЕДН-2): снап + стоп — ТОЛЬКО «призраку»: моб у клиента стоит, либо живая клетка совпадает (±1) с
	//   началом его текущей траектории (сервер моба не повёл — живой случай 15.10). Законно идущего, но отставшего/обогнавшего
	//   моба не останавливаем и не дёргаем: эталонная нормировка скорости — приходит в цель одновременно с сервером.
	const FIntPoint StartTile = AsdaCoord::GameToTile(M->GetMoveStartGame());
	// ★ 07.11.2026 [ИСПРАВЛЕНИЕ]: стоящий актор с отрезком сервера — не призрак: его снял флинч или стан, он добежит сам.
	//   Идущий короткий проход (удар при расхождении) сам ведёт моба на клетку: снап + стоп рвали бы замах.
	const bool bGhost = (!M->IsMoving() && !M->HasServerSegment() && !M->IsGliding())
		|| (FMath::Abs(StartTile.X - LiveCell.X) <= 1 && FMath::Abs(StartTile.Y - LiveCell.Y) <= 1);
	if (!bGhost)
	{
		if (!M->IsMoving())
		{
			// ★ 07.11.2026 [ИСПРАВЛЕНИЕ]: стоит с отрезком или в проходе — он дойдёт сам. Но под станом сервер стоит, и его клетка точна:
			//   разрыв ≥ 3 клеток (бег разошёлся до стана) гасим коротким проходом, иначе актор ждал бы снятия стана вне дальности ударов.
			const UAsdaEffectsSubsystem* Fx = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaEffectsSubsystem>() : nullptr;
			if (M->HasServerSegment() && !M->IsGliding() && !M->IsHitAnimActive() && Fx && Fx->HasMobMoveLock(M->NetRenderId)) { M->NetSnapOrGlide(CellG); }
			return true;
		}
		M->CatchUpToServerCell(CellG);
		UE_LOG(LogAsdaWorld, Log, TEXT("[моб→] %s rid=%d '%s' ЖИВАЯ КЛЕТКА сервера (%d,%d), видимая (%d,%d) — идёт к (%.0f,%.0f): без снапа, догоняет нормировкой скорости [ПОРТ]"),
			Src ? Src : TEXT("?"), M->NetRenderId, *M->MobName, LiveCell.X, LiveCell.Y, NowTile.X, NowTile.Y, M->GetMoveDstGame().X, M->GetMoveDstGame().Y);
		ASDA_CTRACE_MOB(M->NetRenderId, EAsdaCT::Range, TEXT("%s %s: живая клетка сервера (%d,%d), видимая (%d,%d) — моб идёт, догоняет без снапа"),
			Src ? Src : TEXT("?"), *AsdaCtWorld::Mob(M->NetRenderId, M), LiveCell.X, LiveCell.Y, NowTile.X, NowTile.Y);
		return true;
	}
	M->NetSnapPos(CellG);
	M->NetHalt();           // устаревшая траектория снята (гейт хита 13/14 сохраняет состояние — позиция уже поставлена)
	const FVector NowL = M->GetActorLocation();
	UE_LOG(LogAsdaWorld, Log, TEXT("[моб→] %s rid=%d '%s' ЖИВАЯ КЛЕТКА сервера (%d,%d), видимая была (%d,%d) — расхождение ≥3 клеток, призрак: поставлен на клетку сервера и остановлен (%.0f,%.0f)→(%.0f,%.0f) [ПОРТ]"),
		Src ? Src : TEXT("?"), M->NetRenderId, *M->MobName, LiveCell.X, LiveCell.Y, NowTile.X, NowTile.Y, WasL.X, WasL.Y, NowL.X, NowL.Y);
	ASDA_CTRACE_MOB(M->NetRenderId, EAsdaCT::Range, TEXT("%s %s: «призрак» — видимая клетка (%d,%d) разошлась с живой (%d,%d) на ≥3, моб поставлен на клетку сервера и остановлен (дистанция атаки пересчитается)"),
		Src ? Src : TEXT("?"), *AsdaCtWorld::Mob(M->NetRenderId, M), NowTile.X, NowTile.Y, LiveCell.X, LiveCell.Y);
	return true;
}

void UAsdaWorldSubsystem::OnCombatAnimTargetCell(int32 AttackerUserIdx, int32 TargetRid, FIntPoint LiveCell)
{
	TWeakObjectPtr<AAsdaMobActor>* Ex = NetMobs.Find(TargetRid);
	AAsdaMobActor* M = Ex ? Ex->Get() : nullptr;
	if (!M) { return; }
	CorrectMobToServerCell(M, LiveCell, TEXT("4027"));
	(void)AttackerUserIdx;
}

void UAsdaWorldSubsystem::OnCombatAnim(uint8 Case, int32 AttackerUserIdx, int32 TargetRid, const TArray<int32>& DmgQueue, int32 TargetHp)
{
	if (Case != 1)
	{
		ASDA_CTRACE_IF(AsdaCtWorld::Ours(TargetRid, AttackerUserIdx), EAsdaCT::Recv, TEXT("боевой пакет case %d атакующий=%d цель rid=%d — не замах (отказы ведёт OnCombatRefused), разбор очереди пропущен"),
			(int32)Case, AttackerUserIdx, TargetRid);
		return;
	}   // прочие case = отказы атаки (нотисы) — фаза боя-UI
	UAsdaNetSubsystem* Net = GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>();
	UWorld* W = GetGameInstance()->GetWorld();
	if (!Net || !W) { return; }
	const bool bOurs = Net->IsSelfUserIdx(AttackerUserIdx);   // ★ ноль — законный номер, см. IsSelfUserIdx
	// ★★ Ревизия 25.10 (фантомный урон, М-1): свой ответ — сверка с заявкой (до любых ранних выходов, иначе очередь заявок
	//   разойдётся с ответами). Ответы сервера идут по порядку заявок: 4029 — удар принят и очередь расходовалась, 4027 «первый
	//   урон» (8202, цель вне мгновенной дальности) — серия открыта, очередь не расходовалась. Поздний ответ ПРОШЛОЙ серии (после
	//   нового 4026) несёт очередь, которую сервер выбросил пересчётом: в свою очередь не копится и не показывается.
	bool bOwnStaleReply = false;
	if (bOurs)
	{
		if (UAsdaAttackRhythmSubsystem* RhR = GetGameInstance()->GetSubsystem<UAsdaAttackRhythmSubsystem>())
		{
			const bool bConsumed = Net->CombatAnimSrcOpcode == 4029;
			const uint32 ReplyGen = RhR->ResolveMobReply(bConsumed, bConsumed ? nullptr : TEXT("4027: цель вне мгновенной дальности"), TargetRid);
			bOwnStaleReply = ReplyGen != 0 && ReplyGen != RhR->GetSeriesGen() && CVarAsdaOwnHitsServerCommit.GetValueOnGameThread() != 0;
			if (bOwnStaleReply)
			{
				UE_LOG(LogAsdaWorld, Log, TEXT("[бой] rid=%d: свой ответ %d относится к прошлой серии — его %d значени(й) сервер выбросил новой серией, не показываются"),
					TargetRid, (int32)Net->CombatAnimSrcOpcode, DmgQueue.Num());
				ASDA_CTRACE(EAsdaCT::Queue, TEXT("свой ответ %d по rid=%d — ПОЗДНИЙ, серии #%u (сейчас #%u): очередь %s не копится и не показывается"),
					(int32)Net->CombatAnimSrcOpcode, TargetRid, ReplyGen, RhR->GetSeriesGen(), *AsdaCtWorld::Queue(DmgQueue));
			}
		}
	}
	// ★ 18.10.2026 (ревизия Ф4, ВАЖНО 1) [ОРИГИНАЛ]: атакующий бьёт моба — его PvP-серии больше нет (одна очередь у эталона).
	ResetPvpSeriesOnMobFight(AttackerUserIdx, Net->CombatAnimSrcOpcode == 4027 ? TEXT("4027: атакующий бьёт моба") : TEXT("4029: атакующий бьёт моба"));
	// Чужой бой без цели у нас — цифры некуда вешать; незнакомый атакующий → снапшот 5416.
	if (!bOurs && AttackerUserIdx > 0 && !NetPlayers.Contains(AttackerUserIdx) && !Net->RequestedUserInfo.Contains(AttackerUserIdx))
	{
		if (Net->SendAgainUserInfo((uint16)AttackerUserIdx)) { Net->RequestedUserInfo.Add(AttackerUserIdx); }   // ★ ревизия волны 1: в кэш — только отправленный (гейт перехода/обрыв не должны «залипать» номер)
	}
	TWeakObjectPtr<AAsdaMobActor>* Ex = NetMobs.Find(TargetRid);
	AAsdaMobActor* M = Ex ? Ex->Get() : nullptr;
	// ★ Журнал боя: приём ответа на замах (4029 case 0 / 4027 case 1 / код 2 соседа) — атакующий, цель, очередь, HP пакета.
	ASDA_CTRACE_IF(bOurs || AsdaCtWorld::Ours(TargetRid, AttackerUserIdx), bOurs ? EAsdaCT::Recv : EAsdaCT::Peer,
		TEXT("%d%s: атакующий uidx=%d (%s) → %s, очередь пакета %s, HP цели в пакете %d (полоса %d/%d)%s%s"),
		(int32)Net->CombatAnimSrcOpcode, bPeerCode2Append ? TEXT(" (отказ код 2 соседа — дописать)") : TEXT(""),
		AttackerUserIdx, bOurs ? TEXT("я") : TEXT("сосед"), *AsdaCtWorld::Mob(TargetRid, M), *AsdaCtWorld::Queue(DmgQueue), TargetHp,
		M ? M->CurHp : -1, M ? M->MaxHp : -1,
		!M ? TEXT(" — моба нет в мире") : (M->IsDead() ? TEXT(" — цель уже труп") : TEXT("")),
		(M && M->bServerDead) ? TEXT(", цель уже помечена обречённой") : TEXT(""));
	// ══ ★ АТАКУЮЩИЙ И ЕГО ЗАМАХ — СЧИТАЕТСЯ ДО И НЕЗАВИСИМО ОТ СОДЕРЖИМОГО ОЧЕРЕДИ ════════════════
	//   Эталон Recv_CastState@0xF41620 case 0: пакет 4029 — это ВХОД АТАКУЮЩЕГО В СОСТОЯНИЕ АТАКИ
	//   (CChar_SetState(caster, t, 5, target, 0)), и от размера очереди уронов он не зависит вообще.
	//   Раньше весь этот блок сидел внутри `if (M && Buf.Num() > 0)`, поэтому пакет-продолжение с
	//   пустой очередью не давал соседу замаха вовсе (юзер 2026-07-31).
	//   Тип оружия атакующего (клип + импакты battk01..04): свой — SelfWeaponItemId, чужой — его экип.
	int32 WeaponSub = -1;
	int32 WeaponItem = 0;
	if (bOurs) { WeaponItem = Net->SelfWeaponItemId; }
	else if (FAsdaNetPlayerEntry* PE = NetPlayers.Find(AttackerUserIdx)) { WeaponItem = PE->Parts.Weapon; }
	if (WeaponItem > 0)
	{
		if (UAsdaItemTableSubsystem* IT = GetGameInstance()->GetSubsystem<UAsdaItemTableSubsystem>())
		{
			if (const FAsdaItemRecord* R = IT->GetItem(WeaponItem)) { WeaponSub = R->ItemSubType; }
		}
	}
	// Свой перс и чужие игроки — один и тот же класс AAsdaPlayerCharacter.
	AAsdaPlayerCharacter* AtkCh = nullptr;
	if (bOurs) { AtkCh = Cast<AAsdaPlayerCharacter>(LocalPlayerActor); }
	else if (FAsdaNetPlayerEntry* PE = NetPlayers.Find(AttackerUserIdx)) { AtkCh = PE->Actor.Get(); }
	// ★ 17.10.2026 (синхронизация боя, Ф0): наблюдатель по эталону (ключ `asda.Combat.ObserverSync`, разбор у объявления).
	const bool bObserverSync = CVarAsdaObserverSync.GetValueOnGameThread() != 0;
	// ★ 17.10.2026 (Ф1): лента попаданий моба для этого атакующего (ключи отката — у `IsImpactLedgerOn`) и вид его попадания.
	const bool bLedger = IsImpactLedgerOn(bOurs);
	const EAsdaImpactKind ImpactKind = IsRangedWeapon(WeaponSub) ? EAsdaImpactKind::Projectile : EAsdaImpactKind::Attack;
	// ★★★ 17.10.2026 (синхронизация боя, Ф2): замах соседа от пакетов и цифры по плану кадров удара отрезка (разбор у
	//   `AAsdaPlayerCharacter::PeerAcceptSwingPacket`). Ключи: `asda.Combat.SwingPlanFromPackets` и (для соседа) `ObserverSync`.
	const bool bSwingPlan = AAsdaPlayerCharacter::IsSwingPlanFromPackets();                 // Ф2 включён
	const bool bPeerPlanMode = !bOurs && bObserverSync && bSwingPlan && AtkCh != nullptr;   // замах соседа ведут пакеты
	const bool bFrom4027Pkt = Net->CombatAnimSrcOpcode == 4027 && !bPeerCode2Append;        // первый удар серии вне дистанции
	AAsdaPlayerCharacter::FAsdaPeerSwingPlan PeerPlan;   // план отрезка этого 4029 соседа (кадры удара от «сейчас»)
	bool bHavePeerPlan = false;                          // план взят (принятый 4029 соседа по живой цели)
	bool bWantPeerAccept = false;                        // ★ ревизия Ф2 (ВАЖНО 1): принять отрезок ПОСЛЕ разбора признака серии
	bool bPeerNewSeries = false;                         // ★ ревизия Ф2 (ВАЖНО 1): пакет начал новую серию соседа (сброс очереди)
	// Свой замах: момент h-го удара — ближайший кадр удара плана (`UpcomingHitDelaySec`). Прежде — только под серверными часами и
	//   только ближний бой (ревизия СРЕДН-5); Ф2 снимает гейт: и без часов (ключ `ServerRhythm 0`), и дальнобойное — выстрел на
	//   кадре удара прохода, а не по приходу ответа в любой фазе. −1 — плана нет (вызывающий берёт прежний расчёт).
	auto OwnPlanDelay = [&](int32 H) -> float
	{
		if (!bOurs || !AtkCh) { return -1.f; }
		const bool bRangedOwn = IsRangedWeapon(WeaponSub);   // своё дальнобойное
		const bool bPlanOk = bSwingPlan ? true : (AtkCh->IsServerRhythmOwner() && !bRangedOwn);   // ключ 0 — прежний гейт
		float Up = bPlanOk ? AtkCh->UpcomingHitDelaySec(H) : -1.f;
		// ★ 17.10.2026 (ревизия Ф2, мелочь 1): своё дальнобойное — если до кадра выстрела по плану дольше длины текущего отрезка
		//   (ответ пришёл после выстрела этого прохода, следующий кадр — уже за точкой отправки), стрелять сразу: выстрел этого
		//   ответа принадлежит прошедшему кадру, а не следующему проходу.
		if (bRangedOwn && Up > 0.f && H == 0)
		{
			const float Seg = AtkCh->GetAttackSegmentPlannedSec();   // длина текущего отрезка (реальные с)
			if (Seg > 0.f && Up > Seg + 0.05f) { Up = 0.f; }
		}
		return Up;
	};
	// ★ ТАЙМИНГИ ИЗ combo_time (эталон): StepCount ударов за замах, каждый на своём кадре (трек B),
	//   полная длительность = окно комбо. Длина UE-клипа НЕ используется — из-за неё цифры бежали
	//   быстрее, чем сервер считал урон (рассинхрон, юзер 2026-07-29).
	AAsdaPlayerCharacter::FAsdaComboTiming CT;
	float SwingLen = 0.f;
	if (AtkCh)
	{
		CT = AtkCh->GetComboTiming(WeaponSub);
		// ★★★ ТЕМП АТАКИ: `CT.TotalSec` приходит уже поделённым на множитель темпа, а запасная
		//   ветка (строки combo_time нет) берёт длину клипа в его собственном времени — её надо
		//   поделить здесь, иначе на ускоренном темпе запасной путь считал бы замах длиннее, чем он
		//   есть, и вся серия ударов расползалась бы по времени.
		SwingLen = CT.bValid ? CT.TotalSec
		                     : (AtkCh->AttackClipLen(WeaponSub) / FMath::Max(0.1f, AtkCh->AttackTempoMul()));
		// ★ ЗАМАХ СОСЕДА. Свой уже запущен по отправке 4026/4028 (эталон ставит state=5 оптимистично,
		//   ПРИ ОТПРАВКЕ: input.c:1640 и Player_UpdateAutoActionState@0xA249A0), поэтому здесь только чужой.
		//   Гейт `field_8 != 5` эталона (Recv_CastState:42051) = «не перезапускать уже идущий замах»;
		//   у нас признак идущего замаха — IsAttackAnimActive(). По ТРУПУ атака не начинается
		//   (Recv_CastState:41904 `if (Entity_GetActionState(target) == 10) return 1;`), а если цели нет
		//   в списке сущностей — сосед уходит в покой подтипа 20 (Recv_CastState:42286).
		if (!bOurs)
		{
			if (M && !M->IsDead())
			{
				// ★ РАЗВОРОТ СОСЕДА К ЦЕЛИ — 1:1 эталон: Recv_CastState:42059 пишет caster->targetRenderId
				//   (из списка сущностей, НЕ из координат пакета), а покадровый objAI_FaceTargetByUserIdx
				//   @0xA23350 доворачивает атакующего по dir = normalize(posЦели − posАтакующего). Без этого
				//   чужой игрок бил и стрелял «стоя спиной» (юзер 2026-08-02). Доворот живёт в Tick павна
				//   (AttackFaceTarget), здесь — цель + мгновенный первый разворот (в кадре пакета).
				AtkCh->AttackFaceTarget = M;
				const FVector TL = M->GetActorLocation();
				AtkCh->FaceGameDir(AsdaCoord::UeToGame2D(TL) - AtkCh->GetGamePosXZ());   // ★ Аудит №3 (B1)
				if (bPeerPlanMode)
				{
					// ★ Ф2: [ОРИГИНАЛ] 4027 case 1 соседа — только очередь и признак серии 8, БЕЗ замаха (network.c:40812–40819: у
					//   не-локального `field_40 == 0` → `LABEL_36`); замах начнёт следующий 4029 (ветка `&8`, состояние 5). Принятый
					//   4029 — отрезок клипа: старт с кадра 0 вне замаха, иначе в очередь (разбор у `PeerAcceptSwingPacket`).
					// ★ Ревизия Ф2 (ВАЖНО 1): сам приём — ниже, после разбора признака серии (новая серия снимает очередь отрезков).
					if (!bFrom4027Pkt) { bWantPeerAccept = true; }
				}
				else
				{
					// ★ Та же эталонная машина, что у своего перса: «взвод один раз → цикл» (гейт `field_8 != 5`
					//   внутри StartAttackSwing). У соседа контроллера нет — цикл держится ВРЕМЕНЕМ и продлевается
					//   каждым его 4029 (эталон: state 5 соседа живёт, пока идут пакеты/цель жива; выход по трупу
					//   — ветка else ниже, StopAttackAnim). Раньше здесь был PlayAttackClip → рестарт с 0 кадра.
					//   Ключ `asda.Combat.SwingPlanFromPackets 0` / `ObserverSync 0` — этот путь (как после Ф1).
					// ★ Ревизия Ф2 (мелочь 3): ключ переключили посреди пакетной серии — выйти из пакетного режима (отрезки, «тишина»),
					//   иначе `StartAttackSwing` лишь продлил бы удержание, а границы отрезков жили дальше.
					if (AtkCh->IsPeerPacketDriven()) { AtkCh->StopAttackAnim(); }
					AtkCh->StartAttackSwing(WeaponSub, FMath::Max(SwingLen, 1.0f) + 1.5f);
					ASDA_CTRACE_IF(bOurs || AsdaCtWorld::Ours(TargetRid, AttackerUserIdx), EAsdaCT::Peer, TEXT("сосед uidx=%d: замах по %s по ВРЕМЕНИ (ключ SwingPlanFromPackets/ObserverSync 0), удержание %.2f с, оружие %d"),
						AttackerUserIdx, *AsdaCtWorld::Mob(TargetRid, M), FMath::Max(SwingLen, 1.0f) + 1.5f, WeaponSub);
				}
			}
			else
			{
				ASDA_CTRACE_IF(bOurs || AsdaCtWorld::Ours(TargetRid, AttackerUserIdx), EAsdaCT::Peer, TEXT("сосед uidx=%d: цель %s %s — замах соседа погашен (эталон: по трупу атака не начинается)"),
					AttackerUserIdx, *AsdaCtWorld::Mob(TargetRid, M), M ? TEXT("уже труп") : TEXT("не найдена"));
				AtkCh->StopAttackAnim();
			}
		}
	}
	// ★ МЕТКА ТРУПА (эталон Recv_CastState:41907 `if (a2[1] <= 0 && (target[121] & 0x20) == 0)
	//   sub_92C3AD(target, 32)`): остаточный HP <= 0 → цель СРАЗУ помечена мёртвой для боевого цикла,
	//   но НЕ убита. HP уйдёт в 0 (и цель — в state 10) на кадре удара текущего замаха, ниже.
	if (M && TargetHp == 0)
	{
		ASDA_CTRACE_IF(bOurs || AsdaCtWorld::Ours(TargetRid, AttackerUserIdx), EAsdaCT::Death, TEXT("%s: HP в пакете 0 — метка «обречён» (bServerDead) СРАЗУ: новых замахов по цели нет; смерть и визуал — на кадре удара%s"),
			*AsdaCtWorld::Mob(TargetRid, M), M->bServerDead ? TEXT(" (метка уже стояла)") : TEXT(""));
		M->bServerDead = true;
	}
	// ★ ДИАГНОСТИКА МГНОВЕННОГО КИЛА: видно, что ветка клипа отработала даже когда цель умирает
	//   от первого же удара (TargetHp==0 при ещё играющем замахе).
	UE_LOG(LogAsdaWorld, Log, TEXT("[бой] атака: клип=%d цель=%d hp=%d очередь=%d атакующий=%s замах=%s (мгновенный кил: %s)"),
		AAsdaPlayerCharacter::AttackClipIdx(WeaponSub), TargetRid, TargetHp, DmgQueue.Num(),
		bOurs ? TEXT("свой") : TEXT("сосед"),
		(AtkCh && AtkCh->IsAttackAnimActive()) ? TEXT("играет") : TEXT("нет"),
		(TargetHp == 0) ? TEXT("да") : TEXT("нет"));
	ASDA_CTRACE_IF(bOurs || AsdaCtWorld::Ours(TargetRid, AttackerUserIdx), EAsdaCT::Swing, TEXT("%s uidx=%d: клип атаки %d (оружие %d), замах %s, позиция клипа %.3f с; combo_time: %s шагов=%d окно=%.3f с%s"),
		bOurs ? TEXT("свой") : TEXT("сосед"), AttackerUserIdx, AAsdaPlayerCharacter::AttackClipIdx(WeaponSub), WeaponSub,
		(AtkCh && AtkCh->IsAttackAnimActive()) ? TEXT("играет") : TEXT("не играет"), AtkCh ? AtkCh->GetAttackSwingElapsedSec() : 0.f,
		CT.bValid ? TEXT("есть") : TEXT("НЕТ строки"), CT.bValid ? CT.StepCount : -1, SwingLen,
		AtkCh ? TEXT("") : TEXT(" — актора атакующего нет у нас (замаха/выстрела не будет)"));
	// ★★★ БУФЕР УРОНОВ. Пришедшие значения ДОБАВЛЯЮТСЯ в очередь, а расходуются по одному за замах:
	//   сервер отдаёт очередь пачкой один раз, потом присылает пустые массивы и тратит её сам
	//   (проверено на живом потоке: модель «один результат за пакет» сошлась 38 раз из 41).
	//   ★ КЛЮЧ — АТАКУЮЩИЙ, а не цель (см. объявление `DmgBuffers`): так делает эталон, и так
	//   очередь не переживает смерть монстра и не смешивается с чужим боем по общей цели.
	TArray<int32>& Buf = DmgBuffers.FindOrAdd(AttackerUserIdx);
	// ★★★ 17.10.2026 (синхронизация боя, Ф0 → Ф1 по ревизии Ф0 п.1/п.8): ОЧЕРЕДЬ СОСЕДА ПО ПРИЗНАКУ СЕРИИ, КАК ЭТАЛОН.
	//   До Ф0 очередь соседа не сбрасывалась НИКОГДА и копилась даже без живой цели — хвост прошлого моба выводился цифрой на
	//   следующем. Ф0 брал «сосед в серии» из АНИМАЦИИ — у медленного оружия пропуск одного 4029 (сервер молча отбрасывает
	//   ранний 4028) гасил цикл и сбрасывал очередь посреди серии. Теперь — зеркало эталонного `field11C` (`PeerSeriesFlags`):
	//     [ОРИГИНАЛ] 4027 case 1 → сброс и наполнение, признак 8 (network.c:40916–40919, `LABEL_36`);
	//     [ОРИГИНАЛ] 4029 case 0: признак 0x40 — дописать; 8 — дописать и поставить 0x40; иначе — сбросить, наполнить, 0x40
	//                (network.c:42050–42081), БЕЗ всякого таймаута;
	//     [ПОРТ] цель — труп или её нет: очередь сбрасывается и не копится, признак снят. Эталон по трупу выходит ДО очереди
	//            (network.c:41904), а хвост у него снимает машина состояний соседа: замах кончается по смерти цели и покой
	//            `CChar_SetState(…, 0)` сбрасывает очередь и признак (entity_char.c:19301–19305). Машину состояний соседа порт
	//            не ведёт — её выход по смерти цели заменён этим сбросом;
	//     [ПОРТ] смена цели соседа — сброс и наполнение заново, признак 0x40 (проект §4 «и при смене цели соседа»). Эталон в
	//            серии (0x40/8) дописывает при ЛЮБОЙ цели (network.c:42050–42068): при переходе соседа на другого моба в
	//            дистанции (сервер отвечает 4029, а не 4027) хвост прошлого моба — удары, которые сервер уже применил к ПРЕЖНЕМУ
	//            мобу, — выводился бы цифрами на новом (дефект оригинала, закон fix-original-defects). Заодно смена цели
	//            прерывает записи ленты попаданий соседа по прежнему мобу (`CutImpactEntries`).
	//   Своя очередь здесь не трогается — её чистит заявка 4026 (`ClearOwnDamageQueue`, контроллер).
	bool bSkipQueueAdd = bOwnStaleReply;   // ★ Ф0: удары этого пакета не копятся (цель соседа мертва/её нет; ★ 25.10 — поздний свой ответ)
	if (!bOurs && bObserverSync)
	{
		const bool bNoLiveTarget = !M || M->IsDead();                                   // труп/нет цели — очереди нет
		const int32* PrevRid = PeerSeriesTargetRid.Find(AttackerUserIdx);              // цель прошлой серии соседа
		const int32 PrevTarget = PrevRid ? *PrevRid : -1;                              // (копия: карта ниже меняется)
		const bool bFrom4027 = Net->CombatAnimSrcOpcode == 4027;                       // первый удар серии (4027 case 1)
		const bool bTargetChanged = PrevTarget >= 0 && PrevTarget != TargetRid;        // [ПОРТ] смена цели — новая серия
		uint8& Flags = PeerSeriesFlags.FindOrAdd(AttackerUserIdx);                     // зеркало `field11C` соседа
		const uint8 WasFlags = Flags;                                                  // для журнала
		const TCHAR* Why = nullptr;                                                    // причина сброса очереди (nullptr — дописать)
		if (bNoLiveTarget)            { Why = TEXT("цель мертва или её нет"); Flags = 0; }
		else if (bPeerCode2Append)    { Why = nullptr; }                                // код 2 при живой цели — дописать (признак снимет отказ)
		else if (bFrom4027)           { Why = TEXT("4027 case 1 — новая серия"); Flags = 0x08; }
		else if (bTargetChanged)      { Why = TEXT("смена цели соседа"); Flags = 0x40; }
		else if (Flags & 0x40)        { Why = nullptr; }
		else if (Flags & 0x08)        { Why = nullptr; Flags = 0x40; }
		else                          { Why = TEXT("4029 вне серии"); Flags = 0x40; }
		if (Why && Buf.Num() > 0)
		{
			UE_LOG(LogAsdaWorld, Log, TEXT("[бой] очередь соседа %d сброшена (%s, признак серии 0x%02X): было %d значени(й) — rid %d → %d (откат asda.Combat.ObserverSync 0)"),
				AttackerUserIdx, Why, (uint32)WasFlags, Buf.Num(), PrevTarget, TargetRid);
		}
		ASDA_CTRACE_IF(bOurs || AsdaCtWorld::Ours(TargetRid, AttackerUserIdx), EAsdaCT::Queue, TEXT("сосед uidx=%d: признак серии 0x%02X → 0x%02X, %s%s; в очереди было %s (цель %d → %d)"),
			AttackerUserIdx, (uint32)WasFlags, (uint32)Flags, Why ? TEXT("СБРОС очереди: ") : TEXT("дописать в серию"), Why ? Why : TEXT(""),
			*AsdaCtWorld::Queue(Buf), PrevTarget, TargetRid);
		if (Why) { Buf.Reset(); }
		// ★ Ф2 [ОРИГИНАЛ]: у эталона сброс очереди (новая серия: 4027 case 1, 4029 вне серии, смена цели) выбрасывает и значения,
		//   которые клип соседа ещё не снял на своих кадрах удара. Порт снимает значения отрезка в момент его 4029 (план кадров) —
		//   их цифры и выстрелы, ещё не показанные, гасит новое поколение серии (`PeerSeriesGen`, как отказ). Мёртвая/пропавшая цель
		//   поколение не трогает: смертельное попадание прошлого отрезка показывается (правка 2026-08-25). Ключ
		//   `asda.Combat.SwingPlanFromPackets 0` — как после Ф1.
		if (Why && !bNoLiveTarget && bPeerPlanMode) { ++PeerSeriesGen.FindOrAdd(AttackerUserIdx); }
		bPeerNewSeries = Why != nullptr && !bNoLiveTarget;   // ★ ревизия Ф2 (ВАЖНО 1): новая серия — отрезкам прошлой серии конец
		if (bNoLiveTarget) { PeerSeriesTargetRid.Remove(AttackerUserIdx); bSkipQueueAdd = true; }
		else { PeerSeriesTargetRid.Add(AttackerUserIdx, TargetRid); }
		// ★ Ф1: смена цели соседа прерывает его записи ленты по прежнему мобу (назначенные ответом попадания доживают).
		if (bTargetChanged) { CutImpactEntries(AttackerUserIdx, TEXT("смена цели соседа"), PrevTarget); }
	}
	// ★★★ 17.10.2026 (ревизия Ф2, ВАЖНО 1): приём отрезка соседа — ПОСЛЕ разбора признака серии. Прежде `PeerAcceptSwingPacket`
	//   звался до сброса: при новой серии значения пакета вставали за 1–2 пустыми отрезками прошлой серии (сдвиг цифр на отрезок),
	//   а эталон снимает их на ближайших кадрах удара (network.c:42069–42081). Признак новой серии передаётся приёму.
	if (bWantPeerAccept && AtkCh)
	{
		AtkCh->PeerAcceptSwingPacket(WeaponSub, IsRangedWeapon(WeaponSub), PeerPlan, bPeerNewSeries);
		bHavePeerPlan = true;
		ASDA_CTRACE_IF(bOurs || AsdaCtWorld::Ours(TargetRid, AttackerUserIdx), EAsdaCT::Peer, TEXT("сосед uidx=%d: отрезок замаха по %s принят%s: %s, старт через %.3f с, длительность %.3f с, в очереди отрезков впереди %d, кадры удара %s"),
			AttackerUserIdx, *AsdaCtWorld::Mob(TargetRid, M), bPeerNewSeries ? TEXT(" (НОВАЯ серия)") : TEXT(""),
			PeerPlan.bStarted ? TEXT("замах начат с кадра 0") : TEXT("замах уже шёл"), PeerPlan.StartInS, PeerPlan.SegDurS,
			PeerPlan.QueuedAhead, *AsdaCtWorld::Delays(PeerPlan.HitDelaysS));
	}
	// ★ ПРЕДЕЛ 8, А НЕ 4 (правка 2026-08-25). Прежний предел 4 молча выбрасывал значения, которые
	//   сервер уже применил: замер по живому логу — 14 отброшенных из 76 присланных (18 %), и
	//   каждое такое — удар, который никогда не будет показан, плюс сдвиг всей дальнейшей серии.
	//   Сервер держит до 4 отложенных плюс 4 новых, поэтому 8 — его собственный потолок.
	//   ⚠ И отбрасываем ТЕПЕРЬ НЕ МОЛЧА: в оригинале переполнение видно вызывающему (он возвращает
	//   ноль и гейтит бафы замаха), у нас — строкой в журнал.
	{
		int32 Dropped = 0;
		static const TArray<int32> kNoHits;                                     // ★ Ф0: пустой набор — удары не копятся
		const TArray<int32>& Incoming = bSkipQueueAdd ? kNoHits : DmgQueue;    // ★ Ф0: что реально дописывается в очередь
		for (int32 V : Incoming)
		{
			if (Buf.Num() < 8) { Buf.Add(V); } else { ++Dropped; }
		}
		if (Dropped > 0)
		{
			UE_LOG(LogAsdaWorld, Warning,
				TEXT("[бой] очередь атакующего %d переполнена: отброшено %d значени(й) из %d — ")
				TEXT("столько ударов сервер применил, а показать их уже нечем"),
				AttackerUserIdx, Dropped, DmgQueue.Num());
		}
		ASDA_CTRACE_IF(bOurs || AsdaCtWorld::Ours(TargetRid, AttackerUserIdx), EAsdaCT::Queue, TEXT("очередь атакующего uidx=%d (%s): +%d из пакета%s%s → %s"),
			AttackerUserIdx, bOurs ? TEXT("свой") : TEXT("сосед"), Incoming.Num() - Dropped,
			bSkipQueueAdd ? TEXT(" (НЕ копится: цель соседа мертва/её нет)") : TEXT(""),
			(Dropped > 0) ? *FString::Printf(TEXT(", ОТБРОШЕНО %d сверх предела 8"), Dropped) : TEXT(""),
			*AsdaCtWorld::Queue(Buf));
	}
	// ★ Ф2 [ОРИГИНАЛ]: 4027 case 1 соседа — очередь наполнена, замаха и показа нет (network.c:40916–40919, `LABEL_36` пишет только
	//   очередь); удары покажет отрезок следующего 4029. HP из 4027 (@9 — без вычета накопителя) эталон к мобу не применяет.
	if (bPeerPlanMode && bFrom4027Pkt)
	{
		UE_LOG(LogAsdaWorld, Log, TEXT("[бой-Ф2] сосед %d: 4027 case 1 по rid %d — очередь %d, замаха нет (эталон LABEL_36; откат asda.Combat.SwingPlanFromPackets 0)"),
			AttackerUserIdx, TargetRid, Buf.Num());
		ASDA_CTRACE_IF(bOurs || AsdaCtWorld::Ours(TargetRid, AttackerUserIdx), EAsdaCT::Peer, TEXT("сосед uidx=%d: 4027 case 1 по %s — только очередь (%d), замаха и показа нет (их начнёт следующий 4029)"),
			AttackerUserIdx, *AsdaCtWorld::Mob(TargetRid, M), Buf.Num());
		return;
	}
	TArray<int32> Hits;                 // результаты ударов, показываемые в этом пакете
	float SeriesLenForAck = 1.0f;       // длительность серии (для ритма замахов)
	int32 MaxHitsShown = 0;             // сколько ударов реально показываем (для синхронизации HP)
	float LastHitTimeShown = 0.f;       // момент последнего показанного удара (сек от начала серии)
	// ★ ОТЛОЖЕННАЯ СМЕРТЬ ЦЕЛИ — ЕДИНАЯ ТОЧКА. В оригинале ответ на замах (Recv_CastState case 0) поле
	//   HP цели НЕ пишет вовсе (сверено: target[284] там не трогается) — он лишь ставит метку трупа
	//   0x20. HP уходит в 0 на КАДРЕ УДАРА, когда тик анимации атаки применяет урон из очереди
	//   (Combat_ProcessHitResultAndShowDamage), и только тогда charUpdateActionStateTick case 5 видит
	//   `target[+1136] <= 0` и выводит атакующего из состояния 5. Поэтому цель обязана гаснуть С
	//   ЗАДЕРЖКОЙ от начала замаха: гашение по приходу пакета съедало всю анимацию атаки при
	//   убийстве моба с одного удара (юзер 2026-07-31).
	auto KillTargetAfter = [this, W, TargetRid](AAsdaMobActor* Victim, float Delay)
	{
		if (!Victim || Victim->IsDead())
		{
			ASDA_CTRACE_MOB(TargetRid, EAsdaCT::Death, TEXT("%s: отложенная смерть по ответу НЕ ставится — цель %s"), *AsdaCtWorld::Mob(TargetRid, Victim),
				Victim ? TEXT("уже труп") : TEXT("не найдена"));
			return;
		}
		ASDA_CTRACE_MOB(TargetRid, EAsdaCT::Death, TEXT("%s: смерть по ответу запланирована через %.2f с (кадр удара последнего показанного удара + полёт снаряда)%s"),
			*AsdaCtWorld::Mob(TargetRid, Victim), Delay, (Delay <= 0.02f) ? TEXT(" — СРАЗУ") : TEXT(""));
		TWeakObjectPtr<AAsdaMobActor> WeakD = Victim;
		auto DoKill = [this, WeakD, TargetRid]()
		{
			AAsdaMobActor* Mm = WeakD.Get();
			if (!Mm || Mm->IsDead())
			{
				ASDA_CTRACE_MOB(TargetRid, EAsdaCT::Death, TEXT("%s: отложенная смерть по ответу — уже не нужна (%s)"), *AsdaCtWorld::Mob(TargetRid, Mm),
					Mm ? TEXT("моб умер другим каналом") : TEXT("актора нет"));
				return;
			}
			ASDA_CTRACE_MOB(TargetRid, EAsdaCT::Death, TEXT("%s: СМЕРТЬ применена на кадре удара (KillTargetAfter → NetDeath), HP полосы было %d/%d%s"),
				*AsdaCtWorld::Mob(TargetRid, Mm), Mm->CurHp, Mm->MaxHp, (TargetRid == AsdaCombatTrace::CurrentCycleTarget()) ? TEXT(", цель цикла") : TEXT(""));
			// ★ Очередь теперь живёт на АТАКУЮЩЕМ, а не на цели, поэтому чистить её по номеру
			//   монстра больше нечего: она сама обнуляется входом в новый бой (см.
			//   `ClearOwnDamageQueue`). Раньше здесь стояла единственная точка очистки — и она
			//   не срабатывала никогда, потому что выше стоит ранний выход по «цель уже мертва»,
			//   а смерть приходила другим каналом. Отсюда и брались значения из прошлой жизни.
			// rid НЕ освобождаем — труп остаётся сущностью (эталон), реген оживит его НА МЕСТЕ.
			if (AAsdaPlayerController* PC = Cast<AAsdaPlayerController>(GetGameInstance()->GetFirstLocalPlayerController()))
			{
				if (PC->TargetActor.Get() == Mm) { PC->ClearTarget(); }
			}
			Mm->NetDeath();
			// Журнал боя: метка «цель умерла» — ДО выпуска удержанных добычи/опыта (порядок строк: смерть → награда).
			ASDA_CTRACE_END_IF_TARGET(TargetRid, TEXT("цель умерла (смертельное попадание по ответу)"));
			// ★ Ф1: смерть показана смертельным попаданием — удержанные лентой добыча и опыт выходят вместе с ней.
			ReleaseLedgerHeld(TargetRid, TEXT("смертельное попадание"));
		};
		if (Delay <= 0.02f) { DoKill(); return; }
		FTimerHandle TD;
		W->GetTimerManager().SetTimer(TD, FTimerDelegate::CreateLambda(DoKill), Delay, false);
	};
	// ★★★ ЗДОРОВЬЕ ЦЕЛИ ЗДЕСЬ НЕ ПРИМЕНЯЕТСЯ — И ЭТО ЭТАЛОН, А НЕ УПУЩЕНИЕ.
	//   Сверено построчно: `Recv_CastState@0xF41620` (network.c:41863). Поле остатка здоровья
	//   `a2[1]` встречается в case 0 РОВНО ОДИН РАЗ — и только как условие метки трупа:
	//       if ( a2[1] <= 0 && (target[121] & 0x20) == 0 ) sub_92C3AD(target, 32);
	//   Ни одной записи здоровья цели из этого пакета в функции нет. Здоровье уходит в полосу
	//   ПОЗЖЕ: на кадре удара из очереди уронов (ниже) и снимком из 4015 `Recv_CombatHit`.
	//
	//   ⚠ ГРАБЛИ, НАСТУПАЛ 24.08.2026 (пункт 158 changelog, ОТМЕНЁН). Пакет замаха приходит РАНЬШЕ,
	//   чем начинается сам замах, и очередь уронов у него на первом ударе ПУСТАЯ. Применение
	//   здоровья «раз очередь пуста» роняло полосу цели в момент, когда персонаж только заносил
	//   оружие: «нажимаю дважды по мобу, персонаж только взводит оружие, а хп уже уменьшилось»
	//   (владелец, 24.08.2026). Ровно от этого эталон и защищён тем, что здоровье тут не пишет.
	//   Если полоса когда-то отстаёт — лечить надо канал 4015 и очередь, а НЕ этот пакет.

	// ★★★ ИСПРАВЛЕНИЕ ДЕФЕКТА ОРИГИНАЛА: остаток здоровья из пакета замаха применяется НА КАДРЕ
	//   УДАРА, а не сразу и не «никогда».
	//   Владелец 24.08.2026: «в эталоне хп тоже не всегда адекватно работало» — то есть чинить
	//   можно, но чинить надо ПРАВИЛЬНО. Пакет с пустой очередью (сервер посчитал урон у себя —
	//   накопитель NpcServer сливается пару секунд) в эталоне не применяется вовсе, и полоса
	//   висит на старом значении до следующего 4015. Применять его сразу тоже нельзя: пакет
	//   приходит В НАЧАЛЕ замаха, и полоса падала, пока персонаж ещё заносил оружие.
	//   Поэтому — откладываем ровно на кадр удара текущего замаха, туда же, где эталон применяет
	//   урон из очереди. Визуально полоса уходит вместе с попаданием, а значение остаётся точным.
	//   Ключ отката к эталону (не применять вовсе): `asda.Combat.HpSnapshotOnHitFrame 0`.
	const bool bAllChannels = CVarAsdaHitFrameAllChannels.GetValueOnGameThread() != 0;   // ★ 17.10.2026, см. объявление ключа
	bool bSnapDeferred = false;   // ★ 17.10.2026: снимок ушёл на кадр удара — немедленного применения ниже быть не должно
	// ★ 17.10.2026 (Ф2, к ревизии Ф1 п.1 — тот же двойной вычет): ответ с пустой очередью, чей снимок ОБЪЯСНЁН ещё не показанными
	//   ударами записей ленты (известный урон: свой замах показывает до `StepCount` ударов пачки за один ответ, сервер расходует их
	//   по 2/1 за замах — следующий ответ несёт HP «после» ударов, чьи цифры ещё впереди). Отдельно такой снимок не применяется ни на
	//   кадре удара, ни сразу: его урон вычтут сами цифры; лента лишь запоминает серверное HP (сверка «только вниз»). Прежде полоса
	//   уходила к снимку, а затем цифры вычитали тот же урон второй раз (полоса ниже сервера).
	bool bSnapExplained = false;   // снимок покрыт непоказанным известным уроном записей ленты
	if (M && bLedger && TargetHp >= 0 && Buf.Num() == 0 && M->CurHp != TargetHp)
	{
		if (const FMobImpactLedger* LS = ImpactLedgers.Find(TargetRid))
		{
			int64 SnapCover = 0;   // известный урон записей, ещё не показанный попаданием
			for (const FAsdaImpactEntry& E : LS->Pending) { SnapCover += E.PendingDamage; }
			bSnapExplained = SnapCover > 0 && (int64)M->CurHp <= (int64)TargetHp + SnapCover;
		}
		if (bSnapExplained)
		{
			UE_LOG(LogAsdaWorld, Log, TEXT("[лента] rid=%d: снимок %d → %d объяснён непоказанными ударами — отдельно не применяется"), TargetRid, M->CurHp, TargetHp);
			ASDA_CTRACE_IF(bOurs || AsdaCtWorld::Ours(TargetRid, AttackerUserIdx), EAsdaCT::HpPlate, TEXT("%s: снимок ответа без ударов %d → %d НЕ применяется — объяснён ещё не показанными ударами записей ленты (их цифры вычтут сами)"),
				*AsdaCtWorld::Mob(TargetRid, M), M->CurHp, TargetHp);
			NoteLedgerSnapshot(TargetRid, TargetHp, /*bAllowNumber=*/false);
			bSnapDeferred = true;   // немедленного применения ниже тоже нет
		}
	}
#if ASDA_COMBAT_TRACE
	bool bCtSnapEntered = false;   // журнал боя: снимок без ударов ушёл в ветку «на кадр удара» (иначе — строка «почему нет»)
#endif
	if (M && TargetHp >= 0 && Buf.Num() == 0 && M->CurHp != TargetHp && !bSnapExplained
	    && CVarHpSnapshotOnHitFrame.GetValueOnGameThread() != 0
	    && !IsServerHpSnapshotStale(TargetRid, TargetHp))
	{
		// Кадр удара текущего замаха: первый кадр из combo_time, иначе середина клипа.
		float HitAt = (CT.bValid && CT.HitTimesSec.Num() > 0)
			? CT.HitTimesSec[0]
			: ((SwingLen > 0.f) ? SwingLen * 0.5f : 0.25f);
		// ★ 17.10.2026: момент ВИЗУАЛЬНОГО удара считается так же, как у раскладки ударов ниже: от текущей позиции замаха
		//   (а не от нуля клипа), ближний под серверными часами — по плану замаха, дальнобойное — плюс полёт снаряда
		//   (полоса уходит на прилёте, как у цифр урона). Прежнее «кадр удара от нуля» у дальнобойного роняло полосу на
		//   вылете снаряда.
		if (bAllChannels && AtkCh && CT.bValid && CT.HitTimesSec.Num() > 0)
		{
			HitAt = FMath::Max(0.f, CT.HitTimesSec[0] - AtkCh->GetAttackSwingElapsedSec());
			const float Up = OwnPlanDelay(0);   // свой: ближайший кадр удара плана (Ф2 — без гейта часов и для дальнобойного)
			if (Up >= 0.f) { HitAt = Up; }
			if (bHavePeerPlan && PeerPlan.HitDelaysS.Num() > 0) { HitAt = PeerPlan.HitDelaysS[0]; }   // ★ Ф2: сосед — кадр его отрезка
			if (IsRangedWeapon(WeaponSub))
			{
				bool bBoneOk = false;
				const FVector MuzzleNow = ProjectileMuzzle(AtkCh, bBoneOk);
				FVector TO, TE; M->GetActorBounds(false, TO, TE);
				HitAt += AAsdaProjectileActor::FlightTimeSeconds((float)(TO - MuzzleNow).Size(), 2.5f);
			}
			// ★ Ф1: запись ленты для ЛЮБОГО атакующего: урон — дельта снимка, попадание — кадр удара (+ полёт).
			if (bLedger)
			{
				const int32 SnapDmg = FMath::Max(0, M->CurHp - TargetHp);   // урон снимка (известен из пакета)
				ArmImpact(TargetRid, AttackerUserIdx, ImpactKind, HitAt + 0.05f, /*bCommitted=*/true, /*bLethal=*/TargetHp <= 0,
					SnapDmg, /*bOutcomeKnown=*/SnapDmg > 0, TEXT("снимок HP без ударов"));
				NoteLedgerSnapshot(TargetRid, TargetHp, /*bAllowNumber=*/bOurs);
			}
		}
		bSnapDeferred = HitAt > 0.02f;
#if ASDA_COMBAT_TRACE
		bCtSnapEntered = true;   // журнал боя: ветка «снимок на кадр удара» отработала
#endif
		UE_LOG(LogAsdaWorld, Log,
			TEXT("[бой] rid=%d: снимок здоровья без ударов %d → %d отложен на кадр удара (+%.2f с)"),
			TargetRid, M->CurHp, TargetHp, HitAt);
		ASDA_CTRACE_IF(bOurs || AsdaCtWorld::Ours(TargetRid, AttackerUserIdx), EAsdaCT::HpPlate, TEXT("%s: снимок ответа без ударов %d → %d ПРИДЕРЖАН до кадра удара (+%.2f с%s)%s"),
			*AsdaCtWorld::Mob(TargetRid, M), M->CurHp, TargetHp, HitAt, IsRangedWeapon(WeaponSub) ? TEXT(", с полётом снаряда") : TEXT(""),
			(HitAt <= 0.02f) ? TEXT(" — кадр уже пройден, применяется сразу") : TEXT(""));
		TWeakObjectPtr<AAsdaMobActor> WeakHp = M;
		// ★ Ревизия 17.10, п.2: свой замах при `HitFrameAllChannels 1` — немедленного `ApplyMobHp(…, true)` ниже больше нет,
		//   поэтому на кадре удара снимок идёт через `ApplyMobHp`: цифра урона по дельте и MaxHp, как было при немедленном
		//   применении. Задвоения с очередью нет: снимок берётся только при пустой очереди, а цифру по дельте `ApplyMobHp`
		//   глушит, если цифра очереди показана меньше 1 с назад (`LastQueueHitS`). Фильтр устаревшего снимка — внутри
		//   `ApplyMobHp`, второй раз его здесь не зовём (ревизия п.8). Чужой замах и ключ 0 — прежний путь.
		const bool bSnapViaApply = bOurs && bAllChannels;
		// ★ Ф1 (ревизия Ф0 п.3): свой снимок — источник ИЗВЕСТЕН (наш замах), авторство по ленте не спрашиваем; попадание снимка
		//   вычитает его урон из записи ленты атакующего (`NoteImpactShown`).
		auto ApplySnap = [this, WeakHp, TargetRid, TargetHp, bSnapViaApply, AttackerUserIdx]()
		{
			AAsdaMobActor* Mm = WeakHp.Get();
			// К моменту кадра удара очередь могла сама довести полосу до этого значения —
			//   тогда делать нечего. И снимок мог успеть устареть.
			if (!Mm || Mm->IsDead() || Mm->CurHp == TargetHp)
			{
				ASDA_CTRACE_IF(AsdaCtWorld::Ours(TargetRid, AttackerUserIdx), EAsdaCT::HpPlate, TEXT("%s: кадр удара — придержанный снимок %d не нужен (%s)"), *AsdaCtWorld::Mob(TargetRid, Mm), TargetHp,
					!Mm ? TEXT("актора нет") : (Mm->IsDead() ? TEXT("моб уже мёртв") : TEXT("полоса уже на этом значении")));
				return;
			}
			const int32 Before = Mm->CurHp;   // полоса до снимка (урон попадания — разница)
			ASDA_CTRACE_IF(AsdaCtWorld::Ours(TargetRid, AttackerUserIdx), EAsdaCT::HitFrame, TEXT("%s: кадр удара — применяется придержанный снимок ответа %d (полоса %d, атакующий uidx=%d%s)"),
				*AsdaCtWorld::Mob(TargetRid, Mm), TargetHp, Before, AttackerUserIdx, bSnapViaApply ? TEXT(", через ApplyMobHp с цифрой по дельте") : TEXT(""));
			if (bSnapViaApply) { ApplyMobHp(Mm, TargetHp, /*bAllowNumber=*/true, /*bSourceOwn=*/true); }
			else
			{
				if (IsServerHpSnapshotStale(TargetRid, TargetHp))
				{
					ASDA_CTRACE_IF(AsdaCtWorld::Ours(TargetRid, AttackerUserIdx), EAsdaCT::HpPlate, TEXT("%s: кадр удара — придержанный снимок %d УСТАРЕЛ по прогнозу сервера, не применён (полоса %d)"),
						*AsdaCtWorld::Mob(TargetRid, Mm), TargetHp, Mm->CurHp);
					return;
				}
				ASDA_CTRACE_IF(AsdaCtWorld::Ours(TargetRid, AttackerUserIdx), EAsdaCT::HpPlate, TEXT("%s: HP полосы %d → %d / %d (придержанный снимок ответа на кадре удара)"),
					*AsdaCtWorld::Mob(TargetRid, Mm), Mm->CurHp, FMath::Max(0, TargetHp), Mm->MaxHp);
				if (HoldMobHpRaise(Mm, FMath::Max(0, TargetHp), TEXT("снимок ответа на кадре удара"))) { return; }   // ★ 18.10.2026 (пол HP)
				Mm->CurHp = FMath::Max(0, TargetHp);
				UpdateMobHpBar(Mm);
			}
			NoteImpactShown(TargetRid, AttackerUserIdx, Before - Mm->CurHp);
		};
		if (HitAt <= 0.02f) { ApplySnap(); }
		else
		{
			FTimerHandle TH;
			W->GetTimerManager().SetTimer(TH, FTimerDelegate::CreateLambda(ApplySnap), HitAt, false);
		}
	}
#if ASDA_COMBAT_TRACE
	// ★ Журнал боя: «почему НЕ» — снимок без ударов отличается от полосы, но на кадр удара не ушёл (ключ 0 или устарел по прогнозу).
	if (!bCtSnapEntered && !bSnapExplained && M && TargetHp >= 0 && Buf.Num() == 0 && M->CurHp != TargetHp)
	{
		const FAsdaMobHpForecast* CtFc = MobHpForecast.Find(TargetRid);   // прогноз, по которому снимок мог быть отброшен
		ASDA_CTRACE_IF(bOurs || AsdaCtWorld::Ours(TargetRid, AttackerUserIdx), EAsdaCT::HpPlate, TEXT("%s: снимок ответа без ударов %d (полоса %d) на кадр удара НЕ отложен: %s (прогноз сервера %d)"),
			*AsdaCtWorld::Mob(TargetRid, M), TargetHp, M->CurHp,
			(CVarHpSnapshotOnHitFrame.GetValueOnGameThread() == 0) ? TEXT("ключ asda.Combat.HpSnapshotOnHitFrame 0") : TEXT("снимок выше прогноза — устарел"),
			CtFc ? CtFc->Hp : -1);
	}
#endif

	// ★★★ ЗАЩИТА ОТ СКАЧКА ПОЛОСЫ ВВЕРХ (серверный дефект D-03, чиним на нашей стороне).
	//   NpcServer собирает поле здоровья В ДВУХ ПАКЕТАХ ПО-РАЗНОМУ:
	//       8204 (→ клиенту 4029): `HP − накопитель`, с зажимом в 0   (NpcServer.exe.c:63069)
	//       8202 (→ клиенту 4027): просто `HP`, БЕЗ вычета            (NpcServer.exe.c:63395)
	//   Пока моб «спящий» (до 2000 мс после агро) поле HP у него не меняется вовсе, весь урон
	//   копится в накопителе. Поэтому 4029 показывает падающее здоровье, а следующий 4027 —
	//   ПОЛНОЕ. Полоса прыгает вверх и снова вниз: жалоба владельца «хп дёргалось».
	//   Починить на сервере нечем: в отображаемой части `.text` NpcServer нет ни одного свободного
	//   участка длиннее 32 байт, а вычет с зажимом требует 10 байт врезки (проверено 24.08.2026).
	//   Лечим ТЕМ ЖЕ механизмом, что уже есть для навыков, — прогнозом здоровья: запоминаем
	//   значение, которое сервер только что подтвердил, и в течение окна не даём применить
	//   снимок ВЫШЕ него (`IsServerHpSnapshotStale`). Второго механизма не заводим.
	//   ⚠ Прогноз ведётся ТОЛЬКО по мобам и только когда сервер прислал здоровье.
	// ★ 18.10.2026 (пол HP, разбор у `CVarAsdaHpFloorFromServer`): 4029 — пол полосы (HP − накопитель), 4027 — сырое (только опускает).
	if (M && !M->IsDead())
	{
		NoteMobCombat(TargetRid);
		if (TargetHp >= 0) { NoteMobHpFloor(M, TargetHp, /*bFrom4029=*/Net->CombatAnimSrcOpcode == 4029 && !bPeerCode2Append, Net->CombatAnimSrcOpcode == 4027 ? TEXT("4027") : TEXT("4029")); }
	}
	if (M && TargetHp >= 0 && W)
	{
		const double NowF = W->GetTimeSeconds();
		FAsdaMobHpForecast& F = MobHpForecast.FindOrAdd(TargetRid);
		const bool bAlive = F.UntilS > NowF && F.Hp >= 0;   // прежний прогноз ещё держится
		// ★ 05.11.2026 (bug-mob-hp-zero, ключ `asda.Combat.ForecastNoExtend`): ответ не ниже живого прогноза его НЕ продлевает —
		//   иначе один устаревший минимум (запись навыка до лечения босса) держался весь бой.
		if (!(bAlive && TargetHp >= F.Hp && IsForecastNoExtendOn()))
		{
			F.Hp     = bAlive ? FMath::Min(F.Hp, TargetHp) : TargetHp;
			F.UntilS = NowF + MobHpForecastHoldS();
		}
	}

	if (M && Buf.Num() > 0)
	{
		// ══ ★ РАЗБОР ОЧЕРЕДИ УРОНОВ 1:1 (эталон entity_char.c:26886, кадр удара внутри AttackAnimTick) ══
		//     dmgFromQueue = playerArrayPopFront(attacker);                     ← ОДИН элемент на шаг комбо
		//     if (field_39C == field_3A0 + 1 && field_39C % 2u)                 ← ПОСЛЕДНИЙ шаг И НЕЧЁТНЫЙ StepCount
		//     { critBonus = playerArrayPopFront(attacker);                      ← только тогда снимается ПАРА
		//       if (critBonus == -1) critBonus = 0;
		//       else if (critBonus < -1) dmgFromQueue &= ~0x80000000; }
		//     else critBonus = 0;
		//   ПРЕЖНЯЯ СКЛЕЙКА БЫЛА НЕВЕРНОЙ: она считала парой ЛЮБОЙ следующий элемент с битом 31, а
		//   бит 31 стоит и у сентинела -2 («ничего не показывать»). Живой лог 21:43:50, очередь
		//   [274 -2 315 -2] по мобу rid=8 (35 HP): оба реальных удара склеивались с -2, превращались
		//   в мусорные отрицательные значения и уходили в MISS — «сумма=0, HP 35→0»: ни цифр, ни
		//   импактов, ни хит-флинча, моб просто умирал (ровно жалоба юзера 2026-07-31).
		//   Крит — это БИТ 31 НА САМОМ ЗНАЧЕНИИ УДАРА (эталон: ветка `else` от `dmgFromQueue >= -1`
		//   считает dmg = critBonus + (raw & 0x7FFFFFFF) и рисует крит-стилем).
		{
			const int32 Steps = CT.bValid ? FMath::Max(1, CT.StepCount) : Buf.Num();
			int32 Step = 0;
			for (int32 k = 0; k < Buf.Num(); ++k, ++Step)
			{
				int32 Raw = Buf[k];
				int32 CritBonus = 0;
				if (AsdaTakesCritPair(WeaponSub, Steps, Step, Buf, k))
				{
					CritBonus = Buf[++k];
					// ★★★ ДЕФЕКТ ОРИГИНАЛА (D3), обезврежен 24.08.2026. Эталон
					//   (skill_buff.c:370-378 и три двойника) декодирует спутника так:
					//       if ( extra == -1 ) extra = 0;
					//       else if ( extra < -1 ) hitResult &= ~0x80000000;   // ← маска у ОСНОВНОГО
					//   …а ниже безусловно делает `hitResult += extra` (skill_buff.c:466,
					//   entity_char.c:957). Обезврежен только код −1. Если спутник — сам крит
					//   (`0x80000000|dmg`), то у основного удара снимается признак крита И к нему
					//   прибавляется `0x8000xxxx` как ЗНАКОВОЕ число, то есть ≈ −2 147 483 600 —
					//   и это же значение уходит в вычитание здоровья. Если спутник — сентинел
					//   −3…−7, урон молча уменьшается на 3…7.
					//   Что это на самом деле: спутник со значением ≤ −1 — МЕТКА, а не число.
					//   Метка `< -1` означает «у основного удара выставлен бит 31, снять его»
					//   (так же трактует крит-ветка ниже: entity_char.c:27084, skill_buff.c:595).
					//   Складывать метку с уроном нельзя ни в каком случае.
					//   ⚠ Ветка эта в бою НЕДОСТИЖИМА (см. AsdaTakesCritPair: правило живёт только
					//   у ближнего оружия, а у него все серии чётные). Правка — страховка, чтобы
					//   форма дефекта не сработала, если сюда однажды зайдут.
					if (CritBonus <= -1)
					{
						if (CritBonus < -1) { Raw &= ~0x80000000; }   // эталонное снятие признака
						CritBonus = 0;                                // ★ метку НЕ складываем
					}
				}
				// Сентинелы проходят как есть; урон/крит сворачиваются в «значение с битом 31 = крит»
				//   — это внутренний контракт ShowHitResult.
				if (AsdaQueueIsSentinel(Raw)) { Hits.Add(Raw); }
				else if (Raw > 0)             { Hits.Add(Raw + CritBonus); }
				else                          { Hits.Add((int32)(((uint32)(CritBonus + (Raw & 0x7FFFFFFF))) | 0x80000000u)); }
			}
		}
		LastQueueHitS = W->GetTimeSeconds();   // очередь есть → фолбэк по дельте HP не нужен
		ImpactLedgers.FindOrAdd(TargetRid).LastQueueHitS = LastQueueHitS;   // ★ Ф0 → Ф1 (проект §3.7): то же по мобу — в его ленте
		// ★★★ ЭТО ОБХОД ДЕФЕКТА D1, А НЕ ПОРТ ЭТАЛОНА — помечено явно 24.08.2026.
		//   В эталоне такой эвристики НЕТ: за замах расходуется ровно `StepCount` элементов по
		//   кадрам, а остаток остаётся в массиве и там же портится (см. `addEntriesBounded4`,
		//   разбор у чтения 4029 в `AsdaNetSubsystem.cpp`). Мы считаем по дельте здоровья, потому
		//   что иначе пришлось бы воспроизводить и сам дефект. Отклонение осознанное.
		// ★ СКОЛЬКО УДАРОВ СЕРВЕР РЕАЛЬНО ПРИМЕНИЛ: считаем по ДЕЛЬТЕ HP цели (пакет несёт HP ПОСЛЕ
		//   ударов). Живой лог: очередь [9 9 10 9] при падении HP 35→17 = применены ДВА удара, остальные
		//   уходят в следующий замах (эталон: очередь — буфер, расход по кадрам). Без этого мы «съедали»
		//   всю очередь, убивали моба локально раньше сервера и гасили оставшиеся цифры.
		// ★★★ СКОЛЬКО УДАРОВ ПОКАЗАТЬ ЗА ЗАМАХ — 1:1 С ЭТАЛОНОМ, БЕЗ ОГЛЯДКИ НА ЗДОРОВЬЕ.
		//   Правило эталона (`Player_ProcessAttackTick`, entity_char.c:26886 и продвижение шага
		//   :27220-27232) сводится к трём строчкам:
		//       на КАЖДОМ кадре удара ровно один `playerArrayPopFront`;
		//       кадров удара за проход клипа = `field_39C` = StepCount строки combo_time;
		//       поле здоровья из пакета при этом НЕ ЧИТАЕТСЯ ВООБЩЕ.
		//   Здоровье цели у эталона убывает ТОЛЬКО от показанных ударов, а серверные снимки
		//   приезжают отдельными каналами (4015 @+25, 4017 @92) и правят его сами.
		//
		//   ⚠ ЗДЕСЬ БЫЛА ЗАПЛАТКА, И ОНА ЖЕ БЫЛА ИСТОЧНИКОМ СКАЧКОВ ПОЛОСЫ. Порт считал число
		//   ударов «по дельте здоровья»: складывал урон, пока не покроет разницу. На одношаговом
		//   оружии это давало ДВА удара за замах вместо одного (живой лог: «показываем 2 удар(ов),
		//   сумма=460 … дельта 207»), полоса уходила вниз на лишнее, и её приходилось поднимать
		//   обратно досинхронизацией. Заплатка лечила симптом заплатки.
		//
		//   Правило проверено на живом потоке 24.08.2026 (баллиста, StepCount = 1, моб rid=36):
		//       очередь [-2 224 266 -2] → показали 1 удар, сумма 0   … дельта сервера 0
		//       остаток               → показали 1 удар, сумма 224 … дельта 224
		//       остаток               → показали 1 удар, сумма 266 … дельта 266
		//   Сумма показанного ровно равна дельте сервера на каждом шаге.
		//   Ключ возврата к прежней заплатке: `asda.Combat.LegacyHpDeltaHeuristic 1`.
		int32 MaxHits = CT.bValid ? FMath::Min(CT.StepCount, Hits.Num()) : FMath::Min(1, Hits.Num());
		// ★★ Ф2 (проект §4, ревизия проекта п.7): сосед расходует ровно столько значений, сколько кадров удара в отрезке ЭТОГО 4029
		//   (эталон: на каждом кадре удара один `playerArrayPopFront`). Сервер выдаёт пачку из 4 и расходует 2 за замах у оружия
		//   типа ≤ 3 (1 — у остальных), а `StepCount` меча — 4…6 ударов за проход: брать по `StepCount` значило бы показать
		//   удары двух отрезков на одном и сдвинуть «цифра ↔ кадр удара» на отрезок. Остаток — следующему 4029 (обычно пустому).
		if (bHavePeerPlan) { MaxHits = FMath::Min(PeerPlan.HitDelaysS.Num(), Hits.Num()); }
		if (!bHavePeerPlan && CVarLegacyHpDeltaHeuristic.GetValueOnGameThread() != 0 && TargetHp >= 0)
		{
			const int32 DeltaHp = FMath::Max(0, M->CurHp - TargetHp);
			int32 Acc = 0, Used = 0;
			for (int32 h = 0; h < Hits.Num(); ++h)
			{
				const int32 Dmg = AsdaQueueDamage(Hits[h]);
				++Used;
				Acc += Dmg;
				if (Acc >= DeltaHp && DeltaHp > 0) { break; }
			}
			if (DeltaHp > 0) { MaxHits = Used; }
			else { MaxHits = FMath::Min(MaxHits, 1); }
		}
		MaxHits = FMath::Min(MaxHits, Hits.Num());
		MaxHitsShown = MaxHits;
		// ★ РИТМ = ВРЕМЯ ДО ПОСЛЕДНЕЙ ПОКАЗАННОЙ ЦИФРЫ, а не полное окно комбо. Сервер применяет за
		//   ответ ~2 удара, а окно комбо у меча 4 кадра (2 с) — ожидание полного окна давало паузы
		//   «эффекты без цифр». В оригинале поток цифр непрерывный (юзер сверил с оригинальным клиентом):
		//   следующий замах уходит сразу после последнего показанного удара серии.
		SeriesLenForAck = (CT.bValid && CT.HitTimesSec.IsValidIndex(FMath::Max(0, MaxHits - 1)))
			? CT.HitTimesSec[FMath::Max(0, MaxHits - 1)] + 0.1f
			: ((SwingLen > 0.f) ? SwingLen : 1.0f);
		// Диагностика синхрона: сумма показанных ударов должна совпадать с падением HP по серверу.
		{
			int32 Sum = 0;
			for (int32 h = 0; h < MaxHits; ++h) { Sum += AsdaQueueDamage(Hits[h]); }
			// ★ Печатаем и САМИ значения: без них по логу нельзя понять, почему показанная
			//   сумма разошлась с дельтой (24.08.2026 разбирали именно это).
			FString HitsStr; for (int32 h = 0; h < Hits.Num(); ++h)
			{ HitsStr += FString::Printf(TEXT("%s%d"), (h < MaxHits) ? TEXT("*") : TEXT(" "), Hits[h]); }
			FString BufStr; for (int32 V : Buf) { BufStr += FString::Printf(TEXT(" %d"), V); }
			UE_LOG(LogAsdaWorld, Log, TEXT("[бой] rid=%d: показываем %d удар(ов), сумма=%d, HP %d→%d (дельта %d), в буфере было %d | показ:[%s] буфер:[%s] шагов=%d"),
				TargetRid, MaxHits, Sum, M->CurHp, TargetHp, (TargetHp >= 0 ? M->CurHp - TargetHp : -1), Buf.Num(),
				*HitsStr, *BufStr, CT.bValid ? CT.StepCount : -1);
			ASDA_CTRACE_IF(bOurs || AsdaCtWorld::Ours(TargetRid, AttackerUserIdx), EAsdaCT::Queue, TEXT("%s: СЪЁМ очереди атакующего uidx=%d (%s): показываем %d из %s (шагов комбо %d%s), сумма %d, HP полосы %d → сервер %d (дельта %d)"),
				*AsdaCtWorld::Mob(TargetRid, M), AttackerUserIdx, bOurs ? TEXT("свой") : TEXT("сосед"), MaxHits, *AsdaCtWorld::Queue(Hits),
				CT.bValid ? CT.StepCount : -1, bHavePeerPlan ? TEXT(", по кадрам отрезка соседа") : TEXT(""), Sum, M->CurHp, TargetHp,
				(TargetHp >= 0 ? M->CurHp - TargetHp : -1));
		}
		// ★ 25.10.2026 (`asda.Combat.OwnHitsServerCommit`): номер ПОСЛЕДНЕГО значения очереди каждой показываемой цифры своего
		//   ответа в серии (пара крита занимает два значения) и номер серии — показ сверит их с принятыми сервером ударами.
		TArray<int32> OwnHitOrd;      // по h: номер последнего значения в серии (−1 — не свой)
		uint32 OwnHitGen = 0;         // серия своих ответов на момент постановки
		// Показанные удары УХОДЯТ из буфера; остаток достанется следующему пакету (в т.ч. с пустой очередью).
		{
			// Расход считаем ТЕМ ЖЕ правилом пар, что и разбор выше (эталон: пара только на последнем
			//   шаге нечётного комбо) — иначе буфер разъезжается с показанными ударами.
			const int32 Steps = CT.bValid ? FMath::Max(1, CT.StepCount) : Buf.Num();
			int32 Consume = 0, Shown = 0;
			TArray<int32> LastIdx;   // ★ 25.10.2026: индекс последнего значения буфера у каждой цифры
			for (int32 k = 0; k < Buf.Num() && Shown < MaxHits; ++k, ++Consume)
			{
				// ⚠ Правило пары ОБЯЗАНО совпадать с разбором выше — иначе буфер разъедется
				//   с показанными ударами. Поэтому обе точки зовут одну функцию.
				if (AsdaTakesCritPair(WeaponSub, Steps, Shown, Buf, k)) { ++k; ++Consume; }
				LastIdx.Add(k);
				++Shown;
			}
			Buf.RemoveAt(0, FMath::Min(Consume, Buf.Num()));
			if (bOurs && !bHavePeerPlan)
			{
				if (UAsdaAttackRhythmSubsystem* RhO = GetGameInstance()->GetSubsystem<UAsdaAttackRhythmSubsystem>())
				{
					OwnHitGen = RhO->GetSeriesGen();
					const int32 Base = RhO->TakeSeriesOrdinals(Consume);   // номер первого снятого значения в серии
					for (int32 Li : LastIdx) { OwnHitOrd.Add(Base + Li); }
					int32 SelfWt = 0, SelfRange = 0;   // оружие своего персонажа (для журнала: значений за принятый удар)
					RhO->GetSelfWeapon(SelfWt, SelfRange);
					ASDA_CTRACE(EAsdaCT::Queue, TEXT("свой ответ: значения серии #%u с номера %d (снято %d); сервер уже применил %d значени(й) серии (%d за принятый удар)"),
						OwnHitGen, Base, Consume, RhO->ServerCommittedEntries(OwnHitGen), UAsdaAttackRhythmSubsystem::ServerHitsPerSwing(SelfRange));
				}
			}
			ASDA_CTRACE_IF(bOurs || AsdaCtWorld::Ours(TargetRid, AttackerUserIdx), EAsdaCT::Queue, TEXT("очередь атакующего uidx=%d: снято %d значени(й), остаток %s — следующему ответу"),
				AttackerUserIdx, Consume, *AsdaCtWorld::Queue(Buf));
		}
		TWeakObjectPtr<AAsdaMobActor> WeakM = M;
		// ★ ДАЛЬНОБОЙНОЕ ОРУЖИЕ (projectile_re.md): на кадре удара не «прилетает» урон, а ВЫЛЕТАЕТ СНАРЯД —
		//   цифры/импакт/звук попадания идут ПО ПРИЛЁТЕ (эталон Combat_ProcessHitResultAndShowDamage).
		//   Работает и для себя, и для соседей: AtkCh — тот же класс AAsdaPlayerCharacter в обоих случаях.
		const bool bRangedAtk = IsRangedWeapon(WeaponSub) && AtkCh != nullptr;
		float ProjFlightS = 0.f;   // оценка полёта (для сдвига смерти цели и ритма замахов)
		if (bRangedAtk)
		{
			bool bBoneOk = false;
			const FVector MuzzleNow = ProjectileMuzzle(AtkCh, bBoneOk);
			FVector TO, TE; M->GetActorBounds(false, TO, TE);
			ProjFlightS = AAsdaProjectileActor::FlightTimeSeconds((float)(TO - MuzzleNow).Size(), 2.5f);
			// Лук/«лук-арбалет» (8/0xA): стрела на тетиве видна с начала замаха (эталон, кадр 0).
			if (WeaponSub == 8 || WeaponSub == 10) { SetNockedArrow(AtkCh, true); }
		}
		TWeakObjectPtr<AAsdaPlayerCharacter> WeakAtk = AtkCh;
		// ★★★ КОРЕНЬ РАССИНХРОНА (найден 2026-08-25 по живому логу). Кадры удара из `combo_time`
		//   отсчитываются от НАЧАЛА КЛИПА, а со второго прохода клип играет НЕ с начала: он
		//   перематывается на точку окна комбо. У баллисты клип длиной 2.00 с мотается на 1.17 с,
		//   то есть проход длится 0.83 с, а кадр удара (1.17 с от начала клипа) приходится на самое
		//   НАЧАЛО прохода. Планируя показ на 1.17 с от прихода ответа, мы опаздывали больше чем на
		//   секунду — примерно на полтора прохода. Отсюда всё сразу: цифры не совпадали со взмахом,
		//   снаряды вылетали в чужой фазе (и «через раз»), результат приезжал не к тому удару.
		//   Лечение: отсчитывать от того места, где анимация НАХОДИТСЯ СЕЙЧАС.
		//   ★★★ ТЕМП: берём пройденную часть в РЕАЛЬНОМ времени (позиция клипа ÷ множитель темпа) —
		//   моменты ударов ниже тоже реальные, и складывать их с клип-временем нельзя.
		const float ClipPos = AtkCh ? AtkCh->GetAttackSwingElapsedSec() : 0.f;
		// ★★★ 18.10.2026 (ключ `asda.Combat.OwnHitsFifo`, разбор у объявления) [ОРИГИНАЛ entity_char.c:829]: одно значение очереди на
		//   кадр удара. Свой замах — удары этого ответа ложатся ПОСЛЕ кадров, уже занятых непоказанными ударами прошлых ответов (их
		//   таймеры текущего замаха), а не на h-й ближайший кадр «от сейчас». Сосед — по кадрам своего отрезка (Ф2), не меняется.
		const int32 OwnFifoBase = (bOurs && AtkCh && CVarAsdaOwnHitsFifo.GetValueOnGameThread() != 0) ? OwnPendingHitFrames(AtkCh, TargetRid) : 0;
		if (OwnFifoBase > 0)
		{
			ASDA_CTRACE(EAsdaCT::HitFrame, TEXT("%s: свой ответ — %d кадр(ов) удара ещё заняты ударами прошлых ответов: удары этого ответа встают с кадра №%d (FIFO, asda.Combat.OwnHitsFifo)"),
				*AsdaCtWorld::Mob(TargetRid, M), OwnFifoBase, OwnFifoBase + 1);
		}
		// ★ 17.10.2026 (Ф1, ревизия Ф0 п.10) [ОРИГИНАЛ]: отказ, переводящий соседа в покой, прерывает показ уже поставленных цифр
		//   и выстрелов прохода (эталон: покой `CChar_SetState(…, 0)`), — таймеры сверяют поколение серии соседа.
		const bool bGenGuard = !bOurs && bObserverSync;                                        // сверка поколения (только сосед)
		const uint32 SeriesGen = bGenGuard ? PeerSeriesGen.FindRef(AttackerUserIdx) : 0u;      // поколение серии на постановке
		FString PlanLog;   // ★ Ф2: моменты ударов этого ответа (журнал разбора «цифра ↔ кадр удара»)
		// ★ 25.10.2026 (`asda.Combat.OwnHitsServerCommit`): своё значение, которого сервер не применил и уже не применит, — не
		//   показывается: ни цифры, ни выстрела, ни вычета полосы; его урон снимается с ленты попаданий (иначе лента ждала бы показа),
		//   учёт поставленных ударов закрывается (смерть «на обнулившем ударе» его не ждёт). Истина `true` — значение снято.
		auto DropOwnPhantom = [this](int32 Rid, int32 Atk, int32 RawV, int32 HIdx, uint32 Seq, uint32 Gen, int32 Ord) -> bool
		{
			const TCHAR* Why = TEXT("");
			if (AsdaOwnHitServerLive(GetGameInstance(), Gen, Ord, &Why)) { return false; }
			UE_LOG(LogAsdaWorld, Log, TEXT("[бой] rid=%d: своё значение h=%d (%d, №%d серии #%u) НЕ показано — сервер его не применял: %s"),
				Rid, HIdx, AsdaQueueDamage(RawV), Ord, Gen, Why);
			ASDA_CTRACE(EAsdaCT::HitFrame, TEXT("КАДР УДАРА h=%d свой → rid=%d: значение %s (№%d серии #%u) НЕ показано — ФАНТОМ: %s (asda.Combat.OwnHitsServerCommit)"),
				HIdx, Rid, *AsdaCtWorld::Hit(RawV), Ord, Gen, Why);
			NoteImpactShown(Rid, Atk, AsdaQueueDamage(RawV));   // урон больше не ждёт показа — лента его не держит
			OnPlannedHitShown(Rid, Seq);
			++GAsdaOwnPhantomDropped;
			return true;
		};
		for (int32 h = 0; h < MaxHits; ++h)
		{
			const int32 Raw = Hits[h];
			const int32 OwnOrd = OwnHitOrd.IsValidIndex(h) ? OwnHitOrd[h] : -1;   // ★ 25.10.2026: номер значения в серии (−1 — не свой)
			// Момент удара: кадр из combo_time (иначе — равномерно по длительности).
			const float AtRaw = (CT.bValid && CT.HitTimesSec.IsValidIndex(h)) ? CT.HitTimesSec[h]
			              : ((SwingLen > 0.f) ? SwingLen * (float)(h + 1) / (float)(MaxHits + 1) : 0.25f * h);
			// Сколько осталось до этого кадра ОТ ТЕКУЩЕЙ ПОЗИЦИИ клипа. Кадр уже пройден (или мы
			//   ровно на нём) — показываем сразу: анимация до него уже дошла.
			float At = FMath::Max(0.f, AtRaw - ClipPos);
			// ★★ Ревизия СРЕДН-5: свой ближний бой под серверными часами шлёт 2–3 удара за проход, и ответ приходит, когда кадры
			//   ударов «от начала прохода» уже позади — цифры вставали разом. Эталон снимает результаты очереди на порогах
			//   шагов (`Skill_SpawnHitFeedback` skill_buff.c:362-370: `петля·W + A[шаг] ≤ кадр` → `playerArrayPopFront`), то
			//   есть на БЛИЖАЙШИХ кадрах ударов после прихода ответа. Так и раскладываем: h-я цифра — на h-м следующем кадре.
			// ★ Ф2: гейт «свой и под серверными часами, только ближний» снят — свой замах всегда по плану кадров (`OwnPlanDelay`),
			//   сосед — по плану кадров СВОЕГО отрезка (`PeerPlan`): у дальнобойного вылет на кадре удара, цифра на прилёте.
			{
				const float Up = OwnPlanDelay(OwnFifoBase + h);   // ★ 18.10.2026: FIFO — после занятых кадров (OwnFifoBase 0 — как было)
				if (Up >= 0.f) { At = Up; }
			}
			if (bHavePeerPlan && PeerPlan.HitDelaysS.IsValidIndex(h)) { At = PeerPlan.HitDelaysS[h]; }
			PlanLog += FString::Printf(TEXT(" %d@+%.3f"), Raw, At);
			LastHitTimeShown = FMath::Max(LastHitTimeShown, At + (bRangedAtk ? ProjFlightS : 0.f));
			// ★ Ревизия 18.10 (п.1): номер поставленного удара — до таймеров (его везут показ и гашение по трупу «по порядку»).
			const uint32 HitSeq = NextPlannedHitSeq++;
			// Выстрел (кадр удара) → результат по прилёте снаряда.
			auto FireOne = [this, WeakAtk, WeakM, Raw, bOurs, WeaponSub, h, AttackerUserIdx, TargetRid, bGenGuard, SeriesGen, HitSeq,
				DropOwnPhantom, OwnHitGen, OwnOrd]()
			{
				if (bOurs && DropOwnPhantom(TargetRid, AttackerUserIdx, Raw, h, HitSeq, OwnHitGen, OwnOrd)) { return; }   // ★ 25.10.2026: выстрела нет
				if (bGenGuard && PeerSeriesGen.FindRef(AttackerUserIdx) != SeriesGen)
				{
					ASDA_CTRACE_IF(bOurs || AsdaCtWorld::Ours(TargetRid, AttackerUserIdx), EAsdaCT::HitFrame, TEXT("кадр выстрела h=%d соседа uidx=%d по rid=%d (%s) НЕ показан — серия соседа прервана (отказ/новая серия)"),
						h, AttackerUserIdx, TargetRid, *AsdaCtWorld::Hit(Raw));
					return;
				}   // ★ Ф1: серия соседа прервана отказом
				AAsdaPlayerCharacter* A = WeakAtk.Get();
				AAsdaMobActor* Mm = WeakM.Get();
				ASDA_CTRACE_IF(bOurs || AsdaCtWorld::Ours(TargetRid, AttackerUserIdx), EAsdaCT::HitFrame, TEXT("КАДР УДАРА (выстрел) h=%d: %s uidx=%d → %s, значение %s%s"),
					h, bOurs ? TEXT("свой") : TEXT("сосед"), AttackerUserIdx, *AsdaCtWorld::Mob(TargetRid, Mm), *AsdaCtWorld::Hit(Raw),
					(!A || !Mm) ? TEXT(" — стрелка или цели нет, выстрела НЕ будет") : TEXT(""));
				// ★ ВЫСТРЕЛ ПРИВЯЗАН К КАДРУ ЗАМАХА, А НЕ К ЖИЗНИ ЦЕЛИ (эталон Player_ProcessAttackTick:
				//   skillFireArrowProjectile вызывается на кадре удара, проверки «цель жива» там нет —
				//   урон уезжает В СНАРЯДЕ). Раньше при мгновенном киле цель успевала стать трупом до
				//   кадра выстрела, и стрела не вылетала вовсе. Цифру по прилёте труп всё так же не
				//   получает (эталон: state 10 → пакеты по цели игнорируются) — гасится в OnHit.
				if (!A || !Mm) { return; }
				// ★ Ревизия 18.10 (п.4, ключ `asda.Combat.DeathAtLastPendingHit`) [ОРИГИНАЛ entity_char.c:23132]: цель уже труп и этот
				//   выстрел — позже смертельного удара (или атакующий уже в стойке) — стрелять некому и не во что: эталонный персонаж
				//   ушёл в стойку в первом тике после смерти цели, `Player_ProcessAttackTick` больше не зовётся.
				if (IsDeathAtLastPendingHit() && Mm->IsDead() && (IsHitAfterLethal(Mm, AttackerUserIdx, HitSeq) || !A->IsAttackAnimActive()))
				{
					ASDA_CTRACE_IF(bOurs || AsdaCtWorld::Ours(TargetRid, AttackerUserIdx), EAsdaCT::HitFrame, TEXT("кадр выстрела h=%d uidx=%d по ТРУПУ %s (%s) — выстрела НЕТ: %s"),
						h, AttackerUserIdx, *AsdaCtWorld::Mob(TargetRid, Mm), *AsdaCtWorld::Hit(Raw),
						A->IsAttackAnimActive() ? TEXT("удар позже смертельного") : TEXT("атакующий уже в боевой стойке"));
					OnPlannedHitShown(TargetRid, HitSeq);
					return;
				}
#if !UE_BUILD_SHIPPING
				if (bRecordHitResults)   // ★ Ф2: наблюдение самотеста — момент вылета (кадр удара прохода) и значение удара
				{
					FAsdaShotSeen& Sh = ShotsSeen.AddDefaulted_GetRef();
					Sh.Rid = TargetRid; Sh.Raw = Raw; Sh.AttackerUserIdx = AttackerUserIdx;
					Sh.AtS = GetGameInstance() && GetGameInstance()->GetWorld() ? GetGameInstance()->GetWorld()->GetTimeSeconds() : -1.0;
				}
#endif
				TWeakObjectPtr<UAsdaWorldSubsystem> WeakThis = this;
				TWeakObjectPtr<AAsdaMobActor> WM = Mm;
				FireProjectile(A, Mm, WeaponSub, AttackerUserIdx, TargetRid,
					[WeakThis, WM, Raw, bOurs, WeaponSub, h, AttackerUserIdx, TargetRid, HitSeq]()
					{
						UAsdaWorldSubsystem* WS = WeakThis.Get();
						AAsdaMobActor* Tm = WM.Get();
						// Труп ударов больше не получает (эталон: state 10 → пакеты по цели игнорируются).
						// ★★★ УБИВАЮЩИЙ УДАР ПОКАЗЫВАЕТСЯ (правка 2026-08-25, владелец: «урон может не
						//   засчитаться, а моб уже умер»). Здесь стоял гейт «цель жива», и последний
						//   удар КАЖДОГО боя гасился им всегда: замер по живому логу — смерть приходит
						//   через 0.49…1.03 с (медиана 0.725), а цифра запланирована на ~1.46 с
						//   (кадр удара баллисты 35/30 = 1.167 с плюс полёт снаряда 0.29 с).
						//   Труп живёт ещё 2.57 с (клип смерти), так что показывать цифру есть на чём.
						ASDA_CTRACE_IF(bOurs || AsdaCtWorld::Ours(Tm ? Tm->NetRenderId : -1, AttackerUserIdx), EAsdaCT::HitFrame, TEXT("ПРИЛЁТ снаряда h=%d атакующего uidx=%d → %s: %s%s"), h, AttackerUserIdx,
							*AsdaCtWorld::Mob(Tm ? Tm->NetRenderId : -1, Tm), *AsdaCtWorld::Hit(Raw), (WS && Tm) ? TEXT("") : TEXT(" — цели нет, показа НЕ будет"));
						if (WS && Tm) { WS->ShowHitResult(Tm, Raw, bOurs, WeaponSub, h, AttackerUserIdx, HitSeq); }
						if (WS) { WS->OnPlannedHitShown(TargetRid, HitSeq); }   // ★ ревизия 18.10: смертельный — смерть после показа
					});
				// Эталон (ветка 8/0xA): сразу после выстрела на тетиву кладётся СЛЕДУЮЩАЯ стрела —
				//   charObjectArray_initSixSlots вызывается тем же кадром, что и skillFireArrowProjectile.
				if (WeaponSub == 8 || WeaponSub == 10) { SetNockedArrow(A, true); }
			};
			if (At <= 0.02f)
			{
				ASDA_CTRACE_IF(bOurs || AsdaCtWorld::Ours(TargetRid, AttackerUserIdx), EAsdaCT::HitFrame, TEXT("удар h=%d (%s) по %s: до кадра удара %.3f с — кадр уже наступил (позиция клипа %.3f, кадр по combo_time %.3f) — %s СРАЗУ"),
					h, *AsdaCtWorld::Hit(Raw), *AsdaCtWorld::Mob(TargetRid, M), At, ClipPos, AtRaw, bRangedAtk ? TEXT("выстрел") : TEXT("показ"));
				if (bRangedAtk)
				{
					FireOne();
					// ★ 18.10.2026: снаряд вылетел — удар ждёт прилёта (учёт смертельного удара `LethalPlannedHitDelayS`).
					NotePlannedHit(TargetRid, AttackerUserIdx, AsdaQueueDamage(Raw), FTimerHandle(), 0, ProjFlightS, ProjFlightS, HitSeq, bGenGuard, SeriesGen);
				}
				else if (bOurs && DropOwnPhantom(TargetRid, AttackerUserIdx, Raw, h, HitSeq, OwnHitGen, OwnOrd))
				{
					// ★ 25.10.2026: ответ пришёл, когда бой уже прерван, — непримененное значение не показывается.
				}
				else
				{
					ASDA_CTRACE_IF(bOurs || AsdaCtWorld::Ours(TargetRid, AttackerUserIdx), EAsdaCT::HitFrame, TEXT("КАДР УДАРА h=%d: %s uidx=%d → %s, значение %s (сразу)"), h, bOurs ? TEXT("свой") : TEXT("сосед"),
						AttackerUserIdx, *AsdaCtWorld::Mob(TargetRid, M), *AsdaCtWorld::Hit(Raw));
					ShowHitResult(M, Raw, bOurs, WeaponSub, h, AttackerUserIdx);
				}
				continue;
			}
			FTimerHandle T;
			// ★ Ревизия Ф2 (мелочь 5): свой замах по плану кадров — таймер с привязкой к кадру клипа (смена скорости отрезка по серверным
			//   часам его переставит, `RetimeOwnHits`).
			const bool bOwnRetime = bOurs && AtkCh && OwnPlanDelay(OwnFifoBase + h) >= 0.f;
			int32 OwnTid = 0;   // ★ 18.10.2026: номер своего таймера (учёт поставленных ударов)
			if (bRangedAtk)
			{
				if (bOwnRetime) { OwnTid = ArmOwnHitTimer(AtkCh, At, FireOne, TargetRid); }
				else { W->GetTimerManager().SetTimer(T, FTimerDelegate::CreateLambda(FireOne), At, false); }
			}
			else
			{
				auto MeleeHit = [this, WeakM, Raw, bOurs, WeaponSub, h, AttackerUserIdx, bGenGuard, SeriesGen, TargetRid, HitSeq,
					DropOwnPhantom, OwnHitGen, OwnOrd]()
				{
					if (bOurs && DropOwnPhantom(TargetRid, AttackerUserIdx, Raw, h, HitSeq, OwnHitGen, OwnOrd)) { return; }   // ★ 25.10.2026: фантом
					if (bGenGuard && PeerSeriesGen.FindRef(AttackerUserIdx) != SeriesGen)
					{
						ASDA_CTRACE_IF(bOurs || AsdaCtWorld::Ours(WeakM.Get() ? WeakM.Get()->NetRenderId : -1, AttackerUserIdx), EAsdaCT::HitFrame, TEXT("кадр удара h=%d соседа uidx=%d (%s) НЕ показан — серия соседа прервана (отказ/новая серия)"),
							h, AttackerUserIdx, *AsdaCtWorld::Hit(Raw));
						return;
					}   // ★ Ф1: серия соседа прервана отказом
					ASDA_CTRACE_IF(bOurs || AsdaCtWorld::Ours(WeakM.Get() ? WeakM.Get()->NetRenderId : -1, AttackerUserIdx), EAsdaCT::HitFrame, TEXT("КАДР УДАРА h=%d: %s uidx=%d → %s, значение %s%s"), h, bOurs ? TEXT("свой") : TEXT("сосед"),
						AttackerUserIdx, *AsdaCtWorld::Mob(WeakM.Get() ? WeakM.Get()->NetRenderId : -1, WeakM.Get()), *AsdaCtWorld::Hit(Raw),
						WeakM.Get() ? TEXT("") : TEXT(" — цели нет, показа НЕ будет"));
					// Труп ударов больше не получает (эталон: state 10 → пакеты по цели игнорируются).
					// ★★★ УБИВАЮЩИЙ УДАР ПОКАЗЫВАЕТСЯ (правка 2026-08-25, владелец: «урон может не
					//   засчитаться, а моб уже умер»). Здесь стоял гейт «цель жива», и последний
					//   удар КАЖДОГО боя гасился им всегда: замер по живому логу — смерть приходит
					//   через 0.49…1.03 с (медиана 0.725), а цифра запланирована на ~1.46 с
					//   (кадр удара баллисты 35/30 = 1.167 с плюс полёт снаряда 0.29 с).
					//   Труп живёт ещё 2.57 с (клип смерти), так что показывать цифру есть на чём.
					if (AAsdaMobActor* Mm = WeakM.Get(); Mm) { ShowHitResult(Mm, Raw, bOurs, WeaponSub, h, AttackerUserIdx, HitSeq); }
					OnPlannedHitShown(TargetRid, HitSeq);   // ★ ревизия 18.10: смертельный удар — смерть после его показа
				};
				if (bOwnRetime) { OwnTid = ArmOwnHitTimer(AtkCh, At, MeleeHit, TargetRid); }
				else { W->GetTimerManager().SetTimer(T, FTimerDelegate::CreateLambda(MeleeHit), At, false); }
			}
			// ★ 18.10.2026: поставленный удар (свой и соседа одним учётом) — для смерти на ударе, который доводит полосу до 0.
			NotePlannedHit(TargetRid, AttackerUserIdx, AsdaQueueDamage(Raw), T, OwnTid, bRangedAtk ? ProjFlightS : 0.f, At + (bRangedAtk ? ProjFlightS : 0.f),
				HitSeq, bGenGuard, SeriesGen);
		}
		UE_LOG(LogAsdaWorld, Log, TEXT("[бой] rid=%d: моменты ударов ответа (%s%s, позиция клипа %.3f с):%s"), TargetRid,
			bOurs ? TEXT("свой") : TEXT("сосед"), bHavePeerPlan ? TEXT(", план отрезка") : TEXT(""), ClipPos, *PlanLog);
		ASDA_CTRACE_IF(bOurs || AsdaCtWorld::Ours(TargetRid, AttackerUserIdx), EAsdaCT::HitFrame, TEXT("%s: план кадров удара ответа (%s uidx=%d%s, позиция клипа %.3f с%s): значение@через_с —%s; последний показ через %.3f с"),
			*AsdaCtWorld::Mob(TargetRid, M), bOurs ? TEXT("свой") : TEXT("сосед"), AttackerUserIdx, bHavePeerPlan ? TEXT(", план отрезка") : TEXT(""),
			ClipPos, bRangedAtk ? *FString::Printf(TEXT(", снаряд летит %.2f с"), ProjFlightS) : TEXT(""), *PlanLog, LastHitTimeShown);
		// Серия кончилась — тетива пустеет (следующий замах покажет стрелу снова; в оригинале пул
		//   слотов чистится выходом из боевого состояния, у нас — по таймеру после последнего выстрела).
		if (bRangedAtk && (WeaponSub == 8 || WeaponSub == 10))
		{
			TWeakObjectPtr<AAsdaPlayerCharacter> WA = AtkCh;
			FTimerHandle TN;
			W->GetTimerManager().SetTimer(TN, [this, WA]()
			{
				if (AAsdaPlayerCharacter* A = WA.Get()) { SetNockedArrow(A, false); }
			}, FMath::Max(0.2f, LastHitTimeShown + 1.2f), false);
		}
		// ★ Смерть по серверному HP — НА КАДРЕ УДАРА (эталон: именно там применяется урон из очереди),
		//   т.е. ПОСЛЕ показа последнего удара серии. Раньше при отсутствии строки combo_time запасной
		//   веткой шли 0.05 с — цель гасла почти мгновенно и обрывала замах (баг мгновенного кила).
		if (TargetHp == 0 && !M->IsDead())
		{
			// ★ Тот же отсчёт от ТЕКУЩЕЙ позиции клипа, что и у показа ударов (см. пояснение выше):
			//   иначе смерть планируется на кадр, который анимация уже прошла.
			//   ★★★ ТЕМП: та же реальная (а не клип-) шкала, что у показа ударов выше.
			const float ClipPosD = AtkCh ? AtkCh->GetAttackSwingElapsedSec() : 0.f;
			float HitFrameAt = (MaxHits > 0 && CT.bValid && CT.HitTimesSec.IsValidIndex(MaxHits - 1))
				? FMath::Max(0.f, CT.HitTimesSec[MaxHits - 1] - ClipPosD)
				: FMath::Max(SwingLen, 0.f);   // нет таблицы кадров → конец замаха, но не «сейчас»
			// ★ Ревизия СРЕДН-5: смерть — на кадре последнего показанного удара, тем же счётом, что и цифры выше.
			if (MaxHits > 0)
			{
				const float Up = OwnPlanDelay(OwnFifoBase + MaxHits - 1);   // ★ Ф2: гейт часов снят (см. `OwnPlanDelay`); ★ 18.10 — FIFO
				if (Up >= 0.f) { HitFrameAt = Up; }
			}
			if (bHavePeerPlan)   // ★ Ф2: сосед — кадр последнего показанного удара его отрезка (нет ударов — первый кадр/конец отрезка)
			{
				HitFrameAt = PeerPlan.HitDelaysS.IsValidIndex(MaxHits - 1) ? PeerPlan.HitDelaysS[MaxHits - 1]
					: (PeerPlan.HitDelaysS.Num() > 0 ? PeerPlan.HitDelaysS[0] : PeerPlan.StartInS + PeerPlan.SegDurS);
			}
			// Дальнобойное оружие: цель падает не раньше, чем до неё долетит последний снаряд.
			float DeathAt = HitFrameAt + 0.05f + (bRangedAtk ? ProjFlightS : 0.f);
			ASDA_CTRACE_IF(bOurs || AsdaCtWorld::Ours(TargetRid, AttackerUserIdx), EAsdaCT::Death, TEXT("%s: СМЕРТЕЛЬНЫЙ ответ (HP=0) — смерть на кадре последнего показанного удара: кадр %.3f + 0.05 + снаряд %.2f = %.3f с"),
				*AsdaCtWorld::Mob(TargetRid, M), HitFrameAt, bRangedAtk ? ProjFlightS : 0.f, DeathAt);
			// ★★★ 18.10.2026 (ключ `asda.Combat.DeathAtLastPendingHit`, разбор у объявления): смерть — на ударе, который доводит полосу
			//   до 0 (поставленные удары всех ролей одним учётом, полёт уже внутри); удары позже него по трупу не показываются.
			//   ★ Ревизия 18.10 (п.1/п.2): смерть — ПОСЛЕ показа смертельного удара (метка `ArmLethalDeath`, запасной таймер с потолком —
			//   перестановка таймеров своего замаха её не обгонит); повторный смертельный ответ срок не отодвигает.
			float LedgerAt = DeathAt;   // срок смертельной записи ленты (с)
			if (IsDeathAtLastPendingHit())
			{
				float LethalS = 0.f;   // до смертельного удара (с, с полётом)
				int32 NUsed = 0;       // учтено поставленных ударов атакующего
				uint32 LethalSeq = 0;  // номер смертельного удара (0 — нет: смерть по сроку)
				if (LethalPlannedHitDelayS(TargetRid, AttackerUserIdx, M->CurHp, LethalS, NUsed, LethalSeq))
				{
					DeathAt = LethalS + 0.05f;
					ASDA_CTRACE_IF(bOurs || AsdaCtWorld::Ours(TargetRid, AttackerUserIdx), EAsdaCT::Death, TEXT("%s: смерть — на ударе №%u, доводящем полосу %d до 0: через %.3f с (%s, учтено ударов атакующего %d; asda.Combat.DeathAtLastPendingHit)"),
						*AsdaCtWorld::Mob(TargetRid, M), LethalSeq, M->CurHp, DeathAt, (M->CurHp <= 0) ? TEXT("полоса уже 0 — сразу") : TEXT("кадр удара + полёт"), NUsed);
				}
				LedgerAt = ArmLethalDeath(M, AttackerUserIdx, LethalSeq, DeathAt, TEXT("смертельный ответ"));
			}
			else { KillTargetAfter(M, DeathAt); }
			// ★ 17.10.2026: смертельный ответ ТОЖЕ открывает окно «наш замах в полёте» — до момента этой смерти. Прежде окно
			//   взводилось только при HP > 0, и серверная смерть 4015 HP=0 / 4017 flags=0 (моб в бою — 1000 мс после
			//   удара) обгоняла выстрел после полного натяжения (разбор у `ArmOurImpactWindow`).
			// ★ Ф1: запись ленты для ЛЮБОГО атакующего — СМЕРТЕЛЬНАЯ: серверная смерть ляжет на этом попадании (у каждой роли — на
			//   своём экране, в момент своего попадания), а не «после последнего попадания любого» (ревизия проекта п.4).
			if (bLedger)
			{
				int32 ShownSum = 0; for (int32 h = 0; h < MaxHits; ++h) { ShownSum += AsdaQueueDamage(Hits[h]); }   // урон показываемых ударов
				ArmImpact(TargetRid, AttackerUserIdx, ImpactKind, LedgerAt, /*bCommitted=*/true, /*bLethal=*/true, ShownSum, /*bOutcomeKnown=*/true,
					TEXT("смертельный ответ"));
			}
		}
	}
	// ★ ВСЕГДА (в т.ч. при ПУСТОЙ очереди — это продолжение расхода буфера): применить серверный HP,
	//   выдержать ритм замахов и обработать смерть. Иначе такие пакеты «пропадали»: HP не падал,
	//   цикл ждал таймаут, цифры не шли (юзер 2026-07-29, моб rid=218).
	if (M && !M->IsDead())
	{
		if (TargetHp > 0)
		{
			// ★ БЕЗ ДВОЙНОГО СЧЁТА: если удары показываются по кадрам, HP уменьшают ОНИ (плавно), а
			//   серверное значение выставляется ТОЧНО в конце серии. Иначе HP сначала прыгал к серверному,
			//   затем уходил ниже локальными вычитаниями и снова «отскакивал» вверх (юзер 2026-07-29).
			if (MaxHitsShown > 0)
			{
				// ★★★ ПОЛОСА ИДЁТ ЗА СЕРВЕРОМ, А НЕ ЗА НАШЕЙ АРИФМЕТИКОЙ (владелец 24.08.2026:
				//   «нужно подгонять к логике сервера, а не эталонного клиента»).
				//
				//   Чисто эталонная модель — «здоровье убывает только от показанных ударов» —
				//   на нашем сервере даёт ОТСТАВАНИЕ. Показ удара привязан к кадру удара плюс
				//   полёт снаряда (у баллисты это ~1.75 с), а сервер отвечает каждые ~1.3 с.
				//   Разница копится, и полоса отстаёт на два замаха. Живой лог 19:29:55:
				//       `HP 679→357` — сервер уже 357, у нас всё ещё 679 после двух попаданий.
				//   Именно это ощущается как «хп и бой очень неотзывчивый».
				//
				//   Поэтому здоровье цели ставим РОВНО серверным значением из этого же пакета,
				//   и делаем это в момент ВИЗУАЛЬНОГО ПОПАДАНИЯ текущего замаха (кадр удара, а у
				//   дальнобойного — прилёт снаряда). Так полоса и не опережает картинку, и не
				//   отстаёт больше чем на одно попадание: каждый ответ сервера доезжает до полосы
				//   в свой замах, а не через два.
				//
				//   Цифры урона по-прежнему берутся из очереди по правилу эталона (ровно StepCount
				//   за замах) — это только показ. Единственный источник самого ЗДОРОВЬЯ — сервер.
				//
				//   ⚠ Вверх не поднимаем: если серверное значение выше нашего, это либо завышенный
				//   снимок, либо мы уже показали больше. Подъём полосы — то, что владелец видел как
				//   «хуяк, приподнялось». Законные подъёмы (реген, возрождение) идут каналами
				//   4013/4019/4017 и сюда не попадают.
				const int32 FinalHp = TargetHp;
				const float SyncAt  = FMath::Max(0.05f, LastHitTimeShown + 0.05f);
				// ★ Лента попаданий (Ф1; прежде — окно «наш замах в полёте», только свои): пока запись ждёт попадания, серверные
				//   каналы HP этой цели сверяются «только вниз» — значение приедет сюда, в момент попадания.
				if (bLedger)
				{
					int32 ShownSum = 0; for (int32 h = 0; h < MaxHitsShown; ++h) { ShownSum += AsdaQueueDamage(Hits[h]); }   // урон показываемых ударов
					// Ответ без видимого урона (все удары — «−2»/0) — исход неизвестен: серверная смерть ждёт это попадание.
					ArmImpact(TargetRid, AttackerUserIdx, ImpactKind, SyncAt, /*bCommitted=*/true, /*bLethal=*/false, ShownSum,
						/*bOutcomeKnown=*/ShownSum > 0, TEXT("ответ на замах"));
					NoteLedgerSnapshot(TargetRid, TargetHp, /*bAllowNumber=*/false);   // «4029 @4» — серверный снимок
				}
				else if (bOurs)
				{
					// ★ Ревизия 17.10, п.9: ключ переключён в 0, пока лента что-то держала (смерть с `bServerDead`, HP) — сначала
					//   применить придержанное (иначе моб остался бы «обречённым»). При ключе 0 с самого начала держать нечего.
					FlushAllLedgers(TEXT("ключ HitFrameAllChannels 0"));
					PendingImpactRid    = TargetRid;
					PendingImpactUntilS = W->GetTimeSeconds() + (double)SyncAt;
					ASDA_CTRACE_IF(bOurs || AsdaCtWorld::Ours(TargetRid, AttackerUserIdx), EAsdaCT::HitFrame, TEXT("%s: окно «наш замах в полёте» (ключ HitFrameAllChannels 0) до +%.2f с — серверное HP этой цели придерживается"),
						*AsdaCtWorld::Mob(TargetRid, M), SyncAt);
				}
				ASDA_CTRACE_IF(bOurs || AsdaCtWorld::Ours(TargetRid, AttackerUserIdx), EAsdaCT::HpPlate, TEXT("%s: серверное HP ответа %d встанет на полосу в момент попадания (+%.2f с, «только вниз»), сейчас полоса %d/%d"),
					*AsdaCtWorld::Mob(TargetRid, M), FinalHp, SyncAt, M->CurHp, M->MaxHp);
				TWeakObjectPtr<AAsdaMobActor> WeakS = M;
				FTimerHandle TS;
				W->GetTimerManager().SetTimer(TS, [this, WeakS, FinalHp, TargetRid, bLedger, AttackerUserIdx]()
				{
					AAsdaMobActor* Mm = WeakS.Get();
					if (!Mm || Mm->IsDead())
					{
						ASDA_CTRACE_IF(AsdaCtWorld::Ours(TargetRid, AttackerUserIdx), EAsdaCT::HpPlate, TEXT("%s: попадание — сверка с HP ответа %d не нужна (%s)"), *AsdaCtWorld::Mob(TargetRid, Mm), FinalHp,
							Mm ? TEXT("моб уже мёртв") : TEXT("актора нет"));
						return;
					}
					int32 Want = FMath::Max(0, FinalHp);
					// ★ Ф1: при общей цели снимок ответа уже включает урон ДРУГИХ атакующих, чьи попадания на этом экране ещё впереди —
					//   полоса не опускается ниже «снимок + их непоказанный урон» (иначе их цифра вычла бы его второй раз).
					bool bOtherUnknown = false;   // у другого атакующего исход неизвестен — опускать нельзя
					const int64 OtherCover = bLedger ? LedgerOtherCover(TargetRid, AttackerUserIdx, bOtherUnknown) : 0;
					if (bOtherUnknown) { Want = INT32_MAX; }
					else { Want = (int32)FMath::Min<int64>((int64)Want + OtherCover, (int64)INT32_MAX); }
					if (Want <= Mm->CurHp)
					{
						ASDA_CTRACE_IF(AsdaCtWorld::Ours(TargetRid, AttackerUserIdx), EAsdaCT::HpPlate, TEXT("%s: попадание — HP полосы %d → %d / %d (сервер %d%s, атакующий uidx=%d)"),
							*AsdaCtWorld::Mob(TargetRid, Mm), Mm->CurHp, Want, Mm->MaxHp, FinalHp,
							(OtherCover > 0) ? *FString::Printf(TEXT(" + непоказанный чужой урон %lld"), OtherCover) : TEXT(""), AttackerUserIdx);
						Mm->CurHp = Want;
						UpdateMobHpBar(Mm);
					}
					// ⚠ ТОЛЬКО ДЛЯ ЖИВОЙ ЦЕЛИ. Без этой проверки правка «принимаем сервер при нуле»
					//   оживляла УЖЕ УБИТОГО монстра: после его смерти приходит снимок другого
					//   канала со старым значением, и полоса прыгала с нуля обратно (живой лог
					//   25.08: «полоса стояла в 0 при серверных 155» сразу после смерти rid=278).
					else if (Mm->CurHp <= 0 && FinalHp > 0 && !Mm->IsDead() && !Mm->bServerDead && !HoldMobHpRaise(Mm, FinalHp, TEXT("попадание ответа: полоса в 0")))
					{
						// ★★★ ЖИВОЙ МОНСТР С ПУСТОЙ ПОЛОСОЙ — ЛЕЧИМ (правка 2026-08-25). Запрет
						//   поднимать полосу защищает от накопителя сервера, но в четырёх случаях
						//   из семи по живому логу он оставлял монстра стоять с нулём при живом
						//   серверном здоровье (138/139/14/82). Ноль на полосе живого монстра —
						//   заведомая ложь, тут сервер авторитетнее нашего прогноза.
						UE_LOG(LogAsdaWorld, Warning,
							TEXT("[бой] rid=%d: полоса стояла в 0 при серверных %d — принимаем сервер ")
							TEXT("(признак расхождения очереди ударов)"), TargetRid, FinalHp);
						ASDA_CTRACE_IF(AsdaCtWorld::Ours(TargetRid, AttackerUserIdx), EAsdaCT::HpPlate, TEXT("%s: попадание — полоса стояла в 0 при живом сервере, HP полосы 0 → %d / %d (принят сервер; расхождение очереди)"),
							*AsdaCtWorld::Mob(TargetRid, Mm), FinalHp, Mm->MaxHp);
						Mm->CurHp = FinalHp;
						UpdateMobHpBar(Mm);
					}
					else
					{
						UE_LOG(LogAsdaWorld, Log,
							TEXT("[бой] rid=%d: полоса не поднимается %d → %d (расхождение вверх на %d)"),
							TargetRid, Mm->CurHp, FinalHp, FinalHp - Mm->CurHp);
						ASDA_CTRACE_IF(AsdaCtWorld::Ours(TargetRid, AttackerUserIdx), EAsdaCT::HpPlate, TEXT("%s: попадание — полоса НЕ поднимается %d → %d (сервер выше на %d%s; подъём только каналами регена)"),
							*AsdaCtWorld::Mob(TargetRid, Mm), Mm->CurHp, FinalHp, FinalHp - Mm->CurHp,
							bOtherUnknown ? TEXT(", у другого атакующего исход неизвестен") : TEXT(""));
					}
				}, SyncAt, false);
			}
			else
			{
				// ★ 17.10.2026: снимок своего замаха уже отложен на кадр удара (блок «снимок здоровья без ударов» выше) —
				//   немедленное применение здесь роняло полосу в момент заноса, вопреки той правке.
				if (bSnapDeferred && bLedger)
				{
					ASDA_CTRACE_IF(bOurs || AsdaCtWorld::Ours(TargetRid, AttackerUserIdx), EAsdaCT::HpPlate, TEXT("%s: ответ без показанных ударов — немедленного применения HP %d НЕТ (снимок уже придержан до кадра удара)"),
						*AsdaCtWorld::Mob(TargetRid, M), TargetHp);
				}
				if (!(bSnapDeferred && bLedger))
				{
					ASDA_CTRACE_IF(bOurs || AsdaCtWorld::Ours(TargetRid, AttackerUserIdx), EAsdaCT::HpPlate, TEXT("%s: ответ без показанных ударов — HP %d применяется сразу (%s)"), *AsdaCtWorld::Mob(TargetRid, M), TargetHp,
						(bOurs || !bObserverSync) ? TEXT("цифра по дельте — только если источник наш замах") : TEXT("замах соседа: только полоса, без цифры"));
					// ★ 17.10.2026 (Ф0, Р2) [ПОРТ]: дельта из ответа на замах СОСЕДА — только полоса. Раньше здесь рисовалась
					//   «наша» цифра (белая, «вы нанесли» в чат, наше оружие) по чужому удару; эталон HP из 4029 к мобу вообще не
					//   применяет и цифры соседа берёт только из его очереди. Ключ отката `asda.Combat.ObserverSync 0`.
					ApplyMobHp(M, TargetHp, /*bAllowNumber=*/bOurs || !bObserverSync, /*bSourceOwn=*/bOurs);   // ударов нет — цифра по дельте (только свой замах)
				}
			}
		}
		else if (TargetHp == 0 && Hits.Num() == 0)
		{
			// ★ Пустая очередь + HP=0: смерть без детализации ударов. Гасить СРАЗУ нельзя — идущий замах
			//   атакующего оборвётся на старте (эталон обрывает состояние 5 только когда HP цели реально
			//   уходит в 0, т.е. не раньше кадра удара).
			// ★★★ ИСПРАВЛЕНО 2026-08-25 (владелец: «моб убит с одного выстрела — выделение и плашка
			//   пропадают сразу»). Здесь ждали ОСТАТОК АНИМАЦИИ, и это давало ноль в двух частых
			//   случаях: замах ещё не начался (ответ сервера приходит через ~30 мс после заявки) или
			//   уже кончился. Живой лог: урон показан в 38.626, труп в 38.780 — через 154 мс, задолго
			//   до кадра удара. Для дальнобойного оружия вдобавок не учитывался полёт снаряда, а
			//   владелец бил именно баллистой.
			//   Считаем как в ветке с очередью: КАДР УДАРА этого замаха + прилёт снаряда.
			const float HitFrameAt0 = (CT.bValid && CT.HitTimesSec.Num() > 0)
				? CT.HitTimesSec[0]
				: FMath::Max(SwingLen, 0.f);            // нет таблицы кадров → конец замаха, но не «сейчас»
			const float SwingLeft = (AtkCh && AtkCh->IsAttackAnimActive())
				? (float)(AtkCh->AttackAnimUntilS - W->GetTimeSeconds()) : 0.f;
			// Полёт снаряда считаем здесь заново: расчёт из ветки с очередью живёт в её области.
			const bool bRangedHere = IsRangedWeapon(WeaponSub) && AtkCh != nullptr;
			float ProjFlightHere = 0.f;
			if (bRangedHere)
			{
				bool bBoneOk = false;
				const FVector MuzzleNow = ProjectileMuzzle(AtkCh, bBoneOk);
				FVector TO, TE; M->GetActorBounds(false, TO, TE);
				ProjFlightHere = AAsdaProjectileActor::FlightTimeSeconds((float)(TO - MuzzleNow).Size(), 2.5f);
			}
			float DeathAt0 = FMath::Max(HitFrameAt0 + 0.05f + ProjFlightHere, SwingLeft);
			if (bHavePeerPlan)   // ★ Ф2: сосед — кадр удара его отрезка (+ полёт), а не «кадр от начала клипа / остаток замаха»
			{
				const float PeerHit = PeerPlan.HitDelaysS.Num() > 0 ? PeerPlan.HitDelaysS[0] : PeerPlan.StartInS + PeerPlan.SegDurS;
				DeathAt0 = PeerHit + 0.05f + ProjFlightHere;
			}
			// ★★★ 18.10.2026 (ключ `asda.Combat.DeathAtLastPendingHit`, разбор у объявления): `SwingLeft` — это конец ТЕКУЩЕГО ПРОХОДА
			//   клипа (гейт замаха живёт весь цикл: у anim_21 проход 6 с — живой журнал 18.10: «остаток замаха 3.65 с»), а
			//   `HitTimesSec[0]` отсчитан от нуля клипа. Новое правило, одинаковое у обеих ролей:
			//     • есть поставленные непоказанные удары этого атакующего по цели — смерть на ударе, где накопленный урон доводит полосу
			//       до 0 (не хватило — на последнем из них); полёт снаряда уже внутри;
			//     • ударов нет, полоса > 0 и замах идёт — ближайший кадр удара (свой — план замаха, сосед — кадр отрезка) + полёт;
			//     • полоса уже 0 или замаха нет — сразу (0.05 с).
			const TCHAR* DeathWhy = TEXT("прежняя формула (ключ asda.Combat.DeathAtLastPendingHit 0)");   // журнал: откуда срок
			uint32 LethalSeq0 = 0;   // ★ ревизия 18.10: номер смертельного удара (0 — нет: смерть по сроку)
			if (IsDeathAtLastPendingHit())
			{
				float LethalS = 0.f;   // до смертельного удара (с, с полётом)
				int32 NUsed = 0;       // учтено поставленных ударов атакующего
				const bool bSwingNow = AtkCh && AtkCh->IsAttackAnimActive();   // замах атакующего идёт
				if (M->CurHp > 0 && LethalPlannedHitDelayS(TargetRid, AttackerUserIdx, M->CurHp, LethalS, NUsed, LethalSeq0))
				{
					DeathAt0 = LethalS + 0.05f;
					DeathWhy = TEXT("удар, доводящий полосу до 0");
				}
				else if (M->CurHp > 0 && bSwingNow)
				{
					float Near = (CT.bValid && CT.HitTimesSec.Num() > 0) ? FMath::Max(0.f, CT.HitTimesSec[0] - AtkCh->GetAttackSwingElapsedSec()) : 0.f;
					const float Up = OwnPlanDelay(0);   // свой: ближайший кадр удара плана (ударов нет — FIFO не сдвигает)
					if (Up >= 0.f) { Near = Up; }
					if (bHavePeerPlan) { Near = PeerPlan.HitDelaysS.Num() > 0 ? PeerPlan.HitDelaysS[0] : PeerPlan.StartInS + PeerPlan.SegDurS; }
					DeathAt0 = Near + 0.05f + ProjFlightHere;
					DeathWhy = TEXT("ударов нет — ближайший кадр удара + полёт");
				}
				else
				{
					DeathAt0 = 0.05f;
					DeathWhy = (M->CurHp <= 0) ? TEXT("полоса уже 0 — сразу") : TEXT("замаха нет — сразу");
				}
				ASDA_CTRACE_IF(bOurs || AsdaCtWorld::Ours(TargetRid, AttackerUserIdx), EAsdaCT::Death, TEXT("%s: смертельный ответ без ударов — срок смерти %.3f с: %s (полоса %d, учтено поставленных ударов атакующего %d; прежняя формула дала бы max(кадр от нуля %.2f, конец прохода %.2f))"),
					*AsdaCtWorld::Mob(TargetRid, M), DeathAt0, DeathWhy, M->CurHp, NUsed, HitFrameAt0 + 0.05f + ProjFlightHere, SwingLeft);
			}
			UE_LOG(LogAsdaWorld, Log,
				TEXT("[бой] rid=%d: смерть без очереди ударов — гасим через %.2f с ")
				TEXT("(кадр удара %.2f, снаряд %.2f, остаток замаха %.2f; срок: %s)"),
				TargetRid, DeathAt0, HitFrameAt0, ProjFlightHere, SwingLeft, DeathWhy);
			ASDA_CTRACE_IF(bOurs || AsdaCtWorld::Ours(TargetRid, AttackerUserIdx), EAsdaCT::Death, TEXT("%s: смертельный ответ БЕЗ ударов в очереди — смерть через %.2f с (кадр удара %.2f, снаряд %.2f, остаток замаха %.2f%s)"),
				*AsdaCtWorld::Mob(TargetRid, M), DeathAt0, HitFrameAt0, ProjFlightHere, SwingLeft, bHavePeerPlan ? TEXT(", по кадру отрезка соседа") : TEXT(""));
			// ★ Ревизия 18.10 (п.1/п.2): при ключе — смерть после показа смертельного удара (метка, запасной таймер с потолком) или по
			//   сроку; повторный смертельный ответ срок не отодвигает. Ключ 0 — прежний таймер.
			float LedgerAt0 = DeathAt0;   // срок смертельной записи ленты (с)
			if (IsDeathAtLastPendingHit()) { LedgerAt0 = ArmLethalDeath(M, AttackerUserIdx, LethalSeq0, FMath::Max(0.05f, DeathAt0), TEXT("смертельный ответ без ударов")); }
			else { KillTargetAfter(M, FMath::Max(0.f, DeathAt0)); }
			if (bLedger) { ArmImpact(TargetRid, AttackerUserIdx, ImpactKind, LedgerAt0, /*bCommitted=*/true, /*bLethal=*/true, 0, /*bOutcomeKnown=*/true, TEXT("смертельный ответ без ударов")); }   // ★ Ф1
		}
		else if (TargetHp < 0)
		{
			ASDA_CTRACE_IF(bOurs || AsdaCtWorld::Ours(TargetRid, AttackerUserIdx), EAsdaCT::HpPlate, TEXT("%s: в ответе нет поля HP (%d) — полоса не трогается"), *AsdaCtWorld::Mob(TargetRid, M), TargetHp);
		}
	}
	else
	{
		ASDA_CTRACE_IF(bOurs || AsdaCtWorld::Ours(TargetRid, AttackerUserIdx), EAsdaCT::HpPlate, TEXT("%s: HP ответа %d не применяется — %s"), *AsdaCtWorld::Mob(TargetRid, M), TargetHp,
			M ? TEXT("цель уже труп") : TEXT("моба нет в мире"));
	}
	// ★ 18.10.2026 (пол HP): удары ответа поставлены — пол ниже полосы, а непоказанного урона не хватает, — полоса сразу к нему.
	//   Снимок без ударов, придержанный до кадра удара (полоса не падает на заносе — 24.08), опустит полосу сам на кадре удара.
	if (M && !M->IsDead() && TargetHp >= 0 && Net->CombatAnimSrcOpcode == 4029 && !bSnapDeferred) { ApplyHpFloorNow(M, TEXT("4029")); }
	if (bOurs)
	{
		if (AAsdaPlayerController* PC = Cast<AAsdaPlayerController>(GetGameInstance()->GetFirstLocalPlayerController()))
		{
			PC->OnCombatAckReceived(SeriesLenForAck);   // ритм держится на ЛЮБОМ ответе сервера
			ASDA_CTRACE(EAsdaCT::Swing, TEXT("свой ответ по %s принят контроллером (OnCombatAckReceived): длительность серии %.2f с, показано ударов %d"),
				*AsdaCtWorld::Mob(TargetRid, M), SeriesLenForAck, MaxHitsShown);
		}
	}
}

void UAsdaWorldSubsystem::UpdateMobHpBar(AAsdaMobActor* M)
{
	if (!M) { return; }
	if (UAsdaNameplateComponent* NP = M->FindComponentByClass<UAsdaNameplateComponent>())
	{
		// ★ Д8: число сегментов полосы = «жизни» типа (`Mob list.BIN +0x10C` → сущность +18700).
		//   У обычных мобов там 0 ⇒ одиночная полоса, как и было; у боссов появляется сегментация.
		NP->SetHp(M->CurHp, M->MaxHp, /*Segments=*/M->MobLives);
	}
}

// ★ ЖИВАЯ ПРОВЕРКА СНАРЯДОВ (`asda.VerifyProjectile`). Headless слеп к billboard-ветке эффектов, поэтому
//   финальная сверка — командой В ИГРЕ: печатает тип оружия, наличие кости «8_b1», мировую точку выстрела,
//   наличие ассетов (меши стрелы, BP шлейфа), список живых снарядов; с аргументом `fire` — пробный выстрел
//   по текущей цели (замер стартовой точки vs позиции оружия и расчётной длительности полёта).
static void AsdaVerifyProjectileCmd(const TArray<FString>& Args, UWorld* World)
{
	if (!World || !World->GetGameInstance()) { UE_LOG(LogAsdaWorld, Warning, TEXT("[снаряд-замер] мира нет")); return; }
	UAsdaWorldSubsystem* WS = World->GetGameInstance()->GetSubsystem<UAsdaWorldSubsystem>();
	UAsdaNetSubsystem*   NS = World->GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>();
	if (!WS) { return; }
	AAsdaPlayerCharacter* Me = Cast<AAsdaPlayerCharacter>(WS->LocalPlayerActor.Get());
	UE_LOG(LogAsdaWorld, Warning, TEXT("[снаряд-замер] ─────────── проверка подсистемы снарядов ───────────"));
	// Ассеты
	const bool bMeshFly  = LoadObject<UStaticMesh>(nullptr, kArrowMeshFly)  != nullptr;
	const bool bMeshNock = LoadObject<UStaticMesh>(nullptr, kArrowMeshNock) != nullptr;
	// ★ В1-C F5: путь класса — общим правилом по .jeb шлейфа (тот же путь: правило меняет «-» на «_»), не литералом.
	const bool bTrail    = AAsdaEffectActor::ResolveJebClass(kArrowTrailJeb) != nullptr;
	UE_LOG(LogAsdaWorld, Warning, TEXT("[снаряд-замер] ассеты: меш полёта=%s, меш тетивы=%s, BP шлейфа=%s"),
		bMeshFly ? TEXT("ЕСТЬ") : TEXT("НЕТ"), bMeshNock ? TEXT("ЕСТЬ") : TEXT("НЕТ"), bTrail ? TEXT("ЕСТЬ") : TEXT("НЕТ (запусти asda_build_char_fx.py)"));
	// Оружие + точка выстрела
	int32 WSub = -1;
	if (NS && NS->SelfWeaponItemId > 0)
	{
		if (UAsdaItemTableSubsystem* IT = World->GetGameInstance()->GetSubsystem<UAsdaItemTableSubsystem>())
		{
			if (const FAsdaItemRecord* R = IT->GetItem(NS->SelfWeaponItemId)) { WSub = R->ItemSubType; }
		}
	}
	UE_LOG(LogAsdaWorld, Warning, TEXT("[снаряд-замер] своё оружие itemId=%d subType=%d → %s"),
		NS ? NS->SelfWeaponItemId : 0, WSub,
		UAsdaWorldSubsystem::IsRangedWeapon(WSub) ? TEXT("ДАЛЬНОБОЙНОЕ (снаряд есть)") : TEXT("ближний бой (снаряда нет)"));
	if (Me)
	{
		bool bBone = false;
		const FVector Muzzle = WS->ProjectileMuzzle(Me, bBone);
		const FVector ActorL = Me->GetActorLocation();
		FVector WeaponL = FVector::ZeroVector; bool bWeap = false;
		for (UActorComponent* C : Me->GetComponents())
		{
			if (UStaticMeshComponent* SM = Cast<UStaticMeshComponent>(C))
			{
				if (SM->GetFName() == FName(TEXT("WeaponMesh"))) { WeaponL = SM->GetComponentLocation(); bWeap = true; }
			}
		}
		UE_LOG(LogAsdaWorld, Warning, TEXT("[снаряд-замер] старт: кость 8_b1=%s точка=%s | актор=%s | меш оружия=%s (расхождение %.1f юн)"),
			bBone ? TEXT("ЕСТЬ") : TEXT("НЕТ"), *Muzzle.ToCompactString(), *ActorL.ToCompactString(),
			bWeap ? *WeaponL.ToCompactString() : TEXT("нет"), bWeap ? (float)(Muzzle - WeaponL).Size() : -1.f);
	}
	// Живые снаряды
	int32 nProj = 0;
	for (TActorIterator<AAsdaProjectileActor> It(World); It; ++It)
	{
		AAsdaProjectileActor* P = *It;
		UE_LOG(LogAsdaWorld, Warning, TEXT("[снаряд-замер]   живой снаряд: поз=%s старт=%s цель=%s t=%.2f/%.2fс скорость=%.0f uu/с шлейф=%s"),
			*P->GetActorLocation().ToCompactString(), *P->StartPos.ToCompactString(), *P->EndPos.ToCompactString(),
			P->ElapsedS, P->FlightTimeS, P->SpeedUnitsPerMs * 1000.f, P->TrailFx ? TEXT("есть") : TEXT("НЕТ"));
		++nProj;
	}
	UE_LOG(LogAsdaWorld, Warning, TEXT("[снаряд-замер] живых снарядов: %d"), nProj);
	// Пробный выстрел по текущей цели
	if (Args.Num() > 0 && Args[0].Equals(TEXT("fire"), ESearchCase::IgnoreCase) && Me)
	{
		AActor* Tgt = nullptr;
		if (AAsdaPlayerController* PC = Cast<AAsdaPlayerController>(World->GetFirstPlayerController())) { Tgt = PC->TargetActor.Get(); }
		if (!Tgt) { UE_LOG(LogAsdaWorld, Warning, TEXT("[снаряд-замер] цели нет — выбери моба и повтори")); return; }
		const int32 UseSub = UAsdaWorldSubsystem::IsRangedWeapon(WSub) ? WSub : 8;
		const float T = WS->FireProjectile(Me, Tgt, UseSub, NS ? (int32)NS->SelfUserNum : 0, 0, nullptr);
		UE_LOG(LogAsdaWorld, Warning, TEXT("[снаряд-замер] пробный выстрел (тип оружия %d): полёт %.2fс"), UseSub, T);
	}
}

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs CmdAsdaVerifyProjectile(
	TEXT("asda.VerifyProjectile"),
	TEXT("Дамп подсистемы снарядов: ассеты, тип оружия, точка выстрела (кость 8_b1), живые снаряды. Аргумент `fire` — пробный выстрел по цели."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&AsdaVerifyProjectileCmd));
#endif // ASDA_DEV_CONSOLE
