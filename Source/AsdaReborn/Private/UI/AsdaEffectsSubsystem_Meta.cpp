// Copyright (c) 2026 @sitxovski (Discord)
// AsdaEffectsSubsystem_Meta.cpp — ★ R3-c 12.11.2026 (разрез AsdaEffectsSubsystem.cpp, дословный перенос): справочники: мета навыков, сведения о состояниях, подсказка, расписание событий, отмена бафа.
//   Общее для частей — в AsdaEffectsSubsystem_Private.h; шапка и реверс — в AsdaEffectsSubsystem.cpp.
#include "UI/AsdaEffectsSubsystem.h"
#include "AsdaDataTableCache.h"   // ★ Аудит №4 (B2): AsdaDt::Cached — одна ленивая загрузка DataTable на порт

#include "AsdaNetSubsystem.h"
#include "AsdaBytes.h"                      // ★ Аудит №3 (A5): единые читалки сырой записи вместо своей
#include "Items/AsdaItemDbRows.h"
#include "Items/AsdaItemTableSubsystem.h"   // ★ Аудит №3 (A3/A4): таблицы — только через менеджер
#include "Resources/AsdaSoundSubsystem.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/DataTable.h"
#include "HAL/PlatformTime.h"
#include "HAL/IConsoleManager.h"
#include "AsdaEffectsSubsystem_Private.h"   // ★ R3-c 12.11.2026: общее для частей AsdaEffectsSubsystem*.cpp

// ★ Аудит №3 (A3): менеджер таблиц предметов — ЕДИНСТВЕННЫЙ держатель таблиц item-БД.
static UAsdaItemTableSubsystem* AsdaItemsOf(const UGameInstance* GI)
{
	return GI ? const_cast<UGameInstance*>(GI)->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
}

// ═════════════════════════════ справочники ═════════════════════════════════════════════════════
void UAsdaEffectsSubsystem::EnsureSkillMetaLoaded() const
{
	if (bSkillMetaLoaded) { return; }
	bSkillMetaLoaded = true;
	// ★ Аудит №3 (A3): `skill_info` есть в манифесте item-БД (AsdaItemDbManifest.h) — берём УЖЕ
	//   ЗАГРУЖЕННУЮ таблицу у менеджера, второй `LoadObject` (вторая копия 4000+ строк) снят.
	const UDataTable* DT = nullptr;
	if (UAsdaItemTableSubsystem* IT = AsdaItemsOf(GetGameInstance()))
	{
		DT = IT->GetDataTable(FName(TEXT("skill_info")));
	}
	if (!DT)
	{
		UE_LOG(LogAsdaBuff, Warning, TEXT("[эффекты] DT_skill_info не загрузился — цвет таймера/стека и 5307 будут по умолчанию"));
		return;
	}
	for (const TPair<FName, uint8*>& It : DT->GetRowMap())
	{
		const FAsdaSkillInfoRow* R = reinterpret_cast<const FAsdaSkillInfoRow*>(It.Value);
		if (!R) { continue; }
		FAsdaSkillEffectMeta M;
		// ★ Аудит №3 (A5): общая читалка. −1 («байта нет») ни одну ветку не переключает:
		//   `SkillSubType != 5` — истинно и при 0, и при −1; `Buff0SubType == 2 / >= 2` — ложно при обоих;
		//   `Buff0DamageType == Mode` — байт режима из пакета 5307 неотрицателен, поэтому −1 не совпадёт
		//   НИ С ЧЕМ (прежний тихий 0 при битой записи ложно совпадал с режимом 0 — это дефект, а не поведение).
		M.SkillSubType    = AsdaBytes::HexByteAt(R->DataHex, 44);   // .sif +0x2C
		M.Buff0DamageType = AsdaBytes::HexByteAt(R->DataHex, 70);   // .sif +0x46
		M.Buff0SubType    = AsdaBytes::HexByteAt(R->DataHex, 71);   // .sif +0x47
		// ★ 26.10.2026 (bowfx): слоты цепочки состояний и грейд (`skillrecord_fields.tsv`): buff0/1/2 SkillId +0x48/+0x59/+0x6A (i16),
		//   Duration +0x4E/+0x5F/+0x70 (i32), skillGradeNum +0xBE (u8). Битая запись ⇒ слот −1 / длительность 0 (слота нет).
		{
			static const int32 kStateOff[3] = { 0x48, 0x59, 0x6A };
			static const int32 kDurOff[3]   = { 0x4E, 0x5F, 0x70 };
			for (int32 k = 0; k < 3; ++k)
			{
				const int32 B0 = AsdaBytes::HexByteAt(R->DataHex, kStateOff[k]), B1 = AsdaBytes::HexByteAt(R->DataHex, kStateOff[k] + 1);
				M.BuffStateId[k]    = (B0 < 0 || B1 < 0) ? -1 : (int32)(int16)(uint16)(B0 | (B1 << 8));
				M.BuffDurationMs[k] = AsdaBytes::HexI32At(R->DataHex, kDurOff[k]);
			}
			M.GradeNum = FMath::Max(0, AsdaBytes::HexByteAt(R->DataHex, 0xBE));
		}
		M.bValid = true;
		SkillMeta.Add(R->SkillCode, M);
	}
	UE_LOG(LogAsdaBuff, Log, TEXT("[эффекты] метаданные скиллов из DT_skill_info: %d записей"), SkillMeta.Num());
}

const FAsdaSkillEffectMeta& UAsdaEffectsSubsystem::GetSkillMeta(int32 SkillId) const
{
	EnsureSkillMetaLoaded();
	if (const FAsdaSkillEffectMeta* M = SkillMeta.Find(SkillId)) { return *M; }
	return EmptyMeta;
}

// stateinfotip: Id +0, Name +4, Icon +36 (0x24), IconAtlasIndex +68 (0x44), Text +72 (0x48).
//   Ровно те поля, что эталон читает как `rec+36`/`rec+68` (иконка) и `rec+4`/`rec+72` (тултип).
//   ★ Аудит №3 (A3): БЫЛО — свой `LoadObject` + свой кэш `StateInfoCache` (вторая копия имён/иконок/
//     текстов всех 349 строк). СТАЛО — типизированный геттер менеджера: одна загрузка, один индекс,
//     одна копия строк. Резолв по `Id` возвращает ту же строку, что прежняя карта (Id уникален).
bool UAsdaEffectsSubsystem::GetStateInfo(int32 Id, FString& OutName, FString& OutIcon, int32& OutCell, FString& OutText) const
{
	UAsdaItemTableSubsystem* IT = AsdaItemsOf(GetGameInstance());
	FAsdaStateinfotipRow Row;
	if (!IT || !IT->GetStateInfoTip(Id, Row)) { return false; }
	OutName = Row.Name; OutIcon = Row.Icon; OutCell = Row.IconAtlasIndex; OutText = Row.Text;
	return !OutIcon.IsEmpty();
}

// soulstateinfotip — та же раскладка (эталон partyFindMemberSlotByIndex → +36/+68/+4/+72).
//   ★ Аудит №3 (A3): свой `LoadObject` + `SoulStateCache` сняты — см. комментарий у `GetStateInfo`.
bool UAsdaEffectsSubsystem::GetSoulStateInfo(int32 Id, FString& OutName, FString& OutIcon, int32& OutCell, FString& OutText) const
{
	UAsdaItemTableSubsystem* IT = AsdaItemsOf(GetGameInstance());
	FAsdaSoulstateinfotipRow Row;
	if (!IT || !IT->GetSoulStateInfoTip(Id, Row)) { return false; }
	OutName = Row.Name; OutIcon = Row.Icon; OutCell = Row.IconAtlasIndex; OutText = Row.Text;
	return !OutIcon.IsEmpty();
}

// special_itemlist («table2»): subType @80, flag_116 @116, ИКОНКА БАФФА = строка @197 + ячейка @229.
//   ★★★ Аудит №3 (A3 + A4): БЫЛО ТРИ независимых пути к одной таблице — свой `LoadObject`, свой
//     ЛИНЕЙНЫЙ кэш `Table2Cache` и свой hex-разбор колонок. СТАЛО: единственный держатель таблицы —
//     `UAsdaItemTableSubsystem`, резолв по КЛЮЧЕВОМУ индексу (`GetSpecialItem`, эталон
//     `CItemManager_GetTable2Record@0x9C43A0`).
//   ⚠ РАСХОЖДЕНИЕ С ФОРМУЛИРОВКОЙ АУДИТА, проверено по самим данным (558 строк CSV):
//     колонок для смещений 116 и 229 в строке НЕТ и быть не может — это «дырки» раскладки
//     (116 = 0x74 между Price@0x70 и LinkGroupId@0x75; 229 = 0xE5 между строкой Field197@0xC5..0xE4 и
//     ModelEntry@0xE6). Поле `IconCell` — это БАЙТ 196 (0xC4), пара к другой иконке (`IconPath`@0xA4):
//     у 554 записей из 558 он НЕ равен байту 229. Поэтому hex-путь для 116/229 ОСТАВЛЕН — но уже через
//     общую точку `AsdaBytes` (A5), а не через свою читалку.
//     Сверка базы: DataHex — запись 1:1 от нуля (i32@0 == Code), SubType-колонка == i32@0x50 у всех строк;
//     байт 116 принимает только 0 (202 строки) и 2 (356 строк) — совпадает с прежним наблюдением.
bool UAsdaEffectsSubsystem::GetTable2(int32 ItemCode, int32& OutSubType, int32& OutFlag116,
                                      FString& OutBuffIcon, int32& OutBuffCell) const
{
	UAsdaItemTableSubsystem* IT = AsdaItemsOf(GetGameInstance());
	FAsdaSpecialItemlistRow Row;
	if (!IT || !IT->GetSpecialItem(ItemCode, Row)) { return false; }
	OutSubType  = Row.SubType;
	// ⚠ −1 («байта нет») здесь безопасно: единственный потребитель сравнивает `Flag116 != 2` и без
	//   флага 2 иконку не рисует вовсе — то есть битая запись уходит в ту же ветку, что и прежний 0.
	OutFlag116  = AsdaBytes::HexByteAt(Row.DataHex, 116);
	OutBuffIcon = Row.Field197;                                   // строка @197 (колонка есть)
	// Ячейка атласа читается ТОЛЬКО когда флаг == 2, то есть запись заведомо целая; −1 сюда не дойдёт.
	OutBuffCell = AsdaBytes::HexByteAt(Row.DataHex, 229);
	return true;
}

// specialitem_tooltiplist: Id +0, Name +4, Tooltip +36 — источник тултипа предметного эффекта
//   (эталон ItemTooltip_BuildTable2Info@0xEB44C0: `recordPtr->name` + `recordPtr->gap_24`).
//   ★ Аудит №3 (A3): свой `LoadObject` + свой кэш `ItemTipCache` сняты. У менеджера эта таблица уже
//     разобрана ПОЗИЦИОННО — ровно как в эталоне `itemGetTooltipById@0xEC73B0`
//     (`base + 236*(code − FirstId)`), поэтому берём его геттер, а не «поиск по ключу».
//   ⚠ Сверка данных перед заменой: id таблицы сплошные 1..558 без дублей, а `_row = id − 1`, то есть
//     позиционный резолв эталона и прежний поиск по карте дают ОДНУ И ТУ ЖЕ строку.
bool UAsdaEffectsSubsystem::GetItemTooltipText(int32 ItemCode, FString& OutName, FString& OutText) const
{
	UAsdaItemTableSubsystem* IT = AsdaItemsOf(GetGameInstance());
	FAsdaSpecialitemTooltiplistRow Row;
	if (!IT || !IT->GetSpecialItemTooltip(ItemCode, Row)) { return false; }
	OutName = Row.Name; OutText = Row.Tooltip;
	return true;
}

// ═════════════════════════════ ивент-бонусы ════════════════════════════════════════════════════
// Эталон SecessionSchedule_LoadFromTex@0xE81800 (ui_core.c:77793):
//   fopen("<CurrentDir>\System\Texture\act_secession3.TEX", "rt");
//   while (fscanf("%4d %2d %2d %2d %2d %f", &год,&месяц,&день,&часНачала,&часКонца,&бонус), год != 0) push;
//
// ★★★ 2026-08-26: ФАЙЛ БОЛЬШЕ НЕ ЧИТАЕТСЯ С ДИСКА. Расписание живёт в DataTable
//   `DT_EventSchedule` (импортёр `Tools/asda_import_event_schedule.py`), а исходный текстовый
//   файл перенесён из `Content/AsdaAssets/Data/` в `RawAssets/Data/`.
//   ⚠ ПОРЯДОК СТРОК ЗНАЧИМ: эталон обходит массив сверху вниз и берёт ПЕРВОЕ совпадение по
//     месяцу/дню/часу, а в исходнике одна дата встречается многократно. Поэтому ключ строки
//     таблицы — порядковый номер строки файла, и здесь мы раскладываем строки ПО КЛЮЧУ,
//     а не по порядку обхода `GetRowMap` (у `TMap` он не гарантирован).
//   ⚠ Обрыва «год == 0» здесь нет и быть не может: его применил ИМПОРТЁР — в таблицу попали
//     только строки ДО первого нулевого года, ровно как их видел бы эталон.
void UAsdaEffectsSubsystem::LoadEventSchedule()
{
	if (EventSchedule.Num() > 0)
	{
		UE_LOG(LogAsdaBuff, Log, TEXT("[ивент] расписание задано в редакторе (%d строк) — таблица не читается"), EventSchedule.Num());
		return;
	}
	const UDataTable* Dt = AsdaDt::Cached(*EventScheduleTable);
	if (!Dt)
	{
		UE_LOG(LogAsdaBuff, Warning,
			TEXT("[ивент] нет таблицы расписания %s (иконки EXP/Drop будут работать по множителю из пакета 4301)"),
			*EventScheduleTable);
		return;
	}
	const TMap<FName, uint8*>& Map = Dt->GetRowMap();
	EventSchedule.SetNum(Map.Num());
	int32 Bad = 0;
	for (const TPair<FName, uint8*>& It : Map)
	{
		const FAsdaEventScheduleRow* R = reinterpret_cast<const FAsdaEventScheduleRow*>(It.Value);
		if (!R) { continue; }
		const int32 Idx = FCString::Atoi(*It.Key.ToString());   // ключ = порядковый номер строки файла
		if (EventSchedule.IsValidIndex(Idx)) { EventSchedule[Idx] = *R; }
		else                                 { ++Bad; }
	}
	if (Bad > 0)
	{
		UE_LOG(LogAsdaBuff, Error,
			TEXT("[ивент] DT_EventSchedule: %d строк с ключом вне 0…%d — расписание неполное"), Bad, EventSchedule.Num() - 1);
	}
	UE_LOG(LogAsdaBuff, Log, TEXT("[ивент] расписание act_secession3: %d строк из %s"), EventSchedule.Num(), *EventScheduleTable);
}

// Эталон uiSetScheduledClock@0xE82DB0 (ui_core.c:78007) 1:1.
void UAsdaEffectsSubsystem::ApplyEventBonusPacket(const FAsdaEventBonusPkt& P)
{
	EventType     = P.EventType;                                  // hud[704]
	ClockMonth    = P.Month;                                      // hud+2833
	ClockDay      = P.Day;                                        // hud+2834
	ClockHour     = P.Hour;                                       // hud+2835
	ClockMinute   = P.Minute;                                     // hud+2836
	EventRemainMin = 60 * (P.EndHour - P.Hour) - P.Minute;        // hud[705]
	EventRate     = P.BonusRate + 1.f;                            // hud[707]
	// Поля пакета, которые эталонный клиент не читает — только для дампа.
	PktSecond = P.Second; PktSchedYear = P.SchedYear; PktSchedMonth = P.SchedMonth;
	PktSchedDay = P.SchedDay; PktSchedStartHour = P.SchedStartHour;
	PktEndHour = P.EndHour;  PktBonus = P.BonusRate;
	if (EventRemainMin >= 0) { bEventActive = true; }             // hud[706]
	else                     { EventRemainMin = 0; bEventActive = false; }
	if (!bEventActive)
	{
		// ★ ревизия 49 С-2: 4301 «часа нет» гасит значок часа (слоты события 6039 не трогаются)
		bHourExpOn = false; HourExpMult = 1.f; bHourDropOn = false; HourDropMult = 1.f;
		RecombineEventBonus();
	}
	UE_LOG(LogAsdaBuff, Log,
		TEXT("[ивент] 4301 применён: тип=%d активен=%d осталось=%d мин множитель=%.2f (часы клиента %02d.%02d %02d:%02d)"),
		EventType, bEventActive ? 1 : 0, EventRemainMin, EventRate, ClockDay, ClockMonth, ClockHour, ClockMinute);
	if (EventType > 0) { UpdateScheduledClock(-1); }              // эталон: сразу перерисовать
	OnEventBonusChanged.Broadcast();
}

void UAsdaEffectsSubsystem::OnEventBonusPkt(const FAsdaEventBonusPkt& P)
{
	ApplyEventBonusPacket(P);
}

// Эталон gameUi_updateScheduledClockDisplay@0xE819D0 (game_main.c:11423).
void UAsdaEffectsSubsystem::UpdateScheduledClock(int32 Mode)
{
	if (!bEventActive)
	{
		// Эталонные «ручные» ветки таймера окна 0x67: 1 = показать контейнер + Drop_Buff2, 9 = скрыть.
		if (Mode == 1 || Mode == 9) { OnEventBonusChanged.Broadcast(); }
		return;
	}
	// Внутренние «часы» клиента: минута++ на каждый вызов, перенос в часы и сутки.
	if (++ClockMinute >= 59)
	{
		ClockMinute = 0;
		if (++ClockHour >= 24) { ClockHour = 0; ++ClockDay; }
	}
	bScheduleHit = false;
	for (const FAsdaEventScheduleRow& R : EventSchedule)
	{
		if (ClockMonth != R.Month) { continue; }
		if (ClockDay   != R.Day)   { continue; }
		if (ClockHour < R.StartHour) { continue; }
		if (ClockHour > R.EndHour - 1) { continue; }
		if (ClockMinute > 59) { continue; }
		EventRate = R.Bonus + 1.f;
		bScheduleHit = true;
		break;
	}
	if (!bScheduleHit && bEventFallbackToPacketRate)
	{
		// ★ ОТСТУПЛЕНИЕ, УТВЕРЖДЕНО ЮЗЕРОМ (fix-original-defects): расписание в act_secession3
		//   датировано 2011 годом, поэтому совпадений не бывает и оригинал НИЧЕГО не показывает даже
		//   при активном событии. Считаем это дефектом данных и берём множитель из пакета 4301.
		bScheduleHit = true;
	}
	// ★ 21.11.2026 [ИСПРАВЛЕНИЕ] (ревизия 49 С-2): час кончается по времени. У эталона `hud[706]` часами не снимался, а при запасном
	//   пути (`bEventFallbackToPacketRate`) совпадение всегда есть — значок часа горел до перезахода и после конца часа. Остаток минут
	//   из 4301 (60·(EndHour − час) − минута) убывает на каждый шаг часов (шаг = минута); дошёл до нуля — час кончился.
	if (EventRemainMin > 0) { --EventRemainMin; }
	if (EventRemainMin <= 0)
	{
		bEventActive = false;
		bScheduleHit = false;
		bHourExpOn = false; HourExpMult = 1.f; bHourDropOn = false; HourDropMult = 1.f;
		UE_LOG(LogAsdaBuff, Log, TEXT("[ивент] счастливый час кончился по часам клиента — значок часа погашен"));
		RecombineEventBonus();
		OnEventBonusChanged.Broadcast();
		return;
	}
	// Разложить состояние 4301 по двум слотам множителей ЧАСА (вид: 1 опыт, 2 дроп, 3 оба); событие 6039 — свои слоты (ревизия 49 С-2).
	if (bScheduleHit)
	{
		if (EventType == 1 || EventType == 3) { bHourExpOn  = true; HourExpMult  = EventRate; }
		if (EventType == 2 || EventType == 3) { bHourDropOn = true; HourDropMult = EventRate; }
	}
	else
	{
		// Расписание сказало «сейчас события нет» — гасим ТОЛЬКО то, что зажигал сам 4301;
		//   рантайм-ивенты из 6039 живут своей жизнью и здесь не трогаются.
		if (EventType == 1 || EventType == 3) { bHourExpOn  = false; HourExpMult  = 1.f; }
		if (EventType == 2 || EventType == 3) { bHourDropOn = false; HourDropMult = 1.f; }
	}
	RecombineEventBonus();
	OnEventBonusChanged.Broadcast();
}

// ★ 21.11.2026 (ревизия 49 С-2, P128): итог значка. Событие Event.dat (6039) важнее часа: сервер P128 уже кладёт в него «событие +
//   час» (ровно рейт GetExpUpDownValue); без события — значок часа (4301). Добыча — так же (4301 вид 2/3 сток не ставит).
void UAsdaEffectsSubsystem::RecombineEventBonus()
{
	bExpEventOn    = bSetExpOn || bHourExpOn;
	ExpMultiplier  = bSetExpOn ? SetExpMult : bHourExpOn ? HourExpMult : 1.f;
	bDropEventOn   = bSetDropOn || bHourDropOn;
	DropMultiplier = bSetDropOn ? SetDropMult : bHourDropOn ? HourDropMult : 1.f;
}

// Индексы иконок: контейнер EXP_Buff (5100) + один из 7 EXP_Buff* + один из 3 «Drop_Buff*».
//   Эталон: expIdx = (int)(rate/0.5 − 2), dropIdx = (int)(rate − 2), оба клампятся к >= 0;
//   первый показывается при eventType 1 или 3, второй — при 2 или 3.
// ★★★ ЧТО НА САМОМ ДЕЛЕ НА ИКОНКАХ (проверено вырезкой из атласов, а не по именам контролов):
//   EXP_Buff05/15/2/25/3/35/4  = красный значок «EXP ×0.5 / ×1.5 / ×2 / ×2.5 / ×3 / ×3.5 / ×4»;
//   Drop_Buff2/3/4             = **СТОПКА ЗОЛОТЫХ МОНЕТ** «×2 / ×3 / ×4» ⇒ это ЗОЛОТО, а не
//                                выпадение предметов. Значка «дроп предметов» в игре НЕТ ВООБЩЕ
//                                (ни контрола, ни арта — ссылок в срезе нет).
//   Дробных значений для золота (×0.5/×1.5/×2.5/×3.5) в арте тоже НЕТ — только ×2/×3/×4.
//   Соответствие «множитель → индекс»: expIdx k ⇒ rate = (k+2)·0.5, то есть
//   k=1 ×1.5, k=2 ×2, k=3 ×2.5, k=4 ×3, k=5 ×3.5, k=6 ×4 — арт совпадает с числом ТОЧНО;
//   k=0 соответствует rate 1.0 (бонуса нет), а арт подписан «×0.5» — расхождение самих ДАННЫХ
//   оригинала (недостижимо: при нулевом бонусе сервер не помечает событие активным).
//   dropIdx k ⇒ rate = k+2 (×2/×3/×4); дробный rate округляется ВНИЗ (2.5 → значок ×2) — так в эталоне.
void UAsdaEffectsSubsystem::GetEventIconIndices(bool& bOutContainer, int32& OutExpIdx, int32& OutDropIdx) const
{
	bOutContainer = false; OutExpIdx = -1; OutDropIdx = -1;
	// ★ Решение юзера: при множителе ×1 значок не показываем вовсе.
	const float MinShow = bHideEventIconAtX1 ? 1.0001f : 0.f;
	if (bExpEventOn && ExpMultiplier > MinShow)
	{
		OutExpIdx = FMath::Clamp((int32)(ExpMultiplier / 0.5f - 2.f), 0, 6);
		bOutContainer = true;
	}
	if (bDropEventOn && DropMultiplier > MinShow)
	{
		OutDropIdx = FMath::Clamp((int32)(DropMultiplier - 2.f), 0, 2);
		bOutContainer = true;
	}
}

// ★★★ 6039/6041 — НАСТОЯЩИЙ рантайм-источник ивент-множителей (BattleServer/NpcServer).
//   Type 0 = ДРОП (шанс выпадения предметов; вместе с ним больше падает и золота — поэтому
//   на значке `Drop_Buff*` нарисованы монеты), Type 1 = ОПЫТ. Множитель приходит готовым float
//   (сервер делит `*_Value` в процентах на 100: 200 → ×2, 150 → ×1.5).
// ★ ОТСТУПЛЕНИЕ (fix-original-defects): клиент-эталон эти пакеты ПРИНИМАЕТ, но только печатает
//   GM-строку в чат и значки не зажигает — из-за чего значок ДРОПА в игре не появлялся никогда,
//   хотя сама механика на сервере работает. Мы используем те же данные по эталонной раскладке.
void UAsdaEffectsSubsystem::ApplyEventSet(bool bOk, int32 Type, float Multiplier, bool bOn)
{
	if (!bOk) { return; }                       // отказ GM-команды — состояние не меняем
	// ★ ревизия 49 С-2: событие — свои слоты (часы 4301 их не перетирают, 6041 не гасит значок часа)
	if (Type == 0)
	{
		bSetDropOn  = bOn;
		SetDropMult = bOn ? Multiplier : 1.f;
	}
	else if (Type == 1)
	{
		bSetExpOn  = bOn;
		SetExpMult = bOn ? Multiplier : 1.f;
	}
	else { return; }
	RecombineEventBonus();
	UE_LOG(LogAsdaBuff, Log, TEXT("[ивент] %s: вид=%s множитель=×%.2f → опыт ×%.2f (вкл=%d), дроп ×%.2f (вкл=%d)"),
		bOn ? TEXT("6039 SET") : TEXT("6041 OFF"), Type == 0 ? TEXT("ДРОП") : TEXT("ОПЫТ"), Multiplier,
		ExpMultiplier, bExpEventOn ? 1 : 0, DropMultiplier, bDropEventOn ? 1 : 0);
	OnEventBonusChanged.Broadcast();
}

// ═════════════════════════════ отмена баффа (6142) ═════════════════════════════════════════════
// Эталон HUD_PlayerBuffIcon_OnClick case 11 (ui_core.c:76860-76880):
//   гейт `hud->gap_22C[0] != 0`; typeFlag 0 → clearType 0; typeFlag 1 → НЕ шлём; typeFlag 2 → clearType 1;
//   значение id берётся из ЯЧЕЙКИ СЛОТА (`gap_23C[16*slot+20]`) — для type 2 там лежит itemCode.
bool UAsdaEffectsSubsystem::RequestCancel(int32 SlotTypeFlag, int32 SlotId)
{
	if (!bBuffCancelAllowed)
	{
		UE_LOG(LogAsdaBuff, Log, TEXT("[эффекты] отмена баффа запрещена флагом (эталон hud->gap_22C[0] == 0)"));
		return false;
	}
	int32 ClearType = -1;
	if (SlotTypeFlag == 0)      { ClearType = 0; }
	else if (SlotTypeFlag == 2) { ClearType = 1; }
	else
	{
		UE_LOG(LogAsdaBuff, Log, TEXT("[эффекты] соулмейт-эффект (typeFlag 1) снять НЕЛЬЗЯ — эталон оставляет clearType = -1"));
		return false;
	}
	UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net) { return false; }
	return Net->SendBuffCancel(ClearType, SlotId);
}
