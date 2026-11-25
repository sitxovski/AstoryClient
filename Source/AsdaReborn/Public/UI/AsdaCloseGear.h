// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ ОБЩАЯ ТОЧКА «ШЕСТЕРЁНКА У КРЕСТИКА» — UAsdaCloseGearSubsystem (08.11.2026, пачка 19 баг 2, ключ `asda.UI.CloseGearHover`).
//
// ═══ ЭТАЛОН ═══
//   Картинку оригинал НЕ поворачивает (поле поворота контрола +0x160 в `CControl_BuildPlaneVertices@0x12DE440` у кнопок
//   никто не пишет). У окна две шестерёнки: статичная `*_Gear` (видима) и кадровая `*_AniGear` (JUI `Visible="0"`, скин
//   `Ani_Gear_50_InnerCircle` / `Ani_Gear_50`: NE_wheelwin1.tga, 4 кадра 50×50, FrameInterval 300 мс на прогон 0→3, по кругу,
//   EFrameStop 0). Обработчик окна на наведение (событие 8) по id крестика:
//       CControl_SetVisible(Gear, 0); CControl_SetVisible(AniGear, 1); CControl_Show(AniGear);      // показ = прогон с кадра 0
//   на уход курсора (событие 0xA) — обратное и `CControl_CloseHide(AniGear)`.
//   Места (AsdaStory_game.c): NPCShop 1302 (:448667), обмен P2P 2050 (:292649), отряд 2279 (:218974), сообщество 6403
//   (:241818), журнал 702 (:285638), обмен вещей 15265 (:301379), сумка 403 (:348218), посредник 13004 (:361960),
//   навыки 12009 (:462082), персонаж 980 (:479344), склад 4050 (:485348), система 801 (:490221), звания 18703 (:491199).
//   Чат 205/206 (`uiPanel_ShowAndSelectActive@0xBB4310`) лежит в старом окне CHAT_BACKWIN 214, которое в оригинале
//   невидимо (живой дамп, AsdaChatWindowSubsystem.h) — не показывается, в таблицу не входит. MMap/HMap/Chat_Open/
//   Party_AniGearH без IDNum — ссылок в срезе нет. Анимации открытия/закрытия окон в оригинале нет (окна мгновенны).
//
// ═══ ПОРТ ═══
//   Раньше: копии в шести подсистемах (сумка, склад, навыки, персонаж, отряд, обмен P2P), в семи окнах не было ничего,
//   а навыки/персонаж/склад показывали кадровый план без перезапуска прогона. Теперь одна таблица и один тик:
//   курсор над крестиком (`AsdaUi::IsCursorOverWidget` — с проверкой верхнего окна) ⇒ `*_Gear` спрятан, `*_AniGear` показан и
//   его кадровые планы перезапущены с кадра 0; иначе — обратно и `StopAnim`. Ключ 0 — прежние копии подсистем.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Containers/Ticker.h"
#include "AsdaCloseGear.generated.h"

class UUserWidget;
class UWidget;

namespace AsdaCloseGear
{
	// Ключ `asda.UI.CloseGearHover` включён: шестерёнки ведёт общая точка, копии подсистем молчат.
	ASDAREBORN_API bool IsCommonOn();
}

// Строка таблицы: окно и три узла.
struct FAsdaCloseGearRow
{
	FName Window;          // корень окна JUI (id окна в реестре)
	const TCHAR* Close;    // idc крестика (точное имя интерактивного узла)
	const TCHAR* Gear;     // idc статичной шестерёнки (префикс плана/группы)
	const TCHAR* Ani;      // idc кадровой шестерёнки (префикс плана/группы)
	int32 CloseId;         // IDNum крестика (для журнала)
};

// Узлы одной строки в живом окне.
struct FAsdaCloseGearLive
{
	TWeakObjectPtr<UUserWidget> Win;   // окно, по которому найдены узлы
	TWeakObjectPtr<UWidget> Close;     // крестик
	TWeakObjectPtr<UWidget> Gear;      // статичная
	TWeakObjectPtr<UWidget> Ani;       // кадровая
	bool bOver = false;                // курсор над крестиком (последнее применённое)
	bool bApplied = false;             // состояние хоть раз применено к этому окну
};

UCLASS()
class ASDAREBORN_API UAsdaCloseGearSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// Таблица окон (эталон, см. шапку).
	static const TArray<FAsdaCloseGearRow>& Rows();

	// Самотест: узлы строки окна (после тика), false — окна/узлов нет.
	bool DebugNodes(FName Window, UWidget*& OutClose, UWidget*& OutGear, UWidget*& OutAni);
	// Самотест: прогнать тик сейчас.
	void DebugTickNow() { TickAll(); }

private:
	// Тик (FTSTicker): все строки таблицы.
	bool OnTick(float Dt);
	// Пройти таблицу.
	void TickAll();
	// Найти узлы строки в окне.
	void Resolve(const FAsdaCloseGearRow& R, UUserWidget* Win, FAsdaCloseGearLive& L) const;
	// Применить «над крестиком / нет».
	static void Apply(FAsdaCloseGearLive& L, bool bOver);

	FTSTicker::FDelegateHandle TickHandle;   // тик
	TArray<FAsdaCloseGearLive> Live;         // по строкам таблицы
};
