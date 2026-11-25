// Copyright (c) 2026 @sitxovski (Discord)
// Тонкий доступ самотестов к сетевой подсистеме — БЕЗ включения `AsdaNetSubsystem.h`.
//
// ЗАЧЕМ (R1 рефакторинга, 10.11.2026). `AsdaNetSubsystem.h` — 4900 строк; его правка пересобирает каждый .cpp, который его включает.
//   243 из 405 таких файлов — самотесты, и части из них от сети нужно только «подать пакет» да свой номер/уровень/признак
//   смерти. Такой самотест включает ЭТОТ файл: правка заголовка сети его больше не пересобирает.
//
// ЧТО ЗДЕСЬ. Свободные функции-переходники; реализация — `Private/Test/AsdaSelfTestNet.cpp` (единственное место, где нужен
//   полный класс). Переходник поля возвращает ССЫЛКУ на само поле `UAsdaNetSubsystem`, поэтому запись
//   `AsdaTestNet::SelfLevel(Net) = 30;` равнозначна прежней `Net->SelfLevel = 30;`, а переходник метода зовёт метод с теми же
//   аргументами. Никакой своей логики, проверок на nullptr и умолчаний сверх исходных здесь нет.
//
// КОГДА НЕ ПОДХОДИТ. Самотесту нужен другой член сети (делегат, отправитель `Send*`, `TWeakObjectPtr<UAsdaNetSubsystem>`,
//   `IsValid(Net)`) — он включает `AsdaNetSubsystem.h`, как раньше. Набор ниже намеренно узкий: расширять его стоит только
//   тогда, когда новый переходник переводит на тонкий доступ сразу несколько самотестов (замер — `Reports/refactor_2026-11/r1`).
#pragma once
#include "CoreMinimal.h"

class UAsdaNetSubsystem;   // полный класс — только в AsdaSelfTestNet.cpp
class UGameInstance;       // экземпляр игры — владелец подсистемы (параметр Get)

namespace AsdaTestNet
{
	// Сетевая подсистема экземпляра игры: то же, что `GI->GetSubsystem<UAsdaNetSubsystem>()` (GI обязан быть не nullptr, как и там).
	ASDAREBORN_API UAsdaNetSubsystem* Get(const UGameInstance* GI);

	// ── Методы (зовут одноимённый метод `UAsdaNetSubsystem` с теми же аргументами) ──
	// Подать пакет в разбор так, будто он пришёл с сервера: `UAsdaNetSubsystem::DebugFeedPacket`.
	ASDAREBORN_API void DebugFeedPacket(UAsdaNetSubsystem* Net, int32 Opcode, const TArray<uint8>& Body, uint16 HdrA = 0);
	// Назначить свой сессионный номер (офлайн-прогон): `UAsdaNetSubsystem::SetSelfUserNumForDebug`.
	ASDAREBORN_API void SetSelfUserNumForDebug(UAsdaNetSubsystem* Net, uint16 Num);
	// Это мой сессионный номер? `UAsdaNetSubsystem::IsSelfUserIdx`.
	ASDAREBORN_API bool IsSelfUserIdx(const UAsdaNetSubsystem* Net, int32 Uidx);
	// Состояние «души» своего персонажа: `UAsdaNetSubsystem::SetSelfSoulState`.
	ASDAREBORN_API void SetSelfSoulState(UAsdaNetSubsystem* Net, uint8 InState);

	// ── Поля (ссылка на само поле: читать и писать можно так же, как раньше через `Net->`) ──
	ASDAREBORN_API uint16& SelfUserNum(UAsdaNetSubsystem* Net);                  // свой сессионный номер
	ASDAREBORN_API const uint16& SelfUserNum(const UAsdaNetSubsystem* Net);      // то же для чтения по const-указателю: свой сессионный номер
	ASDAREBORN_API bool& bSelfUserNumKnown(UAsdaNetSubsystem* Net);              // свой номер известен
	ASDAREBORN_API const bool& bSelfUserNumKnown(const UAsdaNetSubsystem* Net);      // то же для чтения по const-указателю: свой номер известен
	ASDAREBORN_API int32& SelectedCharId(UAsdaNetSubsystem* Net);                // выбранный персонаж (charId)
	ASDAREBORN_API const int32& SelectedCharId(const UAsdaNetSubsystem* Net);      // то же для чтения по const-указателю: выбранный персонаж (charId)
	ASDAREBORN_API bool& bInGameConnected(UAsdaNetSubsystem* Net);               // вход в игру принят (6004)
	ASDAREBORN_API const bool& bInGameConnected(const UAsdaNetSubsystem* Net);      // то же для чтения по const-указателю: вход в игру принят (6004)
	ASDAREBORN_API bool& bSelfDead(UAsdaNetSubsystem* Net);                      // свой персонаж мёртв
	ASDAREBORN_API const bool& bSelfDead(const UAsdaNetSubsystem* Net);      // то же для чтения по const-указателю: свой персонаж мёртв
	ASDAREBORN_API uint8& SelfLevel(UAsdaNetSubsystem* Net);                     // свой уровень
	ASDAREBORN_API const uint8& SelfLevel(const UAsdaNetSubsystem* Net);      // то же для чтения по const-указателю: свой уровень
	ASDAREBORN_API uint8& SelfSoulState(UAsdaNetSubsystem* Net);                 // состояние «души»
	ASDAREBORN_API const uint8& SelfSoulState(const UAsdaNetSubsystem* Net);      // то же для чтения по const-указателю: состояние «души»
	ASDAREBORN_API int32& EnterMapId(UAsdaNetSubsystem* Net);                    // карта входа
	ASDAREBORN_API const int32& EnterMapId(const UAsdaNetSubsystem* Net);      // то же для чтения по const-указателю: карта входа
	ASDAREBORN_API int32& SelfWeightCur(UAsdaNetSubsystem* Net);                 // текущий вес сумки
	ASDAREBORN_API const int32& SelfWeightCur(const UAsdaNetSubsystem* Net);      // то же для чтения по const-указателю: текущий вес сумки
}
