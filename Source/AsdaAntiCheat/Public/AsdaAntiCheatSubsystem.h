// Copyright (c) 2026 @sitxovski (Discord)
// AsdaAntiCheatSubsystem.h — клиентский Easy Anti-Cheat (EOS SDK 1.19.2.1, режим ClientServer), Э1-Г 16.10.2026.
//
// ЧТО ДЕЛАЕТ. Поднимает EOS (EOS_Initialize → EOS_Platform_Create, оверлей выключен, тик из FTSTicker, лог SDK → LogAsdaAntiCheat)
//   и ведёт поток входа FAsdaEacFlow: POST /token AsdaId → EOS Connect (OpenID) → PUID + IdToken → BeginSession →
//   канал 15901 к сайдкару (тот же хост, что LoginManager; AsdaSecure, SubServer Eac=4) → REG → ACMSG/PING.
//   Обновление Connect-токена — /refresh (refresh только в памяти). KICK сайдкара и нарушение целостности → OnFatal.
//
// ВКЛЮЧЕНИЕ (CONTRACT.md §6): -AsdaNoNet — никогда; Shipping — всегда; Development — только с ключом -AsdaEac.
//   Нет интерфейса AntiCheatClient (игра запущена не через Astory.exe — бутстрапер EAC) — статус «EAC недоступен», игра идёт.
//
// КТО ЗОВЁТ. UAsdaNetSubsystem (модуль AsdaReborn): NotifyLoginHost (ConnectToLogin), NotifyLoginCredentials (SendLogin,
//   тот же MD5-hex, что в 1003), NotifyCharacterLeftWorld/NotifyEnterWorld (сброс сессии), NotifySessionEnded (разрыв/выход);
//   подписан на OnFatal — рвёт игровую сессию штатным HandleConnectionClosed (код 6 → нотис 1106 оригинала).
//   ★ ред. 3: NotifyGameChannelBound — привязка игрового соединения (BIND); OnFatal(SessionFailed) — окончательный отказ
//   (сообщение игроку и возврат на логин); канал 15901 подключается неблокирующе (предел 1 с).
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Containers/Ticker.h"
#include "AsdaEacTypes.h"
#include "AsdaAntiCheatSubsystem.generated.h"

class FAsdaEacFlow;
class FAsdaEacEosBackend;
class IConsoleObject;

// Фатальное событие античита (KICK / нарушение целостности): UI — текст по LocId, сеть — разрыв игровой сессии.
DECLARE_MULTICAST_DELEGATE_OneParam(FAsdaEacFatalEvent, const FAsdaEacFatal&);
// Смена состояния античита (для строки статуса в UI).
DECLARE_MULTICAST_DELEGATE_OneParam(FAsdaEacStateEvent, EAsdaEacState);
// ★ 16.10.2026: мягкое состояние (игра идёт, повтор сам) — loc 991031/991033/991038/991039, один раз на смену состояния.
DECLARE_MULTICAST_DELEGATE_OneParam(FAsdaEacSoftEvent, int32);

UCLASS()
class ASDAANTICHEAT_API UAsdaAntiCheatSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	UAsdaAntiCheatSubsystem();
	virtual ~UAsdaAntiCheatSubsystem() override;   // в .cpp — там полные типы потока и бэкенда

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// ── Точки вызова из сети ──
	// Хост LoginManager (тот же, что в ConnectToLogin) — на нём слушает сайдкар.
	void NotifyLoginHost(const FString& Host);
	// Игрок ввёл логин/пароль: MD5-hex пароля — ровно тот, что уходит LoginManager в 1003. Старый вход не ждёт EAC.
	void NotifyLoginCredentials(const FString& Login, const FString& PasswordMd5Hex);
	// Выход в выбор персонажа. ★ R3-01 (CONTRACT.md §8.6): канал 15901, сессия EAC и Connect СОХРАНЯЮТСЯ (ни EndSession,
	//   ни Connect_Logout); забывается только привязка прежнего игрового соединения.
	void NotifyCharacterLeftWorld();
	// Вход в мир (1021). ★ R3-01: идущую сессию не трогает — сайдкару уйдёт только BIND нового игрового соединения;
	//   если сессии нет (Idle), а refresh есть — поднимает её через /refresh.
	void NotifyEnterWorld();
	// Разрыв/выход/смена сервера: полный сброс (EndSession, 15901, Connect_Logout, refresh стёрт).
	void NotifySessionEnded(const TCHAR* Why);
	// ★ R3-01: соединение с сервером закрылось (UAsdaNetSubsystem::HandleConnectionClosed). Игровое соединение, закрытое после
	//   отправленного клиентом возврата к выбору персонажа (1028) без кика 4101 (KickCode 0xFF), — смена персонажа
	//   (NotifyCharacterLeftWorld). Иначе — настоящий разрыв (LoginManager или игровой): полный сброс (NotifySessionEnded).
	void NotifyConnectionClosed(bool bWasGame, bool bReturnToCharSelectSent, uint8 KickCode);
	// ★ ред. 3 (CONTRACT.md §8.1): рукопожатие AsdaSecure игрового соединения завершено (GameServer/Indun, портал,
	//   переподключение) — BIND сайдкару с этой привязкой. ServerKind: 2 Game, 3 Indun. Bind32 — 32 байта.
	void NotifyGameChannelBound(uint8 ServerKind, const uint8* Bind32);

	// ── Состояние ──
	EAsdaEacState GetState() const;
	// Включён ли EAC политикой (сборка/ключи).
	bool IsEnabledByPolicy() const { return bEnabledByPolicy; }
	// Последняя причина отказа (для UI/журнала).
	FString GetLastError() const;

	// События для UI и сети.
	FAsdaEacFatalEvent OnFatal;
	FAsdaEacStateEvent OnStateChanged;
	FAsdaEacSoftEvent OnSoftNotice;   // ★ 16.10: строка игроку в системный чат (подписан UAsdaNetSubsystem)

private:
	TSharedPtr<FAsdaEacEosBackend> Backend;   // EOS + HTTP + канал 15901 (TSharedPtr: тип неполный в заголовке UCLASS)
	TSharedPtr<FAsdaEacFlow> Flow;            // машина состояний
	FTSTicker::FDelegateHandle TickHandle;    // тик EOS_Platform_Tick, канала и таймеров потока
	FString LoginHost;                        // хост LoginManager (последний ConnectToLogin)
	bool bEnabledByPolicy = false;            // итог политики включения

	// Тик ядра (игровой поток).
	bool TickEac(float DeltaTime);
	TArray<IConsoleObject*> ConsoleCmds;      // дев-команды asda.Eac.* (в Shipping не регистрируются — массив пуст)
};
