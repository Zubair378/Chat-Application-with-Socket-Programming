#include <arpa/inet.h>
#include <atomic>
#include <cctype>
#include <fstream>
#include <iostream>
#include <netinet/in.h>
#include <sstream>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

using namespace std;

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

void receiver_loop(int udp_sock, atomic<bool> &running) {
    char buf[65535];
    while (running) {
        ssize_t n = recvfrom(udp_sock, buf, sizeof(buf) - 1, 0, nullptr, nullptr);
        if (n <= 0) {
            if (!running) {
                break;
            }
            continue;
        }
        buf[n] = '\0';
        string msg = trim(string(buf));

        if (msg.rfind("FILE_FROM ", 0) == 0) {
            istringstream iss(msg);
            string tag, sender, filename;
            iss >> tag >> sender >> filename;
            string b64;
            getline(iss, b64);
            b64 = trim(b64);

            vector<unsigned char> data = base64_decode(b64);
            string out_name = "recv_" + sender + "_" + filename;
            if (write_binary_file(out_name, data)) {
                cout << "[UDP] File received from " << sender << " and saved as " << out_name << endl;
            } else {
                cout << "[UDP] File received from " << sender << " but failed to save." << endl;
            }
            continue;
        }

        cout << "[UDP] " << msg << endl;
    }
}

int main() {
    string server_ip;
    cout << "Server IP (default 127.0.0.1): ";
    getline(cin, server_ip);
    server_ip = trim(server_ip);
    if (server_ip.empty()) {
        server_ip = "127.0.0.1";
    }

    int udp_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (udp_sock < 0) {
        cerr << "Failed to create UDP socket" << endl;
        return 1;
    }

    sockaddr_in local_addr;
    local_addr.sin_family = AF_INET;
    local_addr.sin_addr.s_addr = INADDR_ANY;
    local_addr.sin_port = htons(0);
    if (bind(udp_sock, reinterpret_cast<sockaddr *>(&local_addr), sizeof(local_addr)) < 0) {
        cerr << "UDP bind failed" << endl;
        close(udp_sock);
        return 1;
    }

    sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(UDP_PORT);
    if (inet_pton(AF_INET, server_ip.c_str(), &server_addr.sin_addr) <= 0) {
        cerr << "Invalid server IP" << endl;
        close(udp_sock);
        return 1;
    }

    string username, password;
    cout << "Username: ";
    getline(cin, username);
    cout << "Password: ";
    getline(cin, password);
    username = trim(username);
    password = trim(password);

    string auth = "AUTH " + username + " " + password;
    sendto(udp_sock, auth.c_str(), auth.size(), 0, reinterpret_cast<sockaddr *>(&server_addr), sizeof(server_addr));

    char auth_buf[4096];
    ssize_t n = recvfrom(udp_sock, auth_buf, sizeof(auth_buf) - 1, 0, nullptr, nullptr);
    if (n <= 0) {
        cerr << "No response from server" << endl;
        close(udp_sock);
        return 1;
    }
    auth_buf[n] = '\0';
    string auth_reply = trim(string(auth_buf));
    cout << "[UDP] " << auth_reply << endl;
    if (auth_reply.rfind("AUTH_OK", 0) != 0) {
        close(udp_sock);
        return 1;
    }

    atomic<bool> running(true);
    thread recv_thread(receiver_loop, udp_sock, ref(running));

    cout << "Commands: /help, /msg, /file, /history, /status, /say, /exit" << endl;
    cout << "File command format: /file <user> <path_to_file>" << endl;
    cout << "Note: UDP file transfer is best for small files." << endl;

    while (running) {
        string input;
        if (!getline(cin, input)) {
            break;
        }
        input = trim(input);
        if (input.empty()) {
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
            string command = "CMD " + username + " /file " + target + " " + filename + " " + b64;
            sendto(udp_sock, command.c_str(), command.size(), 0, reinterpret_cast<sockaddr *>(&server_addr), sizeof(server_addr));
            continue;
        }

        string command;
        if (input[0] == '/') {
            command = "CMD " + username + " " + input;
        } else {
            command = "CMD " + username + " /say " + input;
        }

        sendto(udp_sock, command.c_str(), command.size(), 0, reinterpret_cast<sockaddr *>(&server_addr), sizeof(server_addr));

        if (input == "/exit") {
            running = false;
            break;
        }
    }

    running = false;
    shutdown(udp_sock, SHUT_RDWR);
    close(udp_sock);
    if (recv_thread.joinable()) {
        recv_thread.join();
    }

    return 0;
}