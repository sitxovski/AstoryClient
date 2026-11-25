// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ ИМЕНА / МОДЕЛИ / ЗВУК-БАЗЫ СУЩНОСТЕЙ ИЗ ТАБЛИЦ — ОДНА ТОЧКА НА ПОРТ (волна 3 плана дублей, п.14; K6/A11).
//   Отчёт: `Reports/dedup_audit/wave3_entitytext.md`.
//
// ЧТО БЫЛО. Имя моба по `DT_MobNames` разбиралось ДВАЖДЫ — `UAsdaWorldSubsystem::GetMobName` (своя загрузка
//   трёх таблиц `EnsureNameTables` + сильные члены) и `UAsdaNameplateComponent::LookupMobName` (`AsdaDt::Cached`);
//   тела совпадали построчно, кроме условия после поиска «ближайшей строки той же модели» (мир возвращал имя
//   найденной строки даже ПУСТОЕ, плашка при пустом падала к строке MobId−1). Так же дважды — имя NPC, и
//   дважды — «имя NPC из таблицы, иначе запечённое NpcName» (актор NPC и сетевой спаун мира), дважды —
//   «имя цели квеста по виду 1 = моб / иначе NPC» (журнал и трекер).
//
// ★★★ ПОЧЕМУ СВОБОДНЫЕ ФУНКЦИИ, А НЕ ПОДСИСТЕМА. Их зовёт `OnConstruction` ЗАПЕЧЁННЫХ акторов — редактор вне
//   PIE, где `UGameInstance` нет (см. шапку `AsdaDataTableCache.h`). Всё здесь — только `AsdaDt::Cached`.
//
// ★★★ ВИДЫ ИДЕНТИФИКАТОРОВ (не смешивать):
//   · MobTypeId — серверный type моба (1-based, из 4013/4019/4017); строка таблиц мобов = MobTypeId − 1.
//   · NpcId     — `npc_list.BIN` поле @0 (1-based); строка `DT_NpcNames` = NpcId (без сдвига).
//   · Цель-моб гида задания (`QDT.BIN +8`) — ИНДЕКС строки таблиц мобов (0-based) = MobTypeId − 1 (25.10.2026).
//   · ИГРОК: имён игроков в таблицах НЕТ, здесь их нет и не будет. Единственный склад — реестр соседей мира:
//       по сессионному userNum (i16) — `UAsdaWorldSubsystem::GetNetPlayerInfo`, charId соседа —
//       `GetNetPlayerCharId`, обратный путь charId (i32) → userNum — `GetUserNumByCharId`;
//       своё имя — `UAsdaWorldSubsystem::GetSelfCharName` (закон `charid-vs-usernum`).
//   · ПРЕДМЕТ: единая точка уже есть — `UAsdaItemTableSubsystem::GetItemName` (item_list → sowell_list →
//       special_itemlist, как эталон `itemResolveDataByCode`); вторую дверь сюда не заводим.
//   · ПИТОМЕЦ: имя приходит по проводу (6120, `AAsdaPetActor::InitPet`), таблицы имён нет.
// Цвета имён (моб по разнице уровней, NPC, одержимый, питомец) — у владельцев отображения
//   (`UAsdaNameplateComponent`, `UAsdaTargetFrameSubsystem`), см. отчёт: две лесенки мобов РАЗНЫЕ по эталону.
#pragma once
#include "CoreMinimal.h"

namespace AsdaEntityText
{
	// Читаемое (RU) имя моба. `ModelName` — гард порта: если модель строки MobTypeId−1 не совпала (без учёта
	//   регистра), берётся ближайшая по номеру строка с той же моделью; её имя пустое → строка MobTypeId−1.
	//   Пустой `ModelName` — гард выключен. Пусто → не найдено (или MobTypeId ≤ 0, или нет таблицы).
	//   ★ [ПОРТ] 09.10.2026 (мультиязычность): имя — на языке сессии (оверлей `MobNames`, ключ `<строка>.Name`, где
	//   строка — та, из которой взято имя: MobTypeId−1 или найденная гардом); в мире редактора вне PIE — русское.
	ASDAREBORN_API FString MobName(int32 MobTypeId, const FString& ModelName);

	// Имя модели моба (колонка Model строки MobTypeId−1 `DT_MobNames` = `Mob list.BIN` +0x24). Пусто → нет.
	ASDAREBORN_API FString MobModel(int32 MobTypeId);

	// Звук-база моба (`DT_MobSounds`, строка MobTypeId−1, колонка Name = `Mob list.BIN` +0x44). Пусто → нет.
	ASDAREBORN_API FString MobSoundBase(int32 MobTypeId);

	// Читаемое (RU) имя NPC из `DT_NpcNames` (ключ = NpcId). Пусто → нет строки / NpcId ≤ 0.
	//   ★ [ПОРТ] 09.10.2026 (мультиязычность): на языке сессии (оверлей `NpcNames`, ключ `<NpcId>.Name`); в мире
	//   редактора вне PIE — русское (как у `MobName`).
	ASDAREBORN_API FString NpcName(int32 NpcId);

	// Имя NPC для плашки: из таблицы, а если строки нет — `BakedName` (запечённое `AAsdaNpcActor::NpcName`).
	ASDAREBORN_API FString NpcDisplayName(int32 NpcId, const FString& BakedName);

	// Имя цели квеста по виду записи гида/маршрута: 1 = моб (`MobName` без гарда модели), иначе NPC.
	//   Эталон: `sub_939242(g_pMobMgr, id)` / `npcTalk_FindNameById(g_pNpcWnd, id)` (quest.c:1055/1060, :4879/4884).
	//   ★ [ОРИГИНАЛ] 25.10.2026: у моба `Id` — ИНДЕКС строки `Mob list.BIN` (`база + 272*id`), не серверный type
	//   (type = строка + 1). У NPC `Id` — поле id `npc_list` (поиск по полю, как раньше).
	ASDAREBORN_API FString QuestTargetName(int32 TargetKind, int32 Id);

	// Ключ отката `asda.Quest.GuideMobRow`: true (умолчание) — цель-моб гида = строка `Id` (эталон); false — прежняя
	//   строка `Id − 1`. Аксессор нужен стенду и самотесту (имя ключа вне этого .cpp не используем — Shipping-правило).
	ASDAREBORN_API bool QuestGuideMobRowOn();

	// Объектные пути таблиц — единственные литералы (нужны стенду сверки).
	extern ASDAREBORN_API const TCHAR* const kMobNamesPath;
	extern ASDAREBORN_API const TCHAR* const kMobSoundsPath;
	extern ASDAREBORN_API const TCHAR* const kNpcNamesPath;
}
