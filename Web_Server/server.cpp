#include <iostream>
#include <string>
#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional> // For std::function
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <cstring>
#include <cerrno>
#include <atomic> // For shutdown flag
#include <sstream> // For parsing the request line
#include <vector>  // For storing parsed parts
#include <fstream> // For file I/O
#include <filesystem> // For file size and path manipulation (C++17)
#include <map>     // For MIME types

// --- Configuration ---
const int PORT = 8080;
const int MAX_EVENTS = 100;
const int MAX_CONN = 1024;
const int NUM_WORKER_THREADS = 4; // Or std::thread::hardware_concurrency();
const std::string WEB_ROOT = "./public_html"; // Directory to serve files from

// --- MIME Type Mapping ---
std::map<std::string, std::string> get_mime_types() {
    std::map<std::string, std::string> mime_types;
    mime_types[".html"] = "text/html";
    mime_types[".htm"] = "text/html";
    mime_types[".css"] = "text/css";
    mime_types[".js"] = "application/javascript";
    mime_types[".jpg"] = "image/jpeg";
    mime_types[".jpeg"] = "image/jpeg";
    mime_types[".png"] = "image/png";
    mime_types[".gif"] = "image/gif";
    mime_types[".txt"] = "text/plain";
    // Add more common types as needed
    return mime_types;
}
const std::map<std::string, std::string> MIME_TYPES = get_mime_types();

std::string get_content_type(const std::string& path) {
    std::filesystem::path fs_path(path);
    std::string ext = fs_path.extension().string();
    auto it = MIME_TYPES.find(ext);
    if (it != MIME_TYPES.end()) {
        return it->second;
    }
    return "application/octet-stream"; // Default binary type
}


// --- Thread-Safe Queue (Unchanged) ---
template<typename T>
class ThreadSafeQueue {
private:
    std::queue<T> queue_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::atomic<bool> shutdown_{false};

public:
    void push(T value) {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.push(std::move(value));
        cv_.notify_one();
    }
    bool pop(T& value) {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [this]{ return !queue_.empty() || shutdown_; });
        if (shutdown_ && queue_.empty()) return false;
        value = std::move(queue_.front());
        queue_.pop();
        return true;
    }
    void signal_shutdown() {
        shutdown_ = true;
        cv_.notify_all();
    }
};

ThreadSafeQueue<int> task_queue;

// --- Helper: Set Non-Blocking (Unchanged) ---
bool set_non_blocking(int sock_fd) {
    int flags = fcntl(sock_fd, F_GETFL, 0);
    if (flags == -1) { perror("fcntl F_GETFL"); return false; }
    if (fcntl(sock_fd, F_SETFL, flags | O_NONBLOCK) == -1) { perror("fcntl F_SETFL O_NONBLOCK"); return false; }
    return true;
}

// --- Helper: Send HTTP Response ---
// Simplified write - does not handle non-blocking write fully
void send_response(int client_fd, const std::string& status_line, const std::map<std::string, std::string>& headers, const std::string& body) {
    std::ostringstream response_stream;
    response_stream << status_line << "\r\n";
    for (const auto& pair : headers) {
        response_stream << pair.first << ": " << pair.second << "\r\n";
    }
    response_stream << "\r\n"; // End of headers
    response_stream << body;

    std::string response_str = response_stream.str();
    write(client_fd, response_str.c_str(), response_str.length());
}

// --- Client Handling Function (Now Serves Files) ---
void handle_client_request(int client_fd, int epoll_fd) {
    char buffer[4096] = {0};
    std::string request_method;
    std::string request_uri;
    std::string http_version;
    bool request_line_parsed = false;
    int total_bytes_read = 0;
    bool connection_active = true;
    bool keep_alive = false; // Basic Keep-Alive handling

    // Read loop for request line (Simplified)
    while (connection_active && !request_line_parsed) {
        int bytes_read = read(client_fd, buffer + total_bytes_read, sizeof(buffer) - 1 - total_bytes_read);
        // ... (Error handling for read: EAGAIN, 0, -1 - same as before) ...
         if (bytes_read == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            else { perror("read failed"); connection_active = false; break; }
        } else if (bytes_read == 0) {
             std::cout << "[Worker " << std::this_thread::get_id() << "] Client disconnected cleanly before request line: fd=" << client_fd << std::endl;
             connection_active = false; break;
        } else {
            total_bytes_read += bytes_read;
            char* end_of_line = strstr(buffer, "\r\n");
            if (end_of_line) {
                std::string request_line(buffer, end_of_line - buffer);
                std::stringstream ss(request_line);
                if (ss >> request_method >> request_uri >> http_version) {
                    request_line_parsed = true;
                    // Basic Keep-Alive check (very simplified)
                    if (http_version == "HTTP/1.1") {
                        keep_alive = true; // Assume keep-alive for HTTP/1.1 by default
                        // A real server would parse Connection: header
                    }
                } else {
                     std::cerr << "[Worker " << std::this_thread::get_id() << "] Failed to parse request line: '" << request_line << "'" << std::endl;
                     send_response(client_fd, "HTTP/1.1 400 Bad Request", {{"Content-Length", "0"}, {"Connection", "close"}}, "");
                     connection_active = false; break;
                }
            } else if (total_bytes_read >= sizeof(buffer) - 1) {
                 std::cerr << "[Worker " << std::this_thread::get_id() << "] Read buffer full, request line too long or invalid: fd=" << client_fd << std::endl;
                 send_response(client_fd, "HTTP/1.1 400 Bad Request", {{"Content-Length", "0"}, {"Connection", "close"}}, "");
                 connection_active = false; break;
            }
        }
    } // End read loop

    if (connection_active && request_line_parsed) {
        if (request_method == "GET") {
            // --- File Serving Logic ---
            std::string file_path_str = WEB_ROOT;
            if (request_uri == "/") {
                file_path_str += "/index.html"; // Default file
            } else {
                file_path_str += request_uri;
            }

            // Basic path sanitization to prevent directory traversal
            if (file_path_str.find("..") != std::string::npos) {
                send_response(client_fd, "HTTP/1.1 403 Forbidden", {{"Content-Length", "0"}, {"Connection", "close"}}, "");
                connection_active = false;
            } else {
                std::filesystem::path file_path = file_path_str;
                std::error_code ec;
                if (std::filesystem::exists(file_path, ec) && !std::filesystem::is_directory(file_path, ec)) {
                    std::ifstream file_stream(file_path, std::ios::binary | std::ios::ate); // Open at the end to get size
                    if (file_stream) {
                        std::streamsize file_size = file_stream.tellg();
                        file_stream.seekg(0, std::ios::beg); // Go back to the beginning

                        std::string content_type = get_content_type(file_path_str);
                        std::map<std::string, std::string> headers = {
                            {"Content-Type", content_type},
                            {"Content-Length", std::to_string(file_size)},
                            {"Connection", (keep_alive ? "keep-alive" : "close")}
                        };

                        // Send headers
                        std::ostringstream header_stream;
                        header_stream << "HTTP/1.1 200 OK\r\n";
                        for (const auto& pair : headers) {
                            header_stream << pair.first << ": " << pair.second << "\r\n";
                        }
                        header_stream << "\r\n";
                        std::string header_str = header_stream.str();
                        write(client_fd, header_str.c_str(), header_str.length());

                        // Send file content in chunks
                        char file_buffer[4096];
                        while (file_stream.read(file_buffer, sizeof(file_buffer)) || file_stream.gcount() > 0) {
                            // Note: This simple write() doesn't handle non-blocking sockets properly.
                            // A production server needs EPOLLOUT handling for large files.
                            write(client_fd, file_buffer, file_stream.gcount());
                        }
                        std::cout << "[Worker " << std::this_thread::get_id() << "] Served file: " << file_path_str << " to fd=" << client_fd << std::endl;

                    } else {
                        // Error opening file (e.g., permissions)
                        send_response(client_fd, "HTTP/1.1 500 Internal Server Error", {{"Content-Length", "0"}, {"Connection", "close"}}, "");
                        connection_active = false;
                    }
                } else {
                    // File not found or is a directory
                    send_response(client_fd, "HTTP/1.1 404 Not Found", {{"Content-Length", "0"}, {"Connection", "close"}}, "");
                    connection_active = false;
                }
            } // End path sanitization check
        } else {
            // Method not allowed (only support GET for now)
            send_response(client_fd, "HTTP/1.1 405 Method Not Allowed", {{"Content-Length", "0"}, {"Connection", "close"}}, "");
            connection_active = false;
        } // End method check
    } // End parsed check

    if (!connection_active || !keep_alive) {
        epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_fd, NULL);
        close(client_fd);
        std::cout << "[Worker " << std::this_thread::get_id() << "] Connection closed: fd=" << client_fd << std::endl;
    } else {
        // Basic Keep-Alive: Re-register for next request
        std::cout << "[Worker " << std::this_thread::get_id() << "] Connection keep-alive: fd=" << client_fd << ", re-registering." << std::endl;
        epoll_event event;
        event.events = EPOLLIN | EPOLLET; // Re-arm edge trigger
        event.data.fd = client_fd;
        if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &event) == -1) { // Use ADD as we DEL'd in main loop
             perror("epoll_ctl re-add client_fd failed");
             epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_fd, NULL); // Ensure removal on error
             close(client_fd); // Close if re-add fails
        }
    }
}

// --- Worker Thread Loop (Unchanged) ---
void worker_loop(int epoll_fd) {
    while (true) {
        int client_fd;
        if (!task_queue.pop(client_fd)) break;
        handle_client_request(client_fd, epoll_fd);
    }
    std::cout << "Worker thread " << std::this_thread::get_id() << " shutting down." << std::endl;
}

// --- Main Server Setup and Event Loop (Unchanged from Step 4) ---
int main() {
    // 1. Create, bind, listen...
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) { perror("socket failed"); return 1; }
    int reuse = 1; setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);
    if (bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) { perror("bind failed"); close(server_fd); return 1; }
    if (!set_non_blocking(server_fd)) { close(server_fd); return 1; }
    if (listen(server_fd, MAX_CONN) < 0) { perror("listen failed"); close(server_fd); return 1; }

    // 2. Create epoll instance...
    int epoll_fd = epoll_create1(0);
    if (epoll_fd == -1) { perror("epoll_create1 failed"); close(server_fd); return 1; }

    // 3. Add listening socket to epoll...
    epoll_event event;
    event.events = EPOLLIN;
    event.data.fd = server_fd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &event) == -1) { perror("epoll_ctl add server_fd failed"); close(server_fd); close(epoll_fd); return 1; }

    // 4. Create and launch worker threads...
    std::vector<std::thread> worker_threads;
    unsigned int num_cores = std::thread::hardware_concurrency();
    unsigned int num_workers = (num_cores > 0) ? num_cores : NUM_WORKER_THREADS;
    for (unsigned int i = 0; i < num_workers; ++i) {
        worker_threads.emplace_back(worker_loop, epoll_fd);
        std::cout << "Launched worker thread " << i << std::endl;
    }

    // Create the web root directory if it doesn't exist
    if (!std::filesystem::exists(WEB_ROOT)) {
        std::filesystem::create_directory(WEB_ROOT);
        std::cout << "Created web root directory: " << WEB_ROOT << std::endl;
        // Create a default index.html
        std::ofstream default_index(WEB_ROOT + "/index.html");
        if (default_index) {
            default_index << "<!DOCTYPE html><html><head><title>Test Page</title></head><body><h1>Hello from My Server!</h1></body></html>";
            default_index.close();
        }
    }


    std::vector<epoll_event> events(MAX_EVENTS);
    std::cout << "Server listening on port " << PORT << " with " << num_workers << " workers, serving files from " << WEB_ROOT << "..." << std::endl;

    // --- The Main Event Loop (Unchanged) ---
    while (true) {
        int num_events = epoll_wait(epoll_fd, events.data(), MAX_EVENTS, -1);
        if (num_events == -1) {
             if (errno == EINTR) continue;
             perror("epoll_wait failed");
             break;
        }

        for (int i = 0; i < num_events; ++i) {
            int current_fd = events[i].data.fd;
            uint32_t current_events = events[i].events;

            if (current_fd == server_fd) {
                // Accept new connections... (same as before)
                 while (true) {
                    sockaddr_in client_addr;
                    socklen_t client_len = sizeof(client_addr);
                    int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
                    if (client_fd == -1) {
                         if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                         else { perror("accept failed"); break;}
                    }
                    if (!set_non_blocking(client_fd)) { close(client_fd); continue; }
                    event.events = EPOLLIN | EPOLLET;
                    event.data.fd = client_fd;
                    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &event) == -1) {
                        perror("epoll_ctl add client_fd failed");
                        close(client_fd);
                    } else {
                         std::cout << "[Main] New connection accepted: fd=" << client_fd << std::endl;
                    }
                }
            } else {
                 // Handle client events... (same as before)
                 if (current_events & EPOLLIN) {
                     epoll_ctl(epoll_fd, EPOLL_CTL_DEL, current_fd, NULL);
                     task_queue.push(current_fd);
                 }
                 else if (current_events & (EPOLLERR | EPOLLHUP)) {
                    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, current_fd, NULL);
                    task_queue.push(current_fd);
                }
            }
        }
    }

    // --- Cleanup... ---
    std::cout << "Server shutting down..." << std::endl;
    task_queue.signal_shutdown();
    for (auto& t : worker_threads) {
        if(t.joinable()) t.join();
    }
    close(server_fd);
    close(epoll_fd);
    std::cout << "Server shutdown complete." << std::endl;
    return 0;
}

