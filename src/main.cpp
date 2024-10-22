#include <Arduino.h>
#include "DHT.h"
#include <Wifi.h>
#include "ThingSpeak.h"

// Définition des broches et du type de capteur DHT
#define DHTPIN 14   
#define DHTTYPE DHT11  
DHT dht(DHTPIN, DHTTYPE);

// Configuration de l'CPU pour FreeRTOS
#if CONFIG_FREERTOS_UNICORE
static const BaseType_t app_cpu = 0; // Utilisation d'un seul cœur
#else
static const BaseType_t app_cpu = 1; // Utilisation de deux cœurs
#endif

// Déclaration d'un sémaphore pour la synchronisation
static SemaphoreHandle_t mutex;
const char* ssid = "your ssid";  // Nom du réseau Wi-Fi
const char* password = "yourpassword";  // Mot de passe du réseau Wi-Fi

// Déclaration de la file de messages pour la communication entre tâches
static QueueHandle_t msg_queue;
static const uint8_t msg_queue_len = 5; // Longueur de la file
WiFiClient client; // Client Wi-Fi

// Paramètres pour ThingSpeak
unsigned long myChannelNumber = 3; // Numéro de canal ThingSpeak
const char * myWriteAPIKey = "channelthingspeak"; // Clé API pour écrire sur le canal

// Variables pour le timing
unsigned long lastTime = 0; // Temps de la dernière mise à jour
unsigned long timerDelay = 30000; // Délai entre les mises à jour

// Déclaration d'un spinlock pour la protection des ressources partagées
static portMUX_TYPE spinlock = portMUX_INITIALIZER_UNLOCKED;
bool test = false; // Variable pour indiquer une alerte
float temp; // Variable pour stocker la température

// Tâche pour récupérer les valeurs de la file et mettre à jour ThingSpeak
void getFromQueue(void *parameter) {
  float static TempValue; // Variable pour stocker la valeur de température
  while (1) {
    // Attendre la réception d'un message de la file
    if (xQueueReceive(msg_queue, (void*)&TempValue, portMAX_DELAY) == pdTRUE) { 
      Serial.println(TempValue); // Afficher la température reçue

      // Mettre à jour ThingSpeak avec la température
      if (ThingSpeak.writeField(myChannelNumber, 1, temp, myWriteAPIKey) == 200) {
        Serial.println("Channel update successful.");
      } else {
        Serial.println("Problem updating channel. HTTP error code ");
      }
    }
    vTaskDelay(3000 / portTICK_PERIOD_MS); // Attendre avant de vérifier à nouveau
  }  
}

// Fonction d'interruption pour gérer les alertes de danger
void IRAM_ATTR interr() {
   portENTER_CRITICAL(&spinlock); // Entrer dans une section critique
   test = false; // Indiquer qu'il y a un danger
   Serial.println("alert danger"); // Afficher un message d'alerte
   portEXIT_CRITICAL(&spinlock); // Sortir de la section critique
}

// Tâche pour lire la température du capteur
void getTemperature(void *parameter) {
  while (1) {
    // Prendre le mutex pour accéder à la température
    if (xSemaphoreTake(mutex, portMAX_DELAY) == pdTRUE) {
      temp = dht.readTemperature(); // Lire la température
      if (xQueueSend(msg_queue, (void *)&temp, 10) != pdTRUE) {
        Serial.println("valeur ajouter au queue "); // Confirmer l'ajout à la file
      }
      xSemaphoreGive(mutex); // Libérer le mutex
    }
    vTaskDelay(1000 / portTICK_PERIOD_MS); // Attendre avant de lire à nouveau
  }
}

// Tâche pour utiliser la température lue
void useTemperature(void *parameter) {
  while (1) {
    test = false; // Réinitialiser l'état de test
    // Prendre le mutex pour accéder à la température
    if (xSemaphoreTake(mutex, portMAX_DELAY) == pdTRUE) {
      Serial.print("Temperature: "); // Afficher la température
      Serial.println(temp);  
      
      // Vérifier si la température dépasse un seuil critique
      if (temp > 30) {
        interr(); // Appeler la fonction d'alerte
      } else {
        test = true; // Aucun danger, mise à jour de l'état
      }
      xSemaphoreGive(mutex); // Libérer le mutex
    }  
    // Contrôler les sorties en fonction de l'état de test
    if (test == true) {
      digitalWrite(4, HIGH); // Allumer la LED 4
      vTaskDelay(1000 / portTICK_PERIOD_MS);
      digitalWrite(4, LOW); // Éteindre la LED 4
    } else {
      digitalWrite(12, HIGH); // Allumer la LED 12
      vTaskDelay(1000 / portTICK_PERIOD_MS);  
      digitalWrite(12, LOW); // Éteindre la LED 12
    }    
  }
}

// Fonction d'initialisation
void setup() {
  Serial.begin(115200); // Initialiser la communication série
  dht.begin(); // Initialiser le capteur DHT
  pinMode(4, OUTPUT); // Définir la broche 4 comme sortie
  pinMode(12, OUTPUT); // Définir la broche 12 comme sortie
  mutex = xSemaphoreCreateMutex(); // Créer un mutex
  
  WiFi.mode(WIFI_STA); // Configurer le mode Wi-Fi en station
  ThingSpeak.begin(client); // Initialiser ThingSpeak

  // Créer la file de messages
  msg_queue = xQueueCreate(msg_queue_len, sizeof(int));
  if (msg_queue == NULL) {
    Serial.println("Failed to create queue"); // Vérifier si la file a été créée avec succès
    return;  
  }

  WiFi.begin(ssid, password); // Connexion au réseau Wi-Fi
  
  // Boucle jusqu'à ce que la connexion soit établie
  Serial.print("Tentative de connexion au Wi-Fi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000); // Attendre avant de réessayer
    Serial.print("."); // Indicateur de progression
  }
  Serial.println("\nConnexion établie avec succès !");

  // Création des tâches FreeRTOS
  xTaskCreatePinnedToCore(getTemperature, "Task1", 1024, NULL, 1, NULL, app_cpu); 
  xTaskCreatePinnedToCore(useTemperature, "Task2", 2048, NULL, 1, NULL, app_cpu);
  xTaskCreatePinnedToCore(getFromQueue, "Task3", 2048, NULL, 1, NULL, !app_cpu);  
}

// Boucle principale vide
void loop() {
}

