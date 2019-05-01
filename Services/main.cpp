#include <ctime>
#include <chrono>
#include <iostream>
#include <unistd.h>
#include <sys/time.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <string.h>
#include <sqlite3.h>
#include "ServiceUtils.h"

using namespace std;

class MainModule : public ServiceUtils
{
public:

	MainModule()
	{
		m_AutoSleep = true;  // Auto sleep is also enabled in main
		m_AutoUpdate = false; // No auto update for service data in main. The m_ServiceData is reused by service list
		m_AutoWatchdog = false; // No auto watchdog feed at main

		int rst = sqlite3_open("/home/georges/projects/Services/logs.db", &m_Logdb);
		m_err = rst ? -1 : 0;
	}

	~MainModule()
	{
		sqlite3_close(m_Logdb);
	}

	// used to store the index of database elements for each sub module 
	size_t SubIndex[32][127];

	size_t m_TotalDBElements[127];

	sqlite3 *m_Logdb;

	// Add an database keyword in a sub module. This keyword shall be added into the properties before
	bool AddDBElement(string keyword, long Channel)
	{
		size_t i;
		for (i = 0; i < m_TotalServices; i++)
			if (Channel == m_ServiceChannels[i])
				break;
		if (i >= m_TotalServices)
			return false;  // no such a channel in the service list

		size_t index = i;
		for (i = 0; i < m_TotalProperties; i++)
			if (!keyword.compare(m_pptr[i]->keyword))
				break;
		if (i >= m_TotalProperties)
			return false; // no such a keyword in the Properties

		size_t j = m_TotalDBElements[index];
		SubIndex[index][j] = i;
		m_TotalDBElements[index]++;
		return true;
	}

	// Add a service in the list, typically this information is queried from startup table
	bool AddAService(string Title, long Channel)
	{
		if (Channel <= 0 || Channel > 255)
		{
			m_err = -1;
			return false;
		}

		m_err = 0;
		m_ServiceTitles[m_TotalServices] = Title;
		m_ServiceChannels[m_TotalServices] = Channel;
		m_TotalDBElements[m_TotalServices] = 0;
		m_TotalServices++;

		m_ServiceData[m_ServiceDataLength++] = Channel & 0xFF;
		strcpy(m_ServiceData + m_ServiceDataLength, Title.c_str());
		m_ServiceDataLength += Title.length() + 1;
		return true;
	};

	// Let each sub module report its message status
	bool RequestModuleStatus()
	{
		// The main informs every sub module that the service on the Channel has down
		struct timeval tv;
		gettimeofday(&tv, nullptr);
		m_buf.rChn = 1;
		m_buf.sChn = m_Chn;
		m_buf.sec = tv.tv_sec;
		m_buf.usec = tv.tv_usec;
		m_buf.len = 0; // temporal assigned to 0
		m_buf.type = CMD_STATUS;

		for (size_t i = 0; i < m_TotalClients; i++)
			ReSendMsgTo(m_Clients[i]);
		return true;
	}

	bool InformDown()
	{
		// The main informs every sub module that the service on the Channel has down
		struct timeval tv;
		gettimeofday(&tv, nullptr);
		m_buf.rChn = m_MsgChn;
		m_buf.sChn = m_MsgChn;
		m_buf.sec = tv.tv_sec;
		m_buf.usec = tv.tv_usec;
		m_buf.len = 0; // temporal assigned to 0
		m_buf.type = CMD_DOWN;

		// broadcast to every sub modules, not main
		for (size_t i = 1; i < m_TotalServices; i++)
			ReSendMsgTo(m_ServiceChannels[i]);

		return true;
	}

	// Reply the database query from the sub modules
	bool ReplyDBQuery()
	{
		// The main reply the database query results to the client
		struct timeval tv;
		gettimeofday(&tv, nullptr);
		m_buf.rChn = m_MsgChn;
		m_buf.sChn = m_Chn;
		m_buf.sec = tv.tv_sec;
		m_buf.usec = tv.tv_usec;
		m_buf.len = 0; // temporal assigned to 0
		m_buf.type = CMD_DATABASEQUERY;

		size_t offset = 0;
		size_t i;
		for (i = 0; i < m_TotalServices; i++)
			if (m_ServiceChannels[i] == m_MsgChn)
				break;
		if (i >= m_TotalServices)
			return false;  // no such a service channel in the list

		size_t totalElenemts = m_TotalDBElements[i];
		for (i = 0; i < totalElenemts; i++)
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
		if (msgsnd(m_ID, &m_buf, m_buf.len + m_HeaderLength, IPC_NOWAIT))
		{
			m_err = errno;
			return false;
		}
		m_err = 0;
		m_TotalMessageSent++;
		return true;
	}

	bool ReplyServiceList()
	{
		// The main reply the database query results to the client
		struct timeval tv;
		gettimeofday(&tv, nullptr);
		m_buf.rChn = m_MsgChn;
		m_buf.sChn = m_Chn;
		m_buf.sec = tv.tv_sec;
		m_buf.usec = tv.tv_usec;
		m_buf.type = CMD_LIST;

		memcpy(m_buf.mText, m_ServiceData, m_ServiceDataLength);
		m_buf.len = m_ServiceDataLength;
		if (msgsnd(m_ID, &m_buf, m_buf.len + m_HeaderLength, IPC_NOWAIT))
		{
			m_err = errno;
			return false;
		}
		m_err = 0;
		m_TotalMessageSent++;
		return true;
	}

	void ParseDBUpdate()
	{
		// map the updated data from clients to local variables in main module
		size_t offset = 0;
		string keyword;
		do
		{
			keyword.assign(m_buf.mText + offset); // read the keyword
			offset += keyword.length() + 1; // there is a /0 in the end
			if (keyword.empty()) // end of assignment when keyword is empty
				return;

			char n = m_buf.mText[offset++]; // read data length, 0 represents for string
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
						// This is a error 
						Log(keyword + " from channel " + to_string(m_MsgChn) + " has length of "
							+ to_string(n) + ", local's is of " + to_string(m_pptr[i]->len), 2);
						continue;  // continue to parse next one
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

					break;  // break for loop
				}
			}

			if (i >= m_TotalProperties)
			{
				m_err = -120;
				// This is a warning
				Log(m_pptr[i]->keyword + " from channel " + to_string(m_MsgChn) + " does not match any local variables.", 3);
			}
		} while (offset < 255);

	};

	size_t CheckWatchdog()
	{
		struct timeval tv;
		gettimeofday(&tv, nullptr);

		for (size_t i = 1; i < m_TotalServices; i++)
		{
			size_t index = m_ServiceChannels[i];
			if (m_WatchdogTimer[index] > 0 && tv.tv_sec >= m_WatchdogTimer[index] + 5)
			{
				m_WatchdogTimer[index] = 0;
				return index;
			}
		}
		return 0;
	}

	size_t ChkNewMsg()
	{
		memset(m_buf.mText, 0, sizeof(m_buf.mText));  // fill 0 before reading, make sure no garbage left over
		long l = msgrcv(m_ID, &m_buf, sizeof(m_buf), m_Chn, IPC_NOWAIT);
		l -= m_HeaderLength;
		m_err = static_cast<int>(l);
		struct timespec tim = { 0, 1000000L }; // 1ms = 1000000ns
		if (l < 0)
		{
			// Auto update the service data if enabled
			if (m_AutoUpdate)
				UpdateServiceData();

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
		size_t type = m_buf.type;
		size_t offset;
		pid_t pid, ppid;
		size_t TotalMsgSent;
		size_t TotalMsgReceived;
		size_t len;
		string sSeverity[6]{ "Critical", "Error", "Warning", "Info", "Debug", "Verbose" };
		string sState[2]{ "off", "on" };

		string keyword;
		string msg;
		char Severity;

		// reset the watchdog timer for that channel
		m_WatchdogTimer[m_MsgChn] = m_MsgTS_sec; 

		// no auto reply for those normal receiving. type < 31 commands; 32 is string. 33 is integer. anything larger are user defined types
		if (type >= 32)
			return type;

		struct timeval tv;
		gettimeofday(&tv, nullptr);

		size_t i; // general iter
		switch (type)
		{
		case CMD_ONBOARD:
			// update onboard list which is stored at m_Clients[]
			for (i = 0; i < m_TotalClients; i++)
			{ 
				if (m_Clients[i] == m_MsgChn)
					break;
			}
			if (i >= m_TotalClients)
				m_Clients[m_TotalClients] = m_MsgChn;

			// get the module pid and save it in m_Subscriptions[], reuse the storage
			memcpy(&pid, m_buf.mText, sizeof(pid));
			offset = sizeof(pid);
			memcpy(&ppid, m_buf.mText + offset, sizeof(ppid));
			m_Subscriptions[m_TotalClients++] = pid;

			// reply databse query first
			ReplyDBQuery();

			// reply service list to sub-module next
			ReplyServiceList();

			// reply the log severity level at the end
			//TODO get the severity from database
			msg = GetServiceTitle(m_MsgChn);
			SndCmd("LogSeverityLevel=4", msg);

			// local log
			Log("A new service '" + msg + "' gets onboard at channel " + to_string(m_MsgChn));
			return type;

		case CMD_DATABASEUPDATE:
			msg = GetServiceTitle(m_MsgChn);
			ParseDBUpdate();
			Log("Service '" + msg + "' updates its database elements.");
			return type;

		case CMD_LOG:
			Log();
			return type;

		case CMD_DATABASEQUERY:
			msg = GetServiceTitle(m_MsgChn);
			ReplyDBQuery();
			Log("Service '" + msg + "' requests database query.", 5);
			return type;

		case CMD_WATCHDOG:
			msg = GetServiceTitle(m_MsgChn);
			Log("Service '" + msg + "' feed its watchdog.", 5);
			return type;

		case CMD_STRING:
			msg = GetServiceTitle(m_MsgChn);
			Log("Service '" + msg + "'sends a message '" + GetRcvMsg() + "' to the main. ");
			return type;

		case CMD_DOWN:
			msg = GetServiceTitle(m_MsgChn);
			// Test RequestServiceStatus here
			// RequestModuleStatus();

			// broadcast the message that one channel has down
			InformDown();

			Log("Service " + msg + " reports down. Informs every sub modules.");
			return type;

		// Auto parse the system configurations
		case CMD_COMMAND:
			msg.assign(m_buf.mText);
			offset = msg.find_first_of('=');  // find =
			keyword = msg.substr(0, offset);  // keyword is left of =
			msg = msg.substr(offset + 1); // now msg is right of =

			// Update the log severity level
			if (!keyword.compare("LogSeverityLevel"))
			{
				int i = atoi(msg.c_str());
				if (i > 0 && i < 7)
					m_Severity = i;

				if (!msg.compare("Information"))
					m_Severity = 4;

				if (!msg.compare("Debug"))
					m_Severity = 5;

				if (!msg.compare("Verbose"))
					m_Severity = 6;
				return m_buf.type;
			}

			// the flag update of auto service data update
			if (!keyword.compare("AutoUpdate"))
			{
				if (msg.compare("on"))
					m_AutoUpdate = true;
				if (msg.compare("off"))
					m_AutoUpdate = false;
				return m_buf.type;
			}

			// the flag update of the watchdog auto feed 
			if (!keyword.compare("AutoWatchdog"))
			{
				if (msg.compare("on"))
					m_AutoWatchdog = true;
				if (msg.compare("off"))
					m_AutoWatchdog = false;
				return m_buf.type;
			}

			// the flag update of auto sleep
			if (!keyword.compare("AutoSleep"))
			{
				if (msg.compare("on"))
					m_AutoSleep = true;
				if (msg.compare("off"))
					m_AutoSleep = false;
			}
			return type; // return when get a command. No more auto parse


		case CMD_STATUS:
			msg = GetServiceTitle(m_MsgChn);
			Log("Service '" + msg + "' reports its status");

			TotalMsgReceived = 0;
			TotalMsgSent = 0;
			offset = 0;

			// total messages sent
			msg.assign(m_buf.mText + offset);
			offset += msg.length() + 1;
			memcpy(&len, m_buf.mText + offset + 1, sizeof(len));
			Log(msg + "(" + to_string(m_buf.mText[offset]) + ") = " + to_string(len));
			offset += sizeof(len) + 1;
			TotalMsgSent += len;

			// total messages received
			msg.assign(m_buf.mText + offset);
			offset += msg.length() + 1;
			memcpy(&len, m_buf.mText + offset, sizeof(len));
			Log(msg + "(" + to_string(m_buf.mText[offset]) + ") = " + to_string(len));
			offset += sizeof(len) + 1;
			TotalMsgReceived += len;

			// total database elements
			msg.assign(m_buf.mText + offset);
			offset += msg.length() + 1;
			msg.append("(" + to_string(m_buf.mText[offset++]) + ") = ");
			memcpy(&len, m_buf.mText + offset, sizeof(len));
			msg.append(to_string(len));
			Log(msg);
			offset += sizeof(len) + 1;

			// status
			msg.assign(m_buf.mText + offset);
			offset += msg.length() + 1;
			msg.append("(" + to_string(m_buf.mText[offset++]) + ") = ");
			msg.append(sSeverity[m_buf.mText[offset++]] + " ");
			msg.append(sState[m_buf.mText[offset++]] + " ");
			msg.append(sState[m_buf.mText[offset++]] + " ");
			msg.append(sState[m_buf.mText[offset++]]);
			Log(msg);
			offset++;

			// subscriptions
			msg.assign(m_buf.mText + offset);
			len = msg.length();
			offset += len + 1;
			msg.append("(" + to_string(m_buf.mText[offset++]) + ") = ");
			for (size_t i = 0; i < len; i++)
				msg.append(to_string(m_buf.mText[offset + i]) + " ");
			Log(msg);
			offset += len + 1;

			// clients
			msg.assign(m_buf.mText + offset);
			len = msg.length();
			offset += len + 1;
			msg.append("(" + to_string(m_buf.mText[offset++]) + ") = ");
			for (size_t i = 0; i < len; i++)
				msg.append(to_string(m_buf.mText[offset + i]) + " ");
			Log(msg);
			offset += len + 1;

			return type;

		default:
			Log("Gets a message '" + GetRcvMsg() + "' with type of " + to_string(type) + " and length of " + to_string(len));
		}
	}

	// print the log from local with given severity
	bool Log(string msg, char Severity)
	{
		if (Severity > m_Severity)
			return false;

		string sSeverity[6]{ "Critical", "Error", "Warning", "Info", "Debug", "Verbose" };
		string sql = "INSERT INTO AppLog VALUES (";
		char *zErrMsg = 0;

		struct timeval tv;
		gettimeofday(&tv, nullptr);
		
		sql.append(to_string(tv.tv_sec) + ", " + to_string(tv.tv_usec) + ", 'MAIN', 1, ");
		sql.append(to_string(Severity) + ", '");
		sql.append(msg);
		sql.append("', 0, 0);");
		cout << msg << endl;

		if (sqlite3_exec(m_Logdb, sql.c_str(), nullptr, 0, &zErrMsg) != SQLITE_OK)
		{
			fprintf(stderr, "Cannot insert new log into AppLog table with error: %s\n", zErrMsg);
			sqlite3_free(zErrMsg);
			return false;
		}
		return true;
	}

	// print local log with normal severity
	bool Log(string msg)
	{
		return Log(msg, 4);
	}
	
	// print the log from service module
	bool Log()
	{
		string sSeverity[6]{ "Critical", "Error", "Warning", "Info", "Debug", "Verbose" };
		string sql = "INSERT INTO AppLog VALUES (";
		char *zErrMsg = 0;

		struct timeval tv;
		gettimeofday(&tv, nullptr);
		
		string msg;
		msg.assign(m_buf.mText + 1);
		char Severity = m_buf.mText[0];
		if (Severity > 6)
			Severity = 6;
		if (Severity < 1)
			Severity = 1;

		// cout << getDateTime(tv.tv_sec, tv.tv_usec) << " '" << GetServiceTitle(m_MsgChn) << "' [" << to_string(m_buf.mText[0]) << "] "
		//	<< msg << " at " << getDateTime(m_MsgTS_sec, m_MsgTS_usec) << endl;

		sql.append(to_string(tv.tv_sec) + ", " + to_string(tv.tv_usec) + ", '");
		sql.append(GetServiceTitle(m_MsgChn) + "', ");
		sql.append(to_string(m_MsgChn) + ", ");
		sql.append(to_string(Severity) + ", '");
		sql.append(msg);
		sql.append("', " + to_string(m_MsgTS_sec) + ", " + to_string(m_MsgTS_usec) + ");");
		cout << sql << endl;

		if (sqlite3_exec(m_Logdb, sql.c_str(), nullptr, 0, &zErrMsg) != SQLITE_OK)
		{
			fprintf(stderr, "Cannot insert new log into AppLog table with error: %s\n", zErrMsg);
			sqlite3_free(zErrMsg);
			return false;
		}
		return true;
	}

	string getDateTime(time_t tv_sec, time_t tv_usec)
	{
		struct tm *nowtm;
		char tmbuf[64], buf[64];

		nowtm = localtime(&tv_sec);
		strftime(tmbuf, sizeof tmbuf, "%Y-%m-%d %H:%M:%S", nowtm);
		snprintf(buf, sizeof buf, "%s.%06ld", tmbuf, tv_usec);
		return buf;
	}
};


int main(int argc, char *argv[])
{
	string msg;
	string tmp;
	string currentDateTime;

	string serviceTitles[5] { "MAIN", "GPS", "Radar", "Trigger", "FrontCam" };
	char serviceChannels[5] { 1,3,4,5,19 };
	string sSeverity[6] { "Critical", "Error", "Warning", "Info", "Debug", "Verbose"};

	char *bufMsg;
	size_t typeMsg;
	size_t len = 0;
	size_t chn = 0;
	size_t offset{ 0 };
	pid_t pid;
	pid_t ppid;
	string sTitle;
	char LogSeverity{ 4 };
	char Severity;
	string logContent;
	struct timeval tv;

	int PreEvent{ 120 };
	int PreEvent2{ 45 };
	int Chunk{ 30 };
	string CamPath{ "rtsp://10.25.20.0/1/h264major" };
	string User{ "Mark Richman" };
	string Password("noPassword");
	string CloudServer{ "50.24.54.54" };
	int WAP{ 1 };
	string Luanguage{ "English" };
	string ActiveTriggers{ "FLB SRN MIC LSB RLB" };
	int AutoUpload{ 0 };
	size_t totalReceived{ 0 };
	size_t totalSent{ 0 };

	MainModule *launcher = new MainModule();
	if (launcher->m_err)
	{
		fprintf(stderr, "Cannot open the log database.\n");
		return 0;
	}

	if (!launcher->StartService())
	{
		cerr << endl << "Cannot launch the headquater." << endl;
		return -1;
	}
	cout << endl << "Main module starts. Waiting for clients to join...." << endl;

	//launcher->SndCmd("LogSeverityLevel=3", "1");

	launcher->LocalMap("PreEvent", &PreEvent);
	launcher->LocalMap("PreExent", &PreEvent2);
	launcher->LocalMap("Chunk", &Chunk);
	launcher->LocalMap("CamPath", &CamPath);
	launcher->LocalMap("User", &User);
	launcher->LocalMap("PassWord", &Password);
	launcher->LocalMap("Cloud Server", &CloudServer, 0);
	launcher->LocalMap("WAP", &WAP, 4);
	launcher->LocalMap("Luanguage", &Luanguage, 0);
	launcher->LocalMap("Active Triggers", &ActiveTriggers, 0);
	launcher->LocalMap("Auto upload", &AutoUpload);
//	launcher->LocalMap("SeverityLevel", &LogSeverity, 1);

	// Add every service to the service list
	for (int i = 0; i < 5; i++)
		launcher->AddAService(serviceTitles[i], serviceChannels[i]);

	launcher->AddDBElement("PreEvent", 19);
	launcher->AddDBElement("CamPath", 19);
	launcher->AddDBElement("Active upload", 3);
	launcher->AddDBElement("Launguage", 4);

	launcher->AddDBElement("WAP", 5);

	// These settings will take effect only after ChkNewMsg
	launcher->SndCmd("LogSeverityLevel=Information", "1");
	launcher->SndCmd("AutoWatchdog=off", "1");
	launcher->SndCmd("AutoUpdate=on", "1");

	// Simulate the main module/head working
	while (1)
	{
		// check the watch dog
		chn = launcher->CheckWatchdog();
		if (chn)
		{
			cout << launcher->getDateTime(tv.tv_sec, tv.tv_usec) 
				<< " : Watchdog warning. '" << launcher->GetServiceTitle(chn) 
				<< "' stops responding on channel " << chn << endl;
			continue;
		}

		// check if there is any new message sent to main module. These messages are not auto processed by the library.
		typeMsg = launcher->ChkNewMsg();
		if (!typeMsg)
			continue;
	}
}
