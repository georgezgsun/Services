#include <ctime>
#include <chrono>
#include <iostream>
#include <unistd.h>
#include <sys/time.h>
#include <string.h>
#include "../Services/ServiceUtils.h"

using namespace std;

// A demo instance of GPS using services utils
// 1. Test the setup of message queue on sub module
// 2. Test the startup procedure between main module and sub module, including the read and download of the database configure tablet
// 3. Test the cooperation between pic and GPS, using the GPS data from pic
// 4. Test message queue in blocking mode

int main(int argc, char *argv[])
{
	ServiceUtils *GPS = new ServiceUtils(argc, argv);

	int ID{ 0 };
	int baudrate{ 0 };
	string GPSType;
	int last_baudrate{ 0 };
	string GPSPosition; //"3258.1187N,09642.9508W";
	string GPSTime;  // 15:23:51
	string GPSDate;  // 2019-05-29
	string GPSAltitute; // 197.0(m)
	string GPSSpeed; // 37.0(km/h)

	// map those elements from configure database to local variables
	GPS->LocalMap("ID", &ID);
	GPS->LocalMap("BaudRate", &baudrate);
	GPS->LocalMap("Type", &GPSType);

	// map those elements from service data to local variables. This is the special implementation on PIC
	GPS->LocalMap("position", &GPSPosition);
	GPS->LocalMap("altitute", &GPSAltitute);
	GPS->LocalMap("time", &GPSTime);
	GPS->LocalMap("date", &GPSDate);
	GPS->LocalMap("speed", &GPSSpeed);

	// add the local variables to list of service data
	GPS->AddToServiceData("position", &GPSPosition);
	GPS->AddToServiceData("altitute", &GPSAltitute);
	GPS->AddToServiceData("time", &GPSTime);
	GPS->AddToServiceData("date", &GPSDate);
	GPS->AddToServiceData("speed", &GPSSpeed);

	if (!GPS->StartService())
	{
		cerr << endl << "Cannot setup the connection to the main module. Error=" << GPS->m_err << endl;
		return -1;
	}

	long myChannel = GPS->GetServiceChannel("");
	string myTitle = GPS->GetServiceTitle(myChannel);
	cout << "Service provider " << myTitle << " is up at " << myChannel << endl;

	// The demo of send a command
	GPS->SndCmd("Hello from " + myTitle + " module.", "1");

	size_t command;
	struct timeval tv;
	string msg;

	// Main loop that provide the service
	while (true)
	{
		// works in blocking read mode, the service data is provided by pic
		command = GPS->ChkNewMsg(CTL_BLOCKING);

		gettimeofday(&tv, nullptr);
		msg = GPS->GetRcvMsg();
		cout << "\r(" << myTitle << ")" << getDateTime(tv.tv_sec, tv.tv_usec) << " : "; 

		// if CMD_DOWN is sent from others, no return
		if (command == CMD_DOWN)
		{
			if (GPS->m_MsgChn == myChannel)
			{
				cout << myTitle << " is down by the command from main module." << endl;
				break;
			}
			else
				cout << " gets message that " << GPS->GetServiceTitle(GPS->m_MsgChn) << "is down." << endl;
		}
		else if (command == CMD_COMMAND)
		{
			cout << "Get a command '" << msg << "' from " << GPS->m_MsgChn << endl;
			continue;
		}
		else if (command == CMD_SERVICEDATA)
		{
			cout << "(GPS) position=" << GPSPosition << ", altitute=" << GPSAltitute
				<< ", speed=" << GPSSpeed << ", time=" << GPSTime << ", date=" << GPSDate << flush;
			GPS->PublishServiceData();
		}

		if (last_baudrate != baudrate)
		{
			cout << " gets configured as ID=" << ID
				<< " baud rate=" << baudrate
				<< " and type=" << GPSType << ".\n";
			last_baudrate = baudrate;
			continue;
		}

		GPS->WatchdogFeed();
	}
}
