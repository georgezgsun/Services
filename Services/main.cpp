#include <ctime>
#include <chrono>
#include <iostream>
#include <unistd.h>
#include <sys/time.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <string.h>
#include <sqlite3.h>
#include <pthread.h>
#include "ServiceUtils.h"

using namespace std;

struct thread_data
{
	sqlite3 * logDB;
	string * sql;
};

void *LogInDB(void * threadArg)
{
	struct thread_data *my_data;
	my_data = static_cast<struct thread_data *>(threadArg);
	char *zErrMsg = 0;
	if (sqlite3_exec(my_data->logDB, my_data->sql->c_str(), nullptr, 0, &zErrMsg) != SQLITE_OK)
	{
		fprintf(stderr, "Cannot insert new log from MAIN into AppLog table with error: %s\n", zErrMsg);
		sqlite3_free(zErrMsg);
	}
	delete static_cast<thread_data*>(threadArg)->sql;
	pthread_exit(NULL);
}

class MainModule : public ServiceUtils
{
public:

	MainModule()
	{
		m_AutoSleep = true;  // Auto sleep is also enabled in main
		m_AutoUpdate = false; // No auto update for service data in main. The m_ServiceData is reused by service list
		m_AutoWatchdog = false; // No auto watchdog feed at main
	}

	~MainModule()
	{
		sqlite3_close(m_Logdb);
	}

	// used to store the index of database elements for each sub module 
	size_t SubIndex[32][127];

	size_t m_TotalDBElements[127];

	sqlite3 *m_Logdb;
	sqlite3 *m_Confdb;
	sqlite3_stmt *m_log;
	char m_ActionWatchdog[255];
	long m_Pids[255];
	char m_LogSeverityLevel[255];
	string m_ModulePath[255];
	string m_ConfTable[255];
	sqlite3_stmt *m_ConfStatement[255];

	// Main module startup
	bool StartService()
	{
		if (m_ID < 0 || m_Chn != 1)
		{
			m_err = -1;
			fprintf(stderr, "This is for main module startup only.\n");
			return false;
		}

		// connect the logs database
		int rst = sqlite3_open("/home/georges/projects/Services/logs.db", &m_Logdb);
		if (rst)
		{
			m_err = -2;
			fprintf(stderr, "Failed to open the log database at /home/georges/projects/Services/logs.db with error: %s\n", sqlite3_errmsg(m_Logdb));
			return false;
		}

		// connect the configure database
		rst = sqlite3_open("/home/georges/projects/Services/Roswell.db", &m_Confdb);
		if (rst)
		{
			m_err = -3;
			fprintf(stderr, "Failed to open the Roswell configure database at "
				"/home/georges/projects/Services/Roswell.db with error: %s\n", sqlite3_errmsg(m_Confdb));
			return false;
		}

		m_err = 0;

		// prepare statement of log insert
		string sql = "INSERT INTO AppLog VALUES ( ?, ?, ?, ?, ?, ?, ?, ? );";
		m_err = sqlite3_prepare_v2(m_Logdb, sql.c_str(), -1, &m_log, 0);
		if (m_err)
			fprintf(stderr, "Failed to execute statement %s with error: %s\n", sql, sqlite3_errmsg(m_Logdb));

		// query the startup table in configure database
		sql = "SELECT * FROM STARTUP;";
		m_err = sqlite3_prepare_v2(m_Confdb, sql.c_str(), sql.length(), &m_ConfStatement[0], 0);
		if (m_err)
		{
			fprintf(stderr, "Failed to prepare the query of startup table with error:%s.\n", sqlite3_errmsg(m_Confdb));
			m_err = -4;
			return false;
		}

		// Read off all previous messages in the queue at main module starts
		int count = 0;
		while (msgrcv(m_ID, &m_buf, sizeof(m_buf), 0, IPC_NOWAIT) > 0)
			count++;

		// For debug only
		Log("Main module starts. There are " + to_string(count) + " stale messages in the message queue.");

		int ID;
		string ConfTable;
		string tmp;

		// assign main module manually here
		m_TotalProperties = 0;
		m_ServiceDataLength = 0;
		m_ServiceTitles[m_TotalProperties] = "MAIN";
		m_ServiceChannels[m_TotalProperties] = 1;
		m_ModulePath[m_TotalProperties] = "";
		m_ConfTable[m_TotalProperties] = "";
		m_ActionWatchdog[m_TotalProperties] = 2;
		m_LogSeverityLevel[m_TotalProperties] = 4;
		m_ServiceData[m_ServiceDataLength++] = m_ServiceChannels[m_TotalServices] & 0xFF;
		strcpy(m_ServiceData + m_ServiceDataLength, m_ServiceTitles[m_TotalServices].c_str());
		m_ServiceDataLength += m_ServiceTitles[m_TotalServices].length() + 1;
		m_TotalProperties++;

		// Loop to query all other modules in startup table
		do
		{
			// query each row
			// ID, Title, Channel, Path, Configure, Watchdog, Severity
			m_err = sqlite3_step(m_ConfStatement[0]);
			// keep query until there is nothing left
			if (m_err != SQLITE_ROW)
				break;

			count = sqlite3_column_count(m_ConfStatement[0]);
			if ( count < 7)
			{
				m_err = -5;
				fprintf(stderr, "The startup table has not adequate clumns.\n");
				return false;
			}

			// parse each row in startup table
			m_TotalServices = sqlite3_column_int(m_ConfStatement[0], 0);  // The ID
			m_ServiceTitles[m_TotalServices].assign((const char*)sqlite3_column_text(m_ConfStatement[0], 1)); // The service title
			m_ServiceChannels[m_TotalServices] = sqlite3_column_int(m_ConfStatement[0], 2);  // The channel
			m_ModulePath[m_TotalServices].assign((const char*)sqlite3_column_text(m_ConfStatement[0], 3));  // The module path
			m_ConfTable[m_TotalServices].assign((const char*)sqlite3_column_text(m_ConfStatement[0], 4));  // The config table
			tmp.assign((const char*)sqlite3_column_text(m_ConfStatement[0], 5)); // The watchdog reload, restart, or reboot
			m_ActionWatchdog[m_TotalServices] = 0;
			if (!tmp.compare("restart"))
				m_ActionWatchdog[m_TotalServices] = 1;
			if (!tmp.compare("reboot"))
				m_ActionWatchdog[m_TotalServices] = 2;

			tmp.assign((const char*)sqlite3_column_text(m_ConfStatement[0], 6)); //  The log severity level Info, Debug, Verbose
			m_LogSeverityLevel[m_TotalServices] = 4;
			if (!tmp.compare("Debug"))
				m_LogSeverityLevel[m_TotalServices] = 5;
			if (!tmp.compare("Verbose"))
				m_LogSeverityLevel[m_TotalServices] = 6;

			// prepare the service list
			m_ServiceData[m_ServiceDataLength++] = m_ServiceChannels[m_TotalServices] & 0xFF;
			strcpy(m_ServiceData + m_ServiceDataLength, m_ServiceTitles[m_TotalServices].c_str());
			m_ServiceDataLength += m_ServiceTitles[m_TotalServices].length() + 1;
			
			// prepare the query of configure table
			int offset = m_ConfTable[m_TotalServices].find_first_of(':'); // position of :
			ConfTable = m_ConfTable[m_TotalServices].substr(0, offset); // table title is the left of the :
			ID = atoi(m_ConfTable[m_TotalServices].substr(offset + 1).c_str()); // ID number is the right of :
			if (!ID)
				ID = 1;
			string Statement("SELECT * FROM ");
			Statement.append(ConfTable);
			Statement.append(" WHERE ID=" + to_string(ID) + ";");
			m_err = sqlite3_prepare_v2(m_Confdb, Statement.c_str(), Statement.length(), &m_ConfStatement[m_TotalServices], 0);
			if (m_err)
			{
				fprintf(stderr, "Failed to prepare the query of configure table %s"
					" with error:%s.\n", Statement.c_str(), sqlite3_errmsg(m_Confdb));
				return false;
			}

			// update the total service number
			m_TotalServices++;
		} while (true);

		// 
		return true;
	}

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

		size_t i;
		string tmp;
		
		// find the index of the sevice channel
		for (i = 0; i < m_TotalServices; i++)
			if (m_ServiceChannels[i] == m_MsgChn)
				break;
		if (i >= m_TotalServices)
			return false;  // no such a service channel in the list

		// query the configure table
		m_err = sqlite3_reset(m_ConfStatement[i]);
		m_err = sqlite3_step(m_ConfStatement[i]);
		if (m_err != SQLITE_ROW)
		{
			fprintf(stderr, "Failed to query config table of %s with error:%s.\n", GetServiceTitle(m_MsgChn).c_str(), sqlite3_errmsg(m_Confdb));
			m_err = -6;
			return false;
		}
		
		int n;
		double t;
		size_t offset = 0;
		int count = sqlite3_column_count(m_ConfStatement[i]); // find the total column number other than ID

		// gets each column and put them into the database query reply
		// There is a risk that offset may exceed 255 in case too many elements or too long the content.
		for (size_t j = 1; j < count; j++)
		{
			// assign the column name to be the keyword
			tmp.assign((const char*)sqlite3_column_name(m_ConfStatement[i], j));
			strcpy(m_buf.mText + offset, tmp.c_str());
			offset += tmp.length() + 1;

			switch (sqlite3_column_type(m_ConfStatement[i], j))
			{
			case SQLITE_INTEGER:
				m_buf.mText[offset++] = sizeof(int);
				n = sqlite3_column_int(m_ConfStatement[i], j);
				memcpy(m_buf.mText + offset, &n, sizeof(int));
				offset += sizeof(int);
				break;

			case SQLITE_FLOAT:
				m_buf.mText[offset++] = sizeof(double);
				t = sqlite3_column_double(m_ConfStatement[i], j);
				memcpy(m_buf.mText + offset, &t, sizeof(double));
				offset += sizeof(double);
				break;

			case SQLITE_TEXT:
				m_buf.mText[offset++] = 0;
				tmp.assign((const char *) (sqlite3_column_text(m_ConfStatement[i], j)));
				strcpy(m_buf.mText + offset, tmp.c_str());
				offset += tmp.length() + 1;
				break;

			case SQLITE_BLOB:
				n = sqlite3_column_bytes(m_ConfStatement[i], j);
				m_buf.mText[offset++] = n;
				memcpy(m_buf.mText + offset, sqlite3_column_blob(m_ConfStatement[i], j), n);
				offset += n;
				break;

			default:
				m_err = -6;
				fprintf(stderr, "Unaware type of column queried at %s\n.", m_pptr[m_TotalProperties]->keyword.c_str());
				return false;
			}
		}

		if (offset > 255)
		{
			m_err = -7;
			Log("Critical error when query the configuration table of " + GetServiceTitle(m_MsgChn) + ". Total bytes %d grows too many.\n", 1);
			return false;
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

		// Check the watchdog only in the second half of a second
		if (tv.tv_usec < 500000)
			return 0;

		// m_Clients stores all onboard services
		for (size_t i = 0; i < m_TotalClients; i++)
		{
			size_t index = m_Clients[i];
			if (m_WatchdogTimer[index] && tv.tv_sec >= m_WatchdogTimer[index] + 5)
			{
				m_WatchdogTimer[index] = 0;
				m_MsgTS_sec = tv.tv_sec;
				m_MsgTS_usec = tv.tv_usec;
				Log("Watchdog aleart. " + GetServiceTitle(index) + " stops responding on channel " + to_string(index));
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
			// local log
			msg = GetServiceTitle(m_MsgChn);
			Log("A new service " + msg + " gets onboard at channel " + to_string(m_MsgChn));

			// update onboard list which is stored at m_Clients[]
			for (i = 0; i < m_TotalClients; i++)
			{ 
				if (m_Clients[i] == m_MsgChn)
					break;
			}
			if (i >= m_TotalClients)
				m_Clients[m_TotalClients++] = m_MsgChn;

			// get the module pid and save it in m_Subscriptions[], reuse the storage
			memcpy(&pid, m_buf.mText, sizeof(pid));
			offset = sizeof(pid);
			memcpy(&ppid, m_buf.mText + offset, sizeof(ppid));
			m_Pids[i] = pid;

			// reply service list to sub-module next
			ReplyServiceList();
			Log("Replies the service list to channel " + to_string(m_MsgChn));

			// reply databse query first
			ReplyDBQuery();
			Log("Replies the configuration queried from database to channel " + to_string(m_MsgChn));

			// reply the log severity level at the end
			//TODO get the severity from database
			SndCmd("LogSeverityLevel=4", msg);

			// local log
			Log("Send the command LogSeverityLevel=4 to " + msg);
			return type;

		case CMD_DATABASEUPDATE:
			msg = GetServiceTitle(m_MsgChn);
			ParseDBUpdate();
			Log("Service " + msg + " updates its database elements.");
			return type;

		case CMD_LOG:
			Log();
			return type;

		case CMD_DATABASEQUERY:
			msg = GetServiceTitle(m_MsgChn);
			ReplyDBQuery();
			Log("Service " + msg + " requests database query.", 5);
			return type;

		case CMD_WATCHDOG:
			msg = GetServiceTitle(m_MsgChn);
			Log("Service " + msg + " feed its watchdog.", 5);
			return type;

		case CMD_STRING:
			msg = GetServiceTitle(m_MsgChn);
			Log("Service " + msg + "sends a message " + GetRcvMsg() + " to the main. ");
			return type;

		case CMD_DOWN:
			msg = GetServiceTitle(m_MsgChn);
			Log("Service " + msg + " reports down. Informs every sub modules.");

			// broadcast the message that one channel has down
			InformDown();
			return type;

		// Auto parse the system configurations
		case CMD_COMMAND:
			msg.assign(m_buf.mText);
			Log("Get a command: " + msg + " from " + GetServiceTitle(m_MsgChn));
			offset = msg.find_first_of('=');  // find =
			keyword = msg.substr(0, offset);  // keyword is left of =
			msg = msg.substr(offset + 1); // now msg is right of =

			// Update the log severity level
			if (!keyword.compare("LogSeverityLevel"))
			{
				int i = atoi(msg.c_str());
				if (i > 0 && i < 7)
					m_Severity = i;

				if (!msg.compare(sSeverity[3]))
					m_Severity = 4;

				if (!msg.compare(sSeverity[4]))
					m_Severity = 5;

				if (!msg.compare(sSeverity[5]))
					m_Severity = 6;
				return m_buf.type;
			}

			// the flag update of auto service data update
			if (!keyword.compare("AutoUpdate"))
			{
				if (!msg.compare("on"))
					m_AutoUpdate = true;
				if (!msg.compare("off"))
					m_AutoUpdate = false;
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
		return type;
	}

	// print the log from local with given severity
	bool Log(string msg, char Severity = 4)
	{
		if (Severity > m_Severity)
			return false;

		string sSeverity[6]{ "Critical", "Error", "Warning", "Info", "Debug", "Verbose" };
		string sql = "INSERT INTO AppLog VALUES (";

		struct timeval tv;
		gettimeofday(&tv, nullptr);

		//int rst = sqlite3_bind_int(m_log, 1, tv.tv_sec);
		//rst += sqlite3_bind_int(m_log, 2, tv.tv_usec);
		//rst += sqlite3_bind_text(m_log, 3, "MAIN", -1, SQLITE_STATIC);
		//rst += sqlite3_bind_int(m_log, 4, 1);
		//rst += sqlite3_bind_int(m_log, 5, Severity);
		//rst += sqlite3_bind_text(m_log, 6, msg.c_str(), -1, SQLITE_STATIC);
		//rst += sqlite3_bind_int(m_log, 7, m_MsgTS_sec);
		//rst += sqlite3_bind_int(m_log, 8, tv.tv_usec > m_MsgChn ? tv.tv_usec - m_MsgTS_usec : 1000000L + tv.tv_usec - m_MsgTS_usec);

		//if (rst != SQLITE_OK)
		//{
		//	fprintf(stderr, "Error while bind parameters %d.\n", rst);
		//	return false;
		//}

		//rst = sqlite3_step(m_log);

		//if (rst != SQLITE_DONE)
		//{
		//	fprintf(stderr, "Cannot insert %s into the log database:%s\n", msg.c_str(), sqlite3_errmsg(m_Logdb));
		//	return false;
		//}
		//rst = sqlite3_clear_bindings(m_log);
		//return true;

		sql.append(to_string(tv.tv_sec) + ", " + to_string(tv.tv_usec) + ", '");
		sql.append(GetServiceTitle(m_MsgChn) + "', ");
		sql.append(to_string(m_MsgChn) + ", ");
		sql.append(to_string(Severity) + ", '");
		sql.append(msg + "', ");
		tv.tv_usec = tv.tv_usec > m_MsgChn ? tv.tv_usec - m_MsgTS_usec : 1000000L + tv.tv_usec - m_MsgTS_usec;
		sql.append(to_string(m_MsgTS_sec) + ", " + to_string(tv.tv_usec) + "); ");

		pthread_t thread;
		struct thread_data *threadata = new thread_data;
		threadata->logDB = m_Logdb;
		threadata->sql = new string(sql);

		int rst = pthread_create(&thread, NULL, LogInDB, (void *)threadata);
		if (rst)
		{
			printf("Cannot create thread. %d\n", rst);
			return false;
		}
		return true;
	}
	
	// print the log from service module
	bool Log()
	{
		string sSeverity[6]{ "Critical", "Error", "Warning", "Info", "Debug", "Verbose" };
		string sql = "INSERT INTO AppLog VALUES ( ";
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

		sql.append(to_string(tv.tv_sec) + ", " + to_string(tv.tv_usec) + ", '");
		sql.append(GetServiceTitle(m_MsgChn) + "', ");
		sql.append(to_string(m_MsgChn) + ", ");
		sql.append(to_string(Severity) + ", '");
		sql.append(msg + "', ");
		tv.tv_usec = tv.tv_usec > m_MsgChn ? tv.tv_usec - m_MsgTS_usec : 1000000L + tv.tv_usec - m_MsgTS_usec;
		sql.append(to_string(m_MsgTS_sec) + ", " + to_string(tv.tv_usec) + "); ");
		//cout << sql << endl;

		pthread_t thread;
		struct thread_data *threadata = new thread_data;
		threadata->logDB = m_Logdb;
		threadata->sql = new string(sql);

		int rst = pthread_create(&thread, NULL, LogInDB, (void *)threadata);
		if (rst)
		{
			printf("Cannot create thread. %d\n", rst);
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

	launcher->SndCmd("LogSeverityLevel=Debug", "MAIN");
	launcher->SndCmd("AutoWatchdog=off", "MAIN");
	launcher->SndCmd("AutoUpdate=on", "MAIN");

	// Simulate the main module/head working
	while (1)
	{
		// check the watch dog
		size_t chn = launcher->CheckWatchdog();
		if (chn)
		{
			cout << "Watchdog warning. '" << launcher->GetServiceTitle(chn) 
				<< "' stops responding on channel " << chn << endl;
			continue;
		}

		// check if there is any new message sent to main module. These messages are not auto processed by the library.
		size_t type = launcher->ChkNewMsg();
		if (!type)
			continue;

		if (type == CMD_COMMAND)
		{
			msg = launcher->GetRcvMsg();
			cout << "Get a command " << msg << " from " << launcher->GetServiceTitle(launcher->m_MsgChn) << endl;
		}
	}
}
