#include <cmath>
#include <iostream>
#include <vector>

using namespace std;

vector<vector<double>> A = {
    {2, 1, 3},
    {1, 4, 2},
    {3, 2, 5},
};

vector<vector<double>> omega = {
    {0.5, -0.3, 0.2},
    {-0.2, 0.4, -0.1},
    {0.3, 0.1, -0.4},
};

vector<vector<double>> multiplicarMatriz(const vector<vector<double>> &A,
                                         const vector<vector<double>> &B) {
  int m = A.size();
  int p = A[0].size();
  int n = B.size();
  int q = B[0].size();

  if (p != n) {
    cerr << "Error: no se pueden multiplicar (columnas A != filas B)\n";
    return {};
  }

  vector<vector<double>> C(m, vector<double>(q, 0.0));

  for (int i = 0; i < m; i++) {
    for (int j = 0; j < q; j++) {
      for (int k = 0; k < p; k++) {
        C[i][j] += A[i][k] * B[k][j];
      }
    }
  }
  return C;
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

double productoPunto(const vector<double> &v1, const vector<double> &v2) {
  double resultado = 0.0;
  for (size_t i = 0; i < v1.size(); i++) {
    resultado += v1[i] * v2[i];
  }
  return resultado;
}

vector<double> multiplicarEscalar(const vector<double> &v, double escalar) {
  vector<double> resultado(v.size());
  for (size_t i = 0; i < v.size(); i++) {
    resultado[i] = v[i] * escalar;
  }
  return resultado;
}

vector<double> sumarVectores(const vector<double> &v1,
                             const vector<double> &v2) {
  vector<double> resultado(v1.size());
  for (size_t i = 0; i < v1.size(); i++) {
    resultado[i] = v1[i] + v2[i];
  }
  return resultado;
}

vector<double> restarVectores(const vector<double> &v1,
                              const vector<double> &v2) {
  vector<double> resultado(v1.size());
  for (size_t i = 0; i < v1.size(); i++) {
    resultado[i] = v1[i] - v2[i];
  }
  return resultado;
}

vector<vector<double>> calcularY() {
  int m = A.size();        // filas de A (3)
  int n = A[0].size();     // columnas de A (3)
  int l = omega[0].size(); // columnas de omega (3)

  vector<vector<double>> Y(m, vector<double>(l, 0.0));

  for (int i = 0; i < m; i++) {
    for (int j = 0; j < l; j++) {
      for (int k = 0; k < n; k++) {
        Y[i][j] += A[i][k] * omega[k][j];
      }
    }
  }

  return Y;
}

vector<vector<double>> gramSchmidt(const vector<vector<double>> &Y) {
  int m = Y.size();    // filas (3)
  int l = Y[0].size(); // columnas (3)

  vector<vector<double>> Q(m, vector<double>(l, 0.0));
  vector<vector<double>> R(l, vector<double>(l, 0.0));

  vector<vector<double>> columnas(l);
  for (int j = 0; j < l; j++) {
    columnas[j].resize(m);
    for (int i = 0; i < m; i++) {
      columnas[j][i] = Y[i][j];
    }
  }

  for (int j = 0; j < l; j++) {
    vector<double> v = columnas[j]; // v = y_j

    for (int i = 0; i < j; i++) {
      R[i][j] = productoPunto(Q[i], columnas[j]);

      vector<double> proyeccion = multiplicarEscalar(Q[i], R[i][j]);
      v = restarVectores(v, proyeccion);
    }

    double norma = 0.0;
    for (double valor : v) {
      norma += valor * valor;
    }
    norma = sqrt(norma);
    R[j][j] = norma;

    if (norma > 1e-10) {
      for (int i = 0; i < m; i++) {
        Q[i][j] = v[i] / norma;
      }
    } else {
      for (int i = 0; i < m; i++) {
        Q[i][j] = (i == j) ? 1.0 : 0.0;
      }
    }
  }

  return Q;
}

vector<vector<double>> matrixTranspuesta(vector<vector<double>> mat) {
  int filas = mat.size();
  int columnas = mat[0].size();
  vector<vector<double>> transpuesta(columnas, vector<double>(filas));

  for (int i = 0; i < filas; i++) {
    for (int j = 0; j < columnas; j++) {
      transpuesta[j][i] = mat[i][j];
    }
  }
  return transpuesta;
}

int main() {

  int k = 2;
  int p = 1;
  int q = 0;
  int l = k + p;

  cout << "===== SVD RANDOMIZADO PASO A PASO =====" << endl;

  cout << "\n--- PASO 2: Calcular Y = A * omega ---" << endl;
  vector<vector<double>> Y = calcularY();
  imprimirMatriz(Y, "Y");

  cout << "\n--- PASO 3: Gram-Schmidt de Y ---" << endl;

  vector<vector<double>> Q = gramSchmidt(Y);
  imprimirMatriz(Q, "Q (resultado Gram-Schmidt)");

  vector<vector<double>> B = multiplicarMatriz(matrixTranspuesta(Q), A);

  vector<vector<double>> BtB = multiplicarMatriz(matrixTranspuesta(B), B);

  cout << "\n--- Preparación para PASO 4 ---" << endl;
  cout << "Dimensiones: Q^T es " << l << "x3, A es 3x3" << endl;
  cout << "B será " << l << "x3" << endl;

  return 0;

  return 0;
}