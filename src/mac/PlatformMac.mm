#include "../common/Platform.h"
#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>

namespace plat
{
void InitThread( Apartment ) {}
void UninitThread( Apartment ) {}

bool IsPowerPointInstalled()
{
	@autoreleasepool
	{
		NSURL* url = [[NSWorkspace sharedWorkspace] URLForApplicationWithBundleIdentifier:@"com.microsoft.Powerpoint"];
		return url != nil;
	}
}
}// namespace plat
