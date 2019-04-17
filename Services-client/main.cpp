#include <ctime>
#include <chrono>
#include <iostream>
#include <unistd.h>
#include <sys/time.h>
#include <string.h>
#include "../Services/ServiceUtils.h"

using namespace std;

int main(int argc, char *argv[])
{
	ServiceUtils *tester = new ServiceUtils(argc, argv);

	//	{ "PreEvent", "Chunk", "CamPath", "User", "PassWord", "Cloud Server", "WAP", "Luanguage", "Active Triggers", "Auto upload" };
	//  { 1, 1, 0, 0, 0, 0, 1, 0, 0, 1 }; // 1 for int, 0 for string
	//  { "120", "60", "rtsp://10.25.20.0/1/h264major", "Mark Richman", "noPassword", "50.24.54.54", "1", "English", "FLB SRN MIC LSB RLB", "1" };
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

	tester->dbMap("PreEvent", &PreEvent);
	tester->dbMap("PreExent", &PreEvent2);
	tester->dbMap("Chunk", &Chunk);
	tester->dbMap("CamPath", &CamPath);
	tester->dbMap("User", &User);
	tester->dbMap("PassWord", &Password);
	tester->dbMap("Cloud Server", &CloudServer, 0);
	tester->dbMap("WAP", &WAP, 4);
	tester->dbMap("Luanguage", &Luanguage, 0);
	tester->dbMap("Active Triggers", &ActiveTriggers, 0);
	tester->dbMap("Auto upload", &AutoUpload);

	if (!tester->StartService())
	{
		cerr << endl << "Cannot setup the connection to the headquater. Error=" << tester->m_err << endl;
		return -1;
	}
	tester->SndMsg("Hello from a client.", "1");

	size_t typeMsg;
	struct timeval tv;
	struct tm *nowtm;
	char tmbuf[64], datetime[64];
	int lastPreEvent = 1;
	int count = 5;

	string msg;
	while (true)
	{
		gettimeofday(&tv, nullptr);
		nowtm = localtime(&tv.tv_sec);
		strftime(tmbuf, sizeof tmbuf, "%Y-%m-%d %H:%M:%S", nowtm);
		snprintf(datetime, sizeof datetime, "%s.%06ld", tmbuf, tv.tv_usec);
		typeMsg = tester->ChkNewMsg();
		if (typeMsg)
		{
			msg = tester->GetRcvMsg();
			cout << datetime << " : Received messages '" << msg << "' of type " << typeMsg << " from " << tester->m_MsgChn << endl;
		}

		if (lastPreEvent != PreEvent)
		{
			cout << datetime << ":\n";
			cout << "PreEvent is " << PreEvent << ".\n";
			cout << "Chunk is " << Chunk << ".\n";
			cout << "Camera path is '" << CamPath << "'.\n";
			cout << "User is '" << User << "'.\n";
			cout << "Password is '" << Password << "'.\n";
			cout << "Cloud Server is '" << CloudServer << "'.\n";
			cout << "WAP is " << WAP << ".\n";
			cout << "Luanguage is '" << Luanguage << "'.\n";
			cout << "Active Triggers are '" << ActiveTriggers << "'.\n";
			cout << "Auto Upload is " << AutoUpload << ".\n";
			cout << "PreExent is " << PreEvent2 << ".\n";

			lastPreEvent = PreEvent;
		}

		if (tester->WatchdogFeed())
		{
			cout << datetime << " : Heartbeat..\n";
			count--;
			if (!count)
			{
				cout << "Stopped\n";
				break;
			}

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
				break;

			case 1:
				tester->dbQuery();
				break;
			}
		}
	}
}
