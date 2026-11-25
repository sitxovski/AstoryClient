// Copyright (c) 2026 @sitxovski (Discord)
// Ping.h — фоновый опрос состояния игрового сервера и пинга для окна лаунчера (★ 17.10.2026).
//
// Бережно к серверу и сети:
//   • к сокетам LoginManager/GameServer не подключаемся вовсе — спрашиваем GET /status у AsdaId (HTTPS через Caddy),
//     а он отвечает из кэша (2 с) по таблице слушающих портов Windows; любое число лаунчеров = одна проверка за 2 с;
//   • одно HTTPS-соединение на весь сеанс (keep-alive WinHTTP): без нового TCP/TLS на каждый опрос;
//   • окно свёрнуто — опрос на паузе; закрытие окна прерывает идущий запрос сразу.
#pragma once
#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace asda {

// Состояние сервера для отрисовки
enum class ServerState { Checking, Online, Offline };

// Результат одного опроса
struct ServerStatus {
    ServerState state = ServerState::Checking;
    int pingMs = -1;   // время запроса по уже открытому соединению, мс; <0 — нет замера
};

// Опрос /status в своём потоке
class ServerProbe {
public:
    // update — вызывается из потока опроса при каждом результате; log — строки журнала (смена состояния)
    ServerProbe(std::function<void(const ServerStatus&)> update, std::function<void(const std::string&)> log);
    ~ServerProbe();
    ServerProbe(const ServerProbe&) = delete;
    ServerProbe& operator=(const ServerProbe&) = delete;

    // Запустить поток опроса
    void Start();
    // Пауза (окно свёрнуто) / продолжение (с немедленным опросом)
    void SetPaused(bool paused);
    // Попросить остановиться: идущий запрос прерывается закрытием его дескриптора
    void RequestStop();
    // Поток завершился (join не заблокирует)
    bool Done() const { return done_; }
    // Дождаться потока, если он завершился
    void JoinIfDone();

private:
    void Loop();
    // Один запрос: true — ответ получен (code/online/ms заполнены), false — сеть/таймаут/отмена
    bool Query(int& code, bool& online, int& ms);
    // Пауза между опросами; false — пора остановиться
    bool Wait(unsigned ms);

    std::function<void(const ServerStatus&)> update_;   // приёмник результата
    std::function<void(const std::string&)> log_;       // журнал
    std::thread thread_;                                 // поток опроса
    std::atomic<bool> stop_{false}, paused_{false}, done_{true};
    std::mutex m_;                                       // защищает cv_ и req_
    std::condition_variable cv_;                         // пробуждение при паузе/остановке
    void* session_ = nullptr;                            // WinHTTP: сессия (живёт весь сеанс, держит keep-alive)
    void* connect_ = nullptr;                            // WinHTTP: соединение с узлом
    void* req_ = nullptr;                                // WinHTTP: идущий запрос (закрывается при остановке)
};

}  // namespace asda
