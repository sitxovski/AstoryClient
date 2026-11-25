// Copyright (c) 2026 @sitxovski (Discord)
// Реализация окна «Поиск отряда» (`Find_Party_WIn`, JUI 8500). Весь разбор эталона, адреса,
//   таблица нотисов, раскладки пакетов и дефекты — в шапке `AsdaPartyFindSubsystem.h` и в
//   отчётах `Reports/extra_menu_partyfind.md`, `Reports/partyfind_3421_layout.md`,
//   `Reports/party_find_8500.md`.

#include "UI/AsdaPartyFindSubsystem.h"
#include "AsdaPerf.h"   // ★ R4-c (14.11.2026): области замера времени игрового потока
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "AsdaSelf.h"   // ★ В2-A дедупа №2: общая точка «это я?» (класс/уровень/charId/userNum)
#include "UI/AsdaJuiWindowHandle.h"   // ★ волна 3 (15.09.2026): каркас окон JUI
#include "UI/AsdaNotice.h"   // ★ волна 1: фасад нотисов (Loc / Chat / MsgBox)

#include "AsdaBytes.h"
#include "AsdaNetSubsystem.h"
#include "Login/AsdaButtonRelay.h"
#include "Login/AsdaLoginData.h"          // FAsdaLoginData::DecodeCp1251 — имена в записях страниц
#include "UI/AsdaJuiComboBox.h"
#include "UI/AsdaMessageSubsystem.h"
#include "UI/AsdaMiniStateSubsystem.h"
#include "UI/AsdaPartySubsystem.h"
#include "UI/AsdaChatWindowSubsystem.h"
#include "UI/AsdaTipPanel.h"
#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaUiSlotDrift.h"   // ★ 25.10.2026: ключ `asda.UI.BaseShiftKeepsDrag` (рамки выбора не уезжают после перетаскивания)
#include "World/AsdaWorldSubsystem.h"
#include "World/AsdaMapRegistry.h"   // ★ волна 2 (14.09.2026): реестр карт — генерируемый заголовок из Tools/data/asda_map_registry.json (кнопки карт, перекодировка)

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/CheckBox.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/TextBlock.h"
#include "Engine/GameInstance.h"
#include "Engine/Engine.h"          // GEngine->DeferredCommands — выход после самотеста
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"           // шаги самотеста разнесены таймерами (нужна отрисовка)
#include "UnrealClient.h"           // FScreenshotRequest — ИМЕНОВАННЫЙ кадр вместе со слоем Slate
#include "AsdaOutgoingGate.h"               // ★ С-Р1: общий офлайн-гейт отладочных команд (RefuseDebugWhenOnline)
#include "Test/AsdaSelfTestKit.h"   // ★ 11.10.2026 (В3 S1): общий каркас самотестов (аргументы, отказ, перехват, итог, выход)
#include "UI/AsdaUiHelpers.h"   // ★ 31.10.2026: AsdaUi::AnchorRelay — общий GC-якорь реле

DEFINE_LOG_CATEGORY_STATIC(LogAsdaPartyFind, Log, All);

// ★ ДЕФЕКТ ОРИГИНАЛА Д2 (клиент): в `sub_DB8AA0`, ветка события 0x10601, верхняя граница
//   диапазона карт пишется дважды подряд — `0xDB8E00: mov [ebp-0x10], 0x21F0` (8688) и сразу
//   `0xDB8E07: mov [ebp-0x10], 0x21EF` (8687), между ними НЕТ ни одной инструкции. Поэтому клик
//   по последней карте `Find_Party_MapE04` (8688, «Логово Дракона») в оригинале НЕ РАБОТАЕТ:
//   таблица переходов `0xDB9220` его обрабатывает, а проверка диапазона отбрасывает.
//   1 = вести себя ровно как оригинал (8688 не кликается); 0 (по умолчанию) = все 16 карт живые.
// ★ ДЕФЕКТ ОРИГИНАЛА Д3 (клиент): маска фильтра карт только НАБИРАЕТ биты. Эталонный
//   `flags64_SetBit@0xDBB080` делает ровно `mask |= 1 << (code % 64)` — снимающей ветки в
//   подсистеме НЕТ ВОВСЕ (тела `0xDB6120…0xDBF3E0` просмотрены целиком, обнуление маски есть
//   только в `uiFindPartyWindowClose@0xDB9ED0` при полном закрытии окна и в кнопке 8502
//   «По всему региону», которая маску не трогает, а лишь взводит признак `this+0x1644`).
//   У нас кнопки карт — `UCheckBox` (так их сделал импортёр, и это верно: выбор множественный),
//   поэтому «снял галку, а карта всё ещё в фильтре» игрок ВИДИТ. Исправляем: снятие галки
//   снимает и бит. 1 = эталонное поведение (бит остаётся навсегда).
static ASDA_DEV_CVAR(int32, CVarPartyFindLegacyMaskNeverClears,
	TEXT("asda.PartyFind.LegacyMaskNeverClears"), 0,
	TEXT("1 = снятая галка карты НЕ убирает бит из маски фильтра (дефект оригинала); 0 = убирает."),
	ECVF_Default);

static ASDA_DEV_CVAR(int32, CVarPartyFindLegacyMapRange,
	TEXT("asda.PartyFind.LegacyMapRange"), 0,
	TEXT("1 = клик по карте 8688 «Логово Дракона» игнорируется (дефект оригинала); 0 = работает."),
	ECVF_Default);

// ★★★ ДЕФЕКТ ОРИГИНАЛА Д7 (2026-08-30, жалоба владельца «список локаций пиздец тёмный»).
//   Плитка карт в оригинале нечитаема, и это ДАННЫЕ, а не наша ошибка. Подложка плитки — тинт
//   `Diffuse` состояния SS0 скина (`RE/docs/JuiSkin.ini`): `MapName_Normal` = FF423406,
//   `MapName_InDun` = FF3A1105, `MapName_Elite` = FF0F2B09 — почти чёрные. Цвет ПОДПИСИ идёт не
//   из скина, а из поля `CaptionInfo` самого контрола в `gamemain_jui.ini`: у обычных карт
//   ff6d6341, у подземелий ff69453d, у элитных ff3f5e35 — тоже почти чёрные.
//   Контраст (WCAG, относительная яркость): 2.01 / 1.98 / 2.09 при норме 4.5 для мелкого текста
//   ⇒ подпись физически не читается — ровно то, на что жалуется владелец.
//   ЧИНИМ ПО ПРАВИЛУ `fix-original-defects`, но НИЧЕГО НЕ ВЫДУМЫВАЯ: подписи красим в цвет,
//   который ТОТ ЖЕ САМЫЙ скин уже задаёт этой же кнопке для состояния наведения SS1 —
//   FFE4C67C (обычные), FFF08D68 (подземелья), FFAFE9A9 (элитные). Контраст становится
//   7.35 / 6.88 / 10.96. Подложку НЕ трогаем: её осветление, наоборот, СНИЖАЕТ контраст со
//   светлой подписью, а тёмная плитка — фирменный вид оригинала.
//   ★ Сам цвет пишется В АССЕТ идемпотентным шагом пост-импорта
//     `Tools/asda_ui_partyfind_map_captions.py` (переживает переимпорт `.JUI`).
//     Этот ключ — ОТКАТ: 1 = вернуть эталонные тёмные цвета подписей в рантайме.
static ASDA_DEV_CVAR(int32, CVarPartyFindLegacyMapCaptions,
	TEXT("asda.PartyFind.LegacyMapCaptions"), 0,
	TEXT("1 = вернуть ЭТАЛОННОЕ оформление подписей карт: тёмный цвет и отсутствие обрезки ")
	TEXT("(размер 8 — тот же, что и без ключа); 0 = читаемо, обрезка по плитке."),
	ECVF_Default);

// ★ Оригинал при наведении на КАРТУ не выставляет флаг «подсказка показана» (`this+0x1648`):
//   ветка `jle 0xDB9135` ставит флаг только когда код карты <= 0, то есть для четырёх кнопок.
//   Практических последствий нет (флаг читает только закрытие окна, а плашку и так гасит
//   событие 0xA), поэтому ключа отката не заводим — фиксируем наблюдение здесь.

namespace
{
	// ── Окно ──
	const FName  kWindowId(TEXT("Find_Party_WIn"));
	const TCHAR* kWindowWbp =
		TEXT("/Game/AsdaAssets/UI/Player_UI/Social/WBP_Find_Party_WIn.WBP_Find_Party_WIn_C");
	// Слой окна: та же полоса, что у доп-меню (700) и элитного ящика (730) — обычные окна игрока.
	constexpr int32 kWinZ = 720;

	// ── IDNum контролов (адресуемся ТОЛЬКО по номеру: имена `.JUI` врут, закон
	//    `jui-importer-widget-naming`; idc нужен лишь чтобы найти узел в дереве ассета) ──
	constexpr int32 kIdWin        = 8500;
	constexpr int32 kIdMove       = 8501;   // полоса-ручка + заголовок (текст 11800)
	constexpr int32 kIdMapAll     = 8502;   // «По всему региону» (текст 11904)
	constexpr int32 kIdMTitle     = 8504;   // 11821
	constexpr int32 kIdMJobTxt    = 8505;   // 11822 «Профессия»
	constexpr int32 kIdMJobSel    = 8506;
	constexpr int32 kIdMLvTxt     = 8507;   // 11823 «Уровень»
	constexpr int32 kIdMLvSel     = 8508;
	constexpr int32 kIdMFind      = 8509;   // 11824 «Поиск»
	constexpr int32 kIdMSel       = 8570;
	constexpr int32 kIdMTxt1      = 8571;
	constexpr int32 kIdMTxt2      = 8572;
	constexpr int32 kIdMNext      = 8573;
	constexpr int32 kIdMEnd       = 8574;
	constexpr int32 kIdMPre       = 8575;
	constexpr int32 kIdMFirst     = 8576;
	constexpr int32 kIdPTitle     = 8578;   // 11825
	constexpr int32 kIdPChTxt     = 8579;   // 11826 «Канал»
	constexpr int32 kIdPChSel     = 8580;
	constexpr int32 kIdPDesTxt    = 8581;   // 11827 «Цель»
	constexpr int32 kIdPDesSel    = 8582;
	constexpr int32 kIdPFind      = 8583;   // 11824 «Поиск»
	constexpr int32 kIdPSel       = 8644;
	constexpr int32 kIdPTxt1      = 8645;
	constexpr int32 kIdPTxt2      = 8646;
	constexpr int32 kIdPNext      = 8647;
	constexpr int32 kIdPEnd       = 8648;
	constexpr int32 kIdPPre       = 8649;
	constexpr int32 kIdPFirst     = 8650;
	constexpr int32 kIdEditBtn    = 8651;   // 11828 «Изменить»
	constexpr int32 kIdMini2      = 8652;   // 11831 «Свернуть»
	constexpr int32 kIdClose      = 8653;   // 11832 «Отмена»
	constexpr int32 kIdRefresh    = 8654;   // 11829 «Обновить»
	constexpr int32 kIdWhisper    = 8655;   // 11830 «Шепот»
	constexpr int32 kIdEditBox    = 8656;
	constexpr int32 kIdEditTitle  = 8657;   // 11834
	constexpr int32 kIdEditNumTxt = 8658;   // 11833 «Количество членов»
	constexpr int32 kIdEditNum    = 8659;
	constexpr int32 kIdEditDesTxt = 8660;   // 11827 «Цель»
	constexpr int32 kIdEditDes    = 8661;
	constexpr int32 kIdEditApply  = 8662;   // 11828 «Изменить»
	constexpr int32 kIdEditCancel = 8663;   // 2289  «Отмена»
	constexpr int32 kIdMapSel     = 8694;   // зелёная рамка выбранной карты
	constexpr int32 kIdMiniBtn    = 8698;   // крестик-«свернуть» в шапке
	constexpr int32 kIdMapFirst   = 8664;   // начало диапазона карт (эталонный `sub ecx, 0x21D8`)
	constexpr int32 kIdMapLast    = 8688;   // конец диапазона карт (в оригинале ошибочно 8687 — Д2)

	// ── Тексты ──
	namespace PfLoc
	{
		constexpr int32 kWinTitle     = 11800;
		constexpr int32 kMapNameBase  = 11801;   // 11801 «Все» + 11802…11817 — 16 карт
		constexpr int32 kMTitle       = 11821;
		constexpr int32 kColJob       = 11822;
		constexpr int32 kColLevel     = 11823;
		constexpr int32 kBtnFind      = 11824;
		constexpr int32 kPTitle       = 11825;
		// ★★★ НАШИ строки (> 990000): подсказка при ПЕРВОМ за сессию открытии окна.
		//   В оригинале такого сообщения НЕТ — проверено двумя методами (см. шапку .h).
		//   Две строки, а не одна: факт для одиночки и для главы отряда РАЗНЫЙ — одиночка
		//   попадает в список ЖЕЛАЮЩИХ (3410), а глава публикует ОТРЯД (3418) в список
		//   набирающих. Общая формулировка была бы неточной для обоих.
		constexpr int32 kFirstOpenSingle = 990120;
		constexpr int32 kFirstOpenLeader = 990121;
		constexpr int32 kColChannel   = 11826;
		constexpr int32 kColGoal      = 11827;
		constexpr int32 kBtnEdit      = 11828;
		constexpr int32 kBtnRefresh   = 11829;
		constexpr int32 kBtnWhisper   = 11830;
		constexpr int32 kBtnMinimize  = 11831;
		constexpr int32 kBtnCancel    = 11832;
		constexpr int32 kEditNumTxt   = 11833;
		constexpr int32 kEditTitle    = 11834;
		constexpr int32 kJobFirst     = 11835;   // 5 пунктов комбо профессии
		constexpr int32 kLevelFirst   = 11840;   // 11 пунктов комбо уровня
		constexpr int32 kChanFirst    = 11860;   // 13 пунктов комбо канала
		constexpr int32 kGoalEditFirst= 11876;   // 12 пунктов комбо цели панели правки
		constexpr int32 kNumFirst     = 11900;   // 4 пункта комбо числа членов
		constexpr int32 kMapAll       = 11904;
		constexpr int32 kJoinList     = 11905;
		constexpr int32 kNoticeMinim  = 11906;
		constexpr int32 kNoticeCancel = 11907;
		constexpr int32 kNoticeEdited = 11908;
		constexpr int32 kTipMapTitle  = 11909;   // тема 31: заголовок подсказки карты
		constexpr int32 kTipMapTail   = 11910;   // тема 31: хвост тела («Нажми для продолжения»)
		constexpr int32 kBtnCancel2289= 2289;    // подпись 8663
		constexpr int32 kLevelWord    = 12215;   // «Ур.»
		constexpr int32 kClassNameBase= 12300;   // 12300 + classId = имя класса
	}

	// Пары «тема подсказки → (заголовок, тело)» из `UI_BuildHelpTooltipText@0xEB11A0`
	//   (`AsdaStory_game.c:501226…`). Темы 32/33/34/35 — ровно эти четыре.
	struct FPfTipTopic { int32 Title; int32 Body; };
	FPfTipTopic PfTipForButton(int32 IdNum)
	{
		// Байт-таблица `0xDB92D8` с базой 8652 → таблица переходов `0xDB9284`:
		//   8652 и 8698 → 0xDB8FB2 (тема 32) · 8653 → 0xDB8FC5 (33)
		//   8654 → 0xDB8FEB (35, а НЕ 34!)   · 8662 → 0xDB8FD8 (34)
		switch (IdNum)
		{
		case kIdMini2:
		case kIdMiniBtn:    return { 11911, 11912 };   // тема 32
		case kIdClose:      return { 11913, 11914 };   // тема 33
		case kIdRefresh:    return { 11917, 11918 };   // тема 35
		case kIdEditApply:  return { 11915, 11916 };   // тема 34
		default:            return { 0, 0 };
		}
	}

	// ── Карты-фильтры: IDNum → UI-индекс 1…16 (он же смещение текста от 11801).
	//    Снято из веток обработчика события 0x10601 (`0xDB8B9E…0xDB8DFF`): каждая ветка кладёт
	//    в `[ebp-0xC]` свой индекс, и он же используется как `11801 + idx` в нотисе выбора. ──
	//    ★ 14.09.2026 (реестр карт, волна 2): таблицы — в генерируемом `World/AsdaMapRegistry.h` из
	//    `Tools/data/asda_map_registry.json` (значения прежние). Проволочный код карты = id карты: подписи
	//    11802…11817 по UI-индексу совпадают с именами карт remapIndexForward (1→3 «Альпия», 4→0 «Силарис», …).
	using FPfMap = AsdaMapRegistry::FPartyFindButton;
	const auto& kMaps = AsdaMapRegistry::kPartyFindButtons;
	constexpr int32 kMapCount = UE_ARRAY_COUNT(AsdaMapRegistry::kPartyFindButtons);

	// Порт `remapIndexForward@0xDBA900`: UI-индекс комбо/кнопки карты → «проволочный» код сервера (= id карты).
	int32 PfRemapForward(int32 UiIdx)
	{
		const int32 N = UE_ARRAY_COUNT(AsdaMapRegistry::kPartyFindUiToMap);
		return (UiIdx >= 0 && UiIdx < N) ? AsdaMapRegistry::kPartyFindUiToMap[UiIdx] : -1;
	}
	// Порт `remapIndexReverse@0xDBAA50`: «проволочный» код сервера → UI-индекс.
	int32 PfRemapReverse(int32 Wire)
	{
		const int32 N = UE_ARRAY_COUNT(AsdaMapRegistry::kPartyFindMapToUi);
		return (Wire >= -1 && Wire + 1 < N) ? AsdaMapRegistry::kPartyFindMapToUi[Wire + 1] : -1;
	}

	// Собрать ВСЕ планы одного контрола (`<idc>_<juiIdx>_<plane>`). Нужно рамкам выбора:
	//   у `Find_Party_MSel` / `PSel` / `MapSel` в ассете НЕТ единого узла — только 8 картинок-планов
	//   скина `Box_Green3Strok`, и двигать их надо ВМЕСТЕ (закон `jui-importer-widget-naming`).
	void PfCollectPlanes(const UUserWidget* Host, const FString& Idc, TArray<TObjectPtr<UWidget>>& Out)
	{
		Out.Reset();
		if (!Host || !Host->WidgetTree) { return; }
		Host->WidgetTree->ForEachWidget([&Out, &Idc](UWidget* W)
		{
			if (!W) { return; }
			FString NodeIdc; int32 JuiIdx = 0, Plane = 0;
			if (!AsdaUi::ParsePlaneName(W->GetName(), NodeIdc, JuiIdx, Plane)) { return; }
			if (NodeIdc == Idc) { Out.Add(W); }
		});
	}

	// ★★★ СМЕЩЕНИЕ УЗЛА В КООРДИНАТАХ ОКНА (сумма отступов всех канвас-слотов до корня).
	//   ЗАЧЕМ (дефект ПОРТА, жалоба владельца 2026-08-30 «у ряда локаций выделение не на элементе,
	//   а в верхнем-левом углу»). Иерархия импортированного окна НЕОДНОРОДНА (закон
	//   `jui-uneven-node-layout`): 9 из 16 кнопок карт лежат ПРЯМО в канвасе `Find_Party_MapAll_grp`
	//   и несут абсолютные отступы (29,1), (178,1), …, а 7 штук импортёр завернул в собственный
	//   под-канвас `Find_Party_Map<XX>_grp`, и отступ самой кнопки внутри него — РОВНО (0,0):
	//     `Find_Party_MapN06_grp off=(178,19)` → `Find_Party_MapN06 off=(0,0)`;
	//     то же у `MapN07/N08/N09` (пустые слоты), `MapD04` (пустой) и у ЖИВЫХ `MapE03`, `MapE04`.
	//   Старый код брал `SlotOf(node)->GetOffsets()` напрямую, поэтому для завёрнутых карт дельта
	//   выходила (0−29, 0−1) = (−29,−1), и зелёная рамка уезжала в левый-верхний угол плитки.
	//   ⇒ ломалось ровно у трёх кликабельных карт: 8670 «Флавис», 8687 «Застывшая пустыня»,
	//     8688 «Логово дракона» (у остальных завёрнутых номера нет — это пустые слоты разметки).
	//   Считаем координату ОДИНАКОВО для базы и цели — суммой по цепочке родителей до корня;
	//   тогда разность верна независимо от того, на какой глубине лежит узел.
	// ★ sRGB→LINEAR из 0xAARRGGBB (закон `ui-srgb-gamma-rule`) — общая точка `AsdaUi::SrgbU32ToLinear`.
	//   Прежние локальные `PfSrgbToLinear`/`PfColorFromArgb` были её побайтовой копией; сведены
	//   (аудит дублей кривой sRGB, `fix_helpers_dedup.md`).

	FVector2D PfWindowOffsetOf(const UWidget* W)
	{
		FVector2D Acc(0.0, 0.0);
		for (const UWidget* Cur = W; Cur; Cur = Cur->GetParent())
		{
			if (const UCanvasPanelSlot* CS = Cast<UCanvasPanelSlot>(Cur->Slot))
			{
				const FMargin M = CS->GetOffsets();
				Acc.X += M.Left;
				Acc.Y += M.Top;
			}
		}
		return Acc;
	}

	// Имя виджета: точное, затем — по префиксу без хвоста импортёра `_<число>_<число>`
	//   (закон `jui-importer-widget-naming`; образец — `NodeLoose` журнала заданий).
	UWidget* PfNode(const UUserWidget* Host, const FString& Idc, const TCHAR* What)
	{
		if (!Host || !Host->WidgetTree) { return nullptr; }
		if (UWidget* W = AsdaUi::FindNodeExact(Host, *Idc)) { return W; }
		UWidget* Found = nullptr;
		Host->WidgetTree->ForEachWidget([&Found, &Idc](UWidget* W)
		{
			if (Found || !W) { return; }
			if (W->GetName().StartsWith(Idc, ESearchCase::IgnoreCase)) { Found = W; }
		});
		if (!Found && What)
		{
			UE_LOG(LogAsdaPartyFind, Warning,
				TEXT("[поиск отряда] узел не найден: %s (искали «%s», в т. ч. по префиксу)"), What, *Idc);
		}
		return Found;
	}
}

// ═══════════════════════════════ ЖИЗНЕННЫЙ ЦИКЛ ═══════════════════════════════════════════════

void UAsdaPartyFindSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	// ★★★ РОСПИСЬ «КАК ЗАКРЫТЬ СЕБЯ ПО ESC» — порт события 21 (`CControl_OnEvent21@0x12DCC30`).
	//   Окно `Find_Party_WIn` (IDNum 8500) стоит в эталонной таблице `kEscCloseWindows`: признак
	//   `CControl+0x228` ему ставит `FindPartyWindow_InitControls@0xDB68C0`. Проход
	//   `UAsdaWindowSubsystem::CloseTopmostOnEsc` закрывает верхнее окно ИМЕННО ЭТИМ вызовом,
	//   а не снятием виджета: снятие оставило бы подсистеме состояние «открыто», и окно
	//   больше не открылось бы. Закрыватель идемпотентен — повторный вызов ничего не делает.
	Collection.InitializeDependency(UAsdaWindowSubsystem::StaticClass());
	if (UAsdaWindowSubsystem* EscWM = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		EscWM->RegisterEscCloser(kWindowId, FSimpleDelegate::CreateUObject(this, &UAsdaPartyFindSubsystem::CloseWindow));
	}
	Singles.SetNum(10);
	Parties.SetNum(10);
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UAsdaNetSubsystem* Net = GI->GetSubsystem<UAsdaNetSubsystem>())
		{
			Net->OnPartyFindAck.AddUObject(this, &UAsdaPartyFindSubsystem::OnAck);
			Net->OnPartyFindPage.AddUObject(this, &UAsdaPartyFindSubsystem::OnPage);
		}
	}
	UE_LOG(LogAsdaPartyFind, Log, TEXT("[поиск отряда] подсистема поднята (окно 8500)"));
}

void UAsdaPartyFindSubsystem::Deinitialize()
{
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UAsdaNetSubsystem* Net = GI->GetSubsystem<UAsdaNetSubsystem>())
		{
			Net->OnPartyFindAck.RemoveAll(this);
			Net->OnPartyFindPage.RemoveAll(this);
		}
	}
	Super::Deinitialize();
}

// ★★★ К-3: СБРОС СОСТОЯНИЯ СЕССИИ (см. шапку AsdaSessionReset.h — у оригинала этой задачи нет,
//   его разрыв связи завершает процесс). Здесь опаснее всего ДВА поля и ОДИН таймер:
//   `MemberRegId`/`LeaderRegId` — номера записей, которые сервер выдал НАШЕЙ сессии
//   (`:1235`/`:1245`), и `bMinimizedTimer`, который раз в 800 мс шлёт 3414/3422.
//   ⚠ `CloseWindow()` здесь НЕ зовём: он отправляет 3412/3420 — то есть лезет в мёртвый (или уже
//     ЧУЖОЙ) сокет. Закрываем окно и чистим поля локально.
void UAsdaPartyFindSubsystem::ResetSessionState(EAsdaSessionResetReason Reason)
{
	// Смена карты соединение пересоздаёт (`UAsdaNetSubsystem::EnterGameWorld`), поэтому защёлки
	// и периодические запросы снимаем в любом случае; номера записей сервер выдаёт заново тоже.
	if (bOpen || bMinimizedTimer || MemberRegId >= 0 || LeaderRegId >= 0)
	{
		UE_LOG(LogAsdaPartyFind, Log,
			TEXT("[поиск отряда] сброс сессии (%s): окно=%d таймер=%d MemberRegId=%d LeaderRegId=%d"),
			AsdaSessionResetReasonName(Reason), bOpen ? 1 : 0, bMinimizedTimer ? 1 : 0, MemberRegId, LeaderRegId);
	}

	if (UGameInstance* GI = GetGameInstance())
	{
		if (UAsdaWindowSubsystem* WM = GI->GetSubsystem<UAsdaWindowSubsystem>())
		{
			WM->SetWindowVisible(kWindowId, false);
		}
		if (bTipShown)
		{
			if (UAsdaMiniStateSubsystem* Mini = GI->GetSubsystem<UAsdaMiniStateSubsystem>())
			{
				Mini->HideSharedTip(EAsdaTipOwner::PartyFind);
			}
		}
	}
	ClearTables();
	bTipShown = false;
	bOpen = false;
	bClosedFully = true;

	// Номера записей в списках одиночек/отрядов — чисто сессионные.
	MemberRegId = -1;
	LeaderRegId = -1;

	// Таймер свёрнутой панели: без сброса он продолжит слать 3414/3422 после разрыва.
	bMinimizedTimer = false;
	MinimizedAccum  = 0.f;

	// Защёлка «запрос в полёте» и страницы обоих списков.
	bReqInFlight = false; ReqAccum = 0.f; ReqOpcode = 0;
	MapMaskA = 0; MapMaskB = 0;
	MSelIdx = -1; MCurPage = 1; MReqPage = 1; MTotalPages = 1;
	PSelIdx = -1; PCurPage = 1; PReqPage = 1; PTotalPages = 1;
	HoverRowM = -1; HoverRowP = -1;
}

void UAsdaPartyFindSubsystem::Tick(float DeltaTime)
{
	ASDA_PERF_SCOPE("Tick.PartyFind");   // ★ R4-c (14.11.2026): область замера (AsdaPerf.h; в Shipping пусто)
	// ── Защёлка «запрос в полёте»: снимается ответом или ТАЙМАУТОМ; отказ пишет причину в журнал.
	if (bReqInFlight)
	{
		ReqAccum += DeltaTime;
		if (ReqAccum > 5.f)
		{
			UE_LOG(LogAsdaPartyFind, Warning,
				TEXT("[поиск отряда] ответ на опкод %d не пришёл за 5 с — защёлка снята по таймауту"),
				ReqOpcode);
			bReqInFlight = false; ReqAccum = 0.f; ReqOpcode = 0;
		}
	}

	// ── Таймер 800 мс СВЁРНУТОГО окна (эталон `SetTimer(hwnd, 26, 800)` в `uiWindowCloseStartTimer`).
	//    Ветка `WM_TIMER` id 26 в оригинале ВЫРЕЗАНА (`MainWndProc@0x9841A0` знает 5…9, 11…24, 99),
	//    поэтому что именно она делала — не наблюдаемо. Мы держим регистрацию живой минимальным
	//    действием: перезапрашиваем обе страницы, как это делает кнопка «Обновить».
	if (bMinimizedTimer && !bOpen)
	{
		MinimizedAccum += DeltaTime;
		if (MinimizedAccum >= 0.8f)
		{
			MinimizedAccum = 0.f;
			SendGetMember();
			SendGetLeader();
		}
	}

	if (!bOpen || !bReady) { return; }
	Mouse.Update();
	// ★★★ МОДАЛЬНЫЙ ГЕЙТ (П8-9, волна 9): под открытым модальным нотисом эталон не отдаёт мышь
	//   НИКОМУ ниже (`ui_core.c:104149-104153`). Обоснование, порядок и ключ отката
	//   `asda.UI.ModalGatePolling` — в шапке `AsdaUi::IsModalDispatchBlocked` (`AsdaUiHelpers.h`).
	//   ⚠ Защёлки сбрасываем: иначе нажатие ДО нотиса и отпускание ПОСЛЕ его закрытия сошлись бы
	//   в клик, которого игрок не делал. Своей защёлки нажатия у окна нет (строка выбирается прямо
	//   на фронте `bLmbDown`), поэтому сбрасывать нужно только кадр мыши.
	if (AsdaUi::IsModalDispatchBlocked(GetGameInstance()))
	{
		Mouse.Reset();
		return;
	}
	if (CmbJob)     { CmbJob->Tick(Mouse); }
	if (CmbLevel)   { CmbLevel->Tick(Mouse); }
	if (CmbChan)    { CmbChan->Tick(Mouse); }
	if (CmbGoal)    { CmbGoal->Tick(Mouse); }
	if (CmbEditNum) { CmbEditNum->Tick(Mouse); }
	if (CmbEditDes) { CmbEditDes->Tick(Mouse); }
	TickRowHover();
}

// ═══════════════════════════════ ОКНО ═════════════════════════════════════════════════════════

bool UAsdaPartyFindSubsystem::EnsureWindow()
{
	// ★ Каркас окна — `FAsdaJuiWindowHandle::Ensure` (волна 3 аудита дублей): гвард `IsWindowLive`,
	//   сброс протухшего окна, открытие WBP, привязка, скрытие — в одной точке, в прежнем порядке.
	const EAsdaJuiEnsure R = FAsdaJuiWindowHandle(kWindowId, kWindowWbp, kWinZ).Ensure(GetGameInstance(), Win, bReady,
		[this](UAsdaWindowSubsystem&) { ResetWidget(); },
		[this](UAsdaWindowSubsystem&)
		{
			SetupControls();
			RegisterZone();
		},
		[]()
		{
			// Эталон в этом месте показывает `MessageBoxA(nullptr, "Point NULL", "Find_Party_WIn", 0)`.
			UE_LOG(LogAsdaPartyFind, Error,
				TEXT("[поиск отряда] WBP_Find_Party_WIn не загрузился (%s)"), kWindowWbp);
		});
	// Скрытие окна (шаг каркаса): в `.JUI` у контрола 8500 стоит Visible="0"
	return FAsdaJuiWindowHandle::Succeeded(R) && bReady;
}

void UAsdaPartyFindSubsystem::ResetWidget()
{
	UGameInstance* GI = GetGameInstance();
	if (UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->UnregisterUiZone(kWindowId);
		WM->CloseWindow(kWindowId);
	}
	for (UAsdaJuiComboBox* C : { CmbJob.Get(), CmbLevel.Get(), CmbChan.Get(),
	                             CmbGoal.Get(), CmbEditNum.Get(), CmbEditDes.Get() })
	{
		if (C) { C->Reset(); }
	}
	Win = nullptr;
	MRowPlate.Reset(); MRowName.Reset(); MRowJob.Reset(); MRowLevel.Reset(); MRowChannel.Reset(); MRowCursor.Reset();
	PRowPlate.Reset(); PRowLeader.Reset(); PRowGoal.Reset(); PRowNum.Reset(); PRowChannel.Reset(); PRowCursor.Reset();
	MSelPlanes.Reset(); PSelPlanes.Reset(); FrameBase.Reset();
	MTxtPage = nullptr; MTxtTotal = nullptr; PTxtPage = nullptr; PTxtTotal = nullptr;
	Buttons.Reset(); Relays.Reset(); MapNodes.Reset(); MapSelPlanes.Reset(); EditBox = nullptr;
	CmbJob = nullptr; CmbLevel = nullptr; CmbChan = nullptr; CmbGoal = nullptr;
	CmbEditNum = nullptr; CmbEditDes = nullptr;
	bReady = false; bOpen = false;
	Mouse.Reset();
}

void UAsdaPartyFindSubsystem::RegisterZone()
{
	UGameInstance* GI = GetGameInstance();
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM) { return; }
	// Рамка окна — планы скина `Popup_SmWin` с префиксом имени окна.
	// ★ Волна 3: зона по планам рамки — общая точка `FAsdaJuiWindowHandle::RegisterFrameZone` (`!bOpen || !Win` → нет зоны).
	FAsdaJuiWindowHandle::RegisterFrameZone(*WM, kWindowId, this, Win, bOpen, TEXT("Find_Party_WIn_0_%d"), 9);
}

void UAsdaPartyFindSubsystem::BindButton(int32 IdNum, const TCHAR* Idc)
{
	UButton* Btn = Cast<UButton>(PfNode(Win, Idc, Idc));
	if (!Btn)
	{
		UE_LOG(LogAsdaPartyFind, Warning, TEXT("[поиск отряда] кнопка %d («%s») не найдена"), IdNum, Idc);
		return;
	}
	Buttons.Add(IdNum, Btn);
	UAsdaButtonRelay* R = NewObject<UAsdaButtonRelay>(this);
	R->Index = IdNum;
	R->Callback = [this](int32 Id) { OnCommand(Id); };
	// Подсказка при наведении — темы 32/33/34/35 (`UI_BuildHelpTooltipText`), гасим по уходу.
	const FPfTipTopic Topic = PfTipForButton(IdNum);
	if (Topic.Title != 0)
	{
		R->HoverCallback = [this, Topic]()
		{
			UGameInstance* GI = GetGameInstance();
			if (UAsdaMiniStateSubsystem* Mini = GI ? GI->GetSubsystem<UAsdaMiniStateSubsystem>() : nullptr)
			{
				Mini->ShowSharedTip(Loc(Topic.Title), Loc(Topic.Body), EAsdaTipOwner::PartyFind);
				bTipShown = true;
			}
		};
		R->UnhoverCallback = [this]()
		{
			UGameInstance* GI = GetGameInstance();
			if (UAsdaMiniStateSubsystem* Mini = GI ? GI->GetSubsystem<UAsdaMiniStateSubsystem>() : nullptr)
			{
				// ★ Гасит ТОЛЬКО владелец (закон `ui-shared-tip-ownership`).
				Mini->HideSharedTip(EAsdaTipOwner::PartyFind);
			}
			bTipShown = false;
		};
		Btn->OnHovered.AddDynamic(R, &UAsdaButtonRelay::OnHovered);
		Btn->OnUnhovered.AddDynamic(R, &UAsdaButtonRelay::OnUnhovered);
	}
	Btn->OnClicked.AddDynamic(R, &UAsdaButtonRelay::OnClicked);
	AsdaUi::DisableButtonFocusRing(Btn);
	Relays.Add(R);
}

void UAsdaPartyFindSubsystem::SetupControls()
{
	bReady = false;
	if (!Win || !Win->WidgetTree) { return; }

	auto Txt = [this](const TCHAR* Idc, const TCHAR* What) -> UTextBlock*
	{
		return Cast<UTextBlock>(PfNode(Win, Idc, What));
	};

	// ── Списки: 10 строк × (плашка + 4 колонки + подсветка) ──
	MRowPlate.SetNum(10); MRowName.SetNum(10); MRowJob.SetNum(10);
	MRowLevel.SetNum(10); MRowChannel.SetNum(10); MRowCursor.SetNum(10);
	PRowPlate.SetNum(10); PRowLeader.SetNum(10); PRowGoal.SetNum(10);
	PRowNum.SetNum(10); PRowChannel.SetNum(10); PRowCursor.SetNum(10);
	for (int32 i = 0; i < 10; ++i)
	{
		const FString Two = FString::Printf(TEXT("%02d"), i);
		MRowPlate[i]   = PfNode(Win, FString::Printf(TEXT("Find_Party_MList%s"), *Two), nullptr);
		MRowName[i]    = Cast<UTextBlock>(PfNode(Win, FString::Printf(TEXT("Find_Party_MChar%s"), *Two), nullptr));
		MRowJob[i]     = Cast<UTextBlock>(PfNode(Win, FString::Printf(TEXT("Find_Party_MJob%s"),  *Two), nullptr));
		MRowLevel[i]   = Cast<UTextBlock>(PfNode(Win, FString::Printf(TEXT("Find_Party_MLv%s"),   *Two), nullptr));
		MRowChannel[i] = Cast<UTextBlock>(PfNode(Win, FString::Printf(TEXT("Find_Party_MCh%s"),   *Two), nullptr));
		MRowCursor[i]  = PfNode(Win, FString::Printf(TEXT("Find_Party_MCur%s"), *Two), nullptr);
		PRowPlate[i]   = PfNode(Win, FString::Printf(TEXT("Find_Party_PList%s"), *Two), nullptr);
		PRowLeader[i]  = Cast<UTextBlock>(PfNode(Win, FString::Printf(TEXT("Find_Party_PChar%s"), *Two), nullptr));
		PRowGoal[i]    = Cast<UTextBlock>(PfNode(Win, FString::Printf(TEXT("Find_Party_PDes%s"),  *Two), nullptr));
		PRowNum[i]     = Cast<UTextBlock>(PfNode(Win, FString::Printf(TEXT("Find_Party_PNum%s"),  *Two), nullptr));
		PRowChannel[i] = Cast<UTextBlock>(PfNode(Win, FString::Printf(TEXT("Find_Party_PCh%s"),   *Two), nullptr));
		PRowCursor[i]  = PfNode(Win, FString::Printf(TEXT("Find_Party_PCur%s"), *Two), nullptr);
	}
	// ⚠ Подсветка `MCur/PCur` и рамка выбора в `.JUI` имеют Visible="0" — гасим явно.
	for (int32 i = 0; i < 10; ++i)
	{
		if (MRowCursor[i]) { MRowCursor[i]->SetVisibility(ESlateVisibility::Collapsed); }
		if (PRowCursor[i]) { PRowCursor[i]->SetVisibility(ESlateVisibility::Collapsed); }
	}
	// ★ Рамки выбора 8570/8644 в ассете — по ВОСЕМЬ планов скина `Box_Green3Strok`
	//   (`Find_Party_MSel_64_0…7`), единого узла нет. Двигаем и гасим их ВМЕСТЕ.
	PfCollectPlanes(Win, TEXT("Find_Party_MSel"), MSelPlanes);
	PfCollectPlanes(Win, TEXT("Find_Party_PSel"), PSelPlanes);
	for (UWidget* W : MSelPlanes) { if (W) { W->SetVisibility(ESlateVisibility::Collapsed); } }
	for (UWidget* W : PSelPlanes) { if (W) { W->SetVisibility(ESlateVisibility::Collapsed); } }
	if (MSelPlanes.Num() == 0 || PSelPlanes.Num() == 0)
	{
		UE_LOG(LogAsdaPartyFind, Warning,
			TEXT("[поиск отряда] планов рамки выбора найдено: левый %d, правый %d (ожидалось по 8)"),
			MSelPlanes.Num(), PSelPlanes.Num());
	}

	MTxtPage  = Txt(TEXT("Find_Party_MTxt1"), TEXT("страница левого списка"));
	MTxtTotal = Txt(TEXT("Find_Party_MTxt2"), TEXT("всего страниц левого списка"));
	PTxtPage  = Txt(TEXT("Find_Party_PTxt1"), TEXT("страница правого списка"));
	PTxtTotal = Txt(TEXT("Find_Party_PTxt2"), TEXT("всего страниц правого списка"));

	// ── Кнопки ──
	BindButton(kIdMFind,      TEXT("Find_Party_MFindBtn"));
	BindButton(kIdMNext,      TEXT("Find_Party_MNext"));
	BindButton(kIdMEnd,       TEXT("Find_Party_MEnd"));
	BindButton(kIdMPre,       TEXT("Find_Party_MPre"));
	BindButton(kIdMFirst,     TEXT("Find_Party_MFirst"));
	BindButton(kIdPFind,      TEXT("Find_Party_PFindBtn"));
	BindButton(kIdPNext,      TEXT("Find_Party_PNext"));
	BindButton(kIdPEnd,       TEXT("Find_Party_PEnd"));
	BindButton(kIdPPre,       TEXT("Find_Party_PPre"));
	BindButton(kIdPFirst,     TEXT("Find_Party_PFirst"));
	BindButton(kIdEditBtn,    TEXT("Find_Party_EditBtn"));
	BindButton(kIdMini2,      TEXT("Find_Party_MiniBtn2"));
	BindButton(kIdClose,      TEXT("Find_Party_ClosBtn"));
	BindButton(kIdRefresh,    TEXT("Find_Party_ReBtn"));
	BindButton(kIdWhisper,    TEXT("Find_Party_WhisBtn"));
	BindButton(kIdEditApply,  TEXT("Find_Party_Edit"));
	BindButton(kIdEditCancel, TEXT("Find_Party_Cancel"));
	BindButton(kIdMiniBtn,    TEXT("Find_Party_MiniBtn"));
	// ★ 8699 `Find_Party_CloseBtn` в `.JUI` стоит с `Visible = "0"` и лежит ТОЧНО под крестиком
	//   8698 (обе 13×13 в одной точке). Импортёр перенёс её живой кнопкой — если не погасить,
	//   она перехватывает клики по «свернуть» (закон `selfhittest-vs-hittest-invisible`).
	if (UWidget* Dead = PfNode(Win, TEXT("Find_Party_CloseBtn"), nullptr))
	{
		Dead->SetVisibility(ESlateVisibility::Collapsed);
	}
	BindButton(kIdMapAll,     TEXT("Find_Party_MapAll"));

	// ── Карты-фильтры. Кнопки скинов `MapName_Normal/InDun/Elite` — интерактивные. ──
	// Имена контролов кнопок карт — `kMaps[i].Idc` (реестр карт, волна 2; порядок прежний).
	for (int32 i = 0; i < kMapCount; ++i)
	{
		UWidget* W = PfNode(Win, kMaps[i].Idc, kMaps[i].Idc);
		if (!W) { continue; }
		MapNodes.Add(kMaps[i].IdNum, W);
		// ★ Импортёр сделал кнопки карт `UCheckBox` (скин `MapName_*` двухсостоянийный), и это
		//   ВЕРНО: эталонная маска только СТАВИТ биты (`flags64_SetBit@0xDBB080`) и никогда их не
		//   снимает — то есть выбор карт множественный (закон `jui-checkbutton-vs-radiobutton`).
		if (UCheckBox* Chk = Cast<UCheckBox>(W))
		{
			UAsdaButtonRelay* R = NewObject<UAsdaButtonRelay>(this);
			R->Index = kMaps[i].IdNum;
			R->CheckCallback = [this](int32 Id, bool bChecked) { OnMapChecked(Id, bChecked); };
			// Наведение на карту — тема 31: заголовок 11909, тело «<имя карты>» + 11910.
			const int32 UiIdx = kMaps[i].UiIndex;
			R->HoverCallback = [this, UiIdx]()
			{
				UGameInstance* GI = GetGameInstance();
				if (UAsdaMiniStateSubsystem* Mini = GI ? GI->GetSubsystem<UAsdaMiniStateSubsystem>() : nullptr)
				{
					Mini->ShowSharedTip(Loc(PfLoc::kTipMapTitle),
						Loc(PfLoc::kMapNameBase + UiIdx) + Loc(PfLoc::kTipMapTail),
						EAsdaTipOwner::PartyFind);
					bTipShown = true;
				}
			};
			R->UnhoverCallback = [this]()
			{
				UGameInstance* GI = GetGameInstance();
				if (UAsdaMiniStateSubsystem* Mini = GI ? GI->GetSubsystem<UAsdaMiniStateSubsystem>() : nullptr)
				{
					Mini->HideSharedTip(EAsdaTipOwner::PartyFind);
				}
				bTipShown = false;
			};
			Chk->OnCheckStateChanged.AddDynamic(R, &UAsdaButtonRelay::OnCheckChanged);
			AsdaUi::AnchorRelay(Relays, R, Chk);   // ★ 31.10.2026: якорь чистит мёртвые/перепривязанные (`asda.UI.RelayAnchorPrune`)
		}
		else
		{
			UE_LOG(LogAsdaPartyFind, Warning,
				TEXT("[поиск отряда] карта %d («%s») импортирована НЕ чек-кнопкой (%s) — клик ")
				TEXT("по ней не подключён"), kMaps[i].IdNum, kMaps[i].Idc, *W->GetClass()->GetName());
		}
	}
	// ★ ПУСТЫЕ СЛОТЫ РАЗМЕТКИ. У `MapN07/N08/N09` и `MapD04` в `.JUI` `IDNum = 4294967295`
	//   (то есть «номера нет»), подписи нет, а внутри лежит плашка-заглушка `Map_Empty`.
	//   Импортёр всё равно сделал из них живые чек-кнопки — снимаем хит-тест, иначе игрок кликает
	//   по пустому месту и ничего не происходит без объяснения.
	{
		const TCHAR* Empties[] = { TEXT("Find_Party_MapN07"), TEXT("Find_Party_MapN08"),
		                           TEXT("Find_Party_MapN09"), TEXT("Find_Party_MapD04") };
		for (const TCHAR* Empty : Empties)
		{
			if (UWidget* Dead = PfNode(Win, Empty, nullptr))
			{
				Dead->SetVisibility(ESlateVisibility::HitTestInvisible);
				if (UCheckBox* C = Cast<UCheckBox>(Dead)) { C->SetIsEnabled(false); }
			}
		}
	}
	// Зелёная рамка выбранной карты 8694 — тоже восемь планов.
	PfCollectPlanes(Win, TEXT("Find_Party_MapSel"), MapSelPlanes);
	for (UWidget* W : MapSelPlanes) { if (W) { W->SetVisibility(ESlateVisibility::Collapsed); } }

	// ── Панель правки отряда 8656 (в `.JUI` Visible="0") ──
	EditBox = PfNode(Win, TEXT("Find_Party_EditBox"), TEXT("панель правки отряда"));
	if (EditBox) { EditBox->SetVisibility(ESlateVisibility::Collapsed); }

	// ── Статичные подписи (эталон ставит их в `FindPartyWindow_InitControls@0xDB68C0`) ──
	auto SetLoc = [this, &Txt](const TCHAR* Idc, int32 LocId, const TCHAR* What)
	{
		if (UTextBlock* T = Txt(Idc, What)) { T->SetText(FText::FromString(Loc(LocId))); }
	};
	SetLoc(TEXT("Find_Party_Move"),        PfLoc::kWinTitle,    TEXT("заголовок окна"));
	SetLoc(TEXT("Find_Party_MTitle"),      PfLoc::kMTitle,      TEXT("заголовок левого списка"));
	SetLoc(TEXT("Find_Party_MJobSel_Txt"), PfLoc::kColJob,      TEXT("подпись фильтра профессии"));
	SetLoc(TEXT("Find_Party_MLvSel_Txt"),  PfLoc::kColLevel,    TEXT("подпись фильтра уровня"));
	SetLoc(TEXT("Find_Party_PTitle"),      PfLoc::kPTitle,      TEXT("заголовок правого списка"));
	SetLoc(TEXT("Find_Party_PChSel_Txt"),  PfLoc::kColChannel,  TEXT("подпись фильтра канала"));
	SetLoc(TEXT("Find_Party_PDesSel_Txt"), PfLoc::kColGoal,     TEXT("подпись фильтра цели"));
	SetLoc(TEXT("Find_Party_Edit_Title"),  PfLoc::kEditTitle,   TEXT("заголовок панели правки"));
	SetLoc(TEXT("Find_Party_EditNumTxt"),  PfLoc::kEditNumTxt,  TEXT("подпись «Количество членов»"));
	SetLoc(TEXT("Find_Party_EditDesTxt"),  PfLoc::kColGoal,     TEXT("подпись «Цель» панели правки"));
	// Подписи кнопок — узлы `<idc>_Text` (закон «скрытая кнопка и её подпись — разные узлы»).
	SetLoc(TEXT("Find_Party_MFindBtn_Text"), PfLoc::kBtnFind,     TEXT("подпись «Поиск» (лев.)"));
	SetLoc(TEXT("Find_Party_PFindBtn_Text"), PfLoc::kBtnFind,     TEXT("подпись «Поиск» (прав.)"));
	SetLoc(TEXT("Find_Party_EditBtn_Text"),  PfLoc::kBtnEdit,     TEXT("подпись «Изменить»"));
	SetLoc(TEXT("Find_Party_ReBtn_Text"),    PfLoc::kBtnRefresh,  TEXT("подпись «Обновить»"));
	SetLoc(TEXT("Find_Party_WhisBtn_Text"),  PfLoc::kBtnWhisper,  TEXT("подпись «Шепот»"));
	SetLoc(TEXT("Find_Party_MiniBtn2_Text"), PfLoc::kBtnMinimize, TEXT("подпись «Свернуть»"));
	SetLoc(TEXT("Find_Party_ClosBtn_Text"),  PfLoc::kBtnCancel,   TEXT("подпись «Отмена»"));
	SetLoc(TEXT("Find_Party_Edit_Text"),     PfLoc::kBtnEdit,     TEXT("подпись «Применить» правки"));
	SetLoc(TEXT("Find_Party_Cancel_Text"),   PfLoc::kBtnCancel2289, TEXT("подпись «Отмена» правки"));
	SetLoc(TEXT("Find_Party_MapAll_Text"),   PfLoc::kMapAll,      TEXT("подпись «По всему региону»"));
	// Подписи карт — эталон перетирает корейские тексты `.JUI` строками 11802…11817.
	for (int32 i = 0; i < kMapCount; ++i)
	{
		SetLoc(*(FString(kMaps[i].Idc) + TEXT("_Text")), PfLoc::kMapNameBase + kMaps[i].UiIndex,
			TEXT("подпись карты"));
	}
	// ★★★ ОТКАТ Д7 (см. `asda.PartyFind.LegacyMapCaptions` в шапке файла). Читаемые цвета лежат
	//   В АССЕТЕ (шаг `Tools/asda_ui_partyfind_map_captions.py`), поэтому по умолчанию здесь НЕ
	//   делается ничего. Ключ = 1 возвращает эталонные цвета из `gamemain_jui.ini` (`CaptionInfo`
	//   контрола): обычные ff6d6341, подземелья ff69453d, элитные ff3f5e35. Операция идемпотентна.
	if (CVarPartyFindLegacyMapCaptions.GetValueOnGameThread() != 0)
	{
		int32 Painted = 0;
		for (int32 i = 0; i < kMapCount; ++i)
		{
			// Категория карты видна прямо в имени контрола: `MapN*` / `MapD*` / `MapE*`.
			const FString Idc(kMaps[i].Idc);
			const uint32 Argb = Idc.Contains(TEXT("MapD")) ? 0xFF69453Du
			                  : Idc.Contains(TEXT("MapE")) ? 0xFF3F5E35u
			                                               : 0xFF6D6341u;
			if (UTextBlock* T = Txt(*(Idc + TEXT("_Text")), nullptr))
			{
				T->SetColorAndOpacity(FSlateColor(AsdaUi::SrgbU32ToLinear(Argb)));
				// ★ Тем же ключом возвращается и ПОСАДКА подписи: эталонный размер 8 и отсутствие
				//   обрезки. ★ 07.10.2026: окно 8500 шире на 124 (утверждено владельцем; плитки 159, шаг
				//   `asda_ui_widen_windows`), и шаг `asda_ui_partyfind_map_captions` ставит тот же размер 8 —
				//   ключ теперь отличается от умолчания цветом и обрезкой; размер 8 оставлен явно.
				FSlateFontInfo F = T->GetFont();
				F.Size = 8;
				T->SetFont(F);
				T->SetClipping(EWidgetClipping::Inherit);
				++Painted;
			}
		}
		UE_LOG(LogAsdaPartyFind, Warning,
			TEXT("[поиск отряда] `asda.PartyFind.LegacyMapCaptions 1`: подписям карт возвращено ")
			TEXT("ЭТАЛОННОЕ оформление (%d из %d) — контраст ~2.0, шрифт 8 без обрезки (плитки 159 с 07.10 — ")
			TEXT("длинные имена входят и без обрезки)"),
			Painted, kMapCount);
	}

	// ★★★ АВТОРСКИЕ ОТСТУПЫ РАМОК ЗАМЕРЯЕМ ЗДЕСЬ И ОДИН РАЗ — до того, как их кто-нибудь подвинет
	//   (закон `jui-flattened-hierarchy-geometry`: замер внутри функции, которая сама двигает узлы,
	//   ловит уже сдвинутые координаты). База = левый-верх ПЕРВОЙ строки соответствующего списка
	//   и первой кнопки карт; дальше рамка ездит дельтой от этой базы.
	FrameBase.Reset();
	auto Remember = [this](const TArray<TObjectPtr<UWidget>>& Planes)
	{
		for (UWidget* W : Planes)
		{
			if (UCanvasPanelSlot* CS = AsdaUi::SlotOf(W)) { FrameBase.Add(W, CS->GetOffsets()); }
		}
	};
	Remember(MSelPlanes);
	Remember(PSelPlanes);
	Remember(MapSelPlanes);
	// ★ База берётся ТЕМ ЖЕ способом, что и цель, — суммой отступов до корня окна
	//   (`PfWindowOffsetOf`), иначе завёрнутые в под-канвас узлы дают ложную дельту.
	auto BaseOf = [](UWidget* W, FVector2D& Out)
	{
		if (W) { Out = PfWindowOffsetOf(W); }
	};
	if (MRowPlate.Num() > 0) { BaseOf(MRowPlate[0], MSelBase); }
	if (PRowPlate.Num() > 0) { BaseOf(PRowPlate[0], PSelBase); }
	if (TObjectPtr<UWidget>* First = MapNodes.Find(kMaps[0].IdNum)) { BaseOf(*First, MapSelBase); }
	// ★ 25.10.2026 [ИСПРАВЛЕНИЕ]: та же сумма отступов у родителя планов каждой рамки (разбор — у полей в `.h`).
	auto ParentBaseOf = [](const TArray<TObjectPtr<UWidget>>& Planes, FVector2D& Out)
	{
		const UWidget* P = (Planes.Num() > 0 && Planes[0]) ? Planes[0]->GetParent() : nullptr;
		Out = P ? PfWindowOffsetOf(P) : FVector2D::ZeroVector;
	};
	ParentBaseOf(MSelPlanes, MSelParentBase);
	ParentBaseOf(PSelPlanes, PSelParentBase);
	ParentBaseOf(MapSelPlanes, MapSelParentBase);

	FillCombos();

	bReady = true;
	UE_LOG(LogAsdaPartyFind, Log,
		TEXT("[поиск отряда] окно разобрано: кнопок %d, карт %d, строк 10+10, комбо %d"),
		Buttons.Num(), MapNodes.Num(),
		(CmbJob ? 1 : 0) + (CmbLevel ? 1 : 0) + (CmbChan ? 1 : 0) + (CmbGoal ? 1 : 0)
		+ (CmbEditNum ? 1 : 0) + (CmbEditDes ? 1 : 0));
}

void UAsdaPartyFindSubsystem::FillCombos()
{
	// Состав каждого списка снят из циклов заполнения `FindPartyWindow_InitControls@0xDB68C0`.
	auto Make = [this](const TCHAR* Idc, int32 First, int32 Count,
	                   int32 IdNum, bool bOpenUp, int32 DefaultIdx) -> UAsdaJuiComboBox*
	{
		UAsdaJuiComboBox* C = NewObject<UAsdaJuiComboBox>(this);
		if (!C->Setup(Win, Idc, kWindowId, bOpenUp))
		{
			// Эталон в этом месте показывает `MessageBoxA(nullptr, "Point NULL", "<idc>", 0)`.
			UE_LOG(LogAsdaPartyFind, Error, TEXT("[поиск отряда] комбобокс %d («%s») не найден"), IdNum, Idc);
			return nullptr;
		}
		TArray<FString> Items;
		Items.Reserve(Count);
		for (int32 i = 0; i < Count; ++i) { Items.Add(Loc(First + i)); }
		C->SetItems(Items);
		C->SetSelectedIndex(DefaultIdx);
		C->OnPicked.BindWeakLambda(this, [this, IdNum](int32) { OnComboChanged(IdNum); });
		return C;
	};
	// Списки фильтров раскрываются ВНИЗ (стоят у верхней кромки своих панелей).
	CmbJob   = Make(TEXT("Find_Party_MJobSel"),  PfLoc::kJobFirst,      5, kIdMJobSel, false, 0);
	CmbLevel = Make(TEXT("Find_Party_MLvSel"),   PfLoc::kLevelFirst,   11, kIdMLvSel,  false, 0);
	CmbChan  = Make(TEXT("Find_Party_PChSel"),   PfLoc::kChanFirst,    13, kIdPChSel,  false, 0);
	CmbGoal  = Make(TEXT("Find_Party_PDesSel"),  PfLoc::kMapNameBase,  12, kIdPDesSel, false, 0);
	// ★ У комбо числа членов эталон предвыбирает ТРЕТИЙ пункт («6 игроков»):
	//   `CControl_SetSelectedItem(ctrl, v42, (_DWORD *)3)` — третий аргумент 3.
	CmbEditNum = Make(TEXT("Find_Party_EditNum"), PfLoc::kNumFirst,      4, kIdEditNum, true, 3);
	CmbEditDes = Make(TEXT("Find_Party_EditDes"), PfLoc::kGoalEditFirst,12, kIdEditDes, true, 0);
}

// ═══════════════════════════════ ТОГГЛ / ОТКРЫТИЕ / ЗАКРЫТИЕ ══════════════════════════════════

void UAsdaPartyFindSubsystem::ToggleWindow()
{
	// Порт `sub_DB9B80@0xDB9B80`. Первое, что делает эталон: если окно ВИДНО — «свернуть» + нотис.
	if (bOpen)
	{
		MinimizeWindow();
		NoticeLoc(PfLoc::kNoticeMinim);       // 11906 в ЧАТ
		return;
	}
	OpenWindow();
}

void UAsdaPartyFindSubsystem::OpenWindow()
{
	if (!EnsureWindow()) { return; }
	UGameInstance* GI = GetGameInstance();
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	UAsdaNetSubsystem*    Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	UAsdaPartySubsystem*  Party = GI ? GI->GetSubsystem<UAsdaPartySubsystem>() : nullptr;
	UAsdaWorldSubsystem*  World = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
	if (WM)
	{
		WM->SetWindowVisible(kWindowId, true);   // `CControl_SetVisible(win, 1)`
		WM->BringWindowToFront(kWindowId);       // `CClass_MapInsert(g_pWindowMgr, win)`
	}
	bOpen = true;
	Mouse.Reset();

	const int32 Members  = Party ? Party->GetMembers().Num() : 0;
	const bool  bInParty = Party ? Party->IsInParty() : false;
	const bool  bLeader  = Party ? Party->IsPartyLeader() : false;
	const FString SelfName = World ? World->GetSelfCharName() : FString();

	if (bClosedFully)
	{
		// ── Полное открытие (эталон: ветка `this+0x1640 == 1`) ──
		// Кнопка «Изменить» видна ТОЛЬКО лидеру.
		if (TObjectPtr<UButton>* B = Buttons.Find(kIdEditBtn))
		{
			if (*B) { (*B)->SetVisibility(bLeader ? ESlateVisibility::Visible : ESlateVisibility::Collapsed); }
		}
		// Четыре счётчика страниц печатаются БЕЗУСЛОВНО (закон «рисовать безусловно»).
		// ★ ИСПРАВЛЕНИЕ 11.10.2026 (дедуп №2, В2-A, U10e): было `FText::AsNumber` (разделитель разрядов по культуре: EN «1,234», RU «1 234») — стало `FString::FromInt`, как `sprintf("%d")` эталона. Функции открытия
		//   `sub_DB9B80` в выгрузке эталона нет; те же четыре поля (+184/+186, +798/+800) печатают
		//   `findPartyRefreshListA/B` через `sprintf("%d")` (ui_core.c:64711-64714, 64749-64752).
		if (MTxtPage)  { MTxtPage->SetText(FText::FromString(FString::FromInt(MCurPage))); }
		if (MTxtTotal) { MTxtTotal->SetText(FText::FromString(FString::FromInt(MTotalPages))); }
		if (PTxtPage)  { PTxtPage->SetText(FText::FromString(FString::FromInt(PCurPage))); }
		if (PTxtTotal) { PTxtTotal->SetText(FText::FromString(FString::FromInt(PTotalPages))); }
		SelectRow(0, -1);
		SelectRow(1, -1);
		// ★ Эталон: `Party_IsInParty() >= 2` ⇒ я не одиночка, мой номер в списке одиночек сбрасывается.
		if (Members >= 2) { MemberRegId = -1; }

		if (bInParty && !bLeader)
		{
			SendGetMember();                       // 3414 — просто смотрю оба списка
			SendGetLeader();                       // 3422
		}
		else
		{
			if (Members == 0)
			{
				if (Net) { Net->SendPartyFindAddMember3410(SelfName); }   // 3410 — встать в список
				SendGetLeader();                                         // 3422
				ShowFirstOpenNotice(SelfName, /*bAsLeader*/ false);
			}
			if (bLeader && Net)
			{
				SendGetMember();                                         // 3414
				// ★ 1:1: эталон в 3418 всегда кладёт МАКС = 6 и текущее = Party_IsInParty().
				Net->SendPartyFindAddLeader3418(SelfName, 6, Members, Party ? Party->GetPartyIndex() : -1);
				ShowFirstOpenNotice(SelfName, /*bAsLeader*/ true);
			}
		}
	}
	else
	{
		// ── Разворачиваем из «свёрнуто»: только перезапрос обеих страниц ──
		SendGetMember();
		SendGetLeader();
	}
	bClosedFully = false;
	// Эталон в конце открытия снимает таймер автообновления (`KillTimer(hwnd, 26)`).
	bMinimizedTimer = false;
	MinimizedAccum = 0.f;

	UE_LOG(LogAsdaPartyFind, Log,
		TEXT("[поиск отряда] окно 8500 открыто: вОтряде=%d лидер=%d членов=%d мойНомерОдиночки=%d ")
		TEXT("мойОтряд=%d"), bInParty ? 1 : 0, bLeader ? 1 : 0, Members, MemberRegId, LeaderRegId);
}

void UAsdaPartyFindSubsystem::MinimizeWindow()
{
	// Порт `uiWindowCloseStartTimer@0xDB9FF0`: спрятать окно, `this+0x1640 = 0`,
	//   `SetTimer(hwnd, 26, 800)`, и если подсказка была показана — погасить её.
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UAsdaWindowSubsystem* WM = GI->GetSubsystem<UAsdaWindowSubsystem>())
		{
			WM->SetWindowVisible(kWindowId, false);
		}
		if (bTipShown)
		{
			if (UAsdaMiniStateSubsystem* Mini = GI->GetSubsystem<UAsdaMiniStateSubsystem>())
			{
				Mini->HideSharedTip(EAsdaTipOwner::PartyFind);
			}
			bTipShown = false;
		}
	}
	bOpen = false;
	bClosedFully = false;          // остаёмся в списках
	bMinimizedTimer = true;        // таймер 800 мс держит регистрацию живой
	MinimizedAccum = 0.f;
	ShowEditPanel(false);
	UE_LOG(LogAsdaPartyFind, Log, TEXT("[поиск отряда] окно свёрнуто, таймер 800 мс запущен"));
}

void UAsdaPartyFindSubsystem::CloseWindow()
{
	// Порт `uiFindPartyWindowClose@0xDB9ED0`: очистить обе таблицы, снять себя из обоих списков
	//   (3412 + 3420), спрятать карты, снять таймер, сбросить маски и погасить подсказку.
	UGameInstance* GI = GetGameInstance();
	if (UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->SetWindowVisible(kWindowId, false);
	}
	ClearTables();
	if (UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr)
	{
		Net->SendPartyFindSubMember3412(MemberRegId);
		Net->SendPartyFindSubLeader3420(LeaderRegId);
	}
	ApplyMapHighlight(-1);
	ShowEditPanel(false);
	bMinimizedTimer = false;
	MinimizedAccum = 0.f;
	MapMaskA = 0; MapMaskB = 0;    // эталон: `this+0x1638 = 0`, `this+0x163C = 0`
	bClosedFully = true;           // эталон: `this+0x1640 = 1`
	bOpen = false;
	if (bTipShown)
	{
		if (UAsdaMiniStateSubsystem* Mini = GI ? GI->GetSubsystem<UAsdaMiniStateSubsystem>() : nullptr)
		{
			Mini->HideSharedTip(EAsdaTipOwner::PartyFind);
		}
		bTipShown = false;
	}
	UE_LOG(LogAsdaPartyFind, Log, TEXT("[поиск отряда] окно закрыто, оба списка покинуты"));
}

// ═══════════════════════════════ КОМАНДЫ КНОПОК ═══════════════════════════════════════════════

void UAsdaPartyFindSubsystem::OnCommand(int32 IdNum)
{
	// Порт `uiPartyRecruitOnCommand@0xDB9520`. Порядок и номера веток — эталонные.
	UGameInstance* GI = GetGameInstance();
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	UAsdaPartySubsystem* Party = GI ? GI->GetSubsystem<UAsdaPartySubsystem>() : nullptr;

	switch (IdNum)
	{
	case kIdMapAll:
		// «По всему региону»: снять фильтр карт и обновить обе свои записи.
		bWholeRegion = true;
		ApplyMapHighlight(-1);
		// ★ Снимаем галки со всех карт: иначе кнопка говорит «по всему региону», а галки
		//   продолжают показывать выбранные карты. В оригинале галок-виджетов не было —
		//   там `MapName_*` рисовался кадром выбора, который тут же гасит `ApplyMapHighlight(-1)`.
		for (const TPair<int32, TObjectPtr<UWidget>>& It : MapNodes)
		{
			if (UCheckBox* C = Cast<UCheckBox>(It.Value.Get()))
			{
				if (C->IsChecked()) { C->SetIsChecked(false); }
			}
		}
		if (Net)
		{
			MReqPage = 1;   // ★ 1:1: эталонный `net_SendRequest3416@0xDBCFE0` сам взводит страницу в 1 (`selfPtr[185] = 1`)
			Net->SendPartyFindUpdateMember3416(MemberRegId, MapMaskA, MapMaskB);
			Net->SendPartyFindUpdateLeader3424(LeaderRegId, MapMaskA, MapMaskB,
				CmbEditDes ? PfRemapForward(CmbEditDes->GetSelectedIndex()) : -1,
				CmbEditNum ? CmbEditNum->GetSelectedIndex() + 3 : 6,
				Party ? Party->GetMembers().Num() : 0);
		}
		break;

	case kIdMFind:  MReqPage = 1;                            SendGetMember(); break;
	case kIdMNext:  MCurPage = FMath::Min(MCurPage + 1, MTotalPages); MReqPage = MCurPage; SendGetMember(); break;
	case kIdMPre:   MCurPage = FMath::Max(MCurPage - 1, 1);  MReqPage = MCurPage; SendGetMember(); break;
	case kIdMEnd:   MCurPage = MTotalPages; MReqPage = MTotalPages;              SendGetMember(); break;
	case kIdMFirst: MCurPage = 1;           MReqPage = 1;                        SendGetMember(); break;

	case kIdPFind:  PReqPage = 1;                            SendGetLeader(); break;
	case kIdPNext:  PCurPage = FMath::Min(PCurPage + 1, PTotalPages); PReqPage = PCurPage; SendGetLeader(); break;
	case kIdPPre:   PCurPage = FMath::Max(PCurPage - 1, 1);  PReqPage = PCurPage; SendGetLeader(); break;
	case kIdPEnd:   PCurPage = PTotalPages; PReqPage = PTotalPages;              SendGetLeader(); break;
	case kIdPFirst: PCurPage = 1;           PReqPage = 1;                        SendGetLeader(); break;

	case kIdEditBtn:
		// ★ Гейт 1:1: панель правки показывается ТОЛЬКО лидеру (`Party_IsMeBoss`).
		if (Party && Party->IsPartyLeader()) { ShowEditPanel(true); }
		else
		{
			UE_LOG(LogAsdaPartyFind, Log,
				TEXT("[поиск отряда] «Изменить» нажата не лидером — эталон в этом случае молчит"));
		}
		break;

	case kIdMini2:
	case kIdMiniBtn:
		MinimizeWindow();
		NoticeLoc(PfLoc::kNoticeMinim);      // 11906
		break;

	case kIdClose:
		CloseWindow();
		NoticeLoc(PfLoc::kNoticeCancel);     // 11907
		break;

	case kIdRefresh:
		MReqPage = 1; PReqPage = 1;
		SendGetMember();
		SendGetLeader();
		break;

	case kIdWhisper:
	{
		// Эталон `CChatList_OnSelectWhisper@0xDBB7C0`: приоритет у ЛЕВОГО списка; если там
		//   выбора нет — берём правый. Строка с цветовым тегом (своя, подсвеченная) не годится:
		//   эталон в этом случае просто снимает выделение.
		FString Target;
		if (MSelIdx >= 0 && Singles.IsValidIndex(MSelIdx)) { Target = Singles[MSelIdx].Name; }
		else if (PSelIdx >= 0 && Parties.IsValidIndex(PSelIdx)) { Target = Parties[PSelIdx].LeaderName; }
		if (Target.IsEmpty())
		{
			UE_LOG(LogAsdaPartyFind, Log, TEXT("[поиск отряда] «Шепот»: строка не выбрана — снимаем выделение"));
			SelectRow(0, -1); SelectRow(1, -1);
			break;
		}
		if (UAsdaChatWindowSubsystem* Chat = GI ? GI->GetSubsystem<UAsdaChatWindowSubsystem>() : nullptr)
		{
			// ★★★ ИСПРАВЛЕНО 2026-08-30 (жалоба владельца «в чате не было этого видно»).
			//   Эталон `CChatList_OnSelectWhisper@0xDBB7C0` печатает в САМО ПОЛЕ ВВОДА литерал
			//   «/w <имя> » и отдаёт ему фокус. Раньше порт только переключал состояние 7, поле
			//   оставалось ПУСТЫМ — на экране не менялось ничего, и игрок жал кнопку по шесть раз
			//   (см. лог 2026-08-30 18:08:36). Теперь общая точка `BeginWhisperTo`.
			Chat->BeginWhisperTo(Target);
			UE_LOG(LogAsdaPartyFind, Log, TEXT("[поиск отряда] «Шепот» → чат в приват «%s»"), *Target);
			// ★ Эталон снимает выделение строки ВСЕГДА, а не только при пустом выборе: последняя
			//   строка `CChatList_OnSelectWhisper` — `return sub_92597C(v13, a2)`, а `sub_92597C`
			//   доказана вторым методом как «снять выделение списка» (её же зовут
			//   `uiClearListRowsText@0xDBA400` и `uiSlotGrid_ClearAllText@0xDBA510`, ui_core.c:64473/64496).
			SelectRow(0, -1); SelectRow(1, -1);
		}
		else
		{
			UE_LOG(LogAsdaPartyFind, Error, TEXT("[поиск отряда] «Шепот»: окно чата не поднято"));
		}
		break;
	}

	case kIdEditApply:
		if (Net)
		{
			Net->SendPartyFindUpdateLeader3424(LeaderRegId, MapMaskA, MapMaskB,
				CmbEditDes ? PfRemapForward(CmbEditDes->GetSelectedIndex()) : -1,
				CmbEditNum ? CmbEditNum->GetSelectedIndex() + 3 : 6,
				Party ? Party->GetMembers().Num() : 0);
		}
		ShowEditPanel(false);
		NoticeLoc(PfLoc::kNoticeEdited);     // 11908
		break;

	case kIdEditCancel:
		ShowEditPanel(false);
		break;

	default:
		// Эталон: `default: return 1;` — молча. Пишем в журнал, чтобы «ничего не произошло»
		//   не выглядело как потерянный клик (закон `no-scope-cutting`).
		UE_LOG(LogAsdaPartyFind, Log,
			TEXT("[поиск отряда] контрол %d: ветки нет и в эталоне (`default: return`)"), IdNum);
		break;
	}
}

void UAsdaPartyFindSubsystem::OnMapChecked(int32 IdNum, bool bChecked)
{
	// Порт ветки события 0x10601 (`0xDB8B5B…0xDB8EE1`).
	// ★★★ ДЕФЕКТ ОРИГИНАЛА Д2: верхняя граница диапазона у эталона равна 8687, поэтому карта
	//   8688 «Логово Дракона» не кликается. Ключ отката возвращает эталонное поведение.
	const int32 Last = (CVarPartyFindLegacyMapRange.GetValueOnGameThread() != 0) ? 8687 : kIdMapLast;
	if (IdNum < kIdMapFirst || IdNum > Last)
	{
		UE_LOG(LogAsdaPartyFind, Warning,
			TEXT("[поиск отряда] карта %d вне диапазона %d…%d — клик отброшен ")
			TEXT("(при `asda.PartyFind.LegacyMapRange 1` это ЭТАЛОННОЕ поведение для 8688)"),
			IdNum, kIdMapFirst, Last);
		return;
	}
	int32 UiIdx = -1;
	for (const FPfMap& M : kMaps) { if (M.IdNum == IdNum) { UiIdx = M.UiIndex; break; } }
	if (UiIdx < 0) { return; }
	const int32 Wire = PfRemapForward(UiIdx);

	// Эталон: `flags64_SetBit(this, wire)` — бит только СТАВИТСЯ, ранее выбранные НЕ снимаются
	//   (множественный выбор карт). ★ ДЕФЕКТ Д3: снимающей ветки в оригинале нет вовсе, поэтому
	//   снятая галка оставляла карту в фильтре. Исправлено, ключ отката — см. CVar выше.
	if (Wire >= 0)
	{
		const uint64 Bit = 1ull << (uint32)(Wire % 64);
		const bool bLegacy = (CVarPartyFindLegacyMaskNeverClears.GetValueOnGameThread() != 0);
		if (bChecked || bLegacy)
		{
			MapMaskA |= (uint32)(Bit & 0xFFFFFFFFull);
			MapMaskB |= (uint32)(Bit >> 32);
		}
		else
		{
			MapMaskA &= ~(uint32)(Bit & 0xFFFFFFFFull);
			MapMaskB &= ~(uint32)(Bit >> 32);
		}
	}
	bWholeRegion = false;                      // эталон: `this+0x1644 = 0`

	UGameInstance* GI = GetGameInstance();
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	UAsdaPartySubsystem* Party = GI ? GI->GetSubsystem<UAsdaPartySubsystem>() : nullptr;
	if (Net && Party && Party->GetMembers().Num() == 0)
	{
		MReqPage = 1;   // ★ 1:1: эталонный `net_SendRequest3416@0xDBCFE0` сам взводит страницу в 1 (`selfPtr[185] = 1`)
		Net->SendPartyFindUpdateMember3416(MemberRegId, MapMaskA, MapMaskB);
	}
	SendGetMember();
	// Зелёная рамка едет на карту только при ВЫБОРЕ; при снятии галки прячем её.
	ApplyMapHighlight(bChecked ? Wire : -1);

	// Нотис выбора карты: `sprintf("%s %s", loc(11801 + idx), loc(11905))` в ЧАТ.
	//   ★ Эталон выдаёт его на КАЖДОМ событии 0x10601, не различая «выбрал» и «снял» — так и
	//   оставляем: строка сообщает, по какой карте теперь идёт набор.
	if (UAsdaMessageSubsystem* Msg = GI ? GI->GetSubsystem<UAsdaMessageSubsystem>() : nullptr)
	{
		Msg->PostText(EAsdaMsgCategory::Social,
			Loc(PfLoc::kMapNameBase + UiIdx) + TEXT(" ") + Loc(PfLoc::kJoinList));
	}
	UE_LOG(LogAsdaPartyFind, Log,
		TEXT("[поиск отряда] карта %d (UI %d → код %d) %s; маска %08X/%08X"),
		IdNum, UiIdx, Wire, bChecked ? TEXT("ДОБАВЛЕНА в фильтр") : TEXT("УБРАНА из фильтра"),
		MapMaskA, MapMaskB);
}

void UAsdaPartyFindSubsystem::OnComboChanged(int32 IdNum)
{
	// Порт ветки события 0x11201 (`0xDB8EE6…0xDB8F44`): смена фильтра лишь ВЗВОДИТ номер
	//   запрошенной страницы в 1; сам запрос уходит по кнопке «Поиск».
	switch (IdNum)
	{
	case kIdMJobSel:
	case kIdMLvSel:  MReqPage = 1; break;
	case kIdPChSel:
	case kIdPDesSel: PReqPage = 1; break;
	default: break;
	}
	UE_LOG(LogAsdaPartyFind, Log, TEXT("[поиск отряда] фильтр %d изменён, страница взведена в 1"), IdNum);
}

void UAsdaPartyFindSubsystem::ShowEditPanel(bool bShow)
{
	if (EditBox)
	{
		EditBox->SetVisibility(bShow ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
}

// ═══════════════════════════════ ЗАПРОСЫ СТРАНИЦ ══════════════════════════════════════════════

void UAsdaPartyFindSubsystem::SendGetMember()
{
	UGameInstance* GI = GetGameInstance();
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net) { return; }
	// Фильтр профессии: idx 0 → −1 «Все», idx 4 → 0 «Новичок», иначе idx (1/2/3).
	const int32 JobIdx = CmbJob ? CmbJob->GetSelectedIndex() : 0;
	const int32 JobFilter = (JobIdx <= 0) ? -1 : (JobIdx == 4 ? 0 : JobIdx);
	// Границы уровня: idx 0 → 1…100, иначе 10*(idx−1)+1 … 10*idx.
	const int32 LvIdx = CmbLevel ? CmbLevel->GetSelectedIndex() : 0;
	const int32 LvMin = (LvIdx > 0) ? (10 * (LvIdx - 1) + 1) : 1;
	const int32 LvMax = (LvIdx > 0) ? (10 * LvIdx)           : 100;
	// «По всему региону» ⇒ маски обнуляются (эталон: `if (this+0x1644 == 1) маски = 0`).
	const uint32 A = bWholeRegion ? 0u : MapMaskA;
	const uint32 B = bWholeRegion ? 0u : MapMaskB;
	bReqInFlight = true; ReqAccum = 0.f; ReqOpcode = 3414;
	Net->SendPartyFindGetMember3414(MReqPage, MemberRegId, A, B, JobFilter, LvMin, LvMax);
}

void UAsdaPartyFindSubsystem::SendGetLeader()
{
	UGameInstance* GI = GetGameInstance();
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net) { return; }
	const int32 ChanIdx = CmbChan ? CmbChan->GetSelectedIndex() : 0;
	const int32 ChanFilter = ChanIdx - 1;                       // 0 «Все» → −1
	const int32 GoalIdx = CmbGoal ? CmbGoal->GetSelectedIndex() : 0;
	const int32 GoalFilter = PfRemapForward(GoalIdx);           // idx 0 → −1
	const uint32 A = bWholeRegion ? 0u : MapMaskA;
	const uint32 B = bWholeRegion ? 0u : MapMaskB;
	bReqInFlight = true; ReqAccum = 0.f; ReqOpcode = 3422;
	Net->SendPartyFindGetLeader3422(PReqPage, LeaderRegId, A, B, ChanFilter, GoalFilter);
}

// ═══════════════════════════════ ПРИЁМ ════════════════════════════════════════════════════════

void UAsdaPartyFindSubsystem::OnAck(int32 Opcode, const TArray<uint8>& Body)
{
	bReqInFlight = false; ReqAccum = 0.f;
	UGameInstance* GI = GetGameInstance();
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (Body.Num() < 7)
	{
		UE_LOG(LogAsdaPartyFind, Warning, TEXT("[поиск отряда] ответ %d короче 7 б (%d) — отброшен"),
			Opcode, Body.Num());
		return;
	}
	const int32 CharId = AsdaBytes::RdI32(&Body[2]);
	const int32 Code   = (int32)Body[6];
	const int32 Index  = (Body.Num() >= 9) ? AsdaBytes::RdI16(&Body[7]) : -1;
	// ⚠ Эталонные приёмники сверяют `dword[+2]` со своим charId и при расхождении МОЛЧА выходят.
	// ★ ИСПРАВЛЕНИЕ 11.10.2026 (дедуп №2, В2-A, N2): «charId мой?» — `AsdaSelf::IsSelfCharId` (свой ещё неизвестен ⇒ не мой; было 0 == 0 ⇒ мой).
	if (Net && !AsdaSelf::IsSelfCharId(Net, CharId))
	{
		UE_LOG(LogAsdaPartyFind, Warning,
			TEXT("[поиск отряда] ответ %d не мой: charId пакета %d ≠ %d — отброшен (как в эталоне)"),
			Opcode, CharId, Net->SelectedCharId);
		return;
	}
	// Расшифровка кодов — §4.2 отчёта. Эталон проверяет ТОЛЬКО код 1, прочие игнорирует молча;
	//   мы их ПИШЕМ В ЖУРНАЛ, но нотиса не выдумываем (закон `never-invent-only-original`).
	const TCHAR* Why =
		Code == 1 ? TEXT("успех") :
		Code == 2 ? TEXT("charId тела ≠ charId сессии") :
		Code == 3 ? TEXT("нет свободного слота / индекс вне диапазона") :
		Code == 4 ? TEXT("charId записи не совпал (или «уже в списке» / «не лидер»)") :
		Code == 5 ? TEXT("отряд уже опубликован") : TEXT("неизвестный код");
	UE_LOG(LogAsdaPartyFind, Log, TEXT("[поиск отряда] ←%d код=%d (%s) индекс=%d"),
		Opcode, Code, Why, Index);

	switch (Opcode)
	{
	case 3411:   // порт `sub_DBF410`: успех → запомнить свой номер и сразу запросить страницу
		if (Code == 1) { MemberRegId = Index; }
		SendGetMember();
		break;
	case 3413:   // порт `sub_DBF460`: меня сняли со списка одиночек
		MemberRegId = -1;
		break;
	case 3417:   // порт `sub_DBF920`: своя запись обновлена → перезапросить страницу
		SendGetMember();
		break;
	case 3419:   // порт `sub_DBF960`: отряд опубликован → номер отряда, номер одиночки сбросить
		if (Code == 1) { LeaderRegId = Index; MemberRegId = -1; }
		SendGetLeader();
		break;
	case 3421:   // порт `sub_DBF9C0`: отряд снят со списка
		LeaderRegId = -1;
		break;
	case 3425:   // порт `sub_DBFF30`: правка применена → перезапросить страницу
		SendGetLeader();
		break;
	default: break;
	}
}

void UAsdaPartyFindSubsystem::OnPage(int32 Opcode, const TArray<uint8>& Body)
{
	bReqInFlight = false; ReqAccum = 0.f;
	UGameInstance* GI = GetGameInstance();
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (Body.Num() < 10)
	{
		UE_LOG(LogAsdaPartyFind, Warning, TEXT("[поиск отряда] страница %d короче хедера (%d б)"),
			Opcode, Body.Num());
		return;
	}
	// Заголовок страницы (§2.1 отчёта): +0 u16 userNum (эхо), +2 u32 charId (эхо),
	//   +6 i16 номер страницы (1-based), +8 i16 всего страниц.
	const int32 CharId = AsdaBytes::RdI32(&Body[2]);
	const int32 Page   = AsdaBytes::RdI16(&Body[6]);
	const int32 Total  = AsdaBytes::RdI16(&Body[8]);
	if (Net && !AsdaSelf::IsSelfCharId(Net, CharId))   // ★ ИСПРАВЛЕНИЕ 11.10.2026 (дедуп №2, В2-A, N2): «charId мой?» — `AsdaSelf::IsSelfCharId` (свой ещё неизвестен ⇒ не мой; было 0 == 0 ⇒ мой)
	{
		// 1:1 эталон `0xDBFA12: cmp [eax+2], [ecx+0x2D4]` — чужой пакет молча отбрасывается.
		UE_LOG(LogAsdaPartyFind, Warning,
			TEXT("[поиск отряда] страница %d не моя: charId %d ≠ %d — отброшена"),
			Opcode, CharId, Net->SelectedCharId);
		return;
	}

	if (Opcode == 3415)
	{
		// Запись 29 б: +0 u16 userNum · +2 i32 charId (−1 = конец) · +6 u8 класс · +7 u8 уровень ·
		//   +8 i8 канал (0-based) · +9 char[20] имя.
		MCurPage = FMath::Max(1, Page);
		MTotalPages = FMath::Max(1, Total);
		for (int32 i = 0; i < 10; ++i)
		{
			FAsdaPartyFindSingle& R = Singles[i];
			R = FAsdaPartyFindSingle();
			const int32 Off = 10 + 29 * i;
			if (Off + 29 > Body.Num()) { break; }
			R.UserNum = AsdaBytes::RdU16(&Body[Off + 0]);
			R.CharId  = AsdaBytes::RdI32(&Body[Off + 2]);
			if (R.CharId == -1) { break; }
			R.ClassId = (int32)Body[Off + 6];
			R.Level   = (int32)Body[Off + 7];
			R.Channel = (int32)(int8)Body[Off + 8];
			R.Name    = FAsdaLoginData::DecodeCp1251(&Body[Off + 9], 20);
		}
		RefreshLeftList();
	}
	else if (Opcode == 3423)
	{
		// Запись 34 б: +0 u16 userNum лидера · +2 i32 charId (−1 = конец) · +6/+7 u8 (эталон
		//   сохраняет и не выводит) · +8 i8 канал · +9 char[20] имя лидера ·
		//   +29 u8 макс. членов · +30 u8 текущее членов · +31 u16 PartyIndex · +33 i8 цель.
		PCurPage = FMath::Max(1, Page);
		PTotalPages = FMath::Max(1, Total);
		for (int32 i = 0; i < 10; ++i)
		{
			FAsdaPartyFindParty& R = Parties[i];
			R = FAsdaPartyFindParty();
			const int32 Off = 10 + 34 * i;
			if (Off + 34 > Body.Num()) { break; }
			R.UserNum   = AsdaBytes::RdU16(&Body[Off + 0]);
			R.CharId    = AsdaBytes::RdI32(&Body[Off + 2]);
			if (R.CharId == -1) { break; }
			R.RawByte6  = (int32)Body[Off + 6];
			R.RawByte7  = (int32)Body[Off + 7];
			R.Channel   = (int32)(int8)Body[Off + 8];
			R.LeaderName = FAsdaLoginData::DecodeCp1251(&Body[Off + 9], 20);
			R.MaxMembers = (int32)Body[Off + 29];
			R.CurMembers = (int32)Body[Off + 30];
			R.PartyIndex = AsdaBytes::RdU16(&Body[Off + 31]);
			R.GoalWire   = (int32)(int8)Body[Off + 33];
		}
		RefreshRightList();
	}
	else
	{
		UE_LOG(LogAsdaPartyFind, Warning,
			TEXT("[поиск отряда] страница с неизвестным опкодом %d (%d б) — не разобрана"),
			Opcode, Body.Num());
	}
}

// ═══════════════════════════════ ПЕРЕРИСОВКА СПИСКОВ ══════════════════════════════════════════

void UAsdaPartyFindSubsystem::ClearTables()
{
	// Порт `tableResetRecords10@0xDBA640` + `tableClearRecords10@0xDBA750`: обнулить буферы строк
	//   и вернуть счётчики страниц в 1.
	for (FAsdaPartyFindSingle& S : Singles) { S = FAsdaPartyFindSingle(); }
	for (FAsdaPartyFindParty&  P : Parties) { P = FAsdaPartyFindParty();  }
	MCurPage = 1; MTotalPages = 1;
	PCurPage = 1; PTotalPages = 1;
	MSelIdx = -1; PSelIdx = -1;
	RefreshLeftList();
	RefreshRightList();
}

void UAsdaPartyFindSubsystem::RefreshLeftList()
{
	// Порт `findPartyRefreshListA@0xDBABA0`. ★ Рисуем БЕЗУСЛОВНО, а не «только при изменении».
	if (!bReady) { return; }
	if (MCurPage < 1) { MCurPage = 1; }
	const FString ChanWord = Loc(PfLoc::kColChannel);
	const FString LvWord   = Loc(PfLoc::kLevelWord);
	for (int32 i = 0; i < 10; ++i)
	{
		const bool bHas = Singles.IsValidIndex(i) && Singles[i].CharId != -1;
		const FAsdaPartyFindSingle& R = Singles[i];
		if (MRowName[i])    { MRowName[i]->SetText(FText::FromString(bHas ? R.Name : FString())); }
		if (MRowJob[i])     { MRowJob[i]->SetText(FText::FromString(bHas ? Loc(PfLoc::kClassNameBase + R.ClassId) : FString())); }
		if (MRowLevel[i])   { MRowLevel[i]->SetText(FText::FromString(bHas ? FString::Printf(TEXT("%s %d"), *LvWord, R.Level) : FString())); }
		if (MRowChannel[i]) { MRowChannel[i]->SetText(FText::FromString(bHas ? FString::Printf(TEXT("%d%s"), R.Channel + 1, *ChanWord) : FString())); }
	}
	// ★ ИСПРАВЛЕНИЕ 11.10.2026 (дедуп №2, В2-A, U10e): было `FText::AsNumber` (разделитель разрядов по культуре: EN «1,234», RU «1 234») — стало `FString::FromInt`, как `sprintf("%d")` эталона (`findPartyRefreshListA@0xDBABA0`, ui_core.c:64711-64714).
	if (MTxtPage)  { MTxtPage->SetText(FText::FromString(FString::FromInt(MCurPage))); }
	if (MTxtTotal) { MTxtTotal->SetText(FText::FromString(FString::FromInt(MTotalPages))); }
}

void UAsdaPartyFindSubsystem::RefreshRightList()
{
	// Порт `findPartyRefreshListB@0xDBADB0`. Колонка «Цель» — текст `11876 + remapIndexReverse(код)`.
	if (!bReady) { return; }
	if (PCurPage < 1) { PCurPage = 1; }
	const FString ChanWord = Loc(PfLoc::kColChannel);
	for (int32 i = 0; i < 10; ++i)
	{
		const bool bHas = Parties.IsValidIndex(i) && Parties[i].CharId != -1;
		const FAsdaPartyFindParty& R = Parties[i];
		if (PRowLeader[i])  { PRowLeader[i]->SetText(FText::FromString(bHas ? R.LeaderName : FString())); }
		if (PRowGoal[i])
		{
			FString Goal;
			if (bHas)
			{
				const int32 UiIdx = PfRemapReverse(R.GoalWire);
				Goal = (UiIdx >= 0) ? Loc(PfLoc::kGoalEditFirst + UiIdx) : FString();
			}
			PRowGoal[i]->SetText(FText::FromString(Goal));
		}
		if (PRowNum[i])     { PRowNum[i]->SetText(FText::FromString(bHas ? FString::Printf(TEXT("%d/%d"), R.CurMembers, R.MaxMembers) : FString())); }
		if (PRowChannel[i]) { PRowChannel[i]->SetText(FText::FromString(bHas ? FString::Printf(TEXT("%d%s"), R.Channel + 1, *ChanWord) : FString())); }
	}
	// ★ ИСПРАВЛЕНИЕ 11.10.2026 (дедуп №2, В2-A, U10e): было `FText::AsNumber` (разделитель разрядов по культуре: EN «1,234», RU «1 234») — стало `FString::FromInt`, как `sprintf("%d")` эталона (`findPartyRefreshListB@0xDBADB0`, ui_core.c:64749-64752).
	if (PTxtPage)  { PTxtPage->SetText(FText::FromString(FString::FromInt(PCurPage))); }
	if (PTxtTotal) { PTxtTotal->SetText(FText::FromString(FString::FromInt(PTotalPages))); }
}

// ═══════════════════════════════ ВЫБОР СТРОКИ ═════════════════════════════════════════════════

void UAsdaPartyFindSubsystem::SelectRow(int32 Side, int32 Row)
{
	// Порт `wnd_HideTabGroup@0xDBA860` (снятие) + перенос рамки выбора на строку.
	TArray<TObjectPtr<UWidget>>& Plates = (Side == 0) ? MRowPlate : PRowPlate;
	TArray<TObjectPtr<UWidget>>& Frame  = (Side == 0) ? MSelPlanes : PSelPlanes;
	const FVector2D& Base = (Side == 0) ? MSelBase : PSelBase;
	int32& SelIdx = (Side == 0) ? MSelIdx : PSelIdx;
	SelIdx = Row;
	if (Frame.Num() == 0) { return; }
	if (Row < 0 || !Plates.IsValidIndex(Row) || !Plates[Row])
	{
		for (UWidget* W : Frame) { if (W) { W->SetVisibility(ESlateVisibility::Collapsed); } }
		return;
	}
	// ★ Двигаем ВСЕ восемь планов на одну дельту от АВТОРСКОЙ раскладки. Координату строки
	//   считаем `PfWindowOffsetOf` — суммой по цепочке родителей, тем же способом, что и базу:
	//   строки списков сейчас однородны (`Find_Party_MList0N_grp`), но однородность иерархии
	//   импортёр не гарантирует (закон `jui-uneven-node-layout`), и на картах она уже сломалась.
	const FVector2D RowPos = PfWindowOffsetOf(Plates[Row]);
	// ★ 25.10.2026 [ИСПРАВЛЕНИЕ] (`asda.UI.BaseShiftKeepsDrag`): вычесть сдвиг РОДИТЕЛЯ рамки с момента базы — перетаскивание
	//   окна двигает и строку, и группу рамки; без вычета рамка уезжала на двойной вектор.
	const FVector2D ParentShift = (AsdaUi::BaseShiftKeepsDrag() && Frame[0] && Frame[0]->GetParent())
		? PfWindowOffsetOf(Frame[0]->GetParent()) - ((Side == 0) ? MSelParentBase : PSelParentBase) : FVector2D::ZeroVector;
	const float DeltaX = (float)(RowPos.X - Base.X - ParentShift.X);
	const float DeltaY = (float)(RowPos.Y - Base.Y - ParentShift.Y);
	for (UWidget* W : Frame)
	{
		if (!W) { continue; }
		if (const FMargin* B = FrameBase.Find(W))
		{
			if (UCanvasPanelSlot* CS = AsdaUi::SlotOf(W))
			{
				// ★ ИСПРАВЛЕНИЕ (дедуп №2, В2-B, U15): сдвиг от базы по правилу анкоров (было `Left/Top += d`).
				CS->SetOffsets(AsdaUi::ShiftedOffsets(*B, CS->GetAnchors(), FVector2D(DeltaX, DeltaY)));
			}
		}
		W->SetVisibility(ESlateVisibility::HitTestInvisible);
	}
}

void UAsdaPartyFindSubsystem::TickRowHover()
{
	// ★ Клик по строке ловим ПО ЖИВОЙ ГЕОМЕТРИИ (`AsdaUi::IsCursorOverWidget`), а не по слоту:
	//   в `.JUI` колонки строки вложены цепочкой и координаты слотов считаются от разных начал
	//   (закон `jui-cell-chain-nesting`). Тем же способом подсвечиваем строку под курсором.
	auto Scan = [this](TArray<TObjectPtr<UWidget>>& Plates, TArray<TObjectPtr<UWidget>>& Cursors,
	                   int32& Hover, int32 Side)
	{
		int32 Over = -1;
		for (int32 i = 0; i < Plates.Num(); ++i)
		{
			if (Plates[i] && AsdaUi::IsCursorOverWidget(Plates[i])) { Over = i; break; }
		}
		if (Over != Hover)
		{
			if (Cursors.IsValidIndex(Hover) && Cursors[Hover]) { Cursors[Hover]->SetVisibility(ESlateVisibility::Collapsed); }
			if (Cursors.IsValidIndex(Over) && Cursors[Over])   { Cursors[Over]->SetVisibility(ESlateVisibility::HitTestInvisible); }
			Hover = Over;
		}
		if (Mouse.bLmbDown && Over >= 0) { SelectRow(Side, Over); }
	};
	Scan(MRowPlate, MRowCursor, HoverRowM, 0);
	Scan(PRowPlate, PRowCursor, HoverRowP, 1);
}

void UAsdaPartyFindSubsystem::ApplyMapHighlight(int32 WireCode)
{
	// Порт `uiFindParty_ShowTooltipAt@0xDBB330` (мисномер: это НЕ подсказка, а перенос ЗЕЛЁНОЙ
	//   РАМКИ `Find_Party_MapSel` 8694 на выбранную кнопку карты).
	if (MapSelPlanes.Num() == 0) { return; }
	auto HideAll = [this]()
	{
		for (UWidget* W : MapSelPlanes) { if (W) { W->SetVisibility(ESlateVisibility::Collapsed); } }
	};
	if (WireCode < 0) { HideAll(); return; }
	const int32 UiIdx = PfRemapReverse(WireCode);
	int32 IdNum = -1;
	for (const FPfMap& M : kMaps) { if (M.UiIndex == UiIdx) { IdNum = M.IdNum; break; } }
	TObjectPtr<UWidget>* Node = (IdNum >= 0) ? MapNodes.Find(IdNum) : nullptr;
	if (!Node || !*Node) { HideAll(); return; }
	// ★★★ ИСПРАВЛЕН ДЕФЕКТ ПОРТА (2026-08-30): координату кнопки карты берём суммой отступов до
	//   корня окна, а НЕ отступом её собственного слота. Семь кнопок завёрнуты в под-канвас
	//   `Find_Party_Map<XX>_grp` и имеют внутри него отступ (0,0) — подробности у `PfWindowOffsetOf`.
	const FVector2D MapPos = PfWindowOffsetOf(*Node);
	// ★ 25.10.2026 [ИСПРАВЛЕНИЕ]: сдвиг родителя рамки с момента базы вычитается — как у рамок строк (`SelectRow`).
	const FVector2D ParentShift = (AsdaUi::BaseShiftKeepsDrag() && MapSelPlanes[0] && MapSelPlanes[0]->GetParent())
		? PfWindowOffsetOf(MapSelPlanes[0]->GetParent()) - MapSelParentBase : FVector2D::ZeroVector;
	const float DeltaX = (float)(MapPos.X - MapSelBase.X - ParentShift.X);
	const float DeltaY = (float)(MapPos.Y - MapSelBase.Y - ParentShift.Y);
	for (UWidget* W : MapSelPlanes)
	{
		if (!W) { continue; }
		if (const FMargin* B = FrameBase.Find(W))
		{
			if (UCanvasPanelSlot* CS = AsdaUi::SlotOf(W))
			{
				// ★ ИСПРАВЛЕНИЕ (дедуп №2, В2-B, U15): сдвиг от базы по правилу анкоров (было `Left/Top += d`).
				CS->SetOffsets(AsdaUi::ShiftedOffsets(*B, CS->GetAnchors(), FVector2D(DeltaX, DeltaY)));
			}
		}
		W->SetVisibility(ESlateVisibility::HitTestInvisible);
	}
}

// ═══════════════════════════════ ТЕКСТ И НОТИСЫ ═══════════════════════════════════════════════

FString UAsdaPartyFindSubsystem::Loc(int32 LocId) const
{
	// ★ Волна 1 (14.09.2026, В2): общая точка `AsdaNotice::Loc` — теги срезаны, маркеры как в таблице (как было).
	return AsdaNotice::Loc(GetGameInstance(), LocId);
}

void UAsdaPartyFindSubsystem::ShowFirstOpenNotice(const FString& SelfName, bool bAsLeader)
{
	// ★★★ НАШЕ ДОПОЛНЕНИЕ СВЕРХ ЭТАЛОНА (распоряжение владельца 2026-08-30). В оригинале такого
	//   сообщения нет: во ВСЁМ теле подсистемы (0xDB6120…0xDBF3E0) единственные тексты открытия —
	//   заголовки 11821/11825, а модальный `ONEBUT_MSGBOX` 1500 оттуда не зовётся ни разу.
	//
	//   ПОЧЕМУ ЗДЕСЬ, А НЕ В НАЧАЛЕ `OpenWindow`. Показываем ТОЛЬКО тому, кто действительно попал
	//   в список: одиночке (ушёл 3410) и главе отряда (ушёл 3418). Рядовой член отряда окно
	//   открывает, но никуда не регистрируется (эталонная ветка `bInParty && !bLeader` шлёт лишь
	//   3414/3422) — ему сообщение было бы ВРАНЬЁМ.
	//
	//   ФЛАГ СЕССИОННЫЙ И ПРИВЯЗАН К ПЕРСОНАЖУ, а не к процессу: храним ИМЯ персонажа, которому
	//   уже показали. На аккаунте бывает несколько персонажей в игре одновременно, и у каждого
	//   свой клиент; но и смена персонажа без перезапуска процесса не собьёт подсказку — имя
	//   станет другим и сообщение покажется заново. Ни в базу, ни в конфиг ничего не пишется:
	//   перезашёл — увидишь снова (ровно так просил владелец).
	//   ⚠ Признак — ОТДЕЛЬНЫЙ флаг плюс имя, а не «имя не пусто». Имя персонажа может быть
	//   ещё не получено (пустая строка): проверка «имя не пусто» в этом случае НИКОГДА не
	//   защёлкивалась бы и подсказка выскакивала при каждом открытии.
	if (bFirstOpenNoticeShown && NoticeShownForChar == SelfName)
	{
		return;
	}
	bFirstOpenNoticeShown = true;
	NoticeShownForChar = SelfName;

	UGameInstance* GI = GetGameInstance();
	// Штатное ОДНОКНОПОЧНОЕ игровое окно `ONEBUT_MSGBOX` 1500 с подписью кнопки loc 2288 «ОК» —
	//   то же, которым порт показывает прочие уведомления. Своего окна не рисуем.
	const int32 LocId = bAsLeader ? PfLoc::kFirstOpenLeader : PfLoc::kFirstOpenSingle;
	const FString Text = Loc(LocId);
	if (Text.IsEmpty())
	{
		UE_LOG(LogAsdaPartyFind, Warning,
			TEXT("[поиск отряда] строка %d пуста — подсказка первого открытия не показана"), LocId);
		return;
	}
	// ★ ИСПРАВЛЕНИЕ 11.10.2026 (дедуп №2, В2-A, U2): было `ShowGameMessage(Text, 2288)` с литералом кнопки и «нет оконной подсистемы
	//   ⇒ молча» — стало общая политика `AsdaNotice::MsgBox` (окна нет ⇒ лента ItemRefused, умолчательный канал прежнего
	//   вызова); подпись кнопки — `AsdaLoc::kBtnOk` (= 2288).
	AsdaNotice::MsgBox(GI, EAsdaMsgCategory::ItemRefused, Text, AsdaLoc::kBtnOk);
	UE_LOG(LogAsdaPartyFind, Log,
		TEXT("[поиск отряда] первое за сессию открытие персонажем «%s»: показана подсказка %d (%s)"),
		*SelfName, LocId, bAsLeader ? TEXT("глава отряда") : TEXT("одиночка"));
}

void UAsdaPartyFindSubsystem::NoticeLoc(int32 LocId)
{
	// Эталон: `CChat_AddTokenizedLines(g_pChatWnd, текст, -11514, 10000, 0)` — ЧАТ.
	//   В порту канал выбирает `DT_MessageRoutes` по категории; для окна отряда это `Social`.
	// ★ Волна 1 (14.09.2026, В2): маршрут — фасад `AsdaNotice` (категория та же, поведение то же).
	if (AsdaNotice::Chat(GetGameInstance(), EAsdaMsgCategory::Social, LocId)) { return; }
	UE_LOG(LogAsdaPartyFind, Warning, TEXT("[поиск отряда] нотис %d не выдан: подсистема сообщений не поднята"), LocId);
}

// ═══════════════════════════════ ДИАГНОСТИКА ══════════════════════════════════════════════════

void UAsdaPartyFindSubsystem::SimulateClick(int32 IdNum)
{
	// ★ Жмём САМУ кнопку виджета — второго пути обработки ради теста не заводим.
	if (!EnsureWindow()) { return; }
	if (TObjectPtr<UButton>* B = Buttons.Find(IdNum))
	{
		if (*B)
		{
			UE_LOG(LogAsdaPartyFind, Log, TEXT("[поиск отряда] ДИАГНОСТИКА: жмём кнопку %d"), IdNum);
			(*B)->OnClicked.Broadcast();
			return;
		}
	}
	if (TObjectPtr<UWidget>* W = MapNodes.Find(IdNum))
	{
		// ★ Карты — `UCheckBox` (см. `SetupControls`), у них другой делегат. Жмём именно его:
		//   это тот же путь, которым идёт настоящий клик игрока.
		if (UCheckBox* Chk = Cast<UCheckBox>(W->Get()))
		{
			const bool bNew = !Chk->IsChecked();
			UE_LOG(LogAsdaPartyFind, Log,
				TEXT("[поиск отряда] ДИАГНОСТИКА: жмём карту %d (состояние %d → %d)"),
				IdNum, Chk->IsChecked() ? 1 : 0, bNew ? 1 : 0);
			Chk->SetIsChecked(bNew);
			Chk->OnCheckStateChanged.Broadcast(bNew);
			return;
		}
		if (UButton* Btn = Cast<UButton>(W->Get()))
		{
			UE_LOG(LogAsdaPartyFind, Log, TEXT("[поиск отряда] ДИАГНОСТИКА: жмём карту %d"), IdNum);
			Btn->OnClicked.Broadcast();
			return;
		}
	}
	UE_LOG(LogAsdaPartyFind, Warning, TEXT("[поиск отряда] ДИАГНОСТИКА: кнопки %d в окне нет"), IdNum);
}

void UAsdaPartyFindSubsystem::SimulateComboPick(int32 IdNum, int32 Index)
{
	// Выбор пункта ТЕМ ЖЕ путём, что и мышью: ставим индекс живому виджету и дёргаем тот самый
	//   обработчик, что висит на `OnPicked`. Читаем результат ОБРАТНО с виджета, а не из своей
	//   переменной (требование «состояние по виджетам»).
	if (!EnsureWindow()) { return; }
	UAsdaJuiComboBox* C = nullptr;
	switch (IdNum)
	{
	case kIdMJobSel:  C = CmbJob;      break;
	case kIdMLvSel:   C = CmbLevel;    break;
	case kIdPChSel:   C = CmbChan;     break;
	case kIdPDesSel:  C = CmbGoal;     break;
	case kIdEditNum:  C = CmbEditNum;  break;
	case kIdEditDes:  C = CmbEditDes;  break;
	default: break;
	}
	if (!C)
	{
		UE_LOG(LogAsdaPartyFind, Warning, TEXT("[поиск отряда] ДИАГНОСТИКА: комбо %d в окне нет"), IdNum);
		return;
	}
	C->SetSelectedIndex(Index);
	OnComboChanged(IdNum);
	UE_LOG(LogAsdaPartyFind, Warning,
		TEXT("[поиск отряда] ДИАГНОСТИКА: комбо %d → пункт %d, виджет отдаёт %d; ")
		TEXT("взведено: левая стр %d, правая стр %d"),
		IdNum, Index, C->GetSelectedIndex(), MReqPage, PReqPage);
}

void UAsdaPartyFindSubsystem::SimulateRowClick(int32 Side, int32 Row)
{
	if (!EnsureWindow()) { return; }
	SelectRow(Side, Row);
	UE_LOG(LogAsdaPartyFind, Log, TEXT("[поиск отряда] ДИАГНОСТИКА: выбрана строка %d списка %s"),
		Row, Side == 0 ? TEXT("одиночек") : TEXT("отрядов"));
}

FString UAsdaPartyFindSubsystem::DumpState() const
{
	// Читаем состояние ВИДЖЕТОВ, а не свои переменные (требование брифинга).
	FString S;
	S += FString::Printf(TEXT("[поиск отряда] окно=%s разобрано=%d открыто=%d полностьюЗакрыто=%d ")
		TEXT("таймерСвёрнутого=%d\n"),
		Win ? TEXT("есть") : TEXT("НЕТ"), bReady ? 1 : 0, bOpen ? 1 : 0, bClosedFully ? 1 : 0,
		bMinimizedTimer ? 1 : 0);
	S += FString::Printf(TEXT("  мойНомерОдиночки=%d мойОтряд=%d маска=%08X/%08X весьРегион=%d\n"),
		MemberRegId, LeaderRegId, MapMaskA, MapMaskB, bWholeRegion ? 1 : 0);
	S += FString::Printf(TEXT("  левый: стр %d/%d выбор=%d | правый: стр %d/%d выбор=%d\n"),
		MCurPage, MTotalPages, MSelIdx, PCurPage, PTotalPages, PSelIdx);
	if (Win)
	{
		S += FString::Printf(TEXT("  видимость окна по виджету: %s\n"),
			*UEnum::GetValueAsString(Win->GetVisibility()));
	}
	for (int32 i = 0; i < 10; ++i)
	{
		const FString L = MRowName.IsValidIndex(i) && MRowName[i] ? MRowName[i]->GetText().ToString() : TEXT("<нет узла>");
		const FString R = PRowLeader.IsValidIndex(i) && PRowLeader[i] ? PRowLeader[i]->GetText().ToString() : TEXT("<нет узла>");
		S += FString::Printf(TEXT("   стр%02d  одиночка «%s»  |  отряд «%s»\n"), i, *L, *R);
	}
	S += FString::Printf(TEXT("  кнопок привязано %d, карт %d\n"), Buttons.Num(), MapNodes.Num());
	return S;
}

FString UAsdaPartyFindSubsystem::DumpMapHighlight() const
{
	// ★★★ ДОКАЗАТЕЛЬСТВО ПОЛОЖЕНИЯ РАМКИ — ПО ЖИВОЙ ГЕОМЕТРИИ. Слоты ассета здесь врут:
	//   у семи кнопок карт отступ собственного слота (0,0), потому что импортёр завернул их в
	//   под-канвас `Find_Party_Map<XX>_grp` (закон `jui-uneven-node-layout`). Поэтому сверяем
	//   ЭКРАННЫЕ прямоугольники: рамка обязана накрывать плитку, а не висеть в углу.
	FString S = TEXT("[поиск отряда] ЖИВАЯ ГЕОМЕТРИЯ рамки карт:\n");
	if (MapSelPlanes.Num() == 0) { return S + TEXT("  планов рамки НЕТ\n"); }

	// Левый-верхний угол рамки — план 0 скина `Box_Green3Strok` (уголок 3×3).
	const UWidget* Corner = MapSelPlanes[0];
	const FGeometry FrameGeo = Corner ? Corner->GetCachedGeometry() : FGeometry();
	const FVector2D FramePos = FrameGeo.GetAbsolutePosition();
	const FVector2D FrameSz  = FrameGeo.GetAbsoluteSize();
	S += FString::Printf(TEXT("  рамка (план 0 «%s»): экран (%.0f, %.0f) размер %.0f×%.0f, видимость %s\n"),
		Corner ? *Corner->GetName() : TEXT("?"), FramePos.X, FramePos.Y, FrameSz.X, FrameSz.Y,
		Corner ? *UEnum::GetValueAsString(Corner->GetVisibility()) : TEXT("?"));

	// Каждая карта: слот из ассета (может врать) + живой прямоугольник + вердикт «рамка на мне?».
	for (const TPair<int32, TObjectPtr<UWidget>>& It : MapNodes)
	{
		const UWidget* W = It.Value.Get();
		if (!W) { continue; }
		const FGeometry G = W->GetCachedGeometry();
		const FVector2D P = G.GetAbsolutePosition();
		const FVector2D Z = G.GetAbsoluteSize();
		const FVector2D Slot = PfWindowOffsetOf(W);
		const UCanvasPanelSlot* CS = Cast<UCanvasPanelSlot>(W->Slot);
		const FMargin Own = CS ? CS->GetOffsets() : FMargin();
		// «Рамка на мне» = её левый-верхний уголок стоит ВПЛОТНУЮ к левому-верхнему углу плитки
		//   (в разметке рамка на 1 px выше и левее кнопки: Origin «28 −1» против «29 0»).
		//   Допуск 4 точки при шаге сетки 148×18 попадает ровно в одну плитку и ни в какую больше;
		//   прежняя проверка «уголок внутри прямоугольника» ловила сразу несколько соседей.
		const bool bOn = FrameSz.X > 0.f
			&& FMath::Abs(FramePos.X - P.X) <= 4.f
			&& FMath::Abs(FramePos.Y - P.Y) <= 4.f;
		S += FString::Printf(
			TEXT("   карта %d «%s»: свойСлот (%.0f, %.0f) → вОкне (%.0f, %.0f) | экран (%.0f, %.0f) ")
			TEXT("размер %.0f×%.0f%s\n"),
			It.Key, *W->GetName(), Own.Left, Own.Top, Slot.X, Slot.Y, P.X, P.Y, Z.X, Z.Y,
			bOn ? TEXT("  ← РАМКА ЗДЕСЬ") : TEXT(""));
	}
	return S;
}

// ═══════════════════════════════ КОНСОЛЬНЫЕ КОМАНДЫ ═══════════════════════════════════════════

namespace
{
	UAsdaPartyFindSubsystem* PfSys(UWorld* W)
	{
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		return GI ? GI->GetSubsystem<UAsdaPartyFindSubsystem>() : nullptr;
	}

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
	FAutoConsoleCommandWithWorldAndArgs GPfDump(
		TEXT("asda.PartyFind.Dump"),
		TEXT("Состояние окна поиска отряда (8500) по ЖИВЫМ виджетам."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
			[](const TArray<FString>&, UWorld* W)
			{
				if (UAsdaPartyFindSubsystem* S = PfSys(W))
				{
					UE_LOG(LogAsdaPartyFind, Warning, TEXT("%s"), *S->DumpState());
				}
			}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
	FAutoConsoleCommandWithWorldAndArgs GPfToggle(
		TEXT("asda.PartyFind.Toggle"),
		TEXT("Переключить окно поиска отряда (тот же путь, что у пункта доп-меню, команда 7)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
			[](const TArray<FString>&, UWorld* W)
			{
				if (UAsdaPartyFindSubsystem* S = PfSys(W)) { S->ToggleWindow(); }
			}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
	FAutoConsoleCommandWithWorldAndArgs GPfClick(
		TEXT("asda.PartyFind.Click"),
		TEXT("asda.PartyFind.Click <IDNum> — нажать САМУ кнопку окна 8500 (8509, 8654, 8653, 8664…)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
			[](const TArray<FString>& Args, UWorld* W)
			{
				if (Args.Num() < 1) { return; }
				if (UAsdaPartyFindSubsystem* S = PfSys(W)) { S->SimulateClick(FCString::Atoi(*Args[0])); }
			}));
#endif // ASDA_DEV_CONSOLE

	// ★★★ САМОТЕСТ ПУТЁМ ИГРОКА (2026-08-30) — под жалобы владельца на ВИД плитки карт.
	//   Устроен как `asda.Title.SelfTest`: шаги разнесены таймерами, потому что `-ExecCmds`
	//   выполняет все команды на нулевом кадре, а живая геометрия готова только после отрисовки
	//   (закон `ui-check-game-path-geometry`). Кадры именованные — безымянный `shot` пишет
	//   `ScreenShotNNNNN.png` в общую папку, и параллельный прогон чужого агента их перетирает.
	//   Запуск (окон Unreal на экране не появляется, `-NullRHI` НЕЛЬЗЯ — Slate не будет рисовать):
	//     UnrealEditor-Cmd.exe <uproject> "/Game/Map/alpen/alpen?game=…" -game -RenderOffscreen
	//       -ExecCmds="asda.PartyFind.SelfTest shot quit"
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
	FAutoConsoleCommandWithWorldAndArgs GPfSelfTest(
		TEXT("asda.PartyFind.SelfTest"),
		TEXT("Поиск отряда (8500): открыть окно и обойти карты 8664 → 8670 → 8688, снимая ЖИВУЮ ")
		TEXT("геометрию зелёной рамки. `shot` — кадр на каждом шаге, `quit` — выход после прогона."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
			[](const TArray<FString>& Args, UWorld* W)
			{
				// ★ С-Р1 (26.09.2026), офлайн-гейт: самотест открывает окно и жмёт карты — при живом соединении персонаж встал бы в серверный список (3410/3418) и сменил маску карт (3416).
				static const uint16 kGuardOps[] = { 3410, 3414, 3416, 3418, 3422 };
				const FAsdaSelfTestArgs KitArgs = FAsdaSelfTestArgs::Parse(Args);   // ★ 11.10.2026 (В3 S1): каркас — отказ с ИТОГ и quit, второй барьер на все опкоды
				if (AsdaSelfTestKit::RefuseStart(W, TEXT("asda.PartyFind.SelfTest"), KitArgs, 0.f, MakeArrayView(kGuardOps))) { return; }
				UAsdaPartyFindSubsystem* S = PfSys(W);
				if (!S || !W) { return; }
				bool bQuit = false, bShot = false;
				for (const FString& A : Args)
				{
					if (A.Equals(TEXT("quit"), ESearchCase::IgnoreCase)) { bQuit = true; }
					if (A.Equals(TEXT("shot"), ESearchCase::IgnoreCase)) { bShot = true; }
				}
				// ★ СРЕДН-1 (ревизия shop_offline_fix, 10.10.2026): офлайн повторно — на отложенном старте — общий флаг
				//   всех таймеров прогона (проверку делает первый сработавший, при отказе пропускаются все, кроме выхода).
				TSharedRef<FAsdaSelfTestRunGate> Gate = MakeShared<FAsdaSelfTestRunGate>(TEXT("asda.PartyFind.SelfTest"), bQuit);
				// Шаг «открыть». Печатаем РЕШЕНИЕ: «не открылось» и «открылось пустым» — разные исходы.
				FTimerHandle H0;
				W->GetTimerManager().SetTimer(H0, FTimerDelegate::CreateWeakLambda(S, [S, Gate]()
				{
					if (!Gate->Pass(S)) { return; }
					S->ToggleWindow();
					UE_LOG(LogAsdaPartyFind, Warning, TEXT("САМОТЕСТ: открытие → %s\n%s"),
						S->IsWindowOpen() ? TEXT("ОК") : TEXT("ОТКАЗ"), *S->DumpState());
				}), 2.0f, false);

				// ★ КАДР ПЕРВОГО ОТКРЫТИЯ — с модальной подсказкой поверх окна (наше сверх эталона).
				//   Отдельным таймером: модалка строится в том же кадре, что и открытие, а `shot`
				//   пишет файл в КОНЦЕ кадра — без паузы снимок застанет состояние до отрисовки.
				if (bShot)
				{
					FTimerHandle HF;
					W->GetTimerManager().SetTimer(HF, FTimerDelegate::CreateWeakLambda(S, [S, Gate]()
					{
						if (!Gate->Pass(S)) { return; }
						UE_LOG(LogAsdaPartyFind, Warning,
							TEXT("САМОТЕСТ: КАДР → asda_partyfind_firstopen.png"));
						FScreenshotRequest::RequestScreenshot(TEXT("asda_partyfind_firstopen"),
							/*bShowUI=*/true, /*bAddFilenameSuffix=*/false);
					}), 2.6f, false);
				}

				// ★ Карты выбраны НЕ случайно: 8664 «Альпен» лежит ПРЯМО в канвасе плитки,
				//   а 8670 «Флавис» и 8688 «Логово дракона» завёрнуты в под-канвас `*_grp`
				//   с отступом (0,0) — именно у них рамка уезжала в левый-верхний угол.
				const int32 Maps[3] = { 8664, 8670, 8688 };
				for (int32 I = 0; I < 3; ++I)
				{
					const int32 Id = Maps[I];
					FTimerHandle H;
					W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateWeakLambda(S, [S, Id, Gate]()
					{
						if (!Gate->Pass(S)) { return; }
						S->SimulateClick(Id);
					}), 3.0f + 2.0f * (float)I, false);

					// ★ ГЕОМЕТРИЮ СНИМАЕМ ОТДЕЛЬНЫМ ТАЙМЕРОМ, а не сразу после клика:
					//   `GetCachedGeometry` отдаёт прямоугольник ПРОШЛОЙ отрисовки, и дамп в кадре
					//   клика показывает положение рамки ДО переноса (первый прогон 2026-08-30
					//   именно так отставал на шаг и чуть не «доказал» обратное).
					FTimerHandle HG;
					W->GetTimerManager().SetTimer(HG, FTimerDelegate::CreateWeakLambda(S, [S, Id, Gate]()
					{
						if (!Gate->Pass(S)) { return; }
						UE_LOG(LogAsdaPartyFind, Warning, TEXT("САМОТЕСТ: после клика по карте %d\n%s"),
							Id, *S->DumpMapHighlight());
					}), 4.0f + 2.0f * (float)I, false);

					if (bShot)
					{
						FTimerHandle HS;
						W->GetTimerManager().SetTimer(HS, FTimerDelegate::CreateWeakLambda(S, [S, Id, Gate]()
						{
							if (!Gate->Pass(S)) { return; }
							const FString Name = FString::Printf(TEXT("asda_partyfind_map%d"), Id);
							UE_LOG(LogAsdaPartyFind, Warning, TEXT("САМОТЕСТ: КАДР → %s.png"), *Name);
							FScreenshotRequest::RequestScreenshot(Name, /*bShowUI=*/true,
								/*bAddFilenameSuffix=*/false);
						}), 4.3f + 2.0f * (float)I, false);
					}
				}
				if (bQuit)
				{
					FTimerHandle HQ;
					W->GetTimerManager().SetTimer(HQ, FTimerDelegate::CreateWeakLambda(S, [S]()
					{
						// ★ 31.10.2026 (починка пакетного прогона): прогон с выходом за собой закрывает окно 8500 штатным
						//   `CloseWindow` и снимает очередь окон-сообщений. Раньше окно и его подсказка оставались поверх —
						//   следующие самотесты пакета кликали в них (`asda.SelfTest.BoosterView` 10/18,
						//   `asda.Soulmate.NoteSelfTest` 42/7 после этого теста; по отдельности оба зелёные).
						if (S->IsWindowOpen()) { S->CloseWindow(); }
						if (UAsdaWindowSubsystem* WM = S->GetGameInstance() ? S->GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
						{
							WM->ClearMsgBoxQueue(TEXT("asda.PartyFind.SelfTest, итог"));
						}
						UE_LOG(LogAsdaPartyFind, Warning, TEXT("САМОТЕСТ: завершён (окно 8500 закрыто)"));
						if (GEngine) { GEngine->DeferredCommands.Add(TEXT("quit")); }
					}), 11.0f, false);
				}
			}));
#endif // ASDA_DEV_CONSOLE

	// ★★★ СПЛОШНОЙ ОБХОД ВСЕХ КОНТРОЛОВ ОКНА (по требованию владельца 2026-08-30: «проверь, что
	//   каждая кнопка реально работает»). Каждый шаг печатает МАРКЕР, между маркерами в логе
	//   ложатся строки `LogAsdaNet: →NNNN …` — так видно, какой пакет ушёл от какой кнопки, и
	//   видно, если не ушёл НИ ОДИН. Состояние окна печатается тем же `DumpState`.
	//   ⚠ Живого сервера нет: доказывается ОТПРАВКА и СОСТОЯНИЕ, а не ответ. Где без ответа
	//   проверить нельзя — так и помечено в отчёте.
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
	FAutoConsoleCommandWithWorldAndArgs GPfSweep(
		TEXT("asda.PartyFind.ButtonSweep"),
		TEXT("Поиск отряда: нажать ПО ОЧЕРЕДИ все кнопки/комбо/карты окна 8500 и напечатать, что ")
		TEXT("ушло в сеть и как изменилось состояние. Аргумент `quit` — выйти после обхода."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
			[](const TArray<FString>& Args, UWorld* W)
			{
				// ★ С-Р1 (26.09.2026), офлайн-гейт: прогон жмёт все 19 контролов — при живом соединении регистрация, смена маски, 3424 от не-лидера, шёпот и бесконечный опрос 3414/3422.
				static const uint16 kGuardOps[] = { 3410, 3412, 3414, 3416, 3418, 3420, 3422, 3424, 5088 };
				if (FAsdaOutgoingGate::RefuseDebugWhenOnline(W, TEXT("asda.PartyFind.ButtonSweep"), 50.f, MakeArrayView(kGuardOps))) { return; }
				UAsdaPartyFindSubsystem* S = PfSys(W);
				if (!S || !W) { return; }
				bool bQuit = false;
				for (const FString& A : Args)
				{
					if (A.Equals(TEXT("quit"), ESearchCase::IgnoreCase)) { bQuit = true; }
				}
				// Шаги: (номер контрола, человекочитаемое имя, что ДОЛЖНО произойти).
				struct FStep { int32 Id; const TCHAR* Name; const TCHAR* Must; };
				static const FStep kSteps[] = {
					{ 8509, TEXT("«Поиск» левого списка"),      TEXT("3414, страница 1, с фильтрами проф/уровень") },
					{ 8583, TEXT("«Поиск» правого списка"),     TEXT("3422, страница 1, с фильтрами канал/цель") },
					{ 8573, TEXT("след. страница левого"),      TEXT("3414 со следующей страницей") },
					{ 8575, TEXT("пред. страница левого"),      TEXT("3414 с предыдущей") },
					{ 8574, TEXT("последняя страница левого"),  TEXT("3414 с последней") },
					{ 8576, TEXT("первая страница левого"),     TEXT("3414 со страницей 1") },
					{ 8647, TEXT("след. страница правого"),     TEXT("3422 со следующей") },
					{ 8649, TEXT("пред. страница правого"),     TEXT("3422 с предыдущей") },
					{ 8648, TEXT("последняя страница правого"), TEXT("3422 с последней") },
					{ 8650, TEXT("первая страница правого"),    TEXT("3422 со страницей 1") },
					{ 8664, TEXT("плитка «Альпия» 8664"),       TEXT("бит маски, 3416 + 3414, нотис в чат") },
					{ 8674, TEXT("плитка «Дворец Королевы»"),   TEXT("ещё бит маски, 3416 + 3414") },
					{ 8502, TEXT("«По всему миру» 8502"),       TEXT("снять галки карт, 3416 + 3424") },
					{ 8654, TEXT("«Обновить» 8654"),            TEXT("3414 И 3422 подряд") },
					{ 8655, TEXT("«Шепот» 8655"),               TEXT("без выбора строки — снять выделение, пакета нет") },
					{ 8651, TEXT("«Изменить» 8651"),            TEXT("панель правки ТОЛЬКО лидеру; иначе молча") },
					{ 8662, TEXT("«Применить» правки 8662"),    TEXT("3424 + нотис 11908") },
					{ 8663, TEXT("«Отмена» правки 8663"),       TEXT("спрятать панель, пакета нет") },
					{ 8652, TEXT("«Свернуть» 8652"),            TEXT("окно скрыть, из списков НЕ уходить, нотис 11906") },
				};
				float T = 2.0f;
				// ★ СРЕДН-1 (ревизия shop_offline_fix, 10.10.2026): офлайн повторно — на отложенном старте — общий флаг
				//   всех таймеров обхода (проверку делает первый сработавший, при отказе пропускаются все, кроме выхода).
				TSharedRef<FAsdaSelfTestRunGate> Gate = MakeShared<FAsdaSelfTestRunGate>(TEXT("asda.PartyFind.ButtonSweep"), bQuit);
				// Открытие.
				FTimerHandle H0;
				W->GetTimerManager().SetTimer(H0, FTimerDelegate::CreateWeakLambda(S, [S, Gate]()
				{
					if (!Gate->Pass(S)) { return; }
					S->ToggleWindow();
					UE_LOG(LogAsdaPartyFind, Warning,
						TEXT("=== ОБХОД: окно открыто, дальше по одному контролу ==="));
				}), T, false);
				T += 1.0f;
				// Комбобоксы — отдельным шагом ПЕРЕД «Поиском», чтобы в теле 3414 было видно фильтр.
				FTimerHandle HC;
				W->GetTimerManager().SetTimer(HC, FTimerDelegate::CreateWeakLambda(S, [S, Gate]()
				{
					if (!Gate->Pass(S)) { return; }
					UE_LOG(LogAsdaPartyFind, Warning,
						TEXT("--- ШАГ: комбо «Профессия» 8506 → пункт 3, «Уровень» 8508 → пункт 6, ")
						TEXT("«Канал» 8580 → пункт 2, «Цель» 8582 → пункт 5 (ДОЛЖНО: только взвести ")
						TEXT("страницу 1, пакета быть НЕ должно) ---"));
					S->SimulateComboPick(8506, 3);
					S->SimulateComboPick(8508, 6);
					S->SimulateComboPick(8580, 2);
					S->SimulateComboPick(8582, 5);
				}), T, false);
				T += 1.0f;
				for (const FStep& St : kSteps)
				{
					const int32 Id = St.Id; const TCHAR* Nm = St.Name; const TCHAR* Ms = St.Must;
					FTimerHandle H;
					W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateWeakLambda(S, [S, Id, Nm, Ms, Gate]()
					{
						if (!Gate->Pass(S)) { return; }
						UE_LOG(LogAsdaPartyFind, Warning,
							TEXT("--- ШАГ %d %s (ДОЛЖНО: %s) ---"), Id, Nm, Ms);
						S->SimulateClick(Id);
					}), T, false);
					T += 0.6f;
				}
				// Итоговое состояние.
				FTimerHandle HD;
				W->GetTimerManager().SetTimer(HD, FTimerDelegate::CreateWeakLambda(S, [S, Gate]()
				{
					if (!Gate->Pass(S)) { return; }
					UE_LOG(LogAsdaPartyFind, Warning, TEXT("=== ОБХОД ЗАВЕРШЁН ==="));
					UE_LOG(LogAsdaPartyFind, Warning, TEXT("%s"), *S->DumpState());
				}), T, false);
				if (bQuit)
				{
					FTimerHandle HQ;
					W->GetTimerManager().SetTimer(HQ, FTimerDelegate::CreateWeakLambda(S, []()
					{
						if (GEngine) { GEngine->DeferredCommands.Add(TEXT("quit")); }
					}), T + 2.0f, false);
				}
			}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
	FAutoConsoleCommandWithWorldAndArgs GPfRow(
		TEXT("asda.PartyFind.Row"),
		TEXT("asda.PartyFind.Row <0|1> <строка> — выбрать строку списка (0 = одиночки, 1 = отряды)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
			[](const TArray<FString>& Args, UWorld* W)
			{
				if (Args.Num() < 2) { return; }
				if (UAsdaPartyFindSubsystem* S = PfSys(W))
				{
					S->SimulateRowClick(FCString::Atoi(*Args[0]), FCString::Atoi(*Args[1]));
				}
			}));
#endif // ASDA_DEV_CONSOLE
}
