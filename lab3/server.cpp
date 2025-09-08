/* Server code in C */

#include <arpa/inet.h>
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

map<string, int> m_clients;
mutex clientsMutex;

string padNumber(int num, int width) {
  string s = to_string(num);
  if ((int)s.size() < width)
    s = string(width - s.size(), '0') + s;
  return s;
}
void readThread(int socketConn) {
  char buffer[256];
  int n;
  char type;
  do {
    n = read(socketConn, buffer, 255);
    if (n <= 0) {
      break; // error o cliente desconectado
    }
    buffer[n] = '\0';
    std::string data(buffer);
    type = data[0];
    switch (type) {
    case 'n': {
      int len = std::stoi(data.substr(1, 2)); // lee "03"
      std::string nickname = data.substr(3, len);
      std::cout << "Nickname recibido: " << nickname << "\n";
      lock_guard<mutex> lock(clientsMutex);
      m_clients[nickname] = socketConn;
      cout << "Nuevo cliente registrado con nickname: " << nickname << endl;
      break;
    }
    case 'e': {
      cout << "Error recibido: " << data << endl;
      break;
    }
    case 'm': {
      int msgLen = std::stoi(data.substr(1, 3));
      std::string msg = data.substr(4, msgLen);

      std::string senderNick;
      {
        lock_guard<mutex> lock(clientsMutex);
        for (auto &p : m_clients) {
          if (p.second == socketConn) {
            senderNick = p.first;
            break;
          }
        }
      }

      std::cout << "Broadcast de [" << senderNick << "]: " << msg << "\n";

      // 2) Armar protocolo 'M'
      char nickLenBuf[10];
      snprintf(nickLenBuf, sizeof(nickLenBuf), "%02d", (int)senderNick.size());

      char msgLenBuf[10];
      snprintf(msgLenBuf, sizeof(msgLenBuf), "%03d", (int)msg.size());

      std::string proto = "M" + std::string(nickLenBuf) + senderNick +
                          std::string(msgLenBuf) + msg;

      // 3) Enviar a todos los clientes
      lock_guard<mutex> lock(clientsMutex);
      for (auto &p : m_clients) {
        int destSock = p.second;
        if (destSock != socketConn) { // opcional: no enviar al mismo emisor
          write(destSock, proto.c_str(), proto.size());
        }
      }
      break;
    }
    case 't': {
      // 1) Extraer longitud del nickname destino
      int nickLen = std::stoi(data.substr(1, 2));
      std::string destNick = data.substr(3, nickLen);

      // 2) Extraer longitud del mensaje
      int msgLen = std::stoi(data.substr(3 + nickLen, 3));
      std::string msg = data.substr(3 + nickLen + 3, msgLen);

      // 3) Buscar el nickname del emisor recorriendo el map
      std::string senderNick;
      {
        lock_guard<mutex> lock(clientsMutex);
        for (const auto &entry : m_clients) {
          if (entry.second == socketConn) {
            senderNick = entry.first;
            break;
          }
        }
      }

      std::cout << "Mensaje privado de [" << senderNick << "] a [" << destNick
                << "]: " << msg << "\n";

      // 4) Buscar el destino
      lock_guard<mutex> lock(clientsMutex);
      auto it = m_clients.find(destNick);
      if (it != m_clients.end()) {
        int destSock = it->second;

        // 5) Armar protocolo 'T'
        char lenBuf[10];
        snprintf(lenBuf, sizeof(lenBuf), "%02d", (int)senderNick.size());
        char msgLenBuf[10];
        snprintf(msgLenBuf, sizeof(msgLenBuf), "%03d", (int)msg.size());

        std::string proto = "T" + std::string(lenBuf) + senderNick +
                            std::string(msgLenBuf) + msg;

        write(destSock, proto.c_str(), proto.size());
      } else {
        // Usuario no existe
        std::string err = "e03USR";
        write(socketConn, err.c_str(), err.size());
      }
    }
    case 'l': {
      lock_guard<mutex> lock(clientsMutex);

      // Armar respuesta
      string proto = "L"; // opcode de lista

      for (auto &p : m_clients) {
        const string &nick = p.first;
        proto += padNumber((int)nick.size(), 2) + nick;
      }

      // Enviar al cliente que pidió la lista
      write(socketConn, proto.c_str(), proto.size());
      break;
      break;
    }
    case 'x': {
      cout << "Cliente quiere salir." << endl;
      break;
    }
    default:
      cout << "Tipo de mensaje desconocido: " << data[0] << endl;
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
