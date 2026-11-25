// Copyright (c) 2026 @sitxovski (Discord)
// AsdaWorldSubsystem_EntitiesPeer.cpp — ★ R3-c 12.11.2026 (разрез AsdaWorldSubsystem_Entities.cpp, дословный перенос): чужие игроки: появление, таблицы имён, плашка, темп атаки и анимации, поиск по номерам, дев-команды.
//   Общее для частей — в AsdaWorldSubsystem_Entities_Private.h; шапка и реверс — в AsdaWorldSubsystem_Entities.cpp.
#include "World/AsdaAttackExit.h"   // ★ 01.11.2026: 4024/400 — выход своего из действия атаки (AsdaLeaveAttackAction)
#include "World/AsdaWorldSubsystem.h"
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "HAL/IConsoleManager.h"        // ключ `asda.Move.RefusePlank`
#include "Resources/AsdaModelAnimSubsystem.h"   // Д2/Д8/Д9: ресурс только через менеджер
#include "AsdaNetSubsystem.h"
#include "AsdaSelf.h"   // ★ «это я?» — единая точка своего номера (E2, 14.09.2026)
#include "World/AsdaPlayerCharacter.h"
#include "World/AsdaAttackRhythmSubsystem.h"   // ★ 15.10.2026: ключ отката ритма (темп предметов), кадры ударов своего замаха
#include "Framework/Application/SlateApplication.h"
#include "Npc/AsdaNpcActor.h"
#include "Npc/AsdaMobActor.h"
#include "World/AsdaFadeComponent.h"
#include "World/AsdaCoord.h"                 // ★ Аудит №3 (B1/B2): единый перевод координат и тайловая сетка
#include "World/AsdaPlayerController.h"      // снятие таргета при смерти цели-моба (эталон SetActionState(10))
#include "Materials/MaterialInterface.h"      // материал меша стрелы (у ..._1 ссылка битая)
#include "Resources/AsdaGameUserSettings.h"  // ★ 17.10.2026 (Ф5): режим показа цифр урона `DamageTextMode`
#include "Skill/AsdaSkillFxSubsystem.h"      // ★★★ стык «урон ↔ визуал»: момент удара навыка
#include "World/AsdaFxLightSettings.h"         // ★ волна 3 п.16 (15.09.2026): AsdaWeaponGlow — имя свечения заточки (одна таблица баз)
#include "Items/AsdaItemTableSubsystem.h"    // тип оружия атакующего (импакт battk01..04)
#include "UI/AsdaInventorySubsystem.h"       // надетое (контейнер 3): проверка сломанных вещей
#include "UI/AsdaSkillDatabase.h"            // ★ Аудит №3: поля записи навыка — только отсюда
#include "UI/AsdaEffectsSubsystem.h"         // ★ состояния мобов: 4017 бафф-стеки и бафф отражения
#include "Engine/DataTable.h"
#include "Resources/AsdaSoundSubsystem.h"    // звуки ударов (S_c_*normal1, фазовые звуки мобов)
#include "Components/WidgetComponent.h"          // ★ плашка имени — тоже UMeshComponent, но не тело
#include "Engine/StaticMesh.h"
#include "World/AsdaNameplateComponent.h"   // над-головные плашки (порт JUI-окон 1:1)
#include "World/AsdaEntityText.h"           // ★ волна 3 п.14: имена/модели/звук-базы мобов и NPC — одна точка
#include "AsdaDataTableCache.h"             // AsdaDt::Cached — объекты таблиц имён для сильного удержания (HoldEntityNameTables)
#include "World/AsdaPetActor.h"              // питомцы (6120, pet_system_re.md)
#include "World/AsdaSoulGuardianActor.h"     // ★ компаньон-хранитель «ангел» (6156, soul_guardian_companion_re.md)
#include "Misc/ConfigCacheIni.h"             // GConfig — опции показа ангела (FxConfig)
#include "AsdaEffectActor.h"                 // эффекты персонажей: крылья/кольца soulmate (follow-кость)
#include "ProceduralMeshComponent.h"         // asda.VerifyWeaponGlow: чтение мировых вершин секций эффекта
// loc-нотисы подбора (1011/1012/1030)   ★ R3: комментарий снятого включения "Text/AsdaTextSubsystem.h" (файл им не пользовался) — сохранён
// UAsdaRichText::ApplyFormatted (единая точка текста)   ★ R3: комментарий снятого включения "UI/AsdaMarkup.h" (файл им не пользовался) — сохранён
#include "World/AsdaClanFlag.h"              // FAsdaClanFlagData::FromWire (флаг клана из 6542)
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/UObjectGlobals.h"
// ★ С-Р1: общий офлайн-гейт отладочных команд (RefuseDebugWhenOnline)   ★ R3: комментарий снятого включения "AsdaOutgoingGate.h" (файл им не пользовался) — сохранён
#include "World/AsdaCombatTrace.h"           // ★ 18.10.2026: журнал боя (ASDA_CTRACE) — полный цикл действий клиента в бою
#include "AsdaWorldSubsystemInternal.h"   // ★ 23.10.2026 разнос: общее для частей AsdaWorldSubsystem*.cpp (LogAsdaWorld, журнал боя, чистые хелперы)
#include "World/AsdaCrouchStand.h"        // ★ 05.11.2026: присед соседа стоя — 4025 132/133 и код появления 132
#include "AsdaWorldSubsystem_Entities_Private.h"   // ★ R3-c 12.11.2026: общее для частей AsdaWorldSubsystem_Entities*.cpp

// ★★★ МНОЖИТЕЛЬ ТЕМПА АТАКИ (полный разбор — у объявления `GetAttackTempoMul`).
//   1 = считаем как эталон (состояния 27/174/28/233/247 + надетые предметы, плюс восстановленное
//   состояние 12 — см. `asda.Combat.AttackTempoState12`); 0 = темп всегда 1.
static ASDA_DEV_CVAR(int32, CVarAttackTempo,
	TEXT("asda.Combat.AttackTempo"), 1,
	TEXT("1 = множитель темпа атаки из состояний и предметов (эталон); 0 = темп всегда 1.0."),
	ECVF_Default);

// ★★★ ИСПРАВЛЕНИЕ ПЕРВОГО ДЕФЕКТА ОРИГИНАЛА в расчёте темпа: пара «база / за уровень» берётся из
//   ТОГО буфф-слота записи навыка, в котором реально лежит состояние, а не всегда из слота 0.
//   1 = наше исправление; 0 = дословный эталон (всегда S1). Разбор — у `GetAttackTempoMul`.
static ASDA_DEV_CVAR(int32, CVarAttackTempoBuffSlotFix,
	TEXT("asda.Combat.AttackTempoBuffSlotFix"), 1,
	TEXT("1 = величина вклада берётся из СВОЕГО буфф-слота навыка (исправление); 0 = эталон (всегда S1)."),
	ECVF_Default);

// ★★★ ИСПРАВЛЕНИЕ ТРЕТЬЕГО ДЕФЕКТА ОРИГИНАЛА: состояние 12 («дополнительное снижение скорости атаки»
//   по корейскому имени в непереведённой серверной таблице). Сервер вешает его по-настоящему —
//   навыки 632 (до −0.25), 820 (до −0.60), 823 (до −0.55) — а клиент оригинала не читает его НИГДЕ,
//   и эффект теряется: он виден в списке баффов, но ни на что не влияет. Это НЕ мёртвая ветка,
//   в отличие от состояния 247 (то на живой сборке не приходит вовсе из-за данных).
//   1 = учитываем (наше исправление); 0 = как оригинал — состояние 12 не влияет на темп.
static ASDA_DEV_CVAR(int32, CVarAttackTempoState12,
	TEXT("asda.Combat.AttackTempoState12"), 1,
	TEXT("1 = состояние 12 замедляет темп атаки (исправление дефекта оригинала); 0 = как оригинал (не влияет)."),
	ECVF_Default);

// ★★★ [ОРИГИНАЛ] ТЕМП ОТ НАДЕТЫХ ПРЕДМЕТОВ В ТЕМПЕ АТАКИ (15.10.2026, ритм атаки, `wave_PORT_attack_rhythm.md`).
//   Клиент оригинала прибавлял `LinkedGroupCode/100` надетых вещей к скорости замаха (game.c:36452-36468), а NpcServer
//   в длительности удара предметного слагаемого НЕ имеет (`sub_45F3A0`, NpcServer.exe.c:63128-63148: только состояния
//   27/28/174/233/247/52/12). Вещи ускоряли картинку и отправку быстрее сервера: ивент-наборы до +18 %, луки
//   75457/75460 +20/+30 %, шлемы 20712/20714 +220/+340 % — сервер молча отбрасывал такие удары. Исправление дефекта
//   оригинала: по умолчанию предметы в темп атаки НЕ входят (свой персонаж к тому же живёт по серверным часам
//   `UAsdaAttackRhythmSubsystem`, сосед — по своим 4029). Шкалу навыков (`GetAnimSpeedScale`) это не трогает.
//   ★ Соседи (ревизия НИЗК-6): сервер их картинку не гейтит, но все соседи — клиенты порта, и их удары теперь идут по тем же
//   серверным часам БЕЗ предметного темпа; оставь мы предметы в их замахе, у носящих вещи на скорость клип соседа бежал бы
//   быстрее его настоящих ударов (до +340 %) — «пустые» взмахи у чужих. Поэтому предметы сняты и у соседей.
//   1 = как оригинал (предметы ускоряют замах); 0 = исправление.
static ASDA_DEV_CVAR(int32, CVarAttackTempoItems,
	TEXT("asda.Combat.AttackTempoItems"), 0,
	TEXT("1 = надетые предметы ускоряют замах (как оригинал, сервер этого не учитывает); 0 = нет (исправление, по умолчанию)."),
	ECVF_Default);

// ★★★ МНОЖИТЕЛЬ ТЕМПА АНИМАЦИЙ НАВЫКОВ И ЭФФЕКТОВ — эталонное поле `CCharacter + 0x500`
//   (`animSpeedScale`). Полный разбор потока, все 110 потребителей и доказательство того, что
//   базовой атаки он НЕ касается — у объявления `GetAnimSpeedScale` в `AsdaWorldSubsystem.h`.
//   ★ В формуле УЧАСТВУЕТ состояние 78 (оно ПЕРЕБИВАЕТ предметный вклад) — см. ключ
//   `asda.Combat.AnimSpeedScaleState78` ниже. Прежняя редакция этих строк утверждала обратное:
//   она писалась до того, как третий писатель поля был найден вторым методом.
//   1 = считаем как эталон (надетые предметы, а состояние 78 их перебивает);
//   0 = темп анимаций навыков и эффектов всегда 1.0.
static ASDA_DEV_CVAR(int32, CVarAnimSpeedScale,
	TEXT("asda.Combat.AnimSpeedScale"), 1,
	TEXT("1 = множитель темпа анимаций навыков и эффектов (эталон: состояние 78 либо предметы); 0 = всегда 1.0."),
	ECVF_Default);

// ★★★ СОСТОЯНИЕ 78 «Повышение скорости навыков» — ТРЕТИЙ писатель `animSpeedScale` у оригинала.
//   Эталон `Skill_ApplyAttackSpeedBuff@0x99AFC0` (имя ВРЁТ: скорости атаки функция не касается,
//   game.c:33330-33339) вызывается из тика состояния 24 (`charUpdateActionStateTick@0xA216B0`,
//   game.c:81495, и его второй копии `@0xA220E0`, game.c:81867) КАЖДЫЙ КАДР применения навыка:
//       entry = CList_FindByTypeSub(char + 638, 78, 0);
//       if (entry && (rec = CSkillMgr_GetSkillRecord(…, *(i16 *)(entry + 2))))
//           char->animSpeedScale = (entry[9] * rec->s1PerLevel + rec->s1Base) / 100.0;
//           char->animSpeedScale = char->animSpeedScale + 1.0;
//   ⚠ Это ЗАМЕЩЕНИЕ, а не прибавка: вклад предметов при активном 78 пропадает целиком.
//   Написано двумя отдельными операторами — то есть намеренно; воспроизводим как есть.
//   Носители состояния (живые данные `skill_info`, 1255 записей): навыки 630, 839, 1060
//   «Повышение скорости навыков», у всех трёх слот 0, S1 = 0 + 2·уровень, кап уровня 5 ⇒ до +0.10.
//   1 = учитываем состояние 78 (эталон); 0 = формула только по предметам.
static ASDA_DEV_CVAR(int32, CVarAnimSpeedScaleState78,
	TEXT("asda.Combat.AnimSpeedScaleState78"), 1,
	TEXT("1 = состояние 78 «Повышение скорости навыков» задаёт темп анимаций навыков (эталон); 0 = только предметы."),
	ECVF_Default);

static ASDA_DEV_CVAR(int32, CVarPeerPetApplyOnSpawn,
	TEXT("asda.Pet.PeerApplyOnSpawn"), 1,
	TEXT("1 (по умолчанию) — питомец соседа применяется СРАЗУ при спавне владельца, не дожидаясь ")
	TEXT("слива очереди (УЛУЧШЕНИЕ: у эталона он появился бы до 2 с спустя). 0 — только сливом."),
	ECVF_Default);

// ── ★ Спавн ЧУЖОГО игрока (снапшот 4005/4065): павн без Possess, внешность CharAssembly, неймплейт, фейд-ин ──
void UAsdaWorldSubsystem::SpawnRemotePlayer(const FAsdaRemotePlayer& P)
{
	UWorld* W = GetGameInstance()->GetWorld();
	if (!W) { return; }
	// game→UE оси: (x,z)→(-z,x); Y = высота земли трейсом (Y в 4005 не передаётся, как и в движении).
	const FVector2D UeXY = AsdaCoord::GameToUe2D(P.PosGame);   // ★ Аудит №3 (B1)
	const FVector SpawnLoc(UeXY.X, UeXY.Y, TerrainZ(UeXY));

	FActorSpawnParameters SP; SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AAsdaPlayerCharacter* Ch = W->SpawnActor<AAsdaPlayerCharacter>(AAsdaPlayerCharacter::StaticClass(), SpawnLoc, FRotator::ZeroRotator, SP);
	if (!Ch) { UE_LOG(LogAsdaWorld, Warning, TEXT("[net] игрок userIdx=%d: павн не создан"), P.UserIdx); return; }
	Ch->SetActorLocation(SpawnLoc);

	// Внешность: слоты снапшота = слоты char-list (0=шлем,1=тело,2=штаны,3=обувь,4=перчатки; 9=оружие — фаза боя).
	FAsdaNetPlayerEntry E;
	// ★ ПОЛ соседа = байт char+1079 снапшота (4005 body[37] / 4065 rec[54]). Значение вне {1,2}
	//   означает, что поле не заполнено — это надо ВИДЕТЬ, а не молча рисовать мужчину (именно так
	//   пряталась старая ошибка «пол читали из байта созвездия»).
	if (P.Gender != 1 && P.Gender != 2)
	{
		UE_LOG(LogAsdaWorld, Warning, TEXT("[пол] сосед userIdx=%d '%s': байт пола = %u (не 1/2) — рисуем мужскую модель"),
			P.UserIdx, *P.Name, (uint32)P.Gender);
	}
	E.Sex  = (P.Gender == 2) ? 2 : 1;
	E.Name = P.Name;
	E.CharId = P.CharId;   // для клан-сброса 4133 (эталон матчит CharManager_FindByCharId)
	// ★ Класс и уровень соседа: снапшот их несёт (4005@38/@40, 4065 rec@55/@57), а порт до
	//   2026-08-24 выбрасывал. Панель цели читает оба: класс → эмблема `char_emblems`,
	//   уровень → спрайтовые цифры (эталон `HUD_UpdateTargetFrame`/`HUD_TargetLevelDigits`).
	E.ClassId = (int32)P.ClassId;
	E.Level   = (int32)P.Level;
	// ★ 2026-09-17 (окно `OTHERCHARA_DATA` 4200): подкласс (char+1081) и СОЗВЕЗДИЕ (char+1120)
	//   снапшот тоже несёт, а реестр их выбрасывал. Оба нужны `CPlayerInfoWin_SetData@0xDA7E30`:
	//   подкласс выбирает, какое имя профессии печатать, созвездие — название знака и одну из трёх
	//   иконок влечения. ⚠ 4065 этих полей НЕ несёт (это батч состояний, а не снапшот появления),
	//   поэтому после дренажа 4065 значения остаются от 4005 — как и класс с уровнем.
	E.SubClassId    = (int32)P.SubClass;
	E.Constellation = (int32)P.Constellation;
	E.Parts.HairStyle = P.Hair;
	E.Parts.HairColor = P.HairColor;
	E.Parts.FaceIndex = P.Face;
	E.Parts.Helmet = P.EquipItemId[0]; E.Parts.Chest = P.EquipItemId[1]; E.Parts.Pants = P.EquipItemId[2];
	E.Parts.Shoes  = P.EquipItemId[3]; E.Parts.Gloves = P.EquipItemId[4];
	E.Parts.Weapon = P.EquipItemId[9];   // оружие: 4005 несёт слот 9; из 4065 (592б) слот 9 за границей → 0xFB5
	// ★ 25.10.2026 [ОРИГИНАЛ] щит соседа — слот 8 записи экипа 4005 (+136): `Recv_CharFullSpawn` → `SetParts_Armor`, меш на "_7_a1".
	E.Parts.Shield = P.EquipItemId[8];
	// ★★★ 25.10.2026 [ОРИГИНАЛ] ПОКАЗ ШЛЕМА соседа из 4005 [6] (`charMgr_createRemoteEntity`: a179 → char+0x32F78).
	//   Только при СОЗДАНИИ: эталон уже живому соседу флаг повторным 4005 не меняет — это делает 4025/111.
	E.Parts.bHelmetShown = (P.HelmetShow != 0);
	// ★ ЗАТОЧКА соседа (свечение оружия/щита): 5-й байт записи экипа 4005 / байт +24 инстанса 4065.
	E.WeaponGrade  = (int32)P.EquipGrade[9];
	E.ShieldItemId = P.EquipItemId[8];
	E.ShieldGrade  = (int32)P.EquipGrade[8];
	Ch->InitAppearance(E.Sex, E.Parts);
	// Свечение заточенного оружия/щита соседа (эталон зовёт UpdateHeldItemEffect/UpdateEquipAuraEffect
	//   на КАЖДОМ видимом персонаже, entity_char.c:8773).
	ApplyWeaponGlowToActor(Ch, E.Parts.Weapon, E.WeaponGrade, E.ShieldItemId, E.ShieldGrade, P.UserIdx);
	if (P.ActionState == 200) { Ch->SetAnimState(EAsdaPlayerState::Dead); }   // 4065@102: 200 = труп
	// ★ 10.10.2026 (копание): сосед появился УЖЕ копающим — `GameNet_OnUserSpawn@0x9CE750` (game_main.c:7303/7425): код
	//   состояния @102 110 → `CChar_SetState(…, 40)` + `c-shovel-a_01`, 114 (=112+2) → 43 + `c-shovel-b_01`, эффект с привязкой 6;
	//   позиция — тайл пакета (её уже поставил спаун). Остальные коды состояний появления в порту не перенесены (как и прежде).
	else if (P.ActionState == 110 || P.ActionState == 114)
	{
		Ch->EnterEtalonShovelState(P.ActionState == 110 ? 40 : 43, EAsdaShovelEntry::Spawn);
	}
	// ★ 30.10.2026 [ОРИГИНАЛ] код @102 121 (=112+9) → `CChar_SetState(…, 51)` на тайле пакета (game_main.c:7343-7351): сосед вошёл
	//   в обзор во время жеста поражения — клип трека 26 с начала, один раз. Позу 47 спаун не передаёт (только 6724 sub 1).
	else if (P.ActionState == 121)
	{
		Ch->EnterCampPose(51, TEXT("спаун 4065, код 121"));
	}
	// ★ 24.10.2026 [ПОРТ] сосед появился посреди хода ВПРИСЯДКУ: код @102 — то же поле сервера CUser+0x54E4, куда CharMove пишет
	//   статус хода 4006 (у приседа порта — 4). Без этого вошедший в обзор видел бы его бегущим до следующего 4007.
	else if (P.ActionState == AAsdaPlayerCharacter::kCrouchMoveStatus)
	{
		Ch->SetNetCrouch(true);
	}
	// ★ 05.11.2026 [ПОРТ] сосед появился, присев СТОЯ: код @102 — последний 4024 132 (то же поле CUser+0x54E4).
	else if (P.ActionState == AsdaCrouchStand::kStandOnCode && AsdaCrouchStand::RelayOn())
	{
		Ch->SetNetCrouch(true);
	}

	FillNameplate(Ch, P.Name);   // штатная плашка игрока (JUI TARGETMARK_MAN1 1:1)

	// ★ Клан из кэша: снапшот 6153/4135 пришёл раньше спавна (связка 5417→…→4135) — применить сейчас.
	if (const FAsdaPendingClan* C = PendingClanByCharId.Find(P.CharId))
	{
		E.ClanId = C->ClanId;
		ApplyClanToActor(Ch, *C);
		PendingClanByCharId.Remove(P.CharId);
	}
	// ★ Титул/ранг из кэша: 6068/6078 пришли раньше спавна (сервер шлёт их и вне AoI).
	if (const FIntVector* T = PendingTitleByUserIdx.Find(P.UserIdx))
	{
		if (UAsdaNameplateComponent* NP = Ch->FindComponentByClass<UAsdaNameplateComponent>())
		{
			NP->RankValue = T->Z;
			NP->SetTitles(T->X, T->Y);
		}
		PendingTitleByUserIdx.Remove(P.UserIdx);
	}

	// ★ Крылья/кольцо из кэша: 6585/5220 приходят в enter-map флоу (могут опередить спавн актора).
	if (const FAsdaWingState* WSt = WingByCharId.Find(P.CharId))
	{
		if (WSt->bOn) { ApplyWingToActor(Ch, E.Sex != 2, WSt->WingId, true); }
	}
	if (const FAsdaSoulState* SSt = SoulByUserIdx.Find(P.UserIdx))
	{
		if (SSt->SoulIdx > 0) { ApplySoulRingToActor(Ch, true); }
	}
	// ★ Основной канал чужих колец — 6152 по charId (AoI-бандл опережает спавн актора).
	if (const int32* SIdx = SoulIdxByCharId.Find(P.CharId))
	{
		if (*SIdx > 0) { ApplySoulRingToActor(Ch, true); }
	}
	// ★★★ ОДЕРЖИМОСТЬ СОСЕДА: ставим ВСЕГДА и явно (в том числе 0), а не «только если нашли».
	//   Причина — сервер ПЕРЕИСПОЛЬЗУЕТ userIdx и актор может быть создан заново под тем же слотом:
	//   молчаливый пропуск оставил бы на плашке метку предыдущего жильца (та же грабля, что была
	//   у транспорта/крыльев, см. чистку кэшей в обработчике 4011).
	ApplySoulStateToActor(Ch, ResolveSoulStateFor(P.CharId, P.UserIdx));
	// ★ Фракция/звание соседа (6722 приходит в бандле входа в мир — до спавна актора).
	if (const FIntPoint* Fac = FactionByCharId.Find(P.CharId))
	{
		if (UAsdaNameplateComponent* NP = Ch->FindComponentByClass<UAsdaNameplateComponent>())
		{
			NP->SetCamp(Fac->X, Fac->Y);
		}
	}
	// ★ Питомец соседа (6120 тоже в AoI-бандле — приходит раньше спавна владельца).
	//   ★ ВОЛНА 6: это УЛУЧШЕНИЕ над эталоном (у него питомец появился бы только на ближайшем
	//   сливе очереди, до 2 с спустя). Ключ `asda.Pet.PeerApplyOnSpawn 0` возвращает эталонный
	//   порядок. Запись при этом СНИМАЕТСЯ С ОЧЕРЕДИ: иначе слив применил бы её второй раз, а
	//   `ApplyPetToActor` пересоздаёт актор питомца — игрок увидел бы мигание.
	if (CVarPeerPetApplyOnSpawn.GetValueOnGameThread() != 0)
	{
		if (const TPair<int32, FString>* Pet = PetByCharId.Find(P.CharId))
		{
			if (Pet->Key >= 0) { ApplyPetToActor(Ch, Pet->Key, Pet->Value); }
			for (int32 i = PeerPetQueue.Num() - 1; i >= 0; --i)
			{
				if (PeerPetQueue[i].Get<0>() == P.CharId) { PeerPetQueue.RemoveAt(i); }
			}
		}
	}
	// ★ Транспорт соседа (5453 мог прийти до спавна актора).
	if (const int32* MountItem = MountByUserIdx.Find(P.UserIdx))
	{
		FString Model, AnimSet; float H = 0.f;
		if (LookupMount(*MountItem, Model, AnimSet, H))
		{
			HoldSpawnTables(EAsdaSpawnTables::Mount);   // ★ 15.09 (dt_gc_hold): DT_RideEffect — сильно у мира
			FString RFx, RSnd, RSnd2, RSnd3; LookupRideFx(*MountItem, RFx, RSnd, RSnd2, RSnd3);
			Ch->SetMount(Model, AnimSet, H, RFx, RSnd, RSnd2, RSnd3);
		}
	}

	// ★ Фейд-ин появления — эталонные 1500 мс (бит 1 флагов +0x1E4, alpha=f146/1500, как у мобов;
	//   entity_char.c:56885). Поздний экип (0xFB5 оружие после 4065, батчи заточки) подхватит
	//   текущую альфу через AdoptSlots фейд-компонента.
	// ★ ИСПРАВЛЕНИЕ 11.10.2026 (дедуп №2, В2-C, F1): общая точка `UAsdaFadeComponent::Begin` («найти или создать»).
	UAsdaFadeComponent::Begin(Ch, /*bIn*/true, AAsdaMobActor::FadeInSeconds);
	E.Actor = Ch;
	NetPlayers.Add(P.UserIdx, E);
	// ★★★ [ПОРТ] Персонаж мог уйти под GM-невидимость ДО того, как для нас появился его актор
	//   (4103/4025 пришли раньше спауна). У оригинала эту роль играет отложенная очередь действий
	//   (`sub_91EF69` → разбор в `GameNet_OnUserSpawn@0x9CE750`, где те же 300/301 и их спаун-пара
	//   188/189); у нас — запомненный набор. Без этого скрытый оператор выныривал бы на один
	//   AoI-цикл, пока сервер не пришлёт 4103 повторно.
	if (GmHiddenUserIdx.Contains(P.UserIdx))
	{
		Ch->SetActorHiddenInGame(true);
		UE_LOG(LogAsdaWorld, Log, TEXT("[GM-скрытие] userIdx=%d заспавнен сразу СКРЫТЫМ"), P.UserIdx);
	}
	UE_LOG(LogAsdaWorld, Log, TEXT("[net] ИГРОК userIdx=%d '%s' заспавнен: game=(%.0f,%.0f) → UE(%s), пол=%d"),
		P.UserIdx, *P.Name, P.PosGame.X, P.PosGame.Y, *SpawnLoc.ToString(), E.Sex);
}

// ── Имена сущностей из таблиц: ★ волна 3 п.14 (15.09.2026) — `EnsureNameTables`/`GetMobName`/`GetMobSoundBase`/
//   `GetNpcName` удалены, единая точка — `World/AsdaEntityText.h` (тела и канон DT_NpcNames перенесены туда). ──
// ★ Удержание таблиц имён миром (15.09.2026, ревизия п.14) — вместо прежних сильных членов MobNameTable/
//   NpcNameTable/MobSoundTable. Разбора нет: только сильные ссылки на объекты общей точки `AsdaDt::Cached`,
//   чтобы GC не выгружал таблицы между спаунами. Момент и однократность — как у прежнего `EnsureNameTables`.
int32 UAsdaWorldSubsystem::HoldEntityNameTables()
{
	if (bEntityNameTablesHeld) { return HeldEntityNameTables.Num(); }
	bEntityNameTablesHeld = true;
	for (const TCHAR* Path : { AsdaEntityText::kMobNamesPath, AsdaEntityText::kNpcNamesPath, AsdaEntityText::kMobSoundsPath })
	{
		if (UDataTable* T = AsdaDt::Cached(Path)) { HeldEntityNameTables.Add(T); }
	}
	return HeldEntityNameTables.Num();
}

// ★ Удержание таблиц спауна (15.09.2026, dt_gc_hold) — обоснование в объявлении. Пути — объявления читателей
//   (второй копии литералов нет); загрузка и объекты — общая точка `AsdaDt::Cached` внутри `FAsdaHeldTables::Hold`.
static FName AsdaSpawnTablesGroupName(EAsdaSpawnTables Group)
{
	switch (Group)
	{
	case EAsdaSpawnTables::Mob:          return FName(TEXT("Mob"));
	case EAsdaSpawnTables::Npc:          return FName(TEXT("Npc"));
	case EAsdaSpawnTables::Clan:         return FName(TEXT("Clan"));
	case EAsdaSpawnTables::Pet:          return FName(TEXT("Pet"));
	case EAsdaSpawnTables::Mount:        return FName(TEXT("Mount"));
	case EAsdaSpawnTables::MountIndex:   return FName(TEXT("MountIndex"));
	case EAsdaSpawnTables::SoulGuardian: return FName(TEXT("SoulGuardian"));
	}
	return NAME_None;
}

// Пути группы: объявления читателей таблиц (актор моба, плашка, NPC, эмблема клана, питомец, хранитель; `kRideEffectDT` — этот файл).
static TConstArrayView<const TCHAR*> AsdaSpawnTablesPaths(EAsdaSpawnTables Group)
{
	static const TCHAR* const MobPaths[]   = { AAsdaMobActor::kMobStatsPath, AAsdaMobActor::kMobTexturesPath, UAsdaNameplateComponent::kMobNameColorsPath };
	static const TCHAR* const NpcPaths[]   = { AAsdaNpcActor::kNpcQuestNpcPath };
	static const TCHAR* const ClanPaths[]  = { UAsdaClanFlag::kSignTablePath, UAsdaClanFlag::kBorderTablePath };
	static const TCHAR* const PetPaths[]   = { AAsdaPetActor::kPetModelsPath };
	static const TCHAR* const MountPaths[] = { kRideEffectDT };
	static const TCHAR* const MountIndexPaths[] = { kSpecialItemlistDT };   // ★ N12: индекс транспорта `LookupMount`
	static const TCHAR* const GuardPaths[] = { AAsdaSoulGuardianActor::kSkillFxPath };
	switch (Group)
	{
	case EAsdaSpawnTables::Mob:          return MobPaths;
	case EAsdaSpawnTables::Npc:          return NpcPaths;
	case EAsdaSpawnTables::Clan:         return ClanPaths;
	case EAsdaSpawnTables::Pet:          return PetPaths;
	case EAsdaSpawnTables::Mount:        return MountPaths;
	case EAsdaSpawnTables::MountIndex:   return MountIndexPaths;
	case EAsdaSpawnTables::SoulGuardian: return GuardPaths;
	}
	return TConstArrayView<const TCHAR*>();
}

int32 UAsdaWorldSubsystem::HoldSpawnTables(EAsdaSpawnTables Group)
{
	return HeldSpawnTables.Hold(AsdaSpawnTablesGroupName(Group), AsdaSpawnTablesPaths(Group));
}

bool UAsdaWorldSubsystem::IsSpawnTablesHeld(EAsdaSpawnTables Group) const
{
	return HeldSpawnTables.IsHeld(AsdaSpawnTablesGroupName(Group));
}

// ★ Над-головная плашка теперь = штатный компонент UAsdaNameplateComponent (порт JUI-окон 1:1,
//   создаётся в конструкторе акторов и виден в редакторе). Здесь — только заполнение данными из сети.
//   Возвращает компонент актора (или nullptr, если у актора его нет).
UAsdaNameplateComponent* UAsdaWorldSubsystem::FillNameplate(AActor* Owner, const FString& Name)
{
	if (!Owner) { return nullptr; }
	UAsdaNameplateComponent* NP = Owner->FindComponentByClass<UAsdaNameplateComponent>();
	if (!NP) { return nullptr; }
	NP->DisplayName = Name;
	NP->ApplyHeight();   // ★ капсула-хитбокс к этому моменту подогнана (динамический спавн) — высота актуальна
	NP->RefreshNameplate();
	return NP;
}

// ★ ЕДИНСТВЕННЫЙ обратный поиск «актор игрока → сессионный userNum». Симметричен `ActorByUserIdx`:
//   сначала СВОЙ павн (userNum 0 — валидное значение нашего сервера), затем реестр соседей.
//   До 2026-08-10 таких функций было ДВЕ: эта (без своего павна) и `UserIdxByActor`. Старая при
//   цели «я сам» возвращала −1 — для 6538/4201 и для заявки 5256 это «цели нет».
int32 UAsdaWorldSubsystem::FindUserIdxByActor(const AActor* A) const
{
	if (!A) { return -1; }
	if (A == LocalPlayerActor.Get())
	{
		const UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
		return Net ? (int32)Net->SelfUserNum : -1;
	}
	for (const TPair<int32, FAsdaNetPlayerEntry>& It : NetPlayers)
	{
		if (It.Value.Actor.Get() == A) { return It.Key; }
	}
	return -1;
}

// ══════════ ★★★ МНОЖИТЕЛЬ ТЕМПА АТАКИ — ЕДИНАЯ ТОЧКА РАСЧЁТА ═══════════════════════════════════
//   Полный разбор, эталонные адреса и оба исправленных дефекта оригинала — у объявления в
//   `AsdaWorldSubsystem.h`. Здесь — сам счёт, слово в слово по game.c:36385-36470.
// ★ Вклад ОДНОГО состояния. Эталон (каждый из пяти блоков устроен одинаково):
//       skillEntry  = CList_FindByTypeSub(&char->gap_96A[128], <id состояния>, 0);
//       skillRecord = CSkillMgr_GetSkillRecord(g_pSkillInfoTable, *(__int16 *)(skillEntry + 2));
//       вклад = (entry[+9] * record->s1PerLevel + record->s1Base) / 100.0;
//   `entry+2` (у нас `IdFlag`) — id НАВЫКА-ИСТОЧНИКА, `entry+9` (у нас `Byte9`) — его УРОВЕНЬ.
//   Возвращает 0, если состояния нет или записи навыка нет (эталон в обоих случаях не прибавляет).
//
// ★ ИСПРАВЛЕНИЕ ДЕФЕКТА ОРИГИНАЛА №1 (закон fix-original-defects — чиним и ПОМЕЧАЕМ).
//   Оригинал ВСЕГДА читает пару s1Base/s1PerLevel (буфф-слот 0) — даже когда искомое состояние
//   лежит в другом слоте записи. Пример на живых данных (`skill_info.sif`, запись 273 б): навык
//   1209 держит состояния [231, 98, 233], то есть 233 лежит в СЛОТЕ 2, и верная пара — S3 = 15/1
//   (вклад +0.16), а оригинал берёт S1 = 20/1 и даёт +0.21. Мы ищем слот и берём его пару.
//   Если состояния нет ни в одном слоте (данные разошлись с состоянием на проводе) — ведём себя
//   как оригинал и берём слот 0. Откат к дословному эталону: `asda.Combat.AttackTempoBuffSlotFix 0`.
// ★ 15.10.2026 (ритм атаки): та же величина ЦЕЛЫМ числом процентов — её складывает и сервер (бонус B в
//   `sub_45F3A0`, целочисленная сумма). Одна формула на оба потребителя: темп (÷100) и серверные часы атаки
//   (`UAsdaAttackRhythmSubsystem`). OutStack — стек записи (+5), 0 — записи нет.
static int32 AsdaStatePct(const UAsdaEffectsSubsystem* Fx, const UAsdaSkillDatabase* Skills,
	int32 UserIdx, int32 StateId, int32* OutStack = nullptr)
{
	if (OutStack) { *OutStack = 0; }
	if (!Fx || !Skills) { return 0; }
	const FAsdaEffectEntry* E = Fx->FindByTypeSub(UserIdx, StateId, /*Type*/ 0);
	if (!E) { return 0; }
	if (OutStack) { *OutStack = E->Stack; }
	const FAsdaSkillDef* D = Skills->Get(E->IdFlag);
	if (!D) { return 0; }
	// ★ КЛАМП УРОВНЯ: на проводе уровень — БАЙТ (до 255), а `maxLevel` записи в данных не выше 5.
	//   Без зажима битый/чужой байт умножался бы на «за уровень» и разгонял темп в разы.
	const int32 Level = FMath::Clamp(E->Byte9, 0, FMath::Max(0, D->MaxLevel));
	int32 Base = 0, PerLevel = 0;
	const int32 Slot = (CVarAttackTempoBuffSlotFix.GetValueOnGameThread() != 0)
		? D->BuffSlotOfState(StateId)   // ★ исправленный путь: пара из СВОЕГО слота
		: 0;                            // дословный эталон: всегда S1
	D->BuffSlotScale(Slot, Base, PerLevel);
	return Level * PerLevel + Base;
}

static float AsdaStateTempoTerm(const UAsdaEffectsSubsystem* Fx, const UAsdaSkillDatabase* Skills,
	int32 UserIdx, int32 StateId)
{
	return (float)AsdaStatePct(Fx, Skills, UserIdx, StateId) / 100.f;   // ★ 15.10.2026: одна формула с часами атаки (AsdaStatePct)
}

// ★ Вклад ОДНОГО надетого предмета (эталон game.c:36452-36468, дословно то же в трёх других путях):
//       if (rec->itemSubType != 10 || rec->itemVariant != 1
//           || rec->itemSubType == 10 && rec->itemVariant == 2 || (double)rec->linkedGroupCode <= 0.0)
//           dmgMultiplier += (double)rec->linkedGroupCode / 100.0;
//   То есть поле `linkedGroupCode` у экипировки означает прибавку к темпу, и ЕДИНСТВЕННОЕ
//   исключение — УДОЧКИ (subType 10, variant 1) с положительным значением: у них то же поле
//   означает силу рыбалки. БУРЫ (subType 10, variant 2) НЕ исключаются — вторая половина условия
//   оригинала возвращает их обратно, и это проверено; воспроизводим как есть.
static float AsdaItemTempoTerm(const FAsdaItemRecord* R)
{
	if (!R) { return 0.f; }
	const bool bFishingRod = (R->ItemSubType == 10 && R->ItemVariant == 1 && R->LinkedGroupCode > 0);
	if (bFishingRod) { return 0.f; }
	return (float)R->LinkedGroupCode / 100.f;
}

float UAsdaWorldSubsystem::GetAttackTempoMul(const AAsdaPlayerCharacter* Ch) const
{
	if (!Ch || CVarAttackTempo.GetValueOnGameThread() == 0) { return 1.f; }
	UGameInstance* GI = GetGameInstance();
	if (!GI) { return 1.f; }
	const int32 UserIdx = FindUserIdxByActor(Ch);
	if (UserIdx < 0) { return 1.f; }   // персонажа нет ни в реестре, ни свой павн — считать нечем
	// ★ 16.09.2026 (ревизия, МЕЛК): для СВОЕГО павна номер берётся из `SelfUserNum`, а тот до прихода
	//   4003 равен нулю и НЕ является нашим. Без этой проверки темп считался бы по списку эффектов
	//   чужого игрока с номером 0 (закон `charid-vs-usernum`: userNum — не идентификатор персонажа).
	//   `IsSelfUserIdx` знает признак «номер уже известен», поэтому спрашиваем его.
	if (Ch == LocalPlayerActor.Get())
	{
		const UAsdaNetSubsystem* SelfNet = GI->GetSubsystem<UAsdaNetSubsystem>();
		if (!SelfNet || !SelfNet->IsSelfUserIdx(UserIdx)) { return 1.f; }
	}
	const UAsdaEffectsSubsystem* Fx     = GI->GetSubsystem<UAsdaEffectsSubsystem>();
	const UAsdaSkillDatabase*    Skills = GI->GetSubsystem<UAsdaSkillDatabase>();
	// Подтип НАДЕТОГО оружия — эталонное `v118->equippedWeaponType` (гейты состояний 174 и 233).
	const int32 Weapon = Ch->EquippedWeaponSubType;

	// ── Вклады состояний (порядок и знаки — как в эталоне) ──
	float M = 0.f;
	M += AsdaStateTempoTerm(Fx, Skills, UserIdx, 27);                          // ПЛЮС, без условий
	if (Weapon == 11) { M += AsdaStateTempoTerm(Fx, Skills, UserIdx, 174); }   // ПЛЮС, только баллиста
	M -= AsdaStateTempoTerm(Fx, Skills, UserIdx, 28);                          // МИНУС, без условий
	if (Weapon == 3)  { M += AsdaStateTempoTerm(Fx, Skills, UserIdx, 233); }   // ПЛЮС, только двуруч. меч
	M -= AsdaStateTempoTerm(Fx, Skills, UserIdx, 247);                         // МИНУС, без условий
	// ★★★ ТРЕТИЙ ИСПРАВЛЕННЫЙ ДЕФЕКТ ОРИГИНАЛА (ключ `asda.Combat.AttackTempoState12`): состояние 12
	//   сервер вешает реально (навыки 632/820/823, до −0.60 у 820 на пятом уровне), а клиент оригинала
	//   не читает его нигде — эффект наложен, показан в списке баффов и НИ НА ЧТО не влияет.
	//   Знак — МИНУС: корейское имя в непереведённой серверной таблице прямо говорит «снижение»
	//   (감소), как и у 28 с 247. Восстанавливаем потерянный эффект.
	if (CVarAttackTempoState12.GetValueOnGameThread() != 0)
	{
		M -= AsdaStateTempoTerm(Fx, Skills, UserIdx, 12);
	}

	// ── Вклад НАДЕТЫХ ПРЕДМЕТОВ ──
	//   Тот же самый цикл, что и у второй шкалы темпа (`GetAnimSpeedScale`), поэтому он вынесен в
	//   единый помощник `SumWornItemsTempoTerm` — см. его тело ниже.
	//   ★ 15.10.2026 [ОРИГИНАЛ]: по умолчанию НЕ входит — сервер этого слагаемого не знает (`asda.Combat.AttackTempoItems`).
	//   ★ Ревизия НИЗК-4: `asda.Combat.ServerRhythm 0` — полный откат одним ключом, предметы возвращаются вместе с ним.
	if (CVarAttackTempoItems.GetValueOnGameThread() != 0 || !UAsdaAttackRhythmSubsystem::IsEnabled()) { M += SumWornItemsTempoTerm(UserIdx); }

	// ★ Эталон: `if (dmgMultiplier != 0.0) animSpeed = (dmgMultiplier + 1.0) * animSpeed;` —
	//   при нулевой сумме множитель РОВНО 1 (никакого умножения не происходит вовсе).
	if (M == 0.f) { return 1.f; }
	// ★ КЛАМП ИТОГА: ниже 0.1 анимация встала бы или пошла вспять (а вместе с ней — момент урона,
	//   звук замаха, шаг комбо и отправка пакета атаки, потому что все четыре едут на этих часах).
	return FMath::Max(0.1f, M + 1.f);
}

// ★ 15.10.2026: величина состояния целыми процентами для серверных часов атаки (разбор — в заголовке).
int32 UAsdaWorldSubsystem::GetStateBonusPct(int32 UserIdx, int32 StateId, int32* OutStack) const
{
	if (OutStack) { *OutStack = 0; }
	UGameInstance* GI = GetGameInstance();
	if (!GI || UserIdx < 0) { return 0; }
	return AsdaStatePct(GI->GetSubsystem<UAsdaEffectsSubsystem>(), GI->GetSubsystem<UAsdaSkillDatabase>(), UserIdx, StateId, OutStack);
}

// ★★★ ЕДИНЫЙ ПЕРЕБОР НАДЕТЫХ ПРЕДМЕТОВ (объявление и обоснование — в заголовке).
//   Эталон: `for (i = 0; i < 10; ++i) if (i != 5 && i != 6 && i != 7)` — слоты 0,1,2,3,4,8,9
//   (8 = щит, 9 = оружие). Свой набор берём из контейнера 3 инвентаря, чужой — из реестра соседей
//   (там хранятся ровно эти семь слотов: пять видимых частей, щит и оружие).
float UAsdaWorldSubsystem::SumWornItemsTempoTerm(int32 UserIdx) const
{
	UGameInstance* GI = GetGameInstance();
	const UAsdaItemTableSubsystem* Items = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	if (!Items) { return 0.f; }
	float Sum = 0.f;
	static const int32 kTempoEquipSlots[] = { 0, 1, 2, 3, 4, 8, 9 };
	const UAsdaNetSubsystem* Net = GI->GetSubsystem<UAsdaNetSubsystem>();
	if (Net && Net->IsSelfUserIdx(UserIdx))
	{
		if (const UAsdaInventorySubsystem* Inv = GI->GetSubsystem<UAsdaInventorySubsystem>())
		{
			for (int32 Slot : kTempoEquipSlots)
			{
				const FAsdaItemInstance* Cell = Inv->GetCell(3, Slot);   // контейнер 3 = НАДЕТОЕ
				if (!Cell || Cell->ItemCode <= 0) { continue; }
				Sum += AsdaItemTempoTerm(Items->GetItem(Cell->ItemCode));
			}
		}
	}
	else if (const FAsdaNetPlayerEntry* PE = NetPlayers.Find(UserIdx))
	{
		const int32 WornIds[] = { PE->Parts.Helmet, PE->Parts.Chest, PE->Parts.Pants,
		                          PE->Parts.Shoes,  PE->Parts.Gloves,
		                          PE->ShieldItemId, PE->Parts.Weapon };   // те же слоты 0,1,2,3,4,8,9
		static_assert(UE_ARRAY_COUNT(kTempoEquipSlots) == UE_ARRAY_COUNT(WornIds),
			"набор слотов темпа и снимок экипа соседа обязаны совпадать по длине");
		for (int32 ItemId : WornIds)
		{
			if (ItemId <= 0) { continue; }
			Sum += AsdaItemTempoTerm(Items->GetItem(ItemId));
		}
	}
	return Sum;
}

// ★★★ МНОЖИТЕЛЬ ТЕМПА АНИМАЦИЙ НАВЫКОВ И ЭФФЕКТОВ (эталонное `pChar->animSpeedScale`).
//   Полная карта потока — у объявления в заголовке. Две ступени: ПРЕДМЕТНЫЙ цикл плюс 1.0 — и
//   состояние 78, которое ПЕРЕБИВАЕТ результат целиком (ШАГ 1 ниже).
//   ★ В предметных писателях эталона (`game.c:64371-64412` и `:65121-65138`) состояний
//   действительно нет — `CList_FindByTypeSub` там не вызывается ни разу, проверено по телам, а не
//   по аналогии с темпом атаки. Но третий писатель поля — `Skill_ApplyAttackSpeedBuff@0x99AFC0` —
//   пишет его из состояния 78, и прежняя редакция этой шапки, утверждавшая «состояний в формуле
//   нет», противоречила коду прямо под собой: она писалась до того, как он был найден.
float UAsdaWorldSubsystem::GetAnimSpeedScale(const AAsdaPlayerCharacter* Ch) const
{
	if (!Ch || CVarAnimSpeedScale.GetValueOnGameThread() == 0) { return 1.f; }
	UGameInstance* GI = GetGameInstance();
	if (!GI) { return 1.f; }
	const int32 UserIdx = FindUserIdxByActor(Ch);
	if (UserIdx < 0) { return 1.f; }   // персонажа нет ни в реестре, ни свой павн — считать нечем
	// ★ Та же проверка, что и у темпа атаки (закон `charid-vs-usernum`): у СВОЕГО павна номер живёт
	//   в `SelfUserNum`, а тот до прихода 4003 равен нулю и НАШИМ не является. Без неё экипировка
	//   бралась бы из инвентаря по чужому номеру 0.
	if (Ch == LocalPlayerActor.Get())
	{
		const UAsdaNetSubsystem* SelfNet = GI->GetSubsystem<UAsdaNetSubsystem>();
		if (!SelfNet || !SelfNet->IsSelfUserIdx(UserIdx)) { return 1.f; }
	}
	// ── ШАГ 1: СОСТОЯНИЕ 78 «Повышение скорости навыков» ПЕРЕБИВАЕТ предметы целиком ──
	//   Эталон `Skill_ApplyAttackSpeedBuff@0x99AFC0` (разбор и доказательство — у ключа
	//   `asda.Combat.AnimSpeedScaleState78` выше) ПРИСВАИВАЕТ полю величину состояния, а не
	//   прибавляет её. Вклад считает тот же помощник, что и у темпа атаки, — второй копии формулы
	//   «(уровень · за_уровень + база) / 100» в порту нет.
	if (CVarAnimSpeedScaleState78.GetValueOnGameThread() != 0)
	{
		const UAsdaEffectsSubsystem* Fx     = GI->GetSubsystem<UAsdaEffectsSubsystem>();
		const UAsdaSkillDatabase*    Skills = GI->GetSubsystem<UAsdaSkillDatabase>();
		// ★ 16.09.2026 (ревизия, МЕЛК): эталон входит в присваивание только когда нашлись ОБА —
		//   и узел состояния, и запись навыка-источника (`if (rec)` в `Skill_ApplyAttackSpeedBuff`).
		//   Если записи нет, поле НЕ ТРОГАЕТСЯ и остаётся предметным. Прежде мы входили по одному
		//   наличию узла, а помощник при ненайденной записи отдаёт 0 ⇒ множитель схлопывался в 1.0
		//   и вклад надетых вещей терялся. Теперь при отсутствии записи падаем на предметную формулу.
		const FAsdaEffectEntry* E78 = (Fx && Skills) ? Fx->FindByTypeSub(UserIdx, 78, /*Type*/ 0) : nullptr;
		if (E78 && Skills->Get(E78->IdFlag) != nullptr)
		{
			// ★ ИСПРАВЛЕНИЕ ДЕФЕКТА ОРИГИНАЛА (помечаем, закон fix-original-defects): у эталона это
			//   присваивание ЗАЛИПАЕТ. Поле перезаписывается только тиком состояния 24 при живом 78 и
			//   пересборкой внешности, поэтому после спада баффа персонаж ходит с темпом баффа до
			//   ближайшей смены экипировки — а надетые вещи так и остаются не учтёнными. Мы считаем
			//   значение заново на каждый запрос, поэтому спад баффа сразу возвращает предметную
			//   формулу. Откат к «как оригинал, но без залипания» — `asda.Combat.AnimSpeedScaleState78 0`
			//   (тогда 78 не учитывается вовсе; дословного залипания порт не воспроизводит намеренно).
			// ★ ВНИМАНИЕ (ревизия, МЕЛК): помощник общий с темпом АТАКИ, поэтому несёт два наших
			//   отступления, которых у `Skill_ApplyAttackSpeedBuff` нет, — исправление буфф-слота
			//   (под ключом `asda.Combat.AttackTempoBuffSlotFix`, то есть ключ ОДНОЙ шкалы влияет и на
			//   ДРУГУЮ) и кламп уровня по `MaxLevel`. Практически это ничего не меняет: все три
			//   носителя состояния 78 (навыки 630/839/1060) держат его в слоте 0 при `MaxLevel` 5 —
			//   проверено по живым данным. Но знать об этой связке обязательно.
			return FMath::Max(0.1f, AsdaStateTempoTerm(Fx, Skills, UserIdx, 78) + 1.f);
		}
	}

	// ── ШАГ 2: базовая формула — надетые предметы ──
	const float Sum = SumWornItemsTempoTerm(UserIdx);
	// ★ У эталона прибавка безусловная (`animSpeedScale = statWeightSum + 1.0`) — в отличие от темпа
	//   атаки, где нулевая сумма пропускает умножение вовсе. Разницы в значении нет (1.0 в обоих
	//   случаях), но воспроизводим как написано.
	// ★ КЛАМП: ниже 0.1 часы клипа навыка встали бы или пошли вспять, а на них висят пороги фаз,
	//   спавн эффектов, звуки и завершение показа. Эталон отрицательных `LinkedGroupCode` в данных
	//   не имеет, но ставить сюда деление без защиты нельзя.
	return FMath::Max(0.1f, Sum + 1.f);
}

// 4036 соседа: уровень в реестр. Эталон `Recv_LevelUp@0xF424C0` делает то же самое — пишет
//   `pChar->level` найденного по userIdx персонажа; панель цели читает это поле напрямую.
void UAsdaWorldSubsystem::OnRemoteLevel(int32 UserIdx, int32 Level)
{
	FAsdaNetPlayerEntry* E = NetPlayers.Find(UserIdx);
	if (!E) { return; }   // сосед вне AoI — применять некуда (эталон тоже не находит CCharacter)
	if (E->Level == Level) { return; }
	UE_LOG(LogAsdaWorld, Log, TEXT("[реестр] уровень соседа userIdx=%d '%s': %d → %d (4036)"),
		UserIdx, *E->Name, E->Level, Level);
	E->Level = Level;
}

// ★ Снимок реестра соседа для UI (панель цели). Своего павна здесь НЕТ — у себя эти же поля
//   лежат в `UAsdaNetSubsystem` (`SelfLevel`, `SelfClassId`), и вызывающий берёт их оттуда.
bool UAsdaWorldSubsystem::GetNetPlayerInfo(int32 UserIdx, FString& OutName, int32& OutClassId,
	int32& OutLevel, int32& OutClanId) const
{
	const FAsdaNetPlayerEntry* E = NetPlayers.Find(UserIdx);
	if (!E) { return false; }
	OutName    = E->Name;
	OutClassId = E->ClassId;
	OutLevel   = E->Level;
	OutClanId  = E->ClanId;
	return true;
}

// Подкласс (char+1081) и созвездие (char+1120) соседа — для окна `OTHERCHARA_DATA` 4200.
bool UAsdaWorldSubsystem::GetNetPlayerClassExtra(int32 UserIdx, int32& OutSubClassId,
	int32& OutConstellation) const
{
	const FAsdaNetPlayerEntry* E = NetPlayers.Find(UserIdx);
	if (!E) { return false; }
	OutSubClassId    = E->SubClassId;
	OutConstellation = E->Constellation;
	return true;
}

// Пол соседа (char+1079) — для выбора мужского/женского варианта иконки предмета.
bool UAsdaWorldSubsystem::GetNetPlayerSex(int32 UserIdx, int32& OutSex) const
{
	const FAsdaNetPlayerEntry* E = NetPlayers.Find(UserIdx);
	if (!E) { return false; }
	OutSex = (int32)E->Sex;
	return true;
}

// ★ charId (i32) соседа по сессионному userNum (i16). Нужен пакетам меню по игроку 5478 и 4302 —
//   они несут ОБА идентификатора сразу, и подстановка userNum вместо charId привела бы к молчанию
//   сервера (закон charid-vs-usernum). 0 = соседа с таким userNum в реестре нет.
int32 UAsdaWorldSubsystem::GetNetPlayerCharId(int32 UserIdx) const
{
	const FAsdaNetPlayerEntry* E = NetPlayers.Find(UserIdx);
	return E ? E->CharId : 0;
}

int32 UAsdaWorldSubsystem::GetUserNumByCharId(int32 CharId) const
{
	if (CharId == 0) { return -1; }
	for (const TPair<int32, FAsdaNetPlayerEntry>& It : NetPlayers)
	{
		if (It.Value.CharId == CharId) { return It.Key; }
	}
	return -1;
}

// ★ Дедуп №2, В2-A (N4): ключи отката единого поиска актора игрока (разбор — у `ActorByCharId` в заголовке).
static ASDA_DEV_CVAR(int32, CVarCharIdLookupSelf,
	TEXT("asda.World.CharIdLookupSelf"), 1,
	TEXT("1 — `GetPlayerActorByCharId` находит и СВОЙ павн (как эталонный CharManager_FindByCharId; дедуп №2, 11.10.2026); ")
	TEXT("0 — прежнее: только реестр соседей NetPlayers (себя не находит)."),
	ECVF_Default);
static ASDA_DEV_CVAR(int32, CVarUserIdxLookupSelfFirst,
	TEXT("asda.World.UserIdxLookupSelfFirst"), 1,
	TEXT("1 — `FindPlayerActor`/`GetPlayerActorByUserNum` смотрят СВОЙ павн первым (с IsValid), как `ActorByUserIdx` и эталонный ")
	TEXT("CharManager_FindByUserIdx (дедуп №2, 11.10.2026); 0 — прежнее: реестр NetPlayers первым (устаревшая запись с моим номером ")
	TEXT("отдавала чужого), свой — без IsValid."),
	ECVF_Default);

AActor* UAsdaWorldSubsystem::GetPlayerActorByCharId(int32 CharId) const
{
	// ★ ИСПРАВЛЕНИЕ 11.10.2026 (дедуп №2, В2-A, N4): общая точка `ActorByCharId` — свой павн находится (было: только соседи).
	if (CVarCharIdLookupSelf.GetValueOnGameThread() != 0) { return ActorByCharId(CharId); }
	if (CharId == 0) { return nullptr; }
	for (const TPair<int32, FAsdaNetPlayerEntry>& It : NetPlayers)
	{
		if (It.Value.CharId == CharId) { return GetPlayerActorByUserNum(It.Key); }
	}
	return nullptr;
}

AActor* UAsdaWorldSubsystem::GetPlayerActorByUserNum(int32 UserIdx) const
{
	return FindPlayerActor(UserIdx);
}

// ★ Фейковый сосед для живых проверок без сервера. Заполняем ровно те поля, что несёт снапшот
//   4005, и отдаём их штатному `SpawnRemotePlayer` — своей ветки спавна не заводим.
void UAsdaWorldSubsystem::DebugSpawnFakePeer(int32 UserNum, const FString& Name, const FVector2D& PosGame)
{
	DebugSpawnFakePeerState(UserNum, Name, PosGame, 0);
}

// ★ 10.10.2026 (копание): фейковый сосед с кодом состояния появления — разбор у объявления.
void UAsdaWorldSubsystem::DebugSpawnFakePeerState(int32 UserNum, const FString& Name, const FVector2D& PosGame, int32 ActionState)
{
	FAsdaRemotePlayer P;
	P.UserIdx = UserNum;
	P.CharId  = 900000 + UserNum;      // заведомо не пересекается с настоящими charId
	P.Name    = Name.IsEmpty() ? FString::Printf(TEXT("Сосед%d"), UserNum) : Name;
	P.ClassId = 1;
	P.Level    = 10;
	P.Gender   = 1;
	P.PosGame  = PosGame;
	P.ActionState = ActionState;   // ★ 10.10.2026: код состояния появления (самотест копания — 110/114)
	SpawnRemotePlayer(P);
	UE_LOG(LogTemp, Log, TEXT("[мир] фейковый сосед userNum=%d «%s» поставлен в game(%.0f,%.0f)"),
		UserNum, *P.Name, PosGame.X, PosGame.Y);
}

// Актор игрока по userIdx (чужой из реестра или свой павн — на своём данные тоже храним, плашка скрыта).
AActor* UAsdaWorldSubsystem::FindPlayerActor(int32 UserIdx) const
{
	// ★ ИСПРАВЛЕНИЕ 11.10.2026 (дедуп №2, В2-A, N4): общая точка `ActorByUserIdx` — свой павн первым (с IsValid). Было: реестр
	//   соседей первым — устаревшая запись `NetPlayers` с моим номером отдавала ЧУЖОГО актора. Откат — ключ ниже.
	if (CVarUserIdxLookupSelfFirst.GetValueOnGameThread() != 0) { return ActorByUserIdx(UserIdx); }
	if (const FAsdaNetPlayerEntry* E = NetPlayers.Find(UserIdx)) { return E->Actor.Get(); }
	if (UAsdaNetSubsystem* Net = GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>())
	{
		if (Net->IsSelfUserIdx(UserIdx)) { return LocalPlayerActor; }
	}
	return nullptr;
}

// ★ ЖИВОЙ ДАМП МОБОВ (`asda.Mobs.Dump`) — доказательная картина «кто где стоит».
//   Печатает по КАЖДОМУ сетевому мобу: renderId, имя, жив/труп, мировые координаты, ИГРОВОЙ ТАЙЛ
//   (обратный пересчёт: gx=ue.Y, gz=-ue.X, тайл = floor(g/100)), идёт ли куда-то и куда, HP.
//   В конце — гистограмма «сколько мобов в одной клетке»: если фикс работает, у каждой клетки 1.
//   Снимать через 10-20 с после серии респавнов.
static void AsdaMobsDumpCmd(const TArray<FString>& /*Args*/, UWorld* World)
{
	if (!World) { return; }
	TMap<FIntPoint, int32> PerCell;
	int32 N = 0, NDead = 0, NMoving = 0, NFading = 0;
	// ★ КРУГ 7 — сводка по рассинхрону с сервером (главный замер этого круга).
	double DesyncSumAll = 0.0; int32 DesyncMobs = 0, DesyncBad = 0; float DesyncMaxAll = 0.f;
	const double NowS = World->GetTimeSeconds();
	UE_LOG(LogAsdaWorld, Warning, TEXT("[дамп мобов] ───────────────────────────────────────────────"));
	for (TActorIterator<AAsdaMobActor> It(World); It; ++It)
	{
		AAsdaMobActor* M = *It;
		if (!M || M->NetRenderId < 0) { continue; }   // запечённые шаблоны уровня не считаем
		const FVector L = M->GetActorLocation();
		const FIntPoint Cell = AsdaCoord::GameToTile(AsdaCoord::UeToGame2D(L));   // ★ Аудит №3 (B1/B2)
		PerCell.FindOrAdd(Cell)++;
		++N;
		if (M->IsDead())   { ++NDead; }
		if (M->IsMoving()) { ++NMoving; }
		if (M->IsFadingOut()) { ++NFading; }
		// ★ Круг 6: статус фейда и время с момента смерти — доказательная проверка «труп гаснет и уходит»
		//   (эталон: death-клип → флаг 4 + 1500мс-таймер → флаг 0x10 → charRemoveEntity).
		FString Fade;
		switch (M->GetFadeKind())
		{
			case EAsdaMobFade::Corpse: Fade = TEXT("фейд:ТРУП"); break;
			case EAsdaMobFade::Aoi:    Fade = TEXT("фейд:AoI "); break;
			default:                   Fade = TEXT("фейд:нет "); break;
		}
		FString Since;
		if (M->IsDead() && M->GetDeathAtS() > 0.0)
		{
			const double Dt = NowS - M->GetDeathAtS();
			const double ToFade = M->GetCorpseFadeAtS() - NowS;
			Since = (M->GetFadeKind() == EAsdaMobFade::Corpse)
				? FString::Printf(TEXT(" мёртв %.2fс, гаснет"), Dt)
				: FString::Printf(TEXT(" мёртв %.2fс, фейд через %.2fс"), Dt, ToFade);
		}
		// ★ КРУГ 7: рассинхрон с сервером — главная метрика этого круга. Считается на каждом 4015
		//   kind=1/2/3 ДО применения пакета (|позиция актора − клетка cur из пакета|).
		FString Sync = TEXT(" синх:замеров нет");
		if (M->DesyncN > 0)
		{
			const float Avg = (float)(M->DesyncSumU / M->DesyncN);
			Sync = FString::Printf(TEXT(" СИНХ: посл=%.0f средн=%.0f макс=%.0f юн (%.2f/%.2f/%.2f тайла) N=%d"),
				M->DesyncLastU, Avg, M->DesyncMaxU, M->DesyncLastU / 100.f, Avg / 100.f, M->DesyncMaxU / 100.f, M->DesyncN);
			DesyncSumAll += (double)Avg; ++DesyncMobs;
			DesyncMaxAll = FMath::Max(DesyncMaxAll, M->DesyncMaxU);
			if (Avg > 100.f) { ++DesyncBad; }
		}
		// ★ Д7/Д8: рядом с полосой HP печатаем табличный максимум типа и три поля типа
		//   (ранг +0xF8, «бьёт первым» +0x100, «жизни»/сегменты +0x10C) — по этой строке видно,
		//   что MaxHp больше НЕ равен текущему HP при первом появлении раненого моба.
		UE_LOG(LogAsdaWorld, Warning, TEXT("[дамп мобов] rid=%-5d %-28s %s %s мир=(%8.0f,%8.0f,%6.0f) тайл=(%d,%d) %s hp=%d/%d (тип:max=%d ранг=%d первый=%d жизней=%d)%s%s"),
			M->NetRenderId, *M->MobName, M->IsDead() ? TEXT("ТРУП") : TEXT("жив "), *Fade,
			L.X, L.Y, L.Z, Cell.X, Cell.Y,
			M->IsMoving() ? *FString::Printf(TEXT("идёт→(%.0f,%.0f)"), M->GetMoveDstGame().X, M->GetMoveDstGame().Y) : TEXT("стоит"),
			M->CurHp, M->MaxHp, M->MobTypeMaxHp, M->MobRank, M->MobAttackFirst, M->MobLives, *Since, *Sync);
	}
	int32 MaxInCell = 0; FIntPoint WorstCell(0, 0); int32 CellsWithMany = 0;
	for (const TPair<FIntPoint, int32>& P : PerCell)
	{
		if (P.Value > MaxInCell) { MaxInCell = P.Value; WorstCell = P.Key; }
		if (P.Value > 1) { ++CellsWithMany; }
	}
	UE_LOG(LogAsdaWorld, Warning, TEXT("[дамп мобов] ИТОГ: всего=%d (трупов=%d, гаснет=%d, в движении=%d), занятых клеток=%d, клеток с >1 мобом=%d, максимум в одной клетке=%d в (%d,%d)"),
		N, NDead, NFading, NMoving, PerCell.Num(), CellsWithMany, MaxInCell, WorstCell.X, WorstCell.Y);
	// ★ 11.10.2026 (nightvalley без мобов, решение владельца): запечённые в уровень моб-акторы (шаблоны, NetRenderId < 0) —
	//   отдельной строкой; офлайн-проверка «на карте мобов 0» (сетевых без сервера нет).
	int32 NBaked = 0;
	for (TActorIterator<AAsdaMobActor> It(World); It; ++It) { if (*It && It->NetRenderId < 0) { ++NBaked; } }
	UE_LOG(LogAsdaWorld, Warning, TEXT("[дамп мобов] ЗАПЕЧЁННЫХ в уровне %s: %d"), *World->GetMapName(), NBaked);
	UE_LOG(LogAsdaWorld, Warning, TEXT("[дамп мобов] СИНХРОНИЗАЦИЯ С СЕРВЕРОМ: мобов с замерами=%d, средний рассинхрон=%.0f юн (%.2f тайла), максимум=%.0f юн (%.2f тайла), мобов со средним >1 тайла=%d"),
		DesyncMobs, DesyncMobs ? (float)(DesyncSumAll / DesyncMobs) : 0.f,
		DesyncMobs ? (float)(DesyncSumAll / DesyncMobs) / 100.f : 0.f,
		DesyncMaxAll, DesyncMaxAll / 100.f, DesyncBad);
	// ★★★ КРУГ 8: БАЛАНС «СЕРВЕР ПРИСЛАЛ → МИР ПОЛУЧИЛ». Без этой строки потеря 2/3 записей была не видна.
	if (UGameInstance* GI = World->GetGameInstance())
	{
		if (UAsdaWorldSubsystem* WS = GI->GetSubsystem<UAsdaWorldSubsystem>())
		{
			const UAsdaWorldSubsystem::FAsdaMobStats& St = WS->MobStats;
			const int32 Accounted = St.Created + St.Updated + St.RecGateOff + St.RecBadRid;
			UE_LOG(LogAsdaWorld, Warning, TEXT("[дамп мобов] БАЛАНС ЗАПИСЕЙ: пакетов 4013=%d, 4019=%d | записей прислано=%d ⇒ создано=%d, обновлено=%d, отброшено(гейт +27 == 0)=%d, отброшено(rid<0)=%d, НЕУЧТЁННЫХ=%d"),
				St.Pkt4013, St.Pkt4019, St.RecTotal, St.Created, St.Updated, St.RecGateOff, St.RecBadRid, St.RecTotal - Accounted);
			UE_LOG(LogAsdaWorld, Warning, TEXT("[дамп мобов] НЕ СОЗДАНЫ: нет динамической модели=%d (из них не нашлось и шаблона=%d), SpawnActor=nullptr=%d | ушло по AoI=%d, смертей=%d, регенов=%d"),
				St.FailNoModel, St.FailNoTemplate, St.FailSpawn, St.AoiDespawn, St.Deaths, St.Revives);
			// ★ КРУГ 10б: состояние авто-AoI (режим/действующий сектор/наблюдаемый максимум окна 60 с).
			{
				const int32 CVarV = CVarAsdaAoiSectorTiles.GetValueOnGameThread();
				const FString Mode = (CVarV == 0) ? TEXT("АВТО") : (CVarV > 0 ? TEXT("ручной") : TEXT("выкл"));
				const int32 Sec = (CVarV > 0) ? CVarV : ((CVarV == 0) ? WS->AoiAuto.EffTiles : 0);
				const FString Obs = (WS->AoiAuto.LastObsS > 0.0)
					? FString::Printf(TEXT("макс окна 60с=%d (диагностика; порог сектора монотонный за сессию карты), обновлён %.0f с назад"), WS->AoiWindowMax(), NowS - WS->AoiAuto.LastObsS)
					: TEXT("наблюдений ещё нет");
				UE_LOG(LogAsdaWorld, Warning, TEXT("[дамп мобов] AoI: режим=%s, сектор=%d тайлов (%s)"), *Mode, Sec, *Obs);
			}
		}
	}
}

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs CmdAsdaMobsDump(
	TEXT("asda.Mobs.Dump"),
	TEXT("Дамп всех сетевых мобов: rid/имя/жив/мир/тайл/куда идёт + гистограмма «мобов в одной клетке»."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&AsdaMobsDumpCmd));
#endif // ASDA_DEV_CONSOLE
