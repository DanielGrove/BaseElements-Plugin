/*
 BECurl.cpp
 BaseElements Plug-In
 
 Copyright 2011-2014 Goya. All rights reserved.
 For conditions of distribution and use please see the copyright notice in BEPlugin.cpp
 
 http://www.goya.com.au/baseelements/plugin
 
 */


#include "BECurl.h"
#include "BEPluginGlobalDefines.h"
#include "BEOAuth.h"
#include "BEXero.h"


#include <errno.h>


#include "boost/filesystem.hpp"
#include "boost/filesystem/fstream.hpp"
#include "boost/scoped_ptr.hpp"


#include <iostream>


using namespace std;
using namespace boost::filesystem;


#pragma mark -
#pragma mark Globals
#pragma mark -

int g_http_response_code;
string g_http_response_headers;
CustomHeaders g_http_custom_headers;
struct host_details g_http_proxy;
BECurlOptionMap g_curl_options;

extern BEOAuth * g_oauth;
//extern BEXero * g_oauth;


#pragma mark -
#pragma mark Functions
#pragma mark -


static size_t WriteMemoryCallback (void *ptr, size_t size, size_t nmemb, void *data )
{
	size_t realsize = size * nmemb;
	struct MemoryStruct *mem = (struct MemoryStruct *)data;
	
	mem->memory = (char *)realloc(mem->memory, mem->size + realsize + 1);
	if (mem->memory == NULL) {
		g_last_error = kLowMemoryError;
		exit ( EXIT_FAILURE );
	}
	
	memcpy(&(mem->memory[mem->size]), ptr, realsize);
	mem->size += realsize;
	mem->memory[mem->size] = 0;
	
	return realsize;
}


static size_t ReadMemoryCallback (void *ptr, size_t size, size_t nmemb, void *data )
{
	
	size_t curl_size = nmemb * size;
	
	struct MemoryStruct * userdata = (struct MemoryStruct *)data;
	
	size_t to_copy = ( userdata->size < curl_size ) ? userdata->size : curl_size;
	memcpy ( ptr, userdata->memory, to_copy );
	userdata->origin = userdata->memory;
	userdata->size -= to_copy;
	userdata->memory += to_copy;
	
	return to_copy;
	
}


// required when doing authenticated http put

static int SeekFunction ( void *instream, curl_off_t offset, int origin )
{
	int result = CURL_SEEKFUNC_OK;
	
	struct MemoryStruct *userdata = (struct MemoryStruct *)instream;
	
	if ( origin == SEEK_SET ) {
		userdata->size = (size_t)userdata->memory - (size_t)userdata->origin + (size_t)offset;
		userdata->memory = userdata->origin + offset;
	} else {
		// shouldn't be here
		result = CURL_SEEKFUNC_CANTSEEK;
	}
	
	return result;
}


static MemoryStruct InitalizeCallbackMemory ( void )
{
	struct MemoryStruct data;

// suppress "Memory is never released; potential leak of memory pointed to by..."
	
#ifndef __clang_analyzer__
	
	data.memory = (char *)malloc(1);  // this is grown as needed by WriteMemoryCallback
	data.memory[0] = '\0'; 
	data.size = 0;
	
	return data;
	
#endif
	
}


static vector<char> PerformAction ( BECurl * curl )
{	
	curl->set_proxy ( g_http_proxy );
	curl->set_custom_headers ( g_http_custom_headers );
	
	vector<char>data;
	
	switch ( curl->get_http_method() ) {
		case kBE_HTTP_METHOD_DELETE:
			data = curl->http_delete ( );
			break;
			
		case kBE_HTTP_METHOD_PUT:
			data = curl->http_put ( );
			break;
			
		default:
			data = curl->download ( );
			break;
	}
	
	g_last_error = curl->last_error();
	g_http_response_code = curl->response_code();
	g_http_response_headers = curl->response_headers();
	g_http_custom_headers.clear();
	
	return data;
	
} // Perform



vector<char> GetURL ( const string url, const string filename, const string username, const string password )
{	
	boost::scoped_ptr<BECurl> curl ( new BECurl ( url, kBE_HTTP_METHOD_GET, filename, username, password ) );
	return PerformAction ( curl.get() );
}


vector<char> HTTP_POST ( const string url, const string parameters, const string username, const string password )
{	
	boost::scoped_ptr<BECurl> curl ( new BECurl ( url, kBE_HTTP_METHOD_POST, "", username, password, parameters ) );
	return PerformAction ( curl.get() );
}


vector<char> HTTP_PUT ( const string url, const string filename, const string username, const string password )
{
	boost::scoped_ptr<BECurl> curl ( new BECurl ( url, kBE_HTTP_METHOD_PUT, filename, username, password ) );
	return PerformAction ( curl.get() );
}


vector<char> HTTP_PUT_DATA ( const std::string url, const char * data, const size_t size, const std::string username, const std::string password )
{
	boost::scoped_ptr<BECurl> curl ( new BECurl ( url, kBE_HTTP_METHOD_PUT, "", username, password, "", data, size ) );
	return PerformAction ( curl.get() );
}


vector<char> HTTP_DELETE ( const string url, const string username, const string password )
{	
	boost::scoped_ptr<BECurl> curl ( new BECurl ( url, kBE_HTTP_METHOD_DELETE, "", username, password ) );
	return PerformAction ( curl.get() );
}



#pragma mark -
#pragma mark Constructors
#pragma mark -


BECurl::BECurl ( const string download_this, const be_http_method method, const string to_file, const string username, const string password, const string post_parameters, const char * put_data, const size_t size )
{

	url = download_this;
	http_method = method;
	filename = to_file;
	upload_file = NULL; // must intialise, we crash otherwise
	upload_data = (char *)put_data;	// file_data is NOT copied
	upload_data_size = size;
	parameters = post_parameters;
	
	http_response_code = 0;
	
	// set up curl as much as we can
	
	error = curl_global_init ( CURL_GLOBAL_ALL );
	if ( error != kNoError ) {
		throw BECurl_Exception ( error );
	}
	
	curl = curl_easy_init ();
	
	if ( !curl ) {
		throw bad_alloc(); // curl_easy_init thinks all errors are memory errors
	}

	if ( g_oauth ) {
		
		int oauth_error = g_oauth->sign_url ( url, parameters, http_method_as_string() );
		if ( oauth_error != kNoError ) {
			throw BECurl_Exception ( CURLE_LOGIN_DENIED );
		}
	
	} else {
		set_username_and_password ( username, password );
	}

	set_parameters ( );
	
	easy_setopt ( CURLOPT_USERAGENT, USER_AGENT_STRING );
	easy_setopt ( CURLOPT_SSL_VERIFYPEER, 0L );
	easy_setopt ( CURLOPT_SSL_VERIFYHOST, 0L );
	easy_setopt ( CURLOPT_FORBID_REUSE, 1L ); // stop fms running out of file descriptors under heavy usage
	
}


BECurl::~BECurl()
{
	if ( curl ) { curl_easy_cleanup ( curl ); }
	curl_global_cleanup();
}


#pragma mark -
#pragma mark Methods
#pragma mark -


vector<char> BECurl::download ( )
{
	
	try {
		
		prepare ( );
		
		// save the data to file or memory?
		
		if ( filename.empty() ) {
			write_to_memory ( );		
		} else {
			
			upload_file = fopen ( filename.c_str(), "wb" );
			
			// curl will crash rather than fail with an error if outputFile is not open
			
			if ( upload_file ) {
				easy_setopt ( CURLOPT_WRITEDATA, upload_file );
			} else {
				throw BECurl_Exception ( CURLE_WRITE_ERROR );
			}
			
		}
		
		// download this
		easy_setopt ( CURLOPT_URL, url.c_str() );
		perform ( );
		
	} catch ( BECurl_Exception& e ) {
		error = e.code();
	}
	
	cleanup ();
	
	return result;
	
}	//	download



vector<char> BECurl::http_put ( )
{
	
	try {
		
		prepare ( );
		write_to_memory ( );		
		easy_setopt ( CURLOPT_UPLOAD, 1L );
		
		if ( filename.empty() ) {
			
			easy_setopt ( CURLOPT_READFUNCTION, ReadMemoryCallback );
			
			struct MemoryStruct userdata = InitalizeCallbackMemory ( );
			userdata.memory = upload_data;
			userdata.size = upload_data_size;
			easy_setopt ( CURLOPT_READDATA, &userdata );
			easy_setopt ( CURLOPT_INFILESIZE, userdata.size );

			easy_setopt ( CURLOPT_SEEKFUNCTION, SeekFunction );
			easy_setopt ( CURLOPT_SEEKDATA, &userdata );

		} else {
			
			path path = filename;
			
			// no directories etc.
			if ( exists ( path ) && is_regular_file ( path ) ) {
				
				upload_file = fopen ( filename.c_str(), "rb" );
				easy_setopt ( CURLOPT_READDATA, upload_file );
				easy_setopt ( CURLOPT_INFILESIZE, (curl_off_t)file_size ( path ) );
				
			} else {
				throw BECurl_Exception ( (CURLcode)kFileExistsError );
//				error = (CURLcode)kFileSystemError;
			}

		}
		
		easy_setopt ( CURLOPT_PUT, 1L );
		
		// put this
		easy_setopt ( CURLOPT_URL, url.c_str() );
		
		perform ( );
				
	} catch ( filesystem_error& e ) {
		error = (CURLcode)e.code().value();
	} catch ( BECurl_Exception& e ) {
		error = e.code();
	}
	
	cleanup ();
	
	return result;
	
}	//	delete



vector<char> BECurl::http_delete ( )
{
	try {
		
		prepare ( );
		write_to_memory ( );		
		
		easy_setopt ( CURLOPT_CUSTOMREQUEST, HTTP_METHOD_DELETE );
		
		// delete this
		easy_setopt ( CURLOPT_URL, url.c_str() );
		perform ( );
		
	} catch ( BECurl_Exception& e ) {
		error = e.code();
	}
	
	cleanup ();

	return result;
	
}	//	delete



void BECurl::set_parameters ( )
{
	
	// add the parameters to the form
	if ( !parameters.empty() ) {
		easy_setopt ( CURLOPT_POSTFIELDS, parameters.c_str() );
		easy_setopt ( CURLOPT_POSTFIELDSIZE, parameters.length() );
	}
	
}	//	set_parameters


void BECurl::set_username_and_password ( const string username, const string password )
{
	
	// set user name and password for the authentication
	if ( !username.empty() ) {
		
		easy_setopt ( CURLOPT_HTTPAUTH, (long)CURLAUTH_ANY );
		
		string username_and_password = username;
		
		if ( !password.empty() ) {
			username_and_password.append ( ":" );
			username_and_password.append ( password );
		}
		
		easy_setopt ( CURLOPT_USERPWD, username_and_password.c_str() );
		
	}
	
}	//	set_username_and_password



void BECurl::set_proxy ( struct host_details proxy_server )
{
	proxy = proxy_server.host;
	if ( !proxy_server.port.empty() ) {
		proxy += ":" + proxy_server.port;
	}
	
	proxy_login = proxy_server.username;
	if ( !proxy_server.password.empty() ) {
		proxy_login += ":" + proxy_server.password;
	}
	
}	//	set_proxy



void BECurl::set_options ( BECurlOptionMap options )
{
	
	BECurlOptionMap::iterator it = options.begin();
	while ( it != options.end() ) {
		
		boost::shared_ptr<BECurlOption>  curl_option = it->second;
		
		switch ( curl_option->type() ) {
				
			case BECurlOption::type_string:
				easy_setopt ( curl_option->option(), curl_option->as_string().c_str() );
				break;
				
			case BECurlOption::type_long:
				easy_setopt ( curl_option->option(), curl_option->as_long() );
				break;
				
			case BECurlOption::type_curl_off_t:
				easy_setopt ( curl_option->option(), curl_option->as_curl_off_t() );
				break;
				
			default:
				break;
		}
		
		++it;
	}
	
	
}	//	set_options



#pragma mark -
#pragma mark Protected Methods
#pragma mark -


void BECurl::prepare ( )
{
	
	if ( !proxy.empty() ) {
		easy_setopt ( CURLOPT_PROXY, proxy.c_str() );
		easy_setopt ( CURLOPT_PROXYUSERPWD, proxy_login.c_str() );
		easy_setopt ( CURLOPT_PROXYAUTH, CURLAUTH_ANY );
	}

	add_custom_headers ( );
	
	// send all headers & data to these functions
	
	headers = InitalizeCallbackMemory();
	easy_setopt ( CURLOPT_WRITEHEADER, (void *)&headers );
	
	data = InitalizeCallbackMemory();
	easy_setopt ( CURLOPT_HEADERFUNCTION, WriteMemoryCallback );
	
	// any custom options
	set_options ( g_curl_options );
	
}	//	prepare


void BECurl::add_custom_headers ( )
{
	
	custom_headers = NULL;
	
	// add any custom headers
	CustomHeaders::const_iterator it = http_custom_headers.begin();
	while ( it != http_custom_headers.end() ) {
		string custom_header = it->first;
		custom_header.append ( ": " );
		custom_header.append ( it->second );
		custom_headers = curl_slist_append ( custom_headers, custom_header.c_str() );
		++it;
	}
	
	if ( custom_headers ) {
		easy_setopt ( CURLOPT_HTTPHEADER, custom_headers );
	}
	
}	//	add_custom_headers


void BECurl::write_to_memory ( )
{
	// send all data to this function
	easy_setopt ( CURLOPT_WRITEFUNCTION, WriteMemoryCallback );
	easy_setopt ( CURLOPT_WRITEDATA, (void *)&data );	
}


void BECurl::perform ( )
{
	
	error = curl_easy_perform ( curl );
	
	if ( error == kNoError ) {

		error = curl_easy_getinfo ( curl, CURLINFO_RESPONSE_CODE, &http_response_code );
		
		if ( error == kNoError ) {
			// record the header information
			http_response_headers.erase();
			for ( size_t i = 0 ; i < headers.size ; i++ ) {
				http_response_headers.push_back ( headers.memory[i] );
			}
		
			// record the download response
			result.reserve ( data.size );
			for ( size_t i = 0 ; i < data.size ; i++ ) {
				result.push_back ( data.memory[i] );
			}
		} else {
			throw BECurl_Exception ( error );
		}
		
	} else {
		throw BECurl_Exception ( error );
	}
	
}	//	easy_perform


void BECurl::cleanup ( )
{
	
	if ( upload_file ) {
		fclose ( upload_file );
		upload_file = NULL;
	}
	
	be_free ( headers.memory );
	be_free ( data.memory );
	
	if ( custom_headers ) { curl_slist_free_all ( custom_headers ); }	
	
}



void BECurl::easy_setopt ( CURLoption option, ... )
{
	va_list curl_parameter;
	va_start ( curl_parameter, option );
	error = curl_easy_setopt ( curl, option, va_arg ( curl_parameter, void * ) );
	va_end ( curl_parameter );
	if ( error ) {
		throw BECurl_Exception ( error );
	}
	
}


string BECurl::http_method_as_string ( )
{
	string method = "";
	
	switch ( http_method ) {
			
		case kBE_HTTP_METHOD_DELETE:
			method = HTTP_METHOD_DELETE;
			break;
			
		case kBE_HTTP_METHOD_GET:
			method = HTTP_METHOD_GET;
			break;
			
		case kBE_HTTP_METHOD_POST:
			method = HTTP_METHOD_POST;
			break;

		case kBE_HTTP_METHOD_PUT:
			method = HTTP_METHOD_PUT;
			break;
			
		default:
			break;
	}
	
	return method;
}

