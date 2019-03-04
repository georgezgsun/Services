//#include <QCoreApplication>
#include <ctime>
#include <chrono>
#include <iostream>
#include <unistd.h>
#include <sys/time.h>
#include <string.h>
#include "ServiceUtils.h"

using namespace std;

struct Onboard
{
    char cmd;
    char type;
    char title[10];
    pid_t pid;
    pid_t ppid;
};

string current_date()
{
    time_t now = time(nullptr);
    struct tm tstruct;
    char buf[40];
    tstruct = *localtime(&now);
    //format: day DD-MM-YYYY
    strftime(buf, sizeof(buf), "%A %d/%m/%Y", &tstruct);
    return buf;
}
string current_time1()
{
    time_t now = time(nullptr);
    struct tm tstruct;
    char buf[40];
    tstruct = *localtime(&now);
    //format: HH:mm:ss
    strftime(buf, sizeof(buf), "%X", &tstruct);
    return buf;
}

string current_time()
{
	struct timeval tv;
	time_t nowtime;
	struct tm *nowtm;
	char tmbuf[64], buf[64];

    gettimeofday(&tv, nullptr);
	nowtime = tv.tv_sec;
	nowtm = localtime(&nowtime);
	strftime(tmbuf, sizeof tmbuf, "%Y-%m-%d %H:%M:%S", nowtm);
    snprintf(buf, sizeof buf, "%s.%06ld", tmbuf, tv.tv_usec);
    return buf;
}

bool sendPropertieList(string sTitle)
{

};

int main(int argc, char *argv[])
{
//    QCoreApplication a(argc, argv);

    string msg;
    string tmp;
    char txt[255];
    char data[255];
    unsigned char dataLen = 0;

    time_t now;
    struct tm tstruct;
    clock_t nextSend = 0;
    int PreEvent = 120;
    int Chunk = 60;
    int TotalReceived = 0;

    string nameCam1 = "FrontCam";

    string serviceList [5] = {"SERVER", "GPS", "Radar", "Trigger", "FrontCam"};
    size_t serviceChannels [5] = {1,3,4,5,19};
    string DBKeywords [10] = {"PreEvent", "Chunk", "CamPath", "User", "PassWord", "Cloud Server", "WAP", "Luanguage", "Active Triggers", "Auto upload"};
    size_t DBTypes[10] = {1, 1, 0, 0, 0, 0, 1, 0, 0, 1};
    string stringDBValues[10] = {"120", "60", "rtsp://10.25.20.0/1/h264major", "Mark Richman", "noPassword", "50.24,54,54", "1", "English", "FLB SRN MIC LSB RLB", "1"};
    long watchdogs [5] = {0, 0, 0, 0, 0};

    ServiceUtils *launcher = new ServiceUtils(0, "SERVER", 1);
    if (!launcher->StartService())
    {
        cerr << endl << "Cannot launch the headquater." << endl;
        return -1;
    }

    char text[255];
    char DBText[255];
    size_t DBLength = 0;
    size_t type;
    size_t len = 0;
    size_t offset = 0;;
    pid_t pid;
    pid_t ppid;
    string sTitle;
    long logType;
    string logContent;
    struct timeval tv;

    for (int i = 0; i < 10; i ++)
    {
        memcpy(DBText + offset, &DBTypes[i], sizeof (size_t));
        offset += sizeof(size_t);

        if (DBTypes[i] == 0)
        {
            memcpy(DBText + offset, stringDBValues[i].c_str(), stringDBValues[i].length());
            offset += stringDBValues[i].length();
        }
        else
        {
            int n = stoi(stringDBValues[i]);
            memcpy(DBText + offset, &n, sizeof (n));
            offset += sizeof(n);
        }
    }
    DBLength = offset;

    while (1)
    {
        gettimeofday(&tv, nullptr);

        for (int i = 0; i < 5; i ++)
            if (watchdogs[i] & (tv.tv_sec > watchdogs[i]))
            {
                cout << "Warning: watcgdog " << i << " alert at " << tv.tv_sec << "." << tv.tv_usec << endl;
            }

        if (!launcher->RcvMsg(&text, &type, &len))
            continue;

        switch (type)
        {
        case CMD_ONBOARD :
            offset = 0;
            memcpy(&pid, text + offset, sizeof (pid));
            offset += sizeof (pid);
            memcpy(&ppid, text + offset, sizeof(ppid));
            offset += sizeof (ppid);
            sTitle.assign(text + offset);

            cout << "Service provider " << sTitle << " gets onboard at " << launcher->msgTS_sec << "." << launcher->msgTS_usec \
                 << " on channel " << launcher->msgChn << " with PID=" << pid << ", Parent PID=" << ppid << endl;

            // send back the service list first
            offset = 0;
            for (int i = 0; i < 5; i++)
            {
                memcpy(text + offset, &serviceChannels[i], sizeof(long));
                offset += sizeof(long);
                memcpy(text + offset, serviceList[i].c_str(), serviceList[i].length()+1);
                offset += serviceList[i].length() + 1;
            }
            launcher->SndMsg(text, CMD_LIST, offset, sTitle);

            // send back the database property list first
            offset = 0;
            for (int i = 0; i < 10; i ++)
            {
                memcpy(text + offset, &DBTypes[i], sizeof(size_t));
                offset += sizeof(size_t);
                len = DBKeywords[i].length() + 1;
                memcpy(text + offset, &DBKeywords[i], len);
                offset += len;
            }
            launcher->SndMsg(text, CMD_DATABASEINIT, offset, sTitle);

            // send back the database properties
            launcher->SndMsg(DBText, CMD_DATABASEQUERY, DBLength, sTitle);
                break;

        case CMD_LOG :
            offset = 0;
            memcpy(&logType, text, sizeof (logType));
            logContent.assign(text + sizeof (logType));
            cout << "LOG from " << launcher->msgChn << " at " << launcher->msgTS_sec << "." << launcher->msgTS_usec \
                 << " " << logType << " : " << logContent << endl;
            break;

        case CMD_WATCHDOG :
            watchdogs[0] = launcher->msgTS_sec + 5;
            cout << "Get FeedWatchdog notice from " << launcher->msgChn << " at " << launcher->msgTS_sec << "." << launcher->msgTS_usec \
                 << ". Watch dog will be alert in 5s." << endl;
            break;

        case CMD_DATABASEQUERY :
            launcher->SndMsg(DBText, CMD_DATABASEQUERY, DBLength, sTitle);
            cout << "Got database query request from " << launcher->msgChn << " at " << launcher->msgTS_sec << "." << launcher->msgTS_usec \
                 << ". Reply the latest database select result." << endl;
            break;

        deault:
            cout << "Got message from " << launcher->msgChn << " at " << launcher->msgTS_sec << "." << launcher->msgTS_usec \
                 << " with type of " << type << " and length of " << len << endl;
        }
    }

  //  return a.exec();
}
