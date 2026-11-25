// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ UAsdaStallSignSubsystem — ВЫВЕСКИ И ПРИЛАВКИ ЛИЧНЫХ ЛАВОК (`CShopMgr`). Волна Л3 торговли (30.09.2026,
//   `Reports/trade/wave_L3.md`).
//
// ЧТО ЭТО В ОРИГИНАЛЕ (сверено с эталоном `RE/etalon/*.c`, сервером `RE/ida/server_work/GameServer.c`):
//   [ПРОТОКОЛ] 6304 (56 б): [0] u8 открыта 1 / закрыта 0, [1] u8 премиум (== 1), @2 i32 charId, @6 char[50] название. Сервер шлёт
//     его сектору при открытии (GameServer.c:100286) и закрытии (:100428) лавки и адресно в снапшоте входящему в обзор, если лавка
//     открыта (`Send_MoveSectorInClippingUserInfo` :11942/:12702). Отдельного поля в 4005 нет (B §4).
//   [ОРИГИНАЛ] Приём — `charMgrAddNameStatusEntry@0xDDA2E0` (МИСНЕЙМ): запись в очередь `CShopDlg+0x280`. Разбор — раз в > 1000 мс
//     `charManager_UpdateNameStatusList@0xDDA3C0` (game_main.c:1505): персонаж `CharManager_FindByCharId` найден ⇒ `+225921` открыта,
//     `+225922` премиум (`[1] == 1`), `+225923` название; запись удаляется; не найден — ждёт (К-9: не чистится никогда).
//     ★ Волна Л4/Л5 (30.09.2026): [ИСПРАВЛЕНИЕ] К-9 ключом `asda.Trade.StallQueueFixK9` — маркер 4005 «появление charId» (сетевой
//     слой, в порядке потока) снимает прежние записи персонажа; новая 6304 заменяет прежнюю того же charId (очередь ≤ числа charId).
//   [ОРИГИНАЛ] Прилавок `shop.jbm` — `CShopMgr_OpenNpcShop@0xDE0390` (МИСНЕЙМ): объект 0x274 б на userNum (второй раз — не
//     создаётся), модель рисуется матрицей персонажа каждый кадр (`charRenderList_ApplyTransformColors@0xDE0D40`); снимает
//     `npcShopMgr_CloseShopByUserIdx@0xDE0760`. Кто зовёт: свой — раз в 500 мс (`LocalPlayer_UpdateRender`, entity_char.c:7772:
//     признак == 1 ⇒ открыть, иначе снять); чужие — каждый кадр (`CharManager_UpdateAll`, entity_char.c:8473: признак ≠ 1 ИЛИ
//     GM-скрыт ⇒ снять, иначе открыть); уход персонажа из мира (`CharManager_RemoveNode`, entity_char.c:7079) — снять.
//     После открытия/снятия — перераскладка плашки имени (`charNamePlate_LayoutControls`: бейдж −34, ещё −5 без титула и клана).
//   [ОРИГИНАЛ] Вывеска — `Chat_RenderHeadBubble@0xDE10E0` (МИСНЕЙМ, chat.c:6345), каждый кадр по списку прилавков: на первом кадре
//     клон шаблона 23450 (обычная) или 23470 (премиум, по признаку персонажа В ЭТОТ момент), текст = название (`uiSetTruncatedMarkupText`:
//     ≤ 145 px целиком, иначе обрезка с «...» по 125 px), текст сдвинут на 1 px вниз, белый (−1), обводка (0x800000), выравнивание 5.
//     Раз в 4 с — повтор обрезки и «свой и состояние 6 (смерть) ⇒ 6305». Свой: раз в 300 с — метка времени, и если премиум
//     (`byte_2C2F759`) — реклама 5086 «название [X:x/100.Y:z/100]» (глоб-чат, без секрета). Видимость — персонаж во фрустуме и
//     рисуется; точка: верх AABB + 10, у сидящего (состояние 8) — центр AABB (`soundPos`); + `heightOffset` транспорта (Table2 15/2);
//     экран: X − ширина/2, Y − (сдвиг + 62), сдвиг: +16 за титул, +16 за клан, +10 если ни того, ни другого.
//   [ОРИГИНАЛ] Клик — `ui_DispatchCommandEvent` событие 11 (ЛКМ) → `tradeRequestTargetByServerIdx@0xDE0F80` (МИСНЕЙМ): клон вывески
//     под курсором → userNum её прилавка → `UAsdaSelfStoreSubsystem::RequestVisit` (гейты + 6307).
//
// ЧТО В ПОРТЕ:
//   [ПОРТ] Поля `CCharacter` (+225921/+225922/+225923) → записи `Flags` по charId с актором-носителем (у чужого смена актора = новый
//     `CCharacter`, признаки обнуляются, как у эталона). Очередь `CShopDlg+0x280` — здесь же (одна система «вывески»).
//   [ПОРТ] Модель `shop.jbm` — штатный конвейер моделей (`Tools/asda_build_models.py` → `RawAssets/Models/Char/shop.glb` →
//     `Tools/asda_import_models.py` → `/Game/AsdaAssets/Models/Char/shop/StaticMeshes/shop`, материал `M_AsdaObjectLit` + текстура
//     `Characters/Texture/shop`); крепится к `MeshRoot` персонажа в нуле — та же матрица, что у тела (масштаб 0.01, зеркало Y, курс).
//   [ПОРТ] Клон вывески — экземпляр импортированного WBP (`WBP_StreetStall_DisplayOnChar` / `…1`) во вьюпорте под игровым UI
//     (z −1, как облачко чата); клик — покадровый опрос мыши по живой геометрии рамки (`FAsdaMouseFrame`), мировой клик гасит зона
//     интерфейса (`RegisterUiZone`) — у эталона клик по контролу UI в мир не проходит.
//   [ПОРТ] Своя вывеска ставится по той же формуле, что у чужих (эталон — от своей плашки `g_pChatBubbleTarget`, −(сдвиг + 12)):
//     своя плашка в порту рисуется как у всех (закон `self-nameplate-same-as-others`).
//   [ПОРТ] Сброс сессии (смена карты / разрыв / выход) — прилавки и вывески сняты, признаки и очередь очищены, пакетов нет.
//
// ЗАКОН no-reentrant-net-callbacks: колбэк сети только копит; разбор, сцена и отправки — из Tick.
#pragma once

#include "CoreMinimal.h"
#include "AsdaTickGate.h"   // ★ R4-b (14.11.2026): шаблон класса (CDO) не тикает
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "AsdaSessionReset.h"            // IAsdaSessionResettable
#include "UI/AsdaUiInputTypes.h"   // ★ R1: FAsdaMouseFrame / FAsdaDragArm / FAsdaDragGhost … — поля ввода (без всего AsdaUiHelpers.h)
#include "AsdaNetQueue.h"          // TAsdaNetQueue — очередь «колбэк копит, Tick применяет»
#include "World/AsdaOverheadOcclusion.h" // FAsdaOccludeState — вывеска прячется за препятствиями
#include "AsdaStallSignSubsystem.generated.h"

class UUserWidget;
class UWidget;
class UTextBlock;
class UStaticMesh;
class UStaticMeshComponent;
class AActor;

// Один прилавок `CShopMgr` (объект 0x274 б эталона): модель + вывеска над персонажем.
USTRUCT()
struct FAsdaStallObj
{
	GENERATED_BODY()
	int32 UserNum = -1;                                            // +0x1CC (+460) userNum продавца
	TWeakObjectPtr<AActor> Owner;                                  // [ПОРТ] актор-носитель (смена актора = персонаж ушёл)
	UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> Mesh = nullptr;   // модель `shop.jbm`
	UPROPERTY(Transient) TObjectPtr<UUserWidget> Sign = nullptr;            // +0x1D0 (+464) клон вывески 23450/23470 (nullptr — ещё нет)
	UPROPERTY(Transient) TObjectPtr<UTextBlock>  Text = nullptr;            // +0x1E4 (+484) клон текста 23455/23475
	UPROPERTY(Transient) TObjectPtr<UWidget>     Frame0 = nullptr;          // левый-верхний план рамки (геометрия клика)
	UPROPERTY(Transient) TObjectPtr<UWidget>     Frame8 = nullptr;          // правый-нижний план рамки
	FString Title;                                                 // +0x1E8 (+488) название, снятое при создании клона
	bool bPremiumSign = false;                                     // клон 23470 (премиум), иначе 23450
	double T4 = -1.0e9;                                            // +0x268 (+616) метка раз-в-4-с (0 у эталона ⇒ первый кадр сразу)
	double TAd = -1.0e9;                                           // +0x270 (+624) метка рекламы 300 с (0 у эталона ⇒ сразу)
	FVector2D Origin = FVector2D::ZeroVector;                      // левый-верхний угол рамки в канвасе WBP
	FVector2D Size = FVector2D(190.f, 25.f);                       // размер рамки (`vtable+24` окна: 190×25 / 192×27)
	bool bShown = false;                                           // вывеска видима (фрустум + персонаж рисуется + не перекрыта)
	FName Zone;                                                    // зона интерфейса (гасит мировой клик)
	// ★★★ [ОРИГИНАЛ] 29.10.2026 (баг игроков 3): перекрытие вывески геометрией мира — у оригинала планы вывески несут глубину её
	//   точки (chat.c:6586 `matrixProjectZ` → поле 527) и проходят z-тест. Разбор и ключ — `World/AsdaOverheadOcclusion.h`.
	FAsdaOccludeState OccState;
	float OccOpacity = -1.f;                                       // последняя поставленная непрозрачность (−1 — не ставилась)
};

UCLASS()
class ASDAREBORN_API UAsdaStallSignSubsystem : public UGameInstanceSubsystem, public FTickableGameObject,
	public IAsdaSessionResettable
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	// Сброс сессии: прилавки и вывески сняты БЕЗ пакетов, признаки персонажей и очередь 6304 очищены. [ПОРТ] эталон при разрыве
	//   закрывает клиент, при смене карты свой признак не трогает — порт чистит (сервер при портале лавку закрывает сам).
	virtual void ResetSessionState(EAsdaSessionResetReason Reason) override;

	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UAsdaStallSignSubsystem, STATGROUP_Tickables); }
	// ★ R4-b (14.11.2026): шаблон класса (CDO) не тикает — раньше `Tick` звался дважды за кадр, у рабочего объекта и у шаблона
	//   (ключ `asda.Perf.NoCdoTick`, разбор — `AsdaTickGate.h`). Рабочий объект тикает как раньше.
	virtual bool IsTickable() const override { return AsdaTickGate::NotCdo(this); }
	virtual bool IsTickableInEditor() const override { return false; }
	virtual bool IsTickableWhenPaused() const override { return false; }

	static constexpr float kQueuePeriodS   = 1.0f;     // `timeGetTime() − g_dwLastUpdateTime_2C2F988 > 0x3E8`
	static constexpr float kSelfPeriodS    = 0.5f;     // `timeGetTime() − g_dwLastShopBuffUpdateTime > 0x1F4`
	static constexpr float kSignPeriodS    = 4.0f;     // `timeGetTime() − entry+616 > 0xFA0`
	static constexpr float kAdvertPeriodS  = 300.0f;   // `timeGetTime() − entry+624 > 0x493E0`
	static constexpr int32 kSignZ          = -1;       // [ПОРТ] под игровым UI (как облачко чата и плашки имён)

	// ═══ ПРОХОДЫ (тело Tick; публичные — их же зовут самотесты) ═══
	void PumpPending();                          // очередь колбэка сети → очередь вывесок (`charMgrAddNameStatusEntry`)
	void UpdateNameStatusList(bool bForce);      // `charManager_UpdateNameStatusList` (раз в > 1 с; bForce — без ожидания)
	void UpdateStalls(bool bForceSelf);          // открыть/снять прилавки (свой — раз в 0,5 с; bForceSelf — без ожидания)
	void RenderSigns();                          // `Chat_RenderHeadBubble` — вывески, смерть → 6305, реклама 5086
	void TickClick();                            // событие 11 по клону вывески → `tradeRequestTargetByServerIdx`

	// ═══ СОСТОЯНИЕ (гейты, самотест) ═══
	// Признаки персонажа по charId (`+225921/+225922/+225923`). false — записи нет (персонаж «не в лавке»).
	bool GetCharFlags(int32 CharId, bool& bOutOpen, bool& bOutPremium, FString& OutTitle) const;
	int32 GetQueueNum() const { return Queue.Num(); }     // записей 6304, ждущих персонажа
	int32 GetStallCount() const { return Stalls.Num(); }  // прилавков в сцене
	bool HasStall(int32 UserNum) const { return FindStall(UserNum) != nullptr; }
	UStaticMeshComponent* DebugStallMesh(int32 UserNum) const;   // модель прилавка продавца
	UUserWidget* DebugSignWidget(int32 UserNum) const;           // клон вывески (nullptr — ещё нет)
	FString DebugSignText(int32 UserNum) const;                  // текст вывески
	bool DebugSignPremium(int32 UserNum) const;                  // клон 23470
	bool DebugSignShown(int32 UserNum) const;                    // вывеска видима
	// Прямоугольник рамки вывески в абсолютных координатах Slate (как `FAsdaMouseFrame::Cursor`); false — нет/не видна.
	bool SignAbsRect(int32 UserNum, FVector2D& OutMin, FVector2D& OutMax) const;
	// Позиция левого-верхнего угла рамки во вьюпорте (единицы Slate) и экранная точка привязки — последний расчёт.
	bool DebugSignPlacement(int32 UserNum, FVector2D& OutTopLeft, FVector2D& OutAnchor) const;
	// ★ 30.10.2026 (самотест `asda.Overhead.SelfTestLag`): мировая точка привязки вывески последнего расчёта — сверка нарисованной
	//   рамки с проекцией этой точки камерой того же кадра. false — расчёта не было.
	bool DebugSignWorldPoint(int32 UserNum, FVector& OutPt) const;
	void DebugExpireTimers();                    // все метки (очередь, свой, 4 с, 300 с) — в прошлое: следующий проход сработает
	FString DumpState() const;
	const TArray<FString>& GetJournal() const { return Journal; }
	void ClearJournal() { Journal.Reset(); }

private:
	// Запись очереди `CShopDlg+0x280` (56 б пакета 6304).
	struct FStatusEntry
	{
		uint8 Open = 0;        // [0]
		uint8 Premium = 0;     // [1]
		int32 CharId = 0;      // @2
		FString Title;         // @6 char[50]
	};
	// Поля персонажа `CCharacter[1].gap_A42[14897..14899]`.
	struct FCharFlags
	{
		uint8 Open = 0;                    // +225921 (1 — в лавке)
		bool bPremium = false;             // +225922 (`[1] == 1`)
		FString Title;                     // +225923 название
		TWeakObjectPtr<AActor> Actor;      // [ПОРТ] носитель (у своего не проверяется — объект локального игрока один)
		bool bSelf = false;                // признаки своего персонажа
	};

	void OnPacket(int32 Op, const TArray<uint8>& Body);     // колбэк сети: только копит 6304
	FAsdaStallObj* FindStall(int32 UserNum);
	const FAsdaStallObj* FindStall(int32 UserNum) const;
	const FCharFlags* FlagsOfActor(const AActor* A, int32 UserNum) const;   // признаки носителя (свой / чужой)
	// ★ R4-b 14.11.2026 (B4): то же тело с подсистемами, взятыми вызывающим (раз на проход, а не на игрока).
	const FCharFlags* FlagsOfActorWith(const class UAsdaNetSubsystem* Net, const class UAsdaWorldSubsystem* WS, const AActor* A, int32 UserNum) const;
	void OpenStall(int32 UserNum, AActor* Owner);            // `CShopMgr_OpenNpcShop`
	void CloseStall(int32 UserNum, const TCHAR* Why);        // `npcShopMgr_CloseShopByUserIdx`
	void CloseAll(const TCHAR* Why);                         // снять все (сброс)
	void RelayoutPlate(AActor* A, int32 UserNum);            // `charNamePlate_LayoutControls` / `uiItemIconLayoutAndDraw`
	bool CreateSign(FAsdaStallObj& S, bool bPremium);        // клон 23450/23470 (`CControl_InstantiateChild` + настройка текста)
	void ApplyTruncated(FAsdaStallObj& S);                   // `uiSetTruncatedMarkupText`
	// ★ [ПОРТ] 08.10.2026 (баг владельца п.3): строка названия — по центру рамки по вертикали по ЖИВОЙ высоте строки (ключ
	//   `asda.Trade.SignTextCenter`). Зовётся после каждой записи текста (`ApplyTruncated`).
	void CenterSignText(FAsdaStallObj& S);
	void PlaceSign(FAsdaStallObj& S, AActor* Speaker);       // видимость + позиция (хвост `Chat_RenderHeadBubble`)
	void SendAdvert(AActor* Self);                           // реклама 5086 премиум-лавки
	void JournalAdd(const FString& Line);                    // журнал самотеста + лог

	UPROPERTY(Transient) TArray<FAsdaStallObj> Stalls;       // `CShopMgr::npcShopList` (+0x5D8)
	UPROPERTY(Transient) TObjectPtr<UStaticMesh> ShopMesh = nullptr;   // `shop.jbm` (кэш загрузки)
	TArray<FStatusEntry> Queue;                              // `CShopDlg+0x280`
	TMap<int32, FCharFlags> Flags;                           // charId → признаки персонажа
	struct FPending { int32 Op = 0; TArray<uint8> Body; };
	TAsdaNetQueue<FPending> PendingPackets;                         // очередь колбэка сети
	FDelegateHandle HNet;                                    // подписка на `UAsdaNetSubsystem::OnSelfStorePacket`
	double LastQueueAt = -1.0e9;                             // `g_dwLastUpdateTime_2C2F988`
	double LastSelfAt = -1.0e9;                              // `g_dwLastShopBuffUpdateTime`
	TMap<int32, FVector2D> LastTopLeft;                      // последний расчёт позиции вывески (диагностика)
	TMap<int32, FVector2D> LastAnchor;                       // последняя экранная точка привязки (диагностика)
	TMap<int32, FVector> LastWorldPt;                        // ★ 30.10.2026: последняя МИРОВАЯ точка привязки (диагностика)
	TSet<int32> OpenFailLogged;                              // userNum, по которым уже сказали «Shop open fail»
	bool bSignClassFailLogged = false;                       // «шаблон вывески не загрузился» — один раз
	bool bZoneProbe = false;                                 // идёт опрос «чужой UI над курсором» — свои зоны молчат
	FAsdaMouseFrame Mouse;                                   // опрос мыши (клик по вывеске)
	TArray<FString> Journal;                                 // журнал самотеста
	friend struct FAsdaSelfStoreL3SelfTest;                  // самотест волны Л3
};
