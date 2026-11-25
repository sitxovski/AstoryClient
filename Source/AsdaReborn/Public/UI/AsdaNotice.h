// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ AsdaNotice — ФАСАД НОТИСОВ ОКОН (волна 1 аудита дублей, 14.09.2026, `Reports/dedup_audit/01_cpp_ui.md` В2).
//
// ЗАЧЕМ. ~90 обёрток в 35 файлах окон повторяли одни и те же 5–12 строк: достать подсистему текста/окон/
//   сообщений, взять loc-строку, показать однокнопочное окно, при его отсутствии — строку в чат. Копии
//   разошлись механикой: одни при отсутствии оконной подсистемы уходили в чат, другие молча терялись;
//   самоцветы писали в чат мимо маршрутизатора; одно имя `Loc` у разных окон означало то теги срезаны,
//   то сохранены.
//
// ЧТО ЗДЕСЬ, А ЧТО НЕТ.
//   • Здесь — только МЕХАНИКА: откуда текст и по какому маршруту он идёт.
//   • КАНАЛ (категория `EAsdaMsgCategory`) и LOC-ID выбирает вызывающий — по ОБРАБОТЧИКУ эталона, не по
//     семье (закон `loc-per-handler-not-by-family`). Категорию не «унифицировать».
//   • Сообщения с эталонным цветом/TTL/буфером конкретного обработчика (Soulmate 5203, биржа 9913,
//     склад, рулетка, навыки дружбы) остаются на `UAsdaMessageSubsystem::Post(FAsdaMsgParams)`.
//
// ТРИ ВИДА ТЕКСТА — ИХ НЕЛЬЗЯ ПУТАТЬ (у каждого есть живые потребители):
//   `Loc`       — `GetTextById`: inline-теги `<FC=…>` СРЕЗАНЫ, маркеры `[ss1]/[sd1]` ОСТАЮТСЯ как в таблице
//                 (часть окон подставляет их сама: Clan:706, Constellation:685, Party:732/918).
//   `LocRaw`    — `AsdaUi::LocRaw`: теги СОХРАНЕНЫ, маркеры обработаны общим форматтером
//                 (`UI_FormatLocalizedNotice`, закон `loc-string-placeholders`); фолбэк пусто / «[#N]» (до 11.10.2026 — «[loc N]», N9).
//   `LocTagged` — `GetTextByIdRaw`: теги СОХРАНЕНЫ, маркеры НЕ тронуты (цветные подписи вкладок).
//   `LocSlot`   — строковый слот 1 + общий форматтер (теги сохранены).
#pragma once
#include "CoreMinimal.h"
#include "UI/AsdaMsgCategory.h"   // ★ R1: EAsdaMsgCategory — категория события (без класса подсистемы сообщений)
#include "UI/AsdaLocIds.h"             // AsdaLoc::kBtnOk — подпись «ОК» однокнопочного окна

class UGameInstance;
class UWorld;
class UUserWidget;
struct FLogCategoryBase;

namespace AsdaNotice
{
	// ★ U13 (аудит дублей №2, В1, 11.10.2026): ЭХО КОНСОЛЬНОЙ КОМАНДЫ. Каждая строка Text — в лог категории
	//   вызывающего (Display), непустая — ещё и в ленту подсказок (`EAsdaMsgCategory::Tip`), чтобы ответ отладочной
	//   команды был виден в игре. Заменяет восемь дословных копий `static void XxxEcho` (сумка, хотбар, эффекты, снасть,
	//   мини-статус, хранитель, меню, компаньон-хранитель): различалась только категория лога.
	ASDAREBORN_API void ConsoleEcho(UWorld* W, const FString& Text, const FLogCategoryBase& LogCategory);

	// Loc-строка с СРЕЗАННЫМИ тегами, маркеры как в таблице. "" — строки нет.
	ASDAREBORN_API FString Loc(const UGameInstance* GI, int32 Id);
	// Loc-строка с СОХРАНЁННЫМИ тегами и обработанными маркерами. Фолбэк: "" или «[#N]» (до 11.10.2026 — «[loc N]», N9).
	ASDAREBORN_API FString LocRaw(const UGameInstance* GI, int32 Id, bool bDebugFallback = false);
	// Loc-строка с СОХРАНЁННЫМИ тегами, маркеры не тронуты. "" — строки нет.
	ASDAREBORN_API FString LocTagged(const UGameInstance* GI, int32 Id);
	// Loc-строка со строковым слотом 1 (`[ss1]`), теги сохранены. "" — нет подсистемы текста.
	ASDAREBORN_API FString LocSlot(const UGameInstance* GI, int32 Id, const FString& Slot1);

	// Строка в ленту по loc-id через маршрутизатор. false — маршрутизатора нет (ничего не показано).
	ASDAREBORN_API bool Chat(const UGameInstance* GI, EAsdaMsgCategory Cat, int32 LocId, const FString& Fallback = FString());
	// Готовый текст в ленту через маршрутизатор. Пустой текст не отправляется. false — не отправлено.
	ASDAREBORN_API bool ChatText(const UGameInstance* GI, EAsdaMsgCategory Cat, const FString& Text);
	// Loc-строка с подстановками `[ssN]/[sdN]` в ленту через маршрутизатор. false — маршрутизатора нет.
	ASDAREBORN_API bool ChatFmt(const UGameInstance* GI, EAsdaMsgCategory Cat, int32 LocId,
		const TArray<FString>& StringArgs, const TArray<int32>& NumberArgs);
	// Строка системного чата С ЦВЕТОМ ОБРАБОТЧИКА: эталонный `CChat_AddTokenizedLines(txt, color, 10000, 0)`
	//   (журнал + буфер A, TTL 10 с). Идёт через маршрутизатор с явными приёмником/цветом/TTL;
	//   звук берётся из маршрута категории — для строк без звука передавай `System`.
	ASDAREBORN_API bool ChatColored(const UGameInstance* GI, EAsdaMsgCategory Cat, const FString& Text,
		FColor Color, float TtlMs = 10000.f);
	// ★ ПЛАШКА `WARMESSAGE_WIN` 221 — эталонный `CControl_SetTooltipText(g_pChatWnd, txt, color, ttl)` (миснейм IDA:
	//   это не тултип, а строка в плашке над чатом). Общая точка порта (волна В3 ремесла, ревизия В0–В2 М-2; ревизия В3
	//   МЕЛК-12: журнал заданий постит в 221 сам — у него свой стиль `<FO>` по заявке владельца, это не копия):
	//   сведены три копии — склад (`UAsdaWarehouseSubsystem::NoticePlank`), сумка (`UAsdaInventorySubsystem::NoticePlank`)
	//   и ремесло (`UAsdaCraftSubsystem::NoticeChatAndPlank`). Приёмник — только `EAsdaMsgTarget::WarWindow` (не журнал).
	//   Пустой текст / нет маршрутизатора ⇒ false (вызывающий решает, нужен ли фолбэк).
	ASDAREBORN_API bool Plank(const UGameInstance* GI, EAsdaMsgCategory Cat, const FString& Text,
		FColor Color, float TtlMs = 10000.f, int32 SrcId = 0);
	// ★ Плашка 221 по loc-id (эталонный одиночный `CControl_SetTooltipText(g_pChatWnd, UI_FormatLocalizedNotice(id),
	//   color, ttl)`). Строки нет ⇒ `Chat(Cat, LocId)` — пропавшая строка видна в ленте плейсхолдером маршрутизатора,
	//   а не теряется молча. `SrcId` — опкод/место для диагностики маршрутизатора. true — показано в плашке.
	//   (27.09.2026, отказы портала 5055/6201: коды 0 и 2.)
	ASDAREBORN_API bool PlankLoc(const UGameInstance* GI, EAsdaMsgCategory Cat, int32 LocId,
		FColor Color, float TtlMs = 10000.f, int32 SrcId = 0);
	// ★ ПАРА «плашка 221 + строка системного чата» по loc-id — эталонная связка
	//   `CControl_SetTooltipText(g_pChatWnd, txt, color, ttl)` + `CChat_AddTokenizedLines(g_pChatWnd, txt, color, ttl, 0)`
	//   (одна строка, один цвет, одно время жизни). Общая точка для ремесла (`UAsdaCraftSubsystem::NoticeChatAndPlank`)
	//   и отказов портала 5055/6201 (коды 8, 12, 13…20). Строки нет ⇒ `Chat(Cat, LocId)` (плейсхолдер). Звук берётся
	//   из маршрута категории — для строк без звука передавай `System`. true — строка показана.
	//   `bChatFirst` — порядок вывода: false — плашка, затем журнал (5055/6201, `network.c:43000-43062`); true — журнал,
	//   затем плашка (ремесло 5572/5583, `network.c:49557`/`:50060`; ревизия 5055 М-1).
	ASDAREBORN_API bool PlankAndChatLoc(const UGameInstance* GI, EAsdaMsgCategory Cat, int32 LocId,
		FColor Color, float TtlMs = 10000.f, int32 SrcId = 0, bool bChatFirst = false);

	// ★ ЕДИНЫЙ МАРШРУТ ОКНА-СООБЩЕНИЯ: однокнопочный `ONEBUT_MSGBOX` 1500 (эталонный
	//   `CItem_BuildTooltipText(g_pItemTooltipUI, 0, −1, текст)`), а если оконной подсистемы нет — строка в
	//   ленту той же категорией. Категория — ещё и канал фолбэка `ShowGameMessage`, если WBP не загрузился.
	//   Текст отдаётся окну КАК ЕСТЬ (теги срезает вызывающий, если нужно — так было у всех копий).
	//   Пустой текст ⇒ ничего. true — текст отдан окну.
	//   ⚠ Фолбэк кладёт текст КАК ЕСТЬ (`PostText`): маркеры `[ss1]/[sd1]` в нём не подставляются. Если текст —
	//   loc-строка без аргументов, бери `MsgBoxLoc` (его фолбэк — `PostLoc` с подстановкой).
	//   ★ Очередь окон-сообщений (26.09): `EtalonNoticeId` — noticeId эталонного вызова (по умолчанию −1); пара
	//   (0, id), уже стоящая в очереди, глушит повторный показ (`UAsdaWindowSubsystem::ShowGameMessage`).
	ASDAREBORN_API bool MsgBox(const UGameInstance* GI, EAsdaMsgCategory Cat, const FString& Text,
		int32 OkLocId = AsdaLoc::kBtnOk, int32 EtalonNoticeId = -1);
	// То же по loc-id; если строка пуста или окна нет — `Chat(Cat, LocId)` (пропавшая строка видна в
	//   ленте плейсхолдером маршрутизатора, а не молча). true — текст отдан окну.
	ASDAREBORN_API bool MsgBoxLoc(const UGameInstance* GI, EAsdaMsgCategory Cat, int32 LocId,
		int32 OkLocId = AsdaLoc::kBtnOk, int32 EtalonNoticeId = -1);
	// ★ U2 (дедуп №2, В2-A, 11.10.2026): `MsgBox` С ДЕЙСТВИЕМ НА «ОК» И Z-ПОРЯДКОМ (модалка «вошёл в мир мёртвым»:
	//   ОК = воскрешение 4054, z = 800). Политика та же, что у `MsgBox`: окно есть — `ShowGameMessage(…, OnOk, ZOrder, …)`;
	//   оконной подсистемы нет — текст в ленту категорией Cat (колбэк при этом НЕ вызывается: нажать нечего).
	//   Возврат — виджет окна (nullptr: пустой текст, нет подсистемы окон, отброшенный дубль пары очереди или WBP
	//   не загрузился — различает вызывающий, например по `IsMsgBoxQueued`).
	ASDAREBORN_API UUserWidget* MsgBoxCb(const UGameInstance* GI, EAsdaMsgCategory Cat, const FString& Text,
		TFunction<void()> OnOk, int32 ZOrder, int32 OkLocId = AsdaLoc::kBtnOk, int32 EtalonNoticeId = -1);
}
