// Copyright (c) 2026 @sitxovski (Discord)
// UAsdaSessionResetSubsystem + IAsdaSessionResettable — ЕДИНАЯ точка сброса состояния СЕССИИ.
//
// ЗАЧЕМ ЭТО ВООБЩЕ НУЖНО (сверено с оригиналом, не выдумано)
//   В ОРИГИНАЛЕ проблемы «состояние пережило разрыв» НЕТ ПО ПОСТРОЕНИЮ. Обрыв игрового сокета
//   ведёт в `Show_DisconnectPopup@0xEC9A80` (`RE/etalon/item_inventory.c:64303`), а единственная
//   кнопка этого окна (id 8003) обрабатывается `CommonWin_CommonEventFunc@0xC6EAE0`
//   (`RE/etalon/network.c:14044`) и делает `PostQuitMessage(0)` — КЛИЕНТ ЗАВЕРШАЕТСЯ. Всё
//   состояние умирает вместе с процессом, сбрасывать нечего.
//   Порт ведёт себя ИНАЧЕ НАМЕРЕННО: `UAsdaNetSubsystem::HandleConnectionClosed` закрывает сокет и
//   оставляет клиент живым, «чтобы вход МОЖНО БЫЛО ПОВТОРИТЬ». Это наше улучшение поверх оригинала
//   (закон `fix-original-defects`), и именно оно требует того, чего у оригинала не было: полного
//   сброса состояния сессии. Без сброса персонаж B видит списки, защёлки и окна персонажа A.
//
// РАЗНЫЕ СОБЫТИЯ — ИХ НЕЛЬЗЯ СВАЛИВАТЬ В ОДИН ОБРАБОТЧИК
//   (1) РАЗРЫВ СВЯЗИ с игровым сервером — `UAsdaNetSubsystem::OnGameDisconnected`. Сессия
//       персонажа кончилась: сервер о нас забыл, все идентификаторы, списки и «запросы в полёте»
//       мертвы. Сбрасываем ВСЁ.
//   (2) ВХОД В МИР — `UAsdaNetSubsystem::OnEnterWorld` (опкод 1021 от LOGIN-сервера, ответ на
//       выбор персонажа). ★ ИМЕННО ЗДЕСЬ ЛЕЧИТСЯ «ПЕРЕЗАХОД» из находки: 1021 приходит РОВНО
//       один раз на вход персонажа и НЕ приходит при переходах между картами и каналами
//       (те идут по 6013/6015/6201). Мы ещё на карте логина, данных нового персонажа ещё нет —
//       безопаснее точки для полной зачистки не существует.
//   (3) ВЫХОД В ВЫБОР ПЕРСОНАЖА — травел на карту логина. Соединение может быть живо (login-канал),
//       но персонаж сменится. Последствия те же: полный сброс.
//   (4) СМЕНА КАРТЫ (портал/канал/варп на другую карту) — акторы старого мира уничтожены, а
//       игровое соединение ПЕРЕСОЗДАЁТСЯ (`UAsdaNetSubsystem::EnterGameWorld`, мир и подземелья —
//       разные процессы GameServer). Списки персонажа (отряд, клан, приятели, инвентарь, квесты)
//       сервер сохраняет — их трогать НЕЛЬЗЯ. Сбрасывать надо только то, что привязано к УМЕРШЕМУ
//       миру или к ЗАКРЫТОМУ сокету: спавн-признаки эффектов, всплывающие тексты, полёты снарядов,
//       «запросы в полёте» и всё, что ключуется сессионным `userNum` (он выдаётся заново).
//
//   ГОТОВАЯ ТОЧКА для (2) и (3) в проекте УЖЕ ЕСТЬ и уже используется двумя подсистемами —
//   `FCoreUObjectDelegates::PostLoadMapWithWorld` (`AsdaLoadManagerSubsystem.cpp:53`,
//   `AsdaWorldSubsystem.cpp:327`). Новых точек не заводим (закон `check-existing-before-creating`).
//
// КАК ЭТО РАБОТАЕТ
//   Подписчик у обоих событий ОДИН — `UAsdaSessionResetSubsystem`. Он рассылает вызов
//   `IAsdaSessionResettable::ResetSessionState(Reason)` всем подсистемам `GameInstance`, которые
//   этот интерфейс реализуют. Подсистеме остаётся только описать СВОЙ сброс: ни подписок, ни
//   хранения `FDelegateHandle`, ни порядка инициализации.
//
// ⚠ ЗАКОН `no-reentrant-net-callbacks`: сброс НИЧЕГО не отправляет в сеть и не закрывает сокет.
//   Он выполняется в том числе из колбэка закрытия соединения, поэтому обязан быть чисто
//   локальным: очистка полей, очередей, таймеров и гашение окон.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "UObject/Interface.h"
#include "AsdaSessionReset.generated.h"

// Причина сброса. Порядок значений — от «самого разрушительного» к «самому мягкому».
UENUM()
enum class EAsdaSessionResetReason : uint8
{
	// Игровое соединение потеряно (`OnGameDisconnected`): сокет закрыт, сервер о нас забыл.
	Disconnected  = 0,
	// Выход из мира в выбор персонажа (травел на карту логина): следующий вход может быть ДРУГИМ персонажем.
	LeftWorld     = 1,
	// Вход в мир из выбора персонажа (`OnEnterWorld`, опкод 1021). Мы ещё на карте логина,
	//   данных нового персонажа ещё нет — состояние предыдущего входа обязано уйти ЗДЕСЬ.
	EnteringWorld = 2,
	// Смена игровой карты (портал/канал/варп): мир пересоздан, соединение пересоздано,
	//   но данные персонажа на сервере целы.
	MapChanged    = 3
};

// true — сброс ПОЛНЫЙ: сессия персонажа кончилась или начинается новая.
// false — только смена карты: чистим лишь то, что привязано к умершему миру и закрытому сокету.
FORCEINLINE bool AsdaIsFullSessionReset(EAsdaSessionResetReason Reason)
{
	return Reason != EAsdaSessionResetReason::MapChanged;
}

// ★★★ МИР ПЕРЕЖИЛ СБРОС? (смягчение по решению владельца 2026-09-02)
//   Полный сброс (`AsdaIsFullSessionReset`) и «мир умер» — РАЗНЫЕ вещи, и путать их нельзя:
//     · `Disconnected` — сокет закрыт, но УРОВЕНЬ НЕ ВЫГРУЖАЛСЯ: актор своего персонажа жив,
//       стоит на карте, `CurrentMapId` прежний. Обнулять ссылки на мировые объекты здесь НЕ надо —
//       иначе HUD, полосы и миникарта гаснут прямо под стоящим персонажем.
//     · `LeftWorld` / `EnteringWorld` — травел на карту логина: акторы уничтожены движком,
//       ссылки обязаны обнулиться, иначе повиснут на мёртвых объектах.
//     · `MapChanged` — уровень пересоздан, акторы тоже мертвы.
//   ⇒ true ТОЛЬКО для разрыва связи. Использовать для ВИЗУАЛЬНОГО слоя (ссылки на акторы,
//   виджеты, зеркала своего персонажа). Списки СЕССИИ (отряд, приятели, «запросы в полёте»)
//   чистятся по `AsdaIsFullSessionReset` независимо от этого признака — они протухли в любом случае.
FORCEINLINE bool AsdaSessionResetKeepsWorldAlive(EAsdaSessionResetReason Reason)
{
	return Reason == EAsdaSessionResetReason::Disconnected;
}

// Человекочитаемое имя причины — для логов сброса.
ASDAREBORN_API const TCHAR* AsdaSessionResetReasonName(EAsdaSessionResetReason Reason);

UINTERFACE(MinimalAPI)
class UAsdaSessionResettable : public UInterface
{
	GENERATED_BODY()
};

// Интерфейс «у меня есть состояние сессии». Реализуют подсистемы `GameInstance`, которые
// переживают травел и выход в выбор персонажа вместе с `UGameInstance`.
class ASDAREBORN_API IAsdaSessionResettable
{
	GENERATED_BODY()

public:
	// Сбросить состояние сессии. Вызывается ТОЛЬКО из UAsdaSessionResetSubsystem.
	// Реализация обязана быть локальной: ничего не отправлять в сеть и не закрывать соединение.
	virtual void ResetSessionState(EAsdaSessionResetReason Reason) = 0;
};

// Единственный подписчик `OnGameDisconnected` и `PostLoadMapWithWorld`, рассылающий сброс.
UCLASS()
class ASDAREBORN_API UAsdaSessionResetSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// Разослать сброс всем подсистемам, реализующим IAsdaSessionResettable.
	// Публичный — им же пользуется консольная команда проверки `asda.Session.Reset`.
	void Broadcast(EAsdaSessionResetReason Reason);

private:
	void OnGameDisconnected();                 // игровое соединение потеряно
	void OnEnterWorld();                       // 1021: выбран персонаж, начинается новая сессия
	void OnPostLoadMap(UWorld* LoadedWorld);   // травел завершён: разбираем, куда именно приехали

	FDelegateHandle PostLoadMapHandle;   // подписка на FCoreUObjectDelegates::PostLoadMapWithWorld
	FDelegateHandle DisconnectHandle;    // подписка на UAsdaNetSubsystem::OnGameDisconnected
	FDelegateHandle EnterWorldHandle;    // подписка на UAsdaNetSubsystem::OnEnterWorld

	// Имя мира, на котором мы были в прошлый раз: нужно, чтобы отличить ПЕРВЫЙ вход на карту
	// от повторной загрузки того же уровня и не рассылать сброс дважды подряд.
	FString LastWorldName;

	// true — предыдущий загруженный мир был картой логина. Первый травел из логина В МИР — это
	// вход в игру, а не «смена карты»: сбрасывать там нечего, состояние ещё пустое.
	bool bLastWorldWasLogin = true;

	TArray<IConsoleObject*> ConsoleCmds;   // отладочные команды, снимаются в Deinitialize
};
