// SPDX-FileCopyrightText: Copyright 2026 AbdoPS5 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

// Kyty-023: IPC client — inter-process communication for emulator automation.
//
// Enables external tools (or a second emulator instance) to control the
// emulator via a local socket. Use cases:
//   - Headless automation (CI testing, batch compatibility runs)
//   - Remote control from a web dashboard
//   - Coordinated multi-instance testing
//   - Screenshot/scripted input injection
//
// Protocol: simple newline-delimited JSON over a local TCP socket
// (localhost:28015 by default). The emulator acts as the server when
// --ipc-server is passed, or as a client connecting to an existing
// server when --ipc-client <host:port> is passed.
//
// Messages (client → emulator):
//   {"cmd":"status"}           → {"ok":true,"running":true,"game":"PPSA04288"}
//   {"cmd":"screenshot"}        → {"ok":true,"path":"screenshots/..."}
//   {"cmd":"pause"}             → {"ok":true}
//   {"cmd":"resume"}            → {"ok":true}
//   {"cmd":"quit"}              → {"ok":true} (then exits)
//   {"cmd":"set_game","path":"..."} → {"ok":true}
//   {"cmd":"input","button":"cross","down":true} → {"ok":true}
//
// The protocol is intentionally simple — no authentication, no encryption.
// Only bind to localhost. This is NOT a network-facing service.

#ifndef KYTY_IPC_IPC_CLIENT_H_
#define KYTY_IPC_IPC_CLIENT_H_

#include "common/common.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace Libs::Ipc {

// IPC command types that the emulator can receive.
enum class IpcCommand {
    Status,        // Query emulator status
    Screenshot,    // Trigger a screenshot
    Pause,         // Pause emulation
    Resume,        // Resume emulation
    Quit,          // Quit the emulator
    SetGame,       // Set the game path
    InputButton,   // Inject a controller button press
    Unknown,
};

// IPC response structure.
struct IpcResponse {
    bool ok = false;
    std::string message;
    std::string data;  // JSON-encoded response data
};

// Callback type for handling IPC commands.
using CommandHandler = std::function<IpcResponse(const std::string& json_params)>;

class IpcServer {
public:
    IpcServer();
    ~IpcServer();

    // Start listening on the given port (default 28015).
    // Returns true on success. Runs in a background thread.
    bool Start(uint16_t port = 28015);

    // Stop the server and close the socket.
    void Stop();

    // Register a handler for a command.
    void RegisterHandler(IpcCommand cmd, CommandHandler handler);

    // Check if the server is running.
    [[nodiscard]] bool IsRunning() const { return m_running.load(); }

    // Get the port the server is listening on.
    [[nodiscard]] uint16_t GetPort() const { return m_port; }

private:
    void ServerThread();
    void HandleClient(int client_socket);

    std::atomic<bool> m_running{false};
    std::atomic<int> m_server_socket{-1};
    uint16_t m_port = 0;
    std::thread m_thread;

    std::mutex m_handlers_mutex;
    CommandHandler m_handlers[static_cast<int>(IpcCommand::Unknown)];
};

class IpcClient {
public:
    IpcClient();
    ~IpcClient();

    // Connect to a server at host:port.
    bool Connect(const std::string& host, uint16_t port);

    // Disconnect from the server.
    void Disconnect();

    // Send a command and wait for the response.
    IpcResponse SendCommand(IpcCommand cmd, const std::string& json_params = "{}");

    // Convenience methods.
    IpcResponse GetStatus();
    IpcResponse TriggerScreenshot();
    IpcResponse Pause();
    IpcResponse Resume();
    IpcResponse Quit();
    IpcResponse SetGame(const std::string& path);
    IpcResponse InjectButton(const std::string& button, bool down);

    [[nodiscard]] bool IsConnected() const { return m_socket >= 0; }

private:
    int m_socket = -1;
};

// Global accessors.
IpcServer* GetIpcServer();
void InitializeIpcServer(uint16_t port = 28015);

// JSON helpers (minimal — no nlohmann_json dependency for the IPC module).
std::string GetJsonField(const std::string& json, const std::string& key);
const char* CommandToString(IpcCommand cmd);
IpcCommand StringToCommand(const std::string& str);

} // namespace Libs::Ipc

#endif // KYTY_IPC_IPC_CLIENT_H_
