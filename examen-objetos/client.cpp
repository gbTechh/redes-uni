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

using namespace std;

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
    case 'F': {
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

      string sizeFilename;
      for (int i = 0; i < 3; ++i) {
        char c;
        if (read(socketConn, &c, 1) <= 0)
          return;
        sizeFilename += c;
      }
      int msgLen = stoi(sizeFilename);

      string filename;
      for (int i = 0; i < msgLen; ++i) {
        char c;
        if (read(socketConn, &c, 1) <= 0)
          return;
        filename += c;
      }

      string sizeFileData;
      for (int i = 0; i < 10; ++i) {
        char c;
        if (read(socketConn, &c, 1) <= 0)
          return;
        sizeFileData += c;
      }
      long fileLen = stoi(sizeFileData);

      string fileData;
      for (int i = 0; i < fileLen; ++i) {
        char c;
        if (read(socketConn, &c, 1) <= 0)
          return;
        fileData += c;
      }

      guardarArchivo(filename, fileData);
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
      cout << "\n[Server err] Tipo desconocido: " << type << endl;
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
    cout << "4. Enviar un archivo\n";
    cout << "5. Salir (Cerrar conexion)\n";
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
      string dest, filename;
      cout << "Ingrese el nickname del destinatario: ";
      getline(cin, dest);
      cout << "Escriba el nombre del archivo: ";
      getline(cin, filename);

      // leer el archivo
      string fileData = leerArchivo(filename);
      string payload = "f" + padNumber((int)dest.size(), 2) + dest +
                       padNumber((int)filename.size(), 3) + filename +
                       padNumber((int)fileData.size(), 10) + fileData;

      cout << "PAYLOAD CLIENTE: " << payload << "\n";
      write(SocketCli, payload.c_str(), payload.size());
    } else if (opt == 5) {
      // mandar 'x' para indicar salida al servidor
      string payload = "x";
      write(SocketCli, payload.c_str(), payload.size());
      cout << "[" << payload << "]\n";
      break;
    } else {
      cout << "Opcion no valida\n";
    }

  } while (opt != 5);

  // cerrar y esperar hilo lector
  shutdown(SocketCli, SHUT_RDWR);
  close(SocketCli);
  if (reader.joinable())
    reader.join();

  cout << "Cliente finalizado.\n";
  return 0;
}