// Copyright (c) 2026 @sitxovski (Discord)
// UAsdaMinimapSubsystem — МИНИ-КАРТА (JUI-окно 1702 MINIMAP_WIN), её свёрнутая иконка (1700
//   MINIMAPMIN_WIN) и БОЛЬШАЯ КАРТА (1710 MAXMAP_WIN). Полный порт подсистемы CMiniMap/CMiniMapWnd.
//
// ─────────────────────────────── ЭТАЛОН (адреса runtime VA, ASLR off) ────────────────────────────
// РЕНДЕР. Карта — НЕ JUI-контрол: её рисует класс CMap квадом D3D, окно 1702 — только рамка/кнопки.
//   CRenderer_RenderWorldFrame@0x9773B0 (render_core.c:1482) под глобальным битом «UI видим»:
//     uiMiniMapRender@0xB4EFE0   → if (CMap+0x248) MiniMap_RenderBigMap@0xB4FCC0   // ДО контролов
//     WindowMgr_RenderControls   → JUI-контролы окна поверх карты
//     uiBigMapRender@0xB4F080    → if (CMap+0x244) CBigMap_RenderAndHandleClick@0xB521F0
//   → в UMG: картинка карты = первый ребёнок корневого канваса виджета (под всеми контролами).
//
// ПОЛЯ СОСТОЯНИЯ CMap (эталон, RE/ida/gap_breakout.tsv):
//   +0x240 self[144] mapMode   — 1 = РАЗВЁРНУТАЯ (окно 260×260, карта 256×256), 0 = КОМПАКТНАЯ (134/130)
//   +0x244 self[145] fadeMode  — «большая карта активна» (timerSetModeAndStart@0xB4D3C0)
//   +0x248 self[146] isVisible — «мини-карта активна» (uiWindowSetAlphaArgb@0xB4E260: alpha != 0)
//   +0x24C self[147] zoomFlag  — зум (set_o93@0xB4D390, thunk sub_930E17; кнопка 1706 MINIMAP_ZOOM)
//   +0x34C self[211] bTownMap  — ГОРОД/ПОЛЕ (set_oD3@0xA1A680, thunk sub_93539F)
//
// ★★ ГОРОД / ПОЛЕ — ЧТО ИМЕННО МЕНЯЕТСЯ (реверс подтверждён построчно):
//   Бит 0x80 атрибута тайла .TER мини-карту НЕ включает и НЕ выключает. Он переключает флаг
//   CMap+0x34C через sub_93539F(g_pMiniMap, 0|1) (entity_char.c:20986-20997, там же смена BGM), а
//   следом entity_char.c:20998 зовёт worldMapDlg_SelectCurrentMap@0xD66A20 → MiniMap_OpenBigMap@0xD66D40.
//   Влияние флага РОВНО одно — на БОЛЬШУЮ карту:
//     (1) ТЕКСТУРА. CBigMap_RenderAndHandleClick: bTownMap==1 → CMap+512 (<map>_town_big.TEX),
//         иначе CMap+500 (<map>_big.TEX). Загрузка — CMap_LoadBigMapTexture@0xB4B060.
//     (2) КРОП/МАСШТАБ МАРКЕРОВ. uiMapPointWithZoom@0xB4DC30: при bTownMap==1
//         pt = (pt − CropOrigin) * CropScale; значения ставит hudIconSetUvBySlot@0xB54450:
//           mapId 0 silalis → (256,256)×2,  mapId 3 alpen → (25,231)×2,  mapId 7 flammio → (0,0)×2.
//     (3) НАБОР КАРТ. isSpecialSlotIndex@0xB54400 == 1 только для mapId 0/3/7 — только у них есть
//         <map>_town_big.TEX (проверено по клиенту: alpen/flammio/silalis, больше ни у кого).
//         inventory_ToggleControlBarWindow (item_inventory.c:50224) дополнительно ГАСИТ городской
//         режим на mapId 1 (rein): `if (attr&0x80) { town(1); if (curMap==1) town(0); }`.
//   КАРТИНКА САМОЙ МИНИ-КАРТЫ ОТ ЗОНЫ НЕ ЗАВИСИТ: всегда <map>.TEX (CMap+496,
//   CMap_LoadTownTexture@0xB4AF40). Ветка <map>_Town.TEX там мёртвая (флаг никогда не 1) и таких
//   файлов в клиенте НЕТ. Масштаб/зум/рамка/прозрачность мини-карты от города тоже не зависят.
//
// ГЕОМЕТРИЯ (uiMinimapBuildQuads@0xB4CE20 + MiniMap_RenderBigMap):
//   КОМПАКТ:    окно 133×134 @(ScreenW−133, 34);  квад карты 130×130 @(ScreenW−133, 36)
//   РАЗВЁРНУТО: окно 260×260 @(ScreenW−260, 34);  квад карты 256×256 @(ScreenW−259, 36)
//   MiniMap_SetMaxMin@0xD64E70 меняет размеры окна 134↔260 и рамки MMap_BkBoder 130↔256.
//
// МИР → ТЕКСЕЛ КАРТЫ (sub_93D5F9 → sub_B4E080@0xB4E080, множители из uiMinimapBuildQuads):
//   Texel = World * (256 / (TileCountX * 100))            ← ОБЕ оси через TileCountX (так в оригинале)
//   Видимое окно в текселах: развёрнуто zoom?128:256, компакт zoom?64:128
//   Scroll  = clamp(Texel − View/2, 0, 256 − View)        (uiClampUVRectTo01@0xB4DDA0 + world_map.c:2650)
//   UV      = (Scroll .. Scroll+View) / 256
//   Пиксель = (Texel − Scroll) * (zoom ? 2 : 1)           (world_map.c:2657-2662 / 2887-2896)
//
// КЛИК ПО КАРТЕ = ИДТИ (world_map.c:2596-2612): тайл = пиксель_клика (с учётом scroll/зума) →
//   world = 100*тайл + 50 → валидация грунта → move; иначе нотис 1013 + error.wav; троттл 250 мс.
//
// МАРКЕРЫ (текстуры грузит CMiniMap_Init@0xB4BDC0 из ./Map/<name>.TEX):
//   miniMyPos 16×16 — игрок (поворот на yaw персонажа);  miniMyDir 64×64 — конус взгляда (yaw камеры);
//   busDestPos 16×16 — точка автобега;  miniPartyPos — группа;  miniGulidPos — клан;
//   miniCouplePos — супруг(а);  miniQuest/miniQuest2 16×16 — квест-точки (мигание 1000/200 мс);
//   FishingArea 16×16 — зоны рыбалки.
//
// ══════════════════════ ★ КАРТА МИРА = окно 1710 MAXMAP_WIN (НЕ WorldMap_Win!) ═══════════════════
// КАКОЕ ОКНО АКТУАЛЬНОЕ. В JUI три «карточных» окна:
//   1710  MAXMAP_WIN     690×519 — БОЛЬШАЯ КАРТА ТЕКУЩЕГО/ВЫБРАННОГО РЕГИОНА. Живая: 42 контрола,
//                                  класс CMiniMapWnd, рендер CBigMap_RenderAndHandleClick@0xB521F0.
//   58000 WorldMap_Win  1024×768 — «атлас мира» из 15 иконок регионов. МЁРТВАЯ: ни одного упоминания
//                                  id 58000/58001..58015/58100/58109 во ВСЁМ бинаре клиента (проверен
//                                  побайтовый поиск dword по всему файлу — 0 совпадений), обработчиков
//                                  нет, у всех её узлов скин NoImage/NoImage2 (картинки атласа не
//                                  существует). Кнопка перехода к ней MaxMap_Worldmap_btn (1735) тоже
//                                  без обработчика, а её группа MaxMap_Worldmap (1736) в JUI Visible=0.
//   49900 CAMPWAR_L_MAP  200×200 — мини-схема осадной зоны, отдельная фича (не карта мира).
//   Дублей с суффиксом NEW у карточных окон в JUI НЕТ (NEW есть у SYSTEM/QUICK/INVEN/CHAR/ETC).
//   ⇒ портируем 1710; 58000 не портируем (легаси), 1735/1736 держим скрытыми, как в JUI.
//
// ОТКРЫТИЕ/ЗАКРЫТИЕ — три входа, все ведут в inventory_ToggleControlBarWindow@0xD64A30:
//   • КЛАВИША M — Shortcutkey.ini строка 38 «INTERFACE MAP: M» ⇒ cmd 37; hotkeyDispatchAction@0xE3CD90
//     case 37 → inventory_ToggleControlBarWindow (цепочка cmd 30..38 сверена со строками ini один-в-один);
//   • кнопка мини-карты 1707 MINIMAP_MAXBUT → MiniMap_Window_OnCommand@0xD64050 case 0x6AB;
//   • закрытие: 1711 MAXMAP_CLOSE (case 0x6AF → miniMapWndClose@0xD63FF0) и событие 21 у окна 1710.
//   ЗАПРЕТЫ ОТКРЫТИЯ (нотис в чат, окно не открывается): парикмахерская 99000, элитный магазин 99001,
//     магазин бустеров 110611, модальный нотис 99002; молча — при открытом item-main/qty-dlg;
//     в подземелье (слот+68 && тип≠14, либо тип 21) карта ПРИНУДИТЕЛЬНО закрывается + нотис 13908.
//   ЗВУКИ: открытие 5, закрытие 6 (csoundPlayName2DStore).
//
// СОДЕРЖИМОЕ ОКНА:
//   • подложка 512×512 — <map>_big.TEX, в городском режиме <map>_town_big.TEX (только mapId 0/3/7);
//   • маркеры (те же 9 текстур, что у мини-карты) — игрок+конус, точка автобега/перехода, группа,
//     клан, соулмейт, квест-точки, зоны рыбалки; легенда 1725..1728 = нотисы 15203..15206
//     (иконки Quest_Birth / Quest_Done / Map_PartyMem / Map_Soulmate);
//   • список КОНТИНЕНТ 1721 (нотисы 9601 «Альпеон» / 9602 «Инферно») и ОБЛАСТЬ 1723 — таблица
//     uiInitSlotIndexMaps@0xD66320 + подписи helpWndFillNoticeList@0xD66880 (см. FAsdaWorldMapArea);
//   • кнопка «Где я?» 1724 (нотис 9699) → this[64]=−1 + worldMapDlg_SelectCurrentMap@0xD66A20;
//   • подсказка 1730 = нотис 9698, подписи списков 1720 = 9600, 1722 = 9610;
//   • координаты 1733/1734 = ТАЙЛ ПОД КУРСОРОМ (sprintf "%03d"), а НЕ позиция игрока;
//   • курсор над картой: 18 «идти» / 20 «нельзя» (Cursor_SetByIndex), радар-эффект 1731 при открытии,
//     перетаскивание окна за 1713 MAXMAP_MOVE (событие 6 → miniMap_SetupQuadVerts(pos+3)).
//   ЗУМА У БОЛЬШОЙ КАРТЫ НЕТ: поле всегда 512×512 текселов 1:1, прокрутки нет (scrollMax = 512−512 = 0).
//
// МИР → ПИКСЕЛЬ У БОЛЬШОЙ КАРТЫ (uiMapPointWithZoom@0xB4DC30 + CBigMap_RenderAndHandleClick):
//   px = ((tile − CropOrigin) * CropScale) * (512 / TileCountX)      // кроп только в городском режиме
//   обратно (клик/наведение): tile = scrollOff + local_px * (512/512) = local_px, world = 100*tile + 50
//   Кроп городов (hudIconSetUvBySlot@0xB54450): mapId 0 → (256,256)×2, 3 → (25,231)×2, 7 → (0,0)×2.
//
// КЛИК ПО КАРТЕ (троттл 250 мс, только при зажатой ЛКМ и «взведённом» push у 1729):
//   показана СВОЯ карта → проверка грунта → идти (иначе нотис 1013 + error.wav);
//   показана ЧУЖАЯ карта → beginMoveToTile@0xFBE140: заявка на межкартовый переход (mapId, tileX, tileY),
//   которую исполняет система авто-маршрута; заявка блокируется при торговле/складе, тайл ≤ 512.
#pragma once

#include "CoreMinimal.h"
#include "AsdaTickGate.h"   // ★ R4-b (14.11.2026): шаблон класса (CDO) не тикает
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "Types/SlateEnums.h"          // ESelectInfo — параметр колбэков ComboBoxString (1721/1723)
#include "AsdaSessionReset.h"   // IAsdaSessionResettable — сброс состояния сессии
#include "UI/AsdaHelpTopics.h"  // FAsdaHelpTipHover — справка кнопок 1704…1719
#include "AsdaMinimapSubsystem.generated.h"

class UUserWidget;
class UWidget;
class UImage;
class UTextBlock;
class UCanvasPanel;
class UComboBoxString;
class UTexture2D;
class UAsdaWalkGrid;
class AAsdaPlayerCharacter;

// Тип маркера = какой .TEX ставится под квад (эталонные поля CMap).
UENUM(BlueprintType)
enum class EAsdaMapMark : uint8
{
	Player      UMETA(DisplayName = "miniMyPos"),      // сам игрок, 16×16, поворот = yaw персонажа
	PlayerDir   UMETA(DisplayName = "miniMyDir"),      // конус взгляда, 64×64, поворот = yaw камеры
	Destination UMETA(DisplayName = "busDestPos"),     // точка автобега/телепорта, 16×16
	Party       UMETA(DisplayName = "miniPartyPos"),   // участник группы, 16×16
	Guild       UMETA(DisplayName = "miniGulidPos"),   // соклановец, 16×16
	Couple      UMETA(DisplayName = "miniCouplePos"),  // супруг(а)/соулмейт, 16×16
	Quest       UMETA(DisplayName = "miniQuest"),      // квест-точка, 16×16, мигает
	Quest2      UMETA(DisplayName = "miniQuest2"),     // квест-точка (тип 2), 16×16, мигает
	Fishing     UMETA(DisplayName = "FishingArea"),    // зона рыбалки, 16×16
	// ★ [ПОРТ] 31.10.2026 цель взятого задания на КАРТЕ МИРА (моб/зона/NPC сдачи), 16×16. У оригинала таких значков нет
	//   (решение владельца «добавить»); картинка — неиспользуемый маркер оригинала `./Map/miniFriendPos.TEX` (оранжевая
	//   точка: ни «!» квестодателя, ни точки группы/клана/пары). Ключ отката `asda.Map.QuestTargets 0`.
	QuestTarget UMETA(DisplayName = "miniFriendPos"),
};

// Одна внешняя отметка на карте (позиция в ТАЙЛАХ, как во всех списках оригинала).
USTRUCT(BlueprintType)
struct FAsdaMapMark
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "Asda|Minimap") int32 TileX = 0;
	UPROPERTY(BlueprintReadWrite, Category = "Asda|Minimap") int32 TileY = 0;
	UPROPERTY(BlueprintReadWrite, Category = "Asda|Minimap") int32 MapId = -1;   // −1 = «на текущей карте»
	UPROPERTY(BlueprintReadWrite, Category = "Asda|Minimap") EAsdaMapMark Kind = EAsdaMapMark::Party;
	// ★ 31.10.2026: подсказка у значка цели задания на карте мира (заголовок — название задания, тело — строка цели
	//   в формате журнала «=> цель - карта, x:…, y: …»). У прочих видов пусто — подсказки нет.
	UPROPERTY(BlueprintReadWrite, Category = "Asda|Minimap") FString TipTitle;
	UPROPERTY(BlueprintReadWrite, Category = "Asda|Minimap") FString TipBody;
};

// ─────────────────────────── ★ КАРТА МИРА (окно 1710 MAXMAP_WIN) ────────────────────────────────
// Одна строка выпадающего списка «Область» (1723 MaxMap_Area).
// ИСТОЧНИК — ДВЕ функции эталона, сверенные между собой один-в-один:
//   uiInitSlotIndexMaps@0xD66320  — таблица (индекс строки → mapId, городской флаг), два списка:
//                                   this+58 = континент 0 (10 строк), this+61 = континент 1 (9 строк);
//   helpWndFillNoticeList@0xD66880 — подписи строк: континент 0 → нотисы 96110+i (i=0..9),
//                                   континент 1 → нотисы 96210+i (i=0..8).
// Сверка сошлась полностью: городской флаг стоит РОВНО у mapId 0/3/7 — тех же, что даёт
//   isSpecialSlotIndex@0xB54400 (единственные карты с файлом <map>_town_big.TEX).
USTRUCT(BlueprintType)
struct FAsdaWorldMapArea
{
	GENERATED_BODY()

	// 0 = «Альпеон» (заголовок — нотис 9601), 1 = «Инферно» (9602). Эталон: continent = (mapId>=12 || mapId==7).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asda|WorldMap") int32 Continent = 0;
	// Числовой id карты оригинала (тот же, что в DT_MapTable и в g_nCurMapId).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asda|WorldMap") int32 MapId = 0;
	// Городской вариант подложки (<map>_town_big.TEX + кроп hudIconSetUvBySlot). Только mapId 0/3/7.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asda|WorldMap") bool  bTown = false;
	// string-ID подписи строки в DT_GameText (96110+i / 96210+i).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asda|WorldMap") int32 TextId = 0;
};

// Запрос межкартового перехода: клик по карте ДРУГОЙ карты (эталон beginMoveToTile@0xFBE140 —
//   выставляет g_bMapTransferActive + целевые mapId/tileX/tileY; исполнитель — авто-маршрут через порталы).
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FAsdaMapTravelRequest, int32, MapId, int32, TileX, int32, TileY);

UCLASS()
class ASDAREBORN_API UAsdaMinimapSubsystem : public UGameInstanceSubsystem, public FTickableGameObject,
	public IAsdaSessionResettable
{
	GENERATED_BODY()
	friend struct FAsdaFishZoneSelfTest;   // ★ 08.10.2026 (рыбалка В4): самотест гейта «идёт рыбалка» `asda.Fish.ZoneSelfTest`
	friend struct FAsdaNavAttackSelfTest;  // ★ 26.10.2026: самотест `asda.Combat.SelfTestNavAttack` — клик по мини-карте (HandleMapClick)
	friend struct FAsdaQuestDialogRewardSelfTest;   // ★ 31.10.2026: самотест `asda.SelfTest.WorldMapMarks` — списки значков целей (Marks)
	friend struct FAsdaGmMapTeleportSelfTest;      // ★ 09.11.2026: самотест `asda.SelfTest.GmMapTeleport` — Ctrl+клик по картам (HandleMapClick)

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// ── IAsdaSessionResettable (К-3) ──
	// Окно, его защёлка «запрос в полёте» и полученные от сервера данные
	// принадлежат СЕССИИ и переживали разрыв связи вместе с GameInstance.
	virtual void ResetSessionState(EAsdaSessionResetReason Reason) override;

	virtual void Tick(float DeltaTime) override;
	// Курсор сейчас подменён наведением на мини-карту (самотест).
	bool IsMiniCursorOverridden() const { return bMiniCursorOverridden; }
	// Сколько кликов по полю мини-карты принято (самотест).
	int32 GetMapClickCount() const { return MapClickCount; }
	// ★ 09.11.2026: клики, забранные GM-телепортом Ctrl+ЛКМ (самотест).
	int32 GetGmTeleportClickCount() const { return GmTeleportClickCount; }
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UAsdaMinimapSubsystem, STATGROUP_Tickables); }
	// ★ 23.10.2026: переложить панель занятости 79900 — её зовёт панель Друга 6500 при показе/гашении
	//   (панель занятости обязана стоять ПОД панелью Друга, как в .JUI: 6500 занимает y 200…300, занятость — y 300).
	void RelayoutOccupationBox() { LayoutOccupationBox(); }
	// ★ R4-b (14.11.2026): шаблон класса (CDO) не тикает — раньше `Tick` звался дважды за кадр, у рабочего объекта и у шаблона
	//   (ключ `asda.Perf.NoCdoTick`, разбор — `AsdaTickGate.h`). Рабочий объект тикает как раньше.
	virtual bool IsTickable() const override { return AsdaTickGate::NotCdo(this); }
	virtual bool IsTickableInEditor() const override { return false; }
	virtual bool IsTickableWhenPaused() const override { return false; }

	// ── Состояние (эталонные поля CMap) ──
	UFUNCTION(BlueprintCallable, Category = "Asda|Minimap") bool IsMinimapVisible() const { return bVisible; }
	UFUNCTION(BlueprintCallable, Category = "Asda|Minimap") bool IsExpanded() const { return bExpanded; }
	UFUNCTION(BlueprintCallable, Category = "Asda|Minimap") bool IsZoomed() const { return bZoom; }
	UFUNCTION(BlueprintCallable, Category = "Asda|Minimap") bool IsTownMode() const { return bTownMode; }
	UFUNCTION(BlueprintCallable, Category = "Asda|Minimap") bool IsBigMapOpen() const { return bBigMapVisible; }
	// ★ Текущая ширина ОКНА мини-карты в px канваса (133 компакт / 260 развёрнуто) — порт эталонного
	//   `GetSize(g_pMiniMapWnd)` (виртуальный слот +24): его читает авто-якорь окна хранителя 56000
	//   (`cameraTarget_UpdateAbovePlayer@0xEF6790`: X = screenW − minimapW − 145). Эталон берёт размер
	//   БЕЗ оглядки на видимость окна — здесь так же.
	UFUNCTION(BlueprintCallable, Category = "Asda|Minimap") float GetWindowWidthPx() const { return WinW(); }

	// ── Действия кнопок окна (1:1 обработчики MiniMap_Window_OnCommand@0xD64050) ──
	// 1708 MINIMAP_UPDOWN — развернуть/свернуть (uiDlgExpandPanelByCtrlId / uiMinimapWindow_OnClose).
	UFUNCTION(BlueprintCallable, Category = "Asda|Minimap") void SetExpanded(bool bOn);
	// 1706 MINIMAP_ZOOM — зум (sub_930E17).
	UFUNCTION(BlueprintCallable, Category = "Asda|Minimap") void SetZoom(bool bOn);
	// 1704 MINIMAP_CLOSE — свернуть в иконку 1700 (uiDlgCollapseToCompact), 1701 — обратно.
	UFUNCTION(BlueprintCallable, Category = "Asda|Minimap") void SetMinimapVisible(bool bOn);
	// ★ Та же операция, но с ИМЕНЕМ ИНИЦИАТОРА в логе. Диагностика жалобы «мини-карта сама
	//   сворачивается» (`Reports/minimap_collapse_fix.md`): по строке `[миникарта] видимость=…`
	//   раньше нельзя было понять, кнопка это или чужой код. Теперь причина видна сразу.
	//   Blueprint-обёртка выше зовёт этот метод с причиной «внешний вызов».
	void SetMinimapVisibleFrom(bool bOn, const TCHAR* Reason);
	// 1707 MINIMAP_MAXBUT — открыть/закрыть большую карту (inventory_ToggleControlBarWindow).
	UFUNCTION(BlueprintCallable, Category = "Asda|Minimap") void ToggleBigMap();
	UFUNCTION(BlueprintCallable, Category = "Asda|Minimap") void SetBigMapVisible(bool bOn);

	// ───────────────────────────── ★ КАРТА МИРА (1710 MAXMAP_WIN) ────────────────────────────────
	// ЕДИНАЯ точка входа окна карты. Зовут ТРИ места (все три — 1:1 эталон):
	//   • клавиша M            — Shortcutkey.ini «INTERFACE MAP: M» → cmd 37 → hotkeyDispatchAction case 37;
	//   • кнопка мини-карты    — 1707 MINIMAP_MAXBUT → MiniMap_Window_OnCommand case 0x6AB;
	//   • главный интерфейс    — задел: вызывать этот же метод из панели QUICK_NEW_WIN, когда её портируют.
	// Полностью повторяет inventory_ToggleControlBarWindow@0xD64A30 (включая запреты и звуки 5/6).
	UFUNCTION(BlueprintCallable, Category = "Asda|WorldMap") void ToggleWorldMap();
	UFUNCTION(BlueprintCallable, Category = "Asda|WorldMap") bool IsWorldMapOpen() const { return bBigMapVisible; }

	// Строки списка «Область» (см. FAsdaWorldMapArea). Правится в редакторе через Blueprint-геттер
	//   подсистемы; по умолчанию заполняется таблицей эталона (BuildDefaultAreas).
	UFUNCTION(BlueprintCallable, Category = "Asda|WorldMap") const TArray<FAsdaWorldMapArea>& GetWorldMapAreas() const { return WorldMapAreas; }
	UFUNCTION(BlueprintCallable, Category = "Asda|WorldMap") void SetWorldMapAreas(const TArray<FAsdaWorldMapArea>& In);

	// 1721 MaxMap_Continent — выбрать континент (перезаполняет список областей, эталон helpWndFillNoticeList).
	UFUNCTION(BlueprintCallable, Category = "Asda|WorldMap") void SelectContinent(int32 Continent);
	// 1723 MaxMap_Area — выбрать область: показать подложку ЭТОЙ карты (MiniMap_OpenBigMap@0xD66D40).
	UFUNCTION(BlueprintCallable, Category = "Asda|WorldMap") void SelectArea(int32 AreaIndexInContinent);
	// 1724 MaxMap_MyPosBtn «Где я?» — вернуться к своей карте (this[64] = −1; worldMapDlg_SelectCurrentMap).
	UFUNCTION(BlueprintCallable, Category = "Asda|WorldMap") void ShowMyPosition();

	// Какая карта сейчас показана в окне (эталон this[64] / g_nBigMapTargetMapId). −1 = окно не открывалось.
	UFUNCTION(BlueprintCallable, Category = "Asda|WorldMap") int32 GetDisplayedMapId() const { return DisplayMapId; }

	// Запрошенный межкартовый переход (beginMoveToTile).
	UPROPERTY(BlueprintAssignable, Category = "Asda|WorldMap") FAsdaMapTravelRequest OnMapTravelRequested;
	UFUNCTION(BlueprintCallable, Category = "Asda|WorldMap") bool IsTravelRequested() const { return bTravelActive; }
	UFUNCTION(BlueprintCallable, Category = "Asda|WorldMap") void CancelTravelRequest();

	// ══════════════════════════════════════════════════════════════════════════════════════════
	// ★★★ МЕЖКАРТОВЫЙ АВТОПУТЬ — ЕДИНСТВЕННАЯ ТОЧКА ПОРТА
	// ══════════════════════════════════════════════════════════════════════════════════════════
	// ЭТАЛОН. Заявку взводит `beginMoveToTile@0xFBE140` (`this+0 = 1`, `+20 = mapId`,
	//   `+24/+28 = tileX/tileY`, `+224 = 1`), а ИСПОЛНЯЕТ её главный тик игры
	//   (`game_main.c:1788…1895`): как только персонаж дошёл до клетки текущего шага, зовётся
	//   `pathfindGetRouteNodeCoords@0xFBE4C0` — «своя карта == целевая ⇒ КОНЕЧНАЯ клетка, иначе
	//   клетка ПОРТАЛА на следующую карту маршрута», и результат уходит в обычный
	//   `mapClick_BeginPathMove`. После перехода заявка ОСТАЁТСЯ активной и шаг повторяется уже
	//   с новой `g_nCurMapId` — то есть ОДИН клик проходит всю цепочку карт, а не один переход.
	//   Полный разбор — `Reports/quest_tracker_fix.md` §Д3.
	//
	// ПОЧЕМУ ЗДЕСЬ. В оригинале и клик по большой карте, и кнопки маршрута журнала заданий
	//   (`miniMapClick_TravelTo@0xC692D0`) взводят ОДИН И ТОТ ЖЕ объект. Его порт — поля
	//   `bTravelActive`/`TravelMapId`/`TravelTile` этой подсистемы, они уже были. Второго склада
	//   состояния и второго исполнителя в порту нет: журнал заданий зовёт `BeginMapTravel`.
	//
	// ★ РАСЧЁТ ШАГА НЕ ДУБЛИРУЕТСЯ: следующая клетка берётся у
	//   `UAsdaQuestMarkerSubsystem::NextPortalStep` — той же функции, которой экранный трекер
	//   считает расстояние «до ближайшего портала по пути» (граф из 32 рёбер `kPortalGraph`,
	//   порт `pathGraph_BuildNavMesh@0xFBBE90`).
	UFUNCTION(BlueprintCallable, Category = "Asda|WorldMap")
	bool BeginMapTravel(int32 InMapId, int32 TileX, int32 TileY);
	// Строка состояния автопути для диагностики (`Asda.Map.Travel`).
	FString DescribeMapTravel() const;

	// ── Внешние списки отметок (в оригинале — пати/клан/соулмейт/квест/рыбалка) ──
	//   Порт наполняет их из своих подсистем по мере их появления; рендер уже полный.
	UFUNCTION(BlueprintCallable, Category = "Asda|Minimap")
	void SetMarks(EAsdaMapMark Kind, const TArray<FAsdaMapMark>& Marks);
	UFUNCTION(BlueprintCallable, Category = "Asda|Minimap")
	void ClearMarks(EAsdaMapMark Kind);
	/** ★ ДИАГНОСТИКА (волна 1-Б, самотест asda.UI.SelfTestWave1B): текущий список отметок типа Kind
	 *  (nullptr — списка нет). Ровно то, что прочитает отрисовка, — без копии и пересчёта. */
	const TArray<FAsdaMapMark>* FindMarks(EAsdaMapMark Kind) const { return Marks.Find((uint8)Kind); }
	/** ★ ДИАГНОСТИКА (волна 1-Б): сколько квадов маркеров типа Kind РЕАЛЬНО видимо в пуле мини-карты
	 *  (bBig=false) или большой карты (bBig=true) после последней отрисовки: опознаются по текстуре
	 *  кисти (`MarkTex[Kind]`) и видимости. OutImages — сами квады (для живой геометрии). */
	int32 CountDrawnMarks(EAsdaMapMark Kind, bool bBig, TArray<UImage*>* OutImages = nullptr) const;
	/** ★ ДИАГНОСТИКА (волна 1-Б): канва слоя маркеров мини-карты/большой карты (для проверки «квад внутри»). */
	UCanvasPanel* GetMarkLayer(bool bBig) const;
	/** ★ ДИАГНОСТИКА (волна 1-Б): id карты, которую рисует мини-карта (эталон g_nCurMapId; −1 — ещё не было). */
	int32 GetDrawnMapId() const { return MapId; }

	// Точка автобега (busDestPos). bActive=false — маркер снят (эталон g_GroundTargetMarkerActive).
	UFUNCTION(BlueprintCallable, Category = "Asda|Minimap")
	void SetDestinationTile(int32 TileX, int32 TileY, bool bActive);
	// ★ 29.10.2026 (ключ asda.Map.DestMarkClear): поднята ли метка цели автобега (эталон `g_GroundTargetMarkerActive`).
	bool IsDestinationActive() const { return bDestActive; }
	// ★ 29.10.2026: клетка цели автобега (эталон `g_nPlayerTileX/Y` — миснейм, это клетка ЦЕЛИ); смысл — при поднятой метке.
	FIntPoint GetDestinationTile() const { return FIntPoint(DestTileX, DestTileZ); }

	// ── Обработчики контролов окна (нужны UFUNCTION для AddDynamic) ──
	UFUNCTION() void OnCloseClicked();        // 1704 MINIMAP_CLOSE
	UFUNCTION() void OnRestoreClicked();      // 1701 MINIMAPMIN_ON
	UFUNCTION() void OnBigMapCloseClicked();  // 1711 MAXMAP_CLOSE
	UFUNCTION() void OnMapClicked();          // клик по области карты мини-карты (MMap_BkBoder)
	UFUNCTION() void OnBigMapClicked();       // клик по большой карте (MaxMap_Bk1Stroke)
	UFUNCTION() void OnChannelBtnClicked();   // 17500 Change_Channel_Btn — раскрыть/свернуть панель каналов
	UFUNCTION() void OnCampWarOutClicked();   // 17520 CAMPWAR_Out_Button — «выйти из боя» (нотис 10047)
	UFUNCTION() void OnMyPosClicked();        // 1724 MaxMap_MyPosBtn «Где я?»
	UFUNCTION() void OnContinentBoxClicked(); // 1721 MaxMap_Continent — раскрыть/свернуть список
	UFUNCTION() void OnAreaBoxClicked();      // 1723 MaxMap_Area — раскрыть/свернуть список

	// ── ★ ПЕРЕКЛЮЧАТЕЛЬ КАНАЛОВ (17500 кнопка / 1717 подпись / 17501 панель / 17503+i строки) ──
	// Раскрыть/свернуть панель со списком каналов (эталон world_map.c:7519: Visible ^= 1).
	UFUNCTION(BlueprintCallable, Category = "Asda|Minimap") void SetChannelPanelOpen(bool bOn);
	// Выбрать канал (0-based) → 6084. Гейты эталона: тот же канал (нотис 98003) и т.д. — см. .cpp.
	UFUNCTION(BlueprintCallable, Category = "Asda|Minimap") void SelectChannel(int32 Channel);

	// Диагностика карты мира: показанная карта / континент / область / курсор (консоль Asda.WorldMap).
	FString DescribeWorldMap() const;

	// Диагностика: карта / режим / позиция игрока → тексел → пиксель, одной строкой (консоль Asda.Minimap).
	FString DescribeState() const;
	// Проверка формулы мир→пиксель на произвольной мировой точке (для доказательства в отчёте).
	FString DescribeProbe(float GameX, float GameZ) const;
	// ★ Диагностика КОНУСА ОБЗОРА (консоль Asda.Minimap.Pov): все три угла — орбит-камеры
	//   (эталонный g_fCameraYaw), мировой стрелы камеры и курса персонажа — плюс угол, реально
	//   применённый к квадам miniMyDir на обеих картах. Позволяет доказать вращение живьём:
	//   повернуть камеру ПКМ и вызвать команду дважды.
	FString DescribePov() const;

private:
	// ── Эталонные константы геометрии ──
	static constexpr float kWinTop        = 34.f;   // Origin.y окна 1702 в JUI
	static constexpr float kMapTop        = 36.f;   // uiMinimapBuildQuads: v11[12] = 36.0
	static constexpr float kWinCompactW   = 133.f;  // JUI Size 133×134
	static constexpr float kWinExpandW    = 260.f;  // MiniMap_SetMaxMin: Size(260,260), сдвиг −260
	static constexpr float kMapCompactPx  = 130.f;  // квад карты компакт
	static constexpr float kMapExpandPx   = 256.f;  // квад карты развёрнуто
	static constexpr float kTexSpace      = 256.f;  // «тексельное» пространство карты (NormalizeUVBy256)
	static constexpr float kBigMapPx      = 512.f;  // большая карта 512×512
	static constexpr float kBigWinW       = 690.f;  // окно 1710: JUI Size 690×519 (центрируется по экрану)
	static constexpr float kBigWinH       = 519.f;
	static constexpr float kComboRowH     = 20.f;   // строка списка = ячейка Bk_ComboBox (JuiSkin Rect "0 0 89 20")
	static constexpr int32 kMinimapZ      = 120;    // z-order окна мини-карты
	static constexpr int32 kBigMapZ       = 130;

	// Сколько текселов карты видно в окне (эталон: ±64/±128 развёрнуто, ±32/±64 компакт).
	float ViewTexels() const { return bExpanded ? (bZoom ? 128.f : 256.f) : (bZoom ? 64.f : 128.f); }
	float MapPx()      const { return bExpanded ? kMapExpandPx : kMapCompactPx; }
	float WinW()       const { return bExpanded ? kWinExpandW : kWinCompactW; }
	// Пикселей на тексел ДЛЯ МАРКЕРОВ (эталон world_map.c:2657/2887: ровно 2 при зуме, иначе 1).
	float MarkPxPerTexel() const { return bZoom ? 2.f : 1.f; }

	// ── Состояние (1:1 поля CMap) ──
	bool  bVisible       = true;    // CMap+0x248
	bool  bExpanded      = false;   // CMap+0x240
	bool  bZoom          = false;   // CMap+0x24C
	bool  bTownMode      = false;   // CMap+0x34C  (бит 0x80 тайла)
	bool  bBigMapVisible = false;   // CMap+0x244
	int32 MapId          = -1;
	int32 LastTileX      = INT32_MIN;
	int32 LastTileZ      = INT32_MIN;
	// ★ R4-b 14.11.2026 (B1/B2): кэш узлов и показанных значений подписей; кэш узлов справки кнопок.
	TWeakObjectPtr<UUserWidget> LblOwner;                    // виджет, по которому найдены узлы подписей
	TWeakObjectPtr<UTextBlock> LblX, LblY, LblName;          // MINIMAP_X / MINIMAP_Y / MINIMAP_NAME
	int32 LblTx = INT32_MIN, LblTz = INT32_MIN;              // показанные координаты тайла
	int32 LblMapId = INT32_MIN;                              // карта показанного имени
	int32 LblLang = -1;                                      // язык показанных подписей (`FAsdaLang::Current`)
	const void* LblCulture = nullptr;                        // культура показанных чисел (формат `FText::AsNumber`)
	FString LblTitle;                                        // показанное имя локации
	bool  bLblTitleSet = false;                              // имя уже записано в этот виджет
	TWeakObjectPtr<UUserWidget> TipOwner;                    // виджет, по которому найдены узлы справки
	TWeakObjectPtr<UWidget> TipNodes[5];                     // MINIMAP_CLOSE / ZOOM / MAXBUT / UPDOWN / Indicator_MiniMap
	int32 DestTileX = 0, DestTileZ = 0;
	bool  bDestActive = false;
	double LastClickTime = 0.0;     // троттл клика 250 мс (g_dwBigMapClickThrottle)
	double QuestBlinkT   = 0.0;     // мигание квест-точек 1000/200 мс (dword206/207)

	// ═══════ ★★★ ВИДЖЕТ ЗАНЯТОСТИ ГОРОДОВ `Occupation_box` (79900) — ПОД МИНИ-КАРТОЙ ══════════
	// ДАННЫЕ: пакет **6743** (`UAsdaNetSubsystem::CityOwner[3]`, разбор — в его .h).
	//   Слот 0 = Альпия (loc 603, карта 3), 1 = Силарис (600, карта 0), 2 = Пламения (707, карта 7).
	//
	// ★★★ УСТРОЙСТВО (это ЧИСТЫЙ ЭТАЛОН, не отступление — прошлая версия с покраской была ошибкой).
	//   `uniconWar_UpdateFactionStateUI@0x9C8B90` (ui_core.c:3919-3924) для каждого i = 0..2 делает
	//   РОВНО ОДНО действие с этой панелью:
	//       v3 = g_UniconWarFactionState[91*i];                       // фракция-хозяин города
	//       if (v3 == фракция игрока) SetVisible(g_pUniconWarFactionCtrl[i], 1);
	//       else                      SetVisible(g_pUniconWarFactionCtrl[i], 0);
	//   То есть ЦВЕТНАЯ буква города показывается ТОЛЬКО когда город держит ФРАКЦИЯ ИГРОКА, а во
	//   всех прочих случаях (чужая фракция / город не занят / пакета ещё не было) она скрыта, и
	//   виден только СЕРЫЙ базовый вариант, нарисованный на общей подложке `NEW_OCC_BACK`.
	//   ⇒ НИЧЕГО КРАСИТЬ НЕ НАДО: цветные варианты уже лежат в `.JUI` отдельными планами
	//   `Occupation_apen_2_0` / `Occupation_flammio_3_0` / `Occupation_Silalis_4_0`
	//   (26×26, шаг 27), а серые буквы — часть подложки 80×26.
	//
	// ПОРЯДОК НА ЭКРАНЕ задают авторские X: apen −84, flammio −57, Silalis −30 (шаг 27), то есть
	//   слева направо идут контролы 79901, 79902, 79903 — и это же порядок слотов пакета.
	//   ⚠ Суффиксы имён при этом ВРУТ: подсказка эталона (`social.c:4102-4126`) вешает на 79902
	//   нотис **600 «Силарис»**, а на 79903 — **707 «Пламения»**. Экранный порядок слева направо
	//   получается Альпия · Силарис · Пламения — ровно тот, который и нужен.
	//
	// ПОЗИЦИЯ: в `.JUI` панель прибита к правому верху с отступом (−84, 300) в базе 1024×768, что
	//   соответствует низу авторской мини-карты. В порту мини-карта своя, поэтому панель ПРИЖИМАЕТСЯ
	//   к её фактическому низу (`LayoutOccupationBox`), а по горизонтали остаётся выровненной по
	//   правому краю мини-карты. ⚠ Двигаем ТОЛЬКО слот канваса внутри окна панели, в его собственной
	//   системе координат — вторично в единицах вьюпорта окно не сдвигается.
	UPROPERTY(Transient) TObjectPtr<UUserWidget> OccupationWidget;         // WBP_Occupation_box (79900)
	// Цветные планы городов по СЛОТАМ пакета: [0] Альпия, [1] Силарис, [2] Пламения.
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>> OccupationIcons;
	// Все узлы панели — для сдвига её под мини-карту одним общим смещением.
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>> OccupationNodes;
	// Авторские позиции узлов панели (снимаются один раз, до всякого сдвига).
	TArray<FVector2D> OccupationBasePos;
	bool bOccupationBaseCached = false;

	// Показать/скрыть цветные буквы по текущему `CityOwner[]` (по приходу 6743 и при создании).
	void RefreshOccupationBox();
	// Прижать панель к низу мини-карты (зовётся из `ApplyLayout` и при создании).
	void LayoutOccupationBox();

	// ── Виджеты ──
	UPROPERTY(Transient) TObjectPtr<UUserWidget>   MinimapWidget;   // WBP_MINIMAP_WIN    (1702)
	UPROPERTY(Transient) TObjectPtr<UUserWidget>   MinIconWidget;   // WBP_MINIMAPMIN_WIN (1700)
	UPROPERTY(Transient) TObjectPtr<UUserWidget>   BigMapWidget;    // WBP_MAXMAP_WIN     (1710)
	UPROPERTY(Transient) TObjectPtr<UCanvasPanel>  MapView;         // MMap_View   — клип-окно карты
	UPROPERTY(Transient) TObjectPtr<UImage>        MapImage;        // MMap_MapImage — квад карты
	UPROPERTY(Transient) TObjectPtr<UCanvasPanel>  MarkLayer;       // MMap_Markers — слой маркеров
	UPROPERTY(Transient) TObjectPtr<UCanvasPanel>  BigMapView;
	UPROPERTY(Transient) TObjectPtr<UImage>        BigMapImage;
	UPROPERTY(Transient) TObjectPtr<UCanvasPanel>  BigMarkLayer;
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>> MarkPool;       // пул квадов маркеров мини-карты
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>> BigMarkPool;

	// Текстуры маркеров (CMiniMap_Init) и картинки карт.
	UPROPERTY(Transient) TMap<uint8, TObjectPtr<UTexture2D>> MarkTex;
	UPROPERTY(Transient) TObjectPtr<UTexture2D> MiniMapTex;   // <map>.TEX
	UPROPERTY(Transient) TObjectPtr<UTexture2D> BigMapTex;    // <map>_big.TEX / <map>_town_big.TEX

	// Внешние списки отметок по типу.
	TMap<uint8, TArray<FAsdaMapMark>> Marks;

	// ── Внутреннее ──
	bool  EnsureWidgets();
	void  LoadMapTextures();
	void  ApplyLayout();                 // разворот/сворот: рамки, кнопки, окно карты (MiniMap_SetMaxMin)
	void  UpdateMap();                   // UV окна карты + маркеры (MiniMap_RenderBigMap)
	void  UpdateBigMap();                // CBigMap_RenderAndHandleClick
	void  UpdateLabels();                // MINIMAP_NAME / MINIMAP_X / MINIMAP_Y
	// ★ R4-b 14.11.2026 (B1): имя локации подписи 1703 (прежде — внутри `UpdateLabels`).
	FString MapTitleText() const;
	// ★ R4-b 14.11.2026: подписи 1702/1703 по кэшу — узлы ищутся раз на виджет, `SetText` только при смене значения.
	void  UpdateLabelsCached(int32 Tx, int32 Tz);
	void  UpdateTownFlag();              // бит 0x80 тайла → sub_93539F
	void  BindButtons();
	void  HandleMapClick(const FVector2D& LocalPx, bool bBig);
	// ★ 09.11.2026 GM-телепорт: Ctrl+ЛКМ по карте → `UAsdaGmOriginalSubsystem::TryMapClickTeleport`; true — клик забран.
	bool  TryGmMapTeleport(AAsdaPlayerCharacter* P, int32 ClickMapId, const FVector2D& Tile, const UAsdaWalkGrid* G, bool bBig);

	// Хелперы геометрии.
	const UAsdaWalkGrid* GetGrid(AAsdaPlayerCharacter*& OutPlayer) const;
	float WorldToTexelScale(const UAsdaWalkGrid* Grid) const;   // 256/(Width*100)
	FVector2D ScrollTexels(const FVector2D& CenterTexel) const; // clamp(center−View/2, 0, 256−View)
	// Кроп городской большой карты (hudIconSetUvBySlot): true = у карты есть городской вариант.
	static bool TownCrop(int32 InMapId, FVector2D& OutOrigin, float& OutScale);

	UImage* AcquireMark(bool bBig, int32 Index);
	void    HideMarksFrom(bool bBig, int32 Index);
	void    DrawMark(bool bBig, int32& Cursor, EAsdaMapMark Kind, const FVector2D& LocalPx,
	                 float SizePx, float AngleDeg);

	// Кэш смещений контролов окна для разворота/сворота (заполняется при первом ApplyLayout).
	bool bLayoutCached = false;
	TMap<FName, FVector2D> BasePos;      // имя виджета → offsets(L,T) в компактном состоянии

	// ── Каналы ──
	bool  bChannelPanelOpen = false;      // видимость Change_Channel_Win (17501)
	int32 HoverChannel      = -1;         // строка под курсором (жёлтая рамка 17502)
	int32 ShownChannel      = -1;         // на что уже отрисованы подписи (чтобы не дёргать каждый кадр)
	int32 ShownChannelCount = -1;
	int32 ShownShortWord      = -1;        // значение CVar Asda.Minimap.ShortChannelWord на момент отрисовки
	FString ShownSvrChText;                // последняя записанная подпись 1717 — только для лога перерисовки
	UPROPERTY(Transient) TArray<TObjectPtr<class UAsdaButtonRelay>> ChannelRelays;   // клик/ховер строк

	// ── 17520 CAMPWAR_Out_Button: видимость по campwar-флагу записи карты (+69), см. .cpp ──
	bool  bCampWarShown      = false;
	int32 ShownCampWarMapId  = -2;
	void  UpdateCampWarButton();

	// ── ★ КАРТА МИРА: состояние окна 1710 (поля CMiniMapWnd в эталоне) ──
	int32 DisplayMapId   = -1;            // this[64] — какая карта показана (g_nBigMapTargetMapId)
	bool  bDisplayTown   = false;         // городской вариант ПОКАЗАННОЙ карты (2-й аргумент MiniMap_OpenBigMap)
	int32 SelContinent   = 0;             // this[65] — выбранный континент (комбобокс 1721)
	int32 SelArea        = -1;            // this[66] — выбранная строка области (комбобокс 1723)
	FIntPoint HoverTile  = FIntPoint::ZeroValue;   // dword_351F9E8/EC — тайл под курсором (подписи 1733/1734)
	bool  bHoverInMap    = false;         // курсор внутри поля карты (g_bBigMapCursorHover)
	bool  bCursorOverridden = false;      // мы подменили курсор (18 move / 20 nomove) — снять при уходе
	bool  bMiniCursorOverridden = false;  // мини-карта подменила курсор (18 move) — снять при уходе (g_bMapCursorHover1/2)
	int32 MapClickCount = 0;              // принятые клики по полю мини-карты (самотест)
	int32 GmTeleportClickCount = 0;       // ★ 09.11.2026: клики, забранные GM-телепортом (самотест)
	bool  bTravelActive  = false;         // g_bMapTransferActive
	int32 TravelMapId    = -1;            // g_nMapTransferTargetMapId
	FIntPoint TravelTile = FIntPoint::ZeroValue;   // g_nMapTransferTileX/Y
	// ── Исполнитель автопути (порт блока game_main.c:1788…1895) ──
	void TickMapTravel();
	// Клетка ТЕКУЩЕГО шага, к которой персонаж идёт сейчас (эталон g_nPlayerTileX/Y, поля +120/+124).
	FIntPoint TravelStepTile = FIntPoint(-1, -1);
	// Карта, на которой шаг был посчитан: сменилась ⇒ прошли портал ⇒ считаем следующий.
	int32 TravelStepMapId    = -1;
	// Защита от зависания — эталонные `g_AutoWalkRetryCount` / `g_AutoWalkFailCount`: персонаж
	//   стоит там же, а шаг не меняется. После kTravelStuckLimit проб заявка снимается.
	FIntPoint TravelLastTile = FIntPoint(-1, -1);
	int32 TravelStuckCount   = 0;
	double TravelNextTryTime = 0.0;       // троттл повторной выдачи приказа (эталон 500 мс)
	static constexpr int32  kTravelStuckLimit = 5;      // g_AutoWalkFailCount >= 5 ⇒ снять
	static constexpr double kTravelRetrySec   = 0.5;    // 0x1F4 мс
	bool  bWorldMapBuilt = false;         // подписи/списки окна уже разложены
	bool  bWindowPlaced  = false;         // окно уже отцентрировано (MiniMap_BuildUI делает это ОДИН раз)
	bool  bContListOpen  = false;         // раскрыт список континентов (1721)
	bool  bAreaListOpen  = false;         // раскрыт список областей (1723)
	// ★ Состояние собственного перетаскивания (bDragging/DragPrev/bPrevLmbDown) УДАЛЕНО 2026-08-06:
	//   окно 1710 таскает общая система по данным .JUI (UAsdaWindowSubsystem::TickDrag).

	// Список областей (таблица эталона; редактируется). Заполняется BuildDefaultAreas при Initialize.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asda|WorldMap", meta = (AllowPrivateAccess = "true"))
	TArray<FAsdaWorldMapArea> WorldMapAreas;

	void BuildDefaultAreas();             // таблица uiInitSlotIndexMaps + подписи helpWndFillNoticeList
	void BuildWorldMapUI();               // подписи 9600/9610/9698/9699/15203-15206 + биндинг кнопок/комбо
	void FillAreaCombo(int32 Continent);  // helpWndFillNoticeList: перезаполнить 1723 под континент
	void SetComboCaption(const TCHAR* TextWidget, const FString& S);   // cControlSetCaption у 1721/1723
	void ReturnFocusToViewport();         // после комбобокса вернуть клавиатуру миру (иначе не ходит WASD)
	// ★ Раскрывающийся список штатного JUI-комбобокса (скин DarkGrey_List): открыть/закрыть, заполнить.
	void SetComboListOpen(bool bContinent, bool bOpen);
	void RefreshComboList(bool bContinent);
	void SetComboHover(bool bContinent, int32 Row);
	// MiniMap_BuildUI@0xD63220: окно 1710 ставится в ЦЕНТР экрана ОДИН раз при создании.
	void CenterBigMapWindow();
	// Реле кликов/наведения строк обоих списков (клик-кнопки из asda_fix_worldmap_ui.py).
	UPROPERTY(Transient) TArray<TObjectPtr<class UAsdaButtonRelay>> ComboRelays;
	void OpenBigMapFor(int32 InMapId, bool bTown);   // MiniMap_OpenBigMap@0xD66D40
	void SelectCurrentMapInCombos();      // worldMapDlg_SelectCurrentMap@0xD66A20
	void UpdateBigMapHover();             // CBigMap_RenderAndHandleClick: тайл под курсором + курсор 18/20
	void UpdateMiniMapHoverCursor();      // MiniMap_Render: курсор 18 над полем мини-карты (контрол 1714 верхний под мышью)
	void ReleaseMiniHoverCursor();        // вернуть курсор, подменённый мини-картой (уход, загрузка, сброс)
	// ★ 30.10.2026 [ОРИГИНАЛ] справка кнопок мини-карты: `MiniMap_BigMapWindowProc@0xD65210` msg 8 →
	//   `uiBuildHelpTooltipByMsgId@0xD65ED0` (1704 → 14, 1706 → 13/25, 1707 → 15, 1708 → 12/26, 1719 → 27), 0xA → гашение.
	void TickHelpTips();
	FAsdaHelpTipHover HelpTip;            // кнопка под курсором и показанная справка (владелец `EAsdaTipOwner::Minimap`)
	// ★ [ПОРТ] 31.10.2026 подсказка значка цели задания под курсором на карте мира (тот же владелец, своя пара ключей).
	FAsdaHelpTipHover TargetTip;
	// Значок цели под точкой поля 512×512 (локальный пиксель окна); −1 — нет. Порог — полразмера значка.
	int32 FindQuestTargetAt(const FVector2D& LocalPx) const;
	// Показать/снять подсказку значка цели по курсору (зовёт `UpdateBigMapHover`); bInMap = курсор над полем карты.
	void UpdateQuestTargetTip(bool bInMap, const FVector2D& LocalPx);
	void ShiftBigMapWindow(const FVector2D& Delta);
	// Пикселей на тайл в поле 512×512: у своей карты 512/Width, у чужой — 1 (эталон mapTexSize/mapDrawW).
	float BigMapTileToPx() const;
	// Загрузить подложку по mapId (<dir>_big / <dir>_town_big из /Game/AsdaAssets/Textures/MapImage).
	UTexture2D* LoadBigMapImage(int32 InMapId, bool bTown) const;

	// ★★★ СБРОС КЭШЕЙ, ПРИВЯЗАННЫХ К КОНКРЕТНОМУ ВИДЖЕТУ ОКНА 1702 (см. разбор в .cpp).
	//   Зовётся КАЖДЫЙ раз, когда `EnsureWidgets` завела НОВЫЙ `MinimapWidget`: без этого поля
	//   `Shown*` подсистемы переживают смерть старого дерева и глушат перерисовку нового.
	void ResetMinimapWidgetCaches();
	// То же для окна 1710 (карта мира): свой набор кэшей и свой пул маркеров.
	void ResetBigMapWidgetCaches();

	void BindChannelUI();                 // связать 17500/17501/17503+i с обработчиками
	void UpdateChannelUI();               // подписи 1717 и строк + цвет текущего канала + высота панели
	void SetHoverChannel(int32 Channel);  // жёлтая рамка Change_Channel_Sel над строкой
	// Число строк каналов и текущий канал — из сети (эталон g_nChannelCount / g_nCurrentChannel).
	int32 NetChannelCount() const;
	int32 NetCurrentChannel() const;
};
