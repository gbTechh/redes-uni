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
bool A_part_recibida = false;   // Bandera: ¿ya recibimos A?
bool Y_local_calculada = false; // Bandera: ¿ya calculamos Y?
int worker_num_asignado = -1;

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

string padNumber(long long num, int width) {
  string s = to_string(num);
  if ((int)s.size() < width)
    s = string(width - s.size(), '0') + s;
  return s;
}

void calcularYLocal() {
  cout << "\n=== CALCULANDO Y_local = A × Ω ===" << endl;

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

  cout << "A: " << filas_A << " × " << cols_A << endl;
  cout << "Ω: " << filas_Omega << " × " << cols_Omega << endl;

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

    if (filas_A > 10 && i % (filas_A / 10) == 0) {
      float porcentaje = (float)i / filas_A * 100.0f;
      cout << "  Progreso: " << (int)porcentaje << "%" << endl;
    }
  }

  cout << "Y_local calculada: " << Y_local.size() << " × " << Y_local[0].size()
       << endl;
  cout << "Y_local[0][0] = " << Y_local[0][0] << endl;
  cout << "Y_local[0][1] = " << Y_local[0][1] << endl;

  Y_local_calculada = true;

  cout << "Listo para enviar Y_local al servidor" << endl;
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

    // Mostrar progreso cada 1000 filas
    if (fila_cont % 1000 == 0) {
      cout << "  Filas leídas: " << fila_cont << endl;
    }
  }

  file.close();

  cout << "Matriz parseada: " << matriz.size() << " x "
       << (matriz.empty() ? 0 : matriz[0].size()) << endl;

  // Mostrar algunos valores para debug
  if (!matriz.empty()) {
    cout << "  A[0][0] = " << matriz[0][0] << endl;
    cout << "  A[0][1] = " << matriz[0][1] << endl;
  }

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

    // Mostrar progreso cada 10% de las filas
    if (n_matrix > 10 && i % (n_matrix / 10) == 0) {
      float porcentaje = (float)i / n_matrix * 100.0f;
      cout << "  Progreso: " << (int)porcentaje << "%" << endl;
    }
  }

  // Calcular memoria usada
  long long elementos = (long long)n_matrix * k_matrix;
  long long memoria_bytes = elementos * sizeof(double);
  double memoria_mb = memoria_bytes / (1024.0 * 1024.0);

  cout << "Ω generada exitosamente!" << endl;
  cout << "  Elementos: " << elementos << endl;
  cout << "  Memoria usada: " << memoria_mb << " MB" << endl;
  cout << "  Primer valor Ω[0][0] = " << Omega[0][0] << endl;
  cout << "  Último valor Ω[" << n_matrix - 1 << "][" << k_matrix - 1
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

        string archivo_guardado = filename + "-destino";

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
          cout << "Ω lista, calculando Y_local..." << endl;
          calcularYLocal();
        } else {
          cout << "Esperando que Ω se genere..." << endl;
        }
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

      cout << "Parámetros recibidos:" << endl;
      cout << "  n (columnas de A) = " << n_matrix << endl;
      cout << "  k (columnas de Ω) = " << k_matrix << endl;
      cout << "  seed = " << seed_value << endl;

      cout << "\nGenerando matriz Ω en RAM..." << endl;
      generarMatrizOmega();
      cout << "=== Ω LISTA PARA USO ===" << endl;

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