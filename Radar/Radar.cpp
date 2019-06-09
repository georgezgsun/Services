#include <ctime>
#include <chrono>
#include <iostream>
#include <unistd.h>
#include <sys/time.h>
#include <string.h>
#include "../Services/ServiceUtils.h"

using namespace std;

// A demo instance of Radar using services utils
// 1. Test the setup of message queue on sub module
// 2. Test the startup procedure between main module and sub module, including the read and download of the database configure tablet
// 3. Test the cooperation between pic and Roswell, using the Radar data from pic
// 4. Test message queue in blocking mode

string getDateTime(time_t tv_sec, time_t tv_usec)
{
	struct tm *nowtm;
	char tmbuf[64], buf[64];

	nowtm = localtime(&tv_sec);
	strftime(tmbuf, sizeof tmbuf, "%Y-%m-%d %H:%M:%S", nowtm);
	snprintf(buf, sizeof buf, "%s.%06ld", tmbuf, tv_usec);
	return buf;
}

int main(int argc, char *argv[])
{
	ServiceUtils *Radar = new ServiceUtils(argc, argv);

	int ID{ 0 };
	int baudrate{ 0 };
	string RadarType;
	string RadarData;
	int last_baudrate{ 0 };
	char *myBuf;

	Radar->LocalMap("ID", &ID);
	Radar->LocalMap("BaudRate", &baudrate);
	Radar->LocalMap("Type", &RadarType);

	if (!Radar->StartService())
	{
		cerr << endl << "Cannot setup the connection to the main module. Error=" << Radar->m_err << endl;
		return -1;
	}

	long myChannel = Radar->GetServiceChannel("");
	string myTitle = Radar->GetServiceTitle(myChannel);
	cout << "Service provider " << myTitle << " is up at " << myChannel << endl;

	// The demo of send a command
	Radar->SndCmd("Hello from " + myTitle + " module.", "1");
	Radar->SubscribeService("GPIO");

	size_t command;
	struct timeval tv;

	string msg;
	while (true)
	{
		command = Radar->ChkNewMsg(CTL_BLOCKING);

		gettimeofday(&tv, nullptr);
		msg = Radar->GetRcvMsg();

		// if CMD_DOWN is sent from others, no return
		if (command == CMD_DOWN)
		{
			cout << myTitle << " is down by the command from main module." << endl;
			break;
		}
		else if (command == CMD_COMMAND)
		{
			cout << myTitle << " gets a command '" << msg << "' from " << Radar->m_MsgChn << endl;
			continue;
		}
		else if (command == CMD_PUBLISHDATA)
		{
			size_t len = Radar->GetRcvMsgBuf(&myBuf);
			string tmp = Radar->GetRcvMsg();
			size_t offset = tmp.length() + 2;
			RadarData.assign(myBuf + offset);
			offset += RadarData.length() + 1;
			if (RadarData.compare(RadarData))
			{
				cout << endl << "(" << myTitle << ")" << getDateTime(tv.tv_sec, tv.tv_usec) << " : ";
				cout << tmp << "=" << RadarData << endl;
				last_baudrate = baudrate;
			}
			offset += len;
		}

		if (last_baudrate != baudrate)
		{
			cout << myTitle << " gets configured as: \nID=" << ID << endl
				<< "Radar baud rate " << baudrate << endl
				<< "Radar type: " << RadarType << endl;
			last_baudrate = baudrate;
		}

		Radar->WatchdogFeed();
	}
}
