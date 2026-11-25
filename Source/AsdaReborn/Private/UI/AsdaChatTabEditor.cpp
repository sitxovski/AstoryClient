// Copyright (c) 2026 @sitxovski (Discord)
// AsdaChatTabEditor.cpp — ЭТАП 4 подсистемы чата: ДИНАМИЧЕСКИЕ ВКЛАДКИ.
//
// ═══════════════════════════════ ИСТОЧНИК ИСТИНЫ ═══════════════════════════════════════════════
// Полный реверс сделан ЗАРАНЕЕ и здесь не переоткрывается:
//   `Reports/chat_tab_editor_2590_re.md` (агент 2026-08-06) — дерево окна 2590 (39 узлов,
//   21 значимый idnum), окно палитры 2700 (27 idnum), формат `ChatOpt.bin` байт в байт,
//   диалоги 1580 / 1520, все четыре команды меню 2670, лимиты и грабли оригинала.
// Ниже — ТОЛЬКО реализация по нему, каждое число со ссылкой.
//
// ══════════════════ ЧТО ЗДЕСЬ ЖИВЁТ ═════════════════════════════════════════════════════════════
//   • операции над вкладками: добавить / переименовать / удалить / сбросить к дефолту;
//   • редактор вкладок `CHAT_TAB_WIN` (2590) — СПИСОК ВКЛАДОК 2597 столбцом клон-ячеек, 8 строк
//     каналов (галочка + имя + образец цвета), скролл по 10 каналам, чекбокс «вернуть настройки»,
//     ОК/крестик;
//   • окно выбора цвета `CHAT_Color_Select_Win` (2700);
//   • персист `./ChatOpt.bin`.
//
// ═════════ ★★★ БУФЕРЫ (эталонная схема, воспроизведена 1:1) ════════════════════════════════════
//   ЖИВЫЕ данные   : `ChatTabs`     (эталон `tabRecordList`  @+0x550)
//                    `ChanOpts`     (эталон `tabFilterOpt`   @+0x36C, 10 × 24 б)
//   РАБОЧАЯ КОПИЯ  : `ChatTabsEdit` (эталон `tabRecordListMirror` @+0x560)
//                    `ChanOptsEdit` (эталон `tabFilterOptBackup`  @+0x45C)
//   Открытие 2590 = снимок live → edit (`uiRefreshSkillSlotList(this, 1)`).
//   ОК 2650 и КРЕСТИК 2651 — ОДИН И ТОТ ЖЕ обработчик (`case 0xA5A: case 0xA5B:`), оба КОММИТЯТ
//   edit → live и пишут файл. Отдельной «Отмены» у окна 2590 в оригинале НЕТ.
//
// ═════════ ★★★ ПОЧИНЕННЫЕ БАГИ ОРИГИНАЛА (решение юзера 2026-08-06: «ДА, чиним ошибки») ════════
//   ⚠ КАЖДЫЙ пункт — СОЗНАТЕЛЬНОЕ ОТСТУПЛЕНИЕ. Не «возвращать к эталону»!
//   [Б1] Прокрутка списка каналов (2639) в оригинале зовёт `uiRefreshSkillSlotList(this, 1)`
//        (ui_core.c:12396-12400), то есть ЗАНОВО снимает live → edit и СТИРАЕТ несохранённые
//        галочки и цвета. У нас прокрутка только перерисовывает строки (`RefreshTabEditor`),
//        рабочий буфер не трогается; коммит по-прежнему ТОЛЬКО по ОК/крестику.
//   [Б2] Выбор цвета в оригинале РАССИНХРОНИЗИРОВАН: «исходный цвет» читается из ЖИВОГО
//        `tabFilterOpt[ctrlId−2641]` БЕЗ учёта прокрутки (ui_core.c:12499), а ОК пишет в
//        `tabFilterOptBackup[stateFlags3 + stateFlags2]` СО скроллом (chat.c:2124). При
//        прокрученном списке показывался цвет одного канала, а менялся другой. У нас и чтение,
//        и запись идут по ОДНОМУ индексу `ChanIndexForRow(Row) = EditChanScroll + Row`.
//   [Б3] `sprintf(dst, userInput)` в переименовании (`chatMacro_setShortcutText`, chat.c:1940) —
//        ФОРМАТНАЯ СТРОКА ИЗ ПОЛЬЗОВАТЕЛЬСКОГО ВВОДА (имя вкладки с «%s» читало мусор из стека).
//        У нас обычное присваивание строки.
//   [Б4] `ChatOpt.bin` оригинал открывает в ТЕКСТОВОМ режиме (`fopen(...,"r"/"w")`, chat.c:1336/1390):
//        на Windows каждый 0x0A превращается в 0x0D0A, а первый 0x1A обрывает чтение — то есть
//        файл с определёнными цветами/масками читается битым. Пишем и читаем БИНАРНО.
//   [Б5] При загрузке оригинал делает `resize(tabCount)` БЕЗ КЛАМПА (chat.c:1349) — битый или
//        чужой файл кладёт клиент. У нас `tabCount` клампится, при несходстве файл отвергается.
//   [Б6] `chatTabRemoveSelected` зовёт `uiList_UpdateScrollArrow` только для полосы 2501 и забывает
//        про список 2597 (в add/load зовутся оба) — у нас после любой операции перерисовываются ОБА.
//   [Б7] Текст подсказки loc 5338 говорит «не более 5 символов», а код проверяет `strlen <= 10`
//        (5 корейских слогов CP949 = 10 байт). В CP1251 один символ = один байт, поэтому у нас
//        действует ЧИСЛО ИЗ КОДА — 10 символов, а в подсказке цифра приводится к правде.
#include "UI/AsdaChatWindowSubsystem.h"
#include "Login/AsdaLoginData.h"   // ★ общая точка cp1251 (Encode/DecodeCp1251)
#include "UI/AsdaUiHelpers.h"   // ★ Аудит №4 (B1): AsdaUi::SlotOf/SlotRect — общая точка слота канваса
#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaJuiSlot.h"
#include "UI/AsdaMarkup.h"
#include "Text/AsdaTextSubsystem.h"
#include "Resources/AsdaSoundSubsystem.h"
#include "Resources/AsdaTextureSubsystem.h"   // ★ Аудит №3 (A8/B8): атлас JUI — только через менеджер
#include "Login/AsdaButtonRelay.h"
#include "Login/AsdaGameText.h"     // ★ 10.10.2026 (en_completeness): FAsdaGameTextRow — русская строка вкладки по умолчанию
#include "AsdaDataTableCache.h"      // ★ 10.10.2026 (en_completeness): AsdaDt::RowByInt — та же общая точка таблиц
#include "Text/AsdaLang.h"           // ★ 10.10.2026 (en_completeness): EN/AR-строки вкладок по умолчанию (оверлеи)
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/PanelWidget.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"
#include "Components/CheckBox.h"
#include "Engine/GameInstance.h"
#include "Engine/Texture2D.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Serialization/BufferArchive.h"
#include "Framework/Application/SlateApplication.h"   // курсор для колеса над списками 2597/2639

DEFINE_LOG_CATEGORY_STATIC(LogAsdaChatTab, Log, All);

const FName UAsdaChatWindowSubsystem::kTabEditorId(TEXT("CHAT_TAB_WIN"));
const FName UAsdaChatWindowSubsystem::kColorSelectId(TEXT("CHAT_Color_Select_Win"));

namespace
{
	// Пути импортированных окон (генератор Tools/asda_jui_generic_umg.py).
	const TCHAR* kTabEditorWbp = TEXT("/Game/AsdaAssets/UI/Player_UI/Chat/WBP_CHAT_TAB_WIN");
	const TCHAR* kColorWbp     = TEXT("/Game/AsdaAssets/UI/Player_UI/Chat/WBP_CHAT_Color_Select_Win");

	// ★ Файл настроек. Эталон: `fopen("./ChatOpt.bin", ...)` — путь ОТНОСИТЕЛЬНО рабочего каталога
	//   процесса. В UE это `FPaths::ProjectSavedDir()`: писать рядом с .exe нельзя (нет прав в
	//   Program Files), а Saved/ — штатное место пользовательских настроек.
	FString ChatOptPath() { return FPaths::ProjectSavedDir() / TEXT("ChatOpt.bin"); }

	// Размеры записей эталона — держим ЯВНО, файл совместим по раскладке (отчёт §3.2/§3.3/§3.4).
	constexpr int32 kChanOptSize   = 24;   // char name[20] + DWORD colorARGB
	constexpr int32 kTabRecordSize = 22;   // char name[20] + WORD  channelMask
	constexpr int32 kChanCount     = 10;   // 10 каналов
	constexpr int32 kNameBytes     = 20;   // char name[20] в обеих записях
	// Кламп числа вкладок при чтении — [Б5]. В оригинале клампа нет вовсе; верхняя граница взята
	//   с запасом от практического потолка (ширина полосы вкладок при максимальной ширине окна).
	constexpr int32 kTabCountMax   = 64;

	// CP1251: каждый представимый символ — ровно один байт. Пишем/читаем имена как эталон — 20 байт.
	// ★ СВЕДЕНО (`fix_helpers_dedup.md`): пара была ещё одной копией общего кодека cp1251 (и как все
	//   прежние копии — не знала пунктуацию « » – — … №). Теперь через общую точку
	//   `FAsdaLoginData::Encode/DecodeCp1251`. Запись остаётся ровно 20 б с нулевым паддингом.
	void WriteFixedName(TArray<uint8>& Out, const FString& S)
	{
		uint8 Buf[kNameBytes] = { 0 };
		FAsdaLoginData::EncodeCp1251(S, Buf, kNameBytes - 1);   // ≤19 б, Buf[19] остаётся терминатором
		Out.Append(Buf, kNameBytes);
	}
	FString ReadFixedName(const uint8* P)
	{
		return FAsdaLoginData::DecodeCp1251(P, kNameBytes);
	}

	// ★ 10.10.2026 (en_terms_fix, п. 3 — «Party» в RU становился переводимой вкладкой): ПОМЕТКА ПРОИСХОЖДЕНИЯ ИМЕНИ
	//   ВКЛАДКИ в хвосте записи. Раскладка эталона не меняется: имя — C-строка в `char name[20]`, и оригинал, и наш
	//   декодер читают её до первого нуля, байты после нуля никто не читает. Если имя заняло ≤ 17 байт (лимит имени
	//   вкладки — 15 символов), в байты 18–19 пишется подпись: A5 5A — имя задал игрок, 5A A5 — подпись по умолчанию
	//   (loc-вкладка). Длиннее 17 байт (старые/усечённые имена) — без подписи, как раньше.
	constexpr int32 kMarkPos = kNameBytes - 2;                    // 18: первый байт подписи
	constexpr uint8 kMarkUser[2]    = { 0xA5, 0x5A };             // имя вкладки задал игрок
	constexpr uint8 kMarkDefault[2] = { 0x5A, 0xA5 };             // подпись вкладки по умолчанию (loc-id) без номера (запись 10.10 en_terms_fix)
	// ★ 10.10.2026 (terms_minors, МЕЛК-3 ревизии): пометка «по умолчанию» НЕСЁТ НОМЕР ВКЛАДКИ — второй байт
	//   = 0xA5 ^ k, где k = LocId − kTabLocBase (1…kTabLocSpan−1). k = 0 даёт прежнюю подпись `5A A5` (файлы, записанные
	//   до этой правки, — без номера, сверка по тексту как раньше). При чтении вкладка с номером получает LocId по номеру
	//   БЕЗ сверки текста: правка loc-строки вкладки (5411…) больше не превращает её в «свою» со старым текстом.
	//   Байты 0xA5 ^ k (k < 64) = 0x80…0xBF — не 0x0A/0x0D/0x1A, текстовый режим `fopen("r")` оригинала их не портит.
	constexpr int32 kTabLocBase = 5410;                           // LocId вкладки = kTabLocBase + k (эталон: 5411…5417)
	constexpr int32 kTabLocSpan = 64;                             // k < 64: старший бит второго байта всегда 1

	// Пометка имени в записи: 0 — нет (файл оригинала/старого порта или длинное имя), 1 — игрок, 2 — по умолчанию.
	enum class ETabNameMark : uint8 { None, User, Default };

	// Запись имени вкладки + пометка происхождения (если влезает после завершающего нуля). `LocId` — loc-id подписи
	//   вкладки по умолчанию (★ terms_minors: кладётся номером во второй байт пометки; вне 5411…5473 — без номера).
	void WriteTabName(TArray<uint8>& Out, const FString& S, ETabNameMark Mark, int32 LocId = 0)
	{
		uint8 Buf[kNameBytes] = { 0 };
		const int32 N = FAsdaLoginData::EncodeCp1251(S, Buf, kNameBytes - 1);   // ≤19 б, как WriteFixedName
		if (Mark != ETabNameMark::None && N < kMarkPos)                          // нуль-терминатор на позиции N ≤ 17
		{
			const uint8* M = (Mark == ETabNameMark::User) ? kMarkUser : kMarkDefault;
			Buf[kMarkPos] = M[0];
			Buf[kMarkPos + 1] = M[1];
			const int32 K = LocId - kTabLocBase;                                 // номер вкладки в пометке
			if (Mark == ETabNameMark::Default && K > 0 && K < kTabLocSpan) { Buf[kMarkPos + 1] = (uint8)(kMarkDefault[1] ^ K); }
		}
		Out.Append(Buf, kNameBytes);
	}

	// Пометка происхождения из записи: подпись засчитывается, только если до неё строка уже закончилась нулём
	//   (иначе байты 18–19 — часть самого имени). Мусор в хвосте записи чужого файла совпадёт с подписью с
	//   вероятностью 1/65536 (★ terms_minors: с номером — 64/65536) — худшее следствие: вкладка оригинала не
	//   переведётся на язык сессии или получит подпись по умолчанию другой вкладки. `OutLocId` — loc-id из номера
	//   пометки «по умолчанию» (0 — номера нет: прежняя подпись `5A A5`).
	ETabNameMark ReadTabNameMark(const uint8* P, int32* OutLocId = nullptr)
	{
		if (OutLocId) { *OutLocId = 0; }
		bool bEnded = false;
		for (int32 i = 0; i < kMarkPos; ++i) { if (P[i] == 0) { bEnded = true; break; } }
		if (!bEnded) { return ETabNameMark::None; }
		if (P[kMarkPos] == kMarkUser[0] && P[kMarkPos + 1] == kMarkUser[1]) { return ETabNameMark::User; }
		if (P[kMarkPos] == kMarkDefault[0])
		{
			const int32 K = P[kMarkPos + 1] ^ kMarkDefault[1];                  // 0 — без номера
			if (K < kTabLocSpan)
			{
				if (OutLocId && K > 0) { *OutLocId = kTabLocBase + K; }
				return ETabNameMark::Default;
			}
		}
		return ETabNameMark::None;
	}

	// ★★★ ЭТАЛОННЫЕ ABS-КООРДИНАТЫ (база 1024×768) окон 2590 и 2700 — из `gamemain_jui.ini`
	//   (отчёт `chat_tab_editor_2590_re.md` §1.1 и §2.1). Ими кладутся ТОЛЬКО те виджеты, которых
	//   импортёр не создал (контролы с пустым `ID`) или которые он создал не того класса.
	//   Из них берутся ШАГИ клонирования 8-й строки (у её контролов пустой `ID`, точных имён нет).
	constexpr float kRowCheckY[8] = { 262.f, 286.f, 312.f, 337.f, 361.f, 386.f, 412.f, 436.f }; // 2610…2617
	constexpr float kRowLabelY[8] = { 268.f, 293.f, 318.f, 343.f, 368.f, 393.f, 418.f, 443.f }; // 2621…2628
	constexpr float kRowColorY[8] = { 267.f, 292.f, 317.f, 342.f, 367.f, 392.f, 417.f, 441.f }; // 2641…2648

	// ★ ПРЯМОУГОЛЬНИК АТЛАСА ЗАЛИВКИ. Эталон для КАЖДОГО образца цвета и КАЖДОЙ плитки палитры
	//   зовёт `CControl_DrawTextureRect(ctrl, "n_01.tex", 56, 464, 24, 12)` (chat.c:1141 и 1191) —
	//   это участок 24×12 атласа `n_01` (512×512), сплошной белый (проверено по пикселям), поверх
	//   которого движок кладёт цвет через `CControl_SetValueAll4`.
	constexpr float kSwatchAtlas = 512.f;
	const FBox2f kSwatchUV(FVector2f(56.f / kSwatchAtlas, 464.f / kSwatchAtlas),
	                       FVector2f(80.f / kSwatchAtlas, 476.f / kSwatchAtlas));

	// ★★★ ПОИСК ВИДЖЕТА ПО ИМЕНИ С УЧЁТОМ РЕГИСТРА.
	//   `UUserWidget::GetWidgetFromName` сравнивает через `FName` — то есть БЕЗ учёта регистра.
	//   В окне 2700 есть ДВА разных контрола, чьи имена различаются ТОЛЬКО регистром:
	//   `CHAT_Color_Select_Color_s2` (2731, «Выбранный цвет») и `..._Color_S2` (2732, «Исходный»).
	//   Через `GetWidgetFromName` оба запроса возвращали ОДИН И ТОТ ЖЕ виджет — второе превью не
	//   работало в принципе. Ищем сравнением строк с учётом регистра.
	UWidget* FindExactCase(UUserWidget* Host, const TCHAR* Name)
	{
		if (!Host || !Host->WidgetTree) { return nullptr; }
		TArray<UWidget*> All;
		Host->WidgetTree->GetAllWidgets(All);
		for (UWidget* W : All)
		{
			if (W && W->GetName().Equals(Name, ESearchCase::CaseSensitive)) { return W; }
		}
		return nullptr;
	}

	// ★ ПЛАН КОНТРОЛА (`<idc>_<порядковый>_<план>`) с учётом РЕГИСТРА префикса.
	//   Именно план несёт АВТОРСКИЙ rect контрола: одноимённый TextBlock импортёр сдвигает вниз
	//   на (h − 14)/2 (вертикальная центровка подписи), поэтому геометрию берём с ПЛАНА.
	//   `FString::StartsWith` по умолчанию сравнивает БЕЗ учёта регистра — здесь это критично
	//   (`..._Color_s2_` и `..._Color_S2_` различаются ТОЛЬКО регистром).
	UImage* FindPlaneCase(UUserWidget* Host, const FString& Prefix)
	{
		if (!Host || !Host->WidgetTree) { return nullptr; }
		TArray<UWidget*> All;
		Host->WidgetTree->GetAllWidgets(All);
		for (UWidget* W : All)
		{
			UImage* Img = Cast<UImage>(W);
			if (Img && Img->GetName().StartsWith(Prefix, ESearchCase::CaseSensitive)) { return Img; }
		}
		return nullptr;
	}
}

// ★★★ ПАЛИТРА 20 ПЛИТОК — ЗНАЧЕНИЯ ИЗ ЭТАЛОНА, НЕ ВЫДУМАНЫ.
//   `chatInitColorPalette @0xB9E110` (`RE/etalon/chat.c:684-706`) пишет их в `CChat+0x22C…+0x278`
//   (буфер `msgColorPaletteExt`, билдер читает `[4*i + 4]`, i = 0..19 — `chat.c:1188-1196`).
//   Отчёт-подтверждение: `Reports/chat_color_palette_re.md` (байты EXE + декомпил + live.dmp).
//   Порядок — 2711 → 2730, сетка окна 5 столбцов × 4 строки построчно.
const FColor UAsdaChatWindowSubsystem::kPaletteColors[UAsdaChatWindowSubsystem::kPaletteSlots] =
{
	FColor(0xFF, 0xCE, 0xC6, 0xFF), FColor(0x8C, 0x91, 0xFF, 0xFF), FColor(0x99, 0xCC, 0x00, 0xFF),
	FColor(0xFF, 0x00, 0x00, 0xFF), FColor(0xFF, 0xFF, 0xFF, 0xFF), FColor(0xAB, 0xFF, 0x79, 0xFF),
	FColor(0x6F, 0xDB, 0xFE, 0xFF), FColor(0xFF, 0xD8, 0x00, 0xFF), FColor(0xFF, 0x90, 0xFE, 0xFF),
	FColor(0xA0, 0x3C, 0x00, 0xFF), FColor(0xB7, 0x3F, 0xD8, 0xFF), FColor(0x79, 0xF9, 0xFE, 0xFF),
	FColor(0xFE, 0x79, 0xAE, 0xFF), FColor(0xFB, 0x5B, 0xEE, 0xFF), FColor(0x42, 0x91, 0xFC, 0xFF),
	FColor(0x56, 0xE7, 0xFC, 0xFF), FColor(0x8B, 0xFF, 0xCA, 0xFF), FColor(0xBE, 0xFF, 0x6A, 0xFF),
	FColor(0xFF, 0xD4, 0x3F, 0xFF), FColor(0xFF, 0x7A, 0x3F, 0xFF),
};

// ★ Фабрика виджета-заливки: белый прямоугольник атласа `n_01` (56,464,24,12), тинт задаётся
//   вызывающим. Один механизм на образцы строк 2641…2648, плитки палитры 2711…2730 и превью
//   2731/2732 — ровно как в эталоне, где все они рисуются одной парой
//   `CControl_DrawTextureRect` + `CControl_SetValueAll4`.
UImage* UAsdaChatWindowSubsystem::MakeSwatch(UUserWidget* Host, UPanelWidget* Parent, const TCHAR* Name, int32 Z)
{
	// ★ U23 (В1 дедупа №2): квад — общей точкой `AsdaUi::MakeAtlasQuad` (тело было построчной копией `MakeTabQuad`).
	return AsdaUi::MakeAtlasQuad(Host, Parent, Name, FName(TEXT("n_01")), kSwatchUV, FVector2D(24.f, 12.f), Z);
}

// Скопировать слот контрола-«хозяина» (его геометрия пришла из JUI импортёром) на наш виджет.
//   ★ Закон jui-align-anchors соблюдён буквально: схема слота берётся С САМОГО ОБРАЗЦА, а не
//   подставляется числом, поэтому на любом разрешении заливка лежит ровно на своём контроле.
void UAsdaChatWindowSubsystem::PlaceOverWidget(UWidget* What, UWidget* Over)
{
	UCanvasPanelSlot* Dst = AsdaUi::SlotOf(What);
	UCanvasPanelSlot* Src = AsdaUi::SlotOf(Over);
	if (!Dst || !Src) { return; }
	Dst->SetAnchors(Src->GetAnchors());
	Dst->SetAlignment(Src->GetAlignment());
	Dst->SetAutoSize(false);
	Dst->SetOffsets(Src->GetOffsets());
}

// ══════════════════════════ ПАЛИТРА / ОПЦИИ КАНАЛОВ ═══════════════════════════════════════════
// Опции каналов заполняются ШАБЛОНОМ игры: имя = loc 5341…5350, цвет = таблица каналов
//   (`chatInitChannelColorsAndNames @0xBA0660`, отчёт §3.3). Это те же 10 записей, что уже лежат
//   в `kChannels` окна чата, — второй копии значений не заводим, берём через SlotColor/SlotNameLocId.
void UAsdaChatWindowSubsystem::InitDefaultChanOpts()
{
	ChanOpts.SetNum(kChanCount);
	for (int32 i = 0; i < kChanCount; ++i)
	{
		ChanOpts[i].Name  = UiChannelText(SlotNameLocId(i));
		ChanOpts[i].Color = SlotColor(i);
	}
	ChanOptsEdit = ChanOpts;
}

// ══════════════════════════ ПЕРСИСТ `./ChatOpt.bin` ═══════════════════════════════════════════
// ЭТАЛОННАЯ РАСКЛАДКА (отчёт §3.4), воспроизведена БАЙТ В БАЙТ:
//     0x000  240 = 10 × 24   ChatChannelOpt[10] : char name[20] + DWORD colorARGB
//     0x0F0    4             DWORD tabCount
//     0x0F4  tabCount × 22   ChatTabRecord[]    : char name[20] + WORD  channelMask
//   Ни magic, ни версии в файле нет — их и не добавляем, раскладка остаётся эталонной.
//   ★ 10.10.2026 (en_terms_fix): в хвосте `name[20]` записи вкладки ПОСЛЕ завершающего нуля — пометка происхождения
//   имени (игрок / по умолчанию, см. WriteTabName). Читатели C-строки её не видят; размер и смещения не меняются.
// ★ [Б4] РЕЖИМ ФАЙЛА — БИНАРНЫЙ (оригинал открывает текстовым и портит данные). ОТСТУПЛЕНИЕ.
void UAsdaChatWindowSubsystem::SaveChatOpt() const
{
	TArray<uint8> Data;
	Data.Reserve(kChanOptSize * kChanCount + 4 + kTabRecordSize * ChatTabs.Num());
	for (int32 i = 0; i < kChanCount; ++i)
	{
		const FAsdaChanOpt& O = ChanOpts.IsValidIndex(i) ? ChanOpts[i] : FAsdaChanOpt();
		WriteFixedName(Data, O.Name);
		const uint32 Argb = ((uint32)O.Color.A << 24) | ((uint32)O.Color.R << 16)
		                  | ((uint32)O.Color.G << 8)  | (uint32)O.Color.B;
		Data.Append((const uint8*)&Argb, 4);
	}
	const int32 Count = ChatTabs.Num();
	Data.Append((const uint8*)&Count, 4);
	for (const FAsdaChatTab& T : ChatTabs)
	{
		// ★ У эталонных вкладок имя лежит в записи (там его пишет `sprintf(base, loc 54xx)`),
		//   поэтому в файл идёт ГОТОВАЯ подпись — TabName даёт её и для loc-вкладок, и для своих.
		//   ★ 10.10.2026 (en_terms_fix): + пометка происхождения имени в хвосте записи (см. WriteTabName).
		//   ★ 10.10.2026 (terms_minors): у пометки «по умолчанию» — номер вкладки (LocId), см. WriteTabName.
		WriteTabName(Data, T.LocId > 0 ? UiChannelText(T.LocId) : T.Name, T.LocId > 0 ? ETabNameMark::Default : ETabNameMark::User, T.LocId);
		const uint16 Mask = (uint16)T.Mask;
		Data.Append((const uint8*)&Mask, 2);
	}
	if (FFileHelper::SaveArrayToFile(Data, *ChatOptPath()))
	{
		UE_LOG(LogAsdaChatTab, Log,
			TEXT("[чат-вкладки] ChatOpt.bin сохранён: %d байт (10 опций каналов + %d вкладок) → %s"),
			Data.Num(), Count, *ChatOptPath());
	}
	else
	{
		// Эталон здесь показывает MessageBox «ChatTabInfo File Save Fail!» (chat.c:1406).
		UE_LOG(LogAsdaChatTab, Error, TEXT("[чат-вкладки] НЕ УДАЛОСЬ записать %s"), *ChatOptPath());
	}
}

bool UAsdaChatWindowSubsystem::LoadChatOpt()
{
	TArray<uint8> Data;
	if (!FFileHelper::LoadFileToArray(Data, *ChatOptPath()))
	{
		// Эталон при отсутствии файла СРАЗУ пишет дефолт (chat.c:1381) — делаем так же.
		UE_LOG(LogAsdaChatTab, Log, TEXT("[чат-вкладки] %s нет — пишем дефолт"), *ChatOptPath());
		SaveChatOpt();
		return false;
	}
	const int32 Head = kChanOptSize * kChanCount + 4;
	if (Data.Num() < Head)
	{
		UE_LOG(LogAsdaChatTab, Warning,
			TEXT("[чат-вкладки] ChatOpt.bin короче заголовка (%d < %d) — файл отвергнут, остаются дефолты"),
			Data.Num(), Head);
		return false;
	}
	// 10 опций каналов.
	TArray<FAsdaChanOpt> NewOpts;
	NewOpts.SetNum(kChanCount);
	for (int32 i = 0; i < kChanCount; ++i)
	{
		const uint8* P = Data.GetData() + kChanOptSize * i;
		NewOpts[i].Name = ReadFixedName(P);
		uint32 Argb = 0; FMemory::Memcpy(&Argb, P + kNameBytes, 4);
		NewOpts[i].Color = FColor((uint8)((Argb >> 16) & 0xFF), (uint8)((Argb >> 8) & 0xFF),
		                          (uint8)(Argb & 0xFF), (uint8)((Argb >> 24) & 0xFF));
	}
	int32 Count = 0;
	FMemory::Memcpy(&Count, Data.GetData() + kChanOptSize * kChanCount, 4);
	// ★ [Б5] КЛАМП. В оригинале его НЕТ — битый файл ронял клиент на resize().
	if (Count < 1 || Count > kTabCountMax || Data.Num() < Head + Count * kTabRecordSize)
	{
		UE_LOG(LogAsdaChatTab, Warning,
			TEXT("[чат-вкладки] ChatOpt.bin: tabCount=%d при размере %d байт — не сходится, файл отвергнут ")
			TEXT("(★ отступление: в оригинале клампа нет и такой файл ронял клиент)"), Count, Data.Num());
		return false;
	}
	TArray<FAsdaChatTab> NewTabs;
	NewTabs.Reserve(Count);
	int32 Relinked = 0;   // ★ 10.10.2026 (en_completeness): записей, снова ставших loc-подписями
	int32 UserMarked = 0; // ★ 10.10.2026 (en_terms_fix): записей с пометкой «имя задал игрок» (не переводятся)
	int32 ByIndex = 0;    // ★ 10.10.2026 (terms_minors): подписей по умолчанию, восстановленных по номеру из пометки (без сверки текста)
	for (int32 i = 0; i < Count; ++i)
	{
		const uint8* P = Data.GetData() + Head + kTabRecordSize * i;
		FAsdaChatTab T;
		T.Name = ReadFixedName(P);
		uint16 Mask = 0; FMemory::Memcpy(&Mask, P + kNameBytes, 2);
		T.Mask = (int32)Mask;
		T.LocId = 0;   // из файла приходит ГОТОВОЕ имя — вкладка становится «пользовательской»
		// ★ 10.10.2026 (en_completeness, баг владельца «вкладки чата по-русски в английской версии»): имя = подпись
		//   вкладки по умолчанию на ЛЮБОМ языке (файл записан в другой языковой сессии) → снова loc-вкладка, подпись
		//   на языке сессии. Свои имена игрока не трогаются. (`ChatTabs` здесь ещё дефолтные — `InitDefaultTabs`.)
		// ★ 10.10.2026 (en_terms_fix): запись с пометкой «имя задал игрок» не сверяется вовсе — его «Party» в русской
		//   сессии остаётся его именем. Без пометки (файл оригинала/старого порта) и с пометкой «по умолчанию» — сверка.
		// ★ 10.10.2026 (terms_minors): пометка «по умолчанию» с номером — LocId по номеру, если такая вкладка есть среди
		//   эталонных; текст записи не сверяется (loc-строку вкладки могли поправить после сохранения файла).
		int32 MarkLoc = 0;
		const ETabNameMark Mark = ReadTabNameMark(P, &MarkLoc);
		const bool bKnownLoc = MarkLoc > 0 && ChatTabs.ContainsByPredicate([MarkLoc](const FAsdaChatTab& D) { return D.LocId == MarkLoc; });
		if (Mark == ETabNameMark::User)
		{
			++UserMarked;
		}
		else if (Mark == ETabNameMark::Default && bKnownLoc)
		{
			T.LocId = MarkLoc; T.Name.Empty(); ++Relinked; ++ByIndex;
		}
		else
		{
			for (const FAsdaChatTab& Def : ChatTabs)
			{
				if (Def.LocId > 0 && IsDefaultNameAnyLang(T.Name, Def.LocId)) { T.LocId = Def.LocId; T.Name.Empty(); ++Relinked; break; }
			}
		}
		NewTabs.Add(MoveTemp(T));
	}
	// ★ 10.10.2026 (en_completeness): то же для имён 10 каналов — имя по умолчанию на любом языке → на языке сессии.
	for (int32 i = 0; i < kChanCount; ++i)
	{
		const int32 Loc = SlotNameLocId(i);
		if (Loc > 0 && IsDefaultNameAnyLang(NewOpts[i].Name, Loc)) { NewOpts[i].Name = UiChannelText(Loc); ++Relinked; }
	}
	ChanOpts = MoveTemp(NewOpts);
	ChanOptsEdit = ChanOpts;
	ChatTabs = MoveTemp(NewTabs);
	ChatTabsEdit = ChatTabs;
	ActiveTab = FMath::Clamp(ActiveTab, 0, ChatTabs.Num() - 1);
	TabScroll = 0;
	UE_LOG(LogAsdaChatTab, Log, TEXT("[чат-вкладки] ChatOpt.bin загружен: вкладок %d, опций каналов %d; подписей по умолчанию (на языке сессии) %d, из них по номеру в пометке %d; имён игрока с пометкой %d"),
		ChatTabs.Num(), ChanOpts.Num(), Relinked, ByIndex, UserMarked);
	return true;
}

// ★ 10.10.2026 (en_completeness) — разбор у объявления. Сравнение — после того же кодека 20 байт, что у файла
//   (имя в файле могло быть усечено ровно так же).
bool UAsdaChatWindowSubsystem::IsDefaultNameAnyLang(const FString& Name, int32 LocId)
{
	if (Name.IsEmpty() || LocId <= 0) { return false; }
	auto AsInFile = [](const FString& S)
	{
		uint8 Buf[kNameBytes] = { 0 };
		FAsdaLoginData::EncodeCp1251(S, Buf, kNameBytes - 1);
		return FAsdaLoginData::DecodeCp1251(Buf, kNameBytes);
	};
	const int32 Ids[2] = { LocId, RenamedChannelLoc(LocId) };
	for (const int32 Id : Ids)
	{
		if (Id <= 0) { continue; }
		TArray<FString> Cand;   // подписи этой строки на всех языках порта
		if (const FAsdaGameTextRow* R = AsdaDt::RowByInt<FAsdaGameTextRow>(FAsdaLoginData::kGameTextPath, Id, TEXT("ChatOptDefault")))
		{
			Cand.Add(FAsdaLoginData::StripMarkup(R->SourceString));
		}
		for (const EAsdaLang L : { EAsdaLang::En, EAsdaLang::Ar })
		{
			FString S;
			if (FAsdaLang::FindOverlay(L, TEXT("GameText"), FString::FromInt(Id), S)) { Cand.Add(FAsdaLoginData::StripMarkup(S)); }
		}
		for (const FString& C : Cand)
		{
			if (!C.IsEmpty() && (AsInFile(C) == Name || AsInFile(C.TrimStartAndEnd()) == Name)) { return true; }
		}
	}
	return false;
}

// ══════════════════════════ ОПЕРАЦИИ НАД ВКЛАДКАМИ ════════════════════════════════════════════

// `chatAddFilterEntry @0xBA26A0` (chat.c:1894-1930). Маска новой вкладки = 0 — буфер обнулён
//   `sub_922097`, то есть новая вкладка НЕ показывает ни одного канала, пока игрок не отметит
//   галочки в редакторе 2590. Это эталон, а не недоделка.
// ★★★ ТЕКСТ ЛИМИТА — ОДНА ТОЧКА ИСТИНЫ (и подсказка диалога 1580, и отказ в ленте).
//   Эталонная строка loc 5338 говорит «не более 5 символов» (5 корейских слогов CP949 = 10 байт
//   эталонной проверки `strlen <= 0xA`). У нас лимит — 15 символов по решению юзера, поэтому
//   цифру в тексте приводим к правде: иначе игрок читает одно, а получает другое.
FString UAsdaChatWindowSubsystem::TabNameLimitText() const
{
	UGameInstance* GI = GetGameInstance();
	UAsdaTextSubsystem* Txt = GI ? GI->GetSubsystem<UAsdaTextSubsystem>() : nullptr;
	FString S = Txt ? Txt->GetTextById(5338) : FString();
	if (S.IsEmpty())
	{
		return TEXT("[#5338]");   // [ПОРТ] 09.10.2026 (мультиязычность, этап 3): без RU-фолбэка
	}
	// Меняем ЧИСЛО, а не строку целиком: перевод/склонения остаются эталонными.
	// Ищем первое отдельно стоящее число (в RU-строке это «5») и подставляем действующий лимит.
	int32 i = 0;
	while (i < S.Len() && !FChar::IsDigit(S[i])) { ++i; }
	if (i < S.Len())
	{
		int32 j = i;
		while (j < S.Len() && FChar::IsDigit(S[j])) { ++j; }
		S = S.Left(i) + FString::FromInt(kTabNameMaxChars) + S.Mid(j);
	}
	return S;
}

bool UAsdaChatWindowSubsystem::AddChatTab(const FString& Name)
{
	if (Name.Len() > kTabNameMaxChars || Name.IsEmpty())
	{
		// Эталон: `CItem_BuildTooltipText(..., 0, 5338, loc 5338, 1)` — одно-кнопочное уведомление.
		AddChatLineRaw(6, TabNameLimitText(), FColor(255, 0, 0, 255));
		UE_LOG(LogAsdaChatTab, Warning, TEXT("[чат-вкладки] «%s»: длина %d, лимит %d — вкладка не добавлена"),
			*Name, Name.Len(), kTabNameMaxChars);
		return false;
	}
	FAsdaChatTab T;
	T.Name = Name;
	T.Mask = 0;          // ← эталон: обнулённый буфер
	T.LocId = 0;
	ChatTabs.Add(T);
	ChatTabsEdit = ChatTabs;     // эталон: mirror := live сразу после push_back
	RefreshChatTabs();           // [Б6] перерисовываем ОБА списка (эталон забывал про 2597)
	RefreshTabEditor();
	SaveChatOpt();               // эталон пишет файл сразу
	UE_LOG(LogAsdaChatTab, Log, TEXT("[чат-вкладки] добавлена «%s» (маска 0 — каналы отмечаются в 2590), всего %d"),
		*Name, ChatTabs.Num());
	return true;
}

// `chatMacro_setShortcutText @0xBA2830` (chat.c:1932-1955).
// ★★★ ЭТАЛОН переименовывает АКТИВНУЮ вкладку: `stl_vectorAt_1002(self + 344, self[108])`
//   (chat.c:2119) — self[108] пишет только `chatTabSelectFilter`, то есть ЛЕВЫЙ клик по вкладке.
//   ⚠ ОСОЗНАННОЕ ОТСТУПЛЕНИЕ (Д27 п.5): теперь операция адресная — индекс приходит из меню 2670
//   (вкладка под правым кликом). Обёртка `RenameActiveTab` сохранена для консоли и совместимости.
// ★ [Б3] Вместо `sprintf(v3, Str)` — обычное присваивание: форматная строка из пользовательского
//   ввода в оригинале читала мусор из стека при имени с «%s».
bool UAsdaChatWindowSubsystem::RenameTab(int32 Tab, const FString& Name)
{
	if (!ChatTabs.IsValidIndex(Tab)) { return false; }
	if (Name.Len() > kTabNameMaxChars || Name.IsEmpty())
	{
		AddChatLineRaw(6, TabNameLimitText(), FColor(255, 0, 0, 255));
		UE_LOG(LogAsdaChatTab, Warning, TEXT("[чат-вкладки] «%s»: длина %d, лимит %d — переименование отклонено"),
			*Name, Name.Len(), kTabNameMaxChars);
		return false;
	}
	ChatTabs[Tab].Name  = Name;   // ★ [Б3] простое присваивание, не sprintf
	ChatTabs[Tab].LocId = 0;      // переименованная вкладка перестаёт быть loc-вкладкой
	ChatTabsEdit = ChatTabs;
	RefreshChatTabs();
	RefreshTabEditor();
	SaveChatOpt();
	UE_LOG(LogAsdaChatTab, Log, TEXT("[чат-вкладки] вкладка %d переименована в «%s» (активная %d)"),
		Tab, *Name, ActiveTab);
	return true;
}

// `chatTabRemoveSelected @0xBA2A70` (chat.c:1980-2026). `if (tabCount > 1)` — последнюю удалить
//   нельзя; удалять можно ЛЮБУЮ, включая первую.
// ★★★ ЭТАЛОН удаляет АКТИВНУЮ (`self[108]`) и после удаления ставит `this[108] = 0`.
//   ⚠ ОСОЗНАННОЕ ОТСТУПЛЕНИЕ (Д27 п.5): удаляется вкладка, ПО КОТОРОЙ вызвано меню. Раз цель
//   может быть не активной, эталонное «активной становится нулевая» уточнено: активная
//   сохраняется, если удалили другую вкладку (иначе игрока без причины перебрасывало бы на
//   первую вкладку), и сбрасывается в 0, если удалили именно активную — как в эталоне.
bool UAsdaChatWindowSubsystem::RemoveTab(int32 Tab)
{
	if (ChatTabs.Num() <= 1)
	{
		UE_LOG(LogAsdaChatTab, Warning,
			TEXT("[чат-вкладки] осталась одна вкладка — удалять нельзя (эталон: `if (tabCount > 1)`)"));
		return false;
	}
	if (!ChatTabs.IsValidIndex(Tab)) { return false; }
	const FString Was = TabName(Tab);
	ChatTabs.RemoveAt(Tab, 1, EAllowShrinking::No);
	ChatTabsEdit = ChatTabs;
	if (Tab == ActiveTab)        { ActiveTab = 0; }              // эталон: this[108] = 0
	else if (Tab < ActiveTab)    { --ActiveTab; }                // индексы сдвинулись влево
	ActiveTab = FMath::Clamp(ActiveTab, 0, ChatTabs.Num() - 1);
	TabScroll = 0;
	EditTabIdx = FMath::Clamp(EditTabIdx, 0, ChatTabs.Num() - 1);
	TabMenuTargetIdx = INDEX_NONE;         // цель меню исчезла вместе с вкладкой
	RefreshChatTabs();                     // [Б6] оба списка
	RefreshTabEditor();
	RebuildLines();                        // фильтр сменился — лента перестраивается
	SaveChatOpt();
	UE_LOG(LogAsdaChatTab, Log, TEXT("[чат-вкладки] удалена вкладка %d «%s», осталось %d, активная %d"),
		Tab, *Was, ChatTabs.Num(), ActiveTab);
	return true;
}

// `chatInitFilterTabs @0xBA2460` (chat.c:1839). ★ Эталон трогает ТОЛЬКО рабочие буферы и НЕ пишет
//   файл — запись произойдёт по ОК/крестику окна 2590. Воспроизведено 1:1.
void UAsdaChatWindowSubsystem::ResetTabsToDefault()
{
	InitDefaultTabs();
	InitDefaultChanOpts();
	ChatTabsEdit = ChatTabs;
	ChanOptsEdit = ChanOpts;
	EditTabIdx = 0;
	EditChanScroll = 0;
	RefreshChatTabs();
	RefreshTabEditor();
	RebuildLines();
	UGameInstance* GI = GetGameInstance();
	UAsdaTextSubsystem* Txt = GI ? GI->GetSubsystem<UAsdaTextSubsystem>() : nullptr;
	const FString M = Txt ? Txt->GetTextById(5311) : FString();   // «Настройки вкладки чата восстановлены.»
	AddChatLineRaw(6, M.IsEmpty() ? FString(TEXT("[#5311]")) : M);   // [ПОРТ] 09.10.2026 (мультиязычность, этап 3): без RU-фолбэка
	UE_LOG(LogAsdaChatTab, Log,
		TEXT("[чат-вкладки] сброс к эталонным %d (★ на диск НЕ пишем — эталон сохраняет только по ОК окна 2590)"),
		ChatTabs.Num());
}

// ══════════════════════════ ОКНО РЕДАКТОРА ВКЛАДОК 2590 ═══════════════════════════════════════
// Геометрия — из отчёта §1.1 (ABS в базе 1024×768). Окно импортировано целиком
//   (`WBP_CHAT_TAB_WIN`), поэтому скины/рамки берём из ассета, а поведение и раскладку строк
//   восстанавливаем кодом по эталонным rect'ам.
//
//   строка i (i = 0..7):  чекбокс  ABS x 563, y 262 + 25·i, 18×19   (2610+i)
//                         подпись  ABS x 600, y 268 + 25·i, 130×16  (2621+i)
//                         образец  ABS x 745, y 267 + 25·i, 18×18   (2641+i)
//   список вкладок 2597:  ABS 427, 266, 79×25 — ячейки-клоны, шаг 25 по Y
void UAsdaChatWindowSubsystem::ShowTabEditor(bool bSnapshot, int32 TargetTab)
{
	BuildTabEditor();
	if (!TabEditorWidget) { return; }
	if (bSnapshot)
	{
		// `uiRefreshSkillSlotList(this, 1)`: backup := live, mirror := live.
		ChatTabsEdit = ChatTabs;
		ChanOptsEdit = ChanOpts;
		EditTabIdx = FMath::Clamp(ActiveTab, 0, FMath::Max(0, ChatTabs.Num() - 1));
		EditChanScroll = 0;
	}
	// ★ Открытие «по конкретной вкладке» (пункт 2671 меню 2670, Д27 п.5). Эталон здесь ничего не
	//   ставит — редактор показывает `stateFlags1`, то есть последнюю выбранную в списке 2597.
	if (ChatTabsEdit.IsValidIndex(TargetTab))
	{
		EditTabIdx = TargetTab;
		// Прокрутить столбец 2597 так, чтобы целевая вкладка была видна (иначе выделение
		//   уехало бы за пределы окна списка и «настройки открылись не на той вкладке» на вид).
		const int32 Vis = FMath::Max(1, EditTabVisibleCount());
		if (EditTabIdx < EditTabScroll)              { EditTabScroll = EditTabIdx; }
		else if (EditTabIdx >= EditTabScroll + Vis)  { EditTabScroll = EditTabIdx - Vis + 1; }
		EditTabScroll = FMath::Clamp(EditTabScroll, 0, FMath::Max(0, ChatTabsEdit.Num() - Vis));
	}
	TabEditorWidget->SetVisibility(ESlateVisibility::Visible);
	RefreshTabEditor();
	UE_LOG(LogAsdaChatTab, Log,
		TEXT("[чат-вкладки] редактор 2590 открыт (снимок=%s): вкладок %d, редактируется %d «%s», прокрутка каналов %d"),
		bSnapshot ? TEXT("да") : TEXT("нет"), ChatTabsEdit.Num(), EditTabIdx, *TabName(EditTabIdx), EditChanScroll);
}

void UAsdaChatWindowSubsystem::HideTabEditor()
{
	HideColorSelect();
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UAsdaWindowSubsystem* WM = GI->GetSubsystem<UAsdaWindowSubsystem>())
		{
			WM->CloseWindow(kTabEditorId);
			WM->UnregisterWheelHandler(kTabEditorId);
		}
	}
	TabEditorWidget = nullptr;
	bTabEditorBuilt = false;
	TabEditorRelays.Reset();
	// Столбец ячеек 2597 жил внутри окна — вместе с ним и умирает.
	EditTabCells.Reset();
	EditTabCellCaps.Reset();
	EditTabArrowUp = nullptr; EditTabArrowDn = nullptr;
	EditTabArrowUpCap = nullptr; EditTabArrowDnCap = nullptr;
	EditTabCellHost = nullptr;
	bTabEditorListBuilt = false;
	EditTabHoverIdx = -1;
	EditArrowHoverIdx = -1;
	// Заливки образцов цвета и достроенные узлы 8-й строки жили внутри окна 2590.
	EditRowSwatches.Reset();
	EditRow8Check = nullptr;
	EditRow8Label = nullptr;
}

// ОК 2650 и КРЕСТИК 2651 — ОДИН обработчик (`case 0xA5A: case 0xA5B:`, chat.c:2087-2093):
//   live := edit по обоим буферам, запись файла, скрыть окно, применить фильтр активной вкладки.
void UAsdaChatWindowSubsystem::CommitTabEditor()
{
	ChatTabs = ChatTabsEdit;
	ChanOpts = ChanOptsEdit;
	if (ChatTabs.Num() == 0) { InitDefaultTabs(); }
	ActiveTab = FMath::Clamp(ActiveTab, 0, ChatTabs.Num() - 1);
	SaveChatOpt();
	HideTabEditor();
	RefreshChatTabs();
	// ★★★ ЦВЕТА КАНАЛОВ ПРИМЕНЯЮТСЯ К ЛЕНТЕ ИМЕННО ЗДЕСЬ. `RebuildLines` заново собирает разметку
	//   КАЖДОЙ строки через `BuildLineMarkup`, а та берёт цвет канала живой функцией `ChanColor`
	//   (= запись `tabFilterOpt[slot]`, только что перезаписанная коммитом). Поэтому меняется и
	//   текст уже висящих строк, и цвет их префиксов «[Общ.]», и все последующие сообщения.
	//   ★ ОТСТУПЛЕНИЕ (мелкое, осознанное): эталон хранит цвет В САМОЙ строке списка на момент
	//   добавления, поэтому ранее пришедшие строки у него сохраняют СТАРЫЙ цвет; юзер явно
	//   потребовал перекрашивать и уже висящие. Данные при этом 1:1 — источник цвета тот же.
	RebuildLines();     // `sub_9387FC(self[3], this+498)` — применить маску активной вкладки к ленте
	// Раскрытый список каналов 2608 красится теми же цветами — перерисовать, если он открыт.
	if (bComboOpen) { RefreshChannelCombo(-1); }
	// ★★★ ДОКАЗАТЕЛЬНЫЙ ЛОГ (Д28): ЖИВАЯ таблица цветов каналов сразу после коммита — по нему
	//   видно, дошёл ли выбранный цвет до записи, из которой красится лента (`ChanColor`).
	{
		FString T;
		for (int32 i = 0; i < ChanOpts.Num(); ++i)
		{
			T += FString::Printf(TEXT("\n    слот %d «%s» = FF%02X%02X%02X"),
				i, *UiChannelText(SlotNameLocId(i)),
				ChanOpts[i].Color.R, ChanOpts[i].Color.G, ChanOpts[i].Color.B);
		}
		UE_LOG(LogAsdaChatTab, Log, TEXT("[чат-цвета] КОММИТ, живая таблица каналов:%s"), *T);
	}
	UE_LOG(LogAsdaChatTab, Log,
		TEXT("[чат-вкладки] редактор 2590: КОММИТ (ОК/крестик — в эталоне это один и тот же обработчик), вкладок %d"),
		ChatTabs.Num());
}

void UAsdaChatWindowSubsystem::BuildTabEditor()
{
	UGameInstance* GI = GetGameInstance();
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM) { return; }
	// ★★★ ГВАРД ПО `IsWindowLive`, А НЕ ПО NULL (регрессия 2026-08-18, см. minimap_channels_fix.md).
	//   Здесь стояло `if (bTabEditorBuilt && TabEditorWidget) return;`. Эталонный
	//   `uiCloseAllWindows` (порт — `UAsdaWindowSubsystem::CloseAllPlayerWindows()`). Само дерево
//   виджетов сносит `CloseAll()` — вход в мир (`UAsdaWorldSubsystem::RevealWorld`) и разборка
	//   снимает окно 2590 с реестра и с вьюпорта, НЕ трогая наш указатель: он оставался ненулевым,
	//   `bTabEditorBuilt` — взведённым, и редактор вкладок больше не открывался никогда.
	if (bTabEditorBuilt && WM->IsWindowLive(TabEditorWidget)) { return; }
	// Указатель есть, но окно мертво ⇒ снять ВЕСЬ производный от него хлам (клоны ячеек 2597,
	//   заливки образцов, реле, флаг списка) — иначе следующий `BuildTabEditorList` выйдет
	//   по `bTabEditorListBuilt` и столбец вкладок останется пустым.
	if (TabEditorWidget) { HideTabEditor(); }
	// z = 820: выше диалогов (810/811 открываются ПОВЕРХ и модальны), но выше окна чата.
	TabEditorWidget = WM->OpenWindowByPath(kTabEditorId, kTabEditorWbp, 820, ESlateVisibility::Visible, /*bModal*/false);
	if (!TabEditorWidget) { return; }
	// ★★★ Колесо над списками 2597/2639 — через ЕДИНЫЙ реестр окон (см. OnWheelTabEditor).
	//   z редактора 820 > z чата 689 ⇒ при наложении колесо первым получает редактор, как и должно.
	WM->RegisterWheelHandler(kTabEditorId,
		FAsdaUiWheelHandler::CreateUObject(this, &UAsdaChatWindowSubsystem::OnWheelTabEditor));
	UAsdaTextSubsystem* Txt = GI ? GI->GetSubsystem<UAsdaTextSubsystem>() : nullptr;

	// ── Заголовки окна из таблицы текстов (эталон CChat_BuildChatTabUI, chat.c:1060-1175) ────────
	auto SetTxt = [&](const TCHAR* Node, int32 LocId)
	{
		UTextBlock* T = Cast<UTextBlock>(TabEditorWidget->GetWidgetFromName(FName(Node)));
		if (!T) { UE_LOG(LogAsdaChatTab, Warning, TEXT("[чат-вкладки] узел %s не найден"), Node); return; }
		const FString S = Txt ? Txt->GetTextById(LocId) : FString();
		if (!S.IsEmpty()) { T->SetText(FText::FromString(RenameChannelText(S))); }
		T->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	};
	SetTxt(TEXT("CHAT_TAB_WIN_TextMove"), 5301);   // «Настройки вкладки чата»
	SetTxt(TEXT("CHAT_TAB_WIN_B_Text1"),  5302);   // «Вкладка чата»
	SetTxt(TEXT("CHAT_TAB_WIN_B2_Text1"), 5303);   // «Вид»
	SetTxt(TEXT("CHAT_TAB_WIN_B2_Text2"), 5304);   // «Протокол чата»
	SetTxt(TEXT("CHAT_TAB_WIN_B2_Text3"), 5305);   // «Цвет»
	SetTxt(TEXT("CHAT_TAB_WIN_E_Text"),   5306);   // «Вернуть настройки вкладки чата»
	SetTxt(TEXT("CHAT_TAB_WIN_OK_Text"),  2288);   // «ОК»

	TabEditorRelays.Reset();
	auto BindClick = [&](const TCHAR* Node, TFunction<void()> Fn)
	{
		UWidget* W = TabEditorWidget->GetWidgetFromName(FName(Node));
		if (!W) { return; }
		if (UButton* B = Cast<UButton>(W))
		{
			B->OnClicked.Clear();
			UAsdaButtonRelay* R = NewObject<UAsdaButtonRelay>(this);
			R->Callback = [Fn](int32) { Fn(); };
			B->OnClicked.AddDynamic(R, &UAsdaButtonRelay::OnClicked);
			AsdaUi::AnchorRelay(TabEditorRelays, R, B);
		}
	};
	// ОК 2650 и крестик 2651 — ОДНО И ТО ЖЕ действие (эталон).
	BindClick(TEXT("CHAT_TAB_WIN_OK"), [this]() { CommitTabEditor(); });
	BindClick(TEXT("CHAT_TAB_WIN_X"),  [this]() { CommitTabEditor(); });

	// ── ★★★ ВОСЕМЬ СТРОК КАНАЛОВ: ЗАЛИВКИ ОБРАЗЦОВ ЦВЕТА + ДОСТРОЙКА 8-й СТРОКИ ────────────────
	//   ПРИЧИНЫ ПРАВКИ (живой тест 2026-08-06: «галочки „Вид“ пустые и цвета не заданы»), обе
	//   подтверждены ДАМПОМ ДЕРЕВА `WBP_CHAT_TAB_WIN` (221 узел, `asda_ui_dump_window.py`):
	//   • образцы 2641…2648 имеют скин `NoImage_icon`, а его `Type = "Text"` (JuiSkin.ini) ⇒ импортёр
	//     сделал их `UTextBlock` (в дампе: `CHAT_TAB_WIN_B2_Color1 … Color7` — TextBlock, цвет
	//     0.251/0/0.251 = тот самый `ff400040`). Код красил их `Cast<UImage>` — nullptr всегда;
	//   • 8-я строка (2617/2628/2648) в JUI имеет ПУСТОЙ `ID`, поэтому её узлы импортёр назвал по
	//     СКИНУ (`Box_Check_29_0`, `Black_45_0`, `NoImage_icon_61_0`) — по имени они недостижимы.
	//   ЛЕЧЕНИЕ:
	//   • заливку образца рисуем СВОИМ виджетом поверх ПЛАНА контрола (эталонная пара
	//     `DrawTextureRect("n_01.tex",56,464,24,12)` + `SetValueAll4(ARGB)`, chat.c:1141);
	//     геометрию берём именно с ПЛАНА (`..._Color<N>_<i>_0`), а не с TextBlock: подпись импортёр
	//     сдвигает вниз на (h − 14)/2, план же несёт АВТОРСКИЙ rect;
	//   • недостающие узлы 8-й строки клонируем из образца 1-й строки в ТОТ ЖЕ родительский канвас
	//     со сдвигом на эталонный шаг — приём `uiListAppendClonedCell` (клон = копия ячейки + сдвиг).
	//     Шаги сняты с JUI (отчёт §1.1): галочка +174, подпись +175, образец цвета +174.
	EditRowSwatches.Reset();
	EditRowSwatches.SetNum(kTabEditorRows);
	// ★ `SrcCheck` (эталон-клон галочки C1) СНЯТ 2026-08-27 вместе с построением 8-й галочки —
	//   разбор ниже, у `EditRow8Check`.
	UTextBlock* SrcLabel = Cast<UTextBlock>(TabEditorWidget->GetWidgetFromName(TEXT("CHAT_TAB_WIN_B2_T1")));
	UImage*     SrcPlane = FindPlaneCase(TabEditorWidget, TEXT("CHAT_TAB_WIN_B2_Color1_"));
	for (int32 Row = 0; Row < kTabEditorRows; ++Row)
	{
		// План образца: точный для строк 1…7; для 8-й его имя дал скин — клонируем план 1-й строки.
		UImage* Plane = FindPlaneCase(TabEditorWidget,
			FString::Printf(TEXT("CHAT_TAB_WIN_B2_Color%d_"), Row + 1));
		UWidget* Anchor = Plane ? (UWidget*)Plane : (UWidget*)SrcPlane;
		if (!Anchor) { continue; }
		UImage* Sw = MakeSwatch(TabEditorWidget, Anchor->GetParent(),
			*FString::Printf(TEXT("CHAT_TAB_WIN_B2_Swatch%d"), Row), 60);
		if (!Sw) { continue; }
		PlaceOverWidget(Sw, Anchor);
		if (!Plane)
		{
			// 8-я строка — клон 1-й со сдвигом на эталонные 174 px по Y (ABS 267 → 441).
			if (UCanvasPanelSlot* CS = AsdaUi::SlotOf(Sw))
			{
				FMargin O = CS->GetOffsets();
				O.Top += (kRowColorY[7] - kRowColorY[0]);
				CS->SetOffsets(O);
			}
			UE_LOG(LogAsdaChatTab, Log,
				TEXT("[чат-вкладки] строка %d: у контрола 2648 в JUI пустой ID — заливка склонирована ")
				TEXT("с 1-й строки со сдвигом %.0f px"), Row, kRowColorY[7] - kRowColorY[0]);
		}
		// Пустой TextBlock-«хозяин» гасим: он ничего не рисует, но лежит поверх заливки.
		if (UWidget* Lbl = TabEditorWidget->GetWidgetFromName(
				FName(*FString::Printf(TEXT("CHAT_TAB_WIN_B2_Color%d"), Row + 1))))
		{
			Lbl->SetVisibility(ESlateVisibility::Collapsed);
		}
		Sw->SetCursor(EMouseCursor::Hand);
		EditRowSwatches[Row] = Sw;
	}
	// ── Галочка 8-й строки (2617) ───────────────────────────────────────────────────────────────
	//   ★★★ 2026-08-27. БЫЛО (≈45 строк): чекбокс `CHAT_TAB_WIN_B2_C8` строился здесь КЛОНОМ
	//     `CHAT_TAB_WIN_B2_C1` — копировался стиль, слот сдвигался на 174 px по Y, а лишний
	//     план-картинка 8-й строки искался перебором детей по совпадению `offsets.Top` и гасился.
	//     ПОЧЕМУ УБРАНО: контрол СТАТИЧЕН. В `gamemain_jui.ini` он объявлен как обычный сосед
	//     семи остальных (`Box_Check`, `CheckButton`, 18x19, абс. (563, 436)); ни существование,
	//     ни геометрия, ни класс от рантайма не зависят — только `ID` у него пуст, поэтому
	//     импортёр кнопки не делал (класс дефектов S2).
	//     ГДЕ ЖИВЁТ ТЕПЕРЬ: узел `CHAT_TAB_WIN_B2_C1_CHK00` в
	//     `Content/AsdaAssets/UI/Player_UI/Chat/WBP_CHAT_TAB_WIN`, канвас `CHAT_TAB_WIN_B2_C1_grp`,
	//     слот (0, 174) 18x19 — те же 174 px, но из ДАННЫХ, а не из формулы в коде. Тот же шаг
	//     (`Tools/asda_ui_missing_interactive.py`) гасит и лишний план (`hide_plane`).
	//   ⚠ Мышь по-прежнему разбирает единый диспетчер `TickTabEditorPointer`, а сама галочка —
	//     `HitTestInvisible` (см. `RefreshTabEditor`): именно так и в оригинале.
	EditRow8Check = Cast<UCheckBox>(TabEditorWidget->GetWidgetFromName(TEXT("CHAT_TAB_WIN_B2_C1_CHK00")));
	if (!EditRow8Check)
	{
		UE_LOG(LogAsdaChatTab, Error,
			TEXT("[чат-вкладки] 8-я строка: узла `CHAT_TAB_WIN_B2_C1_CHK00` нет в WBP — ")
			TEXT("галочка канала 8-й строки не появится"));
	}

	// ── Подпись 8-й строки (2628): клон T1 в ТОТ ЖЕ канвас `CHAT_TAB_WIN_B2_T1_grp`, Y +175 ──────
	if (!TabEditorWidget->GetWidgetFromName(TEXT("CHAT_TAB_WIN_B2_T8")) && SrcLabel && SrcLabel->GetParent())
	{
		UPanelWidget* Par = SrcLabel->GetParent();
		if (UTextBlock* T8 = TabEditorWidget->WidgetTree->ConstructWidget<UTextBlock>(
				UTextBlock::StaticClass(), FName(TEXT("CHAT_TAB_WIN_B2_T8"))))
		{
			T8->SetFont(SrcLabel->GetFont());
			T8->SetColorAndOpacity(SrcLabel->GetColorAndOpacity());
			// Выравнивание у подписей каналов эталонное — влево (JUI TextAlign 5 у 2621…2628 задаёт
			//   вертикаль; горизонталь у скина `Black` — от левого края rect'а).
			T8->SetJustification(ETextJustify::Left);
			T8->SetAutoWrapText(false);
			T8->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
			if (UCanvasPanelSlot* Src = AsdaUi::SlotOf(SrcLabel))
			{
				if (UCanvasPanelSlot* CS = Cast<UCanvasPanelSlot>(Par->AddChild(T8)))
				{
					CS->SetAnchors(Src->GetAnchors());
					CS->SetAlignment(Src->GetAlignment());
					FMargin O = Src->GetOffsets();
					O.Top += (kRowLabelY[7] - kRowLabelY[0]);
					CS->SetOffsets(O);
					CS->SetZOrder(61);
				}
			}
			EditRow8Label = T8;
		}
	}

	// Список вкладок 2597 — столбец клон-ячеек (см. BuildTabEditorList).
	BuildTabEditorList();

	bTabEditorBuilt = true;
	UE_LOG(LogAsdaChatTab, Log, TEXT("[чат-вкладки] окно 2590 построено (%s)"), kTabEditorWbp);
}

// Разложить строки каналов и список вкладок по рабочему буферу.
void UAsdaChatWindowSubsystem::RefreshTabEditor()
{
	if (!TabEditorWidget) { return; }
	UGameInstance* GI = GetGameInstance();
	UAsdaSoundSubsystem* Snd = GI ? GI->GetSubsystem<UAsdaSoundSubsystem>() : nullptr;

	// ── 8 строк каналов: галочка / подпись / образец цвета ───────────────────────────────────────
	for (int32 Row = 0; Row < kTabEditorRows; ++Row)
	{
		const int32 Chan = ChanIndexForRow(Row);
		const bool bHasChan = ChanOptsEdit.IsValidIndex(Chan);

		// Подпись канала: эталон при refresh берёт её из ШАБЛОНА `gap_27C[24*chan]`.
		//   ★ Строка 8 (idnum 2628) имеет пустой `ID` в JUI — её подпись мы достроили сами
		//   (`EditRow8Label`), поэтому ищем сначала по имени, потом среди достроенных.
		UTextBlock* T = Cast<UTextBlock>(TabEditorWidget->GetWidgetFromName(
			FName(*FString::Printf(TEXT("CHAT_TAB_WIN_B2_T%d"), Row + 1))));
		if (!T && Row == kTabEditorRows - 1) { T = EditRow8Label; }
		if (T)
		{
			T->SetVisibility(bHasChan ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Hidden);
			if (bHasChan) { T->SetText(FText::FromString(UiChannelText(SlotNameLocId(Chan)))); }
		}
		// Галочка «канал виден во вкладке» — бит маски РЕДАКТИРУЕМОЙ вкладки (эталон читает зеркало).
		const bool bOn = bHasChan && ChatTabsEdit.IsValidIndex(EditTabIdx)
		              && ((ChatTabsEdit[EditTabIdx].Mask >> Chan) & 1) != 0;
		UWidget* C = TabEditorWidget->GetWidgetFromName(
			FName(*FString::Printf(TEXT("CHAT_TAB_WIN_B2_C%d"), Row + 1)));
		if (!C && Row == kTabEditorRows - 1) { C = EditRow8Check; }
		if (C)
		{
			// ★★★ ЖИВОЙ ТЕСТ 2026-08-06 (дефект «галочка моргает и снова ставится, а после
			//   переоткрытия окна ДЕЙСТВИТЕЛЬНО снята»). ПРИЧИНА — ДВА обработчика одного клика:
			//     1) наш единый диспетчер `TickTabEditorPointer` срабатывает на ФРОНТЕ НАЖАТИЯ ЛКМ,
			//        инвертирует бит маски и через `RefreshTabEditor` ставит НОВОЕ состояние кадра;
			//     2) сам `SCheckBox` (виджет был `Visible`, т.е. хит-таргет) на ОТПУСКАНИИ ЛКМ
			//        переключает СВОЁ внутреннее состояние — и возвращает кадр к прежнему.
			//   Модель (маска вкладки) при этом верна — потому переоткрытие окна показывало правду.
			//   ЛЕЧЕНИЕ: галочка становится ЧИСТЫМ ИНДИКАТОРОМ состояния — `HitTestInvisible`.
			//   Так же устроен и оригинал: там это вообще не «виджет-кнопка», клик по контролу
			//   разбирает `uiControlHitTest` (§B.1 ui_window_drag_and_hittest.md), а не сам контрол.
			//   ★ Мировой клик сквозь окно НЕ появляется: хит-тест окна (`HitTestWidgetTree`)
			//   отбрасывает только `Collapsed`/`Hidden`, а `HitTestInvisible` учитывает как обычно.
			//   ★ ПОПРАВКА 2026-08-08: кадры наведения/нажатия у скина `Box_Check` ЕСТЬ (SS1 — тот же
			//   UV с Diffuse FFFFFFFF против приглушённого FFE6E6E6 кадра SS0), просто импортёр их
			//   не переносил (дефект Д6, теперь закрыт). Здесь галочка всё равно чистый ИНДИКАТОР
			//   (`HitTestInvisible`, клик разбирает единый диспетчер), поэтому hover-кадр к ней
			//   не применяется — и это соответствует оригиналу.
			C->SetVisibility(bHasChan ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden);
			// ★★★ ЖИВОЙ ТЕСТ 2026-08-06: «галочки „Вид“ пустые». ПРИЧИНА — состояние показывалось
			//   ПРОЗРАЧНОСТЬЮ (`SetRenderOpacity(0.25/1.0)`), а скин `Box_Check` рисует отметку
			//   ОТДЕЛЬНЫМ КАДРОМ (SS0 пустой бокс / SS2 с галочкой) — импортёр так и собрал
			//   `UCheckBox` со стилем «unchecked/checked UV». Полупрозрачный пустой бокс галочкой
			//   не становится никогда. Ставим НАСТОЯЩЕЕ состояние — движок сам меняет кадр.
			if (UCheckBox* Cb = Cast<UCheckBox>(C))
			{
				Cb->SetIsChecked(bOn);
				Cb->SetRenderOpacity(1.f);
				// ★ Виджет — только индикатор (мышь у него забрана выше), но выключать его НЕЛЬЗЯ:
				//   выключенный Slate рисует обесцвеченным. Держим включённым явно.
				Cb->SetIsEnabled(true);
			}
			else
			{
				// Контрол оказался не чекбоксом (перегенерация окна другим классом) — состояние
				//   показываем прозрачностью и ГРОМКО пишем об этом в лог, а не молчим.
				C->SetRenderOpacity(bOn ? 1.f : 0.25f);
				UE_LOG(LogAsdaChatTab, Warning,
					TEXT("[чат-вкладки] строка %d: контрол 261%d — %s, а не UCheckBox; кадр галочки скина ")
					TEXT("Box_Check недоступен, состояние показано прозрачностью"),
					Row, Row, *C->GetClass()->GetName());
			}
			// ★ Курсор-«рука» на индикаторе больше не ставим: `HitTestInvisible`-виджет курсор не
			//   меняет в принципе, а в оригинале над контролами игры курсор и не менялся —
			//   он там свой, отрисованный игрой (`no-invented-ui`). Пустой вызов убран, чтобы не
			//   выглядел работающим.
		}
		// Образец цвета канала — из РАБОЧЕЙ копии (её и правит палитра). [Б2]: индекс тот же.
		//   ★ Рисует его НАША заливка (см. BuildTabEditor): импортёр сделал контрол 2641+i
		//   `UTextBlock` (скин `NoImage_icon` имеет `Type = "Text"`), покрасить его было нельзя.
		if (UImage* Col = EditRowSwatches.IsValidIndex(Row) ? EditRowSwatches[Row].Get() : nullptr)
		{
			Col->SetVisibility(bHasChan ? ESlateVisibility::Visible : ESlateVisibility::Hidden);
			if (bHasChan)
			{
				// Цвет из ChatOpt.bin — sRGB (заливка контрола эталона), в UE через FromSRGBColor.
				Col->SetColorAndOpacity(FLinearColor::FromSRGBColor(ChanOptsEdit[Chan].Color));
			}
		}
	}
	(void)Snd;

	// ── Список вкладок 2597: СТОЛБЕЦ клон-ячеек (эталон `uiListAppendClonedCell` на каждую вкладку) ──
	RefreshTabEditorList();

	UE_LOG(LogAsdaChatTab, Verbose,
		TEXT("[чат-вкладки] редактор: вкладка %d/%d, прокрутка каналов %d, маска 0x%03X"),
		EditTabIdx, ChatTabsEdit.Num(), EditChanScroll,
		ChatTabsEdit.IsValidIndex(EditTabIdx) ? ChatTabsEdit[EditTabIdx].Mask : 0);
}

// ══════════ СПИСОК ВКЛАДОК 2597 — СТОЛБЕЦ КЛОН-ЯЧЕЕК ══════════════════════════════════════════
// ЭТАЛОН (`chat_tab_editor_2590_re.md` §1.3/§1.4/§5.3/§6.2):
//   • 2597 `CHAT_TAB_WIN_B_button` — НЕ кнопка, а СПИСОК: ячейки клонируются по числу вкладок
//     (`uiListAppendClonedCell(this[23])` в `chatAddFilterEntry`, в загрузке файла и в сбросе),
//     подписи всех `tabCount` ячеек переписывает `chatListBoxPopulate` (chat.c:1957) из
//     `tabRecordList[i].name`;
//   • клон = копия ПОСЛЕДНЕЙ ячейки, сдвинутая на её высоту (`uiListAppendClonedCell @0x12F5C60`:
//     `cellHeight = this[498] − this[496]`, ветка «не горизонтальная раскладка»);
//   • выбор строки: `msgCode 66817, ctrlId 0xA25 (2597)` → `pThis[109] = itemIndex;
//     uiRefreshSkillSlotList(this, 0)` (ui_core.c:12348-12350) — то есть выбранная строка задаёт
//     РЕДАКТИРУЕМУЮ вкладку (`stateFlags1`), а СНИМОК live→edit при этом НЕ пересоздаётся (a2 = 0);
//   • прокрутка: скроллбар 2598 (`CHAT_TAB_WIN_B_listbox`), `msgCode 0x17, ctrlId 2598` →
//     `uiControlScrollSelection(2597, …, wparam) + chatListBoxPopulate` (ui_core.c:12389-12395);
//     показывается/прячется он в `uiList_UpdateScrollArrow(this, 1)` (ui_core.c:11838-11846):
//     видим ТОЛЬКО когда число ячеек больше вместимости списка.
//
// ★ ОТСТУПЛЕНИЕ (ровно то же и в ряду вкладок 2501, дополнение 11): собственного СКИНА у стрелок
//   прокрутки в JuiSkin.ini нет — в оригинале их рисует движок. Собираем их из ТОГО ЖЕ скина
//   TalkBar_2 узкими плитками с подписями «▲» / «▼» шириной ровно в полосу 2598 справа от ячеек.
//   Механизм один и тот же для обоих списков — второго не заводим.
void UAsdaChatWindowSubsystem::BuildTabEditorList()
{
	if (bTabEditorListBuilt || !TabEditorWidget || !TabEditorWidget->WidgetTree) { return; }

	// ── 1. ОБРАЗЕЦ. Импортёр развернул RadioButton-контрол 2597 в три виджета: план-картинку
	//   `CHAT_TAB_WIN_B_button_<порядковый>_0` (UImage), прозрачную клик-кнопку `..._OPT0` (UButton)
	//   и подпись `..._Text` (UTextBlock). Точное имя есть только у последних двух, у плана —
	//   порядковый номер в дереве, поэтому ищем по ПРЕФИКСУ + классу.
	UImage*     SrcCell = Cast<UImage>(FindByPrefix(TabEditorWidget, TEXT("CHAT_TAB_WIN_B_button"), UImage::StaticClass()));
	UTextBlock* SrcCap  = Cast<UTextBlock>(TabEditorWidget->GetWidgetFromName(TEXT("CHAT_TAB_WIN_B_button_Text")));
	UCanvasPanelSlot* CellSlot = AsdaUi::SlotOf(SrcCell);
	UCanvasPanelSlot* CapSlot  = AsdaUi::SlotOf(SrcCap);
	if (CellSlot)
	{
		// ★★★ ЗАКОН jui-align-anchors: анкор рантайм-виджета обязан СОВПАДАТЬ со схемой импортёра.
		//   Здесь он совпадает буквально — мы клонируем СЛОТ образца (анкоры, выравнивание,
		//   локальные координаты его группы) и меняем только Y на шаг ячейки. Ровно как эталонный
		//   `uiListAppendClonedCell`: memcpy последней ячейки + сдвиг rect на cellHeight.
		EditTabCellHost = SrcCell->GetParent();
		EditTabCellAnch = CellSlot->GetAnchors();
		EditTabCellOff  = CellSlot->GetOffsets();
		EditTabCapOff   = CapSlot ? CapSlot->GetOffsets()
		                          : FMargin(EditTabCellOff.Left, EditTabCellOff.Top + 5.f, kEditCellW, 14.f);
		UE_LOG(LogAsdaChatTab, Log,
			TEXT("[чат-вкладки] список 2597: образец «%s» в группе «%s», слот анкор [%.3f,%.3f] off(L%.1f T%.1f R%.1f B%.1f)"),
			*SrcCell->GetName(), EditTabCellHost ? *EditTabCellHost->GetName() : TEXT("—"),
			EditTabCellAnch.Minimum.X, EditTabCellAnch.Minimum.Y,
			EditTabCellOff.Left, EditTabCellOff.Top, EditTabCellOff.Right, EditTabCellOff.Bottom);
	}
	else
	{
		// Образца нет (окно перегенерировано с другими именами) — кладём столбец на КОРНЕВОЙ канвас
		//   окна по эталонным ABS-числам JUI. Схема слота при этом POINT (fx 0, fy 0) — ровно та,
		//   что даёт `compute_anchors` для всех контролов окна 2590 (прогон на gamemain_jui.ini).
		EditTabCellHost = nullptr;
		EditTabCellAnch = FAnchors(0.f, 0.f, 0.f, 0.f);
		EditTabCellOff  = FMargin(kEditListX, kEditListY, kEditCellW, kEditCellH);
		EditTabCapOff   = FMargin(kEditListX, kEditListY + 5.f, kEditCellW, 14.f);
		UE_LOG(LogAsdaChatTab, Warning,
			TEXT("[чат-вкладки] список 2597: образец-ячейка в WBP не найдена — столбец строится по ")
			TEXT("эталонным координатам JUI (427, 266) 79×25, шаг 25"));
	}

	// ── 2. Погасить ВЕСЬ импортированный контрол 2597 (план + клик-кнопка + подпись): столбец
	//   рисуем сами, иначе одиночная ячейка осталась бы висеть под первой строкой.
	if (TabEditorWidget->WidgetTree)
	{
		TArray<UWidget*> All;
		TabEditorWidget->WidgetTree->GetAllWidgets(All);
		for (UWidget* W : All)
		{
			if (W && W->GetName().StartsWith(TEXT("CHAT_TAB_WIN_B_button")))
			{
				W->SetVisibility(ESlateVisibility::Collapsed);
			}
		}
	}

	// ── 3. Ячейки и стрелки прокрутки ────────────────────────────────────────────────────────────
	EditTabCells.Reset();
	EditTabCellCaps.Reset();
	bTabEditorListBuilt = true;   // ставим ДО EnsureTabEditorCells: она сама ничего не строит повторно
	EnsureTabEditorCells(EditTabVisibleCount());
	UPanelWidget* Host = EditTabCellHost;
	EditTabArrowUp    = MakeTabQuad(TabEditorWidget, Host, TEXT("CHAT_TAB_WIN_B_ArrowUp"),      120);
	EditTabArrowDn    = MakeTabQuad(TabEditorWidget, Host, TEXT("CHAT_TAB_WIN_B_ArrowDn"),      120);
	EditTabArrowUpCap = MakeTabCap (TabEditorWidget, Host, TEXT("CHAT_TAB_WIN_B_ArrowUp_Text"), 121);
	EditTabArrowDnCap = MakeTabCap (TabEditorWidget, Host, TEXT("CHAT_TAB_WIN_B_ArrowDn_Text"), 121);
	if (EditTabArrowUpCap) { EditTabArrowUpCap->SetText(FText::FromString(TEXT("▲"))); }
	if (EditTabArrowDnCap) { EditTabArrowDnCap->SetText(FText::FromString(TEXT("▼"))); }

	UE_LOG(LogAsdaChatTab, Log,
		TEXT("[чат-вкладки] список 2597 собран: вкладок %d, ячеек %d, вмещается %d ")
		TEXT("(высота полосы 2598 = %.0f / высота ячейки %.0f)"),
		ChatTabsEdit.Num(), EditTabCells.Num(), EditTabVisibleCount(), kEditScrollH, kEditCellH);
}

// Довести число ячеек столбца до N. Ячейки НЕ удаляются при уменьшении списка — лишние просто
//   Collapsed (тот же приём, что у ряда вкладок 2501: `EnsureTabWidgets`).
void UAsdaChatWindowSubsystem::EnsureTabEditorCells(int32 N)
{
	if (!bTabEditorListBuilt || !TabEditorWidget) { return; }
	UPanelWidget* Host = EditTabCellHost;
	for (int32 i = EditTabCells.Num(); i < N; ++i)
	{
		EditTabCells.Add(MakeTabQuad(TabEditorWidget, Host,
			*FString::Printf(TEXT("CHAT_TAB_WIN_B_cell%d"), i), 100));
		EditTabCellCaps.Add(MakeTabCap(TabEditorWidget, Host,
			*FString::Printf(TEXT("CHAT_TAB_WIN_B_cell%d_Text"), i), 101));
	}
}

// ★★★ СКОЛЬКО ЯЧЕЕК ВЛЕЗАЕТ В СПИСОК 2597 — ЭТАЛОННАЯ ФОРМУЛА, а не выдумка.
//   `CChat_BuildChatTabUI` (chat.c:1095-1097):
//        rect = vtbl[6](chatTabBScroll /*2598*/);
//        uiControlCountFittingChildren(chatTabBButton /*2597*/, 1, *(DWORD *)(rect + 4));
//   Тело (`ui_core.c:115677-115722`) при a2 == 1 накапливает ВЫСОТЫ детей и увеличивает счётчик,
//   пока сумма не превысила лимит ⇒ floor(экстент / высота ячейки).
//   ★ ЧТО ТАКОЕ `rect + 4`. Соседний вызов для ряда вкладок 2501 (chat.c:790) берёт `rect + 0`
//   и вычитает 10; если бы поле 0 было «left», для окна 2500 (ABS x = 1) вышло бы (1 − 10) < 0,
//   то есть НОЛЬ вкладок, тогда как оригинал показывает пять = floor((474 − 10) / 79). Значит
//   поле 0 — ШИРИНА, а поле 4 — ВЫСОТА. Для 2598 это 200 ⇒ floor(200 / 25) = **8 ячеек**.
//   То же число независимо подтверждает список каналов: скроллбар 2639 такой же высоты 200 при
//   шаге строки 25, и эталонный цикл строк там ровно `for (tabIdx = 0; tabIdx < 8; ++tabIdx)`.
int32 UAsdaChatWindowSubsystem::EditTabVisibleCount() const
{
	const int32 N = (int32)FMath::FloorToFloat(kEditScrollH / kEditCellH);
	return FMath::Clamp(N, 1, FMath::Max(1, ChatTabsEdit.Num()));
}

// Разложить столбец: ячейки, подписи, состояния скина, стрелки прокрутки.
void UAsdaChatWindowSubsystem::RefreshTabEditorList()
{
	if (!TabEditorWidget) { return; }
	BuildTabEditorList();

	const int32 Vis = EditTabVisibleCount();
	EditTabScroll = FMath::Clamp(EditTabScroll, 0, FMath::Max(0, ChatTabsEdit.Num() - Vis));
	EditTabIdx    = FMath::Clamp(EditTabIdx,    0, FMath::Max(0, ChatTabsEdit.Num() - 1));
	EnsureTabEditorCells(Vis);

	// Слот клона = слот образца со сдвигом по Y на i шагов высоты ячейки (uiListAppendClonedCell).
	auto PlaceClone = [this](UWidget* W, const FMargin& Base, float DY, float OverrideX, float OverrideW)
	{
		UCanvasPanelSlot* CS = AsdaUi::SlotOf(W);
		if (!CS) { return; }
		CS->SetAnchors(EditTabCellAnch);
		CS->SetAlignment(FVector2D(0.f, 0.f));
		CS->SetOffsets(FMargin(OverrideX >= 0.f ? OverrideX : Base.Left, Base.Top + DY,
		                       OverrideW >  0.f ? OverrideW : Base.Right, Base.Bottom));
	};

	for (int32 i = 0; i < EditTabCells.Num(); ++i)
	{
		const int32 Tab   = EditTabScroll + i;
		const bool  bShow = (i < Vis) && ChatTabsEdit.IsValidIndex(Tab);
		const float DY    = kEditCellH * (float)i;
		if (UImage* Q = EditTabCells[i])
		{
			Q->SetVisibility(bShow ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
			PlaceClone(Q, EditTabCellOff, DY, -1.f, -1.f);
			if (bShow)
			{
				// ★ ЧЕТЫРЕ КАДРА СКИНА TalkBar_2 (JuiSkin.ini, отчёт §9): SS0 обычный,
				//   SS1 ВЫБРАННЫЙ (Diffuse FFFF8000, подпись FFFFFFFF), SS2/SS3 наведение.
				//   Подсветку строки здесь рисует САМ СКИН ячейки — отдельная плита-TEXTUREFACTOR
				//   движка (0x96F0F0F0 / 0xFFFFFFFF) нужна спискам БЕЗ кадров выделения
				//   (лента 2521, список каналов 2608); у TalkBar_2 кадры есть, и ряд вкладок 2501
				//   уже отрисован ими же — механизм у обоих списков ОДИН.
				FSlateBrush Br = Q->GetBrush();
				const bool bSel = (Tab == EditTabIdx);
				const bool bHov = (Tab == EditTabHoverIdx) && !bSel;
				Br.SetUVRegion(bSel ? FBox2f(FVector2f(0.840f, 0.430f), FVector2f(0.994f, 0.479f))
				             : (bHov ? FBox2f(FVector2f(0.840f, 0.484f), FVector2f(0.994f, 0.533f))
				                     : FBox2f(FVector2f(0.840f, 0.375f), FVector2f(0.994f, 0.424f))));
				Br.TintColor = FSlateColor(FLinearColor::FromSRGBColor(
					bSel ? FColor(0xFF, 0x80, 0x00, 0xFF)
					     : (bHov ? FColor(0xFF, 0xC0, 0x88, 0xFF) : FColor(0xFF, 0xFF, 0xFF, 0xFF))));
				Q->SetBrush(Br);
			}
		}
		if (UTextBlock* Cp = EditTabCellCaps.IsValidIndex(i) ? EditTabCellCaps[i].Get() : nullptr)
		{
			Cp->SetVisibility(bShow ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
			PlaceClone(Cp, EditTabCapOff, DY, -1.f, -1.f);
			if (bShow)
			{
				// Подпись ячейки — из РАБОЧЕЙ копии (`chatListBoxPopulate` берёт её из списка вкладок).
				const FString N = (ChatTabsEdit[Tab].LocId > 0) ? UiChannelText(ChatTabsEdit[Tab].LocId)
				                                                : ChatTabsEdit[Tab].Name;
				Cp->SetText(FText::FromString(N));
				// CapColor кадров скина: SS0 FFFFF9AC, SS1 FFFFFFFF, SS2 FFFFFF80.
				const bool bSel = (Tab == EditTabIdx);
				const bool bHov = (Tab == EditTabHoverIdx) && !bSel;
				Cp->SetColorAndOpacity(FSlateColor(FLinearColor::FromSRGBColor(
					bSel ? FColor(0xFF, 0xFF, 0xFF, 0xFF)
					     : (bHov ? FColor(0xFF, 0xFF, 0x80, 0xFF) : FColor(0xFF, 0xF9, 0xAC, 0xFF)))));
			}
		}
	}

	// ── Стрелки прокрутки = аналог скроллбара 2598. Эталон (`uiList_UpdateScrollArrow(this, 1)`)
	//   показывает его РОВНО когда ячеек больше, чем вмещает список, и прячет иначе.
	const bool bNeed = (Vis < ChatTabsEdit.Num());
	const float ArrowX = EditTabCellOff.Left + kEditCellW;                  // полоса 2598 справа от ячеек
	const float ArrowDYDn = kEditCellH * (float)FMath::Max(0, Vis - 1);     // нижняя — у последней строки
	auto ShowArrow = [&](UImage* Q, UTextBlock* Cp, float DY, bool bEnabled, int32 ArrowId)
	{
		if (Q)
		{
			Q->SetVisibility(bNeed ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
			PlaceClone(Q, EditTabCellOff, DY, ArrowX, kEditArrowW);
			FSlateBrush Br = Q->GetBrush();
			Br.SetUVRegion(FBox2f(FVector2f(0.840f, 0.484f), FVector2f(0.994f, 0.533f)));
			const bool bHovA = bEnabled && (EditArrowHoverIdx == ArrowId);
			Br.TintColor = FSlateColor(FLinearColor::FromSRGBColor(
				!bEnabled ? FColor(0xFF, 0xC0, 0x88, 0x80)
				          : (bHovA ? FColor(0xFF, 0xE0, 0xB0, 0xFF) : FColor(0xFF, 0xC0, 0x88, 0xFF))));
			Q->SetBrush(Br);
		}
		if (Cp)
		{
			Cp->SetVisibility(bNeed ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
			PlaceClone(Cp, EditTabCapOff, DY, ArrowX, kEditArrowW);
			Cp->SetColorAndOpacity(FSlateColor(FLinearColor::FromSRGBColor(
				bEnabled ? FColor(0xFF, 0xFF, 0x80, 0xFF) : FColor(0xFF, 0xFF, 0x80, 0x60))));
		}
	};
	ShowArrow(EditTabArrowUp, EditTabArrowUpCap, 0.f,        EditTabScroll > 0,                    0);
	ShowArrow(EditTabArrowDn, EditTabArrowDnCap, ArrowDYDn,  EditTabScroll + Vis < ChatTabsEdit.Num(), 1);

	UE_LOG(LogAsdaChatTab, Verbose,
		TEXT("[чат-вкладки] список 2597: вкладок %d, вмещается %d, первая видимая %d, редактируется %d, стрелки %s"),
		ChatTabsEdit.Num(), Vis, EditTabScroll, EditTabIdx, bNeed ? TEXT("показаны") : TEXT("СКРЫТЫ (влезли все)"));
}

// Индекс вкладки под курсором в списке 2597 (−1 = мимо).
int32 UAsdaChatWindowSubsystem::EditTabCellHitTest(const FVector2D& Cur) const
{
	for (int32 i = 0; i < EditTabCells.Num(); ++i)
	{
		const UImage* Q = EditTabCells[i];
		if (!Q || Q->GetVisibility() == ESlateVisibility::Collapsed) { continue; }
		if (IsUnderCursor(Q, Cur)) { return EditTabScroll + i; }
	}
	return -1;
}

// Прокрутка столбца (эталон: `uiControlScrollSelection(2597, …, wparam) + chatListBoxPopulate`).
void UAsdaChatWindowSubsystem::ScrollTabEditorList(int32 Delta)
{
	const int32 Max = FMath::Max(0, ChatTabsEdit.Num() - EditTabVisibleCount());
	const int32 New = FMath::Clamp(EditTabScroll + Delta, 0, Max);
	if (New == EditTabScroll) { return; }
	EditTabScroll = New;
	RefreshTabEditorList();
	UE_LOG(LogAsdaChatTab, Log, TEXT("[чат-вкладки] список 2597: прокрутка → первая видимая %d из %d"),
		EditTabScroll, ChatTabsEdit.Num());
}

// ══════ ★★★ КОЛЕСО НАД РЕДАКТОРОМ ВКЛАДОК 2590: список вкладок 2597 и список каналов 2639 ══════
// Обоснование и разбор эталона — в `OnWheelChatWindow` (AsdaChatWindowSubsystem.cpp): оригинал
//   колесо интерфейсу ОТДАЁТ (очередь типа 7 → виртуальный слот 7 менеджера), но поведение слота
//   для конкретного списка из разобранного среза не выводится ⇒ шаг берём равным шагу ШТАТНЫХ
//   стрелок этого же списка (1 позиция), чтобы колесо и стрелки не расходились.
// ★ Отличие от клика по полосе 2639: клик ЦИКЛИТ смещение (`(x+1) % (Max+1)` — так листает
//   эталонный ползунок), а колесо двигает на 1 С КЛАМПОМ — крутить список по кругу колесом
//   противоестественно. Это осознанное различие, а не рассогласование.
bool UAsdaChatWindowSubsystem::OnWheelTabEditor(int32 Notches)
{
	if (!TabEditorWidget || Notches == 0) { return false; }
	if (!FSlateApplication::IsInitialized()) { return false; }
	const FVector2D Cur = FSlateApplication::Get().GetCursorPos();
	{
		UGameInstance* GI = GetGameInstance();
		if (UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
		{
			if (WM->IsPointerBlockedByTopmost(kTabEditorId)) { return false; }
		}
	}

	// ── Столбец вкладок 2597 (ячейки + обе стрелки полосы 2598) ──
	bool bOverTabList = (EditTabArrowUp && EditTabArrowUp->GetVisibility() != ESlateVisibility::Collapsed
	                     && IsUnderCursor(EditTabArrowUp, Cur))
	                 || (EditTabArrowDn && EditTabArrowDn->GetVisibility() != ESlateVisibility::Collapsed
	                     && IsUnderCursor(EditTabArrowDn, Cur));
	if (!bOverTabList)
	{
		for (const TObjectPtr<UImage>& Q : EditTabCells)
		{
			if (Q && Q->GetVisibility() != ESlateVisibility::Collapsed && IsUnderCursor(Q, Cur))
			{
				bOverTabList = true;
				break;
			}
		}
	}
	if (bOverTabList)
	{
		const int32 Before = EditTabScroll;
		ScrollTabEditorList(-Notches);
		UE_LOG(LogAsdaChatTab, Verbose, TEXT("[колесо] список вкладок 2597: %+d щелч. → %d (было %d)"),
			Notches, EditTabScroll, Before);
		return true;
	}

	// ── Список каналов: полоса прокрутки 2639 + сами 8 строк каналов ──
	const int32 MaxScroll = FMath::Max(0, ChanOptsEdit.Num() - kTabEditorRows);
	if (MaxScroll > 0)
	{
		bool bOverChanList = IsUnderCursor(FindByPrefix(TabEditorWidget, TEXT("listCHAT_TAB_WIN_B2_ListBox")), Cur);
		if (!bOverChanList)
		{
			for (const TObjectPtr<UImage>& Sw : EditRowSwatches)
			{
				if (Sw && Sw->GetVisibility() != ESlateVisibility::Collapsed && IsUnderCursor(Sw, Cur))
				{
					bOverChanList = true;
					break;
				}
			}
		}
		if (bOverChanList)
		{
			const int32 New = FMath::Clamp(EditChanScroll - Notches, 0, MaxScroll);
			if (New == EditChanScroll) { return true; }      // упёрлись — колесо всё равно наше
			EditChanScroll = New;
			RefreshTabEditor();
			UE_LOG(LogAsdaChatTab, Verbose, TEXT("[колесо] список каналов 2639: %+d щелч. → смещение %d из %d"),
				Notches, EditChanScroll, MaxScroll);
			return true;
		}
	}
	return false;
}

// Найти виджет по точному имени, а если такого нет — по префиксу (и, если задан, по классу).
//   ★ Точное имя импортёр даёт только интерактив-контролам и меткам; ПЛАНЫ статик-узлов называются
//   `<idc>_<порядковый в дереве>_<номер плана>`, и точного имени у них нет в принципе.
UWidget* UAsdaChatWindowSubsystem::FindByPrefix(UUserWidget* Host, const TCHAR* Prefix, UClass* OfClass)
{
	if (!Host || !Host->WidgetTree) { return nullptr; }
	if (UWidget* Exact = Host->GetWidgetFromName(FName(Prefix)))
	{
		if (!OfClass || Exact->IsA(OfClass)) { return Exact; }
	}
	TArray<UWidget*> All;
	Host->WidgetTree->GetAllWidgets(All);
	for (UWidget* W : All)
	{
		if (!W || !W->GetName().StartsWith(Prefix)) { continue; }
		if (OfClass && !W->IsA(OfClass)) { continue; }
		return W;
	}
	return nullptr;
}

// ══════════════════════════ ОКНО ВЫБОРА ЦВЕТА 2700 ════════════════════════════════════════════
// Эталон: клик по образцу 2641+i → `stateFlags2 = i`, запомнить ТЕКУЩИЙ цвет, показать 2700;
//   плитка 2711..2730 → «выбранный цвет»; ОК 2735 → записать в рабочую копию и в образец;
//   Отмена 2736 и крестик 2739 → просто закрыть.
// ★ [Б2] И ЧТЕНИЕ, И ЗАПИСЬ идут по ОДНОМУ индексу `ChanIndexForRow(Row)` (в оригинале чтение шло
//   без учёта прокрутки, а запись — с учётом; при прокрученном списке менялся не тот канал).
void UAsdaChatWindowSubsystem::ShowColorSelect(int32 Row)
{
	UGameInstance* GI = GetGameInstance();
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM) { return; }
	EditColorRow = Row;
	const int32 Chan = ChanIndexForRow(Row);
	// ★ [Б2] И «исходный», и стартовый «выбранный» берём по ТОМУ ЖЕ индексу, по которому потом
	//   запишет ОК (в оригинале чтение шло без прокрутки, а запись — с ней).
	EditColorOrig   = ChanOptsEdit.IsValidIndex(Chan) ? ChanOptsEdit[Chan].Color : FColor::White;
	EditColorPicked = EditColorOrig;

	// ★★★ Тот же гвард, что у окна 2590 выше: после `CloseAll()` (вход в мир) указатель на
	//   палитру 2700 жив, а окна нет — по проверке на null палитра больше не открывалась.
	//   `HideColorSelect` снимает и производное (плитки палитры, превью цвета), живущее в окне.
	if (!WM->IsWindowLive(ColorWidget))
	{
		if (ColorWidget) { HideColorSelect(); }
		ColorWidget = WM->OpenWindowByPath(kColorSelectId, kColorWbp, 830, ESlateVisibility::Visible, /*bModal*/false);
		if (!ColorWidget) { return; }
		// ★★★ ПАЛИТРА — ДОЧЕРНЕЕ ОКНО РЕДАКТОРА 2590 (эталон `uiShowChildWindow @0xBA05D0`
		//   регистрирует её верхним окном через `CClass_MapInsert`). Без этой связи клик по
		//   образцу цвета поднимал редактор ПОВЕРХ только что открытой палитры — это и был
		//   дефект «окно настройки цвета уходит на задний план» (разбор — в BringWindowToFront).
		WM->SetWindowOwner(kColorSelectId, kTabEditorId);
		UAsdaTextSubsystem* Txt = GI ? GI->GetSubsystem<UAsdaTextSubsystem>() : nullptr;
		auto SetTxt = [&](const TCHAR* Node, int32 LocId)
		{
			if (UTextBlock* T = Cast<UTextBlock>(ColorWidget->GetWidgetFromName(FName(Node))))
			{
				const FString S = Txt ? Txt->GetTextById(LocId) : FString();
				if (!S.IsEmpty()) { T->SetText(FText::FromString(S)); }
				T->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
			}
		};
		SetTxt(TEXT("CHAT_Color_Select_T_Move"), 5324);   // «Цвета чата»
		SetTxt(TEXT("CHAT_Color_Select_Ct_1"),   5307);   // «Выбранный цвет»
		SetTxt(TEXT("CHAT_Color_Select_Ct_2"),   5308);   // «Исходный цвет»
		SetTxt(TEXT("CHAT_Color_Select_OK_Text"), 2288);
		SetTxt(TEXT("CHAT_Color_Select_No_Text"), 2289);

		auto Bind = [&](const TCHAR* Node, TFunction<void()> Fn)
		{
			if (UButton* B = Cast<UButton>(ColorWidget->GetWidgetFromName(FName(Node))))
			{
				B->OnClicked.Clear();
				UAsdaButtonRelay* R = NewObject<UAsdaButtonRelay>(this);
				R->Callback = [Fn](int32) { Fn(); };
				B->OnClicked.AddDynamic(R, &UAsdaButtonRelay::OnClicked);
				AsdaUi::AnchorRelay(TabEditorRelays, R, B);
			}
		};
		Bind(TEXT("CHAT_Color_Select_OK"), [this]()
		{
			// ★ [Б2] запись по тому же индексу, что и чтение.
			const int32 Ch = ChanIndexForRow(EditColorRow);
			if (ChanOptsEdit.IsValidIndex(Ch))
			{
				ChanOptsEdit[Ch].Color = EditColorPicked;
				// ★★★ ДОКАЗАТЕЛЬНЫЙ ЛОГ (Д28). Живой тест дал «к каналу „Общий чат“ цвет не
				//   применяется», хотя путь цвета для всех слотов один и тот же. Здесь видно
				//   РОВНО в какую запись `tabFilterOpt[slot]` ушёл цвет: строка, прокрутка,
				//   номер слота и его название — если игрок правил не тот канал, это видно сразу.
				UE_LOG(LogAsdaChatTab, Log,
					TEXT("[чат-цвета] ОК палитры: строка %d + прокрутка %d ⇒ СЛОТ %d «%s» := %s ")
					TEXT("(запись tabFilterOpt[%d], эталонный адрес 0x%03X)"),
					EditColorRow, EditChanScroll, Ch, *UiChannelText(SlotNameLocId(Ch)),
					*EditColorPicked.ToString(), Ch, 0x380 + 24 * Ch);
			}
			else
			{
				UE_LOG(LogAsdaChatTab, Error,
					TEXT("[чат-цвета] ОК палитры: индекс канала %d вне таблицы (%d записей) — цвет НЕ записан"),
					Ch, ChanOptsEdit.Num());
			}
			HideColorSelect();
			RefreshTabEditor();
		});
		Bind(TEXT("CHAT_Color_Select_No"), [this]() { HideColorSelect(); });
		Bind(TEXT("CHAT_Color_Select_X"),  [this]() { HideColorSelect(); });

		// ── ★★★ 20 ПЛИТОК ПАЛИТРЫ (2711…2730) — ВСЕ, ЗНАЧЕНИЯ ИЗ ЭТАЛОНА ────────────────────────
		//   Раньше показывались только 10 (цвета каналов), а остальные ЯВНО СКРЫВАЛИСЬ: считалось,
		//   что тела `chatInitColorPalette` в срезе нет. Это был ПРОМАХ ПОИСКА — функция есть
		//   (`RE/etalon/chat.c:667`), просто константы пишутся в буфер `+0x228`, а не в
		//   `colorPalette` (+0x224). Все 20 подтверждены байтами EXE, декомпилом и live.dmp
		//   (`Reports/chat_color_palette_re.md`).
		//   ★ ВТОРАЯ ПРИЧИНА «плитки пустые»: скин плиток `NoImage_icon` имеет `Type = "Text"`,
		//   то есть импортёр создал `UTextBlock` — `Cast<UImage>` всегда давал nullptr и цвет не
		//   ставился НИ РАЗУ. Заливку рисуем своим виджетом, как эталон
		//   (`DrawTextureRect("n_01.tex",56,464,24,12)` + `SetValueAll4(ARGB)`, chat.c:1191-1193).
		PaletteTiles.Reset();
		PaletteTiles.SetNum(kPaletteSlots);
		for (int32 i = 0; i < kPaletteSlots; ++i)
		{
			// Геометрию берём с ПЛАНА контрола (`..._CMark<N>_<i>_0`) — он несёт авторский rect
			//   18×18; одноимённый TextBlock импортёр сдвигает вниз на (h − 14)/2.
			UImage* Plane = FindPlaneCase(ColorWidget, FString::Printf(TEXT("CHAT_Color_Select_CMark%d_"), i + 1));
			if (!Plane) { continue; }
			UImage* Tile = MakeSwatch(ColorWidget, Plane->GetParent(),
				*FString::Printf(TEXT("CHAT_Color_Select_Tile%d"), i), 60);
			if (!Tile) { continue; }
			PlaceOverWidget(Tile, Plane);
			Tile->SetColorAndOpacity(FLinearColor::FromSRGBColor(kPaletteColors[i]));
			Tile->SetCursor(EMouseCursor::Hand);
			PaletteTiles[i] = Tile;
			if (UWidget* Lbl = ColorWidget->GetWidgetFromName(
					FName(*FString::Printf(TEXT("CHAT_Color_Select_CMark%d"), i + 1))))
			{
				Lbl->SetVisibility(ESlateVisibility::Collapsed);   // пустой TextBlock поверх плитки
			}
		}
		// ── Превью 2731 «Выбранный цвет» / 2732 «Исходный цвет» ────────────────────────────────
		//   ★★★ ГРАБЛЯ, НАЙДЕННАЯ ДАМПОМ: их имена различаются ТОЛЬКО РЕГИСТРОМ
		//   (`CHAT_Color_Select_Color_s2` = 2731 и `..._Color_S2` = 2732). `GetWidgetFromName`
		//   сравнивает через `FName` — БЕЗ учёта регистра, поэтому оба запроса возвращали ОДИН И
		//   ТОТ ЖЕ виджет и второе превью не работало в принципе. Более того, TextBlock у них в
		//   дереве фактически один (FName-коллизия при генерации), а ПЛАНЫ — два разных:
		//   `..._Color_s2_77_0` (Top 1) и `..._Color_S2_78_0` (Top 40). Берём планы, сравнивая
		//   префикс С УЧЁТОМ РЕГИСТРА.
		auto MakePreview = [&](const TCHAR* PlanePrefix, const TCHAR* SwName) -> UImage*
		{
			UImage* Plane = FindPlaneCase(ColorWidget, PlanePrefix);
			if (!Plane)
			{
				UE_LOG(LogAsdaChatTab, Error,
					TEXT("[чат-цвета] план превью «%s» в WBP не найден — окно 2700 неполно"), PlanePrefix);
				return nullptr;
			}
			UImage* Pv = MakeSwatch(ColorWidget, Plane->GetParent(), SwName, 60);
			if (Pv) { PlaceOverWidget(Pv, Plane); }
			return Pv;
		};
		ColorPrevPicked = MakePreview(TEXT("CHAT_Color_Select_Color_s2_"), TEXT("CHAT_Color_Select_PrevPicked"));
		ColorPrevOrig   = MakePreview(TEXT("CHAT_Color_Select_Color_S2_"), TEXT("CHAT_Color_Select_PrevOrig"));
		int32 NTiles = 0;
		for (const TObjectPtr<UImage>& T : PaletteTiles) { if (T) { ++NTiles; } }
		UE_LOG(LogAsdaChatTab, Log,
			TEXT("[чат-цвета] окно 2700: плиток %d/%d (значения из chatInitColorPalette @0xB9E110, ")
			TEXT("подтверждены байтами EXE + декомпилом + live.dmp), превью «Выбранный»=%s «Исходный»=%s"),
			NTiles, kPaletteSlots,
			ColorPrevPicked ? TEXT("есть") : TEXT("НЕТ"), ColorPrevOrig ? TEXT("есть") : TEXT("НЕТ"));
		bColorBuilt = true;
	}
	ColorWidget->SetVisibility(ESlateVisibility::Visible);
	RefreshColorSelect();
	UE_LOG(LogAsdaChatTab, Log, TEXT("[чат-цвета] окно 2700 открыто для строки %d (канал %d), исходный цвет %s"),
		Row, Chan, *EditColorPicked.ToString());
}

void UAsdaChatWindowSubsystem::HideColorSelect()
{
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UAsdaWindowSubsystem* WM = GI->GetSubsystem<UAsdaWindowSubsystem>())
		{
			WM->CloseWindow(kColorSelectId);
		}
	}
	ColorWidget = nullptr;
	bColorBuilt = false;
	// Плитки и превью жили внутри окна 2700 — вместе с ним и умирают.
	PaletteTiles.Reset();
	ColorPrevPicked = nullptr;
	ColorPrevOrig = nullptr;
}

// Превью: 2731 «Выбранный цвет» (эталон `CChat+0x228`, this[138]), 2732 «Исходный цвет»
//   (`CChat+0x224`, this[137] — запоминается при клике по образцу строки, `ui_core.c:12500`).
void UAsdaChatWindowSubsystem::RefreshColorSelect()
{
	if (!ColorWidget) { return; }
	if (ColorPrevPicked) { ColorPrevPicked->SetColorAndOpacity(FLinearColor::FromSRGBColor(EditColorPicked)); }
	if (ColorPrevOrig)   { ColorPrevOrig->SetColorAndOpacity(FLinearColor::FromSRGBColor(EditColorOrig)); }
}

// ══════════ МЫШЬ ВНУТРИ РЕДАКТОРА 2590 (единый хит-тест по геометрии, как во всём окне чата) ═══
// ГРАБЛЯ №1/№2 подсистемы: контролы импортёра могут быть ЛЮБОГО класса (UImage вместо UButton),
//   поэтому клики разбираются по геометрии, а не через доставку событий Slate. Тот же приём, что
//   в `TickPointer` основного окна.
void UAsdaChatWindowSubsystem::TickTabEditorPointer(const FVector2D& Cur, bool bLmbEdge)
{
	if (!TabEditorWidget || TabEditorWidget->GetVisibility() == ESlateVisibility::Collapsed) { return; }

	// ── НАВЕДЕНИЕ на ячейки списка 2597 и на стрелки прокрутки — КАЖДЫЙ КАДР (кадр SS2 скина
	//   TalkBar_2). Ровно тот же приём, что у ряда вкладок 2501 (`TabHoverIdx` в TickPointer).
	//   Пока открыта палитра 2700, она забирает мышь целиком — наведение в редакторе гасим.
	{
		const bool bPaletteUp = ColorWidget && ColorWidget->GetVisibility() != ESlateVisibility::Collapsed;
		const int32 NewHover = bPaletteUp ? -1 : EditTabCellHitTest(Cur);
		int32 NewArrow = -1;
		if (!bPaletteUp)
		{
			if (EditTabArrowUp && EditTabArrowUp->GetVisibility() != ESlateVisibility::Collapsed
				&& IsUnderCursor(EditTabArrowUp, Cur)) { NewArrow = 0; }
			else if (EditTabArrowDn && EditTabArrowDn->GetVisibility() != ESlateVisibility::Collapsed
				&& IsUnderCursor(EditTabArrowDn, Cur)) { NewArrow = 1; }
		}
		if (NewHover != EditTabHoverIdx || NewArrow != EditArrowHoverIdx)
		{
			EditTabHoverIdx = NewHover;
			EditArrowHoverIdx = NewArrow;
			RefreshTabEditorList();
		}
	}

	if (!bLmbEdge) { return; }

	UGameInstance* GI = GetGameInstance();
	UAsdaSoundSubsystem* Snd = GI ? GI->GetSubsystem<UAsdaSoundSubsystem>() : nullptr;
	auto Node = [&](const TCHAR* N) { return TabEditorWidget->GetWidgetFromName(FName(N)); };

	// ── Окно палитры 2700 забирает клик первым: оно поверх редактора ─────────────────────────────
	if (ColorWidget && ColorWidget->GetVisibility() != ESlateVisibility::Collapsed)
	{
		for (int32 i = 0; i < PaletteTiles.Num(); ++i)
		{
			UWidget* Tile = PaletteTiles[i];
			if (!IsUnderCursor(Tile, Cur)) { continue; }
			// Эталон: `stateFlags4 = palette[ctrlId−2572]`, превью «Выбранный цвет» обновляется.
			EditColorPicked = kPaletteColors[i];
			RefreshColorSelect();
			if (Snd) { Snd->PlaySound2DById(5); }
			UE_LOG(LogAsdaChatTab, Log, TEXT("[чат-цвета] выбрана плитка %d → %s"), i, *EditColorPicked.ToString());
			return;
		}
		return;   // клик по окну палитры дальше не идёт
	}

	// ── Строки каналов: галочка (2610+i) и образец цвета (2641+i) ───────────────────────────────
	for (int32 Row = 0; Row < kTabEditorRows; ++Row)
	{
		const int32 Chan = ChanIndexForRow(Row);
		if (!ChanOptsEdit.IsValidIndex(Chan)) { continue; }
		UWidget* ChkW = Node(*FString::Printf(TEXT("CHAT_TAB_WIN_B2_C%d"), Row + 1));
		if (!ChkW && Row == kTabEditorRows - 1) { ChkW = EditRow8Check; }
		if (IsUnderCursor(ChkW, Cur))
		{
			// `chatTab_SetChannelBit @0xBA1100`: бит канала в маске РЕДАКТИРУЕМОЙ вкладки (зеркало).
			if (ChatTabsEdit.IsValidIndex(EditTabIdx))
			{
				ChatTabsEdit[EditTabIdx].Mask ^= (1 << Chan);
				if (Snd) { Snd->PlaySound2DById(5); }
				RefreshTabEditor();
				UE_LOG(LogAsdaChatTab, Log, TEXT("[чат-вкладки] вкладка %d: канал %d → %s (маска 0x%03X)"),
					EditTabIdx, Chan, ((ChatTabsEdit[EditTabIdx].Mask >> Chan) & 1) ? TEXT("ВКЛ") : TEXT("выкл"),
					ChatTabsEdit[EditTabIdx].Mask);
			}
			return;
		}
		// Кликается НАША заливка (контрол 2641+i импортёр сделал TextBlock'ом и мы его погасили).
		if (IsUnderCursor(EditRowSwatches.IsValidIndex(Row) ? EditRowSwatches[Row].Get() : nullptr, Cur))
		{
			if (Snd) { Snd->PlaySound2DById(5); }
			// ★★★ ДОКАЗАТЕЛЬНЫЙ ЛОГ (Д28): по какой СТРОКЕ кликнули и какому СЛОТУ она отвечает.
			UE_LOG(LogAsdaChatTab, Log,
				TEXT("[чат-цвета] клик по образцу: строка %d ⇒ слот %d «%s», текущий цвет %s"),
				Row, Chan, *UiChannelText(SlotNameLocId(Chan)), *ChanOptsEdit[Chan].Color.ToString());
			ShowColorSelect(Row);
			return;
		}
	}

	// ── Стрелки прокрутки списка вкладок 2597 (аналог скроллбара 2598) ──────────────────────────
	//   Проверяются ДО ячеек: они лежат в той же полосе 2598, вплотную справа от столбца.
	if (EditTabArrowUp && EditTabArrowUp->GetVisibility() != ESlateVisibility::Collapsed
		&& IsUnderCursor(EditTabArrowUp, Cur))
	{
		ScrollTabEditorList(-1);
		if (Snd) { Snd->PlaySound2DById(5); }
		return;
	}
	if (EditTabArrowDn && EditTabArrowDn->GetVisibility() != ESlateVisibility::Collapsed
		&& IsUnderCursor(EditTabArrowDn, Cur))
	{
		ScrollTabEditorList(1);
		if (Snd) { Snd->PlaySound2DById(5); }
		return;
	}

	// ── Список вкладок 2597: клик по ячейке ВЫБИРАЕТ вкладку для редактирования ──────────────────
	//   ЭТАЛОН (ui_core.c:12348-12350): `case 0xA25: pThis[109] = itemIndex;
	//   uiRefreshSkillSlotList(this, 0);` — выбранная строка задаёт `stateFlags1` (редактируемую
	//   вкладку), СНИМОК live→edit при этом НЕ пересоздаётся (a2 = 0), то есть уже поставленные
	//   галочки и цвета других вкладок сохраняются.
	{
		const int32 Hit = EditTabCellHitTest(Cur);
		if (Hit >= 0)
		{
			EditTabIdx = Hit;
			if (Snd) { Snd->PlaySound2DById(5); }
			RefreshTabEditor();       // перерисовать И строки каналов (маска новой вкладки), И столбец
			UE_LOG(LogAsdaChatTab, Log,
				TEXT("[чат-вкладки] список 2597: выбрана вкладка %d «%s», маска 0x%03X"),
				EditTabIdx, *TabName(EditTabIdx),
				ChatTabsEdit.IsValidIndex(EditTabIdx) ? ChatTabsEdit[EditTabIdx].Mask : 0);
			return;
		}
	}

	// ── Прокрутка списка каналов (эталонный скроллбар 2639): 10 каналов, 8 строк ⇒ смещение 0…2 ──
	// ★ [Б1] В ОРИГИНАЛЕ прокрутка звала `uiRefreshSkillSlotList(this, 1)` и ЗАНОВО снимала
	//   live → edit, стирая несохранённые галочки и цвета. У нас прокрутка только перерисовывает.
	// ★ ГРАБЛЯ 2026-08-06: искали узел по ТОЧНОМУ имени `listCHAT_TAB_WIN_B2_ListBox`, а импортёр
	//   даёт точное имя только интерактив-контролам и меткам — у планов статик-узла имя
	//   `<idc>_<порядковый>_<план>`. Поиск всегда возвращал nullptr, и прокрутка каналов НЕ
	//   РАБОТАЛА ВОВСЕ (каналы 8 и 9 были недостижимы). Ищем по префиксу.
	const int32 MaxScroll = FMath::Max(0, ChanOptsEdit.Num() - kTabEditorRows);
	if (MaxScroll > 0 && IsUnderCursor(FindByPrefix(TabEditorWidget, TEXT("listCHAT_TAB_WIN_B2_ListBox")), Cur))
	{
		EditChanScroll = (EditChanScroll + 1) % (MaxScroll + 1);
		RefreshTabEditor();
		UE_LOG(LogAsdaChatTab, Log,
			TEXT("[чат-вкладки] прокрутка каналов → %d (★ правки НЕ стираются — починен баг оригинала)"),
			EditChanScroll);
		return;
	}

	// ── Чекбокс 2649 «Вернуть настройки вкладки чата» → подтверждение 1520 (noticeId 5310) ───────
	if (IsUnderCursor(Node(TEXT("CHAT_TAB_WIN_E_Check")), Cur))
	{
		UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
		UAsdaTextSubsystem* Txt = GI ? GI->GetSubsystem<UAsdaTextSubsystem>() : nullptr;
		FString M = Txt ? Txt->GetTextById(5310) : FString();
		if (M.IsEmpty()) { M = TEXT("[#5310]"); }   // [ПОРТ] 09.10.2026 (мультиязычность, этап 3): без RU-фолбэка
		if (WM)
		{
			WM->ShowGameConfirm(M, [this](bool bYes) { if (bYes) { ResetTabsToDefault(); } }, 0.f, 2288, 2289, /*очередь: эталон (1, 5310) ui_core.c:12296*/5310, 1);
		}
		return;
	}
}

// ══════════ КОМАНДЫ МЕНЮ 2670 — ЧЕТЫРЕ ПУНКТА, ВСЕ ЧЕРЕЗ ЭТАЛОННЫЕ ОКНА ═══════════════════════
// `chatWnd_OnCommand @0xBA3110`, ветка `cmdId > 0x9FB` (chat.c:2095-2122). cmdId == IDNum контрола.
// Общая часть у всех четырёх — гашение окна 2670 — выполняется вызывающим (HandleMenuMouseUp).
void UAsdaChatWindowSubsystem::HandleTabMenuCommand(int32 IdNum)
{
	UGameInstance* GI = GetGameInstance();
	UAsdaTextSubsystem* Txt = GI ? GI->GetSubsystem<UAsdaTextSubsystem>() : nullptr;
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	auto Loc = [&](int32 Id) { return Txt ? Txt->GetTextById(Id) : FString(); };

	// ★ [Б7] Подсказка loc 5338 в оригинале говорит «не более 5 символов», а код проверяет 10 байт
	//   (5 корейских слогов CP949). Действующий лимит — 15 символов (решение юзера), и цифра в
	//   подсказке приводится к правде ОДНОЙ функцией — той же, что печатает отказ в ленте.
	auto GuideText = [&]() { return TabNameLimitText(); };

	// ★★★ ЦЕЛЬ ВСЕХ ПУНКТОВ — ВКЛАДКА, ПО КОТОРОЙ ВЫЗВАНО МЕНЮ (Д27 п.5, отступление от эталона;
	//   эталон везде берёт активную `self[108]`). Снимаем ОДИН РАЗ: диалоги асинхронные, а к
	//   моменту ответа игрок может успеть переключить вкладку.
	const int32 Target = MenuTargetTab();

	switch (IdNum)
	{
	case 2671:   // 0xA6F «Настройки вкладки чата» → редактор 2590 со СНИМКОМ (uiRefreshSkillSlotList(this,1))
		// ★ Открываем редактор СРАЗУ на целевой вкладке (эталон открыл бы на последней выбранной
		//   в списке 2597 — `stateFlags1`, которое пункт меню не трогает).
		ShowTabEditor(/*bSnapshot*/true, Target);
		break;

	case 2672:   // 0xA70 «Добавить вкладку чата» → диалог ввода 1580, режим 2, поле ПУСТОЕ
		if (WM)
		{
			WM->ShowInputDialog(Loc(5314), GuideText(), FString(), kTabNameMaxChars,
				[this](bool bOk, const FString& Text)
				{
					if (bOk) { AddChatTab(Text.TrimStartAndEnd()); }
				});
		}
		break;

	case 2674:   // 0xA72 «Изменить заголовок» → диалог ввода 1580, режим 1, поле ПРЕДЗАПОЛНЕНО
		if (WM)
		{
			// Эталон предзаполняет имя из ЗЕРКАЛА `tabRecordListMirror[…]` (chat.c:2119) —
			//   у нас это ChatTabsEdit; если редактор не открывали, зеркало равно живому списку.
			const FString Cur = ChatTabsEdit.IsValidIndex(Target)
				? (ChatTabsEdit[Target].LocId > 0 ? UiChannelText(ChatTabsEdit[Target].LocId)
				                                  : ChatTabsEdit[Target].Name)
				: TabName(Target);
			WM->ShowInputDialog(Loc(5313), GuideText(), Cur, kTabNameMaxChars,
				[this, Target](bool bOk, const FString& Text)
				{
					if (bOk) { RenameTab(Target, Text.TrimStartAndEnd()); }
				});
		}
		break;

	case 2673:   // 0xA71 «Удалить вкладку» → подтверждение 1520, текст loc 5309 с именем вкладки
		if (WM)
		{
			// Эталон: `sprintf(noticeText, loc5309, имя)` — здесь loc-строка САМА содержит «%s»,
			//   и подстановка идёт НАШИМ именем, а не пользовательским форматом ⇒ безопасно.
			FString Msg = Loc(5309);
			if (Msg.IsEmpty()) { Msg = TEXT("[#5309]"); }   // [ПОРТ] 09.10.2026 (мультиязычность, этап 3): без RU-фолбэка
			const FString Nm = TabName(Target);
			int32 P = INDEX_NONE;
			if (Msg.FindChar(TEXT('%'), P) && P + 1 < Msg.Len() && Msg[P + 1] == TEXT('s'))
			{
				Msg = Msg.Left(P) + Nm + Msg.Mid(P + 2);
			}
			else { Msg = FString::Printf(TEXT("%s (%s)"), *Msg, *Nm); }
			WM->ShowGameConfirm(Msg, [this, Target](bool bYes)
			{
				// «Да» (1523) → noticeId 5309 → `chatTabRemoveSelected` (ui_core.c:57671).
				if (bYes) { RemoveTab(Target); }
			}, 0.f, 2288, 2289, /*очередь: эталон (1, 5309) chat.c:2114*/5309, 1);
		}
		break;

	default:
		UE_LOG(LogAsdaChatTab, Warning, TEXT("[чат-меню] неизвестный пункт 2670: %d"), IdNum);
		break;
	}
}

// ══════════ Д28: ЦЕПОЧКА ЦВЕТА КАНАЛА — ДИАГНОСТИКА И ПРЯМАЯ ЗАПИСЬ ═══════════════════════════
// Живой тест дал «к каналу „Общий чат“ цвет не применяется». Путь цвета у всех десяти слотов
//   ОДИН И ТОТ ЖЕ (доказательство — в отчёте Д28), поэтому здесь заведены два инструмента,
//   которые разделяют цепочку на две независимо проверяемые половины:
//     • DebugChanColors      — что лежит в живой/рабочей таблице и сколько строк ленты каждым слотом;
//     • SetChanColorDirect   — записать цвет МИМО палитры и редактора (проверка РЕНДЕР-пути).
FString UAsdaChatWindowSubsystem::DebugChanColors() const
{
	int32 PerSlot[10] = { 0 };
	for (const FChatLogLine& L : LogHistory)
	{
		if (L.PrefixSlot >= 0 && L.PrefixSlot <= 9) { ++PerSlot[L.PrefixSlot]; }
	}
	FString S = TEXT("[чат-цвета] таблица каналов (эталон tabFilterOpt @CChat+0x36C, цвет записи i = 0x380 + 24*i)\n");
	S += TEXT("  слот | адрес  | ЖИВОЙ    | в редакторе | заводской | строк в ленте | название\n");
	for (int32 i = 0; i < 10; ++i)
	{
		const FColor Live = ChanOpts.IsValidIndex(i) ? ChanOpts[i].Color : FColor::White;
		const FColor Edit = ChanOptsEdit.IsValidIndex(i) ? ChanOptsEdit[i].Color : FColor::White;
		const FColor Def  = SlotColor(i);
		S += FString::Printf(TEXT("   %2d  | 0x%03X  | FF%02X%02X%02X | FF%02X%02X%02X    | FF%02X%02X%02X  | %5d         | %s (префикс [%s])\n"),
			i, 0x380 + 24 * i,
			Live.R, Live.G, Live.B, Edit.R, Edit.G, Edit.B, Def.R, Def.G, Def.B,
			PerSlot[i], *UiChannelText(SlotNameLocId(i)), *SlotShortName(i));
	}
	S += FString::Printf(TEXT("  строк в истории %d, лента построена: %s, активная вкладка %d маска 0x%03X\n"),
		LogHistory.Num(), LinesBox ? TEXT("да") : TEXT("НЕТ"), ActiveTab, TabMask(ActiveTab));
	S += TEXT("  ★ Общий чат (5085, категория 0) = СЛОТ 4; «[Все] Общий чат» (6561) = СЛОТ 0 — это РАЗНЫЕ каналы.\n");
	return S;
}

bool UAsdaChatWindowSubsystem::SetChanColorDirect(int32 Slot, FColor Color)
{
	if (!ChanOpts.IsValidIndex(Slot))
	{
		UE_LOG(LogAsdaChatTab, Error, TEXT("[чат-цвета] слот %d вне таблицы (%d записей)"), Slot, ChanOpts.Num());
		return false;
	}
	Color.A = 255;
	ChanOpts[Slot].Color = Color;
	// Рабочая копия тоже — иначе следующий коммит редактора вернул бы старое значение.
	if (ChanOptsEdit.IsValidIndex(Slot)) { ChanOptsEdit[Slot].Color = Color; }
	SaveChatOpt();
	RefreshTabEditor();
	RebuildLines();                                  // перекрасить уже висящие строки
	if (bComboOpen) { RefreshChannelCombo(-1); }
	UE_LOG(LogAsdaChatTab, Log,
		TEXT("[чат-цвета] слот %d «%s» := FF%02X%02X%02X напрямую (tabFilterOpt[%d], 0x%03X); лента перестроена"),
		Slot, *UiChannelText(SlotNameLocId(Slot)), Color.R, Color.G, Color.B, Slot, 0x380 + 24 * Slot);
	return true;
}

// ══════════════════════════ ДИАГНОСТИКА ═══════════════════════════════════════════════════════
FString UAsdaChatWindowSubsystem::DebugTabs() const
{
	FString S = FString::Printf(
		TEXT("[чат-вкладки] живых %d, в редакторе %d, активная %d, редактируется %d, прокрутка каналов %d, файл %s"),
		ChatTabs.Num(), ChatTabsEdit.Num(), ActiveTab, EditTabIdx, EditChanScroll, *ChatOptPath());
	for (int32 i = 0; i < ChatTabs.Num(); ++i)
	{
		S += FString::Printf(TEXT("\n  %d: «%s» маска 0x%03X%s"),
			i, *TabName(i), ChatTabs[i].Mask,
			ChatTabs[i].LocId > 0 ? *FString::Printf(TEXT(" (loc %d)"), ChatTabs[i].LocId) : TEXT(" (своя)"));
	}
	for (int32 i = 0; i < ChanOpts.Num(); ++i)
	{
		S += FString::Printf(TEXT("\n  канал %d «%s» цвет FF%02X%02X%02X"),
			i, *ChanOpts[i].Name, ChanOpts[i].Color.R, ChanOpts[i].Color.G, ChanOpts[i].Color.B);
	}
	S += FString::Printf(
		TEXT("\n  список 2597: ячеек %d, вмещается %d, первая видимая %d, под курсором %d, стрелка %d"),
		EditTabCells.Num(), EditTabVisibleCount(), EditTabScroll, EditTabHoverIdx, EditArrowHoverIdx);
	return S;
}

// ★★★ ПОЛНЫЙ СТРУКТУРНЫЙ РАЗБОР ОКНА 2590 (asda.ChatWin.DumpTabEditor) — закон
//   full-structural-analysis-always: перед UI-правкой видеть КАЖДЫЙ контрол, а не гадать по
//   префиксам. Headless-путь закрыт: свойство `WidgetTree` в UE-Python защищено (проверено
//   2026-08-06 на `_probe_chat_tab_editor_widget.py`), поэтому разбор снимается в рантайме.
//   Печатает имя, класс, видимость и слот (SPAN|POINT + анкоры + оффсеты) каждого узла окна.
FString UAsdaChatWindowSubsystem::DebugTabEditorWidgets() const
{
	if (!TabEditorWidget || !TabEditorWidget->WidgetTree)
	{
		return TEXT("[чат-вкладки] окно 2590 не построено — сначала asda.ChatWin.TabEditor");
	}
	FString S = FString::Printf(TEXT("=========== ОКНО РЕДАКТОРА ВКЛАДОК 2590 (%s) ==========="),
		*TabEditorWidget->GetName());
	TArray<UWidget*> All;
	TabEditorWidget->WidgetTree->GetAllWidgets(All);
	for (const UWidget* W : All)
	{
		if (!W) { continue; }
		FString Slot = TEXT("слот: нет");
		if (const UCanvasPanelSlot* CS = AsdaUi::SlotOf(W))
		{
			const FAnchors A = CS->GetAnchors();
			const FMargin  O = CS->GetOffsets();
			const bool bSpan = !FMath::IsNearlyEqual(A.Minimum.X, A.Maximum.X, 1e-4f)
			                || !FMath::IsNearlyEqual(A.Minimum.Y, A.Maximum.Y, 1e-4f);
			Slot = FString::Printf(TEXT("%s анкор [%.3f,%.3f]-[%.3f,%.3f] off(L%.1f T%.1f R%.1f B%.1f) z=%d"),
				bSpan ? TEXT("SPAN ") : TEXT("POINT"), A.Minimum.X, A.Minimum.Y, A.Maximum.X, A.Maximum.Y,
				O.Left, O.Top, O.Right, O.Bottom, CS->GetZOrder());
		}
		S += FString::Printf(TEXT("\n  %-44s %-22s vis=%-10d род=%-28s %s"),
			*W->GetName(), *W->GetClass()->GetName(), (int32)W->GetVisibility(),
			W->GetParent() ? *W->GetParent()->GetName() : TEXT("—"), *Slot);
	}
	S += FString::Printf(TEXT("\n  ИТОГО узлов: %d"), All.Num());
	return S;
}
