// Copyright (c) 2026 @sitxovski (Discord)
// AsdaConnection — TCP-соединение с сервером AsdaStory (Login или Game).
// Транспорт = сырой FSocket (НЕ UE replication). Принятые байты копятся в буфер,
// из него выбираются кадры PacketFrame::Parse (де-frame + де-XOR) → колбэк OnPacket.
// Отправка: PacketFrame::Build (frame + XOR) → Send.
#pragma once
#include "CoreMinimal.h"
#include "Net/PacketFrame.h"
#include <functional>
#include <atomic>

class FSocket;
class FInternetAddr;            // ★ EAC ред. 3: адрес неблокирующего подключения (ConnectAsync)
struct FAsdaResolveState;       // ★ EAC ред. 3: общий блок фонового резолва имени (определение — AsdaConnection.cpp)
// ★ В7b: криптопрослойка трафика (AsdaSecure::ClientChannel поверх адаптера). Полное определение —
//   Private/Secure/AsdaSecureClient.h (включается только в .cpp). Активна лишь при asda.Net.Secure=1.
class FAsdaSecureClient;


class ASDANET_API FAsdaConnection
{
public:
	// колбэк на каждый принятый разобранный пакет (опкод+тело).
	using FOnPacket = std::function<void(const Asda::ParsedPacket&)>;
	// ★ колбэк «соединение оборвано УДАЛЁННОЙ стороной» (FIN/RST или неустранимая ошибка send/recv).
	//   Эталон: клиент узнаёт об этом сообщением 0x511 lParam=2 (FD_CLOSE) и идёт в Show_DisconnectPopup,
	//   которое ПЕРВЫМ делом закрывает сокет (netCloseSocketBySlot). Порт до этого фикса обрыв НЕ замечал:
	//   HasPendingData() при FIN даёт 0 байт, флаг bConnected оставался true → «подключены» в никуда.
	using FOnClosed = std::function<void()>;
	// ★★ МЕЛК-2 (ревизия shop_offline_fix, 10.10.2026): вето отправки. Владелец соединения (`UAsdaNetSubsystem`) решает,
	//   уходит ли кадр с этим опкодом; true = НЕ отправлять (`SendPacket` вернёт false, сокет не трогается). Пустое вето —
	//   отправляется всё, как раньше. Единая точка «перед каждой отправкой»: через `SendPacket` идут ВСЕ отправители порта.
	using FSendVeto = std::function<bool(uint16 Opcode)>;
	// ★★★ 15.10.2026 (ревизия сети, БЛОКЕР-1): НАПОЛНИТЕЛЬ АНТИ-РЕПЛЕЙ-СЕКРЕТА. Защищённые пакеты несут в байтах тела [0..7]
	//   номер-«секрет» (`UAsdaNetSubsystem::FillAntiReplaySecret`), а сервер (`sub_524390`, GameServer.c:111065-111085)
	//   отбрасывает пакет с «Secret Error», если номер не больше прошлого. Номер обязан выдаваться в ПОРЯДКЕ УХОДА НА ПРОВОД:
	//   удар 4028, поставленный к дедлайну, иначе получал бы номер раньше пакета игрового потока, ушедшего в окне ожидания.
	//   Поэтому номер пишется здесь, под `IoLock`, прямо перед сборкой кадра (`SendFrameCore`) — для ВСЕХ защищённых
	//   отправок (`SendPacketSecret`, `SendPacketAt(…, bStampSecret)`). MD5 тела секрет не покрывает (он считается по
	//   payload [24..]) — дописывать его поздно безопасно. Ставит владелец соединения.
	using FSecretFiller = std::function<void(uint8* Out8)>;
	// ★ Только самотест: «провод без сокета» — кадр (уже со вписанным секретом) отдаётся сюда ПОД `IoLock` в момент ухода,
	//   вместо записи в сокет. Нужен проверке порядка номеров на проводе офлайн (`-AsdaNoNet` сокет не создаёт вовсе).
	using FTestWire = std::function<void(uint16 Opcode, const uint8* Body, int32 BodyLen)>;
	// ★ EAC Э1 (16.10.2026): колбэк «сырого потока» — расшифрованные байты канала AsdaSecure как есть (без кадра FB..FE).
	//   Нужен только каналу 15901 к сайдкару античита (свой кадр `u16 len · u8 type · payload`, CONTRACT.md §3).
	using FOnRawData = std::function<void(const uint8* Data, int32 Len)>;
	// ★ EAC ред. 3 (CONTRACT.md §8.1, 16.10.2026): колбэк «рукопожатие AsdaSecure завершено на этом соединении» — один раз
	//   на соединение, на игровом потоке, ВНЕ замка ввода-вывода. SubServer — под-сервер рукопожатия (1 логин, 2 игра, 4 EAC),
	//   Bind — экспортёр привязки канала (32 б, `AsdaSecure::Session::ChannelBinding`). Владелец (UAsdaNetSubsystem) по нему
	//   шлёт BIND сайдкару античита; рвать/пересоздавать соединение из колбэка нельзя (закон no-reentrant-net-callbacks).
	using FOnSecureReady = std::function<void(uint8 SubServer, const uint8* Bind32)>;

	FAsdaConnection();
	~FAsdaConnection();

	// Поднять соединение. Возврат true = соединение УЖЕ установлено и годно для SendPacket
	//   (контракт не менялся: на него опираются и барьер connect→1003 на логине, и немедленный
	//   хендшейк входа в мир). Сокет неблокирующий с момента создания, а ожидание установления
	//   ограничено CVar `asda.Net.ConnectTimeoutSec` (0 = ждать вердикт ОС, как в оригинале):
	//   раньше неверный адрес морозил игровой поток на SYN-таймаут ОС ~21 с — см. AsdaConnection.cpp.
	//   ★ В7d: SecureRole — под-сервер для рукопожатия шифрования (входит в подпись сервера): 1 — LoginManager,
	//   0 — GameServer/Indun, −1 — по порту (15001 = логин; прежнее поведение). Вызывающий знает роль точно
	//   (ConnectToLogin / EnterGameWorld) — по порту угадывать нельзя: порт логина задаётся в таблице серверов.
	//   ★ EAC Э1: SecureRole 4 — сайдкар античита (SubServer::Eac = 4), только вместе с режимом сырого потока.
	bool Connect(const FString& Host, int32 Port, int32 SecureRole = -1);
	void Disconnect();
	// ★★★ 18.10.2026 (портальный выход 5056): МЯГКОЕ закрытие — shutdown на запись (FIN встаёт в поток ПОСЛЕ уже отправленного),
	//   приём до FIN/RST сервера (не дольше MaxWaitSec, данные выбрасываются), затем обычный `Disconnect()`. Зачем: при
	//   непрочитанных входящих closesocket шлёт RST и может потерять последний пакет (5056 старой карте после раскладки панели).
	void DisconnectGraceful(float MaxWaitSec);

	// ★★ EAC ред. 3 (E1-08, 16.10.2026): НЕБЛОКИРУЮЩЕЕ ПОДКЛЮЧЕНИЕ. В отличие от `Connect`, игровой поток НЕ ждёт ни резолва
	//   имени, ни SYN: вызов только начинает подключение (фоновый резолв или неблокирующий connect) и сразу возвращается.
	//   true — подключение начато (идёт) либо уже установлено; false — отказ сразу (-AsdaNoNet, нет сокетной подсистемы,
	//   connect отвергнут на старте). Дальше всё в `Tick`: установлено → рукопожатие AsdaSecure (SecureRole как у `Connect`);
	//   не установлено за TimeoutSec (общий предел на резолв + connect) или отказ → `Disconnect` и колбэк OnClosed.
	//   SendRaw до установления копит байты и отдаёт их каналу сразу после ClientHello. Используется только каналом EAC 15901
	//   (UAsdaAntiCheatSubsystem); игровые соединения 15001/15601 идут прежним `Connect` — их путь не изменился.
	bool ConnectAsync(const FString& Host, int32 Port, int32 SecureRole, float TimeoutSec);
	// ★ EAC ред. 3: идёт ли неблокирующее подключение (ещё не установлено и не отвергнуто).
	bool IsConnectPending() const { return bConnectPending; }
	// ★ 26.10.2026 (ревизия wave_1025d 4.2): последний OnClosed пришёл от НЕСОСТОЯВШЕГОСЯ неблокирующего подключения
	//   (`FailPendingConnect`: отказ ОС, срок истёк), а не от обрыва живого соединения. Чтение снимает признак; новое
	//   подключение (`Connect`/`ConnectAsync`) тоже.
	bool ConsumeClosedByConnectFail() { const bool b = bClosedByConnectFail; bClosedByConnectFail = false; return b; }
	// ★ EAC ред. 3, только самотест: «подключение, на которое никто не отвечает» БЕЗ сети — состояние ConnectAsync в фазе
	//   резолва с блоком, который никогда не завершится (сокет не создаётся, в сеть ничего не уходит). Проверяет, что Tick не
	//   ждёт, SendRaw копит, а по истечении TimeoutSec — Disconnect и OnClosed (как при недоступном сайдкаре).
	void DebugBeginStalledConnect(float TimeoutSec);

	// ★ EAC ред. 3 (CONTRACT.md §8.1): экспортёр привязки рукопожатия AsdaSecure этого соединения (32 б). false — соединения
	//   нет, шифрование не применяется (asda.Net.Secure 0 / профиль XyT) или рукопожатие ещё не завершено.
	bool GetChannelBinding(uint8 Out32[32]);
	// ★ EAC ред. 3: подписка на «рукопожатие завершено» (см. FOnSecureReady). Ставит владелец один раз.
	void SetOnSecureReady(FOnSecureReady InCb) { OnSecureReady = MoveTemp(InCb); }

	// ★★ EAC Э1 (16.10.2026): РЕЖИМ СЫРОГО ПОТОКА — для канала 15901 к сайдкару античита (CONTRACT.md §3).
	//   Включается ДО Connect владельцем соединения (UAsdaAntiCheatSubsystem) и не выключается. В этом режиме:
	//     · шифрование AsdaSecure ОБЯЗАТЕЛЬНО (дев-ключ asda.Net.Secure и профиль XyT его не отключают; без CNG Connect = false);
	//     · принятое после расшифровки отдаётся колбэку InCb как есть, разбор кадров FB..FE НЕ выполняется;
	//     · отправка — только SendRaw (SendPacket/SendPacketAt кадр FB..FE в такой канал не пускают).
	//   Игровые соединения (15001/15601, UAsdaNetSubsystem) режим не включают — их путь не изменился ни в одной строке
	//   исполнения: все новые ветки стоят за `bRawStream`, который у них всегда false (доказательство — CLIENT_REPORT.md §3).
	void SetRawStreamMode(FOnRawData InCb);
	// ★ EAC Э1: отправить байты сырого потока (шифруются AsdaSecure; до готовности рукопожатия копятся в очереди канала и
	//   уходят сразу после ServerHello). false — не в режиме сырого потока, нет соединения или сокет мёртв (тогда разрыв
	//   идёт штатно через OnClosed).
	bool SendRaw(const uint8* Data, int32 Len);

	// ★★★ [ПОРТ] ПРОГОН БЕЗ СЕТИ — ключ командной строки `-AsdaNoNet` (ревизия shopdbl2 Р-2, 10.10.2026).
	//   Автотесты (`-game` + `-ExecCmds` с самотестами) идут офлайн, а клиент сам подключается к логин-серверу через
	//   ~10 с после старта (ранний коннект `AAsdaLoginGameMode::Tick`) — и самотесты слали пакеты на dev-сервер владельца.
	//   С ключом `Connect` не создаёт сокет вовсе и возвращает false (ни одного подключения за весь процесс).
	//   Правило и список прогонов — `Docs/Port/systems/SELFTESTS_OFFLINE.md`. true = процесс запущен с `-AsdaNoNet`.
	static bool IsNoNetRun();

	// ★★ 16.10.2026 (адрес сервера = домен): ПРЕДВЫБОРКА АДРЕСА ПО ИМЕНИ. Запускает фоновый резолв имени (только IPv4) в общий
	//   кэш имён; следующие `Connect`/`ConnectAsync` к этому имени берут готовый адрес без ожидания на игровом потоке.
	//   IPv4-литерал, пустое имя и прогон `-AsdaNoNet` — ничего не делает. Вызывать заранее (экран логина при старте).
	static void PrefetchHost(const FString& Host);
	// ★ 16.10.2026, только диагностика/самотест: разрешить имя тем же путём, что подключение (кэш + фоновый резолв IPv4),
	//   с ожиданием не дольше TimeoutSec. Ни сокета, ни подключения не создаёт. true — OutIp = адрес в виде «a.b.c.d».
	static bool ResolveHostForDiag(const FString& Host, float TimeoutSec, FString& OutIp);

	// ★★★ ПРЕРВАТЬ ТЕКУЩИЙ РАЗБОР ПРИЁМНОГО БУФЕРА (реэнтрантность).
	//   Обработчик пакета иногда обязан сменить соединение (портал/канал/кик). Рвать сокет прямо
	//   из колбэка НЕЛЬЗЯ — DrainFrames в этот момент идёт по буферу. Правильный порядок:
	//   обработчик зовёт RequestStopDrain() и ставит отложенный флаг, разбор аккуратно завершается,
	//   а смена соединения происходит в UAsdaNetSubsystem::Tick ПОСЛЕ выхода из Tick соединения.
	void RequestStopDrain() { bStopDrain = true; }
	bool IsConnected() const;

	// Отправить пакет (тело уже плейнтекст; кадр+XOR соберёт сам).
	bool SendPacket(uint16 Opcode, const uint8* Body, int32 BodyLen, uint16 HdrA = 0, uint16 HdrB = 0, bool bPad = true);
	// ★ Ревизия сети, БЛОКЕР-1: то же для ЗАЩИЩЁННОГО пакета — байты тела [0..7] (анти-реплей-номер) вписываются
	//   наполнителем `FSecretFiller` в момент ухода, под замком (см. `FSecretFiller`).
	bool SendPacketSecret(uint16 Opcode, const uint8* Body, int32 BodyLen, uint16 HdrA = 0, uint16 HdrB = 0, bool bPad = true);
	void SetSecretFiller(FSecretFiller InFiller) { SecretFiller = MoveTemp(InFiller); }
	// ★ Только самотест: подключить/снять «провод без сокета» (соединение считается установленным, сокета нет).
	void DebugAttachTestWire(FTestWire InWire);
	void DebugDetachTestWire();
	bool IsTestWire() const { return bTestWire; }   // подключён провод самотеста (сокета нет, в сеть ничего не уходит)

	// ★★★ 15.10.2026 (ревизия PORT-RHYTHM СРЕДН-2): ОТПРАВКА К АБСОЛЮТНОМУ МОМЕНТУ. Кадр уходит с фонового потока точной
	//   отправки (`FAsdaTimedSend`) ровно в DeadlineSec (FPlatformTime, доли мс), а не в ближайшем кадре игры — удару 4028 при
	//   20 FPS иначе не уложиться в «серверная длительность + 5 %». Вето проверяется сейчас (игровой поток); сборка кадра и
	//   шифрование — в момент ухода под замком соединения (порядок шифрования = порядок на проводе). Итог — в Result
	//   (State: 0 ждёт, 1 ушёл, 2 ошибка, 3 снят; SentAtSec — момент ухода), игровой поток забирает опросом. Неустранимая
	//   ошибка сокета на фоновом потоке разрывает соединение в ближайшем `Tick` (колбэки — только на игровом потоке).
	//   Возврат — номер постановки (0 — не поставлено, ничего не уйдёт).
	struct FTimedResult
	{
		std::atomic<int32> State{ 0 };
		std::atomic<double> SentAtSec{ 0.0 };
	};
	uint64 SendPacketAt(uint16 Opcode, const uint8* Body, int32 BodyLen, bool bPad, double DeadlineSec,
		TSharedPtr<FTimedResult, ESPMode::ThreadSafe> Result, bool bStampSecret = false);
	// Снять невыполненную постановку (ушедшую — дождаться конца). Зовёт владелец; `Disconnect` снимает все сам.
	void CancelTimed(uint64 Ticket);

	// Вызывать каждый кадр: дочитать сокет, распарсить готовые пакеты, дёрнуть OnPacket.
	void Tick();

	void SetOnPacket(FOnPacket InCb) { OnPacket = MoveTemp(InCb); }
	void SetOnClosed(FOnClosed InCb) { OnClosed = MoveTemp(InCb); }
	// ★★ МЕЛК-2: поставить вето отправки (см. `FSendVeto`). Зовёт владелец один раз при создании соединения.
	void SetSendVeto(FSendVeto InVeto) { SendVeto = MoveTemp(InVeto); }

private:
	FSocket* Socket = nullptr;
	bool bConnected = false;      // флаг (вместо deprecated FSocket::GetConnectionState)
	TArray<uint8> RecvBuffer;     // накопитель частичных кадров
	// Поколение буфера: любое пересоздание/сброс (Connect/Disconnect) инкрементит счётчик. DrainFrames
	//   сверяет его после КАЖДОГО колбэка — если изменилось, старые offset/avail недействительны.
	uint32 RecvEpoch = 0;
	bool   bStopDrain = false;    // колбэк попросил прекратить разбор (смена соединения отложена)
	FOnPacket OnPacket;
	FOnClosed OnClosed;
	FSendVeto SendVeto;           // ★★ МЕЛК-2: вето отправки владельца; пусто — без вето

	void DrainFrames();           // выбрать все полные кадры из RecvBuffer
	// ★ EAC Э1: режим сырого потока (см. SetRawStreamMode) и его колбэк.
	bool       bRawStream = false;
	FOnRawData OnRawData;
	FOnSecureReady OnSecureReady;         // ★ EAC ред. 3: колбэк «рукопожатие завершено» (см. FOnSecureReady)
	// ── ★ EAC ред. 3 (E1-08): состояние неблокирующего подключения (ConnectAsync) ──
	bool    bConnectPending = false;      // подключение начато и ещё не завершено (ни успеха, ни отказа)
	bool    bClosedByConnectFail = false; // ★ 26.10.2026: OnClosed вызван `FailPendingConnect` (см. ConsumeClosedByConnectFail)
	bool    bResolvePending = false;      // фаза 1: идёт фоновый резолв имени (сокета ещё нет)
	double  PendingDeadlineSec = 0.0;     // абсолютный срок (FPlatformTime) на резолв + connect
	double  PendingStartSec = 0.0;        // момент начала (для строки лога)
	FString PendingHost;                  // хост подключения (для лога и резолва)
	int32   PendingPort = 0;              // порт подключения
	int32   PendingRole = -1;             // роль рукопожатия AsdaSecure (как у Connect)
	TSharedPtr<FAsdaResolveState, ESPMode::ThreadSafe> PendingResolve;   // общий блок фонового резолва (фаза 1)
	TArray<TArray<uint8>> PendingRawOut;  // SendRaw до установления — уходят сразу после ClientHello
	// ★ EAC ред. 3: шаг неблокирующего подключения из Tick. false — подключение ещё идёт или отвергнуто (выйти из Tick).
	bool TickPendingConnect();
	// ★ EAC ред. 3: создать неблокирующий сокет и начать connect к адресу (фаза 2). false — отказ сразу (Why — причина).
	bool StartPendingSocket(const TSharedPtr<FInternetAddr>& Addr, FString& OutWhy);
	// ★ EAC ред. 3: отказ неблокирующего подключения — строка лога, Disconnect и колбэк OnClosed (как обрыв).
	void FailPendingConnect(const FString& Why);
	// ★ EAC ред. 3: подключение установлено — состояние «соединено», рукопожатие AsdaSecure, выдача накопленного SendRaw.
	bool FinishPendingConnect();
	// ★ EAC Э1: разобрать принятое — DrainFrames() для игровых соединений (как было), выдача сырого потока — для канала EAC.
	void ConsumeInbound();
	// ★ EAC Э1: отдать накопленный RecvBuffer колбэку сырого потока целиком и очистить буфер.
	void DeliverRaw();
	// ★ 15.10.2026: ЗАМОК ВВОДА-ВЫВОДА — сокет и шифрование трогают игровой поток и поток точной отправки. Держат его
	//   отправка кадра, расшифровка приёма и выталкивание очереди шифрования, сброс соединения. Под замком НЕ зовутся колбэки
	//   (`MarkClosed`/`OnPacket`) — иначе владелец, уничтожающий соединение из колбэка, ждал бы фоновый поток (взаимоблокировка).
	FCriticalSection IoLock;
	// Жив ли объект для фонового потока: действие берёт `Lock`, проверяет `Conn` и только потом — `IoLock`.
	struct FAlive
	{
		FCriticalSection Lock;
		FAsdaConnection* Conn = nullptr;
	};
	TSharedPtr<FAlive, ESPMode::ThreadSafe> Alive;
	// Постановки этого соединения с их итогом (снимаются в `Disconnect`, итог — «снят»). Ревизия сети НИЗК-4/НИЗК-6:
	//   обрезаются только ЗАВЕРШЁННЫЕ (итог известен), ждущие не теряются.
	struct FTimedTicket
	{
		uint64 Ticket = 0;
		TSharedPtr<FTimedResult, ESPMode::ThreadSafe> Result;
	};
	TArray<FTimedTicket> TimedTickets;
	std::atomic<bool> bWorkerFatal{ false };   // фоновая отправка упала — разрыв в ближайшем `Tick`
	// ★ Ревизия сети СРЕДН-1: сокет признан мёртвым фоновой отправкой (под `IoLock`) — больше ни одного кадра в него, даже
	//   до `Tick`: иначе новый кадр дописался бы за частично записанным шифротекстом. Сбрасывается в `Disconnect`.
	bool bSocketDead = false;
	// ★ Ревизия сети НИЗК-4: номер соединения (растёт в `Disconnect` под замком). Постановка помнит свой номер и в чужое
	//   (новое) соединение не уходит.
	uint32 ConnEpoch = 0;
	FSecretFiller SecretFiller;           // наполнитель анти-реплей-секрета (ставит владелец)
	FTestWire TestWire;                   // только самотест: «провод без сокета»
	bool bTestWire = false;
	// Сокет (или провод самотеста) есть и соединение установлено.
	bool HasWire() const { return (Socket != nullptr || bTestWire) && bConnected; }
	// Собрать кадр, зашифровать (если канал защищён) и записать в сокет. Вызывать ПОД `IoLock`. bStampSecret — вписать
	//   анти-реплей-номер в тело [0..7] здесь (в момент ухода). false + OutFatalWhy — неустранимая ошибка (вызывающий рвёт
	//   соединение ВНЕ замка); false без причины — просто не ушло.
	bool SendFrameCore(uint16 Opcode, const uint8* Body, int32 BodyLen, uint16 HdrA, uint16 HdrB, bool bPad, const TCHAR*& OutFatalWhy,
		bool bStampSecret = false);
	bool SendPacketImpl(uint16 Opcode, const uint8* Body, int32 BodyLen, uint16 HdrA, uint16 HdrB, bool bPad, bool bStampSecret);
	// Пометить соединение мёртвым (сокет НЕ уничтожаем — это делает владелец через Disconnect()).
	void MarkClosed(const TCHAR* Why);

	// ── ★ В7b: шифрование трафика (внешний конверт поверх старого кадра) ──────────────────────
	//   Включается при asda.Net.Secure=1 (по умолчанию 0 — старый протокол без изменений). Конверт
	//   шифрует УЖЕ собранный кадр (frame+XOR), старый протокол внутри не меняется.
	TUniquePtr<FAsdaSecureClient> Secure;   // nullptr = незашифрованный путь (как раньше)
	bool          bSecureActive = false;    // рукопожатие начато на этом соединении
	TArray<uint8> SecureRaw;                // сырой приём с сокета до расшифровки (временный)
	double        SecureStartSec = 0.0;     // ★ В7d: момент отправки ClientHello (срок ожидания ServerHello)
	bool          bSecureReadyLogged = false; // ★ В7d: строка «рукопожатие завершено» уже в логе
	FString       SecurePeer;               // ★ В7d: «хост:порт (под-сервер)» — для понятных строк лога
	// Начать рукопожатие после установления соединения (отправить ClientHello). Role: 1 логин, 0 игра, −1 по порту.
	bool SecureBeginHandshake(int32 Port, int32 Role);
	// Расшифровать принятые байты Raw → добавить старый поток в RecvBuffer. false = ошибка целостности.
	bool SecureIngest(const uint8* Data, int32 Len);
	// ★ В7d: отправить уже готовые к проводу байты (ClientHello / вытолкнутая очередь) с досылкой остатка.
	bool SecureSendRaw(const uint8* Data, int32 Len);
	// ★ В7d: после приёма — если рукопожатие завершилось, вытолкнуть очередь; если ServerHello нет дольше
	//   asda.Net.SecureHandshakeTimeoutSec — понятная причина в лог и штатный разрыв (MarkClosed → OnClosed).
	//   false = соединение помечено разорванным (вызывающий выходит из Tick).
	bool SecureAfterIngest();
};
