/*
 BEPluginUtilities.cpp
 BaseElements Plug-In
 
 Copyright 2010-2014 Goya. All rights reserved.
 For conditions of distribution and use please see the copyright notice in BEPlugin.cpp
 
 http://www.goya.com.au/baseelements/plugin
 
 */


#include "BEPluginGlobalDefines.h"


#if defined(FMX_MAC_TARGET)

	#include <CoreServices/CoreServices.h>
	#include <Carbon/Carbon.h>

#endif 


#include "BEPluginUtilities.h"


#include "boost/format.hpp"
#include "boost/filesystem.hpp"
#include "boost/filesystem/fstream.hpp"

#include <sstream>
#include <iostream>
#include <iconv.h>


using namespace std;
using namespace fmx;
using namespace boost::filesystem;


extern string g_text_encoding;
extern errcode g_last_ddl_error;


#pragma mark -
#pragma mark Plug-In Utilities
#pragma mark -

// convenience functions that handle most of the work needed to return text from a function

errcode TextConstantFunction ( wstring text, Data& results )
{
	
	WStringAutoPtr text_constant ( new wstring ( text ) );
	
	return TextConstantFunction ( text_constant, results );	
		
} // TextConstantFunction


errcode TextConstantFunction ( WStringAutoPtr text, Data& results )
{
	
	errcode error_result = kNoError;
	
	try {
		
		TextAutoPtr result_text;
		result_text->AssignWide ( text->c_str() );
		
		LocaleAutoPtr default_locale;
		results.SetAsText ( *result_text, *default_locale );
		
	} catch ( bad_alloc& /* e */ ) {
		error_result = kLowMemoryError;
	} catch ( exception& /* e */ ) {
		error_result = kErrorUnknown;
	}
	
	return error_result;
	
} // TextConstantFunction


// get parameters and set function results


#pragma mark -
#pragma mark SetResult
#pragma mark -

void SetResult ( const intmax_t number, Data& results )
{
	FixPtAutoPtr numeric_result;
	numeric_result->AssignDouble ( number );
	results.SetAsNumber ( *numeric_result );
}


// Cannot use SetResult as the compiler is not disignusing const double and const intmax_t

void SetResultAsDouble ( const double number, Data& results )
{
//	FixPtAutoPtr numeric_result;
//	numeric_result->AssignDouble ( number );
//	results.SetAsNumber ( *numeric_result );

//	const std::string as_string = boost::lexical_cast<std::string>( number );
	
	// do this the c++ vay due to avoid rounding errors
	ostringstream stream;
	stream << number;
	StringAutoPtr result ( new string );
	*result = stream.str();
	SetResult ( result, results );
}


void SetResult ( const Text& text, Data& results )
{
	LocaleAutoPtr default_locale;
	results.SetAsText ( text, *default_locale );
}


void SetResult ( const string text, Data& results )
{
	TextAutoPtr result_text;
	result_text->Assign ( text.c_str(), Text::kEncoding_UTF8 );
	SetResult ( *result_text, results );
}


void SetResult ( const StringAutoPtr text, Data& results )
{
	TextAutoPtr result_text;
	result_text->Assign ( text->c_str(), Text::kEncoding_UTF8 );			
	SetResult ( *result_text, results );
}


void SetResult ( const WStringAutoPtr text, Data& results )
{
	TextAutoPtr result_text;
	result_text->AssignWide ( text->c_str() );			
	SetResult ( *result_text, results );
}

void SetResult ( vector<char> data, Data& results )
{
	data.push_back ( '\0' );
	StringAutoPtr data_string ( new string ( &data[0] ) );
	SetResult ( data_string, results );
	
}



void SetResult ( const string filename, const vector<char> data, Data& results )
{
	bool as_binary = !filename.empty();
	
	if ( as_binary ) {	// if a file name is supplied send back a file
		
		BinaryDataAutoPtr resultBinary;
		TextAutoPtr file;
		file->Assign ( filename.c_str(), Text::kEncoding_UTF8 );
		resultBinary->AddFNAMData ( *file ); 
		QuadCharAutoPtr data_type ( 'F', 'I', 'L', 'E' ); 
		resultBinary->Add ( *data_type, (FMX_UInt32)data.size(), (void *)&data[0] );
		results.SetBinaryData ( *resultBinary, true ); 
		
	} else { // otherwise try sending back text

		// filemaker will go into an infinite loop if non-utf8 data is set as utf8
		// so try to convert it first
		
		const string data_string ( data.begin(), data.end() );
		StringAutoPtr utf8 = ConvertTextToUTF8 ( (char *)data_string.c_str(), data_string.size(), g_text_encoding );
		SetResult ( utf8, results );
		
	}
	
} // SetBinaryDataResult


#pragma mark -
#pragma mark ParameterAs
#pragma mark -

bool ParameterAsBoolean ( const DataVect& parameters, const FMX_UInt32 which, const bool default_value )
{
	try {
		return parameters.AtAsBoolean ( which );
	} catch ( exception& /* e */ ) {
		return default_value;
	}
}
	
	
long ParameterAsLong ( const DataVect& parameters, const FMX_UInt32 which, const unsigned long default_value )
{
	try {
		return parameters.AtAsNumber ( which ).AsLong();
	} catch ( exception& /* e */ ) {
		return default_value;
	}	
}


StringAutoPtr ParameterAsUTF8String ( const DataVect& parameters, const FMX_UInt32 which )
{	
	
	StringAutoPtr result ( new string );
	
	try {
		
		TextAutoPtr raw_data;
		raw_data->SetText ( parameters.AtAsText ( which ) );
		
		FMX_UInt32 text_size = (4*(raw_data->GetSize())) + 1;
		char * text = new char [ text_size ]();
		raw_data->GetBytes ( text, text_size, 0, (FMX_UInt32)Text::kSize_End, Text::kEncoding_UTF8 );
		result->assign ( text );
		delete [] text;
		
	} catch ( exception& /* e */ ) {
		;	// return an empty string
	}
	
	return result;
	
} // ParameterAsUTF8String




WStringAutoPtr ParameterAsWideString ( const DataVect& parameters, const FMX_UInt32 which )
{	
	
	WStringAutoPtr result ( new wstring );
	
	try {
		
		TextAutoPtr raw_data;
		raw_data->SetText ( parameters.AtAsText(which) );
		
		FMX_Int32 text_size = raw_data->GetSize();
		FMX_UInt16 * text = new FMX_UInt16 [ text_size + 1 ];
		raw_data->GetUnicode ( text, 0, text_size );
        text[text_size] = 0x0000;

		// wchar_t is 4 bytes on OS X and 2 on Windows
		
		#if defined(FMX_MAC_TARGET)
		
			wchar_t * parameter = new wchar_t [ text_size + 1 ];
			for ( long i = 0 ; i <= text_size ; i++ ) {
				parameter[i] = (wchar_t)text[i];
			}
			delete [] text;
		
		#endif 
		
		#if defined(FMX_WIN_TARGET)
		
			wchar_t * parameter = (wchar_t*)text;
		
		#endif
		
		result->append ( parameter );
		delete [] parameter; // parameter == text on Windows
				
	} catch ( exception& /* e */ ) {
		;	// return an empty string
	}
	
	return result;
	
} // ParameterAsUnicodeString


void ParameterAsChar ( const DataVect& parameters, const FMX_UInt32 which, char ** char_data, FMX_UInt32& size )
{
//	errcode error = NoError();
	
	BinaryDataAutoPtr data ( parameters.AtAsBinaryData ( which ) );
		
	size = 0;
	char * buffer = NULL;
		
	int count = data->GetCount();
		
	if ( count > 0 ) {
			
		// when it's a file or a sound it's easy
			
		QuadCharAutoPtr data_type ( 'F', 'I', 'L', 'E' );
		int which_type = data->GetIndex ( *data_type );
		if ( which_type == kBE_DataType_Not_Found ) {
			QuadCharAutoPtr sound_type ( 's', 'n', 'd', ' ' );
			which_type = data->GetIndex ( *sound_type );
		}
			
		// try and guess which image format to try
			
		if ( which_type == kBE_DataType_Not_Found ) {
				
			// non-image data streams
			QuadCharAutoPtr dpi__type ( 'D', 'P', 'I', '_' );
			QuadCharAutoPtr fnam_type ( 'F', 'N', 'A', 'M' );
			QuadCharAutoPtr size_type ( 'S', 'I', 'Z', 'E' );
				
			for ( int i = 0 ; i < count ; i++ ) {
				QuadCharAutoPtr e_type;
				data->GetType ( i, *e_type );
				if ( *e_type != *dpi__type && *e_type != *fnam_type && *e_type != *size_type ) {
					which_type = i;
						
					// don't overwrite another type with an fm generated jpeg preview
					QuadCharAutoPtr jpeg_type ( 'J', 'P', 'E', 'G' );
					if ( *e_type != *jpeg_type ) {
						break;
					}
					
				}
			}
			
		}
			
			
		if ( which_type != kBE_DataType_Not_Found ) {
			size = data->GetSize ( which_type );
			buffer = new char [ size ];
			data->GetData ( which_type, 0, size, (void *)buffer );
		} else {
			g_last_error = kRequestedDataIsMissingError;
		}
			
	} else {
			
		// if we don't have any streams try getting as text
		// note: we also end up here for anything inserted as QuickTime, which is probably not what the user wants, but...
			
		StringAutoPtr text = ParameterAsUTF8String ( parameters, which );
		size = (FMX_UInt32)text->size();
		buffer = new char [ size ];
		memcpy ( buffer, text->c_str(), size );
		
	}
		
		*char_data = buffer;
		
//		delete [] buffer;
		
	
} // ParameterAsChar



#pragma mark -
#pragma mark Files
#pragma mark -

StringAutoPtr ReadFileAsUTF8 ( WStringAutoPtr path )
{
	
	boost::filesystem::path filesystem_path = *path;
	
	StringAutoPtr result ( new string );
	
	if ( exists ( filesystem_path ) ) {
		size_t length = (size_t)file_size ( filesystem_path ); // boost::uintmax_t
		
		boost::filesystem::ifstream inFile ( filesystem_path, ios_base::in | ios_base::binary | ios_base::ate );
		inFile.seekg ( 0, ios::beg );
		
		// slurp up the file contents
		char * buffer = new char [length];
		inFile.read ( buffer, length );
		inFile.close ();
				
		// convert the text in the file to utf-8 if possible
		result = ConvertTextToUTF8 ( buffer, length, g_text_encoding );
		if ( result->length() == 0 ) {
			result->assign ( buffer );
		}
		delete [] buffer;
	} else {
		g_last_error = kNoSuchFileOrDirectoryError;
	}
	
	return result;
	
} // ReadFileAsUTF8



#pragma mark -
#pragma mark Unicode
#pragma mark -

vector<char> ConvertTextEncoding ( char * in, const size_t length, const string& to, const string& from )
{
	size_t available = (length * 4) + 1;	// worst case for utf-32 to utf-8 ?
	char * encoded = new char [available]();	// value-initialization (to zero)… we crash otherwise
	
	vector<string> codesets;
	if ( from != UTF8 ) {
		codesets.push_back ( from );
	}
	codesets.push_back ( UTF8 );
	codesets.push_back ( UTF16 ); // backwards compatibility with v1.2
	
	/*
	 there no clean way to determine the codeset of the supplied text so
	 try each converting from each of the codesets in turn
	 */
	
	const size_t kIconvError = -1;
	size_t error_result = kIconvError;
	vector<string>::iterator it = codesets.begin();
	size_t remaining = available;
	
	while ( error_result == kIconvError && it != codesets.end() ) {
		
		char * start = in;
		size_t start_length = length;
		char * encoded_start = encoded;
		
		iconv_t conversion = iconv_open ( to.c_str(), it->c_str() );
		if ( conversion != (iconv_t)kIconvError ) {
			error_result = iconv ( conversion, &start, &start_length, &encoded_start, &remaining );
			iconv_close ( conversion );
		} else {
			error_result = errno;
		}

		++it;
	}

	g_last_error = error_result;

	vector<char> out ( encoded, encoded + available - remaining );
	delete[] encoded;
	
	return out;
	
} // ConvertTextEncoding



StringAutoPtr ConvertTextEncoding ( StringAutoPtr in, const string& to, const std::string& from )
{
	vector<char> text = ConvertTextEncoding ( (char *)in->c_str(), (const size_t)in->size() - 1, to, from );
	StringAutoPtr out ( new string ( text.begin(), text.end() ) );
	return out;
}


// convert text to utf-8
// currently handles utf-16, ascii and utf-8 text

StringAutoPtr ConvertTextToUTF8 ( char * in, const size_t length, const std::string& from )
{
	vector<char> text = ConvertTextEncoding ( in, length, UTF8, from );
	StringAutoPtr out ( new string ( text.begin(), text.end() ) );
	return out;
} // ConvertToUTF8


void SetTextEncoding ( const string& encoding )
{
	if ( encoding.empty() ) {
		g_text_encoding = UTF8;
	} else {
		g_text_encoding = encoding;
	}
}



string TextAsUTF8String ( const Text& fmx_text )
{
	
	string result;
	
	try {
		
		FMX_UInt32 text_size = (2*(fmx_text.GetSize())) + 1;
		char * text = new char [ text_size ]();
		fmx_text.GetBytes ( text, text_size, 0, (FMX_UInt32)Text::kSize_End, Text::kEncoding_UTF8 );
		result.assign ( text );
		delete [] text;
		
	} catch ( exception& /* e */ ) {
		;	// return an empty string
	}
	
	return result;
	
} // TextAsString



string DataAsUTF8String ( const Data& data )
{
	return TextAsUTF8String ( data.GetAsText() );	
}




long DataAsLong ( const Data& data )
{
	FixPtAutoPtr number;
	number->AssignFixPt ( data.GetAsNumber() );
	
	return ( number->AsLong() );
}



double DataAsDouble ( const Data& data )
{
	FixPtAutoPtr number;
	number->AssignFixPt ( data.GetAsNumber() );
	
	return ( number->AsFloat() );
}


#pragma mark -
#pragma mark Errors
#pragma mark -

// Should be called at the start of each plugin function to clear out the global error

errcode NoError ( void )
{
	g_last_ddl_error = kNoError;
	
	g_last_error = kNoError;
	return g_last_error;
}

// use as the return value for al plugin functions
// only return errors to FileMaker that it can make sense of

errcode MapError ( const errcode error, const bool map )
{
	
	errcode mapped_error = kNoError;
	
	// map the error to a FileMaker error (so that FMP can display an
	// appropriate dialog etc.
	
	if ( map ) {
		
		mapped_error = error;
		g_last_error = error;

	} else {
		
		// map all other errors to "unknown"
		if ( error != kNoError || g_last_error != kNoError ) {
			mapped_error = kErrorUnknown;
		}

		// overwrite g_last_error, but only if it's not already set
		if ( g_last_error == kNoError ) {
			g_last_error = error;
		}

	}

	return mapped_error;
}


#pragma mark -
#pragma mark Other
#pragma mark -


bool AllowUserAbort ( const ExprEnv& environment )
{
	TextAutoPtr command;
	command->Assign ( "Get ( AllowAbortState )" );
	
	DataAutoPtr reply;
	environment.Evaluate ( *command, *reply );
	bool allow_abort = reply->GetAsBoolean();

	return allow_abort;
}


/*
 The following functions are modifications of ones included in the Example plug-in
 shipped by FileMaker Inc. with the External Plug-In API on the CDs for 
 FileMaker Developer/Pro Advance versions 7 through 10 and are used by permission.
 */

void Do_GetString(unsigned long whichString, FMX_PtrType /* winLangID */, FMX_PtrType resultsize, FMX_Unichar* string)
{
	
	switch ( whichString )
	{
		case kFMXT_OptionsStr:
		{
#if defined(FMX_WIN_TARGET)
			LoadStringW( (HINSTANCE)(gFMX_ExternCallPtr->instanceID), kBE_OptionsStringID, (LPWSTR)string, (uint32)resultsize);
#endif
			
#if defined(FMX_MAC_TARGET)
			Sub_OSXLoadString(kBE_OptionsStringID, string, resultsize);
#endif

//			processedSpecialStringID = true;
			break;
		}
		default:
#if defined(FMX_WIN_TARGET)
			LoadStringW( (HINSTANCE)(gFMX_ExternCallPtr->instanceID), (unsigned int)whichString, (LPWSTR)string, (uint32)resultsize);
#endif
				
#if defined(FMX_MAC_TARGET)
			Sub_OSXLoadString(whichString, string, resultsize);
#endif
	
	} // switch (whichString)
	
	
} // Do_GetString (FMX_Unichar* version)


void Do_GetString(unsigned long whichStringID, TextAutoPtr& intoHere, bool stripFunctionParams)
{
	FMX_Unichar			tempBuffer[kBE_GetStringMaxBufferSize];
	
	Do_GetString ( whichStringID, 0, kBE_GetStringMaxBufferSize, tempBuffer );
	intoHere->AssignUnicode(tempBuffer);
	
	if(stripFunctionParams) {
		
		// The string for this whichStringID is a Function Prototype, but all the plug-in needs now is the Function Name by itself.
		
		TextAutoPtr		parenToken;
		parenToken->Assign ( " (" );
		
		FMX_UInt32 originalSize = intoHere->GetSize();
		FMX_UInt32 firstParenLocation;
		firstParenLocation = intoHere->Find(*parenToken, 0);
		
		intoHere->DeleteText(firstParenLocation, originalSize-firstParenLocation);
		
	} // stripFunctionParams
	
} // Do_GetString (TextAutoPtr version)



#if defined(FMX_MAC_TARGET)

unsigned long Sub_OSXLoadString(unsigned long stringID, FMX_Unichar* intoHere, long intoHereMax)
{
	unsigned long returnResult = 0;
	
	if( (intoHere != NULL) && (intoHereMax > 1) ) {
		// Turn stringID to a textual identifier, then get the string from the .strings file as a null-term unichar array.
		CFStringRef 	strIdStr = CFStringCreateWithFormat( kCFAllocatorDefault, NULL, CFSTR("%ld"), stringID );
		
		// Note: The plug-in must be explicit about the bundle and file it wants to pull the string from.
		CFStringRef 	osxStr = CFBundleCopyLocalizedString( reinterpret_cast<CFBundleRef>(gFMX_ExternCallPtr->instanceID), strIdStr, strIdStr, CFSTR( PLUGIN_NAME ) );
		
		if((osxStr != NULL) && (osxStr != strIdStr)) {
			
			long	osxStrLen = CFStringGetLength(osxStr);
			
			if( osxStrLen < (intoHereMax-1) ) {
				
				CFRange		allChars;
				allChars.location = 0;
				allChars.length = osxStrLen;
				
				CFStringGetCharacters(osxStr, allChars, (UniChar*)(intoHere));
				intoHere[osxStrLen] = 0x0000;
				returnResult = (unsigned long)osxStrLen;
				
			} // osxStrLen
			
		} // osxStr
		
		if ( osxStr ) { CFRelease( osxStr ); }
		CFRelease( strIdStr );
		
	} // intoHere
	
	return(returnResult);
	
} // Sub_OSXLoadString

#endif


