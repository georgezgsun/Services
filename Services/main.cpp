#include <ctime>
#include <chrono>
#include <iostream>
#include <fstream>
#include <unistd.h>
#include <sys/time.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/reboot.h>
#include <string.h>
#include <sqlite3.h>
#include <pthread.h>
#include <signal.h>
#include <functional>
#include <future>

#include "ServiceUtils.h"

using namespace std;

string LogPath;  // the path of the log database that used in the log thread
string Titles[255];  // The service title that used in the log thread

void *LogInsert(void * my_void_ptr)
{
	key_t t_Key = getpid();
	int t_ID = msgget(t_Key, 0444 | IPC_CREAT);  // message queue for read-only
	long t_Chn = 2;
	struct MsgBuf t_buf;

	sqlite3_stmt * stmt;
	sqlite3 *db; // = static_cast<sqlite3 *>(my_void_ptr);

	// connect the logs database
	int rst = sqlite3_open(LogPath.c_str(), &db);
	if (rst)
	{
		fprintf(stderr, "Failed to open the log database at %s with error: %s\n",
			LogPath.c_str(), sqlite3_errmsg(db));
		pthread_exit(NULL);
	}
	fprintf(stderr, "Open %s for the log database.\n", LogPath.c_str());

	string sql = "CREATE TABLE IF NOT EXISTS 'Roswell' ("
		"`ID`	INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT,"
		"`Date Time`	TEXT NOT NULL,"
		"`Service`	TEXT NOT NULL,"
		"`Error Code`	INTEGER NOT NULL,"
		"`Content`	TEXT,"
		"`Pid`	INTEGER	);";
	rst = sqlite3_prepare_v2(db, sql.c_str(), sql.length(), &stmt, 0);
	if (rst)
	{
		fprintf(stderr, "Failed to prepare the statement %s with error: %s\n", sql.c_str(), sqlite3_errmsg(db));
		pthread_exit(NULL);
	}

	rst = sqlite3_step(stmt);
	if (rst)
		fprintf(stderr, "The Roswell table has already been created in %s. \n", LogPath.c_str());
	else
		fprintf(stderr, "A new table has been created using %s in %s.\n", sql.c_str(), LogPath.c_str());
	rst = sqlite3_finalize(stmt);

	// prepare statement of log insert
	sql = "INSERT INTO Roswell VALUES ( null, ?, ?, ?, ?, ? );";
	rst = sqlite3_prepare_v2(db, sql.c_str(), sql.length(), &stmt, 0);
	if (rst != SQLITE_OK)
		fprintf(stderr, "Failed to prepare statement %s with error: %s\n", sql.c_str(), sqlite3_errmsg(db));

	while (true)
	{
		int len = msgrcv(t_ID, &t_buf, sizeof(t_buf), 2, 0); // this is a blocking read
		if (len <= 0)
			continue;

		if (t_buf.type == CMD_LIST)
		{
			size_t offset = 0;
			int TotalServices = 0;
			char chn;

			// Get the service list from the main thread
			do
			{
				chn = t_buf.mText[offset++];
				Titles[chn].assign(t_buf.mText + offset);
				offset += Titles[chn].length() + 1; // increase the m_TotalServices, update the offset to next
				TotalServices++;
			} while (offset < t_buf.len);
		}
		else if (t_buf.type == CMD_STRING)
		{
			// Get the log file path from the main thread
			LogPath.assign(t_buf.mText);
			fprintf(stderr, "(LOG) Get new log path as %s.\n", LogPath.c_str());
		}

		if (t_buf.type != CMD_LOG)
			continue;

		struct timeval tv;
		gettimeofday(&tv, nullptr);

		int dt = (tv.tv_sec - t_buf.sec) * 1000000 + tv.tv_usec - t_buf.usec;
		int ErrorCode;
		size_t offset;
		memcpy(&ErrorCode, t_buf.mText, sizeof(ErrorCode));
		offset = sizeof(ErrorCode);
		string msg(t_buf.mText + offset);

		string date = getDateTime(t_buf.sec, t_buf.usec);
		rst = sqlite3_bind_text(stmt, 1, date.c_str(), date.length(), SQLITE_STATIC);
		if (rst != SQLITE_OK)
			fprintf(stderr, "Failed to bind date time %s into statement %s with error: %s\n", date.c_str(), sql.c_str(), sqlite3_errmsg(db));
		rst = sqlite3_bind_text(stmt, 2, Titles[t_buf.sChn].c_str(), Titles[t_buf.sChn].length(), SQLITE_STATIC); // The service title of that channel. TODO get the service list
		if (rst != SQLITE_OK)
			fprintf(stderr, "Failed to bind service title %s into statement %s with error: %s\n", Titles[t_buf.sChn].c_str(), sql.c_str(), sqlite3_errmsg(db));
		rst = sqlite3_bind_int(stmt, 3, ErrorCode);
		if (rst != SQLITE_OK)
			fprintf(stderr, "Failed to bind Error Code %d into statement %s with error: %s\n", ErrorCode, sql.c_str(), sqlite3_errmsg(db));
		rst = sqlite3_bind_text(stmt, 4, msg.c_str(), msg.length(), SQLITE_STATIC);
		if (rst != SQLITE_OK)
			fprintf(stderr, "Failed to bind log content %s into statement %s with error: %s\n", msg.c_str(), sql.c_str(), sqlite3_errmsg(db));
		rst = sqlite3_bind_int(stmt, 5, dt);
		if (rst != SQLITE_OK)
			fprintf(stderr, "Failed to bind tid %d into statement %s with error: %s\n", dt, sql.c_str(), sqlite3_errmsg(db));

		rst = sqlite3_step(stmt);
		if (rst != SQLITE_DONE)
			fprintf(stderr, "Failed to execute statement %s with error: %s\n", sql.c_str(), sqlite3_errmsg(db));

		rst = sqlite3_reset(stmt);  // this is required before next bind
	};
}

class MainModule : public ServiceUtils
{
public:

	MainModule()
	{

	}

	~MainModule()
	{
		sqlite3_close(m_ConfDB);
	}

	sqlite3 *m_ConfDB;
	sqlite3 *m_LogDB;
	char m_ActionWatchdog[255];
	pid_t m_Pids[255];
	int m_LogSeverityLevel[255];
	string m_ModulePath[255];
	string m_ConfTable[255];
	string m_Path;
	string m_ConfPath;
	sqlite3_stmt *m_ConfStatement[255];

	// Main module startup
	bool StartService(int argc, char *argv[])
	{
		m_Path.assign(argv[0]);
		int pos = m_Path.find_last_of('/') + 1;  // include / at the end
		m_Path = m_Path.substr(0, pos);
		LogPath = m_Path + "StalkerLog.db";
		m_ConfPath = m_Path + "Roswell.db";

		if (m_ID < 0 || m_Chn != 1)
		{
			m_err = -1;
			fprintf(stderr, "This is for main module startup only.\n");
			return false;
		}

		if (argc > 1)
			m_ConfPath = argv[1];
		if (argc > 2)
			LogPath = argv[2];

		// connect the configure database
		int rst = sqlite3_open(m_ConfPath.c_str(), &m_ConfDB);
		if (rst)
		{
			m_err = -3;
			fprintf(stderr, "Failed to open the Roswell configure database at %s with error: %s\n",
				m_ConfPath.c_str(), sqlite3_errmsg(m_ConfDB));
			return false;
		}
		m_err = 0;

		// query the startup table in configure database
		string sql = "SELECT * FROM STARTUP;";
		m_err = sqlite3_prepare_v2(m_ConfDB, sql.c_str(), sql.length(), &m_ConfStatement[0], 0); // the main module config query statement
		if (m_err)
		{
			fprintf(stderr, "Failed to prepare the query of startup table with error:%s.\n", sqlite3_errmsg(m_ConfDB));
			m_err = -4;
			return false;
		}

		// Read off all previous messages in the queue at main module starts
		int count = 0;
		while (msgrcv(m_ID, &m_buf, sizeof(m_buf), 0, IPC_NOWAIT) > 0)
			count++;

		// For debug only
		//Log("Main module starts. There are " + to_string(count) + " stale messages in the message queue.");
		fprintf(stderr, "The configure database is %s, the log database is %s.\n",
			m_ConfPath.c_str(), LogPath.c_str());

		int ID;
		string ConfTable;
		string tmp;
		const unsigned char *read;

		// assign main module manually here
		int Chn = 1;
		m_TotalProperties = 0;
		m_TotalServices = 0;
		m_ServiceDataLength = 0;
		m_ServiceTitles[m_TotalServices] = "MAIN";
		m_ServiceChannels[m_TotalServices] = 1;
		m_ModulePath[m_TotalServices] = argv[0];
		m_ServiceData[m_ServiceDataLength++] = 1;
		strcpy(m_ServiceData + m_ServiceDataLength, m_ServiceTitles[m_TotalServices].c_str());
		m_ServiceDataLength += m_ServiceTitles[m_TotalServices].length() + 1;
		m_Pids[m_TotalServices] = getpid();
		m_TotalServices++;

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
			if (count < 7)
			{
				m_err = -5;
				fprintf(stderr, "The startup table has not adequate clumns.\n");
				return false;
			}

			// parse each column in startup table
			ID = sqlite3_column_int(m_ConfStatement[0], 0);  // The ID
			Chn = sqlite3_column_int(m_ConfStatement[0], 2);  // The channel
			m_ServiceTitles[Chn].assign((const char*)sqlite3_column_text(m_ConfStatement[0], 1)); // The service title
			m_ServiceChannels[Chn] = Chn;
			read = sqlite3_column_text(m_ConfStatement[0], 3);
			if (read)
				m_ModulePath[Chn].assign((const char*)read);  // The module path
			else
				m_ModulePath[Chn].clear(); // in case it is null
			read = sqlite3_column_text(m_ConfStatement[0], 4);
			if (read)
				m_ConfTable[Chn].assign((const char*)read);  // The config table
			else
				m_ConfTable[Chn].clear();  // in case it is null
			read = sqlite3_column_text(m_ConfStatement[0], 5); // The watchdog reload, restart, or reboot
			m_ActionWatchdog[Chn] = 0;
			if (read)
			{
				if (!strcmp((const char*)read, "reload"))
					m_ActionWatchdog[Chn] = 1;
				else if (!strcmp((const char*)read, "restart"))
					m_ActionWatchdog[Chn] = 2;
				else if (!strcmp((const char*)read, "reboot"))
					m_ActionWatchdog[Chn] = 3;
			}

			read = sqlite3_column_text(m_ConfStatement[0], 6); //  The log severity level Info, Debug, Verbose
			m_LogSeverityLevel[Chn] = 2000;
			if (read)
			{
				if (!strcmp((const char*)read, "Debug"))
					m_LogSeverityLevel[Chn] = 3000;
				else if (!strcmp((const char*)read, "Verbose"))
					m_LogSeverityLevel[Chn] = 4000;
			}

			// No further parse on main module
			if (Chn == 1)
				continue;

			// prepare the service list
			m_ServiceData[m_ServiceDataLength++] = Chn & 0xFF;
			strcpy(m_ServiceData + m_ServiceDataLength, m_ServiceTitles[Chn].c_str());
			m_ServiceDataLength += m_ServiceTitles[Chn].length() + 1;

			// prepare the query of configure table
			string Statement("SELECT * FROM ");
			size_t offset = m_ConfTable[Chn].find_first_of(':'); // position of :
			ConfTable = m_ConfTable[Chn].substr(0, offset); // table title is the left of the :
			Statement.append(ConfTable);
			if (offset != string::npos && atoi(m_ConfTable[Chn].substr(offset + 1).c_str()) !=0)
				Statement.append(" WHERE ID=" + m_ConfTable[Chn].substr(offset + 1)); // ID number is the right of : , 0 or other digital means all

			Statement.append(";");
			m_err = sqlite3_prepare_v2(m_ConfDB, Statement.c_str(), Statement.length(), &m_ConfStatement[Chn], 0);
			if (m_err)
			{
				fprintf(stderr, "Failed to prepare the query of configure table %s"
					" with error:%s.\n", Statement.c_str(), sqlite3_errmsg(m_ConfDB));
				return false;
			}

			// update the total service number
			m_TotalServices++;

			// assign the service title so that log thread may use it
			Titles[Chn] = m_ServiceTitles[Chn];
		} while (true);

		// Start the log thread
		pthread_t thread;
		//rst = pthread_create(&thread, NULL, LogInsert, (void *)LogPath.c_str());
		rst = pthread_create(&thread, NULL, LogInsert, nullptr);
		if (rst)
		{
			printf("Cannot create the log thread. %d\n", rst);
			return false;
		}

		//// send the service list to Log thread
		//SndMsg(m_ServiceData, CMD_LIST, m_ServiceDataLength, 2);

		//// send the key of the message queue to the pic
		//key_t key = 12345;
		//int p_ID = msgget(key, 0666 | IPC_CREAT);

		//m_buf.rChn = 1;
		//m_buf.sChn = getpid();  // This is the key
		//m_buf.len = 0;

		//if (msgsnd(p_ID, &m_buf, m_buf.len + m_HeaderLength, IPC_NOWAIT))
		//	printf("(Debug) Critical error. Unable to send the message to channel %d. Message is of length %ld, and header length %ld.\n", p_ID, m_buf.len, m_HeaderLength);
		//else 
		//	printf("(Debug) Send the key %ld to PIC tester via message %d.\n", m_buf.sChn, p_ID);

		return true;
	}

	bool RunService(size_t Channel)
	{
		// Channel 1 and 2 are reserved for main and log
		if (Channel < 3 || Channel > 255)
		{
			fprintf(stderr, "Cannot start the service. The channel specified %d is invalid.\n", Channel);
			Log("Cannot start the service. The channel specified " + to_string(Channel) + " is invalid", 120); // Error code 120.
			return false;
		}

		// Not run the service module in case there is no path assigned
		if (m_ModulePath[Channel].empty())
			return false;

		// Export the configure table as the configure file, where the path is /tmp/conf/<table name>.conf 
		ExportTable(m_ConfTable[Channel]);

		struct timeval tv;
		gettimeofday(&tv, nullptr);
		m_WatchdogTimer[Channel] = tv.tv_sec;  // update the watchdog timer. It will be restarted in case the luanching fails

		int p;
		bool rst{ true };
		string s_title = "title=" + m_ServiceTitles[Channel];
		string s_channel = "channel=" + to_string(Channel);

		pid_t pid = fork();
		if (pid == -1)
		{
			fprintf(stderr, "Cannot fork.\n");
			Log("Cannot fork when trying to start service at channel " + to_string(Channel), 12); // this is a critical error 12
			return false;
		}

		if (pid == 0) // This is child process
		{
			execl(m_ModulePath[Channel].c_str(), m_ModulePath[Channel].c_str(), s_channel.c_str(), s_title.c_str());
			fprintf(stderr, "Cannot start %s %s %s.\n", m_ModulePath[Channel].c_str(), s_channel.c_str(), s_title.c_str());
			Log("Cannot start " + m_ModulePath[Channel], 13);  // log the critical error with code 13
			return false;
		}

		// This is the parent process
		return true;
	}

	bool KillService(size_t Channel)
	{
		// Channel 1 and 2 are reserved for main and log
		if (Channel < 3 || Channel > 255)
			return false;

		// Not run the service in case it is not reload
		switch (m_ActionWatchdog[Channel])
		{
		case 1:  //reload
			break;

		case 2: //restart
			if (!m_ModulePath[1].empty())
				execl(m_ModulePath[1].c_str(), m_ModulePath[1].c_str()); // restart the main module
			fprintf(stderr, "Cannot restart the main module.\n");
			Log("Cannot restart the main module.", 11); // critical error code 11
			sleep(1);

		case 3:  // reboot
			Log("Reboot the whole system.", 1002);
			sleep(1);
			sync();
			reboot(RB_AUTOBOOT);
			exit(0);
			break;

		default:
			return false;
		}

		if (!m_Pids[Channel])  // check if onboard
			return false;

		// delete the service from active module list
		int i;
		for (i = 0; i < m_TotalClients; i++)
		{
			if (Channel == m_Clients[i])
			{
				for (int j = i; j < m_TotalClients - 1; j++)
					m_Clients[j] = m_Clients[j + 1];
				m_Clients[m_TotalClients - 1] = 0;
				break;
			}
		}
		if (i >= m_TotalClients)
		{
			fprintf(stderr, "Cannot find channel %ld in current running services list.\n", Channel);
			for (int j = 0; j < m_TotalClients; j++)
				fprintf(stderr, "Position %d: %d\n", j, m_Clients[j]);
			Log("Cannot find channel " + to_string(Channel) + " in current running services list.\n", 150); // an error with code 150.
			//return true;
		}
		m_TotalClients--;  // reduce the number of active modules here

		// try to determine if the pid is still running
		int rst = kill(m_Pids[Channel], 0);
		if (rst < 0)
		{
			fprintf(stderr, "The process of service '%s' has errno=%d. Shall kill it roughly.\n",
				m_ServiceTitles[Channel].c_str(), errno);
			rst = kill(m_Pids[Channel], SIGKILL);
		}
		else
			rst = kill(m_Pids[Channel], SIGTERM);
		m_Pids[Channel] = 0;

		if (rst < 0)
		{
			fprintf(stderr, "There is something wrong when killing service '%s' with errno=%d\n", m_ServiceTitles[Channel].c_str(), errno);
			Log("There is something wrong when killing service " + m_ServiceTitles[Channel], 200);  // an error with code 200
			return false;
		}

		// to inform every running service module that this service has down
		char buf[255];  //temporal buffer to store those onboard channels
		memset(buf, 0, sizeof(buf));  // clear the buffer
		m_buf.sChn = Channel;
		m_buf.len = 0;
		m_buf.type = CMD_DOWN;
		for (i = 0; i < m_TotalClients; i++)
		{
			ReSendMsgTo(m_Clients[i]);
			buf[m_Clients[i]] = 1;  // set that channel active
		}

		// Clean all the messages sent to those not onboard service
		int count;
		for (i = 3; i < 255; i++)
		{
			if (!buf[i])
			{
				count = 0;
				while (msgrcv(m_ID, &m_buf, sizeof(m_buf), i, IPC_NOWAIT) > 0)
				{
					count++;
					m_TotalMessageReceived++;
				}
				if (count)
					fprintf(stderr, "Channel %d has %d staled messages.\n", i, count);
			}
		}
		//fprintf(stderr, "%s is killed successfully. Read off %d staled messages.\n", m_ServiceTitles[Channel].c_str(), count);
		Log("The service " + m_ServiceTitles[Channel] + " is killed. Read off " + to_string(count) + " staled messages."); // This is an Info

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

		size_t i = m_MsgChn;
		string tmp;

		// query multiple rows in case need to transfer the whole table
		int ID{ 0 };
		const unsigned char *read;
		m_err = sqlite3_reset(m_ConfStatement[i]);  // reset the query statement only once
		do
		{
			// query the configure table
			m_err = sqlite3_step(m_ConfStatement[i]);
			if (m_err != SQLITE_ROW)
			{
				if (!ID) // if get no successful query before
				{
					fprintf(stderr, "Failed to query config table of %s with error:%s.\n", GetServiceTitle(m_MsgChn).c_str(), sqlite3_errmsg(m_ConfDB));
					m_err = -6;
					return false;
				}
				return true;
			}

			int n;
			double t;
			size_t offset = 0;
			int count = sqlite3_column_count(m_ConfStatement[i]); // find the total column number other than ID

			// gets each column and put them into the database query reply
			// There is a risk that offset may exceed 255 in case too many elements or too long the content.
			for (size_t j = 0; j < count; j++)
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
					if (!tmp.compare("ID"))
						ID = n;
					break;

				case SQLITE_FLOAT:
					m_buf.mText[offset++] = sizeof(double);
					t = sqlite3_column_double(m_ConfStatement[i], j);
					memcpy(m_buf.mText + offset, &t, sizeof(double));
					offset += sizeof(double);
					break;

				case SQLITE_TEXT:
					m_buf.mText[offset++] = 0;
					read = sqlite3_column_text(m_ConfStatement[i], j);
					if (read)
					{
						strcpy(m_buf.mText + offset, (const char*)read);
						offset += strlen((const char*)read) + 1;
					}
					else
						m_buf.mText[offset++] = 0; // for a NULL element, add /0 anyway
					break;

				case SQLITE_BLOB: // not sure for a length of 0?
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
				Log("Critical error when query the configuration table of " + GetServiceTitle(m_MsgChn) + ". Total bytes %d grows too many.\n", 1); // critical error with code 1
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
		} while (true);

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
		string sql = ""; 
		string ID = " WHERE ID=";
		sqlite3_stmt *stmt;

		// map the updated data from clients to local variables in main module
		size_t offset = 0;
		string keyword;
		int i;
		double t;
		string value;
		do
		{
			value.clear();
			keyword.assign(m_buf.mText + offset); // read the keyword
			offset += keyword.length() + 1; // there is a /0 in the end
			if (keyword.empty()) // end of assignment when keyword is empty
				break;

			char n = m_buf.mText[offset++]; // read data length, 0 represents for string
			if (n == sizeof(int))
			{
				memcpy(&i, m_buf.mText + offset, n);
				value = to_string(i);
				offset += n;
			}
			else if (n == sizeof(double))
			{
				memcpy(&t, m_buf.mText + offset, n);
				value = to_string(t);
				offset += n;
			}
			else if (n == 0)
			{
				value.assign(m_buf.mText + offset);
				offset += value.length() + 1;
			}

			if (!keyword.compare("ID"))
			{
				ID.append(value + ";");
				continue;
			}
			
			if (sql.empty())
				sql = "UPDATE " + m_ConfTable[m_MsgChn] + " SET ";
			else
				sql.append(", ");
			sql.append(keyword + "=" + value);		
		} while (offset < 255);

		// check if there is no update element
		if (sql.find('=') == std::string::npos)
		{
			m_err = 125;
			Log("There is no element in the database update request.", m_err);
			return;
		}

		// assemble the final sql string
		sql.append(ID);
		m_err = sqlite3_prepare_v2(m_ConfDB, sql.c_str(), sql.length(), &stmt, 0); // the main module config query statement
		if (m_err)
		{
			m_err = -390;
			Log("Cannot prepare the update statement: " + sql + " with error: " + sqlite3_errmsg(m_ConfDB), -m_err);
			return;
		}

		// execute the update statement
		m_err = sqlite3_step(stmt);
		if (m_err)
		{
			m_err = -391;
			Log("Cannot execute the update statement: " + sql + " with error: " + sqlite3_errmsg(m_ConfDB), -m_err);
		}
		else
			Log(sql);

		sqlite3_finalize(stmt);
		return;
	};

	// Export the given table to /tmp/conf folder
	bool ExportTable(string table)
	{
		// prepare the query of configure table
		string Statement("SELECT * FROM ");
		size_t offset = table.find_first_of(':'); // position of :
		Statement.append(table.substr(0, offset)); // table title is the left of the :
		if ((offset != string::npos) && atoi(table.substr(offset + 1).c_str()))
			Statement.append(" WHERE ID=" + table.substr(offset + 1)); // ID number is the right of : , 0 or other digital means all
		Statement.append(";");

		// query the startup table in configure database
		sqlite3_stmt * stmt;
		char split_char{ 9 }; // horizontal tab
		m_err = sqlite3_prepare_v2(m_ConfDB, Statement.c_str(), Statement.length(), &stmt, 0);
		if (m_err)
		{
			m_err = -293;
			Log("Cannot prepare " + Statement + " while export the table with error: " + sqlite3_errmsg(m_ConfDB), -m_err);
			fprintf(stderr, "Failed to prepare %s while export the table with error:%s.\n", Statement.c_str(), sqlite3_errmsg(m_ConfDB));
			return false;
		}

		m_err = sqlite3_step(stmt);
		if (m_err != SQLITE_ROW)
		{
			m_err = -294;
			Log("Cannot execute " + Statement + " with error : " + sqlite3_errmsg(m_ConfDB), -m_err);
			fprintf(stderr, "Failed to execute %s with error:%s.\n", Statement.c_str(), sqlite3_errmsg(m_ConfDB));
			return false;
		}

		int count = sqlite3_column_count(stmt);
		if (count < 1)
		{
			m_err = -295;
			Log("The table " + table + " has not adequate columns.", -m_err);
			fprintf(stderr, "The %s table has not adequate columns.\n", table.c_str());
			return false;
		}

		ofstream file;
		string filename = "/tmp/conf/" + table.substr(0, offset) + ".conf";
		file.open(filename);

		string Line = "";
		for (int i = 0; i < count; i++)
		{
			Line.append((const char*)sqlite3_column_name(stmt, i));
			Line.append(1, split_char);
		}
		file << Line.substr(0, Line.length() - 1) << endl;

		do
		{
			Line.clear();
			for (int i = 0; i < count; i++)
			{
				switch (sqlite3_column_type(stmt, i))
				{
				case SQLITE_INTEGER:
					Line.append(to_string(sqlite3_column_int(stmt, i)));
					Line.append(1, split_char);
					break;

				case SQLITE_FLOAT:
					Line.append(to_string(sqlite3_column_double(stmt, i)));
					Line.append(1, split_char);
					break;

				case SQLITE_TEXT:
					Line.append((const char*)(sqlite3_column_text(stmt, i)));
					Line.append(1, split_char);
					break;

				default:
					Line.append(1, split_char);
					Log("Unsupported type in table " + table + " at column " + to_string(i), 570); // A warning
				}
			}
			file << Line.substr(0, Line.length() - 1) << endl; // get rid of the last split char

			m_err = sqlite3_step(stmt);
		} while (m_err == SQLITE_ROW);
		
		file.close();
		sqlite3_finalize(stmt);
		Log("Exported the table " + table + " to " + filename);
		return true;
	}

	bool ImportTable(string table)
	{
		return true;
	}

	size_t CheckWatchdog()
	{
		struct timeval tv;
		gettimeofday(&tv, nullptr);

		// m_Clients stores all onboard services
		for (size_t i = 0; i < m_TotalClients; i++)
		{
			size_t index = m_Clients[i];
			if (m_WatchdogTimer[index] && tv.tv_sec >= m_WatchdogTimer[index] + 5)
			{
				m_WatchdogTimer[index] = 0;
				m_MsgTS_sec = tv.tv_sec;
				m_MsgTS_usec = tv.tv_usec;
				Log("Watchdog aleart. " + GetServiceTitle(index) + " stops responding on channel " + to_string(index), 520); // a warning with code 520
				return index;
			}
		}
		return 0;
	}

	size_t ChkNewMsg()
	{
		memset(m_buf.mText, 0, sizeof(m_buf.mText));  // fill 0 before reading, make sure no garbage left over
		long l = msgrcv(m_ID, &m_buf, sizeof(m_buf), m_Chn, 0); // blocking read for main
		l -= m_HeaderLength;
		m_err = static_cast<int>(l);
		if (l < 0)
			return 0;
		m_TotalMessageReceived++;

		m_err = 0;
		m_MsgChn = m_buf.sChn; // the service channel where the message comes
		m_MsgTS_sec = m_buf.sec; // the time stamp in seconds of latest receiving message
		m_MsgTS_usec = m_buf.usec;
		size_t type = m_buf.type;
		size_t offset;
		pid_t pid, ppid;
		size_t len = l;

		string keyword;
		string msg;

		// reset the watchdog timer for that channel
		m_WatchdogTimer[m_MsgChn] = m_MsgTS_sec;

		// no auto reply for those normal receiving. type < 31 commands; 32 is string. 33 is integer. anything larger are user defined types
		if (type >= 32)
			return type;

		msg = m_ServiceTitles[m_MsgChn];
		struct timeval tv;
		gettimeofday(&tv, nullptr);

		size_t i; // general iter
		switch (type)
		{
		case CMD_ONBOARD:
			// update onboard list which is stored at m_Clients[]
			for (i = 0; i < m_TotalClients; i++)
				if (m_Clients[i] == m_MsgChn)
					break;
			if (i >= m_TotalClients)
				m_Clients[m_TotalClients++] = m_MsgChn;

			// get the module pid and save it in m_Subscriptions[], reuse the storage
			keyword.assign(m_buf.mText);
			offset = keyword.length() + 1;
			memcpy(&pid, m_buf.mText + offset, sizeof(pid));
			offset += sizeof(pid);
			memcpy(&ppid, m_buf.mText + offset, sizeof(ppid));
			m_Pids[m_MsgChn] = pid; // store the pid into array
			Log("A new service " + keyword + " gets onboard at channel " + to_string(m_MsgChn) + " with pid=" + to_string(pid));
			if (keyword.compare(msg))
				Log("The new service title " + keyword + " is not identical to the predefined title " + msg, 140); // an error with code 140

			// reply databse query first
			ReplyDBQuery();
			Log("Replies the configuration queried from database to channel " + to_string(m_MsgChn), 2100); // a debug with code 2100

			// reply the log severity level next
			SndCmd("LogSeverityLevel=" + to_string(m_LogSeverityLevel[m_MsgChn]), msg);
			Log("Send the command LogSeverityLevel=" + to_string(m_LogSeverityLevel[m_MsgChn]) + " to " + msg, 2100); // a debug log with code 2100

			// reply service list to sub-module at the end
			ReplyServiceList();
			Log("Replies the service list to channel " + to_string(m_MsgChn), 2100); // a debug log with code 2100

			return type;

		case CMD_DATABASEUPDATE:
			ParseDBUpdate();
			Log("Service " + msg + " updates its database elements.");
			return type;

		case CMD_DATABASEQUERY:
			ReplyDBQuery();
			Log("Service " + msg + " requests database query.", 2105); // a debug log
			return type;

		case CMD_WATCHDOG:
			Log("Service " + msg + " feed its watchdog.", 3100); // a verbose log with code 3100
			return type;

		case CMD_STRING:
			return type;

		case CMD_DOWN:
			Log("Service " + msg + " reports down. Informs every other modules.");
			fprintf(stderr, "Service %s reports down. Informs other channels at ", msg.c_str());

			// to inform every running service module that this service has down
			m_buf.sChn = m_MsgChn;
			m_buf.len = 0;
			m_buf.type = CMD_DOWN;
			for (i = 0; i < m_TotalClients; i++)
			{
				if (i == m_MsgChn)
				{
					for (size_t j = i; j < m_TotalClients - 1; j++)
						m_Clients[j] = m_Clients[j + 1];
					m_TotalClients--;
					continue;
				}

				ReSendMsgTo(m_Clients[i]);
				fprintf(stderr, "%d, ", m_Clients[i]);
			}

			fprintf(stderr, " total %d.\n", m_TotalClients);
			return type;

			// Auto parse the system configurations
		case CMD_COMMAND:
			keyword.assign(m_buf.mText);
			offset = keyword.find_first_of('=');  // find =
			msg = keyword.substr(offset + 1); // now msg is right of =
			keyword = keyword.substr(0, offset);  // keyword is left of =

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

		case CMD_STATUS:
			Log("Service '" + msg + "' reports its status", 2250);  // a debug log with code 2250

			ParseStatusReport();
			return type;

		default:
			Log("Gets a message '" + GetRcvMsg() + "' with type of " + to_string(type) + " and length of " + to_string(len) + " from " + msg, 505); // a warning log with code 505
		}
		return type;
	}

	// print the log from local with given severity
	bool Log(string msg, int ErrorCode = 1000)
	{
		if (ErrorCode >= m_Severity)
			return false;

		struct timeval tv;
		gettimeofday(&tv, nullptr);
		m_buf.sec = tv.tv_sec;  //update the timestamp
		m_buf.usec = tv.tv_usec;

		m_buf.rChn = 2; // log is always sent to main module
		m_buf.sChn = m_Chn;
		m_buf.type = CMD_LOG;

		memcpy(m_buf.mText, &ErrorCode, sizeof(ErrorCode));
		size_t offset = sizeof(ErrorCode);
		strcpy(m_buf.mText + offset, msg.c_str());
		offset += msg.length() + 1;
		m_buf.mText[offset++] = 0; // add a /0 in the end anyway
		m_buf.len = offset; // include the /0 at the tail

		if (msgsnd(m_ID, &m_buf, m_buf.len + m_HeaderLength, IPC_NOWAIT))
		{
			m_err = errno;
			printf("Cannot send message in log with error %d.\n", m_err);
			return false;
		}

		m_err = 0;
		m_TotalMessageSent++;
		return true;
	}

	void ParseStatusReport()
	{
		size_t TotalMsgReceived = 0;
		size_t TotalMsgSent = 0;
		int severity;
		size_t offset = 0;
		size_t len;
		string msg;
		string report;

		// total messages sent
		msg.assign(m_buf.mText + offset);
		offset += msg.length() + 1;
		memcpy(&len, m_buf.mText + offset + 1, sizeof(len));
		report = msg + "(" + to_string(m_buf.mText[offset]) + ") = " + to_string(len) + "\n";
		offset += sizeof(len) + 1;
		TotalMsgSent += len;

		// total messages received
		msg.assign(m_buf.mText + offset);
		offset += msg.length() + 1;
		memcpy(&len, m_buf.mText + offset, sizeof(len));
		report += msg + "(" + to_string(m_buf.mText[offset]) + ") = " + to_string(len) + "\n";
		offset += sizeof(len) + 1;
		TotalMsgReceived += len;

		// total database elements
		msg.assign(m_buf.mText + offset);
		offset += msg.length() + 1;
		msg.append("(" + to_string(m_buf.mText[offset++]) + ") = ");
		memcpy(&len, m_buf.mText + offset, sizeof(len));
		msg.append(to_string(len));
		report += msg + "\n";
		offset += sizeof(len) + 1;

		// states
		msg.assign(m_buf.mText + offset);
		offset += msg.length() + 1;
		memcpy(&severity, m_buf.mText + offset, sizeof(severity));
		offset += sizeof(severity);
		report += msg + "\n";
		offset++;

		// subscriptions
		msg.assign(m_buf.mText + offset);
		len = msg.length();
		offset += len + 1;
		msg.append("(" + to_string(m_buf.mText[offset++]) + ") = ");
		for (size_t i = 0; i < len; i++)
			msg.append(to_string(m_buf.mText[offset + i]) + " ");
		report += msg + "\n";
		offset += len + 1;

		// clients
		msg.assign(m_buf.mText + offset);
		len = msg.length();
		offset += len + 1;
		msg.append("(" + to_string(m_buf.mText[offset++]) + ") = ");
		for (size_t i = 0; i < len; i++)
			msg.append(to_string(m_buf.mText[offset + i]) + " ");
		report += msg + "\n";
		offset += len + 1;

		Log(report, 2250);
		return;
	}
};

class later
{
public:
	template <class callable, class... arguments>
	later(int after, bool async, callable&& f, arguments&&... args)
	{
		std::function<typename std::result_of<callable(arguments...)>::type()> task(std::bind(std::forward<callable>(f), std::forward<arguments>(args)...));

		if (async)
		{
			std::thread([after, task]() {
				std::this_thread::sleep_for(std::chrono::milliseconds(after));
				task();
				}).detach();
		}
		else
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(after));
			task();
		}
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

	//start main module
	if (!launcher->StartService(argc, argv))
	{
		cerr << endl << "Cannot launch the headquater." << endl;
		return -1;
	}
	cout << endl << "Main module starts. Waiting for clients to join...." << endl;

	// launch all the sub modules
	for (int i = 3; i < 256; i++)
		if (launcher->RunService(i))
			cout << "MAIN: Launches " << launcher->GetServiceTitle(i) << " service on channel " << i << endl;

	// Main loop for main module
	while (1)
	{
		// check if there is any new message sent to main module. 
		// The main module works in blocking mode. 
		// It has the CMD_COMMAND, CMD_ONBOARD, CMD_DATABASEQUERY, CMD_DATABASEUPDATE, CMD_DOWN, and CMD_STATUS auto processed
		size_t type = launcher->ChkNewMsg();

		if (type == CMD_COMMAND)
		{
			msg = launcher->GetRcvMsg();
			cout << "MAIN gets a command " << msg << " from " << launcher->GetServiceTitle(launcher->m_MsgChn) << endl;

			string cmd;
			string arg;
			
			size_t offset = msg.find_first_of('=');  // find =
			cmd = msg.substr(0, offset);  // keyword is left of =
			arg = msg.substr(offset + 1); // now msg is right of =
			if (!cmd.compare("Export"))
				launcher->ExportTable(arg);
			else if (!cmd.compare("Import"))
				launcher->ImportTable(arg);
		}

		// check the watch dog
		size_t chn = launcher->CheckWatchdog();
		if (chn)
		{
			cout << "MAIN: watchdog warning. '" << launcher->GetServiceTitle(chn)
				<< "' stops responding on channel " << chn << endl;

			if (!launcher->KillService(chn))
			{
				cout << "(MAIN) Cannot stop service " << launcher->GetServiceTitle(chn) << " at channel " << chn << endl;
				continue;
			}

			if (launcher->RunService(chn))
				cout << "MAIN restarts service " << launcher->GetServiceTitle(chn) << " at channel " << chn << endl;
		}
	}
}
