#include "recv.hpp"
#include "protocol.hpp"
#include "diskspace.hpp"
#include <iostream>
#include <fstream>
#include <filesystem>
#include <stdexcept>

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

static fs::path determine_final_path(const fs::path& dst_path, const std::string& file_name, bool is_dir) {
    if (fs::is_directory(dst_path)) {
        return dst_path / file_name;
    } else if (fs::exists(dst_path)) {
        if (is_dir) {
            throw std::runtime_error("Cannot receive directory to existing file");
        }
        return dst_path;
    } else {
        return dst_path;
    }
}

static bool prompt_overwrite(const fs::path& path) {
    std::cout << "File " << path << " already exists. Overwrite? (y/N): ";
    std::string input;
    std::getline(std::cin, input);
    
    std::transform(input.begin(), input.end(), input.begin(), ::tolower);
    return input == "y" || input == "yes";
}

static void handle_directory_entry(SocketStream& stream, const fs::path& final_path, OverwriteMode overwrite_mode) {
    if (!fs::exists(final_path)) {
        fs::create_directories(final_path);
    } else {
        switch (overwrite_mode) {
            case OverwriteMode::Ask:
                if (!prompt_overwrite(final_path)) {
                    PreflightFail preflight_fail{"User declined directory overwrite"};
                    write_preflight_fail(stream, preflight_fail);
                    return;
                }
                break;
            case OverwriteMode::No:
                break;
            case OverwriteMode::Yes:
                break;
        }
    }
    
    PreflightOk preflight_ok{0};
    write_preflight_ok(stream, preflight_ok);
}

static void handle_file_entry(SocketStream& stream, const fs::path& final_path, 
                             const FileMeta& file_meta, OverwriteMode overwrite_mode) {
    if (fs::exists(final_path)) {
        switch (overwrite_mode) {
            case OverwriteMode::Ask:
                if (!prompt_overwrite(final_path)) {
                    PreflightFail preflight_fail{"User declined overwrite"};
                    write_preflight_fail(stream, preflight_fail);
                    return;
                }
                break;
            case OverwriteMode::No: {
                PreflightFail preflight_fail{"File exists, skipping"};
                write_preflight_fail(stream, preflight_fail);
                return;
            }
            case OverwriteMode::Yes:
                break;
        }
    }
    
    // Create parent directory
    if (final_path.has_parent_path()) {
        fs::create_directories(final_path.parent_path());
    }
    
    // Check disk space
    uint64_t available_space = get_available_space(final_path);
    bool has_enough_space = check_disk_space(final_path, file_meta.size);
    
    if (!has_enough_space) {
        std::string error_msg = "Insufficient disk space. Need: " + 
                               format_bytes(file_meta.size) + ", Available: " + 
                               format_bytes(available_space);
        PreflightFail preflight_fail{error_msg};
        write_preflight_fail(stream, preflight_fail);
        throw std::runtime_error("Insufficient disk space");
    }
    
    PreflightOk preflight_ok{available_space};
    write_preflight_ok(stream, preflight_ok);
    
    // Read transfer start
    uint8_t msg_type = read_message_type(stream);
    read_message_length(stream);
    
    if (msg_type != MSG_TRANSFER_START) {
        throw std::runtime_error("Expected TransferStart message");
    }
    
    TransferStart transfer_start = read_transfer_start(stream);
    
    // Receive file data
    fs::path temp_path = final_path;
    temp_path += ".ncp_temp";
    
    std::ofstream temp_file(temp_path, std::ios::binary);
    if (!temp_file) {
        throw std::runtime_error("Cannot create temporary file");
    }
    
    uint64_t total_bytes = 0;
    std::vector<uint8_t> buffer(8192);
    uint64_t file_size = transfer_start.file_size;
    
    while (total_bytes < file_size) {
        uint64_t remaining = file_size - total_bytes;
        size_t to_read = std::min(remaining, static_cast<uint64_t>(buffer.size()));
        
        buffer.resize(to_read);
        read_exact_bytes(stream, buffer);
        temp_file.write(reinterpret_cast<const char*>(buffer.data()), to_read);
        
        total_bytes += to_read;
        
        if (total_bytes % (1024 * 1024) == 0 || total_bytes == file_size) {
            std::cout << "\rReceived: " << total_bytes << "/" << file_size << " bytes" << std::flush;
        }
    }
    std::cout << std::endl;
    
    temp_file.close();
    fs::rename(temp_path, final_path);
    
    TransferResult transfer_result{true, total_bytes};
    write_transfer_result(stream, transfer_result);
}

static void handle_connection(Socket& socket, const fs::path& dst_path, OverwriteMode overwrite_mode) {
    SocketStream stream(socket);
    
    while (true) {
        try {
            uint8_t msg_type = read_message_type(stream);
            if (stream.eof()) break;
            
            if (msg_type != MSG_META) {
                throw std::runtime_error("Expected Meta message");
            }
            
            read_message_length(stream);
            FileMeta file_meta = read_meta(stream);
            
            fs::path final_path = determine_final_path(dst_path, file_meta.name, file_meta.is_dir);
            
            std::cout << "Receiving " << (file_meta.is_dir ? "directory" : "file") 
                      << ": " << file_meta.name << " (" << file_meta.size << " bytes) to " 
                      << final_path << std::endl;
            
            if (file_meta.is_dir) {
                handle_directory_entry(stream, final_path, overwrite_mode);
            } else {
                handle_file_entry(stream, final_path, file_meta, overwrite_mode);
            }
        } catch (const std::exception& e) {
            std::cerr << "Transfer failed: " << e.what() << std::endl;
            throw;
        }
    }
}

void execute(const std::string& host, uint16_t port, 
             const fs::path& dst, OverwriteMode overwrite_mode) {
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
    address.sin_port = htons(port);
    inet_pton(AF_INET, host.c_str(), &address.sin_addr);
    
    if (bind(server_fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0) {
        throw std::runtime_error("Bind failed");
    }
    
    if (listen(server_fd, 1) < 0) {
        throw std::runtime_error("Listen failed");
    }
    
    std::cout << "Listening on port " << port << std::endl;
    
    sockaddr_in client_addr{};
    socklen_t client_len = sizeof(client_addr);
    int client_fd = accept(server_fd, reinterpret_cast<sockaddr*>(&client_addr), &client_len);
    
    if (client_fd < 0) {
        throw std::runtime_error("Accept failed");
    }
    
    std::cout << "Connection from: " << inet_ntoa(client_addr.sin_addr) 
              << ":" << ntohs(client_addr.sin_port) << std::endl;
    
    Socket client_socket(client_fd);
    handle_connection(client_socket, dst, overwrite_mode);
    
    std::cout << "Transfer completed successfully" << std::endl;
    
#ifdef _WIN32
    closesocket(server_fd);
    WSACleanup();
#else
    close(server_fd);
#endif
}

void execute_connect(const std::string& host, uint16_t port,
                    const fs::path& dst, OverwriteMode overwrite_mode) {
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
    handle_connection(client_socket, dst, overwrite_mode);
    
    std::cout << "Transfer completed successfully" << std::endl;
    
#ifdef _WIN32
    WSACleanup();
#endif
}

} // namespace ncp