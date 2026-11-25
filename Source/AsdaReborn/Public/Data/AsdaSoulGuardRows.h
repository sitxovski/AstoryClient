// Copyright (c) 2026 @sitxovski (Discord)
// AsdaSoulGuardRows.h — строки DataTable для двух КЛИЕНТСКИХ таблиц системы «хранителя»
//   (Soul Guardian / Soul Boost), ЭТАП 3 окна MINISTATE_WIN.
//
// ═══════════════════════════ ОТКУДА РАСКЛАДКА (не догадки) ══════════════════════════════════════
// Обе таблицы грузятся эталоном, но ЧИТАТЕЛЕЙ ПОЛЕЙ в срезе почти нет — раскладка снята
//   HEX-РАЗБОРОМ самих файлов и ПОДТВЕРЖДЕНА тремя независимыми источниками:
//     1) поле «число колонок» в заголовке файла (+8) сходится с числом полей записи;
//     2) серверная текстовая таблица `.\SkillTable_Info\SoulGuarderInfoTable.txt` совпадает
//        с клиентской ЗАПИСЬ В ЗАПИСЬ (27 строк: index, skillType, row, buffSkillId, guardianSkillId);
//     3) единственный читатель полей в клиенте — `hudQuickSlotLoadSkills@0xEF44E0` (ui_core.c:92650):
//          `table[489*i + 0]` (= байт +4  → SkillType)  сравнивается с аргументом-типом,
//          `table->field_8[489*i]` (= байт +8  → RowIndex) сравнивается с номером ряда,
//          `table->gap_C[1956*i + 140]` (= байт +152 → GuardianSkillId) кладётся в слот ряда.
//        (489 dword = 1956 байт = размер записи.)
//
// ── ./Data/SoulGuarderTable.BIN — `CItemMgr_LoadSoulGuarderTable@0xEF4E30` ─────────────────────
//   Заголовок 20 б: `i32 ver=0 | i32 count=27 | i32 fieldCount=39 | "SGT\0" | i32 pad`.
//   Запись 1956 б (0x7A4), fieldCount 39 = 4 i32 + name + iconBig + 2 i32 + guardianSkillId
//                                          + iconSmall + 2 i32 + 27 строк:
//     +0   i32      SoulGuardIndex   0..26 (то, что приезжает опкодом 6156)
//     +4   i32      SkillType        1..9 — «элемент/тип» хранителя (CUser+259168 на сервере)
//     +8   i32      RowIndex         0..2 — ряд монет, к которому привязан хранитель
//     +12  i32      BuffSkillId      1201..1227 — ПАССИВНЫЙ бафф уровня хранителя
//     +16  char[64] Name             имя (cp1251 в RU-сборке): «Хранитель зари», …
//     +80  char[64] IconBig          "sb_icon_big"
//     +144 i32      Unk144           во всех 27 записях = 0
//     +148 i32      Unk148           во всех 27 записях = 0
//     +152 i32      GuardianSkillId  1228..1254 — АКТИВНЫЙ скилл ряда (его жмёт игрок)
//     +156 char[64] IconSmall        "sb_icon_small"
//     +220 i32      Unk220           во всех 27 записях = 0
//     +224 i32      Unk224           во всех 27 записях = 0
//     +228 char[64] × 27             модель компаньона, 11 анимаций, 3 эффекта, 11 звуков, портрет
//   ★ Имена 27 строковых полей выведены ИЗ СОДЕРЖИМОГО самих строк (они самоописательные:
//     `sb_md_ani_oh_call_01_00`, `S_e_sb_md_oh_off_00`, `Im_sb_oh_00`), а не придуманы.
//     Читателей этих полей в срезе клиента НЕТ (закон dead-code-markings-suspect: пишем
//     «ссылок в срезе нет», а не «мёртвый код») — это ресурсы визуального компаньона-хранителя.
//
// ── ./Data/SkillCoinTable.bin — `CItemMgr_LoadSkillCoinTable@0xEF4F90` ─────────────────────────
//   Заголовок 20 б: `i32 ver=0 | i32 count=1255 | i32 fieldCount=5 | "SGP\0" | i32 pad`.
//   Запись 20 б = 5 × i32, ИНДЕКС = SkillId:
//     +0  i32 SkillId   0..1254 (совпадает с порядковым номером записи)
//     +4  i32 Coin0     сколько монет даёт этот боевой скилл в РЯД 1
//     +8  i32 Coin1     …в РЯД 2
//     +12 i32 Coin2     …в РЯД 3
//     +16 i32 Field16   ★ ИСПРАВЛЕНО 2026-08-07 (перепроверено чтением самого .bin): НЕ «везде −1».
//                       −1 у обычных скиллов, но у 54 записей 1201..1254 там 1/2/3 — это НОМЕР
//                       БОЕВОГО МОТИОНА КОМПАНЬОНА-ХРАНИТЕЛЯ и одновременно номер эффекта:
//                       1 → мотион 5 + эффект слота 31, 2 → мотион 10 + 32, 3 → мотион 11 + 33.
//                       Читатель — `UpdateList_ModelAnim` через sub_EF3FA0 (soul_guardian_companion_re.md
//                       §3.3/§7.2). Формула по данным: variant = ((skillId − 1201) % 3) + 1.
//   ★ Это клиентская копия серверной `SoulGuarderCoinTable.txt` (там та же таблица, но
//     `i16 skillId + 3×i8 + i8`, 1101 строка). Сверка 1255 против 1101: расхождений в
//     ПЕРЕСЕКАЮЩЕМСЯ диапазоне — 6 строк (1020/1022/1024/1031/1034/1040), где у нашей копии
//     сервера нули, а у клиента значения; остальные 154 записи клиента (1101..1254) нулевые.
//     Начисляет монеты СЕРВЕР по своей копии — клиентская нужна для локального показа/проверки.
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "Data/AsdaSkillCoinRow.h"   // ★ R3: FAsdaSkillCoinRow (вынесено отсюда)
#include "AsdaSoulGuardRows.generated.h"

// Одна из 27 записей SoulGuarderTable.BIN. Ключ строки DataTable = SoulGuardIndex.
USTRUCT(BlueprintType)
struct FAsdaSoulGuarderRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|SoulGuard") int32 SoulGuardIndex = -1;  // +0
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|SoulGuard") int32 SkillType = 0;        // +4
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|SoulGuard") int32 RowIndex = 0;         // +8
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|SoulGuard") int32 BuffSkillId = -1;     // +12  (1201..1227)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|SoulGuard") FString Name;               // +16  char[64]
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|SoulGuard") FString IconBig;            // +80  char[64]
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|SoulGuard") int32 Unk144 = 0;           // +144 (везде 0)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|SoulGuard") int32 Unk148 = 0;           // +148 (везде 0)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|SoulGuard") int32 GuardianSkillId = -1; // +152 (1228..1254)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|SoulGuard") FString IconSmall;          // +156 char[64]
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|SoulGuard") int32 Unk220 = 0;           // +220 (везде 0)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|SoulGuard") int32 Unk224 = 0;           // +224 (везде 0)

	// 27 строк по 64 б с +228. Имена — по содержимому (пример записи 0, тип «oh»).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|SoulGuard") FString Model;          // [ 0] +228  МЕШ .jbm (слоты switch 2 и 90): sb_md_oh_01
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|SoulGuard") FString AniCall;        // [ 1] +292  клип МОТИОНА 8 — CALL (призыв, однократный)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|SoulGuard") FString AniOff;         // [ 2] +356  клип МОТИОНА 9 — OFF (уход, однократный)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|SoulGuard") FString AniIdle0;       // [ 3] +420  клип МОТИОНА 0 — IDLE A (70 % выбора в покое)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|SoulGuard") FString AniIdle1;       // [ 4] +484  клип МОТИОНА 1 — IDLE B (30 %)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|SoulGuard") FString AniBattle0;     // [ 5] +548  клип МОТИОНА 6 — BATTLE-IDLE (боевая стойка)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|SoulGuard") FString AniBattle1;     // [ 6] +612  клип МОТИОНА 3 — ATTACK A
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|SoulGuard") FString AniBattle2;     // [ 7] +676  клип МОТИОНА 4 — ATTACK B
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|SoulGuard") FString AniBattle3;     // [ 8] +740  клип МОТИОНА 5 — SKILL 1 (однократный)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|SoulGuard") FString AniMove;        // [ 9] +804  клип МОТИОНА 7 — MOVE
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|SoulGuard") FString AniSlot10;      // [10] +868  клип МОТИОНА 10 — SKILL 2. ★ НЕ дубль [8]: у записей
	//   SkillType 2/5/8 это ..._04, у 3/6/9 — ..._05 (совпадает с [8] только у 1/4/7)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|SoulGuard") FString AniSlot11;      // [11] +932  клип МОТИОНА 11 — SKILL 3 (та же логика, что у [10])
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|SoulGuard") FString EffSkill1;      // [12] +996  .jeb слота 31 — эффект мотиона 5 (на кости ВЛАДЕЛЬЦА)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|SoulGuard") FString EffSkill2;      // [13] +1060 .jeb слота 32 — эффект мотиона 10
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|SoulGuard") FString EffSkill3;      // [14] +1124 .jeb слота 33 — эффект мотиона 11
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|SoulGuard") FString SndCall;        // [15] +1188 звук слота 108 = мотион 8
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|SoulGuard") FString SndOff;         // [16] +1252 звук слота 109 = мотион 9
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|SoulGuard") FString SndIdle0;       // [17] +1316 звук слота 100 = мотион 0
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|SoulGuard") FString SndIdle1;       // [18] +1380 звук слота 101 = мотион 1
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|SoulGuard") FString SndBattleIdle;  // [19] +1444 звук слота 106 = мотион 6
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|SoulGuard") FString SndAttack0;     // [20] +1508 звук слота 103 = мотион 3
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|SoulGuard") FString SndAttack1;     // [21] +1572 звук слота 104 = мотион 4
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|SoulGuard") FString SndAttack2;     // [22] +1636 звук слота 105 = мотион 5
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|SoulGuard") FString SndAttack3;     // [23] +1700 звук слота 110 = мотион 10
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|SoulGuard") FString SndAttack4;     // [24] +1764 звук слота 111 = мотион 11
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|SoulGuard") FString SndMove;        // [25] +1828 звук ДВИЖЕНИЯ (мотион 7). ★ДЕФЕКТ ОРИГИНАЛА: в бинаре
	//   последний звуковой case — 102, а не 107, поэтому звук бега НЕ ИГРАЕТ НИКОГДА.
	//   Порт читает поле как 107 (AAsdaSoulGuardianActor::MotionSoundName, флаг bLegacyMoveSoundBug)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|SoulGuard") FString Portrait;       // [26] Im_sb_oh_00
};

// ★ R3 (11.11.2026): `FAsdaSkillCoinRow` вынесено дословно в `Data/AsdaSkillCoinRow.h` (включён в шапке этого файла).
