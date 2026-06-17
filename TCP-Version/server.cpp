#include <arpa/inet.h>
#include <ctime>
#include <deque>
#include <iostream>
#include <map>
#include <mutex>
#include <netinet/in.h>
#include <sstream>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>


using namespace std;

static const int TCP_PORT = 8080;
static const int UDP_PORT = 8081;
static const size_t MAX_HISTORY = 100;

struct ClientInfo {
    int tcp_fd;
    string status;
    sockaddr_in udp_addr;
    bool has_udp;
};

map<string, string> g_credentials = {
    {"zubair", "1234"},
    {"ali", "1234"},
    {"ahmed", "1234"},
    {"daniyal", "1234"}
};

map<string, ClientInfo> g_clients;
deque<string> g_history;
mutex g_clients_mutex;
mutex g_history_mutex;
int g_udp_sock = -1;

string trim(const string &s) {
    size_t start = 0;
    while (start < s.size() && (s[start] == ' ' || s[start] == '\t' || s[start] == '\n' || s[start] == '\r')) {
        start++;
    }
    size_t end = s.size();
    while (end > start && (s[end - 1] == ' ' || s[end - 1] == '\t' || s[end - 1] == '\n' || s[end - 1] == '\r')) {
        end--;
    }
    return s.substr(start, end - start);
}

string now_ts() {
    time_t now = time(nullptr);
    tm *local = localtime(&now);
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", local);
    return string(buf);
}

void log_activity(const string &msg) {
    cout << "[" << now_ts() << "] " << msg << endl;
}

bool send_line(int fd, const string &line) {
    string out = line + "\n";
    const char *data = out.c_str();
    size_t left = out.size();
    while (left > 0) {
        ssize_t sent = send(fd, data, left, 0);
        if (sent <= 0) {
            return false;
        }
        data += sent;
        left -= static_cast<size_t>(sent);
    }
    return true;
}

bool recv_line(int fd, string &line) {
    line.clear();
    char ch;
    while (true) {
        ssize_t n = recv(fd, &ch, 1, 0);
        if (n <= 0) {
            return false;
        }
        if (ch == '\n') {
            break;
        }
        line.push_back(ch);
    }
    line = trim(line);
    return true;
}

void add_history(const string &entry) {
    lock_guard<mutex> lock(g_history_mutex);
    g_history.push_back("[" + now_ts() + "] " + entry);
    while (g_history.size() > MAX_HISTORY) {
        g_history.pop_front();
    }
}

void send_udp_to_addr(const sockaddr_in &addr, const string &msg) {
    sendto(g_udp_sock, msg.c_str(), msg.size(), 0, reinterpret_cast<const sockaddr *>(&addr), sizeof(addr));
}

void send_udp_to_user(const string &username, const string &msg) {
    lock_guard<mutex> lock(g_clients_mutex);
    auto it = g_clients.find(username);
    if (it != g_clients.end() && it->second.has_udp) {
        send_udp_to_addr(it->second.udp_addr, msg);
    }
}

void broadcast_udp(const string &msg) {
    lock_guard<mutex> lock(g_clients_mutex);
    for (const auto &p : g_clients) {
        if (p.second.has_udp) {
            send_udp_to_addr(p.second.udp_addr, msg);
        }
    }
}

bool valid_status(const string &status) {
    return status == "online" || status == "away" || status == "busy";
}

void udp_loop() {
    char buffer[4096];
    while (true) {
        sockaddr_in src_addr;
        socklen_t src_len = sizeof(src_addr);
        ssize_t n = recvfrom(g_udp_sock, buffer, sizeof(buffer) - 1, 0, reinterpret_cast<sockaddr *>(&src_addr), &src_len);
        if (n <= 0) {
            continue;
        }
        buffer[n] = '\0';
        string packet = trim(string(buffer));
        if (packet.empty()) {
            continue;
        }

        istringstream iss(packet);
        string type;
        iss >> type;

        if (type == "REGISTER") {
            string username;
            iss >> username;
            lock_guard<mutex> lock(g_clients_mutex);
            auto it = g_clients.find(username);
            if (it != g_clients.end()) {
                it->second.udp_addr = src_addr;
                it->second.has_udp = true;
            }
        } else if (type == "BROADCAST") {
            string username;
            iss >> username;
            string message;
            getline(iss, message);
            message = trim(message);
            if (!username.empty() && !message.empty()) {
                string out = "BROADCAST " + username + ": " + message;
                broadcast_udp(out);
                add_history(username + " (broadcast): " + message);
            }
        } else if (type == "STATUS") {
            string username, status;
            iss >> username >> status;
            if (!username.empty() && valid_status(status)) {
                {
                    lock_guard<mutex> lock(g_clients_mutex);
                    auto it = g_clients.find(username);
                    if (it != g_clients.end()) {
                        it->second.status = status;
                    }
                }
                string note = "NOTIFY " + username + " is now " + status;
                broadcast_udp(note);
                add_history("Status: " + username + " -> " + status);
                log_activity("Status updated: " + username + " -> " + status);
            }
        }
    }
}

void send_help(int fd) {
    send_line(fd, "INFO Commands:");
    send_line(fd, "INFO /help");
    send_line(fd, "INFO /msg <user> <text>");
    send_line(fd, "INFO /file <user> <filename> <base64data>");
    send_line(fd, "INFO /history <n>");
    send_line(fd, "INFO /status <online|away|busy> (sent over UDP by client)");
    send_line(fd, "INFO /say <text> (broadcast over UDP by client)");
    send_line(fd, "INFO /exit");
}

void handle_client(int client_fd) {
    string auth_line;
   send_line(client_fd, "AUTH Please enter your Username and Password to continue:");
    if (!recv_line(client_fd, auth_line)) {
        close(client_fd);
        return;
    }

    istringstream auth_iss(auth_line);
    string username, password;
    auth_iss >> username >> password;
    if (username.empty() || password.empty()) {
        send_line(client_fd, "AUTH_FAIL Invalid auth format");
        close(client_fd);
        return;
    }

    {
        lock_guard<mutex> lock(g_clients_mutex);
        auto cred_it = g_credentials.find(username);
        if (cred_it == g_credentials.end() || cred_it->second != password) {
            send_line(client_fd, "AUTH_FAIL Wrong username/password");
            close(client_fd);
            return;
        }
        if (g_clients.find(username) != g_clients.end()) {
            send_line(client_fd, "AUTH_FAIL User already online");
            close(client_fd);
            return;
        }

        ClientInfo info;
        info.tcp_fd = client_fd;
        info.status = "online";
        info.has_udp = false;
        g_clients[username] = info;
    }

    send_line(client_fd, "AUTH_OK Welcome " + username);
    send_help(client_fd);
    add_history(username + " joined");
    log_activity("User connected: " + username);
    broadcast_udp("NOTIFY " + username + " joined the chat");

    string cmd;
    while (recv_line(client_fd, cmd)) {
        if (cmd.empty()) {
            continue;
        }

        if (cmd == "/help") {
            send_help(client_fd);
            continue;
        }

        if (cmd == "/exit") {
            send_line(client_fd, "INFO Bye");
            break;
        }

        if (cmd.rfind("/history", 0) == 0) {
            istringstream iss(cmd);
            string token;
            int n = 10;
            iss >> token >> n;
            if (n < 1) {
                n = 1;
            }
            if (n > static_cast<int>(MAX_HISTORY)) {
                n = static_cast<int>(MAX_HISTORY);
            }

            vector<string> snapshot;
            {
                lock_guard<mutex> lock(g_history_mutex);
                int start = static_cast<int>(g_history.size()) - n;
                if (start < 0) {
                    start = 0;
                }
                for (int i = start; i < static_cast<int>(g_history.size()); i++) {
                    snapshot.push_back(g_history[i]);
                }
            }

            send_line(client_fd, "HISTORY_BEGIN");
            for (const string &line : snapshot) {
                send_line(client_fd, "HISTORY " + line);
            }
            send_line(client_fd, "HISTORY_END");
            continue;
        }

        if (cmd.rfind("/msg ", 0) == 0) {
            istringstream iss(cmd);
            string token, target;
            iss >> token >> target;
            string text;
            getline(iss, text);
            text = trim(text);

            if (target.empty() || text.empty()) {
                send_line(client_fd, "ERROR Usage: /msg <user> <text>");
                continue;
            }

            int target_fd = -1;
            {
                lock_guard<mutex> lock(g_clients_mutex);
                auto it = g_clients.find(target);
                if (it != g_clients.end()) {
                    target_fd = it->second.tcp_fd;
                }
            }

            if (target_fd == -1) {
                send_line(client_fd, "ERROR User not online");
                continue;
            }

            send_line(target_fd, "PM " + username + ": " + text);
            send_line(client_fd, "INFO Private message sent to " + target);
            add_history(username + " -> " + target + " (pm): " + text);
            continue;
        }

        if (cmd.rfind("/file ", 0) == 0) {
            istringstream iss(cmd);
            string token, target, filename;
            iss >> token >> target >> filename;
            string base64_data;
            getline(iss, base64_data);
            base64_data = trim(base64_data);

            if (target.empty() || filename.empty() || base64_data.empty()) {
                send_line(client_fd, "ERROR Usage: /file <user> <filename> <base64data>");
                continue;
            }

            int target_fd = -1;
            {
                lock_guard<mutex> lock(g_clients_mutex);
                auto it = g_clients.find(target);
                if (it != g_clients.end()) {
                    target_fd = it->second.tcp_fd;
                }
            }

            if (target_fd == -1) {
                send_line(client_fd, "ERROR User not online");
                continue;
            }

            send_line(target_fd, "FILE_FROM " + username + " " + filename + " " + base64_data);
            send_line(client_fd, "INFO File sent to " + target + " as " + filename);
            add_history(username + " sent file to " + target + ": " + filename);
            continue;
        }

        send_line(client_fd, "ERROR Unknown command. Use /help");
    }

    {
        lock_guard<mutex> lock(g_clients_mutex);
        g_clients.erase(username);
    }
    add_history(username + " left");
    log_activity("User disconnected: " + username);
    broadcast_udp("NOTIFY " + username + " left the chat");
    close(client_fd);
}



int main() {
    int tcp_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (tcp_sock < 0) {
        cerr << "Failed to create TCP socket" << endl;
        return 1;
    }

    int opt = 1;
    setsockopt(tcp_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in tcp_addr;
    tcp_addr.sin_family = AF_INET;
    tcp_addr.sin_addr.s_addr = INADDR_ANY;
    tcp_addr.sin_port = htons(TCP_PORT);

    if (bind(tcp_sock, reinterpret_cast<sockaddr *>(&tcp_addr), sizeof(tcp_addr)) < 0) {
        cerr << "TCP bind failed" << endl;
        close(tcp_sock);
        return 1;
    }

    if (listen(tcp_sock, 20) < 0) {
        cerr << "TCP listen failed" << endl;
        close(tcp_sock);
        return 1;
    }

    g_udp_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (g_udp_sock < 0) {
        cerr << "Failed to create UDP socket" << endl;
        close(tcp_sock);
        return 1;
    }

    setsockopt(g_udp_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in udp_addr;
    udp_addr.sin_family = AF_INET;
    udp_addr.sin_addr.s_addr = INADDR_ANY;
    udp_addr.sin_port = htons(UDP_PORT);

    if (bind(g_udp_sock, reinterpret_cast<sockaddr *>(&udp_addr), sizeof(udp_addr)) < 0) {
        cerr << "UDP bind failed" << endl;
        close(g_udp_sock);
        close(tcp_sock);
        return 1;
    }

    log_activity("Server started: TCP 8080, UDP 8081");
    log_activity("Demo users: zubair/ ali/ ahmed/ daniyal/");

    thread udp_thread(udp_loop);
    udp_thread.detach();

    while (true) {
        sockaddr_in client_addr;
        socklen_t len = sizeof(client_addr);
        int client_fd = accept(tcp_sock, reinterpret_cast<sockaddr *>(&client_addr), &len);
        if (client_fd < 0) {
            continue;
        }

        thread t(handle_client, client_fd);
        t.detach();
    }

    close(g_udp_sock);
    close(tcp_sock);
    return 0;
}