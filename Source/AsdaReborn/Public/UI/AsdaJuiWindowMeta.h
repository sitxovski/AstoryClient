// Copyright (c) 2026 @sitxovski (Discord)
// AsdaJuiWindowMeta — МЕТАДАННЫЕ ОКОН, ВЫНУТЫЕ ИЗ ДАННЫХ .JUI (не из кода!).
//
// ★★★ ГЛАВНЫЙ ФАКТ РЕВЕРСА (Reports/ui_window_drag_and_hittest.md, части A и B):
//   • ПЕРЕТАСКИВАНИЕ окон в оригинале НЕ написано в коде окон. Оно ОБЪЯВЛЕНО В ДАННЫХ JUI записью
//     `EVS { EVT="6"; RET="20003"; CtrlID="<IDNum цели>" } EVE` внутри контрола-«ручки».
//     `EVT`/`RET` сериализуются через `%X` (`CUIMgr_SerializeControlTree @0x130A580`,
//     ui_core.c:121654) ⇒ `20003` = **0x20003**. Диспетчер `uiDispatchNotifyToControlByID
//     @0x12F1840` (ui_core.c:113829) по 0x20003 зовёт vtable+9 = `uiControl_RepositionByDelta
//     @0x12F1C30` (ui_core.c:113990): `pos += курсор_текущий − курсор_предыдущий`, клампа НЕТ.
//     ⇒ «Список перетаскиваемых окон» в коде существовать НЕ ДОЛЖЕН — он берётся из .JUI.
//   • ХИТ-ТЕСТ (`uiControlHitTest @0x12DB470`, ui_core.c:107127) прямоугольный, БЕЗ учёта альфы:
//     проверяются ПЛАНЫ контрола (`this+67`) и видимость всей цепочки предков. Атрибут
//     `NoSelect="1"` = ДЕКЛАРАТИВНОЕ «клик проходит насквозь» (в `gamemain_jui.ini` — 5768 контролов).
//     Без него полноэкранные «окна» (`WorldMap_Win` 1024×768, `Tutorial_WIN` 1023×180) накрыли бы экран.
//   • `AlwaysTop="1"` — окно живёт в отдельном верхнем списке менеджера (`topWindowList`, this+101)
//     и НЕ переупорядочивается подъёмом при клике (`uiWindowMgrDispatchMouse @0x12AF540`).
//
// Ассет генерируется импортёром `Tools/asda_jui_generic_umg.py` тем же проходом, что и сами WBP,
// поэтому имена виджетов в мете ГАРАНТИРОВАННО совпадают с реальным деревом окна. Правится в
// редакторе UE как обычный DataAsset (закон ui-editable-in-ue).
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "AsdaJuiWindowMeta.generated.h"

// Одна РУЧКА ПЕРЕТАСКИВАНИЯ окна = контрол с событием EVS{EVT=6, RET=0x20003}.
USTRUCT(BlueprintType)
struct ASDAREBORN_API FAsdaJuiDragHandle
{
	GENERATED_BODY()

	// Виджет-ручка в WBP (зона захвата). Импортёр создаёт для неё отдельную невидимую hit-зону
	//   `<idc|Name>_<idx>_DRAG`: у ручек скин `NoImage` (план есть, картинки нет), поэтому обычного
	//   image-виджета для них в WBP не появляется.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="JUI")
	FName HandleWidget;

	// IDNum контрола-ЦЕЛИ (аргумент `CtrlID` события).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="JUI")
	int32 TargetIdNum = -1;

	// ID цели строкой — для дампов/диагностики.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="JUI")
	FName TargetControl;

	// Корневое окно, В КОТОРОМ лежит цель. Обычно = само окно, но в данных оригинала есть один
	//   межоконный случай: `Gear_book_movetext` (80302) в `Gear_book_WIN` двигает
	//   `Inven_Strength_BOX3` (80200) внутри `INVEN_WIN`. Это ДАННЫЕ оригинала — воспроизводим 1:1.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="JUI")
	FName TargetWindowId;

	// Цель = КОРНЕВОЙ контрол окна ⇒ двигать надо всё окно (все прямые дети RootCanvas).
	//   Так у 120 ручек из 143. Остальные тянут ВЛОЖЕННОЕ под-окно (напр. `SHELL_EQUIP_TITLE` 6301
	//   двигает `SHELLITEM_EQUIP_WIN` 6296 внутри корня `SHELLITEM_STORE_WIN`).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="JUI")
	bool bTargetIsWindowRoot = false;

	// Виджеты цели, когда цель НЕ корень: либо один контейнер-группа `<ID>_grp` (если у цели есть
	//   дети — двигается всё поддерево), либо собственные виджеты контрола.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="JUI")
	TArray<FName> TargetWidgets;

	// ★ Ручка, у которой в ОРИГИНАЛЕ цель не резолвилась (`CtrlID` = -1 или несуществующий IDNum) —
	//   баг оригинала, ПОЧИНЕН по решению юзера (2026-08-06). Цель восстановлена по доминирующему
	//   правилу данных: цель = РОДИТЕЛЬСКИЙ контрол ручки (так у 95 из 138 рабочих ручек), а если
	//   родителя нет (ручка сама корень) — она сама (так у 25 ручек).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="JUI")
	bool bOriginalBugFixed = false;

	// ★★★ РУЧКА, КОТОРОЙ В ОРИГИНАЛЕ НЕ БЫЛО ВООБЩЕ — НАША ДОБАВКА, а не восстановление.
	//   Отличается от `bOriginalBugFixed` тем, что там EVS-блок в данных ЕСТЬ (сломан только
	//   `CtrlID`), а здесь во всём поддереве окна EVS нет ни одного и у окон-близнецов той же
	//   подсистемы его тоже нет — значит, оригинал это окно двигать не давал.
	//   Заводится по прямому решению владельца; ключ отката — `asda.UI.AddedDragHandles 0`
	//   (см. `UAsdaWindowSubsystem`): с ним такие ручки не работают, а «родные» продолжают.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="JUI")
	bool bAddedByPort = false;
};

// Метаданные одного КОРНЕВОГО окна .JUI.
USTRUCT(BlueprintType)
struct ASDAREBORN_API FAsdaJuiWindowMeta
{
	GENERATED_BODY()

	// ID корневого контрола (он же имя окна в реестре и суффикс ассета: `WBP_<WindowId>`).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="JUI")
	FName WindowId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="JUI")
	int32 IdNum = -1;

	// Размер корневого контрола в базе 1024×768 (диагностика).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="JUI")
	FVector2D BaseSize = FVector2D::ZeroVector;

	// `AlwaysTop="1"` у корня — окно не переупорядочивается подъёмом (см. шапку файла).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="JUI")
	bool bAlwaysTop = false;

	// Все ручки перетаскивания окна (у части окон их 2-3: `INVEN_WIN`, `ITEM_SWAP_WIN`, `QUEST_WIN`).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="JUI")
	TArray<FAsdaJuiDragHandle> DragHandles;

	// ★★★ Виджеты контролов с `NoSelect="1"` — клик проходит СКВОЗЬ них (см. шапку файла).
	//   Хит-тест окна обязан их пропускать, иначе полноэкранные подложки заблокируют игру.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="JUI")
	TArray<FName> NoHitWidgets;
};

// Ассет-реестр меты по всем классам .JUI (gamemain / login / commonwin / temp).
UCLASS(BlueprintType)
class ASDAREBORN_API UAsdaJuiWindowMetaAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	// Путь ассета, который ищет `UAsdaWindowSubsystem` (создаётся импортёром JUI).
	static const TCHAR* kAssetPath;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="JUI")
	TArray<FAsdaJuiWindowMeta> Windows;

	// Мета окна по id (nullptr — окна в .JUI нет либо ассет не перегенерирован).
	const FAsdaJuiWindowMeta* FindWindow(FName WindowId) const;
};
