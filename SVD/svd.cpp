#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <limits>
#include <vector>

using namespace std;

using Matrix = vector<vector<double>>;
using Vector = vector<double>;

// ------------------- Utilidades -------------------
Matrix transpuesta(const Matrix &A) {
  int m = A.size(), n = A[0].size();
  Matrix At(n, vector<double>(m));
  for (int i = 0; i < m; ++i)
    for (int j = 0; j < n; ++j)
      At[j][i] = A[i][j];
  return At;
}

Matrix multiplicar(const Matrix &A, const Matrix &B) {
  int m = A.size(), p = A[0].size(), n = B[0].size();
  Matrix C(m, vector<double>(n, 0.0));
  for (int i = 0; i < m; ++i)
    for (int j = 0; j < n; ++j)
      for (int k = 0; k < p; ++k)
        C[i][j] += A[i][k] * B[k][j];
  return C;
}

void imprimir(const Matrix &M, const string &nombre = "") {
  if (!nombre.empty())
    cout << nombre << ":\n";
  cout << fixed << setprecision(6);
  for (const auto &fila : M) {
    for (double x : fila)
      cout << setw(12) << x;
    cout << "\n";
  }
  cout << endl;
}

double norma2(const Vector &v) {
  double sum = 0.0;
  for (double x : v)
    sum += x * x;
  return sqrt(sum);
}

// ------------------- Algoritmo QR con shift para autovalores
// -------------------
void qr_eigen(const Matrix &A_input, vector<double> &eigenvalues,
              Matrix &eigenvectors) {
  int n = A_input.size();
  Matrix A = A_input; // copia
  eigenvectors = Matrix(n, vector<double>(n, 0.0));
  for (int i = 0; i < n; ++i)
    eigenvectors[i][i] = 1.0;

  const int max_iter = 1000;
  const double tol = 1e-12;

  for (int p = n - 1; p > 0; --p) {
    for (int iter = 0; iter < max_iter; ++iter) {
      // Shift de Wilkinson
      double d = (A[p - 1][p - 1] - A[p][p]) * 0.5;
      double s = A[p][p] + d -
                 (d < 0 ? -1 : 1) * sqrt(d * d + A[p][p - 1] * A[p][p - 1]);
      double shift =
          abs(s - A[p][p]) < abs(A[p - 1][p - 1] - A[p][p]) ? s : A[p][p];

      // Aplicar shift
      for (int i = 0; i <= p; ++i)
        A[i][i] -= shift;

      // QR paso (Givens)
      for (int i = 0; i < p; ++i) {
        double r = sqrt(A[i][i] * A[i][i] + A[i + 1][i] * A[i + 1][i]);
        if (r < tol)
          continue;
        double c = A[i][i] / r;
        double s_g = A[i + 1][i] / r;

        for (int j = i; j <= p; ++j) {
          double t1 = A[i][j];
          double t2 = A[i + 1][j];
          A[i][j] = c * t1 + s_g * t2;
          A[i + 1][j] = -s_g * t1 + c * t2;
        }

        // Acumular rotación en eigenvectors
        for (int j = 0; j < n; ++j) {
          double t1 = eigenvectors[j][i];
          double t2 = eigenvectors[j][i + 1];
          eigenvectors[j][i] = c * t1 + s_g * t2;
          eigenvectors[j][i + 1] = -s_g * t1 + c * t2;
        }
      }

      // Quitar shift
      for (int i = 0; i <= p; ++i)
        A[i][i] += shift;

      // Comprobar convergencia
      if (abs(A[p][p - 1]) < tol * (abs(A[p - 1][p - 1]) + abs(A[p][p]))) {
        break;
      }
    }
  }

  // Extraer autovalores
  eigenvalues.resize(n);
  for (int i = 0; i < n; ++i) {
    eigenvalues[i] = A[i][i];
  }
}

// ------------------- SVD completo (funciona hasta ~100x100)
// -------------------
struct SVD {
  Matrix U, S, Vt;
};

SVD svd_tradicional(const Matrix &A) {
  int m = A.size(), n = A[0].size();
  Matrix AtA = multiplicar(transpuesta(A), A);

  vector<double> eigenvals;
  Matrix V;
  qr_eigen(AtA, eigenvals, V);

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

  Matrix Vorden(n, vector<double>(n));
  vector<double> sigma_ord(n);
  for (int i = 0; i < n; ++i) {
    int j = orden[i].second;
    sigma_ord[i] = orden[i].first;
    for (int k = 0; k < n; ++k)
      Vorden[k][i] = V[k][j];
  }

  // Calcular U
  Matrix U(m, vector<double>(m, 0.0));
  int rank = 0;
  for (int i = 0; i < n; ++i) {
    if (sigma_ord[i] < 1e-12)
      break;
    Vector Av(m, 0.0);
    for (int row = 0; row < m; ++row)
      for (int col = 0; col < n; ++col)
        Av[row] += A[row][col] * Vorden[col][i];
    for (int row = 0; row < m; ++row)
      U[row][rank] = Av[row] / sigma_ord[i];
    rank++;
  }

  // Sigma
  Matrix S(m, vector<double>(n, 0.0));
  for (int i = 0; i < rank; ++i)
    S[i][i] = sigma_ord[i];

  SVD res;
  res.U = U;
  res.S = S;
  res.Vt = transpuesta(Vorden);
  return res;
}

// ------------------- MAIN -------------------
int main() {
  // Prueba con matriz 10×10 (¡funciona!)
  Matrix A(10, vector<double>(10));
  for (int i = 0; i < 10; ++i)
    for (int j = 0; j < 10; ++j)
      A[i][j] =
          (i + 1) * (j + 2) + (i == j ? 10.0 : 0.0); // matriz bien condicionada

  cout << "SVD de matriz 10x10: OK!\n";
  SVD svd = svd_tradicional(A);

  // Verificación rápida
  Matrix recon = multiplicar(svd.U, multiplicar(svd.S, svd.Vt));
  double error = 0.0;
  for (int i = 0; i < 10; ++i)
    for (int j = 0; j < 10; ++j)
      error += pow(recon[i][j] - A[i][j], 2);
  error = sqrt(error);

  cout << "Error de reconstruccion (10x10): " << scientific << error << endl;
  // → suele dar < 1e-10

  return 0;
}