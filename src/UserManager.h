#ifndef USERMANAGER_H
#define USERMANAGER_H

#include "FS.h"
#include "EDB.h"
#include "debug.h"

#define DB_FILE "/users.db"
#define DB_TABLE_SIZE 8192
#define MAX_USERS 32
#define NAME_BUF_SIZE 64

enum eUserFlags
{
    NO_DOOR = 0,
    DOOR_ONE = 1,
    DOOR_TWO = 2,
    DOOR_BOTH = DOOR_ONE | DOOR_TWO,
};

// This structure is stored for each user
struct kUser
{
    // Constructor
    kUser()
    {
        memset(this, 0, sizeof(kUser));
    }

    // Card ID (4 or 7 bytes), binary
    union {
        uint64_t u64;
        byte u8[8];
    } ID;

    // User name (plain text + terminating zero character) + appended random data if name is shorter than NAME_BUF_SIZE
    char s8_Name[NAME_BUF_SIZE];

    // This byte stores eUserFlags (which door(s) to open for this user)
    byte u8_Flags;
};

// Database stuff
File dbFile;
void DBWriter(unsigned long address, const byte *data, unsigned int recsize)
{
    dbFile.seek(address, SeekSet);
    dbFile.write(data, recsize);
    dbFile.flush();
}

void DBReader(unsigned long address, byte *data, unsigned int recsize)
{
    dbFile.seek(address, SeekSet);
    dbFile.read(data, recsize);
}
EDB db(&DBWriter, &DBReader);

class UserManager
{
public:
    static void InitDatabase()
    {
        SPIFFS.begin();
        if (SPIFFS.exists(DB_FILE))
        {
            dbFile = SPIFFS.open(DB_FILE, "r+");
            if (dbFile)
            {
                DEBUG("Opening users database %s...", DB_FILE);
                EDB_Status result = db.open(0);
                if (result == EDB_OK)
                {
                    DEBUG("Done.");
                }
                else
                {
                    DEBUG("Error:");
                    DEBUG("Did not find a valid database in %s.", DB_FILE);
                    DEBUG("Creating new table... ");
                    db.create(0, DB_TABLE_SIZE, (unsigned int)sizeof(kUser));
                    DEBUG("Done.");
                    return;
                }
            }
            else
            {
                DEBUG("Could not open file %s.", DB_FILE);
            }
        }
        else
        {
            DEBUG("Creating table...");
            dbFile = SPIFFS.open(DB_FILE, "w+");
            db.create(0, DB_TABLE_SIZE, (unsigned int)sizeof(kUser));
            DEBUG("Done.");
        }
    }

    static void PrintDBError(EDB_Status err)
    {
        Serial.print("ERROR: ");
        switch (err)
        {
        case EDB_OUT_OF_RANGE:
            Serial.println("Recno out of range");
            break;
        case EDB_TABLE_FULL:
            Serial.println("Table full");
            break;
        case EDB_OK:
        default:
            Serial.println("OK");
            break;
        }
    }

    static void DeleteAllUsers()
    {
        db.clear();
    }

    static bool FindUser(uint64_t u64_ID, kUser *pk_User) {
        unsigned long recNo;
        return FindUser(u64_ID, pk_User, &recNo);
    }

    static bool FindUser(uint64_t u64_ID, kUser *pk_User, unsigned long *recno)
    {
        if (u64_ID == 0)
            return false;

        for ((*recno) = 1; (*recno) <= db.count(); (*recno)++)
        {
            DEBUG("Reading record with no %ld", *recno);
            EDB_Status result = db.readRec((*recno), EDB_REC (*pk_User));
            if (result == EDB_OK)
            {
                if (pk_User->ID.u64 == u64_ID)
                {
                    return true;
                }
            }
        }
        return false;
    }

    static bool FindUser(const char *name, kUser *pk_User, unsigned long *recno)
    {
        for ((*recno) = 1; (*recno) <= db.count(); (*recno)++)
        {
            DEBUG("Reading record with no %ld", *recno);
            EDB_Status result = db.readRec((*recno), EDB_REC (*pk_User));
            if (result == EDB_OK)
            {
                DEBUG("Result OK, comparing...");
                if (strcmp(pk_User->s8_Name, name))
                {
                    return true;
                }
            }
        }
        return false;
    }

    // Insert the user alphabetically sorted into the storage file
    static bool StoreNewUser(kUser *pk_NewUser)
    {
        DEBUG("Storing new user named %s..:", pk_NewUser->s8_Name);
        EDB_Status result = db.appendRec(EDB_REC (*pk_NewUser));
        if (result != EDB_OK)
        {
            PrintDBError(result);
            return false;
        }
        DEBUG("User has been stored.");
        Utils::Print("New user stored successfully:\r\n");
        PrintUser(pk_NewUser);
        return true;
    }

    // Deletes a user by ID
    static bool DeleteUser(uint64_t u64_ID)
    {
        DEBUG("Deleting user with UID %lld...", u64_ID);
        unsigned long recNo;
        kUser k_User;
        if (FindUser(u64_ID, &k_User, &recNo))
        {
            DEBUG ("User found at recno %ld.", recNo);
            db.deleteRec(recNo);
            DEBUG("User has been deleted.");
            return true;
        }
        return false;
    }

    // Deletes a user by Name
    static bool DeleteUser(const char *name)
    {
        DEBUG("Deleting user with name %s...", name);
        unsigned long recNo;
        kUser k_User;
        if (FindUser(name, &k_User, &recNo))
        {
            DEBUG ("User found at recno %ld.", recNo);
            db.deleteRec(recNo);
            DEBUG("User has been deleted.");
            return true;
        }
        return false;
    }

    // Modifies the flags of a user.
    // returns false if the user does not exist.
    static bool SetUserFlags(char *s8_Name, byte u8_NewFlags)
    {
        unsigned long recNo;
        kUser k_User;
        if (FindUser(s8_Name, &k_User, &recNo )) {
            k_User.u8_Flags = u8_NewFlags;
            db.updateRec(recNo,EDB_REC k_User);
            return true;
        }
        return false;
    }

    // Prints lines like
    // "Claudia             6D 2F 8A 44 00 00 00    (door 1)"
    // "Johnathan           10 FC D9 33 00 00 00    (door 1 + 2)"
    static void PrintUser(kUser *pk_User)
    {
        Utils::Print(pk_User->s8_Name);

        int s32_Spaces = NAME_BUF_SIZE - strlen(pk_User->s8_Name) + 2;
        for (int i = 0; i < s32_Spaces; i++)
        {
            Utils::Print(" ");
        }

        // The ID may be 4 or 7 bytes long
        Utils::PrintHexBuf(pk_User->ID.u8, 7);

        switch (pk_User->u8_Flags & DOOR_BOTH)
        {
        case DOOR_ONE:
            Utils::Print("   (door 1)\r\n");
            break;
        case DOOR_TWO:
            Utils::Print("   (door 2)\r\n");
            break;
        case DOOR_BOTH:
            Utils::Print("   (door 1 + 2)\r\n");
            break;
        default:
            Utils::Print("   (no door specified)\r\n");
            break;
        }
    }

    static void ListAllUsers()
    {
        Utils::Print("Users stored in database:\r\n");

        if (db.count() == 0)
        {
            Utils::Print("No users.\r\n");
            return;
        }

        kUser k_User;
        for (unsigned long recno = 1; recno <= db.count(); recno++)
        {
            EDB_Status result = db.readRec(recno, EDB_REC k_User);
            if (result == EDB_OK)
            {
                PrintUser(&k_User);
            }
        }
    }
};

#endif // USERMANAGER_H
