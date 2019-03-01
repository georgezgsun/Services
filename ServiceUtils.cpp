#include "ServiceUtils.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/time.h>

#define PERMS 0644
#define DEAFULTMSGQ "/tmp/msgq.txt"

Services::Services(key_t key, string myServiceTitle, long myServiceChannel)
{
    mChn = myServiceChannel;
    mTitle = myServiceTitle;
    mId = -1;
	err = 0;

    // Get the key for the message queue from given filename, if empty using default name
    if (key)
		mKey = key;
	else
        //mKey = getpid();
		mKey = 12345;
	
	if (mChn <= 0)
		err = -1;
	
	if (key < 0)
		err = -2;
};

bool Services::StartService()
{
    // initialize the internal buffers
    memset(&buf, 0, sizeof(buf));
    memset(&myClients, 0, sizeof(myClients));
    memset(&mySubscriptions, 0, sizeof(mySubscriptions));
    memset(&services, 0, sizeof(services));

    totalSubscriptions = 0;
    totalClients = 0;
    totalMessageSent = 0;
    totalMessageReceived =0;
    totalServices = 0;
    totalProperties = 0;

    Onboard report;

    err = -1; // 0 indicates no error;
    if (mChn <= 0)
        return false;

    err = -2; 
    if (mKey < 0)
        return false;

    err = -3; //perror("msgget");
    mId = msgget(mKey, PERMS | IPC_CREAT);
    if (mId == -1)
        return false;

    if (mChn == 1)
    {// Read off all previous messages
        do
        {
        } while (msgrcv(mId, &buf, sizeof(buf), 0, IPC_NOWAIT) > 0);
    }
    else
    {// Report onboard to the server
		char txt[255];
		pid_t pid = getpid();
		pid_t ppid = getppid();
		int len = sizeof(pid);
		memcpy(txt, &pid, len);
		memcpy(txt + len, &ppid, len);
		len += len;
		strcpy(txt + len, mTitle.c_str());
		len += mTitle.length() + 1;
		SndMsg(&txt, CMD_ONBOARD, len, "SERVER");
    }

    err = 0;
	printf("MsgQue ID : %ld\n", mId);
	return true;
}

Services::~Services()
{
	for (int i = 0; i < totalProperties; i++)
		delete pptr[i].ptr;
	
    // if it is the the server , delete the message queue
    if(1 == mChn)
        msgctl(mId, IPC_RMID, nullptr);
};

bool Services::SndMsg(string msg, string ServiceChannel)
{
	return SndMsg(msg.c_str(), 0, msg.length(), ServiceChannel);
};

// Send a packet with given length to specified service provider
bool Services::SndMsg(void *p, int type, int len, string ServiceChannel)
{
    err = -1;  // Message queue has not been opened
	if (mId == -1) 
		return false;
	
    buf.rChn = GetServiceChannel(string ServiceTitle);
	err = -2; // Cannot find the ServiceTitle in the list
    if (buf.rChn <= 0) 
		return false;
	
	err = -3;
	if (len > 255 || len < 0)
		return false;
	
	err = -4;
	if (type > 255 || type < 0)
		return false;

    struct timeval tv;
    gettimeofday(&tv, nullptr);
    buf.sChn = mChn;
    buf.sec = tv.tv_sec;
    buf.usec = tv.tv_usec;
    buf.len = len;
	buf.type = type;

    memcpy(buf.mText, p, len);
	int rst = msgsnd(mId, &buf, len + 5*sizeof (long), IPC_NOWAIT);
	err = errno;
    totalMessageSent++;
    return rst == 0;
}; 

bool Services::QueryServiceData(string ServiceTitle)
{
    return SndMsg(nullptr, CMD_QUERY, 0, ServiceTitle);
};

long Services::GetServiceChannel(string ServiceTitle)
{
    for (int i = 0; i < totalServices; i++)
        if (strncmp(serviceTitles[i].compare(ServiceTitle)) == 0)
            return serviceIndex[i];
    return 0;
};

string Services::GetServiceTitle(long ServiceChannel)
{
    for (int i = 0; i < totalServices; i++)
        if (serviceIndex[i] == ServiceChannel)
            return serviceTitles[i];
    return "";
};

bool Services::SubscribeService(string ServiceTitle)
{
	bool rst = SndMsg(nullptr, CMD_SUBSCRIBE, 0, ServiceTitle);
	long ServiceChannel = GetServiceChannel(ServiceTitle);
	if (ServiceChannel < 1)
		return false;

    for (int i = 0; i < totalClients; i++)
        if (myClients[i] == ServiceChannel)
            return rst;

    // add the new subscription to my list
    mySubscriptions[totalSubscriptions++] = ServiceChannel;
    return rst;
};

bool Services::BroadcastUpdate(void *p, int type, int dataLength)
{
	err = -1;  // Message queue has not been opened
	if (mId == -1) 
		return false;
		
	err = -3;
	if (dataLength > 255 || dataLength < 0)
		return false;
	
	err = -4;
	if (type > 255 || type < 0)
		return false;
	
    struct timeval tv;
    gettimeofday(&tv, nullptr);

    buf.sChn = mChn;
    buf.sec = tv.tv_sec;
    buf.usec = tv.tv_usec;
	buf.type = type;
    buf.len = dataLength;
	memset(buf.mText, 0, sizeof(buf.mText));  // clear the sending buffer
    memcpy(buf.mText, p, dataLength);

    int rst;
    for (int i = 0; i < totalClients; i++)
    {
        buf.rChn = myClients[i];
        rst = msgsnd(mId, &buf, dataLength + 5*sizeof (long), IPC_NOWAIT);
        err = errno;
        if (rst < 0) return false;
    }
    return true;
}; // Multicast the data stored at *p with dataLength and send it to every subscriber

// receive a message from sspecified ervice provider, like GPS, RADAR, TRIGGER. No AutoReply
string Services::RcvMsg()
{
    char b[255];
    string msg = "";
    int len;
	int type;
    if (RcvMsg(&b, &type, &len) && (type == 0) && (len > 0))
        msg.assign(buf.mText, len);

    return msg;
}

 // receive a packet from specified service provider. Autoreply all requests when enabled.
bool Services::RcvMsg(void *p, int *type, int *len); // receive a packet from specified service provider. Autoreply all requests 
{
    int i;
    int j;
    int n;
    struct timeval tv;
    int headerLen = 5 * sizeof(long);

	do
    {
        //buf.rChn = mChn;
		memset(buf.mText, 0, sizeof(buf.mText));  // fill 0 before reading, make sure no garbage left over
        int l = msgrcv(mId, &buf, sizeof(buf), mChn, IPC_NOWAIT) - headerLen;
        err = l;
        if (l <= 0)
            return false;
        totalMessageReceived++;
        err = 0;
		type = buf.type;
		len = buf.len;
        if (p != nullptr)
            memcpy(p, buf.mText, l);

		msgChn = buf.sChn; // the service type of last receiving message 
		msgTS_sec = buf.sec; // the time stamp in seconds of latest receiving message
		msgTS_usec = buf.usec;
        buf.len = len;

		// no autoreply for server
        if (mChn == 1)
            return true;
		
        // no auto reply for those normal receiving. type < 31 is normal return		
		if (type < 32))
			return true;
		
		switch (type)
		{
			// auto reply for new subscription requests
			case CMD_SUBSCRIBE :
				if (msgChn > 0) // add new subscriber to the list when the type is positive
				{
					if (totalClients >= 255)
					{
						log("Error. Too many clients added.", 14);
						break;
					}
					
					for (i = 0; i < totalClients; i++)
						if (myClients[i] == msgChn)
						{
							log("The client exists.", 1000);
							break; // break for loop
						}
					if (i < totalClients)
						break; // break switch
						
					myClients[totalClients++] = msgChn; // increase totalClients by 1
					log("Got new service subscription from " + to_string(msgChn) + ". Now has " + to_string(totalClients) + " clients.", 1000);
				}
				else
				{  // delete the corresponding subscriber from the list when the type is negative
					for ( i = 0; i < totalClients; i++)
						if (myClients[i] + msgChn == 0)
						{  // move later subscriber one step up
							for ( j = i; j < totalClients - 1; j++)
								myClients[j] = myClients[j+1];
							totalClients--;  // decrease totalClients by 1
						}
						
					log("Canceled service subscription from " + to_string(msgChn) + ". Now has " + to_string(totalClients) + " clients.", 1000);
				}
				break; // continue to read next messages

			// auto reply the query of service data query
			case CMD_QUERY :
				gettimeofday(&tv, nullptr);
				buf.rChn = msgChn;
				buf.sChn = mChn;
				buf.sec = tv.tv_sec;
				buf.usec = tv.tv_usec;
				buf.type = dataType;
				buf.len = dataLength;

				memcpy(buf.mText, mData, dataLength);
				len = msgsnd(mId, &buf,  buf.len + headerLen, IPC_NOWAIT);
				err = errno;
				break; // continue to read next message
				
			// auto process the services list reply from the server
			// [index_1][title_1][index_2][title_2] ... [index_n][title_n] ; titles are end with /0
			case CMD_LIST :
				int index;
				int offset = 0;
				
				do 
				{					
					index = *(mText + offset);
					offset += sizeof(index);
					serviceIndex[totalServices] = index;
					serviceTitles[totalServices].assign(mText + offset);
					offset += serviceTitles[totalServices++].length() + 1; // increase the totalServices, update the offset to next
				} while (offset < buf.len);
				log("Received new services list from the server. There are " + to_string(totalServices) + " services in the list.", 1000);
				break;
				
			// auto process the database init reply from the server
			// [type_1][keyword_1][type_2][keyword_2] ... [type_n][keyword_n] ; titles are end with /0
			case CMD_DATABASEINIT :
				int typ;
				string keyword;
				int offset = 0;
				int index = 0;
				
				do 
				{					
					typ = *(mText + offset);	// read the type
					offset += sizeof(type);
					keyword.assign(mText + offset); // read the keyword
					indexDB++;
					
					// place the read DB keyword at the correct place of indexDB 
					bool err = false;
					for (int i = 0; i < totalProperties; i++)
					{
						if (keyword.compare(pptr[i]->keyword) == 0)
						{
							if (i < indexDB)
							{
								log("Fatal error! The keyword " + keyword + " is already in the list at position " + to_string(i), 12);
								err = true;
								break;
							}
							
							if (typ == pptr[i]->type)
							{
								pptr[i]->type = pptr[indexDB];
								pptr[i]->keyword = pptr[indexDB]->keyword;
							}
							else
							{
								log("Fatal error! The data type of " + keyword + " from server is " + to_string(typ) \
								    ", locals is of " + tostring(pptr[i]->type) + ". They are not indentical.", 11);
								err = true;
							}
							
							break;
						}
					}
					
					if (!err)
					{
						totalProperties++;
						pptr[indexDB]->type = typ;
						pptr[indexDB]->keyword = keyword;
						log("Added " + keyword + " from server with type " + to_string(typ) + " to the local keywords list.", 1000);
					}
				} while (offset < buf.len);
				break;
				
			// auto process the database query return from the server
			// [index_1][len_1][data_1][index_2][len_2][data_2] ... [index_n][len_n][data_n] ; end with index=/0
			case CMD_DATABASEQUERY:
				do
				{
					i = static_cast<size_t>(mText[offset++]);
					if (!i)
						break;
					
					pptr[i]->len = mText[offset];
					n = static_cast<size_t>(mText[offset++]);
					if (pptr[i]->type == 0) // a string shall be assigned the value
					{
						if (!pptr[i]->ptr)
							pptr[i]->ptr = new string;
						pptr[i]->ptr->assign(mText + offset, n);
					}
					else
					{
						if (!pptr[i]->ptr)  // make sure the pointer is not NULL before assign any value to it
							pptr[i]->ptr = new char[n];
						memcpy(pptr[i]->ptr, mText + offset, n);
					}
					offset += n;
				} while (offset < 255)
				break;
			
			// auto process the notice that a service is down. 
			// [Down][Service channel]
			case CMD_DOWN:
				Subscribe(buf.mText[1]); // re-subscribe, waiting for the service provider been up again
				log("Got notice that service " + to_string(buf.mText[1]) + " is down. Re-subscribe it.", 1000);

				// stop broadcasting service data to that subscriber
				for ( i = 0; i < totalClients; i++)
					if (myClients[i] == buf.mText[1])
					{  // move later subscriber one step up
						for ( j = i; j < totalClients - 1; j++)
							myClients[j] = myClients[j+1];
						totalClients--;  // decrease totalClients by 1
						log("Stop broadcast service data to " + to_string(buf.mText[1]) + ". Total clients now is " + to_string(totalClients), 1000);
					}
				break;
				
			default:
				log("Error! Unkown command " + to_string(type) + " from " + to_string(buf.sChn), 50);
			
        }

    } while (buf.len > 0);
    return buf.len;
}

// Feed the dog at watchdog server
bool Services::FeedWatchDog()
{
	return SndMsg(nullptr, CMD_WATCHDOG, 0, ServiceTitle);
}; 

// Send a log to log server
// Sending format is [Log][logType][logContent]
bool Services::Log(string logContent, long logType)
{
    char txt[255];
    memcpy(txt, &logType, sizeof (long));
	strcpy(txt + sizeof(log), logContent.c_str());
	return SndMsg(txt, CMD_LOG, logContent.length() + sizeof(long) + 1, "SERVER");
};
	
// assign *i to store the integer queried from the database
bool Services::dbLink(string keyword, void *p, unsigned char type)
{
	int index = getIndex(keyword);
    if (index < 0)
	{
		totalProperties++;
		pptr[totalProperties]->keyword = keyword;
		pptr[totalProperties]->type = type;
		log("Added " + keyword + " to the keywords list. Total properties is " + to_string(totalProperties), 1000);
        return true;
	}
	
    if (pptr[index]->type != type)
    {
		log("Error. Data type of " + keyword + " from the server is of " + to_string(pptr[index]->type) + ". Not identical to " + to_string(type) + " . Total properties is " + to_string(totalProperties), 11);
		return false;
	}
	
	pptr[index]->ptr = p;
	pptr[index]->type = type;
	return true;
};

// assign *s to store the string queried from the database
bool Services::dbLink(string keyword, string *s)
{
	return dbLink(keyword, s, 0);
};

// Send a request to database server to query for the value of keyword. The result will be placed in the queue by database server.
// [index of the keyword]\0
bool Services::dbQuery(string keyword)
{
	int index = getIndex(keyword); 
    if (index < 0)
	{
		log("Error. Cannot find " + keyword + " in the properties.", 50);
		return false;
	}
	char txt = static_cast<char>(index);

	return SndMsg(txt, CMD_DATABASEQUERY, key.length() + 1, "SERVER");
};  

// Send a request to database to update the value of keyword with newvalue. The database server will take care of the data type casting. 
// [index of keyword][length][data]
bool Services::dbUpdate(string keyword)
{
    int index = getIndex(key); // 0 for invalid, 1 for the first
    if (index < 0)
        return false;
	
    unsigned char txt[255];
	int offset = 0;
	txt[offset++] = static_cast<unsigned char>(index); // index of the keyword
	txt[offset++] = static_cast<unsigned char>(pptr[index]->len); // length of the data
	if (pptr[index]->type)
		memcpy(txt + offset, pptr[index]->ptr, pptr[index]->len); // the data body other than a string
	else
		strcpy(txt + offset, pptr[index]->ptr->c_str()); // This is a string

    return SndMsg(txt, CMD_DATABASEUPDATE, pptr[index]->len + 3, "SERVER");
};

inline int Services::getIndex(string keyword)
{
    for (int i = 0; i < totalProperties; i++)
        if (keyword.compare(pptr[i]->keyword) == 0)
            return i;
    return -1;
}
