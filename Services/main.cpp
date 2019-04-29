#include <ctime>
#include <chrono>
#include <iostream>
#include <unistd.h>
#include <sys/time.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <string.h>
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
	};

	// used to store the index of database elements for each sub module 
	size_t SubIndex[32][127];

	size_t m_TotalDBElements[127];

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
		SndMsg(NULL, CMD_COMMAND, 0, m_ServiceChannels[0]);
		for (size_t i = 1; i < m_TotalServices; i++)
			ReSendMsgTo(m_ServiceChannels[i]);
		return true;
	}

	bool InformDown(long Channel)
	{
		// The main informs every sub module that the service on the Channel has down
		struct timeval tv;
		gettimeofday(&tv, nullptr);
		m_buf.rChn = Channel;
		m_buf.sChn = Channel;
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
		for (size_t i = 1; i < m_TotalServices; i++)
		{
			size_t index = m_ServiceChannels[i];
			if (m_WatchdogTimer[index] > 0 && m_buf.sec >= m_WatchdogTimer[index] + 5)
			{
				m_WatchdogTimer[index] = 0;
				return index;
			}
		}
		return 0;
	}
};

string getDateTime(time_t tv_sec, time_t tv_usec)
{
	struct tm *nowtm;
	char tmbuf[64], buf[64];

	nowtm = localtime(&tv_sec);
	strftime(tmbuf, sizeof tmbuf, "%Y-%m-%d %H:%M:%S", nowtm);
	snprintf(buf, sizeof buf, "%s.%06ld", tmbuf, tv_usec);
	return buf;
};

int main(int argc, char *argv[])
{
	string msg;
	string tmp;
	string currentDateTime;

	string serviceTitles[5] = { "MAIN", "GPS", "Radar", "Trigger", "FrontCam" };
	char serviceChannels[5] = { 1,3,4,5,19 };

	char *bufMsg;
	size_t typeMsg;
	size_t len = 0;
	size_t chn = 0;
	size_t offset = 0;
	pid_t pid;
	pid_t ppid;
	string sTitle;
	char LogSeverity(4);
	char Severity;
	string logContent;
	struct timeval tv;

	int PreEvent = 120;
	int PreEvent2 = 45;
	int Chunk = 30;
	string CamPath("rtsp://10.25.20.0/1/h264major");
	string User("Mark Richman");
	string Password("noPassword");
	string CloudServer("50.24.54.54");
	int WAP = 1;
	string Luanguage("English");
	string ActiveTriggers("FLB SRN MIC LSB RLB");
	int AutoUpload = 0;

	MainModule *launcher = new MainModule();
	if (!launcher->StartService())
	{
		cerr << endl << "Cannot launch the headquater." << endl;
		return -1;
	}
	cout << endl << "main module starts. Waiting for clients to join...." << endl;

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

	launcher->SndCmd("LogSeverityLevel=5", "1");
	launcher->SndCmd("AutoWatchdog=off", "1");
	launcher->SndCmd("AutoUpdate=on", "1");

	// Simulate the main module/head working
	while (1)
	{
		gettimeofday(&tv, nullptr);

		// check the watch dog
		chn = launcher->CheckWatchdog();
		if (chn)
		{
			cout << getDateTime(tv.tv_sec, tv.tv_usec) 
				<< " : Watchdog warning. '" << launcher->GetServiceTitle(chn) 
				<< "' stops responding on channel " << chn << endl;
		}

		// check if there is any new message sent to main module. These messages are not auto processed by the library.
		typeMsg = launcher->ChkNewMsg();
		if (!typeMsg)
			continue;
		len = launcher->GetRcvMsgBuf(&bufMsg);

		// get the current time
		gettimeofday(&tv, nullptr);
		currentDateTime = getDateTime(tv.tv_sec, tv.tv_usec) + " : " + launcher->GetServiceTitle(launcher->m_MsgChn);
		tmp = getDateTime(launcher->m_MsgTS_sec, launcher->m_MsgTS_usec);
		// process those messages sent to main module
		switch (typeMsg)
		{
		case CMD_ONBOARD:

			memcpy(&pid, bufMsg, sizeof(pid));
			offset = sizeof(pid);
			memcpy(&ppid, bufMsg + offset, sizeof(ppid));
			// When get sub-module onboard, reply databse query first
			launcher->ReplyDBQuery();

			// Reply service list to sub-module after
			launcher->ReplyServiceList();

			launcher->SndCmd("LogSeverityLevel=5", launcher->GetServiceTitle(launcher->m_MsgChn));

			//launcher->Log("Service module " + launcher->GetServiceTitle(launcher->m_MsgChn) + " gets onboard at channel "
			//	+ to_string(launcher->m_MsgChn));

			if (LogSeverity >= 4)
				cout << currentDateTime << " gets onboard on channel " << launcher->m_MsgChn  << " with pid=" << pid
				<< ", ppid=" << ppid << " at " << tmp << endl;
			break;

		case CMD_DATABASEUPDATE:
			launcher->ParseDBUpdate();
			//launcher->Log("Service " + launcher->GetServiceTitle(launcher->m_MsgChn) + " updates its database elements.");
			if (LogSeverity >= 4)
				cout << currentDateTime << " updates its database elements at " << tmp << endl;
			break;

		case CMD_LOG:
			offset = 0;
			memcpy(&Severity, bufMsg, sizeof(Severity));
			logContent.assign(bufMsg + sizeof(LogSeverity));
			
			if (LogSeverity >= Severity)
				cout << currentDateTime << " logs [" << +Severity << "] " << logContent << endl;
			else
				cout << currentDateTime << " ignores logs [" << +Severity << "]" << endl;
			break;

		case CMD_DATABASEQUERY:
			launcher->ReplyDBQuery();
			//launcher->Log("Service " + launcher->GetServiceTitle(launcher->m_MsgChn) + " requests database query.", 5);
			if (LogSeverity >= 5)
				cout << currentDateTime << " requests database query at " << tmp << endl;
			break;

		case CMD_WATCHDOG:
			//launcher->Log("Service " + launcher->GetServiceTitle(launcher->m_MsgChn) + " feed its watchdog.", 5);
			if (LogSeverity >= 5)
				cout << currentDateTime << " feeds its watchdog at " << tmp << endl;
			break;

		case CMD_STRING:
			cout << currentDateTime << " : Got a message of string '" << launcher->GetRcvMsg() 
				<< "' from " << launcher->m_MsgChn << " at " << tmp << endl;
			break;

		case CMD_DOWN:
			// broadcast the message that one channel has down
			launcher->InformDown(launcher->m_MsgChn);

			//launcher->Log("Service " + launcher->GetServiceTitle(launcher->m_MsgChn) + " reports down. Informs every sub modules.");
			if (LogSeverity >= 4)
				cout << currentDateTime << " : reports down. Informs every modules." << endl;
			break;

		default:
			if (LogSeverity >= 3)
				cout << currentDateTime << " : sends a message '" << launcher->GetRcvMsg() << "' with type of " 
				<< typeMsg << " and length of " << len << " at " << tmp << endl;
		}
	}
}
