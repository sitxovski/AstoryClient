// Copyright (c) 2026 @sitxovski (Discord)
// AsdaPetCageSubsystem_Controls.cpp — ★ R2 10.11.2026 (разрез AsdaPetCageSubsystem.cpp, дословный перенос): виджеты клетки/панели/статуса и их контролы.
//   Общее для частей — в AsdaPetCageSubsystemInternal.h; шапка и реверс — в AsdaPetCageSubsystem.cpp.
#include "UI/AsdaPetCageSubsystem.h"
#include "UI/AsdaEnterGuard.h"   // ★ 25.10.2026: вопросы этого файла Enter не подтверждает (только щелчок)
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "UI/AsdaCraftSubsystem.h"   // ★ гейты Г1/Г2: имена окон ремесла 76000/77500 (волна В2)

#include "UI/AsdaJuiWindowHandle.h"
#include "UI/AsdaNotice.h"
#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaInventorySubsystem.h"
#include "UI/AsdaItemTooltipSubsystem.h"
#include "UI/AsdaTipPanel.h"
#include "UI/AsdaChatWindowSubsystem.h"
#include "UI/AsdaUiHelpers.h"
#include "UI/AsdaPetGrowthSubsystem.h"   // ★ волна 4: кнопка 43055 открывает окно 44200
#include "UI/AsdaPetEvolutionSubsystem.h" // ★ волна 5: кнопка 43056 открывает окно 43500
#include "UI/AsdaPetEvoTipSubsystem.h"   // ★ волна 4: наведение на слот показывает окно 43300
#include "UI/AsdaPetPreviewActor.h"      // ★ волна 9: превью 43052 — свой RT 240×170 и вращение
#include "Engine/TextureRenderTarget2D.h"   // дамп размера буфера превью
#include "Materials/MaterialInstanceDynamic.h"   //   кисть кадра для 43052 (и для 43576 окна 43500)
#include "Materials/MaterialInterface.h"        // ★ 24.09: мастер-материал кисти превью
#include "Kismet/KismetRenderingLibrary.h"      // ★ 24.09: выгрузка буфера превью в PNG (asda.Pet.PreviewDumpRt)
#include "Engine/World.h"                //   спавн актора сцены превью
#include "Items/AsdaItemTableSubsystem.h"
#include "Items/AsdaItemDbRows.h"
#include "Pet/AsdaPetSubsystem.h"
#include "Net/NetOpcodes.h"          // OP_PET_SLOT_COUNT — диагностическая подача 6123
#include "AsdaNetSubsystem.h"        // ★ волна 6: SetSelfWeight — полоса веса из 6109/6122
#include "AsdaBytes.h"               // ★ волна 6: сборка тел 6109/6122 для стенда
#include "Login/AsdaButtonRelay.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Image.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/EditableText.h"
#include "Components/PanelWidget.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Engine/DataTable.h"
#include "Engine/GameInstance.h"
#include "AsdaOutgoingGate.h"               // ★ С-Р1: общий офлайн-гейт отладочных команд (RefuseDebugWhenOnline)
#include "Login/AsdaLoginData.h"   // [ПОРТ] 08.10.2026: лимиты ввода в байтах провода (TruncateToWireLimit)
#include "Test/AsdaSelfTestKit.h"   // ★ 21.10.2026: итог самотеста asda.Pet.SelfTestNameRule
#include "Text/AsdaLang.h"          // ★ 21.10.2026: английские названия пород (оверлей pettable) в самотесте кличек
#include "Text/AsdaLangRows.h"       // [ПОРТ] 09.10.2026 (мультиязычность): формат опции питомца на языке сессии
#include "AsdaPetCageSubsystemInternal.h"   // ★ R2 10.11.2026: общее для частей AsdaPetCageSubsystem*.cpp

static const TCHAR* kCageWbp   = TEXT("/Game/AsdaAssets/UI/Player_UI/Pet/WBP_Pet_Cage_Win.WBP_Pet_Cage_Win_C");
static const TCHAR* kPanelWbp  = TEXT("/Game/AsdaAssets/UI/Player_UI/Pet/WBP_Pet_Button_Win.WBP_Pet_Button_Win_C");
static const TCHAR* kStatusWbp = TEXT("/Game/AsdaAssets/UI/Player_UI/Pet/WBP_Pet_Status_Win.WBP_Pet_Status_Win_C");

namespace PetCageLoc {
// ══════════════════════════════════════════════════════════════════════════════════════════════
//  LOC-ИДЕНТИФИКАТОРЫ. Каждый снят по СВОЕМУ обработчику эталона (закон
//  `loc-per-handler-not-by-family`), файл:строка — рядом с каждой строкой.
// ══════════════════════════════════════════════════════════════════════════════════════════════
	// Подписи окна 43000 — `CUpgradeWindow_Init@0xDC3B90` (item_enhance.c:4064-4148)
	constexpr int32 kTitle        = 100601;  // :4067  43001 «Дом для питомца»
	constexpr int32 kGradeLabel   = 101123;  // :4071  43110 «Ранг»
	constexpr int32 kFeedLabel    = 101122;  // :4075  43112 «Тип питания»
	constexpr int32 kListTitle    = 100600;  // :4081  43005 «Выбери питомца»
	constexpr int32 kNameLabel    = 100604;  // :4092  43070 «Имя»

	constexpr int32 kBtn43054     = 100603;  // :4101  43054 «Отмена»
	constexpr int32 kHint         = 109955;  // :4108  43063 «Чтобы открыть настройки питомца…»
	constexpr int32 kStageLabel   = 100605;  // :4118  43072 «Этап роста»

	constexpr int32 kGrowLabel    = 100607;  // :4127  43077 «Уровень роста»

	constexpr int32 kStat1Label   = 100609;  // :4136  43100 «ОЗ»
	constexpr int32 kStat2Label   = 100610;  // :4140  43102 «Скорость»
	constexpr int32 kStat3Label   = 100611;  // :4144  43104 «Дух»
	constexpr int32 kFuncLabel    = 100612;  // :4148  43106 «Возм. питомца»

	// Подписи панели 43058 — `upgradeWindow_BindResultLabels@0xDCBDA0` (item_enhance.c:5119-5131)
	constexpr int32 kPanelCall      = 109950; // 43065 «Призыв»
	constexpr int32 kPanelRecall    = 109951; // 43066 «Отменить призыв»
	constexpr int32 kPanelGrowth    = 109952; // 43055 «Рост питомца»
	constexpr int32 kPanelEvolution = 109953; // 43056 «Развитие питомца»
	constexpr int32 kPanelClose     = 109954; // 43057 «Закрыть»
	// ★★★ ШЕСТАЯ КНОПКА — НАША СТРОКА, В ОРИГИНАЛЕ ЕЁ НЕТ (закон `never-invent-only-original`:
	//   новое помечаем). Сверено ДВУМЯ источниками: живой `RawAssets/Text/GameText.csv`
	//   (4910 строк) и старый дамп той же таблицы `Reports/_gametext_before_11825.csv` —
	//   ни одной строки со смыслом «сведения о питомце». Ближайшая по смыслу 109960
	//   «Карта развития питомца» ЗАНЯТА заголовком окна 43501 (`AsdaPetEvolutionSubsystem.cpp:52`),
	//   брать чужую нельзя. Номер взят из ПОРТОВОГО диапазона `99xxxx`, который порт уже ведёт
	//   для собственных строк (141 запись, `GameText.csv:4770-4910`), первым свободным за 990346.
	//   ★ С разрешения владельца от 25.09.2026 строка ПЕРЕНЕСЕНА в `DT_GameText`. Переимпорт
	//   сверен ПОСТРОЧНО (`Reports/pet/petinfo/dt_before.tsv` vs `dt_after.tsv`):
	//   4909 → 4910 строк, единственное отличие во всём файле — она сама.
	//   Запасного литерала в коде БОЛЬШЕ НЕТ: подпись обязана приходить ИЗ ТАБЛИЦЫ.
	constexpr int32 kPanelInfo      = 990347; // ★ 43067 «О питомце» — ДОБАВКА ПОРТА
}

// ★★★ ПОЗИЦИЯ ОКНА — ОТСТУПЛЕНИЕ ОТ ОРИГИНАЛА ПО ТРЕБОВАНИЮ ВЛАДЕЛЬЦА (закон `fix-original-defects`).
//   ТРЕБОВАНИЕ ДОСЛОВНО: «По-умолчанию по-середине экрана, потом по сохраненному положению окна
//   после перетаскивания пользователем».
//   ЧТО ДЕЛАЕТ ОРИГИНАЛ: ставит окно по координатам .JUI (`Origin="334 223" Size="512 385"` в базе
//   1024×768) и позиций окон между сессиями НЕ ХРАНИТ ВОВСЕ — `UI_SaveWindowState@0xD61D40`
//   (`ui_core.c:54410-54467`) пишет в `WinState.bin` ровно четыре тега (`QP`, `CO`, `MO`, `SMO`:
//   страница квик-слотов, видимость чата, мини-карта, системная панель) и ни одной координаты;
//   чтение — там же, `:54559-54610`. Перетаскивание правит только рантайм-поля контрола
//   (`uiControl_RepositionByDelta@0x12F1C30`, `ui_core.c:114009-114030`).
//   ⇒ ОБЕ половины требования — наше отступление, и обе помечены здесь.
//   ГДЕ ХРАНИМ: в профиле игрока, а не в ассете — общий механизм раскладки окон порта,
//   `GameUserSettings.ini`, секция `[AsdaWindowLayout]`, ключ `Pos.Pet_Cage_Win.Pet_Cage_Win`.
//   Ничего нового не заведено (закон `check-existing-before-creating`): ручка перетаскивания
//   `PetCage_Move` у окна в данных .JUI уже есть, значит `IsWindowLayoutSaveable` для него истинно
//   и запись/восстановление работают сами.
static ASDA_DEV_CVAR(int32, CVarCageCenterOnScreen,
	TEXT("asda.Pet.CageCenterOnScreen"), 1,
	TEXT("1 (по умолчанию) — дизайн-позиция 43000 = ЦЕНТР ЭКРАНА (требование владельца), а ")
	TEXT("перетаскивание пользователя отсчитывается от центра и переживает сессии через ")
	TEXT("[AsdaWindowLayout] в GameUserSettings.ini. 0 — как в оригинале: позиция из .JUI (334,223)."),
	ECVF_Default);

// ═════════════════════════════════ ОКНО 43000: КАРКАС ═════════════════════════════════════════
bool UAsdaPetCageSubsystem::EnsureCage()
{
	const EAsdaJuiEnsure R = FAsdaJuiWindowHandle(kCageWindowId, kCageWbp, kCageZ).Ensure(
		GetGameInstance(), Cage, bCageReady,
		[this](UAsdaWindowSubsystem&) { ResetCageWidget(); },
		[this](UAsdaWindowSubsystem& WM)
		{
			SetupCageControls();
			FAsdaJuiWindowHandle::RegisterFrameZone(WM, kCageWindowId, this, Cage, bCageOpen,
				TEXT("Pet_Cage_Win_0_%d"), 9);
			// ★★★ ДЕФЕКТ ВЛАДЕЛЬЦА №4, вторая половина: «по умолчанию по середине экрана».
			//   Делается РОВНО ЗДЕСЬ, при создании виджета, и ровно один раз на виджет: к этому
			//   моменту `OpenWindowByPath` уже применил сохранённый сдвиг из ini, и общая точка
			//   `CenterWindowKeepingSavedOffset` его НЕ теряет (снимает → центрирует → возвращает).
			//   Ссылка на `RefWidget` не передаётся намеренно: центрируем по общему прямоугольнику
			//   всех корневых контролов окна, а он включает и полосу заголовка (334,201)-(846,608),
			//   то есть то, что игрок и считает «окном». Разбор отступления — у ключа
			//   `asda.Pet.CageCenterOnScreen`.
			if (CVarCageCenterOnScreen.GetValueOnGameThread() != 0)
			{
				WM.CenterWindowKeepingSavedOffset(kCageWindowId);
			}
		},
		[]()
		{
			UE_LOG(LogAsdaPetCage, Error, TEXT("[питомец-окно] WBP_Pet_Cage_Win не загрузился (%s)"), kCageWbp);
		});
	return FAsdaJuiWindowHandle::Succeeded(R) && bCageReady;
}

bool UAsdaPetCageSubsystem::EnsurePanel()
{
	const EAsdaJuiEnsure R = FAsdaJuiWindowHandle(kPanelWindowId, kPanelWbp, kPanelZ).Ensure(
		GetGameInstance(), Panel, bPanelReady,
		[this](UAsdaWindowSubsystem&) { ResetPanelWidget(); },
		[this](UAsdaWindowSubsystem& WM)
		{
			SetupPanelControls();
			FAsdaJuiWindowHandle::RegisterFrameZone(WM, kPanelWindowId, this, Panel, bPanelShown,
				TEXT("Pet_Button_Win_0_%d"), 9);
			// ★ 07.11.2026 [ИСПРАВЛЕНИЕ]: щелчок мимо гасит панель; разбор — `AsdaWindowSubsystem_TopPress.cpp`.
			WM.RegisterClickAwayMenu(kPanelWindowId, FSimpleDelegate::CreateWeakLambda(this, [this]() { HideActionPanel(); }));
			// ★★★ РОДСТВО ОБЪЯВЛЯЕТСЯ ЗДЕСЬ, А НЕ В `OpenCage` (поправка живого прогона 24.09.2026).
			//   Панель — ЛЕНИВОЕ окно: при открытии 43000 её ещё нет в реестре, и `SetWindowOwner`
			//   оттуда молча не находил цели. Живой прогон это и поймал: 43000 поднялось на z 746,
			//   а панель осталась на своём 742 — то есть ПОД окном, из-под которого её не видно.
			//   В точке создания цель заведомо существует, и `RaiseOwnedAbove` внутри
			//   `SetWindowOwner` сразу ставит панель на ступень выше владельца.
			WM.SetWindowOwner(kPanelWindowId, kCageWindowId);
		},
		[]()
		{
			UE_LOG(LogAsdaPetCage, Error, TEXT("[питомец-окно] WBP_Pet_Button_Win не загрузился"));
		});
	return FAsdaJuiWindowHandle::Succeeded(R) && bPanelReady;
}

bool UAsdaPetCageSubsystem::EnsureStatus()
{
	const EAsdaJuiEnsure R = FAsdaJuiWindowHandle(kStatusWindowId, kStatusWbp, kStatusZ).Ensure(
		GetGameInstance(), Status, bStatusReady,
		[this](UAsdaWindowSubsystem&) { ResetStatusWidget(); },
		[this](UAsdaWindowSubsystem& WM)
		{
			SetupStatusControls();
			// ★★★ ПЛАШКА 43200 РОДСТВА НЕ ПОЛУЧАЕТ — И ЭТО НЕ УПУЩЕНИЕ (поправка ревизии, СРЕДН-1).
			//   Сначала она была объявлена дочерней вместе с панелью 43058, и это вносило НОВЫЙ
			//   дефект: `RaiseOwnedAbove` держит потомка на «z владельца + 1», то есть от 733 и
			//   выше, а `kStatusZ = 705` объявлен слоем HUD, НИЖЕ обычных окон. Плашка же
			//   показывается сама, как только питомец призван, — и висела бы поверх инвентаря
			//   (720) и любого чужого окна ПОСТОЯННО.
			//   ЭТАЛОН ЕЁ НЕ ПОДНИМАЕТ НИКОГДА: на контрол 43200 во всём бинаре одна ссылка
			//   (`AsdaStory_game.c:411326`), и `CClass_MapInsert` рядом с ней нет ни разу — в
			//   отличие от 43000 (`item_enhance.c:3831`) и панели 43058 (`item_enhance.c:5147`).
			FAsdaJuiWindowHandle::RegisterFrameZone(WM, kStatusWindowId, this, Status, bStatusShown,
				TEXT("Pet_Status_Win_0_%d"), 1);
		},
		[]()
		{
			UE_LOG(LogAsdaPetCage, Error, TEXT("[питомец-окно] WBP_Pet_Status_Win не загрузился"));
		});
	return FAsdaJuiWindowHandle::Succeeded(R) && bStatusReady;
}

void UAsdaPetCageSubsystem::ResetCageWidget()
{
	if (UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->UnregisterUiZone(kCageWindowId);
		WM->CloseWindow(kCageWindowId);
	}
	Cage = nullptr;
	Relays.Reset();   // ★ поправка ревизии (МЕЛК-6): мостики окна 43000 умирают вместе с виджетом
	// ★ Волна 9: виджет уничтожен ⇒ указатель на его план превью повис бы. Сам актор сцены и его
	//   render-target НЕ трогаем: эталон при закрытии окна буфер не освобождает
	//   (`releaseThreeComObjects` зовётся лишь при device lost и переинициализации окна).
	ImgPreview3d = nullptr;
	bPreviewBrushSet = false;
	for (FAsdaPetCageCell& C : Cells) { C = FAsdaPetCageCell(); }
	for (TObjectPtr<UWidget>& W : RowGroups) { W = nullptr; }
	SelMarker = nullptr; SelMarkerHost = nullptr;
	BtnClose = BtnTrash = BtnName = nullptr;
	TxtTitle = TxtListTitle = TxtNameLabel = nullptr;
	EdtName = nullptr;
	TxtStageLbl = TxtStage = TxtExpLbl = TxtExp = TxtGrowLbl = TxtGrow = TxtNutLbl = TxtNut = nullptr;
	TxtStat1Lbl = TxtStat1 = TxtStat2Lbl = TxtStat2 = TxtStat3Lbl = TxtStat3 = nullptr;
	TxtFuncLbl = TxtOpt0 = TxtOpt1 = TxtOpt2 = nullptr;
	TxtGradeLbl = TxtGrade = TxtFeedLbl = TxtTaste0 = TxtTaste1 = TxtTaste2 = nullptr;
	TxtHint = nullptr;
	Grp43061 = Grp43062 = nullptr; Txt43053 = Txt43054 = nullptr;
	GaugeExp = FAsdaPetCageGauge();
	GaugeNut = FAsdaPetCageGauge();
	bCageReady = false;
	HoverSlot = -1; TipBar = -1;
	Drag.Disarm();
	Mouse.Reset();
	StatusIconPress.Clear();       // ★ поправка ревизии: и пара иконки 43205 тоже
}

void UAsdaPetCageSubsystem::ResetPanelWidget()
{
	if (UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->UnregisterUiZone(kPanelWindowId);
		WM->CloseWindow(kPanelWindowId);
	}
	Panel = nullptr;
	BtnCall = BtnRecall = BtnGrowth = BtnEvolution = BtnPanelNo = nullptr;
	TxtCall = TxtRecall = TxtGrowth = TxtEvolution = TxtPanelNo = nullptr;
	// ★ Клоны шестой кнопки жили в дереве уничтожаемого виджета — обнулить обязательно,
	//   иначе указатели повиснут на мёртвом поддереве (та же грабля, что у `ImgPreview3d`).
	BtnInfo = nullptr;
	TxtInfo = nullptr;
	PanelRelays.Reset();   // ★ поправка ревизии (МЕЛК-6): мостики панели умирают вместе с виджетом
	bPanelReady = false;
	bPanelShown = false;
}

void UAsdaPetCageSubsystem::ResetStatusWidget()
{
	if (UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->UnregisterUiZone(kStatusWindowId);
		WM->CloseWindow(kStatusWindowId);
	}
	Status = nullptr;
	TxtStatusName = TxtStatusStage = nullptr;
	ImgStatusIcon = nullptr; StatusIconHit = nullptr;
	GaugeStatusExp = FAsdaPetCageGauge();
	GaugeStatusNut = FAsdaPetCageGauge();
	bStatusReady = false;
	bStatusShown = false;
}

// Снять «жёлоб/заливка» полосы: базовый отступ и полная ширина берутся с жёлоба ОДИН раз.
static void BindGauge(FAsdaPetCageGauge& G, UImage* Track, UImage* Fill)
{
	G.Track = Track;
	G.Fill  = Fill;
	FVector2D Pos, Size;
	if (Track && AsdaUi::SlotRect(Track, Pos, Size))
	{
		G.BaseLeft = (float)Pos.X;
		G.FullW    = (float)Size.X;
	}
}

// Разбор ВСЕХ контролов окна 43000 (закон `full-structural-analysis-always`).
//   ★ КОЛЛИЗИИ ИМЁН СНЯТЫ ПО НОМЕРУ, А НЕ ПО ИМЕНИ: в `.JUI` два разных контрола носят одно имя
//   (43072 и 43112 оба `Pet_Cage_StatTxt5`; 43074 и 43110 оба `Pet_Cage_StatTxt6`).
//   Импортёр развёл их суффиксом `_1` у ВТОРОГО вхождения, и различить их можно только так:
//     43072 `Pet_Cage_StatTxt5`   (этап роста) ↔ 43112 `Pet_Cage_StatTxt5_1` (тип питания)
//     43074 `Pet_Cage_StatTxt6`   (опыт)       ↔ 43110 `Pet_Cage_StatTxt6_1` (ранг)
//   Соответствие подтверждено ЖИВОЙ ГЕОМЕТРИЕЙ дампа (`Reports/pet/ui_dump/43000_*.md`,
//   строки idx 108/118/131/133) и составом родителей (`_grp` против `_grp_1`).
void UAsdaPetCageSubsystem::SetupCageControls()
{
	bCageReady = false;
	if (!Cage || !Cage->WidgetTree) { return; }

	TArray<UWidget*> All;
	Cage->WidgetTree->GetAllWidgets(All);

	// Временные накопители планов полос.
	UImage* ExpTrack = nullptr; UImage* ExpFill = nullptr;
	UImage* NutTrack = nullptr; UImage* NutFill = nullptr;

	for (UWidget* W : All)
	{
		if (!W) { continue; }
		const FString N = W->GetName();

		if (UButton* B = Cast<UButton>(W))
		{
			if (N == TEXT("Pet_Cage_ClsBtn"))  { BtnClose = B; continue; }
			if (N == TEXT("PetCage_DelBtn"))   { BtnTrash = B; continue; }
			if (N == TEXT("Pet_Cage_NameBtn")) { BtnName  = B; continue; }
			continue;
		}
		if (UEditableText* E = Cast<UEditableText>(W))
		{
			if (N == TEXT("Pet_Cage_NameInput")) { EdtName = E; }
			continue;
		}
		if (UTextBlock* T = Cast<UTextBlock>(W))
		{
			if (N == TEXT("Pet_Cage_Title"))          { TxtTitle     = T; continue; }
			if (N == TEXT("Pet_Cage_SelTitle_Text"))  { TxtListTitle = T; continue; }
			if (N == TEXT("Pet_Cage_NameTxt"))        { TxtNameLabel = T; continue; }
			if (N == TEXT("Pet_Cage_StatTxt5"))       { TxtStageLbl  = T; continue; }  // 43072
			if (N == TEXT("Pet_Cage_Stat5"))          { TxtStage     = T; continue; }  // 43073
			if (N == TEXT("Pet_Cage_StatTxt6"))       { TxtExpLbl    = T; continue; }  // 43074
			if (N == TEXT("Pet_Cage_EXP"))            { TxtExp       = T; continue; }  // 43076
			if (N == TEXT("Pet_Cage_StatTxt8"))       { TxtGrowLbl   = T; continue; }  // 43077
			if (N == TEXT("Pet_Cage_Stat8"))          { TxtGrow      = T; continue; }  // 43079
			if (N == TEXT("Pet_Cage_StatTxt7"))       { TxtNutLbl    = T; continue; }  // 43080
			if (N == TEXT("Pet_Cage_Nut"))            { TxtNut       = T; continue; }  // 43082
			if (N == TEXT("Pet_Cage_StatTxt1"))       { TxtStat1Lbl  = T; continue; }  // 43100
			if (N == TEXT("Pet_Cage_Stat1"))          { TxtStat1     = T; continue; }  // 43101
			if (N == TEXT("Pet_Cage_StatTxt2"))       { TxtStat2Lbl  = T; continue; }  // 43102
			if (N == TEXT("Pet_Cage_Stat2"))          { TxtStat2     = T; continue; }  // 43103
			if (N == TEXT("Pet_Cage_StatTxt3"))       { TxtStat3Lbl  = T; continue; }  // 43104
			if (N == TEXT("Pet_Cage_Stat3"))          { TxtStat3     = T; continue; }  // 43105
			if (N == TEXT("Pet_Cage_StatTxt4"))       { TxtFuncLbl   = T; continue; }  // 43106
			if (N == TEXT("Pet_Cage_Option0"))        { TxtOpt0      = T; continue; }  // 43107
			if (N == TEXT("Pet_Cage_Option1"))        { TxtOpt1      = T; continue; }  // 43108
			if (N == TEXT("Pet_Cage_Option2"))        { TxtOpt2      = T; continue; }  // 43109
			if (N == TEXT("Pet_Cage_StatTxt6_1"))     { TxtGradeLbl  = T; continue; }  // 43110 ★ коллизия
			if (N == TEXT("Pet_Cage_Grade"))          { TxtGrade     = T; continue; }  // 43111
			if (N == TEXT("Pet_Cage_StatTxt5_1"))     { TxtFeedLbl   = T; continue; }  // 43112 ★ коллизия
			if (N == TEXT("Pet_Cage_Taste0"))         { TxtTaste0    = T; continue; }  // 43113
			if (N == TEXT("Pet_Cage_Taste1"))         { TxtTaste1    = T; continue; }  // 43114
			if (N == TEXT("Pet_Cage_Taste2"))         { TxtTaste2    = T; continue; }  // 43115
			if (N == TEXT("Pet_Cage_Win_TXT00"))      { TxtHint      = T; continue; }  // 43063
			if (N == TEXT("WkBtn_W100_148_txt"))      { Txt43053     = T; continue; }  // подпись 43053
			if (N == TEXT("RdBtn_W100_153_txt"))      { Txt43054     = T; continue; }  // подпись 43054
			// Подпись слота 43140+i — в оригинале она пустая, но её надо очистить.
			for (int32 i = 0; i < kSlots; ++i)
			{
				if (N == FString::Printf(TEXT("Pet_Cage_Pet%02d"), i)) { Cells[i].Label = T; break; }
			}
			continue;
		}
		// Контейнеры рядов 43020…43023 и родители пары 43053/43054.
		if (N == TEXT("Btn_CBase_grp"))   { Grp43061 = W; continue; }
		if (N == TEXT("Btn_CBase_grp_1")) { Grp43062 = W; continue; }
		if (N == TEXT("PetCage_SelBase_grp")) { SelMarkerHost = W; continue; }
		{
			bool bRow = false;
			for (int32 R = 0; R < kRowContainers && !bRow; ++R)
			{
				if (N == FString::Printf(TEXT("PetCage_ListBase%d_grp"), R + 1)) { RowGroups[R] = W; bRow = true; }
			}
			if (bRow) { continue; }
		}

		// Планы: `{idc}_{juiIdx}_{plane}`.
		FString Idc; int32 JuiIdx = 0, PlaneIdx = 0;
		if (AsdaUi::ParsePlaneName(N, Idc, JuiIdx, PlaneIdx))
		{
			if (Idc == TEXT("Pet_Cage_PetSel")) { SelMarker = W; continue; }
			if (UImage* Img = Cast<UImage>(W))
			{
				if (Idc == TEXT("Pet_Cage_EXPBar")) { (PlaneIdx == 0 ? ExpTrack : ExpFill) = Img; continue; }
				if (Idc == TEXT("Pet_Cage_NutBar")) { (PlaneIdx == 0 ? NutTrack : NutFill) = Img; continue; }
				for (int32 i = 0; i < kSlots; ++i)
				{
					if (Idc == FString::Printf(TEXT("Pet_Cage_Pet%02d"), i)) { Cells[i].Icon = Img; break; }
				}
			}
			continue;
		}
	}

	BindGauge(GaugeExp, ExpTrack, ExpFill);
	BindGauge(GaugeNut, NutTrack, NutFill);

	// ★ ВОЛНА 9: план превью 43052 `PetCage_Preview_Win`. Имя несёт индекс `.JUI` и суффикс
	//   плана (`_<idx>_0`), поэтому ищем по ПРЕФИКСУ — но ОБЯЗАТЕЛЬНО с проверкой типа:
	//   ⚠ в этом же окне есть ОДНОИМЁННЫЙ `UTextBlock` (эталон держит по 43052 ещё и подпись,
	//   `item_enhance.c:4089` — поле там названо `pPetNameText`, это миснейм). Поиск «первого
	//   по префиксу» мог бы вернуть именно его, и превью молча осталось бы пустым.
	ImgPreview3d = nullptr;
	for (UWidget* W : All)
	{
		if (UImage* Img = Cast<UImage>(W))
		{
			if (Img->GetName().StartsWith(TEXT("PetCage_Preview_Win")))
			{
				ImgPreview3d = Img;
				break;
			}
		}
	}
	bPreviewBrushSet = false;
	if (!ImgPreview3d)
	{
		UE_LOG(LogAsdaPetCage, Warning,
			TEXT("[питомец-окно] контрол превью 43052 `PetCage_Preview_Win` не найден — кадр ")
			TEXT("рисовать некуда (вращение при этом всё равно работает: угол живёт в сцене)"));
	}

	// ── Кнопки ──
	auto Bind = [this](UButton* Btn, int32 IdNum, TFunction<void(int32)>&& Fn)
	{
		if (!Btn) { return; }
		AsdaUi::BindButton(this, Btn, IdNum, MoveTemp(Fn), Relays);   // ★ U20: реле клика — общая точка
	};
	Bind(BtnClose, 43002, [this](int32) { CloseCage(); });
	Bind(BtnTrash, 43051, [this](int32) { OnTrashClicked(); });
	Bind(BtnName,  43083, [this](int32) { OnNameChangeClicked(); });

	// ── Статичные подписи: ровно те же loc, что ставит `CUpgradeWindow_Init` ──
	auto SetLoc = [this](UTextBlock* T, int32 Id)
	{
		if (!T) { return; }
		const FString S = Loc(Id);
		if (!S.IsEmpty()) { T->SetText(FText::FromString(S)); }
	};
	SetLoc(TxtTitle,     PetCageLoc::kTitle);
	SetLoc(TxtGradeLbl,  PetCageLoc::kGradeLabel);
	SetLoc(TxtFeedLbl,   PetCageLoc::kFeedLabel);
	SetLoc(TxtListTitle, PetCageLoc::kListTitle);
	SetLoc(TxtNameLabel, PetCageLoc::kNameLabel);
	SetLoc(TxtStageLbl,  PetCageLoc::kStageLabel);
	SetLoc(TxtExpLbl,    PetCageLoc::kExpLabel);
	SetLoc(TxtGrowLbl,   PetCageLoc::kGrowLabel);
	SetLoc(TxtNutLbl,    PetCageLoc::kNutLabel);
	SetLoc(TxtStat1Lbl,  PetCageLoc::kStat1Label);
	SetLoc(TxtStat2Lbl,  PetCageLoc::kStat2Label);
	SetLoc(TxtStat3Lbl,  PetCageLoc::kStat3Label);
	SetLoc(TxtFuncLbl,   PetCageLoc::kFuncLabel);

	// === ДЕФЕКТ ВЛАДЕЛЬЦА №3: ТЕКСТ БЛОКА «ВОЗМ. ПИТОМЦА» ВЫХОДИЛ ЗА ПРАВЫЙ КРАЙ ОКНА =========
	//
	// ЧЕЙ ЭТО ДЕФЕКТ — НАШ. Проверено тремя независимыми звеньями:
	//   1. РАЗМЕТКА НЕ НАША И НЕ СЛОМАНА. .JUI даёт 43107..43109 Size="235 15" при Origin="10 1"
	//      внутри 43106 ⇒ абсолютный прямоугольник 607..842 при правом крае окна 846 — контрол
	//      ПОМЕЩАЕТСЯ. Наш ассет повторяет это дословно (`wbp_raw/WBP_Pet_Cage_Win.txt:394-398`),
	//      живая геометрия из дампа — тоже (607,418)-(842,434).
	//      ! ПОПРАВКА РЕВИЗИИ (МЕЛКОЕ): Origin="10 1" есть ТОЛЬКО у 43107; у заголовка блока 43106
	//      ширина 240 при x=597, то есть его правый край 837, а не 842. На вывод это не влияет,
	//      но цифру правлю — в комментарии стояла чужая.
	//   2. ТЕКСТ НЕ НАШ ПО ПРОИСХОЖДЕНИЮ, НО НАШ ПО ДЛИНЕ. Строка приходит не из `error list`, а из
	//      таблицы `PetOptionTable.BIN` (`npc_mob_pet.c:5325-5395`, запись 68 байт).
	//      *** ПОПРАВКА РЕВИЗИИ (КРИТ-4): ЗДЕСЬ БЫЛО НАПИСАНО, ЧТО «текст совпадает с живым
	//      клиентом байт в байт, мы её не переводили и не удлиняли». ЭТО НЕВЕРНО, и доказано было
	//      негодным методом: совпадение md5 с живым клиентом ничего не значит, потому что
	//      ПЕРЕВЕДЁННУЮ таблицу мы в этот живой клиент сами и выложили —
	//      AstoryClient\Tools\Translate\reports\tables\petoptiontable_missing.txt:1 «записей=48, заменено=48»,
	//      выкладка AstoryClient\Tools\Translate\reports\untranslated_deployed\petoptiontable.txt.
	//      Прямое чтение AstoryClient\Tools\Translate\out_client\_bbz_entries\petoptiontable.bin: русские строки до
	//      53 символов, непереведённый донорский остаток — до 45. ⇒ ПЕРЕВОД СТРОКУ УДЛИНИЛ.
	//      На выбор лечения это не влияет (чинить всё равно рендером, звено 3), но мотивировка
	//      обязана быть правдой.
	//   3. РАЗОШЁЛСЯ РЕНДЕР — РЕШАЮЩЕЕ ЗВЕНО. Текстовый узел рисует `ctrlRenderLabelQuad`
	//      (`ui_core.c:110604-110757`), и перенос там включается АТРИБУТОМ скина (поле 925 =
	//      JUI-атрибут `TextOutLine`, `:110742-110746`), а не всегда. Для этих узлов он включён:
	//      скин `NoImage`, `JuiSkin.ini:29835-29838` → TextOutLine="1". ⇒ эталон переносит по
	//      правому краю контрола и обрывает ниже нижнего: эта же длинная строка у него просто
	//      УСЕЧЕНА и за рамку не выходит. Наш `UTextBlock` без этих свойств рисует одну строку и
	//      не обрезает её.
	//      ! ПОПРАВКА РЕВИЗИИ (СРЕДН-5): прежняя ссылка на `CControl_DrawScissorText@0x12DF8B0`
	//      была НЕ ТА — она рисует подпись ПЛАНА и зовётся под гейтом «подпись плана не пуста», а
	//      у скина `NoImage` она пуста, то есть для 43107-43109 не зовётся вовсе.
	// ⇒ ЛЕЧИМ КОДОМ, АССЕТ НЕ НУЖЕН: это не правка РАЗМЕТКИ (размеры слотов не трогаются), а
	//   восстановление свойства ОТРИСОВКИ, которое у эталонного узла включено скином.
	// *** ПОПРАВКА РЕВИЗИИ (СРЕДН-6): СТАВИМ ВСЕМУ ДЕРЕВУ, А НЕ ЧЕТЫРЁМ УЗЛАМ. TextOutLine=1 стоит
	//   у скина `NoImage`, на котором сидят ВСЕ текстовые узлы окна, значит «ножницы» у эталона
	//   тоже у всех. Четыре узла были недоприменением.
	{
		// Признак скина: все тексты 43000 сидят на `NoImage`/`NoImage_icon`/`Black`, у всех трёх
		//   `TextOutLine="1"` (`JuiSkin.ini:29838`, `:45823`, `:32029`) ⇒ ножницы у эталона есть.
		const int32 NBounds = AsdaUi::ApplyJuiTextBoundsToTree(Cage, /*bSkinClipsText*/ true);
		UE_LOG(LogAsdaPetCage, Log,
			TEXT("[питомец-окно] эталонная рамка текста поставлена узлам: %d"), NBounds);
	}
	SetLoc(Txt43053,     PetCageLoc::kBtn43053);
	SetLoc(Txt43054,     PetCageLoc::kBtn43054);
	// ★ 43063 ОБЯЗАТЕЛЕН: при его отсутствии оригинал бьёт `MessageBoxA("43063 UI Error")`
	//   (item_enhance.c:4105-4110). Узел в ассете ЕСТЬ (`Pet_Cage_Win_TXT00`) — сценарий
	//   с окном ошибки не воспроизводится; в порту вместо окна пишем ошибку в лог.
	if (TxtHint) { SetLoc(TxtHint, PetCageLoc::kHint); }
	else { UE_LOG(LogAsdaPetCage, Error, TEXT("[питомец-окно] 43063 UI Error — узла Pet_Cage_Win_TXT00 нет")); }

	// Пара 43053/43054 скрыта `CControl_SetVisible(pSuccessBtn/pFailBtn, 0)` в `CUpgradeWindow_Init`
	//   (item_enhance.c:4113-4114). В разобранном срезе её никто не показывает.
	AsdaUi::SetShown(Grp43061, false);
	AsdaUi::SetShown(Grp43062, false);
	AsdaUi::SetShown(SelMarker, false);

	// Поле имени: лимит 14 символов (`ctrlEditSetTextLength`).
	if (EdtName)
	{
		EdtName->SetText(FText::GetEmpty());
		EdtName->SetJustification(ETextJustify::Left);
	}

	bCageReady = (Cells[0].Icon != nullptr && BtnClose != nullptr && EdtName != nullptr);
	UE_LOG(LogAsdaPetCage, Log,
		TEXT("[питомец-окно] 43000 разобрано: иконок %d, рядов %d, маркер 43050 %s, корзина 43051 %s, ")
		TEXT("поле 43071 %s, кнопка 43083 %s, подсказка 43063 %s, пара 43053/43054 %s/%s"),
		Cells.Num(), RowGroups.Num(), SelMarker ? TEXT("есть") : TEXT("НЕТ"),
		BtnTrash ? TEXT("есть") : TEXT("НЕТ"), EdtName ? TEXT("есть") : TEXT("НЕТ"),
		BtnName ? TEXT("есть") : TEXT("НЕТ"), TxtHint ? TEXT("есть") : TEXT("НЕТ"),
		Txt43053 ? TEXT("есть") : TEXT("НЕТ"), Txt43054 ? TEXT("есть") : TEXT("НЕТ"));
}

void UAsdaPetCageSubsystem::SetupPanelControls()
{
	bPanelReady = false;
	// ★ Клоны шестой кнопки принадлежали ПРЕЖНЕМУ дереву виджетов. Разбор идёт по
	//   СВЕЖЕМУ виджету, значит старые указатели обязаны умереть ЗДЕСЬ, а не только в `ResetPanelWidget`:
	//   иначе гейт `if (BtnInfo) return true;` в `BuildPanelInfoButton` решит, что кнопка уже есть,
	//   и новая панель останется БЕЗ шестой кнопки (молча).
	BtnInfo = nullptr;
	TxtInfo = nullptr;
	if (!Panel || !Panel->WidgetTree) { return; }

	TArray<UWidget*> All;
	Panel->WidgetTree->GetAllWidgets(All);
	for (UWidget* W : All)
	{
		if (!W) { continue; }
		const FString N = W->GetName();
		if (UButton* B = Cast<UButton>(W))
		{
			if (N == TEXT("Pet_Button_1"))  { BtnCall      = B; continue; }
			if (N == TEXT("Pet_Button_2"))  { BtnRecall    = B; continue; }
			if (N == TEXT("Pet_Button_3"))  { BtnGrowth    = B; continue; }
			if (N == TEXT("Pet_Button_4"))  { BtnEvolution = B; continue; }
			if (N == TEXT("Pet_Button_NO")) { BtnPanelNo   = B; continue; }
			continue;
		}
		if (UTextBlock* T = Cast<UTextBlock>(W))
		{
			if (N == TEXT("Pet_Button_1_Text"))  { TxtCall      = T; continue; }
			if (N == TEXT("Pet_Button_2_Text"))  { TxtRecall    = T; continue; }
			if (N == TEXT("Pet_Button_3_Text"))  { TxtGrowth    = T; continue; }
			if (N == TEXT("Pet_Button_4_Text"))  { TxtEvolution = T; continue; }
			if (N == TEXT("Pet_Button_NO_Text")) { TxtPanelNo   = T; continue; }
		}
	}

	auto Bind = [this](UButton* Btn, int32 IdNum, TFunction<void(int32)>&& Fn)
	{
		if (!Btn) { return; }
		AsdaUi::BindButton(this, Btn, IdNum, MoveTemp(Fn), PanelRelays);   // ★ U20: реле клика — общая точка          // ★ мостики панели живут отдельно (см. `PanelRelays` в .h)
	};
	// ★★★ ШЕСТАЯ КНОПКА — СТРОГО ДО привязок и подписей: она рождается клоном `Pet_Button_3`,
	//   и клонировать надо ДО «оживления» окна (прямое предупреждение шапки `CloneWidgetSubtree`:
	//   `UAsdaRichText::ApplyFormatted` подменяет `UTextBlock` на `URichTextBlock`, и поздний клон
	//   унаследовал бы подменённый вид). Разбор и откат — у `BuildPanelInfoButton`.
	BuildPanelInfoButton();

	Bind(BtnCall,      43065, [this](int32) { OnPanelCall(); });
	Bind(BtnRecall,    43066, [this](int32) { OnPanelRecall(); });
	Bind(BtnGrowth,    43055, [this](int32) { OnPanelGrowth(); });
	Bind(BtnEvolution, 43056, [this](int32) { OnPanelEvolution(); });
	Bind(BtnPanelNo,   43057, [this](int32) { OnPanelCancel(); });
	Bind(BtnInfo,      43067, [this](int32) { OnPanelInfo(); });      // ★ наш номер, см. `OnPanelInfo`

	// Подписи — `upgradeWindow_BindResultLabels` (item_enhance.c:5117-5131).
	auto SetLoc = [this](UTextBlock* T, int32 Id)
	{
		if (!T) { return; }
		const FString S = Loc(Id);
		if (!S.IsEmpty()) { T->SetText(FText::FromString(S)); }
	};
	SetLoc(TxtCall,      PetCageLoc::kPanelCall);
	SetLoc(TxtRecall,    PetCageLoc::kPanelRecall);
	SetLoc(TxtGrowth,    PetCageLoc::kPanelGrowth);
	SetLoc(TxtEvolution, PetCageLoc::kPanelEvolution);
	SetLoc(TxtPanelNo,   PetCageLoc::kPanelClose);
	// ★ Подпись шестой кнопки берётся ИЗ ТАБЛИЦЫ, как у пяти соседей. Отдельной веткой (а не
	//   общим `SetLoc`) только ради одного: `SetLoc` при пустой строке НИЧЕГО НЕ ДЕЛАЕТ, а клон
	//   унаследовал от донора подпись «Рост питомца» — и она осталась бы на кнопке, ВРЯ игроку.
	//   Поэтому пустая строка = громкая ошибка в лог и ПУСТАЯ подпись, а не чужая.
	if (TxtInfo)
	{
		const FString S = Loc(PetCageLoc::kPanelInfo);
		if (S.IsEmpty())
		{
			UE_LOG(LogAsdaPetCage, Error,
				TEXT("[питомец-окно] loc %d («О питомце») в DT_GameText НЕ НАЙДЕНА — подпись ")
				TEXT("шестой кнопки пуста (чужую подпись донора не оставляем). ")
				TEXT("Лечение: python Tools/import_gametext.py"), PetCageLoc::kPanelInfo);
		}
		TxtInfo->SetText(FText::FromString(S));
	}

	bPanelReady = (BtnCall != nullptr && BtnRecall != nullptr && BtnPanelNo != nullptr);
	UE_LOG(LogAsdaPetCage, Log,
		TEXT("[питомец-окно] 43058 разобрано: кнопки 43065/43066/43055/43056/43057 = %s/%s/%s/%s/%s; ")
		TEXT("★ шестая «О питомце» (наш 43067) = %s"),
		BtnCall ? TEXT("да") : TEXT("НЕТ"), BtnRecall ? TEXT("да") : TEXT("НЕТ"),
		BtnGrowth ? TEXT("да") : TEXT("НЕТ"), BtnEvolution ? TEXT("да") : TEXT("НЕТ"),
		BtnPanelNo ? TEXT("да") : TEXT("НЕТ"),
		BtnInfo ? TEXT("да") : (UAsdaPetEvoTipSubsystem::IsInfoModeOn() ? TEXT("НЕТ") : TEXT("выключена ключом")));
}

// ═══════════ ★★★ ШЕСТАЯ КНОПКА ПАНЕЛИ 43058 «О ПИТОМЦЕ» — ДОБАВКА ПОРТА ══════════════════════
//
// ЧЕГО В ОРИГИНАЛЕ НЕТ (доказано ДВУМЯ методами, закон `false-negatives-method`):
//   1) КОД: `upgradeWindow_BindResultLabels@0xDCBDA0` (item_enhance.c:5117-5131) кладёт в поля
//      `CRefineWnd + 0x2F4…+0x308` ровно ШЕСТЬ значений — саму панель 43058 и ПЯТЬ её кнопок
//      (43065, 43066, 43055, 43056, 43057), и ровно пять подписей 109950…109954. Седьмого поля
//      нет, а `upgradeWndProc@0xDC3690` разбирает те же пять номеров.
//   2) ДАННЫЕ: живой дамп разметки (`Reports/pet/ui_dump/43058_Pet_Button_Win.md`) даёт
//      6 узлов `.JUI` — корпус 158×119 и пять кнопок; `jui_raw/43058_Pet_Button_Win.tsv` тот же.
//   ⇒ Номер 43067 НАШ (в .JUI свободен), строка подписи тоже наша (см. `PetCageLoc::kPanelInfo`).
//
// ПОЧЕМУ КЛОН, А НЕ ПРАВКА АССЕТА (владелец разрешения на ассеты не давал):
//   • клон даёт БУКВАЛЬНО тот же вид — скин `SmWtBtn_W143`, шрифт, цвет, размер 143×20 и все
//     состояния наведения/нажатия, — потому что это копия соседней кнопки, а не «похожая»;
//   • ассет `WBP_Pet_Button_Win` остаётся нетронутым (`owner-asset-edits-never-lose`), а
//     переимпорт `.JUI` дописанный в ассет контрол всё равно стёр бы
//     (`tools-no-import-side-effects`);
//   • у оригинала это ШТАТНЫЙ приём размножения шаблонов (`CControl_InstantiateChild`), и порт
//     им уже пользовался — трекер заданий 1800 (6 записей в .JUI → 12 на экране).
//
// КУДА ВСТАЁТ И ЧТО ДВИГАЕТСЯ. Порядок кнопок по вертикали в `.JUI`:
//   302 призыв · 322 отзыв · 342 развитие · 362 рост · 383 ЗАКРЫТЬ (серая, отделена лишним px).
//   Новая кнопка садится ПЯТОЙ (362 + шаг), а серая «Закрыть» съезжает вниз на шаг — так
//   сохраняется и порядок «действия, потом закрытие», и зазор перед серой кнопкой.
//   Корпус панели растёт ровно на шаг: средняя полоса рамки (планы 0_3…0_5) выше на шаг,
//   нижняя (0_6…0_8) ниже на шаг. Габарит 158×119 становится 158×139.
//   ★ ЗА КРАЙ ЭКРАНА НЕ ВЫЛЕЗЕТ: `ShowActionPanel` считает корпус ОБЪЕДИНЕНИЕМ живых
//     прямоугольников всех детей и зажимает его в экран (`asda.Pet.PanelClampToScreen`),
//     то есть новая высота учитывается сама, без второго списка чисел.
//
// ЧИСЛА БЕРУТСЯ ИЗ ЖИВЫХ СЛОТОВ, А НЕ ИЗ .JUI. Причина — закон `jui-align-anchors`: писать сюда
//   362/383/395 значило бы завести ВТОРУЮ копию раскладки, которая разойдётся с ассетом при любом
//   переимпорте. Шаг берётся у самой кнопки-донора (её высота), позиции — у соседей.
bool UAsdaPetCageSubsystem::BuildPanelInfoButton()
{
	if (BtnInfo) { return true; }
	// Ключ выключен ⇒ панель обязана выглядеть ДОСЛОВНО как в оригинале: пять кнопок, 158×119.
	if (!UAsdaPetEvoTipSubsystem::IsInfoModeOn()) { return false; }
	// ★ `TxtPanelNo` в стороже ОБЯЗАТЕЛЕН (поправка ревизии, МЕЛК-5): его слот двигается ниже,
	//   и без проверки кнопка «Закрыть» уехала бы на строку вниз, а её подпись осталась на месте.
	if (!Panel || !Panel->WidgetTree || !BtnGrowth || !TxtGrowth || !BtnPanelNo || !TxtPanelNo)
	{
		return false;
	}

	UCanvasPanel* Root = Cast<UCanvasPanel>(Panel->WidgetTree->RootWidget);
	if (!Root) { return false; }

	FVector2D DonorPos(0.0, 0.0), DonorSize(0.0, 0.0);
	if (!AsdaUi::SlotRect(BtnGrowth, DonorPos, DonorSize) || DonorSize.Y <= 0.0) { return false; }
	const double Step = DonorSize.Y;          // шаг строки = высота кнопки (20 в базе .JUI)

	// ── 1. Сам клон: кнопка и её подпись — СОСЕДИ по корневой канве, поэтому клонов два.
	BtnInfo = Cast<UButton>(AsdaUi::CloneWidgetSubtree(Panel, BtnGrowth, Root,
		[](const UWidget*) { return FString(TEXT("Pet_Button_INFO_OPT")); }));
	TxtInfo = Cast<UTextBlock>(AsdaUi::CloneWidgetSubtree(Panel, TxtGrowth, Root,
		[](const UWidget*) { return FString(TEXT("Pet_Button_INFO_OPT_Text")); }));
	if (!BtnInfo || !TxtInfo)
	{
		UE_LOG(LogAsdaPetCage, Error,
			TEXT("[питомец-окно] ★ шестая кнопка НЕ склонирована (кнопка %s, подпись %s)"),
			BtnInfo ? TEXT("есть") : TEXT("НЕТ"), TxtInfo ? TEXT("есть") : TEXT("НЕТ"));
		// ★★★ ПОПРАВКА РЕВИЗИИ (СРЕДН-3): ПОЛОВИНУ, КОТОРАЯ ВСЁ-ТАКИ РОДИЛАСЬ, НАДО УБРАТЬ.
		//   `CloneWidgetSubtree` уже ВСТАВИЛ клон в корневую канву, и простого обнуления
		//   указателей мало: виджет остался бы видимым и лёг бы поверх кнопки-донора
		//   «Рост питомца». Образец уборки — `AsdaCharaInfoSubsystem.cpp:1094-1105`.
		if (BtnInfo) { BtnInfo->RemoveFromParent(); }
		if (TxtInfo) { TxtInfo->RemoveFromParent(); }
		BtnInfo = nullptr; TxtInfo = nullptr;
		return false;
	}

	// ── 2. Новая кнопка — на строку ниже донора; серая «Закрыть» и её подпись — ещё на строку ниже.
	auto ShiftY = [](UWidget* W, double Dy)
	{
		FVector2D P, S;
		if (W && AsdaUi::SlotRect(W, P, S)) { AsdaUi::SetSlotRect(W, FVector2D(P.X, P.Y + Dy), S); }
	};
	ShiftY(BtnInfo,    Step);
	ShiftY(TxtInfo,    Step);
	ShiftY(BtnPanelNo, Step);
	ShiftY(TxtPanelNo, Step);

	// ── 3. Рамка: корпус выше на шаг. ★★★ ПОПРАВКА РЕВИЗИИ (СРЕДН-1): растяжку девятичастной
	//   рамки делает ОБЩАЯ ТОЧКА `AsdaUi::StretchFramePlanesHeight`, а не свой цикл. Здесь стоял
	//   собственный `for (i < 9)` со сдвигом рядов 3..5 / 6..8 — то есть ВТОРОЙ такой хелпер,
	//   ровно то, что запрещено законом `check-existing-before-creating` и прямым текстом в
	//   `AsdaLoginGameMode.cpp:2674` («Второй такой хелпер не заводим»). Общую точку уже зовут
	//   четыре подсистемы (логин, магазин бустеров, парикмахерская, окно покупки).
	//   Имена планов — те же, по которым окно регистрирует свою зону (`RegisterFrameZone`,
	//   `Pet_Button_Win_0_%d`, 9 штук), так что второго списка имён здесь не заводится.
	TArray<TObjectPtr<UWidget>> Planes;
	Planes.Reserve(9);
	for (int32 i = 0; i < 9; ++i)
	{
		Planes.Add(AsdaUi::FindNodeExact(Panel, *FString::Printf(TEXT("Pet_Button_Win_0_%d"), i)));
	}
	// Новая высота корпуса = нынешняя + шаг. Нынешнюю берём У ЖИВЫХ ПЛАНОВ (верх ряда 0 → низ
	//   ряда 6), а не из чисел .JUI: вторая копия раскладки разошлась бы при переимпорте.
	FVector2D TopP(0.0, 0.0), TopS(0.0, 0.0), BotP(0.0, 0.0), BotS(0.0, 0.0);
	if (AsdaUi::SlotRect(Planes[0], TopP, TopS) && AsdaUi::SlotRect(Planes[6], BotP, BotS))
	{
		const float NewH = (float)((BotP.Y + BotS.Y) - TopP.Y + Step);
		if (!AsdaUi::StretchFramePlanesHeight(Planes, NewH))
		{
			UE_LOG(LogAsdaPetCage, Warning,
				TEXT("[питомец-окно] ★ рамку панели 43058 растянуть не удалось — кнопка вставлена, ")
				TEXT("но корпус остался прежним"));
		}
	}

	BtnInfo->SetVisibility(ESlateVisibility::Visible);
	TxtInfo->SetVisibility(ESlateVisibility::SelfHitTestInvisible);   // как у подписей-соседей
	AsdaUi::DisableButtonFocusRing(BtnInfo);

	// ★ Инициализация обязательна (поправка ревизии, МЕЛК-4): `AsdaUi::SlotRect` при неудаче
	//   выходов НЕ ПИШЕТ, и лог печатал бы мусор.
	FVector2D NewPos(0.0, 0.0), NewSize(0.0, 0.0);
	AsdaUi::SlotRect(BtnInfo, NewPos, NewSize);
	UE_LOG(LogAsdaPetCage, Log,
		TEXT("[питомец-окно] ★ шестая кнопка «О питомце» (наш 43067) вставлена клоном 43055 в ")
		TEXT("(%.0f,%.0f) %.0fx%.0f, корпус панели вырос на %.0f px. ДОБАВКА ПОРТА по требованию ")
		TEXT("владельца 25.09.2026, откат asda.Pet.InfoWindow 0"),
		NewPos.X, NewPos.Y, NewSize.X, NewSize.Y, Step);
	return true;
}

void UAsdaPetCageSubsystem::SetupStatusControls()
{
	bStatusReady = false;
	if (!Status || !Status->WidgetTree) { return; }

	TArray<UWidget*> All;
	Status->WidgetTree->GetAllWidgets(All);
	UImage* ExpTrack = nullptr; UImage* ExpFill = nullptr;
	UImage* NutTrack = nullptr; UImage* NutFill = nullptr;

	for (UWidget* W : All)
	{
		if (!W) { continue; }
		const FString N = W->GetName();
		if (UTextBlock* T = Cast<UTextBlock>(W))
		{
			if (N == TEXT("PetStatus_Move")) { TxtStatusName  = T; continue; }  // 43201
			if (N == TEXT("Box_Black_4_txt")){ TxtStatusStage = T; continue; }  // 43202
			if (N == TEXT("PetStatus_Icon")) { StatusIconHit  = T; continue; }  // 43205 (зона клика)
			continue;
		}
		if (UImage* Img = Cast<UImage>(W))
		{
			if (N == TEXT("PetStatus_Icon_13_0")) { ImgStatusIcon = Img; continue; }  // 43205 (план)
			if (N == TEXT("Green_Bar_9_0"))  { ExpTrack = Img; continue; }            // 43203 жёлоб
			if (N == TEXT("Green_Bar_9_1"))  { ExpFill  = Img; continue; }            // 43203 заливка
			if (N == TEXT("Orange_Bar_10_0")){ NutTrack = Img; continue; }            // 43204 жёлоб
			if (N == TEXT("Orange_Bar_10_1")){ NutFill  = Img; continue; }            // 43204 заливка
		}
	}
	BindGauge(GaugeStatusExp, ExpTrack, ExpFill);
	BindGauge(GaugeStatusNut, NutTrack, NutFill);

	bStatusReady = (TxtStatusName != nullptr && GaugeStatusExp.Track != nullptr);
	UE_LOG(LogAsdaPetCage, Log,
		TEXT("[питомец-окно] 43200 разобрано: имя 43201 %s, стадия 43202 %s, иконка 43205 %s, полосы 43203/43204 %s/%s"),
		TxtStatusName ? TEXT("есть") : TEXT("НЕТ"), TxtStatusStage ? TEXT("есть") : TEXT("НЕТ"),
		ImgStatusIcon ? TEXT("есть") : TEXT("НЕТ"),
		GaugeStatusExp.Track ? TEXT("есть") : TEXT("НЕТ"), GaugeStatusNut.Track ? TEXT("есть") : TEXT("НЕТ"));
}
