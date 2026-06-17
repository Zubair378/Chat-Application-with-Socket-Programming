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
- `/help` : View command list
- `/msg <user> <text>` : Send a private message to a specific user
- `/file <user> <filename> <base64data>` : Transfer a file
- `/history <n>` : View the last *n* messages
- `/status <online|away|busy>` : Update your current status (sent over UDP)
- `/say <text>` : Broadcast a message to everyone (sent over UDP)
- `/exit` : Disconnect and close the client
