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
    int type;  // type of this property, first bit 0 indicates string, 1 indicates interger. Any value lower than 32 is of defined structure. 
	int len;  // the lenth of the storage of the property
    void *ptr; // pointer to the real storage of the property
};

// structure that holds the whole packet of a message in message queue
struct MsgBuf
{
   long rChn;  // Receiver type
   long sChn; // Sender Type
   long sec;    // timestamp sec
   long usec;   // timestamp usec
   int type; // type of this property, first bit 0 indicates string, 1 indicates interger. Any value greater than 32 is a command.
   int len;   //length of message payload in mText
   char mText[255];  // message payload
};

class ServiceUTils
{

public:
    ServiceUTils(key_t key, string myServiceTitle, long myServiceChannel); // Define specified message queue
	
    bool StartSevice(); // Open the message queue and specify the property of this module, the properties will be auto updated later
    long GetServiceChannel(string ServiceTitle); // Get the corresponding service type to ServiceTitle, return 0 for not found
    string GetServiceTitle(long ServiceChannel); // Get the corresponding service title to ServiceType, return "" for not found

    bool SubscribeService(string ServiceTitle); // Subscribe a service by its title
    bool QueryServiceData(string ServiceTitle); // Ask for service data from specified service provider
    bool SndMsg(string msg, string ServiceTitle); // Send a string to specified service provider
	bool SndMsg(void *p, int type, int len, string ServiceTitle); // Send a packet with given length to specified service provider
    bool BroadcastUpdate(void *p, int type, int len); // broadcast the data stored at *p with dataLength and send it to every subscriber

    string RcvMsg(); // receive a text message from sspecified ervice provider, like GPS, RADAR, TRIGGER. Not Autoreply.
    bool RcvMsg(void *p, int *type, int *len); // receive a packet from specified service provider. Autoreply all requests when enabled.
	
	bool FeedWatchDog(); // Feed the dog at watchdog server
	bool Log(string logContent, long logType); // Send a log to log server

    bool dbLink(string keyword, void *p, int type); // assign *s to store the string queried from the database
    bool dbQuery(string key); // query for the value from database by the key. The result will be placed in variable assigned to the key before.
    bool dbUpdate(string key); // update the value in database by the key with new value saved in variable assigned to the key before.

    ~ServiceUTils();

    int err; // 0 for no error
	long msgChn; // the service type of last receiving message 
	long msgTS_sec; // the time stamp in seconds of latest receiving message
	long msgTS_usec; // the micro seconds part of the time stamp of latest receiving message

protected:
    struct MsgBuf buf;
    int mId;
    long mChn; // my service channel

    int totalSubscriptions;  // the number of my subscriptions
    long mySubscriptions[255];
    int totalClients; // the number of clients who subscribe my service
    long myClients[255];

    char mData[255]; // store the latest service data that will be sent out per request
	int dataType; // store the type of service data
    int dataLength; // store the length of service data

    int totalServices;
    string serviceTitles[255];  // Services list got from server, 8 char + 1 stop + 1 index
	long serviceIndex[255];

    int totalProperties;
    long totalMessageSent;
    long totalMessageReceived;

    Property *pptr[255]; // Pointer to the Properties of theis module

    // return the index of the key in database
    int getIndex(string key);

private:
    string mTitle;
    key_t mKey;
};

#endif // SERVICEUTILS_H
