#include "TcpChat.h"
#include <Windows.h>
#include <chrono>
#include <iostream>
#include <stdexcept>

struct EventLog {
    std::mutex mutex;
    std::condition_variable wake;
    std::vector<NetEvent> events;
    void Add(const NetEvent& event) {
        std::lock_guard<std::mutex> lock(mutex);
        events.push_back(event);
        wake.notify_all();
    }
    bool Wait(EventType type, const std::string& text = {}) {
        std::unique_lock<std::mutex> lock(mutex);
        return wake.wait_for(lock, std::chrono::seconds(5), [&] {
            for (const auto& event : events)
                if (event.type == type && (text.empty() || event.text == text)) return true;
            return false;
        });
    }
    uint64_t ClientId() {
        std::lock_guard<std::mutex> lock(mutex);
        for (const auto& event : events) if (event.type == EventType::Connected) return event.clientId;
        return 0;
    }
};

static void Check(bool result, const char* name) {
    if (!result) throw std::runtime_error(name);
}

struct TestSocket {
    SOCKET value = INVALID_SOCKET;
    explicit TestSocket(unsigned short port) {
        value = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(port);
        inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);
        if (connect(value, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
            closesocket(value); value = INVALID_SOCKET;
            throw std::runtime_error("test connect failed");
        }
        SetNonBlocking(value);
    }
    ~TestSocket() { if (value != INVALID_SOCKET) closesocket(value); }
};

int main() {
    WSADATA data{};
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) return 1;
    int result = 0;
    try {
        EventLog serverLog, clientLog;
        TcpServer server;
        int error = 0;
        Check(server.Start(0, "Server", [&](const NetEvent& e) { serverLog.Add(e); }, error), "start server");
        TcpClient client;
        client.Start("127.0.0.1", server.Port(), "Client", [&](const NetEvent& e) { clientLog.Add(e); });
        Check(clientLog.Wait(EventType::Connected), "client connected");
        Check(serverLog.Wait(EventType::Connected), "server connected");
        std::string unicode = ToUtf8(L"Xin chào tiếng Việt 👋\r\nDòng thứ hai");
        std::string longText(150000, 'x');
        longText += unicode;
        Check(client.QueueMessage(unicode), "queue unicode");
        Check(client.QueueMessage(longText), "queue long text");
        Check(server.QueueMessage(serverLog.ClientId(), "server sends first"), "server queue");
        Check(clientLog.Wait(EventType::Received, "server sends first"), "server -> client");
        Check(serverLog.Wait(EventType::Sent, "server sends first"), "server text acknowledged");
        Check(serverLog.Wait(EventType::Received, unicode), "unicode received");
        Check(serverLog.Wait(EventType::Received, longText), "long text received intact");
        Check(clientLog.Wait(EventType::Sent, longText), "client text acknowledged");
        std::cout << "PASS: two-way Unicode, 150KB message, queued sends and acknowledgements\n";

        std::atomic<bool> stop{ false };
        {
            TestSocket raw(server.Port());
            auto hello = EncodePacket({ PacketType::Hello, "Fragment", {} });
            // Gửi từng byte để kiểm tra ghép header và nội dung.
            for (char byte : hello) Check(SendAll(raw.value, &byte, 1, stop), "fragmented send");
            Packet response;
            Check(RecvPacket(raw.value, response, stop) == ReadResult::Ok &&
                response.type == PacketType::Hello, "fragmented hello");
            auto first = EncodePacket({ PacketType::Chat, "Fragment", "merged-one" });
            auto second = EncodePacket({ PacketType::Chat, "Fragment", "merged-two" });
            first.insert(first.end(), second.begin(), second.end());
            Check(SendAll(raw.value, first.data(), first.size(), stop), "coalesced send");
            Check(RecvPacket(raw.value, response, stop) == ReadResult::Ok, "first response");
            Check(RecvPacket(raw.value, response, stop) == ReadResult::Ok, "second response");
            Check(serverLog.Wait(EventType::Received, "merged-two"), "coalesced requests decoded");
            shutdown(raw.value, SD_SEND);
        }
        std::cout << "PASS: fragmented header/body and coalesced requests\n";
        {
            TestSocket raw(server.Port());
            uint32_t invalidSize = htonl(MAX_PAYLOAD_BYTES + 1);
            Check(SendAll(raw.value, &invalidSize, 4, stop), "oversized header send");
            char byte;
            Check(RecvExact(raw.value, &byte, 1, stop) != ReadResult::Ok, "reject oversized frame");
        }
        {
            TestSocket raw(server.Port());
            const char partial[] = { 0, 0 };
            Check(SendAll(raw.value, partial, 2, stop), "partial header");
            shutdown(raw.value, SD_SEND);
        }
        client.Stop();
        Check(clientLog.Wait(EventType::Disconnected), "client normal close");
        {
            TestSocket silent(server.Port());
            auto begin = std::chrono::steady_clock::now();
            server.Stop();
            Check(std::chrono::steady_clock::now() - begin < std::chrono::seconds(2), "stop blocked worker");
        }
        std::cout << "PASS: oversized/truncated frames, graceful close and stop while recv waits\n";
        EventLog restartedLog;
        Check(server.Start(0, "Restart", [&](const NetEvent& e) { restartedLog.Add(e); }, error), "restart server");
        TcpServer conflict;
        Check(!conflict.Start(server.Port(), "Other", [](const NetEvent&) {}, error), "bind conflict");
        client.Start("127.0.0.1", server.Port(), "Again", [&](const NetEvent& e) { restartedLog.Add(e); });
        Check(restartedLog.Wait(EventType::Connected), "reconnect");
        server.Stop();
        Check(restartedLog.Wait(EventType::Disconnected), "server close detected");
        client.Stop();
        std::cout << "PASS: reconnect, bind failure and server disconnect\n";
    }
    catch (const std::exception& ex) { std::cerr << "FAIL: " << ex.what() << '\n'; result = 1; }
    WSACleanup();
    return result;
}
