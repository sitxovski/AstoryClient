// Copyright (c) 2026 @sitxovski (Discord)
// UAsdaGmOriginalSubsystem — вкладка «Команды оригинала» GM-меню (19.10.2026, просьба владельца). [ОРИГИНАЛ]+[ПОРТ]
//
// ЧТО ЭТО. В оригинальном клиенте есть отправители ВСЕХ штатных операторских команд (опкоды 6012…6075), но GM-окна,
//   которое их зовёт, в розничной сборке нет — вызовов ноль. Здесь эти команды получают пункты меню: каждый пункт
//   собирает ввод штатными окнами игры (диалог ввода 1580 `ShowInputDialog`, нумпад 1410 `ShowNumPad`, подтверждение
//   1520 `ShowGameConfirm`) и шлёт тело байт в байт как отправитель оригинала (`Private/AsdaNetGmOriginal.cpp`).
//   Ответы сервера (до этого порт их не разбирал) показываются, как у эталона: строки в чат, нотисы 2113/2116/2118,
//   запись предмета получателю 6044, закрытие связи по кику 6019. Группа 4 (`group4_client_handlers.md`): у цели 6025 (2117,
//   звук и появление, закрытие окон), 6058 (сброс откатов навыков), у охвата 6145 (обратный отсчёт: плашки 108143, цифра 0x16,
//   действие по концу) и 6147 (блок карты: в оригинале только стоп в зоне поля боя и плашки 108141/108142, запрета нет).
//   Разбор каждой команды — `Reports/gm_original_commands/group1..4_spec.md`, план вкладки — `00_PLAN.md`.
//
// ПРАВА. Решает СЕРВЕР: гейт оператора (UserIndex 0…5000) и, под нашим P47, GM-флаг персонажа. Отказ у сервера молчаливый —
//   поэтому после отправки команды без ответа в системный чат идёт строка «отправлено (сервер ответа не присылает)».
//   Пункты видны только в GM-меню, а оно открывается только после подтверждения прав сервером (`UAsdaAdminSubsystem`).
//
// ЦЕЛЬ. Команды «по имени» (Summon/Meet/Kick) ищут игрока через RelayServer на любом сервере. Команды «выбранной цели»
//   (Kill/Move/ItemGive/CharValue) берут выбранного персонажа (`AAsdaPlayerController::TargetActor` → userNum + UserIndex):
//   сервер ищет его только на СВОЁМ процессе GameServer. Монстр — его номер сущности `AAsdaMobActor::NetRenderId`.
//
// ПРИЁМ ОТВЕТОВ. `UAsdaNetSubsystem::OnGmOriginalPacket` зовётся из разбора входящих — тела копятся в очередь и
//   разбираются в своём тике (закон no-reentrant-net-callbacks: кик закрывает соединение, это нельзя делать из колбэка).
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Containers/Ticker.h"
#include "AsdaSessionReset.h"   // IAsdaSessionResettable — отсчёт 6145 и блок 6147 живут до конца сессии
#include "AsdaGmOriginalSubsystem.generated.h"

class APlayerController;

// ★ 09.11.2026 Итог Ctrl+ЛКМ GM-телепорта (`UAsdaGmOriginalSubsystem::TryMapClickTeleport`).
enum class EAsdaGmMapTp : uint8
{
	NotGm,          // не GM_ или ключ выключен — клик не перехвачен
	Sent,           // 6010 ушёл (своя карта)
	ProbePending,   // ждём ответа 6037 на пробу прав, затем 6010
	NoRights,       // права не подтверждены и пробу слать нельзя — ничего не ушло
	OtherMap,       // чужая карта при ключе asda.Gm.MapTeleportOtherMap 0 — ничего не ушло
	SentOtherMap,   // 6010 с картой ушёл (чужая карта, серверный P123)
	OutOfMap,       // клетка вне 0<X<w, 0<Z<h — ничего не ушло
};

UCLASS()
class ASDAREBORN_API UAsdaGmOriginalSubsystem : public UGameInstanceSubsystem, public IAsdaSessionResettable
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	// Полный сброс сессии гасит отсчёт 6145 и состояние блока 6147; смена карты — нет (эталон: `Init` только при входе в мир).
	virtual void ResetSessionState(EAsdaSessionResetReason Reason) override;

	// ═══ ★ 09.11.2026 [ПОРТ, ПРОСЬБА ВЛАДЕЛЬЦА] GM-ТЕЛЕПОРТ CTRL+ЛКМ ПО МИНИ-КАРТЕ И КАРТЕ МИРА (пачка 20) ═══════════════════
	//   У оригинала такого клика нет; это надстройка над ШТАТНОЙ операторской командой 6010 `CharJumpCommandPosition@0x54EF50`
	//   (i16 X, i16 Z клетки СВОЕЙ карты; сервер: гейт `sub_52ECF0` + P47 GM-флаг, `IsCheckPosInMaxPos` 0<X<w, 0<Z<h,
	//   ближайшая проходимая клетка спиралью `sub_41EC30`, ответ 4056 себе). Межкартового переноса в точку у стокового сервера
	//   НЕТ (6014 — только к игроку, 6084 — только смена канала на своей карте и клетке): разбор и предложение патча —
	//   `Reports/batch20/gm_teleport/README.md`. ★ Чужая карта — серверный P123 (09.11.2026, «Да, P123»): 6010 с телом 8 б
	//   (X, Z, карта, метка 0xA123, без паддинга) → 5055 код 1; ключ `asda.Gm.MapTeleportOtherMap 0` — ничего не слать (строка 991300).
	//   КТО: только персонаж с именем «GM_…» (имя от сервера, `AsdaNpcGmOnly::SelfIsGm`) И с правами, подтверждёнными сервером
	//   в этой сессии (`IsOperatorConfirmedByServer`: 6006 «принято» / 6037). Не подтверждено — одна проба 6036 (только чтение
	//   на сервере), телепорт уходит по её ответу или отменяется через 5 с. Обычному игроку — `NotGm`: ничего не отправляется,
	//   клик идёт обычным путём (ход/автопуть, как у оригинала). Ключ `asda.Gm.MapClickTeleport 0` только ВЫКЛЮЧАЕТ.
	// Ctrl+ЛКМ по карте: ClickMapId — карта, по которой щёлкнули (мини-карта — своя; карта мира — выбранный регион),
	//   SelfMapId — своя карта, MapW/MapH — размер своей карты в клетках (0 — неизвестен, проверку оставляем серверу).
	EAsdaGmMapTp TryMapClickTeleport(int32 ClickMapId, int32 SelfMapId, int32 TileX, int32 TileZ, int32 MapW, int32 MapH, bool bBigMap);
	// Ждёт ли клик ответа на пробу прав (самотест).
	bool IsMapTpPending() const { return bTpPending; }

	// ── Самотест ──
	// Разобрать ответ так, будто он пришёл с сервера (тот же путь, что живой: очередь → тик).
	void DebugInjectReply(int32 Opcode, const TArray<uint8>& Body);
	// Разобрать очередь сейчас (самотест не ждёт тика).
	void DebugFlush() { ProcessQueue(); }
	// Строки, выведенные в чат последними ответами (самотест сверяет печать эталона).
	const TArray<FString>& DebugLastLines() const { return LastLines; }
	// Очистить журнал строк (самотест меряет вывод каждого ответа с нуля: журнал ограничен 64 строками).
	void DebugClearLines() { LastLines.Reset(); }
	// Число принятых кадров кика 6019 (самотест; само закрытие связи офлайн не происходит).
	int32 DebugKickCount() const { return KickCount; }
	// Ждёт ли меню ответа 6037 на свой запрос характеристик.
	bool IsStatsPending() const { return bStatsPending; }
	// Отсчёт 6145: идёт ли, длительность и прошедшее время (мс), действие по концу.
	bool IsCountDownRunning() const { return bCdRunning; }
	float GetCountDownElapsedMs() const { return CdElapsedMs; }
	float GetCountDownDurationMs() const { return CdDurationMs; }
	int32 GetCountDownAction() const { return CdAction; }
	// Прогнать отсчёт на DeltaMs без тика ядра (самотест: тот же шаг, что в `OnTick`, вне зависимости от мира).
	void DebugTickCountDown(float DeltaMs) { TickCountDown(DeltaMs); }
	// Блок 6147: признак (байт `ctx+12` эталона) и индекс зоны (`CCharacter+0x33074`), −1 — не в зоне.
	bool IsMapBlockOn() const { return bMapBlock; }
	int32 GetMapBlockZone() const { return MapBlockZone; }
	// Зона блока 6147 по игровой позиции (строгие неравенства, первая подходящая; −1 — ни одной). Публична ради самотеста.
	static int32 MapBlockZoneAt(const FVector2D& GamePosXZ);

	// Разбор строк ввода (публичны ради самотеста).
	// «X Y» → две клетки. false — не два целых.
	static bool ParseCell(const FString& S, int32& OutX, int32& OutY);
	// «код количество [склад|warehouse|1]» → поля 6042. false — код/количество не целые.
	static bool ParseItemGive(const FString& S, int32& OutCode, int32& OutAmount, bool& bOutWarehouse);
	// «группа [режим]» → поля 6045/6046.
	static bool ParseGroup(const FString& S, int32& OutGroup, int32& OutMode);
	// «тип множитель канал карта ДД.ММ ЧЧ:ММ ДД.ММ ЧЧ:ММ мин макс» → поля 6038 (проверка диапазонов как у сервера, group3 §1.3).
	struct FEventSetArgs
	{
		uint8 Type = 0;       // 0 дроп, 1 опыт
		float Mult = 1.f;     // 0…10
		int32 Channel = -1;   // −1 = все каналы
		int32 Map = -1;       // −1 = все карты
		uint8 SDay = 1, SMon = 1, SHour = 0, SMin = 0;   // начало
		uint8 EDay = 1, EMon = 1, EHour = 0, EMin = 0;   // конец
		uint8 MinLevel = 1, MaxLevel = 100;              // уровни
	};
	static bool ParseEventSet(const FString& S, FEventSetArgs& Out);
	// «тип канал карта» → поля 6040.
	static bool ParseEventOff(const FString& S, uint8& OutType, int32& OutChannel, int32& OutMap);
	// Шаблон ввода 6038 на текущее время: опыт ×2, все каналы и карты, сейчас → +24 ч, уровни 1…100.
	static FString DefaultEventSetText();
	// «секунды [охват [действие]]» → поля 6145: секунды 1…86400, охват 0…2, действие 0…2.
	static bool ParseCountDown(const FString& S, int32& OutSec, int32& OutScope, int32& OutAction);
	// «minX minZ maxX maxZ X Z» → шесть клеток 6146 режима 1 (min ≤ max, клетки > 0).
	static bool ParseAreaJump(const FString& S, int32 (&Out)[6]);

private:
	// Регистрация пунктов вкладки «Команды оригинала».
	void RegisterCommands();
	// Регистрация одного пункта: id, подпись/подсказка (loc), действие.
	void AddCommand(const TCHAR* Id, int32 TitleLoc, int32 HintLoc, TFunction<void(APlayerController*)> Activate);

	// ── Цель ──
	// Выбранный персонаж игрока: userNum, UserIndex, имя. false — цели нет / не игрок (строка в чат).
	bool GetTargetPlayer(APlayerController* PC, int32& OutUserNum, int32& OutUserIndex, FString& OutName, bool bAllowSelf);
	// Выбранный монстр: номер сущности сервера и группа. false — цели нет / не монстр (строка в чат).
	bool GetTargetMob(APlayerController* PC, int32& OutRid, int32& OutGroup);

	// ── Действия пунктов ──
	void ActByName(int32 Which);                    // 0 Summon, 1 Meet, 2 Kick
	void ActKill(APlayerController* PC);
	void ActMove(APlayerController* PC);
	void ActAdd(uint8 Type);                         // 0 опыт, 1 золото, 2 очки навыков
	void ActMobInfo(APlayerController* PC, uint8 Mode);
	void ActMonsterGroup(APlayerController* PC, bool bPop);
	void ActEventSet();
	void ActEventOff();
	void ActItemGive(APlayerController* PC);
	void ActCharValue(APlayerController* PC);
	// Группа 4 (`group4_spec.md` §11).
	void ActCallMap();                               // 4209 собрать игроков карты к себе
	void ActClearItems();                            // 6007 убрать предметы с земли в своём секторе
	void ActJump(APlayerController* PC);             // 6010 прыжок в клетку своей карты
	void ActRevive(APlayerController* PC);           // 6024 воскресить / восполнить цель
	void ActCoolReset(APlayerController* PC);        // 6058 сбросить откаты навыков (себе / цели)
	void ActCountDown();                             // 6145 обратный отсчёт
	void ActAreaJump(APlayerController* PC);         // 6146 режим 1: перенести всех из прямоугольника
	void ActMapBlock(bool bOn);                      // 6147 блок карты quizstage
	// Своя клетка сервера по позиции павна (тайл = игровая координата / 100). false — павна нет.
	bool GetSelfCell(APlayerController* PC, int32& OutX, int32& OutZ) const;

	// ── Ответы ──
	void OnGmPacket(int32 Opcode, const TArray<uint8>& Body);
	bool OnTick(float DeltaTime);
	void ProcessQueue();
	void HandleReply(int32 Opcode, const TArray<uint8>& Body);
	void DumpMobCard6034(const TArray<uint8>& B);
	void DumpStats6037(const TArray<uint8>& B);
	void DumpCharValue6075(const TArray<uint8>& B);
	// 6025 у цели: «Служба Поддержки воскрешает тебя» — порт `char_onLocalPlayerMapEntry@0xF49090`.
	void ApplyGmRevive6025();
	// Свой персонаж (павн локального контроллера); nullptr — не в мире.
	class AAsdaPlayerCharacter* SelfChar() const;
	// 6058 у цели: сбросить все откаты навыков (окно навыков, панель, навыки Дружбы) — `skillHotbarRefreshForUser`.
	void ApplyCoolReset6058();
	// 6145: старт отсчёта (`netHandleCountDownStart@0xBB6050`) и его шаг (`recallCountdown_Tick@0xECC9F0`).
	void StartCountDown(int32 Seconds, int32 Scope, int32 Action);
	void TickCountDown(float DeltaMs);
	// Крупная цифра отсчёта по центру экрана (стиль 0x16 `FloatingText_Spawn`, x = ширина/2 − 5, y = 200).
	void SpawnCountDownDigit(int32 Value);
	// 6147: блок карты (`netHandleTeleportToPosResponse@0xBB6160`, имя ложное).
	void ApplyMapBlock6147(bool bOn);

	// Строка в системный чат (и в журнал самотеста). bPlank — ещё плашкой над чатом (как `CControl_SetTooltipText` эталона).
	void Say(const FString& Text, FColor Color, bool bPlank = false);
	// Запись в журнал выведенных строк (самотест) с пределом 64 — для плашек без строки чата тоже.
	void AddLine(const FString& Text);
	// Строка «отправлено / не отправлено» после команды без ответа сервера.
	void SaySent(bool bOk, const FString& What);
	FString Loc(int32 Id) const;

	FDelegateHandle HGmPacket;                     // подписка на ответы
	FTSTicker::FDelegateHandle TickHandle;         // тик разбора очереди
	TArray<TPair<int32, TArray<uint8>>> Queue;     // ответы до разбора
	TArray<FString> LastLines;                     // последние выведенные строки (самотест)
	bool bStatsPending = false;                    // ждём 6037 на свой «Мои характеристики»
	int32 InvincibleToggles = 0;                   // сколько раз в сессии переключена неуязвимость (сервер состояния не шлёт)
	int32 KickCount = 0;                           // принятые 6019 (самотест)
	// ── Отсчёт 6145 (`g_RecallCountdown` эталона) ──
	bool bCdRunning = false;                       // byte[+20] «идёт»
	float CdDurationMs = -1.f;                     // [+8] длительность, мс
	float CdElapsedMs = 0.f;                       // [+16] прошло, мс
	int32 CdAction = 1;                            // [+4] действие по концу (инициализатор эталона — 1)
	float CdNoticeAccumMs = 0.f;                   // `g_NoticeTimerAccum` — накопитель секундной плашки
	// ── Блок 6147 ──
	bool bMapBlock = false;                        // байт `ctx+12` (`set_oC`): в оригинале его никто не читает
	int32 MapBlockZone = -1;                       // `CCharacter+0x33074` — зона, в которой застал блок
	// ── ★ 09.11.2026 GM-телепорт Ctrl+ЛКМ: клик, ждущий ответа 6037 на пробу прав ──
	bool bTpPending = false;                       // ждём ответа
	int32 TpMapId = -1;                            // карта, на которой щёлкнули (своя на момент клика)
	int32 TpX = 0, TpZ = 0;                        // клетка
	uint32 TpProbeSeq = 0;                         // `GetGmRightsAnswerSeq` до пробы — его смена = ответ пришёл
	double TpDeadline = 0.0;                       // стенные часы: после — отмена (строка 991301)
	void TickMapTpPending();                       // шаг ожидания (из OnTick)
	int32 TpDest = -1;                             // ★ P123: карта назначения (−1 — своя, форма стока)
	void SendMapTpJump(int32 X, int32 Z, int32 DestMap = -1);   // 6010 (DestMap ≥ 0 — форма P123 на другую карту) + строка чата
};
