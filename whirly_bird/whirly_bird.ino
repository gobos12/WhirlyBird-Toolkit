#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// ----- STEPPER MOTOR CONTROL -----
#include <AccelStepper.h>

const int MS1 = 11;
const int MS2 = 10;
const int MS3 = 8;
const int STEP = 23;
const int MOTOR_DIR = 15;

AccelStepper stepper(AccelStepper::DRIVER, STEP, MOTOR_DIR);
const int BASE_STEPS_PER_REV = 200;  // 1.8° motor
const int MICROSTEP = 16;            // 1,2,4,8,16 (match your MS1..MS3 wiring)
const long STEPS_PER_REV = BASE_STEPS_PER_REV * MICROSTEP;

void changeStepperAngle(int value) {
  long targetSteps = lroundf((value / 360.0f) * (float)STEPS_PER_REV);
  stepper.runToNewPosition(targetSteps);
}

// ----- FAN CONTROL -----
const int PWM = 22;  // EN pin
const int DIR = 18;  // PH pin

void changeFanSpeed(int value) {
  if (value >= 0 && value <= 255) {
    analogWrite(PWM, (int)value);
  }
}

// ----- WEB SERVER -----
#include <WiFi.h>
#include <AsyncTCP.h>           // https://github.com/ESP32Async/AsyncTCP
#include <ESPAsyncWebServer.h>  // https://github.com/ESP32Async/ESPAsyncWebServer

const char *ssid = "smell-engine-esp32";
const char *password = "password";

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

static const char *htmlContent PROGMEM = R"(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>whirly bird</title>
  <link rel="stylesheet" href="	https://cdn.jsdelivr.net/npm/bootstrap@5.3.8/dist/css/bootstrap.min.css">
</head>
<body>
  <h1>Whirly Bird Web Server</h1>
  <br><br>
  <div class="row" id="buttonContainer"></div>
  <script>
    var ws = new WebSocket('ws://192.168.4.1/ws');
    ws.onopen = function() {
      console.log("websocket connected!");
    }
    ws.onmessage = function(event) {
      console.log("websocket message: " + event.data);
    }
    ws.onclose = function() {
      console.log("websocket closed");
    }
    ws.onerror = function(error) {
      console.log("websocket error: " + error);
    }

    const setup = {
      ch1: {
        label: "Clean Air",
        angle: 0
      },
      ch2: {
        label: "Benzaldehyde",
        angle: 90
      },
      ch3: {
        label: "Geosmin",
        angle: 180
      },
      ch4: {
        label: "D-Limonene",
        angle: 270
      }
    }

    function createButtons() {
      const container = document.getElementById('buttonContainer');
      const onClass = 'btn btn-success btn-block mb-2';
      const offClass = 'btn btn-danger btn-block';
      
      for (const key in setup) {
        if(key.startsWith('ch')) {
          const column = document.createElement('div');
          column.className = 'col-md-3';

          const button = document.createElement('button');
          button.className = offClass;
          button.textContent = setup[key].label;
          let on = false;

          button.addEventListener('click', function() {
            on = !on;
            button.className = (on ? onClass : offClass);
            
            var m1 = "MOTOR " + setup[key].angle;
            var m2 = "FAN " + 255;
            ws.send(m1);
            ws.send(m2);

            console.log("websocket sent: " + m1);
            console.log("websocket sent: " + m2);
          });

          column.appendChild(button);
          container.appendChild(button);
        }
      }
    }
    createButtons();

    setInterval(function() {
      if (ws.readyState === WebSocket.OPEN) {
        ws.send("still connected");
      }
    }, 1000);

  </script>
</body>
</html>
)";

static const size_t htmlContentLength = strlen_P(htmlContent);

void processMessage(const String &data) {
  int space = data.indexOf(' ');
  String cmd, valueStr;
  int value = 0;

  if (space > 0) {
    cmd = data.substring(0, space);
    valueStr = data.substring(space + 1);
    value = valueStr.toInt();
  } else {
    cmd = data;
  }

  cmd.toUpperCase();

  // ------ HANDLE CMDS -----
  String reply;
  if (cmd == "FAN") {
    changeFanSpeed(value);
    Serial.println("Changing FAN speed to " + String(value));
  } else if (cmd == "MOTOR") {
    changeStepperAngle(value);
    Serial.println("Changing ANGLE to " + String(value));
  } else {
    Serial.println("UNKNOWN CMD");
  }
}

void wsOnEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
  (void)len;

  if (type == WS_EVT_CONNECT) {
    ws.textAll("new client connected");
    Serial.println("ws connect");
    client->setCloseClientOnQueueFull(false);
    client->ping();
  } else if (type == WS_EVT_DISCONNECT) {
    ws.textAll("client disconnected");
    Serial.println("ws disconnect");
  } else if (type == WS_EVT_ERROR) {
    Serial.println("ws error");
  } else if (type == WS_EVT_PONG) {
    Serial.println("ws pong");
  } else if (type == WS_EVT_DATA) {
    AwsFrameInfo *info = (AwsFrameInfo *)arg;
    Serial.printf(
      "index: %" PRIu64 ", len: %" PRIu64 ", final: %" PRIu8 ", opcode: %" PRIu8 ", framelen: %d\n", info->index, info->len, info->final, info->message_opcode, len);

    if (info->final && info->index == 0 && info->len == len) {  // complete frame
      if (info->message_opcode == WS_TEXT) {
        processMessage((char *)data);
        //Serial.printf("ws text: %s\n", (char *)data);
        client->ping();
      }
    } else {  // incomplete frame
      if (info->index == 0) {
        if (info->num == 0) {
          Serial.printf(
            "ws[%s][%" PRIu32 "] [%" PRIu32 "] MSG START %s\n", server->url(), client->id(), info->num, (info->message_opcode == WS_TEXT) ? "text" : "binary");
        }
        Serial.printf("ws[%s][%" PRIu32 "] [%" PRIu32 "] FRAME START len=%" PRIu64 "\n", server->url(), client->id(), info->num, info->len);
      }
      Serial.printf(
        "ws[%s][%" PRIu32 "] [%" PRIu32 "] FRAME %s, index=%" PRIu64 ", len=%" PRIu32 "]: ", server->url(), client->id(), info->num,
        (info->message_opcode == WS_TEXT) ? "text" : "binary", info->index, (uint32_t)len);

      if (info->message_opcode == WS_TEXT) {
        Serial.printf("%s\n", (char *)data);
      } else {
        for (size_t i = 0; i < len; i++) {
          Serial.printf("%02x ", data[i]);
        }
        Serial.printf("\n");
      }

      if ((info->index + len) == info->len) {
        Serial.printf("ws[%s][%" PRIu32 "] [%" PRIu32 "] FRAME END\n", server->url(), client->id(), info->num);

        if (info->final) {
          Serial.printf("ws[%s][%" PRIu32 "] [%" PRIu32 "] MSG END\n", server->url(), client->id(), info->num);
        }
      }
    }
  }
}

void setup() {
  Serial.begin(115200);

#if ASYNCWEBSERVER_WIFI_SUPPORTED
  WiFi.mode(WIFI_AP);
  bool ok = WiFi.softAP(ssid, password, 1, 0, 4);  // ch=1, hidden=0, max 4 clients
  Serial.println(ok ? "AP started" : "AP failed");
  Serial.print("AP IP: ");
  Serial.println(WiFi.softAPIP());
#endif

  // root html page
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/html", (const uint8_t *)htmlContent, htmlContentLength);
  });

  ws.onEvent(wsOnEvent);
  server.addHandler(&ws);
  server.begin();

  pinMode(MS1, OUTPUT);
  pinMode(MS2, OUTPUT);
  pinMode(MS3, OUTPUT);
  pinMode(PWM, OUTPUT);
  pinMode(DIR, OUTPUT);

  bool ms1 = LOW, ms2 = LOW, ms3 = LOW;
  if (MICROSTEP == 2) { ms1 = HIGH; }
  if (MICROSTEP == 4) { ms2 = HIGH; }
  if (MICROSTEP == 8) {
    ms1 = HIGH;
    ms2 = HIGH;
  }
  if (MICROSTEP == 16) {
    ms1 = HIGH;
    ms2 = HIGH;
    ms3 = HIGH;
  }

  digitalWrite(MS1, ms1);
  digitalWrite(MS2, ms2);
  digitalWrite(MS3, ms3);

  stepper.setMinPulseWidth(2);
  stepper.setMaxSpeed(STEPS_PER_REV);  // sets the maximum steps per second, which determines how fast the motor will turn
  stepper.setAcceleration(2000);       // sets the acceleration rate in steps per second
  stepper.setSpeed(STEPS_PER_REV / 2);
  stepper.setCurrentPosition(0);
}

static uint32_t lastWS = 0;
static const uint32_t deltaWS = 50;

void loop() {
  if (millis() - lastWS >= deltaWS) {
    ws.printfAll("kp:%.4f", (10.0 / 3.0));
    lastWS = millis();
  }
}
