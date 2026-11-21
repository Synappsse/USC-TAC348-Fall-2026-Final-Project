
#include <MFRC522.h>
#include <SparkFunMicroOLED.h>

const int SS_PIN = A0;
const int RST_PIN = A1;


#define BLYNK_TEMPLATE_ID "YOUR_TEMPLATE_ID"
#define BLYNK_TEMPLATE_NAME "IT Checkout Admin"
#define BLYNK_AUTH_TOKEN "YOUR_AUTH_TOKEN"

#include <blynk.h>

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
int selectedUser = 0; // keeps track of which user is currently being viewed

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

// Read the clear restriction button
BLYNK_WRITE(V1) {
    if (param.asInt() == 1) {
        Serial.print("Clear restriction requested for:" + users[selectedUser].userID);
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
    delay(5000);
    Blynk.begin(BLYNK_AUTH_TOKEN);
}

/*****************************************************************************
Item: Main Loop
Purpose: Keeps the system running once setup is complete. 
*****************************************************************************/
void loop() {
    //starts communication with the Blynk app
    Blynk.run();

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

                showMessage(users[i].userID, "USER", "LATE!", "Return your item!");
            }
            else {
                Serial.println("ACCOUNT ACTIVE");
                Serial.println("ACCESS GRANTED");
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
