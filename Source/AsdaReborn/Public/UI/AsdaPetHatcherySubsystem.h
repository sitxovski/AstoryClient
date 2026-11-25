// Copyright (c) 2026 @sitxovski (Discord)
// ═══════════════════════════════════════════════════════════════════════════════════════════════
// UAsdaPetHatcherySubsystem — ОКНО 42000 `Pet_Hatchery_Win`, ИНКУБАТОР (волна 3 порта питомцев,
//   21.09.2026). Эталонный класс — `CPetWindow` (`RE/etalon/npc_mob_pet.c:4013-5165`), глобал
//   `g_pPetWindow`. Реверс с адресами — `Reports/pet_windows_re_report.md` §2, §4.5, §6.2, §6.3.
//
// ЧТО ДЕЛАЕТ ОКНО. Игрок кладёт в инкубатор ЯЙЦО и (по желанию) БУСТЕР, жмёт «Вывести», ждёт
//   таймер, кликает по «готово» — клиент шлёт **6102**, сервер бросает кубик и отвечает **6103**.
//   Успех: питомец появляется у игрока (отдельным пакетом 6106), окно показывает свечение 42024
//   и иконку породы; провал: иконка «неудача» и нотис.
//
// ЧТО ЧИТАЕМ. Данные питомцев — только `UAsdaPetSubsystem` (волна 1): отправку 6102 и разбор 6103
//   делает ОНА, окно лишь зовёт `RequestIncubate` и читает `GetLastIncubateEcho`. Своей копии
//   пет-данных здесь нет. Предметы — `UAsdaInventorySubsystem` (контейнеры 1 и 2) и справочники
//   `UAsdaItemTableSubsystem` (`item_list`, `special_itemlist`, `pettable`).
//
// ═══════════════════ ★★★ ТРИ СЛОТА: КТО ЕСТЬ КТО (главная путаница системы) ═══════════════════
//   Сервер `Recv_IncubatorReq@0x454F70` (`GameServer.c:34503-34690`) читает тело 6102 так:
//     • предмет 1 = `+4 сумка / +5 слот` — **КАТАЛИЗАТОР**, обязан быть `subType == 107`
//       (:34594 для cash-записи, :34608 для обычной). Его процент идёт множителем в формулу.
//       ★ Это ТОТ ЖЕ предмет, двойным кликом по которому окно и открыли: эталон кладёт его в
//         `pSrcSlotFeed` прямо в `petWindowOpen` (`npc_mob_pet.c:4096`), в окне он не выбирается.
//     • предмет 2 = `+7 / +8` — **ЯЙЦО**, обязано быть `subType == 109` (:34618 cash, :34638 обычное).
//       Оно задаёт ПОРОДУ (`PetIndex`) и базовый шанс. Выбирается в сетке 42050…42055.
//     • предмет 3 = `+10 / +11` — **БУСТЕР**, НЕОБЯЗАТЕЛЕН (:34645-34657). Его значение —
//       слагаемое. Выбирается в сетке 42070…42075. Нет предмета в этих координатах ⇒ сервер
//       просто не добавляет слагаемое; предмет ЕСТЬ, но ни в одной таблице не найден ⇒ отказ 5.
//   Формула (:34659): `Rate = EggRate + Cat% · EggRate / 100 + BoosterValue`;
//   провал при `Rate <= rand(0, 100000)` (:34663-34667) ⇒ код 9, иначе код 1.
//
//   ★★ ИМЕНА ЭТАЛОНА ЗДЕСЬ НЕ ВРУТ, ВРЁТ ОДНО: `pItemFeed` — это ЯЙЦО (сетка 42050…42055,
//      `npc_mob_pet.c:4155-4158`), `pItemTool` — БУСТЕР (сетка 42070…42075, :4184-4186),
//      а сбивает с толку `pSrcSlotFeed` — он не «еда», а предмет открытия, то есть катализатор.
//      Подпись второй сетки 42090 получает loc **100407 «Бустер»** (:4050) — сетка бустеров,
//      а не катализаторов. Формулировка §7.3 отчёта («имена эталона противоречат серверу»)
//      этой волной ОПРОВЕРГНУТА — противоречия нет, есть один неудачно названный указатель.
//
// ═══════════════ ★★★ ЧТО ИМЕННО ПОПАДАЕТ В ДВЕ СЕТКИ (это НЕ subType 107/109 пополам) ═════════
//   `petWindowRefreshUI@0xDC1340` собирает два списка ЖЁСТКО (`npc_mob_pet.c:4654-4693`):
//     • сетка ЯИЦ   = все ячейки контейнера **2** (60 шт., `g_InventoryArray2`), у которых
//       запись `item_list` имеет `itemSubType == 109` (:4685-4688), ПЛЮС все ячейки контейнера
//       **1** (30 шт., `g_InventoryBag`), у которых запись `special_itemlist` имеет
//       `subType == 109` (:4697-4700). То есть обычные и cash-яйца в одной сетке.
//     • сетка БУСТЕРОВ = ТОЛЬКО контейнер **1** и ТОЛЬКО `special_itemlist` с **`subType == 108`**
//       (:4663-4666). ★ Число 108 в отчёте не значилось — снято прямо с кода. Обычных
//       (`item_list`) бустеров окно не показывает вовсе.
//   Страниц у сеток по 6 (`nFeedPageCount = ceil(N/6)`), но **контролов смены страницы в окне
//   нет**: поля `nFeedCurPage` / `nToolCurPage` в разобранном срезе только ЧИТАЮТСЯ (:4724, :4804)
//   и НИГДЕ не пишутся — второй метод, сплошной grep по всем шести файлам эталона, дал 0 записей.
//   ⇒ Страница всегда первая; поля заведены и печатаются в дампе, чтобы расхождение было видно.
//
// ═══════════════════════ ★★★ ГАРАНТИРОВАННОЕ ВЫЛУПЛЕНИЕ И ПОКАЗ ШАНСА ═════════════════════════
//   Для ОБЫЧНОГО яйца сервер ставит `Rate = 100000` БЕЗУСЛОВНО (`GameServer.c:34640`), таблица
//   `PetIncubateRate_Table` к нему не применяется вовсе.
//   ★ ТОЧНАЯ ФОРМУЛИРОВКА (поправка ревизии волны 3). «Гарантировано» — не совсем: бросок
//   `sub_5329A0(0, 100000)` возвращает число ВКЛЮЧИТЕЛЬНО по верхней границе
//   (`v8 = Number + v7 % (a2 - Number + 1)`, `GameServer.c:117009`), а провал считается по
//   `if (v15 <= v10)` (`:34665`). Значит:
//     • катализатор с бонусом 0 % ⇒ `Rate == 100000` ⇒ провал возможен с вероятностью 1/100001;
//     • любой положительный бонус ⇒ `Rate > 100000` ⇒ провал НЕВОЗМОЖЕН.
//   Формула: `Rate = EggRate + Cat% · EggRate / 100 + BoosterValue` (`:34663`).
//   Она применяется только к CASH-яйцу и адресуется грейдом из отдельной таблицы (:34619-34621),
//   причём лишь когда грейд ≤ 9.
//   ★ ШАНС ОКНО НЕ ПОКАЗЫВАЕТ. Проверено, а не предположено: единственный контрол, который мог
//   бы его нести, — `Pet_Hatchery_GradeTxt` **42013**, и он НЕ ПРИВЯЗАН в `petWindowBuildUI`
//   (список привязок — `npc_mob_pet.c:4028-4069`, номера 42000/42001/42010/42011/42012/42059/
//   42079/42041/42015/42040/42050…42055/42090/42070…42075/42098/42099/42022/42024/42020/42021/
//   42032/42033/42026/42030/42031; 42013 среди них нет) и не сравнивается ни в одном обработчике.
//   Вместо числа окно показывает ТРИ статические подсказки в 42041 (100500 / 100501 / 100502).
//   ⇒ Своего расчёта шанса здесь нет и быть не должно (закон `never-invent-only-original`).
//   Контрол 42013 всё равно разбирается — ради дампа и чтобы было видно, что он пуст.
//
// ═══════════════════════════════ АВТОМАТ `feedState` 0 → 1 → 2 → 3 ════════════════════════════
//   0 «простой»        — можно менять слоты и отменять; кнопка 42098 запускает таймер.
//   1 «идёт таймер»    — `petWindowUpdateFeed@0xDC0D60` (:4313-4345): длительность =
//                        `pettable[яйцо->ItemVariant] + 0x190` (:4337) либо **3000 мс** для
//                        cash-яйца (:4339); полоса 42015 показывает прогресс; `animState = 1`.
//   2 «готово»         — `CPetWindow_Update` (:4299-4306): таймер вышел ⇒ показать 42022.
//   3 «запрос в сети»  — клик по 42022 (:4419-4426) прячет его, шлёт **6102** и ждёт 6103.
//   Отмена 42099 работает ТОЛЬКО при `feedState == 0` (:4391-4395) — иначе кнопка молчит.
//   Приём 6103 всегда возвращает автомат в 0 и ставит `animState = 3` (:5158-5162).
//
// ═════════════════════════════ АНИМАЦИЯ ДВЕРЕЙ И ПАРА (`animState`) ═══════════════════════════
//   `CPetWindow_Update@0xDC08A0` (:4224-4295), пороги 500 мс:
//     0 — двери спрятаны, пар спрятан; 1 — дверь A видна, через 500 мс → 2 + звук `S_e_pet_close.wav`;
//     2 — дверь B видна (створки закрыты), таймер обнуляется и состояние держится;
//     3 — дверь A видна, через 500 мс → 0 + звук `S_e_pet_shit.wav` + ПОКАЗАТЬ ПАР 42026.
//   Ветка оформления `moodBranch`: 0 — обычный инкубатор (фон 42030, двери 42020/42021),
//   1 — премиум (фон 42031, двери 42032/42033). Задаётся третьим аргументом `petWindowOpen`:
//   обычный предмет `item_list` даёт 0 (`item_inventory.c:37882`, `:40586`), запись
//   `special_itemlist` даёт 1 (`item_inventory.c:38115`).
//
// ═══════════════════════ ★★★ ГЕЙТ ОДНОРАЗОВОСТИ — ФЛАГ ИЗ ПРИЁМНИКА, А НЕ ИЗ КНОПКИ ══════════
//   `bInvDirty` взводится НЕ по нажатию, а в приёмнике 6103, когда сервер сообщил, что СПИСАЛ
//   ячейку КАТАЛИЗАТОРА: `npc_mob_pet.c:5099` (код 1, сумка 2) / `:5104` (код 1, сумка 1) и
//   `:5139` / `:5144` (код 9 — на провале катализатор тоже сгорает).
//   Срабатывает флаг в двух местах, и оба ЗАКРЫВАЮТ окно с нотисом **101119**:
//     • `petWindowUpdateFeed` (:4321-4326) — при нажатии «Вывести»;
//     • `petWindowOnMessage` (:4411-4416) — при ЛЮБОМ сообщении окна (то есть на первый же клик).
//   Смысл: катализатор, которым инкубатор открыли, израсходован — окно больше не действует.
//   ⚠ Для автомата это принципиально: закрытие наступает на СЛЕДУЮЩЕМ взаимодействии, а не
//   сразу по приходу 6103 — игрок успевает увидеть свечение 42024 и иконку результата.
//
// ═════════════════════════ ★★★ ДЕФЕКТ ОРИГИНАЛА, КОТОРЫЙ ЗДЕСЬ ИСПРАВЛЕН ══════════════════════
//   Д-ИНК. `CPetWin_OnResult` на кодах 1 и 9 чистит ячейку бустера БЕЗУСЛОВНО и всегда из
//   контейнера 1: `memset((char*)&g_InventoryBag + 57 * packet[6], 0, 0x39u)`
//   (`npc_mob_pet.c:5110` и `:5150`), где `packet[6]` — слово тела со смещения +12.
//   Когда бустера не клали, сервер возвращает туда умолчание конструктора тела
//   (`sub_45B7F0@0x45B7F0`, `GameServer.c:36999-37008`: сумка 0, слот −1) ⇒ `packet[6] == 0xFFFF`
//   ⇒ запись 57 нулей по адресу `g_InventoryBag + 3 735 495` — выход за массив на 3,5 МБ.
//   Клиент не падал только потому, что этот адрес попадал в свою же кучу.
//   ИСПРАВЛЕНО (закон `fix-original-defects`): чистим ячейку бустера, только если сумка
//   названа (1 или 2) И слот в границах контейнера. Исправление помечено в логе строкой
//   «[инкубатор] Д-ИНК», чтобы его было видно в прогоне.
//
// ═══════════════════════════════ НОТИСЫ: событие → loc → канал ════════════════════════════════
//   Разбор по КАЖДОМУ обработчику (закон `loc-per-handler-not-by-family`). Канал A — модальный
//   однокнопочный нотис (`CItem_BuildTooltipText(g_pItemTooltipUI, 0, −1, текст, 1)`, кнопка
//   loc 2288); канал C — подпись внутри окна; канал F — тултип предмета.
//   Чат у питомцев НЕ используется (доказано двумя методами, отчёт §6.1).
//     100400 → 42001, C, `petWindowBuildUI:4031`            — «Инкубатор питомца»
//     100500 → 42041, C, `petWindowBuildUI:4041`            — стартовая подсказка
//     100404 → 42040, C, `petWindowBuildUI:4045`            — «Выбери яйцо питомца»
//     100407 → 42090, C, `petWindowBuildUI:4050`            — «Бустер»
//     100405 → 42098, C, `petWindowBuildUI:4055`            — «Вывести»
//     100406 → 42099, C, `petWindowBuildUI:4058`            — «отмена»
//     100500 → 42041, C, `CPetWindow_Update:4211`           — яйцо не выбрано (раз в 2000 мс)
//     100501 → 42041, C, `CPetWindow_Update:4216`           — яйцо есть, бустера нет
//     100502 → 42041, C, `CPetWindow_Update:4221`           — яйцо и бустер есть
//     101119 → нотис,  A, `petWindowUpdateFeed:4325`        — одноразовый инкубатор (кнопка)
//     101119 → нотис,  A, `petWindowOnMessage:4415`         — одноразовый инкубатор (любой клик)
//     101106 → нотис,  A, `CPetWin_OnResult:5093` (код 1)   — «Питомец успешно вылупился»
//     100430 → нотис,  A, `CPetWin_OnResult:5115` (код 2)   — «Больше питомцев иметь нельзя»
//     100431 → нотис,  A, `CPetWin_OnResult:5120` (коды 3,7)— ошибка катализатора
//     100432 → нотис,  A, `CPetWin_OnResult:5125` (коды 4,8)— ошибка яйца
//     100433 → нотис,  A, `CPetWin_OnResult:5129` (код 5)   — ошибка вспомогательного средства
//     101107 → нотис,  A, `CPetWin_OnResult:5133` (код 9)   — «Питомец не вылупился»
//   ★ Пары 3/7 и 4/8 делят одну строку — реестр нотисов такие пары теряет (закон
//     `notices-registry-lossy-on-ternary`), поэтому сняты открытием самой ветки switch.
//   Звуки: `S_e_pet_close.wav` (:4257), `S_e_pet_shit.wav` (:4290), `S_e_pet_success.wav` (:5092),
//   `S_e_pet_failure.wav` (:5152), `S_e_pet_open.wav` (:5161, эпилог ЛЮБОГО кода).
//
// ═══════════════════════════════ ВЕТКИ ОТКАЗА ОТПРАВКИ 6102 ═══════════════════════════════════
//   `petWindow_SendFeedRequest@0xDC2570` (:4903-4917) — ТРИ гейта, все МОЛЧАЛИВЫЕ (нотиса нет):
//     О1 `pItemFeed == nullptr`     — яйцо не выбрано;
//     О2 `pSrcSlotFeed == nullptr`  — катализатор неизвестен (окно открыли не предметом);
//     О3 локального игрока нет      — `CharManager_GetLocalPlayer()` вернул 0.
//   Четвёртый отказ спрятан выше: `petWindowUpdateFeed` не запускает таймер, если яйца нет
//   (:4330) — кнопка «Вывести» просто ничего не делает. Тоже без нотиса.
//   Серверные отказы приходят кодами 2…8 в 6103 и разобраны выше.
//
// ═════════════════════ ★★★ КОДЫ СОБЫТИЙ JUI: 11 — НАЖАТИЕ, 13 — ДВОЙНОЙ КЛИК ══════════════════
//   Имена IDA в этом месте перевёрнуты, а от них зависит вся мышь окна, поэтому проверено кодом.
//     • Событие **11** поднимает только `CControl_OnLButtonDblClick@0x12DC190` (`ui_core.c:107474`),
//       и она же ЕДИНСТВЕННАЯ во всём бинаре ставит флаг `0x400` (`:107486`). А
//       `CControl_OnLButtonDown@0x12DB870` без `0x400` сразу выходит (`:107281`) и только потом
//       поднимает 5 и 6 (`:107294-107295`). ⇒ 11 срабатывает ПЕРЕД КАЖДЫМ обычным нажатием ЛКМ,
//       то есть **11 = обычное нажатие**, а имя функции — миснейм.
//     • Событие **13** поднимает `CControl_OnKeyDown@0x12DC240` (`ui_core.c:107490`) — имя тоже
//       не описывает поведение: её зовёт ВТОРОЙ насос ввода (`AsdaStory_game.c:987382`),
//       отдельный от мышиного (`:987287`), и гейтится на `GetStateByteAt(pManager, 0) == 2`,
//       тогда как мышиный — на `== 1`.
//       ⚠ ЭТО ДОКАЗАНО СЛАБЕЕ, чем «11 = нажатие» (поправка ревизии волны 3): аргумент только
//       ОТ ПОТРЕБИТЕЛЯ — на 13 инвентарь зовёт `CInventory_OnSlotUse` («применить предмет»,
//       `ui_core.c:48000`), а на 11 — `CInventoryUI_OnSlotClick` («выделить», `:47932`); будь
//       номера наоборот, одиночный клик по сумке применял бы предмет. Прямого доказательства,
//       что режим 2 — это именно двойной клик, в разобранном срезе нет.
//       ⇒ **13 = скорее всего двойной клик**; на поведение этого окна это не влияет —
//       внутри 42000 все узлы обрабатываются на 11, а точка входа берётся из инвентаря.
//   СЛЕДСТВИЕ ДЛЯ ЭТОГО ОКНА. Точка входа — двойной клик по катализатору 107 в сумке (событие 13,
//   в порту это `UAsdaInventorySubsystem::UseByDoubleClick`). Внутри окна ВСЁ одиночное: и выбор
//   в сетках, и снятие из слотов, и крестик 42002, и клик по «готово» 42022 приходят на 11.
//
// ═══════════════════════════════════ ЧЕГО ЗДЕСЬ НЕТ ═══════════════════════════════════════════
//   • НУМПАД. Окна питомцев его не зовут — доказано двумя методами (отчёт §5, `RE/docs/
//     NUMPAD_FLOW.md`): в `npc_mob_pet.c` все пять вхождений `g_pNumPadDialog` принадлежат
//     магазину NPC (:6373, :6469, :6565, :6601, :7355), в диапазоне инкубатора 4013-5165 их нет.
//     Причина логична: инкубатор берёт РОВНО ОДИН предмет из конкретной ячейки, количество не
//     выбирается. Перепроверено этой волной — вывод подтверждён.
//   • ПРЕВЬЮ ПИТОМЦА — в окне 42000 такого контрола нет вовсе, и это не менялось.
//     ⚠ Прежняя формулировка «через ОБЩИЙ render-target с гачей» НЕВЕРНА: общего RT у гачи и
//     питомца в оригинале нет, буфера ДВА (разбор — в шапке `UI/AsdaPreviewSceneActor.h`).
//     Само превью сделано волной 9 в окнах 43000 (43052) и 43500 (43576).
//   • ОКНА 43500 / 44200 / 43300 / 43400 / 43450 — волны 4-5.
// ═══════════════════════════════════════════════════════════════════════════════════════════════
#pragma once

#include "CoreMinimal.h"
#include "AsdaTickGate.h"   // ★ R4-c (14.11.2026): шаблон класса (CDO) не тикает
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "UI/AsdaUiInputTypes.h"   // ★ R1: FAsdaMouseFrame / FAsdaDragArm / FAsdaDragGhost … — поля ввода (без всего AsdaUiHelpers.h)
#include "AsdaReplyLatch.h"        // FAsdaReplyLatch — засов «запрос в полёте»
#include "AsdaSessionReset.h"      // IAsdaSessionResettable
#include "AsdaPetHatcherySubsystem.generated.h"

// ── Предобъявления окна 42000 «инкубатор». Полные заголовки тянет `.cpp`. ──
class UUserWidget;              // сам виджет окна 42000
class UWidget;                  // обобщённый узел — им ходит поиск контролов по имени
class UImage;                   // планы JUI: ячейки яйца/катализатора/бустера, скины кнопок
class UButton;                  // кнопки автомата (запуск, закрытие)
class UTextBlock;               // подписи: названия вложенных предметов, состояние автомата
class UAsdaAnimatedPlane;       // кадровая анимация автомата (состояния 0..3 — работа инкубатора)
class UAsdaButtonRelay;         // мостик hover/press: состояния подписи кнопки живут через него
class UAsdaPetSubsystem;        // модель данных: отправка 6102 и разбор ответа 6103
class UAsdaInventorySubsystem;  // выбор яйца/катализатора/бустера из сумки и чистка ячеек
class UAsdaItemTableSubsystem;  // имя и иконка предмета по его коду

// ───────────────────────────────────────────────────────────────────────────────────────────────
// ССЫЛКА НА ПРЕДМЕТ ИНВЕНТАРЯ так, как её держит эталон: указатель на 57-байтовую запись.
//   У нас указателей на чужие массивы нет, поэтому ссылка — пара «контейнер + ячейка» плюс код
//   предмета на момент сборки списка. Сверка «тот же предмет?» у эталона идёт по ТРЁМ полям
//   записи — код (+0), контейнер (+4) и слот (+5) (`npc_mob_pet.c:4760`), здесь то же самое.
// ───────────────────────────────────────────────────────────────────────────────────────────────
USTRUCT()
struct FAsdaHatchItemRef
{
	GENERATED_BODY()
	UPROPERTY() int32 Container = 0;   // 1 — премиум-сумка (30), 2 — расходники (60)
	UPROPERTY() int32 Cell      = -1;  // номер ячейки внутри контейнера; −1 — ссылки нет
	UPROPERTY() int32 ItemCode  = 0;   // код предмета на момент сборки списка

	// Ссылка заполнена: есть и контейнер, и номер ячейки.
	bool IsSet() const { return Cell >= 0 && Container > 0; }
	// Сбросить ссылку в «ничего не выбрано».
	void Clear() { Container = 0; Cell = -1; ItemCode = 0; }
	// Две ссылки указывают на ОДНУ и ту же ячейку сумки.
	bool Same(const FAsdaHatchItemRef& O) const
	{
		return Container == O.Container && Cell == O.Cell && ItemCode == O.ItemCode;
	}
};

// ───────────────────────────────────────────────────────────────────────────────────────────────
// ОДНА ЯЧЕЙКА СЕТКИ (42050+i либо 42070+j): план-иконка и одноимённый `TextBlock`.
//   Подпись в оригинале пустая (`CControl_SetText(slot, &Src)`, `npc_mob_pet.c:4716`), но её
//   надо ОЧИЩАТЬ — иначе в ней останется текст, запечённый импортёром из `.JUI`.
// ───────────────────────────────────────────────────────────────────────────────────────────────
USTRUCT()
struct FAsdaHatchCell
{
	GENERATED_BODY()
	// Иконка ячейки (атлас JUI, вырезка 32×32 по UV строки таблицы).
	UPROPERTY(Transient) TObjectPtr<UImage>     Icon  = nullptr;
	// Подпись ячейки под иконкой.
	UPROPERTY(Transient) TObjectPtr<UTextBlock> Label = nullptr;
};

UCLASS()
class ASDAREBORN_API UAsdaPetHatcherySubsystem : public UGameInstanceSubsystem, public FTickableGameObject,
	public IAsdaSessionResettable
{
	GENERATED_BODY()

public:
	// Подъём подсистемы: подписки и заготовки. Виджет окна рождается лениво при первом открытии.
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	// Снятие подписок и указателей на контролы: виджет переживать подсистему не должен.
	virtual void Deinitialize() override;

	/** Окно, выбранные слоты и автомат принадлежат СЕССИИ: сокет пересоздан — предметов нет. */
	virtual void ResetSessionState(EAsdaSessionResetReason Reason) override;

	// Кадр окна: слив событий модели, покадровый опрос мыши, шаг анимации автомата и отсчёт
	//   засова 6102 (`FeedLatch`).
	virtual void Tick(float DeltaTime) override;
	// Обязательная метка профилировщика для `FTickableGameObject` — своей работы не несёт.
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UAsdaPetHatcherySubsystem, STATGROUP_Tickables); }
	// ★ R4-c (14.11.2026): шаблон класса (CDO) не тикает — раньше `Tick` звался дважды за кадр, у рабочего объекта и у шаблона
	//   (ключ `asda.Perf.NoCdoTick`, разбор — `AsdaTickGate.h`). Рабочий объект тикает как раньше.
	virtual bool IsTickable() const override { return AsdaTickGate::NotCdo(this); }
	virtual bool IsTickableInEditor() const override { return false; }
	virtual bool IsTickableWhenPaused() const override { return false; }

	// ── Числа окна. Все из `.JUI`, дампа и кода эталона; ни одно не выдумано ──
	/** Ячеек в сетке ровно шесть: `for (i = 0; i < 6; ++i) FindControlByID(i + 42050)` (:4047-4048). */
	static constexpr int32 kGridCells = 6;
	/** Сторона иконки ячейки: `CControl_DrawTextureRect(slot, …, 32, 32)` (:4714). */
	static constexpr int32 kCellPx = 32;
	/** Сторона слота яйца 42010 и свечения 42024: 64×64 в `.JUI` и в `DrawTextureRect` (:4637). */
	static constexpr int32 kBigPx = 64;
	/** Период смены подсказки 42041: `timeGetTime() - last > 0x7D0` (:4206). */
	static constexpr float kGuidePollSec = 2.0f;
	/** Порог шага анимации дверей: `timeGetTime() - last > 0x1F4` (:4254, :4287). */
	static constexpr float kDoorStepSec = 0.5f;
	/** Длительность таймера для CASH-яйца — константа эталона (:4339). */
	static constexpr int32 kCashFeedMs = 3000;
	/** Смещение длительности в записи `pettable` (472 б): `+0x190` (:4337). */
	static constexpr int32 kPetRowFeedMsOfs = 0x190;
	/** Смещения U/V иконки «неудача» в записи `pettable`: путь +0x140, U +0x160, V +0x164 (:4645-4647). */
	static constexpr int32 kPetRowFailUOfs = 0x160;
	// Смещение в строке `pettable`: V иконки ПРОВАЛА инкубации (снято с эталона).
	static constexpr int32 kPetRowFailVOfs = 0x164;
	/** Подтипы предметов: катализатор 107, яйцо 109, бустер 108 (см. шапку). */
	static constexpr int32 kSubTypeCatalyst = 107;
	// `special_itemlist.subType` яйца — по нему окно отбирает яйца из сумки.
	static constexpr int32 kSubTypeEgg      = 109;
	// `special_itemlist.subType` бустера — по нему окно отбирает бустеры.
	static constexpr int32 kSubTypeBooster  = 108;
	/** z-порядок: инкубатор над сумкой (720) и над клеткой питомца (732) — его открывают ИЗ сумки. */
	static constexpr int32 kHatcheryZ = 736;

	// ═════════════════════════════ ОТКРЫТИЕ / ЗАКРЫТИЕ ОКНА ═══════════════════════════════════
	/** ПОЛНЫЙ порт `petWindowOpen@0xDC03E0` (:4082-4132).
	 *  `CatalystContainer`/`CatalystCell` — ячейка предмета `subType == 107`, двойным кликом по
	 *  которому окно открывают (эталонный `pSrcSlotFeed`). `bPremiumMood` — ветка оформления:
	 *  false для записи `item_list`, true для `special_itemlist` (третий аргумент эталона).
	 *  ★ ТУМБЛЕРА НЕТ: если окно уже открыто (`bActive == 1`), эталон выходит НЕ ДЕЛАЯ НИЧЕГО
	 *  (:4088 `if (LOBYTE(this->bActive) != 1)`), то есть второй катализатор окно не переоткроет
	 *  и не закроет. Воспроизводим 1:1; возвращаем false. */
	UFUNCTION(BlueprintCallable, Category="Asda|Питомцы")
	bool OpenHatchery(int32 CatalystContainer, int32 CatalystCell, bool bPremiumMood);

	/** Порт `petWindow_Close@0xDC0650` (:4136-4142): снять признак и спрятать окно. */
	UFUNCTION(BlueprintCallable, Category="Asda|Питомцы")
	void CloseHatchery();

	// Открыто ли 42000. ⚠ Признак называется `bActive`, а не `bOpen`: у автомата есть ещё и
	//   СОСТОЯНИЕ работы (0..3), и «открыто» с «работает» путать нельзя.
	UFUNCTION(BlueprintPure, Category="Asda|Питомцы")
	bool IsHatcheryOpen() const { return bActive; }

	// ═══════════════════════════════════ ДИАГНОСТИКА ══════════════════════════════════════════
	/** Каждый разобранный контрол окна (закон `full-structural-analysis-always`). */
	FString DumpControls() const;
	/** Состояние автомата, слоты, списки, флаги, коды последнего 6103. */
	FString DumpState() const;
	/** Таблица «событие → loc → канал» с ФАКТИЧЕСКИМИ строками из таблицы текстов порта —
	 *  так видно, что нумерация loc дошла до `DT_GameText`, а не только до `error list.bin`. */
	FString DumpNotices() const;

	/** Нажатие по узлу окна БОЕВЫМ обработчиком — тем же, который зовёт мышь
	 *  (закон `diagnostic-must-use-real-path`: подмена полей подтвердила бы несуществующее).
	 *  Принимает номера JUI 42002 / 42010 / 42012 / 42022 / 42050…42055 / 42070…42075 /
	 *  42098 / 42099. Возвращает false, если такого узла у окна нет. */
	bool ConsoleClick(int32 JuiId);

	/** ★★★ КРИТ-1 РЕВИЗИИ: взвести/снять перехват исходящих пет-пакетов на время прогона.
	 *  Без него живой прогон НЕ МОЖЕТ подтвердить отправку 6102: без сервера `SelectedCharId == 0`,
	 *  и окно упирается в собственный гейт О3, так и не дойдя ни до `RequestIncubate`, ни до
	 *  сборщика тела. Со взведённым перехватом клик по 42022 гонит НАСТОЯЩИЙ путь
	 *  «окно → модель → очередь → `SendPetIncubate6102` → конверт», а тело печатается сюда
	 *  побайтово и с разбором полей. В сеть при этом не уходит ничего.
	 *  `bOn == false` — снять перехват и вернуть прежний charId. */
	void ConsoleSendCapture(bool bOn, int32 TestCharId);

	/** Синтез НАСТОЯЩЕГО тела 6103 (18 б) и прогон его через приёмник `UAsdaPetSubsystem` —
	 *  проверка всего пути «пакет → модель → событие → окно» без сервера. Аргументы: код и,
	 *  по желанию, шесть номеров ячеек и вес; без них берутся ячейки, которые окно и отправило. */
	void ConsoleSynthResult(const TArray<FString>& Args);

private:
	// ── Жизненный цикл виджета ──
	bool EnsureWindow();
	// Снести кэш виджетов окна (смена мира либо пересборка): зона снята, указатели обнулены.
	void ResetWindowWidget();
	// Разобрать контролы окна по именам живого дампа и выставить признак готовности.
	void SetupControls();

	// ── Перерисовка ──
	/** Порт `petWindowRefreshUI@0xDC1340` (:4587-4846) целиком: оба слота, обе сетки, обе рамки. */
	void RefreshUI();
	/** Порт `CPetWindow_Update@0xDC08A0` (:4196-4310): подсказка, двери, пар, полоса прогресса. */
	void TickWindow(float DeltaSeconds);
	/** Порт `petWindowUpdateFeed@0xDC0D60` (:4313-4345): кнопка «Вывести». */
	void OnFeedButton();
	/** Порт ветки `a2 == 42022` в `petWindowOnMessage` (:4419-4426): клик по «готово» → 6102. */
	void OnDoneIconClicked();
	/** Порт пролога `petWindowOnMessage` (:4411-4416): гейт одноразовости на ЛЮБОЙ клик.
	 *  true ⇒ сообщение уже съедено (окно закрылось с нотисом 101119). */
	bool ConsumeOneShotGate();

	// ── Сетки и слоты ──
	/** Порт сбора `cListFeed` (:4683-4700): обычные яйца контейнера 2 + cash-яйца контейнера 1. */
	void BuildEggList();
	/** Порт сбора `cListTool` (:4660-4670): cash-бустеры контейнера 1 (`special_itemlist` 108). */
	void BuildBoosterList();
	/** Порт `petWindowSelectMessage@0xDC06A0` (:4147-4171): клик по 42050+i. */
	void OnEggCellClicked(int32 CellIndex);
	/** Порт `petWindowApplyMessage@0xDC07A0` (:4175-4191): клик по 42070+j. */
	void OnBoosterCellClicked(int32 CellIndex);
	/** Порт `petWindowOnMsgClearFeed@0xDC0F20` (:4350-4363): клик по 42010 снимает яйцо. */
	void OnEggSlotClicked();
	/** Порт `petWindowOnMsgClearTool@0xDC0F70` (:4366-4379): клик по 42012 снимает бустер. */
	void OnBoosterSlotClicked();

	// ── Мышь и подсказки (событий JUI в порту нет, курсор опрашивается покадрово) ──
	void TickMouse();
	/** Номер JUI узла под курсором либо −1. Порядок проверки — эталонный (:4405-4427). */
	int32 HitTestNode() const;
	/** Порт `petWindowBuildItemTooltipByMsgId@0xDC1140` (:4429-4480): тултип предмета под курсором. */
	void TickTooltips();

	// ── Сеть ──
	/** Порт `petWindow_SendFeedRequest@0xDC2570` (:4892-5043) — все три гейта отказа.
	 *  `bOutOwnGate` = true, если отказал НАШ гейт (`SelectedCharId <= 0`), которого у оригинала
	 *  нет: вызывающий обязан вернуть автомат в состояние 2, иначе окно встанет в тупик
	 *  (СРЕДН-2 ревизии волны 3, закон `no-state-latch-before-shown`). */
	bool SendIncubateRequest(bool& bOutOwnGate);

	/** ★★★ ВОЛНА 8 (пункт 1 ревизии): вернуть автомат кормления из состояния 3
	 *  («запрос в сети») в состояние 2 («готово, нажми») и показать 42022 снова.
	 *  Путей сюда три: отказ НАШЕГО гейта (`SelectedCharId <= 0`), отказ модели/связи
	 *  и таймаут ожидания 6103. `Why` — причина для лога. */
	void ReturnFeedToReady(const TCHAR* Why);
	/** Порт `CPetWin_OnResult@0xDC2CA0` (:5060-5165) в части ОКНА: нотис, звук, чистка, эпилог.
	 *  Разбор самого тела сделала волна 1; сюда приходят уже готовые поля. */
	void OnIncubateResult(int32 Result);
	/** Отложенная обработка события модели (закон `no-reentrant-net-callbacks`). */
	void DrainPetEvents();

	// ── Данные ──
	UAsdaPetSubsystem*       Pets() const;
	// Модель контейнеров сумки — окно только читает её и пишет через её общие точки.
	UAsdaInventorySubsystem* Inv()  const;
	// Таблицы предметов (`pettable`, `item_list`, иконки) — источник строк для окна.
	UAsdaItemTableSubsystem* Items() const;

	/** Код предмета в ячейке; 0 — ячейка пуста либо контейнера нет. */
	int32 CellCode(int32 Container, int32 Cell) const;
	/** Подтип предмета по обычной таблице `item_list`; −1 — записи нет. */
	int32 NormalSubType(int32 ItemCode) const;
	/** Подтип предмета по второй таблице `special_itemlist`; −1 — записи нет. */
	int32 SpecialSubType(int32 ItemCode) const;
	/** Имя предмета для подписи 42011: у cash-предмета — имя из `specialitem_tooltiplist`
	 *  (эталон берёт `itemGetTooltipById(...)->name`, :4626), иначе имя из `item_list`. */
	FString ItemDisplayName(int32 ItemCode) const;
	/** Длительность таймера для выбранного яйца, мс (см. `kCashFeedMs` / `kPetRowFeedMsOfs`). */
	int32 FeedDurationMsFor(const FAsdaHatchItemRef& Egg) const;
	/** Порода, которую покажет иконка результата: у обычного яйца — `item_list.LinkedGroupCode`
	 *  (:5081), у cash-яйца — `special_itemlist.ResultModel` (:5084). −1 — неизвестна. */
	int32 ResultModelFor(const FAsdaHatchItemRef& Egg) const;
	/** Нарисовать в 42010 иконку результата из `pettable[resultModel]`: успех — `ResultIcon0`
	 *  с U/V +0x138/+0x13C (:4632-4638), провал — `ResultIcon1` с U/V +0x160/+0x164 (:4643-4649). */
	bool DrawResultIcon(int32 PetIndex, bool bSuccess);
	/** Поставить рамку выбора (42059 / 42079) ровно на ячейку сетки; nullptr-безопасно. */
	void PlaceSelFrame(UWidget* Frame, const UWidget* OverCell);

	// ── Нотисы ──
	FString Loc(int32 LocId) const;
	/** Канал A: однокнопочный модальный нотис, кнопка loc 2288 «ОК». */
	void NoticeMsgBox(int32 LocId) const;
	/** Звук окна (`CSoundFlag_PlaySound2D`). */
	void PlayWav(const TCHAR* WavName) const;

	// ═══════════════════════════════════ СОСТОЯНИЕ ════════════════════════════════════════════
	UPROPERTY(Transient) TObjectPtr<UUserWidget> Win = nullptr;

	// ── Контролы (номер JUI → поле) ──
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtTitle      = nullptr;  // 42001 loc 100400
	UPROPERTY(Transient) TObjectPtr<UButton>    BtnClose      = nullptr;  // 42002 крестик
	UPROPERTY(Transient) TObjectPtr<UImage>     ImgEggSlot    = nullptr;  // 42010 слот ЯЙЦА (64×64)
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtEggSlot    = nullptr;  // 42010 подпись плана
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtEggName    = nullptr;  // 42011 имя предмета
	UPROPERTY(Transient) TObjectPtr<UImage>     ImgToolSlot   = nullptr;  // 42012 слот БУСТЕРА (32×32)
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtToolSlot   = nullptr;  // 42012 подпись плана
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtGradeTxt   = nullptr;  // 42013 ★ эталон в него НЕ пишет
	UPROPERTY(Transient) TObjectPtr<UImage>     BarTrack      = nullptr;  // 42015 жёлоб полосы
	UPROPERTY(Transient) TObjectPtr<UImage>     BarFill       = nullptr;  // 42015 заливка полосы
	UPROPERTY(Transient) TObjectPtr<UWidget>    Door0a        = nullptr;  // 42020 обычная дверь A
	UPROPERTY(Transient) TObjectPtr<UWidget>    Door0b        = nullptr;  // 42021 обычная дверь B
	UPROPERTY(Transient) TObjectPtr<UWidget>    DoneIcon      = nullptr;  // 42022 «готово», клик → 6102
	UPROPERTY(Transient) TObjectPtr<UWidget>    SuccessGlow   = nullptr;  // 42024 свечение успеха
	UPROPERTY(Transient) TObjectPtr<UWidget>    Steam         = nullptr;  // 42026 пар (группа планов)
	UPROPERTY(Transient) TObjectPtr<UWidget>    BgNormal      = nullptr;  // 42030 фон обычный
	UPROPERTY(Transient) TObjectPtr<UWidget>    BgPremium     = nullptr;  // 42031 фон премиум
	UPROPERTY(Transient) TObjectPtr<UWidget>    Door1a        = nullptr;  // 42032 премиум дверь A
	UPROPERTY(Transient) TObjectPtr<UWidget>    Door1b        = nullptr;  // 42033 премиум дверь B
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtEggTab     = nullptr;  // 42040 loc 100404
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtGuide      = nullptr;  // 42041 loc 100500/501/502
	UPROPERTY(Transient) TArray<FAsdaHatchCell> EggCells;                 // 42050…42055
	UPROPERTY(Transient) TObjectPtr<UWidget>    EggSelFrame   = nullptr;  // 42059 рамка выбора яйца
	UPROPERTY(Transient) TArray<FAsdaHatchCell> ToolCells;                // 42070…42075
	UPROPERTY(Transient) TObjectPtr<UWidget>    ToolSelFrame  = nullptr;  // 42079 рамка выбора бустера
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtToolTab    = nullptr;  // 42090 loc 100407 «Бустер»
	UPROPERTY(Transient) TObjectPtr<UButton>    BtnFeed       = nullptr;  // 42098 «Вывести»
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtFeed       = nullptr;  // 42098 подпись
	UPROPERTY(Transient) TObjectPtr<UButton>    BtnCancel     = nullptr;  // 42099 «отмена»
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtCancel     = nullptr;  // 42099 подпись
	// Посредники `OnClicked` настоящих `UButton` окна: живут столько же, сколько и окно.
	UPROPERTY(Transient) TArray<TObjectPtr<UAsdaButtonRelay>> Relays;

	// Базовая геометрия полосы 42015 — снимается с жёлоба ОДИН раз при разборе.
	float BarBaseLeft = 0.f;
	// Полная ширина полосы прогресса в единицах канваса — база для `FAsdaGaugeFill::Clip`.
	float BarFullW    = 0.f;

	// ── Модель окна (поля эталонного `CPetWindow`) ──
	bool  bReady      = false;   // контролы разобраны
	bool  bActive     = false;   // `bActive` (+148): окно открыто
	bool  bInvDirty   = false;   // `bInvDirty` (+149): катализатор израсходован — окно одноразовое
	int32 MoodBranch  = 0;       // `moodBranch`: 0 обычный, 1 премиум
	int32 FeedState   = 0;       // `feedState`: 0 простой, 1 таймер, 2 готово, 3 запрос в сети
	/** ★★★ ВОЛНА 8 (пункт 1 ревизии): сколько ещё ждём 6103 в состоянии 3.
	 *  Общая точка `FAsdaReplyLatch` (до 11.10.2026 `FAsdaInFlightLatch`). У эталона выхода из 3 без ответа нет вовсе
	 *  (npc_mob_pet.c:4424-4432), но у него «ответа нет» = «клиент закрыт»
	 *  (память `original-quits-client-on-disconnect`). Ключ `asda.Pet.SendTimeoutSec`. */
	FAsdaReplyLatch FeedLatch;
	int32 AnimState   = 0;       // `animState`: 0…3, анимация створок
	int32 ResultCode  = 0;       // `resultCode`: 0 нет, 1 успех, 9 провал
	int32 ResultModel = -1;      // `resultModel`: порода для иконки результата
	int32 FeedDurationMs = 0;    // `feedDuration`
	double FeedStartTime = 0.0;  // `feedStartTime` (секунды платформы вместо `timeGetTime`)
	double AnimStepTime  = 0.0;  // `g_dwActionThrottleTime`
	float  GuideLeft     = 0.f;  // остаток до следующей смены подсказки 42041
	int32  GuideShownLoc = -1;   // какая подсказка сейчас стоит (чтобы не дёргать текст каждый раз)

	/** Катализатор — предмет, которым окно открыли (эталонный `pSrcSlotFeed`). */
	UPROPERTY(Transient) FAsdaHatchItemRef Catalyst;
	/** Выбранное ЯЙЦО (эталонный `pItemFeed`). */
	UPROPERTY(Transient) FAsdaHatchItemRef Egg;
	/** Выбранный БУСТЕР (эталонный `pItemTool`), необязателен. */
	UPROPERTY(Transient) FAsdaHatchItemRef Booster;

	/** Полные списки кандидатов (эталонные `cListFeed` / `cListTool`). */
	UPROPERTY(Transient) TArray<FAsdaHatchItemRef> EggList;
	UPROPERTY(Transient) TArray<FAsdaHatchItemRef> ToolList;
	/** Что лежит в шести видимых ячейках (эталонные `pFeedItemPtrs` / `pToolItemPtrs`). */
	UPROPERTY(Transient) TArray<FAsdaHatchItemRef> EggPage;
	UPROPERTY(Transient) TArray<FAsdaHatchItemRef> ToolPage;

	/** Страницы. В оригинале менять их нечем (см. шапку) — держим для дампа и на будущее. */
	int32 EggPageIndex  = 0;
	// Текущая страница сетки инструментов (менять её в оригинале нечем — см. шапку).
	int32 ToolPageIndex = 0;
	// Сколько всего страниц яиц набралось из сумки.
	int32 EggPageCount  = 0;
	// Сколько всего страниц инструментов набралось из сумки.
	int32 ToolPageCount = 0;

	/** Номер JUI, по которому сейчас показан тултип предмета (−1 — не показан). */
	int32 TipId = -1;
	/** Узел, над которым ЛКМ была нажата. Клик засчитывается, только если отпустили
	 *  над ним же: окно открывается двойным кликом по сумке и в кадре открытия кнопка ещё зажата
	 *  (разбор — у `TickMouse`).
	 *  ★ ВОЛНА 8 (пункт 6 ревизии, П5-6 реестра): было голым `int32 PressedId` — вторая из ТРЁХ
	 *  своих реализаций одной пары. Теперь общая точка `FAsdaPressLatch`. */
	FAsdaPressLatch Press;

	// Покадровый опрос мыши и фронты кнопок — общая точка `AsdaUiHelpers`.
	FAsdaMouseFrame Mouse;

	/** Очередь событий модели: из колбэка только КЛАДЁМ (закон `no-reentrant-net-callbacks`). */
	TArray<TTuple<int32, int32, int32>> PetEvents;
	// Подписка на события модели питомцев; снимается в `Deinitialize`.
	FDelegateHandle HPetEvent;
};
