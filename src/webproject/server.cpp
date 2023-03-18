#include "server.h"

#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sstream>
#include <sys/stat.h>
#include <sys/un.h>
#include <variant>


static std::string_view unixPrefix = "unix:";
static std::string_view status405 ="405 Method not allowed";
static std::string_view status404 ="404 Not found";
static std::string_view status400 ="400 Bad request";
static std::string_view status500 ="500 Internal server error";
static std::string_view metrics_ctx = "application/openmetrics-text; version=1.0.0; charset=utf-8";
static std::string_view metrics_path = "/metrics";

struct IPv4Addr {
    sockaddr_in addr;
    void bind(int s) {
        //set SO_REUSEADDR flag
        int reuse_addr = 1;
        if (::setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &reuse_addr, sizeof(reuse_addr))) {
            int e = errno;
            throw std::system_error(e, std::system_category(), "MetricHttpServer: Can't set SO_REUSEADDR");
        }
        if (::bind(s, reinterpret_cast<sockaddr *>(&addr), sizeof(addr))) {
            int e = errno;
            throw std::system_error(e, std::system_category(), "MetricHttpServer: Can't bind to port");
        }
    }
    int socket() const {
        return ::socket(AF_INET, SOCK_STREAM|SOCK_CLOEXEC, IPPROTO_TCP);
    }
};

struct UnixAddr {
    std::string fname;
    unsigned int perms;
    void bind(int s) {
        unlink(fname.c_str());
        sockaddr_un addr;
        addr.sun_family = AF_UNIX;
        std::strncpy(addr.sun_path,fname.c_str(),sizeof(addr.sun_path));
        if (::bind(s, reinterpret_cast<sockaddr *>(&addr), sizeof(addr))) {
            int e = errno;
            throw std::system_error(e, std::system_category(), "MetricHttpServer: Can't bind to unix socket");
        }
        chmod(fname.c_str(), perms);
    }
    int socket() const {
        return ::socket(AF_UNIX, SOCK_STREAM|SOCK_CLOEXEC, 0);
    }
};

using ListenAddr = std::variant<IPv4Addr, UnixAddr>;


UnixAddr resolve_addr_unix(const std::string &address, unsigned int port) {
    unlink(address.c_str());
    unsigned int perms;
    //to octal (666 => 0666)
    perms = ((port/100) % 10) * 64
            + ((port/10)% 10) * 8
            + ((port)% 10);

    return UnixAddr{
        address, perms
    };
}
IPv4Addr resolve_addr_ipv4(const std::string &address, const std::string &port) {
    struct addrinfo hint = {};
    struct addrinfo *res;
    hint.ai_family = AF_INET;
    hint.ai_flags = AI_PASSIVE;

    //translate address and port to sockaddr
    int r = getaddrinfo(address.empty()?nullptr:address.c_str(), port.c_str(), &hint, &res);
    if (r) {
        if (r == EAI_SYSTEM) {
            throw std::system_error(errno, std::system_category(), "MetricHttpServer: getaddrinfo failed");
        } else {
            throw std::runtime_error(std::string("MetricHttpServer: ").append(gai_strerror(r)));
        }
    }
    //search result, whether correct address has been producted
    auto p = res;
    while (p != nullptr && p->ai_family != AF_INET) {
        p = p->ai_next;
    }
    //if not - report error
    if (p == nullptr) {
        freeaddrinfo(res);
        throw std::runtime_error("MetricHttpServer: no appropriate result found");
    }
    //copy sockaddr_in to result
    auto out = *reinterpret_cast<sockaddr_in *>(p->ai_addr);

    freeaddrinfo(res);
    return IPv4Addr{out};
}
ListenAddr resolve_addr(const std::string &address, unsigned int port) {
    if (address.compare(0, unixPrefix.size(), unixPrefix) == 0) {
        return resolve_addr_unix(address.substr(unixPrefix.size()), port);
    } else {
        return resolve_addr_ipv4(address, std::to_string(port));
    }
}


HttpServer::HttpServer(int port, std::string address, EndpointHandler h)
:_h(std::move(h))
{
    //resolve entered address
    auto addr = resolve_addr(address, port);
    //create mother socket
    int mother = std::visit([](auto &x){return x.socket();}, addr);
    try {
        //bind
        std::visit([&](auto &addr) {
            addr.bind(mother);
        }, addr);
        //listen
        if (::listen(mother, SOMAXCONN)) {
            int e = errno;
            throw std::system_error(e, std::system_category(), "MetricHttpServer: Can't listen socket");
        }
    } catch (...) {
        ::close(mother);
        throw;
    }
    //save mother socket - it is ready to accept connections
    _mother = mother;

}

///read socket until sequence has been detected, rest of the data are discarded
/**mostly need to read until "\r\n\r\n" found
 *
 * @param conn connection
 * @param buffer buffer - will contain result
 * @param endseq end sequence
 * @retval true success
 * @retval false connection has been closed
 */

static bool read_until(int conn, std::string &buffer, std::string_view endseq) {
    buffer.clear();
    std::array<char, 1500> tmp;
    for(;;) {
        int r = recv(conn, tmp.data(), tmp.size(), 0);
        if (r < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        if (r == 0) {
            return false;
        }
        buffer.append(tmp.data(), r);
        auto pos = buffer.find(endseq);
        if (pos != buffer.npos) {
            buffer.resize(pos);
            return true;
        }
    }
}

///send whole string to the connection
/**
 * @param conn connection
 * @param data data to write
 * @retval true success
 * @retval false connection has been reset, no write is possible
 */
static bool write_all(int conn, std::string_view data) {
    while (!data.empty()) {
        int r = send(conn, data.data(), data.size(),0);
        if (r<=0) return false;
        data = data.substr(r);
    }
    return true;
}



void HttpServer::send_status(std::string &buffer, int conn, std::string_view status_line, std::string_view extra_msg)  noexcept{
    buffer.clear();
    buffer.append("HTTP/1.0 ");
    buffer.append(status_line);
    buffer.append("\r\n"
                 "Connection: close\r\n"
                 "Content-Type: text/plain\r\n"
                 "Allow: GET\r\n\r\n");
    buffer.append(status_line);
    buffer.append("\r\n");
    if (!extra_msg.empty()) {
        buffer.append("\r\n");
        buffer.append(extra_msg);
        buffer.append("\r\n");
    }
    write_all(conn, buffer);
    ::close(conn);
}

void HttpServer::serve(int conn, std::string &buff)  noexcept{

    if (!read_until(conn, buff, "\r\n\r\n"))
        return;

    std::string_view path (buff);

    if (path.compare(0,4, "GET ")) {
        send_status(buff, conn, status405);
        return;
    }
    path = path.substr(4);
    auto p = path.find(' ');
    if (p == path.npos) {
        send_status(buff, conn, status400);
        return;
    }
    path = path.substr(0, p);
    if (path.empty() || path[0] != '/') {
        send_status(buff, conn, status400);
        return;
    }

    try {

        Request req(path, conn);
        _h(req);

    } catch (std::exception &e) {
        send_status(buff, conn, status500, e.what());
    } catch (...) {
        send_status(buff, conn, status500);
    }
}

void HttpServer::run(std::stop_token stop_token) {

    std::stop_callback cb(stop_token, [&]{
        ::shutdown(SHUT_RD, _mother);
    });
    std::string buffer;
    int s = ::accept(_mother,0,0);
    while (s >= 0) {
        serve(s, buffer);
        s = ::accept(_mother,0,0);
    }
}


HttpServer::~HttpServer() {
}

void HttpServer::Request::send(int code, std::string_view message, std::string_view content_type, std::string_view data)
{
    std::ostringstream bld;
    bld << "HTTP/1.0 " << code << " " << message;
    if (!content_type.empty()) bld << "\r\nContent-Type: " << content_type;
    bld << "\r\nContent-Length:" << data.size();
    bld << "\r\nConnection: close\r\n\r\n";
    write_all(socket, bld.view());
    write_all(socket, data);
    ::close(socket);
    socket = -1;
}

void HttpServer::Request::send(int code, std::string_view message, std::string_view content_type, std::istream &data)
{
    std::ostringstream bld;
    bld << "HTTP/1.0 " << code << " " << message;
    if (!content_type.empty()) bld << "\r\nContent-Type: " << content_type;
    bld << "\r\nConnection: close\r\n\r\n";
    write_all(socket, bld.view());
    std::array<char, 65536> buff;
    while (!!data) {
        data.read(buff.data(), buff.size());
        std::string_view v(buff.data(), data.gcount());
        write_all(socket, v);
    }
    ::close(socket);
    socket = -1;
}
