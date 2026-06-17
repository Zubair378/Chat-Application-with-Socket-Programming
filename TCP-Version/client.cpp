#include <arpa/inet.h>
#include <atomic>
#include <cctype>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <termios.h> 

using namespace std;

static const int TCP_PORT = 8080;
static const int UDP_PORT = 8081;

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

string base64_encode(const vector<unsigned char> &data) {
    static const string table = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    string out;
    int val = 0;
    int valb = -6;
    for (unsigned char c : data) {
        val = (val << 8) + c;
        valb += 8;
        while (valb >= 0) {
            out.push_back(table[(val >> valb) & 0x3F]);
            valb -= 6;
        }
    }
    if (valb > -6) {
        out.push_back(table[((val << 8) >> (valb + 8)) & 0x3F]);
    }
    while (out.size() % 4) {
        out.push_back('=');
    }
    return out;
}

vector<unsigned char> base64_decode(const string &input) {
    static const string table = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    vector<int> t(256, -1);
    for (int i = 0; i < 64; i++) {
        t[static_cast<unsigned char>(table[i])] = i;
    }

    vector<unsigned char> out;
    int val = 0;
    int valb = -8;
    for (unsigned char c : input) {
        if (isspace(c)) {
            continue;
        }
        if (c == '=') {
            break;
        }
        if (t[c] == -1) {
            continue;
        }
        val = (val << 6) + t[c];
        valb += 6;
        if (valb >= 0) {
            out.push_back(static_cast<unsigned char>((val >> valb) & 0xFF));
            valb -= 8;
        }
    }
    return out;
}

string file_name_only(const string &path) {
    size_t p = path.find_last_of("/\\");
    if (p == string::npos) {
        return path;
    }
    return path.substr(p + 1);
}

bool read_binary_file(const string &path, vector<unsigned char> &data) {
    ifstream in(path, ios::binary);
    if (!in) {
        return false;
    }
    data.assign(istreambuf_iterator<char>(in), istreambuf_iterator<char>());
    return true;
}

bool write_binary_file(const string &path, const vector<unsigned char> &data) {
    ofstream out(path, ios::binary);
    if (!out) {
        return false;
    }
    out.write(reinterpret_cast<const char *>(data.data()), static_cast<streamsize>(data.size()));
    return static_cast<bool>(out);
}

void tcp_reader_loop(int tcp_sock, atomic<bool> &running) {
    string line;
    while (running && recv_line(tcp_sock, line)) {
        if (line.rfind("FILE_FROM ", 0) == 0) {
            istringstream iss(line);
            string tag, sender, filename;
            iss >> tag >> sender >> filename;
            string b64;
            getline(iss, b64);
            b64 = trim(b64);

            vector<unsigned char> data = base64_decode(b64);
            string out_name = "recv_" + sender + "_" + filename;
            if (write_binary_file(out_name, data)) {
                cout << "[TCP] File received from " << sender << " and saved as " << out_name << endl;
            } else {
                cout << "[TCP] File received from " << sender << " but failed to save." << endl;
            }
            continue;
        }

        cout << "[TCP] " << line << endl;
    }
    running = false;
}

void udp_reader_loop(int udp_sock, atomic<bool> &running) {
    char buf[4096];
    while (running) {
        ssize_t n = recvfrom(udp_sock, buf, sizeof(buf) - 1, 0, nullptr, nullptr);
        if (n <= 0) {
            if (!running) {
                break;
            }
            continue;
        }
        buf[n] = '\0';
        cout << "[UDP] " << trim(string(buf)) << endl;
    }
}
string get_hidden_password(const string& prompt) {
    cout << prompt << flush;
    string password;

    termios oldt;
    tcgetattr(STDIN_FILENO, &oldt); // Get current terminal settings
    termios newt = oldt;
    newt.c_lflag &= ~ECHO;          // Turn off echoing
    tcsetattr(STDIN_FILENO, TCSANOW, &newt); // Apply new settings

    getline(cin, password);

    tcsetattr(STDIN_FILENO, TCSANOW, &oldt); // Restore old settings
    cout << endl; // Move to the next line since Enter wasn't echoed
    return password;
}

int main() {
    string server_ip;
    cout << "Server IP (default 127.0.0.1): ";
    getline(cin, server_ip);
    server_ip = trim(server_ip);
    if (server_ip.empty()) {
        server_ip = "127.0.0.1";
    }

    int tcp_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (tcp_sock < 0) {
        cerr << "Failed to create TCP socket" << endl;
        return 1;
    }

    sockaddr_in server_tcp_addr;
    server_tcp_addr.sin_family = AF_INET;
    server_tcp_addr.sin_port = htons(TCP_PORT);
    if (inet_pton(AF_INET, server_ip.c_str(), &server_tcp_addr.sin_addr) <= 0) {
        cerr << "Invalid server IP" << endl;
        close(tcp_sock);
        return 1;
    }

    if (connect(tcp_sock, reinterpret_cast<sockaddr *>(&server_tcp_addr), sizeof(server_tcp_addr)) < 0) {
        cerr << "TCP connect failed" << endl;
        close(tcp_sock);
        return 1;
    }

    string line;
    if (!recv_line(tcp_sock, line)) {
        cerr << "Server closed during auth" << endl;
        close(tcp_sock);
        return 1;
    }
    cout << "[TCP] " << line << endl;

string username, password;
    cout << "Username: ";
    getline(cin, username);

    // Use the new hidden function instead of cin
    password = get_hidden_password("Password: "); 

    if (!send_line(tcp_sock, trim(username) + " " + trim(password))) {
        cerr << "Failed to send auth" << endl;
        close(tcp_sock);
        return 1;
    }

    if (!recv_line(tcp_sock, line)) {
        cerr << "Server closed after auth" << endl;
        close(tcp_sock);
        return 1;
    }
    cout << "[TCP] " << line << endl;
    if (line.rfind("AUTH_OK", 0) != 0) {
        close(tcp_sock);
        return 1;
    }

    int udp_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (udp_sock < 0) {
        cerr << "Failed to create UDP socket" << endl;
        close(tcp_sock);
        return 1;
    }

    sockaddr_in local_udp_addr;
    local_udp_addr.sin_family = AF_INET;
    local_udp_addr.sin_addr.s_addr = INADDR_ANY;
    local_udp_addr.sin_port = htons(0);
    if (bind(udp_sock, reinterpret_cast<sockaddr *>(&local_udp_addr), sizeof(local_udp_addr)) < 0) {
        cerr << "UDP bind failed" << endl;
        close(udp_sock);
        close(tcp_sock);
        return 1;
    }

    sockaddr_in server_udp_addr;
    server_udp_addr.sin_family = AF_INET;
    server_udp_addr.sin_port = htons(UDP_PORT);
    inet_pton(AF_INET, server_ip.c_str(), &server_udp_addr.sin_addr);

    string reg = "REGISTER " + trim(username);
    sendto(udp_sock, reg.c_str(), reg.size(), 0, reinterpret_cast<sockaddr *>(&server_udp_addr), sizeof(server_udp_addr));

    atomic<bool> running(true);
    thread tcp_reader(tcp_reader_loop, tcp_sock, ref(running));
    thread udp_reader(udp_reader_loop, udp_sock, ref(running));

    cout << "Commands: /help, /msg, /file, /history, /status, /say, /exit" << endl;
    cout << "File command format: /file <user> <path_to_file>" << endl;

    while (running) {
        string input;
        if (!getline(cin, input)) {
            break;
        }
        input = trim(input);
        if (input.empty()) {
            continue;
        }

        if (input == "/exit") {
            send_line(tcp_sock, input);
            running = false;
            break;
        }

        if (input.rfind("/status ", 0) == 0) {
            istringstream iss(input);
            string cmd, status;
            iss >> cmd >> status;
            string packet = "STATUS " + trim(username) + " " + trim(status);
            sendto(udp_sock, packet.c_str(), packet.size(), 0, reinterpret_cast<sockaddr *>(&server_udp_addr), sizeof(server_udp_addr));
            continue;
        }

        if (input.rfind("/say ", 0) == 0) {
            string text = trim(input.substr(5));
            if (text.empty()) {
                cout << "Usage: /say <text>" << endl;
                continue;
            }
            string packet = "BROADCAST " + trim(username) + " " + text;
            sendto(udp_sock, packet.c_str(), packet.size(), 0, reinterpret_cast<sockaddr *>(&server_udp_addr), sizeof(server_udp_addr));
            continue;
        }

        if (input.rfind("/file ", 0) == 0) {
            istringstream iss(input);
            string cmd, target, path;
            iss >> cmd >> target;
            getline(iss, path);
            path = trim(path);

            if (target.empty() || path.empty()) {
                cout << "Usage: /file <user> <path_to_file>" << endl;
                continue;
            }

            vector<unsigned char> data;
            if (!read_binary_file(path, data)) {
                cout << "Cannot read file: " << path << endl;
                continue;
            }

            string filename = file_name_only(path);
            string b64 = base64_encode(data);
            string tcp_cmd = "/file " + target + " " + filename + " " + b64;
            if (!send_line(tcp_sock, tcp_cmd)) {
                running = false;
                break;
            }
            continue;
        }

        if (input[0] != '/') {
            string packet = "BROADCAST " + trim(username) + " " + input;
            sendto(udp_sock, packet.c_str(), packet.size(), 0, reinterpret_cast<sockaddr *>(&server_udp_addr), sizeof(server_udp_addr));
            continue;
        }

        if (!send_line(tcp_sock, input)) {
            running = false;
            break;
        }
    }

    running = false;
    shutdown(tcp_sock, SHUT_RDWR);
    close(tcp_sock);
    shutdown(udp_sock, SHUT_RDWR);
    close(udp_sock);

    if (tcp_reader.joinable()) {
        tcp_reader.join();
    }
    if (udp_reader.joinable()) {
        udp_reader.join();
    }

    return 0;
}