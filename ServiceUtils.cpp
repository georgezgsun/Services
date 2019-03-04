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

ServiceUtils::ServiceUtils(key_t key, string myServiceTitle, long myServiceChannel)
{
    mChn = myServiceChannel;
    mServiceTitle = myServiceTitle;
    mId = -1;
	err = 0;
    headerLength = sizeof(buf.sChn) + sizeof (buf.sec) + sizeof(buf.usec) + sizeof (buf.type) + sizeof (buf.len);

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

bool ServiceUtils::StartService()
{
    // initialize the internal buffers
    memset(&buf, 0, sizeof(buf));
    memset(&myClients, 0, sizeof(myClients));
    memset(&mySubscriptions, 0, sizeof(mySubscriptions));
    memset(&serviceTitles, 0, sizeof(serviceTitles));
    memset(&serviceChannels, 0, sizeof(serviceChannels));

    totalSubscriptions = 0;
    totalClients = 0;
    totalMessageSent = 0;
    totalMessageReceived =0;
    totalServices = 0;
    totalProperties = 0;
    indexDB = 0;

    err = -1; // 0 indicates no error;
    if (mChn <= 0)
        return false;

    err = -2; 
    if (mKey < 0)
        return false;

    err = -3; //perror("msgget");
    mId = msgget(mKey, PERMS | IPC_CREAT);
    printf("MsgQue ID : %d\n", mId);
    if (mId == -1)
        return false;

	err = 0;
    if (mChn == 1)
    {// Read off all previous messages
        do
        {
        } while (msgrcv(mId, &buf, sizeof(buf), 0, IPC_NOWAIT) > 0);
        return true;
    }
    else
    {// Report onboard to the server
		char txt[255];
		pid_t pid = getpid();
		pid_t ppid = getppid();
        size_t len = sizeof(pid);
		memcpy(txt, &pid, len);
		memcpy(txt + len, &ppid, len);
		len += len;
        strcpy(txt + len, mServiceTitle.c_str());
        len += mServiceTitle.length() + 1;
		return SndMsg(&txt, CMD_ONBOARD, len, "SERVER");
    }
}

ServiceUtils::~ServiceUtils()
{
    //for (int i = 0; i < totalProperties; i++)
    //	delete pptr[i]->ptr;

    // if the server closed, delete the message queue
    if(1 == mChn)
        msgctl(mId, IPC_RMID, nullptr);
};

bool ServiceUtils::SndMsg(string msg, string ServiceTitle)
{
    err = -1;  // Message queue has not been opened
    if (mId == -1)
        return false;

    buf.rChn = GetServiceChannel(ServiceTitle);
    err = -2; // Cannot find the ServiceTitle in the list
    if (buf.rChn <= 0)
        return false;

    struct timeval tv;
    gettimeofday(&tv, nullptr);
    buf.sChn = mChn;
    buf.sec = tv.tv_sec;
    buf.usec = tv.tv_usec;
    buf.len = msg.length()+1;
    buf.type = 0;

    memcpy(buf.mText, msg.c_str(), buf.len);
    if ( msgsnd(mId, &buf, buf.len + headerLength, IPC_NOWAIT) )
    {
        err =0;
        totalMessageSent++;
        return true;
    }
    err = errno;
    return false;

//	return SndMsg(static_cast< void*>(msg.c_str()), 0, msg.length() + 1, ServiceChannel);
};

// Send a packet with given length to specified service provider
bool ServiceUtils::SndMsg(void *p, size_t type, size_t len, string ServiceTitle)
{
    err = -1;  // Message queue has not been opened
    if (mId == -1)
        return false;

    err = -2;
    buf.rChn = -1;
    for (size_t i = 0; i < totalServices; i++)
        if (ServiceTitle.compare(serviceTitles[i]) == 0)
        {
            buf.rChn = static_cast<long>(i);
            break;
        }
    if (buf.rChn <= 0)
        return false; // Cannot find the ServiceTitle in the list

    err = -3;
    if (len > 255)
        return false;

    err = -4;
    if (type > 255)
        return false;

    struct timeval tv;
    gettimeofday(&tv, nullptr);
    buf.sChn = mChn;
    buf.sec = tv.tv_sec;
    buf.usec = tv.tv_usec;
    buf.len = len;
    buf.type = type;

    memcpy(buf.mText, p, len);
    if ( msgsnd(mId, &buf, len + headerLength, IPC_NOWAIT))
    {
        err = 0;
        totalMessageSent ++;
        return true;
    }
    err = errno;
    return false;
};

bool ServiceUtils::SubscribeService(string ServiceTitle)
{
	err = -1;
    long ServiceChannel = GetServiceChannel(ServiceTitle);
	if (ServiceChannel < 1)
		return false;

	err = -2;
	if (!SndMsg(nullptr, CMD_SUBSCRIBE, 0, ServiceTitle))
		return false;

	err = 0;
	// check if the service has been subscribed before
    for (size_t i = 0; i < totalClients; i++)
        if (myClients[i] == ServiceChannel)
			return true;

	// add the new subscription to my list
	mySubscriptions[totalSubscriptions++] = ServiceChannel;
	return true;
};

bool ServiceUtils::BroadcastUpdate(void *p, size_t type, size_t dataLength)
{
	err = -1;  // Message queue has not been opened
	if (mId == -1)
		return false;

	err = -3;
    if (dataLength > 255)
		return false;

	err = -4;
    if (type > 255)
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

    for (size_t i = 0; i < totalClients; i++)
    {
        buf.rChn = myClients[i];
        if ( ! msgsnd(mId, &buf, dataLength + headerLength, IPC_NOWAIT) )
        {
			err = errno;
			return false;
		}
    }

    err = 0;
    return true;
}; // Multicast the data stored at *p with dataLength and send it to every subscriber

// receive a message from sspecified ervice provider, like GPS, RADAR, TRIGGER. No AutoReply
string ServiceUtils::RcvMsg()
{
	char b[255];
	string msg = "";
    size_t len;
    size_t type;
	if (RcvMsg(&b, &type, &len) && (type == 0) && (len > 0))
		msg.assign(buf.mText, len);

	return msg;
}

// receive a packet from specified service provider. Autoreply all requests when enabled.
bool ServiceUtils::RcvMsg(void *p, size_t *type, size_t *len) // receive a packet from specified service provider. Autoreply all requests
{
    size_t i;
    size_t j;
    size_t n;
    size_t offset = 0;
    long index;
    struct timeval tv;

	do
	{
		//buf.rChn = mChn;
		memset(buf.mText, 0, sizeof(buf.mText));  // fill 0 before reading, make sure no garbage left over
        long l = msgrcv(mId, &buf, sizeof(buf), mChn, IPC_NOWAIT);
        l -= headerLength;
        err = static_cast<int>(l);
		if (l <= 0)
			return false;
		totalMessageReceived++;

		err = 0;
        *type = buf.type;
        *len = buf.len;
		if (p != nullptr)
            memcpy(p, buf.mText, static_cast<size_t>(l));

		msgChn = buf.sChn; // the service type of last receiving message
		msgTS_sec = buf.sec; // the time stamp in seconds of latest receiving message
		msgTS_usec = buf.usec;
        size_t typ;
        string keyword;

		// no autoreply for server
		if (mChn == 1)
			return true;

		// no auto reply for those normal receiving. type < 31 is normal return
        if (*type < 32)
			return true;

        switch (*type)
		{
			// auto reply for new subscription request
			case CMD_SUBSCRIBE :
				if (msgChn > 0) // add new subscriber to the list when the type is positive
				{
					if (totalClients >= 255) // make sure no over flow of subscription
					{
						err = -14;
                        Log("Error. Too many clients added.", -err);
						return true;
					}

					for (i = 0; i < totalClients; i++)
						if (myClients[i] == msgChn)
						{
                            Log("The client exists. No more adding.", 1000);
							break; // break for loop
						}
					if (i < totalClients)
						break; // break switch

					myClients[totalClients++] = msgChn; // increase totalClients by 1
                    Log("Got new service subscription from " + to_string(msgChn) + ". Now has " + to_string(totalClients) + " clients.", 1000);
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

                    Log("Canceled service subscription from " + to_string(msgChn) + ". Now has " + to_string(totalClients) + " clients.", 1000);
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
                msgsnd(mId, &buf,  dataLength + headerLength, IPC_NOWAIT);
				err = errno;
				break; // continue to read next message

			// auto process the services list reply from the server
			// [index_1][title_1][index_2][title_2] ... [index_n][title_n] ; titles are end with /0
			case CMD_LIST :
                offset = 0;

				do
				{
                    memcpy(&index, buf.mText + offset, sizeof (index));
					offset += sizeof(index);
                    serviceChannels[totalServices] = index;
                    serviceTitles[totalServices].assign(buf.mText + offset);
					offset += serviceTitles[totalServices++].length() + 1; // increase the totalServices, update the offset to next
				} while (offset < buf.len);
                Log("Received new services list from the server. There are " + to_string(totalServices) + " services in the list.", 1000);
				break;

			// auto process the database init reply from the server
            // [type_1][keyword_1][type_2][keyword_2] ... [type_n][keyword_n] ; keywords are end with /0
			case CMD_DATABASEINIT :
                offset = 0;
                index = 0;

				do
				{
                    memcpy(&typ, buf.mText + offset, sizeof (typ));	// read the type
					offset += sizeof(typ);
                    keyword.assign(buf.mText + offset); // read the keyword

					// place the read DB keyword at the correct place of indexDB
					err = 0;
                    for (i = 0; i < totalProperties; i++)
					{
						if (keyword.compare(pptr[i]->keyword) == 0)
						{
							if (i < indexDB)
							{
								err = -12;
                                Log("Fatal error! The keyword " + keyword + \
									" is already in the list at position " + to_string(i), \
									-err);
								return true; 
							}

							if (typ == pptr[i]->type)
							{
                                pptr[i]->type = pptr[indexDB]->type;
								pptr[i]->keyword = pptr[indexDB]->keyword;
							}
							else
							{
								err = -11;
                                Log("Fatal error! The data type of " + keyword + " from server is " \
									+ to_string(typ) + ", locals is of " \
                                    + to_string(pptr[i]->type) + ". They are not indentical.", \
								    -err);
							    return true;
							}

							break;  // beak for loop
						}
					}

					totalProperties++;
					pptr[indexDB]->type = typ;
					pptr[indexDB]->keyword = keyword;
					indexDB++;
                    Log("Added " + keyword + " from server with type " + to_string(typ) + " to the local keywords list.", 1000);

				} while (offset < buf.len);
				break;

			// auto process the database feedback from the server
			// [index_1][len_1][data_1][index_2][len_2][data_2] ... [index_n][len_n][data_n] ; end with index=/0
			case CMD_DATABASEQUERY:
				do
				{
                    memcpy(&i, buf.mText + offset, sizeof (i)); // read element index
					offset += sizeof(i);
                    memcpy(&n, buf.mText + offset, sizeof(n)); // read data length
					offset += sizeof(n);
					if ((offset > 255) || (n < 1))	// indicates no more data
						break;  // break do while loop

					pptr[i]->len = n;
					if (pptr[i]->type == 0) // a string shall be assigned the value
					{
						if (!pptr[i]->ptr)
							pptr[i]->ptr = new string;
                        static_cast<string *>(pptr[i]->ptr)->assign(buf.mText + offset, n);
					}
					else
					{
						if (!pptr[i]->ptr)  // make sure the pointer is not NULL before assign any value to it
							pptr[i]->ptr = new char[n];
                        memcpy(pptr[i]->ptr, buf.mText + offset, n);
					}
					offset += n;
                } while (offset < 255);
				break;

			// auto process the notice that a service is down.
			// [Service channel]
			case CMD_DOWN:
                memcpy(&l, buf.mText, sizeof (l)); // l stores the service channel which was down
                Log("Got a notice that service number " + to_string(l) + " is down.", 1000);
                keyword = GetServiceTitle(l);
                if (keyword.length() == 0)
				{
					err = -100;
                    Log("Error. No such a service number " + to_string(l) \
						+ " exists in the list. ", -err);
					return true;
				}

				// stop broadcasting service data to that subscriber
				for ( i = 0; i < totalClients; i++)
                    if (myClients[i] == l)
					{
						// re-subscribe the service, waiting for the service provider up again
                        SubscribeService(keyword);
                        Log("Re-subscribe " + keyword, 1000);

						// move later subscriber one step up
						for ( j = i; j < totalClients - 1; j++)
							myClients[j] = myClients[j+1];
						totalClients--;  // decrease totalClients by 1
                        Log("Stop broadcast service data to " + keyword \
							+ ". Total clients now is " + to_string(totalClients), 1000);
					}
				break;

			default:
				err = -50;
                Log("Error! Unkown command " + to_string(*type) + " from " \
					+ to_string(buf.sChn), -err);
				return true;

        }

    } while (err == 0);
    return true;
}

// Feed the dog at watchdog server
bool ServiceUtils::FeedWatchDog()
{
    err = -1;  // Message queue has not been opened
    if (mId == -1)
        return false;

    struct timeval tv;
    gettimeofday(&tv, nullptr);
    buf.rChn = 1; // always sends to server
    buf.sChn = mChn;
    buf.sec = tv.tv_sec;
    buf.usec = tv.tv_usec;
    buf.len = 0;
    buf.type = CMD_WATCHDOG;

    if ( msgsnd(mId, &buf, headerLength, IPC_NOWAIT) )
    {
        err =0;
        totalMessageSent++;
        return true;
    }
    err = errno;
    return false;
}; 

// Send a Log to server
// Sending format is [LogType][LogContent]
bool ServiceUtils::Log(string LogContent, long LogType)
{
    err = -1;  // Message queue has not been opened
    if (mId == -1)
        return false;

    struct timeval tv;
    gettimeofday(&tv, nullptr);
    buf.rChn = 1; // always sends to server
    buf.sChn = mChn;
    buf.sec = tv.tv_sec;
    buf.usec = tv.tv_usec;
    buf.len = LogContent.length() + sizeof (LogType) + 1;
    buf.type = CMD_LOG;

    memcpy(buf.mText, &LogType, sizeof(LogType));
    memcpy(buf.mText + sizeof(LogType), LogContent.c_str(), LogContent.length() + 1);
    if ( msgsnd(mId, &buf, buf.len + headerLength, IPC_NOWAIT) )
    {
        err =0;
        totalMessageSent++;
        return true;
    }
    err = errno;
    return false;
};
	
// assign *i to store the integer queried from the database
bool ServiceUtils::dbLink(string keyword, void *p, size_t type)
{
    err = 0;
    size_t index = getIndex(keyword);
    if (index < 255)
	{
		totalProperties++;
		pptr[totalProperties]->keyword = keyword;
		pptr[totalProperties]->type = type;
        Log("Added " + keyword + " to the keywords list. Total properties is " + to_string(totalProperties), 1000);
        return true;
	}
	
    if (pptr[index]->type != type)
    {
        err = -11;
        Log("Error. Data type of " + keyword + " from the server is of " + to_string(pptr[index]->type) \
            + ". Not identical to " + to_string(type) + " . Total properties is " + to_string(totalProperties), -err);
		return false;
	}
	
	pptr[index]->ptr = p;
	pptr[index]->type = type;
	return true;
};

// assign *s to store the string queried from the database
bool ServiceUtils::dbLink(string keyword, string *s)
{
	return dbLink(keyword, s, 0);
};

// Send a request to database server to query for the value of keyword. The result will be placed in the queue by database server.
// [index of the keyword]\0
bool ServiceUtils::dbQuery()
{
    err = -1;  // Message queue has not been opened
    if (mId == -1)
        return false;

    struct timeval tv;
    gettimeofday(&tv, nullptr);
    buf.rChn = 1; // always sends to server
    buf.sChn = mChn;
    buf.sec = tv.tv_sec;
    buf.usec = tv.tv_usec;
    buf.len = 0;
    buf.type = CMD_DATABASEQUERY;

    if ( msgsnd(mId, &buf, headerLength, IPC_NOWAIT) )
    {
        err =0;
        totalMessageSent++;
        return true;
    }
    err = errno;
    return false;
};  

// Send a request to database to update the value of keyword with newvalue. The database server will take care of the data type casting. 
// [index of keyword][length][data]
bool ServiceUtils::dbUpdate()
{
    err = -1;  // Message queue has not been opened
    if (mId == -1)
        return false;

    struct timeval tv;
    gettimeofday(&tv, nullptr);
    buf.rChn = 1; // always sends to server
    buf.sChn = mChn;
    buf.sec = tv.tv_sec;
    buf.usec = tv.tv_usec;
    buf.len = 0;
    buf.type = CMD_DATABASEUPDATE;

    if ( msgsnd(mId, &buf, headerLength, IPC_NOWAIT) )
    {
        err =0;
        totalMessageSent++;
        return true;
    }
    err = errno;
    return false;
};

long ServiceUtils::GetServiceChannel(string serviceTitle)
{
    for (size_t i = 0; i < totalServices; i++)
        if (serviceTitle.compare(serviceTitles[i]) == 0)
            return static_cast<long>(i);
    return 0;
};

string ServiceUtils::GetServiceTitle(long serviceChannel)
{
    for (size_t i = 0; i < totalServices; i++)
        if (serviceChannel == serviceChannels[i])
            return serviceTitles[i];
    return "";
};

inline size_t ServiceUtils::getIndex(string keyword)
{
    for (size_t i = 0; i < totalProperties; i++)
        if (keyword.compare(pptr[i]->keyword) == 0)
            return i;
    return 255;
}
