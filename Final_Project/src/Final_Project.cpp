
#include "Particle.h"

SYSTEM_MODE(AUTOMATIC);
SYSTEM_THREAD(ENABLED);

#define BLYNK_TEMPLATE_ID "TMPL2OdTyJHF-"
#define BLYNK_TEMPLATE_NAME "Final Project"
#define BLYNK_AUTH_TOKEN "U406i38WWsQGzZvp4W4_z7iiS3mOXgHH"

#include <blynk.h>
#include <MFRC522.h>
#include <SparkFunMicroOLED.h>

const int SS_PIN = A0;
const int RST_PIN = A1;

// OLED Setup
#define PIN_RESET D9
#define DC_JUMPER 1

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
*/
User users[] = {
    User("USER001", "25 F5 1D E0", false, 0), //USER001 will be the sole admin, and a tester
    User("USER002", "5A C1 F3 35", true, 3), //Users 002 and 003 will be the "bad" and "good" users respectively
    User("USER003", "32 7F D0 55", false, 1)
};

const int NUM_USERS = sizeof(users) / sizeof(users[0]);//keeps track of user amount in database
int selectedUser = -1; // keeps track of which user is currently being viewed. set to -1 to prevent incorrect selection
bool userRequestSent = false;

//Attempt to retrieve up to date account on users from Losant
void onUserLookup(const char *event, const char *data) {
    Serial.println("**************************");
    Serial.print("Losant response event: ");
    Serial.println(event);
    Serial.println("**************************");

    if (data != NULL) {
        Serial.println(data);
    }
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
Item: Setup function
Purpose: Sets up the P2, RFID devices and prints generic start message
*****************************************************************************/
void setup() {
    Serial.begin(9600); 

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

    //Request the most up to date user acc info 

// Request saved users once after startup
   static unsigned long loopStartTime = millis();

   //if we are on time and ready to begin, lookup the user records
    if (!userRequestSent && Particle.connected() && millis() - loopStartTime >= 3000) {
        userRequestSent = true;

        Serial.println("Requesting user records from Losant...");
        Particle.publish("it_users_request", "load");
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
        if (scanId == users[i].rfidUID) {
            userFound = true;

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

                showMessage(users[i].userID, "USER", "LATE!        Return your item!"); // workaround to small display screen
            }
            else {
                Serial.println("ACCOUNT ACTIVE");
                Serial.println("ACCESS GRANTED");

                showMessage(users[i].userID, "ACCESS", "GRANTED");

            }

            // Check repeated late returns
            if (users[i].lateCOUNT >= 3) { // this needs to be updated later for the override feature
                Serial.println("USER FLAGGED");
                Serial.println("Please speak to your admin!");
            }
            break;
        }
    }

    if (userFound == false) {
        Serial.println("UNKNOWN CARD");
        Serial.println("ACCESS DENIED");

        showMessage("UNKNOWN", "CARD", "DENIED");
    }
    mfrc522.PICC_HaltA();

    /*
    delay(2000);
    showMessage("IT LOANS", "SCAN CARD", "");
    */
}
