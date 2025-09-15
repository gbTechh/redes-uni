📋 ¿Qué es .c_str()?
.c_str() es un método de la clase std::string que devuelve un puntero constante 
a char (const char*) que apunta a un array de caracteres que contiene una versión de cadena estilo C de la string.

string remitente = "Juan";
archivo.write(remitente, remitente.size());  // ❌ ERROR! No compila
//Error: write() espera un const char*, pero remitente es un std::string

string remitente = "Juan";
archivo.write(remitente.c_str(), remitente.size());  // ✅ CORRECTO

//que devuelve .c_str()?
string mensaje = "Hola";
const char* puntero_c = mensaje.c_str();
//puntero_c apunta a: ['H']['o']['l']['a']['\0']

[std::string object]
  │
  ├── size() = 4
  ├── capacity() = 15
  └── pointer → ['J']['u']['a']['n']['\0'][...]

//con .c_str()
archivo.write(remitente.c_str(), longRemitente);
//c_str() devuelve: 0x12345678 (dirección del primer carácter)
//write() escribe: 'J''u''a''n' (4 bytes, sin el \0)
