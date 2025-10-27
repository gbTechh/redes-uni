// Client UDP Version with 777-byte protocol
#include <arpa/inet.h>
#include <iostream>
#include <netinet/in.h>
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
#define MAX_RETRIES 3
#define TIMEOUT_MS 2000

void printPlays(vector<string> &v_plays) {
  for (size_t i = 0; i < v_plays.size(); i++) {
    if (i % 3 == 0)
      cout << "\n";
    cout << v_plays[i] << "|";
  }
  cout << "\n";
}

void makeAndPrintPlays(string table) {
  vector<string> v_plays(9);
  for (size_t i = 0; i < table.size(); i++) {
    v_plays[i] = table[i];
  }
  printPlays(v_plays);
}

string jugada;
bool inGame = false;

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
        return ""; // All padding characters
    }
    return data.substr(0, endPos + 1);
}

struct sockaddr_in serverAddr;

bool sendToServerWithRetry(int sockfd, const string &message) {
    string paddedMessage = padToFixedSize(message, PROTOCOL_SIZE);
    
    for (int attempt = 0; attempt < MAX_RETRIES; attempt++) {
        ssize_t sent = sendto(sockfd, paddedMessage.c_str(), PROTOCOL_SIZE, 0,
                             (const struct sockaddr*)&serverAddr, sizeof(serverAddr));
        
        if (sent == PROTOCOL_SIZE) {
            return true;
        }
        
        if (attempt < MAX_RETRIES - 1) {
            cout << "Reintento " << (attempt + 1) << "/" << MAX_RETRIES << endl;
            this_thread::sleep_for(chrono::milliseconds(500));
        }
    }
    
    cout << "Error: No se pudo enviar mensaje después de " << MAX_RETRIES << " intentos" << endl;
    return false;
}

void readThreadFn(int socketConn) {
    char buffer[PROTOCOL_SIZE + 1];
    
    while (true) {
        struct sockaddr_in fromAddr;
        socklen_t fromLen = sizeof(fromAddr);
        
        // Set timeout for receiving
        struct timeval tv;
        tv.tv_sec = TIMEOUT_MS / 1000;
        tv.tv_usec = (TIMEOUT_MS % 1000) * 1000;
        setsockopt(socketConn, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        
        ssize_t n = recvfrom(socketConn, buffer, PROTOCOL_SIZE, 0,
                            (struct sockaddr*)&fromAddr, &fromLen);
        
        if (n <= 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                continue; // Timeout, continue listening
            }
            break;
        }
        
        buffer[n] = '\0';
        string receivedMessage(buffer, n);
        string cleanMessage = removePadding(receivedMessage);
        
        if (cleanMessage.empty()) continue;

        // Verify the response is from our server
        if (fromAddr.sin_addr.s_addr != serverAddr.sin_addr.s_addr ||
            fromAddr.sin_port != serverAddr.sin_port) {
            continue; // Ignore messages from other sources
        }

        char type = cleanMessage[0];
        string messageContent = cleanMessage.substr(1);

        switch (type) {
        case 'T': {
            if (messageContent.size() < 2) break;
            
            string lenNickStr = messageContent.substr(0, 2);
            int lenNick = stoi(lenNickStr);
            
            if (messageContent.size() < 2 + lenNick) break;
            string sender = messageContent.substr(2, lenNick);
            
            if (messageContent.size() < 2 + lenNick + 3) break;
            string lenMsgStr = messageContent.substr(2 + lenNick, 3);
            int lenMsg = stoi(lenMsgStr);
            
            if (messageContent.size() < 2 + lenNick + 3 + lenMsg) break;
            string msg = messageContent.substr(2 + lenNick + 3, lenMsg);

            cout << "\n[PRIVADO de " << sender << "] " << msg << endl;
            cout << "> ";
            cout.flush();
            break;
        }
        case 'M': {
            if (messageContent.size() < 2) break;
            
            string lenNickStr = messageContent.substr(0, 2);
            int lenNick = stoi(lenNickStr);
            
            if (messageContent.size() < 2 + lenNick) break;
            string sender = messageContent.substr(2, lenNick);
            
            if (messageContent.size() < 2 + lenNick + 3) break;
            string lenMsgStr = messageContent.substr(2 + lenNick, 3);
            int lenMsg = stoi(lenMsgStr);
            
            if (messageContent.size() < 2 + lenNick + 3 + lenMsg) break;
            string msg = messageContent.substr(2 + lenNick + 3, lenMsg);

            cout << "\n[BROADCAST de " << sender << "] " << msg << endl;
            cout << "> ";
            cout.flush();
            break;
        }
        case 'V': {
            if (messageContent.size() < 1) break;
            
            inGame = true;
            string play = messageContent.substr(0, 1);
            cout << "\n🎮 TE TOCA JUGAR! Tu ficha es [" << play << "]" << endl;
            jugada = play;
            cout << "Selecciona posición [1-9] o 5 para volver al menú: ";
            cout.flush();
            break;
        }
        case 'v': {
            if (messageContent.size() < 9) break;
            
            string tablero = messageContent.substr(0, 9);
            cout << "\n[TABLERO ACTUAL]" << endl;
            makeAndPrintPlays(tablero);
            cout << "> ";
            cout.flush();
            break;
        }
        case 'E': {
            cout << "\n[ERROR] " << messageContent << endl;
            cout << "> ";
            cout.flush();
            break;
        }
        case 'L': {
            if (messageContent.size() < 2) break;
            
            string countStr = messageContent.substr(0, 2);
            int numUsers = stoi(countStr);

            cout << "\n[USUARIOS CONECTADOS] ";
            int pos = 2;
            for (int i = 0; i < numUsers; ++i) {
                if (messageContent.size() < pos + 2) break;
                
                string lenStr = messageContent.substr(pos, 2);
                pos += 2;
                int len = stoi(lenStr);
                
                if (messageContent.size() < pos + len) break;
                string nick = messageContent.substr(pos, len);
                pos += len;
                
                cout << nick << " ";
            }
            cout << endl << "> ";
            cout.flush();
            break;
        }
        case 'O': // OK confirmation
        case 'P': // Game status
        case 'X': // Exit confirmation
            cout << "\n[INFO] " << messageContent << endl;
            cout << "> ";
            cout.flush();
            break;
        default: {
            cout << "\n[RESPUESTA DESCONOCIDA] Tipo: " << type << " Contenido: " << messageContent << endl;
            cout << "> ";
            cout.flush();
            break;
        }
        }
    }
    cout << "Conexión con el servidor perdida." << endl;
}

int main() {
    int port = 45000;
    const char *serverIP = "127.0.0.1";

    int SocketCli = socket(PF_INET, SOCK_DGRAM, 0);
    if (SocketCli < 0) {
        perror("cannot create socket");
        return 1;
    }

    // Set socket to non-blocking for better control
    int flags = fcntl(SocketCli, F_GETFL, 0);
    fcntl(SocketCli, F_SETFL, flags | O_NONBLOCK);

    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(port);
    if (inet_pton(AF_INET, serverIP, &serverAddr.sin_addr) <= 0) {
        perror("inet_pton failed");
        close(SocketCli);
        return 1;
    }

    cout << "Cliente UDP iniciado - Protocolo de " << PROTOCOL_SIZE << " bytes" << endl;
    
    string nickname;
    cout << "Escriba su nickname: ";
    getline(cin, nickname);
    if (nickname.empty())
        nickname = "user";

    // Register with server
    string reg = "n" + padNumber((int)nickname.size(), 2) + nickname;
    cout << "Enviando registro: [" << reg << "]" << endl;
    
    if (!sendToServerWithRetry(SocketCli, reg)) {
        cout << "Error: No se pudo registrar con el servidor" << endl;
        close(SocketCli);
        return 1;
    }

    cout << "Esperando confirmación del servidor..." << endl;
    this_thread::sleep_for(chrono::seconds(1));

    thread reader(readThreadFn, SocketCli);

    int opt = 0;
    while (opt != 5) {
        if (!inGame) {
            cout << "\n--- MENU PRINCIPAL ---" << endl;
            cout << "1. Enviar mensaje privado" << endl;
            cout << "2. Enviar mensaje broadcast" << endl;
            cout << "3. Listar usuarios conectados" << endl;
            cout << "4. Unirse al juego Tic Tac Toe" << endl;
            cout << "5. Salir" << endl;
            cout << "Seleccione opción: ";
            
            if (!(cin >> opt)) {
                cin.clear();
                cin.ignore(10000, '\n');
                opt = 0;
            }
            cin.ignore(); // limpiar \n restante
        } else {
            // When in game, show simplified menu
            cout << "\n--- EN JUEGO ---" << endl;
            cout << "1-9. Seleccionar posición" << endl;
            cout << "5. Volver al menú principal" << endl;
            cout << "Seleccione posición [1-9] o 5 para menú: ";
            
            string input;
            getline(cin, input);
            
            if (input.length() == 1 && input[0] >= '1' && input[0] <= '9') {
                if (input[0] == '5') {
                    opt = 5;
                    inGame = false;
                } else {
                    // Send move
                    string payload = string("w") + jugada + input;
                    cout << "Enviando jugada: [" << payload << "]" << endl;
                    sendToServerWithRetry(SocketCli, payload);
                    continue;
                }
            } else {
                cout << "Entrada inválida. Use 1-9 para jugar, 5 para menú." << endl;
                continue;
            }
        }

        if (opt == 1) {
            string payloadList = "l";
            cout << "Solicitando lista de usuarios..." << endl;
            sendToServerWithRetry(SocketCli, payloadList);
            
            // Wait a bit for the list to arrive
            this_thread::sleep_for(chrono::milliseconds(500));
            
            string dest, msg;
            cout << "Nickname del destinatario: ";
            getline(cin, dest);
            cout << "Mensaje: ";
            getline(cin, msg);
            
            string payload = "t" + padNumber((int)dest.size(), 2) + dest +
                           padNumber((int)msg.size(), 3) + msg;
            cout << "Enviando mensaje privado: [" << payload << "]" << endl;
            sendToServerWithRetry(SocketCli, payload);
            
        } else if (opt == 2) {
            string msg;
            cout << "Mensaje para todos: ";
            getline(cin, msg);
            string payload = "m" + padNumber((int)msg.size(), 3) + msg;
            cout << "Enviando broadcast: [" << payload << "]" << endl;
            sendToServerWithRetry(SocketCli, payload);
            
        } else if (opt == 3) {
            string payload = "l";
            cout << "Solicitando lista de usuarios..." << endl;
            sendToServerWithRetry(SocketCli, payload);
            
        } else if (opt == 4) {
            string payload = "p";
            cout << "Uniéndose al juego..." << endl;
            sendToServerWithRetry(SocketCli, payload);
            inGame = true;
            
        } else if (opt == 5) {
            string payload = "x";
            cout << "Desconectando..." << endl;
            sendToServerWithRetry(SocketCli, payload);
            break;
        } else {
            cout << "Opción no válida" << endl;
        }
    }

    // Cleanup
    this_thread::sleep_for(chrono::seconds(1));
    close(SocketCli);
    if (reader.joinable()) {
        reader.join();
    }

    cout << "Cliente finalizado." << endl;
    return 0;
}