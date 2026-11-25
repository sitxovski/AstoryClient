// Copyright (c) 2026 @sitxovski (Discord)
// LogUpload.h — ★ 19.10.2026 выгрузка запечатанных журналов прошлых игровых сессий на наш сервер (решение владельца 19.10).
// Протокол: Reports/release_logs_2026-10-19/00_SPEC.md (части A↔C и C↔B, «★ Дополнение»: конверт версии 2 с логином).
//
// Что делает один проход (фоновый поток лаунчера; игроку ничего не показывается, запуск игры не ждёт):
//   1) игра запущена (AsdaReborn-Win64-Shipping.exe / Astory.exe) — ничего не трогать;
//   2) %LOCALAPPDATA%\AsdaReborn\Saved: Logs\Sessions\*.alog (вид 1), Logs\AsdaEac*.alog (вид 2), Crashes\*.acrash (вид 3);
//      незагруженные старше 14 дней — удалить без выгрузки (вместе с .alog.meta);
//   3) файлы — от самых свежих: каждый открывается без общего доступа на запись (файл, который кто-то пишет, — пропуск),
//      структура AsdaLogSeal проверяется по правилам приёмника (метка/версия/вид/резервы/ClientHello/точка P-256/длины и
//      seq кадров) и keyId = вшитый текущий ключ журналов (Config.h kLogSealKeyIds); берётся самый длинный корректный
//      префикс ≤ 4 МиБ (обрезанный падением хвост отбрасывается — его всё равно не расшифровать); файл без единого
//      корректного кадра или со старым ключом — сразу в Uploaded\rejected\ без отправки;
//   4) логин учётки: Sessions\<имя>.alog.meta (JSON "account"); для EAC и падений — meta сессии того же pid (падение)
//      или ближайшей по времени сессии; нет — пустой; ★ 31.10.2026: части журнала сессии «<время>_<pid>_part<N>.alog» (ротация
//      клиента) и повтор имени «_2» — та же сессия и тот же pid (SessionPidFromName); каждая часть — свой файл и свой запрос;
//   5) ★ (координатор 19.10, после выкладки B) ОДИН файл на запрос: POST https://@@ASTORY_AUTH_HOST@@/api/v1/ingest, конверт
//      "ASLU" v2 с N = 1, X-Asda-Install (случайный 32-hex, один раз на ПК), запрос ≤ 20 с. Итог — по каждому файлу:
//      204 → <папка>\Uploaded\ (хранятся 10); 400/413 → <папка>\Uploaded\rejected\ (10) и дальше следующий файл;
//      429/5xx/сеть/прочее → файл остаётся, проход прекращается (повтор при следующем запуске лаунчера).
//      Не больше 4 запросов за запуск и 6 за скользящий час (лимит приёмника на IP и id установки) — время отправок
//      хранится рядом с install.id (upload_sent.txt);
//   6) перенос — через дескриптор, открытый при чтении проверяется тот же файл (том+индекс+размер+время): если игра
//      успела ротировать журнал под тем же именем, чужой файл не переносится.
#pragma once
#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace asda {

// Итог прохода выгрузки
enum class LogUploadResult {
    Nothing,       // выгружать нечего (запроса не было)
    Uploaded,      // хотя бы один файл принят (204), повторов нет
    Rejected,      // только отказы (сервера 400/413 или локальные) — файлы в Uploaded\rejected (не повторяются)
    Retry,         // проход прерван: 429/5xx/сеть/таймаут/иной код — файл остался до следующего запуска
    Throttled,     // свой лимит запросов за час исчерпан — ничего не отправлено (повтор при следующем запуске)
    GameRunning,   // игра запущена — ничего не трогали
    Stopped,       // остановлено (закрытие лаунчера)
};

// Имя итога для журнала и тестов
const char* LogUploadResultName(LogUploadResult r);

#if defined(ASDA_LAUNCHER_TEST_BUILD)
// ★ 31.10.2026 (только тестовая сборка): самопроверка разбора имён журналов — pid сессии у частей «_partN» и «_2», pid падения,
// привязка падения к сессии по pid. out — строки ok/FAIL. Возврат: число провалов.
int LogUploadNameSelfTest(const std::function<void(const std::string&)>& out);
#endif

// Счётчики прохода (итог по каждому файлу)
struct LogUploadStats {
    LogUploadResult result = LogUploadResult::Nothing;   // общий итог (см. LogUploadResult)
    int requests = 0;        // отправлено запросов
    int uploaded = 0;        // принято (204)
    int rejected = 0;        // отклонено сервером (400/413)
    int rejectedLocal = 0;   // отклонено без отправки (не контейнер, старый ключ)
    int retry = 0;           // файл оставлен на повтор (429/5xx/сеть/таймаут/иной код)
    int expired = 0;         // удалено как старше 14 дней
    bool throttled = false;  // упёрлись в свой лимит запросов за час
};

// Параметры прохода
struct LogUploadOptions {
    std::wstring savedDir;                         // папка Saved игры (%LOCALAPPDATA%\AsdaReborn\Saved)
    std::wstring installIdFile;                    // файл id установки (%LOCALAPPDATA%\AStoryRemake\install.id); рядом —
                                                   // upload_sent.txt (время отправок для лимита за час)
    std::function<void(const std::string&)> log;   // журнал лаунчера (может быть пустым)
#if defined(ASDA_LAUNCHER_TEST_BUILD)
    std::string endpoint;   // только тестовая сборка: иной приёмник (https://@@ASTORY_AUTH_HOST@@/… или http://127.0.0.1:…/…)
    int maxPerHour = -1;    // только тестовая сборка: свой лимит запросов за час (<0 — как в боевой)
    std::vector<std::string> pinsOverride;   // только тестовая сборка: свои SPKI корня (hex) — проверка отказа пиннинга
#endif
};

// Параметры по умолчанию (папки игрока); log — журнал лаунчера
LogUploadOptions DefaultLogUploadOptions(std::function<void(const std::string&)> log);

// Выгрузчик: один проход в своём потоке
class LogUploader {
public:
    LogUploader() = default;
    ~LogUploader();
    LogUploader(const LogUploader&) = delete;
    LogUploader& operator=(const LogUploader&) = delete;

    // Запустить проход в фоновом потоке (повторный вызов при живом потоке — без действия)
    void Start(LogUploadOptions o);
    // Выполнить проход в текущем потоке (тесты); возврат — счётчики
    LogUploadStats RunNow(const LogUploadOptions& o);
    // Остановить: новых шагов не начинать, идущий HTTP-запрос прервать закрытием его дескриптора
    void RequestStop();
    // Поток завершился (join не заблокирует)
    bool Done() const { return done_; }
    // Дождаться потока, если он завершился
    void JoinIfDone();

private:
    // Один проход (см. шапку); вызывается из потока Start или из RunNow
    LogUploadStats Pass(const LogUploadOptions& o);
    // POST тела на приёмник; true — ответ получен (status заполнен), false — сеть/таймаут/остановка (why — причина)
    bool Post(const LogUploadOptions& o, const std::string& body, const std::string& installId, unsigned long& status,
              std::string& why);
    // Закрыть идущий запрос под замком (сторож таймаута и RequestStop)
    void AbortRequest();
    // Открыть сессию и соединение WinHTTP к приёмнику (один раз на проход); false — why
    bool Connect(const LogUploadOptions& o, std::string& why);
    // Закрыть сессию и соединение
    void Disconnect();

    std::thread thread_;                          // поток прохода
    std::atomic<bool> stop_{false}, done_{true};  // остановка запрошена / поток завершился
    std::mutex m_;                                // защищает req_ и cv_
    std::condition_variable cv_;                  // сторож таймаута и остановка
    void* req_ = nullptr;                         // WinHTTP: идущий запрос
    void* session_ = nullptr;                     // WinHTTP: сессия прохода (без прокси)
    void* connect_ = nullptr;                     // WinHTTP: соединение с приёмником (keep-alive на весь проход)
    std::wstring host_, path_;                    // узел и путь приёмника
    bool secure_ = true;                          // https (в тестовой сборке возможен http://127.0.0.1)
};

}  // namespace asda
