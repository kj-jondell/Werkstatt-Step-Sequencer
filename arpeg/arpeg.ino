/** EIGHT STEP SEQUENCER FOR THE WERKSTATT-01 */

#define EDIT_MODE 0
#define PLAY_MODE 1

#define NORMAL_STEP 0
#define RANDOM_STEP 1
#define PING_PONG_STEP 2
#define BACKWARDS_STEP 3
#define FIBONACCI_STEP 4

#define FORWARD 1
#define BACKWARD -1

#define MINIM 0.25
#define CROTCHET 0.5
#define QUAVER 1
#define SEMIQUAVER 2
#define DEMISEMIQUAVER 4
#define MAX_BPM 240.0
#define MIN_BPM 50.0

#define CLEAR_LED -1

#define SYNC_IN_PIN 5
#define TOGGLE_ACTIVE_PIN 7
#define GATE_OUT_PIN 6
#define SHIFT_IN_PIN 8
#define SHCP_PIN 9
#define STCP_PIN 10
#define BPM_PIN A0
#define NOTE_LENGTH_PIN A1
#define SETTINGS_BUTTONS_PIN A2 //analog /*demux: bpm, note value (x4), note length, show all, edit mode*/

//constants
const short STEP_SEQUENCE [8][3] = {{LOW, LOW, LOW} /*0*/,{LOW, LOW, HIGH}/*1*/,
{LOW, HIGH, HIGH} /*3*/,{LOW, HIGH, LOW}, /*2*/ {HIGH, HIGH, LOW} /*6*/,
{HIGH, HIGH, HIGH} /*7*/,{HIGH, LOW, HIGH} /*5*/, {HIGH, LOW, LOW} /*4*/}; //8 because of eight steps, 3 because of 8 = 2^3

const short STEP_PINS [] = {2, 3, 4}; //LSB - MSB
const short FAST_STEP_PINS [] = {11, 12, 13}; //LSB - MSB

//user-modifiable variables
double bpm, note_value = DEMISEMIQUAVER /* minim, crotchet, quaver, semiquaver */;
short mode = PLAY_MODE, stepping_mode = PING_PONG_STEP,
active_steps = 8, //amount of steps used
step_incrementor = FORWARD; //depending on stepping mode 
bool step_muted [8];
bool show_all = false;

//time variables 
unsigned long gate_delay = 50, last_note_time = 0, last_gate_time = 0, debounce_delay = 10, 
last_sync_time = 0, external_time_out = 600, mspb = 500 /*ms per beat = 120bpm*/;
unsigned long last_debounce_time [8]; 
double note_on_fraction = 0.8; //use 0.8 for now, should be changeable
bool external_sync = false, new_note = true,
waiting_for_sync = true, waiting_for_timer = false;
bool last_button_state [8], current_button_state [8];
short current_step, fast_iterator, sync_reading = LOW, last_sync_reading = LOW,
external_timer_counter = 0, external_sync_counter = 0;

//DEBUGGING
bool DEBUG = true; 

void setup() {
  if(DEBUG) Serial.begin(9600);

  for(int pin = 0; pin < 3; pin++){ //three because of 2^3=8
    pinMode(STEP_PINS[pin], OUTPUT);
    digitalWrite(STEP_PINS[pin], LOW); //init with zero

    pinMode(FAST_STEP_PINS[pin], OUTPUT);
    digitalWrite(FAST_STEP_PINS[pin], LOW); //init with zero
  }
  
  pinMode(SHIFT_IN_PIN, OUTPUT);
  pinMode(SHCP_PIN, OUTPUT);
  pinMode(STCP_PIN, OUTPUT);
  
  pinMode(SYNC_IN_PIN, INPUT);
  pinMode(GATE_OUT_PIN, OUTPUT);
  pinMode(TOGGLE_ACTIVE_PIN, INPUT_PULLUP); 

  for(int step = 0; step < 8; step++){
    step_muted[step] = false; //init all steps as active (should be 'true' but for some strange reason does not work)
    last_button_state[step] = LOW; //init last state as low/off
    current_button_state[step] = LOW; //init current state as low/off
    last_debounce_time[step] = 0; // init as zero
  }

}

/**
 * outputs step pins accordingly to chosen step
 */

void set_step_pins(){
 for(int pin = 0; pin < 3; pin++) //three pins because of 2^3 = 8
    digitalWrite(STEP_PINS[pin], STEP_SEQUENCE[current_step][pin]); 
}

/**
 * definition of stepping modes
 */
void set_stepping_mode(){
  
  switch (stepping_mode) {
    case RANDOM_STEP :  step_incrementor = 1 + random(active_steps);
                        break;
    case PING_PONG_STEP : if(current_step == 0) step_incrementor = FORWARD;
                          else if (current_step == (active_steps-1)) step_incrementor = BACKWARD;
                          if (step_incrementor != FORWARD && step_incrementor != BACKWARD) step_incrementor = FORWARD;
                        break;
    case NORMAL_STEP :  step_incrementor = FORWARD;
                        break;
    case BACKWARDS_STEP : step_incrementor = BACKWARD;
                        break;
   // case FIBONACCI_STEP : step_incrementor = current_step
  }

 // if(stepping_mode != FIBONACCI_STEP && fibonacci_step) fibonacci_step = false;

}
/*
bool fibonacci_step = false;
short fibonacci_counter = 0;
short fibonacci_steps(){
  if(!fibonacci_step){
    fibonacci_counter = 0;  
  }
  
}*/

int wrap_mod (int number, int mod){
   return (number%mod + mod)%mod;
}

/**
  * increases step with every sync pulse. outputs step pins and gate if step is active
  */
void next_step(){
 set_stepping_mode(); 
 
 current_step = wrap_mod(current_step + step_incrementor, active_steps); //next step
 if(step_muted[current_step]){ //only output if current step is active
    set_step_pins();
    send_gate_on();
 }
}

/**
 * fast iterator that scans for input continously 
 */
void fast_next_step(){
  fast_iterator = (fast_iterator + 1) % 8; //next step
  for(int pin = 0; pin < 3; pin++) //three pins because of 2^3 = 8
      digitalWrite(FAST_STEP_PINS[pin], STEP_SEQUENCE[fast_iterator][pin]); 
}

/**
  * sends a gate on signal
  */
void send_gate_on () {
  digitalWrite(GATE_OUT_PIN, HIGH);
}

/**
  * sends a gate off signal
  */
void send_gate_off () {
  digitalWrite(GATE_OUT_PIN, LOW);
}

/**
 * if external sync is active, then do following.. (not finished..) 
 */
void triggered_new_note(){
    last_note_time = millis();
    last_gate_time = millis();  

    next_step(); //increments step and sends gate if active
    
    if(step_muted[current_step]) show_chosen_step(current_step);     
}

/**
 * toggles to next stepping mode
 */
void next_stepping_mode(){
  stepping_mode = (stepping_mode + 1) % 4;
}

/**
 * reads bpm from master bpm pot.
 */
void check_bpm(){
  bpm = analogRead(BPM_PIN) * (MAX_BPM - MIN_BPM) / 1023 + MIN_BPM; //mapping analog read values to range min - max bpm. 1023 is highest analog read value. 
bpm = 120; //TEMPORARY
}

/**
 * checks if button has been pressed and 
 */
void check_toggle_buttons(){
  int active_button_state = digitalRead(TOGGLE_ACTIVE_PIN);

  if(active_button_state != last_button_state[fast_iterator])
    last_debounce_time[fast_iterator] = millis();
  
  if((millis() - last_debounce_time[fast_iterator]) > debounce_delay){
    if(active_button_state != current_button_state[fast_iterator]) {
      current_button_state[fast_iterator] = active_button_state; 
      if(current_button_state[fast_iterator] == HIGH){
        if(mode == PLAY_MODE)
          step_muted[fast_iterator] = !step_muted[fast_iterator];
        else if(mode == EDIT_MODE){
          current_step = fast_iterator;
          show_chosen_step(current_step);
          set_step_pins();
          send_gate_on();
        }
      }
    }
  }
  
  last_button_state[fast_iterator] = active_button_state;
  fast_next_step(); //scanning for input!
}

/**
 * lights the led for every active note. for use in edit mode.
 */
void show_all_active(){
 if(show_all){
   for (int step = 7; step>=0; step--){
      digitalWrite(SHIFT_IN_PIN, !step_muted[step]);
      digitalWrite(SHCP_PIN, HIGH);
      digitalWrite(SHCP_PIN, LOW);
    }
    digitalWrite(STCP_PIN, HIGH);
    digitalWrite(STCP_PIN, LOW);
 }
}

/**
 * lights the led of the currently active note. for use in play mode.
 * clear all by passing CLEAR_ALL as parameter. only if not "show all active"
 * is selected.
 */
void show_chosen_step(int chosen_step){
  if(!show_all){
    for(int step = 7; step >= 0; step--){
      digitalWrite(SHIFT_IN_PIN, (step) == chosen_step ? HIGH : LOW);
      
      digitalWrite(SHCP_PIN, HIGH);
      digitalWrite(SHCP_PIN, LOW);
    }
  
    digitalWrite(STCP_PIN, HIGH);
    digitalWrite(STCP_PIN, LOW);
  }
} 

void loop() {

  //start of TEMPORARY!!!
  //show_all_active();
  mode = PLAY_MODE;
  show_all = false;
  //Serial.println(digitalRead(2));
  //end of TEMPORARY!!!

  if(!external_sync) external_sync = digitalRead(SYNC_IN_PIN);
  else if((millis()-last_sync_time)>external_time_out) external_sync = false;

  check_toggle_buttons(); 
  
  if(show_all) //if "show all" mode is selected by user...
   show_all_active();

  if(mode == EDIT_MODE);
  else if (mode == PLAY_MODE){
    if(external_sync) {
      
      if(waiting_for_timer){
        if((millis() - last_note_time) > (mspb/note_value)){
          
          external_timer_counter++;  
          if ((external_timer_counter + 1) < note_value){
            triggered_new_note();
          }
          else {
            triggered_new_note();
            waiting_for_sync = true; 
            waiting_for_timer = false;
            external_timer_counter = 0;
          }
        }
      }
      
      sync_reading = digitalRead(SYNC_IN_PIN);
      if(sync_reading != last_sync_reading) {
        if(sync_reading == HIGH){ //sync pulse received externally

        if(!waiting_for_timer){
          
          if(!waiting_for_sync){
            external_sync_counter++;
            if((1/note_value) == external_sync_counter){
              waiting_for_sync = true;
              external_sync_counter = 0;
            }
          }
          
          if(waiting_for_sync){
            triggered_new_note();
            if(note_value < QUAVER) waiting_for_sync = false; //sync pulse from volca is quaver
            else if(note_value > QUAVER) waiting_for_timer = true;
          }
          
         }
          
          mspb = (millis()-last_sync_time); 
          gate_delay = mspb / note_value * note_on_fraction * 2.0;
          last_sync_time = millis();
        } 
          last_sync_reading = sync_reading;
      }
    }
    else {
      
      check_bpm();
      unsigned long note_length = ((60.0 * 500.0) / (note_value*bpm)) ; //or ms per beat!
      gate_delay = ((double) note_length) * note_on_fraction;
      
      if (new_note) {
        new_note = false; 
        triggered_new_note(); 
      } else if ((millis()-last_note_time) > note_length) {
        new_note = true;
      }

    }

    if((millis() - last_gate_time) > gate_delay){ //gate time passed
      send_gate_off();
      show_chosen_step(CLEAR_LED); //clear all (show led only when gate on)
    }
    
  }

}

