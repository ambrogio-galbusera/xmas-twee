

Twitter::Twitter(WiFiSecureClient* client, String bearerKey)
{
    _client = client;
    _bearerKey = bearerKey;
    _status = Status_Idle;
    _tweets = NULL;
}

void Twitter::getTweets(String hashtag, int maxTweets)
{
  String url = "/2/tweets/search/recent?query=%23" + hashtag + "&max_results=" + String(maxTweets) + "&expansions=author_id";

#ifdef TWITTER_PRINT_REQUEST
  Serial.print("Requesting URL: ");
  Serial.println(url);
#endif
    
    getUrl(url);

#ifdef TWITTER_PRINT_REPLY
    Serial.println("--------------------------");
    Serial.println(line);
    Serial.println("--------------------------");
#endif

    _status = Status_LoadingTweetsByHashTag;
}

void Twitter::getUrl(String url)
{
    _client->println("GET "+ url + " HTTP/1.1");
    _client->println("Host: api.twitter.com");
    _client->println("User-Agent: arduino/1.0.0");
    _client->println("Authorization: Bearer " + BEARER_TOKEN);
    _client->println("");
    
    _startFound = false;
    _line = "";
    _requestMillis = millis();  
}

bool Twitter::process()
{
    // This will send the request to the server
    if (_client->available() == 0) 
    {
        if (_line.length() > 0)
        {
            if (millis() - _processMillis > TWITTER_REQUEST_IDLETIME) {
                // process response
                return processResponse();
            }
        }
        else
        {
            if (millis() - _requestMillis > TWITTER_REQUEST_TIMEOUT) {
                _lastError = "Client timeout";
                return true;
            }
        }
    }

    // Read all the lines of the reply from server and print them to Serial
    while(_client->available()) {
        String tmp = client.readStringUntil('\r');
        //Serial.println(tmp);

        if (!_startFound)
        {
            int idx = 0;
            while ((idx <= 4) && (tmp[idx] != '{'))
                idx ++;
          
            if (tmp[idx] == '{')
                _startFound = true;
        }

        if (_startFound) _line += tmp;
     }

    _processMillis = millis();
    return false;
}


String Twitter::getAuthor(String authorId)
{
  Serial.println("Lookp user: " + authorId);

  String url = "/2/users/"+authorId;
#ifdef TWITTER_PRINT_REQUEST
  Serial.print("Requesting URL: ");
  Serial.println(url);
#endif
 
  String line = getUrl(url);

#ifdef TWITTER_PRINT_REPLY
  Serial.println("--------------------------");
  Serial.println(line);
  Serial.println("--------------------------");
#endif
  
  return line;
}

bool Twitter::parse()
{
    DeserializationError error = deserializeJson(_doc, _line);
    if (error != DeserializationError::Ok)
    {
        _lastError = "Deserialization error: " + error.f_str();
        return false;
    }
    
    if (_tweets != NULL)
    {
        delete [] _tweets;
        _tweets = NULL;
    }
    
    _tweets = new Tweet[_numTweets];
    _currTweet = 0;
}

bool Twitter::parseTweetStep1()
{
    JsonArray arr = _doc["data"];
    if (_currTweet >= arr.size())
        return true;
    
    
    JsonObject obj = arr[_currTweet];
    String authorId = String((const char*)obj["author_id"]);
  
    getAuthor(authorId);
    return false;
}

bool Twitter::parseTweetStep2()
{
    String authorId;
    String author;
    String text;
    
    JsonArray arr = _doc["data"];
    if (_currTweet >= arr.size())
        return true;
    
    JsonObject obj = arr[_currTweet];
    String id = String((const char*)obj["id"]);
  
    DeserializationError error = DynamicJsonDocument docAuth(16384);
    if (error != DeserializationError::Ok)
    {
        _lastError = "Deserialization error: " + error.f_str();
        return true;
    }

    JsonObject user = docAuth["data"];
    String author = String((const char*)user["name"]);
  
    text = String((const char*)obj["text"]);
  
    _tweets[_currTweet].id = id;
    _tweets[_currTweet].text = text;
    _tweets[_currTweet].author = author;
    _currTweet ++;
}

bool Twitter::processResponse()
{
    if (_status == Status_LoadingTweetsByHashTag)
    {
        if (!parse())
        {
            _status = Status_Idle;
            return false;
        }
        
        if (!parseTweetStep1())
        {
            _status = Status_Idle;
            return false;
        }

        _status = Status_LoadingAuthor;
    }
    else if (_status == Status_LoadingTweetsByAuthor)
    {
        if (!parse())
        {
            _status = Status_Idle;
            return false;
        }

        if (!parseTweetStep1())
        {
            _status = Status_Idle;
            return false;
        }
        
        _status = Status_LoadingAuthor;
    }
    else if (_status == Status_LoadingAuthor)
    {
        if (!parseTweetStep2())
        {
            _status = Status_Idle;
            return false;
        }
        
        if (!parseTweetStep1())
        {
            _status = Status_Idle;
            return false;
        }

        _status = Status_LoadingAuthor;
    }
    
    return true;
}
