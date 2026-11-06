#include "DocumentPrep.h"
#include "Platform.h"
#include <cstring>

bool IsPdfExtension( const std::string& ext )
{
	return ext == "pdf";
}

bool IsPowerPointExtension( const std::string& ext )
{
	return ext == "ppt" || ext == "pptx" || ext == "pptm" || ext == "pps" || ext == "ppsx" || ext == "ppsm" || ext == "pot" || ext == "potx" || ext == "odp";
}

static bool IsIdChar( char c )
{
	return ( c >= 'a' && c <= 'z' ) || ( c >= 'A' && c <= 'Z' ) || ( c >= '0' && c <= '9' ) || c == '-' || c == '_';
}

std::string GoogleSlidesPdfUrl( const std::string& link, std::string* error )
{
	const std::string marker = "/presentation/d/";
	size_t p                 = link.find( marker );
	if( p == std::string::npos )
	{
		if( error )
			*error = "Not a Google Slides link (expected docs.google.com/presentation/d/...)";
		return std::string();
	}
	p += marker.size();
	if( link.compare( p, 2, "e/" ) == 0 )
	{
		if( error )
			*error = "'Publish to web' links can't be exported. Use the normal Share link (Anyone with the link)";
		return std::string();
	}
	size_t end = p;
	while( end < link.size() && IsIdChar( link[ end ] ) )
		++end;
	if( end - p < 10 )
	{
		if( error )
			*error = "Could not find the presentation id in the link";
		return std::string();
	}
	return "https://docs.google.com/presentation/d/" + link.substr( p, end - p ) + "/export/pdf";
}

static bool LooksLikePdf( const std::string& path )
{
	char head[ 5 ] = {};
	return plat::ReadFileHead( path, head, 4 ) && std::memcmp( head, "%PDF", 4 ) == 0;
}

static std::string ToFileUrl( std::string path )
{
	for( auto& c : path )
		if( c == '\\' )
			c = '/';
	std::string out;
	for( unsigned char c : path )
	{
		if( c == ' ' )
			out += "%20";
		else
			out += (char)c;
	}
	if( !out.empty() && out[ 0 ] == '/' )
		return "file://" + out;
	return "file:///" + out;
}

static bool DownloadGoogleSlides( const PrepareRequest& req, const std::string& outPdf, std::string& error, const StatusFn& status )
{
	std::string url = GoogleSlidesPdfUrl( req.input, &error );
	if( url.empty() )
		return false;
	std::string curl = plat::FindCurl();
	if( curl.empty() )
	{
		error = "curl not found (needed to download Google Slides)";
		return false;
	}
	status( "Downloading Google Slides..." );
	std::string tmp = outPdf + ".part";
	plat::RemoveFile( tmp );
	auto r = plat::RunProcess( { curl, "-L", "-f", "-s", "-S", "--max-time", "180", "-o", tmp, url }, 200000, req.cancelled );
	if( !r.started )
	{
		error = "Could not start curl";
		return false;
	}
	if( r.cancelled )
	{
		plat::RemoveFile( tmp );
		error = "cancelled";
		return false;
	}
	if( r.timedOut || r.exitCode != 0 || !LooksLikePdf( tmp ) )
	{
		plat::RemoveFile( tmp );
		error = r.timedOut ? "Download timed out" : "Download failed. Is the presentation shared as 'Anyone with the link can view'?";
		return false;
	}
	plat::RemoveFile( outPdf );
	if( std::rename( tmp.c_str(), outPdf.c_str() ) != 0 )
	{
		error = "Could not write " + outPdf;
		return false;
	}
	return true;
}

static bool ConvertWithLibreOffice( const PrepareRequest& req, const std::string& workDir, std::string& outPdf, std::string& error, const StatusFn& status )
{
	std::string soffice = plat::FindLibreOffice();
	if( soffice.empty() )
	{
		error = "LibreOffice not found";
		return false;
	}
	status( "Converting with LibreOffice..." );
	std::string profile = plat::JoinPath( plat::CacheDir(), "lo_profile" );
	auto r              = plat::RunProcess( { soffice,
                                 "-env:UserInstallation=" + ToFileUrl( profile ),
                                 "--headless",
                                 "--norestore",
                                 "--nologo",
                                 "--convert-to",
                                 "pdf",
                                 "--outdir",
                                 workDir,
                                 req.input },
                               240000, req.cancelled );
	std::string produced = plat::JoinPath( workDir, plat::ReplaceExtension( plat::FileName( req.input ), "pdf" ) );
	if( r.cancelled )
	{
		error = "cancelled";
		return false;
	}
	if( !r.started || r.timedOut || !LooksLikePdf( produced ) )
	{
		error = r.timedOut ? "LibreOffice conversion timed out" : "LibreOffice conversion failed";
		return false;
	}
	outPdf = produced;
	return true;
}

bool PrepareStaticPdf( const PrepareRequest& req, std::string& pdfPathOut, std::string& error, const StatusFn& status )
{
	if( req.input.empty() )
	{
		error = req.isUrl ? "No Google Slides link set" : "No file selected";
		return false;
	}

	if( req.isUrl )
	{
		std::string pdf = plat::JoinPath( plat::CacheDir(), "gslides_" + plat::Hex64( plat::Hash64( req.input ) ) + ".pdf" );
		if( !req.forceRefresh && LooksLikePdf( pdf ) )
		{
			pdfPathOut = pdf;
			return true;
		}
		if( !DownloadGoogleSlides( req, pdf, error, status ) )
			return false;
		pdfPathOut = pdf;
		return true;
	}

	uint64_t size = 0;
	int64_t mtime = 0;
	if( !plat::FileStamp( req.input, size, mtime ) )
	{
		error = "File not found: " + req.input;
		return false;
	}

	std::string ext = plat::Extension( req.input );
	if( IsPdfExtension( ext ) )
	{
		pdfPathOut = req.input;
		return true;
	}
	if( !IsPowerPointExtension( ext ) )
	{
		error = "Unsupported file type: ." + ext;
		return false;
	}

	// Cache key: path + size + mtime, so edits to the deck are picked up.
	std::string key     = req.input + "|" + std::to_string( size ) + "|" + std::to_string( mtime );
	std::string workDir = plat::JoinPath( plat::CacheDir(), "deck_" + plat::Hex64( plat::Hash64( key ) ) );
	plat::CreateDir( workDir );
	std::string cached = plat::JoinPath( workDir, plat::ReplaceExtension( plat::FileName( req.input ), "pdf" ) );
	if( !req.forceRefresh && LooksLikePdf( cached ) )
	{
		pdfPathOut = cached;
		return true;
	}

	bool havePowerPoint  = plat::IsPowerPointInstalled() && req.exportWithPowerPoint;
	bool haveLibreOffice = !plat::FindLibreOffice().empty();

	ConverterChoice conv = req.converter;
	bool autoChosen      = conv == ConverterChoice::Auto;
	if( conv == ConverterChoice::Auto )
	{
		if( havePowerPoint )
			conv = ConverterChoice::PowerPoint;
		else if( haveLibreOffice )
			conv = ConverterChoice::LibreOffice;
		else
		{
			error = "To show PPT files install PowerPoint or LibreOffice (free), or export the deck to PDF";
			return false;
		}
	}

	if( conv == ConverterChoice::PowerPoint )
	{
		if( !havePowerPoint )
		{
			error = "PowerPoint is not installed. Choose Converter: LibreOffice";
			return false;
		}
		status( "Converting with PowerPoint..." );
		std::string err;
		if( !req.exportWithPowerPoint( req.input, cached, &err ) || !LooksLikePdf( cached ) )
		{
			if( autoChosen && haveLibreOffice )
				return ConvertWithLibreOffice( req, workDir, pdfPathOut, error, status );
			error = "PowerPoint export failed" + ( err.empty() ? std::string() : ": " + err );
			return false;
		}
		pdfPathOut = cached;
		return true;
	}

	if( !haveLibreOffice )
	{
		error = "LibreOffice is not installed (libreoffice.org). Or choose Converter: PowerPoint";
		return false;
	}
	return ConvertWithLibreOffice( req, workDir, pdfPathOut, error, status );
}
