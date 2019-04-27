#include <ctime>
#include <chrono>
#include <iostream>
#include <unistd.h>
#include <sys/time.h>
#include <string.h>
#include "ServiceUtils.h"

using namespace std;

class MainModule : public ServiceUtils
{
public:

	MainModule()
	{
	};

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
		return true;
	};

	bool RequestModuleStatus()
	{
		SndMsg(NULL, CMD_COMMAND, 0, m_ServiceChannels[0]);
		for (size_t i = 1; i < m_TotalServices; i++)
			ReSendMsgTo(m_ServiceChannels[i]);
		return true;
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
	char serviceListBuf[255]; // store the service list
	size_t lengthServiceListBuf = 0;

	char *bufMsg;
	size_t typeMsg;
	size_t len = 0;
	size_t chn = 0;
	size_t offset = 0;
	pid_t pid;
	pid_t ppid;
	string sTitle;
	char LogSeverity(2);
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

	// Prepare the service list. This is a simulation.
	// [channel_1][title_1][channel_2][title_2] ... [channel_n][title_n] ; titles are end with /0. channel is of size 1 byte
	for (int i = 0; i < 5; i++)
	{
		serviceListBuf[offset++] = serviceChannels[i];
		strcpy(serviceListBuf + offset, serviceTitles[i].c_str());
		offset += serviceTitles[i].length() + 1;
	}
	lengthServiceListBuf = offset;

	ServiceUtils *launcher = new ServiceUtils(0, argv);
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
	launcher->LocalMap("SeverityLevel", &LogSeverity, 1);

	// Simulate the main module/head working
	while (1)
	{
		gettimeofday(&tv, nullptr);

		// check the watch dog
		chn = launcher->WatchdogFeed();
		if (chn)
		{
			cout << getDateTime(tv.tv_sec, tv.tv_usec) 
				<< " : Watchdog warning. Service provider '" << launcher->GetServiceTitle(chn) 
				<< "' stops responding on channel " << chn << endl;
		}

		// check if there is any new message sent to main module. These messages are not auto processed by the library.
		typeMsg = launcher->ChkNewMsg();
		if (!typeMsg)
			continue;
		len = launcher->GetRcvMsgBuf(&bufMsg);
		//memcpy(bufMsg, p, len);

		currentDateTime = getDateTime(tv.tv_sec, tv.tv_usec);
		tmp = getDateTime(launcher->m_MsgTS_sec, launcher->m_MsgTS_usec);
		// process those messages sent to main module
		switch (typeMsg)
		{
		case CMD_ONBOARD:
			sTitle.assign(bufMsg + offset);
			offset = sTitle.length() + 1;
			memcpy(&pid, bufMsg + offset, sizeof(pid));
			offset += sizeof(pid);
			memcpy(&ppid, bufMsg + offset, sizeof(ppid));

			cout << currentDateTime << " : Service provider " << sTitle 
				<< " gets onboard on channel " << launcher->m_MsgChn 
				<< " with PID=" << pid
				<< ", Parent PID=" << ppid << " at " << tmp << endl;

			// send back the service list first
			launcher->SndMsg(serviceListBuf, CMD_LIST, lengthServiceListBuf, launcher->m_MsgChn);

			// send back the database properties at the end ??
			launcher->ServiceQuery(to_string(launcher->m_MsgChn));
			break;

		case CMD_LOG:
			offset = 0;
			memcpy(&LogSeverity, bufMsg, sizeof(LogSeverity));
			logContent.assign(bufMsg + sizeof(LogSeverity));
			cout << currentDateTime << " : LOG from channel " << launcher->m_MsgChn 
				<< " on " << tmp << ", [" << LogSeverity << "] " << logContent  << endl;
			break;

		case CMD_DATABASEQUERY:
			CloudServer = getDateTime(tv.tv_sec, 0);
			PreEvent = tv.tv_usec;
			launcher->ServiceQuery(to_string(launcher->m_MsgChn));
			cout << currentDateTime << " : Got database query request from channel " 
				<< launcher->m_MsgChn << " at " << tmp << endl;
			break;

		case CMD_WATCHDOG:
			cout << "Got a heartbeat/watchdog message from " << launcher->m_MsgChn << " at " << tmp << endl;
			break;

		case CMD_STRING:
			cout << currentDateTime << " : Got a message of string '" << launcher->GetRcvMsg() 
				<< "' from " << launcher->m_MsgChn << " at " << tmp << endl;
			break;

		case CMD_DATABASEUPDATE:
			cout << currentDateTime << " : Got a database update request from channel " 
				<< launcher->m_MsgChn << " at " << tmp << endl;
			break;

		case CMD_DOWN:
			cout << currentDateTime << " : Got a message of service down from channel "
				<< launcher->m_MsgChn << " at " << tmp << endl;

			// broadcast the message that one channel has down
			launcher->UpdateServiceData();
			cout << currentDateTime << " : Broadcast the message that service on channel "
				<< launcher->m_MsgChn << " is down at " << tmp << endl;
			break;

		default:
			cout << currentDateTime << " : Got message '" << launcher->GetRcvMsg() << "' from " << launcher->m_MsgChn
				<< " with type of " << typeMsg << " and length of " << len << " at " << tmp << endl;
		}
	}

	//  return a.exec();
}
