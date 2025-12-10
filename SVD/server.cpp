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

map<string, vector<vector<double>>> Y_parts;
vector<vector<double>> Y_completa;
vector<vector<double>> Q;
vector<vector<double>> Qt;
mutex Y_mutex;

map<string, vector<vector<double>>> B_parts;
vector<vector<double>> B_completa;
mutex B_mutex;

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

  string proto = "F" + padNumber(worker_id.size(), 2) + worker_id +
                 padNumber(nombre_destino.size(), 3) + nombre_destino +
                 padNumber(fileData.size(), 10) + fileData;

  write(sock, proto.c_str(), proto.size());

  cout << "  ✓ Archivo enviado: " << filename << " -> " << worker_id << " ("
       << fileData.size() << " bytes)" << endl;

  remove(filename.c_str());
  cout << "  Archivo temporal eliminado: " << filename << endl;
}

vector<vector<double>> gramSchmidt(const vector<vector<double>> &Y) {
  int m = Y.size();
  int l = Y[0].size();

  vector<vector<double>> Q(l);
  for (int j = 0; j < l; j++) {
    Q[j].resize(m);
    for (int i = 0; i < m; i++) {
      Q[j][i] = Y[i][j];
    }
  }

  vector<vector<double>> R(l, vector<double>(l, 0.0));

  for (int j = 0; j < l; j++) {
    for (int i = 0; i < j; i++) {
      double dot = 0.0;
      for (int k = 0; k < m; k++) {
        dot += Q[i][k] * Q[j][k];
      }
      R[i][j] = dot;

      for (int k = 0; k < m; k++) {
        Q[j][k] -= dot * Q[i][k];
      }
    }

    double norm = 0.0;
    for (int k = 0; k < m; k++) {
      norm += Q[j][k] * Q[j][k];
    }
    norm = sqrt(norm);
    R[j][j] = norm;

    if (norm > 1e-12) {
      for (int k = 0; k < m; k++) {
        Q[j][k] /= norm;
      }
    } else {
      for (int k = 0; k < m; k++) {
        Q[j][k] = (k == j && k < m) ? 1.0 : 0.0;
      }
    }
  }

  vector<vector<double>> Q_filas(m, vector<double>(l));
  for (int i = 0; i < m; i++) {
    for (int j = 0; j < l; j++) {
      Q_filas[i][j] = Q[j][i];
    }
  }

  return Q_filas;
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

  cout << "  ✓ Q^T enviada: " << filename << " -> " << worker_id << " ("
       << fileData.size() << " bytes)" << endl;

  remove(filename.c_str());
}

void distribuirQtYCalcularB() {
  cout << "\n=== DISTRIBUYENDO Q^T A WORKERS ===" << endl;

  // Qt ya está calculada (k × m)
  int k = Qt.size();
  int m = Qt[0].size();

  cout << "  Q^T: " << k << " × " << m << endl;

  // Dividir Qt por COLUMNAS (cada worker recibe las columnas correspondientes
  // a su parte de A)
  int total_workers = MAX_WORKERS;
  int cols_por_worker = m / total_workers;

  lock_guard<mutex> lock(clientsMutex);

  for (int worker_num = 0; worker_num < total_workers; worker_num++) {
    string worker_id = "worker" + padNumber(worker_num + 1, 3);
    auto it = m_clients.find(worker_id);

    if (it == m_clients.end()) {
      cerr << "ERROR: Worker " << worker_id << " no encontrado" << endl;
      continue;
    }

    int sock = it->second;

    // Determinar rango de columnas para este worker
    int col_inicio = worker_num * cols_por_worker;
    int col_fin = (worker_num == total_workers - 1)
                      ? m
                      : (worker_num + 1) * cols_por_worker;

    // Extraer Qt_part: TODAS las filas, pero solo columnas [col_inicio,
    // col_fin)
    vector<vector<double>> Qt_part;
    for (int i = 0; i < k; i++) {
      vector<double> fila_parte;
      for (int j = col_inicio; j < col_fin; j++) {
        fila_parte.push_back(Qt[i][j]);
      }
      Qt_part.push_back(fila_parte);
    }

    cout << "  Worker " << worker_num << ": Qt_part = " << Qt_part.size()
         << " × " << Qt_part[0].size() << endl;

    // Crear archivo temporal
    string nombre_archivo = crearArchivoQtParte(worker_num, Qt_part);
    if (nombre_archivo.empty()) {
      cerr << "  Error creando archivo Qt para worker " << worker_num << endl;
      continue;
    }

    // Enviar archivo
    enviarQtAWorker(sock, nombre_archivo, worker_num);
  }

  cout << "=== Q^T DISTRIBUIDA A TODOS LOS WORKERS ===" << endl;
  cout << "Esperando cálculo de B en workers..." << endl;
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

  cout << "\n✓ Y completa ensamblada exitosamente!" << endl;
  cout << "  Dimensiones: " << Y_completa.size() << " × "
       << Y_completa[0].size() << endl;

  cout << "  Y[0][0] = " << Y_completa[0][0] << endl;
  cout << "  Y[0][1] = " << Y_completa[0][1] << endl;
  cout << "  Y[" << Y_completa.size() - 1
       << "][0] = " << Y_completa[Y_completa.size() - 1][0] << endl;

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

  Q = gramSchmidt(Y_completa);

  cout << "\n=== CALCULANDO Q^T ===" << endl;
  Qt = transponer(Q);
  cout << "  ✓ Q^T calculada: " << Qt.size() << " × " << Qt[0].size() << endl;

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

  distribuirQtYCalcularB();
}

void ensamblarB() {
  cout << "\n=== ENSAMBLANDO MATRIZ B COMPLETA ===" << endl;

  vector<string> worker_ids;
  for (int i = 1; i <= MAX_WORKERS; i++) {
    worker_ids.push_back("worker" + padNumber(i, 3));
  }

  int filas = 0;
  int total_cols = 0;

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
    } else if ((int)chunk.size() != filas) {
      cerr << "ERROR: Inconsistencia en filas de B" << endl;
      return;
    }

    total_cols += chunk[0].size();
    cout << "  " << wid << ": " << chunk.size() << " × " << chunk[0].size()
         << endl;
  }

  B_completa.resize(filas);
  for (int i = 0; i < filas; i++) {
    B_completa[i].reserve(total_cols);
  }

  for (const auto &wid : worker_ids) {
    auto &chunk = B_parts[wid];
    for (int i = 0; i < filas; i++) {
      B_completa[i].insert(B_completa[i].end(), chunk[i].begin(),
                           chunk[i].end());
    }
  }

  cout << "\n✓ B completa ensamblada: " << B_completa.size() << " × "
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

  cout << "\n=== RANDOMIZED SVD DISTRIBUIDO COMPLETADO ===" << endl;
  cout << "Siguiente paso: Calcular SVD(B) localmente en el servidor" << endl;
  cout << "B es pequeña (" << B_completa.size() << " × " << B_completa[0].size()
       << "), así que se puede calcular con cualquier librería (Eigen, LAPACK, "
          "etc.)"
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

      cout << "  Filas parseadas: " << line_count << endl;

      // Verificar dimensiones
      if (line_count != filas) {
        cerr << "  ADVERTENCIA: Esperaba " << filas << " filas, obtuvo "
             << line_count << endl;
      }
      if (!Y_chunk.empty() && (int)Y_chunk[0].size() != cols) {
        cerr << "  ADVERTENCIA: Esperaba " << cols << " columnas, obtuvo "
             << Y_chunk[0].size() << endl;
      }

      // Guardar en estructura global
      {
        lock_guard<mutex> lock(Y_mutex);
        Y_parts[worker_id] = Y_chunk;

        cout << "  ✓ Y_local de " << worker_id << " almacenada" << endl;
        cout << "  Partes recibidas: " << Y_parts.size() << "/" << MAX_WORKERS
             << endl;

        if ((int)Y_parts.size() == MAX_WORKERS) {
          ensamblarY();
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

      cout << "Worker " << worker_id << " confirmó que Omega está lista"
           << endl;

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

        cout << "  ✓ B_local almacenada (" << B_parts.size() << "/"
             << MAX_WORKERS << ")" << endl;

        if ((int)B_parts.size() == MAX_WORKERS) {
          ensamblarB();
        }
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