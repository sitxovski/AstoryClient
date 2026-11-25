// Copyright (c) 2026 @sitxovski (Discord)
// AsdaHotbarSubsystem_Slots.cpp — ★ R2 10.11.2026 (разрез AsdaHotbarSubsystem.cpp, дословный перенос): эмоции, иконки, перестройка слота, шторка отката, стиль стака, страницы.
//   Общее для частей — в AsdaHotbarSubsystemInternal.h; шапка и реверс — в AsdaHotbarSubsystem.cpp.
#include "UI/AsdaHotbarSubsystem.h"
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "UI/AsdaSoulmateSubsystem.h"    // ★ 06.11.2026: пара и «Друг в игре» — из карточки (`asda.Soul.SlotTruth`)
#include "UI/AsdaCharaInfoSubsystem.h"   // ★ 06.11.2026: перерисовка лотков действий при смене пары
#include "UI/AsdaNoticeColors.h"   // ★ U12 (В1 дедупа №2): эталонные цвета нотисов одной точкой
#include "UI/AsdaItemCountStyle.h"   // ★ 26.10.2026: обводка числа стопки — общая точка (ключ asda.UI.ItemCountOutline)
#include "Resources/AsdaAssetPaths.h"   // ★ волна 2 п. 7 (14.09.2026): реестр путей и правил имён ассетов (генерируемый)
#include "UI/AsdaNotice.h"   // ★ волна 1: фасад нотисов (Loc / Chat / MsgBox)
#include "AsdaBytes.h"   // D4: единые читалки сырой записи (little-endian, DataHex, ASCII-поле)
#include "UI/AsdaUiHelpers.h"   // ★ D2/D4: единые UI-хелперы (дедуп 2026-08-08)

#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaMiniStateSubsystem.h"        // один общий тултип на HUD (эталон держит один g_ItemTooltip)
#include "UI/AsdaItemTooltipSubsystem.h"      // владелец общей плашки (порт g_ItemTooltip)
// ★ ТОЛЬКО ЗАГОЛОВОК, поведение не меняется: ниже (мост к окну навыков) уже используется
//   `UAsdaSkillWindowSubsystem`, но #include отсутствовал — тип «доезжал» через unity-сборку.
//   Как только этот файл попадает в отдельную единицу трансляции, компиляция падала.
#include "UI/AsdaSkillWindowSubsystem.h"
#include "UI/AsdaInventorySubsystem.h"     // сводная сверка шторок отката (`asda.Cooldown.Dump`)
#include "UI/AsdaBoosterPanelSubsystem.h"  // ★ гейт `CObject_GetField254(g_pInventoryWnd)` = ОКНО ГАЧИ 10050
#include "UI/AsdaChatWindowSubsystem.h"    // ★ гейт `Input_IsTextInputFocused(g_pChatInputWnd)`
#include "UI/AsdaSkillDatabase.h"          // ★ Аудит №3 (A13): имя иконки навыка — только отсюда
#include "UI/AsdaAnimatedPlane.h"
#include "Items/AsdaItemTableSubsystem.h"
#include "Resources/AsdaTextureSubsystem.h"
#include "Login/AsdaText.h"   // [ПОРТ] 09.10.2026 (мультиязычность, этап 3): FAsdaText::Loc — заголовки подсказок по id
#include "World/AsdaWorldSubsystem.h"
#include "World/AsdaPlayerCharacter.h"
#include "World/AsdaPlayerController.h"
#include "World/AsdaKeybindConfig.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Engine/Engine.h"   // ★ 17.10.2026 GEngine->Exec(quit) в самотесте иконок эмоций
#include "Components/CanvasPanel.h"
#include "Components/PanelWidget.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Engine/Texture2D.h"
#include "Framework/Application/SlateApplication.h"
#include "Fonts/FontMeasure.h"
#include "Engine/DataTable.h"
#include "Items/AsdaItemDbRows.h"
#include "HAL/IConsoleManager.h"
#include "UObject/ObjectKey.h"       // ★ 26.10.2026: TObjectKey — ревизия списка навыков по экземпляру панели
#include "UI/AsdaCraftSubsystem.h"   // ★ волна В3 ремесла: единый предикат «окно ремесла открыто» (`IsAnyCraftWindowOpen`)
#include "UI/AsdaTradeStateSubsystem.h"   // ★ 29.09.2026: окно обмена 2008 открыто — гейт слотов и значков заявки 4/5
#include "AsdaOutgoingGate.h"               // ★ С-Р1: общий офлайн-гейт отладочных команд (RefuseDebugWhenOnline)
#include "World/AsdaFishingSubsystem.h"     // ★ 08.10.2026 (рыбалка В4): гейт «идёт рыбалка» в `IsActivationBlocked`
#include "AsdaSelf.h"                      // ★ Дедуп №2, В1-B (U5a): «это я?» — `AsdaSelf::IsSelfUserIdx`
#include "UI/AsdaHelpTopics.h"             // ★ 30.10.2026: справочные темы кнопок листания и пустого слота оружия
#include "UI/AsdaDeathInputLock.h"          // ★ 05.11.2026: меню смерти запирает слоты (эталон CUIManager+0x44, ui_core.c:67351)
#include "AsdaHotbarSubsystemInternal.h"   // ★ R2 10.11.2026: общее для частей AsdaHotbarSubsystem*.cpp

static ASDA_DEV_CVAR(int32, CVarLegacyPageDigit,
	TEXT("asda.Quick.LegacyPageDigit"),
	0,
	TEXT("Цифра номера страницы: 0 = исправлено (гасим прежнюю, зажигаем текущую); "
	     "1 = дефект оригинала hotbarSetCurrentPage — обе строки используют nPrevPage (Д2)."),
	ECVF_Default);

// ★★★ ДЕФОЛТ ПЕРЕВЁДЕН НА ЭТАЛОН (1). Причина — геометрия самого скина, `Data/Decoded/jui/gamemain_jui.ini`:
//     Name = "CoolTimeAfter_Effect"  ID = "QUICK_NEW_CEND1"  Origin = "-16 -16"  Size = "64 64"
//   при шаге слотов 35 px и ячейке 32 px (`QUICK_NEW_SLOT1 Origin "0 26"`, `SLOT2 "35 26"`, `SLOT3 "70 26"`).
//   64 px от −16 занимают [−16 … +48], сосед справа начинается на +35, сосед слева кончается на −3 ⇒
//   вспышка ФИЗИЧЕСКИ накрывает по ~13 px (≈40 % ширины иконки) ОБОИХ соседних слотов. Именно это
//   игрок и видел как «применил один навык — откат повесило на другие». В эталоне контролы
//   73031..73043 только `CloseHide`-ятся и не показываются никогда (Д6) — возвращаем эталон.
//   Включить вспышку обратно: `asda.Quick.LegacyNoReadyFlash 0`.
static ASDA_DEV_CVAR(int32, CVarLegacyNoReadyFlash,
	TEXT("asda.Quick.LegacyNoReadyFlash"),
	1,
	TEXT("Вспышка «перезарядка окончена» (CoolTimeAfter_Effect): 1 = как в эталоне — контрол только "
	     "прячется, вспышки нет никогда (ДЕФОЛТ: скин 64x64 при шаге слота 35 px залезает на соседей); "
	     "0 = играем вспышку (отступление Д6)."),
	ECVF_Default);

// ═══ ★★★ 26.10.2026 [ИСПРАВЛЕНИЕ] НАВЫК, КОТОРОГО У ПЕРСОНАЖА НЕТ, НА ПАНЕЛИ ГАСНЕТ (жалоба «All Skill Reset Scroll resets skill
//   points but leaves hotbar skill icons populated/functional»). ═══
//   [ОРИГИНАЛ] После свитка сброса сервер шлёт 5462 (полный, `sub_4803A0`, GameServer.c:51212) или 5464 (ветка, `sub_480F20`,
//   GameServer.c:51394), затем 6057 и новый список 5251 — уровни сброшенных навыков = 0 (навыки с `.sif +190` из списка уходят).
//   Раскладку хотбара (DB_QUICK_SLOT_INFO) сервер НЕ трогает и новой не шлёт. Клиент тоже: `skillTreeUiResetSlots_63D0@0xE563D0`,
//   `skillUiOnLearnSkill_6590@0xE56590`, `skillWnd_ResetElementSlots@0xE55910`, `skillWnd_OnSkillListPacket@0xE55B60`
//   (skill_buff.c:33359-33604) панель не трогают, `CHotbar_RefreshPage@0xDEA6F0` рисует слот навыка без проверки уровня, а
//   `hotbarRebuildSlots` (ui_core.c:67032) чистит слот лишь при отсутствии записи `.sif`. Итог у оригинала — иконка остаётся
//   яркой, нажатие уходит 5256 и сервер отвечает отказом 25 (`RecvChar_SkillUse`, GameServer.c:46036: уровень 0).
//   [ИСПРАВЛЕНИЕ] Слот навыка (тип 3) с уровнем 0 рисуется серой иконкой «<имя>-g» — тот же приём, что у оригинальной книги навыков
//   для невыученного (`Skill_BuildTreeUI`) и у слота навыка Друга без Друга (`hotbarRebuildSlots` case 4). Частичный сброс гасит
//   только навыки своей ветки (их уровни обнулил сервер), повторное изучение зажигает слот снова; раскладка не меняется и
//   на сервер не уходит. Применение такого слота отбивает гейт уровня `UAsdaSkillUseSubsystem::CheckGates` (нотис 1018).
//   Перекраска — по ревизии модели выученного (`AsdaSkillLearnedRevision`, AsdaSkillWindowSubsystem.cpp) в `Tick`.
//   0 — как прежде (слот остаётся ярким).
static ASDA_DEV_CVAR(int32, CVarGreyUnlearnedSkill,
	TEXT("asda.Quick.GreyUnlearnedSkill"),
	1,
	TEXT("Слот навыка на панели быстрого доступа: 1 = невыученный (уровень 0, например после свитка сброса) рисуется серой "
	     "иконкой «-g» и перекрашивается при каждом изменении списка навыков (исправление); 0 = как прежде и как у оригинала — "
	     "иконка остаётся яркой."),
	ECVF_Default);

static ASDA_DEV_CVAR(int32, CVarCooldownCurtainTotal,
	TEXT("asda.Cooldown.CurtainTotal"),
	1,
	TEXT("[ОРИГИНАЛ hudSlotUpdateCooldown@0xDF23D0] Темп шторки отката: 1 = как эталон (интервал по полной длительности, "
	     "прогон кончается ровно в остаток); 0 = прежнее (интервал = остаток при стартовом кадре > 0 — шторка гасла раньше)."),
	ECVF_Default);
static ASDA_DEV_CVAR(int32, CVarCooldownCurtainResync,
	TEXT("asda.Cooldown.CurtainResync"),
	1,
	TEXT("[ИСПРАВЛЕНИЕ 25.10.2026] Шторка доиграла, а откат в реестре ещё идёт (> 0,25 с) — поднять её с текущего кадра: "
	     "1 = да; 0 = прежнее (шторка оставалась погашенной до перекладки предмета)."),
	ECVF_Default);

static ASDA_DEV_CVAR(int32, CVarQuickIconFlagReset,
	TEXT("asda.Quick.IconFlagReset"),
	1,
	TEXT("[ИСПРАВЛЕНИЕ] Пересборка слота панели: 1 = признак «иконка — цельная текстура» (навык/эмоция) сбрасывается при "
	     "каждой пересборке, предмет в бывшем слоте навыка рисуется своей клеткой атласа; 0 = прежнее — рисовалась клетка 0."),
	ECVF_Default);

// ★ Параметры прогона шторки для остатка `RemainMs` и стартового кадра `StartFrame` из `MaxIdx + 1` кадров.
//   Ключ CurtainTotal 1: интервал = остаток · MaxIdx / (MaxIdx − start) — темп полной длительности (эталон), конец прогона
//   ровно в остаток. 0: интервал = остаток (прежнее). Отдельной функцией — её же сверяет самотест `asda.SelfTest.Cooldowns`.
float AsdaCooldownCurtainIntervalMs(float RemainMs, int32& InOutStartFrame, int32 MaxIdx)
{
	if (CVarCooldownCurtainTotal.GetValueOnAnyThread() == 0 || MaxIdx <= 0)
	{
		InOutStartFrame = FMath::Clamp(InOutStartFrame, 0, FMath::Max(0, MaxIdx));   // прежнее: интервал = остаток
		return RemainMs;
	}
	// Последний кадр (MaxIdx) стартом не бывает: делитель (MaxIdx − start) ≥ 1.
	InOutStartFrame = FMath::Clamp(InOutStartFrame, 0, MaxIdx - 1);
	return RemainMs * (float)MaxIdx / (float)(MaxIdx - InOutStartFrame);
}

// ── Таблица «VK → idnum глифа клавиши» (эталон `inputRegisterKeyNameTable@0xDF6250`, input.c:1738) ──
static int32 VkToGlyphIdNum(int32 Vk)
{
	if (Vk >= 112 && Vk <= 123) { return Vk + 73004; }   // F1..F12 → 73116..73127
	if (Vk >= 48  && Vk <= 57)  { return Vk + 73032; }   // 0..9    → 73080..73089
	if (Vk >= 65  && Vk <= 90)  { return Vk + 73025; }   // A..Z    → 73090..73115
	switch (Vk)
	{
	case 16:  return 73141;   // Shift  (idc «NEW_Key_Shit» — опечатка оригинала, Д7)
	case 17:  return 73142;   // Ctrl
	case 18:  return 73143;   // Alt
	case 9:   return 73140;   // Tab
	case 44:  return 73139;   // PrintScreen (VK_SNAPSHOT; прежняя дока звала это «Escape» — неверно)
	case 192: return 73129;   // `
	case 189: return 73130;   // -
	case 187: return 73131;   // =
	case 219: return 73132;   // [
	case 221: return 73133;   // ]
	case 220: return 73134;   // backslash
	case 186: return 73135;   // ;
	case 222: return 73136;   // '
	case 188: return 73137;   // ,
	case 190: return 73138;   // .
	case 191: return 73128;   // /
	default:  return 0;
	}
}

// ══════════════════════════════ ИКОНКИ ЭМОЦИЙ И НАВЫКОВ ИЗ ТАБЛИЦ ══════════════════════════════
//   ★ Ни окно эмоций, ни книга навыков в порту не нужны, чтобы НАРИСОВАТЬ иконку: имя текстуры лежит
//   в тех же таблицах, что уже импортированы подсистемой предметов (50 DataTable). Проверено дважды —
//   по декомпилу и по самим данным.
// Эталон `EmotionTip_Load@0xC529D0` (ui_core.c:31744): запись 265 б, байт `+64` = группа,
//   строка `+32` = имя иконки. Раскладка по трём спискам:
//     группа 0                                   → список 0
//     группа 1, КРОМЕ `act_cancellation`         → список 1
//     группа 2, КРОМЕ `mes_couple`               → список 2
//     группа 3                                   → никуда (в срезе ни один список её не берёт)
void UAsdaHotbarSubsystem::BuildEmotionLists()
{
	if (bEmotionListsBuilt) { return; }
	bEmotionListsBuilt = true;
	UGameInstance* GI = GetGameInstance();
	UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	UDataTable* Dt = IT ? IT->GetDataTable(FName(TEXT("emotiontip"))) : nullptr;
	if (!Dt)
	{
		UE_LOG(LogAsdaQuick, Warning, TEXT("[хотбар] таблица emotiontip не загружена — иконок эмоций не будет"));
		return;
	}
	// Порядок ФАЙЛА: имена строк DataTable — это индекс записи («0», «1», …).
	const int32 Count = Dt->GetRowMap().Num();
	for (int32 i = 0; i < Count; ++i)
	{
		// ★ Аудит №3 (A14): прямой `Dt->FindRow` мимо менеджера снят — резолв через типизированный
		//   геттер `GetEmotionTip`. Он ПОЗИЦИОННЫЙ, потому что ключевой колонки у `emotiontip` нет ни
		//   в данных, ни в оригинале (эталон адресует массив индексом) — поведение то же, что было.
		FAsdaEmotiontipRow R;
		if (!IT->GetEmotionTip(i, R) || R.Tag.IsEmpty()) { continue; }
		// ★ Текст подсказки берём ИЗ ТОЙ ЖЕ ЗАПИСИ И В ТОТ ЖЕ ИНДЕКС: эталонный
		//   `emotionTip_lookupByCode` возвращает УКАЗАТЕЛЬ НА ЗАПИСЬ, а `tooltipShowAtCursor`
		//   печатает строку с её НАЧАЛА (смещение 0 = колонка `Name`).
		if (R.Type == 0)                                           { EmotionList[0].Add(R.Tag); EmotionTip[0].Add(R.Name); }
		else if (R.Type == 1 && R.Tag != TEXT("act_cancellation")) { EmotionList[1].Add(R.Tag); EmotionTip[1].Add(R.Name); }
		else if (R.Type == 2 && R.Tag != TEXT("mes_couple"))       { EmotionList[2].Add(R.Tag); EmotionTip[2].Add(R.Name); }
	}
	UE_LOG(LogAsdaQuick, Log, TEXT("[хотбар] эмоции: списки построены — группа0 %d, группа1 %d, группа2 %d (всего записей %d)"),
		EmotionList[0].Num(), EmotionList[1].Num(), EmotionList[2].Num(), Count);
}

// Эталон `emotionTip_lookupByCode@0xC52870`: 0..99 → список 0 по индексу `code`;
//   100..199 → список 1 по `code % 100`; 200..299 → список 2 по `code % 200`; иначе запись не найдена
//   (и слот в `hotbarRebuildSlots` ЧИСТИТСЯ — LABEL_29).
bool UAsdaHotbarSubsystem::ResolveEmotionIcon(int32 Code, FString& OutTex, bool& bOutFullSize) const
{
	const_cast<UAsdaHotbarSubsystem*>(this)->BuildEmotionLists();
	int32 List = -1, Idx = -1;
	if (Code >= 0 && Code < 100)        { List = 0; Idx = Code; }
	else if (Code >= 100 && Code < 200) { List = 1; Idx = Code % 100; }
	else if (Code >= 200 && Code < 300) { List = 2; Idx = Code % 200; }
	if (List < 0 || !EmotionList[List].IsValidIndex(Idx)) { return false; }
	OutTex = EmotionList[List][Idx];

	// Спец-правила `CHotbar_RefreshPage` (ui_core.c:66400-66445):
	//   • без соулмейта к трём «парным» действиям добавляется суффикс `_g` (серая версия);
	//   • `act_updown` / `act_standup` выбираются по состоянию персонажа (сидит/стоит) — В МОМЕНТ ОТРИСОВКИ
	//     (`ApplyIconToImage` → `SitStandIconName`, 27.09.2026); здесь остаётся имя из таблицы — его же ждёт
	//     лоток окна действий (`ActionUI_RefreshIcons` рисует имя как есть);
	//   • три иконки рисуются во весь размер 64×64, а не 32×32.
	if (!HasSoulmate()
		&& (OutTex == TEXT("act_juggling") || OutTex == TEXT("act_upperarm") || OutTex == TEXT("act_courtship")))
	{
		OutTex += TEXT("_g");
	}
	bOutFullSize = IsFullSizeEmotionIcon(OutTex);
	return true;
}

// ★ 06.11.2026 [ИСПРАВЛЕНИЕ] (баг игроков «Друг вне игры — игра считает, что Друга нет, парные действия закрыты»). Прежде признак жил здесь
//   копией, которую ставил только 5220 при `IsSelfUserIdx`. Но свой 5220 приходит ДО 4003 (журнал Player52 05.11: 5220 в 11:44:56.919, 4003 в
//   11:44:57.003), а строгая проверка номера до 4003 ложна — запись терялась. Друг в игре спасал второй 5220 (`Recv_SoulMateLoginStateInfo`,
//   GameServer.c:59349), вне игры второго нет. Теперь пара и «Друг в игре» читаются из карточки соулмейта: 5220 она сверяет по charId,
//   как эталон (`*(DWORD*)Src == localChar[263]`), и чистит по 5207. Разделение по эталону: `gap_878[47] != −1` — пара (три парных
//   действия: `UI_DispatchTargetContextMenu@0xC532D0` case 9/10/11, `CHotbar_RefreshPage`), `gap_8B4[6] == 1` — Друг в игре (навыки Дружбы).
//   1 — так; 0 — прежний порт (одна копия от 5220 с проверкой номера).
static ASDA_DEV_CVAR(int32, CVarAsdaSoulSlotTruth, TEXT("asda.Soul.SlotTruth"), 1,
	TEXT("Пара и «Друг в игре» из карточки соулмейта (5220 по charId): 1 = как эталон (Друг вне игры парным действиям не мешает, навыки Дружбы — 8162), 0 = прежний порт"),
	ECVF_Default);

bool AsdaSoulSlotTruthOn() { return CVarAsdaSoulSlotTruth.GetValueOnGameThread() != 0; }

bool UAsdaHotbarSubsystem::HasSoulmate() const
{
	if (!AsdaSoulSlotTruthOn()) { return bSelfHasSoulmate; }
	const UGameInstance* GI = GetGameInstance();
	const UAsdaSoulmateSubsystem* Sm = GI ? GI->GetSubsystem<UAsdaSoulmateSubsystem>() : nullptr;
	return Sm && Sm->GetFriendSlot() != -1;
}

bool UAsdaHotbarSubsystem::IsFriendInGame() const
{
	if (!AsdaSoulSlotTruthOn()) { return bSelfHasSoulmate; }
	const UGameInstance* GI = GetGameInstance();
	const UAsdaSoulmateSubsystem* Sm = GI ? GI->GetSubsystem<UAsdaSoulmateSubsystem>() : nullptr;
	return Sm && Sm->GetFriendSlot() != -1 && Sm->HasSoulmatePair();
}

// Сколько записей реально загрузилось в группу (эталон: лоток шире списка — лишние слоты пустые).
int32 UAsdaHotbarSubsystem::EmotionCountInGroup(int32 Group) const
{
	const_cast<UAsdaHotbarSubsystem*>(this)->BuildEmotionLists();
	if (Group < 0 || Group > 2) { return 0; }
	return EmotionList[Group].Num();
}

// ★ Подсказка эмоции — тот же выбор списка/индекса, что и у иконки (`emotionTip_lookupByCode@0xC52870`).
bool UAsdaHotbarSubsystem::ResolveEmotionTip(int32 Code, FString& OutText) const
{
	const_cast<UAsdaHotbarSubsystem*>(this)->BuildEmotionLists();
	int32 List = -1, Idx = -1;
	if (Code >= 0 && Code < 100)        { List = 0; Idx = Code; }
	else if (Code >= 100 && Code < 200) { List = 1; Idx = Code % 100; }
	else if (Code >= 200 && Code < 300) { List = 2; Idx = Code % 200; }
	if (List < 0 || !EmotionTip[List].IsValidIndex(Idx)) { return false; }
	OutText = EmotionTip[List][Idx];
	return !OutText.IsEmpty();
}

// Эталон `uiGetRecordByIndex@0xE52930`: массив читается из `SkillTooltip.BIN` ЦЕЛИКОМ
//   (`Skill_LoadSkillTooltipTable@0xE56860`, 504 б/запись), обращение — ПО ИНДЕКСУ.
//   Иконка = `filler[36]`, т.е. ASCII-строка по смещению 36 записи (проверено по самому файлу:
//   имя вида `skl_f1_hamblow` присутствует на +36 у 500 из 1255 записей).
//   ★ Аудит №3 (A13): к полю +36 вело ТРИ независимых пути (база навыков, панель, хранитель) — свой
//   `LoadObject`/`FindRow`/hex-цикл в каждом. Остался один: `UAsdaSkillDatabase` разбирает ту же
//   запись при подъёме и держит имя в `FAsdaSkillDef::IconName`. База — подсистема (A12), поэтому
//   доступна и когда книга навыков не открывалась.
bool UAsdaHotbarSubsystem::ResolveSkillIcon(int32 RecordIdx, FString& OutTex) const
{
	if (RecordIdx < 0) { return false; }
	UGameInstance* GI = GetGameInstance();
	const UAsdaSkillDatabase* Db = GI ? GI->GetSubsystem<UAsdaSkillDatabase>() : nullptr;
	const FAsdaSkillDef* D = Db ? Db->Get(RecordIdx) : nullptr;
	if (!D) { return false; }
	OutTex = D->IconName;
	return !OutTex.IsEmpty();
}

// Эталон `skillFindEntryById@0xE52980`: линейный поиск по `record[0]` в списке, загруженном из
//   `SoulMateSkillTable.BIN` (272 б/запись). Иконка = смещение 36 (в порту колонка `SkillName`).
//   Нет соулмейта → эталон добавляет суффикс `-g` (`hotbarRebuildSlots`, case 4).
bool UAsdaHotbarSubsystem::ResolveSoulSkillIcon(int32 SkillId, FString& OutTex) const
{
	UGameInstance* GI = GetGameInstance();
	UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	UDataTable* Dt = IT ? IT->GetDataTable(FName(TEXT("soulmateskilltable"))) : nullptr;
	if (!Dt) { return false; }
	for (const TPair<FName, uint8*>& It : Dt->GetRowMap())
	{
		const FAsdaSoulmateskilltableRow* R = reinterpret_cast<const FAsdaSoulmateskilltableRow*>(It.Value);
		if (!R || R->Id != SkillId) { continue; }
		if (R->SkillName.IsEmpty()) { return false; }
		OutTex = R->SkillName;
		if (!IsFriendInGame()) { OutTex += TEXT("-g"); }   // эталон `hotbarRebuildSlots`: `gap_8B4[6] != 1 || gap_878[47] == −1`
		return true;
	}
	return false;
}

// ══════════════════════════════ МОДЕЛЬ: ПЕРЕСБОРКА КЭША ════════════════════════════════════════
//   Эталон `hotbarRebuildSlots@0xDEC380`: цикл по ВСЕМ 78 слотам; для Type=1 перечитывает запись
//   контейнера по (SubType, Index), обновляет Count/Id и имя/UV иконки; если записи больше нет —
//   `memset(&slot[0], 0, 0xA)` (слот молча пустеет).
void UAsdaHotbarSubsystem::RebuildOneSlot(int32 AbsIndex)
{
	if (!Slots.IsValidIndex(AbsIndex)) { return; }
	FAsdaHotbarSlot& S = Slots[AbsIndex];
	S.IconAtlas.Reset(); S.IconX = 0; S.IconY = 0; S.IconSize = 32; S.bIconFromImgTable = false;
	// ★ [ИСПРАВЛЕНИЕ 05.11.2026] флаг «цельная текстура» — тоже поле иконки, его ставят ТОЛЬКО ветки навыка/эмоции ниже
	//   (разбор — у ключа `asda.Quick.IconFlagReset`).
	if (CVarQuickIconFlagReset.GetValueOnGameThread() != 0) { S.bIconWholeTexture = false; }
	if (S.Type == 0) { return; }

	switch ((EAsdaHotbarSlotType)S.Type)
	{
	case EAsdaHotbarSlotType::Item:
	{
		// ★★★ ДЛЯ ИКОНКИ — ПОЛНЫЙ код предмета, НЕ усечённый.
		//   `S.Id` хранится 16-битным (столько отведено в записи слота и в пакете 5067), а коды
		//   `item_list` доходят до **79428** — всё, что выше 65535, при усечении «оборачивается»:
		//   например 72xxx − 65536 = 7xxx, а это диапазон САМОЦВЕТОВ (7001…10925), и `GetItemIcon`
		//   честно отдавал картинку камня. Так и вылезал баг «Инструкция из расходников показывает
		//   иконку самоцвета». Полный код есть у моста инвентаря — им и рисуем.
		int32 IconCode = S.Id;
		// ★★★ ГЕЙТ ГОТОВНОСТИ КОНТЕЙНЕРА (правка 2026-08-09, дефект «раскладка не переживает
		//   перезаход»). Эталон `hotbarRebuildSlots` читает g_InventoryBag НАПРЯМУЮ, и к моменту
		//   прихода 5059 сумка уже заполнена: по проводу 4045/4048 идут РАНЬШЕ 5059 (живой лог
		//   15:41:50.326 / 50.707 против 50.801). У нас оба применяются отложенно в `Tick`, и
		//   регистрация моста (`SetInventoryBridge` → `RebuildSlots`) успевала произойти РАНЬШЕ
		//   разбора батчей — тогда КАЖДЫЙ слот-предмет получал «ячейка пуста» и чистился, а первое
		//   же следующее изменение увозило выпотрошенную страницу на сервер пакетом 5067.
		//   Пока контейнер не получен целиком — слот держим как при неподключённом мосте: рисуем
		//   по кэшу из 5059 и НИЧЕГО не чистим.
		const bool bContainerReady = Inventory.IsBound()
			&& (!Inventory.IsContainerReady || Inventory.IsContainerReady((int32)S.SubType));
		if (Inventory.IsBound() && !bContainerReady && !bLoggedContainerNotReady)
		{
			bLoggedContainerNotReady = true;
			UE_LOG(LogAsdaQuick, Log,
				TEXT("[хотбар] содержимое контейнера %d ещё не получено целиком — слоты-предметы держим "
				     "по кэшу пакета 5059 и НЕ чистим (иначе пустая страница уедет на сервер пакетом 5067)"),
				(int32)S.SubType);
		}
		if (bContainerReady)
		{
			const FAsdaHotbarItemEntry E = Inventory.GetEntry((int32)S.SubType, S.Index);
			if (!E.bValid || E.ItemCode <= 0)
			{
				S.Clear();          // эталон LABEL_29: ячейка опустела → слот чистится
				return;
			}
			S.Count = E.Count;
			S.Id = (int32)(uint16)E.ItemCode;
			IconCode = E.ItemCode;                       // ← полный, без усечения
			// ★★★ ГЕЙТЫ «ПРЕДМЕТ В ПАНЕЛИ НЕ ОТОБРАЖАЕТСЯ» (эталон `hotbarRebuildSlots@0xDEC380`,
			//   ui_core.c:66971-66985). Ровно четыре условия, все ведут в LABEL_29 (слот стирается):
			//     itemCode == 20551 (ЗОЛОТО) · код в диапазоне САМОЦВЕТОВ (g_SowelCodeRangeLo..Hi) ·
			//     itemType == 11 && itemSubType 6..12 · flagWord176 != 0.
			//   ⚠ ПРИМЕНЯЕМ ТОЛЬКО К ПОЛНОМУ КОДУ ИЗ МОСТА. По усечённому 16-битному кэшу пакета
			//   5059 проверка самоцветов даёт ЛОЖНОЕ срабатывание: 72xxx − 65536 = 7xxx попадает
			//   в 7001…10925 — та же грабля, что раньше рисовала «иконку камня» вместо инструкции.
			if (UGameInstance* GI2 = GetGameInstance())
			{
				if (const UAsdaItemTableSubsystem* IT2 = GI2->GetSubsystem<UAsdaItemTableSubsystem>())
				{
					if (const FAsdaItemRecord* R2 = IT2->GetItem(IconCode))
					{
						const bool bDisplayable =
							   (R2->ItemCode != 20551)
							&& !IT2->IsSowelCode(IconCode)
							&& !(R2->ItemType == 11 && R2->ItemSubType >= 6 && R2->ItemSubType <= 12)
							&& (R2->FlagWord176 == 0);
						if (!bDisplayable)
						{
							UE_LOG(LogAsdaQuick, Log,
								TEXT("[хотбар] слот %d: предмет %d в панели не отображается (эталон LABEL_29: "
								     "золото/самоцвет/тип 11 подтип 6..12/flagWord176) — слот очищен"),
								AbsIndex, IconCode);
							S.Clear();
							return;
						}
					}
				}
			}
		}
		else if (!Inventory.IsBound() && !bLoggedNoInventory)
		{
			bLoggedNoInventory = true;
			UE_LOG(LogAsdaQuick, Log,
				TEXT("[хотбар] система инвентаря ещё не портирована: слоты предметов держат (контейнер=%d, ячейка=%d) "
				     "и КЭШ (itemCode=%d, кол-во=%d) из пакета 5059 — иконка рисуется по кэшу, применение недоступно"),
				(int32)S.SubType, S.Index, S.Id, S.Count);
		}
		// Иконка предмета: мост itemCode → imgIndex → item_img_table уже портирован.
		if (UGameInstance* GI = GetGameInstance())
		{
			if (UAsdaItemTableSubsystem* IT = GI->GetSubsystem<UAsdaItemTableSubsystem>())
			{
				const FAsdaItemIcon Ic = IT->GetItemIconSelf(IconCode); // ★ 08.11.2026: атлас пола своего персонажа, как эталон `g_ItemImgOptionVariant`
				if (Ic.IsValid())
				{
					S.IconAtlas = Ic.Atlas; S.IconX = Ic.CellX; S.IconY = Ic.CellY;
					S.IconSize = Ic.CellSize; S.bIconFromImgTable = true;
				}
			}
		}
		break;
	}
	case EAsdaHotbarSlotType::Emotion:
	{
		// ★ Иконка эмоции берётся ПРЯМО из таблицы `emotiontip` — окно эмоций для этого не нужно.
		FString Tex; bool bFull = false;
		if (Misc.GetEmotionIconName)
		{
			Tex = Misc.GetEmotionIconName(S.Index);           // мост (если появится окно эмоций) — приоритетнее
		}
		if (Tex.IsEmpty()) { ResolveEmotionIcon(S.Index, Tex, bFull); }
		if (Tex.IsEmpty())
		{
			// Эталон `hotbarRebuildSlots` case 2: записи нет → слот ЧИСТИТСЯ (LABEL_29).
			UE_LOG(LogAsdaQuick, Warning, TEXT("[хотбар] слот %d: эмоция с кодом %d в таблице emotiontip не найдена — слот очищен"),
				AbsIndex, S.Index);
			S.Clear();
			return;
		}
		bFull = bFull || IsFullSizeEmotionIcon(Tex);           // ★ 17.10.2026 и для имени из моста окна эмоций
		S.IconAtlas = Tex; S.IconX = 0; S.IconY = 0;
		S.IconSize = bFull ? 64 : 32;
		S.bIconFromImgTable = false; S.bIconWholeTexture = true;
		break;
	}
	case EAsdaHotbarSlotType::Skill:
	case EAsdaHotbarSlotType::SkillAuto:
	{
		const bool bAuto = ((EAsdaHotbarSlotType)S.Type == EAsdaHotbarSlotType::SkillAuto);
		if (bAuto && Skills.FindBestSlotForLevel)
		{
			// Эталон: Type=4 при ребилде переписывается на подходящую уровню запись.
			const int32 Best = Skills.FindBestSlotForLevel(S.Id);
			if (Best > 0) { S.Id = (int32)(uint16)Best; }
		}
		FString Tex;
		bool bUnavailable = false;   // ★ 26.10.2026: мост говорит «навык не выучен» (уровень 0 при пришедшем списке)
		if (Skills.GetSkillIconName)
		{
			Tex = Skills.GetSkillIconName(S.Id, bUnavailable);   // мост книги навыков — приоритетнее
		}
		// ★ 26.10.2026 [ИСПРАВЛЕНИЕ] (шапка `asda.Quick.GreyUnlearnedSkill`): невыученный навык (тип 3) — серая иконка «-g».
		//   Серой текстуры нет (у части иконок оригинала её не завели) — остаётся обычная, в журнал строка.
		if (!bAuto && bUnavailable && !Tex.IsEmpty() && CVarGreyUnlearnedSkill.GetValueOnGameThread() != 0)
		{
			const FString Grey = Tex + TEXT("-g");
			UAsdaTextureSubsystem* TexMgr = AsdaTexMgr();
			if (TexMgr && TexMgr->LoadTextureCached(AsdaAssetPaths::JuiTextureObject(Grey)))
			{
				Tex = Grey;
			}
			else
			{
				UE_LOG(LogAsdaQuick, Log, TEXT("[хотбар] слот %d: навык %d не выучен, серой иконки «%s» нет — рисуем обычную"),
					AbsIndex, S.Id, *Grey);
			}
		}
		// ★ Без книги навыков имя иконки всё равно есть: Type 3 — запись `skilltooltip` ПО ИНДЕКСУ,
		//   Type 4 — запись `soulmateskilltable` по коду навыка.
		if (Tex.IsEmpty())
		{
			const bool bOk = bAuto ? ResolveSoulSkillIcon(S.Id, Tex) : ResolveSkillIcon(S.Id, Tex);
			if (!bOk) { Tex.Reset(); }
		}
		if (Tex.IsEmpty())
		{
			// ★★★ ОТСТУПЛЕНИЕ ОТ ЭТАЛОНА, СДЕЛАННОЕ СОЗНАТЕЛЬНО (закон fix-original-defects).
			//   Эталон здесь идёт в LABEL_41 (`ui_core.c:67063`) и делает `memset(slot, 0, 0xA)` —
			//   то есть СТИРАЕТ слот, если запись навыка не нашлась, а следующее же изменение
			//   страницы увозит пустую запись на сервер пакетом 5067. Ровно так пропала бы вся
			//   раскладка навыков при дефекте манифеста item-БД (не грузилась `DT_skill_info`,
			//   `Defs` пуста ⇒ ни одна иконка не резолвилась). Данные слота хранятся на СЕРВЕРЕ,
			//   а причина ненахода — клиентская таблица; поэтому слот сохраняем и громко пишем в лог.
			UE_LOG(LogAsdaQuick, Warning,
				TEXT("[хотбар] слот %d: иконка навыка (тип %d, id %d) в таблицах не найдена — "
				     "слот СОХРАНЁН (эталон здесь стирает запись; проверьте, загрузилась ли DT_skill_info)"),
				AbsIndex, S.Type, S.Id);
			break;
		}
		S.IconAtlas = Tex; S.IconX = 0; S.IconY = 0; S.IconSize = 32;
		S.bIconFromImgTable = false; S.bIconWholeTexture = true;
		break;
	}
	case EAsdaHotbarSlotType::ExtraMenu:
		if (Misc.GetExtraMenuIcon)
		{
			FString Tex; int32 X = 0, Y = 0;
			// Вид ключа берём из `SubType` слота: 0 — эталонный порядковый номер строки,
			//   1 — наш устойчивый код действия (см. `FAsdaHotbarMiscBridge`).
			if (Misc.GetExtraMenuIcon((int32)S.SubType, S.Index, Tex, X, Y))
			{
				S.IconAtlas = Tex; S.IconX = X; S.IconY = Y; S.IconSize = 32; S.bIconFromImgTable = false;
			}
		}
		break;
	default:
		break;
	}
}

void UAsdaHotbarSubsystem::RebuildSlots()
{
	for (int32 I = 0; I < Slots.Num(); ++I) { RebuildOneSlot(I); }
}

// ══════════════════════════════ РЕНДЕР ═════════════════════════════════════════════════════════
bool UAsdaHotbarSubsystem::ApplyIconToImage(UImage* Img, const FAsdaHotbarSlot& S) const
{
	if (!Img) { return false; }
	if (S.IconAtlas.IsEmpty()) { return false; }
	UAsdaTextureSubsystem* TexMgr = AsdaTexMgr();   // ★ Аудит №3 (B8): короткий доступ к менеджеру текстур
	if (!TexMgr) { return false; }
	// ★ 27.09.2026: у слота-эмоции имя «сесть/встать» выбирается по состоянию персонажа в момент отрисовки (эталон).
	const FString IconName = (S.Type == (uint8)EAsdaHotbarSlotType::Emotion) ? SitStandIconName(S.IconAtlas) : S.IconAtlas;
	UTexture2D* Tex = TexMgr->LoadTextureCached(AsdaAssetPaths::JuiTextureObject(IconName));   // ★ волна 2 п. 7
	if (!Tex) { return false; }
	// ★ Иконки эмоций/навыков — ОТДЕЛЬНЫЕ текстуры (эталон `DrawTextureRect(tex, 0, 0, 32, 32)`),
	//   а не ячейки атласа: берём текстуру целиком.
	// ★ ИСПРАВЛЕНИЕ (дедуп №2, В2-B, U4d): крой ПО ЭТАЛОНУ, а не «вся текстура». `CControl_DrawTextureRect(ctrl, tex, 0, 0, W, H)`
	//   (ui_core.c:109716) → `planeFrame_setUV(x/ширина текстуры, …)` (math_geom.c:16937): показывается левый-верхний прямоугольник
	//   W×H ЛОГИЧЕСКИХ пикселей текстуры. W×H = 32×32, у трёх эмоций `act_invite`/`act_itemchange`/`act_itemchange2` — 64×64
	//   (ui_core.c:66401-66440, `S.IconSize`). Было UV 0..1 у любой текстуры; у хранителя — крой. Перепись размеров
	//   (`asda.UI.WholeIconCensus`, 1145 текстур эмоций/навыков/портретов): логический размер = W×H у всех, так что кисть
	//   прежняя (UV 0..1); правило одно — `AsdaUi::ApplyJuiIconRegionTex` (то же, что клетка атласа).
	if (S.bIconWholeTexture)
	{
		const int32 Rect = FMath::Max(1, S.IconSize);
		return AsdaUi::ApplyJuiIconRegionTex(Img, Tex, 0, 0, Rect, Rect, 32.f, 32.f);
	}
	// Атласы бывают уже 8 колонок × 64 px — эталон рисует с D3DTADDRESS_WRAP, повторяем модулем.
	// ★ Дедуп №2, В1-B (U4b, 11.10.2026): вырезка клетки и заворот — общая точка `AsdaUi::ApplyJuiIconRegionTex` (то же тело,
	//   что у `ApplyJuiIconCell`); загрузчик текстуры остаётся здесь (имя «сесть/встать» выбирается при отрисовке).
	//   Контрол слота — 32×32 (JUI Size = "32 32").
	return AsdaUi::ApplyJuiIconRegionTex(Img, Tex, S.IconX, S.IconY, FMath::Max(1, S.IconSize), FMath::Max(1, S.IconSize), 32.f, 32.f);
}

// Эталон `CHotbar_RefreshPage@0xDEA6F0`: цикл по 13 колонкам текущей страницы.
void UAsdaHotbarSubsystem::UpdateSlotVisual(int32 Column, bool bTRow)
{
	TArray<FAsdaHotbarSlotUi>& Row = bTRow ? TUi : MainUi;
	if (!Row.IsValidIndex(Column)) { return; }
	FAsdaHotbarSlotUi& U = Row[Column];
	const int32 Abs = AbsSlot(Column, bTRow);
	const FAsdaHotbarSlot& S = GetSlot(Abs);

	if (S.IsEmpty())
	{
		// Эталон: спрятать COOL/CEND, поставить дефолтную пустую текстуру, очистить текст.
		SetVis(U.Cool.Plane, false);
		SetVis(U.Flash, false);
		if (U.Icon)
		{
			FSlateBrush B = U.Icon->GetBrush();
			B.SetResourceObject(nullptr);
			B.DrawAs = ESlateBrushDrawType::NoDrawType;
			U.Icon->SetBrush(B);
			SetVis(U.Icon, true);
		}
		if (U.StackText) { U.StackText->SetText(FText::GetEmpty()); }
		return;
	}

	if (ApplyIconToImage(U.Icon, S))
	{
		if (U.Icon) { U.Icon->SetVisibility(ESlateVisibility::SelfHitTestInvisible); }
	}
	else if (U.Icon)
	{
		// Иконки нет (тип без моста / атлас не импортирован) — слот остаётся пустым визуально,
		//   но ДАННЫЕ в нём есть и уходят на сервер без потерь.
		FSlateBrush B = U.Icon->GetBrush();
		B.SetResourceObject(nullptr);
		B.DrawAs = ESlateBrushDrawType::NoDrawType;
		U.Icon->SetBrush(B);
	}

	// ★ Счётчик стака — ТЕКСТ самого контрола-иконки (эталон CHotbar_RefreshPage:66450:
	//   `if (*(int*)(slot+4) > 0) CControl_SetText(apSlotIcon[i], "%d")`). Отдельного контрола нет.
	//   ⚠ Именно `%d`, БЕЗ разделителя разрядов: `FText::AsNumber` дал бы «1 000» вместо «1000».
	if (U.StackText)
	{
		const bool bShow = (S.Type == (uint8)EAsdaHotbarSlotType::Item) && (S.Count > 0);
		U.StackText->SetText(bShow ? FText::FromString(FString::FromInt(S.Count)) : FText::GetEmpty());
	}

	UpdateCooldownVisual(Column, bTRow);
}

// ★★★ ЕДИНСТВЕННАЯ РЕАЛИЗАЦИЯ ОТРИСОВКИ ШТОРКИ НА ВСЕ ОКНА (закон `port-shared-points`).
//   Эталон `hudSlotUpdateCooldown@0xDF23D0` / `Hotbar_CooldownOverlay_Update@0xDF22C0` /
//   `uiQuickSlotUpdateCooldownDisplay@0xDF24F0` — все три одинаковы по схеме:
//     state==1 && remain>0 → uiControl_ShowWithItem(ctrl, кадр, 117, 118, (u64)remainMs, animate=1)
//     state==2             → uiControl_ShowWithItem(ctrl, кадр, 127,   8,          1000, animate=0)
//     иначе                → CControl_CloseHide(ctrl)
//   `uiControl_ShowWithItem` кладёт аргументы прямо в план: StartFrame/EndFrame/TotalFrame/
//   FrameInterval/Animate, ставит время старта и делает контрол видимым.
//   Зовут ЭТО тело: панель (`UpdateCooldownVisual`), инвентарь (`TickCooldownCurtains`) и книга
//   навыков (`TickCooldownCurtains`) — своих копий механики кадров у них нет.
bool UAsdaHotbarSubsystem::DriveCurtain(FAsdaCooldownCurtain& C, int32 Key,
                                        const FAsdaHotbarCooldown& CD, double NowS) const
{
	UAsdaAnimatedPlane* P = C.Plane;
	if (!P) { return false; }

	const double EndS = (CD.State == 1) ? (NowS + (double)CD.RemainMs / 1000.0) : 0.0;
	// ★ Перезапускаем спрайт ТОЛЬКО когда что-то реально изменилось: содержимое ячейки, состояние
	//   или момент окончания (допуск 0.15 с — дрожание пересчёта остатка). Иначе анимация,
	//   перерисовываемая каждый кадр, вечно висела бы на кадре 0.
	bool bChanged = (C.ShownKey != Key) || (C.ShownState != CD.State)
		|| (CD.State == 1 && FMath::Abs(EndS - C.ShownEndS) > 0.15);
	// ★ [ИСПРАВЛЕНИЕ] 25.10.2026 — ДОГОНКА (шапка ключей откатов, п. 3): план доиграл и сам погас, а реестр ещё «в откате»
	//   — поднять шторку заново с текущего кадра. Порог 0,25 с — не дёргать план на последних кадрах прогона.
	if (!bChanged && CD.State == 1 && C.ShownState == 1 && CD.RemainMs > 250.f && P->IsAnimFinished()
		&& CVarCooldownCurtainResync.GetValueOnAnyThread() != 0)
	{
		bChanged = true;
		UE_LOG(LogAsdaQuick, Verbose, TEXT("[откат] шторка ключа %d доиграла раньше реестра (осталось %.0f мс) — поднята заново"),
			Key, CD.RemainMs);
	}

	if (CD.State == 1 && CD.RemainMs > 0.f)
	{
		if (bChanged)
		{
			P->TotalFrame      = CurtainTotalFrames;
			P->bLoop           = false;
			P->MaxLoops        = 1;
			// ★ Стартовый кадр = уже пройденная доля отката (эталон: текущий кадр записи реестра +44, его ведёт тик
			//   `craftQueueTick@0xE47600`). Для свежего отката он равен 0.
			// ★★★ [ОРИГИНАЛ] 25.10.2026 — ИНТЕРВАЛ (шапка ключей откатов, п. 2). БЫЛО «интервал = ОСТАТОК (эталон 1:1)» —
			//   НЕВЕРНО: бинарь `hudSlotUpdateCooldown@0xDF23D0` кладёт в интервал ПОЛНУЮ длительность записи (+36).
			//   С остатком перезапущенная посреди отката шторка гасла за `остаток·(117−start)/117`. Теперь прогон
			//   кончается ровно в остаток (`AsdaCooldownCurtainIntervalMs`); ключ `asda.Cooldown.CurtainTotal`.
			int32 Start = CD.IconFrame;
			P->FrameIntervalMs = AsdaCooldownCurtainIntervalMs(CD.RemainMs, Start, CurtainTotalFrames - 1);
			P->StartFrame      = Start;
			P->RestartAnim();
			P->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
			C.ShownKey = Key; C.ShownState = 1; C.ShownEndS = EndS;
		}
		return true;
	}
	if (CD.State == 2)
	{
		if (bChanged)
		{
			// ★★★ animate = 0 ⇒ ЗАМОРОЖЕННЫЙ кадр `CD.IconFrame`, а не прогон. Раньше здесь стоял
			//   `RestartAnim()`, то есть план всё-таки проигрывался (StartFrame…TotalFrame−1) —
			//   это расходилось с эталоном, где шестой аргумент `uiControl_ShowWithItem` = 0 и
			//   означает «кадры не двигать». `StopAnim()` делает ровно это: ставит UV кадра
			//   `StartFrame` и НЕ взводит тик. Единственный источник state 2 — узел дерева навыков
			//   (`skillAddAndRefreshBars@0xE49A20`: кадр 120, EndFrame 127, TotalFrame 8, 1000 мс).
			P->TotalFrame      = State2TotalFrames;
			P->FrameIntervalMs = (float)State2IntervalMs;
			P->bLoop           = false;
			P->MaxLoops        = 1;
			P->StartFrame      = FMath::Max(0, CD.IconFrame);
			P->StopAnim();
			P->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
			C.ShownKey = Key; C.ShownState = 2; C.ShownEndS = 0.0;
		}
		return true;
	}

	if (!bChanged && C.ShownState == 0 && C.ShownKey == Key) { return false; }   // уже погашено
	C.ShownKey = Key; C.ShownState = 0; C.ShownEndS = 0.0;
	P->SetVisibility(ESlateVisibility::Collapsed);
	return false;
}

void UAsdaHotbarSubsystem::UpdateCooldownVisual(int32 Column, bool bTRow)
{
	UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	// ★ 25.10.2026: те же часы, что у реестра (`EndS` записей и `ShownEndS` шторки сравниваются в одной шкале).
	const double Now = World ? AsdaCooldownNowS(GetGameInstance()) : 0.0;
	UpdateCooldownVisualAt(Column, bTRow, World, Now);   // ★ R4-b 14.11.2026: тело — в `UpdateCooldownVisualAt` (дословно)
}

void UAsdaHotbarSubsystem::UpdateCooldownVisualAt(int32 Column, bool bTRow, const UWorld* World, double Now)
{
	TArray<FAsdaHotbarSlotUi>& Row = bTRow ? TUi : MainUi;
	if (!Row.IsValidIndex(Column)) { return; }
	FAsdaHotbarSlotUi& U = Row[Column];
	const int32 Abs = AbsSlot(Column, bTRow);
	const FAsdaHotbarSlot& S = GetSlot(Abs);

	// ★★★ СТРАХОВКА ОТ «ЗАЛИПШЕЙ» ВСПЫШКИ ГОТОВНОСТИ. Гасит её только собственное событие «анимация
	//   завершена» у одноразового плана; если данных скина не хватает (`CanAnimate() == false`),
	//   событие не придёт НИКОГДА, и слот остаётся с ярким оверлеем — визуально «навык в откате».
	//   Дублируем гашение по времени: прогон длится ReadyFlashMs, даём запас 250 мс.
	if (U.FlashEndS > 0.0 && World && Now >= U.FlashEndS)
	{
		U.FlashEndS = 0.0;
		SetVis(U.Flash, false);
	}

	if (!U.Cool.Plane) { return; }

	const FAsdaHotbarCooldown CD = S.IsEmpty() ? FAsdaHotbarCooldown() : FindCooldown(S);
	if (DriveCurtain(U.Cool, Abs, CD, Now))
	{
		if (CD.State == 1)
		{
			WasCoolingSlots.Add(Abs);
			// ★ Вспышка «готово» и шторка одновременно на одном слоте — бессмыслица: гасим остаток
			//   прошлого прогона, если он ещё виден (эталон при показе шторки тоже сначала
			//   `CControl_CloseHide` — `hudSlotUpdateCooldown@0xDF23D0`, ui_core.c:68765).
			if (U.FlashEndS > 0.0) { U.FlashEndS = 0.0; SetVis(U.Flash, false); }
		}
		return;
	}

	// Перезарядка кончилась. ★ Д6: в срезе эталона вспышка `CoolTimeAfter_Effect` НИГДЕ не
	//   показывается (контролы 73031..73043 только CloseHide-ятся), хотя ресурс есть и роль скина
	//   прямая — «после кулдауна». Проигрываем её по факту окончания; вернуть эталон:
	//   asda.Quick.LegacyNoReadyFlash 1.
	if (WasCoolingSlots.Contains(Abs))
	{
		WasCoolingSlots.Remove(Abs);
		if (U.Flash && CVarLegacyNoReadyFlash.GetValueOnAnyThread() == 0)
		{
			U.Flash->TotalFrame      = kFlashFrames;
			U.Flash->FrameIntervalMs = ReadyFlashMs;
			U.Flash->bLoop           = false;
			U.Flash->MaxLoops        = 1;
			U.Flash->StartFrame      = 0;
			U.Flash->RestartAnim();
			U.Flash->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
			U.FlashEndS = Now + (double)ReadyFlashMs / 1000.0 + 0.25;
		}
	}
}


// План-образец шторки: первый попавшийся живой `QUICK_NEW_COOLn`. Все 25 одинаковы по скину
//   (`CoolTime_Effect`), поэтому годится любой; берём первый непустой, чтобы не зависеть от того,
//   разобрана ли уже конкретная колонка.
const UAsdaAnimatedPlane* UAsdaHotbarSubsystem::GetCurtainTemplate() const
{
	for (const FAsdaHotbarSlotUi& U : MainUi) { if (U.Cool.Plane) { return U.Cool.Plane; } }
	for (const FAsdaHotbarSlotUi& U : TUi)    { if (U.Cool.Plane) { return U.Cool.Plane; } }
	return nullptr;
}

// ★ Шторка над ячейкой чужого окна. Эталонного контрола там нет (в `INVEN_WIN` шторок не бывает),
//   поэтому создаём план В РАНТАЙМЕ по геометрии самой ячейки — тем же приёмом, что подписи ячеек
//   панели состава пакета: `WidgetTree->ConstructWidget` + слот-близнец иконки, БЕЗ перегенерации WBP.
//   Весь визуал (атлас, размер кадра, число кадров, UV кадра 0, Diffuse B4000000) копируется с
//   плана панели — ничего не выдумываем (закон `no-invented-ui`).
UAsdaAnimatedPlane* UAsdaHotbarSubsystem::MakeCurtainOverCell(UUserWidget* Host, UWidget* CellIcon,
                                                              const TCHAR* NameHint) const
{
	const UAsdaAnimatedPlane* Tpl = GetCurtainTemplate();
	if (!Host || !Host->WidgetTree || !CellIcon || !Tpl) { return nullptr; }
	UCanvasPanel* Canvas = Cast<UCanvasPanel>(CellIcon->GetParent());
	const UCanvasPanelSlot* SrcSlot = AsdaUi::SlotOf(CellIcon);
	if (!Canvas || !SrcSlot) { return nullptr; }

	UAsdaAnimatedPlane* P = Host->WidgetTree->ConstructWidget<UAsdaAnimatedPlane>(
		UAsdaAnimatedPlane::StaticClass(),
		MakeUniqueObjectName(Host, UAsdaAnimatedPlane::StaticClass(), FName(NameHint)));
	if (!P) { return nullptr; }

	// Визуал — 1:1 с образцом панели.
	P->SetBrush(Tpl->GetBrush());
	P->SetColorAndOpacity(Tpl->GetColorAndOpacity());
	P->FrameTexture     = Tpl->FrameTexture;
	P->FrameSizePx      = Tpl->FrameSizePx;
	P->TotalFrame       = Tpl->TotalFrame;
	P->FrameIntervalMs  = Tpl->FrameIntervalMs;
	P->BaseUV           = Tpl->BaseUV;
	P->bLoop            = false;    // управляем вручную, как и шторками панели
	P->MaxLoops         = 1;
	P->StartFrame       = 0;
	// Шторка не должна отбирать мышь у ячейки: клики по ячейкам инвентаря ловятся опросом курсора.
	P->SetVisibility(ESlateVisibility::Collapsed);

	if (UCanvasPanelSlot* CS = Canvas->AddChildToCanvas(P))
	{
		CS->SetAnchors(SrcSlot->GetAnchors());
		CS->SetAlignment(SrcSlot->GetAlignment());
		CS->SetOffsets(SrcSlot->GetOffsets());
		CS->SetZOrder(SrcSlot->GetZOrder() + 1);   // ПОВЕРХ иконки, как `QUICK_NEW_COOLn` над слотом
	}
	return P;
}

// ★★★ ОФОРМЛЕНИЕ СЧЁТЧИКА СТАКА 1:1 ПО ДАННЫМ (подробности — в объявлении в .h).
//   EXS слота: `TextPos = "0 0"`, `TextColor = FFFFFFFF`, **`TextAlign = "9"`** = 0x1 hCenter | 0x8 vBottom.
//   Импортёр взял маску из `CaptionInfo` (=5, центр+центр) — маску чиним здесь.
//   ★★★ ИСПРАВЛЕНО 2026-08-10 (К1 аудита `code_dedup_audit_2.md`) — ЧТО ЗНАЧИТ `TextOutLine`.
//   БЫЛО (ОТМЕНЕНО): «свойство скина `NoImage_icon` `TextOutLine = "1"` ⇒ чёрная обводка 1 px».
//   ПО БИНАРЮ: `TextOutLine` — это поле контрола **925** (`cfg_WriteTextOutLine @0x12E4D90`,
//   ui_core.c:110573), и единственное его действие в `cControl_RenderLabel` (ui_core.c:110741-110746):
//       textDrawFlags = field924;
//       if (field925 == 1) textDrawFlags |= field926 | 0x2010;   // перенос по словам + клип
//       else               textDrawFlags |= field926 | 0x100;
//   То есть 925 выбирает ФЛАГИ ВЫВОДА (перенос+обрезка рамкой), а НЕ обводку. Обводку/жирность/тень
//   даёт поле **926** (`CClass_SetField926`): `dword_800000` = обводка, `0x200000` = жирный,
//   `0x400000` = тень. ★ ПОПРАВКА 26.10.2026: эталон счётчику поле 926 СТАВИТ — `CClass_SetField926(slot, 0x800000)`
//   (ui_core.c:65971, второй ряд :66110) ⇒ в оригинале у числа ЕСТЬ обводка (прежнее «обводки нет» — ошибка чтения).
void UAsdaHotbarSubsystem::ApplyStackTextStyle(UTextBlock* Tb, const FAsdaHotbarSlotUi& Ui) const
{
	if (!Tb) { return; }
	// Однострочное число: autowrap в CanvasPanel ломает выравнивание (закон ui-autowrap-canvas-gotcha).
	Tb->SetAutoWrapText(false);
	// Горизонталь — бит 0x1 маски.
	Tb->SetJustification(ETextJustify::Center);

	// Кегль. Обводка — по эталону (поле 926 = 0x800000, см. поправку выше), через общую точку ниже.
	FSlateFontInfo Font = Tb->GetFont();
	Font.Size = FMath::Max(1, StackCountFontPx);
	// ★ [ОРИГИНАЛ] 26.10.2026 (решение владельца): чёрная обводка 1 px числа стопки (эталон: поле 926 = 0x800000,
	//   ui_core.c:65971) — общая точка
	//   `AsdaItemCount::ApplyOutline` (ключ `asda.UI.ItemCountOutline`, 0 = прежние `StackOutlinePx`/`StackOutlineColor`).
	//   Высоту строки ниже меряем тем же шрифтом: замер Slate идёт без обводки, позиция числа не меняется.
	AsdaItemCount::ApplyOutline(Font, StackOutlinePx, StackOutlineColor);
	Tb->SetFont(Font);
	// Обводка Slate сдвигает заливку цифры на её толщину вниз (ревизия wave_1026 Н3) — возвращаем на место эталона.
	AsdaItemCount::CompensateGlyph(Tb, Font.OutlineSettings.OutlineSize);

	// Вертикаль — бит 0x8: строка прижимается к НИЗУ ячейки 32×32. UMG рисует строку от ВЕРХА бокса,
	//   поэтому считаем верх сами по реально измеренной высоте строки этого шрифта.
	UCanvasPanelSlot* CS = AsdaUi::SlotOf(Tb);
	if (!CS) { return; }
	// ★ D4 (дедуп UI 2026-08-08): замер высоты строки вынесен в AsdaUi::MeasureLineHeight (общее ядро
	//   с UAsdaMiniStateSubsystem::ApplyJuiTextAlign). Формула Top ниже — специфика хотбара, остаётся тут.
	// ★ 26.10.2026: высота строки — шрифтом БЕЗ обводки. `GetMaxCharacterHeight` с обводкой 1 px даёт строку на 1 px выше
	//   (`SlateFontRenderer.cpp:212-214`; замер самотеста `asda.SelfTest.ItemCount`: бокс 10 → 11, верх −1) — бокс держим
	//   прежним, а сдвиг заливки от обводки снимает `AsdaItemCount::CompensateGlyph` выше.
	FSlateFontInfo MeasureFont = Font;
	MeasureFont.OutlineSettings.OutlineSize = 0;
	const float LineH = AsdaUi::MeasureLineHeight(MeasureFont, (float)Font.Size);
	const float CellH = (Ui.IconBaseSize.Y > 0.f) ? Ui.IconBaseSize.Y : 32.f;
	const float CellW = (Ui.IconBaseSize.X > 0.f) ? Ui.IconBaseSize.X : 32.f;
	FMargin O = CS->GetOffsets();
	O.Left   = Ui.IconBasePos.X;                                        // TextPos = "0 0" ⇒ без сдвига
	O.Top    = Ui.IconBasePos.Y + FMath::Max(0.f, CellH - LineH - StackBottomPadPx);
	O.Right  = CellW;                                                   // ширина = ширина ячейки (центрирование)
	O.Bottom = LineH;
	CS->SetOffsets(O);
}

void UAsdaHotbarSubsystem::RefreshStackTextStyle()
{
	for (const FAsdaHotbarSlotUi& U : MainUi) { ApplyStackTextStyle(U.StackText, U); }
	for (const FAsdaHotbarSlotUi& U : TUi)    { ApplyStackTextStyle(U.StackText, U); }
	UE_LOG(LogAsdaQuick, Log, TEXT("[хотбар] счётчик стака: кегль %d, обводка %d px, выравнивание центр/низ (EXS TextAlign=9)"),
		StackCountFontPx, AsdaItemCount::OutlinePx(StackOutlinePx));   // действующая толщина (общая точка числа стопки)
}

// Диагностика источников иконок: тип 2 — эмоция (код), 3 — навык (ИНДЕКС записи skilltooltip),
//   4 — навык соулмейта (код навыка). Показывает найденное имя и наличие самой текстуры.
FString UAsdaHotbarSubsystem::ProbeIcon(int32 Type, int32 IdOrCode) const
{
	FString Tex; bool bFull = false; bool bOk = false;
	switch (Type)
	{
	case 2: bOk = ResolveEmotionIcon(IdOrCode, Tex, bFull); break;
	case 3: bOk = ResolveSkillIcon(IdOrCode, Tex);          break;
	case 4: bOk = ResolveSoulSkillIcon(IdOrCode, Tex);      break;
	default: return FString::Printf(TEXT("тип %d здесь не разбирается (1 = предмет — через таблицу предметов)"), Type);
	}
	if (!bOk || Tex.IsEmpty())
	{
		return FString::Printf(TEXT("тип %d id/код %d: имя иконки в таблицах НЕ найдено"), Type, IdOrCode);
	}
	UAsdaTextureSubsystem* TexMgr = AsdaTexMgr();   // ★ Аудит №3 (B8): короткий доступ к менеджеру текстур
	UTexture2D* T = TexMgr ? TexMgr->LoadTextureCached(AsdaAssetPaths::JuiTextureObject(Tex)) : nullptr;   // ★ волна 2 п. 7
	return FString::Printf(TEXT("тип %d id/код %d → иконка '%s'%s, текстура %s"),
		Type, IdOrCode, *Tex, bFull ? TEXT(" (64×64)") : TEXT(""),
		T ? TEXT("НАЙДЕНА") : TEXT("не импортирована"));
}

void UAsdaHotbarSubsystem::RefreshRow(bool bTRow)
{
	const int32 N = bTRow ? kTRowCols : kColsPerPage;
	for (int32 C = 0; C < N; ++C) { UpdateSlotVisual(C, bTRow); }
}

// Порт `CHotbar_UpdateAnimSlots@0xDF6980`: 13 слотов текущей страницы; выбор имени — в `ApplyIconToImage`.
void UAsdaHotbarSubsystem::UpdateSitStandIcons()
{
	if (!bControlsReady) { return; }
	RefreshRow(false);
}

FString UAsdaHotbarSubsystem::SitStandIconName(const FString& Tag) const
{
	if (Tag != TEXT("act_updown") && Tag != TEXT("act_standup")) { return Tag; }
	if (!AAsdaPlayerController::IsSitToggleEtalon()) { return Tag; }
	// Состояние действия своего персонажа (`*(g_LocalPlayer + 190204)` = `+47551`): 7 садится, 8 сидит.
	const UGameInstance* GI = GetGameInstance();
	const APlayerController* PC = GI ? GI->GetFirstLocalPlayerController() : nullptr;
	const AAsdaPlayerCharacter* Ch = PC ? Cast<AAsdaPlayerCharacter>(PC->GetPawn()) : nullptr;
	const int32 St = Ch ? Ch->GetEtalonSitState() : 0;
	return (St == 7 || St == 8) ? FString(TEXT("act_standup")) : FString(TEXT("act_updown"));
}

void UAsdaHotbarSubsystem::RefreshPage()
{
	if (!bControlsReady) { return; }
	RefreshRow(false);
	RefreshRow(true);
}

// Эталон: 5 цифр в одном месте, видима ровно одна. `Hotbar_Init_Page2` по умолчанию зажигает цифру 0.
// ★ Публичная обёртка хит-теста для ИНВЕНТАРЯ (бросок предмета в квикслот). Внутренний
//   `HitTestSlot` отдаёт ещё ряд и колонку — здесь нужен только абсолютный индекс.
int32 UAsdaHotbarSubsystem::HitTestSlotUnderCursor() const
{
	bool bTRow = false;
	int32 Col = -1;
	return HitTestSlot(bTRow, Col);
}

// ★ Живой прямоугольник иконки слота (обоснование — в заголовке). Геометрия берётся у ТОГО ЖЕ
//   `U.Icon`, по которому считает `HitTestSlot`, — «куда бросать» и «где ловится» совпадают.
bool UAsdaHotbarSubsystem::GetSlotIconAbsRect(int32 AbsIndex, FVector2D& OutMin, FVector2D& OutMax) const
{
	const int32 MainCount = kColsPerPage * kMainPages;
	const UImage* Ic = nullptr;
	if (AbsIndex >= 0 && AbsIndex < MainCount)
	{
		// Основной ряд показывает ТОЛЬКО текущую страницу — слот чужой страницы на экране не лежит.
		if (AbsIndex / kColsPerPage != CurrentPage) { return false; }
		const int32 Col = AbsIndex % kColsPerPage;
		Ic = MainUi.IsValidIndex(Col) ? MainUi[Col].Icon.Get() : nullptr;
	}
	else if (AbsIndex >= MainCount && AbsIndex < MainCount + kTRowCols)
	{
		if (!bSecondRowVisible) { return false; }
		const int32 Col = AbsIndex - MainCount;
		Ic = TUi.IsValidIndex(Col) ? TUi[Col].Icon.Get() : nullptr;
	}
	if (!Ic) { return false; }
	return AsdaUi::WidgetAbsRect(Ic, OutMin, OutMax);
}

void UAsdaHotbarSubsystem::UpdatePageDigits()
{
	if (PageDigits.Num() < 5) { return; }
	const bool bLegacy = (CVarLegacyPageDigit.GetValueOnAnyThread() != 0);
	// ★ Д3: у эталонного `hotbarSetCurrentPage` нет проверки границ nPrevPage (в Hotbar_SetPage есть) —
	//   клампим.
	const int32 Prev = FMath::Clamp(PrevPage, 0, kMainPages - 1);
	const int32 Cur  = FMath::Clamp(CurrentPage, 0, kMainPages - 1);
	for (int32 D = 0; D < 5; ++D) { SetVis(PageDigits[D], false); }
	// ★ Д2: оригинал в `hotbarSetCurrentPage` гасит и тут же СНОВА показывает цифру nPrevPage.
	SetVis(PageDigits[bLegacy ? Prev : Cur], true);
}

// ══════════════════════════════ ГЛИФЫ КЛАВИШ ═══════════════════════════════════════════════════
//   Эталон `hotkeyBar_RebuildKeybindSlots@0xDF5BF0` / `uiHotkeyBarRefreshKeyIcons@0xDF5F20`:
//     на каждый из 12 слотов ряда — снять прежние клоны, взять бинд действия (slot+6 / slot+18),
//     клонировать глиф модификатора (индекс 60+mod) и глиф самой клавиши (idnum−73080),
//     положить их РЕБЁНКОМ слота, сдвинув основной глиф на ширину глифа модификатора;
//     бинда нет → клон глифа «none» (73144).
//   ★ Слот ОРУЖИЯ (колонка 12) клавиши НЕ имеет — эталон строит подписи только для 12 слотов.
//   ⚠ Сами хоткеи (обработка нажатий) делает ДРУГАЯ подсистема; здесь только ПОДПИСИ.
void UAsdaHotbarSubsystem::RebuildKeyHints()
{
	if (!bControlsReady) { return; }
	// Конфиг клавиш живёт у контроллера игрока (`AAsdaPlayerController::Keys`) — берём ТОЛЬКО НА ЧТЕНИЕ.
	//   ⚠ Загрузку `system/Shortcutkey.ini` и обработку нажатий делает система клавиш; хотбар лишь
	//   рисует подписи по тому, что там уже лежит.
	UAsdaKeybindConfig* KB = nullptr;
	if (UWorld* W = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr)
	{
		if (AAsdaPlayerController* Pc = Cast<AAsdaPlayerController>(W->GetFirstPlayerController()))
		{
			KB = Pc->Keys;
		}
	}

	auto GlyphByIdNum = [this](int32 IdNum) -> UImage*
	{
		TObjectPtr<UImage>* P = KeyGlyphs.Find(FString::FromInt(IdNum));
		return P ? P->Get() : nullptr;
	};

	auto BuildRow = [&](TArray<FAsdaHotbarSlotUi>& Row, int32 FirstCmd, const TCHAR* Tag)
	{
		for (int32 C = 0; C < 12 && C < Row.Num(); ++C)
		{
			FAsdaHotbarSlotUi& U = Row[C];
			SetVis(U.KeyGlyph, false);
			SetVis(U.KeyModGlyph, false);
			if (!U.Icon) { continue; }

			int32 Vk = 0, Mod = 0;
			if (KB)
			{
				if (const FAsdaKeyBind* B = KB->Binds.Find(FirstCmd + C)) { Vk = B->Vk; Mod = (int32)B->Mod; }
			}
			const int32 KeyIdNum = (Vk != 0) ? VkToGlyphIdNum(Vk) : 73144;   // 73144 = глиф «none»
			UImage* SrcKey = GlyphByIdNum(KeyIdNum != 0 ? KeyIdNum : 73144);
			UImage* SrcMod = (Vk != 0 && Mod != 0) ? GlyphByIdNum(73080 + 60 + Mod) : nullptr;
			if (!SrcKey) { continue; }

			// ★ Клоны кладём В ГРУППУ СЛОТА (эталон: родитель клона = сам контрол слота), иначе
			//   локальные координаты будут считаться в чужом канвасе. Создаём один раз и
			//   переиспользуем: эталон пересоздаёт каждый раз, у нас это только мусор в дереве.
			if (!U.KeyGlyph)    { U.KeyGlyph    = CloneImage(SrcKey, TEXT("QuickKeyGlyph"), U.Group); }
			// ⚠ Клон модификатора создаём из ЕГО источника (раньше брался SrcKey — браш потом
			//   перетирался, но размер клона наследовался от чужого глифа).
			if (!U.KeyModGlyph && SrcMod) { U.KeyModGlyph = CloneImage(SrcMod, TEXT("QuickKeyModGlyph"), U.Group); }
			if (!U.KeyGlyph) { continue; }

			// Эталон: глиф модификатора рисуется первым, основной сдвигается на его ШИРИНУ.
			float ShiftX = 0.f;
			if (SrcMod && U.KeyModGlyph)
			{
				FVector2D SP, SS;
				GetSlotRect(SrcMod, SP, SS);
				U.KeyModGlyph->SetBrush(SrcMod->GetBrush());
				if (UCanvasPanelSlot* CS = AsdaUi::SlotOf(U.KeyModGlyph))
				{
					CS->SetOffsets(FMargin(U.IconBasePos.X, U.IconBasePos.Y, SS.X, SS.Y));
				}
				SetVis(U.KeyModGlyph, true);
				ShiftX = SS.X;
			}
			{
				FVector2D SP, SS;
				GetSlotRect(SrcKey, SP, SS);
				U.KeyGlyph->SetBrush(SrcKey->GetBrush());
				if (UCanvasPanelSlot* CS = AsdaUi::SlotOf(U.KeyGlyph))
				{
					CS->SetOffsets(FMargin(U.IconBasePos.X + ShiftX, U.IconBasePos.Y, SS.X, SS.Y));
				}
				SetVis(U.KeyGlyph, true);
			}
		}
		UE_LOG(LogAsdaQuick, Verbose, TEXT("[хотбар] подписи клавиш %s пересобраны (конфиг клавиш %s)"),
			Tag, KB ? TEXT("найден") : TEXT("НЕ найден — показан глиф «none»"));
	};

	BuildRow(MainUi, /*BAR SLOT 1*/ 6,  TEXT("ряда 1"));
	BuildRow(TUi,    /*NEW BAR SLOT 1*/ 18, TEXT("T-ряда"));
}

// ══════════════════════════════ СТРАНИЦЫ ═══════════════════════════════════════════════════════
// Эталон `Hotbar_PageUpDown@0xDEEAD0`: +1 с wrap 5→0, −1 с wrap <0→4; та же блокировка инвентарём.
void UAsdaHotbarSubsystem::PageUpDown(bool bUp)
{
	int32 P = CurrentPage + (bUp ? 1 : -1);
	if (P >= kMainPages) { P = 0; }
	if (P < 0)           { P = kMainPages - 1; }
	SetPage(P);
}

// Эталон `Hotbar_SetPage@0xDEF110`.
void UAsdaHotbarSubsystem::SetPage(int32 NewPage)
{
	if (NewPage < 0 || NewPage >= kMainPages) { return; }
	// ★★★ ИСПРАВЛЕНО 2026-08-15 (та же ошибка чтения, что и в `IsActivationBlocked`).
	//   Эталон `Hotbar_SetPage@0xDEF110` (`ui_core.c:67722`): `CObject_GetField254(g_pInventoryWnd)`
	//   — это флаг «открыт барабан гачи» (`set_oFE@0xD1F7D0`), а НЕ «открыт инвентарь».
	//   Прежний код запрещал F1..F5 при открытом инвентаре — в оригинале страницы там переключаются.
	{
		UGameInstance* GI = GetGameInstance();
		const UAsdaBoosterPanelSubsystem* Booster = GI ? GI->GetSubsystem<UAsdaBoosterPanelSubsystem>() : nullptr;
		if (Booster && Booster->IsWindowOpen())
		{
			UE_LOG(LogAsdaQuick, Log, TEXT("[хотбар] смена страницы отклонена: открыт барабан гачи"));
			return;
		}
	}
	if (CurrentPage == NewPage) { RefreshPage(); return; }

	// Боевое состояние решает, какой веткой идти (эталон `charIsActionStateNonCombat`). Ближайший
	//   эквивалент в порту — боевая стойка + активный замах: ровно они означают «персонаж в бою».
	bool bNonCombat = true;
	int32 TargetRenderId = -1;
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UAsdaWorldSubsystem* WS = GI->GetSubsystem<UAsdaWorldSubsystem>())
		{
			if (AAsdaPlayerCharacter* PC = Cast<AAsdaPlayerCharacter>(WS->LocalPlayerActor))
			{
				bNonCombat = !PC->IsCombatStance() && !PC->IsAttackAnimActive();
			}
		}
		// Эталон передаёт в отложенное действие `targetRenderId` локального игрока: −1 = цели нет
		//   (тогда играется субрежим 20), иначе — субрежим 22 + отправка действия.
		if (UWorld* W = GI->GetWorld())
		{
			if (AAsdaPlayerController* Pc = Cast<AAsdaPlayerController>(W->GetFirstPlayerController()))
			{
				TargetRenderId = Pc->TargetActor.IsValid() ? 1 : -1;
			}
		}
	}

	if (bNonCombat)
	{
		// ★★★ ВНЕ БОЯ страницу применяет НЕ хотбар, а ПЕРСОНАЖ: эталон ставит состояние 104 (0x68)
		//   c параметрами (page, targetRenderId), и машина состояний на следующем тике вызывает
		//   hotbarSetCurrentPage(page) + анимацию смены оружия (субрежим 20, либо 22 при цели).
		//   Именно поэтому у каждой страницы СВОЙ слот оружия и смена страницы = смена набора.
		QueuePageAction(NewPage, TargetRenderId);
		return;
	}

	// В БОЮ (и когда персонаж не в блокирующем состоянии) страница меняется сразу.
	PrevPage = CurrentPage;
	CurrentPage = NewPage;
	UpdatePageDigits();
	PrevPage = CurrentPage;
	RefreshPage();
	UseActiveWeaponSlot();
	UE_LOG(LogAsdaQuick, Log, TEXT("[хотбар] страница → %d (боевая ветка, без анимации смены оружия)"), CurrentPage + 1);
}

void UAsdaHotbarSubsystem::QueuePageAction(int32 Page, int32 TargetRenderId)
{
	bPageActionPending = true;
	PendingPage = Page;
	PendingTargetRenderId = TargetRenderId;
	UE_LOG(LogAsdaQuick, Log, TEXT("[хотбар] отложенное действие персонажа 104: страница %d, цель renderId=%d"),
		Page + 1, TargetRenderId);
}

void UAsdaHotbarSubsystem::TickPageAction()
{
	if (!bPageActionPending) { return; }
	bPageActionPending = false;

	// Эталон, case 0x68 машины состояний персонажа (AsdaStory_game.c:83355):
	//   1) hotbarSetCurrentPage(page) — сама смена страницы + перерисовка + АВТОПРИМЕНЕНИЕ оружия;
	//   2) в бою дальше ничего;
	//   3) targetRenderId == -1 → анимация субрежима 20 (смена оружия/стойка);
	//      иначе → отправка действия + субрежим 22.
	if (CurrentPage != PendingPage && PendingPage >= 0 && PendingPage < kMainPages)
	{
		PrevPage = CurrentPage;
		CurrentPage = PendingPage;
		UpdatePageDigits();
		PrevPage = CurrentPage;
		RefreshPage();
		UseActiveWeaponSlot();
	}

	if (UGameInstance* GI = GetGameInstance())
	{
		if (UAsdaWorldSubsystem* WS = GI->GetSubsystem<UAsdaWorldSubsystem>())
		{
			if (AAsdaPlayerCharacter* PC = Cast<AAsdaPlayerCharacter>(WS->LocalPlayerActor))
			{
				if (!PC->IsCombatStance() && !PC->IsAttackAnimActive())
				{
					// ★ Субрежим 20 = БОЕВАЯ СТОЙКА со сменой набора анимаций под оружие
					//   (память weapon-anim-sets; в порту это `SetCombatStance`, клип idle01).
					//   При наличии цели эталон играет субрежим 22 и дополнительно шлёт действие
					//   `netSabzil_SendActionRaw` — сборка этого пакета принадлежит боевой системе,
					//   здесь только стойка (см. отчёт quick_win_port.md).
					PC->SetCombatStance(true);
					UE_LOG(LogAsdaQuick, Log, TEXT("[хотбар] смена страницы: субрежим %d (стойка/набор оружия)"),
						PendingTargetRenderId == -1 ? 20 : 22);
				}
			}
		}
	}
	UE_LOG(LogAsdaQuick, Log, TEXT("[хотбар] страница применена: %d (набор оружия страницы задействован)"), CurrentPage + 1);
}
