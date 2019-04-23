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

	int PreEvent, lastPreEvent;
	int PreEvent2;
	int Chunk, lastChunk;
	string CamPath, lastCamPath;
	string User, lastUser;
	string Password, lastPassword;
	string CloudServer, lastCloudServer;
	int WAP, lastWAP;
	string Luanguage, lastLuanguage;
	string ActiveTriggers, lastActiveTriggers;
	int AutoUpload, lastAutoUpload;
	string position = "3258.1187N,09642.9508W";
	int height = 202;
	int time;

	tester->LocalMap("PreEvent", &PreEvent);
	tester->LocalMap("PreExent", &PreEvent2);
	tester->LocalMap("Chunk", &Chunk);
	tester->LocalMap("CamPath", &CamPath);
	tester->LocalMap("User", &User);
	tester->LocalMap("PassWord", &Password);
	tester->LocalMap("Cloud Server", &CloudServer, 0);
	tester->LocalMap("WAP", &WAP, 4);
	tester->LocalMap("Luanguage", &Luanguage, 0);
	tester->LocalMap("Active Triggers", &ActiveTriggers, 0);
	tester->LocalMap("Auto upload", &AutoUpload);

	tester->AddToServiceData("position", &position);
	tester->AddToServiceData("latitute", &height);
	tester->AddToServiceData("epic", &time);

	if (!tester->StartService())
	{
		cerr << endl << "Cannot setup the connection to the main module. Error=" << tester->m_err << endl;
		return -1;
	}
	tester->SndCmd("Hello from a client.", "1");

	long myChannel = tester->GetServiceChannel("");
	string myTitle = tester->GetServiceTitle(myChannel);
	cout << "Service provider " << myTitle << " is up at " << myChannel << endl;

	if (myChannel == 19)
	{
		tester->ServiceSubscribe("GPS");
		tester->ServiceSubscribe("Trigger");
		tester->ServiceSubscribe("Radar");
		tester->SndCmd("Hello from " + myTitle, "GPS");
		tester->SndCmd("Hello from " + myTitle, "Trigger");
		tester->SndCmd("Hello from " + myTitle, "Radar");
	}

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

		if (lastPreEvent != PreEvent)
		{
			cout << datetime << " : PreEvent is changed to " << PreEvent << ".\n";
			lastPreEvent = PreEvent;
		}

		if (lastChunk != Chunk)
		{
			cout << datetime << " : Chunk is changed to " << Chunk << ".\n";
			lastChunk = Chunk;
		}

		if (lastCamPath != CamPath)
		{
			cout << datetime << " : Camera path is changed to '" << CamPath << "'.\n";
			lastCamPath = CamPath;
		}

		if (lastUser != User)
		{
			cout << datetime << " : User is changed to '" << User << "'.\n";
			lastUser = User;
		}

		if (lastPassword != Password)
		{
			cout << datetime << " : Password is changed to '" << Password << "'.\n";
			lastPassword = Password;
		}

		if (lastCloudServer != CloudServer)
		{
			cout << datetime << " : Cloud Server is changed to '" << CloudServer << "'.\n";
			lastCloudServer = CloudServer;
		}

		if (lastWAP != WAP)
		{
			cout << datetime << " : WAP is changed to " << WAP << ".\n";
			lastWAP = WAP;
		}

		if (lastLuanguage != Luanguage)
		{
			cout << datetime << " : Luanguage is changed '" << Luanguage << "'.\n";
			lastLuanguage = Luanguage;
		}

		if (lastActiveTriggers != ActiveTriggers)
		{
			cout << datetime << " : Active Triggers are changed to '" << ActiveTriggers << "'.\n";
			lastActiveTriggers = ActiveTriggers;
		}
			
		if (lastAutoUpload != AutoUpload)
		{
			cout << datetime << " : Auto Upload is changed to " << AutoUpload << ".\n";
			lastAutoUpload = AutoUpload;
		}

		if (tester->WatchdogFeed())
		{
			count--;
			cout << datetime << " : Count down " << count << endl;

			switch (count)
			{
			case 4:
				PreEvent = 60;
				Chunk = 30;
				ActiveTriggers = "MIC";
				tester->dbUpdate();
				break;

			case 3:
				tester->dbQuery();
				break;

			case 2:
				PreEvent = 30;
				Chunk = 20;
				ActiveTriggers = "BRK";
				tester->dbUpdate();
				break;

			case 1:
				tester->dbQuery();
				if (myChannel != 19)
					count = 5;
				break;

			default:
				// Send out a requist that this service is going to be down
				tester->SndMsg(nullptr, CMD_DOWN, 0, 1);
				cout << datetime << " : Send down request to main module.\n";
				count = 0;
			}
		}
	}
}
