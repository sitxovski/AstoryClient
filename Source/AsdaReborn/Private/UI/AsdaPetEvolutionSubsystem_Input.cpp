// Copyright (c) 2026 @sitxovski (Discord)
// AsdaPetEvolutionSubsystem_Input.cpp — ★ R3-c 12.11.2026 (разрез AsdaPetEvolutionSubsystem.cpp, дословный перенос): рецепты, мышь, наведение и клики, события питомцев, результат слияния, доступ к подсистемам, тексты.
//   Общее для частей — в AsdaPetEvolutionSubsystem_Private.h; шапка и реверс — в AsdaPetEvolutionSubsystem.cpp.
#include "UI/AsdaPetEvolutionSubsystem.h"
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping

#include "UI/AsdaJuiWindowHandle.h"
#include "UI/AsdaNotice.h"
#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaUiHelpers.h"
#include "UI/AsdaPetCageSubsystem.h"
#include "Items/AsdaItemTableSubsystem.h"
#include "Items/AsdaItemDbRows.h"
#include "Pet/AsdaPetSubsystem.h"
#include "Pet/AsdaPetTableRows.h"
#include "Net/NetOpcodes.h"

#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Engine/GameInstance.h"
#include "AsdaPetEvolutionSubsystem_Private.h"   // ★ R3-c 12.11.2026: общее для частей AsdaPetEvolutionSubsystem*.cpp

// ═══════════════════════════════════ РЕЦЕПТЫ ══════════════════════════════════════════════════
const FAsdaPetEvoStageRow* UAsdaPetEvolutionSubsystem::CurrentMap() const
{
	UAsdaPetSubsystem* P = Pets();
	const FAsdaPetSlot* Pet = Target();
	if (!P || !Pet) { return nullptr; }
	FAsdaPetRow Pr;
	if (!PetRow(Pet->PetIndex, Pr)) { return nullptr; }
	// ★ `pettable + 468` (колонка `WeaponClass`) — МИСНЕЙМ, это КЛЮЧ КАРТЫ РАЗВИТИЯ
	//   (`petFindRecordById(this, *(DWORD*)(v11 + 468))`, ui_core.c:90469). Имя не правим:
	//   заголовок таблицы генерируемый (закон `itemdb-header-generated-not-manual`).
	return P->GetEvoStage(Pr.WeaponClass);
}

const FAsdaPetUnionRecipeRow* UAsdaPetEvolutionSubsystem::CurrentRecipe() const
{
	UAsdaPetSubsystem* P = Pets();
	const FAsdaPetEvoStageRow* Map = CurrentMap();
	if (!P || !Map || CurStage < 0 || !Map->StuffIdx.IsValidIndex(CurStage)) { return nullptr; }
	const int32 Idx = Map->StuffIdx[CurStage];
	// −1 = базовая стадия: рецепта нет, эволюцией её не получить (см. В3 в шапке).
	if (Idx < 0) { return nullptr; }
	return P->GetUnionRecipe(Idx);
}

// Порт `craftFindRecipeMatch` — предикат взят у близнеца `uiRefreshEquipIconGrid` (ui_core.c:90130).
int32 UAsdaPetEvolutionSubsystem::FindRecipeMatch(int32 ReqPetIndex, int32 ReqLevel, const int32 (&Exclude)[3]) const
{
	UAsdaPetSubsystem* P = Pets();
	if (!P || ReqPetIndex < 0) { return -1; }
	for (const FAsdaPetSlot& S : P->GetSlots())
	{
		if (S.PetIndex != ReqPetIndex) { continue; }
		if (S.Level < ReqLevel) { continue; }
		if (S.UniqueId == Exclude[0] || S.UniqueId == Exclude[1] || S.UniqueId == Exclude[2]) { continue; }
		return S.UniqueId;
	}
	return -1;
}

// Порт `craftHasAllRecipeMatches@0xEDD800` (item_enhance.c:9881-9906).
bool UAsdaPetEvolutionSubsystem::HasAllRecipeMatches(int32 Stage) const
{
	UAsdaPetSubsystem* P = Pets();
	const FAsdaPetEvoStageRow* Map = CurrentMap();
	if (!P || !Map || !Map->StuffIdx.IsValidIndex(Stage)) { return false; }
	const int32 Idx = Map->StuffIdx[Stage];
	if (Idx < 0) { return false; }                     // `if (!EntryByKey36) return 0;` (:9894)
	const FAsdaPetUnionRecipeRow* R = P->GetUnionRecipe(Idx);
	if (!R) { return false; }
	int32 Found[3] = { -1, -1, -1 };
	for (int32 i = 0; i < kMaterials; ++i)
	{
		const int32 ReqT = R->ReqPetIndex.IsValidIndex(i) ? R->ReqPetIndex[i] : -1;
		const int32 ReqL = R->ReqLevel.IsValidIndex(i) ? R->ReqLevel[i] : 0;
		Found[i] = FindRecipeMatch(ReqT, ReqL, Found);
	}
	// `return v8 != -1 && v7 != -1 && v6 != -1;` (:9904)
	return Found[0] != -1 && Found[1] != -1 && Found[2] != -1;
}

// Порт `skillTreeFillPrereqSlots@0xEDFF20` (skill_buff.c:37903-37950).
void UAsdaPetEvolutionSubsystem::FillPrereqSlots()
{
	for (FAsdaPetUnionRow& M : Mats) { M.PickedUid = -1; }   // `row[5] = nullptr` (:37927)
	const FAsdaPetUnionRecipeRow* R = CurrentRecipe();
	if (!R) { return; }
	int32 Found[3] = { -1, -1, -1 };
	for (int32 i = 0; i < kMaterials; ++i)
	{
		const int32 ReqT = R->ReqPetIndex.IsValidIndex(i) ? R->ReqPetIndex[i] : -1;
		const int32 ReqL = R->ReqLevel.IsValidIndex(i) ? R->ReqLevel[i] : 0;
		Found[i] = FindRecipeMatch(ReqT, ReqL, Found);
		if (Found[i] != -1) { Mats[i].PickedUid = Found[i]; }
	}
}

// ═══════════════════════════════════ МЫШЬ ═════════════════════════════════════════════════════
void UAsdaPetEvolutionSubsystem::TickMouse()
{
	UAsdaWindowSubsystem* WM = Windows();
	// Чужой всплывающий слой (`PushTopmost`) перехватывает клик первым — грабля оконной подсистемы.
	//   ★ Свои 43400/43450 сюда не попадают: они не заявляют слой «поверх всего», а
	//     `IsPointerBlockedByTopmost` и так пропускает заявки собственного окна.
	if (WM && WM->IsPointerBlockedByTopmost(kEvoWindowId) && !bGridShown) { return; }

	// ★★★ МОДАЛЬНЫЙ ГЕЙТ (поправка финальной приёмки, СРЕДН-4).
	//   Эталон: захваченный модальный контрол забирает ВЕСЬ диспетч мыши (`ui_core.c:104149-104153`),
	//   и нижние окна событий НЕ ПОЛУЧАЮТ вовсе.
	//   ПОЧЕМУ ОДНОГО БЛОКЕРА НЕ ХВАТАЕТ: полноэкранный блокер из `RefreshModalInput` стоит в SLATE
	//   и ест только те клики, что идут через Slate (настоящие `UButton`). Наш покадровый опрос
	//   спрашивает ГЕОМЕТРИЮ (`AsdaUi::IsCursorOverWidget`) и про модалку не знает — поэтому под
	//   открытым нотисом КНОПКИ были мертвы, а СТАДИИ, СЛОТЫ МАТЕРИАЛОВ и ЯЧЕЙКИ СЕТКИ — живы.
	//   ⚠ Защёлки сбрасываем: иначе нажатие ДО нотиса и отпускание ПОСЛЕ его закрытия сошлись бы
	//   в клик, которого игрок не делал.
	// ★ Дедуп №2, В1-B (U9, 11.10.2026): общая точка модального гейта (ключ отката `asda.UI.ModalGatePolling`).
	if (WM && AsdaUi::IsModalDispatchBlocked(GetGameInstance()))
	{
		Press.Clear();
		Mouse.Reset();
		return;
	}

	// ★ ПОРЯДОК: СНАЧАЛА КЛИКИ, ПОТОМ НАВЕДЕНИЕ. В эталоне события 8 и 0xB/0xD независимы
	//   (`uiSlotWindowProc` — switch), а в порту они разбираются в одном тике, и показ подсказки
	//   раньше разбора клика однажды уже съел клик (живой прогон 21.09: первый клик по слоту
	//   материала терялся, потому что в тот же кадр создавалось окно 43450).
	TickClicks();
	TickHover();
}

// Наведение (событие 8): сетка 43400 и три слота материалов показывают подсказку 43450.
void UAsdaPetEvolutionSubsystem::TickHover()
{
	// ── Наведение (событие 8): сетка 43400 и три слота материалов показывают подсказку 43450 ──
	//   `uiPetWindowOnItemSlotInfo@0xEDFC30` (ui_core.c:90253-90264) + ветки 43517/43518/43519
	//   (ui_core.c:90186-90193). «Вошло/ушло» = СМЕНА узла под курсором.
	int32 HoverUid = -1;
	int32 NewKey = -1;
	if (bGridShown && bGridReady)
	{
		for (int32 i = 0; i < kGridCells; ++i)
		{
			if (Cells[i].PetUid < 0 || !Cells[i].Icon) { continue; }
			if (AsdaUi::IsCursorOverWidget(Cells[i].Icon)) { HoverUid = Cells[i].PetUid; NewKey = 43401 + i; break; }
		}
	}
	if (NewKey < 0 && bWidePage)
	{
		for (int32 m = 0; m < kMaterials; ++m)
		{
			if (Mats[m].PickedUid < 0 || !Mats[m].Icon) { continue; }
			if (AsdaUi::IsCursorOverWidget(Mats[m].Icon)) { HoverUid = Mats[m].PickedUid; NewKey = kMatIconId + m; break; }
		}
	}
	if (NewKey != HoverKey)
	{
		HoverKey = NewKey;
		if (HoverUid >= 0) { ShowTip(HoverUid); } else { HideTip(); }   // событие 0xA гасит (:90181)
	}
}

// ★★★ ЧТО СЕЙЧАС ПОД КУРСОРОМ: −1 ничего, 43401+i ячейка сетки, 43510+j стадия, 43517+m слот.
//   Общая точка нажатия и отпускания. ★ ТОЧНОСТЬ ФОРМУЛИРОВКИ (ревизия волны 5, МЕЛКОЕ-1):
//   эталон парует нажатие и отпускание ФЛАГАМИ `0x10`/`0x20` — `CControl_OnLButtonDown` ставит
//   `|= 0x10` после успешного события (ui_core.c:107298), `CControl_OnLButtonUp` гейтится на
//   `0x10 || 0x20` и делает `&= ~0x10`, `|= 0x20` (:107309-107315), `CControl_OnLButtonClick`
//   гейтится на ту же пару (:107327). Хит-тест отпускания там ВЫЧИСЛЯЕТСЯ и передаётся
//   аргументом события 7 (:107335-107337), но гейтом НЕ является — прежний комментарий это путал.
int32 UAsdaPetEvolutionSubsystem::HitTestUnderCursor() const
{
	if (bGridShown && bGridReady)
	{
		for (int32 i = 0; i < kGridCells; ++i)
		{
			if (!Cells[i].Icon || Cells[i].PetUid < 0) { continue; }
			if (AsdaUi::IsCursorOverWidget(Cells[i].Icon)) { return kGridFirstId + i; }
		}
	}
	for (int32 j = 0; j < kStages; ++j)
	{
		if (!Stages[j].Icon) { continue; }
		if (AsdaUi::IsCursorOverWidget(Stages[j].Icon)) { return kStageIconId + j; }
	}
	if (bWidePage)
	{
		for (int32 m = 0; m < kMaterials; ++m)
		{
			if (!Mats[m].Icon) { continue; }
			if (AsdaUi::IsCursorOverWidget(Mats[m].Icon)) { return kMatIconId + m; }
		}
	}
	return -1;
}

// Клики: событие 0xD (двойной ЛКМ) по ячейке сетки, событие 0xB (одиночный) по стадии и слоту.
//
// ★★★ ЦЕЛЬ ЗАЩЁЛКИВАЕТСЯ НА НАЖАТИИ, А НЕ ПЕРЕСЧИТЫВАЕТСЯ НА ОТПУСКАНИИ.
//   ПОЧЕМУ. Оконная подсистема на нажатии в зону окна ПЕРЕВЕШИВАЕТ окно во вьюпорте
//   (`SetWindowZOrder` → снять и добавить заново). В ЭТОМ ЖЕ кадре у виджетов окна нет валидной
//   кэшированной геометрии, `WidgetAbsRect` отдаёт false, и хит-тест отпускания находит «ничего» —
//   клик теряется молча. Живой прогон 21.09 поймал это дважды подряд: первый клик по слоту
//   материала (прогон 2) и клик по стадии (прогон 3) не срабатывали ровно в тех кадрах, где в лог
//   попадала строка «z 750 → 755 (перевешено в вьюпорте)».
//   Эталону эта беда неведома: у него контролы не пересоздаются, а нажатие и отпускание он и так
//   держит парой через флаги `0x10`/`0x20` (`CControl_OnLButtonDown/Up`, ui_core.c:107277-107315).
//   Защёлка воспроизводит именно эту пару.
void UAsdaPetEvolutionSubsystem::TickClicks()
{
	if (Mouse.bLmbDown)
	{
		// `*(a3 + 1972) |= 0x10` — «нажатие принято». Общая точка `FAsdaPressLatch` (волна 8, п.6).
		// ★ ПОПРАВКА РЕВИЗИИ ВОЛНЫ 8 (КРИТ-1): запоминаем и ТОЧКУ КУРСОРА — она нужна
		//   `ReleaseOverOrStill`, чтобы отличить «геометрии нет» от «курсор ушёл».
		Press.Press(HitTestUnderCursor(), -1, Mouse.Cursor);
		return;
	}
	if (!Mouse.bLmbUp) { return; }

	// `*(a3 + 1972) &= ~0x10` — пара закрыта.
	// ★★★ ПОПРАВКА РЕВИЗИИ ВОЛНЫ 8 (КРИТ-1): СВЕРКА ВЕРНУЛАСЬ. БЫЛО `Press.ReleaseLatched()` —
	//   отпускание засчитывалось ВООБЩЕ БЕЗ проверки того, где курсор.
	// ★ ПОПРАВКА ФИНАЛЬНОЙ ПРИЁМКИ (мелкое 1): ПРЕЖНЯЯ ССЫЛКА БЫЛА НЕТОЧНОЙ. Здесь было написано,
	//   что «`CControl_OnLButtonUp`/`OnRButtonUp` отказываются работать при `a2 == -1`». Гейт
	//   `a2 == -1` есть ТОЛЬКО у правой кнопки (`CControl_OnRButtonUp`, ui_core.c:107391);
	//   у левой (`CControl_OnLButtonUp`, ui_core.c:107307-107313) такой проверки НЕТ — там условие
	//   только по флагам `0x10`/`0x20`. Проверено чтением обеих функций.
	//   ПОВЕДЕНИЕ КОДА ОТ ЭТОГО НЕ МЕНЯЕТСЯ И ОСТАЁТСЯ ВЕРНЫМ: у эталона пара держится флагами
	//   НА САМОМ УЗЛЕ, то есть отпускание и так адресовано тому контролу, который принял нажатие —
	//   это равнозначно нашей сверке «отпустили над тем же узлом». Наш запасной путь нужен ровно
	//   там, где у эталона проблемы нет вовсе: в кадре перевешивания окна во вьюпорте геометрии
	//   нет и свежий хит-тест отдаёт −1, хотя курсор стоит на узле.
	//   ⇒ `ReleaseOverOrStill`: сверка со свежим хит-тестом, когда он валиден, и падение на
	//   защёлку ТОЛЬКО при −1 И если курсор не уходил дальше порога от точки нажатия.
	const int32 Key = Press.ReleaseOverOrStill(HitTestUnderCursor(), Mouse.Cursor);
	if (Key < 0) { return; }

	// Сетка 43400 — ДВОЙНОЙ ЛКМ (событие 13, см. В1 в шапке). `FAsdaMouseFrame` двойного клика
	//   не даёт, поэтому распознаём вручную по паре «та же ячейка + интервал».
	if (Key >= kGridFirstId && Key < kGridFirstId + kGridCells)
	{
		const int32 i = Key - kGridFirstId;
		if (GridDbl.Click(i)) { OnGridCellPicked(i); }   // ★ U1 (В2-A): та же ячейка в окне Windows; третий клик — снова первый
		return;
	}
	// Стадии 43510+j — ОДИНОЧНЫЙ ЛКМ (событие 0xB, ui_core.c:90208).
	if (Key >= kStageIconId && Key < kStageIconId + kStages)
	{
		OnStageClicked(Key - kStageIconId);
		return;
	}
	// Слоты материалов 43517+m — ОДИНОЧНЫЙ ЛКМ (ui_core.c:90202-90207).
	if (Key >= kMatIconId && Key < kMatIconId + kMaterials)
	{
		OnMaterialSlotClicked(Key - kMatIconId);
	}
}

// ★ U1 (В2-A дедупа №2): копия распознавателя с прежним порогом окна — для самотеста (описание — у объявления).
FAsdaDblClick UAsdaPetEvolutionSubsystem::DebugDblClickProbe() const
{
	return GridDbl;
}

// ═══════════════════════════════ СОБЫТИЯ МОДЕЛИ (пакеты) ══════════════════════════════════════
void UAsdaPetEvolutionSubsystem::DrainPetEvents()
{
	// ★ ВОЛНА 8 (дубли, пункт 7): слив очереди — общая точка `AsdaPetUi::DrainEvents`
	//   (это тело было скопировано в четыре окна). Хвост у каждого окна свой — он ниже.
	AsdaPetUi::DrainEvents(PetEvents,
		[this](int32 Op, int32 Res, int32 Uid) { OnPetEvent(Op, Res, Uid); });
}

void UAsdaPetEvolutionSubsystem::OnPetEvent(int32 Opcode, int32 Result, int32 PetUniqueId)
{
	if (Opcode == OP_PET_UNION_RESULT) { OnUnionResult(Result, PetUniqueId); return; }
	// Любой другой пет-пакет мог изменить список — если окно открыто, перерисуем панели.
	if (bVisible) { RefreshAll(); }
}

// Порт `petUI_OnPetDataPacket@0xEE0920` (npc_mob_pet.c:7645-7689).
void UAsdaPetEvolutionSubsystem::OnUnionResult(int32 Result, int32 NewPetUid)
{
	// ★★★ ПРОЛОГ ВЫПОЛНЯЕТСЯ НА ЛЮБОМ КОДЕ, ДО проверки `result == 1` (npc_mob_pet.c:7650-7653):
	//   снять «запрос в полёте», снять признак подтверждения, спрятать 43883. Без него окно
	//   зависло бы на отказах 3/4/7 навсегда.
	// ★★★ ВОЛНА 9, ПОПРАВКА РЕВИЗИИ: ГОНКА ЛЕЧИТСЯ СВЕРКОЙ ЭХА, а не только объявляется.
	//   Прежняя запись «номера запроса в пет-протоколе НЕТ ни в одном теле» была ЛОЖНЫМ «нет»:
	//   ответ 6113 несёт младшие 16 бит uid трёх материалов (`GameServer.c:35984-35985`).
	//   Разошлось эхо ⇒ это ответ на ПРЕДЫДУЩИЙ 6112, и применять его нельзя.
	//   ⚠ ЧЕСТНЫЙ ОСТАТОК: повтор ТОГО ЖЕ запроса (те же материалы) даёт то же эхо и остаётся
	//   неразличимым — это предел протокола, а не недоработка.
	if (UAsdaPetSubsystem* PEcho = Pets())
	{
		if (SendLatch.IsForeignEcho(PEcho->GetLastUnionMatLow16(0)))
		{
			UE_LOG(LogAsdaPetEvo, Warning,
				TEXT("[питомец-развитие] 6113 относится к ДРУГОМУ запросу: эхо первого материала ")
				TEXT("(младшие 16 бит uid = 0x%04X) не совпало с отправленным — это ответ на ")
				TEXT("ПРЕДЫДУЩИЙ 6112. Пакет не применяем, засов не снимаем."),
				PEcho->GetLastUnionMatLow16(0));
			return;
		}
	}
	if (SendLatch.ConsumeTimedOut())
	{
		UE_LOG(LogAsdaPetEvo, Warning,
			TEXT("[питомец-развитие] 6113 пришёл ПОСЛЕ таймаута, но эхо первого материала СОВПАЛО — ")
			TEXT("ответ наш. Неразличим лишь повтор ТОГО ЖЕ запроса (те же три материала)."));
	}
	SendLatch.Disarm();
	bConfirmArmed = false;
	SendLeft = 0.f;
	AsdaUi::SetShownDecor(ResultAniGrp, false);
	AsdaUi::SetShownDecor(ResultAni, false);

	if (Result != 1)
	{
		// ★ Нотисов отказа у 6113 в оригинале НЕТ: весь разбор внутри `if (result == 1)` (:7654).
		//   Не выдумываем (закон `never-invent-only-original`) — только лог.
		UE_LOG(LogAsdaPetEvo, Log,
			TEXT("[питомец-развитие] 6113 отказ код=%d (3 материалы/уровень, 4 рецепт вне диапазона, ")
			TEXT("7 результат вне диапазона) — нотиса у оригинала нет"), Result);
		if (bVisible) { RefreshAll(); }
		return;
	}

	// Успех. Модель волны 1 уже добавила нового питомца и УДАЛИЛА три съеденных материала по
	//   младшим 16 битам uid (`petDataFindInvItemById` → `mapEraseNode`, npc_mob_pet.c:7677 →
	//   :5549-5553). Повторно этого не делаем (закон `check-existing-before-creating`).
	for (FAsdaPetUnionRow& M : Mats) { M.PickedUid = -1; }   // `row[5] = nullptr` (:7679-7680)
	RefreshAll();                                            // uiPetPanelRefreshAll (:7681)

	// ★ `CRefineUI_RefreshItemPanel(g_PetData)` (:7682) — ОБНОВИТЬ СПИСОК В ОКНЕ 43000.
	//   Сервер списка 6106 после юниона НЕ шлёт (GameServer.c:36115 отправляет только 6113),
	//   поэтому без этого вызова в клетке остались бы три фантомных питомца до входа в мир.
	if (UAsdaPetCageSubsystem* Cage = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaPetCageSubsystem>() : nullptr)
	{
		Cage->RefreshItemPanel();
	}
	// Нотис 109973 «Развитие питомца завершено…», канал A (:7684-7687).
	NoticeMsgBox(PetEvoLoc::kUnionDone);
	UE_LOG(LogAsdaPetEvo, Log, TEXT("[питомец-развитие] 6113 успех: новый pet=%d, три слота очищены"), NewPetUid);
}

// ═══════════════════════════════════ ДАННЫЕ ═══════════════════════════════════════════════════
UAsdaPetSubsystem* UAsdaPetEvolutionSubsystem::Pets() const
{
	UGameInstance* GI = GetGameInstance();
	return GI ? GI->GetSubsystem<UAsdaPetSubsystem>() : nullptr;
}

UAsdaItemTableSubsystem* UAsdaPetEvolutionSubsystem::Items() const
{
	UGameInstance* GI = GetGameInstance();
	return GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
}

UAsdaWindowSubsystem* UAsdaPetEvolutionSubsystem::Windows() const
{
	UGameInstance* GI = GetGameInstance();
	return GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
}

const FAsdaPetSlot* UAsdaPetEvolutionSubsystem::Target() const
{
	UAsdaPetSubsystem* P = Pets();
	return (P && TargetUid >= 0) ? P->FindByUniqueId(TargetUid) : nullptr;
}

bool UAsdaPetEvolutionSubsystem::PetRow(int32 PetIndex, FAsdaPetRow& Out) const
{
	// ★ ВОЛНА 8 (дубли, пункт 7): тело — в общей точке `AsdaPetUi::PetRow`.
	return AsdaPetUi::PetRow(GetGameInstance(), PetIndex, Out);
}

// ★ ПОЗИЦИЯ, А НЕ ПОЛЕ `Level` — приём волны 2. Эталон адресует `petexptable` только арифметикой
//   `pPetExpTable + 44 × <позиция>` (ui_core.c:90028, :90030-90032), а в данных `Level = позиция + 1`.
bool UAsdaPetEvolutionSubsystem::ExpRow(int32 Pos, FAsdaPetexptableRow& Out) const
{
	// ★ ВОЛНА 8 (дубли, пункт 7): тело — в общей точке `AsdaPetUi::ExpRow`.
	return AsdaPetUi::ExpRow(GetGameInstance(), Pos, Out);
}

// Формула эталона 1:1 (ui_core.c:90029-90033), та же, что у окна 43000:
//   min = petexp[level−1].Exp × (pettable.ExpRateX10 / 10)
//   max = petexp[level  ].Exp × (pettable.ExpRateX10 / 10)
//   pct = 100 / (max − min) × (exp − min)
bool UAsdaPetEvolutionSubsystem::ExpFraction(const FAsdaPetSlot& S, float& OutFrac) const
{
	// ★ ВОЛНА 8 (дубли, пункт 7): формула — в общей точке `AsdaPetUi::ExpFraction`.
	return AsdaPetUi::ExpFraction(GetGameInstance(), S, OutFrac);
}

// ══════════════════════════════════════ НОТИСЫ ════════════════════════════════════════════════
FString UAsdaPetEvolutionSubsystem::Loc(int32 LocId) const
{
	return AsdaNotice::Loc(GetGameInstance(), LocId);
}

void UAsdaPetEvolutionSubsystem::NoticeMsgBox(int32 LocId) const
{
	// Канал A отчёта §6.1: `CItem_BuildTooltipText(g_pItemTooltipUI, 0, −1, текст, 1)` —
	//   однокнопочное модальное окно ONEBUT_MSGBOX 1500, кнопка = loc 2288 «ОК».
	//   ★ Чат у питомцев НЕ используется (доказано двумя методами, отчёт §6.1).
	// ★ ВОЛНА 8 (дубли, пункт 7): тело — в общей точке `AsdaPetUi::NoticeMsgBox`
	//   (там же и единственная константа подписи кнопки 2288).
	AsdaPetUi::NoticeMsgBox(GetGameInstance(), LocId);
}

void UAsdaPetEvolutionSubsystem::SetInfoText(int32 LocId)
{
	// Канал C: подпись 43877 внутри окна.
	if (!TxtInfo) { return; }
	const FString S = Loc(LocId);
	TxtInfo->SetText(FText::FromString(S));
}
