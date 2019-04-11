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

ServiceUtils::ServiceUtils(long myServiceChannel)
{
	m_Chn = myServiceChannel;
	m_Title = "";
	m_ID = -1;
	m_err = 0;
	m_HeaderLength = sizeof(m_buf.sChn) + sizeof(m_buf.sec) + sizeof(m_buf.usec) + sizeof(m_buf.type) + sizeof(m_buf.len);

	// Get the key for the message queue from given filename, if empty using default name
	m_Key = 12345;

	if (m_Chn <= 0)
		m_err = -1;

	// initialize the internal buffers
	memset(&m_buf, 0, sizeof(m_buf));
	memset(&m_Clients, 0, sizeof(m_Clients));
	memset(&m_Subscriptions, 0, sizeof(m_Subscriptions));
	memset(&m_ServiceTitles, 0, sizeof(m_ServiceTitles));
	memset(&m_ServiceChannels, 0, sizeof(m_ServiceChannels));

	m_TotalSubscriptions = 0;
	m_TotalClients = 0;
	m_TotalMessageSent = 0;
	m_TotalMessageReceived = 0;
	m_TotalServices = 1;
	m_TotalProperties = 0;
	m_IndexDB = 0;
};

bool ServiceUtils::StartService()
{
	if (m_Chn <= 0)
	{
		m_err = -1; // 0 indicates no error;
		return false;
	}

	if (m_Key < 0)
	{
		printf("(Debug) Error. MsgQue Key = %d\n", m_Key);
		m_err = -2;
		return false;
	}

	m_ID = msgget(m_Key, PERMS | IPC_CREAT);
	printf("(Debug) MsgQue ID : %d\n", m_ID);

	if (m_ID == -1)
	{
		m_err = -3; //perror("msgget");
		return false;
	}

	if (m_Chn == 1) // The initialization of the server is different from those of other services
	{
		// Read off all previous messages
		do
		{
		} while (msgrcv(m_ID, &m_buf, sizeof(m_buf), 0, IPC_NOWAIT) > 0);
		m_err = 0;
		return true;
	}
	else
	{
		char txt[255];
		pid_t pid = getpid();
		pid_t ppid = getppid();
		size_t len;
		size_t type;

		// read and parse the init messages sent to me
		do
		{
			RcvMsg(txt, &type, &len);
			//} while (m_TotalServices == 0);
		} while (RcvMsg(txt, &type, &len));

		// find my title from the service list, can be "" in case there is no init messages
		m_Title = GetServiceTitle(m_Chn);
		printf("(Debug) Service provider '%s' starts on channel #%ld with PID=%u, PPID=%u.\n", m_Title, m_Chn, pid, ppid);

		// Report onboard to the server
		len = sizeof(pid);
		memcpy(txt, &pid, len);
		memcpy(txt + len, &ppid, len);
		len += len;
		strcpy(txt + len, m_Title.c_str());
		len += m_Title.length() + 1;
		return SndMsg(&txt, CMD_ONBOARD, len, 1);
	}
};

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

	struct timeval tv;
	gettimeofday(&tv, nullptr);
	m_buf.sChn = m_Chn;
	m_buf.sec = tv.tv_sec;
	m_buf.usec = tv.tv_usec;
	m_buf.len = msg.length() + 1;
	m_buf.type = 0;

	strcpy(m_buf.mText, msg.c_str());
	if (msgsnd(m_ID, &m_buf, m_buf.len + m_HeaderLength, IPC_NOWAIT) == 0)
	{
		m_err = 0;
		m_TotalMessageSent++;
		return true;
	}
	m_err = errno;
	return false;

	//	return SndMsg(static_cast< void*>(msg.c_str()), 0, msg.length() + 1, ServiceChannel);
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
		return true;
	}

	printf("(Debug) Error. Unable to send the message to channel %d. Message is of length %ld, and header length %ld.", m_ID, len, m_HeaderLength);
	m_err = errno;
	return false;
};

bool ServiceUtils::SubscribeService(string ServiceTitle)
{
	long ServiceChannel = GetServiceChannel(ServiceTitle);
	if (ServiceChannel < 1)
	{
		m_err = -1;
		return false;
	}

	if (!SndMsg(nullptr, CMD_SUBSCRIBE, 0, ServiceTitle))
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

// query service data from the provider channel
bool ServiceUtils::QueryServiceData(string ServiceTitle)
{
	return SndMsg(nullptr, CMD_QUERY, 0, ServiceTitle);
}

// broadcast the service data to all clients
bool ServiceUtils::BroadcastUpdate(void *p, size_t type, size_t m_DataLength)
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
string ServiceUtils::RcvMsg()
{
	char b[255];
	string msg = "";
	size_t len;
	size_t type;
	if (RcvMsg(&b, &type, &len) && (type == 0) && (len > 0))
		msg.assign(m_buf.mText, len);

	return msg;
}

// receive a packet from specified service provider. Autoreply all requests when enabled.
bool ServiceUtils::RcvMsg(void *p, size_t *type, size_t *len) // receive a packet from specified service provider. Autoreply all requests
{
	size_t i;
	size_t j;
	size_t n;
	size_t offset = 0;
	long index;
	struct timeval tv;

	do
	{
		//m_buf.rChn = m_Chn;
		memset(m_buf.mText, 0, sizeof(m_buf.mText));  // fill 0 before reading, make sure no garbage left over
		long l = msgrcv(m_ID, &m_buf, sizeof(m_buf), m_Chn, IPC_NOWAIT);
		l -= m_HeaderLength;
		m_err = static_cast<int>(l);
		if (l <= 0)
			return false;
		m_TotalMessageReceived++;

		m_err = 0;
		*type = m_buf.type;
		*len = m_buf.len;
		if (p != nullptr)
			memcpy(p, m_buf.mText, static_cast<size_t>(l));

		m_MsgChn = m_buf.sChn; // the service type of last receiving message
		m_MsgTS_sec = m_buf.sec; // the time stamp in seconds of latest receiving message
		m_MsgTS_usec = m_buf.usec;
		size_t typ;
		string keyword;

		// no autoreply for server
		if (m_Chn == 1)
			return true;

		// no auto reply for those normal receiving. type < 31 is normal return
		if (*type < 32)
			return true;

		switch (*type)
		{
			// auto reply for new subscription request
		case CMD_SUBSCRIBE:
			if (m_MsgChn > 0) // add new subscriber to the list when the type is positive
			{
				if (m_TotalClients >= 255) // make sure no over flow of subscription
				{
					m_err = -14;
					Log("Error. Too many clients added.", -m_err);
					return true;
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
		case CMD_QUERY:
			gettimeofday(&tv, nullptr);
			m_buf.rChn = m_MsgChn;
			m_buf.sChn = m_Chn;
			m_buf.sec = tv.tv_sec;
			m_buf.usec = tv.tv_usec;
			m_buf.type = m_DataType;
			m_buf.len = m_DataLength;

			memcpy(m_buf.mText, m_Data, m_DataLength);
			msgsnd(m_ID, &m_buf, m_DataLength + m_HeaderLength, IPC_NOWAIT);
			m_err = errno;
			break; // continue to read next message

		// auto process the services list reply from the server
		// [index_1][title_1][index_2][title_2] ... [index_n][title_n] ; titles are end with /0
		case CMD_LIST:
			offset = 0;

			do
			{
				memcpy(&index, m_buf.mText + offset, sizeof(index));
				offset += sizeof(index);
				m_ServiceChannels[m_TotalServices] = index;
				m_ServiceTitles[m_TotalServices].assign(m_buf.mText + offset);
				offset += m_ServiceTitles[m_TotalServices++].length() + 1; // increase the m_TotalServices, update the offset to next
			} while (offset < m_buf.len);
			Log("Received new services list from the server. There are " + to_string(m_TotalServices) + " services in the list.", 1000);
			break;

			// auto process the database init reply from the server
			// [type_1][keyword_1][type_2][keyword_2] ... [type_n][keyword_n] ; keywords are end with /0
		case CMD_DATABASEINIT:
			offset = 0;
			index = 0;

			do
			{
				memcpy(&typ, m_buf.mText + offset, sizeof(typ));	// read the type
				offset += sizeof(typ);
				keyword.assign(m_buf.mText + offset); // read the keyword

				// place the read DB keyword at the correct place of indexDB
				m_err = 0;
				for (i = 0; i < m_TotalProperties; i++)
				{
					if (keyword.compare(m_pptr[i]->keyword) == 0)
					{
						if (i < m_IndexDB)
						{
							m_err = -12;
							Log("Fatal error! The keyword " + keyword + \
								" is already in the list at position " + to_string(i), \
								- m_err);
							return true;
						}

						if (typ == m_pptr[i]->type)
						{
							m_pptr[i]->type = m_pptr[m_IndexDB]->type;
							m_pptr[i]->keyword = m_pptr[m_IndexDB]->keyword;
						}
						else
						{
							m_err = -11;
							Log("Fatal error! The data type of " + keyword + " from server is " \
								+ to_string(typ) + ", locals is of " \
								+ to_string(m_pptr[i]->type) + ". They are not indentical.", \
								- m_err);
							return true;
						}

						break;  // beak for loop
					}
				}

				m_TotalProperties++;
				m_pptr[m_IndexDB] = new Property;
				m_pptr[m_IndexDB]->type = typ;
				m_pptr[m_IndexDB]->keyword = keyword;
				m_IndexDB++;
				Log("Added " + keyword + " from server with type " + to_string(typ) + " to the local keywords list.", 1000);

			} while (offset < m_buf.len);
			break;

			// auto process the database feedback from the server
			// [index_1][len_1][data_1][index_2][len_2][data_2] ... [index_n][len_n][data_n] ; end with index=/0
		case CMD_DATABASEQUERY:
			do
			{
				memcpy(&i, m_buf.mText + offset, sizeof(i)); // read element index
				offset += sizeof(i);
				memcpy(&n, m_buf.mText + offset, sizeof(n)); // read data length
				offset += sizeof(n);
				if ((offset > 255) || (n < 1))	// indicates no more data
					break;  // break do while loop

				m_pptr[i]->len = n;
				if (m_pptr[i]->type == 0) // a string shall be assigned the value
				{
					if (!m_pptr[i]->ptr)
						m_pptr[i]->ptr = new string;
					static_cast<string *>(m_pptr[i]->ptr)->assign(m_buf.mText + offset, n);
				}
				else
				{
					if (!m_pptr[i]->ptr)  // make sure the pointer is not NULL before assign any value to it
						m_pptr[i]->ptr = new char[n];
					memcpy(m_pptr[i]->ptr, m_buf.mText + offset, n);
				}
				offset += n;
			} while (offset < 255);
			break;

			// auto process the notice that a service is down.
			// [Service channel]
		case CMD_DOWN:
			memcpy(&l, m_buf.mText, sizeof(l)); // l stores the service channel which was down
			Log("Got a notice that service channel " + to_string(l) + " is down.", 1000);
			keyword = GetServiceTitle(l);
			if (keyword.length() == 0)
			{
				m_err = -100;
				Log("Error. No such a service channel " + to_string(l) \
					+ " exists in the list. ", -m_err);
				return true;
			}

			// stop broadcasting service data to that client
			for (i = 0; i < m_TotalClients; i++)
				if (m_Clients[i] == l)
				{
					// move later subscriber one step up
					for (j = i; j < m_TotalClients - 1; j++)
						m_Clients[j] = m_Clients[j + 1];
					m_TotalClients--;  // decrease m_TotalClients by 1
					Log("Stop broadcast service data to " + keyword \
						+ ". Total clients now is " + to_string(m_TotalClients), 1000);
				}


			// re-subscribe the service, waiting for the service provider being up again
			for (i = 0; i < m_TotalSubscriptions; i++)
				if (m_Subscriptions[i] == l)
				{
					SubscribeService(keyword);
					Log("Re-subscribe " + keyword, 1000);
				}

			break;

		default:
			m_err = -50;
			Log("Error! Unkown command " + to_string(*type) + " from " \
				+ to_string(m_buf.sChn), -m_err);
			return true;

		}

	} while (m_err == 0);
	return true;
}

// Feed the dog at watchdog server
bool ServiceUtils::FeedWatchDog()
{
	m_err = -1;  // Message queue has not been opened
	if (m_ID == -1)
		return false;

	struct timeval tv;
	gettimeofday(&tv, nullptr);
	m_buf.rChn = 1; // always sends to server
	m_buf.sChn = m_Chn;
	m_buf.sec = tv.tv_sec;
	m_buf.usec = tv.tv_usec;
	m_buf.len = 0;
	m_buf.type = CMD_WATCHDOG;

	if (msgsnd(m_ID, &m_buf, m_HeaderLength, IPC_NOWAIT) == 0)
	{
		m_err = 0;
		m_TotalMessageSent++;
		return true;
	}
	m_err = errno;
	return false;

	//return SndMsg(nullptr, CMD_WATCHDOG, 0, "SERVER");
};

// Send a Log to server
// Sending format is [LogType][LogContent]
bool ServiceUtils::Log(string LogContent, long LogType)
{
	m_err = -1;  // Message queue has not been opened
	if (m_ID == -1)
		return false;

	struct timeval tv;
	gettimeofday(&tv, nullptr);
	m_buf.rChn = 1; // always sends to server
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
		return true;
	}
	m_err = errno;
	return false;
};

// assign *i to store the integer queried from the database
bool ServiceUtils::dbMap(string keyword, void *p, size_t type)
{
	m_err = 0;
	size_t index = getIndex(keyword);
	if (index == 255) // new keyword
	{
		m_pptr[m_TotalProperties] = new Property;
		m_pptr[m_TotalProperties]->keyword = keyword;
		m_pptr[m_TotalProperties]->type = type;
		m_pptr[m_TotalProperties]->ptr = p;
		m_TotalProperties++;
		Log("Added " + keyword + " to the keywords list. Total properties is " + to_string(m_TotalProperties), 1000);
		return true;
	}

	if (m_pptr[index]->type != type)
	{
		m_err = -11;
		Log("Error. Data type of " + keyword + " from the server is of " + to_string(m_pptr[index]->type) \
			+ ". Not identical to " + to_string(type) + " . Total properties is " + to_string(m_TotalProperties), -m_err);
		return false;
	}

	// TODO delete string * ? Potential memory leak if re-map string
	m_pptr[index]->ptr = p;
	m_pptr[index]->type = type;
	return true;
};

// assign *s to store the string queried from the database
bool ServiceUtils::dbMap(string keyword, string *s)
{
	return dbMap(keyword, s, 0);
};

// Send a request to database server to query for the value of keyword. The result will be placed in the queue by database server.
// [index of the keyword]\0
bool ServiceUtils::dbQuery()
{
	m_err = -1;  // Message queue has not been opened
	if (m_ID == -1)
		return false;

	struct timeval tv;
	gettimeofday(&tv, nullptr);
	m_buf.rChn = 1; // always sends to server
	m_buf.sChn = m_Chn;
	m_buf.sec = tv.tv_sec;
	m_buf.usec = tv.tv_usec;
	m_buf.len = 0;
	m_buf.type = CMD_DATABASEQUERY;

	if (msgsnd(m_ID, &m_buf, m_HeaderLength, IPC_NOWAIT))
	{
		m_err = 0;
		m_TotalMessageSent++;
		return true;
	}
	m_err = errno;
	return false;
};

// Send a request to database to update all the values of keywords with newvalue. The database server will take care of the data type casting. 
// [index_1][len_1][data_1][index_2][len_2][data_2] ... [index_n][len_n][data_n] ; end with index=/0
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
	size_t len = 0;
	for (size_t i = 0; i < m_TotalProperties; i++)
	{
		// assign the index of the property
		memcpy(m_buf.mText + offset, &i, sizeof(i));
		
		//assign the length of the property
		offset += sizeof(i);
		len = m_pptr[i]->len;
		memcpy(m_buf.mText + offset, &len, sizeof(len));
		
		// assign the value of the property
		offset += sizeof(len);
		memcpy(m_buf.mText + offset, m_pptr[i]->ptr, len);
		offset += len;
	}

	if (msgsnd(m_ID, &m_buf, len + m_HeaderLength, IPC_NOWAIT) == 0)
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
	long chn = stoi(serviceTitle);

	if (chn > 0 && chn <= 255)
		return chn;

	for (size_t i = 0; i < m_TotalServices; i++)
		if (serviceTitle.compare(m_ServiceTitles[i]) == 0)
			return m_ServiceChannels[i];
	return 0;
};

string ServiceUtils::GetServiceTitle(long serviceChannel)
{
	for (size_t i = 0; i < m_TotalServices; i++)
		if (serviceChannel == m_ServiceChannels[i])
			return m_ServiceTitles[i];
	return "";
};

inline size_t ServiceUtils::getIndex(string keyword)
{
	for (size_t i = 0; i < m_TotalProperties; i++)
		if (keyword.compare(m_pptr[i]->keyword) == 0)
			return i;
	return 255;
}
