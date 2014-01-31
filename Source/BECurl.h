/*
 BECurl.h
 BaseElements Plug-In
 
 Copyright 2011-2014 Goya. All rights reserved.
 For conditions of distribution and use please see the copyright notice in BEPlugin.cpp
 
 http://www.goya.com.au/baseelements/plugin
 
 */


#if !defined(BECURL_H)
#define BECURL_H


#include "BEPluginUtilities.h"
#include "BECurlOption.h"

#include "boost/shared_ptr.hpp"

#include <vector>
#include <map>


#include "curl/curl.h"


typedef std::map<std::string, boost::shared_ptr<BECurlOption> > BECurlOptionMap;


typedef enum be_http_method {
	kBE_HTTP_METHOD_DELETE,
	kBE_HTTP_METHOD_GET,
	kBE_HTTP_METHOD_POST,
	kBE_HTTP_METHOD_PUT
} be_http_method;


#define HTTP_METHOD_DELETE "DELETE"
#define HTTP_METHOD_GET "GET"
#define HTTP_METHOD_POST "POST"
#define HTTP_METHOD_PUT "PUT"



class BECurl_Exception : public std::runtime_error {
	
public:
	BECurl_Exception ( const CURLcode _code ) : runtime_error ( "BECurl_Exception" ) { error_code = _code; }
	
	virtual const CURLcode code() const throw()
	{
		return error_code;
	}
	
	private:
	CURLcode error_code;
	
};


typedef std::map<std::string, std::string> CustomHeaders;


struct MemoryStruct {
	char * memory;
	size_t size;
	char * origin;
};


struct host_details {
	std::string host;
	std::string port;
	std::string username;
	std::string password;
};


std::vector<char> GetURL ( const std::string url, const std::string filename = "", const std::string username = "", const std::string password = "" );
std::vector<char> HTTP_POST ( const std::string url, const std::string post_parameters, const std::string username = "", const std::string password = "" );
std::vector<char> HTTP_PUT ( const std::string url, const std::string post_parameters, const std::string username = "", const std::string password = "" );
std::vector<char> HTTP_PUT_DATA ( const std::string url, const char * data, const size_t size, const std::string username = "", const std::string password = "" );
std::vector<char> HTTP_DELETE ( const std::string url, const std::string username = "", const std::string password = "" );


class BECurl {
	
public:
	
	BECurl ( const std::string download_this, const be_http_method method = kBE_HTTP_METHOD_GET, const std::string to_file = "", const std::string username = "", const std::string password = "", const std::string post_parameters = "", const char * put_data = NULL, const size_t size = 0  );

	~BECurl();
	
    std::vector<char> download ( );
    std::vector<char> http_put ( );
    std::vector<char> http_delete ( );
	
	be_http_method get_http_method ( ) { return http_method; };
	void set_http_method ( be_http_method method ) { http_method = method; };
	
	void set_parameters ( );
	void set_username_and_password ( const std::string username, const std::string password );
	
	int response_code ( ) { return http_response_code; }
	std::string response_headers ( ) { return http_response_headers; }
	void set_custom_headers ( CustomHeaders _headers ) { http_custom_headers = _headers; }
	void set_proxy ( struct host_details proxy_server );
	void set_options ( BECurlOptionMap options );
	CURLcode last_error ( ) { return error; }
	
protected:
	
	std::string url;
	
	be_http_method http_method;

	std::string filename;
	FILE * upload_file;
	
	char * upload_data;
	size_t upload_data_size;

	std::string parameters;
	
	CURL *curl;
	struct curl_slist *custom_headers;	
	struct MemoryStruct headers;
	struct MemoryStruct data;
	std::vector<char> result;
	
	int http_response_code;
	CustomHeaders http_custom_headers;
	std::string http_response_headers;
	CURLcode error;
	std::string proxy;
	std::string proxy_login;

    void prepare ( );
	void add_custom_headers ( );
	void write_to_memory ( );
	void perform ( );
	void cleanup ( );
	void easy_setopt ( CURLoption option, ... );
	
	std::string http_method_as_string ( );

};


#endif // BEXBECURL_HSLT_H

