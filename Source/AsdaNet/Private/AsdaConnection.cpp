// Copyright (c) 2026 @sitxovski (Discord)
#include "AsdaConnection.h"
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "Interfaces/IPv4/IPv4Address.h"
#include "Common/TcpSocketBuilder.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"      // FPlatformTime::Seconds — срок ожидания коннекта
#include "HAL/IConsoleManager.h"   // CVar предела ожидания коннекта
#include <atomic>                  // флаг завершения фонового резолва имени
#include "Misc/CommandLine.h"      // ★ Р-2: ключ -AsdaNoNet
#include "Misc/Parse.h"            // ★ Р-2: FParse::Param
#include "AsdaTimedSend.h"         // ★ 15.10.2026: поток точной отправки к дедлайну
#include "Misc/ScopeLock.h"
#include "Net/ChannelCipher.h"    // ★ В7d: профиль XyT (чужой сервер) — шифрование не применяется
#include "AsdaPerf.h"             // ★ R4, доля a: область замера разбора кадра
#if PLATFORM_WINDOWS
#include "Secure/AsdaSecureClient.h"   // ★ В7b: криптопрослойка трафика (CNG; только Windows)
#endif

// ★★★ В7b/В7d — ШИФРОВАНИЕ ТРАФИКА (внешний конверт поверх старого кадра FB..FE). ★ В7d (13.10.2026): ПО УМОЛЧАНИЮ ВКЛ
//   (альфа-тест). 1 — ClientHello при Connect, затем AES-256-GCM; 0 — старый протокол без изменений (только дев-сборки:
//   в Shipping ключ — константа ASDA_DEV_CVAR = умолчание 1, ini/консоль его не меняют — так и задумано, см. ниже).
//   Прослойка сервера (L07/P61) различает клиента по первому байту (0xFB старый / 0xA5 Secure); сервер обязан стоять
//   с Enabled=1 (на переходе AllowLegacy=1). ОТКАТА НА СТАРЫЙ ПРОТОКОЛ ПО ИНИЦИАТИВЕ КЛИЕНТА НЕТ: «сервер не ответил на
//   рукопожатие → шлём по-старому» = готовый обход шифрования для посредника (он просто глушит ServerHello). Вместо
//   отката — понятная причина в логе и штатный разрыв (см. SecureAfterIngest / asda.Net.SecureHandshakeTimeoutSec).
static ASDA_DEV_CVAR(int32, CVarAsdaNetSecure,
	TEXT("asda.Net.Secure"), 1,
	TEXT("Шифрование трафика (ECDH+ECDSA+AES-256-GCM поверх старого протокола). 1 = вкл (умолчание, альфа), 0 = старый протокол (только дев)."),
	ECVF_Default);

// ★ R4, доля a (14.11.2026; запись 11 от 15.11.2026 — прежний путь и ключ убраны) [ПОРТ, производительность]: разбор принятого
//   кадра одной копией (`PacketFrame::ParseOneCopy`). Прежний `PacketFrame::Parse` на каждый пакет выделял память дважды и
//   копировал кадр дважды (копия кадра для расшифровки, затем копия тела). Теперь тело копируется один раз и расшифровывается
//   на месте, а разбор буфера соединения (`DrainFrames`) держит один пакет на весь проход — память тела переиспользуется.
//   Итог разбора побайтно тот же. `PacketFrame::Parse` остаётся в `AsdaCore` для отдельного теста протокола (`tests/`).

// ★ В7d: срок ожидания ServerHello после ClientHello. Нет ответа — сервер без шифрования (L07/P61 Enabled=0 или ключ
//   не годен) либо посредник глушит рукопожатие: причина в лог, штатный разрыв (тот же путь, что FIN/RST от сервера).
static ASDA_DEV_CVAR(float, CVarAsdaNetSecureHandshakeTimeoutSec,
	TEXT("asda.Net.SecureHandshakeTimeoutSec"), 5.f,
	TEXT("Срок ожидания ответа сервера на рукопожатие шифрования, секунды. Истёк — разрыв с причиной в логе (без отката на старый протокол)."),
	ECVF_Default);

// ★★★ ПРЕДЕЛ ОЖИДАНИЯ УСТАНОВЛЕНИЯ СОЕДИНЕНИЯ (исправление дефекта ОРИГИНАЛА, аудит 2026-09-02 К-4).
//   Что делает оригинал: CNetwork_Connect@0x12AA840 — WSASocketA → WSAConnect на БЛОКИРУЮЩЕМ сокете →
//   WaitForSingleObject(connectReadyEvent, INFINITE). При недоступном адресе поток стоит ровно столько,
//   сколько отмерит SYN-таймаут ОС (Windows ~21 с), и строку «Connect failed...!» игрок видит только
//   после него. Порт унаследовал это дословно и вдобавок включал неблокирующий режим ВНУТРИ ветки
//   успеха — то есть на неудачном адресе не включал никогда.
//   Что делаем мы: порядок и семантику оригинала сохраняем (по возврату из Connect соединение либо
//   готово к отправке, либо не состоялось — на этом держится и барьер connect→1003, и немедленный
//   хендшейк входа в мир), но ожидание ведём на НЕблокирующем сокете и ограничиваем сверху.
//   Ключ отката к поведению оригинала: `asda.Net.ConnectTimeoutSec 0` — ждать вердикт ОС, как раньше.
//   Предел общий на ОБЕ фазы (резолв имени + сам connect): дольше него игровой поток не замирает.
static ASDA_DEV_CVAR(float, CVarAsdaNetConnectTimeoutSec,
	TEXT("asda.Net.ConnectTimeoutSec"), 3.f,
	TEXT("Предел ожидания подключения к серверу (резолв имени + connect), секунды. 0 = ждать вердикт ОС, как в оригинале."),
	ECVF_Default);

// Общий блок фонового резолва имени. Живёт по TSharedPtr: если игровой поток вышел по таймауту,
//   фоновая задача спокойно допишет сюда результат и отпустит блок — «висячих» указателей нет.
struct FAsdaResolveState
{
	std::atomic<bool>         bDone{ false };   // выставляется ПОСЛЕДНИМ (release) — после записи полей ниже
	TSharedPtr<FInternetAddr> Addr;             // адрес (пусто = не разрешилось); читать только после bDone
	ESocketErrors             Code = SE_NO_ERROR;  // код ошибки резолва для сообщения игроку
};

// ★★ 16.10.2026 (адрес сервера = домен AsdaServerAddr::LoginHost(); с 24.10 — @@ASTORY_AUTH_HOST@@): КЭШ И ПРЕДВЫБОРКА РЕЗОЛВА ИМЕНИ.
//   Клиент теперь ходит к серверу ТОЛЬКО по имени (логин, игровой сервер при приватном адресе в пакете, канал EAC), и
//   каждое подключение начиналось бы с DNS-запроса, а синхронный `Connect` ждёт его на игровом потоке (с пределом
//   asda.Net.ConnectTimeoutSec). Поэтому резолв ведётся общим блоком на имя: первый запрос уходит в фоновый пул заранее
//   (`FAsdaConnection::PrefetchHost` — старт экрана логина, до раннего коннекта), следующие подключения берут готовый
//   адрес без ожидания. Неудачный результат и результат старше kAsdaHostCacheTtlSec не переиспользуются; неудачное
//   подключение по имени сбрасывает запись (`AsdaForgetResolvedHost`) — следующий раз имя разрешится заново (смена A-записи).
//   ★ Только IPv4: сокет `FTcpSocketBuilder` строится под IPv4 (BoundEndpoint = FIPv4Endpoint::Any), а резолв без
//   уточнения протокола мог отдать первым AAAA-адрес — connect такого адреса на IPv4-сокете отвергается сразу.
static constexpr double kAsdaHostCacheTtlSec = 300.0;   // срок годности удачного резолва, с (по истечении — новый запрос)

// Запись кэша: общий блок фонового резолва + момент запуска запроса.
struct FAsdaHostCacheEntry
{
	TSharedPtr<FAsdaResolveState, ESPMode::ThreadSafe> St;   // блок результата (bDone/Addr/Code)
	double StartSec = 0.0;                                   // FPlatformTime::Seconds() запуска запроса
};
static FCriticalSection GAsdaHostCacheLock;                  // защищает GAsdaHostCache (колбэк резолва в карту не пишет)
static TMap<FString, FAsdaHostCacheEntry> GAsdaHostCache;    // имя хоста (в нижнем регистре) → запись

// Блок резолва имени: годный из кэша (идёт сейчас либо удачен и свеж) или новый фоновый запрос (только IPv4).
static TSharedPtr<FAsdaResolveState, ESPMode::ThreadSafe> AsdaAcquireResolve(ISocketSubsystem& SocketSub, const FString& Host)
{
	const FString Key = Host.ToLower();
	const double Now = FPlatformTime::Seconds();
	FScopeLock L(&GAsdaHostCacheLock);
	if (const FAsdaHostCacheEntry* E = GAsdaHostCache.Find(Key))
	{
		const bool bDone = E->St->bDone.load(std::memory_order_acquire);
		if (!bDone || (E->St->Addr.IsValid() && Now - E->StartSec < kAsdaHostCacheTtlSec))
		{
			return E->St;                                    // идёт или готов и свеж — повторно спрашивать DNS не нужно
		}
	}
	TSharedPtr<FAsdaResolveState, ESPMode::ThreadSafe> St = MakeShared<FAsdaResolveState, ESPMode::ThreadSafe>();
	GAsdaHostCache.Add(Key, FAsdaHostCacheEntry{ St, Now });
	SocketSub.GetAddressInfoAsync([St](FAddressInfoResult Res)
	{
		if (Res.ReturnCode == SE_NO_ERROR && Res.Results.Num() > 0)
		{
			St->Addr = Res.Results[0].Address->Clone();
		}
		St->Code = Res.ReturnCode;
		St->bDone.store(true, std::memory_order_release);   // публикуем результат последним действием
	}, *Host, nullptr, EAddressInfoFlags::Default, FNetworkProtocolTypes::IPv4);
	return St;
}

// Сбросить запись кэша (подключение по этому имени не состоялось — следующий раз разрешить заново).
static void AsdaForgetResolvedHost(const FString& Host)
{
	FScopeLock L(&GAsdaHostCacheLock);
	GAsdaHostCache.Remove(Host.ToLower());
}

void FAsdaConnection::PrefetchHost(const FString& Host)
{
	if (IsNoNetRun() || Host.IsEmpty()) { return; }          // прогон без сети — ни одного обращения наружу
	FIPv4Address Lit;
	if (FIPv4Address::Parse(Host, Lit)) { return; }          // IPv4-литерал резолва не требует
	if (ISocketSubsystem* SocketSub = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM))
	{
		AsdaAcquireResolve(*SocketSub, Host);
		UE_LOG(LogTemp, Log, TEXT("AsdaConn: предвыборка адреса '%s' (фоновый резолв)"), *Host);
	}
}

// Резолв с пределом ожидания (определение ниже).
static TSharedPtr<FInternetAddr> AsdaResolveHostBounded(ISocketSubsystem& SocketSub, const FString& Host, double DeadlineSec);

bool FAsdaConnection::ResolveHostForDiag(const FString& Host, float TimeoutSec, FString& OutIp)
{
	OutIp.Reset();
	ISocketSubsystem* SocketSub = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	if (!SocketSub || Host.IsEmpty()) { return false; }
	const double Deadline = TimeoutSec > 0.f ? FPlatformTime::Seconds() + (double)TimeoutSec : 0.0;
	TSharedPtr<FInternetAddr> A = AsdaResolveHostBounded(*SocketSub, Host, Deadline);
	if (!A.IsValid()) { return false; }
	OutIp = A->ToString(false);
	return true;
}

// ★ Резолв имени хоста С ПРЕДЕЛОМ ОЖИДАНИЯ. Синхронный ISocketSubsystem::GetAddressInfo — второй
//   блокирующий вызов на этом пути (первый — сам connect): при недоступном DNS он держит игровой поток
//   столько, сколько отмерит резолвер ОС. Запрос уходит в фоновый пул, а ждём мы его до `DeadlineSec`.
//   DeadlineSec = абсолютное время FPlatformTime::Seconds(); 0 = без предела (поведение оригинала).
//   ★ 16.10.2026: блок берётся из кэша (см. выше) — после предвыборки ожидания нет вовсе. Возвращается КОПИЯ адреса:
//   вызывающий ставит в неё порт, а блок кэша общий.
static TSharedPtr<FInternetAddr> AsdaResolveHostBounded(ISocketSubsystem& SocketSub, const FString& Host, double DeadlineSec)
{
	TSharedPtr<FAsdaResolveState, ESPMode::ThreadSafe> St = AsdaAcquireResolve(SocketSub, Host);

	while (!St->bDone.load(std::memory_order_acquire))
	{
		if (DeadlineSec > 0.0 && FPlatformTime::Seconds() >= DeadlineSec)
		{
			UE_LOG(LogTemp, Warning, TEXT("AsdaConn: имя '%s' не разрешилось в отведённое время — DNS не отвечает"), *Host);
			return nullptr;
		}
		FPlatformProcess::SleepNoStats(0.002f);
	}
	if (!St->Addr.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("AsdaConn: имя '%s' не разрешилось (%s)"), *Host, SocketSub.GetSocketError(St->Code));
		return nullptr;
	}
	return St->Addr->Clone();
}

// ★ Ожидание завершения НЕблокирующего connect. true = соединение установлено и годно для Send.
//   Тонкость Windows: отказ (RST) и SYN-таймаут приходят НЕ в writefds, а в exceptfds, поэтому одного
//   Wait(WaitForWrite) мало — каждую итерацию отдельно опрашиваем состояние ошибки сокета
//   (FSocket::HasPendingConnection даёт false ровно тогда, когда у сокета взведена ошибка).
//   Тонкость POSIX: там писабельность наступает и при ОШИБКЕ connect, поэтому успех подтверждаем
//   именем удалённой стороны (getpeername есть только у реально установленного соединения).
static bool AsdaWaitConnectComplete(FSocket& S, ISocketSubsystem& SocketSub, double DeadlineSec, const FString& Host, int32 Port)
{
	const FTimespan Slice = FTimespan::FromMilliseconds(20);   // шаг опроса = зернистость выхода по ошибке
	for (;;)
	{
		if (S.Wait(ESocketWaitConditions::WaitForWrite, Slice))
		{
			TSharedRef<FInternetAddr> Peer = SocketSub.CreateInternetAddr();
			if (S.GetPeerAddress(*Peer)) { return true; }
			UE_LOG(LogTemp, Warning, TEXT("AsdaConn: подключение к %s:%d не состоялось (%s)"),
				*Host, Port, SocketSub.GetSocketError(SE_GET_LAST_ERROR_CODE));
			return false;
		}
		bool bPending = false;
		if (!S.HasPendingConnection(bPending))   // взведена ошибка сокета = отказ или отвал
		{
			UE_LOG(LogTemp, Warning, TEXT("AsdaConn: сервер %s:%d отклонил подключение (%s)"),
				*Host, Port, SocketSub.GetSocketError(SE_GET_LAST_ERROR_CODE));
			return false;
		}
		if (DeadlineSec > 0.0 && FPlatformTime::Seconds() >= DeadlineSec)
		{
			UE_LOG(LogTemp, Warning, TEXT("AsdaConn: таймаут подключения к %s:%d (%.1f с) — сервер не отвечает"),
				*Host, Port, CVarAsdaNetConnectTimeoutSec.GetValueOnAnyThread());
			return false;
		}
	}
}

FAsdaConnection::FAsdaConnection()
{
	Alive = MakeShared<FAlive, ESPMode::ThreadSafe>();
	Alive->Conn = this;
}
FAsdaConnection::~FAsdaConnection()
{
	OnClosed = nullptr;
	Disconnect();                                   // снимает постановки и дожидается идущей
	FScopeLock L(&Alive->Lock);                     // фоновый поток больше не увидит этот объект
	Alive->Conn = nullptr;
}

// EWOULDBLOCK у неблокирующего сокета = «данных/места нет», НЕ обрыв. Любая другая ошибка (ECONNRESET,
// ECONNABORTED, ENOTCONN, ESHUTDOWN…) и graceful-FIN (recv вернул 0 при готовом к чтению сокете) = обрыв.
static bool AsdaSocketWouldBlock()
{
	ISocketSubsystem* SS = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	return SS != nullptr && SS->GetLastErrorCode() == SE_EWOULDBLOCK;
}

void FAsdaConnection::MarkClosed(const TCHAR* Why)
{
	if (!bConnected) return;
	{
		FScopeLock IoL(&IoLock);            // ★ ревизия сети НИЗК-5: поле читает поток точной отправки (зовётся всегда ВНЕ замка)
		bConnected = false;                 // IsConnected() сразу false — повторный вход поднимет НОВЫЙ коннект
	}
	UE_LOG(LogTemp, Warning, TEXT("AsdaConn: соединение разорвано сервером (%s)"), Why);
	if (OnClosed) OnClosed();               // владелец закроет сокет и сбросит состояние сессии
}

bool FAsdaConnection::IsNoNetRun()
{
	// Ключ читается один раз за процесс: командная строка после старта не меняется.
	static const bool bNoNet = FParse::Param(FCommandLine::Get(), TEXT("AsdaNoNet"));   // кэш ключа -AsdaNoNet
	return bNoNet;
}

bool FAsdaConnection::Connect(const FString& Host, int32 Port, int32 SecureRole)
{
	Disconnect();
	bClosedByConnectFail = false;   // ★ 26.10.2026: новое подключение — признак прошлой неудачи снят
	// ★ Р-2 (10.10.2026): прогон без сети — сокет не создаётся, адрес в журнал не пишется (по логу прогона не должно
	//   быть ни одного подключения к серверу). Объявление и назначение — `AsdaConnection.h`, `IsNoNetRun`.
	if (IsNoNetRun())
	{
		UE_LOG(LogTemp, Warning, TEXT("AsdaConn: ключ -AsdaNoNet — подключение к серверу НЕ выполняется (прогон без сети, порт %d)"), Port);
		return false;
	}
	ISocketSubsystem* SocketSub = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	if (!SocketSub) return false;

	// Общий срок на обе фазы (резолв + connect). 0 в CVar = без предела, как в оригинале.
	const float  TimeoutSec = CVarAsdaNetConnectTimeoutSec.GetValueOnAnyThread();
	const double Deadline   = (TimeoutSec > 0.f) ? (FPlatformTime::Seconds() + (double)TimeoutSec) : 0.0;

	// Адрес: IPv4-литерал напрямую, иначе DNS-резолв хостнейма (OPset.bin может нести имя, не IP).
	TSharedPtr<FInternetAddr> InetAddr;
	FIPv4Address Addr;
	const bool bByName = !FIPv4Address::Parse(Host, Addr);   // ★ 16.10.2026: подключение по имени — при неудаче сбросить кэш резолва
	if (!bByName)
	{
		InetAddr = SocketSub->CreateInternetAddr();
		InetAddr->SetIp(Addr.Value);
	}
	else
	{
		InetAddr = AsdaResolveHostBounded(*SocketSub, Host, Deadline);   // сообщение об ошибке пишет он сам
		if (!InetAddr.IsValid()) { AsdaForgetResolvedHost(Host); return false; }
		UE_LOG(LogTemp, Log, TEXT("AsdaConn: имя '%s' → %s"), *Host, *InetAddr->ToString(false));
	}
	InetAddr->SetPort(Port);

	// ★ Сокет НЕблокирующий СРАЗУ и на любой ветке. Было: AsBlocking() + SetNonBlocking(true) внутри
	//   `if (bOk)` — при неудачном адресе неблокирующий режим не включался никогда, а сам connect
	//   держал игровой поток до SYN-таймаута ОС (см. шапку файла, дефект оригинала К-4).
	Socket = FTcpSocketBuilder(TEXT("AsdaConn")).AsNonBlocking().Build();
	if (!Socket)
	{
		UE_LOG(LogTemp, Warning, TEXT("AsdaConn: не удалось создать сокет для %s:%d"), *Host, Port);
		return false;
	}
	// На неблокирующем сокете connect() возвращается сразу: true = «идёт» (EINPROGRESS/EWOULDBLOCK)
	//   либо «уже готово»; false = отказ ещё на старте (неверный адрес, нет маршрута).
	if (!Socket->Connect(*InetAddr))
	{
		UE_LOG(LogTemp, Warning, TEXT("AsdaConn: connect к %s:%d отклонён сразу (%s)"),
			*Host, Port, SocketSub->GetSocketError(SE_GET_LAST_ERROR_CODE));
		Disconnect();                 // закрыть и уничтожить сокет (оригинал тоже зовёт CNetwork_CloseSocket)
		if (bByName) { AsdaForgetResolvedHost(Host); }
		return false;
	}
	if (!AsdaWaitConnectComplete(*Socket, *SocketSub, Deadline, Host, Port))
	{
		Disconnect();                 // причину уже написал AsdaWaitConnectComplete
		if (bByName) { AsdaForgetResolvedHost(Host); }   // ★ 16.10.2026: адрес мог смениться — следующий раз резолв заново
		return false;
	}
	{
		FScopeLock IoL(&IoLock);      // ★ ревизия сети НИЗК-5: поля, которые читает поток точной отправки, — под замком
		bConnected = true;
		bSocketDead = false;
	}
	RecvBuffer.Reset();
	++RecvEpoch;                      // новое соединение = новое поколение буфера (см. DrainFrames)
	bStopDrain = false;
	SecureRaw.Reset();
	bSecureActive = false;
	bSecureReadyLogged = false;
	// ★ В7b: при включённом шифровании — рукопожатие прямо сейчас (ClientHello уходит первым байтом 0xA5).
	// ★ В7d: профиль XyT (отладочная интеграция с ЧУЖОЙ сборкой сервера, скрыт и выключен владельцем 31.08) — их
	//   сервер нашего конверта не знает: шифрование не применяется, о чём строка в логе.
#if !PLATFORM_WINDOWS
	// ★ EAC Э1: канал сырого потока (сайдкар EAC) без шифрования не поднимается — AsdaSecure есть только на Windows (CNG).
	if (bRawStream) { Disconnect(); return false; }
#endif
	// ★ EAC Э1: в режиме сырого потока шифрование обязательно — ни дев-ключ asda.Net.Secure, ни профиль XyT его не отключают
	//   (сайдкар принимает только AsdaSecure). Для игровых соединений bRawStream == false — условия те же, что были.
	if (bRawStream || CVarAsdaNetSecure.GetValueOnAnyThread() != 0)
	{
		if (!bRawStream && Asda::ChannelCipher::GetProfile() == Asda::EKeyProfile::XyT)
		{
			UE_LOG(LogTemp, Warning, TEXT("AsdaConn: профиль XyT (чужой сервер) — шифрование трафика к %s:%d НЕ применяется"), *Host, Port);
		}
#if PLATFORM_WINDOWS
		else
		{
			SecurePeer = FString::Printf(TEXT("%s:%d"), *Host, Port);
			if (!SecureBeginHandshake(Port, SecureRole))
			{
				UE_LOG(LogTemp, Warning, TEXT("AsdaConn: не удалось начать рукопожатие шифрования с %s — разрыв"), *SecurePeer);
				Disconnect();
				return false;
			}
		}
#endif
	}
	return true;
}

void FAsdaConnection::Disconnect()
{
	// ★ 15.10.2026: отложенные кадры старого соединения не уходят в новое — снять до замка (снятие ждёт идущую отправку,
	//   а та ждёт `IoLock`; держать замок здесь — взаимоблокировка).
	for (const FTimedTicket& T : TimedTickets)
	{
		const bool bRemoved = FAsdaTimedSend::Get().Cancel(T.Ticket);
		// ★ Ревизия сети НИЗК-6: снятая постановка получает итог «снят» — владелец не ждёт её вечно.
		if (bRemoved && T.Result.IsValid() && T.Result->State.load() == 0) { T.Result->State = 3; }
	}
	TimedTickets.Reset();
	FScopeLock IoL(&IoLock);
	++ConnEpoch;                  // ★ ревизия сети НИЗК-4: постановки старого соединения в новое не уйдут
	bSocketDead = false;          // ★ ревизия сети СРЕДН-1
	bWorkerFatal = false;         //   фатал фона относился к старому сокету — новое соединение он не рвёт
	if (Socket)
	{
		Socket->Close();
		ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(Socket);
		Socket = nullptr;
	}
	bConnected = false;
	RecvBuffer.Reset();
	++RecvEpoch;                  // буфер сброшен — все идущие разборы обязаны прекратиться
	bStopDrain = false;
	SecureRaw.Reset();
	bSecureActive = false;
	Secure.Reset();               // сброс канала: ключи затрутся в ~FAsdaSecureClient
	// ★ EAC ред. 3 (E1-08): незавершённое неблокирующее подключение снимается (фоновый резолв допишет в свой блок и отпустит его).
	bConnectPending = false;
	bResolvePending = false;
	PendingResolve.Reset();
	PendingRawOut.Reset();
}

// ★★★ 18.10.2026: см. объявление. Сокет читает только игровой поток; замок — лишь на shutdown (не пересечься с точной отправкой).
void FAsdaConnection::DisconnectGraceful(float MaxWaitSec)
{
	if (Socket && bConnected && !bTestWire)
	{
		{
			FScopeLock IoL(&IoLock);
			Socket->Shutdown(ESocketShutdownMode::Write);
		}
		const double Deadline = FPlatformTime::Seconds() + FMath::Clamp((double)MaxWaitSec, 0.0, 2.0);
		uint8 Sink[4096];
		int32 Dropped = 0;
		bool bPeerClosed = false;
		for (double Left = Deadline - FPlatformTime::Seconds(); Left > 0.0; Left = Deadline - FPlatformTime::Seconds())
		{
			if (!Socket->Wait(ESocketWaitConditions::WaitForRead, FTimespan::FromSeconds(FMath::Min(Left, 0.05)))) { continue; }
			int32 Got = 0;
			if (!Socket->Recv(Sink, sizeof(Sink), Got) || Got <= 0)
			{
				if (!AsdaSocketWouldBlock()) { bPeerClosed = true; break; }   // FIN/RST сервера — он дочитал наш поток
				continue;
			}
			Dropped += Got;
		}
		UE_LOG(LogTemp, Log, TEXT("[AsdaConnection] мягкое закрытие: %s, входящих выброшено %d б"),
			bPeerClosed ? TEXT("сервер закрыл сам") : TEXT("ожидание истекло"), Dropped);
	}
	Disconnect();
}

bool FAsdaConnection::IsConnected() const
{
	return HasWire();
}

bool FAsdaConnection::SendPacket(uint16 Opcode, const uint8* Body, int32 BodyLen, uint16 HdrA, uint16 HdrB, bool bPad)
{
	return SendPacketImpl(Opcode, Body, BodyLen, HdrA, HdrB, bPad, /*bStampSecret=*/false);
}

bool FAsdaConnection::SendPacketSecret(uint16 Opcode, const uint8* Body, int32 BodyLen, uint16 HdrA, uint16 HdrB, bool bPad)
{
	return SendPacketImpl(Opcode, Body, BodyLen, HdrA, HdrB, bPad, /*bStampSecret=*/true);
}

bool FAsdaConnection::SendPacketImpl(uint16 Opcode, const uint8* Body, int32 BodyLen, uint16 HdrA, uint16 HdrB, bool bPad, bool bStampSecret)
{
	if (!HasWire()) return false;
	if (bRawStream) return false;   // ★ EAC Э1: кадр FB..FE в канал сырого потока (сайдкар EAC) не пускаем
	// ★★ МЕЛК-2 (ревизия shop_offline_fix, 10.10.2026): вето владельца — кадр не собирается и в сокет не уходит
	//   (прерванный появившимся соединением офлайн-самотест не отправляет ничего; правило `Docs/Port/systems/SELFTESTS_OFFLINE.md`).
	//   Провод самотеста (сокета нет, в сеть ничего не уходит) вето не подлежит — его ставит сам офлайн-самотест.
	if (!bTestWire && SendVeto && SendVeto(Opcode)) { return false; }
	const TCHAR* FatalWhy = nullptr;
	bool bOk = false;
	{
		FScopeLock IoL(&IoLock);   // ★ 15.10.2026: сокет и шифрование делит поток точной отправки
		bOk = SendFrameCore(Opcode, Body, BodyLen, HdrA, HdrB, bPad, FatalWhy, bStampSecret);
	}
	if (!bOk && FatalWhy) { MarkClosed(FatalWhy); }   // колбэк разрыва — ВНЕ замка
	return bOk;
}

void FAsdaConnection::DebugAttachTestWire(FTestWire InWire)
{
	FScopeLock IoL(&IoLock);
	TestWire = MoveTemp(InWire);
	bTestWire = true;
	bConnected = true;
	bSocketDead = false;
}

void FAsdaConnection::DebugDetachTestWire()
{
	for (const FTimedTicket& T : TimedTickets) { FAsdaTimedSend::Get().Cancel(T.Ticket); }
	TimedTickets.Reset();
	FScopeLock IoL(&IoLock);
	++ConnEpoch;
	TestWire = nullptr;
	bTestWire = false;
	if (!Socket) { bConnected = false; }
}

uint64 FAsdaConnection::SendPacketAt(uint16 Opcode, const uint8* Body, int32 BodyLen, bool bPad, double DeadlineSec,
	TSharedPtr<FTimedResult, ESPMode::ThreadSafe> Result, bool bStampSecret)
{
	if (!HasWire() || !Result) return 0;
	if (bRawStream) return 0;   // ★ EAC Э1: точная отправка кадра FB..FE каналу сырого потока не положена
	if (!bTestWire && SendVeto && SendVeto(Opcode)) { return 0; }
#if PLATFORM_WINDOWS
	// ★ Ревизия сети НИЗК-3: до готовности рукопожатия шифрования кадр ушёл бы в очередь канала, а не на провод — «момент
	//   ухода» был бы ложным. Точная отправка в таком канале не ставится (удар 4028 бывает только в мире, после рукопожатия).
	if (bSecureActive && Secure && !Secure->IsReady()) { return 0; }
#endif
	TArray<uint8> Copy;
	if (Body && BodyLen > 0) { Copy.Append(Body, BodyLen); }
	TSharedPtr<FAlive, ESPMode::ThreadSafe> A = Alive;
	uint32 Epoch = 0;
	{
		FScopeLock IoL(&IoLock);
		Epoch = ConnEpoch;
	}
	const uint64 Ticket = FAsdaTimedSend::Get().Schedule(DeadlineSec, [A, Copy = MoveTemp(Copy), Opcode, bPad, Result, Epoch, bStampSecret](double)
	{
		FScopeLock AL(&A->Lock);
		FAsdaConnection* C = A->Conn;
		if (!C) { Result->State = 3; return; }
		bool bOk = false;
		bool bStale = false;
		double SentAt = 0.0;
		{
			FScopeLock IoL(&C->IoLock);
			if (C->ConnEpoch != Epoch) { bStale = true; }   // соединение сменилось — в новое не уходим
			else if (C->HasWire())
			{
				const TCHAR* FatalWhy = nullptr;
				SentAt = FPlatformTime::Seconds();
				bOk = C->SendFrameCore(Opcode, Copy.GetData(), Copy.Num(), 0, 0, bPad, FatalWhy, bStampSecret);
				if (!bOk && FatalWhy) { C->bSocketDead = true; C->bWorkerFatal = true; }
			}
		}
		Result->SentAtSec = SentAt;
		Result->State = bStale ? 3 : (bOk ? 1 : 2);
	});
	if (Ticket != 0)
	{
		// Обрезать только завершённые постановки (ждущие обязаны сниматься в `Disconnect`).
		TimedTickets.RemoveAll([](const FTimedTicket& T) { return !T.Result.IsValid() || T.Result->State.load() != 0; });
		FTimedTicket T; T.Ticket = Ticket; T.Result = Result;
		TimedTickets.Add(MoveTemp(T));
	}
	return Ticket;
}

void FAsdaConnection::CancelTimed(uint64 Ticket)
{
	FAsdaTimedSend::Get().Cancel(Ticket);
	TimedTickets.RemoveAll([Ticket](const FTimedTicket& T) { return T.Ticket == Ticket; });
}

bool FAsdaConnection::SendFrameCore(uint16 Opcode, const uint8* Body, int32 BodyLen, uint16 HdrA, uint16 HdrB, bool bPad,
	const TCHAR*& OutFatalWhy, bool bStampSecret)
{
	OutFatalWhy = nullptr;
	if (!HasWire() || bSocketDead) return false;   // ★ ревизия сети СРЕДН-1: в мёртвый сокет — ни кадра
	// ★★★ Ревизия сети БЛОКЕР-1: анти-реплей-номер выдаётся ЗДЕСЬ, под замком, в порядке ухода на провод.
	TArray<uint8> Stamped;
	if (bStampSecret && SecretFiller && Body && BodyLen >= 8)
	{
		Stamped.Append(Body, BodyLen);
		SecretFiller(Stamped.GetData());
		Body = Stamped.GetData();
	}
	if (bTestWire)
	{
		if (TestWire) { TestWire(Opcode, Body, BodyLen); }
		return true;
	}
	std::vector<uint8_t> frame = Asda::PacketFrame::Build(Opcode, Body, (size_t)BodyLen, HdrA, HdrB, bPad);
	// ★ В7b: при включённом шифровании — запечатать УЖЕ собранный кадр (frame+XOR) во внешний конверт.
	//   OnSend до завершения рукопожатия копит кадр и возвращает пусто (считаем «отправлено» — вытолкнется
	//   первым Seal после готовности). Ошибка целостности — рвём соединение.
#if PLATFORM_WINDOWS
	std::vector<uint8_t> sealed;
	if (bSecureActive && Secure)
	{
		if (!Secure->OnSend(frame.data(), frame.size(), sealed))
		{
			UE_LOG(LogTemp, Warning, TEXT("AsdaConn: шифрование исходящего кадра (опкод %u) не удалось: %hs — разрыв"),
				(uint32)Opcode, Secure->LastErrName());
			OutFatalWhy = TEXT("ошибка шифрования send");
			return false;
		}
		if (sealed.empty()) { return true; }   // кадр отложен до рукопожатия
		frame.swap(sealed);
	}
#endif
	// Non-blocking сокет может отправить кадр ЧАСТИЧНО — досылаем остаток, иначе рассинхрон потока.
	const int32 total = (int32)frame.size();
	int32 off = 0;
	int32 stall = 0;                       // счётчик итераций без прогресса (EWOULDBLOCK)
	while (off < total)
	{
		int32 sent = 0;
		const bool bOk = Socket->Send(frame.data() + off, total - off, sent);
		if (bOk && sent > 0) { off += sent; stall = 0; continue; }
		// ★ Ошибка НЕ-EWOULDBLOCK = сокет мёртв (сервер уже прислал RST). Раньше порт крутил здесь 2000
		//   итераций по 0.5 мс (=1 с фриза на КАЖДУЮ отправку) и молча возвращал false, оставляя
		//   bConnected=true → «подключены», кнопка входа слала в никуда. Теперь — сразу разрыв.
		if (!bOk && !AsdaSocketWouldBlock())
		{
			OutFatalWhy = TEXT("ошибка send");
			return false;
		}
		// нет прогресса: EWOULDBLOCK (буфер сокета полон) — короткий бэкофф; лимит → считаем разрывом
		if (++stall >= 2000)
		{
			UE_LOG(LogTemp, Warning, TEXT("AsdaConn: send stalled at %d/%d"), off, total);
			OutFatalWhy = TEXT("send stalled");
			return false;
		}
		FPlatformProcess::SleepNoStats(0.0005f);
	}
	return true;
}

void FAsdaConnection::Tick()
{
	// ★ EAC ред. 3 (E1-08): неблокирующее подключение (ConnectAsync) ведётся здесь — игровой поток его не ждёт.
	if (bConnectPending && !TickPendingConnect()) { return; }
	if (!Socket || !bConnected) return;
	// ★ 15.10.2026: неустранимая ошибка фоновой отправки — разрыв здесь, на игровом потоке (колбэки только тут).
	if (bWorkerFatal.exchange(false)) { MarkClosed(TEXT("ошибка send (точная отправка)")); return; }

	FSocket* const S0 = Socket;      // колбэки ниже могут закрыть/пересоздать сокет — сверяем идентичность
	bool bRemoteClosed = false;

	// ★ В7b: при шифровании принимаем в отдельный сырой буфер (шифротекст), иначе — прямо в RecvBuffer.
	//   Расшифрованный старый поток ложится в RecvBuffer ниже (SecureIngest), дальше DrainFrames как есть.
	const bool bSecure = bSecureActive && Secure;
	TArray<uint8>& Target = bSecure ? SecureRaw : RecvBuffer;
	if (bSecure) { SecureRaw.Reset(); }

	uint32 pending = 0;
	while (Socket->HasPendingData(pending) && pending > 0)
	{
		const int32 start = Target.Num();
		Target.AddUninitialized((int32)pending);
		int32 read = 0;
		if (!Socket->Recv(Target.GetData() + start, (int32)pending, read) || read <= 0)
		{
			Target.SetNum(start, EAllowShrinking::No);
			if (!AsdaSocketWouldBlock()) { bRemoteClosed = true; }
			break;
		}
		Target.SetNum(start + read, EAllowShrinking::No);
	}
#if PLATFORM_WINDOWS
	if (bSecure && SecureRaw.Num() > 0)
	{
		bool bIngestOk = false;
		{
			FScopeLock IoL(&IoLock);   // ★ 15.10.2026: расшифровка делит объект канала с потоком точной отправки
			bIngestOk = SecureIngest(SecureRaw.GetData(), SecureRaw.Num());
		}
		if (!bIngestOk)
		{
			// ошибка целостности (подпись/тег/повтор) — соединение рвём, как при FIN/RST (причину написал SecureIngest)
			if (Socket == S0) { MarkClosed(TEXT("ошибка целостности защищённого канала")); }
			return;
		}
		SecureRaw.Reset();
	}
	// ★ В7d: рукопожатие завершилось на этом приёме → вытолкнуть очередь сразу; ServerHello нет дольше срока → разрыв.
	// ★ fix2b (сервер L07/P61): кадры, отправленные сервером ДО рукопожатия (отказ LM 4101/2253, GS 6004 «сервер переполнен»),
	//   приходят сразу за ServerHello, и сервер тут же закрывает соединение. Разобрать их ДО выталкивания своей очереди:
	//   её send в уже закрытый сервером сокет может не пройти, и тогда SecureAfterIngest рвёт соединение раньше DrainFrames.
	if (bSecure && Secure && Secure->IsReady() && RecvBuffer.Num() > 0)
	{
		ConsumeInbound();   // ★ EAC Э1: для игровых соединений это DrainFrames(), как было
		if (Socket != S0 || !bConnected) { return; }   // колбэк пакета закрыл/пересоздал соединение
	}
	if (bSecure && !SecureAfterIngest()) { return; }
	if (bSecure && bRemoteClosed && Secure && !Secure->IsReady())
	{
		UE_LOG(LogTemp, Warning, TEXT("AsdaConn: сервер %s закрыл соединение ДО завершения рукопожатия шифрования — на сервере "
			"не включено шифрование (L07/P61 Enabled=0 / ключ не годен) или ClientHello отвергнут (см. PatchLog сервера: "
			"«[нарушение канала]»). Откат на старый протокол клиентом не выполняется."), *SecurePeer);
	}
#endif

	// ★ ДЕТЕКТ ОБРЫВА. HasPendingData() (FIONREAD) при полученном FIN даёт 0 — «нет данных» и «сервер
	//   закрыл» неотличимы, поэтому порт не замечал кик и висел «подключённым». select(readable) при FIN
	//   срабатывает, а recv(PEEK) отдаёт 0 байт — это и есть признак закрытия (BSD-семантика).
	if (!bRemoteClosed && Socket->Wait(ESocketWaitConditions::WaitForRead, FTimespan::Zero()))
	{
		uint8 Probe = 0;
		int32 got = 0;
		const bool bPeekOk = Socket->Recv(&Probe, 1, got, ESocketReceiveFlags::Peek);
		if ((!bPeekOk || got <= 0) && !AsdaSocketWouldBlock()) { bRemoteClosed = true; }
	}

	ConsumeInbound();   // разобрать всё принятое ДО обрыва (последний пакет обычно = кик 4101); ★ EAC Э1: игровым — DrainFrames()

	// колбэк пакета мог сам закрыть/переподключить соединение — тогда чужой сокет не трогаем
	if (bRemoteClosed && Socket == S0) { MarkClosed(TEXT("FIN/RST от сервера")); }
}

// ★★★ ЗАЩИТА В ГЛУБИНУ ОТ РЕЭНТРАНТНОСТИ (краш 2026-08-04:
//   «Array range out of bounds: index 0 and length 289 into an array of size 0», AsdaConnection.cpp:177).
//   Причина класса: колбэк пакета может ПЕРЕСОЗДАТЬ соединение (портал/смена канала/кик). Прежний код
//   кэшировал `avail` до цикла и проверял только `!Socket` — а после Disconnect+Connect сокет СНОВА не
//   nullptr, гард не срабатывал, RecvBuffer уже пуст, и финальный RemoveAt(0, 289) падал на массиве 0.
//   Теперь три независимых рубежа:
//     1) ПОКОЛЕНИЕ буфера (RecvEpoch): любой Connect/Disconnect его меняет → выходим немедленно;
//     2) `avail` пересчитывается КАЖДУЮ итерацию из RecvBuffer.Num() (буфер мог укоротиться);
//     3) RemoveAt клампится по фактическому размеру.
//   Плюс кооперативный флаг bStopDrain: обработчик, который собирается сменить соединение, зовёт
//   RequestStopDrain() — остаток кадров СТАРОГО соединения не разбирается (они уже неактуальны).
void FAsdaConnection::DrainFrames()
{
	size_t offset = 0;
	const uint32 Epoch0 = RecvEpoch;
	bStopDrain = false;
	// ★ R4, доля a (14.11.2026): пакет один на весь проход — память его тела переиспользуется (разбор заполняет все поля
	//   заново). Пакет остаётся локальной переменной этого вызова, обработчик получает его по ссылке, как и раньше; повторный
	//   вход в разбор из обработчика заводит свой пакет.
	Asda::ParsedPacket pkt;
	while (true)
	{
		const size_t avail = (size_t)RecvBuffer.Num();
		if (offset >= avail) { break; }
		// пропустить мусор до маркера 0xFB
		if (RecvBuffer[(int32)offset] != Asda::PacketFrame::MARK_BEGIN) { ++offset; continue; }
		size_t consumed = 0;
		{
			ASDA_PERF_SCOPE("Net.FrameParse");
			consumed = Asda::PacketFrame::ParseOneCopy(RecvBuffer.GetData(), avail, offset, pkt);
		}
		if (consumed == 0) break;     // неполный кадр — ждём дочитки
		if (OnPacket) OnPacket(pkt);
		// Рубеж 1: соединение закрыто/пересоздано внутри колбэка — буфер уже чужой.
		if (!Socket || RecvEpoch != Epoch0)
		{
			UE_LOG(LogTemp, Warning, TEXT("[net] разбор прерван: соединение сменилось внутри колбэка (эпоха %u→%u)"),
				Epoch0, RecvEpoch);
			return;
		}
		offset += consumed;
		// Кооперативный стоп: обработчик просит не разбирать остаток кадров старого соединения.
		if (bStopDrain)
		{
			UE_LOG(LogTemp, Log, TEXT("[net] разбор остановлен по запросу обработчика (обработано %d б)"), (int32)offset);
			break;
		}
	}
	// убрать обработанное из начала буфера (Рубеж 3: кламп по ФАКТИЧЕСКОМУ размеру)
	const int32 Take = FMath::Clamp((int32)offset, 0, RecvBuffer.Num());
	if (Take > 0)
	{
		RecvBuffer.RemoveAt(0, Take, EAllowShrinking::No);
	}
}

// ── ★ EAC Э1 (16.10.2026): режим сырого потока (канал 15901 к сайдкару античита) ──────────────────────
void FAsdaConnection::SetRawStreamMode(FOnRawData InCb)
{
	bRawStream = true;
	OnRawData = MoveTemp(InCb);
}

void FAsdaConnection::ConsumeInbound()
{
	if (bRawStream) { DeliverRaw(); }
	else            { DrainFrames(); }   // игровые соединения — прежний разбор кадров FB..FE без изменений
}

void FAsdaConnection::DeliverRaw()
{
	if (RecvBuffer.Num() == 0) { return; }
	TArray<uint8> Chunk = MoveTemp(RecvBuffer);   // буфер соединения пуст ДО колбэка (колбэк может рвать соединение)
	RecvBuffer.Reset();
	if (OnRawData) { OnRawData(Chunk.GetData(), Chunk.Num()); }
}

bool FAsdaConnection::SendRaw(const uint8* Data, int32 Len)
{
	if (!bRawStream || !Data || Len <= 0) { return false; }
	// ★ EAC ред. 3 (E1-08): подключение ещё идёт (ConnectAsync) — байты ждут установления и уйдут сразу после ClientHello.
	if (bConnectPending) { PendingRawOut.Emplace(Data, Len); return true; }
	if (!HasWire()) { return false; }
#if PLATFORM_WINDOWS
	const TCHAR* FatalWhy = nullptr;
	bool bOk = false;
	{
		FScopeLock IoL(&IoLock);
		if (!bSocketDead && bSecureActive && Secure)
		{
			std::vector<uint8_t> sealed;
			if (!Secure->OnSend(Data, (size_t)Len, sealed))
			{
				UE_LOG(LogTemp, Warning, TEXT("AsdaConn: шифрование сырого потока (%d б) не удалось: %hs — разрыв"), Len, Secure->LastErrName());
				FatalWhy = TEXT("ошибка шифрования send (сырой поток)");
			}
			else if (sealed.empty())
			{
				bOk = true;   // рукопожатие ещё идёт — байты в очереди канала, уйдут сразу после ServerHello (SecureAfterIngest)
			}
			else if (!SecureSendRaw(sealed.data(), (int32)sealed.size()))
			{
				FatalWhy = TEXT("ошибка send (сырой поток)");
			}
			else
			{
				bOk = true;
			}
		}
	}
	if (!bOk && FatalWhy) { MarkClosed(FatalWhy); }   // колбэк разрыва — ВНЕ замка
	return bOk;
#else
	return false;
#endif
}

// ── ★ EAC ред. 3 (16.10.2026): привязка канала и неблокирующее подключение ─────────────────────────────

bool FAsdaConnection::GetChannelBinding(uint8 Out32[32])
{
#if PLATFORM_WINDOWS
	FScopeLock IoL(&IoLock);   // объект канала делит поток точной отправки
	return Out32 && HasWire() && bSecureActive && Secure && Secure->ChannelBinding(Out32);
#else
	return false;
#endif
}

bool FAsdaConnection::ConnectAsync(const FString& Host, int32 Port, int32 SecureRole, float TimeoutSec)
{
	Disconnect();
	bClosedByConnectFail = false;   // ★ 26.10.2026: новое подключение — признак прошлой неудачи снят
	// Прогон без сети — ни сокета, ни адреса в журнале (как у Connect).
	if (IsNoNetRun())
	{
		UE_LOG(LogTemp, Warning, TEXT("AsdaConn: ключ -AsdaNoNet — подключение к серверу НЕ выполняется (прогон без сети, порт %d)"), Port);
		return false;
	}
#if !PLATFORM_WINDOWS
	// Канал сырого потока без шифрования не поднимается — AsdaSecure есть только на Windows (CNG), как в Connect.
	if (bRawStream) { return false; }
#endif
	ISocketSubsystem* SocketSub = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	if (!SocketSub) { return false; }
	bConnectPending = true;
	PendingStartSec = FPlatformTime::Seconds();
	PendingDeadlineSec = (TimeoutSec > 0.f) ? PendingStartSec + (double)TimeoutSec : 0.0;
	PendingHost = Host;
	PendingPort = Port;
	PendingRole = SecureRole;
	PendingRawOut.Reset();

	FIPv4Address Addr;
	if (FIPv4Address::Parse(Host, Addr))
	{
		TSharedPtr<FInternetAddr> InetAddr = SocketSub->CreateInternetAddr();
		InetAddr->SetIp(Addr.Value);
		InetAddr->SetPort(Port);
		FString Why;
		if (!StartPendingSocket(InetAddr, Why))
		{
			UE_LOG(LogTemp, Warning, TEXT("AsdaConn: неблокирующее подключение к %s:%d отклонено сразу (%s)"), *Host, Port, *Why);
			Disconnect();
			return false;
		}
		return true;
	}
	// Имя хоста — фоновый резолв; Tick заберёт результат без ожидания (тот же блок, что у AsdaResolveHostBounded).
	//   ★ 16.10.2026: блок общий с кэшем имён (AsdaAcquireResolve) — при свежем адресе фаза резолва проходит за один Tick.
	PendingResolve = AsdaAcquireResolve(*SocketSub, Host);
	bResolvePending = true;
	return true;
}

void FAsdaConnection::DebugBeginStalledConnect(float TimeoutSec)
{
	Disconnect();
	bConnectPending = true;
	bResolvePending = true;
	PendingResolve = MakeShared<FAsdaResolveState, ESPMode::ThreadSafe>();   // bDone никогда не взводится — «DNS молчит»
	PendingStartSec = FPlatformTime::Seconds();
	PendingDeadlineSec = (TimeoutSec > 0.f) ? PendingStartSec + (double)TimeoutSec : 0.0;
	PendingHost = TEXT("selftest.invalid");
	PendingPort = 0;
	PendingRole = -1;
	PendingRawOut.Reset();
}

bool FAsdaConnection::StartPendingSocket(const TSharedPtr<FInternetAddr>& Addr, FString& OutWhy)
{
	ISocketSubsystem* SocketSub = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	if (!SocketSub || !Addr.IsValid()) { OutWhy = TEXT("нет адреса"); return false; }
	Socket = FTcpSocketBuilder(TEXT("AsdaConnAsync")).AsNonBlocking().Build();
	if (!Socket) { OutWhy = TEXT("сокет не создан"); return false; }
	// Неблокирующий connect: true = «идёт» (EWOULDBLOCK/EINPROGRESS) или «готово»; false = отказ ещё на старте.
	if (!Socket->Connect(*Addr))
	{
		OutWhy = SocketSub->GetSocketError(SE_GET_LAST_ERROR_CODE);
		return false;
	}
	return true;
}

void FAsdaConnection::FailPendingConnect(const FString& Why)
{
	UE_LOG(LogTemp, Warning, TEXT("AsdaConn: неблокирующее подключение к %s:%d не состоялось за %.0f мс (%s)"),
		*PendingHost, PendingPort, (FPlatformTime::Seconds() - PendingStartSec) * 1000.0, *Why);
	Disconnect();                // сокет закрыт, состояние подключения снято, накопленный SendRaw выброшен
	bClosedByConnectFail = true;  // ★ 26.10.2026: владелец отличит неудачу подключения от обрыва (ConsumeClosedByConnectFail)
	if (OnClosed) { OnClosed(); }   // владелец узнаёт об отказе тем же путём, что об обрыве (у EAC — отложенно, в его Tick)
}

bool FAsdaConnection::TickPendingConnect()
{
	ISocketSubsystem* SocketSub = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	if (!SocketSub) { FailPendingConnect(TEXT("нет сокетной подсистемы")); return false; }
	const double Now = FPlatformTime::Seconds();
	const bool bExpired = PendingDeadlineSec > 0.0 && Now >= PendingDeadlineSec;

	// Фаза 1: резолв имени.
	if (bResolvePending)
	{
		if (!PendingResolve.IsValid() || !PendingResolve->bDone.load(std::memory_order_acquire))
		{
			if (bExpired) { FailPendingConnect(TEXT("имя не разрешилось в отведённое время — DNS не отвечает")); }
			return false;
		}
		bResolvePending = false;
		// ★ 16.10.2026: блок резолва общий (кэш имён) — порт ставим в КОПИЮ адреса.
		TSharedPtr<FInternetAddr> Addr = PendingResolve->Addr.IsValid() ? PendingResolve->Addr->Clone() : TSharedPtr<FInternetAddr>();
		const ESocketErrors Code = PendingResolve->Code;
		PendingResolve.Reset();
		if (!Addr.IsValid())
		{
			AsdaForgetResolvedHost(PendingHost);
			FailPendingConnect(FString::Printf(TEXT("имя не разрешилось: %s"), SocketSub->GetSocketError(Code)));
			return false;
		}
		Addr->SetPort(PendingPort);
		FString Why;
		if (!StartPendingSocket(Addr, Why)) { FailPendingConnect(FString::Printf(TEXT("connect отклонён сразу: %s"), *Why)); return false; }
	}

	// Фаза 2: неблокирующий connect — только опрос с нулевым ожиданием (игровой поток не стоит).
	if (!Socket) { FailPendingConnect(TEXT("сокета нет")); return false; }
	if (Socket->Wait(ESocketWaitConditions::WaitForWrite, FTimespan::Zero()))
	{
		TSharedRef<FInternetAddr> Peer = SocketSub->CreateInternetAddr();
		if (!Socket->GetPeerAddress(*Peer))
		{
			FailPendingConnect(FString::Printf(TEXT("connect: %s"), SocketSub->GetSocketError(SE_GET_LAST_ERROR_CODE)));
			return false;
		}
		return FinishPendingConnect();
	}
	bool bPendingConn = false;
	if (!Socket->HasPendingConnection(bPendingConn))   // взведена ошибка сокета = отказ (RST) или SYN-таймаут ОС
	{
		FailPendingConnect(FString::Printf(TEXT("сервер отклонил подключение: %s"), SocketSub->GetSocketError(SE_GET_LAST_ERROR_CODE)));
		return false;
	}
	if (bExpired) { FailPendingConnect(TEXT("таймаут — сервер не отвечает")); }
	return false;
}

bool FAsdaConnection::FinishPendingConnect()
{
	bConnectPending = false;
	{
		FScopeLock IoL(&IoLock);
		bConnected = true;
		bSocketDead = false;
	}
	RecvBuffer.Reset();
	++RecvEpoch;
	bStopDrain = false;
	SecureRaw.Reset();
	bSecureActive = false;
	bSecureReadyLogged = false;
	UE_LOG(LogTemp, Log, TEXT("AsdaConn: неблокирующее подключение к %s:%d установлено за %.0f мс"),
		*PendingHost, PendingPort, (FPlatformTime::Seconds() - PendingStartSec) * 1000.0);
	// Рукопожатие — по тем же правилам, что в Connect (в сыром потоке шифрование обязательно).
	if (bRawStream || CVarAsdaNetSecure.GetValueOnAnyThread() != 0)
	{
		if (!bRawStream && Asda::ChannelCipher::GetProfile() == Asda::EKeyProfile::XyT)
		{
			UE_LOG(LogTemp, Warning, TEXT("AsdaConn: профиль XyT (чужой сервер) — шифрование трафика к %s:%d НЕ применяется"), *PendingHost, PendingPort);
		}
#if PLATFORM_WINDOWS
		else
		{
			SecurePeer = FString::Printf(TEXT("%s:%d"), *PendingHost, PendingPort);
			if (!SecureBeginHandshake(PendingPort, PendingRole))
			{
				PendingRawOut.Reset();
				// Соединение уже «установлено» — отказ рукопожатия идёт штатным разрывом (OnClosed).
				MarkClosed(TEXT("не удалось начать рукопожатие шифрования"));
				return false;
			}
		}
#endif
	}
	// Байты, отправленные до установления, — в канал (до ServerHello они ждут в очереди AsdaSecure).
	TArray<TArray<uint8>> Q = MoveTemp(PendingRawOut);
	PendingRawOut.Reset();
	for (const TArray<uint8>& M : Q)
	{
		if (!SendRaw(M.GetData(), M.Num())) { return false; }   // отказ уже оформлен разрывом внутри SendRaw
	}
	return true;
}

// ── ★ В7b: шифрование трафика — вспомогательные методы ───────────────────────────────────────
#if PLATFORM_WINDOWS
// Начать рукопожатие: Configure (под-сервер по порту) + Start → ClientHello (первый байт 0xA5) в сокет.
//   ClientHello — НЕ кадр старого протокола, поэтому уходит прямо в сокет, минуя SendPacket/XOR.
bool FAsdaConnection::SecureBeginHandshake(int32 Port, int32 Role)
{
	if (!Socket) { return false; }
	// Под-сервер входит в подпись сервера и HKDF. ★ В7d: роль задаёт вызывающий (ConnectToLogin → 1,
	//   EnterGameWorld → 0); −1 — прежнее правило «15001 = логин, иначе игра».
	// ★ EAC Э1: роль 4 — сайдкар античита (SubServer::Eac). Для ролей 1/0/−1 bEac == false — выбор тот же, что был.
	const bool bEac   = (Role == 4);
	const bool bLogin = !bEac && ((Role == 1) || (Role < 0 && Port == 15001));
	const AsdaSecure::SubServer Sub = bEac ? AsdaSecure::SubServer::Eac
		: (bLogin ? AsdaSecure::SubServer::Login : AsdaSecure::SubServer::Game);
	SecurePeer = FString::Printf(TEXT("%s (%s)"), *SecurePeer, bEac ? TEXT("EAC") : (bLogin ? TEXT("логин") : TEXT("игра")));
	Secure = MakeUnique<FAsdaSecureClient>();
	Secure->Configure(Sub, /*legacy=*/false);
	std::vector<uint8_t> hello;
	if (!Secure->Start(hello) || hello.empty())
	{
		UE_LOG(LogTemp, Warning, TEXT("AsdaConn: Secure->Start не дал ClientHello (CNG недоступен?)"));
		Secure.Reset();
		return false;
	}
	// Отправить ClientHello целиком (non-blocking: досылаем остаток).
	if (!SecureSendRaw(hello.data(), (int32)hello.size())) { Secure.Reset(); return false; }
	bSecureActive = true;
	SecureStartSec = FPlatformTime::Seconds();
	UE_LOG(LogTemp, Log, TEXT("AsdaConn: рукопожатие шифрования начато с %s (ClientHello %d б)"), *SecurePeer, (int32)hello.size());
	return true;
}

// ★ В7d: отправить готовые к проводу байты (ClientHello / вытолкнутая очередь) с досылкой остатка. false — сокет мёртв.
bool FAsdaConnection::SecureSendRaw(const uint8* Data, int32 Len)
{
	if (!Socket) { return false; }
	int32 off = 0, stall = 0;
	while (off < Len)
	{
		int32 sent = 0;
		const bool bOk = Socket->Send(Data + off, Len - off, sent);
		if (bOk && sent > 0) { off += sent; stall = 0; continue; }
		if (!bOk && !AsdaSocketWouldBlock()) { return false; }
		if (++stall >= 2000) { return false; }
		FPlatformProcess::SleepNoStats(0.0005f);
	}
	return true;
}

// ★ В7d: после приёма. Рукопожатие завершилось → строка в лог + очередь (кадры, отправленные ДО ServerHello, например
//   вход в мир 20000 сразу после connect) уходит СРАЗУ, а не со следующей отправкой. Не завершилось за срок → разрыв.
bool FAsdaConnection::SecureAfterIngest()
{
	if (!Secure || !Socket || !bConnected) { return true; }
	if (Secure->IsReady())
	{
		bool bJustReady = false;   // ★ EAC ред. 3: рукопожатие завершилось на ЭТОМ приёме — колбэк OnSecureReady ниже
		if (!bSecureReadyLogged)
		{
			bSecureReadyLogged = true;
			bJustReady = true;
			UE_LOG(LogTemp, Log, TEXT("AsdaConn: рукопожатие шифрования с %s завершено за %.0f мс — подпись сервера проверена, канал AES-256-GCM"),
				*SecurePeer, (FPlatformTime::Seconds() - SecureStartSec) * 1000.0);
		}
		const TCHAR* FatalWhy = nullptr;
		{
			FScopeLock IoL(&IoLock);   // ★ 15.10.2026: очередь шифрования и сокет делит поток точной отправки
			if (Secure->HasPending())
			{
				std::vector<uint8_t> out;
				if (!Secure->Flush(out))
				{
					UE_LOG(LogTemp, Warning, TEXT("AsdaConn: выталкивание очереди после рукопожатия не удалось: %hs — разрыв"), Secure->LastErrName());
					FatalWhy = TEXT("ошибка шифрования очереди");
				}
				else if (!out.empty() && !SecureSendRaw(out.data(), (int32)out.size()))
				{
					FatalWhy = TEXT("ошибка send (очередь после рукопожатия)");
				}
			}
		}
		if (FatalWhy) { MarkClosed(FatalWhy); return false; }   // колбэк разрыва — ВНЕ замка
		// ★ EAC ред. 3 (CONTRACT.md §8.1): сообщить владельцу привязку завершённого рукопожатия (BIND сайдкару). Значение
		//   берётся под замком (объект канала делит поток точной отправки), колбэк — ВНЕ замка.
		if (bJustReady && OnSecureReady)
		{
			uint8 Bind[AsdaSecure::kBindLen] = {};
			bool bHaveBind = false;
			uint8 Sub = 0;
			{
				FScopeLock IoL(&IoLock);
				if (Secure) { bHaveBind = Secure->ChannelBinding(Bind); Sub = (uint8)Secure->GetSubServer(); }
			}
			if (bHaveBind) { OnSecureReady(Sub, Bind); }
			FPlatformMemory::Memzero(Bind, sizeof(Bind));
		}
		return true;
	}
	const float Limit = CVarAsdaNetSecureHandshakeTimeoutSec.GetValueOnAnyThread();
	if (Limit > 0.f && FPlatformTime::Seconds() - SecureStartSec > (double)Limit)
	{
		UE_LOG(LogTemp, Warning, TEXT("AsdaConn: сервер %s не ответил на рукопожатие шифрования за %.1f с — на сервере не включено "
			"шифрование ([L07_TrafficEncryption]/[P61_TrafficEncryption] Enabled=0 или secure_server.key не годен — см. PatchLog "
			"сервера) либо ответ глушится по пути. Откат на старый протокол клиентом НЕ выполняется (это был бы обход шифрования) — разрыв."),
			*SecurePeer, Limit);
		MarkClosed(TEXT("нет ответа на рукопожатие шифрования"));
		return false;
	}
	return true;
}

// Расшифровать принятые байты → добавить старый поток в RecvBuffer (его разберёт DrainFrames).
//   ServerHello/рукопожатие OnRecv потребляет сам (в RecvBuffer ничего не кладёт). false = ошибка.
bool FAsdaConnection::SecureIngest(const uint8* Data, int32 Len)
{
	if (!Secure) { return false; }
	std::vector<uint8_t> dec;
	if (!Secure->OnRecv(Data, (size_t)Len, dec))
	{
		// ★ В7d: понятная причина. BadSignature — ServerHello подписан НЕ тем ключом (посредник или ключ сервера не парный
		//   вшитому server_public.h); BadTag/Replay/Format — кадр сервера испорчен/повторён/негодной длины.
		const AsdaSecure::Err E = Secure->LastErr();
		const TCHAR* Why =
			  E == AsdaSecure::Err::BadSignature ? TEXT("подпись сервера не сошлась с вшитым ключом (подмена сервера или ключ сервера не парный клиенту)")
			: E == AsdaSecure::Err::BadTag       ? TEXT("тег AES-GCM не сошёлся (порча/подмена кадра)")
			: E == AsdaSecure::Err::Replay       ? TEXT("номер кадра не следующий (повтор/переупорядочивание)")
			: E == AsdaSecure::Err::Format       ? TEXT("длина кадра вне допустимого")
			: E == AsdaSecure::Err::BadMagic || E == AsdaSecure::Err::BadVersion
			                                     ? TEXT("ответ сервера — не ServerHello (сервер без шифрования или другой версии)")
			:                                      TEXT("сбой рукопожатия/расшифровки");
		UE_LOG(LogTemp, Warning, TEXT("AsdaConn: нарушение целостности защищённого канала с %s: %hs — %s; разрыв"),
			*SecurePeer, Secure->LastErrName(), Why);
		return false;
	}
	if (!dec.empty())
	{
		RecvBuffer.Append(dec.data(), (int32)dec.size());
	}
	return true;
}
#endif // PLATFORM_WINDOWS
