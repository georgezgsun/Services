#include <ctime>
#include <chrono>
#include <iostream>
#include <unistd.h>
#include <sys/time.h>
#include <string.h>
#include "../Services/ServiceUtils.h"

using namespace std;

// tester on services utils
// 1. Test the setup of message queue on both main module and client
// 2. Test the startup procedure on both main module and client so that they can talk and the initialization is correct
// 3. Test the LocalMap function on both main module and client so that the keywords from the database may map local variable
// 4. Test message send from a client can be received in main module correctly
// 5. Test the watchdog function works on both main module and client
// 6. Test database query and update data transfer is correct between main module and client

// TODO
// 1. Test the service subscription and query
// 2. Test the auto re-subscription after the service provider is back
//

int main(int argc, char *argv[])
{
	ServiceUtils *tester = new ServiceUtils(argc, argv);

	int ID{ 0 };
	int baudrate{ 0 };
	string GPSType;
	int last_baudrate{ 0 };
	string position = "3258.1187N,09642.9508W";
	int height = 202;
	int time;
	string ActiveTriggers;
	string OptionTriggers;
	int SpeedUp;
	int SpeedDown;
	int lastSpeed{ 0 };

	tester->LocalMap("ID", &ID);
	tester->LocalMap("BaudRate", &baudrate);
	tester->LocalMap("Type", &GPSType);

	tester->LocalMap("Active Triggers", &ActiveTriggers);
	tester->LocalMap("Optional Triggers", &OptionTriggers);
	tester->LocalMap("GPS Speeding Threshold", &SpeedUp);
	tester->LocalMap("GPS Speeding Cancel", &SpeedDown);

	tester->AddToServiceData("position", &position);
	tester->AddToServiceData("latitute", &height);
	tester->AddToServiceData("epoc", &time);

	if (!tester->StartService())
	{
		cerr << endl << "Cannot setup the connection to the main module. Error=" << tester->m_err << endl;
		return -1;
	}

	long myChannel = tester->GetServiceChannel("");
	string myTitle = tester->GetServiceTitle(myChannel);
	cout << "Service provider " << myTitle << " is up at " << myChannel << endl;

	tester->SndCmd("Hello from " + myTitle + " module.", "1");

	size_t command;
	struct timeval tv;
	struct tm *nowtm;
	char tmbuf[64], datetime[64];
	int count = myChannel + 10;

	string msg;
	while (true)
	{
		gettimeofday(&tv, nullptr);
		nowtm = localtime(&tv.tv_sec);
		strftime(tmbuf, sizeof tmbuf, "%Y-%m-%d %H:%M:%S", nowtm);
		snprintf(datetime, sizeof datetime, "%s.%06ld", tmbuf, tv.tv_usec);
		time = tv.tv_sec;

		command = tester->ChkNewMsg();
		if (command)
		{
			msg = tester->GetRcvMsg();
			cout << datetime << " : Received messages '" << msg << "' of type " << command << " from " << tester->m_MsgChn << endl;

			// if CMD_DOWN is sent from others, no return
			if (command == CMD_DOWN)
			{
				cout << myTitle << " is down by the command from main module." << endl;
				break;
			}

			if (command == CMD_COMMAND)
			{
				cout << "Get a command '" << msg << "' from " << tester->m_MsgChn << endl;
				continue;
			}
		}

		if (last_baudrate != baudrate)
		{
			cout << datetime << " : " << myTitle << " gets configured as ID=" << ID 
				<< " baud rate=" << baudrate 
				<< " and type=" << GPSType << ".\n";
			last_baudrate = baudrate;
		}

		if (lastSpeed != SpeedUp)
		{
			cout << datetime << " : \n" << myTitle
				<< " gets configured as: \nID=" << ID << endl
				<< "Active Triggers: " << ActiveTriggers << endl
				<< "Optional Triggers: " << OptionTriggers << endl
				<< "GPS Speeding Threshold: " << SpeedUp << endl
				<< "GPS Speeding Cancel: " << SpeedDown << endl;
			lastSpeed = SpeedUp;
		}

		// provide service here
		// get GPS from UART
		// if GPS changed
		//   PublishServiceData
		// provide service here

		if (tester->WatchdogFeed())
		{
			count--;

			if (count <= 0)
			{
				//count = 5;
				//break;
				cout << datetime << " : " << myTitle << " counts down to " << count 
					<< " with pid=" << getpid() << ", ppid=" << getppid() << "." << endl;
				sleep(10);  // this will stroke the watchdog action
				cout << myTitle << " is still running with pid=" << getpid() << endl;
				tester->Log(myTitle + " is still running. No watchdog action.", 1);
				break;
			}
		}
	}
}
