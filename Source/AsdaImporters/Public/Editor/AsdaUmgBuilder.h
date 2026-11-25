// Copyright (c) 2026 @sitxovski (Discord)
// AsdaUmgBuilder — EDITOR-ONLY хелпер генератора JUI→UMG (правило ui-editable-in-ue).
// ★ Живёт в модуле AsdaImporters (Type="Editor"): опирается на UWidgetBlueprint/UMGEditor, которых нет
//   в игровой сборке. Пока файл лежал в игровом модуле, target AsdaReborn (Game) вообще не собирался
//   (UHT: «Unable to find class UWidgetBlueprint»). Python зовёт класс по имени — путь модуля не важен.
// Экспонирует Python'у сборку дерева Widget Blueprint (CanvasPanel + Image/Button/Text на точных позициях/UV
// из JUI), т.к. UWidgetBlueprint::WidgetTree недоступен из Python напрямую. Результат — редактируемый в
// UMG-дизайнере BP, 1:1 с оригиналом. Данные раскладки/текстур/текстов берёт Python-парсер JUI/JuiSkin.
#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "AsdaUmgBuilder.generated.h"

class UWidgetBlueprint;
class UWidget;
class UPanelWidget;
class UTexture2D;

// ★★★ КАДР-СОСТОЯНИЕ JUI-скина (`SS = "N"` внутри плана `ES`).
//   Эталон `cControl_InitFields @0x12E5C90`: план контрола хранит МАССИВ ИЗ 4 КАДРОВ по 492 байта,
//   активный индекс — `plane+494` (= байт `+1976`); рендер (`CPlane_GetFrameRecord @0x12BBF60`)
//   рисует именно активный кадр. Движок НЕ тинтит и НЕ двигает кнопку — он ПОДМЕНЯЕТ КАДР.
//   Загрузчик скина (`ui_core.c:120161`) читает у КАЖДОГО кадра свои `ImageFileName` + `UV` +
//   `Diffuse`, поэтому состояние = (текстура, UV-регион, тинт), а не только тинт.
//   Смысл индексов (`Reports/ui_system/button_states_re.md`):
//     0 — normal / unchecked / unselected;  1 — hover;
//     2 — pressed / checked;                3 — checked+hover / selected.
USTRUCT(BlueprintType)
struct ASDAIMPORTERS_API FAsdaJuiFrame
{
	GENERATED_BODY()

	// Текстура-атлас кадра (у кадров одного плана она может РАЗЛИЧАТЬСЯ — напр. MapName_Elite: SS3 из ne_skillbar3).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AsdaUI")
	TObjectPtr<UTexture2D> Texture = nullptr;

	// UV-регион кадра в атласе (Min = левый-верх, Max = правый-низ).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AsdaUI")
	FBox2D UV = FBox2D(FVector2D(0.0, 0.0), FVector2D(1.0, 1.0));

	// Diffuse кадра (JuiSkin хранит ARGB в sRGB; Python отдаёт уже линейный цвет — закон ui-srgb-gamma-rule).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AsdaUI")
	FLinearColor Tint = FLinearColor::White;
};

UCLASS()
class ASDAIMPORTERS_API UAsdaUmgBuilder : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
#if WITH_EDITOR
	// Создать (пересоздать) Widget Blueprint по пути /Game/... с корневым CanvasPanel. Возвращает BP.
	UFUNCTION(BlueprintCallable, Category="AsdaUI")
	static UWidgetBlueprint* CreateWidgetBP(const FString& PackagePath, const FString& AssetName);

	// Корневой CanvasPanel (создаётся в CreateWidgetBP; геттер для добавления детей).
	UFUNCTION(BlueprintCallable, Category="AsdaUI")
	static UPanelWidget* GetRootCanvas(UWidgetBlueprint* BP);

	// Вложенный CanvasPanel-контейнер (ГРУППА JUI-узла): дети складываются внутрь в ЛОКАЛЬНЫХ координатах.
	//   Двигаешь группу — двигаются дети; в дереве редактора дочерние доступны. Слот = Pos/Size (база 1024×768).
	UFUNCTION(BlueprintCallable, Category="AsdaUI")
	static UPanelWidget* AddCanvas(UWidgetBlueprint* BP, UPanelWidget* Parent, const FString& Name,
		FVector2D Pos, FVector2D Size);

	// Добавить Image в CanvasPanel-родитель: позиция/размер (база 1024×768), текстура + UV-регион + тинт.
	UFUNCTION(BlueprintCallable, Category="AsdaUI")
	static UWidget* AddImage(UWidgetBlueprint* BP, UPanelWidget* Parent, const FString& Name,
		FVector2D Pos, FVector2D Size, UTexture2D* Texture, FBox2D UV, FLinearColor Tint, bool bBoxBrush, FMargin BoxMarginPx);

	// Добавить РАМКУ-ОБВОДКУ (JUI-скин типа Box_White1Stroke: 9-slice, где центральная ячейка ES4 —
	//   ПРОЗРАЧНЫЙ пиксель атласа, т.е. рисуется только контур). В UMG это Border-браш: Margin задаёт
	//   толщину краёв, центр не заливается. Используется камп-марками 1673/1674 плашки игрока.
	UFUNCTION(BlueprintCallable, Category="AsdaUI")
	static UWidget* AddStrokeBox(UWidgetBlueprint* BP, UPanelWidget* Parent, const FString& Name,
		FVector2D Pos, FVector2D Size, UTexture2D* Texture, FBox2D UV, FLinearColor Tint, float ThicknessPx);

	// Добавить АНИМИРОВАННЫЙ JUI-plane (UAsdaAnimatedPlane) — спрайт-цикл кадров (Animate="1" в JuiSkin).
	//   BaseUV = UV кадра0, FrameSizePx = размер кадра (px), TotalFrame/FrameIntervalMs — из JuiSkin. Система анимации.
	UFUNCTION(BlueprintCallable, Category="AsdaUI")
	static UWidget* AddAnimatedPlane(UWidgetBlueprint* BP, UPanelWidget* Parent, const FString& Name,
		FVector2D Pos, FVector2D Size, UTexture2D* Texture, FBox2D BaseUV, FLinearColor Tint,
		FVector2D FrameSizePx, int32 TotalFrame, float FrameIntervalMs, bool bLoop);

	// ★★★ НЕВИДИМАЯ ЗОНА ХИТ-ТЕСТА (прямоугольник контрола без картинки).
	//   ЗАЧЕМ. Хит-тест оригинала (`uiControlHitTest @0x12DB470`, ui_core.c:107127) проверяет ПЛАНЫ
	//   контрола, а не пиксели: контрол со скином `NoImage` (единственный план ES0, `Rect="0 0 5 5"`,
	//   `PlaneAlwaysInvisible="1"`) ВСЁ РАВНО ловит клик — иначе не работали бы сами ручки
	//   перетаскивания окон. Наш генератор создаёт виджеты только для планов С ТЕКСТУРОЙ, поэтому
	//   такие контролы в WBP не появлялись вовсе (в `gamemain_jui.ini` их 1771). Эта зона —
	//   прозрачный `UImage` (`SelfHitTestInvisible`, чтобы не перехватывать клики у настоящих
	//   кнопок): она нужна ГЕОМЕТРИЧЕСКИ — по её живому прямоугольнику подсистема окон решает
	//   «курсор над окном» и «курсор на ручке перетаскивания».
	UFUNCTION(BlueprintCallable, Category="AsdaUI")
	static UWidget* AddHitZone(UWidgetBlueprint* BP, UPanelWidget* Parent, const FString& Name,
		FVector2D Pos, FVector2D Size);

	// ★ Создать/загрузить ассет-реестр меты окон JUI (ручки перетаскивания + контролы NoSelect).
	//   Наполняет и сохраняет его Python тем же проходом, что строит WBP, — имена виджетов в мете
	//   поэтому совпадают с реальным деревом окна по построению.
	UFUNCTION(BlueprintCallable, Category="AsdaUI")
	static class UAsdaJuiWindowMetaAsset* CreateWindowMetaAsset(const FString& PackagePath, const FString& AssetName);

	// Добавить ПРОЗРАЧНЫЙ Button (фон даёт отдельный _Bg-image атласа; кнопка только ловит клики).
	UFUNCTION(BlueprintCallable, Category="AsdaUI")
	static UPanelWidget* AddButton(UWidgetBlueprint* BP, UPanelWidget* Parent, const FString& Name,
		FVector2D Pos, FVector2D Size);

	// Добавить ComboBox (UComboBoxString) — JUI-тип ComboBox (напр. созвездие CARAMAKE_cb). Опции задаёт рантайм.
	UFUNCTION(BlueprintCallable, Category="AsdaUI")
	static UWidget* AddComboBox(UWidgetBlueprint* BP, UPanelWidget* Parent, const FString& Name,
		FVector2D Pos, FVector2D Size);

	// ★ Кнопка со СТИЛЕМ состояний (Normal/Hover/Press из UV+тинтов одной текстуры) + звук клика.
	//   hover/press-эффекты как в оригинале (обычно тот же UV, разный Diffuse-тинт). ClickSoundPath = /Game/...
	UFUNCTION(BlueprintCallable, Category="AsdaUI")
	static UPanelWidget* AddStyledButton(UWidgetBlueprint* BP, UPanelWidget* Parent, const FString& Name,
		FVector2D Pos, FVector2D Size, UTexture2D* Texture,
		FBox2D NormalUV, FLinearColor NormalTint,
		FBox2D HoverUV, FLinearColor HoverTint,
		FBox2D PressUV, FLinearColor PressTint,
		const FString& ClickSoundPath);

	// ★★★ СОСТОЯНИЯ ПОДПИСИ КНОПКИ (цвет + смещение текста на наведении и нажатии).
	//
	// ЗАЧЕМ ОТДЕЛЬНО ОТ `AddStyledButton`. Кадр-состояние плана в JUI несёт НЕ ТОЛЬКО картинку:
	//   загрузчик скина (`ui_core.c:120184-120228`) кладёт в КАЖДЫЙ кадр ещё `CaptionColor`,
	//   `CaptionPoint`, `CaptionAlign` и `CaptionText`, а рендер подписи (`ui_core.c:109129-109151`)
	//   читает их у АКТИВНОГО кадра (`sub_92BCCD(plane,-1)` цвет, `sub_92279F(plane,-1)` смещение).
	//   ⇒ у оригинала при наведении/нажатии меняется и ПОДПИСЬ, а у скинов `NoImage_PushBtn` и
	//   `NoImage_Btn` (картинка кадров одинаковая и ПРОЗРАЧНАЯ) подпись — единственное, что меняется
	//   вообще. Именно поэтому реплики игрока в окне разговора с NPC (578-580) были «мёртвыми».
	//   Сводка по данным: цвет подписи различается по кадрам у 48 из 130 скинов `PushButton`,
	//   смещение — у 24; в 4 JUI-классах это накрывает 713 кнопок с непустым `CaptionText`.
	//
	// ГДЕ ХРАНИМ. В самом `FButtonStyle` кнопки — там для этого есть ровно нужные поля:
	//   `NormalForeground`/`HoveredForeground`/`PressedForeground`/`DisabledForeground` (цвет
	//   содержимого по состояниям) и `NormalPadding`/`PressedPadding` (сдвиг содержимого при
	//   нажатии). Так данные скина лежат НА КНОПКЕ, видны и правятся в редакторе UMG (закон
	//   `ui-editable-in-ue`) и переживают частичную регенерацию окна. Подпись импортёр кладёт
	//   СОСЕДНИМ `TextBlock` `<Имя>_Text` (менять родство нельзя — поедет раскладка всех окон),
	//   поэтому цвет/сдвиг применяет рантайм: `AsdaUi::BindButtonCaptionStates`.
	//
	// ⚠ `HoverOffset` в стиле не хранится: у ВСЕХ 130 скинов `PushButton` `CaptionPoint` кадра 1
	//   совпадает с кадром 0, поэтому наведение подпись не двигает — двигает только нажатие.
	//   Расхождение возникает лишь как ДЕФЕКТ ДАННЫХ оригинала (у 51 контрола `CaptionInfo`
	//   перекрывает кадр 0, а кадры 1/2 остаются со «скиновым» X, и подпись на наведении прыгала
	//   по горизонтали) — оно исправлено В ИМПОРТЁРЕ: hover/press считаются как
	//   `CaptionInfo + (кадрN − кадр0)`, то есть авторское смещение контрола сохраняется во всех
	//   состояниях (закон `fix-original-defects`).
	//
	// Возвращает false, если кнопки нет.
	UFUNCTION(BlueprintCallable, Category="AsdaUI")
	static bool ApplyButtonCaptionStates(class UButton* Button,
		FLinearColor NormalColor, FLinearColor HoverColor, FLinearColor PressColor,
		FVector2D NormalOffset, FVector2D PressOffset);

	// ★ Одеть УЖЕ СОЗДАННУЮ кнопку в скин состояний (Normal/Hover/Press) — для ДЕЛЬТА-правок окон, где
	//   генерик-импортёр не смог применить скин (атлас ещё не был импортирован) и оставил NoDraw-кнопку.
	//   Пересоздавать виджет нельзя: C++ ищет его по имени. Возвращает false, если Button/Texture пустые.
	UFUNCTION(BlueprintCallable, Category="AsdaUI")
	static bool ApplyButtonSkin(class UButton* Button, UTexture2D* Texture,
		FBox2D NormalUV, FLinearColor NormalTint,
		FBox2D HoverUV, FLinearColor HoverTint,
		FBox2D PressUV, FLinearColor PressTint);

	// Добавить TextBlock.
	UFUNCTION(BlueprintCallable, Category="AsdaUI")
	//   bBold — начертание Bold композитного шрифта.
	//   ★★★ ПО УМОЛЧАНИЮ НЕТ (исправлено 2026-09-24; прежнее «по умолчанию ДА» было СИСТЕМНЫМ ДЕФЕКТОМ).
	//   Оригинал не рисует TTF в рантайме: `LoadEngineFont` (ui_core.c:106592-106660) грузит готовый
	//   растровый атлас `JUIData/font/DATA0..199.TEX` + `FONT0.BIN`, а собран атлас через
	//   `CreateFontA(..., cHeight 14, вес `boldFlag ? 700 : 400`, "Arial", charset 204)`
	//   (`fontBuildGlyphAtlasForLang`, font_text.c:6515-6600), причём `boldFlag` инициализируется
	//   НУЛЁМ (ui_core.c:106690) ⇒ ВЕС 400 = Regular. Подтверждено побайтово на живом FONT0.BIN
	//   (7136 б): Arial/14/400 совпадает в 222 глифах из 223 (Σ|Δ| = 1), Arial Bold — в 131.
	//   «Жирный» у оригинала — НЕ гарнитура: тег `|FB` (font_text.c:7971-7977) ставит
	//   `styleFlags |= 0x200000`, а обработчик (font_text.c:6832-6886) рисует ТОТ ЖЕ квад ДВАЖДЫ —
	//   `penX += 1.0` → квад → `penX -= 1.0` → квад, то есть в `x+1` и обратно в `x`, НЕ меняя
	//   продвижения пера (★ поправка 24.09.2026: НЕ «penX+1 и penX-1», второй квад возвращается
	//   в исходную точку). Псевдо-жирный порта — `AsdaUi::ApplyPseudoBold`, он 1:1.
	//   Цена дефекта: Bold = 106.3 % ширины оригинала, у 34 подписей хвост не влезал в слот JUI.
	//   ★★★ ПРАВИЛО НА БУДУЩЕЕ: ГАРНИТУРУ ЗАДАВАТЬ ЯВНО ВСЕГДА. В ассетах гарнитуры НЕ БЫЛО
	//   ВООБЩЕ — `Bold` приходил из CDO движка
	//   (`Engine/Source/Runtime/UMG/Private/Components/TextBlock.cpp:36`:
	//   `Font = FSlateFontInfo(RobotoFontObj.Object, 24, FName("Bold"))`), а тегированная
	//   сериализация пишет только ОТЛИЧИЯ от архетипа, поэтому наше прежнее явное `"Bold"`
	//   совпадало с дефолтом и в пакет не попадало. ⇒ ЛЮБОЙ НОВЫЙ `TextBlock` БЕЗ ЯВНОЙ
	//   ГАРНИТУРЫ СНОВА БУДЕТ `Bold` — и дефект вернётся молча. Эта функция ставит гарнитуру
	//   сама; прямое `ConstructWidget<UTextBlock>` обязан позаботиться вызывающий
	//   (аудит 24.09.2026: 33 таких места в коде, 29 ставят шрифт, 4 подменяются
	//   `UAsdaRichText::ApplyFormatted` на `URichTextBlock` со своим стилем — утечки нет).
	static UWidget* AddText(UWidgetBlueprint* BP, UPanelWidget* Parent, const FString& Name,
		FVector2D Pos, FVector2D Size, const FString& Text, int32 FontSize, FLinearColor Color, int32 HAlign,
		bool bWrap = true, bool bBold = false);

	// Добавить редактируемое поле ввода (UEditableText). bPassword=маска. Для логина/пароля.
	UFUNCTION(BlueprintCallable, Category="AsdaUI")
	static UWidget* AddEditableText(UWidgetBlueprint* BP, UPanelWidget* Parent, const FString& Name,
		FVector2D Pos, FVector2D Size, const FString& HintText, int32 FontSize, FLinearColor Color, bool bPassword);

	// ★ МАССОВАЯ правка шрифта в СУЩЕСТВУЮЩЕМ окне (без пересоздания — ручные правки сохраняются):
	//   у всех текстовых контролов (TextBlock/EditableText/RichText) ставит размер и начертание.
	//   Возвращает число изменённых контролов. Сохранение ассета делает Python.
	UFUNCTION(BlueprintCallable, Category="AsdaUI")
	static int32 RestyleTextFonts(UWidgetBlueprint* BP, int32 FontSize, FName Typeface);

	// ★ ЧЁРНАЯ ОБВОДКА текста (эталон: у над-головных плашек выставлен стиль обводки — SetField926 0x800000).
	//   Массово ставит FontOutline у всех текстовых контролов окна. OutlineSize в ПИКСЕЛЯХ (UE — целое).
	//   Возвращает число изменённых контролов.
	UFUNCTION(BlueprintCallable, Category="AsdaUI")
	static int32 ApplyTextOutline(UWidgetBlueprint* BP, int32 OutlineSize, FLinearColor OutlineColor);

	// ★ Групповой ЭКРАННЫЙ анкор: все контролы с именем на Prefix переанкорить как КОГЕЗИВНЫЙ блок к доле-экрана
	//   AnchorFrac (0..1). Каждый держит px-размер и относительное смещение внутри группы (composites не расходятся),
	//   но группа целиком садится к своей области экрана при любом разрешении — как per-window генерик-анкоры.
	//   BaseSize = базовое разрешение раскладки (1024×768). Монолит WBP_CharSelect: панель→верх-право, полоса→центр.
	UFUNCTION(BlueprintCallable, Category="AsdaUI")
	static void ApplyGroupAnchor(UWidgetBlueprint* BP, const FString& Prefix, FVector2D AnchorFrac, FVector2D BaseSize);

	// ★★★ CheckBox (JUI-тип `CheckButton` = ТОГГЛ) со ВСЕМИ кадрами-состояниями плана.
	//   Раньше сюда клали только кадры 0 и 2 и БЕЗ тинтов, одну и ту же кисть на hovered/pressed —
	//   кнопки-тогглы во всех окнах приходили «мёртвыми» (нарушение закона ui-button-hover-click-rule).
	//   Раскладка кадров → шесть состояний Slate (эталон `checkBox_OnToggle @0x12FDF70`,
	//   `CControl_SetVisibleState @0x12FE000`, push-OnPressed `+1976 = 2`):
	//     Unchecked = 0, UncheckedHovered = 1, UncheckedPressed = 2 (нажатие показывает кадр «отмечено»),
	//     Checked   = 2, CheckedHovered   = 3, CheckedPressed   = 3.
	//   Кадр с пустой текстурой заменяется кадром 0 (скин задал не все состояния).
	UFUNCTION(BlueprintCallable, Category="AsdaUI")
	static UWidget* AddCheckBox(UWidgetBlueprint* BP, UPanelWidget* Parent, const FString& Name,
		FVector2D Pos, FVector2D Size,
		const FAsdaJuiFrame& Normal, const FAsdaJuiFrame& Hover,
		const FAsdaJuiFrame& Checked, const FAsdaJuiFrame& CheckedHover);

	// ★ ДИАГНОСТИКА: текстовый снимок стиля чекбокса — по строке на каждое из шести состояний
	//   (текстура | UV-регион | тинт). Нужен, потому что `FSlateBrush::UVRegion` в Python НЕ
	//   экспонирован, и проверить «состояния действительно различаются» скриптом иначе нельзя.
	UFUNCTION(BlueprintCallable, Category="AsdaUI")
	static FString DescribeCheckBoxStyle(class UCheckBox* Box);

	// ★ КНОПКА-ВАРИАНТ мульти-выбора (JUI-тип `RadioButton`: каждый план `ES` = отдельный выбор).
	//   Визуал покоя и подсветку «выбран» даёт ПЛАН-картинка под кнопкой (её красит рантайм —
	//   напр. `UpdateGenderHighlight`), поэтому Normal/Disabled здесь НЕ рисуются (NoDraw), а
	//   Hovered/Pressed берут кадры 1 и 2 своего плана (`CControl_SetSelectedIndex @0x12F61A0`,
	//   `uiList_OnItemMouseDown @0x12F6320`). Так вариант получает наведение/нажатие, не перекрывая
	//   рантайм-подсветку выбранного.
	UFUNCTION(BlueprintCallable, Category="AsdaUI")
	static UPanelWidget* AddOptionButton(UWidgetBlueprint* BP, UPanelWidget* Parent, const FString& Name,
		FVector2D Pos, FVector2D Size, const FAsdaJuiFrame& Hover, const FAsdaJuiFrame& Pressed);

	// СИСТЕМА прогресс-баров: скин типа "ProgressBar" → нативный UProgressBar (заливка по Percent, БЕЗ scale-мыла).
	UFUNCTION(BlueprintCallable, Category="AsdaUI")
	static UWidget* AddProgressBar(UWidgetBlueprint* BP, UPanelWidget* Parent, const FString& Name,
		FVector2D Pos, FVector2D Size, UTexture2D* Texture, FBox2D FillUV, FLinearColor FillTint);

	// Переставить уже добавленного ребёнка на позицию Index в родителе (z-порядок внутри CanvasPanel).
	//   Нужно для КАРТИНКИ КАРТЫ миникарты: оригинал рисует квад карты (uiMiniMapRender) ДО
	//   WindowMgr_RenderControls, т.е. ПОД всеми JUI-контролами окна → в UMG это child index 0.
	UFUNCTION(BlueprintCallable, Category="AsdaUI")
	static bool MoveChildToIndex(UPanelWidget* Parent, UWidget* Child, int32 Index);

	// ★★★ ЦВЕТ ТЕКСТА ИЗ `EXS TextColor` — СИСТЕМНОЕ ЛЕЧЕНИЕ «ФИОЛЕТОВЫХ ЗАГОЛОВКОВ».
	//   В `gamemain_jui.ini` у контрола ДВА разных цвета:
	//     • `CaptionInfo = "offX offY ARGB align"` — цвет ПОДПИСИ-caption (рисуется только при
	//       непустом `CaptionText`); у 5374 узлов там дизайнерский маркер `ff400040` (фиолетовый);
	//     • блок `EXS … TextColor = "AARRGGBB" … EXE` — НАСТОЯЩИЙ цвет текста контрола
	//       (эталон `CTextControl_LoadProps @0x12E4E20`, font_text.c:9738 → поле `this+3692`).
	//   Импортёр раньше красил Text-контролы цветом из `CaptionInfo` ⇒ 5373 контрола в 251 окне
	//   получили фиолетовый. Функция ставит корректный цвет уже СУЩЕСТВУЮЩИМ ассетам по списку
	//   «имя виджета → цвет» (списки Python берёт из того же JUI). Возвращает число изменённых.
	UFUNCTION(BlueprintCallable, Category="AsdaUI")
	static int32 ApplyJuiTextColors(UWidgetBlueprint* BP, const TArray<FString>& Names,
		const TArray<FLinearColor>& Colors, bool bOnlyIfPlaceholder, FLinearColor Placeholder);

	// ★ ПОЛНЫЙ СТРУКТУРНЫЙ РАЗБОР ОКНА ИЗ HEADLESS (закон full-structural-analysis-always).
	//   Свойство `UWidgetBlueprint::WidgetTree` из UE-Python недоступно (защищённое), поэтому дамп
	//   дерева снимаем здесь: имя, класс, родитель, схема слота (SPAN|POINT), анкоры, оффсеты, z.
	UFUNCTION(BlueprintCallable, Category="AsdaUI")
	static FString DumpWidgetTree(UWidgetBlueprint* BP);

	// ★ ТОЧЕЧНОЕ УДАЛЕНИЕ УЗЛА ассета окна по имени (25.09.2026, окно 50300). Нужна шагам пост-импорта,
	//   которые чинят ИМЕНА узлов: `WidgetTree` из UE-Python недоступен, а голый `RemoveChild` оставил бы
	//   запись в `WidgetVariableNameToGuidMap` (отсюда ensure «Widget … did not get a GUID»). Удаляет штатно,
	//   тем же путём, что кнопка Delete редактора (`FWidgetBlueprintOperationUtils::RemoveWidget`).
	//   `ExpectClass` — защита от промаха: пусто = любой класс, иначе имя класса узла обязано совпасть
	//   (напр. "TextBlock"). Узел С ДЕТЬМИ не удаляется (false) — поддерево шаг должен разбирать явно.
	UFUNCTION(BlueprintCallable, Category="AsdaUI")
	static bool RemoveWidgetByName(UWidgetBlueprint* BP, const FString& Name, const FString& ExpectClass);

	// ★ ТОЧЕЧНОЕ ПЕРЕИМЕНОВАНИЕ УЗЛА ассета окна (25.09.2026, окно 50300): штатный путь редактора
	//   (`FWidgetBlueprintOperationUtils::RenameWidget` — GUID-таблица, привязки, фокус). false, если узла
	//   `OldName` нет или имя `NewName` уже занято. Метка узла (Display Label) после переименования
	//   сбрасывается в пустую — как у узлов, созданных импортёром.
	UFUNCTION(BlueprintCallable, Category="AsdaUI")
	static bool RenameWidgetByName(UWidgetBlueprint* BP, const FString& OldName, const FString& NewName);

	// ★ 30.10.2026 (окно очков «Битвы армий» 49600, решение координатора «вариант 1»): ТОЧЕЧНАЯ КОПИЯ ПОДДЕРЕВА узла `SrcName`
	//   в панель `ParentName` того же ассета — имена узлов = исходные + `Suffix`, позиция корня копии = `Pos` (размер/анкоры/
	//   выравнивание/z/видимость/кисти/шрифты — как у оригинала: листья копируются `DuplicateObject`, панели строятся заново и
	//   получают детей рекурсивно). Новые узлы регистрируются в GUID-таблице (`OnVariableAdded`). Ничего, кроме добавления, не
	//   меняет. Возврат — число созданных узлов (0 — исходного/родителя нет или имя копии занято).
	UFUNCTION(BlueprintCallable, Category="AsdaUI")
	static int32 DuplicateSubtree(UWidgetBlueprint* BP, const FString& SrcName, const FString& ParentName, const FString& Suffix, FVector2D Pos);

	// Скомпилировать и сохранить BP.
	UFUNCTION(BlueprintCallable, Category="AsdaUI")
	static bool CompileAndSave(UWidgetBlueprint* BP);
#endif
};
