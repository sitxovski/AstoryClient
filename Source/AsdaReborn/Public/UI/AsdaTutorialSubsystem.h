// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ ОБУЧЕНИЕ (tutorial) — порт двух живых частей системы оригинала (10.11.2026).
//   Полный разбор, тексты RU/EN, условия и план — `Docs/Port/systems/TUTORIAL.md`.
//
// ЧАСТЬ А — ПОШАГОВЫЙ ГИД С РОБОТОМ (окно `Tuto_Msg_Box_4` 1950). [ОРИГИНАЛ]
//   Объект `g_aUiToggleStateBuf` (0x2C71598): init контролов `0xEBB960` (МИСНЕЙМ `uiTradeWindowInit`), тик
//   `uiTutorialWindowUpdate@0xEBBD40`, кнопки `boosterShopOnMessage_D020@0xEBD020` (МИСНЕЙМ), эффект шага
//   `Player_SpawnRareBoostEffect@0xEBD130`. Старт — вход в мир при уровне 1 (game_main.c:3178-3184), КАЖДЫЙ вход;
//   хранения прогресса нет. 22 шага, текст loc 103000+шаг, картинка `Tutorial_<шаг>.TEX`, засчёт через 1 с.
//
// ЧАСТЬ Б — КНИГА ОБУЧЕНИЯ И ЗНАЧОК «?» (`Tutorial_Notice_WIN` 69800/69801 → `Tutorial_WIN` 69200). [ОРИГИНАЛ]
//   Объект `g_pSkillTutorialWnd` (0x2BDE9D0): `CTutorial_Init@0xB77B90`, `tutorialUiInitControls@0xB77D10`,
//   `TutorialUI_RefreshPage@0xB78460`, `uiCycleToNextValidTab@0xB79420` (раз в 3 с), `uiSkillTutorialOnCommand@0xB79980`.
//   Прогресс — НА СЕРВЕРЕ: маска 256 бит на персонажа (6185 S→C при входе, 6184 C→S «раздел пройден»,
//   `Joy1.dbo.CharTutorial`). Таблица разделов — `DT_Tutorial` (импорт `Tools/asda_import_tutorial.py`).
//   Дефект оригинала «?» горит снова после перезахода: кнопка «Закрыть» 69299 (`uiPanelHideAndResetTimer@0xB79310`)
//   ставила бит только в памяти клиента и не слала 6184. «Закрыть» 6184 НЕ шлёт и в порте (решение владельца 10.11:
//   опыт за раздел — только за «КОНЕЦ», как в оригинале), дефект закрыт ЛОКАЛЬНЫМ прогрессом ниже.
//
// [ИСПРАВЛЕНИЕ] ЛОКАЛЬНЫЙ ПРОГРЕСС НА ПЕРСОНАЖА (решение владельца 10.11.2026, AsdaTutorialStore.cpp): шаг гида и
//   «гид пройден/закрыт», отметки разделов книги — в зашифрованном подписанном файле `Saved/AsdaTutorial/<хэш>.atut`
//   (у выпуска `%LOCALAPPDATA%\AsdaReborn\Saved` — патч лаунчера его не трогает). Привязка: учётка + сервер + charId.
//   Раздел пройден = бит сервера (6185) ИЛИ локальный бит. Подделанный/битый/чужой файл — тихий сброс с нуля.
//   Ключ `asda.Tutorial.LocalProgress` (0 = как оригинал: гид каждый вход с шага 1, «Закрыть» до перезахода).
//
// ЧАСТИ В/Г (поп-апы со стрелками 1900–1923, меню 69300) в оригинале не показываются — тексты TIT.BIN 0–42 пусты
//   во всех донорах, меню никто не открывает; порт их не показывает (TUTORIAL.md §4).
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "AsdaSessionReset.h"
#include "UI/AsdaMarkup.h"
#include "UI/AsdaUiInputTypes.h"   // ★ R1: FAsdaMouseFrame / FAsdaDragArm / FAsdaDragGhost … — поля ввода (без всего AsdaUiHelpers.h)
#include "UI/AsdaTutorialEvents.h"
#include "AsdaTutorialSubsystem.generated.h"

class UUserWidget;
class UWidget;
class UImage;
class UButton;
class UTextBlock;
class UDataTable;

// Одна страница раздела книги (поля записи `Tutorial.bin`, разбор — шапка импортёра).
USTRUCT(BlueprintType)
struct ASDAREBORN_API FAsdaTutorialPage
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly) FString Title;                // +112: заголовок → 69204 (RU; EN — оверлей)
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 DescId = -1;            // +1392: id текста `Tutorial_text` → 69205
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TArray<int32> AnswerId;       // +1552: 3 id ответа `Tutorial_text_answer`
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TArray<int32> NextPage;       // +1792: страница по ответу (≥20 — конец, −1 — нет)
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FString RobotImage;           // +2032: картинка ведущего 256×460
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 RobotImageType = 0;     // +3312: 1 → 69201, 2 → 69203
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FString Image;                // +3392: иллюстрация 380×332 → 69202
};

// Раздел книги обучения = запись `Tutorial.bin` (4672 б). RowName = порядковый номер записи (= индекс бита маски).
USTRUCT(BlueprintType)
struct ASDAREBORN_API FAsdaTutorialRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 Index = 0;              // порядковый номер записи
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 Id = 0;                 // +0 u16: уходит в 6184
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 Category = 0;           // +4: категория меню 69300
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FString Caption;              // +8: имя раздела (меню 69300)
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TArray<int32> CondType;       // +76: вид условия 0…16 (3 шт.)
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TArray<int32> CondA;          // +88: аргумент A
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TArray<int32> CondB;          // +100: аргумент B
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TArray<FAsdaTutorialPage> Pages;   // 20 страниц
};

UCLASS()
class ASDAREBORN_API UAsdaTutorialSubsystem : public UGameInstanceSubsystem, public FTickableGameObject,
	public IAsdaSessionResettable
{
	GENERATED_BODY()

	friend struct FAsdaTutorialSelfTest;   // самотест `asda.SelfTest.Tutorial` (Private/UI/AsdaTutorialSelfTest.cpp)

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;   // подписки на сеть, ESC-роспись книги
	virtual void Deinitialize() override;                                      // отписки, снять окна
	virtual void ResetSessionState(EAsdaSessionResetReason Reason) override;   // см. правила в .cpp

	virtual void Tick(float DeltaTime) override;   // гид (каждый кадр) + опрос «?» раз в 3 с + мышь окон
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UAsdaTutorialSubsystem, STATGROUP_Tickables); }   // счётчик тика для профайлера
	virtual bool IsTickable() const override { return !IsTemplate(); }   // шаблон подсистемы (CDO) не тикает
	virtual bool IsTickableInEditor() const override { return false; }   // только в игре
	virtual bool IsTickableWhenPaused() const override { return false; } // на паузе стоит

	// ── Общая точка флагов (`AsdaTutorial::Mark`) ──
	void MarkFlag(EAsdaTutorialFlag Flag);
	// ── `regionWindow_OpenAndPathMove`: выбор раздела + гейт «книга открыта» (см. `AsdaTutorial::OnNpcTalkOpening`) ──
	bool NpcTalkOpening();
	// ── 6185: маска пройденных разделов (32 б) ──
	void ApplyMask(const uint8* Data, int32 Len);

	// ── Гид: старт (вход в мир при уровне 1) и закрытие ──
	void StartGuide(const TCHAR* Why);
	void CloseGuide(const TCHAR* Why);
	bool IsGuideActive() const { return bGuideOn; }
	int32 GetGuideStep() const { return GuideStep; }

	// ── Книга ──
	bool IsBookOpen() const { return bBookOpen; }
	bool IsNoticeShown() const { return bNoticeShown; }
	int32 GetBookCategory() const { return BookCategory; }
	int32 GetBookPage() const { return BookPage; }
	bool IsSectionDone(int32 Index) const;   // `bitmask_testBit@0xB7AD10` по маске сервера ИЛИ локальной
	bool IsGuideDismissed() const { return bGuideDismissed; }   // гид пройден («Пока») или закрыт («Нет»/крестик)

private:
	// ─────────────── Локальный прогресс (AsdaTutorialStore.cpp) ───────────────
	FString MakeStoreBinding() const;                       // «учётка|хост|сервер|charId»; пусто — персонаж не выбран
	FString StorePathFor(const FString& Binding) const;     // файл персонажа (имя — хэш привязки, charId не виден)
	bool EnsureProgressLoaded();                            // загрузить файл персонажа один раз на привязку
	void SaveProgress(const TCHAR* Why);                    // записать, если состояние изменилось
	void GuideDismiss(const TCHAR* Why);                    // закрыть гид навсегда для персонажа («Пока»/«Нет»/крестик)
	FString StoreBinding;                                   // привязка загруженного состояния (пусто — не загружено)
	FString StoreDirOverride;                               // самотест: свой каталог вместо Saved/AsdaTutorial
	uint8 LocalMask[32] = {};                               // локальные отметки разделов (Закрыть / КОНЕЦ)
	bool bGuideDismissed = false;                           // гид для персонажа больше не поднимается
	uint8 SavedSnapshot[34] = {};                           // шаг, флаги, маска последней записи (не писать без изменений)
	bool bSavedSnapshotValid = false;                       // снимок соответствует файлу на диске
	bool bStoreReadOnly = false;                            // файл не прочитан по сбою или он от нового клиента — не затирать

	// ─────────────── Общее ───────────────
	void OnWorldReady();                                    // 4055/4056 — старт гида при уровне 1
	void OnSelfExp(int32 Level, int64 Exp, int64 Gained, bool bFullExp, int32 SrcRid);   // флаг 75
	void OnQuestStepResult(int32 Result, int32 TalkId, const TArray<uint8>& Body);         // флаг 77 (5044)
	void OnQuestTurnInResult(int32 StepCode, int32 QuestNo, int32 Stage, int32 FinishType, int32 TalkId,
	                         const TArray<uint8>& Body);                                     // флаг 81 (5042)
	FString LocText(int32 Id) const;                        // строка loc на языке сессии (сырая, с тегами)
	bool IsInGameWorld() const;                             // `g_GameState == 1` — мир со своим персонажем
	UUserWidget* EnsureWindow(FName WindowId, const TCHAR* Wbp, int32 Z, TObjectPtr<UUserWidget>& Slot);
	void SetImageTexture(UImage* Img, const FString& Name, bool bNpcImage);   // `CControl_DrawTextureRect`

	void OnQuestPacket(const TArray<uint8>& Body);          // 5047/5052… — заявка на проверку «цель выполнена» (флаг 96)
	void QuestIconCheckTick();                              // `CQuestLog_RefreshSlotIcons` по пакету: флаг 96 при выполненной цели
	FDelegateHandle HWorldReady, HSelfExp, HQuestStep, HQuestTurnIn, HQuestSync, HQuestProg;   // подписки на сеть
	bool bQuestIconCheck = false;                           // пришёл пакет задания — проверить цели в следующем тике

	// ─────────────── Гид 1950 ───────────────
	void GuideTick(float Dt);                 // `uiTutorialWindowUpdate@0xEBBD40`
	void GuideEnterStep();                    // ветка «шаг сменился» (+8 != +4)
	void GuideAdvance();                      // засчёт шага: кнопка 1952 скрыта, эффект+звук, шаг+1
	void GuideAdvanceFx();                    // `Player_SpawnRareBoostEffect`: удача на кости + звук (раз за шаг)
	void GuideOnOk();                         // 1952 (`boosterShopOnMessage_D020`)
	void GuideOnNo();                         // 1953 → флаг 73
	void GuideOnClose();                      // 1955 → закрыть
	bool EnsureGuideWidget();                 // окно + разбор контролов
	void GuideSetButtons(bool bOk, bool bNo, int32 OkLoc);   // видимость и подпись 1952/1953
	UFUNCTION() void OnGuideOkClicked();      // UButton 1952 `Tuto4_OKBtn`
	UFUNCTION() void OnGuideNoClicked();      // UButton 1953 `Tuto4_CanBtn`
	UFUNCTION() void OnGuideCloseClicked();   // UButton 1955 `Tuto_Msg_Box_4_BTN00` (шаг S2 импорта)

	bool bGuideOn = false;                    // `[0]` — окно гида открыто
	int32 GuideStep = 1;                      // `+4` шаг 1…22
	int32 GuideShownStep = 0;                 // `+8` шаг, для которого собраны текст и картинка
	uint8 GuideFlags[100] = {};               // байты буфера 0…99 (40…99 — флаги событий)
	FVector2D GuideStartPos = FVector2D::ZeroVector;   // `+48` позиция игрока на смене шага (игровые X/Z)
	FString GuideText;                        // `+172` полный текст шага
	int32 GuideTextLen = 0;                   // `+1196` длина (символы)
	int32 GuideTextShown = 0;                 // `+1200` напечатано символов
	double GuideTypeLastMs = 0.0;             // `g_dwAutoActionLastTick` — шаг печати 30 мс
	bool bGuideTextRevealed = false;          // `g_bTutorialTextRevealed`
	bool bGuidePending = false;               // `g_bAutoActionPending` — условие выполнено, ждём 1 с
	double GuidePendingMs = 0.0;              // `g_dwAutoActionTime`
	bool bGuideFire = false;                  // `g_bAutoActionFire`
	bool bGuideFx = true;                     // `+1206` — эффект удачи разрешён
	bool bRmbAnchorSet = false;               // `g_dwTutorialInitFlags & 1`
	FVector2D RmbAnchor = FVector2D::ZeroVector;   // `g_vCursorAnchorX` — точка начала протяжки ПКМ
	FAsdaMouseFrame GuideMouse;               // опрос кнопок мыши (флаги 44/45)
	UPROPERTY(Transient) TObjectPtr<UUserWidget> GuideWin = nullptr;   // WBP_Tuto_Msg_Box_4
	UPROPERTY(Transient) FAsdaFmtText GuideTxtFmt;   // 1951 `Tuto4_MsgTxt` (узел виден сборщику мусора)
	TWeakObjectPtr<UButton> GuideOkBtn, GuideNoBtn, GuideCloseBtn;   // 1952 / 1953 / 1955
	TWeakObjectPtr<UTextBlock> GuideOkTxt, GuideNoTxt;               // подписи 1952 / 1953
	TWeakObjectPtr<UImage> GuideImg;          // 1954 `Tuto4_Img` (план `Tuto4_Img_5_0`)
	TWeakObjectPtr<UUserWidget> GuideBound;   // окно, к которому привязаны контролы выше (новое окно — разобрать заново)

	// ─────────────── Книга 69200 и «?» 69800 ───────────────
	bool LoadBook();                          // `DT_Tutorial` в память
	bool CheckRequirements(int32 Index) const;     // `entry_CheckRequirements@0xB798B0`
	bool EvalPredicate(int32 Type, int32 A, int32 B) const;   // 17 предикатов `CTutorial_Init`
	bool SelectFirstValidSection();           // `uiSelectFirstValidTab@0xB79370`
	void CycleTick();                         // `uiCycleToNextValidTab@0xB79420`
	void ShowNotice(bool bOn);                // 69800
	void OpenBook();                          // клик по 69801 → `tutorialPanelShowAndRefresh@0xB792A0`
	void RefreshPage();                       // `TutorialUI_RefreshPage@0xB78460`
	void HideBookAndMark(const TCHAR* Why, bool bSave = true);   // `uiPanelHideAndResetTimer@0xB79310`; bSave — записать отметку в файл
	void SetSectionBit(int32 Index, bool bOn);   // `bitmask_setClearBit@0xB7AC20`
	bool SendSectionDone(int32 Index, const TCHAR* Why);   // `netSendSlotCmd_6184@0xB7AE80`
	void BookMouseTick();                     // события 8/0xB строк ответа, клик по «?»
	bool EnsureBookWidgets();                 // окна 69800 и 69200 + разбор контролов
	UFUNCTION() void OnBookCloseClicked();    // UButton 69299 `Tutorial_XX`

	UPROPERTY(Transient) TObjectPtr<UDataTable> BookTable = nullptr;   // DT_Tutorial (сильная ссылка на сессию)
	TArray<FAsdaTutorialRow> Sections;        // записи по порядку (индекс = бит маски)
	bool bBookTableMissing = false;           // таблицы нет — не искать её каждые 3 с
	uint8 Mask[32] = {};                      // `this+216` — маска пройденных разделов
	bool bMaskKnown = false;                  // 6185 пришёл в этой сессии (журнал/самотест; опрос «?» его не ждёт — как эталон)
	bool bBookOpen = false;                   // `*this` — полоса книги открыта
	bool bNoticeShown = false;                // 69800 видим
	int32 BookCategory = -1;                  // `this[49]` текущий раздел
	int32 BookPage = 0;                       // `this[50]` страница
	int32 BookHover = -1;                     // `this[51]` подсвеченный ответ
	double BookTimerMs = 0.0;                 // `this[52]` — отметка таймера 3 с
	int32 BookHoverCtl = -1;                  // контрол под курсором прошлого кадра (для события 8)
	int32 BookPressCtl = -1;                  // контрол, на котором нажата ЛКМ (клик = отпускание на нём же)
	FAsdaMouseFrame BookMouse;                // опрос мыши книги и «?»
	UPROPERTY(Transient) TObjectPtr<UUserWidget> NoticeWin = nullptr;   // WBP_Tutorial_Notice_WIN
	UPROPERTY(Transient) TObjectPtr<UUserWidget> BookWin = nullptr;     // WBP_Tutorial_WIN
	TWeakObjectPtr<UWidget> NoticeClick;      // 69801 `Tutorial_Notice_click_4_0`
	UPROPERTY(Transient) FAsdaFmtText BookTitle;      // 69204
	UPROPERTY(Transient) FAsdaFmtText BookDesc;       // 69205
	UPROPERTY(Transient) FAsdaFmtText BookAnswer[3];  // 69206…69208
	TWeakObjectPtr<UWidget> BookAnswerHit[3]; // зоны `Tutorial_TT<n>_<k>_HIT`
	TWeakObjectPtr<UWidget> BookHighlight;    // 69216 `Tutorial_LIST_T_Check`
	TWeakObjectPtr<UImage> BookImgLeft, BookImgMid, BookImgRight;   // 69201 / 69202 / 69203
	TWeakObjectPtr<UButton> BookCloseBtn;     // 69299 `Tutorial_XX`
	TWeakObjectPtr<UTextBlock> BookCloseTxt;  // его подпись (loc 10035)
	TWeakObjectPtr<UUserWidget> NoticeBound, BookBound;   // окна, к которым привязаны контролы выше
};
