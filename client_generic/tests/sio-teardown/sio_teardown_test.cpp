// Tears down live wss:// Socket.IO connections while a read is pending, to
// catch use-after-free in sio_beast.cpp's stream teardown (Bugsnag crash in
// bio_write_intern, 0.17.2). Build with ASan; see run.sh.
//
//   sio_teardown_test <port> <scenario> <iterations> [flood]
//   scenario: close | reconnect | watchdog
//   flood: the server streams large events nonstop, so reads are in flight

#include "sio_client.h"
#include "sio_message.h"
#include "sio_socket.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <thread>

using namespace std::chrono_literals;

namespace
{
std::mutex g_mutex;
std::condition_variable g_cv;
int g_opens = 0;
int g_closes = 0;

template <class Pred> bool wait_for(Pred pred, std::chrono::milliseconds timeout)
{
    std::unique_lock<std::mutex> lock(g_mutex);
    return g_cv.wait_for(lock, timeout, pred);
}
} // namespace

int main(int argc, char** argv)
{
    if (argc != 4 && argc != 5)
    {
        std::fprintf(stderr, "usage: %s <port> close|reconnect|watchdog <iterations> [flood]\n", argv[0]);
        return 2;
    }
    std::string const uri = std::string("wss://localhost:") + argv[1] + "/socket.io/";
    std::string const scenario = argv[2];
    int const iterations = std::atoi(argv[3]);
    bool const flood = argc == 5 && std::strcmp(argv[4], "flood") == 0;
    auto query = [&](std::map<std::string, std::string> q) {
        if (flood)
            q["flood"] = "1";
        return q;
    };

    sio::client client;
    client.set_reconnect_delay(10);
    client.set_reconnect_delay_max(10);
    client.set_open_listener([] {
        std::lock_guard<std::mutex> lock(g_mutex);
        ++g_opens;
        g_cv.notify_all();
    });
    client.set_close_listener([](sio::client::close_reason const&) {
        std::lock_guard<std::mutex> lock(g_mutex);
        ++g_closes;
        g_cv.notify_all();
    });
    auto sock = client.socket("/remote-control");

    auto emit_some = [&] {
        for (int i = 0; i < 4; ++i)
            sock->emit("ping", sio::message::list(sio::string_message::create(std::string(4096, 'x'))));
    };

    if (scenario == "watchdog")
    {
        // pingInterval+pingTimeout = 40 ms and the server never pings, so the
        // watchdog tears down each connection with a read pending, then reconnects.
        client.connect(uri, query({{"ping", "20"}}));
        if (!wait_for([&] { return g_opens >= iterations; }, std::chrono::seconds(10 + iterations / 5)))
        {
            std::fprintf(stderr, "FAIL: only %d of %d opens\n", g_opens, iterations);
            return 1;
        }
    }
    else if (scenario == "close" || scenario == "reconnect")
    {
        for (int i = 0; i < iterations; ++i)
        {
            int const opens = g_opens;
            // A changing query makes connect() restart even while open.
            client.connect(uri, query({{"n", std::to_string(i)}}));
            if (!wait_for([&] { return g_opens > opens; }, 5s))
            {
                std::fprintf(stderr, "FAIL: no open on iteration %d\n", i);
                return 1;
            }
            emit_some();
            std::this_thread::sleep_for(std::chrono::milliseconds(i % 3));
            if (scenario == "close")
            {
                int const closes = g_closes;
                client.close();
                if (!wait_for([&] { return g_closes > closes; }, 5s))
                {
                    std::fprintf(stderr, "FAIL: no close on iteration %d\n", i);
                    return 1;
                }
            }
        }
    }
    else
    {
        std::fprintf(stderr, "unknown scenario %s\n", scenario.c_str());
        return 2;
    }
    std::printf("PASS %s%s: %d opens, %d closes\n", scenario.c_str(), flood ? " flood" : "",
                g_opens, g_closes);
    return 0;
}
