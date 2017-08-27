#include <Servo.h>

// Comment the following line to disable debug prints
//#define __DEBUG
// *******************************************************************************
// **                                                                           **
// **              C O N S T A N T S   A N D   P I N   M A P P I N G            **
// **                                                                           **
// *******************************************************************************
// Keep these as define so they can be used in preprocessor statements
#define TOTAL_STEPS               16
#define TOTAL_TRACKS              8

const int  MIN_BPM                = 120;
const int  MAX_BPM                = 300;
const int  BPM_CHG_MIN            = -20;
const int  BPM_CHG_MAX            = 20;

// Servo params --
const byte SERVO_UPDATE_INT       = 10;
const byte SERVO_INCREMENT        = 4;
const byte SERVO_INITIALPOS       = 10;
const byte HIT_SERVO_ANGLE        = 45;

// Sequence params
const byte MAX_POLYPHONY          = 4;

const byte MIN_TRANSITION_REP     = 0;
const byte MAX_TRANSITION_REP     = 1;

const byte MIN_PATTERN_REP        = 1;
const byte MAX_PATTERN_REP        = 1;

// T PARAMS ------
const byte T_MIN_MARK             = 4;
const byte T_MAX_MARK             = 7;
// CT PARAMS -----
const byte CT_MIN_OMIT            = 0;
const byte CT_MAX_OMIT            = 0;
// TD PARAMS -----
const byte TD_MIN_DELAY           = 1;
const byte TD_MAX_DELAY           = 3;
const byte TD_MIN_OMIT            = 0;
const byte TD_MAX_OMIT            = 0;
// CTD PARAMS ----
const byte CTD_MIN_DELAY          = 1;
const byte CTD_MAX_DELAY          = 3;
const byte CTD_MIN_OMIT           = 0;
const byte CTD_MAX_OMIT           = 0;
// TI PARAMS -----
const byte TI_MIN_OMIT            = 0;
const byte TI_MAX_OMIT            = 0;
// CTI PARAMS ----
const byte CTI_MIN_OMIT           = 0;
const byte CTI_MAX_OMIT           = 0;
// TID PARAMS ----
const byte TID_MIN_DELAY          = 1;
const byte TID_MAX_DELAY          = 3;
const byte TID_MIN_OMIT           = 0;
const byte TID_MAX_OMIT           = 0;
// CTID PARAMS ---
const byte CTID_MIN_DELAY          = 1;
const byte CTID_MAX_DELAY          = 3;
const byte CTID_MIN_OMIT           = 0;
const byte CTID_MAX_OMIT           = 0;


// Auto-select data type for each track row
#if TOTAL_STEPS <= 8
typedef byte _tRow;
#else
#if TOTAL_STEPS <= 16
typedef unsigned short _tRow;
#else
typedef unsigned long _tRow;
#endif
#endif

// PINS ------------------------------------
const int LDR_PIN                       = A0;

// Servo pins -----------------------
const int servoPin[TOTAL_TRACKS]        = { 4, 5, 6, 7, 8, 9, 10, 11};
enum {
  _T, _CT, _TD, _CTD, _TI, _CTI, _TID, _CTID
};

// *******************************************************************************
// **                                                                           **
// **                   S E R V O   S W E E P E R   C L A S S                   **
// **                                                                           **
// *******************************************************************************
class Sweeper {
    int pos;                  // current servo position
    bool moving;
    int  updateInterval;      // interval between updates
    unsigned long lastUpdate; // last update of position
    int incstep;              // increment to move for each interval

  public:
    Servo servo;              // the servo

    Sweeper() {
      pos = -1;
      setParams (0, 0);
    }

    void setTargetPos (int p) {
      if (pos == p) return;
      pos = p;
      servo.write (p);
    }
    void setParams (int interval, int is) {
      updateInterval = interval;
      incstep = is;
      moving = false;
    }

    bool isMoving(){
      return moving;
    }

    void attach(int pin) {
      servo.attach(pin);
      setTargetPos (SERVO_INITIALPOS);
    }

    void detach() {
      servo.detach();
    }

    void beat() {
      moving = true;
      lastUpdate = millis();
    }

    void update() {
      unsigned long curTime = millis();
      int targetPos;

      if (moving) {
        if ((curTime - lastUpdate) >= updateInterval) {
          lastUpdate = curTime;
          targetPos = constrain(pos + incstep, SERVO_INITIALPOS, HIT_SERVO_ANGLE + 1);
          if (pos < HIT_SERVO_ANGLE) {
            setTargetPos (targetPos);
          } else {
            moving = false;
          }
        }
      } else {
        // If the servo is not moving, update in real-time the position according to the
        // current value of initialServoPos
        setTargetPos max(targetPos - 1, SERVO_INITIALPOS);
      }
    }
};

// *******************************************************************************
// **                                                                           **
// **                        G L O B A L   D A T A                              **
// **                                                                           **
// *******************************************************************************
volatile word    stepInterval       = 60000/ (MIN_BPM + (MAX_BPM - MIN_BPM)/2);
volatile byte    currentStep        = 0;
volatile byte    curRepetition      = 0;
volatile byte    targetRepetitions  = 0;
volatile word    msCounter;
volatile boolean playing            = true;
volatile byte    stillTransitioning = 0;
volatile byte    currentLeadTrack   = 0;
volatile word    patternNumber      = 0;
volatile long    prevSeed;

// Global Data arrays
_tRow   pattern1[TOTAL_TRACKS+1];
_tRow   pattern2[TOTAL_TRACKS+1];

_tRow   *curTrackData  = pattern1;
_tRow   *prevTrackData = pattern2;
char    transitionCounter[TOTAL_TRACKS];
Sweeper sweeper[TOTAL_TRACKS];
// *******************************************************************************
// **                                                                           **
// **                         R A N D O M   H E L P E R S                       **
// **                                                                           **
// *******************************************************************************
class StepPool {
    char availablePos[TOTAL_STEPS];
    char maxPosIndex;

  public:
    StepPool() {
      reset();
    }

    void reset() {
      for (char p = 0; p < TOTAL_STEPS; p++) availablePos[p] = p;
      resetLimit();
    }

    void resetLimit() {
      maxPosIndex = TOTAL_STEPS - 1;
    }

    void removeFromSetByIndex(char i) {
      if (i < 0 || i > maxPosIndex) return;

      if (i != maxPosIndex) {
        char p = availablePos[i];
        availablePos[i] = availablePos[maxPosIndex];
        availablePos[maxPosIndex] = p;
      }

      // Adjust the array limit
      if (maxPosIndex > 0) {
        maxPosIndex--;
      } else {
        resetLimit();
      }
    }

    void removeFromSetByValue(char v) {
      for (char i = 0; i <= maxPosIndex; i++) {
        if (availablePos[i] == v) {
          removeFromSetByIndex (i);
          return;
        }
      }
    }

    byte getRandomPosition() {
      char i = random(0, maxPosIndex + 1);
      char p = availablePos[i];

      removeFromSetByIndex (i);
      return p;
    }
};

// *******************************************************************************
// **                                                                           **
// **                    P A T T E R N   G E N E R A T I O N                    **
// **                                                                           **
// *******************************************************************************
// Bit set/get macros
#define setTrackStep(tr, b)  tr |= (_tRow)(1 << b)
#define clrTrackStep(tr, b)  tr &= ~(_tRow)(1 << b)
#define getTrackStep(tr, b)  (tr & (1 << b))
// Track limit macro
#define TRACKN(t) ((t) % TOTAL_TRACKS)

void printPatternRow(_tRow r) {
  char b;
  for (b = 0; b < TOTAL_STEPS; b++) {
    Serial.print(r & 1 ? "1 " : "0 ");
    r >>= 1;
  }
  Serial.println("");
}

char countMarkedSteps(_tRow row) {
  char count = 0;

  for (char p = 0; p < TOTAL_STEPS; p++) {
    if (row & 1) count++;
    row >>= 1;
  }

  return count;
}

_tRow reverseRow (_tRow row) {
  _tRow resRow = 0;

  for (char p = 0; p < TOTAL_STEPS; p++) {
    if (row & 1) setTrackStep(resRow, TOTAL_STEPS - 1 - p);
    row >>= 1;
  }
  return resRow;
}

_tRow counterRow (_tRow row) {
  _tRow resRow = 0;

  for (char p = 0; p < TOTAL_STEPS; p++) {
    if (!(row & 1)) setTrackStep(resRow,  p);
    row >>= 1;
  }
  return resRow;
}

_tRow patternFromPool (StepPool *pool, char markTotal) {
  _tRow row = 0;
  for (char m = 0; m < markTotal; m++) setTrackStep (row, pool->getRandomPosition());
  return row;
}

_tRow createPattern (char mark_min, char mark_max) {
  char  to_mark = min(random(mark_min, mark_max + 1), TOTAL_STEPS);
  StepPool step_pool;
  return patternFromPool (&step_pool, to_mark);
}

_tRow doCopyPattern (_tRow src, char omit_min, char omit_max) {
  char to_omit     = min(random(omit_min, omit_max + 1), TOTAL_STEPS);
  char avail_marks = countMarkedSteps(src);
  char to_mark     = max(0, avail_marks - to_omit);

  // DEBUG ----
  //Serial.print("Omitting ");
  //Serial.print(to_omit, DEC);
  //Serial.print(", Marking ");
  //Serial.print(to_mark, DEC);
  //Serial.print(" / ");
  //Serial.println(avail_marks, DEC);
  // ----------

  if (to_mark < 1) return 0;

  // Create a pool of the positions available for us to mark, extracted from the
  // source pattern
  char p = 0;
  StepPool step_pool;
  for (p = 0; p < TOTAL_STEPS; p++) {
    if (!(src & 1)) step_pool.removeFromSetByValue(p);
    src >>= 1;
  }
  return patternFromPool (&step_pool, to_mark);
}

_tRow doCounterPattern (_tRow src, char omit_min, char omit_max) {
  // For the counter patterns we want AT MOST as many beats as there were on the
  // original track, so we will adjust the omit constant to accomodate this constraint
  char src_beats = countMarkedSteps(src);
  char omit_base = (TOTAL_STEPS - src_beats) - src_beats;
  return doCopyPattern (counterRow(src), omit_base + omit_min, omit_base + omit_max);
}


_tRow doReversePattern (_tRow src, char omit_min, char omit_max) {
  return doCopyPattern (reverseRow(src), omit_min, omit_max);
}

_tRow doDelayedPattern (_tRow src, char min_delay, char max_delay, char omit_min, char omit_max) {
  char delay_n = min(random(min_delay, max_delay + 1), TOTAL_STEPS);
  return doCopyPattern ((_tRow)(src << delay_n), omit_min, omit_max);
}

// *******************************************************************************
// **                                                                           **
// **                    S T E P   D A T A   H A N D L I N G                    **
// **                                                                           **
// *******************************************************************************
void fixPolyphony (char max_per_step, char trackStart) {
  char n, t, s;
  char markCount;

  for (s = 0; s < TOTAL_STEPS; s++) {
    markCount = 0;
    for (n = 0; n < TOTAL_TRACKS; n++) {
      t = (n + trackStart) % TOTAL_TRACKS;
      if (getTrackStep(curTrackData[t], s)) markCount++;
      if (markCount > max_per_step) clrTrackStep (curTrackData[t], s);
    }
  }
}

void newBPM(){
  long curBPM = 60000/stepInterval;
  curBPM += random (BPM_CHG_MIN, BPM_CHG_MAX+1);
  curBPM = constrain (curBPM, MIN_BPM, MAX_BPM);
  stepInterval = 60000/curBPM;
#ifdef __DEBUG
  Serial.print("New BPM is ");
  Serial.print(curBPM, DEC);
  Serial.print(". Interval = ");
  Serial.println(stepInterval);
#endif
}

void generateCurrentTrack(){
  char n;

#ifdef __DEBUG
  Serial.print("Random Seed: ");
  Serial.print(prevSeed, DEC);
  Serial.print(" -> ");
#endif
  prevSeed += analogRead(LDR_PIN);
#ifdef __DEBUG
  Serial.println(prevSeed, DEC);
  randomSeed(prevSeed);
#endif

  Serial.print("Pattern ");
  Serial.print(patternNumber);
  Serial.println(" --");

  byte off = currentLeadTrack;
  curTrackData[TRACKN(_T+off)]    = createPattern(T_MIN_MARK, T_MAX_MARK);
  curTrackData[TRACKN(_CT+off)]   = doCounterPattern(curTrackData[TRACKN(_T+off)]  , CT_MIN_OMIT    , CT_MAX_OMIT);
  curTrackData[TRACKN(_TD+off)]   = doDelayedPattern(curTrackData[TRACKN(_T+off)]  , TD_MIN_DELAY   , TD_MAX_DELAY   , TD_MIN_OMIT   , TD_MAX_OMIT);
  curTrackData[TRACKN(_CTD+off)]  = doDelayedPattern(curTrackData[TRACKN(_CT+off)] , CTD_MIN_DELAY  , CTD_MAX_DELAY  , CTD_MIN_OMIT  , CTD_MAX_OMIT);
  curTrackData[TRACKN(_TI+off)]   = doReversePattern(curTrackData[TRACKN(_T+off)]  , TI_MIN_OMIT    , TI_MAX_OMIT);
  curTrackData[TRACKN(_CTI+off)]  = doCounterPattern(curTrackData[TRACKN(_TI+off)] , CTI_MIN_OMIT   , CTI_MAX_OMIT);
  curTrackData[TRACKN(_TID+off)]  = doDelayedPattern(curTrackData[TRACKN(_TI+off)] , TID_MIN_DELAY  , TID_MAX_DELAY  , TID_MIN_OMIT  , TID_MAX_OMIT);
  curTrackData[TRACKN(_CTID+off)] = doDelayedPattern(curTrackData[TRACKN(_CTI+off)], CTID_MIN_DELAY , CTID_MAX_DELAY , CTID_MIN_OMIT , CTID_MAX_OMIT);

#ifdef __DEBUG
  Serial.print(TRACKN(_T+off), DEC);
  Serial.print(": T    : ");
  printPatternRow(curTrackData[TRACKN(_T+off)]);
  Serial.print(TRACKN(_CT+off), DEC);
  Serial.print(": CT   : ");
  printPatternRow(curTrackData[TRACKN(_CT+off)]);
  Serial.print(TRACKN(_TD+off), DEC);
  Serial.print(": TD   : ");
  printPatternRow(curTrackData[TRACKN(_TD+off)]);
  Serial.print(TRACKN(_CTD+off), DEC);
  Serial.print(": CTD  : ");
  printPatternRow(curTrackData[TRACKN(_CTD+off)]);
  Serial.print(TRACKN(_TI+off), DEC);
  Serial.print(": TI   : ");
  printPatternRow(curTrackData[TRACKN(_TI+off)]);
  Serial.print(TRACKN(_CTI+off), DEC);
  Serial.print(": CTI  : ");
  printPatternRow(curTrackData[TRACKN(_CTI+off)]);
  Serial.print(TRACKN(_TID+off), DEC);
  Serial.print(": TID  : ");
  printPatternRow(curTrackData[TRACKN(_TID+off)]);
  Serial.print(TRACKN(_CTID+off), DEC);
  Serial.print(": CTID : ");
  printPatternRow(curTrackData[TRACKN(_CTID+off)]);
#else
  for (n = 0; n < TOTAL_TRACKS; n++) {
      printPatternRow(curTrackData[TRACKN(n+off)]);
    }
#endif

  // Count the number of steps
  char s, markCount, maxSteps  = 0;
  for (s = 0; s < TOTAL_STEPS; s++) {
    markCount = 0;
    for (n = 0; n < TOTAL_TRACKS; n++) {
      if (getTrackStep(curTrackData[n], s)) markCount++;
    }
    if (markCount > maxSteps) maxSteps = markCount;
  }
#ifdef __DEBUG
  Serial.print("Max beat count in a step: ");
  Serial.println(maxSteps, DEC);
#endif

  if (maxSteps > MAX_POLYPHONY) {
    fixPolyphony (MAX_POLYPHONY, currentLeadTrack);
#ifdef __DEBUG
    Serial.print ("Fixing polyphony starting at track #");
    Serial.print (currentLeadTrack, DEC);
    Serial.print (" with limit ");
    Serial.println (MAX_POLYPHONY, DEC);

    for (n = 0; n < TOTAL_TRACKS; n++) {
      Serial.print(TRACKN(n+off), DEC);
      Serial.print(": ");
      printPatternRow(curTrackData[TRACKN(n+off)]);
    }
#endif
  }

#ifdef __DEBUG
  Serial.print ("Lead track: ");
  Serial.println (currentLeadTrack);
#endif

  // Update current lead track
  currentLeadTrack = TRACKN(currentLeadTrack + 1);

  // Select number of repetitions for this new pattern
  curRepetition      = 0;
  targetRepetitions  = random(MIN_PATTERN_REP, MIN_PATTERN_REP+1);

#ifdef __DEBUG
  Serial.print("Target repetitions: ");
  Serial.println(targetRepetitions, DEC);
#endif
  patternNumber++;
}

// *******************************************************************************
// **                                                                           **
// **   A R D U I N O   P R O C S   &   I N T E R R U P T   H A N D L I N G     **
// **                                                                           **
// *******************************************************************************
// TIMER0 INTERRUPT --------------------------------------------
volatile unsigned long rightNow;

// This interrupt is called once a millisecond.
SIGNAL(TIMER0_COMPA_vect) {
  char t;
  rightNow = millis();

  msCounter++;
  if (msCounter < stepInterval) return;

  if (currentStep >= TOTAL_STEPS) {
    // Roll-over to step zero and increase the pattern repetition counter
    currentStep = 0;
    if (stillTransitioning == 0) curRepetition++;
  
    // See if we need to generate a new pattern ----
    if (curRepetition >= targetRepetitions) {
  
      // Make the current pattern the "old" pattern
      if (curTrackData == pattern2){
        prevTrackData = pattern2;
        curTrackData  = pattern1;
        
      }else{
        prevTrackData = pattern1;
        curTrackData  = pattern2;
      }

      // TO-DO: Read LDR for random
      // Generate new track
      generateCurrentTrack();
  
      // Select transition delays at random
      stillTransitioning = 0;
      for (t = 0; t < TOTAL_TRACKS; t++) {
        transitionCounter[t] = random(MIN_TRANSITION_REP, MAX_TRANSITION_REP+1) ;
        if (transitionCounter[t]) stillTransitioning++;
#ifdef __DEBUG
        Serial.print("Transition time for track ");
        Serial.print(t, DEC);
        Serial.print(" is ");
        Serial.println(transitionCounter[t], DEC);
#endif
      }

      if (!stillTransitioning) {
#ifdef __DEBUG        
        Serial.println("No pending transitions ---------------");
#endif
        newBPM();
      }
    }else if (stillTransitioning) {
      // Decrease all transition counters, and re-count the number of tracks still transitioning
      stillTransitioning = 0;
      for (t = 0; t < TOTAL_TRACKS; t++){
        if (transitionCounter[t] > 0) {
          transitionCounter[t]--;
          if (transitionCounter[t] > 0) stillTransitioning++;
        }
      }
      if (!stillTransitioning) {
#ifdef __DEBUG
        Serial.println("All tracks have transitioned ---------");
#endif
        newBPM();
      }
    }
  }

#ifdef __DEBUG
  Serial.print ("Step ");
  Serial.print (currentStep, DEC);
  Serial.print (": ");
#endif
  // Play this step
  for (t = 0; t < TOTAL_TRACKS; t++){
    if (transitionCounter[t]){
      // Use previous track data --------
      if (getTrackStep(prevTrackData[t], currentStep)){
        sweeper[t].beat();
 #ifdef __DEBUG
        Serial.print(t, DEC);
        Serial.print("(P) ");
#endif
      }
    }else {
      // Use current track --------------
      if (getTrackStep(curTrackData[t], currentStep)){
        sweeper[t].beat();
#ifdef __DEBUG
        Serial.print(t, DEC);
        Serial.print(" ");
#endif
      }
    }
  }
#ifdef __DEBUG
  Serial.println("");
#endif
  msCounter = 0;
  currentStep++;
}

// SETUP -------------------------------------------------------
void setup() {
  Serial.begin(9600);

  // Wait a bit before starting
  delay(3000);

  // Initialize sweepers
  for (byte t = 0; t < TOTAL_TRACKS; t++) {
    sweeper[t].attach(servoPin[t]);
    sweeper[t].setParams(SERVO_UPDATE_INT, SERVO_INCREMENT);
    // Wait 1s before moving the next servo
    delay(1000);
  }

  // Test servos
  for (byte t = 0; t < TOTAL_TRACKS; t++) {
    sweeper[t].beat();
    while (sweeper[t].isMoving()) sweeper[t].update();
  }

  // Wait again before we start with everything
  delay(3000);

  Serial.println("< ----- ON ----- >");
  // Init seed
  prevSeed = analogRead(LDR_PIN);

  // Initial pattern
  generateCurrentTrack();

  // Servo/beat update interrupt
  // Timer0 is already used for millis() - we'll just interrupt somewhere
  // in the middle and call the "Compare A" function below
  OCR0A = 0xAF;
  TIMSK0 |= _BV(OCIE0A);

  // TEST
  /*
  Serial.println ("Random test --");
  StepPool step_pool;
  for (char i = 0; i < 32; i++) {
    Serial.println(step_pool.getRandomPosition(), DEC);
  }

  Serial.println ("Count test --");
  Serial.print("Number of 1s in 0: ");
  Serial.println(countMarkedSteps (0), DEC);
  Serial.print("Number of 1s in 1: ");
  Serial.println(countMarkedSteps (1), DEC);
  Serial.print("Number of 1s in 500: ");
  Serial.println(countMarkedSteps ((_tRow)500), DEC);
  Serial.print("Number of 1s in 16357: ");
  Serial.println(countMarkedSteps ((_tRow)16357), DEC);
  Serial.print ("Sizeof _tRow: ");
  Serial.println(sizeof (_tRow), DEC);
  */
}

// LOOP --------------------------------------------------------
void loop() {
  
  // Update servos 
  for (byte s = 0; s < TOTAL_TRACKS; s++) {
    sweeper[s].update();
  }
}
