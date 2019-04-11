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

	string serviceList[5] = { "SERVER", "GPS", "Radar", "Trigger", "FrontCam" };
	long serviceChannels[5] = { 1,3,4,5,19 };
	string DBKeywords[10] = { "PreEvent", "Chunk", "CamPath", "User", "PassWord", "Cloud Server", "WAP", "Luanguage", "Active Triggers", "Auto upload" };
	size_t DBTypes[10] = { 1, 1, 0, 0, 0, 0, 1, 0, 0, 1 }; // 1 for int, 0 for string
	string stringDBValues[10] = { "120", "60", "rtsp://10.25.20.0/1/h264major", "Mark Richman", "noPassword", "50.24,54,54", "1", "English", "FLB SRN MIC LSB RLB", "1" };
	long watchdogs[5] = { 0, 0, 0, 0, 0 };

	ServiceUtils *launcher = new ServiceUtils(1);
	if (!launcher->StartService())
	{
		cerr << endl << "Cannot launch the headquater." << endl;
		return -1;
	}
	cout << endl << "Server starts. Waiting for clients to join...." << endl;

	char text[255];
	char DBText[255]; // store database keywords and element type
	size_t DBLength = 0;
	size_t type;
	size_t len = 0;
	size_t offset = 0;;
	pid_t pid;
	pid_t ppid;
	string sTitle;
	long logType;
	string logContent;
	struct timeval tv;

	// Prepare the database query results. This is a simulation.
	for (int i = 0; i < 10; i++)
	{
		memcpy(DBText + offset, &DBTypes[i], sizeof(size_t));
		offset += sizeof(size_t);

		if (DBTypes[i] == 0)
		{
			memcpy(DBText + offset, stringDBValues[i].c_str(), stringDBValues[i].length());
			offset += stringDBValues[i].length();
		}
		else
		{
			int n = stoi(stringDBValues[i]);
			memcpy(DBText + offset, &n, sizeof(n));
			offset += sizeof(n);
		}
	}
	DBLength = offset;

	// Simulate the server/head working
	while (1)
	{
		gettimeofday(&tv, nullptr);

		// check the watch dog
		for (int i = 0; i < 5; i++)
			if (watchdogs[i] && (tv.tv_sec > watchdogs[i]))
			{
				cout << getDateTime(tv.tv_sec, tv.tv_usec) << " : Warning. Watcgdog " << i << " alert at " << tv.tv_sec << "." << tv.tv_usec << endl;
				watchdogs[i] = tv.tv_sec + 5;
			}

		// check if there is any new message sent to server. These messages are not auto processed by the library.
		if (!launcher->RcvMsg(&text, &type, &len))
			continue;

		tmp = getDateTime(tv.tv_sec, tv.tv_usec);
		cout << tmp << " : ";

		tmp = getDateTime(launcher->m_MsgTS_sec, launcher->m_MsgTS_usec);
		// process those messages sent to server
		switch (type)
		{
		case CMD_ONBOARD:
			offset = 0;
			memcpy(&pid, text + offset, sizeof(pid));
			offset += sizeof(pid);
			memcpy(&ppid, text + offset, sizeof(ppid));
			offset += sizeof(ppid);
			sTitle.assign(text + offset);

			cout << "Service provider " << sTitle << " gets onboard at " << tmp \
				<< " on channel " << launcher->m_MsgChn << " with PID=" << pid << ", Parent PID=" << ppid << endl;

			// check if the onboard client has already received init messages
			if (sTitle != "")
				break;

			//sTitle = to_string(launcher->m_MsgChn);

			// send back the service list first
			offset = 0;
			for (int i = 0; i < 5; i++)
			{
				memcpy(text + offset, &serviceChannels[i], sizeof(long));
				offset += sizeof(long);
				memcpy(text + offset, serviceList[i].c_str(), serviceList[i].length() + 1);
				offset += serviceList[i].length() + 1;
			}
			launcher->SndMsg(text, CMD_LIST, offset, launcher->m_MsgChn);

			// send back the database property list next
			offset = 0;
			for (int i = 0; i < 10; i++)
			{
				memcpy(text + offset, &DBTypes[i], sizeof(size_t));
				offset += sizeof(size_t);
				len = DBKeywords[i].length() + 1;
				memcpy(text + offset, &DBKeywords[i], len);
				offset += len;
			}
			launcher->SndMsg(text, CMD_DATABASEINIT, offset, launcher->m_MsgChn);

			// send back the database properties at the end
			launcher->SndMsg(DBText, CMD_DATABASEQUERY, DBLength, launcher->m_MsgChn);
			break;

		case CMD_LOG:
			offset = 0;
			memcpy(&logType, text, sizeof(logType));
			logContent.assign(text + sizeof(logType));
			cout << "LOG from channel " << launcher->m_MsgChn << " at " << tmp << ", [" << logType << "] " << logContent << endl;
			break;

		case CMD_WATCHDOG:
			watchdogs[0] = launcher->m_MsgTS_sec + 5;
			cout << "Get FeedWatchdog notice from channel " << launcher->m_MsgChn << " at " << tmp \
				<< ". Watch dog will be alert in 5s." << endl;
			break;

		case CMD_DATABASEQUERY:
			launcher->SndMsg(DBText, CMD_DATABASEQUERY, DBLength, sTitle);
			cout << "Got database query request from channel " << launcher->m_MsgChn << " at " << tmp \
				<< ". Reply the latest database select result." << endl;
			break;

		case 0:
			msg.assign(text);
			cout << "Got a message of string " << msg << " from " << launcher->m_MsgChn << " at " << tmp << endl;
			break;

		default:
			cout << "Got message from " << launcher->m_MsgChn << " at " << tmp \
				<< " with type of " << type << " and length of " << len << endl;
		}
	}

	//  return a.exec();
}
