#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include "secrets.h"

const int SPEAKER_PIN = 33;
const int MOTOR_PIN = 25;

WebServer server(80);

const int MOTOR_OFF = 0;
const int MOTOR_ON = 1;
const int MOTOR_MODULATE = 2;
const int MOTOR_SEQUENCE = 3;
const int MAX_MOTOR_SEQUENCE_STEPS = 64;

int motorMode = MOTOR_OFF;
int motorSpeed = 100;
int motorSpinMs = 250;
int motorPauseMs = 750;
bool motorSpinPhase = false;
bool motorPwmHigh = false;
unsigned long motorPhaseStartMs = 0;
unsigned long lastMotorPwmUs = 0;
int motorSequencePowers[MAX_MOTOR_SEQUENCE_STEPS];
int motorSequenceDurations[MAX_MOTOR_SEQUENCE_STEPS];
int motorSequenceLength = 0;
int motorSequenceIndex = 0;
bool motorSequenceRepeat = false;
unsigned long motorSequenceStepStartMs = 0;

const int SOUND_OFF = 0;
const int SOUND_SCARY = 1;
const int SOUND_TONE = 2;
const int SOUND_SEQUENCE = 3;
const int MAX_SOUND_SEQUENCE_STEPS = 64;

int soundMode = SOUND_OFF;
int soundFrequency = 120;
int soundVolume = 50;
bool speakerIsHigh = false;
unsigned long lastSoundStepMs = 0;
unsigned long lastSpeakerToggleUs = 0;
int soundSequenceFrequencies[MAX_SOUND_SEQUENCE_STEPS];
int soundSequenceDurations[MAX_SOUND_SEQUENCE_STEPS];
int soundSequenceVolumes[MAX_SOUND_SEQUENCE_STEPS];
int soundSequenceLength = 0;
int soundSequenceIndex = 0;
bool soundSequenceRepeat = false;
unsigned long soundSequenceStepStartMs = 0;

void writeMotorPin(bool isOn) {
  digitalWrite(MOTOR_PIN, isOn ? HIGH : LOW);
}

void stopMotor() {
  motorMode = MOTOR_OFF;
  motorSequenceRepeat = false;
  motorPwmHigh = false;
  writeMotorPin(false);
}

void setMotor(bool isOn) {
  motorMode = isOn ? MOTOR_ON : MOTOR_OFF;
  motorSpeed = isOn ? 100 : 0;
  motorPwmHigh = false;
  writeMotorPin(isOn);
}

void jiggleMotor() {
  writeMotorPin(true);
  delay(250);
  writeMotorPin(false);
}

void startMotorModulation(int speed, int spinMs, int pauseMs) {
  motorMode = MOTOR_MODULATE;
  motorSpeed = constrain(speed, 0, 100);
  motorSpinMs = constrain(spinMs, 10, 10000);
  motorPauseMs = constrain(pauseMs, 10, 10000);
  motorSpinPhase = true;
  motorPwmHigh = false;
  motorPhaseStartMs = millis();
  lastMotorPwmUs = 0;
}

bool isTruthy(String value) {
  value.toLowerCase();
  return value == "1" || value == "true" || value == "yes" || value == "on";
}

void startMotorSequence(String steps, bool repeat = false) {
  motorSequenceLength = 0;
  motorSequenceIndex = 0;
  motorSequenceRepeat = repeat;

  int start = 0;
  while (start < steps.length() && motorSequenceLength < MAX_MOTOR_SEQUENCE_STEPS) {
    int end = steps.indexOf(';', start);
    if (end == -1) {
      end = steps.length();
    }

    String step = steps.substring(start, end);
    int comma = step.indexOf(',');

    if (comma > 0) {
      motorSequencePowers[motorSequenceLength] = constrain(step.substring(0, comma).toInt(), 0, 100);
      motorSequenceDurations[motorSequenceLength] = constrain(step.substring(comma + 1).toInt(), 10, 10000);
      motorSequenceLength++;
    }

    start = end + 1;
  }

  if (motorSequenceLength == 0) {
    stopMotor();
    return;
  }

  motorMode = MOTOR_SEQUENCE;
  motorPwmHigh = false;
  motorSequenceStepStartMs = millis();
}

void applyMotorSpeed(int speed) {
  speed = constrain(speed, 0, 100);

  if (speed <= 0) {
    writeMotorPin(false);
    return;
  }

  if (speed >= 100) {
    writeMotorPin(true);
    return;
  }

  const unsigned long pwmPeriodUs = 5000;
  unsigned long highUs = pwmPeriodUs * speed / 100;
  unsigned long lowUs = pwmPeriodUs - highUs;
  unsigned long nowUs = micros();

  if (motorPwmHigh && nowUs - lastMotorPwmUs >= highUs) {
    motorPwmHigh = false;
    lastMotorPwmUs = nowUs;
    writeMotorPin(false);
  } else if (!motorPwmHigh && nowUs - lastMotorPwmUs >= lowUs) {
    motorPwmHigh = true;
    lastMotorPwmUs = nowUs;
    writeMotorPin(true);
  }
}

void updateMotor() {
  if (motorMode == MOTOR_OFF) {
    writeMotorPin(false);
    return;
  }

  if (motorMode == MOTOR_ON) {
    applyMotorSpeed(100);
    return;
  }

  if (motorMode == MOTOR_SEQUENCE) {
    unsigned long now = millis();
    if (now - motorSequenceStepStartMs >= (unsigned long)motorSequenceDurations[motorSequenceIndex]) {
      motorSequenceIndex++;

      if (motorSequenceIndex >= motorSequenceLength) {
        if (motorSequenceRepeat) {
          motorSequenceIndex = 0;
        } else {
          stopMotor();
          return;
        }
      }

      motorPwmHigh = false;
      motorSequenceStepStartMs = now;
      writeMotorPin(false);
    }

    applyMotorSpeed(motorSequencePowers[motorSequenceIndex]);
    return;
  }

  unsigned long now = millis();
  if (motorSpinPhase && now - motorPhaseStartMs >= (unsigned long)motorSpinMs) {
    motorSpinPhase = false;
    motorPwmHigh = false;
    motorPhaseStartMs = now;
    writeMotorPin(false);
  } else if (!motorSpinPhase && now - motorPhaseStartMs >= (unsigned long)motorPauseMs) {
    motorSpinPhase = true;
    motorPwmHigh = false;
    motorPhaseStartMs = now;
  }

  if (motorSpinPhase) {
    applyMotorSpeed(motorSpeed);
  } else {
    writeMotorPin(false);
  }
}

void startScarySound() {
  soundMode = SOUND_SCARY;
  soundFrequency = 120;
  soundVolume = 50;
  lastSoundStepMs = 0;
  lastSpeakerToggleUs = 0;
}

void stopScarySound() {
  soundMode = SOUND_OFF;
  soundSequenceRepeat = false;
  speakerIsHigh = false;
  digitalWrite(SPEAKER_PIN, LOW);
}

void startTone(int frequency, int volume) {
  soundFrequency = constrain(frequency, 50, 5000);
  soundVolume = constrain(volume, 0, 100);
  soundMode = soundVolume == 0 ? SOUND_OFF : SOUND_TONE;
  speakerIsHigh = false;
  lastSpeakerToggleUs = 0;
  digitalWrite(SPEAKER_PIN, LOW);
}

void startSoundSequence(String steps, bool repeat = false) {
  soundSequenceLength = 0;
  soundSequenceIndex = 0;
  soundSequenceRepeat = repeat;

  int start = 0;
  while (start < steps.length() && soundSequenceLength < MAX_SOUND_SEQUENCE_STEPS) {
    int end = steps.indexOf(';', start);
    if (end == -1) {
      end = steps.length();
    }

    String step = steps.substring(start, end);
    int firstComma = step.indexOf(',');
    int secondComma = step.indexOf(',', firstComma + 1);

    if (firstComma > 0 && secondComma > firstComma) {
      soundSequenceFrequencies[soundSequenceLength] = constrain(step.substring(0, firstComma).toInt(), 0, 5000);
      soundSequenceDurations[soundSequenceLength] = constrain(step.substring(firstComma + 1, secondComma).toInt(), 10, 10000);
      soundSequenceVolumes[soundSequenceLength] = constrain(step.substring(secondComma + 1).toInt(), 0, 100);
      soundSequenceLength++;
    }

    start = end + 1;
  }

  if (soundSequenceLength == 0) {
    stopScarySound();
    return;
  }

  soundMode = SOUND_SEQUENCE;
  soundFrequency = soundSequenceFrequencies[0];
  soundVolume = soundSequenceVolumes[0];
  soundSequenceStepStartMs = millis();
  speakerIsHigh = false;
  lastSpeakerToggleUs = 0;
  digitalWrite(SPEAKER_PIN, LOW);
}

void updateSquareWave() {
  if (soundMode == SOUND_OFF || soundFrequency <= 0 || soundVolume <= 0) {
    digitalWrite(SPEAKER_PIN, LOW);
    return;
  }

  unsigned long periodUs = 1000000UL / soundFrequency;
  unsigned long highUs = map(soundVolume, 1, 100, 10, periodUs / 2);
  unsigned long lowUs = periodUs - highUs;
  unsigned long nowUs = micros();

  if (speakerIsHigh && nowUs - lastSpeakerToggleUs >= highUs) {
    speakerIsHigh = false;
    lastSpeakerToggleUs = nowUs;
    digitalWrite(SPEAKER_PIN, LOW);
  } else if (!speakerIsHigh && nowUs - lastSpeakerToggleUs >= lowUs) {
    speakerIsHigh = true;
    lastSpeakerToggleUs = nowUs;
    digitalWrite(SPEAKER_PIN, HIGH);
  }
}

void updateSound() {
  if (soundMode == SOUND_SCARY) {
    unsigned long now = millis();
    if (now - lastSoundStepMs >= 20) {
      lastSoundStepMs = now;
      soundFrequency += 5;

      if (soundFrequency > 1800) {
        stopScarySound();
      }
    }
  } else if (soundMode == SOUND_SEQUENCE) {
    unsigned long now = millis();
    if (now - soundSequenceStepStartMs >= (unsigned long)soundSequenceDurations[soundSequenceIndex]) {
      soundSequenceIndex++;

      if (soundSequenceIndex >= soundSequenceLength) {
        if (soundSequenceRepeat) {
          soundSequenceIndex = 0;
          soundFrequency = soundSequenceFrequencies[soundSequenceIndex];
          soundVolume = soundSequenceVolumes[soundSequenceIndex];
          soundSequenceStepStartMs = now;
          speakerIsHigh = false;
          lastSpeakerToggleUs = 0;
          digitalWrite(SPEAKER_PIN, LOW);
        } else {
          stopScarySound();
        }
      } else {
        soundFrequency = soundSequenceFrequencies[soundSequenceIndex];
        soundVolume = soundSequenceVolumes[soundSequenceIndex];
        soundSequenceStepStartMs = now;
        speakerIsHigh = false;
        lastSpeakerToggleUs = 0;
        digitalWrite(SPEAKER_PIN, LOW);
      }
    }
  }

  if (soundMode == SOUND_OFF) {
    return;
  }

  updateSquareWave();
}

void handleHome() {
  server.send(200, "text/html",
    "<!doctype html>"
    "<html>"
    "<head>"
    "<meta name='viewport' content='width=device-width, initial-scale=1'>"
    "<style>"
    "body{font-family:Arial;text-align:center;margin-top:40px;background:#111;color:white;}"
    "button{font-size:24px;padding:18px 28px;margin:10px;border-radius:10px;border:0;}"
    "input{width:260px;}"
    ".on{background:#4caf50;color:white;}.off{background:#f44336;color:white;}.jiggle{background:#ff9800;color:white;}.sound{background:#673ab7;color:white;}.stopSound{background:#607d8b;color:white;}"
    "</style>"
    "</head>"
    "<body>"
    "<h1>ESP32 Motor Control</h1>"
    "<p><a href='/motor/on'><button class='on'>Motor On</button></a></p>"
    "<p><a href='/motor/off'><button class='off'>Motor Off</button></a></p>"
    "<p><a href='/motor/jiggle'><button class='jiggle'>Pulse Once</button></a></p>"
    "<form action='/motor/modulate'>"
    "<h2>Motor Modulation</h2>"
    "<p>Speed: <span id='motorSpeedValue'>100</span>%</p>"
    "<input type='range' name='speed' min='0' max='100' value='100' oninput='motorSpeedValue.textContent=this.value'>"
    "<p>Spin Duration: <span id='spinValue'>250</span> ms</p>"
    "<input type='range' name='spin' min='10' max='3000' value='250' oninput='spinValue.textContent=this.value'>"
    "<p>Pause Duration: <span id='pauseValue'>750</span> ms</p>"
    "<input type='range' name='pause' min='10' max='5000' value='750' oninput='pauseValue.textContent=this.value'>"
    "<p><button class='jiggle'>Start Motor Modulation</button></p>"
    "</form>"
    "<p><a href='/motor/modulate/stop'><button class='off'>Stop Motor Modulation</button></a></p>"
    "<p><a href='/sound/play'><button class='sound'>Play Sound</button></a></p>"
    "<p><a href='/sound/stop'><button class='stopSound'>Stop Sound</button></a></p>"
    "<form action='/tone/send'>"
    "<h2>Custom Tone</h2>"
    "<p>Frequency: <span id='freqValue'>400</span> Hz</p>"
    "<input type='range' name='frequency' min='50' max='5000' value='400' oninput='freqValue.textContent=this.value'>"
    "<p>Volume: <span id='volumeValue'>50</span>%</p>"
    "<input type='range' name='volume' min='0' max='100' value='50' oninput='volumeValue.textContent=this.value'>"
    "<p><button class='sound'>Send Tone</button></p>"
    "</form>"
    "</body>"
    "</html>"
  );
}

void handleMotorOn() {
  setMotor(true);
  server.sendHeader("Location", "/");
  server.send(303);
}

void handleMotorOff() {
  setMotor(false);
  server.sendHeader("Location", "/");
  server.send(303);
}

void handleMotorJiggle() {
  stopMotor();
  jiggleMotor();
  server.sendHeader("Location", "/");
  server.send(303);
}

void handleMotorModulate() {
  int speed = server.arg("speed").toInt();
  int spinMs = server.arg("spin").toInt();
  int pauseMs = server.arg("pause").toInt();
  startMotorModulation(speed, spinMs, pauseMs);
  server.sendHeader("Location", "/");
  server.send(303);
}

void handleMotorModulateStop() {
  stopMotor();
  server.sendHeader("Location", "/");
  server.send(303);
}

void handleMotorRun() {
  int power = server.arg("power").toInt();
  int durationMs = server.arg("duration").toInt();
  bool repeat = isTruthy(server.arg("repeat"));
  startMotorSequence(String(constrain(power, 0, 100)) + "," + String(constrain(durationMs, 10, 10000)), repeat);
  server.sendHeader("Location", "/");
  server.send(303);
}

void handleMotorSequence() {
  String steps = server.arg("steps");
  bool repeat = isTruthy(server.arg("repeat"));
  startMotorSequence(steps, repeat);
  server.sendHeader("Location", "/");
  server.send(303);
}

void handleSoundPlay() {
  startScarySound();
  server.sendHeader("Location", "/");
  server.send(303);
}

void handleSoundStop() {
  stopScarySound();
  server.sendHeader("Location", "/");
  server.send(303);
}

void handleToneSend() {
  int frequency = server.arg("frequency").toInt();
  int volume = server.arg("volume").toInt();
  startTone(frequency, volume);
  server.sendHeader("Location", "/");
  server.send(303);
}

void handleSoundSequence() {
  String steps = server.arg("steps");
  bool repeat = isTruthy(server.arg("repeat"));
  startSoundSequence(steps, repeat);
  server.sendHeader("Location", "/");
  server.send(303);
}

void handleComboSequence() {
  String motorSteps = server.arg("motor");
  String soundSteps = server.arg("sound");
  bool repeat = isTruthy(server.arg("repeat"));

  if (motorSteps.length() > 0) {
    startMotorSequence(motorSteps, repeat);
  }

  if (soundSteps.length() > 0) {
    startSoundSequence(soundSteps, repeat);
  }

  server.sendHeader("Location", "/");
  server.send(303);
}

void setup() {
  Serial.begin(115200);

  pinMode(SPEAKER_PIN, OUTPUT);
  pinMode(MOTOR_PIN, OUTPUT);
  setMotor(false);

  WiFi.begin(WIFI_NAME, WIFI_PASSWORD);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();
  Serial.print("Open this IP in your browser: ");
  Serial.println(WiFi.localIP());

  if (MDNS.begin("esp32-motor")) {
    Serial.println("Or open this URL: http://esp32-motor.local");
  }

  server.on("/", handleHome);
  server.on("/motor/on", handleMotorOn);
  server.on("/motor/off", handleMotorOff);
  server.on("/motor/jiggle", handleMotorJiggle);
  server.on("/motor/modulate", handleMotorModulate);
  server.on("/motor/modulate/stop", handleMotorModulateStop);
  server.on("/motor/run", handleMotorRun);
  server.on("/motor/sequence", handleMotorSequence);
  server.on("/sound/play", handleSoundPlay);
  server.on("/sound/stop", handleSoundStop);
  server.on("/sound/off", handleSoundStop);
  server.on("/tone/send", handleToneSend);
  server.on("/sound/sequence", handleSoundSequence);
  server.on("/combo/sequence", handleComboSequence);
  server.begin();
}

void loop() {
  server.handleClient();
  updateMotor();
  updateSound();
}
