/* Server code in C */

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

map<string, int> m_clients;
vector<int> q_players;
mutex clientsMutex;
mutex gameMutex;

void broadcastBoard() {
  string plays;
  for (size_t i = 0; i < v_plays.size(); i++) {
    plays += v_plays[i];
  }

  printPlays(v_plays);

  lock_guard<mutex> lock(clientsMutex);
  string proto = "v" + plays;
  for (auto &p : m_clients) {
    write(p.second, proto.c_str(), proto.size());
  }
}

string padNumber(int num, int width) {
  string s = to_string(num);
  if ((int)s.size() < width)
    s = string(width - s.size(), '0') + s;
  return s;
}

void readThread(int socketConn) {
  char type;
  ssize_t n;
  do {
    n = read(socketConn, &type, 1);

    if (n <= 0) {
      break;
    }

    switch (type) {
    case 'n': {
      string lenStr;
      for (int i = 0; i < 2; ++i) {
        char c;
        if (read(socketConn, &c, 1) <= 0)
          return;
        lenStr += c;
      }
      int nickLen = stoi(lenStr);

      string nickname;
      for (int i = 0; i < nickLen; ++i) {
        char c;
        if (read(socketConn, &c, 1) <= 0)
          return;
        nickname += c;
      }

      lock_guard<mutex> lock(clientsMutex);

      if (m_clients.count(nickname)) {
        string err = "E015nicknameexiste";
        write(socketConn, err.c_str(), err.size());
        cout << "Cliente intentó usar nickname repetido: " << nickname << endl;
      } else {
        // Registrar nuevo cliente
        m_clients[nickname] = socketConn;
        cout << "Nuevo cliente registrado con nickname[" << type << lenStr
             << nickname << "]: " << nickname << endl;
      }

      break;
    }
    case 'm': {
      string msgLenStr;
      for (int i = 0; i < 3; ++i) {
        char c;
        if (read(socketConn, &c, 1) <= 0)
          return;
        msgLenStr += c;
      }
      int msgLen = stoi(msgLenStr);

      string msg;
      for (int i = 0; i < msgLen; ++i) {
        char c;
        if (read(socketConn, &c, 1) <= 0)
          return;
        msg += c;
      }

      string senderNick;
      {
        lock_guard<mutex> lock(clientsMutex);
        for (auto &p : m_clients) {
          if (p.second == socketConn) {
            senderNick = p.first;
            break;
          }
        }
      }

      cout << "Broadcast de [" << senderNick << "]" << msg << endl;

      lock_guard<mutex> lock(clientsMutex);
      string proto = "M" + padNumber(senderNick.size(), 2) + senderNick +
                     padNumber(msg.size(), 3) + msg;
      cout << "[" << proto << "]\n";
      for (auto &p : m_clients) {
        if (p.second != socketConn) {
          write(p.second, proto.c_str(), proto.size());
        }
      }
      break;
    }
    case 't': {
      string lenStr;
      for (int i = 0; i < 2; ++i) {
        char c;
        if (read(socketConn, &c, 1) <= 0)
          return;
        lenStr += c;
      }
      int destLen = stoi(lenStr);

      string destNick;
      for (int i = 0; i < destLen; ++i) {
        char c;
        if (read(socketConn, &c, 1) <= 0)
          return;
        destNick += c;
      }

      string msgLenStr;
      for (int i = 0; i < 3; ++i) {
        char c;
        if (read(socketConn, &c, 1) <= 0)
          return;
        msgLenStr += c;
      }
      int msgLen = stoi(msgLenStr);

      string msg;
      for (int i = 0; i < msgLen; ++i) {
        char c;
        if (read(socketConn, &c, 1) <= 0)
          return;
        msg += c;
      }

      // obtener nickname del emisor
      string senderNick;
      {
        lock_guard<mutex> lock(clientsMutex);
        for (auto &p : m_clients) {
          if (p.second == socketConn) {
            senderNick = p.first;
            break;
          }
        }
      }

      cout << "Mensaje privado de [" << senderNick << "] a [" << destNick
           << "]: " << msg << endl;

      lock_guard<mutex> lock(clientsMutex);
      auto it = m_clients.find(destNick);
      if (it != m_clients.end()) {
        int destSock = it->second;
        string proto = "T" + padNumber(senderNick.size(), 2) + senderNick +
                       padNumber(msg.size(), 3) + msg;
        cout << "[" << proto << "]\n";
        write(destSock, proto.c_str(), proto.size());
      } else {
        string err = "e03ERR";
        write(socketConn, err.c_str(), err.size());
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

      cout << "Listado protocolo [" << proto << "]: " << "\n";
      write(socketConn, proto.c_str(), proto.size());
      break;
    }
    case 'p': {
      lock_guard<mutex> lock(gameMutex);

      string senderNick;
      {
        lock_guard<mutex> lock(clientsMutex);
        for (auto &p : m_clients) {
          if (p.second == socketConn) {
            senderNick = p.first;
            break;
          }
        }
      }

      auto it = find(q_players.begin(), q_players.end(), socketConn);

      if (it == q_players.end()) {
        q_players.push_back(socketConn);

        if (q_players.size() == 1) {
          cout << "Jugador 1 [" << senderNick << "] esperando oponente...\n";
        } else if (q_players.size() == 2) {
          cout << "Jugador 2 [" << senderNick
               << "] se unió. ¡Iniciando juego!\n";

          broadcastBoard();

          currentTurn = 0;
          string protoTurn = "Vo";
          write(q_players[0], protoTurn.c_str(), protoTurn.size());
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
          write(socketConn, proto.c_str(), proto.size());
        }
      } else {
        cout << "[" << senderNick << "] ya está en la partida\n";
      }

      break;
    }

    case 'w': {
      lock_guard<mutex> lock(gameMutex);

      // Leer jugada (x u o)
      string playPlayer;
      for (int i = 0; i < 1; ++i) {
        char c;
        if (read(socketConn, &c, 1) <= 0)
          return;
        playPlayer += c;
      }

      // Leer posición (1-9)
      string pos;
      for (int i = 0; i < 1; ++i) {
        char c;
        if (read(socketConn, &c, 1) <= 0)
          return;
        pos += c;
      }

      int position = stoi(pos);

      auto it = find(q_players.begin(), q_players.end(), socketConn);
      if (it == q_players.end() || (it - q_players.begin()) > 1) {
        cout << "Jugada rechazada: no es jugador activo\n";
        break;
      }

      if (q_players[currentTurn] != socketConn) {
        cout << "Jugada rechazada: no es su turno\n";
        break;
      }

      cout << "✓ Jugada recibida: " << playPlayer << " en posición " << position
           << endl;

      // Actualizar tablero
      v_plays[position - 1] = playPlayer;

      // Broadcast del tablero a todos
      broadcastBoard();

      // Alternar turno (solo entre los 2 primeros jugadores)
      if (q_players.size() >= 2) {
        currentTurn = 1 - currentTurn; // Alternar entre 0 y 1
        char nextPlay = (currentTurn == 0) ? 'o' : 'x';
        string protoTurn = string("V") + nextPlay;
        write(q_players[currentTurn], protoTurn.c_str(), protoTurn.size());
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
          if (p.second == socketConn) {
            senderNick = p.first;
            break;
          }
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
      write(socketConn, proto.c_str(), proto.size());

      break;
    }
    case 'x': {
      lock_guard<mutex> lockC(clientsMutex);
      string nick;
      for (auto it = m_clients.begin(); it != m_clients.end(); ++it) {
        if (it->second == socketConn) {
          nick = it->first;
          m_clients.erase(it);
          break;
        }
      }

      // Remover de la cola de jugadores
      {
        lock_guard<mutex> lockG(gameMutex);
        auto it = find(q_players.begin(), q_players.end(), socketConn);
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
              write(q_players[currentTurn], protoTurn.c_str(),
                    protoTurn.size());
              cout << "Turno enviado al nuevo Jugador " << (currentTurn + 1)
                   << " (" << nextPlay << ")\n";
            }
          } else if (q_players.size() < 2) {
            cout << "Juego pausado: esperando jugadores...\n";
            currentTurn = 0;
          }
        }
      }

      close(socketConn);
      return;
    }
    default:
      cout << "Tipo de mensaje desconocido: " << type << endl;
      break;
    }

  } while (true);

  close(socketConn);
}

int main(void) {
  struct sockaddr_in stSockAddr;
  int SocketServer = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
  char buffer[256];
  int n;
  string buf;

  if (-1 == SocketServer) {
    perror("can not create socket");
    exit(EXIT_FAILURE);
  }

  // 🔹 Permitir reutilizar el puerto inmediatamente tras cerrar el servidor
  int opt = 1;
  if (setsockopt(SocketServer, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) <
      0) {
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

  if (-1 == listen(SocketServer, 10)) {
    perror("error listen failed");
    close(SocketServer);
    exit(EXIT_FAILURE);
  }

  for (;;) {

    int ConnectFD = accept(SocketServer, NULL, NULL);
    if (ConnectFD == -1) {
      perror("accept failed");
      continue;
    }
    cout << "Client connected" << endl;
    thread clientThread(readThread, ConnectFD);
    clientThread.detach();
  }

  close(SocketServer);
  return 0;
}