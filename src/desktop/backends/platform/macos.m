#include <AvailabilityMacros.h>
#include <AppKit/AppKit.h>

#if MAC_OS_X_VERSION_MAX_ALLOWED >= 1030 // NSAlert is available in 10.3 and later

static void show_error_box_nsalert(const char *message)
{
    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
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
    [pool drain];
}

#else

static void show_error_box_nsrunalertpanel(const char *message)
{
    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];

    NSRunAlertPanel(
        @"Error",
        [NSString stringWithUTF8String:message],
        @"OK",
        nil,
        nil
    );

    [pool drain];
}

#endif

void show_error_box(const char *message)
{
#if MAC_OS_X_VERSION_MAX_ALLOWED >= 1030
    show_error_box_nsalert(message);
#else
    show_error_box_nsrunalertpanel(message);
#endif
}