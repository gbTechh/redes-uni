// Client code con soporte para historial en texto

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

    case 'M': {
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
        string lenStr;
        for (int j = 0; j < 2; ++j) {
          char c;
          if (read(socketConn, &c, 1) <= 0)
            return;
          lenStr += c;
        }
        int len = stoi(lenStr);

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

    case 'H': { // Historial en texto
        string lenHistStr;
        for (int i = 0; i < 6; ++i) {
            char c;
            if (read(socketConn, &c, 1) <= 0)
                return;
            lenHistStr += c;
        }
        int lenHist = stoi(lenHistStr);
        
        if (lenHist == 0) {
            cout << "\nNo hay historial disponible" << endl;
            break;
        }
        
        string historialData;
        for (int i = 0; i < lenHist; ++i) {
            char c;
            if (read(socketConn, &c, 1) <= 0)
                return;
            historialData += c;
        }
        
        cout << "\n=== HISTORIAL DEL CHAT ===" << endl;
        cout << historialData;
        cout << "=== FIN DEL HISTORIAL ===" << endl;
        break;
    }

    default: {
      cout << "\n[Tipo desconocido: " << type << "]" << endl;
      break;
    }
    }
  }
  cout << "Conexión cerrada por el servidor.\n";
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
  write(SocketCli, reg.c_str(), reg.size());

  thread reader(readThreadFn, SocketCli);

  int opt = 0;
  do {
    cout << "\n--- MENU PRINCIPAL ---\n";
    cout << "1. Enviar mensaje privado\n";
    cout << "2. Enviar mensaje a todos\n";
    cout << "3. Listar usuarios conectados\n";
    cout << "4. Ver historial del chat\n";
    cout << "5. Salir\n";
    cout << "Seleccione una opcion: ";
    
    if (!(cin >> opt)) {
      cin.clear();
      string dummy;
      getline(cin, dummy);
      opt = 0;
    }
    cin.ignore();

    if (opt == 1) {
      write(SocketCli, "l", 1);
      string dest, msg;
      cout << "Destinatario: ";
      getline(cin, dest);
      cout << "Mensaje: ";
      getline(cin, msg);
      string payload = "t" + padNumber((int)dest.size(), 2) + dest +
                       padNumber((int)msg.size(), 3) + msg;
      write(SocketCli, payload.c_str(), payload.size());
    } else if (opt == 2) {
      string msg;
      cout << "Mensaje para todos: ";
      getline(cin, msg);
      string payload = "m" + padNumber((int)msg.size(), 3) + msg;
      write(SocketCli, payload.c_str(), payload.size());
    } else if (opt == 3) {
      write(SocketCli, "l", 1);
    } else if (opt == 4) {
      write(SocketCli, "h", 1);
    } else if (opt == 5) {
      write(SocketCli, "x", 1);
      break;
    }

  } while (opt != 5);

  shutdown(SocketCli, SHUT_RDWR);
  close(SocketCli);
  if (reader.joinable())
    reader.join();

  cout << "Cliente finalizado.\n";
  return 0;
}