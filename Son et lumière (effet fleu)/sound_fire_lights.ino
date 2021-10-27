#include "Arduino.h"
#include "SoftwareSerial.h"
#include "DFRobotDFPlayerMini.h"
#include <FastLED.h>

//////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////  DEBUT ZONE A MODIFIER   //////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////

/* Définition des broches */
#define ECHO_PIN  2       // Broche ECHO - Capteur ultrason
#define TRIGGER_PIN 3     // Broche TRIGGER - Capteur ultrason
#define PLAYER_TX_PIN 4   // Broche TX - DFPlayer
#define PLAYER_RX_PIN 5   // Broche RX - DFPlayer
#define LEDS_PIN 6        // Broche data des leds

/* Définition des paramètres du programme */
#define DISTANCE_TRIGER 100    // Distance en cm pour déclenchement de l'action
#define LIGHTING_DURATION 40   // Durée en secondes de l'allumage de l'éclairage
#define PLAYER_VOLUME 30       // Volume de player de 0 à 30
#define NUM_LEDS 30            // Définition du nombre de leds
#define FRAMES_PER_SECOND 30   // Nombre de frames par secondes pour le rafraichissement des flammes
#define PLAYER_RANDOM 1        // 1 : Lecture aléatoire - 0 : Lecture en boucle
#define DEBUG_MODE 1           // 1 : Mode debug ON, 0 : Mode debug Off

//////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////  FIN  ////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////

/* Définition des constantes */
const unsigned long MEASURE_TIMEOUT = 25000UL;  // 25ms = ~8m à 340m/s
const float SOUND_SPEED = 340.0 / 1000;         // Vitesse du son dans l'air en mm/us

#define PLAYER_IDLE 0                           // Le lecteur MP3 est en attente
#define PLAYER_PLAY 1                           // Le lecteur MP3 est entrain de lire un fichier

/* Définition des variables global */
byte IntoArea = 1;                              // Un objet est-il dans la zone de détection ?
unsigned long long TurnOnLighting = 0;          // Timestamp de l'allumage de la lumière
int index_song = 1;                             // Index du fichier MP3 joué
int nb_mp3_files = 0;                           // Nombre total de fichier MP3 sur la carte SD
int player_state = PLAYER_IDLE;                 // Définition de l'état du lecteur MP3 : 0: IDLE, 1: Lecture en cours
CRGB leds[NUM_LEDS];                            // Définition de notre tableau d'état des leds du bandeau
CRGBPalette16 gPal;                             // Définition de la palette de couleur pour l'effet flamme

/* Initialisation du player */
SoftwareSerial playerSerial(PLAYER_RX_PIN, PLAYER_TX_PIN);
DFRobotDFPlayerMini player;

/**
 * Retourne le nombre de millisecondes depuis le démarrage du programme sous la forme d'un
 * nombre entier sur 64 bits (unsigned long long).
 */
unsigned long long superMillis() {
  static unsigned long nbRollover = 0;
  static unsigned long previousMillis = 0;
  unsigned long currentMillis = millis();

  if (currentMillis < previousMillis) {
    nbRollover++;
  }
  previousMillis = currentMillis;

  unsigned long long finalMillis = nbRollover;
  finalMillis <<= 32;
  finalMillis +=  currentMillis;
  return finalMillis;
}

/**
 * Fonction d'initialisation du programme
 */
void setup() {
  // On ouvre le port de communication et l'on attend qu'il soit prêt
  Serial.begin(9600);
  while (!Serial) {
    ; // On attend que le port soit prêt
  }
  if (DEBUG_MODE) Serial.println(F("Port Serial initialisé"));

  // Initialisation du port série pour le lecteur MP3
  playerSerial.begin(9600);
  while (!playerSerial) {
    ; // On attend que le port soit prêt
  }
  if (DEBUG_MODE) Serial.println(F("Port Serial MP3 initialisé"));

  // Initialisation des broches
  pinMode(TRIGGER_PIN, OUTPUT);
  digitalWrite(TRIGGER_PIN, LOW); // La broche TRIGGER doit être à LOW au repos
  pinMode(ECHO_PIN, INPUT);
  //pinMode(RELAI_PIN, OUTPUT);

  // On utilise le port série pour communiquer avec le lecteur MP3
  while (!player.begin(playerSerial)) {
    Serial.println(F("Impossible de se connecter au DFPlayer:"));
    Serial.println(F("1.Vérifier que les branchements sont ok!"));
    Serial.println(F("2.Insérer la carte SD dans le player!"));
    Serial.println(F("Nouvelle tentative de connexion dans 5secondes..."));
    delay(5000);
  }
  if (DEBUG_MODE) Serial.println(F("Player MP3 initialisé"));

  // Définition du volume du player
  player.volume(PLAYER_VOLUME);

  // On récupère le nombre de fichiers MP3
  nb_mp3_files = player.readFileCounts();
  index_song = nb_mp3_files;
  if (DEBUG_MODE) {
    Serial.print(nb_mp3_files);
    Serial.println(F(" fichiers ont été trouvés sur la carte SD"));
  }

  // Initialisation de notre bandeau de leds
  FastLED.addLeds<WS2812B, LEDS_PIN>(leds, NUM_LEDS);
  fill_solid(leds, NUM_LEDS, CRGB::Black);
  FastLED.show();

  // Définition des couleurs pour notre effet flamme
  gPal = CRGBPalette16(CRGB::Black, CRGB::Red, CRGB::Yellow, CRGB::OrangeRed);
  
  // Initialisation de notre fonction d'aléatoire
  randomSeed(analogRead(0));
}

/**
 * Cette fonction lance la lecture d'un nouveau son
 */
void play_mp3() {
  // On lance la lecture d'une piste audio
  if (PLAYER_RANDOM) {
    // On sélectionne un nouveau fichier dans la liste différent du dernier fichier lu
    int new_index_song = random8(1, nb_mp3_files + 1);
    while (new_index_song == index_song) {
      new_index_song = random8(1, nb_mp3_files + 1);
    }
    index_song = new_index_song;
  } else {
    // On sélectionne le fichier suivant, on retourne au début si on est à la fin
    index_song = (index_song == nb_mp3_files) ? 1 : (index_song + 1);
  }
  player.play(index_song);
  player_state = PLAYER_PLAY;
}

/**
 * Boucle du programme
 */
void loop() {
  // On ajoute de l'entropie au générateur de nombres aléatoires
  random16_add_entropy(random());

  // Si la lumière est allumé, on met à jour l'effet flamme
  if (TurnOnLighting != 0) {
    Fire2012WithPalette();
    FastLED.show();
  }
  
  // Etape 1 : Doit'on éteindre l'éclairage ?
  if (TurnOnLighting > 0) {
    if ((superMillis() - TurnOnLighting) / 1000 > LIGHTING_DURATION) {
      // On éteins l'éclairage si aucun MP3 n'est en cours de lecture
      if (player_state == PLAYER_IDLE) {
        fill_solid(leds, NUM_LEDS, CRGB::Black);
        FastLED.show();
        TurnOnLighting = 0;
        if (DEBUG_MODE) Serial.println(F("Extinction des lumières"));
      }      
    }
  }
    
  // Etape 2 : On lance une mesure de distance 
  // On envoie une impulsion HIGH de 10µs sur la broche TRIGGER
  digitalWrite(TRIGGER_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIGGER_PIN, LOW);

  // On mesure le temps entre l'envoi de l'impulsion ultrasonique et son écho (si il existe)
  long measure = pulseIn(ECHO_PIN, HIGH, MEASURE_TIMEOUT);

  // On Calcul la distance à partir du temps mesuré
  float distance_mm = measure / 2 * SOUND_SPEED;

  // Etape 3 : Détection de présence et execution des actions associées
  if (distance_mm > 0 && distance_mm <= DISTANCE_TRIGER * 10) {
    if (IntoArea == 0) {
      // C'est une nouvelle entrée dans la zone de détection
      IntoArea = 1;
          
      // On allume l'éclairage
      TurnOnLighting = superMillis();

      // On lance la lecture d'un MP3
      play_mp3();      
    } else {      
      // Il y a toujours un objet dans la zone de détection, on lance la lecture d'un nouveau 
      // MP3 si aucun MP3 n'est en cours de lecture
      if (player_state == PLAYER_IDLE && TurnOnLighting > 0) {
        play_mp3();
      }
    }
  } else {
    // On sort de la zone de détection, si la lumière est eteinte on stock cette sortie
    if (TurnOnLighting == 0) {
      IntoArea = 0;
    }
  }

  // Etape 4 : Un MP3 vient'il de finir d'être lu ?
  if (player_state == PLAYER_PLAY && player.available()) {
    if (player.readType() == DFPlayerPlayFinished && player.read() == index_song) {
      player_state = PLAYER_IDLE;
    }
  }

  // Affichage des informations dans le moniteur série
  if (DEBUG_MODE) {
    Serial.print(F("Distance : "));
    Serial.print(int(distance_mm/10));
    Serial.print(F(" cm (IntoArea="));
    Serial.print(IntoArea);
    Serial.print(F("), Timeout lumière : "));
    if (TurnOnLighting > 0) {
      if ((superMillis() - TurnOnLighting) / 1000 > LIGHTING_DURATION) {
        Serial.print(F("MP3"));        
      } else {
        Serial.print(LIGHTING_DURATION - int((superMillis() - TurnOnLighting) / 1000));
        Serial.print(F("s"));        
      }
    } else {
      Serial.print(F("-"));
    }
    Serial.print(F(" (TurnOnLighting"));
    Serial.print((TurnOnLighting > 0) ? F(">0") : F("=0"));
    Serial.print(F("), MP3Player :"));
    if (player_state == PLAYER_PLAY) {
      Serial.print(F(" lecture n°"));
      Serial.print(index_song);
    } else {
      Serial.print(F(" IDLE"));      
    }
    Serial.println(F(""));
  }

  // Si l'effet flamme est actif, on calcul un délai de rafraichissement permettant d'afficher le
  // nombre correct de frame par secondes. Sinon on attends une demi seconde.
  if (TurnOnLighting != 0) {
    delay(1000 / FRAMES_PER_SECOND);  
  } else {
    delay(500);
  }
}


// Fire2012 by Mark Kriegsman, July 2012
// 
// There are two main parameters you can play with to control the look and
// feel of your fire: COOLING (used in step 1 above), and SPARKING (used
// in step 3 above).
//
// COOLING: How much does the air cool as it rises?
// Less cooling = taller flames.  More cooling = shorter flames.
// Default 55, suggested range 20-100 
#define COOLING  55

// SPARKING: What chance (out of 255) is there that a new spark will be lit?
// Higher chance = more roaring fire.  Lower chance = more flickery fire.
// Default 120, suggested range 50-200.
#define SPARKING 120

void Fire2012WithPalette() {
  // Array of temperature readings at each simulation cell
  static uint8_t heat[NUM_LEDS];
  
  // Step 1.  Cool down every cell a little
  for (int i = 0; i < NUM_LEDS; i++) {
    heat[i] = qsub8( heat[i],  random8(0, ((COOLING * 10) / NUM_LEDS) + 2));
  }
  
  // Step 2.  Heat from each cell drifts 'up' and diffuses a little
  for (int k = NUM_LEDS - 1; k >= 2; k--) {
    heat[k] = (heat[k - 1] + heat[k - 2] + heat[k - 2] ) / 3;
  }
    
  // Step 3.  Randomly ignite new 'sparks' of heat near the bottom
  if (random8() < SPARKING ) {
    int y = random8(7);
    heat[y] = qadd8( heat[y], random8(160,255) );
  }

  // Step 4.  Map from heat cells to LED colors
  for (int j = 0; j < NUM_LEDS; j++) {
    // Scale the heat value from 0-255 down to 0-240 for best results with color palettes.
    uint8_t colorindex = scale8( heat[j], 240);
    leds[(NUM_LEDS-1) - j] = ColorFromPalette(gPal, colorindex);
  }
}
