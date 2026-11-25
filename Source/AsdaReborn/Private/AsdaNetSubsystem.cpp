// Copyright (c) 2026 @sitxovski (Discord)
// ★ 22.10.2026 РАЗНОС ФАЙЛА (слово владельца «огромные файлы разделить … с гарантией 100 %»): методы UAsdaNetSubsystem
//   разнесены по смысловым группам в AsdaNetSubsystem_{Session,Social,Combat,Items,World}.cpp; общие чистые static-
//   хелперы и категория журнала LogAsdaNet — в AsdaNetSubsystemInternal.h. Строки перенесены БАЙТ В БАЙТ, доказательство —
//   `python Tools/asda_split_cpp.py verify --plan Reports/split_net_2026-10-22/plan.json` (0 расхождений).
//   Здесь осталось ядро: подключение, Initialize/Deinitialize, Tick, XyT, секрет, гейт оператора, диспетчер HandlePacket.
#include "AsdaNetSubsystem.h"
#include "UI/AsdaCommunityClanSubsystem.h"   // ★ 01.11.2026 (clan-window): приём клановых пакетов панели «Предводитель»
#include "AsdaWeightWire.h"   // ★ 29.10.2026 (вес ×2): AsdaWeightFromWire — вес по проводу u16 (1017, 5003, 4999, 6125, …)
#include "World/AsdaAttackRhythmSubsystem.h"   // ★ 15.10.2026: серверные часы обычной атаки (4026/4028/5256)
#include "AsdaTimedSend.h"                     // ★ 15.10.2026: поток точной отправки (стендовая постановка 4028)
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "AsdaXytKeyHex.h"   // ★ R5-fix D1: разбор hex-ключа приёма XyT (одна точка, строгий режим под ключом)
#include "AsdaPerf.h"              // ★ R4, доля a: области замера времени игрового потока
#include "HAL/IConsoleManager.h"
#include "Text/AsdaLang.h"   // ★ N9 (дедуп №2, В2-A): FAsdaLang::MissingTag — единая заглушка «[#id]»
#include "World/AsdaWorldSubsystem.h"
#include "UI/AsdaSoulStoneSubsystem.h"   // ★ 06.11.2026: 5420, 5421, 5424, 5425
#include "UI/AsdaSoulmateSkillSubsystem.h"
#include "UI/AsdaEffectsSubsystem.h"   // ★ гейт продолжения боя: обездвиживающие состояния
#include "AsdaConnection.h"
#include "Net/NetOpcodes.h"
#include "Misc/SecureHash.h"
#include "Login/AsdaLoginData.h"
#include "AsdaBytes.h"   // D4b: единые читалки little-endian вместо лямбд/открытых выражений в каждом хендлере
#include "AsdaPacketWriter.h"   // ★ волна 3 п.10: единый сборщик тел исходящих пакетов (вместо uint8 P[N] + WrI16)
// Проверка раскладки тела перед отправкой (хвост волны 3 п.10); определение и разбор — у `IsSendLinkUp`.
static bool AsdaPacketLayoutBroken(const FAsdaPacketWriter& W, uint16 Opcode, const TCHAR* Sender);
#include "Login/AsdaText.h"                 // имена класса/созвездия из loc-таблиц
// раскладка 0x3EF по itemType   ★ R3: комментарий снятого включения "Items/AsdaItemTableSubsystem.h" (файл им не пользовался) — сохранён
#include "World/AsdaLoadManagerSubsystem.h"  // 6004 успех → BeginMapLoad (Load Manager)
// loc-строки форматов чата (20030/1187/1109/8200/…)   ★ R3: комментарий снятого включения "Text/AsdaTextSubsystem.h" (файл им не пользовался) — сохранён
#include "UI/AsdaChatWindowSubsystem.h"      // RankColor / SlotColor — таблицы каналов и рангов
#include "UI/AsdaInventorySubsystem.h"        // Дд6: FAsdaItemInstance::kRecordSize (57 б) — длина записи предмета
#include "UI/AsdaMessageSubsystem.h"    // 5055 отказ портала: нотисы 1096/1097 в системный чат
#include "UI/AsdaNotice.h"              // ★ 27.09: отказы 5055/6201 — плашка 221 и пара «плашка + журнал» (общие точки)
#include "UI/AsdaChatSubsystem.h"       // ★ 27.09: ColGold() = эталонный −11514 отказов перехода
#include "UI/AsdaHotbarSubsystem.h"     // сохранение раскладки панели перед переходом (hotbar_SendAllLayouts)
#include "UI/AsdaMinimapSubsystem.h"
#include "World/AsdaPossessGate.h"      // ★ гейт действий при одержимости (SoulState==2), см. шапку заголовка
#include "AsdaOutgoingGate.h"               // ★ единый шлюз исходящих пакетов (E1, 14.09.2026)
#include "Test/AsdaSelfTestKit.h"   // ★ 11.10.2026 (В3 S1): общий каркас самотестов (итог и `quit` диагностики asda.Gm.TestEntryHide)
#include "UI/AsdaEntryWindows.h"               // ★ 22.10.2026: окна при загрузке карты — список эталона
#include "UI/AsdaCampWarSubsystem.h"           // ★ 30.10.2026: 5055 код 1 → «Битва армий» (49530, сброс при уходе с карты кампвара)
#include "UI/AsdaCampWarBattleSubsystem.h"     // ★ 30.10.2026: боевая фаза — 4055/6013 при включённом HUD боя
#include "AsdaSelf.h"                       // ★ «это я?» — единая точка своего номера/уровня (E2, 14.09.2026)
#include "World/AsdaCoord.h"                 // ★ единый перевод UE↔игра (Аудит №3 B1) — для SendStop4008AtActor
#include "Engine/GameInstance.h"
#include "Engine/Engine.h"                 // ★ С-Р1 ред. 1: GEngine->AddOnScreenDebugMessage — отказ второго барьера на экране
#include "Net/ChannelCipher.h"                 // ★ XyT: живой ключ ПРИЁМА (профиль + подстановка)
#include "HAL/PlatformFileManager.h"           // ★ XyT: метка времени файла ключа
#include "Misc/FileHelper.h"                   // ★ XyT: чтение файла ключа
#include "Misc/Parse.h"                        // ★ XyT: FParse::HexDigit
#include "Misc/ConfigCacheIni.h"               // ★ GM: список операторских префиксов имени из DefaultGame.ini
#include "World/AsdaKingCrownSubsystem.h"      // ★ 27.09.2026: приём 6126 «корона владельца добычи рейд-босса»
#include "AsdaLogSeal.h"                     // ★ 19.10.2026: логин учётки → .alog.meta журнала сессии (SetSessionAccount)
#include "AsdaAntiCheatSubsystem.h"           // ★ EAC Э1 (16.10.2026): клиентский Easy Anti-Cheat (модуль AsdaAntiCheat)
#include "UI/AsdaDisconnectPopupSubsystem.h"  // ★ EAC ред. 3: попап 8000 с текстом EAC (991xxx), когда строка утверждена
#include "World/AsdaCombatTrace.h"            // ★ 18.10.2026: журнал боя — этапы ОТПР/ПРИЁМ/ОТКАЗ боевых пакетов
#include "World/AsdaMobSkillSubsystem.h"      // ★ 30.10.2026: навыки мобов 8011/8012/8013
#include "UI/AsdaArrowAmmoSubsystem.h"        // ★ 31.10.2026: 5030 — расход стрел слота 10
#include "AsdaNetSubsystemInternal.h"   // ★ 22.10.2026 разнос: общее для частей AsdaNetSubsystem*.cpp (LogAsdaNet, чистые хелперы)
#include "UI/AsdaTutorialEvents.h"   // ★ 10.11.2026 обучение: флаги шагов гида 1950 (TUTORIAL.md §2.3)
#include "AsdaNetSubsystemRecvInternal.h"   // ★ R2: общие помощники и объявления частей AsdaNetSubsystem*.cpp (после включений пролога)

// ★★★ 05.11.2026 (job4-skills) [ИСПРАВЛЕНИЕ] «урон себе» у навыков на себя — 611 Parry, 612 King's Rage, 616 Violent Storm.
//   Причина — на сервере: само-каст шлёт одну запись «персонаж = я» (эталон `combatHandleSkillUse@0xE4B960` →
//   `skillCastSelfSendPacket@0x9FE210`), NpcServer `SkillStart` (sub_418670) считает по ней урон, если у навыка ненулевой
//   множитель .sif +0x3E/+0x40 (у этих трёх 100+20/ур. и 120+45/ур.; в подсказках [ms] нет), а GameServer вычитает его из HP
//   кастера (живой журнал Player5 03.11 20:51:00: 4058 «−1156», затем 5257 с той же записью). Порт поверх этого вычитал урон
//   ВТОРОЙ раз локально (`ApplyVitalsDamage` «урон навыка») и писал «You have delivered 1156 damage to <себе> with Parry».
//   Серверная часть — P109 (AsdaPatch v66: урон таких записей 0 ещё в 8206). Здесь — клиентская: запись 5257/5270 «цель-персонаж =
//   сам кастер» с уроном ≠ 0 разбирается как запись обычного само-баффа (урон 0, флаги & 0x03): ни цифры, ни строки чата, ни
//   локального вычитания; настоящий HP — только из 4058/4060 сервера. Чужие игроки и мобы не трогаются.
//   Ключ `asda.Skill.SelfHitNoDamage`: 1 — исправление; 0 — прежнее поведение (урон записи применяется и к себе).
int32 GAsdaSkillSelfHitNoDamage = 1;
static ASDA_DEV_CVAR_REF(CVarAsdaSkillSelfHitNoDamage,
	TEXT("asda.Skill.SelfHitNoDamage"),
	GAsdaSkillSelfHitNoDamage,
	TEXT("[ИСПРАВЛЕНИЕ 05.11] 1 — запись 5257/5270 «цель = сам кастер» с уроном разбирается как само-бафф (урон 0, флаги & 0x03): нет цифры, строки чата и локального вычитания HP (611/612/616); 0 — прежнее поведение."),
	ECVF_Default);

// ★ 16.10.2026 (EAC, тексты владельца): текст игроку по ключам AsdaEacLoc::ForFatal на языке сессии — заголовок, текст причины,
//   (по случаю) деталь EAC последней строкой. Пусто — своего текста нет (строк 991xxx в таблице нет, детали нет): тогда
//   показывается нотис оригинала по запасному коду. Деталь EAC — чужой текст: угловые скобки гасятся (не разметка).
FString UAsdaNetSubsystem::BuildEacPlayerText(const UGameInstance* GI, int32 TitleId, int32 BodyId, bool bDetails, const FString& Details)
{
	const FString Title = TitleId > 0 ? AsdaNotice::Loc(GI, TitleId) : FString();
	const FString Body  = BodyId  > 0 ? AsdaNotice::Loc(GI, BodyId)  : FString();
	FString Det = bDetails ? Details.TrimStartAndEnd() : FString();
	Det.ReplaceInline(TEXT("<"), TEXT("‹"));
	Det.ReplaceInline(TEXT(">"), TEXT("›"));
	if (Body.IsEmpty() && Det.IsEmpty()) { return FString(); }   // только заголовок — мало: нотис оригинала
	TArray<FString> Lines;
	if (!Title.IsEmpty()) { Lines.Add(Title); }
	if (!Body.IsEmpty())  { Lines.Add(Body); }
	if (!Det.IsEmpty())   { Lines.Add(Det); }
	return FString::Join(Lines, TEXT("\n"));
}
FAsdaPeer4056 GAsdaPeer4056;
// Тайл и признак «прибыл мёртвым» из текущего 4056 о соседе `Uidx`; false — пакет не о нём (или не 4056).
bool AsdaPeerEntry4056(int32 Uidx, int32& OutTileX, int32& OutTileZ, bool& bOutDeadArrival)
{
	if (!GAsdaPeer4056.bValid || GAsdaPeer4056.Uidx != Uidx) { return false; }
	OutTileX = GAsdaPeer4056.TileX; OutTileZ = GAsdaPeer4056.TileZ; bOutDeadArrival = GAsdaPeer4056.bDeadArrival;
	return true;
}

static ASDA_DEV_CVAR(int32, CVarQuestApplyJobChange,
	TEXT("asda.Quest.ApplyJobChange"), 1,
	TEXT("5042: 1 — сеть применяет смену профессии/очки до OnQuestTurnInResult и шлёт OnSelfJobChanged; 0 — прежний путь (баннер пишет поля сам)."),
	ECVF_Default);

DEFINE_LOG_CATEGORY(LogAsdaNet);   // ★ 22.10.2026 разнос: было DEFINE_LOG_CATEGORY_STATIC; объявление — AsdaNetSubsystemInternal.h (один экземпляр на все части)

// Имя класса по classId (loc 12556+job). RU-строки из Reports/charselect_loc_extracted.json.
FString FAsdaCharRecord::ClassName(uint8 Job)
{
	// ★ Имя класса из loc-таблицы DT_GameText (эталон buildLocalizedNoticeString: loc 12556+job). Фолбэк — встроенные.
	const FString L = FAsdaLoginData::Loc(12556 + (int32)Job);
	if (!L.IsEmpty()) { return L; }
	// [ПОРТ] 09.10.2026 (мультиязычность, этап 3): встроенный RU-список имён убран — английский игрок не должен
	//   видеть русский. Строки нет в таблице (классы 0..25 = loc 12556..12581) → нейтральная метка «[#id]».
	constexpr int32 kClassCount = 26;   // число классов (jobId 0..25) в диапазоне loc 12556..12581
	if (Job < kClassCount) { return FAsdaLang::MissingTag(12556 + (int32)Job); }
	return TEXT("—");
}

// Имя созвездия по constellationId (Constellation.BIN). RU-строки 1..12 из того же отчёта.
FString FAsdaCharRecord::ConstellationName(uint8 Id)
{
	// ★ Имя знака из loose-таблицы Constellation (DT_Constellation, id-байт→name). Фолбэк — встроенные 12 знаков.
	if (Id >= 1)
	{
		// ★ Таблица Constellation.csv 1-ИНДЕКСНАЯ (RowName «1»=Овен..«12»=Рыбы). Раньше искали по Id-1 →
		//   ConstellationName(2)=row«1»=«Овен» вместо «Телец» → ДУБЛЬ «Овен» в списке. Ищем по Id напрямую.
		const FString L = FAsdaText::Get(TEXT("Constellation"), (int32)Id, TEXT("name"));
		if (!L.IsEmpty()) { return L; }
	}
	// [ПОРТ] 09.10.2026 (мультиязычность, этап 3): встроенный RU-список 12 знаков убран (английский игрок увидел бы
	//   русский). Нет строки в таблице Constellation → нейтральная метка «[#Constellation.N]».
	constexpr int32 kSignCount = 12;   // знаков зодиака (строки 1..12 таблицы Constellation)
	if (Id >= 1 && Id <= kSignCount) { return FString::Printf(TEXT("[#Constellation.%d]"), (int32)Id); }
	return TEXT("—");
}

// ctor/dtor определены здесь, где FAsdaConnection — полный тип (для TUniquePtr).
UAsdaNetSubsystem::UAsdaNetSubsystem() = default;
UAsdaNetSubsystem::~UAsdaNetSubsystem() = default;

void UAsdaNetSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	AsdaGmLoadOperatorNamePrefixesFromIni();
	Conn = MakeUnique<FAsdaConnection>();
	Conn->SetOnPacket([this](const Asda::ParsedPacket& P) { HandlePacket(P); });
	// ★ Обрыв со стороны сервера (FIN/RST) — эталонный путь FD_CLOSE → Show_DisconnectPopup.
	Conn->SetOnClosed([this]() { HandleConnectionClosed(); });
	// ★★ МЕЛК-2 (ревизия shop_offline_fix, 10.10.2026): единая точка «перед каждой отправкой» — вето прерванного
	//   офлайн-самотеста (без прерывания вето пропускает всё, обычная игра его не видит).
	//   ★ 12.10.2026: + вето на время замера первого запуска (`bBenchSendVeto`, UAsdaBenchSubsystem).
	Conn->SetSendVeto([this](uint16 Opcode) { return bBenchSendVeto || FAsdaOutgoingGate::IsSendVetoedByAbortedSelfTest(this, Opcode); });
	// ★ 15.10.2026 (ревизия сети, БЛОКЕР-1): номер защищённого пакета вписывает соединение в момент ухода, под своим замком
	//   (здесь же — и с потока точной отправки: счётчик трогается только под `IoLock` соединения).
	Conn->SetSecretFiller([this](uint8* Out8) { FillAntiReplaySecretNow(Out8); });
	// ★ EAC Э1 (16.10.2026): KICK сайдкара / нарушение целостности клиента → разрыв игровой сессии ШТАТНЫМ путём сброса
	//   (HandleConnectionClosed). Событие может прийти из колбэка канала EAC или из EOS_Platform_Tick — поэтому здесь только
	//   флаг, сам разрыв — в Tick (закон no-reentrant-net-callbacks).
	Collection.InitializeDependency(UAsdaAntiCheatSubsystem::StaticClass());
	if (UAsdaAntiCheatSubsystem* Eac = AsdaEacOf(this))
	{
		Eac->OnFatal.AddWeakLambda(this, [this](const FAsdaEacFatal& F)
		{
			// ★ ред. 3 (E1-07/E1-11): код причины по таблице оригинала (попап 8000 / текст кика логина) + loc EAC.
			//   KICK 5 «вытеснен новой EAC-сессией» — нейтральный 1099 (дубль-сессия), а не «нечестная игра»;
			//   окончательный отказ входа EAC — 12941 «ошибка авторизации» (Auth) или 1098 «сбой сети» (Unreachable).
			// ★ 16.10.2026: ключи показа и запасной код — одна чистая функция AsdaEacLoc::ForFatal (самотест asda.SelfTest.Eac §10).
			const AsdaEacLoc::FFatalDisplay D = AsdaEacLoc::ForFatal(F);
			const uint8 Reason = D.FallbackReason;
			PendingEacKickReason = Reason;
			PendingEacLocId = D.BodyId;
			PendingEacText = BuildEacPlayerText(GetGameInstance(), D.TitleId, D.BodyId, D.bDetails, F.Details);
			UE_LOG(LogAsdaNet, Warning, TEXT("[EAC] фатальное событие (%s, причина %u, нарушение %d, loc %d) → разрыв игровой сессии в ближайшем Tick (код %u)"),
				F.Source == EAsdaEacFatalSource::SidecarKick ? TEXT("KICK сайдкара")
					: (F.Source == EAsdaEacFatalSource::IntegrityViolation ? TEXT("нарушение целостности") : TEXT("отказ входа EAC")),
				(uint32)F.OurReason, F.ViolationType, F.LocId, (uint32)Reason);
			bPendingEacKick = true;
		});
		// ★ 16.10.2026 (решение координатора): мягкое состояние EAC (игра идёт, повтор сам) — строка в системном чате
		//   (категория Connection → журнал системного чата). Поток зовёт это один раз на смену состояния, не на попытку.
		//   Сеть здесь не трогается — показ прямо из события. Строки нет — нотис оригинала (12941/1098), нет и его — журнал.
		Eac->OnSoftNotice.AddWeakLambda(this, [this](int32 LocId)
		{
			const UGameInstance* GI = GetGameInstance();
			if (!GI || !AsdaEacLoc::IsSoft(LocId)) { return; }
			int32 ShowId = LocId;
			if (AsdaNotice::Loc(GI, ShowId).IsEmpty()) { ShowId = AsdaEacLoc::SoftFallbackNotice(LocId); }
			if (ShowId <= 0 || AsdaNotice::Loc(GI, ShowId).IsEmpty())
			{
				UE_LOG(LogAsdaNet, Warning, TEXT("[EAC] мягкое состояние loc %d: строки в таблице нет — только журнал"), LocId);
				return;
			}
			UE_LOG(LogAsdaNet, Log, TEXT("[EAC] мягкое состояние loc %d → системный чат (строка %d)"), LocId, ShowId);
			AsdaNotice::Chat(GI, EAsdaMsgCategory::Connection, ShowId);
		});
	}
	// ★ EAC ред. 3 (CONTRACT.md §8.1): рукопожатие AsdaSecure ИГРОВОГО соединения (GameServer/Indun; вход в мир, портал,
	//   переподключение — все идут через EnterGameWorld) → BIND сайдкару. Рукопожатие логина (под-сервер 1) не привязывается.
	//   Колбэк приходит из Conn->Tick — здесь только копия, передача — в Tick (закон no-reentrant-net-callbacks).
	Conn->SetOnSecureReady([this](uint8 SubServer, const uint8* Bind32)
	{
		if (SubServer != 2 || !Bind32) { return; }   // 2 = AsdaSecure::SubServer::Game (роль 0 в EnterGameWorld)
		FMemory::Memcpy(PendingEacBind, Bind32, sizeof(PendingEacBind));
		bPendingEacBind = true;
	});
}

void UAsdaNetSubsystem::Deinitialize()
{
	// ★ ВЫХОД ИЗ ИГРЫ (quit/stop-PIE) = ПРОСТОЙ disconnect. НЕ шлём 1028 — 1028 = PortalDisConnectUser
	//   (портал-логаут), он готовит ПОРТАЛ-спавн (spawn-type≠0) для следующего входа → CharMake(param_3==3)
	//   → блок движения. Обычное закрытие сокета → серверный DisConnectUser сохраняет позицию (0xE4D) +
	//   кулдауны (0x86A) БЕЗ портал-состояния (spawn-type=0). 1028 оставлен только для будущего меню
	//   «выход в char-select» из мира (штатный портал-логаут), не для завершения игры.
	if (Conn) { Conn->Disconnect(); Conn.Reset(); }
	Super::Deinitialize();
}

// ★ Путь к файлу, куда внешний агент кладёт текущий ключ ПРИЁМА XyT (32 байта в hex).
//   Меняется через консоль, чтобы не пересобирать клиент при переносе агента.
// ★ античит А2 (12.10.2026, мера 6): в Shipping ключа нет, а путь пуст — ключ сетевого шифра с диска НЕ читается
//   (весь режим XyT — отладочная интеграция с чужой сборкой, на логине он и так принудительно выключен). Путь дев-машины
//   в бинарь Shipping не попадает.
#if ASDA_DEV_CONSOLE
static TAutoConsoleVariable<FString> CVarXytRecvKeyFile(
	TEXT("asda.XyT.RecvKeyFile"),
	TEXT("D:/XYT_RE/frida/recvkey.txt"),
	TEXT("Файл с текущим ключом ПРИЁМА сборки XyT (32 байта hex). Пишется внешним агентом."));
#else
static TAsdaConstCVar<FString> CVarXytRecvKeyFile{FString()};   // Shipping: пустой путь = чтения нет
#endif

// ★ R5-fix D1 (13.11.2026) [ИСПРАВЛЕНИЕ ПОРТА] — разбор в AsdaXytKeyHex.h. 1 = не-hex символ в файле ключа приёма XyT отвергает
//   файл целиком (ключ не меняется, предупреждение в журнале); 0 = как было (не-hex символ читался как 0, мусор давал ключ).
static ASDA_DEV_CVAR(int32, CVarAsdaXytRecvKeyStrictHex,
	TEXT("asda.XyT.RecvKeyStrictHex"), 1,
	TEXT("[ИСПРАВЛЕНИЕ] 1 = файл ключа приёма XyT с не-hex символом отвергается (ключ не меняется); 0 = как прежде: не-hex символ "
	     "читается как 0 и даёт ключ с нулевыми полубайтами."),
	ECVF_Default);

bool AsdaXytKeyHex::IsStrict()
{
	return CVarAsdaXytRecvKeyStrictHex.GetValueOnAnyThread() != 0;
}

bool AsdaXytKeyHex::ParseWith(const FString& Hex, bool bStrict, uint8 OutKey[32], int32& OutBadPos)
{
	OutBadPos = -1;
	if (Hex.Len() < 64) { OutBadPos = Hex.Len(); return false; }
	for (int32 i = 0; i < 32; ++i)
	{
		int32 Hi, Lo;
		if (bStrict)
		{
			Hi = AsdaBytes::HexNibble(Hex[i * 2]);       // не-hex ⇒ −1
			Lo = AsdaBytes::HexNibble(Hex[i * 2 + 1]);
			if (Hi < 0 || Lo < 0) { OutBadPos = i * 2; return false; }
		}
		else
		{
			Hi = FParse::HexDigit(Hex[i * 2]);           // прежнее поведение: не-hex ⇒ 0, отказа нет
			Lo = FParse::HexDigit(Hex[i * 2 + 1]);
		}
		OutKey[i] = (uint8)((Hi << 4) | Lo);
	}
	return true;
}

bool AsdaXytKeyHex::Parse(const FString& Hex, uint8 OutKey[32], int32& OutBadPos)
{
	return ParseWith(Hex, IsStrict(), OutKey, OutBadPos);
}

// Читает файл ключа СЕЙЧАС, без оглядки на метку времени. Возвращает true, если ключ изменился.
bool UAsdaNetSubsystem::ReloadXytRecvKeyNow()
{
	const FString Path = CVarXytRecvKeyFile.GetValueOnGameThread();
	if (Path.IsEmpty()) { return false; }
	FString Hex;
	if (!FFileHelper::LoadFileToString(Hex, *Path)) { return false; }
	Hex.TrimStartAndEndInline();
	if (Hex.Len() < 64) { return false; }
	uint8 Key[32];
	// ★ R5-fix D1 (13.11.2026): разбор — общая `AsdaXytKeyHex::Parse` (не-hex символ = отказ под ключом asda.XyT.RecvKeyStrictHex).
	int32 BadPos = -1;
	//   Отказ здесь молчит, как и раньше: эту функцию зовёт спасение кадра на КАЖДЫЙ кадр с чужим заголовком, и предупреждение
	//   шло бы потоком, пока в файле мусор (ревизия R5-fix). О битом файле один раз на его изменение пишет опрос `TickXytRecvKey`.
	if (!AsdaXytKeyHex::Parse(Hex, Key, BadPos)) { return false; }
	// Отметим файл прочитанным, чтобы периодический опрос не переоткрывал его следом.
	IPlatformFile& PF = FPlatformFileManager::Get().GetPlatformFile();
	const FFileStatData Stat = PF.GetStatData(*Path);
	if (Stat.bIsValid) { XytKeyFileStamp = Stat.ModificationTime.GetTicks(); XytKeyFileSize = Stat.FileSize; }

	const uint32 Before = Asda::ChannelCipher::RecvKeyVersion();
	Asda::ChannelCipher::SetRecvKeyXyT(Key);
	return Asda::ChannelCipher::RecvKeyVersion() != Before;
}

// ★★★ Спасение кадра, расшифрованного УСТАРЕВШИМ ключом (гонка со сменой ключа у лаунчера).
//   Наблюдалось живьём: первый ответ игрового сервера пришёл за 46 мс ДО того, как новый ключ
//   доехал до нас, и был потерян — а повторно сервер его не шлёт.
//   XOR симметричен: (шифр ^ старый) ^ старый ^ новый == (шифр ^ новый). Значит уже разобранный
//   кадр правится на месте наложением (старый ^ новый) на те же позиции ключевого потока.
bool UAsdaNetSubsystem::TryRedecryptXytPacket(Asda::ParsedPacket& Pkt)
{
	uint8 Old[Asda::ChannelCipher::KEY_LEN];
	FMemory::Memcpy(Old, Asda::ChannelCipher::ActiveKey(/*bReceive=*/true), Asda::ChannelCipher::KEY_LEN);
	if (!ReloadXytRecvKeyNow()) { return false; }          // ключ не изменился — правит нечего
	const uint8* New = Asda::ChannelCipher::ActiveKey(/*bReceive=*/true);

	uint8 D[Asda::ChannelCipher::KEY_LEN];
	for (int32 i = 0; i < Asda::ChannelCipher::KEY_LEN; ++i) { D[i] = Old[i] ^ New[i]; }

	// Ровно те же границы, что и в ChannelCipher::Apply: шифруются позиции k=0..count-1 от frame[3].
	const int32 Size  = (int32)Pkt.Size;
	const int32 Count = ((Size - 4) >> 3) * 8;
	auto Delta = [&D](int32 k) -> uint8 { return D[k % Asda::ChannelCipher::KEY_LEN]; };

	// Заголовок: HdrA=frame[3..4] (k=0,1), HdrB=frame[5..6] (k=2,3), Opcode=frame[7..8] (k=4,5).
	if (Count > 1) { Pkt.HdrA   ^= (uint16)(Delta(0) | (Delta(1) << 8)); }
	if (Count > 3) { Pkt.HdrB   ^= (uint16)(Delta(2) | (Delta(3) << 8)); }
	if (Count > 5) { Pkt.Opcode ^= (uint16)(Delta(4) | (Delta(5) << 8)); }
	// Тело: body[i] лежит на frame[9+i], то есть k = 6+i.
	for (int32 i = 0; i < (int32)Pkt.Body.size(); ++i)
	{
		const int32 k = 6 + i;
		if (k >= Count) { break; }                          // хвост за границей шифра не трогаем
		Pkt.Body[i] ^= Delta(k);
	}
	UE_LOG(LogAsdaNet, Log, TEXT("[XyT] кадр пересчитан на новый ключ (версия %u) → опкод %u"),
		Asda::ChannelCipher::RecvKeyVersion(), (uint32)Pkt.Opcode);
	return true;
}

void UAsdaNetSubsystem::TickXytRecvKey(float DeltaSeconds)
{
	// Ключ живёт ~2 минуты, но смена происходит по воле сервера — опрашиваем часто и дёшево:
	//   читаем не файл, а его метку времени/размер, и только при изменении разбираем содержимое.
	XytKeyPollAccum += DeltaSeconds;
	if (XytKeyPollAccum < 0.25) { return; }
	XytKeyPollAccum = 0.0;

	const FString Path = CVarXytRecvKeyFile.GetValueOnGameThread();
	if (Path.IsEmpty()) { return; }
	IPlatformFile& PF = FPlatformFileManager::Get().GetPlatformFile();
	const FFileStatData Stat = PF.GetStatData(*Path);
	if (!Stat.bIsValid) { return; }
	const int64 Stamp = Stat.ModificationTime.GetTicks();
	const int64 Size  = Stat.FileSize;
	if (Stamp == XytKeyFileStamp && Size == XytKeyFileSize) { return; }   // не менялся
	XytKeyFileStamp = Stamp;
	XytKeyFileSize  = Size;

	FString Hex;
	if (!FFileHelper::LoadFileToString(Hex, *Path)) { return; }
	Hex.TrimStartAndEndInline();
	if (Hex.Len() < 64)
	{
		UE_LOG(LogAsdaNet, Warning, TEXT("[XyT] файл ключа приёма короче 64 hex-символов (%d) — пропуск"), Hex.Len());
		return;
	}
	uint8 Key[32];
	// ★ R5-fix D1 (13.11.2026): разбор — общая `AsdaXytKeyHex::Parse`; текст предупреждения прежний.
	int32 BadPos = -1;
	if (!AsdaXytKeyHex::Parse(Hex, Key, BadPos))
	{
		UE_LOG(LogAsdaNet, Warning, TEXT("[XyT] в файле ключа не hex на позиции %d — пропуск"), BadPos);
		return;
	}
	const uint32 Before = Asda::ChannelCipher::RecvKeyVersion();
	Asda::ChannelCipher::SetRecvKeyXyT(Key);
	if (Asda::ChannelCipher::RecvKeyVersion() != Before)
	{
		UE_LOG(LogAsdaNet, Log, TEXT("[XyT] ключ приёма обновлён (версия %u): %s"),
			Asda::ChannelCipher::RecvKeyVersion(), *Hex.Left(64));
	}
}

// ★ 26.10.2026 (ревизия wave_1025d 4.1) [ИСПРАВЛЕНИЕ ПОРТА]: страховка гейта перехода (30 с) считает от ИСХОДА подключения к
//   game-серверу, а не от 5055: неблокирующее подключение ждёт до 20 с (kGameConnectWaitSec), и 20 с SYN + загрузка карты снимали
//   гейт при живом соединении ДО портального 5058. Пока 5058 не ушёл и висит загрузочный экран, гейт держится до kTransferLoadCapSec.
//   0 — как 25.10 (30 с от взвода гейта).
static ASDA_DEV_CVAR(int32, CVarAsdaNetTransferGuardAfterConnect,
	TEXT("asda.Net.TransferGuardAfterConnect"), 1,
	TEXT("1 = страховка гейта перехода (30 с) не считает, пока идёт подключение к game-серверу, и при живом соединении держит гейт ")
	TEXT("до 150 с, пока идёт загрузка карты и портальный 5058 не отправлен; 0 = как прежде (30 с от начала перехода)."),
	ECVF_Default);
// Потолок страховки при живом соединении, пока грузится карта и 5058 ещё не ушёл, с (от исхода подключения). Дольше — гейт снимается,
//   чтобы клиент не онемел навсегда (загрузка, не закончившаяся за 2,5 мин, — уже поломка, а не медленный диск).
static constexpr float kTransferLoadCapSec = 150.f;

void UAsdaNetSubsystem::Tick(float DeltaTime)
{
	// ★ В режиме XyT ключ ПРИЁМА живёт своей жизнью — подхватываем его до разбора входящих кадров.
	if (bXytMode) { TickXytRecvKey(DeltaTime); }
	PollPending4028();   // ★ 15.10.2026: итог 4028, ушедшего с потока точной отправки
	// ★ EAC Э1: отложенный разрыв по KICK/нарушению EAC — вне колбэков. Код 6 — эталонная причина «отключён за нечестную
	//   игру» (нотис 1106: в мире — попап 8000 `ReasonToNoticeId(6)`, на логине — `BuildKickText(6, closed)`) — ЗАПАСНОЙ показ.
	//   ★ 16.10.2026: тексты EAC (991xxx) утверждены и внесены — показывается собранный текст EAC (BuildEacPlayerText).
	if (bPendingEacKick)
	{
		bPendingEacKick = false;
		if (Conn && Conn->IsConnected())
		{
			// ★ ред. 3: строка EAC (991xxx) уже утверждена и внесена в DT_GameText — в мире показываем её тем же попапом 8000
			//   (`ShowDisconnectPopup`), а табличный показ гасим кодом 4 (у него в таблице оригинала попапа нет: оригинал при
			//   неудаче портала так же показывает свой текст отдельно). Строки нет — нотис оригинала по коду причины.
			// ★ 16.10.2026: текст EAC (заголовок + причина) собран в OnFatal; на логине / выборе персонажа его берёт окно кика
			//   (AAsdaLoginGameMode::OnLoginKick → GetEacKickText) с прежним кодом причины — экран логина тот же.
			const bool bWorld = bInGameConnected;
			const bool bEacText = !PendingEacText.IsEmpty();
			LastKickCode = (bWorld && bEacText) ? 4 : PendingEacKickReason;
			EacKickText = PendingEacText;
			HandleConnectionClosed();   // сброс сессии; на логине — OnLoginKick(код) → текст и экран логина, в мире — попап 8000
			EacKickText.Reset();
			if (bWorld && bEacText)
			{
				if (UAsdaDisconnectPopupSubsystem* Pop = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaDisconnectPopupSubsystem>() : nullptr)
				{
					Pop->ShowDisconnectPopupText(PendingEacText);
				}
			}
			UE_LOG(LogAsdaNet, Warning, TEXT("[EAC] разрыв показан: %s (loc %d, запасной код %u) — «%s»"),
				bWorld ? TEXT("попап 8000") : TEXT("окно кика логина"), PendingEacLocId, (uint32)PendingEacKickReason,
				bEacText ? *PendingEacText.Replace(TEXT("\n"), TEXT(" / ")) : TEXT("строк 991xxx нет — нотис оригинала"));
			PendingEacText.Reset();
		}
		else
		{
			UE_LOG(LogAsdaNet, Warning, TEXT("[EAC] фатальное событие без живого соединения (loc %d, код %u) — разрывать нечего"),
				PendingEacLocId, (uint32)PendingEacKickReason);
			PendingEacText.Reset();
		}
	}
	// ★ ред. 3 (§8.1): привязка игрового соединения → BIND сайдкару (вне колбэка соединения).
	if (bPendingEacBind)
	{
		bPendingEacBind = false;
		if (UAsdaAntiCheatSubsystem* Eac = AsdaEacOf(this)) { Eac->NotifyGameChannelBound(/*ServerKind Game*/2, PendingEacBind); }
		FMemory::Memzero(PendingEacBind, sizeof(PendingEacBind));
	}
	if (Conn) Conn->Tick();
	// ★ ФАЗА A: вход в мир отложен на тик (реконнект login→game нельзя делать в колбэке пакета 1021 = реентрант).
	// ★ Отложенная загрузка уровня (запрошена из колбэка 6004) — вне разбора буфера.
	TickGmRestoreVisible(DeltaTime);   // ★ [ПОРТ] 18.10.2026: вернуть «виден» после входа/портала (см. объявление)
	if (bPendingMapLoad)
	{
		bPendingMapLoad = false;
		const int32 M = PendingMapLoadId;
		PendingMapLoadId = -1;
		UE_LOG(LogAsdaNet, Log, TEXT("[загрузка] отложенный BeginMapLoad(mapId=%d) выполняется вне разбора пакетов"), M);
		if (UGameInstance* GI = GetGameInstance())
			if (UAsdaLoadManagerSubsystem* LM = GI->GetSubsystem<UAsdaLoadManagerSubsystem>())
				LM->BeginMapLoad(M);
	}
	// ★ Страховка от вечной блокировки: если переход почему-то не завершился (сервер промолчал,
	//   логон не ушёл), гейт отправки снимается сам, иначе клиент онемел бы навсегда.
	if (bTransferInProgress)
	{
		// ★ 26.10.2026 (ревизия wave_1025d 4.1) [ИСПРАВЛЕНИЕ ПОРТА] (`asda.Net.TransferGuardAfterConnect`): подключение ждёт до
		//   kGameConnectWaitSec (20 с) — пока оно идёт, страховка не считает (её 30 с — от ИСХОДА подключения: удача → загрузка карты
		//   и 5058; неудача → `HandleGameConnectFailed` сам снимет гейт). И пока соединение живо, портальный 5058 ещё не ушёл, а
		//   загрузочный экран висит (5058 уйдёт по концу загрузки, AsdaLoadManagerSubsystem.cpp), гейт держится до kTransferLoadCapSec:
		//   иначе ход/пинг ушли бы до 5058, и сервер, не опознавший игрока на новом сокете, порвал бы связь.
		bool bHoldGuard = false;
		float GuardLimit = 30.f;
		if (CVarAsdaNetTransferGuardAfterConnect.GetValueOnGameThread() != 0)
		{
			const bool bConnecting = bAwaitGameConnect || (Conn && Conn->IsConnectPending());
			if (bConnecting) { TransferGuardSec = 0.f; bHoldGuard = true; }
			else if (bPortalPending && Conn && Conn->IsConnected())
			{
				const UAsdaLoadManagerSubsystem* LMg = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaLoadManagerSubsystem>() : nullptr;
				if (LMg && LMg->IsLoadingScreenVisible()) { GuardLimit = kTransferLoadCapSec; }
			}
		}
		if (!bHoldGuard) { TransferGuardSec += DeltaTime; }
		if (TransferGuardSec > GuardLimit)
		{
			bTransferInProgress = false;
			TransferGuardSec = 0.f;
			UE_LOG(LogAsdaNet, Warning,
				TEXT("[переход] не завершился за %.0f с после исхода подключения — гейт отправки снят принудительно"), GuardLimit);
			// ★ 25.10.2026 [ИСПРАВЛЕНИЕ ПОРТА]: соединения с game-сервером нет (и подключение не идёт) — переход умер, и
			//   портальный 5058 уже не нужен: иначе он ушёл бы при следующем входе вместо 4001. Живое соединение (идёт загрузка
			//   карты, 5058 ещё впереди) признак сохраняет.
			const bool bLinkAlive = Conn && (Conn->IsConnected() || Conn->IsConnectPending());
			if (!bLinkAlive && !bAwaitGameConnect) { ClearPortalTransferState(TEXT("страховка 30 с: соединения с game-сервером нет")); }
			else if (bPortalPending)
			{
				UE_LOG(LogAsdaNet, Log, TEXT("[переход] страховка 30 с: соединение живо — портальный вход 5058 остаётся (загрузка карты)"));
			}
		}
	}
	else { TransferGuardSec = 0.f; }

	if (bPendingEnterWorld)
	{
		bPendingEnterWorld = false;
		UE_LOG(LogAsdaNet, Warning, TEXT("[портал] реконнект выполняется: %s:%d (карта=%d)"),
			*GameServerIP, GameServerPort, EnterMapId);
		EnterGameWorld();
	}
	// ★★★ 2026-09-02 (К-4, часть «точка входа в мир»): ожидание подтверждения game-коннекта.
	//   Взводится только тогда, когда `Conn->Connect` вернул «начато», а сокет ещё не соединён —
	//   то есть при НЕБЛОКИРУЮЩЕМ коннекте. При нынешнем блокирующем сюда не попадаем вовсе.
	//   Как только соединение подтвердилось — уходит отложенный enter-хендшейк 20000 (фаза A3).
	if (bAwaitGameConnect)
	{
		if (Conn && Conn->IsConnected())
		{
			bAwaitGameConnect   = false;
			AwaitGameConnectSec = 0.f;
			UE_LOG(LogAsdaNet, Log, TEXT("EnterGameWorld: соединение с game-сервером подтверждено — отложенный хендшейк 20000"));
			FAsdaOutgoingGate::OnConnectionAppeared(this, TEXT("game-сервер, подтверждение"));   // ★★ МЕЛК-2 (10.10.2026)
			SendEnterWorldHandshake();
		}
		else
		{
			AwaitGameConnectSec += DeltaTime;
			// Предел ожидания = таймаут SYN у ОС: столько же висел прежний БЛОКИРУЮЩИЙ Connect,
			//   прежде чем вернуть FAIL. Дольше ждать нечего — дальше это уже «сервер недоступен».
			if (AwaitGameConnectSec > kGameConnectWaitSec)
			{
				bAwaitGameConnect   = false;
				AwaitGameConnectSec = 0.f;
				// Тот же откат, что и при немедленном отказе Connect (эталон: нотис loc 1001 → char-select).
				UE_LOG(LogAsdaNet, Warning,
					TEXT("EnterGameWorld: game-сервер не ответил на коннект за %.0f с → откат в char-select (нотис loc1001)"),
					kGameConnectWaitSec);
				// ★ 25.10.2026: тот же разрыв с нотисом 1001, что и при отказе ОС (при ключе asda.Net.GameConnectFailDisconnect 0 —
				//   прежнее «State = CharSelect» внутри). Флаг ожидания уже снят выше — повторного входа из OnClosed не будет.
				HandleGameConnectFailed(TEXT("срок ожидания подключения истёк"));
			}
		}
	}
	// ★★★ Keepalive ~5с С МОМЕНТА КОННЕКТА к login-серверу (а НЕ с момента логина — это и был баг
	//   «повисел на экране логина → Соединение отклонено сервером»). Серверная сторона (LoginManager
	//   CUserProc::Check_DisconnectUser sub_406560 по WM_TIMER id=2, период SetTimer 0x1F40 = 8000 мс):
	//   каждый тик Plus_PingCount() для КАЖДОГО слота, у которого m_iUserNum>=0 (ставится на TCP-accept
	//   в SetEmpty_ListenArray, ДО всякого логина); при PingCount>5 → 4101 code=1 + Sleep(200) + разрыв.
	//   Итого кик наступает на 7-м тике ≈ 48…56 с молчания (живой лог: коннект 19:38:31.7 → кик 19:39:25.3,
	//   53.6 с). Сбрасывает счётчик ТОЛЬКО опкод 1000 (Recv_ClientPingMessage sub_406500 → Init_PingCount).
	//   ★ 14.10.2026 (античит, блок Б): период — как у оригинала, > 8 с (раньше 5 с логин / 10 с игра). Логин-серверу
	//   хватает: его таймер 8000 мс и кик при PingCount > 5 ⇒ до кика ≈ 48 с молчания, а пинг идёт каждые ~8 с.
	// ★ ФАЗА B: game-keepalive (heartbeat 1000→1001). Сервер GS `sub_5271F0`: пинг раньше 6000 мс после прошлого →
	//   Log_SpeedHack «Warning», каждый 4-й — «HackCheck» (кика нет). Это и есть серверный детектор ускорения часов:
	//   у оригинала своего клиентского детектора нет (GameGuard в установке мёртв, clock_re.md §3).
	// Ритм — `NetHeartbeatThread@0x95C330`: стенные часы и модуль разности (см. объявление NetHeartbeatDue в .h).
	//   У порта логин- и игровой сокет не живут одновременно (вход в мир закрывает логин-сокет) — один вызов на активный.
	if (StepNetHeartbeat(FPlatformTime::Seconds(), Conn && Conn->IsConnected(), bLoginConnActive, bInGameConnected, LastKickCode))
	{
		SendKeepAlive();
	}
}

bool UAsdaNetSubsystem::StepNetHeartbeat(double NowSec, bool bSocketConnected, bool bLoginSession, bool bGameSession, uint8 KickCode)
{
	if (!NetHeartbeatGateOpen(bSocketConnected, bLoginSession, bGameSession, KickCode)) { return false; }
	if (LastNetTickStampSec <= 0.0)
	{
		LastNetTickStampSec = NowSec;   // первая проверка сессии без явного сброса — отсчёт с этого момента
		return false;
	}
	if (!NetHeartbeatDue(NowSec, LastNetTickStampSec)) { return false; }
	LastNetTickStampSec = NowSec;       // эталон: метка = timeGetTime() сразу после посылки
	return true;
}

bool UAsdaNetSubsystem::NetHeartbeatDue(double NowSec, double StampSec)
{
	return FMath::Abs(NowSec - StampSec) > kNetHeartbeatPeriodSec;
}

bool UAsdaNetSubsystem::NetHeartbeatGateOpen(bool bSocketConnected, bool bLoginSession, bool bGameSession, uint8 KickCode)
{
	return bSocketConnected && (bLoginSession || bGameSession) && KickCode == 0xFF;
}

void UAsdaNetSubsystem::ResetNetHeartbeatStamp()
{
	LastNetTickStampSec = FPlatformTime::Seconds();
}

void UAsdaNetSubsystem::SendKeepAlive()
{
	// Эталон Net_SendKeepAlivePing@0x95C240: тело = MD5("") (16б), опкод 1000, БЕЗ анти-реплей-secret.
	//   Сервер sub_523F00(a4=0): хеширует пустой payload (=MD5("")) и memcmp с телом → всегда совпадает.
	if (!Conn || !Conn->IsConnected()) return;
	uint8 Body[16]; FMD5 Md5; Md5.Final(Body);   // MD5 пустого
	const bool bOk = Conn->SendPacket((uint16)OP_LOGIN_KEEPALIVE_PING, Body, 16);
	if (!bOk) { UE_LOG(LogAsdaNet, Warning, TEXT("KeepAlive: SendPacket FAIL (соединение мертво?)")); }
}

bool UAsdaNetSubsystem::IsNoNetRun()
{
	return FAsdaConnection::IsNoNetRun();
}

bool UAsdaNetSubsystem::ConnectToLogin(const FString& Host, int32 Port)
{
	if (!Conn) return false;
	// ★ Р-2 (10.10.2026): прогон без сети — ни попытки, ни адреса в журнале (по логу прогона подключений нет).
	if (IsNoNetRun())
	{
		UE_LOG(LogAsdaNet, Warning, TEXT("ConnectToLogin: ключ -AsdaNoNet — подключение к логин-серверу не выполняется"));
		State = EAsdaNetState::Disconnected;
		return false;
	}
	State = EAsdaNetState::ConnectingLogin;
	ObfCounter = 0;   // ★ новая сессия → анти-реплей-счётчик с нуля (сервер ждёт монотонный ряд от 0)
	bLoggedIn = false; ResetNetHeartbeatStamp();   // эталон: метка пинга от момента подключения
	bLoginConnActive = false;
	SendFailStreak = 0;
	LastKickCode = 0xFF;                      // 0xFF = «обрыв без 4101» (эталон g_nDisconnectReason=255)
	LastDisconnectReason = 0xFF;               // новая сессия → прошлая причина разрыва неактуальна
	RequestedUserInfo.Reset();                 // новая сессия → запросы 5416 можно слать заново
	bEacReturnToCharSelectSent = false;   // ★ EAC R3-01: новое соединение с LoginManager — его обрыв уже настоящий разрыв
	// ★ EAC Э1: сайдкар античита слушает на ХОСТЕ LoginManager (порт SidecarPort) — запомнить хост до коннекта.
	if (UAsdaAntiCheatSubsystem* Eac = AsdaEacOf(this)) { Eac->NotifyLoginHost(Host); }
	const bool bOk = Conn->Connect(Host, Port, /*SecureRole=*/1);   // ★ В7d: роль для рукопожатия шифрования — логин (порт не угадываем)
	UE_LOG(LogAsdaNet, Log, TEXT("ConnectToLogin %s:%d -> %s"), *Host, Port, bOk ? TEXT("OK") : TEXT("FAIL"));
	if (!bOk) State = EAsdaNetState::Disconnected;
	// ★ keepalive (1000) идёт С ЭТОГО МОМЕНТА — сервер считает пинги от TCP-accept, а не от логина.
	else      bLoginConnActive = true;
	// ★★ МЕЛК-2 (10.10.2026): соединение появилось — идущий офлайн-самотест прерывается (итог, `quit`, вето отправок).
	if (bOk) { FAsdaOutgoingGate::OnConnectionAppeared(this, TEXT("логин-сервер")); }
	return bOk;
}

bool UAsdaNetSubsystem::SendAgainUserInfo(uint16 UserIdx)
{
	if (!Conn || !Conn->IsConnected()) return false;
	if (FAsdaOutgoingGate::Block(this, (uint16)OP_SEND_AGAIN_USER_INFO, EAsdaGate::TransferWide)) { return false; }   // E1 шаг 2: гейт перехода (asda.Net.TransferGateAllChannels)
	// 26б: [0..7]=анти-реплей секрет, [8..23]=MD5(payload,2) (эталон солит g_NetMd5Salt; как и в 4054,
	//   сервер этого билда принимает несолёный — при kick сюда первым делом смотреть соль), [24..25]=userIdx.
	uint8 Body[26];
	FMemory::Memzero(Body, sizeof(Body));
	AsdaBytes::WrI16(&Body[24], (int32)UserIdx);
	{ FMD5 Md5; uint8 Dig[16]; Md5.Update(Body + 24, 2); Md5.Final(Dig); FMemory::Memcpy(Body + 8, Dig, 16); }
	FillAntiReplaySecret(Body);
	const bool bOk = ConnSendSecret((uint16)OP_SEND_AGAIN_USER_INFO, Body, sizeof(Body));
	UE_LOG(LogAsdaNet, Log, TEXT("AgainUserInfo 5416: запрос снапшота userIdx=%u -> %s"), UserIdx, bOk ? TEXT("OK") : TEXT("FAIL"));
	return TrackSend(bOk);   // ★ B19 (аудит №4): игровой сокет — отправка идёт в счётчик обрывов, 5416 (запрос снапшота)
}

// ★ Отладочная подача пакета: собираем кадр и ведём его ЧЕРЕЗ ШТАТНЫЙ разбор — консольные
//   команды не повторяют логику приёмника (второго источника истины не появляется).
void UAsdaNetSubsystem::DebugFeedPacket(int32 Opcode, const TArray<uint8>& Body, uint16 HdrA)
{
	Asda::ParsedPacket Pkt;
	Pkt.Opcode = (uint16)Opcode;
	Pkt.HdrA = HdrA;
	Pkt.Body.assign(Body.GetData(), Body.GetData() + Body.Num());
	Pkt.Size = (uint16)(Body.Num() + 10);
	UE_LOG(LogAsdaNet, Log, TEXT("[отладка] подан пакет %d, тело %d б"), Opcode, Body.Num());
	HandlePacket(Pkt);
}

// ★ Дев-ключ `asda.Quest.ApplyJobChange` (в Shipping — константа 1).
bool UAsdaNetSubsystem::IsQuestJobChangeApplyOn()
{
	return CVarQuestApplyJobChange.GetValueOnGameThread() != 0;
}

// ★★★ 18.10.2026 — БЛОК ПЕРСОНАЖА ТЕЛА 5042 (`net_ApplyPlayerFullUpdate@0xD828A0`, только `case 0`).
//   Раскладка — тело пакета = кадр с +9 (`PacketFrame::HEADER`), эталон `memcpy(savedThis + 60364, packet + 9, 0x1F5)`,
//   значит body[i] = `savedThis + 60364 + i`:
//     body[10] `+60374` — профессия (`gap_43B[10]` = `SelfJob`), ненулевая ⇒ `jobChangeFlag = 1`;
//     body[11] `+60375` — класс (`gap_438[0]` = `SelfClassId`); body[12] `+60376` — подкласс (`gap_438[1]` = `SelfSubJob`);
//     body[13..14] `+60377` i16 — свободные очки умений (`gap_43B[8]` = `SelfSkillPoints`).
//   Ветка 1 (`jobChangeFlag > 0`, network.c:21835-21853): все четыре поля + `skillUiUpdatePointText` (у нас `OnSelfSkillPoints`)
//     и всё, что эталон обновляет дальше по классу, — подписчики `OnSelfJobChanged`.
//   Ветка 2 (`updateFlags[4]`: `!body[10] && !body[11] && !body[12] && i16 body[13] > 0`, network.c:21825-21826, :22607-22611): только
//     очки `gap_43B[8] = body[13]`; строку 6024 в чат пишет диалог NPC (`chatBuildTransactionNotice` вид 5, после строк квеста).
//     ⚠ `skillUiUpdatePointText` эталон в этой ветке НЕ зовёт (счётчик окна обновится при следующей перерисовке); у нас окно
//     навыков держит свою копию очков, поэтому рассылаем `OnSelfSkillPoints` и здесь — [ПОРТ], иначе счётчик врал бы.
void UAsdaNetSubsystem::ApplyQuestTurnInCharBlock(const std::vector<uint8>& B)
{
	if (B.size() < 15) { return; }
	const int32 Job = (int32)B[10];
	const int32 Cls = (int32)B[11];
	const int32 Sub = (int32)B[12];
	const int32 Pts = AsdaBytes::RdI16(B.data() + 13);
	if (Job != 0)
	{
		const int32 OldClass = SelfClassId;
		SelfJob         = Job;
		SelfClassId     = Cls;
		SelfSubJob      = Sub;
		SelfSkillPoints = Pts;
		UE_LOG(LogAsdaNet, Log, TEXT("←5042 смена профессии: профессия %d, класс %d→%d, подкласс %d, очки умений %d"),
			Job, OldClass, Cls, Sub, Pts);
		OnSelfSkillPoints.Broadcast(SelfSkillPoints);
		OnSelfJobChanged.Broadcast(OldClass, Job, Cls, Sub);
		return;
	}
	if (Cls == 0 && Sub == 0 && Pts > 0)
	{
		SelfSkillPoints = Pts;
		UE_LOG(LogAsdaNet, Log, TEXT("←5042 очки умений: %d (ветка updateFlags[4], профессия не менялась)"), Pts);
		OnSelfSkillPoints.Broadcast(SelfSkillPoints);
	}
}

// ★ 19.10.2026 — обёртка для 5052 (объявление и эталон в .h): тот же разбор блока, что у 5042, с пометкой источника.
void UAsdaNetSubsystem::ApplyCompletionCharBlock5052(const TArray<uint8>& StatusBlock501)
{
	const std::vector<uint8> V(StatusBlock501.GetData(), StatusBlock501.GetData() + StatusBlock501.Num());
	bSelfJobChangeFrom5052 = true;
	ApplyQuestTurnInCharBlock(V);
	bSelfJobChangeFrom5052 = false;
}

// ★ R4, доля a (14.11.2026; запись 11 от 15.11.2026 — прежний путь и ключ убраны) [ПОРТ, производительность]: hex тела пакета
//   для строки журнала `RECV op=…`. Прежний код на КАЖДЫЙ входящий пакет делал до 72 `FString::Printf("%02X ")` и столько же
//   дозаписей в растущую строку. Теперь те же знаки (две заглавные шестнадцатеричные цифры и пробел на байт, до 72 байтов —
//   чтобы 6153, 25 + 40 байт дизайна клан-флага, влезал целиком) пишутся в строитель на стеке по таблице цифр. Текст строки
//   журнала побайтно тот же; уровень и состав журнала не меняются (журнал ритейла — решение владельца).
static void AsdaRecvHex(const uint8* Body, int32 Num, FStringBuilderBase& Hex)
{
	ASDA_PERF_SCOPE("Net.RecvHex");
	static const TCHAR kDigits[] = TEXT("0123456789ABCDEF");   // заглавные цифры — как у `%02X`
	const int32 N = FMath::Min(Num, 72);
	for (int32 i = 0; i < N; ++i)
	{
		const uint8 B = Body[i];
		Hex.AppendChar(kDigits[B >> 4]);
		Hex.AppendChar(kDigits[B & 15]);
		Hex.AppendChar(TEXT(' '));
	}
}

void UAsdaNetSubsystem::HandlePacket(const Asda::ParsedPacket& PktIn)
{
	ASDA_PERF_SCOPE("Net.HandlePacket");   // R4, доля a: весь приём одного пакета (в Shipping пусто)
	// ★ XyT: у корректных пакетов ИХ сервера hdrA и hdrB всегда 0xFFFF. Если это не так — кадр
	//   расшифрован устаревшим ключом (лаунчер сменил его быстрее, чем ключ доехал до нас).
	//   Перечитываем ключ и правим кадр на месте, иначе пакет был бы потерян безвозвратно.
	Asda::ParsedPacket PktFixed;
	const Asda::ParsedPacket* PktPtr = &PktIn;
	if (bXytMode && (PktIn.HdrA != 0xFFFF || PktIn.HdrB != 0xFFFF))
	{
		PktFixed = PktIn;
		if (TryRedecryptXytPacket(PktFixed)) { PktPtr = &PktFixed; }
	}
	const Asda::ParsedPacket& Pkt = *PktPtr;
	{
		// 72 байта: чтобы 6153 (25 + 40 байт дизайна клан-флага) влезал целиком (диагностика плашек).
		// ★ R4, доля a (14.11.2026): сборка hex — `AsdaRecvHex` в строитель на стеке (72 × 3 знака помещаются без кучи).
		TStringBuilder<256> HexBuf;
		AsdaRecvHex(Pkt.Body.data(), (int32)Pkt.Body.size(), HexBuf);
		const TCHAR* Hex = HexBuf.ToString();
		UE_LOG(LogAsdaNet, Log, TEXT("RECV op=%u (0x%04X) size=%u hdrA=%u hdrB=%u bodyLen=%d body=[%s]"),
			Pkt.Opcode, Pkt.Opcode, Pkt.Size, Pkt.HdrA, Pkt.HdrB, (int32)Pkt.Body.size(), Hex);
	}

	// ★ R2 рефакторинга (10.11.2026, решение координатора; карта `Reports/refactor_2026-11/splitmaps/AsdaNetSubsystem.md` §5):
	//   разбор опкодов разнесён ДОСЛОВНО по семи подфункциям-областям (`AsdaNetSubsystem_Recv<Область>.cpp`). Подфункция ставит
	//   `bHandled`, если опкод её; метки областей не пересекаются, поэтому порядок вызовов на итог не влияет. Здесь остаётся только
	//   исходная ветка `default:` — опкоды без обработчика.
	bool bHandled = false;   // опкод разобран одной из областей
	HandlePacket_Session(Pkt, bHandled);      if (bHandled) { return; }
	HandlePacket_Items(Pkt, bHandled);        if (bHandled) { return; }
	HandlePacket_World(Pkt, bHandled);        if (bHandled) { return; }
	HandlePacket_Social(Pkt, bHandled);       if (bHandled) { return; }
	HandlePacket_Misc(Pkt, bHandled);         if (bHandled) { return; }
	HandlePacket_CombatState(Pkt, bHandled);  if (bHandled) { return; }
	HandlePacket_Chat(Pkt, bHandled);         if (bHandled) { return; }

#if defined(_MSC_VER) && !defined(__clang__)
#pragma warning(push)
#pragma warning(disable: 4065)   // ★ R2: «switch с default без case» — здесь так и задумано (case разнесены по HandlePacket_*)
#endif
	switch ((NetOpcode)Pkt.Opcode)
	{
	default:
	{
		// ★ Волна В3 рыбалки (08.10.2026): 6175 (0 б) — «конец списка томов книги» (сервер `sub_4115D0` GS:7616-7641 шлёт
		//   его после всех 6174). У оригинала обработчика НЕТ (в диспетчере нет `case 0x181F`, в `netopcode_enum.h` опкода нет —
		//   client_net.md §2.12), поэтому и в перечислении опкодов порта его нет. [ОРИГИНАЛ] Порт ничего не делает — только
		//   журнал (подписчик — книга рыбака, тоже только журнал).
		if (Pkt.Opcode == 6175)
		{
			UE_LOG(LogAsdaNet, Log, TEXT("6175 конец списка томов книги рыбака (%d б): у оригинала обработчика нет — только журнал"),
				(int32)Pkt.Body.size());
			OnFishBookListEnd6175.Broadcast(NetBodyToArray(Pkt.Body));
			break;
		}
		// Не реализованный опкод: аудит на Log-уровне С ДЕДУПОМ (по одному разу за сессию на опкод) —
		//   ищем, в частности, источник campId/factionGrade чужих игроков (полоса лагеря на плашке).
		static TSet<uint16> AuditSeen;
		if (!AuditSeen.Contains(Pkt.Opcode))
		{
			AuditSeen.Add(Pkt.Opcode);
			UE_LOG(LogAsdaNet, Log, TEXT("RECV op=%u (%d б): хендлера нет (аудит, 1-й раз)"), Pkt.Opcode, (int32)Pkt.Body.size());
		}
		else
		{
			UE_LOG(LogAsdaNet, Verbose, TEXT("RECV op=%u (%d б): хендлера нет"), Pkt.Opcode, (int32)Pkt.Body.size());
		}
		break;
	}
	}
#if defined(_MSC_VER) && !defined(__clang__)
#pragma warning(pop)
#endif
}
