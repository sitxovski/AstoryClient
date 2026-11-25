// Copyright (c) 2026 @sitxovski (Discord)
// UAsdaNetSubsystem — владелец соединения с сервером (Слой 1).
// Поднимает FAsdaConnection, тикает его, шлёт login, диспетчит входящие опкоды.
// Поток: ConnectToLogin → SendLogin(0x3EB) → recv 0x3EC → char-list → select → enter-world → GameServer.
#pragma once
#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "AsdaConnection.h"   // полный тип (TUniquePtr + UHT vtable-helper требуют complete)
#include "Net/PacketFrame.h"
#include "AsdaNetQueue.h"     // ★ A2 (аудит №4): TAsdaNetQueue<T> — общая точка очередей «колбэк копит → Tick применяет»
#include "AsdaReplyLatch.h"   // FAsdaReplyLatch — засов «запрос в полёте» (GmHideLatch)
#include "Net/AsdaNetCharTypes.h"     // ★ R1: типы входа/выбора персонажа (вынесены отсюда)
#include "Net/AsdaNetEffectTypes.h"   // ★ R1: типы потока эффектов и «часа удачи»
#include "Net/AsdaNetWorldTypes.h"    // ★ R1: разобранные пакеты мира и боя (в т. ч. бывшие вложенные структуры)
#include "AsdaSessionReset.h"   // IAsdaSessionResettable — сброс состояния сессии
#include "Login/AsdaServerAddress.h"   // ★ 16.10.2026: домен сервера и правило подмены адреса из пакетов
#include "AsdaNetSubsystem.generated.h"

class AActor;   // SendStop4008AtActor — позиция берётся у актора (полный тип нужен только в .cpp)

// ★ R1 рефакторинга (10.11.2026): типы пакетов, не являющиеся частью класса, вынесены дословно в заголовки по областям
//   (включены выше; этот файл остаётся общим входом):
//     `Net/AsdaNetCharTypes.h`   — EAsdaNetState, FAsdaEquipItem, FAsdaCharRecord, FAsdaSoulmateSharedChar, FOnAsdaLoginResult;
//     `Net/AsdaNetEffectTypes.h` — EAsdaEffectMsgKind, FAsdaEffectMsg, FAsdaEventBonusPkt;
//     `Net/AsdaNetWorldTypes.h`  — FAsdaRemotePlayer, FAsdaCombatHit, FAsdaCombatHitAux, FAsdaCombatSideHit, FAsdaCombatSideDamage,
//                                  FAsdaVitals, FAsdaPvpAttackPkt, FAsdaSkillHitRec, FAsdaLootDrop, FAsdaSkillCastRec.

UCLASS()
class ASDAREBORN_API UAsdaNetSubsystem : public UGameInstanceSubsystem, public FTickableGameObject,
	public IAsdaSessionResettable
{
	GENERATED_BODY()
public:
	UAsdaNetSubsystem();
	virtual ~UAsdaNetSubsystem() override;   // в .cpp (=default), где FAsdaConnection полный — для TUniquePtr

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// ── IAsdaSessionResettable (К-3) ──
	// ★ Здесь лежат ЗЕРКАЛА полей `CCharacter`, которые сервер присылает ОДИН раз за вход
	//   (блок 1017: пол, знак зодиака, причёска, ёмкость склада; далее 4005/4065: уровень,
	//   класс, опыт, клан, «о себе»). Портальный вход 5058 их НЕ переприсылает, поэтому при
	//   смене карты их трогать нельзя, а при выходе/разрыве — обязательно нужно: иначе гейты
	//   («знак уже выбран», «я глава клана») отработают по данным ПРЕДЫДУЩЕГО персонажа.
	// ⚠ Рассылается ПОСЛЕДНЕЙ (см. UAsdaSessionResetSubsystem::Broadcast): остальные подсистемы
	//   во время своего сброса ещё читают эти поля.
	virtual void ResetSessionState(EAsdaSessionResetReason Reason) override;

	// FTickableGameObject
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UAsdaNetSubsystem, STATGROUP_Tickables); }
	virtual bool IsTickable() const override { return !IsTemplate() && Conn.IsValid(); }

	// Слой-1 API
	UFUNCTION(BlueprintCallable, Category="AsdaNet")
	bool ConnectToLogin(const FString& Host, int32 Port);

	// Живо ли реальное сокет-соединение (для решения «переподключаться ли» при повторном логине).
	bool IsConnected() const;

	// Закрыть текущее соединение (для смены адреса сервера: пользователь ввёл новый IP → реконнект).
	UFUNCTION(BlueprintCallable, Category="AsdaNet")
	void Disconnect();

	UFUNCTION(BlueprintCallable, Category="AsdaNet")
	bool SendLogin(const FString& Login, const FString& Password);

	// Запрос списка персонажей (Send_CharacterPacket@0xF17E60, опкод 1005): тело 30б =
	// secret[8]+digest[16]+pkt6 (UserIndex u32 + serverIndex u16). Патч-сервер игнорит secret/digest.
	bool SendCharListRequest(uint16 ServerIndex = 0);

	// Запрос/выбор канала (netChannel_SendSelect1012@0xF1FB80, опкод 1012): тело 29б =
	// secret[8]+digest[16]+5б (u32 targetId=UserIndex + u8 channel). Ответ 1013 = счётчики онлайна каналов.
	bool SendChannelSelect(uint8 ServerGroupIndex = 0);   // 1012: payload = UserIndex(u32) + ServerGroupIndex(u8)

	// Список каналов из ответа 1013 (u16[30] онлайн-счётчики). Индекс = номер канала.
	UPROPERTY(BlueprintReadOnly, Category="AsdaNet")
	TArray<int32> ChannelOnline;

	DECLARE_MULTICAST_DELEGATE(FOnAsdaChannelList);
	FOnAsdaChannelList OnChannelListReady;

	// ── ★ КАНАЛЫ В МИРЕ (эталон g_nCurrentChannel@0x340F324, g_nChannelCount@0x340F6DC) ──
	//   Обе величины ставит этап логина (экран выбора сервера/канала) — единственный источник в оригинале:
	//   счётчики 1013 приходят на 1012, число слотов = m_nChannelCount клиента. В мире их читает
	//   MiniMap_BuildUI (панель 17501: строк = ChannelCount, текущий канал — своим цветом) и
	//   ChannelSelectWnd_UpdateChannelLabels (подпись 1717 «<сервер> - <ch> канал», нотис 15200).
	UPROPERTY(BlueprintReadOnly, Category="AsdaNet")
	int32 CurrentChannel = 0;           // 0-based (в UI показывается +1)

	UPROPERTY(BlueprintReadOnly, Category="AsdaNet")
	int32 ChannelCount = 0;             // сколько строк каналов показывать (0 = данных ещё нет)

	UPROPERTY(BlueprintReadOnly, Category="AsdaNet")
	int32 SelectedServerIndex = 0;      // выбранный сервер (эталон dword_340F328): имя = loc 11000+index

	// C→S 6084 OP_RECV_USER_CHANNEL_MOVE — ПЕРЕХОД НА КАНАЛ (эталон netSec_SendChallenge6084@0xF46A90).
	//   Тело 25б = [0..7] анти-реплей-секрет (тот же scrambled-QPC) + [8..23] MD5(payload,1) + [24] номер
	//   канала (0-based). В оригинале шлётся из обработчика клика по строке Change_Channel<NN> (17503+i).
	bool SendChannelMove6084(uint8 Channel);

	// S→C 6015 OP_NET_HANDLE_CHANNEL_TRANSFER — сервер разрешил переход (эталон netHandleChannelTransfer@0xF49660).
	//   Тело: [0..15] строка IP канал-сервера, [16..17] port u16, [18..19] spawnX, [20..21] spawnY,
	//   [22..23] mapId, [24] новый номер канала, [25] portal/channel-param (идёт в enter-хендшейк 20000 b24).
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnAsdaChannelChanged, int32 /*Channel*/);
	FOnAsdaChannelChanged OnChannelChanged;
	uint8 PortalChannelParam = 0xFF;    // эталон g_byPortalChannelParam (вход с char-select = -1/0xFF)
	// ★★★ ТОКЕНЫ КАМПВАРА из портального пакета 5055 (байты +51/+52).
	//   Это НЕ флаги для интерфейса, а непрозрачные серверные метки: клиент кладёт их к себе
	//   (`pkt[52]` → `CCampWar+0x2304`, `pkt[51]` → `+0x2308`), нигде не разбирает и возвращает
	//   ЭХОМ в enter-хендшейк 20000 — байты payload[25]/[26]. Без этого эха сервер на новой карте
	//   не узнаёт состояние кампвара игрока. Единственный бит, который клиент читает локально, —
	//   `pkt[51] & 0x80` (или карта 23): при нём боевой HUD кампвара не поднимается.
	//   0xFF = «не участвует» (значение из init-заполнения хендшейка, подтверждено перехватом).
	uint8 CampWarToken1 = 0xFF;   // из pkt[51] → payload[25]
	uint8 CampWarToken2 = 0xFF;   // из pkt[52] → payload[26]
	// «Боевой HUD кампвара не показывать». ★ 27.09.2026: ставится ТОЛЬКО на переходе 5055 код 1 (бит 0x80 у pkt[51]
	//   или карта-цель 23, эталон `network.c:43118`) и при призыве 6013; отказы 5055 его не трогают. Читателя пока нет —
	//   окно кампвара в порту не сделано (реестр отложенного № 95).
	bool  bCampWarHudSuppressed = false;

	// Вход в мир выбранным персонажем (Send_SelectChar@0x40AD30, опкод 1016): тело 33б =
	// secret[8]+digest[16]+9б payload (эталон charDealMsg entity_char.c:51795):
	//   b0 connType(0), b1-4 UserIndex, b5 СЫРОЙ slotId(rec[1076]), b6-7 канал(rec[1077]), b8 канал.
	// MD5[8..23]=MD5(payload,9). Ответ 1021 (686б) → адрес game-сервера + снапшот перса.
	//
	// ★★★ Mode — РЕЖИМ ВХОДА, байт payload[0] (эталон `LOBYTE(charDealMsg[0]) = режим слота`):
	//   0 — обычный вход СВОИМ персонажем (значение по умолчанию, поведение не менялось);
	//   1 — «душа»: в оригинале ветка есть в протоколе и в БД (`UPCharSoulState @State=1`), но клиент
	//       по ней НИЧЕГО не шлёт (только звук отказа) — поэтому и мы её не шлём, см. реализацию;
	//   2 — ОДЕРЖИМОСТЬ: вход персонажем соулмейта. При Mode==2 `RawSlotId` — это Charidx ЧУЖОГО
	//       персонажа (10/11/12) из `GetSoulmateSharedChars()`, а в payload[1..4] уходит
	//       `OwnerUserIndex` этой записи, а НЕ наш `UserIndex`. Сервер в этом режиме НАМЕРЕННО
	//       пропускает сверку «аккаунт пакета == аккаунт сессии» (`LoginManager.c:4151-4166`),
	//       перепривязывает сессию на аккаунт соулмейта, ставит `CharPos.SoulState = 2` и
	//       ВЫКИДЫВАЕТ владельца из игры. Отказы приходят опкодом 1027: код 2 = соулмейт сейчас
	//       онлайн (loc 131), код 3 = у аккаунта нет активной пары. См. `OnCharSelectRefused`.
	bool SendSelectChar(uint8 RawSlotId, uint8 Group = 0, uint8 Channel = 0, uint8 Mode = 0);

	// ══════════ ★★★ «ОДЕРЖИМОСТЬ»: персонажи соулмейта на экране выбора (1024 → 1023 → 1025) ══════
	//
	// Массив ВСЕГДА из 3 элементов, индекс = Charidx%10 (тот же принцип, что у `CharList`):
	//   оригинал держит их отдельным массивом `CCharacter[3]` по `CCharacterContainer+1655664`,
	//   НЕ в слотах собственных персонажей (`+236592`) — своё и чужое не перетирается.
	//   Пустые элементы — `IsEmpty()`. Заполняется только когда у соулмейта есть галка «поделиться»:
	//   гейт `Send_GetCharInfo` — `Smstate != 1 && MateUserindex > 0 && ShareCount > 0`.
	UPROPERTY(BlueprintReadOnly, Category="AsdaNet")
	TArray<FAsdaSoulmateSharedChar> SoulmateSharedChars;

	// Геттер для потребителей сцены выбора персонажа (контракт с UI-слоем).
	const TArray<FAsdaSoulmateSharedChar>& GetSoulmateSharedChars() const { return SoulmateSharedChars; }

	// Событие: набор персонажей соулмейта готов. Взводится ПОСЛЕ 1025 (конец транзакции 1005),
	//   когда пришли и экип (1024), и список (1023) — раньше данные неполные.
	DECLARE_MULTICAST_DELEGATE(FOnAsdaSoulmateSharedChars);
	FOnAsdaSoulmateSharedChars OnSoulmateSharedCharsUpdated;

	// ── Состояние ветки соулмейта из 1025 (5 б). Сцена выбора персонажа читает это, а не гадает. ──
	//   `[0]` код: 0 = ошибка на сервере (оригинал переспрашивает 1028), 1 = соулмейта нет либо он
	//   ничего не шарит, 2 = `Smstate == 1`, 3 = ветка отработала и соулмейт ОНЛАЙН,
	//   4 = отработала и соулмейт офлайн, 5 = просто показать панель выбора персонажа.
	UPROPERTY(BlueprintReadOnly, Category="AsdaNet")
	int32 SoulmateBranchCode = -1;      // «сырой» [0]; -1 = 1025 в этой сессии ещё не приходил

	// `[1]` подкод → эталонный флаг `+3075045`: 1 и 3 → true, 0 и 2 → false. Гасит ВТОРОЙ абзац
	//   поясняющей панели соулмейта (контрол 116 `SOULINFO_TXT01`).
	UPROPERTY(BlueprintReadOnly, Category="AsdaNet")
	bool bSoulmateInfoSecondParagraph = false;

	// Соулмейт сейчас в игре (эталонный флаг `+3075039`: код 3 → true, код 4 → false).
	//   Пока он онлайн, вход одержимым сервер отклоняет (1027 код 2).
	UPROPERTY(BlueprintReadOnly, Category="AsdaNet")
	bool bSoulmateOnline = false;

	// `[2..4]` — соулмейт в игре / его канал / его CPoridx. ★ Оригинальный клиент эти три байта
	//   НЕ ЧИТАЕТ вовсе; храним их, потому что закон запрещает резать охват разбора.
	UPROPERTY(BlueprintReadOnly, Category="AsdaNet") int32 SoulmateInGameByte = 0;   // [2]
	UPROPERTY(BlueprintReadOnly, Category="AsdaNet") int32 SoulmateChannel = 0;      // [3]
	UPROPERTY(BlueprintReadOnly, Category="AsdaNet") int32 SoulmatePortalId = 0;     // [4]

	// Отказ на выбор персонажа (опкод 1027, тело 651 б, байт [0] ≠ 1):
	//   0 = ошибка `UPCharSoulState` на сервере (оригинал просто разблокирует кнопку),
	//   2 = вход одержимым отклонён, СОУЛМЕЙТ ОНЛАЙН → нотис loc 131,
	//   3 = вход одержимым отклонён, у аккаунта нет активной пары (оригинал молчит — дефект UX).
	//   Код 1 сюда НЕ приходит: это успешный вход, он разбирается как экип локального игрока.
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnAsdaCharSelectRefused, int32 /*Code*/);
	FOnAsdaCharSelectRefused OnCharSelectRefused;

	// Создать персонажа (эталон uiCharCreateSubmitRequest@0xF10280 → OP_UI_CHAR_CREATE_SUBMIT_REQUEST 1014).
	//   Тело 76б = 24 заголовок + 52 payload: clientId@+0(u32), channel@+4(u16), slot@+6=СЫРОЙ slotId 10/11/12
	//   (позиция+10, не индекс — эталон pDlg+473524), name@+7(≤20 cp1251), gender@+27,
	//   hair@+28, color@+29, face@+30, constellation@+31, 5×i32 part-коды@+32 (порядок slotOrder задаёт caller).
	bool SendCreateChar(uint8 Slot, const FString& Name, uint8 Gender, uint8 Hair, uint8 Color,
		uint8 Face, uint8 Constellation, const TArray<int32>& PartCodes, uint16 Channel);

	// Удалить персонажа (эталон netSendReq1018@0xF09B20, опкод 1018). Тело 31б = 24 заголовок + 7 payload:
	//   charId@+0 (u32) = UserIndex/account (rec off1052=sub_929928), slotNum@+4 (u8) = СЫРОЙ slotId
	//   (rec[1076], 10/11/12), word@+5 (u16) = канал (rec[1077]=sub_9303EA). MD5[8..23]=MD5(payload,7).
	bool SendDeleteChar(uint8 RawSlotId, uint16 ServerGroupIndex);   // 1018 @+5 = ServerGroupIndex (@Seridx)

	// Дефолт-внешность из 0x3F7 (1015 parsePacketNameEntryBlock): имя + триплет причёска/цвет/лицо. Для CARAMAKE-префила.
	struct FCharCreateDefaults { FString Name; uint8 Hair = 0; uint8 Color = 0; uint8 Face = 0; bool bValid = false; };
	FCharCreateDefaults CharCreateDefaults;

	// Результат создания (0x3FB/1019): 1=успех, иначе loc-ошибка. Broadcast для UI.
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnAsdaCharCreateResult, int32 /*Code*/);
	FOnAsdaCharCreateResult OnCharCreateResult;
	// Результат УДАЛЕНИЯ (op 1019, эталон charCreate_OnCreateResultPacket@0xF03E90 — по содержанию это
	//   delete-cleanup: code 1=успех (слот чистится локально), 2/3/4=ошибки loc 12434/12459/12820).
	//   Live-подтверждено: сервер отвечает 1019 именно на SendDeleteChar (1018), create отвечает 1026.
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnAsdaCharDeleteResult, int32 /*Code*/);
	FOnAsdaCharDeleteResult OnCharDeleteResult;

	// Событие: получен 1021 (enter-world) → коннект к game-серверу (P2: загрузка мира/спавн).
	DECLARE_MULTICAST_DELEGATE(FOnAsdaEnterWorld);
	FOnAsdaEnterWorld OnEnterWorld;

	// Снимок game-сервера из 1021 (адрес — из пакета; тело 686б несёт снапшот перса для P2).
	//   ★ 16.10.2026: дефолт — домен сервера (был захардкоженный адрес стойки 192.168.10.70); пакеты задают адрес через
	//   ApplyGameServerFromPacket (непубличный адрес из пакета → домен, порт — из пакета).
	FString GameServerIP = AsdaServerAddr::LoginHost();
	int32   GameServerPort = 15601;
	// ★★ 16.10.2026: ЕДИНАЯ ТОЧКА записи адреса игрового сервера из пакета (1021, 6013, 6015, 4055, 5055, свиток 5459).
	//   Ip/Port — как в пакете; Ip непустой и Port 1..65535, иначе ничего не меняет и возвращает false. Непубличный Ip
	//   (RFC1918/loopback/link-local/0.0.0.0, см. AsdaServerAddress.h) заменяется доменом сервера, порт сохраняется;
	//   подмена пишется в журнал с источником Source. true — GameServerIP/GameServerPort обновлены.
	bool ApplyGameServerFromPacket(const FString& Ip, int32 Port, const TCHAR* Source);

	// ═══ ★ РЕЖИМ XyT (сборка чужого сервера; разбор — `Reports/xyt_traffic_capture.md`) ═══
	//   Их клиент наружу не ходит: весь трафик идёт в ЛОКАЛЬНЫЙ прокси `Launcher.exe`, который
	//   слушает 127.0.0.1:17544 (логин) и 127.0.0.1:17505 (игра) и сам уносит пакеты наружу.
	//   Поэтому в этом режиме мы: (1) берём ключ шифра XyT, (2) коннектимся на их лаунчер, и
	//   (3) ИГНОРИРУЕМ адрес game-сервера из пакета 1021 — сервер называет свой внешний адрес,
	//   а идти надо снова в лаунчер, на локальный игровой порт.
	bool  bXytMode      = false;
	int32 XytLoginPort  = 17544;   // порт LOGIN-канала их лаунчера
	int32 XytGamePort   = 17505;   // порт GAME-канала их лаунчера

	// ★★★ ЖИВОЙ КЛЮЧ ПРИЁМА XyT. Их сервер время от времени шлёт лаунчеру служебный кадр
	//   `FC | (len+2)^0x37 | seed[len]`; лаунчер выводит новый ключ приёма по формуле
	//   recv[i] = (seed[T(i) mod seedLen] + 0x0B) ^ send[i] и вписывает его в код СВОЕГО клиента
	//   через WriteProcessMemory. Нас он не патчит — мы другой процесс, поэтому ключ читается
	//   снаружи: внешний агент отдаёт его через файл, а мы подхватываем по времени изменения.
	//   Ключ ОТПРАВКИ при этом постоянен всю сессию. Разбор — `Reports/xyt_traffic_capture.md`.
	void   TickXytRecvKey(float DeltaSeconds);
	// Немедленно перечитать файл ключа (вне периодического опроса). true — ключ реально изменился.
	bool   ReloadXytRecvKeyNow();
	// Гейт исходящих: их сервер не знает часть наших опкодов и кикает за них (kick 4101 code=15).
	bool   IsOpcodeBlockedXyt(uint16 Opcode) const;
	// ★ Спасение кадра, расшифрованного УСТАРЕВШИМ ключом. Лаунчер меняет ключ мгновенно, а к нам он
	//   попадает с задержкой (хук → файл → опрос), поэтому первый кадр после смены приходит «мусором»
	//   и второй раз сервер его не пришлёт. XOR симметричен, значит кадр можно доправить на месте:
	//   наложить (старый ключ XOR новый) на те же позиции. Признак сбоя точный: у корректных пакетов
	//   сервера XyT hdrA и hdrB всегда 0xFFFF.
	bool   TryRedecryptXytPacket(struct Asda::ParsedPacket& Pkt);
	double XytKeyPollAccum = 0.0;      // накопитель опроса файла
	int64  XytKeyFileStamp = 0;        // время изменения файла на момент последнего чтения
	int64  XytKeyFileSize  = 0;        // размер — вторая примета изменения
	static const TCHAR* XytHost() { return TEXT("127.0.0.1"); }
	TArray<uint8> EnterWorldSnapshot;   // 686б тела 1021 (снапшот; mapId=[26] u16, реверс netSendEnterWorldHandshake)

	// ★ ФАЗА A (world_entry_flow.md): данные для enter-хендшейка game-серверу (opcode 20000).
	TArray<uint8> CharSelectData;       // 68б блок из 1017 (Net_RecvCharSelect: body[4..71]); charId = dword[0]
	int32 SelectedCharId = 0;           // charId = CharSelectData[0] (dword тела 1017 offset 4)

	// ══════════════ ★★★ ПОЛ И ВНЕШНОСТЬ СВОЕГО ПЕРСОНАЖА — ЭТАЛОННЫЙ ИСТОЧНИК (1017) ══════════════
	// Эталон game_main.c:2801-2805 (создание локального игрока):
	//     qmemcpy((void *)(pNewLocalPlayer + 1052), &g_aCharSelectData, 0x44u);   // блок 68б из 1017
	//     *(_BYTE *)(pNewLocalPlayer + 1120) = dword_29062B0;                     // созвездие
	//     *(_WORD *)(pNewLocalPlayer + 1121) = *(_WORD *)((char *)&dword_29062B0 + 1);  // причёска+цвет
	//     *(_BYTE *)(pNewLocalPlayer + 1123) = HIBYTE(dword_29062B0);             // лицо
	// где g_aCharSelectData и dword_29062B0 наполняет Net_RecvCharSelect@0xF03910 (network.c:34246-34251):
	//     qmemcpy(&g_aCharSelectData, &recvBuf[1], 0x44u);   // = тело 1017 [4..71]
	//     dword_29062B0 = recvBuf[18];                       // = тело 1017 [72..75]
	// Поле пола = CCharacter+0x437 (structs_ru.h:47 `unsigned __int8 gender`) = char+1079 = блок+27,
	// т.е. ТЕЛО 1017 БАЙТ 31. Именно его читает весь выбор модели (`self->gender == 1` → ch1_m_*, иначе ch1_f_*).
	// ЖИВАЯ СВЕРКА (лог 2026-08-07 15:54:07, чар GM_sitxovski): тело 1017 [28]=0x0A slotId, [31]=0x01 пол,
	//   [32]=0x0C класс 12 (лог «класс=12»), [34]=0x0F уровень 15 — раскладка сходится байт в байт.
	// Раньше порт брал пол СВОЕГО персонажа только из записи char-list (0x3EE) по LastSelectedRawSlot и
	//   при непопадании молча подставлял «мужчина». Теперь эталонный источник — 1017.
	uint8 SelfGender = 0;               // 1017 тело[31] (блок+27): 1=муж, 2=жен; 0 = ещё неизвестно
	uint8 SelfConstellation = 0xFF;     // 1017 тело[72] → char+1120 (0xFF = неизвестно)
	uint8 SelfHair = 0xFF;              // 1017 тело[73] → char+1121
	uint8 SelfHairColor = 0xFF;         // 1017 тело[74] → char+1122
	uint8 SelfFace = 0xFF;              // 1017 тело[75] → char+1123
	// Пол своего персонажа из снапшота появления (4005 тело[37] / 4065 запись[54]) — независимый
	//   контрольный канал: у эталона это тот же байт CCharacter+0x437. 0 = не приходил.
	uint8 SelfGenderFromSpawn = 0;

	// Пол своего персонажа по приоритету источников эталона: 1017 → снапшот 4005/4065 → запись char-list.
	//   Всегда возвращает 1 или 2. OutDiag (если задан) получает построчную сводку всех источников —
	//   ею пользуется и лог спавна, и консольная команда asda.Gender.
	uint8 ResolveSelfGender(FString* OutDiag = nullptr) const;
	// Полная сводка «пол из каждого источника» (свой персонаж + все известные соседи) — для asda.Gender.
	FString DumpGenderSources() const;
	// ★ 32-байт логин-блок (эталон sub_922E5C(g_LoginCtxBuf)) — auth-креды, нужны для пакетов возврата в
	//   char-select/сброса состояния: 1028 (net_sendLoginCharEnterOpcode1028) и 1010 (OP_C_LOGIN_WIN_ON_BUT00).
	//   Захватывается при SendLogin из cred-блока (login-поле, cred+20, 32б). Формат уточняется декомпилом LoginManager.
	uint8 LoginCredBlob32[32] = {};
	// ★ 19.10.2026 (журналы релиза, 00_SPEC.md «Дополнение»): логин последнего отправленного входа (SendLogin) — ждёт ответа 1004;
	//   при статусе 1 уходит в AsdaLogSeal::SetSessionAccount (Sessions\<файл>.alog.meta). Пароль/MD5 здесь не хранятся.
	FString PendingSessionAccount;
	int32 EnterMapId = -1;              // mapId целевой карты = EnterWorldSnapshot[26] (u16)
	bool bPendingEnterWorld = false;   // 1021 получен → на след. тике сделать реконнект+хендшейк (не реентрить в колбэке)
	// ★ 2026-09-02 (К-4): «коннект к game-серверу начат, но ещё не подтверждён» — взводится
	//   `EnterGameWorld`, снимается в `Tick`, когда `FAsdaConnection::IsConnected()` станет true
	//   (тогда же уходит отложенный хендшейк 20000). При БЛОКИРУЮЩЕМ коннекте не взводится вовсе:
	//   там сокет соединён уже к моменту возврата из `Connect`, и A3 уходит в том же кадре.
	bool  bAwaitGameConnect   = false;
	float AwaitGameConnectSec = 0.f;   // сколько секунд ждём подтверждения (страховка от вечного ожидания)
	// Предел ожидания подтверждения game-коннекта, секунды. Взят равным таймауту SYN у ОС — ровно
	//   столько же висел прежний блокирующий `Connect`, прежде чем вернуть FAIL, так что «потолок»
	//   ожидания не меняется, меняется только то, что игровой поток при этом не стоит.
	static constexpr float kGameConnectWaitSec = 20.f;
	// A2+A3+A4: дисконнект login → коннект game-сервер → enter-хендшейк 20000 → (событие для загрузочного экрана).
	void EnterGameWorld();
	// ★★★ 25.10.2026 [ИСПРАВЛЕНИЕ ПОРТА] (Player52 25.10 16:53, Player17 21.10 21:02): подключение к game-серверу не состоялось
	//   (отказ ОС, срок kGameConnectWaitSec, отказ на старте). [ОРИГИНАЛ] переход (`netSendEnterGameHandshake`, network.c:3881):
	//   `Show_DisconnectPopup(1001)` + `g_nDisconnectReason = 4`; вход из выбора персонажа (game_main.c:3337) — ошибка 1001.
	//   Порт: разрыв через `HandleConnectionClosed` (сброс сессии, признаки перехода сняты); в мире — попап 8000 с нотисом 1001
	//   (ОК → экран входа), на сцене входа — окно кика логина с 1001. Прежде игрок оставался в мире без связи и без сообщения.
	//   Why — причина для журнала. Ключ отката `asda.Net.GameConnectFailDisconnect 0` — прежнее «State = CharSelect» без UI.
	void HandleGameConnectFailed(const TCHAR* Why);
	bool SendEnterWorldHandshake();

	// ── ★ ФАЗА B (WORLD_ENTRY_CHAIN.md): game-протокол после хендшейка ──
	bool bInGameConnected = false;      // эталон g_bInGameConnected: 6004(res==2) принят
	// 5056 CharPortalLogoutReq (мисней «GameAck»; эталон netSendAnticheatTsMask@0xF47710): 24б = секрет 8 + MD5(пусто) 16.
	//   ★ 18.10.2026: уходит СТАРОЙ карте перед закрытием её сокета (EnterGameWorld), по новому — только 6203.
	bool SendGameAck5056();

	// ★ C→S 6616 «я получил редкий предмет — объяви всем» (эталон netSendSignedPacket6616@0xA0F140).
	//   Тело 48 б: [0..7] анти-реплей-секрет, [8..23] MD5(msg,24), [24..47] msg = itemCode dword + имя char[20] cp1251.
	//   Зовётся ТОЛЬКО из UAsdaHudSubsystem::NotifyItemInfo (фильтр грейд>=3 — как в оригинале).
	//   Сервер ретранслирует пакетом 6131 всем игрокам (требуется патч 6616, см. AstoryServer/Docs).
	bool SendRareItemNotice6616(const FString& CharName, int32 ItemCode);

	// ═══════════════════ ОТПРАВКА ЧАТА (ЭТАП 2, реверс 2026-08-05) ═══════════════════════════
	// Все тела 1:1 с эталоном; шифрование 0, как у остальных чат-пакетов.
	//   5084 `OP_RECV_NORMAL_CHAT_CLIENT` — «крик»/Общий (`ChatInput_SendShout @0xBA4620`,
	//        chat.c:2373): тело = СЫРОЙ ТЕКСТ без заголовка и без завершающего нуля.
	//   5086 — обычный/Глобальный (`ChatInput_SendNormal @0xBA64A0`, chat.c:2998):
	//        заголовок 27 байт (WORD +0 из ctor sub_931204 — в срезе не раскрыт, шлём 0;
	//        BYTE 0 на +2; char[24] имя игрока на +3), далее текст. len = 27 + strlen.
	//   6560 — каналы 0/1/2 (`ChatInput_SendChannel @0xBA4A20`, chat.c:2466):
	//        BYTE channelType на +0, далее текст. len = 1 + strlen.
	//   5088 — шёпот (`ChatInput_ParseWhisper @0xBA4FF0` / `ChatInput_SendWhisperReply @0xBA4020`):
	//        BYTE тип на +0 (0 = обычный шёпот, 1 = соулмейт), char[20] имя на +1, далее текст.
	//        len = 21 + strlen. Имя длиннее 20 байт — отказ (эталон loc 6942).
	//   5105 — отряд (`Party_SendChat @0xF3A290`, social.c:9569): WORD индекс цели на +0, текст.
	//   4120 — клан/сообщество (`chatSendMessage @0xBCAA00`, chat.c:5640): заголовок 28 байт
	//        (WORD +0, DWORD userIdx +2, char[20] имя +8), далее текст. len = 28 + strlen.
	// ★ Текст кодируется в cp1251 (как весь остальной текст протокола), обрезается до 199 байт
	//   (эталон обрезает на 200: chat.c:2440/3090/2565).
	bool SendChatShout5084(const FString& Text);
	// ChannelType — байт +2 заголовка. 0 = Глобальный (как в эталоне), 1/2 — «Поиск отряда» /
	//   «Торговля»: сервер (`recvGlobalChatClient @0x547670`) читает канал именно отсюда и
	//   ретранслирует его в 5087 всем игрокам. См. развёрнутый разбор в .cpp.
	bool SendChatNormal5086(const FString& Text, const FString& SelfName, uint8 ChannelType = 0);
	bool SendChatChannel6560(uint8 ChannelType, const FString& Text);
	bool SendChatWhisper5088(uint8 Kind, const FString& TargetName, const FString& Text);
	bool SendChatParty5105(uint16 TargetIdx, const FString& Text);
	bool SendChatClan4120(int32 SelfCharId, const FString& SelfName, const FString& Text);   // @2 = charId (★ 27.09.2026: имя параметра было SelfUserIdx)
	// ★ 27.09.2026: префикс клан-чата «(Друг)» (loc 6937) при SoulState ≠ 0, иначе пусто — `chatSendMessage` chat.c:5671.
	//   Единая точка: ею пользуются отправка 4120 и лимит поля ввода режима 6 (`GetActiveInputMaxChars`).
	FString GetClanChatSoulPrefix() const;

	// ═══ ★★★ ЗВАНИЯ: две отправки окна 18700 ═══════════════════════════════════════════════════
	//   Конверт у ОБЕИХ — `SendSecured` (8 б секрет + 16 б MD5 + payload), `bPad = true`:
	//   эталонный `CNetwork_Send(..., 34/32, OP, 1, 1, 1, 0, 0)` передаёт a6 = 1.
	//   ⚠ userNum (i16) и charId (i32) — РАЗНЫЕ поля; перепутать = сервер молча проигнорирует пакет.
	/** C→S **6067** `OP_CHANGE_ACTIVE_FAME` (`netSendReq6067@0xE9B160`), payload 10 б:
	 *  [0..1] userNum i16 · [2..5] charId i32 · [6..7] titleId приставки i16 · [8..9] titleId
	 *  окончания i16. Пустой слот = −1. Ответ — 6068 (`OnCharTitle`). */
	// ★ GM-команда «пересчитать рейтинги СЕЙЧАС» (опкод 6020, серверный патч P25).
	//   What: 0 = ВСЁ (условия всех званий у тех, кто в игре + рейтинговые 65…69 +
	//   место в топе по очкам славы), 1 = только рейтинговые 65…69, 2 = только топ
	//   славы, 3 = только условия всех званий.
	//   ⚠ Честно: 25 из 280 званий пересчитать НЕЛЬЗЯ — их прогресс приходит в самом
	//   событии и нигде не хранится; персонажи не в игре не охвачены вовсе.
	//   Ответа отдельным опкодом НЕТ: сервер отвечает строками
	//   стокового 6561, поэтому результат приходит в чат сам собой.
	//   ⚠ Сервер молча откажет, если UserIndex учётки не в 0…5000 (ADM_*) или
	//   в dbo.CharGmInfo нет GMFlag=1 — это стоковый операторский гейт.
	bool SendGmRatingRecalc6020(uint8 What = 0);

	// ═══ ★★★ [ОРИГИНАЛ] GM-«ЗАБЛОКИРОВАТЬ УЧЁТКУ ЦЕЛИ» — опкод 6094 (сервер `sub_55A510@0x55A510`, GameServer.c:133261) ═══
	//   [ПРОТОКОЛ] Отправитель эталона — `net_sendCharFieldSyncOpcode6094@0xA13680` (network.c:8175; ЕДИНСТВЕННЫЙ вызов в рознице —
	//   самодонос `(6, 1)` → 6095, game_main.c:1928; ветка `targetSelf == 0` → 6094 в рознице НЕ зовётся). Тело payload 8 б:
	//       [0..1] u16 свой userNum (`g_LocalPlayer+190188`)  — сервер НЕ читает (`v8[0]` не используется);
	//       [2..3] u16 userNum ЦЕЛИ (`CharManager_FindByUserIdx(g_nTargetUserIdx)->effectSlots[111].field_5B8`) — в DB-запрос;
	//       [4..7] i32 «время» — DBManager `sub_432950` → `EXEC UPHackBlock <UserIndex цели>, <время>`;
	//   конверт СЕКРЕТ(8)+MD5(16)+payload(8) = 32 б, `CNetwork_Send(…, 32, 0x17CE, 1,1,1,0,0)` = наш `SendSecured`.
	//   ★ Единицы «времени» — ЧАСЫ (−1 = 30 лет): так его шлёт серверный автоблок P48 (`kick-autoblock-p48.md`: `BlockHours=24`)
	//   Сервер: слот отправителя `< 0x226` → гейт оператора `sub_52ECF0` (UserIndex 0…5000; под P47 ещё GM-флаг 2801) → токен
	//   `sub_524390` → MD5 `sub_523F00(…, 8, …)` → строка `Log_SpeedHack` «GM Recv_CharBlock() UserNick <ник GM>, Time N» →
	//   DB 2271 {u16 userNum, i32 UserIndex цели (−1, если слота нет!), u8 слот, u8 группа, i32 время}.
	//   ★★★ ОТВЕТА КЛИЕНТУ НЕТ НИ ПРИ УСПЕХЕ, НИ ПРИ ОТКАЗЕ (ни 6006/6037, ни кика цели): цель доигрывает до выхода,
	//   блок (`us_block = 5` + `BlockList.CloseDate`) срабатывает при следующем входе (4101 код 7) и снимается сам
	//   `UP_AC_LiftExpiredBlock` (P51). Поэтому цель обязана быть ОНЛАЙН в этом GameServer — иначе DBManager получит
	//   UserIndex −1 и запишет «UPHackBlock Error».
	/** C→S **6094**. `TargetUserNum` — сессионный userNum цели (i16, НЕ charId — закон charid-vs-usernum), `Hours` ≥ 1.
	 *  Отказы без отправки: свой номер неизвестен, цель = я сам, номер вне 0…549 (`sub_5328F0`), часы ≤ 0.
	 *  Возвращает факт ОТПРАВКИ — подтверждения сервер не присылает. */
	bool SendGmBlock6094(int32 TargetUserNum, int32 Hours);

	// ═══ ★★★ [ОРИГИНАЛ] GM-«СКРЫТЬСЯ» — опкод 6005 (`Send_HideCommand@0x54E970`) ═══════════════
	//   Полный поток разобран в `Reports/gm_commands_hide_2026-09-09.md`.
	//   Клиентский отправитель оригинала — `sub_9F7AE0(char bHide)` (в декомпил не попал, снят
	//   дизассемблированием `client\system\AsdaStory.exe`): конверт СЕКРЕТ(8) + MD5(16) + payload(2),
	//   `CNetwork_Send(field1C, body, 26, 0x1775, 1, 1, 1, 0, 0)` — то есть ровно наш `SendSecured`.
	//   Payload: [0] = 0 (сервер перетирает его результатом), [1] = 1 скрыться / 0 показаться.
	//   ★ ЭТО ПЕРЕКЛЮЧАТЕЛЬ С ЯВНЫМ АРГУМЕНТОМ, а не «тумблер вслепую»: желаемое состояние едет
	//   в пакете, сервер его не инвертирует. Текущее состояние клиент узнаёт ТОЛЬКО из эха 6006.
	/** C→S **6005**. `bHide` = требуемое состояние. Возвращает факт ОТПРАВКИ, не согласие сервера. */
	bool SendGmHide6005(bool bHide);

	// Подтверждённое сервером состояние GM-невидимости (эхо 6006). false, пока сервер не ответил.
	UFUNCTION(BlueprintPure, Category = "Asda|GM")
	bool IsGmHidden() const { return bGmHidden; }
	// ★ 18.10.2026: взведено ли восстановление «виден» после входа (см. `bGmRestoreVisiblePending`) и его снятие (диагностика).
	bool IsGmRestoreVisiblePending() const { return bGmRestoreVisiblePending; }
	void CancelGmRestoreVisible() { bGmRestoreVisiblePending = false; }
	// Состояние «скрыт» получено расчётом при входе/портале, а не эхом 6006 (для подписи меню).
	UFUNCTION(BlueprintPure, Category = "Asda|GM")
	bool IsGmHiddenFromEntry() const { return bGmHidden && bGmHiddenFromEntry; }
	// Есть неотвеченный запрос 6005 (ждём 6006). Нужен GM-меню, чтобы не слать два запроса подряд.
	UFUNCTION(BlueprintPure, Category = "Asda|GM")
	bool IsGmHidePending() const { return GmHideLatch.IsArmed(); }
	// Секунд прошло с отправки неотвеченного 6005 (0, если запроса нет). ★ Закон no-state-latch-before-shown:
	//   молчание сервера НЕ должно оставлять кнопку в вечном «ждём» — по таймауту заявка снимается.
	double GmHidePendingAge() const { return GmHideLatch.ElapsedSec(); }
	// Снять «ждём ответа» (зовёт GM-меню по истечении таймаута). Ничего не отправляет.
	void ClearGmHidePending() { GmHideLatch.Disarm(); }
	// Результат 6005: (bAccepted — сервер принял команду, bHidden — состояние после неё).
	//   ★ Отказ приходит ТЕМ ЖЕ опкодом 6006 с байтом [0] == 0 — отдельного «нотиса отказа» в
	//   оригинале нет ни одного (проверено: обработчик `localPlayerInputToggleByFlags@0xF49BD0`
	//   не зовёт ни UI_FormatLocalizedNotice, ни CChat_AddTokenizedLines).
	DECLARE_MULTICAST_DELEGATE_TwoParams(FAsdaOnGmHideResult, bool /*bAccepted*/, bool /*bHidden*/);
	FAsdaOnGmHideResult OnGmHideResult;

	// ═══ ★★★ [ОРИГИНАЛ] СЕРВЕР САМ ПРЯЧЕТ ОПЕРАТОРА ПРИ ВХОДЕ И ПРИ ПОРТАЛЕ ══════════════════════
	//   Доказано ДИЗАСМОМ `GameServer.exe` (в декомпил Hex-Rays попали не все ссылки — классическое
	//   ложное «нет», закон `false-negatives-method`). Стойкое поле «скрыт» — `CUser + 0x3BC3B`
	//   (300 = скрыт / 301 = виден), парное поле состояния действия — `CUser + 0x54E4`.
	//   Все 17 ссылок на `+0x3BC3B` в бинаре перечислены:
	//     ПИШУТ: `CharMake` (вход в мир) и `CharPortalMake` (портал/смена карты) — БЕЗУСЛОВНО 300,
	//            если `sub_52ECF0(userNum, -1)` (то есть 0 ≤ UserIndex учётки ≤ 5000);
	//            `Send_HideCommand` — 300/301 по нашему 6005; два места обнуления при заведении слота.
	//     ЧИТАЮТ: `Send_AgainUserInfo` и `Send_MoveSectorInClippingUserInfo` (оттуда уходит 4103
	//            СОСЕДЯМ — сам входящий из рассылки исключён сравнением charId), плюс гейты
	//            движения/атаки/навыков.
	//   ★★★ СОБСТВЕННОМУ КЛИЕНТУ ФЛАГ НЕ ПРИХОДИТ НИ ОДНИМ ПАКЕТОМ: единственный self-пакет входа
	//   4003 (9 б) отправляется в `CharMake` СТРОКОЙ ВЫШЕ записи 300 и несёт ещё старое состояние,
	//   а 6006 при входе не шлётся вовсе. Оригиналу это безразлично — у него своего признака
	//   «я скрыт» нет в принципе.
	//   [ПОРТ] Поэтому состояние входа НЕ угадывается, а ВЫЧИСЛЯЕТСЯ по той же формуле, что у
	//   сервера, из значения, которое реально приезжает по проводу: `UserIndex` учётки — поле
	//   ответа логина 1004 (body+2, u32; см. `UserIndex` ниже). Правило воспроизводится 1:1 в
	//   обработчике 4003, который шлют ОБА серверных пути — и `CharMake`, и `CharPortalMake`.
	//   ⚠ Практическое следствие, которого нет ни в одном нотисе: ПОРТАЛ СНОВА ПРЯЧЕТ оператора,
	//   даже если он только что показался командой 6005.
	//   Ключ отката: `asda.Gm.HideAutoOnEnter 0` (состояние ведётся только по эху 6006).

	/** Учётка проходит операторский гейт сервера `sub_52ECF0` (0 ≤ UserIndex ≤ 5000)? */
	UFUNCTION(BlueprintPure, Category = "Asda|GM")
	bool IsSelfOperatorAccount() const;

	// ═══ ★ [ПОРТ] ВТОРОЙ КРИТЕРИЙ ОПЕРАТОРА — ПРЕФИКС В ИМЕНИ ПЕРСОНАЖА ══════════════════════
	//   ★ ЭТО НАША НАДСТРОЙКА. [ОРИГИНАЛ] сервер имени НЕ смотрит вовсе: ни `CharMake`, ни
	//   `CharPortalMake`, ни `Send_HideCommand` не читают `Charname` — единственный гейт там
	//   `sub_52ECF0` по индексу учётки (перепись всех 17 ссылок на `CUser+0x3BC3B` — выше).
	//   [ПОРТ] Решение владельца 10.09.2026: раз сервер прячет оператора сам, клиенту нужен
	//   критерий, который срабатывает и тогда, когда индекс до клиента почему-либо не доехал.
	//   ★ СВЕРЕНО С ЖИВОЙ БАЗОЙ 10.09.2026 (только чтение), и сверка показывает, ПОЧЕМУ имя
	//   берётся у ПЕРСОНАЖА, а не у учётки:
	//     `AsdaStoryOP.dbo.Us_member` — Userindex 1 = логин «sitxovski», 2 = логин «Rimy»;
	//        у ЛОГИНОВ префикса `GM_` НЕТ НИ У ОДНОГО, по ним критерий не сработал бы никогда.
	//     `Joy1.dbo.Charinfo` (все 5 записей базы):
	//        Userindex 1 → «GM_sitxovski», «sitx»
	//        Userindex 2 → «GM_Rimy», «ADM_sitxovski», «sitxovskiTest»
	//   Почему критерии сложены по ИЛИ, а не заменяют друг друга:
	//     · ТОЛЬКО ПО ПРЕФИКСУ мы бы пропустили «sitx» и «sitxovskiTest» — права у них ЕСТЬ
	//       (индексы 1 и 2), сервер прячет их при входе, а клиент считал бы себя видимым;
	//     · ТОЛЬКО ПО ИНДЕКСУ критерий молчит, пока не пришёл ответ логина 1004 (`UserIndex == 0`
	//       значит «ответа ещё не было», а не учётку №0).
	//   ⚠ ЧЕСТНО О ЦЕНЕ: сложение по ИЛИ добавляет ОБРАТНУЮ ошибку — персонаж с именем `GM_*`,
	//   чей индекс > 5000, получит клиентскую невидимость, оставаясь видимым на сервере. На живой
	//   базе таких нет (все имена с префиксом принадлежат индексам 1 и 2), а когда критерий
	//   срабатывает ТОЛЬКО по имени, в лог уходит предупреждение. Откат: `asda.Gm.OperatorByCharName 0`.

	/** Имя СВОЕГО персонажа для операторского критерия: общая точка `UAsdaWorldSubsystem::
	 *  GetSelfCharName()`, а если она ещё пуста — своя запись char-list по `LastSelectedRawSlot`
	 *  (та же, из которой мир это имя себе и копирует, но доступная НА МИНУТЫ раньше). */
	FString GetSelfCharNameForOperatorCheck() const;

	/** Имя своего персонажа начинается с одного из операторских префиксов
	 *  (`asda.Gm.OperatorNamePrefixes`, умолчание `GM_,ADM_`)? */
	UFUNCTION(BlueprintPure, Category = "Asda|GM")
	bool IsSelfOperatorByCharName() const;

	/** ★ ИТОГОВЫЙ клиентский критерий «я оператор» = индекс в диапазоне прав ИЛИ префикс имени. */
	UFUNCTION(BlueprintPure, Category = "Asda|GM")
	bool IsSelfOperator() const;

	/** Взвести/переподтвердить авто-скрытие входа (зовётся из обработчика 4003 — единственного
	 *  self-пакета ОБОИХ серверных путей, `CharMake` и `CharPortalMake`). Строго ДО `OnSelfSpawn`. */
	void ApplyGmHideOnEntry(const TCHAR* Why);

	/** Доопределение: критерий по имени остался НЕпроверенным (имени в момент 4003 не было).
	 *  Зовётся из `TrySpawnLocalPlayer` сразу после того, как имя село в мир-подсистему, — это
	 *  последняя точка ДО того, как павн станет видимым. Если ничего не ждали — no-op. */
	void ReevaluateGmHideOnEntryIfNamePending();

	/** Ждём ли доопределения критерия по имени (диагностика/лог). */
	bool IsGmHideEntryNamePending() const { return bGmHideEntryNamePending; }

	/** UserIndex учётки (ответ логина 1004, body+2) — для лога диагностики `asda.Gm.WhoAmI`. */
	uint32 GetUserIndexForDiagnostics() const { return UserIndex; }

	// ═══ ★★★ [ПОРТ] СЕРВЕРНЫЙ ПРИЗНАК ОПЕРАТОРА — ЭХО 6006 «ПРИНЯТО» (античит А2, мера 7; 12.10.2026) ═══════════
	//   Админ-функции клиента (GM-меню: свободная камера, транспорт) открываются ТОЛЬКО после того, как сервер в этой
	//   сессии принял операторскую команду 6005: обработчик `Send_HideCommand@0x54E970` проверяет гейт `sub_52ECF0`
	//   (UserIndex учётки 0…5000) и отвечает 6006 с байтом [0] = 1 (принято) или 0 (отказ). Прежний клиентский пароль
	//   (SHA-256 в бинаре) убран. Расчёт по UserIndex/имени (`IsSelfOperator`) — только ПРЕДСКАЗАНИЕ, им не открываем.
	//   Признак живёт одну сессию: полный сброс (разрыв, выход в выбор персонажа) его снимает, смена карты — нет.

	//   ★ ac0r (12.10.2026, ревизия А0/А2 СРЕД-2/СРЕД-3): проба прав GM-меню — НЕ 6005, а 6036 GetValue
	//   (`SendGmRightsProbe6036`): 6005 «принято» переписывает на сервере состояние действия оператора (CUser+21700 =
	//   300/301) и рассылает 4025 по карте — у сидящего/в позе оператора наблюдатели видели сброс позы. 6036 только
	//   читает и отвечает 6037 одному отправителю, и только если гейт прошёл. Признак ставят ОБА ответа: 6006 «принято»
	//   (пункт «Скрыться») и 6037. Неотвеченная проба на сессию — одна: после таймаута повторов нет до конца сессии
	//   (не-оператор с префиксом GM_/ADM_ под P47 ответа не получает — без повторов нет и строк в журнале сервера).

	/** Сервер в этой сессии подтвердил права: 6006 «принято» или ответ 6037 на пробу 6036. */
	bool IsOperatorConfirmedByServer() const { return bOperatorConfirmed6006; }
	/** Счётчик ответов 6006 за процесс (и «принято», и «отказ»): по его смене ждущий узнаёт, что ответ пришёл. */
	uint32 GetGmHideAnswerSeq() const { return GmHideAnswerSeq; }
	/** ★ ac0r: счётчик ответов 6037 на пробу прав 6036 за процесс — по его смене ждущий узнаёт, что ответ пришёл. */
	uint32 GetGmRightsAnswerSeq() const { return GmRightsAnswerSeq; }
	/** ★ ac0r: можно ли слать пробу прав 6036 — нет неотвеченной пробы в этой сессии (ответ снимает запрет). */
	bool CanSendGmRightsProbe() const { return !bGmRightsProbeUnanswered; }
	/** ★ ac0r: проба прав оператора — C→S 6036 `OP_GM_GET_VALUE_REQ`, тело 8 б (токен), как отправитель оригинала
	 *  `0xA0B550`. Шлётся не больше одного раза до ответа 6037 в сессии. Возвращает факт ОТПРАВКИ, не права. */
	bool SendGmRightsProbe6036();

	// ══════ ★★★ 19.10.2026: ШТАТНЫЕ ОПЕРАТОРСКИЕ КОМАНДЫ ОРИГИНАЛА (вкладка «Команды оригинала» GM-меню) ══════
	//   Раскладки тел, гейты сервера и ответы — `Reports/gm_original_commands/group1..4_spec.md`; реализация —
	//   `Private/AsdaNetGmOriginal.cpp`. Отправители оригинального клиента есть в бинаре, но их никто не зовёт (GM-окна
	//   в розничной сборке нет), поэтому тела повторены по их дизасму байт в байт. Отказ гейта сервера (оператор
	//   UserIndex 0…5000, под P47 — GM-флаг) МОЛЧАЛИВЫЙ: функции возвращают только факт отправки.
	//   Имена — cp1251, не длиннее 19 байт + NUL (имя ровно 20 б RelayServer дочитывает в соседние поля, group1 §2.1).
	/** 6012 Summon: вызвать игрока к себе по имени (тело 20 б). */
	bool SendGmSummon6012(const FString& TargetName);
	/** 6014 Meet: переместиться к игроку по имени (тело 20 б). */
	bool SendGmMeet6014(const FString& TargetName);
	/** 6018 Kick: выкинуть игрока по имени (тело 20 б; клиент цели сам закрывает связь по 6019). */
	bool SendGmKick6018(const FString& TargetName);
	/** 6022 Kill: убить игрока этого GameServer (тело 6 б: i16 userNum, i32 UserIndex цели). */
	bool SendGmKill6022(int32 TargetUserNum, int32 TargetUserIndex);
	/** 6026 Move: перенести игрока этого GameServer в клетку (тело 11 б; режим 0 — перенос). */
	bool SendGmMove6026(uint8 Mode, int32 CellX, int32 CellY, int32 TargetUserNum, int32 TargetUserIndex);
	/** 6028 / 6029: объявление сектору / всему серверу текстом из файла сервера `GM_Com_kor.txt` (тело — MD5 пустоты, 16 б). */
	bool SendGmLocalChat6028();
	bool SendGmGlobalChat6029();
	/** 6032 Add: прибавить себе — Type 0 опыт, 1 золото, 2 очки навыков (тело 5 б). */
	bool SendGmAdd6032(uint8 Type, int32 Value);
	/** 6033 GetMob: карточка/дроп монстра (тело 7 б: режим, индекс моба NpcServer, userNum — сервер перезапишет). */
	bool SendGmMobInfo6033(uint8 Mode, int32 MobIndex);
	/** 6035 Invincible: переключить неуязвимость оператора (тело 8 б — только токен; ответа нет). */
	bool SendGmInvincible6035();
	/** 6036 GetValue как ЗАПРОС ХАРАКТЕРИСТИК (ответ 6037 печатается в чат), в отличие от пробы прав — без защёлки. */
	bool SendGmStats6036();
	/** 6038 EventSet: включить ивент (тело 29 б). Type 0 дроп / 1 опыт; Mult 0…10; Channel/Map −1 = все. */
	bool SendGmEventSet6038(uint8 Type, float Mult, int32 Channel, int32 Map, uint8 SMon, uint8 SDay, uint8 SHour, uint8 SMin,
		uint8 EMon, uint8 EDay, uint8 EHour, uint8 EMin, uint8 MinLevel, uint8 MaxLevel);
	/** 6040 EventOff: выключить ивент (тело 15 б). */
	bool SendGmEventOff6040(uint8 Type, int32 Channel, int32 Map);
	/** 6042 ItemGive: выдать предмет игроку этого GameServer (тело 15 б; стопка 1…19; bToWarehouse — на склад). */
	bool SendGmItemGive6042(int32 TargetUserNum, int32 TargetUserIndex, int32 ItemCode, int32 Amount, bool bToWarehouse);
	/** 6045 MonsterPop: вызвать группу монстров (тело 5 б; Mode 0 — в точку GM, иначе — в свой прямоугольник). */
	bool SendGmMonsterPop6045(int32 Group, uint8 Mode);
	/** 6046 MonsterReturn: убрать группу монстров (тело 4 б). */
	bool SendGmMonsterReturn6046(int32 Group);
	/** 6075 UserCharValue: боевые проценты оператора против цели (тело 24 б; TargetType 1 игрок / 0 монстр). */
	bool SendGmCharValue6075(uint8 TargetType, int32 TargetIndex);
	// ── группа 4 (`group4_spec.md`) ──
	/** 4209 CallAllUserMap: собрать всех игроков карты Map СВОЕГО канала к себе (тело 4 б; каждому уходит 6013). */
	bool SendGmCallMap4209(int32 Map);
	/** 6007 DeleteMapAreaItem: убрать все предметы с земли в своём секторе (тело ПУСТОЕ, без токена и MD5). */
	bool SendGmClearGroundItems6007();
	/** 6010 CharJumpCommandPosition: прыжок оператора в клетку своей карты (тело 4 б). ★ 09.11.2026 [ПОРТ, P123]: MapId ≥ 0 —
	 *  форма P123 «на другую карту» (тело 8 б: X, Z, карта, метка 0xA123, без паддинга; сервер без P123 её молча отбрасывает). */
	bool SendGmJump6010(int32 CellX, int32 CellZ, int32 MapId = -1);
	/** 6024 «KillCommand_0» = воскресить мёртвого на месте / восполнить HP и MP живому (тело 6 б, раскладка 6022). */
	bool SendGmRevive6024(int32 TargetUserNum, int32 TargetUserIndex);
	/** 6058 UserSkillCoolTimeAllOff: сбросить все откаты навыков игроку этого GameServer (тело 2 б: userNum). */
	bool SendGmCoolReset6058(int32 TargetUserNum);
	/** 6145 CountDown: обратный отсчёт Seconds (> 0) — Scope 0 карта / 1 канал / 2 все каналы; EndAction 0/1/2 (тело 7 б). */
	bool SendGmCountDown6145(int32 Seconds, uint8 Scope, int32 EndAction);
	/** 6147 MapBlock: On 1 — включить блок выхода за поле (quizstage), иное — снять; A3/A4 никто не читает (тело 9 б). */
	bool SendGmMapBlock6147(uint8 On, int32 A3, int32 A4);
	/** ★ Ответы операторских команд (6019, 6023, 6025, 6027, 6034, 6037, 6043, 6044, 6058, 6075, 6145, 6147 …): опкод + тело как пришло.
	 *  Подписчик — `UAsdaGmOriginalSubsystem` (печать в чат, нотисы, запись предмета, кик). Зовётся из разбора
	 *  входящих — подписчик не шлёт из колбэка (закон no-reentrant-net-callbacks). */
	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnAsdaGmOriginalPacket, int32 /*Opcode*/, const TArray<uint8>& /*Body*/);
	FOnAsdaGmOriginalPacket OnGmOriginalPacket;
	/** ★ Закрыть игровую связь ТАК ЖЕ, как при обрыве сервером (`HandleConnectionClosed`: сброс сессии, `OnGameDisconnected`,
	 *  попап 8000). Нужен кику оператором 6019: эталон `Show_DisconnectPopup(2113)` закрывает свой сокет сам. Не из колбэка разбора. */
	void CloseAsServerDropped(const TCHAR* Why);
	/** ★ 19.10.2026 (ревизия С-2): своё поле `CUser+0x174` сервера = `LocalPlayer+0x41C` клиента (`network.c:43869`,
	 *  `entity_char.c:53188`) — его кладут и сверяют операторские команды (6022/6024/6042/6075, ответы 6013/6023/6025).
	 *  Клиент пишет туда блок 1017, то есть `SelectedCharId` (так же его трактуют анти-подмены 6129). До 1017 — UserIndex учётки. */
	int32 GetSelfCharUserIndex() const { return SelectedCharId != 0 ? SelectedCharId : (int32)UserIndex; }

	/** Снять серверный признак оператора и запрет повторной пробы (полный сброс сессии; самотест). Ничего не шлёт. */
	void ClearOperatorConfirmation() { bOperatorConfirmed6006 = false; bGmRightsProbeUnanswered = false; }
#if !UE_BUILD_SHIPPING
	/** ★ ТОЛЬКО САМОТЕСТ (`asda.Admin.SelfTestOperatorGate`): подменить UserIndex учётки, чтобы проверить гейт
	 *  меню без сервера; возврат — тем же вызовом со старым значением. В Shipping не существует. */
	void SetUserIndexForSelfTest(uint32 NewUserIndex) { UserIndex = NewUserIndex; }
#endif

	/** ★ ТОЛЬКО ДИАГНОСТИКА (консоль `asda.Gm.ForceHideState`): взвести подтверждённый признак
	 *  «я скрыт» локально, без отправки 6005. Прогоняет весь клиентский тракт (прозрачность,
	 *  эффекты, гейт боя) там, где нет доступа к операторской учётке. Наружу ничего не шлёт. */
	void ForceGmHiddenForDiagnostics(bool bHidden);

	bool SendTitleApply6067(int32 TitleIdPrefix, int32 TitleIdSuffix);
	/** C→S **6069** `OP_DELETE_FAME_INFO` (`netSendReq6069@0xE9B810`), payload 8 б:
	 *  [0..1] userNum i16 · [2..5] charId i32 · [6..7] titleId удаляемого звания i16.
	 *  Ответ — 6070 (`OnTitleDeleted`). */
	bool SendTitleDelete6069(int32 TitleId);

	// ═══ ★★★ ПОИСК ОТРЯДА (окно 8500 `Find_Party_WIn`) — ВОСЕМЬ отправителей ════════════════════
	//   Полный реверс — `Reports/extra_menu_partyfind.md` и `Reports/party_find_8500.md`.
	//   Конверт у ВСЕХ восьми — `SendSecured` (8 б скрамблированного таймстампа + MD5 16 б + тело),
	//   `bPad = true`: эталонный `CNetwork_Send(..., <размер>, OP, 1, 1, 1, 0, 0)` передаёт a6 = 1.
	//   Сервер читает тело с +33 (9 б транспорта + 8 б секрета + 16 б MD5) — размеры сходятся точно.
	//   ⚠ Во ВСЕХ телах `userNum` (i16) лежит на +0, а `charId` (i32) — на +2 (закон
	//     `charid-vs-usernum`): при несовпадении charId сервер МОЛЧА отвечает ошибкой и ничего
	//     не делает (`Recv_Client_UpdateMember`, `GameServer.c:33200`).
	//   ⚠ Канал в теле — ОДНОБАЗОВЫЙ (`g_nCurrentChannel + 1`), в UI список «Все / Канал 1…12».
	//   ⚠ «Мой номер в списке» (`RegId`) — u16, ПУСТО = `0xFFFF`, а не 0.
	//
	//   ★ УТОЧНЕНИЕ К ПЕРВОМУ ОТЧЁТУ (проверено вторым методом — читателями полей, а не именами):
	//     байт +6 = ПРОФЕССИЯ (`g_LocalPlayer+1093` = `CCharacter+0x445`, в порту `SelfJob`;
	//       тот же байт гейтит глобальный чат — `if (*(BYTE*)(g_LocalPlayer+1093))`);
	//     байт +7 = КЛАСС/ПОДКЛАСС (`+1080` = `0x438`, в порту `SelfClassId`;
	//       `playerClass = *(BYTE*)(g_LocalPlayer + 1080)`, `AsdaStory_game.c:491621`);
	//     байт +8 = УРОВЕНЬ (`+1082` = `0x43A`, в порту `SelfLevel`; тот же байт читают формула
	//       переносимого веса `…*(BYTE*)(g_LocalPlayer+1082) * 10.0` и гейт «уровень < 6»).
	//     В первом отчёте они были подписаны «класс / уровень / пол» — это ошибка, исправлена здесь.

	/** C→S **3410** `OP_RECV_CLIENT_ADD_MEMBER` (`netSendPlayerInfoReq_3410@0xDBBAD0`), тело 32 б —
	 *  ЗАРЕГИСТРИРОВАТЬ СЕБЯ в списке одиночек, ищущих отряд. Шлётся при открытии окна, когда
	 *  игрок НЕ в отряде. Раскладка: [0..1] userNum · [2..5] charId · [6] профессия · [7] класс ·
	 *  [8] уровень · [9] канал+1 · [10..11] паддинг · [12..31] имя персонажа (cp1251, ASCIIZ).
	 *  Ответ — 3411 (9 б). */
	bool SendPartyFindAddMember3410(const FString& SelfCharName);

	/** C→S **3412** `OP_RECV_CLIENT_SUB_MEMBER` (`Anticheat_sendTsMaskReport_3412@0xDBC1D0` —
	 *  имя в декомпиле ВРЁТ, привязка по опкоду), тело 8 б — СНЯТЬ СЕБЯ со списка одиночек.
	 *  [0..1] userNum · [2..5] charId · [6..7] мой номер в списке (0xFFFF = меня там нет).
	 *  Ответ — 3413 (7 б). */
	bool SendPartyFindSubMember3412(int32 MemberRegId);

	/** C→S **3414** `OP_RECV_CLIENT_GET_MEMBER` (`Anticheat_sendTsMaskReport@0xDBC820`), тело 21 б —
	 *  ЗАПРОСИТЬ СТРАНИЦУ списка одиночек. [0..1] userNum · [2..5] charId · [6..7] номер страницы ·
	 *  [8..9] мой номер в списке · [10..13] маска карт A · [14..17] маска карт B ·
	 *  [18] фильтр профессии (−1 = «Все») · [19] нижняя граница уровня · [20] верхняя граница.
	 *  ★ В первом отчёте байты 19/20 были подписаны «шаг страницы / размер страницы» — НЕВЕРНО:
	 *    эталон считает их как `10*(k−1)+1` и `10*k` по индексу комбобокса уровня, при «Все» — 1 и 100.
	 *  Ответ — 3415 (страница) либо 3411 с кодом ошибки. */
	bool SendPartyFindGetMember3414(int32 Page, int32 MemberRegId, uint32 MapMaskA, uint32 MapMaskB,
		int32 JobFilter, int32 LevelMin, int32 LevelMax);

	/** C→S **3416** `OP_RECV_CLIENT_UPDATE_MEMBER` (`net_SendRequest3416@0xDBCFE0`), тело 22 б —
	 *  ОБНОВИТЬ СВОЮ ЗАПИСЬ в списке одиночек (сменился фильтр карт). [0..1] userNum · [2..5] charId ·
	 *  [6] профессия · [7] класс · [8] уровень · [9] канал+1 · [10..11] паддинг ·
	 *  [12..15] маска A · [16..19] маска B · [20..21] мой номер в списке. Ответ — 3417 (9 б). */
	bool SendPartyFindUpdateMember3416(int32 MemberRegId, uint32 MapMaskA, uint32 MapMaskB);

	/** C→S **3418** `OP_RECV_CLIENT_ADD_LEADER` (`netSendPartyBossPacket@0xDBD700`), тело 36 б —
	 *  ОПУБЛИКОВАТЬ СВОЙ ОТРЯД в списке набора (только лидер). [0..1] userNum · [2..5] charId ·
	 *  [6] профессия · [7] класс · [8] уровень · [9] канал+1 · [10..11] паддинг ·
	 *  [12..31] имя лидера (cp1251) · [32] нужное число членов (эталон всегда шлёт 6) ·
	 *  [33] «я в отряде» · [34..35] индекс отряда. Ответ — 3419 (9 б). */
	bool SendPartyFindAddLeader3418(const FString& SelfCharName, int32 WantMembers,
		int32 InPartyFlag, int32 PartyIndex);

	/** C→S **3420** `OP_RECV_CLIENT_SUB_LEADER` (`netSendPartyLeaderCmd_3420@0xDBDE60`), тело 8 б —
	 *  СНЯТЬ СВОЙ ОТРЯД со списка набора. [0..1] userNum · [2..5] charId ·
	 *  [6..7] номер моего отряда в списке (0xFFFF = его там нет). Ответ — 3421 (7 б). */
	bool SendPartyFindSubLeader3420(int32 LeaderRegId);

	/** C→S **3422** `OP_RECV_CLIENT_GET_LEADER` (`netChar_SendChecksummedStatePacket@0xDBE4C0` —
	 *  имя ВРЁТ), тело 20 б — ЗАПРОСИТЬ СТРАНИЦУ списка отрядов. [0..1] userNum · [2..5] charId ·
	 *  [6..7] номер страницы · [8..9] номер моего отряда в списке · [10..13] маска A ·
	 *  [14..17] маска B · [18] фильтр канала (индекс комбобокса − 1; −1 = «Все») ·
	 *  [19] фильтр цели (`remapIndexForward` от индекса комбобокса; −1 = «Все»).
	 *  Ответ — **3423** (страница 350 б). ★ При расхождении charId оригинальный GameServer шлёт
 *  ту же 350-байтную страницу под НОМЕРОМ 3421 — дефект сервера Д2, разводим по длине. */
	bool SendPartyFindGetLeader3422(int32 Page, int32 LeaderRegId, uint32 MapMaskA, uint32 MapMaskB,
		int32 ChannelFilter, int32 GoalFilter);

	/** C→S **3424** `OP_RECV_CLIENT_UPDATE_LEADER` (`party_sendBossInfoSecure@0xDBEC10`), тело 25 б —
	 *  ОБНОВИТЬ ОПИСАНИЕ СВОЕГО ОТРЯДА (кнопка «Применить» панели правки 8662).
	 *  [0..1] userNum · [2..5] charId · [6] см. ниже · [7] см. ниже · [8] уровень · [9] канал+1 ·
	 *  [10..11] паддинг · [12..15] маска A · [16..19] маска B · [20] цель отряда ·
	 *  [21] нужное число членов (3…6) · [22] «я в отряде» · [23..24] номер моего отряда в списке.
	 *  ★★★ ДЕФЕКТ ОРИГИНАЛА Д1 (`asda.PartyFind.LegacyBossJobByte`): эталон пишет байт **+6 ДВАЖДЫ** —
	 *    `BYTE2(bossInfoStruct[1]) = *(BYTE*)(g_LocalPlayer + 1093)` (профессия), а следующей же
	 *    строкой `BYTE2(bossInfoStruct[1]) = *(BYTE*)(g_LocalPlayer + 1080)` (класс). Второе
	 *    присваивание затирает первое, и байт +7 (там должен быть класс) уходит нулём из
	 *    `sub_91F75C`. То есть у 3424 сервер получает КЛАСС в поле профессии и НОЛЬ в поле класса,
	 *    хотя у 3410/3416/3418 те же поля заполнены правильно. Проверено дизассемблером
	 *    `0xDBEC10` (два подряд `mov byte ptr [ebp-XX+6]`), не только Hex-Rays. */
	bool SendPartyFindUpdateLeader3424(int32 LeaderRegId, uint32 MapMaskA, uint32 MapMaskB,
		int32 Goal, int32 WantMembers, int32 InPartyFlag);

	// ── Приём. ⚠ У ОРИГИНАЛЬНОГО КЛИЕНТА ПРИЁМА НЕТ ВООБЩЕ: среди 415 `case` функции
	//   `Net_RecvPacketDispatch_Loop@0x96AD30` нет ни одного из 3411…3425, а тела приёмников
	//   (`sub_DBF4A0`, `sub_DBFA00`) не имеют вызывателей. Ветки заведены нами заново.
	/** Короткий ответ сервера: 3411 / 3417 / 3419 / 3425 (9 б: +0 userNum, +2 charId, +6 код,
	 *  +7 u16 присвоенный индекс записи) и 3413 / 3421 (7 б, без индекса).
	 *  Коды `+6`: 1 = успех · 2 = charId тела ≠ charId сессии · 3 = нет слота / индекс вне
	 *  диапазона · 4 = charId записи не совпал (у 3411 «уже в списке», у 3419 «не лидер») ·
	 *  5 = отряд уже опубликован. ★ Оригинальный клиент проверяет ТОЛЬКО код 1, прочие молча
	 *  игнорирует, нотисов у окна нет (`sub_DBF410`, `sub_DBF960` разобраны целиком). */
	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnAsdaPartyFindAck, int32 /*opcode*/, const TArray<uint8>& /*body*/);
	FOnAsdaPartyFindAck OnPartyFindAck;
	/** Страница списка: **3415** — одиночки, 300 б = 10 хедера + 10 записей × 29;
	 *  **3423** — отряды, 350 б = 10 хедера + 10 записей × 34. Обе формирует `CPartyMatching`
	 *  в `ChannelManager.exe`, GameServer только пересылает. Раскладка снята побайтово —
	 *  `Reports/partyfind_3421_layout.md` §2 и §3. */
	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnAsdaPartyFindPage, int32 /*opcode*/, const TArray<uint8>& /*body*/);
	FOnAsdaPartyFindPage OnPartyFindPage;
	/** Байты +6/+7/+8 тела: профессия / класс / уровень локального игрока. Одна точка на все
	 *  четыре «длинных» тела поиска отряда, чтобы поля нельзя было перепутать по месту. */
	void PartyFindSelfTriplet(uint8& OutJob, uint8& OutClass, uint8& OutLevel) const;

	// ★ ЧИСТЫЙ выход из мира → возврат в char-select (1028, 56б). Сервер сохраняет позицию + снимает блок
	//   движения + сбрасывает статус аккаунта (иначе застревает состояние 3 → следующий вход без движения).
	bool SendReturnToCharSelect();
	// ★ Сброс «состояния 3»/overlap при авторизации (1010, тот же формат). Точный триггер — по декомпилу LoginManager.
	bool SendResetState3();

	// ★ C→S **4070** — ФЛАГИ ОКНА НАСТРОЕК (приём заявок, показ ОЗ/имени, показ шлема).
	//   Эталон `uiOptionsSendSettingsPacket2@0xED69E0` (ui_core.c:89291), конец
	//   `optApplySettingsFromConfig@0xED1F10`: тело 19 байт, конверт «MD5 без секрета»
	//   (`[0..15] MD5(payload) + [16..34] payload`, длина 35) — то есть наш `SendMd5Signed`.
	//   Раскладку тела собирает ВЛАДЕЛЕЦ окна (`UAsdaSystemOptionSubsystem`), здесь — только
	//   конверт и опкод: так же, как у всех прочих отправителей этой подсистемы.
	//   Обратная сторона — блок 19 байт в 1027 (`ServerOptionBlock`, `OnServerOptionFlags`) → окно настроек
	//   (`UAsdaSystemOptionSubsystem::ApplyServerOptionFlags` = `optionsApplyServerFlags@0xED1B40`), сделано 25.10.2026.
	//   Байт [15] (показ шлема) сервер при смене рассылает соседям 4025 действием 111 (`OnHelmetShowSync`).
	bool SendOptionFlags4070(const uint8* Body19);
	// ★ Финализация входа в мир (эталон prog11/prog13): анти-чит-токены 4122+4124 (26б) + 4301 (40б).
	//   Подтверждают серверу завершение загрузки. Зовётся после спавна (4003)/RevealWorld.
	void SendEntryFinalize();
	// C→S 4001 OP_CHAR_LOGON_REQ (36б = секрет8 + MD5(блок,12) + блок12) — триггер DB-цепочки сервера.
	//   Блок (сервер CharLogonReq@0x527500): [0..3]=charId, [4..5]=0 (сервер не читает), [6..7]=rawSlot
	//   (iCharIndex, 0=ошибка), [8..9]=[10..11]=0xFFFF («нет сохранённой позиции» → спавн-точка карты).
	//   Зовётся Load Manager'ом, когда целевой уровень карты загружен (аналог эталонного prog 3.0).
	bool SendCharLogonReq();
	// C→S 4054 OP_RECV_CHAR_REGEN (25б = секрет8+md5-16+u8 режим): режим 0 = «я готов, покажи меня» (финал
	//   входа); сервер отвечает 4055 (позиция+HP/MP/EXP) и помечает юзера «в мире» (CUser+0x141=1).
	bool SendCharRegen(uint8 Mode);
	// Спавн self (4003, 9б): позиция игрока (тайлы; мир = тайл*100+50). word[3]==200 → вход мёртвым.
	int32 SelfTileX = -1, SelfTileZ = -1;
	bool  bSelfSpawnDead = false;
	// ★★★ 25.10.2026 [ОРИГИНАЛ] ПОКАЗ ШЛЕМА своего персонажа — 4003 [8]. Эталон `Recv_SpawnSelf@0xF3E630`
	//   (network.c:40581-40591): `== 1` → 1, иначе 0, затем `CCharacter_ApplyEquippedHelmet`. Сервер кладёт `CUser+0x4A6`
	//   и в `CharMake`, и в `CharPortalMake` (дизасм GameServer.exe.asm:447222/447866) ⇒ флаг переживает портал и перезаход
	//   (в БД — `Charoption.Hat`, `UPCharOptionUP`/`UPCharOption`). 1 до первого 4003 (конструктор персонажа ставит 1).
	uint8 SelfHelmetShow = 1;
	// ★ 25.10.2026 [ОРИГИНАЛ] БЛОК ОПЦИЙ ПЕРСОНАЖА С СЕРВЕРА — 19 байт тела 1027 с +632 (`Send_SelectChar@0x40AD30`
	//   LoginManager: `EXEC UPCharOption` → v94 @ [ebp-3A0h], тело с [ebp-618h]). Раскладка = тело 4070 (столбцы
	//   `Charoption`: NormalChat…Whisper, Acv, SmEnroll…GuildEnroll, Change, Schange, Monster, Charname, **Hat [15]**,
	//   SoulAttack, ClanAttack, UserAttack). Эталон отдаёт его `optionsApplyServerFlags@0xED1B40` (network.c:34302).
	//   Пусто — блок ещё не приходил.
	TArray<uint8> ServerOptionBlock;
	// Делегат: пришёл блок опций 1027 (19 байт). Слушатель — окно настроек (`UAsdaSystemOptionSubsystem`).
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnAsdaServerOptionFlags, const TArray<uint8>& /*Block19*/);
	FOnAsdaServerOptionFlags OnServerOptionFlags;
	// ★ In-world userNum сервера (4003/0xFA3 body[0..1] = CharMake param_2). Именно это идёт в поле
	//   move msgBody[1..2] (эталон targetUserIdx == own userIdx). Перехват оригинала: =0 (одиночный игрок).
	//   Раньше слали UserIndex&0xFFFF (=charId low word) — сервер CharMove: if(local_2f!=param_2) молча пропускал.
	uint16 SelfUserNum = 0;
	// ★★★ ДЕФЕКТ ПОРТА, найден 2026-08-24 на живом: userNum = 0 — ЗАКОННОЕ значение (наш сервер
	//   выдаёт его первому вошедшему, лог `SpawnSelf 4003: userNum=0`), а весь порт использовал ноль
	//   как «ещё не знаю». Из-за этого гейт №4 применения расходника глушил ВСЕ зелья владельца
	//   (`5028 не отправлен: свой userNum ещё не известен`), а раньше так же ломался поиск своего
	//   павна. Признак «известен» теперь отдельный, сравнение — только через `IsSelfUserIdx`.
	bool bSelfUserNumKnown = false;
	// Свой userNum уже получен пакетом 4003.
	bool IsSelfUserNumKnown() const { return bSelfUserNumKnown; }
	// Принадлежит ли этот userIdx нам. Единственная правильная форма сравнения со своим номером.
	bool IsSelfUserIdx(int32 Uidx) const { return bSelfUserNumKnown && Uidx == (int32)SelfUserNum; }
	// ★ ТОЛЬКО ДЛЯ ЖИВЫХ ОФЛАЙН-ПРОВЕРОК (`Asda.World.FakeSelfSpawn`): офлайн пакет 4003 не
	//   приходит, признак «свой номер известен» остаётся ложным, и `IsSelfUserIdx` не узнаёт даже
	//   собственного павна ⇒ канал «эффект НА персонаже» проверить нечем. В игровом пути этой
	//   функции нет ни одного вызова — номер приходит пакетом (`AsdaNetSubsystem.cpp:4084`).
	void SetSelfUserNumForDebug(uint16 Num) { SelfUserNum = Num; bSelfUserNumKnown = true; }
	TArray<uint8> SelfAppearance;       // тело 4005 (151б, внешность self) — для сборки перса (B4)
	// ★ itemId НАДЕТОГО оружия self (4048 слот 9 / 0xFB5 partType 6). Тип оружия = ItemSubType записи,
	//   дальность атаки = AttackRangeTiles×100 (эталон: игрок +0x2EA10 = квадрат мир-дальности). 0 = кулаки.
	int32 SelfWeaponItemId = 0;
	// ★ ЗАТОЧКА своего снаряжения (свечение оружия/щита, weapon_glow_re.md): байт +0x18 в 57-байтных
	//   инстансах батча 4048 (слот @+5), и байт body+8 в 4021/0xFB5 при смене экипа.
	int32 SelfWeaponGrade = 0;   // уровень заточки своего оружия (экип-слот 9)
	int32 SelfShieldItemId = 0;  // itemId своего щита (экип-слот 8)
	int32 SelfShieldGrade = 0;   // уровень заточки своего щита
	DECLARE_MULTICAST_DELEGATE(FOnAsdaSelfEquipGrade);
	FOnAsdaSelfEquipGrade OnSelfEquipGrade;   // заточка/предмет своего оружия-щита изменились → пересобрать свечение
	DECLARE_MULTICAST_DELEGATE(FOnAsdaSelfSpawn);
	FOnAsdaSelfSpawn OnSelfSpawn;       // 4003 получен → мир-система спавнит персонажа (B4)
	// ── ★ HUD: уровень/опыт self (EXPBar_WIn, разбор — AsdaHudSubsystem.h) ──
	//   SelfLevel: сид = 4005-self @+40, далее 4036 @+2. SelfExp (−1 = неизвестен): 4055 @+35 /
	//   4056 @+12 / 4038 @+1 / 4036 @+3 (все i64; эталон — вызовы UI_UpdateExpBar).
	uint8 SelfLevel = 0;
	// ★★★ ПРОФЕССИЯ (job) локального игрока. −1 = ещё не получена.
	//   ОТКУДА В ЭТАЛОНЕ: `charApplyFullStatusPacket @0xD83670` (entity_char.c:46312) пишет
	//   `CCharacter->gap_43B[10] = *(BYTE*)(body + 17)`; рядом `gap_438[0] = body+18`,
	//   `gap_438[1] = body+19`, `gap_43B[8..9] (WORD) = body+20`. Вызывается из
	//   `questItemResult_apply @0xC63010` (quest.c:3455) по телу пакета (`srcPacket + 9` = Body[0]).
	//   Второй писатель — `net_ApplyPlayerFullUpdate @0xD828A0` (network.c:21838):
	//   `gap_43B[10] = savedThis + 60374` (там же +60375 = флаг смены профессии).
	//   Именно этот байт читает чат: «Глобальный чат доступен только персонажам, у которых есть
	//   профессия» (loc 7004, chat.c:3893) — ★ НИКАКОГО ПОРОГА УРОВНЯ В КОДЕ НЕТ.
	//   ★ ИСТОЧНИК В ПОРТУ: блок char-select 1017, смещение +41 (= 0x445 − 0x41C). Пакет полного
	//   статуса (`charApplyFullStatusPacket`, Body[17]) обновит то же поле, когда будет заведён.
	int32 SelfJob = -1;
	// ★★★ ПОДКЛАСС (`SubJob`, `CCharacter+0x439` = `gap_438[1]`). Читает окно навыков для подписи
	//   вкладки 4-й профессии (`uiPopulateUpgradeOptionList@0xE54900`: нотисы 9422..9427).
	//   ИСТОЧНИК: блок char-select 1017, смещение +29 (= 0x439 − 0x41C). −1 = ещё неизвестен.
	int32 SelfSubJob = -1;
	// ★★★ СВОБОДНЫЕ ОЧКИ УМЕНИЙ (`CCharacter+0x443` = `gap_43B[8]`, i16).
	//   ЧИТАТЕЛЬ В ЭТАЛОНЕ: `skillUiUpdatePointText@0xE56720` (печатает счётчик 1092 SKILL_POINTTXT),
	//   `skillWndSelectSlot@0xE53C80` (гейт «очков > 0»), `Skill_BuildTreeUI` (оверлей «доступен»).
	//   ПИСАТЕЛИ: блок char-select 1017 +39 (первый и единственный на входе в мир), далее ответы
	//   сервера 5254 (@+1 i16), 5462 (@0 i16) и 5464 (@0 i16). −1 = сервер ещё ничего не присылал.
	int32 SelfSkillPoints = -1;
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnAsdaSelfSkillPoints, int32 /*points*/);
	FOnAsdaSelfSkillPoints OnSelfSkillPoints;
	// ★★★ КЛАСС/ПОДКЛАСС локального игрока — байт `CCharacter+0x438` (`gap_438[0]`), НЕ `SelfJob`.
	//   Именно его читает `hudRefreshClassAndElementIcons@0xE79850` (ui_core.c:75387-75404) для выбора
	//   иконки класса мини-статуса: 0 → Tenderfoot(28), 1..7 → Warrior(29), 12..18 → Archer(30),
	//   23..29 → Wizard(31), прочие (8..11, 19..22, >29) → НИ ОДНОЙ (провалы так в оригинале).
	//   ОТКУДА: запись персонажа char-select `0x3EE rec+24` (в порту `FAsdaCharRecord::Job`) и
	//   `4005 self @+38` (у соседей то же поле = `FAsdaRemotePlayer::ClassId`). Писатели в эталоне —
	//   `charApplyFullStatusPacket@0xD83670` (`gap_438[0] = body+18`, entity_char.c:46368) и
	//   `net_ApplyPlayerFullUpdate@0xD828A0` (смена профессии, network.c:21840). −1 = ещё неизвестен.
	int32 SelfClassId = -1;
	// ★★★ 18.10.2026 — СОБЫТИЕ «ПРОФЕССИЯ/КЛАСС СМЕНИЛИСЬ» (ветка смены профессии 5042,
	//   `net_ApplyPlayerFullUpdate@0xD828A0`, network.c:21835-21889: `case 0` + `jobChangeFlag` = body[10] ≠ 0).
	//   Рассылается ПОСЛЕ записи `SelfJob/SelfClassId/SelfSubJob/SelfSkillPoints` и ДО `OnQuestTurnInResult`, чтобы
	//   и квестовые метки, и диалог NPC (страница после сдачи) считали условия класса уже по новому классу.
	//   Подписчики — те, кого эталон обновляет в этой ветке: баннер 18500 + звук 15 + нотис 20200/6177
	//   (`UAsdaChangeJobBannerSubsystem`), окно характеристик (`UI_FillEquipDetailPanel`), окно навыков
	//   (`uiPopulateUpgradeOptionList` + `skillWnd_msgProc(12005, 0)`), магазин снаряжения 6800
	//   (`inventoryBag_DispatchRebuild` + `invBagRebuildItemGrids`), квестовые метки (`npcUiRefreshActionList`).
	//   Ключ отката `asda.Quest.ApplyJobChange 0` — прежний путь (поля пишет баннер из своего обработчика 5042, события нет).
	DECLARE_MULTICAST_DELEGATE_FourParams(FOnAsdaSelfJobChanged, int32 /*oldClassId*/, int32 /*job*/, int32 /*classId*/, int32 /*subJob*/);
	FOnAsdaSelfJobChanged OnSelfJobChanged;
	// Включена ли ветка «сеть применяет смену профессии и очки из 5042 и рассылает `OnSelfJobChanged`»
	//   (дев-ключ `asda.Quest.ApplyJobChange`, по умолчанию 1; в Shipping — всегда 1).
	static bool IsQuestJobChangeApplyOn();
	int64 SelfExp = -1;
	// ★★★ 4038 отдаёт ЧЕТЫРЕ значения, а не два — от них зависит ВЕСЬ показ получения опыта
	//   (эталон `netHandleHpExpUpdate@0xF427A0`, полный разбор — `Reports/exp_gain_ui.md`):
	//     `Gained`      — прибавка, тело **i64**@+9 (раньше читали i32 — обрезали большие награды);
	//     `bFullExp`    — байт@0: 1 = «полный» (наградной) опыт ⇒ надпись над СОБОЙ + доп. строка
	//                     чата loc 2316+2317; 0 = обычный (убийство моба, рыбалка, крафт);
	//     `SrcRenderId` — i16@+17: −1 = показывать над собой, ≥0 = renderId сущности (убитый моб),
	//                     над которой оригинал рисует надпись стилем 0x14.
	//   Прежняя семантика «gained>0 только при flag==1» была НЕВЕРНА: эталон показывает прибавку
	//   во ВСЕХ трёх ветках, флаг лишь выбирает ветку.
	DECLARE_MULTICAST_DELEGATE_FiveParams(FOnAsdaSelfExp, int32 /*level*/, int64 /*exp*/,
		int64 /*gained*/, bool /*bFullExp*/, int32 /*srcRenderId*/);
	FOnAsdaSelfExp OnSelfExp;
	DECLARE_MULTICAST_DELEGATE(FOnAsdaSelfLevelUp);
	FOnAsdaSelfLevelUp OnSelfLevelUp;   // 4036 self: звук S_c_levelup + эффект lvup01 (эталон Recv_LevelUp)
	// ★ 4036 для ЧУЖОГО персонажа — новый уровень соседа. Эталон пишет его в `CCharacter+1082`
	//   найденного по userIdx (`Recv_LevelUp@0xF424C0`), и оттуда его читает ПАНЕЛЬ ЦЕЛИ
	//   (`HUD_TargetLevelDigits(hud, *(target+1082))`). В порту у соседей уровень живёт в реестре
	//   `UAsdaWorldSubsystem::NetPlayers`, поэтому 4036 обязан его туда донести — иначе на панели
	//   цели навсегда останется уровень из снапшота появления.
	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnAsdaRemoteLevel, int32 /*userIdx*/, int32 /*level*/);
	FOnAsdaRemoteLevel OnRemoteLevel;
	// ── ★ NOTICE_WIN: серверное уведомление 3113 (payload: mode u16@+2, текст@+4, RGB@+100..102) ──
	DECLARE_MULTICAST_DELEGATE_ThreeParams(FOnAsdaServerNotice, int32 /*mode*/, const FString& /*text*/, FColor /*rgb*/);
	FOnAsdaServerNotice OnServerNotice;

	// ══════════════ ★ ЧАТ: приём строк (окно CHAT_NEW 2500), 6 опкодов ═══════════════════════
	// Разбор форматов — реверс 2026-08-05 по RE/etalon/network.c. Полезная нагрузка у всех
	// начинается с pkt+9, то есть у нас это ровно `Pkt.Body[0]`.
	//   5085 OP_CHAT_NORMAL   Recv_ChatNormal   @0xBA6AD0  заголовок 26 б, текст с +26, категория 0
	//   5087 OP_CHAT_MESSAGE  Recv_ChatMessage  @0xBA88E0  заголовок 27 б, текст с +27, категория 1
	//   5089 OP_CHAT_WHISPER  Recv_ChatWhisper  @0xBA7530  заголовок 29 б, текст с +29, категория 3/5
	//   4121 OP_SYSTEM_MESSAGE Recv_SystemMessage @0xBA82D0 заголовок 28 б, текст с +28, категория 4
	//   6561 (нет в enum)     Recv_WhisperOrSystemMessage @0xBA8560 заголовок 32 б, текст с +32,
	//                                                     категория = payload[1] + 7
	//   6719 OP_CLAN_CHAT     Recv_ClanChat     @0xCA4D30  флаг@0, имя@+1 (20 б), текст@+21, категория 6
	// Одна принятая строка — в виде, готовом для Chat_AddChatLine.
	struct FAsdaChatLineIn
	{
		int32   Category = 0;                     // a2 эталона (см. таблицу категорий)
		FString Text;                             // ГОТОВАЯ строка (имя уже подставлено)
		FColor  Fallback = FColor(255,255,255,255); // a4 — применяется, ТОЛЬКО если у категории нет слота
		FColor  RowBg = FColor(0,0,0,0);          // a8 — ранговый цвет = ЗАЛИВКА ФОНА строки
		bool    bRowBg = false;                   // a9 — флаг заливки
		int32   SrcOpcode = 0;                    // откуда пришло (для дампов)
		FString SenderName;                       // имя отправителя (для игнор-листа, ЭТАП 3)
		int32   SenderUserIdx = -1;               // userIdx отправителя (для игнор-листа, ЭТАП 3)
		// ★★★ АВТОРСТВО СТРОКИ — ПРИЗНАК ДЛЯ ИГНОР-ЛИСТА (Д27 п.2).
		//   true = строку НАПИСАЛ игрок `SenderName` (обычный/глобальный/отрядный/клановый/
		//          армейский чат, входящий шёпот, широкие каналы) ⇒ «Блокировать чат» её глушит.
		//   false = строка лишь УПОМИНАЕТ имя, но авторства у него нет: служебные уведомления
		//          шёпота 5089 msgType 0/2/3 («игрок не в сети») и ЭХО СОБСТВЕННОГО исходящего
		//          шёпота 5089 msgType 1 при flag != 0 — их глушить нельзя, это ответы на
		//          действия САМОГО игрока.
		bool    bSenderAuthored = false;
		int32   RankLevel = 0;                    // ранг 1..100 (0 = нет)
		// ── ★ ОБЛАЧКО ЧАТА НАД ГОЛОВОЙ (entityName_DrawOverhead @0xBAD1B0) ──────────────────
		// Эталон рядом с Chat_AddChatLine зовёт entityName_DrawOverhead(chatMgr, ent, color,
		//   msgText, fontFlag, variant). ВАЖНО: в облачко идёт СЫРОЙ текст сообщения (msgText/&Str),
		//   БЕЗ префикса «[Имя]: » — в отличие от строки чат-лога. Поля ниже — ровно аргументы вызова.
		int32   BalloonVariant = -1;              // −1 = облачка нет; 0 = TALKBOXW, 1 = TALKBOXB, 2 = TALKBOX_Mon
		FString BalloonText;                      // a5 — сырой текст сообщения (без «[Имя]: »)
		FColor  BalloonColor = FColor(0,0,0,255); // a4 — цвет ТЕКСТА облачка (dword эталона, sRGB)
		int32   BalloonFontFlag = 0;              // a6 → CClass_SetField926(0x100000 при 0, 0x200000 при 1)
		int32   SenderCharId = -1;                // charId отправителя (5089/4121/5106 ищут по нему)
	};
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnAsdaChatLine, const FAsdaChatLineIn&);
	FOnAsdaChatLine OnChatLine;
	// ── ★ 8008: облачко над МОБОМ (recvCharChatBubble @0xBA7FE0) ─────────────────────────────
	//   Тело: 10-байтный заголовок (renderId = WORD@+2), далее строка «Имя: текст»; клиент режет
	//   её по ПЕРВОМУ двоеточию и берёт хвост с отступом +2. Цвет текста −51416 = 0xFFFF3728.
	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnAsdaMobChatBubble, int32 /*renderId*/, const FString& /*text*/);
	FOnAsdaMobChatBubble OnMobChatBubble;
	// ── ★ HERO_NOTICE: 6131 «игрок получил редкий предмет» (itemId dword@0, имя cp1251@+4) ──
	// 6131. Первые 24 байта тела — как в оригинале; байты 24+ добавлены нашим сервером
	// (AsdaPatch.dll, P11): тег источника, ступень и ПРОЦЕНТ заточки, признак неудачи.
	//   src: 0 legacy/6616, 1 подбор, 2 гача, 3 премиум-гача, 4 заточка-успех, 5 заточка-неудача
	DECLARE_MULTICAST_DELEGATE_FiveParams(FOnAsdaItemAcquired, const FString& /*charName*/, int32 /*itemId*/,
		int32 /*src*/, int32 /*refineLevel*/, int32 /*refinePercent*/);
	FOnAsdaItemAcquired OnItemAcquired;
	DECLARE_MULTICAST_DELEGATE(FOnAsdaWorldReady);
	FOnAsdaWorldReady OnWorldReady;     // 4055/4056 получен → игрок «в мире» (B6; живой тест: первый вход = 4056)
	// ★ 22.10.2026: какой пакет поднял последний `OnWorldReady` (4055 / 4056) и «прибыл мёртвым» (4056 state@20 == 200) —
	//   слушателю нужен список окон эталона своей ветки (`AsdaEntryWindows::ApplySelfEntry`). Пишутся ПЕРЕД рассылкой.
	int32 LastWorldReadyOpcode = 0;
	bool  bLastWorldReadyDeadArrival = false;
	bool  bLastWorldReadyMapChange = false;   // 4055 на ДРУГУЮ карту (`B[2] != EnterMapId`) — порт перегрузит уровень
	// ── ★ СМЕРТЬ (4053, Recv_CharDeath_Apply@0xF42E50): userIdx умершего + userIdx убийцы (-1 = нет).
	//   Свой userIdx → меню смерти (After_Death_Win) и полная блокировка действий на СЕРВЕРЕ до 4054.
	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnAsdaCharDeath, int32 /*userIdx*/, int32 /*killerIdx*/);
	FOnAsdaCharDeath OnCharDeath;
	// ★ ПОЗИЦИЯ ПОСЛЕ ВОСКРЕШЕНИЯ/ТЕЛЕПОРТА, назначенная СЕРВЕРОМ (4056 tileX@2/tileY@4;
	//   4055 mapId@2 + tileX@25/tileY@27). MapId >= 0 → карта СМЕНИЛАСЬ (нужна загрузка уровня),
	//   -1 → телепорт в пределах текущей карты.
	DECLARE_MULTICAST_DELEGATE_ThreeParams(FOnAsdaSelfWarp, int32 /*mapId (-1 = та же карта)*/, int32 /*tileX*/, int32 /*tileZ*/);
	FOnAsdaSelfWarp OnSelfWarp;
	// ★ HP/MP self из пакета воскрешения/входа (4056 HP@6/MP@10, 4055 HP@29/MP@33).
	//   Формула сервера SetCurrentRegenHpValue@0x43C8A0: обычное воскрешение = maxHP/2 (50 %).
	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnAsdaSelfVitals, int32 /*hp*/, int32 /*mp*/);
	FOnAsdaSelfVitals OnSelfVitals;

	// ★ R1 (10.11.2026): структура `FAsdaVitals` и её разбор вынесены дословно в `Public/Net/AsdaNetWorldTypes.h`.
	//   Имя `UAsdaNetSubsystem::FAsdaVitals` сохранено псевдонимом — тип тот же.
	using FAsdaVitals = ::FAsdaVitals;
	// userIdx → витальные значения. Ключ SelfUserNum = свой персонаж.
	TMap<int32, FAsdaVitals> VitalsByUserIdx;
	// Любое изменение витальных значений (единственный сигнал для плашек/HUD).
	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnAsdaVitalsChanged, int32 /*userIdx*/, const FAsdaVitals& /*V*/);
	FOnAsdaVitalsChanged OnVitalsChanged;
	// ★ 6539 (ответ на наш запрос 6538 «HP/MP цели») — витальные значения по charId, не по userIdx
	//   (эталон netRecvCharStateUpdate@0xE82A70: CharManager_FindByCharId). Разрешение charId→userIdx
	//   живёт в мир-подсистеме (реестр чужих игроков) — оттуда значения ложатся в единый источник.
	DECLARE_MULTICAST_DELEGATE_FiveParams(FOnAsdaCharStateUpdate, int32 /*charId*/, int32 /*maxHp*/, int32 /*curHp*/, int32 /*maxMp*/, int32 /*curMp*/);
	FOnAsdaCharStateUpdate OnCharStateUpdate;
	// ★ Всплывающая цифра дельты HP/MP (эталон netHandleHpMpUpdate case 1/4/6 → FloatingText стиль 0x12)
	//   + бафф-FX по skillId (267 bleed, 268 bleed2, 269 hp-reco, 275 manasu01, 276 hill01, 296 blossom).
	DECLARE_MULTICAST_DELEGATE_FourParams(FOnAsdaVitalsFx, int32 /*userIdx*/, int32 /*delta*/, uint8 /*updateType*/, int32 /*skillId*/);
	FOnAsdaVitalsFx OnVitalsFx;
	// ★ R5-fix8 (14.11.2026): на время рассылки `OnVitalsFx` из пакета 4058 — «цифру рисовать и при дельте 0» (как оригинал;
	//   ключ `asda.Vitals.HpZeroDigit`). Вне рассылки — false.
	bool bVitalsFxZeroDigit = false;
	// ★ R5-fix6 (13.11.2026) [ОРИГИНАЛ]: эффект МГНОВЕННОГО состояния из пакета HP 4058 / MP 4059 (`netHandleHpMpUpdate@0xF43D00`,
	//   network.c:42767-42808; `pkt_OnMpSkillFxUpdate@0xF440E0`, entity_char.c:53102-53138). Тип 4 — эффект состояния из поля
	//   «навык» у игрока пакета, тип 6 — эффект на своём персонаже. Рассылается при типе 4 и 6; bMp — пакет 4059.
	//   Слушает подсистема состояний (`UAsdaEffectsSubsystem::OnVitalsStateFx`, там правила и ключ `asda.Fx.VitalsStateFx`).
	DECLARE_MULTICAST_DELEGATE_FourParams(FOnAsdaVitalsStateFx, int32 /*userIdx пакета*/, uint8 /*тип обновления*/, int32 /*id состояния*/, bool /*bMp*/);
	FOnAsdaVitalsStateFx OnVitalsStateFx;

	// Записать серверный/локальный снапшот HP. Cur/Max < 0 = «не менять». Src — имя канала для лога.
	void SetVitalsHp(int32 UserIdx, int32 Cur, int32 Max, const TCHAR* Src, bool bServerSnapshot);
	// То же для MP.
	void SetVitalsMp(int32 UserIdx, int32 Cur, int32 Max, const TCHAR* Src, bool bServerSnapshot);
	// ★ Локальное вычитание урона (эталон CCharacter_TakeDamage: HP -= dmg сразу на кадре удара).
	//   Серверный снапшот 4058 остаётся коррекцией — расхождение гасится следующим же пакетом.
	void ApplyVitalsDamage(int32 UserIdx, int32 Dmg, const TCHAR* Src);
	// Витальные значения игрока (nullptr = о нём ещё ничего не известно).
	const FAsdaVitals* GetVitals(int32 UserIdx) const { return VitalsByUserIdx.Find(UserIdx); }
	// Многострочный дамп всех известных витальных значений (консоль asda.Vitals.Dump + лог).
	FString DumpVitals() const;

	// ══════════ ★★★ БОЕВОЙ БЛОК ПЕРСОНАЖА (физ./маг. атака) — `CCharacter+1124..+1191` ═══════════
	// ЧТО ЭТО ЗА ПОЛЯ. 68-байтный «боевой блок» клиентской `CCharacter`; начало +1124 = поле `maxHp`.
	//   Раскладка сверена по ОБОИМ бинарям (`RE/reports/crossref/char_stats.md`):
	//     +1124 d maxHp · +1128 w maxMp · +1130 d curHp · +1134 w curMp ·
	//     +1136 w physAttackMin (сл.568) · +1138 w physAttackMax (сл.569) ·
	//     +1140 w magAttackMin  (сл.570) · +1142 w magAttackMax  (сл.571)
	//   Серверная сторона — ДРУГОЙ layout (`CUser`): +1107 iMaxHP, +1119/+1121
	//   `SetTotalAttackMin/MaxValue`, +1123/+1125 `SetTotalMagicAttackMin/MaxValue`
	//   (`GameServer.c` 25640 / 25740 / 25820 / 25896).
	//   Слова 569 и 571 — ровно те, что берёт отражение урона подтипа 5
	//   (`cclassApplyReflectSkillDamage@0xF8A970`, `game.c:581620/581622`), тик-урон состояний
	//   (`Skill_SetupDotHotTickParams@0xA431B0`) и `charCalcScaledValueByStatRange@0xA89410`
	//   (там же доказан смысл пар: `(a1[568]+a1[569])/2` = физ. атака, `(a1[570]+a1[571])/2` = маг.).
	// ДВА КАНАЛА ДОСТАВКИ. Других писателей этих слов в клиенте НЕТ (сплошной поиск обоих экспортов
	//   по `+ 569)/[569]/+ 1138)` и по всем записям в блок `&pChar->maxHp`):
	//   · **4060** 0xFDC `RecvPacket_CharStatBlockA@0xB93E30` — `memcpy(CCharacter+1124, тело, 0x44)`
	//     БЕЗ userIdx: база берётся как `CClass_GetField142C(&g_InventoryBag)`, т.е. ВСЕГДА локальный
	//     игрок. Сервер шлёт пакет только владельцу (`GameServer.c:113889`,
	//     `sub_4C28F0(..., 4060, 68, a1, v5, a1, 1)` — адресат == владелец).
	//   · **4036** 0xFC4 `Recv_LevelUp@0xF424C0` — `pChar = CharManager_FindByUserIdx(*a1)` и тот же
	//     `qmemcpy(&pChar->maxHp, тело+49, 0x44)`, т.е. ПО userIdx и для ЛЮБОГО персонажа. Сервер
	//     шлёт его владельцу И ВСЕМ СОСЕДЯМ сектора: `GameServer.c:79611-79612`
	//     `sub_4C28F0(...,4036,117,a2,&v50,a2,1)` + `sub_52E6D0(...)` =
	//     `SendPacket_ClippingSectorInOtherUser` (тело одно и то же, 117 б = 49 шапки + 68 блока,
	//     блок скопирован из `CUser+1107`).
	// ⚠ ЧЕГО В КАНАЛАХ НЕТ. Снапшоты появления 4005/5417 (151 б) и 4065/5418 (592 б) боевого блока
	//   не несут: 4005 кладёт поля в staging-запись 0x303, 4065 копируется в `CCharacter+1052..+1123`
	//   и `+1588…` — диапазон +1124..+1191 не затрагивается ни одним из них. Связка ответа на 5416
	//   (`Send_AgainUserInfo@0x41A2E0`: 5417 + 6585 + 5220 + 5418 + 4135 + 4103 + 6722 + 6156 + 6173)
	//   тоже без него. ⇒ сосед, не поднявший уровень у нас на глазах, остаётся без атак-статы И В
	//   ОРИГИНАЛЕ — там эти слова просто нули после `memset(&pChar->maxHp, 0, 0x44)` в конструкторе
	//   (`game.c:79354`), и отражение подтипа 5 по нему даёт 0. Это дефект оригинала, не порта.
	struct FAsdaCombatStats
	{
		int32 PhysAtkMin = 0;   // +1136 (слово 568)
		int32 PhysAtkMax = 0;   // +1138 (слово 569) — отражение подтипа 5 при buff0DamageType == 1
		int32 MagAtkMin  = 0;   // +1140 (слово 570)
		int32 MagAtkMax  = 0;   // +1142 (слово 571) — отражение подтипа 5 при buff0DamageType == 2
		// ★ СТИХИЙНАЯ АТАКА, шесть слов блока +44…+54 = `CCharacter+1168…+1178`
		//   (огонь, вода, земля, ветер, свет, тьма). Читатель в эталоне ровно один — ПАНЕЛЬ ЦЕЛИ:
		//   `HUD_UpdateTargetFrame` (ui_core.c:77526-77541) берёт МАКСИМУМ из шести и зажигает
		//   соответствующую метку `PVP_MARK00..05`. Раньше порт разбирал их только для СВОЕГО
		//   персонажа (`SelfStatBlock::EleAtk`), а панели цели нужен ЧУЖОЙ — тот же блок приезжает
		//   по userIdx в теле 4036 (`Recv_LevelUp`, единственный канал чужих статов).
		int32 Ele[6]     = {0, 0, 0, 0, 0, 0};
		bool  bEleValid  = false;   // блок был длиннее 56 б ⇒ шесть слов реально прочитаны
		FString Src;            // канал последнего обновления («4060» / «4036»)
	};
	// userIdx → боевой блок. Ключ SelfUserNum = свой персонаж.
	TMap<int32, FAsdaCombatStats> CombatStatsByUserIdx;
	// Разобрать 68-байтный боевой блок (Block указывает на его НАЧАЛО = поле maxHp) и положить
	//   в карту по userIdx. Avail — сколько байт реально доступно от Block (защита от короткого тела).
	//   HP/MP из того же блока НЕ трогаем здесь: у них свои каналы-снапшоты (см. вызовы).
	void SetCombatStatsFromBlock(int32 UserIdx, const uint8* Block, int32 Avail, const TCHAR* Src);
	// Боевой блок игрока (nullptr = по нему блока ещё не приходило).
	const FAsdaCombatStats* GetCombatStats(int32 UserIdx) const { return CombatStatsByUserIdx.Find(UserIdx); }

	// ══════ ★★★ ПОЛНЫЙ СТАТ-БЛОК СВОЕГО ПЕРСОНАЖА — питание окна характеристик (CHARAINFO_WIN 900) ══
	// Реверс: `Reports/char_info_window.md` §3. Два серверных блока, оба ТОЛЬКО про своего чара:
	//   • **4060** (68 б) → `CCharacter+1124…+1191` (`RecvPacket_CharStatBlockA@0xB93E30`);
	//   • **4064** (36 б) → `CCharacter+1192…+1227` (`RecvPacket_CharStatBlockB@0xB93F10`).
	// Те же два блока приезжают в теле 4036 (`Recv_LevelUp@0xF424C0`: `+13` 0x24 и `+49` 0x44) —
	//   там уже ПО userIdx, поэтому для своего userIdx мы забираем и их.
	// ⚠ HP/MP отсюда НЕ раздаём: у них свой канал `SetVitalsHp/Mp` (единый источник полос).
	struct FAsdaSelfStatBlock
	{
		// Смещения — от начала 68-байтного блока (см. отчёт §3.2, сборка `sub_43A950@0x43A950`).
		int32 PhysAtkMin = 0;   // +12
		int32 PhysAtkMax = 0;   // +14
		int32 MagAtkMin  = 0;   // +16
		int32 MagAtkMax  = 0;   // +18
		int32 MagDefense = 0;   // +20 — одиночное число (STATUS_ST4)
		int32 DefMin     = 0;   // +22
		int32 DefMax     = 0;   // +24
		int32 BlockMin   = 0;   // +30 — шанс блока (STATUS_ST5)
		int32 BlockMax   = 0;   // +32
		int32 EleAtk[6]  = {0, 0, 0, 0, 0, 0};   // +44…+54 — огонь, вода, земля, воздух, свет, тьма
		int32 EleDef[6]  = {0, 0, 0, 0, 0, 0};   // +56…+66 — те же шесть
		bool  bValid     = false;                // блок хоть раз приходил
	};
	// 36-байтный блок: 18 слов = 6 базовых + 6 прибавок + 6 не читаемых окном
	//   (доказательство пар — серверный `sub_43ACF0@0x43ACF0`: «итог = слово 6 + слово 0»).
	struct FAsdaSelfBaseStats
	{
		int32 Base[6]  = {0, 0, 0, 0, 0, 0};   // Сила, Ловкость, Выносливость, Энергия, Интеллект, Удача
		int32 Bonus[6] = {0, 0, 0, 0, 0, 0};   // прибавка к тем же шести (может быть отрицательной)
		int32 Tail[6]  = {0, 0, 0, 0, 0, 0};   // слова 12…17 — эталонное окно их не читает, храним как есть
		bool  bValid   = false;
	};
	FAsdaSelfStatBlock SelfStatBlock;
	FAsdaSelfBaseStats SelfBaseStats;
	// Сработал любой из двух блоков — окно характеристик перерисовывается целиком
	//   (эталон на каждый пакет зовёт `UI_FillEquipDetailPanel`).
	DECLARE_MULTICAST_DELEGATE(FOnAsdaSelfStatsChanged);
	FOnAsdaSelfStatsChanged OnSelfStatsChanged;
	// Разобрать 68-байтный блок (Block = его начало) в `SelfStatBlock`. Avail — доступно байт.
	void SetSelfStatBlockFrom68(const uint8* Block, int32 Avail, const TCHAR* Src);
	// Разобрать 36-байтный блок (Block = его начало) в `SelfBaseStats`.
	void SetSelfBaseStatsFrom36(const uint8* Block, int32 Avail, const TCHAR* Src);

	// ★ ОЧКИ РЕМЕСЛА (`STATUS_ST12`, эталонный глобал `g_nShopValueA`). Приезжают НЕ стат-блоком, а
	//   пакетом окна ковки (`item_enhance.c:5807` — `filler[152]`) и пакетом склада лавки
	//   (`shopParseStockPacket@…`, `shop_trade.c:22251` — слово 442 записи). −1 = ещё не приходили.
	int32 SelfCraftSkill = -1;

	// ════════════════ ★★★ ВЕС (нагрузка) ЛОКАЛЬНОГО ИГРОКА — MINISTATE_WEIGHTBAR ═════════════════
	// ГДЕ ЖИВЁТ В ОРИГИНАЛЕ: `CCharacter+0x448` (i16) макс. вес, `+0x44A` (i16) текущий, `+0x44C`
	//   (dword) — ГОЛДА (не вес!). Раскладка сверена с сервером: `CUser+416/+418/+420`
	//   (`GameServer.c` sub_43F880 SetCurrentWeightValue / sub_43FB20 GetMaxWeight).
	// ОТКУДА БЕРЁТСЯ БАЗОВЫЙ МАКСИМУМ (реверс 2026-08-06, подтверждено обеими сторонами):
	//   это ПОЛЕ БАЗЫ ДАННЫХ, а не формула. Сервер грузит его блоком 0x45 байт из ответа
	//   DBM_CHARINFO_RES: `charLogOnRes@0x5277D0` → `memcpy(CUser+372, тело+5, 0x45)`; сохраняет
	//   хранимкой `UPMaxWeight %d,%d,%d` (строки в `AstoryServer/_legacy/bytepatches_2026-07/DBManager.exe`), читает `UPCharInfo`.
	//   Ни формулы от силы/уровня, ни константы 15000 в декомпиле сервера НЕТ — 15000 в живом
	//   дампе это значение строки БД. Рантайм-писатель максимума на сервере один — `sub_43FA50`
	//   (предмет со свойством 0x33, потолок 30000), он же шлёт клиенту 2236 и 5451.
	//   В КЛИЕНТ тот же блок приезжает записью спавна: `GameNet_OnUserSpawn@0x9CE750`
	//   (`game_main.c:7013` qmemcpy(v41, запись+27, 0x44)) → `charMgr_createRemoteEntity`
	//   (`entity_char.c:6577` qmemcpy(char+1052, &a3, 0x44)) ⇒ **макс. вес = запись+71 (i16)**,
	//   текущий = +73 (i16), голда = +75 (dword). Это ровно 592-байтная запись 4065/5418.
	// ЧЕСТНО: числового опкода «полный статус при входе» для СВОЕГО персонажа не найдено —
	//   если сервер не прислал ни self-записи 4065, ни 5451, максимум остаётся неизвестным, и
	//   эталон в этом случае просто НИЧЕГО не рисует (`HUD_UpdateMiniWeightBar`: `if (max <= 0) return 0`).
	int32 SelfWeightCur = -1;   // −1 = ещё не приходил
	int32 SelfWeightMax = -1;   // −1 = ещё не приходил (полоса веса не обновляется, как в эталоне)
	FString SelfWeightSrc;      // канал последнего изменения («5013» / «5451 case50» / «4065 self» / «консоль»)
	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnAsdaSelfWeight, int32 /*cur*/, int32 /*max*/);
	FOnAsdaSelfWeight OnSelfWeight;
	// Записать вес. Cur/Max < 0 = «не менять». Всегда шлёт OnSelfWeight (как эталон — он зовёт
	//   HUD_UpdateMiniWeightBar на каждый пакет инвентаря, даже если число не изменилось).
	void SetSelfWeight(int32 Cur, int32 Max, const TCHAR* Src);

	// ★ 19.10.2026 — ОБАЯНИЕ своего персонажа: эталонное поле клиента `g_pGameClient + 770252` (`get_oBC0CC@0xC27CF0`,
	//   он же `sub_931DC6`; писатель `uiSetThousandsFormattedValue@0xD00920`, ui_core.c:46678). Писателей у эталона ДВА:
	//   батч 4048 (`recv_InventoryFullRefresh`, network.c:17311: `CCharacter+0x450 (i32) + CCharacter+0x454 (u8)` = блок 1017
	//   +52 / +56) и строка награды 5042 вид 6 (`chatBuildTransactionNotice`, chat.c:6199-6201: новое значение тела @495).
	//   Читатель в порту — строка «Повышено обаяние» сдачи квеста (разность «новое − текущее»).
	int32 SelfCharm = 0;
	// [ПОРТ] исправление дефекта: эталон на КАЖДОМ 4048 перезаписывает поле устаревшим значением блока 1017 (5042 блок не
	//   обновляет), и следующая награда обаянием показала бы завышенную разность. У нас блок берётся ОДИН раз после 1017.
	bool bSelfCharmFromBlock = false;
	// ★ 19.10.2026 — блок персонажа ЗАВЕРШЕНИЯ квеста по 5052 (`charApplyFullStatusPacket@0xD83670`, entity_char.c:46333-46356):
	//   те же поля, что у 5042 (профессия/класс/подкласс/очки, раскладка 5042 body[10..14] после перевода блока), — та же
	//   функция `ApplyQuestTurnInCharBlock` и то же событие `OnSelfJobChanged`. Зовёт диалог NPC из ветки завершения 5052.
	void ApplyCompletionCharBlock5052(const TArray<uint8>& StatusBlock501);
	// true только на время рассылки `OnSelfJobChanged` из 5052: у этой ветки эталона НЕТ баннера 18500, звука 15 и сброса
	//   дерева навыков (`skillWnd_msgProc(12005)`) — подписчики (баннер, окно навыков) смотрят сюда.
	bool bSelfJobChangeFrom5052 = false;

	// ★ 4206 OP_RECV_USER_BATTLE_STATE — РЕЖИМ PvP (иконка MiniState_PvPMode 5300).
	//   Эталон `netTradeRequestHandler@0xF4A190` (network.c:44087): тело — userIdx i16@0,
	//   dword@8, u8@6, u8@7 (подтип), u8@12 (команда). Команда 1: если @7 > 0 →
	//   `buffFxSlots[160] = 1` + `sub_922DFD(g_pHUD, 1)` (показать иконку) + чат
	//   loc 108114 (или 108131 при @7 == 2); если @7 == 0 → флаг и иконка гасятся,
	//   чат loc 108132. Команда 2 / 0 — только чат про ЦЕЛЬ (loc 108119 / 108120).
	//   bOn относится к игроку UserIdx; мини-статус реагирует только на СВОЙ userIdx.
	DECLARE_MULTICAST_DELEGATE_ThreeParams(FOnAsdaPvpMode, int32 /*userIdx*/, bool /*bOn*/, int32 /*subType*/);
	FOnAsdaPvpMode OnPvpMode;
	// ★ 06.10.2026 (портрет): команды 4206 «0» и «2» — ОТКАЗ смены режима боя (`network.c:44166-44186`).
	//   Эталон: `if (Target_GetSelectedUserIdx(локальный) == *a1)` — миснейм, это userNum ПЕРЕДАННОГО персонажа,
	//   то есть «пакет про меня»; команда 2 → чат loc 108119, команда 0 → loc 108120. [ПРОТОКОЛ] не меняется.
	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnAsdaPvpModeRefused, int32 /*userIdx*/, int32 /*cmd 0|2*/);
	FOnAsdaPvpModeRefused OnPvpModeRefused;
	// ★★★ 17.10.2026 (синхронизация боя, Ф4) — PvP-РЕЖИМ ПО userIdx (ТЗ `Reports/combat_sync_2026-10-17/05_TZ_PVP_FIELD10112.md`).
	//   [ОРИГИНАЛ] 4206 команда 1 (`netTradeRequestHandler@0xF4A190`, network.c:44099–44150) пишет у персонажа userIdx:
	//     `[88]` = байт @6 (у сервера — `[4064]`, «аргумент смены»; его же эталон шлёт обратно при переключении, ui_core.c:32313);
	//     только у СВОЕГО: `[92]` = @7 (режим: бит 1 дуэль, 2 свободный, 4 командный) и флаг `[160]` (@7 > 0 — режим включён);
	//     @7 == 1 → `[80]` = i32 @8 (соперник дуэли), @7 == 4 → `[116]` = i32 @8 (команда); @7 == 0 → `[88]`=0, `[80]`=−1, `[160]`=0.
	//   Порт держит то же по userIdx; запись снимается сбросом сессии и уходом персонажа из видимости (4011 — ★ 18.10.2026, ревизия Ф4
	//   мелочь 6: прежде 4011 запись не снимал, комментарий это обещал зря).
	struct FAsdaPvpState
	{
		uint8 ArgByte = 0;      // @6 (`[88]`) — байт, который клиент шлёт серверу при переключении
		uint8 Mode = 0;         // @7 (`[92]`) — режим 0 / 1 дуэль / 2 свободный / 4 командный (своего — по эталону; чужого — справочно)
		int32 Rival = -1;       // `[80]` — соперник дуэли (@8 при режиме 1), −1 — нет
		int32 Team = -1;        // `[116]` — команда (@8 при режиме 4), −1 — нет
		bool bOn = false;       // `[160]` — флаг PvP-режима (у эталона только свой; гейт 4201/4203 и клика по игроку)
	};
	TMap<int32, FAsdaPvpState> PvpStates;
	// Состояние PvP персонажа (nullptr — пакетов 4206 о нём не было).
	const FAsdaPvpState* GetPvpState(int32 UserIdx) const { return PvpStates.Find(UserIdx); }
	// [ОРИГИНАЛ] флаг `[160]` СВОЕГО персонажа (гейт `+209060 == 1` у 4201/4203, `localPlayer[1].buffFxSlots[160] == 1` у клика).
	bool IsSelfPvpOn() const { const FAsdaPvpState* S = bSelfUserNumKnown ? PvpStates.Find((int32)SelfUserNum) : nullptr; return S && S->bOn; }
	// ★ Ф4: переключатель режима (функция 2/8, `UI_DispatchTargetContextMenu` case 8, ui_core.c:32297–32320): режим включён —
	//   снять флаг локально (нотис 108132, иконка), `[88]` = 0 и 4206; выключен — 4206 с текущим `[88]`. Возврат — пакет ушёл.
	bool TogglePvpMode(const TCHAR* Why);
	// ★ 18.10.2026 (ревизия Ф4, мелочь 7): идёт рассылка `OnPvpMode` ЛОКАЛЬНОГО отбоя (`TogglePvpMode`), а не ответа сервера 4206:
	//   эталон пишет нотис 108132 только в чат (ui_core.c:32302), у ответа сервера — пара «чат + подсказка» (network.c:44113–44115).
	bool bPvpModeLocalBroadcast = false;

	// ★ R1 (10.11.2026): структура `FAsdaPvpAttackPkt` и её разбор вынесены дословно в `Public/Net/AsdaNetWorldTypes.h`.
	//   Имя `UAsdaNetSubsystem::FAsdaPvpAttackPkt` сохранено псевдонимом — тип тот же.
	using FAsdaPvpAttackPkt = ::FAsdaPvpAttackPkt;
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnAsdaPvpAttack, const FAsdaPvpAttackPkt& /*пакет*/);
	FOnAsdaPvpAttack OnPvpAttackStart;   // 4202
	FOnAsdaPvpAttack OnPvpHit;           // 4204
	// ★ 06.10.2026 (волна 2-В): ДУЭЛЬ, четыре приёма S→C одним делегатом «опкод + сырое тело» (разбор целиком —
	//   `UAsdaDuelSubsystem`, там же модель `CDuelMgr`; закон no-reentrant-net-callbacks — здесь только копия байт):
	//   4302 (34 б) — вызов МНЕ (`Recv_PartyMember(g_pQuickSlotBar, 14, …)`, network.c:2092);
	//   4303 (33 б) — итог вызова/ответа (`PvpDuel_OnNoticePacket@0xDE5170`);
	//   4304 (55 б) — результат дуэли (`PvpDuel_ApplyMatchResult@0xDE5560`);
	//   4305 (14 б) — арена создана (`CDuelMgr_CreateDuelArena@0xDE61E0`).
	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnAsdaDuelPacket, int32 /*Opcode*/, const TArray<uint8>& /*Body*/);
	FOnAsdaDuelPacket OnDuelPacket;
	// ★ 06.10.2026 (волна 2-Г): ГОРОД И СВИТКИ — три приёма S→C одним делегатом «опкод + сырое тело» (разбор — в
	//   `UAsdaTownWarSubsystem` и `UAsdaScrollCallSubsystem`; закон no-reentrant-net-callbacks — здесь только копия байт):
	//   6745 (2 б)  — итог смены ставки налога (`CChat_ShowItemSubnotice@0xBCE6B0`, имя IDA врёт; network.c:1081);
	//   6135 (62 б) — итог свитка призыва (`netRecvInventoryItemUpdate@0xDCE180`, network.c:609);
	//   6136 (62 б) — меня зовут свитком призыва (`campWarBuildResultTooltip@0xDCED80`, имя врёт; network.c:612).
	FOnAsdaDuelPacket OnWave2DPacket;
	// ★ 30.10.2026 (выбор армии, кнопка NPC 30056): два приёма S→C одним делегатом «опкод + сырое тело» (разбор — в
	//   `UAsdaFactionSelectSubsystem`; закон no-reentrant-net-callbacks — здесь только длина и копия байт):
	//   6701 (44 б) — сведения войны армий (`Recv_ClanInfo@0xCA3C60`; i16 +19/+21/+23 — численность армий для окна 44000);
	//   6703 (3 б)  — итог выбора армии (`Recv_ClanCreateResult@0xCA3DF0`: u8 код, i16 армия).
	FOnAsdaDuelPacket OnCampWarSquadPacket;
private:
	// Время мира в секундах (0 вне мира) — отметка последнего серверного снапшота HP для дампа.
	double VitalsNowSeconds() const;
public:
	// ── ★ ПОРТАЛ-ХЕНДОФФ (4055 смена карты): после загрузки уровня слать 5058, а не 4001.
	bool  bPortalPending = false;
	// ★★★ 25.10.2026 [ИСПРАВЛЕНИЕ ПОРТА]: снять признаки незавершённого перехода — `bPortalPending`, тайл прибытия
	//   `PortalTileX/Z`, отложенный реконнект `bPendingEnterWorld`. Зовут: разрыв (`HandleConnectionClosed`), полный сброс
	//   сессии и вход из выбора персонажа (`ResetSessionState`), страховка 30 с при мёртвом соединении (`Tick`). Иначе после
	//   перелогина уходил портальный 5058 вместо 4001, и сервер не присылал 5251/6057/6106 (пустые навыки и питомцы).
	//   [ОРИГИНАЛ] признака нет вовсе: портальный вход — отдельная ветка загрузки (game_main.c:3592-3612, шаг 3.0 шлёт 5058),
	//   вход из выбора персонажа — ветка 0xB (4001); после разрыва клиент закрывается. Why — для журнала.
	//   Ключ отката `asda.Net.PortalPendingReset 0`.
	void ClearPortalTransferState(const TCHAR* Why);
	// ★ Идёт смена карты/канала: соединение уже новое, игрок на нём ещё не опознан. На это время
	//   наружу пропускаются только хендшейк 20000 и логоны 5058/4001 — остальное сервер принял бы
	//   за мусор от неизвестного и оборвал бы связь. Снимается при подтверждении входа в мир.
	//   ⚠ Именно «идёт переход», а НЕ «нет игровой сессии»: на экране логина сессии тоже нет,
	//   и по второму условию гейт заблокировал бы сам вход в игру (проверено на живом).
	bool  bTransferInProgress = false;
	float TransferGuardSec = 0.f;   // сколько уже длится переход — страховка от вечного гейта
	// ★ С-Р1 (офлайн-гейт отладочных команд, 26.09.2026): до этого момента (FPlatformTime::Seconds) опкоды из
	//   `DebugSendGuardOpcodes` в живое соединение не уходят. Взводит ТОЛЬКО `FAsdaOutgoingGate::RefuseDebugWhenOnline`
	//   (самотест, запущенный офлайн); 0 — окна нет (обычная игра). ★ Редакция 1: снимается при входе в мир (пакет 1021,
	//   `FAsdaOutgoingGate::ClearDebugSendWindow`); общее и для барьера ремесла (МЕЛК-1).
	double DebugSendGuardUntil = 0.0;
	// ★ С-Р1: опкоды, которые самотест может отправить (объединение по всем командам, взведшим окно).
	TSet<uint16> DebugSendGuardOpcodes;
	// ★ Р-2 (ревизия shopdbl2, 10.10.2026): имя офлайн-самотеста, допущенного `FAsdaOutgoingGate::RefuseDebugWhenOnline`
	//   в этом процессе. Пока не пусто, РАННЕЕ автоподключение к логин-серверу (`AAsdaLoginGameMode::Tick`, ~10 с после
	//   старта) не поднимается: иначе самотест с `delay=N` стартовал бы уже онлайн и слал пакеты на dev-сервер.
	//   Ручной вход (`AAsdaLoginGameMode::DoLogin`) защёлку снимает — решение игрока важнее (Warning в журнал).
	FString OfflineSelfTestLatch;
	// ★ 12.10.2026 (автоподбор графики, UAsdaBenchSubsystem): на время замера первого запуска НИ ОДИН пакет не уходит —
	//   карта замера грузится без сессии, офлайн-спавн героя зовёт боевой путь входа (4122/4124/4301), а соединение с
	//   логин-сервером к этому моменту закрыто. Снимается при возврате на экран логина. Обычная игра его не видит.
	bool bBenchSendVeto = false;
	// ★★ МЕЛК-2 (ревизия shop_offline_fix, 10.10.2026): офлайн-самотесты, допущенные в этом процессе (`RefuseDebugWhenOnline`
	//   → false), с признаком «закрыть процесс при отказе» (ставит `RefuseDeferredStartWhenOnline` по аргументу `quit`).
	//   Ручной вход защёлку снимает, а этот список — нет: событие соединения (`FAsdaOutgoingGate::OnConnectionAppeared`)
	//   по нему прерывает самотесты и очищает его.
	TMap<FString, bool> OfflineSelfTestsAdmitted;
	// ★★ МЕЛК-2: офлайн-самотест прерван появившимся соединением. Пока true (до входа в мир — пакет 1021), сокет
	//   пропускает только опкоды пути входа (`FAsdaOutgoingGate::IsSendVetoedByAbortedSelfTest`).
	bool bOfflineSelfTestAborted = false;
	// ★ Р-2: прогон без сети (`-AsdaNoNet`, см. `FAsdaConnection::IsNoNetRun`) — для кода вне модуля AsdaNet.
	static bool IsNoNetRun();
	// ★ Отложенная загрузка уровня (см. Tick): травел нельзя запускать из колбэка пакета.
	bool  bPendingMapLoad = false;
	int32 PendingMapLoadId = -1;
	int32 PortalTileX = -1, PortalTileZ = -1;
	// C→S 5058 CharPortalLogonReq (45 б) — портальный вход на карту (позиция = тайл из 4055).
	bool SendCharPortalLogonReq();

	// Сохранить раскладку панели быстрого доступа перед переходом (эталон `hotbar_SendAllLayouts`
	// во всех четырёх ветках: портал 5055, призыв 6013, смена канала 6015, короткий варп 6201).
	void FlushHotbarBeforeTransfer(const TCHAR* Reason);

	// Остановить автопуть и движение на время перехода: до логона на новом соединении игрок не
	// опознан, и любой игровой пакет там обрывает связь.
	void StopTravelAndMovementForTransfer(const TCHAR* Reason);

	// ══════ ★★★ ЭМОЦИИ / ДЕЙСТВИЯ ПЕРСОНАЖА — 4024 C→S и 4025 S→C ═══════════════════════════════
	// Реверс: `Reports/char_info_window.md` §7 (`Send_CharStatePos@0x9F9EF0`, network.c:5063;
	//   `Recv_CharSpawnFx@0xF3AF00`, network.c:39532; сервер `CharEMotionSync@0x546730`).
	// C→S 4024 (29 б = MD5(тело13) + тело13):
	//     +0 u16 СВОЙ userIdx (сервер сверяет с отправителем и роняет пакет при несовпадении)
	//     +2 u16 actionType   (сервер принимает ТОЛЬКО 100…400, остальное молча выбрасывает)
	//     +4 u8  паддинг (сервер перезаписывает его своим флагом показа шлема)
	//     +5 f32 X, +9 f32 Z  — НАПРАВЛЕНИЕ ВЗГЛЯДА (единичный курс), НЕ позиция (Y не передаётся).
	//       ★ 27.09.2026 (`Reports/owner_0927/pkt4024_facing.md`): все отправители эталона берут
	//       `effectSlots[112].gap_298+12` (= `CCharacter+47718*4`), которое пишется только вместе с
	//       `charComputeFacingAngleFromDir`. Сервер `sub_546730` при X≠0 и Z≠0 кладёт их в `posWorldX+2`
	//       (миснейм) — это поле курса `CUser`: туда же `CharMove` пишет `sub_523280(cur,dst)` = (dst−cur)/|dst−cur|.
	//       Прежняя строка «сервер обновляет позицию при X>0 и Z>0» была неверна. Брать `GetFacingDirGame()`.
	// Сервер рассылает ту же 13-байтную нагрузку соседям сектора опкодом 4025.
	bool SendCharStatePos4024(int32 ActionType, float FaceDirX, float FaceDirZ);
	// ★ Ревизия six_items СРЕДН-1 (09.10.2026): C→S **5428** `OP_CHAR_E_MOTION_MINING_END` — конец клипа копания своего
	//   персонажа (`Combat_SendSelectTarget@0x9F80F0` — МИСНЕЙМ, зовётся только из конца case 7/10 `CCharacter_UpdateActionState`).
	//   Конверт `SendSecured` (секрет 8 + MD5 16 + тело 3 = 27 б, `CNetwork_Send(…, 27, 5428, 1, 1, 1, 0, 0)` — пад включён);
	//   тело: [0..1] свой userNum (u16, `Target_GetSelectedUserIdx(локальный)`; сервер `CharEMotion_MiningEnd@0x5469C0` сверяет
	//   его с отправителем) · [2] SubMode (эталон всегда 1). Ответ сервера — эхо 5428 (у клиента обработчика нет) и 11/12/38.
	bool SendMiningEnd5428(uint8 SubMode);

	// S→C 4025: userIdx, actionType, X, Z. Разбор actionType — на стороне мира
	//   (`UAsdaWorldSubsystem::OnCharAction`), потому что там живут акторы и клипы.
	DECLARE_MULTICAST_DELEGATE_FourParams(FOnAsdaCharAction, int32 /*userIdx*/, int32 /*actionType*/,
		float /*gameX*/, float /*gameZ*/);
	FOnAsdaCharAction OnCharAction;
	// ★★★ 25.10.2026 [ОРИГИНАЛ] 4025 действие **111** — СМЕНА ПОКАЗА ШЛЕМА персонажа. Сервер (4070 → `sub_52A0C0`, дизасм
	//   :409632) при смене байта `CUser+0x4A6` шлёт соседям 4025 {userIdx, 111, флаг @4} (отправителю — нет). Эталон
	//   `Recv_CharSpawnFx@0xF3AF00` (network.c:39840-39865): байт @4 ≠ 0 → показ, затем `CCharacter_ApplyEquippedHelmet`.
	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnAsdaHelmetShowSync, int32 /*userIdx*/, bool /*bShow*/);
	FOnAsdaHelmetShowSync OnHelmetShowSync;
	// ★ 10.10.2026 (копание): S→C **5430** — ИТОГ КОПКИ (`OP_MINING_RESULT`; до 10.10.2026 назван по вызову внутри — `OP_EFFECT_SPAWN_BY_NAME`,
	//   это миснейм). Эталон — `network.c:1530-1592` (case 0x1536): тело 3 б = [0] итог u8 (1 — удачно, иначе — не удалась),
	//   [1..2] userNum копавшего (i16). Сервер: NpcServer по 5428 (`sub_4586A0` → `sub_426370`: прошло ли время копки
	//   `CollectionTypeAniTime0N` из ini/Collection.ini — 6000/9000 мс, затем шанс предмета) шлёт GameServer'у 5431, тот
	//   (`sub_546C00`, GameServer.c:124903) рассылает 5430 СЕКТОРУ (`sub_52E630`, включая копавшего), при удаче кладёт добычу на
	//   землю у копавшего (`Search_AddMonsterMapItem` → 5005) и начисляет опыт. Разбор (нотис, эффект, звук) делает мир.
	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnAsdaMiningResult, int32 /*result*/, int32 /*userNum*/);
	FOnAsdaMiningResult OnMiningResult;
	// Воскрешение своего персонажа подтверждено сервером (4055/4056 со state != 200) — закрыть меню смерти.
	DECLARE_MULTICAST_DELEGATE(FOnAsdaCharRevived);
	FOnAsdaCharRevived OnCharRevived;
	// ── ★ ПРЕДЛОЖЕНИЕ ВОСКРЕШЕНИЯ ОТ ДРУГОГО ИГРОКА (6093 S→C, сервер sub_52B070): userIdx инициатора,
	//   Type (0 = полное HP/MP, 2 = процент, 3 = 39 %), процент. Клиент добавляет опцию 0/1/2 в меню смерти.
	DECLARE_MULTICAST_DELEGATE_ThreeParams(FOnAsdaResurrectOffer, int32 /*casterUserIdx*/, uint8 /*type*/, int32 /*percent*/);
	FOnAsdaResurrectOffer OnResurrectOffer;
	// C→S 6093: игрок принял предложение (см. реализацию — раскладка 1:1 netSendPkt6093_Target@0xF47090).
	bool SendResurrectConfirm6093(uint8 Type, int32 CasterUserIdx, int32 Percent);
	// ── ★ ЭХО ПРОВЕРКИ СОСТОЯНИЯ (6001, sub_52BFF0): опкод + причина (0=ОК, 1=персонаж мёртв, 2..5=прочее).
	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnAsdaActionBlocked, int32 /*opcode*/, uint8 /*reason*/);
	FOnAsdaActionBlocked OnActionBlocked;
	// ★★ ЕДИНАЯ ТОЧКА ВХОДА В СОСТОЯНИЕ СМЕРТИ (три источника, см. death_revive_system.md §4):
	//     1) 4053 — гибель в процессе игры (клип падения проигрывается);
	//     2) 4003 regenState == 200 — ВХОД В МИР УЖЕ МЁРТВЫМ (умер в прошлой сессии / релог);
	//     3) 6001 причина 1 — сервер отклонил действие «персонаж МЁРТВ», а клиент считал себя живым
	//        (рассинхрон: пропущен 4053 либо мы вошли мёртвыми до готовности павна).
	//   Идемпотентно: повторный вызов при уже взведённом bSelfDead ничего не делает.
	//   bAlreadyDead = true → презентация ставит ПОЗУ трупа без анимации падения.
	void EnterSelfDeadState(const TCHAR* Reason, bool bAlreadyDead);
	// Текущий вызов смерти пришёл «уже мёртвым» (без клипа падения) — читает презентация в OnPlayerCharDeath.
	bool bSelfDeathNoClip = false;
	// Локальное зеркало серверного CUser+79: взводится по 4053 (свой), снимается по 4055/4056 (state != 200).
	//   Публично — чтобы UI/контроллер не слал заведомо отклоняемые действия (подбор/атака/движение).
	bool bSelfDead = false;
	// ── ★ ЭФФЕКТ ПОЯВЛЕНИЯ (spawn_revive_effect.md): 4056 = «прибытие» на карту (вход/ЛЮБОЕ воскрешение:
	//   город CharRegenVillageMove, на месте CharRegenSelfPosition, кампвар) — сервер шлёт self И всем в
	//   секторе (SendPacket_ClippingSectorInOtherUser@GameServer). Эталон-клиент GameNet_OnLocalPlayerMapEntry
	//   @0xF43390: cmchar01 + S_c_entry.wav на userIdx (self-ветка и ветка чужого). 4055 = то же через
	//   перезагрузку карты (эффект в эталоне играет первый кадр мира, CRenderer_RenderWorldFrame@0x9773B0).
	//   bDeadArrival: 4056 state@+20 == 200 → прибыл МЁРТВЫМ (эталон ставит state 29; эффект играет всё равно).
	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnAsdaMapEntryFx, int32 /*userIdx*/, bool /*bDeadArrival*/);
	FOnAsdaMapEntryFx OnMapEntryFx;
	// ★ 07.10.2026 (ревизия 2-В, М-1): то же событие ТОЛЬКО для 4056 (`GameNet_OnLocalPlayerMapEntry@0xF43390`).
	//   `hudShowTargetNameplate` эталона стоит в 4056 и 6025, а в 4055 (`netHandler_EnterWorld@0xF43050`) его нет.
	FOnAsdaMapEntryFx OnMapEntry4056;
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnAsdaEnterWorldFail, int32 /*res*/);
	FOnAsdaEnterWorldFail OnEnterWorldFail;   // 6004 res 0/1/3: нотис loc 1001/302 + возврат char-select (UI)
	// ★★★ A2 (аудит №4 `system_dedup_audit_4.md`): ЧЕТЫРЕ ОЧЕРЕДИ ОКРУЖЕНИЯ — НА ОБЩЕЙ ТОЧКЕ
	//   `TAsdaNetQueue<T>` (`Public/UI/AsdaUiHelpers.h`), а не сырым публичным `TArray`.
	//   ЧЕМ БЫЛО ОПАСНО. Потребитель (`UAsdaWorldSubsystem::ProcessEnvBatches`) складывал в свой
	//   локальный список УКАЗАТЕЛИ `&B` на элементы `PendingMobBatches`/`PendingMobUpdates`, потом
	//   ~300 строк спавнил акторов и только в конце звал `Reset()` на ЧУЖОМ поле. Любой `Add` в это
	//   окно (а спавн актора может дотикать сеть) переаллоцировал массив ⇒ ВИСЯЧИЕ УКАЗАТЕЛИ.
	//   Теперь: колбэк делает `Push` (под замком), потребитель — `Drain(Out)`, который забирает
	//   накопленное `MoveTemp`-ом и чистит очередь ОДНОЙ операцией; дальше работа идёт по СВОЕЙ
	//   копии-значению, и переаллокация очереди на неё не влияет. Закон `no-reentrant-net-callbacks`.
	// Сырые батчи окружения: NPC 4067 (записи 108б), мобы 4013 (шаг записи 174).
	TAsdaNetQueue<TArray<uint8>> PendingNpcBatches, PendingMobBatches;
	// ★ 4019 0xFB3 — ОДИНОЧНЫЙ апдейт моба (Recv_MobSpawnLoop@0xF30330). Раскладка записи та же, что у
	//   4013, но СЕМАНТИКА ДРУГАЯ: для моба позиция из пакета НЕ применяется (только HP + траектория к
	//   dst). Поэтому отдельная очередь — смешивание с 4013 телепортировало мобов в одну точку.
	TAsdaNetQueue<TArray<uint8>> PendingMobUpdates;
	// ★ ДРУГИЕ ИГРОКИ (remote_player_display_re.md + _server_re.md). Два источника одного снапшота:
	//   4005 (0xFA5, 151б wire, AoI-пуш сервера: вход в мир/пересечение сектора/портал/стоп/реген) и
	//   4065 (0xFE1, 592б/запись, батч; = wire-копия снапшота 0x303). Распарсенные копятся здесь,
	//   дренит UAsdaWorldSubsystem (спавн акторов после готовности мира).
	TAsdaNetQueue<FAsdaRemotePlayer> PendingRemotePlayers;
	// ★ 6068 (0x17B4) — ТИТУЛ/РАНГ игрока (эталон guild_title_nameplate_re.md, 14б):
	//   [0..1] userIdx u16, [2..5] charId i32, [6..9] rankValue i32, [10..11] titleId0 i16, [12..13] titleId1 i16.
	//   Гильдия/титул в снапшотах 4005/4065 НЕ приходят — только этим пакетом (и клановыми 4105/4133).
	DECLARE_MULTICAST_DELEGATE_FourParams(FOnAsdaCharTitle, int32 /*userIdx*/, int32 /*titleId0*/, int32 /*titleId1*/, int32 /*rank*/);
	FOnAsdaCharTitle OnCharTitle;
	// ★ 6078 (0x17BE) — обновление ТОЛЬКО ранга (rankValue → бейдж 1668), без титулов
	//   (эталон charNet_UpdateFieldRefreshPlate@0xE9C0F0: [0..1] userIdx, [2..5] charId, [6..9] rankValue).
	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnAsdaCharRank, int32 /*userIdx*/, int32 /*rank*/);
	FOnAsdaCharRank OnCharRank;
	// ═══ ★★★ СЕМЬЯ «ЗВАНИЯ / СЛАВА» (Fame) — окно 18700, разбор `Reports/title_system.md` ═══
	//   Все шесть тел разобраны по эталону; здесь только приём и КОПИЯ тела потребителю
	//   (`UAsdaTitleSubsystem`) — закон `no-reentrant-net-callbacks`.
	//   6065 (0x17B1) `uiTitle_OnJobChangeResult@0xE9A9F0` — ЗВАНИЕ ПОЛУЧЕНО/ПОТЕРЯНО (≥49 б):
	//     [0..1] userIdx i16 · [2..5] charId i32 · [6..7] titleId i16 · [8] flag u8 (0 = получено,
	//     1 = снято) · [9..44] блок 6 стат-троек по 3×i16 · [45..48] очки славы i32.
	//     Эффект над персонажем `0018.jeb`.
	//   6085 (0x17C5) `netHandler_OnGuildTitleResult@0xE9A850` — СЕКРЕТ ЗВАНИЯ ОТКРЫТ (8 б):
	//     [0..3] charId i32 · [4..5] userIdx i16 · [6..7] titleId i16. Эффект `ex_mark_003.jeb`.
	//     ⚠ Клиентское имя «guild title» — МИСНЕЙМ: к кланам ветка отношения не имеет, это
	//     установка бита ВТОРОЙ маски («найдено»), нотис 20501.
	//   6070 (0x17B6) `questTrackBitToggleByUserIdx@0xE9BE80` — ЗВАНИЕ УДАЛЕНО (13 б):
	//     [0..1] userIdx i16 · [2..5] charId i32 · [6] код результата u8 (1 = успех) ·
	//     [7..8] titleId i16 · [9..12] очки славы i32.
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnAsdaTitleEvent, const TArray<uint8>& /*body*/);
	FOnAsdaTitleEvent OnTitleGranted;    // 6065
	FOnAsdaTitleEvent OnTitleFound;      // 6085
	FOnAsdaTitleEvent OnTitleDeleted;    // 6070
	// 6066 (0x17B2) `netOnCharEquipUpdate@0xE9A6F0` — МАСКА ПОЛУЧЕННЫХ званий (42 б):
	//   [0..1] userIdx i16 · [2..5] charId i32 · [6..9] очки славы i32 · [10..41] 256 бит маски.
	// 6086 (0x17C6) `netHandler_OnCharData32@0xE9A7D0` — МАСКА «НАЙДЕНО» (38 б):
	//   [0..3] charId i32 · [4..5] userIdx i16 · [6..37] 256 бит маски. ⚠ ПОРЯДОК ПОЛЕЙ ОБРАТНЫЙ
	//   к 6066 — это не опечатка разбора, так в эталоне.
	// ★ РАСШИРЕНИЕ МАСКИ ДО 512 БИТ (2026-08-29, серверный патч P21). Формат самоописателен
	//   ПО ДЛИНЕ ТЕЛА: старая часть не сдвинута, ещё 32 байта (биты 256…511) дописаны В ХВОСТ.
	//     6066: 42 б (сервер не обновлён) либо 74 б — [42..73] биты 256…511;
	//     6086: 38 б (сервер не обновлён) либо 70 б — [38..69] биты 256…511.
	//   ⚠ Поэтому здесь проверка длины именно «НЕ МЕНЬШЕ» (>= 42 / >= 38), а не «равно»:
	//   тело большей длины — это расширенный формат, его разбирает `UAsdaTitleSubsystem`.
	//   Разбор длины живёт в `UAsdaTitleSubsystem::ParseMaskBody`, сюда тело уходит целиком.
	FOnAsdaTitleEvent OnTitleMaskOwned;  // 6066
	FOnAsdaTitleEvent OnTitleMaskFound;  // 6086
	// 2255 (0x8CF) `charApplyStateFlagDataA@0xE9BFE0` — БЛОКИ СТАТОВ полученных званий, батч:
	//   шапка [0..1] userIdx i16 · [2..5] charId i32, дальше записи по 46 б с СМЕЩЕНИЯ 6:
	//   [0..1] titleId i16 · [2..9] служебные 8 б (клиент не читает) · [10..45] 6 троек по 3×i16
	//   (значение стата = вторая i16 тройки). Пустая тройка = первая i16 равна −1.
	FOnAsdaTitleEvent OnTitleStatBatch;  // 2255
	// ★ КЛАН НА ПЛАШКЕ (guild_title_nameplate_re.md §1.5):
	//   4109 (0x100D, Recv_ClanMemberLeave_Notice@0xBC9940 case 1) — клан ЧУЖОГО игрока по userIdx:
	//     [0] case, [3..4] userIdx i16, [5..6] clanId u16, [7..] clanName sz. ЕДИНСТВЕННЫЙ путь узнать
	//     клан чужого (в снапшотах 4005/4065 клана НЕТ). Сервер шлёт его и при инвайте, и при спавне.
	//   4105 (0x1009, Recv_AuthPassword case 1) — СВОЙ клан: clanId @1556+0, имя @pkt+3, звание @pkt+173.
	//   4133 (0x1025, Recv_AuthRelogin case 2) — выход/роспуск: сброс clanId=-1, имя/звание обнулить.
	DECLARE_MULTICAST_DELEGATE_FourParams(FOnAsdaClanInfo, int32 /*userIdx: -1=self*/, int32 /*clanId: -1=сброс*/,
		const FString& /*clanName*/, const FString& /*clanRank*/);
	FOnAsdaClanInfo OnClanInfo;
	// ★ 6542 (0x198E, partyMemberUpdateHpState@0xBCDC00 — реально «обновление ФЛАГА клана»):
	//   [0..1] clanId u16, [2] flagOn byte, [3..42] 40 байт дизайна флага. Матчит ВСЕ сущности по clanId.
	DECLARE_MULTICAST_DELEGATE_ThreeParams(FOnAsdaClanFlag, int32 /*clanId*/, bool /*flagOn*/, const TArray<uint8>& /*flag40*/);
	FOnAsdaClanFlag OnClanFlag;
	// 4133 case 2 — сброс клана: эталон матчит сущность по charId (+имя персонажа для сверки).
	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnAsdaClanReset, int32 /*charId*/, const FString& /*charName*/);
	FOnAsdaClanReset OnClanReset;
	// ★ 4135 — СНАПШОТ КЛАНА чужого игрока по charId (осн. путь; шлётся в связке 5417/5220/5418/4135):
	//   clanId, имя, звание в клане, флаг вкл + 40 байт дизайна эмблемы.
	DECLARE_MULTICAST_DELEGATE_SixParams(FOnAsdaClanSnapshot, int32 /*charId*/, int32 /*clanId*/,
		const FString& /*clanName*/, const FString& /*clanRank*/, bool /*flagOn*/, const TArray<uint8>& /*flag40*/);
	FOnAsdaClanSnapshot OnClanSnapshot;

	// ═══ ★★★ СОЗДАНИЕ И РОСПУСК КЛАНА — окна `CLAN_MAKING_WIN` (14400, код NPC 30015) и
	//     `CLAN_DELETE_BOX` (14470, код NPC 30016). Разбор — `Reports/npc_repair_variants_30015.md`.
	//
	// ★ C→S 4104 `OP_CLAN_CREATE_REQ` (96 б = 8 секрет + 16 MD5 + 72 тело).
	//   Эталон `netSendPasswordPacket@0xBC8A10` + обнуление `authPasswordCtxCtor@0xBC8C00`:
	//     @0  i32 — эталон кладёт `g_aCharSelectData`; сервер `ClanCreateReq@0x4017B0` берёт тело
	//               с БАЙТА 4 (`clanBody[2]`), поэтому четыре первых байта он НЕ читает;
	//     @4  u8  тип клана — эталон ВСЕГДА 0 (переключатель 14406 мёртв, дефект D1);
	//     @5  17 б имя клана cp1251 (сервер: `memcpy(v14, &clanBody[2]+1, 0x11)`);
	//     @22 4 × u16 = 0 (сервер: `v20[i] = clanBody[i+11]`) — блок эмблемы, при создании нули;
	//     @31…@70 5 × {i32,i32} = 0 (сервер: `v22[2j]/v22[2j+1]`) — звания/права, при создании нули;
	//     @30 = 0, @71 = 0.
	//   Сервер проверяет MD5 ровно по 72 байтам (`sub_523F00(pPacket, 0x48u, …)`).
	bool SendClanCreate4104(uint8 ClanType, const FString& ClanName);

	// ★ C→S 4106 `OP_CLAN_DELETE_REQ` (26 б = 8 + 16 + 2 тело) `netSendAcceptAuth@0xBC9270`:
	//     @0 u16 clanId. Сервер `ClanDeleteReq@0x402090`: MD5 по 2 байтам, затем `clanIndex <= 10000`.
	bool SendClanDelete4106(int32 ClanId);

	// ★ C→S 4138 (28 б = 8 + 16 + 4 тело) — эталон шлёт СРАЗУ после успешного 4105:
	//     @0 i32 userIdx. ⚠ ИМЯ СЕРВЕРНОЙ ФУНКЦИИ ВРЁТ: `ClanMemberLeaveReq@0x403840` никого не
	//   выгоняет — тело зовёт `sub_405560` = разослать СОСТАВ клана. Это «обновить состав».
	bool SendClanRosterRefresh4138();

	// ★ S→C 4105 `OP_AUTH_PASSWORD` (тело 196 б) — РЕЗУЛЬТАТ СОЗДАНИЯ, `Recv_AuthPassword@0xBC8CD0`:
	//     @0 u8 код · @1 u16 clanId · @3 имя клана (20) · @173 звание (9) · @192 i32 новое ЗОЛОТО.
	//   Коды: 1 успех · 0 (12400) · 2 (12402) · 3 (12403) · 4 (12404) · 6 (12406) · 7 (12407) ·
	//         8 (12408) · 9 (12458) · 10 (12168); код 5 клиент игнорирует (`default: return`).
	//   ⚠ Колбэк только КОПИТ (закон `no-reentrant-net-callbacks`).
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnAsdaClanCreateResult, const TArray<uint8>& /*body196*/);
	FOnAsdaClanCreateResult OnClanCreateResult;

	// ★ S→C 4107 `OP_UI_INVEN_NOTICE_BY_CODE` (тело 1 б) — РЕЗУЛЬТАТ РОСПУСКА,
	//   `uiInvenNoticeByCode@0xBC94A0`. Коды: 1 успех (имя клана + 12410) · 0 (12409) ·
	//   2 (12411) · 3 (12412) · 4 (12167). ⚠ Имя приёмника в эталоне врёт — к инвентарю не относится.
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnAsdaClanDisbandResult, const TArray<uint8>& /*body1*/);
	FOnAsdaClanDisbandResult OnClanDisbandResult;

	// ── Зеркала полей `CCharacter`, которые читают гейты кланового окна ──
	//   ★ Смещения сняты ДИЗАССЕМБЛЕРОМ (0xBC2CFC / 0xBC2D3B), а не по именам gap_*.
	/** `CCharacter+0x97B` (char+2427, u8): звание в клане, **1 = глава**. Источники: 4105 код 1
	 *  (эталон пишет 1), 4133 case 2 (пишет 0), 6523 `uiHandleSlotResultPacket@0xBCD0E0`
	 *  (charId @0, звание u16 @7). 0 = не в клане / звание неизвестно. */
	uint8 SelfClanRank = 0;
	/** ★★★ ОБЩИЙ ГЕТТЕР СВОЕГО ЗВАНИЯ В КЛАНЕ (эталон `sub_BC7090@0xBC7090` = «звание == 1»).
	 *  Все гейты клановых окон обязаны спрашивать ЕГО, а не лезть в поле напрямую: писателей у
	 *  `CCharacter+0x97B` ровно три (4105 → 1, 4133 → 0, 6523 → значение), и все три уже сведены
	 *  в это поле. Значения: 0 — не в клане/без прав, **1 = глава**, 2..5 — маски прав из записи
	 *  клана 4123 (смещения 57/59/61/63). Название звания — loc `17046 − звание`.
	 *  ⚠ ДЕФЕКТ ОРИГИНАЛА (доказан полным сканом писателей, `clan_soulmate_gaps.md` §1.6):
	 *    после релогина звание не восстанавливает НИЧТО — сервер 6523 на входе в игру не шлёт.
	 *    Порт закрывает дыру байтом `[33]` СВОЕЙ записи 4125 (см. `UAsdaCommunityClanSubsystem`,
	 *    ключ `asda.Community.Clan.RestoreGradeFrom4125`). */
	uint8 GetSelfClanGrade() const { return SelfClanRank; }
	/** `CCharacter+0x96A` (char+2410): имя своего клана (из 4105 @3, сбрасывается 4133 case 2). */
	FString SelfClanName;
	/** `CCharacter+0x8C1` (char+2241, u8): признак «Друг» (соулмейт). Ненулевой ⇒ клановые
	 *  операции запрещены нотисом 12435. Источник — 6152 `PartyMark_Helper2@0xE64750`, тело @12
	 *  (эталон: `pChar->gap_8BE[3] = *(BYTE*)(a2 + 12)`), применяется по charId. */
	uint8 SelfSoulSilhouette = 0;

	// ★★★ SoulState ЛЮБОГО персонажа (свой и соседи) — `CUser+99027` сервера = `CCharacter+2241`
	//   эталона (`gap_8BE[3]`). Значения = `@State` из `UPLoginGame`: 0 обычный вход, 1 «душа»,
	//   2 ОДЕРЖИМОСТЬ (вход в тело соулмейта). Приходит ДВУМЯ каналами, оба разбираются ниже:
	//     • 6152 (14 б), байт +12 — ключ charId (AoI-бандл сразу после 4005, `sub_422A90`);
	//     • 5220 (записи 101 б), байт +32 — ключ userIdx (`Send_AgainUserInfo`/`Send_SoulMateInfo`).
	//   Доказательства (asm-раскладка стековых аргументов, отладочная строка `connecttype`
	//   самого оригинала) — `Reports/possess_broadcast_soulstate.md` §1-2.
	//   ⚠ Провод НЕ менялся: байт ехал и раньше, порт его просто терял.
	TMap<int32, int32> SoulStateByCharId;    // charId  → SoulState (канал 6152, основной)
	TMap<int32, int32> SoulStateByUserIdx;   // userIdx → SoulState (канал 5220, резервный)
	/** SoulState по charId; −1, если про этого персонажа сервер ещё ничего не присылал. */
	int32 GetSoulStateByCharId(int32 InCharId) const
	{
		const int32* P = SoulStateByCharId.Find(InCharId);
		return P ? *P : -1;
	}
	/** SoulState по userIdx; −1, если про этот слот сервер ещё ничего не присылал. */
	int32 GetSoulStateByUserIdx(int32 InUserIdx) const
	{
		const int32* P = SoulStateByUserIdx.Find(InUserIdx);
		return P ? *P : -1;
	}
	// ★ Отдельные делегаты (а НЕ расширение арности OnSoulmatePeer/OnSoulmateState): на те два
	//   делегата подписаны ещё UAsdaSkillUseSubsystem и UAsdaHotbarSubsystem — смена сигнатуры
	//   сломала бы чужие файлы. Здесь только признак одержимости, по своему ключу у каждого канала.
	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnAsdaPeerSoulStateCharId, int32 /*charId*/, int32 /*soulState*/);
	FOnAsdaPeerSoulStateCharId OnPeerSoulStateByCharId;    // источник 6152
	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnAsdaPeerSoulStateUserIdx, int32 /*userIdx*/, int32 /*soulState*/);
	FOnAsdaPeerSoulStateUserIdx OnPeerSoulStateByUserIdx;  // источник 5220

	// 0xFB5 (4021) набор части экипа / 0xFB7 (4023) сброс: userIdx, partType, itemId (0=снять), grade.
	// ★ 4-й параметр — НЕ «цвет», а УРОВЕНЬ ЗАТОЧКИ: эталон Recv_CharFullSpawn@0xF3E970 (network.c:40631)
	//   пишет body+8 в gap_636[57*slot+16] = инстанс+0x18; сервер CharPutOnEquip@0x4CD730 кладёт туда
	//   grade (GameServer.c:81823). Используется свечением заточенного оружия/щита.
	DECLARE_MULTICAST_DELEGATE_FourParams(FOnAsdaCharPart, int32 /*userIdx*/, int32 /*partType*/, int32 /*itemId*/, uint8 /*grade*/);
	FOnAsdaCharPart OnCharPartUpdate;
	// ★ C→S 5416 (26б = секрет8 + MD5(payload,2)16 + u16 userIdx) — ЗАПРОС снапшота незнакомого игрока
	//   (эталон netSendMaskedHandshake@0x9F95F0: зовётся из Recv_EntityMove, когда движется userIdx,
	//   которого нет в CharManager). Сервер отвечает пачкой 5417(=4005, 151б)+5220+5418(=4065, 592б)+4135
	//   (Send_AgainUserInfo@0x41A2E0) — на нашем сервере это ЕДИНСТВЕННЫЙ путь получить снапшот стоявшего
	//   рядом игрока, если его 4005-пуш мы пропустили (вход раньше нас/другая последовательность).
	bool SendAgainUserInfo(uint16 UserIdx);
	TSet<int32> RequestedUserInfo;   // анти-спам 5416 (сброс записи при спавне/деспавне этого userIdx)
	DECLARE_MULTICAST_DELEGATE(FOnAsdaEnvBatch);
	FOnAsdaEnvBatch OnEnvBatch;         // новый батч окружения добавлен → adoption (UAsdaWorldSubsystem)
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnAsdaEntityDespawn, int32 /*renderId*/);
	FOnAsdaEntityDespawn OnEntityDespawn;   // 4011: сущность ушла (renderId)
	// ★ 5047 (0x13B7) CQuest_OnRecv_StateSync@0xC608E0 — СОСТОЯНИЕ КВЕСТОВ ИГРОКА, 688 байт тела:
	//   12 слотов по 44 б (id@0 «>=2001 занят», номер i16@8, стадия i16@10, 5 целей по 6 б с @14)
	//   + 160 б битовой карты «квест завершён» (эталон CQuest+1140). Сервер шлёт при входе в мир
	//   (после DBM_QUESTWINDOW_INFO_RES, GameServer sub_45C400@0x45C400) и после каждого изменения.
	//   Подписчик — UAsdaQuestMarkerSubsystem (метки «!» над NPC и точки на картах).
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnAsdaQuestStateSync, const TArray<uint8>& /*blob688*/);
	FOnAsdaQuestStateSync OnQuestStateSync;
	// 4015 Recv_CombatHit@0xF310E0 — универсальный ЭКШЕН сущности (мисномер «удар»). Payload:
	//   @0 i16 attacker (игрок; -1 нет), @4 i16 target rid, @6 u8 kind, @7/@9 i16 cur-тайл,
	//   @11/@13 i16 dst-тайл, @15 i16 speedRaw (speed=100/raw), @25 i32 НОВЫЙ HP цели
	//   (эталон: targetEntity[284]=*(pkt+25) БЕЗУСЛОВНО в шапке; state==10 → пакет игнор).
	//   kind: 1=стоп/снап на cur-тайл, 2=движение cur→dst (ОСНОВНОЕ движение мобов), 3=удар
	//   (хит-анимация цели + урон атакующему-игроку @21, позиц-коррекция при расхождении ≥3 тайлов),
	//   5/7=каст/спец. Смерть моба = newHp<=0 (4011 мобов НЕ удаляет — только игроков).
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnAsdaCombatHit, const struct FAsdaCombatHit&);
	FOnAsdaCombatHit OnCombatHit;
	// 4017 Recv_CombatHitAux@0xF33B60: flags=0 → СМЕРТЬ моба rid (основной канал смерти!);
	//   flags&4 → спавн/снап (tile, hp); прочее → HP-апдейт. Hp=-1/Tile=(-1,-1) если тело короткое.
	//   MobType (@0 записи) нужен для ветки flags&4: эталон при отсутствующей сущности СОЗДАЁТ её
	//   (`mobTableFindIndexById(g_pMobMgr, *(__int16*)a3)` → mobListSpawnAndLink), т.е. 4017 — ещё один
	//   полноценный канал спауна моба, а не только апдейт.
	//   ★ 2026-08-11 (mobhit-agent): делегат переведён с пяти голых параметров на структуру
	//   `FAsdaCombatHitAux` — иначе 28 id состояний (@8) и 28 глубин стека (@64) передать некуда,
	//   а без них порт не воспроизводит `Skill_Buff_RefreshStackDepth@0xF84090` (снятие дебаффов моба).
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnAsdaCombatHitAux, const struct FAsdaCombatHitAux&);
	FOnAsdaCombatHitAux OnCombatHitAux;
	// 4027 Recv_CombatAnimState@0xF3EEB0 case 1 (взвод атаки игрока по цели): @1 attackerUserIdx,
	//   @3 targetRid, @13 i32[4] очередь результатов ударов замаха (0=конец; -1 DODGE, -2 MISS,
	//   -3 IMMUNE, >0 урон, бит31=крит). Другие case = отказы (нотисы) — Code!=1 бродкастится с Code.
	// TargetHp — HP цели ПОСЛЕ применения ударов этого пакета (4029 @+4 / 4027 @+9; -1 = нет данных).
	//   По нему определяется, сколько ударов очереди сервер РЕАЛЬНО применил (остальные — след. замаха).
	DECLARE_MULTICAST_DELEGATE_FiveParams(FOnAsdaCombatAnim, uint8 /*case*/, int32 /*attackerUserIdx*/,
		int32 /*targetRid*/, const TArray<int32>& /*dmgQueue*/, int32 /*targetHp*/);
	FOnAsdaCombatAnim OnCombatAnim;
	// ★ 17.10.2026 (синхронизация боя, Ф0) [ОРИГИНАЛ]: опкод пакета, который СЕЙЧАС бродкастит `OnCombatAnim` — 4029
	//   (`Recv_CastState`, ответ на замах) или 4027 (`Recv_CombatAnimState` case 1, первый удар серии); 0 — вне бродкаста.
	//   Эталон по-разному ведёт очередь ударов СОСЕДА: 4027 case 1 всегда сбрасывает и наполняет её заново (network.c:40916–40919,
	//   `LABEL_36`), 4029 — дописывает, пока сосед в серии (network.c:42041–42071). Отдельное поле, а не шестой параметр
	//   делегата: подписчиков у делегата несколько (стенд приёма `AsdaRecvStand.cpp` тоже), их сигнатуры не трогаем.
	uint16 CombatAnimSrcOpcode = 0;
	// ★ 17.10.2026 (синхронизация боя, Ф1, ревизия Ф0 п.2/п.3) [ОРИГИНАЛ]: поля ОТКАЗА 4029, которые СЕЙЧАС бродкастит
	//   `OnCombatRefused` (вне бродкаста — rid −1). Эталон `Recv_CastState` решает код 2 по цели ПАКЕТА (@2 rid → её HP
	//   `target[283]`, network.c:41898/42197) и при живой цели дописывает очередь пакета (@8, network.c:42204–42216). Поля, а не
	//   параметры делегата — сигнатуру делегата держат и другие подписчики (стенд приёма).
	int32 CombatRefusedRid = -1;           // rid цели из пакета отказа (@2)
	int32 CombatRefusedTargetHp = -1;      // HP цели из пакета отказа (@4)
	TArray<int32> CombatRefusedQueue;      // очередь ударов из пакета отказа (@8, непустой префикс)
	// ★★★ 15.10.2026 (волна mobpos_swing): ЖИВАЯ КЛЕТКА ЦЕЛИ из 4027 case 1 (@5 i16 x, @7 i16 z). GameServer
	//   `sub_4C4EF0@0x4C4EF0` (case 0x200A) копирует её из ответа NpcServer 8202 «первый урон» (`Process_AttackToMonster
	//   sub_45FCD0`) — это клетка моба в NpcServer НА МОМЕНТ удара (mob+30360/30364), а не кэш GameServer. Эталон
	//   `Recv_CombatAnimState@0xF3EEB0` её не читает; порт сверяет с ней видимую позицию моба (`UAsdaWorldSubsystem::
	//   CorrectMobToServerCell`). Бродкастится ДО `OnCombatAnim`, чтобы боевой тик увидел уже исправленную позицию.
	DECLARE_MULTICAST_DELEGATE_ThreeParams(FOnAsdaCombatAnimTargetCell, int32 /*attackerUserIdx*/, int32 /*targetRid*/,
		FIntPoint /*liveCell*/);
	FOnAsdaCombatAnimTargetCell OnCombatAnimTargetCell;
	// ★★★ 2026-09-02: ВТОРОЙ канал цифр урона того же замаха — 4029 `@+25`/`@+33` и 4027
	//   `@+29`/`@+37`. Полный разбор и доказательства обеими сторонами — у `FAsdaCombatSideDamage`.
	//   Бродкастится ТОЛЬКО в тех же ветках, где эталон наполняет очереди: удачный замах
	//   (4029 sub == 0 / 4027 case == 1) И принятая очередь уронов — иначе `sub_F40670` не звался бы.
	//   Отдельный делегат, а не пятый параметр `OnCombatAnim`: подписчик боевой анимации живёт в
	//   `AsdaWorldSubsystem`, менять его сигнатуру из сетевого слоя нельзя без правки того файла.
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnAsdaCombatSideDamage, const struct FAsdaCombatSideDamage&);
	FOnAsdaCombatSideDamage OnCombatSideDamage;
	// Разбор второго канала — ОДИН на оба опкода. `OffI16`/`OffI32` — смещения четвёрок в теле
	//   (4029: 25 и 33; 4027: 29 и 37). Тело обязано вмещать `OffI32 + 16` байт, иначе разбора нет.
	void ParseCombatSideDamage(const std::vector<uint8>& Body, int32 OffI16, int32 OffI32,
		int32 Opcode, int32 AttackerUserIdx, int32 TargetRid);

	// ═══════════ ДЛИНЫ СЕТЕВЫХ ЗАПИСЕЙ ФИКСИРОВАННОГО РАЗМЕРА (★ Аудит №3, B12) ═══════════════
	// Шаг записи — это ЧАСТЬ РАСКЛАДКИ ПАКЕТА, а не «просто число цикла»: ошибка в нём сдвигает
	//   ВСЕ поля хвоста и молча портит данные. Поэтому каждая длина названа ровно один раз, рядом
	//   с длиной записи предмета `FAsdaItemInstance::kRecordSize` = 57 (`UI/AsdaInventorySubsystem.h`).
	// Запись результата применения навыка 4102 (`netApplySkillHitResultPacket@0xF4F8A0`):
	//   u8 kind@0, i16 targetIdx@1, i16 skillId@3, i32 damage@5, i32 hpAfter@9, u8 applyFlag@13,
	//   i16 casterIdx@14, u8 stack@16. Раскладка полей — у `FAsdaSkillHitRec` ниже.
	//   ⚠ Ту же длину читает `Skill/AsdaSkillFxSubsystem.cpp` — берёт её ОТСЮДА, своей копии не заводит.
	static constexpr int32 kMobHitRecSize    = 17;
	// Запись состояния каста 5257/5270 — разбирает `ParseSkillCastRecs29` (поля у `FAsdaSkillCastRec`).
	static constexpr int32 kSkillCastRecSize = 29;
	// Запись слота панели быстрого доступа 5059/5067: u8 type@0, u8 subType@1, u16 index@2,
	//   u32 count@4, u16 id@8. Поля разбирает ЕДИНСТВЕННОЕ место — `UAsdaHotbarSubsystem`
	//   (его `kWireSlotBytes` — то же число); сетевой слой страницу только переносит целиком.
	static constexpr int32 kQuickSlotRecSize = 10;
	// Колонок в странице панели (эталон `Hotbar_LoadPage@0xDEA3D0`: 12 обычных + колонка оружия).
	static constexpr int32 kQuickSlotCols    = 13;
	// Полезная часть страницы (без ведущего байта номера страницы) и вся раскладка страницы.
	static constexpr int32 kQuickSlotPageBytes   = kQuickSlotCols * kQuickSlotRecSize;   // 130
	static constexpr int32 kQuickSlotLayoutBytes = 1 + kQuickSlotPageBytes;              // 131

	// ★ R1 (10.11.2026): структура `FAsdaSkillHitRec` и её разбор вынесены дословно в `Public/Net/AsdaNetWorldTypes.h`.
	//   Имя `UAsdaNetSubsystem::FAsdaSkillHitRec` сохранено псевдонимом — тип тот же.
	using FAsdaSkillHitRec = ::FAsdaSkillHitRec;
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnAsdaSkillHit, const FAsdaSkillHitRec& /*rec*/);
	FOnAsdaSkillHit OnSkillHit;
	// ★ Остаточный HP цели из боевого пакета (4029 @+4 / 4027 @+9) — авторитетный источник, гасит
	//   рассинхрон «цифры бегут, HP тает медленно».
	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnAsdaTargetHp, int32 /*targetRid*/, int32 /*hp*/);
	FOnAsdaTargetHp OnTargetHp;
	// Отказ атаки (4029 sub!=0 / 4027 case!=1): эталон сбрасывает атакующего в idle И показывает
	//   WARMESSAGE-строку. Карты «код→loc» у 4027 и 4029 РАЗНЫЕ (пересекающиеся коды!) — поэтому
	//   делегат несёт опкод-источник (4027 или 4029). Карты — AsdaWorldSubsystem::OnCombatRefused.
	DECLARE_MULTICAST_DELEGATE_ThreeParams(FOnAsdaCombatRefused, int32 /*casterUserIdx*/, uint8 /*code*/, uint16 /*srcOpcode*/);
	FOnAsdaCombatRefused OnCombatRefused;
	// 4205 (netTradeCancelResetCharState@0xF4A0D0): сервер сбрасывает боевое состояние —
	//   выход ИЗ БОЕВОЙ СТОЙКИ в мирный покой.
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnAsdaCombatStanceReset, int32 /*userIdx*/);
	FOnAsdaCombatStanceReset OnCombatStanceReset;
	// Сырые батчи инвентаря (модель данных — фаза C UI): 4045(bag)/4048(экип) записи 57б.
	// ★★★ A2 (аудит №4): те же две очереди — тоже на общей точке `TAsdaNetQueue`.
	//   ⚠ ПОБОЧНАЯ НАХОДКА (проверено ДВУМЯ методами — поиском по полному имени и по подстроке
	//   `BagPackets`/`EquipPackets` по всему `Source/` и `Plugins/`): читателя у них НЕТ ВООБЩЕ —
	//   рабочий канал инвентаря это делегат `OnInventoryBatch`. Значит сырые тела копились до конца
	//   сессии (утечка), а счётчик в логе 4046/4049 «получен (N пакетов)» рос от полного обновления
	//   к полному обновлению и показывал не размер батча, а сумму за сессию. Забираем накопленное
	//   `Drain`-ом на терминаторе 4046/4049 — память ограничена одним обновлением, а число в логе
	//   становится правдой (см. `fix-original-defects`: дефект помечен, а не унаследован).
	TAsdaNetQueue<TArray<uint8>> PendingBagPackets, PendingEquipPackets;
	uint8 LastSelectedRawSlot = 0;      // rawSlot из последнего SendSelectChar (для блока 4001)
	// ★ Режим последнего 1016 (`SendSelectChar`): 0 = свой персонаж, 1 = «душа» (свой персонаж
	//   призраком-наблюдателем, включено решением владельца 2026-09-03), 2 = вход телом соулмейта.
	//   Хранится ровно до ответа сервера: 1017 (успех) переносит его в `SelfSoulState`, 1027
	//   (отказ) состояние снимает. Заведён 2026-09-03 по ревизии `review_possess_action_gate.md` п.4 —
	//   до неё признак взводился по факту ОТПРАВКИ и переживал отказ входа.
	uint8 LastSelectMode = 0;

	// ★★★ СВОЙ SoulState — ЕДИНСТВЕННЫЙ ИСТОЧНИК ПРАВДЫ (2026-09-03): 0 = обычный вход,
	//   1 = «душа» (1016 режим 1 → сервер `UPCharSoulState @State=1`, `CharPos.SoulState = 1`),
	//   2 = одержимость (1016 режим 2 → `CharPos.SoulState = 2`). Зеркало байта `CUser+99027`
	//   сервера для СЕБЯ. В оригинале клиент это состояние отдельно НЕ хранит; нужно индикатору
	//   «(Одержим)» над своей плашкой, белому силуэту души и гейтам действий
	//   (`World/AsdaPossessGate.h`: `AsdaPossess` для 2, `AsdaSoul` для 1).
	//   ★★★ КОГДА СТАВИТСЯ (закон `no-state-latch-before-shown`): по УСПЕШНОМУ ответу сервера 1017 —
	//   `SetSelfSoulState(LastSelectMode)`; по отказу 1027 (сервер вход не дал) и в
	//   `ResetSessionState` — сбрасывается в 0. На ОТПРАВКЕ 1016 не взводится: отправили ≠ пустили.
	//   ⚠ ПИСАТЬ ТОЛЬКО через `SetSelfSoulState` — иначе разойдётся с зеркалом `bSelfPossessed` ниже.
	//   ⚠ ОПРОВЕРГНУТО 2026-09-03 (`Reports/possess_broadcast_soulstate.md`): прежняя приписка
	//   здесь утверждала, будто чужой SoulState по проводу не приходит. Он ПРИХОДИТ — каналами 6152
	//   (тело @12) и 5220 (запись @32), см. `SoulStateByCharId`/`SoulStateByUserIdx` выше. Своё поле
	//   остаётся нужным по другой причине: свой режим известен УЖЕ на выборе персонажа, до любого
	//   пакета, и потому главнее сетевого кэша для СВОЕГО персонажа.
	uint8 SelfSoulState = 0;

	// ★ СОВМЕСТИМОЕ ЗЕРКАЛО `SelfSoulState == 2` для существующих читателей по имени поля
	//   (`AsdaWorldSubsystem` ×3, `AsdaPossessGate`). Это НЕ второе состояние: пишется ТОЛЬКО из
	//   `SetSelfSoulState`, прямых присваиваний быть не должно. Новый код пусть читает
	//   `IsSelfPossessed()` / `IsSelfSoul()` / `SelfSoulState`.
	bool bSelfPossessed = false;

	// Единственный сеттер своего SoulState: держит `bSelfPossessed` в согласии с `SelfSoulState`.
	//   Значения вне 0..2 — не с провода (мы сами шлём режим), поэтому трактуются как 0 с предупреждением.
	void SetSelfSoulState(uint8 InState);   // `In`: иначе C4458 — параметр затенял бы член `State`
	// Я вошёл «душой» (SoulState == 1): белый силуэт, серверный гейт `sub_52EEA0`.
	bool IsSelfSoul() const      { return SelfSoulState == 1; }
	// Я вошёл одержимым (SoulState == 2): телом соулмейта, серверный гейт `sub_52EE40`.
	bool IsSelfPossessed() const { return SelfSoulState == 2; }

	// ── ★ ДВИЖЕНИЕ (player_control_move_re.md): 4006 move / 4008 stop; тело 27б + MD5 впереди ──
	// Тело: [0]=type u8, [1]=tgtUserIdx u16, [3]=flag u16 (клиент всегда 2), [5]/[9]=curX/curZ float
	//   (GAME-мир, Y не передаётся), [13]/[17]=dstX/dstZ float, [21]=0 (в ответе сервер кладёт скорость),
	//   [25]=tgtRenderId u16 (-1 нет). Payload = [16б MD5(тело27)][27б тело] (БЕЗ анти-реплей секрета).
	bool SendMove4006(float CurX, float CurZ, float DstX, float DstZ, int32 TgtRenderId = -1);
	// ★ 4008 CharStop. Уходит ТОЛЬКО при взведённом `bActionPending` — обхода у эталона нет
	//   (`Net_SendActionPacket@0x9F9C00`); полное доказательство — в шапке определения (.cpp).
	bool SendStop4008(float CurX, float CurZ);
	// ★ 4008 CharStop от позиции АКТОРА (обычно свой персонаж). Координаты тела 4008 — ИГРОВЫЕ
	//   (gameX, gameZ): эталон кладёт в msg[5]/msg[9] `worldPos` персонажа (network.c:4998). Перевод
	//   UE→игра — только через общую точку `AsdaCoord::UeToGame2D` (тот же, что у `GetGamePosXZ`).
	//   Введено 26.09.2026: три отправителя (крафт, копание, гача) передавали сюда сырые
	//   `GetActorLocation().X/.Y` движка. `Self == nullptr` → (0,0), как было у тех мест раньше.
	bool SendStop4008AtActor(const AActor* Self);

	// ★ АТАКА МОБА (эталон netSabzil_SendActionRaw@0x9FA180, опкод 4026, всего 42б):
	//   [0..7] анти-реплей секрет, [8..23] MD5(payload,18), [24..41] payload 18б:
	//   [0..1] u16 actionType = renderId цели, [2..5] float X, [6..9] float Z, затем ДУБЛЬ X/Z.
	//   Эталон шлёт ПОСЛЕ стопа 4008. Ответ сервера — 4027 (состояние боя).
	//   ★★★ 15.10.2026 (волна mobpos_swing): X/Z — ПОЗИЦИЯ СВОЕГО ПЕРСОНАЖА, не цели (имена параметров `TgtX/TgtY` исторические,
	//   IDA-имя `targetX` у `netSabzil_SendActionRaw` — миснейм). Доказано двумя методами: (1) вызов `Player_UpdateAutoActionState`
	//   (entity_char.c, case 5) передаёт `CObject_GetSubField47649(локальный игрок)` = узел кривой движения СВОЕГО персонажа;
	//   (2) перехват оригинала `Reports/netcap_20260729_combat_shop_death_loot.log`: X/Z в 4026 и 4028 побайтно равны
	//   позиции предшествующего 4008 (`05 d4 28 46 e2 8e bb 46`, `ff 96 22 46 54 25 b5 46`). То же у 4028 (payload [2],[3]).
	bool SendAttackMob4026(int32 TargetRenderId, float TgtX = 0.f, float TgtY = 0.f);
	// ★ Продолжение автоатаки (4028, замахи после первого 4026; перехват: 48б, payload 24б).
	bool SendAttackNext4028(int32 TargetRenderId, float TgtX = 0.f, float TgtY = 0.f);
	// ★★★ 15.10.2026 (ревизия PORT-RHYTHM СРЕДН-2): тот же 4028, но уходит РОВНО в DeadlineSec (FPlatformTime) с потока
	//   точной отправки (`FAsdaConnection::SendPacketAt`), а не в ближайшем кадре игры. Все гейты и барьер серверных часов
	//   проверяются сейчас относительно дедлайна; итог (время ухода) забирает `PollPending4028` в `Tick`. Дедлайн уже
	//   наступил — обычная немедленная отправка. Одновременно поставлен не больше одного удара.
	bool SendAttackNext4028At(int32 TargetRenderId, float TgtX, float TgtY, double DeadlineSec);
	// Поставленный 4028 ещё не ушёл.
	bool HasPending4028() const { return Pending4028Result.IsValid(); }
	// Момент ухода поставленного 4028 — выставлен на время вызова перехватчика стенда (самотест берёт точное время), иначе 0.
	double LastTimedSendAtS = 0.0;
	// ★ Только самотест ритма атаки: заявку навыка 5256/6158 тоже отдавать перехватчику стенда. По умолчанию выкл — у прочих
	//   самотестов с полным перехватом (4007 и др.) заявка навыка офлайн, как и прежде, не уходит (их сценарии на это опираются).
	bool bStandCaptureSkillUse = false;
	// Снять поставленный 4028 (заявка навыка, сброс сессии, разрыв). Уже ушедший — учесть (время ухода — часам).
	void CancelPending4028(const TCHAR* Why);

	// ★★★ 17.10.2026 (Ф4) PvP: АТАКА ИГРОКА 4201 и ДОКЛАД УДАРА 4203 — `netSabzil_SendAction@0x9FA870` (network.c:5230–5377) и
	//   `netSabzil_SendTargetAction4203@0x9FAFC0` (:5394+). Кадр 42 б = секрет 8 + MD5 16 + тело 18 б: @0 i16 userIdx ЦЕЛИ
	//   (`Target_GetSelectedUserIdx(targetChar)` — userNum, НЕ charId: перепутанный номер сервер молча игнорирует), @2/@6 f32 X/Z
	//   СВОЕГО персонажа, @10/@14 — их дубль (сервер читает только @0, GameServer.c:130942). Конверт — как у 4026 (`SendSecured`).
	//   Гейты эталона: флаг PvP-режима `[160]` (4201 — `+209060 == 1`, иначе `CharFx_AutoSabzil_State(…, 20)`); 4203 — ещё и
	//   «обездвижен» (`charHasAnyEquippedItemType` = состояния 0/1/3/4/8/14) и перед ним — стоп 4008 при `g_bActionPending`.
	//   Гейты порта общей точки (`SendSecured`: XyT/одержимость/«душа»/переход) и GM-«скрыться» (4201/4203 в списке).
	//   Возврат false — пакет не ушёл (вызывающий выходит в боевую стойку, как эталон).
	bool SendAttackPlayer4201(int32 TargetUserIdx, float SelfX, float SelfZ);
	bool SendAttackedPlayer4203(int32 TargetUserIdx, float SelfX, float SelfZ);
	// ★ Ф4: 4206 (13 б, `netSendSecuredOp4206@0xF4A580`, кадр 37 б): @0 i16 свой userIdx, @2 i32 свой charId (сервер сверяет с
	//   `CUser+0x16B`), @6/@7 — байт `[88]` (оба), @8..12 нули. Сервер (`Recv_UserBattleState@0x5572D0`, GameServer.c:131868):
	//   (@6 == 0 и режим 2) или (режим 0 и команды нет) — смена режима и 4206 сектору (@12 = 1), иначе 4206 только себе (@12 = 2).
	bool SendBattleState4206(uint8 ArgByte);

	// ★★★ ЗАПРОС HP/MP ЦЕЛИ-ИГРОКА (эталон 6538 `OP_RECV_USER_TARGET_HPMP`; ответ 6539).
	//   Эталон `netSendMaskedPacket_op6538@0xE82420` (`RE/etalon/network.c:30551-30677`):
	//     кадр 36 б = [0..7] анти-реплей секрет · [8..23] MD5(payload,12) · [24..35] payload 12 б:
	//       payload[0..3]  i32 charId    ЦЕЛИ  (`CCharacter+1052`, НЕ userNum!)
	//       payload[4..5]  i16 userNum   ЦЕЛИ  (`CCharacter+190188`)
	//       payload[6..9]  i32 charId    ПРЕЖНЕЙ цели (−1, если её не было)
	//       payload[10..11] i16 userNum  ПРЕЖНЕЙ цели (−1)
	//   Зачем «прежняя»: сервер `Recv_User_TargetHPMP@0x532EC0` (`RE/ida/server_work/GameServer.c:117022`)
	//     читает РОВНО 12 байт с +33 и по ним ведёт список наблюдателей (20 слотов на игрока):
	//     из прежней цели нас ВЫЧЁРКИВАЕТ, в новую ВПИСЫВАЕТ и шлёт `Send_TargetScanUser` → 6539.
	//     Пока мы вписаны, каждое изменение HP цели прилетает нам новым 6539 — это и есть
	//     единственный канал HP чужого игрока в панели цели.
	bool SendTargetInfoReq6538(int32 TargetCharId, int32 TargetUserNum, int32 PrevCharId, int32 PrevUserNum);

	// ══════ ★ КОНТЕКСТНОЕ МЕНЮ ПО ЧУЖОМУ ИГРОКУ (окно TRANTOOL_WIN 2000) ══════════════════════
	//   Полный разбор — `Public/UI/AsdaPlayerMenuSubsystem.h` §3 и
	//   `Reports/player_context_menu.md`. У ВСЕХ шести пакетов шапка одинакова:
	//   [0..7] анти-реплей секрет · [8..23] MD5(payload) · [24..] payload; a6=1 ⇒ bPad=true.
	//   ⚠ ВЕЗДЕ, где параметр назван UserNum — это СЕССИОННЫЙ i16 (`CCharacter+190188`),
	//     а где CharId — это i32 из БД (`CCharacter+1052`). Перепутать = сервер молча промолчит.

	// 2003 «Вступить в клан» → 4108 `partySendInviteAuth@0xBC96F0`, len 26, payload = u16 userNum.
	bool SendClanInvite4108(int32 TargetUserNum);
	// 2055 «Добавить приятеля» → 4150 `netSendGuarded_op4150@0xCE7680`, len 26, payload = u16 userNum.
	bool SendFriendInvite4150(int32 TargetUserNum);
	// 2002 «Предложить Дружбу» (соулмейт) → 5200 `coupleSendProposalReq@0xE658E0`, len 169.
	//   ⚠ ШАПКА ДРУГАЯ: [0..15] MD5(payload,153), payload с [16] — 8-байтного секрета НЕТ.
	//   payload(153): @0 u8 режим · @1 char[20] имя · @21 u16 уровень · @23 u8 пол · @24 u8 класс ·
	//                 @25 char[128] текст заявки (поле окна 8200).
	bool SendSoulmateCreate5200(const FString& TargetName, int32 Mode, int32 MyLevel, int32 MySex,
		int32 MyClass, const FString& Message);
	// ═══ Соулмейт: ядро протокола (отчёт soulmate_6400.md §5.3, конверт A = MD5+payload, паддинг вкл) ═══
	// 5202 ответ на предложение Дружбы (payload 32, size 48). ★ bAccept=true → [0]=1 (кнопка 8408
	//   «Принять», Channel в [10]); false → [0]=0 (8409 «Отказать»). InitCharId (i32@1) и InitUserNum
	//   (i16@5) — идентификаторы инициатора из входящего 5201; сервер требует оба >= 0.
	bool SendSoulmateResponse5202(bool bAccept, int32 InitCharId, int32 InitUserNum,
		uint8 B7, uint8 B8, uint8 B9, uint8 Channel, const FString& InitName, uint8 SkillType);
	// 5204 запрос разрыва (payload 5, size 21). payload: @0 u8=0, @1 i32 номер слота соулмейта.
	bool SendSoulmateBreakReq5204(int32 SlotIndex);
	// ★ Волна 1-А (ревизия §7): 5045 «первая встреча с NPC окончена» (payload 2, size 18): @0 u16 npcId.
	//   Эталон — хвост `UI_resetAllWindowsAndLeave_5045@0xD999AA`; сервер `sub_45C260` (только журнал 2084).
	bool SendNpcFirstMeet5045(int32 NpcId);
	// 5206 ответ на предложение разрыва (payload 31, size 47). Раскладка как у 5202 без байта [31];
	//   [9] инициализируется в −1. bAccept → [0]=1 согласиться разорвать.
	bool SendSoulmateBreakResp5206(bool bAccept, int32 PartnerCharId, int32 PartnerUserNum,
		uint8 B7, uint8 B8, uint8 Channel, const FString& PartnerName);
	// 5221 режимы/галки (payload 4, size 20). Шлётся весь 4-байтовый блок целиком (галки 6543–6546).
	bool SendSoulmateModes5221(uint8 Main, uint8 Mode1, uint8 Mode2, uint8 Mode3);
	// 5483 поиск кандидатов (payload 2, size 26, конверт B). [0]=страница (окно+496), [1]=фильтр (+488).
	bool SendSoulmateSearch5483(uint8 Page, uint8 Filter);
	// 2244 `antiCheat_SendBitMaskCheck` (имя-миснейм: к античиту отношения не имеет) — кнопка 6584
	//   «Информация о предыдущем Друге». Конверт B, payload 0, полный size 24 (токен 8 + MD5 16).
	//   Ответ приходит существующим путём записи соулмейта. Найдено аудитом 31.08.2026 (T3).
	bool SendSoulmateOldPartnerReq2244();
	// 6052 заметка соулмейту (payload 69, size 93, конверт B). @0 i32 partnerId · @4 char[65] текст.
	//   ★ Замена ' → ` перед отправкой (защита от SQL-инъекции оригинала — сохранить). Длина 1..64.
	bool SendSoulmateNote6052(int32 PartnerId, const FString& Text);
	// 6054 подтверждение получения заметки (конверт B).
	bool SendSoulmateNoteConfirm6054(int32 NoteId);
	// 2004/2005 «Обмен …» → 5282 `CExchange_SendRequest/SendAcceptRequest`, len 27.
	//   payload(3) = u16 userNum @0 | u8 режим @2 (0 = обычные предметы, 1 = личные/внешние).
	bool SendExchangeReq5282(int32 TargetUserNum, bool bPersonalItems);
	// Таймаут заявки обмена (33 000 мс) → 5286 `CExchange_SendCancelTimeout@0xC7B5F0`, len 30.
	//   payload(6) = i32 −2 @0 | u16 userNum @4.
	bool SendExchangeCancel5286(int32 TargetUserNum);
	// ★ 29.09.2026 (волна О1 торговли): ответ на заявку обмена → 5284, len 28 — `CExchange_SendWindowReady@0xC7CF60`
	//   (подкод 1 «принять»), `CExchange_SendWindowCancel@0xC7D5F0` (0 «отказ»), `CExchange_SendRequestAck@0xC7DC40` (3 «занят»).
	//   payload(4) = u8 подкод @0 | i16 userNum ИНИЦИАТОРА @1 | u8 тип≠0 @3. Сервер `CharExchangeAcceptReq@0x4DFC80`.
	bool SendExchangeAnswer5284(uint8 SubCode, int32 InitiatorUserNum, bool bPersonalItems);
	// ★ 29.09.2026 (волна О1): закрыть окно обмена → 5286 `CExchange_SendClose@0xC830A0`, len 30.
	//   payload(6) = i32 windowIndex (из 5285) @0 | u16 0 @4. Сервер шлёт партнёру 5287 и освобождает окно (`sub_4DF6D0`).
	bool SendExchangeClose5286(int32 WindowIndex);
	// ★ 29.09.2026 (волна О2 торговли): положить предмет в лоток окна 2008 → 5288, len 36 — `Exchange_SendAddItem@0xC7E3A0`,
	//   `CExchange_SendAddItem2@0xC7EAE0`, `…3@0xC7F1A0`, `…4@0xC7F860`. payload(12) = i32 windowIndex @0 | u8 ячейка лотка @4 |
	//   u8 invType @5 | i16 invSlot @6 | i32 кол-во @8. Сервер `CharExchangeDragDropItem` (case 0x14A8), ответ 5289.
	bool SendExchangeAddItem5288(int32 WindowIndex, int32 TraySlot, int32 InvType, int32 InvSlot, int32 Count);
	// ★ 29.09.2026 (волна О2): снять предмет с лотка → 5290, len 35 — `Exchange_SendRemoveItem@0xC7FF10`, `…2@0xC80600`,
	//   `…3@0xC80CB0`, `…4@0xC81320`. payload(11) = i32 windowIndex @0 | u8 invType @4 | i16 invSlot @5 | i32 кол-во @7.
	//   Сервер `CharExchangeDragDropDeleteItem` (case 0x14AA) ответа не шлёт.
	bool SendExchangeRemoveItem5290(int32 WindowIndex, int32 InvType, int32 InvSlot, int32 Count);
	// ★ 30.09.2026 (волна О3 торговли): фиксация готовности → 5291, len 64 — `Exchange_SendLockToggle@0xC81990`
	//   (shop_trade.c:9781-10083). payload(40) = i32 windowIndex @0 | u8 lock (+0x4C4) @4 | 5 записей по 7 б с @5:
	//   u8 invType +0, i16 invSlot +1, i32 кол-во +3 (пустая ячейка лотка — 0, −1, 0). Сервер `CharExchangeFixReq@0x4E3600`
	//   (case 0x14AB), ответ 5292: партнёру — готовность и 5 записей, мне — только отказ ([4] = 0 деньги / 2 несоответствие).
	bool SendExchangeFix5291(int32 WindowIndex, uint8 Lock, const int32 InvType[5], const int32 InvSlot[5], const int32 Count[5]);
	// ★ 30.09.2026 (волна О3): итоговое подтверждение обмена → 5293, len 28 — `Exchange_SendComplete@0xC82A80`
	//   (в местах вызова `CExchange_SendConfirm`, shop_trade.c:10087-10213). payload(4) = i32 windowIndex @0.
	//   Сервер `CharExchangeCompleteReq@0x4E4720` (case 0x14AD); ответ — итог 5294/5295 (волна О4).
	bool SendExchangeComplete5293(int32 WindowIndex);
	// 2006 «Информация о персонаже» → 5478 `antiCheat_sendItemFlagDigest@0xDA9000`, len 31.
	//   payload(7) = u16 userNum @0 | i32 charId @2 | u8 раса/пол цели (`char+1076`) @6.
	bool SendTargetCharInfo5478(int32 TargetUserNum, int32 TargetCharId, int32 TargetRaceByte);
	// 2070 «Вызвать на поединок» → 4302 `netSendDuelRequest@0xDE4340`, len 58.
	//   payload(34): @0 u16 мой userNum · @2 i32 мой charId · @6 char[20] моё имя ·
	//                @26 u16 userNum цели · @28 i32 charId цели · @32 i16 −1.
	bool SendDuelReq4302(int32 MyUserNum, int32 MyCharId, const FString& MyName,
		int32 TargetUserNum, int32 TargetCharId);
	// ★ 06.10.2026 (волна 2-В, `Reports/owner_1006/wave2C_duel_mark_banner.md`): ОТВЕТ на вызов → 4303
	//   `duelSendReqPkt4303@0xDE4AB0` (combat_pvp.c:6225-6352), len 57 = 8 счётчик + 16 MD5 + 33 тела.
	//   [ПРОТОКОЛ] payload(33): @0 u8 ответ (1 = принять, 0 = отказ) · @1 u16 userNum ВЫЗВАВШЕГО · @3 i32 его charId ·
	//   @7 char[20] нули (имя подставит сервер) · @27 i16 pvpIndex из 4302 @32. Сервер `Recv_UserPvpRes@0x557F10`.
	bool SendDuelAnswer4303(uint8 Answer, int32 ChallengerUserNum, int32 ChallengerCharId, int32 PvpIndex);
	// ★ 26.10.2026 (bowfx) [ОРИГИНАЛ] C→S 8226 — «моб умер от моего урона по времени» (`netSendSoulmateSkillReq@0xA07260` — МИСНЕЙМ):
	//   payload 4 б `[i16 свой userNum][i16 renderId моба]`, конверт MD5 без секрета (`CNetwork_Send(…, 20, 8226, 1, 1, 1, 0, 0)`).
	//   Сервер: GameServer `sub_428310` (MD5, a4=0, payload @+25) → NpcServer `Process_ClientMonsterDead@0x45AD10` (смерть при HP <= 0).
	bool SendDotDeadClaim8226(int32 MobRid);
	// ★ 29.10.2026 (Charge 537/597) [ОРИГИНАЛ] C→S 8215 `OP_CHAR_UPDATE_DASH` ветки моба тика рывка `Char_UpdateDash@0xB3B010`
	//   (entity_char.c:42617-42640): тело 17 б `[0]=1 [1..2]=userNum кастера [3..6]=он же i32 (Target_GetSelectedUserIdx — МИСНЕЙМ)
	//   [7..8]=renderId цели [9..10]=537 (зашит в коде и для 597) [11]=1 [12]=0 [13..14]/[15..16]=тайл X/Z новой точки моба`,
	//   конверт MD5 без секрета (`CNetwork_Send(…, 33, 8215, 1, 1, 1, 0, 0)`). Сервер: GameServer `sub_428020@0x428020` → NpcServer
	//   `sub_45A150` → `sub_436E90` (моб на клетку, таймер «SkillPain»); пакетов в ответ нет.
	bool SendDash8215(int32 CasterUserNum, int32 MobRid, int32 SkillCode, int32 TileX, int32 TileZ);
	// ★ 05.11.2026 [ОРИГИНАЛ] C→S 8215 ВИД 0 — ПОДТВЕРЖДЕНИЕ ПОПАДАНИЯ навыка по мобу: `netSendSkillCastGroundTarget(userNum, charId, renderId,
	//   навык, 0)@0xA040E0`. Эталон шлёт его из ~40 путей показа удара СВОЕГО кастера (бит 0 флагов записи, `pChar+190260`): тики навыков,
	//   прилёт носителя `CEffect_UpdateSkillCast`, обработчик записи вида 13 (граната). Сервер: GameServer `sub_428020` → NpcServer
	//   `Process_CollisionCheck` (`sub_45A150`) → ветка 0 `CounterAttack()` (`sub_43C830`): СПИСЫВАЕТ ОТЛОЖЕННЫЙ урон навыка (+34376,
	//   флаг +34370) и ставит моба на клетку @13/@15. Без этого пакета у моба, не втянутого в бой (OnHate не поднял +34369), урон висит
	//   вечно — клиент показал смерть по «HP после = 0», а моб жив: «зомби» после площадной гранаты (журналы игроков 26.10–02.11).
	//   Тело то же, что у рывка, `[0] = 0`; клетка — по `behaviorType` навыка (см. вызывающего).
	bool SendSkillHit8215(int32 CasterUserNum, int32 MobRid, int32 SkillCode, int32 TileX, int32 TileZ);
	// ★ 29.10.2026 (Charge) [ОРИГИНАЛ] C→S 4008 вид 15 `Send_CharSkillAction@0x9F9D80` (network.c:5033): точка персонажа по прибытии
	//   рывка — тело 27 б как у 4008 (`[0]=15`, свой userNum, cur=dst=точка, `[25..26]=−1`), MD5 без секрета. БЕЗ гейта
	//   `g_bActionPending` и без его изменения. Сервер `CharStop@0x545C70`: при состоянии 177 позиция принимается без проверки
	//   дальности, рассылка 4009 себе и соседям.
	bool SendSkillActionStop4008K15(float CurX, float CurZ);
	// 2063 «Просмотреть блог» (кнопка мертва, ветка кода жива) → 6082 `netSendReqStr_op6082@0xFBF9B0`,
	//   len 44, payload = char[20] имя цели.
	bool SendGetUserId6082(const FString& TargetName);
	// Общая сборка «секрет8 + MD5(payload) + payload» для пакетов меню (кроме 5200 — у него своя шапка).
	bool SendMenuPacket(uint16 Opcode, const uint8* Payload, int32 PayloadLen, const TCHAR* What);

	// ★ ОТВЕТЫ на пакеты меню по игроку. Один делегат на все пять опкодов: тела разные, но
	//   потребитель один (UAsdaPlayerMenuSubsystem), а окна-приёмники (обмен 2008, инфо 4200,
	//   соулмейт 8200) ещё не портированы — заводить пять узких делегатов было бы задел «в стол».
	//     4151 друзья · 5283 заявка обмена · 5479 сведения о персонаже · 5201 заявка соулмейта ·
	//     6083 логин витрины (ответ на 6082).
	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnAsdaPlayerMenuResult, int32 /*opcode*/, const TArray<uint8>& /*body*/);
	FOnAsdaPlayerMenuResult OnPlayerMenuResult;

	// ══════ ★ ОТРЯД: окна PARTYTAKE_WIN 2250 и PARTYBOX_WIN 2245 ═══════════════════════════════
	//   Полный разбор — `Public/UI/AsdaPartySubsystem.h` и `Reports/party_invite_2250.md`.
	//
	//   ★★★ ШАПКА У ВСЕЙ СЕМЬИ ОТРЯДА — БЕЗ 8-БАЙТНОГО СЕКРЕТА: тело = [0..15] MD5(payload) +
	//   [16..] payload. Двустороннее доказательство: клиент `Party_Send_NewPartyReq@0xF38780`
	//   (`social.c:9315`) кладёт MD5 в [0..15] и payload в [16..], len 19 при payload 3;
	//   сервер `sub_523F00@0x523F00` при `a4 == 0` считает MD5 от `pkt+25` и сверяет с `pkt+9`
	//   (разница ровно 16). Ветка `a4 == 1` — это конверт `SendSecured`, party её не использует.
	//   ⚠ Поэтому здесь НЕЛЬЗЯ звать `SendSecured`: сервер отобьёт пакет на сверке MD5.
	//   У всех сборщиков `CNetwork_Send(…, 1, 1, 1, 0, 0)` ⇒ a6 = 1 ⇒ ПАДДИНГ ЕСТЬ (bPad = true).
	//   ⚠ Во ВСЕХ четырёх исходящих идёт ТОЛЬКО userNum (i16), charId — никогда.

	// 5090 `Party_Send_NewPartyReq@0xF38780`, len 19: payload(3) = u16 userNum @0 | u8 правило @2.
	//   Правило дележа: 0 = «кто успел» (loc 12153), 1 = «автораспределение» (loc 12154).
	bool SendPartyNewReq5090(int32 TargetUserNum, uint8 LootRule);
	// 5094 `Party_Send_JoinReq@0xF390B0`, len 18: payload(2) = u16 userNum @0. Шлёт только ГЛАВА.
	bool SendPartyJoinReq5094(int32 TargetUserNum);
	// 5092 `Party_Send_NewPartyAccept@0xF38CC0`, len 19: payload(3) = u8 код @0 | i16 partyIndex @1.
	//   Коды эталона: 1 принять · 0 отказ/таймаут · 5 «я занят своей заявкой» · 6 «висит другая заявка».
	bool SendPartyNewAccept5092(uint8 Code, int32 PartyIndex);
	// 5096 `Party_Send_InviteAccept@0xF395B0`, len 19: payload(3) = u8 код @0 | i16 partyIndex @1.
	//   Коды эталона: 1 принять · 0 отказ/таймаут · 6 и 7 — «занят» (порядок аргументов обратный 5092!).
	bool SendPartyInviteAccept5096(uint8 Code, int32 PartyIndex);
	// 5098 `Party_Send_InviteByName@0xF39AE0`, len 39 (16 + 23): payload = i16 partyIndex @0 |
	//   u8 режим @2 | char[20] имя @3. Кнопка 2260 «Выход» шлёт режим 0 со СВОИМ именем.
	bool SendPartyLeave5098(int32 PartyIndex, uint8 Mode, const FString& Name);
	// 5100 `Party_Send_BanishByName@0xF39F80`, len 42 (16 + 26): payload = i32 charId @0 |
	//   i16 partyIndex @4 | char[20] имя @6. Шлёт только глава (кнопка 2262 «Выгнать»).
	bool SendPartyBanish5100(int32 TargetCharId, int32 PartyIndex, const FString& Name);

	// ★ ЗАЯВКА В ОТРЯД: 5091 (новый отряд) и 5095 (вступление в существующий). Тела 30 б,
	//   РАСКЛАДКА ОДИНАКОВА (сервер собирает их одним и тем же кодом — `Recv_RequestUser_NewPartyCreate`
	//   и `recvRequestUserInviteParty`): @0 u8 код · @1 i16 userNum · @3 i32 charId ·
	//   @7 char[20] имя · @27 u8 правило дележа · @28 i16 reqId(partyIndex).
	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnAsdaPartyRequest, int32 /*opcode*/, const TArray<uint8>& /*body30*/);
	FOnAsdaPartyRequest OnPartyRequest;

	// ★ РЕЗУЛЬТАТЫ И СОБЫТИЯ ОТРЯДА: 5093 (15 б), 5097 (29 б), 5099, 5101, 5102, 5103,
	//   ★ 5104 (12 б, позиция соотрядника — волна 1-Б), 5107, 5108, 5109.
	//   Разбирает `UAsdaPartySubsystem` — тела разные, потребитель один.
	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnAsdaPartyResult, int32 /*opcode*/, const TArray<uint8>& /*body*/);
	FOnAsdaPartyResult OnPartyResult;

	// ★ ЯБЛОКО ДРУЖБЫ (волна 1-Б, 06.10.2026): 6049 (рост яблока, 7 б) и 6051 («яблоко съедено», 14 б) —
	//   панель 6689…6699 окна соулмейта 6500. Разбирает `UAsdaSoulAppleSubsystem` (тела разные).
	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnAsdaSoulApplePacket, int32 /*opcode*/, const TArray<uint8>& /*body*/);
	FOnAsdaSoulApplePacket OnSoulApplePacket;
	// ★ Ревизия 1-Б С-1: 5226 (0x146A) `tradeWnd_resetAndNotify@0xE67A70` — «Друг уходит» (миснейм; тело эталон не читает).
	FSimpleMulticastDelegate OnSoulmatePartnerLeft;

	// ── ★ ЛУТ (реверс Reports/loot_system_re.md) ─────────────────────────────
	// ★ R1 (10.11.2026): структура `FAsdaLootDrop` и её разбор вынесены дословно в `Public/Net/AsdaNetWorldTypes.h`.
	//   Имя `UAsdaNetSubsystem::FAsdaLootDrop` сохранено псевдонимом — тип тот же.
	using FAsdaLootDrop = ::FAsdaLootDrop;
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnAsdaLootSpawn, const TArray<FAsdaLootDrop>&);
	FOnAsdaLootSpawn OnLootSpawn;            // 5005: батч предметов на земле
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnAsdaLootRemove, FIntPoint /*tile*/);
	FOnAsdaLootRemove OnLootRemove;          // 5015: мгновенное удаление предмета с тайла (без фейда)
	// 5013 результат подбора: result @0 (1=ok, 0/3=нотис 1012, 2=инвентарь полон 1011, 4=чужой 1030,
	//   5=фантом → удалить биллборд по тайлам @8/@10), picker i16@1, запись предмета 57б @3
	//   (itemCode i32@3, кол-во i32@12), новый вес i16@60.
	DECLARE_MULTICAST_DELEGATE_FourParams(FOnAsdaLootPickupResult, uint8 /*result*/, int32 /*pickerUserNum*/,
		const TArray<uint8>& /*body62*/, int32 /*newWeight*/);
	FOnAsdaLootPickupResult OnLootPickupResult;
	// 5017 «подбор по тайлу» (сервер указывает предмет): result u8@0 (1=ok), itemCode i32@1, тайлы i16@6/@8 —
	//   клиент подходит к тайлу и шлёт 5012 (тот же флоу, что клик).
	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnAsdaLootPickHint, uint8 /*result*/, FIntPoint /*tile*/);
	FOnAsdaLootPickHint OnLootPickHint;
	// 5012 подбор (SendItemPickPacket@0x9FCFB0): 32б = [0..7] анти-реплей токен + [8..23] MD5(payload 8) +
	//   [24..31] payload 4×i16 (charTileX, charTileZ, itemTileX, itemTileZ). Троттла нет (гейт — state подбора).
	bool SendItemPick5012(const FIntPoint& CharTile, const FIntPoint& ItemTile);
	// ★ 09.10.2026 (автолопата): признак «5012 ушёл, ответа ещё нет» — эталонное поле `g_pUIManager+0x58` (`get_o16`/`set_o16`).
	//   Ставит `SendItemPickPacket@0x9FCFB0` (`sub_927560(g_pUIManager, 1)` после `CNetwork_Send`), снимают
	//   `Net_HandleItemPickupResult@0xF45420` (5013) и `billboardRemoveByKeyClearTarget@0xF45350` (5015) — оба безусловно, первой
	//   строкой. Читает его цикл автолопаты (game_main.c:2278: подбор и счёт только при `!get_o16`). Сбрасывается сбросом сессии.
	bool bItemPickPending = false;
	// ★ 09.10.2026 (МЕЛК-1): метка отправки 5012 (мс, часы `MoveClockMs`) — для сторожа [ПОРТ] `asda.Loot.PickPendingGuardSec`.
	double ItemPickStampMs = 0.0;
	// ★ 09.10.2026 (МЕЛК-1): взведён ли «подбор 5012 в пути» с учётом сторожа [ПОРТ] (снятие сторожем — в лог).
	//   Гейт эталона `sub_92101B` читают через него: `SendItemPick5012` и цикл автолопаты.
	bool IsItemPickPending();
	// ═══════════ ПАНЕЛЬ БЫСТРОГО ДОСТУПА (QUICK_NEW_WIN) — реверс quick_win_re_B.md ═══════════
	// ★ 5059 (0x13C3) S→C — сохранённая раскладка ОДНОЙ страницы квикслотов (`Hotbar_LoadPage@0xDEA3D0`).
	//   Тело 131 б: @0 u8 page, далее 13 записей по 10 б (Type, SubType, u16 Index, u32 Count, u16 Id).
	//   Ни MD5, ни анти-реплей-токена в S→C НЕТ. Приходит ПО ОДНОЙ странице (сервер шлёт по одному
	//   пакету на каждый ответ БД `DBM_QUICKSLOT_LOAD_RES` 0x82E), идентификатора персонажа в теле нет —
	//   адресация чисто по userIndex соединения.
	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnAsdaHotbarPage, uint8 /*page*/, const TArray<uint8>& /*slots130*/);
	FOnAsdaHotbarPage OnHotbarPage;

	// ★ 5067 (0x13CB) C→S — сохранить ОДНУ страницу (`Hotbar_SendLayout@0xDF2070`). Тело 155 б:
	//   [0..7] анти-реплей токен, [8..23] MD5(layout131), [24..154] layout = u8 page + 13×10 б.
	//   Ответа сервер НЕ шлёт вовсе; отказ виден только в его логе («Secret Error»).
	//   Slots130 — ровно 130 байт (13 записей по 10).
	bool SendHotbarLayout5067(uint8 Page, const TArray<uint8>& Slots130);
	// ★ Аудит №3 (B12): геометрия страницы 5059 вынесена из тела обработчика в именованную функцию —
	//   по образцу `ParseSkillCastRecs29`. `Data`/`Len` — тело пакета целиком (включая байт номера
	//   страницы @0). `OutSlots` всегда ровно `kQuickSlotPageBytes`, хвост короткого тела — нули.
	//   Возвращает число РЕАЛЬНО пришедших байт слотов (для диагностики короткого тела).
	static int32 ParseQuickSlotPage10(const uint8* Data, int32 Len, TArray<uint8>& OutSlots);

	// ═════════════════════ ИНВЕНТАРЬ (реверс `Reports/inventory_re_B.md`) ═════════════════════
	// ★★★ ПЯТЬ КОНТЕЙНЕРОВ ОРИГИНАЛА (роутер сервера `sub_4D61E0@0x4D61E0`) — числа протокола:
	//     1 = ShellInven         30 ячеек × 57 б  (снаряжение / квестовые; НЕ стекуется)
	//     2 = SpendCellobInven   60 ячеек × 57 б  (расходники / самоцветы / ЗОЛОТО itemCode 20551)
	//     3 = надетое            11 ячеек × 57 б  (+9 «аватар» 11..19 — в оригинале без UI)
	//     4 = слоты КВЕСТОВ      12 × 44 б        — ★ НЕ инвентарь предметов
	//     5 = склад              до 270 × 57 б    (страницами по 30, ёмкость = CUser+430)
	//   ⚠ Клиентский символ `g_InventoryBag` = контейнер 1, а НЕ «сумка расходников».
	//
	// ★ Батчи содержимого при входе в мир (S→C, нарезка «до 11 записей = 627 б», затем НУЛЕВОЙ
	//   пакет-маркер конца): 4045→4046 контейнер 1, 4048→4049 контейнер 2, 4051→4052 склад.
	//   Колбэк только КОПИТ (закон `no-reentrant-net-callbacks`) — применяет подписчик в своём Tick.
	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnAsdaInvBatch, int32 /*Container 1/2/5*/, const TArray<uint8>& /*RawN×57*/);
	FOnAsdaInvBatch OnInventoryBatch;
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnAsdaInvBatchEnd, int32 /*Container 1/2/5*/);
	FOnAsdaInvBatchEnd OnInventoryBatchEnd;

	// ★ 1027/0x403 — ПОЛНЫЕ 57-байтовые записи НАДЕТОГО (11 × 57 = 627 б, индекс = экип-слот).
	//   Единственный источник контейнера 3 при входе в мир (batch 4048 — это контейнер 2!).
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnAsdaEquipFull, const TArray<uint8>& /*Raw11x57*/);
	FOnAsdaEquipFull OnEquipFull;

	// ★ 5001 (24 б) — РЕЗУЛЬТАТ перемещения/экипа/снятия (`sub_4D12F0` после ответа БД 2018):
	//   @0 u8 result (1 = применить локально), @1 u8 subKind (1 = обмен местами, 2 = переезд),
	//   @2..12 src item-ref 11 б (slot@2 i16, type@6 u8), @13..23 dst item-ref (slot@13, type@17).
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnAsdaItemMoveResult, const TArray<uint8>& /*body24*/);
	FOnAsdaItemMoveResult OnItemMoveResult;

	// ★ 5009 (113 б) — результат 5008 «положить на склад» (`sendCharItemKeeping@0x50ACD0`):
	//     `@0 u8 result`, `@1…55` 5×11 записей ИНВЕНТАРЯ, `@56…110` 5×11 записей СКЛАДА (тип 5),
	//     `@111 i16` новый вес персонажа.
	//   Коды: 0 общий отказ · 1 УСПЕХ · 2 склад полон · 3 гейт · 4 · 5 анти-дублирование ·
	//         6 попытка положить ЗОЛОТО (20551) · 7 вес стека ≥ 30000.
	// ★ 5011 (117 б) — результат 5010 «забрать со склада» (`sendCharItemRetake@0x50CA80`):
	//     та же раскладка, но `@111 i32` — НОВАЯ СУММА ЗОЛОТА (списана комиссия), `@115 i16` вес.
	//   Коды: 0 общий отказ · 1 УСПЕХ · 2 инвентарь полон · 3 перевес · 4 гейт · 5 не хватает денег.
	//   `bKeeping` = true для 5009 (положить), false для 5011 (забрать).
	DECLARE_MULTICAST_DELEGATE_ThreeParams(FOnAsdaWarehouseResult,
		bool /*bKeeping*/, uint8 /*result*/, const TArray<uint8>& /*body*/);
	FOnAsdaWarehouseResult OnWarehouseResult;

	// ★ 5003 (14 б) `Recv_ItemEquipStat@0xD0F350` — изменение слота / количества / ДЕНЕГ:
	//   @0 u8 result, @1 i16 slotIndex, @5 u8 containerType, @6 i32 newAmount,
	//   @10 i16 newStackWeight, @12 i16 newCurWeight.
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnAsdaItemSlotUpdate, const TArray<uint8>& /*body14*/);
	FOnAsdaItemSlotUpdate OnItemSlotUpdate;

	// ★ 4999 (59 б) `recvInventoryRemoveSlot@0x998BC0` — слот очищен: запись 57 б + i16 вес @57.
	//   ⚠ Отправителя в срезе GameServer.exe НЕТ (см. отчёт §9.1) — путь есть, живого трафика может не быть.
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnAsdaItemSlotRemoved, const TArray<uint8>& /*body59*/);
	FOnAsdaItemSlotRemoved OnItemSlotRemoved;

	// ★ 5029 (24 б) `OnRecv_UseItemResult@0xD0B1E0` — результат использования расходника:
	//   @0 u8 result (1 = успех), @1 i16 targetUserIdx, @7 i32 itemCode, @11 i16 slotIndex
	//   (ВСЕГДА контейнер 2), @16 i32 newAmount, @18 i16 newCurWeight, @20 i16 newStackWeight.
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnAsdaItemUseResult, const TArray<uint8>& /*body24*/);
	FOnAsdaItemUseResult OnItemUseResult;

	// ★ 5007 (18 б) — результат уничтожения предмета (`CharItemDump`): @0 u8 result (2 = «занят»).
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnAsdaItemDumpResult, const TArray<uint8>& /*body18*/);
	FOnAsdaItemDumpResult OnItemDumpResult;

	// ★★★ 5000 C→S ОБЩИЙ ОТПРАВИТЕЛЬ (`CharItemMove@0x4CE3E0`; 46 б = 8 токен + 16 MD5 + 22 payload).
	//   Payload = ДВА 11-байтовых item-ref, СОБРАННЫХ ИЗ ПОЛЕЙ ЗАПИСИ (индекса ячейки в пакете НЕТ!):
	//     @0  i16 srcSlot      = запись+5      @11 i16 dstSlot   (0 = сервер подберёт сам)
	//     @2  i16 srcSecond    = запись+7      @13 i16 (не заполняется)
	//     @4  u8  srcType      = запись+4      @15 u8  dstType   (контейнер назначения)
	//     @5  i32 srcAmount    = запись+9      @16..21 нули
	//     @9  i16 srcWeight    = запись+14
	//   Смысл пары (srcType→dstType): 1→1/2→2/5→5 перестановка, 1→3/2→3 НАДЕТЬ (dstSlot = itemType),
	//   3→1/3→2 СНЯТЬ (dstSlot игнорируется), 4→4 слоты квестов. 1↔2 сервером ЗАПРЕЩЕНО.
	//   ★ Клиент НЕ применяет перемещение оптимистично — ждёт 5001 result=1.
	bool SendItemMove5000(const TArray<uint8>& SrcEntry57, int32 DstSlot, int32 DstType);

	// ★ 5028 C→S (29 б = 24 + 5) `CharSpendItemUse@0x4D8C40` — ИСПОЛЬЗОВАТЬ РАСХОДНИК:
	//   @0 u8 containerType, @1 i16 slotIndex, @3 i16 targetUserIdx (заполняется только при itemSubType==15).
	bool SendSpendItemUse5028(int32 ContainerType, int32 SlotIndex, int32 TargetUserIdx = -1);

	// ★ 06.11.2026 КАМЕНЬ ДУШИ, БАНКА ОПЫТА. Конверт у всех трёх — `SendSecured`. Логика — `UAsdaSoulStoneSubsystem`.
	// 5419 C→S (35 б = 24 + 11) `SoulJar_RequestUser_Req@0x495010`, эталон `net_sendEquipItemRequest@0xD22D50` (network.c:18949):
	//   @0 i16 ячейка (запись +5), @2 i16 запись +7, @4 u8 контейнер (запись +4), @5 i32 запись +9, @9 i16 запись +14.
	bool SendSoulJarRequest5419(const uint8 (&Body11)[11]);
	// 5422 C→S (38 б = 24 + 14) `SoulJar_ResponseUser_Res@0x4954D0`, эталон `netSendItemMoveSecure@0xD23540` (network.c:19133):
	//   @0 u8 режим (1 согласие, 2 отказ), @1 i16 userNum просителя, @3 11 байт из 5421. ⚠ Имя в эталоне — миснейм, торговли здесь нет.
	bool SendSoulJarResponse5422(uint8 Mode, int32 RequesterUserNum, const uint8 (&Pos11)[11]);
	// 5423 C→S (24 б, тела нет) `SoulJar_ForwardExp_Req@0x495610`, эталон `antiCheat_SendIntegrityChallenge@0xD24470` (security.c:1212).
	bool SendSoulJarForwardExp5423();

	// ═════════ ★★★ ВОЛНА 2: КОРОБКИ/ПАКЕТЫ · ГАЧА · ПРЕМИУМ-ПРЕДМЕТЫ ═════════════════════════════
	//
	// ★★★ 6096 C→S (49 б = 8 токен + 16 MD5 + 25 тело) `net_SendItemActionReq_5slot@0xDAB7E0`
	//   (имя эталона ВРЁТ — это и есть «вскрыть коробку»), сервер `Recv_OpenPackageItem@0x55A820`
	//   (диспетчер `case 0x17D0`, GameServer.c:19404). Тело:
	//     @0  i16 СВОЙ userNum (`Target_GetSelectedUserIdx(локальный)` — миснейм, ★ 27.09.2026) — сервер это поле НЕ читает;
	//     @2  i32 **charId** отправителя (`CCharacter+0x41C`); сервер сверяет с `sub_5324F0(...)`
	//              и при расхождении отвечает кодом 2 ⇒ сюда идёт `SelectedCharId`, НЕ userNum
	//              ([[charid-vs-usernum]]);
	//     @6  u8  число выдаваемых позиций (1…5; сервер: 0 или >5 ⇒ код 6);
	//     @7  u8  контейнер коробки (запись слота +4);
	//     @8  i16 слот коробки (запись слота +5);
	//     @10 5 × {u8 контейнер, i16 слот} — слоты-приёмники, ПОДБИРАЕТ САМ КЛИЕНТ
	//              (`itemFindSowelSlots@0xDAACF0`); 0xFF = позиция не используется.
	//   `Slots15` — ровно 15 байт этих пяти троек (порядок как в эталоне: контейнер, затем i16 слот).
	// ★ 27.09.2026: параметр @0 переименован `TargetUserIdx` → `SelfNum` (это свой номер, не цель).
	bool SendOpenPackageItem6096(int32 SenderCharId, int32 SelfNum, uint8 Count,
	                             uint8 SrcContainer, int32 SrcSlot, const TArray<uint8>& Slots15);

	// ★★★ 6097 S→C (300 б) `netOnStorageResultPacket@0xDABFD0` (имя ВРЁТ — это результат вскрытия).
	//   Раскладка снята по СЕРВЕРНОЙ сборке (`sub_55B1C0@0x55B1C0`, GameServer.c:133590-133600) и
	//   сходится с чтением клиента байт в байт:
	//     @0 i16 userNum · @2 i32 charId · @6 u8 сколько позиций выдано · @7 u8 код результата
	//     @8 u8 контейнер коробки · @9 i16 слот коробки · @11 5 × 57 б записи выданных предметов
	//     @296 i32 новый вес персонажа.
	//   Пакет идёт ТОЛЬКО автору (`sub_4C28F0` → `PackingSendPacket`), рассылки по сектору нет.
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnAsdaPackageOpenResult, const TArray<uint8>& /*body300*/);
	FOnAsdaPackageOpenResult OnPackageOpenResult;

	// ★★★ 5468 C→S (31 б = 8 + 16 + 7) `Send_GachaItemUse@0xA0EAD0` — ПРИМЕНИТЬ ГАЧА-ПРЕДМЕТ
	//   («бустер»). Сервер `Recv_GachaItemUse@0x4F48C0` (`case 0x155C`). Тело:
	//     @0 i32 itemCode · @4 u8 контейнер · @5 i16 слот.
	bool SendGachaItemUse5468(int32 ItemCode, uint8 ContainerType, int32 SlotIndex);

	// ★★★ 5469 S→C (68 б) `Recv_BoosterShopResult@0xF4C220` — результат гачи, ТОЛЬКО автору:
	//     @0 u8 код (0 = УСПЕХ, 1…8 — отказы) · @1 u8 «редкий» · @2 i32 код потраченной гачи
	//     @6 u8 контейнер · @7 i16 слот на удаление · @9 57 б запись выигранного предмета
	//     @66 i16 новый вес.
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnAsdaGachaResult, const TArray<uint8>& /*body68*/);
	FOnAsdaGachaResult OnGachaResult;

	// ★★★ 5474 C→S (25 б = 8 + 16 + 1) — «покажи соседям мой эффект удачи»; тело: @0 u8 «редкий».
	//   Сервер `Recv_GachaItemEffect@0x4F58D0` → 5473 по 9 секторам, ОТПРАВИТЕЛЬ ИСКЛЮЧЁН
	//   (`SendPacket_ClippingSectorInOtherUser@0x41EA80`: `if (userNum != a3) send`), поэтому свой
	//   эффект клиент играет сам, а двойного показа не бывает.
	bool SendGachaItemEffect5474(uint8 bRare);

	// ★★★ 5473 S→C (7 б) `OnRecv_OtherPlayerBoost@0xF4C700`: @0 i32 charId (клиент НЕ читает),
	//   @4 i16 userNum (по нему ищется актор), @6 u8 «редкий».
	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnAsdaOtherPlayerBoost, int32 /*userNum*/, bool /*bRare*/);
	FOnAsdaOtherPlayerBoost OnOtherPlayerBoost;

	// ═════════════ ПРЕМИУМ-ГАЧА (лутбокс за премиум-валюту) ═════════════
	//   ★★★ ЭТО ДРУГАЯ СИСТЕМА, НЕ «бустер» выше. Доказано пятью независимыми признаками
	//   (Reports/gacha_net_re.md §4.1): своя сцена `CGachaScene` (7 окон Premium_Pick*, вход —
	//   кнопка HUD 73410 «Лотобокс»), свои опкоды, своя валюта (премиум-баланс `CCharacter+0x44C`),
	//   свои таблицы, ноль общего кода. Бустер — расходник из сумки (5468/5469), платится предметом.
	//   Приз выбирает ВНЕШНИЙ агент GP7 (порт 15022, команда 0x50 subtype 2 = LuckyBox), а не клиент
	//   и не GameServer: у премиум-гачи таблицы вероятностей нет вообще, есть список 356 позиций
	//   (`PrimiumGaChaTable.txt`), из которого агент выбирает равномерно.

	// ══════════ ★★★ «О СЕБЕ» (окно 8300 `My_Introduction_Win`, кнопка 987 окна 900) ═════════════
	// Полный разбор — `Reports/char_info_fix2.md` §4. Опкод ОДИН на оба направления.
	//
	// C→S 6080 (152 б = 8 обфусц. таймштамп + 16 MD5 + 128 текст) `netSecure_sendTextInput@0xE74220`:
	//   текст санитизируется (`'` → `` ` ``) и обрезается до 128 байт. Сервер
	//   (`Recv_SoulmateIntroduction`, GameServer.c:61150) проверяет MD5 по 0x80 и пересылает
	//   community/DB-серверу опкодом 2262 (136 б).
	bool SendSoulmateIntroduction6080(const FString& Text);

	// S→C 6080 (128 б) `uiSetNameText@0xE749B0` (`network.c:525`, case 0x17C0) — СОХРАНЁННЫЙ текст
	//   «о себе». Приходит от `charLogonCommunityRes@0x498B40` — и в ответ на запись, и при входе.
	//   Тело — ровно 128 байт cp1251 без поля длины.
	FString SelfIntroduction;
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnAsdaSelfIntro, const FString& /*text*/);
	FOnAsdaSelfIntro OnSelfIntroChanged;

	// ══════════════ ★★★ ПРЕМИУМ-МОЛЛ: ПОКУПКА 5070 → РЕЗУЛЬТАТ 5071 ═══════════════════════════
	// Полный побайтовый разбор — `Reports/premium_mall_core.md` §3.2/§3.3, порт —
	//   `Reports/premium_mall_port.md`.
	//
	// C→S **5070** `OP_CHAR_CHARGE_SHOP_BUY`, LEN 195 = 24 (конверт `SendSecured`) + 171 (0xAB).
	//   ★★★ ПАДДИНГ ЗДЕСЬ НУЖЕН. Все пять эталонных отправителей этой ветки зовут
	//   `CNetwork_Send(..., 195, OP_CHAR_CHARGE_SHOP_BUY, 1, 1, 1, 0, 0)` — шестой аргумент
	//   (`a6`) = **1** (`ida.c:422504`, `480761`, `480966`, `491030`, `617195`; эталон
	//   `shop_trade.c:1429,3016,3221`). В отличие от аукциона, где пад ЛОМАЛ пакет, здесь он
	//   обязателен ⇒ `SendSecured(..., bPad = true)` (значение по умолчанию).
	//   Сервер читает тело с `frame+33` = 9 (кадр) + 24 (конверт) — сходится побайтово
	//   (`srv.c:104591-104598`).
	//
	//   ТЕЛО 171 б (конструктор `sub_B85C10@0xB85C10`):
	//     +0        u8   режим/тип витрины (эталон кладёт 1)
	//     +1        u8   признак таблицы: 1 = `item_list`, 0 = `special_itemlist`
	//                    (сервер по нему выбирает диапазон кодов `sub_532A90` / `sub_532B10`)
	//     +2        u8   доставка: 1 = в инвентарь, 0 = «в хранилище» (сервер: 1→v39=1, иначе 5)
	//     +3+4·i    i32  ×7 коды предметов, ПУСТО = −1
	//     +31+4·k   u16  ×70 (35 пар) гнёзда/опции, ПУСТО = −1
	//   ⚠ Ловушка проекта «пустой слот = −1, при этом +4 = 0» здесь НЕ применяется: пустой
	//     слот — просто −1 (dword), поля +4 у записи нет.
	bool SendPremiumShopBuy5070(const uint8 Body171[171]);

	// Удобная сборка тела: коды (до 7) + флаги. Пустые слоты и все 35 пар гнёзд = −1.
	//   ★ Цикл ОГРАНИЧЕН семью записями и НЕ пропускает индекс 7 — это исправление дефектов
	//   К1/К2 оригинала (`boosterShopSendBuyItem@0xC2F240`: `slotIdx != 7` вместо `slotIdx < 7`,
	//   переполнение стека при count ≥ 43 и потеря восьмой позиции). Ключи отката —
	//   `asda.PremiumMall.FixMultiBuyBound` / `asda.PremiumMall.FixSlot7Skip` в
	//   `UAsdaPremiumShopSubsystem`.
	static void BuildPremiumShopBody(const TArray<int32>& ItemCodes, uint8 Mode, bool bTable1,
		bool bToInventory, uint8 OutBody[171]);

	// S→C **5071** `shop_OnRecvTransactionResult@0xC30BD0`, тело 407 (0x197):
	//     +0        u8   код результата 0…0x0E
	//     +1        u8   селектор валюты: ≠0 → GXP, 0 → очки обаяния
	//     +2…+5     i32  новый баланс GXP
	//     +6…+7     i16  вес
	//     +8…+406   7 × 57 — стандартные записи слота инвентаря
	//   ★ Байт +12 (= +4 внутри записи слота 0) — «подрежим доставки»: 5 ⇒ покупка ушла
	//     в ХРАНИЛИЩЕ, иначе — в инвентарь.
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnAsdaPremiumShopResult, const TArray<uint8>& /*body407*/);
	FOnAsdaPremiumShopResult OnPremiumShopResult;

	// ══════════════════ ★ 01.11.2026 P101_CashGift: ПОДАРОК ИЗ КЭШ-МАГАЗИНА (реверс `Reports/cash_gift/re/client.md`) ══════════
	//   Конверт вида A «с секретом», как у 5070: `CNetwork_Send(…, size, op, 1, 1, 1, 0, 0)` ⇒ `SendSecured(op, тело, N, bPad = true)`.
	//   Отправители не проверяют ключ `asda.CashGift.Enable` — это делает владелец состояния `UAsdaCashGiftSubsystem`.
	// ★ C→S **5072** (44 = 24 + 20) «проверить имя получателя»: тело — char[20] имени cp1251 с NUL-добивкой
	//   (`netSendSecureChallengeMd5@0xC42BC0`, network.c:13506; `BoosterShop_SendBuy@0xB841E0`, shop_trade.c:917).
	bool SendCashGiftCheck5072(const uint8 Name20[20]);
	// ★ C→S **5074** (211 = 24 + 187) «подарить»: тело собирает `UAsdaCashGiftSubsystem::BuildPresentBody`
	//   (`net_sendItemActionRequest@0xC33C60` network.c:13076; `BoosterShop_SendBuyPresent@0xB84840` shop_trade.c:1049).
	bool SendCashGiftPresent5074(const uint8 Body187[187]);
	// ★ S→C 5073 (34 б, проверка имени), 5075 (6 б, итог подарка), 5077 (61 б, итог через реле), 5078 (421 б, подарок пришёл):
	//   тело целиком (без 9-байтной шапки) делегатом. Разбор и реакция — `UAsdaCashGiftSubsystem` (из своего Tick, не из колбэка).
	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnAsdaCashGiftPacket, int32 /*Opcode*/, const TArray<uint8>& /*Body*/);
	FOnAsdaCashGiftPacket OnCashGiftPacket;

	// ★★★ 5466 C→S (28 б = 8 + 16 + 4) `net_SendVerifiedRequest5466@0xECADD0` — запрос
	//   премиум-баланса. Тело: @0 i32 charId (сервер сверяет с `sub_5324F0`; иначе 5467 код 2).
	//   ⚠ Здесь именно charId (i32), а НЕ userNum (i16) — при путанице сервер молча игнорирует.
	bool SendPremiumBalanceQuery5466(int32 CharId);

	// ★★★ 5467 S→C (5 б) `netHandleAuthResult@0xECB480` — премиум-баланс:
	//     @0 u8 статус (1 = успех, 0 = нет данных → нотис 12942, 2 = charId не совпал, 3 = занят)
	//     @1 i32 баланс премиум-очков → `CCharacter+0x44C`.
	//   Цепочка на сервере: 5466 → GP7 команда 0x5A → ответ 0x5B (15 б) → 5467.
	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnAsdaPremiumBalance, uint8 /*status*/, int32 /*balance*/);
	FOnAsdaPremiumBalance OnPremiumBalance;

	// ══════════════════════ ПАРИКМАХЕРСКАЯ 15000 (`npc_hairshop_30029.md` §5) ══════════════════════
	// ★★★ 5470 C→S (28 б = 8 метки + 16 md5 + 4 тела) — «КУПИТЬ ПРИЧЁСКУ».
	//   ⚠ МИСНЕЙМ `OP_RECV_GACHA_ITEM_USE_5470` уже снят в `NetOpcodes.h`: к гаче опкод отношения
	//   не имеет — серверный обработчик пишет в лог `"Recv_HairShopBuy() %s, Hair = %d,%d,%d"`
	//   (`GameServer.c:94687`). Отправители эталона: `hairShop_SendApplyRequest@0xC4D130`
	//   (`shop_trade.c:6163`) и `boosterShop_SendOpenRequest@0xCF1890` (`ida.c:545083`) — тело у
	//   обеих одинаковое, `Net_Md5Digest16(..., 4)`, `CNetwork_Send(..., 28, 5470, 1,1,1,0,0)`.
	//   Тело — 4 байта, предзаполнение `sub_CF2270@0xCF2270` («пусто = −1, соседнее = 0»):
	//     @0 i16 HairId (запись `hairtable` +0)  ·  @2 u8 HairStyle (+44)  ·  @3 u8 ColorIdx (+77)
	//   ★ Стиль и цвет сервер лишь СВЕРЯЕТ с таблицей (шаг 9 `Recv_GachaItemUse_0@0x4F59C0`),
	//     поэтому оба обязаны быть взяты ИЗ ЗАПИСИ, а не выдуманы.
	bool SendHairShopBuy5470(int32 HairId, uint8 HairStyle, uint8 ColorIdx);

	// ★★★ 5471 S→C (тело 9 б) `hairShop_HandleResult@0xC4D390` — результат покупки причёски:
	//     @0 u8  код 0…5 (0 ошибка БД → 13007, 1 успех → 13005, 2 мало GXP → 13003,
	//                     3 уровень → 13017, 4 пол → 13018, 5 «друзьям недоступно» → 12165)
	//     @1 i16 HairId · @3 u8 HairStyle → `char+0x4C5` · @4 u8 ColorIdx → `char+0x4C6`
	//     @5 i32 остаток кэша → `char+0x44C`
	//   ★ Хвоста нет — все 9 байт значимы (`sendHairShopBuy@0x4F5D50`, `GameServer.c:94770-94776`).
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnAsdaHairShopResult, const TArray<uint8>& /*body9*/);
	FOnAsdaHairShopResult OnHairShopResult;

	// ★★★ 5472 S→C (тело 10 б) `CharPacket_ApplyHelmet@0xCF22C0` — ★ МИСНЕЙМ: это рассылка
	//   «ЧУЖОЙ ИГРОК СМЕНИЛ ПРИЧЁСКУ» (`sub_52E6D0(..., 5472, 10, ...)`, `GameServer.c:94781`):
	//     @0 i32 charId  ·  @4 i16 userNum  ·  @6 i16 HairId  ·  @8 u8 Style  ·  @9 u8 Color
	//   ★★★ ЛОВУШКА `charid-vs-usernum`: клиент ищет персонажа `CharManager_FindByUserIdx(v3[2])`,
	//     то есть по **+4 (userNum, i16)**; поле +0 (charId) он НЕ использует вовсе.
	DECLARE_MULTICAST_DELEGATE_FourParams(FOnAsdaHairChanged, int32 /*userNum*/, int32 /*hairId*/,
		uint8 /*style*/, uint8 /*color*/);
	FOnAsdaHairChanged OnHairChanged;

	// ★★★ 6577 C→S (29 б = 8 + 16 + 5) `premiumGatya_SendRequest@0xB6F890` — КРУТИТЬ:
	//     @0 i32 charId (сервер сверяет с `CUser+372`) · @4 u8 номер слота/типа гачи.
	//   Серверный путь: `Recv_UserPrimiumGaCha@0x4F6F10` → цена из `PrimiumGaChaPriceTable.txt` →
	//   (наш патч P05) запрос к GP7 кодом 105 «LuckyBox Item Buy Request» → ответ агента → 6578.
	bool SendPremiumGachaRoll6577(int32 CharId, uint8 SlotType);

	// ★★★ 6578 S→C (63 б) `GachaScene_OnServerResult` — результат прокрута:
	//     @0 u8 статус (1 = успех; 2 → нотис 110623, 3 → 110624, 0xA → 110620, 0xB → 12960)
	//     @1 56 б запись приза (→ `CGachaScene+720`) · @57 u8 хвост записи (→ +776)
	//     @58 u8 сервером не заполняется · @59 i32 новый премиум-баланс (→ `CCharacter+0x44C`).
	//   ⚠ Флага «редкий приз» в пакете НЕТ — у премиум-гачи нет ветвления по редкости
	//     (проверено сплошным сканом 159 функций 0xB6A000…0xB74000).
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnAsdaPremiumGachaResult, const TArray<uint8>& /*body63*/);
	FOnAsdaPremiumGachaResult OnPremiumGachaResult;

	// ★★★ 6581 C→S (35 б = 8 + 16 + 11) `sowelWndSendSocketRequest@0xB71D70` — ЗАБРАТЬ приз:
	//     @0 i32 код предмета приза · @4 i16 индекс слота · @6 u8 контейнер · @7 i32 код варианта.
	//   ⚠ ПОПРАВКА к старому доку `RE/docs/client/logic/premium_package_gacha.md`: поле @0 —
	//     это КОД ПРЕДМЕТА, а не charId; сервер (`Recv_UserPrimiumGaChaReward@0x4F72B0`) сверяет
	//     его с кодом в слоте и при расхождении отвечает 6582 с кодом 10.
	bool SendPremiumGachaReward6581(int32 ItemCode, int32 SlotIndex, uint8 ContainerType, int32 VariantCode);

	// ★★★ 6582 S→C (61 б) `uiGachaResultApply` — приз зачислен:
	//     @0 u8 статус (1 = успех, 10 = отказ) · @1 u8 контейнер очищаемой ячейки (в успехе 0)
	//     @2 i16 индекс очищаемой ячейки (в успехе −1) · @4 56 б запись предмета · @60 u8 хвост.
	//   Успех → нотис 110671.
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnAsdaPremiumGachaApply, const TArray<uint8>& /*body61*/);
	FOnAsdaPremiumGachaApply OnPremiumGachaApply;

	// ═════════ ★★★ ЭЛИТНЫЙ ПОЧТОВЫЙ ЯЩИК (окно 69000): 11004/11005/11006/11007 ═════════════════
	//   Полный реверс — `Reports/elite_mailbox_re.md`. Владелец в порту —
	//   `UAsdaPremiumPostSubsystem`. Здесь только транспорт: сборка тел и диспетчеризация.
	//
	// ★ 11004 C→S, 28 б = 8 токен + 16 MD5(payload 4) + 4 НУЛЕВЫХ байта
	//   (`netSecure_sendGuardedRequest@0xEF0120`: `packetBuf` заполняется токеном и дайджестом,
	//   хвост 4 б остаётся нулём, длина 28). Тела сервер (`Recv_PremiumItemList_Req@0x4F7F70`)
	//   вообще не читает — ни анти-реплея, ни MD5 в этой ветке нет. Гвард «запрос уже в полёте»
	//   держит ВЫЗЫВАЮЩИЙ (эталонное `this[616]`), а не транспорт.
	bool SendPremiumPostListReq11004();

	// ★ 11005 S→C — содержимое ящика: `CharIndex(4) + N × запись(10)`.
	//   Запись (имена из формат-строки лога сервера `0x594568`):
	//     @0 i32 Order · @4 i32 ItemIndex · @8 u8 Count · @9 u8 BuyType (1 = шоп, 3 = гача).
	//   ⚠⚠ ДЕФЕКТ ОРИГИНАЛА C: сервер (`RecvDB_PremiumItemList_Res@0x4F8050`) число записей НЕ
	//   ограничивает (буфер 649 б = до 64 записей), а клиент рассчитан ровно на 7
	//   (`_WORD slotArray[36]`, 72 б) ⇒ `memcpy(slotArray, src, 10*N)` — переполнение стека.
	//   Здесь тело отдаётся подписчику КАК ЕСТЬ (без обрезки), а кламп N ≤ 7 делает разбор в
	//   подсистеме — так в логе видно и реальное N сервера, и сколько мы взяли.
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnAsdaPremiumPostList, const TArray<uint8>& /*body*/);
	FOnAsdaPremiumPostList OnPremiumPostList;

	// ★ 11006 C→S, 34 б = 8 токен + 16 MD5(payload 10) + 10 б записи ВЫБРАННОЙ строки.
	//   Сервер `Recv_PremiumItemToInven_Req@0x4F8340` проверяет и анти-реплей (`0x524390`),
	//   и MD5 по 10 байтам (`0x523F00(pkt, 0x0A, …)`) — то есть, в отличие от 11004, кадр обязан
	//   быть подписан полностью. Это ровно формат `SendSecured`.
	bool SendPremiumPostToInven11006(const uint8 Rec10[10]);

	// ★ 11007 S→C, 409 б (0x199): @0 u8 статус · @1 i32 CharIndex · @5 7×57 записей предмета
	//   · @404 i32 текущий вес · @408 u8 BuyType.
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnAsdaPremiumPostApply, const TArray<uint8>& /*body409*/);
	FOnAsdaPremiumPostApply OnPremiumPostApply;

	// ★★★ 5450 C→S (281 б = 8 + 16 + 257) `netSendItemUsePacket@0xA0D470` — ПРИМЕНИТЬ ПРЕМИУМ/
	//   ФУНКЦИОНАЛЬНЫЙ предмет (`special_itemlist`). Сервер `Char_FunctionItem_Use@0x4ED780`
	//   (`case 0x154A`) читает только ПЕРВЫЕ 137 байт (`memcpy(v88, a3+33, 0x89)`), но кадр обязан
	//   быть полным: MD5 клиент считает по всем 0x101 байтам. Раскладка (эталон
	//   `struct_ResetToDefault@0xA0DD60`, network.c:7261 — она же задаёт значения по умолчанию −1):
	//     @0   i16 targetUserNum (свой userNum)      @2  i32 itemCode (код `special_itemlist`)
	//     @6   i16 слот инвентаря                    @8  i16 posX      @10 i16 posZ
	//     @12  u8  clickByte                         @13 i16 value
	//     @15  i16[20] список целей (элемент 0 — свой userNum)
	//     @55  char[200] сообщение (переименование/объявление; сервер берёт из неё 80 б)
	//     @255 i16 доп. слово, пишется ТОЛЬКО при `subType == 36`.
	//   ⚠ Длина строки именно 200 (`memset(this+55, 0, 0xC8)`), а «char[205]» в декомпиле — артефакт.
	bool SendFunctionItemUse5450(int32 TargetUserNum, int32 ItemCode, int32 ItemPos,
	                             int32 PosX, int32 PosZ, uint8 ClickByte, int32 Value,
	                             const TArray<int32>& TargetUserNums, const FString& Message);

	// ★★★ 5455 C→S (28 б = 8 + 16 + 4) `netSendSimpleCmd_5455@0xA0E4B0` — ВЫКЛЮЧИТЬ состояние
	//   функционального предмета (транспорт/спутник). Тело: @0 i32 itemCode.
	//   Сервер `PopFunctionItemState_Toggle@0x4F1F60` → 5454 по сектору.
	bool SendFunctionItemToggle5455(int32 ItemCode);

	// ★★★ 5451 S→C (68 б) `netRecvItemBuffApply@0xF23C60` — результат 5450, ТОЛЬКО автору:
	//     @0 u8 код результата (1 = успех, остальные — отказы, таблица нотисов в инвентаре)
	//     @1 i16 userNum · @3 57 б запись предмета (её +0 код, +4 контейнер, +5 слот)
	//     @60 i16 текущий вес · @62 i32 · @66 i16 максимальный вес.
	//   ⚠⚠ ИСПРАВЛЕНО В ВОЛНЕ 2: прежний обработчик считал `Body[0]` «подтипом эффекта» и ждал 50.
	//   На деле `Body[0]` — КОД РЕЗУЛЬТАТА, а 50 — это `case 50` ВЛОЖЕННОГО `switch (rec->subType − 1)`
	//   внутри ветки успеха (network.c:36541-36555), то есть `SpecialItemRecord.subType == 51`.
	//   Из-за подмены условия вес по этому каналу не обновлялся НИ РАЗУ.
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnAsdaFunctionItemResult, const TArray<uint8>& /*body68*/);
	FOnAsdaFunctionItemResult OnFunctionItemResult;

	// ★ 5452 S→C (4 б) `recvItemRemoveById@0xF34B30` — снять запись активного функц. предмета:
	//   @0 i32 itemCode (у эталона это `id` узла списка `g_pItemMgr+2`).
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnAsdaFunctionItemExpired, int32 /*itemCode*/);
	FOnAsdaFunctionItemExpired OnFunctionItemExpired;

	// ★★★ 5456 S→C (2 б) `recvWarehouseChangeBagPage@0xF34BC0` — РАСШИРЕНИЕ СКЛАДА
	//   (ёмкость В ЯЧЕЙКАХ, сервер `CUser+430 += 30`, потолок 270): @0 i16 новая ёмкость.
	//   ★ РАНЬШЕ ЗДЕСЬ БЫЛА ОШИБКА: это число писалось в МАКСИМАЛЬНЫЙ ВЕС, и полоса
	//   веса в мини-статусе показывала 8842/210 = 4210 % вместо 8842/32000 = 27 %
	//   (разбор с числами — `Reports/ministate_weight_fix.md`).
	//   Эталонная цепочка: `recvWarehouseChangeBagPage` → `warehouseChangeBagPage@0xF26820`
	//   → `inventoryChangeBagPage@0xE8E440` (`item_inventory.c:59130`), которая пишет
	//   `CCharacter->gap_43B[27]` = `CCharacter+0x456`. Вес живёт в `gap_43B[13]` = +0x448 (максимум)
	//   и `gap_43B[15]` = +0x44A (текущий) — см. `HUD_UpdateMiniWeightBar@0xE78FA0`, `ui_core.c:75142`.
	//   Что именно в +0x456, видно в `uiUpdateStatusTexts@0xE8D090` (`ui_core.c:78865`):
	//   `wsprintfA(Str, "%3d/%3d", gap_43B[29], gap_43B[27])` — «занято / всего ячеек склада»,
	//   а `gap_43B[27] / 30` — число вкладок (`ui_core.c:78833`, `item_inventory.c:59102`).
	// ПРАВИЛА ПРИЁМА ЗНАЧЕНИЯ (1:1 `inventoryChangeBagPage`): принять только если новое
	//   больше текущего; если текущее уже 270 — отказ (loc 32010); если новое даёт
	//   меньше 2 вкладок (`новое/30 < 2`) — откат к прежнему (loc 12834); иначе принять,
	//   открыть вкладку `новое/30 - 1` и сообщить loc 12833.
	// NoticeLocId = тот самый loc для чата/подсказки; рисует его ОКНО СКЛАДА (эталон
	//   делает это внутри `g_pWarehouseWnd`), а не сетевой слой.
	// ★★★ bAlsoChat — четвёртый параметр заведён 19.08.2026 (дефект «предмет расширения склада
	//   применился, а модалки нет», `Reports/inventory_use_fix.md`). Эталон
	//   `inventoryChangeBagPage@0xE8E440` в ЧЕТЫРЁХ своих ветках выводит текст ПО-РАЗНОМУ:
	//     • новое ≤ текущего            → чат 12834 + модалка 12834   (bAlsoChat = true)
	//     • текущее уже 270             → чат 32010 + модалка 32010   (bAlsoChat = true)
	//     • принято                     → чат 12833 + модалка 12833   (bAlsoChat = true)
	//     • новое/30 < 2 (откат)        → ТОЛЬКО модалка 12834        (bAlsoChat = false)
	//   Трёх параметров не хватало, чтобы отличить первую ветку от четвёртой (у обеих
	//   `bAccepted = false, loc = 12834`), и порт неизбежно врал бы лишней строкой в чате.
	DECLARE_MULTICAST_DELEGATE_FourParams(FOnAsdaBagExpand,
		int32 /*newMaxSlots*/, bool /*bAccepted*/, int32 /*noticeLocId*/, bool /*bAlsoChat*/);
	FOnAsdaBagExpand OnBagExpand;

	// ЁМКОСТЬ СКЛАДА в ячейках (`CCharacter+0x456`) и число занятых (`CCharacter+0x458`).
	//   Первичный источник — блок 1017 (смещения блока +58 и +60), дальше обновляет 5456.
	//   Не путать с `UAsdaInventorySubsystem::kStoreSlots` — там ПОТОЛОК 270, а здесь ФАКТ.
	//   −1 = ещё не приходило.
	int32 SelfStoreSlotsMax  = -1;
	int32 SelfStoreSlotsUsed = -1;
	// Потолок ёмкости склада и шаг вкладки (эталон `inventoryChangeBagPage`: 270 и 30).
	static constexpr int32 kStoreSlotsCap  = 270;
	static constexpr int32 kStoreSlotsPage = 30;

	// ★ 5457 S→C (57 б) `recvInventoryConfirmDye@0xF34BF0` — имя ВРЁТ: это ВОССТАНОВЛЕНИЕ
	//   ПРОЧНОСТИ; тело = полная запись предмета 57 б (прочность в байте +13).
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnAsdaItemRepaired, const TArray<uint8>& /*entry57*/);
	FOnAsdaItemRepaired OnItemRepaired;

	// ★ 5459 S→C (84 б) `recvItemStatusNotice@0xF34C20` → `uiTooltip_ShowItemStatusNotice@0xF26880`:
	//   @0 u8 код (1 = успех, прочие — нотисы), @25 57 б запись предмета.
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnAsdaItemStatusNotice, const TArray<uint8>& /*body84*/);
	FOnAsdaItemStatusNotice OnItemStatusNotice;

	// ★ 5006 C→S (35 б = 24 + 11) `CharItemDump@0x4D3410` — УНИЧТОЖИТЬ предмет (item-ref 11 б).
	// OverrideAmount > 0 — уничтожить ЧАСТЬ стека (количество из окна 1410); <= 0 — весь стек.
	bool SendItemDump5006(const TArray<uint8>& Entry57, int32 OverrideAmount = -1);

	// ★ 5002 C→S (35 б = 24 + 11) `CharItemDrop@0x4D2690` — ВЫБРОСИТЬ на землю (item-ref 11 б).
	//   ⚠ В КЛИЕНТЕ ЭТОГО ОТПРАВИТЕЛЯ НЕТ (дефект оригинала, отчёт §8.3) — серверный обработчик полный.
	bool SendItemDrop5002(const TArray<uint8>& Entry57);

	// ★ 4050 C→S (29 б = 24 + 5) — запрос СТРАНИЦЫ СКЛАДА (`Send_ClientStoreHouseItemInfo@0x529DA0`):
	//   payload 5 б: `+0 u32 userIdx`, `+4 u8 pageNo` = вкладка + 1 (эталон `uiBagSendActionPacket@0xE89B30`,
	//   `ui_core.c:78585`). Ответ — 4051 (записи) + 4052 (конец).
	//   ★ Диапазон 1…9: при 0 сервер читает память ДО массива склада (дефект D2, см. .cpp).
	bool SendStoreHousePageReq4050(int32 PageNo);

	// ═══ СКЛАД: ПОЛОЖИТЬ (5008) и ЗАБРАТЬ (5010) ═══════════════════════════════════════════════
	//
	// Оба пакета — 79 б = 24 (токен+MD5) + 55 (тело). Тело = **5 записей по 11 байт**, по числу
	//   ячеек КОРЗИНЫ ПЕРЕНОСА окна склада (контролы 4039…4043). Раскладка одной записи —
	//   ровно та же, что у 5000 (`SendItemMove5000`), поля берутся из 57-байтовой записи слота:
	//     `+0  i16` = e[5..6]   слот-источник
	//     `+2  i16` = e[7..8]   ★ сервер это поле НЕ ЧИТАЕТ (проверено по обоим обработчикам:
	//                            `CharItemKeeping@0x5094A0` и `CharItemRetake@0x50B3D0` обращаются
	//                            только к `+0`, `+4`, `+5`)
	//     `+4  u8 ` = e[4]      тип контейнера-источника
	//     `+5  i32` = e[9..12]  количество
	//     `+9  i16` = e[14..15] ВЕС СТЕКА; для обычных торгуемых предметов (кроме золота 20551)
	//                            эталон пересчитывает его как `количество * LOWORD(field72)`
	//                            (`network.c:31272`), где `field72` — вес единицы из шаблона.
	//   ★ ХВОСТ: неиспользованные записи НЕ обнуляются, а инициализируются
	//     `helper_initInvalidIds@0x973D30` (`network.c:3586`) через `CRT_ArrayCallbackIterator(_, 11, 5, _)`:
	//     `+0 = −1`, `+2 = −1`, `+4 = 0` (именно НОЛЬ, не −1), `+5 = −1`, `+9 = −1`.
	//     Ноль в `+0` для сервера — ВАЛИДНЫЙ слот, поэтому обнулять хвост нельзя (на этом же
	//     спотыкались ремонт 6571 и сброс улучшений 20200).
	//
	// ★ ИСПРАВЛЕНИЕ ДЕФЕКТА ОРИГИНАЛА D1: в эталоне поле `+2` индексируется номером ЯЧЕЙКИ КОРЗИНЫ
	//   (`11 * slot`), тогда как остальные четыре поля — порядковым номером заполненной записи
	//   (`11 * cnt`), см. `network.c:31266` против `:31264/31265/31267/31268`. При дырках в корзине
	//   (например занято 1-е и 3-е гнёзда) значение уезжает в чужую запись. Последствий НЕТ —
	//   сервер поле не читает, — но мы пишем его правильно. Откат: `asda.Warehouse.LegacyField2 1`.
	//
	// `Basket` — до 5 непустых 57-байтовых записей ячеек корзины (пустые просто не передавать).
	// 5008 (`Send_CharItemKeeping@0xE8C3A0`, кнопка 4002 «Keep») — положить НА склад; ответ 5009.
	bool SendItemKeeping5008(const TArray<TArray<uint8>>& Basket);
	// 5010 (`Send_CharItemRetake@0xE8BAF0`, кнопка 4049 «Get») — забрать СО склада; ответ 5011.
	//   ⚠ Сервер берёт комиссию 30 золота за предмет ТОЛЬКО здесь (бесплатно, если город карты
	//     принадлежит фракции игрока); клиент до отправки сам проверяет деньги (нотис 2245).
	bool SendItemRetake5010(const TArray<TArray<uint8>>& Basket);

private:
	// Общая сборка тела 55 б (5 записей по 11) для 5008/5010 — см. комментарий у отправителей.
	void BuildWarehouseBasketBody55(const TArray<TArray<uint8>>& Basket, uint8* Out55) const;
public:

	// ★ 5000 (0x1388) C→S — ИСПОЛЬЗОВАТЬ предмет сумки (`net_sendInventoryItemUse@0xD11B20`).
	//   Тело 46 б: [0..7] токен, [8..23] MD5(payload22), [24..45] payload 22 б, собранный из
	//   57-байтовой записи контейнера: p[0..1]=e[5..6], p[2..3]=e[7..8], p[4]=e[4], p[5..8]=e[9..12]
	//   (count), p[9..10]=e[14..15], p[11..12]=itemType, p[15]=3 (код действия «использовать»).
	//   ⚠ Индекса ячейки в пакете НЕТ — предмет опознаётся полями своей записи, поэтому вызывающий
	//   обязан передать СЫРЫЕ 57 байт (их даст система инвентаря, когда появится).
	bool SendInventoryItemUse5000(const TArray<uint8>& Entry57, int32 ItemType);

	// ═══════ ★★★ «ХВАСТОВСТВО ПРЕДМЕТОМ»: 6129 C→S / 6130 S→C ═════════════════════════════════════
	//   Полный реверс — `Reports/showoff_system.md`. Окна: меню `Open_Item_Win` (28000) и список
	//   получателя `Opend_Item_Win` (28100). Владелец в порту — `UAsdaShowOffSubsystem`.
	//
	// ★ 6129 C→S, ВСЕГО 33 б = 8 токен + 16 MD5(payload 9) + payload 9 б.
	//   Эталон `netSendTsMaskedReq6129@0xD32880` (network.c:19988) собирает payload ИМЕННО ТАК:
	//     msg[0]                        = i32 ОТПРАВИТЕЛЬ (localPlayer->userIdx);
	//     LOWORD(msg[1])                = i16 userIdx АДРЕСАТА (−1 = «всем вокруг»);
	//     BYTE2(msg[1])                 = u8  КОНТЕЙНЕР предмета (запись+4);
	//     *(WORD*)((char*)&msg[1] + 3)  = i16 СЛОТ предмета     (запись+5).
	//   ⇒ раскладка байт: @0 i32 sender, @4 i16 target, @6 u8 container, @7 i16 slot.
	//   ★ MD5 считается по 9 байтам (`Net_Md5Digest16(&g_NetMd5Salt, msg, 9, dig)`) — это ровно то,
	//     что делает общий `SendSecured`, поэтому спец-сборщик не нужен.
	//
	// ★★★ ДВА РАЗНЫХ ПРОСТРАНСТВА ИНДЕКСОВ В ОДНОМ ПАКЕТЕ (найдено 2026-08-09, живой дефект
	//   «окно Opend_Item_Win не появляется вовсе»):
	//     @0 i32 sender — это **charId** (в сервере он же `iUserIndex`, поле `*(DWORD*)(CUser+372)`,
	//        имя персонажа лежит следом с +376). Сервер сверяет:
	//        `memcpy(v6, a3+33, 9); if (v6[0] == sub_5324F0(this, arg0))`, где
	//        `sub_5324F0` = `sub_5324A0(...)->gap_16B` = тот самый `CUser+372`
	//        (GameServer.c:116575-116586, лог :25118 «iUserNum %d, iUserIndex %d» — ДВА РАЗНЫХ поля;
	//        лог :6861 «Recv_StartFishing() Different UserIdx..!! Req:%d Serv:%d» сверяет то же).
	//        Это ровно тот же charId, что мы шлём в 4001 `CharLogonReq` и 5058 `CharPortalLogonReq`,
	//        т.е. `SelectedCharId` из 1017.
	//     @4 i16 target — наоборот, СЕССИОННЫЙ userNum (`SelfUserNum`-домен): сервер отдаёт его
	//        прямо в `sub_4C28F0(…, 6130, 81, v6[1], …)` и в `sub_5324F0(this, v6[1])`.
	//   ⇒ если в @0 положить сессионный userNum, сверка не проходит и сервер выходит МОЛЧА
	//     (отказа протокол не предусматривает) — 6130 не приходит НИКОМУ, включая отправителя.
	//   Отсюда имя параметра: `SenderCharId`, а НЕ userIdx.
	//
	//   СЕРВЕР `Recv_ItemInfoDisplay@0x55BF80` (GameServer.c:133853), полный список ворот:
	//     1) `sub_5328F0(arg0)` — индекс соединения валиден;
	//     2) `sub_524390(...)` — анти-реплей-токен;
	//     3) `sub_523F00(a3, 9, arg0, 1)` — MD5 по 9 б payload (в этой сборке результат memcmp
	//        отбрасывается — функция ВСЕГДА возвращает 1, длина при этом не проверяется);
	//     4) `v6[0] == sub_5324F0(this, arg0)` — АНТИ-ПОДМЕНА ОТПРАВИТЕЛЯ (см. выше);
	//     5) `sub_4D61E0(itemMgr, arg0, container, slot, out57, nullptr) == 1` — предмет ОБЯЗАН
	//        реально лежать в этом контейнере и слоте: 1 (шелл, слот ≤29), 2 (расходники, <60),
	//        3 (надетое, ≤10), 5 (склад, < MaxWeight). Иначе — снова молчание.
	//   Проверок уровня/кулдауна/партии/клана НЕТ.
	bool SendItemInfoDisplay6129(int32 SenderCharId, int32 TargetUserIdx, uint8 ContainerType, int32 SlotIndex);

	// ★ 6130 S→C, тело 81 б — «кто-то показал предмет» (`itemListWnd_OnFillPacket@0x974270`,
	//   item_inventory.c:25 — копирует `Src+9`, т.е. это уже payload, ровно 0x51 б).
	//   Сервер собирает тело в `Recv_ItemInfoDisplay`: [i32][char[20] ИМЯ отправителя][57 б ЗАПИСЬ].
	//     @0   i32 — при широковещании = userIdx ОТПРАВИТЕЛЯ, при адресной посылке сервер кладёт сюда
	//                userIdx АДРЕСАТА (`v9 = sub_5324F0(this, v6[1])`). ★ КЛИЕНТ ЭТО ПОЛЕ НЕ ЧИТАЕТ
	//                вовсе (эталон берёт только имя с +4 и запись с +24) — расхождение безвредно.
	//     @4   char[20] имя персонажа-отправителя, cp1251, обрывается нулём (`strcpy(&v8[4], a2+4)`).
	//     @24  57 б — ЗАПИСЬ ПРЕДМЕТА того же формата, что и содержимое контейнеров (код @+0 и т.д.).
	//   РАССЫЛКА: target < 0 → `SendPacket_ClippingSectorInAllUser` (ВСЕ в секторе AoI отправителя,
	//     включая его самого); target >= 0 → `sub_4C28F0(…, 6130, 81, target, …)` — ТОЛЬКО адресату.
	//   ⚠ Колбэк только КОПИТ тело (закон `no-reentrant-net-callbacks`).
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnAsdaItemShowOff, const TArray<uint8>& /*body81*/);
	FOnAsdaItemShowOff OnItemShowOff;

	// ═══════ ★★★ РЫБОЛОВНАЯ СНАСТЬ: 6165 C→S / 6166 S→C (окно «Взять рыболовную снасть» 15900) ═══
	// Реверс: `Reports/fishing_window_port.md`. Клиент `itemNet_SendItemActionPacket@0xFC9E20`
	//   (`CNetwork_Send(..., 51, OP_RECV_FISHING_TOOL_USE=6165, ...)`), сервер `Recv_FishingToolUse
	//   @0x40E230` (читает тело 0x1B по смещению пакета +33), ответ ВСЕГДА `6166` длиной 117 б.
	// ТЕЛО 27 б = [i32 userIndex][u8 actionType][ref11 УДОЧКА][ref11 ЛЕСКА/КРЮЧОК], где ref11 —
	//   тот же 11-байтовый item-ref, что у 5000/5002/5006 (`AsdaBuildItemRef11`):
	//   slot@0 i16 = запись+5, second@2 i16 = запись+7, type@4 u8 = запись+4, amount@5 i32 =
	//   запись+9, weight@9 i16 = запись+14. Сервер ищет предметы РОВНО по (type, slot).
	//   actionType: 0 = материал с `itemSubType == 208` (ЛЕСКА), 1 = `209` (КРЮЧОК).
	bool SendFishingToolUse6165(int32 InUserIndex, uint8 ActionType,
		const TArray<uint8>& RodEntry57, const TArray<uint8>& MaterialEntry57);

	// ★ 6166 (117 б) `invHandleItemSlotSyncPacket@0xFCA590` — РЕЗУЛЬТАТ 6165 и одновременно
	//   СИНХРОНИЗАЦИЯ ДВУХ СЛОТОВ. Раскладка тела (эталон читает `Src+9`, т.е. это уже payload):
	//     @0   u8  result: 1 = успех (нотис 111027), 0/2/3/4/5 = отказ сервера (см. Recv_FishingToolUse:
	//          0 = char-flag / portal-logout / рассинхрон userIdx, 2 = материал не найден,
	//          3 = удочка не найдена, 4 = материал не 208/209, 5 = удочка не (тип 9, подтип 10, вариант 1));
	//     @1   i16 новый ТЕКУЩИЙ вес персонажа;
	//     @3   57 б запись слота №1 (обновлённая УДОЧКА);
	//     @60  57 б запись слота №2 (израсходованный материал; amount@+9 == −1 ⇒ слот очищен).
	//   ⚠ Колбэк только КОПИТ (закон `no-reentrant-net-callbacks`).
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnAsdaFishingToolResult, const TArray<uint8>& /*body117*/);
	FOnAsdaFishingToolResult OnFishingToolResult;

	// ═══════ ★★★ РЫБАЛКА (волна В1, 08.10.2026): 6167/6169/6171 C→S, 6164/6168/6170/6172/6173/6183 S→C ═══
	// Реверс: `Reports/owner_1008/fishing_parts/client_net.md` §1-2, сервер — `server.md` §2. Конверт всех
	//   трёх отправителей — вид A «с секретом» (`SendSecured`, a6=1, паддинг есть); charId — i32 `SelectedCharId`.
	//   Потребитель всех приёмников — `UAsdaFishingSubsystem` (очередь → Tick), колбэки только копируют тело.
	// ★ 6167 (30 б) `acSendChallenge6167@0xFCB720`: @0 i32 charId, @4 i16 номер слота спота (зона +148).
	bool SendStartFishing6167(int32 CharId, int32 SpotSlot);
	// ★ 6169 (32 б) `netSendMaskedPacket_op6169@0xFCC150`: @0 i32 charId, @4 i16 шкала, @6 u8 всего, @7 u8 верно.
	bool SendPhaseResult6169(int32 CharId, int32 Gauge, int32 Total, int32 Correct);
	// ★ 6171 (31 б) `netSendTsMaskedReq6171@0xFCC8D0`: @0 i32 charId, @4 u8 режим (1 вытащить / 2 отмена), @5 i16 шкала.
	bool SendEndFishing6171(int32 CharId, int32 Mode, int32 Gauge);
	// Тело пакета рыбалки S→C (копия фиксированной длины; хвост-паддинг сервера отброшен).
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnAsdaFishPacket, const TArray<uint8>& /*body*/);
	FOnAsdaFishPacket OnFishSkill6164;       // 6164 (4 б)   `uiUpdateStatValueLabel@0xFCAAF0`: i32 навык
	FOnAsdaFishPacket OnFishStart6168;       // 6168 (7 б)   `netHandleTooltipNoticePacket@0xFCBD80`: u8 код, i32, i16
	FOnAsdaFishPacket OnFishGauge6170;       // 6170 (5 б)   `recvSetInputGaugeValue@0xFCC7A0`: u8 код, i16 фаза, i16 шкала
	FOnAsdaFishPacket OnFishResult6172;      // 6172 (121 б) `netOnItemUpgradeResult@0xFCCF50` (МИСНЕЙМ: результат рыбалки)
	FOnAsdaFishPacket OnFishPeer6173;        // 6173 (11 б)  `stat_RecvStatSlotUpdate@0xFCAA10` (МИСНЕЙМ: чужой рыбак)
	// 6183 (64 б) `netHandleInventoryUpdate@0xFD9DF0` — ответ на 6182 (награда книги, волна В3). В1 слушает его
	//   ТОЛЬКО ради Р2: сервер шлёт 6183 коды 4/6 и из `Recv_EndFishing` (GameServer.c:7152-7184) вместо 6172.
	FOnAsdaFishPacket OnFishBookReward6183;
	// ═══════ ★★★ КНИГА РЫБАКА (волна В3, 08.10.2026): 6176/6178/6180/6182 C→S, 6174/6175/6177/6179/6181 S→C ═══
	// Реверс: `Reports/owner_1008/fishing_parts/client_net.md` §1.5-1.8/§2.7-2.12, сервер `server.md` §2.5-2.8.
	//   Конверт всех четырёх — вид A «с секретом» (`SendSecured`, a6=1, паддинг есть); charId — i32 `SelectedCharId`.
	//   Потребитель приёмников — `UAsdaFishBookSubsystem` (очередь → Tick); 6183 — общий `OnFishBookReward6183` выше.
	// ★ 6176 (39 б) `acSendChallenge6176@0xFD7B20`: @0 i32 charId, @4 ref11 книги-предмета (запись [469]).
	bool SendFishBookRegist6176(int32 CharId, const TArray<uint8>& BookEntry57);
	// ★ 6178 (32 б) `netSendSecuredOp6178@0xFD7360` (МИСНЕЙМ опкода `OP_RECV_START_FISHING_6178` — это УДАЛЕНИЕ книги):
	//   @0 i32 charId, @4 i32 id тома (`state[g_nUpgradeListIndex] + 4`).
	bool SendFishBookDelete6178(int32 CharId, int32 PageId);
	// ★ 6180 (43 б) `netSendPacket6180@0xFD8800`: @0 i32 charId, @4 i32 ИНДЕКС тома (`[463]`, не id!),
	//   @8 ref11 стопки рыбы [466], где amount (@13) := ВВЕДЁННОЕ количество [467] (не стопка записи).
	bool SendFishBookUpdate6180(int32 CharId, int32 TomeIndex, const TArray<uint8>& FishEntry57, int32 Qty);
	// ★ 6182 (36 б) `item_sendConfirmWithCostCheck@0xFD94A0`: @0 i32 charId, @4 i32 id тома, @8 i32 код награды.
	bool SendFishBookReward6182(int32 CharId, int32 PageId, int32 RewardCode);
	FOnAsdaFishPacket OnFishBookPage6174;      // 6174 (305 б) `listParseRosterEntryPacket@0xFD71E0`: запись тома
	FOnAsdaFishPacket OnFishBookListEnd6175;   // 6175 (0 б) конец списка томов — у оригинала обработчика НЕТ (только журнал)
	FOnAsdaFishPacket OnFishBookRegist6177;    // 6177 (365 б) `invHandleItemUseResultPacket@0xFD81C0`: ответ на 6176
	FOnAsdaFishPacket OnFishBookDelete6179;    // 6179 (5 б)   `itemCollectOnRemovePacket@0xFD79B0`: ответ на 6178
	FOnAsdaFishPacket OnFishBookUpdate6181;    // 6181 (365 б) `netHandleInventoryUpdate_9020@0xFD9020`: ответ на 6180
	// ★ 6570 (4 б) `netApplySlotStateFlag@0xEDB150`: износ предмета — @0 u8 контейнер, @1 i16 слот, @3 u8 прочность.
	//   Сервер шлёт его после результата рыбалки (износ удочки 20 %, `recvDBDuraDis@0x538210`) и при любом
	//   `ItemDuraDis`. Потребитель — `UAsdaInventorySubsystem` (поле +13 записи).
	FOnAsdaFishPacket OnSlotDurability6570;

	// ═══════ ★★★ ДОСКА ОБЪЯВЛЕНИЙ — «ограниченные задания» 20000…20056, окно 75000 (28.10.2026) ═══
	//   Разбор: `Docs/Port/systems/BOARD_QUEST_FLOW.md` §5. Имена опкодов в `NetOpcodes.h` — миснеймы IDA, номера верны.
	//   Потребитель приёмников — `UAsdaBoardQuestSubsystem` (из колбэка только кладёт тело в очередь, разбор — в Tick).
	//   S→C: 6588 список узлов (N × 8; ★ тело с хвостом-паддингом сервера — целые узлы и фильтр по таблице у подсистемы),
	//        6589 пустой, 6590 результат (11 б), 6594 награда (484 б), 6595 суточный сброс (0 б).
	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnAsdaBoardPacket, int32 /*Opcode*/, const TArray<uint8>& /*Body*/);
	FOnAsdaBoardPacket OnBoardPacket;
	// ★ 6591 «взять задание» (`netSendReqDword_op6591@0xEECEB0`): конверт A «с секретом», тело i32 questId (4 б), паддинг есть.
	bool SendBoardEnter6591(int32 QuestId);
	// ★ 6593 «сдать задание» (`netSendTsMaskedReq6593@0xEED4C0`): конверт A, тело i32 questId + u8 выбор (0…3 или 99), 5 б, паддинг есть.
	bool SendBoardResult6593(int32 QuestId, int32 Choice);

	// ═══════ ★★★ САМОЦВЕТЫ (sowel): три пары опкодов. Реверс — `Reports/sowel_system_port.md` ═══
	//   ВСТАВКА/ЗАМЕНА  5024 → 5025 · ИЗВЛЕЧЕНИЕ 5022 → 5023 · «СВЯТОСТЬ» 5031 → 5032.

	// ★ 5024 (35 б = 8 + 16 + 11) — вставить/заменить камень в ГНЕЗДЕ предмета.
	//   Клиент `itemSowelSocketRequest@0xD090B0` / `itemSowelApplySend@0xD1D590`;
	//   сервер `CharCellobItemMove@0x4DC390` (`sub_523F00(a3, 0xBu, …)`, тело по `(char*)a3 + 33`):
	//     @0  u8  контейнер ПРЕДМЕТА: 1 = сумка (ShellInven), 3 = НАДЕТОЕ;
	//     @1  i16 слот предмета  — сервер: `sub_4D61E0(user, num, body[0], body[1..2])`;
	//     @3  u8  НОМЕР ГНЕЗДА 0..3 (у обычного камня всегда 0, у `SpecialFlag` — первое пустое 1..N);
	//     @4  i16 код камня, СТОЯЩЕГО в гнезде сейчас (0 = гнездо пустое) — сервер сверяет;
	//     @6  i16 код НОВОГО камня (обязан быть ≥ 7001, иначе отказ);
	//     @8  u8  контейнер камня — сервер ТРЕБУЕТ 2 (расходники);
	//     @9  i16 слот камня в контейнере 2.
	bool SendSowelSocket5024(uint8 TargetContainer, int32 TargetSlot, uint8 SocketIndex,
		int32 OldSowelCode, int32 NewSowelCode, uint8 SrcContainer, int32 SrcSlot);

	// ★ 5025 (117 б) `netRecvItemActionResult@0xD0FB10` — ответ на 5024.
	//   @0 u8 код: 1 = успех (`invApplySlotSwapResult@0xD0FF10`), 0 = отказ (нотис 2311), 2 = 1215,
	//      3 = «прочность камня кончилась» (в @1 приходит ПРЕЖНЯЯ запись предмета), 4 = 1216,
	//      5 = 2045, 6 = 1217, 7 = 1218, 8 = 1219, 9 = 8013, 0xA = 1220, 0xB = 1221, 0xF = 111303
	//      (сервер шлёт 15, когда код камня в пакете не совпал с содержимым слота);
	//   @1  57 б — НОВАЯ запись предмета-носителя (её +4 контейнер, +5 слот говорят, куда класть);
	//   @58 57 б — запись ВЫНУТОГО камня, возвращаемого в контейнер 2 (её +5 = слот);
	//   @115 i16 — слот контейнера 2, который надо ОЧИСТИТЬ, если запись @58 пуста (камень израсходован).
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnAsdaSowelSocketResult, const TArray<uint8>& /*body117*/);
	FOnAsdaSowelSocketResult OnSowelSocketResult;

	// ★ 5022 (28 б = 8 + 16 + 4) — ИЗВЛЕЧЬ камень из гнезда (`invItemDropSendPacket@0xD06BA0`,
	//   он же «OK» диалога 2002 → `netSendItemActionRequest@0xD1CE30`). Сервер `CharCellobItemDetach@0x4DBC30`.
	//     @0 u8 контейнер предмета (1 сумка / 3 надетое) · @1 i16 слот · @3 u8 НОМЕР ГНЕЗДА.
	//   ★ Гнездо 0 эталон извлекать НЕ даёт (`if (invCtx[200])`) — камень «основы» вынимается только заменой.
	bool SendSowelDetach5022(uint8 TargetContainer, int32 TargetSlot, uint8 SocketIndex);

	// ★ 5023 (115 б) `netRecvSowelActionResult@0xD0F7D0` — ответ на 5022.
	//   @0 u8 код: 1 = успех (нотис 20120 с разницей веса, звук 11, пересборка свечения),
	//      2 = НУЖНО ПОДТВЕРЖДЕНИЕ (двухкнопочный msgbox, нотис 2002; «OK» повторяет 5022),
	//      4 = нотис 2003, 5 = нотис 8013;
	//   @1  57 б новая запись предмета · @58 57 б запись вынутого камня (её +5 = слот контейнера 2);
	//   @5 u8 контейнер предмета, @6 i16 слот предмета, @14 u8 прежний вес записи (для нотиса 20120).
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnAsdaSowelDetachResult, const TArray<uint8>& /*body115*/);
	FOnAsdaSowelDetachResult OnSowelDetachResult;

	// ★ 5031 (30 б = 8 + 16 + 6) — применить «СВЯТОЙ» предмет (itemType 11) к КАМНЮ в контейнере 2.
	//   Клиент `net_sendSecuredRequest_5031@0xD167A0`, сервер `CharSpendHolyItemUse@0x4DE590`
	//   (`sub_523F00(arg4, 6u, …)`): @0 u8 контейнер святого предмета (клиент шлёт 2), @1 i16 его слот,
	//   @3 u8 контейнер камня (клиент шлёт 2), @4 i16 слот камня.
	bool SendHolyItemUse5031(uint8 HolyContainer, int32 HolySlot, uint8 SowelContainer, int32 SowelSlot);

	// ★ 5032 (63 б) `sowelWndOnSocketResultPacket@0xD15EE0` — ответ на 5031.
	//   @0 u8 код (0 = ничего не вышло, 1 = успех, 2..8 — отказы), @1 i16 новый текущий вес,
	//   @3 u8 контейнер израсходованного святого предмета, @4 i16 его слот,
	//   @6 57 б новая запись камня (её +5 = слот контейнера 2, куда её класть).
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnAsdaHolyItemResult, const TArray<uint8>& /*body63*/);
	FOnAsdaHolyItemResult OnHolyItemResult;

	// ═══════════ ★★★ ВОЙНА ФРАКЦИЙ ЗА ГОРОДА (Unicon/RoseWar) — ПРИНАДЛЕЖНОСТЬ И НАЛОГ ═════════
	// ОПКОД: **6743** (`0x1A57`, `OP_UNICON_WAR_UPDATE_FACTION_STATE_UI`), тело **274 байта**.
	//   Клиент: `uniconWar_UpdateFactionStateUI@0x9C8B90` (ui_core.c:3900) — `memcpy(dst, Src+9, 0x112)`.
	//   Сервер: `Send_RoseWarOwnerInfo@0x46B060` — `sub_4C28F0(…, 6743, 274, …)`, рассылка ВСЕМ
	//   вошедшим в мир; обновляется по `Chat_Game_OwnerInfoUpdate` (приход данных от чат-сервера).
	//
	// ТЕЛО: `[u8 результат]` + **3 записи по 91 байту**. 1 + 3·91 = 274 ✔.
	//   Раскладка записи снята ДВУМЯ методами — по адресам глобалов клиента
	//   (`unk_29087E0` = байт 0, `g_UniconWarFactionState` = 0x29087E1, `g_UniconWarOptPercent` =
	//   0x29087E3, `g_UniconWarFactionName` = 0x29087E9 и 0x29087FF) и по серверному логу
	//   `"Type:%d, Owner:%d, Clan:%s(%d), Tax:%d"` / `"MasterName:%s, MasterIdx:%d, CharIdx:%d"`:
	//     +0  i16 **фракция-хозяин города** (3 = НЕ ЗАНЯТ; клиент ещё превращает −1 в 3)
	//     +2  u8  **налог/скидка, проценты**
	//     +3/+4/+5 u8 очки победы 1/2/3
	//     +6  i16 id клана-хозяина · +8  строка имя клана
	//     +25 i32 индекс мастера   · +29 u8 индекс персонажа · +30 строка имя мастера
	//
	// ПОРЯДОК ЗАПИСЕЙ = ПОРЯДОК ГОРОДОВ, и он ЖЁСТКИЙ (`uniconWar_UpdateFactionStateUI`
	//   печатает для i = 0/1/2 нотисы 603/600/707, а `social.c:4102-4126` вешает те же 603/600/707
	//   на контролы 79901/79902/79903):
	//     слот 0 — **Альпия** (loc 603), карта `g_nCurMapId` == 3
	//     слот 1 — **Силарис** (loc 600), карта 0
	//     слот 2 — **Пламения** (loc 707), карта 7
	//   ⚠ Имена контролов `.JUI` при этом ВРУТ: `Occupation_flammio` (79902) — это СИЛАРИС,
	//   `Occupation_Silalis` (79903) — ПЛАМЕНИЯ (закон `false-negatives-method`: доказано совпадением
	//   loc-идентификаторов подсказки с порядком слотов пакета).
	//
	// КОДЫ ФРАКЦИЙ: 0 = Свет, 1 = Тьма, 2 = Хаос, 3 = не занят. Подтверждено дважды: эталон строит
	//   локализованное имя как `код + 10191` (10191 «Свет», 10192 «Тьма», 10193 «Хаос»), и то же
	//   соответствие уже записано в порту у `UAsdaWorldSubsystem::SelfCampId`.
	struct FAsdaCityOwner
	{
		int32   OwnerFaction = 3;   // +0  (3 = город не занят)
		int32   TaxPercent   = 0;   // +2
		int32   ClanId       = -1;  // +6
		FString ClanName;           // +8
		// ★ 06.10.2026 (волна 2-Г): глава клана-хозяина — +25 i32 charId (`dword_29087FA`), +29 u8 индекс персонажа
		//   (`byte_29087FE`). Ими окно города 79300 решает «я глава» (`uiCampWarPanelTick@0xBCE4B0`, ui_core.c:17573:
		//   `== g_LocalPlayer+1052 && == g_LocalPlayer+1076`); сервер сверяет те же поля (`User_Game_TaxChange`, +40/+44).
		int32   MasterCharId  = -1; // +25
		int32   MasterCharIdx = -1; // +29
		FString MasterName;         // +30
		// ★★★ ЭМБЛЕМА КЛАНА-ПОБЕДИТЕЛЯ ЕДЕТ ПРЯМО ЗДЕСЬ, второго пакета нет.
		//   +50 u8  — «у клана есть флаг» (эталон: `if (byte_2908813[91*i] > 0) …`);
		//   +51..+90 — ТЕ ЖЕ 40 БАЙТ ДИЗАЙНА, что в 6542 и 4135 (`FAsdaClanFlagData::FromWire`).
		//   Доказано адресной арифметикой читателей в `uniconWar_UpdateFactionStateUI@0x9C8B90`
		//   (база записи 0x29087E1): 0x2908813 → +50, 0x2908814 → +51 (BorderIdx = flag40+0),
		//   0x290881C → +59 (BorderColor = +8), 0x2908824 → +67 (BgColor = +16),
		//   0x290882C → +75 (SignIdx = +24), 0x2908834 → +83 (SignColor = +32).
		//   51 + 40 = 91 — ровно длина записи, хвост занят целиком.
		bool bFlagOn = false;
		TArray<uint8> FlagDesign40;   // пусто ⇒ дизайна нет
	};
	// Слоты 0/1/2 = Альпия / Силарис / Пламения. `bCityOwnerReceived` = пакет 6743 уже приходил.
	FAsdaCityOwner CityOwner[3];
	bool bCityOwnerReceived = false;

	// Фракция-хозяин города по слоту (3 = не занят / данных нет).
	UFUNCTION(BlueprintPure, Category="AsdaNet")
	int32 GetCityOwnerFaction(int32 Slot) const
	{ return (Slot >= 0 && Slot < 3) ? CityOwner[Slot].OwnerFaction : 3; }
	// Процент налога/скидки города по слоту.
	UFUNCTION(BlueprintPure, Category="AsdaNet")
	int32 GetCityTaxPercent(int32 Slot) const
	{ return (Slot >= 0 && Slot < 3) ? CityOwner[Slot].TaxPercent : 0; }
	UFUNCTION(BlueprintPure, Category="AsdaNet")
	bool HasCityOwnerInfo() const { return bCityOwnerReceived; }

	// Дизайн флага клана-хозяина города (40 байт формата `FAsdaClanFlagData::FromWire`).
	//   nullptr — у города нет клана либо флаг выключен: эталон в этом случае ничего не печёт и
	//   `UI_LoadClanIcon@0x9C9250` уходит в ветку `g_pDefaultClanIconTex` (родная текстура).
	const TArray<uint8>* GetCityClanFlagDesign(int32 Slot) const
	{
		if (Slot < 0 || Slot >= 3) { return nullptr; }
		if (!CityOwner[Slot].bFlagOn || CityOwner[Slot].FlagDesign40.Num() < 40) { return nullptr; }
		return &CityOwner[Slot].FlagDesign40;
	}

	// Пришло новое состояние принадлежности городов (виджет занятости и магазин перерисовываются).
	DECLARE_MULTICAST_DELEGATE(FOnAsdaCityOwnerInfo);
	FOnAsdaCityOwnerInfo OnCityOwnerInfo;

	// ★ ОФЛАЙН-ПОДАЧА 6743 (консоль `asda.War.Fake`): собирает эталонное тело 274 б и проводит его
	//   ЧЕРЕЗ ШТАТНЫЙ разбор — второго источника истины не заводим.
	void SimulateCityOwnerInfo(const TArray<int32>& Factions3, const TArray<int32>& TaxPercents3);
	// ★ Офлайн-подача 6743 С ДИЗАЙНОМ ФЛАГА клана-хозяина одного города (консоль `asda.War.FakeFlag`):
	//   те же 40 байт формата `FAsdaClanFlagData`, что приходят с сервера в хвосте записи (+51…+90).
	void SimulateCityClanFlag(int32 Slot, int32 BorderIdx, int32 BorderColor,
	                          int32 BgColor, int32 SignIdx, int32 SignColor);
	// Разбор тела 6743 (одна точка на сеть и на консольную подачу).
	void ApplyCityOwnerBody(const TArray<uint8>& Body274);

	// ═══════════════════ МАГАЗИН NPC (окно NPCSHOP_WIN 1300) ═══════════════════════════════════
	// Реверс потока целиком — `Reports/npc_shop_port.md`. Тела читаются от начала Body
	//   (заголовок 9 б уже снят разбором пакета), поэтому эталонные смещения «Src+9» здесь = 0.

	// ★ 5020 `OP_CHAR_ITEM_BUY` (66 б = 8 секрет + 16 MD5 + 42 тело) — КУПИТЬ КОРЗИНУ.
	//   Эталон `Send_CharItemBuy_1@0xE2A240` (network.c:28269). Тело — 7 записей по 6 байт:
	//     @6·i+0 i32 itemCode · @6·i+4 u16 количество.
	//   ⚠ Записей в теле СЕМЬ (сервер `CharItemBuy@0x50F170` крутит `for (i = 0; i < 7; ++i)` по
	//     `(char*)buyReq + 6*i` и проверяет длину `sub_523F00(a3, 0x2A, …)` = 42), а корзина окна
	//     вмещает ПЯТЬ — оставшиеся две записи эталон шлёт нулями. Повторяем 1:1: длина тела
	//     фиксированная 42, лишние записи нулевые (иначе не сойдётся MD5).
	//   ⚠ Количество эталон принудительно ставит в 1 для самоцвета и для предмета с ненулевым
	//     `ItemRecord->flagWord176` — это делает ВЫЗЫВАЮЩИЙ (подсистема магазина), здесь тела не правим.
	bool SendCharItemBuy5020(const TArray<int32>& ItemCodes, const TArray<int32>& Quantities);

	// ★ 5021 `OP_SHOP_SELL_RESULT_0` (406 б) — ОТВЕТ на 5020 (`Recv_ShopSell_Result@0xE28FD0`).
	//   @0 u8 код: 1 = успех; 0 = нотис 9570; 2 = 2233; 3 = 1012; 4 = 2048; 5 = 9569.
	//   @1 + 57·i — СЕМЬ записей предмета по 57 б (клиент разбирает ПЯТЬ — больше корзина не даёт);
	//   @400 i32 новые деньги игрока · @404 i16 новый текущий вес.
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnAsdaShopBuyResult, const TArray<uint8>& /*body406*/);
	FOnAsdaShopBuyResult OnShopBuyResult;

	// ★ 5018 `OP_CHAR_ITEM_SELL` (79 б = 8 + 16 + 55) — ПРОДАТЬ КОРЗИНУ.
	//   Эталон `Send_CharItemSell@0xE32800` (network.c:28585). Тело — 5 записей по 11 байт, и это
	//   тот же 11-байтовый item-ref, что у 5000/5002/5006:
	//     @11·i+0 i16 слот записи (`запись+5`) · @11·i+2 i16 второй индекс (`запись+7`)
	//     @11·i+4 u8 контейнер (`запись+4`)   · @11·i+5 i32 количество (`запись+9`)
	//     @11·i+9 i16 вес стопки (`запись+14`).
	//   Сервер `CharItemSell@0x50D240` читает ровно 0x37 и адресует контейнер байтом @+4.
	bool SendCharItemSell5018(const TArray<uint8>& Refs11x5);

	// ★ 5019 `OP_SHOP_BUY_RESULT` (62 б) — ОТВЕТ на 5018 (`Recv_ShopBuy_Result@0xE31630`).
	//   @0 u8 код: 1 = успех; 0 = нотис 2234; 2 = 8013.
	//   @1 + 11·i — ПЯТЬ item-ref по 11 б (та же раскладка, что в запросе; слот < 0 ⇒ запись пуста);
	//   @56 i32 новые деньги игрока · @60 i16 новый текущий вес.
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnAsdaShopSellResult, const TArray<uint8>& /*body62*/);
	FOnAsdaShopSellResult OnShopSellResult;

	// ═══════ ★★★ РЕМОНТ СНАРЯЖЕНИЯ — окно `INVEN_Repair_Win` (JUI 57300), коды кнопки NPC 30069 ═══
	//   Полный реверс с адресами — `Reports/repair_and_warshop_port.md` §1.

	// ★ 6571 `OP_RECV_USER_DURA_REGEN` (147 б = 8 секрет + 16 MD5 + 123 тело) — ПОЧИНИТЬ.
	//   Эталон `net_SendSlotBatchReq@0xEDA7A0` (network.c:32512). Тело РОВНО 123 байта:
	//     @0…40   41 × u8  — контейнер записи (поле записи +4);
	//     @41…122 41 × i16 — слот записи      (поле записи +5).
	//   Записей ровно столько, сколько чинится: режим 1 — одна, режим 2 — 11 надетых,
	//   режим 3 — 11 надетых + 30 ячеек сумки. Хвост уходит НУЛЯМИ (длина тела фиксированная,
	//   иначе не сойдётся MD5) — поэтому обе колонки передаются массивами одинаковой длины,
	//   а добивку делает сам метод.
	//   ⚠ Ноль в колонке контейнера — законное значение «записи нет»: сервер ориентируется на пару
	//   (контейнер, слот) и нулевую пару отбрасывает сам.
	bool SendRepair6571(const TArray<uint8>& Containers, const TArray<int32>& Slots);

	// ★ 6572 `OP_NET_HANDLE_STATE_ARRAY_PACKET` (тело 170 б) — ОТВЕТ на 6571
	//   (`netHandleStateArrayPacket@0xEDB2B0`, network.c:32731):
	//     @0    i16 код: 1 = успех, 0 и 10 = ошибка (нотис 110315 с этим кодом), 11 = нет золота (2050);
	//     @2…42    41 × u8  — контейнер записи;
	//     @43…124  41 × i16 — слот записи (−1 ⇒ запись не используется);
	//     @125…165 41 × u8  — НОВАЯ ПРОЧНОСТЬ (поле записи +13) соответствующего слота;
	//     @166  i32 — НОВОЕ ЗОЛОТО игрока.
	//   ⚠ Колбэк только КОПИТ (закон `no-reentrant-net-callbacks`).
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnAsdaRepairResult, const TArray<uint8>& /*body170*/);
	FOnAsdaRepairResult OnRepairResult;

	// ═══ ★★★ ПЕРЕНОС УЛУЧШЕНИЯ КАМНЕМ (special_itemlist SubType 204 оружие / 206 доспех) ═══ (31.10.2026, баг 4)
	//   Поток и логика — `UAsdaUpgradeStoneSubsystem` (шапка `UI/AsdaUpgradeStoneSubsystem.h`).
	//
	// ★ 6551 `OP_RECV_USER_UPGRADE_BLESS` (40 б = 8 секрет + 16 MD5 + 16 тело) — ПРИМЕНИТЬ КАМЕНЬ.
	//   Эталон `netSendAntiCheatChallengeResponse@0xB65D30` (имя IDA врёт; network.c:8815). Тело 16 б — две
	//   ссылки по 8 б, порядок ЖЁСТКИЙ (сервер `Recv_UserUpgradeBless@0x5358C0`, GameServer.c:118125: первая —
	//   `sub_532B10` «код спец-предмета», вторая — `sub_532A90` «код ≥ 20551»):
	//     @0  i32 код КАМНЯ (`this+432`) · @4 i16 слот (+5) · @6 u8 контейнер (+4), @7 = 0
	//     @8  i32 код СНАРЯЖЕНИЯ (`this+428`) · @12 i16 слот · @14 u8 контейнер, @15 = 0
	//   Раскладка та же, что у 6553 (`HIWORD = *(u8*)(rec+4)`); MD5 — по этим 16 байтам.
	bool SendUpgradeBless6551(int32 StoneItemCode, int32 StoneContainer, int32 StoneSlot,
	                          int32 GearItemCode,  int32 GearContainer,  int32 GearSlot);

	// ★ 6552 `OP_INVENTORY_PARSE_ITEM_UPDATE_PACKET` (тело 119 б) — ОТВЕТ на 6551
	//   (`inventory_parseItemUpdatePacket@0xB65780`, item_inventory.c:4838; сервер `recvDBUpgradeBless@0x535FB0`):
	//     @0      i8 код: 1 перенос удался · 5 не удался (камень пропал, вещь прежняя) · 0 108530 · 2 108532 (камень) ·
	//                 3 108533 (снаряжение) · 4 108534 (пустой камень) · 99 108535 (БД); иное — молча;
	//     @1…57   57 б — запись КАМНЯ (код 0 ⇒ ячейку очистить) · @58…114 57 б — запись СНАРЯЖЕНИЯ;
	//     @115    i32 — НОВЫЙ ВЕС (эталон `HUD_UpdateMiniWeightBar` в ветках 1 и 5 БЕЗ гейта `> 0`).
	//   Вес сеть ставит сама; запись и нотисы — подсистема камней. ⚠ Колбэк только КОПИТ (`no-reentrant-net-callbacks`).
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnAsdaUpgradeBlessResult, const TArray<uint8>& /*body119*/);
	FOnAsdaUpgradeBlessResult OnUpgradeBlessResult;

	// ★ 6549 `OP_RECV_USER_UPGRADE_EXTRACTION` (96 б = 8 секрет + 16 MD5 + 72 тело) — РАЗДЕЛИТЬ УЛУЧШЕНИЕ (окно 50500).
	//   Эталон `antiCheat_sendEquipMaskDigest@0xB664A0` (имя IDA врёт; security.c:651), заготовка `sub_B66B90` = всё −1:
	//     @0   8 × i32 коды ПУСТЫХ КАМНЕЙ (`this+95…102`) · @32 8 × i16 их слоты (+5) · @48 8 × i16 контейнеры (+4)
	//     @64  i32 код ПРЕДМЕТА (`this+94`) · @68 i16 слот · @70 i16 контейнер.
	//   Неиспользованные камни остаются −1 — сервер (`Recv_UserUpgradeExtraction@0x534640`, GameServer.c:117761) их пропускает.
	//   MD5 — по этим 72 байтам. `Stones` — до 8 записей сумки по 57 б (код/контейнер/слот берутся из записи).
	bool SendUpgradeExtraction6549(const TArray<TArray<uint8>>& Stones, int32 ItemCode, int32 ItemContainer, int32 ItemSlot);

	// ★ 6550 `OP_NET_HANDLE_ITEM_OP_RESULT_PACKET` (тело 577 б) — ОТВЕТ на 6549 (`netHandleItemOpResultPacket@0xB66C20`,
	//   network.c:9001; сервер `recvDBUpgradeExtraction@0x535260`):
	//     @0    i8 код: 1 успех · 0 108600 · 2 108602 (камни) · 3 108603 (предмет) · 4 108604 (не улучшен) · 5 108605
	//                (камней мало) · 6 нет места в сумке (оригинал молчит) · 98 108606 · 99 108607;
	//     @1…570  10 записей по 57 б: [0] новый КАМЕНЬ, [1] ПРЕДМЕТ, [2…9] пустые камни после списания;
	//     @571  i32 НОВЫЙ ВЕС (эталон ставит при коде 1 без гейта) · @575 i8 исход 0 «отлично» / 1 «успех» / 2 «провал» /
	//           3 «крах» · @576 i8 потрачено пустых камней.
	//   Вес сеть ставит сама; записи, нотисы, окно — `UAsdaUpgradeSplitSubsystem`. ⚠ Колбэк только КОПИТ.
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnAsdaUpgradeExtractionResult, const TArray<uint8>& /*body577*/);
	FOnAsdaUpgradeExtractionResult OnUpgradeExtractionResult;

	// ═══ ★★★ ИЗМЕНЕНИЕ ПАРАМЕТРОВ ПРЕДМЕТА — окно `ItemOptionValue_WIN` (JUI 50300), код 30060 ═══
	//   Полный реверс — `Reports/npc_exchange_params_30060.md`.
	//
	// ★ 6553 `OP_RECV_USER_ITEM_OPTION_TRANS` (40 б = 8 секрет + 16 MD5 + 16 тело) — ИЗМЕНИТЬ.
	//   Эталон `itemSendUpgradeRequest@0xB611E0` (item_inventory.c:3830). Тело РОВНО 16 байт —
	//   ДВЕ ссылки на предмет по 8 б, и порядок в них ЖЁСТКИЙ (сервер `Recv_UserItemOptionTrans@0x536460`
	//   проверяет первую через `sub_532B10` = «код 1…N», то есть таблица special_itemlist, а вторую
	//   через `sub_532A90` = «код ≥ 20551», то есть item_list):
	//     @0  i32 код СВИТКА обмена параметрами (special_itemlist, SubType 67)
	//     @4  i16 слот свитка          (поле записи +5)
	//     @6  u8  контейнер свитка     (поле записи +4)   · @7 старший байт слова = 0
	//     @8  i32 код ИЗМЕНЯЕМОГО снаряжения (item_list)
	//     @12 i16 слот снаряжения      (поле записи +5)
	//     @14 u8  контейнер снаряжения (поле записи +4)   · @15 = 0
	//   ⚠ Эталон кладёт контейнер в СТАРШЕЕ слово через `HIWORD(pair[1]) = *(u8*)(rec+4)`, поэтому
	//     байт @7/@15 — это старший байт контейнера, и он всегда 0.
	//   MD5 считается ИМЕННО по этим 16 байтам (`Net_Md5Digest16(&g_NetMd5Salt, pair, 0x10, dig)`).
	bool SendItemOptionTrans6553(int32 ScrollItemCode, int32 ScrollContainer, int32 ScrollSlot,
	                             int32 GearItemCode,   int32 GearContainer,   int32 GearSlot);

	// ★ 6554 `OP_NET_ON_ITEM_OPTION_TRANS_RESULT` (тело 119 б) — ОТВЕТ на 6553
	//   (`Net_OnItemUpgradeResult@0xB61990`, item_inventory.c:3880):
	//     @0        i8  код: 1 успех · 0 «Ошибка параметра» (109700) · 2 «Ошибка данных свитка»
	//                   (109702) · 3 «Ошибка данных снаряжения» (109703) · 98 «Ошибка БД» (109704) ·
	//                   99 «Ошибка вызова процедуры» (109705);
	//     @1…57     57 б — запись СВИТКА после списания (код 0 ⇒ стопка кончилась, слот очистить);
	//     @58…114   57 б — запись СНАРЯЖЕНИЯ с НОВЫМИ параметрами (+29/+31 и +33/+35);
	//     @115      i32 — НОВЫЙ ТЕКУЩИЙ ВЕС игрока (эталон зовёт `HUD_UpdateMiniWeightBar`, и
	//                   ТОЛЬКО при значении > 0 — в соседнем 6550 такого гейта нет).
	//   ⚠ Колбэк только КОПИТ (закон `no-reentrant-net-callbacks`).
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnAsdaItemOptionTransResult, const TArray<uint8>& /*body119*/);
	FOnAsdaItemOptionTransResult OnItemOptionTransResult;

	// ═══ ★★★ ПЕРЕДАЧА ПАРАМЕТРОВ — окно `EXTRACT_WIN` (JUI 16000), код кнопки NPC 30035 ═══
	//   Полный реверс — `Reports/npc_transfer_params_30035.md`.
	//
	// ★ 5433 `OP_RECV_SHELL_ITEM_OPTION_EXTRACTION` (49 б = 8 секрет + 16 MD5 + 25 тело).
	//   Эталон `itemExtract_sendCombineRequest@0xC921D0` (item_inventory.c:25765); сервер
	//   `Recv_ShellItemOption_Extraction@0x513F80` требует РОВНО 0x19 (`sub_523F00(a3, 0x19u, …)`)
	//   и считает MD5 по этим же 25 байтам. Тело — ДВЕ 11-байтовые ссылки на предмет плюс свиток:
	//     @0  i16 слот ИСТОЧНИКА       (поле записи +5)   — у кого параметры ЗАБИРАЮТ
	//     @2  i16 второй индекс        (поле записи +7)
	//     @4  u8  контейнер источника  (поле записи +4)   — сервер требует РОВНО 1 (сумка снаряжения)
	//     @5  i32 количество           (поле записи +9)
	//     @9  i16 вес стопки           (поле записи +14)
	//     @11…@21 — то же для ПРИЁМНИКА (кому параметры ОТДАЮТ); контейнер тоже обязан быть 1
	//     @22 i16 слот СВИТКА-бустера  (поле записи +5); 0xFFFF ⇒ свиток не используется
	//     @24 u8  контейнер свитка     (поле записи +4); 0xFF  ⇒ свиток не используется
	//   ⚠ Эталон инициализирует ХВОСТ ссылок значением −1 (`helper_initInvalidIds`), а поля @22/@24
	//   заполняет ТОЛЬКО когда включён чекбокс 16412 И выбранная строка списка имеет
	//   `special_itemlist.SubType == 96`. Ноль здесь — НЕ «нет свитка», он адресует слот 0.
	bool SendItemOptionExtraction5433(const TArray<uint8>& SrcRecord57, const TArray<uint8>& DstRecord57,
	                                  const TArray<uint8>& ScrollRecord57);

	// ★ 5434 `OP_EXTRACT_SHOP` (тело 184 б) — ОТВЕТ на 5433 (`Recv_ExtractShop@0xC92AD0`).
	//   Раскладка совпадает у клиента (`uiSlotContainer_Init@0xC93570`) и у сервера (`sub_517320`):
	//     @0    u8  код результата;
	//     @1    57 б — запись ИСТОЧНИКА (сервер его УЖЕ УДАЛИЛ; клиент чистит его слот);
	//     @58   57 б — запись ПРИЁМНИКА с новыми параметрами (только при коде 9);
	//     @115  57 б — запись СВИТКА после списания (код 0 в ней ⇒ стопка кончилась);
	//     @172  i16 — новый текущий ВЕС · @174 i32 — новое ЗОЛОТО · @178 i32 (клиент не читает);
	//     @182  i16 — userNum адресата (клиент сверяет со своим).
	//   Коды: 9 успех (13809), 10 неудача (13810), 7 мало золота (13807), 0 общий отказ (13800),
	//   1…6 — 13801…13806 (эта сборка сервера их не шлёт), прочее — 13808.
	//   ⚠ Колбэк только КОПИТ (закон `no-reentrant-net-callbacks`).
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnAsdaExtractShopResult, const TArray<uint8>& /*body184*/);
	FOnAsdaExtractShopResult OnExtractShopResult;

	// ═══ ★★★ ЗНАК ЗОДИАКА — окно `Constellation_S_WIN` (JUI 74500), код кнопки NPC 30073 ═══
	//   Полный реверс — `Reports/npc_button_30073.md`.
	//
	// ★ 6586 `OP_RECV_USER_STAR_POINT_UPDATE` (29 б = 8 секрет + 16 MD5 + 5 тело).
	//   Эталон `netSendMaskedPacket_op6586@0xECC0C0` (network.c:32239):
	//     @0 i32 userIdx  (`CharManager_GetLocalPlayer()->userIdx`);
	//     @4 u8  выбранный знак 1…12 (поле окна `this+0x20`, = индекс списка + 1).
	//   Сервер `Recv_UserStarPointUpdate@0x539210` требует РОВНО 5 (`sub_523F00(a3, 5u, …)`) и
	//   читает ТОЛЬКО байт @4 — четыре байта userIdx он игнорирует (шлём 1:1, как у 4050).
	bool SendConstellationSelect6586(uint8 ConstellationId);

	// ★ 6587 `OP_ITEM_GRADE_NOTICE_HANDLE_RESULT` (тело 6 б) — ОТВЕТ на 6586.
	//   Приёмник эталона — `itemGradeNotice_HandleResult@0xECC6F0` (item_inventory.c:64662);
	//   отправители сервера ровно два (`GameServer.c:119409` и `:119457`), раскладка тела:
	//     @0    u8  код результата (1 успех · 2 знак уже стоит · 99 ошибка БД);
	//     @1…4  i32 charId (сервер кладёт `CUser+372`, клиент НЕ сверяет);
	//     @5    u8  назначенный знак 1…12 (значим только при коде 1).
	//   ⚠ Клиент обрабатывает ещё код 98, но НИ ОДИН отправитель GameServer его не шлёт.
	//   ⚠ Колбэк только КОПИТ (закон `no-reentrant-net-callbacks`).
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnAsdaConstellationResult, const TArray<uint8>& /*body6*/);
	FOnAsdaConstellationResult OnConstellationResult;

	// ═══ ★★★ СБРОС/ВОССТАНОВЛЕНИЕ УЛУЧШЕНИЙ — окно `Item_Initializ_Win` (JUI 20200) ═══
	//   Коды кнопок NPC 30046 (режим 0) и 30047 (режим 1). Полный реверс с адресами —
	//   `Reports/npc_reset_upgrade_30046.md`.

	// ★ 6506 `OP_RECV_USER_SHELL_ITEM_UPGRADE_RESET` (60 б = 8 секрет + 16 MD5 + 36 тело).
	//   Эталон `netSendPacket6506@0xD30D30` (network.c:19844); сервер
	//   `Recv_User_ShellItemUpgradeReset` требует РОВНО 0x24 (`sub_523F00(a3, 0x24u, …)`)
	//   и читает тело как `__int16 v20[18]` — байт в байт с клиентским `_WORD payload[18]`:
	//     @0…1  i16 — эталон оставляет 0 (инициализатор `sub_D313B0@0xD313B0`);
	//     @2    u8  — РЕЖИМ: 0 = полный сброс за золото, 1 = возврат попыток за свитки;
	//     @3    u8  — контейнер ЦЕЛЕВОГО предмета (эталон всегда пишет 1 = сумка);
	//     @4…5  i16 — СЛОТ целевого предмета (поле записи +5);
	//     @6…15 10×u8  — контейнеры материалов (эталон пишет 1 во все десять безусловно);
	//     @16…35 10×i16 — слоты материалов.
	//   ★★★ ХВОСТ: инициализатор ставит контейнер 0 и слот −1 для ВСЕХ десяти записей
	//   (и для целевой тоже). Ноль в слоте для сервера ВАЛИДЕН — ровно на этом ремонт получал
	//   отказ «предмет не найден», поэтому неиспользуемые слоты обязаны уходить как −1.
	//   ⚠ ДЕФЕКТ ОРИГИНАЛА: в режиме 0 эталон передаёт адрес ОДНОГО `int singleSlotId = −1`,
	//   а упаковщик читает оттуда десять — то есть слоты @18…35 уходят мусором со стека.
	//   Безвредно (сервер весь материальный блок при режиме 0 пропускает), но мы шлём −1.
	bool SendItemReset6506(uint8 Mode, int32 TargetSlot, const TArray<int32>& MaterialSlots);

	// ★ 6507 `OP_UI_INVENTORY_ON_RECV_SLOT_UPDATE` — ОТВЕТ на 6506.
	//   Обработчик эталона — `uiInventoryOnRecvSlotUpdate@0xD31430` (ui_core.c:49739).
	//   ★ Тело ВСЕГДА 154 б (0x9A) — и на успех, и на отказ: сервер шлёт его одним и тем же
	//   `sub_4C28F0(…, 6507, 154, …)`, при отказе значим только байт 0.
	//     @0   u8  — код: 1 успех · 0 общий отказ (БЕЗ нотиса!) · 4 нет золота · 5 нет свитков
	//                · 6 «не удаётся обнулить». Коды 2/3 клиент разбирает, но сервер их не шлёт;
	//     @1   i16 — новый ВЕС игрока · @3 i32 — новое ЗОЛОТО игрока;
	//     @7   0x38 — новая запись ЦЕЛЕВОГО предмета · @63 u8 — 57-й байт этой записи;
	//     @64+9i (i=0…9) — 10 записей материалов: u8 контейнер · i16 слот
	//                      · i32 количество (0 ⇒ слот очистить) · i16 доп. число.
	//   ⚠ Колбэк только КОПИТ (закон `no-reentrant-net-callbacks`).
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnAsdaItemResetResult, const TArray<uint8>& /*body154*/);
	FOnAsdaItemResetResult OnItemResetResult;

	// ═══ ★★★ УЛУЧШЕНИЕ СНАРЯЖЕНИЯ (ЗАТОЧКА) — окно `Item_Upgrade_Win` (JUI 20000), 25.09.2026 ═══
	//   Полный реверс с адресами — `Reports/gear_upgrade_re_report.md` (§5 протокол).
	//   Окно — `UAsdaItemUpgradeSubsystem`.

	// ★ 6504 `OP_CHAR_SHELL_ITEM_UPGRADE_6504` (36 б = 8 секрет + 16 MD5 + 12 тело).
	//   Эталон `netSendBoneMaskRequest@0xD2AAB0` (ИМЯ IDA ВРЁТ — это отправитель заточки,
	//   network.c:19454, отправка `:19600` = `CNetwork_Send(…, 36, 6504, 1, 1, 1, 0, 0)` — конверт
	//   «с секретом» и паддинг включён, ровно `SendSecured(…, bPad = true)`).
	//   Сервер `CharShellItemUpgrade_0@0x4FEE50` сверяет MD5 профилем a5 = 1 РОВНО по 12 байтам
	//   (`sub_523F00(a3, 0xCu, arg0, 1)`, GameServer.c:98785): хеш с `+33`, сверка с `+17` — это и
	//   есть раскладка `SendSecured` (секрет @0…7, MD5 @8…23, тело @24 ⇒ кадр +9/+17/+33).
	//   Тело — 4 пары {u8 контейнер, i16 слот}, упакованные без выравнивания (`sub_9392FB` =
	//   `sub_D2B170@0xD2B170` — инициализатор ставит контейнер 0 и слот −1 во ВСЕ четыре пары):
	//     @0 u8  контейнер улучшаемого предмета — эталон ВСЕГДА пишет 1 (сервер: `!= 1` → код 4);
	//     @1 i16 слот улучшаемого предмета (поле записи +5);
	//     @3 u8  контейнер свитка/купона (поле записи свитка +4);
	//     @4 i16 слот свитка/купона (+5);
	//     @6 u8  контейнер помощника subType 97 (`this+3384`, асм `[eax+0xD38]`);
	//     @7 i16 его слот;
	//     @9 u8  контейнер помощника subType 98 (`this+3388`, асм `[eax+0xD3C]`);
	//     @10 i16 его слот.
	//   ★★★ СЕНТИНЕЛ −1 в слоте НЕИСПОЛЬЗОВАННОГО помощника обязателен: слот 0 для сервера ВАЛИДЕН
	//   (`sub_4D61E0` нашёл бы предмет в ячейке 0 и списал бы его как купон). Передавай −1 и контейнер 0.
	//   ★ В теле НЕТ ни charId (i32), ни userNum (i16): userNum сервер штампует сам по сокет-слоту
	//   (`CopyToWriteBuffer@0x5656D0`), charId живёт только в пакете GS↔DB 2304.
	bool SendItemUpgrade6504(uint8 ItemContainer, int32 ItemSlot, uint8 ScrollContainer, int32 ScrollSlot,
		uint8 Helper97Container, int32 Helper97Slot, uint8 Helper98Container, int32 Helper98Slot);

	// ★ 6505 `OP_ON_RECV_ITEM_UPGRADE_BATCH` (тело 231 б = 0xE7) — ОТВЕТ на 6504.
	//   Приёмник эталона `Recv_ItemUpgradeBatch@0xD2B1E0` (network.c:19609, диспетчер `:833`);
	//   сборщик сервера `recvDBShellItemUpgrade@0x500800` (GameServer.c:99253-99263) и 13 ранних
	//   отказов `CharShellItemUpgrade_0` (все — `sub_4C28F0(…, 6505, 231, …)`):
	//     @0   u8  код: 1 успех · 8 провал · 2/3/4/7 отказы · 5/6 разбираются, но сервер их не шлёт ·
	//              0 — отказ БЕЗ текста (эталон `default: break`);
	//     @1   i16 новый ВЕС персонажа (эталон берёт его ТОЛЬКО в ветке кода 1 — `HUD_UpdateMiniWeightBar`);
	//     @3   57  запись улучшаемого предмета после операции;
	//     @60  57  запись свитка/купона после списания;
	//     @117 57  запись помощника subType 97;   ★ 97 РАНЬШЕ 98 (ревизия отчёта, GameServer.c:99253-99259)
	//     @174 57  запись помощника subType 98.
	//   ⚠ Колбэк только КОПИТ (закон `no-reentrant-net-callbacks`), записи в модель кладёт окно из Tick.
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnAsdaItemUpgradeResult, const TArray<uint8>& /*body231*/);
	FOnAsdaItemUpgradeResult OnItemUpgradeResult;

	// ═════ ★★★ ВЫСОКАЯ ПЕРЕПЛАВКА — окно `Smelting3_Win` (JUI 78200), 25.09.2026 (волна 7 заточки) ═════
	// ★ C→S **6500** `OP_RECV_USER_UPPER_REFINE`. Эталон `itemCompose_SendCraftRequest@0xCF5390`
	//   (item_inventory.c:34893-35039): `CNetwork_Send(…, sendBuf, 64, 6500, 1,1,1,0,0)` = конверт
	//   `SendSecured` (8 б секрет + 16 б MD5 + 40 б тела), паддинг включён. Сервер `Recv_User_UpperRefine
	//   @0x4FC7E0` (GameServer.c:97879): `sub_523F00(a3, 0x28, …, 1)` — профиль a5 = 1, тело с кадра +33.
	//   Тело 40 б (`craftReq[10]`, инициализатор `ItemUpdate_InitState@0xCF5B30`):
	//     @0  i32 ключ рецепта = поле +0 записи `RefineHigh.BIN` (Idx); инициализатор −1;
	//     @4  i16 слот улучшаемого предмета сумки (запись +5); инициализатор −1;
	//     @6  u8  его контейнер (запись +4); инициализатор 0;
	//     @7 / @18 / @29 — три 11-байтовые ССЫЛКИ на материалы (`itemTableFindRecordById@0x999C80`):
	//        +0 i16 слот (запись +5) · +2 i16 запись +7 · +4 u8 контейнер (запись +4) · +5 i32 количество
	//        (запись +9) · +9 i16 запись +14. Инициализатор `helper_initInvalidIds@0x973D30`:
	//        {−1, −1, 0, −1, −1} — контейнер 0 сервер пропускает (`if (refineReq[11*i+11])`).
	//   ★ В теле НЕТ ни charId, ни userNum — сервер берёт пользователя по сокет-слоту (`arg0`).
	//   `Mat0..2` — 57-байтовые записи ячеек материалов (пустой массив ⇒ ссылка остаётся инициализатором).
	static TArray<uint8> BuildUpperRefine6500Body(int32 RecipeKey, int32 TargetSlot, uint8 TargetContainer,
		const TArray<uint8>& Mat0, const TArray<uint8>& Mat1, const TArray<uint8>& Mat2);
	// Собрать тело (см. выше) и отправить конвертом `SendSecured`. false — пакет НЕ ушёл.
	bool SendUpperRefine6500(int32 RecipeKey, int32 TargetSlot, uint8 TargetContainer,
		const TArray<uint8>& Mat0, const TArray<uint8>& Mat1, const TArray<uint8>& Mat2);

	// ★ S→C **6501** `OP_ITEM_UPDATE` (тело 235 б) — ответ на 6500. Приёмник эталона `Recv_ItemUpdate
	//   @0xCF5B90` (network.c:16857, диспетчер :826 `case 0x1965`); сборщики сервера — ранние отказы
	//   `Recv_User_UpperRefine` и `recvDBUpperRefine@0x4FD470` (все `sub_4C28F0(…, 6501, 235, …)`):
	//     @0   u8  код: 1 успех · 8 провал · 0/2/5/6/9 отказы сервера (3/4/7 клиент разбирает, сервер не шлёт);
	//     @1 / @58 / @115  57 б — три записи МАТЕРИАЛОВ после списания;
	//     @172 57 б — запись улучшенного предмета (эталон кладёт её ТОЛЬКО при коде 1);
	//     @229 i16 вес персонажа; @231 i32 НОВАЯ сумма золота — обе эталон берёт ТОЛЬКО при коде 1/8.
	//   Вес ставит сетевой слой (как у 6505), всё остальное — окно из своего Tick.
	//   ⚠ Колбэк только КОПИТ (закон `no-reentrant-net-callbacks`).
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnAsdaHighRefineResult, const TArray<uint8>& /*body235*/);
	FOnAsdaHighRefineResult OnHighRefineResult;

	// ═════ ★★★ РАЗБОРКА СНАРЯЖЕНИЯ — окно `Disassembly_Win` (JUI 32000), волна 2-А 06.10.2026 ═════
	//   Реверс — `Reports/owner_1006/wave2A_disassembly.md`; окно — `UAsdaDisassemblySubsystem`.
	// ★ C→S **6502** `OP_RECV_USER_DISASSEMBLE`. [ПРОТОКОЛ] Эталон `netSendByteWordCmd_6502@0xE0E460` (network.c:27724):
	//   `Net_Md5Digest16(…, cmdPayload, 3u)` + `CNetwork_Send(…, packetBuf, 27, 6502, 1,1,1,0,0)` = конверт `SendSecured`
	//   (8 б секрет + 16 б MD5 + 3 б тела), паддинг включён. Сервер `Recv_User_Disassemble@0x4FD9B0` (GameServer.c:98227):
	//   `sub_523F00(a3, 3u, …, 1)`, тело с кадра +33: `sub_4D61E0(…, (u8)тело[0], (i16)тело[1], …)`.
	//   Тело 3 б (инициализатор `sub_92CDDF` — нули): @0 u8 контейнер предмета (запись +4), @1 i16 слот (запись +5).
	//   ★ В теле НЕТ ни charId, ни userNum — сервер берёт пользователя по сокет-слоту.
	static TArray<uint8> BuildDisassemble6502Body(uint8 ItemContainer, int32 ItemSlot);
	// Собрать тело (см. выше) и отправить `SendSecured`. false — пакет НЕ ушёл.
	bool SendDisassemble6502(uint8 ItemContainer, int32 ItemSlot);

	// ★ S→C **6503** `OP_ITEM_DISJOINT` (тело 119 б) — ответ на 6502. Приёмник эталона `Recv_ItemDisjoint@0xE0EAD0`
	//   (network.c:27860, диспетчер :829 `case 0x1967`); сборщики сервера — ранние отказы `Recv_User_Disassemble`
	//   и ответ БД `sub_4FE900` (все `sub_4C28F0(…, 6503, 119, …)`):
	//     @0   u8  код: 1 успех · 0 отказ без текста · 3 нет ячейки · 4 перевес · 6 нет предмета · 9 запись таблицы
	//              не та (2/5/7/8 клиент разбирает, сервер не шлёт);
	//     @1   i16 вес персонажа (эталон берёт его ТОЛЬКО при кодах 1 и 8 — `HUD_UpdateMiniWeightBar`);
	//     @3   57  запись разобранного предмета (код 0 — ячейка очищена; контейнер +4 / слот +5);
	//     @60  57  запись выданного материала;
	//     @117 i16 сколько выдано (подпись слота результата).
	//   Вес ставит сетевой слой (как у 6501/6505), всё остальное — окно из своего Tick.
	//   ⚠ Колбэк только КОПИТ (закон `no-reentrant-net-callbacks`).
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnAsdaDisassembleResult, const TArray<uint8>& /*body119*/);
	FOnAsdaDisassembleResult OnDisassembleResult;

	// ═════ ★★★ СОЗДАНИЕ СНАРЯЖЕНИЯ (РЕМЕСЛО) — окна 76000/77000/77500, 25.09.2026 (волна В1) ═════
	//   Полный реверс — `Reports/craft/craft_re_report.md` §5 + ревизия `craft_re_REVIEW.md`.
	//   Модель и приём — `UAsdaCraftSubsystem`, окно списка 76000 — `UAsdaCraftWindowSubsystem`.
	//   Все четыре C→S — конверт «с секретом» `SendSecured` (8 б секрет + 16 б MD5 + тело), паддинг
	//   включён: клиент `Net_Md5Digest16(…, 3u/2u/0x1Au/3u)` + `CNetwork_Send(…, 1,1,1,0,0)`
	//   (network.c:49751/49880/50228/50010), сервер `sub_523F00(…, 3/2/0x1A/3, userNum, 1)`
	//   (GameServer.c:121005/121254/121405/121873) — хеш с кадра +33.
	//   ★ В телах C→S НЕТ ни charId (i32), ни userNum (i16): userNum сервер штампует сам по сокету,
	//   charId живёт только в пакетах GS↔DB (+2) и сверяется с `CUser+0x174` в `recvDB*` ремесла.

	// C→S **6598** `OP_RECV_USER_CRAFT_RECIPE_REGIST` — «изучить рецепт свитком». Эталон
	//   `netSendMaskedPacket_op6598@0xFDF5B0` (network.c:49627-49753): тело 3 б —
	//   @0 i16 слот свитка (`payload[0] = subOpcode`), @2 u8 контейнер свитка (`LOBYTE(payload[1])`).
	//   Сервер `RecvUser_CraftRecipeRegist@0x53CCA0`: `sub_4D61E0(…, SLOBYTE(a4[1]), a4[0], …)`.
	bool SendCraftRecipeRegist6598(int32 ScrollSlot, uint8 ScrollContainer);
	// C→S **6600** `OP_RECV_USER_CRAFT_RECIPE_DELETE` — «удалить (забыть) рецепт». Эталон
	//   `netSendSecuredOp6600@0xFDFBD0` (network.c:49757-49882): тело 2 б — @0 i16 id рецепта.
	bool SendCraftRecipeDelete6600(int32 RecipeId);
	// C→S **6602** `OP_RECV_USER_CRAFT_ENTER` — «изготовить 1 шт.». Эталон `netSendItemCombineReq@0xFE5E30`
	//   (network.c:50080-50230), конструктор тела `upgradeSlotsInit@0xFE6540` (item_enhance.c:10313, миснейм):
	//   тело 26 б — @0 i16 id рецепта · @2 i16×6 слоты материалов · @14 u8×6 контейнеры материалов ·
	//   @20 i16×2 слоты катализаторов · @24 u8×2 контейнеры катализаторов. Инициализатор: id −1, слоты −1,
	//   контейнеры 0. ★ Катализаторы клиент НЕ ЗАПОЛНЯЕТ НИКОГДА (в окне 77500 для них нет ни одного
	//   контрола) — уходят −1/0. Количества в теле нет: одна попытка на пакет.
	//   `MatSlots`/`MatContainers` — по 6 элементов (короче — остаток остаётся инициализатором).
	bool SendCraftEnter6602(int32 RecipeId, const TArray<int32>& MatSlots, const TArray<uint8>& MatContainers);
	// Собрать тело 6602 (см. выше) без отправки — для проверки раскладки самотестом.
	static TArray<uint8> BuildCraftEnter6602Body(int32 RecipeId, const TArray<int32>& MatSlots, const TArray<uint8>& MatContainers);
	// C→S **6604** `OP_RECV_USER_CRAFT_SCROLL_LEVEL_UP` — «поднять уровень ремесла свитком». Эталон
	//   `netSendMaskedPacket_op6604@0xFE01E0` (network.c:49886-50012): тело 3 б — как у 6598
	//   (@0 i16 слот свитка, @2 u8 контейнер).
	bool SendCraftScrollLevelUp6604(int32 ScrollSlot, uint8 ScrollContainer);

	// S→C пять ответов ремесла. Длина тела — по вызовам отправки сервера (`sub_4C28F0(…, опкод, N, …)`):
	//   **6597** 593 б (`recvDBCraftList@0x53C7E0`, GameServer.c:120942) — список изученных + уровень;
	//   **6599**  64 б — ответ 6598;  **6601** 3 б — ответ 6600;  **6603** 525 б — ответ 6602;
	//   **6605**  61 б — ответ 6604. Приёмники эталона (имена IDA — миснеймы): `shopParseStockPacket@0xFDEEB0`,
	//   `netHandleInventoryUpdatePacket@0xFDF080`, `netOnSubcodeNoticeHandler@0xFDF3E0`,
	//   `upgradeDlg_onResultPacket@0xFE65E0`, `netOnItemUseResult@0xFE0800` (диспетчер network.c:953-967).
	//   ВЕС ставит сетевой слой ровно в тех ветках, где его ставит эталон (как у 6505/6501); всё прочее —
	//   модель ремесла из своего Tick. ⚠ Колбэк только КОПИТ (закон `no-reentrant-net-callbacks`).
	//   ★ 6597 приходит ПОСЛЕ входа на GameServer и ЗАНОВО при каждом портале (DBManager
	//   `Recv_PortalMoveUserInfo` → `CRT_UPCraftInfoList`, ревизия Р-1) — фильтр по сбросу сессии НЕ нужен.
	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnAsdaCraftPacket, int32 /*opcode*/, const TArray<uint8>& /*body*/);
	FOnAsdaCraftPacket OnCraftPacket;

	// ═════ ★★★ ЛИЧНАЯ ЛАВКА — ПРОДАВЕЦ (волна Л1 торговли, 29.09.2026; `Reports/trade/wave_L1.md`) ═════
	//   Разбор — `Reports/trade/B_selfstore_client.md` §3.1, сервер — `C2_selfstore_server.md` §3.1-3.7.
	//   [ПРОТОКОЛ] Конверт у всех трёх — `SendSecured` (TS8 + MD5-16 + тело, пад): эталонный
	//   `CNetwork_Send(…, 1, 1, 1, 0, 0)` у `netSendPlayerInfo6300@0xDD45C0`, `storeOpenSendRequest@0xDD3AA0`,
	//   `antiCheat_SendPositionReport@0xDD4DA0` (МИСНЕЙМ = отправитель 6305). Сервер читает тело с +33.
	//   ★ В каждом теле charId (i32) и userNum (i16) — РАЗНЫЕ поля (закон `charid-vs-usernum`).
	// C→S **6300** `OP_CHAR_SELF_STORE_READY` (94 б = 24 + 70): @0 i32 charId продавца; @4 u8 премиум
	//   (`CSellWindow+0x389 != 0`); @5 char[20] ИМЯ персонажа (`sprintf "%s"`); @25 45 б запись билета
	//   (`CSellWindow+0x38A`, `qmemcpy(…, 0x2D)`). `Ticket45` — ровно 45 байт (иначе пакет не уходит).
	bool SendSelfStoreReady6300(int32 SelfCharId, bool bPremium, const FString& SelfName, const TArray<uint8>& Ticket45);
	// C→S **6302** `OP_CHAR_SELF_STORE_OPEN` (529 б = 24 + 505): @0 i32 charId; @4 u8 число лотов (≤ 10);
	//   @5+45·n лот: @0 i32 код, @4 u8 контейнер, @5 i16 слот, @7 i32 кол-во (0 у нештучного), @11 i16 индекс = n,
	//   @13 i32 цена за единицу (≤ 0 ⇒ 1), @17 4×i16 = −1, @25 5 пар {i16 −1, i16 0} (`slotStructReset@0xDD39B0`);
	//   пустые лоты — тот же сброс (@5/@11 = −1); @455 char[50] название лавки. Массивы — по одному элементу на лот.
	bool SendSelfStoreOpen6302(int32 SelfCharId, const TArray<int32>& ItemIds, const TArray<uint8>& Containers,
		const TArray<int32>& Slots, const TArray<int32>& Qtys, const TArray<int32>& Prices, const FString& Title);
	// Собрать тело 6302 без отправки (самотест раскладки и стенд). Пустой массив — лотов вне `ItemIds.Num()` нет.
	static TArray<uint8> BuildSelfStoreOpen6302Body(int32 SelfCharId, const TArray<int32>& ItemIds, const TArray<uint8>& Containers,
		const TArray<int32>& Slots, const TArray<int32>& Qtys, const TArray<int32>& Prices, const FString& Title);
	// C→S **6305** `OP_CHAR_SELF_STORE_CLOSE` (30 б = 24 + 6): @0 i32 charId ХОЗЯИНА лавки, @4 i16 userNum хозяина
	//   (`CShopDlg+0x274/+0x278` = глобалы-МИСНЕЙМЫ `g_CachedLocalPlayerSerial`/`g_CachedSelectedUserIdx`).
	bool SendSelfStoreClose6305(int32 HostCharId, int32 HostUserNum);
	// ═════ ★★★ ЛИЧНАЯ ЛАВКА — ПОСЕТИТЕЛЬ (волна Л2 торговли, 30.09.2026; `Reports/trade/wave_L2.md`) ═════
	// C→S **6307** `OP_CHAR_SELF_STORE_INFO` (34 б = 24 + 10) [ПРОТОКОЛ]: @0 i32 СВОЙ charId, @4 i32 charId хозяина,
	//   @8 i16 userNum хозяина (`netSendTradeTargetRequest6307@network.c:26875`, `sub_91EBA9` — обнуление 10 б).
	bool SendSelfStoreInfo6307(int32 SelfCharId, int32 HostCharId, int32 HostUserNum);
	// C→S **6309** `OP_CHAR_SELF_STORE_BUY_ITEM` (305 б = 24 + 281) [ПРОТОКОЛ]: @0 i32 свой charId, @4 i32 charId хозяина,
	//   @8 i16 userNum хозяина, @10 u8 число позиций (≤ 6), @11+45·n позиция: @0 i32 код, @7 i32 кол-во (0 у нештучного),
	//   @11 i16 индекс лота витрины, @13 i32 цена за единицу; прочие поля позиции и ВСЕ пустые позиции — `slotStructReset`
	//   (код 0, @5/@11 i16 −1, @17 4×i16 −1, @25 5 пар {i16 −1, i16 0}; `sub_DDBED0` = конструктор тела `shopDlg_SendBuyReq@0xDDB5F0`).
	bool SendSelfStoreBuy6309(int32 SelfCharId, int32 HostCharId, int32 HostUserNum, const TArray<int32>& ItemIds,
		const TArray<int32>& Qtys, const TArray<int32>& LotIdx, const TArray<int32>& Prices);
	// Собрать тело 6309 без отправки (самотест раскладки и стенд). Позиции сверх `ItemIds.Num()` (и сверх 6) — пустые.
	static TArray<uint8> BuildSelfStoreBuy6309Body(int32 SelfCharId, int32 HostCharId, int32 HostUserNum, const TArray<int32>& ItemIds,
		const TArray<int32>& Qtys, const TArray<int32>& LotIdx, const TArray<int32>& Prices);
	// ═════ ★★★ ЛИЧНАЯ ЛАВКА — ЧАТ (волна Л4 торговли, 30.09.2026; `Reports/trade/wave_L45.md`) ═════
	// C→S **6313** `OP_RECV_SELF_STORE_CHAT_CLIENT` (МИСНЕЙМ «recv» — это C→S) [ПРОТОКОЛ]: @0 i32 charId ХОЗЯИНА (`CShopDlg+0x274`),
	//   @4 i16 userNum хозяина (`+0x278`), @6 char[20] имя отправителя (`thunk_getFieldAt1056(LocalPlayer)`), @26 char[80] текст.
	//   ★ [ИСПРАВЛЕНИЕ] К-0: оригинал (`netSendTwoStringRequest6313@0xDDD150`) шлёт тело 226 б (MD5 по 0xE2, текст char[200]), а
	//   сервер (`Recv_SelfStoreChatClient` S:101545, `sub_523F00(a3, 0x6A)`) проверяет MD5 по 106 б — каждое сообщение оригинала
	//   отброшено. Порт шлёт 130 б = 24 + **106**; текст ≤ 79 байт cp1251 + ноль (сервер копирует 80 б без терминатора в 6314).
	bool SendSelfStoreChat6313(int32 HostCharId, int32 HostUserNum, const FString& SenderName, const FString& Text);
	// Собрать тело 6313 (106 б) без отправки (самотест раскладки и стенд).
	static TArray<uint8> BuildSelfStoreChat6313Body(int32 HostCharId, int32 HostUserNum, const FString& SenderName, const FString& Text);
	// S→C ответы продавцу: **6301** 1 б (`itemEnhanceOnResult@0xDD4F80` — МИСНЕЙМ = ответ 6300), **6303** 452 б
	//   (`pktHandleShopWindowOpen@0xDD3310`), **6306** 571 б (`sellWindowOnSellResult@0xDD5230` — ответ 6305).
	//   Длины — по `memcpy(…, Src + 9, N)` приёмников эталона. ⚠ Колбэк только КОПИТ (закон `no-reentrant-net-callbacks`):
	//   подписчик `UAsdaSelfStoreSubsystem` кладёт тело в очередь и разбирает его в своём Tick.
	//   ★ Волна Л2 (30.09.2026): тем же делегатом идут ответы посетителю и продавцу — **6308** 628 б (`shopHandleOpenShopPacket`;
	//   ★ сервер шлёт и укороченный 4 б — «Same User Error», GameServer.c:100595 — тело дополняется нулями до 628, как `memcpy`
	//   эталона из буфера), **6310** 350 б, **6311** 281 б, **6312** 281 б, **6315** 5 б.
	//   ★ Волна Л4 (30.09.2026): **6314** 104 б (`shopDlg_RecvLogLineColored@0xDDB1F0`; сервер шлёт 104, эталон копирует 224 из буфера
	//   — порт дополняет нулями до 104): @0 i32 номер отправителя (0 хозяин, 1..5 гость), @4 имя[20], @24 текст[80].
	//   ★ Волна Л4/Л5 (К-9 [ИСПРАВЛЕНИЕ]): тем же делегатом, В ПОРЯДКЕ ПОТОКА, идёт МАРКЕР **4005** (4 б: i32 charId соседа) — «персонаж
	//   вошёл в обзор»; читает только `UAsdaStallSignSubsystem` (чистка очереди вывесок 6304), прочие подписчики его игнорируют.
	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnAsdaSelfStorePacket, int32 /*opcode*/, const TArray<uint8>& /*body*/);
	FOnAsdaSelfStorePacket OnSelfStorePacket;

	// ═════ ★★★ ВОЛНА 2-Б (06.10.2026): ДОКЛАД ОБ ОХОТЕ 56450 (код NPC 30068) и ПОЖЕРТВОВАНИЕ (код NPC 30051) ═════
	//   Полный реверс — `Reports/owner_1006/wave2B_hunt_donation.md` §2–§3.

	// ★ 6150 `OP_RECV_CLIENT_TITLE_ITEM_CHECK` (35 б = 8 + 16 + 11 тело) — СДАТЬ ЗНАКИ ОХОТЫ В ЗАЧЁТ ЗВАНИЯ. [ПРОТОКОЛ]
	//   Эталон `net_sendBlobSyncOpcode6150@0xED0170` (network.c:32369); сервер `RecvClient_TitleItemCheck@0x55F090`
	//   читает ровно 11 (`sub_523F00(arg4, 0xBu, …)`):
	//     @0 i32 charId (`LocalPlayer+0x41C`) · @4 i16 слот записи (+5) · @6 u8 контейнер записи (+4) · @7 i32 количество.
	bool SendTitleItemCheck6150(uint8 Container, int32 Slot, int32 Quantity);

	// ★ 6151 `OP_INVENTORY_UI_ON_ITEM_MOVE_PACKET` (тело 66 б) — ОТВЕТ на 6150 (`inventoryUI_onItemMovePacket@0xED0820`,
	//   network.c:633 case 0x1807): @0 i32 charId · @4 запись 57 б (+4 контейнер, +5 слот, +9 кол-во) · @61 u8 код
	//   (1/2 — сдано; 9 — звание уже есть; прочие — ошибка) · @62 i32 вес (эталон ставит ТОЛЬКО при кодах 1/2 — здесь).
	//   ⚠ Колбэк только КОПИТ (закон `no-reentrant-net-callbacks`); разбор — `UAsdaHuntReportSubsystem`.
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnAsdaHuntReportResult, const TArray<uint8>& /*body66*/);
	FOnAsdaHuntReportResult OnHuntReportResult;

	// ★ 6132 `OP_RECV_USER_EVENT_ITEM_STACK_REQ` (37 б = 8 + 16 + 13 тело) — ПОЖЕРТВОВАТЬ ПРЕДМЕТ 60073. [ПРОТОКОЛ]
	//   Эталон `netSendGuarded_op6132_useItem60073@0xD9F680` (network.c:24129); сервер `Recv_UserEventItemStackReq@0x55C1C0`
	//   читает ровно 13: @0 i32 charId (`g_LocalPlayer+0x41C`) · @4 i32 код предмета (+0) · @8 u8 контейнер (+4) ·
	//   @9 i16 слот (+5) · @11 i16 количество.
	bool SendUserEventItemStack6132(int32 ItemCode, uint8 Container, int32 Slot, int32 Quantity);

	// ★ 6133 `OP_ON_RECV_REFINE_SLOT_UPDATE` (тело 66 б, миснейм) — ОТВЕТ на 6132 (`onRecvRefineSlotUpdate@0xD9F8B0`,
	//   network.c case 0x17F5): @0 i32 charId · @4 u8 код (1 — успех, иначе молча) · @5 запись 57 б (её +9 = @14 — новое
	//   кол-во) · @62 i32 вес (эталон ставит ТОЛЬКО при коде 1 — здесь). Нотисов у эталона нет ни в одной ветке.
	//   ⚠ Колбэк только КОПИТ; разбор — `UAsdaDonationSubsystem`.
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnAsdaDonationResult, const TArray<uint8>& /*body66*/);
	FOnAsdaDonationResult OnDonationResult;

	// ═════ ★★★ ВОЕННЫЙ МАГАЗИН — окно `HONOR_itemmall_Win` (JUI 67200), код кнопки NPC 30070 ═════
	//   Полный реверс — `Reports/repair_and_warshop_port.md` §2. Валюта — ПРЕДМЕТ 20628
	//   «Монеты славы» (у всех 140 строк витрины), плюс требование воинского звания фракции.

	// ★ 6162 `OP_CHAR_ROSE_WAR_STORE_BUY` (36 б = 8 + 16 + 12 тело) — КУПИТЬ ОДИН ТОВАР.
	//   Эталон `net_sendStateSyncOpcode6162@0xB74ED0` (network.c:9569); сервер
	//   `CharRoseWarStoreBuy@0x514FE0` читает ровно 12 (`sub_523F00(arg4, 0xCu, …)`):
	//     @0 i16 userNum · @2 i32 charId · @6 i16 StorePos (индекс строки таблицы) · @8 i32 код товара.
	//   ⚠ Сервер СВЕРЯЕТ и StorePos, и код товара со своей 31-байтовой строкой витрины: расхождение
	//   даёт код ответа 0, а не молчание.
	bool SendWarShopBuy6162(int32 StorePos, int32 ItemCode);

	// ★ 6163 `OP_UI_HANDLE_ITEM_ACTION_RESULT` (тело 121 б) — ОТВЕТ на 6162
	//   (`uiHandleItemActionResult@0xB75580`, ui_core.c:8248):
	//     @0  u8  код: 1 = успех; 0 → нотис 9570, 2 → 1216, 3 → 12676, 4 → 12547, 5 → 110608,
	//         6 → 110609, 7 → 110610, 8 → 111926, 9 → 111927;
	//     @1  i16 новый текущий вес · @3 i32 новое ЗОЛОТО игрока;
	//     @7  57 б — запись СПИСАННОЙ цены (её +4 контейнер, +5 слот, +9 количество; −1 ⇒ слот очищен);
	//     @64 57 б — запись КУПЛЕННОГО предмета (её +0 код уходит в нотис 110601).
	//   ⚠ Колбэк только КОПИТ (закон `no-reentrant-net-callbacks`).
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnAsdaWarShopBuyResult, const TArray<uint8>& /*body121*/);
	FOnAsdaWarShopBuyResult OnWarShopBuyResult;

	// ═══ ДЕРЕВО НАВЫКОВ (окно SKILL_WIN 12000) ═══
	// Реверс: `Reports/skill_window_port.md` §3. Тела читаются от начала Body (заголовок 9 б
	//   снят разбором пакета), поэтому эталонные смещения «Src+9» здесь = 0.

	// ★ 5253 (28 б = 8 секрет + 16 MD5 + 4 тело) — ЗАПРОС «вложить очко в навык».
	//   Эталон `net_sendRequest_5253@0xE54DE0` ← `skillWndSendSelectedSlot@0xE53E30`.
	//   Тело РОВНО 4 байта (сервер `sub_523F00(arg4, 4u, …)` считает MD5 именно по 4):
	//     @0 i16 skillCode · @2 u8 flagByte (эталон всегда 1) · @3 u8 pointCount (эталон всегда 1).
	//   ⚠ charId/userNum в тело НЕ кладутся: сервер `Recv_SkillPoint_SkillActive@0x4774C0` адресует
	//     по userIdx соединения (`arg0`). Любой лишний байт ломает MD5-проверку.
	bool SendSkillLearn5253(int32 SkillCode, uint8 PointCount = 1, uint8 FlagByte = 1);

	// ★ 5254 (111 б) `skillHandleUpgradeResponse@0xE55420` — ОТВЕТ на 5253.
	//   @0 u8 код: 1 = успех, 2..8 = отказ (нотисы 2201/2202/2206/2207/2208/111700/111701, иначе 2200);
	//   @1 i16 новое число очков навыка · @3 i16 skillCode · @5 u8 новый уровень навыка;
	//   @6 u8 режим пересчёта статов (1 = блоки A+B, 2 = только B) · @7 36 б блок A · @43 68 б блок B.
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnAsdaSkillUpgradeResult, const TArray<uint8>& /*body111*/);
	FOnAsdaSkillUpgradeResult OnSkillUpgradeResult;

	// ★ 5251 `skillWnd_ParseSkillUpdatePacket@0xE559F0` — ПОЛНЫЙ список записей дерева.
	//   Тело = N × 34 б; запись: @0 i16 skillCode · @2 u8 уровень · @3 u8 «можно вкачивать»
	//   · @10 i16 требуемый уровень персонажа. Записи ДОБАВЛЯЮТСЯ в список своего дерева.
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnAsdaSkillListFull, const TArray<uint8>& /*bodyN*34*/);
	FOnAsdaSkillListFull OnSkillListFull;

	// ★ 6056 `skillWnd_OnSkillListPacket@0xE55B60` — те же 34-б записи, но перед вставкой запись
	//   с тем же skillCode УДАЛЯЕТСЯ (обновление, а не добавление).
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnAsdaSkillListUpdate, const TArray<uint8>& /*bodyN*34*/);
	FOnAsdaSkillListUpdate OnSkillListUpdate;

	// ★ 5252 `Net_RecvLearnedSkillList@0xE56090` — КОМПАКТНЫЙ список выученного (вход в мир).
	//   Тело 105 б = 35 × 3 б: @0 i16 skillCode (< 0 — пусто) · @2 u8 уровень.
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnAsdaSkillListCompact, const TArray<uint8>& /*body105*/);
	FOnAsdaSkillListCompact OnSkillListCompact;

	// ★ 6057 `skillWnd_ResetElementSlots@0xE55910` — ПОЛНЫЙ сброс всех списков навыков (тело пустое).
	DECLARE_MULTICAST_DELEGATE(FOnAsdaSkillTreeReset);
	FOnAsdaSkillTreeReset OnSkillTreeReset;

	// ★ 5462 `skillTreeUiResetSlots_63D0@0xE563D0` — очистка 45 слотов + нотис 12855; @0 i16 новые очки.
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnAsdaSkillSlotsReset, int32 /*newSkillPoints*/);
	FOnAsdaSkillSlotsReset OnSkillSlotsReset;

	// ★★★ 5464 `skillUiOnLearnSkill_6590@0xE56590` — НЕ «навык изучен» (прежнее имя было ошибочным).
	//   Сервер шлёт пакет из ветки ПЕРЕСЧЁТА навыков под характеристику (GameServer.c:51354):
	//   тело 3 б — `@0 i16 новые очки` (сервер кладёт накопленную сумму), `@2 u8 grade` (1..12).
	//   Клиент показывает текст 12854 «Навыки приведены в соответствие с характеристикой [[ss1]]».
	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnAsdaSkillsRealigned, int32 /*newSkillPoints*/, int32 /*grade*/);
	FOnAsdaSkillsRealigned OnSkillsRealigned;

	// ★ 5465 `Recv_SkillCoolTime@0xE4B180` — кулдауны навыков: до 30 записей по 6 б
	//   (@0 i32 skillId, @4 i16 остаток секунд); терминатор skillId == -1.
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnAsdaSkillCooldowns, const TArray<uint8>& /*bodyN*6*/);
	FOnAsdaSkillCooldowns OnSkillCooldowns;

	// ★ 21.10.2026: 5274 `pktHandlerCharFxStop@0xF4EDF0` (имя ложное) — «откат навыка истёк». Сервер шлёт его только самому
	//   игроку: естественное истечение (2 б), сброс всех откатов 6058/предмет (4 б, [2..3] — мусор стека), навыки соулмейта (2 б).
	//   Клиент читает только [0..1] i16 id навыка. Подписчик — значок по центру экрана `UAsdaCooldownReadyIconSubsystem`
	//   (опция 30479). Откаты по 5274 НЕ трогаются (их сбрасывает 6058). Разбор: `Reports/skill_cooldown_ready_icon/01_etalon.md`.
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnAsdaSkillCooldownReady, int32 /*skillId*/);
	FOnAsdaSkillCooldownReady OnSkillCooldownReady;

	// ★★★ ВТОРОЙ БЛОК ТОГО ЖЕ ПАКЕТА 5465 — ОТКАТЫ ПРЕМИУМ-ПРЕДМЕТОВ (`special_itemlist`).
	//   Эталон `Recv_SkillCoolTime@0xE4B180` (`network.c:28806-28832`) после цикла навыков
	//   (`a2 + 6*i`, i < 30, байты 0…179) читает ВТОРОЙ массив ровно такого же формата
	//   со смещения **+180**: `for (j=0; j<30 && *(i32*)(a2 + 6*j + 180) != -1; ++j)`,
	//   где @180+6j — i32 КОД предмета, @184+6j — i16 остаток В СЕКУНДАХ. Полная длительность
	//   берётся не из пакета, а из таблицы: `CItemManager_GetTable2Record(code)->cooldownBase`.
	//   ⚠ ДО ЭТОЙ ПРАВКИ ПОРТ ЭТОТ БЛОК ВЫБРАСЫВАЛ: тело 5465 отдавалось целиком только в
	//   `OnSkillCooldowns`, а разбор навыков останавливается на первом терминаторе −1 (он в
	//   байтах 0…3 у персонажа без откатов навыков) — то есть байты 180…359 не читал никто,
	//   и после входа в мир откаты премиум-предметов, которые сервер ПРОДОЛЖАЕТ держать
	//   (отказ 5451 кодом 8), были игроку не видны.
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnAsdaItemCooldowns, const TArray<uint8>& /*30*6 б*/);
	FOnAsdaItemCooldowns OnItemCooldowns;

	// ★ 6713 Recv_ClanMyMembership@0xCA4AB0 — СВОЯ фракция (лагерь): @0 i16 factionId (-1=нет;
	//   0=свет/синий, 1=тьма/красный, 2=хаос/зелёный — полосы 1671/1670/1672), @2 u32 очки,
	//   @6 i8 звание (эмблема = faction_grade.tex, ячейка звание-1; альтернатор с эмблемой клана).
	DECLARE_MULTICAST_DELEGATE_ThreeParams(FOnAsdaSelfFaction, int32 /*factionId*/, int32 /*points*/, int32 /*grade*/);
	FOnAsdaSelfFaction OnSelfFaction;

	// ★ 6585 recvWingDataTableInsert@0xEE3170 — КРЫЛЬЯ Plus Package (wings_soulmate_re.md §A3):
	//   body 11б: @0 u32 charId, @4 i16 wingId (id записи PlusPackageTable; -1 нет), @6 u32 остаток сек,
	//   @10 u8 flag (1=вкл, 0=выкл/истёк). Сервер шлёт на логин/enter-map/покупку/истечение.
	DECLARE_MULTICAST_DELEGATE_FourParams(FOnAsdaWingState, int32 /*charId*/, int32 /*wingId*/,
		int32 /*remainSec*/, bool /*bOn*/);
	FOnAsdaWingState OnWingState;

	// ★ 5220 Recv_PartyMinimapMark@0xE641A0 (мисномер; дебаг «SOUL OTHER CHECK») — соулмейт-стейт
	//   ЛЮБОГО чара (wings_soulmate_re.md §B3): мульти-записи 101б: @0 u32 charId, @4 u16 userIdx,
	//   @6 блок 0x5F → char+2215: soulIdx i32 (байт 0; >0 = КОЛЬЦО ON), partner charId (13),
	//   partner userIdx i16 (17). Сервер шлёт в enter-map флоу (вместе с 6585/5417).
	DECLARE_MULTICAST_DELEGATE_FourParams(FOnAsdaSoulmateState, int32 /*charId*/, int32 /*userIdx*/,
		int32 /*soulIdx*/, int32 /*partnerUserIdx*/);
	FOnAsdaSoulmateState OnSoulmateState;

	// ★ ТРАНСПОРТ (mount_system_re.md): S→C 0x154D (5453, 10 б) — БРОДКАСТ посадки соседям:
	//   userIdx i16@0, itemCode i32@2, duration i32@6 (−1 = бессрочно); 0x154E (5454, 6 б) — высадка
	//   (userIdx i16@0); 0x154B (5451, 68 б) — результат СВОЕЙ посадки (свой userIdx/itemCode в шапке).
	//   Персонаж к транспорту НЕ крепится: меш рисуется в матрице персонажа, а его клипы подменяются
	//   на сидячие Ch1_n_sit_* (слоты 0/1/5) — посадка запечена в самих анимациях.
	DECLARE_MULTICAST_DELEGATE_ThreeParams(FOnAsdaMount, int32 /*userIdx*/, int32 /*itemCode*/, bool /*bOn*/);
	FOnAsdaMount OnMountState;

	// ══════════════════ АКТИВНЫЕ ЭФФЕКТЫ (баффы/дебаффы/усилители) — ЭТАП 2 MINISTATE_WIN ══════════════
	// Полный реверс: Reports/ministate_win_re_B.md. Один компактный канал на все опкоды потока
	//   эффектов: разбирается ЗДЕСЬ (побайтово 1:1 с эталоном), применяется в UAsdaEffectsSubsystem.
	//   Поля повторяют «сырую» 40-байтную запись эталона, из которой buffList_AddOrUpdate@0xF35810
	//   читает +2/+5/+6/+7/+9 — поэтому имена полей = смещения эталона, а не абстракции.
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnAsdaEffectMsg, const struct FAsdaEffectMsg& /*Msg*/);
	FOnAsdaEffectMsg OnEffectMsg;

	// ★ 4301 (0x10CD) OP_ALARM_GAME_SERVER_EVENT_INFO, S→C, тело 16 б — «час удачи» (EXP/Drop ×N).
	//   Эталон uiSetScheduledClock@0xE82DB0 (ui_core.c:78007): memcpy(CHUD+2832, Src, 0x10);
	//     hud[704] = Src[0]  (1=EXP, 2=DROP, 3=оба); hud[705] = 60*(Src[11]-Src[3]) - Src[4] (минут до конца);
	//     hud[707] = *(float*)(Src+12) + 1.0;  hud[706] = (hud[705] >= 0);
	//   Src[1] = месяц, Src[2] = день, Src[3] = час, Src[4] = минута (внутренние «часы» клиента).
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnAsdaEventBonus, const struct FAsdaEventBonusPkt& /*Pkt*/);
	FOnAsdaEventBonus OnEventBonus;

	// ★★★ 6039 `OP_EVENT_SET` / 6041 `OP_EVENT_OFF` (S→C) — НАСТОЯЩИЙ источник ивент-множителей.
	//   Механика живёт в BattleServer (`NpcServer.exe`): таблица `./Event/Event.ini` с ДВУМЯ видами —
	//     `Item_*` → **ДРОП** (шанс выпадения предметов и золота), применяется в `sub_42E3A0@0x42E3A0`
	//               (`flt_47D7E8[map] − 1.0` добавляется к шансу каждого предмета в дроп-рутине
	//               `sub_437A30` и в `ProcessSkill_Steal`);
	//     `Exp_*`  → **ОПЫТ**, применяется там же (`flt_47D8B0[map]`, лог
	//               `"MapIndex %d, Monster %s, iEventFactor %f"`).
	//   `*_Value` — ЦЕЛОЕ В ПРОЦЕНТАХ (`v = Value/100.0`), т.е. 200 = ×2, 150 = ×1.5 (дробные бывают).
	//   NpcServer рассылает 6039/6041 всем; GameServer их сохраняет в `./Event/Event.dat` и пишет
	//   `EventLog.txt` (колонки **ItemDrop** и **Exp**, поля S.Mon/S.Day/S.Hour/S.Min/S.Time/E.*/
	//   Value/MinLevel/MaxLevel, индекс `[50*вид + карта]`). Клиент-эталон 6039/6041 ПРИНИМАЕТ, но
	//   только печатает GM-строку в чат (`Recv_EventSet@0xF4BAD0`) — значки мини-статуса от них
	//   не зажигает, потому дроп-значок в игре и не появлялся никогда.
	//   Тело 6039 (30 б): `u8 result@0` (1 = ok), `S.Mon@7, S.Day@8, S.Hour@9, S.Min@10`,
	//     `E.Mon@11, E.Day@12, E.Hour@13, E.Min@14`, **`i8 Type@15` (0 = дроп, 1 = опыт)**,
	//     **`f32 Value@16` (множитель)**, `i32 channel@20`, `i32 server/map@24`, MinLevel@28, MaxLevel@29.
	//   Тело 6041 (16 б): `u8 result@0`, **`i8 Type@7`**, `i32 channel@8`, `i32 server@12`.
	DECLARE_MULTICAST_DELEGATE_FourParams(FOnAsdaEventSet, bool /*bOk*/, int32 /*Type: 0 дроп, 1 опыт*/,
		float /*Multiplier*/, bool /*bOn*/);
	FOnAsdaEventSet OnEventSet;

	// ★ 6142 (0x17FE) OP_RECV_CLIENT_ADVANTAGE_BUFF_END, C→S — ОТМЕНА баффа кликом по иконке.
	//   Эталон sendInventoryActionRequest@0xE82ED0 (network.c:30741): тело 31 б =
	//     [0..7] анти-реплей токен + [8..23] MD5(payload,7) + [24..30] payload
	//     payload = u32 userIdx, u8 kind (0 = скилловый, 1 = предметный/table2), i16 id.
	bool SendBuffCancel(int32 Kind, int32 Id);

	// ══════════════ СИСТЕМА «ХРАНИТЕЛЯ» (Soul Guardian / Soul Boost) — ЭТАП 3 MINISTATE_WIN ══════
	// ★ 6157 (0x180D) `recvSkillSlotState@0xEF6B10` (network.c:33977), S→C, тело 11 б от Src+9:
	//     `+0 u32 UserIndex` (клиент не читает; сервер кладёт CUser+372)
	//     `+4 u8  AddFlag`   1 = НАЧИСЛЕНИЕ (летящая монета + перезагрузка скиллов рядов),
	//                        0 = списание/распад (просто обновить счётчики), 2 = обновить счётчики
	//     `+5 u8  SkillType` элемент/тип хранителя (сервер CUser+259168)
	//     `+6 i16 SkillIndex` скилл, породивший монеты; −1 = нет
	//     `+8/+9/+10 u8 iSoulCoin[0..2]` заряды трёх рядов (0..10)
	DECLARE_MULTICAST_DELEGATE_SevenParams(FOnAsdaSoulSlotState, int32 /*userIdx*/, int32 /*addFlag*/,
		int32 /*skillType*/, int32 /*skillIndex*/, int32 /*c0*/, int32 /*c1*/, int32 /*c2*/);
	FOnAsdaSoulSlotState OnSoulSlotState;

	// ★ 6156 (0x180C) `charHandleSlotKeyPacket@0xEF6D00` (entity_char.c:50086), S→C, тело 5 б:
	//     `+0 i32 CharId`, `+4 i8 SoulGuardIndex` (−1 = хранитель снят).
	//   Клиент кладёт пару в карту `obj[95]`; применяется в `charVisibilityStateRefresh@0xEF72C0`
	//   (`charObj->gap_4E2[16] = SoulGuardIndex` + CCharManager_AddOrUpdateObject) — это ВИЗУАЛЬНЫЙ
	//   компаньон-хранитель у персонажа и триггер плашки «SOUL GUARDER».
	// ★★★ КЛЮЧ = charId, НЕ userIdx (исправлено 2026-08-08, тройная сверка):
	//   1) сервер кладёт в поле `*(_DWORD*)(CUser+372)` == `CUser::gap_16B` (имя персонажа лежит
	//      следом, +376) — GameServer.c:55672 (`sub_48AFF0`), :55751 (`sub_48B210`), :56744;
	//   2) клиент достаёт запись из карты в `charVisibilityStateRefresh` через
	//      `CharManager_FindByCharId(charId)` (сверка по `char+1052`), а НЕ `FindByUserIdx`
	//      (сверка по `char+190188`) — entity_char.c:6254/6272/50134;
	//   3) живой лог `Log_SoulGuarder/SoulGuarder_20260807.txt`: `UserIndex:75180` у GM_sitxovski —
	//      сессионный индекс ограничен 0..5000, значит это charId.
	//   То же поле `sub_5324F0` → `*(_DWORD*)pUser->gap_16B` кладётся в 4159, где клиент тоже
	//   ищет `CharManager_FindByCharId`.
	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnAsdaSoulGuardIdx, int32 /*charId*/, int32 /*soulGuardIndex*/);
	FOnAsdaSoulGuardIdx OnSoulGuardIdx;

	// ★★★ 6159 (0x180F) `Recv_SkillCast@0xF4E370` (network.c:44800) — РЕЗУЛЬТАТ КАСТА СКИЛЛА,
	//   S→C, рассылается ПО ВСЕМУ СЕКТОРУ, включая самого кастера
	//   (`sub_52E630` → `SendPacket_ClippingSectorInAllUser`, GameServer.c:57395/114720/13299).
	//   ★ Это и есть канал «сосед применил скилл» — в т.ч. скилл ХРАНИТЕЛЯ (1228..1254).
	//   Тело: 8-б шапка + N×29 б записей попаданий:
	//     `+0 u8  result`  1 = успех, иначе код отказа (нотис `dword_15B5D78[result]`)
	//     `+1 i16 casterUserIdx`  ← СЕССИОННЫЙ userIdx (не charId!), сервер кладёт `a2`
	//     `+3 i16 skillCode`
	//     `+5 u8  1`
	//     `+6 i16 flag`    (сервер: v25 — 1 у «мгновенных» скиллов хранителя)
	//   Клиент по успеху: `recordTable_FindIndexById(char, skillCode)` + `CChar_SetState(..., 24)`.
	DECLARE_MULTICAST_DELEGATE_FiveParams(FOnAsdaSkillCast, int32 /*result*/, int32 /*casterUserIdx*/,
		int32 /*skillCode*/, int32 /*flag*/, int32 /*hitCount*/);
	FOnAsdaSkillCast OnSkillCast;

	// ★ 6160 (0x1810) `Recv_SkillCastIdle@0xF4D9C0` (network.c:44642) — НАЧАЛО каста (long-cast),
	//   тоже по всему сектору. Шапка 7 б: `+0 i16 casterUserIdx, +2 i16 skillCode, +4 i16 flag,
	//   +6 u8 1`, дальше N×29. Клиент ставит состояние 14 (`CharFx_AutoSabzil_State(..., 14)`).
	DECLARE_MULTICAST_DELEGATE_FourParams(FOnAsdaSkillCastIdle, int32 /*casterUserIdx*/,
		int32 /*skillCode*/, int32 /*flag*/, int32 /*hitCount*/);
	FOnAsdaSkillCastIdle OnSkillCastIdle;

	// ★ 6161 (0x1811) / 5271 (0x1497) `recvOtherCharSkillUse@0xF4D7D0` (network.c:44599) —
	//   «каст начался/кончился» ПО СЕКТОРУ. 7 б: `u8 result, i16 userIdx, i16 skillId, i16 flag`.
	//   6161 = скиллы ХРАНИТЕЛЯ (GameServer.c:57356 `RecvBattle_SoulGuarderSkillUse`,
	//   :57518 `RecvBattle_SoulGuarderSkillCastingEnd`), 5271 = обычные скиллы (:46427/:46595).
	DECLARE_MULTICAST_DELEGATE_FiveParams(FOnAsdaSkillCastState, int32 /*result*/, int32 /*userIdx*/,
		int32 /*skillId*/, int32 /*flag*/, bool /*bGuardian*/);
	FOnAsdaSkillCastState OnSkillCastState;

	// ★★★ 5257 (0x1489) `Recv_SkillCast@0xF4E370` — РЕЗУЛЬТАТ применения ОБЫЧНОГО скилла.
	//   Функция-обработчик у эталона ОДНА с 6159 (`network.c:1819 case 0x1489: goto LABEL_723`),
	//   но делегат ОТДЕЛЬНЫЙ: `UAsdaSoulGuardSubsystem::OnSkillCast6159` печатает нотис отказа для
	//   ЛЮБОГО result != 1 ещё до фильтра «это скилл хранителя», и общий делегат дал бы двойной
	//   нотис на обычных навыках. Раскладка идентична 6159 (см. FOnAsdaSkillCast выше).
	FOnAsdaSkillCast OnSkillUseResult;
	// ★ 5270 (0x1496) `Recv_SkillCastIdle@0xF4D9C0` — НАЧАЛО каста обычного скилла (motionType 1/7).
	//   Шапка 7 б: i16 casterUserIdx, i16 skillCode, i16 уровень, u8 1; далее N×29 записей.
	FOnAsdaSkillCastIdle OnSkillUseCastIdle;

	// ★ R1 (10.11.2026): структура `FAsdaSkillCastRec` и её разбор вынесены дословно в `Public/Net/AsdaNetWorldTypes.h`.
	//   Имя `UAsdaNetSubsystem::FAsdaSkillCastRec` сохранено псевдонимом — тип тот же.
	using FAsdaSkillCastRec = ::FAsdaSkillCastRec;
	// Записи 29 б пакета 5257 (result == 1) либо 5270 — вместе с шапкой кастера/навыка.
	DECLARE_MULTICAST_DELEGATE_ThreeParams(FOnAsdaSkillCastRecs, int32 /*casterUserIdx*/,
		int32 /*skillCode*/, const TArray<FAsdaSkillCastRec>& /*recs*/);
	FOnAsdaSkillCastRecs OnSkillCastRecs;
	// ★★★ ТЕ ЖЕ записи, но пакетов 6159 / 6160 (скиллы ХРАНИТЕЛЯ ДУШИ) — ОТДЕЛЬНЫМ каналом.
	//   ЗАЧЕМ ОТДЕЛЬНО: подписка на `OnSkillCastRecs` у `UAsdaSoulGuardSubsystem` разная (нотис
	//   отказа печатается только по 6159/6161), смешивать каналы нельзя — был бы двойной нотис.
	// ★★★ ПОПРАВКА 2026-08-11 (aoedmg-agent): УРОН ОТСЮДА ТЕПЕРЬ БЕРЁТСЯ. Прежняя шапка утверждала,
	//   что «боевые числа скиллов хранителя порт ведёт цепочкой 4028/4029» — это ОПРОВЕРГНУТО
	//   данными: во всех живых логах порта (13 файлов, включая 29 МБ трассу 2026-08-10) пакетов
	//   4027/4028/4029/4102 РОВНО НОЛЬ. Следствие дефекта: у скиллов хранителя не показывалось ни
	//   одной цифры урона («записей урона 0» в логе фазы), а смерть цели приезжала сырым HP-пакетом
	//   4015/4017 — то есть ДО кадра удара («побочный моб помирал мгновенно», юзер 2026-08-11).
	//   ЭТАЛОН различия не делает: `case 0x1489` (5257) и `case 0x180F` (6159) диспетчера ведут в
	//   ОДНУ `Recv_SkillCast@0xF4E370`, которая кладёт `*(u16*)(rec+1)` в массив целей и
	//   `*(i32*)(rec+3)` в массив урона (`skill_buff.c:19019-19054`), а показывает их ТИК на кадре
	//   удара. Подписчиков теперь двое: разворот кастера (мир) и парковка урона (слой показа).
	//   ЭТАЛОН: 6159 и 5257 разбирает ОДНА функция `Recv_SkillCast@0xF4E370`; она пишет
	//   `LOWORD(pChar->targetRenderId) = targetWords[0]` (индекс ПЕРВОЙ 29-байтной записи) и ставит
	//   `CChar_SetState(pChar, 24)`. Курс потом каждый кадр держит `objAI_FaceTargetByUserIdx@0xA23350`
	//   при `state ∈ {5, 24, 14, 27, 13} && targetRenderId != −1`. Компаньон-хранитель своего курса
	//   НЕ имеет: `charPreviewUpdateRotation@0xFAA520` строит его матрицу как
	//   `Translate(0, +30 + высотаТранспорта, 0) × ownerWorldMatrix`, т.е. он наследует курс ВЛАДЕЛЬЦА
	//   (и эффекты слоёв 31/32/33 висят на костях `M_DUMMY`/`G_DUMMY` владельца — там же).
	//   ⇒ «хранитель бьёт не соблюдая направление» лечится разворотом ИГРОКА, а не компаньона.
	FOnAsdaSkillCastRecs OnSoulSkillCastRecs;
	// Разбор N записей по 29 б, начиная с Off (границы проверяются; неполный хвост отбрасывается).
	static void ParseSkillCastRecs29(const uint8* Data, int32 Len, int32 Off, int32 N,
		TArray<FAsdaSkillCastRec>& Out);

	// ★ Локализованный нотис отказа по коду результата 5257/6159/5271/6161: эталонная таблица
	//   `dword_15B5D78` (VA 0x15B5D78, файловый оффсет 0xC98D78 в AsdaStory.exe сборки IDB),
	//   35 значащих элементов. Возвращает 0, если код вне таблицы.
	static int32 SkillFailNoticeLocId(int32 ResultCode);

	// ★★★ ОДНА ЗАПИСЬ ЦЕЛИ (29 б) заявки на применение навыка.
	//   `Type` — байт 0 записи: **1 = цель-ПЕРСОНАЖ** (само-каст либо игрок), **0 = цель-СУЩНОСТЬ**
	//     (моб, индекс = renderId). Иные значения эталон не пишет.
	//   `Idx`  — i16: renderId моба ЛИБО сессионный userNum игрока. charId (i32) сюда не кладётся
	//     НИКОГДА (закон charid-vs-usernum: сервер молча проигнорирует заявку либо ответит отказом).
	struct FAsdaSkillUseRec
	{
		int32 Type = 1;
		int32 Idx  = -1;
	};

	// ★★★ 5256 / 6158 (C→S) — применение навыка. В ЭТАЛОНЕ ЭТО ОДНА И ТА ЖЕ ЗАЯВКА: все восемь
	//   отправителей (`skillCastSelfSendPacket@0x9FE210`, `Skill_SendSkillUse@0x9FED00`,
	//   `Skill_SendSkillUseOnTarget@0x9FFB10`, `skillCastAreaSendPacket@0xA00570`,
	//   `Skill_SendAreaSkillTargetsCast@0xA01140`, `Skill_SendAreaSkillCast@0xA01DA0`,
	//   `skillSendUseSkillPacket@0xA02AB0`, `skillCastAreaTargetsAndSend@0xA12800`) собирают
	//   ОДИНАКОВОЕ тело и последней строкой выбирают опкод:
	//     `if (skillRecord->skillCode < 1201) → 5256, иначе → 6158`
	//   (skill_buff.c:2226-2234 / 2571-2578 / 3337-3345 и т.д.). Поэтому опкод здесь НЕ параметр —
	//   он выводится из кода навыка ровно так же. Скиллы Хранителя души (1228..1254) уходят этим же
	//   вызовом, а не отдельной функцией: своей у эталона нет.
	//   Число записей — от 0 до 20. НОЛЬ записей валиден (`Skill_SendAreaSkillTargetsCast` при
	//   `targetMode != -1`), сервер считает их по длине кадра: `iRecvCount = (Size − 41) / 29`.
	//   `GroundTileX/Z`: тайлы точки на земле для навыков по площади (иначе 0).
	bool SendSkillUseRequest(int32 SkillCode, int32 TileX, int32 TileZ,
		const TArray<FAsdaSkillUseRec>& Recs, int32 GroundTileX = 0, int32 GroundTileZ = 0);

	// Одиночная запись — самый частый случай (само-каст, цель-моб, цель-игрок).
	bool SendSkillUse5256(int32 SkillCode, int32 TileX, int32 TileZ,
		int32 TargetType, int32 TargetIdx, int32 GroundTileX = 0, int32 GroundTileZ = 0);

	// ★ 5231 (C→S) — применение навыка СОУЛМЕЙТА (`netSkill_SendSoulmateUse@0xA07540`), 218 б.
	//   Раскладка и конструктор payload — в .cpp у реализации. TargetUserIdx — сессионный i16.
	bool SendSoulmateSkillUse5231(int32 SoulmateSkillId, int32 TargetUserIdx);

	// ══════════════ ОКНО РАЗГОВОРА С NPC: диспетчер команд 30001…30088 (этап Э3) ═════════════════
	// Все отправители ниже — порт таблицы `CGameUI_HandleControlCommand@0xD87590` (game_main.c:9892).
	// Размеры пакетов взяты из самих эталонных сборщиков (payload + 24 байта шапки `SendSecured`).
	// ⚠ Отчёт реверса §6.2 указывал для 6124 «30 б» — по коду там **70** (payload 0x2E), исправлено.

	// 5477 `netSendReqWord_op5477@0xD8C890` — ТЕЛЕПОРТ/ПОРТАЛ. payload 2 б (u16), пакет 26 б.
	//   Коды 30031 (слово = байт `WarpListTable.BIN[listIndex] + 4`), 30033/30040 → 3, 30036 → 4,
	//   30037 → 5, 30039/30041 → 0, 30042 → 7, 30063 → 21, 30064 → 22.
	bool SendPortalMove5477(int32 ReqWord);

	// 6059 `netSendPacket6059@0xD8CEA0` — «событийный NPC» (код 30038). payload 3×u16, пакет 30 б.
	//   ★ 27.09.2026: слово +0 — СВОЙ userNum (`effectSlots[111].field_5B8` = `char+190188`), НЕ charId
	//   (прежняя подпись «charId урезан до слова» была ошибкой; отчёт `Reports/owner_0927/charid_senders_audit.md`).
	//   Троттлинг оригинала — «один раз в календарный день на цель» (поля `EventNpc*` ниже, отказ — нотис 7501 окном (0, 24)).
	bool SendEventNpc6059(int32 InSelfUserNum, int32 NpcId, int32 TargetId);

	// 6098 `netSendGuarded_op6098_throttled@0xD8D5A0` — ПОКУПКА У NPC ПО КОДУ ОБМЕНА.
	//   payload 3×u16 (свой userNum, npcId, тип обмена), пакет 30 б, троттлинг 0xBB8 = 3000 мс.
	//   Коды 30048 (тип 3), 30049 (4), 30052…30055 (9/10/11/12), 30079…30082 (58/59/60/57),
	//   30083…30085 (48/49/50). ★ Прошедший троттлинг пишет тип в ОБЩЕЕ с 6059 поле `EventNpcLastTarget` (эталон `self[24116]`).
	bool SendNpcExchange6098(int32 InSelfUserNum, int32 NpcId, int32 ExchangeType);

	// 6124 `netSendGuarded_op6124_invMatchList@0xD8DBF0` — ПРИЗЫВ РЕЙД-БОССА (30050 / 30061).
	//   payload 0x2E = 46 б: i32 charId + 6 записей по 7 б (i32 itemCode, u8 контейнер, u16 слот);
	//   пакет 70 б. Записи — первые `MaxMatches` ячеек контейнера 2 с нужным кодом предмета, пустые — {−1, 0, −1}.
	//   ★ 27.09.2026: +0 — charId (`CCharacter::userIdx` = `char+1052`), сервер сверяет с `CUser+372` (отказ 6125 код 2).
	bool SendRaidMonsterCall6124(int32 SelfCharId, int32 MatchItemCode, int32 MaxMatches);

	// 6091 `itemSendCraftRequest_EEE0@0xD9EEE0` — КРАФТ ×1/×5/×10 (30043…30045).
	//   payload 3×u16 (свой userNum, npcId, количество), пакет 30 б. Отправке предшествует
	//   проверка пяти материалов 20977…20981 — она живёт в подсистеме диалога.
	//   ★ 27.09.2026: +0 — СВОЙ userNum (`Target_GetSelectedUserIdx(локальный)` — миснейм, это `char+190188`), не цель.
	bool SendItemCraft6091(int32 InSelfUserNum, int32 NpcId, int32 Count);

	// 6146 `netSendReq6146@0xBB5380` — «прыжок к цели» (30071 / 30072). payload 27 б, пакет 51 б:
	//   u8 режим, i16 userNum, затем 6 × i32 (константы из таблицы диспетчера).
	//   ★ 27.09.2026: у 30071/30072 эталон кладёт СВОЙ userNum (тот же миснейм), сервер поле не читает.
	bool SendClientTargetJump6146(int32 Mode, int32 UserNum, const int32 (&Six)[6]);

	// 6746 `antiCheat_SendHeartbeat6746@0xFE9B60` — запрос CampWar (код 30077).
	//   payload 4 б (i32 charId), пакет 28 б.
	bool SendCampWarReq6746(int32 SelfCharId);

	// ★ 06.10.2026 (волна 2-Г): 6744 — СТАВКА НАЛОГА ГОРОДА (окно `Town_as_WIN` 79300, кнопка «Да» окна 1520 id 86).
	//   Эталон `AntiTamper_SendHeartbeatPacket@0xBCE890` (имя IDA ВРЁТ; security.c:953-1077): payload 5 б =
	//   +0 i32 charId (`g_LocalPlayer+1052`), +4 u8 позиция полосы 79314 (`sub_9331D5` = поле +154); пакет 29 б, a6 = 1.
	//   [ПРОТОКОЛ] Сервер `User_Game_TaxChange@0x46B230`: `sub_523F00(a3, 5, …, 1)` — конверт `SendSecured`; ставка < 26.
	bool SendTownTaxChange6744(int32 SelfCharId, int32 Rate);
	// ★ 06.10.2026 (волна 2-Г): 6134 — СВИТОК ПРИЗЫВА (`special_itemlist.SubType` 64, окно 1552 вид 59, кнопка 1554).
	//   Эталон `netSendNamedTargetCmd_6134@0xDCDB20` (network.c:26296): payload 26 б = +0 i32 charId, +4 i16 ячейка сумки
	//   (`CMsgBoxMgr+0x32F4`), +6 char[20] имя цели (cp1251, `strcpy` из поля 1558); пакет 50 б, a6 = 1.
	//   [ПРОТОКОЛ] Сервер `RecvClient_FunctionItem_Summon@0x4F2900` (`sub_523F00(a3, 0x1A, …, 1)`), ответ 6135.
	bool SendScrollSummon6134(int32 SelfCharId, int32 BagCell, const FString& TargetName);
	// ★ 06.10.2026 (волна 2-Г): 5458 — СВИТОК ПЕРЕМЕЩЕНИЯ к игроку (`SubType` 38, окно 1552 вид 60, кнопка 1554).
	//   Эталон `net_sendSecureCmdPacket@0xA0DE30` (network.c:7281): payload 22 б = +0 i16 ячейка сумки (`CMsgBoxMgr+0x32F0`),
	//   +2 char[20] имя; пакет 46 б, a6 = 1. [ПРОТОКОЛ] Сервер `RecvClient_FunctionItem_Meet@0x4F33E0` (0x16, конверт
	//   `SendSecured`), ответ 5459. Гейт «души» стоит в диспетчере сервера (`GameServer.c:19732`).
	bool SendScrollMeet5458(int32 BagCell, const FString& TargetName);
	// ★ 06.10.2026 (волна 2-Г): 6137 — ОТВЕТ ЦЕЛИ НА ПРИЗЫВ СВИТКОМ (окно 1520 id 61, «OK» 104002 / «Отказать» 104003).
	//   Эталон `net_SendActionWithAntiCheatMask@0xDCE650` (network.c:26560): payload 21 б = первые 20 байт тела 6136
	//   (+0 i32, +4 i16, +6 u8, +7 i32, +11 i16, +13 i32, +17 i16, +19 u8) + ответ (1 «да», 10 «нет»); пакет 45 б, a6 = 1.
	//   [ПРОТОКОЛ] Сервер `RecvClient_PremiumSummon_Relay_Res@0x4F30D0` (`sub_523F00(a3, 0x15, …, 1)`).
	bool SendSummonAnswer6137(const TArray<uint8>& Body6136, uint8 Answer);
	// ★ 09.11.2026 (пачка 20 баг 8): 6138 — ПРОВЕРКА ИМЕНИ предмета смены имени (окно 7900, кнопка 7905 «Доступность»).
	//   Эталон `AntiTamper_SendStringChecksumPacket@0xD33AB0` (имя IDA ВРЁТ): payload 24 б = +0 i32 charId, +4 char[20] имя (cp1251);
	//   пакет 48 б. [ПРОТОКОЛ] Сервер `RecvClient_CharNameCheck@0x55D7C0` (`sub_523F00(a3, 0x18, …, 1)` — конверт `SendSecured`),
	//   DB 2450 `UPNickCheck` → ответ 6139 (5 б). Разбор — `UAsdaCharRenameSubsystem`.
	bool SendCharNameCheck6138(int32 SelfCharId, const FString& NewName);
	// ★ 09.11.2026: 6140 — СМЕНА ИМЕНИ (вопрос 67 «ОК»). Эталон `netSendTsMaskedReq6140@0xD33430`: payload 27 б = +0 i32 charId,
	//   +4 char[20] имя, +24 i16 ячейка, +26 u8 контейнер; пакет 51 б. [ПРОТОКОЛ] Сервер `RecvClient_CharNameChange@0x55D910`
	//   (`sub_523F00(a3, 0x1B, …, 1)`), DB 2452 `UPCharRename` → ответ 6141 (86 б).
	bool SendCharNameChange6140(int32 SelfCharId, const FString& NewName, int32 Cell, int32 Container);

	// ★ 06.10.2026 (волна 2-Г): ПЕРЕХОД НА КАНАЛ ПО СВИТКУ ПЕРЕМЕЩЕНИЯ — порт `netPortalChannelTransfer@0xF49EE0`
	//   (network.c:44027), его зовёт 5459 код 1 (`uiTooltip_ShowItemStatusNotice@0xF26880`). Адрес канал-сервера, порт,
	//   карта и тайл прибытия — из тела 5459 (+1 IP[16], +17 порт, +19 X, +21 Y, +23 карта). Номер канала эталон НЕ
	//   меняет. Дорога та же, что у призыва 6013 (реконнект на следующем тике, затем портальный логон 5058).
	//   Why — для лога. При взведённом `bDebugCaptureTransfer` (самотест) переход только записывается, сокет не трогается.
	void StartScrollChannelTransfer(const FString& Ip, int32 Port, int32 MapId, int32 TileX, int32 TileZ, const TCHAR* Why);
	// Самотест: переход по свитку не выполнять, а записать параметры (закон «штатный приём, перехват исхода»).
	bool bDebugCaptureTransfer = false;
	// Самотест: параметры последнего перехвата перехода (карта, тайл X/Z, порт, IP).
	int32 DebugTransferMap = -1, DebugTransferX = -1, DebugTransferZ = -1, DebugTransferPort = -1;
	FString DebugTransferIp;

	// ★★★ 6723 `netSendCampWarjoin@0xCA5750` (network.c:15916) — ВСТУПИТЬ В ОСАДУ, клик по NPC на
	//   карте CampWar (mapId 19). Ответ сервера — 6724 `CCampWar_OnObjectEffectPacket`.
	//   ⚠ Имя опкода в enum («user game regist cancel») к делу отношения не имеет — авторитет
	//     номер 6723, не название. Вызов эталона:
	//     `CNetwork_Send(g_pNetwork, ..., buf, 32, 6723, 1,1,1,0,0)`.
	//   payload 8 б, пакет 32 б (конверт тот же, что у клановых 6706/6708, social.c:4711/4749):
	//     +0 i32 charId локального игрока (эталон: `*(i32*)(g_pCampWarClan + 0x22D0)`)
	//     +4 u16 id фракции/лагеря      (эталон: `*(u16*)(g_pCampWarClan + 0x0B7C)`, заполняет
	//                                    приём 6713 `Recv_ClanMyMembership@0xCA4AB0` — в порту это
	//                                    `UAsdaWorldSubsystem::SelfCampId`)
	//     +6 u16 МЛАДШЕЕ СЛОВО серверного id NPC-сущности (эталон: `sub_93CD7A(npc)` = `get_o73`
	//        @0x967BB0 = `*(i32*)(npc + 0x1CC)`, оно же поле `+460`, по которому ищут в списках
	//        `findChildByMemberId@0xFCDF10` / `List_FindNodeByField460@0xF91A00`).
	bool SendCampWarJoin6723(int32 SelfCharId, int32 FactionId, int32 NpcEntityId);

	// ★★★ 6749 `netSendReqSelfId_op6749@0xFEA180` (network.c:50233) — код меню диалога **30078**.
	//   ⚠ РАНЬШЕ СЧИТАЛОСЬ, ЧТО ДАННЫХ НЕТ: реестр отложенного (строка 46) стоял на «второй глобал
	//   `word_2BFF13C` в срезе не разобран». РАЗОБРАН 2026-08-20 арифметикой адресов:
	//   `g_pCampWarClan` = **0x2BFE5C0** (`RE/ida/bng_002.tsv:7`), а `0x2BFE5C0 + 0x0B7C = 0x2BFF13C`
	//   — то есть `word_2BFF13C` это РОВНО то же поле, что `g_pCampWarClan + 0x0B7C` в пакете 6723:
	//   **id фракции/лагеря**, который заполняет приём 6713 `Recv_ClanMyMembership@0xCA4AB0`.
	//   Подтверждение с другой стороны: у обоих пакетов одинаковая пара «i32 charId + u16 фракция».
	//   payload 6 б: +0 i32 charId (`*(i32*)(g_LocalPlayer + 1052)`), +4 u16 id фракции. Пакет 30 б.
	bool SendCampWarSelfId6749(int32 SelfCharId, int32 FactionId);

	// ★ 30.10.2026 (выбор армии): 6700 `Clan_Send_Op6700@0xCA21A0` — ЗАПРОС СВЕДЕНИЙ ВОЙНЫ АРМИЙ (ответ 6701). Эталон шлёт
	//   его при открытии разговора с NPC 133/179/180/184 с режимом 0. Payload 5 б: +0 i32 charId (`CCampWar+0x22D0`),
	//   +4 u8 режим (сервер `User_Game_GetMainInfo@0x461CA0` принимает 0…2). Конверт с секретом и MD5, 29 б.
	bool SendCampWarMainInfo6700(int32 SelfCharId, uint8 Mode);
	// ★ 30.10.2026 (выбор армии): 6702 `Clan_Send_Op6702@0xCA2360` — ВЫБРАТЬ АРМИЮ (ОК окна 46000; ответ 6703).
	//   Payload 6 б: +0 i32 charId, +4 u16 армия 0…2 (сервер `User_Game_SelectSquad@0x462480` читает @4 и не проверяет
	//   диапазон). Конверт с секретом и MD5, 30 б.
	bool SendCampWarSelectSquad6702(int32 SelfCharId, int32 Squad);
	// ★ 30.10.2026 (CampWar целиком): ОБЩИЙ отправитель пакетов «Битвы армий» C→S — тело (payload) собирает подсистема
	//   CampWar (`UAsdaCampWarSubsystem`) 1:1 с эталонным отправителем (раскладки — `Reports/faction_select/REG_SPEC.md`,
	//   `BATTLE_SPEC.md`), конверт — общий `SendSecured` (8 секрет + MD5-16 + тело). Пропускает ТОЛЬКО опкоды из белого
	//   списка CampWar в `AsdaNetSubsystem_World.cpp` (`kCampWarSendOps`), иначе отказ с журналом — не лазейка для чужих пакетов.
	bool SendCampWar(uint16 Opcode, const uint8* Payload, int32 Len, const TCHAR* What);

	// ═══════════════ ПИТОМЦЫ, ВОСЕМЬ ЗАПРОСОВ C→S. Конверт у ВСЕХ — `SendSecured` ═══════════════
	//   (8 б секрет + 16 б MD5(тело) + тело), `bPad` по умолчанию. Проверено ПОСЕМЕЙНО на сервере:
	//   `sub_523F00(pkt, <длина тела>, userNum, 1)` — четвёртый аргумент 1 ⇒ тело с `pkt+33`
	//   (закон `packet-envelope-two-kinds`; по аналогии с соседями не переносить).
	// ★ Длины тела — СЕРВЕРНЫЕ (второй аргумент `sub_523F00`), не клиентские: лишний байт сервер
	//   отобьёт молча (закон `packet-size-check-no-padding`).
	// ★ Первое поле у семи из восьми — `charId` (i32), НЕ `userNum`: сервер сверяет его с `CUser+372`
	//   и при расхождении пишет «RecvUserIndex/ServerUserIndex» и выходит (закон `charid-vs-usernum`).
	//   У 6148 поля charId нет вовсе — тело начинается с `petUniqueId` (GameServer.c:36624-36640).

	/** C→S 6100, тело 8 б: `+0 i32 charId`, `+4 i32 petUniqueId`. Призыв/отзыв — тумблер, решение
	 *  принимает сервер. Эталон `upgradeSendStartSecure@0xDC7F40` (item_enhance.c:4357-4458),
	 *  обработчик `Recv_CallPetReq` (GameServer.c:34404). Ответ — 6101 (9 б). */
	bool SendPetCall6100(int32 SelfCharId, int32 PetUniqueId);

	/** C→S 6102, тело 13 б: `+0 i32 charId`, `+4 u8 bag1`, `+5 i16 slot1` (КАТАЛИЗАТОР, subType 107),
	 *  `+7 u8 bag2`, `+8 i16 slot2` (ЯЙЦО, subType 109), `+10 u8 bag3`, `+11 i16 slot3` (бустер, −1 = нет).
	 *  ★ Порядок предметов сверен С ОБЕИХ сторон и совпадает: сервер ждёт 107, затем 109
	 *  (GameServer.c:34591 vs :34618), клиент кладёт `pSrcSlotFeed`→+4/+5, `pItemFeed`→+7/+8,
	 *  `pItemTool`→+10/+11 (npc_mob_pet.c:4930-4938). Имена эталона при этом ВЕРНЫ:
	 *  `pItemFeed` — действительно яйцо (сетка 42050..42055), `pItemTool` — бустер
	 *  (сетка 42070..42075, npc_mob_pet.c:4184-4186, подпись 42090 = loc 100407 «Бустер»).
	 *  Сбивает с толку только `pSrcSlotFeed`: это предмет, ПО КОТОРОМУ ОТКРЫЛИ ОКНО
	 *  (`petWindowOpen`, npc_mob_pet.c:4096), и у него subType 107 — катализатор.
	 *  ⚠ Прежняя формулировка «имена эталона врут» (`pet_windows_re_report.md` §7.3) ОПРОВЕРГНУТА
	 *  ревизией 2026-09-21 и перепроверена здесь построчно. Ответ — 6103 (18 б). */
	bool SendPetIncubate6102(int32 SelfCharId, int32 Bag1, int32 Slot1, int32 Bag2, int32 Slot2,
	                         int32 Bag3, int32 Slot3);

	/** C→S 6108, тело 11 б: `+0 i32 charId`, `+4 i32 petUniqueId`, `+8 u8 bag`, `+9 i16 slot`.
	 *  Кормление (предмет subType 105/106). Эталон `netSendItemPairRequest@0xDC8FC0`
	 *  (network.c:25745-25861), обработчик GameServer.c:35479. Ответ — 6109 (89 б). */
	bool SendPetFeed6108(int32 SelfCharId, int32 PetUniqueId, int32 ItemBag, int32 ItemSlot);

	/** C→S 6110, тело 8 б: `+0 i32 charId`, `+4 i32 petUniqueId`. Выбросить питомца (необратимо).
	 *  Обработчик `Recv_PetLiberate_Req` (GameServer.c:35798). Ответ — 6111 (9 б). */
	bool SendPetRelease6110(int32 SelfCharId, int32 PetUniqueId);

	/** C→S 6112, тело 18 б: `+0 i32 charId`, `+4 i16 unionIndex`, `+6/+10/+14 i32 petUniqueId[3]`.
	 *  Эволюция (юнион). Эталон `skillTree_SendLearnRequest@0xEE01A0` (★ МИСНЕЙМ: это НЕ изучение
	 *  навыка), обработчик `Recv_PetUnion_Req` (GameServer.c:35936). Ответ — 6113 (56 б). */
	bool SendPetUnion6112(int32 SelfCharId, int32 UnionIndex, int32 Mat0, int32 Mat1, int32 Mat2);

	/** C→S 6118, тело 24 б: `+0 i32 charId`, `+4 i32 petUniqueId`, `+8 char[16] newName` (cp1251).
	 *  Переименование — один раз за жизнь питомца. Обработчик GameServer.c:36214. Ответ — 6119 (25 б). */
	bool SendPetRename6118(int32 SelfCharId, int32 PetUniqueId, const FString& NewName);

	/** C→S 6121, тело 7 б: `+0 i32 charId`, `+4 u8 bag`, `+5 i16 slot`. Расширитель ячеек
	 *  (subType 104, +6 за раз). Обработчик GameServer.c:36397. Ответ — 6122 (13 б). */
	bool SendPetSlotUp6121(int32 SelfCharId, int32 ItemBag, int32 ItemSlot);

	/** C→S 6148, тело 9 б: `+0 i32 petUniqueId`, `+4 i16 itemSlot`, `+6 u8 itemBag`,
	 *  `+7 i16 premiumSlot` (−1 = без защиты). ★ charId в теле НЕТ. Лимит-брейк (рост предела
	 *  уровня), обработчик `RecvClient_PetMaxLevelUp` (GameServer.c:36570). Ответ — 6149 (110 б). */
	bool SendPetLimitBreak6148(int32 PetUniqueId, int32 ItemBag, int32 ItemSlot, int32 PremiumSlot);

	// ★ 5043 `sendStartQuestBookProcess@0xD9D9D0` (в отчёте реверса — `netSendCraftSlotRequest`):
	//   ПРИНЯТЬ (режим 1) и СДАТЬ (режим 2) шаг квеста из окна диалога.
	//   · режим 1: payload 4 б = u16 npcId, u16 индекс записи в таблице «NPC ВЫДАЁТ» (0…9); пакет 28 б;
	//   · режим 2: payload 7 б = u16 npcId, u16 индекс в таблице «NPC ПРИНИМАЕТ», u8 номер цели,
	//     u16 номер слота квеста; пакет 31 б.
	//   Оригинал держит по одному флагу «заявка в полёте» на каждый режим (`this[159]` / `this[158]`)
	//   и при повторном нажатии показывает нотис 6523 / 6524 — гейт живёт в подсистеме диалога.
	bool SendQuestStepAccept5043(int32 NpcId, int32 GiveSlotIndex);
	// ★★★ СДАЧА (завершение шага) КВЕСТА — опкод 5041, НЕ 5043. Было 5043: сервер там считает MD5
	//   по 4 байтам, наш семибайтовый MD5 не сходился, пакет молча выбрасывался. Разбор —
	//   `Reports/quest_accept_fix.md` §5. Тело 7 б (u16 npcId, u16 слот «ПРИНИМАЕТ» 0…9,
	//   u8 индекс выбранной награды, u16 номер слота журнала). Ответ — 5042.
	bool SendQuestTurnIn5041(int32 NpcId, int32 TakeSlotIndex, int32 ObjIndex, int32 QuestSlotNo);

	// ★ 5044 `CQuest_RecvPacket@0xD9D260` — ОТВЕТ на 5043. Тело 0x2F.
	//   `Result` = body[0]: 0 = принято/сдано (звук + переход на страницу `TalkId`), иначе код отказа
	//   (эталон показывает тултип 6191/6192/6176/6181…6186 — таблица в подсистеме диалога).
	DECLARE_MULTICAST_DELEGATE_ThreeParams(FOnAsdaQuestStepResult, int32 /*Result*/, int32 /*TalkId*/, const TArray<uint8>& /*Body*/);
	FOnAsdaQuestStepResult OnQuestStepResult;

	// ═══════════ ★★★ ЖУРНАЛ ЗАДАНИЙ (окно 700 QUEST_WIN) — добавлено 2026-08-22 ══════════════════
	//   Полный реверс — `Reports/quest_window_re.md` §4, реализация — `quest_window_impl.md`.
	//
	// ★★★ 5006 C→S «ОТКАЗАТЬСЯ ОТ ЗАДАНИЯ». Тот же опкод, что «уничтожить предмет», — сервер
	//   (`CharItemDump`, GameServer.c:83508) ветвится по ПОД-БАЙТУ `payload+4`:
	//     1 = экипировка, 2 = инвентарь, **4 = ЯЧЕЙКА ЖУРНАЛА КВЕСТОВ**.
	//   ⚠ Отправитель эталона называется `CQuest_Send_Accept@0xC612C0` — МИСНОНИМ: он вызывается
	//     только из веток `case 6174/6175` кнопки «ОК» модалки, а 6174 в очередь ставит КОРЗИНА 713.
	//     Это отказ от задания, приём квеста идёт из окна NPC опкодом 5043.
	//   Payload 11 б (пакет 35 = 24 + 11): `@0 u16 номер ячейки журнала`, `@4 u8 = 4`, остальное нули.
	//   Ответ игроку — обновлённый 5047.
	bool SendQuestAbandon5006(int32 QuestSlotIndex);

	// ★★★ 5000 C→S «ПЕРЕНЕСТИ ЗАДАНИЕ В ДРУГУЮ ЯЧЕЙКУ ЖУРНАЛА» (`CQuest_Send_SelectSlot@0xC5C760`).
	//   Тот же общий опкод перемещения предмета; пара типов 4→4 = «ячейка журнала → ячейка журнала».
	//   Payload 22 б (пакет 46 = 24 + 22): `@0 u16 откуда`, `@4 u8 = 4`, `@11 u16 куда`, `@15 u8 = 4`.
	//   Сервер (`CharItemMove@0x82166`) отказывает при индексе >= 12 — лимит журнала подтверждён.
	bool SendQuestSlotMove5000(int32 SrcSlotIndex, int32 DstSlotIndex);

	// ★★★ 5052 S→C — ПРОГРЕСС ЦЕЛЕЙ КВЕСТА (`questItemResult_apply@0xC63010`), тело 508 б.
	//   ⚠ До 2026-08-22 этот пакет в порту НЕ ОБРАБАТЫВАЛСЯ ВООБЩЕ: счётчики целей в журнале не
	//   двигались никогда, потому что полный дамп 5047 сервер после каждого убитого моба не шлёт.
	//   Разбор тела — в `UAsdaQuestMarkerSubsystem::ApplyQuestProgressBlob` (там же смещения).
	//   Закон `no-reentrant-net-callbacks`: здесь только копия байт и делегат.
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnAsdaQuestProgress, const TArray<uint8>& /*body508*/);
	FOnAsdaQuestProgress OnQuestProgress;

	// ★ 5042 `recvRewardQuestProcess@0x45CF90` — ОТВЕТ на 5041 «сдать шаг квеста». Тело 501 б:
	//   `StepCode` = body[0] (0 — обычный шаг), `QuestNo` = u16 +1, `Stage` = u16 +3,
	//   `FinishType` = body[5] (2 — квест завершён целиком), `TalkId` = u16 +6;
	//   в теле по +15 лежит обновлённая 44-байтовая запись слота журнала (раскладка 5047).
	DECLARE_MULTICAST_DELEGATE_SixParams(FOnAsdaQuestTurnInResult, int32 /*StepCode*/, int32 /*QuestNo*/,
		int32 /*Stage*/, int32 /*FinishType*/, int32 /*TalkId*/, const TArray<uint8>& /*Body*/);
	FOnAsdaQuestTurnInResult OnQuestTurnInResult;

	// ★ 6120 (0x17E8) — АКТИВНЫЙ ПИТОМЕЦ игрока (pet_system_re.md): 22 б — charId i32@0 (ключ),
	//   petTypeId u16@4 (−1/0xFFFF = убрать), имя char[16]@6 (cp1251). Приходит в AoI-бандле при входе
	//   в зону + при призыве/отзыве/переименовании; периодики нет. (В эталоне поля ошибочно названы posX/posY.)
	DECLARE_MULTICAST_DELEGATE_ThreeParams(FOnAsdaPeerPet, int32 /*charId*/, int32 /*petTypeId*/, FString /*name*/);
	FOnAsdaPeerPet OnPeerPet;

	// ★★★ ПИТОМЦЫ, ВСЕ ТРИНАДЦАТЬ ОТВЕТОВ S→C — ОДИН делегат «опкод + сырое тело».
	//   6101 (9 б) · 6103 (18) · 6104 (29) · 6105 (15) · 6106 (5 + 46·N) · 6107 (8) · 6109 (89) ·
	//   6111 (9) · 6113 (56) · 6119 (25) · 6122 (13) · 6123 (5) · 6149 (110).
	//   Разбор ЦЕЛИКОМ живёт в `UAsdaPetSubsystem` — там же, где модель данных, в которую он
	//   пишет: тринадцать раздельных делегатов с тринадцатью сигнатурами означали бы, что
	//   раскладка полей описана ДВАЖДЫ (здесь и там) и разъедется при первой правке.
	//   Здесь — только копия байт и рассылка (закон `no-reentrant-net-callbacks`).
	// ⚠ 6120 в этот делегат НЕ входит: он уже принимается выше как `OnPeerPet` и его потребитель —
	//   мир (`UAsdaWorldSubsystem::OnPeerPet`). Второго приёмника заводить нельзя
	//   (закон `check-existing-before-creating`).
	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnAsdaPetPacket, int32 /*Opcode*/, const TArray<uint8>& /*Body*/);
	FOnAsdaPetPacket OnPetPacket;

	// ★ 6722 recvCampWarClanMemberUpdate@0xCA6C80 — ФРАКЦИЯ/ЗВАНИЕ ЧУЖОГО игрока (faction_peer_re.md):
	//   9 б: charId i32@0 (КЛЮЧ — FindByCharId), userIdx u16@4 (клиент игнорирует), campId i16@6
	//   (SquadIndex; −1 = нет; 0=Свет/СИНИЙ, 1=Тьма/красный, 2=Хаос/зелёный), grade i8@8 (звание →
	//   ячейка faction_grade.tex = max(grade,1)−1). Сервер шлёт только при campId>=0: вход в мир,
	//   взаимный AoI, смена/сброс отряда. Единственный сетевой писатель campId/grade для НЕ-локальных
	//   (6713 пишет только своего). Запись живёт в списке до спавна чара — штатный кэш оригинала.
	DECLARE_MULTICAST_DELEGATE_ThreeParams(FOnAsdaPeerFaction, int32 /*charId*/, int32 /*campId*/, int32 /*grade*/);
	FOnAsdaPeerFaction OnPeerFaction;

	// ★ 6152 PartyMark_Helper2@0xE64750 — СОУЛМЕЙТ-СТЕЙТ СОСЕДЕЙ (wings_soulmate_re.md §J): сервер шлёт
	//   его в AoI-бандле сразу после каждого 4005 (Send_MoveSectorInClippingUserInfo@0x41AD20), гейт
	//   iSoulMateIndex!=-1 → приходит ТОЛЬКО про соседей с парой. Запись 14б: charId u32@0 (ключ —
	//   FindByCharId, НЕ userIdx!), soulIdx i32@4 (>0 = КОЛЬЦО ON), partnerCharId u32@8, флаг силуэта u8@12.
	//   (5220 соседям НЕ рассылается — только событийно/себе, поэтому кольца чужих шли мимо порта.)
	DECLARE_MULTICAST_DELEGATE_ThreeParams(FOnAsdaSoulmatePeer, int32 /*charId*/, int32 /*soulIdx*/,
		int32 /*partnerCharId*/);
	FOnAsdaSoulmatePeer OnSoulmatePeer;

	// ★ 5203 Recv_SoulmateBond@0xE65FE0: subcode@0 (1=БОНД СОЗДАН), pairIdx u32@1, initiatorUidx u32@5,
	//   имя партнёра @9. При subcode 1 — церемония soulmatec у обоих + звук S_c_SMMate01 (wings §F1).
	DECLARE_MULTICAST_DELEGATE_FourParams(FOnAsdaSoulmateBond, int32 /*subcode*/, int32 /*pairIdx*/,
		int32 /*initiatorUidx*/, FString /*partnerName*/);
	FOnAsdaSoulmateBond OnSoulmateBond;
	// ★ 5207 soulmateWnd_handleResponsePacket@0xE67590: subcode@0 (1/3=РАЗРЫВ), имя@5. userIdx НЕТ —
	//   кольцо гаснет у себя и партнёра (по сохранённому userIdx) (wings §F2).
	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnAsdaSoulmateBreak, int32 /*subcode*/, FString /*name*/);
	FOnAsdaSoulmateBreak OnSoulmateBreak;

	// ★★★ 5110/5111 — ОДНА И ТА ЖЕ запись соулмейта (251 б), реверс `soulmateUpdateInfo@0xE64C30`
	//   и `soulmateWnd_ApplyInfoPacket@0xE65060` (обе пишут в `CSoulmateWnd+1357`, он же `gap_54D`):
	//     • **5110** шлёт запись ЦЕЛИКОМ: `memcpy(this+1357, Src, 0xFB)` ⇒ rec[n] = body[n];
	//     • **5111** шлёт её БЕЗ 13-байтового заголовка: все копии идут в `gap_54D[13+n] ← body[n]`
	//       ⇒ rec[n] = body[n−13]. Сдвиг 13 проверен по ВСЕМ 19 присваиваниям функции.
	//   ⚠ 5111 копирует НЕ ВСЕ байты: rec[38],[39],[41] (класс/ступень/звание-2) он пропускает —
	//     они остаются от последнего 5110. Повторяем это 1:1, иначе профессия будет мигать.
	//   Раскладка записи (доказана `CSoulmateWnd_RefreshInfo@0xE63410`):
	//     rec[0..3] слот · rec[4..7] charId партнёра · rec[8..9] userNum · rec[10..12] gap_8BE
	//     rec[29] уровень · rec[38] класс (≥101 ⇒ поле пустое) · rec[39] ступень класса
	//     rec[40]/rec[41] звание · rec[42..45] iSoulMateIndex · rec[50] уровень Дружбы
	//     rec[63..] имя партнёра (cp1251)
	//   Отдаём СЫРОЕ тело + опкод: применение к записи живёт в UAsdaSoulmateSubsystem (как в эталоне).
	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnAsdaSoulmateCard, int32 /*opcode*/, const TArray<uint8>& /*body*/);
	FOnAsdaSoulmateCard OnSoulmateCard;

	// ★ 5205 (миснейм itemEnchant_OnResultPacket) — ОТКАЗ на запрос разрыва 5204 (§5.2): [0]=1 в бою /
	//   2 Smidx не совпал / 3 партнёр не онлайн. Доказано вызовом с g_pSoulmateWnd в диспетчере.
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnAsdaSoulmateBreakRefused, int32 /*reason*/);
	FOnAsdaSoulmateBreakRefused OnSoulmateBreakRefused;

	// ★ 5205 ЦЕЛИКОМ (61 б). Одного кода недостаточно: ветка «партнёр просит разрыва»
	//   отвечает пакетом 5206, а ему нужны charId/userNum партнёра и эхо-байты из этого же тела.
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnAsdaSoulmateBreakRequest, const TArray<uint8>&);
	FOnAsdaSoulmateBreakRequest OnSoulmateBreakRequest;

	// ★ 5484 soulmateParseSearchResult@0xE73E00 — список кандидатов поиска (§5.6). Запись переменной
	//   длины (33 + rec[31]); здесь отдаём тело целиком, разбор с проверкой длины (D7) — в подсистеме.
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnAsdaSoulmateSearch, const TArray<uint8>& /*body*/);
	FOnAsdaSoulmateSearch OnSoulmateSearch;
	// ★ 5485 findSoulmateWin_Show@0xE741B0 (волна 1-А, 06.10.2026) — сервер (`Send_SoulSearch@0x54DFD0`) шлёт его ПОСЛЕДНИМ,
	//   тело 0 б: показать окно поиска 8200 и перерисовать список, накопленный пакетами 5484. Подписчик — соулмейт.
	FSimpleMulticastDelegate OnSoulmateSearchShow;

	// ★ Детект МЁРТВОГО game-соединения (живой лог 2026-07-29 19:26: сервер замолк, «send stalled 0/N»,
	//   все отправки FAIL, клиент молча «дёргался» замахами). 3 подряд неудачные боевые/move-отправки →
	//   bInGameConnected=false + бродкаст (мир-система показывает нотис, контроллер гасит бой).
	DECLARE_MULTICAST_DELEGATE(FOnAsdaGameDisconnected);
	FOnAsdaGameDisconnected OnGameDisconnected;
	int32 SendFailStreak = 0;
	bool TrackSend(bool bOk);                    // учёт полосы неудач отправки; возвращает bOk

	// ★★★ СТРАХОВКА ОТ КИКА «NotKnow Packet Count» (наша, оригинала здесь нет).
	//   Опкода 6616 в СТОКОВОМ диспетчере GameServer нет: пакет падает в ветку `default`
	//   @0x0042F020, которая безусловно зовёт `IncUnknownPacketCount@0x005244B0`
	//   (`mov edx,[ecx+0x1E]; add edx,1; mov [eax+0x1E],edx` @0x005244DB — единственная
	//   запись в это поле во всём срезе .text). Периодическая проверка
	//   `IsUnknownPacketOverLimit@0x005244F0` (`cmp dword [ecx+0x1E],5; jle → 0` @0x0052451D)
	//   при значении > 5 возвращает 1, и по адресу 0x00526D73 сервер зовёт
	//   `DisconnectUser(idx, 3)` @0x0052EDB0 со строкой
	//   «CheckDisConnectUser() NotKnow Packet Count» (.rdata @0x00599500).
	//   ⇒ ШЕСТОЙ необслуженный пакет = кик. Сброса поля в срезе .text нет (ссылок ноль).
	//
	//   Обслуженный 6616 наблюдаем НАДЁЖНО: наш P01 в ответ рассылает 6131 через
	//   `SendPacket_AllPlayer_InMap@0x0052E520` — цикл по всем слотам процесса БЕЗ исключения
	//   отправителя (фильтры только userNum>=0, онлайн==1, подавление==0), поэтому свой
	//   же анонс возвращается и нам. Значит «ответ на 6616» = ЛЮБОЙ входящий 6131.
	//
	//   Порог 3 (а не 5): бюджет CUser+0x1E общий на все неизвестные серверу опкоды.
	//   Клиент штатно шлёт ещё и 6560 («Поиск отряда»/«Торговля»), которого в стоке тоже
	//   нет — ему оставляем запас 2. Счётчик обнуляется при входе в мир (6004 result=2),
	//   потому что вход = новое соединение и новый CUser, где серверное поле снова 0.
	static constexpr int32 kMaxUnacked6616 = 3;
	int32 Unacked6616 = 0;                       // отправлено 6616 подряд без единого 6131
	bool  bAnnounce6616Blocked = false;          // порог достигнут — 6616 больше не шлём

	// ★ Кик/обрыв на LOGIN-соединении (эталон: пакет 4101 + путь FD_CLOSE → Show_DisconnectPopup).
	//   Code = байт body[0] пакета 4101 (0xFF = обрыв без 4101, эталонный g_nDisconnectReason=255).
	//   bClosed=false — эталонные «нотисные» коды 0x0D..0x20 / 0x65..0x6B: соединение живо, вход повторяем
	//   по тому же сокету. bClosed=true — сокет ЗАКРЫТ, следующий вход поднимет новый коннект.
	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnAsdaLoginKick, int32 /*code*/, bool /*bClosed*/);
	FOnAsdaLoginKick OnLoginKick;
	uint8 LastKickCode = 0xFF;                   // код последнего 4101 (для попапа при последующем FIN)

	// ★ РИТМ KEEPALIVE 1000 — 1:1 с потоком оригинала `NetHeartbeatThread@0x95C330` (античит, блок Б плана альфы, 14.10.2026;
	//   реверс `Reports/anticheat/wSHIP_logs/clock_re.md` §4.1). [ОРИГИНАЛ] Раз в 50 мс `t = timeGetTime()`; если
	//   |t − g_LastNetTickStamp| > 8000 мс (МОДУЛЬ разности) и `g_GameState != 0 && g_nDisconnectReason == 255` — 1000 на
	//   логин- и игровой слот, метка = сейчас. Стенные часы, а не сумма DeltaTime кадров: ускоренные часы клиента сокращают
	//   интервал пинга ровно как у оригинала, и серверная проверка GS `sub_5271F0` («пинг < 6000 мс» → Log_SpeedHack
	//   Warning, каждый 4-й — HackCheck) срабатывает с тем же порогом ×1.33 (при прежних 10 с порта было ×1.67).
	//   Своего детектора часов у оригинала нет: GameGuard в установке мёртв (нет GameGuard.des/npsc.des), 8905 клиент не шлёт.
	static constexpr double kNetHeartbeatPeriodSec = 8.0;   // эталон: `> 8000` мс (cmp 1F40h)
	// Пора ли слать 1000: |Now − Stamp| > 8 с (строго больше; модуль — устойчиво к скачку часов НАЗАД, как в оригинале).
	static bool NetHeartbeatDue(double NowSec, double StampSec);
	// Гейт посылки: живой сокет, активна логин- или игровая сессия, кода кика нет (у порта код кика — `LastKickCode`, 0xFF = нет).
	static bool NetHeartbeatGateOpen(bool bSocketConnected, bool bLoginSession, bool bGameSession, uint8 KickCode);
	// Шаг планировщика пинга на момент `NowSec` (зовёт Tick с FPlatformTime::Seconds(); самотест — с подставным временем).
	//   true — слать 1000 сейчас (метка уже переставлена на NowSec). Состояние сессии передаётся явно.
	bool StepNetHeartbeat(double NowSec, bool bSocketConnected, bool bLoginSession, bool bGameSession, uint8 KickCode);
	double GetNetHeartbeatStamp() const { return LastNetTickStampSec; }        // метка (для самотеста)
	void SetNetHeartbeatStampForTest(double V) { LastNetTickStampSec = V; }    // подмена метки (только самотест; возвращать!)

	// ★★★ ПРИЧИНА ПОСЛЕДНЕГО РАЗРЫВА ДЛЯ ПОПАПА `COMMONWIN_MSGBOX` 8000 (17.09.2026).
	//   ЗАЧЕМ ОТДЕЛЬНОЕ ПОЛЕ. `LastKickCode` живёт только до конца `HandleConnectionClosed`: там он
	//   читается и тут же сбрасывается в 0xFF, потому что относится к СЛЕДУЮЩЕМУ входу. А эталонный
	//   `g_nDisconnectReason` — ГЛОБАЛ, который WndProc читает УЖЕ ПОСЛЕ закрытия сокета
	//   (`AsdaStory_game.c:23941`, ветка `msg == 1297 && lParam == 2`) и по нему выбирает нотис
	//   попапа. Подписчик `OnGameDisconnected` у нас работает ровно в этот момент, поэтому причину
	//   надо сохранить до броадкаста и НЕ чистить — это зеркало эталонного глобала, а не второй
	//   `LastKickCode`. 255 = «обрыв без 4101» (то же значение, что у оригинала).
	uint8 LastDisconnectReason = 0xFF;
	// Причина последнего разрыва (для `UAsdaDisconnectPopupSubsystem::ReasonToNoticeId`).
	int32 GetLastDisconnectReason() const { return (int32)LastDisconnectReason; }
	// ★ 16.10.2026 (EAC, тексты владельца): текст EAC для окна кика логина (заголовок + причина, 991xxx). Не пуст ТОЛЬКО на
	//   время HandleConnectionClosed, вызванного событием EAC: AAsdaLoginGameMode::OnLoginKick показывает его вместо нотиса
	//   оригинала по коду. Пусто — строк 991xxx в таблице нет (или разрыв не от EAC): нотис оригинала, как раньше.
	const FString& GetEacKickText() const { return EacKickText; }
	// ★ 16.10.2026: текст игроку по ключам AsdaEacLoc::ForFatal на языке сессии — строки заголовка/причины (0 — нет) и,
	//   при bDetails, деталь EAC последней строкой (угловые скобки гасятся). Пусто — показывать нечего (нотис оригинала).
	//   Открыт для самотеста asda.SelfTest.EacTexts.
	static FString BuildEacPlayerText(const UGameInstance* GI, int32 TitleId, int32 BodyId, bool bDetails, const FString& Details);

	// ★★★ Эталон `g_bActionPending`: «есть НЕЗАКРЫТЫЙ стопом ход». Ставится ТОЛЬКО отправкой 4006
	//   (`Combat_SendNormalAttack@0x9F8CA0`, combat_pvp.c:1511) и сбрасывается ТОЛЬКО отправкой 4008
	//   (`Net_SendActionPacket@0x9F9C00`, network.c:5016-5018). Приход 4007/4009 его НЕ трогает —
	//   проверено по всем 6 упоминаниям в декомпиле. Не «ждём ответа сервера», а «сервер считает нас
	//   идущими, пока мы не пришлём CharStop».
	bool bActionPending = false;
	float LastServerSpeed = 0.35f;               // последняя скорость из 4007/6071 (юнит/мс); база 3.5 тайла/с

	// ★★★ 27.09.2026 — ЭТАЛОННЫЙ ФЛАГ «ЗАПРОС ХОДА В ПУТИ» (бит 2 `g_nGameFlags`) и его часы.
	//   Разбор — `Reports/owner_0927/move_4007.md` §3. Кратко:
	//   · Слово флагов = `CGame+0x4914` (VA 0x2906264; `this[4677]` в декомпиле, IDA зовёт объект `g_InventoryBag` —
	//     миснейм, это объект игры). Бит 2 ВЗВОДИТ единственный писатель — отправитель 4006
	//     `Combat_SendNormalAttack@0x9F8CA0` (`sub_9F8FA0(&game, 2)`, скан бинаря: 1 вызов на весь образ).
	//   · СНИМАЮТ: свой 4007 любого result (`Recv_EntityMove`, после гейта «жив и не занят»); отказы боя
	//     4027 коды 6/7/8/9/20, 4029 коды 4/6/7, 4202 коды 6/7/8/9, 4204 коды 4/6/7; сторож 10 000 мс
	//     (`effectUpdateAttachedTargetPos@0x9954F0` — каждый кадр: |сейчас − метка| > 10000 → снять).
	//   · ЧИТАЮТ (13 мест в бинаре, второй метод — скан `and eax, 2` по адресу слова): мировой клик
	//     (`world_OnLeftClickPick`, `Picking_TestRayPickWorldTarget`, `CGame_HandleGroundClickAttack` — отказ),
	//     применение навыков (`combatHandleSkillUse`, `skillCastActivate`, `skillUseOnTargetDispatch` — молча),
	//     авто-действие (`Player_UpdateAutoActionState`) и тик своего персонажа (`Player_ExecuteSkillOnTarget@0x9F16D0`:
	//     пока бит взведён — НЕ шлёт стоп 4008 и не перезапрашивает подход, а доигрывает текущий шаг).
	//   ⇒ СМЫСЛ: «ход запрошен у сервера (4006), ответа 4007 ещё нет» — ввод мира и навыки ждут ответа.
	//   Метка времени — `g_flNetTickInterval` = `CGame+0x1434` (VA 0x2902D84, float): пишется отправкой 4006,
	//   грантом 4007 (подгонка скорости) и сбрасывается на «сейчас» под состоянием «Паника» (id 4).
	//   Состояние флага ведём ВСЕГДА; читатели работают под ключом `asda.Move.EtalonSpeedSync` (контроллер).
	bool   bMoveRequestInFlight = false;         // эталонный бит 2 `g_nGameFlags`
	double MoveRequestStampMs   = 0.0;           // эталонный `g_flNetTickInterval` (мс; у нас double — см. отчёт)
	// Часы эталона: `CObject_GetField2(0x3528F98)` = `timeGetTime()` кадра (`timerUpdateHighRes@0x965D60`), мс.
	static double MoveClockMs() { return FPlatformTime::Seconds() * 1000.0; }
	// Взведён ли бит 2. Сторож 10 с эталона (кадровый) исполнен лениво — при чтении; снятие пишется в лог.
	bool IsMoveRequestInFlight();
	// Снять бит 2 (эталон `gameFlagsClear(&game, 2)@0xF3CC50`). Why — для журнала.
	void ClearMoveRequestInFlight(const TCHAR* Why);

	// 4007 EntityMove: result([0]): 1=иди, 5=анти-чит снап, 0=сброс, 2/3/6..9=нотисы; speed@+21 (юнит/мс).
	// ★ 27.09.2026: +2 параметра — подтип хода @+3 (i16; эталон исполняет грант только при 1..2) и
	//   renderId цели хода @+25 (i16; эталон пишет его в старшее слово `targetRenderId` сущности). Для 4009
	//   оба передаются, но эталон `Recv_EntityStop@0xF3AB50` их не читает — слушатель обязан их игнорировать.
	DECLARE_MULTICAST_DELEGATE_EightParams(FOnAsdaEntityMove, int32 /*userIdx*/, int32 /*result*/,
		FVector2D /*cur game XZ*/, FVector2D /*dst game XZ*/, float /*speed*/, bool /*bStop*/,
		int32 /*подтип хода @+3*/, int32 /*renderId цели хода @+25*/);
	FOnAsdaEntityMove OnEntityMove;   // 4007 (bStop=false) и 4009 (bStop=true, снап в cur)

	// ★★★ 4062 `netRecvEntityPosSync@0xF3C150` — ПОЗИЦИОННЫЙ СИНК сущности. Отдельный делегат, а не
	//   OnEntityMove: у 4062 НЕТ поля result и НЕТ текущей точки — в пакете только тип, цель и скорость.
	//   Текущую точку слушатель берёт у актора; эталон поступает так же (`CObject_GetSubField47649`).
	//   Параметры: тип-байт КАК ЕСТЬ (0 = «стоп», как case 0 у 4007) и СКОРОСТЬ в единицах/мс — то же поле,
	//   что `speed@21` у 4007. ★ 16.09.2026: раньше здесь стояло «длительность пути» — это был миснейм IDA
	//   (`moveDuration` в `charMoveStepToCell` на самом деле хранит РАССТОЯНИЕ), и пересчёт в слушателе был
	//   лишним; поля +13/+17/+21 читаются как float, а не целыми.
	DECLARE_MULTICAST_DELEGATE_FourParams(FOnAsdaEntityPosSync, int32 /*userIdx*/, uint8 /*тип-байт*/,
		FVector2D /*dst game XZ*/, float /*скорость, единиц/мс*/);
	FOnAsdaEntityPosSync OnEntityPosSync;

	// ★★★ 6071 `Recv_CharMoveToCell@0xF3CC90` — НОВАЯ СКОРОСТЬ движения сущности (посадка на транспорт и
	//   слезание с него, 5450/5455; смена состояний скорости). В теле НЕТ ни цели, ни текущей точки —
	//   только чья скорость и какая: эталон продолжает УЖЕ ИДУЩИЙ шаг в прежнюю цель с новым темпом.
	//   Скорость — в единицах/мс, то же поле, что `speed@21` у 4007.
	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnAsdaEntitySpeed, int32 /*userIdx*/, float /*скорость, ед/мс*/);
	FOnAsdaEntitySpeed OnEntitySpeed;

	UPROPERTY(BlueprintReadOnly, Category="AsdaNet")
	EAsdaNetState State = EAsdaNetState::Idle;

	// Подписка UI/GameMode на ответ логина 1004.
	FOnAsdaLoginResult OnLoginResult;

	// UserIndex из ответа 1004 (body+2, u32) — нужен для server-select (1012) и enter-world.
	uint32 UserIndex = 0;

	// ★ char-list userNo (эталон Send_CharacterPacket: pkt[0]=get_oBBE4B) — НЕ сырой UserIndex! Сверка с захватом:
	//   char-list payload байт[2] = байт[9] логин-ответа 1004 (серверный session-номер). Оригинал 1004 body[9]=0x14
	//   → userNo=0x001425AC; UserIndex=0x000125AC. Составляем: (UserIndex&0xFFFF)|(body9<<16)|(body10<<24).
	uint32 CharListUserNo = 0;

	// Список персонажей (из блока 1006). До 3 слотов; пустые = IsEmpty().
	UPROPERTY(BlueprintReadOnly, Category="AsdaNet")
	TArray<FAsdaCharRecord> CharList;

	// Событие: список персонажей получен (→ вход в char-select: перелёт+открытие+спавн).
	DECLARE_MULTICAST_DELEGATE(FOnAsdaCharList);
	FOnAsdaCharList OnCharListReady;

	// ★ Событие: транзакция 1005 ПРОВАЛИЛАСЬ — сервер прислал 1025 с кодом 0 (ошибка SQL на его
	//   стороне) и списка персонажей (1006) НЕ будет. Без реакции экран логина залипает навсегда
	//   и молча: страховка «переход взводит 1006» здесь не срабатывает, потому что 1006 не придёт.
	//   Эталон в этом месте сбрасывает состояние и переспрашивает опкодом 1028; мы 1028 не шлём
	//   (раскладка не доказана) — слой логина должен повторить штатный запрос 1005.
	FOnAsdaCharList OnCharListFailed;

	// ★ ОТЛАДОЧНАЯ ПОДАЧА ПАКЕТА (только для консольных команд и живых прогонов): собрать
	//   `ParsedPacket` из опкода и тела и провести его ЧЕРЕЗ ШТАТНЫЙ `HandlePacket`. Нужна,
	//   чтобы проверять приёмник вместе с его ветвлением, а не в обход (иначе консольная команда
	//   повторяла бы логику разбора и стала бы вторым источником истины). Ничего в сокет не пишет.
	//   ★ 04.11.2026 (status_effect_expiry): `HdrA` — поле заголовка кадра (5306 читает userIdx из него, не из тела).
	void DebugFeedPacket(int32 Opcode, const TArray<uint8>& Body, uint16 HdrA = 0);

	// ══════════ АУКЦИОН (`CItemMediation`, окно EXCHANGE_WIN 13000) ══════════════════════════
	//   Полный разбор — `Reports/npc_exchange_win_13000.md`. ★ Имена 9902/9904/9913/9914 в
	//   `NetOpcodes.h` (`OP_NPC_SHOP_DISPATCH_RESULT*`) — МИСНЕЙМЫ: к магазину NPC отношения нет,
	//   это результаты аукциона (`npcShop_DispatchResult@0xD43270`, `npc_mob_pet.c:511`).
	//   Поэтому здесь опкоды заданы числами с комментарием, а не через тот enum.
	//
	//   Шапка payload — 15 байт (`sub_D3C720@0xD3C720` + ручные записи):
	//     +0 = −1, +1 = −1, +2 = 0, +3 i16 = −1, +5 i32 = userIdx, +9 = 0,
	//     +10 i32 = `g_nItemMediationSessionId`, +14 = 0.
	//
	//   ★★★ ЧТО ЭТО ЗА ПОЛЕ +10 — СНЯТО ПО СЕРВЕРУ 2026-08-28 (жалоба владельца «неверный нотис»).
	//   Прежний комментарий («сессия, в бою всегда 0, шлём ноль») был ДОГАДКОЙ по отсутствию
	//   xref-записи и оказался неверным. Сервер читает ИМЕННО ЭТИ 4 байта и ищет по ним NPC:
	//     `Recv_TransAction_InqueryItemReq@0x519FB0` (`GameServer.c:107505-107512`):
	//        memcpy(Src, (char *)a3 + 33, 0xF);            // a3+33 == payload+0 (8 б секрет + 16 MD5)
	//        v11 = sub_4375E0(*(_DWORD *)(*(_DWORD *)(this + 4) + 20), *(int *)&Src[5]);
	//        if (!v11) { v15 = 2; }                        // ⇒ КОД 2 ⇒ клиентский нотис 12672
	//     `sub_4375E0@0x4375E0` (`GameServer.c:23192`): линейный поиск в массиве NPC карты
	//        (записи по 108 б, счётчик `+16`) по полю `rec+2`; загрузка массива —
	//        `GameServer.c:22654`: `rec+2 = src[221*i + 4]`, где 221 (0xDD) — шаг записи файла
	//        `.npc`, а поле +4 — ★ ТИП NPC (`GameServer.c:22663`: `if (rec2 < 0x12C) …` — индекс в
	//        таблицу типов на 300 записей). То же поле разбирает наш импортёр
	//        (`Tools/asda_build_npc_manifest.parse_npc_file`: `'<HxxHxxhhH'`, `tid` на +4).
	//   ⇒ +10 = **ID (тип) NPC-АУКЦИОНЩИКА**, у которого открыт аукцион. Тем же полем пользуются
	//   9901 (`GameServer.c:106851`, отказ 9), 9905 (`:107813`) и 9909 (`:108881`).
	//   Ноль ⇒ на карте нет NPC с типом 0 ⇒ отказ 2 ⇒ «Такого персонажа нет на этой карте».
	void BuildExchangeHeader15(uint8* Out) const;

	/** ID (тип) NPC-аукционщика для шапки аукционных пакетов (+10). Ставит
	 *  `UAsdaExchangeSubsystem::OpenMode` из текущего собеседника NPC-диалога. */
	void SetExchangeNpcId(int32 NpcId);
	int32 GetExchangeNpcId() const { return ExchangeNpcId; }
	/** Тип NPC-аукционщика (см. `BuildExchangeHeader15`). −1 = ещё не знаем (шлём 0, как раньше). */
	int32 ExchangeNpcId = -1;

	// 9915 — открыть сессию аукциона. Payload пустой (`mediationSendOpen@0xD44370`).
	bool SendExchangeOpen9915();
	// 9901 — выставить лоты. До 5 записей по 98 б (`mediationSendRegister@0xD4F0F0`).
	bool SendExchangeRegister9901(const TArray<TArray<uint8>>& Rows98);
	// 9903 — поиск/страница. Ровно 8 байт критериев; `Criteria[7]` = номер страницы (0-базовый).
	bool SendExchangeSearch9903(const uint8 Criteria8[8]);
	// 9905 — купить. До 8 записей по 45 б (`mediationSendBuy@0xD56990`).
	bool SendExchangeBuy9905(const TArray<TArray<uint8>>& Rows45);
	// 9907 — запросить «мои лоты». Payload — 1 байт: индекс категории + 1.
	bool SendExchangeMyLots9907(int32 CategoryIndex);
	// 9909 — снять лоты с продажи. До 10 записей по 45 б. ★ Имя эталона
	//   `itemMediationSendRegisterRequest` — МИСНЕЙМ, ничего не регистрирует.
	bool SendExchangeDelete9909(const TArray<TArray<uint8>>& Rows45);

	// Результат любого аукционного S→C пакета. `Opcode` — 9902/9904/9906/9908/9910/9913/9914,
	//   `Kind` — значение `a3` эталонного диспетчера (1/4/4/3/3/6/7), `Sub` — его `a4`.
	DECLARE_MULTICAST_DELEGATE_FourParams(FOnAsdaExchangePacket, int32 /*Opcode*/, int32 /*Kind*/,
		int32 /*Sub*/, const TArray<uint8>& /*Body*/);
	FOnAsdaExchangePacket OnExchangePacket;

	// ══════════════════════════════════════════════════════════════════════════════════════════
	// ★★★ СООБЩЕСТВО (вкладки 14500 «Приятель», 14033 «Клан», 14600 «Кто?») + СОУЛМЕЙТ
	//     (навыки Дружбы, Зов друга, рулетка). Источники — ПОБАЙТОВЫЕ отчёты:
	//       `Reports/community_tabs_frnd_clan_who.md`    (§1.5, §1.6, §2.5, §2.6, §3.5, §3.6)
	//       `Reports/soulmate_skills_recall_roulette.md` (§0, A.3-A.5, B.2, C.2-C.5)
	//     ★ КОНВЕРТ У ВСЕХ C→S ЭТИХ ТРЁХ СЕМЕЙСТВ — «B» (`SendSecured`): [0..7] обфусцированный
	//       токен + [8..23] MD5(payload, Len) + [24..] payload, `CNetwork_Send(..., a6 = 1)` ⇒
	//       паддинг ВКЛЮЧЁН (bPad = true по умолчанию). Сверено ПОСЕМЕЙНО:
	//       приятели — community_tabs_frnd_clan_who.md:262-264 («конверт у ВСЕХ отправок этой
	//       вкладки — (B) … и a6 = 1»); клан — там же :802 («конверт всех C→S — (B) … a6 = 1»);
	//       «Кто?» 5480 — там же :1099 («24 = 8 + 16 + 0; конверт (B), a6 = 1»);
	//       соулмейт 5231/5237/5427/6575/6579 — soulmate_skills_recall_roulette.md:12-31.
	//     ★ ТЕЛА S→C во ВСЕХ этих семьях начинаются с `pktBuf[9]`, то есть у нас это ровно
	//       `Pkt.Body[0]` (soulmate_skills_recall_roulette.md:33).
	// ══════════════════════════════════════════════════════════════════════════════════════════

	// ── ПРИЯТЕЛИ (вкладка COMMUNITY_FRND_WIN 14500) ──────────────────────────────────────────
	// C→S 4156 «дай список приятелей». Эталон `netSendReqOp4156@0xCE8A30` (game.c:329811-329928).
	//   Полный размер 24 = 8 токен + 16 MD5 + 0 payload; MD5 считается ОТ ПУСТОГО тела
	//   (`Net_Md5Digest16(salt, nullptr, 0, md5)`). Отчёт: community_tabs_frnd_clan_who.md:377-389.
	//   ★ Клиентский антиспам 5000 мс (`isWithinTimeout5000@0xCE9B00`: `timeGetTime() − this[66] <= 0x1388`)
	//     реализован ВНУТРИ метода: раньше 5 секунд после прошлой удачной отправки пакет НЕ уходит,
	//     метод возвращает false. Точки вызова эталона — переключение на вкладку 1 (game.c:241742)
	//     и кнопка 14511 «Обновить» (game.c:330322).
	//   ★ Побочно: приём 4158 ПЕРЕЗАПУСКАЕТ этот таймер (дефект оригинала D-F9) — воспроизведено 1:1.
	bool SendFriendListReq4156();

	// C→S 4153 «ответ на приглашение в приятели». Эталон `netSendPacket1039@0xCE7F30`
	//   (game.c:329571-329606), полный размер 47 = 24 + 23. Отчёт: community_tabs_frnd_clan_who.md:332-346.
	//   payload(23): @0 char[20] имя пригласившего · @20 i16 userIndex пригласившего ·
	//                @22 u8 ответ (1 = принять, 2 = отклонить; 3 сервер предусматривает, клиент не шлёт).
	//   ★ МИСНЕЙМ ПАРАМЕТРА КОНТРАКТА: `CharId` здесь — НЕ charId (i32), а i16 userIndex
	//     пригласившего (эталон берёт его из записи очереди 4152, поле @20). Метод сперва ищет
	//     userNum в кэше входящих приглашений по имени (`PendingFriendInvites`, наполняется приёмом
	//     4152) и лишь если не нашёл — усекает `CharId` до i16. Сервер `Recv_UserFriendOK@0x537BF0`
	//     (GameServer.c:118976) имя из тела НЕ читает, работает только по i16.
	bool SendFriendAnswer4153(bool bAccept, int32 CharId, const FString& Name);

	// C→S 4154 «удалить приятеля». Эталон `netSendTimeSyncMask49@0xCE8180` (★ МИСНЕЙМ — никакой
	//   синхронизации времени; game.c:329617-329755), полный размер 49 = 24 + 25.
	//   Отчёт: community_tabs_frnd_clan_who.md:348-359.
	//   payload(25): @0 i32 CharUniqueIndex приятеля · @4 u8 код класса · @5 char[20] имя.
	//   ★ Код класса и имя контракт не передаёт — берём их из кэша записей списка (`FriendEntries`,
	//     наполняется приёмом 4157/4160 и 4151 с кодом 1: поля записи @4 и @11). Другого источника
	//     31-байтовой записи в клиенте не существует.
	//   ★★★ 31.08.2026: доказано, что сервер ЧИТАЕТ все три поля и передаёт их в БД, а ИМЕНЕМ (@5)
	//     находит второго участника и шлёт ему 4155 (`FriendMemberDeleteReq@0x406630`,
	//     GameServer.c:2660-2668 → `sub_4067C0@0x4067C0`, GameServer.c:2617). Поэтому если записи в
	//     кэше нет, метод НИЧЕГО НЕ ШЛЁТ и возвращает false с ошибкой в лог: пакет с пустым именем
	//     сервер примет, но список второго игрока рассинхронизируется. Сначала запросить 4156.
	bool SendFriendDelete4154(int32 CharId);

	// S→C 4152 «тебе предлагают стать приятелями», 22 б (сервер `sub_40BD90@0x40BD90`).
	//   Тело: @0 char[20] имя пригласившего · @20 i16 userIndex пригласившего.
	//   Отчёт: community_tabs_frnd_clan_who.md:304-330. UI обязан показать МОДАЛЬНОЕ окно YES/NO
	//   (тип 20081) с текстом loc 20081 и ответить 4153.
	//   ★ ДЕФЕКТ ОРИГИНАЛА D-F1 (НЕ воспроизводим): при выключенном звуке эталон вместо показа
	//     приглашения молча слал ОТКАЗ (`else → netSendPacket1039(…, 2)`, game.c:430761-430769).
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnAsdaFriendInvite, const TArray<uint8>&);
	FOnAsdaFriendInvite OnFriendInvite;

	// S→C 4155 «результат удаления приятеля», 6 б (`uiTradeListRemoveEntryByItem@0xCE8840`).
	//   Тело: @0 u8 код (1 = удалено → чат loc 20001; иначе ошибка → нотис loc 12814) ·
	//         @1 i32 CharUniqueIndex удалённого · @5 u8 код класса.
	//   Отчёт: community_tabs_frnd_clan_who.md:361-375.
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnAsdaFriendDeleteResult, const TArray<uint8>&);
	FOnAsdaFriendDeleteResult OnFriendDeleteResult;

	// S→C 4157 «список приятелей» — ТЕЛО ЦЕЛИКОМ, записи ПО 31 БАЙТУ подряд
	//   (`pktParseListRecords31@0xCE8FE0`, game.c:329939-329971). Отчёт: §1.5 (:239-257) и :391-410.
	//   Запись(31): @0 i32 charId · @4 u8 код класса (ключ дедупликации) · @5 u8 канал (0-based) ·
	//               @6 u8 MapGroup (id карты) · @7 u8 уровень · @8 u8 класс 1-й проф. ·
	//               @9 u8 класс 2-й проф. (приоритетный, если ≠ 0) · @10 u8 онлайн (1 = в сети) ·
	//               @11 char[20] имя.
	//   ★ ДЕФЕКТ ОРИГИНАЛА D-F3: эталон НЕ чистит список перед добавлением (дубли при повторном 4156).
	//     В СЛУЖЕБНОМ кэше `FriendEntries` дефект не воспроизводится (кэш пересобирается) — поведение
	//     самого списка решает подписчик, ему тело отдаётся как есть.
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnAsdaFriendList, const TArray<uint8>&);
	FOnAsdaFriendList OnFriendList;

	// S→C 4158 «перерисуй список», тело ПУСТОЕ (`recvTradeListPageRefresh@0xCE9140`, game.c:329990).
	//   Отчёт: community_tabs_frnd_clan_who.md:412-416.
	DECLARE_MULTICAST_DELEGATE(FOnAsdaFriendListRefresh);
	FOnAsdaFriendListRefresh OnFriendListRefresh;

	// S→C 4160 «обновление ОДНОЙ записи списка приятелей», 33 б (сервер `sub_40BDC0@0x40BDC0`).
	//   Тело: @0 i16 · @2 запись приятеля 31 б (раскладка выше).
	//   ★★★ 31.08.2026 поле @0 РАЗОБРАНО: это заголовочные 2 байта серверной обёртки
	//     `{ i16; FriendRecord31 }` (ctor `sub_40BDC0@0x40BDC0`, GameServer.c:5174-5179), и они
	//     ВСЕГДА НОЛЬ — единственный производитель 4160 во всём GameServer явно пишет
	//     `*(_WORD *)v16 = 0` (GameServer.c:2805) и обе рассылки (:2827 и :2832) шлют этот же блок.
	//     Потребителя нет ни на одной стороне. Индекса/страницы/кода поле НЕ несёт: обновляемую
	//     запись адресует только charId внутри неё (наш upsert — по паре charId+класс). HIGH.
	//   Отчёт: community_tabs_frnd_clan_who.md:425-433.
	//   ★ ДЕФЕКТ ОРИГИНАЛА D-F4: в эталонном диспетчере обработчика `case 0x1040` НЕТ — сервер шлёт,
	//     клиент молча роняет; онлайн-статус приятеля обновлялся только полной перезагрузкой списка.
	//     ИСПРАВЛЕНО (помечено): пакет принимается и разбирается.
	//   ★ МИСНЕЙМ рядом: `OP_NET_RECV_CHAR_UPDATE_LIST_ENTRY = 4159` к приятелям отношения НЕ имеет —
	//     4159 это секторный broadcast применения навыка (GameServer.c:46003 / :57174).
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnAsdaFriendEntryUpdate, const TArray<uint8>&);
	FOnAsdaFriendEntryUpdate OnFriendEntryUpdate;

	// ── ВКЛАДКА КЛАНА (COMMUNITY_CLAN_WIN 14033) ─────────────────────────────────────────────
	// ★★★ ПРОТИВОРЕЧИЕ КОНТРАКТА И ОТЧЁТА (имена контракта сохранены, поведение — по отчёту):
	//   по community_tabs_frnd_clan_who.md:815-819 опкод 4122 = `Send_ClanBaseInfo@0x401240`, то есть
	//   БАЗОВАЯ ИНФА клана (ответ 4123, 191 б), а 4124 = `Send_ClanUserList_DB@0x401450`, то есть
	//   СПИСОК ЧЛЕНОВ (ответ 4125 записями по 131 б). В именах контракта они переставлены местами.
	//   ★★★ ИМЕНА ИСПРАВЛЕНЫ 2026-08-31 ПО РОЛЯМ ОПКОДОВ (расхождение нашли ДВА независимых агента):
	//   4122 = `Send_ClanBaseInfo@0x401240` — базовая ИНФА клана (ответ 4123);
	//   4124 = `Send_ClanUserList_DB@0x401450` — СПИСОК ЧЛЕНОВ (ответ 4125, конец 4126).
	//   Тело у обоих одно: i16 clanIndex. Параметра «страница» в проводе НЕТ — сервер сам шлёт порции.
	//   запрашивает базовую инфу клана. Опкод в имени метода = опкод в проводе, это первично.
	// C→S 4122, полный размер 26 = 24 + 2. payload(2): @0 i16 clanIndex.
	//   Эталон `netSendAntiCheatTokenOp4122@0xBCACA0` (★ МИСНЕЙМ — не анти-чит), сервер
	//   `Send_ClanBaseInfo@0x401240`, гейт `0 ≤ clanIndex ≤ 10000`, членство НЕ проверяется.
	//   ★ Параметр `Page` НЕ ДОКАЗАН как поле провода: в теле 4122 всего 2 байта, и это клан-индекс;
	//     постраничного запроса у семейства нет вовсе — сервер сам шлёт список пакетами 4125 и
	//     закрывает его пустым 4126. `Page` трактуется как КЛАН-ИНДЕКС: > 0 — берётся как есть,
	//     ≤ 0 — подставляется известный (`KnownClanIndex`), которого нет → 0 («нет клана», как в
	//     `SendEntryFinalize`, где эталон на LoadProgress == 11 шлёт нули).
	bool SendClanBaseInfoReq4122(int32 ClanIndex);
	// C→S 4124 «СПИСОК ЧЛЕНОВ клана», полный размер 26 = 24 + 2. payload(2): @0 i16 clanIndex.
	//   Эталон `netSendAntiCheatToken@0xBCAEA0` (★ МИСНЕЙМ), сервер `Send_ClanUserList_DB@0x401450`,
	//   DB-опкод 2175, членство НЕ проверяется. Отчёт: community_tabs_frnd_clan_who.md:819.
	//   Клан-индекс берётся из `KnownClanIndex` (последний использованный/принятый), иначе 0.
	bool SendClanMemberListReq4124(int32 ClanIndex);
	// S→C 4125 «порция списка членов клана» — ТЕЛО ЦЕЛИКОМ, записи ПО 131 БАЙТУ
	//   (`recvParseServerChannelList@0xBCB060`, ★ МИСНЕЙМ: к списку каналов отношения нет).
	//   Запись(131) — отчёт §2.5 (:775-799): @0 i32 userIndex · @4 u8 charIndex (слот 0..2) ·
	//     @5 char[20] имя · @25 u8 пол · @26 u8 уровень · @27 u8 класс 1-й проф. ·
	//     @28 u8 класс 2-й проф. (приоритетный) · @29 i32 активные очки (ОД) · @33 u8 звание 1..5
	//     (1 = мастер … 5 = новичок) · @34 i32 не идентифицировано · @38 u8[9] клановые флаги ·
	//     @47 char[64] memo «Заметки» (пишет опкод 6518) · @111 u8 номер канала · @112 u8 id карты ·
	//     @113 u8 «в игре» 0/1 · @114 u8 доп. флаг · @115 char[16] дата последнего выхода.
	//   Конец списка — отдельный ПУСТОЙ 4126.
	//   ★ 4133 (обновление ОДНОГО члена, 134 б) в диспетчере УЖЕ ЗАНЯТ (выход/роспуск клана) — не трогаем.
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnAsdaClanMemberList, const TArray<uint8>&);
	FOnAsdaClanMemberList OnClanMemberList;

	// ══════ ★★★ ПЯТЬ ОПКОДОВ КЛАНА, ЗАКРЫТЫЕ РЕВЕРСОМ (`Reports/clan_soulmate_gaps.md` §2) ═══
	//   Конверт у всех C→S — «B» (секрет 8 + MD5 16 + payload), паддинг СТАВИТСЯ (a6 = 1), сервер
	//   читает тело с +33 (`Send_ClanBaseInfo@0x401240`: `memcpy(&token, a3+9, 8)`, `a3+33` — тело).
	//   То есть отправка — тем же `SendSecured`, что у уже готовых 4122/4124.

	// S→C 4123 «ИНФО-ПАНЕЛЬ КЛАНА» — ответ на 4122, payload РОВНО 191 б
	//   (`recvTradeWindowState@0xBCAE60`: `qmemcpy(wnd+0x614, body, 0xBF)`; сервер `sub_4C28F0(…,4123,191,…)`).
	//   ⚠ ОБА ИМЕНИ ЭТАЛОНА ВРУТ: ни к торговле, ни к «состоянию окна» пакет отношения не имеет.
	//   Раскладка блока (база = смещение 0 в теле; дизасм `uiTradeWindowRefresh@0xBC3BF0`):
	//     @0   i16       индекс клана
	//     @2   char[18]  имя клана                → контрол 14003 `CMTY_CLAN_NAME_V`
	//     @20  char[27]  имя главы клана          → контрол 14007 `CMTY_CLAN_CHIEF_V`
	//     @40  i32       userIndex главы          @44 u8 charIndex главы
	//     @47  u8        уровень клана, печатается как `(lvl+1)` в формате "%02dLv" → 14005
	//     @49  i32       значение → контрол 14011 `CmtyClan_NeededPoints` ("%d")
	//     @53  i32       значение → контрол 14009 `CmtyClan_TotalPoints`  ("%d")
	//     @57  u16       МАСКА ПРАВ звания 2      @59 u16 звания 3   @61 u16 звания 4   @63 u16 звания 5
	//     @66  u8        флаг «есть эмблема» (== 1 → читать блок ниже)
	//     @67  40 б      блок эмблемы/флага клана
	//     @172 char[9]   название звания строкой (то же поле, что 4105 кладёт в `char+0x980`)
	//   ★ ПРАВА: `ClanRankPermission@0xBC44A0` — `mask = u16 @ (блок + 53 + звание*2)`,
	//     `bit = (право − 1) & 15`, результат `mask & (1 << bit)`. Смысл битов (по местам проверки
	//     на сервере): 1 — приглашать · 2 — писать/удалять объявления · 8 — менять звания ·
	//     0x10 — исключать. Биты 4 и 0x20 — мест проверки в срезе сервера НЕТ (не доказаны).
	//   ★ ДОПОЛНЕНИЕ 27.09.2026 (сверено с сервером `ClanBaseInfo_MaxClanUserCount_Req@0x404710`,
	//     локалы `v22/v23/v24/v25` = блок+40/+44/+45/+47, дизасм живого образа 0x4048F0…0x40498E):
	//     @45  u8        ЛИМИТ ЧЛЕНОВ клана (текущее число мест; окно 14450 пишет его в 14453)
	//     @47  u8        «уровень» клана = ЧИСЛО УЖЕ СДЕЛАННЫХ РАСШИРЕНИЙ (0…5; сервер при 5 → отказ 5)
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnAsdaClanBaseInfo, const TArray<uint8>& /*191 б*/);
	FOnAsdaClanBaseInfo OnClanBaseInfo;

	// ★ S→C 4131 / 4132 — ОБНОВЛЕНИЕ ИНФО-БЛОКА КЛАНА «СВЕРХУ» (27.09.2026).
	//   4131 `recvTradeWindowStateAlt@0xBCB4E0`: тело = блок 191 б, `memcpy(wnd+0x614, body, 0xBF)`
	//        + перерисовка. Отправителя 4131 в срезе GameServer нет (ссылок в срезе нет) — приём
	//        переносим 1:1, раз он есть у клиента.
	//   4132 `uiTradeWindowResetState@0xBCB530`: тело 192 б = @0 u8 код + @1 блок 191 б.
	//        Код 1 — `memcpy(wnd+0x614, body+1, 0xBF)`; код 2 — клан удалён: блок сбрасывается
	//        в «клана нет» (clanId −1, остальное 0). Оба кода кончаются перерисовкой панели.
	//        Сервер рассылает 4132 ВСЕМ членам клана на каждой карте (`clanBaseInfoUpdate@0x404D80`,
	//        GameServer.c:1996-2014), в частности ПОСЛЕ РАСШИРЕНИЯ (`clanBaseInfoMaxClanUserCountDBRes`
	//        → `sub_4051C0(…, 1, блок)` → 4132) — без этого приёма лимит мест и уровень клана
	//        в порте не обновлялись до следующего 4123.
	//   Делегат отдаёт опкод и тело ЦЕЛИКОМ; колбэк только копит (закон `no-reentrant-net-callbacks`).
	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnAsdaClanBaseInfoPush, int32 /*4131|4132*/, const TArray<uint8>& /*тело*/);
	FOnAsdaClanBaseInfoPush OnClanBaseInfoPush;

	// ★ C→S 4136 `OP_CLAN_BASE_INFO_MAX_CLAN_USER_COUNT_REQ` (31 б = 8 секрет + 16 MD5 + 7 тело) —
	//   РАСШИРЕНИЕ КЛАНА, окно `CLAN_INCREASE_BOX` 14450, кнопка 14461. Эталон
	//   `netSendInventoryItemNamePacket@0xBC50D0` (★ МИСНЕЙМ), раскладка снята ДИЗАССЕМБЛЕРОМ
	//   (0xBC512A…0xBC5158; прежняя запись реестра «u16 @0 · u8 @2 · i32 @3» была НЕВЕРНОЙ):
	//     @0 i32 `char+0x41C` (userIdx) · @4 i16 clanId (`char+0x968`) · @6 u8 = 5 (литерал клиента).
	//   Сервер `ClanBaseInfo_MaxClanUserCount_Req@0x404710` сверяет MD5 по 7 байтам и читает ТОЛЬКО
	//   clanId @4; байт @6 («+5») сервер НЕ читает — прибавку он берёт свою (+10). Шлём 1:1.
	bool SendClanIncrease4136(int32 ClanId);

	// ★ S→C 4137 `OP_UI_INVENTORY_REPAIR_RESULT_HANDLER` (тело 9 б) — РЕЗУЛЬТАТ РАСШИРЕНИЯ,
	//   эталон `uiInventoryRepairResultHandler@0xBC5310` (★ МИСНЕЙМ). Раскладка — сервер
	//   `clanBaseInfoMaxClanUserCountDBRes@0x404B60`: @0 u8 код · @1 u16 clanId · @3 u8 НОВЫЙ ЛИМИТ
	//   ЧЛЕНОВ · @4 i32 НОВОЕ ЗОЛОТО · @8 u8 новое число расширений (клиент его не читает).
	//   Коды: 0 (12450) · 1 успех (12451) · 2 (12452) · 3 (12453) · 4 (12454) · 5 (12455).
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnAsdaClanIncreaseResult, const TArray<uint8>& /*9 б*/);
	FOnAsdaClanIncreaseResult OnClanIncreaseResult;

	// ★ 27.09.2026 (`Reports/owner_0927/friend_gate_12435.md`). S→C 4110 `OP_ITEM_SET_SPLIT_MOVE_SOURCE` (тело 23 б) —
	//   [ПРОТОКОЛ] ВХОДЯЩЕЕ ПРИГЛАШЕНИЕ В КЛАН. Эталон `case 0x100E` (network.c:2149-2152): `Recv_PartyMember(g_pQuickSlotBar,
	//   6, …)` (значок уведомления 6) + `itemSetSplitMoveSource@0xBC9E40` (★ МИСНЕЙМ, item_inventory.c:7334-7341) — запомнить
	//   поля. Раскладка — сервер `ClanMemberInvite_RequestUser` (GameServer.c:688, `sub_4C28F0(…, 4110, 23, …)` :804):
	//     @0 i16 clanId · @2 char[17] имя клана · @19 i16 userNum пригласившего · @21 i16 userNum приглашённого (мой).
	//   Колбэк только КОПИТ (закон `no-reentrant-net-callbacks`), разбор — `UAsdaClanSubsystem::Tick`.
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnAsdaClanInviteRecv, const TArray<uint8>& /*23 б*/);
	FOnAsdaClanInviteRecv OnClanInviteRecv;

	// ★ 27.09.2026. C→S 4111 `OP_CLAN_MEMBER_INVITE_RECV_RESPONSE_USER` (48 = 8 секрет + 16 MD5 + 24) — [ПРОТОКОЛ] ОТВЕТ
	//   НА ПРИГЛАШЕНИЕ В КЛАН. Эталон `itemSendSplitOrMoveRequest@0xBC9ED0` (★ МИСНЕЙМ, item_inventory.c:7345-7389):
	//     @0 u8 ответ (1 да / 0 нет) · @1 i16 clanId · @3 char[17] имя клана (`sprintf %s`) · @20 i16 пригласивший ·
	//     @22 i16 приглашённый. Заготовка `itemInscriptionCtxCtor@0xBCA150`: @1/@20/@22 = −1, @3..@19 = 0.
	//   Сервер `ClanMemberInvite_Recv_ResponseUser` (GameServer.c:833) сверяет MD5 по 24 б и требует @22 == свой userNum;
	//   «да» → DB → 4109 код 1 обоим и 4135, «нет» → 4109 код 2 пригласившему.
	//   Гейт «Друг» (12435) держит ВЫЗЫВАЮЩИЙ (`UAsdaClanSubsystem::SendInviteAnswer`). ClanName17 — сырые байты 4110 @2.
	bool SendClanInviteAnswer4111(bool bAccept, int32 ClanId, const TArray<uint8>& ClanName17, int32 ReqUserNum, int32 RespUserNum);

	// S→C 4126 «КОНЕЦ СПИСКА ЧЛЕНОВ» — payload РОВНО 0, тело клиентом не читается вовсе
	//   (`case 0x101E: g_bChannelListReqPending = 0; Recv_ServerChannelList(g_pCommunityWnd)`;
	//    сервер `sub_4C28F0(…, 4126, 0, (i16)a1, nullptr, 0, 0)` — буфер `nullptr`).
	//   Это ЧЕСТНАЯ граница порций 4125: подписчику больше не нужно угадывать конец по длине.
	DECLARE_MULTICAST_DELEGATE(FOnAsdaClanMemberListEnd);
	FOnAsdaClanMemberListEnd OnClanMemberListEnd;

	// C→S 4116 «ВЫЙТИ ИЗ КЛАНА». payload 24, полный размер 48 = 8 + 16 + 24.
	//   Эталон `netSendNameConfirmPacket@0xBCA620` (★ МИСНЕЙМ), сервер `ClanMemberLeaveReq_0@0x403930`
	//   (MD5 по 24 б, тело `pPacket+33`). Тело: @0 i32 мой charId (`char+0x41C`) · @4 char[20] моё имя.
	//   ★ ГЕЙТ КЛИЕНТА (эталон `00BCA639`): при звании == 1 (глава) пакет НЕ шлётся — вместо него
	//     нотис 12433. Гейт держит ВЫЗЫВАЮЩИЙ (у него же и окно для нотиса).
	//   Ответ сервера — 4117 (25 б, `body[0]`: 0 отказ / 1 успех).
	// 6515 `netSendAuthToken6515@0xBCC470` (миснейм: к авторизации отношения не имеет) — УДАЛИТЬ
	//   объявление клана. Конверт B, payload 10, полный size 34:
	//     @0 i32 charId · @4 u16 clanIndex · @6 i32 ключ записи объявления.
	//   Без него кнопки «Очистить» 14090…14094 во вкладке клана мертвы (найдено аудитом 31.08.2026).
	bool SendClanNoticeDelete6515(int32 CharId, int32 ClanIndex, int32 NoticeKey);

	bool SendClanLeaveReq4116(int32 CharId, const FString& CharName);

	// S→C 4117 «ОТВЕТ НА ВЫХОД/ИСКЛЮЧЕНИЕ ИЗ КЛАНА», payload 25
	//   (`uiInventoryItemNameResultHandler@0xBCA870`, ★ МИСНЕЙМ — к именам предметов не относится).
	//   `body[0]`: **0 → нотис 12424** («не удалось»), **1 → нотис 12425** («вышел») + эталон чистит
	//   клановый блок персонажа (136 б с `char+0x968`) и ЗАКРЫВАЕТ окно сообщества.
	//   Остальные 24 байта тела эталон не читает — семантика НЕ доказана.
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnAsdaClanLeaveResult, const TArray<uint8>& /*25 б*/);
	FOnAsdaClanLeaveResult OnClanLeaveResult;

	// C→S 6511 «ПРОЧИТАТЬ ОБЪЯВЛЕНИЯ КЛАНА». payload 8, полный размер 32 = 8 + 16 + 8.
	//   Эталон `netSendAntiCheatTokenPacket@0xBCC1C0` (★ МИСНЕЙМ — анти-чита там нет), зовётся при
	//   переходе в подрежим 1 «Информация» и по кнопке 14025. Сервер `Recv_User_Notice_Read@0x406EF0`,
	//   MD5 по 8 б, гейты `clanIndex ≤ 10000` и `CUser+372 == body[0]`. Ответ — 6512.
	//   Тело: @0 i32 мой charId · @4 u16 индекс клана · @6 u16 курсор/страница (`CCommunityWnd+0x78C`).
	bool SendClanNoticeReadReq6511(int32 CharId, int32 ClanIndex, int32 Cursor);

	// ★★★ C→S 6513 «НАПИСАТЬ ОБЪЯВЛЕНИЕ КЛАНА» (добавлено 2026-09-02; кнопки клан-панели 14031 «ОК»
	//   и 14030 «Отмена» привязывает следующая волна — здесь только сетевой слой).
	//   Эталон `chatSendMessage_BE30@0xBCBE30`; конверт вида А (`SendSecured`), 574 = 24 + 550.
	//   payload(550): @0 i32 charId (`char+0x41C`) · @4 i16 clanIndex · @6…@49 нули (ключ, автор,
	//     дата — их проставляет сервер) · **@50 char[500] текст**.
	//   ⚠ Текст лежит по **+50**, а НЕ по +6: `chat.c:5730` пишет в `(char*)&chatPacket[12] + 2`,
	//     сервер читает `strlen(&body[50])` (`GameServer.c:3053`). Реализация «текст с +6» вернула
	//     бы отказ 6514 — эта ошибка уже была в отчёте аудита и снята ревизией.
	//   Сервер требует длину 1..500 и право `Authority & 2` по званию; отказ — 6514.
	//   `ClanIndex <= 0` → берётся последний известный (`KnownClanIndex`, его запоминает 4122/4124).
	bool SendClanNoticeWrite6513(int32 CharId, int32 ClanIndex, const FString& Text);

	// ★★★ C→S 6518 «НАПИСАТЬ ЗАМЕТКУ О СЕБЕ» (добавлено 2026-09-02; кнопка 14023 «Сохранить» —
	//   следующая волна). Эталон `chatSendMessage_C7D0@0xBCC7D0`, конверт А, 94 = 24 + 70.
	//   payload(70): @0 i32 charId · @4 i16 clanIndex · **@6 char[64] текст**.
	//   Сервер `Recv_User_Memo_Write@0x409320` требует `strlen(&body[6]) <= 64`; НИЖНЕЙ границы нет —
	//   пустая заметка законна и стирает прежнюю. Отказ — 6519.
	bool SendClanMemoWrite6518(int32 CharId, int32 ClanIndex, const FString& Text);

	// S→C 6512 «ОБЪЯВЛЕНИЯ КЛАНА», payload РОВНО 546 (сервер `sub_4C28F0(…, 6512, 546, …)`).
	//   [0] u8 флаг: **1 = есть данные**, иначе клиент игнорирует пакет ЦЕЛИКОМ (`00BCC3B4`);
	//   [1] u8 маркер порции: **2 = последняя** → перерисовать панель (`00BCC423`);
	//   [2..545] запись объявления 544 б; клиент держит НЕ БОЛЕЕ 5 записей (при 6-й выбрасывает
	//   самую старую), первые 4 байта записи — числовой ключ (сравнивается с `wnd+0x78C`).
	//   ⚠ ВНУТРЕННЯЯ РАСКЛАДКА 544-байтовой записи НЕ ДОКАЗАНА (разбор живёт в
	//     `strSplitToTokenArray_B@0xBCFD90` / `uiMemberListPopulate@0xBC6490`, отчёт «Что осталось
	//     недостижимым» п.2). Поля из неё НЕ ВЫДУМЫВАЕМ — отдаём тело целиком.
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnAsdaClanNotice, const TArray<uint8>& /*546 б*/);
	FOnAsdaClanNotice OnClanNotice;

	// ── ВКЛАДКА «КТО?» (COMMUNITY_Who_WIN 14600) ─────────────────────────────────────────────
	// C→S 5480 «кто в игре». Эталон `netSendTimestampPacket@0xEBF680` (★ МИСНЕЙМ; game.c:505552-505690),
	//   полный размер 24 = 8 + 16 + 0 — payload ПУСТОЙ. Отчёт: community_tabs_frnd_clan_who.md:1089.
	//   ★★★ 31.08.2026 ДОКАЗАНО двумя методами, что тела нет, и найдено, где живут страница и фильтр:
	//     · клиент: в `netSendTimestampPacket` нет ни одного `memcpy(&buf[24], …)`, размер 24;
	//     · сервер: у `Send_WhoCommand@0x54DB40` сигнатура `(CUser*, __int16)` — указателя на пакет
	//       он физически не получает (GameServer.c:127871, диспетчер :19778-19780);
	//     · ФИЛЬТР целиком серверный: ±5 уровней, «в игре», не заблокирован, charId ≠ свой,
	//       лимит 42 записи (GameServer.c:127907-127914), порции 5481 + пустой 5482;
	//     · СТРАНИЦА целиком клиентская: 7 строк на страницу, поле окна `this[70]`,
	//       всего страниц = ceil(entryCount/7) (`uiCommunityWhoRefreshList@0xEBE630` game.c:505237,
	//       `uiCommunityWhoUpdatePaging@0xEBFD40` game.c:505727); кнопки 14603..14606 ничего не шлют.
	//     Параметры приняты ради контракта, В ПРОВОД НЕ ИДУТ. ★ Реальное действие отправителя, кроме
	//     самой отправки, — СБРОС накопителя записей (`*(this+73) = 0`, game.c:505590): выполняется,
	//     чистится `GetWhoEntries()`.
	bool SendWhoSearch5480(uint8 Page, uint8 Filter);
	// S→C 5481 «страница списка Кто?» — ТЕЛО ЦЕЛИКОМ, записи ПО 35 БАЙТ (до 18 записей = 630 б).
	//   Запись(35) — отчёт §3.5 (:1067-1084): @0 u8 уровень · @1 u8 класс 1-й проф. ·
	//     @2 u8 класс 2-й проф. (★ эталоном НЕ читался — дефект D-W2) · @3 char[20] имя ·
	//     @23 i32 id отряда (−1 = не в отряде) · @27 u8 флаг «глава отряда» · @28 i16 userIndex ·
	//     @30 i32 CharUniqueIndex · @34 u8 код класса (Nick_name_[8]).
	//   ★ СТОП-УСЛОВИЕ: запись, у которой @28 i16 == −1 ИЛИ @30 i32 == −1, — это КОНЕЦ списка;
	//     она и всё после неё не разбираются (эталон `communityWhoAppendListFromPacket@0xEBFC10`).
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnAsdaWhoResult, const TArray<uint8>&);
	FOnAsdaWhoResult OnWhoResult;
	// S→C 5482 «конец списка Кто?», тело ПУСТОЕ (`uiCommunityWhoUpdatePaging@0xEBFD40`) — маркер
	//   «пересчитай страницы и перерисуй». Отчёт: community_tabs_frnd_clan_who.md:1101.
	//   ★ Параметр делегата — тело пакета (у 5482 оно всегда пустое); оставлен ради контракта.
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnAsdaWhoNotice, const TArray<uint8>&);
	FOnAsdaWhoNotice OnWhoNotice;

	// ── НАВЫКИ ДРУЖБЫ (соулмейт) ─────────────────────────────────────────────────────────────
	// S→C 5232 «состояние применения навыка Дружбы», 197 б (`net_handleSoulmateSkillState@0xF505E0`,
	//   диспетчер эталона `case 0x1470`). Тело — отчёт A.3 (soulmate_skills_recall_roulette.md:107-151):
	//     @0 u8 код состояния/ошибки (0..0x23) · @1 i16 userNum кастующего · @3 i16 id навыка Дружбы ·
	//     @5 «боевой блок» 192 б, раскладка та же, что у C→S 5231:
	//       blk+0   i16      основная цель A (renderId моба)
	//       blk+2   i16[10]  targetsA            blk+22  i32[10] damagesA
	//       blk+62  u8[10]   flagsA              blk+72  i32[10] значения удара/лечения A
	//       blk+112 i16      основная цель B (userNum игрока)
	//       blk+114 i16[10]  targetsB            blk+134 i32[10] damagesB
	//       blk+174 u8[10]   flagsB              blk+184 8 б хвост
	//   ★ Ветвление эталона: blk+0 == −1 && blk+2 == −1 → работает только половина B; blk+112 == −1 &&
	//     blk+114 == −1 → только половина A; иначе обе. ★ ДЕФЕКТЫ ОРИГИНАЛА D1 (цикл на 20 целей при
	//     массивах по 10) и D4 (нет ветки кода 18) — на приём не влияют, разбор у подписчика.
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnAsdaSoulSkillState, const TArray<uint8>&);
	FOnAsdaSoulSkillState OnSoulSkillState;
	// S→C 5233 «навык Дружбы применён (отложенная ветка)», 196 б (`Net_RecvSoulmateSkillApply@0xF52B80`,
	//   диспетчер `case 0x1471`). Тело — отчёт A.4 (:153-174): @0 i16 userNum получателя ·
	//   @2 i16 id навыка · @4 тот же «боевой блок» 192 б (смещения внутри блока — как у 5232).
	//   ★ ДЕФЕКТ ОРИГИНАЛА D2: эталон читал флаги половины B по blk+62 (это блок A) вместо blk+174.
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnAsdaSoulSkillApply, const TArray<uint8>&);
	FOnAsdaSoulSkillApply OnSoulSkillApply;
	// S→C 2247 «таблица кулдаунов навыков Дружбы» (`SMSkillCoolTime`), 246 б
	//   (`Net_RecvSoulmateSkillCoolTime@0xE4B6F0`; сервер пересылает 246-байтовый ответ DBAgent 0x8C7
	//   как есть). Тело — отчёт A.5 (:176-200): @0 i16 userNum · @2 i32 charId (сверка синхронизации) ·
	//   далее 30 пар по 8 б: @6+8i i32 id навыка (терминатор −1) · @10+8i i32 остаток кулдауна, мс.
	//   6 + 30*8 = 246 ✓. ★ charId (i32) и userNum (i16) лежат в этом пакете РЯДОМ — не путать.
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnAsdaSoulSkillCooldowns, const TArray<uint8>&);
	FOnAsdaSoulSkillCooldowns OnSoulSkillCooldowns;

	// ══════ ★★★ ПОДСВЕТКА ИКОНОК ЛОТКА НАВЫКОВ ДРУЖБЫ: 5228 (весь блок) и 5238 (один слот) ══════
	//   ПОЧЕМУ ЭТО ВАЖНО: `skillHotbarRefreshSlots@0xE69440` собирает имя текстуры иконки как
	//   `"%s"` при байте == 1 и `"%s%s"` с суффиксом **"-g"** (серый вариант) иначе. Без приёма
	//   этих двух пакетов лоток Дружбы рисует ВСЕ иконки серыми — ровно тот симптом, что был.
	//
	//   ГДЕ ЖИВЁТ БЛОК В ЭТАЛОНЕ (`clan_soulmate_gaps.md` §3.1): `g_pSoulmateWnd` — это НЕ
	//   указатель, а АДРЕС статического объекта `0x02C3A6A8`; приёмник `0x02C3AB90` = объект+0x4E8
	//   = объект+1256. Hex-Rays показал его посторонним глобалом `unk_2C3AB90` и тем «спрятал»
	//   писателя — прежний вывод «писателя в срезе нет» был ОШИБКОЙ МЕТОДА.
	//
	//   ОТОБРАЖЕНИЕ: `объект + 1256 + i = payload[i]`, а читатель лотка берёт `объект + 1257 + k`
	//   (`skill_buff.c:34822`, k = id навыка 30..44) ⇒ **байт «навык k открыт» = blob[k + 1]**.
	//   Блок 101 б покрывает k = 0..99: `[0]` + 100 флагов.
	//
	//   S→C 5228 `netRecvSkillHotbarFull@0xF50560` (`case 0x146C`): `memcpy(объект+1256, body, 0x65)`
	//     — payload РОВНО 101 б (сервер: `sub_4C28F0(…, 5228, 101, …)`, три места, длина сходится).
	//   S→C 5238 `netHandleSkillSlotEnable@0xF505A0` (`case 0x1476`): `byte_2C3AB91[*a1] = 1`, то есть
	//     тело — i16 ИНДЕКС k, и единица кладётся в blob[k + 1] (база `0x2C3AB91` = объект+1257).
	//     payload РОВНО 2 б (сервер: `sub_4C28F0(…, 5238, 2, …)`).
	static constexpr int32 kSoulTrayBlobLen = 101;                 // длина блока 5228, байт
	/** Блок «иконка навыка k открыта» (`объект+1256`, 101 б). Индексация — через `IsSoulTraySkillLit`. */
	uint8 SoulSkillTrayBlob[kSoulTrayBlobLen] = {};
	/** Зажжена ли иконка навыка Дружбы `SkillId` (эталон читает `объект + 1257 + k`, сравнение с 1). */
	bool IsSoulTraySkillLit(int32 SkillId) const
	{
		const int32 Idx = SkillId + 1;                              // база+1: blob[k+1] (см. выше)
		return (Idx >= 0 && Idx < kSoulTrayBlobLen) && (SoulSkillTrayBlob[Idx] == 1);
	}
	/** Блок обновлён (5228 целиком либо 5238 одним слотом) — подписчику пора пересобрать лоток.
	 *  Параметр: id зажжённого навыка для 5238, либо −1 для «пришёл весь блок 5228». */
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnAsdaSoulSkillTray, int32 /*SkillId, −1 = весь блок*/);
	FOnAsdaSoulSkillTray OnSoulSkillTray;

	// ── ЗОВ ДРУГА (Mate Recall) ──────────────────────────────────────────────────────────────
	// S→C 5236 «друг призывает тебя», 42 б. ★ МИСНЕЙМ ЭТАЛОНА: `netOnUpgradeResultForLocal@0xF547B0`
	//   («результат апгрейда») — это приёмник Зова друга, а глобал `g_aUpgradeResultRecord` — на деле
	//   `g_MateRecallRequest` (база 0x2906853). Тело — отчёт B.2 (:341-365):
	//     @0  i32      charId инициатора        @4  i16 userNum инициатора
	//     @6  u8       мир/сервер-группа        @7  u8  карта инициатора
	//     @8  u8       зона                     @9  u8  канал инициатора
	//     @10 char[16] IP канального сервера    @26 i16 порт канального сервера
	//     @28 i16      плиточная X инициатора   @30 i16 плиточная Y/Z инициатора
	//     @32 i32      charId ЦЕЛИ (меня)       @36 i16 userNum ЦЕЛИ (меня)
	//     @38 u8       (сервер `*(v5+99021)`)   @39 u8  карта цели
	//     @40 u8       (сервер `*(v5+99022)`)   @41 u8  канал цели
	//   ★ ГЕЙТ ЭТАЛОНА: зов «мой», только если @36 == мой userNum, иначе пакет игнорируется.
	//   ★ Признак «зов ведёт на другую карту/канал» = (@7 != @39) || (@41 != @9): при нём согласие идёт
	//     НЕ пакетом 5427, а сменой канала/карты по полям @10/@26/@28/@30 (`netHandleChannelPortal`).
	//   Тело кэшируется целиком (`LastMateRecall`) — из него собирается ответ 5237 и координаты 5427,
	//     ровно как эталон кэширует его в `g_MateRecallRequest` (`qmemcpy(..., a1, 0x2A)`).
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnAsdaMateRecall, const TArray<uint8>&);
	FOnAsdaMateRecall OnMateRecall;
	// C→S 5237 «ответ на зов друга». Эталон `netSkill_SendBuffApply5237@0xA09120` (★ МИСНЕЙМ — это
	//   отправитель 5237, а не «применение бафа»); полный размер 36 = 24 + 12.
	//   payload(12) — отчёт B.2 (:367-384):
	//     @0  u8  код (1 = согласие; 2 = отказ — единственное, что шлёт эталон; 0/3/4/5 идут ОТ сервера)
	//     @1  i32 charId инициатора  (сервер: `if (sRes.iRequestUserIndex < 0)` → отбой)
	//     @5  i16 userNum инициатора (сервер: `IsCheckUserNum(sRes.iRequestUserArrayIndex)`)
	//     @7  u8  эхо 5236@6 · @8 u8 эхо 5236@7 (карта) · @9 u8 эхо 5236@8 ·
	//     @10 u8  эхо 5236@9 (канал) · @11 u8 эхо 5236@41 (канал цели)
	//   ★ charId (@1, i32) и userNum (@5, i16) — РАЗНЫЕ поля; оба берутся из кэша 5236 (@0 и @4).
	//     `PartnerUserNum` используется для @5, если кэша нет; при живом кэше он с ним сверяется.
	//   ★★★ 31.08.2026 «сервер их не читает» ПОДНЯТО ДО HIGH и доведено до конца потока:
	//     сервер `Send_ResponseUser_To_RequestUser_MateRecallSkill@0x49ED50` читает ТОЛЬКО @5
	//     (GameServer.c:64360) и @1 (:64362), после чего ПЕРЕСЫЛАЕТ все 12 байт как есть
	//     (:64365; межсерверная ветка `sub_49EE70@0x49EE70` — :64397). Клиент-получатель
	//     `Recv_CharStateNotice@0xF548D0` (диспетчер `case 0x1475`, game.c:15571) читает ТОЛЬКО
	//     @0 (switch по кодам 0/2/3/4/5) и @5 (`CharManager_FindByUserIdx`). Итог: @7..@11 не читает
	//     НИКТО, но длина обязана быть 12 (`sub_523F00(a3, 0xCu, …)`) и MD5 идёт по всем 12 байтам.
	//   ★ Если кэша 5236 нет, поля заполняются НЕ нулями, а значениями конструктора этой же
	//     структуры (`sub_A09780@0xA09780` ≡ серверный `sub_49F000@0x49F000`): @1 = −1, @7..@9 = 0xFF,
	//     @10 = @11 = 0. Ноль в @1 сервер принял бы за настоящий charId; −1 он штатно отбивает.
	bool SendMateRecallAnswer5237(int32 Code, int32 PartnerUserNum);
	// C→S 5427 «телепорт по зову друга: координаты инициатора». Эталон
	//   `netSendMaskedMd5Packet5427@0xA06C50`; полный размер 28 = 24 + 4, MD5 по 4 байтам.
	//   payload(4): @0 i16 X плитки (= 5236@28) · @2 i16 Y/Z плитки (= 5236@30). Отчёт B.2 (:386-402).
	//   ★ Шлётся ТОЛЬКО при согласии на зов В ПРЕДЕЛАХ той же карты и канала; иначе эталон вместо
	//     пакета инициирует переход канала. Сервер `CharSoulMateRecallPosition@0x52BAE0` при
	//     невалидной плитке принудительно ставит 200/200 и пишет лог.
	bool SendMateRecallPosition5427(int32 TileX, int32 TileZ);
	// ★ 26.10.2026 (savepoint) [ОРИГИНАЛ] C→S 5280 «сохранить местоположение» (точка возрождения и свитка возврата) —
	//   «Да» окна (8, 6178) после кнопки NPC 30022. Эталон `itemDropToGroundRequest@0xDA2C60` (МИСНЕЙМ; AsdaStory_game.c:399578):
	//   payload 6 б `[i16 npcId (g_pNpcWnd+0x178A8)][i16 тайл X][i16 тайл Z]`, конверт MD5 БЕЗ секрета
	//   (`CNetwork_Send(…, 22, 5280, 1, 1, 1, 0, 0)`, 22 = 16 MD5 + 6) — наш `SendMd5Signed` с падом.
	//   Сервер `sub_52A360@0x52A360` (GameServer.c:113237; `sub_523F00(…, 6, …, 0)` — MD5 с +9, тело с +25): npcId НЕ читает,
	//   X/Z ≤ 0 → только лог; иначе CUser +27259 карта (текущая карта сервера) / +27260 X / +27262 Z / +27264 IP+порт канала
	//   из VillageServerConfig и 2110 в DBManager → `UPVillagePosUp` (таблица Joy1.dbo.VillagePos). Ответа клиенту НЕТ.
	bool SendSaveVillagePos5280(int32 NpcId, int32 TileX, int32 TileZ);

	// ── РУЛЕТКА СОУЛМЕЙТА (окно GOLD_roullete_WIN 67000) ─────────────────────────────────────
	// C→S 6575 «крутить». Эталон `netSendAntiCheatRequest@0xE6E2D0` (★ МИСНЕЙМ — это запрос рулетки);
	//   полный размер 28 = 24 + 4. payload(4): @0 i32 charId локального игрока (доказано сверкой с
	//   6580@1: `g_LocalPlayer+1052`, network.c:30171/30177/30201). Отчёт C.2 (:460-506).
	//   ★ Валюта входа — не деньги и не предмет, а очки близости соулмейта (порог 200) плюс суточная
	//     отметка; все семь гейтов живут на сервере (`Recv_UserSoulRoulettet@0x49A1C0`), клиент не проверяет.
	bool SendRouletteSpin6575();
	// C→S 6579 «забрать приз». Эталон `netSendAntiCheatPacket@0xE6E5F0` (★ МИСНЕЙМ);
	//   полный размер 28 = 24 + 4. Отчёт C.4 (:530-549).
	//   ★★★ 31.08.2026 PAYLOAD РАЗОБРАН = КОНСТАНТА 0xFFFFFFFF. `sub_934D28` — не функция, а
	//     5-байтовая заглушка компоновки: дизасм реального EXE даёт `0x934d28: jmp 0xe6e760`, а тело
	//     `sub_E6E760@0xE6E760` состоит из одного `mov dword ptr [eax], 0xffffffff`. В отправителе
	//     `0xE6E5FF: lea ecx,[ebp-8]` → это ОТДЕЛЬНАЯ локаль (Hex-Rays склеил её с `this` в «v8[2]»),
	//     и до `memcpy(buf+24, [ebp-8], 4)` (0xE6E6C2) в неё никто не пишет.
	//     `SlotIndex` в провод не идёт и в эталоне (game.c:473622-473624 отдаёт его только в
	//     клиентскую отрисовку). Сервер `Recv_UserSoulResult` тело ЧИТАЕТ (`memcpy(&v8, a3+33, 4)`,
	//     GameServer.c:61991), но тут же затирает прочитанное состоянием из CUser
	//     (`v8.m_pcThread = *(CThreadRun **)(v11 + 99385)`, :62008); строка/сектор/база —
	//     +99382 / +99384 / +99385. Длина payload обязана быть 4 (`sub_523F00(a3, 4u, …)`, :61989).
	bool SendRouletteClaim6579(int32 SlotIndex);
	// S→C 6576 «результат прокрутки», 3 б (`roulettePanelHandleNetEvent@0xE6E480`). Отчёт C.3 (:507-529):
	//   @0 u8 код (1 = крутить; 10 = отказ loc 8081 «близость < 200 / уже сегодня»; 11 = отказ loc 8167
	//   «партнёр офлайн, другой канал, далеко»; 12 = отказ — ★ ветки в эталоне НЕТ, дефект D3) ·
	//   @1 i8 индекс выпавшего сектора 0..3 (угол = сектор × 90°, при 0 → 360°) ·
	//   @2 i8 следующая строка таблицы (−1 = терминал цепочки).
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnAsdaRouletteResult, const TArray<uint8>&);
	FOnAsdaRouletteResult OnRouletteResult;
	// S→C 6580 «выдача приза», 73 б (`Recv_RouletteResult@0xE6E790`). Отчёт C.5 (:551-583):
	//   @0 u8 1 = успех / 0 = ошибка · @1 i32 charId победителя (★ i32 charId, НЕ userNum!) ·
	//   @5 u8 вид приза (0 пусто / 1 опыт / 2 предмет / 3 ОЗ+ОМ) · @6 i32 значение (опыт) ·
	//   @10 запись предмета 57 б (эталон отдаёт её в `OnRecv_ItemUpdate`) ·
	//   @67 i32 текущие ОЗ · @71 i16 текущие ОМ.
	//   ★ ДЕФЕКТ ОРИГИНАЛА D6: ТЕКСТ приза берётся из клиентской таблицы по локальному углу колеса,
	//     а начисление — из серверной; при рассинхроне таблиц «показал одно, дал другое».
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnAsdaRoulettePrize, const TArray<uint8>&);
	FOnAsdaRoulettePrize OnRoulettePrize;

	// Служебное для UI: последнее принятое тело зова друга (5236, 42 б); пусто, если зова не было.
	const TArray<uint8>& GetLastMateRecall() const { return LastMateRecall; }
	// Служебное для UI: кэш записей приятелей (по 31 б), собранный из 4157 / 4160 / 4151(код 1).
	const TArray<TArray<uint8>>& GetFriendEntries() const { return FriendEntries; }
	// Служебное для UI: накопитель записей вкладки «Кто?» (по 35 б). Дописывается пакетами 5481,
	//   очищается ОТПРАВКОЙ 5480 — ровно как в эталоне (`*(this+73) = 0`, game.c:505590).
	//   Страница НЕ сетевая: 7 строк на страницу, всего страниц = ceil(Num()/7), минимум 1
	//   (`uiCommunityWhoUpdatePaging@0xEBFD40`). Фильтр — целиком серверный (±5 уровней и т.д.).
	const TArray<TArray<uint8>>& GetWhoEntries() const { return WhoEntries; }


private:
	TUniquePtr<FAsdaConnection> Conn;
	// ★ 15.10.2026: поставленный к дедлайну 4028 (итог — атомики, пишет поток точной отправки).
	TSharedPtr<FAsdaConnection::FTimedResult, ESPMode::ThreadSafe> Pending4028Result;
	uint64 Pending4028Ticket = 0;          // номер постановки (соединения или исполнителя — для стенда)
	bool bPending4028Stand = false;        // постановка под перехватом стенда/самотеста (сокета нет)
	TArray<uint8> Pending4028Body;         // тело для перехватчика (стенд)
	int32 Pending4028Rid = -1;             // цель (для строки лога)
	bool SendAttackNext4028Impl(int32 TargetRenderId, float TgtX, float TgtY, double DeadlineSec);
	void PollPending4028();                // забрать итог поставленного 4028 (каждый `Tick`)
	void HandlePacket(const Asda::ParsedPacket& Pkt);
	// ★ R1/R2 рефакторинга (10.11.2026, решение координатора): приёмник `HandlePacket` в волне R2 делится на семь подфункций по
	//   областям (карта `Reports/refactor_2026-11/splitmaps/AsdaNetSubsystem.md` §5). Здесь ТОЛЬКО объявления — внесены одним
	//   заходом с прочими правками этого заголовка, чтобы R2 не платила отдельной полной пересборкой. Тел пока НЕТ, `HandlePacket`
	//   их не зовёт: поведение прежнее. Договор: подфункция разбирает группы `case` СВОЕЙ области дословно; `bHandled` = true,
	//   если опкод её, иначе false (тогда диспетчер идёт к следующей области, в конце — прежняя ветка `default`).
	void HandlePacket_Session(const Asda::ParsedPacket& Pkt, bool& bHandled);       // вход, выбор/создание/удаление персонажа, вход в мир, перенос канала, спавн себя, облик (1004…6203)
	void HandlePacket_Items(const Asda::ParsedPacket& Pkt, bool& bHandled);         // контейнеры/склад, предметы, торговец, премиум-ящик, гача, бустеры, улучшение/разбор, лавки
	void HandlePacket_World(const Asda::ParsedPacket& Pkt, bool& bHandled);         // сущности мира: мобы, квесты, удары 4015/4017, портал 5055, скорость 6071, баффы, питомцы, касты, движение
	void HandlePacket_Social(const Asda::ParsedPacket& Pkt, bool& bHandled);        // соулмейт, клан, друзья, пати, объявления
	void HandlePacket_Misc(const Asda::ParsedPacket& Pkt, bool& bHandled);          // наземный лут, PvP/дуэль, война кланов/городов, GM-ответы, отмена торговли, горячие клавиши навыков
	void HandlePacket_CombatState(const Asda::ParsedPacket& Pkt, bool& bHandled);   // ответ на замах 4029/4027, экипировка и транспорт соседей, HP/MP, возрождение 4055/4056, уровень/опыт
	void HandlePacket_Chat(const Asda::ParsedPacket& Pkt, bool& bHandled);          // чат: обычный/шёпот/системный/пати/клан, серверные уведомления

	// ── ★ СОСТОЯНИЕ СЕМЕЙСТВ «Сообщество» и «Соулмейт» (реализация — в конце .cpp) ────────────
	// Метка времени последней УДАЧНОЙ отправки 4156, сек. игрового `FPlatformTime::Seconds()`.
	//   Эталон держит её в `CCommunityFriendUI+66` (`timerStart_9B90@0xCE9B90` = `timeGetTime()`),
	//   а гейт `isWithinTimeout5000@0xCE9B00` сравнивает разницу с 0x1388 = 5000 мс.
	//   Отрицательное значение = «ещё ни разу не слали» (первый запрос уходит без задержки).
	double LastFriendListReqTime = -1.0;
	// Кэш записей списка приятелей по 31 байту (см. раскладку у `OnFriendList`). Нужен ровно для
	//   одного: собрать тело 4154 (код класса @4 и имя @11), которых контракт не передаёт.
	TArray<TArray<uint8>> FriendEntries;
	// Очередь входящих приглашений в приятели (тела 4152 по 22 б: имя[20] + i16 userIndex).
	//   Эталон держит её в `CCommunityFriendUI+0x110` и отвечает 4153 ровно по её полю @20.
	//   ★ Повторное приглашение от того же userIndex эталон молча игнорирует — воспроизведено.
	TArray<TArray<uint8>> PendingFriendInvites;
	// Накопитель записей вкладки «Кто?» по 35 б (раскладка `Send_WhoCommand@0x54DB40`,
	//   GameServer.c:127917-127927: @0 уровень · @1 код класса (клиент: loc 12556 + значение) ·
	//   @3 имя[20] · @23 i32 + @27 u8 деньги · @28 i16 userNum · @30 i32 charId · @34 доп.).
	//   Эталон копит их прямо в окне (`+300`, ёмкость 40 при серверном лимите выборки 42).
	TArray<TArray<uint8>> WhoEntries;
	// Последнее принятое тело зова друга 5236 (42 б) — источник полей ответа 5237 и координат 5427.
	TArray<uint8> LastMateRecall;
	// Клан-индекс, использованный/принятый последним (для 4122 и 4124: payload = i16 clanIndex).
	//   0 = «клана нет» — ровно то, что шлёт эталон на LoadProgress == 11 (`SendEntryFinalize`).
	int32 KnownClanIndex = 0;
	// Обновить кэш `FriendEntries` одной записью в 31 б: заменить по паре (charId @0, класс @4)
	//   либо добавить. Пара — тот же ключ дедупликации, что у эталона (`listContainsIdTypePair`).
	void UpsertFriendEntry(const uint8* Rec31);

	// ★ Анти-реплей-счётчик сессии (obftick_antireplay_spec.md): ОДИН монотонный счётчик на все защищённые
	//   пакеты сессии (char-list/channel-select/enter-world/...), старт 0, +1 на пакет, wrap >0x3FFFFFFF→0.
	//   Сбрасывается при новом коннекте. Сервер (sub_524390) требует строго возрастающий counter, иначе
	//   «Secret Error» → кик 4101 code=3 (текущий баг — секрет слался нулями).
	int32 ObfCounter = 0;
	// Заполняет obfTick (8 байт secret[0..7]): QPC с вбитыми битами (0x57A7A5+counter) по позициям bitPosTable.
	void FillAntiReplaySecret(uint8* Out8);
	// ★★★ 15.10.2026 (ревизия сети, БЛОКЕР-1): выдать номер СЕЙЧАС (прежнее тело `FillAntiReplaySecret`). Зовёт наполнитель
	//   соединения под `IoLock` в момент ухода кадра на провод; при ключе `asda.Net.SecretAtSend 0` или под перехватчиком
	//   стенда — сам `FillAntiReplaySecret` (номер при сборке, как прежде).
	void FillAntiReplaySecretNow(uint8* Out8);
	// Номер выдаётся в момент ухода (ключ `asda.Net.SecretAtSend`, по умолчанию 1).
	static bool IsSecretAtSend();
	// Отправить защищённое тело: при `IsSecretAtSend` номер [0..7] впишет соединение в момент ухода, иначе — как прежде.
	bool ConnSendSecret(uint16 Opcode, const uint8* Body, int32 BodyLen, uint16 HdrA = 0, uint16 HdrB = 0, bool bPad = true);
	// ★ 19.10.2026 (команды оригинала, `AsdaNetGmOriginal.cpp`): конверт «только MD5 пустоты, 16 б» (6028/6029) и
	//   «только токен, 8 б» (6035/6036 без защёлки пробы). What — для журнала.
	bool SendGmEmptyMd5(uint16 Op, const TCHAR* What);
	bool SendGmTokenOnly(uint16 Op, const TCHAR* What);
public:
	// ★ Только самотест (ревизия сети, БЛОКЕР-1): «провод без сокета» у соединения — кадры, уже со вписанным номером, отдаются
	//   Sink под замком соединения в момент ухода (в том числе с потока точной отправки). Офлайн; при живом соединении — отказ.
	bool DebugAttachTestWire(TFunction<void(uint16 Opcode, const uint8* Body, int32 BodyLen)> Sink);
	void DebugDetachTestWire();
	// Подключён провод самотеста: «соединение» есть, но в сеть ничего не уходит — офлайн-гейт самотестов его не считает живым.
	bool IsTestWireActive() const { return Conn && Conn->IsTestWire(); }
private:

	// ── ★★★ GM-«скрыться» (6005/6006). Состояние ведём ТОЛЬКО по подтверждению сервера ──────────
	//   Локального флага «я скрыт» у оригинала нет: клиент поднимает бит скрытия строго в
	//   обработчике 6006 (`localPlayerInputToggleByFlags@0xF49BD0`, network.c:44004) и в 4025/4103.
	//   Данные ОДНОЙ сессии → чистятся в ResetSessionState.
	bool   bGmHidden = false;        // подтверждённое сервером состояние (эхо 6006, байт [1])
	// ★ ОТКУДА взято текущее `bGmHidden`: true — вычислено при входе/портале по формуле сервера
	//   (`CharMake`/`CharPortalMake`, см. `IsSelfOperatorAccount`), сервер этого не подтверждал;
	//   false — пришло эхом 6006. Нужно ТОЛЬКО подписи GM-меню, чтобы она не врала оператору.
	bool   bGmHiddenFromEntry = false;
	// ★ [ПОРТ] Критерий «оператор по префиксу имени» не был проверен при 4003, потому что имени
	//   персонажа тогда ещё не знали. Снимается доопределением в `TrySpawnLocalPlayer`.
	bool   bGmHideEntryNamePending = false;
	// ★★★ [ПОРТ] 18.10.2026 (владелец): последнее состояние скрытия помнится per-персонаж (`[AsdaGm] Hidden.<charId>`
	//   в GameUserSettings, пишется только по эху 6006 «принято»). Сервер прячет оператора при КАЖДОМ появлении
	//   (`CharMake`/`CharPortalMake` пишут 300) — если запомнено «виден» (или записи нет), после входа шлём 6005(0).
	//   Взводится в `ApplyGmHideOnEntry`, исполняется в `Tick` (не из разбора пакета). Откат: `asda.Gm.HideRestoreLast 0`.
	bool   bGmRestoreVisiblePending = false;
	float  GmRestoreRetrySec = 0.f;       // пауза между попытками отправки (гейт перехода/засов 6005)
	int32  GmRestoreAttempts = 0;         // попыток отправки 6005(0) за этот вход (потолок — 10)
	bool   bGmRestoreInFlight = false;    // ушедший 6005 — от восстановления (отказ 6006 → повтор, а не «сдаться»)
	bool   bGmRestoreSending = false;     // идёт отправка ОТ восстановления (иначе 6005 из GM-меню снимает восстановление)
	// Сохранённое состояние скрытия для текущего персонажа: 1 скрыт, 0 виден, −1 записи нет.
	int32 LoadSavedGmHidden() const;
	void  SaveGmHidden(bool bHidden) const;
	void  TickGmRestoreVisible(float DeltaTime);
	// 6005 отправлен, 6006 ещё не пришёл; момент отправки (стенные часы) — для таймаута «ждём» (`asda.Gm.HideAckTimeoutSec`).
	//   ★ Дедуп №2, В2-A, S2 (11.10.2026): прежняя пара `bGmHidePending + GmHideSentAt` — общая точка `FAsdaReplyLatch`, семантика та же.
	FAsdaReplyLatch GmHideLatch;
	// ★ Античит А2 (мера 7): сервер в этой сессии ответил 6006 «принято» — единственный признак, открывающий GM-меню.
	bool   bOperatorConfirmed6006 = false;
	// ★ Античит А2 (мера 7): сколько ответов 6006 пришло за процесс (принято + отказ) — сигнал «ответ получен».
	uint32 GmHideAnswerSeq = 0;
	// ★ ac0r: сколько ответов 6037 (на пробу прав 6036) пришло за процесс — сигнал «ответ получен».
	uint32 GmRightsAnswerSeq = 0;
	// ★ ac0r: проба 6036 ушла в этой сессии и ответа 6037 не было — новых проб до конца сессии нет (таймаут не снимает).
	bool   bGmRightsProbeUnanswered = false;

	// ★ ОБЩЕЕ 60-байтовое тело заявки на применение навыка (эталон `skillCastSelfSendPacket@0x9FE210`).
	//   Один сборщик на 5256 (обычные) и 6158 (хранитель) — в эталоне это буквально одна функция,
	//   различается только опкод в финальном `CNetwork_Send`.
	bool SendSkillUseBody(int32 SkillCode, int32 TileX, int32 TileZ,
		const TArray<FAsdaSkillUseRec>& Recs, int32 GroundTileX, int32 GroundTileZ);

	// ★★ Д6: ЕДИНЫЙ сборщик защищённого пакета: тело = [0..7 анти-реплей секрет][8..23 MD5(payload,Len)]
	//   [24.. payload]. Порядок байт 1:1 с ручными отправителями (MD5 до секрета). bPad = флаг a6 SendPacket
	//   (ДЕФОЛТ true = как у SendPacket; enter-хендшейк 20000 передаёт false). Спец-случаи «MD5 пустого»
	//   (5056) и «MD5 всего cred-блока» (логин) остаются отдельными — у них другая семантика payload.
	bool SendSecured(uint16 Opcode, const uint8* Payload, int32 Len, bool bPad = true);
	// ★ 17.10.2026 (Ф4): тело 18 б 4201/4203 (@0 i16 userIdx цели, @2/@6 X/Z своего, @10/@14 дубль) через `SendSecured`.
	bool SendPvpAttackBody(uint16 Op, int32 TargetUserIdx, float SelfX, float SelfZ);

	// ★★ ВТОРОЙ конверт оригинала: тело = [0..15] MD5(payload) + [16..] payload, БЕЗ 8-байтного
	//   анти-реплей секрета. Им пользуются вся семья отряда (5090/5092/5094/5096) и заявка
	//   соулмейта 5200. Отдельная точка нужна потому, что сервер выбирает формулу сверки по
	//   флагу `a4` в `sub_523F00`: `a4 == 0` ⇒ MD5 @+9 / payload @+25 (этот конверт),
	//   `a4 == 1` ⇒ MD5 @+17 / payload @+33 (конверт `SendSecured`). Перепутать = «Secret Error».
	//   bPad повторяет шестой аргумент `CNetwork_Send` (у всех перечисленных он = 1).
	bool SendMd5Signed(uint16 Opcode, const uint8* Payload, int32 Len, bool bPad = true);

	// ★★★ СТЕНД ПОБАЙТОВОЙ СВЕРКИ ТЕЛ (волна 3 плана дублей, п.10: `FAsdaPacketWriter`; `AsdaPacketStand.cpp`,
	//   отчёт `Reports/dedup_audit/wave3_packet_writer.md`). Перехватчик ставится ТОЛЬКО стендом на его
	//   собственный объект подсистемы: тогда `SendSecured`/`SendMd5Signed`/`SendMenuPacket` отдают payload
	//   (опкод, длина, пад, вид конверта 'S'/'5'/'M') сюда и в сеть не шлют. Перехват стоит ПОСЛЕ проверок,
	//   влияющих на тело (кламп/проверка длины и указателя), и с тем же падом, что уходит в провод; сокет и
	//   гейты `FAsdaOutgoingGate` (состояние живой сессии) при перехватчике пропускаются.
	//   В игре пуст — порядок конвертов прежний: сокет → гейты → длина.
	friend struct FAsdaPacketStand;
	// ★ 11.10.2026 (волна В0 аудита дублей №2): стенд ВХОДЯЩИХ пакетов `asda.Net.RecvStand` (`AsdaRecvStand.cpp`) —
	//   подаёт случайные тела в штатный `HandlePacket` СВОЕГО объекта подсистемы и пишет, что разбор отдал
	//   (делегаты, ответные отправки под перехватом, поля состояния, строки лога). Нужны закрытые поля и `HandlePacket`.
	friend struct FAsdaRecvStand;
	// ★ 25.10.2026: самотест сброса портального входа (`asda.SelfTest.PortalPending`, `AsdaPortalPendingSelfTest.cpp`) —
	//   имитирует разрыв и неудачное подключение штатным путём (`Conn`, `HandleConnectionClosed`), без сети.
	friend struct FAsdaPortalPendingSelfTest;
	// ★ 26.09.2026: самотест координат 4008 (`asda.Net.SelfTestStop4008`, `AsdaStop4008SelfTest.cpp`) — узкий
	//   перехват 4008 (`ArmSendCapture`) и взвод `bActionPending` на время пробы; БОЕВЫЕ отправители не копируются.
	friend struct FAsdaStop4008SelfTest;
	// ★ 27.09.2026: самотест своего 4007 (`asda.Move.SelfTest4007`, `AsdaMove4007SelfTest.cpp`) — перехват
	//   4006/4008 (`ArmSendCapture`), подача 4007/4009 через штатный `HandlePacket` и чтение бита 2.
	friend struct FAsdaMove4007SelfTest;
	// ★ 27.09.2026: самотест аудита отправителей charId/userNum (`asda.Net.CharIdSendersSelfTest`, `AsdaCharIdSendersSelfTest.cpp`) —
	//   БОЕВЫЕ отправки под перехватом (`ArmSendCapture`, узкий для 6124 — чтобы ячейки брались из инвентаря, а не из стенда).
	friend struct FAsdaCharIdSendersSelfTest;
	// ★ 08.10.2026 (рыбалка В1): самотест `asda.Fish.SelfTest` — 6167/6169/6171 под перехватом, приёмы через HandlePacket.
	friend struct FAsdaFishingSelfTest;
	// ★ 08.10.2026 (рыбалка В3): самотест `asda.FishBook.SelfTest` — БОЕВЫЕ 6176/6178/6180/6182 под перехватом.
	friend struct FAsdaFishBookSelfTest;
	// ★ 08.10.2026 (рыбалка В5): самотест `asda.Fish.AnimSelfTest` — 4024/6167/6169/6171 под перехватом, 4025/6168/6170/6172/6173 через HandlePacket.
	friend struct FAsdaFishAnimSelfTest;
	// ★ 09.10.2026 (рыбалка, ревизия В4 ВАЖН-1): самотест `asda.Fish.ZoneSelfTest` — 4006 одиночного клика под перехватом (бит 2 «запрос хода в пути»).
	friend struct FAsdaFishZoneSelfTest;
	// ★ 27.09.2026: самотест тела 4024 (`asda.Net.SelfTest4024`, `AsdaPkt4024SelfTest.cpp`, отчёт
	//   `Reports/owner_0927/pkt4024_facing.md`) — узкий перехват 4024 (`ArmSendCapture(…, 4024)`).
	friend struct FAsdaPkt4024SelfTest;
	// ★ Тот же перехватчик нужен самопроверке питомцев (`UAsdaPetSubsystem::RunSelfTest`): она
	//   обязана гонять НАСТОЯЩИЕ `SendPet*`, а не собственную копию сборки тела — иначе ошибка
	//   в боевом сборщике самопроверкой не ловится (доказано мутациями при ревизии 2026-09-21:
	//   сдвиг смещения в `SendPetCall6100` прежняя самопроверка пропускала). Перехватчик ставится
	//   на время прогона и сразу снимается; в сеть при нём ничего не уходит.
	friend class UAsdaPetSubsystem;
	// ★ Волна 13 (25.09.2026, М-8 ревизии волны 12): самотест окна 78200 проверяет 6500 со 2-й страницы сетки
	//   тем же приёмом — БОЕВАЯ отправка `SendCraftRequest`, перехват на время проверки (`DebugCapture6500`).
	friend class UAsdaHighRefineSubsystem;
	// ★ Ремесло, волна В1 (25.09.2026): самотест окна 76000 проверяет отправители 6598/6600/6602/6604 тем же
	//   узким перехватом — БОЕВАЯ отправка, перехват ТОЛЬКО своего опкода, снимается сам после первого пакета.
	friend class UAsdaCraftSubsystem;
	// ★ 27.09.2026 (`who_clan_fix.md`, СРЕДН-3 ревизии): самотест окна 14450 `asda.Clan.IncreaseSelfTest` проверяет БОЕВОЙ
	//   `SendClanIncrease4136` узким перехватом 4136 (`ArmSendCapture(…, 4136)`), снимает его своим поколением.
	friend class UAsdaClanSubsystem;
	// ★ Волна Л1 торговли (29.09.2026): самотест продавца личной лавки `asda.Trade.SelfTestStoreL1` гоняет БОЕВЫЕ
	//   `SendSelfStoreReady6300/Open6302/Close6305` под перехватом (`ArmSendCapture`, весь трафик — офлайн сокета нет).
	friend struct FAsdaSelfStoreL1SelfTest;
	// ★ Волна Л2 торговли (30.09.2026): самотест посетителя лавки (`asda.Trade.SelfTestStoreL2`) — тот же перехват отправки.
	friend struct FAsdaSelfStoreL2SelfTest;
	// ★ Волна Л3 торговли (30.09.2026): самотест вывесок лавки (`asda.Trade.SelfTestStoreL3`) — перехват 6307/6305/5086.
	friend struct FAsdaSelfStoreL3SelfTest;
	friend struct FAsdaSelfStoreL4SelfTest;   // ★ 30.09.2026: самотест чата лавки (волна Л4) — перехват 6313
	friend struct FAsdaSelfStoreL5SelfTest;   // ★ 30.09.2026: самотест гейтов лавки (волна Л5) — перехват 6305/4006/4024
	// ★ 29.09.2026 (волна О1 торговли): самотест обмена `asda.Trade.SelfTestExchangeO1` — БОЕВЫЕ 5282/5284/5286 под
	//   перехватом (`ArmSendCapture`, весь трафик), приём через штатный `HandlePacket`.
	friend struct FAsdaExchangeP2PSelfTest;
	// ★ 29.09.2026 (волна О2 торговли): самотест окна обмена `asda.Trade.SelfTestExchangeO2` — БОЕВЫЕ 5288/5290/5286 под тем же
	//   перехватом, приём 5285/5287/5289 через штатный `HandlePacket`.
	friend struct FAsdaExchangeP2PSelfTestO2;
	// ★ 30.09.2026 (волна О3 торговли): самотест фиксации/итога `asda.Trade.SelfTestExchangeO3` — БОЕВЫЕ 5291/5293 (и 5288/5290/
	//   5286) под тем же перехватом, приём 5289/5292 через штатный `HandlePacket`.
	friend struct FAsdaExchangeP2PSelfTestO3;
	// ★ 30.09.2026 (волна О4 торговли): самотест итога обмена `asda.Trade.SelfTestExchangeO4` — БОЕВЫЕ 5282/5288/5291/5293 под тем же
	//   перехватом, приём 5285/5289/5292/5294/5295/9913 через штатный `HandlePacket`; вес читает/возвращает.
	friend struct FAsdaExchangeP2PSelfTestO4;
	// ★ 06.10.2026 (волна 2-В): самотест `asda.UI.SelfTestWave2C` — БОЕВОЙ `SendDuelAnswer4303` под узким перехватом 4303.
	friend struct FAsdaWave2CSelfTest;
	// ★ 18.10.2026: самотест `asda.Quest.SelfTestJobChange` — квесты сразу после сдачи / смены профессии / нового уровня.
	friend struct FAsdaQuestJobChangeSelfTest;
	// ★ 18.10.2026: блок персонажа тела 5042 (body[10..14]) — до рассылки `OnQuestTurnInResult`. Эталон
	//   `net_ApplyPlayerFullUpdate@0xD828A0`, `case 0` (resultCode = body[0]): смена профессии (`jobChangeFlag`,
	//   network.c:21835-21853) и ветка «только очки умений» (`updateFlags[4]`, network.c:21825/:22607-22611).
	void ApplyQuestTurnInCharBlock(const std::vector<uint8>& B);
	// ★ 06.10.2026 (волна 2-Б): самотест `asda.Npc.SelfTestWave2B` — БОЕВЫЕ `SendTitleItemCheck6150`/`SendUserEventItemStack6132` под перехватом.
	friend struct FAsdaWave2BSelfTest;
	// ★ 06.10.2026 (волна 2-Г): самотест `asda.UI.SelfTestWave2D` — БОЕВЫЕ 6744/6134/5458/6137 под перехватом.
	friend struct FAsdaWave2DSelfTest;
	// ★ 08.10.2026 [ПОРТ]: самотест `asda.Text.SelfTestArabic` — БОЕВЫЕ отправители чата 5084/5086/6560/5088/5105/4120 под перехватом (байты RU/EN/AR).
	friend struct FAsdaArabicTextSelfTest;
	// ★ 11.10.2026 (дедуп №2, В3 S1): общий каркас самотестов `AsdaSelfTestKit` — перехват с отказом при занятом (N1(в)).
	//   Новым самотестам `friend` здесь не нужен: перехват — через `AsdaSelfTestKit::ArmCapture/DisarmCapture`.
	friend struct FAsdaSelfTestKitNet;
	TFunction<void(uint16 Opcode, const uint8* Payload, int32 Len, bool bPad, TCHAR Envelope)> SendCaptureForStand;
	// ★ Волна 14 (МЕЛК-3 ревизии волны 13): УЗКИЙ перехват. 0 — перехватчик берёт ВЕСЬ исходящий трафик (стенд,
	//   питомцы: у них сокета нет). ≠0 — только этот опкод; всё остальное идёт обычным путём (сокет → гейты), а
	//   `IsSendLinkUp` и подмена ячеек 6124 перехватчик не видят. Так ставит его самотест 78200 (`DebugCapture6500`).
	uint16 SendCaptureOnlyOpcode = 0;
	// Перехватчик берёт этот опкод? (стоит И (весь трафик ИЛИ ровно этот опкод)).
	bool IsStandCapture(uint16 Opcode) const
	{
		return static_cast<bool>(SendCaptureForStand) && (SendCaptureOnlyOpcode == 0 || SendCaptureOnlyOpcode == Opcode);
	}
	// Перехватчик стоит на ВЕСЬ трафик (стенд) — для проверок без опкода (связь, ячейки 6124).
	bool IsStandCaptureAll() const { return static_cast<bool>(SendCaptureForStand) && SendCaptureOnlyOpcode == 0; }
	// ★ Волна 15 (МЕЛК-2 ревизии волны 14): ПОКОЛЕНИЕ перехватчика. Отладочные перехваты окна 78200
	//   (`DebugCapture6500`, узкий) и питомцев (`BeginSendCapture`, весь трафик) ставятся через `ArmSendCapture`
	//   и снимаются через `DisarmSendCapture` СО СВОИМ номером: чужой перехватчик не снимается, а постановка
	//   всегда задаёт и узость (`OnlyOpcode`), так что прежняя узость 6500 не достаётся перехвату питомцев.
	//   0 — «нет своего перехватчика». Стенд (`FAsdaPacketStand`) работает на отдельном объекте подсистемы.
	uint32 SendCaptureGeneration = 0;
	// Поставить перехватчик (весь трафик при `OnlyOpcode == 0`, иначе только этот опкод). Возвращает поколение.
	uint32 ArmSendCapture(TFunction<void(uint16, const uint8*, int32, bool, TCHAR)> Fn, uint16 OnlyOpcode)
	{
		SendCaptureForStand   = MoveTemp(Fn);
		SendCaptureOnlyOpcode = OnlyOpcode;
		if (++SendCaptureGeneration == 0) { ++SendCaptureGeneration; }   // 0 зарезервирован под «нет»
		return SendCaptureGeneration;
	}
	// Снять перехватчик, только если он всё ещё того поколения, что вернул `ArmSendCapture`. true — снят.
	bool DisarmSendCapture(uint32 Generation)
	{
		if (Generation == 0 || Generation != SendCaptureGeneration || !SendCaptureForStand) { return false; }
		SendCaptureForStand   = nullptr;
		SendCaptureOnlyOpcode = 0;
		return true;
	}
	// Коды предметов ячеек контейнера 2 для 6124 НА СТЕНДЕ: у объекта стенда нет подсистемы инвентаря
	//   (его GameInstance не инициализирован), и без подмены цикл сканирования остался бы непроверенным.
	//   Читается ТОЛЬКО при стоящем перехватчике; в игре не используется.
	TArray<int32> StandContainer2CodesForStand;
	// Сокет жив либо стоит перехватчик стенда — проверка связи отправителей на `FAsdaPacketWriter`.
	bool IsSendLinkUp() const;

	// Троттлинг эталона у 6098 (`this[27930]`, порог 0xBB8 = 3000 мс) — «покупка у NPC по коду обмена».
	double LastNpcExchangeSendTime = 0.0;

	// ★ 27.09.2026 — «раз в календарный день на цель» у 6059 (`netSendPacket6059@0xD8CEA0`). Три поля объекта игры эталона,
	//   начальные значения — из `gameObj_ctor@0xD8E300` (game_main.c:10543-10545). Отказ: взведено && цель та же && день > 0 && день
	//   тот же (`GetLocalTime().wDay`) → нотис 7501 окном (0, 24), пакета нет. Сбрасываются вместе с сессией (оригинал при разрыве
	//   закрывает клиент — новый процесс начинает с конструктора).
	int32 EventNpcDayArmed = 0;     // `self[24115]` — 1 после первой отправки 6059
	int32 EventNpcLastTarget = -1;  // `self[24116]` — цель последней 6059; ★ ОБЩЕЕ поле: его же пишет прошедший троттлинг 6098
	int32 EventNpcDay = -1;         // `self[24117]` — день месяца последней 6059 (`wDay`, 1…31)
	// Вернуть три поля к значениям конструктора эталона.
	void ResetEventNpcDayThrottle() { EventNpcDayArmed = 0; EventNpcLastTarget = -1; EventNpcDay = -1; }
	// Самотест: подменить «сегодняшний день» (0 — настоящий `FDateTime::Now().GetDay()`).
	int32 EventNpcDayOverrideForTest = 0;

	// ★ Д7: ЕДИНЫЙ захват «пол self из снапшота появления» (сравнение+предупреждение+присваивание).
	//   Раскладка записей 4005/4065 РАЗНАЯ (пол @37 vs @54) — оффсет разбирает вызывающий, сюда идёт готовый байт.
	//   ★ история бага: читали созвездие @47/@95 вместо пола @37/@54 (исправлено 2026-08-07) — потому в одной точке.
	void CaptureSelfGenderFromSpawn(uint8 Gender, const TCHAR* Src);

	// ★ Keepalive (эталон Net_SendKeepAlivePing@0x95C240): держит сессию login-сервера ЖИВОЙ. Без него сессия
	//   виснет idle-таймаутом сервера → призрак → overlap на след. входе → server-select/char-list кикаются.
	//   Тело = MD5("") 16б, opcode 1000, БЕЗ obfTick. Шлём периодически, пока подключены (после логина).
	void SendKeepAlive();
	// Сбросить метку пинга на «сейчас» (эталон: `g_LastNetTickStamp = timeGetTime()` при подключении и ответе 6004).
	void ResetNetHeartbeatStamp();
	double LastNetTickStampSec = 0.0;   // эталон g_LastNetTickStamp: момент последнего пинга/сброса, FPlatformTime::Seconds(); 0 — сессии нет
	bool bLoggedIn = false;       // получен char-list (char-select); сброс при (пере)подключении
	// ★ login-сокет ЖИВ (с момента успешного connect и до его закрытия) — гейт keepalive 1000. Сервер
	//   считает пинги от TCP-accept (m_iUserNum>=0), поэтому пинговать надо ДО логина, иначе кик 4101 code=1.
	bool bLoginConnActive = false;
	// ★ EAC Э1 (16.10.2026): KICK сайдкара / нарушение целостности EAC — разорвать игровую сессию в ближайшем Tick
	//   (код 6 → HandleConnectionClosed). Флаг ставит подписка на UAsdaAntiCheatSubsystem::OnFatal.
	bool bPendingEacKick = false;
	// ★ EAC ред. 3 (E1-07/E1-11): код причины разрыва из таблицы оригинала для этого события (6 — нечестная игра 1106,
	//   2 — дубль-сессия 1099 для вытеснения, 11 — ошибка авторизации 12941, 1 — сбой сети 1098) и loc-id EAC (991xxx):
	//   если строка 991xxx уже есть в DT_GameText — в мире показывается она (попап 8000), иначе — нотис оригинала по коду.
	uint8 PendingEacKickReason = 6;
	int32 PendingEacLocId = 0;
	// ★ 16.10.2026: готовый текст игроку (AsdaEacLoc::ForFatal: заголовок + причина [+ деталь EAC]) на языке сессии;
	//   пусто — строк 991xxx нет → нотис оригинала по PendingEacKickReason.
	FString PendingEacText;
	// ★ 16.10.2026: PendingEacText на время HandleConnectionClosed — для окна кика логина (GetEacKickText).
	FString EacKickText;
	// ★ EAC ред. 3 (CONTRACT.md §8.1): рукопожатие AsdaSecure игрового соединения завершено — привязку (32 б) отдать
	//   UAsdaAntiCheatSubsystem (кадр BIND) в Tick, вне колбэка соединения. Затирается после передачи.
	bool  bPendingEacBind = false;
	uint8 PendingEacBind[32] = {};
	// ★ EAC R3-01 (CONTRACT.md §8.6): клиент отправил по игровому соединению возврат к выбору персонажа (1028). Закрытие этого
	//   соединения сервером без кика 4101 — смена персонажа, а не разрыв: канал 15901 и сессия EAC сохраняются.
	//   Ставит SendReturnToCharSelect (только в мире), снимают HandleConnectionClosed, ConnectToLogin, EnterGameWorld, Disconnect.
	bool bEacReturnToCharSelectSent = false;
	// Обработчик обрыва со стороны сервера (колбэк FAsdaConnection::OnClosed).
	void HandleConnectionClosed();

	// Строка версии, сверяемая login-сервером (cred+0). Зависит от сборки сервера.
	static const char* LoginVersionString;

	// Сборка тела логина (267б) по login_server.md §1 (реверс LoginManager):
	// [0..7] secret + [8..23] MD5(cred) + [24..266] cred-block (version@0/login@20/MD5pass@81).
	// NB: серверный патч (JNZ→JMP @0x406E99/0x406F1D) обходит проверку целостности (Secret+MD5);
	// сервер читает cred-block с frame+33 == body+24. Пароль обрабатывает SQL-процедура UPUserLogin.
	TArray<uint8> BuildLoginBody(const FString& Login, const FString& Password) const;
};
