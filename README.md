# Comprobador Modbus RTU con ESP32, RS-485 y pantalla Nokia 5110

## 1. Descripción del proyecto

Este proyecto consiste en un comprobador de comunicaciones Modbus RTU desarrollado con un ESP32 Node32S, un transceptor RS-485 MAX485 y una pantalla LCD Nokia 5110.

El dispositivo permite consultar registros de equipos compatibles con Modbus RTU, comprobar la comunicación con diferentes direcciones de esclavo y visualizar los resultados desde una interfaz web accesible mediante Wi-Fi.

El objetivo es disponer de una herramienta portátil para realizar pruebas de comunicación con equipos industriales, contadores eléctricos, controladores y otros dispositivos que utilicen Modbus RTU sobre RS-485.

El programa incluye distintas opciones de prueba y una función de escritura de registros. Esta última debe utilizarse únicamente cuando se conozca el registro que se desea modificar y se disponga de autorización para hacerlo.

## 2. Características principales

- Microcontrolador ESP32 Node32S.
- Comunicación Modbus RTU mediante RS-485.
- Transceptor MAX485.
- Pantalla LCD Nokia 5110 con controlador PCD8544.
- Punto de acceso Wi-Fi propio.
- Interfaz web accesible desde un ordenador, una tableta o un teléfono móvil.
- Configuración de los parámetros de comunicación serie.
- Consulta de registros Modbus.
- Visualización de resultados y errores de comunicación.
- Funciones de escritura de registros, según las opciones implementadas en el programa.
- Indicación local de información en la pantalla LCD.

## 3. Hardware necesario

| Componente | Función |
|---|---|
| ESP32 Node32S | Control principal del comprobador |
| Módulo MAX485 o transceptor RS-485 compatible | Conversión entre UART y bus RS-485 |
| Pantalla Nokia 5110 | Visualización local |
| Cable USB | Alimentación, programación y depuración |
| Cableado para RS-485 | Conexión con el equipo que se va a comprobar |
| Fuente de alimentación adecuada, si es necesaria | Alimentación del montaje |

También se necesita un ordenador con Arduino IDE para compilar y cargar el programa.

## 4. Esquema general del sistema

```text
                 ORDENADOR / MÓVIL
                         |
                      Wi-Fi
                         |
                         v
                  ESP32 Node32S
                 +---------------+
                 |               |
                 |  Interfaz web |
                 |  Modbus RTU   |
                 |  Pantalla LCD |
                 |               |
                 +-------+-------+
                         |
                       UART2
                         |
                         v
                    +---------+
                    | MAX485  |
                    +----+----+
                         |
                    Bus RS-485
                         |
                         v
                  EQUIPO MODBUS
```

El ESP32 crea una red Wi-Fi a la que se conecta el usuario. Desde el navegador puede acceder a la interfaz web del comprobador.

Cuando se solicita una lectura o escritura, el ESP32 transmite la petición Modbus RTU a través del MAX485. El equipo remoto responde por el mismo bus RS-485.

## 5. Conexiones del ESP32 con el MAX485

La comunicación serie utiliza UART2 del ESP32.

| ESP32 Node32S | MAX485 | Función |
|---|---|---|
| GPIO 17 (RX2) | RO | Recepción de datos |
| GPIO 22 (TX2) | DI | Transmisión de datos |
| GPIO 21 | DE y /RE unidos | Control de transmisión y recepción |
| GND | GND | Referencia común |
| Alimentación compatible | VCC | Alimentación del módulo |

### Esquema en modo texto

```text
ESP32 Node32S                   MAX485
--------------                  ------

GPIO 22 (TX2) ----------------> DI
GPIO 17 (RX2) <---------------- RO

GPIO 21 -------------------+--> DE
                           |
                           +--> /RE

GND -------------------------- GND
Alimentación adecuada ------- VCC
```

Las señales DE y /RE se conectan juntas a GPIO 21 para controlar la dirección de comunicación.

En la configuración habitual del MAX485:

- GPIO 21 en nivel alto: se habilita la transmisión.
- GPIO 21 en nivel bajo: se habilita la recepción.

El programa debe cambiar correctamente este pin durante cada intercambio Modbus.

### Precaución con los niveles lógicos

El ESP32 trabaja con lógica de 3,3 V y sus GPIO no toleran 5 V. Algunos módulos MAX485 se alimentan a 5 V y pueden entregar una señal RO de 5 V.

Antes de conectar RO al GPIO 17, comprueba la tensión de salida del módulo. Si es necesario, utiliza un transceptor compatible con lógica de 3,3 V o un adaptador de nivel adecuado.

## 6. Conexiones del bus RS-485

```text
       ESP32
          |
          v
       MAX485
          |
          +---------------------- RS-485 A
          |
          +---------------------- RS-485 B
                                      |
                                      v
                                EQUIPO MODBUS
```

La conexión habitual entre dispositivos es:

| MAX485 | Equipo Modbus |
|---|---|
| A | A o D+, según el fabricante |
| B | B o D−, según el fabricante |

**Importante:** la nomenclatura A/B no es uniforme entre todos los fabricantes. Si no hay comunicación, consulta los manuales de ambos equipos y verifica la correspondencia de las líneas.

Recomendaciones para el cableado:

- Utilizar un par trenzado para las señales A y B.
- Evitar derivaciones innecesariamente largas.
- Instalar resistencias de terminación únicamente cuando corresponda por la topología y la longitud del bus.
- No conectar nunca las señales RS-485 directamente a los GPIO del ESP32.
- Comprobar que los dispositivos conectados al bus utilizan parámetros Modbus compatibles.

## 7. Conexiones de la pantalla Nokia 5110

La pantalla utiliza la biblioteca `Adafruit_PCD8544` con esta declaración:

```cpp
Adafruit_PCD8544 display(18, 23, 16, 5, 4);
```

El orden de los parámetros corresponde a los pines de reloj, datos, control de datos/comandos, selección y reinicio.

| ESP32 Node32S | Nokia 5110 | Función |
|---|---|---|
| GPIO 18 | SCLK / CLK | Reloj serie |
| GPIO 23 | DIN / MOSI | Datos |
| GPIO 16 | D/C | Selección de datos o comandos |
| GPIO 5 | CS / SCE | Selección de pantalla |
| GPIO 4 | RST / RESET | Reinicio |
| GND | GND | Masa |
| Alimentación compatible | VCC | Alimentación |
| Según el módulo | LIGHT / LED | Retroiluminación |

### Esquema en modo texto

```text
ESP32 Node32S                   NOKIA 5110
--------------                  ---------

GPIO 18 ----------------------> SCLK
GPIO 23 ----------------------> DIN
GPIO 16 ----------------------> D/C
GPIO 5  ----------------------> CS / SCE
GPIO 4  ----------------------> RST / RESET

GND -------------------------- GND
Alimentación compatible ------ VCC
```

Comprueba la tensión admitida por tu módulo antes de conectarlo. La alimentación de la pantalla y la de la retroiluminación pueden tener requisitos diferentes según la placa.

## 8. Resumen de los pines utilizados

| GPIO | Uso |
|---|---|
| GPIO 4 | Reset de Nokia 5110 |
| GPIO 5 | Selección de Nokia 5110 |
| GPIO 16 | Señal D/C de Nokia 5110 |
| GPIO 17 | RX2 del ESP32, recepción RS-485 |
| GPIO 18 | Reloj de Nokia 5110 |
| GPIO 21 | Control de dirección del MAX485 |
| GPIO 22 | TX2 del ESP32, transmisión RS-485 |
| GPIO 23 | Datos de Nokia 5110 |

No cambies estos pines sin comprobar también la configuración correspondiente en el código.

## 9. Configuración inicial de Modbus

La configuración inicial prevista para el equipo de prueba es:

| Parámetro | Valor |
|---|---|
| Protocolo | Modbus RTU |
| Interfaz física | RS-485 |
| Dirección del esclavo | 5 |
| Velocidad | 19200 baudios |
| Paridad | Even (par) |
| Bits de parada | 1 |

Esta configuración debe coincidir con la del equipo conectado.

La dirección Modbus identifica al dispositivo en el bus. No debe confundirse con la dirección de un registro.

Si el equipo utiliza otros parámetros, hay que ajustarlos en el programa o mediante las opciones de configuración que incluya la interfaz.

## 10. Instalación del entorno de desarrollo

### 10.1. Instalar Arduino IDE

Descarga e instala Arduino IDE desde la página oficial:

https://www.arduino.cc/en/software

### 10.2. Configurar el ESP32

En Arduino IDE, instala el soporte para placas ESP32 mediante el Gestor de placas, si todavía no está instalado.

Selecciona la placa correspondiente a tu ESP32. En muchas instalaciones se utiliza una configuración compatible con ESP32 Dev Module, aunque debes comprobar cuál corresponde a tu placa concreta.

Selecciona también el puerto serie correcto. En este montaje se ha utilizado COM4, pero el puerto puede cambiar según el ordenador y el cable USB.

### 10.3. Instalar las bibliotecas

Instala las bibliotecas utilizadas por el programa desde el Gestor de bibliotecas de Arduino IDE, si no están ya disponibles:

- `ModbusMaster`, para las operaciones Modbus.
- `Adafruit GFX Library`, si la pantalla la utiliza como dependencia.
- `Adafruit PCD8544 Nokia 5110 LCD library`, para controlar la pantalla.

Si alguna biblioteca requiere dependencias adicionales, instala las que indique Arduino IDE.

### 10.4. Compilar y cargar el programa

1. Abre el archivo del comprobador en Arduino IDE.
2. Comprueba que están instaladas las bibliotecas necesarias.
3. Selecciona la placa y el puerto serie.
4. Compila el programa.
5. Conecta el ESP32 por USB.
6. Carga el programa.
7. Abre el monitor serie si necesitas consultar mensajes de diagnóstico.

Si la carga falla, comprueba el puerto, el cable USB, el controlador USB-serie y la selección de placa.

## 11. Acceso a la interfaz web

El comprobador crea un punto de acceso Wi-Fi propio.

Configuración inicial prevista:

| Parámetro | Valor |
|---|---|
| Nombre de la red (SSID) | `ModbusTester` |
| Contraseña | `Tester123` |
| Dirección de acceso | `http://192.168.4.1` |

### Procedimiento de conexión

1. Enciende el ESP32.
2. Desde el ordenador o el móvil, abre la lista de redes Wi-Fi.
3. Conéctate a `ModbusTester`.
4. Introduce la contraseña `Tester123`.
5. Abre el navegador.
6. Accede a `http://192.168.4.1`.

La dirección indicada es la dirección habitual del ESP32 cuando crea su propio punto de acceso con la configuración predeterminada. Si se ha cambiado la configuración de red en el código, utiliza la dirección configurada realmente.

Por seguridad, cambia la contraseña predeterminada antes de utilizar el comprobador en un entorno donde otras personas puedan conectarse a él.

## 12. Opciones del comprobador

El programa presenta varias opciones de prueba desde su interfaz. Las operaciones concretas dependen de la implementación del código cargado.

### Opciones 1 a 5: pruebas de comunicación y lectura

Estas opciones se utilizan para realizar las comprobaciones disponibles en el programa, como consultar registros, comprobar la respuesta de un equipo y visualizar los datos recibidos.

Antes de ejecutar una lectura, verifica:

- La dirección del esclavo.
- La velocidad de comunicación.
- La paridad.
- Los bits de parada.
- La dirección del registro.
- La cantidad de registros que se desea leer, si la operación lo requiere.

Una respuesta correcta indica que el equipo ha respondido a la petición realizada. Una respuesta de error puede deberse a una configuración incorrecta, un registro no válido, un problema de cableado o una excepción Modbus devuelta por el equipo.

### Opción 6: escritura de registros

La opción 6 permite realizar operaciones de escritura en los registros contemplados por el programa.

La escritura Modbus puede modificar parámetros de funcionamiento del equipo conectado. Por ello, antes de utilizar esta opción:

1. Identifica el registro exacto que se va a modificar.
2. Consulta la documentación del fabricante.
3. Comprueba si el registro admite escritura.
4. Verifica el formato y el rango de valores permitidos.
5. Asegúrate de que el equipo puede modificarse sin afectar a una instalación en servicio.
6. Confirma el resultado de la operación mediante una lectura posterior, si el registro lo permite.

No escribas valores aleatorios en registros desconocidos. Una escritura aceptada por el protocolo no garantiza que el valor sea adecuado para el equipo.

## 13. Funcionamiento de una comunicación Modbus RTU

Una comunicación Modbus RTU sigue el esquema maestro-esclavo habitual:

```text
COMPROBADOR                         EQUIPO MODBUS
    |                                     |
    |------- Petición Modbus ------------>|
    |                                     |
    |<------ Respuesta Modbus ------------|
    |                                     |
    v                                     v
Interpretar datos                  Ejecutar petición
y mostrar resultado                y responder
```

El comprobador inicia la comunicación. El equipo remoto responde si reconoce la dirección y puede procesar la petición.

Una trama Modbus RTU incluye normalmente:

- Dirección del esclavo.
- Código de función.
- Datos de la petición o respuesta.
- CRC de 16 bits para detectar errores de transmisión.

El CRC permite comprobar la integridad de la trama, pero no garantiza que el registro consultado sea el correcto ni que un valor recibido tenga el significado esperado.

### Lectura de registros

La función Modbus 03 se utiliza para leer registros de retención (*Holding Registers*).

En las bibliotecas habituales, el programa especifica la dirección inicial y la cantidad de registros. Debe comprobarse cómo representa las direcciones la biblioteca y cómo las documenta el fabricante, ya que algunas tablas utilizan referencias como `40001` mientras que la petición transmite un desplazamiento desde cero.

Por tanto, una dirección indicada en el manual no siempre se introduce literalmente en la función de lectura.

### Escritura de registros

La escritura utiliza el código de función que corresponda al tipo de operación implementada, por ejemplo, escritura de un único registro o escritura de varios registros.

El código exacto depende de la función utilizada por el programa. Para verificar una escritura, consulta la respuesta del esclavo y, cuando sea posible, vuelve a leer el registro.

## 14. Interpretación de los resultados

Durante las pruebas pueden aparecer diferentes situaciones:

| Resultado | Posible significado |
|---|---|
| Respuesta correcta | El equipo ha respondido a la petición |
| Sin respuesta | Dirección incorrecta, cableado, parámetros serie o equipo desconectado |
| Error de CRC | Trama recibida con errores o problema de comunicación |
| Excepción Modbus | El equipo ha respondido indicando un error de protocolo u operación |
| Valor inesperado | Dirección, formato, escala o interpretación del registro incorrectos |
| Escritura sin efecto aparente | Registro de solo lectura, valor no permitido o parámetro que requiere otro procedimiento |

Estos resultados son orientativos. Para identificar la causa real, revisa el código de error, la trama intercambiada y la documentación del equipo.

## 15. Comprobaciones si no hay comunicación

Si el comprobador no recibe respuesta, realiza estas comprobaciones en orden:

1. Comprueba que el equipo Modbus está alimentado y operativo.
2. Verifica que la dirección del esclavo coincide con la configurada.
3. Confirma la velocidad, la paridad y los bits de parada.
4. Revisa las conexiones entre TX2, RX2, DI y RO.
5. Comprueba que GPIO 21 controla correctamente DE y /RE.
6. Verifica que las líneas A y B están conectadas de acuerdo con los manuales.
7. Comprueba que el bus RS-485 no tiene cortocircuitos.
8. Revisa la compatibilidad eléctrica del transceptor con el ESP32.
9. Confirma que la dirección y el tipo de registro son correctos.
10. Utiliza el monitor serie para revisar los mensajes de diagnóstico disponibles.

Si el equipo funciona con otro maestro Modbus, compara los parámetros de comunicación y la petición que realiza el comprobador con una petición conocida que funcione.

## 16. Precauciones de seguridad

- No conectes el comprobador directamente a circuitos de tensión de red.
- No conectes las entradas del ESP32 a señales de 5 V sin comprobar su compatibilidad.
- Desconecta la alimentación antes de modificar el cableado.
- Comprueba la tensión de alimentación de cada módulo.
- Evita realizar escrituras en equipos industriales en servicio sin autorización.
- Consulta siempre el manual del fabricante antes de modificar registros.
- Utiliza aislamiento galvánico cuando la aplicación o el entorno eléctrico lo requieran.
- No consideres una respuesta Modbus correcta como una garantía de que el valor leído sea seguro o válido.

Este montaje está destinado a pruebas de comunicación de baja tensión. Las conexiones a instalaciones eléctricas industriales deben realizarse con los procedimientos de seguridad y las competencias correspondientes.

## 17. Posibles ampliaciones

El proyecto se puede ampliar con funciones como:

- Exploración de direcciones de esclavo.
- Selección de velocidad y paridad desde la web.
- Lectura de varios registros consecutivos.
- Visualización de tramas en hexadecimal.
- Registro de errores y tiempos de respuesta.
- Exportación de resultados.
- Gestión de una tabla de registros favoritos.
- Protección adicional de las operaciones de escritura.
- Ajuste del contraste y de los mensajes de la pantalla LCD.

Estas funciones son posibles mejoras y no deben considerarse implementadas hasta que se incorporen al programa.

## 18. Resumen

El comprobador integra un ESP32, un transceptor RS-485 y una pantalla Nokia 5110 para realizar pruebas de comunicación Modbus RTU desde una interfaz web.

El ESP32 gestiona las peticiones, el MAX485 adapta la señal UART al bus RS-485 y la pantalla permite consultar información localmente. La interfaz Wi-Fi facilita el manejo del equipo sin necesidad de mantener conectado un ordenador por USB.

La fiabilidad de las pruebas depende de que el cableado, los niveles eléctricos, los parámetros serie, la dirección del esclavo y las direcciones de los registros sean correctos.

**Nota:** los detalles de las opciones de menú y de las funciones de escritura deben verificarse contra la versión exacta del código fuente utilizada para compilar el comprobador.
