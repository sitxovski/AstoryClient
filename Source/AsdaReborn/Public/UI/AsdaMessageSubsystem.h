// Copyright (c) 2026 @sitxovski (Discord)
// UAsdaMessageSubsystem — ЕДИНЫЙ МАРШРУТИЗАТОР ВСЕХ ЭКРАННЫХ СООБЩЕНИЙ КЛИЕНТА.
//
// ═══════════════════════════════════ ЗАЧЕМ ЭТО ЕСТЬ ════════════════════════════════════════════
// До этого круга каждый источник сам решал, куда писать: World звал AddWarMessageById, лут звал
// AddSystemMessage, HUD звал ShowHeroNotice/ShowServerNotice. Получался НАБОР ОДИНАКОВЫХ ФУНКЦИЙ
// без общей политики: цвет/TTL/парность дублировались по коду, а любое изменение маршрута требовало
// правок в десятке мест и пересборки.
//
// Здесь — ОДНА точка входа `Post(FAsdaMsgParams)`. Вызывающий сообщает ТОЛЬКО СМЫСЛ события
// (категорию) и текст (готовый или loc-id). Куда сообщение попадёт, каким цветом, на сколько
// секунд и со звуком — решает ТАБЛИЦА МАРШРУТИЗАЦИИ В ДАННЫХ (DataTable /Game/AsdaAssets/Data/
// DT_MessageRoutes (СЫРОГО CSV в Content больше НЕТ — источник живёт в RawAssets/Data/), при отсутствии
// и его — встроенные умолчания). Таблица правится БЕЗ ПЕРЕСБОРКИ; `asda.Msg.Reload` перечитывает.
//
// ═════════════════ ЦЕЛЕВАЯ МАРШРУТИЗАЦИЯ (РЕШЕНИЕ ЮЗЕРА 2026-08-04) ════════════════════════════
//   ★★★ ПОПРАВКА (после скрина «Битва началась»): военные события идут в ОКНО С РАМКОЙ —
//   NOTICE_WIN (228), а НЕ в бесфоновый верхний список 221 и БЕЗ дубля в журнал.
//   военные/кампвар (старт/конец битвы, захваты, зоны)   → NOTICE_WIN (228), окно с рамкой
//   объявления администраторов (серверный push 3113)     → NOTICE_WIN (228)
//   редкая добыча ДРУГИХ игроков (6131)                  → HERO_NOTICE (83400)
//   ВСЁ остальное системное (отказы движения/атаки/замаха, «нет стрел», «недоступная область»,
//     «сделан скриншот», «прочность упала до 0», разрыв связи, подбор, подсказки, торговля,
//     квесты, дуэли, прогресс)                           → СИСТЕМНЫЙ ЧАТ (журнал 2570/2572)
//   парные вызовы оригинала (104 из 316)                 → ТОЛЬКО системный чат
//
// ★ ОСОЗНАННОЕ ОТСТУПЛЕНИЕ ОТ ЭТАЛОНА (фиксируется явно, закон never-invent-only-original):
//   в оригинале 212 из 316 сайтов пишут в WARMESSAGE_WIN СОЛО — включая все серверные отказы
//   (движение 4007, бой 4027/4029), «нет стрел», «недоступная область» и т. п. Юзер решил, что
//   верхний канал 221 остаётся ТОЛЬКО за военными/кампвар-событиями, а весь остальной feedback
//   уходит в системный чат. Полная разметка всех 316 сайтов «оригинал → наш маршрут» —
//   Reports/warmsg_callsites.tsv (колонка target_user).
//   Отступление обратимо БЕЗ ПЕРЕСБОРКИ: достаточно поставить бит WarWindow нужным категориям
//   в таблице маршрутизации.
//
// ★ СУДЬБА ОКНА 221 (WARMESSAGE_WIN): после поправки ни одна категория в него больше не пишет.
//   Канал НЕ УДАЛЁН (окно, строки, TTL, центрирование, тень, команды asda.War.* на месте) — он
//   поднимается пустым и потому НЕВИДИМ (скин Action_List, единственный план PlaneAlwaysInvisible=1:
//   фона нет, а строк ноль). Чтобы им снова пользоваться, достаточно бита WarWindow (2) у любой
//   категории в DT_MessageRoutes + `asda.Msg.Reload` — без пересборки.
//
// ═══════════════════════════════ ПРИЁМНИКИ (низкоуровневые) ════════════════════════════════════
//   SystemChat → UAsdaChatSubsystem::AddSystemMessage (эталон CChat_AddTokenizedLines: журнал 2572
//                + невидимый буфер A/B 2557/2558);
//   WarWindow  → UAsdaChatSubsystem::AddWarMessage    (эталон CControl_SetTooltipText: список 221,
//                3 строки, центр, без переноса, тень);
//   NoticeWin  → UAsdaHudSubsystem::ShowServerNotice  (эталон 3113: режимы 0/1/100);
//   HeroNotice → UAsdaHudSubsystem::ShowHeroNotice    (эталон uiShowTimedNoticeText: очередь, 10 с);
//   ChatRibbon → UAsdaChatWindowSubsystem::AddAnnounceLine (эталон Chat_AddChatLine канал 10 для
//                редкой добычи и uiChatLogAddLine слот 255 для объявлений 3113 — см. enum ниже);
//   Sound      → UAsdaSoundSubsystem::PlaySound2D     (эталон CSoundFlag_PlaySound2D, «error.wav»).
// Эти функции — СТОКИ. Прикладной код их больше НЕ зовёт: только через Post.
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "UI/AsdaMsgCategory.h"   // ★ R1: EAsdaMsgCategory — категория события (вынесена отсюда)
#include "AsdaMessageSubsystem.generated.h"

// ── ★ R1 (10.11.2026): категория события `EAsdaMsgCategory` вынесена дословно в `UI/AsdaMsgCategory.h` (включён выше). ──

// ── Приёмники (битовая маска). Одно сообщение может уйти сразу в несколько окон. ──
UENUM(BlueprintType, meta = (Bitflags, UseEnumValuesAsMaskValuesInEditor = "true"))
enum class EAsdaMsgTarget : uint8
{
	None       = 0      UMETA(Hidden),
	SystemChat = 1      UMETA(DisplayName = "Системный чат (журнал 2572)"),
	WarWindow  = 2      UMETA(DisplayName = "WARMESSAGE_WIN (221)"),
	NoticeWin  = 4      UMETA(DisplayName = "NOTICE_WIN (228)"),
	HeroNotice = 8      UMETA(DisplayName = "HERO_NOTICE (83400)"),
	TrackB     = 16     UMETA(DisplayName = "Буфер B (2558) вместо A — значимое событие"),
	// ★★★ ДУБЛЬ ОПОВЕЩЕНИЯ СТРОКОЙ В ЖИВУЮ ЛЕНТУ ЧАТА (CHAT_NEW 2500, список 2521).
	//   ЭТАЛОН ЭТО ДЕЛАЛ САМ, в двух местах и обоими способами:
	//     • редкая добыча — `Chat_AddChatLine(g_pChatWnd, /*канал*/10, msg, −16711936 /*зелёный*/, …)`
	//       (item_inventory.c:39570 свой подбор/гача/заточка; network.c:3750 чужой, приём 6131).
	//       Канал 10 у `Chat_AddChatLine @0xBB14C0` ветки не имеет ⇒ `default:` оставляет
	//       инициализаторы (chat.c:5024-5026): colorType = 4 (слот строки), needPrefix = 0
	//       (ПРЕФИКСА НЕТ), textColor = a4 (цвет НЕ перетирается цветом канала);
	//     • объявление администрации 3113 — `uiChatLogAddLine(listBoxSys, текст, −65536 /*КРАСНЫЙ*/,
	//       0, 0.0f /*TTL вечно*/, 0,0,0, 255 /*слот*/)` (ui_core.c:12656) ⇒ дублируются все режимы
	//       0/1/100. ★ ПОПРАВКА 2026-08-06: дубль идёт НЕ «до разбора режима», а ПОСЛЕ разворота
	//       режима 100 — sprintf(loc 10221) в uiNoticeUpdate3Line_BC90@0xBABC90 стоит СТРОКОЙ ВЫШЕ
	//       (ui_core.c:12645-12652) ⇒ в чат попадает готовый текст, а не число минут.
	//       Приёмник — листбокс 226 окна CHAT_BACKWIN 214, которое
	//       инициализация прячет (chat.c:256), поэтому в этой сборке дубль 3113 фактически невидим.
	//       Мы портируем живое окно CHAT_NEW 2500 (закон jui-prefer-new-windows) ⇒ строка видна.
	//   ⚠ ОСОЗНАННОЕ ОТСТУПЛЕНИЕ (требование юзера 2026-08-06 «да давай в принципе все Notice и
	//   прочие баннеры дублировать в чат»): для ОСТАЛЬНЫХ категорий дубля в ленту 2521 в эталоне НЕТ
	//   (там журнал 2572 + невидимые буферы A/B). Отступление обратимо БЕЗ ПЕРЕСБОРКИ — снять
	//   галочку `ChatRibbon` в DT_MessageRoutes и выполнить `asda.Msg.Reload`.
	ChatRibbon = 32     UMETA(DisplayName = "Лента чата (CHAT_NEW 2500, список 2521)"),
};
ENUM_CLASS_FLAGS(EAsdaMsgTarget);

// ── Строка ТАБЛИЦЫ МАРШРУТИЗАЦИИ. Имя строки DataTable = имя категории (War, MoveRefused, …). ──
USTRUCT(BlueprintType)
struct ASDAREBORN_API FAsdaMsgRoute : public FTableRowBase
{
	GENERATED_BODY()

	// Категория (дублирует имя строки — на случай переименования строк в редакторе).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Маршрут")
	EAsdaMsgCategory Category = EAsdaMsgCategory::System;

	// Куда уходит сообщение: битовая маска EAsdaMsgTarget (в редакторе — галочки).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Маршрут",
		meta = (Bitmask, BitmaskEnum = "/Script/AsdaReborn.EAsdaMsgTarget"))
	int32 Targets = 1;

	// Цвет строки по умолчанию — в sRGB (значения эталона: FFD306 золотой, FF0000 красный, …).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Маршрут")
	FColor Color = FColor(0xFF, 0xD3, 0x06, 0xFF);

	// Время жизни строки, мс (эталон: 10000 обычно, 2000 зоны кампвара, 30000 перегруз).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Маршрут")
	float TtlMs = 10000.f;

	// Звук при показе (эталон CSoundFlag_PlaySound2D «error.wav» у клиентских отказов). Пусто — без звука.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Маршрут")
	FName Sound;

	// ★★★ СЛОТ КАНАЛА ДЛЯ ДУБЛЯ В ЛЕНТУ (действует только при взведённом бите `ChatRibbon`).
	//   Слот — это ФИЛЬТР ВКЛАДОК ленты: строка видна на вкладке, если бит её слота стоит в маске
	//   вкладки (`RebuildLines`, эталон `sub_92B976` + рендер списка ui_core.c:111448).
	//     −1  = «канала нет» ⇒ строка видна на ЛЮБОЙ вкладке. Это 1:1 эталонный слот 255, с которым
	//           оригинал кладёт в чат объявление администрации 3113 (ui_core.c:12656): 255 ≥ числа
	//           каналов (10), а проверка рендера `line[+3460] >= chanCount` пропускает такую строку.
	//      4  = эталонный слот анонса редкой добычи: канал 10 → default-ветка `Chat_AddChatLine`
	//           ставит colorType = 4 (chat.c:5024). Бит 4 = 0x10 стоит ТОЛЬКО в маске вкладки 0
	//           «Объединение» (1023) — остальные маски (928/896/960/930/900/903) его не содержат
	//           ⇒ в ОРИГИНАЛЕ анонс добычи виден только на первой вкладке. Хотите «видно всегда» —
	//           поставьте −1 (пересборка не нужна).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Маршрут",
		meta = (DisplayName = "Слот строки в ленте (−1 = все вкладки, 4 = эталон анонса добычи)"))
	int32 ChatSlot = -1;

	// ★ ЦВЕТ СТРОКИ-ДУБЛЯ В ЛЕНТЕ. A = 0 → «не задан»: строка берёт ДЕЙСТВУЮЩИЙ цвет события
	//   (перекрытие вызывающего, иначе `Color` маршрута) ⇒ дубль совпадает с баннером один в один —
	//   это и есть требование юзера «дублировать с таким же цветом текста».
	//   A > 0 → цвет ленты фиксирован таблицей и от цвета баннера не зависит. Заводское значение
	//   стоит ровно у ОДНОЙ категории — AdminNotice (3113): эталон кладёт дубль в чат ВСЕГДА
	//   КРАСНЫМ (`uiChatLogAddLine(..., −65536, ...)`, ui_core.c:12656), а RGB из пакета уходит
	//   только в бар NOTICE_WIN. Нужен «как в баннере» — обнулите альфу (пересборка не нужна).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Маршрут",
		meta = (DisplayName = "Цвет строки в ленте (A=0 — брать цвет события)"))
	FColor ChatColor = FColor(0, 0, 0, 0);

	// Комментарий: как было в оригинале и почему у нас иначе (для редактора и asda.Msg.Dump).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Маршрут")
	FString Note;
};

// ── Параметры одного сообщения. Всё, кроме Category и текста, необязательно. ──
USTRUCT(BlueprintType)
struct ASDAREBORN_API FAsdaMsgParams
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Сообщение")
	EAsdaMsgCategory Category = EAsdaMsgCategory::System;

	// Готовый текст. Если пусто — берётся строка LocId из DT_GameText (с подстановкой Args).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Сообщение")
	FString Text;

	// id строки DT_GameText (эталонный UI_FormatLocalizedNotice).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Сообщение")
	int32 LocId = 0;

	// Подстановки [ss1]/[ss2]… и [sd1]/[sd2]… (эталонные слоты FormatById).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Сообщение")
	TArray<FString> StringArgs;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Сообщение")
	TArray<int32> NumberArgs;

	// Текст, если строки LocId нет в таблице (пусто → диагностический плейсхолдер, см. cvar).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Сообщение")
	FString Fallback;

	// Перекрытия таблицы. bUseColor/TtlMs<0/Targets<0 = «взять из таблицы».
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Перекрытия")
	bool bUseColor = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Перекрытия")
	FColor Color = FColor::White;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Перекрытия")
	float TtlMs = -1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Перекрытия")
	int32 Targets = -1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Перекрытия")
	FName Sound;

	// Режим NOTICE_WIN (эталон 3113: 0=строка бара, 1=липкая строка, 100=loc 10221).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Перекрытия")
	int32 NoticeMode = 0;

	// ★ СТИЛЕВЫЕ INLINE-ТЕГИ ДЛЯ БАННЕРНЫХ ПРИЁМНИКОВ: `<FO>` — чёрная обводка,
	//   `<FB>` — жирный. Пусто = стиль канала (у 221 это эталонная тень без обводки).
	//   ПОЧЕМУ ПОЛЕ СООБЩЕНИЯ, А НЕ СВОЙСТВО КАНАЛА: эталон ставит стиль ПОШТУЧНО каждому
	//   контролу (`CClass_SetField926`: 227 и 83401 — обводка, 229/231 — жирный, 230 — ничего),
	//   а у списка 221 (`uiChatLogAddLine`) стилевого аргумента нет вовсе. Значит канал 221
	//   обязан оставаться эталонным для всех, кто им пользуется, а «золотой текст с чёрной
	//   обводкой» задаёт КОНКРЕТНОЕ сообщение (требование владельца 2026-08-22 к нотису
	//   «Получено задание …» — см. `Reports/quest_notice_sound_fix.md` §1.4).
	//   Своей обводки никто не рисует: тег разбирает общий `UAsdaRichText`/`FAsdaRichDecorator`.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Перекрытия")
	FString StyleTag;

	// Диагностика: опкод/место-источник (пишется в лог, помогает ловить «сообщение не видно»).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Диагностика")
	int32 SrcOpcode = 0;
};

UCLASS()
class ASDAREBORN_API UAsdaMessageSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// ★ ЕДИНСТВЕННАЯ ТОЧКА ВЫВОДА. Всё остальное — тонкие обёртки над ней.
	UFUNCTION(BlueprintCallable, Category = "Asda|Messages")
	void Post(const FAsdaMsgParams& Params);

	// Короткая форма: готовый текст.
	UFUNCTION(BlueprintCallable, Category = "Asda|Messages")
	void PostText(EAsdaMsgCategory Category, const FString& Text);

	// Короткая форма: строка из DT_GameText по id (эталонный UI_FormatLocalizedNotice).
	UFUNCTION(BlueprintCallable, Category = "Asda|Messages")
	void PostLoc(EAsdaMsgCategory Category, int32 LocId, const FString& Fallback = FString());

	// Строка из DT_GameText с подстановками [ss1]/[sd1].
	UFUNCTION(BlueprintCallable, Category = "Asda|Messages")
	void PostLocFmt(EAsdaMsgCategory Category, int32 LocId,
	                const TArray<FString>& StringArgs, const TArray<int32>& NumberArgs);

	// Перечитать таблицу маршрутизации из данных (asda.Msg.Reload) — без пересборки.
	void ReloadRoutes();

	// Таблица маршрутизации + состояние окон одной простынёй (asda.Msg.Dump).
	FString DumpRouting() const;

	// Маршрут категории (для диагностики и для обёрток).
	const FAsdaMsgRoute& GetRoute(EAsdaMsgCategory Category) const;

	// Имя категории строкой (для логов, консоли и имён строк DataTable).
	static FString CategoryName(EAsdaMsgCategory Category);
	// Разбор имени категории из консоли (регистронезависимо). false — не распознано.
	static bool ParseCategory(const FString& S, EAsdaMsgCategory& Out);
	// Расшифровка маски приёмников словами.
	static FString TargetsToString(int32 Targets);

private:
	// Заполнить Routes встроенными умолчаниями (источник истины при отсутствии данных).
	void FillDefaults();
	// Наложить поверх умолчаний DataTable /Game/AsdaAssets/Data/DT_MessageRoutes. true — применено.
	bool ApplyDataTable();

	// ★★★ КОНТРОЛЬ ПРАВИЛА «ЖУРНАЛ ИЛИ ЛЕНТА, НО НЕ ОБА» (регресс 2026-08-06: строки системного
	//   журнала дублировались в основную ленту чата — «Ты получаешь предмет: …» видно дважды).
	//   Категория с одновременно взведёнными `SystemChat` (журнал 2570) и `ChatRibbon` (лента 2521)
	//   даёт пользователю двойник. Дублировать в ленту допустимо только БАННЕРНЫМ категориям
	//   (NOTICE_WIN 228 / HERO_NOTICE 83400 / WARMESSAGE_WIN 221), которые в журнал не пишут.
	//   Данные — авторитет, поэтому маска НЕ правится молча: выводится Warning с именами категорий,
	//   и он же виден в `asda.Msg.Dump`. Возвращает число нарушений.
	int32 WarnRibbonConflicts() const;

	// Резолв текста: Text → LocId(+args) → Fallback → плейсхолдер. Пусто = показывать нечего.
	FString ResolveText(const FAsdaMsgParams& P, bool& bOutMissingLoc) const;

	// Маршруты по индексу категории (размер = EAsdaMsgCategory::MAX).
	TArray<FAsdaMsgRoute> Routes;

	// Откуда взялась таблица (для asda.Msg.Dump): «DataTable» / «CSV» / «встроенные умолчания».
	FString RoutesSource;

	// Счётчики отправок по категориям — видно, что источник ЖИВОЙ, даже если окно не показало.
	TArray<int32> PostCount;
};
