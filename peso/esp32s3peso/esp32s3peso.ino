#include "HX711.h"
#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <lvgl.h>
#include "ui.h"
#include "gfx_conf.h"

// Configuración WiFi
const char* ssid = "usuario de su internet";
const char* password = "contraseña";

// Definición de pines HX711
#define DT 38
#define SCK 44

// Variables para la báscula
HX711 balanza;
float factorCalibracion = 198180.70;
long offset = 0;
float peso = 0;
float lectura = 0;

// Servidor web en puerto 80
WebServer servidor(80);

// Variables para almacenar datos históricos
#define MAX_DATOS 60
float historialPesos[MAX_DATOS];
int indiceHistorial = 0;
unsigned long ultimaActualizacion = 0;
const unsigned long intervaloActualizacion = 1000; // 1 segundo

// Variables para la pantalla LVGL
static lv_disp_draw_buf_t draw_buf;
static lv_color_t buf1[screenWidth * 10];
static lv_disp_drv_t disp_drv;
static lv_indev_drv_t indev_drv;

// Declaración de funciones para corregir errores de referencia
void handleRoot();
void handleData();
void handleTara();
void handleHistory();
void handleNotFound();

void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) {
  uint32_t w = (area->x2 - area->x1 + 1);
  uint32_t h = (area->y2 - area->y1 + 1);
  tft.setAddrWindow(area->x1, area->y1, w, h);
  tft.pushColors((uint16_t *)&color_p->full, w * h, true);
  lv_disp_flush_ready(disp);
}

void my_touchpad_read(lv_indev_drv_t *indev_drv, lv_indev_data_t *data) {
  uint16_t touchX, touchY;
  bool touched = tft.getTouch(&touchX, &touchY);
  if (touched) {
    data->state = LV_INDEV_STATE_PR;
    data->point.x = touchX;
    data->point.y = touchY;
  } else {
    data->state = LV_INDEV_STATE_REL;
  }
}

void inicializar_lvgl() {
  // Inicializar pantalla
  tft.begin();
  tft.setRotation(0);
  tft.setBrightness(128);
  
  // Inicializar LVGL
  lv_init();
  
  // Inicializar buffer para dibujar
  lv_disp_draw_buf_init(&draw_buf, buf1, NULL, screenWidth * 10);
  
  // Inicializar el controlador de pantalla
  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res = screenWidth;
  disp_drv.ver_res = screenHeight;
  disp_drv.flush_cb = my_disp_flush;
  disp_drv.draw_buf = &draw_buf;
  lv_disp_drv_register(&disp_drv);
  
  // Inicializar el controlador de entrada táctil
  lv_indev_drv_init(&indev_drv);
  indev_drv.type = LV_INDEV_TYPE_POINTER;
  indev_drv.read_cb = my_touchpad_read;
  lv_indev_drv_register(&indev_drv);
  
  // Inicializar interfaz de usuario
  ui_init();
}

void actualizarPantalla() {
  // Mostrar el peso en la pantalla integrada
  char pesoTexto[32];
  snprintf(pesoTexto, sizeof(pesoTexto), "%.2f Kg", peso);
  lv_textarea_set_text(uic_mostar_peso, pesoTexto);

  // Mostrar la IP en la pantalla integrada
  lv_textarea_set_text(uic_mostrar_ip, WiFi.localIP().toString().c_str());
}

void configurarBotonTara() {
  lv_obj_add_event_cb(uic_restar_peso, [](lv_event_t * e) {
    if (balanza.is_ready()) {
      offset = balanza.read();
      Serial.println("Tara realizada con éxito");
      actualizarPantalla();
    } else {
      Serial.println("Error: HX711 no responde-1");
    }
  }, LV_EVENT_CLICKED, NULL);
}

void setup() {
  Serial.begin(115200);
  
  // Inicializar HX711
  balanza.begin(DT, SCK);
  
  // Realizar tara inicial
  Serial.println("Calibrando tara...");
  delay(2000);
  if (balanza.is_ready()) {
    offset = balanza.read();
    Serial.print("Tara establecida: ");
    Serial.println(offset);
  } else {
    Serial.println("Error: HX711 no responde-2");
  }
  
  // Inicializar historial de pesos
  for (int i = 0; i < MAX_DATOS; i++) {
    historialPesos[i] = 0;
  }
  
  // Conectar a WiFi
  WiFi.begin(ssid, password);
  Serial.print("Conectando a WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("Conectado a WiFi. IP: ");
  Serial.println(WiFi.localIP());
  
  // Configurar rutas del servidor web
  servidor.on("/", handleRoot);
  servidor.on("/data", handleData);
  servidor.on("/tara", handleTara);
  servidor.on("/history", handleHistory);
  servidor.onNotFound(handleNotFound);
  
  // Iniciar servidor web
  servidor.begin();
  Serial.println("Servidor web iniciado");
  
  // Mostrar información importante en el monitor
  Serial.println("=================================");
  Serial.println("SISTEMA DE MEDICIÓN DE PESO");
  Serial.println("=================================");
  Serial.print("Conectado a red: ");
  Serial.println(ssid);
  Serial.print("Dirección IP: ");
  Serial.println(WiFi.localIP());
  Serial.print("Factor de calibración: ");
  Serial.println(factorCalibracion);
  Serial.println(balanza.read());
  Serial.println("=================================");
  Serial.println("Accede a la interfaz desde un navegador usando la IP mostrada");
  Serial.println("=================================");
  
  // Inicializar LVGL y la pantalla
  inicializar_lvgl();

  // Configurar el botón de tara
  configurarBotonTara();

  // Actualizar la pantalla con los valores iniciales
  actualizarPantalla();
}

void loop() {
  servidor.handleClient();
  
  // Actualizar lectura de peso cada segundo
  unsigned long ahora = millis();
  if (ahora - ultimaActualizacion >= intervaloActualizacion) {
    ultimaActualizacion = ahora;
    
    if (balanza.is_ready()) {
      long lectura = balanza.read();
      peso = ((lectura - offset) / factorCalibracion); // Convertir a gramos
      
      // Actualizar historial
      historialPesos[indiceHistorial] = peso;
      indiceHistorial = (indiceHistorial + 1) % MAX_DATOS;
      
      Serial.print("Peso: ");
      Serial.print(peso, 2);
      Serial.println(" Kg");
    } else {
      Serial.println("Error: HX711 no responde-3");
    }
  }

  // Actualizar la pantalla en tiempo real
  actualizarPantalla();

  // Actualizar LVGL (necesario para que la interfaz responda)
  lv_timer_handler();
  delay(5);
}

// Manejador de la página principal
void handleRoot() {
  String html = R"rawliteral(
<!DOCTYPE html>
<html lang="es">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Sistema de Medición de Peso</title>
  <style>
    body {
      font-family: Arial, sans-serif;
      margin: 0;
      padding: 0;
      background-color: #f5f5f5;
      color: #333;
    }
    .container {
      max-width: 1200px;
      margin: 0 auto;
      padding: 20px;
    }
    header {
      background-color: #2c3e50;
      color: white;
      padding: 1rem;
      text-align: center;
      border-radius: 5px;
      margin-bottom: 20px;
      box-shadow: 0 2px 5px rgba(0,0,0,0.1);
    }
    h1 {
      margin: 0;
      font-size: 1.8rem;
    }
    .dashboard {
      display: grid;
      grid-template-columns: repeat(auto-fit, minmax(300px, 1fr));
      gap: 20px;
      margin-bottom: 20px;
    }
    .card {
      background-color: white;
      border-radius: 5px;
      padding: 20px;
      box-shadow: 0 2px 5px rgba(0,0,0,0.1);
    }
    .card-title {
      font-size: 1.2rem;
      margin-top: 0;
      margin-bottom: 15px;
      color: #2c3e50;
      border-bottom: 1px solid #eee;
      padding-bottom: 10px;
    }
    .weight-display {
      font-size: 3rem;
      text-align: center;
      font-weight: bold;
      color: #2c3e50;
      margin-bottom: 20px;
    }
    .weight-unit {
      font-size: 1.5rem;
      color: #7f8c8d;
    }
    .chart-container {
      height: 300px;
      position: relative;
    }
    .btn {
      background-color: #3498db;
      color: white;
      border: none;
      padding: 10px 15px;
      border-radius: 4px;
      cursor: pointer;
      font-size: 1rem;
      transition: background-color 0.3s;
    }
    .btn:hover {
      background-color: #2980b9;
    }
    .btn-tare {
      background-color: #e74c3c;
    }
    .btn-tare:hover {
      background-color: #c0392b;
    }
    .status {
      margin-top: 10px;
      font-size: 0.9rem;
      color: #7f8c8d;
    }
    .footer {
      text-align: center;
      margin-top: 20px;
      padding-top: 20px;
      border-top: 1px solid #ddd;
      font-size: 0.9rem;
      color: #7f8c8d;
    }
    #weightChart {
      width: 100%;
      height: 100%;
      background-color: white;
    }
  </style>
</head>
<body>
  <div class="container">
    <header>
      <h1>Sistema de Medición de Peso</h1>
      <h1>Cristian Cardona</h1>
      <h1>Deyvid Ramirez</h1>
    </header>
    
    <div class="dashboard">
      <div class="card">
        <h2 class="card-title">Peso Actual</h2>
        <div class="weight-display">
          <span id="peso">0.00</span>
          <span class="weight-unit">Kg</span>
        </div>
        <button class="btn btn-tare" onclick="realizarTara()">Tara</button>
        <div class="status" id="status">Conectado</div>
      </div>
      
      <div class="card">
        <h2 class="card-title">Estadísticas</h2>
        <div>
          <p><strong>Máximo:</strong> <span id="max">0.00</span> Kg</p>
          <p><strong>Mínimo:</strong> <span id="min">0.00</span> Kg</p>
          <p><strong>Promedio:</strong> <span id="promedio">0.00</span> Kg</p>
        </div>
      </div>
    </div>
    
    <div class="card">
      <h2 class="card-title">Historial de Mediciones</h2>
      <div class="chart-container">
        <canvas id="weightChart"></canvas>
      </div>
    </div>
    
    <div class="footer">
      Sistema de Medición de Peso v1.0 | ESP32-S3 con HX711
      ITM – Institución Universitaria – Reacreditada en Alta Calidad
    </div>
  </div>
  
  <script>
    // Variables para el gráfico
    let canvas = document.getElementById('weightChart');
    let ctx = canvas.getContext('2d');
    let chartData = Array(60).fill(0);
    
    // Ajustar el tamaño del canvas
    function resizeCanvas() {
      canvas.width = canvas.parentElement.clientWidth;
      canvas.height = canvas.parentElement.clientHeight;
    }
    
    // Dibujar el gráfico
    function drawChart() {
      const width = canvas.width;
      const height = canvas.height;
      const padding = 40;
      
      // Limpiar el canvas
      ctx.clearRect(0, 0, width, height);
      
      // Dibujar ejes
      ctx.beginPath();
      ctx.strokeStyle = '#666';
      ctx.lineWidth = 1;
      ctx.moveTo(padding, padding);
      ctx.lineTo(padding, height - padding);
      ctx.lineTo(width - padding, height - padding);
      ctx.stroke();
      
      // Encontrar máximo para escala
      const maxValue = Math.max(...chartData, 1);
      
      // Dibujar datos
      ctx.beginPath();
      ctx.strokeStyle = '#3498db';
      ctx.lineWidth = 2;
      
      const pointWidth = (width - padding * 2) / (chartData.length - 1);
      
      chartData.forEach((value, index) => {
        const x = padding + (index * pointWidth);
        const y = height - padding - ((value / maxValue) * (height - padding * 2));
        
        if (index === 0) {
          ctx.moveTo(x, y);
        } else {
          ctx.lineTo(x, y);
        }
      });
      
      ctx.stroke();
      
      // Dibujar etiquetas del eje Y
      ctx.fillStyle = '#666';
      ctx.font = '12px Arial';
      ctx.textAlign = 'right';
      ctx.textBaseline = 'middle';
      
      for (let i = 0; i <= 5; i++) {
        const value = (maxValue * i / 5).toFixed(1);
        const y = height - padding - ((i / 5) * (height - padding * 2));
        ctx.fillText(value + 'g', padding - 5, y);
      }
    }
    
    // Actualizar datos
    function actualizarDatos() {
      fetch('/data')
        .then(response => response.json())
        .then(data => {
          document.getElementById('peso').textContent = data.peso.toFixed(2);
          document.getElementById('status').textContent = 'Última actualización: ' + new Date().toLocaleTimeString();
        })
        .catch(error => {
          document.getElementById('status').textContent = 'Error de conexión';
        });
        
      fetch('/history')
        .then(response => response.json())
        .then(data => {
          chartData = data.pesos;
          
          const pesosFiltrados = chartData.filter(peso => peso > 0);
          const max = Math.max(...pesosFiltrados, 0);
          const min = Math.min(...pesosFiltrados, 0);
          const promedio = pesosFiltrados.length ? 
            (pesosFiltrados.reduce((a, b) => a + b, 0) / pesosFiltrados.length) : 0;
          
          document.getElementById('max').textContent = max.toFixed(2);
          document.getElementById('min').textContent = min.toFixed(2);
          document.getElementById('promedio').textContent = promedio.toFixed(2);
          
          drawChart();
        })
        .catch(error => {
          console.error('Error:', error);
        });
    }
    
    function realizarTara() {
      fetch('/tara')
        .then(response => response.json())
        .then(data => {
          document.getElementById('status').textContent = data.mensaje;
        })
        .catch(error => {
          document.getElementById('status').textContent = 'Error al realizar tara';
        });
    }
    
    // Inicialización
    window.addEventListener('load', () => {
      resizeCanvas();
      actualizarDatos();
      setInterval(actualizarDatos, 1000);
    });
    
    // Manejar redimensionamiento
    window.addEventListener('resize', () => {
      resizeCanvas();
      drawChart();
    });
  </script>
</body>
</html>
)rawliteral";
  
  servidor.send(200, "text/html", html);
}

// Manejador para entregar datos de peso actual
void handleData() {
  StaticJsonDocument<200> doc;
  doc["peso"] = peso;
  
  String jsonString;
  serializeJson(doc, jsonString);
  servidor.send(200, "application/json", jsonString);
}

// Manejador para realizar tara
void handleTara() {
  StaticJsonDocument<200> doc;
  
  if (balanza.is_ready()) {
    offset = balanza.read();
    doc["mensaje"] = "Tara realizada con éxito";
    doc["offset"] = offset;
  } else {
    doc["mensaje"] = "Error: HX711 no responde-4";
  }
  
  String jsonString;
  serializeJson(doc, jsonString);
  servidor.send(200, "application/json", jsonString);
}

// Manejador para datos históricos
void handleHistory() {
  StaticJsonDocument<2048> doc;
  JsonArray pesoArray = doc.createNestedArray("pesos");
  
  for (int i = 0; i < MAX_DATOS; i++) {
    int indice = (indiceHistorial + i) % MAX_DATOS;
    pesoArray.add(historialPesos[indice]);
  }
  
  String jsonString;
  serializeJson(doc, jsonString);
  servidor.send(200, "application/json", jsonString);
}

// Manejador para rutas no encontradas
void handleNotFound() {
  servidor.send(404, "text/plain", "Página no encontrada");
}
