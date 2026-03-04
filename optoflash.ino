// Flashes LED for optogenetics. 
// Written by ADW. 


//void loop() {
//  digitalWrite(ledPin, HIGH);    // set the LED on
//  delay(1000);                  // wait for a second
//  digitalWrite(ledPin, LOW);    // set the LED off
//  delay(2000);                  // wait for a second
//}

const int ledPin =  0;          // the number of the LED pin (must be PWM-capable for dimming)
int ledState = LOW;             // ledState used to set the LED
boolean newData = false;
boolean startSignal = false;
const bool ctrlActiveLow = true;   // BuckPuck CTRL: LOW=ON, HIGH=OFF
const bool driveInvertsCtrl = true; // transistor inverts pin -> CTRL

// Buffer must hold full START command with burst parameters
const byte numChars = 128;
char receivedChars[numChars];
char tempChars[numChars];
char messageFromPC[numChars] = {0};


// Reasonable defaults for optogenetics LED control
unsigned long exptLength = 10UL * 60UL * 1000UL;   // 10 min in ms
unsigned long restTime = 0;                        // ms, optional initial rest
int pulseWidth = 10;   // ms
int frequency = 50;    // ms period (20 Hz)
// Optional burst mode (Case 2)
int burstOn = 0;       // ms (0 = disabled, use single-pulse mode)
int burstOff = 0;      // ms
int burstDuration = 0; // ms (total time to run burst within each cycle)
int restBetweenBursts = 0; // ms (LED off after burst, per cycle)
int brightnessPercent = 100; // 0-100 (% duty for ON time)
unsigned long startTime = 0;  //ms
unsigned long elapsedExptTime = 0; // ms
unsigned long startPulseTime = 0;
unsigned long elapsedPulseTime = 0;
unsigned long startCycleTime = 0;
unsigned long elapsedCycleTime =0;
unsigned long startSubpulseTime = 0;
unsigned long elapsedSubpulseTime = 0;



//============

void setup() {
    Serial.begin(9600);
    Serial.println("Input data: SIGNAL, Experiment Length (min), Initial Rest (min), Stimulation Period (ms), Mode (Continuous|Flicker), [LED On (ms), LED Off (ms)], Rest Interval (ms), Power (%)"); 
    Serial.println("Enter data like <START, 5, 0, 5000, Continuous, 25000, 100>");
    Serial.println("Or for flicker: <START, 5, 0, 5000, Flicker, 100, 900, 25000, 100>");
    Serial.println();
    // initialize the digital pin as an output.
    pinMode(ledPin, OUTPUT);
    analogWrite(ledPin, pwmOff());
}

//============
    // Convert the input values into an experiment. Each experiment consists of:
    // --> A total experiment length (should match the microscope time)
    // --> A starting rest period
    // --> The cycle rate (given in hertz)
    // --> The pulse width (given in ms)
    // Hz are converted to millisecond time too. 
//=============


void loop() {
    recvWithStartEndMarkers();
    if (newData == true) {
        strcpy(tempChars, receivedChars);
            // this temporary copy is necessary to protect the origianal data
            //   because strtok() used in parseData() replaces the commas with \0
        parseData();
        showParsedData();
        newData = false;
    }
    if (startSignal == true) {
        if (startTime == 0) {
            startTime = millis();
            Serial.print("Experiment began at ");
            Serial.print(startTime);
            Serial.println(" ms.");
            elapsedExptTime = millis() - startTime;    
        }
        else if (elapsedExptTime >= exptLength) {
            startSignal = false;
            Serial.print("Experiment ended at ");
            Serial.print(millis());
            Serial.println(" ms.");
            elapsedExptTime = 0;
            startTime = 0;
            startPulseTime = 0;
            elapsedPulseTime = 0;
            startCycleTime = 0;
            elapsedCycleTime = 0; 
            elapsedExptTime = millis() - startTime;    
        }
        else {
            if (elapsedExptTime >= restTime) {
                if (elapsedCycleTime == 0) {
                    startCycleTime = millis();
                    //Serial.print("Cycle began at ");
                    //Serial.print(startCycleTime);
                    //Serial.println(" ms.");
                    elapsedCycleTime = millis() - startCycleTime;
                }
                else if (elapsedCycleTime < frequency) {
                    // Burst mode (Case 2): burstOn/off for burstDuration, then off for restBetweenBursts
                    if (burstOn > 0 && burstDuration > 0) {
                        if (elapsedCycleTime < (unsigned long)burstDuration) {
                            if (startSubpulseTime == 0) {
                                startSubpulseTime = millis();
                                elapsedSubpulseTime = 0;
                            }
                            elapsedSubpulseTime = millis() - startSubpulseTime;
                            int subpulsePeriod = burstOn + burstOff;
                            if (subpulsePeriod <= 0) {
                                analogWrite(ledPin, pwmOff());
                            } else {
                                if (elapsedSubpulseTime >= (unsigned long)subpulsePeriod) {
                                    startSubpulseTime = millis();
                                    elapsedSubpulseTime = 0;
                                }
                                if (elapsedSubpulseTime < (unsigned long)burstOn) {
                                    analogWrite(ledPin, pwmOn());
                                } else {
                                    analogWrite(ledPin, pwmOff());
                                }
                            }
                        } else {
                            analogWrite(ledPin, pwmOff());
                        }
                    }
                    // Single-pulse mode (Case 1)
                    else {
                        if (elapsedPulseTime == 0) {
                            startPulseTime= millis();
                            //Serial.print("Pulse began at ");
                            //Serial.print(startPulseTime);
                            //Serial.println(" ms.");
                            analogWrite(ledPin, pwmOn());
                            elapsedPulseTime = millis() - startPulseTime;
                        }
                        if (elapsedPulseTime < pulseWidth) {
                            analogWrite(ledPin, pwmOn());
                            elapsedPulseTime = millis() - startPulseTime;
                        }
                        else {
                            analogWrite(ledPin, pwmOff());
                            elapsedPulseTime = millis() - startPulseTime;
                        }
                    }
                    elapsedCycleTime = millis() - startCycleTime;
                }
                else {
                    Serial.print("REACHED END OF CYCLE AT ");
                    Serial.println(elapsedCycleTime);
                    startPulseTime = 0;
                    elapsedPulseTime = 0;
                    startCycleTime = 0;
                    elapsedCycleTime = 0;  
                    startSubpulseTime = 0;
                    elapsedSubpulseTime = 0;
                    analogWrite(ledPin, pwmOff());         
                } 
            }
        }
        elapsedExptTime = millis() - startTime;    
    }
    else if (startSignal == false && elapsedExptTime > 0) {
        analogWrite(ledPin, pwmOff()); 
        Serial.print("Experiment stopped at ");
        Serial.print(millis());
        Serial.println(" ms.");
        elapsedExptTime = 0;
        startTime = 0;
        startPulseTime = 0;
        elapsedPulseTime = 0;
        startCycleTime = 0;
        elapsedCycleTime = 0; 
        startSubpulseTime = 0;
        elapsedSubpulseTime = 0;
    }
}

void recvWithStartEndMarkers() {
    static boolean recvInProgress = false;
    static byte ndx = 0;
    char startMarker = '<';
    char endMarker = '>';
    char rc;

    while (Serial.available() > 0 && newData == false) {
        rc = Serial.read();

        if (recvInProgress == true) {
            if (rc != endMarker) {
                receivedChars[ndx] = rc;
                ndx++;
                if (ndx >= numChars) {
                    ndx = numChars - 1;
                }
            }
            else {
                receivedChars[ndx] = '\0'; // terminate the string
                recvInProgress = false;
                ndx = 0;
                newData = true;
            }
        }

        else if (rc == startMarker) {
            recvInProgress = true;
        }
    }
}

static void trimInPlace(char *s) {
    if (s == NULL) {
        return;
    }
    // Trim leading whitespace
    char *start = s;
    while (*start == ' ' || *start == '\t' || *start == '\r' || *start == '\n') {
        start++;
    }
    if (start != s) {
        memmove(s, start, strlen(start) + 1);
    }
    // Trim trailing whitespace
    size_t len = strlen(s);
    while (len > 0) {
        char c = s[len - 1];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            s[len - 1] = '\0';
            len--;
        } else {
            break;
        }
    }
}

int pwmOn() {
    int pct = brightnessPercent;
    if (pct < 0) {
        pct = 0;
    } else if (pct > 100) {
        pct = 100;
    }
    int value = (pct * 255) / 100;
    int level = ctrlActiveLow ? (255 - value) : value;
    return driveInvertsCtrl ? (255 - level) : level;
}

int pwmOff() {
    int level = ctrlActiveLow ? 255 : 0;
    return driveInvertsCtrl ? (255 - level) : level;
}

void parseData() {      // split the data into its parts

    char * strtokIndx; // this is used by strtok() as an index

    strtokIndx = strtok(tempChars,",");      // get the first part - the string
    if (strtokIndx == NULL) {
        return;
    }
    trimInPlace(strtokIndx);
    strcpy(messageFromPC, strtokIndx); // copy it to messageFromPC
    const char * startMsg = "START"; 
    const char * stopMsg = "STOP";
    const char * statusMsg = "STATUS";
    if (strcmp(messageFromPC,startMsg)==0) {
        Serial.println("SIGNAL START DETECTED");
        startSignal = true;
    }
    else if (strcmp(messageFromPC,stopMsg)==0) {
        Serial.println("SIGNAL STOP DETECTED");
        startSignal = false;
    }
    else if (strcmp(messageFromPC,statusMsg)==0) {
        Serial.println("STATUS REQUEST");
    }
    // Parse any remaining parameters if present (works for START/STOP too)
    strtokIndx = strtok(NULL, ","); // this continues where the previous call left off
    if (strtokIndx != NULL) {
        trimInPlace(strtokIndx);
        long exptMin = atol(strtokIndx);
        if (exptMin > 0) {
            exptLength = (unsigned long)exptMin * 60UL * 1000UL; 
        }

        strtokIndx = strtok(NULL, ",");
        if (strtokIndx != NULL) {
            trimInPlace(strtokIndx);
            long restMin = atol(strtokIndx);
            if (restMin >= 0) {
                restTime = (unsigned long)restMin * 60UL * 1000UL;
            }
        }

        strtokIndx = strtok(NULL, ",");
        if (strtokIndx != NULL) {
            trimInPlace(strtokIndx);
            int pw = atoi(strtokIndx);
            if (pw > 0) {
                pulseWidth = pw;
            }
        }

        strtokIndx = strtok(NULL, ",");
        if (strtokIndx != NULL) {
            trimInPlace(strtokIndx);
            float hz = atof(strtokIndx);
            if (hz > 0.0f) {
                frequency = static_cast<int>(1000.0f / hz);
            }
        }

        // Optional burst parameters (Case 2)
        strtokIndx = strtok(NULL, ",");
        if (strtokIndx != NULL) {
            trimInPlace(strtokIndx);
            int bo = atoi(strtokIndx);
            if (bo > 0) {
                burstOn = bo;
            }
        }

        strtokIndx = strtok(NULL, ",");
        if (strtokIndx != NULL) {
            trimInPlace(strtokIndx);
            int bf = atoi(strtokIndx);
            if (bf >= 0) {
                burstOff = bf;
            }
        }

        strtokIndx = strtok(NULL, ",");
        if (strtokIndx != NULL) {
            trimInPlace(strtokIndx);
            int bd = atoi(strtokIndx);
            if (bd > 0) {
                burstDuration = bd;
            }
        }

        strtokIndx = strtok(NULL, ",");
        if (strtokIndx != NULL) {
            trimInPlace(strtokIndx);
            int rb = atoi(strtokIndx);
            if (rb >= 0) {
                restBetweenBursts = rb;
            }
        }

        // Optional brightness (%)
        strtokIndx = strtok(NULL, ",");
        if (strtokIndx != NULL) {
            trimInPlace(strtokIndx);
            int bp = atoi(strtokIndx);
            if (bp >= 0 && bp <= 100) {
                brightnessPercent = bp;
            }
        }

        if (burstDuration > 0) {
            frequency = burstDuration + restBetweenBursts;
        }
    }

    // Validation / clamping
    if (frequency > 0 && pulseWidth > frequency) {
        pulseWidth = frequency;
    }
    if (burstDuration > 0) {
        if (burstOn <= 0) {
            burstOn = 1;
        }
        if (burstOff < 0) {
            burstOff = 0;
        }
    }
}

//============

void showParsedData() {
    Serial.print("Message ");
    Serial.println(messageFromPC);
    Serial.print("Experiment Length (ms) ");
    Serial.println(exptLength);
    Serial.print("Rest Length (ms) ");
    Serial.println(restTime);
    Serial.print("Pulse Width (ms) ");
    Serial.println(pulseWidth);
    Serial.print("Frequency (ms period) ");
    Serial.println(frequency);
    Serial.print("Burst On (ms) ");
    Serial.println(burstOn);
    Serial.print("Burst Off (ms) ");
    Serial.println(burstOff);
    Serial.print("Burst Duration (ms) ");
    Serial.println(burstDuration);
    Serial.print("Rest Between Bursts (ms) ");
    Serial.println(restBetweenBursts);
    Serial.print("Brightness (%) ");
    Serial.println(brightnessPercent);
}
