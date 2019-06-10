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

// A GPIO module that talks with embeded picRoswell to get raw data for GPS, Trigger, and Radar modules in Roswell
// 1. There are two threads in this module. 
// 2. The sub thread periodicly sends pic query commands (readGPS, readTrigger, and readRadar) to picRoswell who will reply the outputs from the pic.
// 3. The main thread receives the pic outputs and publishes them to subscribers like GPS, Trigger, and Radar.
// 4. The main thread reads from message queue in blocking mode

static int t_Chn; // global variable to hold my service channel 
static int m_ID; //  global variable to hold the ID of the main message queue
static int t_ID;  // global variable to hold the ID of the message queue to picRoswell
bool RadarOn = false; // a control of get Radar status. True is on, false is off;

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

// pic timer thread periodically (every 50ms) sends a wakeup message to the main thread
void *picTimer(void * my_void_ptr)
{
	struct MsgBuf t_buf;
	t_buf.rChn = m_ID; // tell the picRoswell to reply using this message queue
	t_buf.sChn = t_Chn; // tell the picRoswell to reply on this channel
	t_buf.len = 0;
	t_buf.type = CMD_NULL;
	memset(t_buf.mText, 0, sizeof(t_buf.mText));
	size_t HeaderLength = sizeof(t_buf.sChn) + sizeof(t_buf.sec) + sizeof(t_buf.usec) + sizeof(t_buf.type) + sizeof(t_buf.len);

	struct timespec tim = { 0, 250000000L }; // 1ms = 1000000ns
	int count = 0;
	string cmd;
	while (true)
	{
		// sleep for 250ms
		clock_nanosleep(CLOCK_REALTIME, 0, &tim, NULL);
		cmd = readTrigger; // read the trigger every 250ms 
		count++;
		if (count >= 4)
			count = 0;
		if (count == 1)
			cmd.append(readGPS); // read GPS every 1000ms
		if (count == 3 && RadarOn)
			cmd.append(readRadar);  // read Radar every 1000ms if required

		strcpy(t_buf.mText, cmd.c_str()); // put the cmd content into the buffer
		t_buf.len = cmd.length() + 1;
		if (msgsnd(t_ID, &t_buf, t_buf.len + HeaderLength, IPC_NOWAIT))
		{
			// Read off all messages in the queue when it is full
			count = 0;
			while (msgrcv(t_ID, &t_buf, sizeof(t_buf), 0, IPC_NOWAIT) > 0)
				count++;
			printf("The message queue %d with picRoswell is full. Read off %d staled messages from it.\n", 
				t_ID, count);
			count = 0;
		}
	};
}

class GPIOpic : public ServiceUtils
{
public:
	int t_ID;
	GPIOpic(int argc, char* argv[])
	{
		//ServiceUtils(argc, argv);

		// parse the arguments in JSON style
		string cmd;
		string arg;
		size_t offset;
		m_Key = getppid();
		m_Title.clear();
		m_Chn = 0;

		for (int i = 1; i < argc; i++)
		{
			cmd.assign(argv[i]);
			offset = cmd.find_first_of('=');

			// make it backward compatible with original position sensitive arguments
			if (offset == string::npos)
			{
				if (i == 1)
					cmd = "channel=" + cmd;
				else if (i == 2)
					cmd = "title=" + cmd;
				else
					cmd = "parentPID=" + cmd;
				offset = cmd.find_first_of('=');
			}
			arg = cmd.substr(offset + 1); // argument is the right side of '='
			cmd = cmd.substr(0, offset); // command is the left side of the '='

			if (!cmd.compare("title"))
				m_Title = arg;
			else if (!cmd.compare("channel"))
				m_Chn = atoi(arg.c_str());
			else if (!cmd.compare("parentPID"))
				m_Key = atoi(arg.c_str());
		}

		// check if all the required arguments are specified correctly
		if (m_Title.empty() || (m_Chn <= 0) || (m_Key <= 0))
		{
			fprintf(stderr, "Expected usage is %s [channel=service channel] [title=service title] [parentPID=ppid of main module].\n", argv[0]);
			exit(1);
		}

		//// setup the message queue
		//m_ID = msgget(m_Key, PERMS | IPC_CREAT);
		//if (m_ID == -1)
		//{
		//	perror("Message queue");
		//	m_err = -3;
		//}
		//fprintf(stderr, "(%s) MsgQue key: %d ID: %d\n", m_Title.c_str(), m_Key, m_ID);

		Init();
		t_ID = m_ID;

		//m_Clients[0] = 3;
		//m_Clients[1] = 4;
		//m_Clients[2] = 5;
		//m_TotalClients = 3;
	}

	bool publishGPIOData()
	{
		// broadcast the service data get from the pic
		struct timeval tv;
		size_t offset = 0;
		gettimeofday(&tv, nullptr);
		memset(m_ServiceData, 0, sizeof(m_ServiceData));
		m_ServiceDataLength = 0;

		// parse the pic reply
		if (m_buf.mText[2] == 0x22) // This is the reply of GPS
		{
			// position
			strcpy(m_ServiceData, "position");
			offset = strlen("position") + 1;
			m_ServiceData[offset++] = 0;	// specify it is a string
			for (int i = 3; i < 12; i++) // xxxx.xxxx
				m_ServiceData[offset++] = m_buf.mText[i];
			m_ServiceData[offset++] = m_buf.mText[13]; // N
			m_ServiceData[offset++] = m_buf.mText[14]; // ,
			for (int i = 15; i < 25; i++)
				m_ServiceData[offset++] = m_buf.mText[i];
			m_ServiceData[offset++] = m_buf.mText[26]; // W
			m_ServiceData[offset++] = 0; // end of a string

			// altitute
			strcpy(m_ServiceData + offset, "altitute");
			offset += strlen("altitute") + 1;
			m_ServiceData[offset++] = 0;	// specify it is a string
			for (int i = 28; i < 33; i++) // xxx.x
				m_ServiceData[offset++] = m_buf.mText[i];
			m_ServiceData[offset++] = 0; // end of a string

			// speed
			strcpy(m_ServiceData + offset, "speed");
			offset += strlen("speed") + 1;
			m_ServiceData[offset++] = 0;	// specify it is a string
			for (int i = 42; i < 46; i++) // xxx.x
				m_ServiceData[offset++] = m_buf.mText[i];
			m_ServiceData[offset++] = 0; // end of a string

			// time
			strcpy(m_ServiceData + offset, "time");
			offset += strlen("time") + 1;
			m_ServiceData[offset++] = 0;	// specify it is a string
			m_ServiceData[offset++] = m_buf.mText[48];
			m_ServiceData[offset++] = m_buf.mText[49];
			m_ServiceData[offset++] = ':';
			m_ServiceData[offset++] = m_buf.mText[50];
			m_ServiceData[offset++] = m_buf.mText[51];
			m_ServiceData[offset++] = ':';
			m_ServiceData[offset++] = m_buf.mText[52];
			m_ServiceData[offset++] = m_buf.mText[53];
			m_ServiceData[offset++] = 0; // end of a string

			// date
			strcpy(m_ServiceData + offset, "date");
			offset += strlen("date") + 1;
			m_ServiceData[offset++] = 0;	// specify it is a string
			m_ServiceData[offset++] = '2';
			m_ServiceData[offset++] = '0';
			m_ServiceData[offset++] = m_buf.mText[59];
			m_ServiceData[offset++] = m_buf.mText[60];
			m_ServiceData[offset++] = '-';
			m_ServiceData[offset++] = m_buf.mText[57];
			m_ServiceData[offset++] = m_buf.mText[58];
			m_ServiceData[offset++] = '-';
			m_ServiceData[offset++] = m_buf.mText[55];
			m_ServiceData[offset++] = m_buf.mText[56];
			m_ServiceData[offset++] = 0; // end of a string

			m_ServiceDataLength = offset;
		}
		else if (m_buf.mText[2] == 0x23) // this is the reply of Radar
		{
			// put a 0 at the place of crc to end the string
			offset = m_buf.mText[1];
			m_buf.mText[offset] = 0;

			strcpy(m_ServiceData, "Radar");
			offset = strlen("Radar") + 1;
			m_ServiceData[offset++] = 0;	// specify it is a string

			strcpy(m_ServiceData + offset, m_buf.mText + 3);
			offset += m_buf.mText[1] - 1; // +1 for the /0 at the end, -2 for the [0] and [1]

			m_ServiceDataLength = offset;
		}
		else if (m_buf.mText[2] == 0x24)  // Trigger
		{
			string triggers = "";
			for (int i = 3; i < m_buf.mText[1] - 1; i++)
			{
				if (m_buf.mText[i])
					triggers.append("^");
				else
					triggers.append("-");

				if ((i - 2) % 4 == 0)
					triggers.append(" ");
			}

			strcpy(m_ServiceData, "Trigger");
			offset = strlen("Trigger") + 1;
			m_ServiceData[offset++] = 0;	// specify it is a string
			strcpy(m_ServiceData + offset, triggers.c_str());
			offset += triggers.length() + 1;

			m_ServiceDataLength = offset;
		}
		else if (m_buf.mText[2] == 0x25) // serial number, firmware, hardware
		{
			// put a 0 at the place of crc to end the string
			offset = m_buf.mText[1];
			m_buf.mText[offset] = 0;

			string ID = "Serial Number";
			if (m_buf.mText[3] > 47 && m_buf.mText[3] < 58) // a digital
				ID = "Firmware Version";
			if (m_buf.mText[1] > 15) // long string
				ID = "Hardware Version";
			
			strcpy(m_ServiceData, ID.c_str());
			offset = ID.length() + 1;
			m_ServiceData[offset++] = 0;	// specify it is a string
			strcpy(m_ServiceData + offset, m_buf.mText + 3);
			offset += m_buf.mText[1] - 1; // +1 for the /0 at the end, -2 for the [0] and [1]

			m_ServiceDataLength = offset;
		}

		m_buf.sChn = m_Chn;
		m_buf.sec = tv.tv_sec;
		m_buf.usec = tv.tv_usec;
		m_buf.type = CMD_SERVICEDATA;
		m_buf.len = m_ServiceDataLength;
		memcpy(m_buf.mText, m_ServiceData, m_ServiceDataLength);

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

		//for (size_t i = 0; i < m_ServiceDataLength; i++)
		//	if (m_ServiceData[i])
		//		printf("%c", m_ServiceData[i]);
		//	else
		//		printf(" ");

		return true;
	}
};

int main(int argc, char *argv[])
{
	// start and initialize the pic service
	GPIOpic *pic = new GPIOpic(argc, argv);
	if (!pic->StartService())
	{
		cerr << endl << "Cannot setup the connection to the main module. Error=" << pic->m_err << endl;
		return -1;
	}

	long myChannel = pic->GetServiceChannel("");
	string myTitle = pic->GetServiceTitle(myChannel);
	//myChannel = 10;
	t_Chn = myChannel; // store the channel so that the sub thread knows my channel
	m_ID = pic->t_ID;  // store the message queue ID so that the sub thread knows my message queue
	cout << "Service provider " << myTitle << " is up at " << myChannel << endl;

	// The demo of send a command
	pic->SndCmd("Hello from " + myTitle + " module.", "1");

	size_t command;
	struct timeval tv;
	string msg;

	key_t g_Key = 0x04d8; // using VID of pic as the key
	struct MsgBuf g_buf; // buffer for pic message queue
	int g_ID = msgget(g_Key, 0666 | IPC_CREAT); // setup the message queue by which pic can be reached
	t_ID = g_ID; // share the message ID with the sub thread in a goble variable
	int g_HeaderLength = sizeof(g_buf.sChn) + sizeof(g_buf.sec) + sizeof(g_buf.usec) + sizeof(g_buf.type) + sizeof(g_buf.len);
	printf("Trying to reach picRoswell via message queue of ID %d or of key %d.\n", g_ID, g_Key);

	g_buf.rChn = m_ID; // tell the picRoswell to reply using this message queue
	g_buf.sChn = myChannel; // tell the picRoswell to reply on this channel
	g_buf.len = 0;
	g_buf.type = 0;
	g_buf.sec = 0;
	g_buf.usec = 0;
	memset(g_buf.mText, 0, sizeof(g_buf.mText));
	//if (msgsnd(g_ID, &g_buf, g_buf.len + g_HeaderLength, IPC_NOWAIT))
	//	printf("\n(Critical error) Unable to send the message to queue %d.\n", g_ID);

	// Start the timer thread
	pthread_t thread;
	int rst = pthread_create(&thread, NULL, picTimer, NULL);
	if (rst)
	{
		printf("Cannot create the timer thread. %d\n", rst);
		return false;
	}

	// Main loop that provide the service
	int counter = 0;
	while (true)
	{
		// works in blocking read mode, the service data is provided by pic
		command = pic->ChkNewMsg(CTL_BLOCKING);

		gettimeofday(&tv, nullptr);
		msg = pic->GetRcvMsg();
		string cmd{};

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

			if (!msg.compare("TurnLEDOn"))
				cmd = turnLEDOn;
			else if (!msg.compare("TurnLEDOff"))
				cmd = turnLEDOff;
			else if (!msg.compare("TurnMicOn"))
				cmd = turnMIC1On + turnMIC2On;
			else if (!msg.compare("TurnMICOff"))
				cmd = turnMIC1Off + turnMIC2Off;
			else if (!msg.compare("RadarOn"))
				RadarOn = true;
			else if (!msg.compare("RadarOff"))
				RadarOn = false;
			else if (!msg.compare("ReadGPS"))
				cmd = readGPS;
			else if (!msg.compare("ReadTrigger"))
				cmd = readTrigger;
			else if (!msg.compare("ReadRadar"))
				cmd = readRadar;
			else if (!msg.compare("ReadVersions"))
				cmd = readFirmwareVersion + readHardwareVersion + readSerialNumber;
		}

		// This is the data from picRoswell
		//if (command == CMD_SERVICEDATA && pic->m_MsgChn == myChannel)
		if (command == CMD_SERVICEDATA)
		{
			//cout << "\r(" << myTitle << ")" << getDateTime(tv.tv_sec, tv.tv_usec) << " : ";
			pic->publishGPIOData();
		}

		if (!cmd.empty())
		{
			strcpy(g_buf.mText, cmd.c_str());
			g_buf.len = cmd.length() + 1;
			if (msgsnd(g_ID, &g_buf, g_buf.len + g_HeaderLength, IPC_NOWAIT))
				pic->Log("Cannot send command " + cmd + " to the picRoswell process.", 670);
		}

		pic->WatchdogFeed();
	}
}
