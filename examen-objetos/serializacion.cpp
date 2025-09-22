#include <cstdint> // ¡IMPORTANTE! Para uint32_t
#include <cstring>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

// ===============================
// DEFINICIÓN DE LAS CLASES ORIGINALES (SIN MODIFICAR)
// ===============================

class Sillon {
public:
  int numeroCojines;
  std::string material;
  float peso;
  std::string color;

  Sillon(int cojines = 0, const std::string &mat = "", float p = 0.0f,
         const std::string &col = "")
      : numeroCojines(cojines), material(mat), peso(p), color(col) {}

  void mostrar() const {
    std::cout << "  Sillón: " << material << ", " << numeroCojines
              << " cojines, " << peso << " kg, color " << color << std::endl;
  }
};

class Mesa {
public:
  int numeroPatas;
  std::string forma;
  float area;
  std::string material;

  Mesa(int patas = 0, const std::string &form = "", float a = 0.0f,
       const std::string &mat = "")
      : numeroPatas(patas), forma(form), area(a), material(mat) {}

  void mostrar() const {
    std::cout << "  Mesa: " << forma << ", " << numeroPatas << " patas, "
              << area << " m², " << material << std::endl;
  }
};

class Sala {
public:
  std::string nombre;
  float areaTotal;
  std::string tipo;
  std::vector<Sillon> sillones;
  std::vector<Mesa> mesas;

  Sala(const std::string &nom = "", float area = 0.0f,
       const std::string &tip = "")
      : nombre(nom), areaTotal(area), tipo(tip) {}

  void mostrar() const {
    std::cout << "\n=== SALA: " << nombre << " ===" << std::endl;
    std::cout << "Área total: " << areaTotal << " m²" << std::endl;
    std::cout << "Tipo: " << tipo << std::endl;
    std::cout << "Sillones (" << sillones.size() << "):" << std::endl;
    for (const auto &sillon : sillones) {
      sillon.mostrar();
    }
    std::cout << "Mesas (" << mesas.size() << "):" << std::endl;
    for (const auto &mesa : mesas) {
      mesa.mostrar();
    }
  }
};

// ===============================
// SISTEMA DE SERIALIZACIÓN EXTERNO
// ===============================

// Macros para simplificar la serialización
#define SERIALIZAR_INT(datos, valor)                                           \
  datos.append(reinterpret_cast<const char *>(&valor), sizeof(int))

#define SERIALIZAR_FLOAT(datos, valor)                                         \
  datos.append(reinterpret_cast<const char *>(&valor), sizeof(float))

#define SERIALIZAR_STRING(datos, valor)                                        \
  do {                                                                         \
    uint32_t longitud = valor.size();                                          \
    datos.append(reinterpret_cast<const char *>(&longitud), sizeof(uint32_t)); \
    datos.append(valor);                                                       \
  } while (0)

#define DESERIALIZAR_INT(datos, pos, valor)                                    \
  std::memcpy(&valor, datos.data() + pos, sizeof(int));                        \
  pos += sizeof(int)

#define DESERIALIZAR_FLOAT(datos, pos, valor)                                  \
  std::memcpy(&valor, datos.data() + pos, sizeof(float));                      \
  pos += sizeof(float)

#define DESERIALIZAR_STRING(datos, pos, valor)                                 \
  do {                                                                         \
    uint32_t longitud;                                                         \
    std::memcpy(&longitud, datos.data() + pos, sizeof(uint32_t));              \
    pos += sizeof(uint32_t);                                                   \
    valor.assign(datos.data() + pos, longitud);                                \
    pos += longitud;                                                           \
  } while (0)

// Template principal de serialización externa
template <typename T> struct SerializadorExterno;

// Especialización para tipos primitivos
template <> struct SerializadorExterno<int> {
  static std::string serializar(int valor) {
    std::string datos;
    datos.append(reinterpret_cast<const char *>(&valor), sizeof(int));
    return datos;
  }

  static int deserializar(const std::string &datos, size_t &pos) {
    int valor;
    DESERIALIZAR_INT(datos, pos, valor);
    return valor;
  }
};

template <> struct SerializadorExterno<float> {
  static std::string serializar(float valor) {
    std::string datos;
    datos.append(reinterpret_cast<const char *>(&valor), sizeof(float));
    return datos;
  }

  static float deserializar(const std::string &datos, size_t &pos) {
    float valor;
    DESERIALIZAR_FLOAT(datos, pos, valor);
    return valor;
  }
};

template <> struct SerializadorExterno<std::string> {
  static std::string serializar(const std::string &valor) {
    std::string datos;
    SERIALIZAR_STRING(datos, valor);
    return datos;
  }

  static std::string deserializar(const std::string &datos, size_t &pos) {
    std::string valor;
    DESERIALIZAR_STRING(datos, pos, valor);
    return valor;
  }
};

// Especialización para Sillon
template <> struct SerializadorExterno<Sillon> {
  static std::string serializar(const Sillon &sillon) {
    std::string datos;

    SERIALIZAR_INT(datos, sillon.numeroCojines);
    SERIALIZAR_STRING(datos, sillon.material);
    SERIALIZAR_FLOAT(datos, sillon.peso);
    SERIALIZAR_STRING(datos, sillon.color);

    return datos;
  }

  static Sillon deserializar(const std::string &datos, size_t &pos) {
    Sillon sillon;

    DESERIALIZAR_INT(datos, pos, sillon.numeroCojines);
    DESERIALIZAR_STRING(datos, pos, sillon.material);
    DESERIALIZAR_FLOAT(datos, pos, sillon.peso);
    DESERIALIZAR_STRING(datos, pos, sillon.color);

    return sillon;
  }
};

// Especialización para Mesa
template <> struct SerializadorExterno<Mesa> {
  static std::string serializar(const Mesa &mesa) {
    std::string datos;

    SERIALIZAR_INT(datos, mesa.numeroPatas);
    SERIALIZAR_STRING(datos, mesa.forma);
    SERIALIZAR_FLOAT(datos, mesa.area);
    SERIALIZAR_STRING(datos, mesa.material);

    return datos;
  }

  static Mesa deserializar(const std::string &datos, size_t &pos) {
    Mesa mesa;

    DESERIALIZAR_INT(datos, pos, mesa.numeroPatas);
    DESERIALIZAR_STRING(datos, pos, mesa.forma);
    DESERIALIZAR_FLOAT(datos, pos, mesa.area);
    DESERIALIZAR_STRING(datos, pos, mesa.material);

    return mesa;
  }
};

// Especialización para vector de cualquier tipo
template <typename T> struct SerializadorExterno<std::vector<T>> {
  static std::string serializar(const std::vector<T> &vector) {
    std::string datos;

    uint32_t tamaño = vector.size();
    datos.append(reinterpret_cast<const char *>(&tamaño), sizeof(uint32_t));

    for (const auto &elemento : vector) {
      std::string datosElemento = SerializadorExterno<T>::serializar(elemento);
      uint32_t longitud = datosElemento.size();
      datos.append(reinterpret_cast<const char *>(&longitud), sizeof(uint32_t));
      datos.append(datosElemento);
    }

    return datos;
  }

  static std::vector<T> deserializar(const std::string &datos, size_t &pos) {
    std::vector<T> vector;

    uint32_t tamaño;
    std::memcpy(&tamaño, datos.data() + pos, sizeof(uint32_t));
    pos += sizeof(uint32_t);

    for (uint32_t i = 0; i < tamaño; ++i) {
      uint32_t longitud;
      std::memcpy(&longitud, datos.data() + pos, sizeof(uint32_t));
      pos += sizeof(uint32_t);

      std::string datosElemento(datos.data() + pos, longitud);
      pos += longitud;

      size_t posElemento = 0;
      vector.push_back(
          SerializadorExterno<T>::deserializar(datosElemento, posElemento));
    }

    return vector;
  }
};

// Especialización para la clase Sala (OBJETO COMPLEJO)
template <> struct SerializadorExterno<Sala> {
  static std::string serializar(const Sala &sala) {
    std::string datos;

    // Serializar campos simples
    SERIALIZAR_STRING(datos, sala.nombre);
    SERIALIZAR_FLOAT(datos, sala.areaTotal);
    SERIALIZAR_STRING(datos, sala.tipo);

    // Serializar vector de sillones
    std::string datosSillones =
        SerializadorExterno<std::vector<Sillon>>::serializar(sala.sillones);
    uint32_t longSillones = datosSillones.size();
    datos.append(reinterpret_cast<const char *>(&longSillones),
                 sizeof(uint32_t));
    datos.append(datosSillones);

    // Serializar vector de mesas
    std::string datosMesas =
        SerializadorExterno<std::vector<Mesa>>::serializar(sala.mesas);
    uint32_t longMesas = datosMesas.size();
    datos.append(reinterpret_cast<const char *>(&longMesas), sizeof(uint32_t));
    datos.append(datosMesas);

    return datos;
  }

  static Sala deserializar(const std::string &datos, size_t &pos) {
    Sala sala;

    // Deserializar campos simples
    DESERIALIZAR_STRING(datos, pos, sala.nombre);
    DESERIALIZAR_FLOAT(datos, pos, sala.areaTotal);
    DESERIALIZAR_STRING(datos, pos, sala.tipo);

    // Deserializar vector de sillones
    uint32_t longSillones;
    std::memcpy(&longSillones, datos.data() + pos, sizeof(uint32_t));
    pos += sizeof(uint32_t);

    std::string datosSillones(datos.data() + pos, longSillones);
    pos += longSillones;

    size_t posSillones = 0;
    sala.sillones = SerializadorExterno<std::vector<Sillon>>::deserializar(
        datosSillones, posSillones);

    // Deserializar vector de mesas
    uint32_t longMesas;
    std::memcpy(&longMesas, datos.data() + pos, sizeof(uint32_t));
    pos += sizeof(uint32_t);

    std::string datosMesas(datos.data() + pos, longMesas);
    pos += longMesas;

    size_t posMesas = 0;
    sala.mesas = SerializadorExterno<std::vector<Mesa>>::deserializar(
        datosMesas, posMesas);

    return sala;
  }
};

// ===============================
// FUNCIONES DE USUARIO SIMPLIFICADAS
// ===============================

// Función template para serializar cualquier objeto
template <typename T> std::string serializar(const T &objeto) {
  return SerializadorExterno<T>::serializar(objeto);
}

// Función template para deserializar cualquier objeto
template <typename T> T deserializar(const std::string &datos) {
  size_t pos = 0;
  return SerializadorExterno<T>::deserializar(datos, pos);
}

// ===============================
// EJEMPLO DE USO CON TU PROTOCOLO
// ===============================

// Función para enviar objeto Sala a través del socket
void enviarObjetoSala(int socket, const Sala &sala) {
  // Serializar el objeto completo
  std::string datosSerializados = serializar(sala);

  std::cout << "Tamaño serializado: " << datosSerializados.size() << " bytes"
            << std::endl;

  // Usar tu función padNumber existente
  auto padNumber = [](int num, int width) {
    std::string s = std::to_string(num);
    if ((int)s.size() < width)
      s = std::string(width - s.size(), '0') + s;
    return s;
  };

  // Enviar a través del protocolo (similar a como envías archivos)
  std::string payload =
      "O" + padNumber(datosSerializados.size(), 6) + datosSerializados;
  write(socket, payload.c_str(), payload.size());

  std::cout << "Objeto Sala enviado exitosamente!" << std::endl;
}

// Función para recibir objeto Sala desde el socket
Sala recibirObjetoSala(int socket) {
  // Leer longitud de los datos (6 dígitos)
  std::string lenDatosStr;
  for (int i = 0; i < 6; ++i) {
    char c;
    if (read(socket, &c, 1) <= 0)
      throw std::runtime_error("Error leyendo longitud");
    lenDatosStr += c;
  }
  int lenDatos = std::stoi(lenDatosStr);

  // Leer datos del objeto
  std::string datosObjeto;
  for (int i = 0; i < lenDatos; ++i) {
    char c;
    if (read(socket, &c, 1) <= 0)
      throw std::runtime_error("Error leyendo datos");
    datosObjeto += c;
  }

  // Deserializar el objeto
  return deserializar<Sala>(datosObjeto);
}

// ===============================
// DEMOSTRACIÓN COMPLETA
// ===============================

int main() {
  std::cout << "=== DEMOSTRACIÓN SERIALIZACIÓN EXTERNA ===" << std::endl;

  // 1. Crear un objeto Sala complejo
  Sala salaOriginal("Sala de Estar Principal", 35.5f, "Residencial");

  // Agregar sillones
  salaOriginal.sillones.push_back(Sillon(3, "Cuero", 15.0f, "Negro"));
  salaOriginal.sillones.push_back(Sillon(2, "Tela", 8.5f, "Beige"));
  salaOriginal.sillones.push_back(Sillon(4, "Cuero", 18.0f, "Marrón"));

  // Agregar mesas
  salaOriginal.mesas.push_back(Mesa(4, "Rectangular", 6.2f, "Madera de roble"));
  salaOriginal.mesas.push_back(Mesa(1, "Redonda", 2.8f, "Cristal"));

  std::cout << "=== SALA ORIGINAL ===" << std::endl;
  salaOriginal.mostrar();

  // 2. Serializar el objeto completo
  std::string datosSerializados = serializar(salaOriginal);
  std::cout << "\n=== DATOS SERIALIZADOS ===" << std::endl;
  std::cout << "Tamaño: " << datosSerializados.size() << " bytes" << std::endl;

  // 3. Deserializar a un nuevo objeto
  Sala salaReconstruida = deserializar<Sala>(datosSerializados);

  std::cout << "\n=== SALA RECONSTRUIDA ===" << std::endl;
  salaReconstruida.mostrar();

  // 4. Verificar que son iguales
  std::cout << "\n=== VERIFICACIÓN ===" << std::endl;
  std::cout << "Nombre igual: "
            << (salaOriginal.nombre == salaReconstruida.nombre ? "SÍ" : "NO")
            << std::endl;
  std::cout << "Área igual: "
            << (salaOriginal.areaTotal == salaReconstruida.areaTotal ? "SÍ"
                                                                     : "NO")
            << std::endl;
  std::cout << "Número de sillones igual: "
            << (salaOriginal.sillones.size() == salaReconstruida.sillones.size()
                    ? "SÍ"
                    : "NO")
            << std::endl;
  std::cout << "Número de mesas igual: "
            << (salaOriginal.mesas.size() == salaReconstruida.mesas.size()
                    ? "SÍ"
                    : "NO")
            << std::endl;

  // 5. Demostrar que también funciona con objetos simples
  std::cout << "\n=== SERIALIZACIÓN DE OBJETOS SIMPLES ===" << std::endl;

  Sillon sillonSimple(1, "Plástico", 5.5f, "Rojo");
  std::string datosSillon = serializar(sillonSimple);
  Sillon sillonCopy = deserializar<Sillon>(datosSillon);

  std::cout << "Sillón original: " << sillonSimple.material << std::endl;
  std::cout << "Sillón copia: " << sillonCopy.material << std::endl;

  return 0;
}

/*

En el cliente:

cpp
// Donde tengas tu menú, añadir:
else if (opt == 8) {  // Enviar objeto Sala
    Sala salaParaEnviar("Mi Sala", 30.0f, "Privada");
    salaParaEnviar.sillones.push_back(Sillon(2, "Cuero", 10.0f, "Azul"));
    salaParaEnviar.mesas.push_back(Mesa(4, "Cuadrada", 4.0f, "Madera"));

    enviarObjetoSala(SocketCli, salaParaEnviar);
}
En el servidor:

cpp
case 'O': {  // Recibir objeto Sala
    try {
        Sala salaRecibida = recibirObjetoSala(socketConn);

        // Obtener quién lo envió
        std::string remitente;
        {
            std::lock_guard<std::mutex> lock(clientsMutex);
            for (auto &p : m_clients) {
                if (p.second == socketConn) {
                    remitente = p.first;
                    break;
                }
            }
        }

        std::cout << remitente << " envió un objeto Sala: " <<
salaRecibida.nombre << std::endl; salaRecibida.mostrar();

    } catch (const std::exception& e) {
        std::cout << "Error recibiendo objeto: " << e.what() << std::endl;
    }
    break;
}
*/