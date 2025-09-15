#include "send.hpp"
#include "protocol.hpp"
#include "directory.hpp"
#include <iostream>
#include <fstream>
#include <filesystem>
#include <stdexcept>
#include <thread>
#include <chrono>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace ncp {

class Socket {
private:
    int fd_;
    
public:
    Socket(int fd) : fd_(fd) {}
    ~Socket() { 
#ifdef _WIN32
        closesocket(fd_);
#else
        close(fd_);
#endif
    }
    
    void write(const void* data, size_t size) {
        const char* ptr = static_cast<const char*>(data);
        size_t sent = 0;
        while (sent < size) {
            int result = send(fd_, ptr + sent, size - sent, 0);
            if (result <= 0) throw std::runtime_error("Send failed");
            sent += result;
        }
    }
    
    void read(void* data, size_t size) {
        char* ptr = static_cast<char*>(data);
        size_t received = 0;
        while (received < size) {
            int result = recv(fd_, ptr + received, size - received, 0);
            if (result <= 0) throw std::runtime_error("Receive failed");
            received += result;
        }
    }
};

class SocketStream : public std::iostream {
private:
    class SocketBuf : public std::streambuf {
        Socket& socket_;
        char buffer_[8192];
        
    public:
        SocketBuf(Socket& socket) : socket_(socket) {
            setg(buffer_, buffer_, buffer_);
        }
        
        int underflow() override {
            if (gptr() < egptr()) return traits_type::to_int_type(*gptr());
            
            try {
                socket_.read(buffer_, 1);
                setg(buffer_, buffer_, buffer_ + 1);
                return traits_type::to_int_type(*gptr());
            } catch (...) {
                return traits_type::eof();
            }
        }
        
        int overflow(int c) override {
            if (c != traits_type::eof()) {
                char ch = traits_type::to_char_type(c);
                socket_.write(&ch, 1);
            }
            return c;
        }
        
        int sync() override {
            return 0;
        }
    };
    
    SocketBuf buf_;
    
public:
    SocketStream(Socket& socket) : std::iostream(&buf_), buf_(socket) {}
};

static void wait_for_preflight(SocketStream& stream) {
    uint8_t msg_type = read_message_type(stream);
    read_message_length(stream);
    
    switch (msg_type) {
        case MSG_PREFLIGHT_OK:
            read_preflight_ok(stream);
            break;
        case MSG_PREFLIGHT_FAIL: {
            PreflightFail preflight_fail = read_preflight_fail(stream);
            throw std::runtime_error(preflight_fail.reason);
        }
        default:
            throw std::runtime_error("Unexpected response to Meta message");
    }
}

static void transfer_file_data(SocketStream& stream, const fs::path& file_path, uint64_t file_size) {
    TransferStart transfer_start{file_size};
    write_transfer_start(stream, transfer_start);
    
    std::ifstream file(file_path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("Cannot open file for reading");
    }
    
    std::vector<uint8_t> buffer(8192);
    uint64_t total_sent = 0;
    
    while (total_sent < file_size) {
        uint64_t remaining = file_size - total_sent;
        size_t to_read = std::min(remaining, static_cast<uint64_t>(buffer.size()));
        
        file.read(reinterpret_cast<char*>(buffer.data()), to_read);
        size_t bytes_read = file.gcount();
        if (bytes_read == 0) break;
        
        buffer.resize(bytes_read);
        write_raw_bytes(stream, buffer);
        total_sent += bytes_read;
        
        if (total_sent % (1024 * 1024) == 0 || total_sent == file_size) {
            std::cout << "\rSent: " << total_sent << "/" << file_size << " bytes" << std::flush;
        }
    }
    std::cout << std::endl;
    
    if (total_sent != file_size) {
        throw std::runtime_error("File size mismatch: sent " + std::to_string(total_sent) + 
                                " bytes, expected " + std::to_string(file_size));
    }
    
    // Read transfer result
    uint8_t msg_type = read_message_type(stream);
    read_message_length(stream);
    
    if (msg_type != MSG_TRANSFER_RESULT) {
        throw std::runtime_error("Expected TransferResult message");
    }
    
    TransferResult transfer_result = read_transfer_result(stream);
    if (!transfer_result.ok) {
        throw std::runtime_error("Transfer failed");
    }
}

static void transfer_single_file(SocketStream& stream, const fs::path& src_path) {
    uint64_t file_size = fs::file_size(src_path);
    std::string file_name = src_path.filename().string();
    
    FileMeta file_meta{file_name, file_size, false};
    write_meta(stream, file_meta);
    wait_for_preflight(stream);
    transfer_file_data(stream, src_path, file_size);
}

static void transfer_directory(SocketStream& stream, const fs::path& src_path) {
    std::vector<FileEntry> entries = walk_directory(src_path);
    uint64_t total_size = calculate_total_size(entries);
    
    std::cout << "Directory contains " << entries.size() << " entries, total size: " 
              << total_size << " bytes" << std::endl;
    
    for (const auto& entry : entries) {
        std::cout << "Transferring " << (entry.is_dir ? "directory" : "file") 
                  << ": " << entry.relative_path << std::endl;
        
        FileMeta file_meta{
            entry.relative_path.string(),
            entry.size,
            entry.is_dir
        };
        
        write_meta(stream, file_meta);
        wait_for_preflight(stream);
        
        if (!entry.is_dir) {
            transfer_file_data(stream, entry.path, entry.size);
        }
    }
}

static void handle_send_connection(Socket& socket, const fs::path& src_path, bool is_directory) {
    SocketStream stream(socket);
    
    if (is_directory) {
        transfer_directory(stream, src_path);
    } else {
        transfer_single_file(stream, src_path);
    }
}

static void attempt_transfer(const std::string& host, uint16_t port, 
                           const fs::path& src_path, bool is_directory) {
#ifdef _WIN32
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif
    
    int client_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (client_fd < 0) {
        throw std::runtime_error("Socket creation failed");
    }
    
    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    inet_pton(AF_INET, host.c_str(), &server_addr.sin_addr);
    
    std::cout << "Connecting to " << host << ":" << port << "..." << std::endl;
    
    if (connect(client_fd, reinterpret_cast<sockaddr*>(&server_addr), sizeof(server_addr)) < 0) {
        throw std::runtime_error("Connection failed");
    }
    
    std::cout << "Connection established" << std::endl;
    
    Socket client_socket(client_fd);
    handle_send_connection(client_socket, src_path, is_directory);
    
#ifdef _WIN32
    WSACleanup();
#endif
}

void execute_send(const std::string& host, uint16_t port,
                  const fs::path& src, uint32_t retries,
                  OverwriteMode overwrite_mode) {
    (void)overwrite_mode; // Unused in minimal implementation
    
    if (!fs::exists(src)) {
        throw std::runtime_error("Source path does not exist: " + src.string());
    }
    
    bool is_directory = fs::is_directory(src);
    std::cout << "Source is " << (is_directory ? "directory" : "file") << ": " << src << std::endl;
    
    std::exception_ptr last_error;
    
    for (uint32_t attempt = 1; attempt <= retries; ++attempt) {
        std::cout << "Attempt " << attempt << "/" << retries << std::endl;
        
        try {
            attempt_transfer(host, port, src, is_directory);
            std::cout << "Transfer completed successfully" << std::endl;
            return;
        } catch (const std::exception& e) {
            std::cerr << "Attempt " << attempt << " failed: " << e.what() << std::endl;
            last_error = std::current_exception();
            
            if (attempt < retries) {
                std::cout << "Retrying in 1 second..." << std::endl;
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
        }
    }
    
    if (last_error) {
        std::rethrow_exception(last_error);
    } else {
        throw std::runtime_error("Transfer failed");
    }
}

void execute_send_listen(uint16_t port, const fs::path& src, OverwriteMode overwrite_mode) {
    (void)overwrite_mode; // Unused in minimal implementation
    
    if (!fs::exists(src)) {
        throw std::runtime_error("Source path does not exist: " + src.string());
    }
    
    bool is_directory = fs::is_directory(src);
    std::cout << "Source is " << (is_directory ? "directory" : "file") << ": " << src << std::endl;
    
#ifdef _WIN32
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif
    
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        throw std::runtime_error("Socket creation failed");
    }
    
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&opt), sizeof(opt));
    
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);
    
    if (bind(server_fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0) {
        throw std::runtime_error("Bind failed");
    }
    
    if (listen(server_fd, 1) < 0) {
        throw std::runtime_error("Listen failed");
    }
    
    std::cout << "Listening on port " << port << " (send mode)" << std::endl;
    
    sockaddr_in client_addr{};
    socklen_t client_len = sizeof(client_addr);
    int client_fd = accept(server_fd, reinterpret_cast<sockaddr*>(&client_addr), &client_len);
    
    if (client_fd < 0) {
        throw std::runtime_error("Accept failed");
    }
    
    std::cout << "Connection from: " << inet_ntoa(client_addr.sin_addr) 
              << ":" << ntohs(client_addr.sin_port) << std::endl;
    
    Socket client_socket(client_fd);
    handle_send_connection(client_socket, src, is_directory);
    
    std::cout << "Transfer completed successfully" << std::endl;
    
#ifdef _WIN32
    closesocket(server_fd);
    WSACleanup();
#else
    close(server_fd);
#endif
}

} // namespace ncp