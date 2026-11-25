// Copyright (c) 2026 @sitxovski (Discord)
// AsdaEffectMobTableRows.h — строки DataTable для трёх «досок данных» оригинала:
//   DT_EffectList        ← Data/Effect/effect list.bin        (магия "EFF")
//   DT_CallMob           ← Data/Mob/CallMobTable.BIN          (магия "CMT")
//   DT_MonsterSkillEffect← Data/Mob/Monster_SkillEffect.BIN    (магия "MSE")
// Импортёры: Tools/asda_build_effect_list_dt.py, asda_build_callmob_dt.py,
//            asda_build_monster_skilleffect_dt.py. Сверка: Tools/asda_verify_effect_mob_tables.py.
// Во всех строках есть поле DataHex — сырая запись файла 1:1 (hex), чтобы ни один байт оригинала
// не был потерян даже там, где семантика поля не подтверждена эталоном.
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "AsdaEffectMobTableRows.generated.h"

// ---------------------------------------------------------------------------
// DT_EffectList — реестр имён эффектов из Data/Effect/effect list.bin.
// ★ ЧЕСТНО: загрузчика этого файла в клиенте НЕТ (строки "effect list" нет ни в AsdaStory.exe,
//   ни в Launcher/GameUpdater; fourcc "EFF" нигде не разбирается). Раскладка выведена по размеру
//   файла и по полю «число колонок» заголовка (=2), а не по декомпилу — см. отчёт
//   Reports/effect_list_tables.md. Файл байт-в-байт одинаков во всех 5 имеющихся клиентах.
// Формат (452 б): заголовок 20 б {float 0.1 @0; int Count=12 @4; int Columns=2 @8; "EFF\0" @12; int 1 @16},
//   далее КОЛОНОЧНО (SoA): char Name[12][32] (384 б), затем int32 Flag[12] (48 б).
// Эффекты движка резолвятся ОРИГИНАЛОМ по строковому имени (CEffectMgr_FindNamedEffect @0xF55A30 —
//   std::map<std::string,…>), индексного реестра в рантайме клиента не существует.
USTRUCT(BlueprintType)
struct FAsdaEffectListRow : public FTableRowBase
{
	GENERATED_BODY()
	// Порядковый индекс в файле (0..Count-1); он же RowName.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|EffectList") int32 Index = 0;
	// Имя эффекта как в файле (as-is, без расширения). Соответствующих .jeb в поставке НЕТ.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|EffectList") FString EffectName;
	// Вторая колонка файла. Значения: 1 у "eft_bow" и "eft_swrdspark", 0 у остальных.
	// Семантика НЕ подтверждена (загрузчика нет) — храним как есть.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|EffectList") int32 Flag = 0;
	// Сырьё 1:1: 32 байта имени + 4 байта Flag (hex).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|EffectList") FString DataHex;
};

// ---------------------------------------------------------------------------
// DT_CallMob — Data/Mob/CallMobTable.BIN (магия "CMT", заголовок 20 б, запись 32 б = 8×int32,
//   8 записей; файл байт-в-байт одинаков в трёх наших сборках клиента, md5 279ce135…).
// Загрузчик CONFIRMED: mobLoadCallMobTable @0xF76D10 — fread(hdr,0x14), strncmp("CMT",4),
//   new[](32*count), fread(rows, 0x20, count).
// ★ ПОПРАВКА 27.09.2026: потребителей ДВА — ещё гейт приёма 6126 (`netHandleCharIdByteUpdate@0x973E30` читает
//   байт +16 записи 0 через `dword_34A0490` = `g_pMobMgr+0x9A8`, см. поле SpawnArgByte). Прежде здесь стояло:
// ЕДИНСТВЕННЫЙ потребитель таблицы в клиенте — objListFindByTwoKeys @0xF77000:
//   ищет запись, у которой ОДНОВРЕМЕННО dword[1] == a2 и dword[5] == a3, и возвращает её адрес.
// Вызов ровно один — `game_main.c:10211`, команда меню NPC 30050 «призыв рейд-босса»:
//     itemId       = getField3036(&g_pLocalPlayer);                       // ← id ТЕКУЩЕЙ КАРТЫ
//     recipeRecord = objListFindByTwoKeys(g_pMobMgr, pThis[436], itemId); // pThis[436] = npcId собеседника
//     materialId   = *(DWORD*)(recipeRecord + 8);
//     if (CInventory_CountItemById(&g_InventoryBag, materialId) < *(DWORD*)(recipeRecord + 12)) → отказ
//     netSendGuarded_op6124_invMatchList(pThis, userIdx, materialId, *(DWORD*)(recipeRecord + 12));
//   `getField3036` @0x9661F0 = поле +3036 локального игрока; оно же уходит в
//   `worldMapPlaceMarkersByType(g_pMap, …)` (game_main.c:3488) и сравнивается с 19/23 — это mapId.
// ★ ВТОРОЙ, НЕЗАВИСИМЫЙ ИСТОЧНИК (снимает все догадки о «неизвестных» полях):
//   у сервера та же таблица лежит текстом — `GameServer\MonsterData_Info\CallMobTable.txt`,
//   загрузчик sub_4B51B0 @0x4B51B0. Первые ВОСЕМЬ её колонок ПОБАЙТНО совпадают с записями
//   0…5 клиентского .BIN (клиентский файл — надмножество: в нём ещё 2 записи), а потребитель
//   Recv_RaidMonsterCallReq @0x428C50 (опкод 6124) и ответная recv @0x429280 читают их так:
//     txt col1 → rec+2  i16  — npcId               (клиент +4,  1-й ключ поиска)
//     txt col2 → rec+4  i32  — materialId          (клиент +8,  сверяется с id предмета из пакета)
//     txt col3 → rec+8  i16  — needCount           (клиент +12, сверяется с числом отданных предметов)
//     txt col4 → rec+10 u8   — SpawnArgByte        (клиент +16, уходит в NpcServer пакетом 8910)
//     txt col5 → rec+11 u8   — mapId               (клиент +20, сверяется с id карты мира)
//     txt col6 → rec+12 i16  — SpawnArgWord        (клиент +24, уходит в NpcServer пакетом 8910)
//     txt col7 → rec+14 i16  — BossSlotIndex       (клиент +28, проверка `0 <= v < monsterCount`,
//                                                   затем «already called sommon monster» по этому слоту)
//   Поэтому «Unk»-полей у этой таблицы больше НЕТ: имена ниже отражают роль, подтверждённую сервером.
// Спавн призванного моба — СЕРВЕРНЫЙ; клиент держит таблицу как каталог рецептов.
USTRUCT(BlueprintType)
struct FAsdaCallMobRow : public FTableRowBase
{
	GENERATED_BODY()
	// +0  порядковый индекс записи (совпадает с RowName и с колонкой 0 серверного .txt).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|CallMob") int32 Index = 0;
	// +4  npcId собеседника — ПЕРВЫЙ ключ поиска (objListFindByTwoKeys, dword[1]).
	//     Значения 187/57/21/22/23/41/200/2 — это номера NPC, а не коды предметов (те от 20000).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|CallMob") int32 NpcId = 0;
	// +8  код ПРЕДМЕТА-материала, который считается в сумке (CInventory_CountItemById)
	//     и уходит в пакет 6124. Значения 23522/20966/23542…/75208/79373.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|CallMob") int32 MaterialId = 0;
	// +12 сколько штук материала нужно (оно же maxMatches в теле пакета 6124): 6 у семи записей, 1 у последней.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|CallMob") int32 NeedCount = 0;
	// +16 байт-аргумент серверной команды призыва (NpcServer, опкод 8910). В данных 1 только у записи 0
	//     (npcId 187, карта 15), у остальных 0.
	//     ★ ПОПРАВКА 27.09.2026: клиент это поле ЧИТАЕТ — у записи 0 и только у неё: гейт приёма 6126
	//     `netHandleCharIdByteUpdate@0x973E30` `*(BYTE*)(dword_34A0490 + 16) == 1`, где `dword_34A0490` =
	//     `g_pMobMgr + 0x9A8` = указатель на записи этой таблицы (xref IDB). Порт: `UAsdaKingCrownSubsystem::IsGateOpen`.
	//     Прежняя фраза «клиент не читает» была ложным «нет» (поиск шёл по потребителю objListFindByTwoKeys).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|CallMob") int32 SpawnArgByte = 0;
	// +20 id карты — ВТОРОЙ ключ поиска (objListFindByTwoKeys, dword[5]); значения 3…18.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|CallMob") int32 MapId = 0;
	// +24 word-аргумент той же серверной команды 8910. Во всех записях 0. Клиент не читает.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|CallMob") int32 SpawnArgWord = 0;
	// +28 индекс слота рейд-босса в списке монстров карты (сервер: `>=0 && < monsterCount`).
	//     Во всех записях 0. Клиент не читает.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|CallMob") int32 BossSlotIndex = 0;
	// Сырьё 1:1: вся 32-байтная запись (hex) — чтобы ни один байт оригинала не потерялся.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|CallMob") FString DataHex;
};

// ---------------------------------------------------------------------------
// DT_MonsterSkillEffect — Data/Mob/Monster_SkillEffect.BIN (магия "MSE", заголовок 20 б,
//   запись 0x2D8 = 728 б, 91 запись). Загрузчик CONFIRMED: Mob_LoadSkillEffects @0xF76770.
// Раскладка выведена из skillCloneEntryByIndex @0xF76900 (смещения там даны от &count, т.е. на 4
//   больше внутризаписных) — он разворачивает файловую запись в рантайм-структуру 0x19C:
//     for i in 0..4:  Jeb[i]      ← rec+0x004+64*i   (если нет расширения — дописывается ".jeb")
//                     Bone[i]     ← rec+0x14E+64*i
//                     SlotType[i] ← rec+0x144+i  (байт; если SlotType[3] != 0 → взводится флаг +408)
//                     SlotParam[i]← rec+0x149+i  (байт → dword)
//     EffectId ← rec+0x000 (word);  RangeA ← rec+0x2D0 (float);  RangeB ← rec+0x2D4 (float)
//   Дефолты того же объекта (sub_F76BA0 @0xF76BA0): RangeA=500.0, RangeB=800.0 — подтверждает float.
// Зона rec+0x28E..0x2CF (66 б) и rec+0x002 (word) — НУЛИ во всех 91 записях (проверено побайтно).
USTRUCT(BlueprintType)
struct FAsdaMonsterSkillEffectRow : public FTableRowBase
{
	GENERATED_BODY()
	// +0x000 word — id эффекта скилла моба; в файле совпадает с порядковым индексом (0..90).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|MobSkillFx") int32 EffectId = 0;
	// +0x002 word — 0 во всех записях (резерв/выравнивание).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|MobSkillFx") int32 Reserved2 = 0;
	// +0x004 +64*i — имя .jeb слота i (as-is; расширение в файле опущено).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|MobSkillFx") FString Jeb0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|MobSkillFx") FString Jeb1;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|MobSkillFx") FString Jeb2;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|MobSkillFx") FString Jeb3;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|MobSkillFx") FString Jeb4;
	// +0x144+i — байт-тип слота i (0=слот пуст; в данных 1/2/3/4/5).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|MobSkillFx") int32 SlotType0 = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|MobSkillFx") int32 SlotType1 = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|MobSkillFx") int32 SlotType2 = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|MobSkillFx") int32 SlotType3 = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|MobSkillFx") int32 SlotType4 = 0;
	// +0x149+i — байт-параметр слота i (в данных 0/8/15); в рантайме расширяется в dword.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|MobSkillFx") int32 SlotParam0 = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|MobSkillFx") int32 SlotParam1 = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|MobSkillFx") int32 SlotParam2 = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|MobSkillFx") int32 SlotParam3 = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|MobSkillFx") int32 SlotParam4 = 0;
	// +0x14E+64*i — имя кости привязки слота i (обычно "Bip01 Footsteps").
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|MobSkillFx") FString Bone0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|MobSkillFx") FString Bone1;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|MobSkillFx") FString Bone2;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|MobSkillFx") FString Bone3;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|MobSkillFx") FString Bone4;
	// +0x2D0 float — дефолт объекта 500.0; в данных 500/1000/2000. Дистанция/радиус (семантика не подтверждена).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|MobSkillFx") float RangeA = 0.f;
	// +0x2D4 float — дефолт объекта 800.0; в данных 1000/1500.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|MobSkillFx") float RangeB = 0.f;
	// Сырьё 1:1: вся 728-байтная запись (hex).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|MobSkillFx") FString DataHex;
};
