// Boost.Beast-backed drop-in shim for the socket.io-client-cpp API surface
// used by EDreamClient. Engine.IO v4 / Socket.IO protocol 5, websocket-only.

#include "sio_client.h"
#include "sio_socket.h"

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/asio/strand.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/websocket/ssl.hpp>
#include <boost/json.hpp>

#include <openssl/ssl.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <sstream>
#include <thread>
#include <unordered_map>
#include <vector>

namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
namespace net = boost::asio;
namespace ssl = boost::asio::ssl;
using tcp = boost::asio::ip::tcp;

namespace sio
{

class packet
{
};

namespace
{

static std::string url_encode(const std::string& value)
{
    static const char hex[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(value.size() * 3);
    for (unsigned char c : value)
    {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~')
        {
            out.push_back(static_cast<char>(c));
        }
        else
        {
            out.push_back('%');
            out.push_back(hex[c >> 4]);
            out.push_back(hex[c & 0x0F]);
        }
    }
    return out;
}

static boost::json::value message_to_json(const message::ptr& msg)
{
    if (!msg)
        return nullptr;
    switch (msg->get_flag())
    {
    case message::flag_null:
        return nullptr;
    case message::flag_boolean:
        return msg->get_bool();
    case message::flag_integer:
        return msg->get_int();
    case message::flag_double:
        return msg->get_double();
    case message::flag_string:
        return boost::json::string(msg->get_string());
    case message::flag_binary:
        // Binary not used by EDreamClient; emit as empty string placeholder.
        return boost::json::string("");
    case message::flag_array:
    {
        boost::json::array arr;
        for (auto const& child : msg->get_vector())
            arr.push_back(message_to_json(child));
        return arr;
    }
    case message::flag_object:
    {
        boost::json::object obj;
        for (auto const& kv : msg->get_map())
            obj[kv.first] = message_to_json(kv.second);
        return obj;
    }
    }
    return nullptr;
}

static message::ptr json_to_message(boost::json::value const& v)
{
    switch (v.kind())
    {
    case boost::json::kind::null:
        return null_message::create();
    case boost::json::kind::bool_:
        return bool_message::create(v.as_bool());
    case boost::json::kind::int64:
        return int_message::create(v.as_int64());
    case boost::json::kind::uint64:
        return int_message::create(static_cast<int64_t>(v.as_uint64()));
    case boost::json::kind::double_:
        return double_message::create(v.as_double());
    case boost::json::kind::string:
        return string_message::create(std::string(v.as_string()));
    case boost::json::kind::array:
    {
        auto arr = array_message::create();
        for (auto const& child : v.as_array())
            arr->get_vector().push_back(json_to_message(child));
        return arr;
    }
    case boost::json::kind::object:
    {
        auto obj = object_message::create();
        for (auto const& kv : v.as_object())
            obj->get_map()[std::string(kv.key())] = json_to_message(kv.value());
        return obj;
    }
    }
    return null_message::create();
}

struct parsed_uri
{
    bool secure = true;
    std::string host;
    std::string port = "443";
    std::string path = "/socket.io/";
};

static bool parse_uri(std::string const& uri, parsed_uri& out)
{
    // Accept wss://host[:port][/path]
    std::string rest = uri;
    if (rest.rfind("wss://", 0) == 0)
    {
        out.secure = true;
        rest = rest.substr(6);
        out.port = "443";
    }
    else if (rest.rfind("ws://", 0) == 0)
    {
        out.secure = false;
        rest = rest.substr(5);
        out.port = "80";
    }
    else
    {
        return false;
    }

    auto slash = rest.find('/');
    std::string hostport = slash == std::string::npos ? rest : rest.substr(0, slash);
    if (slash != std::string::npos && slash + 1 < rest.size())
        out.path = rest.substr(slash); // keep leading /
    else
        out.path = "/socket.io/";

    if (!out.path.empty() && out.path.back() != '/')
        out.path.push_back('/');

    auto colon = hostport.rfind(':');
    // IPv6 not supported in this shim; hostnames won't contain ':'.
    if (colon != std::string::npos)
    {
        out.host = hostport.substr(0, colon);
        out.port = hostport.substr(colon + 1);
    }
    else
    {
        out.host = hostport;
    }
    return !out.host.empty();
}

} // namespace

// ---------------------------------------------------------------------------
// event
// ---------------------------------------------------------------------------

event::event(std::string const& nsp, std::string const& name, message::list const& messages, bool need_ack)
    : m_nsp(nsp)
    , m_name(name)
    , m_messages(messages)
    , m_need_ack(need_ack)
{
}

event::event(std::string const& nsp, std::string const& name, message::list&& messages, bool need_ack)
    : m_nsp(nsp)
    , m_name(name)
    , m_messages(std::move(messages))
    , m_need_ack(need_ack)
{
}

const std::string& event::get_nsp() const { return m_nsp; }
const std::string& event::get_name() const { return m_name; }

const message::ptr& event::get_message() const
{
    static message::ptr empty;
    if (m_messages.size() > 0)
        return m_messages[0];
    return empty;
}

const message::list& event::get_messages() const { return m_messages; }
bool event::need_ack() const { return m_need_ack; }
void event::put_ack_message(message::list const& ack_message) { m_ack_message = ack_message; }
message::list const& event::get_ack_message() const { return m_ack_message; }
message::list& event::get_ack_message_impl() { return m_ack_message; }

// ---------------------------------------------------------------------------
// socket::impl + client_impl
// ---------------------------------------------------------------------------

class socket::impl
{
public:
    impl(client_impl* client, std::string nsp)
        : m_client(client)
        , m_nsp(std::move(nsp))
    {
    }

    client_impl* m_client;
    std::string m_nsp;
    std::mutex m_event_mutex;
    std::unordered_map<std::string, event_listener> m_event_binding;
    std::unordered_map<std::string, event_listener_aux> m_event_binding_aux;
    event_listener m_any;
    event_listener_aux m_any_aux;
    error_listener m_error_listener;
};

class client_impl : public std::enable_shared_from_this<client_impl>
{
public:
    using ws_stream = websocket::stream<ssl::stream<beast::tcp_stream>>;

    // Keeps this object alive for shared_from_this while client exists.
    std::shared_ptr<client_impl> m_self;

    client_impl()
        : m_work(net::make_work_guard(m_ioc))
        , m_strand(net::make_strand(m_ioc))
        , m_resolver(m_strand)
        , m_ssl_ctx(ssl::context::tlsv12_client)
        , m_ping_timer(m_strand)
        , m_reconnect_timer(m_strand)
    {
        // On Windows, OpenSSL's default verify paths do not include the system
        // certificate store (unlike curl/Schannel used for REST). Load it so
        // wss:// handshakes can verify public CAs (e.g. api-alpha).
#if defined(_WIN32)
        // OpenSSL defaults omit the Windows system store; curl/Schannel does not.
        if (SSL_CTX_load_verify_store(m_ssl_ctx.native_handle(), "org.openssl.winstore://") != 1)
            m_ssl_ctx.set_default_verify_paths();
#else
        m_ssl_ctx.set_default_verify_paths();
#endif
        m_ssl_ctx.set_verify_mode(ssl::verify_peer);
        m_network_thread = std::thread([this]() { m_ioc.run(); });
    }

    void shutdown()
    {
        m_abort_retries = true;
        m_work.reset();
        m_ioc.stop();
        if (m_network_thread.joinable() &&
            m_network_thread.get_id() != std::this_thread::get_id())
            m_network_thread.join();
        m_ws.reset();
        m_write_queue.clear();
        m_writing = false;
    }

    ~client_impl()
    {
        shutdown();
    }

    void set_open_listener(client::con_listener const& l) { m_open_listener = l; }
    void set_fail_listener(client::con_listener const& l) { m_fail_listener = l; }
    void set_reconnecting_listener(client::con_listener const& l) { m_reconnecting_listener = l; }
    void set_reconnect_listener(client::reconnect_listener const& l) { m_reconnect_listener = l; }
    void set_close_listener(client::close_listener const& l) { m_close_listener = l; }
    void set_socket_open_listener(client::socket_listener const& l) { m_socket_open_listener = l; }
    void set_socket_close_listener(client::socket_listener const& l) { m_socket_close_listener = l; }

    void clear_con_listeners()
    {
        m_open_listener = nullptr;
        m_fail_listener = nullptr;
        m_reconnecting_listener = nullptr;
        m_reconnect_listener = nullptr;
        m_close_listener = nullptr;
    }

    void clear_socket_listeners()
    {
        m_socket_open_listener = nullptr;
        m_socket_close_listener = nullptr;
    }

    void set_reconnect_attempts(int attempts) { m_reconn_attempts = static_cast<unsigned>(attempts); }
    void set_reconnect_delay(unsigned millis)
    {
        m_reconn_delay = millis;
        if (m_reconn_delay_max < millis)
            m_reconn_delay_max = millis;
    }
    void set_reconnect_delay_max(unsigned millis)
    {
        m_reconn_delay_max = millis;
        if (m_reconn_delay > millis)
            m_reconn_delay = millis;
    }

    bool opened() const { return m_opened.load(); }
    std::string const& get_sessionid() const { return m_sid; }

    socket::ptr const& get_socket(std::string const& nsp)
    {
        std::string key = nsp;
        if (key.empty())
            key = "/";
        else if (key.front() != '/')
            key = "/" + key;

        std::lock_guard<std::mutex> lock(m_socket_mutex);
        auto it = m_sockets.find(key);
        if (it != m_sockets.end())
            return it->second;

        auto sock = socket::ptr(new socket(this, key, message::ptr()));
        auto inserted = m_sockets.emplace(key, sock);
        return inserted.first->second;
    }

    void connect(std::string const& uri,
                 std::map<std::string, std::string> const& query,
                 std::map<std::string, std::string> const& headers,
                 message::ptr const& /*auth*/)
    {
        m_abort_retries = false;
        m_uri = uri;
        m_query = query;
        m_headers = headers;
        m_reconn_made = 0;

        net::post(m_strand, [self = shared_from_this()]() { self->do_connect(); });
    }

    void close()
    {
        m_abort_retries = true;
        net::post(m_strand, [self = shared_from_this()]() {
            self->m_reconnect_timer.cancel();
            self->m_ping_timer.cancel();
            self->close_socket_unlocked();
            if (self->m_opened.exchange(false))
            {
                if (self->m_close_listener)
                    self->m_close_listener(client::close_reason_normal);
            }
        });
    }

    void sync_close()
    {
        close();
        // Best-effort wait for close on the strand.
        std::mutex m;
        std::condition_variable cv;
        bool done = false;
        net::post(m_strand, [&]() {
            std::lock_guard<std::mutex> lock(m);
            done = true;
            cv.notify_one();
        });
        std::unique_lock<std::mutex> lock(m);
        cv.wait_for(lock, std::chrono::seconds(2), [&]() { return done; });
    }

    void emit(std::string const& nsp, std::string const& name, message::list const& msglist)
    {
        net::post(m_strand, [self = shared_from_this(), nsp, name, msglist]() {
            if (!self->m_ws || !self->m_opened.load())
                return;

            boost::json::array arr;
            arr.emplace_back(name);
            for (size_t i = 0; i < msglist.size(); ++i)
                arr.push_back(message_to_json(msglist[i]));

            std::string payload = "42";
            if (nsp != "/")
            {
                payload += nsp;
                payload += ",";
            }
            payload += boost::json::serialize(arr);
            self->write_text(payload);
        });
    }

    void dispatch_event(std::string const& nsp, std::string const& name, message::list&& messages)
    {
        socket::ptr sock;
        {
            std::lock_guard<std::mutex> lock(m_socket_mutex);
            auto it = m_sockets.find(nsp);
            if (it == m_sockets.end())
                return;
            sock = it->second;
        }
        if (!sock || !sock->m_impl)
            return;

        event ev(nsp, name, std::move(messages), false);
        auto* impl = sock->m_impl;
        std::lock_guard<std::mutex> lock(impl->m_event_mutex);
        auto it = impl->m_event_binding.find(name);
        if (it != impl->m_event_binding.end() && it->second)
            it->second(ev);
        auto it_aux = impl->m_event_binding_aux.find(name);
        if (it_aux != impl->m_event_binding_aux.end() && it_aux->second)
        {
            message::list ack;
            it_aux->second(name, ev.get_message(), false, ack);
        }
        if (impl->m_any)
            impl->m_any(ev);
        if (impl->m_any_aux)
        {
            message::list ack;
            impl->m_any_aux(name, ev.get_message(), false, ack);
        }
    }

private:
    friend class socket;

    void do_connect()
    {
        close_socket_unlocked();
        m_opened = false;
        m_nsp_connected = false;

        parsed_uri parsed;
        if (!parse_uri(m_uri, parsed) || !parsed.secure)
        {
            fail_and_maybe_reconnect();
            return;
        }
        m_parsed = parsed;

        m_ws = std::make_unique<ws_stream>(m_strand, m_ssl_ctx);
        m_ws->next_layer().set_verify_callback(ssl::host_name_verification(m_parsed.host));

        m_resolver.async_resolve(
            m_parsed.host, m_parsed.port,
            beast::bind_front_handler(&client_impl::on_resolve, shared_from_this()));
    }

    void on_resolve(beast::error_code ec, tcp::resolver::results_type results)
    {
        if (ec)
        {
            fail_and_maybe_reconnect();
            return;
        }
        beast::get_lowest_layer(*m_ws).expires_after(std::chrono::seconds(30));
        beast::get_lowest_layer(*m_ws).async_connect(
            results, beast::bind_front_handler(&client_impl::on_connect, shared_from_this()));
    }

    void on_connect(beast::error_code ec, tcp::resolver::results_type::endpoint_type)
    {
        if (ec)
        {
            fail_and_maybe_reconnect();
            return;
        }

        if (!SSL_set_tlsext_host_name(m_ws->next_layer().native_handle(), m_parsed.host.c_str()))
        {
            fail_and_maybe_reconnect();
            return;
        }

        beast::get_lowest_layer(*m_ws).expires_after(std::chrono::seconds(30));
        m_ws->next_layer().async_handshake(
            ssl::stream_base::client,
            beast::bind_front_handler(&client_impl::on_ssl_handshake, shared_from_this()));
    }

    void on_ssl_handshake(beast::error_code ec)
    {
        if (ec)
        {
            fail_and_maybe_reconnect();
            return;
        }

        beast::get_lowest_layer(*m_ws).expires_never();
        m_ws->set_option(websocket::stream_base::timeout::suggested(beast::role_type::client));
        m_ws->set_option(websocket::stream_base::decorator(
            [headers = m_headers](websocket::request_type& req) {
                for (auto const& kv : headers)
                    req.set(kv.first, kv.second);
            }));

        std::ostringstream target;
        target << m_parsed.path << "?EIO=4&transport=websocket";
        for (auto const& kv : m_query)
            target << "&" << url_encode(kv.first) << "=" << url_encode(kv.second);

        m_ws->async_handshake(
            m_parsed.host, target.str(),
            beast::bind_front_handler(&client_impl::on_ws_handshake, shared_from_this()));
    }

    void on_ws_handshake(beast::error_code ec)
    {
        if (ec)
        {
            fail_and_maybe_reconnect();
            return;
        }
        m_writing = false;
        m_write_queue.clear();
        do_read();
    }

    void do_read()
    {
        if (!m_ws)
            return;
        m_ws->async_read(
            m_buffer, beast::bind_front_handler(&client_impl::on_read, shared_from_this()));
    }

    void on_read(beast::error_code ec, std::size_t)
    {
        if (ec)
        {
            on_disconnected(client::close_reason_drop);
            return;
        }

        if (!m_ws->got_text())
        {
            m_buffer.consume(m_buffer.size());
            do_read();
            return;
        }

        std::string data = beast::buffers_to_string(m_buffer.data());
        m_buffer.consume(m_buffer.size());
        handle_packet(data);
        do_read();
    }

    void handle_packet(std::string const& data)
    {
        if (data.empty())
            return;

        char engine_type = data[0];
        std::string rest = data.substr(1);

        switch (engine_type)
        {
        case '0': // open
        {
            try
            {
                auto v = boost::json::parse(rest);
                auto const& obj = v.as_object();
                if (auto* sid = obj.if_contains("sid"))
                    m_sid = std::string(sid->as_string());
                m_ping_interval_ms = 25000;
                m_ping_timeout_ms = 20000;
                if (auto* pi = obj.if_contains("pingInterval"))
                    m_ping_interval_ms = static_cast<unsigned>(pi->to_number<std::uint64_t>());
                if (auto* pt = obj.if_contains("pingTimeout"))
                    m_ping_timeout_ms = static_cast<unsigned>(pt->to_number<std::uint64_t>());
            }
            catch (...)
            {
                // keep defaults
            }
            arm_ping_watchdog();
            // Connect default + /remote-control namespaces used by the app.
            write_text("40");
            write_text("40/remote-control,");
            break;
        }
        case '2': // ping
            write_text("3");
            arm_ping_watchdog();
            break;
        case '3': // pong
            arm_ping_watchdog();
            break;
        case '4': // message (socket.io packet)
            handle_socketio_packet(rest);
            break;
        case '1': // close
            on_disconnected(client::close_reason_normal);
            break;
        default:
            break;
        }
    }

    void handle_socketio_packet(std::string const& data)
    {
        if (data.empty())
            return;
        char type = data[0];
        std::string rest = data.substr(1);

        // Optional attachments prefix N- for binary; not used here.
        std::string nsp = "/";
        std::string payload;

        auto comma = rest.find(',');
        auto bracket = rest.find('[');
        auto brace = rest.find('{');

        if (!rest.empty() && rest[0] == '/')
        {
            // namespace present
            std::size_t end = rest.size();
            if (comma != std::string::npos)
                end = comma;
            else if (bracket != std::string::npos)
                end = bracket;
            else if (brace != std::string::npos)
                end = brace;
            nsp = rest.substr(0, end);
            if (comma != std::string::npos && comma == end)
                payload = rest.substr(comma + 1);
            else if (end < rest.size())
                payload = rest.substr(end);
        }
        else
        {
            payload = rest;
        }

        switch (type)
        {
        case '0': // CONNECT
            if (nsp == "/remote-control" || (!m_nsp_connected && nsp == "/"))
            {
                if (nsp == "/remote-control")
                {
                    m_nsp_connected = true;
                    m_opened = true;
                    m_reconn_made = 0;
                    if (m_socket_open_listener)
                        m_socket_open_listener(nsp);
                    if (m_open_listener)
                        m_open_listener();
                }
            }
            break;
        case '1': // DISCONNECT
            if (nsp == "/remote-control")
                on_disconnected(client::close_reason_normal);
            break;
        case '2': // EVENT
        {
            try
            {
                auto v = boost::json::parse(payload);
                auto const& arr = v.as_array();
                if (arr.empty() || !arr[0].is_string())
                    break;
                std::string name(arr[0].as_string());
                message::list messages;
                for (std::size_t i = 1; i < arr.size(); ++i)
                    messages.push(json_to_message(arr[i]));
                // Ensure socket exists for nsp
                (void)get_socket(nsp);
                dispatch_event(nsp, name, std::move(messages));
            }
            catch (...)
            {
            }
            break;
        }
        default:
            break;
        }
    }

    void write_text(std::string const& text)
    {
        m_write_queue.push_back(text);
        if (!m_writing)
            do_write();
    }

    void do_write()
    {
        if (!m_ws || m_write_queue.empty())
        {
            m_writing = false;
            return;
        }
        m_writing = true;
        auto& front = m_write_queue.front();
        m_ws->text(true);
        m_ws->async_write(
            net::buffer(front),
            beast::bind_front_handler(&client_impl::on_write, shared_from_this()));
    }

    void on_write(beast::error_code ec, std::size_t)
    {
        if (ec)
        {
            on_disconnected(client::close_reason_drop);
            return;
        }
        if (!m_write_queue.empty())
            m_write_queue.pop_front();
        if (m_write_queue.empty())
            m_writing = false;
        else
            do_write();
    }

    void arm_ping_watchdog()
    {
        m_ping_timer.cancel();
        auto timeout = std::chrono::milliseconds(m_ping_interval_ms + m_ping_timeout_ms);
        m_ping_timer.expires_after(timeout);
        m_ping_timer.async_wait([self = shared_from_this()](beast::error_code ec) {
            if (ec)
                return;
            self->on_disconnected(client::close_reason_drop);
        });
    }

    void close_socket_unlocked()
    {
        m_ping_timer.cancel();
        if (m_ws)
        {
            beast::error_code ec;
            m_ws->close(websocket::close_code::normal, ec);
            m_ws.reset();
        }
        m_write_queue.clear();
        m_writing = false;
        m_buffer.consume(m_buffer.size());
    }

    void on_disconnected(client::close_reason reason)
    {
        bool was_open = m_opened.exchange(false);
        m_nsp_connected = false;
        close_socket_unlocked();

        if (was_open && m_close_listener)
            m_close_listener(reason);

        if (!m_abort_retries)
            schedule_reconnect();
        else if (!was_open && m_fail_listener)
            m_fail_listener();
    }

    void fail_and_maybe_reconnect()
    {
        m_opened = false;
        m_nsp_connected = false;
        close_socket_unlocked();
        if (!m_abort_retries)
            schedule_reconnect();
        else if (m_fail_listener)
            m_fail_listener();
    }

    unsigned next_delay() const
    {
        unsigned reconn_made = std::min(m_reconn_made, 32u);
        double delay = m_reconn_delay * std::pow(1.5, reconn_made);
        if (delay > m_reconn_delay_max)
            delay = m_reconn_delay_max;
        return static_cast<unsigned>(delay);
    }

    void schedule_reconnect()
    {
        if (m_reconn_made >= m_reconn_attempts)
        {
            if (m_fail_listener)
                m_fail_listener();
            return;
        }

        if (m_reconnecting_listener)
            m_reconnecting_listener();

        unsigned delay = next_delay();
        if (m_reconnect_listener)
            m_reconnect_listener(m_reconn_made, delay);
        ++m_reconn_made;

        m_reconnect_timer.expires_after(std::chrono::milliseconds(delay));
        m_reconnect_timer.async_wait([self = shared_from_this()](beast::error_code ec) {
            if (ec || self->m_abort_retries)
                return;
            self->do_connect();
        });
    }

    net::io_context m_ioc;
    net::executor_work_guard<net::io_context::executor_type> m_work;
    net::strand<net::io_context::executor_type> m_strand;
    tcp::resolver m_resolver;
    ssl::context m_ssl_ctx;
    std::unique_ptr<ws_stream> m_ws;
    beast::flat_buffer m_buffer;
    net::steady_timer m_ping_timer;
    net::steady_timer m_reconnect_timer;
    std::thread m_network_thread;

    std::mutex m_state_mutex;
    std::mutex m_socket_mutex;
    std::map<std::string, socket::ptr> m_sockets;

    std::string m_uri;
    std::map<std::string, std::string> m_query;
    std::map<std::string, std::string> m_headers;
    parsed_uri m_parsed;
    std::string m_sid;

    std::deque<std::string> m_write_queue;
    bool m_writing = false;

    std::atomic<bool> m_opened{false};
    bool m_nsp_connected = false;
    std::atomic<bool> m_abort_retries{false};

    unsigned m_ping_interval_ms = 25000;
    unsigned m_ping_timeout_ms = 20000;
    unsigned m_reconn_delay = 5000;
    unsigned m_reconn_delay_max = 25000;
    unsigned m_reconn_attempts = 0xFFFFFFFFu;
    unsigned m_reconn_made = 0;

    client::con_listener m_open_listener;
    client::con_listener m_fail_listener;
    client::con_listener m_reconnecting_listener;
    client::reconnect_listener m_reconnect_listener;
    client::close_listener m_close_listener;
    client::socket_listener m_socket_open_listener;
    client::socket_listener m_socket_close_listener;
};

// ---------------------------------------------------------------------------
// socket
// ---------------------------------------------------------------------------

socket::socket(client_impl* client, std::string const& nsp, message::ptr const&)
    : m_impl(new impl(client, nsp))
{
}

socket::~socket()
{
    delete m_impl;
}

void socket::on(std::string const& event_name, event_listener const& func)
{
    std::lock_guard<std::mutex> lock(m_impl->m_event_mutex);
    m_impl->m_event_binding[event_name] = func;
}

void socket::on(std::string const& event_name, event_listener_aux const& func)
{
    std::lock_guard<std::mutex> lock(m_impl->m_event_mutex);
    m_impl->m_event_binding_aux[event_name] = func;
}

void socket::off(std::string const& event_name)
{
    std::lock_guard<std::mutex> lock(m_impl->m_event_mutex);
    m_impl->m_event_binding.erase(event_name);
    m_impl->m_event_binding_aux.erase(event_name);
}

void socket::on_any(event_listener const& func)
{
    std::lock_guard<std::mutex> lock(m_impl->m_event_mutex);
    m_impl->m_any = func;
}

void socket::on_any(event_listener_aux const& func)
{
    std::lock_guard<std::mutex> lock(m_impl->m_event_mutex);
    m_impl->m_any_aux = func;
}

void socket::off_all()
{
    std::lock_guard<std::mutex> lock(m_impl->m_event_mutex);
    m_impl->m_event_binding.clear();
    m_impl->m_event_binding_aux.clear();
    m_impl->m_any = nullptr;
    m_impl->m_any_aux = nullptr;
}

void socket::close() {}

void socket::on_error(error_listener const& l)
{
    std::lock_guard<std::mutex> lock(m_impl->m_event_mutex);
    m_impl->m_error_listener = l;
}

void socket::off_error()
{
    std::lock_guard<std::mutex> lock(m_impl->m_event_mutex);
    m_impl->m_error_listener = nullptr;
}

void socket::emit(std::string const& name, message::list const& msglist,
                  std::function<void(message::list const&)> const& /*ack*/)
{
    if (m_impl && m_impl->m_client)
        m_impl->m_client->emit(m_impl->m_nsp, name, msglist);
}

std::string const& socket::get_namespace() const { return m_impl->m_nsp; }

void socket::on_connected() {}
void socket::on_close() {}
void socket::on_open() {}
void socket::on_disconnect() {}
void socket::on_message_packet(packet const&) {}

// ---------------------------------------------------------------------------
// client
// ---------------------------------------------------------------------------

client::client()
    : m_impl(nullptr)
{
    auto impl = std::make_shared<client_impl>();
    impl->m_self = impl;
    m_impl = impl.get();
}

client::client(client_options const&)
    : client()
{
}

client::~client()
{
    if (!m_impl)
        return;
    auto keep = m_impl->m_self;
    m_impl->shutdown();
    m_impl->m_self.reset();
    m_impl = nullptr;
    keep.reset();
}

void client::set_open_listener(con_listener const& l) { m_impl->set_open_listener(l); }
void client::set_fail_listener(con_listener const& l) { m_impl->set_fail_listener(l); }
void client::set_reconnecting_listener(con_listener const& l) { m_impl->set_reconnecting_listener(l); }
void client::set_reconnect_listener(reconnect_listener const& l) { m_impl->set_reconnect_listener(l); }
void client::set_close_listener(close_listener const& l) { m_impl->set_close_listener(l); }
void client::set_socket_open_listener(socket_listener const& l) { m_impl->set_socket_open_listener(l); }
void client::set_socket_close_listener(socket_listener const& l) { m_impl->set_socket_close_listener(l); }
void client::clear_con_listeners() { m_impl->clear_con_listeners(); }
void client::clear_socket_listeners() { m_impl->clear_socket_listeners(); }

void client::connect(const std::string& uri)
{
    connect(uri, {}, {}, message::ptr());
}

void client::connect(const std::string& uri, const message::ptr& auth)
{
    connect(uri, {}, {}, auth);
}

void client::connect(const std::string& uri, const std::map<std::string, std::string>& query)
{
    connect(uri, query, {}, message::ptr());
}

void client::connect(const std::string& uri, const std::map<std::string, std::string>& query,
                     const message::ptr& auth)
{
    connect(uri, query, {}, auth);
}

void client::connect(const std::string& uri, const std::map<std::string, std::string>& query,
                     const std::map<std::string, std::string>& http_extra_headers)
{
    connect(uri, query, http_extra_headers, message::ptr());
}

void client::connect(const std::string& uri, const std::map<std::string, std::string>& query,
                     const std::map<std::string, std::string>& http_extra_headers,
                     const message::ptr& auth)
{
    m_impl->connect(uri, query, http_extra_headers, auth);
}

void client::set_reconnect_attempts(int attempts) { m_impl->set_reconnect_attempts(attempts); }
void client::set_reconnect_delay(unsigned millis) { m_impl->set_reconnect_delay(millis); }
void client::set_reconnect_delay_max(unsigned millis) { m_impl->set_reconnect_delay_max(millis); }
void client::set_logs_default() {}
void client::set_logs_quiet() {}
void client::set_logs_verbose() {}

sio::socket::ptr const& client::socket(const std::string& nsp)
{
    return m_impl->get_socket(nsp);
}

void client::close() { m_impl->close(); }
void client::sync_close() { m_impl->sync_close(); }
void client::set_proxy_basic_auth(const std::string&, const std::string&, const std::string&) {}
bool client::opened() const { return m_impl->opened(); }
std::string const& client::get_sessionid() const { return m_impl->get_sessionid(); }

} // namespace sio
