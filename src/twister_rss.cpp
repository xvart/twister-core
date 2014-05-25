#include "twister_rss.h"
#include "init.h"
#include "bitcoinrpc.h"
#include "json/json_spirit.h"

#include <sstream>
#include <algorithm>
#include <vector>
#include <ctime>
#include <boost/lexical_cast.hpp>

using namespace std;
using namespace json_spirit;

int generateRSS(string uri, string *output)
{
    string account = "";
    int max = 20;

    // get URI parameters if available
    size_t startPosition, endPosition;
    if(uri.find("max=")!=string::npos)
    {
        startPosition = uri.find("max=")+4;
        if(uri.find("&",startPosition)!=string::npos)
            endPosition=uri.find("&",startPosition);
        else
            endPosition=uri.length();
        try
        {
            max = boost::lexical_cast<int>(uri.substr(startPosition,endPosition-startPosition));
        }
        catch(boost::bad_lexical_cast e)
        {
            return RSS_ERROR_NOT_A_NUMBER;
        }
    }
    if(uri.find("account=")!=string::npos)
    {
        startPosition = uri.find("account=")+8;
        if(uri.find("&",startPosition)!=string::npos)
            endPosition=uri.find("&",startPosition);
        else
            endPosition=uri.length();

        account = uri.substr(startPosition,endPosition-startPosition);
    }

    const Array emptyArray;
    Array accountsArray = listwalletusers(emptyArray,false).get_array();
    
    // if no account was specified, choose the first one
    if(account=="")
    {
        if(accountsArray.size()>0)
        {
            account = accountsArray[0].get_str();
        }
        else return RSS_ERROR_NO_ACCOUNT;
    }
    
    // if an account name was specified, check that it exists
    else
    {
        bool accountExists = false;
        for(int i=0;i<accountsArray.size();i++)
        {
            if(accountsArray[i]==account)
                accountExists=true;
        }
        if(!accountExists) return RSS_ERROR_BAD_ACCOUNT;	
    }

    // get an array of followed usernames and transform it to required format
    Array params1;
    params1.push_back(account);
    Array followingArray = getfollowing(params1,false).get_array();
    Array postSources;
    for(int i=0;i<followingArray.size();i++)
    {
        Object item;
        item.push_back(Pair("username",followingArray[i]));
        postSources.push_back(item);
    }

    Array params2;
    params2.push_back(max);
    params2.push_back(postSources);
    Array posts = getposts(params2,false).get_array();
    Array params3;
    params3.push_back(account);
    params3.push_back(max);
    params3.push_back(postSources);
    Object messages = getdirectmsgs(params3,false).get_obj();
    vector<Object> outputVector;   
          
    for(int j=0;j<messages.size();j++)
    {
        Array userArray = messages[j].value_.get_array();
        for(int i=0;i<userArray.size();i++)
        {
            try
            {
                if(find_value(userArray[i].get_obj(),"fromMe").get_bool())      //only report received messages
                  continue;
                
                string postTitle, postAuthor, postMsg;
                postAuthor=messages[j].name_;
                postTitle="Direct Message from "+postAuthor;
                postMsg=find_value(userArray[i].get_obj(),"text").get_str();            
                time_t postTime(find_value(userArray[i].get_obj(),"time").get_int64());

                Object item;
                item.push_back(Pair("time",postTime));
                item.push_back(Pair("title",postTitle));
                item.push_back(Pair("author",postAuthor));
                item.push_back(Pair("msg",postMsg));
                outputVector.push_back(item);
            }
            catch(exception ex)
            {
                fprintf(stderr, "Warning: RSS couldn't parse a direct message, skipping.\n");
                continue;
            }
        }
    }
        
    for(int i=0;i<posts.size();i++)
    {
        try
        {
            Object userpost = find_value(posts[i].get_obj(),"userpost").get_obj();
            string postTitle, postAuthor, postMsg;
            Value rt = find_value(userpost,"rt");

            if(rt.is_null())    // it's a normal post
            {
                postAuthor = find_value(userpost,"n").get_str();
                Value reply = find_value(userpost,"reply");
                if(!reply.is_null()&&find_value(reply.get_obj(),"n").get_str()==account)
                {
                    postTitle = "Reply from "+postAuthor;
                }
                else postTitle = postAuthor;
                postMsg = find_value(userpost,"msg").get_str();
            }
            else   // it's a retwist
            {               
                postAuthor = find_value(rt.get_obj(),"n").get_str();
                postTitle = postAuthor + " - via " + find_value(userpost,"n").get_str();
                postMsg = find_value(rt.get_obj(),"msg").get_str();
            }
            
            time_t postTime(find_value(userpost,"time").get_int64());
            
            Object item;
            item.push_back(Pair("time",postTime));
            item.push_back(Pair("title",postTitle));
            item.push_back(Pair("author",postAuthor));
            item.push_back(Pair("msg",postMsg));
            outputVector.push_back(item);
        }
        catch(exception ex)
        {
            fprintf(stderr, "Warning: RSS couldn't parse a public post, skipping.\n");
            continue;
        }
    }
    
    sort(outputVector.begin(),outputVector.end(),sortByTime);
    
    ostringstream ret;
    
    ret << "<?xml version=\"1.0\" encoding=\"UTF-8\" ?>\n"
        << "<rss version=\"2.0\">\n"
        << "<channel>\n"
        << "  <title>Twister Postboard - " << account << "</title>\n"
        << "  <description>New posts from Twister</description>\n";
    
    int outputSize = (outputVector.size()>max)?max:outputVector.size();
    
    for(int i=0;i<outputSize;i++)
    {
        Object item = outputVector[i];
        time_t postTime(find_value(item,"time").get_int64());
        char timeString[100];
        strftime(timeString, sizeof(timeString), "%a, %d %b %Y %H:%M:%S %z", gmtime(&postTime));

        ret << "  <item>\n"
            << "    <title>" << find_value(item,"title").get_str() << "</title>\n"
            << "    <author>" << find_value(item,"author").get_str() << "</author>\n"
            << "    <description>" << find_value(item,"msg").get_str() << "</description>\n"
            << "    <pubDate>" << timeString << "</pubDate>\n"
            << "  </item>\n";
    }

    ret << "</channel>\n"
        << "</rss>\n";

    *output = ret.str();
    return RSS_OK;
}

bool sortByTime (Object i,Object j)
{ 
    return (find_value(i,"time").get_int64()>find_value(j,"time").get_int64());
}
