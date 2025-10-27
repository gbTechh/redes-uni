#include <arpa/inet.h>
#include <fstream>
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

string leerArchivo(string filename) {
  ifstream archivo(filename, ios::binary | ios::ate);
  if (!archivo) {
    cout << "Error: No se pudo abrir el archivo '" << filename << "'" << endl;
    return "";
  }

  streamsize size = archivo.tellg();
  archivo.seekg(0, ios::beg);

  string content(size, '\0');
  if (archivo.read(&content[0], size)) {
    return content;
  }

  return "";
}

void guardarArchivo(const string &filename, const string &data) {
  string nombreDestino;
  size_t puntoPos = filename.find_last_of('.');

  if (puntoPos != string::npos) {
    nombreDestino =
        filename.substr(0, puntoPos) + "-destino" + filename.substr(puntoPos);
  } else {
    nombreDestino = filename + "-destino";
  }

  ofstream archivo(nombreDestino, ios::binary);
  if (archivo) {
    archivo.write(data.c_str(), data.size());
    archivo.close();
    cout << "Archivo guardado como: " << nombreDestino << endl;
  } else {
    cout << "Error al guardar el archivo: " << nombreDestino << endl;
  }
}

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

struct sockaddr_in serverAddr;
map<int, string> fileFragments; // For reassembling fragmented files

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

vector<string> fragmentLargeMessage(const string &message, size_t maxFragmentSize) {
    vector<string> fragments;
    for (size_t i = 0; i < message.size(); i += maxFragmentSize) {
        fragments.push_back(message.substr(i, maxFragmentSize));
    }
    return fragments;
}

void readThreadFn(int socketConn) {
    char buffer[PROTOCOL_SIZE + 1];
    
    while (true) {
        struct sockaddr_in fromAddr;
        socklen_t fromLen = sizeof(fromAddr);
        
        struct timeval tv;
        tv.tv_sec = TIMEOUT_MS / 1000;
        tv.tv_usec = (TIMEOUT_MS % 1000) * 1000;
        setsockopt(socketConn, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        
        ssize_t n = recvfrom(socketConn, buffer, PROTOCOL_SIZE, 0,
                            (struct sockaddr*)&fromAddr, &fromLen);
        
        if (n <= 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;
            }
            break;
        }
        
        buffer[n] = '\0';
        string receivedMessage(buffer, n);
        string cleanMessage = removePadding(receivedMessage);
        
        if (cleanMessage.empty()) continue;

        if (fromAddr.sin_addr.s_addr != serverAddr.sin_addr.s_addr ||
            fromAddr.sin_port != serverAddr.sin_port) {
            continue;
        }

        char type = cleanMessage[0];

        switch (type) {
        case 'T': {
            if (cleanMessage.size() < 2) break;
            
            string lenNickStr = cleanMessage.substr(1, 2);
            int lenNick = stoi(lenNickStr);
            
            if (cleanMessage.size() < 2 + lenNick) break;
            string sender = cleanMessage.substr(2, lenNick);
            
            if (cleanMessage.size() < 2 + lenNick + 3) break;
            string lenMsgStr = cleanMessage.substr(2 + lenNick, 3);
            int lenMsg = stoi(lenMsgStr);
            
            if (cleanMessage.size() < 2 + lenNick + 3 + lenMsg) break;
            string msg = cleanMessage.substr(2 + lenNick + 3, lenMsg);

            cout << "\n[PRIVADO de " << sender << "] " << msg << endl;
            cout << "> ";
            cout.flush();
            break;
        }
        case 'M': {
            if (cleanMessage.size() < 2) break;
            
            string lenNickStr = cleanMessage.substr(1, 2);
            int lenNick = stoi(lenNickStr);
            
            if (cleanMessage.size() < 2 + lenNick) break;
            string sender = cleanMessage.substr(2, lenNick);
            
            if (cleanMessage.size() < 2 + lenNick + 3) break;
            string lenMsgStr = cleanMessage.substr(2 + lenNick, 3);
            int lenMsg = stoi(lenMsgStr);
            
            if (cleanMessage.size() < 2 + lenNick + 3 + lenMsg) break;
            string msg = cleanMessage.substr(2 + lenNick + 3, lenMsg);

            cout << "\n[BROADCAST de " << sender << "] " << msg << endl;
            cout << "> ";
            cout.flush();
            break;
        }
        case 'E': {
            cout << "\n[ERROR] " << cleanMessage.substr(1) << endl;
            cout << "> ";
            cout.flush();
            break;
        }
        case 'F': {
            // Check if this is a fragmented file (starts with fragment info)
            if (cleanMessage.size() > 7 && isdigit(cleanMessage[1])) {
                // Fragmented file
                string fragNumStr = cleanMessage.substr(1, 3);
                string totalFragsStr = cleanMessage.substr(4, 3);
                int fragNum = stoi(fragNumStr);
                int totalFrags = stoi(totalFragsStr);
                
                string fragmentData = cleanMessage.substr(7);
                
                // Store fragment
                fileFragments[fragNum] = fragmentData;
                
                cout << "\n[ARCHIVO] Recibiendo fragmento " << (fragNum + 1) << "/" << totalFrags << endl;
                
                // If all fragments received, reassemble
                if (fileFragments.size() == totalFrags) {
                    string fullData;
                    for (int i = 0; i < totalFrags; i++) {
                        fullData += fileFragments[i];
                    }
                    
                    // Parse the complete file data
                    if (fullData.size() >= 2) {
                        string senderLenStr = fullData.substr(0, 2);
                        int senderLen = stoi(senderLenStr);
                        
                        if (fullData.size() >= 2 + senderLen + 3) {
                            string sender = fullData.substr(2, senderLen);
                            string filenameLenStr = fullData.substr(2 + senderLen, 3);
                            int filenameLen = stoi(filenameLenStr);
                            
                            if (fullData.size() >= 2 + senderLen + 3 + filenameLen + 10) {
                                string filename = fullData.substr(2 + senderLen + 3, filenameLen);
                                string fileDataLenStr = fullData.substr(2 + senderLen + 3 + filenameLen, 10);
                                long fileDataLen = stol(fileDataLenStr);
                                
                                if (fullData.size() >= 2 + senderLen + 3 + filenameLen + 10 + fileDataLen) {
                                    string fileData = fullData.substr(2 + senderLen + 3 + filenameLen + 10, fileDataLen);
                                    
                                    guardarArchivo(filename, fileData);
                                    fileFragments.clear();
                                }
                            }
                        }
                    }
                }
            } else {
                // Regular file transfer
                if (cleanMessage.size() < 2) break;
                
                string lenStr = cleanMessage.substr(1, 2);
                int senderLen = stoi(lenStr);
                
                if (cleanMessage.size() < 2 + senderLen) break;
                string sender = cleanMessage.substr(2, senderLen);
                
                if (cleanMessage.size() < 2 + senderLen + 3) break;
                string sizeFilename = cleanMessage.substr(2 + senderLen, 3);
                int filenameLen = stoi(sizeFilename);
                
                if (cleanMessage.size() < 2 + senderLen + 3 + filenameLen) break;
                string filename = cleanMessage.substr(2 + senderLen + 3, filenameLen);
                
                if (cleanMessage.size() < 2 + senderLen + 3 + filenameLen + 10) break;
                string sizeFileData = cleanMessage.substr(2 + senderLen + 3 + filenameLen, 10);
                long fileDataLen = stol(sizeFileData);
                
                if (cleanMessage.size() < 2 + senderLen + 3 + filenameLen + 10 + fileDataLen) break;
                string fileData = cleanMessage.substr(2 + senderLen + 3 + filenameLen + 10, fileDataLen);

                guardarArchivo(filename, fileData);
            }
            break;
        }
        case 'L': {
            if (cleanMessage.size() < 2) break;
            
            string countStr = cleanMessage.substr(1, 2);
            int numUsers = stoi(countStr);

            cout << "\n[USUARIOS CONECTADOS] ";
            int pos = 2;
            for (int i = 0; i < numUsers; ++i) {
                if (cleanMessage.size() < pos + 2) break;
                
                string lenStr = cleanMessage.substr(pos, 2);
                pos += 2;
                int len = stoi(lenStr);
                
                if (cleanMessage.size() < pos + len) break;
                string nick = cleanMessage.substr(pos, len);
                pos += len;
                
                cout << nick << " ";
            }
            cout << endl << "> ";
            cout.flush();
            break;
        }
        case 'O': // OK
        case 'X': // Exit confirmation
            cout << "\n[INFO] " << cleanMessage.substr(1) << endl;
            cout << "> ";
            cout.flush();
            break;
        default: {
            cout << "\n[RESPUESTA DESCONOCIDA] Tipo: " << type << endl;
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
    do {
        cout << "\n--- MENU PRINCIPAL ---" << endl;
        cout << "1. Enviar mensaje privado" << endl;
        cout << "2. Enviar mensaje broadcast" << endl;
        cout << "3. Listar usuarios conectados" << endl;
        cout << "4. Enviar un archivo" << endl;
        cout << "5. Salir" << endl;
        cout << "Seleccione opción: ";
        
        if (!(cin >> opt)) {
            cin.clear();
            cin.ignore(10000, '\n');
            opt = 0;
        }
        cin.ignore();

        if (opt == 1) {
            string payloadList = "l";
            cout << "Solicitando lista de usuarios..." << endl;
            sendToServerWithRetry(SocketCli, payloadList);
            
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
            string dest, filename;
            cout << "Nickname del destinatario: ";
            getline(cin, dest);
            cout << "Nombre del archivo: ";
            getline(cin, filename);

            string fileData = leerArchivo(filename);
            if (fileData.empty()) {
                cout << "No se pudo leer el archivo." << endl;
                continue;
            }

            string payload = "f" + padNumber((int)dest.size(), 2) + dest +
                           padNumber((int)filename.size(), 3) + filename +
                           padNumber((int)fileData.size(), 10) + fileData;

            cout << "Tamaño del payload: " << payload.size() << " bytes" << endl;
            
            // Handle large files by fragmenting
            if (payload.size() > PROTOCOL_SIZE) {
                cout << "Archivo demasiado grande, fragmentando..." << endl;
                vector<string> fragments = fragmentLargeMessage(payload, PROTOCOL_SIZE - 7);
                for (size_t i = 0; i < fragments.size(); i++) {
                    string fragmentPayload = "f" + padNumber(i, 3) + padNumber(fragments.size(), 3) + fragments[i];
                    cout << "Enviando fragmento " << (i + 1) << "/" << fragments.size() << endl;
                    sendToServerWithRetry(SocketCli, fragmentPayload);
                    this_thread::sleep_for(chrono::milliseconds(100)); // Small delay between fragments
                }
            } else {
                sendToServerWithRetry(SocketCli, payload);
            }
            
        } else if (opt == 5) {
            string payload = "x";
            cout << "Desconectando..." << endl;
            sendToServerWithRetry(SocketCli, payload);
            break;
        } else {
            cout << "Opción no válida" << endl;
        }

    } while (opt != 5);

    this_thread::sleep_for(chrono::seconds(1));
    close(SocketCli);
    if (reader.joinable()) {
        reader.join();
    }

    cout << "Cliente finalizado." << endl;
    return 0;
}