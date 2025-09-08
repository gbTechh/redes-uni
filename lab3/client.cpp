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
    char buffer[1024];
    int n = read(socketConn, buffer, sizeof(buffer) - 1);
    if (n <= 0)
      break; // servidor cerró o error
    buffer[n] = '\0';

    // 📌 Parsear según opcode
    char opcode = buffer[0];

    if (opcode == 'T') {
      // Mensaje privado
      string lenNickStr(buffer + 1, 2);
      int lenNick = stoi(lenNickStr);
      string sender(buffer + 3, lenNick);

      string lenMsgStr(buffer + 3 + lenNick, 3);
      int lenMsg = stoi(lenMsgStr);
      string msg(buffer + 6 + lenNick, lenMsg);

      cout << "\n[Privado de " << sender << "] " << msg << endl;
    } else if (opcode == 'M') {
      // Mensaje broadcast
      string lenNickStr(buffer + 1, 2);
      int lenNick = stoi(lenNickStr);
      string sender(buffer + 3, lenNick);

      string lenMsgStr(buffer + 3 + lenNick, 3);
      int lenMsg = stoi(lenMsgStr);
      string msg(buffer + 6 + lenNick, lenMsg);

      cout << "\n[Broadcast de " << sender << "] " << msg << endl;
    } else if (opcode == 'E') {
      // Error
      cout << "\n[Error del servidor] " << (buffer + 1) << endl;
    } else if (opcode == 'L') {
      int pos = 1;
      cout << "\n[Usuarios conectados] ";
      while (pos < n) {
        string lenStr(buffer + pos, 2);
        int len = stoi(lenStr);
        pos += 2;

        string nick(buffer + pos, len);
        pos += len;

        cout << nick << " ";
      }
      cout << endl;
    } else {
      // Mensaje desconocido (debug)
      cout << "\n[Server raw] " << buffer << endl;
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

  // 1) Pedir nickname e enviarlo con protocolo nXXnick (XX = 2 dígitos)
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

  // 2) Hilo para recibir mensajes del servidor
  thread reader(readThreadFn, SocketCli);

  // 3) Menu simple que arma los protocolos y los envia al servidor
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
      string dest, msg;
      cout << "Ingrese el nickname del destinatario: ";
      getline(cin, dest);
      cout << "Escriba el mensaje: ";
      getline(cin, msg);
      string payload = "t" + padNumber((int)dest.size(), 2) + dest +
                       padNumber((int)msg.size(), 3) + msg;

      write(SocketCli, payload.c_str(), payload.size());
    } else if (opt == 2) {
      string msg;
      cout << "Escriba el mensaje para todos: ";
      getline(cin, msg);
      string payload = "m" + padNumber((int)msg.size(), 3) + msg;
      write(SocketCli, payload.c_str(), payload.size());
    } else if (opt == 3) {
      string payload = "l";
      write(SocketCli, payload.c_str(), payload.size());
    } else if (opt == 4) {
      // mandar 'x' para indicar salida al servidor
      string payload = "x";
      write(SocketCli, payload.c_str(), payload.size());
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
