#pragma once

#include <string>

#define CMD_NULL 0
#define CMD_ONBOARD 1
#define CMD_LIST 2
#define CMD_DATABASEUPDATE 3
#define CMD_DATABASEQUERY 4
#define CMD_LOG 5
#define CMD_WATCHDOG 6
#define CMD_DOWN 7
#define CMD_SUBSCRIBE 8
#define CMD_UNSUBSCRIBE 9
#define CMD_QUERY 10
#define CMD_SERVICEDATA 11
#define CMD_COMMAND 12
#define CMD_STATUS 13
#define CMD_PUBLISHDATA 14
#define CMD_STRING 32
#define CMD_INTEGER 33

#define CTL_BLOCKING 16
#define CTL_CONTINUALDATARECEIVE 8
#define CTL_AUTOPUBLISH 4
#define CTL_AUTOWATCHDOG 2
#define CTL_AUTOSLEEP 1

using namespace std;

// structure that defines properties, used to retrieve properties from database
struct Property
{
	string keyword;// keyword of this property, shall match with those in database
	char len;  // length of this property, 0 indicates string. 
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

protected:
	struct MsgBuf m_buf;
	int m_ID;
	long m_Chn; // my service channel

	size_t m_TotalSubscriptions;  // the number of my subscriptions
	long m_Subscriptions[255];
	size_t m_TotalClients; // the number of clients who subscribe my service
	long m_Clients[255];

	char m_ServiceData[255]; // store the latest service data that will be sent out per request
	size_t m_ServiceDataLength; // store the length of service data
	size_t m_TotalServiceDataElements;  // store total elements in service data
	Property m_ServiceDataElements[255];  // store the Property of elements in service data 

	size_t m_TotalServices;
	string m_ServiceTitles[255];  // Service titles list got from main module
	long m_ServiceChannels[255];  // Services Channels list got from main module

	size_t m_TotalProperties; 
	long m_TotalMessageSent;
	long m_TotalMessageReceived;
	Property *m_pptr[255]; // Pointer to the Properties of theis module

	size_t m_TotalDatabaseElements;  // store total elements get from database
	size_t m_IndexdbElements[255];  // store the index of database elements in Properties
	long m_WatchdogTimer[255];  // Store the watchdog timers

	size_t m_HeaderLength;  // The header length of message
	int m_Severity; // The level of the log;  1-Critical, 2-Error, 3-Warning, 4-Information, 5-Debug, 6-Verbose;

	bool ReSendMsgTo(long ServiceChannel);
	bool ReportStatus();

private:
	string m_Title;  // The title of this channel
	key_t m_Key;  // The key of the message queue

public:

	int m_err; // 0 for no error
	long m_MsgChn; // the service type of last receiving message
	long m_MsgTS_sec; // the time stamp in seconds of latest receiving message
	long m_MsgTS_usec; // the micro seconds part of the time stamp of latest receiving message

	ServiceUtils();
	ServiceUtils(int argc, char *argv[]); // Define specified message queue
	~ServiceUtils();

	bool StartService(); // Open the message queue and specify the property of this module, the properties will be auto updated later
	long GetServiceChannel(string ServiceTitle); // Get the corresponding service type to ServiceTitle, return 0 for not found
	string GetServiceTitle(long ServiceChannel); // Get the corresponding service title to ServiceType, return "" for not found

	bool SubscribeService(string ServiceTitle); // Subscribe a service by its title
	bool QueryService(string ServiceTitle); // Ask for service data from specified service provider
	bool SndCmd(string msg, string ServiceTitle); // Send a command in string to specified service provider
	bool SndMsg(void *p, size_t type, size_t len, long ServiceChannel); // Send a packet with given length to specified service provider with channel
	bool SndMsg(void *p, size_t type, size_t len, string ServiceTitle); // Send a packet with given length to specified service provider with title
	bool PublishServiceData(); // update and broadcast the service data if any data are changed

	size_t ChkNewMsg(int Control = 7);  // receive a new message. return is the message type. 0 means no new message. There is a 1ms sleep after in case there is no message.
	string GetRcvMsg(); // receive a text message from specified service provider, like GPS, RADAR, TRIGGER.
	size_t GetRcvMsgBuf(char **p); // return the pointer of the buffer and its length. This buffer will change in next message operation.

	bool WatchdogFeed(); // smart feed the watchdog for this module in main module
	bool Log(string logContent, int ErrorCode = 1000); // Send a log to main module which will save this log into database. It is controlled by log level

	bool LocalMap(string keyword, void *p, char len); // map local *p with length len (0 for string) to be one of the local property, function in database actions and messages
	bool LocalMap(string keyword, string *s); // assign string *s to be one of the local property, function in database actions and messages
	bool LocalMap(string keyword, int *n); // assign *n to to be one of the local property, function in database actions and messages

	bool AddToServiceData(string keyword, void *p, char len); // assign *p with length len (0 for string) to be element
	bool AddToServiceData(string keyword, string *s); // assign string *s to be element of the service data
	bool AddToServiceData(string keyword, int *n); // assign int *n to be element of the service data

	 // send a command to the main module to ask for configures from the database. 
	// The results will be auto parsed in ChkNewMsg() where configures are stored in variables mapped before or later.
	bool QueryConfigures();
	bool UpdateConfigures(); // send the configures back to main module and update the corresponding tables in the database.
};
