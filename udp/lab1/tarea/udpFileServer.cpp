/* Server code in C - UDP Version with 777-byte protocol */

#include <arpa/inet.h>
#include <fstream>
#include <iostream>
#include <map>
#include <mutex>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <sys/socket.h>
#include <sys/types.h>
#include <thread>
#include <unistd.h>

using namespace std;

#define PROTOCOL_SIZE 777

map<string, struct sockaddr_in> m_clients; // nickname -> address
mutex clientsMutex;

string padNumber(int num, int width) {
  string s = to_string(num);
  if ((int)s.size() < width)
    s = string(width - s.size(), '0') + s;
  return s;
}

string padToFixedSize(const string &data, size_t targetSize, char padChar = '#') {
    if (data.size() >= targetSize) {
        return data.substr(0, targetSize);
    } else {
        return data + string(targetSize - data.size(), padChar);
    }
}

string removePadding(const string &data, char padChar = '#') {
    size_t endPos = data.find_last_not_of(padChar);
    if (endPos == string::npos) {
        return "";
    }
    return data.substr(0, endPos + 1);
}

void sendToClient(int sockfd, const string &message, const struct sockaddr_in &clientAddr) {
    string paddedMessage = padToFixedSize(message, PROTOCOL_SIZE);
    sendto(sockfd, paddedMessage.c_str(), PROTOCOL_SIZE, 0,
           (const struct sockaddr*)&clientAddr, sizeof(clientAddr));
}

vector<string> fragmentMessage(const string &message, size_t fragmentSize) {
    vector<string> fragments;
    for (size_t i = 0; i < message.size(); i += fragmentSize) {
        fragments.push_back(message.substr(i, fragmentSize));
    }
    return fragments;
}

void handleClientMessage(int sockfd, const string &message, const struct sockaddr_in &clientAddr) {
    if (message.empty()) return;
    
    string cleanMessage = removePadding(message);
    if (cleanMessage.empty()) return;
    
    char type = cleanMessage[0];
    
    switch (type) {
    case 'n': {
        if (cleanMessage.size() < 3) return;
        
        string lenStr = cleanMessage.substr(1, 2);
        int nickLen = stoi(lenStr);
        
        if (cleanMessage.size() < 3 + nickLen) return;
        string nickname = cleanMessage.substr(3, nickLen);

        lock_guard<mutex> lock(clientsMutex);

        if (m_clients.count(nickname)) {
            string err = "E015nicknameexiste";
            sendToClient(sockfd, err, clientAddr);
            cout << "Cliente intentó usar nickname repetido: " << nickname << endl;
        } else {
            m_clients[nickname] = clientAddr;
            cout << "Nuevo cliente registrado con nickname[" << type << lenStr
                 << nickname << "]: " << nickname << endl;
            
            // Send confirmation
            string confirm = "OK" + padNumber(nickname.size(), 2) + nickname;
            sendToClient(sockfd, confirm, clientAddr);
        }
        break;
    }
    case 'm': {
        if (cleanMessage.size() < 4) return;
        
        string msgLenStr = cleanMessage.substr(1, 3);
        int msgLen = stoi(msgLenStr);
        
        if (cleanMessage.size() < 4 + msgLen) return;
        string msg = cleanMessage.substr(4, msgLen);

        string senderNick;
        {
            lock_guard<mutex> lock(clientsMutex);
            for (auto &p : m_clients) {
                if (p.second.sin_addr.s_addr == clientAddr.sin_addr.s_addr &&
                    p.second.sin_port == clientAddr.sin_port) {
                    senderNick = p.first;
                    break;
                }
            }
        }

        if (senderNick.empty()) return;

        cout << "Broadcast de [" << senderNick << "]" << msg << endl;

        lock_guard<mutex> lock(clientsMutex);
        string proto = "M" + padNumber(senderNick.size(), 2) + senderNick +
                     padNumber(msg.size(), 3) + msg;
        
        // Fragment and send to all clients
        string fullMessage = padToFixedSize(proto, PROTOCOL_SIZE);
        for (auto &p : m_clients) {
            if (p.first != senderNick) {
                sendto(sockfd, fullMessage.c_str(), PROTOCOL_SIZE, 0,
                      (const struct sockaddr*)&p.second, sizeof(p.second));
            }
        }
        break;
    }
    case 't': {
        if (cleanMessage.size() < 3) return;
        
        string lenStr = cleanMessage.substr(1, 2);
        int destLen = stoi(lenStr);
        
        if (cleanMessage.size() < 3 + destLen) return;
        string destNick = cleanMessage.substr(3, destLen);
        
        if (cleanMessage.size() < 3 + destLen + 3) return;
        string msgLenStr = cleanMessage.substr(3 + destLen, 3);
        int msgLen = stoi(msgLenStr);
        
        if (cleanMessage.size() < 3 + destLen + 3 + msgLen) return;
        string msg = cleanMessage.substr(3 + destLen + 3, msgLen);

        string senderNick;
        {
            lock_guard<mutex> lock(clientsMutex);
            for (auto &p : m_clients) {
                if (p.second.sin_addr.s_addr == clientAddr.sin_addr.s_addr &&
                    p.second.sin_port == clientAddr.sin_port) {
                    senderNick = p.first;
                    break;
                }
            }
        }

        if (senderNick.empty()) return;

        cout << "Mensaje privado de [" << senderNick << "] a [" << destNick
             << "]: " << msg << endl;

        lock_guard<mutex> lock(clientsMutex);
        auto it = m_clients.find(destNick);
        if (it != m_clients.end()) {
            string proto = "T" + padNumber(senderNick.size(), 2) + senderNick +
                           padNumber(msg.size(), 3) + msg;
            sendToClient(sockfd, proto, it->second);
        } else {
            string err = "e03ERRDestinatario no encontrado";
            sendToClient(sockfd, err, clientAddr);
        }
        break;
    }
    case 'l': {
        lock_guard<mutex> lock(clientsMutex);

        int numUsers = m_clients.size();
        string proto = "L" + padNumber(numUsers, 2);

        for (auto &p : m_clients) {
            const string &nick = p.first;
            proto += padNumber((int)nick.size(), 2) + nick;
        }

        cout << "Listado protocolo [" << proto << "]" << endl;
        sendToClient(sockfd, proto, clientAddr);
        break;
    }
    case 'f': {
        if (cleanMessage.size() < 3) return;
        
        string lenStr = cleanMessage.substr(1, 2);
        int destLen = stoi(lenStr);
        
        if (cleanMessage.size() < 3 + destLen) return;
        string destNick = cleanMessage.substr(3, destLen);
        
        if (cleanMessage.size() < 3 + destLen + 3) return;
        string sizeFilename = cleanMessage.substr(3 + destLen, 3);
        int filenameLen = stoi(sizeFilename);
        
        if (cleanMessage.size() < 3 + destLen + 3 + filenameLen) return;
        string filename = cleanMessage.substr(3 + destLen + 3, filenameLen);
        
        if (cleanMessage.size() < 3 + destLen + 3 + filenameLen + 10) return;
        string sizeFileData = cleanMessage.substr(3 + destLen + 3 + filenameLen, 10);
        long fileLen = stol(sizeFileData);
        
        if (cleanMessage.size() < 3 + destLen + 3 + filenameLen + 10 + fileLen) return;
        string fileData = cleanMessage.substr(3 + destLen + 3 + filenameLen + 10, fileLen);

        string senderNick;
        {
            lock_guard<mutex> lock(clientsMutex);
            for (auto &p : m_clients) {
                if (p.second.sin_addr.s_addr == clientAddr.sin_addr.s_addr &&
                    p.second.sin_port == clientAddr.sin_port) {
                    senderNick = p.first;
                    break;
                }
            }
        }

        if (senderNick.empty()) return;

        cout << "Archivo [" << filename << "] de [" << senderNick << "] para [" << destNick 
             << "], tamaño: " << fileLen << " bytes" << endl;

        lock_guard<mutex> lock(clientsMutex);
        auto it = m_clients.find(destNick);
        if (it != m_clients.end()) {
            string proto = "F" + padNumber(senderNick.size(), 2) + senderNick +
                           padNumber(filename.size(), 3) + filename +
                           padNumber(fileData.size(), 10) + fileData;
            
            // Handle large files by fragmenting
            if (proto.size() > PROTOCOL_SIZE) {
                vector<string> fragments = fragmentMessage(proto, PROTOCOL_SIZE - 10);
                for (size_t i = 0; i < fragments.size(); i++) {
                    string fragmentProto = "F" + padNumber(i, 3) + padNumber(fragments.size(), 3) + fragments[i];
                    sendToClient(sockfd, fragmentProto, it->second);
                }
            } else {
                sendToClient(sockfd, proto, it->second);
            }
        } else {
            string err = "e03ERRDestinatario no encontrado";
            sendToClient(sockfd, err, clientAddr);
        }
        break;
    }
    case 'x': {
        lock_guard<mutex> lock(clientsMutex);
        string nick;
        for (auto it = m_clients.begin(); it != m_clients.end(); ++it) {
            if (it->second.sin_addr.s_addr == clientAddr.sin_addr.s_addr &&
                it->second.sin_port == clientAddr.sin_port) {
                nick = it->first;
                m_clients.erase(it);
                break;
            }
        }
        cout << "Cliente " << nick << " desconectado." << endl;
        
        string response = "X01Desconexion exitosa";
        sendToClient(sockfd, response, clientAddr);
        break;
    }
    default:
      cout << "Tipo de mensaje desconocido: " << type << endl;
      string response = "E99Tipo desconocido";
      sendToClient(sockfd, response, clientAddr);
      break;
    }
}

void cleanupInactiveClients(int sockfd) {
    while (true) {
        sleep(30);
        // UDP doesn't need active cleanup like TCP since there's no connection state
        // But we could implement heartbeat mechanism if needed
        this_thread::sleep_for(chrono::seconds(30));
    }
}

int main(void) {
    struct sockaddr_in stSockAddr;
    int SocketServer = socket(PF_INET, SOCK_DGRAM, 0);
    char buffer[PROTOCOL_SIZE + 1];

    if (-1 == SocketServer) {
        perror("can not create socket");
        exit(EXIT_FAILURE);
    }

    int opt = 1;
    if (setsockopt(SocketServer, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt(SO_REUSEADDR) failed");
        close(SocketServer);
        exit(EXIT_FAILURE);
    }

    memset(&stSockAddr, 0, sizeof(struct sockaddr_in));

    stSockAddr.sin_family = AF_INET;
    stSockAddr.sin_port = htons(45000);
    stSockAddr.sin_addr.s_addr = INADDR_ANY;

    if (-1 == bind(SocketServer, (const struct sockaddr *)&stSockAddr,
                   sizeof(struct sockaddr_in))) {
        perror("error bind failed");
        close(SocketServer);
        exit(EXIT_FAILURE);
    }

    cout << "UDP Server listening on port 45000..." << endl;
    cout << "Protocol size: " << PROTOCOL_SIZE << " bytes" << endl;

    // Start cleanup thread
    thread cleanupThread(cleanupInactiveClients, SocketServer);
    cleanupThread.detach();

    for (;;) {
        struct sockaddr_in clientAddr;
        socklen_t clientLen = sizeof(clientAddr);
        
        ssize_t n = recvfrom(SocketServer, buffer, PROTOCOL_SIZE, 0,
                            (struct sockaddr*)&clientAddr, &clientLen);
        
        if (n > 0) {
            buffer[n] = '\0';
            string message(buffer, n);
            
            // Handle message directly (UDP is connectionless)
            handleClientMessage(SocketServer, message, clientAddr);
        }
    }

    close(SocketServer);
    return 0;
}