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
	tester->LocalMap("Optiona Triggers", &OptionTriggers);
	tester->LocalMap("Speed up", &SpeedUp);
	tester->LocalMap("Speed down", &SpeedDown);

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
//	int lastPreEvent = 1;
	int count = 5;

	string msg;
	while (true)
	{
		gettimeofday(&tv, nullptr);
		nowtm = localtime(&tv.tv_sec);
		strftime(tmbuf, sizeof tmbuf, "%Y-%m-%d %H:%M:%S", nowtm);
		snprintf(datetime, sizeof datetime, "%s.%06ld", tmbuf, tv.tv_usec);

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
			cout << datetime << " : " << myTitle
				<< " gets configured as ID=" << ID
				<< ", active triggers: " << ActiveTriggers
				<< ", optional triggers: " << OptionTriggers
				<< ", GPS speed trigger at " << SpeedUp
				<< ", GPS speed cancel at " << SpeedDown
				<< endl;
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
			cout << datetime << " : Count down " << count << endl;

			if (count <= 0)
				break;

		}
	}
}
