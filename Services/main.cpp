//#include <QCoreApplication>
#include <ctime>
#include <chrono>
#include <iostream>
#include <unistd.h>
#include <sys/time.h>
#include <string.h>
#include "ServiceUtils.h"

using namespace std;

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

	string serviceTitles[5] = { "SERVER", "GPS", "Radar", "Trigger", "FrontCam" };
	char serviceChannels[5] = { 1,3,4,5,19 };
	char serviceListBuf[255]; // store the service list
	size_t lengthServiceListBuf = 0;

	string DBKeywords[10] = { "PreEvent", "Chunk", "CamPath", "User", "PassWord", "Cloud Server", "WAP", "Luanguage", "Active Triggers", "Auto upload" };
	char  DBTypes[10] = { 1, 1, 0, 0, 0, 0, 1, 0, 0, 1 }; // 1 for int, 0 for string
	string stringDBValues[10] = { "120", "60", "rtsp://10.25.20.0/1/h264major", "Mark Richman", "noPassword", "50.24.54.54", "1", "English", "FLB SRN MIC LSB RLB", "1" };
	char DBQueryBuf[255]; // store database query result
	size_t lengthDBQueryBuf = 0;

	char DBQueryBuf2[255]; // store database query result
	size_t lengthDBQueryBuf2 = 0;

	char *bufMsg;
	size_t typeMsg;
	size_t len = 0;
	size_t chn = 0;
	size_t offset = 0;
	pid_t pid;
	pid_t ppid;
	string sTitle;
	long logType;
	string logContent;
	struct timeval tv;

	int PreEvent = 0;
	int PreEvent2;
	int Chunk;
	string CamPath;
	string User;
	string Password;
	string CloudServer;
	int WAP;
	string Luanguage;
	string ActiveTriggers;
	int AutoUpload;

	// Prepare the service list. This is a simulation.
	// [channel_1][title_1][channel_2][title_2] ... [channel_n][title_n] ; titles are end with /0. channel is of size 1 byte
	for (int i = 0; i < 5; i++)
	{
		serviceListBuf[offset++] = serviceChannels[i];
		strcpy(serviceListBuf + offset, serviceTitles[i].c_str());
		offset += serviceTitles[i].length() + 1;
	}
	lengthServiceListBuf = offset;

	// Prepare the database query results. This is a simulation.
	// [keyword_1][len_1][data_1][keyword_2][len_2][data_2] ... [keyword_n][len_n][data_n] ; end with keyword empty.
	// len is of size 1 byte, 0 represents for string. keyword end with /0
	offset = 0;
	for (int i = 0; i < 10; i++)
	{
		// assign the keywords first
		strcpy(DBQueryBuf + offset, DBKeywords[i].c_str());
		offset += DBKeywords[i].length() + 1;


		// string value is assigned differently, there is a /0 at the end
		if (DBTypes[i])
		{
			DBQueryBuf[offset++] = sizeof(int);
			int t = stoi(stringDBValues[i]);
			memcpy(DBQueryBuf + offset, &t, sizeof(int));
			offset += sizeof(int);
		}
		else
		{
			DBQueryBuf[offset++] = 0;
			strcpy(DBQueryBuf + offset, stringDBValues[i].c_str());
			offset += stringDBValues[i].length() + 1;

		}
	}
	DBQueryBuf[offset] = 0;
	lengthDBQueryBuf = offset;

	lengthDBQueryBuf2 = lengthDBQueryBuf;
	memcpy(DBQueryBuf2, DBQueryBuf, lengthDBQueryBuf);
	for (size_t i = 0; i < lengthDBQueryBuf2; i++)
		if (DBQueryBuf2[i] == 'v')
			DBQueryBuf2[i] = 'x';

	ServiceUtils *launcher = new ServiceUtils(0, argv);
	if (!launcher->StartService())
	{
		cerr << endl << "Cannot launch the headquater." << endl;
		return -1;
	}
	cout << endl << "Server starts. Waiting for clients to join...." << endl;

	launcher->dbMap("PreEvent", &PreEvent);
	launcher->dbMap("PreExent", &PreEvent2);
	launcher->dbMap("Chunk", &Chunk);
	launcher->dbMap("CamPath", &CamPath);
	launcher->dbMap("User", &User);
	launcher->dbMap("PassWord", &Password);
	launcher->dbMap("Cloud Server", &CloudServer, 0);
	launcher->dbMap("WAP", &WAP, 4);
	launcher->dbMap("Luanguage", &Luanguage, 0);
	launcher->dbMap("Active Triggers", &ActiveTriggers, 0);
	launcher->dbMap("Auto upload", &AutoUpload);

	// Simulate the server/head working
	while (1)
	{
		gettimeofday(&tv, nullptr);

		// check the watch dog
		chn = launcher->WatchdogFeed();
		if (chn)
		{
			cout << getDateTime(tv.tv_sec, tv.tv_usec) << " : Warning. Watchdog " << chn << " alerts at " << getDateTime(tv.tv_sec,tv.tv_usec) << endl;
		}

		// check if there is any new message sent to server. These messages are not auto processed by the library.
		typeMsg = launcher->ChkNewMsg();
		if (!typeMsg)
			continue;
		len = launcher->GetRcvMsgBuf(&bufMsg);
		//memcpy(bufMsg, p, len);

		tmp = getDateTime(tv.tv_sec, tv.tv_usec);
		cout << tmp << " : ";

		tmp = getDateTime(launcher->m_MsgTS_sec, launcher->m_MsgTS_usec);
		// process those messages sent to server
		switch (typeMsg)
		{
		case CMD_ONBOARD:
			offset = 0;
			memcpy(&pid, bufMsg + offset, sizeof(pid));
			offset += sizeof(pid);
			memcpy(&ppid, bufMsg + offset, sizeof(ppid));
			offset += sizeof(ppid);
			sTitle.assign(bufMsg + offset);

			cout << "Service provider " << sTitle << " gets onboard on channel " << launcher->m_MsgChn << " with PID=" << pid 
				<< ", Parent PID=" << ppid << " at " << tmp << endl;

			// send back the service list first
			launcher->SndMsg(serviceListBuf, CMD_LIST, lengthServiceListBuf, launcher->m_MsgChn);

			// send back the database properties at the end
			launcher->SndMsg(DBQueryBuf, CMD_DATABASEQUERY, lengthDBQueryBuf, launcher->m_MsgChn);
			launcher->SndMsg(DBQueryBuf2, CMD_DATABASEQUERY, lengthDBQueryBuf2, launcher->m_MsgChn);
			break;

		case CMD_LOG:
			offset = 0;
			memcpy(&logType, bufMsg, sizeof(logType));
			logContent.assign(bufMsg + sizeof(logType));
			cout << "LOG from channel " << launcher->m_MsgChn << ", [" << logType << "] " << logContent << " at " << tmp << endl;
			break;

		case CMD_DATABASEQUERY:
			launcher->SndMsg(DBQueryBuf2, CMD_DATABASEQUERY, lengthDBQueryBuf2, launcher->m_MsgChn);
			cout << "Got database query request from channel " << launcher->m_MsgChn << " at " << tmp << endl;
			break;

		case CMD_WATCHDOG:
			cout << "Got a heartbeat/watchdog message from " << launcher->m_MsgChn << " at " << tmp << endl;
			break;

		case CMD_STRING:
			cout << "Got a message of string '" << launcher->GetRcvMsg() << "' from " << launcher->m_MsgChn << " at " << tmp << endl;
			break;

		default:
			cout << "Got message '" << launcher->GetRcvMsg() << "' from " << launcher->m_MsgChn \
				<< " with type of " << typeMsg << " and length of " << len << " at " << tmp << endl;
		}
	}

	//  return a.exec();
}
