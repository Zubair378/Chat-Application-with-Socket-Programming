#include <arpa/inet.h>
#include <ctime>
#include <deque>
#include <iostream>
#include <map>
#include <netinet/in.h>
#include <sstream>
#include <string>
#include <unistd.h>
#include <vector>

using namespace std;

static const int UDP_PORT = 8081;
static const size_t MAX_HISTORY = 100;

struct ClientInfo {
    sockaddr_in addr;
    string status;
    bool online;
};

map<string, string> g_credentials = {
    {"alice", "1234"},
    {"bob", "1234"},
    {"charlie", "1234"},
    {"david", "1234"}
};

map<string, ClientInfo> g_clients;
map<string, string> g_addr_to_user;
deque<string> g_history;

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

string addr_key(const sockaddr_in &addr) {
    char ip[INET_ADDRSTRLEN] = {0};
    inet_ntop(AF_INET, &addr.sin_addr, ip, sizeof(ip));
    return string(ip) + ":" + to_string(ntohs(addr.sin_port));
}

void add_history(const string &entry) {
    g_history.push_back("[" + now_ts() + "] " + entry);
    while (g_history.size() > MAX_HISTORY) {
        g_history.pop_front();
    }
}

void send_udp(int sock, const sockaddr_in &addr, const string &msg) {
    sendto(sock, msg.c_str(), msg.size(), 0, reinterpret_cast<const sockaddr *>(&addr), sizeof(addr));
}

void send_to_user(int sock, const string &username, const string &msg) {
    auto it = g_clients.find(username);
    if (it == g_clients.end() || !it->second.online) {
        return;
    }
    send_udp(sock, it->second.addr, msg);
}

void broadcast_all(int sock, const string &msg) {
    for (const auto &entry : g_clients) {
        if (entry.second.online) {
            send_udp(sock, entry.second.addr, msg);
        }
    }
}

bool valid_status(const string &status) {
    return status == "online" || status == "away" || status == "busy";
}

void send_help(int sock, const sockaddr_in &to) {
    send_udp(sock, to, "INFO Commands:");
    send_udp(sock, to, "INFO /help");
    send_udp(sock, to, "INFO /msg <user> <text>");
    send_udp(sock, to, "INFO /file <user> <filename> <base64data>");
    send_udp(sock, to, "INFO /history <n>");
    send_udp(sock, to, "INFO /status <online|away|busy>");
    send_udp(sock, to, "INFO /say <text>");
    send_udp(sock, to, "INFO /exit");
}

int main() {
    int udp_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (udp_sock < 0) {
        cerr << "Failed to create UDP socket" << endl;
        return 1;
    }

    int opt = 1;
    setsockopt(udp_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(UDP_PORT);

    if (bind(udp_sock, reinterpret_cast<sockaddr *>(&server_addr), sizeof(server_addr)) < 0) {
        cerr << "UDP bind failed" << endl;
        close(udp_sock);
        return 1;
    }

    log_activity("UDP server started on port 8081");
    log_activity("Demo users: alice/1234 bob/1234 charlie/1234 david/1234");

    char buffer[65535];
    while (true) {
        sockaddr_in src_addr;
        socklen_t src_len = sizeof(src_addr);
        ssize_t n = recvfrom(udp_sock, buffer, sizeof(buffer) - 1, 0, reinterpret_cast<sockaddr *>(&src_addr), &src_len);
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

        if (type == "AUTH") {
            string username, password;
            iss >> username >> password;
            if (username.empty() || password.empty()) {
                send_udp(udp_sock, src_addr, "AUTH_FAIL Usage: AUTH <username> <password>");
                continue;
            }

            auto cred = g_credentials.find(username);
            if (cred == g_credentials.end() || cred->second != password) {
                send_udp(udp_sock, src_addr, "AUTH_FAIL Wrong username/password");
                continue;
            }

            auto online_it = g_clients.find(username);
            if (online_it != g_clients.end() && online_it->second.online) {
                send_udp(udp_sock, src_addr, "AUTH_FAIL User already online");
                continue;
            }

            ClientInfo info;
            info.addr = src_addr;
            info.status = "online";
            info.online = true;
            g_clients[username] = info;
            g_addr_to_user[addr_key(src_addr)] = username;

            send_udp(udp_sock, src_addr, "AUTH_OK Welcome " + username);
            send_help(udp_sock, src_addr);
            add_history(username + " joined");
            log_activity("User connected: " + username);
            broadcast_all(udp_sock, "NOTIFY " + username + " joined the chat");
            continue;
        }

        if (type != "CMD") {
            send_udp(udp_sock, src_addr, "ERROR First login with: AUTH <username> <password>");
            continue;
        }

        string username;
        iss >> username;
        string cmd;
        getline(iss, cmd);
        cmd = trim(cmd);

        if (username.empty() || cmd.empty()) {
            send_udp(udp_sock, src_addr, "ERROR Usage: CMD <username> <command>");
            continue;
        }

        auto user_it = g_clients.find(username);
        if (user_it == g_clients.end() || !user_it->second.online) {
            send_udp(udp_sock, src_addr, "ERROR You are not logged in");
            continue;
        }

        if (addr_key(src_addr) != addr_key(user_it->second.addr)) {
            send_udp(udp_sock, src_addr, "ERROR This address is not registered for that user");
            continue;
        }

        if (cmd == "/help") {
            send_help(udp_sock, src_addr);
            continue;
        }

        if (cmd == "/exit") {
            user_it->second.online = false;
            g_addr_to_user.erase(addr_key(src_addr));
            send_udp(udp_sock, src_addr, "INFO Bye");
            add_history(username + " left");
            log_activity("User disconnected: " + username);
            broadcast_all(udp_sock, "NOTIFY " + username + " left the chat");
            continue;
        }

        if (cmd.rfind("/history", 0) == 0) {
            istringstream hss(cmd);
            string token;
            int count = 10;
            hss >> token >> count;
            if (count < 1) {
                count = 1;
            }
            if (count > static_cast<int>(MAX_HISTORY)) {
                count = static_cast<int>(MAX_HISTORY);
            }

            send_udp(udp_sock, src_addr, "HISTORY_BEGIN");
            int start = static_cast<int>(g_history.size()) - count;
            if (start < 0) {
                start = 0;
            }
            for (int i = start; i < static_cast<int>(g_history.size()); i++) {
                send_udp(udp_sock, src_addr, "HISTORY " + g_history[i]);
            }
            send_udp(udp_sock, src_addr, "HISTORY_END");
            continue;
        }

        if (cmd.rfind("/status ", 0) == 0) {
            string status = trim(cmd.substr(8));
            if (!valid_status(status)) {
                send_udp(udp_sock, src_addr, "ERROR Use /status <online|away|busy>");
                continue;
            }
            user_it->second.status = status;
            add_history("Status: " + username + " -> " + status);
            log_activity("Status updated: " + username + " -> " + status);
            broadcast_all(udp_sock, "NOTIFY " + username + " is now " + status);
            continue;
        }

        if (cmd.rfind("/say ", 0) == 0) {
            string text = trim(cmd.substr(5));
            if (text.empty()) {
                send_udp(udp_sock, src_addr, "ERROR Usage: /say <text>");
                continue;
            }
            broadcast_all(udp_sock, "BROADCAST " + username + ": " + text);
            add_history(username + " (broadcast): " + text);
            continue;
        }

        if (cmd.rfind("/msg ", 0) == 0) {
            istringstream mss(cmd);
            string token, target;
            mss >> token >> target;
            string text;
            getline(mss, text);
            text = trim(text);

            if (target.empty() || text.empty()) {
                send_udp(udp_sock, src_addr, "ERROR Usage: /msg <user> <text>");
                continue;
            }

            auto target_it = g_clients.find(target);
            if (target_it == g_clients.end() || !target_it->second.online) {
                send_udp(udp_sock, src_addr, "ERROR User not online");
                continue;
            }

            send_to_user(udp_sock, target, "PM " + username + ": " + text);
            send_udp(udp_sock, src_addr, "INFO Private message sent to " + target);
            add_history(username + " -> " + target + " (pm): " + text);
            continue;
        }

        if (cmd.rfind("/file ", 0) == 0) {
            istringstream fss(cmd);
            string token, target, filename;
            fss >> token >> target >> filename;
            string data;
            getline(fss, data);
            data = trim(data);

            if (target.empty() || filename.empty() || data.empty()) {
                send_udp(udp_sock, src_addr, "ERROR Usage: /file <user> <filename> <base64data>");
                continue;
            }

            auto target_it = g_clients.find(target);
            if (target_it == g_clients.end() || !target_it->second.online) {
                send_udp(udp_sock, src_addr, "ERROR User not online");
                continue;
            }

            send_to_user(udp_sock, target, "FILE_FROM " + username + " " + filename + " " + data);
            send_udp(udp_sock, src_addr, "INFO File sent to " + target + " as " + filename);
            add_history(username + " sent file to " + target + ": " + filename);
            continue;
        }

        if (cmd[0] != '/') {
            broadcast_all(udp_sock, "BROADCAST " + username + ": " + cmd);
            add_history(username + " (broadcast): " + cmd);
            continue;
        }

        send_udp(udp_sock, src_addr, "ERROR Unknown command. Use /help");
    }

    close(udp_sock);
    return 0;
}