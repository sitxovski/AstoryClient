// Copyright (c) 2026 @sitxovski (Discord)
// UAsdaChatSubsystem — СИСТЕМНЫЕ СООБЩЕНИЯ клиента (окно CHAT_NEW_SYSTEM + оверлеи A/B).
//
// ═══════════════════════════════ ЭТАЛОН (сборка IDB, RE/etalon) ════════════════════════════════
// ЕДИНАЯ точка входа всех системных строк клиента — `CChat_AddTokenizedLines @0xBA9C80`
// (chat.c:3285), сигнатура (CChat*, const char* text, int color, int lifeMs, int track):
//     tokens = textTokenizer_SplitStringIntoRows(text, chatLineMaxWidth)   // перенос по ширине списка
//     для каждой строки:
//         uiChatLogAddLine(chatSystemListBox, line, color, 0, 0.0f,   ...)   // ВСЕГДА, TTL 0 = вечно
//         track==1 ? uiChatLogAddLine(sysMsgBWin, line, color, 0, 10000.0f, ...)
//                  : uiChatLogAddLine(sysMsgAWin, line, color, 0, (float)lifeMs, ...)
//     автоскролл вниз, если скроллбар был внизу (scrollAtBottomFlag, chat.c:3317/3333)
// `uiChatLogAddLine @0x12EA470` (ui_core.c:111638): TTL>0 → строке ставится метка
//     timeGetTime()+TTL (поля +863/+864, флаг [3448]) — строка гаснет сама; TTL==0 = вечная.
//     Переполнение m_maxLines → удаляется САМАЯ СТАРАЯ строка.
//
// ОКНА (JUI gamemain, база 1024×768; постройка `CChat_BuildServNoticeUI @0xB9EFB0`, chat.c:962):
//   • CHAT_NEW_SYSTEM (2570) @ (1, 508, 474×70) — ПОСТОЯННЫЙ журнал; SetVisible(1)+listMoveToEnd.
//       – CHAT_NEW_SERV_listbox (2572, скин Inven_List) @ (22, 516, 446×57) — список, ЛИМИТ 500 строк
//         (sub_935C9B(...,500)), scrollOffset 2, флаг 0x400000 (тень), sub_93A24B(...,10).
//         Клик по 2572 НЕ поглощается UI (game_main.c:5748 whitelist: 2572/2521/2557/2558/159/9501)
//         — список прозрачен для игрового клика.
//       – CHAT_NEW_SERV_SYSbottn (2574, TalkBar_3) @ (10, 514, 66×25): строка 0 = loc 12144 «Система»;
//         JUI Visible=0, кода показа нет → остаётся скрытым.
//       – CHAT_NEW_SERV_line0 (2575) @ окно: SetVisible(0) — разделитель, всегда скрыт.
//       – CHAT_NEW_SERV_HomeBtn (2573, MinimizeBtn_WinSm) @ (451, 519, 13×13) — «к последней строке».
//       – CHAT_NEW_SYSTEM_TextMove (2576, NoImage) @ (1, 508, 474×32) — зона перетаскивания окна.
//   • CHAT_NEW_SERV (2550) @ (24, 754, 440×44), скин ChatBase_Center + дети ChatBase_L (0,754,24×44)
//     и ChatBase_R (464,754,22×44) — тёмная плита-подложка чата у САМОГО НИЗА экрана.
//     `CChat_BuildServNoticeUI` делает `CControl_SetVisible(chatServWin, 0)` — окно СКРЫТО ЦЕЛИКОМ.
//     Внутри лежат два накопителя (оба Action_List, ЛИМИТ 5 строк, scrollOffset 2, флаг 0x400000):
//       – CHAT_NEW_SERV_SysMsg_Win   (2557) @ (17, 271, 464×57) — дорожка A (обычный feedback);
//       – CHAT_NEW_SERV_SysMsg_Win_2 (2558) @ (17, 332, 464×57) — дорожка B (значимые события).
//     ★★★ ОБА ТОЖЕ `CControl_SetVisible(..., 0)` (chat.c:1017 и chat.c:1029) — и во ВСЁМ эталоне
//     нет НИ ОДНОГО места, где 2550/2557/2558 включались бы обратно (полный grep по всем 30 файлам:
//     эти id встречаются только в chat.c:1008/1013/1025 при поиске по ID, далее только запись строк
//     в chat.c:3325/3330). `CControl_SetVisible@0x12DFB00` действительно гасит `isVisible`, а рендер
//     контрола этот флаг проверяет. ⇒ **дорожки A/B — НЕВИДИМЫЕ буферы-накопители**: игрок системные
//     сообщения видит ТОЛЬКО в журнале 2572. Порт держит их как ДАННЫЕ (строки + TTL 10 000 мс),
//     виджетов для них НЕ создаёт и окно WBP_CHAT_NEW_SERV НЕ открывает — иначе его плита
//     ChatBase_* рисуется поверх полосы опыта (EXPBar_WIn 310 @ y 758..768). Регрессия круга 5.
//
// ПРАВИЛО МАРШРУТИЗАЦИИ (полная таблица — Docs/UI/HUD/NOTICES_ALL.md):
//   системный чат (2572)  — ВСЕГДА для любого сообщения через CChat_AddTokenizedLines (журнал),
//                           ЕДИНСТВЕННОЕ постоянно читаемое игроком;
//   буфер A (2557)        — track=0: рутинный отклик на действие игрока (≈96 % вызовов эталона), НЕВИДИМ;
//   буфер B (2558)        — track=1: значимые события (свой подбор, редкий дроп, PvP-итоги, анонсы), НЕВИДИМ;
//   WARMESSAGE_WIN (221)  — немедленный отказ/предупреждение по действию (см. блок ниже);
//   HERO_NOTICE_WIN       — глобальное событие про ДРУГОГО игрока (6131, грейд ≥3);
//   NOTICE_WIN (228)      — ТОЛЬКО серверный push 3113 (клиент сам туда не пишет);
//   чат-лог CHAT_BACKWIN  — только сообщения с каналом и отправителем (обычный/шёпот/пати/гильдия).
//
// ═════════════════════════ WARMESSAGE_WIN (JUI id 221) — ПОЛНЫЙ РЕВЕРС ═════════════════════════
// Писатель: `CControl_SetTooltipText@0xBA9A30` (ui_core.c:12069; МИСНОМЕР — фактически
//   CChat::AddWarMessage): uiChatLogAddLine(warMessageWin, Str, color, 0, (float)ttlMs, /*a6=*/1,
//   0, 0, /*alpha=*/255) + CClass_MapInsert(g_pWindowMgr, warMessageWin) (поднять окно наверх).
//   ★ БЕЗ текст-токенайзера: строка НЕ переносится по ширине (в отличие от AddTokenizedLines).
//   ★ a6=1 → флаг строки |=1 (поле row+2804): рендер листбокса x += colWidth/2 − textW/2
//     (ui_core.c:112630-112643) = СТРОКА ЦЕНТРИРУЕТСЯ; бит 2 был бы right-align.
// Окно (gamemain_jui.ini): скин Action_List (единственный план PlaneAlwaysInvisible=1 → ФОНА НЕТ),
//   Size 464×45, AlignX «16 50 28» = центр по X (x=512−232=280), AlignY «32 50 28» =
//   y=768·0.28−45/2=192.5 (верхняя четверть экрана), Visible=1, AlwaysTop=1, NoSelect=1.
// Настройка (CChat_Init chat.c:412-419): лимит sub_935C9B(3) = МАКС 3 СТРОКИ (переполнение
//   вытесняет самую старую), scrollOffset 2, флаг 0x400000 (тень текста), принудительно видимо.
// Жизнь строки: TTL параметром вызова — 10 000 мс (304 сайта), 2000 (4: кампвар-зоны 108124/108125),
//   30 000 (2: перегруз 2358/2359); гаснет сама (метка timeGetTime()+TTL, поля +863/+864).
// 316 колл-сайтов (полный дамп: Reports/warmsg_callsites.tsv): network.c 190, ui_core.c 33,
//   item_inventory.c 24, entity_char.c 17, combat_pvp.c 11, skill_buff.c 11, chat.c 9, прочие 21.
//   Цвета: −11514 золотой FFD306 (229), −65536 красный (40), −256 жёлтый (13), −1 белый (7), редкие
//   единично. ★ ПАРНОСТЬ: только 104 сайта дублируют строку в системный чат (AddTokenizedLines
//   рядом), 212 — ТОЛЬКО WARMESSAGE. Прежняя формулировка «типовой паттерн — парный вызов» неточна:
//   пара характерна для ЗНАЧИМЫХ событий (итог дуэли, редкий дроп, квест), немедленные отказы
//   сервера (движение 4007, бой 4027/4029) идут SOLO. Порт: bAlsoSystemChat у AddWarMessage.
// Источники, ПОДКЛЮЧЁННЫЕ в порту (карта кодов — AsdaWorldSubsystem):
//   4007 result 2/3/6/7/8/9 → loc 1013/1016/1019/2319/10183/10184 (золотой) — отказ движения;
//   4027 case 2/3/5/6/7/8/9/11/12/20 → loc 2320/2204/1074/1082/2036/2038/1300/112117/112118/1032
//     (красный) — отказ взвода атаки (только self, эталон-гейт effectSlots[112].field_40);
//   4029 sub 1/3 → loc 1024/1095 (золотой), sub 4/5/6/7 → 2036/1074/1082/2036 (красный);
//   разрыв game-соединения → loc 1001 (красный, эталон network.c:767/1316).
//
// ЦВЕТА (частоты вызовов эталона): -11514 0xFFFFD306 золотой (основной, 351), -1 белый (223),
//   -75279 0xFFFED9F1 (85), -256 жёлтый (73), -65536 красный (45), -21248 0xFFFFAD00 (22),
//   -7930543 0xFF86FD51 зелёный (свой подбор), -16711936 ярко-зелёный (редкий дроп).
//   ЗАКОН ui-srgb-gamma-rule: цвета JUI/эталона заданы в sRGB → в UE FLinearColor::FromSRGBColor.
//
// ═══════════════ ★★★ КРУГ 7: ЭТИ ФУНКЦИИ — СТОКИ, А НЕ ПРИКЛАДНОЙ API ═══════════════════════════
// AddSystemMessage / AddSystemMessageById / AddWarMessage / AddWarMessageById реализуют ОКНА
//   (эталонные CChat_AddTokenizedLines и CControl_SetTooltipText) и больше НЕ вызываются из
//   прикладного кода напрямую. Всё, что хочет что-то показать игроку, зовёт ЕДИНЫЙ МАРШРУТИЗАТОР
//   `UAsdaMessageSubsystem::Post(Category, …)`, а он по ТАБЛИЦЕ МАРШРУТИЗАЦИИ В ДАННЫХ решает,
//   в какие окна отдать строку (см. Public/UI/AsdaMessageSubsystem.h). Так цвет/TTL/парность
//   живут в ОДНОМ месте и правятся без пересборки.
//
// ЗАКОНЫ: окна = ИМПОРТИРОВАННЫЕ WBP (no-invented-ui, редактируемы в UE-редакторе); текст только из
//   DT_GameText (AsdaTextSubsystem); inline-теги <FC=..>/<FB>/</F> — через UAsdaRichText::ApplyFormatted.
#pragma once

#include "CoreMinimal.h"
#include "AsdaTickGate.h"   // ★ R4-b (14.11.2026): шаблон класса (CDO) не тикает
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "UI/AsdaUiInputTypes.h"   // ★ R1: FAsdaMouseFrame / FAsdaDragArm / FAsdaDragGhost … — поля ввода (без всего AsdaUiHelpers.h)
#include "Layout/Margin.h"         // FMargin
#include "AsdaSessionReset.h"   // IAsdaSessionResettable — сброс состояния сессии
#include "UI/AsdaNoticeColors.h"   // ★ U12 (В1 дедупа №2): эталонные цвета нотисов (ColGold берёт отсюда)
#include "AsdaChatSubsystem.generated.h"

class UUserWidget;
class UScrollBox;
class UWidget;
class UButton;

// Дорожка плавающего оверлея (параметр `track` эталонного CChat_AddTokenizedLines).
UENUM(BlueprintType)
enum class EAsdaSysTrack : uint8
{
	A = 0 UMETA(DisplayName = "A (2557) — рутинный отклик"),
	B = 1 UMETA(DisplayName = "B (2558) — значимое событие"),
};

UCLASS()
class ASDAREBORN_API UAsdaChatSubsystem : public UGameInstanceSubsystem, public FTickableGameObject,
	public IAsdaSessionResettable
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// ── IAsdaSessionResettable (К-3) ──
	// Окно, его защёлка «запрос в полёте» и полученные от сервера данные
	// принадлежат СЕССИИ и переживали разрыв связи вместе с GameInstance.
	virtual void ResetSessionState(EAsdaSessionResetReason Reason) override;

	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UAsdaChatSubsystem, STATGROUP_Tickables); }
	// ★ R4-b (14.11.2026): шаблон класса (CDO) не тикает — раньше `Tick` звался дважды за кадр, у рабочего объекта и у шаблона
	//   (ключ `asda.Perf.NoCdoTick`, разбор — `AsdaTickGate.h`). Рабочий объект тикает как раньше.
	virtual bool IsTickable() const override { return AsdaTickGate::NotCdo(this); }
	virtual bool IsTickableInEditor() const override { return false; }
	virtual bool IsTickableWhenPaused() const override { return false; }

	// ★ ЕДИНАЯ точка системного сообщения = эталонный CChat_AddTokenizedLines.
	//   RawText — сырой текст (может нести inline-теги <FC=..>), Color — базовый цвет строки в sRGB,
	//   Track — буфер-накопитель (A рутинное / B значимое), LifeMs — TTL строки буфера A (эталон 10000).
	//   Строка ВСЕГДА уходит в ВИДИМЫЙ журнал 2572 и дублируется в выбранный НЕВИДИМЫЙ буфер.
	UFUNCTION(BlueprintCallable, Category="Asda|Chat")
	void AddSystemMessage(const FString& RawText, FColor Color,
	                      EAsdaSysTrack Track = EAsdaSysTrack::A, float LifeMs = 10000.f);

	// То же, но текст берётся из DT_GameText по loc-id (Fallback — если строки нет в таблице).
	UFUNCTION(BlueprintCallable, Category="Asda|Chat")
	void AddSystemMessageById(int32 LocId, FColor Color,
	                          EAsdaSysTrack Track = EAsdaSysTrack::A, const FString& Fallback = FString());

#if !UE_BUILD_SHIPPING
	// ★ 12.10.2026 (самотест чата отряда): сколько строк журнала 2572 содержат `Needle` — проверка «нотис появился ИМЕННО
	//   сейчас» (счёт до/после), а не «когда-то раньше в этом процессе». Только чтение.
	int32 DebugCountSystemLines(const FString& Needle) const
	{
		int32 N = 0;
		for (const auto& L : LogHistory) { if (L.Raw.Contains(Needle)) { ++N; } }
		return N;
	}
	// ★ 29.09.2026 (ревизия волны 0 торговли, С-1): цвет ПОСЛЕДНЕЙ строки журнала 2572, содержащей `Needle`, — самотесты
	//   проверяют КАНАЛ нотиса (цвет обработчика эталона), а не только факт строки. false — такой строки нет. Только чтение.
	bool DebugFindLastSystemLineColor(const FString& Needle, FColor& OutColor) const
	{
		for (int32 i = LogHistory.Num() - 1; i >= 0; --i)
		{
			if (LogHistory[i].Raw.Contains(Needle)) { OutColor = LogHistory[i].Color; return true; }
		}
		return false;
	}
	// ★ 06.10.2026 (самотест портрета): есть ли в невидимом буфере A/B (2557/2558) живая строка с `Needle` —
	//   проверка ДОРОЖКИ нотиса (параметр track эталонного CChat_AddTokenizedLines). Только чтение.
	bool DebugBufferHas(EAsdaSysTrack Track, const FString& Needle) const
	{
		for (const FSysBufferLine& L : (Track == EAsdaSysTrack::B) ? LinesB : LinesA)
		{
			if (L.Raw.Contains(Needle)) { return true; }
		}
		return false;
	}
	// ★ 19.10.2026 (самотест строк наград 5042): сколько живых строк плашки 221 содержат `Needle`; цвет последней такой — в
	//   `OutColor` (если есть). Проверка ПРИЁМНИКА эталонного `CControl_SetTooltipText`, а не только журнала. Только чтение.
	// ★ 19.10.2026 (тот же самотест): индекс ПОСЛЕДНЕЙ строки журнала 2572 с `Needle` (−1 — нет) — проверка ПОРЯДКА строк.
	int32 DebugLastSystemLineIndex(const FString& Needle) const
	{
		for (int32 i = LogHistory.Num() - 1; i >= 0; --i) { if (LogHistory[i].Raw.Contains(Needle)) { return i; } }
		return -1;
	}
	int32 DebugCountWarLines(const FString& Needle, FColor* OutColor = nullptr) const
	{
		int32 N = 0;
		for (const FWarLine& L : WarLines)
		{
			if (L.Raw.Contains(Needle)) { ++N; if (OutColor) { *OutColor = L.Color; } }
		}
		return N;
	}
#endif

	// ★ WARMESSAGE_WIN (221) = эталонный CControl_SetTooltipText: строка в верхний 3-строчный
	//   всегда-видимый список (центр X, y≈25 % экрана). БЕЗ переноса строк, центрирование, тень.
	//   bAlsoSystemChat=true воспроизводит ПАРНЫЙ вызов эталона (104 сайта из 316): та же строка
	//   дублируется в журнал 2572 + буфер A. Отказы сервера (движение/бой) — solo, без дубля.
	//   ★ StyleTag — inline-теги стиля строки (`<FO>` чёрная обводка, `<FB>` жирный). Пусто =
	//   эталонный вид канала (тень 0x400000, обводки нет: у `uiChatLogAddLine` стилевого аргумента
	//   не существует). Стиль приходит ОТ ВЫЗЫВАЮЩЕГО ровно так же, как у NOTICE_WIN/HERO_NOTICE
	//   в `UAsdaHudSubsystem::ApplyNoticeText`, — чтобы канал 221 оставался общей точкой показа
	//   «чистых» надписей и не переставал быть эталонным из-за одного особого сообщения.
	UFUNCTION(BlueprintCallable, Category="Asda|Chat")
	void AddWarMessage(const FString& RawText, FColor Color, float LifeMs = 10000.f,
	                   bool bAlsoSystemChat = false, const FString& StyleTag = FString());

	// То же по loc-id из DT_GameText (Fallback — если строки нет в таблице).
	UFUNCTION(BlueprintCallable, Category="Asda|Chat")
	void AddWarMessageById(int32 LocId, FColor Color, float LifeMs = 10000.f,
	                       bool bAlsoSystemChat = false, const FString& Fallback = FString(),
	                       const FString& StyleTag = FString());

	// Диагностика: сколько строк в журнале/буферах + видимость окон (для asda.Chat.Dump).
	FString DebugState() const;
	// Самотест плашки 221 (волна В5): строк в данных; списков строк в окне и строк-виджетов во всех списках окна
	//   (после сброса сессии список обязан остаться ОДИН, а строк-виджетов — не больше, чем строк в данных).
	int32 DebugWarLineCount() const { return WarLines.Num(); }
	void DebugWarWidgetCounts(int32& OutBoxes, int32& OutRows) const;
	// Самотест: гашение строк 221 по TTL — та же функция, что зовёт тик (офлайн тик чата стоит на гейте «в мире»).
	void DebugTickWarExpiry() { TickWarExpiry(); }

	// ★★★ КУРСОР НАД ОКНОМ ЖУРНАЛА? Публично — для AAsdaPlayerController::OnLeftClick.
	//   ПОЧЕМУ ТАК: клик по миру идёт НЕ через Slate, а напрямую через
	//   `InputComponent->BindKey(EKeys::LeftMouseButton, IE_Pressed, …, OnLeftClick)`
	//   (AsdaPlayerController.cpp:69). Слой UMG остановил бы его, только если бы виджет под
	//   курсором СЪЕЛ событие — но наша подложка и узлы управления это `UImage`, а `SImage`
	//   не реализует `OnMouseButtonDown` и возвращает Unhandled: `Visible` делает картинку
	//   хит-таргетом для наведения, но событие не поглощает. Поэтому блокировку делаем на стороне
	//   контроллера — тем же приёмом, что уже применён там для свободной камеры админа и смерти.
	//   ★ 2026-08-04: этот же расчёт зарегистрирован как ЗОНА в общем реестре
	//   UAsdaWindowSubsystem (RegisterUiZone/IsCursorOverAnyUiWindow) — контроллер спрашивает
	//   реестр, а не каждую подсистему персонально. Метод оставлен: он и есть провайдер зоны.
	UFUNCTION(BlueprintPure, Category="Asda|Chat")
	bool IsCursorOverWindow() const;

	// ★ ПОЛНЫЙ структурный дамп чат-виджетов в рантайме (закон full-structural-analysis-always):
	//   КАЖДЫЙ узел дерева — имя / класс / видимость / анкоры / позиция+размер слота / родитель.
	//   Команда asda.Chat.DumpWidgets. Нужен, чтобы ловить «лишние полосы» от планов импортёра.
	void DumpWidgets() const;

	// Готовые цвета эталона (sRGB, значения = int-константы вызовов CChat_AddTokenizedLines).
	static FColor ColGold()   { return AsdaNoticeColor::Gold; }            // -11514  основной системный (★ U12: значение — `AsdaNoticeColors.h`)
	static FColor ColWhite()  { return FColor(0xFF, 0xFF, 0xFF, 0xFF); }   // -1
	static FColor ColRed()    { return FColor(0xFF, 0x00, 0x00, 0xFF); }   // -65536  критично
	static FColor ColAmber()  { return FColor(0xFF, 0xAD, 0x00, 0xFF); }   // -21248  сбой операции
	static FColor ColGreen()  { return FColor(0x86, 0xFD, 0x51, 0xFF); }   // -7930543 свой подбор
	static FColor ColLime()   { return FColor(0x00, 0xFF, 0x00, 0xFF); }   // -16711936 редкий дроп

private:
	// ── Жизненный цикл окон ──
	bool EnsureWidgets();
	void ResetWidgets();
	// Создать панель-список внутри импортированного WBP на rect контрола JUI (анкор = доля экрана).
	class UScrollBox* MakeScrollList(UUserWidget* Owner, const TCHAR* Name, float X, float Y, float W, float H);

	// Строка НЕВИДИМОГО буфера A/B: только данные + TTL (виджетов у буферов нет — см. шапку).
	// `Born` — `FPlatformTime::Seconds()` (монотонные часы процесса, как эталонный `timeGetTime`; волна В5).
	struct FSysBufferLine { FString Raw; FColor Color; double Born = 0.0; float LifeSec = 10.f; };

	void PushLine(class UPanelWidget* Panel, const FString& RawText, FColor Color, int32 MaxLines);
	void PushBufferLine(EAsdaSysTrack Track, const FString& RawText, FColor Color, float LifeSec);
	void TickBufferExpiry();

	// ── WARMESSAGE_WIN (221) ──
	// Строка верхнего списка: данные живут в сабсистеме (переживают травел), виджеты пересоздаются.
	// Style — стилевые теги строки (`<FO>`/`<FB>`), хранятся вместе со строкой: перестроение
	//   списка (TTL, травел, пересоздание окна) обязано вернуть тот же вид, что был при показе.
	//   `Born` — `FPlatformTime::Seconds()`: время мира после смены карты начинается с нуля (волна В5, МЕЛК-14 ревизии В4).
	struct FWarLine { FString Raw; FColor Color; double Born = 0.0; float LifeSec = 10.f; FString Style; };
	TArray<FWarLine> WarLines;                 // [0] = старшая; максимум kWarMax
	bool EnsureWarWidget();                    // открыть WBP_WARMESSAGE_WIN + вертикальный список
	void RebuildWarLines();                    // перестроить виджеты-строки из WarLines
	void TickWarExpiry();                      // гашение строк по их TTL (эталон +863/+864)

	// CHAT_NEW_SERV_HomeBtn (2573) — «к последней строке». Обработчика по ID 2573 в эталоне НЕТ
	//   (grep по всем 30 файлам: 0 хитов), кнопку тянет штатный механизм по типу контрола. Что она
	//   делает — восстановлено по родственному полю: HomeBtn обычного лога 261 назван
	//   `m_pChatCurLineBtn` («кнопка текущей строки», structs_ru.h:2596), а универсальная идиома
	//   «прыгнуть к новейшей строке» в чате — `Field153 = CClass_GetField153(scroll);
	//   uiScrollbarSetPos(scroll, Field153)` (chat.c:1824-1832, 2078, 3335, 3378, 3419-3423).
	//   Плюс иконка — стрелка ВНИЗ. ⇒ клик = прокрутить журнал в САМЫЙ НИЗ и вернуть автоскролл.
	UFUNCTION()
	void OnHomeBtnClicked();

	// ── Виджеты ──
	UPROPERTY(Transient) TObjectPtr<UUserWidget> SystemWidget;   // WBP_CHAT_NEW_SYSTEM (2570) — ЕДИНСТВЕННОЕ видимое
	UPROPERTY(Transient) TObjectPtr<UScrollBox>  SysLog;         // 2572 — журнал, 500 строк, TTL 0
	UPROPERTY(Transient) TObjectPtr<UButton>     HomeBtn;        // 2573 (родная кнопка WBP либо реле)

	// ── ★ РАСТЯГИВАНИЕ ОКНА ВВЕРХ (эталонная «ручка» *_Resize_Cur) ──────────────────────────────
	//   В JUI у чат-окон есть невидимый (skin NoImage) хват высотой РОВНО 5 px во всю ширину,
	//   лежащий на ВЕРХНЕЙ кромке окна:
	//     CHAT_Resize_Cur          (CHAT_BACKWIN)   abs=(3, 438, 454×5)  — окно 214 = (3, 439, 454×319)
	//     CHAT_NEW_SERV_Resize_Cur (CHAT_NEW_SERV)  abs=(15, 578, 454×5) — кромка чат-колонки
	//   idnum = −1 у обоих ⇒ по ID их не находят, обработчика под конкретный idc в эталоне нет:
	//   тянет их ШТАТНЫЙ механизм окна по типу контрола (как TextMove тянет окно целиком).
	//   Низ окна закреплён (POINT-анкор fy=1.0) — тянем ТОЛЬКО верхнюю кромку вверх/вниз,
	//   верхний-ПРАВЫЙ угол входит в ту же полосу (её правый конец).
	// ★ КОНТРОЛЫ, ДОБАВЛЕННЫЕ ПОЛЬЗОВАТЕЛЕМ ВРУЧНУЮ в WBP (имена сняты из ассета, 2026-08-04):
	//   RESIZE, SCROLL_UP, SCROLL_DOWN — все класса UButton, лежат на КОРНЕВОМ канвасе окна.
	//   Полное дерево WBP_CHAT_NEW_SYSTEM на эту дату:
	//     CHAT_NEW_SERV_back_1 / _1_0            — плита-подложка (Image)
	//     CHAT_NEW_SYSTEM_TextMove               — зона перетаскивания окна
	//     CHAT_NEW_SERV_SYSbottn_3 / _3_0 / _OPT0 / _Text — заголовок «Система»
	//     CHAT_NEW_SERV_line0_4 / _4_0 … _4_7    — рамка, 8 кромок (центральный белый план импортёр НЕ создал)
	//     CHAT_NEW_SERV_listbox_grp              — ГРУППА-канвас списка, ВНУТРИ неё CHAT_NEW_SERV_HomeBtn
	//     RESIZE, SCROLL_UP, SCROLL_DOWN         — добавлены пользователем
	//   ★★★ ФАКТ (лог 2026-08-04 19:35 + подтверждение пользователя): эти три узла — **UImage**,
	//   а не UButton. Прежний поиск через Cast<UButton> не находил их вовсе («RESIZE=НЕТ …»),
	//   отсюда шли ВСЕ три симптома: обработчики не привязывались, белый список ресайза не
	//   срабатывал (SCROLL_DOWN попадал в раскладку и растягивался), перехват клика не настраивался.
	//   У UImage нет OnPressed/OnReleased, поэтому мышь ловится ЕДИНЫМ механизмом, не зависящим от
	//   класса виджета и от доставки событий Slate: хит-тест по ФАКТИЧЕСКОЙ геометрии в тике —
	//   `GetCachedGeometry().IsUnderLocation(курсор)` + состояние ЛКМ через
	//   `FSlateApplication::GetPressedMouseButtons()`, с фиксацией узла, НА КОТОРОМ началось нажатие
	//   (иначе протаскивание мимо ложно срабатывало бы на соседнем узле).
	UPROPERTY(Transient) TObjectPtr<UWidget>     ResizeSrc;      // RESIZE      — тянуть за неё (вертикаль)
	UPROPERTY(Transient) TObjectPtr<UWidget>     ScrollUpSrc;    // SCROLL_UP   — листать вверх
	UPROPERTY(Transient) TObjectPtr<UWidget>     ScrollDownSrc;  // SCROLL_DOWN — листать вниз
	UPROPERTY(Transient) TObjectPtr<UWidget>     PressedNode;    // узел, на котором началось нажатие ЛКМ
	// ★ ГОРИЗОНТАЛЬ: своя прозрачная полоса по ПРАВОЙ кромке (в JUI такого контрола нет — добавка
	//   порта, ровно как CHAT_NEW_ResizeRight у окна чата 2500). Отдельная зона, а не «угол»:
	//   вертикальный хват RESIZE уже отлажен и подтверждён юзером, совмещённый менял бы вторую ось.
	UPROPERTY(Transient) TObjectPtr<UWidget>     ResizeRightSrc; // CHAT_NEW_SERV_ResizeRight

	// Снимок исходного canvas-слота РОДНОГО контрола WBP + его роль при растяжении.
	//   ── ВЕРТИКАЛЬ (низ окна закреплён, тянем верхнюю кромку) ──
	//   bStretch — контрол перекрывает окно по высоте (плита/рамка/средний ряд 9-slice) → растёт;
	//   bTop     — прижат к ВЕРХНЕЙ кромке (заголовок, кнопка, верхний ряд 9-slice) → едет вверх;
	//   иначе    — прижат к НИЖНЕЙ кромке (нижний ряд 9-slice) → не двигается.
	//   ── ГОРИЗОНТАЛЬ (левый край закреплён, тянем правую кромку; 2026-08-05) ──
	//   bWide    — контрол занимает бо́льшую часть ширины РОДИТЕЛЯ (плита, шапка, верхний/нижний ряд
	//              9-slice, группа списка) → его ШИРИНА растёт на надбавку DW;
	//   bRight   — прижат к ПРАВОМУ краю родителя (правый ряд 9-slice, кнопка 2573 внутри группы) →
	//              его X едет вправо на DW; иначе прижат к ЛЕВОМУ краю и стоит на месте.
	//   bRoot    — прямой ребёнок КОРНЕВОГО канваса. Только у таких координаты ЭКРАННЫЕ (базовые px),
	//              поэтому вертикальные роли и общий сдвиг окна применимы ИСКЛЮЧИТЕЛЬНО к ним;
	//              вложенные в группы планы двигаются ТОЛЬКО по горизонтали и ТОЛЬКО локально.
	struct FNativeSlot
	{
		TWeakObjectPtr<UWidget> W;
		FMargin Orig;                       // POINT-слот: (X, Y, Ширина, Высота)
		bool bTop = false;
		bool bStretch = false;
		bool bRoot = false;
		bool bWide = false;
		bool bRight = false;
	};
	TArray<FNativeSlot> NativeSlots;

	void CaptureNativeSlots();              // снять исходные слоты и расклассифицировать роли
	// Применить ГЕОМЕТРИЮ окна (базовые px) ко всем ролям: высота вверх от закреплённого низа,
	//   ширина вправо от закреплённого левого края.
	void ApplyWindowGeometry(float NewH, float NewW);
	void ApplyWindowHeight(float NewH);     // прежняя точка входа: высота при неизменной ширине
	void ApplyWindowWidth(float NewW);      // ширина при неизменной высоте
	void EnsureResizeRight();               // прозрачная полоса-кромка справа (наш хват горизонтали)
	void ApplyLinesWidth();                 // перенос строк журнала по ТЕКУЩЕЙ ширине ленты
	void OnResizeRightPressed();
	void OnResizeRightReleased();
	// ★ Сцепка: подтянуть НИЗ журнала к верхней кромке окна чата 2500 (см. kDockGap).
	//   Вызывается каждый тик; перекладывает окно только когда кромка реально изменилась.
	void TickDockToChat();
	void OnResizePressed();
	void OnResizeReleased();
	// ★ Записать ТЕКУЩИЙ размер журнала в ini игры (единый механизм — UAsdaWindowSubsystem,
	//   секция [AsdaWindowLayout]; решение юзера 2026-08-07, в эталоне сохранения размеров нет).
	void PersistWindowSize();
	void OnScrollUpPressed();
	void OnScrollDownPressed();
	void OnScrollReleased();
	// Единый хит-тест-механизм мыши по узлам управления (работает с любым классом виджета).
	void TickPointer(float DeltaTime);
	bool IsUnderCursor(const UWidget* W, const FVector2D& CursorAbs) const;
	// Узел окна по имени среди ВСЕХ UWidget (не только кнопок): точное имя, затем вхождение.
	class UWidget* FindNodeByName(const TCHAR* Exact, const TCHAR* Needle) const;
	// ★ Перенесено в ассет (15.09, разрешение владельца): NormalizeControlSlot удалён — слоты WBP_CHAT_NEW_SYSTEM уже POINT.
	// Прямоугольник окна в АБСОЛЮТНЫХ экранных координатах (по фактической геометрии подложки).
	bool GetWindowScreenRect(FVector2D& OutMin, FVector2D& OutMax) const;
	// Регистрация/снятие зоны окна в общем реестре UAsdaWindowSubsystem (перехват мирового клика).
	void RegisterZone();
	void UnregisterZone();
	// Id зоны в реестре = имя окна оригинала.
	static const FName kZoneId;
	void ScrollByLines(int32 Lines);        // листание журнала (шаг — наш, см. kScrollStepLines)
	// ★★★ Колесо мыши над списком журнала 2572. Разбор эталона — в `OnWheelChatWindow`
	//   (AsdaChatWindowSubsystem.cpp): оригинал колесо интерфейсу ОТДАЁТ (очередь типа 7 →
	//   виртуальный слот 7 менеджера окон), но поведение слота для конкретного списка из
	//   разобранного среза не выводится ⇒ шаг = шаг штатных кнопок SCROLL_UP/SCROLL_DOWN.
	bool OnWheelJournal(int32 Notches);
	float MeasureLineHeight() const;        // фактическая высота строки журнала (десайр-сайз первой)

	float WinH = kWinMinH;                  // текущая высота окна (базовые px), переживает травел
	float WinW = 474.f;                     // текущая ШИРИНА окна (базовые px) = kWinMinW; переживает травел
	                                        //   ★ литералом: kWinMinW объявлена НИЖЕ по файлу — инициализатор
	                                        //   поля не видит статик-константы, объявленные позже (C2065).
	bool  bHomeBtnNative = false;           // true = кнопка 2573 взята ИЗ WBP (а не реле поверх плана)
	bool  bFrameShown = false;              // рамка 2575 показана (★ отступление от эталона, решение юзера)
	bool  bHeaderShown = false;             // заголовок 2574 показан (★ там же)
	bool  bPlateHidden = false;             // плита переведена в ровную заливку (★ решение юзера)
	TWeakObjectPtr<class UImage> PlateImg;  // родная плита CHAT_NEW_SERV_back — подложка окна
	float AppliedBgAlpha = -1.f;            // применённая непрозрачность подложки (CVar asda.Chat.BgAlpha)
	void  ApplyPlateBrush(float Alpha);     // подменить кисть плиты на сплошную заливку
	bool  bResizing = false;
	// ★ Аудит №3 (B3): прежний латч `bPrevLmbDown` — общая точка (фронты «отпущена → нажата» и
	//   «нажата → отпущена» считает `FAsdaMouseFrame::Update`, вызов стоит на месте прежнего опроса).
	FAsdaMouseFrame Mouse;
	float DragStartH = 0.f, DragStartMouseY = 0.f;
	// ★★★ ГОРИЗОНТАЛЬНОЕ РАСТЯГИВАНИЕ ОКНА 2570 — ОТСТУПЛЕНИЕ ОТ ОРИГИНАЛА (решение юзера 2026-08-05).
	//   РЕВЕРС (ДОПОЛНЕНИЕ 18 отчёта Reports/chat_main_window.md): окна 2500 и 2570 в ОРИГИНАЛЕ
	//   размер НЕ МЕНЯЮТ ВООБЩЕ — ни по одной оси. Класс `CResizeWindow` в движке есть и умеет обе оси
	//   независимо (`uiControl_ClampMoveResize`, ui_core.c:119292), но создаётся ТОЛЬКО при
	//   `Type = "ResizeWindow"` (ui_core.c:105552), а такой строки в gamemain_jui.ini НЕТ НИ РАЗУ;
	//   у окна 2570 контролов с Resize/Size в имени нет вовсе — его 6 детей это back, TextMove,
	//   SYSbottn 2574, line0 2575, listbox 2572 (+HomeBtn 2573). Размеры окон никуда не сохраняются
	//   (`CChat_SaveChatOpt`, chat.c:1397-1402, пишет только фильтры каналов и вкладки).
	//   ⇒ и вертикаль (сделана раньше), и горизонталь — НАШИ отступления. Видеть это в коде и считать
	//   багом порта — ОШИБКА.
	//   ★ ШИРИНА ЖУРНАЛА НЕЗАВИСИМА ОТ ШИРИНЫ ОКНА ЧАТА 2500 (прямое требование юзера): сцепка
	//   `TickDockToChat` относится ТОЛЬКО к вертикали (низ журнала приклеен к верхней кромке чата).
	bool  bResizingW = false;               // тянем ПРАВУЮ кромку
	float ResizeStartW = 474.f;             // ширина окна в момент нажатия (базовые px) = kWinMinW
	float ResizeStartX = 0.f;               // курсор X в момент нажатия (базовые px)
	float LinesWrapW = 0.f;                 // фактическая ширина переноса строк ленты (0 = не считана)
	// Автоповтор листания при удержании кнопки (пауза, затем шаг каждые kRepeatEvery).
	int32 ScrollHeldDir = 0;                // 0 = не держим, −1 = вверх, +1 = вниз
	float ScrollHeldTime = 0.f;
	UPROPERTY(Transient) TObjectPtr<UUserWidget> WarWidget;      // WBP_WARMESSAGE_WIN (221)
	UPROPERTY(Transient) TObjectPtr<class UVerticalBox> WarBox;  // список строк 221 (центр X, верх)

	// Невидимые буферы-накопители 2557/2558 (эталон держит их скрытыми) — чистые данные.
	TArray<FSysBufferLine> LinesA, LinesB;

	// Пережившая травел история журнала (окна умирают вместе с миром — данные живут в сабсистеме).
	struct FSysLogLine { FString Raw; FColor Color; };
	TArray<FSysLogLine> LogHistory;


	// Лимиты/времена эталона.
	static constexpr int32 kSysLogMax   = 500;    // sub_935C9B(chatSystemListBox, 500)
	static constexpr int32 kBufferMax   = 5;      // sub_935C9B(sysMsgA/BWin, 5)
	static constexpr float kBufferLife  = 10.f;   // uiChatLogAddLine TTL 10 000 мс
	static constexpr int32 kSysZ        = 690;    // под нотисами (700/710), над игровым слоем
	// Пределы растяжения. Минимум — авторская высота окна 2570 (JUI 70 px, ≈4 строки).
	// Максимум — 319 px: высота CHAT_BACKWIN (3, 439, 454×319), самой большой чат-области оригинала.
	static constexpr float kWinMinH   = 70.f;
	static constexpr float kWinMaxH   = 319.f;
	static constexpr float kWinBottom = 578.f;   // ЭТАЛОННЫЙ низ окна 2570 в базе (508+70)
	// ★★★ СЦЕПКА С ОКНОМ ЧАТА 2500 (требование юзера 2026-08-05).
	//   В JUI журнал занимает y 508…578, а основной чат начинается с y 580 ⇒ ЗАЗОР РОВНО 2 px.
	//   Когда чат растёт вверх, журнал обязан ехать вверх на ту же величину, оставаясь
	//   приклеенным к верхней кромке чата. Реализовано так: чат публикует свою верхнюю кромку
	//   в реестре окон (UAsdaWindowSubsystem::SetWindowAnchorTop), а журнал в своём тике читает
	//   её и пересчитывает СВОЙ низ = кромка чата − зазор. Своё растягивание журнала (кнопка
	//   RESIZE, пределы 70…319) при этом не трогается: он растёт вверх от этого же низа.
	static constexpr float kDockGap = 2.f;       // 580 − 578, эталонный зазор между окнами
	// Текущий низ окна журнала в базовых px. Равен kWinBottom, пока чат стоит в исходной позиции.
	float DockBottom = kWinBottom;
	// Высота хвата. В эталоне полоса *_Resize_Cur ровно 5 px; здесь 8 px, симметрично по кромке
	//   (4 сверху / 4 снизу) — 5 px на верхней кромке физически трудно поймать курсором, и живой тест
	//   2026-08-03 это подтвердил («растягивание не работает»). ★ Отступление ради попадания мышью.
	// Листание кнопками SCROLL_UP/SCROLL_DOWN. Шаг = 2 строки — ★ НАШЕ решение, а не данные эталона
	//   (исправлено 2026-08-06: `uiListSetScrollOffset(list, 2)` @0x12EA420 задаёт МЕЖСТРОЧНЫЙ
	//   ИНТЕРВАЛ 2 px, `2 + getFontGlyphHeight()`, а не шаг листания — ui_core.c:111621).
	//   Автоповтор при удержании — тоже добавка порта ради удобства (в эталоне не подтверждён).
	static constexpr int32 kScrollStepLines = 2;
	static constexpr float kRepeatDelay = 0.35f;   // пауза до автоповтора
	static constexpr float kRepeatEvery = 0.12f;   // период автоповтора
	static constexpr float kGrabH     = 8.f;
	// Кегль игрового UI. В JUI размер шрифта per-control НЕ хранится — он глобальный; портовый
	//   импортёр (Tools/asda_jui_generic_umg.py, FONT_PX = 8) ставит 8 px всему импортированному UI.
	//   Базовый стиль DT_AsdaRichStyle — 14 px, поэтому строки журнала явно приводим тегом <FS08>.
	static constexpr float kFontPx    = 8.f;
	static constexpr float kLine0W    = 474.f;   // ширина рамки CHAT_NEW_SERV_line0 (JUI 1,508,474x70)
	// ── ★ ПРЕДЕЛЫ И ХВАТ ГОРИЗОНТАЛЬНОГО РАСТЯГИВАНИЯ (см. блок у bResizingW) ──
	static constexpr float kWinX      = 1.f;     // левый край окна 2570 в базе (JUI «1 508 474×70») — ЯКОРЬ
	// Минимум = авторская ширина 474 (прямое требование юзера: «стандартный размер и есть минимальный»).
	//   ★ Литералы, а не kLine0W/AsdaJui::kBaseW: инициализатор static constexpr внутри класса не видит
	//   константы, объявленные НИЖЕ, а kBaseW тянул бы заголовок AsdaJuiSlot.h в этот .h (C2065, грабля
	//   прошлого захода — ДОПОЛНЕНИЕ 18 Д18.4).
	static constexpr float kWinMinW   = 474.f;   // = kLine0W
	// Максимум = правый край базового экрана минус якорь и зазор: 1024 − kWinX(1) − 4 = 1019.
	//   ★ У 2570, в отличие от 2500, справа НЕТ колонки кнопок (2605/2606/2607) — вычитать нечего.
	static constexpr float kWinMaxW   = 1019.f;
	static constexpr float kResizeBandW   = 8.f; // ширина полосы захвата правой кромки (как у 2500)
	static constexpr float kResizeBandOut = 3.f; // насколько полоса заходит внутрь окна от правого края
	static constexpr int32 kWarMax      = 3;      // sub_935C9B(warMessageWin, 3) — макс 3 строки
	static constexpr int32 kWarZ        = 705;    // AlwaysTop=1: над NOTICE_WIN (700), под HERO (710)
	// Геометрия JUI 221: AlignX «16 50 28» центр, AlignY «32 50 28» → верх окна на 0.2507 высоты
	//   (y=192.5 базы 768). POINT-анкор по центру X — на ультрашироком экране окно НЕ растягивается.
	static constexpr float kWarAnchorY  = 192.5f / 768.f;
	static constexpr float kWarW        = 464.f;
	static constexpr float kWarH        = 45.f;
};
