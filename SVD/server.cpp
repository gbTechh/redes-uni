/* Server code in C */

#include <arpa/inet.h>
#include <atomic>
#include <fstream>
#include <iostream>
#include <map>
#include <mutex>
#include <netinet/in.h>
#include <random>
#include <sstream>
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

const int DIGITS_N = 8;
const int DIGITS_K = 6;
const int DIGITS_SEED = 10;
const int DIGITS_WORKER_ID = 3;
const int DIGITS_ROWS_COLS = 8;
const int DIGITS_DATA_SIZE = 15;

map<string, int> m_clients;
mutex clientsMutex;
map<int, string> socket_to_id;
int next_id = 1;
const int MAX_WORKERS = 4;

atomic<bool> computation_started(false);
int target_rank = 1000;
int oversampling = 10;
int omega_columns = 0;
uint32_t global_seed = 0;
string archivo_matriz = "matrix.txt";

vector<vector<double>> matriz_global;
bool matriz_cargada = false;

string padNumber(long long num, int width) {
  string s = to_string(num);
  if ((int)s.size() < width)
    s = string(width - s.size(), '0') + s;
  return s;
}
string getClientId(int sock) {
  lock_guard<mutex> lock(clientsMutex);
  auto it = socket_to_id.find(sock);
  return (it != socket_to_id.end()) ? it->second : "unknown";
}

pair<int, int> getMatrixDimensions(const string &filename) {
  ifstream file(filename);
  if (!file.is_open()) {
    cerr << "Error abriendo " << filename << endl;
    return {0, 0};
  }

  string first_line;
  if (!getline(file, first_line)) {
    return {0, 0};
  }

  stringstream ss(first_line);
  string cell;
  int cols = 0;

  while (getline(ss, cell, ',')) {
    cols++;
  }

  int rows = 1;
  while (getline(file, first_line)) {
    rows++;
  }

  file.close();
  return {rows, cols};
}

vector<vector<double>> readMatrixPart(const string &filename, int start_row,
                                      int end_row) {
  ifstream file(filename);
  vector<vector<double>> matrix_part;

  string line;
  int current_row = 0;

  while (getline(file, line) && current_row < end_row) {
    if (current_row >= start_row) {
      vector<double> row;
      stringstream ss(line);
      string cell;

      while (getline(ss, cell, ',')) {
        row.push_back(stod(cell));
      }
      matrix_part.push_back(row);
    }
    current_row++;
  }

  file.close();
  return matrix_part;
}

string leerArchivoBinario(const string &filename) {
  ifstream archivo(filename, ios::binary | ios::ate);
  if (!archivo) {
    cout << "Error: No se pudo abrir '" << filename << "'" << endl;
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

string crearArchivoParteA(int worker_num,
                          const vector<vector<double>> &A_part) {
  string nombre_archivo = "A_part_worker" + to_string(worker_num) + ".csv";

  ofstream archivo(nombre_archivo);
  if (!archivo.is_open()) {
    cerr << "Error creando archivo " << nombre_archivo << endl;
    return "";
  }

  for (const auto &fila : A_part) {
    for (size_t j = 0; j < fila.size(); j++) {
      archivo << fila[j];
      if (j < fila.size() - 1)
        archivo << ",";
    }
    archivo << "\n";
  }

  archivo.close();
  cout << "  Archivo creado: " << nombre_archivo << " (" << A_part.size() << "x"
       << A_part[0].size() << ")" << endl;

  return nombre_archivo;
}

void cargarMatrizGlobal() {
  if (matriz_cargada)
    return;

  ifstream file(archivo_matriz);
  string linea;

  while (getline(file, linea)) {
    vector<double> fila;
    stringstream ss(linea);
    string valor;

    while (getline(ss, valor, ',')) {
      fila.push_back(stod(valor));
    }
    matriz_global.push_back(fila);
  }

  matriz_cargada = true;
  cout << "Matriz global cargada: " << matriz_global.size() << " x "
       << matriz_global[0].size() << endl
       << flush;
}

void enviarArchivoAWorker(int sock, const string &filename, int worker_num) {
  string fileData = leerArchivoBinario(filename);
  if (fileData.empty()) {
    cerr << "Error: Archivo vacío " << filename << endl;
    return;
  }

  string nombre_destino = "A_part_" + to_string(worker_num) + ".csv";

  string worker_id = "worker" + padNumber(worker_num + 1, 3);

  string proto = "F" + padNumber(worker_id.size(), 2) + worker_id +
                 padNumber(nombre_destino.size(), 3) + nombre_destino +
                 padNumber(fileData.size(), 10) + fileData;

  write(sock, proto.c_str(), proto.size());

  cout << "  ✓ Archivo enviado: " << filename << " -> " << worker_id << " ("
       << fileData.size() << " bytes)" << endl;

  remove(filename.c_str());
  cout << "  Archivo temporal eliminado: " << filename << endl;
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
    case 'o': { // Confirmación de worker matriz omega
      string lenStr;
      for (int i = 0; i < 3; ++i) {
        char c;
        if (read(socketConn, &c, 1) <= 0)
          return;
        lenStr += c;
      }
      int len = stoi(lenStr);

      string worker_id;
      for (int i = 0; i < len; ++i) {
        char c;
        if (read(socketConn, &c, 1) <= 0)
          return;
        worker_id += c;
      }

      cout << "Worker " << worker_id << " confirmó que Ω está lista" << endl;

      int worker_num = 0;
      if (worker_id.find("worker") == 0) {
        string num_str = worker_id.substr(6); // "001"
        worker_num = stoi(num_str) - 1;       // 0-based
      }

      // calcular filas para el worker
      int total_workers = MAX_WORKERS;
      int filas_totales = matriz_global.size();
      int columnas_totales = matriz_global[0].size();
      int filas_por_worker = filas_totales / total_workers;

      int inicio = worker_num * filas_por_worker;
      int fin = (worker_num == total_workers - 1)
                    ? filas_totales
                    : (worker_num + 1) * filas_por_worker;

      vector<vector<double>> A_part;
      for (int i = inicio; i < fin; i++) {
        A_part.push_back(matriz_global[i]);
      }

      string nombre_archivo = crearArchivoParteA(worker_num, A_part);
      if (nombre_archivo.empty()) {
        cerr << "  Error creando archivo para worker " << worker_num << endl;
        break;
      }

      enviarArchivoAWorker(socketConn, nombre_archivo, worker_num);
      break;
    }
    case 'f': {
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

      lock_guard<mutex> lock(clientsMutex);
      auto it = m_clients.find(destNick);
      if (it != m_clients.end()) {
        int destSock = it->second;
        string proto = "F" + padNumber(destNick.size(), 2) + destNick +
                       padNumber(filename.size(), 3) + filename +
                       padNumber(fileData.size(), 10) + fileData;
        cout << "[" << proto << "]\n";
        write(destSock, proto.c_str(), proto.size());
      } else {
        string err = "e03ERR";
        write(socketConn, err.c_str(), err.size());
      }
      break;
    }
    case 'x': {
      string client_id = getClientId(socketConn);
      {
        lock_guard<mutex> lock(clientsMutex);
        m_clients.erase(client_id);
        socket_to_id.erase(socketConn);
      }
      close(socketConn);
      cout << "Cliente desconectado: " << client_id << endl;
      break;
    }
    default:
      cout << "Tipo de mensaje desconocido: " << type << endl;
      break;
    }

  } while (true);

  close(socketConn);
}

void broadcastOmegaSeed() {
  cout << "=== INICIANDO BROADCAST ===" << endl;
  cout << "computation_started: " << computation_started << endl;
  cout << "Workers conectados: " << m_clients.size() << endl;
  if (computation_started) {
    cout << "Broadcast ya iniciado." << endl;
    return;
  }

  auto [m, n] = getMatrixDimensions(archivo_matriz);
  if (m == 0 || n == 0)
    return;
  omega_columns = target_rank + oversampling;

  random_device rd;
  global_seed = rd();
  cout << "protocolo broacast\n";
  string proto = "S" + padNumber(n, DIGITS_N) +
                 padNumber(omega_columns, DIGITS_K) +
                 padNumber(global_seed, DIGITS_SEED);

  for (auto &[client_id, sock] : m_clients) {
    write(sock, proto.c_str(), proto.size());
    cout << "  Enviado a " << client_id << endl;
  }
  computation_started = true;
}

int main(int argc, char *argv[]) {

  for (int i = 1; i < argc; i++) {
    string arg = argv[i];

    if (arg == "-h" || arg == "--help") {
      cout << "Uso: ./server [k] [p] [archivo_matriz]\n";
      cout << "Ejemplos:\n";
      cout << "  ./server.exe        → k 45000, p 100, archivo matrix.txt\n";
      return 0;
    } else if (i == 1 && isdigit(arg[0])) {
      target_rank = atoi(argv[i]);
    } else if (i == 2 && isdigit(arg[0])) {
      oversampling = atoi(argv[i]);
    } else {
      archivo_matriz = argv[i];
    }
  }

  struct sockaddr_in stSockAddr;
  int SocketServer = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
  char buffer[256];
  int n;
  string buf;

  if (-1 == SocketServer) {
    perror("can not create socket");
    exit(EXIT_FAILURE);
  }

  // Permitir reutilizar el puerto inmediatamente tras cerrar el servidor
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
    {
      lock_guard<mutex> lock(clientsMutex);
      if (m_clients.size() >= MAX_WORKERS) {
        string reject = "R004Full";
        write(ConnectFD, reject.c_str(), reject.size());
        close(ConnectFD);
        cout << "Conexion rechazada: ya hay " << MAX_WORKERS
             << " workers conectados.\n";
        continue;
      }

      string client_id = "worker" + padNumber(next_id++, 3);
      m_clients[client_id] = ConnectFD;
      socket_to_id[ConnectFD] = client_id;

      string welcome = "I" + padNumber(client_id.size(), 3) + client_id;
      write(ConnectFD, welcome.c_str(), welcome.size());

      cout << "Worker conectado: " << client_id
           << " (total: " << m_clients.size() << "/" << MAX_WORKERS << ")\n";
    }

    thread clientThread(readThread, ConnectFD);
    clientThread.detach();
    {
      lock_guard<mutex> lock(clientsMutex);
      if (m_clients.size() == MAX_WORKERS && !computation_started.load()) {
        cout << "¡4 workers! Cargando matriz..." << endl << flush;
        cargarMatrizGlobal();
        cout << ">>> Matriz cargada, llamando a broadcastOmegaSeed()..." << endl
             << flush;
        broadcastOmegaSeed();
        cout << ">>> broadcastOmegaSeed() completado" << endl << flush;
      }
    }
  }

  close(SocketServer);
  return 0;
}