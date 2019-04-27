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
	m_Title.assign("MAIN");

	m_TotalClients = 0;
	m_TotalDataBaseElements = 0;
	m_TotalMessageReceived = 0;
	m_TotalMessageSent = 0;
	m_TotalProperties = 0;
	m_TotalServiceDataElements = 0;
	m_TotalServices = 0;
	m_TotalSubscriptions = 0;
	m_Severity = 4;

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

	m_Key = 12345;  // This is for debug purpose
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
	m_TotalDataBaseElements = 0;
	m_TotalMessageReceived = 0;
	m_TotalMessageSent = 0;
	m_TotalProperties = 0;
	m_TotalServiceDataElements = 0;
	m_TotalServices = 0;
	m_TotalSubscriptions = 0;
	m_Severity = 4;

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
	{
		// Read off all previous messages in the queue at main module starts
		while (msgrcv(m_ID, &m_buf, sizeof(m_buf), 0, IPC_NOWAIT) > 0)
		{
			count++;
		}
		printf("(Debug) There are %d stale messages in main module's message queue.\n", count);
		m_err = 0;
		return true;
	}
	else
	{
		char txt[255];
		pid_t pid = getpid();
		pid_t ppid = getppid();

		// Read off all messages to me in the queue before I startup
		while (msgrcv(m_ID, &m_buf, sizeof(m_buf), m_Chn, IPC_NOWAIT) > 0)
		{
			count++;
		} 
		printf("(Debug) Service provider '%s' starts on channel #%ld with PID=%u, PPID=%u. There are %d stale messages for it.\n", 
			m_Title.c_str(), m_Chn, pid, ppid, count);

		// Report onboard to the main module
		strcpy(txt, m_Title.c_str());
		size_t len = m_Title.length() + 1;
		memcpy(txt + len, &pid, len);
		len += sizeof(pid);
		memcpy(txt + len, &ppid, len);
		len += sizeof(pid);
		SndMsg(txt, CMD_ONBOARD, len, 1);

		// Get initialization from the main module;
		count = 5;
		do
		{
			ChkNewMsg();
			if (count-- <= 0)
			{
				m_err = -10;
				Log("Cannot get the initialization messages from the main module in 5ms.", 1);
				return false;
			}
		} while (m_TotalServices <= 0);
		printf("(Debug) Get initialized in %dms.\n", 5 - count);
		
		// Broadcast my onboard messages to all service channels other than the main module and myself
		for (size_t i = 1; i < m_TotalServices; i++)
			SndMsg(txt, CMD_ONBOARD, len, m_ServiceChannels[i]);

		return true;
	}
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
	if (m_Chn == m_buf.rChn) // do not send messages to itself
		return false;

	struct timeval tv;
	gettimeofday(&tv, nullptr);
	m_buf.sChn = m_Chn;
	m_buf.sec = tv.tv_sec;
	m_buf.usec = tv.tv_usec;
	m_buf.len = msg.length() + 1;
	m_buf.type = CMD_COMMAND;

	strcpy(m_buf.mText, msg.c_str());
	if (msgsnd(m_ID, &m_buf, m_buf.len + m_HeaderLength, IPC_NOWAIT) == 0)
	{
		m_err = 0;
		m_TotalMessageSent++;
		if (m_buf.rChn == 1)
			m_WatchdogTimer[m_buf.rChn] = m_buf.sec;  // set timer for next watchdog feed
		return true;
	}
	m_err = errno;
	return false;
};

// Send a packet with given length to specified service provider
bool ServiceUtils::SndMsg(void *p, size_t type, size_t len, string ServiceTitle)
{
	long Chn = GetServiceChannel(ServiceTitle);
	if (Chn)
		return SndMsg(p, type, len, Chn);

	printf("(Debug) Cannot find %s in the service list.", ServiceTitle.c_str());
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
		printf("(Debug) Error. %ld is an invalid service channel. It shall be positive.", ServiceChannel);
		m_err = -2; // Cannot find the ServiceTitle in the list
		return false;
	}
	if (m_Chn == m_buf.rChn) // do not send messages to itself
		return false;

	struct timeval tv;
	gettimeofday(&tv, nullptr);
	m_buf.sChn = m_Chn;
	m_buf.sec = tv.tv_sec;
	m_buf.usec = tv.tv_usec;
	m_buf.len = len;  // len is of 0-255, so do the cast here.
	m_buf.type = type;

	memcpy(m_buf.mText, p, m_buf.len);
	if (msgsnd(m_ID, &m_buf, m_buf.len + m_HeaderLength, IPC_NOWAIT) == 0)
	{
		m_err = 0;
		m_TotalMessageSent++;
		if (ServiceChannel == 1)
			m_WatchdogTimer[m_buf.rChn] = m_buf.sec;  // set timer for next watchdog feed
		return true;
	}

	printf("(Debug) Error. Unable to send the message to channel %d. Message is of length %ld, and header length %ld.\n", m_ID, len, m_HeaderLength);
	m_err = errno;
	return false;
};

// Re-send the message to another service channel, using original settings and buffer
bool ServiceUtils::ReSendMsgTo(long ServiceChannel)
{
	m_buf.rChn = ServiceChannel;
	if (m_buf.rChn <= 0)
	{
		printf("(Debug) Error. %ld is an invalid service channel. It shall be positive.", ServiceChannel);
		m_err = -2; // Cannot find the ServiceTitle in the list
		return false;
	}

	if (msgsnd(m_ID, &m_buf, m_buf.len + m_HeaderLength, IPC_NOWAIT) == 0)
	{
		m_err = 0;
		m_TotalMessageSent++;
		if (ServiceChannel == 1)
			m_WatchdogTimer[m_buf.rChn] = m_buf.sec;  // set timer for next watchdog feed
		return true;
	}

	printf("(Debug) Error. Unable to send the message to channel %d. Message is of length %ld, and header length %ld.\n", m_ID, m_buf.len, m_HeaderLength);
	m_err = errno;
	return false;
}

//Subscribe the service from the service provider with specified title
bool ServiceUtils::ServiceSubscribe(string ServiceTitle)
{
	long ServiceChannel = GetServiceChannel(ServiceTitle);
	if (ServiceChannel < 1)
	{
		m_err = -1;
		return false;
	}

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
bool ServiceUtils::ServiceQuery(string ServiceTitle)
{
	if (m_ID == -1)
	{
		m_err = -1;  // Message queue has not been opened
		return false;
	}

	long Chn = GetServiceChannel(ServiceTitle);
	if (!Chn)
	{
		printf("(Debug) Cannot find %s in the service list.", ServiceTitle.c_str());
		m_err = -2;
		return false;
	}

	// It is a normal query send for clients.
	if (m_Chn > 1)
		return SndMsg(nullptr, CMD_QUERY, 0, Chn);

	// The main reply the database query results to the client
	struct timeval tv;
	gettimeofday(&tv, nullptr);
	m_buf.rChn = Chn;
	m_buf.sChn = m_Chn;
	m_buf.sec = tv.tv_sec;
	m_buf.usec = tv.tv_usec;
	m_buf.len = 0; // temporal assigned to 0
	m_buf.type = CMD_DATABASEQUERY;

	size_t offset = 0;
	for (size_t i = 0; i < m_TotalProperties; i++)
	{
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
	if (msgsnd(m_ID, &m_buf, m_buf.len + m_HeaderLength, IPC_NOWAIT) == 0)
	{
		m_err = 0;
		m_TotalMessageSent++;
		return true;
	}
	m_err = errno;
	return false;
}

// broadcast the service data to all clients
bool ServiceUtils::UpdateServiceData()
{
	if (m_ID == -1)
	{
		m_err = -1;  // Message queue has not been opened
		return false;
	}

	// broadcast the updated service data
	struct timeval tv;
	gettimeofday(&tv, nullptr);

	m_buf.sChn = m_Chn;
	m_buf.sec = tv.tv_sec;
	m_buf.usec = tv.tv_usec;
	m_buf.type = CMD_SERVICEDATA;
	m_buf.len = m_ServiceDataLength;

	// The main module will broadcast the message to all live service provider that a channel is down. 
	if (m_Chn == 1)
	{
		m_buf.type = CMD_DOWN;
		size_t chn;
		memcpy(m_buf.mText, m_ServiceData, m_ServiceDataLength);
		memcpy(&chn, m_ServiceData, sizeof(chn));
		for (size_t i = 0; i < m_TotalServices; i++)
		{
			if (i == chn)
				continue;

			//m_buf.rChn = m_ServiceChannels[i];
			//if (msgsnd(m_ID, &m_buf, m_buf.len + m_HeaderLength, IPC_NOWAIT) != 0)
			//{
			//	m_err = errno;
			//	return false;
			//}
			//m_TotalMessageSent++;
			if (!ReSendMsgTo(m_ServiceChannels[i]))
				return false;
		}
		return true;
	}

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
		//{
		//	m_buf.rChn = m_Clients[i];
		//	if (msgsnd(m_ID, &m_buf, m_buf.len + m_HeaderLength, IPC_NOWAIT) != 0)
		//	{
		//		m_err = errno;
		//		return false;
		//	}
		//	m_TotalMessageSent++;
		//}

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
	struct timespec tim = { 0, 1000000L }; // 1ms = 1000000ns

	do
	{
		memset(m_buf.mText, 0, sizeof(m_buf.mText));  // fill 0 before reading, make sure no garbage left over
		long l = msgrcv(m_ID, &m_buf, sizeof(m_buf), m_Chn, IPC_NOWAIT);
		l -= m_HeaderLength;
		m_err = static_cast<int>(l);
		if (l < 0)
		{
			// To sleep for 1ms. This may significantly reduce the CPU usage
			clock_nanosleep(CLOCK_REALTIME, 0, &tim, NULL);
			return 0;
		}
		m_TotalMessageReceived++;

		m_err = 0;
		m_MsgChn = m_buf.sChn; // the service channel where the message comes
		m_MsgTS_sec = m_buf.sec; // the time stamp in seconds of latest receiving message
		m_MsgTS_usec = m_buf.usec;
		string keyword;

		// no autoreply for main module
		if (m_Chn == 1)
		{
			m_WatchdogTimer[m_MsgChn] = m_MsgTS_sec; // set the watchdog timer for that channel
			//printf("(Debug) Watchdog timer %d updates to %ld.\n", m_MsgChn, m_WatchdogTimer[m_MsgChn]);

			if (m_buf.type == CMD_ONBOARD)
			{
				// keep record for those onboard services in the main module
				m_ServiceChannels[m_TotalServices] = m_buf.sChn;
				m_ServiceTitles[m_TotalServices++].assign(m_buf.mText);
			}

			if (m_buf.type == CMD_DOWN)
			{
				// Auto fill the service data with down channel
				m_ServiceDataLength = sizeof(m_MsgChn);
				memcpy(m_ServiceData, &m_MsgChn, m_ServiceDataLength);
			}

			// Automatic parse and map is needed when the service provider reply a database update
			if (m_buf.type != CMD_DATABASEUPDATE)
				return m_buf.type;

			// map the updated data from clients to local variables in main module
			offset = 0;
			do
			{
				keyword.assign(m_buf.mText + offset); // read the keyword
				offset += keyword.length() + 1; // there is a /0 in the end
				if (keyword.empty()) // end of assignment when keyword is empty
					return m_buf.type; 

				char n = m_buf.mText[offset++]; // read data length, 0 represents for string
				bool keywordMatched = false;
				size_t i;
				for (i = 0; i < m_TotalProperties; i++)
				{
					// check if the keyword matched
					if (keyword.compare(m_pptr[i]->keyword) == 0)
					{
						// check the len/type matched or not
						if (n != m_pptr[i]->len)
						{
							m_err = -121;
							printf("Error! %s from channel %d has length of %d, locals is of %d.\n", 
								m_pptr[i]->keyword.c_str(), m_MsgChn, n, m_pptr[i]->len);
							return m_buf.type;
						}

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
						keywordMatched = true;
						break;  // break for loop
					}
				}

				if (!keywordMatched)
				{
					m_err = -120;
					printf("Error! %s from channel %d does not match any local variables.", m_pptr[i]->keyword.c_str(), m_MsgChn);
				}
			} while (offset < 255);

			return m_buf.type;
		}

		// no auto reply for those normal receiving. type < 31 commands; 32 is string. 33 is integer. anything larger are user defined types
		if (m_buf.type >= 32)
			return m_buf.type;

		switch (m_buf.type)
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
			printf("Received new services list from the main module. My service title is %s. There are %d services in the list.\n", GetServiceTitle(m_Chn).c_str(), m_TotalServices);
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

				// map the system pre-defined SeverityLevel element
				if (keyword == "SeverityLevel" && n == sizeof(m_Severity))
					m_pptr[i]->ptr = &m_Severity; 

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
				for (j = 0; j <m_TotalDataBaseElements; j++)
					if (i == m_IndexdbElements[j])
						break;
 
				// Add the element to the database element list if it does not appear before
				if (j >= m_TotalDataBaseElements)
					m_IndexdbElements[m_TotalDataBaseElements++] = i;
			} while (offset < 255);
			break; // continue to read next message

		// auto process the message that a service is down.
		// [Service channel]
		case CMD_DOWN:
			memcpy(&l, m_buf.mText, sizeof(l)); // l stores the service channel which was down
			
			// return CMD_DOWN if it is the command from the main module to let this service down
			if (m_MsgChn == 1 && m_Chn == l)
				return m_buf.type;

			// if it is not from the server
			if (m_MsgChn > 1)
				l = m_MsgChn;

			// stop broadcasting service data to that client
			for (size_t i = 0; i < m_TotalClients; i++)
				if (m_Clients[i] == l)
				{
					// move later subscriber one step up
					for (size_t j = i; j < m_TotalClients - 1; j++)
						m_Clients[j] = m_Clients[j + 1];
					m_TotalClients--;  // decrease m_TotalClients by 1

					Log("Channel " + to_string(l) + " is down. Stop broadcasting service data to it. Total clients now is " 
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

			return m_buf.type; // return when get a service data. No more auto parse

		case CMD_COMMAND:
			return m_buf.type; // return when get a command. No more auto parse

		case CMD_STATUS:
			if (m_MsgChn != 1)
				return m_buf.type; // do not report status
	
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
			memcpy(m_buf.mText + offset, &m_TotalMessageSent, sizeof(m_TotalMessageSent));
			offset += sizeof(m_TotalMessageSent);

			strcpy(m_buf.mText + offset, "TotalReceived");
			offset += 14;
			memcpy(m_buf.mText + offset, &m_TotalMessageReceived, sizeof(m_TotalMessageReceived));
			offset += sizeof(m_TotalMessageReceived);

			strcpy(m_buf.mText + offset, "Subscriptions");
			offset += 14; // length of Subscriptions\0
			for (size_t i = 0; i < m_TotalSubscriptions; i++)
				m_buf.mText[offset++] = m_Subscriptions[i];

			if (offset + 8 + m_TotalClients > 255)
			{
				if (msgsnd(m_ID, &m_buf, m_buf.len + m_HeaderLength, IPC_NOWAIT) == 0)
				{
					m_err = 0;
					m_TotalMessageSent++;
					m_WatchdogTimer[m_buf.rChn] = m_buf.sec;  // set timer for next watchdog feed
					offset = 0;
				}
				else
				{
					m_err = errno;
					return m_buf.type;
				}
			}

			strcpy(m_buf.mText + offset, "Clients");
			offset += 8; // length of Clients\0
			for (size_t i = 0; i < m_TotalClients; i++)
				m_buf.mText[offset++] = m_Clients[i];
			break;

		default:
			m_err = -50;
			Log("Error! Unkown command " + to_string(m_buf.type) + " from " + to_string(m_buf.sChn), 1);
			return m_buf.type;
		}

	} while (m_err == 0);
	return m_buf.type;
}

// Feed the dog at watchdog main module
size_t ServiceUtils::WatchdogFeed()
{
	// Message queue has not been opened
	if (m_ID == -1)
	{
		m_err = -1;
		return 0;
	}

	struct timeval tv;
	gettimeofday(&tv, nullptr);
	m_buf.rChn = 1; // always sends to main module
	m_buf.sChn = m_Chn;
	m_buf.sec = tv.tv_sec;
	m_buf.usec = tv.tv_usec;
	m_buf.len = 0;
	m_buf.type = CMD_WATCHDOG;

	// Handle the service provider.
	if (m_Chn > 1)
	{
		// feed the watchdog roughly every second
		if (m_buf.sec <= m_WatchdogTimer[m_buf.rChn])
			return 0;

		// Send heartbeat message and set new timer threshold
		if (msgsnd(m_ID, &m_buf, m_HeaderLength, IPC_NOWAIT) == 0)
		{
			m_err = 0;
			m_TotalMessageSent++;
			m_WatchdogTimer[m_buf.rChn] = m_buf.sec;  // re-set watchdog timer for that channel

			//UpdateServiceData();  // Automatic update the service data right after watchdog feed
			return 1;
		}

		m_err = errno;
		return 0;
	}

	// Handle the main module watchdog
	for (size_t i = 0; i < m_TotalServices; i++)
	{
		size_t index = m_ServiceChannels[i];
		if (m_WatchdogTimer[index] > 0 && m_buf.sec >= m_WatchdogTimer[index] + 5)
		{
			//printf("(Debug) Watchdog %d reached at %ld.%6ld, where timer is %ld.\n", index, m_buf.sec, m_buf.usec, m_WatchdogTimer[index]);
			m_WatchdogTimer[index] = 0;
			return index;
		}
	}
	return 0;
};

// Send a Log to main module with specified severe level
// Sending format is [LogType][LogContent]
bool ServiceUtils::Log(string LogContent, char Severity)
{
	// for main module, log is print for debug
	if (m_Chn == 1)
	{
		printf("main module log : [%d] %s\n", Severity, LogContent.c_str());
		return true;
	}

	// Message queue has not been opened
	if (m_ID == -1)
	{
		m_err = -1;
		return false;
	}

	// Log those with equal or lower level of severity
	if (Severity > m_Severity)
		return false;

	struct timeval tv;
	gettimeofday(&tv, nullptr);
	m_buf.rChn = 1; // log is always sent to main module
	m_buf.sChn = m_Chn;
	m_buf.sec = tv.tv_sec;
	m_buf.usec = tv.tv_usec;
	m_buf.len = LogContent.length() + sizeof(Severity) + 1;
	m_buf.type = CMD_LOG;

	memcpy(m_buf.mText, &Severity, sizeof(Severity));
	memcpy(m_buf.mText + sizeof(Severity), LogContent.c_str(), LogContent.length() + 1);
	if (msgsnd(m_ID, &m_buf, m_buf.len + m_HeaderLength, IPC_NOWAIT) == 0)
	{
		m_err = 0;
		m_TotalMessageSent++;
		m_WatchdogTimer[m_buf.rChn] = m_buf.sec;  // set timer for next watchdog feed 
		return true;
	}
	m_err = errno;
	return false;
};

// Send a log to main with severe level 4
bool ServiceUtils::Log(string LogContent)
{
	return Log(LogContent, 4);
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
		if (keyword.compare(m_pptr[i]->keyword) == 0)
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
	Log("Added " + keyword + " to the service data. Total elements in the list is " + to_string(m_TotalProperties));
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
bool ServiceUtils::dbQuery()
{
	return SndMsg(nullptr, CMD_DATABASEQUERY, 0, 1);
};

// Send a request to database to update all the values of keywords with newvalue. The database main module will take care of the data type casting. 
// [keyword_1][type_1][len_1][data_1][keyword_2][type_2][len_2][data_2] ... [keyword_n][type_n][len_n][data_n] ; 
// end with keyword empty, type and length are of size 1 byte, keyword end with /0
bool ServiceUtils::dbUpdate()
{
	if (m_ID == -1)
	{
		m_err = -1;  // Message queue has not been opened
		return false;
	}

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
	for (size_t j = 0; j < m_TotalDataBaseElements; j++)
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
	if (msgsnd(m_ID, &m_buf, m_buf.len + m_HeaderLength, IPC_NOWAIT) == 0)
	{
		m_err = 0;
		m_TotalMessageSent++;
		return true;
	}
	m_err = errno;
	return false;
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
	// return service title of myself
	if (!serviceChannel)
		return m_Title;

	for (size_t i = 0; i < m_TotalServices; i++)
		if (serviceChannel == m_ServiceChannels[i])
			return m_ServiceTitles[i];
	return "";
};
