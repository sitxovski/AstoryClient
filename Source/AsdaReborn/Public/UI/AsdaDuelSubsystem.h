// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ ДУЭЛЬ (PvP-ПОЕДИНОК) — порт `CDuelMgr` (глобал `g_pDuelMgr` = 0x2C30410) и оверлея `PVP_Gfont_Win` (JUI 1200).
//   Волна 2-В (06.10.2026), отчёт `Reports/owner_1006/wave2C_duel_mark_banner.md` §1.
//
//   ПОЛНЫЙ ПОТОК (клиент — combat_pvp.c:6029-6760, ui_core/render_core; сервер — GameServer.c:132064-132364, 77570-78019):
//     1. Вызов: меню игрока 2070 → 4302 (уже есть: `UAsdaPlayerMenuSubsystem::ActionDuel`). Сервер пересылает 4302 ЦЕЛИ.
//     2. Цель: 4302 → `Recv_PartyMember(g_pQuickSlotBar, 14, pkt)`: `duelCopyTargetInfoFromPkt` (только если +26 = я)
//        + значок уведомления 14 (`statusBarAddIcon`: `m_bRequestActive = 1`, WM_TIMER 31 на 30 000 мс).
//        Клик по значку → `duelUiPanelShow`: окно 2245 (ОБЩЕЕ с отрядом) с текстом «имя» + loc 108100, шкала 0…30 000.
//        ОК/Отмена (2248/2249) → 4303 с ответом 1/0 + `duelUiPanelReset`. WM_TIMER 31 → `duelUiPanelReset` + снять значок 14.
//     3. Сервер: 4303 обоим (+0 = 1 принято, иначе коды отказа 0/2..8) и 4305 всем в секторе (арена).
//        4303 код 1: `pTargetChar[1].buffFxSlots[136] = 0` — запуск 11-секундного отсчёта (render_core.c:3444-3475):
//        раз в секунду цифры 10…1 в окне 1200 и строка 108116; в конце — 108115 и надпись START (1201), угасание 3 с.
//     4. 4305: запись арены (радиусы 3000/2000 вокруг тайла-центра), `[92] = 1` участникам, флаг типа 6 в центре,
//        флаги типа 5 над участниками, эффект `Camp_start.jeb` своему. Каждый кадр — `uiZoneProximityWarnTick`:
//        дальше 2000 → 108124, дальше 3000 → 108125 (не чаще 500 мс).
//     5. 4304 (55 б) результат: эффекты `pvp_win.jeb`/`pvp_lose.jeb`, проигравшему — жест 51; своему участнику —
//        108117, сброс PvP-полей, гашение PvP-иконки портрета, 108112/108122 (тип 0..3) или 108113/108123 (тип 4)
//        и надпись VICTORY (1202) / LOSE (1203); зрителям — 108112/108113.
//     6. Вход на карту (game_main.c:3896-3903): арены и флаги удаляются, окно 1200 гаснет, `[88]/[80]/[92]` сброшены.
//   Что перенесено, что отложено (нет системы флагов-объектов и ассетов эффектов/жеста), дефекты оригинала и сервера —
//   в отчёте. Нотисы — по таблице §1.3 отчёта (решение владельца: итоги дуэли — журнал + буфер B, категория Duel).
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "AsdaSessionReset.h"
#include "AsdaTickGate.h"   // ★ R4, доля a: шаблон класса не тикает (общая точка)
#include "AsdaDuelSubsystem.generated.h"

class UUserWidget;
class UWidget;
class AActor;

UCLASS()
class ASDAREBORN_API UAsdaDuelSubsystem : public UGameInstanceSubsystem, public FTickableGameObject,
	public IAsdaSessionResettable
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;   // подписки: сеть, хотбар, окно 2245
	virtual void Deinitialize() override;                                      // отписки, снять окно 1200
	// Смена карты — как вход на карту эталона (арены, окно 1200, поля режима); полный сброс — всё, включая вызов.
	virtual void ResetSessionState(EAsdaSessionResetReason Reason) override;

	virtual void Tick(float DeltaTime) override;   // таймеры вызова, отсчёт, предупреждения арены, угасание надписей
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UAsdaDuelSubsystem, STATGROUP_Tickables); }
	// ★ R4, доля a (14.11.2026): шаблон класса (CDO) не тикает — `AsdaTickGate.h`, ключ `asda.Perf.NoCdoTick`; рабочий объект тикает как раньше.
	virtual bool IsTickable() const override { return AsdaTickGate::NotCdo(this); }
	virtual bool IsTickableInEditor() const override { return false; }
	virtual bool IsTickableWhenPaused() const override { return false; }

	// ── Модель `CDuelMgr` (поля эталона) ──
	/** Вызов, пришедший МНЕ (`m_nTargetSessionId` +132, `m_wTargetUserIdx` +142, `m_szTargetName` +144, `m_nTargetField34` +136). */
	struct FTarget
	{
		int32   CharId  = -1;   // charId вызвавшего (4302 @2)
		int32   UserIdx = -1;   // userNum вызвавшего (4302 @0)
		FString Name;           // имя вызвавшего (4302 @6, вписывает сервер)
		int32   PvpIdx  = -1;   // номер группы на сервере (4302 @32)
	};
	/** Запись арены (`operator new(0x28)`, CDuelMgr_CreateDuelArena): id = pvpIdx + 10000. */
	struct FArena
	{
		int32     ObjId  = -1;        // [0] pvpIdx + 10000
		FVector2D Center = FVector2D::ZeroVector;   // [7..9] позиция тайла-центра (игровые X/Z)
		float     OuterR = 3000.f;    // [2] 0x453B8000 — граница поля
		float     InnerR = 2000.f;    // [3] 0x44FA0000 — «далеко от флага»
		int32     CharA  = -1;        // [5] charId вызвавшего
		int32     CharB  = -1;        // [6] charId ответившего
		TWeakObjectPtr<AActor> StartFx;   // [1] эффект `Camp_start.jeb` (гасится по 4304)
		TWeakObjectPtr<AActor> CenterFlag;   // [4] ★ 31.10.2026: флаг-объект типа 6 `pvp_flag_land` в центре (`clanFlagSpawnAt`)
	};

	const FTarget& GetTarget() const { return Target; }
	bool  IsRequestActive() const { return bRequestActive; }        // `m_bRequestActive` (+180)
	bool  IsPromptShown() const { return bPromptShown; }            // окно 2245 показано дуэлью
	bool  IsCountdownRunning() const { return CountdownMs < 11000u; }   // `[136] < 0x2AF8`
	int32 GetLastCountdownShown() const { return LastCountdown; }   // `m_nLastCountdown`
	int32 GetLastSlotShown() const { return LastSlot; }             // `m_nLastSlotShown`: 0 START, 1 VICTORY, 2 LOSE
	int32 GetArenaCount() const { return Arenas.Num(); }
	bool  IsInArena(int32 CharId) const { return InArenaChars.Contains(CharId); }   // `[92] == 1`
	// ★ 31.10.2026 (самотест флагов дуэли): флаг типа 6 арены группы Pvp и флаг типа 5 за спиной участника charId (nullptr — нет).
	AActor* DebugCenterFlag(int32 Pvp) const;
	class USkeletalMeshComponent* DebugBackFlag(int32 CharId) const;
	int32 GetDuelIndex(int32 UserIdx) const { const int32* P = DuelIndexByUser.Find(UserIdx); return P ? *P : -1; }   // `[156]`

	// ── Отладка/самотест (живой код, без копий логики) ──
	void DebugSetManualClock(bool bOn) { bManualClock = bOn; }   // настоящий Tick перестаёт двигать время
	void DebugAdvance(float Seconds) { Advance(Seconds); }       // тот же шаг, что у Tick
	/** След нотисов (loc id в порядке выдачи) — «обработчик отправил». */
	const TArray<int32>& DebugNoticeTrace() const { return NoticeTrace; }
	void DebugClearTrace() { NoticeTrace.Reset(); }
	/** Последний текст нотиса каждого loc id (для сверки подстановки имён). */
	FString DebugLastText(int32 LocId) const { const FString* S = LastTextByLoc.Find(LocId); return S ? *S : FString(); }
	/** Видимость узлов окна 1200: 0 START/1 VICTORY/2 LOSE; цифры: десятки/единицы. */
	bool  DebugIconShown(int32 Slot) const;
	float DebugIconOpacity(int32 Slot) const;
	int32 DebugShownTens() const;   // показанная цифра десятков (−1 нет)
	int32 DebugShownOnes() const;   // показанная цифра единиц (−1 нет)
	bool  DebugOverlayShown() const;
	UUserWidget* DebugOverlay() const { return Overlay; }
	/** Сброс всего состояния между сценариями самотеста. */
	void  DebugResetAll() { ResetAll(TEXT("самотест")); NoticeTrace.Reset(); LastTextByLoc.Reset(); ResultDedup.Reset(); }
	/** Сколько повторов 4304 отброшено исправлением [ПОРТ] (сервер шлёт проигравшему дважды). */
	int32 DebugDedupDropped() const { return DedupDropped; }

private:
	// ── сеть (делегат `OnDuelPacket`) ──
	void OnDuelPacket(int32 Opcode, const TArray<uint8>& Body);
	void OnChallenge4302(const TArray<uint8>& B);   // `Recv_PartyMember(…, 14, …)`
	void OnNotice4303(const TArray<uint8>& B);      // `PvpDuel_OnNoticePacket@0xDE5170`
	void OnResult4304(const TArray<uint8>& B);      // `PvpDuel_ApplyMatchResult@0xDE5560`
	void OnArena4305(const TArray<uint8>& B);       // `CDuelMgr_CreateDuelArena@0xDE61E0`
	// ── окно 2245 и значок 14 ──
	void OnNoticeIconClicked(int32 Type);           // `uiNoticeButtonDispatch` case 14 → `duelUiPanelShow`
	void OnJoinBoxButton(bool bOk);                 // `CParty_OnControl` 2248/2249, ветка дуэли
	void PanelShow();                               // `duelUiPanelShow@0xDE4190`
	void PanelReset(const TCHAR* Why);              // `duelUiPanelReset@0xDE41E0`
	void SendAnswer(uint8 Answer, const TCHAR* Why);   // `duelSendReqPkt4303@0xDE4AB0`
	// ── окно 1200 ──
	bool EnsureOverlay();
	void HideControlGroup();                        // `uiHideControlGroup@0xDE3E50`
	void HideAll();                                 // `uiQuickSlot_HideAll@0xDE4000`
	void ShowTwoDigitNumber(int32 N);               // `uiQuickSlot_ShowTwoDigitNumber@0xDE3F00`
	void ShowSlot(int32 Slot);                      // `uiQuickSlot_ShowSlot@0xDE4070` (в IDA вызов назван uiShowDualControlTimed)
	// ── ход времени ──
	void Advance(float Seconds);
	void TickCountdown(float Ms);                   // render_core.c:3444-3475
	void TickProximity(float Ms);                   // `uiZoneProximityWarnTick@0xDE3710`
	// ── прочее ──
	void Notice(int32 LocId, const FString& Text, int32 Kind, const FColor& Color, float TtlMs);   // одна точка выдачи
	FString LocName(int32 LocId, const FString& Name) const;   // «имя» + loc (подстановка %s, если она в строке)
	void ClearSelfPvpFields(const TCHAR* Why);      // `[160]=0; 108117; [88]=0; [80]=−1; [92]=0; sub_922DFD(g_pHUD, 0)`
	void ResetArenas(const TCHAR* Why);             // `partyEffects_clearAll` + `uiQuickSlot_HideAll`
	void ResetAll(const TCHAR* Why);

	FDelegateHandle DuelPacketHandle;   // подписка на `OnDuelPacket`
	FDelegateHandle NoticeClickHandle;  // подписка на клики значков хотбара
	FDelegateHandle JoinBoxHandle;      // подписка на кнопки окна 2245

	FTarget Target;                     // вызов, пришедший мне
	bool  bRequestActive = false;       // `m_bRequestActive` (+180) — ставит `statusBarAddIcon(14)`
	bool  bPromptShown   = false;       // окно 2245 показано дуэлью
	float TimerAccumMs   = 0.f;         // `m_nTimerAccum` (+128) — шкала 2247
	float Timer31LeftMs  = -1.f;        // WM_TIMER 31 (30 000 мс от значка): −1 = не взведён

	uint32 CountdownMs = 0xFFFFFFFFu;   // `pChar[1].buffFxSlots[136]` своего персонажа: < 11000 — идёт отсчёт
	float  CountdownTickAccum = 0.f;    // `g_fCountdownTickAccum`
	int32  LastCountdown = -1;          // `m_nLastCountdown`
	int32  LastSlot = -1;               // `m_nLastSlotShown`
	float  SlotFadeElapsedMs = -1.f;    // угасание надписи START/VICTORY/LOSE (3000 мс), −1 = не идёт
	int32  SlotShowSeq = 0;             // счётчик показов надписи: угасание, начатое в этом шаге, шаг не съедает

	TArray<FArena> Arenas;              // `m_ArenaList` (+184)
	float WarnAccumMs = 0.f;            // `self_2[44]` — общий накопитель предупреждений
	TSet<int32> InArenaChars;           // charId с `[92] == 1` (по 4305)
	TMap<int32, int32> DuelIndexByUser; // `[156]` по userNum

	// ═══ ★★ 31.10.2026 ФЛАГИ ДУЭЛИ — объекты-флаги эталона (`uiClanFlagGetOrCreate@0xFAF910`, `Reports/duel/DUEL_FLOW.md` §3) ═══
	//   Тип 6 — `pvp_flag_land.jbm` + клип `pvp_flag_land.jbb` по кругу на тайле-центре арены (4305 `clanFlagSpawnAt`, ключ pvp+10000);
	//   тип 5 — `pvp_flag_back.jbm` + `Battle_Flag_00.jbb` по кругу на кости "2" участника, поворот YPR(π, 0, π/2) (`CharAnim_UpdateFrame`),
	//   ключ charId+20000; создаёт 4305 (оба участника) и 4206 команда 1 [7] == 1, стирают 4304 (оба), 4206 [7] == 0, вход на карту.
	//   Видим, пока персонаж виден и у него `[92] == 1` (в арене) — `TickFlags`.
	TMap<int32, TWeakObjectPtr<class USkeletalMeshComponent>> BackFlags;   // charId → флаг типа 5 (компонент на акторе персонажа)
	int32 SelfPvp92 = 0;                  // свой `[92]` по 4206 (команда 1 пишет [7] своему)
	FDelegateHandle PvpModeHandle;        // подписка на 4206 (`UAsdaNetSubsystem::OnPvpMode`)
	void SetBackFlag(int32 CharId, bool bOn, const TCHAR* Why);   // создать/стереть флаг типа 5 (`uiClanFlagGetOrCreate(…,5)` / `listEraseByObjId`)
	AActor* SpawnCenterFlag(const FVector2D& CenterGame);          // флаг типа 6 на земле центра арены
	void TickFlags();                                              // видимость флагов типа 5 (`[92] == 1` или свой 4206)
	void OnPvpMode4206(int32 UserIdx, bool bOn, int32 SubType);    // 4206 команда 1: [7] == 1 → флаг, 0 → стереть

	TArray<TPair<uint32, double>> ResultDedup;   // [ПОРТ] подписи уже применённых 4304 и время (исправление двойной доставки)
	int32 DedupDropped = 0;

	// окно 1200
	UPROPERTY(Transient) TObjectPtr<UUserWidget> Overlay = nullptr;   // WBP_PVP_Gfont_Win
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>> Icons;           // 1201 START, 1202 VICTORY, 1203 LOSE
	UPROPERTY(Transient) TObjectPtr<UWidget> TensBase = nullptr;      // 1210 `PVP_Gfont_Count` (m_pCount2Base)
	UPROPERTY(Transient) TObjectPtr<UWidget> OnesBase = nullptr;      // 1221 `PVP_Gfont_Count2` (m_pCount1Base)
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>> TensDigits;      // 1211..1220
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>> OnesDigits;      // 1222..1231

	TArray<int32> NoticeTrace;              // след нотисов (самотест)
	TMap<int32, FString> LastTextByLoc;     // последний текст по loc (самотест)
	bool bManualClock = false;              // самотест ведёт часы сам
};
