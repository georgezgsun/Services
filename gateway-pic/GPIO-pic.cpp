#include <ctime>
#include <chrono>
#include <iostream>
#include <unistd.h>
#include <sys/time.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <string.h>
#include <pthread.h>

#include "../Services/ServiceUtils.h"

using namespace std;

// A pic between pic and GPS, Trigger, and Radar modules in Roswell
// 1. There are two threads in this module. 
// 2. One thread periodicly sends pic query commands to picRoswell who will reply the outputs from the pic.
// 3. The main thread receives the pic outputs and publishes them to subscribers like GPS, Trigger, and Radar.
// 4. The main thread reads from message queue in blocking mode

static long t_Chn; // global variable to hold the service channel
static int t_ID;

// pic timer thread periodically (every 50ms) sends a wakeup message to the main thread
void *picTimer(void * my_void_ptr)
{
//	key_t t_Key = getpid();
//	int t_ID = msgget(t_Key, 0444 | IPC_CREAT);  // message queue for read-only
	
	struct MsgBuf t_buf;
	t_buf.rChn = t_Chn;
	t_buf.sChn = t_Chn;
	t_buf.len = 0;
	t_buf.type = CMD_NULL;
	memset(t_buf.mText, 0, sizeof(t_buf.mText));
	size_t HeaderLength = sizeof(t_buf.sChn) + sizeof(t_buf.sec) + sizeof(t_buf.usec) + sizeof(t_buf.type) + sizeof(t_buf.len);

	struct timespec tim = { 0, 50000000L }; // 1ms = 1000000ns
	while (true)
	{
		// sleep for 50ms
		//this_thread::sleep_for(chrono::milliseconds(50));
		clock_nanosleep(CLOCK_REALTIME, 0, &tim, NULL);
		if (msgsnd(t_ID, &t_buf, HeaderLength, IPC_NOWAIT))
			printf("(Critical error) Unable to send the message to queue %d.\n", t_ID);
	};
}

class GPIOpic : public ServiceUtils
{
public:
	int t_ID;
	GPIOpic(int argc, char* argv[])
	{
		ServiceUtils(argc, argv);
		t_ID = m_ID;
	}

	bool publishGPIOData()
	{
		// broadcast the service data get from the pic
		struct timeval tv;
		gettimeofday(&tv, nullptr);

		m_buf.sChn = m_Chn;
		m_buf.sec = tv.tv_sec;
		m_buf.usec = tv.tv_usec;
		m_buf.type = CMD_SERVICEDATA;
		memcpy(m_ServiceData, m_buf.mText, sizeof(m_ServiceData));

		// The service provider will broadcast the service data to all its clients
		for (size_t i = 0; i < m_TotalClients; i++)
		{
			m_buf.rChn = m_Clients[i];
			if (msgsnd(m_ID, &m_buf, m_buf.len + m_HeaderLength, IPC_NOWAIT))
			{
				m_err = errno;
				printf("\n%s Unable to publish the GPIOpic data to channel %d with error code %d.\n", getDateTime(tv.tv_sec, tv.tv_usec).c_str(), m_ID, m_err);
				return false;
			}
		}

		for (size_t i = 0; i < m_buf.len; i++)
		{
			if (m_buf.mText[i] < 32 || m_buf.mText[i] > 128)
				printf("(%02hx)", m_buf.mText[i]);
			else 
				printf("%c", m_buf.mText[i]);
		}
		return true;
	}
};

int main(int argc, char *argv[])
{
	// These are the available commands that need to be sent to the pic
	string readSerialNumber = "B20525000100";
	string readGPS = "B2032200";
	string readRadar = "B2032300";
	string readTrigger = "B2032400";
	string readFirmwareVersion = "B20525070100";
	string readHardwareVersion = "B20525080100";
	string turnLEDOn = "B20541000100";
	string turnLEDOff = "B20541000000";
	string turnMIC1On = "B2063200020100";
	string turnMIC1Off = "B2063200020000";
	string turnMIC2On = "B2063200010100";
	string turnMIC2Off = "B2063200010100";
	string setHeartbeatOn = "B204F10100";
	string setHeartbeatOff = "B204F10000";
	string getSystemIssueFlags = "B2032700";

	// start and initialize the pic service
	GPIOpic *pic = new GPIOpic(argc, argv);
	if (!pic->StartService())
	{
		cerr << endl << "Cannot setup the connection to the main module. Error=" << pic->m_err << endl;
		return -1;
	}

	long myChannel = pic->GetServiceChannel("");
	string myTitle = pic->GetServiceTitle(myChannel);
	t_Chn = myChannel; // global shared with sub thread
	t_ID = pic->t_ID;
	cout << "Service provider " << myTitle << " is up at " << myChannel << endl;

	// The demo of send a command
	pic->SndCmd("Hello from " + myTitle + " module.", "1");

	size_t command;
	struct timeval tv;
	string msg;

	key_t g_Key = 0x04d8; // using VID of pic as the key
	struct MsgBuf g_buf; // buffer for pic message queue
	int g_ID = msgget(g_Key, 0666 | IPC_CREAT); // setup the message queue by which pic can be reached
	int g_HeaderLength = sizeof(g_buf.sChn) + sizeof(g_buf.sec) + sizeof(g_buf.usec) + sizeof(g_buf.type) + sizeof(g_buf.len);
	printf("I can be reached using message queue of ID %d and of key %d.\n", g_ID, g_Key);

	g_buf.rChn = g_Key;
	g_buf.sChn = myChannel;
	g_buf.len = 0;
	g_buf.type = 0;
	g_buf.sec = 0;
	g_buf.usec = 0;
	memset(g_buf.mText, 0, sizeof(g_buf.mText));
	if (msgsnd(g_ID, &g_buf, g_buf.len + g_HeaderLength, IPC_NOWAIT))
		printf("\n(Critical error) Unable to send the message to queue %d.\n", g_ID);

	// Start the timer thread
	pthread_t thread;
	int rst = pthread_create(&thread, NULL, picTimer, nullptr);
	if (rst)
	{
		printf("Cannot create the timer thread. %d\n", rst);
		return false;
	}

	// Main loop that provide the service
	while (true)
	{
		// works in blocking read mode, the service data is provided by pic
		command = pic->ChkNewMsg(CTL_BLOCKING);

		gettimeofday(&tv, nullptr);
		msg = pic->GetRcvMsg();

		// if CMD_DOWN is sent from others, no return
		if (command == CMD_DOWN)
		{
			if (pic->m_MsgChn == myChannel)
			{
				cout << myTitle << " is down by the command from main module." << endl;
				break;
			}
			else
				cout << " gets message that " << pic->GetServiceTitle(pic->m_MsgChn) << "is down." << endl;
			continue;
		}

		if (command == CMD_COMMAND)
		{
			pic->Log("Gets a command " + msg + " from " + to_string(pic->m_MsgChn));
			cout << myTitle << " gets a command '" << msg << "' from " << pic->m_MsgChn << endl;

			string cmd{};
			if (!msg.compare("TurnLEDOn"))
				cmd = turnLEDOn;
			else if (!msg.compare("TurnLEDOff"))
				cmd = turnLEDOff;
			else if (!msg.compare("TurnMicOn"))
				cmd = turnMIC1On + turnMIC2On;
			else if (!msg.compare("TurnMICOff"))
				cmd = turnMIC1Off + turnMIC2Off;
			else if (!msg.compare("ReadGPS"))
				cmd = readGPS;
			else if (!msg.compare("ReadTrigger"))
				cmd = readTrigger;
			else if (!msg.compare("ReadRadar"))
				cmd = readRadar;
			else if (!msg.compare("ReadVersions"))
				cmd = readFirmwareVersion + readHardwareVersion + readSerialNumber;

			if (!cmd.empty())
			{
				strcpy(g_buf.mText, cmd.c_str());
				g_buf.len = cmd.length() + 1;
				if (msgsnd(g_ID, &g_buf, g_buf.len + g_HeaderLength, IPC_NOWAIT))
					pic->Log("Cannot send command " + cmd + " to the picRoswell process.", 670);
			}
			else
				pic->Log("Received an unkown command " + cmd + " from " + to_string(pic->m_MsgChn), 680);
			continue;
		}
		
		// This is the data from picRoswell
		if (command == CMD_SERVICEDATA && pic->m_MsgChn == myChannel)
		{
			cout << "\r(" << myTitle << ")" << getDateTime(tv.tv_sec, tv.tv_usec) << " : ";
			pic->publishGPIOData();
		}

		pic->WatchdogFeed();
	}
}
