// cliente.cpp
#include <arpa/inet.h>
#include <fstream>
#include <iostream>
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

string my_id = "";
vector<vector<double>> Omega;
int n_matrix = 0, k_matrix = 0;
uint32_t seed_value = 0;
bool omega_generated = false;

vector<vector<double>> A_part;  // Parte de matriz A para este worker
vector<vector<double>> Y_local; // Resultado Y_local = A_part × Ω
vector<vector<double>> Qt_part; // Nueva: parte de Q^T
vector<vector<double>> B_local;

bool A_part_recibida = false;
bool Y_local_calculada = false;
bool Qt_part_recibida = false;
bool B_local_calculada = false;
int worker_num_asignado = -1;

void guardarArchivo(const string &filename, const string &data) {
  ofstream archivo(filename, ios::binary);
  if (archivo) {
    archivo.write(data.c_str(), data.size());
    archivo.close();
  } else {
    cout << "Error al guardar el archivo: " << filename << endl;
  }
}

string padNumber(long long num, int width) {
  string s = to_string(num);
  if ((int)s.size() < width)
    s = string(width - s.size(), '0') + s;
  return s;
}

void enviarYLocal(int socketConn) {
  if (!Y_local_calculada || Y_local.empty()) {
    cerr << "ERROR: Y_local no está lista para enviar" << endl;
    return;
  }

  cout << "\n=== ENVIANDO Y_local AL SERVIDOR ===" << endl;

  stringstream ss;
  for (const auto &fila : Y_local) {
    for (size_t j = 0; j < fila.size(); j++) {
      ss << fila[j];
      if (j < fila.size() - 1)
        ss << ",";
    }
    ss << "\n";
  }
  string y_data = ss.str();

  cout << "  Dimensiones: " << Y_local.size() << " × " << Y_local[0].size()
       << endl;
  cout << "  Tamaño datos: " << y_data.size() << " bytes ("
       << (y_data.size() / (1024.0 * 1024.0)) << " MB)" << endl;

  string proto =
      "y" + padNumber(my_id.size(), 3) + my_id + padNumber(Y_local.size(), 8) +
      padNumber(Y_local[0].size(), 8) + padNumber(y_data.size(), 10) + y_data;

  cout << "  Enviando..." << endl;

  write(socketConn, proto.c_str(), proto.size());

  cout << "  ✓ Y_local enviada exitosamente" << endl;
}
void calcularYLocal() {
  cout << "\n=== CALCULANDO Y_local = A × Omega ===" << endl;

  if (!omega_generated) {
    cerr << "ERROR: Ω no generada aún" << endl;
    return;
  }
  if (A_part.empty()) {
    cerr << "ERROR: A_part vacía" << endl;
    return;
  }

  int filas_A = A_part.size();
  int cols_A = A_part[0].size();
  int filas_Omega = Omega.size();   // Debería ser = cols_A
  int cols_Omega = Omega[0].size(); // columnas de Ω

  cout << "  A_part: " << filas_A << " × " << cols_A << endl;
  cout << "  Omega:  " << filas_Omega << " × " << cols_Omega << endl;

  if (cols_A != filas_Omega) {
    cerr << "ERROR: No se puede multiplicar: A.cols=" << cols_A
         << " pero Ω.rows=" << filas_Omega << endl;
    return;
  }

  Y_local.resize(filas_A, vector<double>(cols_Omega, 0.0));

  cout << "Multiplicando matrices..." << endl;

  for (int i = 0; i < filas_A; i++) {
    for (int j = 0; j < cols_Omega; j++) {
      double suma = 0.0;
      for (int k = 0; k < cols_A; k++) {
        suma += A_part[i][k] * Omega[k][j];
      }
      Y_local[i][j] = suma;
    }
  }

  cout << "Y_local calculada: " << Y_local.size() << " × " << Y_local[0].size()
       << endl;
  cout << "Y_local[0][0] = " << Y_local[0][0] << endl;
  cout << "Y_local[0][1] = " << Y_local[0][1] << endl;

  Y_local_calculada = true;
}

void enviarBLocal(int socketConn) {
  if (!B_local_calculada || B_local.empty()) {
    cerr << "ERROR: B_local no está lista para enviar" << endl;
    return;
  }

  cout << "\n=== ENVIANDO B_local AL SERVIDOR ===" << endl;

  stringstream ss;
  for (const auto &fila : B_local) {
    for (size_t j = 0; j < fila.size(); j++) {
      ss << fila[j];
      if (j < fila.size() - 1)
        ss << ",";
    }
    ss << "\n";
  }
  string b_data = ss.str();

  cout << "  Dimensiones: " << B_local.size() << " × " << B_local[0].size()
       << endl;
  cout << "  Tamaño: " << (b_data.size() / (1024.0 * 1024.0)) << " MB" << endl;

  string proto =
      "b" + padNumber(my_id.size(), 3) + my_id + padNumber(B_local.size(), 8) +
      padNumber(B_local[0].size(), 8) + padNumber(b_data.size(), 10) + b_data;

  write(socketConn, proto.c_str(), proto.size());

  cout << "  ✓ B_local enviada" << endl;
}

void calcularBLocal(int socketConn) {
  cout << "\n=== CALCULANDO B_local = Q^T × A_part ===" << endl;

  if (Qt_part.empty()) {
    cerr << "ERROR: Qt_part no recibida" << endl;
    return;
  }
  if (A_part.empty()) {
    cerr << "ERROR: A_part vacía" << endl;
    return;
  }

  int filas_Qt = Qt_part.size();   // k
  int cols_Qt = Qt_part[0].size(); // m/4 (parte de columnas)
  int filas_A = A_part.size();     // m/4 (mismas que cols_Qt)
  int cols_A = A_part[0].size();   // n

  cout << "  Qt_part: " << filas_Qt << " × " << cols_Qt << endl;
  cout << "  A_part:  " << filas_A << " × " << cols_A << endl;

  if (cols_Qt != filas_A) {
    cerr << "ERROR: No se puede multiplicar: Qt.cols=" << cols_Qt
         << " pero A.rows=" << filas_A << endl;
    return;
  }

  B_local.resize(filas_Qt, vector<double>(cols_A, 0.0));

  cout << "  Multiplicando matrices..." << endl;

  // B = Qt × A
  for (int i = 0; i < filas_Qt; i++) {
    for (int j = 0; j < cols_A; j++) {
      double suma = 0.0;
      for (int k = 0; k < cols_Qt; k++) {
        suma += Qt_part[i][k] * A_part[k][j];
      }
      B_local[i][j] = suma;
    }

    // Progreso
    if (filas_Qt > 10 && i % max(1, filas_Qt / 10) == 0) {
      float progreso = (float)(i + 1) / filas_Qt * 100.0f;
      cout << "    Progreso: " << (int)progreso << "%" << endl;
    }
  }

  cout << "  ✓ B_local calculada: " << B_local.size() << " × "
       << B_local[0].size() << endl;
  cout << "    B[0][0] = " << B_local[0][0] << endl;
  cout << "    B[0][1] = " << B_local[0][1] << endl;

  B_local_calculada = true;

  enviarBLocal(socketConn);
}

vector<vector<double>> parseMatrixFromFile(const string &filename) {
  cout << "Parseando matriz desde: " << filename << endl;

  vector<vector<double>> matriz;
  ifstream file(filename);

  if (!file.is_open()) {
    cerr << "ERROR: No se pudo abrir " << filename << endl;
    return matriz;
  }

  string linea;
  int fila_cont = 0;

  while (getline(file, linea)) {
    vector<double> fila;
    stringstream ss(linea);
    string valor_str;

    while (getline(ss, valor_str, ',')) {
      fila.push_back(stod(valor_str));
    }

    matriz.push_back(fila);
    fila_cont++;
  }

  file.close();

  cout << "Matriz parseada: " << matriz.size() << " x "
       << (matriz.empty() ? 0 : matriz[0].size()) << endl;

  return matriz;
}

void generarMatrizOmega() {
  cout << "Generando matriz Ω de tamaño " << n_matrix << " x " << k_matrix
       << "..." << endl;

  Omega.resize(n_matrix);
  for (int i = 0; i < n_matrix; i++) {
    Omega[i].resize(k_matrix);
  }

  mt19937 generador(seed_value);
  normal_distribution<double> distribucion(0.0, 1.0);

  // Rellenar matriz con valores aleatorios
  int contador = 0;
  for (int i = 0; i < n_matrix; i++) {
    for (int j = 0; j < k_matrix; j++) {
      Omega[i][j] = distribucion(generador);
      contador++;
    }
  }

  // Calcular memoria usada
  long long elementos = (long long)n_matrix * k_matrix;
  long long memoria_bytes = elementos * sizeof(double);
  double memoria_mb = memoria_bytes / (1024.0 * 1024.0);

  cout << "Matriz omega generada exitosamente!" << endl;
  cout << "  Elementos: " << elementos << endl;
  cout << "  Memoria usada: " << memoria_mb << " MB" << endl;
  cout << "  Primer valor [0][0] = " << Omega[0][0] << endl;
  cout << "  Último valor [" << n_matrix - 1 << "][" << k_matrix - 1
       << "] = " << Omega[n_matrix - 1][k_matrix - 1] << endl;

  omega_generated = true;
}

void readThreadFn(int socketConn) {
  cout << ">>> Thread de lectura iniciado, esperando mensajes..." << endl;
  while (true) {
    char type;
    ssize_t n = read(socketConn, &type, 1);
    if (n <= 0)
      break;

    switch (type) {
    case 'I': {
      string lenStr;
      for (int i = 0; i < 3; ++i) {
        char c;
        if (read(socketConn, &c, 1) <= 0)
          return;
        lenStr += c;
      }
      int len = stoi(lenStr);

      string received_id;
      for (int i = 0; i < len; ++i) {
        char c;
        if (read(socketConn, &c, 1) <= 0)
          return;
        received_id += c;
      }

      my_id = received_id;
      cout << "\nTu ID asignado por el servidor: " << my_id << endl;
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
      int filenameLen = stoi(sizeFilename);

      string filename;
      for (int i = 0; i < filenameLen; ++i) {
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
      fileData.resize(fileLen);
      size_t totalRead = 0;
      while (totalRead < fileLen) {
        ssize_t n = read(socketConn, &fileData[totalRead], fileLen - totalRead);
        if (n <= 0) {
          cerr << "Error leyendo datos del archivo" << endl;
          return;
        }
        totalRead += n;
      }

      guardarArchivo(filename, fileData);

      if (filename.find("A_part_") == 0) {
        cout << "\n=== MATRIZ A RECIBIDA ===" << endl;

        string archivo_guardado = filename;

        A_part = parseMatrixFromFile(archivo_guardado);
        A_part_recibida = true;

        size_t pos = filename.find("A_part_");
        if (pos != string::npos) {
          size_t start = pos + 7;
          size_t end = filename.find(".csv");
          if (end != string::npos) {
            string num_str = filename.substr(start, end - start);
            worker_num_asignado = stoi(num_str);
            cout << "Soy worker número: " << worker_num_asignado << endl;
          }
        }

        if (omega_generated) {
          cout << "Matriz omega lista, calculando Y_local..." << endl;
          calcularYLocal();

          cout << "\n=== Enviar Matriz Y ===" << endl;
          enviarYLocal(socketConn);

        } else {
          cout << "Esperando que Ω se genere..." << endl;
        }
      } else if (filename.find("Qt_part_") == 0) {
        cout << "\n=== RECIBIDA MATRIZ Q^T ===" << endl;

        Qt_part = parseMatrixFromFile(filename);
        Qt_part_recibida = true;

        // Calcular B ahora que tenemos Qt y A
        if (A_part_recibida) {
          calcularBLocal(socketConn);
        } else {
          cout << "  Esperando A_part para calcular B..." << endl;
        }

        // remove(filename.c_str());
      }
      break;
    }
    case 'S': {
      string n_str;
      for (int i = 0; i < DIGITS_N; ++i) {
        char c;
        if (read(socketConn, &c, 1) <= 0)
          return;
        n_str += c;
      }
      n_matrix = stoi(n_str);

      string k_str;
      for (int i = 0; i < DIGITS_K; ++i) {
        char c;
        if (read(socketConn, &c, 1) <= 0)
          return;
        k_str += c;
      }
      k_matrix = stoi(k_str);

      string seed_str;
      for (int i = 0; i < DIGITS_SEED; ++i) {
        char c;
        if (read(socketConn, &c, 1) <= 0)
          return;
        seed_str += c;
      }
      seed_value = stoul(seed_str);

      cout << "PROTOCOLO: " << type << "|" << n_matrix << "|" << k_matrix << "|"
           << seed_value << endl;

      cout << "\nGenerando matriz Omega" << endl;
      generarMatrizOmega();
      cout << "=== Matriz Omega Lista ===" << endl;

      string confirmacion = "o" + padNumber(my_id.size(), 3) + my_id;
      write(socketConn, confirmacion.c_str(), confirmacion.size());
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

  thread reader(readThreadFn, SocketCli);
  reader.detach();

  cout << "\n=== Cliente conectado ===" << endl;
  cout << "El servidor controlará todo el proceso." << endl;
  cout << "Escribe 'salir' y presiona Enter para desconectar:\n" << endl;

  string comando;
  while (true) {
    getline(cin, comando);
    if (comando == "salir" || comando == "exit") {
      break;
    }
  }

  string payload = "x";
  write(SocketCli, payload.c_str(), payload.size());

  shutdown(SocketCli, SHUT_RDWR);
  close(SocketCli);

  this_thread::sleep_for(chrono::milliseconds(500));

  cout << "Cliente finalizado.\n";
  return 0;
}