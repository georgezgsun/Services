#pragma once
#ifndef SERVICEUTILS_H
#define SERVICEUTILS_H


#include <string>

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
#define CMD_STRING 32
#define CMD_INTEGER 33

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

public:
	ServiceUtils(int argc, char *argv[]); // Define specified message queue

	bool StartService(); // Open the message queue and specify the property of this module, the properties will be auto updated later
	long GetServiceChannel(string ServiceTitle); // Get the corresponding service type to ServiceTitle, return 0 for not found
	string GetServiceTitle(long ServiceChannel); // Get the corresponding service title to ServiceType, return "" for not found

	bool ServiceSubscribe(string ServiceTitle); // Subscribe a service by its title
	bool ServiceQuery(string ServiceTitle); // Ask for service data from specified service provider
	bool SndCmd(string msg, string ServiceTitle); // Send a command in string to specified service provider
	bool SndMsg(void *p, size_t type, size_t len, long ServiceChannel); // Send a packet with given length to specified service provider with channel
	bool SndMsg(void *p, size_t type, size_t len, string ServiceTitle); // Send a packet with given length to specified service provider with title
	bool UpdateServiceData(); // update and broadcast the service data if any member changed

	size_t ChkNewMsg();  // receive a new message. return is the message type. 0 means no new message. There is a 1ms sleep after in case there is no message.
	string GetRcvMsg(); // receive a text message from sspecified ervice provider, like GPS, RADAR, TRIGGER. Not Autoreply.
	size_t GetRcvMsgBuf(char **p); // return the pointer of the buffer and its length. This buffer will change in next message operation.

	size_t WatchdogFeed(); // Feed the dog at watchdog main module
	bool Log(string logContent, long logType); // Send a log to log main module

	bool LocalMap(string keyword, void *p, char len); // assign *p with length len (0 for string) to be one of the local property, function in database actions and messages
	bool LocalMap(string keyword, string *s); // assign string *s to be one of the local property, function in database actions and messages
	bool LocalMap(string keyword, int *n); // assign *n to to be one of the local property, function in database actions and messages

	bool AddToServiceData(string keyword, void *p, char len); // assign *p with length len (0 for string) to be element
	bool AddToServiceData(string keyword, string *s); // assign string *s to be element of the service data
	bool AddToServiceData(string keyword, int *n); // assign int *n to be element of the service data

	bool dbQuery(); // query the database. The result will be placed in variables linked to the keyword before.
	bool dbUpdate(); // update the database with variables linked to the keyword before.

	~ServiceUtils();

	int m_err; // 0 for no error
	long m_MsgChn; // the service type of last receiving message
	long m_MsgTS_sec; // the time stamp in seconds of latest receiving message
	long m_MsgTS_usec; // the micro seconds part of the time stamp of latest receiving message

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

	size_t m_TotalDataBaseElements;  // store total elements get from database
	size_t m_IndexdbElements[255];  // store the index of database elements in Properties

	long m_WatchdogTimer[255];  // Store the watchdog timers

private:
	string m_Title;  // The title of this channel
	size_t m_HeaderLength;  // The header length of message
	//size_t m_IndexDB;  // The database
	key_t m_Key;  // The key of the message queue
};

#endif // SERVICEUTILS_H
