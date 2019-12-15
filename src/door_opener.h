/**************************************************************************
  @author   Elmü, Michael Rüttgers
  @see http://www.codeproject.com/Articles/1096861/DIY-electronic-RFID-Door-Lock-with-Battery-Backup

**************************************************************************/

// This compiler switch defines if you use AES (128 bit) or DES (168 bit) for the PICC master key and the application master key.
// Cryptographers say that AES is better.
// But the disadvantage of AES encryption is that it increases the power consumption of the card more than DES.
// The maximum read distance is 5,3 cm when using 3DES keys and 4,0 cm when using AES keys.
// (When USE_DESFIRE == false the same Desfire card allows a distance of 6,3 cm.)
// If the card is too far away from the antenna you get a timeout error at the moment when the Authenticate command is executed.
// IMPORTANT: Before changing this compiler switch, please execute the RESTORE command on all personalized cards!
#define USE_AES false

// This define should normally be false
// If this is true you can use Classic cards / keyfobs additionally to Desfire cards.
// This means that the code is compiled for Defire cards, but when a Classic card is detected it will also work.
// This mode is not recommended because Classic cards do not offer the same security as Desfire cards.
#define ALLOW_ALSO_CLASSIC true

// This password will be required when entering via Terminal
// If you define an empty string here, no password is requested.
// If any unauthorized person may access the dooropener hardware phyically you should provide a password!
#define PASSWORD "ihrkommthiernichtrein"

// The interval of inactivity in minutes after which the password must be entered again (automatic log-off)
#define PASSWORD_TIMEOUT 5

// This Arduino / Teensy pin is connected to the relay that opens the door 1
#define DOOR_1_PIN D1

// This Arduino / Teensy pin is connected to the optional relay that opens the door 2
#define DOOR_2_PIN D2

// This Arduino / Teensy pin is connected to the PN532 RSTPDN pin (reset the PN532)
// When a communication error with the PN532 is detected the board is reset automatically.
#define RESET_PIN D3
// The software SPI SCK  pin (Clock)
#define SPI_CLK_PIN D5
// The software SPI MISO pin (Master In, Slave Out)
#define SPI_MISO_PIN D6
// The software SPI MOSI pin (Master Out, Slave In)
#define SPI_MOSI_PIN D7
// The software SPI SSEL pin (Chip Select)
#define SPI_CS_PIN D8

// The interval in milliseconds that the relay is powered which opens the door
#define OPEN_INTERVAL 3000

#define OPEN_INVERT true

// This is the interval that the RF field is switched off to save battery.
// The shorter this interval, the more power is consumed by the PN532.
// The longer  this interval, the longer the user has to wait until the door opens.
// The recommended interval is 1000 ms.
// Please note that the slowness of reading a Desfire card is not caused by this interval.
// The SPI bus speed is throttled to 10 kHz, which allows to transmit the data over a long cable,
// but this obviously makes reading the card slower.
#define RF_OFF_INTERVAL 200

#if USE_AES
#define DESFIRE_KEY_TYPE AES
#define DEFAULT_APP_KEY gi_PN532.AES_DEFAULT_KEY
#else
#define DESFIRE_KEY_TYPE DES
#define DEFAULT_APP_KEY gi_PN532.DES3_DEFAULT_KEY
#endif

#include "Desfire.h"
#include "Secrets.h"
#include "Buffer.h"
#include "user_manager.h"

// The tick counter starts at zero when the CPU is reset.
// This interval is added to the 64 bit tick count to get a value that does not start at zero,
// because gu64_LastPasswd is initialized with 0 and must always be in the past.
#define PASSWORD_OFFSET_MS (2 * PASSWORD_TIMEOUT * 60 * 1000)

enum eLED
{
    LED_OFF,
    LED_RED,
    LED_GREEN,
};

struct kCard
{
    byte u8_UidLength;  // UID = 4 or 7 bytes
    byte u8_KeyVersion; // for Desfire random ID cards
    bool b_PN532_Error; // true -> the error comes from the PN532, false -> crypto error
    eCardType e_CardType;
};

class DoorOpener
{
public:
    void setup()
    {
        gs8_CommandBuffer[0] = 0;

        Utils::SetPinMode(DOOR_1_PIN, OUTPUT);
        Utils::WritePin(DOOR_1_PIN, OPEN_INVERT ? HIGH : LOW);

        Utils::SetPinMode(DOOR_2_PIN, OUTPUT);
        Utils::WritePin(DOOR_2_PIN, OPEN_INVERT ? HIGH : LOW);

        Utils::SetPinMode(LED_BUILTIN, OUTPUT);

        FlashLED(LED_GREEN, 1000);

        // Software SPI is configured to run a slow clock of 10 kHz which can be transmitted over longer cables.
        gi_PN532.InitSoftwareSPI(SPI_CLK_PIN, SPI_MISO_PIN, SPI_MOSI_PIN, SPI_CS_PIN, RESET_PIN);

        InitReader(false);

        gi_PiccMasterKey.SetKeyData(SECRET_PICC_MASTER_KEY, sizeof(SECRET_PICC_MASTER_KEY), CARD_KEY_VERSION);
    }

    void loop()
    {
        bool b_KeyPress = ReadKeyboardInput();

        uint64_t u64_StartTick = Utils::GetMillis64();

        static uint64_t u64_LastRead = 0;
        if (gb_InitSuccess)
        {
            // While the user is typing do not read the card to avoid delays and debug output.
            if (b_KeyPress)
            {
                u64_LastRead = u64_StartTick + 1000; // Give the user 1000 ms + RF_OFF_INTERVAL between each character
                return;
            }

            // Turn on the RF field for 100 ms then turn it off for one second (RF_OFF_INTERVAL) to safe battery
            if ((int)(u64_StartTick - u64_LastRead) < RF_OFF_INTERVAL)
                return;
        }

        do // pseudo loop (just used for aborting with break;)
        {
            if (!gb_InitSuccess)
            {
                InitReader(true); // flash red LED for 2.4 seconds
                break;
            }

            kUser k_User;
            kCard k_Card;
            if (!ReadCard(k_User.ID.u8, &k_Card))
            {
                if (IsDesfireTimeout())
                {
                    // Nothing to do here because IsDesfireTimeout() prints additional error message and blinks the red LED
                }
                else if (k_Card.b_PN532_Error) // Another error from PN532 -> reset the chip
                {
                    InitReader(true); // flash red LED for 2.4 seconds
                }
                else // e.g. Error while authenticating with master key
                {
                    FlashLED(LED_RED, 1000);
                }

                Utils::Print("> ");
                break;
            }

            // No card present in the RF field
            if (k_Card.u8_UidLength == 0)
            {
                gu64_LastID = 0;

                FlashLED(LED_GREEN, 20);
                break;
            }

            // Still the same card present
            if (gu64_LastID == k_User.ID.u64)
                break;

            // A different card was found in the RF field
            // OpenDoor() needs the RF field to be ON (for CheckDesfireSecret())
            OpenDoor(k_User.ID.u64, &k_Card, u64_StartTick);
            Utils::Print("> ");
        } while (false);

        // Turn off the RF field to save battery
        // When the RF field is on,  the PN532 board consumes approx 110 mA.
        // When the RF field is off, the PN532 board consumes approx 18 mA.
        gi_PN532.SwitchOffRfField();

        u64_LastRead = Utils::GetMillis64();
    }

private:
    char gs8_CommandBuffer[500];  // Stores commands typed by the user via Terminal and the password
    uint32_t gu32_CommandPos = 0; // Index in gs8_CommandBuffer
    uint64_t gu64_LastPasswd = 0; // Timestamp when the user has enetered the password successfully
    uint64_t gu64_LastID = 0;     // The last card UID that has been read by the RFID reader
    bool gb_InitSuccess = false;  // true if the PN532 has been initialized successfully
    Desfire gi_PN532; // The class instance that communicates with Mifare Desfire cards
    DESFIRE_KEY_TYPE gi_PiccMasterKey;
    UserManager userManager;

    // Reset the PN532 chip and initialize, set gb_InitSuccess = true on success
    // If b_ShowError == true -> flash the red LED very slowly
    void InitReader(bool b_ShowError)
    {
        if (b_ShowError)
        {
            SetLED(LED_RED);
            Utils::Print("Communication Error -> Reset PN532\r\n");
        }

        do // pseudo loop (just used for aborting with break;)
        {
            gb_InitSuccess = false;

            // Reset the PN532
            gi_PN532.begin(); // delay > 400 ms

            byte IC, VersionHi, VersionLo, Flags;
            if (!gi_PN532.GetFirmwareVersion(&IC, &VersionHi, &VersionLo, &Flags))
                break;

            char Buf[80];
            sprintf(Buf, "Chip: PN5%02X, Firmware version: %d.%d\r\n", IC, VersionHi, VersionLo);
            Utils::Print(Buf);
            sprintf(Buf, "Supports ISO 14443A:%s, ISO 14443B:%s, ISO 18092:%s\r\n", (Flags & 1) ? "Yes" : "No",
                    (Flags & 2) ? "Yes" : "No",
                    (Flags & 4) ? "Yes" : "No");
            Utils::Print(Buf);

            // Set the max number of retry attempts to read from a card.
            // This prevents us from waiting forever for a card, which is the default behaviour of the PN532.
            if (!gi_PN532.SetPassiveActivationRetries())
                break;

            // configure the PN532 to read RFID tags
            if (!gi_PN532.SamConfig())
                break;

            gb_InitSuccess = true;
        } while (false);

        if (b_ShowError)
        {
            Utils::DelayMilli(2000); // a long interval to make the LED flash very slowly
            SetLED(LED_OFF);
            Utils::DelayMilli(100);
        }
    }

    // If everything works correctly, the green LED will flash shortly (20 ms).
    // If the LED does not flash permanently this means that there is a severe error.
    // Additionally the LED will flash long (for 1 second) when the door is opened.
    // -----------------------------------------------------------------------------
    // If only the red LED is flashing this shows a communication error with the PN532 (flash very slow),
    // or someone not authorized trying to open the door (flash for 1 second)
    // or on power failure the red LED flashes shortly (battery voltage is below limit).
    // -----------------------------------------------------------------------------
    // The red LED flashing alternatingly with the green LED means that the battery is old and must be replaced soon.
    void FlashLED(eLED e_LED, int s32_Interval)
    {
        SetLED(e_LED);
        Utils::DelayMilli(s32_Interval);
        SetLED(LED_OFF);
    }

    void SetLED(eLED e_LED)
    {
        Utils::WritePin(LED_BUILTIN, LOW);

        switch (e_LED)
        {
        case LED_RED:
            Utils::WritePin(LED_BUILTIN, HIGH);
            break;
        case LED_GREEN:
            Utils::WritePin(LED_BUILTIN, HIGH);
            break;
        default: // Just to avoid stupid GCC compiler warning
            break;
        }
    }

    // Checks if the user has typed anything in the Terminal program and stores it in gs8_CommandBuffer
    // Execute the command when Enter has been hit.
    // returns true if any key has been pressed since the last call to this function.
    bool ReadKeyboardInput()
    {
        uint64_t u64_Now = Utils::GetMillis64() + PASSWORD_OFFSET_MS;

        bool b_KeyPress = false;
        while (SerialClass::Available())
        {
            b_KeyPress = true;
            // Check if the password must be entered
            bool b_PasswordValid = PASSWORD[0] == 0 || (u64_Now - gu64_LastPasswd) < (PASSWORD_TIMEOUT * 60 * 1000);

            byte u8_Char = SerialClass::Read();
            char s8_Echo[] = {(char)u8_Char, 0};

            if (u8_Char == '\r' || u8_Char == '\n')
            {
                OnCommandReceived(b_PasswordValid);
                Utils::Print("\r\n> ");
                continue;
            }

            if (u8_Char == 8) // backslash

            {
                if (gu32_CommandPos > 0)
                {
                    gu32_CommandPos--;
                    Utils::Print(s8_Echo); // Terminal Echo
                }
                continue;
            }

            // Ignore all other control characters and characters that the terminal will not print correctly (e.g. umlauts)
            if (u8_Char < 32 || u8_Char > 126)
                continue;

            // Terminal Echo
            if (b_PasswordValid)
                Utils::Print(s8_Echo);
            else
                Utils::Print("*"); // don't display the password chars in the Terminal

            if (gu32_CommandPos >= sizeof(gs8_CommandBuffer))
            {
                Utils::Print("ERROR: Command too long\r\n");
                gu32_CommandPos = 0;
            }

            gs8_CommandBuffer[gu32_CommandPos++] = u8_Char;
        }
        return b_KeyPress;
    }

    void OnCommandReceived(bool b_PasswordValid)
    {
        kUser k_User;
        char *s8_Parameter;

        gs8_CommandBuffer[gu32_CommandPos++] = 0;
        gu32_CommandPos = 0;
        Utils::Print(LF);

        if (!b_PasswordValid)
        {
            b_PasswordValid = strcmp(gs8_CommandBuffer, PASSWORD) == 0;
            if (!b_PasswordValid)
            {
                Utils::Print("Invalid password.\r\n");
                Utils::DelayMilli(500);
                return;
            }

            Utils::Print("Welcome to the access authorization terminal.\r\n");
            gs8_CommandBuffer[0] = 0; // clear buffer -> show menu
        }

        // As long as the user is logged in and types anything into the Terminal, the log-in time must be extended.
        gu64_LastPasswd = Utils::GetMillis64() + PASSWORD_OFFSET_MS;

        // This command must work even if gb_InitSuccess == false
        if (Utils::strnicmp(gs8_CommandBuffer, "DEBUG", 5) == 0)
        {
            if (!ParseParameter(gs8_CommandBuffer + 5, &s8_Parameter, 1, 1))
                return;

            if (s8_Parameter[0] < '0' || s8_Parameter[0] > '3')
            {
                Utils::Print("Invalid debug level.\r\n");
                return;
            }

            gi_PN532.SetDebugLevel(s8_Parameter[0] - '0');
            return;
        }

        // This command must work even if gb_InitSuccess == false
        if (Utils::stricmp(gs8_CommandBuffer, "RESET") == 0)
        {
            InitReader(false);
            if (gb_InitSuccess)
            {
                Utils::Print("PN532 initialized successfully\r\n"); // The chip has reponded (ACK) as expected
                return;
            }
        }

        // This command must work even if gb_InitSuccess == false
        if (PASSWORD[0] != 0 && Utils::stricmp(gs8_CommandBuffer, "EXIT") == 0)
        {
            gu64_LastPasswd = 0;
            Utils::Print("You have logged out.\r\n");
            return;
        }

        if (gb_InitSuccess)
        {
            if (Utils::stricmp(gs8_CommandBuffer, "CLEAR") == 0)
            {
                Clear();
                return;
            }

            if (Utils::stricmp(gs8_CommandBuffer, "LIST") == 0)
            {
                userManager.ListAllUsers();
                return;
            }

            if (Utils::stricmp(gs8_CommandBuffer, "RESTORE") == 0)
            {
                if (RestoreDesfireCard())
                    Utils::Print("Restore success\r\n");
                else
                    Utils::Print("Restore failed\r\n");
                gi_PN532.SwitchOffRfField();
                return;
            }

            if (Utils::stricmp(gs8_CommandBuffer, "MAKERANDOM") == 0)
            {
                if (MakeRandomCard())
                    Utils::Print("MakeRandom success\r\n");
                else
                    Utils::Print("MakeRandom failed\r\n");
                gi_PN532.SwitchOffRfField();
                return;
            }

            if (Utils::strnicmp(gs8_CommandBuffer, "ADD", 3) == 0)
            {
                if (!ParseParameter(gs8_CommandBuffer + 3, &s8_Parameter, 3, NAME_BUF_SIZE - 1))
                    return;

                AddCard(s8_Parameter);

                // Required! Otherwise the next ReadPassiveTargetId() does not detect the card and the door opens after adding a user.
                gi_PN532.SwitchOffRfField();
                return;
            }

            if (Utils::strnicmp(gs8_CommandBuffer, "DEL", 3) == 0)
            {
                if (!ParseParameter(gs8_CommandBuffer + 3, &s8_Parameter, 3, NAME_BUF_SIZE - 1))
                    return;

                if (!userManager.DeleteUser(0, s8_Parameter))
                    Utils::Print("Error: User not found.\r\n");

                return;
            }

            if (Utils::strnicmp(gs8_CommandBuffer, "DOOR12", 6) == 0) // FIRST !!!
            {
                if (!ParseParameter(gs8_CommandBuffer + 6, &s8_Parameter, 3, NAME_BUF_SIZE - 1))
                    return;

                if (!userManager.SetUserFlags(s8_Parameter, DOOR_BOTH))
                    Utils::Print("Error: User not found.\r\n");

                return;
            }
            if (Utils::strnicmp(gs8_CommandBuffer, "DOOR1", 5) == 0) // AFTER !!!
            {
                if (!ParseParameter(gs8_CommandBuffer + 5, &s8_Parameter, 3, NAME_BUF_SIZE - 1))
                    return;

                if (!userManager.SetUserFlags(s8_Parameter, DOOR_ONE))
                    Utils::Print("Error: User not found.\r\n");

                return;
            }
            if (Utils::strnicmp(gs8_CommandBuffer, "DOOR2", 5) == 0)
            {
                if (!ParseParameter(gs8_CommandBuffer + 5, &s8_Parameter, 3, NAME_BUF_SIZE - 1))
                    return;

                if (!userManager.SetUserFlags(s8_Parameter, DOOR_TWO))
                    Utils::Print("Error: User not found.\r\n");

                return;
            }

            if (strlen(gs8_CommandBuffer))
                Utils::Print("Invalid command.\r\n\r\n");
            // else: The user pressed only ENTER

            Utils::Print("Usage:\r\n");
            Utils::Print(" CLEAR          : Clear all users and their cards\r\n");
            Utils::Print(" ADD    {user}  : Add a user and his card\r\n");
            Utils::Print(" DEL    {user}  : Delete a user and his card\r\n");
            Utils::Print(" LIST           : List all users\r\n");
            Utils::Print(" DOOR1  {user}  : Open only door 1 for this user\r\n");
            Utils::Print(" DOOR2  {user}  : Open only door 2 for this user\r\n");
            Utils::Print(" DOOR12 {user}  : Open both doors for this user\r\n");

            Utils::Print(" RESTORE        : Removes the master key and the application from the card\r\n");
            Utils::Print(" MAKERANDOM     : Converts the card into a Random ID card (FOREVER!)\r\n");
        }
        else // !gb_InitSuccess
        {
            Utils::Print("FATAL ERROR: The PN532 did not respond. (Board initialization failed)\r\n");
            Utils::Print("Usage:\r\n");
        }

        // In case of a fatal error only these 2 commands are available:
        Utils::Print(" RESET          : Reset the PN532 and run the chip initialization anew\r\n");
        Utils::Print(" DEBUG {level}  : Set debug level (0= off, 1= normal, 2= RxTx data, 3= details)\r\n");

        if (PASSWORD[0] != 0)
            Utils::Print(" EXIT           : Log out\r\n");
        Utils::Print(LF);

#if USE_AES
        Utils::Print("Compiled for Desfire EV1 cards (AES - 128 bit encryption used)\r\n");
#else
        Utils::Print("Compiled for Desfire EV1 cards (3K3DES - 168 bit encryption used)\r\n");
#endif
#if ALLOW_ALSO_CLASSIC
        Utils::Print("Classic cards are also allowed.\r\n");
#endif

        Utils::Print("Terminal access is password protected: ");
        Utils::Print(PASSWORD[0] ? "Yes\r\n" : "No\r\n");

        Utils::Print("System is running since ");
        Utils::PrintInterval(Utils::GetMillis64(), LF);
    }

    // Parse the parameter behind "ADD", "DEL" and "DEBUG" commands and trim spaces
    bool ParseParameter(char *s8_Command, char **ps8_Parameter, int minLength, int maxLength)
    {
        int P = 0;
        if (s8_Command[P++] != ' ')
        {
            // The first char after the command must be a space
            Utils::Print("Invalid command\r\n");
            return false;
        }

        // Trim spaces at the begin
        while (s8_Command[P] == ' ')
        {
            P++;
        }

        char *s8_Param = s8_Command + P;
        int s32_Len = strlen(s8_Param);

        // Trim spaces at the end
        while (s32_Len > 0 && s8_Param[s32_Len - 1] == ' ')
        {
            s32_Len--;
            s8_Param[s32_Len] = 0;
        }

        if (s32_Len > maxLength)
        {
            Utils::Print("Parameter too long.\r\n");
            return false;
        }
        if (s32_Len < minLength)
        {
            Utils::Print("Parameter too short.\r\n");
            return false;
        }

        *ps8_Parameter = s8_Param;
        return true;
    }

    // ================================================================================

    // Stores a new user and his card
    void AddCard(const char *s8_UserName)
    {
        kUser k_User;
        kCard k_Card;
        if (!WaitForCard(&k_User, &k_Card))
            return;

        // First the entire memory of s8_Name is filled with random data.
        // Then the username + terminating zero is written over it.
        // The result is for example: s8_Name[NAME_BUF_SIZE] = { 'P', 'e', 't', 'e', 'r', 0, 0xDE, 0x45, 0x70, 0x5A, 0xF9, 0x11, 0xAB }
        // The string operations like stricmp() will only read up to the terminating zero,
        // but the application master key is derived from user name + random data.
        Utils::GenerateRandom((byte *)k_User.s8_Name, NAME_BUF_SIZE);
        strcpy(k_User.s8_Name, s8_UserName);

        // Utils::Print("User + Random data: ");
        // Utils::PrintHexBuf((byte*)k_User.s8_Name, NAME_BUF_SIZE, LF);

        kUser k_Found;
        if (userManager.FindUser(k_User.ID.u64, &k_Found))
        {
            Utils::Print("This card has already been stored for user ");
            Utils::Print(k_Found.s8_Name, LF);
            return;
        }

        if ((k_Card.e_CardType & CARD_Desfire) == 0) // Classic
        {
#if !ALLOW_ALSO_CLASSIC
            Utils::Print("The card is not a Desfire card.\r\n");
            return;
#endif
        }
        else // Desfire
        {
            if (!ChangePiccMasterKey())
                return;

            if (k_Card.e_CardType != CARD_DesRandom)
            {
                // The secret stored in a file on the card is not required when using a card with random ID
                // because obtaining the real card UID already requires the PICC master key. This is enough security.
                if (!StoreDesfireSecret(&k_User))
                {
                    Utils::Print("Could not personalize the card.\r\n");
                    return;
                }
            }
        }

        // By default a new user can open door one
        k_User.u8_Flags = DOOR_ONE;

        userManager.StoreNewUser(&k_User);
    }

    void Clear()
    {
        Utils::Print("\r\nATTENTION: ALL cards and users will be erased.\r\nIf you are really sure hit 'Y' otherwise hit 'N'.\r\n\r\n");

        if (!WaitForKeyYesNo())
            return;

        userManager.DeleteAllUsers();
        Utils::Print("All cards have been deleted.\r\n");
    }

    // Waits until the user either hits 'Y' or 'N'
    // Timeout = 30 seconds
    bool WaitForKeyYesNo()
    {
        uint64_t u64_Start = Utils::GetMillis64();
        while (true)
        {
            char c_Char = SerialClass::Read();
            if (c_Char == 'n' || c_Char == 'N' || (Utils::GetMillis64() - u64_Start) > 30000)
            {
                Utils::Print("Aborted.\r\n");
                return false;
            }

            if (c_Char == 'y' || c_Char == 'Y')
                return true;

            delay(200);
        }
    }

    // Waits for the user to approximate the card to the reader
    // Timeout = 30 seconds
    // Fills in pk_Card competely, but writes only the UID to pk_User.
    bool WaitForCard(kUser *pk_User, kCard *pk_Card)
    {
        Utils::Print("Please approximate the card to the reader now!\r\nYou have 30 seconds. Abort with ESC.\r\n");
        uint64_t u64_Start = Utils::GetMillis64();

        while (true)
        {
            if (ReadCard(pk_User->ID.u8, pk_Card) && pk_Card->u8_UidLength > 0)
            {
                // Avoid that later the door is opened for this card if the card is a long time in the RF field.
                gu64_LastID = pk_User->ID.u64;

                // All the stuff in this function takes about 2 seconds because the SPI bus speed has been throttled to 10 kHz.
                Utils::Print("Processing... (please do not remove the card)\r\n");
                return true;
            }

            if ((Utils::GetMillis64() - u64_Start) > 30000)
            {
                Utils::Print("Timeout waiting for card.\r\n");
                return false;
            }

            if (SerialClass::Read() == 27) // ESCAPE
            {
                Utils::Print("Aborted.\r\n");
                return false;
            }
        }
    }

    // Reads the card in the RF field.
    // In case of a Random ID card reads the real UID of the card (requires PICC authentication)
    // ATTENTION: If no card is present, this function returns true. This is not an error. (check that pk_Card->u8_UidLength > 0)
    // pk_Card->u8_KeyVersion is > 0 if a random ID card did a valid authentication with SECRET_PICC_MASTER_KEY
    // pk_Card->b_PN532_Error is set true if the error comes from the PN532.
    bool ReadCard(byte u8_UID[8], kCard *pk_Card)
    {
        memset(pk_Card, 0, sizeof(kCard));

        if (!gi_PN532.ReadPassiveTargetID(u8_UID, &pk_Card->u8_UidLength, &pk_Card->e_CardType))
        {
            pk_Card->b_PN532_Error = true;
            return false;
        }

        if (pk_Card->e_CardType == CARD_DesRandom) // The card is a Desfire card in random ID mode
        {
            if (!AuthenticatePICC(&pk_Card->u8_KeyVersion))
                return false;

            // replace the random ID with the real UID
            if (!gi_PN532.GetRealCardID(u8_UID))
                return false;

            pk_Card->u8_UidLength = 7; // random ID is only 4 bytes
        }
        return true;
    }

    // returns true if the cause of the last error was a Timeout.
    // This may happen for Desfire cards when the card is too far away from the reader.
    bool IsDesfireTimeout()
    {
        // For more details about this error see comment of GetLastPN532Error()
        if (gi_PN532.GetLastPN532Error() == 0x01) // Timeout
        {
            Utils::Print("A Timeout mostly means that the card is too far away from the reader.\r\n");

            // In this special case we make a short pause only because someone tries to open the door
            // -> don't let him wait unnecessarily.
            FlashLED(LED_RED, 200);
            return true;
        }
        return false;
    }

    // b_PiccAuth = true if random ID card with successful authentication with SECRET_PICC_MASTER_KEY
    void OpenDoor(uint64_t u64_ID, kCard *pk_Card, uint64_t u64_StartTick)
    {
        kUser k_User;
        if (!userManager.FindUser(u64_ID, &k_User))
        {
            Utils::Print("Unknown person tries to open the door: ");
            Utils::PrintHexBuf((byte *)&u64_ID, 7, LF);
            FlashLED(LED_RED, 1000);
            return;
        }

        if ((pk_Card->e_CardType & CARD_Desfire) == 0) // Classic
        {
#if !ALLOW_ALSO_CLASSIC
            Utils::Print("The card is not a Desfire card.\r\n");
            FlashLED(LED_RED, 1000);
            return;
#endif
        }
        else // Desfire
        {
            if (pk_Card->e_CardType == CARD_DesRandom) // random ID Desfire card
            {
                // In case of a random ID card the authentication has already been done in ReadCard().
                // But ReadCard() may also authenticate with the factory default DES key, so we must check here
                // that SECRET_PICC_MASTER_KEY has been used for authentication.
                if (pk_Card->u8_KeyVersion != CARD_KEY_VERSION)
                {
                    Utils::Print("The card is not personalized.\r\n");
                    FlashLED(LED_RED, 1000);
                    return;
                }
            }
            else // default Desfire card
            {
                if (!CheckDesfireSecret(&k_User))
                {
                    if (IsDesfireTimeout()) // Prints additional error message and blinks the red LED
                        return;

                    Utils::Print("The card is not personalized.\r\n");
                    FlashLED(LED_RED, 1000);
                    return;
                }
            }
        }

#if false
                // Check the speed of the entire communication process with the card (ReadPassiveTargetID + Crypto stuff):
                // In Classic         mode: 125 ms
                // In Desfire Random  mode: 676 ms
                // In Desfire Default mode: 799 ms
                // If you want to get this faster modify PN532_SOFT_SPI_DELAY but you must check the SPI signals on an oscilloscope!
                char s8_Buf[80];
                sprintf(s8_Buf, "Reading the card took %d ms.\r\n", (int)(Utils::GetMillis64() - u64_StartTick));
                Utils::Print(s8_Buf);
#endif

        switch (k_User.u8_Flags & DOOR_BOTH)
        {
        case DOOR_ONE:
            Utils::Print("Opening door 1 for ");
            break;
        case DOOR_TWO:
            Utils::Print("Opening door 2 for ");
            break;
        case DOOR_BOTH:
            Utils::Print("Opening door 1 + 2 for ");
            break;
        default:
            Utils::Print("No door specified for ");
            break;
        }
        Utils::Print(k_User.s8_Name);
        switch (pk_Card->e_CardType)
        {
        case CARD_DesRandom:
            Utils::Print(" (Desfire random card)", LF);
            break;
        case CARD_Desfire:
            Utils::Print(" (Desfire default card)", LF);
            break;
        default:
            Utils::Print(" (Classic card)", LF);
            break;
        }

        ActivateRelais(k_User.u8_Flags);

        // Avoid that the door is opened twice when the card is in the RF field for a longer time.
        gu64_LastID = u64_ID;
    }

    void ActivateRelais(byte u8_Flags)
    {
        int u8_Pin = D1;
        Utils::WritePin(u8_Pin, OPEN_INVERT ? LOW : HIGH); // Relais on

        Utils::DelayMilli(OPEN_INTERVAL);
        Utils::WritePin(u8_Pin, OPEN_INVERT ? HIGH : LOW); // Relais off
        //SetLED(LED_GREEN); // Green = an authorized person is opening the door

        //Utils::DelayMilli(1000); // let the green LED flash for at least one second
        //SetLED(LED_OFF);
    }
 
    // If the card is personalized -> authenticate with SECRET_PICC_MASTER_KEY,
    // otherwise authenticate with the factory default DES key.
    bool AuthenticatePICC(byte *pu8_KeyVersion)
    {
        if (!gi_PN532.SelectApplication(0x000000)) // PICC level
            return false;

        if (!gi_PN532.GetKeyVersion(0, pu8_KeyVersion)) // Get version of PICC master key
            return false;

        // The factory default key has version 0, while a personalized card has key version CARD_KEY_VERSION
        if (*pu8_KeyVersion == CARD_KEY_VERSION)
        {
            if (!gi_PN532.Authenticate(0, &gi_PiccMasterKey))
                return false;
        }
        else // The card is still in factory default state
        {
            if (!gi_PN532.Authenticate(0, &gi_PN532.DES2_DEFAULT_KEY))
                return false;
        }
        return true;
    }

    // Generate two dynamic secrets: the Application master key (AES 16 byte or DES 24 byte) and the 16 byte StoreValue.
    // Both are derived from the 7 byte card UID and the the user name + random data stored in EEPROM using two 24 byte 3K3DES keys.
    // This function takes only 6 milliseconds to do the cryptographic calculations.
    bool GenerateDesfireSecrets(kUser *pk_User, DESFireKey *pi_AppMasterKey, byte u8_StoreValue[16])
    {
        // The buffer is initialized to zero here
        byte u8_Data[24] = {0};

        // Copy the 7 byte card UID into the buffer
        memcpy(u8_Data, pk_User->ID.u8, 7);

        // XOR the user name and the random data that are stored in EEPROM over the buffer.
        // s8_Name[NAME_BUF_SIZE] contains for example { 'P', 'e', 't', 'e', 'r', 0, 0xDE, 0x45, 0x70, 0x5A, 0xF9, 0x11, 0xAB }
        int B = 0;
        for (int N = 0; N < NAME_BUF_SIZE; N++)
        {
            u8_Data[B++] ^= pk_User->s8_Name[N];
            if (B > 15)
                B = 0; // Fill the first 16 bytes of u8_Data, the rest remains zero.
        }

        byte u8_AppMasterKey[24];

        DES i_3KDes;
        if (!i_3KDes.SetKeyData(SECRET_APPLICATION_KEY, sizeof(SECRET_APPLICATION_KEY), 0) || // set a 24 byte key (168 bit)
            !i_3KDes.CryptDataCBC(CBC_SEND, KEY_ENCIPHER, u8_AppMasterKey, u8_Data, 24))
            return false;

        if (!i_3KDes.SetKeyData(SECRET_STORE_VALUE_KEY, sizeof(SECRET_STORE_VALUE_KEY), 0) || // set a 24 byte key (168 bit)
            !i_3KDes.CryptDataCBC(CBC_SEND, KEY_ENCIPHER, u8_StoreValue, u8_Data, 16))
            return false;

        // If the key is an AES key only the first 16 bytes will be used
        if (!pi_AppMasterKey->SetKeyData(u8_AppMasterKey, sizeof(u8_AppMasterKey), CARD_KEY_VERSION))
            return false;

        return true;
    }

    // Check that the data stored on the card is the same as the secret generated by GenerateDesfireSecrets()
    bool CheckDesfireSecret(kUser *pk_User)
    {
        DESFIRE_KEY_TYPE i_AppMasterKey;
        byte u8_StoreValue[16];
        if (!GenerateDesfireSecrets(pk_User, &i_AppMasterKey, u8_StoreValue))
            return false;

        if (!gi_PN532.SelectApplication(0x000000)) // PICC level
            return false;

        byte u8_Version;
        if (!gi_PN532.GetKeyVersion(0, &u8_Version))
            return false;

        // The factory default key has version 0, while a personalized card has key version CARD_KEY_VERSION
        if (u8_Version != CARD_KEY_VERSION)
            return false;

        if (!gi_PN532.SelectApplication(CARD_APPLICATION_ID))
            return false;

        if (!gi_PN532.Authenticate(0, &i_AppMasterKey))
            return false;

        // Read the 16 byte secret from the card
        byte u8_FileData[16];
        if (!gi_PN532.ReadFileData(CARD_FILE_ID, 0, 16, u8_FileData))
            return false;

        if (memcmp(u8_FileData, u8_StoreValue, 16) != 0)
            return false;

        return true;
    }

    // Store the SECRET_PICC_MASTER_KEY on the card
    bool ChangePiccMasterKey()
    {
        byte u8_KeyVersion;
        if (!AuthenticatePICC(&u8_KeyVersion))
            return false;

        if (u8_KeyVersion != CARD_KEY_VERSION) // empty card
        {
            // Store the secret PICC master key on the card.
            if (!gi_PN532.ChangeKey(0, &gi_PiccMasterKey, NULL))
                return false;

            // A key change always requires a new authentication
            if (!gi_PN532.Authenticate(0, &gi_PiccMasterKey))
                return false;
        }
        return true;
    }

    // Create the application SECRET_APPLICATION_ID,
    // store the dynamic Application master key in the application,
    // create a StandardDataFile SECRET_FILE_ID and store the dynamic 16 byte value into that file.
    // This function requires previous authentication with PICC master key.
    bool StoreDesfireSecret(kUser *pk_User)
    {
        if (CARD_APPLICATION_ID == 0x000000 || CARD_KEY_VERSION == 0)
            return false; // severe errors in Secrets.h -> abort

        DESFIRE_KEY_TYPE i_AppMasterKey;
        byte u8_StoreValue[16];
        if (!GenerateDesfireSecrets(pk_User, &i_AppMasterKey, u8_StoreValue))
            return false;

        // First delete the application (The current application master key may have changed after changing the user name for that card)
        if (!gi_PN532.DeleteApplicationIfExists(CARD_APPLICATION_ID))
            return false;

        // Create the new application with default settings (we must still have permission to change the application master key later)
        if (!gi_PN532.CreateApplication(CARD_APPLICATION_ID, KS_FACTORY_DEFAULT, 1, i_AppMasterKey.GetKeyType()))
            return false;

        // After this command all the following commands will apply to the application (rather than the PICC)
        if (!gi_PN532.SelectApplication(CARD_APPLICATION_ID))
            return false;

        // Authentication with the application's master key is required
        if (!gi_PN532.Authenticate(0, &DEFAULT_APP_KEY))
            return false;

        // Change the master key of the application
        if (!gi_PN532.ChangeKey(0, &i_AppMasterKey, NULL))
            return false;

        // A key change always requires a new authentication with the new key
        if (!gi_PN532.Authenticate(0, &i_AppMasterKey))
            return false;

        // After this command the application's master key and it's settings will be frozen. They cannot be changed anymore.
        // To read or enumerate any content (files) in the application the application master key will be required.
        // Even if someone knows the PICC master key, he will neither be able to read the data in this application nor to change the app master key.
        if (!gi_PN532.ChangeKeySettings(KS_CHANGE_KEY_FROZEN))
            return false;

        // --------------------------------------------

        // Create Standard Data File with 16 bytes length
        DESFireFilePermissions k_Permis;
        k_Permis.e_ReadAccess = AR_KEY0;
        k_Permis.e_WriteAccess = AR_KEY0;
        k_Permis.e_ReadAndWriteAccess = AR_KEY0;
        k_Permis.e_ChangeAccess = AR_KEY0;
        if (!gi_PN532.CreateStdDataFile(CARD_FILE_ID, &k_Permis, 16))
            return false;

        // Write the StoreValue into that file
        if (!gi_PN532.WriteFileData(CARD_FILE_ID, 0, 16, u8_StoreValue))
            return false;

        return true;
    }

    // If you have already written the master key to a card and want to use the card for another purpose
    // you can restore the master key with this function. Additionally the application SECRET_APPLICATION_ID is deleted.
    // If a user has been found in the storage for this card he will also be deleted.
    bool RestoreDesfireCard()
    {
        kUser k_User;
        kCard k_Card;
        if (!WaitForCard(&k_User, &k_Card))
            return false;

        userManager.DeleteUser(k_User.ID.u64, NULL);

        if ((k_Card.e_CardType & CARD_Desfire) == 0)
        {
            Utils::Print("The card is not a Desfire card.\r\n");
            return false;
        }

        byte u8_KeyVersion;
        if (!AuthenticatePICC(&u8_KeyVersion))
            return false;

        // If the key version is zero AuthenticatePICC() has already successfully authenticated with the factory default DES key
        if (u8_KeyVersion == 0)
            return true;

        // An error in DeleteApplication must not abort.
        // The key change below is more important and must always be executed.
        bool b_Success = gi_PN532.DeleteApplicationIfExists(CARD_APPLICATION_ID);
        if (!b_Success)
        {
            // After any error the card demands a new authentication
            if (!gi_PN532.Authenticate(0, &gi_PiccMasterKey))
                return false;
        }

        if (!gi_PN532.ChangeKey(0, &gi_PN532.DES2_DEFAULT_KEY, NULL))
            return false;

        // Check if the key change was successfull
        if (!gi_PN532.Authenticate(0, &gi_PN532.DES2_DEFAULT_KEY))
            return false;

        return b_Success;
    }

    bool MakeRandomCard()
    {
        Utils::Print("\r\nATTENTION: Configuring the card to send a random ID cannot be reversed.\r\nThe card will be a random ID card FOREVER!\r\nIf you are really sure what you are doing hit 'Y' otherwise hit 'N'.\r\n\r\n");
        if (!WaitForKeyYesNo())
            return false;

        kUser k_User;
        kCard k_Card;
        if (!WaitForCard(&k_User, &k_Card))
            return false;

        if ((k_Card.e_CardType & CARD_Desfire) == 0)
        {
            Utils::Print("The card is not a Desfire card.\r\n");
            return false;
        }

        byte u8_KeyVersion;
        if (!AuthenticatePICC(&u8_KeyVersion))
            return false;

        return gi_PN532.EnableRandomIDForever();
    }
};
