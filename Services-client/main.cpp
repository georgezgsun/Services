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
	if (!tester->StartService())
	{
		cerr << endl << "Cannot launch the tester. Error=" << tester->m_err << endl;
		return -1;
	}
	tester->SndMsg("Hello from a client.", "1");

	//	{ "PreEvent", "Chunk", "CamPath", "User", "PassWord", "Cloud Server", "WAP", "Luanguage", "Active Triggers", "Auto upload" };
	//  { 1, 1, 0, 0, 0, 0, 1, 0, 0, 1 }; // 1 for int, 0 for string
	//  { "120", "60", "rtsp://10.25.20.0/1/h264major", "Mark Richman", "noPassword", "50.24,54,54", "1", "English", "FLB SRN MIC LSB RLB", "1" };
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

	char txt[64];
	size_t typeMsg;
	size_t len;
	struct timeval tv;
	struct tm *nowtm;
	char tmbuf[64], datetime[64];
	int lastPreEvent = 1;

	string msg;
	while (true)
	{
		gettimeofday(&tv, nullptr);
		nowtm = localtime(&tv.tv_sec);
		strftime(tmbuf, sizeof tmbuf, "%Y-%m-%d %H:%M:%S", nowtm);
		snprintf(datetime, sizeof datetime, "%s.%06ld", tmbuf, tv.tv_usec);
		typeMsg = tester->RcvMsg();
		if (typeMsg)
		{
			msg = tester->GetRcvMsg();
			cout << datetime << " : Received messages '" << msg << "' of type " << typeMsg << " from " << tester->m_MsgChn << endl;
		}

		if (lastPreEvent != PreEvent)
		{
			cout << datetime << ":\n";
			cout << "PreEvent is " << PreEvent << " now\n";
			cout << "Chunk is " << Chunk << " now\n";
			cout << "User is " << User << " now\n";
			cout << "Password is " << Password << " now\n";
			cout << "Cloud Server is " << WAP << " now\n";
			cout << "WAP is " << PreEvent << " now\n";
			cout << "Luanguage is " << Luanguage << " now\n";
			cout << "Active Triggers are " << ActiveTriggers << " now\n";
			cout << "Auto Upload is " << AutoUpload << " now\n";
			cout << "PreExent is " << PreEvent2 << " now\n";

			lastPreEvent = PreEvent;
		}

	}
	//struct timeval tv;
	//time_t nowtime;
	//struct tm *nowtm;
	//char tmbuf[64], buf[64];

	//int chunk;
	//int trigger;
	//string msg;
	//int i = 11;
	//while (true)
	//{
	//	gettimeofday(&tv, nullptr);
	//	nowtm = localtime(&tv.tv_sec);
	//	strftime(tmbuf, sizeof tmbuf, "%Y-%m-%d %H:%M:%S", nowtm);
	//	snprintf(buf, sizeof buf, "%s.%06ld", tmbuf, tv.tv_usec);

	//	if ( tester->RcvMsg();
	//	if (msg.length() > 0)
	//	{
	//		cout << buf << " : Received " << msg << " from " << tester->m_MsgChn << endl;

	//		if (i > 20) i = 11;
	//		cout << "Service list: " << mq->GetServiceTitle(i) << " " << i++ << endl;
	//		if (msg == "end") break;

	//		cout << "Database query FrontCam=" << frontCam << ", PreEvent=" << PreEvent << ", chunk=" << chunk << endl;

	//		TotalReceived++;
	//		if (TotalReceived == 3)
	//		{
	//			mq->dbAssign("PreEvent", &PreEvent);
	//			mq->dbAssign("FrontCam", &frontCam);
	//			mq->dbAssign("Chunk", &chunk);
	//		}
	//	}
	//}

}
