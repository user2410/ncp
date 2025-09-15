#include "send.hpp"
#include "recv.hpp"
#include <iostream>
#include <string>
#include <vector>
#include <filesystem>
#include <cstdlib>

namespace fs = std::filesystem;

struct Args {
    uint8_t verbose = 0;
    enum class CommandType { Send, Recv } command_type;
    
    // Send args
    std::string host;
    uint16_t port = 0;
    uint32_t retries = 3;
    ncp::OverwriteMode overwrite = ncp::OverwriteMode::Ask;
    bool listen = false;
    fs::path src_or_dst;
};

static void print_help() {
    std::cout << "ncp 0.1.0 - Minimal file transfer over TCP\n\n";
    std::cout << "USAGE:\n";
    std::cout << "    ncp [-v|-vv] send --host <HOST> --port <PORT> [OPTIONS] <SRC>\n";
    std::cout << "    ncp [-v|-vv] send --listen --port <PORT> [OPTIONS] <SRC>\n";
    std::cout << "    ncp [-v|-vv] recv --port <PORT> [OPTIONS] <DST>\n";
    std::cout << "    ncp [-v|-vv] recv --host <HOST> --port <PORT> [OPTIONS] <DST>\n";
    std::cout << "    ncp [-v|-vv] recv --listen --port <PORT> [OPTIONS] <DST>\n\n";
    std::cout << "OPTIONS:\n";
    std::cout << "    -v, -vv          Increase verbosity\n";
    std::cout << "    --host <HOST>    Target/bind host (auto-enables connect mode for recv)\n";
    std::cout << "    --port <PORT>    Port number\n";
    std::cout << "    --listen, -l     Listen mode (send and recv)\n";
    std::cout << "    --retries <N>    Retry attempts (send only, default: 3)\n";
    std::cout << "    --overwrite <M>  Overwrite mode: ask, yes, no (default: ask)\n";
    std::cout << "    -h, --help       Show this help\n";
}

static ncp::OverwriteMode parse_overwrite_mode(const std::string& mode) {
    if (mode == "ask") return ncp::OverwriteMode::Ask;
    if (mode == "yes") return ncp::OverwriteMode::Yes;
    if (mode == "no") return ncp::OverwriteMode::No;
    throw std::runtime_error("Invalid overwrite mode");
}

static Args parse_send_args(const std::vector<std::string>& args) {
    Args result;
    result.command_type = Args::CommandType::Send;
    
    bool host_specified = false;
    bool src_specified = false;
    
    for (size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "--host") {
            if (++i >= args.size()) throw std::runtime_error("--host requires value");
            result.host = args[i];
            host_specified = true;
        } else if (args[i] == "--port") {
            if (++i >= args.size()) throw std::runtime_error("--port requires value");
            result.port = std::stoi(args[i]);
        } else if (args[i] == "--retries") {
            if (++i >= args.size()) throw std::runtime_error("--retries requires value");
            result.retries = std::stoi(args[i]);
        } else if (args[i] == "--overwrite") {
            if (++i >= args.size()) throw std::runtime_error("--overwrite requires value");
            result.overwrite = parse_overwrite_mode(args[i]);
        } else if (args[i] == "--listen" || args[i] == "-l") {
            result.listen = true;
        } else if (args[i].empty() || args[i][0] != '-') {
            result.src_or_dst = args[i];
            src_specified = true;
        } else {
            throw std::runtime_error("Unknown option: " + args[i]);
        }
    }
    
    if (!result.listen && !host_specified) {
        throw std::runtime_error("--host required (or use --listen)");
    }
    if (result.port == 0) throw std::runtime_error("--port required");
    if (!src_specified) throw std::runtime_error("source path required");
    
    return result;
}

static Args parse_recv_args(const std::vector<std::string>& args) {
    Args result;
    result.command_type = Args::CommandType::Recv;
    result.host = "0.0.0.0";
    
    bool host_specified = false;
    bool dst_specified = false;
    
    for (size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "--host") {
            if (++i >= args.size()) throw std::runtime_error("--host requires value");
            result.host = args[i];
            host_specified = true;
        } else if (args[i] == "--port") {
            if (++i >= args.size()) throw std::runtime_error("--port requires value");
            result.port = std::stoi(args[i]);
        } else if (args[i] == "--overwrite") {
            if (++i >= args.size()) throw std::runtime_error("--overwrite requires value");
            result.overwrite = parse_overwrite_mode(args[i]);
        } else if (args[i] == "--listen" || args[i] == "-l") {
            result.listen = true;
        } else if (args[i].empty() || args[i][0] != '-') {
            result.src_or_dst = args[i];
            dst_specified = true;
        } else {
            throw std::runtime_error("Unknown option: " + args[i]);
        }
    }
    
    // Logic: --listen forces listen mode, --host forces connect mode, default is listen
    if (!result.listen && !host_specified) {
        result.listen = true;
    }
    
    if (result.port == 0) throw std::runtime_error("--port required");
    if (!dst_specified) throw std::runtime_error("destination path required");
    
    return result;
}

static Args parse_args(int argc, char* argv[]) {
    if (argc < 2) {
        throw std::runtime_error("Usage: ncp [send|recv] [options]");
    }
    
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i) {
        args.emplace_back(argv[i]);
    }
    
    Args result;
    size_t i = 0;
    
    // Parse global options
    while (i < args.size() && !args[i].empty() && args[i][0] == '-' && args[i] != "--") {
        if (args[i] == "-v") {
            result.verbose = 1;
        } else if (args[i] == "-vv") {
            result.verbose = 2;
        } else if (args[i] == "--help" || args[i] == "-h") {
            print_help();
            std::exit(0);
        } else {
            break;
        }
        ++i;
    }
    
    if (i >= args.size()) {
        throw std::runtime_error("Missing command");
    }
    
    std::vector<std::string> cmd_args(args.begin() + i + 1, args.end());
    
    if (args[i] == "send") {
        Args send_args = parse_send_args(cmd_args);
        result.command_type = send_args.command_type;
        result.host = send_args.host;
        result.port = send_args.port;
        result.retries = send_args.retries;
        result.overwrite = send_args.overwrite;
        result.listen = send_args.listen;
        result.src_or_dst = send_args.src_or_dst;
    } else if (args[i] == "recv") {
        Args recv_args = parse_recv_args(cmd_args);
        result.command_type = recv_args.command_type;
        result.host = recv_args.host;
        result.port = recv_args.port;
        result.overwrite = recv_args.overwrite;
        result.listen = recv_args.listen;
        result.src_or_dst = recv_args.src_or_dst;
    } else {
        throw std::runtime_error("Unknown command: " + args[i]);
    }
    
    return result;
}

int main(int argc, char* argv[]) {
    try {
        Args args = parse_args(argc, argv);
        
        if (args.verbose >= 1) {
            std::cerr << "[INFO] Starting ncp with verbosity level " << static_cast<int>(args.verbose) << std::endl;
        }
        
        if (args.command_type == Args::CommandType::Send) {
            if (args.verbose >= 2) {
                if (args.listen) {
                    std::cerr << "[DEBUG] Executing send listen command: port " << args.port 
                              << " -> " << args.src_or_dst << std::endl;
                } else {
                    std::cerr << "[DEBUG] Executing send command: " << args.host << ":" << args.port 
                              << " -> " << args.src_or_dst << std::endl;
                }
            }
            
            if (args.listen) {
                ncp::execute_send_listen(args.port, args.src_or_dst, args.overwrite);
            } else {
                ncp::execute_send(args.host, args.port, args.src_or_dst, args.retries, args.overwrite);
            }
        } else {
            if (args.verbose >= 2) {
                if (args.listen) {
                    std::cerr << "[DEBUG] Executing recv listen command: " << args.host << ":" << args.port 
                              << " -> " << args.src_or_dst << std::endl;
                } else {
                    std::cerr << "[DEBUG] Executing recv connect command: " << args.host << ":" << args.port 
                              << " -> " << args.src_or_dst << std::endl;
                }
            }
            
            if (args.listen) {
                ncp::execute(args.host, args.port, args.src_or_dst, args.overwrite);
            } else {
                ncp::execute_connect(args.host, args.port, args.src_or_dst, args.overwrite);
            }
        }
        
        if (args.verbose >= 1) {
            std::cerr << "[INFO] Operation completed successfully" << std::endl;
        }
        
        return 0;
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}