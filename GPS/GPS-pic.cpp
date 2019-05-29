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
	ServiceUtils *GPS = new ServiceUtils(argc, argv);

	int ID{ 0 };
	int baudrate{ 0 };
	char *myBuf;
	string GPSType;
	int last_baudrate{ 0 };
	string GPSPosition; //"3258.1187N,09642.9508W";
	string GPSTime;
	string GPSDate;
	string GPSAltitute;

	GPS->LocalMap("ID", &ID);
	GPS->LocalMap("BaudRate", &baudrate);
	GPS->LocalMap("Type", &GPSType);

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
	while (true)
	{
		command = GPS->ChkNewMsg(CTL_BLOCKING);

		gettimeofday(&tv, nullptr);
		msg = GPS->GetRcvMsg();
		cout << "\r(" << myTitle << ")" << getDateTime(tv.tv_sec, tv.tv_usec) << " : "; 

		// if CMD_DOWN is sent from others, no return
		if (command == CMD_DOWN)
		{
			cout << "Module is down by the command from main module." << endl;
			break;
		}
		else if (command == CMD_COMMAND)
		{
			cout << "Get a command '" << msg << "' from " << GPS->m_MsgChn << endl;
			continue;
		}
		else if (command == CMD_PUBLISHDATA)
		{
			int len = GPS->GetRcvMsgBuf(&myBuf);
			string tmp = GPS->GetRcvMsg();
			int offset = tmp.length() + 2;
			GPSPosition.assign(myBuf + offset);
			offset += GPSPosition.length() + 1;
			cout << tmp << "=" << GPSPosition << ", ";

			tmp.assign(myBuf + offset);
			offset += tmp.length() + 2;
			GPSAltitute.assign(myBuf + offset);
			offset += GPSAltitute.length() + 1;
			cout << tmp << "=" << GPSAltitute << ", ";

			tmp.assign(myBuf + offset);
			offset += tmp.length() + 2;
			GPSTime.assign(myBuf + offset);
			offset += GPSTime.length() + 1;
			cout << tmp << "=" << GPSTime << ", ";

			tmp.assign(myBuf + offset);
			offset += tmp.length() + 2;
			GPSDate.assign(myBuf + offset);
			offset += GPSDate.length() + 1;
			cout << tmp << "=" << GPSDate << flush;
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
