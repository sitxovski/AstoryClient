// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ «ЭТО Я?» — ЕДИНАЯ ТОЧКА НОМЕРА / ПЕРСОНАЖА / УРОВНЯ СВОЕГО ИГРОКА (аудит дублей 14.09.2026: E2 / K3 / С7). [ПОРТ]
//
//   ЗАЧЕМ. Сессионный номер своего игрока (`UAsdaNetSubsystem::SelfUserNum`, i16 из 4003) сравнивали
//   открыто (`Uidx == Net->SelfUserNum`) в 12 местах мира, в навыках и в сетевых обработчиках. Ноль —
//   ЗАКОННЫЙ номер (сервер выдаёт его первому вошедшему), поэтому до пакета 4003 такое сравнение считало
//   «своим» чужого игрока с номером 0. Признак «номер известен» (`bSelfUserNumKnown`) заведён 24.08.2026,
//   но открытые сравнения его не видели.
//
//   ЧТО ДЕЛАЕТ ОРИГИНАЛ. Своего номера как отдельной переменной для сравнения у него нет: сущность ищется
//   по её СОБСТВЕННОМУ полю `+190188` (`CharManager_FindByUserIdx@0x9D1240`, entity_char.c:6272-6283), а
//   «это локальный игрок» — признак самой сущности (`+190260`). До появления своей сущности
//   (`g_LocalPlayer = 0` при сбросе, game_main.c:3347) «своим» не считается никто. Строгая форма
//   (`номер известен && совпал`) воспроизводит именно это.
//
//   ★ НЕ ПУТАТЬ ПРОСТРАНСТВА (закон `charid-vs-usernum`): userIdx — i16 слот сессии, charId — i32 номер
//   персонажа (`SelectedCharId`).
//
//   КЛЮЧ ОТКАТА: `asda.Self.StrictUnknownNum` 1 = до 4003 «я» не совпадает ни с кем (умолчание);
//     0 = как до 14.09.2026 — открытое сравнение с `SelfUserNum` (0 до 4003); `GetUserIdxOr` отдаёт сырой номер. Ключ действует ТОЛЬКО на
//     места, переведённые 14.09 на `AsdaSelf::IsSelfUserIdx`; метод `UAsdaNetSubsystem::IsSelfUserIdx`
//     был строгим и раньше и от ключа не зависит.
//     ★ 11.10.2026 (дедуп №2, В1-B, U5a): все вызовы метода из окон (`Private/UI`, 31 место в 16 подсистемах) переведены на
//     `AsdaSelf::IsSelfUserIdx` — при ключе 1 результат тот же (строгая форма), ключ 0 теперь действует и на них. Метод
//     остаётся для сети/мира (`AsdaNetSubsystem`, `AsdaWorldSubsystem`, NPC) и самотестов; новый код зовёт только `AsdaSelf`.
#pragma once
#include "CoreMinimal.h"

class UAsdaNetSubsystem;
class UGameInstance;
struct FAsdaCharRecord;

namespace AsdaSelf
{
	// ★ «Этот userIdx — мой?» (i16-пространство сессии). До 4003 — false (см. ключ отката в шапке).
	ASDAREBORN_API bool IsSelfUserIdx(const UAsdaNetSubsystem* Net, int32 UserIdx);
	// То же от GameInstance — для UI-подсистем.
	ASDAREBORN_API bool IsSelfUserIdx(const UGameInstance* GameInstance, int32 UserIdx);

	// Получен ли уже свой userIdx пакетом 4003.
	ASDAREBORN_API bool IsUserIdxKnown(const UAsdaNetSubsystem* Net);

	// Свой userIdx, либо `Fallback`, пока 4003 не пришёл (для ключей реестров и вызовов «по номеру»).
	//   При `asda.Self.StrictUnknownNum 0` — как до 14.09.2026: сырой `SelfUserNum` (0 до 4003), `Fallback` не используется.
	ASDAREBORN_API int32 GetUserIdxOr(const UAsdaNetSubsystem* Net, int32 Fallback);
	// То же от GameInstance — для UI-подсистем.
	ASDAREBORN_API int32 GetUserIdxOr(const UGameInstance* GameInstance, int32 Fallback);

	// charId выбранного персонажа (i32, из ответа 1017); 0 — персонаж ещё не выбран.
	ASDAREBORN_API int32 GetCharId(const UAsdaNetSubsystem* Net);

	// Запись ВЫБРАННОГО персонажа в списке выбора (по сырому слоту последнего 1016), nullptr — нет такой.
	ASDAREBORN_API const FAsdaCharRecord* FindSelectedCharRecord(const UAsdaNetSubsystem* Net);

	// Уровень своего персонажа: живой `SelfLevel` (4005-self / 4036), до него — уровень ВЫБРАННОГО
	//   персонажа из списка выбора (то же правило, что у своей плашки, `AsdaNameplateComponent.cpp:183`);
	//   0 — неизвестен.
	ASDAREBORN_API int32 GetLevel(const UAsdaNetSubsystem* Net);
	// То же от GameInstance — для UI-подсистем.
	ASDAREBORN_API int32 GetLevel(const UGameInstance* GameInstance);

	// ★★★ Дедуп №2, В2-A (U5d, 11.10.2026): КЛАСС своего персонажа — байт `CCharacter+0x438` (`gap_438[0]`, в порту
	//   `SelfClassId`): его читают `hudRefreshClassAndElementIcons@0xE79850` (ui_core.c:75387-75404), строка «Профессия» окна
	//   характеристик, фильтры магазинов и тултипа. Писатели эталона — `charApplyFullStatusPacket@0xD83670` (entity_char.c:46368)
	//   и `net_ApplyPlayerFullUpdate@0xD828A0` (network.c:21840); до них — запись ВЫБРАННОГО персонажа char-select
	//   `0x3EE rec+24` (`FAsdaCharRecord::Job`). ⚠ НЕ `SelfJob` — это ПРОФЕССИЯ `+0x445`, другой байт (гейт глобального чата).
	//   До 11.10.2026 фолбэков было пять: `SelfJob` (характеристики, тултип, окно навыков), запись char-select (мини-статус),
	//   сырое −1 (магазин снаряжения, окно покупки, звания), 0 (панель цели). −1 — неизвестен.
	ASDAREBORN_API int32 GetClassId(const UAsdaNetSubsystem* Net);
	// То же от GameInstance — для UI-подсистем.
	ASDAREBORN_API int32 GetClassId(const UGameInstance* GameInstance);

	// ★★★ 26.10.2026 — ПОДКЛАСС своего персонажа: байт `CCharacter+0x439` (`gap_438[1]`, в порту `SelfSubJob`); до него — запись
	//   ВЫБРАННОГО персонажа char-select `0x3FF rec+25` (`FAsdaCharRecord::SubClass`). Писатели эталона — те же, что у класса
	//   (`charApplyFullStatusPacket` body+19, `net_ApplyPlayerFullUpdate` +60376). −1 — неизвестен.
	ASDAREBORN_API int32 GetSubJob(const UAsdaNetSubsystem* Net);
	// То же от GameInstance — для UI-подсистем.
	ASDAREBORN_API int32 GetSubJob(const UGameInstance* GameInstance);

	// ★★★ 26.10.2026 — СТУПЕНЬ 4-Й ПРОФЕССИИ (разбор `Reports/class_34_39/RESEARCH.md` §6, сервер `sub_461010` GameServer.c:39238).
	//   [ОРИГИНАЛ] Сервер при смене на 4-ю профессию пишет новую ступень в ПОДКЛАСС (CUser+0x191), а основной класс оставляет
	//   3/14/25; эталон же решает «4-я профессия или нет» по ОСНОВНОМУ классу (`skillwin_OpenSkillWindow` — кнопка 12400,
	//   `uiTradeDialogInitFromItemType` — флажок 16521, `uiItemUpgradeWnd_OnCommand` — ветка 10/11/12). Честно сменивший
	//   профессию не видел своей вкладки. Сервер сам опознаёт 4-ю профессию по подклассу (`sub_481CE0`: `switch (SubJob)`).
	//   [ИСПРАВЛЕНИЕ] Класс для предикатов 4-й профессии = подкласс, если он ≠ 0, — боевой класс 4-й ступени (`IsJob4Class`)
	//   ТОЙ ЖЕ полосы, что основной класс; иначе основной класс (ремесленный подкласс 34…39 боевую вкладку не открывает: его
	//   полоса 0, а дерево строится по полосе основного класса). Ключ `asda.Job4.StageFromSubJob` (1; 0 — как эталон: основной класс).
	ASDAREBORN_API int32 Job4StageClass(int32 ClassId, int32 SubJob);
	// То же по своим полям (`GetClassId` + `GetSubJob`).
	ASDAREBORN_API int32 GetJob4StageClass(const UAsdaNetSubsystem* Net);
	// То же от GameInstance — для UI-подсистем.
	ASDAREBORN_API int32 GetJob4StageClass(const UGameInstance* GameInstance);
	// Включён ли ключ `asda.Job4.StageFromSubJob` (в Shipping — всегда 1).
	ASDAREBORN_API bool IsJob4StageFromSubJobOn();
	// [ОРИГИНАЛ] Предикат «класс 4-й профессии» эталона: {4..7, 15..18, 26..29, 34..39} (`skill_buff.c:32759`, `ui_core.c:59010`).
	ASDAREBORN_API bool IsJob4Class(int32 ClassId);
	// [ОРИГИНАЛ] Ветка 4-й профессии класса (`uiItemUpgradeWnd_OnCommand`, ui_core.c:58444-58449): 10 / 11 / 12; −1 — нет.
	ASDAREBORN_API int32 Job4GradeOfClass(int32 ClassId);
	// [ОРИГИНАЛ] Полоса класса `itemGradeBandFromTypeIndex@0xEC97A0`: 1..11 → 1, 12..22 → 2, 23..33 → 3, прочие → 0.
	ASDAREBORN_API int32 BandOfClass(int32 ClassId);
	// Ремесленный подкласс 34…39 (невыпущенная функция оригинала: навыков, деревьев и ветки 16500 нет).
	ASDAREBORN_API bool IsCraftClass(int32 ClassId);

	// ★★★ Дедуп №2, В2-A (N2, 11.10.2026): «этот charId (i32) — мой?». Своего charId (`SelectedCharId`, ответ 1017) может ещё
	//   не быть (0): что тогда отвечать — решает ВЫЗЫВАЮЩИЙ, явно. До 11.10.2026 политик было три без имени: «принять всё»
	//   (питомцы — самопроверка гоняет приёмники без сессии), «отвергнуть» (мини-статус) и «сравнить 0 == 0» (звания ×6, поиск
	//   отряда ×2, рулетка, война за город, кольцо соулмейта у соседа) — последняя считала «своим» пакет с нулём в поле charId.
	//   Эталон сравнивает поле тела со своим charId открыто (`cmp [eax+2], [ecx+0x2D4]`, поиск отряда 0xDBFA12), но пакетов до
	//   выбора персонажа не получает — «до 1017» есть только у порта (сброс сессии, самотесты).
	//   Ключ отката `asda.Self.StrictUnknownNum 0` действует и здесь: открытое сравнение `CharId == SelectedCharId` (при `Reject`).
	enum class EAsdaUnknownSelf : uint8
	{
		Reject,      // свой charId неизвестен (0) — «не мой» ни один (умолчание)
		AcceptAll    // свой charId неизвестен (0) — «мой» любой (питомцы: самопроверка без сессии)
	};
	ASDAREBORN_API bool IsSelfCharId(const UAsdaNetSubsystem* Net, int32 CharId, EAsdaUnknownSelf WhenUnknown = EAsdaUnknownSelf::Reject);
	// То же от GameInstance — для UI-подсистем.
	ASDAREBORN_API bool IsSelfCharId(const UGameInstance* GameInstance, int32 CharId, EAsdaUnknownSelf WhenUnknown = EAsdaUnknownSelf::Reject);
}
