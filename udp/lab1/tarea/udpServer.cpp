/* Server code in C - UDP Version with 777-byte protocol */

#include <algorithm>
#include <arpa/inet.h>
#include <iostream>
#include <map>
#include <mutex>
#include <netinet/in.h>
#include <queue>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <sys/socket.h>
#include <sys/types.h>
#include <thread>
#include <unistd.h>
#include <vector>

using namespace std;

#define PROTOCOL_SIZE 777

void printPlays(vector<string> &v_plays) {
  for (size_t i = 0; i < v_plays.size(); i++) {
    if (i % 3 == 0)
      cout << "\n";
    cout << v_plays[i] << "|";
  }
  cout << "\n";
}

int play = 0;
int currentTurn = 0;
static vector<string> v_plays(9, " ");

char getPlay() {
  if (play % 2 == 0) {
    play++;
    return 'o';
  } else {
    play++;
    return 'x';
  }
}

struct ClientInfo {
    struct sockaddr_in address;
    string nickname;
    time_t lastSeen;
};

map<string, ClientInfo> m_clients; // key: "ip:port" -> ClientInfo
vector<string> q_players; // store client keys instead of socket FDs
mutex clientsMutex;
mutex gameMutex;

string getClientKey(const struct sockaddr_in &addr) {
    char ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(addr.sin_addr), ip, INET_ADDRSTRLEN);
    return string(ip) + ":" + to_string(ntohs(addr.sin_port));
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
        return ""; // All padding characters
    }
    return data.substr(0, endPos + 1);
}

void sendToClient(int sockfd, const string &message, const struct sockaddr_in &clientAddr) {
    string paddedMessage = padToFixedSize(message, PROTOCOL_SIZE);
    sendto(sockfd, paddedMessage.c_str(), PROTOCOL_SIZE, 0, 
           (const struct sockaddr*)&clientAddr, sizeof(clientAddr));
}

void broadcastBoard(int sockfd) {
    string plays;
    for (size_t i = 0; i < v_plays.size(); i++) {
        plays += v_plays[i];
    }

    printPlays(v_plays);

    lock_guard<mutex> lock(clientsMutex);
    string proto = "v" + plays;
    for (auto &p : m_clients) {
        sendToClient(sockfd, proto, p.second.address);
    }
}

string padNumber(int num, int width) {
    string s = to_string(num);
    if ((int)s.size() < width)
        s = string(width - s.size(), '0') + s;
    return s;
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
    string clientKey = getClientKey(clientAddr);

    switch (type) {
    case 'n': {
        if (cleanMessage.size() < 3) return;
        
        string lenStr = cleanMessage.substr(1, 2);
        int nickLen = stoi(lenStr);
        
        if (cleanMessage.size() < 3 + nickLen) return;
        string nickname = cleanMessage.substr(3, nickLen);

        lock_guard<mutex> lock(clientsMutex);

        // Check if nickname already exists
        bool nicknameExists = false;
        for (auto &p : m_clients) {
            if (p.second.nickname == nickname) {
                nicknameExists = true;
                break;
            }
        }

        if (nicknameExists) {
            string err = "E015nicknameexiste";
            sendToClient(sockfd, err, clientAddr);
            cout << "Cliente intentó usar nickname repetido: " << nickname << endl;
        } else {
            // Register new client
            ClientInfo clientInfo;
            clientInfo.address = clientAddr;
            clientInfo.nickname = nickname;
            clientInfo.lastSeen = time(nullptr);
            m_clients[clientKey] = clientInfo;
            
            cout << "Nuevo cliente registrado con nickname[" << type << lenStr
                 << nickname << "]: " << nickname << " (" << clientKey << ")" << endl;
            
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
            if (m_clients.find(clientKey) != m_clients.end()) {
                senderNick = m_clients[clientKey].nickname;
            } else {
                return;
            }
        }

        cout << "Broadcast de [" << senderNick << "]" << msg << endl;

        lock_guard<mutex> lock(clientsMutex);
        string proto = "M" + padNumber(senderNick.size(), 2) + senderNick +
                     padNumber(msg.size(), 3) + msg;
        cout << "Broadcast protocolo [" << proto << "]\n";
        
        // Fragment if necessary and send to all clients
        string fullMessage = padToFixedSize(proto, PROTOCOL_SIZE);
        for (auto &p : m_clients) {
            if (p.first != clientKey) {
                sendto(sockfd, fullMessage.c_str(), PROTOCOL_SIZE, 0,
                      (const struct sockaddr*)&p.second.address, sizeof(p.second.address));
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

        // Get sender nickname
        string senderNick;
        {
            lock_guard<mutex> lock(clientsMutex);
            if (m_clients.find(clientKey) != m_clients.end()) {
                senderNick = m_clients[clientKey].nickname;
            } else {
                return;
            }
        }

        cout << "Mensaje privado de [" << senderNick << "] a [" << destNick
             << "]: " << msg << endl;

        lock_guard<mutex> lock(clientsMutex);
        
        // Find destination client
        bool found = false;
        for (auto &p : m_clients) {
            if (p.second.nickname == destNick) {
                string proto = "T" + padNumber(senderNick.size(), 2) + senderNick +
                             padNumber(msg.size(), 3) + msg;
                cout << "Privado protocolo [" << proto << "]\n";
                sendToClient(sockfd, proto, p.second.address);
                found = true;
                break;
            }
        }
        
        if (!found) {
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
            const string &nick = p.second.nickname;
            proto += padNumber((int)nick.size(), 2) + nick;
        }

        cout << "Listado protocolo [" << proto << "]\n";
        sendToClient(sockfd, proto, clientAddr);
        break;
    }
    case 'p': {
        lock_guard<mutex> lock(gameMutex);

        string senderNick;
        {
            lock_guard<mutex> lock(clientsMutex);
            if (m_clients.find(clientKey) != m_clients.end()) {
                senderNick = m_clients[clientKey].nickname;
            } else {
                return;
            }
        }

        auto it = find(q_players.begin(), q_players.end(), clientKey);

        if (it == q_players.end()) {
            q_players.push_back(clientKey);

            if (q_players.size() == 1) {
                cout << "Jugador 1 [" << senderNick << "] esperando oponente...\n";
                string response = "P01Esperando oponente...";
                sendToClient(sockfd, response, clientAddr);
            } else if (q_players.size() == 2) {
                cout << "Jugador 2 [" << senderNick
                     << "] se unió. ¡Iniciando juego!\n";

                broadcastBoard(sockfd);

                currentTurn = 0;
                string protoTurn = "Vo";
                sendToClient(sockfd, protoTurn, m_clients[q_players[0]].address);
                cout << "Turno enviado al Jugador 1 (o)\n";
                
                string response2 = "P02Juego iniciado - Eres X";
                sendToClient(sockfd, response2, m_clients[q_players[1]].address);
            } else {
                cout << "👁️  Espectador [" << senderNick << "] (posición "
                     << q_players.size() << ")\n";

                // Send current board to spectator
                string plays;
                for (size_t i = 0; i < v_plays.size(); i++) {
                    plays += v_plays[i];
                }
                string proto = "v" + plays;
                sendToClient(sockfd, proto, clientAddr);
                
                string response = "P03Modo espectador";
                sendToClient(sockfd, response, clientAddr);
            }
        } else {
            cout << "[" << senderNick << "] ya está en la partida\n";
            string response = "P04Ya estas en la partida";
            sendToClient(sockfd, response, clientAddr);
        }
        break;
    }
    case 'w': {
        lock_guard<mutex> lock(gameMutex);

        if (cleanMessage.size() < 3) return;
        
        string playPlayer = cleanMessage.substr(1, 1);
        string pos = cleanMessage.substr(2, 1);
        int position = stoi(pos);

        auto it = find(q_players.begin(), q_players.end(), clientKey);
        if (it == q_players.end() || (it - q_players.begin()) > 1) {
            cout << "Jugada rechazada: no es jugador activo\n";
            string response = "E01No eres jugador activo";
            sendToClient(sockfd, response, clientAddr);
            break;
        }

        int playerIndex = it - q_players.begin();
        if (playerIndex != currentTurn) {
            cout << "Jugada rechazada: no es su turno\n";
            string response = "E02No es tu turno";
            sendToClient(sockfd, response, clientAddr);
            break;
        }

        // Validate position is empty
        if (v_plays[position - 1] != " ") {
            cout << "Jugada rechazada: posición ocupada\n";
            string response = "E03Posicion ocupada";
            sendToClient(sockfd, response, clientAddr);
            break;
        }

        cout << "✓ Jugada recibida: " << playPlayer << " en posición " << position
             << endl;

        // Update board
        v_plays[position - 1] = playPlayer;

        // Broadcast board to everyone
        broadcastBoard(sockfd);

        // Switch turn (only between first 2 players)
        if (q_players.size() >= 2) {
            currentTurn = 1 - currentTurn; // Toggle between 0 and 1
            char nextPlay = (currentTurn == 0) ? 'o' : 'x';
            string protoTurn = string("V") + nextPlay;
            sendToClient(sockfd, protoTurn, m_clients[q_players[currentTurn]].address);
            cout << "Turno enviado al Jugador " << (currentTurn + 1) << " ("
                 << nextPlay << ")\n";
        }
        break;
    }
    case 'v': {
        lock_guard<mutex> lock(gameMutex);
        string senderNick;
        {
            lock_guard<mutex> lock(clientsMutex);
            if (m_clients.find(clientKey) != m_clients.end()) {
                senderNick = m_clients[clientKey].nickname;
            } else {
                return;
            }
        }
        cout << "Jugador conectado para jugar Tic-tac-toe [" << senderNick
             << "]\n";

        string plays;
        for (size_t i = 0; i < v_plays.size(); i++) {
            plays += v_plays[i];
        }
        printPlays(v_plays);
        string proto = "v" + plays;
        sendToClient(sockfd, proto, clientAddr);
        break;
    }
    case 'x': {
        lock_guard<mutex> lockC(clientsMutex);
        
        string nick;
        if (m_clients.find(clientKey) != m_clients.end()) {
            nick = m_clients[clientKey].nickname;
            m_clients.erase(clientKey);
        }

        // Remove from player queue
        {
            lock_guard<mutex> lockG(gameMutex);
            auto it = find(q_players.begin(), q_players.end(), clientKey);
            if (it != q_players.end()) {
                int playerPosition = it - q_players.begin();
                q_players.erase(it);

                cout << "Cliente [" << nick << "] desconectado (era posición "
                     << (playerPosition + 1) << ")\n";

                if (playerPosition < 2 && q_players.size() >= 2) {
                    cout << "El espectador ahora es Jugador " << (playerPosition + 1)
                         << "\n";

                    if (currentTurn == playerPosition) {
                        char nextPlay = (currentTurn == 0) ? 'o' : 'x';
                        string protoTurn = string("V") + nextPlay;
                        sendToClient(sockfd, protoTurn, m_clients[q_players[currentTurn]].address);
                        cout << "Turno enviado al nuevo Jugador " << (currentTurn + 1)
                             << " (" << nextPlay << ")\n";
                    }
                } else if (q_players.size() < 2) {
                    cout << "Juego pausado: esperando jugadores...\n";
                    currentTurn = 0;
                }
            }
        }
        
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
        sleep(30); // Check every 30 seconds
        lock_guard<mutex> lock(clientsMutex);
        time_t now = time(nullptr);
        
        for (auto it = m_clients.begin(); it != m_clients.end(); ) {
            if (now - it->second.lastSeen > 60) { // 60 seconds timeout
                cout << "Removing inactive client: " << it->second.nickname << endl;
                
                // Remove from game queue
                lock_guard<mutex> lockG(gameMutex);
                auto gameIt = find(q_players.begin(), q_players.end(), it->first);
                if (gameIt != q_players.end()) {
                    q_players.erase(gameIt);
                }
                
                it = m_clients.erase(it);
            } else {
                ++it;
            }
        }
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

    // 🔹 Permitir reutilizar el puerto inmediatamente tras cerrar el servidor
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

    // Start cleanup thread for inactive clients
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
            
            // Update last seen time
            string clientKey = getClientKey(clientAddr);
            {
                lock_guard<mutex> lock(clientsMutex);
                if (m_clients.find(clientKey) != m_clients.end()) {
                    m_clients[clientKey].lastSeen = time(nullptr);
                }
            }
            
            // Handle message
            handleClientMessage(SocketServer, message, clientAddr);
        }
    }

    close(SocketServer);
    return 0;
}