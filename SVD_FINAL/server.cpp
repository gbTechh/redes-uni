/* server.cpp */

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

map<string, vector<vector<double>>> Y_parts;
vector<vector<double>> Y_completa;
vector<vector<double>> Q;
vector<vector<double>> Qt;
mutex Y_mutex;

map<string, vector<vector<double>>> Q_parts;
mutex Q_mutex;

map<int, pair<int, int>> A_row_ranges;
mutex A_ranges_mutex;

map<string, vector<vector<double>>> B_parts;
vector<vector<double>> B_completa;
mutex B_mutex;

map<string, vector<vector<double>>> BtB_parts;
vector<vector<double>> BtB_completa;
vector<vector<double>> V_svd;
vector<double> S_svd;
vector<vector<double>> U_completa;
mutex BtB_mutex;
mutex U_mutex;
map<string, vector<vector<double>>> U_parts;

void distribuirBParaSVD();
string crearArchivoBParte(int worker_num, const vector<vector<double>> &B_part);
void enviarBParteAWorker(int sock, const string &filename, int worker_num);
string crearArchivoBtB();
void enviarBtBAWorker(int sock, const string &filename,
                      const string &nombre_destino, int worker_num);
string crearArchivoV();
string crearArchivoS();
void enviarVySAWorker(int sock, const string &filename,
                      const string &nombre_destino, int worker_num);
void distribuirBtBParaSVD();
void distribuirQtYCalcularB();

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

  // F | tam_id(2 bytes) | worker_id | tam_n_archivo(3 bytes) | nombre_archivo |
  // tam_payload(10 bytes) | payload
  string proto = "F" + padNumber(worker_id.size(), 2) + worker_id +
                 padNumber(nombre_destino.size(), 3) + nombre_destino +
                 padNumber(fileData.size(), 10) + fileData;

  write(sock, proto.c_str(), proto.size());

  cout << "  Archivo enviado: " << filename << " -> " << worker_id << " ("
       << fileData.size() << " bytes)" << endl;
}

string crearArchivoQtParte(int worker_num,
                           const vector<vector<double>> &Qt_part) {
  string nombre_archivo = "Qt_part_worker" + to_string(worker_num) + ".csv";

  ofstream archivo(nombre_archivo);
  if (!archivo.is_open()) {
    cerr << "Error creando archivo " << nombre_archivo << endl;
    return "";
  }

  for (const auto &fila : Qt_part) {
    for (size_t j = 0; j < fila.size(); j++) {
      archivo << fila[j];
      if (j < fila.size() - 1)
        archivo << ",";
    }
    archivo << "\n";
  }

  archivo.close();
  return nombre_archivo;
}

void enviarQtAWorker(int sock, const string &filename, int worker_num) {
  string fileData = leerArchivoBinario(filename);
  if (fileData.empty()) {
    cerr << "Error: Archivo vacío " << filename << endl;
    return;
  }

  string nombre_destino = "Qt_part_" + to_string(worker_num) + ".csv";
  string worker_id = "worker" + padNumber(worker_num + 1, 3);

  string proto = "F" + padNumber(worker_id.size(), 2) + worker_id +
                 padNumber(nombre_destino.size(), 3) + nombre_destino +
                 padNumber(fileData.size(), 10) + fileData;

  write(sock, proto.c_str(), proto.size());

  cout << "  Q^T enviada: " << filename << " -> " << worker_id << " ("
       << fileData.size() << " bytes)" << endl;
}

string crearArchivoYCompleta() {
  string nombre_archivo = "Y_completa_temp.csv";

  ofstream archivo(nombre_archivo);
  if (!archivo.is_open()) {
    cerr << "Error creando archivo " << nombre_archivo << endl;
    return "";
  }

  for (const auto &fila : Y_completa) {
    for (size_t j = 0; j < fila.size(); j++) {
      archivo << fila[j];
      if (j < fila.size() - 1)
        archivo << ",";
    }
    archivo << "\n";
  }

  archivo.close();

  // Asegurar que el archivo esté completamente escrito antes de leerlo
  ifstream verificar(nombre_archivo);
  if (!verificar.is_open()) {
    cerr << "ERROR: No se pudo verificar archivo " << nombre_archivo << endl;
    return "";
  }
  verificar.close();

  return nombre_archivo;
}

void enviarYAWorker(int sock, const string &filename, int worker_num) {
  usleep(10000);

  string fileData = leerArchivoBinario(filename);
  if (fileData.empty()) {
    cerr << "Error: Archivo vacío " << filename << endl;
    return;
  }

  if (fileData.size() < 10) {
    cerr << "Error: Archivo demasiado pequeño " << filename << " ("
         << fileData.size() << " bytes)" << endl;
    return;
  }

  string nombre_destino = "Y_completa.csv";
  string worker_id = "worker" + padNumber(worker_num + 1, 3);

  string proto = "F" + padNumber(worker_id.size(), 2) + worker_id +
                 padNumber(nombre_destino.size(), 3) + nombre_destino +
                 padNumber(fileData.size(), 10) + fileData;

  write(sock, proto.c_str(), proto.size());

  cout << "  Y completa enviada: " << filename << " -> " << worker_id << " ("
       << fileData.size() << " bytes)" << endl;
}

void distribuirQtYCalcularB() {
  cout << "\n=== DISTRIBUYENDO Q^T A WORKERS ===" << endl;

  int k = Qt.size();
  int m = Qt[0].size();

  cout << "  Q^T: " << k << " × " << m << endl;

  lock_guard<mutex> lock(clientsMutex);
  lock_guard<mutex> lock_ranges(A_ranges_mutex);

  for (int worker_num = 0; worker_num < MAX_WORKERS; worker_num++) {
    string worker_id = "worker" + padNumber(worker_num + 1, 3);
    auto it = m_clients.find(worker_id);

    if (it == m_clients.end()) {
      cerr << "ERROR: Worker " << worker_id << " no encontrado" << endl;
      continue;
    }

    // Obtener el rango de filas que recibió este worker para A
    auto range_it = A_row_ranges.find(worker_num);
    if (range_it == A_row_ranges.end()) {
      cerr << "ERROR: No se encontró rango de filas para worker " << worker_num
           << endl;
      continue;
    }

    int col_inicio =
        range_it->second
            .first; // inicio de filas de A = inicio de columnas de Qt
    int col_fin =
        range_it->second.second; // fin de filas de A = fin de columnas de Qt

    int sock = it->second;

    // Extraer Qt_part: todas las filas, pero solo columna que corresponden
    // exactamente a las filas que recibió este worker en A
    vector<vector<double>> Qt_part;
    for (int i = 0; i < k; i++) {
      vector<double> fila_parte;
      for (int j = col_inicio; j < col_fin; j++) {
        fila_parte.push_back(Qt[i][j]);
      }
      Qt_part.push_back(fila_parte);
    }

    cout << "  Worker " << worker_num << ": Qt_part = " << Qt_part.size()
         << " × " << Qt_part[0].size() << " (columnas " << col_inicio << "-"
         << (col_fin - 1) << ")" << endl;

    string nombre_archivo = crearArchivoQtParte(worker_num, Qt_part);
    if (nombre_archivo.empty()) {
      cerr << "  Error creando archivo Qt para worker " << worker_num << endl;
      continue;
    }

    enviarQtAWorker(sock, nombre_archivo, worker_num);
  }

  cout << "=== Q^T DISTRIBUIDA A TODOS LOS WORKERS ===" << endl;
  cout << "Esperando cálculo de B en workers..." << endl;
}

vector<vector<double>> transponer(const vector<vector<double>> &M) {
  if (M.empty())
    return {};

  int filas = M.size();
  int cols = M[0].size();

  vector<vector<double>> Mt(cols, vector<double>(filas));

  for (int i = 0; i < filas; i++) {
    for (int j = 0; j < cols; j++) {
      Mt[j][i] = M[i][j];
    }
  }

  return Mt;
}

void enviarYAWorker001ParaQ() {
  cout << "\n=== ENVIANDO Y A WORKER001 PARA CALCULAR Q ===" << endl;

  string worker_id = "worker001";

  lock_guard<mutex> lock(clientsMutex);
  auto it = m_clients.find(worker_id);

  if (it == m_clients.end()) {
    cerr << "ERROR: Worker001 no encontrado" << endl;
    return;
  }

  int sock = it->second;

  string nombre_archivo = "Y_completa_for_Q.csv";
  ofstream archivo(nombre_archivo);
  if (!archivo.is_open()) {
    cerr << "ERROR: No se pudo crear archivo temporal" << endl;
    return;
  }

  for (const auto &fila : Y_completa) {
    for (size_t j = 0; j < fila.size(); j++) {
      archivo << fila[j];
      if (j < fila.size() - 1)
        archivo << ",";
    }
    archivo << "\n";
  }
  archivo.close();

  string fileData = leerArchivoBinario(nombre_archivo);
  if (fileData.empty()) {
    cerr << "ERROR: Archivo Y vacío" << endl;
    return;
  }

  string nombre_destino = "Y_completa.csv";

  string proto = "F" + padNumber(worker_id.size(), 2) + worker_id +
                 padNumber(nombre_destino.size(), 3) + nombre_destino +
                 padNumber(fileData.size(), 10) + fileData;

  write(sock, proto.c_str(), proto.size());

  cout << " Y_completa enviada a " << worker_id << " (" << fileData.size()
       << " bytes)" << endl;
  cout << "  Esperando que worker001 calcule Q con Gram-Schmidt..." << endl;

  remove(nombre_archivo.c_str());
}

void ensamblarY() {
  cout << "\n=== ENSAMBLANDO MATRIZ Y COMPLETA ===" << endl;

  vector<string> worker_ids;
  for (int i = 1; i <= MAX_WORKERS; i++) {
    worker_ids.push_back("worker" + padNumber(i, 3));
  }

  int total_filas = 0;
  int cols = 0;

  for (const auto &wid : worker_ids) {
    auto it = Y_parts.find(wid);
    if (it == Y_parts.end()) {
      cerr << "ERROR: Falta Y_part de " << wid << endl;
      return;
    }

    auto &chunk = it->second;
    if (chunk.empty()) {
      cerr << "ERROR: Y_part de " << wid << " está vacía" << endl;
      return;
    }

    if (cols == 0) {
      cols = chunk[0].size();
    } else if ((int)chunk[0].size() != cols) {
      cerr << "ERROR: Inconsistencia en columnas de Y" << endl;
      return;
    }

    total_filas += chunk.size();
    cout << "  " << wid << ": " << chunk.size() << " filas" << endl;
  }

  Y_completa.reserve(total_filas);

  for (const auto &wid : worker_ids) {
    auto &chunk = Y_parts[wid];
    Y_completa.insert(Y_completa.end(), chunk.begin(), chunk.end());
  }

  cout << "\nY completa ensamblada exitosamente!" << endl;
  cout << "  Dimensiones: " << Y_completa.size() << " × "
       << Y_completa[0].size() << endl;

  long long elementos = (long long)Y_completa.size() * Y_completa[0].size();
  long long memoria_bytes = elementos * sizeof(double);
  double memoria_mb = memoria_bytes / (1024.0 * 1024.0);
  double memoria_gb = memoria_mb / 1024.0;

  cout << "  Memoria usada: " << memoria_mb << " MB (" << memoria_gb << " GB)"
       << endl;

  ofstream file("Y_completa.csv");
  if (file.is_open()) {
    for (const auto &fila : Y_completa) {
      for (size_t j = 0; j < fila.size(); j++) {
        file << fila[j];
        if (j < fila.size() - 1)
          file << ",";
      }
      file << "\n";
    }
    file.close();
    cout << "  Y_completa guardada en 'Y_completa.csv'" << endl;
  }

  enviarYAWorker001ParaQ();
}

string crearArchivoBParte(int worker_num,
                          const vector<vector<double>> &B_part) {
  string nombre_archivo = "B_part_" + to_string(worker_num) + ".csv";

  ofstream archivo(nombre_archivo);
  if (!archivo.is_open()) {
    cerr << "Error creando archivo " << nombre_archivo << endl;
    return "";
  }

  for (const auto &fila : B_part) {
    for (size_t j = 0; j < fila.size(); j++) {
      archivo << fila[j];
      if (j < fila.size() - 1)
        archivo << ",";
    }
    archivo << "\n";
  }

  archivo.close();
  cout << "  Archivo creado: " << nombre_archivo << " (" << B_part.size() << "x"
       << B_part[0].size() << ")" << endl;

  return nombre_archivo;
}

void enviarBParteAWorker(int sock, const string &filename, int worker_num) {
  string fileData = leerArchivoBinario(filename);
  if (fileData.empty()) {
    cerr << "Error: Archivo vacío " << filename << endl;
    return;
  }

  string nombre_destino = "B_part_" + to_string(worker_num) + ".csv";
  string worker_id = "worker" + padNumber(worker_num + 1, 3);

  // F | tam_id(2 bytes) | worker_id | tam_n_archivo(3 bytes) | nombre_archivo |
  // tam_payload(10 bytes) | payload
  string proto = "F" + padNumber(worker_id.size(), 2) + worker_id +
                 padNumber(nombre_destino.size(), 3) + nombre_destino +
                 padNumber(fileData.size(), 10) + fileData;

  write(sock, proto.c_str(), proto.size());

  cout << "  Archivo enviado: " << filename << " -> " << worker_id << " ("
       << fileData.size() << " bytes)" << endl;
}

void distribuirBParaSVD() {
  cout << "\n=== DISTRIBUYENDO B A WORKERS PARA SVD ===" << endl;

  if (B_completa.empty()) {
    cerr << "ERROR: B_completa vacía" << endl;
    return;
  }

  int k = B_completa.size();
  int n = B_completa[0].size();

  cout << "  B completa: " << k << " × " << n << endl;

  int total_workers = MAX_WORKERS;
  int filas_por_worker = k / total_workers;

  lock_guard<mutex> lock(clientsMutex);

  for (int worker_num = 0; worker_num < total_workers; worker_num++) {
    string worker_id = "worker" + padNumber(worker_num + 1, 3);
    auto it = m_clients.find(worker_id);

    if (it == m_clients.end()) {
      cerr << "ERROR: Worker " << worker_id << " no encontrado" << endl;
      continue;
    }

    int sock = it->second;

    int fila_inicio = worker_num * filas_por_worker;
    int fila_fin = (worker_num == total_workers - 1)
                       ? k
                       : (worker_num + 1) * filas_por_worker;

    vector<vector<double>> B_part;
    for (int i = fila_inicio; i < fila_fin; i++) {
      B_part.push_back(B_completa[i]);
    }

    cout << "  Worker " << worker_num << ": B_part = " << B_part.size() << " × "
         << B_part[0].size() << endl;

    string nombre_archivo = crearArchivoBParte(worker_num, B_part);
    if (nombre_archivo.empty()) {
      cerr << "  Error creando archivo B para worker " << worker_num << endl;
      continue;
    }

    enviarBParteAWorker(sock, nombre_archivo, worker_num);
  }

  cout << "=== B DISTRIBUIDA A TODOS LOS WORKERS ===" << endl;
  cout << "Esperando cálculo de BtB en workers..." << endl;
}

void ensamblarB() {
  cout << "\n=== ENSAMBLANDO MATRIZ B COMPLETA ===" << endl;

  vector<string> worker_ids;
  for (int i = 1; i <= MAX_WORKERS; i++) {
    worker_ids.push_back("worker" + padNumber(i, 3));
  }

  int filas = 0;
  int cols = 0;

  for (const auto &wid : worker_ids) {
    auto it = B_parts.find(wid);
    if (it == B_parts.end()) {
      cerr << "ERROR: Falta B_part de " << wid << endl;
      return;
    }

    auto &chunk = it->second;
    if (chunk.empty()) {
      cerr << "ERROR: B_part de " << wid << " está vacía" << endl;
      return;
    }

    if (filas == 0) {
      filas = chunk.size();
      cols = chunk[0].size();
    } else if ((int)chunk.size() != filas) {
      cerr << "ERROR: Inconsistencia en filas de B" << endl;
      return;
    } else if ((int)chunk[0].size() != cols) {
      cerr << "ERROR: Inconsistencia en columnas de B" << endl;
      return;
    }

    cout << "  " << wid << ": " << chunk.size() << " × " << chunk[0].size()
         << endl;
  }

  B_completa.resize(filas, vector<double>(cols, 0.0));

  // SUMAR todas las contribuciones B_local
  for (const auto &wid : worker_ids) {
    auto &chunk = B_parts[wid];
    for (int i = 0; i < filas; i++) {
      for (int j = 0; j < cols; j++) {
        B_completa[i][j] += chunk[i][j];
      }
    }
  }

  cout << "\nB completa ensamblada: " << B_completa.size() << " × "
       << B_completa[0].size() << endl;

  cout << "  B[0][0] = " << B_completa[0][0] << endl;
  cout << "  B[0][1] = " << B_completa[0][1] << endl;

  long long elementos = (long long)B_completa.size() * B_completa[0].size();
  long long memoria_bytes = elementos * sizeof(double);
  double memoria_mb = memoria_bytes / (1024.0 * 1024.0);
  double memoria_gb = memoria_mb / 1024.0;

  cout << "  Memoria: " << memoria_mb << " MB (" << memoria_gb << " GB)"
       << endl;

  ofstream file("B_completa.csv");
  if (file.is_open()) {
    for (const auto &fila : B_completa) {
      for (size_t j = 0; j < fila.size(); j++) {
        file << fila[j];
        if (j < fila.size() - 1)
          file << ",";
      }
      file << "\n";
    }
    file.close();
    cout << "  B_completa guardada en 'B_completa.csv'" << endl;
  }

  distribuirBParaSVD();
}

string crearArchivoBtB() {
  string nombre_archivo = "BtB_completa_temp.csv";

  ofstream archivo(nombre_archivo);
  if (!archivo.is_open()) {
    cerr << "Error creando archivo " << nombre_archivo << endl;
    return "";
  }

  for (const auto &fila : BtB_completa) {
    for (size_t j = 0; j < fila.size(); j++) {
      archivo << fila[j];
      if (j < fila.size() - 1)
        archivo << ",";
    }
    archivo << "\n";
  }

  archivo.close();
  return nombre_archivo;
}

void enviarBtBAWorker(int sock, const string &filename,
                      const string &nombre_destino, int worker_num) {
  string fileData = leerArchivoBinario(filename);
  if (fileData.empty()) {
    cerr << "Error: Archivo vacío " << filename << endl;
    return;
  }

  string worker_id = "worker" + padNumber(worker_num + 1, 3);

  // F | tam_id(2 bytes) | worker_id | tam_n_archivo(3 bytes) | nombre_archivo |
  // tam_payload(10 bytes) | payload
  string proto = "F" + padNumber(worker_id.size(), 2) + worker_id +
                 padNumber(nombre_destino.size(), 3) + nombre_destino +
                 padNumber(fileData.size(), 10) + fileData;

  write(sock, proto.c_str(), proto.size());

  cout << "  Archivo enviado: " << filename << " -> " << worker_id << " ("
       << fileData.size() << " bytes)" << endl;
}

void distribuirBtBParaSVD() {
  cout << "\n=== DISTRIBUYENDO BtB A WORKER PARA CALCULAR SVD ===" << endl;

  if (BtB_completa.empty()) {
    cerr << "ERROR: BtB_completa vacía" << endl;
    return;
  }

  // Enviar BtB al worker 0 para que calcule SVD
  int worker_num = 0;
  string worker_id = "worker" + padNumber(worker_num + 1, 3);

  lock_guard<mutex> lock(clientsMutex);
  auto it = m_clients.find(worker_id);

  if (it == m_clients.end()) {
    cerr << "ERROR: Worker " << worker_id << " no encontrado" << endl;
    return;
  }

  int sock = it->second;

  string nombre_archivo = crearArchivoBtB();
  if (nombre_archivo.empty()) {
    cerr << "  Error creando archivo BtB" << endl;
    return;
  }

  string nombre_destino = "BtB_completa.csv";
  enviarBtBAWorker(sock, nombre_archivo, nombre_destino, worker_num);

  cout << "=== BtB ENVIADA A " << worker_id << " PARA CALCULAR SVD ===" << endl;
  cout << "Esperando cálculo de V y S..." << endl;
}

string crearArchivoV() {
  string nombre_archivo = "V_svd_temp.csv";

  ofstream archivo(nombre_archivo);
  if (!archivo.is_open()) {
    cerr << "Error creando archivo " << nombre_archivo << endl;
    return "";
  }

  for (const auto &fila : V_svd) {
    for (size_t j = 0; j < fila.size(); j++) {
      archivo << fila[j];
      if (j < fila.size() - 1)
        archivo << ",";
    }
    archivo << "\n";
  }

  archivo.close();
  return nombre_archivo;
}

string crearArchivoS() {
  string nombre_archivo = "S_svd_temp.csv";

  ofstream archivo(nombre_archivo);
  if (!archivo.is_open()) {
    cerr << "Error creando archivo " << nombre_archivo << endl;
    return "";
  }

  for (size_t i = 0; i < S_svd.size(); i++) {
    archivo << S_svd[i];
    if (i < S_svd.size() - 1)
      archivo << ",";
  }
  archivo << "\n";

  archivo.close();
  return nombre_archivo;
}

void enviarVySAWorker(int sock, const string &filename,
                      const string &nombre_destino, int worker_num) {
  string fileData = leerArchivoBinario(filename);
  if (fileData.empty()) {
    cerr << "Error: Archivo vacío " << filename << endl;
    return;
  }

  string worker_id = "worker" + padNumber(worker_num + 1, 3);

  // F | tam_id(2 bytes) | worker_id | tam_n_archivo(3 bytes) | nombre_archivo |
  // tam_payload(10 bytes) | payload
  string proto = "F" + padNumber(worker_id.size(), 2) + worker_id +
                 padNumber(nombre_destino.size(), 3) + nombre_destino +
                 padNumber(fileData.size(), 10) + fileData;

  write(sock, proto.c_str(), proto.size());

  cout << "  Archivo enviado: " << filename << " -> " << worker_id << " ("
       << fileData.size() << " bytes)" << endl;
}

void distribuirVyS() {
  cout << "\n=== DISTRIBUYENDO V Y S A WORKERS ===" << endl;

  if (V_svd.empty() || S_svd.empty()) {
    cerr << "ERROR: V o S no calculados" << endl;
    return;
  }

  lock_guard<mutex> lock(clientsMutex);

  string archivo_V = crearArchivoV();
  string archivo_S = crearArchivoS();

  if (archivo_V.empty() || archivo_S.empty()) {
    cerr << "ERROR: No se pudieron crear archivos V o S" << endl;
    return;
  }

  // Enviar V y S a todos los workers
  for (int worker_num = 0; worker_num < MAX_WORKERS; worker_num++) {
    string worker_id = "worker" + padNumber(worker_num + 1, 3);
    auto it = m_clients.find(worker_id);

    if (it == m_clients.end()) {
      cerr << "ERROR: Worker " << worker_id << " no encontrado" << endl;
      continue;
    }

    int sock = it->second;

    string nombre_V = "V_svd_" + to_string(worker_num) + ".csv";
    enviarVySAWorker(sock, archivo_V, nombre_V, worker_num);

    string nombre_S = "S_svd_" + to_string(worker_num) + ".csv";
    enviarVySAWorker(sock, archivo_S, nombre_S, worker_num);
  }

  cout << "=== V Y S DISTRIBUIDOS A TODOS LOS WORKERS ===" << endl;
  cout << "Esperando cálculo de U en workers..." << endl;
}

void ensamblarBtB() {
  cout << "\n=== ENSAMBLANDO BtB COMPLETA ===" << endl;

  vector<string> worker_ids;
  for (int i = 1; i <= MAX_WORKERS; i++) {
    worker_ids.push_back("worker" + padNumber(i, 3));
  }

  int n = 0;

  for (const auto &wid : worker_ids) {
    auto it = BtB_parts.find(wid);
    if (it == BtB_parts.end()) {
      cerr << "ERROR: Falta BtB_part de " << wid << endl;
      return;
    }

    auto &chunk = it->second;
    if (chunk.empty()) {
      cerr << "ERROR: BtB_part de " << wid << " está vacía" << endl;
      return;
    }

    if (n == 0) {
      n = chunk.size();
    } else if ((int)chunk.size() != n || (int)chunk[0].size() != n) {
      cerr << "ERROR: Inconsistencia en dimensiones de BtB" << endl;
      return;
    }

    cout << "  " << wid << ": " << chunk.size() << " × " << chunk[0].size()
         << endl;
  }

  BtB_completa.resize(n, vector<double>(n, 0.0));

  // SUMAR todas las contribuciones BtB_local
  for (const auto &wid : worker_ids) {
    auto &chunk = BtB_parts[wid];
    for (int i = 0; i < n; i++) {
      for (int j = 0; j < n; j++) {
        BtB_completa[i][j] += chunk[i][j];
      }
    }
  }

  cout << "\nBtB completa ensamblada: " << BtB_completa.size() << " × "
       << BtB_completa[0].size() << endl;

  distribuirBtBParaSVD();
}

void ensamblarU() {
  cout << "\n=== ENSAMBLANDO MATRIZ U COMPLETA ===" << endl;

  vector<string> worker_ids;
  for (int i = 1; i <= MAX_WORKERS; i++) {
    worker_ids.push_back("worker" + padNumber(i, 3));
  }

  int total_filas = 0;
  int cols = 0;

  for (const auto &wid : worker_ids) {
    auto it = U_parts.find(wid);
    if (it == U_parts.end()) {
      cerr << "ERROR: Falta U_part de " << wid << endl;
      return;
    }

    auto &chunk = it->second;
    if (chunk.empty()) {
      cerr << "ERROR: U_part de " << wid << " está vacía" << endl;
      return;
    }

    if (cols == 0) {
      cols = chunk[0].size();
    } else if ((int)chunk[0].size() != cols) {
      cerr << "ERROR: Inconsistencia en columnas de U" << endl;
      return;
    }

    total_filas += chunk.size();
    cout << "  " << wid << ": " << chunk.size() << " filas" << endl;
  }

  U_completa.reserve(total_filas);

  for (const auto &wid : worker_ids) {
    auto &chunk = U_parts[wid];
    for (const auto &fila : chunk) {
      U_completa.push_back(fila);
    }
  }

  cout << "\nU completa ensamblada: " << U_completa.size() << " × "
       << U_completa[0].size() << endl;

  // Guardar U
  ofstream file("U_completa.csv");
  if (file.is_open()) {
    for (const auto &fila : U_completa) {
      for (size_t j = 0; j < fila.size(); j++) {
        file << fila[j];
        if (j < fila.size() - 1)
          file << ",";
      }
      file << "\n";
    }
    file.close();
    cout << "  U_completa guardada en 'U_completa.csv'" << endl;
  }

  if (!V_svd.empty()) {
    ofstream fileV("V_completa.csv");
    if (fileV.is_open()) {
      for (const auto &fila : V_svd) {
        for (size_t j = 0; j < fila.size(); j++) {
          fileV << fila[j];
          if (j < fila.size() - 1)
            fileV << ",";
        }
        fileV << "\n";
      }
      fileV.close();
      cout << "  V_completa guardada en 'V_completa.csv'" << endl;
    }
  }

  if (!S_svd.empty()) {
    ofstream fileS("S_completa.csv");
    if (fileS.is_open()) {
      for (size_t i = 0; i < S_svd.size(); i++) {
        fileS << S_svd[i];
        if (i < S_svd.size() - 1)
          fileS << ",";
      }
      fileS << "\n";
      fileS.close();
      cout << "  S_completa guardada en 'S_completa.csv'" << endl;
    }
  }

  cout << "\n=== SVD DISTRIBUIDO COMPLETADO ===" << endl;
  cout << "  Archivos guardados: U_completa.csv, V_completa.csv, S_completa.csv"
       << endl;
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
    case 'y': { // Recibir Y_local de worker
      cout << "\n>>> Recibiendo Y_local..." << endl;

      string lenStr;
      for (int i = 0; i < 3; ++i) {
        char c;
        if (read(socketConn, &c, 1) <= 0)
          return;
        lenStr += c;
      }
      int worker_id_len = stoi(lenStr);

      string worker_id;
      for (int i = 0; i < worker_id_len; ++i) {
        char c;
        if (read(socketConn, &c, 1) <= 0)
          return;
        worker_id += c;
      }

      string filas_str, cols_str;
      for (int i = 0; i < 8; ++i) {
        char c;
        if (read(socketConn, &c, 1) <= 0)
          return;
        filas_str += c;
      }
      for (int i = 0; i < 8; ++i) {
        char c;
        if (read(socketConn, &c, 1) <= 0)
          return;
        cols_str += c;
      }
      int filas = stoi(filas_str);
      int cols = stoi(cols_str);

      string data_len_str;
      for (int i = 0; i < 10; ++i) {
        char c;
        if (read(socketConn, &c, 1) <= 0)
          return;
        data_len_str += c;
      }
      long data_len = stol(data_len_str);

      cout << "  Worker: " << worker_id << endl;
      cout << "  Dimensiones: " << filas << " × " << cols << endl;
      cout << "  Bytes a recibir: " << data_len << endl;

      string y_data;
      y_data.resize(data_len);
      size_t totalRead = 0;
      while (totalRead < (size_t)data_len) {
        ssize_t n = read(socketConn, &y_data[totalRead], data_len - totalRead);
        if (n <= 0) {
          cerr << "Error leyendo datos de Y" << endl;
          return;
        }
        totalRead += n;
      }

      cout << "  Datos recibidos: " << totalRead << " bytes" << endl;

      // Parsear CSV
      cout << "  Parseando datos..." << endl;
      vector<vector<double>> Y_chunk;
      stringstream ss(y_data);
      string line;
      int line_count = 0;

      while (getline(ss, line)) {
        if (line.empty())
          continue;

        vector<double> fila;
        stringstream ss_line(line);
        string val;

        while (getline(ss_line, val, ',')) {
          fila.push_back(stod(val));
        }

        if (!fila.empty()) {
          Y_chunk.push_back(fila);
          line_count++;
        }
      }

      {
        lock_guard<mutex> lock(Y_mutex);
        Y_parts[worker_id] = Y_chunk;

        cout << "  Y_local de " << worker_id << " almacenada" << endl;
        cout << "  Partes recibidas: " << Y_parts.size() << "/" << MAX_WORKERS
             << endl;

        if ((int)Y_parts.size() == MAX_WORKERS) {
          ensamblarY();
        }
      }
      break;
    }
    case 'Q': { // Recibir Q_part de worker
      cout << "\n>>> Recibiendo Q_part..." << endl;

      string lenStr;
      for (int i = 0; i < 3; ++i) {
        char c;
        if (read(socketConn, &c, 1) <= 0)
          return;
        lenStr += c;
      }
      int worker_id_len = stoi(lenStr);

      string worker_id;
      for (int i = 0; i < worker_id_len; ++i) {
        char c;
        if (read(socketConn, &c, 1) <= 0)
          return;
        worker_id += c;
      }

      string filas_str, cols_str;
      for (int i = 0; i < 8; ++i) {
        char c;
        if (read(socketConn, &c, 1) <= 0)
          return;
        filas_str += c;
      }
      for (int i = 0; i < 8; ++i) {
        char c;
        if (read(socketConn, &c, 1) <= 0)
          return;
        cols_str += c;
      }
      int filas = stoi(filas_str);
      int cols = stoi(cols_str);

      string data_len_str;
      for (int i = 0; i < 10; ++i) {
        char c;
        if (read(socketConn, &c, 1) <= 0)
          return;
        data_len_str += c;
      }
      long data_len = stol(data_len_str);

      cout << "  Worker: " << worker_id << " | " << filas << " × " << cols
           << endl;

      string q_data;
      q_data.resize(data_len);
      size_t totalRead = 0;
      while (totalRead < (size_t)data_len) {
        ssize_t n = read(socketConn, &q_data[totalRead], data_len - totalRead);
        if (n <= 0) {
          cerr << "Error leyendo Q_part" << endl;
          return;
        }
        totalRead += n;
      }

      // Parsear Q COMPLETA
      vector<vector<double>> Q_completa;
      stringstream ss(q_data);
      string line;

      while (getline(ss, line)) {
        if (line.empty())
          continue;

        vector<double> fila;
        stringstream ss_line(line);
        string val;

        while (getline(ss_line, val, ',')) {
          fila.push_back(stod(val));
        }

        if (!fila.empty()) {
          Q_completa.push_back(fila);
        }
      }

      Q = Q_completa;

      cout << "  ✓ Q completa recibida: " << Q.size() << " × " << Q[0].size()
           << endl;

      ofstream fileq("Q.csv");
      if (fileq.is_open()) {
        for (const auto &fila : Q) {
          for (size_t j = 0; j < fila.size(); j++) {
            fileq << fila[j];
            if (j < fila.size() - 1)
              fileq << ",";
          }
          fileq << "\n";
        }
        fileq.close();
        cout << "  Q guardada en 'Q.csv'" << endl;
      }
      Qt = transponer(Q);
      cout << "  ✓ Q^T calculada: " << Qt.size() << " × " << Qt[0].size()
           << endl;

      distribuirQtYCalcularB();
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

      cout << "Worker " << worker_id << " confirmó que Omega está lista"
           << endl;

      int worker_num = 0;
      if (worker_id.find("worker") == 0) {
        string num_str = worker_id.substr(6);
        worker_num = stoi(num_str) - 1;
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

      // Guardar el rango de filas para este worker
      {
        lock_guard<mutex> lock(A_ranges_mutex);
        A_row_ranges[worker_num] = make_pair(inicio, fin);
      }

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
    case 'b': { // Recibir B_local de worker
      cout << "\n>>> Recibiendo B_local..." << endl;

      string lenStr;
      for (int i = 0; i < 3; ++i) {
        char c;
        if (read(socketConn, &c, 1) <= 0)
          return;
        lenStr += c;
      }
      int worker_id_len = stoi(lenStr);

      string worker_id;
      for (int i = 0; i < worker_id_len; ++i) {
        char c;
        if (read(socketConn, &c, 1) <= 0)
          return;
        worker_id += c;
      }

      string filas_str, cols_str;
      for (int i = 0; i < 8; ++i) {
        char c;
        if (read(socketConn, &c, 1) <= 0)
          return;
        filas_str += c;
      }
      for (int i = 0; i < 8; ++i) {
        char c;
        if (read(socketConn, &c, 1) <= 0)
          return;
        cols_str += c;
      }
      int filas = stoi(filas_str);
      int cols = stoi(cols_str);

      string data_len_str;
      for (int i = 0; i < 10; ++i) {
        char c;
        if (read(socketConn, &c, 1) <= 0)
          return;
        data_len_str += c;
      }
      long data_len = stol(data_len_str);

      cout << "  Worker: " << worker_id << " | " << filas << " × " << cols
           << endl;

      string b_data;
      b_data.resize(data_len);
      size_t totalRead = 0;
      while (totalRead < (size_t)data_len) {
        ssize_t n = read(socketConn, &b_data[totalRead], data_len - totalRead);
        if (n <= 0) {
          cerr << "Error leyendo B" << endl;
          return;
        }
        totalRead += n;
      }

      // Parsear CSV
      vector<vector<double>> B_chunk;
      stringstream ss(b_data);
      string line;

      while (getline(ss, line)) {
        if (line.empty())
          continue;

        vector<double> fila;
        stringstream ss_line(line);
        string val;

        while (getline(ss_line, val, ',')) {
          fila.push_back(stod(val));
        }

        if (!fila.empty()) {
          B_chunk.push_back(fila);
        }
      }

      {
        lock_guard<mutex> lock(B_mutex);
        B_parts[worker_id] = B_chunk;

        cout << "  B_local almacenada (" << B_parts.size() << "/" << MAX_WORKERS
             << ")" << endl;

        if ((int)B_parts.size() == MAX_WORKERS) {
          ensamblarB();
        }
      }
      break;
    }
    case 'T': { // Recibir BtB_contrib de worker
      cout << "\n>>> Recibiendo BtB_contrib..." << endl;

      string lenStr;
      for (int i = 0; i < 3; ++i) {
        char c;
        if (read(socketConn, &c, 1) <= 0)
          return;
        lenStr += c;
      }
      int worker_id_len = stoi(lenStr);

      string worker_id;
      for (int i = 0; i < worker_id_len; ++i) {
        char c;
        if (read(socketConn, &c, 1) <= 0)
          return;
        worker_id += c;
      }

      string filas_str, cols_str;
      for (int i = 0; i < 8; ++i) {
        char c;
        if (read(socketConn, &c, 1) <= 0)
          return;
        filas_str += c;
      }
      for (int i = 0; i < 8; ++i) {
        char c;
        if (read(socketConn, &c, 1) <= 0)
          return;
        cols_str += c;
      }
      int filas = stoi(filas_str);
      int cols = stoi(cols_str);

      string data_len_str;
      for (int i = 0; i < 10; ++i) {
        char c;
        if (read(socketConn, &c, 1) <= 0)
          return;
        data_len_str += c;
      }
      long data_len = stol(data_len_str);

      cout << "  Worker: " << worker_id << " | " << filas << " × " << cols
           << endl;

      string data;
      data.resize(data_len);
      size_t totalRead = 0;
      while (totalRead < (size_t)data_len) {
        ssize_t n = read(socketConn, &data[totalRead], data_len - totalRead);
        if (n <= 0) {
          cerr << "Error leyendo BtB_contrib" << endl;
          return;
        }
        totalRead += n;
      }

      // Parsear CSV
      vector<vector<double>> BtB_chunk;
      stringstream ss(data);
      string line;

      while (getline(ss, line)) {
        if (line.empty())
          continue;

        vector<double> fila;
        stringstream ss_line(line);
        string val;

        while (getline(ss_line, val, ',')) {
          fila.push_back(stod(val));
        }

        if (!fila.empty()) {
          BtB_chunk.push_back(fila);
        }
      }

      {
        lock_guard<mutex> lock(BtB_mutex);
        BtB_parts[worker_id] = BtB_chunk;

        cout << "  BtB_contrib almacenada (" << BtB_parts.size() << "/"
             << MAX_WORKERS << ")" << endl;

        if ((int)BtB_parts.size() == MAX_WORKERS) {
          ensamblarBtB();
        }
      }
      break;
    }
    case 'U': { // Recibir U_part de worker
      cout << "\n>>> Recibiendo U_part..." << endl;

      string lenStr;
      for (int i = 0; i < 3; ++i) {
        char c;
        if (read(socketConn, &c, 1) <= 0)
          return;
        lenStr += c;
      }
      int worker_id_len = stoi(lenStr);

      string worker_id;
      for (int i = 0; i < worker_id_len; ++i) {
        char c;
        if (read(socketConn, &c, 1) <= 0)
          return;
        worker_id += c;
      }

      string filas_str, cols_str;
      for (int i = 0; i < 8; ++i) {
        char c;
        if (read(socketConn, &c, 1) <= 0)
          return;
        filas_str += c;
      }
      for (int i = 0; i < 8; ++i) {
        char c;
        if (read(socketConn, &c, 1) <= 0)
          return;
        cols_str += c;
      }
      int filas = stoi(filas_str);
      int cols = stoi(cols_str);

      string data_len_str;
      for (int i = 0; i < 10; ++i) {
        char c;
        if (read(socketConn, &c, 1) <= 0)
          return;
        data_len_str += c;
      }
      long data_len = stol(data_len_str);

      cout << "  Worker: " << worker_id << " | " << filas << " × " << cols
           << endl;

      string u_data;
      u_data.resize(data_len);
      size_t totalRead = 0;
      while (totalRead < (size_t)data_len) {
        ssize_t n = read(socketConn, &u_data[totalRead], data_len - totalRead);
        if (n <= 0) {
          cerr << "Error leyendo U_part" << endl;
          return;
        }
        totalRead += n;
      }

      vector<vector<double>> U_chunk;
      stringstream ss(u_data);
      string line;

      while (getline(ss, line)) {
        if (line.empty())
          continue;

        vector<double> fila;
        stringstream ss_line(line);
        string val;

        while (getline(ss_line, val, ',')) {
          fila.push_back(stod(val));
        }

        if (!fila.empty()) {
          U_chunk.push_back(fila);
        }
      }

      {
        lock_guard<mutex> lock(U_mutex);
        U_parts[worker_id] = U_chunk;

        cout << "  U_part almacenada (" << U_parts.size() << "/" << MAX_WORKERS
             << ")" << endl;

        if ((int)U_parts.size() == MAX_WORKERS) {
          ensamblarU();
        }
      }
      break;
    }
    case 'V': { // Recibir V del SVD de worker
      cout << "\n>>> Recibiendo V del SVD..." << endl;

      string lenStr;
      for (int i = 0; i < 3; ++i) {
        char c;
        if (read(socketConn, &c, 1) <= 0)
          return;
        lenStr += c;
      }
      int worker_id_len = stoi(lenStr);

      string worker_id;
      for (int i = 0; i < worker_id_len; ++i) {
        char c;
        if (read(socketConn, &c, 1) <= 0)
          return;
        worker_id += c;
      }

      string filas_str, cols_str;
      for (int i = 0; i < 8; ++i) {
        char c;
        if (read(socketConn, &c, 1) <= 0)
          return;
        filas_str += c;
      }
      for (int i = 0; i < 8; ++i) {
        char c;
        if (read(socketConn, &c, 1) <= 0)
          return;
        cols_str += c;
      }
      int filas = stoi(filas_str);
      int cols = stoi(cols_str);

      string data_len_str;
      for (int i = 0; i < 10; ++i) {
        char c;
        if (read(socketConn, &c, 1) <= 0)
          return;
        data_len_str += c;
      }
      long data_len = stol(data_len_str);

      cout << "  Worker: " << worker_id << " | V: " << filas << " × " << cols
           << endl;

      string v_data;
      v_data.resize(data_len);
      size_t totalRead = 0;
      while (totalRead < (size_t)data_len) {
        ssize_t n = read(socketConn, &v_data[totalRead], data_len - totalRead);
        if (n <= 0) {
          cerr << "Error leyendo V" << endl;
          return;
        }
        totalRead += n;
      }

      vector<vector<double>> V_chunk;
      stringstream ss(v_data);
      string line;

      while (getline(ss, line)) {
        if (line.empty())
          continue;

        vector<double> fila;
        stringstream ss_line(line);
        string val;

        while (getline(ss_line, val, ',')) {
          fila.push_back(stod(val));
        }

        if (!fila.empty()) {
          V_chunk.push_back(fila);
        }
      }

      V_svd = V_chunk;
      cout << "  V recibida y almacenada" << endl;

      // Si también tenemos S, distribuir a todos los workers
      if (!S_svd.empty()) {
        distribuirVyS();
      }
      break;
    }
    case 's': { // Recibir S (valores singulares) del SVD de worker
      cout << "\n>>> Recibiendo S (valores singulares) del SVD..." << endl;

      string lenStr;
      for (int i = 0; i < 3; ++i) {
        char c;
        if (read(socketConn, &c, 1) <= 0)
          return;
        lenStr += c;
      }
      int worker_id_len = stoi(lenStr);

      string worker_id;
      for (int i = 0; i < worker_id_len; ++i) {
        char c;
        if (read(socketConn, &c, 1) <= 0)
          return;
        worker_id += c;
      }

      string length_str;
      for (int i = 0; i < 8; ++i) {
        char c;
        if (read(socketConn, &c, 1) <= 0)
          return;
        length_str += c;
      }
      int length = stoi(length_str);

      string data_len_str;
      for (int i = 0; i < 10; ++i) {
        char c;
        if (read(socketConn, &c, 1) <= 0)
          return;
        data_len_str += c;
      }
      long data_len = stol(data_len_str);

      cout << "  Worker: " << worker_id << " | S: " << length
           << " valores singulares" << endl;

      string s_data;
      s_data.resize(data_len);
      size_t totalRead = 0;
      while (totalRead < (size_t)data_len) {
        ssize_t n = read(socketConn, &s_data[totalRead], data_len - totalRead);
        if (n <= 0) {
          cerr << "Error leyendo S" << endl;
          return;
        }
        totalRead += n;
      }

      stringstream ss(s_data);
      string line;
      if (getline(ss, line)) {
        stringstream ss_line(line);
        string val;
        S_svd.clear();
        while (getline(ss_line, val, ',')) {
          S_svd.push_back(stod(val));
        }
      }

      cout << "  S recibida y almacenada" << endl;

      // Si también tenemos V, distribuir a todos los workers
      if (!V_svd.empty()) {
        distribuirVyS();
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
      cout << "  ./server.exe        → k 1000, p 100, archivo matrix.txt\n";
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

  if (-1 == SocketServer) {
    perror("can not create socket");
    exit(EXIT_FAILURE);
  }

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