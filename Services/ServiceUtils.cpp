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

ServiceUtils::ServiceUtils(int argc, char *argv[])
{
	// Get the key for the message queue from pid for server, ppid for clients
	m_ID = -1;
	m_err = 0;
	m_HeaderLength = sizeof(m_buf.sChn) + sizeof(m_buf.sec) + sizeof(m_buf.usec) + sizeof(m_buf.type) + sizeof(m_buf.len);

	if (argc <= 1)
	{
		m_Chn = 1;
		m_Key = getpid();
		m_Title.assign("SERVER");
	}
	else
	{
		m_Chn = atoi(argv[1]);
		m_Key = getppid();
		m_Title = "";
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

	// initializing those internal buffers
	memset(&m_buf, 0, sizeof(m_buf));
	memset(m_Clients, 0, sizeof(m_Clients));
	memset(m_Subscriptions, 0, sizeof(m_Subscriptions));
	memset(m_ServiceTitles, 0, sizeof(m_ServiceTitles));
	memset(m_ServiceChannels, 0, sizeof(m_ServiceChannels));
	memset(m_WatchdogTimer, 0, sizeof(m_WatchdogTimer));

	m_TotalSubscriptions = 0;
	m_TotalClients = 0;
	m_TotalMessageSent = 0;
	m_TotalMessageReceived = 0;
	m_TotalServices = 0;
	m_TotalProperties = 0;
};

bool ServiceUtils::StartService()
{
	int count = 0;
	if (m_ID < 0)
	{
		m_err = -3;
		return false;
	}

	// The initialization of the server and other service providers are different
	if (m_Chn == 1)
	{
		// Read off all previous messages in the queue at server starts
		while (msgrcv(m_ID, &m_buf, sizeof(m_buf), 0, IPC_NOWAIT) > 0)
		{
			count++;
		}
		printf("(Debug) There are %d stale messages in server's message queue.\n", count);
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

		// Report onboard to the server
		size_t len = sizeof(pid);
		memcpy(txt, &pid, len);
		memcpy(txt + len, &ppid, len);
		len += len;
		strcpy(txt + len, m_Title.c_str());
		len += m_Title.length() + 1;
		bool rst = SndMsg(txt, CMD_ONBOARD, len, 1);

		// Get initialization from the server;
		count = 5;
		do
		{
			ChkNewMsg();
			if (count-- <= 0)
			{
				m_err = -10;
				Log("Cannot get the initialization messages from the server in 5ms.", 10);
				return false;
			}
		} while (m_TotalServices <= 0);
		printf("(Debug) Get initialized in %dms.\n", 5 - count);
		
		// Broadcast my onboard messages to all service channels other than the server and myself
		for (size_t i = 1; i < m_TotalServices; i++)
			rst = SndMsg(txt, CMD_ONBOARD, len, m_ServiceChannels[i]);

		return rst;
	}
	return true;
}

ServiceUtils::~ServiceUtils()
{
	for (size_t i = 0; i < m_TotalProperties; i++)
		delete m_pptr[i];

	// if the server closed, delete the message queue
	if (1 == m_Chn)
		msgctl(m_ID, IPC_RMID, nullptr);
};

// Send a string message to specified service provider. It is uasually a command.
bool ServiceUtils::SndMsg(string msg, string ServiceTitle)
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
	m_buf.type = CMD_STRING;

	strcpy(m_buf.mText, msg.c_str());
	if (msgsnd(m_ID, &m_buf, m_buf.len + m_HeaderLength, IPC_NOWAIT) == 0)
	{
		m_err = 0;
		m_TotalMessageSent++;
		if (m_buf.rChn == 1)
			m_WatchdogTimer[m_buf.rChn] = m_buf.sec + 2;  // set the next watchdog feed 2s later
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

	m_buf.rChn = ServiceChannel;
	if (m_buf.rChn <= 0)
	{
		printf("(Debug) Error. %ld is an invalid service channel. It shall be positive.", ServiceChannel);
		m_err = -2; // Cannot find the ServiceTitle in the list
		return false;
	}
	if (m_Chn == m_buf.rChn) // do not send messages to itself
		return false;

	if (len > 255)
	{
		printf("(Debug) Error. %ld is an invalid length. It shall be 0-255.", len);
		m_err = -3;
		return false;
	}

	if (type > 255)
	{
		printf("(Debug) Error. %ld is an invalid data type. It shall be 0-255.", type);
		m_err = -4;
		return false;
	}

	struct timeval tv;
	gettimeofday(&tv, nullptr);
	m_buf.sChn = m_Chn;
	m_buf.sec = tv.tv_sec;
	m_buf.usec = tv.tv_usec;
	m_buf.len = len;
	m_buf.type = type;

	memcpy(m_buf.mText, p, len);
	if (msgsnd(m_ID, &m_buf, len + m_HeaderLength, IPC_NOWAIT) == 0)
	{
		m_err = 0;
		m_TotalMessageSent++;
		if (ServiceChannel == 1)
			m_WatchdogTimer[m_buf.rChn] = m_buf.sec + 2;  // set the next watchdog feed 2s later
		return true;
	}

	printf("(Debug) Error. Unable to send the message to channel %d. Message is of length %ld, and header length %ld.\n", m_ID, len, m_HeaderLength);
	m_err = errno;
	return false;
};

bool ServiceUtils::SendServiceSubscription(string ServiceTitle)
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

// send the request for query service data from the provider channel
bool ServiceUtils::SendServiceQuery(string ServiceTitle)
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

	// Reply the database query results to the client
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
			if (msgsnd(m_ID, &m_buf, offset + m_HeaderLength, IPC_NOWAIT))
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
		m_buf.mText[offset++] = m_pptr[i]->len & 0xFF;

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
	if (msgsnd(m_ID, &m_buf, offset + m_HeaderLength, IPC_NOWAIT) == 0)
	{
		m_err = 0;
		m_TotalMessageSent++;
		return true;
	}
	m_err = errno;
	return false;
}

// broadcast the service data to all clients
bool ServiceUtils::BroadcastServiceDataUpdate(void *p, size_t type, size_t m_DataLength)
{
	if (m_ID == -1)
	{
		m_err = -1;  // Message queue has not been opened
		return false;
	}

	if (m_DataLength > 255)
	{
		m_err = -3;
		return false;
	}

	if (type > 255)
	{
		m_err = -4;
		return false;
	}

	struct timeval tv;
	gettimeofday(&tv, nullptr);

	m_buf.sChn = m_Chn;
	m_buf.sec = tv.tv_sec;
	m_buf.usec = tv.tv_usec;
	m_buf.type = type;
	m_buf.len = m_DataLength;
	memset(m_buf.mText, 0, sizeof(m_buf.mText));  // clear the sending buffer
	memcpy(m_buf.mText, p, m_DataLength);
	memset(m_Data, 0, sizeof(m_Data));
	memcpy(m_Data, p, m_DataLength);

	for (size_t i = 0; i < m_TotalClients; i++)
	{
		m_buf.rChn = m_Clients[i];
		if (!msgsnd(m_ID, &m_buf, m_DataLength + m_HeaderLength, IPC_NOWAIT))
		{
			m_err = errno;
			return false;
		}
	}

	m_err = 0;
	return true;
}; // Multicast the data stored at *p with m_DataLength and send it to every subscriber

// receive a message from sspecified ervice provider, like GPS, RADAR, TRIGGER. No AutoReply
string ServiceUtils::GetRcvMsg()
{
	//string msg = "";
	//msg.assign(m_buf.mText);
	return m_buf.mText;
}

// return the pointer of the buffer *p and its length. This buffer will change in next message operation.
size_t ServiceUtils::GetRcvMsgBuf(char **p)
{
	*p = m_buf.mText;
	return m_buf.len;
};

// receive a packet from specified service provider. Autoreply all requests when enabled.
size_t ServiceUtils::ChkNewMsg() // receive a packet from specified service provider. Autoreply all requests
{
	size_t i;
	size_t j;
	size_t n;
	size_t offset = 0;
	struct timespec tim = { 0, 1000000L }; // sleep for 1ms

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

		// no autoreply for server
		if (m_Chn == 1)
		{
			m_WatchdogTimer[m_MsgChn] = m_MsgTS_sec + 5; // set the watchdog 5s later
			//printf("(Debug) Watchdog timer %d updates to %ld.\n", m_MsgChn, m_WatchdogTimer[m_MsgChn]);
			if (m_buf.type == CMD_ONBOARD)  // keep record for those onboard services in the server
				m_ServiceChannels[m_TotalServices++] = m_buf.sChn;

			if (m_buf.type != CMD_DATABASEUPDATE)
				return m_buf.type;

			// map the updated data from clients to local variables
			offset = 0;
			do
			{
				keyword.assign(m_buf.mText + offset); // read the keyword
				offset += keyword.length() + 1; // there is a /0 in the end
				if (keyword == "") // end of assignment when keyword is empty
					return m_buf.type; 

				n = m_buf.mText[offset++]; // read data length, 0 represents for string
				bool keywordMatched = false;
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
					printf("Error! %s from channel %d does not any keywords in the database.", m_pptr[i]->keyword.c_str(), m_MsgChn);
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
			if (m_MsgChn > 0) // add new subscriber to the list when the type is positive
			{
				if (m_TotalClients >= 255) // make sure no over flow of subscription
				{
					m_err = -14;
					Log("Error. Too many clients added.", -m_err);
					return m_buf.type;
				}

				for (i = 0; i < m_TotalClients; i++)
					if (m_Clients[i] == m_MsgChn)
					{
						Log("The client " + to_string(m_MsgChn) + " is already subscribed.", 1000);
						break; // break for loop
					}
				if (i < m_TotalClients)
					break; // break switch

				m_Clients[m_TotalClients++] = m_MsgChn; // increase m_TotalClients by 1
				Log("Got new service subscription from " + to_string(m_MsgChn) + ". Now has " + to_string(m_TotalClients) + " clients.", 1000);
			}
			else
			{  // delete the corresponding subscriber from the list when the type is negative
				for (i = 0; i < m_TotalClients; i++)
					if (m_Clients[i] + m_MsgChn == 0)
					{  // move later subscriber one step up
						for (j = i; j < m_TotalClients - 1; j++)
							m_Clients[j] = m_Clients[j + 1];
						m_TotalClients--;  // decrease m_TotalClients by 1
					}

				Log("Service subscription from " + to_string(m_MsgChn) + " is canceled. Now has " + to_string(m_TotalClients) + " clients.", 1000);
			}
			break; // continue to read next messages

		// auto reply the query of service data query
		// The service data are stored at m_Data with type of m_DataType, length of m_DataLength. It shall be sent to m_MsgChn.
		case CMD_QUERY:
			SndMsg(m_Data, m_DataType, m_DataLength, m_MsgChn);
			break; // continue to read next message

		// auto process the services list reply from the server
		// [channel_1][title_1][channel_2][title_2] ... [channel_n][title_n] ; titles are end with /0. channel is of size 1 byte
		case CMD_LIST:
			offset = 0;

			do
			{
				m_ServiceChannels[m_TotalServices] = m_buf.mText[offset++];
				m_ServiceTitles[m_TotalServices].assign(m_buf.mText + offset);
				offset += m_ServiceTitles[m_TotalServices++].length() + 1; // increase the m_TotalServices, update the offset to next
			} while (offset < m_buf.len);
			printf("Received new services list from the server. My service title is %s. There are %d services in the list.\n", GetServiceTitle(m_Chn).c_str(), m_TotalServices);
			break;

		// auto process the database feedback from the server
		// [keyword_1][type_1][len_1][data_1][keyword_2][type_2][len_2][data_2] ... [keyword_n][type_n][len_n][data_n] ; 
		// end with keyword empty, type and length are of size 1 byte, keyword end with /0
		case CMD_DATABASEQUERY:
			offset = 0;
			do
			{
				keyword.assign(m_buf.mText + offset); // read the keyword
				offset += keyword.length() + 1; // there is a /0 in the end
				if (keyword == "") // end of assignment when keyword is empty
					break;

				n = m_buf.mText[offset++]; // read data length, 0 represents for string
				bool keywordMatched = false;
				for (i = 0; i < m_TotalProperties; i++)
				{
					// check if matched before
					if (keyword.compare(m_pptr[i]->keyword) == 0)
					{
						// check the type matched or not
						keywordMatched = n == m_pptr[i]->len;
						if (!keywordMatched)
						{
							m_err = -11;
							Log("Fatal error! The data length/type of " + keyword + " from server is " \
								+ to_string(n) + ", locals is of " \
								+ to_string(m_pptr[i]->len) + ". They are not indentical.", \
								- m_err);
							return m_buf.type;
						}

						break;  // break for loop
					}
				}

				// It is a new property if the keyword does not match any previous keywords
				if (!keywordMatched)
				{
					if (m_TotalProperties >= 255)
					{
						Log("Error. Too many properties are added.", 15);
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
			} while (offset < 255);
			break;

		// auto process the message that a service is down.
		// [Service channel]
		case CMD_DOWN:
			memcpy(&l, m_buf.mText, sizeof(l)); // l stores the service channel which was down

			if (m_MsgChn > 1)
				l = m_MsgChn;

			// stop broadcasting service data to that client
			for (i = 0; i < m_TotalClients; i++)
				if (m_Clients[i] == l)
				{
					// move later subscriber one step up
					for (j = i; j < m_TotalClients - 1; j++)
						m_Clients[j] = m_Clients[j + 1];
					m_TotalClients--;  // decrease m_TotalClients by 1

					Log("Channel " + to_string(l) + " is down. Stop broadcasting service data to it. Total clients now is " + to_string(m_TotalClients), 1000);
				}
			break;

		// auto process the onboard message
		case CMD_ONBOARD:
			for (size_t i = 1; i < m_TotalSubscriptions; i++)
				if (m_MsgChn == m_Subscriptions[i])
				{
					// re-subscribe the service when it is up again
					SndMsg(nullptr, CMD_SUBSCRIBE, 0, m_MsgChn);
					break;
				}
			break;

		default:
			m_err = -50;
			Log("Error! Unkown command " + to_string(m_buf.type) + " from " \
				+ to_string(m_buf.sChn), -m_err);
			return m_buf.type;

		}

	} while (m_err == 0);
	return m_buf.type;
}

// Feed the dog at watchdog server
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
	m_buf.rChn = 1; // always sends to server
	m_buf.sChn = m_Chn;
	m_buf.sec = tv.tv_sec;
	m_buf.usec = tv.tv_usec;
	m_buf.len = 0;
	m_buf.type = CMD_WATCHDOG;

	// Handle the service provider.
	if (m_Chn > 1)
	{
		// Do not feed the watchdog too often
		if (m_buf.sec < m_WatchdogTimer[m_buf.rChn])
			return 0;

		// Send heartbeat message and set new timer threshold
		if (msgsnd(m_ID, &m_buf, m_HeaderLength, IPC_NOWAIT) == 0)
		{
			m_err = 0;
			m_TotalMessageSent++;
			m_WatchdogTimer[m_buf.rChn] = m_buf.sec + 2;
			return 1;
		}

		m_err = errno;
		return 0;
	}

	// Handle the server watchdog
	for (size_t i = 0; i < m_TotalServices; i++)
	{
		size_t index = m_ServiceChannels[i];
		if (m_WatchdogTimer[index] > 0 && m_buf.sec >= m_WatchdogTimer[index])
		{
			//printf("(Debug) Watchdog %d reached at %ld.%6ld, where timer is %ld.\n", index, m_buf.sec, m_buf.usec, m_WatchdogTimer[index]);
			m_WatchdogTimer[index] = 0;
			return index;
		}
	}
	return 0;
};

// Send a Log to server
// Sending format is [LogType][LogContent]
bool ServiceUtils::Log(string LogContent, long LogType)
{
	// for server, log is not sent
	if (m_Chn == 1)
	{
		printf("Server log : [%d] %s\n", LogType, LogContent.c_str());
		return true;
	}

	// Message queue has not been opened
	if (m_ID == -1)
	{
		m_err = -1;
		return false;
	}

	struct timeval tv;
	gettimeofday(&tv, nullptr);
	m_buf.rChn = 1; // log is always sent to server
	m_buf.sChn = m_Chn;
	m_buf.sec = tv.tv_sec;
	m_buf.usec = tv.tv_usec;
	m_buf.len = LogContent.length() + sizeof(LogType) + 1;
	m_buf.type = CMD_LOG;

	memcpy(m_buf.mText, &LogType, sizeof(LogType));
	memcpy(m_buf.mText + sizeof(LogType), LogContent.c_str(), LogContent.length() + 1);
	if (msgsnd(m_ID, &m_buf, m_buf.len + m_HeaderLength, IPC_NOWAIT) == 0)
	{
		m_err = 0;
		m_TotalMessageSent++;
		m_WatchdogTimer[m_buf.rChn] = m_buf.sec + 2;
		return true;
	}
	m_err = errno;
	return false;
};

// assign *p to store the variable queried from the database with length len, len=0 for string
bool ServiceUtils::dbMap(string keyword, void *p, size_t len)
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
				Log("Error. Data type of " + keyword + " from the server is of " + to_string(m_pptr[i]->len) \
					+ ". Not identical to " + to_string(len) + " . Total properties is " + to_string(m_TotalProperties), -m_err);
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
		Log("Error. Too many properties are added. Total properties is " + to_string(m_TotalProperties), 102);
		return false;
	}

	m_pptr[m_TotalProperties] = new Property;
	m_pptr[m_TotalProperties]->keyword = keyword;
	m_pptr[m_TotalProperties]->len = len;
	m_pptr[m_TotalProperties]->ptr = p;
	m_TotalProperties++;
	Log("Added " + keyword + " to the keywords list. Total properties is " + to_string(m_TotalProperties), 1000);
	return true;
};

// assign *s to store the string queried from the database
bool ServiceUtils::dbMap(string keyword, string *s)
{
	return dbMap(keyword, s, 0);
};

// assign *n to store the integer queried from the database
bool ServiceUtils::dbMap(string keyword, int *n)
{
	return dbMap(keyword, n, sizeof(int));
};

// Send a request to database server to query for the value of keyword. The result will be placed in the queue by database server.
bool ServiceUtils::dbQuery()
{
	return SndMsg(nullptr, CMD_DATABASEQUERY, 0, 1);
};

// Send a request to database to update all the values of keywords with newvalue. The database server will take care of the data type casting. 
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
	m_buf.rChn = 1; // always sends to server
	m_buf.sChn = m_Chn;
	m_buf.sec = tv.tv_sec;
	m_buf.usec = tv.tv_usec;
	m_buf.len = 0; // temporal assigned to 0
	m_buf.type = CMD_DATABASEUPDATE;

	size_t offset = 0;
	for (size_t i = 0; i < m_TotalProperties; i++)
	{
		size_t len = m_pptr[i]->keyword.length() + 2;
		len += m_pptr[i]->len ? m_pptr[i]->len : static_cast<string *>(m_pptr[i]->ptr)->length() + 1;

		// send the message if the buffer is full
		if (offset + len > 255)
		{
			m_buf.len = offset;
			if (msgsnd(m_ID, &m_buf, offset + m_HeaderLength, IPC_NOWAIT))
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
		m_buf.mText[offset++] = m_pptr[i]->len & 0xFF;
		
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
	if (msgsnd(m_ID, &m_buf, offset + m_HeaderLength, IPC_NOWAIT) == 0)
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

	// Check is it a number 
	return atoi(serviceTitle.c_str());
};

string ServiceUtils::GetServiceTitle(long serviceChannel)
{
	for (size_t i = 0; i < m_TotalServices; i++)
		if (serviceChannel == m_ServiceChannels[i])
			return m_ServiceTitles[i];
	return "";
};
