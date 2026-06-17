# Multi-Client Chat System

This repository contains a CLI-based multi-client chat system implemented in C++ using Socket Programming. The project features two implementations: a **TCP-Version** and a **UDP-Version**. 

## Features
- **User Authentication:** Login with username and password.
- **Direct Messaging (`/msg`):** Send private messages to other online users.
- **Broadcasting (`/say`):** Send messages to all users (handled via UDP).
- **File Transfer (`/file`):** Send files to other users using base64 encoding.
- **Chat History (`/history`):** Retrieve the last `n` messages.
- **Status Updates (`/status`):** Set status to `online`, `away`, or `busy`.

## Prerequisites
Since this is a Linux-based CLI application, Windows users must run it using **WSL (Windows Subsystem for Linux)**.

1. **Install WSL:** If you haven't already, open PowerShell as Administrator and run:
   ```bash
   wsl --install
   ```
2. **Install a C++ Compiler (GCC):** Inside your WSL terminal (e.g., Ubuntu or Kali Linux), install `g++`:
   ```bash
   sudo apt update
   sudo apt install g++
   ```

## How to Compile and Run

You can choose to run either the TCP-Version or the UDP-Version. Navigate to the respective folder in your WSL terminal.

### 1. Compile the Code
Navigate to either `TCP-Version` or `UDP-version` and compile the server and client files:
```bash
# Compile the server
g++ server.cpp -o server -pthread

# Compile the client
g++ client.cpp -o client -pthread
```
*(Note: `-pthread` is required if multi-threading is used in the implementation).*

### 2. Run the Server
**The server must be started first!** Open a terminal, navigate to the version folder, and run:
```bash
./server
```
*The server will start listening for connections (e.g., TCP on 8080 and UDP on 8081).*

### 3. Run the Client(s)
Open a new WSL terminal (or multiple terminals for multiple clients) and run:
```bash
./client
```
1. Enter the Server IP (Default is `127.0.0.1` for localhost).
2. Enter your credentials (e.g., Demo users: `zubair`, `ali`, `ahmed`, `daniyal`).
3. Type `/help` to see the list of available commands.

## Available Client Commands
| Command | Format | Description |
|---|---|---|
| **Help** | `/help` | View the list of available commands. |
| **Direct Message** | `/msg <user> <text>` | Send a private, direct message to a specific user. |
| **File Transfer** | `/file <user> <filename> <base64data>` | Read, encode, and transfer a file to a specific user over the network. |
| **Chat History** | `/history <n>` | Retrieve the last *n* messages from the server's memory. |
| **Update Status** | `/status <online\|away\|busy>` | Update your current availability status (Sent over UDP). |
| **Broadcast** | `/say <text>` | Send a public message to everyone currently connected (Sent over UDP). |
| **Exit** | `/exit` | Disconnect gracefully and close the client terminal.

---

## 🏗️ Architecture & Protocol Breakdown

The `TCP-Version` leverages a hybrid approach. It uses the reliable TCP protocol for critical operations and UDP for fast, lightweight broadcasts:

| Feature | Protocol Used (TCP-Version) | Protocol Used (UDP-Version) | Design Reason (in TCP-Version) |
|---|---|---|---|
| **Authentication** | TCP | UDP | Needs high reliability to ensure credentials are continuously verified and transmitted safely. |
| **Direct Messaging** | TCP | UDP | Guarantees message delivery without packet loss for secure private chats. |
| **File Transfer** | TCP | UDP | Essential for keeping file data structurally intact and avoiding corrupted file reconstruction. |
| **Chat History** | TCP | UDP | Ensures the chronological order and completeness when fetching past communication logs. |
| **Broadcasting (`/say`)**| UDP | UDP | Faster widespread delivery to all clients; occasional missing packets are acceptable for general chat. |
| **Status Updates** | UDP | UDP | Used for real-time lightweight presence indicators without clogging the TCP stream. |

## 👥 Demo Users Profile Data

You can use the following pre-registered credentials to quickly evaluate the application without setting up a database.

| Implementation Version | Username | Password | Role / Purpose |
|---|---|---|---|
| **TCP-Version** | `zubair` | `1234` | Primary test user / Client 1 |
| **TCP-Version** | `ali` | `1234` | Secondary test user / Client 2 |
| **TCP-Version** | `ahmed` | `1234` | Test user / Client 3 |
| **TCP-Version** | `daniyal` | `1234` | Test user / Client 4 |
| **UDP-Version** | `alice` | `1234` | Primary test user / Client 1 |
| **UDP-Version** | `bob` | `1234` | Secondary test user / Client 2 |
| **UDP-Version** | `charlie` | `1234` | Test user / Client 3 |
| **UDP-Version** | `david` | `1234` | Test user / Client 4 |

## 📁 Directory & Data Structure

Here is the exact data layout for the different components in the project repository:

| File / Folder Path | Type | Description |
|---|---|---|
| `TCP-Version/server.cpp` | Source Code | Code for the hybrid TCP/UDP server handling multi-threaded connections safely using mutexes. |
| `TCP-Version/client.cpp` | Source Code | Code for the client interface handling dual-socket connections, CLI inputs, and Base64 parsing. |
| `UDP-version/server.cpp` | Source Code | Pure UDP server implementation keeping track of user statuses and broadcasting on port 8081. |
| `UDP-version/client.cpp` | Source Code | Pure UDP client implementation utilizing non-blocking polling and `sendto`/`recvfrom`. |
| `Report.docx` | Documentation | Detailed project report containing screenshots and the operational flow logic. |
| `Assignment 2...pdf` | Documentation | Initial assignment document defining the socket rules and application constraints. |

