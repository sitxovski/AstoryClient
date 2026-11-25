// Copyright (c) 2026 @sitxovski (Discord)
// UAsdaHotbarSubsystem — реализация панели быстрого доступа QUICK_NEW_WIN.
//   Полный разбор эталона — в шапке AsdaHotbarSubsystem.h; источники: Reports/quick_win_re_A.md,
//   quick_win_re_B.md; отчёт по реализации — Reports/quick_win_port.md.
#include "UI/AsdaHotbarSubsystem.h"
#include "AsdaPerf.h"   // ★ R4-b 14.11.2026: области замера времени игрового потока
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "UI/AsdaSoulmateSubsystem.h"    // ★ 06.11.2026: пара и «Друг в игре» — из карточки (`asda.Soul.SlotTruth`)
#include "UI/AsdaCharaInfoSubsystem.h"   // ★ 06.11.2026: перерисовка лотков действий при смене пары
#include "UI/AsdaNoticeColors.h"   // ★ U12 (В1 дедупа №2): эталонные цвета нотисов одной точкой
#include "UI/AsdaItemCountStyle.h"   // ★ 26.10.2026: обводка числа стопки — общая точка (ключ asda.UI.ItemCountOutline)
#include "Resources/AsdaAssetPaths.h"   // ★ волна 2 п. 7 (14.09.2026): реестр путей и правил имён ассетов (генерируемый)
#include "UI/AsdaNotice.h"   // ★ волна 1: фасад нотисов (Loc / Chat / MsgBox)
#include "AsdaBytes.h"   // D4: единые читалки сырой записи (little-endian, DataHex, ASCII-поле)
#include "UI/AsdaUiHelpers.h"   // ★ D2/D4: единые UI-хелперы (дедуп 2026-08-08)

#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaMessageSubsystem.h"
#include "UI/AsdaMiniStateSubsystem.h"        // один общий тултип на HUD (эталон держит один g_ItemTooltip)
#include "UI/AsdaItemTooltipSubsystem.h"      // владелец общей плашки (порт g_ItemTooltip)
// ★ ТОЛЬКО ЗАГОЛОВОК, поведение не меняется: ниже (мост к окну навыков) уже используется
//   `UAsdaSkillWindowSubsystem`, но #include отсутствовал — тип «доезжал» через unity-сборку.
//   Как только этот файл попадает в отдельную единицу трансляции, компиляция падала.
#include "UI/AsdaSkillWindowSubsystem.h"
#include "UI/AsdaInventorySubsystem.h"     // сводная сверка шторок отката (`asda.Cooldown.Dump`)
#include "UI/AsdaBoosterPanelSubsystem.h"  // ★ гейт `CObject_GetField254(g_pInventoryWnd)` = ОКНО ГАЧИ 10050
#include "UI/AsdaChatWindowSubsystem.h"    // ★ гейт `Input_IsTextInputFocused(g_pChatInputWnd)`
#include "UI/AsdaSkillDatabase.h"          // ★ Аудит №3 (A13): имя иконки навыка — только отсюда
#include "UI/AsdaAnimatedPlane.h"
#include "Login/AsdaButtonRelay.h"
#include "Items/AsdaItemTableSubsystem.h"
#include "Resources/AsdaTextureSubsystem.h"
#include "Text/AsdaTextSubsystem.h"
#include "Login/AsdaText.h"   // [ПОРТ] 09.10.2026 (мультиязычность, этап 3): FAsdaText::Loc — заголовки подсказок по id
#include "AsdaNetSubsystem.h"
#include "World/AsdaWorldSubsystem.h"
#include "World/AsdaLoadManagerSubsystem.h"
#include "World/AsdaPlayerCharacter.h"
#include "World/AsdaPlayerController.h"
#include "World/AsdaKeybindConfig.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Engine/Engine.h"   // ★ 17.10.2026 GEngine->Exec(quit) в самотесте иконок эмоций
#include "Components/CanvasPanel.h"
#include "Components/PanelWidget.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Engine/Texture2D.h"
#include "Framework/Application/SlateApplication.h"
#include "Fonts/FontMeasure.h"
#include "Engine/DataTable.h"
#include "Items/AsdaItemDbRows.h"
#include "HAL/IConsoleManager.h"
#include "UObject/ObjectKey.h"       // ★ 26.10.2026: TObjectKey — ревизия списка навыков по экземпляру панели
#include "UI/AsdaCraftSubsystem.h"   // ★ волна В3 ремесла: единый предикат «окно ремесла открыто» (`IsAnyCraftWindowOpen`)
#include "UI/AsdaTradeStateSubsystem.h"   // ★ 29.09.2026: окно обмена 2008 открыто — гейт слотов и значков заявки 4/5
#include "AsdaOutgoingGate.h"               // ★ С-Р1: общий офлайн-гейт отладочных команд (RefuseDebugWhenOnline)
#include "World/AsdaFishingSubsystem.h"     // ★ 08.10.2026 (рыбалка В4): гейт «идёт рыбалка» в `IsActivationBlocked`
#include "AsdaSelf.h"                      // ★ Дедуп №2, В1-B (U5a): «это я?» — `AsdaSelf::IsSelfUserIdx`
#include "UI/AsdaHelpTopics.h"             // ★ 30.10.2026: справочные темы кнопок листания и пустого слота оружия
#include "UI/AsdaDeathInputLock.h"          // ★ 05.11.2026: меню смерти запирает слоты (эталон CUIManager+0x44, ui_core.c:67351)
#include "AsdaHotbarSubsystemInternal.h"   // ★ R2 10.11.2026: общее для частей AsdaHotbarSubsystem*.cpp

DEFINE_LOG_CATEGORY(LogAsdaQuick);   // ★ R2: было DEFINE_LOG_CATEGORY_STATIC — категория общая для частей

static const TCHAR* kQuickWbp = TEXT("/Game/AsdaAssets/UI/Player_UI/HUD/WBP_QUICK_NEW_WIN.WBP_QUICK_NEW_WIN_C");

// Число кадров скинов (JuiSkin.ini): шторка `CoolTime_Effect` = 118, вспышка `CoolTimeAfter_Effect` = 16.
//   По ним И ТОЛЬКО по ним мы отличаем роль плана — потому что у слота ОРУЖИЯ скины в JUI перепутаны
//   (дефект Д1: CEND13 получил шторку, COOL13 — вспышку).
static constexpr int32 kCurtainFrames = 118;

// ═══════════════════════════ КЛЮЧИ КОНСОЛИ: ВОЗВРАТ ДЕФЕКТОВ ОРИГИНАЛА ═════════════════════════
//   Закон fix-original-defects: дефект чиним, но оставляем ключ «как было» для сверки с эталоном.
static ASDA_DEV_CVAR(int32, CVarLegacySkinSwap,
	TEXT("asda.Quick.LegacySkinSwap"),
	0,
	TEXT("Слот ОРУЖИЯ: 0 = роль плана берётся по числу кадров скина (исправлено, Д1); "
	     "1 = как в JUI оригинала — CEND13 шторка, COOL13 вспышка (перепутано)."),
	ECVF_Default);

// Ревизия модели выученных навыков окна навыков (определена в AsdaSkillWindowSubsystem.cpp).
extern uint32 AsdaSkillLearnedRevision();

// Последняя ревизия, по которой панель перекрасила слоты навыков (по экземпляру подсистемы; без правки заголовка UCLASS).
static TMap<TObjectKey<UAsdaHotbarSubsystem>, uint32> GHotbarSeenSkillRev;

// ★ R4-b 14.11.2026 [ПОРТ] (R4, доля b, B5/B7): шторки откатов — мир и часы реестра раз за кадр (прежде — на каждый слот); защёлка клавиш
//   обнуляется одним проходом (прежде — двумя одинаковыми подряд). Шторки те же (часы — те же `AsdaCooldownNowS`).
// ★ R4-b закрытие (15.11.2026): ключ `asda.Perf.HotbarFrame` — старый путь и ключ удалены — только новое (решение владельца).
// ★ R4-b 14.11.2026 [ПОРТ] (R4, доля b, B6): ревизия подписей клавиш — регистронезависимый хэш пути раскладки каждый кадр; теперь хэш считается
//   заново только при смене байтов пути (одинаковые байты — тот же хэш). Ревизия та же.
// ★ R4-b закрытие (15.11.2026): ключ `asda.Perf.HotbarKeyRev` — старый путь и ключ удалены — только новое (решение владельца).

// ★ R4-b 14.11.2026 (B6): хэш пути раскладки по кэшу — тот же `GetTypeHash`, пересчёт только при несовпадении байтов с прошлым путём.
static uint32 AsdaHotbarPathHashCached(const FString& Path, FString& Cache, uint32& Hash, bool& bCached)
{
	if (!bCached || Path.Len() != Cache.Len() || FMemory::Memcmp(*Path, *Cache, (SIZE_T)Path.Len() * sizeof(TCHAR)) != 0)
	{
		Cache = Path; Hash = GetTypeHash(Path); bCached = true;
	}
	return Hash;
}


// ═══ ★★★ ОСОЗНАННОЕ ОТСТУПЛЕНИЕ ОТ ОРИГИНАЛА — РЕШЕНИЕ ЮЗЕРА 2026-08-14 ═══════════════════════
//
// ЧТО ДЕЛАЕТ ОРИГИНАЛ (доказано ДВУМЯ методами, `Reports/item_cooldown_premium.md` §2):
//   • ПРЕДМЕТЫ — шторка ТОЛЬКО на панели быстрого доступа (`QUICK_NEW_COOL1..13`). Все 5 вызовов
//     `hotbarPushItemCooldown@0xE48FD0` и все 12 вызовов `hotbarRefreshSlotsByItemRef@0xDF0840`
//     первым аргументом несут `g_pQuickSlotBar`. Ни в `INVEN_WIN`, ни на 11 слотах надетого
//     контролов шторки в .JUI НЕТ вообще. (`COOLTIME00..27` окна состояния — это счётчик
//     длительности БАФФА, скин `CoolTime_Mini`, к откату отношения не имеет.)
//   • НАВЫКИ — а вот тут шторка в оригинале ЕСТЬ (уточнение к §2 отчёта, снято 2026-08-14 по
//     `skill_buff.c`): `skillAddAndRefreshBars@0xE49A20` показывает узел дерева
//     `m_aSkillSlot[68·слот + 12]` (= `SKILLTREE_COOL{NN}`, тот же скин `CoolTime_Effect`) вызовом
//     `uiControl_ShowWithItem(ctrl, 120, 127, 8, 1000, 0)` — ЗАМОРОЖЕННЫЙ кадр 120, animate = 0, —
//     а `skillRemoveAndRefreshBars@0xE49BB0` гасит его (`CControl_CloseHide`).
//
// ЧТО ДЕЛАЕМ МЫ ПРИ ЗНАЧЕНИИ 1 (умолчание):
//   • ячейки сумки/снаряжения и слоты надетого получают шторку, которой в оригинале не было;
//   • узел дерева навыков получает не замороженный кадр, а ЖИВОЙ ПРОГОН, синхронный с остатком, —
//     ровно та же механика кадров, что у панели (закон `no-invented-ui`: визуал не выдуман, а взят
//     с плана панели; данные — из ЕДИНОГО реестра откатов этой подсистемы).
// ЧТО ДЕЛАЕМ ПРИ ЗНАЧЕНИИ 0: инвентарь шторку не рисует вовсе, узел дерева показывает эталонный
//   замороженный кадр 120 — то есть ровно поведение оригинала в обоих окнах.
static ASDA_DEV_CVAR(int32, CVarCooldownShowEverywhere,
	TEXT("asda.Cooldown.ShowEverywhere"),
	1,
	TEXT("Шторка отката вне панели быстрого доступа (отступление от оригинала, решение юзера 2026-08-14): "
	     "1 = показывать ВЕЗДЕ — панель, ячейки инвентаря/снаряжения, узлы дерева навыков (живой прогон); "
	     "0 = как в оригинале — в инвентаре шторки нет, узел дерева показывает замороженный кадр 120 "
	     "(эталон skillAddAndRefreshBars@0xE49A20)."),
	ECVF_Default);

bool UAsdaHotbarSubsystem::IsCooldownShownEverywhere()
{
	return CVarCooldownShowEverywhere.GetValueOnAnyThread() != 0;
}

// ═══════ ★★★ ОТКАТЫ: ЧАСЫ, ПРОГОН ШТОРКИ, СНИМОК 5465 (25.10.2026, жалоба владельца) ═══════════════════════════════
// Жалоба: «у маунтов fable (559–637) отсчёт на иконке не совпадает с реальным; переложишь предмет — отсчёт обновляется
//   и „дооткатывается“». Разбор (эталон + бинарь IDB 13 344 768 б + сервер + движок), четыре независимые причины:
//
// 1) [ИСПРАВЛЕНИЕ] ЧАСЫ РЕЕСТРА. Реестр жил на `UWorld::GetTimeSeconds()`. Мировое время — НЕ реальное:
//    `UWorld::Tick` (LevelTick.cpp:1599) зажимает каждый кадр `AWorldSettings::FixupDeltaSeconds` до
//    `MaxUndilatedFrameTime` = 0,4 с (BaseGame.ini:199), а `OpenLevel` (портал) рождает НОВЫЙ мир с часами от нуля.
//    Итог: любой рывок дольше 0,4 с (посадка на маунт грузит модель/эффекты — у новых fable-маунтов рывок самый
//    длинный) оставлял реестр позади сервера, а шторка (`UAsdaAnimatedPlane`, часы Слейта = реальные) доигрывала
//    вовремя и гасла; реестр же ещё «в откате» → иконка готова, а предмет не применяется; перекладка перезапускала
//    шторку с остатком реестра → «дооткат». После портала все записи (их EndS — в часах старого мира) уезжали в
//    будущее на всё время прошлой карты. Оригинал считает по `timeGetTime()` (реальные мс): запись реестра
//    `hotbarPushItemCooldown@0xE48FD0` +32 = `(float)timeGetTime()`, тик `craftQueueTick@0xE47600` (имя врёт —
//    это тик реестра CSkillWnd+0xE0) — `timeGetTime() − (+32)`. Сервер — `GetTickCount()` (P82 Expired, сток sub_4F24E0).
//    Ключ `asda.Cooldown.RealClock`: 1 — реальные часы `FPlatformTime::Seconds()` (как оригинал и сервер), 0 — прежнее.
//
// 2) [ОРИГИНАЛ] ДЛИТЕЛЬНОСТЬ ПРОГОНА ШТОРКИ. Порт ставил `FrameInterval = ОСТАТОК` при стартовом кадре = доле
//    пройденного. Кадр плана = `elapsed / interval · 117 + start` (ui_core.c:108836-108840, порт 1:1 в
//    AsdaAnimatedPlane::TickAnim) ⇒ шторка, перезапущенная посреди отката (окно открыли, предмет переложили, страница,
//    вход/портал с остатком), доигрывала за `остаток · (117 − start) / 117` — РАНЬШЕ сервера. Эталон по бинарю
//    `hudSlotUpdateCooldown@0xDF23D0` (0xDF2445…0xDF246B): `push 1; fld [rec+36]; →u64; push 118; push 117;
//    push [rec+44]` — интервал = ПОЛНАЯ длительность записи (+36), стартовый кадр = текущий кадр реестра (+44,
//    его пишет тик `craftQueueTick`); проверка `> 0` — тоже по +36. Псевдокод зовёт это «cooldownFrac» — имя врёт.
//    Мы ставим интервал так, чтобы прогон кончился РОВНО в остаток: `остаток · 117 / (117 − start)` — это темп
//    эталона (полная длительность), но без его недобора до 1 кадра на усечении `(u64)`.
//    Ключ `asda.Cooldown.CurtainTotal`: 1 — так, 0 — прежнее «интервал = остаток».
//
// 3) [ИСПРАВЛЕНИЕ] ДОГОНКА ШТОРКИ. Оригинал перезапускает шторку на КАЖДОМ `CHotbar_RefreshPage` (событийно), у нас
//    `DriveCurtain` перезапускает только при смене содержимого/состояния. Если план доиграл, а реестр всё ещё «в
//    откате» (остаток > 0,25 с), шторка поднимается заново с текущего кадра. Ключ `asda.Cooldown.CurtainResync`.
//
// 4) [ОРИГИНАЛ] 5465 — ПОЛНЫЙ СНИМОК. `Recv_SkillCoolTime@0xE4B180` первой строкой зовёт `skillHotbarRefreshLocked`
//    (0xE4A210: `add ecx,0E0h; call 0x9339CD (пусто?); jne; call 0x934DD2` → 0xE5A2B0 — освободить буфер вектора
//    52-байтовых записей) — то есть ОЧИЩАЕТ ВЕСЬ реестр (навыки и предметы) и строит его заново из пакета. Сервер шлёт
//    5465 только на входе в мир и после портала (`charLogonCoolTimeSaveRes@0x528EA0`, `charPortalLogonCoolTimeSaveRes
//    @0x52E030`, GameServer.c:112738/114783) — это его сохранённый набор. Порт только дописывал, и на портале у нас
//    оставались откаты, которых у сервера уже нет (групповые откаты расходников сервер на портале не сохраняет).
//    Ключ `asda.Cooldown.Snapshot5465`: 1 — снимок, 0 — прежнее дописывание.
static ASDA_DEV_CVAR(int32, CVarCooldownRealClock,
	TEXT("asda.Cooldown.RealClock"),
	1,
	TEXT("[ИСПРАВЛЕНИЕ 25.10.2026] Часы реестра откатов (панель, сумка, книга навыков): 1 = реальные (FPlatformTime, как "
	     "timeGetTime оригинала и GetTickCount сервера); 0 = прежнее мировое время (зажато 0,4 с/кадр, обнуляется порталом)."),
	ECVF_Default);

static ASDA_DEV_CVAR(int32, CVarCooldownSnapshot5465,
	TEXT("asda.Cooldown.Snapshot5465"),
	1,
	TEXT("[ОРИГИНАЛ Recv_SkillCoolTime@0xE4B180 → skillHotbarRefreshLocked@0xE4A210] Пакет 5465 (вход в мир, портал) — "
	     "полный снимок откатов: 1 = реестр очищается и строится из пакета; 0 = прежнее дописывание поверх."),
	ECVF_Default);

// ★ ЕДИНЫЕ ЧАСЫ РЕЕСТРА ОТКАТОВ (точка входа для инвентаря и книги навыков: объявлена там локально — ключ здесь).
//   `Ctx` — любой объект игры (для запасной ветки «мировое время» при ключе 0).
double AsdaCooldownNowS(const UObject* Ctx)
{
	if (CVarCooldownRealClock.GetValueOnAnyThread() != 0) { return FPlatformTime::Seconds(); }
	const UWorld* W = Ctx ? Ctx->GetWorld() : nullptr;
	return W ? W->GetTimeSeconds() : 0.0;
}

// Аксессоры ключей для соседних .cpp (закон shipping-проверки: чужой ключ — функцией, не поиском по имени).
bool AsdaCooldownRealClockOn()     { return CVarCooldownRealClock.GetValueOnAnyThread() != 0; }
bool AsdaCooldownSnapshot5465On()  { return CVarCooldownSnapshot5465.GetValueOnAnyThread() != 0; }

// ══════════════════════════════ МЕЛКИЕ ПОМОЩНИКИ ═══════════════════════════════════════════════
void UAsdaHotbarSubsystem::SetVis(UWidget* W, bool bVisible)
{
	if (!W) { return; }
	// SelfHitTestInvisible: клики по панели ловим ОПРОСОМ (эталон получает готовые события 5/7/0xD
	//   от менеджера окон), поэтому виджеты не должны отбирать мышь у мира.
	// ★ Волна 1 (14.09.2026, В3): правило видимости — общая точка `AsdaUi::SetShownDecor` (режим тот же).
	AsdaUi::SetShownDecor(W, bVisible);
}

bool UAsdaHotbarSubsystem::GetSlotRect(const UWidget* W, FVector2D& OutPos, FVector2D& OutSize)
{
	// ★ Аудит №4 (B1): тело переехало в `AsdaUi::SlotRect` — здесь остаётся тонкий форвард
	//   (это же тело жило ПОБАЙТОВО одинаковым ещё у мини-статуса и хранителя душ).
	//   Знание «POINT-анкор импортёра: R/B = размер» теперь одно на порт.
	return AsdaUi::SlotRect(W, OutPos, OutSize);
}

void UAsdaHotbarSubsystem::SetSlotPos(UWidget* W, const FVector2D& Pos)
{
	// ★ Аудит №4 (B1): тело переехало в `AsdaUi::SetSlotPos` — здесь тонкий форвард.
	AsdaUi::SetSlotPos(W, Pos);
}

// Клон плана-картинки (эталон `CControl_InstantiateChild(src, 1, parent, 0)`).
UImage* UAsdaHotbarSubsystem::CloneImage(UImage* Src, const TCHAR* NameHint, UPanelWidget* Dest)
{
	if (!Src || !QuickWidget) { return nullptr; }
	UCanvasPanel* Canvas = Dest ? Cast<UCanvasPanel>(Dest) : Cast<UCanvasPanel>(Src->GetParent());
	UCanvasPanelSlot* SS = AsdaUi::SlotOf(Src);
	if (!Canvas || !SS) { return nullptr; }
	UImage* C = NewObject<UImage>(QuickWidget, UImage::StaticClass(),
		MakeUniqueObjectName(QuickWidget, UImage::StaticClass(), FName(NameHint)));
	C->SetBrush(Src->GetBrush());
	C->SetColorAndOpacity(Src->GetColorAndOpacity());
	C->SetVisibility(ESlateVisibility::Collapsed);
	if (UCanvasPanelSlot* CS = Canvas->AddChildToCanvas(C))
	{
		CS->SetAnchors(SS->GetAnchors());
		CS->SetAlignment(SS->GetAlignment());
		CS->SetOffsets(SS->GetOffsets());
		CS->SetZOrder(SS->GetZOrder() + 1);
	}
	return C;
}

void UAsdaHotbarSubsystem::Notice(int32 LocId) const
{
	// ★ Волна 1 (14.09.2026, В2): маршрут — фасад `AsdaNotice` (категория та же, поведение то же).
	// [ПОРТ] 09.10.2026 (мультиязычность, этап 3): RU-фолбэк убран — нет строки → плейсхолдер «[#id]» маршрутизатора.
	AsdaNotice::Chat(GetGameInstance(), EAsdaMsgCategory::ItemRefused, LocId);
	UE_LOG(LogAsdaQuick, Log, TEXT("[хотбар] нотис %d"), LocId);
}

// ★ D5 (аудит `code_dedup_audit_2.md`): разбор имени плана переехал в `AsdaUi::ParsePlaneName`
//   (одна из четырёх бывших копий формулы «два числовых хвоста с конца»).
// Первая ASCII-строка (имя текстуры) в сырой записи таблицы начиная со смещения Off.
//   Записи `*.bin` фиксированной длины, поля-строки нуль-терминированные.
// ★ D4 (аудит `code_dedup_audit_2.md`): тело в `AsdaBytes::AsciiFieldAt` (Public/AsdaBytes.h).
static FORCEINLINE FString AsciiFieldAt(const TArray<uint8>& Rec, int32 Off, int32 MaxLen = 32)
{
	return AsdaBytes::AsciiFieldAt(Rec, Off, MaxLen);
}

// Разбор hex-строки колонки `DataHex` (в неё лежит ВСЯ запись таблицы 1:1).
// ★ D4: тело в `AsdaBytes::HexToBytes`.
static FORCEINLINE bool HexToBytes(const FString& Hex, TArray<uint8>& Out) { return AsdaBytes::HexToBytes(Hex, Out); }

// ══════════════════════════════ ЖИЗНЕННЫЙ ЦИКЛ ═════════════════════════════════════════════════
void UAsdaHotbarSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency(UAsdaNetSubsystem::StaticClass());
	Collection.InitializeDependency(UAsdaWindowSubsystem::StaticClass());

	Slots.SetNum(kSlotCount);
	SentSnapshot.SetNum(kSlotCount);
	KeyLatch.SetNumZeroed(24);

	if (UAsdaNetSubsystem* Net = GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>())
	{
		Net->OnHotbarPage.AddUObject(this, &UAsdaHotbarSubsystem::OnHotbarPageReceived);
		// ★★★ Второй блок 5465 — откаты ПРЕМИУМ-предметов, которые сервер держит между сессиями.
		//   Эталон разбирает оба блока в одном обработчике `Recv_SkillCoolTime@0xE4B180`; у нас
		//   блок навыков забирает окно навыков, блок предметов — панель (реестр откатов живёт здесь,
		//   как вектор `hotbar+56` эталона).
		// ★ [ОРИГИНАЛ] 25.10.2026 — 5465 = ПОЛНЫЙ СНИМОК (шапка ключей откатов, п. 4). Делегат блока навыков приходит на
		//   КАЖДЫЙ 5465 и раньше блока предметов (AsdaNetSubsystem.cpp, `OP_NET_RECV_SKILL_COOL_TIME`), поэтому реестр
		//   предметов чистится здесь, а `ApplyServerItemCooldowns` следом строит его из пакета. Откаты навыков чистит их
		//   хозяин — книга навыков (`ApplyCooldowns`); локальные записи навыков панели (зеркала и соулмейт, 2247) не
		//   трогаем: панель спрашивает навыки через мост книги, а соулмейт ведёт свой реестр.
		Net->OnSkillCooldowns.AddWeakLambda(this, [this](const TArray<uint8>& /*SkillBlock*/)
		{
			if (!AsdaCooldownSnapshot5465On()) { return; }
			const int32 Was = ItemCooldowns.Num();
			ItemCooldowns.Reset();
			UE_LOG(LogAsdaQuick, Log, TEXT("[хотбар] 5465: снимок откатов сервера — прежние записи предметов сняты (%d), строим из пакета"), Was);
		});
		Net->OnItemCooldowns.AddUObject(this, &UAsdaHotbarSubsystem::ApplyServerItemCooldowns);
		// ★ 5452 — сервер отпустил откат предмета: синхронизируем свой таймер (см. метод).
		Net->OnFunctionItemExpired.AddUObject(this, &UAsdaHotbarSubsystem::ClearItemCooldownByCode);
		// ★ Наличие соулмейта решает, рисовать ли «серые» версии иконок (суффиксы «-g» / «_g») —
		//   эталон проверяет `gap_8B4[6] == 1 || gap_878[47] > -1`. Источник в порту — пакет 5220.
		Net->OnSoulmateState.AddWeakLambda(this, [this](int32 /*CharId*/, int32 UserIdx, int32 SoulIdx, int32 /*PartnerIdx*/)
		{
			if (AsdaSoulSlotTruthOn()) { return; }   // ★ 06.11.2026: при ключе состояние сверяет `Tick` с карточкой соулмейта
			UAsdaNetSubsystem* N = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
			if (!N || !AsdaSelf::IsSelfUserIdx(N, UserIdx)) { return; }
			const bool bNew = (SoulIdx > 0);
			if (bNew != bSelfHasSoulmate)
			{
				bSelfHasSoulmate = bNew;
				UE_LOG(LogAsdaQuick, Log, TEXT("[хотбар] соулмейт %s — иконки навыков/действий перерисованы"),
					bNew ? TEXT("есть") : TEXT("нет"));
				RebuildSlots();
				RefreshPage();
			}
		});
	}
}

void UAsdaHotbarSubsystem::Deinitialize()
{
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UAsdaNetSubsystem* Net = GI->GetSubsystem<UAsdaNetSubsystem>())
		{
			Net->OnHotbarPage.RemoveAll(this);
			Net->OnSoulmateState.RemoveAll(this);
			Net->OnItemCooldowns.RemoveAll(this);
			Net->OnSkillCooldowns.RemoveAll(this);   // ★ 25.10.2026: снимок 5465 (подписка в Initialize)
			Net->OnFunctionItemExpired.RemoveAll(this);
		}
	}
	if (AAsdaPlayerController* Pc = BoundController.Get())
	{
		Pc->OnHotbarPage.RemoveDynamic(this, &UAsdaHotbarSubsystem::OnKeyHotbarPage);
		Pc->OnHotbarSlot.RemoveDynamic(this, &UAsdaHotbarSubsystem::OnKeyHotbarSlot);
		BoundController = nullptr;
	}
	ResetWidget();
	Super::Deinitialize();
}

// ★ [ИСПРАВЛЕНИЕ] 25.10.2026: «эта причина начинает новую сессию и вещи прошлой уходят» — определено рядом с ключом
//   в `AsdaInventorySubsystem.cpp` (общая точка инвентаря, панели и склада).
bool AsdaSessionResetClearsItems(EAsdaSessionResetReason Reason);

// ★★★ К-3: СБРОС СОСТОЯНИЯ СЕССИИ (шапка `AsdaSessionReset.h`). Подсистема живёт на
//   `GameInstance` и переживает разрыв связи, выход в выбор персонажа и травел; всё, что
//   ниже, — данные и защёлки ОДНОЙ сессии. Отправок здесь нет (закон `no-reentrant-net-callbacks`).
void UAsdaHotbarSubsystem::ResetSessionState(EAsdaSessionResetReason Reason)
{
	// Очередь страниц и захват — состояние сессии соединения и умершего мира.
	// ⚠ КУЛДАУНЫ при смене карты НЕ трогаем: они отсчитываются сервером и относятся к персонажу,
	//   а не к карте; сбрасывать их на каждом портале значило бы показывать готовыми навыки,
	//   которые сервер ещё не отпустил.
	PendingPages.Clear();
	bPageActionPending = false;
	PendingPage = 0;
	PendingTargetRenderId = -1;
	bDragActive = false; DragSrcSlot = -1; DragSrcColumn = -1; bDragSrcTRow = false;
	DragArm.Disarm();
	DragSavedStackText.Empty();
	HoverSlot = -1;
	Mouse.Reset();
	// ★ ИСПРАВЛЕНО 2026-09-02 (падение при нажатии слота после сброса сессии). БЫЛО `KeyLatch.Reset()`
	//   — это делает массив ПУСТЫМ (Num=0), а он обязан всегда быть длины 24 (`SetNumZeroed(24)` в
	//   Initialize): `ActivateSlotByColumn`/`ReleaseSlotByColumn` читают `KeyLatch[KeyColumn]` по
	//   индексу 0..23 БЕЗ проверки размера. Итог — `Array index out of bounds: 0 into an array of
	//   size 0` на первом же нажатии слота после разрыва связи/входа. Обнуляем ЗНАЧЕНИЯ, а не длину.
	//   (Тот же класс, что уронил `BurstCache` хранителя душ — но `SetNumZeroed` не ловился прежним
	//   поиском по `.Init(_,N)`; см. памятку `tarray-reset-breaks-fixed-length`.)
	for (uint8& L : KeyLatch) { L = 0; }
	LastSkillDispatchS = -1.0;
	if (!AsdaIsFullSessionReset(Reason)) { return; }
	UE_LOG(LogAsdaQuick, Log, TEXT("[панель] сброс сессии (%s): страница и кулдауны сняты"),
		AsdaSessionResetReasonName(Reason));
	SkillCooldowns.Reset();
	ItemCooldowns.Reset();
	WasCoolingSlots.Reset();
	// ★ ИСПРАВЛЕНИЕ 11.10.2026 (дедуп №2, В2-B; тот же дефект, что у чата — `wave2_A.md` п.0): окно 2900 ЗАКРЫВАЕТСЯ В РЕЕСТРЕ.
	//   Было: сброс только забывал указатель (`ResetWidget`), окно оставалось живым в реестре; при сбросе без смены мира
	//   (разрыв связи) следующий `EnsureWidget` получал от `OpenWindowByPath` ТОТ ЖЕ виджет и `SetupControls` строил второй
	//   набор клонов планов (`CloneImage`) и второе реле страниц поверх старого дерева: узлов 607 → 663 → 719, привязок
	//   2 → 4 → 6 после двух сбросов (`asda.UI.TreeCensus`, `w2B_logs`). Стало: окно снимается с реестра и вьюпорта (тихо),
	//   `EnsureWidget` на ближайшем тике создаёт НОВЫЙ экземпляр — как после травела. Ключа отката нет: прежнее — порча дерева.
	if (UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->CloseWindowSilently(kQuickWindowId);
	}
	ResetWidget();
	CurrentPage = 0;
	PrevPage = 0;
	bSelfHasSoulmate = false;
	bSelfMateInGame = false;
	// ★ [ИСПРАВЛЕНИЕ] 25.10.2026 — перезаход другой учёткой (`Reports/inventory_relog_leak/REPORT.md`): раскладка
	//   78 слотов — данные ПЕРСОНАЖА. Эталон держит её в глобальном `aSlotData`, который рождается пустым с процессом
	//   (при выходе клиент закрывается), а 5059 перезаписывает только ПРИШЕДШИЕ страницы (`Hotbar_LoadPage@0xDEA3D0`).
	//   Без очистки страница, которую сервер новому персонажу не прислал, показывала иконки прошлого.
	//   Снимок «отправлено» чистим вместе со слотами: пусто = пусто ⇒ `SendAllPageLayouts` не шлёт 5067 «очистить».
	//   Длина фиксированная (78, `SetNum` в `Initialize`) — чистим значения. Причины и ключ — общие с инвентарём.
	//   5059 приходит после логона на game-сервер, то есть ПОСЛЕ 1021, — данных новой сессии здесь ещё нет.
	if (AsdaSessionResetClearsItems(Reason))
	{
		for (FAsdaHotbarSlot& S : Slots) { S.Clear(); }
		for (FAsdaHotbarSlot& S : SentSnapshot) { S.Clear(); }
		UE_LOG(LogAsdaQuick, Log, TEXT("[панель] сброс сессии (%s): раскладка слотов прошлой сессии снята"),
			AsdaSessionResetReasonName(Reason));
	}
}

void UAsdaHotbarSubsystem::ResetWidget()
{
	QuickWidget = nullptr;
	// ★ Аудит №3 (B4): призрак живёт в дереве ЭТОГО виджета — при пересоздании окна забываем его
	//   целиком (как `ScrollBar = FAsdaScrollBarNomal()` у инвентаря), иначе `Begin` увидит старый
	//   `UImage` из мёртвого дерева и не соберёт новый.
	DragGhost = FAsdaDragGhost();
	MainUi.Reset();
	TUi.Reset();
	PageDigits.Reset();
	PageDigit6 = nullptr;
	BtnPageUp = nullptr; BtnPageDown = nullptr;
	RowMainBase = nullptr; RowTBase = nullptr; RowTAnchor = nullptr;
	KeysBase1 = nullptr; KeysBase2 = nullptr;
	KeyGlyphs.Reset();
	NoticeUi.Reset();
	Relays.Reset();
	bControlsReady = false;
	DragSrcSlot = -1; DragSrcColumn = -1; bDragActive = false; bDragSrcTRow = false;
	// ★ Взвод тоже снимаем — см. ту же причину в `UAsdaInventorySubsystem` (пересборка окна).
	DragArm.Disarm();
	HoverSlot = -1; bTipShown = false;
}

bool UAsdaHotbarSubsystem::EnsureWidget()
{
	UGameInstance* GI = GetGameInstance();
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM) { return false; }
	if (WM->IsWindowLive(QuickWidget))
	{
		return true;
	}
	ResetWidget();
	// Корень SelfHitTestInvisible: растянутый на вьюпорт UUserWidget с Visible съедает клики по миру.
	QuickWidget = WM->OpenWindowByPath(kQuickWindowId, kQuickWbp, kQuickZ, ESlateVisibility::SelfHitTestInvisible);
	if (!QuickWidget)
	{
		UE_LOG(LogAsdaQuick, Error, TEXT("[хотбар] WBP_QUICK_NEW_WIN не загрузился (%s)"), kQuickWbp);
		return false;
	}
	SetupControls();
	RebuildSlots();
	RefreshPage();
	RebuildKeyHints();
	UpdatePageDigits();
	SetSecondRowVisible(bSecondRowVisible);
	SetPanelActive(bPanelActive);
	UE_LOG(LogAsdaQuick, Log, TEXT("[хотбар] окно открыто (z=%d), контролы разобраны: %s"),
		kQuickZ, bControlsReady ? TEXT("да") : TEXT("НЕТ"));
	return true;
}

// ══════════════════════════════ РАЗБОР ДЕРЕВА ВИДЖЕТОВ ═════════════════════════════════════════
//   Имена — по закону jui-importer-widget-naming: план = `{idc}_{juiIdx}_{plane}`, интерактив/Text =
//   точное имя idc, группа = `{idc}_grp`. Скин `NoImage_icon` у слотов имеет Type=Text, поэтому
//   генератор создал на каждый слот ПАРУ: план-картинку (иконка) и TextBlock (СЧЁТЧИК СТАКА).
void UAsdaHotbarSubsystem::SetupControls()
{
	bControlsReady = false;
	if (!QuickWidget || !QuickWidget->WidgetTree) { return; }

	MainUi.SetNum(kColsPerPage);
	TUi.SetNum(kTRowCols);

	TArray<UWidget*> All;
	QuickWidget->WidgetTree->GetAllWidgets(All);

	// Анимированные планы каждой колонки складываем в корзину и раскладываем ПО ЧИСЛУ КАДРОВ (Д1).
	TArray<UAsdaAnimatedPlane*> MainAnim[kColsPerPage];
	TArray<UAsdaAnimatedPlane*> TAnim[kTRowCols];
	UImage* DimBase1 = nullptr;
	UImage* DimBase2 = nullptr;
	UImage* NoticeIcon[7] = {};
	UAsdaAnimatedPlane* NoticeSel[7] = {};

	for (UWidget* W : All)
	{
		if (!W) { continue; }
		const FString N = W->GetName();

		// ── интерактив (кнопки страниц) ──
		if (UButton* Btn = Cast<UButton>(W))
		{
			if (N == TEXT("QUICK_NEW_UPBUT"))   { BtnPageUp = Btn;   continue; }
			if (N == TEXT("QUICK_NEW_DOWNBUT")) { BtnPageDown = Btn; continue; }
			continue;
		}

		// ── счётчики стака (TextBlock с точным именем idc) ──
		if (UTextBlock* Tb = Cast<UTextBlock>(W))
		{
			Tb->SetAutoWrapText(false);                       // закон ui-autowrap-canvas-gotcha
			for (int32 C = 0; C < 12; ++C)
			{
				if (N == FString::Printf(TEXT("QUICK_NEW_SLOT%d"), C + 1)) { MainUi[C].StackText = Tb; break; }
			}
			if (N == TEXT("QUICK_NEW_SLOT_WEAPON")) { MainUi[kWeaponCol].StackText = Tb; }
			for (int32 C = 0; C < kTRowCols; ++C)
			{
				if (N == FString::Printf(TEXT("QUICK_NEW_SLOT_T%d"), C + 1)) { TUi[C].StackText = Tb; break; }
			}
			// Счётчик пуст, пока в слоте ничего нет.
			Tb->SetText(FText::GetEmpty());
			continue;
		}

		// ── группы / якоря ──
		if (N == TEXT("QUICK_NEW_SlotBtnBase_1_grp")) { RowMainBase = W; continue; }
		if (N == TEXT("QUICK_NEW_SlotBtnBase_2_grp")) { RowTBase = W;    continue; }
		if (N == TEXT("QUICK_NEW_SlotBase_2_grp"))    { RowTAnchor = W;  continue; }
		if (N == TEXT("Quick_Keys_Base_1_grp"))       { KeysBase1 = W;   continue; }
		if (N == TEXT("Quick_Keys_Base_2_grp"))       { KeysBase2 = W;   continue; }

		// ── планы ──
		FString Idc; int32 JuiIdx = 0, PlaneIdx = 0;
		if (!AsdaUi::ParsePlaneName(N, Idc, JuiIdx, PlaneIdx)) { continue; }
		UAsdaAnimatedPlane* Anim = Cast<UAsdaAnimatedPlane>(W);
		UImage* Img = Cast<UImage>(W);
		if (!Img) { continue; }

		// иконки основного ряда
		bool bTaken = false;
		for (int32 C = 0; C < 12 && !bTaken; ++C)
		{
			if (Idc == FString::Printf(TEXT("QUICK_NEW_SLOT%d"), C + 1)) { MainUi[C].Icon = Img; bTaken = true; }
		}
		if (bTaken) { continue; }
		if (Idc == TEXT("QUICK_NEW_SLOT_WEAPON")) { MainUi[kWeaponCol].Icon = Img; continue; }
		for (int32 C = 0; C < kTRowCols; ++C)
		{
			if (Idc == FString::Printf(TEXT("QUICK_NEW_SLOT_T%d"), C + 1)) { TUi[C].Icon = Img; bTaken = true; break; }
		}
		if (bTaken) { continue; }

		// шторки/вспышки основного ряда (idc: CEND1..CEND12 + CEN10-опечатка, COOL1..COOL12,
		//   у оружия CEND13/COOL13 со СПУТАННЫМИ скинами)
		if (Anim)
		{
			for (int32 C = 0; C < 12 && !bTaken; ++C)
			{
				const FString A1 = FString::Printf(TEXT("QUICK_NEW_CEND%d"), C + 1);
				const FString A2 = FString::Printf(TEXT("QUICK_NEW_CEN%d"), C + 1);   // ★ idc-опечатка у 10-го
				const FString A3 = FString::Printf(TEXT("QUICK_NEW_COOL%d"), C + 1);
				if (Idc == A1 || Idc == A2 || Idc == A3) { MainAnim[C].Add(Anim); bTaken = true; }
			}
			if (bTaken) { continue; }
			if (Idc == TEXT("QUICK_NEW_CEND13") || Idc == TEXT("QUICK_NEW_COOL13"))
			{
				// ★ Д1: у слота ОРУЖИЯ скины перепутаны. Разложим по числу кадров ниже; при
				//   asda.Quick.LegacySkinSwap 1 порядок берём как в JUI (по имени).
				MainAnim[kWeaponCol].Add(Anim);
				continue;
			}
			for (int32 C = 0; C < kTRowCols && !bTaken; ++C)
			{
				const FString B1 = FString::Printf(TEXT("QUICK_NEW_T1_CEND%d"), C + 1);
				const FString B2 = FString::Printf(TEXT("QUICK_NEW_T1_COOL%d"), C + 1);
				if (Idc == B1 || Idc == B2) { TAnim[C].Add(Anim); bTaken = true; }
			}
			if (bTaken) { continue; }
			// мигающая рамка иконки уведомления
			for (int32 I = 0; I < 7; ++I)
			{
				if (Idc == FString::Printf(TEXT("P_ICONSEL%02d_N"), I)) { NoticeSel[I] = Anim; bTaken = true; break; }
			}
			if (bTaken) { continue; }
		}

		// затемнение
		if (Idc == TEXT("QUICK_NEW_Disable1"))   { DimBase1 = Img; continue; }
		if (Idc == TEXT("QUICK_NEW_Disable_2"))  { DimBase2 = Img; continue; }

		// цифры страницы
		for (int32 D = 0; D < 5; ++D)
		{
			if (Idc == FString::Printf(TEXT("QUICK_NEW_NUM%d"), D + 1))
			{
				PageDigits.SetNum(FMath::Max(PageDigits.Num(), 5));
				PageDigits[D] = Img; bTaken = true; break;
			}
		}
		if (bTaken) { continue; }
		if (Idc == TEXT("QUICK_NEW_NUM6")) { PageDigit6 = Img; continue; }

		// иконки уведомлений
		for (int32 I = 0; I < 7; ++I)
		{
			if (Idc == FString::Printf(TEXT("P_ICON%02d_N"), I)) { NoticeIcon[I] = Img; bTaken = true; break; }
		}
		if (bTaken) { continue; }

		// ── глифы клавиш ряда 1: ключ = idnum. Порядковый индекс в .JUI однозначен, idc — нет
		//   (санитайзер схлопывает «/», «`», «-», … в одно имя «NEW_Key__»). Соответствие:
		//   JuiIdx 81..144 → idnum 73080..73143, JuiIdx 145 = noimage (idnum −1), 146 → 73144.
		if (Idc.StartsWith(TEXT("NEW_Key")) && !Idc.StartsWith(TEXT("NEW_T_Key")))
		{
			int32 IdNum = 0;
			if (JuiIdx >= 81 && JuiIdx <= 144) { IdNum = 73080 + (JuiIdx - 81); }
			else if (JuiIdx == 146)            { IdNum = 73144; }
			if (IdNum != 0) { KeyGlyphs.Add(FString::FromInt(IdNum), Img); }
			SetVis(Img, false);
			continue;
		}
		// Глифы T-таблицы (73230..73293) в коде эталона НЕ используются — подписи T-ряда берутся из
		//   ОСНОВНОЙ таблицы. Прячем и оставляем как есть (там же живёт дефект Д5 — сдвиг имён F11/F12).
		if (Idc.StartsWith(TEXT("NEW_T_Key"))) { SetVis(Img, false); continue; }
	}

	// ── Раскладка анимированных планов по РОЛИ (число кадров) ──
	const bool bLegacySwap = (CVarLegacySkinSwap.GetValueOnAnyThread() != 0);
	auto AssignAnim = [&](TArray<UAsdaAnimatedPlane*>& Bin, FAsdaHotbarSlotUi& Ui, const TCHAR* Tag)
	{
		for (UAsdaAnimatedPlane* A : Bin)
		{
			if (!A) { continue; }
			const bool bIsCurtain = (A->TotalFrame == kCurtainFrames);
			const bool bIsFlash   = (A->TotalFrame == kFlashFrames);
			if (bIsCurtain)   { Ui.Cool.Plane = A; }
			else if (bIsFlash){ Ui.Flash = A; }
			else              { UE_LOG(LogAsdaQuick, Warning, TEXT("[хотбар] %s: план с TotalFrame=%d не опознан"), Tag, A->TotalFrame); }
			A->bLoop = false;                 // оба скина в UI играем как one-shot (управляем вручную)
			A->MaxLoops = 1;
			SetVis(A, false);
		}
		if (bLegacySwap) { Swap(Ui.Cool.Plane, Ui.Flash); }   // вернуть дефект оригинала Д1
	};
	for (int32 C = 0; C < kColsPerPage; ++C) { AssignAnim(MainAnim[C], MainUi[C], *FString::Printf(TEXT("колонка %d"), C)); }
	for (int32 C = 0; C < kTRowCols;   ++C) { AssignAnim(TAnim[C],    TUi[C],    *FString::Printf(TEXT("T-колонка %d"), C)); }

	// ── Затемняющие оверлеи. Эталон: база + клоны, каждый ставится в позицию СВОЕГО слота.
	//   Ряд 1: база 73151 + клоны i=1..12 = 13 штук на 13 колонок.
	//   Ряд 2: база 73300 + клоны j=1..11 = 12 штук на 12 колонок.
	//   (Реверс А §5.2 счёл ряд 2 «11 клонов вместо 12» дефектом — это ЛОЖНАЯ ТРЕВОГА:
	//    базовый контрол сам обслуживает колонку 0, итог 12 на 12.)
	//   ⚠ Координаты в UMG ЛОКАЛЬНЫ для канваса: импортёр положил каждый слот в СВОЮ группу
	//   `{idc}_grp`, а базовый оверлей затемнения лежит рядом с группами, в канвасе РЯДА. Поэтому
	//   позиция оверлея колонки = позиция ГРУППЫ слота (в канвасе ряда) + позиция иконки внутри группы.
	auto FillRow = [&](TArray<FAsdaHotbarSlotUi>& Row, int32 N, UImage* DimBase, const TCHAR* CloneName)
	{
		for (int32 C = 0; C < N; ++C)
		{
			FAsdaHotbarSlotUi& U = Row[C];
			if (!U.Icon) { continue; }
			U.Group = Cast<UPanelWidget>(U.Icon->GetParent());
			FVector2D P, S;
			if (GetSlotRect(U.Icon, P, S)) { U.IconBasePos = P; U.IconBaseSize = S; }
			FVector2D GP, GS;
			if (U.Group && GetSlotRect(U.Group, GP, GS)) { U.GroupPos = GP; }
			UImage* Dim = (C == 0) ? DimBase : CloneImage(DimBase, CloneName);
			if (Dim) { SetSlotPos(Dim, U.GroupPos + U.IconBasePos); SetVis(Dim, false); }
			U.Dim = Dim;
			// ★ Счётчик стака оформляем ПОСЛЕ того, как известна геометрия ячейки (нужна её высота).
			ApplyStackTextStyle(U.StackText, U);
		}
	};
	FillRow(MainUi, kColsPerPage, DimBase1, TEXT("QUICK_NEW_Disable1_clone"));
	FillRow(TUi,    kTRowCols,   DimBase2, TEXT("QUICK_NEW_Disable_2_clone"));

	// ── Иконки уведомлений: в JUI все Visible=0, эталон показывает по факту события ──
	NoticeUi.SetNum(7);
	for (int32 I = 0; I < 7; ++I)
	{
		NoticeUi[I].Icon = NoticeIcon[I];
		NoticeUi[I].SelFrame = NoticeSel[I];
		// ★★★ Группа-родитель слота (`P_ICON0X_N_grp`) — её импортёр свернул в COLLAPSED, потому что
		//   в `.JUI` у 73350…73356 `Visible = "0"`, а дети (рамка 7336X) есть. Берём её тем же
		//   способом, что и у обычных ячеек хотбара (`U.Group = U.Icon->GetParent()`).
		NoticeUi[I].Group = NoticeIcon[I] ? NoticeIcon[I]->GetParent() : nullptr;
		NoticeUi[I].NoticeType = -1;
		SetVis(NoticeUi[I].Group, false);
		SetVis(NoticeIcon[I], false);
		SetVis(NoticeSel[I], false);
	}

	// ── Кнопки страниц (событие 66561 эталона → Hotbar_PageUpDown) ──
	auto BindPageBtn = [&](UButton* Btn, bool bUp)
	{
		if (!Btn) { return; }
		AsdaUi::BindButton(this, Btn, bUp ? 1 : 0, [this](int32 Up) { PageUpDown(Up != 0); }, Relays);   // ★ U20: реле клика — общая точка
	};
	BindPageBtn(BtnPageUp, true);
	BindPageBtn(BtnPageDown, false);

	// Цифра-дубликат NUM6 в код эталона НЕ грузится (цикл 73321..73325) — держим скрытой.
	SetVis(PageDigit6, false);

	int32 Found = 0;
	for (const FAsdaHotbarSlotUi& U : MainUi) { Found += (U.Icon ? 1 : 0); }
	for (const FAsdaHotbarSlotUi& U : TUi)    { Found += (U.Icon ? 1 : 0); }
	bControlsReady = (Found == kColsPerPage + kTRowCols);
	++WidgetGeneration;
	UE_LOG(LogAsdaQuick, Log,
		TEXT("[хотбар] разбор: слотов-иконок %d/%d, цифр страниц %d, глифов клавиш %d, кнопки %s/%s"),
		Found, kColsPerPage + kTRowCols, PageDigits.Num(), KeyGlyphs.Num(),
		BtnPageUp ? TEXT("есть") : TEXT("НЕТ"), BtnPageDown ? TEXT("есть") : TEXT("НЕТ"));
	if (!bControlsReady)
	{
		UE_LOG(LogAsdaQuick, Warning, TEXT("[хотбар] не все слоты найдены — проверь имена узлов в WBP_QUICK_NEW_WIN"));
	}
}

// ══════════════════════════════ АДРЕСАЦИЯ ══════════════════════════════════════════════════════
int32 UAsdaHotbarSubsystem::AbsSlot(int32 Column, bool bTRow) const
{
	if (bTRow)
	{
		return (Column >= 0 && Column < kTRowCols) ? (kTRowFirst + Column) : -1;
	}
	return (Column >= 0 && Column < kColsPerPage) ? (Column + kColsPerPage * CurrentPage) : -1;
}

static FAsdaHotbarSlot GEmptySlot;

const FAsdaHotbarSlot& UAsdaHotbarSubsystem::GetSlot(int32 AbsIndex) const
{
	if (Slots.IsValidIndex(AbsIndex)) { return Slots[AbsIndex]; }
	return GEmptySlot;
}

// ══════════════════════════════ TICK ═══════════════════════════════════════════════════════════
void UAsdaHotbarSubsystem::Tick(float DeltaTime)
{
	ASDA_PERF_SCOPE("Tick.Hotbar");   // ★ R4-b (14.11.2026): область замера (AsdaPerf.h; в Shipping пусто)
	UGameInstance* GI = GetGameInstance();
	if (!GI) { return; }
	UAsdaLoadManagerSubsystem* LM = GI->GetSubsystem<UAsdaLoadManagerSubsystem>();
	UAsdaWorldSubsystem* WS = GI->GetSubsystem<UAsdaWorldSubsystem>();
	// Хотбар живёт только В МИРЕ (эталон: CHotbar_InitUI зовётся при входе, g_GameState == 1).
	if (!LM || LM->CurrentMapId < 0 || !WS || !IsValid(WS->LocalPlayerActor)) { return; }
	EnsureKeyBinding();    // контроллер появляется позже подсистемы — подписываемся лениво

	// ★ 06.11.2026 (`asda.Soul.SlotTruth`): пара появилась/разорвана, Друг вошёл/вышел — перерисовать иконки. Эталон делает это из
	//   обработчиков 5110/5207/5220 (`skillHotbarRefreshSlots`, `hotbarRebuildSlots`); сверка кадра не зависит от порядка подписок.
	if (AsdaSoulSlotTruthOn())
	{
		const bool bPair = HasSoulmate();
		const bool bInGame = IsFriendInGame();
		if (bPair != bSelfHasSoulmate || bInGame != bSelfMateInGame)
		{
			bSelfHasSoulmate = bPair;
			bSelfMateInGame = bInGame;
			UE_LOG(LogAsdaQuick, Log, TEXT("[хотбар] соулмейт: пара %s, Друг %s — иконки перерисованы"),
				bPair ? TEXT("есть") : TEXT("нет"), bInGame ? TEXT("в игре") : TEXT("вне игры"));
			RebuildSlots();
			RefreshPage();
			if (UAsdaCharaInfoSubsystem* CI = GI->GetSubsystem<UAsdaCharaInfoSubsystem>()) { CI->RefreshEmotionIcons(); }
		}
	}

	// ★★★ ПОДПИСИ КЛАВИШ НА ЯЧЕЙКАХ. `RebuildKeyHints()` зовётся из `OpenWindow`, но контроллер
	//   (а значит и раскладка `Shortcutkey.ini`) поднимается ПОЗЖЕ подсистемы: на тот момент
	//   `Pc->Keys` = nullptr, все 24 ячейки получали глиф «не назначено» — и больше никто их
	//   не пересобирал. Отсюда «клавиш на панели нет». Дособираем, как только раскладка появилась,
	//   и пересобираем, если её перечитали с диска (`asda.Keybinds.Reload` меняет счётчик ревизии).
	if (bControlsReady)
	{
		const UWorld* Wld = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
		const AAsdaPlayerController* PCk = Wld ? Cast<AAsdaPlayerController>(Wld->GetFirstPlayerController()) : nullptr;
		if (PCk && PCk->Keys)
		{
			ASDA_PERF_SCOPE("Hotbar.KeyHints");   // ★ R4-b 14.11.2026: область замера (AsdaPerf.h; в Shipping пусто)
			// ★ R4-b 14.11.2026 (B6): хэш пути по кэшу (пересчёт при смене пути). ★ R4-b закрытие (15.11.2026): старый путь и ключ удалены — только новое (решение владельца).
			const uint32 PathHash = AsdaHotbarPathHashCached(PCk->Keys->SourcePath, HintPathCache, HintPathHash, bHintPathCached);
			const uint32 Rev = PathHash ^ (uint32)PCk->Keys->Binds.Num()
			                 ^ ((uint32)PCk->Keys->Source << 24);
			if (!bKeyHintsBound || Rev != KeyHintsRevision)
			{
				KeyHintsRevision = Rev;
				bKeyHintsBound = true;
				RebuildKeyHints();
			}
		}
	}

	// ★ Мост К ОКНУ НАВЫКОВ: иконка навыка (с суффиксом «-g» у невыученного), вид цели, кулдаун,
	//   подсказка, авто-подмена уровня. Ставится ЛЕНИВО и РОВНО ОДИН раз — `SetSkillBridge`
	//   внутри себя зовёт `RebuildSlots()+RefreshPage()`, и повтор каждый кадр перетирал бы модель.
	//   Подключаем со стороны панели: окно навыков поднимается независимо и может появиться позже.
	if (!bSkillBridgeBound)
	{
		if (UAsdaSkillWindowSubsystem* SW = GI->GetSubsystem<UAsdaSkillWindowSubsystem>())
		{
			SetSkillBridge(SW->MakeHotbarBridge());
			bSkillBridgeBound = true;
			UE_LOG(LogAsdaQuick, Log, TEXT("[хотбар] мост к окну навыков подключён — иконки и применение навыков доступны"));
		}
	}

	if (!EnsureWidget()) { return; }

	// ★ Снятие защёлки клавиш РАЗ В КАДР. В эталоне её снимает второй `switch` того же
	//   `hotkeyDispatchAction` (обработка отпускания), но у нашего контроллера события отпускания
	//   НЕТ — `PollKeys` стреляет ровно один раз по фронту. Без этого сброса слот сработал бы
	//   единственный раз за сессию.
	// ★ R4-b 14.11.2026 (B7): защёлку обнуляет один проход ниже (прежде — два одинаковых подряд). ★ R4-b закрытие (15.11.2026): старый путь и ключ удалены — только новое (решение владельца).

	// ★ Подписка на клавиши контроллера живёт в `EnsureKeyBinding()` (вызывается выше по тику).
	//   ⚠ Здесь был ВТОРОЙ, дублирующий блок подписки: панель и раскладку делали два агента
	//   параллельно, и стык каждый сделал по-своему ⇒ одно нажатие срабатывало ДВАЖДЫ
	//   (страница переключалась через одну, слот применялся дважды). Оставлен один путь.
	//   ★ Ловушка, из-за которой это легко проглядеть: имя `OnHotbarPage` занято ДВАЖДЫ и в
	//   РАЗНЫХ смыслах — у `UAsdaNetSubsystem` это «с сервера пришла страница 5059», у
	//   `AAsdaPlayerController` — «нажата клавиша страницы».

	// ★★★ СБРОС АНТИДРЕБЕЗГА РАЗ В КАДР (2026-08-08). Эталон снимает защёлку `CHotbar+0x5FA0`
	//   вторым switch'ем того же вызова — при ОТПУСКАНИИ клавиши. У нас отпускания нет как события:
	//   `AAsdaPlayerController::PollKeys` стреляет РОВНО ОДИН РАЗ по фронту отпускания, то есть
	//   каждый импульс уже одиночный. Если защёлку не снимать, `ActivateSlotByColumn` пропустит
	//   ВТОРОЕ и все последующие нажатия — слот сработал бы один раз за сессию.
	//   Снимаем в начале кадра, СЛЕДУЮЩЕГО за нажатием: защита от двойного срабатывания внутри
	//   одного кадра сохраняется (мышь + клавиша), а повторные нажатия проходят.
	for (int32 i = 0; i < KeyLatch.Num(); ++i) { KeyLatch[i] = 0; }

	ApplyPendingPages();   // ★ 5059 применяется ЗДЕСЬ, а не в колбэке пакета

	// ★ 26.10.2026 [ИСПРАВЛЕНИЕ] (шапка `asda.Quick.GreyUnlearnedSkill`): список выученного изменился (сброс свитком, изучение,
	//   смена профессии, сброс сессии) — пересобрать ТОЛЬКО слоты навыков (тип 3): серая/обычная иконка по уровню. Слоты предметов
	//   не трогаем (их пересборка чистит опустевшие ячейки). Раскладка не меняется — 5067 не уходит.
	{
		const uint32 Rev = AsdaSkillLearnedRevision();
		uint32& Seen = GHotbarSeenSkillRev.FindOrAdd(TObjectKey<UAsdaHotbarSubsystem>(this), Rev);
		if (Seen != Rev)
		{
			Seen = Rev;
			int32 N = 0;
			for (int32 I = 0; I < Slots.Num(); ++I)
			{
				if (Slots[I].Type == (uint8)EAsdaHotbarSlotType::Skill) { RebuildOneSlot(I); ++N; }
			}
			if (N > 0)
			{
				RefreshPage();
				UE_LOG(LogAsdaQuick, Log, TEXT("[хотбар] список навыков изменился (ревизия %u) — слотов навыков перекрашено %d"), Rev, N);
			}
		}
	}
	TickPageAction();      // отложенное действие персонажа 104 (смена страницы + набора оружия)

	// Обновление шторок перезарядки — раз в кадр, как эталон делает это в цикле перерисовки.
	{
		ASDA_PERF_SCOPE("Hotbar.Cooldowns");   // ★ R4-b 14.11.2026: область замера (AsdaPerf.h; в Shipping пусто)
		// ★ R4-b 14.11.2026 (B5): мир и часы реестра — раз за кадр. ★ R4-b закрытие (15.11.2026): старый путь и ключ удалены — только новое (решение владельца).
		const UWorld* CdWorld = GI->GetWorld();
		const double CdNow = CdWorld ? AsdaCooldownNowS(GI) : 0.0;
		for (int32 C = 0; C < MainUi.Num(); ++C) { UpdateCooldownVisualAt(C, false, CdWorld, CdNow); }
		if (bSecondRowVisible)
		{
			for (int32 C = 0; C < TUi.Num(); ++C) { UpdateCooldownVisualAt(C, true, CdWorld, CdNow); }
		}
	}

	TickPointer();
	// ★ Пульсация иконок уведомлений (порт таймеров 18…24 по 500 мс) — после указателя,
	//   чтобы снятая этим же кадром иконка уже не мигала.
	TickNoticeBlink(DeltaTime);
}
