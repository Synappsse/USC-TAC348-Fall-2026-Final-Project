#include "Particle.h"

SYSTEM_MODE(AUTOMATIC);
SYSTEM_THREAD(ENABLED);

#define BLYNK_TEMPLATE_ID "TMPL2OdTyJHF-"
#define BLYNK_TEMPLATE_NAME "Final Project"
#define BLYNK_AUTH_TOKEN "U406i38WWsQGzZvp4W4_z7iiS3mOXgHH"

#define ARDUINOJSON_ENABLE_ARDUINO_STRING 1

#include <ArduinoJson.h>
#include <blynk.h>
#include <MFRC522.h>
#include <SparkFunMicroOLED.h>

const int SS_PIN = A0;
const int RST_PIN = A1;

//Setup for buttons
const int BUTTON_PINS[3] = {D2, D3, D4};
const char* BUTTON_COLORS[3] = {"RED", "GREEN", "BLUE"};

int selectedObject = -1; // -1 means no equipment selected
bool checkoutConfirmed = false; //only set to true when the user has confirmed their checkout

int lastButtonReading[3] = {HIGH, HIGH, HIGH};// Prevent one press from registering repeatedly
int buttonState[3] = {HIGH, HIGH, HIGH};

unsigned long lastButtonChange[3] = {0, 0, 0};
const unsigned long BUTTON_DEBOUNCE = 40;

//Pins for speaker
const int SPEAKER_PIN = A2;

//Pin for returns sensors
const int RETURN_BEAM_PIN = D6;
int lastBeamState = HIGH; // Remember the previous sensor reading
bool beamWasBroken = false;//if there was an object in the sensors path or not

//Pins for Magnetic switch

const int CABINET_SWITCH_PIN = D5;
const unsigned long CABINET_WARNING_TIME = 10000;

// Cabinet monitoring
unsigned long checkoutStartedAt = 0;
bool cabinetWasOpened = false;
bool cabinetWarningOn = false;

// Debounce the magnetic switch
int cabinetState = HIGH;
int lastCabinetReading = HIGH;

unsigned long lastCabinetChange = 0;
const unsigned long CABINET_DEBOUNCE = 50;


// OLED Setup
#define PIN_RESET D9
#define DC_JUMPER 1

//Setup for pins of OLED and RFID scanner
MFRC522 mfrc522(SS_PIN, RST_PIN);
MicroOLED oled(MODE_I2C, PIN_RESET, DC_JUMPER);
/****************************************************************************
Item: User class declaration
Purpose: Creates a class for admins to oversee and manage user profiles 
****************************************************************************/
class User {
public:
    String userID; //Hold an individual user id for readability and identity management
    String rfidUID; //ID for authentication, this value is unique and held only by the individual's RFID card
    bool isLATE; //Flag for user's current loaned item. If that item is late, this flag will be on
    int lateCOUNT; //Number of times a specific user has been late. This number CAN exceed the limit of 3 if an admin overrides 

    User(String id, String uid, bool late, int count) {
        userID = id;
        rfidUID = uid;
        isLATE = late;
        lateCOUNT = count;
    }
};

/*This is an array of users with their specific data.
  The array may later be updated for dynamic allocation
  should users wish to be added. As of now, the 3 users 
  are connected to the 2 cards and 1 keychain given in 
  the TAC348 class kit.

  Secondly, this is the starting setup of the users database, 
  it is automatically updated upon startup by grabbing the
  latest information from Losant.
*/
User users[] = {
    User("USER001", "25 F5 1D E0", false, 0), //USER001 will be the sole admin, and a tester
    User("USER002", "5A C1 F3 35", true, 3), //Users 002 and 003 will be the "bad" and "good" users respectively
    User("USER003", "32 7F D0 55", false, 1)
};

const int NUM_USERS = sizeof(users) / sizeof(users[0]);//keeps track of user amount in database
int selectedUser = -1; // keeps track of which user is currently being viewed (before authorization). set to -1 to prevent incorrect selection
int activeBorrower = -1; //keeps track of which user is actively attempting to borrow (after authentication)
bool userRequestSent = false; //starting flag for user database request
bool accountsLoaded = false; //starting flag to check on status and validity of database transfer

//Attempt to retrieve up to date account on users from Losant

void onUserLookup(const char *event, const char *data) {
    //if we don't recieve anything, do nothing
    if (data == NULL) {
        return;
    }

    // Parse the JSON and manage any errors
    StaticJsonDocument<1024> doc;
    DeserializationError error = deserializeJson(doc, data);
    if (error) {
        Serial.println("Error parsing Losant JSON!");
        return;
    }

    // Get the array of user records
    JsonArray records = doc.as<JsonArray>();

    // Create temp variables until this function is resolved completely
    bool found[NUM_USERS] = {};
    bool savedLate[NUM_USERS] = {};
    int savedCount[NUM_USERS] = {};

    // Check that the record contains valid fields
    for (JsonObject record : records) {
        if (!record["userID"].is<const char*>() ||
            !record["isLATE"].is<bool>() ||
            !record["lateCOUNT"].is<int>()) {
            continue;
        }

        String id = record["userID"].as<const char*>();

        // Match the Losant account to our RFID user
        for (int i = 0; i < NUM_USERS; i++) {
            if (id == users[i].userID) {
                found[i] = true;
                savedLate[i] = record["isLATE"].as<bool>();
                savedCount[i] = record["lateCOUNT"].as<int>();
                break;
            }
        }
    }

    // Only update if the correct, full database is given
    for (int i = 0; i < NUM_USERS; i++) {
        if (!found[i]) {
            Serial.println("Incomplete user records from Losant!");
            return;
        }
    }

    // Update the Photon's local info
    for (int i = 0; i < NUM_USERS; i++) {

        users[i].isLATE = savedLate[i];
        users[i].lateCOUNT = savedCount[i];

        Serial.print("Loaded: ");
        Serial.print(users[i].userID);
        Serial.print(" | isLATE: ");
        Serial.print(users[i].isLATE);
        Serial.print(" | lateCOUNT: ");
        Serial.println(users[i].lateCOUNT);
    }

    accountsLoaded = true;
    Serial.println("ALL USER ACCOUNTS SYNCHRONIZED!");
}


/****************************************************************************
Item: Show message on OLED
Purpose: Takes inputs for OLED and prints them out. Helps display user 
and loan info
****************************************************************************/
void showMessage(String line1, String line2, String line3) {
    oled.clear(PAGE);
    oled.setCursor(0, 0);
    oled.setFontType(0);

    oled.println(line1);
    oled.println(line2);
    oled.println(line3);

    oled.display();
}

//Displays the checkout menu for users to select from
void showCheckoutMenu() {
    showMessage("1 RED", "2 GREEN", "3 BLUE");
}

//Setup for object selection
void handleObjectButton(int item) {

    // Must have a valid logged-in borrower
    if (!accountsLoaded || activeBorrower == -1) {
        Serial.println("Scan your RFID card first!");
        return;
    }

    if (checkoutConfirmed) {
        Serial.println("Selection already confirmed.");
        return;
    }

    // Same button pressed twice
    if (selectedObject == item) {
        checkoutConfirmed = true;

        // Begin monitoring the cabinet
        checkoutStartedAt = millis();
        cabinetWasOpened = (cabinetState == HIGH);
        cabinetWarningOn = false;

        Serial.print("Checkout selected: Object ");
        Serial.println(item + 1);

        Serial.println("Take your item and close the cabinet.");

        showMessage("TAKE ITEM", "CLOSE", "DRAWER");
    }
    else {
        // First press or different button selected
        selectedObject = item;

        Serial.print("Selected Object ");
        Serial.println(item + 1);

        showMessage(
            "OBJ" + String(item + 1) + " " + BUTTON_COLORS[item],
            "LOAN TBD",
            "SAME=YES"
        );
    }
}

/****************************************************************************
Item: Write to user 
Purpose: Unlocks user accounts if an admin sends the unlock request 
****************************************************************************/
// Read the user selected in the Blynk app
BLYNK_WRITE(V0) {
    int choice = param.asInt();

    if (choice >= 0 && choice < NUM_USERS) {
        selectedUser = choice;

        Serial.print("Admin selected: ");
        Serial.println(users[selectedUser].userID);
    }
}

//read the selected user and check to see if the restriction can be cleared
BLYNK_WRITE(V1) {

    if (!accountsLoaded) {
      Serial.println("Cannot clear restriction: accounts not loaded.");
      return;
    }
    //reqrites the parameter as an integer so we can read it
    if (param.asInt() == 1) {

        if (selectedUser >= 0 && selectedUser < NUM_USERS) { //if the selected user exists....

            // Remove the account restriction
            users[selectedUser].isLATE = false;

            Serial.print("Restriction cleared for: ");
            Serial.println(users[selectedUser].userID);

            //test to ensure late count still works
            Serial.print("Late Return Count: ");
            Serial.println(users[selectedUser].lateCOUNT);

            //update our information in Losant
            //losant webhook: https://triggers.losant.com/webhooks/PoojXddN0X6lCraRojiseg6MS_ZgqerYXxfTZZ-X
            Particle.publish("it_restriction_cleared", users[selectedUser].userID);
        }
        
        //If something went wrong just throw an error
        else {
            Serial.println("Error. Please select a user first.");
        }
    }
}


/*****************************************************************************
Item: Speaker functions
Purpose: Sets up the specific sounds for user auth feedback
*****************************************************************************/
void playAccessGranted() {
    tone(SPEAKER_PIN, 2200, 150);
}

void playAccessDenied() {
    tone(SPEAKER_PIN, 1100, 450);
}

/*****************************************************************************
Item: Setup function
Purpose: Sets up the P2, RFID devices and prints generic start message
*****************************************************************************/
void setup() {
    Serial.begin(9600); 

    pinMode(SPEAKER_PIN, OUTPUT); //set the speaker pin to output so we can play sounds

    // Initialize the return sensor
    pinMode(RETURN_BEAM_PIN, INPUT);
    lastBeamState = digitalRead(RETURN_BEAM_PIN);

    Serial.println("Return sensor initialized!"); 

    //Initialize the buttons
    for (int i = 0; i < 3; i++) {
    pinMode(BUTTON_PINS[i], INPUT);
}

    //Initialize the magnetic switch
    pinMode(CABINET_SWITCH_PIN, INPUT);

    cabinetState = digitalRead(CABINET_SWITCH_PIN);
    lastCabinetReading = cabinetState;

    //Initialize the RFID scanner
    mfrc522.setSPIConfig();
    mfrc522.PCD_Init();

    // Initialize the OLED screen
    oled.begin();
    oled.clear(ALL);
    oled.clear(PAGE);

    // Display the startup message
    showMessage("IT LOANS", "SCAN CARD", "");

    // Connect to Blynk for admin controls
    // Connect to Blynk for admin controls
    delay(5000);

    //subsrcibe to any updates for user accounts
    Particle.subscribe("hook-response/it_users_request", onUserLookup);

    Serial.println("Connecting to Blynk...");
    Blynk.begin(BLYNK_AUTH_TOKEN);
    Serial.println("Blynk connection finished!");
}

/*****************************************************************************
Item: Main Loop
Purpose: Keeps the system running once setup is complete. 
*****************************************************************************/
void loop() {
    //starts communication with the Blynk app
    Blynk.run();

    // Read the magnetic cabinet switch
    int cabinetReading = digitalRead(CABINET_SWITCH_PIN);

    if (cabinetReading != lastCabinetReading) {
        lastCabinetChange = millis();
    }

    // Only process a stable reading
    if (millis() - lastCabinetChange >= CABINET_DEBOUNCE) {

        if (cabinetReading != cabinetState) {
            cabinetState = cabinetReading;

            if (checkoutConfirmed) {

                if (cabinetState == HIGH) {
                    cabinetWasOpened = true;
                    Serial.println("CABINET OPENED");
                }

                // Cabinet was opened, and is now closed
                else if (cabinetWasOpened) {

                    Serial.println("CABINET CLOSED");

                    if (cabinetWarningOn) {
                        noTone(SPEAKER_PIN);
                        cabinetWarningOn = false;
                    }

                    Serial.println("Cabinet test complete!");
                    Serial.println("No checkout recorded yet.");

                    checkoutConfirmed = false;
                    selectedObject = -1;
                    cabinetWasOpened = false;

                    showMessage("CABINET", "CLOSED", "TEST ONLY");
                }
            }
        }
    }

    lastCabinetReading = cabinetReading;

    // Start warning if the cabinet remains open
    // for 10 seconds after checkout confirmation
    if (checkoutConfirmed &&
        cabinetState == HIGH &&
        millis() - checkoutStartedAt >= CABINET_WARNING_TIME &&
        !cabinetWarningOn) {

        cabinetWarningOn = true;

        Serial.println("WARNING: CLOSE THE CABINET!");

        tone(SPEAKER_PIN, 1100);
        showMessage("PLEASE", "CLOSE", "CABINET");
    }


    // Check all three equipment buttons
    for (int i = 0; i < 3; i++) {
        int reading = digitalRead(BUTTON_PINS[i]);
        if (reading != lastButtonReading[i]) {
            lastButtonChange[i] = millis();
        }

        // Only accept a stable button state
        if (millis() - lastButtonChange[i] > BUTTON_DEBOUNCE) {
            if (reading != buttonState[i]) {
                buttonState[i] = reading;
                if (buttonState[i] == LOW) {
                    handleObjectButton(i);
                }
            }
        }
        lastButtonReading[i] = reading;
    }

    int beamState = digitalRead(RETURN_BEAM_PIN);
    if (beamState != lastBeamState) { //if a change in the sensor is detected, check if the beam was broken or restored
        if (beamState == LOW) {
            Serial.println("RETURN IN PROGRESS");
            beamWasBroken = true;
        }
        else if (beamWasBroken) {
            Serial.println("RETURN COMPLETE");
            beamWasBroken = false;
        }
        lastBeamState = beamState;
    }

   // Request saved users once after startup
   static unsigned long loopStartTime = millis();

   //if we are on time and ready to begin, lookup the user records
    if (!userRequestSent && Particle.connected() && millis() - loopStartTime >= 3000) {
        userRequestSent = true;

        Serial.println("Requesting user records from Losant...");
        Particle.publish("it_users_request", "load");
    }

    //Does not proceed until the full database is loaded
    if (!accountsLoaded) {
        return;
    }
    //if no card is available, do nothing
    if (!mfrc522.PICC_IsNewCardPresent()) {
        return;
    }
    if (!mfrc522.PICC_ReadCardSerial()) {
        return;
    }

    //Prepare to grab the ID of any scanned card
    String scanId = "";
    for (byte i = 0; i < mfrc522.uid.size; i++) {
        if (i > 0) {
            scanId += " ";
        }
        if (mfrc522.uid.uidByte[i] < 0x10) {
            scanId += "0";
        }
        scanId += String(mfrc522.uid.uidByte[i], HEX);
    }
   
    //Clean up card ID text and present output based on current database
    scanId.toUpperCase(); 
    Serial.println("**************************");
    Serial.print("Scanned UID: ");
    Serial.println(scanId);
    Serial.println("**************************");

    //begin search for user connected to the RFID card
    bool userFound = false;
    for (int i = 0; i < NUM_USERS; i++) {
        if (scanId == users[i].rfidUID) { //attempts to find RFIDS tag info in the database
            userFound = true;
            
            // If this user is already logged in, log them out and prevent wasting time
            if (activeBorrower == i) {

                if (checkoutConfirmed) {
                    Serial.println("Close the cabinet before logging out.");

                    showMessage("CLOSE", "CABINET", "FIRST");
                    break;
                }

                Serial.print("Goodbye!");
                activeBorrower = -1;
                selectedObject = -1;
                checkoutConfirmed = false;

                showMessage("Goodbye!", "", "");
                delay(3000);//wait 3 seconds to confirm user logout then return to main page
                
                showMessage("IT LOANS", "SCAN CARD", "");
                break;
            }

            if (activeBorrower != -1) { //If we are logged in as user x, but user y scans in... (-1 is the flag for NO CURRENT active user)
                Serial.print("Station in use by: ");
                Serial.println(users[activeBorrower].userID);

                playAccessDenied();
                showMessage("WRONG ACTIVE USER", "PLEASE", "LOG OUT FIRST"); //let user y know they must wait
                break;
            }

            //testing output for identified users
            //prints test result of users history
            Serial.print("User: ");
            Serial.println(users[i].userID);

            Serial.print("Late Returns: ");
            Serial.println(users[i].lateCOUNT);

            // Check current account restrictions
            //relay whether user can use an item or not
            if (users[i].isLATE) {
                Serial.println("ACCOUNT RESTRICTED");
                Serial.println("ACCESS DENIED");
                
                playAccessDenied();
                showMessage(users[i].userID, "USER", "LATE!        Return your item!"); // extra space is workaround for small display screen
            }
           
            else {
                Serial.println("ACCOUNT ACTIVE");
                Serial.println("ACCESS GRANTED");

                // Log in the authenticated user
                                
                activeBorrower = i;
                selectedObject = -1;
                checkoutConfirmed = false;

                Serial.print("User logged in: ");
                Serial.println(users[activeBorrower].userID);
                
                playAccessGranted();
                showMessage(users[i].userID, "ACCESS", "GRANTED");
                delay(3000);

                showCheckoutMenu();
            }


            // Check repeated late returns
            if (users[i].lateCOUNT >= 3 && users[i].isLATE) { // this needs to be updated later for the override feature
                Serial.println("USER FLAGGED");
                Serial.println("Please speak to your admin!"); //This will prompt user to request an override from admin. Admin will do this via Blynk
            }
            break;
        }
    }

    //if the card we read is not in the data base, then deny access entirely
    if (userFound == false) {
        Serial.println("UNKNOWN CARD");
        Serial.println("ACCESS DENIED");

        playAccessDenied();
        showMessage("UNKNOWN", "CARD", "DENIED");
    }
    mfrc522.PICC_HaltA();
}
