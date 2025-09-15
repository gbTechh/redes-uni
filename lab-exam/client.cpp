// client_simple.cpp
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

using namespace std;

string padNumber(int num, int width) {
  string s = to_string(num);
  if ((int)s.size() < width)
    s = string(width - s.size(), '0') + s;
  return s;
}

void readThreadFn(int socketConn) {
  while (true) {
    char type;
    ssize_t n = read(socketConn, &type, 1);
    if (n <= 0)
      break;

    switch (type) {
    case 'T': {
      string lenNickStr;
      for (int i = 0; i < 2; ++i) {
        char c;
        if (read(socketConn, &c, 1) <= 0)
          return;
        lenNickStr += c;
      }
      int lenNick = stoi(lenNickStr);

      string sender;
      for (int i = 0; i < lenNick; ++i) {
        char c;
        if (read(socketConn, &c, 1) <= 0)
          return;
        sender += c;
      }

      string lenMsgStr;
      for (int i = 0; i < 3; ++i) {
        char c;
        if (read(socketConn, &c, 1) <= 0)
          return;
        lenMsgStr += c;
      }
      int lenMsg = stoi(lenMsgStr);

      string msg;
      for (int i = 0; i < lenMsg; ++i) {
        char c;
        if (read(socketConn, &c, 1) <= 0)
          return;
        msg += c;
      }

      cout << "\n[Privado de " << sender << "] " << msg << endl;
      break;
    }

    case 'M': { // Broadcast
      string lenNickStr;
      for (int i = 0; i < 2; ++i) {
        char c;
        if (read(socketConn, &c, 1) <= 0)
          return;
        lenNickStr += c;
      }
      int lenNick = stoi(lenNickStr);

      string sender;
      for (int i = 0; i < lenNick; ++i) {
        char c;
        if (read(socketConn, &c, 1) <= 0)
          return;
        sender += c;
      }

      string lenMsgStr;
      for (int i = 0; i < 3; ++i) {
        char c;
        if (read(socketConn, &c, 1) <= 0)
          return;
        lenMsgStr += c;
      }
      int lenMsg = stoi(lenMsgStr);

      string msg;
      for (int i = 0; i < lenMsg; ++i) {
        char c;
        if (read(socketConn, &c, 1) <= 0)
          return;
        msg += c;
      }

      cout << "\n[Broadcast de " << sender << "] " << msg << endl;
      break;
    }

    case 'E': {
      string errMsg;
      for (int i = 0; i < 15; ++i) {
        char c;
        if (read(socketConn, &c, 1) <= 0)
          return;
        errMsg += c;
      }
      cout << "\n[Error del servidor] " << errMsg << endl;
      break;
    }

    case 'L': {
      // cantidad de usuarios
      string countStr;
      for (int i = 0; i < 2; ++i) {
        char c;
        if (read(socketConn, &c, 1) <= 0)
          return;
        countStr += c;
      }
      int numUsers = stoi(countStr);

      cout << "\n[Usuarios conectados] ";
      for (int i = 0; i < numUsers; ++i) {
        // Leer longitud del nickname
        string lenStr;
        for (int j = 0; j < 2; ++j) {
          char c;
          if (read(socketConn, &c, 1) <= 0)
            return;
          lenStr += c;
        }
        int len = stoi(lenStr);

        // Leer nickname
        string nick;
        for (int j = 0; j < len; ++j) {
          char c;
          if (read(socketConn, &c, 1) <= 0)
            return;
          nick += c;
        }
        cout << nick << " ";
      }
      cout << endl;
      break;
    }

    default: {
      cout << "\n[Server raw] Tipo desconocido: " << type << endl;
      break;
    }
    }
  }
  cout << "Lectura: conexión cerrada por el servidor (o error).\n";
}
int main() {
  int port = 45000;
  const char *serverIP = "127.0.0.1";

  int SocketCli = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (SocketCli < 0) {
    perror("cannot create socket");
    return 1;
  }

  struct sockaddr_in stSockAddr;
  memset(&stSockAddr, 0, sizeof(stSockAddr));
  stSockAddr.sin_family = AF_INET;
  stSockAddr.sin_port = htons(port);
  if (inet_pton(AF_INET, serverIP, &stSockAddr.sin_addr) <= 0) {
    perror("inet_pton failed");
    close(SocketCli);
    return 1;
  }

  if (connect(SocketCli, (const struct sockaddr *)&stSockAddr,
              sizeof(stSockAddr)) < 0) {
    perror("connect failed");
    close(SocketCli);
    return 1;
  }

  string nickname;
  cout << "Escriba su nickname: ";
  getline(cin, nickname);
  if (nickname.empty())
    nickname = "user";

  string reg = "n" + padNumber((int)nickname.size(), 2) + nickname;
  ssize_t w = write(SocketCli, reg.c_str(), reg.size());
  if (w <= 0) {
    perror("write nickname failed");
    close(SocketCli);
    return 1;
  }
  cout << "[" << reg << "]\n";

  thread reader(readThreadFn, SocketCli);

  int opt = 0;
  do {
    cout << "\n--- MENU PRINCIPAL ---\n";
    cout << "1. Enviar mensaje a un cliente (privado)\n";
    cout << "2. Enviar mensaje a todos los clientes (broadcast)\n";
    cout << "3. Listar todos los clientes conectados\n";
    cout << "4. Salir (Cerrar conexion)\n";
    cout << "Seleccione una opcion: ";
    if (!(cin >> opt)) {
      cin.clear();
      string dummy;
      getline(cin, dummy);
      opt = 0;
    }
    cin.ignore(); // limpiar \n restante

    if (opt == 1) {
      string payloadList = "l";
      write(SocketCli, payloadList.c_str(), payloadList.size());
      cout << "[" << payloadList << "]\n";
      string dest, msg;
      cout << "Ingrese el nickname del destinatario: ";
      getline(cin, dest);
      cout << "Escriba el mensaje: ";
      getline(cin, msg);
      string payload = "t" + padNumber((int)dest.size(), 2) + dest +
                       padNumber((int)msg.size(), 3) + msg;
      cout << "[" << payload << "]\n";
      write(SocketCli, payload.c_str(), payload.size());
    } else if (opt == 2) {
      string msg;
      cout << "Escriba el mensaje para todos: ";
      getline(cin, msg);
      string payload = "m" + padNumber((int)msg.size(), 3) + msg;
      cout << "[" << payload << "]\n";
      write(SocketCli, payload.c_str(), payload.size());
    } else if (opt == 3) {
      string payload = "l";
      write(SocketCli, payload.c_str(), payload.size());
      cout << "[" << payload << "]\n";
    } else if (opt == 4) {
      // mandar 'x' para indicar salida al servidor
      string payload = "x";
      write(SocketCli, payload.c_str(), payload.size());
      cout << "[" << payload << "]\n";
      break;
    } else {
      cout << "Opcion no valida\n";
    }

  } while (opt != 4);

  // cerrar y esperar hilo lector
  shutdown(SocketCli, SHUT_RDWR);
  close(SocketCli);
  if (reader.joinable())
    reader.join();

  cout << "Cliente finalizado.\n";
  return 0;
}
