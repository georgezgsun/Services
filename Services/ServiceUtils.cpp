#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/time.h>

#include "ServiceUtils.h"

#define PERMS 0644

ServiceUtils::ServiceUtils()
{
	m_ID = -1;
	m_err = 0;
	// Get the key for the message queue from pid for main module, ppid for clients
	m_HeaderLength = sizeof(m_buf.sChn) + sizeof(m_buf.sec) + sizeof(m_buf.usec) + sizeof(m_buf.type) + sizeof(m_buf.len);

	m_Chn = 1;
	m_Key = getpid();
	//m_Key = 12345; // for debug purpose 
	m_Title.assign("MAIN");

	m_ID = msgget(m_Key, PERMS | IPC_CREAT);
	printf("(Debug) MsgQue key: %d ID: %d\n", m_Key, m_ID);
	m_err = m_ID == -1 ? -3 : 0;

	m_TotalClients = 0;
	m_TotalDatabaseElements = 0;
	m_TotalMessageReceived = 0;
	m_TotalMessageSent = 0;
	m_TotalProperties = 0;
	m_TotalServiceDataElements = 0;
	m_TotalServices = 0;
	m_TotalSubscriptions = 0;
	m_Severity = 2000;  // Information
	m_AutoPublish = true;  // Enable service data auto update by default
	m_AutoWatchdog = true;  // Enable watchdog auto feed by default 
	m_AutoSleep = true; // Enable auto sleep by default

	memset(m_Clients, 0, sizeof(m_Clients));
	memset(m_ServiceChannels, 0, sizeof(m_ServiceChannels));
	memset(m_ServiceData, 0, sizeof(m_ServiceData));
	memset(m_ServiceDataElements, 0, sizeof(m_ServiceDataElements));
	memset(m_ServiceTitles, 0, sizeof(m_ServiceTitles));
	memset(m_Subscriptions, 0, sizeof(m_Subscriptions));
	memset(m_IndexdbElements, 0, sizeof(m_IndexdbElements));
	memset(m_WatchdogTimer, 0, sizeof(m_WatchdogTimer));
}

// Constructor, where first argument is for the service channel, second is for the service title. Example 3 GPS
ServiceUtils::ServiceUtils(int argc, char *argv[])
{
	m_ID = -1;
	m_err = 0;
	// Get the key for the message queue from pid for main module, ppid for clients
	m_HeaderLength = sizeof(m_buf.sChn) + sizeof(m_buf.sec) + sizeof(m_buf.usec) + sizeof(m_buf.type) + sizeof(m_buf.len);

	if (argc <= 1)
	{
		m_Chn = 1;
		m_Key = getpid();
		m_Title.assign("MAIN");
	}
	else
	{
		m_Chn = atoi(argv[1]);
		m_Key = getppid();
		m_Title.clear();
		m_err = m_Chn <= 0 ? -1 : 0;
	}

	if (argc > 2)
		m_Title.assign(argv[2]);

	//m_Key = 12345;  // This is for debug purpose
	if (argc > 3)
		m_Key = atoi(argv[3]);
	if (m_Key < 0)
	{
		printf("(Debug) Error. MsgQue Key = %d\n", m_Key);
		m_err = -2;
	}

	m_ID = msgget(m_Key, PERMS | IPC_CREAT);
	printf("(Debug) MsgQue key: %d ID: %d\n", m_Key, m_ID);
	m_err = m_ID == -1 ? -3 : 0;

	m_TotalClients = 0;
	m_TotalDatabaseElements = 0;
	m_TotalMessageReceived = 0;
	m_TotalMessageSent = 0;
	m_TotalProperties = 0;
	m_TotalServiceDataElements = 0;
	m_TotalServices = 0;
	m_TotalSubscriptions = 0;
	m_Severity = 2000;  // The default severity level Info corresponds to error code < 2000
	m_AutoPublish = true;  // Enable service data auto update by default
	m_AutoWatchdog = true;  // Enable watchdog auto feed by default 
	m_AutoSleep = true; // Enable auto sleep by default

	memset(m_Clients, 0, sizeof(m_Clients));
	memset(m_ServiceChannels, 0, sizeof(m_ServiceChannels));
	memset(m_ServiceData, 0, sizeof(m_ServiceData));
	memset(m_ServiceDataElements, 0, sizeof(m_ServiceDataElements));
	memset(m_ServiceTitles, 0, sizeof(m_ServiceTitles));
	memset(m_Subscriptions, 0, sizeof(m_Subscriptions));
	memset(m_IndexdbElements, 0, sizeof(m_IndexdbElements));
	memset(m_WatchdogTimer, 0, sizeof(m_WatchdogTimer));
};

// Start the service and do the initialization
bool ServiceUtils::StartService()
{
	int count = 0;
	if (m_ID < 0)
	{
		m_err = -3;
		return false;
	}

	// The initialization of the main module and other service providers are different
	if (m_Chn == 1)
		return true;

	char txt[255];
	pid_t pid = getpid();
	pid_t ppid = getppid();

	// Read off all messages to me in the queue before I startup
	while (msgrcv(m_ID, &m_buf, sizeof(m_buf), m_Chn, IPC_NOWAIT) > 0)
	{
		count++;
		m_TotalMessageReceived++;
	}

	// Report onboard to the main module
	strcpy(txt, m_Title.c_str());
	size_t len = m_Title.length() + 1;
	memcpy(txt + len, &pid, sizeof(pid));
	len += sizeof(pid);
	memcpy(txt + len, &ppid, sizeof(ppid));
	len += sizeof(ppid);
	SndMsg(txt, CMD_ONBOARD, len, 1);

	if (count)
		Log(m_Title + " reads " + to_string(count) + " staled messages at startup.", 4);
	fprintf(stderr, "%s starts with pid=%ld, ppid=%ld.\n", m_Title.c_str(), pid, ppid);

	// Get initialization from the main module;
	m_AutoPublish = false;  // disable service data auto update during startup
	m_AutoWatchdog = false; // disable watchdog auto feed during startup
	m_AutoSleep = false;  // disable auto sleep during startup
	struct timespec tim = { 0, 1000000L }; // 1ms = 1000000ns

	count = 0;
	do
	{
		if (!ChkNewMsg())
			clock_nanosleep(CLOCK_REALTIME, 0, &tim, NULL);

		if (count++ >= 100)
		{
			Log("Cannot get the initialization messages from the main module in " 
				+ to_string(count) + "ms. Error: " + to_string(m_err), 1);
			m_err = -10;
			return false;
		}
	} while (m_TotalServices <= 0);
	Log(m_Title + " gets initialized in " + to_string(count) + "ms", 4);
	m_AutoSleep = true;  // enable service data auto update after startup
	m_AutoWatchdog = true; // enable watchdog auto feed after startup
	m_AutoPublish = false;  // enable service data auto feed after startup
		
	// Broadcast my onboard messages to all service channels other than the main module and myself
	for (size_t i = 1; i < m_TotalServices; i++)
		SndMsg(txt, CMD_ONBOARD, len, m_ServiceChannels[i]);

	return true;
}

ServiceUtils::~ServiceUtils()
{
	for (size_t i = 0; i < m_TotalProperties; i++)
		delete m_pptr[i];

	// if the main module closed, delete the message queue
	if (1 == m_Chn)
		msgctl(m_ID, IPC_RMID, nullptr);
};

// Send a command in string to specified service provider.
bool ServiceUtils::SndCmd(string msg, string ServiceTitle)
{
	// clear the buffer before any modifications
	memset(&m_buf, 0, sizeof(m_buf));

	if (m_ID == -1)
	{
		m_err = -1;  // Message queue has not been opened
		return false;
	}

	m_buf.rChn = GetServiceChannel(ServiceTitle);
	if (m_buf.rChn <= 0)
	{
		m_err = -2; // Cannot find the ServiceTitle in the list
		return false;
	}

	struct timeval tv;
	gettimeofday(&tv, nullptr);
	m_buf.sChn = m_Chn;
	m_buf.sec = tv.tv_sec;
	m_buf.usec = tv.tv_usec;
	m_buf.len = msg.length() + 1;
	m_buf.type = CMD_COMMAND;

	strcpy(m_buf.mText, msg.c_str());
	if (msgsnd(m_ID, &m_buf, m_buf.len + m_HeaderLength, IPC_NOWAIT))
	{
		m_err = errno;
		return false;
	}
	m_err = 0;
	m_TotalMessageSent++;
	if (m_buf.rChn == 1)
		m_WatchdogTimer[m_buf.rChn] = m_buf.sec;  // set timer for next watchdog feed
	return true;
};

// Send a packet with given length to specified service provider
bool ServiceUtils::SndMsg(void *p, size_t type, size_t len, string ServiceTitle)
{
	long Chn = GetServiceChannel(ServiceTitle);
	if (Chn)
		return SndMsg(p, type, len, Chn);

	Log(m_Title + " cannot find " + ServiceTitle + " in the service list while SndMsg().", 4);
	m_err = -2;
	return false;
}

// Send a packet with given length to specified service provider
bool ServiceUtils::SndMsg(void *p, size_t type, size_t len, long ServiceChannel)
{
	// clear the buffer before any modifications
	memset(&m_buf, 0, sizeof(m_buf));

	if (m_ID == -1)
	{
		printf("(Debug) Error. Message queue has not been created.");
		m_err = -1;  // Message queue has not been opened
		return false;
	}

	if (len > 255)
	{
		Log("Error. Length of the message to be sent is limited to 255.", 2);
		m_err = -2;
		return false;
	}

	m_buf.rChn = ServiceChannel;
	if (m_buf.rChn <= 0)
	{
		Log(m_Title + " got an invalid service channel " + to_string(ServiceChannel) + " while SndMsg.", 2);
		m_err = -2; // Cannot find the ServiceTitle in the list
		return false;
	}

	struct timeval tv;
	gettimeofday(&tv, nullptr);
	m_buf.sChn = m_Chn;
	m_buf.sec = tv.tv_sec;
	m_buf.usec = tv.tv_usec;
	m_buf.len = len;  // len is of 0-255, so do the cast here.
	m_buf.type = type;

	memcpy(m_buf.mText, p, m_buf.len);
	if (msgsnd(m_ID, &m_buf, m_buf.len + m_HeaderLength, IPC_NOWAIT))
	{
		printf("(Debug) Critical error. Unable to send the message to channel %d. Message is of length %ld, and header length %ld.\n", m_ID, len, m_HeaderLength);
		m_err = errno;
		return false;
	}

	m_err = 0;
	m_TotalMessageSent++;
	if (ServiceChannel == 1)
		m_WatchdogTimer[m_buf.rChn] = m_buf.sec;  // set timer for next watchdog feed
	return true;
};

// Re-send the message to another service channel, using original settings and buffer
bool ServiceUtils::ReSendMsgTo(long ServiceChannel)
{
	m_buf.rChn = ServiceChannel;
	if (m_buf.rChn <= 0)
	{
		Log(m_Title + " gets an invalid service channel number " + to_string(ServiceChannel) + " in ReSendMsgTo().", 4);
		m_err = -2; // Cannot find the ServiceTitle in the list
		return false;
	}

	if (msgsnd(m_ID, &m_buf, m_buf.len + m_HeaderLength, IPC_NOWAIT))
	{
		printf("(Debug) Error. Unable to send the message to channel %d. Message is of length %ld, and header length %ld.\n", m_ID, m_buf.len, m_HeaderLength);
		m_err = errno;
		return false;
	}

	m_err = 0;
	m_TotalMessageSent++;
	if (ServiceChannel == 1)
		m_WatchdogTimer[m_buf.rChn] = m_buf.sec;  // set timer for next watchdog feed
	return true;
}

//Subscribe the service from the service provider with specified title
bool ServiceUtils::SubscribeService(string ServiceTitle)
{
	long ServiceChannel = GetServiceChannel(ServiceTitle);
	if (ServiceChannel < 1)
	{
		m_err = -1;
		return false;
	}

	if (m_Chn == 1)
		return false; // No service subscribe for the main module

	if (!SndMsg(nullptr, CMD_SUBSCRIBE, 0, ServiceChannel))
	{
		m_err = -2;
		return false;
	}

	m_err = 0;
	// check if the service has been subscribed before
	for (size_t i = 0; i < m_TotalClients; i++)
	{
		if (m_Clients[i] == ServiceChannel)
			return true;
	}

	// add the new subscription to my list
	m_Subscriptions[m_TotalSubscriptions++] = ServiceChannel;
	return true;
};

// query the service data from the service provider with specified title
bool ServiceUtils::QueryService(string ServiceTitle)
{
	if (m_ID == -1)
	{
		m_err = -1;  // Message queue has not been opened
		return false;
	}

	if (m_Chn == 1)
		return false; // No service query for the main module

	long Chn = GetServiceChannel(ServiceTitle);
	if (!Chn)
	{
		Log(m_Title + " cannot find " + ServiceTitle + " in the service list when querying service.", 4);
		m_err = -2;
		return false;
	}

	return SndMsg(nullptr, CMD_QUERY, 0, Chn);
}

// broadcast the service data to all clients
bool ServiceUtils::PublishServiceData()
{
	if (m_ID == -1)
	{
		m_err = -1;  // Message queue has not been opened
		return false;
	}

	if (m_Chn == 1)
		return false; // No service data update for the main module

	// broadcast the updated service data
	struct timeval tv;
	gettimeofday(&tv, nullptr);

	m_buf.sChn = m_Chn;
	m_buf.sec = tv.tv_sec;
	m_buf.usec = tv.tv_usec;
	m_buf.type = CMD_SERVICEDATA;
	m_buf.len = m_ServiceDataLength;

	size_t offset = 0;
	string keyword;
	memset(m_buf.mText, 0, sizeof(m_buf.mText));  // fill 0 before reading, make sure no garbage left over
	for (size_t i = 0; i < m_TotalServiceDataElements; i++)
	{
		strcpy(m_buf.mText + offset, m_ServiceDataElements[i].keyword.c_str());
		offset += m_ServiceDataElements[i].keyword.length() + 1;

		m_buf.mText[offset++] = m_ServiceDataElements[i].len;

		if (m_ServiceDataElements[i].len)
		{
			memcpy(m_buf.mText + offset, m_ServiceDataElements[i].ptr, m_ServiceDataElements[i].len);
			offset += m_ServiceDataElements[i].len;
		}
		else
		{
			strcpy(m_buf.mText + offset, static_cast<string *>(m_ServiceDataElements[i].ptr)->c_str());
			offset += static_cast<string *>(m_ServiceDataElements[i].ptr)->length() + 1;
		}		
	}
	m_ServiceDataLength = offset;

	// Check if there is any update
	if (!memcmp(m_buf.mText, m_ServiceData, m_ServiceDataLength))
		return false; // return when there is nothing changed

	// There is a changing. Copy the updated service data to m_ServiceData including those /0 at the tail
	memcpy(m_ServiceData, m_buf.mText, sizeof(m_ServiceData));

	// The service provider will broadcast the service data to all its clients
	for (size_t i = 0; i < m_TotalClients; i++)
		if (!ReSendMsgTo(m_Clients[i]))
			return false;

	Log("The service date get auto updated and broadcasted.", 5);
	m_err = 0;
	return true;
}; // Multicast the data stored at *p with m_ServiceDataLength and send it to every subscriber

// receive a message from sspecified ervice provider, like GPS, RADAR, TRIGGER. No AutoReply
string ServiceUtils::GetRcvMsg()
{
	return m_buf.mText;
}

// get the pointer of the received buffer *p and its length. This buffer will change in next message operation.
size_t ServiceUtils::GetRcvMsgBuf(char **p)
{
	*p = m_buf.mText;
	return m_buf.len;
};

// receive a packet from specified service provider. 
// Auto map and autoreply to default requests.
size_t ServiceUtils::ChkNewMsg()
{
	size_t offset = 0;
	size_t type;
	struct timespec tim = { 0, 1000000L }; // 1ms = 1000000ns

	do
	{
		memset(m_buf.mText, 0, sizeof(m_buf.mText));  // fill 0 before reading, make sure no garbage left over
		long l = msgrcv(m_ID, &m_buf, sizeof(m_buf), m_Chn, IPC_NOWAIT);
		l -= m_HeaderLength;
		m_err = static_cast<int>(l);
		if (l < 0)
		{
			// Auto update the service data if enabled
			if (m_AutoPublish)
				PublishServiceData();

			// Auto feed the watchdog if enabled.
			if (m_AutoWatchdog)
				WatchdogFeed();

			// Auto sleep for a short period of time (1ms) to reduce the CPU usage if enabled.
			if (m_AutoSleep)
				clock_nanosleep(CLOCK_REALTIME, 0, &tim, NULL);

			return 0;
		}
		m_TotalMessageReceived++;

		m_err = 0;
		m_MsgChn = m_buf.sChn; // the service channel where the message comes
		m_MsgTS_sec = m_buf.sec; // the time stamp in seconds of latest receiving message
		m_MsgTS_usec = m_buf.usec;
		type = m_buf.type;
		string keyword;
		string msg;

		// no autoreply for main module
		if (m_Chn == 1)
			return type;

		// no auto reply for those normal receiving. type < 31 commands; 32 is string. 33 is integer. anything larger are user defined types
		if (type >= 32)
			return type;

		switch (type)
		{
		// auto reply for new subscription request
		case CMD_SUBSCRIBE:
			size_t i;
			for (i = 0; i < m_TotalClients; i++)
				if (m_Clients[i] == m_MsgChn)
				{
					Log("The client " + to_string(m_MsgChn) + " is already subscribed.");
					break; // break for loop
				}
			if (i < m_TotalClients)
				break; // break switch

			// make sure no over flow of subscription
			if (m_TotalClients >= 255)
			{
				m_err = -14;
				Log("Error. Too many clients added.", 1);
				return m_buf.type;
			}

			m_Clients[m_TotalClients++] = m_MsgChn; // increase m_TotalClients by 1
			Log("Got new service subscription from " + to_string(m_MsgChn) + ". Now I has " + to_string(m_TotalClients) + " clients.", 5);
			break;
			
		case CMD_UNSUBSCRIBE:
			// delete the corresponding subscriber from the list. This may be useless
			for (size_t i = 0; i < m_TotalClients; i++)
				if (m_Clients[i] == m_MsgChn)
				{  
					// move later subscriber one step up
					for (size_t j = i; j < m_TotalClients - 1; j++)
						m_Clients[j] = m_Clients[j + 1];
					m_TotalClients--;  // decrease m_TotalClients by 1
				}

			Log("Service subscription from " + to_string(m_MsgChn) + " is canceled. Now has " + to_string(m_TotalClients) + " clients.", 5);
			break; // continue to read next messages

		// auto reply the query of service data query
		// The service data are stored at m_ServiceData with length of m_ServiceDataLength.
		case CMD_QUERY:
			SndMsg(m_ServiceData, CMD_SERVICEDATA, m_ServiceDataLength, m_MsgChn);
			break; // continue to read next message

		// auto process the services list reply from the main module
		// [channel_1][title_1][channel_2][title_2] ... [channel_n][title_n] ; titles are end with /0. channel is of size 1 byte
		case CMD_LIST:
			offset = 0;

			do
			{
				m_ServiceChannels[m_TotalServices] = m_buf.mText[offset++];
				m_ServiceTitles[m_TotalServices].assign(m_buf.mText + offset);
				offset += m_ServiceTitles[m_TotalServices++].length() + 1; // increase the m_TotalServices, update the offset to next
			} while (offset < m_buf.len);
			Log(m_Title + " received new services list from the main module.  There are " 
				+ to_string(m_TotalServices) + " services in the list.");
			break; // continue to read next message

		// auto process the database feedback from the main module
		// [keyword_1][type_1][len_1][data_1][keyword_2][type_2][len_2][data_2] ... [keyword_n][type_n][len_n][data_n] ; 
		// end with keyword empty, type and length are of size 1 byte, keyword end with /0
		case CMD_DATABASEQUERY:
			offset = 0;
			do
			{
				keyword.assign(m_buf.mText + offset); // read the keyword
				offset += keyword.length() + 1; // there is a /0 in the end
				if (keyword.empty()) // end of assignment when keyword is empty
					break;

				// read data length, 0 represents for string
				char n = m_buf.mText[offset++]; 

				size_t i;
				for (i = 0; i < m_TotalProperties; i++)
				{
					// check if matched before
					if (keyword.compare(m_pptr[i]->keyword) == 0)
					{
						// check the type matched or not
						if (n != m_pptr[i]->len)
						{
							m_err = -11;
							Log("Fatal error! The data length/type of " + keyword + " from main module is " \
								+ to_string(n) + ", locals is of " \
								+ to_string(m_pptr[i]->len) + ". They are not indentical.", 1);
							return m_buf.type;
						}

						break;  // break for loop
					}
				}

				// It is a new property if the keyword does not match any previous keywords
				if (i >= m_TotalProperties)
				{
					// check if there are too many properties
					if (m_TotalProperties >= 255)
					{
						Log("Error. Too many properties are added.", 1);
						return m_buf.type;
					}
					m_pptr[m_TotalProperties] = new Property;
					m_pptr[m_TotalProperties]->ptr = nullptr;
					m_pptr[m_TotalProperties]->len = n;
					m_pptr[m_TotalProperties]->keyword = keyword;
					m_TotalProperties++;
				}

				if (m_pptr[i]->len) // type 0 means a string which shall be assigned the value in a different way
				{
					if (!m_pptr[i]->ptr)  // make sure the pointer is not NULL before assign any value to it
						m_pptr[i]->ptr = new char[n];
					memcpy(m_pptr[i]->ptr, m_buf.mText + offset, n);
					offset += n;
				}
				else
				{
					if (!m_pptr[i]->ptr)
						m_pptr[i]->ptr = new string;
					static_cast<string *>(m_pptr[i]->ptr)->assign(m_buf.mText + offset);
					offset += static_cast<string *>(m_pptr[i]->ptr)->length() + 1;
				}

				size_t j;
				for (j = 0; j <m_TotalDatabaseElements; j++)
					if (i == m_IndexdbElements[j])
						break;
 
				// Add the element to the database element list if it does not appear before
				if (j >= m_TotalDatabaseElements)
					m_IndexdbElements[m_TotalDatabaseElements++] = i;
			} while (offset < 255);
			//break; // continue to read next message
			return type;  // return, no more auto parse; for list transfer

		// auto process the message that a service is down.
		case CMD_DOWN:
			// Return if it is sent to myself
			if (m_MsgChn == m_Chn)
				return type;  // return, no more auto parse

			// stop broadcasting service data to that client
			for (size_t i = 0; i < m_TotalClients; i++)
				if (m_Clients[i] == m_MsgChn)
				{
					// move later subscriber one step up
					for (size_t j = i; j < m_TotalClients - 1; j++)
						m_Clients[j] = m_Clients[j + 1];
					m_TotalClients--;  // decrease m_TotalClients by 1

					Log("Channel " + to_string(m_MsgChn) + " is down. Stop broadcasting service data to it. Total clients now is " 
						+ to_string(m_TotalClients));
				}
			break; // continue to read next message

		// auto process the onboard message
		case CMD_ONBOARD:
			for (size_t i = 1; i < m_TotalSubscriptions; i++)
				if (m_MsgChn == m_Subscriptions[i])
				{
					// re-subscribe the service when it is up again
					SndMsg(nullptr, CMD_SUBSCRIBE, 0, m_MsgChn);
					break;
				}
			break; // continue to read next message
			
		// auto parse the service data reply
		case CMD_SERVICEDATA:
			offset = 0;
			do
			{
				keyword.assign(m_buf.mText + offset); // read the keyword
				offset += keyword.length() + 1; // there is a /0 in the end
				if (keyword.empty()) // end of assignment when keyword is empty
					break;

				char n = m_buf.mText[offset++]; // read data length, 0 represents for string
				size_t i;
				for (i = 0; i < m_TotalProperties; i++)
				{
					// check if matched before
					if (keyword.compare(m_pptr[i]->keyword) == 0)
					{
						// check the type matched or not
						if (n != m_pptr[i]->len)
						{
							m_err = -11;
							Log("Fatal error! The data length/type of " + keyword + " from main module is " \
								+ to_string(n) + ", locals is of " \
								+ to_string(m_pptr[i]->len) + ". They are not indentical.", 1);
							return m_buf.type;
						}

						break;  // break for loop
					}
				}

				// check if not matched any local variable
				if (i >= m_TotalProperties)
					continue;  // no match then return

				// type 0 means a string which shall be assigned the value in a different way
				if (m_pptr[i]->len) 
				{
					memcpy(m_pptr[i]->ptr, m_buf.mText + offset, n);
					offset += n;
				}
				else
				{
					static_cast<string *>(m_pptr[i]->ptr)->assign(m_buf.mText + offset);
					offset += static_cast<string *>(m_pptr[i]->ptr)->length() + 1;
				}
			} while (offset < 255);

			return type; // return when get a service data. No more auto parse

		// Auto parse the system configurations
		case CMD_COMMAND:
			msg.assign(m_buf.mText);
			offset = msg.find_first_of('=');  // find =
			keyword = msg.substr(0, offset);  // keyword is left of =
			msg = msg.substr(offset + 1); // now msg is right of =

			// Update the log severity level
			if (!keyword.compare("LogSeverityLevel"))
			{
				if (!msg.compare("Info"))
					m_Severity = 2000;

				if (!msg.compare("Debug"))
					m_Severity = 3000;

				if (!msg.compare("Verbose"))
					m_Severity = 4000;
				return m_buf.type;
			}

			// the flag update of auto service data update
			if (!keyword.compare("AutoPublish"))
			{
				if (!msg.compare("on"))
					m_AutoPublish = true;
				if (!msg.compare("off"))
					m_AutoPublish = false;
				return m_buf.type;
			}

			// the flag update of the watchdog auto feed 
			if (!keyword.compare("AutoWatchdog"))
			{
				if (!msg.compare("on"))
					m_AutoWatchdog = true;
				if (!msg.compare("off"))
					m_AutoWatchdog = false;
				return m_buf.type;
			}

			// the flag update of auto sleep
			if (!keyword.compare("AutoSleep"))
			{
				if (!msg.compare("on"))
					m_AutoSleep = true;
				if (!msg.compare("off"))
					m_AutoSleep = false;
			}
			return type; // return when get a command. No more auto parse

		case CMD_STATUS:
			if (m_MsgChn != 1)
				return type; // do not report status
	
			offset = 0;
			struct timeval tv;
			gettimeofday(&tv, nullptr);
			m_buf.rChn = 1; // always report to main
			m_buf.sChn = m_Chn;
			m_buf.sec = tv.tv_sec;
			m_buf.usec = tv.tv_usec;
			m_buf.len = 0; // temperal value
			m_buf.type = CMD_STATUS;
			
			strcpy(m_buf.mText + offset, "TotalSent");
			offset += 10;
			m_buf.mText[offset++] = sizeof(m_TotalMessageSent);
			memcpy(m_buf.mText + offset, &m_TotalMessageSent, sizeof(m_TotalMessageSent));
			offset += sizeof(m_TotalMessageSent);

			strcpy(m_buf.mText + offset, "TotalReceived");
			offset += 14;
			m_buf.mText[offset++] = sizeof(m_TotalMessageReceived);
			memcpy(m_buf.mText + offset, &m_TotalMessageReceived, sizeof(m_TotalMessageReceived));
			offset += sizeof(m_TotalMessageReceived);

			strcpy(m_buf.mText + offset, "dbElements");
			offset += 11;
			m_buf.mText[offset++] = sizeof(m_TotalDatabaseElements);
			memcpy(m_buf.mText + offset, &m_TotalDatabaseElements, sizeof(m_TotalDatabaseElements));
			offset += sizeof(m_TotalDatabaseElements);

			strcpy(m_buf.mText + offset, "States");
			offset += 7;
			m_buf.mText[offset++] = 0; // it is a string
			m_buf.mText[offset++] = (m_Severity >> 4) & 0xFF;
			m_buf.mText[offset++] = m_AutoPublish ? 1 : 0;
			m_buf.mText[offset++] = m_AutoWatchdog ? 1 : 0;
			m_buf.mText[offset++] = m_AutoSleep ? 1 : 0;
			m_buf.mText[offset++] = 0; // end of the string

			strcpy(m_buf.mText + offset, "Subscriptions");
			offset += 14; // length of Subscriptions\0
			m_buf.mText[offset++] = 0; // 0 represents a string
			for (size_t i = 0; i < m_TotalSubscriptions; i++)
				m_buf.mText[offset++] = m_Subscriptions[i] & 0xFF;
			m_buf.mText[offset++] = 0; // add 0 at the end of a string

			m_buf.len = offset;
			if (offset + 8 + m_TotalClients > 255)
			{
				if (msgsnd(m_ID, &m_buf, m_buf.len + m_HeaderLength, IPC_NOWAIT))
				{
					m_err = errno;
					return m_buf.type;
				}
				else
				{
					m_err = 0;
					m_TotalMessageSent++;
					m_WatchdogTimer[m_buf.rChn] = m_buf.sec;  // set timer for next watchdog feed
					offset = 0;
				}
			}

			strcpy(m_buf.mText + offset, "Clients");
			offset += 8; // length of Clients\0
			m_buf.mText[offset++] = 0;  // 0 represents a string
			for (size_t i = 0; i < m_TotalClients; i++)
				m_buf.mText[offset++] = m_Clients[i];
			m_buf.mText[offset++] = 0; // add 0 at the end of a string

			m_buf.len = offset;
			if (msgsnd(m_ID, &m_buf, m_buf.len + m_HeaderLength, IPC_NOWAIT))
			{
				m_err = errno;
				return m_buf.type;
			}

			m_err = 0;
			m_TotalMessageSent++;
			m_WatchdogTimer[m_buf.rChn] = m_buf.sec;  // set timer for next watchdog feed
			offset = 0;
			break;

		default:
			m_err = -50;
			Log("Error! Unkown command " + to_string(m_buf.type) + " from " + to_string(m_buf.sChn), 1);
			return type;
		}

	} while (m_err == 0);
	return type;
}

// Feed the dog at watchdog main module
bool ServiceUtils::WatchdogFeed()
{
	// Message queue has not been opened
	if (m_ID == -1)
	{
		m_err = -1;
		return 0;
	}

	if (m_Chn == 1)
		return false; // No watchdog feed for the main module

	struct timeval tv;
	gettimeofday(&tv, nullptr);
	m_buf.rChn = 1; // always sends to main module
	m_buf.sChn = m_Chn;
	m_buf.sec = tv.tv_sec;
	m_buf.usec = tv.tv_usec;
	m_buf.len = 0;
	m_buf.type = CMD_WATCHDOG;

	// feed the watchdog roughly every second
	if (m_buf.sec == m_WatchdogTimer[m_buf.rChn])
		return false;
	if (m_buf.sec == m_WatchdogTimer[m_buf.rChn] + 1 && m_buf.usec < m_WatchdogTimer[0])
		return false;

	// Send heartbeat message and set new timer threshold
	if (msgsnd(m_ID, &m_buf, m_HeaderLength, IPC_NOWAIT))
	{
		m_err = errno;
		return false;
	}

	m_err = 0;
	m_TotalMessageSent++;
	m_WatchdogTimer[m_buf.rChn] = m_buf.sec;  // re-set watchdog timer for that channel
	m_WatchdogTimer[0] = m_buf.usec; // save the microsecond in [0]
	return true;
};

// Send a Log to main module with specified severe level
// Sending format is [LogType][LogContent]
bool ServiceUtils::Log(string logContent, int ErrorCode)
{
	// Message queue has not been opened
	if (m_ID == -1)
	{
		m_err = -1;
		return false;
	}

	// Log those with equal or lower level of severity
	if (ErrorCode >= m_Severity)
		return false;

	struct timeval tv;
	m_buf.rChn = 1; // log is always sent to main module
	m_buf.sChn = m_Chn;
	m_buf.type = CMD_LOG;

	// segmental the log into short logs in case it is very long
	size_t len;
	while (len = logContent.length())  // no log when the content is empty
	{
		gettimeofday(&tv, nullptr);
		m_buf.sec = tv.tv_sec;  //update the timestamp
		m_buf.usec = tv.tv_usec;

		int offset = 0;
		if (len > 200)
			len = 200;
		memcpy(m_buf.mText, &ErrorCode, sizeof(ErrorCode));
		offset += sizeof(ErrorCode);
		memcpy(m_buf.mText + offset, logContent.c_str(), len);
		offset += len;
		m_buf.mText[offset] = 0; // add a /0 in the end anyway
		m_buf.len = offset + 1; // include the /0 at the tail

		if (msgsnd(m_ID, &m_buf, m_buf.len + m_HeaderLength, IPC_NOWAIT))
		{
			m_err = errno;
			printf("Cannot send message in log with error %d.\n", m_err);
			return false;
		}

		m_err = 0;
		m_TotalMessageSent++;
		m_WatchdogTimer[m_buf.rChn] = m_buf.sec;  // set timer for next watchdog feed

		// shorten the logContent
		logContent = logContent.substr(len);
	}
	return true;
};

bool ServiceUtils::Log(string logContent)
{
	return Log(logContent, 1000);
}

// assign *p to store the variable queried from the database with length len, len=0 for string
bool ServiceUtils::LocalMap(string keyword, void *p, char len)
{
	if (!p)
	{
		m_err = -1;
		return false;
	}

	m_err = 0;
	for (size_t i = 0; i < m_TotalProperties; i++)
		if (!keyword.compare(m_pptr[i]->keyword))
		{
			if (m_pptr[i]->len != len)
			{
				m_err = -11;
				Log("Error. Data type of " + keyword + " from the main module is of " + to_string(m_pptr[i]->len) \
					+ ". Not identical to " + to_string(len) + " . Total properties is " + to_string(m_TotalProperties), 1);
				return false;
			}

			if (m_pptr[i]->ptr)
			{
				if (len)
					memcpy(p, m_pptr[i]->ptr, len);
				else
					static_cast<string *>(p)->assign( static_cast<string *>(m_pptr[i]->ptr)->c_str());
			}
			m_pptr[i]->ptr = p; // always assign the pointer to be the new mapped vaiable
			return true;
		}

	if (m_TotalProperties >= 255) // new keyword
	{
		Log("Error. Too many properties are added. Total properties is " + to_string(m_TotalProperties), 2);
		return false;
	}

	m_pptr[m_TotalProperties] = new Property;
	m_pptr[m_TotalProperties]->keyword = keyword;
	m_pptr[m_TotalProperties]->len = len;
	m_pptr[m_TotalProperties]->ptr = p;
	m_TotalProperties++;
	Log("Added " + keyword + " to the keywords list. Total properties is " + to_string(m_TotalProperties));
	return true;
}

// assign *s to store the string queried from the database
bool ServiceUtils::LocalMap(string keyword, string *s)
{
	return LocalMap(keyword, s, 0);
}

// assign *n to store the integer queried from the database
bool ServiceUtils::LocalMap(string keyword, int *n)
{
	return LocalMap(keyword, n, sizeof(int));
}

// assign *p with length len (0 for string) to be element
bool ServiceUtils::AddToServiceData(string keyword, void *p, char len)
{
	if (!p)
	{
		m_err = -1;
		return false;
	}

	m_err = 0;
	size_t i;
	for (i = 0; i < m_TotalServiceDataElements; i++)
		if (keyword.compare(m_ServiceDataElements[i].keyword) == 0)
		{
			if (m_ServiceDataElements[i].len != len)
			{
				m_err = -11;
				Log("Error. " + keyword + " specified before is of type/length " + to_string(m_pptr[i]->len) \
					+ ". It is not identical to new value " + to_string(len) 
					+ " . Total elements in service data  is " + to_string(m_TotalServiceDataElements), 1);
				return false;
			}

			return true; // return if it is already added
		}

	if (m_TotalServiceDataElements >= 255)
	{
		m_err = -102;
		Log("Error. Too many elements are added to the service data. Total elements is now "
			+ to_string(m_TotalServiceDataElements), 1);
		return false;
	}

	// Add it to the list if it does not added before
	m_ServiceDataElements[m_TotalServiceDataElements].keyword = keyword;
	m_ServiceDataElements[m_TotalServiceDataElements].len = len;
	m_ServiceDataElements[m_TotalServiceDataElements].ptr = p;
	m_TotalServiceDataElements++;
	Log("Added " + keyword + " to the service data. Total elements in the list is " + to_string(m_TotalServiceDataElements));
	return true;
}

// assign string *s to be element of the service data
bool ServiceUtils::AddToServiceData(string keyword, string *s)
{
	return AddToServiceData(keyword, s, 0);
}

// assign int *n to be element of the service data
bool ServiceUtils::AddToServiceData(string keyword, int *n)
{
	return AddToServiceData(keyword, n, sizeof(int));
}

// Send a request to database main module to query for the value of keyword. The results will sent back automatically by main module.
bool ServiceUtils::QueryConfigures()
{
	if (m_Chn == 1)
		return false; // No database query for the main module

	return SndMsg(nullptr, CMD_DATABASEQUERY, 0, 1);
};

// Send a request to database to update all the values of keywords with newvalue. The database main module will take care of the data type casting. 
// [keyword_1][type_1][len_1][data_1][keyword_2][type_2][len_2][data_2] ... [keyword_n][type_n][len_n][data_n] ; 
// end with keyword empty, type and length are of size 1 byte, keyword end with /0
bool ServiceUtils::UpdateConfigures()
{
	if (m_ID == -1)
	{
		m_err = -1;  // Message queue has not been opened
		return false;
	}

	if (m_Chn == 1)
		return false; // No database update for the main module

	struct timeval tv;
	gettimeofday(&tv, nullptr);
	m_buf.rChn = 1; // always sends to main module
	m_buf.sChn = m_Chn;
	m_buf.sec = tv.tv_sec;
	m_buf.usec = tv.tv_usec;
	m_buf.len = 0; // temporal assigned to 0
	m_buf.type = CMD_DATABASEUPDATE;

	size_t offset = 0;
	size_t i;
	for (size_t j = 0; j < m_TotalDatabaseElements; j++)
	{
		i = m_IndexdbElements[j];
		size_t len = m_pptr[i]->keyword.length() + 2;
		len += m_pptr[i]->len ? m_pptr[i]->len : static_cast<string *>(m_pptr[i]->ptr)->length() + 1;

		// send the message if the buffer is full
		if (offset + len > 255)
		{
			m_buf.len = offset;
			if (msgsnd(m_ID, &m_buf, m_buf.len + m_HeaderLength, IPC_NOWAIT))
			{
				m_err = errno;
				return false;
			};
			
			m_TotalMessageSent++;
			offset = 0;
			m_err = 0;
		}

		// assign the keyword of the property
		strcpy(m_buf.mText + offset, m_pptr[i]->keyword.c_str());
		offset += m_pptr[i]->keyword.length() + 1;
		
		//assign the length of the property
		len = m_pptr[i]->len;
		m_buf.mText[offset++] = m_pptr[i]->len;
		
		// assign the value of the property
		if (len)
		{
			memcpy(m_buf.mText + offset, m_pptr[i]->ptr, len);
			offset += len;
		}
		else
		{
			// A string is different from normal
			strcpy(m_buf.mText + offset, static_cast<string *>(m_pptr[i]->ptr)->c_str());
			offset += static_cast<string *>(m_pptr[i]->ptr)->length() + 1;
		}
	}

	m_buf.len = offset;
	if (msgsnd(m_ID, &m_buf, m_buf.len + m_HeaderLength, IPC_NOWAIT))
	{
		m_err = errno;
		return false;
	}
	m_err = 0;
	m_TotalMessageSent++;
	return true;
};

long ServiceUtils::GetServiceChannel(string serviceTitle)
{
	// query from the services list
	for (size_t i = 0; i < m_TotalServices; i++)
		if (serviceTitle.compare(m_ServiceTitles[i]) == 0)
			return m_ServiceChannels[i];

	// return service channel of myself
	if (serviceTitle.empty())
		return m_Chn;

	// Check is it a number 
	return atoi(serviceTitle.c_str());
};

string ServiceUtils::GetServiceTitle(long serviceChannel)
{
	// return service title of myself for 0
	if (!serviceChannel)
		return m_Title;

	for (size_t i = 0; i < m_TotalServices; i++)
		if (serviceChannel == m_ServiceChannels[i])
			return m_ServiceTitles[i];
	return "";
};
