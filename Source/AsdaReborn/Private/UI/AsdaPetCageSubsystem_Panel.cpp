// Copyright (c) 2026 @sitxovski (Discord)
// AsdaPetCageSubsystem_Panel.cpp — ★ R2 10.11.2026 (разрез AsdaPetCageSubsystem.cpp, дословный перенос): строки питомцев, панели, слоты, подсказки, клики и действия панели.
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
// ★ волна 6: SetSelfWeight — полоса веса из 6109/6122   ★ R3: комментарий снятого включения "AsdaNetSubsystem.h" (файл им не пользовался) — сохранён
#include "AsdaBytes.h"               // ★ волна 6: сборка тел 6109/6122 для стенда

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
// ★ С-Р1: общий офлайн-гейт отладочных команд (RefuseDebugWhenOnline)   ★ R3: комментарий снятого включения "AsdaOutgoingGate.h" (файл им не пользовался) — сохранён
#include "Login/AsdaLoginData.h"   // [ПОРТ] 08.10.2026: лимиты ввода в байтах провода (TruncateToWireLimit)
#include "Test/AsdaSelfTestKit.h"   // ★ 21.10.2026: итог самотеста asda.Pet.SelfTestNameRule
#include "Text/AsdaLang.h"          // ★ 21.10.2026: английские названия пород (оверлей pettable) в самотесте кличек
#include "Text/AsdaLangRows.h"       // [ПОРТ] 09.10.2026 (мультиязычность): формат опции питомца на языке сессии
#include "AsdaPetCageSubsystemInternal.h"   // ★ R2 10.11.2026: общее для частей AsdaPetCageSubsystem*.cpp

namespace PetCageLoc {
	// Реле подписи 43053 — `CItemRefineUI_RefreshItemPanel` (AsdaStory_game.c:411501-411505)
	constexpr int32 kBtn43053Alt  = 101108;  // сытость == 0 → альтернативная подпись

	// Ветки кнопок панели — `upgradeWndProc@0xDC3690`
	constexpr int32 kNeedHealItem = 101104;  // :3909  43065, пилюли нет
	constexpr int32 kNotSummoned  = 101113;  // :3926  43066, питомец не призван
	constexpr int32 kBadWords     = 7009;    // :3960  43083, фильтр брани

	// ★ [ПОРТ] 21.10.2026 (владелец): клички только латиницей — наша строка «Кличка может состоять только из латинских букв и цифр.»
	constexpr int32 kLatinOnly    = 990363;

	// Подтверждения (канал B) — `item_inventory.c:51033-51117`, действия `ui_core.c:57085-57104`
	constexpr int32 kMsg55Text  = 100711;    // «Выбросить питомца…»       → 6110
	constexpr int32 kMsg55Ok    = 100601;
	constexpr int32 kMsg55No    = 100406;
	constexpr int32 kMsg56Text  = 101002;    // «Вылечить питомца пилюлей?» → 6108
	constexpr int32 kMsg5657Ok  = 100613;    // «Использовать»
	constexpr int32 kMsg5657No  = 100614;    // «отмена»
	constexpr int32 kMsg57Text  = 101109;    // «Дать питомцу имя [ %s ]?»  → 6118

	// Двухстрочные подсказки полос — `CUpgradeWindow_OnTooltip@0xDC7C40` (item_enhance.c:4304-4314)
	constexpr int32 kTipExpBody = 101304;    // тело подсказки 43203
	constexpr int32 kTipNutBody = 101303;    // тело подсказки 43204

	// Базы рядов loc: ранг `101200 + Grade`, тип питания `101300 + код`
	constexpr int32 kGradeBase  = 101200;
	constexpr int32 kTasteBase  = 101300;
}

// Цвета ранга питомца 0..8 — `CItemRefineUI_RefreshItemPanel` (AsdaStory_game.c:411515-411531),
//   сняты дословно (в эталоне это знаковые int, здесь те же биты как ARGB).
static const uint32 kGradeArgb[9] =
{
	0xFFFFFFFFu, // 0  −1
	0xFFFFFF46u, // 1  −186
	0xFFFF969Cu, // 2  −26916
	0xFF9700B2u, // 3  −6881486
	0xFFFF7700u, // 4  −34816
	0xFF40E0FFu, // 5  −12500481
	0xFFBE5FFFu, // 6  −4304641
	0xFFFA1E46u, // 7  −385466
	0xFF9BEFEBu, // 8  −6558741
};

// Цвета «типа питания» 0..2 — там же (AsdaStory_game.c:411537-411543, :411553-:411559, :411569-:411575).
static const uint32 kTasteArgb[3] = { 0xFF9700B2u, 0xFFFA1E46u, 0xFF40E0FFu };

// ★★★ ПРОГРЕВ ЛЕНИВЫХ ОКОН 43000 (ВОЛНА 8, 21.09.2026 — БЛОКЕР-1 сквозной ревизии).
//   БОЛЕЗНЬ. Два окна набора питомцев собирались ЛЕНИВО, прямо внутри обработки мыши:
//     • 43300 `Pet_Tooltips_Win` — в `UAsdaPetEvoTipSubsystem::ShowForPet`, которую зовёт
//       НАВЕДЕНИЕ на слот; наведение в порту приходит из того же покадрового опросчика, что и
//       нажатие (`FAsdaMouseFrame::Update`, `AsdaUiHelpers.cpp:709-711`), а тот ловит ФРОНТЫ
//       сравнением с прошлым КАДРОМ. Синхронная загрузка класса (≈200 мс) съедала все кадры
//       между нажатием и отпусканием, фронта не видел ни один — ПЕРВЫЙ клик игрока по питомцу
//       пропадал целиком («приходится нажимать дважды»). Доказательство — три опыта на точке
//       (397,448): настоящий клик Slate до прогрева → «выбран uid=−1»; хит-тест той же точки →
//       uid=2001; тот же клик при живом 43300 → uid=2001.
//     • 43058 `Pet_Button_Win` — в `ShowActionPanel`, которую зовёт ПРАВЫЙ клик по слоту. Та же
//       механика, та же цена, только фронт съедается у ПКМ.
//   ЛЕЧЕНИЕ. Собрать оба окна при открытии 43000 и оставить скрытыми. Приём не нов и не выдуман:
//   так уже лечены окно инвентаря (`UAsdaInventorySubsystem::PrewarmWindow`, ключ
//   `asda.Inven.Prewarm`) и пара 43400/43450 (`UAsdaPetEvolutionSubsystem::OpenEvo` — там эта же
//   грабля названа в комментарии на месте). Сборка стоит один раз за сессию: `Ensure` живое окно
//   не пересобирает.
//   ⚠ ПОЧЕМУ ЭТО НЕ СЪЕДАЕТ САМ КЛИК ОТКРЫТИЯ 43000: к моменту прогрева `OpenCage` своё дело уже
//   сделала (окно показано), а пропуск фронта отпускания на кнопке 454 инвентаря безвреден —
//   защёлка инвентаря сбрасывается по `bLmbUp` следующего кадра. Проверено живым прогоном.
// ═══════════ ДЕФЕКТ ВЛАДЕЛЬЦА №4 (24.09.2026): 43000 ОТКРЫВАЛОСЬ ПОД ИНВЕНТАРЁМ ═══════════════
//
// ПРИЧИНА (по эталону, а не по догадке). Окно получало z ОДИН РАЗ, при создании виджета
//   (`kCageZ = 732`), а `OpenCage` только переключала видимость — `SetWindowVisible` z не трогает.
//   Инвентарь же поднимает себя при КАЖДОМ показе и при каждом клике по своему телу
//   (`AsdaInventorySubsystem.cpp:1254`; подъём по клику — `AsdaWindowSubsystem.cpp:1497-1511`).
//   Клик по кнопке 454 «Дом для питомца» — это клик по телу инвентаря: инвентарь уезжал на
//   732 + 1 = 733, а 43000 показывалось на своём неподвижном 732 и оказывалось ПОД ним.
// ★★★ В ОРИГИНАЛЕ ЭТА СТРОКА ЕСТЬ, И ПОРТ ЕЁ ПРОСТО ПОТЕРЯЛ: `CRefineWnd_Open@0xDC3390`
//   (`item_enhance.c:3800-3836`) в самом конце, после `CControl_SetVisible(окно, 1)` (`:3826`) и
//   `CRefineUI_RefreshItemPanel` (`:3830`), делает
//       `CClass_MapInsert((CClass *)g_pWindowMgr, *(_DWORD *)&this->gap_0[8]);`   (`:3831`)
//   ! ПОПРАВКА РЕВИЗИИ (СРЕДН-5): ЧТО ИМЕННО ДЕЛАЕТ ЭТА СТРОКА — раньше здесь было написано
//   «обходит детей контрола», и это не так. `CClass_MapInsert` кладёт окно в очередь
//   `g_pWindowMgr + 0x1D8` обычным `push_back` (`stl_runtime.c:48012-48034`), а разбирает её
//   `CControl_DispatchMessage@0x12B2440` (`ui_core.c:105801-105814`) парой
//   `RemoveChild` + `AddChildOrdered`. То есть это ПОКАДРОВАЯ ОЧЕРЕДЬ ОТЛОЖЕННОГО ПОДЪЁМА, а
//   перестановка в списке детей происходит уже при её разборе. Вывод не меняется: порядок в
//   списке и есть z (рендер идёт вперёд, хит-тест назад), а порт этот подъём потерял. Наш
//   эквивалент — `BringWindowToFront`.
// *** КЛЮЧ ОТКАТА: ЗАПОЛНЯТЬ ЛИ 43079 «УРОВЕНЬ РОСТА» (24.09.2026).
//   Разбор дефекта оригинала — у места заполнения в `RefreshSelected`.
static ASDA_DEV_CVAR(int32, CVarFillGrowLevel,
	TEXT("asda.Pet.FillGrowLevel"), 1,
	TEXT("1 (по умолчанию) - заполнять поле 43079 «Уровень роста» процентом, который оригинал ")
	TEXT("считает и ТЕРЯЕТ (sprintf в общий буфер без SetText, item_inventory.c:54554-54559). ")
	TEXT("0 - дословно оригиналу: поле остаётся пустым."),
	ECVF_Default);

// ★★★ ПОРТ `strFilterCharsKeepAfterNameChar@0xBB2B90` (AsdaStory_game.c:228101-228123).
//   Эталон выбрасывает из имени символы этого списка, НО оставляет их, когда предыдущий байт —
//   «валидный байт имени по языку» (`isValidNameCharByLanguage`), то есть когда символ оказался
//   ВТОРЫМ БАЙТОМ двухбайтовой буквы CP1251/CP949. В порте строка — UTF-16, второго байта
//   буквы отдельным символом не бывает вовсе, поэтому исключение недостижимо и фильтр вырождается
//   в «убрать все перечисленные символы». Список дословно из вызовов эталона
//   (AsdaStory_game.c:411488 — откат поля, :413560 — сборка тела 6118).
static const TCHAR* kPetNameBadChars = TEXT("\t ~`!@#%^&*()=_+{}[];':\"\\|,/<>?-$.^");

// Вернуть имя без запрещённых символов.
static FString FilterPetName(const FString& In)
{
	FString Out;
	Out.Reserve(In.Len());
	for (const TCHAR C : In)
	{
		if (!FCString::Strchr(kPetNameBadChars, C)) { Out.AppendChar(C); }
	}
	return Out;
}

// ═══════════════════════════════ ДОСТУП К МОДЕЛИ И ТАБЛИЦАМ ═══════════════════════════════════
UAsdaPetSubsystem* UAsdaPetCageSubsystem::Pets() const
{
	return GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaPetSubsystem>() : nullptr;
}

const FAsdaPetSlot* UAsdaPetCageSubsystem::Selected() const
{
	const UAsdaPetSubsystem* P = Pets();
	return P ? P->GetSelectedPet() : nullptr;
}

const FAsdaPetSlot* UAsdaPetCageSubsystem::Active() const
{
	const UAsdaPetSubsystem* P = Pets();
	return P ? P->GetActivePet() : nullptr;
}

int32 UAsdaPetCageSubsystem::VisibleCount() const
{
	const UAsdaPetSubsystem* P = Pets();
	if (!P) { return 0; }
	// Эталон обрывает обход списка на `nPageSlots` (= число ДОСТУПНЫХ ЯЧЕЕК), см. М2/М3 в шапке:
	//   `if (++slotIdx >= thisUI[120]) break;` (AsdaStory_game.c:411480).
	return FMath::Min(P->GetSlots().Num(), FMath::Min(P->GetPossessionCount(), kSlots));
}

// ★ Возврат ПО ЗНАЧЕНИЮ, а не указателем на функционально-локальный static: окно развития
//   (волна 5) берёт три материала и результат разом, и второй вызов молча перетёр бы первый.
bool UAsdaPetCageSubsystem::PetRow(int32 PetIndex, FAsdaPetRow& Out) const
{
	// ★ ВОЛНА 8 (дубли, пункт 7): тело переехало в общую точку `AsdaPetUi::PetRow`.
	return AsdaPetUi::PetRow(GetGameInstance(), PetIndex, Out);
}

// ★★★ ПОЗИЦИЯ, А НЕ ПОЛЕ `Level`. Эталон адресует `petexptable` ТОЛЬКО арифметикой
//   `pPetExpTable + 44 * <позиция>` (AsdaStory_game.c:411508 стадия, :411566-411570 опыт,
//   :411586-411590 три цены, :412100-412106 плашка 43200, item_enhance.c:4299-4301 подсказка).
//   А в данных `Level = позиция + 1` (`RawAssets\Data\ItemDB\petexptable.csv`: строка `_row=0`
//   имеет `Level=1`). Поиск по полю `Level` давал бы строку на ОДНУ ВЫШЕ: питомцу уровня 2 —
//   «Зародыш(1)» вместо «Незрелый(2)», пороги опыта 0/333 вместо 333/733, `CostBase` 60 вместо 100.
//   Поэтому берём строку с `Level == Pos + 1`; запасной путь — имя строки `_row` (оно и есть
//   позиция), и только в последнюю очередь порядок `GetAllRows`, который ничего не гарантирует.
bool UAsdaPetCageSubsystem::ExpRow(int32 Pos, FAsdaPetexptableRow& Out) const
{
	// ★ ВОЛНА 8 (дубли, пункт 7): тело переехало в общую точку `AsdaPetUi::ExpRow` — там же
	//   и разбор «позиция, а не поле Level».
	return AsdaPetUi::ExpRow(GetGameInstance(), Pos, Out);
}

FString UAsdaPetCageSubsystem::OptionFormat(int32 OptionId) const
{
	const UAsdaItemTableSubsystem* IT = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	const UDataTable* DT = IT ? IT->GetDataTable(TEXT("petoptiontable")) : nullptr;
	if (!DT || OptionId < 0) { return FString(); }
	TArray<FAsdaPetoptiontableRow*> Rows;
	DT->GetAllRows<FAsdaPetoptiontableRow>(TEXT("petoptiontable"), Rows);
	for (FAsdaPetoptiontableRow* R : Rows)
	{
		// ★ [ПОРТ] 09.10.2026 (мультиязычность): оверлей `petoptiontable`, ключ `<_row>.Format` (строку ищет обратный индекс по указателю).
		if (R && R->Id == OptionId) { return AsdaLangRows::Text(DT, R, TEXT("Format"), R->Format); }
	}
	return Rows.IsValidIndex(OptionId) ? AsdaLangRows::Text(DT, Rows[OptionId], TEXT("Format"), Rows[OptionId]->Format) : FString();
}

// Формула эталона 1:1 (AsdaStory_game.c:411566-411572; та же в `CUpgradeWindow_OnTooltip`):
//   min = petexp[level-1].Exp × (pettable.ExpRateX10 / 10)
//   max = petexp[level  ].Exp × (pettable.ExpRateX10 / 10)
//   pct = 100 / (max − min) × (exp − min)
bool UAsdaPetCageSubsystem::ExpFraction(const FAsdaPetSlot& S, float& OutFrac) const
{
	// ★ ВОЛНА 8 (дубли, пункт 7): формула переехала в общую точку `AsdaPetUi::ExpFraction`
	//   (тело было ДОСЛОВНОЙ копией такого же в окне 43500). Клампа там нет — как у эталона.
	return AsdaPetUi::ExpFraction(GetGameInstance(), S, OutFrac);
}

// ════════════════════════════ ПЕРЕРИСОВКА: cRefineUI_ResetPanel ═══════════════════════════════
void UAsdaPetCageSubsystem::ResetPanel()
{
	if (!bCageReady) { return; }

	// 1) Все 30 иконок затираются пустой кистью (эталон рисует пустую текстуру 32×32).
	// ★ Эталон ИКОНКИ НЕ ПРЯЧЕТ: он рисует в них ПУСТУЮ текстуру 32×32
	//   (`CControl_DrawTextureRect(slot, пусто, 0, 0, 32, 32)`, AsdaStory_game.c:411355-411357),
	//   а видимость трогает только `RefreshItemPanel` и только в сторону «показать».
	for (FAsdaPetCageCell& C : Cells)
	{
		AsdaUi::ClearImageBrush(C.Icon);
		if (C.Label) { C.Label->SetText(FText::GetEmpty()); }
	}

	// 2) ★ РЯДЫ, А НЕ СТРАНИЦЫ (М3): ряд j виден при `j < PossessionCount/6 − 1`.
	const int32 Count = Pets() ? Pets()->GetPossessionCount() : 0;
	for (int32 j = 0; j < kRowContainers; ++j)
	{
		const bool bShow = (j < (Count / kSlotsPerRow) - 1);
		AsdaUi::SetShown(RowGroups[j], bShow);
	}

	// 3) Маркер выбора и все значения.
	AsdaUi::SetShown(SelMarker, false);
	if (EdtName) { EdtName->SetText(FText::GetEmpty()); }
	auto Clear = [](UTextBlock* T) { if (T) { T->SetText(FText::GetEmpty()); } };
	Clear(TxtStage); Clear(TxtExp); Clear(TxtNut); Clear(TxtGrow);
	Clear(TxtTaste0); Clear(TxtTaste1); Clear(TxtTaste2);
	Clear(TxtGrade);
	Clear(TxtStat1); Clear(TxtStat2); Clear(TxtStat3);
	Clear(TxtOpt0); Clear(TxtOpt1); Clear(TxtOpt2);
	FAsdaGaugeFill::Clip(GaugeExp.Fill, GaugeExp.BaseLeft, GaugeExp.FullW, 0.f);
	FAsdaGaugeFill::Clip(GaugeNut.Fill, GaugeNut.BaseLeft, GaugeNut.FullW, 0.f);
}

// ══════════════════ ПЕРЕРИСОВКА: CItemRefineUI_RefreshItemPanel@0xDC48C0 ══════════════════════
void UAsdaPetCageSubsystem::RefreshItemPanel()
{
	if (!bCageOpen || !bCageReady) { return; }
	ResetPanel();

	UAsdaPetSubsystem* P = Pets();
	if (!P) { return; }
	const TArray<FAsdaPetSlot>& Slots = P->GetSlots();

	// ★ Выбранный мог исчезнуть: питомца съел юнион (6113 удаляет три материала) или его
	//   выбросили (6111). Эталон в этих ветках сам зануляет `wnd[154]`; у нас модель общая,
	//   поэтому на каждой перерисовке сверяемся заново.
	if (P->GetSelectedUniqueId() != -1 && P->GetSelectedPet() == nullptr)
	{
		P->SetSelectedUniqueId(-1);
		NameShownForUid = -1;
	}

	const int32 Limit = VisibleCount();
	const FAsdaPetSlot* Sel = Selected();

	for (int32 i = 0; i < Limit && i < Slots.Num(); ++i)
	{
		const FAsdaPetSlot& S = Slots[i];
		AsdaUi::SetShown(Cells[i].Icon, true);
		FAsdaPetRow IconRow;
		if (PetRow(S.PetIndex, IconRow))
		{
			// `CControl_DrawTextureRect(slot, TexturePath(pettable+360), +392, +396, 32, 32)`.
			AsdaUi::ApplyJuiIconCell(Cells[i].Icon, IconRow.IconTexture, IconRow.IconU, IconRow.IconV, kCellPx, (float)kCellPx);
		}
		// ★★★ МАРКЕР ВЫБОРА 43050 — ТАБЛИЦА СМЕЩЕНИЙ ЭТАЛОНА (AsdaStory_game.c:411465-411478):
		//     x = позиция_иконки.x + 10
		//     y = 41 * (slot / 6), затем: slot <= 5 → y = 8; 12..17 → y −= 8; 18..23 → y −= 16;
		//         24..29 → y −= 24   ⇒ ряды дают 8 · 41 · 74 · 107 · 140, шаг РОВНО 33.
		//   Шаг 33 — это и есть шаг рядов в `.JUI` (432/465/498/531/564), поэтому таблица эталона
		//   и «смещение контейнера ряда» дают ОДНО И ТО ЖЕ. Берём числа эталона, а якорем ряда 0
		//   служит авторская позиция самого маркера из `.JUI` (10,26 внутри `PetCage_SelBase_grp`):
		//   эталонное `Class_GetVec2_74` возвращает координаты в системе, которой у нас нет,
		//   а якорь фиксирует ряд 0 на своём месте и делает всю колонку 1:1.
		if (Sel && Sel->UniqueId == S.UniqueId && SelMarker)
		{
			FVector2D IconPos, IconSize;
			if (AsdaUi::SlotRect(Cells[i].Icon, IconPos, IconSize))
			{
				if (MarkerAnchor.X < 0.0)
				{
					FVector2D MPos, MSize;
					if (AsdaUi::SlotRect(SelMarker, MPos, MSize)) { MarkerAnchor = MPos; }
				}
				int32 YOff = 41 * (i / kSlotsPerRow);
				if (i <= 5) { YOff = 8; }
				if (i >= 12 && i <= 17) { YOff -= 8; }
				if (i >= 18 && i <= 23) { YOff -= 16; }
				if (i >= 24 && i <= 29) { YOff -= 24; }
				// Якорь = (x ряда 0 столбца 0 + 10, y ряда 0) — из `.JUI`; к нему прибавляем
				//   столбец (шаг 33 по x, как у иконок) и смещение ряда из таблицы минус базовые 8.
				const double ColX = 33.0 * (double)(i % kSlotsPerRow);
				const FVector2D Pos(MarkerAnchor.X + ColX, MarkerAnchor.Y + (double)(YOff - 8));
				AsdaUi::SetSlotPos(SelMarker, Pos);
				AsdaUi::SetShown(SelMarker, true);
			}
		}
	}

	// ── Правый блок. Нет выбранного ⇒ поле имени пустое и всё (эталон, ветка `else`) ──
	if (!Sel)
	{
		if (EdtName) { EdtName->SetText(FText::GetEmpty()); }
		return;
	}
	FAsdaPetRow Pr;
	if (!PetRow(Sel->PetIndex, Pr) || Sel->Level < 1) { return; }

	// Поле имени 43071: пустое или совпадающее — подставить имя; при СМЕНЕ выбранного — тоже.
	if (EdtName)
	{
		// ★ Эталон сравнивает НЕ сырой текст, а пропущенный через фильтр запрещённых
		//   символов (AsdaStory_game.c:411487-411488): пусто после фильтра ⇒ вернуть имя питомца.
		const FString Typed = FilterPetName(EdtName->GetText().ToString());
		if (Typed.IsEmpty() || Typed == Sel->Name) { EdtName->SetText(FText::FromString(Sel->Name)); }
		if (NameShownForUid != Sel->UniqueId)
		{
			NameShownForUid = Sel->UniqueId;
			EdtName->SetText(FText::FromString(Sel->Name));
		}
	}

	// ★ РЕЛЕ ПОДПИСИ 43053: сытость != 0 → loc 100602, иначе loc 101108.
	if (Txt43053)
	{
		const FString S = Loc(Sel->Fullness != 0 ? PetCageLoc::kBtn43053 : PetCageLoc::kBtn43053Alt);
		if (!S.IsEmpty()) { Txt43053->SetText(FText::FromString(S)); }
	}

	// 43073 «стадия/(MaxLevel)» — `sprintf("%s/(%d)", petexp[level-1].StageName, slot+108)`.
	//   ★ Поле `slot+108` (`CPetSlot+0x6C`) — это MaxLevel: приёмник списка кладёт туда `wire+24`
	//   (network.c:25644), а `wire+24` у `PetRecord` = MaxLevel. Прежнее имя `usageCount` — ложь.
	if (TxtStage)
	{
		FAsdaPetexptableRow Lo;
		const bool bLo = ExpRow(Sel->Level - 1, Lo);
		TxtStage->SetText(FText::FromString(FString::Printf(TEXT("%s/(%d)"),
			bLo ? *Lo.StageName : TEXT(""), Sel->MaxLevel)));
	}

	// 43111 «ранг»: loc `101200 + Grade`, цвет по таблице.
	if (TxtGrade)
	{
		const int32 G = FMath::Clamp(Pr.Grade, 0, 8);
		const FString S = Loc(PetCageLoc::kGradeBase + Pr.Grade);
		TxtGrade->SetText(FText::FromString(S));
		TxtGrade->SetColorAndOpacity(FSlateColor(AsdaUi::SrgbU32ToLinear(kGradeArgb[G])));
	}

	// 43113/43114/43115 «тип питания»: loc `101300 + код`, три цвета; отрицательный код — пропуск.
	{
		const int32 Codes[3] = { Pr.OptRarityCode0, Pr.OptRarityCode1, Pr.OptRarityCode2 };
		UTextBlock* Dst[3] = { TxtTaste0, TxtTaste1, TxtTaste2 };
		for (int32 k = 0; k < 3; ++k)
		{
			if (!Dst[k] || Codes[k] < 0) { continue; }
			const FString S = Loc(PetCageLoc::kTasteBase + Codes[k]);
			Dst[k]->SetText(FText::FromString(S));
			Dst[k]->SetColorAndOpacity(FSlateColor(AsdaUi::SrgbU32ToLinear(kTasteArgb[FMath::Clamp(Codes[k], 0, 2)])));
		}
	}

	// 43075/43076 — полоса и процент ОПЫТА.
	{
		float Frac = 0.f;
		const bool bOk = ExpFraction(*Sel, Frac);
		FAsdaGaugeFill::Clip(GaugeExp.Fill, GaugeExp.BaseLeft, GaugeExp.FullW, FMath::Clamp(Frac, 0.f, 1.f));
		if (TxtExp)
		{
			TxtExp->SetText(FText::FromString(FString::Printf(TEXT("%.2f%%"), bOk ? Frac * 100.f : 0.f)));
		}
	}

	// 43081/43082 — полоса и процент СЫТОСТИ (эталон берёт число как есть, диапазон 0..100).
	{
		const float Nut = (float)Sel->Fullness;
		FAsdaGaugeFill::Clip(GaugeNut.Fill, GaugeNut.BaseLeft, GaugeNut.FullW, FMath::Clamp(Nut / 100.f, 0.f, 1.f));
		if (TxtNut) { TxtNut->SetText(FText::FromString(FString::Printf(TEXT("%.2f%%"), Nut))); }
	}

	// 43101/43103/43105 — три числа: `petexp[level].CostBase × pettable.<фактор>`.
	FAsdaPetexptableRow Cur;
	if (ExpRow(Sel->Level, Cur))
	{
		auto Num = [](UTextBlock* T, int32 V) { if (T) { T->SetText(FText::FromString(AsdaUi::FormatThousands(V))); } };
		Num(TxtStat1, Cur.CostBase * Pr.FeedCostFactor);
		Num(TxtStat2, Cur.CostBase * Pr.GoldRewardFactor);
		Num(TxtStat3, Cur.CostBase * Pr.ExpRewardFactor);

		// *** 43079 «УРОВЕНЬ РОСТА» — ИСПРАВЛЕНИЕ ДЕФЕКТА ОРИГИНАЛА (закон `fix-original-defects`).
		//   ЧТО ДЕЛАЕТ ЭТАЛОН: считает процент `CostBase % 100`, кладёт его `sprintf`-ом в общий
		//   буфер — и НЕ ЗОВЁТ `CControl_SetText`; следующий же `sprintf` затирает буфер ценой,
		//   и текст уходит в контрол уже ДРУГОЙ (`item_inventory.c:54554-54559`, та же ветка в
		//   `AsdaStory_game.c:411578-411582`). ⇒ у оригинала поле «Уровень роста» ПУСТО ВСЕГДА,
		//   и это видно на снимке владельца.
		//   ПОЧЕМУ ЗАПОЛНЯЕМ: число посчитано самим оригиналом и предназначалось этому контролу —
		//   потерян только вызов. Подпись 43077 «Уровень роста» при этом рисуется, то есть пустое
		//   поле выглядит как поломка, а не как замысел.
		//   ⚠ ЭТО ОТСТУПЛЕНИЕ, И ОНО ПОМЕЧЕНО: ключ отката `asda.Pet.FillGrowLevel` 0 возвращает
		//   дословное поведение оригинала (поле пустое).
		if (TxtGrow && CVarFillGrowLevel.GetValueOnGameThread() != 0)
		{
			TxtGrow->SetText(FText::FromString(
				FString::Printf(TEXT("%.2f%%"), (float)(Cur.CostBase % 100))));
		}
	}

	// 43107/43108/43109 — три бонуса: шаблон из `petoptiontable[type]` с аргументами («+»/«», значение).
	{
		UTextBlock* Dst[3] = { TxtOpt0, TxtOpt1, TxtOpt2 };
		for (int32 k = 0; k < 3; ++k)
		{
			if (!Dst[k] || !Sel->Abilities.IsValidIndex(k)) { continue; }
			const FAsdaPetAbility& A = Sel->Abilities[k];
			if (A.Type < 0) { continue; }
			const FString Fmt = OptionFormat(A.Type);
			if (Fmt.IsEmpty()) { continue; }
			const FString Sign = (A.Value >= 0) ? TEXT("+") : TEXT("");
			Dst[k]->SetText(FText::FromString(
				AsdaUi::FormatPrintfArgs(Fmt, { Sign, FString::FromInt(A.Value) })));
		}
	}
}

// ══════════════════ ПЕРЕРИСОВКА ПЛАШКИ: refineWnd_RefreshSelectedPanel@0xDC70B0 ═══════════════
void UAsdaPetCageSubsystem::RefreshStatusPanel()
{
	if (!bStatusShown || !bStatusReady) { return; }

	// Эталон СНАЧАЛА гасит всё, и только потом заполняет.
	if (TxtStatusName)  { TxtStatusName->SetText(FText::GetEmpty()); }
	if (TxtStatusStage) { TxtStatusStage->SetText(FText::GetEmpty()); }
	FAsdaGaugeFill::Clip(GaugeStatusExp.Fill, GaugeStatusExp.BaseLeft, GaugeStatusExp.FullW, 0.f);
	FAsdaGaugeFill::Clip(GaugeStatusNut.Fill, GaugeStatusNut.BaseLeft, GaugeStatusNut.FullW, 0.f);

	const FAsdaPetSlot* A = Active();
	if (!A) { HideStatusPanel(); return; }
	FAsdaPetRow Pr;
	if (!PetRow(A->PetIndex, Pr) || A->Level < 1) { return; }

	if (TxtStatusName) { TxtStatusName->SetText(FText::FromString(A->Name)); }
	if (TxtStatusStage)
	{
		FAsdaPetexptableRow Lo;
		const bool bLo = ExpRow(A->Level - 1, Lo);
		TxtStatusStage->SetText(FText::FromString(FString::Printf(TEXT("%s/(%d)"),
			bLo ? *Lo.StageName : TEXT(""), A->MaxLevel)));
	}
	AsdaUi::ApplyJuiIconCell(ImgStatusIcon, Pr.IconTexture, Pr.IconU, Pr.IconV, kCellPx, (float)kCellPx);

	float Frac = 0.f;
	ExpFraction(*A, Frac);
	FAsdaGaugeFill::Clip(GaugeStatusExp.Fill, GaugeStatusExp.BaseLeft, GaugeStatusExp.FullW, FMath::Clamp(Frac, 0.f, 1.f));
	FAsdaGaugeFill::Clip(GaugeStatusNut.Fill, GaugeStatusNut.BaseLeft, GaugeStatusNut.FullW,
		FMath::Clamp((float)A->Fullness / 100.f, 0.f, 1.f));
}

// ════════════════════════════════════ МЫШЬ ════════════════════════════════════════════════════
void UAsdaPetCageSubsystem::TickSlots()
{
	// Чужой всплывающий слой перехватывает клик первым (грабля №2 оконной подсистемы).
	if (UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		if (WM->IsPointerBlockedByTopmost(kCageWindowId)) { return; }
		// ★★★ МОДАЛЬНЫЙ ГЕЙТ (поправка финальной приёмки, СРЕДН-4).
		//   Эталон: захваченный модальный контрол забирает ВЕСЬ диспетч мыши
		//   (`ui_core.c:104149-104153`), и нижние окна событий НЕ ПОЛУЧАЮТ вовсе.
		//   ПОЧЕМУ ОДНОГО БЛОКЕРА НЕ ХВАТАЕТ: полноэкранный блокер из `RefreshModalInput`
		//   стоит в SLATE и ест только те клики, что идут через Slate (настоящие `UButton`).
		//   Наш же покадровый опрос спрашивает ГЕОМЕТРИЮ (`AsdaUi::IsCursorOverWidget`,
		//   `AsdaUiHelpers.cpp:346-355`) и про модалку не знает — поэтому под открытым нотисом
		//   КНОПКИ были мертвы, а ЯЧЕЙКИ И СЛОТЫ — ЖИВЫ. Расхождение с эталоном и источник
		//   следующего «клик прошёл, а не должен был».
		//   ⚠ Сбрасываем защёлки: иначе нажатие ДО нотиса и отпускание ПОСЛЕ его закрытия
		//   сошлись бы в клик, которого игрок не делал.
		// ★ Дедуп №2, В1-B (U9, 11.10.2026): общая точка модального гейта (ключ отката `asda.UI.ModalGatePolling`).
		if (AsdaUi::IsModalDispatchBlocked(GetGameInstance()))
		{
			Drag.Disarm();
			StatusIconPress.Clear();
			Mouse.Reset();
			return;
		}
	}

	const int32 Over = SlotUnderPoint(Mouse.Cursor);
	// ★ ВОЛНА 4 (21.09.2026): у `HoverSlot` наконец появился потребитель — окно 43300.
	//   Эталон на событии 8 (наведение вошло) зовёт `refineGetSlotItemState(wnd, a2)`
	//   (item_enhance.c:3996), а на 0xA (наведение ушло) и 0xB (ЛКМ) гасит 43300
	//   тем же `sub_930FAC(g_PetUIDialog)` (:4003, :4010). В порту событий JUI нет, поэтому
	//   «вошло/ушло» — это СМЕНА слота под курсором.
	const int32 PrevHover = HoverSlot;
	HoverSlot = Over;
	if (Over != PrevHover) { UpdateEvoTipForHover(Over); }

	// ★★★ ПРАВЫЙ КЛИК ПО СЛОТУ — событие 0xE (`uiRefineWndSelectSlot`): выбрать питомца И открыть
	//   панель действий 43058. Доказательство — САМ ТЕКСТ ПОДСКАЗКИ 43063, которую окно печатает
	//   игроку: loc 109955 = «View pet options by right clicking a Pet in storage»
	//   (`error list.txt`, сверка вторым методом с RU-вариантом `error list.bin`). «Pet options» —
	//   это и есть пять кнопок 43058 (Summon / Cancel summon / Pet limit break / Pet evolve /
	//   Close). Раньше привязка 0xE из среза не выводилась; теперь выведена.
	// *** ПОПРАВКА 24.09.2026 — ДЕФЕКТ ВЛАДЕЛЬЦА «ПКМ ПО СЛОТУ НЕ ОТКРЫВАЕТ 43058».
	//
	// ЧТО БЫЛО. Волна 8 сделала ПКМ «парной»: нажатие взводило защёлку, панель открывалась на
	//   ОТПУСКАНИИ со сверкой «отпустили над тем же слотом». Обоснованием служила ссылка на
	//   `ui_core.c:107388-107396` (флаги 0x80/0x100). Ссылка была прочитана неверно.
	//
	// ЧТО НА САМОМ ДЕЛЕ (разбор по ОПРЕДЕЛЕНИЯМ, не по именам):
	//   · флаги 0x80/0x100 ставит `CControl_OnRButtonDown@0x12DBB60` (`ui_core.c:107350-107385`),
	//     и она НЕ ЧИТАЕТ состояния кнопок мыши; окно 43000 вешает на эти события подсказку
	//     (`item_enhance.c:3993-4005`: 8 — состояние слота, 9 — подсказка у курсора, 0xA — гашение)
	//     ⇒ это защёлка НАВЕДЕНИЯ, а имена `OnRButton*` — миснеймы;
	//   · настоящее событие ПКМ — 0xE (`CControl_OnRButtonDblClick@0x12DC410`,
	//     `ui_core.c:107522-107535`), и флаги 0x80/0x100 оно не проверяет вовсе.
	//
	// ! ПОПРАВКА РЕВИЗИИ (СРЕДН-3): ЗДЕСЬ БЫЛО НАПИСАНО, ЧТО 0xE «поднимается КАЖДЫЙ кадр, пока
	//   правая зажата». ЭТО НЕВЕРНО. Байты состояния кнопок не опрашиваются покадрово: насос ввода
	//   кладёт в них значения РАЗОВО, на сообщении «правая вниз» (`input.c:5146`, `:5148`).
	//   ⇒ 0xE — это ФРОНТ НАЖАТИЯ, ровно как у нас. Никакой «потерянной функции» (панель, едущая
	//   за курсором всё удержание) у эталона нет, и ключ `asda.Pet.PanelFollowCursor` компенсирует
	//   НЕ ЕЁ: он про то, ставить ли панель под курсор в момент открытия (см. его собственный
	//   разбор), и к 0xE отношения не имеет.
	// ! ПОПРАВКА РЕВИЗИИ (СРЕДН-4): формулировка «пары у правой кнопки в оригинале НЕТ» была
	//   слишком широкой. Пара есть: 0xE вниз / 16 вверх (`CControl_OnKeyUp@0x12DC4A0`,
	//   `ui_core.c:107539-107546`, диспетчеризуется `:107888`) — симметрично ЛКМ (0xB вниз /
	//   13 вверх). НО обработчик окна 43000 `upgradeWndProc` разбирает только 5, 7, 8, 9, 0xA,
	//   0xB, 0xE, 21 и 66561: событий 13 и 16 в его switch НЕТ. ⇒ снятие парности верно ИМЕННО
	//   ДЛЯ ЭТОГО ОКНА, а не вообще.
	// ! ПОПРАВКА РЕВИЗИИ (СРЕДН-5): ссылки на точки вызова `ui_core.c:107784` и `:108130` были
	//   МЁРТВЫМИ — там `a2 == -1`, и первое же условие функции всё отбрасывает. Живая точка
	//   одна: `uiControlDispatchMouseLeft@0x12DD620` (`ui_core.c:108063`, `:108066`, `:108072`).
	//
	// ЧЕМ ПАРНОСТЬ УБИВАЛА КЛИК В ЖИВОЙ ИГРЕ (хватало любого из трёх):
	//   1) если в кадре нажатия у слота ещё не посчитана геометрия, `SlotUnderPoint` отдаёт −1, а
	//      `FAsdaPressLatch::Press` при ключе < 0 выходит МОЛЧА — защёлка не взведена;
	//   2) любой ранний выход `TickSlots` в кадре отпускания съедал отпускание;
	//   3) сдвиг мыши между нажатием и отпусканием ломал сверку.
	// ★ ПОПРАВКА РЕВИЗИИ (СРЕДН-2): УРОВНЕВЫЕ ПРОВЕРКИ СНЯТЫ. Было
	//   `Mouse.bRmbDown && !Mouse.bLmb && !Mouse.bMmb` — буквально по условию 0xE
	//   (`ui_core.c:107524-107526`: правая == 1, левая != 1, средняя != 1). Но эти байты не живут
	//   сами по себе: насос ввода на сообщении «правая вниз» ПРИНУДИТЕЛЬНО кладёт в байты 304 и
	//   306 значение −1 (`input.c:5146`, `:5148`), то есть на фронте ПКМ левая и средняя у эталона
	//   ВСЕГДА читаются как «не нажаты» и обе проверки выполняются всегда. Наша копия спрашивала
	//   ЖИВОЕ состояние кнопок — и если игрок держал ЛКМ и жал ПКМ, порт панель не открывал, а
	//   оригинал открывает. Расхождение снято: остаётся ровно фронт ПКМ над слотом.
	if (Mouse.bRmbDown && Over >= 0)
	{
		OnSlotRightClicked(Over);
		return;
	}

	// Событие 5 (LMB down) — взвод захвата иконки ради сброса на корзину 43051.
	if (Mouse.bLmbDown && Over >= 0) { Drag.Arm(Over, -1, Mouse.Cursor); }
	if (Mouse.bLmb && Drag.ShouldStart(Mouse.Cursor)) { Drag.MarkStarted(); }

	// Событие 7 (LMB up) — если отпустили НАД КОРЗИНОЙ, это выброс питомца.
	if (Mouse.bLmbUp)
	{
		const int32 Dragged = Drag.KeyA;
		const bool bWasDrag = Drag.bStarted;
		// ★ волна 8: «курсор так и не уходил с точки нажатия» — снимается ДО `Disarm`.
		const bool bStillOnSlot = Drag.IsStillClick(Mouse.Cursor);
		Drag.Disarm();
		if (bWasDrag && Dragged >= 0 && BtnTrash && AsdaUi::IsCursorOverWidget(BtnTrash))
		{
			// Эталон выбирает перетаскиваемого и открывает то же подтверждение (msgbox 55).
			if (UAsdaPetSubsystem* P = Pets())
			{
				if (P->GetSlots().IsValidIndex(Dragged)) { P->SetSelectedUniqueId(P->GetSlots()[Dragged].UniqueId); }
			}
			RefreshItemPanel();
			OnTrashClicked();
			return;
		}
		// ★★★ ВОЛНА 8, ЖИВОЙ ПРОГОН 21.09: КЛИК ЗАСЧИТЫВАЕТСЯ ПО ЗАЩЁЛКЕ, А НЕ
		//   ПО СВЕЖЕМУ ХИТ-ТЕСТУ. БЫЛО: `Over == Dragged`. Прогон показал построчно, что
		//   кадр ОТПУСКАНИЯ совпадает с «[z-порядок] Pet_Cage_Win: z 732 → 746 (перевешено в
		//   вьюпорте)» — в этом кадре у виджетов нет кэшированной геометрии, `SlotUnderPoint`
		//   возвращал −1, сверка не сходилась — и ПЕРВЫЙ КЛИК ИГРОКА ПРОПАДАЛ ДАЖЕ ПРИ
		//   ПРОГРЕТЫХ ОКНАХ. Значит у блокера-1 было ДВЕ причины, а не одна: синхронная
		//   загрузка 43300 внутри нажатия (вылечена прогревом) И эта.
		//   СТАЛО: `IsStillClick` — взведено, захват не начат, курсор не ушёл дальше порога
		//   от точки нажатия. Это НЕ ослабление сверки: уйти с иконки 32×32, не пройдя
		//   порог в 4 пикселя, невозможно; а если порог пройден, ветка уходит в
		//   перетаскивание (`bWasDrag`) и сюда не попадает вовсе.
		if (!bWasDrag && Dragged >= 0 && (Over == Dragged || bStillOnSlot))
		{
			// Событие 0xB (`refineWndSelectSlot`) — ОДИНАРНАЯ ЛКМ: ТОЛЬКО выбрать, панель не трогать.
			//   Панель открывает правый клик (событие 0xE, см. выше, loc 109955).
			OnSlotLeftClicked(Dragged);
		}
	}
}

// ★ ОБЩАЯ ТОЧКА ХИТ-ТЕСТА СЛОТА. Её зовут и покадровый опрос, и диагностическая команда
//   `asda.Pet.WindowHitTest` — чтобы проверка шла ТЕМ ЖЕ путём, каким ходит игрок
//   (закон `diagnostic-must-use-real-path`), а не мимо него.
//   Учитывается ровно то же, что и в эталоне: только ВИДИМЫЕ слоты (`VisibleCount`), первый попавший.
int32 UAsdaPetCageSubsystem::SlotUnderPoint(const FVector2D& AbsPos) const
{
	const int32 Limit = VisibleCount();
	for (int32 i = 0; i < Limit; ++i)
	{
		if (!Cells.IsValidIndex(i) || !Cells[i].Icon) { continue; }
		FVector2D Mn, Mx;
		if (!AsdaUi::WidgetAbsRect(Cells[i].Icon, Mn, Mx)) { continue; }
		if (AbsPos.X >= Mn.X && AbsPos.X < Mx.X && AbsPos.Y >= Mn.Y && AbsPos.Y < Mx.Y) { return AsdaUi::IsCoveredByHigherWindow(Cells[i].Icon, AbsPos) ? -1 : i; }
	}
	return -1;
}

void UAsdaPetCageSubsystem::TickTooltips()
{
	UAsdaItemTooltipSubsystem* Tip = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTooltipSubsystem>() : nullptr;
	if (!Tip) { return; }
	// Двухстрочные подсказки полос 43203/43204 — `CUpgradeWindow_OnTooltip@0xDC7C40`.
	//   ⚠ В окне 43000 этих номеров НЕТ: обе полосы живут в плашке 43200, и обработчик окна
	//   43000 ловит их потому, что оба окна — один объект эталона. Здесь так же.
	int32 Over = -1;
	if (bStatusShown && bStatusReady)
	{
		if (GaugeStatusExp.Track && AsdaUi::IsCursorOverWidget(GaugeStatusExp.Track)) { Over = 43203; }
		else if (GaugeStatusNut.Track && AsdaUi::IsCursorOverWidget(GaugeStatusNut.Track)) { Over = 43204; }
	}
	if (Over == TipBar) { return; }
	TipBar = Over;
	if (Over < 0) { Tip->HideTooltipFor(EAsdaTipOwner::PetCage); return; }

	FAsdaTipMetrics M; M.FontPx = 8;
	const int32 TitleLoc = (Over == 43203) ? PetCageLoc::kExpLabel : PetCageLoc::kNutLabel;
	const int32 BodyLoc  = (Over == 43203) ? PetCageLoc::kTipExpBody : PetCageLoc::kTipNutBody;
	Tip->ShowTextTip(EAsdaTipOwner::PetCage, Loc(TitleLoc), Loc(BodyLoc), M);
}

void UAsdaPetCageSubsystem::TickStatusIcon()
{
	// ★★★ ОДИНАРНОЕ нажатие ЛКМ, НЕ двойное. В эталоне это ветка `case 0xBu` того же обработчика,
	//   что и выбор слота (`item_enhance.c:4005-4008`), а событие 11 поднимается перед КАЖДЫМ
	//   обычным нажатием ЛКМ — доказательство по битам состояния в шапке `.h`.
	// ★★★ ПОПРАВКА РЕВИЗИИ ВОЛНЫ 8 (КРИТ-1): ПАРА НАЖАТИЯ ПОЯВИЛАСЬ, А СВЕРКА СТАЛА УСТОЙЧИВОЙ.
	//   БЫЛО: действие по одному лишь фронту ОТПУСКАНИЯ плюс `IsCursorOverWidget` в кадре
	//   отпускания. Две беды сразу:
	//     1) пары нет — нажатие в мире и отпускание над иконкой открывало окно 43000, чего эталон
	//        не делает (флаги `0x10`/`0x20`, `ui_core.c:107277-107315`);
	//     2) плашка 43200 тоже имеет ручку перетаскивания (`Pet_Status_Win_0_DRAG`, живой дамп
	//        `43200_Pet_Status_Win.md:13`), а значит в кадре отпускания её могли перевесить во
	//        вьюпорте — и `IsCursorOverWidget` вернул бы false при курсоре ровно на иконке.
	//   СТАЛО: тот же приём, что у слотов 43000 и узлов 42000/43400 — `FAsdaPressLatch` с
	//   `ReleaseOverOrStill`. Ключ защёлки один (43205), `KeyB` не нужен.
	if (!StatusIconHit) { return; }
	const bool bOverIcon = AsdaUi::IsCursorOverWidget(StatusIconHit);
	if (Mouse.bLmbDown)
	{
		StatusIconPress.Press(bOverIcon ? kStatusIconKey : -1, -1, Mouse.Cursor);
		return;
	}
	if (!Mouse.bLmbUp) { return; }
	if (StatusIconPress.ReleaseOverOrStill(bOverIcon ? kStatusIconKey : -1, Mouse.Cursor) < 0) { return; }
	OnStatusIconClicked();
}

// ══════════════════════════════════ ДЕЙСТВИЯ ИГРОКА ═══════════════════════════════════════════
void UAsdaPetCageSubsystem::OnSlotRightClicked(int32 SlotIndex)
{
	// Порт `uiRefineWndSelectSlot@0xDC7610` (ui_core.c:64936): выбрать слот, если он занят,
	//   и ПОКАЗАТЬ панель 43058; затем перерисовать.
	UAsdaPetSubsystem* P = Pets();
	if (!P) { return; }
	const TArray<FAsdaPetSlot>& Slots = P->GetSlots();
	P->SetSelectedUniqueId(-1);
	if (Slots.IsValidIndex(SlotIndex) && Slots[SlotIndex].UniqueId != -1)
	{
		P->SetSelectedUniqueId(Slots[SlotIndex].UniqueId);
		ShowActionPanel();
	}
	RefreshItemPanel();
	FollowSelectionInInfoWindow();
}

// ★ ВОЛНА 4: наведение на слот списка 43140+i — порт `refineGetSlotItemState@0xDC7550`
//   (item_enhance.c:4265). Вся логика веток — внутри `UAsdaPetEvoTipSubsystem::OnCageSlotHovered`,
//   здесь только решение «какой uid под курсором»: слот вне списка или пустой ⇒ −1, то есть
//   эталонная ветка `uiTooltipShowByItemId(44)`.
void UAsdaPetCageSubsystem::UpdateEvoTipForHover(int32 SlotIndex)
{
	UAsdaPetEvoTipSubsystem* Tip = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaPetEvoTipSubsystem>() : nullptr;
	if (!Tip) { return; }
	if (SlotIndex < 0)
	{
		// Курсор ушёл со списка — событие 0xA эталона: просто спрятать (`sub_930FAC`, :4003).
		// ★ 25.09: закреплённое окно «О питомце» движением мыши не гасится (см. `HideOnEtalonEvent`).
		Tip->HideOnEtalonEvent();
		return;
	}
	UAsdaPetSubsystem* P = Pets();
	int32 Uid = -1;
	if (P)
	{
		const TArray<FAsdaPetSlot>& Slots = P->GetSlots();
		if (Slots.IsValidIndex(SlotIndex)) { Uid = Slots[SlotIndex].UniqueId; }
	}
	Tip->OnCageSlotHovered(Uid);
}

void UAsdaPetCageSubsystem::OnSlotLeftClicked(int32 SlotIndex)
{
	// Порт `refineWndSelectSlot@0xDC7490` (item_enhance.c:4247): ТОЛЬКО выбрать, без панели.
	// ★ Событие 0xB эталона вдобавок гасит 43300 (`sub_930FAC(g_PetUIDialog)`, item_enhance.c:4010).
	if (UAsdaPetEvoTipSubsystem* Tip = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaPetEvoTipSubsystem>() : nullptr)
	{
		Tip->HideOnEtalonEvent();   // ★ 25.09: закреплённое окно клик по списку не закрывает
	}
	UAsdaPetSubsystem* P = Pets();
	if (!P) { return; }
	const TArray<FAsdaPetSlot>& Slots = P->GetSlots();
	P->SetSelectedUniqueId(-1);
	if (Slots.IsValidIndex(SlotIndex) && Slots[SlotIndex].UniqueId != -1)
	{
		P->SetSelectedUniqueId(Slots[SlotIndex].UniqueId);
	}
	RefreshStatusPanel();
	RefreshItemPanel();
	FollowSelectionInInfoWindow();
}

void UAsdaPetCageSubsystem::OnStatusIconClicked()
{
	// Эталон: `case 0xBu` → `if (a2 == 43205) CRefineWnd_Open(wnd);` (item_enhance.c:4005-4008).
	OpenCage();
}

// Корзина 43051 / сброс на неё — `uiItemTooltip_RefreshIfActive@0xDC76E0` (ui_core.c:64956):
//   есть выбранный ⇒ msgbox 55 (текст 100711), по «ОК» — 6110.
void UAsdaPetCageSubsystem::OnTrashClicked()
{
	const FAsdaPetSlot* Sel = Selected();
	if (!Sel) { return; }
	if (bConfirmOpen) { return; }
	UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM) { return; }
	const FString Text = Loc(PetCageLoc::kMsg55Text);
	if (Text.IsEmpty()) { return; }
	const int32 Uid = Sel->UniqueId;
	bConfirmOpen = true;
	AsdaEnterGuard::NoEnter(WM)->ShowGameConfirm(Text, [this, Uid](bool bYes)
	{
		bConfirmOpen = false;
		if (!bYes) { return; }
		// Отправка отложенная: `RequestRelease` кладёт запрос в очередь волны 1 и шлёт из её `Tick`.
		if (UAsdaPetSubsystem* P = Pets()) { P->RequestRelease(Uid); }
	}, /*GaugeGateSec*/ 0.f, PetCageLoc::kMsg55Ok, PetCageLoc::kMsg55No, /*очередь: эталон (1, 55) ui_core.c:64958*/55, 1);
}

// Кнопка 43083 «Change» — `upgradeWndProc`, a2 == 43083 (item_enhance.c:3940-3973).
void UAsdaPetCageSubsystem::OnNameChangeClicked()
{
	const FAsdaPetSlot* Sel = Selected();
	if (!Sel || !EdtName) { return; }
	// Гейт эталона (item_enhance.c:3941-3944): менять можно, ТОЛЬКО пока имя равно
	//   дефолтному из таблицы пород (`strcmp(slot+12, pPetTable[472*idx + 4])`); иначе — нотис 101118
	//   и откат поля. ★ Строки породы нет ⇒ сравнивать не с чем, и `strcmp` эталона на мусоре
	//   даёт «не равно» — то есть гейт ЗАКРЫТ. Раньше здесь он молча пропускался.
	// ★ [ИСПРАВЛЕНИЕ] 21.10.2026: «имя по умолчанию» берётся из СЕРВЕРНОЙ таблицы пород (`DT_PetInfo.DefaultName` — копия
	//   `PetInfo_Table.txt`), ровно с ней сравнивает и сервер (GameServer.c:36262-36278 → иначе код 3). Эталон сравнивал с
	//   клиентской pettable, потому что в каждой сборке оригинала её названия совпадали с серверными; в порте названия пород
	//   переведены («Утка-механик»), и гейт был закрыт у всех игроков. Строки сервера нет — прежнее сравнение с pettable.
	FAsdaPetRow Pr;
	const bool bHaveRow = PetRow(Sel->PetIndex, Pr);
	const UAsdaPetSubsystem* PS = Pets();
	const FAsdaPetInfoRow* Srv = PS ? PS->GetPetInfo(Sel->PetIndex) : nullptr;
	const bool bDefaultName = Srv ? (Sel->Name == Srv->DefaultName) : (bHaveRow && Sel->Name == Pr.Name);
	if (!bDefaultName)
	{
		NoticeMsgBox(PetCageLoc::kNameLocked);
		EdtName->SetText(FText::FromString(Sel->Name));
		return;
	}

	// Имя чистится тем же фильтром, что и у эталона перед сборкой тела 6118
	//   (`refineSendStartByNameSecure`, AsdaStory_game.c:413560), и режется до 14 символов.
	FString Typed = FilterPetName(EdtName->GetText().ToString());
	// ★ [ПОРТ] 08.10.2026: лимит в БАЙТАХ провода (поле 6118 — 16 б, маркер cp1256 бесплатен: арабская кличка — 14 букв).
	Typed = FAsdaLoginData::TruncateToWireLimit(Typed, kNameMaxChars, 16);

	// ★ [ПОРТ] 21.10.2026: только латиница и цифры (арабская и русская клички запрещены — слово владельца).
	if (!IsLatinPetName(Typed))
	{
		NoticeMsgBox(PetCageLoc::kLatinOnly);
		return;
	}

	// Фильтр брани — ОБЩАЯ точка порта (`Chat_MatchBannedWord@0xBB2830`).
	if (UAsdaChatWindowSubsystem* Chat = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaChatWindowSubsystem>() : nullptr)
	{
		if (Chat->IsCursed(Typed))
		{
			NoticeMsgBox(PetCageLoc::kBadWords, /*эталон (0, 7009), item_enhance.c:3961*/7009);
			return;
		}
	}

	if (bConfirmOpen) { return; }
	UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM) { return; }
	// msgbox 57: текст `sprintf(loc 101109, имя из поля ввода)`.
	const FString Text = AsdaUi::FormatPrintfArgs(Loc(PetCageLoc::kMsg57Text), { Typed });
	if (Text.IsEmpty()) { return; }
	const int32 Uid = Sel->UniqueId;
	bConfirmOpen = true;
	AsdaEnterGuard::NoEnter(WM)->ShowGameConfirm(Text, [this, Uid, Typed](bool bYes)
	{
		bConfirmOpen = false;
		if (!bYes) { return; }
		if (UAsdaPetSubsystem* P = Pets()) { P->RequestRename(Uid, Typed); }
	}, 0.f, PetCageLoc::kMsg5657Ok, PetCageLoc::kMsg5657No, /*очередь: эталон (1, 57) item_enhance.c:3965*/57, 1);
}

// 43065 «Призыв» — `upgradeWndProc`, a2 == 43065 (item_enhance.c:3868-3920).
void UAsdaPetCageSubsystem::OnPanelCall()
{
	const FAsdaPetSlot* Sel = Selected();
	if (Sel)
	{
		if (Sel->Fullness != 0)
		{
			// Сытость есть: призываем ТОЛЬКО если питомец ещё не призван. Если уже призван —
			//   оригинал не делает НИЧЕГО (ветки `else` у `if (!*(_BYTE*)wnd[154])` нет).
			if (!Sel->bIsActive)
			{
				if (UAsdaPetSubsystem* P = Pets()) { P->RequestCallPet(Sel->UniqueId); }
			}
		}
		else
		{
			// Сытость 0 — «питомец без сил». Ищем в сумке 2 предмет subType 105
			//   (порт `invFindItemByType105@0xDC8EE0`, item_inventory.c:54596: 60 ячеек контейнера 2).
			int32 FoundContainer = -1, FoundCell = -1;
			UAsdaInventorySubsystem* Inv = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
			const UAsdaItemTableSubsystem* IT = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
			if (Inv && IT)
			{
				for (int32 i = 0; i < 60; ++i)
				{
					const FAsdaItemInstance* It = Inv->GetCell(2, i);
					if (!It || It->IsEmpty()) { continue; }
					const FAsdaItemRecord* Rec = IT->GetItem(It->ItemCode);
					if (Rec && Rec->ItemSubType == 105) { FoundContainer = It->ContainerType; FoundCell = It->SlotIndex; break; }
				}
			}
			if (FoundCell >= 0)
			{
				if (!bConfirmOpen)
				{
					UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
					const FString Text = Loc(PetCageLoc::kMsg56Text);
					if (WM && !Text.IsEmpty())
					{
						bConfirmOpen = true;
						AsdaEnterGuard::NoEnter(WM)->ShowGameConfirm(Text, [this, FoundContainer, FoundCell](bool bYes)
						{
							bConfirmOpen = false;
							if (!bYes) { return; }
							// ★ ЭТО ВТОРОЙ ПУТЬ 6108 эталона (`useEnhanceSlot == 1`,
							//   network.c:25735-25752): он требует ВЫБРАННОГО питомца, а не
							//   призванного, и номер берёт у него сам. Поэтому `bFromWindowSlot`
							//   = true, а `petUid` наружу не передаётся (гейт волны 1, ревизия
							//   2026-09-21). Гейт «нужен призванный» + нотис 101105 относятся
							//   ТОЛЬКО к первому пути — применению предмета из сумки (волна 6).
							if (UAsdaPetSubsystem* P = Pets())
							{
								(void)P->RequestFeed(FoundContainer, FoundCell, /*bFromWindowSlot*/ true);
							}
						}, 0.f, PetCageLoc::kMsg5657Ok, PetCageLoc::kMsg5657No, /*очередь: эталон (1, 56) item_enhance.c:3905*/56, 1);
					}
				}
			}
			else
			{
				NoticeMsgBox(PetCageLoc::kNeedHealItem);   // 101104
			}
		}
	}
	HideActionPanel();
}

// 43066 «Отменить призыв» — item_enhance.c:3921-3938.
void UAsdaPetCageSubsystem::OnPanelRecall()
{
	const FAsdaPetSlot* Sel = Selected();
	if (Sel)
	{
		if (Sel->bIsActive)
		{
			if (UAsdaPetSubsystem* P = Pets()) { P->RequestCallPet(Sel->UniqueId); }
		}
		else
		{
			NoticeMsgBox(PetCageLoc::kNotSummoned);   // 101113 «Ты вызываешь не питомца»
		}
	}
	HideActionPanel();
}

// 43055 → окно 44200 «Рост питомца». Гейты 109975 (призван) и 109946 (уровень ≥ 10) принадлежат
//   САМОМУ окну 44200 (`uiUpgradeDialog_Open@0xEE18E0`, ui_core.c:90605-90650), а не этой кнопке:
//   оригинал зовёт открытие БЕЗУСЛОВНО и передаёт ему ВЫБРАННЫЙ слот
//   (`uiUpgradeDialog_Open(g_pUpgradeDialog, wnd[154])`, item_enhance.c:3934).
//   ★ ВОЛНА 4 (21.09.2026): проводка доделана — раньше здесь была только запись в лог.
//   ★ Панель 43058 прячется ДО открытия, как и у всех прочих кнопок панели (`sub_93DB58`).
void UAsdaPetCageSubsystem::OnPanelGrowth()
{
	const FAsdaPetSlot* Sel = Selected();
	const int32 Uid = Sel ? Sel->UniqueId : -1;
	HideActionPanel();
	UAsdaPetGrowthSubsystem* G = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaPetGrowthSubsystem>() : nullptr;
	const bool bOpened = G ? G->OpenGrowth(Uid) : false;
	UE_LOG(LogAsdaPetCage, Log,
		TEXT("[питомец-окно] 43055 «Рост питомца» → окно 44200: выбран pet=%d, уровень=%d, призван=%s → %s"),
		Uid, Sel ? Sel->Level : -1, (Active() != nullptr) ? TEXT("да") : TEXT("нет"),
		bOpened ? TEXT("открыто") : (G ? TEXT("гейт/отказ") : TEXT("подсистемы 44200 нет")));
}

// 43056 → окно 43500 «Развитие питомца». Гейт 109974 (питомец призван) принадлежит САМОМУ окну
//   43500 (`uiPetDialog_Open@0xEDD450`, ui_core.c:89960-89980), а не этой кнопке: оригинал зовёт
//   открытие БЕЗУСЛОВНО и передаёт ему ВЫБРАННЫЙ слот
//   (`uiPetDialog_Open(g_PetUIDialog, wnd[154])`, item_enhance.c:3939).
//   ★ ВОЛНА 5 (21.09.2026): проводка доделана — раньше здесь была только запись в лог.
//   ★ Панель 43058 прячется ДО открытия, как и у всех прочих кнопок панели (`sub_93DB58`).
void UAsdaPetCageSubsystem::OnPanelEvolution()
{
	const FAsdaPetSlot* Sel = Selected();
	const int32 Uid = Sel ? Sel->UniqueId : -1;
	HideActionPanel();
	UAsdaPetEvolutionSubsystem* E = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaPetEvolutionSubsystem>() : nullptr;
	const bool bOpened = E ? E->OpenEvolution(Uid) : false;
	UE_LOG(LogAsdaPetCage, Log,
		TEXT("[питомец-окно] 43056 «Развитие питомца» → окно 43500: выбран pet=%d, призван=%s → %s"),
		Uid, (Active() != nullptr) ? TEXT("да") : TEXT("нет"),
		bOpened ? TEXT("открыто") : (E ? TEXT("гейт/отказ") : TEXT("подсистемы 43500 нет")));
}

void UAsdaPetCageSubsystem::OnPanelCancel()
{
	HideActionPanel();
}

// ★★★ ШЕСТАЯ КНОПКА «О питомце» (наш 43067) — ДОБАВКА ПОРТА, требование владельца 25.09.2026.
//   ЭТАЛОННОГО ПРООБРАЗА НЕТ ВООБЩЕ: `upgradeWndProc@0xDC3690` такой ветки не разбирает
//   (доказательство «чего в оригинале нет» — у `BuildPanelInfoButton`).
//   ПОВЕДЕНИЕ ВЫБРАНО ПО ОБРАЗЦУ СОСЕДЕЙ, а не выдумано: `OnPanelGrowth`/`OnPanelEvolution`
//   тоже сперва прячут панель, а потом открывают своё окно — панель это ВСПЛЫВАЮЩЕЕ меню,
//   и остаться на экране после выбора пункта она не должна (`sub_93DB58` у всех пунктов эталона).
//   ⚠ Клетку 43000 здесь НЕ ЗАКРЫВАЕМ и `CloseCageForChildWindow` не зовём: у 43500/44200 это
//   порт ЭТАЛОННОГО закрытия (`ui_core.c:89976`, `:90627`), а у нашего окна закрывать нечего —
//   эталон его вообще не знает, и «О питомце» задумано как СПУТНИК списка, а не замена ему.
// ★★★ ОДНА ТОЧКА «окно О питомце, следуй за выбором» (требование владельца 25.09.2026).
//   Зовётся из ТРЁХ мест, где выбор может смениться: ЛКМ по слоту, ПКМ по слоту и пачка
//   серверных событий (там питомца могли выбросить). Отдельная функция, а не три копии
//   обращения к подсистеме, — иначе четвёртое место забудут (закон `no-scope-cutting`).
//   ⚠ Ничего не решает сама: «закреплено ли окно» и «жив ли питомец» знает только
//   `UAsdaPetEvoTipSubsystem::RetargetIfPinned`, там же и все края.
void UAsdaPetCageSubsystem::FollowSelectionInInfoWindow()
{
	UAsdaPetSubsystem* P = Pets();
	if (UAsdaPetEvoTipSubsystem* Tip = GetGameInstance()
			? GetGameInstance()->GetSubsystem<UAsdaPetEvoTipSubsystem>() : nullptr)
	{
		Tip->RetargetIfPinned(P ? P->GetSelectedUniqueId() : -1);
	}
}

void UAsdaPetCageSubsystem::OnPanelInfo()
{
	HideActionPanel();

	UAsdaPetSubsystem* P = Pets();
	const int32 Uid = P ? P->GetSelectedUniqueId() : -1;
	if (Uid < 0)
	{
		// Панель поднимается только правым кликом по ЗАНЯТОМУ слоту (`OnSlotRightClicked`),
		//   поэтому выбранный питомец здесь есть всегда. Ветка — страховка от консольного вызова.
		UE_LOG(LogAsdaPetCage, Warning,
			TEXT("[питомец-окно] «О питомце»: выбранного питомца нет — открывать нечего"));
		return;
	}
	if (UAsdaPetEvoTipSubsystem* Tip = GetGameInstance()
			? GetGameInstance()->GetSubsystem<UAsdaPetEvoTipSubsystem>() : nullptr)
	{
		Tip->OpenInfoWindow(Uid);
	}
}
