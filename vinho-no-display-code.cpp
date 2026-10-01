#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <RTClib.h>
#include <EEPROM.h>
#include "DHT.h"

// ======================================================
// CONFIGURAÇÕES GERAIS
// ======================================================

#define SERIAL_OPTION 1
#define VERSAO_PROJETO "v10"

// ======================================================
// PINOS
// ======================================================

// DHT
#define DHTPIN 2

// Sensor físico: DHT11
// Sensor do simulador: DHT22
#define DHTTYPE DHT11

// Botões
#define BOTAO_ANTERIOR 8
#define BOTAO_PROXIMO  7
#define BOTAO_CONFIRMAR 6

// Buzzer
#define BUZZER_PIN 9

// LEDs
#define LED_VERDE 10
#define LED_AMARELO 11
#define LED_VERMELHO 12

// Sensores analógicos
#define LDR_PIN A0

// O divisor do LDR deste projeto apresenta leitura invertida:
// quanto maior a leitura do A0, menor a luminosidade.
const bool LDR_INVERTIDO = true;
#define POT_PIN A1

// ======================================================
// OBJETOS
// ======================================================

DHT dht(DHTPIN, DHTTYPE);

LiquidCrystal_I2C lcd(0x27, 16, 2);

RTC_DS1307 RTC;


// ======================================================
// EEPROM
// ======================================================
//
// ATmega328P = 1024 bytes de EEPROM.
//
// 99 registros x 10 bytes = 990 bytes
//
// Cada registro armazena timestamp, temperatura, umidade,
// luminosidade e as flags que identificam a anomalia.
//
// 34 bytes finais reservados para calibração + configurações.
//
// ======================================================

const int MAX_RECORDS = 99;

const int RECORD_SIZE = 10;

const int EEPROM_LOG_START = 0;

const int EEPROM_LOG_END =
  MAX_RECORDS * RECORD_SIZE;

// Configurações ficam depois dos registros
const int EEPROM_CALIBRATION_ADDRESS = 990;
const int EEPROM_SETTINGS_ADDRESS = 996;


// ======================================================
// ESTRUTURA DE CONFIGURAÇÃO
// ======================================================

struct Configuracao {

  uint16_t magic;

  byte idioma;

  float tempMin;
  float tempMax;

  float umidMin;
  float umidMax;

  float luzMin;
  float luzMax;
};

struct CalibracaoLDR {
  uint16_t magic;
  uint16_t escuro;
  uint16_t claro;
};

const uint16_t CALIBRACAO_MAGIC = 0x4C44;
CalibracaoLDR calibracaoLDR;


// Valor utilizado para verificar se a configuração
// existente na EEPROM é válida.
const uint16_t CONFIG_MAGIC = 0x4C47;


// Configuração atual
Configuracao config;


// ======================================================
// IDIOMAS
// ======================================================

enum Idioma {

  PT_BR = 0,
  EN_US = 1,
  ES_ES = 2
};


// ======================================================
// LOCALIZAÇÃO / UNIDADES / FUSO HORÁRIO
// ======================================================
// O RTC é mantido na referência-base do projeto: UTC-3.
// A interface converte data/hora para o fuso associado ao idioma.
// PT-BR: °C, DD/MM/YYYY, 24 h, UTC-3
// EN-US: °F, MM/DD/YYYY, 12 h, UTC-5
// ES-ES: °C, DD/MM/YYYY, 24 h, UTC+1
// ======================================================

const int FUSO_RTC_BASE = -3;

int fusoHorarioAtual() {
  if (config.idioma == EN_US) return -5;
  if (config.idioma == ES_ES) return 1;
  return -3;
}

bool usaFahrenheit() {
  return config.idioma == EN_US;
}

bool usaFormato12Horas() {
  return config.idioma == EN_US;
}

float celsiusParaFahrenheit(float valor) {
  return valor * 9.0 / 5.0 + 32.0;
}

float fahrenheitParaCelsius(float valor) {
  return (valor - 32.0) * 5.0 / 9.0;
}

float temperaturaExibicao(float valorCelsius) {
  if (isnan(valorCelsius)) return NAN;
  if (usaFahrenheit()) return celsiusParaFahrenheit(valorCelsius);
  return valorCelsius;
}

float temperaturaInterna(float valorExibicao) {
  if (usaFahrenheit()) return fahrenheitParaCelsius(valorExibicao);
  return valorExibicao;
}

DateTime horarioLocalAtual() {
  DateTime rtc = RTC.now();
  int diferencaHoras = fusoHorarioAtual() - FUSO_RTC_BASE;
  return rtc + TimeSpan(diferencaHoras * 3600L);
}

DateTime horarioRTCFromLocal(DateTime local) {
  int diferencaHoras = fusoHorarioAtual() - FUSO_RTC_BASE;
  return local - TimeSpan(diferencaHoras * 3600L);
}

void imprimirDoisDigitos(int valor) {
  if (valor < 10) lcd.print(F("0"));
  lcd.print(valor);
}

void imprimirData(DateTime dt) {
  if (config.idioma == EN_US) {
    imprimirDoisDigitos(dt.month()); lcd.print(F("/"));
    imprimirDoisDigitos(dt.day()); lcd.print(F("/"));
    lcd.print(dt.year());
  } else {
    imprimirDoisDigitos(dt.day()); lcd.print(F("/"));
    imprimirDoisDigitos(dt.month()); lcd.print(F("/"));
    lcd.print(dt.year());
  }
}

void imprimirHora(DateTime dt) {
  if (usaFormato12Horas()) {
    int hora = dt.hour();
    const char* periodo = "AM";
    if (hora >= 12) periodo = "PM";
    hora %= 12;
    if (hora == 0) hora = 12;
    imprimirDoisDigitos(hora); lcd.print(F(":"));
    imprimirDoisDigitos(dt.minute()); lcd.print(F(":"));
    imprimirDoisDigitos(dt.second()); lcd.print(F(" "));
    lcd.print(periodo);
  } else {
    imprimirDoisDigitos(dt.hour()); lcd.print(F(":"));
    imprimirDoisDigitos(dt.minute()); lcd.print(F(":"));
    imprimirDoisDigitos(dt.second());
  }
}

const __FlashStringHelper* textoTemperatura() { return F("TEMP"); }
const __FlashStringHelper* textoUmidade() {
  if (config.idioma == EN_US) return F("HUM");
  return F("UMID");
}
const __FlashStringHelper* textoLuz() {
  if (config.idioma == EN_US) return F("LIGHT");
  return F("LUZ");
}
const __FlashStringHelper* textoDia() {
  if (config.idioma == EN_US) return F("DAY");
  return F("DIA");
}
const __FlashStringHelper* textoMes() {
  if (config.idioma == EN_US) return F("MON");
  return F("MES");
}
const __FlashStringHelper* textoAno() {
  if (config.idioma == EN_US) return F("YEAR");
  return F("ANO");
}
const __FlashStringHelper* textoErro() {
  if (config.idioma == EN_US) return F("ERROR");
  return F("ERRO");
}
const __FlashStringHelper* textoAlerta() {
  if (config.idioma == EN_US) return F("ALERT!");
  return F("ALERTA!");
}
const __FlashStringHelper* textoStatusOK() {
  if (config.idioma == EN_US) return F("STATUS: OK");
  if (config.idioma == ES_ES) return F("ESTADO: OK");
  return F("STATUS: OK");
}
const __FlashStringHelper* textoDHTErro() {
  if (config.idioma == EN_US) return F("DHT ERROR");
  if (config.idioma == ES_ES) return F("ERROR DHT");
  return F("ERRO DHT");
}
const __FlashStringHelper* textoHoraAjustada() {
  if (config.idioma == EN_US) return F("TIME ADJUSTED");
  return F("HORA AJUSTADA");
}
const __FlashStringHelper* textoConfiguracoesSalvas() {
  if (config.idioma == EN_US) return F("SETTINGS");
  if (config.idioma == ES_ES) return F("CONFIGURACION");
  return F("CONFIGURACOES");
}
const __FlashStringHelper* textoSalvas() {
  if (config.idioma == EN_US) return F("SAVED!");
  if (config.idioma == ES_ES) return F("GUARDADA!");
  return F("SALVAS!");
}
const __FlashStringHelper* textoVoltarInicio() {
  if (config.idioma == EN_US) return F("BACK TO START?");
  if (config.idioma == ES_ES) return F("VOLVER AL INICIO?");
  return F("VOLTAR AO INICIO?");
}
const __FlashStringHelper* textoSim() {
  if (config.idioma == EN_US) return F("YES");
  if (config.idioma == ES_ES) return F("SI");
  return F("SIM");
}
const __FlashStringHelper* textoNao() {
  if (config.idioma == PT_BR) return F("NAO");
  if (config.idioma == EN_US) return F("NO");
  return F("NO");
}
const __FlashStringHelper* textoAjustePot() {
  if (config.idioma == EN_US) return F("TURN POT CONF");
  return F("GIRE POT CONF");
}
const __FlashStringHelper* textoUnidadeTemperatura() {
  if (usaFahrenheit()) return F("F");
  return F("C");
}



// ======================================================
// ESTADOS DO SISTEMA
// ======================================================

enum Estado {

  TELA_INICIAL,

  MENU_PRINCIPAL,

  TELA_LOGS,

  MENU_CONFIG,

  CONFIG_IDIOMA,

  CONFIG_DATA,

  CONFIG_HORA,

  CONFIG_TEMP,

  CONFIG_UMIDADE,

  CONFIG_LUZ,

  CONFIG_CALIBRACAO,

  CONFIG_SAIR
};


Estado estadoAtual = TELA_INICIAL;


// ======================================================
// VARIÁVEIS DE MENU
// ======================================================

int menuIndex = 0;

const int TOTAL_ITENS_MENU = 8;

int menuPrincipalIndex = 0;

const int TOTAL_ITENS_MENU_PRINCIPAL = 3;


// ======================================================
// VARIÁVEIS DOS SENSORES
// ======================================================

float temperature = NAN;

float humidity = NAN;

int lightPercent = 0;


// ======================================================
// CONTROLE DO DHT
// ======================================================

unsigned long lastDHTRead = 0;

const unsigned long DHT_INTERVAL = 3000;


// ======================================================
// CONTROLE DO LCD
// ======================================================

unsigned long lastLCDUpdate = 0;

const unsigned long LCD_INTERVAL = 3000;

byte paginaInicial = 0;


// ======================================================
// CONTROLE DE LOG
// ======================================================

int currentAddress = 0;

int logIndex = 0;

int totalLogs = 0;

bool logDetalhes = false;

// Bits que identificam o tipo de anomalia registrado.
const byte ANOMALIA_TEMPERATURA = 0x01;
const byte ANOMALIA_UMIDADE     = 0x02;
const byte ANOMALIA_LUZ         = 0x04;
const byte ANOMALIA_DHT         = 0x08;

// Guarda o último conjunto de anomalias que já foi registrado.
// Assim, uma anomalia persistente gera somente um registro.
byte flagsAnomaliaAnteriorLog = 0;

// Estado atual calculado uma vez por ciclo, em atualizarAlertas().
byte flagsAnomaliaAtual = 0;
byte flagsAnomaliaProcessada = 0xFF;


// ======================================================
// CONTROLE DOS ALERTAS
// ======================================================

bool estadoAlarmeAnterior = false;

unsigned long lastBuzzer = 0;


// ======================================================
// CONFIGURAÇÃO DE DATA
// ======================================================

byte dataCampo = 0;

int editDay;

int editMonth;

int editYear;


// ======================================================
// CONFIGURAÇÃO DE HORA
// ======================================================

byte horaCampo = 0;

int editHour;

int editMinute;


// ======================================================
// CONFIGURAÇÃO DOS TRIGGERS
// ======================================================

byte triggerCampo = 0;


// ======================================================
// TELA DE SAÍDA
// ======================================================

bool selecionarSim = true;


// ======================================================
// CARACTERES DA ANIMAÇÃO
// ======================================================

byte garrafa[8] = {

  B11111,
  B10001,
  B11111,
  B10001,
  B10001,
  B01010,
  B00100,
  B00100
};


byte vinho1[8] = {

  B00100,
  B01100,
  B01100,
  B00010,
  B00110,
  B00100,
  B01100,
  B00100
};


byte vinho2[8] = {

  B00100,
  B00110,
  B00110,
  B01000,
  B00110,
  B00100,
  B00110,
  B00100
};


byte taca[8] = {

  B10001,
  B10001,
  B10001,
  B01010,
  B00100,
  B00100,
  B00100,
  B01110
};


byte tacaMaisCheia[8] = {

  B10001,
  B10001,
  B11111,
  B01110,
  B00100,
  B00100,
  B00100,
  B01110
};


byte tacaCheia[8] = {

  B11111,
  B11111,
  B11111,
  B01110,
  B00100,
  B00100,
  B00100,
  B01110
};


void carregarConfiguracao();
void carregarCalibracaoLDR();
void salvarCalibracaoLDR();
void salvarConfiguracao();
void descobrirProximoEndereco();
void lerSensores();
void atualizarAlertas();
void registrarDados();
int contarLogs();
int obterEnderecoLogPorIndice(int);
void mostrarMenuPrincipal();
void imprimirDataHoraLog(DateTime);
void mostrarTipoAnomalia(byte);
void mostrarTelaLogs();
void tratarInterface();
int lerBotao(int);
int lerConfirmar();
void entrarNaConfiguracao();
void mostrarMenuConfig();
void mostrarConfigIdioma();
void iniciarConfiguracaoData();
void mostrarConfigData();
void ajustarData(int,int,int);
void alterarCampoData(int);
int diasNoMes(int,int);
void iniciarConfiguracaoHora();
void mostrarConfigHora();
void ajustarHora(int,int,int);
void mostrarConfigTemperatura();
void ajustarTriggerTemperatura(int,int,int);
void mostrarConfigUmidade();
void ajustarTriggerUmidade(int,int,int);
void mostrarConfigLuz();
void ajustarTriggerLuz(int,int,int);
void iniciarCalibracaoLDR();
void mostrarCalibracaoLDR();
void mostrarTelaSair();
void mostrarTelaInicial();
void atualizarTela();
void animacao();
void image00(); void image01(); void image02(); void image03(); void image04(); void image05(); void image06(); void image07(); void image08(); void image09();
void get_log();
bool temperaturaFora(); bool umidadeFora(); bool luminosidadeFora(); bool existeAlarme();
byte obterFlagsAnomaliaAtual();

const __FlashStringHelper* textoIdioma();
const __FlashStringHelper* textoData();
const __FlashStringHelper* textoHora();
const __FlashStringHelper* textoSair();
const __FlashStringHelper* textoCalibracao();
// ======================================================
// SETUP
// ======================================================

void setup() {

  // ----------------------------------------------------
  // SERIAL
  // ----------------------------------------------------

  Serial.begin(9600);


  // ----------------------------------------------------
  // PINOS
  // ----------------------------------------------------

  pinMode(BOTAO_ANTERIOR, INPUT_PULLUP);

  pinMode(BOTAO_PROXIMO, INPUT_PULLUP);

  pinMode(BOTAO_CONFIRMAR, INPUT_PULLUP);


  pinMode(BUZZER_PIN, OUTPUT);

  pinMode(LED_VERDE, OUTPUT);

  pinMode(LED_AMARELO, OUTPUT);

  pinMode(LED_VERMELHO, OUTPUT);


  digitalWrite(LED_VERDE, LOW);

  digitalWrite(LED_AMARELO, LOW);

  digitalWrite(LED_VERMELHO, LOW);


  // ----------------------------------------------------
  // DHT
  // ----------------------------------------------------

  dht.begin();

  delay(2000);


  // ----------------------------------------------------
  // LCD
  // ----------------------------------------------------

  lcd.init();

  lcd.backlight();


  // Caracteres da animação
  lcd.createChar(0, garrafa);

  lcd.createChar(1, vinho1);

  lcd.createChar(2, taca);

  lcd.createChar(3, tacaMaisCheia);

  lcd.createChar(4, tacaCheia);

  lcd.createChar(5, vinho2);


  // ----------------------------------------------------
  // RTC
  // ----------------------------------------------------

  if (!RTC.begin()) {

    lcd.clear();

    lcd.setCursor(0, 0);

    lcd.print(F("RTC ERROR"));

    Serial.println(F("ERRO: RTC nao encontrado!"));

    while (1);
  }


  // ----------------------------------------------------
  // RTC PARADO
  // ----------------------------------------------------

  if (!RTC.isrunning()) {

    Serial.println(F("RTC parado."));

    Serial.println(F("Ajustando para data/hora da compilacao."));

    RTC.adjust(DateTime(F(__DATE__), F(__TIME__)));

    lcd.clear();

    lcd.setCursor(0, 0);

    lcd.print(F("RTC ADJUSTED"));

    delay(1500);
  }


  // ----------------------------------------------------
  // CARREGA CONFIGURAÇÕES
  // ----------------------------------------------------

  carregarConfiguracao();
  carregarCalibracaoLDR();


  // ----------------------------------------------------
  // DESCOBRE PRÓXIMO ENDEREÇO DA EEPROM
  // ----------------------------------------------------

  descobrirProximoEndereco();

  // O LDR começa sempre em 0%.
  // Se o sensor não estiver conectado, a rotina lerSensores()
  // também mantém esse valor em 0%.
  lightPercent = 0;

  // Primeira leitura imediata para evitar que NAN seja
  // interpretado como uma falha antes dos 3 segundos.
  lastDHTRead = millis() - DHT_INTERVAL;
  lerSensores();
  atualizarAlertas();


  // ----------------------------------------------------
  // ANIMAÇÃO INICIAL
  // ----------------------------------------------------

  animacao();


  // ----------------------------------------------------
  // TELA INICIAL
  // ----------------------------------------------------

  mostrarTelaInicial();

}


// ======================================================
// LOOP PRINCIPAL
// ======================================================

void loop() {

  // ====================================================
  // LEITURA DOS SENSORES
  // ====================================================

  lerSensores();


  // ====================================================
  // CONTROLE DOS ALERTAS
  // ====================================================

  atualizarAlertas();


  // ====================================================
  // REGISTRO NA EEPROM
  // ====================================================
  // A EEPROM só é consultada/escrita quando o estado da
  // anomalia muda. Isso evita trabalho desnecessário.

  if (flagsAnomaliaAtual != flagsAnomaliaProcessada) {
    registrarDados();
    flagsAnomaliaProcessada = flagsAnomaliaAtual;
  }


  // ====================================================
  // TRATAMENTO DOS BOTÕES
  // ====================================================

  tratarInterface();


  // ====================================================
  // ATUALIZAÇÃO DA TELA
  // ====================================================

  atualizarTela();


  delay(20);
}


// ======================================================
// CARREGAR CONFIGURAÇÃO DA EEPROM
// ======================================================

void carregarConfiguracao() {

  EEPROM.get(
    EEPROM_SETTINGS_ADDRESS,
    config
  );


  // Verifica se os dados são válidos

  if (config.magic != CONFIG_MAGIC) {

    Serial.println(F("Configuracao inexistente."));

    // --------------------------------------------------
    // VALORES PADRÃO DO PROJETO
    // --------------------------------------------------

    config.magic = CONFIG_MAGIC;

    config.idioma = PT_BR;

    // Temperatura:
    // 15 < T < 25
    config.tempMin = 15.0;

    config.tempMax = 25.0;

    // Umidade:
    // 30 < U < 50
    config.umidMin = 30.0;

    config.umidMax = 50.0;

    // Luminosidade:
    // 0 < L < 30
    config.luzMin = 0.0;

    config.luzMax = 30.0;


    salvarConfiguracao();
  }
}


// ======================================================
// CARREGAR CALIBRAÇÃO DO LDR
// ======================================================

void carregarCalibracaoLDR() {

  EEPROM.get(
    EEPROM_CALIBRATION_ADDRESS,
    calibracaoLDR
  );

  bool calibracaoValida = false;

  if (calibracaoLDR.magic == CALIBRACAO_MAGIC) {

    if (LDR_INVERTIDO) {
      calibracaoValida =
        calibracaoLDR.escuro > calibracaoLDR.claro;
    } else {
      calibracaoValida =
        calibracaoLDR.escuro < calibracaoLDR.claro;
    }
  }

  if (!calibracaoValida) {

    calibracaoLDR.magic = CALIBRACAO_MAGIC;

    if (LDR_INVERTIDO) {
      calibracaoLDR.escuro = 1023;
      calibracaoLDR.claro = 0;
    } else {
      calibracaoLDR.escuro = 0;
      calibracaoLDR.claro = 1023;
    }

    EEPROM.put(
      EEPROM_CALIBRATION_ADDRESS,
      calibracaoLDR
    );
  }
}

void salvarCalibracaoLDR() {
  calibracaoLDR.magic = CALIBRACAO_MAGIC;
  EEPROM.put(EEPROM_CALIBRATION_ADDRESS, calibracaoLDR);
}

// ======================================================
// SALVAR CONFIGURAÇÃO
// ======================================================

void salvarConfiguracao() {

  config.magic = CONFIG_MAGIC;

  EEPROM.put(
    EEPROM_SETTINGS_ADDRESS,
    config
  );


  if (SERIAL_OPTION) {

    Serial.println();

    if (config.idioma == EN_US) Serial.println(F("Settings saved."));
    else Serial.println(F("Configuracoes salvas."));

    if (config.idioma == EN_US) Serial.print(F("Language: "));
    else Serial.print(F("Idioma: "));

    Serial.println(config.idioma);

    Serial.print(F("Temp: "));

    Serial.print(config.tempMin);

    Serial.print(F(" a "));

    Serial.println(config.tempMax);

    if (config.idioma == EN_US) Serial.print(F("Hum: "));
    else Serial.print(F("Umid: "));

    Serial.print(config.umidMin);

    Serial.print(F(" a "));

    Serial.println(config.umidMax);

    if (config.idioma == EN_US) Serial.print(F("Light: "));
    else Serial.print(F("Luz: "));

    Serial.print(config.luzMin);

    Serial.print(F(" a "));

    Serial.println(config.luzMax);
  }
}


// ======================================================
// DESCOBRIR PRÓXIMO ENDEREÇO DA EEPROM
// ======================================================

void descobrirProximoEndereco() {

  uint32_t maiorTimestamp = 0;

  int enderecoMaisRecente = -1;


  for (
    int address = EEPROM_LOG_START;
    address < EEPROM_LOG_END;
    address += RECORD_SIZE
  ) {

    uint32_t timestamp;

    EEPROM.get(
      address,
      timestamp
    );


    if (timestamp != 0xFFFFFFFFUL &&
        timestamp != 0) {

      if (
        enderecoMaisRecente == -1 ||
        timestamp > maiorTimestamp
      ) {

        maiorTimestamp = timestamp;

        enderecoMaisRecente = address;
      }
    }
  }


  // Nenhum registro
  if (enderecoMaisRecente == -1) {

    currentAddress = EEPROM_LOG_START;

    return;
  }


  currentAddress =
    enderecoMaisRecente + RECORD_SIZE;


  // EEPROM cheia
  if (currentAddress >= EEPROM_LOG_END) {

    currentAddress = EEPROM_LOG_START;
  }
}


// ======================================================
// LEITURA DOS SENSORES
// ======================================================

void lerSensores() {

  // ----------------------------------------------------
  // DHT
  // ----------------------------------------------------

  if (
    millis() - lastDHTRead >= DHT_INTERVAL
  ) {

    lastDHTRead = millis();


    float newHumidity =
      dht.readHumidity();


    float newTemperature =
      dht.readTemperature();


    if (
      isnan(newHumidity) ||
      isnan(newTemperature)
    ) {

      // Marca a falha para que o LED amarelo e o sistema
      // de logs reconheçam a anomalia.
      humidity = NAN;
      temperature = NAN;

      Serial.println(
        "ERRO: falha na leitura DHT"
      );

    } else {

      humidity = newHumidity;

      temperature = newTemperature;


      if (SERIAL_OPTION) {

        if (config.idioma == EN_US) Serial.print(F("Temperature: "));
        else Serial.print(F("Temperatura: "));
        Serial.print(temperaturaExibicao(temperature), 1);
        Serial.print(F(" ")); Serial.print(textoUnidadeTemperatura());
        if (config.idioma == EN_US) Serial.print(F(" | Humidity: "));
        else if (config.idioma == ES_ES) Serial.print(F(" | Humedad: "));
        else Serial.print(F(" | Umidade: "));

        Serial.print(humidity);

        Serial.println(F(" %"));
      }
    }
  }


  // ----------------------------------------------------
  // LDR
  // ----------------------------------------------------

  int rawLDR =
    analogRead(LDR_PIN);


  // ----------------------------------------------------
  // TRATAMENTO DO LDR INVERTIDO
  // ----------------------------------------------------
  // Neste circuito:
  //
  //   A0 alto  -> pouca/nenhuma luz -> 0%
  //   A0 baixo -> muita luz         -> 100%
  //
  // A calibracao tambem usa essa orientacao.
  // ----------------------------------------------------

  const int LDR_ALTO_SEM_LUZ = 1018;

  if (LDR_INVERTIDO) {

    if (rawLDR >= LDR_ALTO_SEM_LUZ) {

      lightPercent = 0;

    } else if (
      calibracaoLDR.escuro != calibracaoLDR.claro
    ) {

      lightPercent =
        map(
          rawLDR,
          calibracaoLDR.escuro,
          calibracaoLDR.claro,
          0,
          100
        );

      lightPercent =
        constrain(
          lightPercent,
          0,
          100
        );

    } else {

      lightPercent =
        map(
          rawLDR,
          1023,
          0,
          0,
          100
        );

      lightPercent =
        constrain(
          lightPercent,
          0,
          100
        );
    }

  } else {

    if (rawLDR <= 5) {

      lightPercent = 0;

    } else if (
      calibracaoLDR.escuro != calibracaoLDR.claro
    ) {

      lightPercent =
        map(
          rawLDR,
          calibracaoLDR.escuro,
          calibracaoLDR.claro,
          0,
          100
        );

      lightPercent =
        constrain(
          lightPercent,
          0,
          100
        );

    } else {

      lightPercent =
        map(
          rawLDR,
          0,
          1023,
          0,
          100
        );

      lightPercent =
        constrain(
          lightPercent,
          0,
          100
        );
    }
  }


// ======================================================
} 


// VERIFICA SE ESTÁ FORA DOS LIMITES
// ======================================================

bool temperaturaFora() {

  if (isnan(temperature))
    return false;


  return (
    temperature < config.tempMin ||
    temperature > config.tempMax
  );
}


bool umidadeFora() {

  if (isnan(humidity))
    return false;


  return (
    humidity < config.umidMin ||
    humidity > config.umidMax
  );
}


bool luminosidadeFora() {

  return (
    lightPercent < config.luzMin ||
    lightPercent > config.luzMax
  );
}


bool existeAlarme() {

  if (
    temperaturaFora() ||
    umidadeFora() ||
    luminosidadeFora()
  ) {

    return true;
  }


  return false;
}


// ======================================================
// IDENTIFICAÇÃO DA ANOMALIA ATUAL
// ======================================================

byte obterFlagsAnomaliaAtual() {

  byte flags = 0;

  if (isnan(temperature) || isnan(humidity)) {
    flags |= ANOMALIA_DHT;
    return flags;
  }

  if (temperaturaFora())
    flags |= ANOMALIA_TEMPERATURA;

  if (umidadeFora())
    flags |= ANOMALIA_UMIDADE;

  if (luminosidadeFora())
    flags |= ANOMALIA_LUZ;

  return flags;
}


// ======================================================
// LEDs E BUZZER
// ======================================================

void atualizarAlertas() {

  // Calcula as flags uma única vez por ciclo.
  flagsAnomaliaAtual = obterFlagsAnomaliaAtual();

  bool alarmeAtual =
    (flagsAnomaliaAtual &
     (ANOMALIA_TEMPERATURA | ANOMALIA_UMIDADE | ANOMALIA_LUZ)) != 0;


  // ----------------------------------------------------
  // DHT com erro
  // ----------------------------------------------------

  if (flagsAnomaliaAtual & ANOMALIA_DHT) {

    digitalWrite(LED_VERDE, LOW);
    digitalWrite(LED_AMARELO, HIGH);
    digitalWrite(LED_VERMELHO, LOW);

    // Falha do DHT não aciona o buzzer de alarme.
    estadoAlarmeAnterior = false;
    return;
  }


  // ----------------------------------------------------
  // NORMAL
  // ----------------------------------------------------

  if (!alarmeAtual) {

    digitalWrite(LED_VERDE, HIGH);
    digitalWrite(LED_AMARELO, LOW);
    digitalWrite(LED_VERMELHO, LOW);
  }


  // ----------------------------------------------------
  // ALARME
  // ----------------------------------------------------

  else {

    digitalWrite(LED_VERDE, LOW);
    digitalWrite(LED_AMARELO, LOW);
    digitalWrite(LED_VERMELHO, HIGH);

    // Bipe somente na entrada do estado de alarme.
    if (!estadoAlarmeAnterior && alarmeAtual) {
      tone(BUZZER_PIN, 2000, 300);
      lastBuzzer = millis();
    }
  }

  estadoAlarmeAnterior = alarmeAtual;
}


// ======================================================
// REGISTRO NA EEPROM
// ======================================================
//
// Somente eventos de anomalia são armazenados.
// Uma anomalia persistente não é gravada novamente.
// Quando a EEPROM chega ao limite, currentAddress aponta
// para a posição que deve ser sobrescrita, substituindo
// o registro mais antigo.
//
// Formato de 10 bytes por registro:
// 0..3  = timestamp UTC
// 4..5  = temperatura x100
// 6..7  = umidade x100
// 8     = luminosidade 0..100
// 9     = flags da anomalia
//
// ======================================================

void registrarDados() {

  byte flagsAtuais = flagsAnomaliaAtual;

  // Nenhuma anomalia: libera o próximo evento.
  if (flagsAtuais == 0) {
    flagsAnomaliaAnteriorLog = 0;
    return;
  }

  // Enquanto o sistema continua em estado de anomalia,
  // não cria outro registro. Só uma nova transição
  // NORMAL -> ANOMALIA gera um novo log.
  if (flagsAnomaliaAnteriorLog != 0) {
    return;
  }

  DateTime agora = horarioLocalAtual();

  int16_t tempInt = 0;
  int16_t humiInt = 0;

  if (!isnan(temperature))
    tempInt = (int16_t)(temperature * 100.0);

  if (!isnan(humidity))
    humiInt = (int16_t)(humidity * 100.0);

  uint8_t lightInt = (uint8_t)constrain(lightPercent, 0, 100);

  uint32_t timestamp =
    horarioRTCFromLocal(agora).unixtime();

  // currentAddress aponta sempre para a próxima posição.
  // Quando a EEPROM está cheia, essa posição é a do
  // registro mais antigo e ele é sobrescrito.
  EEPROM.put(currentAddress, timestamp);
  EEPROM.put(currentAddress + 4, tempInt);
  EEPROM.put(currentAddress + 6, humiInt);
  EEPROM.put(currentAddress + 8, lightInt);
  EEPROM.put(currentAddress + 9, flagsAtuais);

  flagsAnomaliaAnteriorLog = flagsAtuais;

  if (SERIAL_OPTION) {
    Serial.println();
    if (config.idioma == EN_US)
      Serial.println(F("===== NEW ANOMALY LOG ====="));
    else
      Serial.println(F("===== NOVA ANOMALIA ====="));

    if (config.idioma == EN_US) Serial.print(F("Address: "));
    else Serial.print(F("Endereco: "));
    Serial.println(currentAddress);

    Serial.print(F("Timestamp: "));
    Serial.println(timestamp);

    if (config.idioma == EN_US) Serial.print(F("Temperature: "));
    else Serial.print(F("Temperatura: "));
    if (isnan(temperature)) Serial.print(F("ERROR"));
    else {
      Serial.print(temperaturaExibicao(temperature), 1);
      Serial.print(F(" "));
      Serial.print(textoUnidadeTemperatura());
    }
    Serial.println();

    if (config.idioma == EN_US) Serial.print(F("Humidity: "));
    else Serial.print(F("Umidade: "));
    if (isnan(humidity)) Serial.print(F("ERROR"));
    else {
      Serial.print(humidity, 1);
      Serial.print(F(" %"));
    }
    Serial.println();

    if (config.idioma == EN_US) Serial.print(F("Light: "));
    else Serial.print(F("Luminosidade: "));
    Serial.print(lightPercent);
    Serial.println(F(" %"));

    if (config.idioma == EN_US) Serial.print(F("Anomaly flags: "));
    else Serial.print(F("Flags: "));
    Serial.println(flagsAtuais);
    Serial.println(F("==========================="));
  }

  currentAddress += RECORD_SIZE;
  if (currentAddress >= EEPROM_LOG_END)
    currentAddress = EEPROM_LOG_START;
}


// ======================================================
// CONTROLE E LEITURA DOS LOGS
// ======================================================

int contarLogs() {

  int quantidade = 0;

  for (int address = EEPROM_LOG_START;
       address < EEPROM_LOG_END;
       address += RECORD_SIZE) {

    uint32_t timestamp;
    EEPROM.get(address, timestamp);

    if (timestamp != 0 && timestamp != 0xFFFFFFFFUL)
      quantidade++;
  }

  return quantidade;
}


int obterEnderecoLogPorIndice(int indice) {

  // indice 0 = registro mais recente.
  uint32_t timestampAnterior = 0xFFFFFFFFUL;
  int enderecoEncontrado = -1;

  for (int passo = 0; passo <= indice; passo++) {

    uint32_t maiorTimestamp = 0;
    int melhorEndereco = -1;

    for (int address = EEPROM_LOG_START;
         address < EEPROM_LOG_END;
         address += RECORD_SIZE) {

      uint32_t timestamp;
      EEPROM.get(address, timestamp);

      if (timestamp == 0 || timestamp == 0xFFFFFFFFUL)
        continue;

      if (timestamp >= timestampAnterior)
        continue;

      if (timestamp > maiorTimestamp) {
        maiorTimestamp = timestamp;
        melhorEndereco = address;
      }
    }

    if (melhorEndereco < 0)
      return -1;

    enderecoEncontrado = melhorEndereco;
    timestampAnterior = maiorTimestamp;
  }

  return enderecoEncontrado;
}


const __FlashStringHelper* textoMenuLogs() { return F("LOGS"); }


const __FlashStringHelper* textoMenuConfiguracao() {
  if (config.idioma == EN_US) return F("CONFIGURATION");
  if (config.idioma == ES_ES) return F("CONFIGURACION");
  return F("CONFIGURACAO");
}


const __FlashStringHelper* textoMenuSair() {
  if (config.idioma == EN_US) return F("EXIT");
  if (config.idioma == ES_ES) return F("SALIR");
  return F("SAIR");
}


const __FlashStringHelper* textoSemLogs() {
  if (config.idioma == EN_US) return F("NO LOGS");
  if (config.idioma == ES_ES) return F("SIN LOGS");
  return F("SEM LOGS");
}


const __FlashStringHelper* textoDetalhesLog() {
  if (config.idioma == EN_US) return F("DETAILS");
  if (config.idioma == ES_ES) return F("DETALLES");
  return F("DETALHES");
}


void mostrarMenuPrincipal() {

  lcd.clear();

  // O menu possui 3 opções. Como o LCD tem apenas 2 linhas,
  // são exibidas a opção selecionada e a próxima opção.
  if (menuPrincipalIndex == 0) {

    lcd.setCursor(0, 0);
    lcd.print(F("> "));
    lcd.print(textoMenuLogs());

    lcd.setCursor(0, 1);
    lcd.print(F("  "));
    lcd.print(textoMenuConfiguracao());
  }

  else if (menuPrincipalIndex == 1) {

    lcd.setCursor(0, 0);
    lcd.print(F("> "));
    lcd.print(textoMenuConfiguracao());

    lcd.setCursor(0, 1);
    lcd.print(F("  "));
    lcd.print(textoMenuSair());
  }

  else {

    lcd.setCursor(0, 0);
    lcd.print(F("> "));
    lcd.print(textoMenuSair());

    lcd.setCursor(0, 1);
    lcd.print(F("  "));
    lcd.print(textoMenuLogs());
  }
}


void imprimirDataHoraLog(DateTime dt) {

  // Formato compacto para caber no LCD 16x2.
  if (config.idioma == EN_US) {
    imprimirDoisDigitos(dt.month());
    lcd.print(F("/"));
    imprimirDoisDigitos(dt.day());
  }
  else {
    imprimirDoisDigitos(dt.day());
    lcd.print(F("/"));
    imprimirDoisDigitos(dt.month());
  }

  lcd.print(F("/"));
  lcd.print(dt.year() % 100);
  lcd.print(F(" "));

  if (usaFormato12Horas()) {
    int hora = dt.hour();
    const char* periodo = hora >= 12 ? "P" : "A";
    hora %= 12;
    if (hora == 0) hora = 12;
    imprimirDoisDigitos(hora);
    lcd.print(F(":"));
    imprimirDoisDigitos(dt.minute());
    lcd.print(periodo);
  }
  else {
    imprimirDoisDigitos(dt.hour());
    lcd.print(F(":"));
    imprimirDoisDigitos(dt.minute());
  }
}


void mostrarTipoAnomalia(byte flags) {

  lcd.print(F("ANOM: "));

  if (flags == ANOMALIA_TEMPERATURA) {
    lcd.print(config.idioma == EN_US ? "TEMP" : "TEMP");
    return;
  }

  if (flags == ANOMALIA_UMIDADE) {
    lcd.print(config.idioma == EN_US ? "HUM" : "UMID");
    return;
  }

  if (flags == ANOMALIA_LUZ) {
    lcd.print(config.idioma == EN_US ? "LIGHT" : "LUZ");
    return;
  }

  if (flags == ANOMALIA_DHT) {
    lcd.print(F("DHT"));
    return;
  }

  lcd.print(config.idioma == EN_US ? "MULTI" : "MULTIPLA");
}


void mostrarTelaLogs() {

  totalLogs = contarLogs();

  if (totalLogs == 0) {

    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(textoSemLogs());
    lcd.setCursor(0, 1);
    lcd.print(config.idioma == EN_US ? "HOLD=BACK" : "SEG=VOLTAR");
    return;
  }

  if (logIndex >= totalLogs)
    logIndex = totalLogs - 1;

  if (logIndex < 0)
    logIndex = 0;

  int address = obterEnderecoLogPorIndice(logIndex);

  if (address < 0) {
    lcd.clear();
    lcd.print(textoSemLogs());
    return;
  }

  uint32_t timestamp;
  int16_t tempInt;
  int16_t humiInt;
  uint8_t lightInt;
  byte flags;

  EEPROM.get(address, timestamp);
  EEPROM.get(address + 4, tempInt);
  EEPROM.get(address + 6, humiInt);
  EEPROM.get(address + 8, lightInt);
  EEPROM.get(address + 9, flags);

  DateTime dtUTC(timestamp);
  DateTime dtLocal = dtUTC + TimeSpan((fusoHorarioAtual() - FUSO_RTC_BASE) * 3600L);

  lcd.clear();

  if (!logDetalhes) {

    lcd.setCursor(0, 0);
    lcd.print(F("LOG "));
    lcd.print(logIndex + 1);
    lcd.print(F("/"));
    lcd.print(totalLogs);

    lcd.setCursor(0, 1);
    imprimirDataHoraLog(dtLocal);
  }
  else {

    lcd.setCursor(0, 0);
    lcd.print(F("T:"));
    if (flags & ANOMALIA_DHT) {
      lcd.print(F("ERR"));
    }
    else {
      lcd.print(temperaturaExibicao(tempInt / 100.0), 1);
      lcd.print(textoUnidadeTemperatura());
    }

    lcd.print(F(" U:"));
    if (flags & ANOMALIA_DHT) {
      lcd.print(F("ERR"));
    }
    else {
      lcd.print(humiInt / 100.0, 0);
      lcd.print(F("%"));
    }

    lcd.setCursor(0, 1);
    lcd.print(F("L:"));
    lcd.print(lightInt);
    lcd.print(F("% "));
    mostrarTipoAnomalia(flags);
  }
}


// ======================================================
// TRATAMENTO DA INTERFACE
// ======================================================

void tratarInterface() {

  int eventoAnterior = lerBotao(BOTAO_ANTERIOR);
  int eventoProximo = lerBotao(BOTAO_PROXIMO);
  int eventoConfirmar = lerConfirmar();

  // ====================================================
  // TELA INICIAL
  // ====================================================

  if (estadoAtual == TELA_INICIAL) {

    if (eventoAnterior) {
      if (paginaInicial == 0) paginaInicial = 2;
      else paginaInicial--;
      mostrarTelaInicial();
      return;
    }

    if (eventoProximo) {
      paginaInicial++;
      if (paginaInicial > 2) paginaInicial = 0;
      mostrarTelaInicial();
      return;
    }

    // CONFIRMAR LONGO = MENU PRINCIPAL
    if (eventoConfirmar == 2) {
      estadoAtual = MENU_PRINCIPAL;
      menuPrincipalIndex = 0;
      mostrarMenuPrincipal();
      return;
    }
  }

  // ====================================================
  // MENU PRINCIPAL: LOGS / CONFIGURACAO
  // ====================================================

  else if (estadoAtual == MENU_PRINCIPAL) {

    if (eventoAnterior) {
      menuPrincipalIndex--;
      if (menuPrincipalIndex < 0)
        menuPrincipalIndex = TOTAL_ITENS_MENU_PRINCIPAL - 1;
      mostrarMenuPrincipal();
      return;
    }

    if (eventoProximo) {
      menuPrincipalIndex++;
      if (menuPrincipalIndex >= TOTAL_ITENS_MENU_PRINCIPAL)
        menuPrincipalIndex = 0;
      mostrarMenuPrincipal();
      return;
    }

    if (eventoConfirmar == 1) {

      if (menuPrincipalIndex == 0) {

        totalLogs = contarLogs();
        logIndex = 0;
        logDetalhes = false;
        estadoAtual = TELA_LOGS;
        mostrarTelaLogs();
      }

      else if (menuPrincipalIndex == 1) {

        menuIndex = 0;
        estadoAtual = MENU_CONFIG;
        mostrarMenuConfig();
      }

      else {

        // SAIR: retorna para a tela inicial.
        estadoAtual = TELA_INICIAL;
        paginaInicial = 0;
        mostrarTelaInicial();
      }

      return;
    }

    if (eventoConfirmar == 2) {
      estadoAtual = TELA_INICIAL;
      paginaInicial = 0;
      mostrarTelaInicial();
      return;
    }
  }

  // ====================================================
  // TELA DE LOGS
  // ====================================================

  else if (estadoAtual == TELA_LOGS) {

    if (eventoAnterior) {
      if (totalLogs > 0) {
        logIndex++;
        if (logIndex >= totalLogs) logIndex = 0;
        logDetalhes = false;
        mostrarTelaLogs();
      }
      return;
    }

    if (eventoProximo) {
      if (totalLogs > 0) {
        logIndex--;
        if (logIndex < 0) logIndex = totalLogs - 1;
        logDetalhes = false;
        mostrarTelaLogs();
      }
      return;
    }

    if (eventoConfirmar == 1) {
      if (totalLogs > 0) {
        logDetalhes = !logDetalhes;
        mostrarTelaLogs();
      }
      return;
    }

    // Confirmar longo = voltar ao menu principal.
    if (eventoConfirmar == 2) {
      estadoAtual = MENU_PRINCIPAL;
      menuPrincipalIndex = 0;
      mostrarMenuPrincipal();
      return;
    }
  }

  // ====================================================
  // MENU DE CONFIGURACAO
  // ====================================================

  else if (estadoAtual == MENU_CONFIG) {

    if (eventoAnterior) {
      menuIndex--;
      if (menuIndex < 0) menuIndex = TOTAL_ITENS_MENU - 1;
      mostrarMenuConfig();
      return;
    }

    if (eventoProximo) {
      menuIndex++;
      if (menuIndex >= TOTAL_ITENS_MENU) menuIndex = 0;
      mostrarMenuConfig();
      return;
    }

    if (eventoConfirmar == 1) {
      entrarNaConfiguracao();
      return;
    }

    if (eventoConfirmar == 2) {
      estadoAtual = MENU_PRINCIPAL;
      menuPrincipalIndex = 1;
      mostrarMenuPrincipal();
      return;
    }
  }

  // ====================================================
  // IDIOMA
  // ====================================================

  else if (estadoAtual == CONFIG_IDIOMA) {

    if (eventoAnterior) {
      if (config.idioma == 0) config.idioma = 2;
      else config.idioma--;
      mostrarConfigIdioma();
      return;
    }

    if (eventoProximo) {
      config.idioma++;
      if (config.idioma > 2) config.idioma = 0;
      mostrarConfigIdioma();
      return;
    }

    if (eventoConfirmar == 1) {
      salvarConfiguracao();
      estadoAtual = MENU_CONFIG;
      mostrarMenuConfig();
      return;
    }
  }

  // ====================================================
  // DATA
  // ====================================================

  else if (estadoAtual == CONFIG_DATA) {
    ajustarData(eventoAnterior, eventoProximo, eventoConfirmar);
    return;
  }

  // ====================================================
  // HORA
  // ====================================================

  else if (estadoAtual == CONFIG_HORA) {
    ajustarHora(eventoAnterior, eventoProximo, eventoConfirmar);
    return;
  }

  // ====================================================
  // TEMPERATURA
  // ====================================================

  else if (estadoAtual == CONFIG_TEMP) {
    ajustarTriggerTemperatura(eventoAnterior, eventoProximo, eventoConfirmar);
    return;
  }

  // ====================================================
  // UMIDADE
  // ====================================================

  else if (estadoAtual == CONFIG_UMIDADE) {
    ajustarTriggerUmidade(eventoAnterior, eventoProximo, eventoConfirmar);
    return;
  }

  // ====================================================
  // LUMINOSIDADE
  // ====================================================

  else if (estadoAtual == CONFIG_LUZ) {
    ajustarTriggerLuz(eventoAnterior, eventoProximo, eventoConfirmar);
    return;
  }

  // ====================================================
  // CALIBRACAO DO LDR
  // ====================================================

  else if (estadoAtual == CONFIG_CALIBRACAO) {
    if (eventoConfirmar == 1 || eventoConfirmar == 2) {
      estadoAtual = MENU_CONFIG;
      mostrarMenuConfig();
    }
    return;
  }

  // ====================================================
  // SAIR DA CONFIGURACAO
  // ====================================================

  else if (estadoAtual == CONFIG_SAIR) {

    if (eventoAnterior || eventoProximo) {
      selecionarSim = !selecionarSim;
      mostrarTelaSair();
      return;
    }

    if (eventoConfirmar == 1) {

      if (selecionarSim) {

        salvarConfiguracao();

        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print(textoConfiguracoesSalvas());
        lcd.setCursor(0, 1);
        lcd.print(textoSalvas());
        delay(1200);

        estadoAtual = TELA_INICIAL;
        paginaInicial = 0;
        mostrarTelaInicial();
      }
      else {
        estadoAtual = MENU_CONFIG;
        mostrarMenuConfig();
      }
      return;
    }
  }
}

// ======================================================
// LEITURA DOS BOTÕES
// ======================================================
//
// Retorna:
// 0 = nada
// 1 = pressionado
//
// ======================================================

int lerBotao(int pin) {

  if (
    digitalRead(pin) ==
    LOW
  ) {

    delay(30);


    if (
      digitalRead(pin) ==
      LOW
    ) {

      while (
        digitalRead(pin) ==
        LOW
      ) {
        delay(5);
      }

      return 1;
    }
  }


  return 0;
}


// ======================================================
// BOTÃO CONFIRMAR
// ======================================================
//
// Retorna:
// 0 = nada
// 1 = toque curto
// 2 = toque longo
//
// ======================================================

int lerConfirmar() {

  if (
    digitalRead(BOTAO_CONFIRMAR) !=
    LOW
  ) {

    return 0;
  }


  delay(30);


  if (
    digitalRead(BOTAO_CONFIRMAR) !=
    LOW
  ) {

    return 0;
  }


  unsigned long inicio =
    millis();


  while (
    digitalRead(BOTAO_CONFIRMAR) ==
    LOW
  ) {

    delay(10);
  }


  unsigned long duracao =
    millis() - inicio;


  if (
    duracao >= 1000
  ) {

    return 2;
  }


  return 1;
}


// ======================================================
// ENTRAR EM UMA CONFIGURAÇÃO
// ======================================================

void entrarNaConfiguracao() {

  switch (menuIndex) {

    case 0:

      estadoAtual =
        CONFIG_IDIOMA;

      mostrarConfigIdioma();

      break;


    case 1:

      iniciarConfiguracaoData();

      estadoAtual =
        CONFIG_DATA;

      mostrarConfigData();

      break;


    case 2:

      iniciarConfiguracaoHora();

      estadoAtual =
        CONFIG_HORA;

      mostrarConfigHora();

      break;


    case 3:

      triggerCampo = 0;

      estadoAtual =
        CONFIG_TEMP;

      mostrarConfigTemperatura();

      break;


    case 4:

      triggerCampo = 0;

      estadoAtual =
        CONFIG_UMIDADE;

      mostrarConfigUmidade();

      break;


    case 5:

      triggerCampo = 0;

      estadoAtual =
        CONFIG_LUZ;

      mostrarConfigLuz();

      break;


    case 6:
      estadoAtual = CONFIG_CALIBRACAO;
      iniciarCalibracaoLDR();
      break;

    case 7:
      selecionarSim = true;
      estadoAtual = CONFIG_SAIR;
      mostrarTelaSair();
      break;
  }
}


// ======================================================
// MENU CONFIGURACAO
// ======================================================

void mostrarMenuConfig() {

  lcd.clear();

  if (menuIndex == 0) {
    lcd.setCursor(0, 0); lcd.print(F("> ")); lcd.print(textoIdioma());
    lcd.setCursor(0, 1); lcd.print(F("  ")); lcd.print(textoData());
  }
  else if (menuIndex == 1) {
    lcd.setCursor(0, 0); lcd.print(F("> ")); lcd.print(textoData());
    lcd.setCursor(0, 1); lcd.print(F("  ")); lcd.print(textoHora());
  }
  else if (menuIndex == 2) {
    lcd.setCursor(0, 0); lcd.print(F("> ")); lcd.print(textoHora());
    lcd.setCursor(0, 1); lcd.print(F("  ")); lcd.print(textoTemperatura());
  }
  else if (menuIndex == 3) {
    lcd.setCursor(0, 0); lcd.print(F("> ")); lcd.print(textoTemperatura());
    lcd.setCursor(0, 1); lcd.print(F("  ")); lcd.print(textoUmidade());
  }
  else if (menuIndex == 4) {
    lcd.setCursor(0, 0); lcd.print(F("> ")); lcd.print(textoUmidade());
    lcd.setCursor(0, 1); lcd.print(F("  ")); lcd.print(textoLuz());
  }
  else if (menuIndex == 5) {
    lcd.setCursor(0, 0); lcd.print(F("> ")); lcd.print(textoLuz());
    lcd.setCursor(0, 1); lcd.print(F("  ")); lcd.print(textoCalibracao());
  }
  else if (menuIndex == 6) {
    lcd.setCursor(0, 0); lcd.print(F("> ")); lcd.print(textoCalibracao());
    lcd.setCursor(0, 1); lcd.print(F("  ")); lcd.print(textoSair());
  }
  else {
    lcd.setCursor(0, 0); lcd.print(F("> ")); lcd.print(textoSair());
    lcd.setCursor(0, 1); lcd.print(F("  ")); lcd.print(textoIdioma());
  }
}

// ======================================================
// TEXTOS DO MENU
// ======================================================

const __FlashStringHelper* textoIdioma() {
  if (config.idioma == EN_US) return F("LANGUAGE");
  return F("IDIOMA");
}


const __FlashStringHelper* textoData() {
  if (config.idioma == EN_US) return F("DATE");
  if (config.idioma == ES_ES) return F("FECHA");
  return F("DATA");
}


const __FlashStringHelper* textoHora() {
  if (config.idioma == EN_US) return F("TIME");
  return F("HORA");
}


const __FlashStringHelper* textoSair() {
  if (config.idioma == EN_US) return F("EXIT");
  if (config.idioma == ES_ES) return F("SALIR");
  return F("SAIR");
}

const __FlashStringHelper* textoCalibracao() {
  if (config.idioma == EN_US) return F("CALIBRATION");
  if (config.idioma == ES_ES) return F("CALIBRACION");
  return F("CALIBRACAO");
}


// ======================================================
// CONFIGURAÇÃO DE IDIOMA
// ======================================================

void mostrarConfigIdioma() {

  lcd.clear();


  lcd.setCursor(0, 0);

  if (config.idioma == PT_BR) {

    lcd.print(F("> PT-BR"));

    lcd.setCursor(0, 1);

    lcd.print(F("  EN-US  ES-ES"));
  }


  else if (config.idioma == EN_US) {

    lcd.print(F("  PT-BR > EN-US"));

    lcd.setCursor(0, 1);

    lcd.print(F("  ES-ES"));
  }


  else {

    lcd.print(F("  PT-BR  EN-US"));

    lcd.setCursor(0, 1);

    lcd.print(F("> ES-ES"));
  }
}


// ======================================================
// INICIAR CONFIGURAÇÃO DE DATA
// ======================================================

void iniciarConfiguracaoData() {

  DateTime agora =
    horarioLocalAtual();


  editDay =
    agora.day();


  editMonth =
    agora.month();


  editYear =
    agora.year();


  dataCampo = 0;
}


// ======================================================
// MOSTRAR DATA
// ======================================================

void mostrarConfigData() {

  lcd.clear();
  lcd.setCursor(0, 0);

  if (config.idioma == EN_US) {
    imprimirDoisDigitos(editMonth); lcd.print(F("/"));
    imprimirDoisDigitos(editDay); lcd.print(F("/"));
  } else {
    imprimirDoisDigitos(editDay); lcd.print(F("/"));
    imprimirDoisDigitos(editMonth); lcd.print(F("/"));
  }
  lcd.print(editYear);

  lcd.setCursor(0, 1);
  if (config.idioma == EN_US) {
    if (dataCampo == 0) lcd.print(F("> DAY  POT"));
    else if (dataCampo == 1) lcd.print(F("> MON  POT"));
    else lcd.print(F("> YEAR POT"));
  } else {
    if (dataCampo == 0) lcd.print(F("> DIA  POT"));
    else if (dataCampo == 1) lcd.print(F("> MES  POT"));
    else lcd.print(F("> ANO  POT"));
  }
}


// ======================================================
// AJUSTAR DATA COM POTENCIOMETRO
// ======================================================

void ajustarData(
  int anterior,
  int proximo,
  int confirmar
) {

  int valorPot = analogRead(POT_PIN);

  int valorAnterior;
  int valorNovo;

  if (dataCampo == 0) {
    int maxDia = diasNoMes(editMonth, editYear);
    valorAnterior = editDay;
    valorNovo = map(valorPot, 0, 1023, 1, maxDia);
    editDay = constrain(valorNovo, 1, maxDia);
  }
  else if (dataCampo == 1) {
    valorAnterior = editMonth;
    valorNovo = map(valorPot, 0, 1023, 1, 12);
    editMonth = constrain(valorNovo, 1, 12);

    int maxDia = diasNoMes(editMonth, editYear);
    if (editDay > maxDia)
      editDay = maxDia;
  }
  else {
    valorAnterior = editYear;
    valorNovo = map(valorPot, 0, 1023, 2024, 2099);
    editYear = constrain(valorNovo, 2024, 2099);
  }

  if (valorNovo != valorAnterior) {
    mostrarConfigData();
  }

  if (confirmar == 1) {
    dataCampo++;

    if (dataCampo > 2) {
      int maxDia = diasNoMes(editMonth, editYear);
      if (editDay > maxDia)
        editDay = maxDia;

      DateTime agora = horarioLocalAtual();
      DateTime dataLocal(
        editYear, editMonth, editDay,
        agora.hour(), agora.minute(), agora.second()
      );

      RTC.adjust(horarioRTCFromLocal(dataLocal));

      estadoAtual = MENU_CONFIG;
      mostrarMenuConfig();
      return;
    }

    mostrarConfigData();
  }
}


// ======================================================
// ALTERAR CAMPO DA DATA - mantida para compatibilidade
// ======================================================

void alterarCampoData(int delta) {
  (void)delta;
}


// ======================================================
// DIAS NO MÊS
// ======================================================

int diasNoMes(
  int mes,
  int ano
) {

  if (
    mes == 2
  ) {

    if (
      (ano % 4 == 0 && ano % 100 != 0) ||
      (ano % 400 == 0)
    ) {

      return 29;
    }


    return 28;
  }


  if (
    mes == 4 ||
    mes == 6 ||
    mes == 9 ||
    mes == 11
  ) {

    return 30;
  }


  return 31;
}


// ======================================================
// INICIAR CONFIGURAÇÃO DA HORA
// ======================================================

void iniciarConfiguracaoHora() {

  DateTime agora =
    horarioLocalAtual();


  editHour =
    agora.hour();


  editMinute =
    agora.minute();


  horaCampo = 0;
}


// ======================================================
// MOSTRAR CONFIGURAÇÃO DE HORA
// ======================================================

void mostrarConfigHora() {

  lcd.clear();
  lcd.setCursor(0, 0);

  if (usaFormato12Horas()) {
    int hora = editHour;
    const char* periodo = (hora >= 12) ? "PM" : "AM";
    hora %= 12;
    if (hora == 0) hora = 12;
    imprimirDoisDigitos(hora);
    lcd.print(F(":"));
    imprimirDoisDigitos(editMinute);
    lcd.print(F(" "));
    lcd.print(periodo);
  }
  else {
    imprimirDoisDigitos(editHour);
    lcd.print(F(":"));
    imprimirDoisDigitos(editMinute);
  }

  lcd.setCursor(0, 1);
  if (config.idioma == EN_US) {
    if (horaCampo == 0) lcd.print(F("> HOUR POT"));
    else lcd.print(F("> MIN  POT"));
  } else {
    if (horaCampo == 0) lcd.print(F("> HORA POT"));
    else lcd.print(F("> MIN  POT"));
  }
}


// ======================================================
// AJUSTAR HORA COM POTENCIOMETRO
// ======================================================

void ajustarHora(
  int anterior,
  int proximo,
  int confirmar
) {

  int valorPot = analogRead(POT_PIN);
  int valorAnterior;
  int valorNovo;

  if (horaCampo == 0) {
    valorAnterior = editHour;
    valorNovo = map(valorPot, 0, 1023, 0, 23);
    editHour = constrain(valorNovo, 0, 23);
  }
  else {
    valorAnterior = editMinute;
    valorNovo = map(valorPot, 0, 1023, 0, 59);
    editMinute = constrain(valorNovo, 0, 59);
  }

  if (valorNovo != valorAnterior) {
    mostrarConfigHora();
  }

  if (confirmar == 1) {
    horaCampo++;

    if (horaCampo > 1) {
      DateTime agora = horarioLocalAtual();
      DateTime horaLocal(
        agora.year(), agora.month(), agora.day(),
        editHour, editMinute, 0
      );

      RTC.adjust(horarioRTCFromLocal(horaLocal));

      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print(textoHoraAjustada());
      delay(1000);

      estadoAtual = MENU_CONFIG;
      mostrarMenuConfig();
      return;
    }

    mostrarConfigHora();
  }
}


// ======================================================
// CONFIGURAÇÃO DE TEMPERATURA
// ======================================================

void mostrarConfigTemperatura() {

  lcd.clear();

  lcd.setCursor(0, 0);

  if (triggerCampo == 0)
    lcd.print(F("MIN: "));
  else
    lcd.print(F("MAX: "));

  float valor;

  if (triggerCampo == 0)
    valor = temperaturaExibicao(config.tempMin);
  else
    valor = temperaturaExibicao(config.tempMax);

  lcd.print(valor, 1);

  lcd.print(F(" "));
  lcd.print(textoUnidadeTemperatura());

  lcd.setCursor(0, 1);
  lcd.print(textoAjustePot());
}
// ======================================================
// AJUSTAR TEMPERATURA
// ======================================================

void ajustarTriggerTemperatura(
  int anterior,
  int proximo,
  int confirmar
) {
  (void)anterior;
  (void)proximo;

  int valorPot = analogRead(POT_PIN);

  if (triggerCampo == 0) {

    int limiteMaximo = (int)config.tempMax - 1;

    if (limiteMaximo < -20)
      limiteMaximo = -20;

    int novoValor = map(
      valorPot,
      0,
      1023,
      -20,
      limiteMaximo
    );

    if (novoValor != (int)round(config.tempMin)) {
      config.tempMin = novoValor;
      mostrarConfigTemperatura();
    }
  }

  else {

    int limiteMinimo = (int)config.tempMin + 1;

    if (limiteMinimo > 60)
      limiteMinimo = 60;

    int novoValor = map(
      valorPot,
      0,
      1023,
      limiteMinimo,
      60
    );

    if (novoValor != (int)round(config.tempMax)) {
      config.tempMax = novoValor;
      mostrarConfigTemperatura();
    }
  }

  // Confirmar avança do MIN para o MAX e depois salva.
  if (confirmar == 1) {

    triggerCampo++;

    if (triggerCampo > 1) {

      salvarConfiguracao();

      estadoAtual = MENU_CONFIG;

      mostrarMenuConfig();

      return;
    }

    mostrarConfigTemperatura();
  }
}
// ======================================================
// CONFIGURAÇÃO DE UMIDADE
// ======================================================

void mostrarConfigUmidade() {

  lcd.clear();

  lcd.setCursor(0, 0);

  if (triggerCampo == 0)
    lcd.print(F("MIN: "));
  else
    lcd.print(F("MAX: "));

  if (triggerCampo == 0)
    lcd.print(config.umidMin, 0);
  else
    lcd.print(config.umidMax, 0);

  lcd.print(F(" %"));

  lcd.setCursor(0, 1);
  lcd.print(textoAjustePot());
}


// ======================================================
// AJUSTAR UMIDADE
// ======================================================

void ajustarTriggerUmidade(
  int anterior,
  int proximo,
  int confirmar
) {
  (void)anterior;
  (void)proximo;


  int valorPot = analogRead(POT_PIN);

  if (triggerCampo == 0) {

    int limiteMaximo = (int)config.umidMax - 1;

    if (limiteMaximo < 0)
      limiteMaximo = 0;

    int novoValor = map(
      valorPot,
      0,
      1023,
      0,
      limiteMaximo
    );

    if (novoValor != (int)round(config.umidMin)) {
      config.umidMin = novoValor;
      mostrarConfigUmidade();
    }
  }

  else {

    int limiteMinimo = (int)config.umidMin + 1;

    if (limiteMinimo > 100)
      limiteMinimo = 100;

    int novoValor = map(
      valorPot,
      0,
      1023,
      limiteMinimo,
      100
    );

    if (novoValor != (int)round(config.umidMax)) {
      config.umidMax = novoValor;
      mostrarConfigUmidade();
    }
  }

  if (confirmar == 1) {

    triggerCampo++;

    if (triggerCampo > 1) {

      salvarConfiguracao();

      estadoAtual = MENU_CONFIG;

      mostrarMenuConfig();

      return;
    }

    mostrarConfigUmidade();
  }
}


// ======================================================
// CONFIGURAÇÃO DE LUMINOSIDADE
// ======================================================

void mostrarConfigLuz() {

  lcd.clear();

  lcd.setCursor(0, 0);

  if (triggerCampo == 0)
    lcd.print(F("MIN: "));
  else
    lcd.print(F("MAX: "));

  if (triggerCampo == 0)
    lcd.print(config.luzMin, 0);
  else
    lcd.print(config.luzMax, 0);

  lcd.print(F(" %"));

  lcd.setCursor(0, 1);
  lcd.print(textoAjustePot());
}


// ======================================================
// AJUSTAR LUMINOSIDADE
// ======================================================

void ajustarTriggerLuz(
  int anterior,
  int proximo,
  int confirmar
) {
  (void)anterior;
  (void)proximo;


  int valorPot = analogRead(POT_PIN);

  if (triggerCampo == 0) {

    int limiteMaximo = (int)config.luzMax - 1;

    if (limiteMaximo < 0)
      limiteMaximo = 0;

    int novoValor = map(
      valorPot,
      0,
      1023,
      0,
      limiteMaximo
    );

    if (novoValor != (int)round(config.luzMin)) {
      config.luzMin = novoValor;
      mostrarConfigLuz();
    }
  }

  else {

    int limiteMinimo = (int)config.luzMin + 1;

    if (limiteMinimo > 100)
      limiteMinimo = 100;

    int novoValor = map(
      valorPot,
      0,
      1023,
      limiteMinimo,
      100
    );

    if (novoValor != (int)round(config.luzMax)) {
      config.luzMax = novoValor;
      mostrarConfigLuz();
    }
  }

  if (confirmar == 1) {

    triggerCampo++;

    if (triggerCampo > 1) {

      salvarConfiguracao();

      estadoAtual = MENU_CONFIG;

      mostrarMenuConfig();

      return;
    }

    mostrarConfigLuz();
  }
}


// ======================================================
// CALIBRAÇÃO DO LDR
// ======================================================

void mostrarCalibracaoLDR() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(textoCalibracao());
}

void iniciarCalibracaoLDR() {
  const byte AMOSTRAS = 10;

  // 1) Luz acesa: aguarda e calcula a media.
  lcd.clear();
  lcd.setCursor(0, 0);
  if (config.idioma == EN_US) lcd.print(F("TURN LIGHT ON"));
  else if (config.idioma == ES_ES) lcd.print(F("ENCIENDA LUZ"));
  else lcd.print(F("ACENDA A LUZ"));
  lcd.setCursor(0, 1);
  if (config.idioma == EN_US) lcd.print(F("WAIT 5 SEC"));
  else if (config.idioma == ES_ES) lcd.print(F("ESPERE 5 SEG"));
  else lcd.print(F("AGUARDE 5 SEG"));
  delay(5000);

  unsigned long somaClaro = 0;
  for (byte i = 0; i < AMOSTRAS; i++) {
    somaClaro += analogRead(LDR_PIN);
    delay(100);
  }
  uint16_t novoClaro = (uint16_t)(somaClaro / AMOSTRAS);

  // 2) Luz apagada: aguarda e calcula a media.
  lcd.clear();
  lcd.setCursor(0, 0);
  if (config.idioma == EN_US) lcd.print(F("TURN LIGHT OFF"));
  else if (config.idioma == ES_ES) lcd.print(F("APAGUE A LUZ"));
  else lcd.print(F("APAGUE A LUZ"));
  lcd.setCursor(0, 1);
  if (config.idioma == EN_US) lcd.print(F("WAIT 5 SEC"));
  else if (config.idioma == ES_ES) lcd.print(F("ESPERE 5 SEG"));
  else lcd.print(F("AGUARDE 5 SEG"));
  delay(5000);

  unsigned long somaEscuro = 0;
  for (byte i = 0; i < AMOSTRAS; i++) {
    somaEscuro += analogRead(LDR_PIN);
    delay(100);
  }
  uint16_t novoEscuro = (uint16_t)(somaEscuro / AMOSTRAS);

  lcd.clear();

  // Aceita a ordem correta para o tipo de circuito selecionado.
  // No circuito invertido, claro deve gerar leitura menor
  // que escuro.
  bool calibracaoValida;

  if (LDR_INVERTIDO) {
    calibracaoValida = novoEscuro > novoClaro;
  } else {
    calibracaoValida = novoClaro > novoEscuro;
  }

  if (!calibracaoValida) {
    lcd.setCursor(0, 0);
    if (config.idioma == EN_US) lcd.print(F("CALIBRATION ERR"));
    else if (config.idioma == ES_ES) lcd.print(F("CALIBRACION ERR"));
    else lcd.print(F("CALIBRACAO ERRO"));
    lcd.setCursor(0, 1);
    if (config.idioma == EN_US) lcd.print(F("CHECK LDR"));
    else if (config.idioma == ES_ES) lcd.print(F("REVISE EL LDR"));
    else lcd.print(F("VERIFIQUE O LDR"));
    if (SERIAL_OPTION) {
      Serial.print(F("Calibracao invalida. Claro="));
      Serial.print(novoClaro);
      Serial.print(F(" Escuro="));
      Serial.println(novoEscuro);
    }
    delay(1800);
    estadoAtual = MENU_CONFIG;
    mostrarMenuConfig();
    return;
  }

  calibracaoLDR.escuro = novoEscuro;
  calibracaoLDR.claro = novoClaro;
  salvarCalibracaoLDR();

  lcd.setCursor(0, 0);
  if (config.idioma == EN_US) lcd.print(F("CALIBRATION OK"));
  else if (config.idioma == ES_ES) lcd.print(F("CALIBRACION OK"));
  else lcd.print(F("CALIBRACAO OK"));
  lcd.setCursor(0, 1);
  if (config.idioma == EN_US) lcd.print(F("LDR CONFIGURED"));
  else if (config.idioma == ES_ES) lcd.print(F("LDR CONFIGURADO"));
  else lcd.print(F("LDR CONFIGURADO"));

  if (SERIAL_OPTION) {
    Serial.println(F("===== CALIBRACAO LDR ====="));
    Serial.print(F("Escuro -> 0%: "));
    Serial.println(calibracaoLDR.escuro);
    Serial.print(F("Claro  -> 100%: "));
    Serial.println(calibracaoLDR.claro);
    Serial.println(F("=========================="));
  }

  delay(1800);
  estadoAtual = MENU_CONFIG;
  mostrarMenuConfig();
}

// ======================================================
// TELA DE SAÍDA
// ======================================================

void mostrarTelaSair() {

  lcd.clear();

  lcd.setCursor(0, 0);
  lcd.print(textoVoltarInicio());

  lcd.setCursor(0, 1);

  if (selecionarSim) {

    lcd.print(F("> "));
    lcd.print(textoSim());
    lcd.print(F("     "));
    lcd.print(textoNao());
  }

  else {

    lcd.print(F("  "));
    lcd.print(textoSim());
    lcd.print(F("   > "));
    lcd.print(textoNao());
  }
}
// ======================================================
// TELA INICIAL
// ======================================================

void mostrarTelaInicial() {
  lcd.clear();
  DateTime agora = horarioLocalAtual();

  if (paginaInicial == 0) {
    lcd.setCursor(0, 0); imprimirData(agora);
    lcd.setCursor(0, 1); imprimirHora(agora);
  }
  else if (paginaInicial == 1) {
    lcd.setCursor(0, 0); lcd.print(F("T:"));
    if (isnan(temperature)) lcd.print(textoErro());
    else { lcd.print(temperaturaExibicao(temperature), 1); lcd.print((char)223); lcd.print(textoUnidadeTemperatura()); }
    lcd.setCursor(0, 1); lcd.print(config.idioma == EN_US ? "H:" : "U:");
    if (isnan(humidity)) lcd.print(textoErro());
    else { lcd.print(humidity, 1); lcd.print(F("%")); }
  }
  else {
    lcd.setCursor(0, 0); lcd.print(textoLuz()); lcd.print(F(":")); lcd.print(lightPercent); lcd.print(F("%"));
    lcd.setCursor(0, 1);
    if (existeAlarme()) lcd.print(textoAlerta());
    else if (isnan(temperature) || isnan(humidity)) lcd.print(textoDHTErro());
    else lcd.print(textoStatusOK());
  }
}
// ======================================================
// ATUALIZAR TELA AUTOMATICAMENTE
// ======================================================

void atualizarTela() {

  if (
    estadoAtual !=
    TELA_INICIAL
  ) {

    return;
  }


  if (
    millis() -
    lastLCDUpdate >=
    LCD_INTERVAL
  ) {

    lastLCDUpdate =
      millis();


    paginaInicial++;


    if (
      paginaInicial > 2
    ) {

      paginaInicial = 0;
    }


    mostrarTelaInicial();
  }
}


// ======================================================
// ANIMAÇÃO DE INÍCIO
// ======================================================

void animacao() {

  image00();

  delay(500);


  image01();

  delay(500);


  image02();

  delay(500);


  image03();

  delay(500);


  image04();

  delay(500);


  image05();

  delay(500);


  image06();

  delay(500);


  image07();

  delay(500);


  image08();

  delay(500);

  image09();

  delay(1500);
}


// ======================================================
// FRAME 00
// ======================================================

void image00() {

  lcd.clear();

  lcd.setCursor(7, 0);

  lcd.write((uint8_t)0);
}


// ======================================================
// FRAME 01
// ======================================================

void image01() {

  lcd.clear();

  lcd.setCursor(7, 0);

  lcd.write((uint8_t)0);

  lcd.setCursor(7, 1);

  lcd.write((uint8_t)1);
}


// ======================================================
// FRAME 02
// ======================================================

void image02() {

  lcd.clear();

  lcd.setCursor(7, 0);

  lcd.write((uint8_t)1);
}


// ======================================================
// FRAME 03
// ======================================================

void image03() {

  lcd.clear();

  lcd.setCursor(7, 0);

  lcd.write((uint8_t)1);

  lcd.setCursor(7, 1);

  lcd.write((uint8_t)1);
}


// ======================================================
// FRAME 04
// ======================================================

void image04() {

  lcd.clear();

  lcd.setCursor(6, 1);

  lcd.write((uint8_t)2);
}


// ======================================================
// FRAME 05
// ======================================================

void image05() {

  lcd.clear();

  lcd.setCursor(7, 1);

  lcd.write((uint8_t)2);

  lcd.setCursor(7, 0);

  lcd.write((uint8_t)1);
}


// ======================================================
// FRAME 06
// ======================================================

void image06() {

  lcd.clear();

  lcd.setCursor(7, 1);

  lcd.write((uint8_t)3);

  lcd.setCursor(7, 0);

  lcd.write((uint8_t)5);
}


// ======================================================
// FRAME 07
// ======================================================

void image07() {

  lcd.clear();

  lcd.setCursor(7, 1);

  lcd.write((uint8_t)4);

  lcd.setCursor(7, 0);

  lcd.write((uint8_t)1);
}


// ======================================================
// FRAME 08
// ======================================================

void image08() {

  lcd.clear();

  lcd.setCursor(7, 1);

  lcd.write((uint8_t)4);
}

// ======================================================
// FRAME 09 - NOME DO PROJETO
// ======================================================

void image09() {

  lcd.clear();

  lcd.setCursor(7, 0);

  lcd.write((uint8_t)4);

  lcd.setCursor(0, 1);

  lcd.print(F("Vinho no display"));
}


// ======================================================
// LEITURA DO LOG PELA SERIAL
// ======================================================

void get_log() {

  Serial.println();
  Serial.println(F("=========================================="));
  Serial.println(config.idioma == EN_US ?
    "             ANOMALY LOG" :
    "             LOG DE ANOMALIAS");
  Serial.println(F("=========================================="));

  for (int address = EEPROM_LOG_START;
       address < EEPROM_LOG_END;
       address += RECORD_SIZE) {

    uint32_t timestamp;
    int16_t tempInt;
    int16_t humiInt;
    uint8_t lightInt;
    byte flags;

    EEPROM.get(address, timestamp);

    if (timestamp == 0xFFFFFFFFUL || timestamp == 0)
      continue;

    EEPROM.get(address + 4, tempInt);
    EEPROM.get(address + 6, humiInt);
    EEPROM.get(address + 8, lightInt);
    EEPROM.get(address + 9, flags);

    DateTime dtBase(timestamp);
    DateTime dt = dtBase + TimeSpan((fusoHorarioAtual() - FUSO_RTC_BASE) * 3600L);

    Serial.print(dt.timestamp(DateTime::TIMESTAMP_FULL));
    Serial.print(F(" | "));

    if (flags & ANOMALIA_DHT) Serial.print(F("DHT "));
    if (flags & ANOMALIA_TEMPERATURA) Serial.print(F("TEMP "));
    if (flags & ANOMALIA_UMIDADE) Serial.print(F("HUM "));
    if (flags & ANOMALIA_LUZ) Serial.print(F("LUZ "));

    Serial.print(F("| T="));
    if (flags & ANOMALIA_DHT && tempInt == 0) Serial.print(F("ERR"));
    else {
      Serial.print(temperaturaExibicao(tempInt / 100.0), 1);
      Serial.print(textoUnidadeTemperatura());
    }

    Serial.print(F(" | U="));
    if (flags & ANOMALIA_DHT && humiInt == 0) Serial.print(F("ERR"));
    else Serial.print(humiInt / 100.0, 1);

    Serial.print(F("% | L="));
    Serial.print(lightInt);
    Serial.println(F("%"));
  }

  Serial.println(F("=========================================="));
}
O código final. (Data logger - Vinho no display)
JULIANA MEDEIROS SILVA

LIVIA PEREIRA QUEIROZ;
