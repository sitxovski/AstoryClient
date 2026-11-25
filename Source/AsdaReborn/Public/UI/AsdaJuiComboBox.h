// Copyright (c) 2026 @sitxovski (Discord)
// AsdaJuiComboBox — ★★★ ЕДИНЫЙ ВЫПАДАЮЩИЙ СПИСОК JUI (скин `Bk_ComboBox` + `DarkGrey_List`).
//
// ЗАЧЕМ ОБЩАЯ ТОЧКА. В `gamemain_jui.ini` контролов со скином `Bk_ComboBox` — **50 штук**.
//   `UComboBoxString`, который на такой узел ставит импортёр, рисуется ДВИЖКОВЫМ стилем (белая
//   плашка, чёрный текст) и к оригиналу отношения не имеет: раскрывающуюся часть он строит через
//   `SMenuAnchor` + `SListView`, а кисти меню (`WidgetStyle.MenuBorderBrush`) читаются на
//   КОНСТРУИРОВАНИИ `SComboBox` — после `AddToViewport` подменить их уже нельзя. Поэтому игровой
//   вид можно собрать только из тех же примитивов, из которых его рисует сам оригинал: плита с
//   UV-вырезкой атласа + строки текста + плита подсветки строки.
//   К моменту появления этого класса такой вид был написан в порту ТРИЖДЫ (чат 2608, карта мира
//   1721/1723, созвездие логина `CARAMAKE_cb`) с побайтово одинаковыми UV-константами. Четвёртой
//   копии (окно 68500 премиум-гачи) не заводилось — вместо неё заведён этот класс.
//
// ДАННЫЕ, А НЕ ПОДБОР. Все числа ниже взяты из `RE/docs/JuiSkin.ini`:
//
//   `Bk_ComboBox` (Type = "ComboBox", ListBoxName = "DarkGrey_List")
//     ES=0 ТЕЛО    Rect = "0 0 89 20"   CaptionPoint = "5 1"   CaptionAlign = "4"
//                  SS0 Diffuse FF213739, подпись FFFFFFFF
//                  SS1 Diffuse FF225777, подпись FFE9D6BC      ← наведение
//                  SS2 Diffuse FF153549, подпись FFE9D6BC      ← нажато/раскрыто
//     ES=1 СТРЕЛКА Rect = "90 0 110 20" ⇒ РОВНО 20×20, атлас N_02
//                  SS0/SS1 UV (0.785,0.520)-(0.824,0.559)
//                  SS2     UV (0.834,0.520)-(0.873,0.559)      ← второй кадр стрелки
//   `DarkGrey_List` (Type = "ListBox")
//     ES=0 SS=0    Rect = "0 0 50 20" ⇒ ВЫСОТА СТРОКИ 20; атлас N_02
//                  UV (0.285,0.816)-(0.371,0.840); CaptionColor FFE9D6BC
//
// ★ ПОДСВЕТКА СТРОКИ — ЭТО ДВИЖОК, А НЕ СКИН. В `DarkGrey_List` единственный ES/SS, планов
//   выделения нет; квад под строкой рисует рендер списка (`ui_core.c:112969`):
//     под курсором  → D3DRS_TEXTUREFACTOR = −1762594576 = **0x96F0F0F0** (альфа 150, цвет F0F0F0);
//     выбранная     → `~item[+2760]`, а это поле у списков, заведённых `CClass_DrawText(…, −1, 0)`,
//                     остаётся нулём ⇒ `~0` = **0xFFFFFFFF** (сплошная белая плита).
//
// ★ ПОЧЕМУ СТРЕЛКУ ПРИХОДИТСЯ ПРАВИТЬ. Полная ширина скина 110, а контролы в .JUI бывают уже
//   (68507/68508 — 170, 2608 — 100). Импортёр масштабирует ВСЕ планы пропорционально, поэтому
//   квадратная стрелка приезжает сплюснутой (замер дампа окна 68500: тело 137.5×20, стрелка
//   30.9×20). Движок для ComboBox кладёт стрелку СВОИМ размером к ПРАВОМУ краю, а тянет только
//   тело — так делает и `Setup()`.
//
// ЧТО КЛАСС БЕРЁТ ИЗ WBP (ничего не выдумывая, закон `no-invented-ui`):
//   `<idc>`          — `UComboBoxString` импортёра: источник ПРЯМОУГОЛЬНИКА и родителя, сам гасится;
//   `<idc>_<n>_0`    — план тела, `<idc>_<n>_1` — план стрелки (закон `jui-importer-widget-naming`);
//   `<idc>_Text`     — подпись выбранного пункта (в неё импортёр положил дизайнерскую рыбу
//                      `CaptionText`, напр. «ABC123»; класс её ЗАМЕЩАЕТ реальным значением).
// Что создаёт сам: `<idc>_List` (канвас), `<idc>_ListBg`, `<idc>_RowBg{i}`, `<idc>_Row{i}`.
#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "UI/AsdaUiInputTypes.h"   // ★ R1: FAsdaMouseFrame / FAsdaDragArm / FAsdaDragGhost … — поля ввода (без всего AsdaUiHelpers.h)
#include "AsdaJuiComboBox.generated.h"

class UUserWidget;
class UPanelWidget;
class UCanvasPanel;
class UComboBoxString;
class UImage;
class UTextBlock;

/** Выбран пункт с индексом Idx (индекс в массиве, переданном в `SetItems`). */
DECLARE_DELEGATE_OneParam(FAsdaComboPicked, int32 /*Idx*/);

UCLASS()
class ASDAREBORN_API UAsdaJuiComboBox : public UObject
{
	GENERATED_BODY()

public:
	/** Привязаться к контролу `Idc` внутри окна `Host`.
	 *  @param OwnerWindowId — id окна в `UAsdaWindowSubsystem`: нужен для `PushTopmost`, чтобы
	 *         раскрытый список лёг ПОВЕРХ остальных окон и клик по нему не провалился в мир.
	 *  @param bOpenUp — раскрывать список ВВЕРХ (когда снизу места нет, как у чата 2608).
	 *  @param InPlaneIdc — ★ ИМЯ, ПОД КОТОРЫМ ИМПОРТЁР НАЗВАЛ ПЛАНЫ, если оно ОТЛИЧАЕТСЯ от `InIdc`.
	 *         Пусто (по умолчанию) = планы называются `<InIdc>_<jui>_<plane>`, как у контролов с
	 *         заполненным `ID` в .JUI. Но у 151 интерактив-контрола в 17 окнах поле `ID` ПУСТОЕ
	 *         (`PORT_PENDING_ON_SYSTEMS.md`, п. 70), и тогда импортёр именует планы по СКИНУ —
	 *         например у комбо разрешения окна `SYSTEM_Option_NEW` сам виджет зовётся
	 *         `SYSTEM_Op_SYSTEM_SYS22_CMB00`, а его планы — `Bk_ComboBox_245_0` / `_1` и подпись
	 *         `Bk_ComboBox_245_txt`. Здесь передаётся именно `Bk_ComboBox_245`.
	 *         Подпись ищется сначала как `<InIdc>_Text` (обычный случай), затем как
	 *         `<InPlaneIdc>_txt` (случай пустого `ID`).
	 *  false — контрола нет в дереве окна (тогда это дефект привязки, и он попадает в лог). */
	bool Setup(UUserWidget* InHost, const FString& InIdc, FName InOwnerWindowId, bool bInOpenUp,
		const FString& InPlaneIdc = FString());

	bool IsReady() const { return bReady; }

	/** Полный состав списка. Подписи — уже готовые строки (локализацию делает вызывающий). */
	void SetItems(const TArray<FString>& Labels);

	void  SetSelectedIndex(int32 Idx);
	int32 GetSelectedIndex() const { return SelectedIndex; }
	int32 NumItems() const { return ItemLabels.Num(); }
	/** Подпись пункта (для дампов и живых проверок). Пустая строка — индекс вне списка. */
	FString GetItemLabel(int32 Idx) const { return ItemLabels.IsValidIndex(Idx) ? ItemLabels[Idx] : FString(); }

	void SetOpen(bool bInOpen);
	bool IsOpen() const { return bOpen; }

	/** Кадровый разбор мыши. Кадр опрашивает ВЛАДЕЛЕЦ (одна `FAsdaMouseFrame` на окно) и передаёт
	 *  сюда — второй опрос в том же кадре съел бы фронты (закон общей точки `FAsdaMouseFrame`). */
	void Tick(const FAsdaMouseFrame& Mouse);

	/** Дерево окна пересоздано ⇒ все указатели мертвы. Зовётся владельцем из его `EnsureWindows`. */
	void Reset();

	/** Экранный прямоугольник РАСКРЫТОГО списка (для `FAsdaUiZoneRect` слоя «поверх всего»). */
	bool GetListScreenRect(FVector2D& OutMin, FVector2D& OutMax) const;

	FAsdaComboPicked OnPicked;

	// ── Числа скина (публичные: ими пользуется раскладка владельца) ──
	static constexpr float kRowH   = 20.f;   // `DarkGrey_List` ES0 Rect height
	static constexpr float kArrowW = 20.f;   // `Bk_ComboBox`  ES1 Rect = "90 0 110 20"

private:
	void RebuildRows();
	void ApplyBodyState();
	void ApplyListGeometry();
	/** Порт `CControl_SetVisible` у раскрытого списка + слой «поверх всего». */
	void PushOrPopTopmost();

	UPROPERTY(Transient) TObjectPtr<UUserWidget>     Host = nullptr;
	UPROPERTY(Transient) TObjectPtr<UComboBoxString> Engine = nullptr;   // движковый комбо: гасится
	UPROPERTY(Transient) TObjectPtr<UImage>          BodyPlane = nullptr;  // ES0
	UPROPERTY(Transient) TObjectPtr<UImage>          ArrowPlane = nullptr; // ES1
	UPROPERTY(Transient) TObjectPtr<UTextBlock>      Label = nullptr;      // `<idc>_Text`
	UPROPERTY(Transient) TObjectPtr<UCanvasPanel>    List = nullptr;
	UPROPERTY(Transient) TObjectPtr<UImage>          ListBg = nullptr;
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>>     RowBgs;
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> Rows;

	FString Idc;
	// Имя, под которым импортёр назвал ПЛАНЫ и подпись этого контрола. Совпадает с `Idc`, кроме
	//   контролов с пустым `ID` в .JUI (там это имя скина с индексом, напр. `Bk_ComboBox_245`).
	FString PlaneIdc;
	FName   OwnerWindowId;
	FName   ClaimId;                       // id заявки на слой «поверх всего»
	bool    bOpenUp = false;
	bool    bReady = false;
	bool    bOpen = false;
	bool    bTopmostPushed = false;
	int32   SelectedIndex = INDEX_NONE;
	int32   HoverRow = INDEX_NONE;
	TArray<FString> ItemLabels;

	FVector2D BodyPos = FVector2D::ZeroVector;    // прямоугольник контрола в координатах родителя
	FVector2D BodySize = FVector2D::ZeroVector;
};
