#ifndef SERVICEUTILS_H
#define SERVICEUTILS_H


#include <string>

#define CMD_ONBOARD 33
#define CMD_LIST 33
#define CMD_DATABASEINIT 34
#define CMD_DATABASEUPDATE 34
#define CMD_DATABASEQUERY 35
#define CMD_LOG 36
#define CMD_WATCHDOG 37
#define CMD_DOWN 37
#define CMD_STOP 38
#define CMD_SUBSCRIBE 39
#define CMD_QUERY 40
using namespace std;

// structure that defines properties, used to retrieve properties from database
struct Property
{
    string keyword;// keyword of this property, shall match with those in database
    size_t type;  // type of this property, first bit 0 indicates string, 1 indicates interger. Any value lower than 32 is of defined structure.
    size_t len;  // the lenth of the storage of the property
    void *ptr; // pointer to the real storage of the property
};

// structure that holds the whole packet of a message in message queue
struct MsgBuf
{
   long rChn;  // Receiver type
   long sChn; // Sender Type
   long sec;    // timestamp sec
   long usec;   // timestamp usec
   size_t type; // type of this property, first bit 0 indicates string, 1 indicates interger. Any value greater than 32 is a command.
   size_t len;   //length of message payload in mText
   char mText[255];  // message payload
};

class ServiceUtils
{

public:
    ServiceUtils(key_t key, string myServiceTitle, long myServiceChannel); // Define specified message queue
	
    bool StartService(); // Open the message queue and specify the property of this module, the properties will be auto updated later
    long GetServiceChannel(string ServiceTitle); // Get the corresponding service type to ServiceTitle, return 0 for not found
    string GetServiceTitle(long ServiceChannel); // Get the corresponding service title to ServiceType, return "" for not found

    bool SubscribeService(string ServiceTitle); // Subscribe a service by its title
    bool QueryServiceData(string ServiceTitle); // Ask for service data from specified service provider
    bool SndMsg(string msg, string ServiceTitle); // Send a string to specified service provider
    bool SndMsg(void *p, size_t type, size_t len, string ServiceTitle); // Send a packet with given length to specified service provider
    bool BroadcastUpdate(void *p, size_t type, size_t len); // broadcast the data stored at *p with dataLength and send it to every subscriber

    string RcvMsg(); // receive a text message from sspecified ervice provider, like GPS, RADAR, TRIGGER. Not Autoreply.
    bool RcvMsg(void *p, size_t *type, size_t *len); // receive a packet from specified service provider. Autoreply all requests when enabled.
	
	bool FeedWatchDog(); // Feed the dog at watchdog server
	bool Log(string logContent, long logType); // Send a log to log server

    bool dbLink(string keyword, void *p, size_t type); // assign *p to store the returned data with type structure type
    bool dbLink(string keyword, string *s); // assign *s to store the string queried from the database
    bool dbQuery(); // query the database. The result will be placed in variables linked to the keyword before.
    bool dbUpdate(); // update the database with variables linked to the keyword before.

    ~ServiceUtils();

    int err; // 0 for no error
	long msgChn; // the service type of last receiving message 
	long msgTS_sec; // the time stamp in seconds of latest receiving message
	long msgTS_usec; // the micro seconds part of the time stamp of latest receiving message

protected:
    struct MsgBuf buf;
    int mId;
    long mChn; // my service channel

    size_t totalSubscriptions;  // the number of my subscriptions
    long mySubscriptions[255];
    size_t totalClients; // the number of clients who subscribe my service
    long myClients[255];

    char mData[255]; // store the latest service data that will be sent out per request
    size_t dataType; // store the type of service data
    size_t dataLength; // store the length of service data

    size_t totalServices;
    string serviceTitles[255];  // Services list got from server, 8 char + 1 stop + 1 index
    long serviceChannels[255];

    size_t totalProperties;
    long totalMessageSent;
    long totalMessageReceived;

    Property *pptr[255]; // Pointer to the Properties of theis module

    // return the index of the key in database
    size_t getIndex(string key);

private:
    string mServiceTitle;
    size_t headerLength;
    size_t indexDB;
    key_t mKey;
};

#endif // SERVICEUTILS_H
