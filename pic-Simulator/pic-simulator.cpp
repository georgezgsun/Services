/*******************************************************
 PIC reader for Roswell based on HIDAPI

 George Sun
 A-Concept Inc.

 06/05/2019
********************************************************/

#include <cstdio>
#include <stdio.h>
#include <wchar.h>
#include <string.h>
#include <stdlib.h>
#include <sys/time.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/types.h>
#include <dirent.h>
#include <iostream>
#include <unistd.h>

using namespace std;

string getDateTime(time_t tv_sec, time_t tv_usec)
{
	struct tm *nowtm;
	char tmbuf[64], buf[64];

	nowtm = localtime(&tv_sec);
	strftime(tmbuf, sizeof tmbuf, "%Y-%m-%d %H:%M:%S", nowtm);
	snprintf(buf, sizeof buf, "%s.%06ld", tmbuf, tv_usec);
	return buf;
}

// structure that holds the whole packet of a message in message queue
struct MsgBuf
{
	long rChn;  // Receiver type
	long sChn; // Sender Type
	long sec;    // timestamp sec
	long usec;   // timestamp usec
	size_t type; // type of this property, first bit 0 indicates string, 1 indicates interger. Any value greater than 32 is a command.
	size_t len;   //length of message payload in mText
	char mText[255];  // message payload
};

int main(int argc, char* argv[])
{
	int res;
#define MAX_STR 255
	unsigned short VID = 0x04d8;
	unsigned short PID = 0xf2bf;
	unsigned char buf[255];

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
	string GPSReply = "GPS3258.1157,N,09642.9383,W,201.3,40.014,000.10,100200,120619.";
	string TriggerReply = "Tri--^-----^------------"; //[1] is the length, 0-5 0 for low, 1 for high
	string InfoReply = "infGP012367x"; // the serial number

	key_t m_key = VID;  // using the VID as the key for message queue
	int s_ID = 0;
	int m_ID = msgget(m_key, 0666 | IPC_CREAT);
	printf("Start PIC simulator.\n");
	printf("I can be reached at message queue of ID %d or of key %d.\n", m_ID, m_key);

	struct MsgBuf m_buf;
	long sChn;
	int m_HeaderLength = sizeof(m_buf.sChn) + sizeof(m_buf.sec) + sizeof(m_buf.usec) + sizeof(m_buf.type) + sizeof(m_buf.len);
	string cmd;
	string tStamp;

	struct timespec tim = { 0, 999999L }; // sleep for almost 1ms
	struct timeval tv;
	gettimeofday(&tv, NULL);

	memset(m_buf.mText, 0, sizeof(m_buf.mText));  // fill 0 before reading, make sure no garbage left over
	memset(buf, 0, sizeof(buf));
	while (true)
	{
		// read from message queue for any channels in blocking mode
		int l = msgrcv(m_ID, &m_buf, sizeof(m_buf), 0, 0);
		l -= m_HeaderLength;
		if (l >= 0)
		{
			// using rChn as the message queue ID
			if (s_ID != m_buf.rChn)
			{
				s_ID = m_buf.rChn;
				printf("\nGet a new message queue ID %d.\n", s_ID);
			}

			// get the command from the message queue
			if (m_buf.mText[0] == 'B')
				cmd.assign(m_buf.mText);

			// update the receiving channel
			sChn = m_buf.sChn;
		}

		gettimeofday(&tv, NULL);
		tStamp = getDateTime(tv.tv_sec, tv.tv_usec);

		// If the command is empty
		if (cmd.empty())
			continue;

		// print the reply simulation
		printf("\r%s (%s): ", tStamp.c_str(), cmd.c_str()); // print the header with timestamp

		if (cmd.length() > 10)
		{
			strcpy(m_buf.mText, GPSReply.c_str());
			m_buf.mText[1] = GPSReply.length() + 2;
			m_buf.mText[2] = 0x22;
			m_buf.mText[48] = tStamp.c_str()[11];
			m_buf.mText[49] = tStamp.c_str()[12];
			m_buf.mText[50] = tStamp.c_str()[14];
			m_buf.mText[51] = tStamp.c_str()[15];
			m_buf.mText[52] = tStamp.c_str()[17];
			m_buf.mText[53] = tStamp.c_str()[18];
		}
		else if (!cmd.compare(readTrigger))
		{
			strcpy(m_buf.mText, TriggerReply.c_str());
			m_buf.mText[1] = 23;
			m_buf.mText[2] = 0x24;
			m_buf.mText[4] = tStamp.c_str()[18];
			for (int i = 0; i < m_buf.mText[1]; i++)
				if (m_buf.mText[i + 3] == '-')
					m_buf.mText[i + 3] = 0;
				else
					m_buf.mText[i + 3] = 1;
		}
		else if (!cmd.compare(readSerialNumber))
		{
			strcpy(m_buf.mText, InfoReply.c_str());
			m_buf.mText[1] = 10;
			m_buf.mText[2] = 0x25;
		}

		for (int i = 3; i < m_buf.mText[1] + 1; i++)
			if (m_buf.mText[i] < 32 || m_buf.mText[i] > 128)
				printf("%02hx ", m_buf.mText[i]);
			else
				printf("%c", m_buf.mText[i]);

		m_buf.sec = tv.tv_sec;
		m_buf.usec = tv.tv_usec;
		m_buf.type = 11; // CMD_SERVICEDATA
		m_buf.rChn = sChn; // reply back to the sender's channel
		m_buf.sChn = PID;
		m_buf.len = m_buf.mText[1] + 2; // the length of the pic report plus 0xB2 and len
		m_buf.mText[m_buf.len] = 0; // put a /0 at the end. This may help to convert it into a string.

		if (s_ID && msgsnd(s_ID, &m_buf, m_buf.len + m_HeaderLength, IPC_NOWAIT))
		{
			printf("\n%s (Critical error) Unable to send the message to queue %d. Message is %s.\n",
				getDateTime(tv.tv_sec, tv.tv_usec).c_str(), s_ID, m_buf.mText);
			s_ID = 0;
		}
		cmd.clear();
	}

	return 0;
}

