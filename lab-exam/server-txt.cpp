/* Server code in C con historial en texto */

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
#include <fstream>
#include <ctime>

using namespace std;

map<string, int> m_clients;
mutex clientsMutex;

string padNumber(int num, int width) {
  string s = to_string(num);
  if ((int)s.size() < width)
    s = string(width - s.size(), '0') + s;
  return s;
}

// Función para guardar mensajes en archivo de TEXTO
void guardarMensajeTexto(const string& tipo, const string& remitente, 
                        const string& destinatario, const string& mensaje) {
    ofstream archivo("historial_chat.txt", ios::app); // Modo texto (sin binary)
    
    if (archivo) {
        time_t ahora = time(nullptr);
        char buffer[80];
        strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", localtime(&ahora));
        
        if (tipo == "M") {
            archivo << "[BROADCAST][" << buffer << "][" << remitente << "] " << mensaje << "\n";
        } else {
            archivo << "[PRIVADO][" << buffer << "][" << remitente << "->" << destinatario << "] " << mensaje << "\n";
        }
    }
}

// Función para leer el historial desde el servidor (TEXTO)
void leerHistorialServidor() {
    ifstream archivo("historial_chat.txt");
    
    if (!archivo) {
        cout << "No hay historial disponible" << endl;
        return;
    }
    
    cout << "\n=== HISTORIAL DEL CHAT (SERVER) ===" << endl;
    
    string linea;
    while (getline(archivo, linea)) {
        cout << linea << endl;
    }
    cout << "=== FIN DEL HISTORIAL ===" << endl;
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
        m_clients[nickname] = socketConn;
        cout << "Nuevo cliente registrado: " << nickname << endl;
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

      cout << "Broadcast de [" << senderNick << "]: " << msg << endl;

      // GUARDAR EN HISTORIAL (TEXTO)
      guardarMensajeTexto("M", senderNick, "Todos", msg);

      lock_guard<mutex> lock(clientsMutex);
      string proto = "M" + padNumber(senderNick.size(), 2) + senderNick +
                     padNumber(msg.size(), 3) + msg;
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

      cout << "Mensaje privado de [" << senderNick << "] a [" << destNick << "]: " << msg << endl;

      // GUARDAR EN HISTORIAL (TEXTO)
      guardarMensajeTexto("T", senderNick, destNick, msg);

      lock_guard<mutex> lock(clientsMutex);
      auto it = m_clients.find(destNick);
      if (it != m_clients.end()) {
        int destSock = it->second;
        string proto = "T" + padNumber(senderNick.size(), 2) + senderNick +
                       padNumber(msg.size(), 3) + msg;
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
      write(socketConn, proto.c_str(), proto.size());
      break;
    }
    case 'h': { // 'h' para historial (TEXTO)
        cout << "Cliente solicitó historial del chat" << endl;
        
        ifstream archivo("historial_chat.txt");
        string historialCompleto;
        string linea;
        
        while (getline(archivo, linea)) {
            historialCompleto += linea + "\n";
        }
        
        if (historialCompleto.empty()) {
            string respuesta = "HNo hay historial disponible";
            write(socketConn, respuesta.c_str(), respuesta.size());
        } else {
            string respuesta = "H" + padNumber(historialCompleto.size(), 6) + historialCompleto;
            write(socketConn, respuesta.c_str(), respuesta.size());
        }
        break;
    }
    case 'x': {
      string nick;
      {
        lock_guard<mutex> lock(clientsMutex);
        for (auto it = m_clients.begin(); it != m_clients.end(); ++it) {
          if (it->second == socketConn) {
            nick = it->first;
            m_clients.erase(it);
            break;
          }
        }
      }
      close(socketConn);
      cout << "Cliente " << nick << " desconectado." << endl;
      break;
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

  cout << "Servidor iniciado en puerto 45000 (Modo TEXTO)" << endl;
  cout << "Comandos del servidor:" << endl;
  cout << "  - 'historial': Ver historial del chat" << endl;
  cout << "  - 'quit': Salir del servidor" << endl;

  thread serverThread([]() {
      string comando;
      while (true) {
          cout << "\nServidor> ";
          getline(cin, comando);
          
          if (comando == "historial") {
              leerHistorialServidor();
          } else if (comando == "quit") {
              cout << "Apagando servidor..." << endl;
              exit(0);
          }
      }
  });
  serverThread.detach();

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