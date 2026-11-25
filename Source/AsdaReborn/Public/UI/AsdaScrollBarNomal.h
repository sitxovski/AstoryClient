// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ ВЕРТИКАЛЬНАЯ ПОЛОСА ПРОКРУТКИ СКИНА `Nomal_Scr` — ОДНА НА ВЕСЬ ПОРТ.
//
// ЗАЧЕМ ОТДЕЛЬНЫЙ КОМПОНЕНТ (аудит `Reports/code_dedup_audit_2.md`, D3 + Дд1).
//   Контрола полосы в .JUI НЕТ и быть не может: движок оригинала создаёт `CScrollBar` В РАНТАЙМЕ —
//   `uiControl_AttachVertScrollbar @0x12F3160` читает у СКИНА колёсного окна ключ
//   `ScrollBarFileNameVert = "Nomal_Scr"` и подвешивает полосу РЕБЁНКОМ этого окна. Импортёр же
//   разбирает только `gamemain_jui.ini`, поэтому четыре плана порт собирает сам — из тех же данных.
//   Так вышло ДВА полных экземпляра этого кода (инвентарь 494 и книга навыков 12291), включая
//   ПОБАЙТОВО совпадающие UV-константы: правка UV в одном месте разъезжала полосу второго окна.
//
// ЧТО ЗДЕСЬ, А ЧТО У ОКНА. Здесь — всё, что задано СКИНОМ и формулами движка: константы `Nomal_Scr`,
//   создание четырёх планов, раскладка (`uiScrollbarLayoutPlanes @0x12FE500`), длина и положение
//   бегунка (`uiScrollbarSetRange` / `uiScrollbarLayoutThumb`), разбор ввода
//   (`uiScrollbarOnButtonEvent @0x12FF2C0` / `uiScrollbarOnMouseDrag @0x12FF020`).
//   У окна остаётся ТОЛЬКО его собственное: где полоса стоит, какой у неё диапазон рядов и что
//   значит «прокрутить» — окно получает от `Tick` ДЕЙСТВИЕ и применяет его своей моделью.
#pragma once
#include "CoreMinimal.h"
#include "AsdaScrollBarNomal.generated.h"

class UUserWidget;
class UCanvasPanel;
class UImage;

// ═════ ДАННЫЕ СКИНА `Nomal_Scr` (JuiSkin.ini, [Ctrl] Name = "Nomal_Scr") ═══════════════════════
//   Type = "ScrollBar", ScrollBarType = "1" (вертикальный), ImageFileName = "N_01.tga" (512×512),
//   у всех планов `PlaneAlphaBlending = 1`. Diffuse (ARGB, одинаковый во всех состояниях SS0–SS2):
//     ES0 дорожка = FFFFFFFF; ES1 ↑, ES2 ↓, ES3 бегунок = 64FFFFFF (альфа 100/255 ≈ 0.39, RGB белые).
//   ★ Волна 13 (25.09.2026, СРЕДН-2 ревизии волны 12): прежняя строка «Diffuse всех планов = FFFFFFFF»
//   была НЕВЕРНОЙ (`JuiSkin.ini:55414…`), и полосы, собранные `Build`, рисовались непрозрачными — ярче
//   оригинала. Оригинал (`CControl_BuildPlaneVertices@0x12DE440` → вершинный DIFFUSE; `CControl_RenderPlanes`:
//   COLOROP/ALPHAOP = MODULATE(TEXTURE, DIFFUSE), при `PlaneAlphaBlending` SRCALPHA/INVSRCALPHA) умножает
//   тексель на вершинный цвет и смешивает по альфе. Порт: тинт кисти = Diffuse плана (Slate тоже
//   умножает тексель на тинт и смешивает по альфе), `ColorAndOpacity` виджета — белый (второй раз цвет
//   не умножаем). Ни одна константа не подобрана «на глаз».
namespace AsdaNomalScr
{
	// Атлас скина. Берётся через `UAsdaTextureSubsystem` (кэш + refCount), закон `resource-subsystems`.
	inline const TCHAR* kAtlasPath = TEXT("/Game/AsdaAssets/Textures/JUI/n_01.n_01");

	inline constexpr float kWidth    = 11.f;   // Rect всех четырёх планов по X: 0 … 11
	inline constexpr float kButton   = 11.f;   // ES1 "0 0 11 11" и ES2 "0 41 11 52" ⇒ 11×11
	inline constexpr float kThumbMin =  6.f;   // `uiScrollbarSetRange`: if (len < 6) len = 6

	// Diffuse планов (D3DCOLOR = ARGB, как пишет `sscanf("%X …")` загрузчика скина, ui_core.c:120260-120275).
	inline constexpr uint32 kDiffuseTrack  = 0xFFFFFFFFu;   // ES0 дорожка
	inline constexpr uint32 kDiffuseButton = 0x64FFFFFFu;   // ES1 ↑, ES2 ↓, ES3 бегунок
	// ARGB скина → тинт кисти: RGB sRGB→линейный (как `asda_jui_parse._hex_argb_to_rgba` импортёра),
	//   альфа линейна (A/255). `FColor(uint32)` хранит ровно 0xAARRGGBB.
	inline FLinearColor DiffuseToTint(uint32 Argb) { return FLinearColor(FColor(Argb)); }

	// UV планов. Первая пара = левый-верх, последняя = правый-низ (JUI пишет 4 угла).
	inline FBox2f UvTrack()       { return FBox2f(FVector2f(0.996f, 0.000f), FVector2f(1.000f, 0.004f)); }
	inline FBox2f UvUp()          { return FBox2f(FVector2f(0.387f, 0.438f), FVector2f(0.408f, 0.459f)); }
	inline FBox2f UvDown()        { return FBox2f(FVector2f(0.414f, 0.438f), FVector2f(0.436f, 0.459f)); }
	// ★ SS2 (нажата) у ES2 — ЕДИНСТВЕННОЕ состояние во всём скине, где UV отличается: сдвиг на
	//   −1 тексель по обеим осям ⇒ рисунок стрелки уезжает на +1 px вправо-вниз. Это и есть
	//   эталонная «вдавленность» кнопки, а не опечатка (ширина выборки та же, 0.022 = 11 px).
	inline FBox2f UvDownPressed() { return FBox2f(FVector2f(0.412f, 0.436f), FVector2f(0.434f, 0.457f)); }
	inline FBox2f UvThumb()       { return FBox2f(FVector2f(0.359f, 0.438f), FVector2f(0.381f, 0.459f)); }
}

// Что полоса просит сделать окно по итогам кадра ввода.
UENUM()
enum class EAsdaScrollAction : uint8
{
	None     UMETA(DisplayName = "ничего"),
	// Сдвиг НА `Value` рядов (события 69633/69634 — стрелки, 69635/69636 — «страница» по дорожке).
	//   Шаг и стрелки, и «страницы» в обоих окнах порта = 1 ряд (`uiControlSetScrollPage(…, 1, 1)`).
	ScrollBy UMETA(DisplayName = "прокрутить на N рядов"),
	// Перетаскивание бегунка (событие 69637): `Value` = АБСОЛЮТНЫЙ номер ряда, уже клампленный.
	SetRow   UMETA(DisplayName = "встать на ряд N")
};

// Полоса как ЧАСТЬ ОКНА: держится UPROPERTY-полем подсистемы, поэтому планы под защитой GC.
USTRUCT()
struct ASDAREBORN_API FAsdaScrollBarNomal
{
	GENERATED_BODY()

	// ── Четыре плана скина (порядок отрисовки оригинала: дорожка → бегунок → стрелки) ────────────
	UPROPERTY(Transient) TObjectPtr<UImage> Track = nullptr;   // ES0 — дорожка, ловит «страницу»
	UPROPERTY(Transient) TObjectPtr<UImage> Up    = nullptr;   // ES1 — стрелка вверх
	UPROPERTY(Transient) TObjectPtr<UImage> Down  = nullptr;   // ES2 — стрелка вниз (единственная с SS2)
	UPROPERTY(Transient) TObjectPtr<UImage> Thumb = nullptr;   // ES3 — бегунок

	// ── Последняя посчитанная геометрия (в координатах ХОСТ-КАНВАСА; нужна окну для дампов) ──────
	FVector2D Origin = FVector2D::ZeroVector;
	float BarHeight = 0.f;
	float ThumbLen  = 0.f;
	float ThumbTop  = 0.f;    // от верха полосы (уже с учётом стрелки ↑)

	// Латч +608 эталона: бегунок «схвачен», пока ЛКМ не отпущена (`uiScrollbarOnMouseDrag`).
	bool  bThumbGrab = false;
	// Какой план сейчас нажат (1 ↑, 2 ↓, 3 бегунок, 0 дорожка, −1 нет) — только для картинки SS2.
	int32 PressedPlane = -1;
	// ★ Волна 13 (М-5): UV стрелки ↓ в обычном состоянии — ИЗ КИСТИ плана (у `Adopt` это ассет окна, у `Build` —
	//   данные скина). Нажатое состояние = она же со сдвигом SS2−SS0 скина, поэтому правка UV владельцем в
	//   ассете не откатывается первым же нажатием.
	FBox2f DownUvNormal = AsdaNomalScr::UvDown();

	// Планы созданы?
	bool IsBuilt() const { return Track != nullptr; }

	// ── Собрать четыре плана в `HostCanvas` (один раз). `Owner` — окно-владелец (объекты создаются
	//    в нём, живут ровно столько же). `NamePrefix` уходит в имена узлов для дампов.
	//    `ZTrack` — z дорожки; бегунок ZTrack+1, стрелки ZTrack+2 (порядок оригинала).
	//    false — атлас скина не найден: полосу НЕ строим и «своим» цветом не рисуем (`no-invented-ui`).
	bool Build(UUserWidget* Owner, UCanvasPanel* HostCanvas, const TCHAR* NamePrefix, int32 ZTrack = 20);

	// ── ★ 25.09.2026 (окно 78200): взять четыре плана, УЖЕ ЛЕЖАЩИЕ В АССЕТЕ окна (их кладёт шаг
	//    пост-импорта из данных скина), вместо создания кодом. Браши/тинт/место — из ассета, код их
	//    не переписывает; дальше всё как у собранной полосы (`Layout` ставит планы и бегунок в
	//    координатах канваса-родителя планов, `Tick` разбирает ввод). Все четыре обязаны быть в ОДНОМ
	//    канвасе. false — какого-то плана нет или родитель не канвас (полосу тогда не ведём).
	bool Adopt(UImage* InTrack, UImage* InUp, UImage* InDown, UImage* InThumb);

	// ── Раскладка планов и бегунка (`uiScrollbarLayoutPlanes`, ScrollBarType = 1):
	//      ↑ (0, 0, 11, 11) · дорожка (0, 11, 11, H−11) · ↓ (0, H−11, 11, H) · бегунок поверх дорожки.
	//    `InOrigin` — левый-верх полосы в координатах ХОСТ-КАНВАСА (окно считает его само: у одного
	//      это правый край колёсного окна по живой геометрии, у другого — слот панели).
	//    `InBarHeight` — полная высота полосы; `Scale` — множитель авторских размеров (1, если окно
	//      не масштабируется); `RangeA2`/`RangeA3` — целые аргументы эталона (см. ниже); `MaxRow` —
	//      максимальный номер ряда (= a3 − a2, как `+0x264`); `Pos` — текущий ряд.
	//    ★ `MaxRow <= 0` ⇒ бегунок схлопывается в ноль, а стрелки и дорожка ОСТАЮТСЯ на экране —
	//      ровно как снимает флаг +632 `uiScrollbarSetRange`.
	//    ★ Волна 14 (МЕЛК-1 ревизии волны 13): доля видимого передаётся ЦЕЛЫМИ аргументами эталона
	//      `uiScrollbarSetRange(a2, a3)` / `uiControlSetScrollRange(a2, a3)` (`RangeA2` видимых из `RangeA3`):
	//      длина бегунка считается ровно их float-арифметикой (`EtalonThumbLen`), а по готовой доле её не
	//      повторить — округление `100·a2/a3` во float теряется.
	void Layout(const FVector2D& InOrigin, float InBarHeight, float Scale,
	            int32 RangeA2, int32 RangeA3, int32 MaxRow, int32 Pos);
	//    ★ Волна 15: прежняя форма с готовой долей (`float VisibleFrac`) удалена — последнее окно на ней,
	//      50300 `AsdaItemOptionSubsystem`, переведено на целые `(4, N)` эталона; теперь все 9 окон считают
	//      длину бегунка одной формулой `EtalonThumbLen`.

	// ── Длина бегунка по `uiScrollbarSetRange@0x12FF5E0` (x87, дизассемблер сборки IDB 13 344 768 б):
	//      fild a2 · fmul dword 100.0f · fidiv a3 · fstp dword v15 · fild len · fmul dword v15 ·
	//      fdiv dword 100.0f · `_ftol` (усечение к нулю) · if (<6) 6.
	//    Точность x87 на главном потоке — 24 бита: `CreateDevice` зовётся без D3DCREATE_FPU_PRESERVE
	//      (флаги только 0x20/0x40, `CD3DManager_GetBehaviorFlags`), и дальше код exe точность не меняет.
	//      ★ Волна 15 (МЕЛК-1 ревизии волны 14), уточнение: вызовов `_controlfp` в exe 16. (а) Один —
	//      стартовый код CRT (0xFF235E из `WinMainCRTStartup`): `_controlfp(0x10000, 0x30000)` = 53 бита,
	//      но это ДО WinMain, а `CreateDevice` позже ставит 24. (б) Остальные — пары в D3DX, которые
	//      ВРЕМЕННО ставят 64 бита (`_controlfp(0, 0x30000)`) и затем возвращают сохранённое; к `SetRange`
	//      они не относятся. `fldcw` в 71 функции меняют только режим округления (биты 10–11).
	//      (в) Остаточный риск: внешние DLL на главном потоке (`fmod.dll`, `im404Control.dll`, `Addon.dll`,
	//      `ijl15.dll`, IME) статически не проверены. Замеров на живом клиенте нет и не будет (запрет).
	//      Поэтому КАЖДАЯ операция округляется до float — как float-арифметика SSE здесь.
	//    `LenJ` — длина дорожки в пикселях `.JUI` (целое, `rect.bottom − rect.top` плана 0).
	static int32 EtalonThumbLen(int32 LenJ, int32 RangeA2, int32 RangeA3);

	// Спрятать полосу целиком (окно свёрнуто / вкладка без прокрутки) и снять латч захвата.
	void Hide();

	// Курсор над любой частью полосы — чтобы клик по ней не уходил в содержимое окна и в мир.
	bool IsCursorOver() const;

	// ── Ввод. `bLmb` — ЛКМ зажата сейчас, `bLmbDownEdge` — фронт нажатия этого кадра.
	//    Возвращает ДЕЙСТВИЕ, `OutValue` — его аргумент (дельта рядов либо абсолютный ряд).
	//    Разбор 1:1 по `uiScrollbarOnButtonEvent` / `uiScrollbarOnMouseDrag`; авто-повтора при
	//    удержании в эталоне НЕТ (одно нажатие = один ряд), и выдумывать его нельзя.
	EAsdaScrollAction Tick(bool bLmb, bool bLmbDownEdge, int32 MaxRow, int32& OutValue);

	// Текстовый дамп состояния (для консольных команд окон).
	FString Dump() const;
};
