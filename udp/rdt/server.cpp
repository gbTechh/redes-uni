// Server code in C++ - UDP Version - Go-Back-N (GBN) Implementation
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

#define PORT 45000
#define BUFFER_SIZE 777

// Mapa para guardar el siguiente número de secuencia esperado por cada cliente
// (GBN Receiver)
map<string, int> clientExpectedSeqNum;

struct Packet {
  int seqnum;
  int acknum;
  int checksum;
  string payload;
};

string serializePacket(const Packet &pkt) {
  string result;
  result += to_string(pkt.seqnum) + "|";
  result += to_string(pkt.acknum) + "|";
  result += to_string(pkt.checksum) + "|";
  result += pkt.payload;
  return result;
}
Packet deserializePacket(const string &data) {
  Packet pkt;
  size_t pos1 = data.find('|');
  size_t pos2 = data.find('|', pos1 + 1);
  size_t pos3 = data.find('|', pos2 + 1);

  if (pos1 == string::npos || pos2 == string::npos || pos3 == string::npos) {
    pkt.seqnum = -1;
    return pkt;
  }

  try {
    pkt.seqnum = stoi(data.substr(0, pos1));
    pkt.acknum = stoi(data.substr(pos1 + 1, pos2 - pos1 - 1));
    pkt.checksum = stoi(data.substr(pos2 + 1, pos3 - pos2 - 1));
    pkt.payload = data.substr(pos3 + 1);
  } catch (...) {
    pkt.seqnum = -1;
  }
  return pkt;
}

int calcularChecksum(const Packet &pkt) {
  int suma = pkt.seqnum + pkt.acknum;
  for (char c : pkt.payload) {
    suma += (unsigned char)c;
  }

  // COUT de debug original mantenido
  cout << "calculate cksum fn - suma: " << suma << endl;
  int checksum = 255 - (suma % 256);
  return checksum;
}
void printPlays(vector<string> &v_plays) {
  for (size_t i = 0; i < v_plays.size(); i++) {
    if (i % 3 == 0)
      cout << "\n";
    cout << v_plays[i] << "|";
  }
  cout << "\n";
}

string padNumber(int num, int width) {
  string s = to_string(num);
  if ((int)s.size() < width)
    s = string(width - s.size(), '0') + s;
  return s;
}

string getClientKey(const struct sockaddr_in &addr) {
  char ip[INET_ADDRSTRLEN];
  inet_ntop(AF_INET, &(addr.sin_addr), ip, INET_ADDRSTRLEN);
  return string(ip) + ":" + to_string(ntohs(addr.sin_port));
}

string removePadding(const string &message) {
  size_t pos = message.find('#');
  if (pos != string::npos) {
    return message.substr(0, pos);
  }
  return message;
}

struct ClientInfo {
  struct sockaddr_in address;
  string nickname;
};

map<string, ClientInfo> m_clients; // nickname -> ClientInfo
vector<string> q_players;          // almacena nicknames
mutex clientsMutex;
mutex gameMutex;

int currentTurn = 0;
static vector<string> v_plays(9, " ");

void sendACK(int sockfd, const struct sockaddr_in &clientAddr, int ackNum) {

  Packet pkt;
  pkt.seqnum = 0;
  pkt.acknum = ackNum;
  pkt.payload = "ACK";
  pkt.checksum = calcularChecksum(pkt);

  string serialized = serializePacket(pkt);
  serialized.resize(BUFFER_SIZE, '#');

  cout << "[Server GBN] Enviando ACK #" << ackNum
       << " a cliente: " << getClientKey(clientAddr) << endl;

  sendto(sockfd, serialized.c_str(), serialized.size(), 0,
         (const struct sockaddr *)&clientAddr, sizeof(clientAddr));
}

void sendToClient(int sockfd, const string &message,
                  const struct sockaddr_in &clientAddr) {

  Packet pkt;
  pkt.seqnum = 0;
  pkt.acknum = 0; // No es un ACK
  pkt.payload = message;
  pkt.checksum = calcularChecksum(pkt);

  string serialized = serializePacket(pkt);
  serialized.resize(BUFFER_SIZE, '#');

  cout << "SEN TO CLIENTE SERIALIZED: " << serialized << endl;
  cout << "Serialized size: " << serialized.size() << endl;

  sendto(sockfd, serialized.c_str(), serialized.size(), 0,
         (const struct sockaddr *)&clientAddr, sizeof(clientAddr));
  cout << "[Server] Enviado mensaje de respuesta: [" << message << "]" << endl;
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

void processNormalMessage(int sockfd, string message,
                          const struct sockaddr_in &clientAddr) {
  if (message.empty())
    return;

  char type = message[0];
  cout << "[Process] Procesando mensaje tipo: " << type << " con contenido: ["
       << message.substr(1) << "]" << endl;

  switch (type) {
  case 'n': {
    if (message.size() < 3)
      return;

    string lenStr = message.substr(1, 2);
    int nickLen = stoi(lenStr);

    if (message.size() < 3 + nickLen)
      return;
    string nickname = message.substr(3, nickLen);

    lock_guard<mutex> lock(clientsMutex);

    if (m_clients.count(nickname)) {
      string err = "E015nicknameexiste";
      sendToClient(sockfd, err, clientAddr);
      cout << "Cliente intentó usar nickname repetido: " << nickname << endl;
    } else {
      ClientInfo clientInfo;
      clientInfo.address = clientAddr;
      clientInfo.nickname = nickname;
      m_clients[nickname] = clientInfo;

      cout << "Nuevo cliente registrado con nickname[" << type << lenStr
           << nickname << "]: " << nickname << endl;
    }
    break;
  }
  case 'm': {
    if (message.size() < 4)
      return;

    string msgLenStr = message.substr(1, 3);
    int msgLen = stoi(msgLenStr);

    if (message.size() < 4 + msgLen)
      return;
    string msg = message.substr(4, msgLen);

    string senderNick;
    {
      lock_guard<mutex> lock(clientsMutex);
      for (auto &p : m_clients) {
        if (p.second.address.sin_addr.s_addr == clientAddr.sin_addr.s_addr &&
            p.second.address.sin_port == clientAddr.sin_port) {
          senderNick = p.first;
          break;
        }
      }
    }

    if (senderNick.empty())
      return;

    cout << "Broadcast de [" << senderNick << "]" << msg << endl;

    lock_guard<mutex> lock(clientsMutex);
    string proto = "M" + padNumber(senderNick.size(), 2) + senderNick +
                   padNumber(msg.size(), 3) + msg;
    cout << "[" << proto << "]\n";
    for (auto &p : m_clients) {
      if (p.first != senderNick) {
        sendToClient(sockfd, proto, p.second.address);
      }
    }
    break;
  }
  case 't': {
    if (message.size() < 3)
      return;

    string lenStr = message.substr(1, 2);
    int destLen = stoi(lenStr);

    if (message.size() < 3 + destLen)
      return;
    string destNick = message.substr(3, destLen);

    if (message.size() < 3 + destLen + 3)
      return;
    string msgLenStr = message.substr(3 + destLen, 3);
    int msgLen = stoi(msgLenStr);

    if (message.size() < 3 + destLen + 3 + msgLen)
      return;
    string msg = message.substr(3 + destLen + 3, msgLen);

    string senderNick;
    {
      lock_guard<mutex> lock(clientsMutex);
      for (auto &p : m_clients) {
        if (p.second.address.sin_addr.s_addr == clientAddr.sin_addr.s_addr &&
            p.second.address.sin_port == clientAddr.sin_port) {
          senderNick = p.first;
          break;
        }
      }
    }

    if (senderNick.empty())
      return;

    cout << "Mensaje privado de [" << senderNick << "] a [" << destNick
         << "]: " << msg << endl;

    lock_guard<mutex> lock(clientsMutex);
    auto it = m_clients.find(destNick);
    if (it != m_clients.end()) {
      string proto = "T" + padNumber(senderNick.size(), 2) + senderNick +
                     padNumber(msg.size(), 3) + msg;
      cout << "[" << proto << "]\n";
      sendToClient(sockfd, proto, it->second.address);
    } else {
      string err = "E015Elclientenoexi";
      sendToClient(sockfd, err, clientAddr);
      // COUT de error mantenido
      cout << "Error: Cliente de destino [" << destNick << "] no encontrado."
           << endl;
    }
    break;
  }
  case 'l': {
    lock_guard<mutex> lock(clientsMutex);

    int numUsers = m_clients.size();
    string proto = "L" + padNumber(numUsers, 2);

    cout << "Construyendo listado de " << numUsers << " usuarios..." << endl;
    for (auto &p : m_clients) {
      const string &nick = p.first;
      proto += padNumber((int)nick.size(), 2) + nick;
    }

    cout << "Listado protocolo [" << proto << "]: " << "\n";
    sendToClient(sockfd, proto, clientAddr);
    break;
  }
  case 'p': { // Solicitar jugar
    lock_guard<mutex> lockG(gameMutex);

    string senderNick;
    {
      lock_guard<mutex> lockC(clientsMutex);
      for (auto &p : m_clients) {
        if (p.second.address.sin_addr.s_addr == clientAddr.sin_addr.s_addr &&
            p.second.address.sin_port == clientAddr.sin_port) {
          senderNick = p.first;
          break;
        }
      }
    }

    if (senderNick.empty())
      return;

    auto it = find(q_players.begin(), q_players.end(), senderNick);

    if (it == q_players.end()) {
      q_players.push_back(senderNick);

      if (q_players.size() == 1) {
        cout << "Jugador 1 [" << senderNick << "] esperando oponente...\n";
      } else if (q_players.size() == 2) {
        cout << "Jugador 2 [" << senderNick << "] se unió. ¡Iniciando juego!\n";

        fill(v_plays.begin(), v_plays.end(), " ");

        broadcastBoard(sockfd);

        currentTurn = 0;
        string protoTurn = "Vo";
        sendToClient(sockfd, protoTurn, m_clients[q_players[0]].address);
        cout << "Turno enviado al Jugador 1 (o)\n";
      } else {
        cout << "👁️  Espectador [" << senderNick << "] (posición "
             << q_players.size() << ")\n";

        string plays;
        for (size_t i = 0; i < v_plays.size(); i++) {
          plays += v_plays[i];
        }
        string proto = "v" + plays;
        sendToClient(sockfd, proto, clientAddr);
      }
    } else {
      cout << "[" << senderNick << "] ya está en la partida\n";
    }
    break;
  }
  case 'w': {
    lock_guard<mutex> lockG(gameMutex);

    if (message.size() < 3)
      return;

    string playPlayer = message.substr(1, 1);
    string pos = message.substr(2, 1);
    int position;
    try {
      position = stoi(pos);
    } catch (...) {
      cout << "Error: Posición de jugada no es un número." << endl;
      return;
    }

    string senderNick;
    {
      lock_guard<mutex> lockC(clientsMutex);
      for (auto &p : m_clients) {
        if (p.second.address.sin_addr.s_addr == clientAddr.sin_addr.s_addr &&
            p.second.address.sin_port == clientAddr.sin_port) {
          senderNick = p.first;
          break;
        }
      }
    }

    if (senderNick.empty())
      return;

    auto it = find(q_players.begin(), q_players.end(), senderNick);
    if (it == q_players.end() || (it - q_players.begin()) > 1) {
      cout << "Jugada rechazada: [" << senderNick << "] no es jugador activo\n";
      return;
    }

    int playerIndex = it - q_players.begin();
    if (playerIndex != currentTurn) {
      cout << "Jugada rechazada: no es su turno. Es turno de jugador "
           << (currentTurn + 1) << endl;
      return;
    }

    // Validar posición
    if (position < 1 || position > 9 || v_plays[position - 1] != " ") {
      string err = "E015Posicioninval";
      sendToClient(sockfd, err, clientAddr);
      cout << "Jugada inválida en posición " << position << endl;
      return;
    }

    cout << "✓ Jugada recibida de [" << senderNick << "]: " << playPlayer
         << " en posición " << position << endl;

    v_plays[position - 1] = playPlayer;

    broadcastBoard(sockfd);

    // Alternar turno
    if (q_players.size() >= 2) {
      currentTurn = 1 - currentTurn; // Alternar entre 0 y 1
      char nextPlay = (currentTurn == 0) ? 'o' : 'x';
      string protoTurn = string("V") + nextPlay;
      sendToClient(sockfd, protoTurn,
                   m_clients[q_players[currentTurn]].address);
      cout << "Turno enviado al Jugador " << (currentTurn + 1) << " ("
           << nextPlay << ")\n";
    }
    break;
  }
  case 'x': { // Salida del cliente
    lock_guard<mutex> lockC(clientsMutex);
    string nick;
    for (auto it = m_clients.begin(); it != m_clients.end(); ++it) {
      if (it->second.address.sin_addr.s_addr == clientAddr.sin_addr.s_addr &&
          it->second.address.sin_port == clientAddr.sin_port) {
        nick = it->first;
        m_clients.erase(it);
        cout << "Cliente [" << nick << "] eliminado de la lista de clientes."
             << endl;
        break;
      }
    }

    {
      lock_guard<mutex> lockG(gameMutex);
      auto it = find(q_players.begin(), q_players.end(), nick);
      if (it != q_players.end()) {
        int playerPosition = it - q_players.begin();
        q_players.erase(it);

        cout << "Cliente [" << nick << "] desconectado (era posición "
             << (playerPosition + 1) << ") y eliminado de la cola de juego.\n";

        // Lógica de juego mantenida
        if (playerPosition < 2 && q_players.size() >= 2) {
          cout << "El espectador ahora es Jugador " << (playerPosition + 1)
               << "\n";

          if (currentTurn == playerPosition) {
            char nextPlay = (currentTurn == 0) ? 'o' : 'x';
            string protoTurn = string("V") + nextPlay;
            sendToClient(sockfd, protoTurn,
                         m_clients[q_players[currentTurn]].address);
            cout << "Turno enviado al nuevo Jugador " << (currentTurn + 1)
                 << " (" << nextPlay << ")\n";
          }
        } else if (q_players.size() < 2) {
          cout << "Juego pausado: esperando jugadores...\n";
          currentTurn = 0;
        }
      }
    }
    break;
  }
  default:
    cout << "Tipo de mensaje desconocido: " << type << endl;
    break;
  }
}

void handleClientMessage(int sockfd, const string &rawMessage,
                         const struct sockaddr_in &clientAddr) {
  if (rawMessage.empty())
    return;

  cout << "MESSAGE: " << rawMessage << endl;

  Packet receivedPkt = deserializePacket(rawMessage);

  if (receivedPkt.seqnum == -1) {
    cout << "ERROR: Paquete recibido con formato inválido. Descartando."
         << endl;
    return;
  }

  string payloadWithoutPadding = removePadding(receivedPkt.payload);
  Packet tempPkt = receivedPkt;
  tempPkt.payload = payloadWithoutPadding;

  if (calcularChecksum(tempPkt) != receivedPkt.checksum) {
    cout << "ERROR: Checksum incorrecto para PKT #" << receivedPkt.seqnum
         << ". Descartando paquete." << endl;
    return;
  }

  string clientKey = getClientKey(clientAddr);

  if (clientExpectedSeqNum.find(clientKey) == clientExpectedSeqNum.end()) {
    clientExpectedSeqNum[clientKey] = 0;
  }
  int expected = clientExpectedSeqNum[clientKey];

  if (receivedPkt.seqnum == expected) {

    cout << "[Server GBN] Recibido PKT #" << receivedPkt.seqnum
         << " (ESPERADO). Procesando payload..." << endl;

    string message = payloadWithoutPadding;

    clientExpectedSeqNum[clientKey]++;

    sendACK(sockfd, clientAddr, clientExpectedSeqNum[clientKey]);

    processNormalMessage(sockfd, message, clientAddr);

  } else {

    cout << "[Server GBN] Recibido PKT #" << receivedPkt.seqnum
         << " (FUERA DE ORDEN/DUPLICADO). Esperado: " << expected << endl;

    if (expected >= 0) {
      sendACK(sockfd, clientAddr, expected);
    }
  }
}

int main(void) {
  struct sockaddr_in servaddr, cliaddr;
  int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
  char buffer[BUFFER_SIZE];

  if (sockfd < 0) {
    perror("socket creation failed");
    exit(EXIT_FAILURE);
  }

  int opt = 1;
  if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
    perror("setsockopt failed");
    close(sockfd);
    exit(EXIT_FAILURE);
  }

  memset(&servaddr, 0, sizeof(servaddr));
  memset(&cliaddr, 0, sizeof(cliaddr));

  servaddr.sin_family = AF_INET;
  servaddr.sin_addr.s_addr = INADDR_ANY;
  servaddr.sin_port = htons(PORT);

  if (bind(sockfd, (const struct sockaddr *)&servaddr, sizeof(servaddr)) < 0) {
    perror("bind failed");
    close(sockfd);
    exit(EXIT_FAILURE);
  }

  cout << "UDP Server listening on port " << PORT << " (GBN Enabled)..."
       << endl;

  while (true) {
    socklen_t len = sizeof(cliaddr);
    ssize_t n = recvfrom(sockfd, (char *)buffer, BUFFER_SIZE, MSG_WAITALL,
                         (struct sockaddr *)&cliaddr, &len);

    if (n > 0) {
      buffer[n] = '\0';
      string message(buffer, n);
      handleClientMessage(sockfd, message, cliaddr);
    }
  }

  close(sockfd);
  return 0;
}