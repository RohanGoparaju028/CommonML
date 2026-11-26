#include "builtins_internal.hpp"
#include "cml/source.hpp"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
using ssize_t = int;
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <cerrno>
#include <csignal>
#include <cstring>
#include <string>

namespace cml::detail {

namespace {

#ifdef _WIN32
void ensure_winsock() {
    static bool inited = []() {
        WSADATA wsa {};
        WSAStartup(MAKEWORD(2, 2), &wsa);
        return true;
    }();
    (void)inited;
}
void close_sock(int fd) {
    closesocket(static_cast<SOCKET>(fd));
}
#else
void ensure_winsock() {}
void close_sock(int fd) {
    close(fd);
}
#endif

void configure_socket_nosigpipe(int fd) {
#ifdef SO_NOSIGPIPE
    int yes = 1;
    setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, reinterpret_cast<const char*>(&yes), sizeof(yes));
#elif !defined(_WIN32)
    (void)fd;
    std::signal(SIGPIPE, SIG_IGN);
#else
    (void)fd;
#endif
}

std::string errno_msg(const std::string& prefix) {
#ifdef _WIN32
    return prefix + ": WSA error " + std::to_string(WSAGetLastError());
#else
    return prefix + ": " + std::strerror(errno);
#endif
}

}  // namespace

void append_net_builtins(std::vector<BuiltinDef>& list) {
    // tcp_listen : String -> Int -> Int
    list.push_back(BuiltinDef{
        "tcp_listen",
        TypeScheme{{}, make_tfun(string_type(), make_tfun(int_type(), int_type()))},
        2,
        [](const std::vector<Value>& args) {
            ensure_winsock();
            const auto& host = std::get<ValString>(args[0].node).value;
            int64_t port = std::get<ValInt>(args[1].node).value;
            if (port < 0 || port > 65535) {
                throw CompileError(Diagnostic{"runtime", "tcp_listen port out of range 0..65535", {}});
            }

            struct addrinfo hints {};
            hints.ai_family = AF_INET;
            hints.ai_socktype = SOCK_STREAM;
            hints.ai_flags = AI_PASSIVE;

            std::string port_str = std::to_string(port);
            const char* node = host.empty() ? nullptr : host.c_str();
            struct addrinfo* res = nullptr;
            int gai = getaddrinfo(node, port_str.c_str(), &hints, &res);
            if (gai != 0 || !res) {
                throw CompileError(Diagnostic{
                    "runtime", std::string("tcp_listen getaddrinfo failed: ") + gai_strerror(gai), {}});
            }

            int fd = -1;
            for (struct addrinfo* p = res; p != nullptr; p = p->ai_next) {
                fd = static_cast<int>(socket(p->ai_family, p->ai_socktype, p->ai_protocol));
                if (fd < 0) continue;
                int yes = 1;
                setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&yes), sizeof(yes));
                configure_socket_nosigpipe(fd);
                if (bind(fd, p->ai_addr, static_cast<int>(p->ai_addrlen)) == 0) {
                    if (listen(fd, 64) == 0) {
                        break;
                    }
                }
                close_sock(fd);
                fd = -1;
            }
            freeaddrinfo(res);

            if (fd < 0) {
                throw CompileError(Diagnostic{"runtime", errno_msg("tcp_listen failed"), {}});
            }
            return Value::make_int(static_cast<int64_t>(fd));
        },
    });

    // tcp_local_port : Int -> Int
    list.push_back(BuiltinDef{
        "tcp_local_port",
        TypeScheme{{}, make_tfun(int_type(), int_type())},
        1,
        [](const std::vector<Value>& args) {
            int fd = static_cast<int>(std::get<ValInt>(args[0].node).value);
            struct sockaddr_in addr {};
            socklen_t len = sizeof(addr);
            if (getsockname(fd, reinterpret_cast<struct sockaddr*>(&addr), &len) != 0) {
                throw CompileError(Diagnostic{"runtime", errno_msg("tcp_local_port failed"), {}});
            }
            return Value::make_int(static_cast<int64_t>(ntohs(addr.sin_port)));
        },
    });

    // tcp_accept : Int -> Int
    list.push_back(BuiltinDef{
        "tcp_accept",
        TypeScheme{{}, make_tfun(int_type(), int_type())},
        1,
        [](const std::vector<Value>& args) {
            int server_fd = static_cast<int>(std::get<ValInt>(args[0].node).value);
            if (server_fd < 0) {
                throw CompileError(Diagnostic{"runtime", "tcp_accept called with invalid fd", {}});
            }
            int client_fd = accept(server_fd, nullptr, nullptr);
            if (client_fd < 0) {
                throw CompileError(Diagnostic{"runtime", errno_msg("tcp_accept failed"), {}});
            }
            configure_socket_nosigpipe(client_fd);
            return Value::make_int(static_cast<int64_t>(client_fd));
        },
    });

    // tcp_connect : String -> Int -> Int
    list.push_back(BuiltinDef{
        "tcp_connect",
        TypeScheme{{}, make_tfun(string_type(), make_tfun(int_type(), int_type()))},
        2,
        [](const std::vector<Value>& args) {
            ensure_winsock();
            const auto& host = std::get<ValString>(args[0].node).value;
            int64_t port = std::get<ValInt>(args[1].node).value;
            if (port <= 0 || port > 65535) {
                throw CompileError(Diagnostic{"runtime", "tcp_connect port out of range 1..65535", {}});
            }

            struct addrinfo hints {};
            hints.ai_family = AF_UNSPEC;
            hints.ai_socktype = SOCK_STREAM;

            std::string port_str = std::to_string(port);
            struct addrinfo* res = nullptr;
            int gai = getaddrinfo(host.c_str(), port_str.c_str(), &hints, &res);
            if (gai != 0 || !res) {
                throw CompileError(Diagnostic{
                    "runtime", std::string("tcp_connect getaddrinfo failed: ") + gai_strerror(gai), {}});
            }

            int fd = -1;
            for (struct addrinfo* p = res; p != nullptr; p = p->ai_next) {
                fd = static_cast<int>(socket(p->ai_family, p->ai_socktype, p->ai_protocol));
                if (fd < 0) continue;
                configure_socket_nosigpipe(fd);
                if (connect(fd, p->ai_addr, static_cast<int>(p->ai_addrlen)) == 0) {
                    break;
                }
                close_sock(fd);
                fd = -1;
            }
            freeaddrinfo(res);

            if (fd < 0) {
                throw CompileError(Diagnostic{"runtime", errno_msg("tcp_connect failed"), {}});
            }
            return Value::make_int(static_cast<int64_t>(fd));
        },
    });

    // sock_send : Int -> String -> Int
    list.push_back(BuiltinDef{
        "sock_send",
        TypeScheme{{}, make_tfun(int_type(), make_tfun(string_type(), int_type()))},
        2,
        [](const std::vector<Value>& args) {
            int fd = static_cast<int>(std::get<ValInt>(args[0].node).value);
            const auto& data = std::get<ValString>(args[1].node).value;
            if (fd < 0) {
                throw CompileError(Diagnostic{"runtime", "sock_send called with invalid fd", {}});
            }
            size_t total = 0;
            while (total < data.size()) {
#ifdef MSG_NOSIGNAL
                ssize_t n = send(fd, data.data() + total, static_cast<int>(data.size() - total), MSG_NOSIGNAL);
#else
                ssize_t n = send(fd, data.data() + total, static_cast<int>(data.size() - total), 0);
#endif
                if (n < 0) {
                    if (errno == EINTR) continue;
                    throw CompileError(Diagnostic{"runtime", errno_msg("sock_send failed"), {}});
                }
                total += static_cast<size_t>(n);
            }
            return Value::make_int(static_cast<int64_t>(total));
        },
    });

    // sock_recv : Int -> Int -> String
    list.push_back(BuiltinDef{
        "sock_recv",
        TypeScheme{{}, make_tfun(int_type(), make_tfun(int_type(), string_type()))},
        2,
        [](const std::vector<Value>& args) {
            int fd = static_cast<int>(std::get<ValInt>(args[0].node).value);
            int64_t max_bytes = std::get<ValInt>(args[1].node).value;
            if (fd < 0) {
                throw CompileError(Diagnostic{"runtime", "sock_recv called with invalid fd", {}});
            }
            if (max_bytes <= 0) {
                return Value::make_string("");
            }
            std::string buf(static_cast<size_t>(max_bytes), '\0');
            ssize_t n = 0;
            do {
                n = recv(fd, buf.data(), static_cast<int>(buf.size()), 0);
            } while (n < 0 && errno == EINTR);
            if (n < 0) {
                throw CompileError(Diagnostic{"runtime", errno_msg("sock_recv failed"), {}});
            }
            buf.resize(static_cast<size_t>(n));
            return Value::make_string(std::move(buf));
        },
    });

    // sock_recv_all : Int -> Int -> String
    list.push_back(BuiltinDef{
        "sock_recv_all",
        TypeScheme{{}, make_tfun(int_type(), make_tfun(int_type(), string_type()))},
        2,
        [](const std::vector<Value>& args) {
            int fd = static_cast<int>(std::get<ValInt>(args[0].node).value);
            int64_t max_bytes = std::get<ValInt>(args[1].node).value;
            if (fd < 0) {
                throw CompileError(Diagnostic{"runtime", "sock_recv_all called with invalid fd", {}});
            }
            std::string out;
            char chunk[4096];
            while (max_bytes <= 0 || static_cast<int64_t>(out.size()) < max_bytes) {
                size_t to_read = sizeof(chunk);
                if (max_bytes > 0) {
                    size_t rem = static_cast<size_t>(max_bytes) - out.size();
                    if (rem < to_read) to_read = rem;
                }
                ssize_t n = recv(fd, chunk, static_cast<int>(to_read), 0);
                if (n < 0) {
                    if (errno == EINTR) continue;
                    throw CompileError(Diagnostic{"runtime", errno_msg("sock_recv_all failed"), {}});
                }
                if (n == 0) break;
                out.append(chunk, static_cast<size_t>(n));
            }
            return Value::make_string(std::move(out));
        },
    });

    // sock_close : Int -> Unit
    list.push_back(BuiltinDef{
        "sock_close",
        TypeScheme{{}, make_tfun(int_type(), unit_type())},
        1,
        [](const std::vector<Value>& args) {
            int fd = static_cast<int>(std::get<ValInt>(args[0].node).value);
            if (fd >= 0) {
                close_sock(fd);
            }
            return Value::unit();
        },
    });
}

}  // namespace cml::detail
