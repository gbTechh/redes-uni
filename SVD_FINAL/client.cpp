// cliente.cpp
#include <algorithm>
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
vector<vector<double>>
    Y_completa; // Y completa recibida del servidor para calcular Q
vector<vector<double>> Q_local; // Q calculada localmente usando Gram-Schmidt
vector<vector<double>> Qt_part; // Nueva: parte de Q^T
vector<vector<double>> B_local;
vector<vector<double>> B_part;      // Parte de B para SVD distribuido
vector<vector<double>> BtB_contrib; // Contribución a BtB = B_part^T × B_part
vector<vector<double>>
    BtB_completa;              // BtB completa para calcular SVD (solo worker 0)
vector<vector<double>> U_part; // Parte de U para SVD
vector<vector<double>> V_svd;  // Matriz V del SVD
vector<double> S_svd;          // Valores singulares

bool A_part_recibida = false;
bool Y_local_calculada = false;
bool Y_completa_recibida = false;
bool Q_local_calculada = false;
bool Qt_part_recibida = false;
bool B_local_calculada = false;
bool B_part_recibida = false;
bool BtB_contrib_calculada = false;
bool V_recibida = false;
bool S_recibida = false;
bool U_part_calculada = false;
int worker_num_asignado = -1;

void enviarBtBContribucion(int socketConn);
void enviarVyS(int socketConn);
void enviarUPart(int socketConn);
void calcularQLocal(int socketConn);
void enviarQPart(int socketConn);

void guardarArchivo(const string &filename, const string &data) {
  ofstream archivo(filename, ios::binary);
  if (archivo) {
    archivo.write(data.c_str(), data.size());
    archivo.flush();
    archivo.close();

    ifstream verificar(filename, ios::binary | ios::ate);
    if (verificar) {
      streamsize size = verificar.tellg();
      verificar.close();
      if (size != (streamsize)data.size()) {
        cerr << "ERROR: Archivo " << filename
             << " no se escribió completamente. "
             << "Esperado: " << data.size() << " bytes, Escrito: " << size
             << " bytes" << endl;
      }
    }
  } else {
    cerr << "ERROR: No se pudo abrir el archivo para escribir: " << filename
         << endl;
  }
}

void multiplicarRangoMatrices(int fila_inicio, int fila_fin,
                              const vector<vector<double>> &A,
                              const vector<vector<double>> &B,
                              vector<vector<double>> &C) {
  int cols_A = A[0].size();
  int cols_B = B[0].size();

  for (int i = fila_inicio; i < fila_fin; i++) {
    for (int j = 0; j < cols_B; j++) {
      double suma = 0.0;
      for (int k = 0; k < cols_A; k++) {
        suma += A[i][k] * B[k][j];
      }
      C[i][j] = suma;
    }
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

  // y | id formateado | tam_id(3 bytes) | id | rows(8bytes) | columns(8bytes) |
  // tam_payload(10 bytes) | payload
  string proto =
      "y" + padNumber(my_id.size(), 3) + my_id + padNumber(Y_local.size(), 8) +
      padNumber(Y_local[0].size(), 8) + padNumber(y_data.size(), 10) + y_data;

  cout << "  Enviando..." << endl;

  write(socketConn, proto.c_str(), proto.size());

  cout << "Y_local enviada exitosamente" << endl;
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
  int filas_Omega = Omega.size();
  int cols_Omega = Omega[0].size();

  cout << "  A_part: " << filas_A << " × " << cols_A << endl;
  cout << "  Omega:  " << filas_Omega << " × " << cols_Omega << endl;

  if (cols_A != filas_Omega) {
    cerr << "ERROR: No se puede multiplicar: A.cols=" << cols_A
         << " pero Ω.rows=" << filas_Omega << endl;
    return;
  }

  Y_local.resize(filas_A, vector<double>(cols_Omega, 0.0));

  cout << "Multiplicando matrices..." << endl;

  int num_hilos = thread::hardware_concurrency();
  if (num_hilos == 0) {
    num_hilos = 4;
  }

  int filas_por_hilo = filas_A / num_hilos;
  vector<thread> hilos;

  cout << "  Iniciando con " << num_hilos << " hilos..." << endl;

  for (int i = 0; i < num_hilos; i++) {
    int fila_inicio = i * filas_por_hilo;
    int fila_fin;

    if (i == num_hilos - 1) {
      fila_fin = filas_A;
    } else {
      fila_fin = (i + 1) * filas_por_hilo;
    }

    hilos.emplace_back(multiplicarRangoMatrices, fila_inicio, fila_fin,
                       cref(A_part), cref(Omega), ref(Y_local));
  }

  for (auto &h : hilos) {
    if (h.joinable()) {
      h.join();
    }
  }

  cout << "Y_local calculada: " << Y_local.size() << " × " << Y_local[0].size()
       << endl;

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

  // b | tam_id(3 bytes) | id | rows(8 bytes) | columns(8 bytes) | tam_data(10
  // bytes) | payload
  string proto =
      "b" + padNumber(my_id.size(), 3) + my_id + padNumber(B_local.size(), 8) +
      padNumber(B_local[0].size(), 8) + padNumber(b_data.size(), 10) + b_data;

  write(socketConn, proto.c_str(), proto.size());

  cout << "B_local enviada" << endl;
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
  int num_hilos = thread::hardware_concurrency();
  if (num_hilos == 0) {
    num_hilos = 4; // Valor por defecto
  }

  int filas_por_hilo = filas_Qt / num_hilos;
  vector<thread> hilos;

  cout << "  Iniciando con " << num_hilos << " hilos para B_local..." << endl;

  for (int i = 0; i < num_hilos; i++) {
    int fila_inicio = i * filas_por_hilo;
    int fila_fin;

    if (i == num_hilos - 1) {
      fila_fin = filas_Qt;
    } else {
      fila_fin = (i + 1) * filas_por_hilo;
    }

    hilos.emplace_back(multiplicarRangoMatrices, fila_inicio, fila_fin,
                       cref(Qt_part), cref(A_part), ref(B_local));
  }

  for (auto &h : hilos) {
    if (h.joinable()) {
      h.join();
    }
  }

  cout << "  B_local calculada: " << B_local.size() << " × "
       << B_local[0].size() << endl;

  B_local_calculada = true;

  enviarBLocal(socketConn);
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

vector<vector<double>> gramSchmidt(const vector<vector<double>> &Y) {
  cout << "\n=== CALCULANDO GRAM-SCHMIDT ===" << endl;

  int m = Y.size();    // filas
  int l = Y[0].size(); // columnas

  cout << "  Input Y: " << m << " × " << l << endl;

  vector<vector<double>> Q_cols(l);
  for (int j = 0; j < l; j++) {
    Q_cols[j].resize(m);
    for (int i = 0; i < m; i++) {
      Q_cols[j][i] = Y[i][j];
    }
  }

  for (int j = 0; j < l; j++) {
    for (int i = 0; i < j; i++) {
      double dot = 0.0;
      for (int k = 0; k < m; k++) {
        dot += Q_cols[i][k] * Q_cols[j][k];
      }

      for (int k = 0; k < m; k++) {
        Q_cols[j][k] -= dot * Q_cols[i][k];
      }
    }

    double norm = 0.0;
    for (int k = 0; k < m; k++) {
      norm += Q_cols[j][k] * Q_cols[j][k];
    }
    norm = sqrt(norm);

    if (norm > 1e-10) {
      for (int k = 0; k < m; k++) {
        Q_cols[j][k] /= norm;
      }
    } else {
      cout << "  ADVERTENCIA: Columna " << j
           << " es linealmente dependiente (norma=" << norm << ")" << endl;

      for (int k = 0; k < m; k++) {
        Q_cols[j][k] = (k == j % m) ? 1.0 : 0.0;
      }

      for (int i = 0; i < j; i++) {
        double dot = 0.0;
        for (int k = 0; k < m; k++) {
          dot += Q_cols[i][k] * Q_cols[j][k];
        }
        for (int k = 0; k < m; k++) {
          Q_cols[j][k] -= dot * Q_cols[i][k];
        }
      }

      norm = 0.0;
      for (int k = 0; k < m; k++) {
        norm += Q_cols[j][k] * Q_cols[j][k];
      }
      norm = sqrt(norm);

      if (norm > 1e-10) {
        for (int k = 0; k < m; k++) {
          Q_cols[j][k] /= norm;
        }
      }
    }

    if (l > 10 && j % max(1, l / 10) == 0) {
      float progreso = (float)(j + 1) / l * 100.0f;
      cout << "  Progreso: " << (int)progreso << "%" << endl;
    }
  }

  vector<vector<double>> Q_filas(m, vector<double>(l));
  for (int i = 0; i < m; i++) {
    for (int j = 0; j < l; j++) {
      Q_filas[i][j] = Q_cols[j][i];
    }
  }

  cout << "  Q calculada: " << Q_filas.size() << " × " << Q_filas[0].size()
       << endl;

  cout << "  Verificando ortogonalidad..." << endl;
  // double max_error = 0.0;
  // for (int i = 0; i < l; i++) {
  //   for (int j = i; j < l; j++) {
  //     double dot = 0.0;
  //     for (int k = 0; k < m; k++) {
  //       dot += Q_cols[i][k] * Q_cols[j][k];
  //     }
  //     double expected = (i == j) ? 1.0 : 0.0;
  //     double error = abs(dot - expected);
  //     max_error = max(max_error, error);
  //   }
  // }
  // cout << "  Error máximo de ortogonalidad: " << max_error << endl;

  return Q_filas;
}

void calcularQLocal(int socketConn) {
  cout << "\n=== CALCULANDO Q USANDO GRAM-SCHMIDT ===" << endl;

  if (!Y_completa_recibida || Y_completa.empty()) {
    cerr << "ERROR: Y_completa no recibida" << endl;
    return;
  }

  cout << "  Y_completa: " << Y_completa.size() << " × " << Y_completa[0].size()
       << endl;

  Q_local = gramSchmidt(Y_completa);
  // Q_local = transponer(Q_local);

  cout << "  Q_local transpuesta calculada: " << Q_local.size() << " × "
       << Q_local[0].size() << endl;

  Q_local_calculada = true;

  enviarQPart(socketConn);
}

void enviarQPart(int socketConn) {
  if (!Q_local_calculada || Q_local.empty()) {
    cerr << "ERROR: Q_local no está lista para enviar" << endl;
    return;
  }

  if (Q_local.empty() || Q_local[0].empty()) {
    cerr << "ERROR: Q_local vacía o inválida" << endl;
    return;
  }

  cout << "\n=== ENVIANDO Q_local AL SERVIDOR ===" << endl;
  cout << "  Dimensiones: " << Q_local.size() << " × " << Q_local[0].size()
       << endl;

  stringstream ss;
  for (const auto &fila : Q_local) {
    for (size_t j = 0; j < fila.size(); j++) {
      ss << fila[j];
      if (j < fila.size() - 1)
        ss << ",";
    }
    ss << "\n";
  }
  string q_data = ss.str();

  // Q | tam_id(3 bytes) | id | rows(8 bytes) | columns(8 bytes) | tam_data(10
  // bytes) | payload
  string proto =
      "Q" + padNumber(my_id.size(), 3) + my_id + padNumber(Q_local.size(), 8) +
      padNumber(Q_local[0].size(), 8) + padNumber(q_data.size(), 10) + q_data;

  write(socketConn, proto.c_str(), proto.size());
  cout << "Q_local enviada" << endl;
}

vector<vector<double>> transponerMatriz(const vector<vector<double>> &mat) {
  if (mat.empty() || mat[0].empty())
    return {};
  int filas = mat.size();
  int cols = mat[0].size();
  vector<vector<double>> transpuesta(cols, vector<double>(filas));
  for (int i = 0; i < filas; i++) {
    for (int j = 0; j < cols; j++) {
      transpuesta[j][i] = mat[i][j];
    }
  }
  return transpuesta;
}

vector<vector<double>> multiplicarMatrices(const vector<vector<double>> &A,
                                           const vector<vector<double>> &B) {
  if (A.empty() || B.empty() || A[0].size() != B.size()) {
    cerr << "ERROR: No se pueden multiplicar matrices" << endl;
    return {};
  }

  int filas_A = A.size();
  int cols_A = A[0].size();
  int cols_B = B[0].size();

  vector<vector<double>> C(filas_A, vector<double>(cols_B, 0.0));

  int num_hilos = thread::hardware_concurrency();
  if (num_hilos == 0) {
    num_hilos = 4;
  }

  if (filas_A < num_hilos * 4) {
    num_hilos = 1;
  }

  int filas_por_hilo = filas_A / num_hilos;
  vector<thread> hilos;

  for (int i = 0; i < num_hilos; i++) {
    int fila_inicio = i * filas_por_hilo;
    int fila_fin;

    if (i == num_hilos - 1) {
      fila_fin = filas_A;
    } else {
      fila_fin = (i + 1) * filas_por_hilo;
    }

    hilos.emplace_back(multiplicarRangoMatrices, fila_inicio, fila_fin, cref(A),
                       cref(B), ref(C));
  }

  for (auto &h : hilos) {
    if (h.joinable()) {
      h.join();
    }
  }

  return C;
}

void calcularBtBContribucion(int socketConn) {
  cout << "\n=== CALCULANDO CONTRIBUCIÓN A BtB ===" << endl;

  if (B_part.empty() || B_part[0].empty()) {
    cerr << "ERROR: B_part no recibida o vacía" << endl;
    cerr << "  B_part_recibida = " << (B_part_recibida ? "true" : "false")
         << endl;
    cerr << "  B_part.size() = " << B_part.size() << endl;
    return;
  }

  int filas_B = B_part.size();
  int cols_B = B_part[0].size();

  cout << "  B_part: " << filas_B << " × " << cols_B << endl;

  // BtB_contrib = B_part^T × B_part
  vector<vector<double>> Bt = transponerMatriz(B_part);
  BtB_contrib = multiplicarMatrices(Bt, B_part);

  cout << "  BtB_contrib calculada: " << BtB_contrib.size() << " × "
       << BtB_contrib[0].size() << endl;

  BtB_contrib_calculada = true;
  enviarBtBContribucion(socketConn);
}

void enviarBtBContribucion(int socketConn) {
  if (!BtB_contrib_calculada || BtB_contrib.empty()) {
    cerr << "ERROR: BtB_contrib no está lista para enviar" << endl;
    return;
  }

  cout << "\n=== ENVIANDO BtB_contrib AL SERVIDOR ===" << endl;

  stringstream ss;
  for (const auto &fila : BtB_contrib) {
    for (size_t j = 0; j < fila.size(); j++) {
      ss << fila[j];
      if (j < fila.size() - 1)
        ss << ",";
    }
    ss << "\n";
  }
  string data = ss.str();

  cout << "  Dimensiones: " << BtB_contrib.size() << " × "
       << BtB_contrib[0].size() << endl;

  // T | tam_id(3 bytes) | id | rows(8 bytes) | columns(8 bytes) | tam_data(10
  // bytes) | payload
  string proto = "T" + padNumber(my_id.size(), 3) + my_id +
                 padNumber(BtB_contrib.size(), 8) +
                 padNumber(BtB_contrib[0].size(), 8) +
                 padNumber(data.size(), 10) + data;

  write(socketConn, proto.c_str(), proto.size());
  cout << "BtB_contrib enviada" << endl;
}

void qr_eigen_svd(const vector<vector<double>> &A_input,
                  vector<double> &eigenvalues,
                  vector<vector<double>> &eigenvectors) {
  int n = A_input.size();
  vector<vector<double>> A = A_input;
  eigenvectors = vector<vector<double>>(n, vector<double>(n, 0.0));
  for (int i = 0; i < n; ++i)
    eigenvectors[i][i] = 1.0;

  const int max_iter = 1000;
  const double tol = 1e-12;

  for (int p = n - 1; p > 0; --p) {
    for (int iter = 0; iter < max_iter; ++iter) {
      double d = (A[p - 1][p - 1] - A[p][p]) * 0.5;
      double s = A[p][p] + d -
                 (d < 0 ? -1 : 1) * sqrt(d * d + A[p][p - 1] * A[p][p - 1]);
      double shift =
          abs(s - A[p][p]) < abs(A[p - 1][p - 1] - A[p][p]) ? s : A[p][p];

      for (int i = 0; i <= p; ++i)
        A[i][i] -= shift;

      vector<vector<double>> Q_accumulated =
          vector<vector<double>>(n, vector<double>(n, 0.0));
      for (int i = 0; i < n; ++i)
        Q_accumulated[i][i] = 1.0;

      for (int i = 0; i < p; ++i) {
        double r = sqrt(A[i][i] * A[i][i] + A[i + 1][i] * A[i + 1][i]);
        if (r < tol)
          continue;
        double c = A[i][i] / r;
        double s_g = A[i + 1][i] / r;

        for (int j = 0; j <= p; ++j) {
          double t1 = A[i][j];
          double t2 = A[i + 1][j];
          A[i][j] = c * t1 + s_g * t2;
          A[i + 1][j] = -s_g * t1 + c * t2;
        }

        for (int j = 0; j <= p; ++j) {
          double t1 = A[j][i];
          double t2 = A[j][i + 1];
          A[j][i] = c * t1 + s_g * t2;
          A[j][i + 1] = -s_g * t1 + c * t2;
        }

        for (int j = 0; j < n; ++j) {
          double t1 = eigenvectors[j][i];
          double t2 = eigenvectors[j][i + 1];
          eigenvectors[j][i] = c * t1 + s_g * t2;
          eigenvectors[j][i + 1] = -s_g * t1 + c * t2;
        }
      }

      for (int i = 0; i <= p; ++i)
        A[i][i] += shift;

      if (abs(A[p][p - 1]) < tol * (abs(A[p - 1][p - 1]) + abs(A[p][p]))) {
        break;
      }
    }
  }

  eigenvalues.resize(n);
  for (int i = 0; i < n; ++i) {
    eigenvalues[i] = A[i][i];
  }
}

void calcularSVDdeBtB(int socketConn) {
  cout << "\n=== CALCULANDO SVD DE BtB ===" << endl;

  if (BtB_completa.empty()) {
    cerr << "ERROR: BtB_completa vacía" << endl;
    return;
  }

  int n = BtB_completa.size();

  cout << "  BtB: " << n << " × " << n << endl;

  // Calcular autovalores y autovectores de BtB
  vector<double> eigenvals;
  vector<vector<double>> V;
  qr_eigen_svd(BtB_completa, eigenvals, V);

  // Valores singulares
  vector<double> sigma(n);
  for (int i = 0; i < n; ++i) {
    sigma[i] = sqrt(max(0.0, eigenvals[i]));
  }

  // Ordenar de mayor a menor
  vector<pair<double, int>> orden;
  for (int i = 0; i < n; ++i)
    orden.emplace_back(sigma[i], i);
  sort(orden.rbegin(), orden.rend());

  vector<vector<double>> Vorden(n, vector<double>(n));
  S_svd.resize(n);
  for (int i = 0; i < n; ++i) {
    int j = orden[i].second;
    S_svd[i] = orden[i].first;
    for (int k = 0; k < n; ++k)
      Vorden[k][i] = V[k][j];
  }

  V_svd = transponerMatriz(Vorden);

  cout << "  SVD calculado:" << endl;
  cout << "    V: " << V_svd.size() << " × " << V_svd[0].size() << endl;
  cout << "    S: " << S_svd.size() << " valores singulares" << endl;
  if (!S_svd.empty()) {
    cout << "    σ_max = " << S_svd[0] << ", σ_min = " << S_svd[n - 1] << endl;
  }

  // Enviar V y S al servidor
  enviarVyS(socketConn);
}

void enviarVyS(int socketConn) {
  cout << "\n=== ENVIANDO V Y S AL SERVIDOR ===" << endl;

  if (V_svd.empty() || S_svd.empty()) {
    cerr << "ERROR: V o S no calculados" << endl;
    return;
  }

  if (V_svd.empty() || V_svd[0].empty()) {
    cerr << "ERROR: V_svd vacía o inválida" << endl;
    return;
  }

  stringstream ss_v;
  for (const auto &fila : V_svd) {
    for (size_t j = 0; j < fila.size(); j++) {
      ss_v << fila[j];
      if (j < fila.size() - 1)
        ss_v << ",";
    }
    ss_v << "\n";
  }
  string v_data = ss_v.str();

  // V | tam_id(3 bytes) | id | rows(8 bytes) | columns(8 bytes) | tam_data(10
  // bytes) | payload
  string proto_v = "V" + padNumber(my_id.size(), 3) + my_id +
                   padNumber(V_svd.size(), 8) + padNumber(V_svd[0].size(), 8) +
                   padNumber(v_data.size(), 10) + v_data;

  write(socketConn, proto_v.c_str(), proto_v.size());
  cout << "  V enviada: " << V_svd.size() << " × " << V_svd[0].size() << endl;

  // Enviar S
  stringstream ss_s;
  for (size_t i = 0; i < S_svd.size(); i++) {
    ss_s << S_svd[i];
    if (i < S_svd.size() - 1)
      ss_s << ",";
  }
  ss_s << "\n";
  string s_data = ss_s.str();

  // s | tam_id(3 bytes) | id | length(8 bytes) | tam_data(10 bytes) | payload
  string proto_s = "s" + padNumber(my_id.size(), 3) + my_id +
                   padNumber(S_svd.size(), 8) + padNumber(s_data.size(), 10) +
                   s_data;

  write(socketConn, proto_s.c_str(), proto_s.size());
  cout << "  S enviada: " << S_svd.size() << " valores singulares" << endl;
}

void calcularUPart(int socketConn) {
  cout << "\n=== CALCULANDO U_part = B_part × V × S^(-1) ===" << endl;

  if (B_part.empty()) {
    cerr << "ERROR: B_part no recibida" << endl;
    return;
  }
  if (V_svd.empty()) {
    cerr << "ERROR: V no recibida" << endl;
    return;
  }
  if (S_svd.empty()) {
    cerr << "ERROR: S no recibida" << endl;
    return;
  }

  int filas_B = B_part.size();
  int cols_B = B_part[0].size();
  int cols_V = V_svd[0].size();
  int rank = min((int)S_svd.size(), cols_V);

  cout << "  B_part: " << filas_B << " × " << cols_B << endl;
  cout << "  V: " << V_svd.size() << " × " << cols_V << endl;
  cout << "  S: " << rank << " valores singulares" << endl;

  // Calcular B_part × V
  vector<vector<double>> BV = multiplicarMatrices(B_part, V_svd);

  // Aplicar S^(-1) (dividir cada columna por su valor singular)
  U_part.resize(filas_B, vector<double>(rank, 0.0));
  for (int i = 0; i < filas_B; i++) {
    for (int j = 0; j < rank; j++) {
      if (S_svd[j] > 1e-12) {
        U_part[i][j] = BV[i][j] / S_svd[j];
      } else {
        U_part[i][j] = 0.0;
      }
    }
  }

  cout << "  U_part calculada: " << U_part.size() << " × " << U_part[0].size()
       << endl;

  U_part_calculada = true;
  enviarUPart(socketConn);
}

void enviarUPart(int socketConn) {
  if (!U_part_calculada || U_part.empty()) {
    cerr << "ERROR: U_part no está lista para enviar" << endl;
    return;
  }

  cout << "\n=== ENVIANDO U_part AL SERVIDOR ===" << endl;

  stringstream ss;
  for (const auto &fila : U_part) {
    for (size_t j = 0; j < fila.size(); j++) {
      ss << fila[j];
      if (j < fila.size() - 1)
        ss << ",";
    }
    ss << "\n";
  }
  string data = ss.str();

  cout << "  Dimensiones: " << U_part.size() << " × " << U_part[0].size()
       << endl;

  // U | tam_id(3 bytes) | id | rows(8 bytes) | columns(8 bytes) | tam_data(10
  // bytes) | payload
  string proto = "U" + padNumber(my_id.size(), 3) + my_id +
                 padNumber(U_part.size(), 8) + padNumber(U_part[0].size(), 8) +
                 padNumber(data.size(), 10) + data;

  write(socketConn, proto.c_str(), proto.size());
  cout << "U_part enviada" << endl;
}

vector<vector<double>> parseMatrixFromFile(const string &filename) {

  vector<vector<double>> matriz;
  ifstream file(filename);

  if (!file.is_open()) {
    cerr << "ERROR: No se pudo abrir " << filename << endl;
    return matriz;
  }

  string linea;

  while (getline(file, linea)) {
    vector<double> fila;
    stringstream ss(linea);
    string valor_str;

    while (getline(ss, valor_str, ',')) {
      fila.push_back(stod(valor_str));
    }

    matriz.push_back(fila);
  }

  file.close();

  cout << "Matriz parseada: " << matriz.size() << " x "
       << (matriz.empty() ? 0 : matriz[0].size()) << endl;

  return matriz;
}

void imprimirMatriz(const vector<vector<double>> &mat, const string &nombre) {
  cout << "\n" << nombre << " = " << endl;
  for (const auto &fila : mat) {
    for (double val : fila) {
      cout << val << "\t";
    }
    cout << endl;
  }
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
  cout << "  Memoria usada: " << memoria_mb << " MB" << endl;
  // imprimirMatriz(Omega, "Matriz Omega: ");
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

      } else if (filename.find("Y_completa.csv") == 0 ||
                 filename == "Y_completa.csv") {
        cout << "\n=== RECIBIDA Y COMPLETA PARA CALCULAR Q ===" << endl;

        Y_completa = parseMatrixFromFile(filename);
        if (Y_completa.empty() || Y_completa[0].empty()) {
          cerr << "ERROR: Y_completa vacía o inválida" << endl;
          break;
        }
        Y_completa_recibida = true;

        cout << "  Y_completa: " << Y_completa.size() << " × "
             << Y_completa[0].size() << endl;

        // Calcular Q usando Gram-Schmidt
        calcularQLocal(socketConn);
      } else if (filename.find("B_part_") == 0) {
        cout << "\n=== RECIBIDA MATRIZ B_part PARA SVD ===" << endl;

        usleep(10000);

        B_part = parseMatrixFromFile(filename);

        if (B_part.empty() || B_part[0].empty()) {
          cerr << "ERROR: B_part vacía o inválida después de parsear "
               << filename << endl;
          cerr << "  Intentando leer el archivo nuevamente..." << endl;
          usleep(50000);
          B_part = parseMatrixFromFile(filename);
          if (B_part.empty() || B_part[0].empty()) {
            cerr << "ERROR: B_part sigue vacía después de reintento" << endl;
            break;
          }
        }

        B_part_recibida = true;
        cout << "  B_part parseada correctamente: " << B_part.size() << " × "
             << B_part[0].size() << endl;

        // Calcular contribución a BtB
        calcularBtBContribucion(socketConn);
      } else if (filename.find("BtB_completa") == 0) {
        cout << "\n=== RECIBIDA BtB_COMPLETA PARA CALCULAR SVD ===" << endl;

        BtB_completa = parseMatrixFromFile(filename);

        // Calcular SVD de BtB
        calcularSVDdeBtB(socketConn);
      } else if (filename.find("V_svd_") == 0) {
        cout << "\n=== RECIBIDA MATRIZ V DEL SVD ===" << endl;

        V_svd = parseMatrixFromFile(filename);
        V_recibida = true;

        // Si también tenemos S, calcular U
        if (S_recibida) {
          calcularUPart(socketConn);
        } else {
          cout << "  Esperando valores singulares S..." << endl;
        }
      } else if (filename.find("S_svd_") == 0) {
        cout << "\n=== RECIBIDOS VALORES SINGULARES S ===" << endl;

        // Leer S como vector desde archivo CSV
        ifstream file(filename);
        string line;
        if (getline(file, line)) {
          stringstream ss(line);
          string val;
          S_svd.clear();
          while (getline(ss, val, ',')) {
            S_svd.push_back(stod(val));
          }
        }
        file.close();
        S_recibida = true;

        // Si también tenemos V, calcular U
        if (V_recibida) {
          calcularUPart(socketConn);
        } else {
          cout << "  Esperando matriz V..." << endl;
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

      cout << "PROTOCOLO: " << type << "|" << n_matrix << "|" << k_matrix << "|"
           << seed_value << endl;

      cout << "\nGenerando matriz Omega" << endl;
      generarMatrizOmega();
      cout << "=== Matriz Omega Lista ===" << endl;
      // o | tam_id(3 bytes) | id
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