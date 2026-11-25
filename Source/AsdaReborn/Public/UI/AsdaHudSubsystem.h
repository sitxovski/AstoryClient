// Copyright (c) 2026 @sitxovski (Discord)
// UAsdaHudSubsystem — HUD игрока, ЭТАП 1: три окна поверх импортированных WBP (Player_UI/HUD).
//
// ═══════════════════════════════ ЭТАЛОН (сборка IDB, RE/etalon) ════════════════════════════════
// 1) EXPBar_WIn (JUI id 310) — полоса опыта во всю ширину низа экрана (база 1024×768, y=758..768):
//    • MINISTATE_LEVELBAR (id 2, скин EXP_Green_Bar: ES0=белая подложка, ES1=зелёная заливка
//      FF87D92C) — кэшируется HUD_Main_Init (CHUD+0x230 pLevelBar), диапазон 0..100
//      (CProgressBar_SetRange, ui_core.c:74898), авто-подгон ширины под экран (75284-75290).
//    • ФОРМУЛА (HUD_UpdateExpBar@0xE792C0, ui_core.c:75252-75278; = statBarUpdatePercent@0xF09640):
//        next = CumExp[level]   (таблица charexp, 150 уровней — НЕ smcharexp!)
//        base = CumExp[level-1];  Span = next − base
//        pct  = (Span − (next − Exp)) / Span · 100 = (Exp − base)/Span · 100
//    • MINISTATE_EXPTEXT (id 35) — текст процента "%.2lf %%", дробь УСЕКАЕТСЯ (не округляется):
//      (int)(pct·100)/100 (uiBar_SetPercent_Slot35@0xB936C0, ui_core.c:11141-11142).
//    • QUICK_EXPCount0..9 (id 390..399) — атлас-цифры процента над квикбаром
//      (hudRenderNumberDigits@ui_core.c:69470): оригинал гасит все глифы и показывает клоны цифр
//      на X-оффсетах {3,9,18,24} от родителя (id 35): десятки (скрыты при 0), единицы,
//      десятые, сотые; точка (QUICK_EXPDot) и «%» (QUICK_EXPPercent) — статичные глифы.
//      В JUI id 394 — ОПЕЧАТКА: idc «QUICK_EXPCount3» при скине SmNum_Bold_White4 → цифры
//      сопоставляются по ПОРЯДКУ узлов (1,2,3,4,5,6,7,8,9,0), не по имени.
//    • ИСТОЧНИКИ EXP (все → UI_UpdateExpBar):
//        4055 netHandler_EnterWorld  — exp i64 @body+35 (вход/респаун; HP@+29, MP@+33)
//        4056 GameNet_OnLocalPlayerMapEntry — exp i64 @body+12 (первый вход; HP@+6 dword, MP@+10 i16)
//        4038 netHandleHpExpUpdate   — тело 19 б: flag u8@0, exp i64@+1, прибавка **i64**@+9,
//                                      источник i16@+17 (−1 = над собой, ≥0 = renderId сущности).
//                                      ★ ИСПРАВЛЕНО 2026-08-22: было «flag=1 = показ прибавки,
//                                      прибавка i32@+9» — НЕВЕРНО. Прибавка показывается во ВСЕХ
//                                      трёх ветках эталона; flag=1 лишь выбирает ветку «наградной
//                                      опыт» (над собой + доп. строка чата 2316+2317). Полный
//                                      разбор показа — `Reports/exp_gain_ui.md`.
//        4036 Recv_LevelUp           — userIdx i16@0, level u8@2, exp i64@+3, скилл-очки i16@+11;
//                                      звук S_c_levelup.wav + эффект lvup01 + баннер (network.c:42299+)
//      Сид до первого пакета: запись персонажа char-select (0x3FF level / 0x3EE exp i64).
//    • CHAT_NEW_SERV_NOTICE (id 2551) — контрол ПОДСИСТЕМЫ ЧАТА (CChat_Init chat.c:1037), в макете
//      этого окна лишь размещён; здесь не трогаем (порт чата — отдельный этап).
//
// 2) NOTICE_WIN (id 228, скин Notice_Tip) — бар СЕРВЕРНОГО УВЕДОМЛЕНИЯ, 3 строки текста
//    (NOTICE_TXT00=229 середина y107, TXT01=230 верх y99, TXT02=231 низ y116).
//    • Драйвер: опкод 3113 (network.c:2831-2833, только g_GameState==1) →
//      uiNoticeUpdate3Line_BC90@0xBABC90 → ...Begin@0xBABE50 → uiNoticeUpdate3Line@0xBAC300.
//    • Тело пакета (payload = pkt+9, 107 байт): word@+2 = РЕЖИМ, текст ASCIIZ @+4 (≤95),
//      цвет R,G,B @+100..102 (клиент достраивает альфу FF). Режимы (до-реверс, круг 2):
//        0   → строка 3-строчного бара NOTICE_WIN;
//        1   → ЛИПКАЯ строка CHAT_NEW_SERV_NOTICE (id 2551, контрол a1+68 в макете EXPBar_WIn):
//              uiNoticeUpdate3Line ветка HIWORD==1 (SetText+SetField923(RGB)+SetVisible 1),
//              жизнь 15 000 мс — uiNoticeTimeoutTick@0xBAD090 (флаг мигания на 12 000 мс),
//              Begin ветка HIWORD==1 сбрасывает прежний текст (буфер +2356). ЭТО НЕ «убрать всё»;
//        100 → текст = atoi(текст) в loc 10221 «Битва начнется через %d мин.», затем режим 0
//              (BC90 сбрасывает mode-word в 0).
//      «Очистки всех строк» в эталоне НЕТ — строки уходят только по своему TTL.
//    • Текст несёт inline-теги (<FC:AARRGGBB>..</F> — наш ..notice так и шлёт): рендер JUI парсит
//      их НАТИВНО (textParseColorTag; разделитель ':'/'=' не проверяется). Базовый цвет строки =
//      RGB пакета (SetField923), теги перекрашивают свои спаны, </F> сброс к базовому.
//      Порт: FAsdaMarkup/UAsdaRichText::ApplyFormatted; база из пакета = обёртка <FC=FFRRGGBB>
//      + замена </F> на </F><FC=FFRRGGBB> (реставрация базового после сброса).
//    • Поведение строк режима 0: каждая живёт 10 000 мс (uiTimedNoticeDialog_tick@0xBACB80),
//      новая сдвигает старые; 1 строка → TXT00 (центр), 2 → TXT01(старая)+TXT02(новая).
//      Инициализируется скрытым (CChat_Init chat.c:420-450, SetVisible(0)).
//    • Наш сервер шлёт 3113 командой ..notice/..noticeoff (GM-гейт, C→S опкод 6030).
//
// 3) HERO_NOTICE_WIN (id 83400, heroMainWin CChat+0x67C) — всплывающий тайм-нотис, единственный
//    текст NOTICE_TXT00_C (id 83401, heroText CChat+0x680).
//    • Показ: uiShowTimedNoticeText@0xBA9AB0 (ui_core.c:12086): если окно видно — строка в ОЧЕРЕДЬ;
//      иначе показать, objStartTimedAction(текст, 255, 0, 10000) = жизнь 10 с, цвет = параметр,
//      ширина панели = ширина текста + 10 (uiPanel9SliceLayout).
//    • Клик по нотису (обработчик события 0x18, ui_core.c:12408-12425, ctrlId 83401/83402):
//      скрыть текущий; очередь не пуста → показать следующий (снова 10 с, зелёный).
//    • Триггеры в эталоне (оба зелёные -16711936, 10 с): 6131 packetShowItemAcquiredNotice →
//      Recv_ItemFound@0x9744B0 «[ss1] получает предмет: [ss2]» (loc 20250, грейд ≥3, не свой);
//      CItemManager_NotifyItemInfo (item_inventory.c:39569) — свой редкий подбор (тот же loc 20250).
//      ★ КРУГ 5: лут-нотисы отсюда УБРАНЫ. Эталон шлёт все строки подбора (свои и отказы) в
//      CChat_AddTokenizedLines → системный чат CHAT_NEW_SYSTEM + оверлей A/B (UAsdaChatSubsystem).
//      HERO_NOTICE остаётся ТОЛЬКО за событием про ДРУГОГО игрока (6131, грейд ≥3).
//
// ЗАКОНЫ: окна = ИМПОРТИРОВАННЫЕ WBP (no-invented-ui, редактируемы в UE); заливка бара = клип
//   ширины слота ES1 (ui-progressbar-animate-existing); цвета из пакетов sRGB→linear
//   (ui-srgb-gamma-rule, FLinearColor::FromSRGBColor); текст только из DT_GameText.
#pragma once

#include "CoreMinimal.h"
#include "AsdaTickGate.h"   // ★ R4-b (14.11.2026): шаблон класса (CDO) не тикает
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "AsdaSessionReset.h"   // IAsdaSessionResettable — сброс состояния сессии
#include "AsdaHudSubsystem.generated.h"

class UUserWidget;
class UImage;
class UTextBlock;
class UButton;
class UCanvasPanel;

UCLASS()
class ASDAREBORN_API UAsdaHudSubsystem : public UGameInstanceSubsystem, public FTickableGameObject,
	public IAsdaSessionResettable
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// ── IAsdaSessionResettable (К-3) ──
	// Находка №8 отчёта 11_hud (уровень повышен ревизией до ВАЖНО): LevelUpBanner
	// не обнулялся нигде, ResetWidgets его не трогает. После ЛЮБОГО травела ветка
	// if (!LevelUpBanner) больше не срабатывает, а отрицательное T не даёт истечь показу —
	// баннеры уровня и звания не показываются вообще.
	virtual void ResetSessionState(EAsdaSessionResetReason Reason) override;

	// FTickableGameObject — самовосстановление окон (после CloseAll/травела) + таймеры строк.
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UAsdaHudSubsystem, STATGROUP_Tickables); }
	// ★ R4-b (14.11.2026): шаблон класса (CDO) не тикает — раньше `Tick` звался дважды за кадр, у рабочего объекта и у шаблона
	//   (ключ `asda.Perf.NoCdoTick`, разбор — `AsdaTickGate.h`). Рабочий объект тикает как раньше.
	virtual bool IsTickable() const override { return AsdaTickGate::NotCdo(this); }
	virtual bool IsTickableInEditor() const override { return false; }
	virtual bool IsTickableWhenPaused() const override { return false; }

	// ── HERO_NOTICE: показать тайм-нотис (эталон uiShowTimedNoticeText: занято → в очередь) ──
	//   Эталонный цвет вызовов = зелёный -16711936 (0xFF00FF00), время жизни 10 000 мс.
	UFUNCTION(BlueprintCallable, Category="Asda|HUD")
	void ShowHeroNotice(const FString& Text, FLinearColor Color, float LifeSec = 10.f);

	// ★ 1:1 c эталоном CItemManager_NotifyItemInfo@0xD0C8F0 (item_inventory.c:39497) — ЕДИНАЯ точка
	//   «я получил предмет». Условие анонса: грейд >= 3 И (ItemType <= 9 ИЛИ (ItemType==11 && ItemSubType==200)).
	//   При выполнении: локальный HERO_NOTICE со СВОИМ именем (loc 20250) + C→S 6616 (сервер разошлёт 6131
	//   всем остальным). Порога «+N» у заточки в оригинале НЕТ — критерий один для всех четырёх точек вызова.
	//   Точки вызова оригинала: подбор (ui_core.c:47110), гача-коробка (ui_core.c:7689),
	//   успешная заточка (item_enhance.c:10694), магазин (shop_trade.c:12408).
	//   bForce=true — послать 6616 МИНУЯ фильтр (только для отладки серверного тракта, asda.Item.Notify <id> 1).
	// ★★★ Эталон принимает не itemId, а ЗАПИСЬ предмета, и берёт из неё контейнер (`запись+4`) и
	//   слот (`запись+5`, word) — они нужны ТРЕТЬЕМУ пакету ветки: `netSendTsMaskedReq6129(self,
	//   userIdx, −1, слот, контейнер)` = «похвастаться» всем вокруг (item_inventory.c:39572-39575).
	//   Порт долго слал только 20250 и 6616. Передавай контейнер/слот, если они известны;
	//   −1/−1 — вызов без них (тогда 6129 не уходит, о чём пишется в лог).
	UFUNCTION(BlueprintCallable, Category="Asda|HUD")
	void NotifyItemInfo(int32 ItemId, bool bForce = false, int32 ContainerType = -1, int32 SlotIndex = -1);

	// ── NOTICE_WIN: серверное уведомление (3113). Mode 0=строка бара, 1=липкая строка 2551 (15 с),
	//    100=loc 10221→режим 0; прочие режимы = игнор (эталон веток не имеет). Текст сырой (inline-теги),
	//    базовый цвет = RGB пакета; RGB(0,0,0) = «не задан» → БЕЛЫЙ (кампвар: подтверждение юзера,
	//    контроллер шлёт нули; клиент-код сентинела не имеет — скин-дефолт FF000000 нечитабелен).
	void ShowServerNotice(int32 Mode, const FString& Text, const FColor& Rgb);
	// ★ 10.10.2026 (МЕЛК-1, `owner_1010/overlaps_fixes.md`): только для `asda.Hud.TestServerNotice` офлайн — поднять окна HUD
	//   (вне мира их не создаёт Tick) и вернуть строку «видимые строки объявления: TXT00/TXT01/TXT02 = текст» для журнала.
	FString DebugNoticeState();

	// ★★★ РАЗВОРАЧИВАНИЕ ТЕЛА 3113 В ГОТОВУЮ СТРОКУ — обязано выполняться ДО МАРШРУТИЗАЦИИ.
	//   Эталон uiNoticeUpdate3Line_BC90@0xBABC90 (ui_core.c:12637-12656) делает ровно три шага
	//   ИМЕННО В ЭТОМ ПОРЯДКЕ:
	//     1) if (mode == 100) { v6 = atoi(буфер+1888); sprintf(буфер+1888, loc 10221, v6); mode = 0; }
	//     2) uiChatLogAddLine(..., буфер+1888, −65536, …, 255)   ← дубль в чат УЖЕ ГОТОВОЙ строкой
	//     3) uiNoticeUpdate3LineBegin(...)                        ← показ бара
	//   ⇒ в режиме 100 тело пакета несёт НЕ ТЕКСТ, А АРГУМЕНТ (число минут, «5»), и подстановка
	//   в loc 10221 «Битва начнется через %d мин.» происходит РАНЬШЕ обоих потребителей.
	//   ⚠ Если развернуть аргумент позже (внутри ShowServerNotice), в ленту чата уедет голое «5» —
	//   ровно этот дефект юзер и увидел 2026-08-06. Возвращает готовый текст; для режимов ≠ 100
	//   отдаёт вход без изменений.
	UFUNCTION(BlueprintCallable, Category="Asda|HUD")
	FString ResolveNoticeText(int32 Mode, const FString& RawText) const;

	// Режим, с которым строка идёт дальше после ResolveNoticeText: 100 → 0 (эталон обнуляет
	//   mode-word после подстановки, дальше строка обрабатывается как обычная строка бара).
	static int32 ResolvedNoticeMode(int32 Mode) { return (Mode == 100) ? 0 : Mode; }

	// Принудительно обновить EXP-бар из текущих данных (уровень/опыт NetSubsystem или char-записи).
	UFUNCTION(BlueprintCallable, Category="Asda|HUD")
	void RefreshExpBar();

	// Диагностика EXP одной строкой: level/exp/пороги CumExp/процент (для asda.Hud.TestExp и логов).
	FString DebugExpState() const;

	// ★★★ ПОКАЗ ПРИБАВКИ ОПЫТА — порт `netHandleHpExpUpdate@0xF427A0` (разбор: `exp_gain_ui.md`).
	//   Всплывающая надпись идёт через УЖЕ СУЩЕСТВУЮЩИЙ канал `UAsdaFloatingTextSubsystem`
	//   (тот же, что цифры урона), строки чата — через `UAsdaMessageSubsystem` (категория Progress).
	//   Своей системы показа текста здесь НЕ заводится.
	//   `bFullExp` — байт@0 пакета (наградной опыт), `SrcRenderId` — i16@+17 (−1 = над собой).
	//   Публично ради консольной проверки `asda.Hud.TestExpGain` без сервера.
	void ShowExpGain(int64 Gained, bool bFullExp, int32 SrcRenderId);

	// ★ Доля опыта текущего уровня (0..1) по той же формуле `HUD_UpdateExpBar@0xE792C0`. ОДНА точка
	//   расчёта на порт: окно характеристик (`STATUS_PEXP`/`STATUS_TEXP`, 916/915) берёт число
	//   ОТСЮДА — эталон там тоже не считает сам, а получает его от HUD (`uiBar_SetPercent_Slot35`).
	//   −1 = таблица порогов или уровень ещё недоступны (эталон в этом случае полосу не трогает).
	float GetExpFraction() const;

private:
	// ── Жизненный цикл окон ──
	bool EnsureWidgets();                  // создать/найти три окна (идемпотентно, после травела заново)
	void ResetWidgets();                   // сброс ссылок (окна умерли с миром)

	// ── EXP ──
	void OnSelfExp(int32 Level, int64 Exp, int64 Gained, bool bFullExp, int32 SrcRenderId);
public:
	// ★ Показ повышения уровня. Публичный: его же гоняет тестовая команда `asda.Test.LevelUp`,
	//   чтобы проверять показ, не дожидаясь настоящего уровня.
	void OnSelfLevelUp();

	// ★★★ БАННЕР «ЗВАНИЕ ПОЛУЧЕНО» — тот же `WBP_Con_Banner2`, но НАДПИСЬ ДРУГАЯ.
	//   Эталон `uiTitle_OnJobChangeResult@0xE9A9F0` (ui_core.c:82339) при получении звания зовёт
	//   `changeJobBannerShow(g_pChangeJobBanner, 2, 500, 500, 500)`. Тип 2 в `changeJobBannerShow`
	//   (ui_core.c:18035-18040) кладёт в `gap_F4` пятёрку, а тип 1 — шестёрку: это и есть выбор
	//   надписи `Con_Banner2_GetTitle` вместо `Con_Banner2_LevelUp`.
	//   ⚠ ГЕЙТ ЭТАЛОНА (не потерять): баннер показывается ТОЛЬКО когда (а) пакет про СВОЕГО
	//     персонажа, (б) звание ПОЛУЧЕНО (флаг 0, не «потеряно») и (в) окно званий ЗАКРЫТО —
	//     при открытом окне эталон вместо баннера перерисовывает список.
	//   ⚠ Длительность показа у типа 2 — 500 мс (у уровня 2000): первый аргумент-после-типа.
	void ShowTitleBanner();
	// ★ 06.10.2026 (волна 2-В): признак `CChangeJobBanner::showActive` (+0x104) — `WBP_Con_Banner2` сейчас на экране.
	//   Его читает баннер смены профессии 18500: `changeJobBannerStart` не стартует, пока идёт показ (ui_core.c:17931-17934).
	bool IsConBanner2Active() const { return LevelUpBannerActive; }
private:

	// ★ Общая точка баннера `WBP_Con_Banner2` (эталон `changeJobBannerShow`).
	//   `BannerType`: 1 = повышение уровня (`Con_Banner2_LevelUp`), 2 = получение звания
	//   (`Con_Banner2_GetTitle`). `HoldSeconds` — время показа между въездом и угасанием
	//   (эталонный первый аргумент-после-типа / 1000: уровень 2000 → 2.0 с, звание 500 → 0.5 с).
	void ShowBanner(int32 BannerType, float HoldSeconds);
	void TickLevelUpBanner(float DeltaSeconds);
	UPROPERTY(Transient) TObjectPtr<UUserWidget> LevelUpBanner = nullptr;
	double LevelUpBannerStartS = 0.0;
	float  LevelUpBannerHoldS  = 2.0f;                                    // показ: 2.0 с уровень / 0.5 с звание
	bool   LevelUpBannerActive = false;                                   // 4036 self: звук S_c_levelup
	bool ResolveLevelExp(int32& OutLevel, int64& OutExp) const;   // Net.SelfLevel/SelfExp или char-запись
	// pct 0..100 по таблице charexp (формула HUD_UpdateExpBar; false = таблица/уровень недоступны).
	bool ComputeExpPct(int32 Level, int64 Exp, double& OutPct) const;
public:
	// ★ Накопленный опыт строки charexp как int64. В charexp.bin по +8 лежит int64, а
	//   сгенерированная строка DataTable делит его на два int32: CumExp (младшее слово)
	//   и Zero2 (старшее). С 64-го уровня младшее слово отрицательное, с 65-го старшее
	//   ненулевое — читать ТОЛЬКО через эту функцию (общая точка HUD / окно персонажа /
	//   выбор персонажа). Эталон: sub_EC1850 отдаёт QWORD с +8 записи.
	static int64 CharexpCumExp(const struct FAsdaCharexpRow& Row);
private:
	void ApplyExpPct(double Pct);          // клип заливки ES1 + текст "%.2f %%" + атлас-цифры
	void SetupExpControls();               // найти заливку/текст/глифы, снять fullW, создать клоны цифр
	void UpdateExpDigits(double Pct);      // hudRenderNumberDigits: 4 клона на оффсетах {3,9,18,24}

	// ── NOTICE_WIN ──
	struct FAsdaNoticeLine
	{
		FString Text;                      // СЫРОЙ текст (с inline-тегами) — формат на показе
		FColor  Rgb = FColor::White;       // базовый цвет строки из пакета (@+100..102)
		double  Born = 0.0;                // FPlatformTime::Seconds показа (жизнь 10 с; монотонные часы — волна В6)
	};
	TArray<FAsdaNoticeLine> NoticeLines;   // [0] = старшая; максимум 2 (★ 10.10.2026: третье объявление выбрасывает старшую, как эталон)
	void RefreshNoticeLines();             // раскладка по TXT00/01/02 (1→центр TXT00, 2→верх TXT01 + низ TXT02; TXT00 с ними не бывает)
	void TickNoticeExpiry();               // снятие строк по 10-секундному таймеру
	// Липкая строка CHAT_NEW_SERV_NOTICE (mode 1): показ 15 000 мс (uiNoticeTimeoutTick@0xBAD090).
	void ShowServSticky(const FString& Text, const FColor& Rgb);
	double ServStickyShownAt = -1.0;       // < 0 = скрыта
	// Сырой текст + базовый RGB пакета → форматированный показ в контроле (ApplyFormatted; база цвета
	//   = обёртка <FC=FFRRGGBB> + реставрация после каждого </F>). Возвращает актуальный виджет.
	// Форматированный показ нотиса: базовый цвет пакета + СТИЛЬ КОНТРОЛА и inline-теги текста.
	//   ★ `StyleTag` — эталонное поле 926 этого контрола, тегом разметки: «<FB>» = 0x200000 (жирный),
	//   «<FO>» = dword_800000 (обводка), «» = поле 926 не ставится. Раньше обводка вешалась ВСЕМ
	//   контролам по ОТМЕНЁННОМУ прочтению `TextOutLine` (см. К1 в `code_dedup_audit_2.md`).
	class UWidget* ApplyNoticeText(class UWidget* Ctrl, const FString& RawText, const FColor& Rgb,
		const TCHAR* StyleTag);

	// ── HERO_NOTICE ──
	struct FAsdaHeroNotice { FString Text; FLinearColor Color; float LifeSec = 10.f; };
	TArray<FAsdaHeroNotice> HeroQueue;     // очередь (эталон pThis+418)
	double HeroShownAt = -1.0;             // < 0 = не показан
	float  HeroLife = 10.f;
	void PresentHeroNotice(const FAsdaHeroNotice& N);
	void DismissHeroNotice();              // клик/таймаут: скрыть, показать следующий из очереди
	UFUNCTION()
	void OnHeroClicked();                  // клик-перехватчик (эталон событие 0x18 по 83401)
public:
	// 6131 «игрок получил/улучшил предмет». Публичный — его дёргает консольная проверка
	//   asda.Hero.TestAnnounce <itemId> [процент] [тег] [имя] (см. AsdaHudSubsystem.cpp).
	void OnItemAcquired(const FString& CharName, int32 ItemId, int32 Src, int32 RefineLevel, int32 RefinePercent);
private:

	// ── Виджеты (GC-руты) ──
	UPROPERTY(Transient) TObjectPtr<UUserWidget> ExpBarWidget;
	UPROPERTY(Transient) TObjectPtr<UUserWidget> NoticeWidget;
	UPROPERTY(Transient) TObjectPtr<UUserWidget> HeroWidget;

	// EXPBar_WIn: контролы
	UPROPERTY(Transient) TObjectPtr<UImage>     ExpFill;        // MINISTATE_LEVELBAR план ES1 (зелёная заливка)
	// ★ КРУГ 3: текст «%.2f %%» в HUD НЕ выводится — эталон uiBar_SetPercent_Slot35 пишет его в окно
	//   CHARAINFO (this=&g_pCharInfoWnd), а в HUD-баре процент показывают ТОЛЬКО атлас-цифры.
	//   TextBlock MINISTATE_EXPTEXT (caption скина) гасится, остаётся его чёрная подложка Black_Ot125.
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>> ExpDigitClones;   // 4 клона цифр (десятки/единицы/десятые/сотые)
	FSlateBrush DigitBrush[10];            // брашы глифов 0..9 (из QUICK_EXPCount по ПОРЯДКУ узлов)
	bool  bDigitBrushValid[10] = {};
	// ★ КРУГ 3: ПОЛНЫЙ анкор заливки хранится с момента Setup (Min и Max). Прежний код читал span из
	//   ТЕКУЩИХ анкоров: первый же ApplyExpPct(0) схлопывал Max=Min, и заливка навсегда оставалась
	//   нулевой ширины («зелёной полосы нет вовсе»).
	float ExpFillAnchorMinX = 0.f;         // левый край полного бара (анкор подложки ES0)
	float ExpFillAnchorMaxX = 1.f;         // правый край полного бара
	bool  bExpFillSpan = true;             // true = клип анкорами; false = клип offsets-шириной
	float ExpFillFullW = 0.f;              // полная ширина offsets (фолбэк point-анкора)
	bool  bExpControlsReady = false;
	double LastExpPct = -1.0;              // последний применённый процент (лог/повтор)
public:
	// Рантайм-дамп контролов EXP-бара (видимость/анкоры/размеры заливки, клоны цифр, группа процента)
	//   — доказательная строка для asda.Hud.TestExp (закон full-structural-analysis).
	void DumpExpWidgets() const;
private:

	// NOTICE_WIN: контролы (UWidget: после первого ApplyFormatted TextBlock подменяется RichTextBlock)
	UPROPERTY(Transient) TObjectPtr<UWidget> NoticeTxt[3];      // [0]=TXT00 центр, [1]=TXT01 верх, [2]=TXT02 низ
	// Липкая строка серверного нотиса (id 2551, живёт в WBP_EXPBar_WIn; mode 1, 15 с)
	UPROPERTY(Transient) TObjectPtr<UWidget> ServNoticeText;

	// HERO_NOTICE_WIN: контролы
	UPROPERTY(Transient) TObjectPtr<UWidget>    HeroText;       // NOTICE_TXT00_C (rich после первого показа)
	UPROPERTY(Transient) TObjectPtr<UButton>    HeroClick;      // прозрачный клик-перехватчик поверх окна

	// Делегаты NetSubsystem
	FDelegateHandle HSelfExp, HLevelUp, HServerNotice, HItemAcquired;

	// z-order: EXP-бар ПОД игровыми окнами (постоянный слой), нотисы — над ними.
	static constexpr int32 kExpBarZ = 90;
	static constexpr int32 kNoticeZ = 700;
	static constexpr int32 kHeroZ   = 710;
	static constexpr double kNoticeLineLifeS = 10.0;   // эталон: 10 000 мс на строку (обе системы)
};
