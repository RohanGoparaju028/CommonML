#include "builtins_internal.hpp"
#include "cml/source.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <netdb.h>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <unistd.h>

namespace cml::detail {

namespace {

void configure_socket_nosigpipe(int fd) {
#ifdef SO_NOSIGPIPE
    int yes = 1;
    setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &yes, sizeof(yes));
#else
    (void)fd;
    std::signal(SIGPIPE, SIG_IGN);
#endif
}

std::string errno_msg(const std::string& prefix) {
    return prefix + ": " + std::strerror(errno);
}

}  // namespace

void append_net_builtins(std::vector<BuiltinDef>& list) {
    // tcp_listen : String -> Int -> Int
    list.push_back(BuiltinDef{
        "tcp_listen",
        TypeScheme{{}, make_tfun(string_type(), make_tfun(int_type(), int_type()))},
        2,
        [](const std::vector<Value>& args) {
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
                fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
                if (fd < 0) continue;
                int yes = 1;
                setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
                configure_socket_nosigpipe(fd);
                if (bind(fd, p->ai_addr, p->ai_addrlen) == 0) {
                    if (listen(fd, 64) == 0) {
                        break;
                    }
                }
                close(fd);
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
                fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
                if (fd < 0) continue;
                configure_socket_nosigpipe(fd);
                if (connect(fd, p->ai_addr, p->ai_addrlen) == 0) {
                    break;
                }
                close(fd);
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
                ssize_t n = send(fd, data.data() + total, data.size() - total, MSG_NOSIGNAL);
#else
                ssize_t n = send(fd, data.data() + total, data.size() - total, 0);
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
                n = recv(fd, buf.data(), buf.size(), 0);
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
                ssize_t n = recv(fd, chunk, to_read, 0);
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
                close(fd);
            }
            return Value::unit();
        },
    });
}

}  // namespace cml::detail
