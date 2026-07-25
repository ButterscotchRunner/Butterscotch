#include <AvailabilityMacros.h>
#include <AppKit/AppKit.h>

static void show_error_box(const char *message)
{
    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];

#if MAC_OS_X_VERSION_MAX_ALLOWED >= 1030 // NSAlert is available in 10.3 and later

    NSAlert *alert = [[NSAlert alloc] init];

    [alert setMessageText:@"Error"];
    [alert setInformativeText:[NSString stringWithUTF8String:message]];
    [alert addButtonWithTitle:@"OK"];
#if MAC_OS_X_VERSION_MAX_ALLOWED >= 101200 // NSAlertStyleCritical is available in 10.12 and later
        [alert setAlertStyle:NSAlertStyleCritical];
#else
        [alert setAlertStyle:NSCriticalAlertStyle];
#endif

    [alert runModal];
    [alert release];

#else

    NSRunAlertPanel(
        @"Error",
        [NSString stringWithUTF8String:message],
        @"OK",
        nil,
        nil
    );

#endif

    [pool drain];
}