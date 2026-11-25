// Copyright (c) 2026 @sitxovski (Discord)
// AsdaPetEvolutionSubsystem_Window.cpp — ★ R3-c 12.11.2026 (разрез AsdaPetEvolutionSubsystem.cpp, дословный перенос): окно эволюции: открыть/закрыть, клики, сетка, слияние (отправка), обновление этапов, материалов, сетки, подсказки.
//   Общее для частей — в AsdaPetEvolutionSubsystem_Private.h; шапка и реверс — в AsdaPetEvolutionSubsystem.cpp.
#include "UI/AsdaPetEvolutionSubsystem.h"
#include "UI/AsdaEnterGuard.h"   // ★ 25.10.2026: вопросы этого файла Enter не подтверждает (только щелчок)
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping

#include "UI/AsdaJuiWindowHandle.h"
#include "UI/AsdaNotice.h"
#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaUiHelpers.h"
#include "UI/AsdaPetCageSubsystem.h"
#include "Items/AsdaItemDbRows.h"
#include "Pet/AsdaPetSubsystem.h"
#include "Pet/AsdaPetTableRows.h"
#include "Net/NetOpcodes.h"

#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"   // ★ волна 9: кисть кадра превью для 43576
#include "Engine/DataTable.h"
#include "Engine/GameInstance.h"
#include "AsdaPetEvolutionSubsystem_Private.h"   // ★ R3-c 12.11.2026: общее для частей AsdaPetEvolutionSubsystem*.cpp

static ASDA_DEV_CVAR(int32, CVarEvoStageArrowsLiteral,
	TEXT("asda.Pet.EvoStageArrowsLiteral"), 0,
	TEXT("0 — стрелки стадий 4 и 5 работают (дефект ПД6 оригинала исправлен);\n")
	TEXT("1 — дословно как в оригинале: у стадий 4 и 5 стрелок нет (IDNum в .JUI отсутствует)."),
	ECVF_Default);

// ══════════════════════════ ОТКРЫТИЕ: uiPetDialog_Open@0xEDD450 ═══════════════════════════════
bool UAsdaPetEvolutionSubsystem::OpenEvolution(int32 PetUniqueId)
{
	UAsdaPetSubsystem* P = Pets();
	if (!P) { return false; }

	// Г1 — `if (g_pActiveBuffEntry)` (ui_core.c:89962): есть ПРИЗВАННЫЙ ⇒ нотис 109974 и выход.
	//   ★ Проверяется ПЕРВЫМ и на аргумент не смотрит вовсе.
	if (P->GetActivePet() != nullptr)
	{
		NoticeMsgBox(PetEvoLoc::kGateSummoned);
		UE_LOG(LogAsdaPetEvo, Log, TEXT("[питомец-развитие] гейт 109974: питомец призван, окно 43500 не открыто"));
		return false;
	}

	// Г2 — `else if (a2)` (:89967): питомца не передали ⇒ МОЛЧА ничего (ветки `else` нет).
	const FAsdaPetSlot* Pet = (PetUniqueId >= 0) ? P->FindByUniqueId(PetUniqueId) : nullptr;
	if (!Pet)
	{
		UE_LOG(LogAsdaPetEvo, Log,
			TEXT("[питомец-развитие] окно 43500 не открыто: питомца uid=%d нет (эталон в этой ветке молчит)"),
			PetUniqueId);
		return false;
	}

	if (!EnsureEvo()) { return false; }
	// ★★★ СЕТКУ И ПОДСКАЗКУ ПОДНИМАЕМ ЗАРАНЕЕ, А НЕ ПО ПЕРВОМУ КЛИКУ. Так делает и эталон:
	//   `uiPetDialogInit@0xEDC7C0` биндит контролы 43400 (ui_core.c:89874-89885) и 43450 (:89887-89897)
	//   ОДНОВРЕМЕННО с контролами 43500, потому что все три — части одного объекта `CPetMgr`.
	//   ★ Живой прогон 21.09 показал, ЗАЧЕМ это важно в порту: создание окна посреди зажатой ЛКМ
	//     (`OpenWindowByPath` → `AddToViewport`) рвёт путь захвата мыши Slate и съедает клик —
	//     ровно та грабля, что описана над `UAsdaWindowSubsystem::SetWindowZOrder`. Первый клик по
	//     слоту материала терялся, второй проходил.
	EnsureGrid();
	EnsureTip();

	// ═══ ★★★ ДЕФЕКТ ВЛАДЕЛЬЦА №3 (25.09.2026): СЕТКА 43400 ПОЯВЛЯЛАСЬ И ТУТ ЖЕ ПРОПАДАЛА ═══
	//
	// ЧТО ЭТО БЫЛО НА САМОМ ДЕЛЕ. Не «закрывается», а НАКРЫВАЕТСЯ: 43500 поднимается ПОВЕРХ
	//   43400 в том же кадре, сразу после показа сетки. Доказательство — живой лог самого
	//   владельца `Saved/Logs/AsdaReborn.log`, кадр [992], ЧЕТЫРЕ строки подряд:
	//     :5360 [z-порядок] «Pet_Evolution_Win»: z 755 → 750   ← `Restack()` опустил 43500
	//     :5361 [z-порядок] «Pet_Inven_Win»:    z 752 → 752
	//     :5362 [питомец-развитие] сетка 43400 открыта на колонке 0
	//     :5363 [z-порядок] «Pet_Evolution_Win»: z 750 → 755   ← ПОДЪЁМ 43500 ПОВЕРХ СЕТКИ
	//   Повторяется на КАЖДОМ клике: в логе девять таких «750 → 755». Корпус 43500 (205,126,390×558)
	//   вместе с его правой панелью накрывает корпус 43400 (420,289,207×171) целиком.
	//   Контроль: консольный путь `asda.Pet.EvoSlot 0` (без мыши) сетку открывает, и она ОСТАЁТСЯ
	//   (`Reports/pet/v9/game_pet_v9_sweep_rev.log:4691`) ⇒ логика открытия исправна,
	//   ломает её только путь мыши. Подозрения «двойной клик», «защёлка», «клик вне окна»
	//   отпадают: `SetWindowVisible(43400, false)` в логе дефекта не вызывается НИ РАЗУ.
	//
	// ПОЧЕМУ ПОДЪЁМ ВООБЩЕ СЛУЧАЛСЯ. `BringWindowToFront` (`AsdaWindowSubsystem.cpp:1188-1209`)
	//   считает «верх обычного слоя» по ВСЕМ окнам, кроме СВОИХ ПОТОМКОВ. Сетка 43400 (752) и
	//   подсказка 43450 (754) потомками 43500 объявлены НЕ БЫЛИ ⇒ шли в расчёт как соперники,
	//   и клик по телу 43500 уводил его на 754 + 1 = 755.
	//
	// ★ У ЭТАЛОНА ЭТОГО БЫТЬ НЕ МОЖЕТ ПО УСТРОЙСТВУ. Там 43400 и 43450 — КОНТРОЛЫ внутри
	//   того же `CPetMgr` (`uiPetDialogInit@0xEDC7C0` биндит их одной функцией: `ui_core.c:89877`,
	//   `:89888`), а подъём окна — `CClass_MapInsert` = ОТЛОЖЕННАЯ очередь менеджера
	//   (`entity_char.c:66090-66098`, разбирается в конце кадра). Порядок у оригинала:
	//   сперва поднимается диалог, потом — сетка. Сетка выигрывает ВСЕГДА.
	//   И парного «спрятать 43400» на этом событии у эталона нет вовсе: `sub_9257FB` зовётся
	//   ровно трижды — `ui_core.c:90001` (закрытие диалога), `:90161` (кнопка 43439),
	//   `:90281` (кандидат выбран). На клике по слоту материала — никогда.
	//
	// ЛЕЧЕНИЕ — РОДСТВО, тем же приёмом, каким уже вылечена панель 43058 у окна 43000
	//   (`AsdaPetCageSubsystem.cpp:727`). Цепочка 43500 → 43400 → 43450 повторяет эталонную
	//   вложенность контролов: `RaiseOwnedAbove` держит каждого потомка на ступень выше
	//   владельца сам, а `IsOwnedBy` убирает их из расчёта «верха» — подъёма 43500 поверх
	//   собственной сетки больше не происходит вовсе.
	if (UAsdaWindowSubsystem* WMown = Windows())
	{
		WMown->SetWindowOwner(kGridWindowId, kEvoWindowId);   // 43400 — всегда над 43500
		WMown->SetWindowOwner(kTipWindowId,  kGridWindowId);  // 43450 — всегда над 43400
	}

	TargetUid = PetUniqueId;                 // m_pCurItem = a2 (:89970-89971)
	bVisible = true;                         // m_bVisible = 1 (:89973)
	if (UAsdaWindowSubsystem* WM = Windows())
	{
		WM->SetWindowVisible(kEvoWindowId, true);   // SetVisible(1) + MapInsert (:89972, :89974)
	}
	// ★★★ ДЕФЕКТ ВЛАДЕЛЬЦА №4 (25.09.2026) — ОТСТУПЛЕНИЕ ОТ ОРИГИНАЛА, ПОМЕЧЕНО.
	//   ЭТАЛОН ЗАКРЫВАЕТ 43000 ЗДЕСЬ: `uiPetDialog_Open@0xEDD450` последним делом зовёт
	//   `uiCloseWindowReset((CControl **)g_bDeviceReloadPending)` (`ui_core.c:89976`), а тот гасит
	//   контрол 43000 (доказательство тремя способами — у объявления `CloseCageForChildWindow`
	//   в `AsdaPetCageSubsystem.h`). Владелец потребовал обратного, поэтому решение принимает
	//   ЕДИНАЯ ТОЧКА с ключом отката `asda.Pet.KeepCageOpen`, а не этот вызов.
	if (UAsdaPetCageSubsystem* Cage = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaPetCageSubsystem>() : nullptr)
	{
		Cage->CloseCageForChildWindow(TEXT("43500 «Эволюция»"));
		// Клетка осталась открытой ⇒ 43500 обязано быть ВЫШЕ неё и не лежать на ней целиком.
		// ⚠ РОДСТВО ЗДЕСЬ НЕ ОБЪЯВЛЯЕМ НАМЕРЕННО (в отличие от панели 43058 и подсказки 43300).
		//   `RaiseOwnedAbove` держит потомка РОВНО на `z(владельца) + 1`, то есть при каждом
		//   подъёме клетки СТАСКИВАЛ бы 43500 с его 750 на 747 — а на 750 висит целая стопка
		//   43500 → 43400 → 43450, и её числа завязаны друг на друга (`Restack`). Подсказке и
		//   панели такое родство нужно: они ходят ПОД курсором ПОВЕРХ клетки. Окна 43500/44200
		//   стоят СБОКУ и не перекрываются с ней вовсе — им хватает разового подъёма.
		if (Cage->IsCageOpen())
		{
			if (UAsdaWindowSubsystem* WMz = Windows()) { WMz->BringWindowToFront(kEvoWindowId); }
			Cage->PlaceChildBesideCage(kEvoWindowId, Win, (double)kWinWideW);
		}
		// ★★★ ВОЛНА 9: ПРЕВЬЮ 43576. В оригинале это ТА ЖЕ текстура "PetWindow", что и в 43052
		//   (`npc_mob_pet.c:5602` и `:5605` — один handle, один UV-регион `0,0,240,170`),
		//   поэтому своей сцены и своей камеры у 43576 НЕТ: берём ту же кисть у окна 43000.
		//   Заявку ставим ПОСЛЕ `CloseCage()`: оно гасит капчер, а нам кадр как раз нужен —
		//   эталонный гейт рендера «открыто 43000 ИЛИ открыт диалог развития» (`:5591`).
		Cage->HoldPreviewForEvolution(true);
		if (const FAsdaPetSlot* PreviewPet = Target())
		{
			Cage->SetPreviewPet(PreviewPet->PetIndex, PreviewPet->Name);
		}
		if (ImgResult3d)
		{
			// ★★★ ТОТ ЖЕ ДЕФЕКТ, ЧТО У 43052 (найден 24.09.2026 разбором дефекта владельца).
			//   Контрол 43576 носит ТОТ ЖЕ скин-атлас `NoImage_icon` (дамп 43500, узел 8), и
			//   `SetBrushResourceObject` НЕ снимает записанный импортёром микро-кусок атласа
			//   (`uv 0.994 0.000 … 1.000 0.004`) — материал сэмплился бы одной точкой пустого
			//   угла кадра и контрол вышел бы абсолютно прозрачным. Кисть собираем ЗАНОВО общей
			//   точкой; разбор целиком — у `AsdaUi::ApplySceneMaterialBrush`.
			//   ⚠ Заметить это на 43576 было НЕЧЕМ: окно эволюции открывается редко, а пустая
			//   плашка выглядит как «ещё не выбрали питомца». Лечим вместе с 43052, а не потом.
			// ★ ПОПРАВКА РЕВИЗИИ (СРЕДН-7): СХЕМА ТА ЖЕ, ЧТО У 43052, А НЕ ВТОРАЯ СВОЯ. Здесь
			//   передавался ГОТОВЫЙ экземпляр актора сцены (`GetPreviewBrush`), а окно 43000
			//   передаёт МАСТЕР и ставит параметр на экземпляре, который создаёт сам виджет.
			//   Две схемы на одну болезнь — верный способ вылечить одну и забыть другую.
			if (UMaterialInterface* Master = Cage->GetPreviewBrushMaster())
			{
				if (UMaterialInstanceDynamic* Own = AsdaUi::ApplySceneMaterialBrush(
						ImgResult3d, Master, Cage->GetPreviewRenderSize(), TEXT("превью 43576")))
				{
					Own->SetTextureParameterValue(TEXT("SceneColor"), Cage->GetPreviewRenderTarget());
				}
			}
		}
	}
	// Три слота материалов обнуляются (:89976-89977).
	for (FAsdaPetUnionRow& M : Mats) { M.PickedUid = -1; }
	// 43877 ← 109967 (:89978-89979).
	SetInfoText(PetEvoLoc::kHintOpened);
	// `m_pSelSlot = 0`, `m_bFlag535 = 0`, спрятать 43883 (:89980-89983).
	SendLatch.Disarm();
	bConfirmArmed = false;
	SendLeft = 0.f;
	AsdaUi::SetShownDecor(ResultAniGrp, false);
	AsdaUi::SetShownDecor(ResultAni, false);
	// `m_curSelIdx = −1` (:89984) и СВЁРНУТАЯ страница (:89985 `uiUpgradeDialogReset`).
	CurStage = -1;
	CurCol = 0;
	ApplyPage(false);
	// Сетка и подсказка при открытии не показываются.
	CloseGrid();
	HideTip();
	RefreshAll();

	UE_LOG(LogAsdaPetEvo, Log,
		TEXT("[питомец-развитие] 43500 открыто на pet=%d (порода %d, ур. %d); карта развития: %s"),
		Pet->UniqueId, Pet->PetIndex, Pet->Level, CurrentMap() ? TEXT("есть") : TEXT("НЕТ (ключ −1)"));
	return true;
}

void UAsdaPetEvolutionSubsystem::CloseEvolution()
{
	if (!bVisible) { return; }
	bVisible = false;
	// `uiPetDialogHide@0xEDD620` (ui_core.c:89995-90002): спрятать окно, затем `sub_9257FB`
	//   (спрятать сетку 43400) и `sub_92BA11` (спрятать подсказку 43450).
	if (UAsdaWindowSubsystem* WM = Windows()) { WM->SetWindowVisible(kEvoWindowId, false); }
	// ★ ВОЛНА 9: заявку на кадр превью снимаем — оба окна закрыты, жечь GPU больше не на что.
	//   Сам буфер НЕ освобождается (эталон освобождает его только при device lost).
	if (UAsdaPetCageSubsystem* Cage = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaPetCageSubsystem>() : nullptr)
	{
		Cage->HoldPreviewForEvolution(false);
	}
	CloseGrid();
	HideTip();
	HoverKey = -1;
	Press.Clear();
	// ★★★ ВОЛНА 8 (пункт 1 ревизии): ЗАКРЫТИЕ СНИМАЕТ ЗАСОВ «6112 В ПОЛЁТЕ». Открытие его уже
	//   снимало (`OpenEvolution`, :89980 эталона), а закрытие — нет; пара была неполной.
	//   Повторный 6112 сервер обработает по СВОЕМУ состоянию (материалов уже нет ⇒ код 3),
	//   второго юниона он не сделает.
	SendLatch.Disarm();
	bConfirmArmed = false;
	SendLeft = 0.f;
	Mouse.Reset();
	UE_LOG(LogAsdaPetEvo, Log, TEXT("[питомец-развитие] 43500 закрыто"));
}

// ═════════════════════════════════ ДЕЙСТВИЯ ИГРОКА ════════════════════════════════════════════
// Событие 0xB по иконке стадии — порт `skillTreeOnTabSelect@0xEE00D0` (skill_buff.c:37953-37978).
void UAsdaPetEvolutionSubsystem::OnStageClicked(int32 Stage)
{
	// Гейт эталона: `if (!*((BYTE*)this + 1333))` — пока крутится подтверждение, выбор не меняется.
	if (bConfirmArmed) { return; }
	if (Stage < 0 || Stage >= kStages) { return; }
	CurStage = Stage;                        // `*(this + 331) = i` (:37960)
	SetInfoText(PetEvoLoc::kHintPickMats);   // 43877 ← 109968 (:37961-37963)
	FillPrereqSlots();                       // автоподбор трёх материалов (:37964)
	RefreshMaterials();                      // itemPanel3SlotRefresh (:37965)
	ApplyPage(true);                         // uiUpgradeDialogActivate (:37966)
	RefreshStages();                         // skillTree_RefreshSlots (:37967)
	UE_LOG(LogAsdaPetEvo, Log, TEXT("[питомец-развитие] выбрана стадия %d, рецепт %d, автоподбор %d/%d/%d"),
		Stage, CurrentRecipe() ? CurrentRecipe()->UnionIndex : -1,
		Mats[0].PickedUid, Mats[1].PickedUid, Mats[2].PickedUid);
}

// Событие 0xB по слоту материала — порт `equipUiSetSlotAndShow@0xEDFDB0` (item_inventory.c:65205).
void UAsdaPetEvolutionSubsystem::OnMaterialSlotClicked(int32 Col)
{
	if (bConfirmArmed) { return; }           // тот же гейт `!*((BYTE*)this + 1333)` (:65209)
	if (Col < 0 || Col >= kMaterials) { return; }
	CurCol = Col;                            // `*(this + 330) = a2` (:65211)
	if (!EnsureGrid()) { return; }
	bGridShown = true;
	if (UAsdaWindowSubsystem* WM = Windows()) { WM->SetWindowVisible(kGridWindowId, true); }
	Restack();
	RefreshGrid();                           // equipUiShowAndRefresh → uiRefreshEquipIconGrid
	// ★ ШАГ ЭТАЛОНА, КОТОРОГО ЗДЕСЬ НЕ БЫЛО: то же событие 0xB гасит подсказку 43450 —
	//   `sub_92BA11(this)` (`ui_core.c:90209`). Что именно она гасит, доказано адресом:
	//   тумблер ведёт на `sub_EDE7E0@0xEDE7E0` = `CControl_SetVisible(*(this + 293), 0)`,
	//   а `293 × 4 = 1172 = 0x494` — это `CPetMgr::m_pCtrl43450` (`structs_ru.h`).
	HideTip();
	UE_LOG(LogAsdaPetEvo, Log, TEXT("[питомец-развитие] сетка 43400 открыта на колонке %d"), Col);
}

// Событие 0xD (ДВОЙНОЙ ЛКМ) по ячейке сетки — порт `uiPetPanelOnSlotSelect@0xEDFCB0`
//   (ui_core.c:90267-90287).
void UAsdaPetEvolutionSubsystem::OnGridCellPicked(int32 Cell)
{
	if (!Cells.IsValidIndex(Cell)) { return; }
	if (Cells[Cell].PetUid < 0) { return; }  // `if (*(this + 2*a2 − 86591))` — пустую не берём
	if (!Mats.IsValidIndex(CurCol)) { return; }
	Mats[CurCol].PickedUid = Cells[Cell].PetUid;   // `*(this + 20*col + 152) = запись` (:90277)
	CloseGrid();                                   // sub_9257FB (:90278)
	HideTip();                                     // sub_92BA11 (:90279)
	RefreshAll();                                  // uiPetPanelRefreshAll (:90280)
	UE_LOG(LogAsdaPetEvo, Log, TEXT("[питомец-развитие] в колонку %d положен pet=%d"),
		CurCol, Mats[CurCol].PickedUid);
}

// Кнопка 43876 — порт `uiCraftCheckSlotsAndShowTooltip@0xEDFE00` (ui_core.c:90297-90320).
void UAsdaPetEvolutionSubsystem::OnOkClicked()
{
	if (bConfirmArmed) { return; }                 // `if (!*(this + 1333))` (:90303)
	bool bAll = true;
	for (const FAsdaPetUnionRow& M : Mats) { if (M.PickedUid < 0) { bAll = false; } }
	if (!bAll)
	{
		SetInfoText(PetEvoLoc::kNoMats);           // 43877 ← 109971 (:90315-90317)
		UE_LOG(LogAsdaPetEvo, Log, TEXT("[питомец-развитие] 43876: нотис 109971 — слоты %d/%d/%d"),
			Mats[0].PickedUid, Mats[1].PickedUid, Mats[2].PickedUid);
		return;
	}
	if (bConfirmOpen) { return; }
	UAsdaWindowSubsystem* WM = Windows();
	if (!WM) { return; }
	// msgbox 70: текст 109972, кнопки 108510/108511 (item_inventory.c:51104-51113).
	const FString Text = Loc(PetEvoLoc::kConfirm70);
	if (Text.IsEmpty()) { return; }
	bConfirmOpen = true;
	AsdaEnterGuard::NoEnter(WM)->ShowGameConfirm(Text, [this](bool bYes)
	{
		bConfirmOpen = false;
		if (!bYes) { return; }
		ArmUnionSend();                            // sub_92A2DD (ui_core.c:57101)
	}, /*GaugeGateSec*/ 0.f, PetEvoLoc::kMsg70Ok, PetEvoLoc::kMsg70No, /*очередь: эталон (1, 70) ui_core.c:90318*/70, 1);
}

void UAsdaPetEvolutionSubsystem::OnCloseClicked()
{
	CloseEvolution();
}

// Порт `sub_9257FB` — спрятать 43400. Окно 43500 при этом НЕ закрывается.
void UAsdaPetEvolutionSubsystem::CloseGrid()
{
	if (!bGridShown) { return; }
	bGridShown = false;
	if (UAsdaWindowSubsystem* WM = Windows()) { WM->SetWindowVisible(kGridWindowId, false); }
}

void UAsdaPetEvolutionSubsystem::OnPageArrow(bool bExpand)
{
	// 43582 → `uiUpgradeDialogActivate`, 43583 → `uiUpgradeDialogReset` (ui_core.c:90162-90174).
	ApplyPage(bExpand);
	if (!bExpand)
	{
		// Свёрнутая страница прячет и сетку с подсказкой: они живут в правой панели.
		CloseGrid();
		HideTip();
	}
}

// Порт `sub_92A2DD` = `wnd_ShowCtrl146Timestamp@0xEDFEC0` (ui_core.c:90330-90342), см. В2 в шапке:
//   показать 43883, `m_bFlag535 = 1`, запомнить `timeGetTime()`. САМ ПАКЕТ УЙДЁТ ЧЕРЕЗ 4 СЕКУНДЫ.
void UAsdaPetEvolutionSubsystem::ArmUnionSend()
{
	AsdaUi::SetShownDecor(ResultAniGrp, true);
	AsdaUi::SetShownDecor(ResultAni, true);
	bConfirmArmed = true;
	SendLeft = kSendDelaySec;
	UE_LOG(LogAsdaPetEvo, Log,
		TEXT("[питомец-развитие] подтверждение 70 принято: анимация 43883 пущена, 6112 уйдёт через %.1f с"),
		kSendDelaySec);
}

// Порт `skillTree_SendLearnRequest@0xEE01A0` (skill_buff.c:37981-38133).
void UAsdaPetEvolutionSubsystem::SendUnion()
{
	// `if (this->m_pSelSlot != (void*)1)` — второй раз до ответа 6113 не шлём (skill_buff.c:37988).
	if (SendLatch.IsArmed()) { return; }
	UAsdaPetSubsystem* P = Pets();
	const FAsdaPetSlot* Pet = Target();
	const FAsdaPetUnionRecipeRow* Recipe = CurrentRecipe();
	if (!P || !Pet || !Recipe)
	{
		UE_LOG(LogAsdaPetEvo, Warning,
			TEXT("[питомец-развитие] 6112 не отправлен: цель=%s рецепт=%s"),
			Pet ? TEXT("есть") : TEXT("НЕТ"), Recipe ? TEXT("есть") : TEXT("НЕТ"));
		bConfirmArmed = false;
		return;
	}
	// `LOWORD(body[1]) = petRecord.StuffIdx[curSel]` (skill_buff.c:38023) и три uid (:38024-38029).
	StandExpectUnion = Recipe->UnionIndex;
	for (int32 i = 0; i < kMaterials; ++i) { StandExpectMat[i] = Mats[i].PickedUid; }
	const bool bOk = P->RequestUnion(Recipe->UnionIndex, Mats[0].PickedUid, Mats[1].PickedUid, Mats[2].PickedUid);
	// `this->m_pSelSlot = (void*)1` — признак «в полёте» (skill_buff.c:38131).
	// ★★★ ВОЛНА 8 (пункт 1 сквозной ревизии): ЗАСОВ СТАВИТСЯ ТОЛЬКО ПРИ ЖИВОЙ СВЯЗИ И С ТАЙМАУТОМ.
	//   БЫЛО: `bSendInFlight = true` БЕЗУСЛОВНО, а `bOk` (принята ли очередь) уходил только в лог.
	//   Снять засов могли лишь приход 6113 и сброс сессии — то есть при молчании сервера кнопка
	//   43876 «ОК» умирала до «закрыть → открыть», а `OnUnionResult` без пакета не приходил вовсе.
	//   ПОЧЕМУ ЭТО НЕ РАСХОЖДЕНИЕ С ЭТАЛОНОМ: у оригинала отправка СИНХРОННА и обрыв связи
	//   закрывает клиент (память `original-quits-client-on-disconnect`), поэтому «ответа нет» у
	//   него недостижимо. У порта отправка отложена на кадр (закон `no-reentrant-net-callbacks`).
	//   Правильный образец рядом — `UAsdaPetGrowthSubsystem::SendLimitBreak` (там это уже так).
	//   Условие «связь жива» — общая точка `AsdaPetUi::IsSendLinkUp`.
	const bool bLink = AsdaPetUi::IsSendLinkUp(GetGameInstance());
	if (bLink && bOk)
	{
		// ★★★ ВОЛНА 9: засов взводится С КЛЮЧОМ ЭХА. Ответ 6113 несёт МЛАДШИЕ 16 БИТ uid трёх
		//   материалов (+50/+52/+54, `GameServer.c:35984-35985`), причём и в отказах тоже.
		//   Ключом берём ПЕРВЫЙ материал: по нему поздний ответ на предыдущий 6112 отличается.
		//   ⚠ Именно младшие 16 бит — потому что сервер их усекает (дефект ПД9); сравнивать
		//   надо ровно то, что приедет по проводу, иначе сверка сама станет ложным «нет».
		SendLatch.Arm(6112,   // ★ S2 (В2-A): опкод запроса; таймаут — ключ `asda.Pet.SendTimeoutSec` на каждом тике
			(Mats[0].PickedUid >= 0) ? (Mats[0].PickedUid & 0xFFFF) : INDEX_NONE);
	}
	else
	{
		UE_LOG(LogAsdaPetEvo, Warning,
			TEXT("[питомец-развитие] 6112 НЕ уйдёт (связь=%s, очередь=%s) — засов «в полёте» НЕ взведён, ")
			TEXT("кнопка 43876 остаётся живой"),
			bLink ? TEXT("есть") : TEXT("НЕТ"), bOk ? TEXT("принята") : TEXT("ОТКАЗ"));
	}
	bConfirmArmed = false;
	SendLeft = 0.f;
	UE_LOG(LogAsdaPetEvo, Log,
		TEXT("[питомец-развитие] 6112 отправлен: рецепт %d, материалы %d/%d/%d, очередь=%s"),
		Recipe->UnionIndex, Mats[0].PickedUid, Mats[1].PickedUid, Mats[2].PickedUid,
		bOk ? TEXT("принята") : TEXT("ОТКАЗ"));
}

// ══════════════════════════════════ ПЕРЕРИСОВКА ═══════════════════════════════════════════════
void UAsdaPetEvolutionSubsystem::RefreshAll()
{
	RefreshStages();
	RefreshMaterials();
	if (bGridShown) { RefreshGrid(); }
}

// Порт `skillTree_RefreshSlots@0xEDD920` (skill_buff.c:37738-37894).
void UAsdaPetEvolutionSubsystem::RefreshStages()
{
	if (!bEvoReady) { return; }

	// Пролог: всё гасится (skill_buff.c:37784-37792).
	for (FAsdaPetEvoRow& R : Stages)
	{
		AsdaUi::ClearImageBrush(R.Icon);
		if (R.Label) { R.Label->SetText(FText::GetEmpty()); }
		AsdaUi::SetShownDecor(R.GfontMain, false);
		AsdaUi::SetShownDecor(R.GfontEvo, false);
		HideCounter(R.Lead1, R.Glyphs);
		// *** ПРОЛОГ ГАСИТ ЕЩЁ И СТРЕЛКУ, И ПЛАШКУ «LV» (поправка ревизии, СРЕДН-3).
		//   Эталонный пролог (`skill_buff.c:37772-37783`) в цикле по семи строкам безусловно делает
		//   `SetVisible(0)` для `row[19i+2]`, `row[19i+3]`, `row[19i+6]` И ВСЕХ ДЕСЯТИ цифровых
		//   планов, оставляя видимой только саму плашку строки. Наш пролог гасил глифы и цифры, но
		//   НЕ стрелку и НЕ клон «LV» — и у породы без цепочки развития (карта −1, выход по
		//   `if (!Pet || !Map)` ниже) на экране оставались нарисованные стрелки и красные «LV» при
		//   пустых плашках. Теперь гасится всё, как у эталона.
		AsdaUi::SetShownDecor(R.SlideAni, false);
		AsdaUi::SetShownDecor(R.CountClone, false);
		if (R.Icon) { R.Icon->SetColorAndOpacity(FLinearColor(1.f, 1.f, 1.f, (float)kAlphaReady / 255.f)); }
		R.StageLevel = -1;
	}
	for (const TObjectPtr<UImage>& Pl : RedBoxPlanes) { AsdaUi::SetShownDecor(Pl, false); }

	const FAsdaPetSlot* Pet = Target();
	const FAsdaPetEvoStageRow* Map = CurrentMap();
	if (!Pet || !Map) { return; }            // `if (v28->m_pCurItem)` + `if (petRecord)` (:37783, :37794)

	for (int32 k = 0; k < kStages; ++k)
	{
		FAsdaPetEvoRow& R = Stages[k];
		const int32 Type = Map->PetType.IsValidIndex(k) ? Map->PetType[k] : -1;
		R.StageLevel = Map->Level.IsValidIndex(k) ? Map->Level[k] : -1;
		// ★★★ ГАШЕНИЕ ПУСТЫХ СТАДИЙ ПО ДАННЫМ: `if (PetType[k] != -1)` (skill_buff.c:37795).
		if (Type < 0) { continue; }

		// «Здесь стоит ТВОЙ питомец» + дерево достижимых стадий (В5 в шапке, :37797-37812).
		if (Pet->PetIndex == Type)
		{
			AsdaUi::SetShownDecor(R.GfontMain, true);
			if (k == 0)
			{
				for (int32 n = 1; n <= 3; ++n) { if (Stages.IsValidIndex(n)) { AsdaUi::SetShownDecor(Stages[n].GfontEvo, true); } }
			}
			else if (k >= 1 && k <= 3)
			{
				const int32 Next = k + 3;    // 1→4, 2→5, 3→6
				if (Stages.IsValidIndex(Next)) { AsdaUi::SetShownDecor(Stages[Next].GfontEvo, true); }
			}
		}

		// Цифры уровня стадии (:37814-37831).
		DrawCounter(R.StageLevel, R.Lead1, R.Glyphs);

		// Иконка породы 64×64 + яркость по готовности рецепта (:37840-37864).
		FAsdaPetRow Pr;
		const bool bRow = PetRow(Type, Pr);
		if (bRow)
		{
			AsdaUi::ApplyJuiIconCell(R.Icon, Pr.ResultIcon0, Pr.ResultIcon0U, Pr.ResultIcon0V, kBigIconPx, (float)kBigIconPx);
		}
		const bool bReady = HasAllRecipeMatches(k);
		if (R.Icon)
		{
			const float A = (float)(bReady ? kAlphaReady : kAlphaDim) / 255.f;
			R.Icon->SetColorAndOpacity(FLinearColor(1.f, 1.f, 1.f, A));
		}
		// ★ Стрелка строки: `if (k) SetVisible(row[18], bReady)` (:37850, :37862).
		//   Дефект ПД6 — у строк 4 и 5 оригинал стрелку не находит; ключ возвращает это дословно.
		if (k > 0 && R.SlideAni)
		{
			const bool bLiteral = CVarEvoStageArrowsLiteral.GetValueOnGameThread() != 0;
			const bool bAddressable = !(bLiteral && (k == 4 || k == 5));
			if (bAddressable) { AsdaUi::SetShownDecor(R.SlideAni, bReady); }
		}
		// Подпись — имя породы `pettable + 4` (:37866-37867).
		if (R.Label && bRow) { R.Label->SetText(FText::FromString(Pr.Name)); }

		// Красная рамка выбора 43885 (:37869-37881). Перенос — сдвигом от авторского места:
		//   авторская рамка стоит ровно на иконке строки 0 с отступом (−3, −5), а разница
		//   абсолютных позиций иконок в общем канвасе окна равна разнице позиций в канвасе рамки.
		if (CurStage == k && RedBoxBase.X >= 0.0 && Stage0IconBase.X >= 0.0 && R.Icon)
		{
			FVector2D IconAbsMin, IconAbsMax, Icon0AbsMin, Icon0AbsMax;
			if (AsdaUi::WidgetAbsRect(R.Icon, IconAbsMin, IconAbsMax) &&
				Stages[0].Icon && AsdaUi::WidgetAbsRect(Stages[0].Icon, Icon0AbsMin, Icon0AbsMax))
			{
				const FVector2D D = IconAbsMin - Icon0AbsMin;
				for (int32 p = 0; p < RedBoxPlanes.Num(); ++p)
				{
					if (!RedBoxPlanes[p]) { continue; }
					FMargin B;
					if (!AsdaUi::BaseOffsetsOf(LayoutBase, RedBoxPlanes[p], B)) { continue; }
					AsdaUi::SetSlotPos(RedBoxPlanes[p], FVector2D(B.Left + D.X, B.Top + D.Y));
					AsdaUi::SetShownDecor(RedBoxPlanes[p], true);
				}
			}
		}
	}
}

// Порт `itemPanel3SlotRefresh@0xEDE050` (item_inventory.c:65057-65188).
void UAsdaPetEvolutionSubsystem::RefreshMaterials()
{
	if (!bEvoReady) { return; }

	// Пролог: все три строки гасятся (item_inventory.c:65090-65101).
	for (FAsdaPetUnionRow& R : Mats)
	{
		AsdaUi::ClearImageBrush(R.Icon);
		if (R.Name)  { R.Name->SetText(FText::GetEmpty()); }
		if (R.Ready) { R.Ready->SetText(FText::GetEmpty()); }
		AsdaUi::SetShownDecor(R.GfontReady, false);
		HideCounter(R.Lead1, R.Glyphs);
		if (R.Icon) { R.Icon->SetColorAndOpacity(FLinearColor(1.f, 1.f, 1.f, (float)kAlphaReady / 255.f)); }
		R.ReqLevel = -1;
		R.ReqPetIndex = -1;
	}
	if (TxtResult) { TxtResult->SetText(FText::GetEmpty()); }
	AsdaUi::SetShownDecor(GfontResult, false);

	const FAsdaPetSlot* Pet = Target();
	const FAsdaPetEvoStageRow* Map = CurrentMap();
	const FAsdaPetUnionRecipeRow* Recipe = CurrentRecipe();
	// `if (self->m_curSelIdx != -1)` (:65113) — без выбранной стадии правая панель пуста.
	if (!Pet || !Map || CurStage < 0 || !Recipe) { return; }

	// 43742 — имя породы РЕЗУЛЬТАТА (`pettable[PetType[curSel]] + 4`, :65117-65120).
	if (TxtResult && Map->PetType.IsValidIndex(CurStage))
	{
		FAsdaPetRow Rr;
		if (PetRow(Map->PetType[CurStage], Rr)) { TxtResult->SetText(FText::FromString(Rr.Name)); }
	}
	// 43740 «готово» — виден, когда заняты ВСЕ ТРИ слота (:65121-65124).
	const bool bAll = (Mats[0].PickedUid >= 0 && Mats[1].PickedUid >= 0 && Mats[2].PickedUid >= 0);
	AsdaUi::SetShownDecor(GfontResult, bAll);

	UAsdaPetSubsystem* P = Pets();
	for (int32 d = 0; d < kMaterials; ++d)
	{
		FAsdaPetUnionRow& R = Mats[d];
		R.ReqPetIndex = Recipe->ReqPetIndex.IsValidIndex(d) ? Recipe->ReqPetIndex[d] : -1;
		R.ReqLevel    = Recipe->ReqLevel.IsValidIndex(d) ? Recipe->ReqLevel[d] : -1;
		// Цифры требуемого уровня (`stuff + 4d + 24`, :65127-65155).
		DrawCounter(R.ReqLevel, R.Lead1, R.Glyphs);

		FAsdaPetRow Rr;
		if (!PetRow(R.ReqPetIndex, Rr)) { continue; }   // `if (!gemNameEntry) return;` (:65158)
		AsdaUi::ApplyJuiIconCell(R.Icon, Rr.ResultIcon0, Rr.ResultIcon0U, Rr.ResultIcon0V, kBigIconPx, (float)kBigIconPx);
		const FAsdaPetSlot* Picked = (P && R.PickedUid >= 0) ? P->FindByUniqueId(R.PickedUid) : nullptr;
		if (Picked)
		{
			// Слот ЗАНЯТ: яркая иконка, «Питомец готов» (109970), имя ВЫБРАННОГО (:65161-65168).
			if (R.Icon) { R.Icon->SetColorAndOpacity(FLinearColor(1.f, 1.f, 1.f, (float)kAlphaReady / 255.f)); }
			if (R.Ready) { R.Ready->SetText(FText::FromString(Loc(PetEvoLoc::kMatReady))); }
			AsdaUi::SetShownDecor(R.GfontReady, true);
			if (R.Name) { R.Name->SetText(FText::FromString(Picked->Name)); }
		}
		else
		{
			// Слот ПУСТ: тусклая иконка, «Не выбрано» (109969), имя ТРЕБУЕМОЙ породы (:65171-65179).
			if (R.Icon) { R.Icon->SetColorAndOpacity(FLinearColor(1.f, 1.f, 1.f, (float)kAlphaDim / 255.f)); }
			if (R.Ready) { R.Ready->SetText(FText::FromString(Loc(PetEvoLoc::kMatEmpty))); }
			AsdaUi::SetShownDecor(R.GfontReady, false);
			if (R.Name) { R.Name->SetText(FText::FromString(Rr.Name)); }
		}
	}
}

// Порт `uiRefreshEquipIconGrid@0xEDEA90` (ui_core.c:90066-90143).
void UAsdaPetEvolutionSubsystem::RefreshGrid()
{
	if (!bGridReady) { return; }
	// Пролог: тридцать иконок затираются пустой 32×32, данные обнуляются (:90080-90085).
	for (FAsdaPetGridCell& C : Cells) { AsdaUi::ClearImageBrush(C.Icon); C.PetUid = -1; }

	UAsdaPetSubsystem* P = Pets();
	const FAsdaPetUnionRecipeRow* Recipe = CurrentRecipe();
	if (!P || !Target() || !Recipe || !Mats.IsValidIndex(CurCol)) { return; }
	const int32 ReqType  = Recipe->ReqPetIndex.IsValidIndex(CurCol) ? Recipe->ReqPetIndex[CurCol] : -1;
	const int32 ReqLevel = Recipe->ReqLevel.IsValidIndex(CurCol) ? Recipe->ReqLevel[CurCol] : 0;
	// Уже занятые в ДВУХ ДРУГИХ слотах исключаются (:90112-90119).
	int32 Busy[3] = { -1, -1, -1 };
	for (int32 i = 0; i < kMaterials; ++i) { Busy[i] = Mats[i].PickedUid; }

	// ★★★ ДЕФЕКТ ОРИГИНАЛА ПД8 — ПЕРЕПОЛНЕНИЕ СЕТКИ (помечаем исправление, закон
	//   `fix-original-defects`; находка ревизии волны 5, СРЕДН-4).
	//   Эталон (ui_core.c:90121-90138) пишет кандидатов в `m_aMatSlots` БЕЗ проверки границы, а
	//   массив ровно `char[240]` = 30 × 8 (`structs_ru.h:2808`). При 31-м подходящем питомце он
	//   затирает соседние поля `CPetMgr` — то есть портит собственное состояние окна.
	//   ПОЧЕМУ ДОСТИЖИМО: ячеек у игрока до 30, и все 30 могут быть одной породы нужного уровня.
	//   ИСПРАВЛЕНО: лишние кандидаты не показываются, факт переполнения пишется в лог.
	//   Ключа «вернуть как было» здесь НЕТ сознательно: дословное поведение — это порча чужих
	//   полей объекта, и воспроизводить его нельзя ни под каким ключом.
	int32 Out = 0;
	int32 Overflow = 0;
	for (const FAsdaPetSlot& S : P->GetSlots())
	{
		// Фильтр эталона, три условия (:90130): порода == требуемой, уровень >= требуемого,
		//   uid не совпадает ни с одним из трёх уже занятых.
		// ★★★ ВОЛНА 8 (пункт 9 сквозной ревизии): ФИЛЬТР ТЕПЕРЬ ПЕРЕД ПРОВЕРКОЙ ГРАНИЦЫ.
		//   БЫЛО: `if (Out >= kGridCells) { ++Overflow; continue; }` стояло ПЕРВОЙ строкой цикла,
		//   то есть после заполнения тридцати ячеек в `Overflow` попадали ВСЕ оставшиеся записи
		//   списка — и подходящие, и НЕподходящие. Лог печатал «кандидатов Out + Overflow», то
		//   есть ЗАВЫШЕННОЕ число: у игрока с 30 питомцами разных пород он объявил бы «кандидатов
		//   30» там, где их, скажем, три. Число из диагностики попадает в отчёты волн, поэтому
		//   врать оно не имеет права. Теперь `Overflow` считает ТОЛЬКО прошедших фильтр.
		if (S.PetIndex != ReqType) { continue; }
		if (S.Level < ReqLevel) { continue; }
		if (S.UniqueId == Busy[0] || S.UniqueId == Busy[1] || S.UniqueId == Busy[2]) { continue; }
		if (Out >= kGridCells) { ++Overflow; continue; }
		FAsdaPetRow Pr;
		if (PetRow(S.PetIndex, Pr))
		{
			// `DrawTextureRect(cell, TexturePath(pettable+360), +392, +396, 32, 32)` (:90133-90135).
			AsdaUi::ApplyJuiIconCell(Cells[Out].Icon, Pr.IconTexture, Pr.IconU, Pr.IconV, kCellIconPx, (float)kCellIconPx);
		}
		Cells[Out].PetUid = S.UniqueId;
		++Out;
	}
	if (Overflow > 0)
	{
		UE_LOG(LogAsdaPetEvo, Warning,
			TEXT("[питомец-развитие] ДЕФЕКТ ПД8: кандидатов %d при 30 ячейках — %d не показаны. ")
			TEXT("Оригинал в этом месте затёр бы соседние поля CPetMgr (ui_core.c:90121-90138)."),
			Out + Overflow, Overflow);
	}
	UE_LOG(LogAsdaPetEvo, Verbose,
		TEXT("[питомец-развитие] сетка 43400: колонка %d, порода %d, уровень >= %d ⇒ кандидатов %d (лишних %d)"),
		CurCol, ReqType, ReqLevel, Out, Overflow);
}

// Порт `uiRefreshItemExpProgress@0xEDE810` (ui_core.c:90017-90038).
void UAsdaPetEvolutionSubsystem::RefreshTip(int32 PetUniqueId)
{
	if (!bTipReady) { return; }
	// Пролог: три подписи чистятся (:90024-90026).
	if (TxtTipName)  { TxtTipName->SetText(FText::GetEmpty()); }
	if (TxtTipStage) { TxtTipStage->SetText(FText::GetEmpty()); }
	if (TxtTipPct)   { TxtTipPct->SetText(FText::GetEmpty()); }

	UAsdaPetSubsystem* P = Pets();
	const FAsdaPetSlot* S = (P && PetUniqueId >= 0) ? P->FindByUniqueId(PetUniqueId) : nullptr;
	if (!S) { return; }
	// 43452 ← имя питомца (`CPetSlot + 0x0C`, :90027).
	if (TxtTipName) { TxtTipName->SetText(FText::FromString(S->Name)); }
	// 43454 ← имя стадии роста (`petexptable[level − 1] + 12`, :90028).
	FAsdaPetexptableRow Lo;
	if (TxtTipStage && S->Level >= 1 && ExpRow(S->Level - 1, Lo))
	{
		TxtTipStage->SetText(FText::FromString(Lo.StageName));
	}
	// 43451 полоса и 43455 «%.2f%%» (:90029-90037).
	float Frac = 0.f;
	const bool bFrac = ExpFraction(*S, Frac);
	const float Pct = bFrac ? FMath::Clamp(Frac, 0.f, 1.f) * 100.f : 0.f;
	FAsdaGaugeFill::Clip(TipBarFill, kTipBarLeft, kTipBarFullW, bFrac ? FMath::Clamp(Frac, 0.f, 1.f) : 0.f);
	if (TxtTipPct) { TxtTipPct->SetText(FText::FromString(FString::Printf(TEXT("%.2f%%"), Pct))); }
}

// Порт `uiShowItemInfoWindow@0xEDE740` (ui_core.c:90004-90014): показать 43450 и встать в курсор+2.
void UAsdaPetEvolutionSubsystem::ShowTip(int32 PetUniqueId)
{
	if (!EnsureTip()) { return; }
	TipForUid = PetUniqueId;                 // `*(this + 316) = a2` (:90011)
	RefreshTip(PetUniqueId);
	if (UAsdaWindowSubsystem* WM = Windows()) { WM->SetWindowVisible(kTipWindowId, true); }
	bTipShown = true;
	Restack();
	// `copyPair79(g_pWindowMgr, v3)` + `SetField74_75(ctrl, x + 2, y + 2)` (:90013).
	//   Иерархия .JUI расплющена, поэтому «поставить окно» = сдвинуть все дети корневой канвы
	//   (общая точка `FAsdaPopupMenu::PlaceAt`, тот же приём у окна 43300 волны 4).
	{
		const FVector2D TargetPos = Mouse.Cursor + FVector2D((double)kTipCursorOffsetPx, (double)kTipCursorOffsetPx);
		FAsdaPopupMenu::PlaceAt(Tip, TargetPos);
	}
}

// ★★★ ВОССТАНОВИТЬ ПОРЯДОК ТРЁХ ОКОН.
//   В оригинале 43400 и 43450 — КОНТРОЛЫ внутри диалога 43500, и перекрыть их диалог не может.
//   В порту это три отдельных окна, а оконная подсистема на клик по зоне поднимает окно
//   (`SetWindowZOrder`, «перевешено в вьюпорте»): живой прогон 21.09 показал, как 43500 уехало с
//   750 на 755 и накрыло собой и сетку (752), и подсказку (754). Поэтому после каждого показа
//   сетки или подсказки стопка выставляется заново и ЯВНО — числами из шапки.
// ★★★ ПОПРАВКА 25.09.2026 (ДЕФЕКТ ВЛАДЕЛЬЦА №3): ЖЁСТКИЕ ЧИСЛА УБРАНЫ, ПОРЯДОК ДЕРЖИТ РОДСТВО.
//   ПОЧЕМУ ПРЕЖНЯЯ РЕДАКЦИЯ НЕ РАБОТАЛА — это не «недосмотр в числах», а ПРОИГРАННАЯ ГОНКА.
//   `Restack` живёт в тике ПОДСИСТЕМЫ (`FTickableGameObject`), а подъём окна делает `TickDrag`
//   оконной подсистемы на core-ticker (`AsdaWindowSubsystem.cpp:289-290`, подъём — `:1304-1309`).
//   Живой лог владельца показывает порядок внутри ОДНОГО кадра [992]
//   (`Saved/Logs/AsdaReborn.log:5360-5363`): сперва `Restack` опускает 43500 на 750, потом
//   подъём уводит его на 755 — поверх сетки. Заплатка всегда срабатывала РАНЬШЕ поломки.
//   Теперь порядок задан РОДСТВОМ (`SetWindowOwner` в `OpenEvolution`), а `RaiseOwnedAbove`
//   внутри `BringWindowToFront` держит цепочку 43500 → 43400 → 43450 при ЛЮБОМ подъёме, в том
//   числе сделанном позже в том же кадре. Числа `kEvoZ/kGridZ/kTipZ` остаются стартовыми
//   (с ними окна регистрируются), но спорить с родством им больше нечем.
void UAsdaPetEvolutionSubsystem::Restack()
{
	UAsdaWindowSubsystem* WM = Windows();
	if (!WM) { return; }
	// ★★★ ПОПРАВКА РЕВИЗИИ 25.09.2026 (СРЕДНЕЕ-6): ВОССТАНАВЛИВАЕМ ПОРЯДОК, НО НЕ ПОДНИМАЕМ СТОПКУ.
	//   Промежуточная редакция звала здесь `BringWindowToFront(43500)`, и это МОЛЧА меняло
	//   поведение: открытие сетки 43400 выносило весь диалог на верх обычного слоя, чего до
	//   правки не было, и пометки об изменении не стояло. Нужно ровно одно — чтобы потомки
	//   43400/43450 лежали выше владельца; это и делает `RaiseOwnedAbove` без подъёма самого
	//   43500. Сам подъём остался там, где он был и раньше: на клике по телу окна
	//   (`PendingRaiseWindowId` оконной подсистемы).
	WM->RaiseOwnedAbove(kEvoWindowId);
}

// Порт `sub_92BA11` — спрятать 43450.
void UAsdaPetEvolutionSubsystem::HideTip()
{
	if (!bTipShown) { TipForUid = -1; return; }
	bTipShown = false;
	TipForUid = -1;
	if (UAsdaWindowSubsystem* WM = Windows()) { WM->SetWindowVisible(kTipWindowId, false); }
}
