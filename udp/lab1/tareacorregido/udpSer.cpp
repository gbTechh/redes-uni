/* Server code in C++ - UDP Version */

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
#define BUFFER_SIZE 1024

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
};

map<string, ClientInfo> m_clients; // nickname -> ClientInfo
vector<string> q_players;          // almacena nicknames en lugar de sockets
mutex clientsMutex;
mutex gameMutex;

string padNumber(int num, int width) {
  string s = to_string(num);
  if ((int)s.size() < width)
    s = string(width - s.size(), '0') + s;
  return s;
}

void sendToClient(int sockfd, const string &message,
                  const struct sockaddr_in &clientAddr) {
  sendto(sockfd, message.c_str(), message.size(), 0,
         (const struct sockaddr *)&clientAddr, sizeof(clientAddr));
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

void handleClientMessage(int sockfd, const string &message,
                         const struct sockaddr_in &clientAddr) {
  if (message.empty())
    return;

  char type = message[0];

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
      // Registrar nuevo cliente
      ClientInfo clientInfo;
      clientInfo.address = clientAddr;
      clientInfo.nickname = nickname;
      m_clients[nickname] = clientInfo;
      cout << "Nuevo cliente registrado: " << nickname << endl;
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

    // obtener nickname del emisor
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
      sendToClient(sockfd, proto, it->second.address);
    } else {
      string err = "e03ERR";
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

    cout << "Listado usuarios solicitado" << endl;
    sendToClient(sockfd, proto, clientAddr);
    break;
  }
  case 'p': {
    lock_guard<mutex> lock(gameMutex);

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

    auto it = find(q_players.begin(), q_players.end(), senderNick);

    if (it == q_players.end()) {
      q_players.push_back(senderNick);

      if (q_players.size() == 1) {
        cout << "Jugador 1 [" << senderNick << "] esperando oponente...\n";
      } else if (q_players.size() == 2) {
        cout << "Jugador 2 [" << senderNick << "] se unió. ¡Iniciando juego!\n";

        broadcastBoard(sockfd);

        currentTurn = 0;
        string protoTurn = "Vo";
        sendToClient(sockfd, protoTurn, m_clients[q_players[0]].address);
        cout << "Turno enviado al Jugador 1 (o)\n";
      } else {
        cout << "👁️  Espectador [" << senderNick << "] (posición "
             << q_players.size() << ")\n";

        // Enviar tablero actual al espectador
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
    lock_guard<mutex> lock(gameMutex);

    if (message.size() < 3)
      return;

    string playPlayer = message.substr(1, 1);
    string pos = message.substr(2, 1);
    int position = stoi(pos);

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

    auto it = find(q_players.begin(), q_players.end(), senderNick);
    if (it == q_players.end() || (it - q_players.begin()) > 1) {
      cout << "Jugada rechazada: no es jugador activo\n";
      break;
    }

    int playerIndex = it - q_players.begin();
    if (playerIndex != currentTurn) {
      cout << "Jugada rechazada: no es su turno\n";
      break;
    }

    cout << "✓ Jugada recibida: " << playPlayer << " en posición " << position
         << endl;

    // Actualizar tablero
    v_plays[position - 1] = playPlayer;

    // Broadcast del tablero a todos
    broadcastBoard(sockfd);

    // Alternar turno (solo entre los 2 primeros jugadores)
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
  case 'v': {
    lock_guard<mutex> lock(gameMutex);
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

    cout << "Jugador conectado para jugar Tic-tac-toe [" << senderNick << "]\n";

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
    for (auto it = m_clients.begin(); it != m_clients.end(); ++it) {
      if (it->second.address.sin_addr.s_addr == clientAddr.sin_addr.s_addr &&
          it->second.address.sin_port == clientAddr.sin_port) {
        nick = it->first;
        m_clients.erase(it);
        break;
      }
    }

    // Remover de la cola de jugadores
    {
      lock_guard<mutex> lockG(gameMutex);
      auto it = find(q_players.begin(), q_players.end(), nick);
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

int main(void) {
  struct sockaddr_in servaddr, cliaddr;
  int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
  char buffer[BUFFER_SIZE];

  if (sockfd < 0) {
    perror("socket creation failed");
    exit(EXIT_FAILURE);
  }

  // Permitir reutilizar el puerto
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

  cout << "UDP Server listening on port " << PORT << "..." << endl;

  while (true) {
    socklen_t len = sizeof(cliaddr);
    ssize_t n = recvfrom(sockfd, (char *)buffer, BUFFER_SIZE, MSG_WAITALL,
                         (struct sockaddr *)&cliaddr, &len);

    if (n > 0) {
      buffer[n] = '\0';
      string message(buffer, n);

      // Manejar el mensaje en el hilo principal
      // En una aplicación real podrías usar threads aquí también
      handleClientMessage(sockfd, message, cliaddr);
    }
  }

  close(sockfd);
  return 0;
}