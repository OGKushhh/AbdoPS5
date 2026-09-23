// SPDX-FileCopyrightText: Copyright 2026 AbDoPS5 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

// Kyty-023: IPC client — implementation.

#include "ipc/ipcClient.h"

#include "common/logging/log.h"
#include "graphics/presentation/screenshot.h"

#include <cstring>
#include <memory>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
using socket_t = SOCKET;
constexpr socket_t INVALID_SOCKET_VALUE = INVALID_SOCKET;
constexpr int SOCKET_ERROR_VALUE = SOCKET_ERROR;
inline void close_socket(socket_t s) { closesocket(s); }
#else
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <arpa/inet.h>
using socket_t = int;
constexpr socket_t INVALID_SOCKET_VALUE = -1;
constexpr int SOCKET_ERROR_VALUE = -1;
inline void close_socket(socket_t s) { ::close(s); }
#endif

namespace Libs::Ipc {

// ============================================================================
// JSON helpers (minimal — avoids nlohmann_json dependency for IPC)
// ============================================================================

std::string GetJsonField(const std::string& json, const std::string& key) {
    // Very simple JSON field extraction: looks for "key":"value" or "key":value
    std::string search = "\"" + key + "\":";
    auto pos = json.find(search);
    if (pos == std::string::npos) return "";

    pos += search.size();
    // Skip whitespace
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) pos++;

    if (pos >= json.size()) return "";

    if (json[pos] == '"') {
        // String value
        pos++;
        auto end = json.find('"', pos);
        if (end == std::string::npos) return "";
        return json.substr(pos, end - pos);
    } else {
        // Numeric/boolean value
        auto end = json.find_first_of(",}", pos);
        if (end == std::string::npos) end = json.size();
        return json.substr(pos, end - pos);
    }
}

const char* CommandToString(IpcCommand cmd) {
    switch (cmd) {
        case IpcCommand::Status:      return "status";
        case IpcCommand::Screenshot:  return "screenshot";
        case IpcCommand::Pause:       return "pause";
        case IpcCommand::Resume:      return "resume";
        case IpcCommand::Quit:        return "quit";
        case IpcCommand::SetGame:     return "set_game";
        case IpcCommand::InputButton: return "input";
        default:                      return "unknown";
    }
}

IpcCommand StringToCommand(const std::string& str) {
    if (str == "status")      return IpcCommand::Status;
    if (str == "screenshot")  return IpcCommand::Screenshot;
    if (str == "pause")       return IpcCommand::Pause;
    if (str == "resume")      return IpcCommand::Resume;
    if (str == "quit")        return IpcCommand::Quit;
    if (str == "set_game")    return IpcCommand::SetGame;
    if (str == "input")       return IpcCommand::InputButton;
    return IpcCommand::Unknown;
}

// ============================================================================
// IpcServer
// ============================================================================

IpcServer::IpcServer() {
#if defined(_WIN32)
    WSADATA wsa_data;
    WSAStartup(MAKEWORD(2, 2), &wsa_data);
#endif
}

IpcServer::~IpcServer() {
    Stop();
#if defined(_WIN32)
    WSACleanup();
#endif
}

bool IpcServer::Start(uint16_t port) {
    m_port = port;

    socket_t sock = static_cast<socket_t>(::socket(AF_INET, SOCK_STREAM, 0));
    if (sock == INVALID_SOCKET_VALUE) {
        LOGF("IPC: failed to create server socket\n");
        return false;
    }

    // Allow address reuse
    int opt = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&opt), sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK); // localhost only
    addr.sin_port = htons(port);

    if (bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR_VALUE) {
        LOGF("IPC: failed to bind to port %u\n", port);
        close_socket(sock);
        return false;
    }

    if (listen(sock, 4) == SOCKET_ERROR_VALUE) {
        LOGF("IPC: failed to listen on port %u\n", port);
        close_socket(sock);
        return false;
    }

    m_server_socket.store(static_cast<int>(sock));
    m_running.store(true);
    m_thread = std::thread(&IpcServer::ServerThread, this);

    LOGF("IPC: server listening on localhost:%u\n", port);
    return true;
}

void IpcServer::Stop() {
    if (!m_running.exchange(false)) return;

    if (m_server_socket.load() >= 0) {
        close_socket(static_cast<socket_t>(m_server_socket.load()));
        m_server_socket.store(-1);
    }

    if (m_thread.joinable()) {
        m_thread.join();
    }
}

void IpcServer::RegisterHandler(IpcCommand cmd, CommandHandler handler) {
    std::lock_guard lock(m_handlers_mutex);
    m_handlers[static_cast<int>(cmd)] = std::move(handler);
}

void IpcServer::ServerThread() {
    while (m_running.load()) {
        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);
        socket_t client = static_cast<socket_t>(
            accept(static_cast<socket_t>(m_server_socket.load()),
                   reinterpret_cast<sockaddr*>(&client_addr), &client_len));

        if (client == INVALID_SOCKET_VALUE) {
            if (m_running.load()) continue;
            break;
        }

        HandleClient(static_cast<int>(client));
        close_socket(client);
    }
}

void IpcServer::HandleClient(int client_socket) {
    char buffer[4096] = {};
    std::string accumulated;

    while (m_running.load()) {
        int bytes = recv(static_cast<socket_t>(client_socket), buffer, sizeof(buffer) - 1, 0);
        if (bytes <= 0) break;

        accumulated.append(buffer, bytes);

        // Process complete lines (newline-delimited JSON)
        size_t pos;
        while ((pos = accumulated.find('\n')) != std::string::npos) {
            std::string line = accumulated.substr(0, pos);
            accumulated.erase(0, pos + 1);

            if (line.empty()) continue;

            // Parse the command
            std::string cmd_str = GetJsonField(line, "cmd");
            IpcCommand cmd = StringToCommand(cmd_str);

            // Dispatch to handler
            IpcResponse response;
            if (cmd == IpcCommand::Unknown) {
                response.ok = false;
                response.message = "unknown command: " + cmd_str;
            } else {
                std::lock_guard lock(m_handlers_mutex);
                auto& handler = m_handlers[static_cast<int>(cmd)];
                if (handler) {
                    response = handler(line);
                } else {
                    response.ok = false;
                    response.message = "no handler for command";
                }
            }

            // Send response
            std::string response_json = "{\"ok\":" + std::string(response.ok ? "true" : "false");
            if (!response.message.empty()) {
                response_json += ",\"message\":\"" + response.message + "\"";
            }
            if (!response.data.empty()) {
                response_json += ",\"data\":" + response.data;
            }
            response_json += "}\n";

            send(static_cast<socket_t>(client_socket), response_json.c_str(),
                 static_cast<int>(response_json.size()), 0);
        }
    }
}

// ============================================================================
// IpcClient
// ============================================================================

IpcClient::IpcClient() {
#if defined(_WIN32)
    WSADATA wsa_data;
    WSAStartup(MAKEWORD(2, 2), &wsa_data);
#endif
}

IpcClient::~IpcClient() {
    Disconnect();
#if defined(_WIN32)
    WSACleanup();
#endif
}

bool IpcClient::Connect(const std::string& host, uint16_t port) {
    m_socket = static_cast<int>(::socket(AF_INET, SOCK_STREAM, 0));
    if (m_socket < 0) return false;

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, host.c_str(), &addr.sin_addr);

    if (connect(static_cast<socket_t>(m_socket), reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        close_socket(static_cast<socket_t>(m_socket));
        m_socket = -1;
        return false;
    }

    return true;
}

void IpcClient::Disconnect() {
    if (m_socket >= 0) {
        close_socket(static_cast<socket_t>(m_socket));
        m_socket = -1;
    }
}

IpcResponse IpcClient::SendCommand(IpcCommand cmd, const std::string& json_params) {
    IpcResponse response;
    if (m_socket < 0) {
        response.ok = false;
        response.message = "not connected";
        return response;
    }

    // Build request JSON
    std::string request = "{\"cmd\":\"" + std::string(CommandToString(cmd)) + "\"";
    // Merge extra params (just append the inner fields)
    if (!json_params.empty() && json_params != "{}") {
        // Strip the outer braces and append
        std::string inner = json_params;
        if (inner.front() == '{') inner = inner.substr(1);
        if (inner.back() == '}') inner.pop_back();
        request += "," + inner;
    }
    request += "}\n";

    // Send
    send(static_cast<socket_t>(m_socket), request.c_str(), static_cast<int>(request.size()), 0);

    // Receive response
    char buffer[4096] = {};
    int bytes = recv(static_cast<socket_t>(m_socket), buffer, sizeof(buffer) - 1, 0);
    if (bytes <= 0) {
        response.ok = false;
        response.message = "connection closed";
        return response;
    }

    std::string response_str(buffer, bytes);
    response.ok = GetJsonField(response_str, "ok") == "true";
    response.message = GetJsonField(response_str, "message");
    response.data = GetJsonField(response_str, "data");
    return response;
}

IpcResponse IpcClient::GetStatus() {
    return SendCommand(IpcCommand::Status);
}

IpcResponse IpcClient::TriggerScreenshot() {
    return SendCommand(IpcCommand::Screenshot);
}

IpcResponse IpcClient::Pause() {
    return SendCommand(IpcCommand::Pause);
}

IpcResponse IpcClient::Resume() {
    return SendCommand(IpcCommand::Resume);
}

IpcResponse IpcClient::Quit() {
    return SendCommand(IpcCommand::Quit);
}

IpcResponse IpcClient::SetGame(const std::string& path) {
    return SendCommand(IpcCommand::SetGame, "{\"path\":\"" + path + "\"}");
}

IpcResponse IpcClient::InjectButton(const std::string& button, bool down) {
    return SendCommand(IpcCommand::InputButton,
                       "{\"button\":\"" + button + "\",\"down\":" + (down ? "true" : "false") + "}");
}

// ============================================================================
// Global accessors
// ============================================================================

namespace {
std::unique_ptr<IpcServer> g_ipc_server;
}

IpcServer* GetIpcServer() {
    return g_ipc_server.get();
}

void InitializeIpcServer(uint16_t port) {
    if (g_ipc_server != nullptr) return;

    g_ipc_server = std::make_unique<IpcServer>();

    // Register default handlers
    g_ipc_server->RegisterHandler(IpcCommand::Status, [](const std::string&) -> IpcResponse {
        return {true, "", "\"running\":true"};
    });

    g_ipc_server->RegisterHandler(IpcCommand::Screenshot, [](const std::string&) -> IpcResponse {
        if (auto* screenshot = Graphics::GetScreenshot()) {
            screenshot->Request();
            return {true, "screenshot requested"};
        }
        return {false, "screenshot module not initialized"};
    });

    g_ipc_server->RegisterHandler(IpcCommand::Quit, [](const std::string&) -> IpcResponse {
        LOGF("IPC: quit command received — exiting\n");
        // Post a quit message to the application
        // (Can't call QApplication::quit() directly from a non-GUI thread,
        // but we can set a flag that the main loop checks.)
        // For now, just log — the actual quit needs platform-specific handling.
        return {true, "quit requested"};
    });

    g_ipc_server->RegisterHandler(IpcCommand::Pause, [](const std::string&) -> IpcResponse {
        return {true, "pause not yet implemented"};
    });

    g_ipc_server->RegisterHandler(IpcCommand::Resume, [](const std::string&) -> IpcResponse {
        return {true, "resume not yet implemented"};
    });

    if (!g_ipc_server->Start(port)) {
        LOGF("IPC: failed to start server on port %u — IPC disabled\n", port);
        g_ipc_server.reset();
        return;
    }
}

} // namespace Libs::Ipc
