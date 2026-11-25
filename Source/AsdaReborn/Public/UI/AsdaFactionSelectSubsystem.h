// Copyright (c) 2026 @sitxovski (Discord)
// UAsdaFactionSelectSubsystem — ВЫБОР АРМИИ (ФРАКЦИИ): окно `CAMPWAR_Win` (JUI **44000**) и подтверждение
//   `CAMPWAR_select` (JUI **46000**). Порт 30.10.2026 по багу игроков «нет кнопки выбора фракции» (заглушка
//   «faction selection window is not available yet (code 30056)»). Полный разбор — `Reports/faction_select/FLOW.md`,
//   дамп ассетов — `Reports/faction_select/wbp_tree.txt`.
//
// ═════════════════ ★★★ ПОТОК ОРИГИНАЛА (все ветки) ═════════════════════════════════════════════
//   ВХОД ОДИН: кнопка диалога NPC с кодом **30056** («Select Team») — NPC 133 (карта 7, Пламения), 179 (карта 0, Силарис),
//     180 (карта 3, Альпия) (`NpcNsrButton.json`). `CGameUI_HandleControlCommand` (game_main.c:10233-10236):
//     `campWarWindowShow(g_pCampWarClan)`, затем `UI_resetAllWindowsAndLeave_5045` — РАЗГОВОР ЗАКРЫВАЕТСЯ.
//   ДАННЫЕ ОКНА — пакет 6701 (`Recv_ClanInfo@0xCA3C60`, 44 б): запрос 6700 (`Clan_Send_Op6700@0xCA21A0`, i32 charId +
//     u8 0) клиент шлёт сам при ОТКРЫТИИ разговора с NPC 133/179/180/184 (`itemIsCategory133or179_184@0xDA3E90`,
//     `regionWindow_OpenAndPathMove`). Байты 18…39 тела → `CCampWar+0xBAE`; i16 по +19/+21/+23 — численность армий
//     0/1/2 (`campWarWindowShow` читает `CCampWar+2991/2993/2995`). Сервер (`User_Game_GetMainInfo@0x461CA0` → 3601 →
//     `sub_461FA0` → 6701) отвечает только на картах войны (байт +130 записи карты ≤ 2).
//   ОКНО 44000 (`campWarWindowShow@0xC9AB10`): видимо; строки i = 0…2 — знак `CampWar_MarkImage` 64×64 (44020+10i),
//     имя loc 10191+i (44027+10i), полоса 0…100 (44050+10i), процент «%.2f%%» = доля i от суммы трёх (44029+10i;
//     сумма ≤ 0 → 0), кнопка «выбрать» loc 10002 (44025+10i). Заголовок 44001 = 10001, «Закрыть» 44099 = 5035, X 44002.
//     Проверка строки `CampWar_BaseTable` (`sub_CA1440`) для слотов 0…2 у оригинала всегда истинна: в таблице 3 записи.
//   «ВЫБРАТЬ» i (`CClan_OnMemberListUI@0xC9E920`, a2 = 44025/44035/44045) → `campWarShowSlotWindow@0xC9AD10`:
//     `self[2244] = i`, окно 46000 наверх и видно, имя 46020 = loc 10191+i, знак 46012 = ячейка i. Текст 46030 = 10003
//     «Армию можно выбрать только один раз. Продолжить?», ОК 46040 = 2288, Отмена 46050 = 2289.
//   ОК 46040 → `Clan_Send_Op6702(self[2244])` (6702: i32 charId + u16 армия, 30 б) и скрыть 46000. Отмена → скрыть 46000.
//   X 44002 / «Закрыть» 44099 / ESC (событие 21, признак +0x228 у 44000) → скрыть 44000 (`sub_C9ACE0`); 46000 при этом
//     остаётся как есть (у оригинала признака ESC у 46000 нет). `uiCloseAllWindows` окно 44000 НЕ закрывает.
//   ОТВЕТ 6703 (`Recv_ClanCreateResult@0xCA3DF0`, 3 б: u8 код, i16 армия) — каждый исход однокнопочным окном
//     (`CItem_BuildTooltipText(g_pItemTooltipUI, 0, id, текст, 1)`):
//     1 → своя армия (`CCampWar+0xB7C`), `CFactionUI_UpdateInfo(армия, очки, звание)` (очки/звание — из 6713, если
//         звание > 0, иначе 0/0), нотис 10008 «Ты присоединяешься к армии '[ss1]'» ([ss1] = loc 10191+армия);
//     0 → 10009 · 2 → 10005 «уже выбрал» · 3 → 10004 «сменить профессию» · 4 → 10006 «нет места» · 5 → 10010; иное — молча.
//   СЕРВЕР (`User_Game_SelectSquad@0x462480`): армия уже есть → 2; `CUser+435` (смен профессии) < 2 → 3; иначе 3603 в
//     канал Operation → `Chat_Game_SelectSquad@0x4626C0`: код 1 → `CUser+259148` = армия, 6703 и рассылка 6722 вокруг.
//     БД `Joy1.UPCharCampSelect` ставит армию на ВЕСЬ АККАУНТ (все персонажи `CharAppr` по Userindex).
//   ★ Нумпада нет, звуков нет, повторную отправку оригинал не запирает (сервер ответит кодом 2).
#pragma once

#include "CoreMinimal.h"
#include "AsdaTickGate.h"   // ★ R4-b (14.11.2026): шаблон класса (CDO) не тикает
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "AsdaNetQueue.h"              // TAsdaNetQueue — очередь тел 6701/6703 (закон no-reentrant-net-callbacks)
#include "AsdaSessionReset.h"          // IAsdaSessionResettable
#include "AsdaFactionSelectSubsystem.generated.h"

class UUserWidget;
class UWidget;
class UImage;
class UButton;
class UTextBlock;
class UAsdaButtonRelay;

// ── Тексты окон. Все id сверены с `RawAssets/Text/GameText.csv`; каждый — из своего обработчика эталона. ──
namespace AsdaFactionSelectLoc
{
	constexpr int32 kTitle        = 10001;   // 44001 «Выбери армию»            (`CCampWar_InitSelectWindow`)
	constexpr int32 kBtnSelect    = 10002;   // 44025/44035/44045 «выбрать»
	constexpr int32 kBtnClose     = 5035;    // 44099 «Закрыть»
	constexpr int32 kConfirmText  = 10003;   // 46030 «Армию можно выбрать только один раз. Продолжить?» (`CCampWar_InitSelectMsg`)
	constexpr int32 kConfirmOk    = 2288;    // 46040 «ОК»
	constexpr int32 kConfirmNo    = 2289;    // 46050 «Отмена»
	constexpr int32 kFactionBase  = 10191;   // имя армии = индекс + 10191 (0 Свет / 1 Тьма / 2 Хаос)
	constexpr int32 kResJoined    = 10008;   // 6703 код 1 — «Ты присоединяешься к армии '[ss1]'»
	constexpr int32 kResFailed    = 10009;   // 6703 код 0 — «Не удалось выбрать армию.»
	constexpr int32 kResAlready   = 10005;   // 6703 код 2 — «Персонаж уже выбрал армию.»
	constexpr int32 kResJob       = 10004;   // 6703 код 3 — «…сменивший профессию…»
	constexpr int32 kResFull      = 10006;   // 6703 код 4 — «В выбранной армии нет места.»
	constexpr int32 kResBusy      = 10010;   // 6703 код 5 — «Это поле боя занято другой армией.»
}

UCLASS()
class ASDAREBORN_API UAsdaFactionSelectSubsystem : public UGameInstanceSubsystem, public FTickableGameObject,
	public IAsdaSessionResettable
{
	GENERATED_BODY()

public:
	// Подписка на 6701/6703 (`OnCampWarSquadPacket`) и на свою фракцию 6713 (`OnSelfFaction`), роспись ESC.
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	// Отписка, снятие виджетов.
	virtual void Deinitialize() override;
	// Окна и данные 6701 принадлежат сессии: любое снятие — окна скрыты, очереди пусты, численность армий забыта.
	virtual void ResetSessionState(EAsdaSessionResetReason Reason) override;
	// Разбор очередей 6701/6703 (закон no-reentrant-net-callbacks).
	virtual void Tick(float DeltaTime) override;
	// Стат тикающего объекта.
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UAsdaFactionSelectSubsystem, STATGROUP_Tickables); }
	// В редакторе не тикаем.
	// ★ R4-b (14.11.2026): шаблон класса (CDO) не тикает — раньше `Tick` звался дважды за кадр, у рабочего объекта и у шаблона
	//   (ключ `asda.Perf.NoCdoTick`, разбор — `AsdaTickGate.h`). Рабочий объект тикает как раньше.
	virtual bool IsTickable() const override { return AsdaTickGate::NotCdo(this); }
	virtual bool IsTickableInEditor() const override { return false; }
	// На паузе не тикаем.
	virtual bool IsTickableWhenPaused() const override { return false; }

	// ── Числа окон (`.JUI`, `CProgressBar_ctor`, `CCampWar_InitSelectWindow`) ──
	static constexpr int32 kSquadCount  = 3;      // армий у оригинала ровно три (строки окна 0…2)
	static constexpr float kBarFullW    = 160.f;  // ширина полосы 44050+10i (`Size = "160 20"`)
	static constexpr float kBarMax      = 100.f;  // `CProgressBar_ctor`: диапазон 0…100.0
	static constexpr int32 kSelectZ     = 700;    // окно 44000 (уровень окон сообщества, как окно города 79300)
	static constexpr int32 kConfirmZ    = 710;    // окно 46000 выше 44000 (`CClass_MapInsert` — наверх)
	static constexpr int32 k6703Len     = 3;      // тело 6703 (`sub_4C28F0(…, 6703, 3, …)`)

	// ═══ ВХОД / ЗАКРЫТИЕ ═══
	// Порт `campWarWindowShow@0xC9AB10` — кнопка NPC 30056 (разговор закрывает вызывающий, как эталон).
	UFUNCTION(BlueprintCallable, Category="Asda|FactionSelect")
	void OpenFromNpc();
	// Порт `sub_C9ACE0` — X 44002, «Закрыть» 44099, ESC.
	UFUNCTION(BlueprintCallable, Category="Asda|FactionSelect")
	void CloseSelectWindow();
	// Окно 44000 видно.
	UFUNCTION(BlueprintPure, Category="Asda|FactionSelect")
	bool IsSelectOpen() const { return bSelectOpen; }
	// Окно подтверждения 46000 видно.
	UFUNCTION(BlueprintPure, Category="Asda|FactionSelect")
	bool IsConfirmOpen() const { return bConfirmOpen; }
	// Порт ветки `itemIsCategory133or179_184` в `regionWindow_OpenAndPathMove`: NPC войны — запросить 6700 (режим 0).
	//   Зовёт диалог NPC при открытии разговора. true — NPC из списка (запрос отдан сетевому слою).
	bool OnNpcTalkOpened(int32 NpcId);
	// NPC, у которого открытие разговора запрашивает 6700 (133, 179, 180, 184 — `itemIsCategory133or179_184@0xDA3E90`).
	static bool IsWarInfoNpc(int32 NpcId);

	// ═══ ДЕЙСТВИЯ (боевые обработчики кнопок; самотест зовёт их же через клики Slate) ═══
	void OnSelectClicked(int32 Squad);   // 44025/44035/44045 → `campWarShowSlotWindow(i)`
	void OnConfirmOk();                  // 46040 → 6702 + скрыть 46000
	void OnConfirmCancel();              // 46050 → скрыть 46000

	// ═══ ДИАГНОСТИКА / САМОТЕСТ ═══
	int32 GetPendingSquad() const { return PendingSquad; }                   // `self[2244]` — армия в окне подтверждения
	int32 GetSquadCount(int32 Squad) const;                                   // численность армии из 6701 (−1 — нет такой)
	bool HasMainInfo() const;                                                 // 6701 приходил в этой сессии (модель `UAsdaCampWarSubsystem`)
	FString GetText(int32 IdNum) const;                                       // текст контрола 44001/44027…/44029…/44025…/44099/46020/46030/46040/46050
	float GetBarFrac(int32 Squad) const;                                      // доля заливки полосы строки (0…1; −1 — нет полосы)
	bool IsMarkShown(int32 IdNum) const;                                      // знак 44020/44030/44040/46012 нарисован
	UWidget* GetHitWidget(int32 IdNum) const;                                 // кнопка 44025/44035/44045/44099/44002/46040/46050
	const TArray<int32>& GetNoticeTrace() const { return NoticeTrace; }      // loc-id показанных нотисов 6703
	FString GetLastNoticeText() const { return LastNoticeText; }             // текст последнего нотиса
	void ClearTrace() { NoticeTrace.Reset(); LastNoticeText.Reset(); }        // сбросить след нотисов
	int32 GetLastSelfPoints() const { return LastSelfPoints; }               // очки армии из последнего 6713 (для 6703 код 1)
	int32 GetLastSelfGrade() const { return LastSelfGrade; }                 // звание из последнего 6713
	FString DumpControls() const;                                             // каждый разобранный контрол обоих окон
	int32 GetFoundControls() const { return FoundControls; }                  // разобрано контролов
	int32 GetTotalControls() const { return TotalControls; }                  // ожидалось контролов

private:
	// ── Окна ──
	bool EnsureSelectWidget();         // каркас `FAsdaJuiWindowHandle::Ensure` окна 44000
	bool EnsureConfirmWidget();        // то же для 46000
	void ResetSelectWidget();          // снять ссылки на виджет 44000
	void ResetConfirmWidget();         // снять ссылки на виджет 46000
	void SetupSelectControls();        // порт `CCampWar_InitSelectWindow@0xC971A0` (разбор + статичные подписи)
	void SetupConfirmControls();       // порт `CCampWar_InitSelectMsg@0xC97630`
	void HideConfirm();                // `CControl_SetVisible(self[89], 0)`
	void ApplyPercents();              // часть `campWarWindowShow`: полосы и «%.2f%%» по численности из 6701
	// ── Сеть ──
	void OnSelectResult(const TArray<uint8>& B); // порт `Recv_ClanCreateResult`
	// ── Прочее ──
	void Notice(int32 LocId, const FString& Text);   // ONEBUT (`CItem_BuildTooltipText(…, 0, id, …)`) + след
	FString Loc(int32 LocId) const;                  // loc-строка (теги срезаны)

	// ═══ ВИДЖЕТЫ ОКНА 44000 ═══
	UPROPERTY(Transient) TObjectPtr<UUserWidget> SelectWidget = nullptr;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtTitle = nullptr;          // 44001 `CAMPWAR_text`
	UPROPERTY(Transient) TObjectPtr<UButton>    BtnX = nullptr;              // 44002 `CAMPWAR_Win_BTN00`
	UPROPERTY(Transient) TObjectPtr<UButton>    BtnClose = nullptr;          // 44099 `CAMPWAR_Clik_M`
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtClose = nullptr;          // подпись 44099
	UPROPERTY(Transient) TObjectPtr<UImage>     Marks[3];          // 44020+10i — план 64×64
	UPROPERTY(Transient) TObjectPtr<UTextBlock> Names[3];          // 44027+10i
	UPROPERTY(Transient) TObjectPtr<UImage>     BarBack[3];        // 44050+10i ES0 — жёлоб полосы
	UPROPERTY(Transient) TObjectPtr<UImage>     BarFill[3];        // 44050+10i ES1 — заливка
	UPROPERTY(Transient) TObjectPtr<UTextBlock> Percents[3];       // 44029+10i
	UPROPERTY(Transient) TObjectPtr<UButton>    BtnSelect[3];      // 44025+10i
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtSelect[3];      // подпись 44025+10i
	float BarFrac[kSquadCount] = { 0.f, 0.f, 0.f };                          // текущая доля заливки (диагностика)

	// ═══ ВИДЖЕТЫ ОКНА 46000 ═══
	UPROPERTY(Transient) TObjectPtr<UUserWidget> ConfirmWidget = nullptr;
	UPROPERTY(Transient) TObjectPtr<UImage>     ConfirmMark = nullptr;       // 46012 `CAMPWAR_select_M3_18_0`
	UPROPERTY(Transient) TObjectPtr<UTextBlock> ConfirmName = nullptr;       // 46020 `CAMPWAR_select_TEXT_1`
	UPROPERTY(Transient) TObjectPtr<UTextBlock> ConfirmText = nullptr;       // 46030 `CAMPWAR_select_TEXT`
	UPROPERTY(Transient) TObjectPtr<UButton>    BtnOk = nullptr;             // 46040 `CAMPWAR_select_OK_C`
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtOk = nullptr;             // подпись 46040
	UPROPERTY(Transient) TObjectPtr<UButton>    BtnNo = nullptr;             // 46050 `CAMPWAR_select_NO_C`
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtNo = nullptr;             // подпись 46050

	UPROPERTY(Transient) TArray<TObjectPtr<UAsdaButtonRelay>> Relays;        // реле кнопок обоих окон (GC-якорь)
	int32 FoundControls = 0;           // разобрано контролов
	int32 TotalControls = 0;           // ожидалось контролов

	// ═══ МОДЕЛЬ (поля `CCampWar` эталона) ═══
	bool bSelectReady = false;         // контролы 44000 разобраны
	bool bConfirmReady = false;        // контролы 46000 разобраны
	bool bSelectOpen = false;          // 44000 видно
	bool bConfirmOpen = false;         // 46000 видно
	int32 PendingSquad = -1;           // `self[2244]` — армия, показанная в 46000
	int32 LastSelfPoints = 0;          // `CCampWar+2946` — очки армии из 6713
	int32 LastSelfGrade = 0;           // `CCampWar+2950` — звание из 6713
	TAsdaNetQueue<TArray<uint8>> PendingResults;     // тела 6703 из сети
	FDelegateHandle HSquadPacket;      // подписка на `OnCampWarSquadPacket`
	FDelegateHandle HSelfFaction;      // подписка на `OnSelfFaction` (6713)
	TArray<int32> NoticeTrace;         // след нотисов
	FString LastNoticeText;            // текст последнего нотиса
};
