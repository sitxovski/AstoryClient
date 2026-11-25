// Copyright (c) 2026 @sitxovski (Discord)
// Типы пакетов входа и выбора персонажа: состояние соединения, запись списка персонажей, ячейка экипировки, персонаж
//   соулмейта («одержимость»), делегат результата входа.
//   ★ R1 рефакторинга (10.11.2026): дословно вынесены из `AsdaNetSubsystem.h`, чтобы потребителю типов не нужен был весь
//   класс сети. `AsdaNetSubsystem.h` включает этот файл — прежние потребители ничего не теряют. Отражение UE (имена,
//   модуль `/Script/AsdaReborn`) не менялось.
#pragma once
#include "CoreMinimal.h"
#include "AsdaNetCharTypes.generated.h"

UENUM()
enum class EAsdaNetState : uint8
{
	Idle, ConnectingLogin, LoginSent, CharSelect, EnteringWorld, InWorld, Disconnected
};

// Одна ячейка экипировки персонажа (0x3EF uiEquipGrid_FillItemRecords@0xF05E90).
// Индекс ячейки = itemType (part-slot: см. tagCHARACTER_SetPartsModelByType@0x9E16E0):
//   0=HAIR 1=FACE 2=CHEST(тело/броня) 3=GLOVES 4=PANTS 5=SHOES 6=WEAPON 7=ARMOR 8=HELMET 9=? 10=RIDING.
// В char-select на модель влияют CHEST(2)/GLOVES(3)/PANTS(4)/SHOES(5)/HELMET(8)/WEAPON(6).
USTRUCT(BlueprintType)
struct FAsdaEquipItem
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, Category="AsdaNet") int32 ItemId  = 0;   // 1007/1024 cell+0 (u32) код предмета (0 = слот пуст)
	// ★ ИСПРАВЛЕНО 2026-09-02 (ревизия). БЫЛО: «0x3EF cell+10 (u16) enchant/plus» — комментарий ВРАЛ,
	//   и в это поле писалось слово из `cell+4`. На проводе char-list ЗАТОЧКИ НЕТ ВООБЩЕ:
	//   `cell+4` — это `EqSlot.Slot` (row+5 процедур `UPPreEqitem`/`UPPreSoulEqItem`), эталон кладёт
	//   его в запись предмета +5 = SlotIndex (`uiEquipGrid_FillItemRecords@0xF05E90` → +1593).
	//   Заточка приходит ДРУГИМ пакетом — 1027, запись +0x18 (порт сам так и читает её
	//   в `AsdaWorldSubsystem`). Поэтому поля РАЗВЕДЕНЫ, а не переименованы: иначе пострадала бы
	//   верная запись оттуда. До экрана `Enchant` из char-list не доходил (читателей не было) —
	//   это была мина, а не видимый дефект.
	UPROPERTY(BlueprintReadOnly, Category="AsdaNet") int32 Enchant = 0;   // ТОЛЬКО из 1027 (запись+0x18); в 1007/1024 не приходит
	UPROPERTY(BlueprintReadOnly, Category="AsdaNet") int32 EqSlot  = -1;  // 1007/1024 cell+4 (i16) = EqSlot.Slot с провода
	bool IsEmpty() const { return ItemId == 0; }
};

// Запись персонажа из char-list. Данные приходят РАЗНЫМИ опкодами в один record на слот (3 слота):
//   0x3FF charInfoParsePacket — имя/класс/уровень/HP/MP/6 статов (запись 58б, смещения см. charselect_record_layout.md §2A).
//   0x3EE charSelect_FillSlotRecords — constellationId (Body[190+i]) + признак пустого слота (Body[4+59*i]==0).
//   0x3EF uiEquipGrid_FillItemRecords — экип (11 ячеек×14б по itemType) → Equip[itemType].
USTRUCT(BlueprintType)
struct FAsdaCharRecord
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, Category="AsdaNet") uint8  Slot = 0xFF;   // slotId (slot = id%10); 0xFF = пустой слот
	UPROPERTY(BlueprintReadOnly, Category="AsdaNet") uint8  RawSlotId = 0; // СЫРОЙ slotId из 0x3EE (10/11/12) — rec[1076]; шлётся в select(1016)/delete(1018), НЕ индекс 0..2
	UPROPERTY(BlueprintReadOnly, Category="AsdaNet") FString Name;          // 0x3FF rec+2 char[32]
	// ★ ПОЛ. 0x3EE rec+22 (= payload[59*i+26]) — эталон charSelect_FillSlotRecords@0xF06310 кладёт этот байт
	//   в v3[1079] = CCharacter+0x437 = `gender`. 1=муж, 2=жен; 0 = слот пуст / сервер не заполнил.
	UPROPERTY(BlueprintReadOnly, Category="AsdaNet") uint8  Sex = 0;
	UPROPERTY(BlueprintReadOnly, Category="AsdaNet") uint8  Job = 0;        // 0x3FF rec+24 класс (classId)
	UPROPERTY(BlueprintReadOnly, Category="AsdaNet") uint8  SubClass = 0;   // 0x3FF rec+25 subClass
	UPROPERTY(BlueprintReadOnly, Category="AsdaNet") uint8  Level = 0;      // 0x3FF rec+26 уровень
	UPROPERTY(BlueprintReadOnly, Category="AsdaNet") int64  Exp = 0;        // 0x3EE rec+26/+30 (i64) — total exp (для exp-бара)
	UPROPERTY(BlueprintReadOnly, Category="AsdaNet") int32  HpMax = 0;      // 0x3FF rec+35 (u32)
	UPROPERTY(BlueprintReadOnly, Category="AsdaNet") int32  HpCur = 0;      // 0x3FF rec+41 (u32)
	UPROPERTY(BlueprintReadOnly, Category="AsdaNet") int32  MpMax = 0;      // 0x3FF rec+39 (u16)
	UPROPERTY(BlueprintReadOnly, Category="AsdaNet") int32  MpCur = 0;      // 0x3FF rec+45 (u16)
	// 6 статов (u16) в порядке инфо-панели: [0]Сила [1]Ловкость [2]Выносливость [3]Энергия [4]Интеллект [5]Удача
	UPROPERTY(BlueprintReadOnly, Category="AsdaNet") TArray<int32> Stats;   // 0x3FF rec+47..+57 (6×u16)
	UPROPERTY(BlueprintReadOnly, Category="AsdaNet") uint8  Constellation = 0; // 0x3EE Body[190+i] (u8)
	// Внешность (0x3EE блок сразу после 3 записей: hair@181+3i, color@182+3i, face@183+3i; эталон
	//   charSelect_FillSlotRecords:498 memcpy 3б → rec[1121/1122/1123]). Face → текстура лица превью (A1).
	UPROPERTY(BlueprintReadOnly, Category="AsdaNet") uint8  Hair = 0;          // причёска (в превью НЕ рендерится)
	UPROPERTY(BlueprintReadOnly, Category="AsdaNet") uint8  Color = 0;         // цвет/тинт (A2, применение вне 1:1 пока)
	UPROPERTY(BlueprintReadOnly, Category="AsdaNet") uint8  Face = 0;          // индекс лица 0..10 → текстура ch1_{m|f}_00_face001_{NN}
	// Экипировка по itemType (11 ячеек, индекс = part-slot). Заполняется 0x3EF; влияет на превью-модель.
	UPROPERTY(BlueprintReadOnly, Category="AsdaNet") TArray<FAsdaEquipItem> Equip;
	bool IsEmpty() const { return Slot == 0xFF; }

	// Имя класса по classId (loc-таблица 12556+job; RU из charselect_loc_extracted.json).
	static FString ClassName(uint8 Job);
	// Имя созвездия по constellationId (Constellation.BIN; RU 1..12 из charselect_loc_extracted.json).
	static FString ConstellationName(uint8 Id);
};

// ══════════════════ ★★★ «ОДЕРЖИМОСТЬ»: ПЕРСОНАЖ СОУЛМЕЙТА НА ЭКРАНЕ ВЫБОРА ═════════════════════
//
// ЧТО ЭТО. Персонаж, которому его владелец поставил галку «поделиться» в игровом окне 6400,
//   встаёт РЯДОМ с нашими на экране выбора персонажа полной 3D-моделью (внешность + надетое),
//   и им можно войти в игру. Оригинал зовёт это состояние «possessed» (одержимость):
//   тело/предметы/навыки доступны, но выбрасывать и торговать нельзя.
//   Полный разбор — `Reports/soulmate_share_display_server.md` и `…_client.md`.
//
// ОТКУДА ДАННЫЕ. Логин-сервер, `CUserProc::Send_GetCharInfo@0x407CE0` (`LoginManager.c:2891-3260`),
//   в ответ на ОДИН наш запрос 1005 отдаёт залпом:
//     1007×N (экип МОИХ)  →  1006 (список МОИХ)
//     1024×N (экип ЧУЖИХ) →  1023 (список ЧУЖИХ) →  1025 (статус ветки)
//   ★ Экипировка ВСЕГДА приходит РАНЬШЕ списка — и у своих, и у чужих (`LoginManager.c:3081` до
//   `:3084`). Поэтому 1024 создаёт запись, а 1023 её дополняет, а не наоборот.
//   GameServer в этом тракте не участвует ни одним байтом: MS SQL → LoginManager → клиент.
//
// ПОЛЯ. Запись 58 б пакета 1023 (`memcpy(&v59[58*i+1], &v44[v50], 0x3A)`), колонки процедуры
//   `UPPreSoulMateChar` (`WHERE Userindex=<аккаунт соулмейта> AND Share=1`), упакованные плотно:
//     rec+0 Charidx u8 · rec+1..20 Charname char[20] cp1251 БЕЗ терминатора · rec+21 Clan u8
//     (в процедуре жёстко `SET @Clan = 0`) · rec+22 Gender · rec+23 Job · rec+24 SubJob ·
//     rec+25 Lvl · rec+26 Expe i64 · rec+34 MaxHp i32 · rec+38 MaxMp i16 · rec+40 Hp i32 ·
//     rec+44 Mp i16 · rec+46..57 шесть статов i16.
//   Внешность лежит ХВОСТОМ пакета (payload[175+3*j], 3 б) — отдельной процедурой `…CharApp`.
//   Экипировка приходит пакетом 1024 (11 ячеек по 14 б), см. `Equip` ниже.
USTRUCT(BlueprintType)
struct FAsdaSoulmateSharedChar
{
	GENERATED_BODY()

	// СЫРОЙ Charidx (10/11/12) — rec+0. Индекс в массиве = Charidx%10. 0 = запись не заполнена.
	//   ★ Именно это поле оригинал терял (см. коммент к обработчику 1023): его затирал 32-байтовый
	//   memcpy имени, из-за чего персонаж соулмейта не находился НИКОГДА.
	UPROPERTY(BlueprintReadOnly, Category="AsdaNet") uint8 Charidx = 0;
	UPROPERTY(BlueprintReadOnly, Category="AsdaNet") FString CharName;      // rec+1, 20 б cp1251
	// rec+21. Процедура `UPPreSoulMateChar` объявляет `DECLARE @Clan TINYINT; SET @Clan = 0` —
	//   значение ВСЕГДА 0. Поле оставлено, потому что оно занимает байт записи и участвует в раскладке.
	UPROPERTY(BlueprintReadOnly, Category="AsdaNet") uint8 ClanId = 0;
	UPROPERTY(BlueprintReadOnly, Category="AsdaNet") uint8 Gender = 0;      // rec+22: 1=муж, 2=жен (выбор модели/клипов)
	UPROPERTY(BlueprintReadOnly, Category="AsdaNet") uint8 Job = 0;         // rec+23 класс (подпись loc 12556+Job)
	UPROPERTY(BlueprintReadOnly, Category="AsdaNet") uint8 SubJob = 0;      // rec+24 подкласс (loc 12556+SubJob при ≠0)
	UPROPERTY(BlueprintReadOnly, Category="AsdaNet") uint8 Level = 0;       // rec+25 уровень
	UPROPERTY(BlueprintReadOnly, Category="AsdaNet") int64 Exp = 0;         // rec+26 (i64, bigint Expe)
	UPROPERTY(BlueprintReadOnly, Category="AsdaNet") int32 MaxHp = 0;       // rec+34 (i32)
	UPROPERTY(BlueprintReadOnly, Category="AsdaNet") int32 MaxMp = 0;       // rec+38 (i16)
	UPROPERTY(BlueprintReadOnly, Category="AsdaNet") int32 Hp = 0;          // rec+40 (i32)
	UPROPERTY(BlueprintReadOnly, Category="AsdaNet") int32 Mp = 0;          // rec+44 (i16)
	// Шесть статов rec+46..57 (i16) в порядке процедуры: Strength, Agility, Health, Spell, Mentality, Fortune.
	UPROPERTY(BlueprintReadOnly, Category="AsdaNet") TArray<int32> Stats;
	// Внешность — хвост 1023 (payload[175+3*j]): причёска, цвет волос, тип лица.
	//   ★ В записи 58 б их НЕТ: сервер берёт их отдельной процедурой `UPPreSoulMateCharApp`
	//   и приклеивает блоком по 3 б. Без них модель не собрать, поэтому поля здесь.
	UPROPERTY(BlueprintReadOnly, Category="AsdaNet") uint8 Hair = 0;
	UPROPERTY(BlueprintReadOnly, Category="AsdaNet") uint8 HairColor = 0;
	UPROPERTY(BlueprintReadOnly, Category="AsdaNet") uint8 Face = 0;
	// Userindex АККАУНТА-ВЛАДЕЛЬЦА (соулмейта) — 1024 [0..3]. В 1023 его нет.
	//   ★ Обязателен для входа одержимым: 1016 в режиме 2 шлёт ИМЕННО его, а не наш UserIndex
	//   (эталон `charDealSendReq`: `*(DWORD*)(charDealMsg+1) = subRecord[1052]`).
	UPROPERTY(BlueprintReadOnly, Category="AsdaNet") int32 OwnerUserIndex = 0;
	// Экипировка из 1024 (11 ячеек по 14 б). Тип общий с char-list (закон `check-existing-before-creating`):
	//   индекс = itemType из item-таблицы (фолбэк — поле Slot ячейки), значение — код предмета.
	//   ★ `Enchant` здесь ВСЕГДА 0: в этом пакете заточки нет (см. коммент к обработчику 1024).
	UPROPERTY(BlueprintReadOnly, Category="AsdaNet") TArray<FAsdaEquipItem> Equip;

	// Запись пуста, если сервер её не прислал (Charidx вне 10..12).
	bool IsEmpty() const { return Charidx < 10 || Charidx > 12; }
};

// Результат логина (recv 1004 body[0]): 1=OK, 2=ID fail, 3=pass fail, 4=overlap(уже в игре, старый кикнут→ретрай),
// 5+=err. Подписчик решает: 1→флоу, 4→перелогин, иначе→ошибка.
DECLARE_MULTICAST_DELEGATE_OneParam(FOnAsdaLoginResult, uint8 /*Status*/);
