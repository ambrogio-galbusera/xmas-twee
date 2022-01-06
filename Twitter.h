#ifndef Twitter_h
#define Twitter_h

#define TWITTER_REQUEST_TIMEOUT     5000
#define TWITTER_REQUEST_IDLETIME    100

#include <WiFiClientSecure.h>
#include <ArduinoJson.h>

typedef struct 
{
  String id;
  String text;
  String author;
} Tweet;

class Twitter
{
public:
    Twitter(WiFiSecureClient* client, String bearerKey);
    
    void getTweetsByHashTag(String hashTag, int maxTweets);
    
    bool process();
    String lastError();
    
    int numTweets();
    Tweet tweetAt(int idx);
    
private:
    typedef enum {
        Status_Idle,
        Status_LoadingTweetsByHashTag,
        Status_LoadingTweetsByAuthor,
        Status_LoadingAuthor
    } StatusEnum;
    
    String getAuthor(String authorId);
    String getUrl(String url);
    bool parseTweetStep1();
    bool parseTweetStep2();
    bool processResponse();
    
    WiFiSecureClient* _client;
    String _bearerKey;
    Tweet* _tweets;
    int _numTweets;
    bool _startFound;
    String _line;
    unsigned long _requestMillis;
    unsigned long _processMillis;
    DynamicJsonDocument _doc(16384);
    String _lastError;
    StatusEnum _status;
};

#endif
