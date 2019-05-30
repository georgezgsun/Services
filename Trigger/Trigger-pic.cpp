#include <ctime>
#include <chrono>
#include <iostream>
#include <unistd.h>
#include <sys/time.h>
#include <string.h>
#include "../Services/ServiceUtils.h"

using namespace std;

// A demo instance of Trigger using services utils
// 1. Test the setup of message queue on sub module
// 2. Test the startup procedure between main module and sub module, including the read and download of the database configure tablet
// 3. Test the cooperation between pic and Trigger, using the Trigger data from pic
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
	ServiceUtils *Trigger = new ServiceUtils(argc, argv);

	int ID{ 0 };
	string ActiveTriggers;
	string OptionTriggers;
	int SpeedUp;
	int SpeedDown;
	int lastSpeed{ 0 };
	char *myBuf;
	string myTriggers;
	string lastTriggers{};

	Trigger->LocalMap("ID", &ID);
	Trigger->LocalMap("Active Triggers", &ActiveTriggers);
	Trigger->LocalMap("Optional Triggers", &OptionTriggers);
	Trigger->LocalMap("GPS Speeding Threshold", &SpeedUp);
	Trigger->LocalMap("GPS Speeding Cancel", &SpeedDown);

	if (!Trigger->StartService())
	{
		cerr << endl << "Cannot setup the connection to the main module. Error=" << Trigger->m_err << endl;
		return -1;
	}

	long myChannel = Trigger->GetServiceChannel("");
	string myTitle = Trigger->GetServiceTitle(myChannel);
	cout << "Service provider " << myTitle << " is up at " << myChannel << endl;

	// The demo of send a command
	Trigger->SndCmd("Hello from " + myTitle + " module.", "1");

	size_t command;
	struct timeval tv;

	string msg;
	while (true)
	{
		command = Trigger->ChkNewMsg(CTL_BLOCKING);

		gettimeofday(&tv, nullptr);
		msg = Trigger->GetRcvMsg();

		// if CMD_DOWN is sent from others, no return
		if (command == CMD_DOWN)
		{
			if (Trigger->m_MsgChn == myChannel)
			{
				cout << myTitle << " is down by the command from main module." << endl;
				break;
			}
			else
				cout << " gets message that " << Trigger->GetServiceTitle(Trigger->m_MsgChn) << "is down." << endl;
		}
		else if (command == CMD_COMMAND)
		{
			cout << myTitle << " gets a command '" << msg << "' from " << Trigger->m_MsgChn << endl;
			continue;
		}
		else if (command == CMD_PUBLISHDATA)
		{
			size_t len = Trigger->GetRcvMsgBuf(&myBuf);
			string tmp = Trigger->GetRcvMsg();
			size_t offset = tmp.length() + 2;
			myTriggers.assign(myBuf + offset);
			offset += myTriggers.length() + 1;
			if (myTriggers.compare(lastTriggers))
			{
				cout << endl << "(" << myTitle << ")" << getDateTime(tv.tv_sec, tv.tv_usec) << " : ";
				cout << tmp << "=" << myTriggers << endl;
				lastTriggers = myTriggers;
			}
			offset += len;
		}

		if (lastSpeed != SpeedUp)
		{
			cout << myTitle	<< " gets configured as: \nID=" << ID << endl
				<< "Active Triggers: " << ActiveTriggers << endl
				<< "Optional Triggers: " << OptionTriggers << endl
				<< "GPS Speeding Threshold: " << SpeedUp << endl
				<< "GPS Speeding Cancel: " << SpeedDown << endl;
			lastSpeed = SpeedUp;
		}

		Trigger->WatchdogFeed();
	}
}
