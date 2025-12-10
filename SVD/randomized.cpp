#include <cmath>
#include <iostream>
#include <vector>

using namespace std;

vector<vector<double>> A = {
    {23, 45, 67},
    {42, 84, 73},
    {17, 63, 94},
};

vector<vector<double>> omega = {{0.156047, -0.966728, 1.5537, 0.15112},
                                {0.0370743, -1.82948, 1.08994, 1.06901},
                                {0.476882, -1.02592, 0.530486, 0.613809}};

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

  cout << "  ✓ Q calculada: " << Q_filas.size() << " × " << Q_filas[0].size()
       << endl;

  cout << "  Verificando ortogonalidad..." << endl;
  double max_error = 0.0;
  for (int i = 0; i < l; i++) {
    for (int j = i; j < l; j++) {
      double dot = 0.0;
      for (int k = 0; k < m; k++) {
        dot += Q_cols[i][k] * Q_cols[j][k];
      }
      double expected = (i == j) ? 1.0 : 0.0;
      double error = abs(dot - expected);
      max_error = max(max_error, error);
    }
  }
  cout << "  Error máximo de ortogonalidad: " << max_error << endl;

  return Q_filas;
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

vector<double> multiplicarMatrizPorVector(const vector<vector<double>> &mat,
                                          const vector<double> &vec) {
  int m = mat.size();
  int n = mat[0].size();
  vector<double> resultado(m, 0.0);

  for (int i = 0; i < m; i++) {
    for (int j = 0; j < n; j++) {
      resultado[i] += mat[i][j] * vec[j];
    }
  }
  return resultado;
}

vector<double> metodoPotencia(const vector<vector<double>> &mat,
                              int max_iter = 100) {
  int n = mat.size();
  vector<double> x(n, 1.0); // Vector inicial

  for (int iter = 0; iter < max_iter; iter++) {
    // y = M * x
    vector<double> y = multiplicarMatrizPorVector(mat, x);

    // Normalizar y
    double norma = 0.0;
    for (double val : y)
      norma += val * val;
    norma = sqrt(norma);

    if (norma < 1e-12)
      break;

    for (int i = 0; i < n; i++) {
      x[i] = y[i] / norma;
    }
  }
  return x;
}

// Encontrar k autovectores principales usando deflación
vector<vector<double>> encontrarAutovectores(const vector<vector<double>> &mat,
                                             int k) {
  int n = mat.size();
  vector<vector<double>> autovectores;
  vector<double> autovalores;

  vector<vector<double>> M = mat; // Copia para deflación

  for (int i = 0; i < k; i++) {
    // Encontrar autovector dominante
    vector<double> v = metodoPotencia(M);

    // Calcular autovalor correspondiente
    vector<double> Av = multiplicarMatrizPorVector(mat, v);
    double lambda = productoPunto(v, Av);

    autovectores.push_back(v);
    autovalores.push_back(lambda);

    // Deflación: M = M - λ * v * v^T
    for (int row = 0; row < n; row++) {
      for (int col = 0; col < n; col++) {
        M[row][col] -= lambda * v[row] * v[col];
      }
    }
  }

  // Ordenar por autovalor descendente
  for (int i = 0; i < k; i++) {
    for (int j = i + 1; j < k; j++) {
      if (autovalores[i] < autovalores[j]) {
        swap(autovalores[i], autovalores[j]);
        swap(autovectores[i], autovectores[j]);
      }
    }
  }

  return autovectores;
}

// Calcular valores singulares a partir de B y V
vector<vector<double>> calcularU(const vector<vector<double>> &B,
                                 const vector<vector<double>> &V,
                                 vector<double> &sigma) {
  int l = B.size();
  int n = B[0].size();
  int k = V.size();

  vector<vector<double>> U(l, vector<double>(k, 0.0));

  for (int i = 0; i < k; i++) {
    // Calcular u_i = B * v_i / σ_i
    vector<double> Bv(l, 0.0);
    for (int row = 0; row < l; row++) {
      for (int col = 0; col < n; col++) {
        Bv[row] += B[row][col] * V[i][col];
      }
    }

    sigma[i] = sqrt(sigma[i]); // σ_i = √(λ_i)

    for (int j = 0; j < l; j++) {
      U[j][i] = Bv[j] / sigma[i];
    }
  }

  return U;
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
  imprimirMatriz(B, "Matriz B: ");
  vector<vector<double>> BtB = multiplicarMatriz(matrixTranspuesta(B), B);

  cout << "\n--- PASO 6: Encontrar autovectores de B^T * B ---" << endl;
  vector<vector<double>> V_vectores = encontrarAutovectores(BtB, k);

  // Convertir vector de vectores a matriz (cada autovector es una fila)
  int n = BtB.size();
  vector<vector<double>> V(k, vector<double>(n, 0.0));
  for (int i = 0; i < k; i++) {
    for (int j = 0; j < n; j++) {
      V[i][j] = V_vectores[i][j];
    }
  }
  imprimirMatriz(V, "V (autovectores, cada fila es un autovector)");

  // Paso 7: Calcular valores singulares y matriz U
  cout << "\n--- PASO 7: Calcular valores singulares y matriz U ---" << endl;

  // Primero calcular autovalores aproximados
  vector<double> sigma_sq(k, 0.0); // σ² (autovalores)
  for (int i = 0; i < k; i++) {
    vector<double> BtB_v = multiplicarMatrizPorVector(BtB, V[i]);
    sigma_sq[i] = productoPunto(V[i], BtB_v);
  }

  // Calcular U
  vector<double> sigma = sigma_sq; // Copiar para pasar por referencia
  vector<vector<double>> U = calcularU(B, V, sigma);
  imprimirMatriz(U, "U");

  // Transponer V para tener columnas como autovectores
  vector<vector<double>> V_T = matrixTranspuesta(V);

  // Paso 8: Construir matrices finales del SVD
  cout << "\n--- PASO 8: SVD FINAL ---" << endl;

  // Matriz Σ (diagonal con valores singulares)
  vector<vector<double>> Sigma(3, vector<double>(3, 0.0));
  for (int i = 0; i < k; i++) {
    Sigma[i][i] = sigma[i];
  }

  // U_final = Q * U
  vector<vector<double>> U_final = multiplicarMatriz(Q, U);

  cout << "\nValores singulares (sigma):" << endl;
  for (int i = 0; i < k; i++) {
    cout << "σ" << i + 1 << " = " << sigma[i] << endl;
  }

  imprimirMatriz(U_final, "U (matriz de vectores singulares izquierdos)");
  imprimirMatriz(V_T, "V^T (matriz de vectores singulares derechos)");
  imprimirMatriz(Sigma, "Σ (matriz de valores singulares)");

  // Verificación: Reconstruir A ≈ U * Σ * V^T
  cout << "\n--- Verificación: Reconstrucción A ≈ U * Σ * V^T ---" << endl;
  vector<vector<double>> Sigma_VT = multiplicarMatriz(Sigma, V_T);
  vector<vector<double>> A_reconstruida = multiplicarMatriz(U_final, Sigma_VT);
  imprimirMatriz(A_reconstruida, "A reconstruida");

  cout << "\n===== SVD COMPLETADO =====" << endl;

  return 0;

  return 0;
}